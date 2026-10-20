module;
#include <algorithm>
#include <cmath>
#include <optional>

export module Inkeys.UI.Bar.RecallAttributeMotion;
export import Inkeys.UI.Bar.RecallAttributeLayout;
import Inkeys.UI.Bar.Animation;
import Inkeys.UI.Bar.EraserAttributeMotion;

export namespace Inkeys::UI::Bar
{
	// 撤回属性只保留自身布局类型，开合时间线与几何解析直接复用共享属性实现。
	struct RecallSurfacePose
	{
		double scale = 1, x = 0, y = 0;
		RecallAttributeRect Apply(RecallAttributeRect r) const noexcept
		{ return {r.left * scale + x, r.top * scale + y, r.right * scale + x, r.bottom * scale + y}; }
		friend bool operator==(const RecallSurfacePose&, const RecallSurfacePose&) = default;
	};
	inline RecallSurfacePose ComposeRecallPose(RecallSurfacePose parent, RecallSurfacePose child) noexcept
	{ return {parent.scale * child.scale, parent.x + child.x * parent.scale, parent.y + child.y * parent.scale}; }
	inline RecallSurfacePose ResolveRecallSurfacePose(RecallAttributeRect panel, RecallAttributeRect anchor,
		double geometry, double compactWidth) noexcept
	{
		const auto pose=ResolveEraserSurfacePose({panel.left,panel.top,panel.right,panel.bottom},
			{anchor.left,anchor.top,anchor.right,anchor.bottom},geometry,compactWidth);
		return {pose.scale,pose.x,pose.y};
	}
	inline RecallSurfacePose FitRecallSurfacePose(RecallSurfacePose pose, RecallAttributeRect shell, RecallAttributeRect work) noexcept
	{
		const auto fitted=FitEraserSurfacePose({pose.scale,pose.x,pose.y},
			{shell.left,shell.top,shell.right,shell.bottom},{work.left,work.top,work.right,work.bottom});
		return {fitted.scale,fitted.x,fitted.y};
	}
	using RecallSurfaceMotion = BarAttributePanelMotion;
	struct RecallAttributePresentation
	{
		RecallAttributeLayout geometry;
		RecallSurfacePose panelPose;
		double zoom = 1, panelOpacity = 0;
		bool panelVisible = false;
	};
	inline RecallAttributeLayout TransformRecallAttributeLayout(RecallAttributeLayout layout, RecallSurfacePose panel) noexcept
	{
		layout.panel = panel.Apply(layout.panel);
		for (size_t i = 0; i < layout.items.size(); ++i) layout.items[i] = panel.Apply(layout.items[i]);
		return layout;
	}
}
