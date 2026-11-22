#include <d2d1.h>
#include <iostream>
#include <optional>
#include <variant>
#include <vector>

import Inkeys.UI.UI3.DragSession;
import Inkeys.Other.Config;
#include "../Inkeys/Inkeys/UI/Setting/Setting.ComponentEditor.h"

int RunDragSessionTests()
{
	using namespace Inkeys::UI::UI3;
	using Result = DragSession::ReleaseResult;
	int failures = 0;
	auto check = [&](bool ok, const char *name) {
		if (!ok)
		{
			std::cerr << "FAIL drag session " << name << '\n';
			++failures;
		}
	};
	for (auto kind : {DragPointerKind::Mouse, DragPointerKind::Touch, DragPointerKind::Pen})
	{
		DragSession drag;
		const float threshold = kind == DragPointerKind::Mouse ? 8.f : 20.f;
		drag.Press(1, kind, {0, 0}, {7}, 2);
		drag.Press(2, kind, {5, 6}, {9}, 2);
		check(!drag.Move(2, {100, 0}) && drag.Point().x == 0 && drag.Payload().token == 7 &&
				  drag.Release(2, {100, 0}) == Result::None,
			  "other contact");
		check(!drag.Move(1, {threshold - 1, 0}) && drag.Release(1, {threshold - 1, 0}) == Result::Click, "click");
		drag.Press(1, kind, {0, 0}, {7}, 2);
		check(drag.Move(1, {threshold, 0}) && !drag.Move(1, {threshold + 1, 0}) &&
				  drag.Release(1, {threshold, 0}) == Result::Drop,
			  "threshold");
		drag.BeginDragging(2, kind, {3, 4}, {9});
		check(drag.Phase() == DragPhase::Dragging && drag.Origin().y == 4 && drag.PointerId() == 2, "external");
		drag.Cancel();
		check(drag.Release(2, {3, 4}) == Result::None && drag.Phase() == DragPhase::Idle, "cancel");
	}
	using namespace Inkeys;
	using namespace Inkeys::UI::Bar;
	using namespace Inkeys::UI::Setting::ComponentEditor;
	auto model = Reset();
	BarLayoutCatalog catalog;
	const std::string component = BarComponentId::Explorer;
	catalog[component] = {Segment::B1, BarButtonSizeKind::TwoTwo, true, true};
	catalog[BarButtonId::UserDivider] = {Segment::A1, BarButtonSizeKind::OneTwo, true, true};
	check(ResolveDrop(model, Position{Segment::A1, 0}, catalog, false, false, false, {{Segment::B1, 0}}).action ==
			  DropAction::NotAllowed,
		  "fixed zone");
	check(ResolveDrop(model, std::string_view(component), catalog, false, false, false, {{Segment::A1, 0}}).action ==
			  DropAction::CancelAdd,
		  "component zone");
	auto allowed = ResolveDrop(model, std::string_view(BarButtonId::UserDivider), catalog, false, false, false,
							   {{Segment::More, 0}, {Segment::A1, 0}});
	auto divided = allowed.gap ? Insert(model, BarButtonId::UserDivider, *allowed.gap, catalog) : std::nullopt;
	check(divided && (*divided)[Segment::A1].front().Id == BarButtonId::UserDivider, "first allowed gap");
	// 空 B1 的插入缝和 More 的命中有重叠，More 拒绝后仍应考虑 B1 的缝。
	const auto dividerAtMoreEdge = ResolveDrop(model, std::string_view(BarButtonId::UserDivider),
		catalog, false, false, true, {{Segment::B1, 0}});
	check(dividerAtMoreEdge.action == DropAction::Insert && dividerAtMoreEdge.gap
		&& dividerAtMoreEdge.gap->segment == Segment::B1 && dividerAtMoreEdge.gap->index == 0,
		"divider at More edge");
	const auto fixedAtMoreEdge = ResolveDrop(model, Position{Segment::A2, 0},
		catalog, false, false, true, {{Segment::A2, 1}});
	check(fixedAtMoreEdge.action == DropAction::NotAllowed && !fixedAtMoreEdge.gap, "fixed A2 at More edge");
	const auto dividerAtA2Edge = ResolveDrop(model, std::string_view(BarButtonId::UserDivider),
		catalog, false, false, true, {{Segment::A2, 0}});
	check(dividerAtA2Edge.action == DropAction::CancelAdd && !dividerAtA2Edge.gap, "divider needs B1 at More edge");
	check(divided && ResolveDrop(*divided, Position{Segment::A1, 0}, catalog, false, false, true, {{Segment::B1, 0}}).action
		== DropAction::Insert, "existing divider at More edge");
	auto store = ResolveDrop(model, std::string_view(component), catalog, false, false, true, {});
	auto stored = store.gap ? Insert(model, component, *store.gap, catalog) : std::nullopt;
	check(store.action == DropAction::Store && stored && (*stored)[Segment::More].back().Id == component,
		  "append to more");
	const auto before = model;
	auto cancel = ResolveDrop(model, std::string_view(component), catalog, true, false, false, {});
	check(cancel.action == DropAction::CancelAdd && !cancel.gap && before == model, "discard external");
	check(ResolveDrop(model, Position{Segment::More, 0}, catalog, true, false, false, {}).action == DropAction::NotAllowed,
		  "retain settings");
	const auto loaded = stored ? Insert(*stored, component, {Segment::B1, 0}, catalog) : std::nullopt;
	if (loaded)
	{
		const Position item{Segment::B1, 0}, fixed{Segment::A1, 0}, fixedA2{Segment::A2, 0};
		const Position setting{Segment::More, 0}, storedItem{Segment::More, 1};
		auto resolve = [&](const ItemSource& source, bool trash, bool outside, bool more,
			const std::vector<Position>& gaps) { return ResolveDrop(*loaded, source, catalog, trash, outside, more, gaps); };
		check(resolve(storedItem, false, false, false, {{Segment::B1, 1}}).action == DropAction::Insert,
			"removable main gap");
		check(resolve(item, false, false, true, {{Segment::B1, 1}}).action == DropAction::Store,
			"removable More priority");
		const auto trashDrop = resolve(item, true, false, true, {{Segment::B1, 1}});
		check(trashDrop.action == DropAction::Delete && !trashDrop.gap, "removable trash priority");
		const auto outside = resolve(storedItem, false, true, false, {});
		check(outside.action == DropAction::Delete && !outside.gap, "removable outside preview");
		check(resolve(item, false, false, false, {{Segment::A2, 0}}).action == DropAction::Invalid,
			"removable invalid inside preview");
		check(resolve(fixed, false, false, false, {{Segment::A1, 1}}).action == DropAction::Insert,
			"fixed original region");
		check(resolve(fixedA2, true, false, false, {{Segment::A2, 1}}).action == DropAction::NotAllowed,
			"fixed A2 over trash");
		check(resolve(fixed, false, true, false, {}).action == DropAction::NotAllowed, "fixed outside preview");
		check(resolve(setting, false, false, false, {{Segment::A1, 0}}).action == DropAction::NotAllowed,
			"settings invalid region");
		check(resolve(setting, false, false, false, {{Segment::B1, 0}}).action == DropAction::Insert,
			"settings from More to B1");
		check(resolve(std::string_view(component), false, false, false, {{Segment::B1, 1}}).action == DropAction::Insert,
			"library main gap");
		check(resolve(std::string_view(component), false, true, true, {{Segment::B1, 1}}).action == DropAction::CancelAdd,
			"library outside preview");
	}
	else check(false, "drop fixture has a component in each region");

	const auto capsule = TrashBounds({0, 0, 400, 240}, 154, 170, 36);
	check(capsule.left == 115 && capsule.right == 285 && capsule.top == 179 && capsule.bottom == 215,
		"trash centered in lower reserve");
	const auto narrow = TrashBounds({0, 0, 90, 150}, 130, 170, 36);
	check(narrow.left == 0 && narrow.right == 90 && narrow.top == 130 && narrow.bottom == 150,
		"trash fits narrow preview");
	return failures;
}
