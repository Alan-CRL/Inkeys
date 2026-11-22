module;
// UI3 分层迁移的像素回归护栏：每场景输出无头、自上而下的预乘 BGRA8 和查看用 PNG，
// manifest 只记录名称、尺寸与 FNV-1a64。固定工作区、语言、主题、缩放与 1/60 秒帧时刻，
// 隔离配置、隐藏光标光；不能稳定到达的场景记 SKIP，不用真实时间推进或复制产品绘制。
#include "../../../IdtMain.h"
#include "../../../IdtState.h"
#include "../../../IdtI18n.h"
#include "../../../IdtI18nKeys.g.h"
#include "../../../resource.h"
#include "../../Drawing/Draw3/Draw3.ShapeInteraction.h"
#include "../../Log/Log.h"
#include "../../Window/Window.Legacy.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <wincodec.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <crtdbg.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <locale>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

module Inkeys.UI.Bar;
import :Main;
import :Scene;
import :Theme;
import :Atomic;
import Inkeys.UI.PageControl;
import Inkeys.UI.RenderPipeline;
import Inkeys.Text.Font;
import Inkeys.UI.StartupPlacement;

using namespace Inkeys::UI::UI3;

// 只在早期无窗口 CLI 使用；通过现有目标快照和拖动状态驱动真实协调器。
class BarGoldenTestAccess
{
public:
	static void Display(BarUISetClass& owner)
	{
		owner.pendingDisplaySerial.fetch_add(1);
		owner.pendingDisplayLeft = owner.pendingDisplayTop = 0;
		owner.pendingWorkAreaLeft = owner.pendingWorkAreaTop = 0;
		owner.pendingDisplayRight = owner.pendingWorkAreaRight = 1920;
		owner.pendingDisplayBottom = owner.pendingWorkAreaBottom = 1080;
		owner.pendingDisplayDpi = 96;
		owner.pendingDisplayUiBaseScale = 1;
		owner.pendingDesktopDockLine = 1080;
		owner.pendingProtectBottomEdge = false;
		owner.pendingDisplaySerial.fetch_add(1);
	}
	static void DockHint(BarUISetClass& owner)
	{
		const auto frame = owner.BottomDockPresentedSnapshot();
		owner.bottomDockDragRigidGripScreenX = frame.mainCenterScreenX;
		owner.bottomDockDragRigidGripScreenY = frame.mainCenterScreenY;
		owner.bottomDockDragSession = 1;
		owner.bottomDockIndicatorGestureEligible = true;
		owner.bottomDockDragActive = true;
	}
};

namespace Inkeys::UI::Bar
{
	namespace
	{
		namespace Pipeline = Inkeys::UI::RenderPipeline;
		namespace Pages = Inkeys::UI::PageControl;
		using Clock = std::chrono::steady_clock;
		using Microsoft::WRL::ComPtr;
		enum class Content { Expanded, Folded, More, Draw, Geometry, Eraser, Recall,
			PickerDark, PickerLight, TooltipA, TooltipB, DockHint, Construction, Page, Shape,
			Pressed, Disabled, Badge, PickerProgress, TooltipSoft };
		struct Scene
		{
			const char* name;
			Content content;
			bool dark = false;
			double scale = 1;
			unsigned layer = 0;
			const char* skip = nullptr;
		};
		constexpr Scene Scenes[]{
			{"bar-expanded-light-100", Content::Expanded},
			{"bar-expanded-dark-100", Content::Expanded, true},
			{"bar-expanded-light-150", Content::Expanded, false, 1.5},
			{"bar-expanded-dark-150", Content::Expanded, true, 1.5},
			{"bar-button-pressed-light-100", Content::Pressed},
			{"bar-button-pressed-dark-100", Content::Pressed, true},
			{"bar-button-disabled-light-100", Content::Disabled},
			{"bar-geometry-badge-light-100", Content::Badge},
			{"bar-folded-light-100", Content::Folded},
			{"bar-more-light-100", Content::More},
			{"bar-more-dark-100", Content::More, true},
			{"bar-draw-attr-light-100", Content::Draw},
			{"bar-draw-attr-dark-100", Content::Draw, true},
			{"bar-geometry-attr-light-100", Content::Geometry},
			{"bar-eraser-attr-light-100", Content::Eraser},
			{"bar-recall-attr-light-100", Content::Recall, false, 1, 0,
				"BarUISetClass::AdvanceRecallChromeIdle reads steady_clock::now instead of FrameContext::frameTime"},
			{"bar-color-picker-dark-100", Content::PickerDark},
			{"bar-color-picker-light-100", Content::PickerLight},
			{"bar-color-picker-progress-light-100", Content::PickerProgress},
			{"bar-color-picker-progress-dark-100", Content::PickerProgress, true},
			{"bar-tooltip-a-light-100", Content::TooltipA},
			{"bar-tooltip-a-soft-light-100", Content::TooltipSoft},
			{"bar-tooltip-b-light-100", Content::TooltipB},
			{"bar-bottom-dock-hint-light-100", Content::DockHint},
			{"bar-construction-prompt-light-100", Content::Construction},
			{"page-ppt-compact-light", Content::Page},
			{"page-ppt-compact-dark", Content::Page, true},
			{"page-whiteboard-expanded-light", Content::Page, false, 1, 1},
			{"page-more-light", Content::Page, false, 1, 2},
			{"page-theme-light", Content::Page, false, 1, 3},
			{"shape-toolbar-light", Content::Shape},
			{"shape-toolbar-dark", Content::Shape, true},
			{"shape-palette-light", Content::Shape, false, 1, 1},
			{"shape-sides-light", Content::Shape, false, 1, 2},
			{"shape-color-picker-light", Content::Shape, false, 1, 3},
			{"shape-color-picker-dark", Content::Shape, true, 1, 4},
			{"shape-color-picker-compact-light", Content::Shape, false, 1, 0,
				"ShapeEditorSurface::Impl::pickerMotion compact pose is an opening/closing transient, not a settled state"},
		};
		Clock::time_point FrameTime(unsigned frame)
		{
			return Clock::time_point{} + std::chrono::seconds(1)
				+ std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(frame / 60.0));
		}
		struct Pixels
		{
			SIZE size{};
			POINT origin{};
			std::vector<BYTE> bytes;
			bool failed = false;
			BOOL Capture(const UPDATELAYEREDWINDOWINFO& info)
			{
				if (!info.hdcSrc || !info.psize || !info.pptSrc || !info.pptDst || info.psize->cx <= 0 || info.psize->cy <= 0
					|| info.psize->cx > 16384 || info.psize->cy > 16384) { failed = true; return FALSE; }
				// 直接复制真实 ULW 源裁剪；32 位 DIB 的 SRCCOPY 保留预乘 alpha，不另画一份 UI。
				BITMAPINFO bitmap{};
				bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
				bitmap.bmiHeader.biWidth = info.psize->cx;
				bitmap.bmiHeader.biHeight = -info.psize->cy;
				bitmap.bmiHeader.biPlanes = 1;
				bitmap.bmiHeader.biBitCount = 32;
				bitmap.bmiHeader.biCompression = BI_RGB;
				bytes.resize(static_cast<size_t>(info.psize->cx) * info.psize->cy * 4);
				void* data = nullptr;
				HDC dc = CreateCompatibleDC(info.hdcSrc);
				HBITMAP dib = CreateDIBSection(info.hdcSrc, &bitmap, DIB_RGB_COLORS, &data, nullptr, 0);
				HGDIOBJ previous = dc && dib ? SelectObject(dc, dib) : nullptr;
				const bool selected = previous && previous != HGDI_ERROR;
				const bool copied = selected && data && BitBlt(dc, 0, 0, info.psize->cx, info.psize->cy,
					info.hdcSrc, info.pptSrc->x, info.pptSrc->y, SRCCOPY) && GdiFlush();
				if (copied)
				{
					size = *info.psize;
					origin = *info.pptDst;
					std::memcpy(bytes.data(), data, bytes.size());
				}
				if (selected) SelectObject(dc, previous);
				if (dib) DeleteObject(dib);
				if (dc) DeleteDC(dc);
				failed |= !copied;
				return copied ? TRUE : FALSE;
			}
			bool Valid() const
			{
				if (failed || size.cx <= 0 || size.cy <= 0 || bytes.size() != static_cast<size_t>(size.cx) * size.cy * 4) return false;
				bool visible = false;
				for (size_t i = 0; i < bytes.size(); i += 4)
				{
					if (bytes[i] > bytes[i+3] || bytes[i+1] > bytes[i+3] || bytes[i+2] > bytes[i+3]) return false;
					visible |= bytes[i+3] != 0;
				}
				return visible;
			}
			bool InkIn(RECT bounds) const
			{
				// 排除透明留白与低 alpha 光晕，确认指定区域确有主体像素。
				unsigned count = 0;
				for (LONG y = 0; y < size.cy; ++y)
					for (LONG x = 0; x < size.cx; ++x)
					{
						const POINT point{origin.x + x, origin.y + y};
						const bool inside = PtInRect(&bounds, point) != FALSE;
						if (inside && bytes[(static_cast<size_t>(y) * size.cx + x) * 4 + 3] > 128
							&& ++count >= 16) return true;
					}
				return false;
			}
		};
		std::uint64_t Hash(const std::vector<BYTE>& bytes)
		{
			std::uint64_t value = 14695981039346656037ULL;
			for (const auto byte : bytes) { value ^= byte; value *= 1099511628211ULL; }
			return value;
		}
		bool SavePng(const std::filesystem::path& path, const Pixels& pixels)
		{
			// PNG 存直通 alpha；只转换查看用副本，BGRA 和哈希始终使用原始预乘字节。
			auto straight = pixels.bytes;
			for (size_t i = 0; i < straight.size(); i += 4)
				for (size_t channel = 0; channel < 3; ++channel)
					straight[i+channel] = straight[i+3] ? static_cast<BYTE>((std::min)(255u,
						(unsigned(straight[i+channel]) * 255u + straight[i+3] / 2u) / straight[i+3])) : 0;
			ComPtr<IWICImagingFactory> factory; ComPtr<IWICStream> stream; ComPtr<IWICBitmapEncoder> encoder;
			ComPtr<IWICBitmapFrameEncode> frame; ComPtr<IPropertyBag2> properties;
			HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
			if (SUCCEEDED(hr)) hr = factory->CreateStream(&stream);
			if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
			if (SUCCEEDED(hr)) hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
			if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
			if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, &properties);
			if (SUCCEEDED(hr)) hr = frame->Initialize(properties.Get());
			if (SUCCEEDED(hr)) hr = frame->SetSize(pixels.size.cx, pixels.size.cy);
			WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
			if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
			if (SUCCEEDED(hr) && !IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA)) return false;
			if (SUCCEEDED(hr)) hr = frame->WritePixels(pixels.size.cy, pixels.size.cx * 4,
				static_cast<UINT>(straight.size()), straight.data());
			if (SUCCEEDED(hr)) hr = frame->Commit();
			if (SUCCEEDED(hr)) hr = encoder->Commit();
			return SUCCEEDED(hr);
		}
		bool ResetFixture(const Scene& scene)
		{
			// PositionUpdate 等生产入口仍使用全局 owner；早期 CLI 无订阅/worker，原址重建
			// 保证面板、缓存、按压和候选帧全部从相同初值开始，不能沿用上一场景的动画历史。
			SetThemeStyleSource(nullptr);
			std::destroy_at(&barUISet);
			std::construct_at(&barUISet);
			auto& owner = barUISet;
			owner.barStyle.requestedDarkStyle = scene.dark;
			owner.barStyle.darkStyle = scene.dark;
			owner.barStyle.requestedBackgroundOpacity = 0; owner.barStyle.backgroundOpacity = 0;
			owner.barStyle.requestedContentOpacity = 100; owner.barStyle.contentOpacity = 100;
			owner.barStyle.dpiZoom = owner.barStyle.uiBaseZoom = 1;
			owner.barStyle.configZoom = owner.barStyle.zoom = scene.scale;
			owner.barWindow.x = owner.barWindow.y = 0;
			owner.barWindow.w = 1920; owner.barWindow.h = 1080;
			IdtWindowsIsVisible.floatingWindow = false;
			BarGoldenTestAccess::Display(owner);
			SetThemeStyleSource(&owner.barStyle);
			UiAnimationEnabled = true; UiAnimationSpeedRate = 1;
			BarUiEdgeLightingEnabled = true; BarUiDynamicEdgeLightingEnabled = false;
			BarUiDebugModeEnabled = false; BarUiDebugFrameRateEnabled = false;
			SetStatusIndicatorOptions(false, false, false, false);
			BarAtomic::sustainFlag = false; BarAtomic::renderOnceFlag = true;
			SetPptPresentationActive(false); SetWhiteboardActive(false); SetCurrentPageHasContent(false);
			Inkeys::Drawing::Draw3::PublishShapeUiSnapshot({});
			SurfaceSharedLighting lighting{};
			lighting.primaryScreenX = 800; lighting.primaryScreenY = 900; lighting.primaryRadiusPixels = 600;
			lighting.primaryVisible = lighting.edgeLightingEnabled = true;
			lighting.cursorVisible = false; lighting.cursorIntensity = 0;
			Inkeys::UI::UI3::PublishSharedLighting(lighting);
			if (!I18n::load(1, L"JSON", L"zh-CN")) return false;
			owner.ConfigureLocalizedTypography(); owner.barMedia.LoadFormat();
			// 图形固定状态不属于 owner；显式复位，避免角标场景污染后续场景。
			(void)ChangeStateModeToShape();
			(void)SetShapeToolPresetIfRevision(draw3::shapes::PresetKind::RegularPolygon, StateModeTransitionRevision());
			(void)SetShapePolygonSidesIfRevision(5, StateModeTransitionRevision());
			if (IsShapeToolPinned(GetStateModeSnapshot()))
				(void)ToggleShapeToolPinIfRevision(draw3::shapes::PresetKind::RegularPolygon, StateModeTransitionRevision());
			(void)ChangeStateModeToPenTool(PenToolSelectionEnum::HardPen);
			(void)SetPenWidth(4, false); (void)SetPenColor(RGB(30, 140, 220), false);
			return true;
		}
		// host 非空时由第二宿主测试复用同一份搭建：只多接内容重放回调、读回通知与静止后的接手点。
		HRESULT RenderMain(const Scene& scene, Pixels& pixels, const BarGoldenMainSceneHost* host = nullptr)
		{
			auto& owner = barUISet;
			(void)owner.InitializeStartupPlacement(Inkeys::UI::StartupPlacement::CenteredBottomBar);
			InitializeUI(owner);
			// InitializeUI 的 Zoom 启动采样只用于初始布局；首帧之前覆盖所有显示派生值。
			owner.barStyle.dpiZoom = owner.barStyle.uiBaseZoom = 1;
			owner.barStyle.configZoom = owner.barStyle.zoom = scene.scale;
			owner.barStyle.initialZoomFitPending = false;
			owner.superellipseMap[BarUISetSuperellipseEnum::MainButton]->x.SetDirect(300);
			owner.superellipseMap[BarUISetSuperellipseEnum::MainButton]->y.SetDirect(900);
			owner.barButtonSet.PresetInitialization(); owner.barButtonSet.RegisterBuiltInComponents();
			owner.barButtonSet.Load(); owner.barButtonSet.StateUpdate();
			bool capture = false;
			auto callback = CreateBarOffscreenRenderCallback(owner, [&](const auto& info)->BOOL
			{
				if (!capture) return TRUE;
				const BOOL copied = pixels.Capture(info);
				if (copied && host && host->captured) host->captured(info, pixels.bytes);
				return copied;
			}, host ? host->replay : BarMainBarContentReplayCallback{});
			unsigned frame = 0;
			auto result = Pipeline::FrameResult::Idle;
			const auto advance = [&](unsigned count)
			{
				for (unsigned i = 0; i < count; ++i)
				{
					result = callback({Pipeline::GetDeviceEpoch(), Pipeline::GetSharedAssets(), FrameTime(++frame)});
					if (result == Pipeline::FrameResult::DeviceLost) return false;
				}
				return true;
			};
			if (!advance(240)) return E_FAIL;
			owner.barState.fold = scene.content == Content::Folded;
			owner.barState.moreExpanded = scene.content == Content::More;
			owner.barState.drawAttribute = scene.content == Content::Draw || scene.content == Content::PickerDark
				|| scene.content == Content::PickerLight || scene.content == Content::TooltipA || scene.content == Content::TooltipB
				|| scene.content == Content::PickerProgress || scene.content == Content::TooltipSoft;
			const bool drawPanelExpected = owner.barState.drawAttribute;
			owner.barState.geometryAttribute = scene.content == Content::Geometry;
			owner.barState.eraserAttribute = scene.content == Content::Eraser;
			if (scene.content == Content::Geometry)
			{
				(void)ChangeStateModeToShape();
				// 正多边形本身被选中时不启动类别图标的循环边数动画，静态帧可充分收敛。
				(void)SetShapeToolPresetIfRevision(draw3::shapes::PresetKind::RegularPolygon, StateModeTransitionRevision());
				(void)SetShapePolygonSidesIfRevision(5, StateModeTransitionRevision());
			}
			if (scene.content == Content::Eraser) (void)ChangeStateModeToEraser();
			if (scene.content == Content::Disabled) (void)ChangeStateModeToSelection();
			if (scene.content == Content::Badge)
			{
				(void)ChangeStateModeToShape();
				if (!ToggleShapeToolPinIfRevision(draw3::shapes::PresetKind::RegularPolygon, StateModeTransitionRevision())) return E_FAIL;
			}
			if (scene.content == Content::TooltipSoft)
			{
				(void)ChangeStateModeToPenTool(PenToolSelectionEnum::SoftPen);
				(void)SetPenWidth(4, false);
			}
			if (scene.content == Content::Construction)
			{
				(void)ChangeStateModeToShape();
				auto snapshot = std::make_shared<Inkeys::Drawing::Draw3::ShapeUiSnapshot>();
				snapshot->identity.session = snapshot->identity.generation = 1;
				snapshot->construction.active = true;
				snapshot->construction.preset = draw3::shapes::PresetKind::Arc;
				snapshot->construction.step = 2; snapshot->construction.totalSteps = 2;
				snapshot->construction.confirmedSteps = 1; snapshot->construction.canBack = true;
				Inkeys::Drawing::Draw3::PublishShapeUiSnapshot(snapshot);
			}
			if (scene.content == Content::DockHint) BarGoldenTestAccess::DockHint(owner);
			if (scene.content == Content::PickerDark || scene.content == Content::PickerLight || scene.content == Content::PickerProgress)
			{
				owner.barState.drawAttributeBar.colorPickerOpen = true;
				owner.colorPicker.colorPickerDarkTone = scene.content != Content::PickerLight;
				if (scene.content == Content::PickerProgress)
				{
					// 输入 worker 不启动；固定其发布的保持进度，真实绘制路径负责画环。
					owner.colorPicker.colorPickerPointerPressed = true;
					owner.colorPicker.colorPickerHoldHintActive = true;
					owner.colorPicker.colorPickerHoldProgress = 0.5f;
					owner.colorPicker.colorPickerPointerY = 0;
				}
			}
			if (scene.content == Content::TooltipA || scene.content == Content::TooltipSoft)
			{
				// 先等属性面板换边完成，再按生产交互锁存菜单方向和笔型，避免首帧被关闭。
				owner.barButtonSet.StateUpdate(); owner.UpdateRendering(false);
				if (!advance(240)) return E_FAIL;
				owner.barState.drawAttributeBar.penTypeMenuAnchorMode = static_cast<int>(GetStateModeSnapshot().Pen.ModeSelect);
				owner.barState.drawAttributeBar.penTypeMenuOpenBelow = static_cast<bool>(owner.barState.widgetPosition.primaryBar);
				owner.barState.drawAttributeBar.penTypeMenuDirectionLocked = true;
				owner.barState.drawAttributeBar.penTypeMenuOpen = true;
				owner.barState.drawAttributeBar.thicknessAnnotationPinned = true;
			}
			if (scene.content == Content::TooltipB)
			{
				(void)SetPenWidth(200, false);
				owner.barState.drawAttributeBar.thicknessOverflowPinned = true;
			}
			owner.barButtonSet.StateUpdate(); owner.UpdateRendering(false);
			auto* pressedButton = owner.barButtonSet.preset[static_cast<int>(BarButtonPresetEnum::Eraser)];
			if (scene.content == Content::Pressed)
			{
				if (!pressedButton) return E_FAIL;
				SetButtonPressedVisual(*pressedButton, true);
			}
			if (!advance(360)) return E_FAIL;
			capture = true; BarAtomic::renderOnceFlag = true;
			owner.UpdateRendering(false);
			if (!advance(1) || result == Pipeline::FrameResult::Retry || !pixels.Valid()) return E_FAIL;
			// 防止“场景名正确、实际面板没打开”的空基线混入 manifest。
			const auto visible = [&](BarUISetShapeEnum key) { return owner.shapeMap[key]->pct.val > .01; };
			if (static_cast<bool>(owner.barState.fold) != (scene.content == Content::Folded)) return E_FAIL;
			if (scene.content == Content::Folded && visible(BarUISetShapeEnum::MainBar)) return E_FAIL;
			if (scene.content != Content::Folded && !visible(BarUISetShapeEnum::MainBar)) return E_FAIL;
			if (scene.content == Content::More && !visible(BarUISetShapeEnum::MorePanel)) return E_FAIL;
			if (drawPanelExpected && (!owner.barState.drawAttribute || !visible(BarUISetShapeEnum::DrawAttributeBar))) return E_FAIL;
			if (scene.content == Content::Geometry && !owner.geometryAttribute.PresentationSnapshot().visible) return E_FAIL;
			if (scene.content == Content::Eraser && !owner.eraserAttribute.PresentationSnapshot().panelVisible) return E_FAIL;
			if ((scene.content == Content::PickerDark || scene.content == Content::PickerLight)
				&& !(owner.colorPicker.PanelVisibility() > .01)) return E_FAIL;
			if ((scene.content == Content::TooltipA || scene.content == Content::TooltipSoft)
				&& (!owner.barState.drawAttributeBar.penTypeMenuOpen
					|| !owner.barState.drawAttributeBar.thicknessAnnotationPinned
					|| !visible(BarUISetShapeEnum::DrawAttributeBar_PenTypeMenu)
					|| !visible(BarUISetShapeEnum::DrawAttributeBar_ThicknessAnnotationPopup)
					|| owner.wordMap[BarUISetWordEnum::DrawAttributeBar_ThicknessAnnotationPopupText]->content.GetVal()
						!= IW(scene.content == Content::TooltipSoft ? I18nKey.UI.Bar.DrawAttributes.AnnotationFixedUnsupportedTitle
							: I18nKey.UI.Bar.DrawAttributes.AnnotationUnavailableTitle))) return E_FAIL;
			if (scene.content == Content::TooltipB && !visible(BarUISetShapeEnum::DrawAttributeBar_ThicknessOverflowPopup)) return E_FAIL;
			if (scene.content == Content::DockHint && !owner.BottomDockPresentedSnapshot().indicatorVisible) return E_FAIL;
			if (scene.content == Content::Construction && !owner.constructionPrompt.Visible()) return E_FAIL;
			if (scene.content == Content::Pressed && (!pressedButton->IsVisible() || pressedButton->icon.pct.val <= .01
				|| pressedButton->button.pct.val <= .01 || pressedButton->pressScale.val >= .999)) return E_FAIL;
			if (scene.content == Content::Disabled)
			{
				const auto* button = owner.barButtonSet.preset[static_cast<int>(BarButtonPresetEnum::Clean)];
				if (!button || !button->IsVisible() || button->state->state != WidgetState::Disable
					|| button->icon.pct.val <= .01 || button->icon.pct.val >= .999) return E_FAIL;
			}
			if (scene.content == Content::Badge)
			{
				const auto* button = owner.barButtonSet.preset[static_cast<int>(BarButtonPresetEnum::Geometry)];
				if (!button || !button->IsVisible() || button->state->state != WidgetState::Selected
					|| !button->badge || button->badge->progress.val < .99 || button->icon.pct.val <= .01) return E_FAIL;
			}
			if (scene.content == Content::PickerProgress && (!(owner.colorPicker.PanelVisibility() > .01)
				|| !(owner.colorPicker.HintVisibility() > .01))) return E_FAIL;
			const auto previousSize = pixels.size;
			const auto previousBytes = pixels.bytes;
			capture = false;
			if (!advance(120)) return E_FAIL;
			capture = true; BarAtomic::renderOnceFlag = true; owner.UpdateRendering(false);
			pixels = {}; // 第二次必须收到新的真实提交，不能用上次缓冲冒充静止帧。
			if (!advance(1) || result == Pipeline::FrameResult::Retry || !pixels.Valid()) return E_FAIL;
			if (pixels.size.cx != previousSize.cx || pixels.size.cy != previousSize.cy || pixels.bytes != previousBytes) return S_FALSE;
			if (host)
			{
				// 第二宿主测试从静止态接手：每次强制渲染一帧，并要求收到新的真实提交。
				const auto renderFrame = [&]
				{
					capture = true; pixels = {}; BarAtomic::renderOnceFlag = true; owner.UpdateRendering(false);
					return advance(1) && result != Pipeline::FrameResult::Retry && pixels.Valid();
				};
				if (host->settledNatural)
				{
					// 自然帧：只请求渲染，不设 renderOnce，生产按自己的脏区局部提交。
					const auto naturalFrame = [&]
					{
						capture = true; pixels = {}; owner.UpdateRendering(false);
						return advance(1) && result != Pipeline::FrameResult::Retry && pixels.Valid();
					};
					return host->settledNatural(renderFrame, naturalFrame) ? S_OK : E_FAIL;
				}
				return !host->settled || host->settled(renderFrame) ? S_OK : E_FAIL;
			}
			if (scene.content == Content::Pressed || scene.content == Content::Badge || scene.content == Content::PickerProgress
				|| scene.content == Content::TooltipA || scene.content == Content::TooltipSoft || scene.content == Content::TooltipB
				|| scene.content == Content::More || scene.content == Content::Draw || scene.content == Content::Geometry
				|| scene.content == Content::Eraser || scene.content == Content::PickerDark || scene.content == Content::PickerLight)
			{
				// 反例只撤销待测效果：若真实绘制没有消费它，就不能记 OK。
				const auto expected = pixels;
				capture = false;
				if (scene.content == Content::Pressed) SetButtonPressedVisual(*pressedButton, false);
				if (scene.content == Content::Badge)
					(void)ToggleShapeToolPinIfRevision(draw3::shapes::PresetKind::RegularPolygon, StateModeTransitionRevision());
				if (scene.content == Content::PickerProgress) owner.colorPicker.colorPickerHoldProgress = 0.0f;
				if (scene.content == Content::TooltipA || scene.content == Content::TooltipSoft)
					owner.barState.drawAttributeBar.thicknessAnnotationPinned = false;
				if (scene.content == Content::TooltipB) owner.barState.drawAttributeBar.thicknessOverflowPinned = false;
				if (scene.content == Content::More) owner.barState.moreExpanded = false;
				if (scene.content == Content::Draw) owner.barState.drawAttribute = false;
				if (scene.content == Content::Geometry) owner.barState.geometryAttribute = false;
				if (scene.content == Content::Eraser) owner.barState.eraserAttribute = false;
				if (scene.content == Content::PickerDark || scene.content == Content::PickerLight)
					owner.barState.drawAttributeBar.colorPickerOpen = false;
				owner.barButtonSet.StateUpdate(); owner.UpdateRendering(false);
				if (!advance(240)) return E_FAIL;
				capture = true; pixels = {}; BarAtomic::renderOnceFlag = true; owner.UpdateRendering(false);
				if (!advance(1) || result == Pipeline::FrameResult::Retry || !pixels.Valid()
					|| (pixels.size.cx == expected.size.cx && pixels.size.cy == expected.size.cy && pixels.bytes == expected.bytes)) return E_FAIL;
				pixels = expected;
			}
			return S_OK;
		}
		HRESULT RenderPage(const Scene& scene, Pixels& pixels)
		{
			Pages::OffscreenSnapshot snapshot{};
			if (scene.layer == 0)
			{
				snapshot.ppt.presentationVisible = snapshot.ppt.presentationActive = true;
				snapshot.ppt.currentPage = 7; snapshot.ppt.totalPage = 28;
				snapshot.ppt.layout.session = 1;
			}
			else
			{
				auto& board = snapshot.whiteboard;
				board.active = board.expandedLayoutTarget = true;
				board.currentPage = 2; board.totalPage = 4; board.generation = 1;
				board.previousEnabled = board.previousInteractive = board.nextEnabled = board.nextInteractive = true;
				board.menu = static_cast<int>(scene.layer) - 1;
				board.backgroundRgb = 0x123B32; board.pattern = 0;
			}
			const LONG width = scene.layer == 0 ? 240 : 305;
			auto callback = Pages::CreatePageControlOffscreenRenderCallback(0, [snapshot] { return snapshot; },
				{200, 500, 200 + width, 580}, 1, [&](const auto& info) { return pixels.Capture(info); });
			auto result = Pipeline::FrameResult::Idle;
			for (unsigned frame = 0; frame < 360; ++frame)
			{
				result = callback({Pipeline::GetDeviceEpoch(), Pipeline::GetSharedAssets(), FrameTime(frame)});
				if (result == Pipeline::FrameResult::DeviceLost || pixels.failed) return E_FAIL;
			}
			if (result == Pipeline::FrameResult::Retry || !pixels.Valid()) return E_FAIL;
			if (!pixels.InkIn({210, 510, 200 + width - 10, 570})) return E_FAIL;
			// 对应 Bar.Scene 的 MenuBounds/ThemeBounds 内部；Theme 不能只画出 More 就算通过。
			if (scene.layer >= 2 && !pixels.InkIn({400, 388, 530, 485})) return E_FAIL;
			if (scene.layer == 3 && !pixels.InkIn({555, 209, 765, 485})) return E_FAIL;
			return result == Pipeline::FrameResult::Idle ? S_OK : S_FALSE;
		}
		HRESULT Render(const Scene& scene, Pixels& pixels)
		{
			Inkeys::Testing::WorkspaceSceneScope workspace(WorkspaceScene::Desktop);
			if (!workspace || !ResetFixture(scene)) return E_FAIL;
			if (scene.content == Content::Page) return RenderPage(scene, pixels);
			if (scene.content == Content::Shape)
				return RenderShapeEditorGoldenScene(scene.layer, [&](const auto& info) { return pixels.Capture(info); });
			return RenderMain(scene, pixels);
		}
	}

	HRESULT RunGoldenMainSceneForHost(bool drawAttribute, bool dark, double scale,
		const BarGoldenMainSceneHost& host)
	{
		// 与基线的 Render 相同：工作区场景隔离、原址重建夹具，再走 RenderMain 的搭建与静止判定。
		const Scene scene{"host-main-bar", drawAttribute ? Content::Draw : Content::Expanded, dark, scale};
		Inkeys::Testing::WorkspaceSceneScope workspace(WorkspaceScene::Desktop);
		if (!workspace || !ResetFixture(scene)) return E_FAIL;
		Pixels pixels;
		return RenderMain(scene, pixels, &host);
	}

	namespace
	{
		// 动画轨迹基线：固定帧时钟驱动真实协调器，逐帧记录主栏内容的动画中间值。
		// 像素 golden 只看静止帧，抽取共享编排时用它证明展开/收起/加入/重开/换边/More/折叠等过程逐帧一致。
		struct TraceWriter
		{
			std::string text;
			void Value(double v)
			{
				char buffer[64];
				const auto r = std::to_chars(buffer, buffer + sizeof(buffer), v);
				text.push_back(' ');
				text.append(buffer, r.ptr);
			}
			void Visual(const char* name, const UiInnheritBaseClass& s)
			{
				text += name;
				for (double v : {double(s.x.val), double(s.y.val), double(s.w.val), double(s.h.val), double(s.pct.val),
					double(s.inhX), double(s.inhY)}) Value(v);
				text.push_back('\n');
			}
			void Button(const char* tag, std::size_t index, const BarButtonClass& b)
			{
				text += tag; text += std::to_string(index); text.push_back(' '); text += b.id;
				const auto& s = b.button;
				for (double v : {double(s.x.val), double(s.y.val), double(s.w.val), double(s.h.val), double(s.pct.val),
					double(s.inhX), double(s.inhY)}) Value(v);
				text += " icon"; for (double v : {double(b.icon.x.val), double(b.icon.y.val), double(b.icon.w.val), double(b.icon.h.val),
					double(b.icon.pct.val), double(b.icon.inhX), double(b.icon.inhY)}) Value(v);
				text += " name"; for (double v : {double(b.name.x.val), double(b.name.y.val), double(b.name.size.val), double(b.name.pct.val),
					double(b.name.inhX), double(b.name.inhY)}) Value(v);
				text += " state"; Value(double(b.pressScale.val)); Value(static_cast<int>(b.size.load()));
				Value(b.IsVisible() ? 1 : 0); Value(static_cast<int>(b.state->state.load())); Value(static_cast<int>(b.hoverStage.load()));
				text.push_back('\n');
			}
		};

		void TraceFrame(TraceWriter& w, BarUISetClass& owner, unsigned frame)
		{
			w.text += "frame " + std::to_string(frame) + " side " + std::to_string(int(static_cast<bool>(owner.barState.widgetPosition.mainBar)))
				+ " fold " + std::to_string(int(static_cast<bool>(owner.barState.fold))) + '\n';
			w.Visual("main", *owner.shapeMap[BarUISetShapeEnum::MainBar]);
			w.Visual("mainButton", *owner.superellipseMap[BarUISetSuperellipseEnum::MainButton]);
			w.Visual("more", *owner.shapeMap[BarUISetShapeEnum::MorePanel]);
			w.Visual("moreClose", *owner.shapeMap[BarUISetShapeEnum::MorePanelCloseHit]);
			w.Visual("moreCloseSvg", *owner.svgMap[BarUISetSvgEnum::MorePanelClose]);
			w.Visual("draw", *owner.shapeMap[BarUISetShapeEnum::DrawAttributeBar]);
			const auto main = owner.barButtonSet.buttonList.Snapshot();
			for (std::size_t i = 0; i < main.size(); ++i) if (main[i]) w.Button("b", i, *main[i]);
			const auto more = owner.barButtonSet.GetMoreButtonSnapshot();
			for (std::size_t i = 0; i < more.explicitMore.size(); ++i) if (more.explicitMore[i]) w.Button("m", i, *more.explicitMore[i]);
			for (std::size_t i = 0; i < more.forcedOverflow.size(); ++i) if (more.forcedOverflow[i]) w.Button("f", i, *more.forcedOverflow[i]);
		}

		// step(n) 推进 n 帧并逐帧记录；脚本在两次 step 之间改变状态，模拟生产的状态切换后请求渲染。
		using TraceStep = std::function<bool(unsigned)>;
		HRESULT RunTraceScenario(const char* name, bool compact, Inkeys::UI::StartupPlacement placement,
			const std::function<bool(BarUISetClass&, const TraceStep&)>& script, std::string& out)
		{
			const Scene scene{name, Content::Expanded, false, 1};
			Inkeys::Testing::WorkspaceSceneScope workspace(WorkspaceScene::Desktop);
			if (!workspace || !ResetFixture(scene)) return E_FAIL;
			auto& owner = barUISet;
			if (compact) owner.barStyle.requestedSizeMode = static_cast<int>(BarSizeMode::Compact);
			(void)owner.InitializeStartupPlacement(placement);
			InitializeUI(owner);
			owner.barStyle.dpiZoom = owner.barStyle.uiBaseZoom = 1;
			owner.barStyle.configZoom = owner.barStyle.zoom = scene.scale;
			owner.barStyle.initialZoomFitPending = false;
			owner.superellipseMap[BarUISetSuperellipseEnum::MainButton]->x.SetDirect(300);
			owner.superellipseMap[BarUISetSuperellipseEnum::MainButton]->y.SetDirect(900);
			owner.barButtonSet.PresetInitialization(); owner.barButtonSet.RegisterBuiltInComponents();
			owner.barButtonSet.Load(); owner.barButtonSet.StateUpdate();
			auto callback = CreateBarOffscreenRenderCallback(owner, [](const auto&)->BOOL { return TRUE; });
			unsigned frame = 0;
			TraceWriter writer;
			const auto advance = [&](unsigned count, bool record)
			{
				for (unsigned i = 0; i < count; ++i)
				{
					if (callback({Pipeline::GetDeviceEpoch(), Pipeline::GetSharedAssets(), FrameTime(++frame)})
						== Pipeline::FrameResult::DeviceLost) return false;
					if (record) TraceFrame(writer, owner, frame);
				}
				return true;
			};
			if (!advance(240, false)) return E_FAIL;
			owner.barButtonSet.StateUpdate(); owner.UpdateRendering(false);
			if (!advance(360, false)) return E_FAIL;
			const TraceStep step = [&](unsigned count) { return advance(count, true); };
			if (!step(2) || !script(owner, step)) return E_FAIL;
			out = std::move(writer.text);
			return S_OK;
		}

		struct TraceScenario
		{
			std::string name;
			bool compact;
			std::function<bool(BarUISetClass&, const TraceStep&)> script;
			Inkeys::UI::StartupPlacement placement = Inkeys::UI::StartupPlacement::CenteredBottomBar;
		};

		std::vector<TraceScenario> TraceScenarios()
		{
			const auto request = [](BarUISetClass& owner) { owner.barButtonSet.StateUpdate(); owner.UpdateRendering(false); };
			const auto selection = [=](BarUISetClass& owner) { (void)ChangeStateModeToSelection(); request(owner); };
			const auto pen = [=](BarUISetClass& owner) { (void)ChangeStateModeToPenTool(PenToolSelectionEnum::HardPen); request(owner); };
			const auto more = [=](BarUISetClass& owner, bool open) { owner.barState.moreExpanded = open; request(owner); };
			// 选择态无内容时橡皮/图形/撤回隐藏：收起与展开批次、前半程加入与后半程重开。
			const auto toggleAfter = [=](unsigned gap)
			{
				return [=](BarUISetClass& owner, const TraceStep& step)
				{
					selection(owner);
					if (!step(gap)) return false;
					pen(owner);
					return step(50);
				};
			};
			std::vector<TraceScenario> list;
			for (const bool compact : {false, true})
			{
				const std::string suffix = compact ? "-compact" : "";
				const auto add = [&](const char* name, std::function<bool(BarUISetClass&, const TraceStep&)> script,
					Inkeys::UI::StartupPlacement placement = Inkeys::UI::StartupPlacement::CenteredBottomBar)
				{
					list.push_back({name + suffix, compact, std::move(script), placement});
				};
				add("collapse-expand", toggleAfter(50));
				add("early-join", toggleAfter(4));
				add("late-restart", toggleAfter(16));
				add("more-open-reverse", [=](BarUISetClass& owner, const TraceStep& step)
				{
					more(owner, true);
					if (!step(40)) return false;
					more(owner, false);
					if (!step(8)) return false;
					more(owner, true);
					return step(40);
				});
				add("fold-unfold", [=](BarUISetClass& owner, const TraceStep& step)
				{
					owner.barState.fold = true; request(owner);
					if (!step(40)) return false;
					owner.barState.fold = false; request(owner);
					return step(50);
				});
				add("press-release", [=](BarUISetClass& owner, const TraceStep& step)
				{
					auto* eraser = owner.barButtonSet.preset[static_cast<int>(BarButtonPresetEnum::Eraser)];
					if (!eraser) return false;
					SetButtonPressedVisual(*eraser, true); request(owner);
					if (!step(20)) return false;
					SetButtonPressedVisual(*eraser, false); request(owner);
					return step(30);
				});
				add("draw-attribute", [=](BarUISetClass& owner, const TraceStep& step)
				{
					owner.barState.drawAttribute = true; request(owner);
					if (!step(40)) return false;
					owner.barState.drawAttribute = false; request(owner);
					return step(40);
				});
				// 换边：居中吸底锁住展开方向，先收窄到选择态（左侧空间足够容纳），解除居中并请求另一侧；
				// 换边进行中切回绘制态，覆盖“换边中重排”（展开后空间不足会再次换边，同样逐帧记录）。
				add("side-switch-relayout", [=](BarUISetClass& owner, const TraceStep& step)
				{
					selection(owner);
					if (!step(50)) return false;
					owner.ClearBottomDockCenterForFold();
					owner.barState.widgetPosition.mainBar = !static_cast<bool>(owner.barState.widgetPosition.mainBar); request(owner);
					if (!step(8)) return false;
					pen(owner);
					return step(60);
				});
			}
			return list;
		}
	}

	int RunUi3TraceDump(const wchar_t* outputDirectory)
	{
		if (!outputDirectory || !*outputDirectory) return 2;
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
		_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE); _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
		struct RuntimeScope
		{
			std::wstring originalPath = globalPath;
			bool com = false, pipeline = false;
			~RuntimeScope()
			{
				SetThemeStyleSource(nullptr);
				Inkeys::Drawing::Draw3::PublishShapeUiSnapshot({});
				barUISet.specTarget.Discard();
				if (pipeline) Pipeline::Shutdown();
				if (com) CoUninitialize();
				globalPath = originalPath;
			}
		} runtime;
		unsigned ok = 0, failed = 0;
		std::ostringstream report;
		try
		{
			const auto directory = std::filesystem::absolute(outputDirectory);
			std::filesystem::create_directories(directory / L"runtime-config/Inkeys/Config");
			globalPath = (directory / L"runtime-config").wstring() + L"\\";
			runtime.com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
			runtime.pipeline = runtime.com && SUCCEEDED(Pipeline::Initialize());
			IdtFontFileLoader::IsLoaderInitialized(); IdtFontCollectionLoader::IsLoaderInitialized();
			const std::array<UINT, 4> fonts{IDR_TTF1, IDR_TTF7, IDR_TTF3, IDR_TTF8};
			const bool assets = runtime.pipeline && SUCCEEDED(Pipeline::InitializeFontCollection(IdtFontFileLoader::GetLoader(),
				IdtFontCollectionLoader::GetLoader(), fonts));
			for (const auto& scenario : TraceScenarios())
			{
				const auto path = directory / (scenario.name + ".trace");
				std::filesystem::remove(path);
				std::string first, second;
				const char* failure = nullptr;
				if (!assets) failure = "runtime initialization failed";
				else if (FAILED(RunTraceScenario(scenario.name.c_str(), scenario.compact, scenario.placement, scenario.script, first))
					|| FAILED(RunTraceScenario(scenario.name.c_str(), scenario.compact, scenario.placement, scenario.script, second)))
					failure = "scenario failed";
				else if (first != second) failure = "two fresh runs produced different traces";
				else
				{
					std::ofstream file(path, std::ios::binary | std::ios::trunc);
					file << first;
					if (!file) failure = "trace output failed";
				}
				if (failure) { ++failed; report << "FAIL " << scenario.name << ' ' << failure << '\n'; }
				else { ++ok; report << "OK " << scenario.name << " bytes=" << first.size() << '\n'; }
			}
			report << "scenarios ok=" << ok << " failed=" << failed << '\n';
			std::ofstream results(directory / L"results.log", std::ios::binary | std::ios::trunc);
			results << report.str();
		}
		catch (...) { ++failed; report << "FAIL output exception\n"; }
		std::fprintf(stderr, "%s", report.str().c_str());
		return failed ? 1 : 0;
	}

	int RunUi3GoldenDump(const wchar_t* outputDirectory)
	{
		if (!outputDirectory || !*outputDirectory) return 2;
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
		_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE); _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
		unsigned ok = 0, skipped = 0, failed = 0;
		unsigned processed = 0;
		std::array<Scene, std::size(Scenes)> ordered;
		std::copy(std::begin(Scenes), std::end(Scenes), ordered.begin());
		std::sort(ordered.begin(), ordered.end(), [](const Scene& a, const Scene& b) { return std::strcmp(a.name, b.name) < 0; });
		std::ostringstream report;
		std::ofstream results, manifest;
		struct RuntimeScope
		{
			std::wstring originalPath = globalPath;
			bool com = false, pipeline = false;
			~RuntimeScope()
			{
				SetThemeStyleSource(nullptr);
				Inkeys::Drawing::Draw3::PublishShapeUiSnapshot({});
				barUISet.specTarget.Discard();
				if (pipeline) Pipeline::Shutdown();
				if (com) CoUninitialize();
				globalPath = originalPath;
			}
		} runtime;
		int exitCode = 0;
		try
		{
			const auto directory = std::filesystem::absolute(outputDirectory);
			std::filesystem::create_directories(directory / L"runtime-config/Inkeys/Config");
			globalPath = (directory / L"runtime-config").wstring() + L"\\";
			// 不调用任何用户配置加载器；产品配置归一化写回只会落在隔离目录。
			results.open(directory / L"results.log", std::ios::binary | std::ios::trunc);
			manifest.open(directory / L"manifest.txt", std::ios::binary | std::ios::trunc);
			report.imbue(std::locale::classic()); manifest.imbue(std::locale::classic());
			if (!results || !manifest) throw std::runtime_error("output stream");
			const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			runtime.com = SUCCEEDED(com);
			if (!runtime.com) exitCode = 2;
			else
			{
				runtime.pipeline = SUCCEEDED(Pipeline::Initialize());
				if (!runtime.pipeline) exitCode = 3;
			}
			bool assets = false;
			if (!exitCode)
			{
				IdtFontFileLoader::IsLoaderInitialized(); IdtFontCollectionLoader::IsLoaderInitialized();
				const std::array<UINT, 4> fonts{IDR_TTF1, IDR_TTF7, IDR_TTF3, IDR_TTF8};
				assets = SUCCEEDED(Pipeline::InitializeFontCollection(IdtFontFileLoader::GetLoader(),
					IdtFontCollectionLoader::GetLoader(), fonts));
			}
			for (const auto& scene : ordered)
			{
				// 只删除本入口拥有的同名产物，防止旧 OK 文件伪装成这次 SKIP/FAIL 的输出。
				const auto rawPath = directory / (std::string(scene.name) + ".bgra");
				const auto pngPath = directory / (std::string(scene.name) + ".png");
				std::filesystem::remove(rawPath); std::filesystem::remove(pngPath);
				const char* failure = nullptr;
				const char* skip = scene.skip;
				Pixels first, second;
				if (exitCode || !assets) failure = exitCode == 2 ? "COM initialization failed"
					: exitCode == 3 ? "RenderPipeline::Initialize failed" : "font collection initialization failed";
				else if (!skip)
				{
					try
					{
						const HRESULT a = Render(scene, first);
						const HRESULT b = SUCCEEDED(a) && a != S_FALSE ? Render(scene, second) : a;
						if (a == S_FALSE || b == S_FALSE) skip = "fixed-frame animation did not settle";
						else if (FAILED(a) || FAILED(b) || !first.Valid() || !second.Valid()) failure = "scene setup, submit or BGRA readback failed";
						else if (first.size.cx != second.size.cx || first.size.cy != second.size.cy || first.bytes != second.bytes)
							skip = "fresh fixed-frame fixtures produced different pixels";
						else
						{
							std::ofstream raw(rawPath, std::ios::binary | std::ios::trunc);
							raw.write(reinterpret_cast<const char*>(first.bytes.data()), static_cast<std::streamsize>(first.bytes.size()));
							raw.close();
							if (!raw || !SavePng(pngPath, first)) failure = "BGRA or PNG output failed";
						}
					}
					catch (...) { failure = "scene or output exception"; }
				}
				if (failure)
				{
					++failed; report << "FAIL " << scene.name << ' ' << failure << '\n';
					Inkeys::Log::TryFormat(Inkeys::Log::Level::Error, Inkeys::Log::Module::Bar,
						"Golden scene {}: {}", scene.name, failure);
				}
				else if (skip) { ++skipped; report << "SKIP " << scene.name << ' ' << skip << '\n'; }
				else
				{
					++ok; report << "OK " << scene.name << '\n';
					manifest << scene.name << ' ' << first.size.cx << ' ' << first.size.cy << ' '
						<< std::hex << std::setw(16) << std::setfill('0') << Hash(first.bytes) << std::dec << '\n';
				}
				if (failure || skip)
				{
					std::error_code ignored;
					std::filesystem::remove(rawPath, ignored); std::filesystem::remove(pngPath, ignored);
				}
				++processed;
			}
		}
		catch (...)
		{
			for (; processed < ordered.size(); ++processed)
			{
				++failed; report << "FAIL " << ordered[processed].name << " output directory or stream failure\n";
			}
			Inkeys::Log::TryWrite(Inkeys::Log::Level::Error, Inkeys::Log::Module::Bar, std::nullopt,
				"Golden dump could not prepare or write its output directory");
		}
		manifest.close();
		if (!manifest || !results)
		{
			// 索引写失败时整批不能作为基线；内存中的结果统一转成 FAIL，汇总仍与场景数一致。
			ok = skipped = 0; failed = static_cast<unsigned>(std::size(Scenes));
			report.str(std::string{}); report.clear();
			for (const auto& scene : ordered) report << "FAIL " << scene.name << " manifest or results output failed\n";
		}
		report << "scenes=" << std::size(Scenes) << " ok=" << ok << " skipped=" << skipped << " failed=" << failed << '\n';
		results << report.str(); results.close();
		if (!results)
		{
			ok = skipped = 0; failed = static_cast<unsigned>(std::size(Scenes));
			Inkeys::Log::TryWrite(Inkeys::Log::Level::Error, Inkeys::Log::Module::Bar, std::nullopt,
				"Golden dump could not finish results.log");
		}
		std::fprintf(stderr, "scenes=%zu ok=%u skipped=%u failed=%u\n", std::size(Scenes), ok, skipped, failed);
		return exitCode ? exitCode : failed ? 1 : 0;
	}
}
