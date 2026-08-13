module;

#include "../../../IdtMain.h"

#include "../../../IdtConfiguration.h"
#include "../../../IdtDisplayManagement.h"
#include "../../../IdtDraw.h"
#include "../../../IdtDrawpad.h"
#include "../../../IdtState.h"
#include "../../Window/Window.Legacy.hpp"

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
import Inkeys.Window;
import Inkeys.UI.RenderPipeline;
// 初始化只读 Main 中的共享布局常量，保持 topology 与 Rendering 数值一致。
extern const double BarButtonCursorLightIntensity;
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
constexpr double BarGeometryAttributeShapeButtonSize = 50.0;
namespace Inkeys::UI::Bar
{
	WNDPROC WindowProc() noexcept
	{
		return barWindowMsgCallback;
	}

	void Initialization()
	{
		Inkeys::Thread::StatusGuard guard("BarInitializationClass::BarInitialization");
		if (offSignal) return;
		// 初始化
		if (!InitializeWindow(barUISet)) return;
		InitializeUI(barUISet);

		barUISet.barMedia.LoadFormat();

		// 初始化 按钮 们
		barUISet.barButtonSet.PresetInitialization();
		barUISet.barButtonSet.RegisterBuiltInComponents();
		{
			barUISet.barButtonSet.Load();
			barUISet.barButtonSet.StateUpdate();
		}

		barUISet.barState.PositionUpdate(barUISet.barStyle.zoom);
		if (offSignal)
			return;

		// Hook 自有 jthread，并在创建它的线程卸载。
		(void)Inkeys::Input::MouseHook::Start([&]()
			{
				barUISet.barState.fold = true;
				barUISet.UpdateRendering(false);
			});
		// Bar 只注册单帧回调，唯一渲染线程由 RenderPipeline 持有。
		barUISet.Rendering();
		thread interactionThread([&]() { barUISet.Interact(); });

		// 等待

		while (!offSignal) this_thread::sleep_for(chrono::milliseconds(100));
		// 先停止输入生产者，再由窗口线程撤销计时器、Raw Input 与 capture。
		Inkeys::Input::MouseHook::Stop();
		// 退出信号与普通渲染请求共用代次通知，唤醒真正休眠的渲染线程。
		BarAtomic::wait.Notify();
		Inkeys::UI::RenderPipeline::WakeForStop();

		if (interactionThread.joinable()) interactionThread.join();
		barUISet.StopRendering();
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
		barUISet.barWindow.w = MainMonitor.MonitorWidth;
		barUISet.barWindow.h = MainMonitor.MonitorHeight - 1;
		barUISet.barWindow.pct = 255;
		// 真实窗口范围由首帧 ULW 原子提交，初始化阶段不再短暂覆盖整张屏幕。
		RECT bounds{ MainMonitor.rcMonitor.left, MainMonitor.rcMonitor.top,
			MainMonitor.rcMonitor.left + 1, MainMonitor.rcMonitor.top + 1 };
		if (!Inkeys::Window::GetService().SetBounds(Inkeys::Window::WindowRole::Bar, bounds))
			return false;

		return true;
	}
	void InitializeUI(BarUISetClass& barUISet)
	{
		Inkeys::UI::Bar::Zoom::Initialize(barUISet);
		SetThemeStyleSource(&barUISet.barStyle);

		// 定义主按钮的位置（Inkeys2 兼容模式）
		double mainX, mainY;
		{
			mainX = static_cast<double>(barUISet.barWindow.x + barUISet.barWindow.w - 80 - 50) / barUISet.barStyle.zoom;
			mainY = static_cast<double>(barUISet.barWindow.y + barUISet.barWindow.h - 80 - 200) / barUISet.barStyle.zoom;
		}
		// 定义 UI 控件
		{
			// 背景层
			{
				auto word = make_shared<BarUiWordClass>(700.0, 150.0, 1200.0, 300.0, L"", 30.0, GetThemeColor(BarThemeColorEnum::TextPrimary));
				word->content.Initialization(L"软件遇到透明背景无法正常显示的故障\n\nexe属性->关闭使用简化的颜色模式\nWindows7用户请开启Aero主题\n\n联系开发者->软件选项主页中\n重启软件试试");
				word->pct.Initialization(0.0);
				word->enable.Initialization(true);
				barUISet.wordMap[BarUISetWordEnum::BackgroundWarning] = word;
			}

			// 主按钮
			{
				auto superellipse = make_shared<BarUiSuperellipseClass>(mainX, mainY, 80.0, 80.0, 3.0, 1.0, GetThemeColor(BarThemeColorEnum::Surface), GetThemeColor(BarThemeColorEnum::SurfaceFrame));
				superellipse->pct.Initialization(0.6);
				superellipse->framePct = BarUiPctClass(0.18);
				superellipse->frameRendering = BarUiFrameRenderingEnum::PointLight;
				superellipse->frameLightColor = BarUiFrameLightColorEnum::PenWhenDrawing;
				superellipse->enable.Initialization(true);
				barUISet.superellipseMap[BarUISetSuperellipseEnum::MainButton] = superellipse;

				{
					auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, nullopt, nullopt);
					svg->InitializationFromResource(L"UI", barUISet.barStyle.darkStyle ? L"logo1" : L"logo2");
					svg->SetWH(nullopt, 80.0);
					svg->enable.Initialization(true);
					barUISet.svgMap[BarUISetSvgEnum::logo1] = svg;
				}
				{
					auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, GetPenColor(), nullopt);
					svg->InitializationFromResource(L"UI", L"Frame94");
					svg->SetWH(nullopt, 80.0);
					svg->pct.Initialization(0.0); // 首帧先隐藏，避免状态计算前闪烁错误颜色。
					svg->enable.Initialization(true);
					barUISet.svgMap[BarUISetSvgEnum::logoInk] = svg;
				}
				{
					// TODO “收起” 文字标识
				}
			}
			// 主栏
			{
				auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 80.0, 80.0, 8.0, 8.0, 1.0, GetThemeColor(BarThemeColorEnum::Surface), GetThemeColor(BarThemeColorEnum::SurfaceFrame));
				shape->pct.Initialization(0.8);
				shape->framePct = BarUiPctClass(0.18);
				shape->frameRendering = BarUiFrameRenderingEnum::PointLight;
				shape->frameLightColor = BarUiFrameLightColorEnum::PenWhenDrawing;
				shape->w.mod = BarUiValueModeEnum::Variable;
				shape->h.mod = BarUiValueModeEnum::Variable;
				shape->enable.Initialization(true);
				barUISet.shapeMap[BarUISetShapeEnum::MainBar] = shape;

				// 更多浮层沿用属性面板的 Surface、边框与点光风格。
				{
					auto panel = make_shared<BarUiShapeClass>(
						0.0, 0.0, BarMorePanelCompactWidth,
						BarMorePanelCompactHeight, 8.0 * BarMorePanelCompactScale,
						8.0 * BarMorePanelCompactScale, BarMorePanelCompactScale,
						GetThemeColor(BarThemeColorEnum::Surface),
						GetThemeColor(BarThemeColorEnum::SurfaceFrame));
					panel->pct.Initialization(0.0);
					panel->framePct = BarUiPctClass(0.0);
					panel->frameRendering = BarUiFrameRenderingEnum::PointLight;
					panel->frameLightColor = BarUiFrameLightColorEnum::PenWhenDrawing;
					panel->w.mod = BarUiValueModeEnum::Variable;
					panel->h.mod = BarUiValueModeEnum::Variable;
					panel->enable.Initialization(true);
					barUISet.shapeMap[BarUISetShapeEnum::MorePanel] = panel;

					auto divider = make_shared<BarUiShapeClass>(
						0.0, 0.0, 1.0, 1.0, 0.5, 0.5, 1.0,
						GetThemeColor(BarThemeColorEnum::SurfaceFrame), nullopt);
					divider->pct.Initialization(0.0);
					divider->enable.Initialization(true);
					barUISet.shapeMap[BarUISetShapeEnum::MorePanelDivider] = divider;

					auto close = make_shared<BarUiShapeClass>(
						0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0,
						GetThemeColor(BarThemeColorEnum::PressedFill), nullopt);
					close->pct.Initialization(0.0);
					close->framePct = BarUiPctClass(0.0);
					close->enable.Initialization(true);
					barUISet.shapeMap[BarUISetShapeEnum::MorePanelCloseHit] = close;

					auto closeSvg = make_shared<BarUiSVGClass>(
						0.0, 0.0, GetThemeColor(BarThemeColorEnum::TextPrimary), nullopt);
					closeSvg->InitializationFromResource(L"UI", L"barCloseSmall");
					closeSvg->SetWH(18.0, 18.0);
					closeSvg->pct.Initialization(0.0);
					closeSvg->enable.Initialization(true);
					barUISet.svgMap[BarUISetSvgEnum::MorePanelClose] = closeSvg;
				}

				// 绘制属性（一级菜单）
				{
					// 初值就是绘制按钮中心处的等比紧凑态，避免首轮展开从旧 60×60 几何起步。
					auto shape = make_shared<BarUiShapeClass>(
						0.0, 0.0,
						BarDrawAttributeCompactWidth, BarDrawAttributeCompactHeight,
						8.0 * BarDrawAttributeCompactScale,
						8.0 * BarDrawAttributeCompactScale,
						BarDrawAttributeCompactScale,
						GetThemeColor(BarThemeColorEnum::Surface),
						GetThemeColor(BarThemeColorEnum::SurfaceFrame));
					shape->pct.Initialization(
						BarDrawAttributeSurfaceOpacity);
					shape->framePct = BarUiPctClass(0.18);
					shape->frameRendering = BarUiFrameRenderingEnum::PointLight;
					shape->frameLightColor = BarUiFrameLightColorEnum::PenWhenDrawing;
					shape->w.mod = BarUiValueModeEnum::Variable;
					shape->h.mod = BarUiValueModeEnum::Variable;
					shape->enable.Initialization(true);
					barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar] = shape;

					// Color 区域
					{
						// Color 1
						{
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect1), GetThemeColor(BarThemeColorEnum::SurfaceFrame));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect1] = shape;

							auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect1] = svg;
						}
						// Color 2
						{
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect2), GetThemeColor(BarThemeColorEnum::SurfaceFrame));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect2] = shape;

							auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect2] = svg;
						}
						// Color 3
						{
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect3), GetThemeColor(BarThemeColorEnum::SurfaceFrame));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect3] = shape;

							auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect3] = svg;
						}
						// Color 4
						{
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect4), GetThemeColor(BarThemeColorEnum::SurfaceFrame));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect4] = shape;

							auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect4] = svg;
						}
						// Color 5
						{
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect5), GetThemeColor(BarThemeColorEnum::SurfaceFrame));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect5] = shape;

							auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect5] = svg;
						}
						// Color 6
						{
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect6), GetThemeColor(BarThemeColorEnum::SurfaceFrame));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect6] = shape;

							auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect6] = svg;
						}
						// Color 7
						{
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect7), GetThemeColor(BarThemeColorEnum::SurfaceFrame));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect7] = shape;

							auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect7] = svg;
						}
						// Color 8
						{
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect8), GetThemeColor(BarThemeColorEnum::SurfaceFrame));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect8] = shape;

							auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect8] = svg;
						}
						// Color 9
						{
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect9), GetThemeColor(BarThemeColorEnum::SurfaceFrame));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect9] = shape;

							auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect9] = svg;
						}
						// Color 10
						{
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect10), GetThemeColor(BarThemeColorEnum::SurfaceFrame));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect10] = shape;

							auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect10] = svg;
						}
						// Color 11
						{
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0, GetPresetColor(BarThemePresetColorEnum::ColorSelect11), GetThemeColor(BarThemeColorEnum::SurfaceFrame));
							shape->enable.Initialization(true);
							barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ColorSelect11] = shape;

							auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, nullopt, nullopt);
							svg->InitializationFromResource(L"UI", L"colorSelect");
							svg->SetWH(15.0, 15.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[BarUISetSvgEnum::DrawAttributeBar_ColorSelect11] = svg;
						}
						// Color 12 承担命中和圆形点光边框，内容由独立 SVG/色芯控件继承它的动画几何。
						{
							auto shape = make_shared<BarUiShapeClass>(
								0.0, 0.0, 30.0, 30.0, 15.0, 15.0, 1.0,
								nullopt, GetThemeColor(BarThemeColorEnum::SurfaceFrame));
							shape->pct.Initialization(0.0);
							shape->framePct = BarUiPctClass(0.0);
							shape->frameLightPct = BarUiPctClass(0.0);
							shape->enable.Initialization(true);
							barUISet.shapeMap[
								BarUISetShapeEnum::DrawAttributeBar_ColorSelect12] = shape;

							auto inner = make_shared<BarUiShapeClass>(
								9.0, 9.0, 12.0, 12.0, 6.0, 6.0, 1.0,
								GetPenColor() & 0x00FFFFFF,
								GetThemeColor(BarThemeColorEnum::SurfaceFrame));
							inner->pct.Initialization(0.0);
							inner->framePct = BarUiPctClass(0.0);
							inner->enable.Initialization(true);
							barUISet.shapeMap[
								BarUISetShapeEnum::DrawAttributeBar_ColorSelect12Inner] = inner;

							auto wheel = make_shared<BarUiPNGClass>(0.0, 0.0);
							wheel->InitializationFromResource(L"PNG", L"colorCustom");
							wheel->SetWH(30.0, 30.0);
							wheel->pct.Initialization(0.0);
							wheel->enable.Initialization(true);
							barUISet.pngMap[
								BarUISetPngEnum::DrawAttributeBar_ColorSelect12Wheel] = wheel;

							// 右下角徽标与预设色完全复用同一个绿勾 SVG。
							auto check = make_shared<BarUiSVGClass>(
								15.0, 15.0, nullopt, nullopt);
							check->InitializationFromResource(L"UI", L"colorSelect");
							check->SetWH(15.0, 15.0);
							check->pct.Initialization(0.0);
							check->enable.Initialization(true);
							barUISet.svgMap[
								BarUISetSvgEnum::DrawAttributeBar_ColorSelect12Check] = check;

							// 色系切换按钮改为太阳/月亮纯图标。
							auto toneSun = make_shared<BarUiSVGClass>(
								0.0, 0.0,
								GetThemeColor(BarThemeColorEnum::TextPrimary), nullopt);
							toneSun->InitializationFromResource(L"UI", L"colorSun");
							toneSun->SetWH(20.0, 20.0);
							toneSun->pct.Initialization(0.0);
							toneSun->enable.Initialization(true);
							barUISet.svgMap[
								BarUISetSvgEnum::DrawAttributeBar_ColorPickerToneSun] = toneSun;

							auto toneMoon = make_shared<BarUiSVGClass>(
								0.0, 0.0,
								GetThemeColor(BarThemeColorEnum::TextPrimary), nullopt);
							toneMoon->InitializationFromResource(L"UI", L"colorMoon");
							toneMoon->SetWH(20.0, 20.0);
							toneMoon->pct.Initialization(0.0);
							toneMoon->enable.Initialization(true);
							barUISet.svgMap[
								BarUISetSvgEnum::DrawAttributeBar_ColorPickerToneMoon] = toneMoon;
						}
						for (int i = static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ColorSelect1);
							i <= static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ColorSelect12); i++)
						{
							// 颜色块沿用普通边框色与点光参数，但保持 Frame 策略，禁止画笔染色。
							auto shape = barUISet.shapeMap[static_cast<BarUISetShapeEnum>(i)];
							shape->frameRendering = BarUiFrameRenderingEnum::PointLight;
							shape->framePct = BarUiPctClass(0.0);
							shape->framePrimaryLightEnabled = false;
							shape->frameCursorLightIntensityScale = BarColorSwatchCursorLightIntensity;
							// 18% 只控制基础灰边；色块仅由鼠标追光随填充进度完整显现。
							shape->frameLightOpacitySource = BarUiFrameLightOpacitySourceEnum::ObjectPct;
						}

						auto InitializePickerHit = [&](BarUISetShapeEnum type,
							optional<double> radius = nullopt,
							optional<COLORREF> fill = nullopt)
							{
								auto hit = make_shared<BarUiShapeClass>(
									0.0, 0.0, 1.0, 1.0,
									radius, radius, 1.0, fill, nullopt);
								hit->pct.Initialization(0.0);
								hit->enable.Initialization(true);
								barUISet.shapeMap[type] = hit;
							};
						{
							auto pickerPanel = make_shared<BarUiShapeClass>(
								0.0, 0.0, 1.0, 1.0, 8.0, 8.0, 1.0,
								GetThemeColor(BarThemeColorEnum::Surface),
								GetThemeColor(BarThemeColorEnum::SurfaceFrame));
							pickerPanel->pct.Initialization(0.0);
							pickerPanel->framePct = BarUiPctClass(0.0);
							pickerPanel->frameLightPct = BarUiPctClass(0.0);
							pickerPanel->frameRendering = BarUiFrameRenderingEnum::PointLight;
							pickerPanel->frameLightColor = BarUiFrameLightColorEnum::PenWhenDrawing;
							// 面板边缘同时接受第一光源与第三光源。
							pickerPanel->framePrimaryLightEnabled = true;
							pickerPanel->frameCursorLightIntensityScale =
								BarButtonCursorLightIntensity;
							pickerPanel->enable.Initialization(true);
							barUISet.shapeMap[
								BarUISetShapeEnum::DrawAttributeBar_ColorPickerPanel] = pickerPanel;
						}
						InitializePickerHit(
							BarUISetShapeEnum::DrawAttributeBar_ColorPickerPalette, 4.0);
						InitializePickerHit(
							BarUISetShapeEnum::DrawAttributeBar_ColorPickerToneToggle,
							4.0, GetThemeColor(BarThemeColorEnum::PressedFill));
						InitializePickerHit(
							BarUISetShapeEnum::DrawAttributeBar_ColorPickerCloseHit,
							4.0, GetThemeColor(BarThemeColorEnum::PressedFill));
						InitializePickerHit(
							BarUISetShapeEnum::DrawAttributeBar_ColorPickerPreviewBubble,
							4.0, GetPenColor() & 0x00FFFFFF);
						InitializePickerHit(
							BarUISetShapeEnum::DrawAttributeBar_ColorPickerHoldHint,
							4.0, GetThemeColor(BarThemeColorEnum::Surface));

						auto InitializePickerWord = [&](BarUISetWordEnum type,
							const wchar_t* text, double size)
							{
								auto word = make_shared<BarUiWordClass>(
									0.0, 0.0, 1.0, 1.0, text, size,
									GetThemeColor(BarThemeColorEnum::TextPrimary));
								word->pct.Initialization(0.0);
								word->enable.Initialization(true);
								barUISet.wordMap[type] = word;
							};
						InitializePickerWord(
							BarUISetWordEnum::DrawAttributeBar_ColorPickerRgb,
							L"R", 13.0);
						InitializePickerWord(
							BarUISetWordEnum::DrawAttributeBar_ColorPickerG,
							L"G", 13.0);
						InitializePickerWord(
							BarUISetWordEnum::DrawAttributeBar_ColorPickerB,
							L"B", 13.0);
						InitializePickerWord(
							BarUISetWordEnum::DrawAttributeBar_ColorPickerOpacity,
							L"透明度", 13.0);
						InitializePickerWord(
							BarUISetWordEnum::DrawAttributeBar_ColorPickerRgbValue,
							L"0", 13.0);
						InitializePickerWord(
							BarUISetWordEnum::DrawAttributeBar_ColorPickerGValue,
							L"0", 13.0);
						InitializePickerWord(
							BarUISetWordEnum::DrawAttributeBar_ColorPickerBValue,
							L"0", 13.0);
						InitializePickerWord(
							BarUISetWordEnum::DrawAttributeBar_ColorPickerOpacityValue,
							L"100%", 13.0);
						InitializePickerWord(
							BarUISetWordEnum::DrawAttributeBar_ColorPickerHoldLabel,
							L"保持并固定颜色", 12.0);
					}
					{ /**/ }
					// 画笔样式区域
					{
						auto InitializePenTypeButton = [&](BarUISetShapeEnum shapeType,
							BarUISetSvgEnum svgType, BarUISetWordEnum wordType,
							const wchar_t* resourceName, const wchar_t* text)
						{
							auto shape = make_shared<BarUiShapeClass>(0.0, 0.0, 115.0, 30.0,
								4.0, 4.0, 1.0, GetThemeColor(BarThemeColorEnum::PressedFill), nullopt);
							shape->enable.Initialization(true);
							barUISet.shapeMap[shapeType] = shape;

							auto svg = make_shared<BarUiSVGClass>(0.0, 0.0, GetThemeColor(BarThemeColorEnum::TextPrimary), nullopt);
							svg->InitializationFromResource(L"UI", resourceName);
							svg->SetWH(18.0, 18.0);
							svg->enable.Initialization(true);
							barUISet.svgMap[svgType] = svg;

							auto word = make_shared<BarUiWordClass>(0.0, 0.0, 80.0, 30.0,
								text, 12.0, GetThemeColor(BarThemeColorEnum::TextPrimary));
							word->enable.Initialization(true);
							barUISet.wordMap[wordType] = word;
						};
// 从上到下固定为刷子、激光笔、荧光笔、硬笔、软笔。
							// barBrush1=硬笔，barBrush2=软笔；刷子使用独立 barPaintBrush。
							InitializePenTypeButton(BarUISetShapeEnum::DrawAttributeBar_Brush2,
								BarUISetSvgEnum::DrawAttributeBar_Brush2,
								BarUISetWordEnum::DrawAttributeBar_Brush2,
								L"barPaintBrush", L"刷子");
							InitializePenTypeButton(BarUISetShapeEnum::DrawAttributeBar_Laser,
								BarUISetSvgEnum::DrawAttributeBar_Laser,
								BarUISetWordEnum::DrawAttributeBar_Laser,
								L"barLaser", L"激光笔");
							InitializePenTypeButton(BarUISetShapeEnum::DrawAttributeBar_Highlight1,
								BarUISetSvgEnum::DrawAttributeBar_Highlight1,
								BarUISetWordEnum::DrawAttributeBar_Highlight1,
								L"barHighlighter1", L"荧光笔");
							InitializePenTypeButton(BarUISetShapeEnum::DrawAttributeBar_Brush1,
								BarUISetSvgEnum::DrawAttributeBar_Brush1,
								BarUISetWordEnum::DrawAttributeBar_Brush1,
								L"barBrush1", L"硬笔");
							InitializePenTypeButton(BarUISetShapeEnum::DrawAttributeBar_SoftPen,
								BarUISetSvgEnum::DrawAttributeBar_SoftPen,
								BarUISetWordEnum::DrawAttributeBar_SoftPen,
								L"barBrush2", L"软笔");
						}
					// 粗细调节区域
					{
						auto shape = make_shared<BarUiShapeClass>(0.0, 0.0,
							240.0, BarDrawAttributeThicknessHeight,
							4.0, 4.0, 1.0,
							nullopt,
							GetThemeColor(BarThemeColorEnum::SurfaceFrame));
						shape->pct.Initialization(0.0);
						shape->framePct = BarUiPctClass(0.0);
						shape->frameLightPct = BarUiPctClass(0.0);
						shape->enable.Initialization(true);
						barUISet.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect] = shape;

						// 与几何选择窗口共用 1 DIP / 0.5 DIP / 第三光参数。
						auto divider = make_shared<BarUiShapeClass>(
							0.0, 0.0, BarUiDividerWidth, BarUiDividerWidth,
							BarUiDividerRadius, BarUiDividerRadius, 1.0,
							GetThemeColor(BarThemeColorEnum::SurfaceFrame),
							GetThemeColor(BarThemeColorEnum::SurfaceFrame));
						divider->pct.Initialization(0.0);
						divider->framePct = BarUiPctClass(0.0);
						divider->frameLightPct = BarUiPctClass(0.0);
						divider->frameRendering = BarUiFrameRenderingEnum::PointLight;
						divider->frameLightColor = BarUiFrameLightColorEnum::Frame;
						divider->framePrimaryLightEnabled = false;
						divider->frameCursorLightIntensityScale =
							BarUiDividerCursorLightIntensity;
						divider->enable.Initialization(true);
						barUISet.shapeMap[
							BarUISetShapeEnum::DrawAttributeBar_ThicknessDivider] = divider;

						auto extensionHit = make_shared<BarUiShapeClass>(
							0.0, 0.0, BarDrawAttributePenTypeExtensionWidth,
							BarDrawAttributePenTypeButtonHeight, 4.0, 4.0, 1.0,
							GetThemeColor(BarThemeColorEnum::PressedFill), nullopt);
						extensionHit->pct.Initialization(0.0);
						extensionHit->enable.Initialization(true);
						barUISet.shapeMap[
							BarUISetShapeEnum::DrawAttributeBar_PenTypeExtensionHit] = extensionHit;

						auto extensionDivider = make_shared<BarUiShapeClass>(
							0.0, 0.0, BarUiDividerWidth, 20.0,
							BarUiDividerRadius, BarUiDividerRadius, 1.0,
							GetThemeColor(BarThemeColorEnum::Accent),
							GetThemeColor(BarThemeColorEnum::Accent));
						extensionDivider->pct.Initialization(0.0);
						extensionDivider->framePct = BarUiPctClass(0.0);
						extensionDivider->frameLightPct = BarUiPctClass(0.0);
						extensionDivider->frameRendering = BarUiFrameRenderingEnum::PointLight;
						extensionDivider->frameLightColor = BarUiFrameLightColorEnum::Frame;
						extensionDivider->framePrimaryLightEnabled = false;
						extensionDivider->frameCursorLightIntensityScale =
							BarUiDividerCursorLightIntensity;
						extensionDivider->enable.Initialization(true);
						barUISet.shapeMap[
							BarUISetShapeEnum::DrawAttributeBar_PenTypeExtensionDivider] = extensionDivider;

						auto word = make_shared<BarUiWordClass>(0.0, 0.0,
							90.0, 30.0, L"", 13.0,
							GetThemeColor(BarThemeColorEnum::TextPrimary));
						word->enable.Initialization(true);
						barUISet.wordMap[BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay] = word;

						auto InitializeThicknessButton = [&](BarUISetShapeEnum shapeType)
							{
								auto button = make_shared<BarUiShapeClass>(
									0.0, 0.0, 30.0, 30.0, 4.0, 4.0, 1.0,
									GetThemeColor(BarThemeColorEnum::PressedFill),
									GetThemeColor(BarThemeColorEnum::TextPrimary));
								button->pct.Initialization(0.0);
								button->framePct = BarUiPctClass(0.0);
								button->frameLightPct = BarUiPctClass(0.0);
								button->frameRendering =
									BarUiFrameRenderingEnum::PointLight;
								button->framePrimaryLightEnabled = false;
								button->frameCursorLightIntensityScale =
									BarButtonCursorLightIntensity;
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
									make_shared<BarUiShapeClass>(
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
							make_shared<BarUiShapeClass>(
								0.0, 0.0, 1.0, 1.0,
								0.5, 0.5, nullopt,
								RGB(255, 255, 255), nullopt);
						thicknessPreviewCircle->pct.Initialization(0.0);
						thicknessPreviewCircle->enable.Initialization(true);
						barUISet.shapeMap[
							BarUISetShapeEnum::
								DrawAttributeBar_ThicknessPreviewPopupCircle] =
							thicknessPreviewCircle;
						auto adjustSvg = make_shared<BarUiSVGClass>(
							0.0, 0.0,
							GetThemeColor(BarThemeColorEnum::TextPrimary), nullopt);
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
							auto numberWord = make_shared<BarUiWordClass>(
								0.0, 0.0, 30.0, 30.0, L"", 10.0,
								GetThemeColor(BarThemeColorEnum::TextPrimary));
							numberWord->pct.Initialization(0.0);
							numberWord->enable.Initialization(true);
							barUISet.wordMap[wordType] = numberWord;
						}

						auto InitializeTooltipSurface =
							[&](BarUISetShapeEnum shapeType, double width, double height)
							{
								auto surface = make_shared<BarUiShapeClass>(
									0.0, 0.0, width, height, 4.0, 4.0, 1.0,
									GetThemeColor(BarThemeColorEnum::Surface),
									GetThemeColor(BarThemeColorEnum::SurfaceFrame));
								surface->pct.Initialization(0.0);
								surface->framePct = BarUiPctClass(0.0);
								surface->frameLightPct = BarUiPctClass(0.0);
								surface->frameRendering =
									BarUiFrameRenderingEnum::PointLight;
								surface->frameLightColor =
									BarUiFrameLightColorEnum::Frame;
								surface->framePrimaryLightEnabled = false;
								surface->frameCursorLightIntensityScale =
									BarButtonCursorLightIntensity;
								surface->enable.Initialization(true);
								barUISet.shapeMap[shapeType] = surface;
							};
						auto InitializeTooltipHit =
							[&](BarUISetShapeEnum shapeType, double size)
							{
								auto hit = make_shared<BarUiShapeClass>(
									0.0, 0.0, size, size, nullopt, nullopt,
									nullopt, nullopt, nullopt);
								hit->pct.Initialization(0.0);
								hit->enable.Initialization(true);
								barUISet.shapeMap[shapeType] = hit;
							};
						auto InitializeTooltipCloseButton =
							[&](BarUISetShapeEnum shapeType)
							{
								auto button = make_shared<BarUiShapeClass>(
									0.0, 0.0,
									BarThicknessTooltipCloseButtonSize,
									BarThicknessTooltipCloseButtonSize,
									4.0, 4.0, nullopt,
									GetThemeColor(
										BarThemeColorEnum::PressedFill),
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
							row->fill = BarUiColorClass(
								GetThemeColor(BarThemeColorEnum::PressedFill));
							row->rw = BarUiValueClass(4.0);
							row->rh = BarUiValueClass(4.0);
							row->ft = BarUiValueClass(1.0);
							row->pct.Initialization(0.0);
						}

						auto menuFreeWord = make_shared<BarUiWordClass>(
							0.0, 0.0, 80.0, BarDrawAttributePenTypeMenuRowHeight,
							L"自由线", 12.0,
							GetThemeColor(BarThemeColorEnum::TextPrimary));
						menuFreeWord->pct.Initialization(0.0);
						menuFreeWord->enable.Initialization(true);
						barUISet.wordMap[
							BarUISetWordEnum::DrawAttributeBar_PenTypeMenuFreeLine] = menuFreeWord;

						auto extensionArrow = make_shared<BarUiSVGClass>(
							0.0, 0.0, GetThemeColor(BarThemeColorEnum::Accent), nullopt);
						extensionArrow->InitializationFromResource(L"UI", L"barThicknessAdjust");
						extensionArrow->SetWH(18.0, 18.0);
						extensionArrow->pct.Initialization(0.0);
						extensionArrow->enable.Initialization(true);
						barUISet.svgMap[
							BarUISetSvgEnum::DrawAttributeBar_PenTypeExtensionArrow] = extensionArrow;

						auto menuCheck = make_shared<BarUiSVGClass>(
							0.0, 0.0, GetThemeColor(BarThemeColorEnum::Accent), nullopt);
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

auto annotationLabel = make_shared<BarUiWordClass>(
								0.0, 0.0, 48.0, 24.0, L"标注线", 13.0,
								RGB(200, 200, 200));
							annotationLabel->pct.Initialization(0.0);
							annotationLabel->enable.Initialization(true);
							barUISet.wordMap[
								BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationLabel] =
								annotationLabel;

							auto holdLockLabel = make_shared<BarUiWordClass>(
								0.0, 0.0, 120.0, 24.0, L"保持并固定粗细", 13.0,
								RGB(200, 200, 200));
							holdLockLabel->pct.Initialization(0.0);
							holdLockLabel->enable.Initialization(true);
							barUISet.wordMap[
								BarUISetWordEnum::DrawAttributeBar_ThicknessHoldLockLabel] =
								holdLockLabel;

							auto thicknessPreviewNumber = make_shared<BarUiWordClass>(
								0.0, 0.0, 30.0, 20.0, L"0",
								BarThicknessPreviewNumberFontSize,
								GetThemeColor(BarThemeColorEnum::TextPrimary));
							thicknessPreviewNumber->pct.Initialization(0.0);
							thicknessPreviewNumber->enable.Initialization(true);
							barUISet.wordMap[
								BarUISetWordEnum::
									DrawAttributeBar_ThicknessPreviewPopupNumber] =
								thicknessPreviewNumber;

							COLORREF popupBodyColor = MixBarUiColor(
								GetThemeColor(BarThemeColorEnum::TextPrimary),
								GetThemeColor(BarThemeColorEnum::Surface), 0.45);
						auto InitializeTooltipWord =
							[&](BarUISetWordEnum wordType, const wchar_t* text,
								double size, COLORREF color)
							{
								auto word = make_shared<BarUiWordClass>(
									0.0, 0.0, 1.0, 1.0, text, size, color);
								word->pct.Initialization(0.0);
								word->enable.Initialization(true);
								barUISet.wordMap[wordType] = word;
							};
						InitializeTooltipWord(
							BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationPopupText,
							L"启用标注线（暂不可用）",
							BarThicknessTooltipTitleFontSize,
							GetThemeColor(BarThemeColorEnum::TextPrimary));
						InitializeTooltipWord(
							BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationPopupBody,
							L"锁定绘制方向仅为水平、竖直或斜45°",
							BarThicknessTooltipBodyFontSize, popupBodyColor);
						InitializeTooltipWord(
							BarUISetWordEnum::DrawAttributeBar_ThicknessOverflowPopupText,
							L"墨迹粗细超出预览范围",
							BarThicknessTooltipTitleFontSize,
							GetThemeColor(BarThemeColorEnum::TextPrimary));
						InitializeTooltipWord(
							BarUISetWordEnum::DrawAttributeBar_ThicknessOverflowPopupBody,
							L"预览中的粗细可能与绘制粗细不一致。",
							BarThicknessTooltipBodyFontSize, popupBodyColor);

						auto InitializeTooltipSvg =
							[&](BarUISetSvgEnum svgType, const wchar_t* resourceName,
								COLORREF color, double size)
							{
								auto svg = make_shared<BarUiSVGClass>(
									0.0, 0.0, color, nullopt);
								svg->InitializationFromResource(L"UI", resourceName);
								svg->SetWH(size, size);
								svg->pct.Initialization(0.0);
								svg->enable.Initialization(true);
								barUISet.svgMap[svgType] = svg;
							};
						InitializeTooltipSvg(
							BarUISetSvgEnum::DrawAttributeBar_ThicknessAnnotationInfo,
							L"barQuestion", RGB(200, 200, 200), 14.0);
						InitializeTooltipSvg(
							BarUISetSvgEnum::DrawAttributeBar_ThicknessOverflowInfo,
							L"barInfo", RGB(255, 255, 255), 14.0);
						InitializeTooltipSvg(
							BarUISetSvgEnum::DrawAttributeBar_ThicknessAnnotationPopupClose,
							L"barCloseSmall",
							GetThemeColor(BarThemeColorEnum::TextPrimary), 14.0);
						InitializeTooltipSvg(
							BarUISetSvgEnum::DrawAttributeBar_ThicknessOverflowPopupClose,
							L"barCloseSmall",
							GetThemeColor(BarThemeColorEnum::TextPrimary), 14.0);
					}

					// 几何属性（一级菜单）
					{
						auto panel = make_shared<BarUiShapeClass>(
							0.0, 0.0,
							BarGeometryAttributeCompactWidth,
							BarGeometryAttributeCompactHeight,
							8.0 * BarGeometryAttributeCompactScale,
							8.0 * BarGeometryAttributeCompactScale,
							BarGeometryAttributeCompactScale,
							GetThemeColor(BarThemeColorEnum::Surface),
							GetThemeColor(BarThemeColorEnum::SurfaceFrame));
						panel->pct.Initialization(0.0);
						panel->framePct = BarUiPctClass(0.0);
						panel->frameRendering = BarUiFrameRenderingEnum::PointLight;
						panel->frameLightColor = BarUiFrameLightColorEnum::PenWhenDrawing;
						panel->w.mod = BarUiValueModeEnum::Variable;
						panel->h.mod = BarUiValueModeEnum::Variable;
						panel->enable.Initialization(true);
						barUISet.shapeMap[BarUISetShapeEnum::GeometryAttributeBar] = panel;

						auto InitializeGeometryButton = [&](BarUISetShapeEnum shapeType,
							BarUISetWordEnum wordType, const wchar_t* text,
							double size, double fontSize)
							{
								auto button = make_shared<BarUiShapeClass>(
									0.0, 0.0, size, size, 4.0, 4.0, 1.0,
									GetThemeColor(BarThemeColorEnum::PressedFill),
									GetThemeColor(BarThemeColorEnum::TextPrimary));
								button->pct.Initialization(0.0);
								button->framePct = BarUiPctClass(0.0);
								button->frameLightPct = BarUiPctClass(0.0);
								button->frameRendering = BarUiFrameRenderingEnum::PointLight;
								button->framePrimaryLightEnabled = false;
								button->frameCursorLightIntensityScale =
									BarButtonCursorLightIntensity;
								button->enable.Initialization(true);
								barUISet.shapeMap[shapeType] = button;

								auto word = make_shared<BarUiWordClass>(
									0.0, 0.0, size, size, text, fontSize,
									GetThemeColor(BarThemeColorEnum::TextPrimary));
								word->pct.Initialization(0.0);
								word->enable.Initialization(true);
								barUISet.wordMap[wordType] = word;
							};
						InitializeGeometryButton(
							BarUISetShapeEnum::GeometryAttributeBar_StraightLine,
							BarUISetWordEnum::GeometryAttributeBar_StraightLine,
							L"直线", BarGeometryAttributeShapeButtonSize, 11.0);
						InitializeGeometryButton(
							BarUISetShapeEnum::GeometryAttributeBar_Rectangle,
							BarUISetWordEnum::GeometryAttributeBar_Rectangle,
							L"矩形", BarGeometryAttributeShapeButtonSize, 11.0);

						auto InitializeGeometryShapeSvg = [&](BarUISetSvgEnum svgType,
							const wchar_t* resourceName)
							{
								auto icon = make_shared<BarUiSVGClass>(
									0.0, 0.0,
									GetThemeColor(BarThemeColorEnum::TextPrimary),
									GetThemeColor(BarThemeColorEnum::TextPrimary));
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

						auto divider = make_shared<BarUiShapeClass>(
							0.0, 0.0, BarUiDividerWidth, BarUiDividerWidth,
							BarUiDividerRadius, BarUiDividerRadius, 1.0,
							GetThemeColor(BarThemeColorEnum::SurfaceFrame),
							GetThemeColor(BarThemeColorEnum::SurfaceFrame));
						divider->pct.Initialization(0.0);
						divider->framePct = BarUiPctClass(0.0);
						divider->frameLightPct = BarUiPctClass(0.0);
						divider->frameRendering = BarUiFrameRenderingEnum::PointLight;
						divider->frameLightColor = BarUiFrameLightColorEnum::Frame;
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
							auto close = make_shared<BarUiShapeClass>(
								0.0, 0.0,
								BarGeometryAttributeThicknessButtonSize,
								BarGeometryAttributeThicknessButtonSize,
								4.0, 4.0, 1.0,
								GetThemeColor(BarThemeColorEnum::PressedFill),
								nullopt);
							close->pct.Initialization(0.0);
							close->framePct = BarUiPctClass(0.0);
							close->frameLightPct = BarUiPctClass(0.0);
							close->frameRendering = BarUiFrameRenderingEnum::PointLight;
							close->framePrimaryLightEnabled = false;
							close->frameCursorLightIntensityScale =
								BarButtonCursorLightIntensity;
							close->enable.Initialization(true);
							barUISet.shapeMap[
								BarUISetShapeEnum::GeometryAttributeBar_Close] = close;

							auto closeSvg = make_shared<BarUiSVGClass>(
								0.0, 0.0,
								GetThemeColor(BarThemeColorEnum::TextPrimary),
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
	}
}
