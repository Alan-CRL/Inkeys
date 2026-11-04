#include "../Inkeys/Inkeys/UI/Bar/Bar.StatusIndicator.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>

namespace
{
	using namespace Inkeys::UI::Bar;
	constexpr double kFrame = 1.0 / 60.0;
	constexpr std::uint32_t kRed = 0xFFC43E1Cu, kBlue = 0xFF1F4FB8u, kGreen = 0xFF2EA043u;

	StatusIndicatorSpec Spec(std::uint64_t key, StatusIndicatorKind kind,
		StatusIndicatorLifetime lifetime = StatusIndicatorLifetime::Persistent,
		StatusIndicatorAction action = StatusIndicatorAction::None,
		std::uint32_t argb = kGreen, std::wstring text = L"state")
	{
		StatusIndicatorSpec spec;
		spec.key = key; spec.kind = kind; spec.lifetime = lifetime; spec.action = action;
		spec.accentArgb = argb; spec.icon = StatusIndicatorIcon::Info; spec.text = std::move(text);
		return spec;
	}

	void Run(StatusIndicatorModel& model, double& now, double until, bool animations = true, double frame = kFrame)
	{
		while (now < until - 1e-9)
		{
			const double dt = (std::min)(frame, until - now);
			now += dt;
			model.Advance(now, dt, 1.0, animations);
		}
	}

	StatusIndicatorLayoutInput FoldedInput()
	{
		StatusIndicatorLayoutInput input;
		input.mainCenterX = 500; input.mainCenterY = 500; input.mainWidth = 80; input.mainHeight = 80; input.mainN = 3;
		input.baseBounds = { 451, 451, 549, 549 };
		input.work = { 0, 0, 1920, 1040 };
		return input;
	}

	const StatusIndicatorEntryLayout* FindLayout(const StatusIndicatorLayout& layout, std::uint64_t key)
	{
		for (const auto& entry : layout.entries)
			if (entry.key == key) return &entry;
		return nullptr;
	}

	bool Near(double a, double b, double epsilon = 1e-6) { return std::abs(a - b) <= epsilon; }
	double CenterX(const StatusIndicatorEntryLayout& l) { return (l.bounds.left + l.bounds.right) / 2; }
	double CenterY(const StatusIndicatorEntryLayout& l) { return (l.bounds.top + l.bounds.bottom) / 2; }
}

int RunStatusIndicatorTests()
{
	int failures = 0;
	auto check = [&](bool value, const char* label)
	{
		if (!value) { ++failures; std::cerr << "[StatusIndicator] " << label << '\n'; }
	};
	using Phase = StatusIndicatorPhase;
	using Kind = StatusIndicatorKind;
	using Life = StatusIndicatorLifetime;
	using Action = StatusIndicatorAction;
	using Result = StatusIndicatorPublishResult;
	using namespace StatusIndicatorStyle;
	const double inf = std::numeric_limits<double>::infinity();

	// Dedupe: same key updates fields without replaying the appear animation.
	{
		StatusIndicatorModel model; double now = 0;
		check(model.Publish(Spec(1, Kind::PptSlideshow), now) == Result::Added, "first publish adds");
		Run(model, now, 0.2);
		const double appear = model.Find(1)->appear;
		check(appear > 0 && appear < 1, "appear animation in progress");
		check(model.Publish(Spec(1, Kind::PptSlideshow, Life::Persistent, Action::None, kRed, L"updated"), now) == Result::Updated,
			"same key publish is update");
		check(model.Find(1)->appear == appear && model.Find(1)->phase == Phase::Expanded
			&& model.Find(1)->spec.text == L"updated" && model.Find(1)->spec.accentArgb == kRed,
			"update keeps animation state and replaces fields");
		check(Near(model.Find(1)->holdEnd, 3.0), "update does not restart hold");
		Run(model, now, 4.5);
		check(model.Find(1)->phase == Phase::Docked, "expired persistent docks");
		check(model.Publish(Spec(1, Kind::PptSlideshow), now) == Result::Updated && model.Find(1)->phase == Phase::Docked
			&& model.Front() == nullptr, "docked republish does not replay capsule");
		check(model.Retire(1, now) && model.Find(1)->phase == Phase::Exiting, "retire visible docked exits");
		Run(model, now, now + 0.1);
		check(model.Publish(Spec(1, Kind::PptSlideshow), now) == Result::Updated && model.Find(1)->phase == Phase::Docked,
			"republish revives exiting entry as docked");
		Run(model, now, now + 1.0);
		check(model.Find(1) && model.Find(1)->vanish == 0, "revived entry stays registered");
		check(model.Retire(1, now), "retire again");
		Run(model, now, now + 1.0);
		check(model.Find(1) == nullptr && model.Count() == 0, "exit animation erases entry");
	}

	// Filtering: hidden entries stay registered, re-enabled persistent ones return docked.
	{
		StatusIndicatorModel model; double now = 0;
		model.Publish(Spec(1, Kind::PptSlideshow), now);
		model.Publish(Spec(2, Kind::SuperTop), now);
		model.Publish(Spec(3, Kind::Debug, Life::Notification), now);
		Run(model, now, 0.5);
		model.SetOptions({ true, false, true, true }, now);
		check(model.FrontKey() == 2 && Near(model.Find(2)->holdStart, 0.5), "hiding front hands hold to next visible");
		Run(model, now, 1.5);
		check(model.Find(1) && model.Find(1)->presence == 0 && model.Find(1)->phase == Phase::Docked,
			"hidden persistent fades out but stays registered");
		check(model.Find(3) && model.Find(3)->visible, "debug kind ignores type filters");
		check(model.VisibleCount() == 2, "visible count excludes hidden entry");
		model.SetOptions({ false, true, true, true }, now);
		Run(model, now, 2.5);
		check(model.Find(3) == nullptr && model.Count() == 2, "hidden notification is dropped");
		check(model.Publish(Spec(4, Kind::Debug, Life::Notification), now) == Result::Rejected,
			"notification published while hidden is rejected");
		check(model.Publish(Spec(5, Kind::Debug), now) == Result::Added && model.Find(5)->phase == Phase::Docked
			&& !model.Find(5)->visible, "persistent published while hidden registers docked");
		model.SetOptions({ true, true, true, true }, now);
		check(model.Front() == nullptr, "re-enable does not replay capsules");
		for (const std::uint64_t key : { 1ull, 2ull, 5ull })
			check(model.Find(key) && model.Find(key)->phase == Phase::Docked && model.Find(key)->appear == 1
				&& model.Find(key)->morph.position == 1 && model.Find(key)->flight.position == 1,
				"re-enabled persistent reappears docked");
		Run(model, now, 3.5);
		check(model.Find(1)->presence == 1 && model.Find(2)->presence == 1 && model.Front() == nullptr,
			"re-enabled entries fade in as docked tiles");
		model.SetOptions({ true, true, false, true }, now);
		check(model.Retire(2, now) && model.Find(2) == nullptr, "retiring hidden entry erases immediately");
	}

	// Capacity 25 and eviction order.
	{
		StatusIndicatorModel model; double now = 0;
		for (std::uint64_t key = 1; key <= 25; ++key)
			check(model.Publish(Spec(key, Kind::Debug), now) == Result::Added, "debug fill adds");
		check(model.Count() == 25 && model.DebugCount() == 25, "capacity filled");
		check(model.Publish(Spec(26, Kind::Debug), now) == Result::Added && model.Find(1) == nullptr
			&& model.Count() == 25, "full model evicts oldest debug");

		StatusIndicatorModel mixed;
		for (std::uint64_t key = 1; key <= 24; ++key) mixed.Publish(Spec(key, Kind::Debug), now);
		mixed.Publish(Spec(100, Kind::Debug, Life::Notification), now);
		check(mixed.Publish(Spec(200, Kind::NewVersion), now) == Result::Added && mixed.Find(100) == nullptr
			&& mixed.Find(1) != nullptr, "oldest notification is evicted before debug entries");

		StatusIndicatorModel real;
		for (std::uint64_t key = 1; key <= 25; ++key) real.Publish(Spec(key, Kind::SuperTop), now);
		check(real.Publish(Spec(26, Kind::Debug), now) == Result::Rejected && real.Count() == 25,
			"no evictable entry rejects");
		check(real.NewestKey(Kind::SuperTop, Life::Persistent).value_or(0) == 25, "newest key lookup");
	}

	// Front-only hold timing, slide toward the edge, equal width behind and ring ownership.
	{
		StatusIndicatorModel model; double now = 0;
		model.Publish(Spec(1, Kind::PptSlideshow, Life::Persistent, Action::None, kRed), now);
		model.SetTextWidth(1, 40);
		Run(model, now, 1.0);
		model.Publish(Spec(2, Kind::SuperTop, Life::Persistent, Action::None, kBlue), now);
		model.SetTextWidth(2, 100);
		Run(model, now, 2.95);
		check(model.Find(1)->holding && Near(model.Find(1)->holdEnd, 3.0), "front holds from publish");
		check(!model.Find(2)->holding && model.Find(2)->phase == Phase::Expanded, "queued entry does not count down");
		check(model.Ring().owner == 1 && model.RingColor() == kRed, "ring owned by front");
		check(Near(model.WakeDeadline(), 3.0), "wake deadline is front hold end");
		const auto input = FoldedInput();
		{
			const auto layout = ResolveStatusIndicatorLayout(model, input);
			const auto* front = FindLayout(layout, 1); const auto* behind = FindLayout(layout, 2);
			check(front && behind && Near(behind->bounds.Width(), front->bounds.Width(), 0.05)
				&& Near(CenterY(*behind), CenterY(*front) - StackStep, 1e-3) && behind->z < front->z,
				"queued capsule matches front width and peeks one step outward behind it");
		}
		Run(model, now, 3.0 + kFrame);
		check(model.Find(1)->phase == Phase::Collapsing, "front collapses on expiry");
		check(model.Find(2)->holding && model.Find(2)->holdStart >= 3.0 && Near(model.Find(2)->holdEnd, model.Find(2)->holdStart + 3.0),
			"next entry starts hold when it becomes front");
		check(model.Ring().owner == 2 && model.Ring().colorT < 1, "ring crossfades to new front color");
		const double top0 = FindLayout(ResolveStatusIndicatorLayout(model, input), 2)->bounds.top;
		const double slot0 = model.Find(2)->stack.position;
		Run(model, now, now + 0.08);
		const double slot1 = model.Find(2)->stack.position;
		const double top1 = FindLayout(ResolveStatusIndicatorLayout(model, input), 2)->bounds.top;
		check(slot1 < slot0 && slot1 > 0 && top1 > top0, "behind entry slides onto the edge with spring");
		Run(model, now, 4.5);
		check(model.Find(2)->stack.position == 0 && model.Find(1)->phase == Phase::Docked, "slide settles at front slot");
		check(Near(model.Find(2)->width.position, StatusIndicatorCapsuleWidth(100), 0.1), "new front widens to its own text");
		check(model.RingColor() == kBlue, "ring color settles to new owner");
		const double holdEnd = model.Find(2)->holdEnd;
		Run(model, now, holdEnd - 0.05);
		check(model.Find(2)->phase == Phase::Expanded, "second hold keeps full 3s");
		Run(model, now, holdEnd + 0.05);
		check(model.Find(2)->phase == Phase::Collapsing && model.Ring().owner == 0, "ring released when queue empties");
	}

	// Capsule geometry: vertical center on the main button top border, icon left, centered text.
	{
		StatusIndicatorModel model; double now = 0;
		model.Publish(Spec(1, Kind::Debug), now);
		model.SetTextWidth(1, 20);
		Run(model, now, 1.0);
		const auto input = FoldedInput();
		const auto layout = ResolveStatusIndicatorLayout(model, input);
		const auto* capsule = FindLayout(layout, 1);
		check(capsule && capsule->shape == StatusIndicatorShape::Capsule && capsule->role == StatusIndicatorRole::Capsule,
			"front is a clickable capsule");
		if (capsule)
		{
			check(Near(CenterX(*capsule), 500) && Near(CenterY(*capsule), 460) && Near(capsule->bounds.Height(), CapsuleHeight)
				&& Near(capsule->radius, CapsuleRadius), "capsule centered on main button top border");
			check(Near(capsule->bounds.Width(), 2 * CapsulePadding + CapsuleIcon + CapsuleGap + 20), "capsule width follows text");
			check(Near(capsule->iconCenterX, capsule->bounds.left + CapsuleHeight / 2) && Near(capsule->iconSize, CapsuleIcon),
				"icon sits at the left square");
			check(Near(capsule->textX - capsule->textClip.left, (capsule->textClip.Width() - 20) / 2, 1e-6)
				&& !capsule->fadeLeft && !capsule->fadeRight, "short text centered without fades");
			check(capsule->overflow && !StatusIndicatorRectContains(input.baseBounds, capsule->bounds),
				"capsule may grow the window");
			check(StatusIndicatorRectContains(layout.reserveBounds, capsule->bounds), "capsule inside reserved envelope");
		}
		auto flipped = input; flipped.below = 1;
		const auto downLayout = ResolveStatusIndicatorLayout(model, flipped);
		const auto* down = FindLayout(downLayout, 1);
		check(down && Near(CenterY(*down), 540), "inverted capsule centered on bottom border");
	}

	// Collapse motion: morph into icon, inward flight under the queue, docked tiles yield only on landing; frame-rate independent.
	{
		const auto input = FoldedInput();
		StatusIndicatorModel model; double now = 0;
		model.Publish(Spec(1, Kind::Debug), now);
		Run(model, now, 4.5);
		check(model.Find(1)->phase == Phase::Docked, "first entry docked");
		const double oldX = CenterX(*FindLayout(ResolveStatusIndicatorLayout(model, input), 1));
		model.Publish(Spec(2, Kind::Debug), now);
		model.SetTextWidth(2, 80);
		model.Publish(Spec(3, Kind::Debug), now); // 排在其后，收起时成为队首
		const double expire = now + Hold + kFrame;
		Run(model, now, expire);
		check(model.Find(2)->phase == Phase::Collapsing && !model.Find(2)->landed, "second starts collapsing");
		Run(model, now, now + FlightDelay * 0.5);
		check(model.Find(2)->flight.position == 0 && model.Find(2)->morph.position > 0, "morph starts before flight");
		bool dipped = false, yieldedEarly = false, landedSeen = false, crossed = false, endedBelow = false;
		int previousOrder = 0;
		while (model.Find(2)->phase == Phase::Collapsing && now < expire + 3)
		{
			Run(model, now, now + kFrame);
			const auto layout = ResolveStatusIndicatorLayout(model, input);
			const auto* fly = FindLayout(layout, 2); const auto* docked = FindLayout(layout, 1); const auto* front = FindLayout(layout, 3);
			if (fly && fly->bounds.top >= 460 + CapsuleHeight / 2) dipped = true;
			if (!model.Find(2)->landed && docked && !Near(CenterX(*docked), oldX, 1e-6)) yieldedEarly = true;
			if (model.Find(2)->landed) landedSeen = true;
			// 与新队首的层级关系只能在两者不相交时改变。
			if (fly && front)
			{
				const int order = fly->z > front->z ? 1 : -1;
				if (previousOrder != 0 && order != previousOrder && fly->bounds.Intersects(front->bounds)) crossed = true;
				previousOrder = order; endedBelow = order < 0;
			}
		}
		check(dipped, "flight first dips inward out of the capsule area");
		check(!crossed && endedBelow, "flying tile switches below the queue only after clearing it");
		check(!yieldedEarly, "docked tile waits for the landing before yielding");
		check(landedSeen && model.Find(2)->phase == Phase::Docked, "flying tile lands and docks");
		Run(model, now, now + 1.0);
		const auto layout = ResolveStatusIndicatorLayout(model, input);
		check(Near(CenterX(*FindLayout(layout, 1)), oldX - DockStep, 1e-3), "older tile shifted left after landing");

		auto sample = [&](double frame)
		{
			StatusIndicatorModel m; double t = 0;
			m.Publish(Spec(1, Kind::Debug), t); m.SetTextWidth(1, 60);
			Run(m, t, Hold + 0.25, true, frame);
			return ResolveStatusIndicatorLayout(m, input);
		};
		const auto a = sample(1.0 / 30.0), b = sample(1.0 / 120.0);
		const auto* la = FindLayout(a, 1); const auto* lb = FindLayout(b, 1);
		check(la && lb && Near(la->bounds.left, lb->bounds.left, 0.05) && Near(la->bounds.top, lb->bounds.top, 0.05)
			&& Near(la->bounds.Width(), lb->bounds.Width(), 0.05), "collapse geometry is frame-rate independent");
	}

	// Docked order: newest at the right end on the border; removal shifts left ones right.
	{
		StatusIndicatorModel model; double now = 0;
		model.Publish(Spec(1, Kind::PptSlideshow), now);
		model.Publish(Spec(2, Kind::SuperTop), now);
		model.Publish(Spec(3, Kind::NewVersion), now);
		Run(model, now, 12.0);
		check(model.DockedCount() == 3, "all three docked");
		const auto input = FoldedInput();
		auto layout = ResolveStatusIndicatorLayout(model, input);
		const auto* a = FindLayout(layout, 1); const auto* b = FindLayout(layout, 2); const auto* c = FindLayout(layout, 3);
		check(a && b && c, "docked layouts exist");
		if (a && b && c)
		{
			const double edge = 500 - StatusIndicatorSuperHalfHeight(26, 40, 40, 3);
			check(CenterX(*c) > CenterX(*b) && CenterX(*b) > CenterX(*a), "newer docked tile is further right");
			check(c->z > b->z && b->z > a->z, "newer docked tile is above");
			check(Near(CenterX(*c), 526) && Near(CenterY(*c), edge, 1e-6) && Near(c->bounds.Width(), DockSize)
				&& Near(c->radius, DockRadius) && c->shape == StatusIndicatorShape::Tile, "newest docked on the border at the right end");
			check(Near(CenterX(*c) - CenterX(*b), DockStep), "docked step");
			check(Near(c->opacity, DockOpacity) && c->role == StatusIndicatorRole::Dock, "docked opacity half");
		}
		auto flipped = input; flipped.below = 1;
		const auto downLayout = ResolveStatusIndicatorLayout(model, flipped);
		const auto* down = FindLayout(downLayout, 3);
		check(down && CenterY(*down) > 500, "inverted docked tiles sit on the bottom border");
		const double ax = a ? CenterX(*a) : 0;
		model.Retire(2, now);
		Run(model, now, now + 0.05);
		const double axMid = CenterX(*FindLayout(ResolveStatusIndicatorLayout(model, input), 1));
		check(axMid > ax, "left tile starts shifting right");
		Run(model, now, now + 1.0);
		layout = ResolveStatusIndicatorLayout(model, input);
		check(model.Find(2) == nullptr && Near(CenterX(*FindLayout(layout, 1)), 520, 1e-3), "gap filled by spring");
		check(!model.NeedsFrames() && model.WakeDeadline() == inf, "docked-only steady state needs no frames");
		check(!model.Advance(now + kFrame, kFrame, 1.0, true), "steady advance reports no change");
	}

	// normalBounds stays within baseBounds for 1..25 entries across main-button sizes.
	{
		struct Config { double w, h; StatusIndicatorRect base; };
		const Config configs[] = {
			{ 80, 80, { 451, 451, 549, 549 } },
			{ 84, 84, { 449, 449, 551, 551 } },
			{ 60, 60, { 461, 461, 539, 539 } },
			{ 80, 80, { 451, 451, 1200, 549 } },
		};
		bool contained = true, overflowSeen = false, reserved = true;
		for (const auto& config : configs)
		{
			StatusIndicatorLayoutInput input = FoldedInput();
			input.mainWidth = config.w; input.mainHeight = config.h; input.baseBounds = config.base;
			for (const double below : { 0.0, 1.0 })
			{
				input.below = below;
				for (std::uint64_t count = 1; count <= 25; count += 3)
				{
					StatusIndicatorModel model; double now = 0;
					std::uint64_t published = 0;
					const double end = 3.0 * static_cast<double>(count) + 2.0;
					while (now < end)
					{
						while (published < count && now >= 0.05 * static_cast<double>(published))
						{
							++published;
							const auto life = published % 3 == 0 ? Life::Notification : Life::Persistent;
							model.Publish(Spec(published, Kind::Debug, life), now);
							model.SetTextWidth(published, static_cast<double>((published * 37) % 220));
						}
						if (count >= 4 && Near(now, 2.0, kFrame / 2)) model.Retire(4, now);
						now += 1.0 / 30.0;
						model.Advance(now, 1.0 / 30.0, 1.0, true);
						const auto layout = ResolveStatusIndicatorLayout(model, input);
						if (!StatusIndicatorRectContains(config.base, layout.normalBounds, 1e-6)) contained = false;
						for (const auto& entry : layout.entries)
							if (!entry.overflow && !StatusIndicatorRectContains(config.base, entry.bounds, 1e-6)) contained = false;
						if (!StatusIndicatorRectContains(UnionStatusIndicatorRect(layout.reserveBounds, config.base),
							OutsetStatusIndicatorRect(layout.overflowBounds, -1.0), 1e-6)) reserved = false;
					}
					if (model.DockedCount() >= 2)
					{
						const auto docked = model.Entries().back().spec.key;
						model.ClickDocked(docked, now);
						Run(model, now, now + 0.5, true, 1.0 / 30.0);
						const auto layout = ResolveStatusIndicatorLayout(model, input);
						if (!StatusIndicatorRectContains(config.base, layout.normalBounds, 1e-6)) contained = false;
						if (!layout.overflowBounds.Empty()) overflowSeen = true;
					}
				}
			}
		}
		check(contained, "normal geometry never leaves baseBounds");
		check(reserved, "overflow geometry stays inside the reserved envelope");
		check(overflowSeen, "fan geometry reported as overflow");
	}

	// Compact fan: layers fill inward first, symmetric about straight up, newest at right, mirrored when inverted.
	{
		const double r0 = StatusIndicatorFanLayerRadius(80, 80, 3, 0), r1 = StatusIndicatorFanLayerRadius(80, 80, 3, 1);
		check(Near(r1 - r0, FanLayerGap) && r0 - FanSize / 2 - FanClear > StatusIndicatorSuperRadius(50 * Pi / 180, 40, 40, 3) - 1e-6,
			"first layer hugs the main button outside its corners");
		const auto cap0 = StatusIndicatorFanLayerCapacity(r0), cap1 = StatusIndicatorFanLayerCapacity(r1);
		check(cap0 >= 4 && cap1 > cap0, "outer layers hold more tiles");
		check(StatusIndicatorFanLayerCapacity(0) == 1, "fan capacity at least one");
		const auto input = FoldedInput();
		const std::size_t count = cap0 + 2;
		const auto first = ResolveStatusIndicatorFanPlacement(0, count, input);
		const auto second = ResolveStatusIndicatorFanPlacement(1, count, input);
		const auto last0 = ResolveStatusIndicatorFanPlacement(cap0 - 1, count, input);
		const auto outer = ResolveStatusIndicatorFanPlacement(cap0, count, input);
		check(first.layer == 0 && last0.layer == 0 && outer.layer == 1, "fan fills inner layer first");
		check(first.x > second.x && first.x > 500 && last0.x < 500 && Near(first.x - 500, 500 - last0.x, 1e-6),
			"newest at right, layer symmetric about straight up");
		check(Near(std::hypot(first.x - second.x, first.y - second.y), 2 * r0 * std::sin((FanSize + FanGap) / r0 / 2), 1e-6),
			"neighbouring tiles packed one slot apart");
		check(first.y < 500 && !first.mirrored, "fan opens upward");
		const auto single = ResolveStatusIndicatorFanPlacement(0, 1, input);
		check(Near(single.x, 500) && Near(single.y, 500 - r0), "single fan tile straight up");
		auto flipped = input; flipped.below = 1;
		const auto down = ResolveStatusIndicatorFanPlacement(0, 3, flipped);
		check(down.y > 500 && !down.mirrored, "inverted fan opens downward");
		auto low = input; low.work.top = 470;
		const auto mirrored = ResolveStatusIndicatorFanPlacement(0, 3, low);
		check(mirrored.mirrored && mirrored.y > 500, "fan mirrors when the work area is short");

		StatusIndicatorModel model; double now = 0;
		for (std::uint64_t key = 1; key <= 3; ++key) model.Publish(Spec(key, Kind::Debug), now);
		Run(model, now, 12.0);
		model.ClickDocked(1, now);
		Run(model, now, now + 0.6);
		const auto layout = ResolveStatusIndicatorLayout(model, input);
		const auto* newest = FindLayout(layout, 3); const auto* oldest = FindLayout(layout, 1);
		check(newest && oldest && newest->overflow && CenterX(*newest) > CenterX(*oldest)
			&& CenterY(*newest) < 500 && Near(newest->bounds.Width(), FanSize) && Near(newest->opacity, 1)
			&& newest->role == StatusIndicatorRole::Fan, "model fan places newest at right with full opacity");
		check(!layout.overflowBounds.Empty() && layout.normalBounds.Empty(), "fan bounds are overflow only");
	}

	// Scroll finishes inside the hold; long text fades at the moving edges.
	{
		const double limit = StatusIndicatorTextLimit();
		const double overflow = StatusIndicatorTextOverflow(200);
		check(Near(overflow, 200 - limit) && Near(limit, CapsuleMaxWidth - 2 * CapsulePadding - CapsuleIcon - CapsuleGap),
			"overflow uses capsule text limit");
		check(StatusIndicatorTextOverflow(20) == 0, "short text has no overflow");
		check(StatusIndicatorTextOverflow(8 * TextSize) == 0, "eight CJK glyphs fit without scrolling");
		check(StatusIndicatorScrollOffset(overflow, 0) == 0 && StatusIndicatorScrollOffset(overflow, ScrollPause) == 0,
			"scroll pauses at start");
		check(Near(StatusIndicatorScrollOffset(overflow, Hold - ScrollPause), overflow), "scroll done before hold end");
		double previous = 0; bool monotonic = true;
		for (double t = 0; t <= Hold; t += 0.01)
		{
			const double offset = StatusIndicatorScrollOffset(overflow, t);
			if (offset + 1e-9 < previous) monotonic = false;
			previous = offset;
		}
		check(monotonic, "scroll offset is monotonic");
		check(Near(StatusIndicatorCapsuleWidth(500), CapsuleMaxWidth) && Near(StatusIndicatorCapsuleWidth(0), 2 * CapsulePadding + CapsuleIcon + CapsuleGap),
			"capsule width follows text up to the cap");

		StatusIndicatorModel model; double now = 0;
		model.Publish(Spec(1, Kind::Debug), now);
		model.SetTextWidth(1, 200);
		model.SetDrawingActive(true);
		Run(model, now, 1.0, false);
		check(model.NeedsFrames(), "scrolling text needs frames even without ring or animations");
		{
			const auto midLayout = ResolveStatusIndicatorLayout(model, FoldedInput());
			const auto* mid = FindLayout(midLayout, 1);
			check(mid && mid->fadeLeft && mid->fadeRight, "mid scroll fades both edges");
		}
		Run(model, now, 2.8, false);
		const auto layout = ResolveStatusIndicatorLayout(model, FoldedInput());
		const auto* entry = FindLayout(layout, 1);
		check(entry && Near(entry->scrollOffset, overflow) && Near(entry->bounds.Width(), CapsuleMaxWidth)
			&& entry->fadeLeft && !entry->fadeRight, "front text fully scrolled");
		check(!model.NeedsFrames() && Near(model.WakeDeadline(), 3.0), "after scroll only hold deadline remains");
	}

	// Progress: monotonic, reset absorbed, failure ends cycle, completion lingers.
	{
		StatusIndicatorModel model; double now = 0;
		model.Publish(Spec(9, Kind::NewVersion, Life::Persistent, Action::OpenSoftwareVersion), now);
		Run(model, now, 4.0);
		check(model.SetProgress(9, 0.3, false, now), "progress starts cycle");
		Run(model, now, 5.0);
		check(Near(model.Progress().display, 0.3, 1e-3) && model.Progress().opacity == 1, "progress ring shows value");
		model.SetProgress(9, 0.5, false, now);
		check(!model.SetProgress(9, 0.0, false, now) && model.Progress().target == 0.5, "reset to zero absorbed");
		Run(model, now, 6.0);
		check(Near(model.Progress().display, 0.5, 1e-3), "display never decreases");
		model.SetDrawingActive(true);
		Run(model, now, 6.5);
		check(model.Progress().opacity == 0 && Near(model.Progress().display, 0.5, 1e-3), "drawing hides progress without losing it");
		model.SetDrawingActive(false);
		check(model.SetProgress(9, -1, false, now), "failure ends cycle");
		Run(model, now, 7.0);
		check(model.Progress().opacity == 0 && model.Find(9) != nullptr, "failed ring fades, entry kept");
		model.SetProgress(9, 0.2, false, now);
		check(model.Progress().display == 0 && model.Progress().target == 0.2, "new cycle restarts from zero");
		Run(model, now, 8.0);
		check(model.SetProgress(9, 1.0, true, now), "complete");
		const double completeAt = now;
		check(Near(model.WakeDeadline(), completeAt + ProgressCompleteLinger), "linger deadline");
		Run(model, now, completeAt + 2.9);
		check(model.Progress().active && model.Progress().display == 1 && model.Progress().opacity == 1, "complete ring lingers");
		const auto layout = ResolveStatusIndicatorLayout(model, FoldedInput());
		check(layout.progress.key == 9 && layout.progress.value == 1 && layout.progress.argb == kGreen
			&& Near(layout.progress.width, RingWidth), "progress layout is a thin line");
		Run(model, now, completeAt + 3.5);
		check(!model.Progress().active && model.Progress().opacity == 0 && model.Find(9), "ring fades after linger");
		check(!model.NeedsFrames() && model.WakeDeadline() == inf, "idle after progress");
	}

	// Drawing gate: ring hides, hold keeps running.
	{
		StatusIndicatorModel model; double now = 0;
		model.Publish(Spec(1, Kind::Debug), now);
		Run(model, now, 0.5);
		check(model.Ring().opacity == 1, "ring visible when not drawing");
		model.SetDrawingActive(true);
		Run(model, now, 1.0);
		check(model.Ring().opacity == 0 && Near(model.Find(1)->holdEnd, 3.0), "drawing hides ring, hold unchanged");
		check(!model.NeedsFrames() && Near(model.WakeDeadline(), 3.0), "hidden ring idles until deadline");
		model.SetDrawingActive(false);
		Run(model, now, 1.5);
		check(model.Ring().opacity == 1, "ring returns while owner still holds");
		const auto layout = ResolveStatusIndicatorLayout(model, FoldedInput());
		check(layout.ring.owner == 1 && Near(layout.ring.width, RingWidth)
			&& layout.ring.opacity >= 0.55 && layout.ring.opacity <= 1.0, "ring layout breathes as a thin line");
		model.SetDrawingActive(true);
		Run(model, now, 3.1);
		check(model.Find(1)->phase != Phase::Expanded, "hold expires while drawing");
	}

	// Display accent: lifted on the dark material, darkened on the light one.
	{
		const auto lightness = [](std::uint32_t argb) { return StatusIndicatorColor::ToOklch(argb)[0]; };
		const auto dark = StatusIndicatorColor::Display(kBlue, 0), light = StatusIndicatorColor::Display(kBlue, 1);
		check(lightness(dark) >= 0.71 && lightness(light) <= 0.53 && (dark >> 24) == 0xFF, "accent readable on both materials");
		const auto hueDark = StatusIndicatorColor::ToOklch(dark)[2], hueSource = StatusIndicatorColor::ToOklch(kBlue)[2];
		check(Near(hueDark, hueSource, 0.08), "display accent keeps the hue");
	}

	// Animations disabled: everything snaps.
	{
		StatusIndicatorModel model; double now = 0;
		model.Publish(Spec(1, Kind::Debug), now);
		model.Publish(Spec(2, Kind::Debug, Life::Notification), now);
		now += kFrame; model.Advance(now, kFrame, 1.0, false);
		check(model.Find(1)->appear == 1 && model.Ring().opacity == 1, "appear snaps");
		now = 3.01; model.Advance(now, kFrame, 1.0, false);
		check(model.Find(1)->phase == Phase::Docked && model.Find(1)->morph.position == 1 && model.Find(1)->landed,
			"collapse snaps to docked");
		check(model.Find(2)->holding && model.Find(2)->stack.position == 0, "next front snaps into slot");
		now = 6.02; model.Advance(now, kFrame, 1.0, false);
		check(model.Find(2) == nullptr, "notification dismiss snaps");
		model.Retire(1, now);
		now += kFrame; model.Advance(now, kFrame, 1.0, false);
		check(model.Count() == 0 && !model.NeedsFrames(), "exit snaps");
	}

	// 发布与过滤/点击可能在同一帧排队到达，未初始化的槽位不能留下永久续帧。
	{
		StatusIndicatorModel hidden; double now = 0;
		hidden.Publish(Spec(1, Kind::Debug), now);
		hidden.SetOptions({ false, true, true, true }, now);
		Run(hidden, now, 1.0);
		check(hidden.Count() == 1 && !hidden.NeedsFrames() && hidden.WakeDeadline() == inf,
			"filtered before first frame settles without busy loop");

		StatusIndicatorModel clicked; now = 0;
		clicked.Publish(Spec(1, Kind::Debug), now);
		clicked.ClickExpanded(1, now);
		Run(clicked, now, 1.5);
		check(clicked.Find(1)->phase == Phase::Docked && !clicked.NeedsFrames() && clicked.WakeDeadline() == inf,
			"collapsed before first frame settles without busy loop");
	}

	// Fan注销时冻结原弧槽位，不能因容量变化而从两端瞬移到正上方。
	{
		StatusIndicatorModel model; double now = 0;
		model.Publish(Spec(1, Kind::Debug), now);
		model.Publish(Spec(2, Kind::Debug), now);
		Run(model, now, 8.0);
		model.ClickDocked(1, now);
		Run(model, now, now + 0.5);
		const auto before = ResolveStatusIndicatorLayout(model, FoldedInput());
		model.Retire(2, now);
		model.Advance(now, 0, 1.0, true);
		const auto after = ResolveStatusIndicatorLayout(model, FoldedInput());
		for (const auto key : { 1ull, 2ull })
		{
			const auto* a = FindLayout(before, key); const auto* b = FindLayout(after, key);
			check(a && b && Near(CenterX(*a), CenterX(*b)) && Near(CenterY(*a), CenterY(*b)),
				"fan retirement keeps both visible centers on zero-dt frame");
		}
		check(!model.FanOpen() && model.FrozenFanCount() == 2, "retirement closes fan with original slots");
		Run(model, now, now + 0.05);
		const auto moving = ResolveStatusIndicatorLayout(model, FoldedInput());
		const auto* previous = FindLayout(after, 1); const auto* current = FindLayout(moving, 1);
		check(previous && current && std::hypot(CenterX(*previous) - CenterX(*current),
			CenterY(*previous) - CenterY(*current)) < 20, "fan retirement continues with a small first motion");
		model.Retire(1, now);
		model.Advance(now, 0, 1.0, true);
		const auto twice = ResolveStatusIndicatorLayout(model, FoldedInput());
		const auto* retired = FindLayout(twice, 1);
		check(current && retired && Near(CenterX(*current), CenterX(*retired))
			&& Near(CenterY(*current), CenterY(*retired)), "second retirement keeps the original fan snapshot");
		Run(model, now, now + 1.0);
		check(model.Count() == 0 && model.FrozenFanCount() == 0 && !model.NeedsFrames(), "retired fan returns to idle");
	}

	// Docked tiles sit on the main button border: the part inside the button yields to the main button.
	{
		const auto input = FoldedInput();
		check(StatusIndicatorInsideMainButton(input, 526, 470, MainButtonYield), "tile lower half inside the button yields");
		check(!StatusIndicatorInsideMainButton(input, 526, 458, MainButtonYield), "tile part outside the button stays hittable");
		check(!StatusIndicatorInsideMainButton(input, 500, 462.5, MainButtonYield), "the 3 DIP rim next to the border stays with the tile");
		check(StatusIndicatorInsideMainButton(input, 500, 500, MainButtonYield), "button center is never a tile hit");
	}

	// New version source: publish once, silent download progress, completion and retirement.
	{
		using Tracker = StatusIndicatorUpdateTracker;
		Tracker tracker;
		auto a = tracker.Step(1, 0, 0);
		check(!a.publish && !a.progress && !tracker.Published(), "checking for updates publishes nothing");
		a = tracker.Step(Tracker::Downloading, 0, 1000);
		check(!a.publish && !tracker.Published(), "silent download waits for progress above 0%");
		a = tracker.Step(Tracker::Downloading, 100, 1000);
		check(a.publish && a.progress && Near(a.value, 0.1) && tracker.IsDownloading(), "first bytes publish the capsule with progress");
		a = tracker.Step(Tracker::Downloading, 101, 1000);
		check(!a.publish && !a.progress, "tiny increments are throttled");
		a = tracker.Step(Tracker::Downloading, 20, 1000);
		check(!a.progress, "line reset does not report a lower value");
		a = tracker.Step(Tracker::Downloading, 600, 1000);
		check(a.progress && Near(a.value, 0.6), "progress keeps increasing");
		a = tracker.Step(Tracker::Restart, 1000, 1000);
		check(!a.publish && a.progress && a.complete && Near(a.value, 1), "staged update completes the ring");
		a = tracker.Step(Tracker::Restart, 1000, 1000);
		check(!a.progress, "completion is reported once");
		a = tracker.Step(Tracker::Latest, 0, 0);
		check(a.retire && !tracker.Published(), "latest version retires the state");

		Tracker manual;
		a = manual.Step(Tracker::New, 0, 0);
		check(a.publish && !a.progress, "update found without silent update publishes without a ring");
		a = manual.Step(Tracker::Downloading, 500, 1000);
		check(!a.publish && a.progress && Near(a.value, 0.5), "later download reuses the published state");
		a = manual.Step(Tracker::DownloadFail, 500, 1000);
		check(a.progress && a.value < 0 && manual.Published(), "download failure ends the ring but keeps the state");
	}

	// A new state closes the re-expanded capsule or the open fan so it owns the anchor.
	{
		StatusIndicatorModel model; double now = 0;
		model.Publish(Spec(1, Kind::SuperTop), now);
		Run(model, now, 4.5);
		model.ClickDocked(1, now);
		Run(model, now, now + 0.8);
		check(model.Reexpanded(), "super top re-expanded for reading");
		model.Publish(Spec(2, Kind::PptSlideshow), now);
		check(!model.Reexpanded() && model.Find(1)->morphTarget == 1 && model.FrontKey() == 2,
			"new slideshow state closes the re-expanded capsule at once");
		Run(model, now, now + 4.5);
		model.Publish(Spec(3, Kind::Debug), now);
		Run(model, now, now + 4.5);
		model.ClickDocked(1, now);
		check(model.FanOpen(), "fan open with several docked tiles");
		model.ClickDocked(2, now);
		model.Publish(Spec(4, Kind::Debug), now);
		check(!model.FanOpen() && model.Selected() == 0, "new state closes the fan and its selection");
	}

	// Click state machine.
	{
		StatusIndicatorModel single; double now = 0;
		single.Publish(Spec(1, Kind::NewVersion, Life::Persistent, Action::OpenSoftwareVersion), now);
		Run(single, now, 4.5);
		check(single.ClickDocked(1, now) == Action::OpenSoftwareVersion && !single.FanOpen() && !single.Reexpanded(),
			"single docked runs action");

		// 单个无动作方块：就地重新展开，再点或 Hold 到期收回。
		StatusIndicatorModel ppt; now = 0;
		ppt.Publish(Spec(1, Kind::PptSlideshow), ppt.Now());
		ppt.SetTextWidth(1, 52);
		Run(ppt, now, 4.5);
		const auto input = FoldedInput();
		check(ppt.ClickDocked(1, now) == Action::None && !ppt.FanOpen() && ppt.Reexpanded(), "single docked re-expands");
		check(Near(ppt.WakeDeadline(), now + Hold), "re-expanded capsule auto closes after hold");
		Run(ppt, now, now + 0.8);
		{
			const auto openLayout = ResolveStatusIndicatorLayout(ppt, input);
			const auto* open = FindLayout(openLayout, 1);
			check(open && open->shape == StatusIndicatorShape::Capsule && open->role == StatusIndicatorRole::Capsule
				&& Near(CenterY(*open), 460, 0.05) && Near(open->bounds.Width(), StatusIndicatorCapsuleWidth(52), 0.1)
				&& open->textOpacity > 0.99, "re-expanded capsule returns to the anchor with text");
		}
		check(ppt.ClickExpanded(1, now) && !ppt.Reexpanded(), "click re-expanded capsule closes it");
		Run(ppt, now, now + 1.2);
		{
			const auto backLayout = ResolveStatusIndicatorLayout(ppt, input);
			const auto* back = FindLayout(backLayout, 1);
			check(back && back->shape == StatusIndicatorShape::Tile && back->role == StatusIndicatorRole::Dock
				&& !ppt.NeedsFrames(), "closed capsule docks again and idles");
		}
		ppt.ClickDocked(1, now);
		Run(ppt, now, now + Hold + 1.5);
		check(!ppt.Reexpanded() && ppt.Find(1)->landed && !ppt.NeedsFrames(), "re-expansion auto closes");

		StatusIndicatorModel model; now = 0;
		model.Publish(Spec(1, Kind::PptSlideshow), now);
		model.Publish(Spec(2, Kind::NewVersion, Life::Persistent, Action::OpenSoftwareVersion), now);
		model.Publish(Spec(3, Kind::SuperTop), now);
		Run(model, now, 11.0);
		check(model.ClickDocked(1, now) == Action::None && model.FanOpen() && model.Selected() == 0, "click opens fan");
		check(Near(model.WakeDeadline(), now + FanAutoClose), "fan auto close deadline");
		model.ClickDocked(1, now);
		check(model.Selected() == 1, "click in fan selects");
		Run(model, now, now + 0.5);
		const auto layout = ResolveStatusIndicatorLayout(model, input);
		const auto* selected = FindLayout(layout, 1);
		check(selected && selected->shape == StatusIndicatorShape::Capsule && selected->overflow
			&& selected->z == static_cast<int>(layout.entries.size()) - 1, "selected capsule shown on top");
		model.ClickDocked(1, now);
		check(model.Selected() == 0 && model.FanOpen(), "click same deselects");
		model.ClickDocked(1, now);
		model.ClickDocked(3, now);
		check(model.Selected() == 0, "click other deselects");
		model.ClickDocked(3, now);
		check(model.ClickExpanded(3, now) && model.Selected() == 0, "click selected capsule collapses it");
		check(model.ClickDocked(2, now) == Action::OpenSoftwareVersion && !model.FanOpen(), "action tile in fan runs and closes");
		model.ClickDocked(1, now);
		check(model.FanOpen() && model.ClickOutside(now) && !model.FanOpen(), "outside click closes fan");
		check(!model.ClickOutside(now), "outside click without fan is no-op");
		model.ClickDocked(1, now);
		const double opened = now;
		Run(model, now, opened + FanAutoClose - 0.1);
		check(model.FanOpen(), "fan stays open before auto close");
		Run(model, now, opened + FanAutoClose + 0.1);
		check(!model.FanOpen(), "fan auto closes after inactivity");
		Run(model, now, now + 1.0);
		check(model.FanProgress() == 0 && !model.NeedsFrames(), "closed fan idles");
		model.ClickDocked(1, now);
		model.Retire(2, now); model.Retire(3, now);
		Run(model, now, now + 0.1);
		check(!model.FanOpen(), "fan closes when fewer than two docked");

		StatusIndicatorModel expanded; now = 0;
		expanded.Publish(Spec(1, Kind::Debug), now);
		expanded.Publish(Spec(2, Kind::Debug, Life::Notification), now);
		Run(expanded, now, 0.5);
		check(expanded.ClickExpanded(1, now) && expanded.Find(1)->phase == Phase::Collapsing, "click capsule collapses persistent");
		check(expanded.FrontKey() == 2 && Near(expanded.Find(2)->holdStart, 0.5), "next becomes front on click");
		check(expanded.ClickExpanded(2, now) && expanded.Find(2)->phase == Phase::Dismissing, "click notification dismisses");
		Run(expanded, now, 2.0);
		check(expanded.Find(2) == nullptr && expanded.Find(1)->phase == Phase::Docked, "dismissed notification removed");
		check(!expanded.ClickExpanded(1, now), "click on docked unselected tile is not an expanded click");
	}

	return failures;
}
