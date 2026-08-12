#pragma once

#include <Windows.h>

#include <algorithm>
#include <cmath>

namespace Inkeys::UI::Bar
{
	[[nodiscard]] constexpr bool IsBarWindowRectEmpty(const RECT& value) noexcept
	{
		return value.left >= value.right || value.top >= value.bottom;
	}

	[[nodiscard]] constexpr RECT IntersectBarWindowRect(
		const RECT& left, const RECT& right) noexcept
	{
		RECT result{
			(left.left > right.left) ? left.left : right.left,
			(left.top > right.top) ? left.top : right.top,
			(left.right < right.right) ? left.right : right.right,
			(left.bottom < right.bottom) ? left.bottom : right.bottom,
		};
		return IsBarWindowRectEmpty(result) ? RECT{} : result;
	}

	constexpr void UnionBarWindowRect(RECT& target, const RECT& value) noexcept
	{
		if (IsBarWindowRectEmpty(value)) return;
		if (IsBarWindowRectEmpty(target))
		{
			target = value;
			return;
		}
		target.left = (target.left < value.left) ? target.left : value.left;
		target.top = (target.top < value.top) ? target.top : value.top;
		target.right = (target.right > value.right) ? target.right : value.right;
		target.bottom = (target.bottom > value.bottom) ? target.bottom : value.bottom;
	}

	[[nodiscard]] inline RECT InflateAndClipBarWindowRect(
		RECT value, LONG padding, const RECT& layoutBounds) noexcept
	{
		if (IsBarWindowRectEmpty(value)) return {};
		padding = (padding > 0) ? padding : 0;
		value.left -= padding;
		value.top -= padding;
		value.right += padding;
		value.bottom += padding;
		return IntersectBarWindowRect(value, layoutBounds);
	}

	[[nodiscard]] constexpr RECT DeflateBarWindowRect(
		RECT value, LONG padding) noexcept
	{
		if (IsBarWindowRectEmpty(value)) return {};
		padding = (padding > 0) ? padding : 0;
		value.left += padding;
		value.top += padding;
		value.right -= padding;
		value.bottom -= padding;
		return IsBarWindowRectEmpty(value) ? RECT{} : value;
	}

	[[nodiscard]] constexpr bool SameBarWindowRect(
		const RECT& left, const RECT& right) noexcept
	{
		return left.left == right.left && left.top == right.top
			&& left.right == right.right && left.bottom == right.bottom;
	}

	[[nodiscard]] constexpr POINT BarClientToLayoutPoint(
		POINT clientPoint, const RECT& committedViewport) noexcept
	{
		clientPoint.x += committedViewport.left;
		clientPoint.y += committedViewport.top;
		return clientPoint;
	}

	[[nodiscard]] constexpr POINT BarLayoutToClientPoint(
		POINT layoutPoint, const RECT& committedViewport) noexcept
	{
		layoutPoint.x -= committedViewport.left;
		layoutPoint.y -= committedViewport.top;
		return layoutPoint;
	}

	[[nodiscard]] constexpr POINT BarScreenToLayoutPoint(
		POINT screenPoint, POINT monitorOrigin) noexcept
	{
		screenPoint.x -= monitorOrigin.x;
		screenPoint.y -= monitorOrigin.y;
		return screenPoint;
	}

	[[nodiscard]] constexpr POINT BarLayoutToScreenPoint(
		POINT layoutPoint, POINT monitorOrigin) noexcept
	{
		layoutPoint.x += monitorOrigin.x;
		layoutPoint.y += monitorOrigin.y;
		return layoutPoint;
	}

	[[nodiscard]] constexpr RECT BarLayoutToSurfaceRect(
		RECT layoutRect, POINT capacityOrigin) noexcept
	{
		layoutRect.left -= capacityOrigin.x;
		layoutRect.right -= capacityOrigin.x;
		layoutRect.top -= capacityOrigin.y;
		layoutRect.bottom -= capacityOrigin.y;
		return layoutRect;
	}

	[[nodiscard]] constexpr RECT BarLayoutToClientRect(
		RECT layoutRect, const RECT& viewport) noexcept
	{
		layoutRect.left -= viewport.left;
		layoutRect.right -= viewport.left;
		layoutRect.top -= viewport.top;
		layoutRect.bottom -= viewport.top;
		return layoutRect;
	}

	[[nodiscard]] constexpr bool ContainsBarWindowRect(
		const RECT& outer, const RECT& inner) noexcept
	{
		return IsBarWindowRectEmpty(inner)
			|| (!IsBarWindowRectEmpty(outer)
				&& outer.left <= inner.left && outer.top <= inner.top
				&& outer.right >= inner.right && outer.bottom >= inner.bottom);
	}

	struct BarWindowCapacityDecision
	{
		POINT origin{};
		SIZE size{};
		bool sizeChanged = false;
	};

	// 容量始终围绕主按钮锚点；预测遗漏时只扩到足以覆盖本帧真实外框。
	[[nodiscard]] inline BarWindowCapacityDecision ResolveBarWindowCapacity(
		SIZE currentSize,
		POINT anchor,
		const RECT& currentContentBounds,
		const RECT& layoutBounds,
		LONG padding) noexcept
	{
		const SIZE previousSize = currentSize;
		currentSize.cx = (std::max)(1L, currentSize.cx);
		currentSize.cy = (std::max)(1L, currentSize.cy);
		RECT requiredBounds = InflateAndClipBarWindowRect(
			currentContentBounds, padding, layoutBounds);
		if (!IsBarWindowRectEmpty(requiredBounds))
		{
			const LONG horizontalReach = (std::max)(
				(std::max)(0L, anchor.x - requiredBounds.left),
				(std::max)(0L, requiredBounds.right - anchor.x));
			const LONG verticalReach = (std::max)(
				(std::max)(0L, anchor.y - requiredBounds.top),
				(std::max)(0L, requiredBounds.bottom - anchor.y));
			currentSize.cx = (std::max)(currentSize.cx,
				(std::max)(1L, horizontalReach * 2));
			currentSize.cy = (std::max)(currentSize.cy,
				(std::max)(1L, verticalReach * 2));
		}

		const POINT origin{
			anchor.x - currentSize.cx / 2,
			anchor.y - currentSize.cy / 2 };
		const RECT capacityBounds{
			origin.x, origin.y,
			origin.x + currentSize.cx, origin.y + currentSize.cy };
		if (!ContainsBarWindowRect(capacityBounds, requiredBounds))
		{
			// 奇数尺寸或边界舍入仍不足时，以一像素对称余量完成兜底。
			currentSize.cx += 2;
			currentSize.cy += 2;
		}

		return {
			POINT{ anchor.x - currentSize.cx / 2,
				anchor.y - currentSize.cy / 2 },
			currentSize,
			currentSize.cx != previousSize.cx
				|| currentSize.cy != previousSize.cy
		};
	}

	struct BarWindowViewportDecision
	{
		RECT viewport{};
		bool changed = false;
	};

	// 动画期间只允许扩张；进入 idle 的最后一帧才按当前外框一次收缩。
	class BarWindowViewportController
	{
	public:
		[[nodiscard]] BarWindowViewportDecision Resolve(
			const RECT& currentContentBounds,
			const RECT& predictedEnvelope,
			const RECT& layoutBounds,
			LONG padding,
			bool settleToCurrent,
			POINT committedTranslation = {}) noexcept
		{
			RECT requested = currentContentBounds;
			if (!settleToCurrent)
				UnionBarWindowRect(requested, predictedEnvelope);
			requested = InflateAndClipBarWindowRect(
				requested, padding, layoutBounds);
			if (IsBarWindowRectEmpty(requested)) requested = layoutBounds;

			RECT next = requested;
			if (initialized_ && !settleToCurrent)
			{
				next = committed_;
				next.left += committedTranslation.x;
				next.right += committedTranslation.x;
				next.top += committedTranslation.y;
				next.bottom += committedTranslation.y;
				const LONG width = next.right - next.left;
				const LONG height = next.bottom - next.top;
				if (next.left < layoutBounds.left)
					next.left = layoutBounds.left, next.right = next.left + width;
				if (next.right > layoutBounds.right)
					next.right = layoutBounds.right, next.left = next.right - width;
				if (next.top < layoutBounds.top)
					next.top = layoutBounds.top, next.bottom = next.top + height;
				if (next.bottom > layoutBounds.bottom)
					next.bottom = layoutBounds.bottom, next.top = next.bottom - height;
				UnionBarWindowRect(next, requested);
				next = IntersectBarWindowRect(next, layoutBounds);
			}
			const bool changed = !initialized_
				|| !SameBarWindowRect(next, committed_);
			return { next, changed };
		}

		void Commit(const RECT& viewport) noexcept
		{
			committed_ = viewport;
			initialized_ = !IsBarWindowRectEmpty(viewport);
		}

		[[nodiscard]] constexpr RECT Committed() const noexcept
		{
			return committed_;
		}

		[[nodiscard]] constexpr bool Initialized() const noexcept
		{
			return initialized_;
		}

	private:
		RECT committed_{};
		bool initialized_ = false;
	};
}
