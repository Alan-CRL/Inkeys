module;

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

module Inkeys.Startup.Progress;

namespace Inkeys::Startup
{
	namespace
	{
		constexpr auto MilestoneCount = static_cast<std::size_t>(Milestone::Count);
		static_assert(MilestoneCount < 64);

		constexpr std::array<std::uint16_t, MilestoneCount> Weights{
			10, 15, 20, 15, 10, 15, 30, 15, 10, 15, 35, 85, 25, 25,
			30, 20, 40, 30, 45, 55, 25, 20, 15, 35, 30, 25, 15, 30,
			35, 35, 15, 25, 20, 5, 20, 10, 20, 10, 10, 15, 40,
		};

		[[nodiscard]] constexpr std::uint64_t Bit(Milestone milestone) noexcept
		{
			return std::uint64_t{ 1 } << static_cast<unsigned>(milestone);
		}

		[[nodiscard]] constexpr bool IsPreviewOnly(Milestone milestone) noexcept
		{
			return milestone == Milestone::CacheClassified
				|| milestone == Milestone::PreviewOwnerReady
				|| milestone == Milestone::PreviewRenderClientReady
				|| milestone == Milestone::PreviewFirstFrameCommitted;
		}

		[[nodiscard]] consteval std::uint32_t NominalTotal() noexcept
		{
			std::uint32_t total = 0;
			for (const auto weight : Weights) total += weight;
			return total;
		}

		static_assert(NominalTotal() == 1000);
		std::atomic<ProgressTracker*> activeTracker = nullptr;
	}

	Plan Plan::ForStartup(bool previewEnabled) noexcept
	{
		Plan plan;
		for (std::size_t index = 0; index < MilestoneCount; ++index)
		{
			const auto milestone = static_cast<Milestone>(index);
			if (!previewEnabled && IsPreviewOnly(milestone)) continue;
			plan.milestoneMask_ |= Bit(milestone);
			plan.totalUnits += Weights[index];
		}
		return plan;
	}

	bool Plan::Contains(Milestone milestone) const noexcept
	{
		return milestone < Milestone::Count
			&& (milestoneMask_ & Bit(milestone)) != 0;
	}

	std::uint16_t Plan::Weight(Milestone milestone) const noexcept
	{
		return Contains(milestone)
			? Weights[static_cast<std::size_t>(milestone)] : 0;
	}

	bool Plan::PreviewEnabled() const noexcept
	{
		return Contains(Milestone::CacheClassified);
	}

	ProgressTracker::ProgressTracker(Plan plan,
		std::chrono::steady_clock::time_point startTime) noexcept
		: plan_(plan), startTime_(startTime)
	{
	}

	void ProgressTracker::Lock() const noexcept
	{
		while (lock_.test_and_set(std::memory_order_acquire))
		{
			// 回调只保护几个标量，短暂自旋避免引入可能抛异常的锁路径。
		}
	}

	void ProgressTracker::Unlock() const noexcept
	{
		lock_.clear(std::memory_order_release);
	}

	bool ProgressTracker::Complete(Milestone milestone) noexcept
	{
		if (!plan_.Contains(milestone)) return false;
		const auto bit = Bit(milestone);
		Lock();
		if (failed_ || (completedMask_ & bit) != 0)
		{
			Unlock();
			return false;
		}
		completedMask_ |= bit;
		completedUnits_ += plan_.Weight(milestone);
		completedUnits_ = (std::min)(completedUnits_, plan_.totalUnits);
		++revision_;
		Unlock();
		return true;
	}

	bool ProgressTracker::Fail(std::uint32_t failureCode) noexcept
	{
		Lock();
		if (failed_)
		{
			Unlock();
			return false;
		}
		failed_ = true;
		failureCode_ = failureCode;
		++revision_;
		Unlock();
		return true;
	}

	Snapshot ProgressTracker::GetSnapshot() const noexcept
	{
		Lock();
		Snapshot snapshot;
		snapshot.completedUnits = completedUnits_;
		snapshot.totalUnits = plan_.totalUnits;
		snapshot.actualRatio = plan_.totalUnits == 0 ? 0.0
			: static_cast<double>(completedUnits_) /
				static_cast<double>(plan_.totalUnits);
		snapshot.failed = failed_;
		snapshot.failureCode = failureCode_;
		snapshot.revision = revision_;
		snapshot.startTime = startTime_;
		Unlock();
		return snapshot;
	}

	void SetActiveTracker(ProgressTracker* tracker) noexcept
	{
		activeTracker.store(tracker, std::memory_order_release);
	}

	void ClearActiveTracker(ProgressTracker* tracker) noexcept
	{
		(void)activeTracker.compare_exchange_strong(tracker, nullptr,
			std::memory_order_acq_rel);
	}

	bool Report(Milestone milestone) noexcept
	{
		const auto tracker = activeTracker.load(std::memory_order_acquire);
		return tracker && tracker->Complete(milestone);
	}

	bool ReportFailure(std::uint32_t failureCode) noexcept
	{
		const auto tracker = activeTracker.load(std::memory_order_acquire);
		return tracker && tracker->Fail(failureCode);
	}

	Snapshot ActiveSnapshot() noexcept
	{
		const auto tracker = activeTracker.load(std::memory_order_acquire);
		return tracker ? tracker->GetSnapshot() : Snapshot{};
	}
}
