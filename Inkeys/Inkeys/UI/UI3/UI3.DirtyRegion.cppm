module;

#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

// L1 脏区跟踪与调试包络；只处理几何数值，不拥有呈现目标。
export module Inkeys.UI.UI3.DirtyRegion;

export namespace Inkeys::UI::UI3
{
	// Debug 框颜色和线宽由 Bar/Whiteboard 共同使用，避免两套语义漂移。
	inline constexpr FLOAT DebugFrameWidth = 1.0F;
	inline constexpr FLOAT DebugDirtyFrameInset = 2.5F;
	inline constexpr FLOAT DebugWindowFrameInset = DebugFrameWidth / 2.0F;
	inline constexpr COLORREF DebugDirtyColor = RGB(255, 0, 0);
	inline constexpr COLORREF DebugPresentedColor = RGB(0, 255, 0);

	[[nodiscard]] inline constexpr COLORREF ResolveDebugFrameColor(
		bool finalIdleFrame) noexcept
	{
		return finalIdleFrame ? DebugPresentedColor : DebugDirtyColor;
	}

	using DirtyVisualKey = std::uint64_t;

	class DirtyRegionTracker
	{
	public:
		DirtyRegionTracker()
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

		void Observe(DirtyVisualKey key, const RECT& currentBounds)
		{
			auto& record = visualRecords_[key];
			if (record.observedFrame != frameSerial_)
				observedKeys_.push_back(key);
			record.currentBounds = ClipToWindow(Normalize(currentBounds));
			record.observedFrame = frameSerial_;
		}

		void MarkChanged(DirtyVisualKey key)
		{
			auto& record = visualRecords_[key];
			if (record.changed) return;
			record.changed = true;
			changedKeys_.push_back(key);
		}

		[[nodiscard]] bool ShouldObserve(DirtyVisualKey key) const noexcept
		{
			if (!initialized_ || fullDamagePending_) return true;
			const auto record = visualRecords_.find(key);
			return record != visualRecords_.end() && record->second.changed;
		}

		[[nodiscard]] bool HasOnlyChangedKeys(
			DirtyVisualKey first, DirtyVisualKey second) const noexcept
		{
			if (changedKeys_.empty()) return false;
			for (DirtyVisualKey key : changedKeys_)
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

		void TranslateCommitted(POINT translation) noexcept
		{
			if (translation.x == 0 && translation.y == 0) return;
			auto Translate = [&](RECT& value)
				{
					if (IsEmpty(value)) return;
					value.left += translation.x;
					value.right += translation.x;
					value.top += translation.y;
					value.bottom += translation.y;
				};
			Translate(pendingDamage_);
			for (auto& [key, record] : visualRecords_)
			{
				if (record.hasCommittedBounds) Translate(record.committedBounds);
				Translate(record.currentBounds);
			}
		}

		[[nodiscard]] RECT ResolveDamage(bool requireFallback)
		{
			for (DirtyVisualKey key : changedKeys_)
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
				for (DirtyVisualKey key : observedKeys_)
				{
					auto found = visualRecords_.find(key);
					if (found == visualRecords_.end()) continue;
					auto& record = found->second;
					record.hasCommittedBounds = true;
					record.committedBounds = record.currentBounds;
				}
				for (DirtyVisualKey key : changedKeys_)
				{
					auto found = visualRecords_.find(key);
					if (found == visualRecords_.end()
						|| found->second.observedFrame == frameSerial_)
						continue;
					found->second.hasCommittedBounds = false;
					found->second.committedBounds = {};
				}
			}
			for (DirtyVisualKey key : changedKeys_)
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

		// 最近一次 ResolveDamage 是否返回整窗（诊断用）。
		[[nodiscard]] bool ResolvedFullDamage() const noexcept { return resolvedFullDamage_; }
		// 诊断（仅调试模式调用）：逐个访问本帧已标记的键及其旧包络、本帧包络（未观察则为空）。
		template <class Visit>
		void VisitChangedKeys(Visit&& visit) const
		{
			for (DirtyVisualKey key : changedKeys_)
			{
				const auto found = visualRecords_.find(key);
				if (found == visualRecords_.end()) continue;
				const auto& record = found->second;
				visit(key, record.hasCommittedBounds ? record.committedBounds : RECT{},
					record.observedFrame == frameSerial_ ? record.currentBounds : RECT{});
			}
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
		std::unordered_map<DirtyVisualKey, VisualRecord> visualRecords_;
		std::vector<DirtyVisualKey> changedKeys_;
		std::vector<DirtyVisualKey> observedKeys_;
		std::uint64_t frameSerial_ = 0;
		bool fullDamagePending_ = false;
		bool resolvedFullDamage_ = false;
		bool initialized_ = false;
	};

	[[nodiscard]] inline RECT IntersectDirtyRect(
		const RECT& left, const RECT& right) noexcept
	{
		RECT result{
			left.left > right.left ? left.left : right.left,
			left.top > right.top ? left.top : right.top,
			left.right < right.right ? left.right : right.right,
			left.bottom < right.bottom ? left.bottom : right.bottom };
		return DirtyRegionTracker::IsEmpty(result) ? RECT{} : result;
	}

	[[nodiscard]] inline RECT ResolveLightBorderDamage(
		const RECT& outerBounds,
		const RECT& contentBounds,
		const RECT& lightInfluenceBounds) noexcept
	{
		if (DirtyRegionTracker::IsEmpty(outerBounds)
			|| DirtyRegionTracker::IsEmpty(contentBounds)
			|| DirtyRegionTracker::IsEmpty(lightInfluenceBounds))
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
			DirtyRegionTracker::UnionInPlace(result,
				IntersectDirtyRect(edgeBand, lightInfluenceBounds));
		return result;
	}

	[[nodiscard]] inline RECT ResolveScaledDirtyBounds(
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

	// 调试框最小可见边长（像素）：1px 描边内缩 2.5px 后仍保留正尺寸。
	inline constexpr LONG DebugFrameMinimumSide = 8;

	struct DebugFrameRect
	{
		// 描边覆盖的整数像素范围：必须并入本帧提交区，并记为“上一帧调试框”供下一帧擦除。
		RECT bounds{};
		// 内缩后的描边矩形；无正尺寸时不画。
		FLOAT left = 0.0F, top = 0.0F, right = 0.0F, bottom = 0.0F;
		[[nodiscard]] bool Visible() const noexcept { return right > left && bottom > top; }
	};

	// 调试框几何：先裁到 clip（为空表示不裁）；任一边短于最小可见边长时以原中心向外扩，
	// 再平移回 clip 内——保活像素、细条脏区也能看到框，且框不会画到窗口之外。
	[[nodiscard]] inline DebugFrameRect ResolveDebugFrameRect(
		const RECT& target, const RECT& clip,
		FLOAT inset = DebugDirtyFrameInset) noexcept
	{
		DebugFrameRect result{};
		const bool hasClip = !DirtyRegionTracker::IsEmpty(clip);
		RECT r = hasClip ? IntersectDirtyRect(target, clip) : target;
		if (DirtyRegionTracker::IsEmpty(r)) return result;
		const auto fit = [hasClip](LONG& low, LONG& high, LONG clipLow, LONG clipHigh)
			{
				if (high - low >= DebugFrameMinimumSide) return;
				const LONG grow = DebugFrameMinimumSide - (high - low);
				low -= grow / 2;
				high = low + DebugFrameMinimumSide;
				if (!hasClip) return;
				if (clipHigh - clipLow <= DebugFrameMinimumSide) { low = clipLow; high = clipHigh; return; }
				if (low < clipLow) { high += clipLow - low; low = clipLow; }
				if (high > clipHigh) { low -= high - clipHigh; high = clipHigh; }
			};
		fit(r.left, r.right, clip.left, clip.right);
		fit(r.top, r.bottom, clip.top, clip.bottom);
		result.bounds = r;
		result.left = static_cast<FLOAT>(r.left) + inset;
		result.top = static_cast<FLOAT>(r.top) + inset;
		result.right = static_cast<FLOAT>(r.right) - inset;
		result.bottom = static_cast<FLOAT>(r.bottom) - inset;
		return result;
	}

	struct DebugDamageResolution
	{
		// 本帧框所标记的范围（业务刷新范围 ∪ 调试文字；最终空闲帧无新范围时沿用上一帧框）。
		RECT frameTarget{};
		// 实际绘制的框（最小可见尺寸、已裁剪）；frame.bounds 已并入 presentDamage。
		DebugFrameRect frame{};
		RECT presentDamage{};
	};

	[[nodiscard]] inline DebugDamageResolution ResolveDebugDamage(
		const RECT& businessDamage,
		const RECT& previousTextBounds,
		const RECT& previousFrameBounds,
		const RECT& currentTextBounds,
		bool debugEnabled,
		bool finalIdleFrame = false,
		const RECT& clip = {}) noexcept
	{
		DebugDamageResolution result{};
		result.frameTarget = businessDamage;
		if (debugEnabled)
		{
			if (finalIdleFrame && DirtyRegionTracker::IsEmpty(
				result.frameTarget))
				result.frameTarget = previousFrameBounds;
			DirtyRegionTracker::UnionInPlace(
				result.frameTarget, currentTextBounds);
			result.frame = ResolveDebugFrameRect(result.frameTarget, clip);
		}

		result.presentDamage = businessDamage;
		// 旧覆盖层始终进入提交区，关闭调试时也能完整擦除文字与红框。
		DirtyRegionTracker::UnionInPlace(
			result.presentDamage, previousTextBounds);
		DirtyRegionTracker::UnionInPlace(
			result.presentDamage, previousFrameBounds);
		if (debugEnabled)
		{
			DirtyRegionTracker::UnionInPlace(
				result.presentDamage, currentTextBounds);
			DirtyRegionTracker::UnionInPlace(
				result.presentDamage, result.frameTarget);
			DirtyRegionTracker::UnionInPlace(
				result.presentDamage, result.frame.bounds);
		}
		return result;
	}
}
