module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <vector>
#include <windows.h>
#include <concurrentqueue/moodycamel/blockingconcurrentqueue.h>

module draw3.contact_input;

namespace draw3
{
	namespace
	{
		constexpr size_t kContactSlotsPerBlock = 32;
		constexpr size_t kIngressQueueCapacity = 256;
		constexpr uint64_t kMaxProducerGeneration = (~uint64_t{ 0 }) >> 3;

		enum class ProducerState : uint32_t
		{
			Free,
			Initializing,
			Producing,
			Closing,
			ConsumerOwned
		};

		constexpr uint64_t kProducerStateMask = 0x7;

		uint64_t MakeProducerRoute(uint64_t generation, ProducerState state) noexcept
		{
			return (generation << 3) | static_cast<uint64_t>(state);
		}

		uint64_t RouteGeneration(uint64_t route) noexcept
		{
			return route >> 3;
		}

		ProducerState RouteState(uint64_t route) noexcept
		{
			return static_cast<ProducerState>(route & kProducerStateMask);
		}

		struct LowPowerQueueTraits : moodycamel::ConcurrentQueueDefaultTraits
		{
			static constexpr int MAX_SEMA_SPINS = 0;
		};

		using IngressQueue = moodycamel::BlockingConcurrentQueue<IngressCommand, LowPowerQueueTraits>;

		struct ContactBlock
		{
			std::array<ContactRecord, kContactSlotsPerBlock> records;
			std::atomic<ContactBlock*> next = nullptr;
		};

		struct LocatedContact
		{
			ContactRecord* record = nullptr;
			uint64_t generation = 0;

			explicit operator bool() const noexcept { return record != nullptr; }
		};
	}

	struct ContactRecordAccess
	{
		static ProducerState State(const ContactRecord& record) noexcept
		{
			return RouteState(record.producerRoute_.Load());
		}

		static bool TrySetState(ContactRecord& record, ProducerState expected, ProducerState desired) noexcept
		{
			uint64_t expectedRoute = record.producerRoute_.Load();
			if (RouteState(expectedRoute) != expected) return false;
			return record.producerRoute_.CompareExchangeStrong(expectedRoute,
				MakeProducerRoute(RouteGeneration(expectedRoute), desired));
		}

		static bool TrySetExactState(ContactRecord& record, uint64_t generation,
			ProducerState expected, ProducerState desired) noexcept
		{
			uint64_t expectedRoute = MakeProducerRoute(generation, expected);
			return record.producerRoute_.CompareExchangeStrong(
				expectedRoute, MakeProducerRoute(generation, desired));
		}

		static void SetState(ContactRecord& record, ProducerState state) noexcept
		{
			const uint64_t route = record.producerRoute_.Load();
			record.producerRoute_.Store(MakeProducerRoute(RouteGeneration(route), state));
		}

		static void SetRoute(ContactRecord& record, uint64_t generation, ProducerState state) noexcept
		{
			record.producerRoute_.Store(MakeProducerRoute(generation, state));
		}

		static bool HasRoute(const ContactRecord& record, uint64_t generation,
			ProducerState state) noexcept
		{
			return record.producerRoute_.Load() == MakeProducerRoute(generation, state);
		}

		static void Initialize(ContactRecord& record, uint32_t tabletContextId, uint32_t contactId,
			InputDeviceType deviceType, ContactSnapshot snapshot, uint64_t generation) noexcept
		{
			record.tabletContextId_.Store(tabletContextId);
			record.contactId_.Store(contactId);
			record.deviceType_.Store(static_cast<uint32_t>(deviceType));
			snapshot.phase = ContactPhase::Down;
			snapshot.sequence = 2;
			record.downSnapshot_ = snapshot;
			record.writerLatch_.clear(std::memory_order_release);
			record.x_.Store(snapshot.position.x);
			record.y_.Store(snapshot.position.y);
			record.pressure_.Store(snapshot.pressure);
			record.width_.Store(snapshot.contactSize.width);
			record.height_.Store(snapshot.contactSize.height);
			record.qpc_.Store(snapshot.qpc);
			record.phase_.Store(static_cast<uint32_t>(ContactPhase::Down));
			record.sequence_.Store(snapshot.sequence);
			SetRoute(record, generation, ProducerState::Initializing);
		}

		static bool Matches(const ContactRecord& record, uint32_t tabletContextId, uint32_t contactId) noexcept
		{
			return record.tabletContextId_.Load() == tabletContextId &&
				record.contactId_.Load() == contactId;
		}

		static uint64_t Generation(const ContactRecord& record) noexcept
		{
			return RouteGeneration(record.producerRoute_.Load());
		}

		static bool TryLockWriter(ContactRecord& record) noexcept
		{
			return !record.writerLatch_.test_and_set(std::memory_order_acquire);
		}

		static void LockWriter(ContactRecord& record) noexcept
		{
			while (record.writerLatch_.test_and_set(std::memory_order_acquire))
				YieldProcessor();
		}

		static void UnlockWriter(ContactRecord& record) noexcept
		{
			record.writerLatch_.clear(std::memory_order_release);
		}

		static void PublishSnapshot(ContactRecord& record, ContactSnapshot snapshot) noexcept
		{
			uint64_t sequence = record.sequence_.Load();
			if ((sequence & 1u) != 0) ++sequence;
			record.sequence_.Store(sequence + 1); // 奇数表示多字段正在更新。
			record.x_.Store(snapshot.position.x);
			record.y_.Store(snapshot.position.y);
			record.pressure_.Store(snapshot.pressure);
			record.width_.Store(snapshot.contactSize.width);
			record.height_.Store(snapshot.contactSize.height);
			record.qpc_.Store(snapshot.qpc);
			record.phase_.Store(static_cast<uint32_t>(snapshot.phase));
			record.sequence_.Store(sequence + 2); // 偶数一次性发布一致终态。
		}

		static bool ReadSnapshot(const ContactRecord& record, ContactSnapshot& snapshot) noexcept
		{
			for (int attempt = 0; attempt < 32; ++attempt)
			{
				const uint64_t sequenceBefore = record.sequence_.Load();
				if ((sequenceBefore & 1u) != 0)
				{
					YieldProcessor();
					continue;
				}

				ContactSnapshot candidate;
				candidate.position.x = record.x_.Load();
				candidate.position.y = record.y_.Load();
				candidate.pressure = record.pressure_.Load();
				candidate.contactSize.width = record.width_.Load();
				candidate.contactSize.height = record.height_.Load();
				candidate.qpc = record.qpc_.Load();
				candidate.phase = static_cast<ContactPhase>(record.phase_.Load());
				const uint64_t sequenceAfter = record.sequence_.Load();
				if (sequenceBefore == sequenceAfter && (sequenceAfter & 1u) == 0)
				{
					candidate.sequence = sequenceAfter;
					snapshot = candidate;
					return true;
				}
			}
			return false;
		}
	};

	struct ContactInputCoordinatorImpl
	{
		ContactInputCoordinatorImpl() : queue(kIngressQueueCapacity)
		{
			auto firstBlock = std::make_unique<ContactBlock>();
			blockHead.store(firstBlock.get(), std::memory_order_release);
			blocks.push_back(std::move(firstBlock)); // 首批 32 个稳定 slot 在 RTS 启用前完成预分配。
		}

		LocatedContact FindProducing(uint32_t tabletContextId, uint32_t contactId) const noexcept
		{
			for (ContactBlock* block = blockHead.load(std::memory_order_acquire); block;
				block = block->next.load(std::memory_order_acquire))
			{
				for (ContactRecord& record : block->records)
				{
					const uint64_t generation = ContactRecordAccess::Generation(record);
					if (ContactRecordAccess::HasRoute(record, generation, ProducerState::Producing) &&
						ContactRecordAccess::Matches(record, tabletContextId, contactId))
						return LocatedContact{ &record, generation };
				}
			}
			return {};
		}

		ContactRecord* AcquireFreeSlot()
		{
			std::lock_guard lock(allocationMutex);
			for (const auto& block : blocks)
			{
				for (ContactRecord& record : block->records)
				{
					if (ContactRecordAccess::TrySetState(
						record, ProducerState::Free, ProducerState::Initializing)) return &record;
				}
			}

			auto newBlock = std::make_unique<ContactBlock>();
			ContactRecord* record = &newBlock->records.front();
			ContactRecordAccess::SetState(*record, ProducerState::Initializing);
			ContactBlock* oldHead = blockHead.load(std::memory_order_relaxed);
			newBlock->next.store(oldHead, std::memory_order_relaxed);
			ContactBlock* publishedBlock = newBlock.get();
			blocks.push_back(std::move(newBlock));
			blockHead.store(publishedBlock, std::memory_order_release); // 块链只追加，热路径无需哈希或锁。
			return record;
		}

		bool EnqueueReliably(const IngressCommand& command)
		{
			return queue.try_enqueue(command) || queue.enqueue(command);
		}

		void AbortUnqueuedDown(ContactRecord& record, uint64_t generation) noexcept
		{
			for (;;)
			{
				if (ContactRecordAccess::TrySetExactState(record, generation,
					ProducerState::Producing, ProducerState::Closing))
				{
					// 先关 route 并等已进入的 Move 退出，之后才允许该 slot 再次被取得。
					ContactRecordAccess::LockWriter(record);
					ContactRecordAccess::UnlockWriter(record);
					ContactRecordAccess::TrySetExactState(record, generation,
						ProducerState::Closing, ProducerState::Free);
					return;
				}
				if (ContactRecordAccess::TrySetExactState(record, generation,
					ProducerState::ConsumerOwned, ProducerState::Free)) return;

				if (ContactRecordAccess::Generation(record) != generation ||
					ContactRecordAccess::State(record) == ProducerState::Free) return;
				YieldProcessor(); // 并发 Up/Disabled 已抢先关闭时，等待其发布不可逆终态。
			}
		}

		bool Close(ContactRecord& record, uint64_t expectedGeneration,
			uint32_t tabletContextId, uint32_t contactId,
			ContactSnapshot snapshot, ContactPhase phase) noexcept
		{
			if (!ContactRecordAccess::Matches(record, tabletContextId, contactId) ||
				!ContactRecordAccess::TrySetExactState(record, expectedGeneration,
					ProducerState::Producing, ProducerState::Closing)) return false;
			ContactRecordAccess::LockWriter(record); // Up 先关路由，再等待正在发布的 Move 退出。
			if (!ContactRecordAccess::HasRoute(record, expectedGeneration, ProducerState::Closing) ||
				!ContactRecordAccess::Matches(record, tabletContextId, contactId))
			{
				ContactRecordAccess::UnlockWriter(record);
				return false;
			}
			snapshot.phase = phase;
			ContactRecordAccess::PublishSnapshot(record, snapshot);
			ContactRecordAccess::UnlockWriter(record);
			return ContactRecordAccess::TrySetExactState(record, expectedGeneration,
				ProducerState::Closing, ProducerState::ConsumerOwned);
		}

		IngressQueue queue;
		std::atomic<bool> controlWakePending = false;
		std::atomic<ContactBlock*> blockHead = nullptr;
		std::mutex allocationMutex;
		std::vector<std::unique_ptr<ContactBlock>> blocks;
	};

	uint32_t ContactRecord::TabletContextId() const noexcept
	{
		return tabletContextId_.Load();
	}

	uint32_t ContactRecord::ContactId() const noexcept
	{
		return contactId_.Load();
	}

	InputDeviceType ContactRecord::DeviceType() const noexcept
	{
		return static_cast<InputDeviceType>(deviceType_.Load());
	}

	const ContactSnapshot& ContactRecord::DownSnapshot() const noexcept
	{
		return downSnapshot_;
	}

	uint64_t ContactRecord::Generation() const noexcept
	{
		return ContactRecordAccess::Generation(*this);
	}

	ContactInputCoordinator::ContactInputCoordinator()
		: impl_(std::make_unique<ContactInputCoordinatorImpl>())
	{
	}

	ContactInputCoordinator::~ContactInputCoordinator() = default;

	bool ContactInputCoordinator::PublishDown(uint32_t tabletContextId, uint32_t contactId,
		InputDeviceType deviceType, const ContactSnapshot& snapshot)
	{
		ContactRecord* record = nullptr;
		try
		{
			record = impl_->AcquireFreeSlot();
		}
		catch (const std::bad_alloc&)
		{
			return false;
		}
		if (!record) return false;

		const uint64_t previousGeneration = ContactRecordAccess::Generation(*record);
		const uint64_t generation = previousGeneration >= kMaxProducerGeneration
			? 1 : previousGeneration + 1;
		ContactRecordAccess::Initialize(
			*record, tabletContextId, contactId, deviceType, snapshot, generation);
		ContactRecordAccess::SetState(*record, ProducerState::Producing);
		const IngressCommand command{ IngressCommandKind::Down, ContactHandle{ record, generation } };
		try
		{
			if (impl_->EnqueueReliably(command)) return true;
		}
		catch (const std::bad_alloc&)
		{
		}
		impl_->AbortUnqueuedDown(*record, generation); // 入队失败也不能与已经进入的 Move/Up 争用复用。
		return false;
	}

	bool ContactInputCoordinator::PublishMove(uint32_t tabletContextId, uint32_t contactId,
		const ContactSnapshot& snapshot) noexcept
	{
		const LocatedContact located = impl_->FindProducing(tabletContextId, contactId);
		if (!located || !ContactRecordAccess::TryLockWriter(*located.record)) return false;
		if (!ContactRecordAccess::HasRoute(
			*located.record, located.generation, ProducerState::Producing) ||
			!ContactRecordAccess::Matches(*located.record, tabletContextId, contactId))
		{
			ContactRecordAccess::UnlockWriter(*located.record);
			return false;
		}
		ContactSnapshot moveSnapshot = snapshot;
		moveSnapshot.phase = ContactPhase::Move;
		ContactRecordAccess::PublishSnapshot(*located.record, moveSnapshot);
		const bool routeStillMatches = ContactRecordAccess::HasRoute(
			*located.record, located.generation, ProducerState::Producing);
		ContactRecordAccess::UnlockWriter(*located.record);
		if (!routeStillMatches) return false;
		return true;
	}

	bool ContactInputCoordinator::PublishUp(uint32_t tabletContextId, uint32_t contactId,
		const ContactSnapshot& snapshot) noexcept
	{
		const LocatedContact located = impl_->FindProducing(tabletContextId, contactId);
		return located && impl_->Close(*located.record, located.generation,
			tabletContextId, contactId, snapshot, ContactPhase::Up);
	}

	bool ContactInputCoordinator::PublishCancelled(uint32_t tabletContextId, uint32_t contactId,
		const ContactSnapshot& snapshot) noexcept
	{
		const LocatedContact located = impl_->FindProducing(tabletContextId, contactId);
		return located && impl_->Close(*located.record, located.generation,
			tabletContextId, contactId, snapshot, ContactPhase::Cancelled);
	}

	bool ContactInputCoordinator::TryReadSnapshot(ContactHandle handle, ContactSnapshot& snapshot) const noexcept
	{
		if (!handle.record || ContactRecordAccess::Generation(*handle.record) != handle.generation) return false;
		const ProducerState state = ContactRecordAccess::State(*handle.record);
		if (state == ProducerState::Free || state == ProducerState::Initializing) return false;
		ContactSnapshot candidate;
		if (!ContactRecordAccess::ReadSnapshot(*handle.record, candidate) ||
			ContactRecordAccess::Generation(*handle.record) != handle.generation) return false;
		const ProducerState stateAfter = ContactRecordAccess::State(*handle.record);
		if ((candidate.phase == ContactPhase::Up || candidate.phase == ContactPhase::Cancelled) &&
			stateAfter != ProducerState::ConsumerOwned) return false;
		snapshot = candidate;
		return true;
	}

	void ContactInputCoordinator::Recycle(ContactHandle handle) noexcept
	{
		if (!handle.record) return;
		for (;;)
		{
			if (ContactRecordAccess::TrySetExactState(
				*handle.record, handle.generation, ProducerState::ConsumerOwned, ProducerState::Free)) return;
			if (ContactRecordAccess::Generation(*handle.record) != handle.generation ||
				ContactRecordAccess::State(*handle.record) != ProducerState::Closing) return;
			YieldProcessor(); // 初始化失败与并发取消重叠时，等关闭者交出 consumer ownership。
		}
	}

	void ContactInputCoordinator::CloseAllProducerContacts(int64_t qpc) noexcept
	{
		for (ContactBlock* block = impl_->blockHead.load(std::memory_order_acquire); block;
			block = block->next.load(std::memory_order_acquire))
		{
			for (ContactRecord& record : block->records)
			{
				const uint64_t generation = ContactRecordAccess::Generation(record);
				while (ContactRecordAccess::HasRoute(record, generation, ProducerState::Producing))
				{
					ContactSnapshot snapshot;
					if (!ContactRecordAccess::ReadSnapshot(record, snapshot))
					{
						YieldProcessor();
						continue;
					}
					snapshot.qpc = qpc;
					impl_->Close(record, generation, record.TabletContextId(), record.ContactId(),
						snapshot, ContactPhase::Cancelled);
					break;
				}
			}
		}
	}

	bool ContactInputCoordinator::TryDequeue(IngressCommand& command) noexcept
	{
		return impl_->queue.try_dequeue(command);
	}

	void ContactInputCoordinator::WaitDequeue(IngressCommand& command) noexcept
	{
		impl_->queue.wait_dequeue(command);
	}

	bool ContactInputCoordinator::PublishControlWake() noexcept
	{
		bool expected = false;
		if (!impl_->controlWakePending.compare_exchange_strong(
			expected, true, std::memory_order_acq_rel, std::memory_order_acquire)) return true;

		const IngressCommand command{ IngressCommandKind::ControlWake, {} };
		try
		{
			if (impl_->EnqueueReliably(command)) return true;
		}
		catch (const std::bad_alloc&)
		{
		}
		impl_->controlWakePending.store(false, std::memory_order_release);
		return false;
	}

	void ContactInputCoordinator::AcknowledgeControlWake() noexcept
	{
		impl_->controlWakePending.store(false, std::memory_order_release);
	}
}
