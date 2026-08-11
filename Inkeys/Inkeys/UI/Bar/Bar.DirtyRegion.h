#pragma once

#include <Windows.h>

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace Inkeys::UI::Bar
{
	using BarDirtyVisualKey = std::uint64_t;

	class BarDirtyRegionTracker
	{
	public:
		void BeginFrame(const RECT& windowBounds)
		{
			windowBounds_ = Normalize(windowBounds);
			observedBounds_.clear();
			if (!initialized_) fullDamagePending_ = true;
		}

		void Observe(BarDirtyVisualKey key, const RECT& currentBounds)
		{
			observedBounds_[key] = ClipToWindow(Normalize(currentBounds));
		}

		void MarkChanged(BarDirtyVisualKey key)
		{
			changedKeys_.insert(key);
		}

		void IncludeDamage(const RECT& previousBounds, const RECT& currentBounds)
		{
			UnionInPlace(pendingDamage_, ClipToWindow(Normalize(previousBounds)));
			UnionInPlace(pendingDamage_, ClipToWindow(Normalize(currentBounds)));
		}

		void ForceFullDamage()
		{
			fullDamagePending_ = true;
		}

		[[nodiscard]] RECT ResolveDamage(bool requireFallback)
		{
			for (BarDirtyVisualKey key : changedKeys_)
			{
				if (const auto committed = committedBounds_.find(key);
					committed != committedBounds_.end())
					UnionInPlace(pendingDamage_, committed->second);
				if (const auto observed = observedBounds_.find(key);
					observed != observedBounds_.end())
					UnionInPlace(pendingDamage_, observed->second);
			}

			if (fullDamagePending_ || (requireFallback && IsEmpty(pendingDamage_)))
				return windowBounds_;
			return ClipToWindow(pendingDamage_);
		}

		void CommitPresented()
		{
			// 只有完整呈现事务成功后才能推进快照，否则旧像素范围会在重试时丢失。
			committedBounds_ = observedBounds_;
			changedKeys_.clear();
			pendingDamage_ = {};
			fullDamagePending_ = false;
			initialized_ = true;
		}

		void RetainForRetry(bool forceFullDamage)
		{
			if (forceFullDamage) ForceFullDamage();
		}

		[[nodiscard]] bool HasPendingDamage() const noexcept
		{
			return fullDamagePending_ || !changedKeys_.empty()
				|| !IsEmpty(pendingDamage_);
		}

		[[nodiscard]] static constexpr bool IsEmpty(const RECT& value) noexcept
		{
			return value.left >= value.right || value.top >= value.bottom;
		}

		static constexpr void UnionInPlace(RECT& target, const RECT& value) noexcept
		{
			if (IsEmpty(value)) return;
			if (IsEmpty(target))
			{
				target = value;
				return;
			}
			if (value.left < target.left) target.left = value.left;
			if (value.top < target.top) target.top = value.top;
			if (value.right > target.right) target.right = value.right;
			if (value.bottom > target.bottom) target.bottom = value.bottom;
		}

	private:
		[[nodiscard]] static constexpr RECT Normalize(RECT value) noexcept
		{
			if (IsEmpty(value)) return {};
			return value;
		}

		[[nodiscard]] RECT ClipToWindow(RECT value) const noexcept
		{
			if (IsEmpty(value) || IsEmpty(windowBounds_)) return {};
			if (value.left < windowBounds_.left) value.left = windowBounds_.left;
			if (value.top < windowBounds_.top) value.top = windowBounds_.top;
			if (value.right > windowBounds_.right) value.right = windowBounds_.right;
			if (value.bottom > windowBounds_.bottom) value.bottom = windowBounds_.bottom;
			return IsEmpty(value) ? RECT{} : value;
		}

		RECT windowBounds_{};
		RECT pendingDamage_{};
		std::unordered_map<BarDirtyVisualKey, RECT> committedBounds_;
		std::unordered_map<BarDirtyVisualKey, RECT> observedBounds_;
		std::unordered_set<BarDirtyVisualKey> changedKeys_;
		bool fullDamagePending_ = false;
		bool initialized_ = false;
	};

	struct BarDebugDamageResolution
	{
		RECT frameTarget{};
		RECT presentDamage{};
	};

	[[nodiscard]] inline BarDebugDamageResolution ResolveBarDebugDamage(
		const RECT& businessDamage,
		const RECT& previousTextBounds,
		const RECT& previousFrameBounds,
		const RECT& currentTextBounds,
		bool debugEnabled) noexcept
	{
		BarDebugDamageResolution result{};
		result.frameTarget = businessDamage;
		if (debugEnabled)
			BarDirtyRegionTracker::UnionInPlace(
				result.frameTarget, currentTextBounds);

		result.presentDamage = businessDamage;
		// 旧覆盖层始终进入提交区，关闭调试时也能完整擦除文字与红框。
		BarDirtyRegionTracker::UnionInPlace(
			result.presentDamage, previousTextBounds);
		BarDirtyRegionTracker::UnionInPlace(
			result.presentDamage, previousFrameBounds);
		if (debugEnabled)
		{
			BarDirtyRegionTracker::UnionInPlace(
				result.presentDamage, currentTextBounds);
			BarDirtyRegionTracker::UnionInPlace(
				result.presentDamage, result.frameTarget);
		}
		return result;
	}
}
