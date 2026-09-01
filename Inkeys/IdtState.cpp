#include "IdtState.h"

#include "IdtConfiguration.h"
#include "IdtDraw.h"
#include "IdtFreezeFrame.h"
#include "IdtPlug-in.h"
#include "Inkeys/Business/LegacyDrawState.hpp"
#include "Inkeys/Business/PenToolState.hpp"
#include "Inkeys/Drawing/Draw3/Draw3.Product.h"
#include "Inkeys/Drawing/Draw3/Draw3.PresentationState.h"
#include "Inkeys/Window/Window.Legacy.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>

import Inkeys.UI.Bar;
import Inkeys.UI.Ppt;
import Inkeys.UI.Whiteboard;
import Inkeys.Window;

StateModeClass stateMode;

namespace
{
	using Inkeys::Drawing::Draw3::Bridge::ProductState;
	using Inkeys::Drawing::Draw3::Bridge::Tool;
	using Inkeys::Drawing::Draw3::Bridge::Workspace;
	std::mutex draw3PresentationMutex;
	std::atomic_bool draw3PresentationRetryPending = false;
	bool draw3PresentationFailureActive = false;

	Workspace CurrentPrimaryWorkspace() noexcept
	{
		return PptInfoState.TotalPage > 0
			? Workspace::Presentation : Workspace::Desktop;
	}

	enum class Draw3PresentationReconcileResult : std::uint8_t
	{
		Waiting,
		Applied,
		Retry,
	};

	enum class WhiteboardPhase : std::uint8_t
	{
		Inactive,
		Entering,
		Active,
		Exiting,
	};

	std::atomic_bool whiteboardDesired = false;
	std::atomic_bool whiteboardPreviousRequested = false;
	std::atomic_bool whiteboardNextRequested = false;
	std::atomic<WhiteboardPhase> whiteboardPhase = WhiteboardPhase::Inactive;

	std::uint32_t ColorRefToRgba(COLORREF color) noexcept
	{
		return (static_cast<std::uint32_t>(GetRValue(color)) << 24) |
			(static_cast<std::uint32_t>(GetGValue(color)) << 16) |
			(static_cast<std::uint32_t>(GetBValue(color)) << 8) | 0xffu;
	}

	Tool CurrentDraw3Tool() noexcept
	{
		if (IsLaserToolActive()) return Tool::Laser;
		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtEraser)
		{
			return Inkeys::Drawing::Draw3::Bridge::NormalizeLegacyEraserMode(
				setlist.eraserSetting.eraserMode) == 1
				? Tool::SpeedEraser
				: Tool::FixedEraser;
		}

		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtShape)
		{
			switch (stateMode.Shape.ModeSelect)
			{
			case ShapeModeSelectEnum::IdtShapeStraightLine1:
				return Tool::SolidLine;
			case ShapeModeSelectEnum::IdtShapeDashedLine1:
				return Tool::DashedLine;
			case ShapeModeSelectEnum::IdtShapeRectangle1:
				return Tool::OutlineRectangle;
			default:
				return Tool::SolidLine;
			}
		}

		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen &&
			stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1)
			return Tool::Highlighter;
		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen &&
			stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHardPen)
			return Tool::HardPen;
		return Tool::Pen;
	}

	void PublishDraw3State() noexcept
	{
		ProductState state{};
		state.tool = CurrentDraw3Tool();
		state.widthDip = (std::max)(0.1f, GetPenWidth());
		state.colorRgba = ColorRefToRgba(GetPenColor());
		state.selectionMode =
			stateMode.StateModeSelect == StateModeSelectEnum::IdtSelection;
		state.autoSaveEnabled = setlist.saveSetting.enable;
		Inkeys::Drawing::Draw3::PublishProductState(state);
	}

	[[nodiscard]] bool Draw3WorkspaceReady(
		const Inkeys::Drawing::Draw3::HostRuntimeSnapshot& runtime,
		Workspace workspace) noexcept
	{
		if (!runtime.firstFrameReady || runtime.workspace != workspace)
			return false;
		const auto expectedTarget =
			Inkeys::Drawing::Draw3::Bridge::SelectionUsesAuxiliaryOutput(
				runtime.selectionMode, workspace)
			? Inkeys::Drawing::Draw3::HostOutputTarget::SelectionUlw
			: Inkeys::Drawing::Draw3::HostOutputTarget::PrimaryDrawpad;
		if (workspace != Workspace::Whiteboard && runtime.selectionMode &&
			!runtime.currentPageHasContent && !runtime.auxiliaryFullFrameClean)
			return false;
		return runtime.requestedOutputTarget == expectedTarget &&
			runtime.readyOutputTarget == expectedTarget &&
			runtime.readyOutputRevision == runtime.requestedOutputRevision &&
			runtime.presentedContentRevision == runtime.contentRevision;
	}

	[[nodiscard]] const char* OutputTargetName(
		Inkeys::Drawing::Draw3::HostOutputTarget target) noexcept
	{
		return target == Inkeys::Drawing::Draw3::HostOutputTarget::SelectionUlw
			? "selection-ulw" : "primary-drawpad";
	}

	[[nodiscard]] const char* SurfaceVisibilityName(
		Inkeys::Window::DrawpadSurfaceVisibility visibility) noexcept
	{
		switch (visibility)
		{
		case Inkeys::Window::DrawpadSurfaceVisibility::Primary:
			return "primary";
		case Inkeys::Window::DrawpadSurfaceVisibility::Presentation:
			return "presentation";
		case Inkeys::Window::DrawpadSurfaceVisibility::Hidden:
		default:
			return "hidden";
		}
	}

	void LogDraw3PresentationFailure(
		const Inkeys::Drawing::Draw3::HostRuntimeSnapshot& runtime,
		Inkeys::Window::DrawpadSurfaceVisibility visibility) noexcept
	{
		if (!IDTLogger) return;
		auto& service = Inkeys::Window::GetService();
		const HWND primary = service.Handle(Inkeys::Window::WindowRole::Drawpad);
		const HWND presentation = service.Handle(
			Inkeys::Window::WindowRole::DrawpadPresentation);
		RECT primaryBounds{};
		RECT presentationBounds{};
		if (primary) (void)GetWindowRect(primary, &primaryBounds);
		if (presentation) (void)GetWindowRect(presentation, &presentationBounds);
		const HWND primaryOwner = primary ? GetWindow(primary, GW_OWNER) : nullptr;
		const HWND presentationOwner = presentation
			? GetWindow(presentation, GW_OWNER) : nullptr;
		const LONG_PTR primaryExStyle = primary
			? GetWindowLongPtrW(primary, GWL_EXSTYLE) : 0;
		const LONG_PTR presentationExStyle = presentation
			? GetWindowLongPtrW(presentation, GWL_EXSTYLE) : 0;
		IDTLogger->warn(
			"[状态线程][ReconcileDraw3Presentation] surface 切换失败, "
			"surface={}, requested={}@{}, ready={}@{}, content={}/{}, "
			"firstFrame={}, clean={}, primary={{hwnd=0x{:X},owner=0x{:X},visible={},topmost={},rect=({},{},{},{})}}, "
			"presentation={{hwnd=0x{:X},owner=0x{:X},visible={},topmost={},rect=({},{},{},{})}}",
			SurfaceVisibilityName(visibility),
			OutputTargetName(runtime.requestedOutputTarget),
			runtime.requestedOutputRevision,
			OutputTargetName(runtime.readyOutputTarget),
			runtime.readyOutputRevision,
			runtime.presentedContentRevision, runtime.contentRevision,
			runtime.firstFrameReady, runtime.auxiliaryFullFrameClean,
			static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(primary)),
			static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(primaryOwner)),
			primary && IsWindowVisible(primary),
			(primaryExStyle & WS_EX_TOPMOST) != 0,
			primaryBounds.left, primaryBounds.top,
			primaryBounds.right, primaryBounds.bottom,
			static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(presentation)),
			static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(presentationOwner)),
			presentation && IsWindowVisible(presentation),
			(presentationExStyle & WS_EX_TOPMOST) != 0,
			presentationBounds.left, presentationBounds.top,
			presentationBounds.right, presentationBounds.bottom);
	}

	[[nodiscard]] Draw3PresentationReconcileResult
		ReconcileDraw3PresentationState()
	{
		std::scoped_lock lock(draw3PresentationMutex);
		const auto runtime = Inkeys::Drawing::Draw3::ProductRuntimeSnapshot();
		const bool selectionMode = runtime.selectionMode;
		const bool whiteboard = runtime.workspace ==
			Inkeys::Drawing::Draw3::Bridge::Workspace::Whiteboard;
		auto& service = Inkeys::Window::GetService();
		using Inkeys::Window::WindowRole;

		// 白板空页仍保留完整绘制栏和主 Drawpad；选择只表示拖动态，不切辅助 ULW。
		Inkeys::UI::Bar::SetCurrentPageHasContent(
			whiteboard || runtime.currentPageHasContent);
		const auto expectedTarget =
			Inkeys::Drawing::Draw3::Bridge::SelectionUsesAuxiliaryOutput(
				selectionMode, runtime.workspace)
			? Inkeys::Drawing::Draw3::HostOutputTarget::SelectionUlw
			: Inkeys::Drawing::Draw3::HostOutputTarget::PrimaryDrawpad;
		const bool targetReady = runtime.requestedOutputTarget == expectedTarget &&
			runtime.readyOutputTarget == expectedTarget &&
			runtime.readyOutputRevision == runtime.requestedOutputRevision &&
			runtime.presentedContentRevision == runtime.contentRevision;
		if (!runtime.firstFrameReady ||
			(selectionMode && !whiteboard && !targetReady))
		{
			draw3PresentationRetryPending.store(false, std::memory_order_release);
			draw3PresentationFailureActive = false;
			return Draw3PresentationReconcileResult::Waiting;
		}

		Inkeys::Window::DrawpadSurfaceVisibility visibility;
		if (whiteboard)
		{
			visibility = Inkeys::Window::DrawpadSurfaceVisibility::Primary;
		}
		else switch (Inkeys::Drawing::Draw3::ResolveDrawpadPresentationSurface(
			selectionMode, runtime.currentPageHasContent,
			runtime.auxiliaryFullFrameClean))
		{
		case Inkeys::Drawing::Draw3::DrawpadPresentationSurface::Primary:
			visibility = Inkeys::Window::DrawpadSurfaceVisibility::Primary; break;
		case Inkeys::Drawing::Draw3::DrawpadPresentationSurface::Presentation:
			visibility = Inkeys::Window::DrawpadSurfaceVisibility::Presentation; break;
		case Inkeys::Drawing::Draw3::DrawpadPresentationSurface::Hidden:
		default:
			visibility = Inkeys::Window::DrawpadSurfaceVisibility::Hidden; break;
		}

		if (!service.SetDrawpadSurfaceVisibility(visibility))
		{
			draw3PresentationRetryPending.store(true, std::memory_order_release);
			if (!draw3PresentationFailureActive)
				LogDraw3PresentationFailure(runtime, visibility);
			draw3PresentationFailureActive = true;
			return Draw3PresentationReconcileResult::Retry;
		}

		draw3PresentationRetryPending.store(false, std::memory_order_release);
		if (draw3PresentationFailureActive && IDTLogger)
			IDTLogger->info(
				"[状态线程][ReconcileDraw3Presentation] surface 切换已恢复, surface={}",
				SurfaceVisibilityName(visibility));
		draw3PresentationFailureActive = false;
		const HWND drawpad = service.Handle(WindowRole::Drawpad);
		const HWND presentation = service.Handle(WindowRole::DrawpadPresentation);
		IdtWindowsIsVisible.drawpadWindow = drawpad && presentation &&
			service.Ready(WindowRole::Drawpad) &&
			service.Ready(WindowRole::DrawpadPresentation) &&
			runtime.firstFrameReady;
		return Draw3PresentationReconcileResult::Applied;
	}
}

void SyncDraw3State()
{
	PublishDraw3State();
	ReconcileDraw3Presentation();
}

void ReconcileDraw3Presentation()
{
	(void)ReconcileDraw3PresentationState();
}

bool IsLaserPenSelected() noexcept
{
	return stateMode.laserActive;
}

bool IsLaserToolActive() noexcept
{
	return Inkeys::Business::IsLaserToolActive(
		stateMode.StateModeSelect == StateModeSelectEnum::IdtPen,
		IsLaserPenSelected());
}

void RequestWhiteboardActive(bool active) noexcept
{
	whiteboardDesired.store(active, std::memory_order_release);
	if (!active)
	{
		whiteboardPreviousRequested.store(false, std::memory_order_release);
		whiteboardNextRequested.store(false, std::memory_order_release);
	}
}

bool WhiteboardRequested() noexcept
{
	return whiteboardDesired.load(std::memory_order_acquire);
}

bool WhiteboardActive() noexcept
{
	return whiteboardPhase.load(std::memory_order_acquire) ==
		WhiteboardPhase::Active;
}

bool WhiteboardTransactionActive() noexcept
{
	return whiteboardDesired.load(std::memory_order_acquire) ||
		whiteboardPhase.load(std::memory_order_acquire) !=
		WhiteboardPhase::Inactive;
}

void RequestWhiteboardPreviousPage() noexcept
{
	if (WhiteboardActive())
		whiteboardPreviousRequested.store(true, std::memory_order_release);
}

void RequestWhiteboardNextPage() noexcept
{
	if (WhiteboardActive())
		whiteboardNextRequested.store(true, std::memory_order_release);
}

bool SetPenWidth(float targetWidth, bool setMemory)
{
	if (targetWidth <= 0.0f) return false;
	// 仅活动 Laser 使用独立粗细；其他顶层工具不能被笔型记忆覆盖。
	if (IsLaserToolActive())
		stateMode.Pen.Laser.width = targetWidth;
	else if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen)
	{
		if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenSoftPen ||
			stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHardPen)
			stateMode.Pen.Brush1.width = targetWidth;
		else if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1)
			stateMode.Pen.Highlighter1.width = targetWidth;
		else
			return false;
	}
	else if (stateMode.StateModeSelect == StateModeSelectEnum::IdtShape)
	{
		if (stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeStraightLine1 ||
			stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeDashedLine1)
			stateMode.Shape.StraightLine1.width = targetWidth;
		else if (stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeRectangle1)
			stateMode.Shape.Rectangle1.width = targetWidth;
		else
			return false;
	}
	else
		return false;

	if (setMemory) SetMemory();
	PublishDraw3State();
	return true;
}

bool SetPenColor(COLORREF targetColor, bool setMemory)
{
	if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen)
	{
		if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenSoftPen ||
			stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHardPen)
			stateMode.Pen.Brush1.color = targetColor;
		else if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1)
			stateMode.Pen.Highlighter1.color = targetColor;
		else
			return false;
	}
	else if (stateMode.StateModeSelect == StateModeSelectEnum::IdtShape)
	{
		if (stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeStraightLine1 ||
			stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeDashedLine1)
			stateMode.Shape.StraightLine1.color = targetColor;
		else if (stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeRectangle1)
			stateMode.Shape.Rectangle1.color = targetColor;
		else
			return false;
	}
	else
		return false;

	if (setMemory) SetMemory();
	BackgroundColorMode = computeContrast(targetColor, RGB(255, 255, 255)) >= 3 ? 0 : 1;
	PublishDraw3State();
	return true;
}

float GetPenWidth()
{
	if (IsLaserToolActive()) return stateMode.Pen.Laser.width;
	if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen)
	{
		return stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1
			? stateMode.Pen.Highlighter1.width
			: stateMode.Pen.Brush1.width;
	}
	if (stateMode.StateModeSelect == StateModeSelectEnum::IdtShape)
	{
		return stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeRectangle1
			? stateMode.Shape.Rectangle1.width
			: stateMode.Shape.StraightLine1.width;
	}
	return stateMode.Pen.Brush1.width;
}

COLORREF GetPenColor()
{
	if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen)
	{
		return stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1
			? stateMode.Pen.Highlighter1.color
			: stateMode.Pen.Brush1.color;
	}
	if (stateMode.StateModeSelect == StateModeSelectEnum::IdtShape)
	{
		return stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeRectangle1
			? stateMode.Shape.Rectangle1.color
			: stateMode.Shape.StraightLine1.color;
	}
	return stateMode.Pen.Brush1.color;
}

float GetEffectivePenOpacity()
{
	// Laser 不复用已记忆的 Pen.ModeSelect；当前只有荧光笔使用固定半透明合成。
	return !IsLaserToolActive() &&
		stateMode.StateModeSelect == StateModeSelectEnum::IdtPen &&
		stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1
		? Inkeys::Drawing::Draw3::Bridge::kHighlighterCompositeOpacity : 1.0f;
}

bool ChangeStateModeToSelection()
{
	stateMode.StateModeSelectTarget = StateModeSelectEnum::IdtSelection;
	// selection 保留由定格按钮显式进入的定格状态。
	if (!FreezeFrame.select)
	{
		FreezeFrame.mode = 0;
		FreezeFrame.select = false;
	}
	if (state == 1.1) state = 1;
	stateMode.StateModeSelect = StateModeSelectEnum::IdtSelection;
	stateMode.StateModeSelectEcho = StateModeSelectEnum::IdtSelection;
	BackgroundColorMode = 0;
	SyncDraw3State();
	return true;
}

bool ChangeStateModeToPen()
{
	stateMode.StateModeSelectTarget = StateModeSelectEnum::IdtPen;
	stateMode.StateModeSelect = StateModeSelectEnum::IdtPen;
	stateMode.StateModeSelectEcho = StateModeSelectEnum::IdtPen;
	BackgroundColorMode = computeContrast(GetPenColor(), RGB(255, 255, 255)) >= 3 ? 0 : 1;
	SyncDraw3State();
	return true;
}

bool ChangeStateModeToShape()
{
	stateMode.StateModeSelectTarget = StateModeSelectEnum::IdtShape;
	stateMode.StateModeSelect = StateModeSelectEnum::IdtShape;
	stateMode.StateModeSelectEcho = StateModeSelectEnum::IdtShape;
	BackgroundColorMode = computeContrast(GetPenColor(), RGB(255, 255, 255)) >= 3 ? 0 : 1;
	SyncDraw3State();
	return true;
}

bool ChangeStateModeToEraser()
{
	stateMode.StateModeSelectTarget = StateModeSelectEnum::IdtEraser;
	stateMode.StateModeSelect = StateModeSelectEnum::IdtEraser;
	stateMode.StateModeSelectEcho = StateModeSelectEnum::IdtEraser;
	BackgroundColorMode = 0;
	SyncDraw3State();
	return true;
}

bool ChangeStateModeToTouchTest()
{
	// 输入测试尚未接入 Draw3，入口由设置页隐藏。
	return false;
}

void StateMonitoring()
{
	auto snapshot = Inkeys::Drawing::Draw3::ProductRuntimeSnapshot();
	std::uint64_t revision = snapshot.runtimeRevision;
	bool pageSwitching = false;
	bool requestedPreviousPage = false;
	std::size_t requestedPageIndex = 0;
	std::uint64_t pageCommandCountBeforeRequest = 0;
	auto& service = Inkeys::Window::GetService();
	ReconcileDraw3Presentation();
	while (!offSignal)
	{
		// 内容、目标就绪和全帧 clean 共享一个 revision，避免快速切换漏掉握手。
		(void)Inkeys::Drawing::Draw3::WaitForProductRuntimeRevision(revision, 250);
		if (offSignal) break;
		snapshot = Inkeys::Drawing::Draw3::ProductRuntimeSnapshot();
		const bool runtimeChanged = snapshot.runtimeRevision != revision;
		if (runtimeChanged)
		{
			revision = snapshot.runtimeRevision;
		}
		if (runtimeChanged || draw3PresentationRetryPending.load(
			std::memory_order_acquire))
		{
			ReconcileDraw3Presentation();
		}

		WhiteboardPhase phase = whiteboardPhase.load(std::memory_order_acquire);
		const bool desired = whiteboardDesired.load(std::memory_order_acquire);
		if (phase == WhiteboardPhase::Inactive && desired)
		{
			// Enter 先锁住辅助面板和指针，再由 Window Service 统一切换 HWND 样式。
			whiteboardPhase.store(WhiteboardPhase::Entering,
				std::memory_order_release);
			Inkeys::UI::Bar::CollapseAuxiliaryPanels(true);
			Inkeys::UI::Whiteboard::CancelPointerCapture();
			// PageControl 是共享宿主的唯一所有者；保留 PPT 状态并连续形变。
			if (!service.EnterWhiteboardWindowMode())
			{
				// 样式事务失败时恢复 Presentation 控件，下一轮仍可重试进入。
				Inkeys::UI::Ppt::PublishPresentationVisible(
					PptInfoState.TotalPage > 0);
				whiteboardPhase.store(WhiteboardPhase::Inactive,
					std::memory_order_release);
				continue;
			}
			// 几何目标不等待 Draw3 首帧；背景 active 仍在 Entering 就绪后发布。
			Inkeys::UI::Whiteboard::PublishExpandedLayoutTarget(true);
			Inkeys::Drawing::Draw3::SetProductActivationAllowed(false);
			(void)service.SetClickThrough(
				Inkeys::Window::WindowRole::Drawpad, true);
			FreezeFrame.mode = 0;
			FreezeFrame.select = false;
			FreezePPT = false;
			Inkeys::Drawing::Draw3::PublishProductWorkspace(
				Workspace::Whiteboard);
			continue;
		}

		if (phase == WhiteboardPhase::Entering)
		{
			if (!desired)
			{
				Inkeys::UI::Whiteboard::CancelPointerCapture();
				Inkeys::UI::Whiteboard::PublishExpandedLayoutTarget(false);
				(void)service.SetClickThrough(
					Inkeys::Window::WindowRole::Drawpad, true);
				Inkeys::UI::Whiteboard::PublishPageState(
					static_cast<int>(snapshot.currentPageIndex + 1),
					static_cast<int>(snapshot.pageCount), true);
				whiteboardPhase.store(WhiteboardPhase::Exiting,
					std::memory_order_release);
				Inkeys::Drawing::Draw3::PublishProductWorkspace(
					CurrentPrimaryWorkspace());
				continue;
			}
			if (!Draw3WorkspaceReady(snapshot, Workspace::Whiteboard)) continue;

			SetWhiteboardFreezeSurfaceOwned(true);
			Inkeys::UI::Whiteboard::PublishPageState(
				static_cast<int>(snapshot.currentPageIndex + 1),
				static_cast<int>(snapshot.pageCount), false);
			Inkeys::UI::Whiteboard::PublishActive(true);
			if (!Inkeys::UI::Whiteboard::BackgroundMatchesActive(true)) continue;
			Inkeys::UI::Bar::SetWhiteboardActive(true);
			const bool whiteboardWindowStateReady =
				service.SetOverlayFullscreen(true) &&
				service.SetOverlayTopmost(false) &&
				service.SetClickThrough(
					Inkeys::Window::WindowRole::Drawpad, false);
			if (!whiteboardWindowStateReady)
			{
				// Enter 的窗口状态未完整收敛时回滚 UI/Freeze，再等待下一次请求重试。
				(void)service.SetClickThrough(
					Inkeys::Window::WindowRole::Drawpad, true);
				Inkeys::UI::Whiteboard::PublishActive(false);
				SetWhiteboardFreezeSurfaceOwned(false);
				Inkeys::UI::Bar::SetWhiteboardActive(false);
				(void)service.SetOverlayFullscreen(false);
				(void)service.LeaveWhiteboardWindowMode();
				(void)service.SetOverlayTopmost(true);
				Inkeys::Drawing::Draw3::PublishProductWorkspace(
					CurrentPrimaryWorkspace());
				whiteboardPhase.store(WhiteboardPhase::Exiting,
					std::memory_order_release);
				continue;
			}
			Inkeys::Drawing::Draw3::SetProductActivationAllowed(true);
			whiteboardPhase.store(WhiteboardPhase::Active,
				std::memory_order_release);
			continue;
		}

		if (phase == WhiteboardPhase::Active)
		{
			if (!desired)
			{
				pageSwitching = false;
				Inkeys::UI::Whiteboard::CancelPointerCapture();
				Inkeys::UI::Bar::CollapseAuxiliaryPanels(true);
				// 单一 PageControl 从当前白板帧反向重定向，不插入隐藏帧。
				Inkeys::UI::Whiteboard::PublishExpandedLayoutTarget(false);
				Inkeys::UI::Whiteboard::PublishActive(false);
				(void)service.SetClickThrough(
					Inkeys::Window::WindowRole::Drawpad, true);
				Inkeys::UI::Whiteboard::PublishPageState(
					static_cast<int>(snapshot.currentPageIndex + 1),
					static_cast<int>(snapshot.pageCount), true);
				Inkeys::Drawing::Draw3::SetProductActivationAllowed(false);
				whiteboardPhase.store(WhiteboardPhase::Exiting,
					std::memory_order_release);
				Inkeys::Drawing::Draw3::PublishProductWorkspace(
					CurrentPrimaryWorkspace());
				continue;
			}

			if (pageSwitching)
			{
				const std::uint64_t completed = requestedPreviousPage
					? snapshot.previousPageCommandCount
					: snapshot.nextPageCommandCount;
				if (snapshot.currentPageIndex == requestedPageIndex ||
					completed != pageCommandCountBeforeRequest)
					pageSwitching = false;
			}
			if (!pageSwitching && snapshot.workspace == Workspace::Whiteboard)
			{
				const bool previous = whiteboardPreviousRequested.exchange(
					false, std::memory_order_acq_rel);
				const bool next = whiteboardNextRequested.exchange(
					false, std::memory_order_acq_rel);
				if (previous && snapshot.currentPageIndex > 0)
				{
					requestedPageIndex = snapshot.currentPageIndex - 1;
					requestedPreviousPage = true;
					pageCommandCountBeforeRequest = snapshot.previousPageCommandCount;
					pageSwitching = Inkeys::Drawing::Draw3::PublishProductCommand(
						Inkeys::Drawing::Draw3::Bridge::CommandType::PreviousPage) ==
						Inkeys::Drawing::Draw3::Bridge::CommandResult::Accepted;
				}
				else if (next)
				{
					requestedPageIndex = snapshot.currentPageIndex + 1;
					requestedPreviousPage = false;
					pageCommandCountBeforeRequest = snapshot.nextPageCommandCount;
					pageSwitching = Inkeys::Drawing::Draw3::PublishProductCommand(
						Inkeys::Drawing::Draw3::Bridge::CommandType::NextPage) ==
						Inkeys::Drawing::Draw3::Bridge::CommandResult::Accepted;
				}
			}
			Inkeys::UI::Whiteboard::PublishPageState(
				static_cast<int>(snapshot.currentPageIndex + 1),
				static_cast<int>(snapshot.pageCount), pageSwitching);
			continue;
		}

		if (phase == WhiteboardPhase::Exiting)
		{
			if (desired)
			{
				// 先从当前插值帧重定向；背景仍等待 Draw3 Whiteboard 首帧。
				Inkeys::UI::Whiteboard::PublishExpandedLayoutTarget(true);
				Inkeys::UI::Bar::CollapseAuxiliaryPanels(true);
				Inkeys::UI::Whiteboard::CancelPointerCapture();
				if (!service.EnterWhiteboardWindowMode())
				{
					// 重入也必须等待完整的窗口样式事务，不复用半套状态。
					whiteboardPhase.store(WhiteboardPhase::Exiting,
						std::memory_order_release);
					continue;
				}
				Inkeys::Drawing::Draw3::SetProductActivationAllowed(false);
				(void)service.SetClickThrough(
					Inkeys::Window::WindowRole::Drawpad, true);
				Inkeys::Drawing::Draw3::PublishProductWorkspace(
					Workspace::Whiteboard);
				whiteboardPhase.store(WhiteboardPhase::Entering,
					std::memory_order_release);
				continue;
			}
			Inkeys::UI::Whiteboard::PublishExpandedLayoutTarget(false);
			const Workspace primaryWorkspace = CurrentPrimaryWorkspace();
			if (!Draw3WorkspaceReady(snapshot, primaryWorkspace)) continue;

			// Presentation 已有可接管帧后才关闭 Whiteboard，避免 raw COM present 重叠。
			Inkeys::UI::Whiteboard::PublishActive(false);
			if (!Inkeys::UI::Whiteboard::BackgroundMatchesActive(false)) continue;

			Inkeys::UI::Whiteboard::PublishPageState(
				static_cast<int>(snapshot.currentPageIndex + 1),
				static_cast<int>(snapshot.pageCount), false);
			SetWhiteboardFreezeSurfaceOwned(false);
			Inkeys::UI::Bar::SetWhiteboardActive(false);
			(void)service.Hide(Inkeys::Window::WindowRole::Freeze);
			// Presentation 辅助输出始终不参与输入；显式恢复其 click-through，
			// 防止 Whiteboard 期间的窗口样式切换把旧命中状态带回桌面。
			(void)service.SetClickThrough(
				Inkeys::Window::WindowRole::DrawpadPresentation, true);
			const bool presentationWindowStateReady =
				service.SetOverlayFullscreen(false) &&
				service.LeaveWhiteboardWindowMode() &&
				service.SetOverlayTopmost(true) &&
				service.SetClickThrough(
					Inkeys::Window::WindowRole::Drawpad, snapshot.selectionMode);
			if (!presentationWindowStateReady) continue;
			// window mode 恢复后重发 COM 事实；PageControl 始终是共享宿主唯一所有者。
			Inkeys::UI::Ppt::PublishPresentationVisible(
				PptInfoState.TotalPage > 0);
			Inkeys::Drawing::Draw3::SetProductActivationAllowed(false);
			whiteboardPhase.store(WhiteboardPhase::Inactive,
				std::memory_order_release);
			ReconcileDraw3Presentation();
		}
	}
}

bool GetStateMode_Discard(StateModeStruct_Discard* stateModeInfo)
{
	if (!stateModeInfo) return false;
	stateModeInfo->brushWidth = GetPenWidth();
	stateModeInfo->brushColor = GetPenColor();
	if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen)
		stateModeInfo->brushMode = stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1 ? 2.0f : 1.0f;
	else if (stateMode.StateModeSelect == StateModeSelectEnum::IdtShape)
		stateModeInfo->brushMode = stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeRectangle1 ? 4.0f : 3.0f;
	else
		return false;
	return true;
}
