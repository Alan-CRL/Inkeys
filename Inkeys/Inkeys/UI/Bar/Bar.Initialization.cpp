module;

#include "../../../IdtMain.h"

#include "../../../IdtConfiguration.h"
#include "../../../IdtDraw.h"
#include "../../../IdtI18n.h"
#include "../../../IdtI18nKeys.g.h"
#include "../../../IdtState.h"
#include "../../Drawing/Draw3/Draw3.Product.h"
#include "../../Input/KeyboardShortcuts.h"
#include "../../Window/Window.Legacy.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <atomic>
#include <optional>


#include <algorithm>

module Inkeys.UI.Bar;
import :Main;
import :Layout;
import :Atomic;
import :Zoom;
import :Theme;

import Inkeys.Conv.Color;
import Inkeys.Helper.Thread;
import Inkeys.Business.ComponentActions;
import Inkeys.Input.MouseHook;
import Inkeys.Other.Config;
import Inkeys.Window;
import Inkeys.UI.RenderPipeline;
import Inkeys.UI.StartupPreview;
import Inkeys.Startup.Progress;
import Inkeys.Display;
import Inkeys.Other.Config;
import Inkeys.UI.UI3.Material;
import Inkeys.UI.StartupPlacement;
import Inkeys.UI.Bar.LogoAppearance;

using namespace Inkeys::UI::UI3;

// 初始化只读 Main 中的共享布局常量，保持 topology 与 Rendering 数值一致。
extern const double BarDrawAttributeCompactWidth;
extern const double BarDrawAttributeCompactScale;
extern const double BarDrawAttributeCompactHeight;
extern const double BarDrawAttributeThicknessHeight;
extern const double BarDrawAttributeSurfaceOpacity;
extern const double BarUiDividerRadius;
extern const double BarUiDividerCursorLightIntensity;
extern const double BarDrawAttributePenTypeButtonWidth;
extern const double BarDrawAttributePenTypeButtonHeight;
extern const double BarDrawAttributePenTypeExtensionWidth;
extern const double BarDrawAttributePenTypeMenuRowHeight;
extern const double BarDrawAttributePenTypeMenuPadding;
extern const double BarDrawAttributePenTypeMenuCheckSize;
extern const double BarDrawAttributePenTypeMenuHeight;
extern const double BarGeometryAttributeCompactWidth;
extern const double BarGeometryAttributeCompactScale;
extern const double BarGeometryAttributeCompactHeight;
extern const double BarGeometryAttributeThicknessButtonSize;
extern const double BarGeometryAttributeDividerCursorLightIntensity;
extern const double BarThicknessPreviewNumberFontSize;
extern const double BarThicknessTooltipCloseButtonSize;
extern const double BarThicknessTooltipTitleFontSize;
extern const double BarThicknessTooltipBodyFontSize;
extern const double BarMorePanelCompactScale;
extern const double BarMorePanelCompactWidth;
extern const double BarMorePanelCompactHeight;

constexpr double BarColorSwatchCursorLightIntensity = 0.50;
extern constexpr double BarGeometryAttributeShapeButtonSize = 50.0;

bool BarUISetClass::InitializeStartupPlacement(Inkeys::UI::StartupPlacement placement) noexcept
{
	// 只在渲染注册前调用，底部吸附状态通过类自身维护，不暴露 protected 字段。
	startupPlacement = Inkeys::UI::NormalizeStartupPlacement(static_cast<int>(placement));
	// floating 在此表示“折叠为单个主按钮”；居中顶栏是展开的 Floating 形态。
	const bool floating = Inkeys::UI::StartupPlacementIsFolded(startupPlacement);
	const bool docked = Inkeys::UI::StartupPlacementDocksToBottom(startupPlacement);
	barState.fold = floating;
	bottomDockMode.store(docked ? Inkeys::UI::Bar::BarBottomDockMode::BottomDocked
		: Inkeys::UI::Bar::BarBottomDockMode::Floating, memory_order_relaxed);
	bottomDockCenterMode.store(docked ? Inkeys::UI::Bar::BarBottomDockCenterMode::Centered
		: Inkeys::UI::Bar::BarBottomDockCenterMode::Free, memory_order_relaxed);
	return floating;
}

void BarUISetClass::PublishDisplaySnapshot(
	Inkeys::Display::SnapshotPtr snapshot) noexcept
{
	const auto* monitor = snapshot
		? snapshot->Find(Inkeys::UI::Bar::PptSceneMonitor()) : nullptr;
	if (!monitor && snapshot) monitor = snapshot->Primary();
	if (!monitor) return;
	const double desktopDockLine = Inkeys::Display::ResolveDesktopBottomDockLine(*monitor);
	const bool protectBottomEdge = Inkeys::Display::ShouldProtectTaskbarBottomEdge(*monitor);
	const double uiBaseScale = Inkeys::Display::ResolveUiScaleBase(
		*snapshot, *monitor, static_cast<Inkeys::Display::UiScaleBaseMode>(
			Inkeys::config.UI.Bar.UseTaskbarHeightAsScaleBase.load()));
	{
		lock_guard lock(pendingDisplayPublishMutex);
		// 其他屏幕或 Shell 重复采样不能重启本栏的位置动画。
		const auto previous = PendingDisplaySnapshot();
		if (previous.serial != 0
			&& EqualRect(&previous.bounds, &monitor->bounds)
			&& EqualRect(&previous.workArea, &monitor->workArea)
			&& previous.dpi == (monitor->effectiveDpiX ? monitor->effectiveDpiX : USER_DEFAULT_SCREEN_DPI)
			&& previous.uiBaseScale == uiBaseScale
			&& previous.desktopDockLine == desktopDockLine
			&& previous.protectBottomEdge == protectBottomEdge) return;
		// 奇数 serial 表示发布中；读方只接受同一偶数 serial 的完整快照。
		pendingDisplaySerial.fetch_add(1, memory_order_acq_rel);
		std::atomic_thread_fence(memory_order_release);
		pendingDisplayLeft.store(monitor->bounds.left, memory_order_relaxed);
		pendingDisplayTop.store(monitor->bounds.top, memory_order_relaxed);
		pendingDisplayRight.store(monitor->bounds.right, memory_order_relaxed);
		pendingDisplayBottom.store(monitor->bounds.bottom, memory_order_relaxed);
		pendingWorkAreaLeft.store(monitor->workArea.left, memory_order_relaxed);
		pendingWorkAreaTop.store(monitor->workArea.top, memory_order_relaxed);
		pendingWorkAreaRight.store(monitor->workArea.right, memory_order_relaxed);
		pendingWorkAreaBottom.store(monitor->workArea.bottom, memory_order_relaxed);
		pendingDisplayDpi.store(monitor->effectiveDpiX ? monitor->effectiveDpiX :
			USER_DEFAULT_SCREEN_DPI, memory_order_relaxed);
		pendingDisplayUiBaseScale.store(uiBaseScale, memory_order_relaxed);
		pendingDesktopDockLine.store(desktopDockLine, memory_order_relaxed);
		pendingProtectBottomEdge.store(protectBottomEdge, memory_order_relaxed);
		pendingDisplaySerial.fetch_add(1, memory_order_release);
	}
	UpdateRendering(false);
}

void BarUISetClass::PublishWindowDpi(UINT dpi) noexcept
{
	if (!dpi) return;
	// DPI 与任务栏基准来自同一显示快照，避免局部更新拼出跨屏混合数据。
	(void)Inkeys::Display::Refresh(Inkeys::Display::ChangeReason::Display);
	PublishDisplaySnapshot(Inkeys::Display::GetSnapshot());
}

void BarUISetClass::StartDisplayTracking()
{
	displaySubscription.Reset();
	displaySubscription = Inkeys::Display::Subscribe(
		[this](Inkeys::Display::SnapshotPtr snapshot)
		{
			// 订阅线程只发布原子目标，布局与资源仍由共享渲染线程接管。
			PublishDisplaySnapshot(std::move(snapshot));
		});
}

void BarUISetClass::StopDisplayTracking() noexcept
{
	displaySubscription.Reset();
}

namespace Inkeys::UI::Bar
{
	WNDPROC WindowProc() noexcept
	{
		return barWindowMsgCallback;
	}

	void Initialization(Inkeys::UI::StartupPlacement startupPlacement)
	{
		Inkeys::Thread::StatusGuard guard("BarInitializationClass::BarInitialization");
		Inkeys::UI::StartupPreview::SetBarStartupState(
			Inkeys::UI::StartupPreview::BarStartupState::Initializing);
		if (offSignal)
		{
			Inkeys::UI::StartupPreview::SetBarStartupState(
				Inkeys::UI::StartupPreview::BarStartupState::StoppedBeforeReady);
			return;
		}
		// 在渲染注册前锁存位置及折叠状态，角落模式从第一帧就是主按钮。
		const bool floatingStartup = barUISet.InitializeStartupPlacement(startupPlacement);
		// 初始化
		if (!InitializeWindow(barUISet))
		{
			Inkeys::UI::StartupPreview::SetBarStartupState(
				Inkeys::UI::StartupPreview::BarStartupState::WindowMissing);
			return;
		}
		(void)Inkeys::Startup::Report(Inkeys::Startup::Milestone::BarWindowReady);
		barUISet.ConfigureLocalizedTypography();
		InitializeUI(barUISet);
		(void)Inkeys::Startup::Report(Inkeys::Startup::Milestone::BarUiGraphReady);
		barUISet.StartDisplayTracking();

		barUISet.barMedia.LoadFormat();
		(void)Inkeys::Startup::Report(Inkeys::Startup::Milestone::BarMediaReady);

		// 首次布局直接读取 Draw3 内容快照，避免依赖监控线程稍后的修订通知。
		SetCurrentPageHasContent(
			Inkeys::Drawing::Draw3::ProductRuntimeSnapshot().currentPageHasContent);
		// 初始化 按钮 们
		barUISet.barButtonSet.PresetInitialization();
		barUISet.barButtonSet.RegisterBuiltInComponents();
		{
			barUISet.barButtonSet.Load();
			barUISet.barButtonSet.StateUpdate();
		}
		if (floatingStartup)
		{
			// 子控件的继承仅传位置、不传透明度；首帧直接设置折叠终态，不能先淡出图标。
			for (int id = 0; id < barUISet.barButtonSet.tot; ++id)
			{
				auto* button = barUISet.barButtonSet.buttonList.Get(id);
				if (!button) continue;
				button->button.x.SetDirect(40.0);
				button->button.y.SetDirect(40.0);
				button->button.pct.SetDirect(0.0);
				if (button->button.framePct) button->button.framePct->SetDirect(0.0);
				if (button->button.frameLightPct) button->button.frameLightPct->SetDirect(0.0);
				button->icon.pct.SetDirect(0.0);
				button->pngIcon.pct.SetDirect(0.0);
				button->name.pct.SetDirect(0.0);
			}
		}
		(void)Inkeys::Startup::Report(Inkeys::Startup::Milestone::BarComponentsReady);
		SetContentStateUpdatesReady(true);

		barUISet.barState.PositionUpdate(barUISet.barStyle.zoom);
		(void)Inkeys::Startup::Report(Inkeys::Startup::Milestone::BarStateReady);
		if (offSignal)
		{
			SetContentStateUpdatesReady(false);
			Inkeys::UI::StartupPreview::SetBarStartupState(
				Inkeys::UI::StartupPreview::BarStartupState::StoppedBeforeReady);
			return;
		}

		namespace Keyboard = Inkeys::Input::KeyboardShortcuts;
		const auto& keys = Inkeys::config.Input.Shortcuts;
		Keyboard::SetEnabled((keys.ToggleAnnotation.load() ? Keyboard::Flag(Keyboard::Command::ToggleAnnotation) : 0U) |
			(keys.Undo.load() ? Keyboard::Flag(Keyboard::Command::Undo) : 0U) |
			(keys.RedoY.load() ? Keyboard::Flag(Keyboard::Command::RedoY) : 0U) |
			(keys.RedoShiftZ.load() ? Keyboard::Flag(Keyboard::Command::RedoShiftZ) : 0U) |
			(keys.Clear.load() ? Keyboard::Flag(Keyboard::Command::Clear) : 0U) |
			(keys.Freeze.load() ? Keyboard::Flag(Keyboard::Command::Freeze) : 0U));
		const auto keyboardStatus = Keyboard::Start();
		if ((!keyboardStatus.hookInstalled ||
			((keyboardStatus.enabledMask & Keyboard::Flag(Keyboard::Command::ToggleAnnotation)) &&
				!keyboardStatus.hotkeyRegistered)))
			Inkeys::Log::Warn(Inkeys::Log::Module::Input, "快捷键输入服务状态: hook={}, hotkey={}, hookError={}, hotkeyError={}",
				keyboardStatus.hookInstalled, keyboardStatus.hotkeyRegistered,
				keyboardStatus.hookError, keyboardStatus.hotkeyError);
		if (!Inkeys::Input::MouseHook::Start())
			Inkeys::Log::Warn(Inkeys::Log::Module::Input, "鼠标钩子安装失败，全局滚轮和栏外点击暂不可用");
		// Bar 只注册单帧回调，唯一渲染线程由 RenderPipeline 持有。
		if (!barUISet.Rendering())
		{
			SetContentStateUpdatesReady(false);
			Keyboard::Stop();
			Inkeys::Input::MouseHook::Stop();
			barUISet.StopDisplayTracking();
			return;
		}
		thread interactionThread([&]() { barUISet.Interact(); });

		// 等待

		while (!offSignal) this_thread::sleep_for(chrono::milliseconds(100));
		SetContentStateUpdatesReady(false);
		// 先停止输入生产者，再由窗口线程撤销计时器、Raw Input 与 capture。
		Keyboard::Stop();
		Inkeys::Input::MouseHook::Stop();
		// 退出信号与普通渲染请求共用代次通知，唤醒真正休眠的渲染线程。
		BarAtomic::wait.Notify();
		Inkeys::UI::RenderPipeline::WakeForStop();

		// 输入 worker 在无手势时无限休眠；退出必须显式唤醒同一个事件邮箱。
		Inkeys::UI::UI3::WakeInteractionWorker();
		if (interactionThread.joinable()) interactionThread.join();
		barUISet.StopDisplayTracking();
		barUISet.StopRendering();
		if (Inkeys::UI::StartupPreview::GetBarStartupState()
			!= Inkeys::UI::StartupPreview::BarStartupState::FirstFrameCommitted)
			Inkeys::UI::StartupPreview::SetBarStartupState(
				Inkeys::UI::StartupPreview::BarStartupState::StoppedBeforeReady);
		Inkeys::Business::ShutdownComponentActions();

		return;
	}

	bool InitializeWindow(BarUISetClass& barUISet)
	{
		// 创建窗口失败时不能继续启动输入与渲染线程。
		HWND window = floating_window;
		if (!window || !IsWindow(window)) return false;

		barUISet.barWindow.x = 0;
		barUISet.barWindow.y = 0;
		const auto displaySnapshot = Inkeys::Display::GetSnapshot();
		const auto* monitor = displaySnapshot ? displaySnapshot->Primary() : nullptr;
		const RECT monitorBounds = monitor ? monitor->bounds :
			RECT{ 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
		barUISet.barWindow.w = (std::max)(1L, monitorBounds.right - monitorBounds.left);
		barUISet.barWindow.h = (std::max)(1L, monitorBounds.bottom - monitorBounds.top);
		barUISet.barWindow.pct = 255;
		// 真实窗口范围由首帧 ULW 原子提交，初始化阶段不再短暂覆盖整张屏幕。
		RECT bounds{ monitorBounds.left, monitorBounds.top,
			monitorBounds.left + 1, monitorBounds.top + 1 };
		// 预缩放仅用于首帧前防闪；失败时仍由首帧 ULW 提交真实窗口范围。
		(void)Inkeys::Window::GetService().SetBounds(
			Inkeys::Window::WindowRole::Bar, bounds);

		return true;
	}
	void InitializeUI(BarUISetClass& barUISet)
	{
		Inkeys::UI::Bar::Zoom::Initialize(barUISet);
		// 启动配置已在初始化线程之前发布，首帧直接采用该目标。
		const bool darkStyle = barUISet.barStyle.requestedDarkStyle.load(
			std::memory_order_acquire);
		barUISet.barStyle.darkStyle = darkStyle;
		SetThemeStyleSource(&barUISet.barStyle);

		// 定义主按钮的位置（Inkeys2 兼容模式）
		double mainX, mainY;
		{
			mainX = static_cast<double>(barUISet.barWindow.x + barUISet.barWindow.w - 80 - 50) / barUISet.barStyle.zoom;
			mainY = static_cast<double>(barUISet.barWindow.y + barUISet.barWindow.h - 80 - 200) / barUISet.barStyle.zoom;
		}
		if (Inkeys::UI::StartupPlacementIsFolded(barUISet.startupPlacement))
		{
			const auto snapshot = Inkeys::Display::GetSnapshot();
			const auto* monitor = snapshot ? snapshot->Primary() : nullptr;
			if (monitor)
			{
				const RECT visible = Inkeys::UI::ResolveStartupPlacementBounds(
					barUISet.startupPlacement, monitor->bounds, monitor->workArea,
					BarMainButtonWidthDip + ButtonFrameThicknessDip,
					BarMainButtonHeightDip + ButtonFrameThicknessDip,
					barUISet.barStyle.zoom, barUISet.barStyle.dpiZoom);
				mainX = ((static_cast<double>(visible.left) + visible.right) / 2.0
					- monitor->bounds.left) / barUISet.barStyle.zoom;
				mainY = ((static_cast<double>(visible.top) + visible.bottom) / 2.0
					- monitor->bounds.top) / barUISet.barStyle.zoom;
			}
		}
		// 定义 UI 控件
		{
			// 背景层
			{
				auto word = make_shared<UiWordClass>(700.0, 150.0, 1200.0, 300.0, L"", 30.0, GetThemeColor(ThemeColorEnum::TextPrimary));
				word->content.Initialization(IW(I18nKey.UI.Bar.Errors.TransparentBackgroundWarning));
				word->pct.Initialization(0.0);
				word->enable.Initialization(true);
				barUISet.wordMap[BarUISetWordEnum::BackgroundWarning] = word;
			}

			// 主按钮
			{
				auto superellipse = make_shared<UiSuperellipseClass>(mainX, mainY, 80.0, 80.0, 3.0, 1.0, GetThemeColor(ThemeColorEnum::Surface), GetThemeColor(ThemeColorEnum::SurfaceFrame));
				superellipse->pct.Initialization(BarMainButtonCollapsedFillOpacity);
				superellipse->framePct = UiPctClass(0.18);
				superellipse->frameRendering = UiFrameRenderingEnum::PointLight;
				superellipse->frameLightColor = UiFrameLightColorEnum::PenWhenDrawing;
				superellipse->enable.Initialization(true);
				barUISet.superellipseMap[BarUISetSuperellipseEnum::MainButton] = superellipse;

				{
					auto svg = make_shared<UiSVGClass>(0.0, 0.0, nullopt, nullopt);
					svg->InitializationFromResource(L"UI", L"logo1");
					barUISet.ConfigureLogoAppearance(*svg, BarLogoAppearance::Resolve(0.0, 0.0, 0));
					svg->SetWH(nullopt, 80.0);
					svg->enable.Initialization(true);
					barUISet.svgMap[BarUISetSvgEnum::logo1] = svg;
				}
			}
			// 主栏
			{
				auto shape = make_shared<UiShapeClass>(0.0, 0.0,
					BarMainBarWidthDip, BarMainBarHeightDip,
					BarMainBarCornerRadiusDip, BarMainBarCornerRadiusDip,
					ButtonFrameThicknessDip,
					GetThemeColor(ThemeColorEnum::Surface),
					GetThemeColor(ThemeColorEnum::SurfaceFrame));
				const bool foldedStartup = barUISet.barState.fold;
				shape->pct.Initialization(foldedStartup ? 0.0 : BarMainBarFillOpacity);
				shape->framePct = UiPctClass(foldedStartup ? 0.0 : BarMainBarFrameOpacity);
				shape->frameRendering = UiFrameRenderingEnum::PointLight;
				shape->frameLightColor = UiFrameLightColorEnum::PenWhenDrawing;
				shape->w.mod = UiValueModeEnum::Variable;
				shape->h.mod = UiValueModeEnum::Variable;
				shape->enable.Initialization(true);
				barUISet.shapeMap[BarUISetShapeEnum::MainBar] = shape;

				// 更多浮层沿用属性面板的 Surface、边框与点光风格。
				{
					auto panel = make_shared<UiShapeClass>(
						0.0, 0.0, BarMorePanelCompactWidth,
						BarMorePanelCompactHeight, 8.0 * BarMorePanelCompactScale,
						8.0 * BarMorePanelCompactScale, BarMorePanelCompactScale,
						GetThemeColor(ThemeColorEnum::Surface),
						GetThemeColor(ThemeColorEnum::SurfaceFrame));
					panel->pct.Initialization(0.0);
					panel->framePct = UiPctClass(0.0);
					panel->frameRendering = UiFrameRenderingEnum::PointLight;
					panel->frameLightColor = UiFrameLightColorEnum::PenWhenDrawing;
					panel->w.mod = UiValueModeEnum::Variable;
					panel->h.mod = UiValueModeEnum::Variable;
					panel->enable.Initialization(true);
					barUISet.shapeMap[BarUISetShapeEnum::MorePanel] = panel;

					auto divider = make_shared<UiShapeClass>(
						0.0, 0.0, 1.0, 1.0, 0.5, 0.5, 1.0,
						GetThemeColor(ThemeColorEnum::SurfaceFrame), nullopt);
					divider->pct.Initialization(0.0);
					divider->enable.Initialization(true);
					barUISet.shapeMap[BarUISetShapeEnum::MorePanelDivider] = divider;

					auto close = make_shared<UiShapeClass>(
						0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0,
						GetThemeColor(ThemeColorEnum::PressedFill), nullopt);
					close->pct.Initialization(0.0);
					close->framePct = UiPctClass(0.0);
					close->enable.Initialization(true);
					barUISet.shapeMap[BarUISetShapeEnum::MorePanelCloseHit] = close;

					auto closeSvg = make_shared<UiSVGClass>(
						0.0, 0.0, GetThemeColor(ThemeColorEnum::IconPrimary), nullopt);
					closeSvg->InitializationFromResource(L"UI", L"barCloseSmall");
					closeSvg->SetWH(18.0, 18.0);
					closeSvg->pct.Initialization(0.0);
					closeSvg->enable.Initialization(true);
					barUISet.svgMap[BarUISetSvgEnum::MorePanelClose] = closeSvg;
				}

				// 绘制属性（一级菜单）
				{
					// 初值就是绘制按钮中心处的等比紧凑态，避免首轮展开从旧 60×60 几何起步。
					auto shape = make_shared<UiShapeClass>(
						0.0, 0.0,
						BarDrawAttributeCompactWidth, BarDrawAttributeCompactHeight,
						8.0 * BarDrawAttributeCompactScale,
						8.0 * BarDrawAttributeCompactScale,
						BarDrawAttributeCompactScale,
						GetThemeColor(ThemeColorEnum::Surface),
						GetThemeColor(ThemeColorEnum::SurfaceFrame));
					shape->pct.Initialization(
						BarDrawAttributeSurfaceOpacity);
					shape->framePct = UiPctClass(0.18);
					shape->frameRendering = UiFrameRenderingEnum::PointLight;
					shape->frameLightColor = UiFrameLightColorEnum::PenWhenDrawing;
					shape->w.mod = UiValueModeEnum::Variable;
					shape->h.mod = UiValueModeEnum::Variable;
					shape->enable.Initialization(true);
					barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar] = shape;

					// Color 区域
					{
						// Color 1
						{
							auto shape = make_shared<UiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect1), GetThemeColor(ThemeColorEnum::SwatchFrame));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1] = shape;

							auto svg = make_shared<UiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect1] = svg;
						}
						// Color 2
						{
							auto shape = make_shared<UiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect2), GetThemeColor(ThemeColorEnum::SwatchFrame));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2] = shape;

							auto svg = make_shared<UiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect2] = svg;
						}
						// Color 3
						{
							auto shape = make_shared<UiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect3), GetThemeColor(ThemeColorEnum::SwatchFrame));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3] = shape;

							auto svg = make_shared<UiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect3] = svg;
						}
						// Color 4
						{
							auto shape = make_shared<UiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect4), GetThemeColor(ThemeColorEnum::SwatchFrame));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4] = shape;

							auto svg = make_shared<UiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect4] = svg;
						}
						// Color 5
						{
							auto shape = make_shared<UiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect5), GetThemeColor(ThemeColorEnum::SwatchFrame));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5] = shape;

							auto svg = make_shared<UiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect5] = svg;
						}
						// Color 6
						{
							auto shape = make_shared<UiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect6), GetThemeColor(ThemeColorEnum::SwatchFrame));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6] = shape;

							auto svg = make_shared<UiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect6] = svg;
						}
						// Color 7
						{
							auto shape = make_shared<UiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect7), GetThemeColor(ThemeColorEnum::SwatchFrame));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7] = shape;

							auto svg = make_shared<UiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect7] = svg;
						}
						// Color 8
						{
							auto shape = make_shared<UiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect8), GetThemeColor(ThemeColorEnum::SwatchFrame));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8] = shape;

							auto svg = make_shared<UiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect8] = svg;
						}
						// Color 9
						{
							auto shape = make_shared<UiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect9), GetThemeColor(ThemeColorEnum::SwatchFrame));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9] = shape;

							auto svg = make_shared<UiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect9] = svg;
						}
						// Color 10
						{
							auto shape = make_shared<UiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect10), GetThemeColor(ThemeColorEnum::SwatchFrame));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10] = shape;

							auto svg = make_shared<UiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect10] = svg;
						}
						// Color 11
						{
							auto shape = make_shared<UiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect11), GetThemeColor(ThemeColorEnum::SwatchFrame));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11] = shape;

							auto svg = make_shared<UiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect11] = svg;
						}
						// Color 12 承担命中和圆形点光边框，内容由独立 SVG/色芯控件继承它的动画几何。
						{
							auto shape = make_shared<UiShapeClass>(
								0.0, 0.0, 30.0, 30.0, 15.0, 15.0, 1.0,
								nullopt, GetThemeColor(ThemeColorEnum::SwatchFrame));
							shape->pct.Initialization(0.0);
							shape->framePct = UiPctClass(0.0);
							shape->frameLightPct = UiPctClass(0.0);
							shape->enable.Initialization(true);
							barUISet.shapeMap[
								BarUISetShapeEnum::DrawAttributeBar_ColorSelect12] = shape;

							auto inner = make_shared<UiShapeClass>(
								9.0, 9.0, 12.0, 12.0, 6.0, 6.0, 1.0,
								GetPenColor() & 0x00FFFFFF,
								GetThemeColor(ThemeColorEnum::SwatchFrame));
							inner->pct.Initialization(0.0);
							inner->framePct = UiPctClass(0.0);
							inner->enable.Initialization(true);
							barUISet.shapeMap[
								BarUISetShapeEnum::DrawAttributeBar_ColorSelect12Inner] = inner;

							auto wheel = make_shared<UiPNGClass>(0.0, 0.0);
							wheel->InitializationFromResource(L"PNG", L"colorCustom");
							wheel->SetWH(30.0, 30.0);
							wheel->pct.Initialization(0.0);
							wheel->enable.Initialization(true);
							barUISet.pngMap[
								BarUISetPngEnum::DrawAttributeBar_ColorSelect12Wheel] = wheel;

							// 右下角徽标与预设色完全复用同一个绿勾 SVG。
							auto check = make_shared<UiSVGClass>(
								15.0, 15.0, nullopt, nullopt);
							check->InitializationFromResource(L"UI", L"colorSelect");
							check->SetWH(15.0, 15.0);
							check->pct.Initialization(0.0);
							check->enable.Initialization(true);
							barUISet.svgMap[
								BarUISetSvgEnum::DrawAttributeBar_ColorSelect12Check] = check;

						}
						for (int i = static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ColorSelect1);
							i <= static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ColorSelect12); i++)
						{
							// 颜色块沿用普通边框色与点光参数，但保持 Frame 策略，禁止画笔染色。
							auto shape = barUISet.shapeMap[static_cast<BarUISetShapeEnum>(i)];
							shape->frameRendering = UiFrameRenderingEnum::PointLight;
							shape->framePct = UiPctClass(0.0);
							shape->framePrimaryLightEnabled = false;
							shape->frameCursorLightIntensityScale = BarColorSwatchCursorLightIntensity;
							// 18% 只控制基础灰边；色块仅由鼠标追光随填充进度完整显现。
							shape->frameLightOpacitySource = UiFrameLightOpacitySourceEnum::ObjectPct;
						}

						barUISet.colorPicker.SetLabels({IW(I18nKey.UI.Bar.ColorPicker.RedChannelLabel), IW(I18nKey.UI.Bar.ColorPicker.GreenChannelLabel), IW(I18nKey.UI.Bar.ColorPicker.BlueChannelLabel), IW(I18nKey.UI.Bar.ColorPicker.OpacityLabel)}, IW(I18nKey.UI.Bar.ColorPicker.HoldColorLabel));
						// 反馈层只承载标准按钮状态，原色块/外框仍保留自己的配色与点光。
						for (int i = static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ColorSelect1Feedback);
							i <= static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowInfoFeedback); ++i)
						{
							auto feedback = make_shared<UiShapeClass>(0.0, 0.0, 30.0, 30.0,
								4.0, 4.0, 1.0, GetThemeColor(ThemeColorEnum::PressedFill), nullopt);
							feedback->pct.Initialization(0.0);
							feedback->enable.Initialization(true);
							barUISet.shapeMap[static_cast<BarUISetShapeEnum>(i)] = feedback;
						}

					}
					{ /**/ }
					// 画笔样式区域
					{
						auto InitializePenTypeButton = [&](BarUISetShapeEnum shapeType,
							BarUISetSvgEnum svgType, BarUISetWordEnum wordType,
							const wchar_t* resourceName, const wstring& text)
						{
							auto shape = make_shared<UiShapeClass>(0.0, 0.0, 115.0, 30.0,
								4.0, 4.0, 1.0, GetThemeColor(ThemeColorEnum::PressedFill), nullopt);
							shape->enable.Initialization(true);
							barUISet.shapeMap[shapeType] = shape;

							auto svg = make_shared<UiSVGClass>(0.0, 0.0, GetThemeColor(ThemeColorEnum::IconPrimary), nullopt);
							svg->InitializationFromResource(L"UI", resourceName);
							svg->SetWH(18.0, 18.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[svgType] = svg;

							auto word = make_shared<UiWordClass>(0.0, 0.0, 80.0, 30.0,
								text, 12.0, GetThemeColor(ThemeColorEnum::TextPrimary));
							word->enable.Initialization(true);
							barUISet.wordMap[wordType] = word;
						};
// 从上到下固定为刷子、激光笔、荧光笔、硬笔、软笔。
							// barBrush1=硬笔，barBrush2=软笔；刷子使用独立 barPaintBrush。
							InitializePenTypeButton(BarUISetShapeEnum::DrawAttributeBar_Brush2,
								BarUISetSvgEnum::DrawAttributeBar_Brush2,
								BarUISetWordEnum::DrawAttributeBar_Brush2,
								L"barPaintBrush", IW(I18nKey.UI.Bar.DrawAttributes.BrushLabel));
							InitializePenTypeButton(BarUISetShapeEnum::DrawAttributeBar_Laser,
								BarUISetSvgEnum::DrawAttributeBar_Laser,
								BarUISetWordEnum::DrawAttributeBar_Laser,
								L"barLaser", IW(I18nKey.UI.Bar.DrawAttributes.LaserLabel));
							InitializePenTypeButton(BarUISetShapeEnum::DrawAttributeBar_Highlight1,
								BarUISetSvgEnum::DrawAttributeBar_Highlight1,
								BarUISetWordEnum::DrawAttributeBar_Highlight1,
								L"barHighlighter1", IW(I18nKey.UI.Bar.DrawAttributes.HighlighterLabel));
							InitializePenTypeButton(BarUISetShapeEnum::DrawAttributeBar_Brush1,
								BarUISetSvgEnum::DrawAttributeBar_Brush1,
								BarUISetWordEnum::DrawAttributeBar_Brush1,
								L"barBrush1", IW(I18nKey.UI.Bar.DrawAttributes.HardPenLabel));
							InitializePenTypeButton(BarUISetShapeEnum::DrawAttributeBar_SoftPen,
								BarUISetSvgEnum::DrawAttributeBar_SoftPen,
								BarUISetWordEnum::DrawAttributeBar_SoftPen,
								L"barBrush2", IW(I18nKey.UI.Bar.DrawAttributes.SoftPenLabel));
						}
					// 粗细调节区域
					{
						auto shape = make_shared<UiShapeClass>(0.0, 0.0,
							240.0, BarDrawAttributeThicknessHeight,
							4.0, 4.0, 1.0,
							nullopt,
							GetThemeColor(ThemeColorEnum::SurfaceFrame));
						shape->pct.Initialization(0.0);
						shape->framePct = UiPctClass(0.0);
						shape->frameLightPct = UiPctClass(0.0);
						shape->enable.Initialization(true);
						barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect] = shape;

						// 与几何选择窗口共用 1 DIP / 0.5 DIP / 第三光参数。
						auto divider = make_shared<UiShapeClass>(
							0.0, 0.0, BarUiDividerWidth, BarUiDividerWidth,
							BarUiDividerRadius, BarUiDividerRadius, 1.0,
							GetThemeColor(ThemeColorEnum::SurfaceFrame),
							GetThemeColor(ThemeColorEnum::SurfaceFrame));
						divider->pct.Initialization(0.0);
						divider->framePct = UiPctClass(0.0);
						divider->frameLightPct = UiPctClass(0.0);
						divider->frameRendering = UiFrameRenderingEnum::PointLight;
						divider->frameLightColor = UiFrameLightColorEnum::Frame;
						divider->framePrimaryLightEnabled = false;
						divider->frameCursorLightIntensityScale =
							BarUiDividerCursorLightIntensity;
						divider->enable.Initialization(true);
						barUISet.shapeMap[
							BarUISetShapeEnum::DrawAttributeBar_ThicknessDivider] = divider;

						auto extensionHit = make_shared<UiShapeClass>(
							0.0, 0.0, BarDrawAttributePenTypeExtensionWidth,
							BarDrawAttributePenTypeButtonHeight, 4.0, 4.0, 1.0,
							GetThemeColor(ThemeColorEnum::PressedFill), nullopt);
						extensionHit->pct.Initialization(0.0);
						extensionHit->enable.Initialization(true);
						barUISet.shapeMap[
							BarUISetShapeEnum::DrawAttributeBar_PenTypeExtensionHit] = extensionHit;

						auto extensionDivider = make_shared<UiShapeClass>(
							0.0, 0.0, BarUiDividerWidth, 20.0,
							BarUiDividerRadius, BarUiDividerRadius, 1.0,
							GetThemeColor(ThemeColorEnum::Accent),
							GetThemeColor(ThemeColorEnum::Accent));
						extensionDivider->pct.Initialization(0.0);
						extensionDivider->framePct = UiPctClass(0.0);
						extensionDivider->frameLightPct = UiPctClass(0.0);
						extensionDivider->frameRendering = UiFrameRenderingEnum::PointLight;
						extensionDivider->frameLightColor = UiFrameLightColorEnum::Frame;
						extensionDivider->framePrimaryLightEnabled = false;
						extensionDivider->frameCursorLightIntensityScale =
							BarUiDividerCursorLightIntensity;
						extensionDivider->enable.Initialization(true);
						barUISet.shapeMap[
							BarUISetShapeEnum::DrawAttributeBar_PenTypeExtensionDivider] = extensionDivider;

						auto word = make_shared<UiWordClass>(0.0, 0.0,
							90.0, 30.0, L"", 13.0,
							GetThemeColor(ThemeColorEnum::TextPrimary));
						word->enable.Initialization(true);
						barUISet.wordMap[BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay] = word;

						auto InitializeThicknessButton = [&](BarUISetShapeEnum shapeType)
							{
								auto button = make_shared<UiShapeClass>(
									0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0,
									GetThemeColor(ThemeColorEnum::PressedFill),
									GetThemeColor(ThemeColorEnum::TextPrimary));
								button->pct.Initialization(0.0);
								button->framePct = UiPctClass(0.0);
								button->frameLightPct = UiPctClass(0.0);
								button->frameRendering =
									UiFrameRenderingEnum::PointLight;
								button->framePrimaryLightEnabled = false;
								button->frameCursorLightIntensityScale =
									ButtonCursorLightIntensity;
								button->frameLightCursorLightIntensityScale =
									ButtonLightCursorLightIntensity;
								button->enable.Initialization(true);
								barUISet.shapeMap[shapeType] = button;
							};
						InitializeThicknessButton(
							BarUISetShapeEnum::DrawAttributeBar_ThicknessFine);
						InitializeThicknessButton(
							BarUISetShapeEnum::DrawAttributeBar_ThicknessMedium);
						InitializeThicknessButton(
							BarUISetShapeEnum::DrawAttributeBar_ThicknessCoarse);
						InitializeThicknessButton(
							BarUISetShapeEnum::DrawAttributeBar_ThicknessAdjust);
						auto InitializeThicknessSliderShape =
							[&](BarUISetShapeEnum shapeType)
							{
								auto sliderShape =
									make_shared<UiShapeClass>(
										0.0, 0.0, 1.0, 1.0,
										nullopt, nullopt, nullopt,
										nullopt, nullopt);
								sliderShape->pct.Initialization(0.0);
								sliderShape->enable.Initialization(true);
								barUISet.shapeMap[shapeType] =
									sliderShape;
							};
						InitializeThicknessSliderShape(
							BarUISetShapeEnum::
								DrawAttributeBar_ThicknessSliderHit);
						InitializeThicknessSliderShape(
							BarUISetShapeEnum::
								DrawAttributeBar_ThicknessSliderThumb);
						auto thicknessPreviewCircle =
							make_shared<UiShapeClass>(
								0.0, 0.0, 1.0, 1.0,
								0.5, 0.5, nullopt,
								RGB(255, 255, 255), nullopt);
						thicknessPreviewCircle->pct.Initialization(0.0);
						thicknessPreviewCircle->enable.Initialization(true);
						barUISet.shapeMap[
							BarUISetShapeEnum::
								DrawAttributeBar_ThicknessPreviewPopupCircle] =
							thicknessPreviewCircle;
						auto adjustSvg = make_shared<UiSVGClass>(
							0.0, 0.0,
							GetThemeColor(ThemeColorEnum::IconPrimary), nullopt);
						adjustSvg->InitializationFromResource(
							L"UI", L"barThicknessAdjust");
						adjustSvg->SetWH(18.0, 18.0);
						adjustSvg->pct.Initialization(0.0);
						adjustSvg->enable.Initialization(true);
						barUISet.svgMap[
							BarUISetSvgEnum::DrawAttributeBar_ThicknessAdjust] =
							adjustSvg;

						const BarUISetWordEnum numberWords[] =
						{
							BarUISetWordEnum::DrawAttributeBar_ThicknessFineNumber,
							BarUISetWordEnum::DrawAttributeBar_ThicknessMediumNumber,
							BarUISetWordEnum::DrawAttributeBar_ThicknessCoarseNumber,
						};
						for (auto wordType : numberWords)
						{
							auto numberWord = make_shared<UiWordClass>(
								0.0, 0.0, 30.0, 30.0, L"", 10.0,
								GetThemeColor(ThemeColorEnum::TextPrimary));
							numberWord->pct.Initialization(0.0);
							numberWord->enable.Initialization(true);
							barUISet.wordMap[wordType] = numberWord;
						}

						auto InitializeTooltipSurface =
							[&](BarUISetShapeEnum shapeType, double width, double height)
							{
								auto surface = make_shared<UiShapeClass>(
									0.0, 0.0, width, height, 4.0, 4.0, 1.0,
									GetThemeColor(ThemeColorEnum::Surface),
									GetThemeColor(ThemeColorEnum::SurfaceFrame));
								surface->pct.Initialization(0.0);
								surface->framePct = UiPctClass(0.0);
								surface->frameLightPct = UiPctClass(0.0);
								surface->frameRendering =
									UiFrameRenderingEnum::PointLight;
								surface->frameLightColor =
									UiFrameLightColorEnum::Frame;
								surface->framePrimaryLightEnabled = false;
								surface->frameCursorLightIntensityScale =
									ButtonCursorLightIntensity;
								surface->enable.Initialization(true);
								barUISet.shapeMap[shapeType] = surface;
							};
						auto InitializeTooltipHit =
							[&](BarUISetShapeEnum shapeType, double size)
							{
								auto hit = make_shared<UiShapeClass>(
									0.0, 0.0, size, size, nullopt, nullopt,
									nullopt, nullopt, nullopt);
								hit->pct.Initialization(0.0);
								hit->enable.Initialization(true);
								barUISet.shapeMap[shapeType] = hit;
							};
						auto InitializeTooltipCloseButton =
							[&](BarUISetShapeEnum shapeType)
							{
								auto button = make_shared<UiShapeClass>(
									0.0, 0.0,
									BarThicknessTooltipCloseButtonSize,
									BarThicknessTooltipCloseButtonSize,
									4.0, 4.0, nullopt,
									GetThemeColor(
										ThemeColorEnum::PressedFill),
									nullopt);
								button->pct.Initialization(0.0);
								button->enable.Initialization(true);
								barUISet.shapeMap[shapeType] = button;
							};

						InitializeTooltipHit(
							BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationInfoHit,
							14.0);
						InitializeTooltipSurface(
							BarUISetShapeEnum::
								DrawAttributeBar_ThicknessPreviewPopupSurface,
							1.0, 1.0);
						InitializeTooltipSurface(
							BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowBadge,
							24.0, 24.0);
						InitializeTooltipHit(
							BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowInfoHit,
							14.0);
						InitializeTooltipSurface(
							BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopup,
							1.0, 1.0);
						InitializeTooltipCloseButton(
							BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopupCloseHit);
						InitializeTooltipSurface(
							BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopup,
							1.0, 1.0);
						InitializeTooltipCloseButton(
							BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopupCloseHit);
						InitializeTooltipSurface(
							BarUISetShapeEnum::DrawAttributeBar_PenTypeMenu,
							BarDrawAttributePenTypeButtonWidth,
							BarDrawAttributePenTypeMenuHeight);
						InitializeTooltipHit(
							BarUISetShapeEnum::DrawAttributeBar_PenTypeMenuFreeLine,
							BarDrawAttributePenTypeButtonWidth
							- BarDrawAttributePenTypeMenuPadding * 2.0);
						InitializeTooltipHit(
							BarUISetShapeEnum::DrawAttributeBar_PenTypeMenuAnnotationLine,
							BarDrawAttributePenTypeButtonWidth
							- BarDrawAttributePenTypeMenuPadding * 2.0);

						// 菜单行复用普通按钮的 PressedFill 视觉，禁用行只保留阻挡命中。
						for (auto rowType : {
							BarUISetShapeEnum::DrawAttributeBar_PenTypeMenuFreeLine,
							BarUISetShapeEnum::DrawAttributeBar_PenTypeMenuAnnotationLine })
						{
							auto row = barUISet.shapeMap[rowType];
							row->fill = UiColorClass(
								GetThemeColor(ThemeColorEnum::PressedFill));
							row->rw = UiValueClass(4.0);
							row->rh = UiValueClass(4.0);
							row->ft = UiValueClass(1.0);
							row->pct.Initialization(0.0);
						}

						auto menuFreeWord = make_shared<UiWordClass>(
							0.0, 0.0, 80.0, BarDrawAttributePenTypeMenuRowHeight,
							IW(I18nKey.UI.Bar.DrawAttributes.FreeLineLabel), 12.0,
							GetThemeColor(ThemeColorEnum::TextPrimary));
						menuFreeWord->pct.Initialization(0.0);
						menuFreeWord->enable.Initialization(true);
						barUISet.wordMap[
							BarUISetWordEnum::DrawAttributeBar_PenTypeMenuFreeLine] = menuFreeWord;

						auto extensionArrow = make_shared<UiSVGClass>(
							0.0, 0.0, GetThemeColor(ThemeColorEnum::Accent), nullopt);
						extensionArrow->InitializationFromResource(L"UI", L"barThicknessAdjust");
						extensionArrow->SetWH(18.0, 18.0);
						extensionArrow->pct.Initialization(0.0);
						extensionArrow->enable.Initialization(true);
						barUISet.svgMap[
							BarUISetSvgEnum::DrawAttributeBar_PenTypeExtensionArrow] = extensionArrow;

						auto menuCheck = make_shared<UiSVGClass>(
							0.0, 0.0, GetThemeColor(ThemeColorEnum::Accent), nullopt);
						menuCheck->InitializationFromResource(
							L"UI", L"fluentCheckmark12Filled");
						// SVG 外层为 128px；控件按 12x12 viewBox 的 optical size 显式布局。
						menuCheck->SetWH(
							BarDrawAttributePenTypeMenuCheckSize,
							BarDrawAttributePenTypeMenuCheckSize);
						menuCheck->pct.Initialization(0.0);
						menuCheck->enable.Initialization(true);
						barUISet.svgMap[
							BarUISetSvgEnum::DrawAttributeBar_PenTypeMenuCheck] = menuCheck;

							auto annotationLabel = make_shared<UiWordClass>(
								0.0, 0.0, 80.0, 24.0,
								IW(I18nKey.UI.Bar.DrawAttributes.AnnotationLineLabel), 13.0,
								RGB(200, 200, 200));
							annotationLabel->pct.Initialization(0.0);
							annotationLabel->enable.Initialization(true);
							barUISet.wordMap[
								BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationLabel] =
								annotationLabel;

							auto holdLockLabel = make_shared<UiWordClass>(
								0.0, 0.0, 120.0, 24.0,
								IW(I18nKey.UI.Bar.DrawAttributes.HoldThicknessLabel), 13.0,
								RGB(200, 200, 200));
							holdLockLabel->pct.Initialization(0.0);
							holdLockLabel->enable.Initialization(true);
							barUISet.wordMap[
								BarUISetWordEnum::DrawAttributeBar_ThicknessHoldLockLabel] =
								holdLockLabel;

							auto thicknessPreviewNumber = make_shared<UiWordClass>(
								0.0, 0.0, 30.0, 20.0, L"0",
								BarThicknessPreviewNumberFontSize,
								GetThemeColor(ThemeColorEnum::TextPrimary));
							thicknessPreviewNumber->pct.Initialization(0.0);
							thicknessPreviewNumber->enable.Initialization(true);
							barUISet.wordMap[
								BarUISetWordEnum::
									DrawAttributeBar_ThicknessPreviewPopupNumber] =
								thicknessPreviewNumber;

							COLORREF popupBodyColor = MixUiColor(
								GetThemeColor(ThemeColorEnum::TextPrimary),
								GetThemeColor(ThemeColorEnum::Surface), 0.45);
						auto InitializeTooltipWord =
							[&](BarUISetWordEnum wordType, const wstring& text,
								double size, COLORREF color)
							{
								auto word = make_shared<UiWordClass>(
									0.0, 0.0, 1.0, 1.0, text, size, color);
								word->pct.Initialization(0.0);
								word->enable.Initialization(true);
								barUISet.wordMap[wordType] = word;
							};
						InitializeTooltipWord(
							BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationPopupText,
							IW(I18nKey.UI.Bar.DrawAttributes.AnnotationUnavailableTitle),
							BarThicknessTooltipTitleFontSize,
							GetThemeColor(ThemeColorEnum::TextPrimary));
						InitializeTooltipWord(
							BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationPopupBody,
							IW(I18nKey.UI.Bar.DrawAttributes.AnnotationDescription),
							BarThicknessTooltipBodyFontSize, popupBodyColor);
						InitializeTooltipWord(
							BarUISetWordEnum::DrawAttributeBar_ThicknessOverflowPopupText,
							IW(I18nKey.UI.Bar.DrawAttributes.ThicknessOverflowTitle),
							BarThicknessTooltipTitleFontSize,
							GetThemeColor(ThemeColorEnum::TextPrimary));
						InitializeTooltipWord(
							BarUISetWordEnum::DrawAttributeBar_ThicknessOverflowPopupBody,
							IW(I18nKey.UI.Bar.DrawAttributes.ThicknessOverflowBody),
							BarThicknessTooltipBodyFontSize, popupBodyColor);

						auto InitializeTooltipSvg =
							[&](BarUISetSvgEnum svgType, const wchar_t* resourceName,
								COLORREF color, double size)
							{
								auto svg = make_shared<UiSVGClass>(
									0.0, 0.0, color, nullopt);
								svg->InitializationFromResource(L"UI", resourceName);
								svg->SetWH(size, size);
								svg->pct.Initialization(0.0);
								svg->enable.Initialization(true);
								barUISet.svgMap[svgType] = svg;
							};
						InitializeTooltipSvg(
							BarUISetSvgEnum::DrawAttributeBar_ThicknessAnnotationInfo,
							L"barQuestion", darkStyle ? RGB(200, 200, 200)
								: GetThemeColor(ThemeColorEnum::IconPrimary), 14.0);
						InitializeTooltipSvg(
							BarUISetSvgEnum::DrawAttributeBar_ThicknessOverflowInfo,
							L"barInfo", GetThemeColor(ThemeColorEnum::IconPrimary), 14.0);
						InitializeTooltipSvg(
							BarUISetSvgEnum::DrawAttributeBar_ThicknessAnnotationPopupClose,
							L"barCloseSmall",
							GetThemeColor(ThemeColorEnum::TextPrimary), 14.0);
						InitializeTooltipSvg(
							BarUISetSvgEnum::DrawAttributeBar_ThicknessOverflowPopupClose,
							L"barCloseSmall",
							GetThemeColor(ThemeColorEnum::TextPrimary), 14.0);
					}

					// 几何属性（一级菜单）
					{
						auto panel = make_shared<UiShapeClass>(
							0.0, 0.0,
							BarGeometryAttributeCompactWidth,
							BarGeometryAttributeCompactHeight,
							8.0 * BarGeometryAttributeCompactScale,
							8.0 * BarGeometryAttributeCompactScale,
							BarGeometryAttributeCompactScale,
							GetThemeColor(ThemeColorEnum::Surface),
							GetThemeColor(ThemeColorEnum::SurfaceFrame));
						panel->pct.Initialization(0.0);
						panel->framePct = UiPctClass(0.0);
						panel->frameRendering = UiFrameRenderingEnum::PointLight;
						panel->frameLightColor = UiFrameLightColorEnum::PenWhenDrawing;
						panel->w.mod = UiValueModeEnum::Variable;
						panel->h.mod = UiValueModeEnum::Variable;
						panel->enable.Initialization(true);
						barUISet.shapeMap[BarUISetShapeEnum::GeometryAttributeBar] = panel;

						auto InitializeGeometryButton = [&](BarUISetShapeEnum shapeType,
							BarUISetWordEnum wordType, const wstring& text,
							double size, double fontSize)
							{
								auto button = make_shared<UiShapeClass>(
									0.0, 0.0, size, size, 4.0, 4.0, 1.0,
									GetThemeColor(ThemeColorEnum::PressedFill),
									GetThemeColor(ThemeColorEnum::TextPrimary));
								button->pct.Initialization(0.0);
								button->framePct = UiPctClass(0.0);
								button->frameLightPct = UiPctClass(0.0);
								button->frameRendering = UiFrameRenderingEnum::PointLight;
								button->framePrimaryLightEnabled = false;
								button->frameCursorLightIntensityScale =
									ButtonCursorLightIntensity;
								button->frameLightCursorLightIntensityScale =
									ButtonLightCursorLightIntensity;
								button->enable.Initialization(true);
								barUISet.shapeMap[shapeType] = button;

								auto word = make_shared<UiWordClass>(
									0.0, 0.0, size, size, text, fontSize,
									GetThemeColor(ThemeColorEnum::TextPrimary));
								word->pct.Initialization(0.0);
								word->enable.Initialization(true);
								barUISet.wordMap[wordType] = word;
							};
						InitializeGeometryButton(
							BarUISetShapeEnum::GeometryAttributeBar_StraightLine,
							BarUISetWordEnum::GeometryAttributeBar_StraightLine,
							IW(I18nKey.UI.Bar.GeometryAttributes.StraightLineLabel),
							BarGeometryAttributeShapeButtonSize, 11.0);
						InitializeGeometryButton(
							BarUISetShapeEnum::GeometryAttributeBar_Rectangle,
							BarUISetWordEnum::GeometryAttributeBar_Rectangle,
							IW(I18nKey.UI.Bar.GeometryAttributes.RectangleLabel),
							BarGeometryAttributeShapeButtonSize, 11.0);

						auto InitializeGeometryShapeSvg = [&](BarUISetSvgEnum svgType,
							const wchar_t* resourceName)
							{
								auto icon = make_shared<UiSVGClass>(
									0.0, 0.0,
									GetThemeColor(ThemeColorEnum::IconPrimary),
									GetThemeColor(ThemeColorEnum::IconPrimary));
								icon->InitializationFromResource(L"UI", resourceName);
								icon->SetWH(28.0, 28.0);
								icon->pct.Initialization(0.0);
								icon->enable.Initialization(true);
								barUISet.svgMap[svgType] = icon;
							};
						InitializeGeometryShapeSvg(
							BarUISetSvgEnum::GeometryAttributeBar_StraightLine,
							L"barShapeStraightLine");
						InitializeGeometryShapeSvg(
							BarUISetSvgEnum::GeometryAttributeBar_Rectangle,
							L"barShapeRectangle");

						auto divider = make_shared<UiShapeClass>(
							0.0, 0.0, BarUiDividerWidth, BarUiDividerWidth,
							BarUiDividerRadius, BarUiDividerRadius, 1.0,
							GetThemeColor(ThemeColorEnum::SurfaceFrame),
							GetThemeColor(ThemeColorEnum::SurfaceFrame));
						divider->pct.Initialization(0.0);
						divider->framePct = UiPctClass(0.0);
						divider->frameLightPct = UiPctClass(0.0);
						divider->frameRendering = UiFrameRenderingEnum::PointLight;
						divider->frameLightColor = UiFrameLightColorEnum::Frame;
						divider->framePrimaryLightEnabled = false;
						divider->frameCursorLightIntensityScale =
							BarGeometryAttributeDividerCursorLightIntensity;
						divider->enable.Initialization(true);
						barUISet.shapeMap[
							BarUISetShapeEnum::GeometryAttributeBar_Divider] = divider;

						const BarUISetShapeEnum thicknessShapes[] =
						{
							BarUISetShapeEnum::GeometryAttributeBar_ThicknessFine,
							BarUISetShapeEnum::GeometryAttributeBar_ThicknessMedium,
							BarUISetShapeEnum::GeometryAttributeBar_ThicknessCoarse,
						};
						const BarUISetWordEnum thicknessWords[] =
						{
							BarUISetWordEnum::GeometryAttributeBar_ThicknessFineNumber,
							BarUISetWordEnum::GeometryAttributeBar_ThicknessMediumNumber,
							BarUISetWordEnum::GeometryAttributeBar_ThicknessCoarseNumber,
						};
						for (size_t index = 0; index < 3; ++index)
							InitializeGeometryButton(thicknessShapes[index],
								thicknessWords[index], L"",
								BarGeometryAttributeThicknessButtonSize, 10.0);

						// 关闭按钮与下排粗细按钮共用 30 DIP 交互尺寸和通用点光样式。
						{
							auto close = make_shared<UiShapeClass>(
								0.0, 0.0,
								BarGeometryAttributeThicknessButtonSize,
								BarGeometryAttributeThicknessButtonSize,
								4.0, 4.0, 1.0,
								GetThemeColor(ThemeColorEnum::PressedFill),
								nullopt);
							close->pct.Initialization(0.0);
							close->framePct = UiPctClass(0.0);
							close->frameLightPct = UiPctClass(0.0);
							close->frameRendering = UiFrameRenderingEnum::PointLight;
							close->framePrimaryLightEnabled = false;
							close->frameCursorLightIntensityScale =
								ButtonCursorLightIntensity;
							close->frameLightCursorLightIntensityScale =
								ButtonLightCursorLightIntensity;
							close->enable.Initialization(true);
							barUISet.shapeMap[
								BarUISetShapeEnum::GeometryAttributeBar_Close] = close;

							auto closeSvg = make_shared<UiSVGClass>(
								0.0, 0.0,
								GetThemeColor(ThemeColorEnum::IconPrimary),
								nullopt);
							closeSvg->InitializationFromResource(L"UI", L"barCloseSmall");
							closeSvg->SetWH(18.0, 18.0);
							closeSvg->pct.Initialization(0.0);
							closeSvg->enable.Initialization(true);
							barUISet.svgMap[
								BarUISetSvgEnum::GeometryAttributeBar_Close] = closeSvg;
						}
					}
				}
			}
		}
		const double lightMaterial = darkStyle ? 0.0 : 1.0;
		for (auto& [key, shape] : barUISet.shapeMap)
			shape->lightMaterial = lightMaterial;
		for (auto surface : {
			BarUISetShapeEnum::MainBar, BarUISetShapeEnum::MorePanel,
			BarUISetShapeEnum::DrawAttributeBar,
			BarUISetShapeEnum::DrawAttributeBar_PenTypeMenu,
			BarUISetShapeEnum::DrawAttributeBar_ThicknessPreviewPopupSurface,
			BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopup,
			BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopup,
			BarUISetShapeEnum::GeometryAttributeBar })
		{
			auto shape = barUISet.shapeMap[surface];
			shape->themeSurface = true;
			shape->fill->SetDirect(GetThemeColor(ThemeModeEnum::Dark, ThemeColorEnum::Surface));
			shape->frame->SetDirect(GetThemeColor(ThemeModeEnum::Dark, ThemeColorEnum::SurfaceFrame));
		}
		// 主栏与主按钮常驻在课件上方，浅色端提高填充透明度避免深色背景透出发灰；弹出面板保持原值。
		barUISet.shapeMap[BarUISetShapeEnum::MainBar]->lightFillOpacityScale =
			ThemeMaterial::LightResidentSurfaceOpacityScale;
		barUISet.shapeMap[BarUISetShapeEnum::MainBar]->residentSurface = true;
		auto mainButton = barUISet.superellipseMap[BarUISetSuperellipseEnum::MainButton];
		mainButton->themeSurface = true;
		mainButton->lightFillOpacityScale = ThemeMaterial::LightResidentSurfaceOpacityScale;
		mainButton->residentSurface = true;
		mainButton->lightMaterial = barUISet.barState.fold ? 0.0 : lightMaterial;
		mainButton->fill->SetDirect(GetThemeColor(ThemeModeEnum::Dark, ThemeColorEnum::Surface));
		mainButton->frame->SetDirect(GetThemeColor(ThemeModeEnum::Dark, ThemeColorEnum::SurfaceFrame));

	}
}
