module;
#include "../../../IdtMain.h"
#include "../../../IdtState.h"
#include "../../../IdtConfiguration.h"
#include "../StartupPlacement.h"
#include "../../../IdtI18n.h"
#include "../../../IdtI18nKeys.g.h"
#include "../../../resource.h"
#include <d2d1_1helper.h>
#include <wincodec.h>
#include <crtdbg.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

module Inkeys.UI.Bar;
import :Main;
import :Layout;
import :Theme;
import Inkeys.UI.RenderPipeline;
import Inkeys.Text.Font;

using namespace Inkeys::UI::Bar;
using namespace draw3::shapes;
using Microsoft::WRL::ComPtr;

// 仅离屏 fixture 读取内部状态；产品入口仍只暴露成功呈现快照。
class BarGeometryAttributeTestAccess
{
public:
	static const BarButtonClass& Button(const BarGeometryAttributePanel& p, int item) { return p.buttons_[item]; }
	static bool CarouselArmed(const BarGeometryAttributePanel& p) { return p.carouselArmed_; }
	static unsigned CarouselSides(const BarGeometryAttributePanel& p) { return p.carouselSides_; }
	static void FinishThickness(BarUISetClass& owner, bool cancelled = false) { owner.CloseThicknessSlider(cancelled); }
	static void FinishColor(BarUISetClass& owner, bool cancelled = false) { owner.CloseColorPicker(cancelled); }
	static void Display(BarUISetClass& owner, LONG width, LONG height)
	{
		owner.pendingDisplaySerial.fetch_add(1);
		owner.pendingDisplayLeft = owner.pendingDisplayTop = owner.pendingWorkAreaLeft = owner.pendingWorkAreaTop = 0;
		owner.pendingDisplayRight = owner.pendingWorkAreaRight = width;
		owner.pendingDisplayBottom = owner.pendingWorkAreaBottom = height;
		owner.pendingDisplayDpi = USER_DEFAULT_SCREEN_DPI;
		owner.pendingDisplaySerial.fetch_add(1);
	}
};

namespace
{
	std::vector<unsigned char> ReadPixels(ID2D1DeviceContext* dc, ID2D1Bitmap1* target)
	{
		ComPtr<ID2D1Bitmap1> readable;
		const auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, target->GetPixelFormat());
		if (FAILED(dc->CreateBitmap(target->GetPixelSize(), nullptr, 0, &props, &readable)) ||
			FAILED(readable->CopyFromBitmap(nullptr, target, nullptr))) return {};
		D2D1_MAPPED_RECT map{}; if (FAILED(readable->Map(D2D1_MAP_OPTIONS_READ, &map))) return {};
		const auto size = target->GetPixelSize(); std::vector<unsigned char> bytes(size.width * size.height * 4);
		for (UINT y = 0; y < size.height; ++y) std::memcpy(bytes.data() + y * size.width * 4, map.bits + y * map.pitch, size.width * 4);
		readable->Unmap(); return bytes;
	}
	bool SavePng(const std::filesystem::path& path, const std::vector<unsigned char>& bytes, UINT width, UINT height)
	{
		if (bytes.size() != static_cast<size_t>(width) * height * 4) return false;
		ComPtr<IWICImagingFactory> factory; ComPtr<IWICStream> stream; ComPtr<IWICBitmapEncoder> encoder;
		ComPtr<IWICBitmapFrameEncode> frame; ComPtr<IPropertyBag2> properties;
		HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
		if (SUCCEEDED(hr)) hr = factory->CreateStream(&stream);
		if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
		if (SUCCEEDED(hr)) hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
		if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
		if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, &properties);
		if (SUCCEEDED(hr)) hr = frame->Initialize(properties.Get());
		if (SUCCEEDED(hr)) hr = frame->SetSize(width, height);
		WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
		if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
		if (SUCCEEDED(hr)) hr = frame->WritePixels(height, width * 4, static_cast<UINT>(bytes.size()), const_cast<BYTE*>(bytes.data()));
		if (SUCCEEDED(hr)) hr = frame->Commit(); if (SUCCEEDED(hr)) hr = encoder->Commit();
		return SUCCEEDED(hr);
	}
}

namespace Inkeys::UI::Bar
{
	int RunGeometryAttributeOffscreenTest()
	{
		// 只初始化 UI 共享设备与纹理，不创建 HWND、不启动主窗口或后台交互线程。
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
		_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE); _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
		const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED); if (FAILED(com)) return 2;
		if (FAILED(RenderPipeline::Initialize())) { CoUninitialize(); return 3; }
		IdtFontFileLoader::IsLoaderInitialized(); IdtFontCollectionLoader::IsLoaderInitialized();
		const std::array<UINT,4> fonts{IDR_TTF1, IDR_TTF7, IDR_TTF3, IDR_TTF8};
		const auto fontHr = RenderPipeline::InitializeFontCollection(IdtFontFileLoader::GetLoader(), IdtFontCollectionLoader::GetLoader(), fonts);
		std::filesystem::create_directories(L"Build/shape-p2-ui");
		// 全部真实业务写回及配置归一化都落到 fixture 目录，不能覆盖用户偏好。
		const auto originalGlobalPath = globalPath;
		globalPath = (std::filesystem::absolute(L"Build/shape-p2-ui") / L"runtime-config").wstring() + L"\\";
		std::filesystem::create_directories(std::filesystem::path(globalPath) / L"Inkeys/Config");
		std::ofstream report("Build/shape-p2-ui/results.log"); int failures = 0, checks = 0;
		auto expect = [&](bool ok, const char* why) { ++checks; if (!ok) { ++failures; report << "FAIL " << why << '\n'; } };
		expect(SUCCEEDED(fontHr), "shared multilingual font collection initializes");
		auto& owner = barUISet; SetThemeStyleSource(&owner.barStyle);
		expect(I18n::load(1, L"JSON", L"zh-CN"), "bundled base translation");
		owner.spec.ConfigureLocalizedTypography(); owner.barButtonSet.PresetInitialization(); owner.barMedia.LoadFormat();
		auto root = std::make_shared<BarUiSuperellipseClass>(100, 600, 80, 80, 3.0, 1.0, std::nullopt, std::nullopt);
		owner.superellipseMap[BarUISetSuperellipseEnum::MainButton] = root;
		auto main = std::make_shared<BarUiShapeClass>(330, 0, 580, 80, 8, 8, 1, std::nullopt, std::nullopt);
		owner.shapeMap[BarUISetShapeEnum::MainBar] = main;
		auto* anchor = owner.barButtonSet.preset[static_cast<int>(BarButtonPresetEnum::Geometry)];
		anchor->button.x.SetDirect(250); anchor->button.y.SetDirect(40); anchor->button.w.SetDirect(70); anchor->button.h.SetDirect(70);
		ChangeStateModeToShape();
		owner.barState.fold = false; owner.barState.geometryAttribute = true;
		owner.barState.drawAttribute = false; owner.barState.eraserAttribute = false; owner.barState.recallAttribute = false;
		owner.barState.moreExpanded = false; owner.barState.widgetPosition.primaryBar = false;
		BarUiAnimationEnabled = true; BarUiAnimationSpeedRate = 1;
		double zoom = 1; RECT work{0, 0, 1200, 800};
		auto tick = [&](int frames = 1, double dt = 1.0 / 60)
		{
			for (int f = 0; f < frames; ++f)
			{
				owner.geometryAttribute.Advance(owner, GetStateModeSnapshot(), dt, 1, zoom, work, {}, 0, 0);
				owner.geometryAttribute.CommitPresented();
			}
		};
		auto preset = [&](PresetKind p)
		{
			const auto s = GetStateModeVersionedSnapshot(); SetShapeToolPresetIfRevision(p, s.revision);
			expect(GetShapeToolSpec(GetStateModeSnapshot()).preset == p && StateModeTransitionRevision() > s.revision, "tool setter accepts current revision including repeated selection"); tick(90);
		};
		auto pointer = [&](USHORT type, int item, bool held, bool contact = false)
		{
			const auto f = owner.geometryAttribute.PresentationSnapshot(); const auto r = f.items[item];
			ExMessage message{}; message.message = type; message.lbutton = held;
			message.x = static_cast<short>(std::lround(EraserRectCenterX(r) * f.zoom));
			message.y = static_cast<short>(std::lround(EraserRectCenterY(r) * f.zoom));
			return owner.geometryAttribute.Pointer(owner, message, false, contact);
		};
		auto click = [&](int item, bool contact = false)
		{
			expect(pointer(WM_LBUTTONDOWN, item, true, contact), "presented down consumed"); tick(4);
			expect(pointer(WM_LBUTTONUP, item, false, contact), "presented up consumed"); tick(90);
		};
		expect(SUCCEEDED(owner.spec.EnsureDeviceResources(RenderPipeline::GetDeviceEpoch(), 1200, 800)), "offscreen target setup");
		owner.spec.SetFrameZoom(1); preset(PresetKind::Line);
		expect(!owner.geometryAttribute.Active(), "stable panel sleeps between carousel deadlines");
		auto f = owner.geometryAttribute.PresentationSnapshot();
		expect(f.visible && f.acceptsInput && f.items[23].Width() == 0 && f.items[26].Width() > 0, "line hides fill style and keeps stroke controls");
		expect(f.items[5].top > f.items[0].top && f.items[10].left > f.items[5].left, "eleven categories use at most six columns");
		expect(f.items[35].right <= f.items[34].left, "current color entry and close do not overlap");
		const auto selectedBefore = GetShapeToolSpec(GetStateModeSnapshot());
		const auto carouselBefore = BarGeometryAttributeTestAccess::CarouselSides(owner.geometryAttribute);
		unsigned retry = 0;
		expect(!owner.geometryAttribute.PollCarouselTimer(retry, std::chrono::steady_clock::now()) && retry > 0, "early timer returns remaining deadline");
		expect(owner.geometryAttribute.PollCarouselTimer(retry, std::chrono::steady_clock::now() + std::chrono::seconds(2)), "one expired timer advances one icon");
		tick(60);
		expect(BarGeometryAttributeTestAccess::CarouselSides(owner.geometryAttribute) != carouselBefore && GetShapeToolSpec(GetStateModeSnapshot()) == selectedBefore, "carousel does not mutate actual sides or selection");
		expect(!owner.geometryAttribute.Active(), "content transition settles before next timer");

		// 真实共享按钮按压/回弹，命中几何不跟随 draw-only pressScale 收缩。
		f = owner.geometryAttribute.PresentationSnapshot(); const auto beforeHit = f.items[1];
		pointer(WM_MOUSEMOVE, 1, false); tick(15); pointer(WM_LBUTTONDOWN, 1, true); tick(10);
		expect(BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, 1).pressScale.val < 1 && owner.geometryAttribute.PresentationSnapshot().items[1] == beforeHit, "press uses shared scale without moving presented hit");
		expect(!BarGeometryAttributeTestAccess::CarouselArmed(owner.geometryAttribute), "press pauses carousel deadline");
		pointer(WM_LBUTTONUP, 1, false); tick(90);
		expect(GetShapeToolSpec(GetStateModeSnapshot()).preset == PresetKind::Arrow && BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, 1).pressScale.val == 1, "release commits once and rebounds");
		pointer(WM_LBUTTONDOWN, 2, true); pointer(WM_MOUSEMOVE, 0, true); pointer(WM_MOUSEMOVE, 2, true); pointer(WM_LBUTTONUP, 2, false); tick(60);
		expect(GetShapeToolSpec(GetStateModeSnapshot()).preset == PresetKind::Arrow, "drag out remains cancelled after reenter");
		pointer(WM_LBUTTONDOWN, 2, true);
		{ const auto s = GetStateModeVersionedSnapshot(); SetShapeToolPresetIfRevision(PresetKind::Ellipse, s.revision); }
		pointer(WM_LBUTTONUP, 2, false); tick(60);
		expect(GetShapeToolSpec(GetStateModeSnapshot()).preset == PresetKind::Ellipse, "stale press cannot overwrite newer tool revision");
		pointer(WM_LBUTTONDOWN, 2, true);
		ExMessage cancel{}; owner.geometryAttribute.Pointer(owner, cancel, true, true); pointer(WM_LBUTTONUP, 2, false, true); tick(60);
		expect(GetShapeToolSpec(GetStateModeSnapshot()).preset == PresetKind::Ellipse, "capture cancellation leaves tool unchanged");
		for (int i = 0; i < 11; ++i) { click(i, i % 2 != 0); expect(static_cast<int>(GetShapeToolSpec(GetStateModeSnapshot()).preset) == i, "every category commits through normalized pointer path"); }
		expect(!BarGeometryAttributeTestAccess::CarouselArmed(owner.geometryAttribute), "selected polygon stops carousel");
		for (unsigned sides = 3; sides <= 12; ++sides)
		{
			click(13 + sides - 3); const auto tool = GetShapeToolSpec(GetStateModeSnapshot());
			expect(tool.sides == sides && ShapeToolIconResource(tool) == L"barShapePolygon" + std::to_wstring(sides), "numeric sides select exact centered polygon icon");
		}
		preset(PresetKind::Circle); f = owner.geometryAttribute.PresentationSnapshot();
		expect(f.items[11].Width() > 0 && f.items[13].Width() == 0, "circle displays two mode options only");
		click(12); expect(GetShapeToolSpec(GetStateModeSnapshot()).creationMode == CreationMode::Drag, "bounding square mode");
		click(11); expect(GetShapeToolSpec(GetStateModeSnapshot()).creationMode == CreationMode::CenterRadius, "center radius mode");
		click(24); f = owner.geometryAttribute.PresentationSnapshot();
		expect(GetShapeFillMode(GetStateModeSnapshot()) == draw3::shapes::FillMode::Fill && f.items[26].Width() == 0 && f.items[30].Width() == 0, "fill-only hides line and width groups");
		click(25); for (int pattern = 0; pattern < 4; ++pattern) { click(26 + pattern); expect(static_cast<int>(GetShapeStyle(GetStateModeSnapshot()).linePattern) == pattern, "line pattern current revision setter"); }
		preset(PresetKind::Line); expect(GetShapeStyle(GetStateModeSnapshot()).linePattern == LinePattern::Solid, "open and closed line memories independent");
		preset(PresetKind::Rectangle); expect(GetShapeStyle(GetStateModeSnapshot()).linePattern == LinePattern::DashDot, "closed shape pattern persists across tools");
		// 精调、颜色入口只切面板，不切成画笔或改当前形状参数。
		const auto editTool = GetShapeToolSpec(GetStateModeSnapshot()); click(33);
		expect(owner.barState.geometryThicknessEditing && owner.barState.drawAttribute && !owner.barState.geometryAttribute && GetShapeToolSpec(GetStateModeSnapshot()) == editTool && GetStateModeSnapshot().StateModeSelect == StateModeSelectEnum::IdtShape, "fine adjustment routes to existing shape-preserving thickness subview");
		BarGeometryAttributeTestAccess::FinishThickness(owner);
		expect(!owner.barState.geometryThicknessEditing && !owner.barState.drawAttribute && owner.barState.geometryAttribute, "normal thickness close returns to shape panel");
		tick(90); click(33); owner.CollapseAuxiliaryPanels(false);
		expect(!owner.barState.geometryThicknessEditing && !owner.barState.geometryAttribute, "workspace cancel does not reopen shape panel");
		owner.barState.geometryAttribute = true; tick(90); click(35);
		expect(owner.barState.geometryColorEditing && owner.barState.drawAttributeBar.colorPickerOpen && GetShapeToolSpec(GetStateModeSnapshot()) == editTool, "color routes to existing shape-preserving picker");
		BarGeometryAttributeTestAccess::FinishColor(owner);
		expect(!owner.barState.geometryColorEditing && !owner.barState.drawAttribute && owner.barState.geometryAttribute, "normal color close returns to shape panel");
		tick(90); click(35); owner.CollapseAuxiliaryPanels(false); owner.barState.geometryAttribute = true; tick(90);

		// 正式资源、三语、DPI、上下展开、主题与设备代次均通过实际面板渲染。
		int frames = 0;
		for (const wchar_t* language : {L"zh-CN", L"zh-TW", L"en-US"})
		{
			expect(I18n::load(1, L"JSON", language), "all bundled languages load"); owner.spec.ConfigureLocalizedTypography();
			for (bool dark : {false, true}) for (double scale : {0.75, 1.0, 1.5, 2.0}) for (bool below : {false, true})
			{
				zoom = scale; work = {0, 0, static_cast<LONG>(1200 * zoom), static_cast<LONG>(800 * zoom)};
				owner.barStyle.zoom = zoom; owner.barStyle.darkStyle = dark; owner.barState.widgetPosition.primaryBar = below;
				root->y.SetDirect(below ? 120 : 600); BarUiAnimationEnabled = true;
				const auto epoch = RenderPipeline::GetDeviceEpoch();
				expect(SUCCEEDED(owner.spec.EnsureDeviceResources(epoch, work.right, work.bottom)), "DPI target resources"); owner.spec.SetFrameZoom(zoom);
				preset(below ? PresetKind::Circle : PresetKind::RegularPolygon); tick(60);
				f = owner.geometryAttribute.PresentationSnapshot();
				expect(f.visible && f.acceptsInput && f.panel.left >= 0 && f.panel.top >= 0 && f.panel.right <= 1200 && f.panel.bottom <= 800, "panel and parameters fit both expansion sides");
				for (int i = 0; i < 11; ++i)
				{
					const auto& b = BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, i);
					expect(owner.spec.MeasureText(b.name.content.GetVal(), b.name.size.val).width <= b.name.w.val + 0.5, "localized category label fits measured column");
				}
				owner.spec.PrepareFrameLighting(1.0 / 60, {}, static_cast<int>(StateModeSelectEnum::IdtShape), 0, GetPenColor(GetStateModeSnapshot()), 0);
				auto* dc = owner.spec.GetDeviceContext(); dc->BeginDraw(); dc->SetTransform(D2D1::IdentityMatrix()); dc->Clear(D2D1::ColorF(0, 0, 0, 0));
				owner.geometryAttribute.Draw(owner.spec, dc); const HRESULT hr = dc->EndDraw(); expect(SUCCEEDED(hr), "production panel draw completes");
				const auto pixels = ReadPixels(dc, owner.spec.GetTargetBitmap());
				expect(!pixels.empty() && std::any_of(pixels.begin(), pixels.end(), [](auto v) { return v != 0; }), "production panel produces actual pixels");
				for (int i = 0; i < 11; ++i) expect(BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, i).icon.cacheBitmap != nullptr, "every category rasterizes embedded SVG");
				if (scale == 1 && !below && language == std::wstring(L"zh-CN"))
					expect(SavePng(dark ? L"Build/shape-p2-ui/panel-dark.png" : L"Build/shape-p2-ui/panel-light.png", pixels, work.right, work.bottom), "QA screenshot writes from offscreen pixels");
				++frames;
			}
		}
		// 设备资源重建保留原 SVG 载荷；关闭动画立即稳定并撤销轮播唤醒。
		owner.spec.DiscardDeviceResources(); expect(SUCCEEDED(owner.spec.EnsureDeviceResources(RenderPipeline::GetDeviceEpoch(), work.right, work.bottom)), "device resources rebuild");
		BarUiAnimationEnabled = false; owner.barState.geometryAttribute = false; tick(1, 0);
		expect(!owner.geometryAttribute.PresentationSnapshot().visible && !BarGeometryAttributeTestAccess::CarouselArmed(owner.geometryAttribute), "animation-disabled close stops all scheduled wakeups");
		owner.barState.geometryAttribute = true; preset(PresetKind::Circle); tick(1, 0);
		expect(owner.geometryAttribute.PresentationSnapshot().opacity == 1 && !owner.geometryAttribute.Active() && !BarGeometryAttributeTestAccess::CarouselArmed(owner.geometryAttribute), "animation-disabled open reaches full state without polling");
		const auto sf = owner.geometryAttribute.PresentationSnapshot();
		ExMessage corner{}; corner.message = WM_LBUTTONDOWN; corner.x = static_cast<short>(sf.panel.left * zoom); corner.y = static_cast<short>(sf.panel.top * zoom);
		expect(!owner.geometryAttribute.Pointer(owner, corner), "rounded transparent corner does not own input");
		owner.geometryAttribute.Close(owner); owner.spec.DiscardDeviceResources();

		// 走完整生产协调器，检查借用属性的实际绘制/布局/成功呈现命中，避免独立面板测试漏掉旧控件残留。
		owner.barWindow.x = owner.barWindow.y = 0; owner.barWindow.w = 1200; owner.barWindow.h = 800;
		(void)owner.InitializeStartupPlacement(Inkeys::UI::StartupPlacement::BottomLeftFloating);
		InitializeUI(owner); BarGeometryAttributeTestAccess::Display(owner, 1200, 800);
		owner.barStyle.dpiZoom = 1; owner.barStyle.configZoom = 1; owner.barStyle.zoom = 1;
		owner.barButtonSet.Load(); owner.barButtonSet.StateUpdate();
		owner.barState.fold = false; owner.barState.geometryAttribute = true;
		owner.barState.drawAttribute = false; BarUiAnimationEnabled = true;
		unsigned actualCommits = 0;
		auto scene = CreateBarOffscreenRenderCallback(owner, [&](const UPDATELAYEREDWINDOWINFO& receipt)
		{
			++actualCommits; return receipt.hdcSrc && receipt.psize && receipt.psize->cx > 0 && receipt.psize->cy > 0;
		});
		auto sceneTime = std::chrono::steady_clock::now();
		auto sceneTick = [&](int count = 90)
		{
			owner.UpdateRendering(false);
			for (int i = 0; i < count; ++i)
			{
				sceneTime += std::chrono::milliseconds(16);
				const auto result = scene(RenderPipeline::FrameContext{RenderPipeline::GetDeviceEpoch(), RenderPipeline::GetSharedAssets(), sceneTime});
				expect(result != RenderPipeline::FrameResult::DeviceLost, "actual scene keeps its production device");
			}
		};
		sceneTick(2); owner.barState.fold = false; owner.barState.geometryAttribute = true; sceneTick();
		expect(actualCommits > 0 && owner.geometryAttribute.PresentationSnapshot().acceptsInput, "actual scene commits complete shape panel");
		auto hit = [&](BarUISetShapeEnum key)
		{
			const auto s = owner.shapeMap[key];
			return owner.HitTestPresentedShape(*s, s->inhX + s->w.val / 2, s->inhY + s->h.val / 2);
		};
		for (int i = static_cast<int>(BarUISetShapeEnum::GeometryAttributeBar);
			i <= static_cast<int>(BarUISetShapeEnum::GeometryAttributeBar_Close); ++i)
			expect(!hit(static_cast<BarUISetShapeEnum>(i)), "retired geometry subtree owns no transparent hit");
		auto allHidden = [&](bool thickness)
		{
			for (int i = static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ColorSelect1);
				i <= static_cast<int>(BarUISetShapeEnum::DrawAttributeBar_ColorSelect12Inner); ++i)
			{
				const auto key = static_cast<BarUISetShapeEnum>(i);
				if (!IsDrawAttributeVisualAllowed(owner.barState, key))
					expect(!owner.shapeMap[key]->enable.val && !hit(key), thickness ? "fine subview withdraws hidden pixels and transparent hits" : "color subview withdraws hidden pixels and transparent hits");
			}
			for (const auto& [key, svg] : owner.svgMap)
				if (!IsDrawAttributeVisualAllowed(owner.barState, key)) expect(!svg->enable.val, "hidden SVG cannot survive hover retarget");
			for (const auto& [key, word] : owner.wordMap)
				if (!IsDrawAttributeVisualAllowed(owner.barState, key)) expect(!word->enable.val, "hidden labels cannot survive hover retarget");
		};
		auto saveScene = [&](const wchar_t* filename)
		{
			const auto size = owner.spec.GetTargetBitmap()->GetPixelSize();
			const auto bytes = ReadPixels(owner.spec.GetDeviceContext(), owner.spec.GetTargetBitmap());
			expect(SavePng(std::filesystem::path(L"Build/shape-p2-ui") / filename, bytes, size.width, size.height), "actual scene pixel evidence");
		};
		// 记忆的荧光/激光状态不能劫持图形的宽度量程或预览。
		ChangeStateModeToPenTool(PenToolSelectionEnum::Highlighter); ChangeStateModeToShape();
		{ const auto s = GetStateModeVersionedSnapshot(); SetShapeToolPresetIfRevision(PresetKind::Ellipse, s.revision); }
		const auto penBefore = GetStateModeSnapshot().Pen;
		const auto shapeBefore = GetShapeToolSpec(GetStateModeSnapshot());
		for (bool below : {false, true})
		{
			if (below) { ChangeStateModeToPenTool(PenToolSelectionEnum::Laser); ChangeStateModeToShape(); }
			owner.barState.widgetPosition.primaryBar = below; owner.BeginGeometryAttributeEdit(true); sceneTick();
			expect(owner.barState.geometryThicknessEditing && GetShapeToolSpec(GetStateModeSnapshot()) == shapeBefore, "actual fine subview retains shape identity");
			allHidden(true);
			expect(hit(BarUISetShapeEnum::DrawAttributeBar_ThicknessSliderHit), "actual shape slider owns presented hit");
			expect(PenModeUsesThicknessPresets(GetStateModeSnapshot().Pen.ModeSelect, GetStateModeSnapshot()), "remembered laser cannot hide shape presets");
			const auto presetMetrics = GetBarThicknessPresetMetrics(GetStateModeSnapshot().Pen.ModeSelect, 1, GetStateModeSnapshot());
			expect(presetMetrics.resolvedWidthDip == GetBarThicknessPresetMetrics(PenModeSelectEnum::IdtPenSoftPen, 1, GetStateModeSnapshot()).resolvedWidthDip, "remembered highlighter cannot change shape preset family");
			auto panel = owner.shapeMap[BarUISetShapeEnum::DrawAttributeBar];
			auto region = owner.shapeMap[BarUISetShapeEnum::DrawAttributeBar_ThicknessSelect];
			expect(region->inhX >= panel->inhX && region->inhY >= panel->inhY && region->inhX + region->w.val <= panel->inhX + panel->w.val + 0.01 && region->inhY + region->h.val <= panel->inhY + panel->h.val + 0.01, "actual fine region fits compact panel");
			for (int presetIndex = 0; presetIndex < 3; ++presetIndex)
			{
				const auto s = GetStateModeVersionedSnapshot(); ApplyBarThicknessPresetIfRevision(presetIndex, s.revision, false);
				expect(GetPenWidthSelection(GetStateModeSnapshot()).preset == static_cast<Inkeys::Business::PenWidth::Preset>(presetIndex), "shared no-save preset adapter writes current shape identity");
			}
			{ const auto s = GetStateModeVersionedSnapshot(); expect(SetPenWidthIfRevision(7.25, s.revision, false), "shared fine setter accepts current shape"); }
			sceneTick(30); expect(std::abs(GetPenWidthMetrics(GetStateModeSnapshot()).resolvedWidthDip - 7.25) < 0.000001, "fine value writes current shape slot");
			if (!below) saveScene(L"scene-fine.png");
			BarGeometryAttributeTestAccess::FinishThickness(owner); sceneTick();
			expect(owner.barState.geometryAttribute && owner.geometryAttribute.PresentationSnapshot().acceptsInput, "actual fine close restores shape panel");
			owner.BeginGeometryAttributeEdit(false); sceneTick(); allHidden(false);
			expect(hit(BarUISetShapeEnum::DrawAttributeBar_ColorPickerPalette) && hit(BarUISetShapeEnum::DrawAttributeBar_ColorSelect1), "actual picker and swatches retain presented hit");
			{ const auto color = RGB(35, 90, 165); expect(SetPenColor(color, false) && GetPenColor(GetStateModeSnapshot()) == color, "shared color setter writes current shape slot"); }
			sceneTick(30); if (!below) saveScene(L"scene-color.png");
			BarGeometryAttributeTestAccess::FinishColor(owner); sceneTick();
			expect(owner.barState.geometryAttribute && owner.geometryAttribute.PresentationSnapshot().acceptsInput, "actual color close restores shape panel");
		}
		expect(GetStateModeSnapshot().Pen.Highlighter1.color == penBefore.Highlighter1.color && GetStateModeSnapshot().Pen.Highlighter1.width == penBefore.Highlighter1.width, "shape subviews leave remembered pen untouched");
		owner.BeginGeometryAttributeEdit(true); ChangeStateModeToPenTool(PenToolSelectionEnum::SoftPen); sceneTick();
		expect(!owner.barState.geometryThicknessEditing && !owner.barState.geometryAttribute && !owner.barState.drawAttribute, "tool change cancels borrowed view without reopening shape panel");
		owner.barState.drawAttribute = true; sceneTick();
		expect(owner.shapeMap[BarUISetShapeEnum::DrawAttributeBar_Brush1]->enable.val && owner.svgMap[BarUISetSvgEnum::DrawAttributeBar_Brush1]->enable.val && hit(BarUISetShapeEnum::DrawAttributeBar_Brush1), "normal pen subtree restores original drawing and hits");
		saveScene(L"scene-pen-restored.png"); owner.CollapseAuxiliaryPanels(false); sceneTick();
		// 固定70DIP主栏槽位独立于展开面板：全部类别在三语下都不能被NO_WRAP+CLIP截断。
		ChangeStateModeToShape(); owner.barState.fold = false; owner.barState.geometryAttribute = false;
		for (const wchar_t* language : {L"zh-CN", L"zh-TW", L"en-US"})
		{
			expect(I18n::load(1, L"JSON", language), "main geometry label language loads"); owner.spec.ConfigureLocalizedTypography();
			for (int i = 0; i < 11; ++i)
			{
				const auto s = GetStateModeVersionedSnapshot(); SetShapeToolPresetIfRevision(static_cast<PresetKind>(i), s.revision);
				owner.UpdateRendering();
				for (int frame = 0; frame < 50; ++frame)
				{
					sceneTick(1);
					auto& word = owner.barButtonSet.preset[static_cast<int>(BarButtonPresetEnum::Geometry)]->name;
					if (word.pct.val > 0 && word.contentPct > 0.01)
						expect(owner.spec.MeasureText(word.content.GetVal(), word.size.val, DWRITE_FONT_WEIGHT_BOLD).width <= word.w.val + 0.5,
							"current main geometry glyphs fit throughout shared content transition");
				}
				auto& word = owner.barButtonSet.preset[static_cast<int>(BarButtonPresetEnum::Geometry)]->name;
				expect(word.content.GetVal() == ShapeToolLabel(static_cast<PresetKind>(i)) &&
					owner.spec.MeasureText(word.content.GetVal(), word.size.val, DWRITE_FONT_WEIGHT_BOLD).width <= word.w.val + 0.5,
					"all eleven localized main labels fit original slot");
				if (std::wstring(language) == L"en-US" && i == static_cast<int>(PresetKind::Parallelogram)) saveScene(L"main-parallelogram-en.png");
				if (std::wstring(language) == L"en-US" && i == static_cast<int>(PresetKind::RegularPolygon)) saveScene(L"main-regular-polygon-en.png");
			}
		}
		scene = {}; owner.spec.DiscardDeviceResources();
		report << "Actual scene commits=" << actualCommits << '\n';
		globalPath = originalGlobalPath;
		report << "Geometry UI checks=" << checks << " frames=" << frames << " failures=" << failures << '\n';
		std::cerr << "Geometry UI checks=" << checks << " frames=" << frames << " failures=" << failures << '\n'; report.close();
		RenderPipeline::Shutdown(); CoUninitialize(); return failures == 0 ? 0 : 1;
	}
}
