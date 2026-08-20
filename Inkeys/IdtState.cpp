#include "IdtState.h"

#include "IdtConfiguration.h"
#include "IdtDraw.h"
#include "IdtFreezeFrame.h"
#include "IdtPlug-in.h"
#include "Inkeys/Business/LegacyDrawState.hpp"
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
		if (stateMode.laserActive) return Tool::Laser;
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
		if (workspace == Workspace::Presentation && runtime.selectionMode &&
			!runtime.currentPageHasContent && !runtime.auxiliaryFullFrameClean)
			return false;
		return runtime.requestedOutputTarget == expectedTarget &&
			runtime.readyOutputTarget == expectedTarget &&
			runtime.readyOutputRevision == runtime.requestedOutputRevision &&
			runtime.presentedContentRevision == runtime.contentRevision;
	}
}

void SyncDraw3State()
{
	PublishDraw3State();
	ReconcileDraw3Presentation();
}

void ReconcileDraw3Presentation()
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
	if (!runtime.firstFrameReady) return;
	// 离开选择后先让主 Drawpad 接管输入，避免等待预热帧时首个 Down 穿到下层窗口。
	// 选择态仍必须等待辅助 ULW 内容就绪，防止旧帧或双窗 alpha 叠加。
	if (selectionMode && !whiteboard && !targetReady) return;

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
	(void)service.SetDrawpadSurfaceVisibility(visibility);

	const HWND drawpad = service.Handle(WindowRole::Drawpad);
	const HWND presentation = service.Handle(WindowRole::DrawpadPresentation);
	IdtWindowsIsVisible.drawpadWindow = drawpad && presentation &&
		service.Ready(WindowRole::Drawpad) &&
		service.Ready(WindowRole::DrawpadPresentation) &&
		Inkeys::Drawing::Draw3::ProductFirstFrameReady();
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
	if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen)
	{
		if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenBrush1)
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
		if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenBrush1)
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
	stateMode.laserActive = false;
	BackgroundColorMode = 0;
	SyncDraw3State();
	return true;
}

bool ChangeStateModeToPen()
{
	stateMode.StateModeSelectTarget = StateModeSelectEnum::IdtPen;
	stateMode.StateModeSelect = StateModeSelectEnum::IdtPen;
	stateMode.StateModeSelectEcho = StateModeSelectEnum::IdtPen;
	stateMode.laserActive = false;
	BackgroundColorMode = computeContrast(GetPenColor(), RGB(255, 255, 255)) >= 3 ? 0 : 1;
	SyncDraw3State();
	return true;
}

bool ChangeStateModeToShape()
{
	stateMode.StateModeSelectTarget = StateModeSelectEnum::IdtShape;
	stateMode.StateModeSelect = StateModeSelectEnum::IdtShape;
	stateMode.StateModeSelectEcho = StateModeSelectEnum::IdtShape;
	stateMode.laserActive = false;
	BackgroundColorMode = computeContrast(GetPenColor(), RGB(255, 255, 255)) >= 3 ? 0 : 1;
	SyncDraw3State();
	return true;
}

bool ChangeStateModeToEraser()
{
	stateMode.StateModeSelectTarget = StateModeSelectEnum::IdtEraser;
	stateMode.StateModeSelect = StateModeSelectEnum::IdtEraser;
	stateMode.StateModeSelectEcho = StateModeSelectEnum::IdtEraser;
	stateMode.laserActive = false;
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
			// Enter 尚未完成 Draw3 首帧握手前，左右白板栏保持隐藏且不可命中。
			(void)service.Hide(Inkeys::Window::WindowRole::WhiteboardLeft);
			(void)service.Hide(Inkeys::Window::WindowRole::WhiteboardRight);
			if (!service.EnterWhiteboardWindowMode())
			{
				// 样式事务失败时保持 Presentation，下一轮仍可重试进入。
				whiteboardPhase.store(WhiteboardPhase::Inactive,
					std::memory_order_release);
				continue;
			}
			Inkeys::Drawing::Draw3::SetProductActivationAllowed(false);
			(void)service.SetClickThrough(
				Inkeys::Window::WindowRole::Drawpad, true);
			Inkeys::UI::Ppt::PublishPresentationVisible(false);
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
				(void)service.SetClickThrough(
					Inkeys::Window::WindowRole::Drawpad, true);
				Inkeys::UI::Whiteboard::PublishPageState(
					static_cast<int>(snapshot.currentPageIndex + 1),
					static_cast<int>(snapshot.pageCount), true);
				whiteboardPhase.store(WhiteboardPhase::Exiting,
					std::memory_order_release);
				Inkeys::Drawing::Draw3::PublishProductWorkspace(
					Workspace::Presentation);
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
				(void)service.SetOverlayTopmost(true);
				(void)service.LeaveWhiteboardWindowMode();
				Inkeys::Drawing::Draw3::PublishProductWorkspace(
					Workspace::Presentation);
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
				// Exit 一旦请求就先撤掉白板命中窗口，等待 Presentation 接管期间不再消费点击。
				(void)service.Hide(Inkeys::Window::WindowRole::WhiteboardLeft);
				(void)service.Hide(Inkeys::Window::WindowRole::WhiteboardRight);
				(void)service.SetClickThrough(
					Inkeys::Window::WindowRole::Drawpad, true);
				Inkeys::UI::Whiteboard::PublishPageState(
					static_cast<int>(snapshot.currentPageIndex + 1),
					static_cast<int>(snapshot.pageCount), true);
				Inkeys::Drawing::Draw3::SetProductActivationAllowed(false);
				whiteboardPhase.store(WhiteboardPhase::Exiting,
					std::memory_order_release);
				Inkeys::Drawing::Draw3::PublishProductWorkspace(
					Workspace::Presentation);
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
				// Exit 尚未完成时重新进入，重新走 Enter 的窗口/输出握手。
				Inkeys::UI::Bar::CollapseAuxiliaryPanels(true);
				Inkeys::UI::Whiteboard::CancelPointerCapture();
				(void)service.Hide(Inkeys::Window::WindowRole::WhiteboardLeft);
				(void)service.Hide(Inkeys::Window::WindowRole::WhiteboardRight);
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
				Inkeys::UI::Ppt::PublishPresentationVisible(false);
				Inkeys::Drawing::Draw3::PublishProductWorkspace(
					Workspace::Whiteboard);
				whiteboardPhase.store(WhiteboardPhase::Entering,
					std::memory_order_release);
				continue;
			}
			if (!Draw3WorkspaceReady(snapshot, Workspace::Presentation)) continue;

			// Presentation 已有可接管帧后才关闭 Whiteboard，避免 raw COM present 重叠。
			Inkeys::UI::Whiteboard::PublishActive(false);
			if (!Inkeys::UI::Whiteboard::BackgroundMatchesActive(false)) continue;

			Inkeys::UI::Whiteboard::PublishPageState(
				static_cast<int>(snapshot.currentPageIndex + 1),
				static_cast<int>(snapshot.pageCount), false);
			// 退出事务结算后立即按 COM 当前放映状态恢复 PPT 控件，不等待下一轮轮询。
			Inkeys::UI::Ppt::PublishPresentationVisible(
				PptInfoState.TotalPage > 0);
			SetWhiteboardFreezeSurfaceOwned(false);
			Inkeys::UI::Bar::SetWhiteboardActive(false);
			(void)service.Hide(Inkeys::Window::WindowRole::WhiteboardLeft);
			(void)service.Hide(Inkeys::Window::WindowRole::WhiteboardRight);
			(void)service.Hide(Inkeys::Window::WindowRole::Freeze);
			const bool presentationWindowStateReady =
				service.SetOverlayFullscreen(false) &&
				service.SetOverlayTopmost(true) &&
				service.LeaveWhiteboardWindowMode() &&
				service.SetClickThrough(
					Inkeys::Window::WindowRole::Drawpad, snapshot.selectionMode);
			if (!presentationWindowStateReady) continue;
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
