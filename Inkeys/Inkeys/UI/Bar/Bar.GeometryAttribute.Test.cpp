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
	static const BarButtonClass& Button(const BarGeometryAttributePanel& p, int item) { return *p.buttons_[item]; }
	static bool CarouselArmed(const BarGeometryAttributePanel& p) { return p.carouselArmed_; }
	static unsigned CarouselSides(const BarGeometryAttributePanel& p) { return p.carouselSides_; }
	static auto Categories(const BarGeometryAttributePanel& p) { return p.categoryList_.Snapshot(); }
	static auto Attributes(const BarGeometryAttributePanel& p) { return p.attributeList_.Snapshot(); }
	static auto Candidate(const BarGeometryAttributePanel& p) { return p.frame_; }
	static int Focused(const BarGeometryAttributePanel& p) { return p.focused_; }
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
		expect(f.visible && f.acceptsInput && f.items[23].Width() == 0 && f.items[26].Width() > 0, "line hides fill style and keeps stroke controls");
		const auto list = BarGeometryAttributeTestAccess::Categories(owner.geometryAttribute);
		const auto grid = ResolveBarButtonListLayout(list, BarButtonListLayoutKind::GeometryGrid);
		expect(list.size() == 11 && grid.subColumns == 12 && grid.totalRows == 4 && !grid.showDivider, "shape-owned list uses its independent six-column policy");
		expect(f.items.size() == 34 && f.items[33].left > f.items[5].right, "close is the only side-rail entry");
		expect(f.items[0].top == f.items[5].top && f.items[6].top > f.items[0].top && f.items[6].left == f.items[0].left,
			"shape list keeps row-major geometry order and wraps after six categories");
		for (const size_t count : {size_t{1}, size_t{6}, size_t{7}, size_t{11}})
		{
			const std::vector<std::shared_ptr<BarButtonClass>> subset(list.begin(), list.begin() + count);
			const auto shapeGrid = ResolveBarButtonListLayout(subset, BarButtonListLayoutKind::GeometryGrid);
			expect(shapeGrid.placements.size() == count && shapeGrid.subColumns <= 12 && !shapeGrid.showDivider, "shape policy bounds only its own visible categories");
		}
		const auto standard = ResolveBarButtonVisualMetrics(BarButtonVisualLayoutKind::StandardTwoTwo);
		for (int i = 0; i < 11; ++i)
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
		for (int presetIndex = 0; presetIndex < 3; ++presetIndex)
		{
			const auto before = GetStateModeVersionedSnapshot();
			expect(ApplyBarThicknessPresetIfRevision(presetIndex, before.revision, false), "existing preset adapter accepts current shape"); tick(60);
			for (int i = 0; i < 3; ++i)
			{
				const auto& button = BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, 30 + i);
				expect(!button.name.enable.val && !button.icon.enable.val && button.name.content.GetVal().empty(), "three thickness buttons contain no numeric label or extra SVG");
				expect((button.state->state == BarWidgetState::Selected) == (i == presetIndex), "three existing presets preserve selected feedback");
			}
		}
		click(33); expect(!owner.barState.geometryAttribute && !owner.barState.drawAttribute, "only side-rail button closes panel without opening another view");
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
				for (int i = 0; i < 11; ++i)
				{
					const auto& b = BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, i);
					expect(owner.spec.MeasureText(b.name.content.GetVal(), b.name.size.val, DWRITE_FONT_WEIGHT_BOLD).width <= b.name.w.val + 0.5, "localized category label fits shared standard slot");
				}
				owner.spec.PrepareFrameLighting(1.0 / 60, {}, static_cast<int>(StateModeSelectEnum::IdtShape), 0, GetPenColor(GetStateModeSnapshot()), 0);
				auto* dc = owner.spec.GetDeviceContext(); dc->BeginDraw(); dc->SetTransform(D2D1::IdentityMatrix()); dc->Clear(D2D1::ColorF(0, 0, 0, 0));
				owner.geometryAttribute.Draw(owner.spec, dc); const HRESULT hr = dc->EndDraw(); expect(SUCCEEDED(hr), "production panel draw completes");
				const auto pixels = ReadPixels(dc, owner.spec.GetTargetBitmap());
				expect(!pixels.empty() && std::any_of(pixels.begin(), pixels.end(), [](auto v) { return v != 0; }), "production panel produces actual pixels");
				for (int i = 0; i < 11; ++i) expect(BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, i).icon.cacheBitmap != nullptr, "every category rasterizes embedded SVG");
				if (scale == 1 && !dark && !below && language == std::wstring(L"zh-CN"))
				{
					for (int item = 26; item < 33; ++item)
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
			for (int i = 23; i < 33; ++i) if (presented.items[i].Width() > 0)
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
			const bool closed = wanted != PresetKind::Line && wanted != PresetKind::Arrow;
			const bool stroke = !closed || fill != draw3::shapes::FillMode::Fill;
			for (int i = 0; i < 34; ++i)
			{
				const bool shown = i < 11 || i == 33 || ((i == 11 || i == 12) && wanted == PresetKind::Circle) ||
					(i >= 13 && i < 23 && wanted == PresetKind::RegularPolygon) || (i >= 23 && i < 26 && closed) || (i >= 26 && i < 33 && stroke);
				const auto rect = presented.items[i]; const auto& hit = presented.hits[i];
				expect((rect.Width() > 0) == shown && hit.visible == shown, "stable attribute visibility follows current shape and fill applicability");
				if (!shown) continue;
				expect(rect.left >= presented.panel.left - 0.01 && rect.top >= presented.panel.top - 0.01 &&
					rect.right <= presented.panel.right + 0.01 && rect.bottom <= presented.panel.bottom + 0.01,
					"all visible categories parameters and attributes fit actual panel bounds");
				const auto emitted = clientRect(rect, presented);
				expect(emitted.left >= -1 && emitted.top >= -1 && emitted.right <= receipt.size.cx + 1 && emitted.bottom <= receipt.size.cy + 1,
					"all visible shape controls survive complete production crop");
				if (i >= 11 && i < 23)
					expect(rect.top >= presented.parameterClip.top - 0.01 && rect.bottom <= presented.parameterClip.bottom + 0.01,
						"stable parameter buttons fit the exact clip used by production draw");
				expect(HitTestBarButton(hit, EraserRectCenterX(rect), EraserRectCenterY(rect)), "successful production presentation owns current button center");
				expect(std::abs(hit.boundsDip.left - rect.left) < 0.02 && std::abs(hit.boundsDip.top - rect.top) < 0.02 &&
					std::abs(hit.boundsDip.right - rect.right) < 0.02 && std::abs(hit.boundsDip.bottom - rect.bottom) < 0.02,
					"presented hit uses same complete rectangle as actual draw");
				if (i < 11)
				{
					const auto& button = BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, i);
					expect(owner.spec.MeasureText(button.name.content.GetVal(), button.name.size.val, DWRITE_FONT_WEIGHT_BOLD).width <= button.name.w.val + 0.5,
						"actual scene localized category label fits measured shared slot");
				}
				if (i >= 26 && i < 33)
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
						for (int item = 23; item < 33; ++item) if (presented.items[item].Width() > 0)
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
			rowGeometry(presented);
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
				for (int shape = 0; shape < 11; ++shape) for (int fill = 0; fill < 3; ++fill)
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
			for (int i = 0; i < 10; ++i) startOpacity[i] = BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, 23 + i).name.pct.val;
			const double firstContrast = previewContrast(31, start);
			if (start.hits[26].enabled)
				for (int attempt = 0; attempt < 34 && BarGeometryAttributeTestAccess::Focused(owner.geometryAttribute) != 26; ++attempt)
					owner.geometryAttribute.Keyboard(owner, VK_TAB, true);
			std::array<std::vector<double>,10> positions, opacities;
			std::vector<double> partialThicknessContrast;
			BarUiAnimationEnabled = true; selectScene(nextShape, nextFill);
			for (int frame = 0; frame < 42; ++frame)
			{
				sceneTick(1); const auto current = owner.geometryAttribute.PresentationSnapshot(); rowGeometry(current);
				if (frame == 1)
				{
					const int item = current.items[26].Width() > 0 ? 26 : 23;
					const auto rect = current.items[item]; const auto before = GetStateModeVersionedSnapshot();
					const int focusedAttribute = BarGeometryAttributeTestAccess::Focused(owner.geometryAttribute);
					if (focusedAttribute < 0 || (focusedAttribute >= 23 && focusedAttribute < 33))
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
					for (int attempt = 0; attempt < 34; ++attempt)
					{
						owner.geometryAttribute.Keyboard(owner, VK_TAB, true); const int focused = BarGeometryAttributeTestAccess::Focused(owner.geometryAttribute);
						expect(focused < 0 || (current.hits[focused].visible && current.hits[focused].enabled), "keyboard traversal skips retiring or reflow-disabled controls");
					}
				}
				for (int item = 23; item < 33; ++item)
				{
					const auto& button = BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, item);
					opacities[item - 23].push_back(button.name.pct.val);
					if (current.items[item].Width() > 0) positions[item - 23].push_back((current.items[item].left - current.panel.left) / current.pose.scale);
					if (button.name.pct.tar == 0 && button.name.pct.val > 0.01)
						expect(!current.hits[item].enabled, "retiring attribute cannot accept a fresh press during fade");
				}
				const double opacity = BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, 31).name.pct.val;
				if (startOpacity[8] > 0.99 && opacity > 0.2 && opacity < 0.8) partialThicknessContrast.push_back(previewContrast(31, current));
			}
			const auto end = owner.geometryAttribute.PresentationSnapshot();
			for (int i = 0; i < 10; ++i)
			{
				const double finalOpacity = BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, i + 23).name.pct.val;
				if (std::abs(finalOpacity - startOpacity[i]) > 0.9)
					expect(std::count_if(opacities[i].begin(), opacities[i].end(), [](double v) { return v > 0.05 && v < 0.95; }) >= 2,
						"attribute content visibility traverses intermediate opacity values");
				if (start.items[i + 23].Width() > 0 && end.items[i + 23].Width() > 0 && positions[i].size() > 2)
				{
					const double initial = (start.items[i + 23].left - start.panel.left) / start.pose.scale;
					const double final = (end.items[i + 23].left - end.panel.left) / end.pose.scale;
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
			if (startOpacity[8] > 0.99 && BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, 31).name.pct.val < 0.01)
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
		scenePointer(WM_LBUTTONDOWN, 27, true); scenePointer(WM_LBUTTONUP, 27, false); sceneTick(2);
		expect(StateModeTransitionRevision() > beforeClick.revision && GetShapeStyle(GetStateModeSnapshot()).linePattern == LinePattern::Dashed,
			"settled attribute list restores real presented pointer action");
		owner.spec.DiscardDeviceResources(); previousPixels.clear(); sceneTick(2); checkStableScene(PresetKind::Circle, draw3::shapes::FillMode::Outline);
		for (int item = 0; item < 11; ++item) expect(BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, item).icon.cacheBitmap != nullptr,
			"hidden-to-visible production resources rasterize after device cache rebuild");
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
