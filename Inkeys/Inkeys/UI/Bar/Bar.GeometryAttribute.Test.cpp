module;
#include "../../../IdtMain.h"
#include "../../../IdtState.h"
#include "../../../IdtConfiguration.h"
#include "../StartupPlacement.h"
#include "../../../IdtI18n.h"
#include "../../../IdtI18nKeys.g.h"
#include "../../../resource.h"
#include "../../Drawing/Draw3/Draw3.ShapeInteraction.h"
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
	static const BarButtonClass& Button(const BarGeometryAttributePanel& p, int item) { return *p.buttons_[item]; }
	static bool CarouselArmed(const BarGeometryAttributePanel& p) { return p.carouselArmed_; }
	static unsigned CarouselSides(const BarGeometryAttributePanel& p) { return p.carouselSides_; }
	static auto Categories(const BarGeometryAttributePanel& p) { return p.categoryList_.Snapshot(); }
	static auto Attributes(const BarGeometryAttributePanel& p) { return p.attributeList_.Snapshot(); }
	static auto Candidate(const BarGeometryAttributePanel& p) { return p.frame_; }
	static auto PromptFrame(const BarConstructionPrompt& p) { return p.presented_; }
	static const BarUiWordClass& PromptLabel(const BarConstructionPrompt& p) { return p.label_; }
	static const BarButtonClass& PromptButton(const BarConstructionPrompt& p,int i) { return *p.buttons_[i]; }
	static bool DockIndicatorVisible(const BarUISetClass& p) { return p.bottomDockIndicatorPresentedVisible.load(); }
	static int Focused(const BarGeometryAttributePanel& p) { return p.focused_; }
	static void Focus(BarGeometryAttributePanel& p, int item) { p.focused_ = item; }
	static void Execute(BarGeometryAttributePanel& p, BarUISetClass& owner, int item, std::uint64_t revision) { p.Execute(owner, item, revision); }
	static double ScrollRequest(const BarGeometryAttributePanel& p) { return p.scrollRequest_; }
	static bool ScrollDragging(const BarGeometryAttributePanel& p) { return p.scrollDragging_; }
	static void MoveRoot(BarUISetClass& owner, double x, double y)
	{
		const auto root = owner.superellipseMap[BarUISetSuperellipseEnum::MainButton];
		const double zoom = owner.barStyle.zoom;
		// 使用产品松手后的直移吸收入口，让真实PositionUpdate决定上下展开方向。
		owner.directWindowDragTranslationX = static_cast<LONG>(std::lround((x - root->x.val) * zoom));
		owner.directWindowDragTranslationY = static_cast<LONG>(std::lround((y - root->y.val) * zoom));
	}
	static void Display(BarUISetClass& owner, LONG width, LONG height, UINT dpi = USER_DEFAULT_SCREEN_DPI)
	{
		owner.pendingDisplaySerial.fetch_add(1);
		owner.pendingDisplayLeft = owner.pendingDisplayTop = owner.pendingWorkAreaLeft = owner.pendingWorkAreaTop = 0;
		owner.pendingDisplayRight = owner.pendingWorkAreaRight = width;
		owner.pendingDisplayBottom = owner.pendingWorkAreaBottom = height;
		owner.pendingDisplayDpi = dpi;
		owner.pendingDisplaySerial.fetch_add(1);
	}
};

namespace
{
	// 逐字段比较完整业务快照，不能用含padding的memcmp，也不能只看当前工具名。
	bool SameBusinessSnapshot(const StateModeVersionedSnapshot& before, const StateModeVersionedSnapshot& after)
	{
		if (before.revision != after.revision) return false;
		const auto& a = before.state; const auto& b = after.state;
		auto selection = [](const auto& x, const auto& y) { return x.source == y.source && x.preset == y.preset && x.explicitDip == y.explicitDip; };
		auto stroke = [&](const auto& x, const auto& y) { return x.width == y.width && x.color == y.color && selection(x.widthSelection, y.widthSelection); };
		return a.StateModeSelect == b.StateModeSelect && a.StateModeSelectTarget == b.StateModeSelectTarget && a.StateModeSelectEcho == b.StateModeSelectEcho &&
			a.laserActive == b.laserActive && a.lastAnnotationMode == b.lastAnnotationMode &&
			a.penWidthContext.revision == b.penWidthContext.revision && a.penWidthContext.paintDevice == b.penWidthContext.paintDevice &&
			a.penWidthContext.effectiveDpi == b.penWidthContext.effectiveDpi && a.penWidthContext.pixelWidth == b.penWidthContext.pixelWidth && a.penWidthContext.pixelHeight == b.penWidthContext.pixelHeight &&
			a.Pen.ModeSelect == b.Pen.ModeSelect && stroke(a.Pen.Brush1, b.Pen.Brush1) && a.Pen.Brush1.widthPreset == b.Pen.Brush1.widthPreset &&
			stroke(a.Pen.Highlighter1, b.Pen.Highlighter1) && a.Pen.Highlighter1.widthPreset == b.Pen.Highlighter1.widthPreset &&
			stroke(a.Pen.Laser, b.Pen.Laser) && std::equal(std::begin(a.Pen.Laser.widthPreset), std::end(a.Pen.Laser.widthPreset), std::begin(b.Pen.Laser.widthPreset)) &&
			a.Shape.ModeSelect == b.Shape.ModeSelect && a.Shape.circleMode == b.Shape.circleMode && a.Shape.polygonSides == b.Shape.polygonSides &&
			a.Shape.fillMode == b.Shape.fillMode && a.Shape.openPattern == b.Shape.openPattern && a.Shape.closedPattern == b.Shape.closedPattern &&
			stroke(a.Shape.StraightLine1, b.Shape.StraightLine1) && stroke(a.Shape.Rectangle1, b.Shape.Rectangle1);
	}
	EraserAttributeRect Intersect(EraserAttributeRect a, EraserAttributeRect b)
	{
		const EraserAttributeRect result{(std::max)(a.left, b.left), (std::max)(a.top, b.top), (std::min)(a.right, b.right), (std::min)(a.bottom, b.bottom)};
		return result.Width() > 0 && result.Height() > 0 ? result : EraserAttributeRect{};
	}
	std::vector<unsigned char> ReadPixels(ID2D1DeviceContext* dc, ID2D1Bitmap1* target, const D2D1_RECT_U* crop = nullptr)
	{
		ComPtr<ID2D1Bitmap1> readable;
		const auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, target->GetPixelFormat());
		const auto size = crop ? D2D1::SizeU(crop->right - crop->left, crop->bottom - crop->top) : target->GetPixelSize();
		if (FAILED(dc->CreateBitmap(size, nullptr, 0, &props, &readable)) ||
			FAILED(readable->CopyFromBitmap(nullptr, target, crop))) return {};
		D2D1_MAPPED_RECT map{}; if (FAILED(readable->Map(D2D1_MAP_OPTIONS_READ, &map))) return {};
		std::vector<unsigned char> bytes(size.width * size.height * 4);
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
		std::string caseContext;
		auto expect = [&](bool ok, const char* why) { ++checks; if (!ok) { ++failures; report << "FAIL " << why << " [" << caseContext << "]\n"; } };
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
		expect(f.visible && f.acceptsInput && f.items[GeometryStyleBegin].Width() == 0 && f.items[GeometryPatternBegin].Width() > 0, "line hides fill style and keeps stroke controls");
		const auto list = BarGeometryAttributeTestAccess::Categories(owner.geometryAttribute);
		const auto grid = ResolveBarButtonListLayout(list, BarButtonListLayoutKind::GeometryGrid);
		expect(list.size() == GeometryCategoryCount && grid.subColumns == 12 && grid.totalRows == 12 && !grid.showDivider, "shape-owned list uses its independent six-column policy");
		expect(f.items.size() == GeometryControlCount && f.items[GeometryCloseButton].left > f.items[5].right, "close is the only side-rail entry");
		expect(f.items[0].top == f.items[5].top && f.items[6].top > f.items[0].top && f.items[6].left == f.items[0].left,
			"shape list keeps row-major geometry order and wraps after six categories");
		for (const size_t count : {size_t{1}, size_t{6}, size_t{7}, size_t{11}, size_t{12}, size_t{35}, size_t{36}})
		{
			const std::vector<std::shared_ptr<BarButtonClass>> subset(list.begin(), list.begin() + count);
			const auto shapeGrid = ResolveBarButtonListLayout(subset, BarButtonListLayoutKind::GeometryGrid);
			expect(shapeGrid.placements.size() == count && shapeGrid.subColumns <= 12 && !shapeGrid.showDivider, "shape policy bounds only its own visible categories");
		}
		const auto standard = ResolveBarButtonVisualMetrics(BarButtonVisualLayoutKind::StandardTwoTwo);
		for (int i = 0; i < GeometryCategoryCount; ++i)
		{
			const auto& button = *list[i];
			expect(button.size == BarButtonSizeEnum::twoTwo && f.items[i].Width() == standard.buttonWidthDip && f.items[i].Height() == standard.buttonHeightDip,
				"category dimensions follow shared list metrics");
			expect(button.icon.y.val == standard.iconOffsetYDip && button.name.y.val == standard.primaryOffsetYDip && button.name.h.val == standard.primarySlotHeightDip,
				"shape list retains shared standard icon and label metrics");
		}
		// 轮播期限之间没有用户反馈时应持续静止，不能靠隐形动画维持渲染唤醒。
		std::array<double,60> stableAdvanceMs{};
		for (double& elapsed : stableAdvanceMs)
		{
			const auto begin = std::chrono::steady_clock::now();
			const bool changed = owner.geometryAttribute.Advance(owner, GetStateModeSnapshot(), 1.0 / 60, 1, zoom, work, {}, 0, 0);
			elapsed = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now() - begin).count();
			expect(!changed && !owner.geometryAttribute.Changed() && !owner.geometryAttribute.Active(), "stable open panel can sleep between carousel deadlines");
		}
		std::sort(stableAdvanceMs.begin(), stableAdvanceMs.end());
		report << "Stable panel Advance CPU ms median=" << stableAdvanceMs[30] << " p95=" << stableAdvanceMs[56] << " (not presentation latency)\n";
		const auto selectedBefore = GetShapeToolSpec(GetStateModeSnapshot());
		const auto carouselBefore = BarGeometryAttributeTestAccess::CarouselSides(owner.geometryAttribute);
		unsigned retry = 0;
		expect(!owner.geometryAttribute.PollCarouselTimer(retry, std::chrono::steady_clock::now()) && retry > 0, "early timer returns remaining deadline");
		expect(owner.geometryAttribute.PollCarouselTimer(retry, std::chrono::steady_clock::now() + std::chrono::seconds(2)), "one expired timer advances one icon");
		tick(60);
		expect(BarGeometryAttributeTestAccess::CarouselSides(owner.geometryAttribute) != carouselBefore && GetShapeToolSpec(GetStateModeSnapshot()) == selectedBefore, "carousel does not mutate actual sides or selection");
		expect(!owner.geometryAttribute.Active(), "content transition settles before next timer");

		const auto& catalog = ShapeCategoryCatalog();
		expect(catalog.size() == 36 && GeometryImplementedCategoryCount == 21 && GeometryControlCount == 59, "approved catalog has 36 UI tools and has twenty-one enabled business presets");
		for (int i = 0; i < GeometryCategoryCount; ++i)
		{
			expect(catalog[i].key && catalog[i].resource && !ShapeCategoryLabel(i).empty(), "every catalog entry has stable UI key resource and label");
			expect(catalog[i].preset.has_value() == (i < 15 || (i >= 18 && i < 24)), "future catalog tools do not invent business preset mappings");
			for (int j = 0; j < i; ++j) expect(std::wstring(catalog[i].key) != catalog[j].key, "catalog UI keys are unique");
		}
		for (int item = 0; item < GeometryCategoryCount; ++item)
		{
			if (catalog[item].preset) continue;
			caseContext = "disabled tool=" + std::to_string(item);
			const auto before = GetStateModeVersionedSnapshot();
			pointer(WM_MOUSEMOVE, item, false); tick(10);
			for (bool contact : {false, true})
			{
				expect(pointer(WM_LBUTTONDOWN, item, true, contact), "disabled category consumes pointer down instead of canvas fallthrough"); tick(5);
				expect(pointer(WM_LBUTTONUP, item, false, contact), "disabled category consumes pointer up instead of canvas fallthrough");
				pointer(WM_LBUTTONDBLCLK, item, true, contact); pointer(WM_LBUTTONUP, item, false, contact);
				ExMessage lost{}; owner.geometryAttribute.Pointer(owner, lost, true, contact); tick(40);
				expect(SameBusinessSnapshot(before, GetStateModeVersionedSnapshot()), "disabled mouse and contact actions preserve all business fields and revision");
			}
			BarGeometryAttributeTestAccess::Execute(owner.geometryAttribute, owner, item, before.revision);
			BarGeometryAttributeTestAccess::Focus(owner.geometryAttribute, item);
			owner.geometryAttribute.Keyboard(owner, VK_RETURN, true); owner.geometryAttribute.Keyboard(owner, VK_SPACE, true);
			expect(SameBusinessSnapshot(before, GetStateModeVersionedSnapshot()), "direct Execute and stale forced keyboard focus cannot activate future tool");
			const auto& button = BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, item);
			expect(button.state->state == BarWidgetState::Disable && std::abs(button.icon.pct.val - 0.30) < 0.000001 && std::abs(button.name.pct.val - 0.30) < 0.000001,
				"future tools use shared disabled state and 0.30 icon label opacity");
			expect(button.pressScale.val == 1 && !f.hits[item].enabled, "future tools do not acquire successful press feedback or action hit");
		}
		for (BYTE key : {static_cast<BYTE>(VK_TAB), static_cast<BYTE>(VK_LEFT), static_cast<BYTE>(VK_RIGHT)})
			for (int i = 0; i < GeometryControlCount * 2; ++i)
			{
				owner.geometryAttribute.Keyboard(owner, key, true);
				const int focus = BarGeometryAttributeTestAccess::Focused(owner.geometryAttribute);
				expect(focus < 0 || focus >= GeometryCategoryCount || catalog[focus].preset.has_value(), "keyboard navigation skips all fifteen disabled categories");
			}
		owner.geometryAttribute.ResetPointerFeedback(); caseContext.clear(); tick(90);

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
		for (int i = 0; i < GeometryCategoryCount; ++i) if (catalog[i].preset) { click(i, i % 2 != 0); expect(GetShapeToolSpec(GetStateModeSnapshot()).preset == *catalog[i].preset, "every enabled category commits through normalized pointer path"); }
		preset(PresetKind::RegularPolygon); // 全类别遍历现在以Pyramid结束，轮播和边数用例须显式选择原目标。
		expect(!BarGeometryAttributeTestAccess::CarouselArmed(owner.geometryAttribute), "selected polygon stops carousel");
		for (unsigned sides = 3; sides <= 12; ++sides)
		{
			click(GeometrySidesBegin + sides - 3); const auto tool = GetShapeToolSpec(GetStateModeSnapshot());
			expect(tool.sides == sides && ShapeToolIconResource(tool) == L"barShapePolygon" + std::to_wstring(sides), "numeric sides select exact centered polygon icon");
		}
		preset(PresetKind::Circle); f = owner.geometryAttribute.PresentationSnapshot();
		expect(f.items[GeometryCircleCenter].Width() > 0 && f.items[GeometrySidesBegin].Width() == 0, "circle displays two mode options only");
		click(GeometryCircleBox); expect(GetShapeToolSpec(GetStateModeSnapshot()).creationMode == CreationMode::Drag, "bounding square mode");
		click(GeometryCircleCenter); expect(GetShapeToolSpec(GetStateModeSnapshot()).creationMode == CreationMode::CenterRadius, "center radius mode");
		click(GeometryStyleBegin + 1); f = owner.geometryAttribute.PresentationSnapshot();
		expect(GetShapeFillMode(GetStateModeSnapshot()) == draw3::shapes::FillMode::Fill && f.items[GeometryPatternBegin].Width() == 0 && f.items[GeometryThicknessBegin].Width() == 0, "fill-only hides line and width groups");
		click(GeometryStyleBegin + 2); for (int pattern = 0; pattern < 4; ++pattern) { click(GeometryPatternBegin + pattern); expect(static_cast<int>(GetShapeStyle(GetStateModeSnapshot()).linePattern) == pattern, "line pattern current revision setter"); }
		preset(PresetKind::Line); expect(GetShapeStyle(GetStateModeSnapshot()).linePattern == LinePattern::Solid, "open and closed line memories independent");
		preset(PresetKind::Rectangle); expect(GetShapeStyle(GetStateModeSnapshot()).linePattern == LinePattern::DashDot, "closed shape pattern persists across tools");
		for (int presetIndex = 0; presetIndex < 3; ++presetIndex)
		{
			const auto before = GetStateModeVersionedSnapshot();
			expect(ApplyBarThicknessPresetIfRevision(presetIndex, before.revision, false), "existing preset adapter accepts current shape"); tick(60);
			for (int i = 0; i < 3; ++i)
			{
				const auto& button = BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, GeometryThicknessBegin + i);
				expect(!button.name.enable.val && !button.icon.enable.val && button.name.content.GetVal().empty(), "three thickness buttons contain no numeric label or extra SVG");
				expect((button.state->state == BarWidgetState::Selected) == (i == presetIndex), "three existing presets preserve selected feedback");
			}
		}
		click(GeometryCloseButton); expect(!owner.barState.geometryAttribute && !owner.barState.drawAttribute, "only side-rail button closes panel without opening another view");
		owner.barState.geometryAttribute = true; tick(90);

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
				for (int i = 0; i < GeometryCategoryCount; ++i)
				{
					const auto& b = BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, i);
					expect(owner.spec.MeasureText(b.name.content.GetVal(), b.name.size.val, DWRITE_FONT_WEIGHT_BOLD).width <= b.name.w.val + 0.5, "localized category label fits shared standard slot");
				}
				owner.spec.PrepareFrameLighting(1.0 / 60, {}, static_cast<int>(StateModeSelectEnum::IdtShape), 0, GetPenColor(GetStateModeSnapshot()), 0);
				auto* dc = owner.spec.GetDeviceContext(); dc->BeginDraw(); dc->SetTransform(D2D1::IdentityMatrix()); dc->Clear(D2D1::ColorF(0, 0, 0, 0));
				owner.geometryAttribute.Draw(owner.spec, dc); const HRESULT hr = dc->EndDraw(); expect(SUCCEEDED(hr), "production panel draw completes");
				const auto pixels = ReadPixels(dc, owner.spec.GetTargetBitmap());
				expect(!pixels.empty() && std::any_of(pixels.begin(), pixels.end(), [](auto v) { return v != 0; }), "production panel produces actual pixels");
				for (int i = 0; i < GeometryCategoryCount; ++i) expect(BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, i).icon.cacheBitmap != nullptr, "every category rasterizes embedded SVG");
				if (scale == 1 && !dark && !below && language == std::wstring(L"zh-CN"))
				{
					for (int item = GeometryPatternBegin; item < GeometryCloseButton; ++item)
					{
						const auto rect = f.items[item]; const auto color = static_cast<COLORREF>(BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, item).name.color.val);
						unsigned ink = 0; double covariance = 0;
						// 只采按钮内侧的高alpha内容，排除透明背景/边框光；斜线应从左下通往右上。
						for (int y = static_cast<int>(ceil(rect.top + 5)); y < rect.bottom - 5; ++y)
							for (int x = static_cast<int>(ceil(rect.left + 5)); x < rect.right - 5; ++x)
							{
								const auto offset = (static_cast<size_t>(y) * work.right + x) * 4;
								if (offset + 3 >= pixels.size()) continue;
								const int difference = std::abs(static_cast<int>(pixels[offset]) - GetBValue(color))
									+ std::abs(static_cast<int>(pixels[offset + 1]) - GetGValue(color)) + std::abs(static_cast<int>(pixels[offset + 2]) - GetRValue(color));
								if (pixels[offset + 3] > 235 && difference < 280)
								{
									++ink; covariance += (x + 0.5 - EraserRectCenterX(rect)) * (y + 0.5 - EraserRectCenterY(rect));
								}
							}
						expect(ink > 1 && covariance < 0, "line patterns and thickness previews produce rising diagonal pixels");
					}
				}
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
		owner.geometryAttribute.Close(owner); tick(1, 0);
		for (int hidden = 0; hidden < 20; ++hidden)
		{
			owner.geometryAttribute.Advance(owner, GetStateModeSnapshot(), 1.0 / 60, 1, zoom, work, {}, 0, 0);
			expect(!owner.geometryAttribute.Changed() && !owner.geometryAttribute.Active() && !BarGeometryAttributeTestAccess::CarouselArmed(owner.geometryAttribute), "hidden stable panel has no animation or carousel wake");
		}
		owner.spec.DiscardDeviceResources();

		// 走完整生产协调器，检查新列表及原子树的实际绘制/成功呈现命中。
		owner.barWindow.x = owner.barWindow.y = 0; owner.barWindow.w = 1200; owner.barWindow.h = 800;
		(void)owner.InitializeStartupPlacement(Inkeys::UI::StartupPlacement::BottomLeftFloating);
		InitializeUI(owner); BarGeometryAttributeTestAccess::Display(owner, 1200, 800);
		owner.barStyle.dpiZoom = 1; owner.barStyle.configZoom = 1; owner.barStyle.zoom = 1;
		owner.barButtonSet.Load(); owner.barButtonSet.StateUpdate();
		owner.barState.fold = false; owner.barState.geometryAttribute = true;
		owner.barState.drawAttribute = false; BarUiAnimationEnabled = true;
		unsigned actualCommits = 0;
		struct Receipt { POINT source{}, destination{}; SIZE size{}; RECT dirty{}; bool full = true; } receipt, previousReceipt;
		bool rejectNextPresent = false, rejectedPresent = false, captureScene = false;
		auto scene = CreateBarOffscreenRenderCallback(owner, [&](const UPDATELAYEREDWINDOWINFO& r)
		{
			if (!r.hdcSrc || !r.psize || !r.pptSrc || !r.pptDst || r.psize->cx <= 0 || r.psize->cy <= 0) return FALSE;
			if (rejectNextPresent) { rejectNextPresent = false; rejectedPresent = true; SetLastError(ERROR_GEN_FAILURE); return FALSE; }
			receipt = {*r.pptSrc, *r.pptDst, *r.psize, r.prcDirty ? *r.prcDirty : RECT{}, r.prcDirty == nullptr};
			++actualCommits; return TRUE;
		});
		unsigned capturedCommit = 0;
		bool savedInkDiagnostic = false, savedAntialiasEvidence = false;
		unsigned antialiasAccepted = 0;
		std::vector<unsigned char> scenePixels, previousPixels;
		auto capturePresented = [&]
		{
			if (!captureScene || capturedCommit == actualCommits) return;
			const auto size = owner.spec.GetTargetBitmap()->GetPixelSize();
			const bool cropValid = receipt.source.x >= 0 && receipt.source.y >= 0 &&
				static_cast<unsigned>(receipt.source.x + receipt.size.cx) <= size.width &&
				static_cast<unsigned>(receipt.source.y + receipt.size.cy) <= size.height;
			expect(cropValid, "actual ULW source crop stays within production bitmap");
			if (!cropValid) return;
			const D2D1_RECT_U crop{static_cast<UINT32>(receipt.source.x), static_cast<UINT32>(receipt.source.y),
				static_cast<UINT32>(receipt.source.x + receipt.size.cx), static_cast<UINT32>(receipt.source.y + receipt.size.cy)};
			scenePixels = ReadPixels(owner.spec.GetDeviceContext(), owner.spec.GetTargetBitmap(), &crop);
			expect(scenePixels.size() == static_cast<size_t>(receipt.size.cx) * receipt.size.cy * 4, "actual ULW pixels read back after successful scene transaction");
			if (!receipt.full)
			{
				expect(receipt.dirty.left >= 0 && receipt.dirty.top >= 0 && receipt.dirty.right <= receipt.size.cx &&
					receipt.dirty.bottom <= receipt.size.cy && receipt.dirty.right > receipt.dirty.left && receipt.dirty.bottom > receipt.dirty.top,
					"partial ULW dirty rectangle is contained by its actual crop");
				if (!previousPixels.empty())
				{
					const bool sameMapping = receipt.size.cx == previousReceipt.size.cx && receipt.size.cy == previousReceipt.size.cy &&
						receipt.source.x == previousReceipt.source.x && receipt.source.y == previousReceipt.source.y;
					expect(sameMapping, "source or viewport size changes require complete window replacement");
					bool covered = sameMapping && scenePixels.size() == previousPixels.size();
					if (covered) for (LONG y = 0; y < receipt.size.cy && covered; ++y)
						for (LONG x = 0; x < receipt.size.cx; ++x)
							if (x < receipt.dirty.left || x >= receipt.dirty.right || y < receipt.dirty.top || y >= receipt.dirty.bottom)
							{
								const size_t offset = (static_cast<size_t>(y) * receipt.size.cx + x) * 4;
								if (std::memcmp(scenePixels.data() + offset, previousPixels.data() + offset, 4) != 0) { covered = false; break; }
							}
					expect(covered, "all changed actual pixels are covered by committed ULW dirty bounds");
				}
			}
			previousPixels = scenePixels; previousReceipt = receipt; capturedCommit = actualCommits;
		};
		auto sceneTime = std::chrono::steady_clock::now();
		auto sceneResult = RenderPipeline::FrameResult::Idle;
		auto sceneTick = [&](int count = 90, bool request = true)
		{
			if (request) owner.UpdateRendering(false);
			for (int i = 0; i < count; ++i)
			{
				sceneTime += std::chrono::milliseconds(16);
				sceneResult = scene(RenderPipeline::FrameContext{RenderPipeline::GetDeviceEpoch(), RenderPipeline::GetSharedAssets(), sceneTime});
				expect(sceneResult != RenderPipeline::FrameResult::DeviceLost, "actual scene keeps its production device");
				capturePresented();
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
		auto saveScene = [&](const wchar_t* filename)
		{
			const auto size = owner.spec.GetTargetBitmap()->GetPixelSize();
			const auto bytes = ReadPixels(owner.spec.GetDeviceContext(), owner.spec.GetTargetBitmap());
			expect(SavePng(std::filesystem::path(L"Build/shape-p2-ui") / filename, bytes, size.width, size.height), "actual scene pixel evidence");
		};
		// 从实际ULW窗口截面读像素，不把独立面板贴到另一张“看起来相同”的纹理上。
		auto clientRect = [&](EraserAttributeRect rect, const GeometryAttributePresentation& presented)
		{
			const auto mapping = owner.BottomDockPresentedSnapshot();
			auto x = [&](double v) { return v * presented.zoom + mapping.horizontalMapping.rigidOverlayTranslationXDip * mapping.zoom
				+ mapping.monitorOrigin.x + mapping.directTranslation.x - receipt.destination.x; };
			auto y = [&](double v) { return v * presented.zoom + mapping.rigidTranslationDip * mapping.zoom
				+ mapping.monitorOrigin.y + mapping.directTranslation.y - receipt.destination.y; };
			return EraserAttributeRect{x(rect.left), y(rect.top), x(rect.right), y(rect.bottom)};
		};
		auto pixel = [&](double x, double y)
		{
			const LONG px = static_cast<LONG>(std::floor(x)), py = static_cast<LONG>(std::floor(y));
			if (px < 0 || py < 0 || px >= receipt.size.cx || py >= receipt.size.cy) return std::array<unsigned char,4>{};
			const size_t offset = (static_cast<size_t>(py) * receipt.size.cx + px) * 4;
			if (offset + 4 > scenePixels.size()) return std::array<unsigned char,4>{};
			return std::array<unsigned char,4>{scenePixels[offset], scenePixels[offset + 1], scenePixels[offset + 2], scenePixels[offset + 3]};
		};
		struct DisabledInkEvidence { unsigned covered = 0; double peak = 0; bool Visible() const { return covered >= 3 && peak > 0.12; } };
		auto disabledInk = [&](EraserAttributeRect icon, EraserAttributeRect cell, COLORREF foreground)
		{
			DisabledInkEvidence evidence;
			auto straight = [&](double x, double y)
			{
				const auto value = pixel(x, y); std::array<double,3> color{};
				for (int c = 0; c < 3; ++c) color[c] = value[3] ? (std::min)(255.0, value[c] * 255.0 / value[3]) : 0;
				return color;
			};
			// 内容透明度0.30由公共按钮提供；以实际空白边带为背景，验证真实ULW中仍存在SVG轮廓。
			const auto a = straight(cell.left + cell.Width() * 0.05, EraserRectCenterY(icon));
			const auto b = straight(cell.right - cell.Width() * 0.05, EraserRectCenterY(icon));
			const std::array<double,3> ink{static_cast<double>(GetBValue(foreground)), static_cast<double>(GetGValue(foreground)), static_cast<double>(GetRValue(foreground))};
			std::array<double,3> background{}, direction{}; double norm = 0;
			for (int c = 0; c < 3; ++c) { background[c] = (a[c] + b[c]) / 2; direction[c] = background[c] - ink[c]; norm += direction[c] * direction[c]; }
			if (norm < 100) return evidence;
			for (LONG y = static_cast<LONG>(ceil(icon.top)); y < icon.bottom; ++y)
				for (LONG x = static_cast<LONG>(ceil(icon.left)); x < icon.right; ++x)
				{
					const auto sample = straight(x, y); double projected = 0, residual = 0;
					for (int c = 0; c < 3; ++c) projected += (background[c] - sample[c]) * direction[c];
					const double coverage = (std::clamp)(projected / norm, 0.0, 1.0);
					for (int c = 0; c < 3; ++c) { const auto difference = sample[c] - background[c] + coverage * direction[c]; residual += difference * difference; }
					if (pixel(x, y)[3] > 180 && residual < 30 * 30) { evidence.peak = (std::max)(evidence.peak, coverage); if (coverage > 0.08) ++evidence.covered; }
				}
			return evidence;
		};
		std::array<unsigned,GeometryCategoryCount> futurePixelCases{};
		auto checkFuturePixels = [&](const GeometryAttributePresentation& presented)
		{
			for (int i = 0; i < GeometryCategoryCount; ++i)
			{
				if (catalog[i].preset) continue;
				const auto& button = BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, i);
				const EraserAttributeRect image{button.icon.inhX, button.icon.inhY, button.icon.inhX + button.icon.w.val, button.icon.inhY + button.icon.h.val};
				const auto clipped = Intersect(image, presented.categoryClip);
				if (clipped.Width() < image.Width() - 0.01 || clipped.Height() < image.Height() - 0.01) continue;
				expect(button.icon.cacheBitmap != nullptr && !button.icon.svg.GetVal().empty() && FindResourceW(nullptr, ShapeCategoryCatalog()[i].resource, L"UI"),
					"visible future category rasterizes its own formally embedded SVG");
				const auto icon = clientRect(image, presented), cell = clientRect(presented.items[i], presented);
				const COLORREF color = button.icon.color1->val;
				const auto evidence = disabledInk(icon, cell, color);
				const auto row = presented.items[GeometryStyleBegin].Width() > 0 ? presented.items[GeometryStyleBegin] : presented.items[GeometryPatternBegin];
				const double gap = 5 * presented.pose.scale;
				const EraserAttributeRect blank{presented.panel.right - gap - image.Width(), row.top + (row.Height() - image.Height()) / 2,
					presented.panel.right - gap, row.top + (row.Height() + image.Height()) / 2};
				const auto negative = clientRect(blank, presented); const auto empty = disabledInk(negative, negative, color);
				expect(evidence.Visible(), "actual emitted disabled SVG has visible 0.30 content over the local background");
				expect(!empty.Visible(), "identical disabled SVG pixel oracle rejects same-size real blank footer crop");
				if (!evidence.Visible() || empty.Visible()) report << "DisabledInk item=" << i << " peak=" << evidence.peak << " covered=" << evidence.covered << " blankPeak=" << empty.peak << " [" << caseContext << "]\n";
				++futurePixelCases[i];
			}
		};
		struct DiagonalInkEvidence
		{
			double peak = 0, diagonalMean = 0, reverseMean = 0;
			unsigned covered = 0;
			bool Visible() const { return covered >= 2 && peak > 0.15 && diagonalMean > reverseMean + 0.025; }
		};
		auto diagonalInk = [&](EraserAttributeRect rect, COLORREF foreground)
		{
			DiagonalInkEvidence evidence;
			if (rect.Width() <= 0 || rect.Height() <= 0) return evidence;
			auto straight = [&](double x, double y)
			{
				const auto value = pixel(x, y); std::array<double,3> color{};
				for (int channel = 0; channel < 3; ++channel)
					color[channel] = value[3] ? (std::min)(255.0, value[channel] * 255.0 / value[3]) : 0;
				return color;
			};
			// 反向对角两角避开斜线，取真实局部背景；先解除预乘，AA灰点不能按接近纯前景色判断。
			const auto topLeft = straight(rect.left + rect.Width() * 0.2, rect.top + rect.Height() * 0.2);
			const auto bottomRight = straight(rect.right - rect.Width() * 0.2, rect.bottom - rect.Height() * 0.2);
			const std::array<double,3> inkColor{static_cast<double>(GetBValue(foreground)), static_cast<double>(GetGValue(foreground)), static_cast<double>(GetRValue(foreground))};
			std::array<double,3> background{}, direction{}; double norm = 0;
			for (int channel = 0; channel < 3; ++channel)
			{
				background[channel] = (topLeft[channel] + bottomRight[channel]) / 2;
				direction[channel] = background[channel] - inkColor[channel]; norm += direction[channel] * direction[channel];
			}
			if (norm < 100) return evidence;
			const double side = (std::min)(rect.Width(), rect.Height()), inset = (std::max)(1.0, side * 0.125);
			double diagonalSum = 0, reverseSum = 0; unsigned diagonalCount = 0, reverseCount = 0;
			for (LONG y = static_cast<LONG>(ceil(rect.top + inset)); y < rect.bottom - inset; ++y)
				for (LONG x = static_cast<LONG>(ceil(rect.left + inset)); x < rect.right - inset; ++x)
				{
					const double dx = x + 0.5 - EraserRectCenterX(rect), dy = y + 0.5 - EraserRectCenterY(rect);
					if (std::hypot(dx, dy) < side * 0.10) continue;
					const auto sample = straight(x, y); double projection = 0;
					for (int channel = 0; channel < 3; ++channel) projection += (background[channel] - sample[channel]) * direction[channel];
					double coverage = (std::clamp)(projection / norm, 0.0, 1.0), residual = 0;
					for (int channel = 0; channel < 3; ++channel)
					{
						const double difference = sample[channel] - (background[channel] - coverage * direction[channel]); residual += difference * difference;
					}
					if (residual > 40 * 40 || pixel(x, y)[3] <= 180) coverage = 0;
					if (std::abs(dx + dy) < side * 0.18 && dx * dy < 0)
					{
						diagonalSum += coverage; ++diagonalCount; evidence.peak = (std::max)(evidence.peak, coverage);
						if (coverage > 0.10) ++evidence.covered;
					}
					if (std::abs(dx - dy) < side * 0.18 && dx * dy > 0) { reverseSum += coverage; ++reverseCount; }
				}
			evidence.diagonalMean = diagonalCount ? diagonalSum / diagonalCount : 0;
			evidence.reverseMean = reverseCount ? reverseSum / reverseCount : 0;
			return evidence;
		};
		auto rowGeometry = [&](const GeometryAttributePresentation& presented)
		{
			bool any = false; double bottom = 0;
			for (int i = GeometryStyleBegin; i < GeometryCloseButton; ++i) if (presented.items[i].Width() > 0)
			{
				const auto rect = presented.items[i]; const auto& button = BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, i);
				expect(rect.left >= presented.panel.left - 0.01 && rect.right <= presented.panel.right + 0.01 &&
					rect.top >= presented.panel.top - 0.01 && rect.bottom <= presented.panel.bottom + 0.01, "all current attribute buttons stay inside actual panel");
				expect(std::abs(button.button.inhX - rect.left) < 0.01 && std::abs(button.button.inhY - rect.top) < 0.01 &&
					std::abs(button.button.w.val - rect.Width()) < 0.01 && std::abs(button.button.h.val - rect.Height()) < 0.01,
					"published attribute rectangle matches production button draw geometry");
				const auto crop = clientRect(rect, presented);
				expect(crop.left >= -1 && crop.top >= -1 && crop.right <= receipt.size.cx + 1 && crop.bottom <= receipt.size.cy + 1,
					"entire attribute button fits successfully emitted viewport crop");
				any = true; bottom = (std::max)(bottom, rect.bottom);
			}
			expect(any && std::abs((presented.panel.bottom - bottom) - 5 * presented.pose.scale) < 0.01,
				"actual bottom row keeps five DIP padding throughout panel height and fit animation");
		};
		auto checkStableScene = [&](PresetKind wanted, draw3::shapes::FillMode fill)
		{
			const auto presented = owner.geometryAttribute.PresentationSnapshot();
			expect(presented.visible && presented.acceptsInput && !owner.geometryAttribute.Active(), "complete production scene settles and enables input");
			const auto mapping = owner.BottomDockPresentedSnapshot();
			const auto panel = clientRect(presented.panel, presented);
			expect(panel.left >= -1 && panel.top >= -1 && panel.right <= receipt.size.cx + 1 && panel.bottom <= receipt.size.cy + 1,
				"complete production panel lies within its committed ULW crop");
			expect(presented.panel.Width() * presented.zoom <= mapping.workArea.right - mapping.workArea.left + 1 &&
				presented.panel.Height() * presented.zoom <= mapping.workArea.bottom - mapping.workArea.top + 1,
				"runtime fit contains complete panel in actual work area");
			const bool closed = GetToolCapabilities(wanted).fill;
			const bool stroke = !closed || fill != draw3::shapes::FillMode::Fill;
			expect(presented.categoryViewportHeight > 0 && presented.categoryContentHeight >= presented.categoryViewportHeight - 0.01 &&
				presented.scrollOffset >= -0.01 && presented.scrollOffset <= presented.scrollMaximum + 0.01, "category viewport and offset obey actual production scroll limits");
			expect(presented.categoryClip.left >= presented.panel.left && presented.categoryClip.top >= presented.panel.top &&
				presented.categoryClip.right <= presented.panel.right && presented.categoryClip.bottom <= presented.panel.bottom, "category clip remains inside panel while fixed controls stay outside its scroll content");
			for (int i = 0; i < GeometryControlCount; ++i)
			{
				const bool shown = i < GeometryCategoryCount || i == GeometryCloseButton || ((i == GeometryCircleCenter || i == GeometryCircleBox) && wanted == PresetKind::Circle) ||
					(i >= GeometrySidesBegin && i < GeometryStyleBegin && GetToolCapabilities(wanted).regularSides) || (i >= GeometryStyleBegin && i < GeometryPatternBegin && closed) || (i >= GeometryPatternBegin && i < GeometryCloseButton && stroke);
				const auto rect = presented.items[i]; const auto& hit = presented.hits[i];
				const auto visibleRect = i < GeometryCategoryCount ? Intersect(rect, presented.categoryClip) : rect;
				const bool actuallyVisible = shown && visibleRect.Width() > 0;
				expect((rect.Width() > 0) == shown && hit.visible == actuallyVisible, "visibility separates complete category geometry from clipped presented hits");
				if (!shown) continue;
				if (actuallyVisible)
				{
					expect(visibleRect.left >= presented.panel.left - 0.01 && visibleRect.top >= presented.panel.top - 0.01 &&
						visibleRect.right <= presented.panel.right + 0.01 && visibleRect.bottom <= presented.panel.bottom + 0.01,
						"visible category clips parameters and attributes fit actual panel bounds");
					const auto emitted = clientRect(visibleRect, presented);
					expect(emitted.left >= -1 && emitted.top >= -1 && emitted.right <= receipt.size.cx + 1 && emitted.bottom <= receipt.size.cy + 1,
						"all visible shape control pixels survive complete production crop");
					if (i >= GeometryCircleCenter && i < GeometryStyleBegin)
						expect(rect.top >= presented.parameterClip.top - 0.01 && rect.bottom <= presented.parameterClip.bottom + 0.01,
							"stable parameter buttons fit the exact clip used by production draw");
					const bool actionable = i >= GeometryCategoryCount || catalog[i].preset.has_value();
					expect(hit.enabled == actionable && HitTestBarButton(hit, EraserRectCenterX(visibleRect), EraserRectCenterY(visibleRect)) == actionable,
						"successful presentation enables current visible implemented controls and rejects future categories");
					expect(std::abs(hit.boundsDip.left - visibleRect.left) < 0.02 && std::abs(hit.boundsDip.top - visibleRect.top) < 0.02 &&
						std::abs(hit.boundsDip.right - visibleRect.right) < 0.02 && std::abs(hit.boundsDip.bottom - visibleRect.bottom) < 0.02,
						"presented hit uses the same clipped geometry as actual draw");
				}
				else expect(!hit.enabled, "fully clipped categories cannot receive actions");
				if (i < GeometryCategoryCount)
				{
					const auto& button = BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, i);
					expect(owner.spec.MeasureText(button.name.content.GetVal(), button.name.size.val, DWRITE_FONT_WEIGHT_BOLD).width <= button.name.w.val + 0.5,
						"actual scene localized category label fits measured shared slot");
					if (!catalog[i].preset)
						expect(button.state->state == BarWidgetState::Disable && std::abs(button.icon.pct.val - 0.30) < 0.000001 && std::abs(button.name.pct.val - 0.30) < 0.000001,
							"disabled future category retains shared 0.30 content without becoming selected");
				}
				if (i >= GeometryPatternBegin && i < GeometryCloseButton)
				{
					const auto target = clientRect(rect, presented); unsigned ink = 0;
					int nearestDistance = 1000, maxAlpha = 0; std::array<unsigned char,4> nearest{};
					const COLORREF color = BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, i).name.color.val;
					const double inset = (std::max)(1.0, 4 * presented.pose.scale * presented.zoom);
					for (LONG y = static_cast<LONG>(ceil(target.top + inset)); y < target.bottom - inset; ++y)
						for (LONG x = static_cast<LONG>(ceil(target.left + inset)); x < target.right - inset; ++x)
						{
							const auto value = pixel(x, y);
							const int distance = std::abs(static_cast<int>(value[0]) - GetBValue(color)) +
								std::abs(static_cast<int>(value[1]) - GetGValue(color)) + std::abs(static_cast<int>(value[2]) - GetRValue(color));
							maxAlpha = (std::max)(maxAlpha, static_cast<int>(value[3]));
							if (distance < nearestDistance) { nearestDistance = distance; nearest = value; }
							if (value[3] > 180 && distance < 180) ++ink;
						}
					// 原有实色像素判据保留；低DPI点线允许有方向性且显著高于背景的真实AA覆盖。
					const auto antialias = ink == 0 ? diagonalInk(target, color) : DiagonalInkEvidence{};
					const bool actualInk = ink > 0 || antialias.Visible();
					if (ink == 0 && antialias.Visible())
					{
						++antialiasAccepted;
						const double gap = 5 * presented.pose.scale;
						const EraserAttributeRect blank{presented.panel.right - gap - rect.Width(), rect.top, presented.panel.right - gap, rect.bottom};
						double lastAttributeRight = 0;
						for (int item = GeometryStyleBegin; item < GeometryCloseButton; ++item) if (presented.items[item].Width() > 0)
							lastAttributeRight = (std::max)(lastAttributeRight, presented.items[item].right);
						expect(blank.left > lastAttributeRight + gap, "AA negative control lies in actual blank panel row beyond all attribute buttons");
						const auto empty = diagonalInk(clientRect(blank, presented), color);
						expect(!empty.Visible(), "identical AA oracle rejects real blank panel pixels");
						if (!savedAntialiasEvidence)
						{
							report << "AAInkEvidence item=" << i << " covered=" << antialias.covered << " peak=" << antialias.peak
								<< " diagonal=" << antialias.diagonalMean << " reverse=" << antialias.reverseMean << " blankPeak=" << empty.peak << '\n';
							expect(SavePng(L"Build/shape-p2-ui/scene-ink-antialias-evidence.png", scenePixels, receipt.size.cx, receipt.size.cy), "AA proof saves actual ULW crop");
							savedAntialiasEvidence = true;
						}
					}
					if (!actualInk)
					{
						report << "InkDiagnostic item=" << i << " minColorDistance=" << nearestDistance << " maxAlpha=" << maxAlpha
							<< " expectedRGB=" << static_cast<unsigned>(GetRValue(color)) << ',' << static_cast<unsigned>(GetGValue(color)) << ',' << static_cast<unsigned>(GetBValue(color))
							<< " nearestBGRA=" << static_cast<unsigned>(nearest[0]) << ',' << static_cast<unsigned>(nearest[1]) << ',' << static_cast<unsigned>(nearest[2]) << ',' << static_cast<unsigned>(nearest[3])
							<< " buttonClient=" << target.left << ',' << target.top << ',' << target.right << ',' << target.bottom << " [" << caseContext << "]\n";
						if (!savedInkDiagnostic)
						{
							expect(SavePng(L"Build/shape-p2-ui/scene-ink-diagnostic.png", scenePixels, receipt.size.cx, receipt.size.cy), "failed ink diagnostic saves actual ULW crop");
							savedInkDiagnostic = true;
						}
					}
					expect(actualInk, "actual emitted bottom-row crop contains line-pattern or thickness ink");
				}
			}
			rowGeometry(presented); checkFuturePixels(presented);
		};
		captureScene = true; BarUiAnimationEnabled = false; owner.geometryAttribute.ResetPointerFeedback();
		unsigned sceneCases = 0;
		for (const wchar_t* language : {L"zh-CN", L"zh-TW", L"en-US"})
		{
			expect(I18n::load(1, L"JSON", language), "production matrix language loads"); owner.spec.ConfigureLocalizedTypography();
			for (double scale : {0.75, 1.0, 1.5, 2.0}) for (bool smallWork : {false, true}) for (bool below : {false, true})
			{
				const LONG width = smallWork ? 460 : 1200, height = smallWork ? 420 : 800;
				owner.barStyle.darkStyle = smallWork; owner.barStyle.configZoom = 1;
				BarGeometryAttributeTestAccess::Display(owner, static_cast<LONG>(width * scale), static_cast<LONG>(height * scale), static_cast<UINT>(96 * scale));
				sceneTick(2);
				BarGeometryAttributeTestAccess::MoveRoot(owner, 80, below ? 80 : height - 80); sceneTick(2);
				expect(static_cast<bool>(owner.barState.widgetPosition.primaryBar) == below, "actual main-bar position chooses intended expansion side");
				for (int shape = 0; shape < GeometryImplementedCategoryCount; ++shape) for (int fill = 0; fill < 3; ++fill)
				{
					caseContext = "scene=" + std::to_string(++sceneCases) + " shape=" + std::to_string(shape) + " fill=" + std::to_string(fill) +
						" scale=" + std::to_string(scale) + " small=" + std::to_string(smallWork) + " below=" + std::to_string(below);
					const auto tool = GetStateModeVersionedSnapshot(); SetShapeToolPresetIfRevision(static_cast<PresetKind>(shape), tool.revision);
					const auto style = GetStateModeVersionedSnapshot(); SetShapeFillModeIfRevision(static_cast<draw3::shapes::FillMode>(fill), style.revision);
					owner.barState.fold = false; owner.barState.geometryAttribute = true; owner.UpdateRendering(); sceneTick(2);
					checkStableScene(static_cast<PresetKind>(shape), static_cast<draw3::shapes::FillMode>(fill));
					if (language == std::wstring(L"zh-CN") && scale == 1 && shape == static_cast<int>(PresetKind::Circle) && !below && fill == 2)
						expect(SavePng(smallWork ? L"Build/shape-p2-ui/scene-shape-small.png" : L"Build/shape-p2-ui/scene-shape-complete.png",
							scenePixels, receipt.size.cx, receipt.size.cy), "actual cropped shape scene writes pixel evidence");
				}
			}
		}
		for (int i = 0; i < GeometryCategoryCount; ++i) if (!catalog[i].preset) expect(futurePixelCases[i] > 0, "all fifteen future resources have real emitted pixel evidence");
		report << "Complete production scene configurations=" << sceneCases << " AA pixel cases=" << antialiasAccepted << '\n'; caseContext = "animated attributes";
		const auto attributes = BarGeometryAttributeTestAccess::Attributes(owner.geometryAttribute);
		expect(attributes.size() == 12 && attributes[3]->preset == BarButtonPresetEnum::Divider && attributes[8]->preset == BarButtonPresetEnum::Divider,
			"attributes own a shared single-row list with independent group dividers");
		BarGeometryAttributeTestAccess::Display(owner, 1200, 800); sceneTick(2);
		BarGeometryAttributeTestAccess::MoveRoot(owner, 80, 650); sceneTick(2);
		owner.barStyle.darkStyle = false;
		auto selectScene = [&](PresetKind p, draw3::shapes::FillMode fill)
		{
			const auto tool = GetStateModeVersionedSnapshot(); SetShapeToolPresetIfRevision(p, tool.revision);
			const auto style = GetStateModeVersionedSnapshot(); SetShapeFillModeIfRevision(fill, style.revision);
			owner.barState.fold = false; owner.barState.geometryAttribute = true; owner.UpdateRendering();
		};
		// A方案通过生产呈现/输入回放：只滚动类别，参数、属性和关闭入口保持固定。
		caseContext = "category viewport scrolling";
		selectScene(PresetKind::Circle, draw3::shapes::FillMode::Outline); sceneTick(2);
		const auto spacious = owner.geometryAttribute.PresentationSnapshot();
		expect(spacious.scrollMaximum == 0 && std::abs(spacious.categoryViewportHeight - spacious.categoryContentHeight) < 0.01,
			"normal workspace displays all six catalog rows without a scrollbar");
		BarGeometryAttributeTestAccess::Display(owner, 460, 420); sceneTick(2); BarGeometryAttributeTestAccess::MoveRoot(owner, 80, 340); sceneTick(2);
		auto scrollFrame = owner.geometryAttribute.PresentationSnapshot();
		expect(scrollFrame.scrollMaximum > 0 && scrollFrame.categoryViewportHeight < scrollFrame.categoryContentHeight &&
			scrollFrame.scrollTrack.Width() > 0 && scrollFrame.scrollThumb.Height() < scrollFrame.scrollTrack.Height(), "restricted workspace retains fixed rows and exposes a real category viewport scrollbar");
		const auto smallMain = owner.shapeMap[BarUISetShapeEnum::MainBar];
		const EraserAttributeRect mainBounds{smallMain->inhX, smallMain->inhY, smallMain->inhX + smallMain->w.val, smallMain->inhY + smallMain->h.val};
		expect(Intersect(scrollFrame.panel, mainBounds).Width() == 0, "restricted panel chooses an expansion region that keeps the main toolbar reachable");
		expect(scrollFrame.pose.scale > 0.9, "ordinary restricted workspace preserves useful button size by scrolling instead of shrinking all six rows");
		auto panelPoint = [&](USHORT type, double x, double y, bool held = false, bool contact = false, short wheel = 0)
		{
			const auto current = owner.geometryAttribute.PresentationSnapshot(); ExMessage message{};
			message.message = type; message.lbutton = held; message.x = static_cast<short>(std::lround(x * current.zoom)); message.y = static_cast<short>(std::lround(y * current.zoom)); message.wheel = wheel;
			return owner.geometryAttribute.Pointer(owner, message, false, contact);
		};
		auto wheel = [&](short amount)
		{
			const auto current = owner.geometryAttribute.PresentationSnapshot();
			return panelPoint(WM_MOUSEWHEEL, EraserRectCenterX(current.categoryClip), EraserRectCenterY(current.categoryClip), false, false, amount);
		};
		expect(SavePng(L"Build/shape-p2-ui/panel-scroll-top.png", scenePixels, receipt.size.cx, receipt.size.cy), "restricted viewport top writes actual emitted crop");
		const auto fixed = scrollFrame; const auto beforeScroll = GetStateModeVersionedSnapshot();
		BarUiAnimationEnabled = true; expect(wheel(-WHEEL_DELTA), "category wheel is consumed by its own viewport");
		const double firstRequest = BarGeometryAttributeTestAccess::ScrollRequest(owner.geometryAttribute);
		expect(firstRequest > 0, "first wheel publishes a positive scroll intent without a UI thread geometry write");
		expect(wheel(-WHEEL_DELTA) && BarGeometryAttributeTestAccess::ScrollRequest(owner.geometryAttribute) >= firstRequest, "consecutive wheel input accumulates unpresented intent");
		unsigned intermediate = 0;
		for (int i = 0; i < 8; ++i)
		{
			sceneTick(1); const auto current = owner.geometryAttribute.PresentationSnapshot(); rowGeometry(current);
			if (current.scrollOffset > 0 && current.scrollOffset < BarGeometryAttributeTestAccess::ScrollRequest(owner.geometryAttribute) - 0.01) ++intermediate;
			expect(current.items[GeometryCloseButton] == fixed.items[GeometryCloseButton] && current.items[GeometryPatternBegin] == fixed.items[GeometryPatternBegin] && current.items[GeometryCircleCenter] == fixed.items[GeometryCircleCenter],
				"category scrolling leaves close parameter and attribute rows fixed throughout shared animation");
			if (!current.categoriesAcceptInput)
			{
				for (int item = 0; item < GeometryCategoryCount; ++item) expect(!current.hits[item].enabled, "scroll animation disables only category action hits");
				BarGeometryAttributeTestAccess::Focus(owner.geometryAttribute, 0); owner.geometryAttribute.Keyboard(owner, VK_RETURN, true);
				expect(SameBusinessSnapshot(beforeScroll, GetStateModeVersionedSnapshot()), "Enter cannot execute an unpresented category during animated scroll");
			}
		}
		expect(intermediate >= 2, "scroll offsets pass through multiple shared animation samples"); sceneTick(90);
		BarUiAnimationEnabled = false;
		for (int i = 0; i < 20; ++i) wheel(-WHEEL_DELTA); sceneTick(2);
		scrollFrame = owner.geometryAttribute.PresentationSnapshot();
		expect(std::abs(scrollFrame.scrollOffset - scrollFrame.scrollMaximum) < 0.01 && scrollFrame.categoriesAcceptInput, "wheel clamps at the last row and restores category input after settle");
		checkStableScene(PresetKind::Circle, draw3::shapes::FillMode::Outline);
		expect(SavePng(L"Build/shape-p2-ui/panel-scroll-bottom.png", scenePixels, receipt.size.cx, receipt.size.cy), "restricted viewport bottom writes actual emitted crop");
		for (int i = 0; i < GeometryCategoryCount; ++i)
		{
			if (catalog[i].preset) continue;
			const auto current = owner.geometryAttribute.PresentationSnapshot(); const auto visible = Intersect(current.items[i], current.categoryClip);
			const auto before = GetStateModeVersionedSnapshot();
			if (visible.Width() > 0)
			{
				expect(panelPoint(WM_LBUTTONDOWN, EraserRectCenterX(visible), EraserRectCenterY(visible), true, true), "visible disabled scrolled category blocks contact from reaching canvas");
				panelPoint(WM_LBUTTONUP, EraserRectCenterX(visible), EraserRectCenterY(visible), false, true);
			}
			BarGeometryAttributeTestAccess::Execute(owner.geometryAttribute, owner, i, before.revision);
			expect(SameBusinessSnapshot(before, GetStateModeVersionedSnapshot()), "future tools remain inert at scrolled presentation and direct action entry");
		}
		// 滚到底时Tab跨过所有未来项，使首个真实工具进入视口，呈现前不能执行旧坐标。
		BarGeometryAttributeTestAccess::Focus(owner.geometryAttribute, GeometryCloseButton);
		const auto keyboardBefore = GetStateModeVersionedSnapshot();
		owner.geometryAttribute.Keyboard(owner, VK_TAB, true); owner.geometryAttribute.Keyboard(owner, VK_RETURN, true);
		expect(BarGeometryAttributeTestAccess::Focused(owner.geometryAttribute) == 0 && SameBusinessSnapshot(keyboardBefore, GetStateModeVersionedSnapshot()), "Tab brings real category into view and Enter waits for a successful visible snapshot");
		sceneTick(2); scrollFrame = owner.geometryAttribute.PresentationSnapshot();
		expect(scrollFrame.scrollOffset == 0 && scrollFrame.hits[0].enabled && scrollFrame.hits[0].visible, "keyboard focus scrolls first implemented tool fully into view");
		for (int i = 0; i < GeometryImplementedCategoryCount - 1; ++i) { owner.geometryAttribute.Keyboard(owner, VK_RIGHT, true); sceneTick(2); }
		scrollFrame = owner.geometryAttribute.PresentationSnapshot();
		const int focused = BarGeometryAttributeTestAccess::Focused(owner.geometryAttribute);
		expect(focused == 23 && scrollFrame.hits[focused].enabled && scrollFrame.hits[focused].visible &&
			scrollFrame.items[focused].top >= scrollFrame.categoryClip.top - 0.01 && scrollFrame.items[focused].bottom <= scrollFrame.categoryClip.bottom + 0.01, "keyboard navigates only real tools and brings second-row focus fully inside viewport");
		// 宽触控轨道通过公共Down/Move/Up回放，同时覆盖取消和跨代/跨工作区失效。
		for (bool contact : {false, true})
		{
			for (int i = 0; i < 20; ++i) wheel(WHEEL_DELTA); sceneTick(2); scrollFrame = owner.geometryAttribute.PresentationSnapshot();
			const auto before = GetStateModeVersionedSnapshot(); const double x = EraserRectCenterX(scrollFrame.scrollTrack), y = EraserRectCenterY(scrollFrame.scrollThumb);
			expect(panelPoint(WM_LBUTTONDOWN,x,y,true,contact) && BarGeometryAttributeTestAccess::ScrollDragging(owner.geometryAttribute), "scrollbar track captures mouse or normalized pen touch contact");
			panelPoint(WM_MOUSEMOVE,x,scrollFrame.scrollTrack.bottom + 60,true,contact); sceneTick(2);
			panelPoint(WM_LBUTTONUP,x,scrollFrame.scrollTrack.bottom + 60,false,contact); sceneTick(2);
			const auto end = owner.geometryAttribute.PresentationSnapshot();
			expect(!BarGeometryAttributeTestAccess::ScrollDragging(owner.geometryAttribute) && std::abs(end.scrollOffset-end.scrollMaximum)<0.01 && SameBusinessSnapshot(before,GetStateModeVersionedSnapshot()), "scrollbar outside release clamps bottom and preserves all drawing business state");
			expect(panelPoint(WM_LBUTTONDOWN,EraserRectCenterX(end.scrollTrack),EraserRectCenterY(end.scrollThumb),true,contact), "bottom thumb accepts another contact");
			ExMessage lost{}; expect(owner.geometryAttribute.Pointer(owner,lost,true,contact), "capture loss cancels active scrollbar gesture");
			const double cancelledRequest = BarGeometryAttributeTestAccess::ScrollRequest(owner.geometryAttribute);
			panelPoint(WM_MOUSEMOVE,x,end.scrollTrack.top-60,true,contact); panelPoint(WM_LBUTTONUP,x,end.scrollTrack.top-60,false,contact); sceneTick(2);
			expect(!BarGeometryAttributeTestAccess::ScrollDragging(owner.geometryAttribute) && BarGeometryAttributeTestAccess::ScrollRequest(owner.geometryAttribute)==cancelledRequest, "late events cannot continue a cancelled scrollbar drag");
		}
		scrollFrame = owner.geometryAttribute.PresentationSnapshot();
		panelPoint(WM_LBUTTONDOWN,EraserRectCenterX(scrollFrame.scrollTrack),EraserRectCenterY(scrollFrame.scrollThumb),true,true);
		{ const auto current=GetStateModeVersionedSnapshot(); SetShapeToolPresetIfRevision(PresetKind::Rectangle,current.revision); }
		panelPoint(WM_MOUSEMOVE,EraserRectCenterX(scrollFrame.scrollTrack),scrollFrame.scrollTrack.top,true,true);
		expect(!BarGeometryAttributeTestAccess::ScrollDragging(owner.geometryAttribute), "business revision invalidates old scrollbar gesture"); sceneTick(2);
		scrollFrame=owner.geometryAttribute.PresentationSnapshot();
		panelPoint(WM_LBUTTONDOWN,EraserRectCenterX(scrollFrame.scrollTrack),EraserRectCenterY(scrollFrame.scrollThumb),true,true);
		BarGeometryAttributeTestAccess::Display(owner,460,380); sceneTick(2);
		panelPoint(WM_MOUSEMOVE,EraserRectCenterX(scrollFrame.scrollTrack),scrollFrame.scrollTrack.top,true,true);
		expect(!BarGeometryAttributeTestAccess::ScrollDragging(owner.geometryAttribute), "workspace and track generation change invalidate old scrollbar geometry");
		// 呈现失败不能把滚动候选的裁剪与命中发布到交互侧。
		const auto previousScroll=owner.geometryAttribute.PresentationSnapshot(); wheel(WHEEL_DELTA); rejectNextPresent=true; sceneTick(1);
		const auto failedScroll=owner.geometryAttribute.PresentationSnapshot();
		expect(failedScroll.items==previousScroll.items && failedScroll.categoryClip==previousScroll.categoryClip && failedScroll.scrollOffset==previousScroll.scrollOffset,
			"failed actual scroll presentation retains previous successful clip coordinates and offset");
		previousPixels.clear(); sceneTick(2);
		BarGeometryAttributeTestAccess::Display(owner,1200,800); sceneTick(2); BarGeometryAttributeTestAccess::MoveRoot(owner,80,650); sceneTick(2);
		caseContext = "animated attributes";
		selectScene(PresetKind::Rectangle, draw3::shapes::FillMode::Outline); sceneTick(2);
		auto previewContrast = [&](int item, const GeometryAttributePresentation& current)
		{
			if (current.items[item].Width() <= 0) return 0.0;
			const auto rect = clientRect(current.items[item], current);
			const auto ink = pixel(EraserRectCenterX(rect), EraserRectCenterY(rect));
			const auto background = pixel(rect.left + rect.Width() * 0.75, rect.top + rect.Height() * 0.75);
			return static_cast<double>(std::abs(static_cast<int>(ink[0]) - background[0]) +
				std::abs(static_cast<int>(ink[1]) - background[1]) + std::abs(static_cast<int>(ink[2]) - background[2]));
		};
		auto animateAttributes = [&](PresetKind nextShape, draw3::shapes::FillMode nextFill)
		{
			const auto start = owner.geometryAttribute.PresentationSnapshot();
			std::array<double,10> startOpacity{};
			for (int i = 0; i < 10; ++i) startOpacity[i] = BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, GeometryStyleBegin + i).name.pct.val;
			const double firstContrast = previewContrast(GeometryThicknessBegin + 1, start);
			if (start.hits[GeometryPatternBegin].enabled)
				for (int attempt = 0; attempt < GeometryControlCount && BarGeometryAttributeTestAccess::Focused(owner.geometryAttribute) != GeometryPatternBegin; ++attempt)
					owner.geometryAttribute.Keyboard(owner, VK_TAB, true);
			std::array<std::vector<double>,10> positions, opacities;
			std::vector<double> partialThicknessContrast;
			BarUiAnimationEnabled = true; selectScene(nextShape, nextFill);
			for (int frame = 0; frame < 42; ++frame)
			{
				sceneTick(1); const auto current = owner.geometryAttribute.PresentationSnapshot(); rowGeometry(current);
				if (frame == 1)
				{
					const int item = current.items[GeometryPatternBegin].Width() > 0 ? GeometryPatternBegin : GeometryStyleBegin;
					const auto rect = current.items[item]; const auto before = GetStateModeVersionedSnapshot();
					const int focusedAttribute = BarGeometryAttributeTestAccess::Focused(owner.geometryAttribute);
					if (focusedAttribute < 0 || (focusedAttribute >= GeometryStyleBegin && focusedAttribute < GeometryCloseButton))
					{
						owner.geometryAttribute.Keyboard(owner, VK_RETURN, true);
						expect(StateModeTransitionRevision() == before.revision, "keyboard Enter cannot commit an attribute while its group reflows or retires");
					}
					ExMessage down{}; down.message = WM_LBUTTONDOWN; down.lbutton = true;
					down.x = static_cast<short>(std::lround(EraserRectCenterX(rect) * current.zoom));
					down.y = static_cast<short>(std::lround(EraserRectCenterY(rect) * current.zoom));
					owner.geometryAttribute.Pointer(owner, down);
					ExMessage up = down; up.message = WM_LBUTTONUP; up.lbutton = false; owner.geometryAttribute.Pointer(owner, up);
					expect(StateModeTransitionRevision() == before.revision, "intermediate attribute reflow Down Up cannot mutate tool or style");
					for (int attempt = 0; attempt < GeometryControlCount; ++attempt)
					{
						owner.geometryAttribute.Keyboard(owner, VK_TAB, true); const int focused = BarGeometryAttributeTestAccess::Focused(owner.geometryAttribute);
						expect(focused < 0 || (focused < GeometryCategoryCount && catalog[focused].preset.has_value()) || (current.hits[focused].visible && current.hits[focused].enabled), "keyboard traversal skips disabled future retiring or reflowing controls");
					}
				}
				for (int item = GeometryStyleBegin; item < GeometryCloseButton; ++item)
				{
					const auto& button = BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, item);
					opacities[item - GeometryStyleBegin].push_back(button.name.pct.val);
					if (current.items[item].Width() > 0) positions[item - GeometryStyleBegin].push_back((current.items[item].left - current.panel.left) / current.pose.scale);
					if (button.name.pct.tar == 0 && button.name.pct.val > 0.01)
						expect(!current.hits[item].enabled, "retiring attribute cannot accept a fresh press during fade");
				}
				const double opacity = BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, GeometryThicknessBegin + 1).name.pct.val;
				if (startOpacity[8] > 0.99 && opacity > 0.2 && opacity < 0.8) partialThicknessContrast.push_back(previewContrast(GeometryThicknessBegin + 1, current));
			}
			const auto end = owner.geometryAttribute.PresentationSnapshot();
			for (int i = 0; i < 10; ++i)
			{
				const double finalOpacity = BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, i + GeometryStyleBegin).name.pct.val;
				if (std::abs(finalOpacity - startOpacity[i]) > 0.9)
					expect(std::count_if(opacities[i].begin(), opacities[i].end(), [](double v) { return v > 0.05 && v < 0.95; }) >= 2,
						"attribute content visibility traverses intermediate opacity values");
				if (start.items[i + GeometryStyleBegin].Width() > 0 && end.items[i + GeometryStyleBegin].Width() > 0 && positions[i].size() > 2)
				{
					const double initial = (start.items[i + GeometryStyleBegin].left - start.panel.left) / start.pose.scale;
					const double final = (end.items[i + GeometryStyleBegin].left - end.panel.left) / end.pose.scale;
					if (std::abs(final - initial) > 2)
					{
						expect(std::abs(positions[i][0] - initial) < std::abs(final - initial) * 0.8,
							"surviving attribute does not jump straight to new group position");
						double largestStep = std::abs(positions[i][0] - initial);
						for (size_t step = 1; step < positions[i].size(); ++step) largestStep = (std::max)(largestStep, std::abs(positions[i][step] - positions[i][step - 1]));
						expect(largestStep < std::abs(final - initial) * 0.8, "attribute list reflow advances through continuous positions");
					}
				}
			}
			if (startOpacity[8] > 0.99 && BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, GeometryThicknessBegin + 1).name.pct.val < 0.01)
				expect(firstContrast > 10 && std::any_of(partialThicknessContrast.begin(), partialThicknessContrast.end(), [&](double v) { return v > 1 && v < firstContrast * 0.9; }),
					"actual thickness content fades in emitted pixels before leaving the list");
			checkStableScene(nextShape, nextFill);
		};
		animateAttributes(PresetKind::Line, draw3::shapes::FillMode::Outline);
		animateAttributes(PresetKind::Rectangle, draw3::shapes::FillMode::Outline);
		animateAttributes(PresetKind::Circle, draw3::shapes::FillMode::OutlineAndFill);
		animateAttributes(PresetKind::Circle, draw3::shapes::FillMode::Fill);
		animateAttributes(PresetKind::Circle, draw3::shapes::FillMode::Outline);
		BarUiAnimationEnabled = false; BarGeometryAttributeTestAccess::Display(owner, 460, 420); sceneTick(2);
		BarGeometryAttributeTestAccess::MoveRoot(owner, 80, 80); sceneTick(2);
		animateAttributes(PresetKind::RegularPolygon, draw3::shapes::FillMode::Fill);
		animateAttributes(PresetKind::Rectangle, draw3::shapes::FillMode::OutlineAndFill);
		// 提交失败时实际动画值可以前进，交互只能继续使用上次成功呈现的位置。
		caseContext = "failed presentation and input"; BarUiAnimationEnabled = false;
		selectScene(PresetKind::Rectangle, draw3::shapes::FillMode::Outline); sceneTick(2);
		const auto beforeFailure = owner.geometryAttribute.PresentationSnapshot();
		selectScene(PresetKind::Line, draw3::shapes::FillMode::Outline); rejectNextPresent = true; sceneTick(1);
		const auto failed = owner.geometryAttribute.PresentationSnapshot();
		expect(rejectedPresent && failed.items == beforeFailure.items && failed.panel == beforeFailure.panel && failed.tool == beforeFailure.tool,
			"failed actual ULW transaction leaves successful geometry and hit snapshot untouched");
		expect(BarGeometryAttributeTestAccess::Candidate(owner.geometryAttribute).items != failed.items, "failure fixture actually advanced candidate layout");
		previousPixels.clear(); sceneTick(90); checkStableScene(PresetKind::Line, draw3::shapes::FillMode::Outline);
		selectScene(PresetKind::Ellipse, draw3::shapes::FillMode::Outline); sceneTick(2);
		auto scenePointer = [&](USHORT type, int item, bool held)
		{
			const auto presented = owner.geometryAttribute.PresentationSnapshot(); const auto rect = presented.items[item];
			ExMessage message{}; message.message = type; message.lbutton = held;
			message.x = static_cast<short>(std::lround(EraserRectCenterX(rect) * presented.zoom));
			message.y = static_cast<short>(std::lround(EraserRectCenterY(rect) * presented.zoom));
			return owner.geometryAttribute.Pointer(owner, message);
		};
		scenePointer(WM_LBUTTONDOWN, 2, true); sceneTick(1);
		ExMessage lost{}; owner.geometryAttribute.Pointer(owner, lost, true); scenePointer(WM_LBUTTONUP, 2, false); sceneTick(2);
		expect(GetShapeToolSpec(GetStateModeSnapshot()).preset == PresetKind::Ellipse, "actual presented pointer cancellation cannot create a category change");
		scenePointer(WM_LBUTTONDOWN, 2, true);
		selectScene(PresetKind::Circle, draw3::shapes::FillMode::Outline); scenePointer(WM_LBUTTONUP, 2, false); sceneTick(2);
		expect(GetShapeToolSpec(GetStateModeSnapshot()).preset == PresetKind::Circle, "actual presented stale press cannot overwrite newer tool revision");
		const auto beforeClick = GetStateModeVersionedSnapshot();
		scenePointer(WM_LBUTTONDOWN, GeometryPatternBegin + 1, true); scenePointer(WM_LBUTTONUP, GeometryPatternBegin + 1, false); sceneTick(2);
		expect(StateModeTransitionRevision() > beforeClick.revision && GetShapeStyle(GetStateModeSnapshot()).linePattern == LinePattern::Dashed,
			"settled attribute list restores real presented pointer action");
		owner.spec.DiscardDeviceResources(); previousPixels.clear(); sceneTick(2); checkStableScene(PresetKind::Circle, draw3::shapes::FillMode::Outline);
		for (int item = 0; item < GeometryCategoryCount; ++item)
			if (owner.geometryAttribute.PresentationSnapshot().hits[item].visible)
				expect(BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, item).icon.cacheBitmap != nullptr,
					"visible production resources rasterize after device cache rebuild");
		owner.geometryAttribute.ResetPointerFeedback(); sceneTick(360);
		const unsigned settledCommits = actualCommits; sceneTick(20, false);
		expect(actualCommits == settledCommits && sceneResult == RenderPipeline::FrameResult::Idle && !owner.geometryAttribute.Active(),
			"actual settled scene sleeps with no animation-disabled carousel or repeated presentation");
		owner.geometryAttribute.Close(owner); sceneTick(2); const unsigned hiddenCommits = actualCommits; sceneTick(20, false);
		expect(actualCommits == hiddenCommits && !owner.geometryAttribute.PresentationSnapshot().visible && !BarGeometryAttributeTestAccess::CarouselArmed(owner.geometryAttribute),
			"hidden actual scene has no residual shape-list animation or timer wake");
		captureScene = false; previousPixels.clear(); caseContext.clear();
		BarGeometryAttributeTestAccess::Display(owner, 1200, 800); sceneTick(2); BarGeometryAttributeTestAccess::MoveRoot(owner, 80, 650); sceneTick(2);
		BarUiAnimationEnabled = true;
		// 固定70DIP主栏槽位独立于展开面板：全部类别在三语下都不能被NO_WRAP+CLIP截断。
		ChangeStateModeToShape(); owner.barState.fold = false; owner.barState.geometryAttribute = false;
		for (const wchar_t* language : {L"zh-CN", L"zh-TW", L"en-US"})
		{
			expect(I18n::load(1, L"JSON", language), "main geometry label language loads"); owner.spec.ConfigureLocalizedTypography();
			for (int i = 0; i < GeometryImplementedCategoryCount; ++i)
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
		// 构造提示读取同一生产ULW成功截面，逐槽确认真实文字，不能以面板alpha代替内容。
		auto checkPromptPixels = [&](const wchar_t* filename)
		{
			const auto frame=BarGeometryAttributeTestAccess::PromptFrame(owner.constructionPrompt);
			GeometryAttributePresentation coordinates;coordinates.zoom=frame.zoom;
			const auto panel=clientRect(frame.panel,coordinates);
			const auto background=pixel((panel.left+panel.right)/2,panel.top+3*frame.zoom);
			const auto ink=[&](EraserAttributeRect rect,COLORREF foreground)
			{
				unsigned covered=0;double peak=0;if(background[3]<180)return std::pair{covered,peak};
				const std::array<double,3> target{double(GetBValue(foreground)),double(GetGValue(foreground)),double(GetRValue(foreground))};
				std::array<double,3> base{},delta{};double norm=0;
				for(int c=0;c<3;++c){base[c]=background[c]*255./background[3];delta[c]=target[c]-base[c];norm+=delta[c]*delta[c];}
				if(norm<100)return std::pair{covered,peak};
				for(LONG y=static_cast<LONG>(std::ceil(rect.top));y<rect.bottom;++y)for(LONG x=static_cast<LONG>(std::ceil(rect.left));x<rect.right;++x)
				{
					const auto raw=pixel(x,y);if(raw[3]<180)continue;
					std::array<double,3> actual{};double projection=0,residual=0;
					for(int c=0;c<3;++c){actual[c]=raw[c]*255./raw[3];projection+=(actual[c]-base[c])*delta[c];}projection/=norm;
					for(int c=0;c<3;++c){const double difference=actual[c]-base[c]-projection*delta[c];residual+=difference*difference;}
					if(projection>.20&&projection<1.15&&residual<(std::max)(324.,norm*.04)){++covered;peak=(std::max)(peak,projection);}
				}return std::pair{covered,peak};
			};
			const auto checkWord=[&](const BarUiWordClass& word)
			{
				expect(word.size.val>0&&word.w.val>0&&word.h.val>0&&!word.content.GetVal().empty(),"construction prompt uses a real positive-size localized word slot");
				const EraserAttributeRect slot{word.inhX,word.inhY,word.inhX+word.w.val,word.inhY+word.h.val};
				const auto evidence=ink(clientRect(slot,coordinates),word.color.val);
				expect(evidence.first>=3&&evidence.second>.35,"actual construction prompt label or action has foreground text pixels beyond its surface");
				const EraserAttributeRect blank{panel.left+6*frame.zoom,panel.top+2*frame.zoom,panel.right-6*frame.zoom,panel.top+4*frame.zoom};
				const auto negative=ink(blank,word.color.val);expect(negative.first==0,"construction text oracle rejects actual blank panel padding");
			};
			checkWord(BarGeometryAttributeTestAccess::PromptLabel(owner.constructionPrompt));
			for(int i=0;i<3;++i)if(frame.buttons[i].visible)checkWord(BarGeometryAttributeTestAccess::PromptButton(owner.constructionPrompt,i).name);
			std::filesystem::create_directories(L"Build/shape-p34-ui");
			expect(SavePng(std::filesystem::path(L"Build/shape-p34-ui")/filename,scenePixels,receipt.size.cx,receipt.size.cy),"construction prompt PNG uses actual emitted production ULW pixels");
		};
		captureScene=true;
		// 构造提示使用独立状态，同一完整Bar场景呈现；关闭时不污染原dock提示业务。
		owner.barState.fold = false; owner.barState.geometryAttribute = false;
		for (const auto presetKind : {PresetKind::Polygon, PresetKind::Arc, PresetKind::Cuboid, PresetKind::Cylinder})
		{
			auto construction = std::make_shared<Inkeys::Drawing::Draw3::ShapeUiSnapshot>();
			construction->identity.session = 71; construction->identity.generation = 1;
			construction->construction.active = true; construction->construction.preset = presetKind;
			construction->construction.canBack = true; construction->construction.canComplete = presetKind == PresetKind::Polygon;
			construction->construction.confirmedSteps = 1;
			Inkeys::Drawing::Draw3::PublishShapeUiSnapshot(construction); owner.UpdateRendering(false); sceneTick(70);
			expect(owner.constructionPrompt.Visible(), "multi-contact session publishes its own production Bar prompt");
			const auto regions = owner.constructionPrompt.PresentedRegions();
			expect(regions[0].right > regions[0].left && regions[0].bottom > regions[0].top,
				"construction prompt participates in successful presentation and window bounds");
			expect(!BarGeometryAttributeTestAccess::DockIndicatorVisible(owner), "construction prompt takes priority over dock feedback without overwriting its state");
			if(presetKind==PresetKind::Polygon)
			{
				checkPromptPixels(L"construction-normal.png");
				const auto frame=BarGeometryAttributeTestAccess::PromptFrame(owner.constructionPrompt);
				const auto currentMain=owner.shapeMap[BarUISetShapeEnum::MainBar];
				const EraserAttributeRect mainRect{currentMain->inhX,currentMain->inhY,currentMain->inhX+currentMain->w.val,currentMain->inhY+currentMain->h.val};
				expect(Intersect(frame.panel,mainRect).Width()==0,"normal-work construction prompt never intersects the actual main bar");
				expect(frame.panel.bottom+5<=mainRect.top+.01,"normal-work prompt keeps the original five-DIP gap above the main bar");
			}
			const auto before = GetStateModeVersionedSnapshot();
			owner.constructionPrompt.Keyboard(owner, VK_TAB, true); owner.constructionPrompt.Keyboard(owner, VK_RIGHT, true);
			expect(SameBusinessSnapshot(before, GetStateModeVersionedSnapshot()), "prompt keyboard only navigates construction controls, never changes next-tool style");
		}
		BarGeometryAttributeTestAccess::Display(owner,460,420,192); owner.barStyle.configZoom=1;
		auto smallPrompt = std::make_shared<Inkeys::Drawing::Draw3::ShapeUiSnapshot>();
		smallPrompt->identity.session=72;smallPrompt->identity.generation=1;
		smallPrompt->construction.active=true;smallPrompt->construction.preset=PresetKind::Polygon;
		smallPrompt->construction.canBack=smallPrompt->construction.canComplete=true;smallPrompt->construction.confirmedSteps=3;
		Inkeys::Drawing::Draw3::PublishShapeUiSnapshot(smallPrompt);owner.UpdateRendering(false);sceneTick(90);
		const auto prompt = BarGeometryAttributeTestAccess::PromptFrame(owner.constructionPrompt);
		expect(prompt.visible && prompt.input, "fixed-pixel high-DPI construction prompt settles and accepts controls");
		for(const auto& button:prompt.buttons)
		{
			expect(button.visible && button.enabled && button.boundsDip.left>=prompt.panel.left && button.boundsDip.right<=prompt.panel.right &&
				button.boundsDip.top>=prompt.panel.top && button.boundsDip.bottom<=prompt.panel.bottom,
				"Finish Back Cancel stay fully inside wrapped prompt without shrinking standard buttons");
		}
		checkPromptPixels(L"construction-small-200.png");
		Inkeys::Drawing::Draw3::PublishShapeUiSnapshot({}); owner.UpdateRendering(false); sceneTick(70);
		expect(!owner.constructionPrompt.Visible(), "finished or cancelled construction prompt retires fully");
		scene = {}; owner.spec.DiscardDeviceResources();
		report << "Actual scene commits=" << actualCommits << '\n';
		globalPath = originalGlobalPath;
		report << "Geometry UI checks=" << checks << " frames=" << frames << " failures=" << failures << '\n';
		std::cerr << "Geometry UI checks=" << checks << " frames=" << frames << " failures=" << failures << '\n'; report.close();

		RenderPipeline::Shutdown(); CoUninitialize(); return failures == 0 ? 0 : 1;
	}
}
