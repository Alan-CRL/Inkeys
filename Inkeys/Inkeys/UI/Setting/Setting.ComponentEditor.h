#pragma once

import Inkeys.UI.Bar.LayoutModel;

extern "C++" {
struct ImVec2;
struct ImGuiWindow;

namespace Inkeys::UI::Setting::ComponentEditor
{
	void Draw(ImGuiWindow* scrollWindow, void (*queueWrite)());
	void BeginLibraryDrag(const char* componentId, ImVec2 screenPoint, bool touch);
	bool DragActive();
	enum class DropAction { Invalid, Insert, Store, Delete, CancelAdd };
	struct DropTarget { DropAction action = DropAction::Invalid; std::optional<Bar::Position> gap; };
	inline DropTarget ResolveDrop(const Bar::BarLayoutModel& model, const Bar::ItemSource& source,
		const Bar::BarLayoutCatalog& catalog, bool trash, bool more, const std::vector<Bar::Position>& gaps)
	{
		if (trash)
		{
			auto pos = std::get_if<Bar::Position>(&source);
			if (!pos) return {DropAction::CancelAdd,{}};
			return {Bar::Remove(model,*pos,catalog) ? DropAction::Delete : DropAction::Invalid,{}};
		}
		if (more)
		{
			Bar::Position end{Bar::Segment::More,model[Bar::Segment::More].size()};
			if (Bar::CanPlace(model,source,end,catalog)==Bar::PlaceDecision::Allowed) return {DropAction::Store,end};
			return {};
		}
		// 实例已按距缝隙的距离排序，过滤后第一个合法位置就是插入目标。
		for (auto gap : gaps)
			if (Bar::CanPlace(model,source,gap,catalog)==Bar::PlaceDecision::Allowed) return {DropAction::Insert,gap};
		return {};
	}
}
}
