module;

#include "../../../IdtMain.h"

#include "../../../IdtConfiguration.h"
#include <dwrite_1.h>
#include "../../../IdtDraw.h"
#include "../../Business/LegacyDrawState.hpp"
#include "../../../IdtState.h"
#include "../../Window/Window.Legacy.hpp"
#include "Bar.A2.h"
#include "Bar.BottomDock.h"
#include "Bar.PresentDecision.h"
#include "Bar.PresentationProbe.h"
#include <bit>
#include <limits>

#pragma comment(lib, "dxguid.lib")

module Inkeys.UI.Bar;
import :Main;
import :Rendering;
import :Layout;
import :Atomic;
import :Zoom;
import :Theme;

import Inkeys.UI.Bar.FramePacing;
import Inkeys.UI.RenderPipeline;
import Inkeys.Display;

import <ranges>;

import Inkeys.Conv.Color;
import Inkeys.Other.Inputs;
import Inkeys.Conv.Text;

// Interaction 实现单元独占窗口消息状态；协调器仅通过窄接口读取或投递。
bool ReadColorPickerEntryPressed();
void RequestBarBorderCursorSuspend();

namespace
{
	IdtAtomic<bool> currentPageHasContent = false;
	std::atomic_bool contentStateUpdatesReady = false;
	std::atomic_bool pptPresentationActive = false;
	// 几何只使用渲染线程已接管的场景；语义发布不能提前改变在途帧的 dock 线。
	std::atomic_bool pptDockSceneActive = false;
	std::atomic<HWND> pptPresentationWindow = nullptr;
	std::atomic<HMONITOR> pptSceneMonitor = nullptr;
	std::mutex pptSceneMutex;
	Inkeys::UI::Bar::BarPptSceneState pptSceneState;
	std::mutex pptBusinessFocusMutex;
	std::function<void()> pptBusinessFocusCallback;
	Inkeys::UI::Bar::BarA2CallbackDispatcher endShowDispatcher;
	std::atomic_bool whiteboardActive = false;
	std::atomic_bool whiteboardBottomDockRequested = false;
	std::atomic_bool whiteboardDockLockActive = false;
}
extern constexpr double BarButtonHoverFadeDur = BarButtonHoverFadeDurationSeconds;
// Rendering 与 topology 共享同一组 module-linkage 常量，拆分后不复制数值。
extern constexpr double BarDrawAttributeExpandedHeight = 185.0;
extern constexpr double BarDrawAttributeCompactWidth = 60.0;
extern constexpr double BarDrawAttributeCompactScale =
	BarDrawAttributeCompactWidth / BarDrawAttributeExpandedWidth;
extern constexpr double BarDrawAttributeCompactHeight =
	BarDrawAttributeExpandedHeight * BarDrawAttributeCompactScale;
extern constexpr double BarDrawAttributeThicknessHeight = 105.0;
extern constexpr double BarDrawAttributeThicknessControlHeight = 30.0;
extern constexpr double BarDrawAttributeSurfaceOpacity = 0.95;
extern constexpr double BarDrawAttributeThicknessContentInset =
	BarDrawAttributeGap * 2.0;
// 几何属性与绘制属性共用同一组分割线参数，保证光影和圆角一致。
extern constexpr double BarUiDividerRadius = 0.5;
extern constexpr double BarUiDividerCursorLightIntensity = 0.30;
extern constexpr double BarDrawAttributePenTypeButtonWidth = 115.0;
extern constexpr double BarDrawAttributePenTypeButtonHeight = 30.0;
extern constexpr double BarDrawAttributePenTypeLeft =
	BarDrawAttributeExpandedWidth - BarDrawAttributeGap
	- BarDrawAttributePenTypeButtonWidth;
extern constexpr double BarDrawAttributeThicknessDividerLeft =
	BarDrawAttributeGap;
extern constexpr double BarDrawAttributeThicknessDividerRight =
	BarDrawAttributePenTypeLeft - BarDrawAttributeGap;
extern constexpr double BarDrawAttributeThicknessDividerWidth =
	BarDrawAttributeThicknessDividerRight
	- BarDrawAttributeThicknessDividerLeft;
extern constexpr double BarDrawAttributeThicknessAdjustX =
	BarDrawAttributeThicknessDividerRight
	- BarDrawAttributeThicknessControlHeight;
extern constexpr double BarDrawAttributeThicknessPresetStartX =
	BarDrawAttributeThicknessAdjustX
	- (BarDrawAttributeThicknessControlHeight + BarDrawAttributeGap) * 3.0;
extern constexpr double BarDrawAttributePenTypeExtensionDividerX = 85.0;
extern constexpr double BarDrawAttributePenTypeExtensionWidth =
	BarDrawAttributePenTypeButtonWidth
	- BarDrawAttributePenTypeExtensionDividerX;
extern constexpr double BarDrawAttributePenTypeMenuRowHeight = 30.0;
extern constexpr double BarDrawAttributePenTypeMenuPadding = 5.0;
extern constexpr double BarDrawAttributePenTypeMenuCheckAreaWidth = 26.0;
extern constexpr double BarDrawAttributePenTypeMenuCheckSize = 12.0;
extern constexpr double BarDrawAttributePenTypeMenuCheckInset =
	(BarDrawAttributePenTypeMenuCheckAreaWidth
		- BarDrawAttributePenTypeMenuCheckSize) / 2.0;
extern constexpr double BarDrawAttributePenTypeMenuHeight =
	BarDrawAttributePenTypeMenuPadding * 2.0
	+ BarDrawAttributePenTypeMenuRowHeight * 2.0;
extern constexpr double BarGeometryAttributeExpandedWidth = 335.0;
extern constexpr double BarGeometryAttributeExpandedHeight = 100.0;
extern constexpr double BarGeometryAttributeCompactWidth = 60.0;
extern constexpr double BarGeometryAttributeCompactScale =
	BarGeometryAttributeCompactWidth / BarGeometryAttributeExpandedWidth;
extern constexpr double BarGeometryAttributeCompactHeight =
	BarGeometryAttributeExpandedHeight * BarGeometryAttributeCompactScale;
extern constexpr double BarGeometryAttributeGap = 5.0;
extern constexpr double BarGeometryAttributeThicknessButtonSize = 30.0;
extern constexpr double BarGeometryAttributeDividerCursorLightIntensity =
	BarUiDividerCursorLightIntensity;
extern constexpr double BarThicknessSliderTrackHeight = 4.0;
extern constexpr double BarThicknessSliderThumbCenterDiameter = 12.0;
extern constexpr double BarThicknessSliderThumbHoverCenterDiameter = 14.0;
extern constexpr double BarThicknessSliderThumbPressedCenterDiameter = 10.0;
extern constexpr double BarThicknessSliderThumbAnimationDur = 0.28;
	extern constexpr double BarThicknessSliderPressAnimationDur = 0.12;
	extern constexpr double BarThicknessPreviewPopupAnimationDur = 0.40;
	extern constexpr double BarThicknessPreviewNumberAnimationDur = 0.18;
	extern constexpr double BarThicknessPreviewPopupPadding = 8.0;
	extern constexpr double BarThicknessPreviewPopupThumbGap = 10.0;
	extern constexpr double BarThicknessPreviewAvoidGap = 5.0;
	extern constexpr double BarThicknessPreviewNumberGap = 5.0;
	extern constexpr double BarThicknessPreviewNumberInset = 5.0;
	extern constexpr double BarThicknessPreviewNumberFontSize = 13.0;
	// FineDial 激活区沿 Preview 展开方向排列，数值均为未缩放 DIP。
	extern constexpr double BarThicknessFineDialActivationPreviewBaseOpacity = 0.5;
	extern constexpr double BarThicknessFineDialActivationPreviewEnterDur = 0.18;
	extern constexpr double BarThicknessFineDialActivationPreviewFadeOutDur = 0.30;
	extern constexpr double BarThicknessFineDialSelectionTransitionDur = 0.18;
	extern constexpr double BarThicknessFineDialPopupPanelGapDip = 8.0;
	extern constexpr double BarThicknessFineDialTransitionDur = 0.28;
	extern constexpr double BarThicknessFineDialThetaLimit = 1.20;
	extern constexpr double BarThicknessFineDialDepthLiftDip = 4.0;
	extern constexpr double BarThicknessFineDialEdgeFadeStart = 0.68;
	extern constexpr double BarThicknessFineDialTickLengthDip = 7.0;
	extern constexpr double BarThicknessFineDialMajorTickLengthDip = 12.0;
	extern constexpr double BarThicknessFineDialSelectorWidthDip = 7.0;
	extern constexpr double BarThicknessFineDialSelectorHeightDip = 5.0;
	extern constexpr double BarThicknessSliderThumbMorphExitOpacity = 0.04;
	// 拖动改值后静止 0.5s 出提示，再 1.5s（合计 2.0s）进度走满并锁定粗细。
	extern constexpr double BarThicknessHoldHintAnimDur = 0.18;
	extern constexpr double BarThicknessHoldExchangeAnimDur = 0.12;
	// 圆环相对文字行高为 3/5，并比默认 5px 间隙更贴近文字。
	extern constexpr double BarThicknessHoldRingSizeScale = 3.0 / 5.0;
	extern constexpr double BarThicknessHoldRingTextGap = 2.0;
	extern constexpr double BarThicknessTooltipBadgeHeight = 24.0;
extern constexpr double BarThicknessTooltipIconSize = 14.0;
	extern constexpr double BarThicknessTooltipCloseButtonSize = 20.0;
extern constexpr double BarThicknessTooltipHitPadding = 2.0;
extern constexpr double BarThicknessTooltipPadding = 8.0;
extern constexpr double BarThicknessTooltipCloseReserve = 25.0;
extern constexpr double BarThicknessTooltipTitleFontSize = 12.0;
extern constexpr double BarThicknessTooltipBodyFontSize = 10.0;
extern constexpr double BarThicknessTooltipLineGap = 3.0;
extern constexpr double BarThicknessTooltipPopupGap =
	BarDrawAttributeThicknessContentInset;
extern constexpr double BarThicknessTooltipFillOpacity =
	BarDrawAttributeSurfaceOpacity;
extern constexpr double BarThicknessTooltipFrameOpacity = 0.18;
extern constexpr double BarColorSwatchFrameOpacity = 0.18;
extern constexpr double BarColorPickerPanelWidth = 300.0;
// 5 顶距 + 30 顶栏 + 5 间隔 + 132 色板 + 32 读数（含底部 10px 额外间隙）+ 5 底距 = 209。
extern constexpr double BarColorPickerPanelHeight = 209.0;
extern constexpr double BarColorPickerPaletteInset = 5.0;
extern constexpr double BarColorPickerPaletteTop = 40.0;
extern constexpr double BarColorPickerPaletteWidth = 290.0;
extern constexpr double BarColorPickerPaletteHeight = 132.0;
// 与粗细快速调节按钮同高（30px），顶部色系/预览/关闭共用。
extern constexpr double BarColorPickerChromeHeight = 30.0;
extern constexpr double BarColorPickerChromeTop = 5.0;
extern constexpr double BarColorPickerPanelGap = BarDrawAttributeGap;
// 颜色选择器与绘制属性窗口共用默认展开时长，保证回弹节奏一致。
extern constexpr double BarColorPickerPanelAnimationDur = 0.40;
extern constexpr double BarColorPickerCompactScale = BarDrawAttributeCompactScale;
extern constexpr double BarColorPickerCompactWidth =
	BarColorPickerPanelWidth * BarColorPickerCompactScale;
extern constexpr double BarColorPickerCompactHeight =
	BarColorPickerPanelHeight * BarColorPickerCompactScale;
extern constexpr double BarColorPickerHoldHintAnimationDur = 0.18;
extern constexpr double BarMorePanelGap = 5.0;
// 主栏与浮层之间留出更明显的净空；网格单元仍沿用标准 5 DIP 间距。
extern constexpr double BarMorePanelAnchorGap = 12.0;
extern constexpr double BarMorePanelPadding = 5.0;
// 关闭按钮放在网格右侧的窄栏，不再占用面板顶部高度。
extern constexpr double BarMorePanelCloseSideWidth = 35.0;
extern constexpr double BarMorePanelSeparatorGap = 10.0;
extern constexpr double BarMorePanelCompactScale = 0.16;
extern constexpr double BarMorePanelCompactWidth = 60.0;
extern constexpr double BarMorePanelCompactHeight = 30.0;
// ====================
// 媒体

// 媒体操控类
void BarMediaClass::LoadFormat()
{
	formatCache = make_unique<BarFormatCache>(
		Inkeys::UI::RenderPipeline::DWriteFactory().Get());
}

// ====================
// 界面

Inkeys::UI::Bar::Ui3FiniteSignature BarUISetClass::ReadFiniteSignature(
	const StateModeVersionedSnapshot& tool)
{
	using namespace Inkeys::UI::Bar;
	Ui3FiniteSignature signature;
	const auto& attribute = barState.drawAttributeBar;
	const bool auxClosed = !attribute.penTypeMenuOpen && !attribute.colorPickerOpen
		&& attribute.thicknessViewMode == ThicknessViewMode::Preview
		&& !attribute.thicknessSliderHover && !attribute.thicknessSliderPinned
		&& !attribute.thicknessSliderPressed && !attribute.thicknessSliderDragging
		&& !attribute.thicknessPreviewDragging && !attribute.thicknessSliderCapture
		&& !attribute.thicknessFineDialDragging && !attribute.thicknessFineDialPhysicsActive
		&& !attribute.thicknessFineDialCandidateActive && !attribute.thicknessFineDialRangeTransitionActive
		&& !attribute.thicknessFineDialActivationPreviewActive && !attribute.thicknessFineDialActivationDwellActive
		&& !attribute.thicknessSliderHoldHintActive && !attribute.thicknessSliderHoldLocked
		&& !attribute.thicknessAnnotationHover && !attribute.thicknessAnnotationHoverGrace
		&& !attribute.thicknessAnnotationPinned && !attribute.thicknessOverflowHover
		&& !attribute.thicknessOverflowHoverGrace && !attribute.thicknessOverflowPinned
		&& !attribute.colorPickerPointerPressed && !attribute.colorPickerPointerCapture
		&& !attribute.colorPickerHoldHintActive && !attribute.colorPickerHoldLocked
		&& attribute.colorPickerKeyboardDownMask == 0;
	signature.flags = (barState.fold ? 1u : 0u) | (barState.drawAttribute ? 2u : 0u)
		| (barState.geometryAttribute ? 4u : 0u) | (barState.moreExpanded ? 8u : 0u)
		| (barState.eraserAttribute ? 16u : 0u) | (barState.eraserSensitivityOpen ? 32u : 0u)
		| (auxClosed ? 64u : 0u);
	signature.validMask = 1;
	// 宽/色/工具版本来自同一快照，不用独立getter拼接不同代状态。
	signature.stateMode = static_cast<std::uint32_t>(tool.state.StateModeSelect);
	signature.penMode = static_cast<std::uint32_t>(tool.state.Pen.ModeSelect);
	signature.penColorRgb = GetPenColor(tool.state) & 0x00FFFFFFu;
	const float width = GetPenWidth(tool.state);
	signature.penWidthBits = std::bit_cast<std::uint32_t>(width);
	signature.toolRevision = tool.revision;
	if (tool.state.StateModeSelect == StateModeSelectEnum::IdtPen && !tool.state.laserActive
		&& isfinite(width) && width > 0.0f) signature.validMask |= 2;
	signature.mainSide = barState.widgetPosition.mainBar ? 1u : 0u;
	signature.primarySide = barState.widgetPosition.primaryBar ? 1u : 0u;
	signature.validMask |= 4;
	signature.thicknessView = static_cast<std::uint32_t>(attribute.thicknessViewMode.load());
	signature.validMask |= 8;
	signature.darkStyle = barStyle.darkStyle ? 1u : 0u;
	signature.validMask |= 16;
	// 环境只做一次非阻塞读取；锁忙/未知display不填零冒充有效mask。
	{
		unique_lock displayLock(pendingDisplayPublishMutex, std::try_to_lock);
		if (displayLock)
		{
			const auto serial = pendingDisplaySerial.load(std::memory_order_acquire);
			const auto dpi = pendingDisplayDpi.load(std::memory_order_relaxed);
			if (serial != 0 && (serial & 1ULL) == 0 && dpi != 0)
			{
				signature.displaySerial = serial;
				signature.dpi = dpi;
				signature.validMask |= 32 | 64;
			}
		}
	}
	const double configZoom = barStyle.configZoom;
	signature.configZoomBits = std::bit_cast<std::uint64_t>(configZoom);
	if (isfinite(configZoom) && configZoom > 0.0) signature.validMask |= 128;
	if (Inkeys::UI::Bar::WhiteboardActive() || Inkeys::UI::Bar::PptPresentationActive()) signature.validMask = 0;
	return signature;
}

void BarUISetClass::UpdateRendering(bool updateState)
{
	static mutex mtx;
	lock_guard<mutex> lock(mtx);

	// 状态更新
	if (updateState)
	{
		// 按钮、面板归位和粗细文字使用同一工具代次，再统一发布渲染请求。
		const auto stateMode = GetStateModeSnapshot();
		barButtonSet.StateUpdate(stateMode);
		// 仅在画笔模式刷新粗细文字，收起过程中保留最后一次有效显示。
		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen)
			barState.ThicknessDisplayUpdate(GetPenWidth(stateMode));
	}

	if (Inkeys::UI::Bar::CurrentUi3FiniteMutation())
	{
		const auto tool = GetStateModeVersionedSnapshot();
		const auto signature = ReadFiniteSignature(tool);
		// 规范化已结束；包括UpdateRendering(false)，仍在原Notify/Request之前封口。
		Inkeys::UI::Bar::FinishCurrentUi3FiniteAtRenderRequest(signature);
	}

	// 通知计算并渲染
	BarAtomic::wait.Notify();
	Inkeys::UI::RenderPipeline::Request(
		Inkeys::UI::RenderPipeline::Client::Bar);
}

// 全局 Bar UI 集合
BarUISetClass barUISet;

// ====================
// 环境

// 初始化

namespace Inkeys::UI::Bar
{
	void SetAnimationOptions(bool enable, double speedRate)
	{
		speedRate = isfinite(speedRate) ? clamp(speedRate, 0.1, 5.0) : 1.0;
		// 使用足够大的有限倍率统一完成普通动画、批次和 SVG 关键帧，不能用 0 让时间轴停住。
		BarUiAnimationEnabled = enable;
		BarUiAnimationSpeedRate = enable ? speedRate : 1.0e12;
		if (!enable) RequestBarBorderCursorSuspend();
		barUISet.UpdateRendering(false);
	}

	void SetEdgeLightingOptions(bool enable, bool dynamic)
	{
		BarUiEdgeLightingEnabled = enable;
		BarUiDynamicEdgeLightingEnabled = dynamic;
		// 关闭任一级动态光门禁时，统一交给 Bar 窗口线程注销 Raw Input。
		if (!enable || !dynamic) RequestBarBorderCursorSuspend();
		barUISet.UpdateRendering(false);
	}

	void SetDebugOptions(bool enable, bool showFrameRate)
	{
		BarUiDebugModeEnabled = enable;
		BarUiDebugFrameRateEnabled = showFrameRate;
		// 渲染线程会比较新旧选项，只在需要时清除 FPS 文字或红框。
		barUISet.UpdateRendering(false);
		Inkeys::UI::RenderPipeline::Request(
			Inkeys::UI::RenderPipeline::WhiteboardMask());
	}

	bool DebugModeEnabled() noexcept
	{
		return BarUiDebugModeEnabled;
	}

	void SetCurrentPageHasContent(bool hasContent) noexcept
	{
		if (static_cast<bool>(currentPageHasContent) == hasContent) return;
		currentPageHasContent = hasContent;
		if (contentStateUpdatesReady.load(std::memory_order_acquire))
			barUISet.UpdateRendering();
	}

	bool CurrentPageHasContent() noexcept
	{
		return currentPageHasContent;
	}

	void SetPptPresentationActive(bool active) noexcept
	{
		if (pptPresentationActive.exchange(active, std::memory_order_acq_rel)
			== active) return;
		if (contentStateUpdatesReady.load(std::memory_order_acquire))
			barUISet.UpdateRendering();
	}

	bool PptPresentationActive() noexcept
	{
		return pptPresentationActive.load(std::memory_order_acquire);
	}

	void PublishPptSession(std::uint64_t session, bool active, HWND showWindow) noexcept
	{
		bool changed = false;
		{
			std::scoped_lock lock(pptSceneMutex);
			// 先发布完整载荷，再开放场景边沿；渲染线程不能读到旧窗口或旧 dock 条件。
			pptPresentationWindow.store(active ? showWindow : nullptr, std::memory_order_release);
			changed = pptPresentationActive.exchange(active, std::memory_order_acq_rel) != active;
			changed = pptSceneState.Publish(session, active) || changed;
		}
		if (changed) barUISet.UpdateRendering(
			contentStateUpdatesReady.load(std::memory_order_acquire));
	}
	std::optional<bool> ConsumePptSceneTransition() noexcept
	{
		std::scoped_lock lock(pptSceneMutex);
		const auto transition = pptSceneState.Take();
		if (transition.has_value())
			pptDockSceneActive.store(pptSceneState.active, std::memory_order_release);
		return transition;
	}
	HWND PptPresentationWindow() noexcept
	{
		return pptPresentationWindow.load(std::memory_order_acquire);
	}
	HMONITOR PptSceneMonitor() noexcept
	{
		return pptSceneMonitor.load(std::memory_order_acquire);
	}
	void UpdatePptSceneMonitor(HWND window) noexcept
	{
		{
			std::scoped_lock lock(pptSceneMutex);
			if (!WhiteboardActive() && window && IsWindow(window))
				pptSceneMonitor.store(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST),
					std::memory_order_release);
		}
		barUISet.PublishDisplaySnapshot(Inkeys::Display::GetSnapshot());
	}
	double SceneBottomDockInsetDip() noexcept
	{
		return WhiteboardActive() || pptDockSceneActive.load(std::memory_order_acquire)
			? 5.0 : 0.0;
	}
	bool PptBusinessFocusAllowed() noexcept
	{
		return PptPresentationActive() && !WhiteboardActive()
			&& !barUISet.barState.drawAttributeBar.colorPickerOpen
			&& !barUISet.barState.eraserAttribute;
	}
	void SetBusinessFocusCallback(std::function<void()> callback)
	{
		std::scoped_lock lock(pptBusinessFocusMutex);
		pptBusinessFocusCallback = std::move(callback);
	}
	void NotifyPptBusinessAction()
	{
		if (!PptBusinessFocusAllowed()) return;
		std::function<void()> callback;
		{
			std::scoped_lock lock(pptBusinessFocusMutex);
			callback = pptBusinessFocusCallback;
		}
		if (callback) callback();
	}
	void SetEndShowRequestCallback(std::function<void(std::uint64_t)> callback)
	{
		endShowDispatcher.SetStamped(std::move(callback));
	}
	void CompleteEndShowRequest(std::uint64_t request) noexcept
	{
		endShowDispatcher.Complete(request);
	}

	void SetEndShowCallback(std::function<void()> callback)
	{
		endShowDispatcher.Set(std::move(callback));
	}

	void RequestEndShow()
	{
		// 回调只投递原 PPT 业务队列，不能在 Bar 输入线程持锁执行。
		(void)endShowDispatcher.Dispatch();
	}

	void CompleteEndShowRequest() noexcept
	{
		endShowDispatcher.Complete();
	}

	void SetWhiteboardActive(bool active) noexcept
	{
		const bool changed = whiteboardActive.exchange(active,
			std::memory_order_acq_rel) != active;
		if (changed)
		{
			// 工作区切换时默认使用拖拽模式，属性浮层保持收起；后续由用户按钮控制展开。
			barUISet.CollapseAuxiliaryPanels(true);
		}
		if (active)
		{
			if (changed)
			{
				// 白板仍以主屏为场景；返回时保持原有收起位置，不重放 PPT 入口。
				{
					std::scoped_lock lock(pptSceneMutex);
					pptSceneMonitor.store(nullptr, std::memory_order_release);
				}
				barUISet.PublishDisplaySnapshot(Inkeys::Display::GetSnapshot());
			}
			RequestWhiteboardBottomDock();
		}
		else
		{
			// 退出白板后主栏回到 Presentation 的收起态，避免下一次桌面点击
			// 被残留的 bottom dock 或辅助面板重新唤醒。
			barUISet.barState.fold = true;
			whiteboardBottomDockRequested.store(false, std::memory_order_release);
			whiteboardDockLockActive.store(false, std::memory_order_release);
		}
		if (changed || contentStateUpdatesReady.load(std::memory_order_acquire))
			barUISet.UpdateRendering();
	}

	void CollapseAuxiliaryPanels(bool cancelCapture) noexcept
	{
		barUISet.CollapseAuxiliaryPanels(cancelCapture);
	}

	bool WhiteboardActive() noexcept
	{
		return whiteboardActive.load(std::memory_order_acquire);
	}

	void RequestWhiteboardBottomDock() noexcept
	{
		whiteboardDockLockActive.store(true, std::memory_order_release);
		whiteboardBottomDockRequested.store(true, std::memory_order_release);
		if (contentStateUpdatesReady.load(std::memory_order_acquire))
			barUISet.UpdateRendering(false);
	}

	bool ConsumeWhiteboardBottomDockRequest() noexcept
	{
		return whiteboardBottomDockRequested.exchange(false,
			std::memory_order_acq_rel);
	}

	bool WhiteboardDockLockActive() noexcept
	{
		return whiteboardDockLockActive.load(std::memory_order_acquire);
	}

	void ClearWhiteboardDockLock() noexcept
	{
		whiteboardBottomDockRequested.store(false, std::memory_order_release);
		whiteboardDockLockActive.store(false, std::memory_order_release);
	}

	bool HideWhiteboardSnapIndicator() noexcept
	{
		return WhiteboardActive();
	}

	void SetContentStateUpdatesReady(bool ready) noexcept
	{
		contentStateUpdatesReady.store(ready, std::memory_order_release);
		// 初始化窗口内可能已收到内容变化，再做一次完整状态同步。
		if (ready) barUISet.UpdateRendering();
	}


}
