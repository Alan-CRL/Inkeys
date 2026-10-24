#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.Bridge.h"
#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.Shortcut.h"
#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.h"
#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.PresentationState.h"
#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.TimerPeriod.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

namespace
{
	using namespace Inkeys::Drawing::Draw3::Bridge;
	using Inkeys::Drawing::Draw3::MouseCursorStyle;
	using Inkeys::Drawing::Draw3::PenCursorStyle;
	using Inkeys::Drawing::Draw3::DrawpadPresentationSurface;
	using Inkeys::Drawing::Draw3::ResolveDrawpadPresentationSurface;
	using Inkeys::Drawing::Draw3::TimerPeriodController;

	struct TimerPeriodCalls
	{
		int beginCount = 0;
		int endCount = 0;
		unsigned int lastPeriod = 0;
		bool beginSucceeds = true;
	};

	bool BeginTimerPeriod(void* context, unsigned int period) noexcept
	{
		auto* calls = static_cast<TimerPeriodCalls*>(context);
		if (!calls) return false;
		++calls->beginCount;
		calls->lastPeriod = period;
		return calls->beginSucceeds;
	}

	void EndTimerPeriod(void* context, unsigned int period) noexcept
	{
		auto* calls = static_cast<TimerPeriodCalls*>(context);
		if (!calls) return;
		++calls->endCount;
		calls->lastPeriod = period;
	}

	bool Expect(bool condition, const char* name)
	{
		if (!condition) std::cerr << "[Draw3Bridge] failed: " << name << '\n';
		return condition;
	}

	void TestProductState(int& failures)
	{
		StateBridge bridge;
		const ProductState initial = bridge.Snapshot();
		if (!Expect(initial.colorRgba == 0x000000FFu,
			"default color uses RGBA black")) ++failures;
		if (!Expect(initial.revision == 0,
			"default snapshot starts at revision zero")) ++failures;
		if (!Expect(initial.cursorSettings.mouseStyle == MouseCursorStyle::System
			&& initial.cursorSettings.penStyle == PenCursorStyle::Circle,
			"bridge defaults use the system mouse cursor and the original pen circle")) ++failures;
		if (!Expect(initial.selectionMode,
			"default product state starts in selection mode")) ++failures;
		if (!Expect(initial.workspace == Workspace::Desktop,
			"default product state starts in desktop workspace")) ++failures;

		ProductState first{};
		first.colorRgba = 0x12345678u;
		first.widthDip = 4.5f;
		first.autoSaveEnabled = true;
		first.revision = 99;
		bridge.PublishState(first);
		const ProductState firstSnapshot = bridge.Snapshot();
		if (!Expect(firstSnapshot.colorRgba == 0x12345678u
			&& firstSnapshot.widthDip == 4.5f
			&& firstSnapshot.autoSaveEnabled
			&& firstSnapshot.revision == 1,
			"publish owns the next revision")) ++failures;

		first.colorRgba = 0xAABBCCDDu;
		bridge.PublishState(first);
		const ProductState secondSnapshot = bridge.Snapshot();
		// 快照按值返回，后续发布不能回写已经取得的产品状态。
		if (!Expect(firstSnapshot.colorRgba == 0x12345678u
			&& firstSnapshot.revision == 1
			&& secondSnapshot.colorRgba == 0xAABBCCDDu
			&& secondSnapshot.revision == 2,
			"snapshots are immutable with monotonic revisions")) ++failures;

		ProductState pageState = secondSnapshot;
		pageState.page = 6;
		pageState.hasPage = true;
		pageState.workspace = Workspace::Whiteboard;
		bridge.PublishState(pageState);
		ProductState toolOnlyState = bridge.Snapshot();
		toolOnlyState.tool = Tool::Laser;
		bridge.PublishState(toolOnlyState);
		const ProductState preservedPage = bridge.Snapshot();
		if (!Expect(preservedPage.tool == Tool::Laser && preservedPage.hasPage
			&& preservedPage.page == 6
			&& preservedPage.workspace == Workspace::Whiteboard,
			"tool state publication preserves page and workspace")) ++failures;

		if (!Expect(bridge.PublishWorkspace(Workspace::Desktop),
			"explicit desktop publication changes workspace")) ++failures;
		const ProductState desktop = bridge.Snapshot();
		if (!Expect(desktop.workspace == Workspace::Desktop && !desktop.hasPage
			&& desktop.page == 0,
			"desktop publication clears stale PPT page identity")) ++failures;
		if (!Expect(!bridge.PublishWorkspace(Workspace::Desktop),
			"repeated desktop publication is idempotent")) ++failures;
		if (!Expect(bridge.PublishWorkspace(Workspace::Presentation)
			&& bridge.Snapshot().workspace == Workspace::Presentation,
			"presentation is an explicit third workspace")) ++failures;

		const auto beforeCursor = bridge.Snapshot();
		auto cursorOnly = beforeCursor;
		cursorOnly.cursorSettings.penStyle = PenCursorStyle::PenIcon;
		bridge.PublishState(cursorOnly);
		const auto afterCursor = bridge.Snapshot();
		if (!Expect(afterCursor.revision == beforeCursor.revision + 1
			&& afterCursor.cursorSettings == cursorOnly.cursorSettings
			&& beforeCursor.cursorSettings.penStyle == PenCursorStyle::Circle
			&& afterCursor.cursorSettings.mouseStyle == beforeCursor.cursorSettings.mouseStyle
			&& afterCursor.tool == beforeCursor.tool && afterCursor.widthDip == beforeCursor.widthDip,
			"pen-only cursor updates publish an immutable revision without changing mouse or brush")) ++failures;
		cursorOnly = afterCursor;
		cursorOnly.cursorSettings.mouseStyle = MouseCursorStyle::Drawing;
		bridge.PublishState(cursorOnly);
		const auto mouseChanged = bridge.Snapshot();
		if (!Expect(mouseChanged.revision == afterCursor.revision + 1
			&& mouseChanged.cursorSettings.penStyle == PenCursorStyle::PenIcon
			&& mouseChanged.cursorSettings.mouseStyle == MouseCursorStyle::Drawing
			&& afterCursor.cursorSettings.mouseStyle == MouseCursorStyle::System,
			"mouse-only cursor updates preserve the independently selected pen type")) ++failures;
	}

	void TestMouseCursorToolProjection(int& failures)
	{
		// 断言表直接表达产品规则；调用 Host 使用的生产 helper，不在测试中复制分支。
		struct ToolCase { Tool tool; MouseCursorStyle systemChoice; MouseCursorStyle drawingChoice; };
		constexpr ToolCase cases[] = {
			{ Tool::Pen, MouseCursorStyle::System, MouseCursorStyle::Drawing },
			{ Tool::HardPen, MouseCursorStyle::System, MouseCursorStyle::Drawing },
			{ Tool::Highlighter, MouseCursorStyle::Drawing, MouseCursorStyle::Drawing },
			{ Tool::FixedEraser, MouseCursorStyle::System, MouseCursorStyle::System },
			{ Tool::SpeedEraser, MouseCursorStyle::System, MouseCursorStyle::System },
			{ Tool::Laser, MouseCursorStyle::System, MouseCursorStyle::System },
			{ Tool::SolidLine, MouseCursorStyle::System, MouseCursorStyle::System },
			{ Tool::DashedLine, MouseCursorStyle::System, MouseCursorStyle::System },
			{ Tool::OutlineRectangle, MouseCursorStyle::System, MouseCursorStyle::System },
			{ Tool::FilledRectangle, MouseCursorStyle::System, MouseCursorStyle::System },
			{ Tool::ConfiguredEraser, MouseCursorStyle::System, MouseCursorStyle::System }
		};
		for (const auto& test : cases)
		{
			if (!Expect(ResolveMouseCursorStyleForTool(test.tool, MouseCursorStyle::System) == test.systemChoice &&
				ResolveMouseCursorStyleForTool(test.tool, MouseCursorStyle::Drawing) == test.drawingChoice,
				"production tool projection keeps highlighter independent and excludes every shape")) ++failures;
		}
		const Inkeys::Drawing::Draw3::CursorSettings raw{ MouseCursorStyle::System, PenCursorStyle::PenIcon };
		const auto marker = ResolveCursorSettingsForTool(Tool::Highlighter, raw);
		const auto shape = ResolveCursorSettingsForTool(Tool::SolidLine, raw);
		const auto brush = ResolveCursorSettingsForTool(Tool::Pen, raw);
		if (!Expect(marker.mouseStyle == MouseCursorStyle::Drawing &&
			shape.mouseStyle == MouseCursorStyle::System && brush.mouseStyle == MouseCursorStyle::System &&
			raw.mouseStyle == MouseCursorStyle::System &&
			marker.penStyle == PenCursorStyle::PenIcon && shape.penStyle == raw.penStyle && brush.penStyle == raw.penStyle,
			"actual marker-to-shape-to-brush projection preserves raw system preference and independent pen type")) ++failures;
		using Inkeys::Drawing::Draw3::RestoreMouseCursorStyle;
		using Inkeys::Drawing::Draw3::RestorePenCursorStyle;
		if (!Expect(RestoreMouseCursorStyle(0) == MouseCursorStyle::System &&
			RestoreMouseCursorStyle(1) == MouseCursorStyle::Drawing &&
			RestoreMouseCursorStyle(-1) == MouseCursorStyle::System &&
			RestoreMouseCursorStyle(2) == MouseCursorStyle::System &&
			RestorePenCursorStyle(0) == PenCursorStyle::Circle &&
			RestorePenCursorStyle(1) == PenCursorStyle::PenIcon &&
			RestorePenCursorStyle(-1) == PenCursorStyle::Circle &&
			RestorePenCursorStyle(2) == PenCursorStyle::Circle,
			"shared cursor restorers accept only one as the optional pen icon")) ++failures;
	}

	void TestPresentationSessionIdentity(int& failures)
	{
		StateBridge bridge;
		PresentationTarget target;
		target.key.bytes[0] = 1;
		target.sourceIdentity = "path:c:\\session.pptx";
		target.totalPages = 2;
		target.bindingRevision = 7;
		target.sessionRevision = 11;
		const auto first = bridge.PublishPresentationTarget(target);
		const auto old = ReadyIdentityFor(*bridge.Snapshot().presentationTarget);
		const auto repeat = bridge.PublishPresentationTarget(target);
		if (!Expect(first == repeat, "same session and absolute target publication is idempotent")) ++failures;
		++target.sessionRevision;
		const auto reopened = bridge.PublishPresentationTarget(target);
		const auto current = ReadyIdentityFor(*bridge.Snapshot().presentationTarget);
		if (!Expect(first && reopened && *first != *reopened && old != current &&
			current.sessionRevision == target.sessionRevision,
			"same file and page in a new show cannot match an old ready or UI ack")) ++failures;
		bridge.Publish(CommandType::Clear);
		target.pageIndex = 1;
		bridge.PublishPresentationTarget(target);
		Command captured;
		if (!Expect(bridge.TryConsume(captured) && captured.presentationTarget &&
			ReadyIdentityFor(*captured.presentationTarget) == current,
			"page change preserves the accepted command session and target")) ++failures;
		const auto beforeReset = bridge.Snapshot().presentationTarget->targetRevision;
		bridge.Reset();
		const auto afterReset = bridge.PublishPresentationTarget(target);
		if (!Expect(afterReset && *afterReset > beforeReset,
			"Host reset does not reuse target revisions from an old UI commit")) ++failures;
	}

	void TestCommandQueue(int& failures)
	{
		StateBridge bridge(2);
		if (!Expect(bridge.Publish(CommandType::Clear) == CommandResult::Accepted
			&& bridge.Publish(CommandType::Undo) == CommandResult::Accepted,
			"commands enter the bounded queue")) ++failures;
		if (!Expect(bridge.Publish(CommandType::Redo) == CommandResult::QueueFull,
			"queue rejects commands at capacity")) ++failures;

		Command command{};
		if (!Expect(bridge.TryConsume(command)
			&& command.type == CommandType::Clear && command.sequence == 1,
			"first command preserves order and sequence")) ++failures;
		if (!Expect(bridge.TryConsume(command)
			&& command.type == CommandType::Undo && command.sequence == 2,
			"second command preserves order and sequence")) ++failures;
		if (!Expect(!bridge.TryConsume(command),
			"empty queue reports no command")) ++failures;

		StateBridge minimumCapacity(0);
		if (!Expect(minimumCapacity.Publish(CommandType::Redo) == CommandResult::Accepted
			&& minimumCapacity.Publish(CommandType::Clear) == CommandResult::QueueFull,
			"zero capacity is normalized to one")) ++failures;

		PresentationTarget targetA{};
		targetA.key.bytes[0] = 0xA;
		targetA.sourceIdentity = "path:a.pptx";
		targetA.bindingToken = "binding:a";
		targetA.bindingMode = SlideBindingMode::StableSlideId;
		targetA.slideIds = { 101 };
		targetA.slideId = 101;
		targetA.totalPages = 1;
		PresentationTarget targetB = targetA;
		targetB.key.bytes[0] = 0xB;
		targetB.sourceIdentity = "path:b.pptx";
		targetB.bindingToken = "binding:b";

		StateBridge desktopAfterClear;
		const auto targetARevision = desktopAfterClear.PublishPresentationTarget(targetA);
		desktopAfterClear.Publish(CommandType::Clear);
		desktopAfterClear.PublishWorkspace(Workspace::Desktop);
		if (!Expect(desktopAfterClear.TryConsume(command) && targetARevision &&
			command.type == CommandType::Clear &&
			command.workspace == Workspace::Presentation &&
			command.presentationTarget &&
			command.presentationTarget->key == targetA.key &&
			desktopAfterClear.Snapshot().workspace == Workspace::Desktop,
			"A Clear keeps the A scene before the latest Desktop transition"))
			++failures;

		StateBridge bAfterUndo;
		bAfterUndo.PublishPresentationTarget(targetA);
		bAfterUndo.Publish(CommandType::Undo);
		bAfterUndo.Publish(CommandType::Redo);
		const auto targetBRevision = bAfterUndo.PublishPresentationTarget(targetB);
		if (!Expect(bAfterUndo.TryConsume(command) && targetBRevision &&
			command.type == CommandType::Undo &&
			command.workspace == Workspace::Presentation &&
			command.presentationTarget &&
			command.presentationTarget->key == targetA.key &&
			bAfterUndo.Snapshot().presentationTarget &&
			bAfterUndo.Snapshot().presentationTarget->key == targetB.key,
			"A Undo keeps the A scene before the latest B target")) ++failures;
		if (!Expect(bAfterUndo.TryConsume(command) &&
			command.type == CommandType::Redo &&
			command.workspace == Workspace::Presentation &&
			command.presentationTarget &&
			command.presentationTarget->key == targetA.key,
			"A Redo keeps the A scene before the latest B target")) ++failures;
	}

	void TestUnsupportedAndLifecycle(int& failures)
	{
		StateBridge bridge(2);
		if (!Expect(bridge.Publish(CommandType::Save) == CommandResult::Unsupported
			&& bridge.Publish(CommandType::SuperRecovery) == CommandResult::Unsupported
			&& bridge.Publish(CommandType::AutoStraighten) == CommandResult::Unsupported
			&& bridge.Publish(CommandType::InputTest) == CommandResult::Unsupported
			&& bridge.Publish(CommandType::PrepareExitAutoSave) == CommandResult::Unsupported,
			"not-ready commands remain explicitly unsupported")) ++failures;

		if (!Expect(bridge.Publish(CommandType::NextPage) == CommandResult::Accepted,
			"unsupported commands do not consume queue sequence")) ++failures;
		Command command{};
		if (!Expect(bridge.TryConsume(command) && command.sequence == 1,
			"first supported command keeps sequence one")) ++failures;

		bridge.Publish(CommandType::PreviousPage);
		bridge.Stop();
		if (!Expect(!bridge.Running()
			&& bridge.TryConsume(command) && command.type == CommandType::PreviousPage
			&& bridge.Publish(CommandType::Clear) == CommandResult::NotRunning
			&& bridge.Publish(CommandType::Save) == CommandResult::NotRunning,
			"stop closes producers and preserves accepted commands")) ++failures;

		ProductState stoppedState{};
		stoppedState.colorRgba = 0xFFFFFFFFu;
		bridge.PublishState(stoppedState);
		if (!Expect(bridge.Snapshot().revision == 0,
			"stopped bridge rejects state publication")) ++failures;

		bridge.Reset();
		const ProductState resetState = bridge.Snapshot();
		if (!Expect(bridge.Running()
			&& resetState.colorRgba == 0x000000FFu
			&& resetState.revision == 0
			&& !bridge.TryConsume(command),
			"reset reopens and clears bridge state")) ++failures;
		if (!Expect(bridge.Publish(CommandType::Redo) == CommandResult::Accepted
			&& bridge.TryConsume(command) && command.sequence == 1,
			"reset restarts command sequence")) ++failures;

		bridge.Reset();
		PresentationTarget exitTarget{};
		exitTarget.key.bytes[0] = 0xA;
		exitTarget.sourceIdentity = "path:a.pptx";
		exitTarget.bindingToken = "binding:a";
		exitTarget.totalPages = 1;
		bridge.PublishPresentationTarget(exitTarget);
		bridge.Publish(CommandType::Clear);
		if (!Expect(bridge.StopWithFinalCommand(CommandType::PrepareExitAutoSave)
			&& bridge.TryConsume(command) && command.type == CommandType::Clear
			&& command.presentationTarget && command.presentationTarget->key == exitTarget.key
			&& bridge.TryConsume(command) &&
				command.type == CommandType::PrepareExitAutoSave &&
				command.presentationTarget && command.presentationTarget->key == exitTarget.key
			&& !bridge.TryConsume(command),
			"shutdown barrier stays behind every accepted command in its captured scene"))
			++failures;

		StateBridge fullBridge(1);
		if (!Expect(fullBridge.Publish(CommandType::Undo) == CommandResult::Accepted
			&& fullBridge.StopWithFinalCommand(CommandType::PrepareExitAutoSave)
			&& fullBridge.TryConsume(command) && command.type == CommandType::Undo
			&& fullBridge.TryConsume(command) &&
				command.type == CommandType::PrepareExitAutoSave,
			"shutdown barrier bypasses a full business queue")) ++failures;
	}

	void TestEraserModeMapping(int& failures)
	{
		if (!Expect(NormalizeLegacyEraserMode(0) == 1
			&& NormalizeLegacyEraserMode(1) == 1,
			"legacy pressure eraser normalizes to speed eraser")) ++failures;
		if (!Expect(NormalizeLegacyEraserMode(2) == 2
			&& NormalizeLegacyEraserMode(-1) == 2
			&& NormalizeLegacyEraserMode(99) == 2,
			"unknown eraser modes normalize to fixed eraser")) ++failures;
		if (!Expect(EncodeEraserMode(Tool::SpeedEraser) == 1
			&& EncodeEraserMode(Tool::FixedEraser) == 2
			&& EncodeEraserMode(Tool::Pen) == 2,
			"eraser encoding only emits speed or fixed values")) ++failures;
	}

	void TestTimerPeriodController(int& failures)
	{
		TimerPeriodCalls calls;
		{
			TimerPeriodController timer({ &calls, &BeginTimerPeriod, &EndTimerPeriod });
			if (!Expect(timer.SelectionMode() && !timer.Active()
				&& calls.beginCount == 0 && calls.endCount == 0,
				"selection mode does not request high timer resolution")) ++failures;

			timer.SetSelectionMode(false);
			timer.SetSelectionMode(false);
			if (!Expect(!timer.SelectionMode() && timer.Active()
				&& calls.beginCount == 1 && calls.endCount == 0
				&& calls.lastPeriod == 1,
				"drawing mode begins one timer period idempotently")) ++failures;

			timer.SetSelectionMode(true);
			timer.SetSelectionMode(true);
			if (!Expect(timer.SelectionMode() && !timer.Active()
				&& calls.beginCount == 1 && calls.endCount == 1,
				"returning to selection ends the successful period once")) ++failures;

			timer.SetSelectionMode(false);
			if (!Expect(timer.Active() && calls.beginCount == 2,
				"re-entering drawing begins a new timer period")) ++failures;
		}
		if (!Expect(calls.endCount == 2,
			"controller destruction releases an active timer period")) ++failures;

		TimerPeriodCalls failedCalls;
		failedCalls.beginSucceeds = false;
		TimerPeriodController failedTimer({
			&failedCalls, &BeginTimerPeriod, &EndTimerPeriod });
		failedTimer.SetSelectionMode(false);
		failedTimer.SetSelectionMode(false);
		failedTimer.SetSelectionMode(true);
		if (!Expect(!failedTimer.Active() && failedCalls.beginCount == 1
			&& failedCalls.endCount == 0,
			"failed begin is not retried or paired within the same drawing visit")) ++failures;

		failedCalls.beginSucceeds = true;
		failedTimer.SetSelectionMode(false);
		if (!Expect(failedTimer.Active() && failedCalls.beginCount == 2,
			"failed begin is retried after leaving and re-entering drawing")) ++failures;
		failedTimer.SetSelectionMode(true);
		if (!Expect(!failedTimer.Active() && failedCalls.endCount == 1,
			"successful retry receives exactly one matching end")) ++failures;
	}

	void TestSceneReadyHandoff(int& failures)
	{
		using namespace Inkeys::Drawing::Draw3;
		ProductState desired{};
		std::optional<PresentationReadyIdentity> none;
		for (const auto workspace : { Workspace::Desktop, Workspace::Whiteboard, Workspace::Presentation })
		{
			desired.workspace = workspace;
			if (!Expect(PresentationSceneNeedsReady(desired, workspace, none, true),
				"same workspace retains outstanding command-scene ready responsibility")) ++failures;
			if (!Expect(!PresentationSceneNeedsReady(desired, workspace, none, false),
				"settled workspace does not create a spurious ready responsibility")) ++failures;
		}
		PresentationTarget target{};
		target.key.bytes[0] = 7;
		target.totalPages = 2;
		target.sessionRevision = 9;
		target.targetRevision = 3;
		desired.presentationTarget = std::make_shared<PresentationTarget>(target);
		const auto identity = ReadyIdentityFor(target);
		if (!Expect(PresentationSceneNeedsReady(desired, Workspace::Presentation, none, false),
			"missing CPU identity requires ready even in the same presentation workspace")) ++failures;
		if (!Expect(!PresentationSceneNeedsReady(desired, Workspace::Presentation, identity, false),
			"matching CPU identity settles the scene handoff")) ++failures;
		std::optional<PresentationReadyIdentity> ui;
		if (!Expect(TryLatchPresentationUiReady(desired, none, identity, ui) && ui == identity,
			"actual UI commit is retained before CPU ready, including temporary old-scene replay")) ++failures;
		if (!Expect(PresentationSceneNeedsReady(desired, Workspace::Presentation, none, true),
			"early UI commit does not settle the independent CPU/command scene obligation")) ++failures;
		auto stale = identity;
		--stale.targetRevision;
		if (!Expect(!TryLatchPresentationUiReady(desired, none, stale, ui) && ui == identity,
			"stale UI target cannot overwrite the retained current commit")) ++failures;
		ui.reset();
		if (!Expect(!TryLatchPresentationUiReady(desired, identity, identity, ui) && !ui,
			"suspended target cannot latch a UI commit")) ++failures;
		desired.workspace = Workspace::Whiteboard;
		if (!Expect(!TryLatchPresentationUiReady(desired, none, identity, ui) && !ui,
			"presentation UI commit cannot cross into whiteboard")) ++failures;
		desired.workspace = Workspace::Presentation;
		desired.presentationTarget.reset();
		if (!Expect(!TryLatchPresentationUiReady(desired, none, identity, ui),
			"presentation without a trusted target cannot accept old UI commit")) ++failures;
	}

	void TestSceneDispatchResponsibility(int& failures)
	{
		using namespace Inkeys::Drawing::Draw3;
		ProductState desired{};
		if (!Expect(!PresentationSceneNeedsDispatch(desired, Workspace::Desktop,
			Workspace::Desktop, 0, false), "settled desktop heartbeat does not dispatch")) ++failures;
		PresentationTarget target{};
		target.key.bytes[0] = 17;
		target.sourceIdentity = "path:c:\\scene-dispatch.pptx";
		target.totalPages = 2;
		target.targetRevision = 4;
		desired.presentationTarget = std::make_shared<PresentationTarget>(target);
		for (const auto origin : { Workspace::Desktop, Workspace::Whiteboard })
		{
			desired.workspace = Workspace::Presentation;
			if (!Expect(PresentationSceneNeedsDispatch(desired, origin, origin, 0, false),
				"PPT request dispatches from desktop and whiteboard")) ++failures;
			if (!Expect(!PresentationSceneNeedsDispatch(desired, origin,
				Workspace::Presentation, target.targetRevision, false),
				"same pending PPT heartbeat leaves the single Controller retry in charge")) ++failures;
			desired.workspace = origin;
			desired.presentationTarget.reset();
			if (!Expect(PresentationSceneNeedsDispatch(desired, origin,
				Workspace::Presentation, target.targetRevision, false),
				"return to presented origin dispatches to retire a failed PPT request")) ++failures;
			if (!Expect(!PresentationSceneNeedsDispatch(desired, origin, origin, 0, false),
				"retired PPT request does not cause repeated origin dispatch")) ++failures;
			desired.presentationTarget = std::make_shared<PresentationTarget>(target);
		}
		desired.presentationTarget.reset();
		for (const auto origin : { Workspace::Desktop, Workspace::Whiteboard, Workspace::Presentation })
		{
			desired.workspace = origin;
			const auto failed = origin == Workspace::Whiteboard ? Workspace::Desktop : Workspace::Whiteboard;
			if (!Expect(PresentationSceneNeedsDispatch(desired, origin, failed, 0, false),
				"workspace-only failed request retires even without a PPT target revision")) ++failures;
			if (!Expect(!PresentationSceneNeedsDispatch(desired, origin, origin, 0, false),
				"targetless workspace heartbeat remains deduplicated")) ++failures;
		}
		desired.workspace = Workspace::Presentation;
		if (!Expect(PresentationSceneNeedsDispatch(desired, Workspace::Presentation,
			Workspace::Presentation, target.targetRevision, false),
			"targetless PPT retires a previous full target in the same workspace")) ++failures;
		desired.presentationTarget = std::make_shared<PresentationTarget>(target);
		if (!Expect(PresentationSceneNeedsDispatch(desired, Workspace::Presentation,
			Workspace::Presentation, target.targetRevision - 1, false),
			"full PPT target revision still controls page dispatch")) ++failures;
		if (!Expect(PresentationSceneNeedsDispatch(desired, Workspace::Presentation,
			Workspace::Whiteboard, target.targetRevision, false),
			"full PPT re-entry retires a workspace request even when the target revision matches")) ++failures;

		if (!Expect(PresentationSceneNeedsDispatch(desired, Workspace::Presentation,
			Workspace::Presentation, target.targetRevision, true),
			"restoreLatest forces dispatch even when both workspace and target match")) ++failures;

		StateBridge bridge;
		if (!Expect(bridge.PublishPresentationTarget(target).has_value(),
			"FIFO fixture accepts PPT target")) ++failures;
		const auto accepted = bridge.Snapshot();
		bridge.Publish(CommandType::Clear);
		bridge.Publish(CommandType::Undo);
		bridge.PublishWorkspace(Workspace::Desktop);
		Command command;
		for (const auto type : { CommandType::Clear, CommandType::Undo })
		{
			if (!Expect(bridge.TryConsume(command) && command.type == type &&
				command.workspace == Workspace::Presentation &&
				command.presentationTarget == accepted.presentationTarget,
				"accepted business keeps captured PPT scene and FIFO through desktop return")) ++failures;
			if (!Expect(command.presentationTarget &&
				PresentationSceneNeedsDispatch(bridge.Snapshot(), Workspace::Desktop,
					command.workspace, command.presentationTarget->targetRevision, true),
				"restoreLatest follows each old business prefix despite the unchanged presented desktop")) ++failures;
		}
		if (!Expect(!PresentationSceneNeedsDispatch(bridge.Snapshot(), Workspace::Desktop,
			Workspace::Desktop, 0, false), "host lifecycle reset starts with a settled desktop request")) ++failures;
	}

	void TestDrawpadPresentationPlan(int& failures)
	{
		using namespace Inkeys::Drawing::Draw3;
		ProductState desired{};
		desired.workspace = Workspace::Desktop;
		desired.selectionMode = true;
		HostRuntimeSnapshot ready{};
		ready.firstFrameReady = true;
		ready.workspace = desired.workspace;
		ready.selectionMode = true;
		ready.requestedOutputTarget = ready.readyOutputTarget = HostOutputTarget::SelectionUlw;
		ready.requestedOutputRevision = ready.readyOutputRevision = 3;
		ready.contentRevision = ready.presentedContentRevision = 8;
		if (!Expect(DrawpadPresentationReady(desired, ready),
			"desktop retained ink selection accepts matching output")) ++failures;
		const auto reject = [&](HostRuntimeSnapshot candidate, const char* reason)
		{
			if (!Expect(!DrawpadPresentationReady(desired, candidate), reason)) ++failures;
		};
		auto pending = ready; pending.firstFrameReady = false;
		reject(pending, "initial frame pending retains previous surface");
		pending = ready; pending.workspace = Workspace::Presentation;
		reject(pending, "PPT exit cannot commit the old workspace frame");
		pending = ready; pending.selectionMode = false;
		reject(pending, "PPT pen frame cannot satisfy desktop selection");
		pending = ready; pending.requestedOutputTarget = HostOutputTarget::PrimaryDrawpad;
		reject(pending, "pending auxiliary target retains previous surface");
		pending = ready; pending.readyOutputTarget = HostOutputTarget::PrimaryDrawpad;
		reject(pending, "primary ready cannot satisfy auxiliary output");
		pending = ready; --pending.readyOutputRevision;
		reject(pending, "previous output generation cannot commit");
		pending = ready; --pending.presentedContentRevision;
		reject(pending, "same content bool with stale ink revision cannot commit");
		pending = ready; pending.commandScenePending = true;
		reject(pending, "command replay cannot commit an intermediate scene");
		desired.selectionMode = false;
		if (!Expect(!DrawpadPresentationReady(desired, ready),
			"selection frame cannot satisfy a newer pen intent")) ++failures;
		ready.selectionMode = false;
		ready.requestedOutputTarget = ready.readyOutputTarget = HostOutputTarget::PrimaryDrawpad;
		if (!Expect(DrawpadPresentationReady(desired, ready),
			"pen handoff accepts the preheated primary frame")) ++failures;
		pending = ready; --pending.presentedContentRevision;
		reject(pending, "pen handoff also waits for matching content revision");
		desired.workspace = Workspace::Presentation;
		ready.workspace = Workspace::Presentation;
		if (!Expect(!DrawpadPresentationReady(desired, ready),
			"PPT requires a trusted target identity")) ++failures;
		PresentationTarget target{};
		target.sessionRevision = 5; target.targetRevision = 10;
		desired.presentationTarget = std::make_shared<const PresentationTarget>(target);
		ready.presentationReady = ReadyIdentityFor(target);
		if (!Expect(DrawpadPresentationReady(desired, ready),
			"matching PPT target frame is eligible for handoff")) ++failures;
		++target.targetRevision;
		desired.presentationTarget = std::make_shared<const PresentationTarget>(target);
		if (!Expect(!DrawpadPresentationReady(desired, ready),
			"new PPT target rejects old same-mode ready identity")) ++failures;

		if (!Expect(SelectionUsesAuxiliaryOutput(true, Workspace::Desktop)
			&& SelectionUsesAuxiliaryOutput(true, Workspace::Presentation)
			&& !SelectionUsesAuxiliaryOutput(true, Workspace::Whiteboard)
			&& !SelectionUsesAuxiliaryOutput(false, Workspace::Whiteboard),
			"whiteboard selection stays on the primary drawpad")) ++failures;
		if (!Expect(ResolveDrawpadPresentationSurface(true, false, true) ==
			DrawpadPresentationSurface::Hidden,
			"clean empty selection hides both drawpad surfaces")) ++failures;
		if (!Expect(ResolveDrawpadPresentationSurface(true, false, false) ==
			DrawpadPresentationSurface::Presentation,
			"empty selection waits for full-frame clean")) ++failures;
		if (!Expect(ResolveDrawpadPresentationSurface(true, true, false) ==
			DrawpadPresentationSurface::Presentation,
			"content selection uses the auxiliary surface")) ++failures;
		if (!Expect(ResolveDrawpadPresentationSurface(false, false, true) ==
			DrawpadPresentationSurface::Primary,
			"drawing mode uses the primary surface")) ++failures;
	}

	void TestPageRuntimeRevisionPolicy(int& failures)
	{
		using Inkeys::Drawing::Draw3::Detail::HostRuntimeRevisionSignal;
		HostRuntimeRevisionSignal signal;
		std::atomic_bool running = true;
		const std::uint64_t initialRevision = signal.Revision();
		if (!Expect(!signal.PublishPageChange(2, 8, 2, 8)
			&& signal.Revision() == initialRevision
			&& !signal.WaitForChange(initialRevision, 1, running),
			"stable page runtime neither publishes nor wakes")) ++failures;

		std::atomic_bool pageWaitCompleted = false;
		std::jthread pageWaiter([&]
			{
				pageWaitCompleted.store(signal.WaitForChange(
					initialRevision, 1000, running), std::memory_order_release);
			});
		const bool pagePublished = signal.PublishPageChange(2, 8, 3, 8);
		pageWaiter.join();
		const std::uint64_t pageRevision = signal.Revision();
		if (!Expect(pagePublished && pageRevision != initialRevision
			&& pageWaitCompleted.load(std::memory_order_acquire),
			"page index change publishes revision and wakes waiter")) ++failures;

		std::atomic_bool countWaitCompleted = false;
		std::jthread countWaiter([&]
			{
				countWaitCompleted.store(signal.WaitForChange(
					pageRevision, 1000, running), std::memory_order_release);
			});
		const bool countPublished = signal.PublishPageChange(3, 8, 3, 9);
		countWaiter.join();
		if (!Expect(countPublished && signal.Revision() != pageRevision
			&& countWaitCompleted.load(std::memory_order_acquire),
			"page count change publishes revision and wakes waiter")) ++failures;

		bool repeatedWakePrompt = true;
		for (int iteration = 0; iteration < 64 && repeatedWakePrompt; ++iteration)
		{
			const std::uint64_t revision = signal.Revision();
			std::atomic_bool waitEntered = false;
			std::atomic_bool waitCompleted = false;
			std::jthread waiter([&]
				{
					waitEntered.store(true, std::memory_order_release);
					waitCompleted.store(signal.WaitForChange(
						revision, 1000, running), std::memory_order_release);
				});
			while (!waitEntered.load(std::memory_order_acquire)) std::this_thread::yield();
			const auto publishedAt = std::chrono::steady_clock::now();
			signal.Publish();
			waiter.join();
			repeatedWakePrompt = waitCompleted.load(std::memory_order_acquire)
				&& std::chrono::steady_clock::now() - publishedAt
				< std::chrono::milliseconds(750);
		}
		if (!Expect(repeatedWakePrompt,
			"repeated revision handoff cannot lose the condition-variable wake")) ++failures;

		const std::uint64_t stopRevision = signal.Revision();
		std::atomic_bool stopWaitCompleted = false;
		std::jthread stopWaiter([&]
			{
				stopWaitCompleted.store(signal.WaitForChange(
					stopRevision, 1000, running), std::memory_order_release);
			});
		running.store(false, std::memory_order_release);
		signal.NotifyAll();
		stopWaiter.join();
		if (!Expect(stopWaitCompleted.load(std::memory_order_acquire),
			"host stop notification releases runtime waiter")) ++failures;
	}

	void TestPptWaitObservationBoundary(int& failures)
	{
		using Inkeys::Drawing::Draw3::Detail::HostRuntimeRevisionSignal;
		HostRuntimeRevisionSignal signal;
		std::atomic_bool running = true;
		double oldMilliseconds = 0.0, capturedMilliseconds = 0.0;
		constexpr int samples = 8;
		for (int sample = 0; sample < samples; ++sample)
		{
			const auto beforeDecision = signal.Revision();
			// 确定性安排：ready 正好发生在业务判断与开始等待之间。
			signal.Publish();
			const auto oldStart = std::chrono::steady_clock::now();
			const bool oldWoke = signal.WaitForChange(signal.Revision(), 50, running);
			const auto fixedStart = std::chrono::steady_clock::now();
			const bool capturedWoke = signal.WaitForChange(beforeDecision, 50, running);
			const auto finished = std::chrono::steady_clock::now();
			oldMilliseconds += std::chrono::duration<double, std::milli>(fixedStart - oldStart).count();
			capturedMilliseconds += std::chrono::duration<double, std::milli>(finished - fixedStart).count();
			if (!Expect(!oldWoke && capturedWoke,
				"ready between decision and wait is preserved by the captured revision")) ++failures;
		}
		std::cout << "[PptWaitBoundary] synthetic samples=" << samples
			<< " old_basis_mean_ms=" << oldMilliseconds / samples
			<< " captured_basis_mean_ms=" << capturedMilliseconds / samples << '\n';
	}

	struct DrawingActivityCalls
	{
		int started = 0;
		int ended = 0;
	};

	void RecordDrawingActivity(void* context, bool active) noexcept
	{
		auto* calls = static_cast<DrawingActivityCalls*>(context);
		if (!calls) return;
		if (active) ++calls->started;
		else ++calls->ended;
	}

	void TestAcceptedStrokeNoticeLifecycle(int& failures)
	{
		using Inkeys::Drawing::Draw3::Detail::HostAcceptedStrokeContext;
		using Inkeys::Drawing::Draw3::Detail::HostAcceptedStrokeState;
		using Inkeys::Drawing::Draw3::Detail::HostDrawingActivityState;
		HostAcceptedStrokeState state;
		HostAcceptedStrokeContext current;
		current.running = true;
		state.BeginHost();
		AcceptedStrokeBegin scene;
		scene.sceneGeneration = 1;
		state.PublishScene(scene);
		auto accepted = scene;
		accepted.kind = CompletedStrokeKind::Drawing;
		state.Stamp(accepted);
		if (!Expect(state.IsCurrent(accepted, current),
			"accepted stroke in current CPU scene is valid before any Present acknowledgement")) ++failures;

		HostDrawingActivityState activity;
		activity.Publish(true, nullptr, nullptr);
		activity.EndIfActive(nullptr, nullptr);
		if (!Expect(!activity.Active() && state.IsCurrent(accepted, current),
			"short stroke ending before UI consumes begin does not retire the notice")) ++failures;
		auto secondContact = accepted;
		secondContact.kind = CompletedStrokeKind::Eraser;
		auto shapeContact = accepted;
		shapeContact.kind = CompletedStrokeKind::Shape;
		if (!Expect(state.IsCurrent(secondContact, current) && state.IsCurrent(shapeContact, current),
			"each accepted eraser or shape in an overlapping batch retains its own kind")) ++failures;
		auto rejected = accepted;
		rejected.kind = CompletedStrokeKind::None;
		if (!Expect(!state.IsCurrent(rejected, current),
			"scene-only or rejected contact metadata cannot become a begin notice")) ++failures;

		scene.workspace = Workspace::Whiteboard;
		++scene.sceneGeneration;
		state.PublishScene(scene);
		scene.workspace = Workspace::Desktop;
		++scene.sceneGeneration;
		state.PublishScene(scene);
		if (!Expect(!state.IsCurrent(accepted, current),
			"leaving and returning to the same workspace/page does not revive old begin")) ++failures;
		accepted = scene;
		accepted.kind = CompletedStrokeKind::Drawing;
		state.Stamp(accepted);
		++scene.sceneGeneration;
		state.PublishScene(scene);
		if (!Expect(!state.IsCurrent(accepted, current),
			"Clear or recovery retires old notice even with identical page coordinates")) ++failures;
		accepted = scene;
		accepted.kind = CompletedStrokeKind::Drawing;
		state.Stamp(accepted);
		current.desiredWorkspace = Workspace::Whiteboard;
		if (!Expect(!state.IsCurrent(accepted, current),
			"new desired workspace rejects old begin before CPU switch completes")) ++failures;
		current.desiredWorkspace = Workspace::Desktop;
		current.commandScenePending = true;
		if (!Expect(!state.IsCurrent(accepted, current),
			"pending scene command cannot fold the old scene")) ++failures;
		current.commandScenePending = false;
		current.running = false;
		if (!Expect(!state.IsCurrent(accepted, current), "stopped Host rejects begin")) ++failures;
		current.running = true;
		state.BeginHost();
		state.PublishScene(scene);
		if (!Expect(!state.IsCurrent(accepted, current),
			"Host restart rejects old begin even when scene generation is reused")) ++failures;

		scene.workspace = Workspace::Presentation;
		scene.pageIndex = 2;
		++scene.sceneGeneration;
		PresentationReadyIdentity presentation;
		presentation.key.bytes[0] = 3;
		presentation.pageIndex = 2;
		presentation.sessionRevision = 7;
		presentation.targetRevision = 9;
		scene.presentation = presentation;
		state.PublishScene(scene);
		accepted = scene;
		accepted.kind = CompletedStrokeKind::Eraser;
		state.Stamp(accepted);
		current.desiredWorkspace = Workspace::Presentation;
		current.desiredPresentation = presentation;
		if (!Expect(state.IsCurrent(accepted, current), "matching PPT begin is valid")) ++failures;
		current.admissionBlocked = true;
		if (!Expect(!state.IsCurrent(accepted, current), "suspended PPT rejects pending begin")) ++failures;
		current.admissionBlocked = false;
		++current.desiredPresentation->sessionRevision;
		if (!Expect(!state.IsCurrent(accepted, current),
			"same PPT page in a reopened show rejects old begin")) ++failures;
		current.desiredPresentation = presentation;
		++current.desiredPresentation->targetRevision;
		if (!Expect(!state.IsCurrent(accepted, current),
			"new PPT target rejects old begin before scene switch")) ++failures;
		current.desiredPresentation.reset();
		if (!Expect(!state.IsCurrent(accepted, current), "unknown PPT target rejects begin")) ++failures;
		current.desiredPresentation = presentation;
		scene.pageIndex = 3;
		state.PublishScene(scene);
		if (!Expect(!state.IsCurrent(accepted, current),
			"logical current page is checked independently of presentation target")) ++failures;
	}

	void TestDrawingActivityLifecycle(int& failures)
	{
		using Inkeys::Drawing::Draw3::Detail::HostDrawingActivityState;
		HostDrawingActivityState state;
		DrawingActivityCalls calls;
		if (!Expect(!state.Publish(false, &calls, &RecordDrawingActivity)
			&& state.Publish(true, &calls, &RecordDrawingActivity)
			&& !state.Publish(true, &calls, &RecordDrawingActivity)
			&& state.Publish(false, &calls, &RecordDrawingActivity)
			&& calls.started == 1 && calls.ended == 1 && !state.Active(),
			"false-to-true-to-true-to-false publishes one start and end")) ++failures;

		state.Publish(true, &calls, &RecordDrawingActivity);
		if (!Expect(state.EndIfActive(&calls, &RecordDrawingActivity)
			&& !state.EndIfActive(&calls, &RecordDrawingActivity)
			&& calls.started == 2 && calls.ended == 2 && !state.Active(),
			"host stop or exception completes one active drawing interval")) ++failures;
	}
	void TestConditionalShortcutCommands(int& failures)
	{
		namespace Shortcut = Inkeys::Drawing::Draw3::Shortcuts;
		StateBridge bridge;
		Shortcut::State state;
		state.BeginHost();
		Shortcut::Context actual;
		actual.scope.pageEpoch = 1;
		actual.contentRevision = 5;
		actual.available = true;
		actual.hasContent = true;
		state.Publish(actual, bridge.Snapshot().shortcutIntentEpoch);
		auto source = state.Snapshot();
		std::array<std::uint64_t, 3> ids{};
		const std::array commands{ CommandType::Clear, CommandType::Undo, CommandType::Redo };
		for (std::size_t i = 0; i != commands.size(); ++i)
		{
			ids[i] = state.Reserve(source.scope);
			auto request = std::make_shared<Shortcut::Request>(Shortcut::Request{ ids[i], source.scope, 0 });
			if (!Expect(ids[i] && bridge.PublishShortcut(commands[i], request) == CommandResult::Accepted,
				"shortcut Clear/Undo/Redo reserve exact completion before same-page enqueue")) ++failures;
		}
		Command command;
		for (std::size_t i = 0; i != commands.size(); ++i)
		{
			if (!Expect(bridge.TryConsume(command) && command.type == commands[i] && command.shortcut &&
				command.shortcut->requestId == ids[i] &&
				state.Authorize(*command.shortcut, actual) == Shortcut::Result::Applied,
				"production Controller admission keeps same-page FIFO across Clear content revisions")) ++failures;
			actual.contentRevision++;
			actual.hasContent = i == 1;
			state.Publish(actual, source.scope.intentEpoch);
			state.Complete(ids[i], Shortcut::Result::Applied);
			Shortcut::Completion result;
			if (!Expect(state.Take(ids[i], result) && result.source.scope == source.scope &&
				result.resultContext.contentRevision == actual.contentRevision &&
				!state.Take(ids[i], result), "named completion contains its exact post-mutation context once")) ++failures;
		}

		const auto oldScope = state.Snapshot().scope;
		const auto oldId = state.Reserve(oldScope);
		Shortcut::Request old{ oldId, oldScope, 0 };
		actual.scope.pageEpoch = 2;
		state.Publish(actual, oldScope.intentEpoch);
		actual.scope.pageEpoch = 3; // A→B→A，页号可以相同，epoch 不复用。
		state.Publish(actual, oldScope.intentEpoch);
		if (!Expect(state.Authorize(old, actual) == Shortcut::Result::Stale,
			"actual Controller page ABA rejects the delayed shortcut")) ++failures;
		state.Retire(oldId);
		const auto fresh = state.Snapshot();
		const auto lease = state.Acquire(fresh, true);
		const auto ownedId = state.Reserve(fresh.scope);
		Shortcut::Request owned{ ownedId, fresh.scope, lease };
		if (!Expect(lease && state.Authorize(owned, actual) == Shortcut::Result::Applied,
			"compound transaction permits only its own lease-stamped Clear")) ++failures;
		for (const auto command : { CommandType::Clear, CommandType::Undo, CommandType::Redo })
			if (!Expect(!state.OrdinaryCommandAllowed(command),
				"compound lease excludes ordinary mouse content edits until selection commit")) ++failures;
		if (!Expect(state.OrdinaryCommandAllowed(CommandType::NextPage) &&
			state.OrdinaryCommandAllowed(CommandType::Save),
			"compound lease does not rewrite unrelated existing command admission")) ++failures;
		owned.expectedContentRevision = actual.contentRevision;
		actual.hasContent = false;
		if (!Expect(Shortcut::ResolveFence(owned, actual) == Shortcut::Result::NoContent,
			"production fence accepts exact named empty content without a second Clear")) ++failures;
		++actual.contentRevision;
		if (!Expect(Shortcut::ResolveFence(owned, actual) == Shortcut::Result::Stale,
			"an admitted contact finalized before fence invalidates second-Delete empty proof")) ++failures;
		--actual.contentRevision;
		owned.leaseId = 0;
		if (!Expect(state.Authorize(owned, actual) == Shortcut::Result::Blocked &&
			!state.Release(lease + 1) && state.Lease() == lease && state.Release(lease),
			"foreign command and lease release cannot open another transaction gate")) ++failures;
		for (const auto command : { CommandType::Clear, CommandType::Undo, CommandType::Redo })
			if (!Expect(state.OrdinaryCommandAllowed(command),
				"own lease release immediately restores ordinary content commands")) ++failures;
		state.Retire(ownedId);
		actual.contentRevision++;
		state.Publish(actual, fresh.scope.intentEpoch);
		if (!Expect(!state.Acquire(fresh, true),
			"new ink or Undo after first Delete rejects stale second-Delete empty proof")) ++failures;
		const auto beforeRestart = state.Snapshot();
		state.BeginHost();
		state.Publish(actual, fresh.scope.intentEpoch);
		if (!Expect(!state.Reserve(beforeRestart.scope), "Host restart retires all prior shortcut scopes")) ++failures;

		auto request = std::make_shared<Shortcut::Request>();
		request->requestId = 1;
		request->scope.workspace = Workspace::Desktop;
		request->scope.intentEpoch = bridge.Snapshot().shortcutIntentEpoch;
		auto tool = bridge.Snapshot();
		tool.tool = Tool::HardPen;
		bridge.PublishState(tool);
		if (!Expect(bridge.PublishShortcut(CommandType::Clear, request) == CommandResult::Accepted,
			"tool publication does not replace stable page identity")) ++failures;
		bridge.TryConsume(command);
		bridge.PublishWorkspace(Workspace::Whiteboard);
		bridge.PublishWorkspace(Workspace::Desktop);
		if (!Expect(bridge.PublishShortcut(CommandType::Clear, request) != CommandResult::Accepted,
			"Bridge desired A→B→A rejects old event even if renderer coalesces B")) ++failures;

		PresentationTarget target;
		target.key.bytes[0] = 7;
		target.sourceIdentity = "shortcut-probe";
		target.totalPages = 1;
		target.sessionRevision = 1;
		bridge.PublishPresentationTarget(target);
		request->scope.workspace = Workspace::Presentation;
		request->scope.intentEpoch = bridge.Snapshot().shortcutIntentEpoch;
		request->scope.presentation = ReadyIdentityFor(*bridge.Snapshot().presentationTarget);
		++target.sessionRevision;
		bridge.PublishPresentationTarget(target);
		if (!Expect(bridge.PublishShortcut(CommandType::Undo, request) != CommandResult::Accepted,
			"same page in another PPT show cannot execute old shortcut")) ++failures;
	}

	void TestWhiteboardShortcutCommandOrder(int& failures)
	{
		namespace Shortcut = Inkeys::Drawing::Draw3::Shortcuts;
		using Inkeys::Drawing::Draw3::WhiteboardPattern;
		using Inkeys::Drawing::Draw3::WhiteboardTheme;
		StateBridge bridge(3);
		bridge.PublishWorkspace(Workspace::Whiteboard);
		auto request = std::make_shared<Shortcut::Request>();
		request->requestId = 31;
		request->scope.workspace = Workspace::Whiteboard;
		request->scope.pageIndex = 2;
		request->scope.pageEpoch = 7;
		request->scope.intentEpoch = bridge.Snapshot().shortcutIntentEpoch;
		const WhiteboardTheme theme{0x172A46u, WhiteboardPattern::Grid};
		// 两个分支共用命令队列：主题身份不能覆盖快捷键身份，也不能退休页面意图。
		if (!Expect(bridge.PublishWhiteboardTheme(theme, 9, 2) == CommandResult::Accepted &&
			bridge.PublishShortcut(CommandType::Clear, request) == CommandResult::Accepted &&
			bridge.Snapshot().shortcutIntentEpoch == request->scope.intentEpoch &&
			bridge.StopWithFinalCommand(CommandType::PrepareExitAutoSave),
			"whiteboard theme and conditional Clear share the queue before the final save barrier")) ++failures;
		Command command;
		if (!Expect(bridge.TryConsume(command) && command.type == CommandType::SetWhiteboardTheme &&
			command.workspace == Workspace::Whiteboard && command.whiteboardTheme == theme &&
			command.whiteboardGeneration == 9 && command.whiteboardPage == 2 && !command.shortcut,
			"whiteboard theme retains its own generation and page in FIFO")) ++failures;
		if (!Expect(bridge.TryConsume(command) && command.type == CommandType::Clear &&
			command.shortcut == request && command.shortcut->scope.pageEpoch == 7 &&
			command.whiteboardGeneration == 0,
			"conditional whiteboard Clear retains its request independently of theme payload")) ++failures;
		if (!Expect(bridge.TryConsume(command) && command.type == CommandType::PrepareExitAutoSave &&
			command.workspace == Workspace::Whiteboard && !command.shortcut && !bridge.TryConsume(command),
			"whiteboard final save barrier follows both accepted branch commands exactly once")) ++failures;

		bridge.Reset();
		bridge.PublishWorkspace(Workspace::Whiteboard);
		request->scope.intentEpoch = bridge.Snapshot().shortcutIntentEpoch;
		bridge.PublishWorkspace(Workspace::Desktop);
		bridge.PublishWorkspace(Workspace::Whiteboard);
		if (!Expect(bridge.PublishShortcut(CommandType::Clear, request) == CommandResult::NotRunning,
			"closing and reopening whiteboard rejects the previous shortcut intent")) ++failures;
	}

	void TestShortcutBoundedCompletions(int& failures)
	{
		namespace Shortcut = Inkeys::Drawing::Draw3::Shortcuts;
		Shortcut::State state;
		state.BeginHost();
		Shortcut::Context scene;
		scene.available = true;
		scene.scope.pageEpoch = 1;
		state.Publish(scene, 1);
		const auto context = state.Snapshot();
		std::array<std::uint64_t, 64> ids{};
		for (auto& id : ids) id = state.Reserve(context.scope);
		if (!Expect(ids.front() && ids.back() && !state.Reserve(context.scope),
			"completion capacity is reserved and bounded before queue acceptance")) ++failures;
		state.Retire(ids.front());
		const auto replacement = state.Reserve(context.scope);
		state.Complete(ids.front(), Shortcut::Result::Applied);
		Shortcut::Completion completion;
		if (!Expect(replacement && replacement != ids.front() && !state.Take(replacement, completion),
			"retired late callback cannot complete reused slot")) ++failures;
		state.Complete(replacement, Shortcut::Result::Failed);
		state.Complete(replacement, Shortcut::Result::Applied);
		if (!Expect(state.Take(replacement, completion) && completion.result == Shortcut::Result::Failed,
			"real failure is exact and duplicate completion cannot turn it into success")) ++failures;
		state.Complete(ids[1], Shortcut::Result::NoContent);
		if (!Expect(state.Take(ids[1], completion) && completion.result == Shortcut::Result::NoContent,
			"empty Clear proof is distinct from a failed or blocked Clear")) ++failures;
		state.Stop();
		if (!Expect(!state.Take(ids[2], completion) && !state.Reserve(context.scope),
			"Stop retires pending completions and prevents new commands")) ++failures;
	}

}

int RunDraw3BridgeTests()
{
	int failures = 0;
	TestProductState(failures);
	TestConditionalShortcutCommands(failures);
	TestWhiteboardShortcutCommandOrder(failures);
	TestShortcutBoundedCompletions(failures);
	TestMouseCursorToolProjection(failures);
	TestPresentationSessionIdentity(failures);
	TestCommandQueue(failures);
	TestUnsupportedAndLifecycle(failures);
	TestEraserModeMapping(failures);
	TestTimerPeriodController(failures);
	TestDrawpadPresentationPlan(failures);
	TestSceneReadyHandoff(failures);
	TestSceneDispatchResponsibility(failures);
	TestPageRuntimeRevisionPolicy(failures);
	TestDrawingActivityLifecycle(failures);
	TestAcceptedStrokeNoticeLifecycle(failures);
	TestPptWaitObservationBoundary(failures);
	return failures;
}
