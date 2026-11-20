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
	check(ResolveDrop(model, Position{Segment::A1, 0}, catalog, false, false, {{Segment::B1, 0}}).action ==
			  DropAction::Invalid,
		  "fixed zone");
	check(ResolveDrop(model, std::string_view(component), catalog, false, false, {{Segment::A1, 0}}).action ==
			  DropAction::Invalid,
		  "component zone");
	auto allowed = ResolveDrop(model, std::string_view(BarButtonId::UserDivider), catalog, false, false,
							   {{Segment::More, 0}, {Segment::A1, 0}});
	auto divided = allowed.gap ? Insert(model, BarButtonId::UserDivider, *allowed.gap, catalog) : std::nullopt;
	check(divided && (*divided)[Segment::A1].front().Id == BarButtonId::UserDivider, "first allowed gap");
	auto store = ResolveDrop(model, std::string_view(component), catalog, false, true, {});
	auto stored = store.gap ? Insert(model, component, *store.gap, catalog) : std::nullopt;
	check(store.action == DropAction::Store && stored && (*stored)[Segment::More].back().Id == component,
		  "append to more");
	const auto before = model;
	auto cancel = ResolveDrop(model, std::string_view(component), catalog, true, false, {});
	check(cancel.action == DropAction::CancelAdd && !cancel.gap && before == model, "discard external");
	check(ResolveDrop(model, Position{Segment::More, 0}, catalog, true, false, {}).action == DropAction::Invalid,
		  "retain settings");
	return failures;
}
