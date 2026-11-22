#pragma once

#include "Setting.Motion.h"
#include "imgui/imgui_impl_dx11.h"
// 全局模块片段已引入 SDK 类型时，避免在命名模块内重复包含。
#ifndef _D2DBASETYPES_INCLUDED
#include <d2dbasetypes.h>
#endif
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

import Inkeys.Other.Config;
import Inkeys.UI.Bar.LayoutModel;
import Inkeys.UI.UI3.DragSession;

extern "C++" {
struct ImVec2;
struct ImGuiWindow;

namespace Inkeys::UI::Setting::ComponentEditor
{
	void Draw(ImGuiWindow* scrollWindow, void (*queueWrite)(), float pageOpacity, bool inputBlocked);
	void BeginLibraryDrag(const char* componentId, ImVec2 screenPoint, bool touch);
	bool DragActive();

	// 设置宿主的预览按压与拖动交接，使用同一个 DragSession 持有指针。
	template<class Instance, class Hit>
	struct PreviewPressInput
	{
		UI3::DragSession drag;
		Hit pressed;
		double moreSince = -1;
		bool moreHoverConsumed = false;

		bool CloseHeld() const
		{
			return drag.Phase() == UI3::DragPhase::Pressed && pressed.kind == decltype(pressed.kind)::Close;
		}

		bool Begin(Instance& instance, const Hit& hit, std::uint32_t pointer, UI3::DragPointerKind kind,
			D2D1_POINT_2F point, std::uint64_t revision, float pixelsPerDip)
		{
			pressed = hit;
			drag.Press(pointer, kind, point, {revision}, pixelsPerDip);
			return instance.Press(hit);
		}

		bool Update(Instance& instance, std::uint32_t pointer, D2D1_POINT_2F point, const Hit& hit,
			bool over, bool down, bool& dirty)
		{
			// X 移出只松开视觉，保留捕获供移回恢复；其他按钮越过阈值才尝试抓起。
			if (CloseHeld())
			{
				dirty |= instance.SetClosePressed(over && hit.kind == decltype(hit.kind)::Close && down);
				return true;
			}
			return !drag.Move(pointer, point)
				|| (pressed.position && instance.Lift(*pressed.position, drag.Payload().token));
		}

		bool Release(Instance& instance, std::uint32_t pointer, D2D1_POINT_2F point, const Hit& hit,
			const Hit& presentedHit, bool over, std::uint64_t revision, bool& dirty)
		{
			if (drag.Phase() != UI3::DragPhase::Pressed || pointer != drag.PointerId()) return false;
			const auto pressRevision = drag.Payload().token;
			if (CloseHeld())
			{
				// X 的抬起使用接收输入时的已呈现命中，避免本轮绘制推进改变判定。
				if (over && presentedHit.kind == decltype(presentedHit.kind)::Close && pressRevision == revision)
					dirty |= instance.Activate(pressed);
			}
			else
			{
				if (drag.Release(pointer, point) != UI3::DragSession::ReleaseResult::Click) return false;
				// 普通点击只激活按下时同版本的同一项，拖放由宿主单独提交。
				const bool samePosition = hit.position.has_value() == pressed.position.has_value()
					&& (!hit.position || (hit.position->segment == pressed.position->segment
						&& hit.position->index == pressed.position->index));
				if (over && hit.kind == pressed.kind && samePosition && pressRevision == revision)
					dirty |= instance.Activate(hit);
			}
			Cancel(instance);
			return true;
		}

		// 自动展开只看已呈现的 More 命中，分割线和固定项也能触发。
		bool UpdateMoreHover(Instance& instance, const Hit& hit, bool clipped, double now)
		{
			if (!clipped || hit.kind != decltype(hit.kind)::More)
			{
				moreSince = -1;
				moreHoverConsumed = false;
			}
			else if (moreSince < 0)
				moreSince = now;
			else if (!moreHoverConsumed && now - moreSince >= 0.5)
			{
				// 即使 More 没有可见项，本次停留也已消费，只有实际展开才需要重绘。
				moreHoverConsumed = true;
				if (instance.MoreExpanded()) return false;
				instance.SetMoreExpanded(true);
				return instance.MoreExpanded();
			}
			return false;
		}

		void Cancel(Instance& instance)
		{
			drag.Cancel();
			moreSince = -1;
			moreHoverConsumed = false;
			instance.ReleasePress();
			instance.CancelLift();
		}
	};

	enum class DropAction { Invalid, Insert, Store, Delete, CancelAdd, NotAllowed };
	struct DropTarget { DropAction action = DropAction::Invalid; std::optional<Bar::Position> gap; };
	inline DropTarget ResolveDrop(const Bar::BarLayoutModel& model, const Bar::ItemSource& source,
		const Bar::BarLayoutCatalog& catalog, bool trash, bool outsidePreview, bool more,
		const std::vector<Bar::Position>& gaps)
	{
		const auto* from = std::get_if<Bar::Position>(&source);
		if (!trash && !outsidePreview)
		{
			bool divider = false;
			if (more)
			{
				const Bar::Position end{Bar::Segment::More, model[Bar::Segment::More].size()};
				if (Bar::CanPlace(model, source, end, catalog) == Bar::PlaceDecision::Allowed)
					return {DropAction::Store, end};
				// 空 B1 的插入缝锚在 More 边缘，只有用户分割线可在该重叠处插入 B1。
				divider = from
					? static_cast<std::size_t>(from->segment) < model.segments.size()
						&& from->index < model[from->segment].size()
						&& model[from->segment][from->index].Id == BarButtonId::UserDivider
					: std::get<std::string_view>(source) == BarButtonId::UserDivider;
			}
			for (auto gap : gaps)
				if ((!more || (divider && gap.segment == Bar::Segment::B1))
					&& Bar::CanPlace(model, source, gap, catalog) == Bar::PlaceDecision::Allowed)
					return {DropAction::Insert, gap};
		}

		if (!from) return {DropAction::CancelAdd, {}};
		if (!Bar::Remove(model, *from, catalog)) return {DropAction::NotAllowed, {}};
		return {trash || outsidePreview ? DropAction::Delete : DropAction::Invalid, {}};
	}

	// 设置宿主在拖动帧中用同一个已呈现命中决定落点和 More 停留。
	template<class Instance, class Hit>
	DropTarget ResolveDragFrameDrop(PreviewPressInput<Instance, Hit>& input, Instance& instance,
		const Hit& hit, bool clipped, double now, bool& dirty, const Bar::BarLayoutModel& model,
		const Bar::ItemSource& source, const Bar::BarLayoutCatalog& catalog, bool trash, bool outsidePreview,
		const std::vector<Bar::Position>& gaps)
	{
		const bool more = clipped && hit.kind == decltype(hit.kind)::More;
		const auto drop = ResolveDrop(model, source, catalog, trash, outsidePreview, more, gaps);
		dirty |= input.UpdateMoreHover(instance, hit, clipped, now);
		return drop;
	}

	inline void FinishPreviewBlock(Design::PageBlockScope& block, ImDrawList& list, int first, int end, float pageOpacity)
	{
		block.Finish();
		// DX11 通常用离屏组统一乘 RGBA；逐顶点退路只改 alpha，需要补齐图片的 RGB。
		// 旧页退路会在正文末尾乘 alpha，这里先为那一步补齐 RGB，避免重复乘 alpha。
		const bool pageVertexFade = pageOpacity <= 0 || list._Splitter._Count > 1
			|| !ImGui_ImplDX11_SupportsOpacityGroups() || !(ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasVtxOffset);
		for (int i = first; i < end; ++i)
		{
			auto& color = list.VtxBuffer[i].col;
			const auto alpha = color >> IM_COL32_A_SHIFT;
			const auto rgb = pageVertexFade ? ImU32(std::round(alpha * pageOpacity)) : alpha;
			color = IM_COL32(rgb, rgb, rgb, alpha);
		}
	}

	inline D2D1_RECT_F TrashBounds(D2D1_RECT_F preview, float bodyBottom, float width, float height)
	{
		// 窄窗口缩小预览时，下方预留可能不足 36 DIP，胶囊仍应留在块内。
		bodyBottom = (std::min)((std::max)(bodyBottom, preview.top), preview.bottom);
		width = (std::min)(width, preview.right - preview.left);
		height = (std::min)(height, preview.bottom - bodyBottom);
		const float left = (preview.left + preview.right - width) / 2;
		const float top = (bodyBottom + preview.bottom - height) / 2;
		return {left, top, left + width, top + height};
	}
}
}
