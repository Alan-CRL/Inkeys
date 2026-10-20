module;
#include <algorithm>
#include <array>
#include <cmath>

export module Inkeys.UI.Bar.RecallAttributeLayout;
export import Inkeys.UI.Bar.Metrics;

export namespace Inkeys::UI::Bar
{
	// 撤回属性单按钮布局：面板 80x80，内置单 70x70 Redo，留白/避让语义与橡皮属性一致。
	struct RecallAttributeRect
	{
		double left = 0, top = 0, right = 0, bottom = 0;
		double Width() const noexcept { return right - left; }
		double Height() const noexcept { return bottom - top; }
		bool Contains(double x, double y) const noexcept
		{ return x >= left && x < right && y >= top && y < bottom; }
		friend bool operator==(const RecallAttributeRect&, const RecallAttributeRect&) = default;
	};
	enum class RecallAttributeItem : int
	{
		Redo, Count, None = -1
	};
	struct RecallAttributeLayout
	{
		RecallAttributeRect panel, anchor, work;
		std::array<RecallAttributeRect, 1> items{};
		bool below = false, horizontalOverflow = false;
		friend bool operator==(const RecallAttributeLayout&, const RecallAttributeLayout&) = default;
	};
	struct RecallAttributeLayoutInput
	{
		RecallAttributeRect main, anchor, work;
		double dpiScale = 1, zoom = 1;
		bool below = false;
		// 直拖或方向过渡沿用已呈现侧，不能因临时工作区坐标立即翻边。
		bool lockPanelSide = false;
	};
	inline RecallAttributeRect OffsetRecallRect(RecallAttributeRect r, double x, double y) noexcept
	{ return {r.left + x, r.top + y, r.right + x, r.bottom + y}; }
	inline RecallAttributeRect IntersectRecallRect(RecallAttributeRect a, RecallAttributeRect b) noexcept
	{
		return { (std::max)(a.left, b.left), (std::max)(a.top, b.top),
			(std::max)((std::max)(a.left, b.left), (std::min)(a.right, b.right)),
			(std::max)((std::max)(a.top, b.top), (std::min)(a.bottom, b.bottom)) };
	}
	inline int ResolveRecallAttributeRelease(int pressed, int hit, bool inside, bool cancelled) noexcept
	{
		if (cancelled || !inside || pressed < 0) return -1;
		return hit == pressed ? pressed : -1;
	}
	inline double RecallRectCenterX(RecallAttributeRect r) noexcept { return (r.left + r.right) / 2; }
	inline double RecallRectCenterY(RecallAttributeRect r) noexcept { return (r.top + r.bottom) / 2; }
	inline RecallAttributeLayoutInput RebaseRecallAttributeLayoutInput(
		RecallAttributeLayoutInput input, RecallAttributeRect anchor) noexcept
	{
		const double x = RecallRectCenterX(anchor) - RecallRectCenterX(input.anchor);
		const double y = RecallRectCenterY(anchor) - RecallRectCenterY(input.anchor);
		input.main = OffsetRecallRect(input.main, x, y);
		input.anchor = OffsetRecallRect(input.anchor, x, y);
		input.work = OffsetRecallRect(input.work, x, y);
		return input;
	}
	inline constexpr double RecallAttributeStandardGapDip = 5.0;
	inline RecallAttributeLayout ResolveRecallAttributeLayout(const RecallAttributeLayoutInput& input) noexcept
	{
		RecallAttributeLayout r; r.anchor = input.anchor; r.work = input.work;
		const double height = input.main.Height() > 0 ? input.main.Height() : BarMainBarHeightDip;
		const double button = BarButtonTwoSideDip;
		const double gap = RecallAttributeStandardGapDip;
		// 单按钮面板：左右各 5 DIP 留白，自然宽 80，与主栏高一致。
		const double naturalWidth = gap + button + gap;
		const double available = (std::max)(1.0, input.work.Width());
		const double halfButton = (std::min)(button / 2, available / 2);
		// Redo 中心锚定主栏撤回入口，窄区仅裁剪溢出，不压缩留白或按钮。
		const double redoCenter = (std::clamp)(RecallRectCenterX(input.anchor), input.work.left + halfButton, input.work.right - halfButton);
		const double idealX = redoCenter - (gap + button / 2);
		r.horizontalOverflow = naturalWidth > available || idealX < input.work.left || idealX + naturalWidth > input.work.right;
		const double panelGap = BarMainButtonToMainBarGapDip;
		const double down = input.work.bottom - input.main.bottom - panelGap, up = input.main.top - input.work.top - panelGap;
		r.below = input.below;
		if (!input.lockPanelSide && (r.below ? down : up) < height && (r.below ? up : down) > (r.below ? down : up)) r.below = !r.below;
		const double y = (std::clamp)(r.below ? input.main.bottom + panelGap : input.main.top - panelGap - height,
			input.work.top, (std::max)(input.work.top, input.work.bottom - height));
		r.panel = { (std::max)(idealX, input.work.left), y, (std::min)(idealX + naturalWidth, input.work.right), y + height };
		const double cy = y + height / 2;
		const double redoLeft = idealX + gap;
		r.items[0] = { redoLeft, cy - button / 2, redoLeft + button, cy + button / 2 };
		return r;
	}
}
