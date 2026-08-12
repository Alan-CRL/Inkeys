#pragma once

#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Inkeys::UI::Bar
{
	using BarDirtyVisualKey = std::uint64_t;

	class BarDirtyRegionTracker
	{
	public:
		BarDirtyRegionTracker()
		{
			// 视觉对象数量稳定且很小，预留节点避免首轮动画期间反复扩容。
			visualRecords_.reserve(256);
			changedKeys_.reserve(256);
			observedKeys_.reserve(256);
		}

		void BeginFrame(const RECT& windowBounds)
		{
			windowBounds_ = Normalize(windowBounds);
			resolvedFullDamage_ = false;
			observedKeys_.clear();
			if (++frameSerial_ == 0)
			{
				frameSerial_ = 1;
				for (auto& [key, record] : visualRecords_)
					record.observedFrame = 0;
			}
			if (!initialized_) fullDamagePending_ = true;
		}

		void Observe(BarDirtyVisualKey key, const RECT& currentBounds)
		{
			auto& record = visualRecords_[key];
			if (record.observedFrame != frameSerial_)
				observedKeys_.push_back(key);
			record.currentBounds = ClipToWindow(Normalize(currentBounds));
			record.observedFrame = frameSerial_;
		}

		void MarkChanged(BarDirtyVisualKey key)
		{
			auto& record = visualRecords_[key];
			if (record.changed) return;
			record.changed = true;
			changedKeys_.push_back(key);
		}

		[[nodiscard]] bool ShouldObserve(BarDirtyVisualKey key) const noexcept
		{
			if (!initialized_ || fullDamagePending_) return true;
			const auto record = visualRecords_.find(key);
			return record != visualRecords_.end() && record->second.changed;
		}

		[[nodiscard]] bool HasOnlyChangedKeys(
			BarDirtyVisualKey first, BarDirtyVisualKey second) const noexcept
		{
			if (changedKeys_.empty()) return false;
			for (BarDirtyVisualKey key : changedKeys_)
				if (key != first && key != second) return false;
			return true;
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
				const auto found = visualRecords_.find(key);
				if (found == visualRecords_.end()) continue;
				const auto& record = found->second;
				if (record.hasCommittedBounds)
					UnionInPlace(pendingDamage_, record.committedBounds);
				if (record.observedFrame == frameSerial_)
					UnionInPlace(pendingDamage_, record.currentBounds);
			}

			resolvedFullDamage_ = fullDamagePending_
				|| (requireFallback && IsEmpty(pendingDamage_));
			if (resolvedFullDamage_)
				return windowBounds_;
			return ClipToWindow(pendingDamage_);
		}

		void CommitPresented()
		{
			// 只有完整呈现事务成功后才能推进快照，否则旧像素范围会在重试时丢失。
			if (!initialized_ || fullDamagePending_ || resolvedFullDamage_)
			{
				// 全窗口已真实呈现时才能一次性校准全部已观察视觉。
				for (auto& [key, record] : visualRecords_)
				{
					record.hasCommittedBounds = record.observedFrame == frameSerial_;
					record.committedBounds = record.hasCommittedBounds
						? record.currentBounds : RECT{};
				}
			}
			else
			{
				// 普通成功帧只推进本帧实际观察项，功能组移动后子控件快照也随之校准。
				for (BarDirtyVisualKey key : observedKeys_)
				{
					auto found = visualRecords_.find(key);
					if (found == visualRecords_.end()) continue;
					auto& record = found->second;
					record.hasCommittedBounds = true;
					record.committedBounds = record.currentBounds;
				}
				for (BarDirtyVisualKey key : changedKeys_)
				{
					auto found = visualRecords_.find(key);
					if (found == visualRecords_.end()
						|| found->second.observedFrame == frameSerial_)
						continue;
					found->second.hasCommittedBounds = false;
					found->second.committedBounds = {};
				}
			}
			for (BarDirtyVisualKey key : changedKeys_)
			{
				auto found = visualRecords_.find(key);
				if (found != visualRecords_.end()) found->second.changed = false;
			}
			changedKeys_.clear();
			pendingDamage_ = {};
			fullDamagePending_ = false;
			resolvedFullDamage_ = false;
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

		[[nodiscard]] std::size_t VisualRecordCount() const noexcept
		{
			return visualRecords_.size();
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
		struct VisualRecord
		{
			RECT committedBounds{};
			RECT currentBounds{};
			std::uint64_t observedFrame = 0;
			bool hasCommittedBounds = false;
			bool changed = false;
		};

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
		std::unordered_map<BarDirtyVisualKey, VisualRecord> visualRecords_;
		std::vector<BarDirtyVisualKey> changedKeys_;
		std::vector<BarDirtyVisualKey> observedKeys_;
		std::uint64_t frameSerial_ = 0;
		bool fullDamagePending_ = false;
		bool resolvedFullDamage_ = false;
		bool initialized_ = false;
	};

	[[nodiscard]] inline RECT IntersectBarDirtyRect(
		const RECT& left, const RECT& right) noexcept
	{
		RECT result{
			left.left > right.left ? left.left : right.left,
			left.top > right.top ? left.top : right.top,
			left.right < right.right ? left.right : right.right,
			left.bottom < right.bottom ? left.bottom : right.bottom };
		return BarDirtyRegionTracker::IsEmpty(result) ? RECT{} : result;
	}

	[[nodiscard]] inline RECT ResolveBarLightBorderDamage(
		const RECT& outerBounds,
		const RECT& contentBounds,
		const RECT& lightInfluenceBounds) noexcept
	{
		if (BarDirtyRegionTracker::IsEmpty(outerBounds)
			|| BarDirtyRegionTracker::IsEmpty(contentBounds)
			|| BarDirtyRegionTracker::IsEmpty(lightInfluenceBounds))
			return {};

		const LONG leftReach = contentBounds.left > outerBounds.left
			? contentBounds.left - outerBounds.left : 1;
		const LONG topReach = contentBounds.top > outerBounds.top
			? contentBounds.top - outerBounds.top : 1;
		const LONG rightReach = outerBounds.right > contentBounds.right
			? outerBounds.right - contentBounds.right : 1;
		const LONG bottomReach = outerBounds.bottom > contentBounds.bottom
			? outerBounds.bottom - contentBounds.bottom : 1;

		// 单一 ULW RECT 无法表达四条离散边，本层先裁掉没有边框贡献的光圈内部。
		const RECT edgeBands[] = {
			{ outerBounds.left, outerBounds.top, outerBounds.right,
				contentBounds.top + topReach },
			{ outerBounds.left, contentBounds.bottom - bottomReach,
				outerBounds.right, outerBounds.bottom },
			{ outerBounds.left, outerBounds.top,
				contentBounds.left + leftReach, outerBounds.bottom },
			{ contentBounds.right - rightReach, outerBounds.top,
				outerBounds.right, outerBounds.bottom },
		};
		RECT result{};
		for (const RECT& edgeBand : edgeBands)
			BarDirtyRegionTracker::UnionInPlace(result,
				IntersectBarDirtyRect(edgeBand, lightInfluenceBounds));
		return result;
	}

	[[nodiscard]] inline RECT ResolveBarScaledDirtyBounds(
		double left, double top, double right, double bottom,
		double pivotX, double pivotY, double scale, double zoom,
		LONG padding) noexcept
	{
		if (!std::isfinite(left) || !std::isfinite(top)
			|| !std::isfinite(right) || !std::isfinite(bottom)
			|| !std::isfinite(pivotX) || !std::isfinite(pivotY)
			|| !std::isfinite(scale) || scale <= 0.0
			|| !std::isfinite(zoom) || zoom <= 0.0
			|| left >= right || top >= bottom)
			return {};

		const double scaledLeft = pivotX + (left - pivotX) * scale;
		const double scaledTop = pivotY + (top - pivotY) * scale;
		const double scaledRight = pivotX + (right - pivotX) * scale;
		const double scaledBottom = pivotY + (bottom - pivotY) * scale;
		padding = padding > 0 ? padding : 0;
		return RECT{
			static_cast<LONG>(std::floor(scaledLeft * zoom)) - padding,
			static_cast<LONG>(std::floor(scaledTop * zoom)) - padding,
			static_cast<LONG>(std::ceil(scaledRight * zoom)) + padding,
			static_cast<LONG>(std::ceil(scaledBottom * zoom)) + padding };
	}

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
		bool debugEnabled,
		bool finalIdleFrame = false) noexcept
	{
		BarDebugDamageResolution result{};
		result.frameTarget = businessDamage;
		if (debugEnabled)
		{
			if (finalIdleFrame && BarDirtyRegionTracker::IsEmpty(
				result.frameTarget))
				result.frameTarget = previousFrameBounds;
			BarDirtyRegionTracker::UnionInPlace(
				result.frameTarget, currentTextBounds);
		}

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
