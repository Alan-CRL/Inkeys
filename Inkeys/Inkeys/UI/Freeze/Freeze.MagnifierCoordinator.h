#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace Inkeys::UI::Freeze::MagnifierInternal
{
	template <typename Handle, typename IsWindow, typename ProcessId, typename IsChild>
	[[nodiscard]] std::vector<Handle> BuildFilterList(
		std::span<const Handle> candidates,
		std::uint32_t currentProcessId,
		IsWindow&& isWindow,
		ProcessId&& processId,
		IsChild&& isChild)
	{
		std::vector<Handle> result;
		result.reserve(candidates.size());
		for (const Handle handle : candidates)
		{
			if (!handle || !isWindow(handle) ||
				processId(handle) != currentProcessId || isChild(handle))
				continue;
			if (std::find(result.begin(), result.end(), handle) != result.end())
				continue;
			result.emplace_back(handle);
		}
		return result;
	}

	struct Request
	{
		std::uint64_t revision = 0;
		bool active = false;
	};

	enum class Stage : std::uint8_t
	{
		Applied,
		Hidden,
		Superseded,
		Stopped,
		InvalidTarget,
		PrepareHidden,
		Filter,
		Source,
		Invalidate,
		Redraw,
		Reveal,
		Conceal,
	};

	struct TransactionResult
	{
		Stage stage = Stage::Superseded;
		bool concealed = false;
	};

	struct CoordinatorSnapshot
	{
		Request latest{};
		std::uint64_t processedRevision = 0;
		std::uint64_t appliedRevision = 0;
		bool visible = false;
		bool stopped = false;
	};

	class Coordinator
	{
	public:
		void Reset() noexcept
		{
			std::scoped_lock lock(mutex_);
			latest_ = {};
			processedRevision_ = 0;
			appliedRevision_ = 0;
			visible_ = false;
			stopped_ = false;
		}

		void Publish(Request request) noexcept
		{
			{
				std::scoped_lock lock(mutex_);
				if (stopped_ || request.revision <= latest_.revision) return;
				latest_ = request;
			}
			condition_.notify_one();
		}

		void Stop() noexcept
		{
			{
				std::scoped_lock lock(mutex_);
				stopped_ = true;
			}
			condition_.notify_all();
		}

		[[nodiscard]] std::optional<Request> WaitNext() noexcept
		{
			std::unique_lock lock(mutex_);
			condition_.wait(lock, [this]
				{
					return stopped_ || latest_.revision > processedRevision_;
				});
			if (stopped_) return std::nullopt;
			return latest_;
		}

		[[nodiscard]] bool IsCurrentActive(Request request) const noexcept
		{
			std::scoped_lock lock(mutex_);
			return !stopped_ && latest_.revision == request.revision &&
				latest_.active && request.active;
		}

		[[nodiscard]] bool IsStopped() const noexcept
		{
			std::scoped_lock lock(mutex_);
			return stopped_;
		}

		[[nodiscard]] bool CommitApplied(Request request) noexcept
		{
			std::scoped_lock lock(mutex_);
			processedRevision_ = std::max(processedRevision_, request.revision);
			if (stopped_ || latest_.revision != request.revision ||
				!latest_.active || !request.active)
				return false;
			appliedRevision_ = request.revision;
			visible_ = true;
			return true;
		}

		void CompleteWithoutApply(Request request, bool concealed) noexcept
		{
			std::scoped_lock lock(mutex_);
			processedRevision_ = std::max(processedRevision_, request.revision);
			if (!concealed) return;
			appliedRevision_ = 0;
			visible_ = false;
		}

		[[nodiscard]] CoordinatorSnapshot Snapshot() const noexcept
		{
			std::scoped_lock lock(mutex_);
			return { latest_, processedRevision_, appliedRevision_, visible_, stopped_ };
		}

	private:
		mutable std::mutex mutex_;
		std::condition_variable condition_;
		Request latest_{};
		std::uint64_t processedRevision_ = 0;
		std::uint64_t appliedRevision_ = 0;
		bool visible_ = false;
		bool stopped_ = false;
	};

	template <typename Operations>
	[[nodiscard]] TransactionResult ExecutePresent(
		Coordinator& coordinator, Request request, Operations& operations) noexcept
	{
		auto obsoleteStage = [&coordinator]() noexcept
			{
				return coordinator.IsStopped() ? Stage::Stopped : Stage::Superseded;
			};
		auto cancel = [&operations](Stage stage) noexcept
			{
				return TransactionResult{ stage, operations.Conceal() };
			};
		auto requireCurrent = [&]() noexcept -> std::optional<TransactionResult>
			{
				if (coordinator.IsCurrentActive(request)) return std::nullopt;
				return cancel(obsoleteStage());
			};

		if (const auto obsolete = requireCurrent()) return *obsolete;
		if (!operations.TargetsCurrent()) return cancel(Stage::InvalidTarget);
		if (!operations.PrepareHidden()) return cancel(Stage::PrepareHidden);
		if (const auto obsolete = requireCurrent()) return *obsolete;
		if (!operations.TargetsCurrent()) return cancel(Stage::InvalidTarget);
		if (!operations.SubmitFilter()) return cancel(Stage::Filter);
		if (const auto obsolete = requireCurrent()) return *obsolete;
		if (!operations.TargetsCurrent()) return cancel(Stage::InvalidTarget);
		if (!operations.SubmitSource()) return cancel(Stage::Source);
		if (const auto obsolete = requireCurrent()) return *obsolete;
		if (!operations.Invalidate()) return cancel(Stage::Invalidate);
		if (const auto obsolete = requireCurrent()) return *obsolete;
		if (!operations.Redraw()) return cancel(Stage::Redraw);
		if (const auto obsolete = requireCurrent()) return *obsolete;
		if (!operations.TargetsCurrent()) return cancel(Stage::InvalidTarget);
		if (!operations.Reveal()) return cancel(Stage::Reveal);
		if (const auto obsolete = requireCurrent()) return *obsolete;
		if (!operations.TargetsCurrent()) return cancel(Stage::InvalidTarget);
		return { Stage::Applied, false };
	}
}
