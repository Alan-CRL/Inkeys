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
#include <exception>
#include <limits>
#include <memory>
#include <vector>
#include <windows.h>
#include <concurrentqueue/moodycamel/blockingconcurrentqueue.h>

module Inkeys.Drawing.Draw3.contact_input;

namespace Inkeys::Drawing::Draw3
{
	namespace
	{
		constexpr size_t kContactSlotsPerBlock = 32;
		constexpr size_t kMaximumContactSlotCapacity = 4096;
		constexpr size_t kMinimumIngressQueueCapacity = 256;
		constexpr size_t kExplicitProducerCount = 1;
		constexpr uint32_t kCommandWakeCapacity = 258;
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
			ConsumerOwned,
			Quarantined,
			ClosingDiscarded
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
			// 外部测试参数和异常系统数据都不能触发 size_t 回绕或巨额预分配。
			requested = std::clamp(requested,
				kContactSlotsPerBlock, kMaximumContactSlotCapacity);
			return (requested + kContactSlotsPerBlock - 1) /
				kContactSlotsPerBlock * kContactSlotsPerBlock;
		}

		bool HasFinitePosition(const ContactSnapshot& snapshot) noexcept
		{
			const double x = snapshot.position.x;
			const double y = snapshot.position.y;
			return std::isfinite(x) && std::isfinite(y) &&
				x >= static_cast<double>((std::numeric_limits<LONG>::min)()) &&
				x <= static_cast<double>((std::numeric_limits<LONG>::max)()) &&
				y >= static_cast<double>((std::numeric_limits<LONG>::min)()) &&
				y <= static_cast<double>((std::numeric_limits<LONG>::max)());
		}

		bool TryComputeQpcDeadline(int64_t nowQpc, int64_t qpcFrequency,
			double timeoutMilliseconds, int64_t& deadlineQpc) noexcept
		{
			if (nowQpc < 0 || qpcFrequency <= 0 ||
				!std::isfinite(timeoutMilliseconds) || timeoutMilliseconds <= 0.0)
				return false;
			const double timeoutTicks = timeoutMilliseconds *
				static_cast<double>(qpcFrequency) / 1000.0;
			const int64_t maximumDelta = (std::numeric_limits<int64_t>::max)() - nowQpc;
			if (!std::isfinite(timeoutTicks) || timeoutTicks <= 0.0 ||
				timeoutTicks >= static_cast<double>(maximumDelta)) return false;
			deadlineQpc = nowQpc + static_cast<int64_t>(timeoutTicks);
			return deadlineQpc > nowQpc;
		}

		DWORD SafeCoarseWaitMilliseconds(double milliseconds) noexcept
		{
			constexpr double kMaximumFiniteWait = static_cast<double>(INFINITE - 1u);
			return static_cast<DWORD>(std::min(milliseconds, kMaximumFiniteWait));
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
		ContactRecord* CommandWakeMarker() noexcept
		{
			return reinterpret_cast<ContactRecord*>(uintptr_t{ 1 });
		}

		struct CommandFallbackWake
		{
			uint64_t downTarget = 0;
			uint64_t ordinal = 0;
		};

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
		static uint64_t Route(const ContactRecord& record) noexcept
		{
			return record.producerRoute_.Load();
		}

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

		static void ResetStoppedRecord(ContactRecord& record) noexcept
		{
			// Host 已停止所有 producer/consumer；旧 generation 的排队指针不可进入下一次 Start。
			record.writerLatch_.clear(std::memory_order_relaxed);
			SetState(record, ProducerState::Free);
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
			// writerLatch_ 只能由实际持锁者释放，槽位跨 generation 复用时不能重置 ownership。
			record.x_.Store(snapshot.position.x);
			record.y_.Store(snapshot.position.y);
			record.pressure_.Store(snapshot.pressure);
			record.tilt_.Store(snapshot.tilt);
			record.orientation_.Store(snapshot.orientation);
			record.isInvertedCursor_.Store(snapshot.isInvertedCursor ? 1u : 0u);
			record.rawContactWidth_.Store(snapshot.rawContactSize.width);
			record.rawContactHeight_.Store(snapshot.rawContactSize.height);
			record.contactAreaUnits_.Store(static_cast<uint32_t>(snapshot.contactAreaUnits));
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
			record.rawContactWidth_.Store(snapshot.rawContactSize.width);
			record.rawContactHeight_.Store(snapshot.rawContactSize.height);
			record.contactAreaUnits_.Store(static_cast<uint32_t>(snapshot.contactAreaUnits));
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
				// 来源在 Down 锁存；RTS 映射换代会关闭旧 contact，不在 Move 中拼接另一设备。
				candidate.source = record.downSnapshot_.source;
				candidate.admissionRevision = record.downSnapshot_.admissionRevision;
				candidate.position.x = record.x_.Load();
				candidate.position.y = record.y_.Load();
				candidate.pressure = record.pressure_.Load();
				candidate.tilt = record.tilt_.Load();
				candidate.orientation = record.orientation_.Load();
				candidate.isInvertedCursor = record.isInvertedCursor_.Load() != 0;
				candidate.rawContactSize={record.rawContactWidth_.Load(),record.rawContactHeight_.Load()};
				candidate.contactAreaUnits=static_cast<SpeedEraser::ContactAreaUnits>(record.contactAreaUnits_.Load());
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
			ingressProducerToken(queue)
		{
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

		bool PublishControlWake() noexcept
		{
			bool expected = false;
			if (!controlWakePending.compare_exchange_strong(expected, true,
				std::memory_order_acq_rel, std::memory_order_acquire))
			{
				SignalWake();
				return true;
			}
			LockIngressEnqueue();
			const bool enqueued =
				!failNextControlWakeEnqueueForTesting.exchange(false, std::memory_order_acq_rel) &&
				queue.try_enqueue(ingressProducerToken, nullptr);
			if (enqueued)
			{
				UnlockIngressEnqueue();
				Count(controlWakes);
				SignalWake(); // 入队完成后通知，避免空闲线程先醒后再次无限等待。
				return true;
			}
			// 失败时用已成功入队的 Down 水位做逻辑 marker，不能让后续新笔迹抢在 Clear 前。
			controlWakeDrainTarget.store(downEnqueuedCount.load(std::memory_order_relaxed),
				std::memory_order_relaxed);
			controlWakeFallbackPending.store(true, std::memory_order_release);
			UnlockIngressEnqueue();
			controlWakeEnqueueFailures.fetch_add(1, std::memory_order_relaxed);
			SignalWake();
			return false;
		}

		bool TryReserveCommandWake() noexcept
		{
			uint32_t count = commandWakeReservations.load(std::memory_order_acquire);
			while (count < kCommandWakeCapacity)
			{
				if (commandWakeReservations.compare_exchange_weak(count, count + 1,
					std::memory_order_acq_rel, std::memory_order_acquire)) return true;
			}
			return false;
		}

		void PublishReservedCommandWake() noexcept
		{
			LockIngressEnqueue();
			const uint64_t ordinal = ++commandWakePublishedOrdinal;
			const bool enqueued =
				!failNextCommandWakeEnqueueForTesting.exchange(false, std::memory_order_acq_rel) &&
				queue.try_enqueue(ingressProducerToken, CommandWakeMarker());
			if (!enqueued)
			{
				const uint64_t write = commandFallbackWrite.load(std::memory_order_relaxed);
				const uint64_t read = commandFallbackRead.load(std::memory_order_acquire);
				// 每个 Bridge 命令先占预约；256 个普通槽加 final 小于固定 ring 容量。
				if (write - read >= kCommandWakeCapacity) std::terminate();
				commandFallbacks[write % kCommandWakeCapacity] = {
					downEnqueuedCount.load(std::memory_order_relaxed), ordinal };
				commandFallbackWrite.store(write + 1, std::memory_order_release);
			}
			UnlockIngressEnqueue();
			if (enqueued) Count(controlWakes);
			SignalWake();
		}

		bool TryTakeCommandWakeFallback(ContactRecord*& record) noexcept
		{
			const uint64_t read = commandFallbackRead.load(std::memory_order_relaxed);
			if (read == commandFallbackWrite.load(std::memory_order_acquire)) return false;
			const CommandFallbackWake& wake = commandFallbacks[read % kCommandWakeCapacity];
			if (wake.ordinal != commandWakeConsumedOrdinal + 1 ||
				downDequeuedCount.load(std::memory_order_acquire) < wake.downTarget) return false;
			commandFallbackRead.store(read + 1, std::memory_order_release);
			++commandWakeConsumedOrdinal;
			commandWakeReservations.fetch_sub(1, std::memory_order_acq_rel);
			lastDequeuedControlWakeKind = ControlWakeKind::Command;
			record = nullptr;
			return true;
		}

		bool TryTakeUnqueuedControlWake(ContactRecord*& record) noexcept
		{
			if (!controlWakeFallbackPending.exchange(false, std::memory_order_acq_rel)) return false;
			record = nullptr;
			lastDequeuedControlWakeKind = ControlWakeKind::General;
			controlWakeInlineRecoveries.fetch_add(1, std::memory_order_relaxed);
			return true;
		}

		void SignalWake() noexcept
		{
			wakeGeneration.fetch_add(1, std::memory_order_release);
			if (wakeEvent) SetEvent(wakeEvent);
		}

		void LockIngressEnqueue() noexcept
		{
			// 仅串行两个 try_enqueue；不在锁内处理样本、模型或业务回调。
			while (ingressEnqueueLatch.test_and_set(std::memory_order_acquire))
				YieldProcessor();
		}

		void UnlockIngressEnqueue() noexcept
		{
			ingressEnqueueLatch.clear(std::memory_order_release);
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
					if ((ContactRecordAccess::HasRoute(record, generation, ProducerState::Producing) ||
						ContactRecordAccess::HasRoute(record, generation, ProducerState::Quarantined)) &&
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

		bool EnqueueDown(ContactRecord* record) noexcept
		{
			LockIngressEnqueue();
			const bool enqueued = queue.try_enqueue(ingressProducerToken, record);
			if (enqueued)
				downEnqueuedCount.fetch_add(1, std::memory_order_release);
			UnlockIngressEnqueue();
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
			if (!ContactRecordAccess::Matches(record, tabletContextId, contactId)) return false;
			bool quarantined = false;
			if (!ContactRecordAccess::TrySetExactState(record, expectedGeneration,
				ProducerState::Producing, ProducerState::Closing))
			{
				quarantined = ContactRecordAccess::TrySetExactState(record, expectedGeneration,
					ProducerState::Quarantined, ProducerState::Closing);
				if (!quarantined) return false;
			}
			// 测试 hook 仅在 Closing CAS 成功后暂停；默认空指针不改变生产终态。
			ContactClosePauseForTesting* pause = closePauseForTesting.load(
				std::memory_order_acquire);
			if (pause && closePauseForTesting.compare_exchange_strong(pause, nullptr,
				std::memory_order_acq_rel, std::memory_order_acquire))
			{
				pause->entered.store(true, std::memory_order_release);
				while (!pause->resume.load(std::memory_order_acquire)) YieldProcessor();
			}
			ContactRecordAccess::LockWriter(record); // Up 先关路由，再等待正在发布的 Move 退出。
			const uint64_t routeAfterWriter = ContactRecordAccess::Route(record);
			const ProducerState stateAfterWriter = RouteState(routeAfterWriter);
			if (RouteGeneration(routeAfterWriter) != expectedGeneration ||
				(stateAfterWriter != ProducerState::Closing &&
					stateAfterWriter != ProducerState::ClosingDiscarded) ||
				!ContactRecordAccess::Matches(record, tabletContextId, contactId))
			{
				ContactRecordAccess::UnlockWriter(record);
				return false;
			}
			if (!HasFinitePosition(snapshot))
			{
				ContactSnapshot latest;
				if (!ContactRecordAccess::ReadSnapshot(record, latest) || !HasFinitePosition(latest))
				{
					// Down 已通过有限位置校验且在本 generation 内不可变；坏终态仍须闭合路由。
					latest = record.DownSnapshot();
					if (!HasFinitePosition(latest))
					{
						ContactRecordAccess::UnlockWriter(record);
						return false; // 内存本身损坏时不能把仍在写的槽伪装为空闲。
					}
				}
				const int64_t terminalQpc = snapshot.qpc;
				snapshot = latest; // 终态坏包仍要可靠闭合，并沿用最后一个有效位置。
				if (terminalQpc > 0) snapshot.qpc = terminalQpc;
			}
			snapshot.phase = phase;
			ContactRecordAccess::PublishSnapshot(record, snapshot);
			ContactRecordAccess::UnlockWriter(record);
			bool closed = ContactRecordAccess::TrySetExactState(record, expectedGeneration,
				ProducerState::Closing, quarantined ? ProducerState::Free : ProducerState::ConsumerOwned);
			bool producerRecycles = closed && quarantined;
			if (!closed)
			{
				// 消费者在 writer 完成前交出 handle；唯一 Close producer 承担最终释放。
				closed = ContactRecordAccess::TrySetExactState(record, expectedGeneration,
					ProducerState::ClosingDiscarded, ProducerState::Free);
				producerRecycles = closed;
			}
			if (closed)
			{
				if (producerRecycles)
				{
					if (quarantined)
						quarantinedContacts.fetch_sub(1, std::memory_order_acq_rel);
					ReleaseSlot(record);
					Count(recycled);
					(void)PublishControlWake(); // 无绘制帧时也需退休聚合 physical-contact 状态。
				}
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
		IngressProducerToken ingressProducerToken;
		std::atomic_flag ingressEnqueueLatch = ATOMIC_FLAG_INIT;
		std::atomic<uint64_t> downEnqueuedCount = 0;
		std::atomic<uint64_t> downDequeuedCount = 0;
		std::atomic<uint32_t> commandWakeReservations = 0;
		uint64_t commandWakePublishedOrdinal = 0; // 单一 ingress enqueue 锁保护。
		uint64_t commandWakeConsumedOrdinal = 0; // 唯一绘制消费者拥有。
		ContactRecord* deferredIngressRecord = nullptr; // 唯一消费者保留，停止后才能重置。
		std::atomic<bool> deferredIngressPending = false;
		std::array<CommandFallbackWake, kCommandWakeCapacity> commandFallbacks{};
		std::atomic<uint64_t> commandFallbackWrite = 0;
		std::atomic<uint64_t> commandFallbackRead = 0;
		ControlWakeKind lastDequeuedControlWakeKind = ControlWakeKind::General;
		std::atomic<bool> controlWakePending = false;
		std::atomic<bool> controlWakeFallbackPending = false;
		std::atomic<uint64_t> controlWakeDrainTarget = 0;
		std::atomic<bool> failNextControlWakeEnqueueForTesting = false;
		std::atomic<bool> failNextCommandWakeEnqueueForTesting = false;
		std::atomic<ContactClosePauseForTesting*> closePauseForTesting = nullptr;
#if defined(DRAW3_CONTACT_TESTING)
		std::atomic<CommandFallbackMissHookForTesting*> commandFallbackMissHookForTesting = nullptr;
#endif
		std::atomic<ContactBlock*> blockHead = nullptr;
		std::vector<std::unique_ptr<ContactBlock>> blocks;
		HANDLE wakeEvent = nullptr;
		int64_t qpcFrequency = 0;
		std::atomic<uint64_t> wakeGeneration = 0;
		std::atomic<uint64_t> admissionRevision = 0;
		std::atomic<size_t> quarantinedContacts = 0;
		std::atomic<bool> diagnosticsEnabled = false;
		std::atomic<uint64_t> downPublished = 0;
		std::atomic<uint64_t> downRejected = 0;
		std::atomic<uint64_t> movePublished = 0;
		std::atomic<uint64_t> moveContended = 0;
		std::atomic<uint64_t> terminalPublished = 0;
		std::atomic<uint64_t> recycled = 0;
		std::atomic<uint64_t> controlWakes = 0;
		std::atomic<uint64_t> controlWakeEnqueueFailures = 0;
		std::atomic<uint64_t> controlWakeInlineRecoveries = 0;
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
		if (!HasFinitePosition(snapshot))
		{
			impl_->Count(impl_->downRejected);
			return false;
		}
		ContactRecord* record = impl_->AcquireFreeSlot();
		if (!record)
		{
			impl_->Count(impl_->downRejected);
			return false;
		}

		const uint64_t previousGeneration = ContactRecordAccess::Generation(*record);
		const uint64_t generation = previousGeneration >= kMaxProducerGeneration
			? 1 : previousGeneration + 1;
		ContactSnapshot admitted = snapshot;
		admitted.admissionRevision = impl_->admissionRevision.load(std::memory_order_acquire);
		ContactRecordAccess::Initialize(
			*record, tabletContextId, contactId, deviceType, admitted, generation);
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
		if (!HasFinitePosition(snapshot)) return false;
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
		if (!handle.record) return false;
		const auto readable = [&](uint64_t route) noexcept
		{
			const ProducerState state = RouteState(route);
			return RouteGeneration(route) == handle.generation &&
				(state == ProducerState::Producing || state == ProducerState::Closing ||
					state == ProducerState::ConsumerOwned);
		};
		if (!readable(ContactRecordAccess::Route(*handle.record))) return false;
		ContactSnapshot candidate;
		if (!ContactRecordAccess::ReadSnapshot(*handle.record, candidate)) return false;
		// 单次 route 读取同时核代次和所有权，拒绝已交出 handle 的 ClosingDiscarded。
		const uint64_t routeAfter = ContactRecordAccess::Route(*handle.record);
		if (!readable(routeAfter)) return false;
		if ((candidate.phase == ContactPhase::Up || candidate.phase == ContactPhase::Cancelled) &&
			RouteState(routeAfter) != ProducerState::ConsumerOwned) return false;
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
			const uint64_t route = ContactRecordAccess::Route(*handle.record);
			if (RouteGeneration(route) != handle.generation) return;
			if (RouteState(route) != ProducerState::Closing) return;
			// 终态正由唯一 producer 写入；消费者先交出 handle，不占住绘制线程。
			if (ContactRecordAccess::TrySetExactState(*handle.record, handle.generation,
				ProducerState::Closing, ProducerState::ClosingDiscarded)) return;
		}
	}

	void ContactInputCoordinator::DiscardUntilTerminal(ContactHandle handle) noexcept
	{
		if (!handle.record) return;
		for (;;)
		{
			// 先计数再交出 Producing 路由，保证抢先 Up 不会使计数下溢。
			impl_->quarantinedContacts.fetch_add(1, std::memory_order_acq_rel);
			if (ContactRecordAccess::TrySetExactState(*handle.record, handle.generation,
				ProducerState::Producing, ProducerState::Quarantined)) return;
			impl_->quarantinedContacts.fetch_sub(1, std::memory_order_acq_rel);
			const uint64_t route = ContactRecordAccess::Route(*handle.record);
			if (RouteGeneration(route) != handle.generation) return;
			const ProducerState state = RouteState(route);
			if (state == ProducerState::Closing)
			{
				if (ContactRecordAccess::TrySetExactState(*handle.record, handle.generation,
					ProducerState::Closing, ProducerState::ClosingDiscarded)) return;
				continue; // 输给 Close 的终态 CAS 时改走 ConsumerOwned 回收。
			}
			if (state == ProducerState::ClosingDiscarded || state == ProducerState::Quarantined)
				return; // 重复 Discard 不重复计数或释放。
			if (state == ProducerState::ConsumerOwned) Recycle(handle);
			if (state != ProducerState::Producing) return;
		}
	}

	void ContactInputCoordinator::SetAdmissionBlocked(bool blocked) noexcept
	{
		uint64_t revision = impl_->admissionRevision.load(std::memory_order_acquire);
		while ((revision % 2 != 0) != blocked)
		{
			if (impl_->admissionRevision.compare_exchange_weak(revision, revision + 1,
				std::memory_order_acq_rel, std::memory_order_acquire))
			{
				(void)PublishControlWake();
				return;
			}
		}
	}

	bool ContactInputCoordinator::AdmissionBlocked() const noexcept
	{
		return AdmissionRevision() % 2 != 0;
	}

	uint64_t ContactInputCoordinator::AdmissionRevision() const noexcept
	{
		return impl_->admissionRevision.load(std::memory_order_acquire);
	}

	bool ContactInputCoordinator::ContactAdmitted(ContactHandle handle) const noexcept
	{
		const uint64_t revision = AdmissionRevision();
		return revision % 2 == 0 && handle.record &&
			handle.record->Generation() == handle.generation &&
			handle.record->DownSnapshot().admissionRevision == revision;
	}

	bool ContactInputCoordinator::HasQuarantinedContacts() const noexcept
	{
		return impl_->quarantinedContacts.load(std::memory_order_acquire) != 0;
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
				while (ContactRecordAccess::HasRoute(record, generation, ProducerState::Producing) ||
					ContactRecordAccess::HasRoute(record, generation, ProducerState::Quarantined))
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
		// Command 失败退路按自身发布序号及旧 Down 水位补齐，不会越过前一实体命令。
		if (impl_->TryTakeCommandWakeFallback(record)) return true;
#if defined(DRAW3_CONTACT_TESTING)
		// 原消费窗口的单次测试接缝，不改变 fallback 判定或实体 marker 行为。
		if (auto* hook = impl_->commandFallbackMissHookForTesting.exchange(
			nullptr, std::memory_order_acq_rel))
		{
			if (hook->callback) hook->callback(hook->context);
		}
#endif
		// 实体 marker 与 Down 共用 producer FIFO；General 失败退路等旧 Down 出队。
		auto fallbackReady = [this]() noexcept
			{
				return impl_->controlWakeFallbackPending.load(std::memory_order_acquire) &&
					impl_->downDequeuedCount.load(std::memory_order_acquire) >=
					impl_->controlWakeDrainTarget.load(std::memory_order_relaxed);
			};
		if (fallbackReady() && impl_->TryTakeUnqueuedControlWake(record)) return true;
		const bool deferred = impl_->deferredIngressPending.load(std::memory_order_relaxed);
		if (deferred || impl_->queue.try_dequeue(record))
		{
			if (deferred) record = impl_->deferredIngressRecord;
			ContactRecord* fallback = nullptr;
			// 实体出队的 acquire 已涵盖更早的 fallback 发布；复查后保留原事件，不能偷用其命令序号。
			if (impl_->TryTakeCommandWakeFallback(fallback))
			{
				impl_->deferredIngressRecord = record;
				impl_->deferredIngressPending.store(true, std::memory_order_release);
				record = fallback;
				return true;
			}
			if (deferred)
			{
				impl_->deferredIngressRecord = nullptr;
				impl_->deferredIngressPending.store(false, std::memory_order_release);
			}
			if (record == CommandWakeMarker())
			{
				record = nullptr;
				impl_->lastDequeuedControlWakeKind = ControlWakeKind::Command;
				++impl_->commandWakeConsumedOrdinal;
				impl_->commandWakeReservations.fetch_sub(1, std::memory_order_acq_rel);
			}
			else if (record)
				impl_->downDequeuedCount.fetch_add(1, std::memory_order_release);
			else impl_->lastDequeuedControlWakeKind = ControlWakeKind::General;
			return true;
		}
		return fallbackReady() && impl_->TryTakeUnqueuedControlWake(record);
	}

	void ContactInputCoordinator::WaitDequeue(ContactRecord*& record) noexcept
	{
		for (;;)
		{
			const uint64_t observedGeneration = CaptureWakeGeneration();
			if (TryDequeue(record)) return;
			if (impl_->wakeGeneration.load(std::memory_order_acquire) != observedGeneration)
				continue;
			// 控制 token 失败只发 event，不能再阻塞于 queue 自身的 semaphore。
			if (impl_->wakeEvent)
			{
				if (WaitForSingleObject(impl_->wakeEvent, INFINITE) != WAIT_FAILED)
					continue;
			}
			Sleep(10); // event 创建/等待失败时保留有限退路，避免无界自旋。
		}
	}

	bool ContactInputCoordinator::HasPendingWork() const noexcept
	{
		return impl_->deferredIngressPending.load(std::memory_order_acquire) ||
			impl_->commandFallbackRead.load(std::memory_order_acquire) !=
			impl_->commandFallbackWrite.load(std::memory_order_acquire) ||
			impl_->controlWakeFallbackPending.load(std::memory_order_acquire) ||
			impl_->queue.size_approx() != 0;
	}

	bool ContactInputCoordinator::PublishControlWake() noexcept
	{
		return impl_->PublishControlWake();
	}

	bool ContactInputCoordinator::TryReserveCommandWake() noexcept
	{
		return impl_->TryReserveCommandWake();
	}

	void ContactInputCoordinator::CancelReservedCommandWake() noexcept
	{
		impl_->commandWakeReservations.fetch_sub(1, std::memory_order_acq_rel);
	}

	void ContactInputCoordinator::PublishReservedCommandWake() noexcept
	{
		impl_->PublishReservedCommandWake();
	}

	ControlWakeKind ContactInputCoordinator::LastDequeuedControlWakeKind() const noexcept
	{
		return impl_->lastDequeuedControlWakeKind;
	}

	void ContactInputCoordinator::FailNextControlWakeEnqueueForTesting() noexcept
	{
		impl_->failNextControlWakeEnqueueForTesting.store(true, std::memory_order_release);
	}

	void ContactInputCoordinator::FailNextCommandWakeEnqueueForTesting() noexcept
	{
		impl_->failNextCommandWakeEnqueueForTesting.store(true, std::memory_order_release);
	}

#if defined(DRAW3_CONTACT_TESTING)
	void ContactInputCoordinator::SetNextCommandFallbackMissHookForTesting(
		CommandFallbackMissHookForTesting* hook) noexcept
	{
		impl_->commandFallbackMissHookForTesting.store(hook, std::memory_order_release);
	}
#endif

	void ContactInputCoordinator::PauseNextCloseAfterRouteClosedForTesting(
		ContactClosePauseForTesting* pause) noexcept
	{
		impl_->closePauseForTesting.store(pause, std::memory_order_release);
	}

	void ContactInputCoordinator::AcknowledgeControlWake() noexcept
	{
		if (impl_->lastDequeuedControlWakeKind == ControlWakeKind::General)
			impl_->controlWakePending.store(false, std::memory_order_release);
	}

	void ContactInputCoordinator::ResetForNextRun() noexcept
	{
		// 仅由所有 producer 已静止且绘制线程已 join 后的 Host::Start 调用；不与 Closing writer 并发。
		impl_->closePauseForTesting.store(nullptr, std::memory_order_relaxed);
#if defined(DRAW3_CONTACT_TESTING)
		impl_->commandFallbackMissHookForTesting.store(nullptr, std::memory_order_relaxed);
#endif
		impl_->deferredIngressRecord = nullptr;
		impl_->deferredIngressPending.store(false, std::memory_order_relaxed);
		ContactRecord* discarded = nullptr;
		while (impl_->queue.try_dequeue(discarded)) {}
		for (const auto& block : impl_->blocks)
		{
			for (ContactRecord& record : block->records)
				ContactRecordAccess::ResetStoppedRecord(record);
			block->freeMask.store(kAllSlotsFree, std::memory_order_release);
		}
		impl_->downEnqueuedCount.store(0, std::memory_order_relaxed);
		impl_->downDequeuedCount.store(0, std::memory_order_relaxed);
		impl_->commandWakeReservations.store(0, std::memory_order_relaxed);
		impl_->commandWakePublishedOrdinal = 0;
		impl_->commandWakeConsumedOrdinal = 0;
		impl_->commandFallbackWrite.store(0, std::memory_order_relaxed);
		impl_->commandFallbackRead.store(0, std::memory_order_relaxed);
		impl_->controlWakePending.store(false, std::memory_order_relaxed);
		impl_->controlWakeFallbackPending.store(false, std::memory_order_relaxed);
		impl_->controlWakeDrainTarget.store(0, std::memory_order_relaxed);
		impl_->failNextControlWakeEnqueueForTesting.store(false, std::memory_order_relaxed);
		impl_->failNextCommandWakeEnqueueForTesting.store(false, std::memory_order_relaxed);
		impl_->quarantinedContacts.store(0, std::memory_order_relaxed);
		impl_->lastDequeuedControlWakeKind = ControlWakeKind::General;
		if (impl_->wakeEvent) ResetEvent(impl_->wakeEvent);
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
		int64_t deadline = 0;
		if (!TryComputeQpcDeadline(
			now.QuadPart, impl_->qpcFrequency, timeoutMilliseconds, deadline)) return false;
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
				const DWORD coarseWait = SafeCoarseWaitMilliseconds(coarseWaitMilliseconds);
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
		int64_t deadline = 0;
		if (!TryComputeQpcDeadline(
			now.QuadPart, impl_->qpcFrequency, timeoutMilliseconds, deadline)) return;
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
				const DWORD coarseWait = SafeCoarseWaitMilliseconds(coarseWaitMilliseconds);
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
		snapshot.controlWakeEnqueueFailures =
			impl_->controlWakeEnqueueFailures.load(std::memory_order_relaxed);
		snapshot.controlWakeInlineRecoveries =
			impl_->controlWakeInlineRecoveries.load(std::memory_order_relaxed);
		snapshot.activeWaits = impl_->activeWaits.load(std::memory_order_relaxed);
		snapshot.slotCapacity = impl_->slotCapacity;
		snapshot.occupiedSlots = impl_->OccupiedSlotCount();
		return snapshot;
	}
}
