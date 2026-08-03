module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <windows.h>
#include <concurrentqueue/moodycamel/blockingconcurrentqueue.h>

module draw3.contact_input;

namespace draw3
{
	namespace
	{
		constexpr size_t kContactSlotsPerBlock = 32;
		constexpr size_t kMinimumIngressQueueCapacity = 256;
		constexpr size_t kDownProducerTokenCount = 32;
		constexpr size_t kExplicitProducerCount = kDownProducerTokenCount + 1;
		constexpr uint64_t kMaxProducerGeneration = (~uint64_t{ 0 }) >> 3;
		constexpr uint32_t kAllSlotsFree = 0xFFFFFFFFu;

		static_assert(sizeof(ContactRecord*) == sizeof(uintptr_t),
			"ingress payload 必须保持原生指针宽度");
		static_assert(std::atomic<uint32_t>::is_always_lock_free,
			"contact 位图要求 32 位原子始终无锁");

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

		size_t RoundUpSlotCapacity(size_t requested) noexcept
		{
			requested = std::max(requested, kContactSlotsPerBlock);
			return (requested + kContactSlotsPerBlock - 1) /
				kContactSlotsPerBlock * kContactSlotsPerBlock;
		}

		size_t ComputeDefaultSlotCapacity() noexcept
		{
			const int maximumTouches = std::max(0, GetSystemMetrics(SM_MAXIMUMTOUCHES));
			return RoundUpSlotCapacity(2u * (static_cast<size_t>(maximumTouches) + 2u));
		}

		struct LowPowerQueueTraits : moodycamel::ConcurrentQueueDefaultTraits
		{
			static constexpr int MAX_SEMA_SPINS = 0;
			static constexpr size_t INITIAL_IMPLICIT_PRODUCER_HASH_SIZE = 0;
		};

		using IngressQueue =
			moodycamel::BlockingConcurrentQueue<ContactRecord*, LowPowerQueueTraits>;
		using IngressProducerToken = IngressQueue::producer_token_t;

		struct ContactBlock
		{
			std::array<ContactRecord, kContactSlotsPerBlock> records;
			std::atomic<uint32_t> freeMask = kAllSlotsFree;
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

		static void SetOwner(ContactRecord& record, ContactBlock* block, uint32_t bit) noexcept
		{
			record.ownerBlock_ = block;
			record.ownerBit_ = bit;
		}

		static ContactBlock* OwnerBlock(const ContactRecord& record) noexcept
		{
			return static_cast<ContactBlock*>(record.ownerBlock_);
		}

		static uint32_t OwnerBit(const ContactRecord& record) noexcept
		{
			return record.ownerBit_;
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
			record.tilt_.Store(snapshot.tilt);
			record.orientation_.Store(snapshot.orientation);
			record.isInvertedCursor_.Store(snapshot.isInvertedCursor ? 1u : 0u);
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
			record.tilt_.Store(snapshot.tilt);
			record.orientation_.Store(snapshot.orientation);
			record.isInvertedCursor_.Store(snapshot.isInvertedCursor ? 1u : 0u);
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
				candidate.tilt = record.tilt_.Load();
				candidate.orientation = record.orientation_.Load();
				candidate.isInvertedCursor = record.isInvertedCursor_.Load() != 0;
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
		explicit ContactInputCoordinatorImpl(size_t requestedSlotCapacity)
			: slotCapacity(RoundUpSlotCapacity(requestedSlotCapacity)),
			queueCapacity(std::max(kMinimumIngressQueueCapacity, slotCapacity + 1)),
			queue(queueCapacity, kExplicitProducerCount, 0),
			controlProducerToken(queue)
		{
			for (auto& token : downProducerTokens)
				token = std::make_unique<IngressProducerToken>(queue);

			const size_t blockCount = slotCapacity / kContactSlotsPerBlock;
			blocks.reserve(blockCount);
			ContactBlock* previousBlock = nullptr;
			for (size_t blockIndex = 0; blockIndex < blockCount; ++blockIndex)
			{
				auto block = std::make_unique<ContactBlock>();
				ContactBlock* blockAddress = block.get();
				for (size_t slotIndex = 0; slotIndex < kContactSlotsPerBlock; ++slotIndex)
				{
					ContactRecordAccess::SetOwner(blockAddress->records[slotIndex], blockAddress,
						uint32_t{ 1 } << static_cast<uint32_t>(slotIndex));
				}
				if (previousBlock)
					previousBlock->next.store(blockAddress, std::memory_order_relaxed);
				else
					blockHead.store(blockAddress, std::memory_order_relaxed);
				previousBlock = blockAddress;
				blocks.push_back(std::move(block));
			}
			wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
			LARGE_INTEGER frequency = {};
			if (QueryPerformanceFrequency(&frequency)) qpcFrequency = frequency.QuadPart;
		}

		~ContactInputCoordinatorImpl()
		{
			if (wakeEvent) CloseHandle(wakeEvent);
		}

		void Count(std::atomic<uint64_t>& counter) noexcept
		{
			if (diagnosticsEnabled.load(std::memory_order_relaxed))
				counter.fetch_add(1, std::memory_order_relaxed);
		}

		void SignalWake() noexcept
		{
			wakeGeneration.fetch_add(1, std::memory_order_release);
			if (wakeEvent) SetEvent(wakeEvent);
		}

		LocatedContact FindProducing(uint32_t tabletContextId, uint32_t contactId) const noexcept
		{
			for (ContactBlock* block = blockHead.load(std::memory_order_acquire); block;
				block = block->next.load(std::memory_order_acquire))
			{
				uint32_t occupied = ~block->freeMask.load(std::memory_order_acquire);
				while (occupied != 0)
				{
					const uint32_t slotIndex = static_cast<uint32_t>(std::countr_zero(occupied));
					occupied &= occupied - 1;
					ContactRecord& record = block->records[slotIndex];
					const uint64_t generation = ContactRecordAccess::Generation(record);
					if (ContactRecordAccess::HasRoute(record, generation, ProducerState::Producing) &&
						ContactRecordAccess::Matches(record, tabletContextId, contactId))
						return LocatedContact{ &record, generation };
				}
			}
			return {};
		}

		ContactRecord* AcquireFreeSlot() noexcept
		{
			for (ContactBlock* block = blockHead.load(std::memory_order_acquire); block;
				block = block->next.load(std::memory_order_acquire))
			{
				uint32_t available = block->freeMask.load(std::memory_order_acquire);
				while (available != 0)
				{
					const uint32_t slotIndex = static_cast<uint32_t>(std::countr_zero(available));
					const uint32_t bit = uint32_t{ 1 } << slotIndex;
					const uint32_t desired = available & ~bit;
					if (block->freeMask.compare_exchange_strong(available, desired,
						std::memory_order_acq_rel, std::memory_order_acquire))
					{
						ContactRecord& record = block->records[slotIndex];
						if (ContactRecordAccess::State(record) == ProducerState::Free)
							return &record;
						block->freeMask.fetch_or(bit, std::memory_order_release);
						break; // 位图与 route 不一致时拒绝该块，不能覆盖尚未清理的 record。
					}
				}
			}
			return nullptr;
		}

		void ReleaseSlot(ContactRecord& record) noexcept
		{
			ContactBlock* owner = ContactRecordAccess::OwnerBlock(record);
			const uint32_t bit = ContactRecordAccess::OwnerBit(record);
			if (owner && bit != 0) owner->freeMask.fetch_or(bit, std::memory_order_release);
		}

		int AcquireDownProducerToken() noexcept
		{
			uint32_t available = downProducerTokenMask.load(std::memory_order_acquire);
			while (available != 0)
			{
				const uint32_t index = static_cast<uint32_t>(std::countr_zero(available));
				const uint32_t bit = uint32_t{ 1 } << index;
				if (downProducerTokenMask.compare_exchange_strong(available, available & ~bit,
					std::memory_order_acq_rel, std::memory_order_acquire))
					return static_cast<int>(index);
			}
			return -1;
		}

		void ReleaseDownProducerToken(int index) noexcept
		{
			if (index >= 0)
				downProducerTokenMask.fetch_or(
					uint32_t{ 1 } << static_cast<uint32_t>(index), std::memory_order_release);
		}

		bool EnqueueDown(ContactRecord* record) noexcept
		{
			const int tokenIndex = AcquireDownProducerToken();
			if (tokenIndex < 0) return false;
			const bool enqueued = queue.try_enqueue(
				*downProducerTokens[static_cast<size_t>(tokenIndex)], record);
			ReleaseDownProducerToken(tokenIndex);
			return enqueued;
		}

		void AbortUnqueuedDown(ContactRecord& record, uint64_t generation) noexcept
		{
			for (;;)
			{
				if (ContactRecordAccess::TrySetExactState(record, generation,
					ProducerState::Producing, ProducerState::Closing))
				{
					// 先关 route 并等已进入的 Move 退出，最后才 release 归还位图。
					ContactRecordAccess::LockWriter(record);
					ContactRecordAccess::UnlockWriter(record);
					if (ContactRecordAccess::TrySetExactState(record, generation,
						ProducerState::Closing, ProducerState::Free))
						ReleaseSlot(record);
					return;
				}
				if (ContactRecordAccess::TrySetExactState(record, generation,
					ProducerState::ConsumerOwned, ProducerState::Free))
				{
					ReleaseSlot(record);
					return;
				}

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
			const bool closed = ContactRecordAccess::TrySetExactState(record, expectedGeneration,
				ProducerState::Closing, ProducerState::ConsumerOwned);
			if (closed)
			{
				Count(terminalPublished);
				SignalWake(); // 终态必须打断活动帧等待，不能多滞留一个 120 FPS 周期。
			}
			return closed;
		}

		size_t OccupiedSlotCount() const noexcept
		{
			size_t count = 0;
			for (ContactBlock* block = blockHead.load(std::memory_order_acquire); block;
				block = block->next.load(std::memory_order_acquire))
				count += static_cast<size_t>(std::popcount(
					~block->freeMask.load(std::memory_order_acquire)));
			return count;
		}

		const size_t slotCapacity;
		const size_t queueCapacity;
		IngressQueue queue;
		IngressProducerToken controlProducerToken;
		std::array<std::unique_ptr<IngressProducerToken>, kDownProducerTokenCount> downProducerTokens;
		std::atomic<uint32_t> downProducerTokenMask = kAllSlotsFree;
		std::atomic<bool> controlWakePending = false;
		std::atomic<ContactBlock*> blockHead = nullptr;
		std::vector<std::unique_ptr<ContactBlock>> blocks;
		HANDLE wakeEvent = nullptr;
		int64_t qpcFrequency = 0;
		std::atomic<uint64_t> wakeGeneration = 0;
		std::atomic<bool> diagnosticsEnabled = false;
		std::atomic<uint64_t> downPublished = 0;
		std::atomic<uint64_t> downRejected = 0;
		std::atomic<uint64_t> movePublished = 0;
		std::atomic<uint64_t> moveContended = 0;
		std::atomic<uint64_t> terminalPublished = 0;
		std::atomic<uint64_t> recycled = 0;
		std::atomic<uint64_t> controlWakes = 0;
		std::atomic<uint64_t> activeWaits = 0;
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
		: impl_(std::make_unique<ContactInputCoordinatorImpl>(ComputeDefaultSlotCapacity()))
	{
	}

#if defined(DRAW3_TESTING)
	ContactInputCoordinator::ContactInputCoordinator(size_t slotCapacityForTesting)
		: impl_(std::make_unique<ContactInputCoordinatorImpl>(slotCapacityForTesting))
	{
	}
#endif

	ContactInputCoordinator::~ContactInputCoordinator() = default;

	bool ContactInputCoordinator::PublishDown(uint32_t tabletContextId, uint32_t contactId,
		InputDeviceType deviceType, const ContactSnapshot& snapshot)
	{
		ContactRecord* record = impl_->AcquireFreeSlot();
		if (!record)
		{
			impl_->Count(impl_->downRejected);
			return false;
		}

		const uint64_t previousGeneration = ContactRecordAccess::Generation(*record);
		const uint64_t generation = previousGeneration >= kMaxProducerGeneration
			? 1 : previousGeneration + 1;
		ContactRecordAccess::Initialize(
			*record, tabletContextId, contactId, deviceType, snapshot, generation);
		ContactRecordAccess::SetState(*record, ProducerState::Producing);
		if (impl_->EnqueueDown(record))
		{
			impl_->Count(impl_->downPublished);
			impl_->SignalWake();
			return true;
		}
		impl_->AbortUnqueuedDown(*record, generation); // 入队失败也不能与已经进入的 Move/Up 争用复用。
		impl_->Count(impl_->downRejected);
		return false;
	}

	bool ContactInputCoordinator::PublishMove(uint32_t tabletContextId, uint32_t contactId,
		const ContactSnapshot& snapshot) noexcept
	{
		const LocatedContact located = impl_->FindProducing(tabletContextId, contactId);
		if (!located || !ContactRecordAccess::TryLockWriter(*located.record))
		{
			impl_->Count(impl_->moveContended);
			return false;
		}
		if (!ContactRecordAccess::HasRoute(
			*located.record, located.generation, ProducerState::Producing) ||
			!ContactRecordAccess::Matches(*located.record, tabletContextId, contactId))
		{
			ContactRecordAccess::UnlockWriter(*located.record);
			impl_->Count(impl_->moveContended);
			return false;
		}
		ContactSnapshot moveSnapshot = snapshot;
		moveSnapshot.phase = ContactPhase::Move;
		ContactRecordAccess::PublishSnapshot(*located.record, moveSnapshot);
		const bool routeStillMatches = ContactRecordAccess::HasRoute(
			*located.record, located.generation, ProducerState::Producing);
		ContactRecordAccess::UnlockWriter(*located.record);
		if (!routeStillMatches)
		{
			impl_->Count(impl_->moveContended);
			return false;
		}
		impl_->Count(impl_->movePublished);
		return true; // Move 只覆盖最新 snapshot，不触发活动帧唤醒。
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
				*handle.record, handle.generation, ProducerState::ConsumerOwned, ProducerState::Free))
			{
				impl_->ReleaseSlot(*handle.record); // route 先 Free，位图再以 release 允许下一代取得。
				impl_->Count(impl_->recycled);
				return;
			}
			if (ContactRecordAccess::Generation(*handle.record) != handle.generation ||
				ContactRecordAccess::State(*handle.record) != ProducerState::Closing) return;
			YieldProcessor(); // 并发取消重叠时，等待关闭者交出 consumer ownership。
		}
	}

	void ContactInputCoordinator::CloseAllProducerContacts(int64_t qpc) noexcept
	{
		for (ContactBlock* block = impl_->blockHead.load(std::memory_order_acquire); block;
			block = block->next.load(std::memory_order_acquire))
		{
			uint32_t occupied = ~block->freeMask.load(std::memory_order_acquire);
			while (occupied != 0)
			{
				const uint32_t slotIndex = static_cast<uint32_t>(std::countr_zero(occupied));
				occupied &= occupied - 1;
				ContactRecord& record = block->records[slotIndex];
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

	bool ContactInputCoordinator::TryDequeue(ContactRecord*& record) noexcept
	{
		return impl_->queue.try_dequeue(record);
	}

	void ContactInputCoordinator::WaitDequeue(ContactRecord*& record) noexcept
	{
		impl_->queue.wait_dequeue(record);
	}

	bool ContactInputCoordinator::PublishControlWake() noexcept
	{
		impl_->SignalWake(); // sticky 请求已发布后，即使 queue wake 已合并也要打断活动帧等待。
		bool expected = false;
		if (!impl_->controlWakePending.compare_exchange_strong(
			expected, true, std::memory_order_acq_rel, std::memory_order_acquire)) return true;

		ContactRecord* controlWake = nullptr;
		if (impl_->queue.try_enqueue(impl_->controlProducerToken, controlWake))
		{
			impl_->Count(impl_->controlWakes);
			return true;
		}
		impl_->controlWakePending.store(false, std::memory_order_release);
		return false;
	}

	void ContactInputCoordinator::AcknowledgeControlWake() noexcept
	{
		impl_->controlWakePending.store(false, std::memory_order_release);
	}

	uint64_t ContactInputCoordinator::CaptureWakeGeneration() const noexcept
	{
		return impl_->wakeGeneration.load(std::memory_order_acquire);
	}

	bool ContactInputCoordinator::WaitForWake(
		uint64_t observedGeneration, double timeoutMilliseconds) noexcept
	{
		impl_->Count(impl_->activeWaits);
		if (impl_->wakeGeneration.load(std::memory_order_acquire) != observedGeneration) return true;
		if (timeoutMilliseconds <= 0.0 || impl_->qpcFrequency <= 0) return false;

		LARGE_INTEGER now = {};
		QueryPerformanceCounter(&now);
		const int64_t deadline = now.QuadPart + static_cast<int64_t>(
			timeoutMilliseconds * static_cast<double>(impl_->qpcFrequency) / 1000.0);
		for (;;)
		{
			if (impl_->wakeGeneration.load(std::memory_order_acquire) != observedGeneration) return true;
			QueryPerformanceCounter(&now);
			const int64_t remainingTicks = deadline - now.QuadPart;
			if (remainingTicks <= 0) return false;
			const double remainingMilliseconds =
				static_cast<double>(remainingTicks) * 1000.0 / static_cast<double>(impl_->qpcFrequency);
			const double coarseWaitMilliseconds = std::floor(remainingMilliseconds - 1.25);
			if (impl_->wakeEvent && coarseWaitMilliseconds >= 1.0)
			{
				const DWORD coarseWait = static_cast<DWORD>(coarseWaitMilliseconds);
				const DWORD result = WaitForSingleObject(impl_->wakeEvent, coarseWait);
				if (result == WAIT_FAILED) return false;
			}
			else
			{
				YieldProcessor(); // 不再追加最短 1ms 内核等待，活动末段自旋吸收调度抖动；空闲仍完全阻塞。
			}
		}
	}

	void ContactInputCoordinator::WaitForFrameDeadline(
		double timeoutMilliseconds) noexcept
	{
		impl_->Count(impl_->activeWaits);
		if (timeoutMilliseconds <= 0.0 || impl_->qpcFrequency <= 0) return;

		LARGE_INTEGER now = {};
		QueryPerformanceCounter(&now);
		const int64_t deadline = now.QuadPart + static_cast<int64_t>(
			timeoutMilliseconds * static_cast<double>(impl_->qpcFrequency) / 1000.0);
		for (;;)
		{
			QueryPerformanceCounter(&now);
			const int64_t remainingTicks = deadline - now.QuadPart;
			if (remainingTicks <= 0) return;
			const double remainingMilliseconds =
				static_cast<double>(remainingTicks) * 1000.0 / static_cast<double>(impl_->qpcFrequency);
			const double coarseWaitMilliseconds = std::floor(remainingMilliseconds - 1.25);
			if (impl_->wakeEvent && coarseWaitMilliseconds >= 1.0)
			{
				const DWORD coarseWait = static_cast<DWORD>(coarseWaitMilliseconds);
				if (WaitForSingleObject(impl_->wakeEvent, coarseWait) == WAIT_FAILED) return;
			}
			else
			{
				YieldProcessor(); // 最后约 1.25ms 继续使用现有高精度等待策略。
			}
		}
	}

	void ContactInputCoordinator::EnableDiagnostics(bool enabled) noexcept
	{
		impl_->diagnosticsEnabled.store(enabled, std::memory_order_release);
	}

	ContactInputDiagnosticsSnapshot ContactInputCoordinator::DiagnosticsSnapshot() const noexcept
	{
		ContactInputDiagnosticsSnapshot snapshot;
		snapshot.downPublished = impl_->downPublished.load(std::memory_order_relaxed);
		snapshot.downRejected = impl_->downRejected.load(std::memory_order_relaxed);
		snapshot.movePublished = impl_->movePublished.load(std::memory_order_relaxed);
		snapshot.moveContended = impl_->moveContended.load(std::memory_order_relaxed);
		snapshot.terminalPublished = impl_->terminalPublished.load(std::memory_order_relaxed);
		snapshot.recycled = impl_->recycled.load(std::memory_order_relaxed);
		snapshot.controlWakes = impl_->controlWakes.load(std::memory_order_relaxed);
		snapshot.activeWaits = impl_->activeWaits.load(std::memory_order_relaxed);
		snapshot.slotCapacity = impl_->slotCapacity;
		snapshot.occupiedSlots = impl_->OccupiedSlotCount();
		return snapshot;
	}
}
