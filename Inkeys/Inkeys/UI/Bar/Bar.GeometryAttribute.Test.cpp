module;
#include "../../../IdtMain.h"
#include "../../../IdtState.h"
#include "../../../IdtConfiguration.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <optional>
#include "../../../IdtI18n.h"
#include "../../../IdtI18nKeys.g.h"
#include "../../../resource.h"
#include "../../Drawing/Draw3/Draw3.ShapeInteraction.h"
#include "../../Input/UiTouchRouting.h"
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

#include <cstdint>
#include <d2d1_1.h>
#include <wrl/client.h>
#include <utility>

module Inkeys.UI.Bar;
import :Main;
import :Layout;
import :Theme;
import Inkeys.UI.RenderPipeline;
import Inkeys.Text.Font;
import Inkeys.UI.UI3.Material;
import Inkeys.UI.UI3.FrameRing;
import Inkeys.UI.UI3.LoadingIndicator;
import Inkeys.UI.UI3.Progress;
import Inkeys.UI.StartupPlacement;

using namespace Inkeys::UI::UI3;

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
	static void CategoryVisible(BarGeometryAttributePanel& p,int item,bool visible) { p.buttons_[item]->userVisible = visible; }
	static auto CarouselDeadline(const BarGeometryAttributePanel& p) { return p.carouselDeadline_; }
	static auto Attributes(const BarGeometryAttributePanel& p) { return p.attributeList_.Snapshot(); }
	static auto Candidate(const BarGeometryAttributePanel& p) { return p.frame_; }
	static auto PromptFrame(const BarConstructionPrompt& p) { return p.presented_; }
	static auto PromptCandidate(const BarConstructionPrompt& p) { return p.frame_; }
	static void PromptSubmit(BarConstructionPrompt& p, std::function<bool(const Inkeys::Drawing::Draw3::ShapeUiAction&)> submit) { p.submit_=std::move(submit); }
	static const UiWordClass& PromptLabel(const BarConstructionPrompt& p) { return p.label_; }
	static const UiWordClass& PromptRingLabel(const BarConstructionPrompt& p) { return p.ringLabel_; }
	static double PromptRingProgress(const BarConstructionPrompt& p) { return p.ringProgress_.val; }
	static auto PromptContentPhase(const BarConstructionPrompt& p) { return p.contentPhase_; }
	static const UiShapeClass& PromptSurface(const BarConstructionPrompt& p) { return p.surface_; }
	static const BarButtonClass& PromptButton(const BarConstructionPrompt& p,int i) { return *p.buttons_[i]; }
	static bool DockIndicatorVisible(const BarUISetClass& p) { return p.BottomDockPresentedSnapshot().indicatorVisible; }
	static void Execute(BarGeometryAttributePanel& p, BarUISetClass& owner, int item, std::uint64_t revision) { p.Execute(owner, item, revision); }
	static double ScrollRequest(const BarGeometryAttributePanel& p) { return p.scrollRequest_; }
	static BarButtonClass& MutableButton(BarGeometryAttributePanel& p, int item) { return *p.buttons_[item]; }
	// 长按期限用真实 steady_clock；测试把起点前移代替真实等待。
	static void ShiftHold(BarGeometryAttributePanel& p, std::chrono::milliseconds by) { std::lock_guard lock(p.holdMutex_); p.holdProgress_.ShiftTime(-by); }
	static int HoldItem(BarGeometryAttributePanel& p) { std::lock_guard lock(p.holdMutex_); return p.holdItem_; }
	static int RingItem(const BarGeometryAttributePanel& p) { return p.ringItem_; }
	static double RingProgress(const BarGeometryAttributePanel& p) { return p.ringProgress_; }
	static double RingOpacity(const BarGeometryAttributePanel& p) { return p.ringOpacity_; }
	static double RingOpacityTarget(const BarGeometryAttributePanel& p) { return p.ringShown_ ? 1.0 : 0.0; }
	// 模拟进度环绘制失败（render owner 同线程写入）。
	static void FailRing(BarGeometryAttributePanel& p) { p.ringFailed_ = true; }
	static bool RingFailed(const BarGeometryAttributePanel& p) { return p.ringFailed_; }
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
			a.Shape.ModeSelect == b.Shape.ModeSelect && a.Shape.circleMode == b.Shape.circleMode && a.Shape.polygonSides == b.Shape.polygonSides && a.Shape.prismSides == b.Shape.prismSides && a.Shape.pyramidSides == b.Shape.pyramidSides &&
			a.Shape.fillMode == b.Shape.fillMode && a.Shape.openPattern == b.Shape.openPattern && a.Shape.closedPattern == b.Shape.closedPattern &&
			a.Shape.auxiliary == b.Shape.auxiliary && a.Shape.auxiliaryChoices == b.Shape.auxiliaryChoices && a.Shape.pinnedPreset == b.Shape.pinnedPreset &&
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
	// 抽取前页码加载环的原实现，仅用于证明共享 ButtonFrameRing 的页码模式逐像素不变。
	bool DrawLegacyPageRing(ID2D1DeviceContext* context, EraserAttributeRect r, float corner, float z, PageLoadingArc arc, COLORREF color, float alpha)
	{
		constexpr float inset=static_cast<float>(ButtonFrameThicknessDip)+PageLoadingStyle::Gap+PageLoadingStyle::Width/2;
		const float width=static_cast<float>(r.Width())-2*inset,height=static_cast<float>(r.Height())-2*inset;
		if(width<=PageLoadingStyle::Width||height<=PageLoadingStyle::Width)return true;
		const float radius=(std::max)(.5F,(std::min)({corner,width/2,height/2}));
		ComPtr<ID2D1Factory> factory;context->GetFactory(factory.GetAddressOf());if(!factory)return false;
		ComPtr<ID2D1PathGeometry> path;ComPtr<ID2D1GeometrySink> sink;ComPtr<ID2D1SolidColorBrush> brush;
		HRESULT hr=factory->CreatePathGeometry(path.GetAddressOf());
		if(SUCCEEDED(hr))hr=path->Open(sink.GetAddressOf());
		if(SUCCEEDED(hr)){
			const auto point=[](float x,float y){return D2D1::Point2F(x,y);};
			const auto corner90=[&](float x,float y){sink->AddArc(D2D1::ArcSegment(point(x,y),D2D1::SizeF(radius,radius),0,D2D1_SWEEP_DIRECTION_CLOCKWISE,D2D1_ARC_SIZE_SMALL));};
			sink->BeginFigure(point(width/2,0),D2D1_FIGURE_BEGIN_HOLLOW);
			sink->AddLine(point(width-radius,0));corner90(width,radius);
			sink->AddLine(point(width,height-radius));corner90(width-radius,height);
			sink->AddLine(point(radius,height));corner90(0,height-radius);
			sink->AddLine(point(0,radius));corner90(radius,0);
			sink->AddLine(point(width/2,0));
			sink->EndFigure(D2D1_FIGURE_END_CLOSED);hr=sink->Close();
		}
		float length=0;
		if(SUCCEEDED(hr))hr=path->ComputeLength(nullptr,D2D1_DEFAULT_FLATTENING_TOLERANCE,&length);
		if(SUCCEEDED(hr))hr=context->CreateSolidColorBrush(D2D1::ColorF(0,0,0,0),brush.GetAddressOf());
		if(FAILED(hr)||length<=PageLoadingStyle::Width*3)return false;
		const float weight=PageLoadingStyle::Width,perimeter=length;
		const float visible=(std::clamp)(static_cast<float>(arc.length)*perimeter,weight,perimeter-3*weight);
		const float dashes[2]={(visible-weight)/weight,(perimeter-(visible-weight))/weight};
		const float offset=(perimeter-static_cast<float>(arc.start)*perimeter)/weight;
		ComPtr<ID2D1StrokeStyle> stroke;
		const auto properties=D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND,D2D1_CAP_STYLE_ROUND,
			D2D1_CAP_STYLE_ROUND,D2D1_LINE_JOIN_ROUND,10.0F,D2D1_DASH_STYLE_CUSTOM,offset);
		if(FAILED(factory->CreateStrokeStyle(properties,dashes,2,stroke.GetAddressOf())))return true;
		brush->SetColor(D2D1::ColorF(GetRValue(color)/255.0F,GetGValue(color)/255.0F,GetBValue(color)/255.0F,alpha));
		D2D1_MATRIX_3X2_F original;context->GetTransform(&original);
		context->SetTransform(D2D1::Matrix3x2F::Scale(z,z)*D2D1::Matrix3x2F::Translation(static_cast<FLOAT>((r.left+inset)*z),static_cast<FLOAT>((r.top+inset)*z))*original);
		context->DrawGeometry(path.Get(),brush.Get(),weight,stroke.Get());
		context->SetTransform(original);return true;
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
		owner.ConfigureLocalizedTypography(); owner.barButtonSet.PresetInitialization(); owner.barMedia.LoadFormat();
		auto root = std::make_shared<UiSuperellipseClass>(100, 600, 80, 80, 3.0, 1.0, std::nullopt, std::nullopt);
		owner.superellipseMap[BarUISetSuperellipseEnum::MainButton] = root;
		auto main = std::make_shared<UiShapeClass>(330, 0, 580, 80, 8, 8, 1, std::nullopt, std::nullopt);
		owner.shapeMap[BarUISetShapeEnum::MainBar] = main;
		auto* anchor = owner.barButtonSet.preset[static_cast<int>(BarButtonPresetEnum::Geometry)];
		anchor->button.x.SetDirect(250); anchor->button.y.SetDirect(40); anchor->button.w.SetDirect(70); anchor->button.h.SetDirect(70);
		ChangeStateModeToShape();
		owner.barState.fold = false; owner.barState.geometryAttribute = true;
		owner.barState.drawAttribute = false; owner.barState.eraserAttribute = false; owner.barState.recallAttribute = false;
		owner.barState.moreExpanded = false; owner.barState.widgetPosition.primaryBar = false;
		UiAnimationEnabled = true; UiAnimationSpeedRate = 1;
		double zoom = 1; RECT work{0, 0, 1200, 800};
		auto tick = [&](int frames = 1, double dt = 1.0 / 60)
		{
			for (int f = 0; f < frames; ++f)
			{
				owner.geometryAttribute.Advance(owner, GetStateModeSnapshot(), dt, 1, zoom, work, {}, 0, 0);
				owner.geometryAttribute.CommitPresented();
			}
		};
		auto selectCategory = [&](ShapeToolbarCategory category)
		{
			BarGeometryAttributeTestAccess::Execute(owner.geometryAttribute,owner,GeometrySegmentBegin+static_cast<int>(category),StateModeTransitionRevision()); tick(90);
		};
		auto preset = [&](PresetKind p)
		{
			const auto s = GetStateModeVersionedSnapshot(); SetShapeToolPresetIfRevision(p, s.revision);
			expect(GetShapeToolSpec(GetStateModeSnapshot()).preset == p && StateModeTransitionRevision() > s.revision, "tool setter accepts current revision including repeated selection");
			for (const auto& entry : ShapeCategoryCatalog()) if (entry.preset == p) selectCategory(entry.category);
			tick(90);
		};
		bool pointerAccepted = false;
		auto pointer = [&](USHORT type, int item, bool held, bool contact = false)
		{
			const auto f = owner.geometryAttribute.PresentationSnapshot(); const auto r = f.items[item];
			ExMessage message{}; message.message = type; message.lbutton = held;
			message.x = static_cast<short>(std::lround(EraserRectCenterX(r) * f.zoom));
			message.y = static_cast<short>(std::lround(EraserRectCenterY(r) * f.zoom));
			return owner.geometryAttribute.Pointer(owner, message, false, contact, &pointerAccepted);
		};
		auto click = [&](int item, bool contact = false)
		{
			expect(pointer(WM_LBUTTONDOWN, item, true, contact), "presented down consumed"); tick(4);
			expect(pointer(WM_LBUTTONUP, item, false, contact), "presented up consumed"); expect(pointerAccepted, "accepted shape control reports button feedback including repeated selection"); tick(90);
		};
		expect(SUCCEEDED(owner.specTarget.Ensure(RenderPipeline::GetDeviceEpoch(), 1200, 800)), "offscreen target setup");
		owner.spec.SetFrameZoom(1); preset(PresetKind::Line);
		for (int item = 0; item < GeometryCategoryCount; ++item)
		{
			const auto& button = BarGeometryAttributeTestAccess::Button(owner.geometryAttribute,item);
			expect(std::abs(button.name.fontTargetSize-button.name.size.val)<.000001,"first shape expansion freezes final category font target");
		}
		for (int item : {GeometryCircleCenter,GeometryCircleBox,GeometryTriangleModeBegin,GeometryTrapezoidMode})
		{
			const auto& button = BarGeometryAttributeTestAccess::Button(owner.geometryAttribute,item);
			expect(std::abs(button.name.fontTargetSize-button.name.size.val)<.000001,"first parameter expansion freezes final option font target");
		}
		expect(!owner.geometryAttribute.Active(), "stable panel sleeps between carousel deadlines");
		auto f = owner.geometryAttribute.PresentationSnapshot();
		expect(f.visible && f.acceptsInput && f.items[GeometryStyleBegin].Width() == 0 && f.items[GeometryPatternBegin].Width() > 0, "line hides fill style and keeps stroke controls");
		const auto list = BarGeometryAttributeTestAccess::Categories(owner.geometryAttribute);
		const auto grid = ResolveBarButtonListLayout(list, BarButtonListLayoutKind::GeometryGrid);
		expect(list.size() == GeometryCategoryCount && grid.subColumns == 12 && grid.totalRows == 6 && !grid.showDivider, "shape-owned list uses its independent six-column policy");
		expect(f.items.size() == GeometryControlCount && std::abs(f.items[GeometryCloseButton].right-f.items[5].right)<.01 && f.items[GeometryCloseButton].bottom<f.items[5].top, "close sits in the header aligned to the last category column");
		const auto& catalog = ShapeCategoryCatalog();
		expect(f.items[0].top == f.items[5].top && f.items[6].top > f.items[0].top && f.items[6].left == f.items[0].left,
			"shape list keeps row-major geometry order and wraps after six categories");
		expect(catalog[9].preset == PresetKind::RegularPolygon && catalog[10].preset == PresetKind::Polygon && catalog[11].preset == PresetKind::Polyline,
			"regular and free polygons stay adjacent before polyline");
		for (const size_t count : {size_t{1}, size_t{6}, size_t{7}, size_t{11}, size_t{12}, size_t{35}})
		{
			std::vector<std::shared_ptr<BarButtonClass>> subset;
			for (size_t i = 0; i < count; ++i) { auto button = std::make_shared<BarButtonClass>(*list[i]); button->hide = false; subset.push_back(std::move(button)); }
			const auto shapeGrid = ResolveBarButtonListLayout(subset, BarButtonListLayoutKind::GeometryGrid);
			expect(shapeGrid.placements.size() == count && shapeGrid.subColumns <= 12 && !shapeGrid.showDivider, "shape policy bounds only its own visible categories");
		}
		const auto standard = ResolveButtonVisualMetrics(ButtonVisualLayoutKind::StandardTwoTwo);
		for (int i = 0; i < GeometryCategoryCount; ++i)
		{
			const auto& button = *list[i];
			expect(button.size == ButtonSizeEnum::twoTwo && (i >= 17 || (f.items[i].Width() == standard.buttonWidthDip && f.items[i].Height() == standard.buttonHeightDip)),
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
		const auto carouselDeadline = BarGeometryAttributeTestAccess::CarouselDeadline(owner.geometryAttribute);
		bool carouselDue = false;
		expect(owner.geometryAttribute.ConsumeCarouselWake(carouselDeadline-std::chrono::milliseconds(1),carouselDue) == carouselDeadline && !carouselDue,"early deadline preserves sleeping render");
		expect(owner.geometryAttribute.ConsumeCarouselWake(carouselDeadline,carouselDue) == std::chrono::steady_clock::time_point{} && carouselDue,"deadline requests exactly one render frame");
		expect(BarGeometryAttributeTestAccess::CarouselSides(owner.geometryAttribute) == carouselBefore,"wake owner does not advance visual state");
		(void)owner.geometryAttribute.ConsumeCarouselWake(carouselDeadline,carouselDue); expect(!carouselDue,"deadline wake is consumed once");
		owner.geometryAttribute.Advance(owner,GetStateModeSnapshot(),1.0/60,1,zoom,work,{},0,0,nullptr,false,carouselDeadline);
		tick(60);
		expect(BarGeometryAttributeTestAccess::CarouselSides(owner.geometryAttribute) != carouselBefore && GetShapeToolSpec(GetStateModeSnapshot()) == selectedBefore, "carousel does not mutate actual sides or selection");
		expect(!owner.geometryAttribute.Active(), "content transition settles before next timer");

		const auto disabledOpacity = [&] { return ThemeMaterial::DisabledContentOpacity(owner.barStyle.darkStyle ? 0.0 : 1.0); };
		expect(catalog.size() == 35 && GeometryImplementedCategoryCount == 20 && GeometryControlCount == 70, "35 catalog tools, 20 enabled presets and derived parameter/segment controls");
		for (int i = 0; i < GeometryCategoryCount; ++i)
		{
			expect(catalog[i].key && catalog[i].resource && !ShapeCategoryLabel(i).empty(), "every catalog entry has stable UI key resource and label");
			expect(catalog[i].preset.has_value() == (i < 14 || (i >= 17 && i < 23)), "future catalog tools do not invent business preset mappings");
			for (int j = 0; j < i; ++j) expect(std::wstring(catalog[i].key) != catalog[j].key, "catalog UI keys are unique");
		}

		std::array<int,3> categoryCounts{};
		for (const auto& entry : catalog)
		{
			++categoryCounts[static_cast<int>(entry.category)];
			expect(entry.preset != PresetKind::RoundedRectangle,"rounded rectangle is absent from toolbar catalog");
		}
		expect(categoryCounts == std::array<int,3>{17,6,12},"approved category membership is 17/6/12");
		// 原业务快照保留圆角枚举，面板及下一笔只投影为普通矩形。
		(void)SetShapeToolPresetIfRevision(PresetKind::RoundedRectangle,StateModeTransitionRevision());
		owner.geometryAttribute.Close(owner); tick(90); owner.barState.geometryAttribute = true; tick(90);
		expect(GetShapeToolSpec(GetStateModeSnapshot()).preset == PresetKind::Rectangle
			&& BarGeometryAttributeTestAccess::Button(owner.geometryAttribute,2).state->state == WidgetState::Selected,"old rounded tool opens with rectangle selected");
		preset(PresetKind::Prism); owner.geometryAttribute.Close(owner); tick(90);
		owner.barState.geometryAttribute = true; tick(90); f = owner.geometryAttribute.PresentationSnapshot();
		expect(f.category == ShapeToolbarCategory::Solid && BarGeometryAttributeTestAccess::Button(owner.geometryAttribute,21).state->state == WidgetState::Selected,
			"opening with solid tool selects solid category and its actual tool");
		const auto beforeCategory = GetStateModeVersionedSnapshot();
		const auto oldCategory = f;
		BarGeometryAttributeTestAccess::Execute(owner.geometryAttribute,owner,GeometrySegmentBegin,StateModeTransitionRevision()); tick(1,0);
		const auto switchStart = owner.geometryAttribute.PresentationSnapshot();
		expect(SameBusinessSnapshot(beforeCategory,GetStateModeVersionedSnapshot()),"category switch preserves complete tool state and revision");
		expect(switchStart.items[17] == oldCategory.items[17] && switchStart.items[0].left >= switchStart.panel.left+4
			&& std::abs(switchStart.items[0].top-switchStart.categoryClip.top) < 0.01,"switch first frame keeps outgoing anchor and incoming target without fly-in");
		expect(BarGeometryAttributeTestAccess::Button(owner.geometryAttribute,0).icon.pct.val == 0,"new category starts transparent at its target");
		tick(1); const auto switchMoving = owner.geometryAttribute.PresentationSnapshot();
		expect(switchMoving.panel.Height() > oldCategory.panel.Height() && switchMoving.panel.Height() < 354.5,"category height traverses continuous batch samples");
		// 分类重排中的新旧目录、固定行和分段都不能借助新按压执行。
		for (int item = 0; item < GeometryControlCount; ++item) if (switchMoving.hits[item].visible)
		{
			expect(!switchMoving.hits[item].enabled, "moving category layout disables every presented control");
			for (bool contact : {false,true})
			{
				pointer(WM_LBUTTONDOWN,item,true,contact); pointer(WM_LBUTTONUP,item,false,contact);
				expect(!pointerAccepted, "moving category layout rejects mouse and contact activation");
			}
			expect(SameBusinessSnapshot(beforeCategory,GetStateModeVersionedSnapshot()), "moving category controls preserve tool style and revision");
		}
		tick(90);
		expect(owner.geometryAttribute.PresentationSnapshot().category == ShapeToolbarCategory::Regular, "moving segment activation cannot redirect category");
		// 少于六项且只有辅助项时，面板宽度由真实辅助文字槽决定。
		for (int i = 20; i < 23; ++i) BarGeometryAttributeTestAccess::CategoryVisible(owner.geometryAttribute,i,false);
		selectCategory(ShapeToolbarCategory::Regular); selectCategory(ShapeToolbarCategory::Solid);
		expect(owner.geometryAttribute.PresentationSnapshot().panel.Width() >= 354.5,"fewer than six items falls back to the measured attribute row width");
		for (int i = 20; i < 23; ++i) BarGeometryAttributeTestAccess::CategoryVisible(owner.geometryAttribute,i,true);
		selectCategory(ShapeToolbarCategory::Regular); selectCategory(ShapeToolbarCategory::Solid);
		const std::array<Auxiliary,3> auxiliaryBits{Auxiliary::Center,Auxiliary::Foci,Auxiliary::Diagonals};
		for (const auto& entry : catalog) if (entry.preset)
		{
			preset(*entry.preset); f = owner.geometryAttribute.PresentationSnapshot();
			for (int bit = 0; bit < 3; ++bit)
			{
				const int item = GeometryAuxBegin+bit; const auto& button = BarGeometryAttributeTestAccess::Button(owner.geometryAttribute,item);
				const bool supported = (SupportedAuxiliary(*entry.preset) & auxiliaryBits[bit]) != Auxiliary::None;
				expect((f.items[item].Width() > 0) == supported,"auxiliary toggle presence follows preset capability");
				if (supported) expect(button.name.enable.val && button.icon.enable.val && !f.accessibleNames[item].empty()
					&& button.name.content.GetVal() == ShapeAuxiliaryLabel(auxiliaryBits[bit],*entry.preset)
					&& owner.spec.MeasureText(button.name.content.GetVal(),button.name.size.val).width <= button.name.w.val+0.5
					&& (button.state->state == WidgetState::Selected) == ((GetShapeAuxiliary(GetStateModeSnapshot()) & auxiliaryBits[bit]) != Auxiliary::None),"auxiliary toggles keep measured localized labels beside icons and correct defaults");
			}
		}
		for (const auto& entry : catalog) if (entry.preset && SupportedAuxiliary(*entry.preset) != Auxiliary::None)
		{
			preset(*entry.preset); const auto before = GetStateModeVersionedSnapshot();
			const auto beforeStyle = GetShapeStyle(before.state); const auto beforeTool = GetShapeToolSpec(before.state);
			const auto editorBefore = Inkeys::Drawing::Draw3::ProductShapeUiSnapshot();
			const auto bit = SupportedAuxiliary(*entry.preset);
			const int item = GeometryAuxBegin+(bit == Auxiliary::Center ? 0 : bit == Auxiliary::Foci ? 1 : 2);
			click(item); const auto after = GetStateModeVersionedSnapshot();
			expect((GetShapeAuxiliary(after.state) & bit) != (GetShapeAuxiliary(before.state) & bit)
				&& after.revision == before.revision+1 && GetShapeToolSpec(after.state) == beforeTool,"auxiliary click changes only next-stroke style and its revision");
			auto normalized = after; normalized.revision = before.revision;
			normalized.state.Shape.auxiliary = before.state.Shape.auxiliary; normalized.state.Shape.auxiliaryChoices = before.state.Shape.auxiliaryChoices;
			expect(SameBusinessSnapshot(before,normalized),"auxiliary toggle preserves every other business field");
			auto expectedStyle = beforeStyle; expectedStyle.auxiliary = GetShapeAuxiliary(after.state);
			expect(GetShapeStyle(after.state) == expectedStyle && Inkeys::Drawing::Draw3::ProductShapeUiSnapshot() == editorBefore,"auxiliary toggle leaves object snapshot and other style fields intact");
			BarGeometryAttributeTestAccess::Execute(owner.geometryAttribute,owner,item,before.revision);
			expect(SameBusinessSnapshot(after,GetStateModeVersionedSnapshot()),"stale auxiliary toggle cannot overwrite newer next-stroke state");
			click(item);
		}
		preset(PresetKind::Line);

		for (int item = 0; item < GeometryCategoryCount; ++item)
		{
			if (catalog[item].preset) continue;
			selectCategory(catalog[item].category);
			caseContext = "disabled tool=" + std::to_string(item);
			const auto before = GetStateModeVersionedSnapshot();
			pointer(WM_MOUSEMOVE, item, false); tick(10);
			for (bool contact : {false, true})
			{
				expect(pointer(WM_LBUTTONDOWN, item, true, contact), "disabled category consumes pointer down instead of canvas fallthrough"); tick(5);
				expect(pointer(WM_LBUTTONUP, item, false, contact), "disabled category consumes pointer up instead of canvas fallthrough"); expect(!pointerAccepted, "disabled category does not report accepted feedback");
				pointer(WM_LBUTTONDBLCLK, item, true, contact); pointer(WM_LBUTTONUP, item, false, contact);
				ExMessage lost{}; owner.geometryAttribute.Pointer(owner, lost, true, contact); tick(40);
				expect(SameBusinessSnapshot(before, GetStateModeVersionedSnapshot()), "disabled mouse and contact actions preserve all business fields and revision");
			}
			BarGeometryAttributeTestAccess::Execute(owner.geometryAttribute, owner, item, before.revision);
			expect(SameBusinessSnapshot(before, GetStateModeVersionedSnapshot()), "direct Execute cannot activate future tool");
			const auto& button = BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, item);
			expect(button.state->state == WidgetState::Disable && std::abs(button.icon.pct.val - disabledOpacity()) < 0.000001 && std::abs(button.name.pct.val - disabledOpacity()) < 0.000001,
				"future tools use shared theme disabled opacity");
			expect(button.pressScale.val == 1 && !f.hits[item].enabled, "future tools do not acquire successful press feedback or action hit");
		}
		owner.geometryAttribute.ResetPointerFeedback(); caseContext.clear(); selectCategory(ShapeToolbarCategory::Regular); tick(90);

		// 真实共享按钮按压/回弹，命中几何不跟随 draw-only pressScale 收缩。
		f = owner.geometryAttribute.PresentationSnapshot(); const auto beforeHit = f.items[1];
		pointer(WM_MOUSEMOVE, 1, false); tick(15); pointer(WM_LBUTTONDOWN, 1, true); tick(10);
		expect(BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, 1).pressScale.val < 1 && owner.geometryAttribute.PresentationSnapshot().items[1] == beforeHit, "press uses shared scale without moving presented hit");
		expect(!BarGeometryAttributeTestAccess::CarouselArmed(owner.geometryAttribute), "press pauses carousel deadline");
		pointer(WM_LBUTTONUP, 1, false); tick(90);
		expect(GetShapeToolSpec(GetStateModeSnapshot()).preset == PresetKind::Arrow && BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, 1).pressScale.val == 1, "release commits once and rebounds");
		pointer(WM_LBUTTONDOWN, 2, true); pointer(WM_MOUSEMOVE, 0, true); pointer(WM_MOUSEMOVE, 2, true); pointer(WM_LBUTTONUP, 2, false); tick(60);
		expect(GetShapeToolSpec(GetStateModeSnapshot()).preset == PresetKind::Arrow && !pointerAccepted, "drag out remains cancelled after reenter without accepted feedback");
		pointer(WM_LBUTTONDOWN, 2, true);
		{ const auto s = GetStateModeVersionedSnapshot(); SetShapeToolPresetIfRevision(PresetKind::Ellipse, s.revision); }
		pointer(WM_LBUTTONUP, 2, false); tick(60);
		expect(GetShapeToolSpec(GetStateModeSnapshot()).preset == PresetKind::Ellipse && !pointerAccepted, "stale press cannot overwrite newer tool revision or report accepted feedback");
		pointer(WM_LBUTTONDOWN, 2, true);
		ExMessage cancel{}; owner.geometryAttribute.Pointer(owner, cancel, true, true); pointer(WM_LBUTTONUP, 2, false, true); tick(60);
		expect(GetShapeToolSpec(GetStateModeSnapshot()).preset == PresetKind::Ellipse && !pointerAccepted, "capture cancellation leaves tool unchanged without accepted feedback");
		for (int i = 0; i < GeometryCategoryCount; ++i) if (catalog[i].preset) { selectCategory(catalog[i].category); click(i, i % 2 != 0); expect(GetShapeToolSpec(GetStateModeSnapshot()).preset == *catalog[i].preset, "every enabled category commits through normalized pointer path"); }
		preset(PresetKind::RegularPolygon); // 全类别遍历现在以Pyramid结束，轮播和边数用例须显式选择原目标。
		expect(!BarGeometryAttributeTestAccess::CarouselArmed(owner.geometryAttribute), "selected polygon stops carousel");
		for (unsigned sides = 3; sides <= 14; ++sides)
		{
			click(GeometrySidesBegin + sides - 3); const auto tool = GetShapeToolSpec(GetStateModeSnapshot());
			expect(tool.sides == sides && ShapeToolIconResource(tool) == L"barShapePolygon" + std::to_wstring(sides), "numeric sides select exact centered polygon icon");
		}
		preset(PresetKind::Triangle); f = owner.geometryAttribute.PresentationSnapshot();
		expect(f.items[GeometryTriangleModeBegin].Width() > 0 && f.items[GeometryTriangleModeBegin+2].Width() > 0 && f.items[GeometryTrapezoidMode].Width() == 0,"triangle exposes three construction constraints");
		click(GeometryTriangleModeBegin+2); expect(GetShapeToolSpec(GetStateModeSnapshot()).constraint == PlanarConstraint::Equilateral && GetShapeToolSpec(GetStateModeSnapshot()).creationMode == CreationMode::CenterRadius,"triangle equilateral selects center construction");
		preset(PresetKind::Trapezoid); f = owner.geometryAttribute.PresentationSnapshot();
		expect(f.items[GeometryTrapezoidMode].Width() > 0 && BarGeometryAttributeTestAccess::Button(owner.geometryAttribute,GeometryTrapezoidMode).state->state != WidgetState::Selected,"trapezoid is unconstrained by default");
		click(GeometryTrapezoidMode); expect(GetShapeToolSpec(GetStateModeSnapshot()).constraint == PlanarConstraint::Isosceles,"trapezoid isosceles toggles through production action");
		preset(PresetKind::Circle); f = owner.geometryAttribute.PresentationSnapshot();
		expect(f.items[GeometryCircleCenter].Width() > 0 && f.items[GeometrySidesBegin].Width() == 0, "circle displays two mode options only");
		selectCategory(ShapeToolbarCategory::Solid); f = owner.geometryAttribute.PresentationSnapshot();
		expect(f.items[GeometryCircleCenter].Width()==0 && f.items[GeometryAuxBegin].Width()==0 && GetShapeToolSpec(GetStateModeSnapshot()).preset==PresetKind::Circle,"browsing another category hides circle parameters without changing the current tool");
		selectCategory(ShapeToolbarCategory::Regular);
		click(GeometryCircleBox); expect(GetShapeToolSpec(GetStateModeSnapshot()).creationMode == CreationMode::Drag, "bounding square mode");
		click(GeometryCircleCenter); expect(GetShapeToolSpec(GetStateModeSnapshot()).creationMode == CreationMode::CenterRadius, "center radius mode");
		click(GeometryStyleBegin + 1); f = owner.geometryAttribute.PresentationSnapshot();
		expect(GetShapeFillMode(GetStateModeSnapshot()) == draw3::shapes::FillMode::Fill && f.items[GeometryStyleBegin+2].Width() == 0
			&& f.items[GeometryPatternBegin].Width() == 0 && f.items[GeometryThicknessBegin].Width() == 0 && f.items[GeometryAuxBegin].Width() == 0
			&& f.items[GeometryCircleCenter].Width() > 0, "fill-only removes mixed entry and hides line width and auxiliary controls while retaining construction");
		{ const auto legacy = GetStateModeVersionedSnapshot(); SetShapeFillModeIfRevision(draw3::shapes::FillMode::OutlineAndFill,legacy.revision); }
		tick(90); f = owner.geometryAttribute.PresentationSnapshot();
		expect(f.items[GeometryStyleBegin+2].Width() == 0
			&& BarGeometryAttributeTestAccess::Button(owner.geometryAttribute,GeometryStyleBegin).state->state == WidgetState::Selected
			&& BarGeometryAttributeTestAccess::Button(owner.geometryAttribute,GeometryStyleBegin+1).state->state == WidgetState::Selected,
			"legacy mixed style selects both retained entries without exposing a third button");
		click(GeometryStyleBegin); expect(GetShapeFillMode(GetStateModeSnapshot()) == draw3::shapes::FillMode::Outline,"clicking legacy mixed state converts explicitly to outline only");
		for (int pattern = 0; pattern < 4; ++pattern) { click(GeometryPatternBegin + pattern); expect(static_cast<int>(GetShapeStyle(GetStateModeSnapshot()).linePattern) == pattern, "line pattern current revision setter"); }
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
				expect((button.state->state == WidgetState::Selected) == (i == presetIndex), "three existing presets preserve selected feedback");
			}
		}
		click(GeometryCloseButton); expect(!owner.barState.geometryAttribute && !owner.barState.drawAttribute, "only side-rail button closes panel without opening another view");
		owner.barState.geometryAttribute = true; tick(90);

		// R1/R2：类别角标、长按固定与进度环（真实面板输入入口 + 交互 owner 期限）。
		{
			using namespace std::chrono_literals;
			caseContext = "pin-badge-hold";
			auto& panel = owner.geometryAttribute;
			const auto indexOf = [&](PresetKind p) { for (int i = 0; i < GeometryCategoryCount; ++i) if (catalog[i].preset == p) return i; return -1; };
			const int line = indexOf(PresetKind::Line), arrow = indexOf(PresetKind::Arrow), rect = indexOf(PresetKind::Rectangle), ellipse = indexOf(PresetKind::Ellipse);
			const auto badge = [&](int item) -> ButtonBadgeClass& { return *BarGeometryAttributeTestAccess::MutableButton(panel,item).badge; };
			const auto badgeGeometry = [&](int item) { auto& b = BarGeometryAttributeTestAccess::MutableButton(panel,item); return ResolveButtonBadgeGeometry(b,{b.button.inhX,b.button.inhY}); };
			const auto pinned = [] { return IsShapeToolPinned(GetStateModeSnapshot()); };
			for (int i = 0; i < GeometryCategoryCount; ++i)
				expect(BarGeometryAttributeTestAccess::Button(panel,i).badge.has_value() == catalog[i].preset.has_value(), "only selectable categories own an optional badge");
			preset(PresetKind::Rectangle);
			expect(badge(line).progress.val == 1 && badge(arrow).progress.val == 1 && badge(rect).progress.val == 0, "line and arrow categories keep resident pin badges");
			{
				const auto g = badgeGeometry(line); const auto r = panel.PresentationSnapshot().items[line];
				expect(std::abs(g.centerX-(r.right-ButtonBadgeInsetDip-ButtonBadgeDiameterDip/2)) < .01 && std::abs(g.centerY-(r.top+ButtonBadgeInsetDip+ButtonBadgeDiameterDip/2)) < .01
					&& g.diameter == ButtonBadgeDiameterDip && std::abs(g.angle) < .000001, "panel badge sits at the top-right inset of its category");
			}
			click(ellipse);
			expect(GetShapeToolSpec(GetStateModeSnapshot()).preset == PresetKind::Ellipse && !pinned() && badge(ellipse).progress.val == 0, "short tap still selects without pinning");

			// 0.2s 前不出现进度环；期限由交互 owner 消费，不依赖帧率。
			const auto beforeHold = GetStateModeVersionedSnapshot();
			expect(pointer(WM_LBUTTONDOWN, rect, true), "hold down consumed");
			auto wake = panel.ConsumeHoldWake(owner, std::chrono::steady_clock::now());
			expect(wake != std::chrono::steady_clock::time_point{} && wake - std::chrono::steady_clock::now() <= 200ms, "hold arms the 0.2s ring deadline");
			tick(5); expect(BarGeometryAttributeTestAccess::RingItem(panel) < 0, "no progress ring before half a second");
			BarGeometryAttributeTestAccess::ShiftHold(panel, 250ms);
			wake = panel.ConsumeHoldWake(owner, std::chrono::steady_clock::now());
			expect(wake != std::chrono::steady_clock::time_point{} && wake - std::chrono::steady_clock::now() <= 750ms && wake - std::chrono::steady_clock::now() > 600ms, "ring wake then hands over the one-second commit deadline");
			tick(1);
			expect(BarGeometryAttributeTestAccess::RingItem(panel) == rect && BarGeometryAttributeTestAccess::RingProgress(panel) > 0
				&& BarGeometryAttributeTestAccess::RingProgress(panel) < .2 && panel.Active(), "progress ring starts after the delay and keeps frames active");
			BarGeometryAttributeTestAccess::ShiftHold(panel, 270ms); tick(30);
			const double midProgress = BarGeometryAttributeTestAccess::RingProgress(panel);
			expect(midProgress > .35 && midProgress < .6 && BarGeometryAttributeTestAccess::RingOpacity(panel) == 1, "ring progress follows elapsed time from 0.2s over 0.8s");
			{
				// 从顶边中点顺时针：约四成进度时右上有环、左上仍空。
				auto* dc = owner.specTarget.DeviceContext(); owner.spec.SetFrameZoom(zoom);
				owner.PrepareFrameLighting(1.0 / 60, {}, ResolveMainBarFrameLightingInput(&owner, static_cast<int>(StateModeSelectEnum::IdtShape), 0, GetPenColor(GetStateModeSnapshot()), 0));
				dc->BeginDraw(); dc->SetTransform(D2D1::IdentityMatrix()); dc->Clear(D2D1::ColorF(0, 0, 0, 0)); panel.Draw(owner.spec, dc);
				expect(SUCCEEDED(dc->EndDraw()), "panel with progress ring draws");
				const auto pixels = ReadPixels(dc, owner.specTarget.TargetBitmap()); const auto r = panel.PresentationSnapshot().items[rect];
				const UINT width = owner.specTarget.TargetBitmap()->GetPixelSize().width;
				// 面板底色不透明，按与环色（IconPrimary）的最小色差比较左右两侧。
				const COLORREF ringColor = GetThemeColor(ThemeColorEnum::IconPrimary);
				const auto distance = [&](size_t i) { return std::abs(static_cast<int>(pixels[i]) - GetBValue(ringColor)) + std::abs(static_cast<int>(pixels[i + 1]) - GetGValue(ringColor))
					+ std::abs(static_cast<int>(pixels[i + 2]) - GetRValue(ringColor)); };
				int right = 765, left = 765;
				for (int y = static_cast<int>(r.top); y < static_cast<int>(r.top) + 10; ++y) for (int dx = 4; dx < 9; ++dx)
				{
					const size_t a = (static_cast<size_t>(y) * width + static_cast<size_t>(EraserRectCenterX(r) + dx)) * 4;
					const size_t b = (static_cast<size_t>(y) * width + static_cast<size_t>(EraserRectCenterX(r) - dx)) * 4;
					if (b + 3 < pixels.size() && a + 3 < pixels.size()) { right = (std::min)(right, distance(a)); left = (std::min)(left, distance(b)); }
				}
				report << "HoldRingPixels rightDistance=" << right << " leftDistance=" << left << '\n';
				(void)SavePng(L"Build/shape-p2-ui/hold-ring.png", pixels, width, owner.specTarget.TargetBitmap()->GetPixelSize().height);
				expect(right < 90 && left > right + 150, "progress ring is drawn clockwise from the top-edge midpoint");
			}
			expect(SameBusinessSnapshot(beforeHold, GetStateModeVersionedSnapshot()), "ring phase has no business side effects");
			pointer(WM_LBUTTONUP, rect, false); tick(40);
			expect(SameBusinessSnapshot(beforeHold, GetStateModeVersionedSnapshot()) && !pointerAccepted && BarGeometryAttributeTestAccess::RingItem(panel) < 0,
				"releasing before completion neither selects nor pins and the ring fades out");

			// 移出按钮中止。
			pointer(WM_LBUTTONDOWN, rect, true); BarGeometryAttributeTestAccess::ShiftHold(panel, 360ms);
			(void)panel.ConsumeHoldWake(owner, std::chrono::steady_clock::now()); tick(2);
			pointer(WM_MOUSEMOVE, ellipse, true);
			expect(BarGeometryAttributeTestAccess::HoldItem(panel) < 0, "leaving the button aborts the hold");
			BarGeometryAttributeTestAccess::ShiftHold(panel, 2000ms);
			expect(panel.ConsumeHoldWake(owner, std::chrono::steady_clock::now()) == std::chrono::steady_clock::time_point{}, "aborted hold schedules nothing");
			pointer(WM_LBUTTONUP, ellipse, false); tick(40);
			expect(SameBusinessSnapshot(beforeHold, GetStateModeVersionedSnapshot()), "aborted hold leaves the tool unchanged");

			// 超过移动阈值只中止长按，原按钮内抬起仍是短按。
			{
				const auto fr = panel.PresentationSnapshot(); const auto r = fr.items[rect];
				pointer(WM_LBUTTONDOWN, rect, true);
				ExMessage drift{}; drift.message = WM_MOUSEMOVE; drift.lbutton = true;
				drift.x = static_cast<short>(std::lround(EraserRectCenterX(r) * fr.zoom) + 8); drift.y = static_cast<short>(std::lround(EraserRectCenterY(r) * fr.zoom));
				panel.Pointer(owner, drift);
				expect(BarGeometryAttributeTestAccess::HoldItem(panel) < 0, "moving beyond the slop aborts the hold");
				pointer(WM_LBUTTONUP, rect, false); tick(60);
				expect(GetShapeToolSpec(GetStateModeSnapshot()).preset == PresetKind::Rectangle && !pinned(), "drifted press inside the button keeps the short tap");
			}

			// 取消中止。
			const auto beforeCancel = GetStateModeVersionedSnapshot();
			pointer(WM_LBUTTONDOWN, ellipse, true); BarGeometryAttributeTestAccess::ShiftHold(panel, 730ms);
			{ ExMessage lost{}; panel.Pointer(owner, lost, true); }
			BarGeometryAttributeTestAccess::ShiftHold(panel, 1000ms);
			expect(panel.ConsumeHoldWake(owner, std::chrono::steady_clock::now()) == std::chrono::steady_clock::time_point{}, "cancelled hold cannot commit later");
			pointer(WM_LBUTTONUP, ellipse, false); tick(40);
			expect(SameBusinessSnapshot(beforeCancel, GetStateModeVersionedSnapshot()), "pointer cancellation leaves no side effect");

			// 满 1s：选中并固定；随后的抬起不触发短按。
			pointer(WM_LBUTTONDOWN, ellipse, true); BarGeometryAttributeTestAccess::ShiftHold(panel, 1100ms);
			expect(panel.ConsumeHoldWake(owner, std::chrono::steady_clock::now()) == std::chrono::steady_clock::time_point{}, "commit deadline is consumed once");
			expect(GetShapeToolSpec(GetStateModeSnapshot()).preset == PresetKind::Ellipse && pinned() && GetStateModeSnapshot().Shape.pinnedPreset == PresetKind::Ellipse,
				"completed hold selects and pins the category");
			const auto afterCommit = StateModeTransitionRevision();
			tick(3);
			{
				const auto g = badgeGeometry(ellipse);
				expect(badge(ellipse).progress.val > 0 && badge(ellipse).progress.val < 1 && g.angle < 0 && g.angle > -45, "pinned category badge scales and rotates in");
			}
			pointer(WM_LBUTTONUP, ellipse, false); tick(60);
			expect(StateModeTransitionRevision() == afterCommit && pinned(), "release after a completed hold does not short-tap");
			expect(badge(ellipse).progress.val == 1 && std::abs(badgeGeometry(ellipse).angle) < .000001 && badge(rect).progress.val == 0, "panel badge settles on the pinned category only");

			// 再次长按取消固定，角标旋转离场。
			pointer(WM_LBUTTONDOWN, ellipse, true); BarGeometryAttributeTestAccess::ShiftHold(panel, 1100ms);
			(void)panel.ConsumeHoldWake(owner, std::chrono::steady_clock::now()); pointer(WM_LBUTTONUP, ellipse, false); tick(9);
			expect(!pinned() && GetShapeToolSpec(GetStateModeSnapshot()).preset == PresetKind::Ellipse, "second hold unpins");
			expect(badge(ellipse).progress.val > 0 && badgeGeometry(ellipse).angle > 0, "unpinned badge rotates out");
			tick(60); expect(badge(ellipse).progress.val == 0, "unpinned badge disappears");

			// 直线/箭头：长按不改变固定状态。
			pointer(WM_LBUTTONDOWN, line, true); BarGeometryAttributeTestAccess::ShiftHold(panel, 1100ms);
			(void)panel.ConsumeHoldWake(owner, std::chrono::steady_clock::now()); pointer(WM_LBUTTONUP, line, false); tick(10);
			expect(GetShapeToolSpec(GetStateModeSnapshot()).preset == PresetKind::Line && pinned() && !GetStateModeSnapshot().Shape.pinnedPreset, "hold on line selects it and keeps it pinned");
			pointer(WM_LBUTTONDOWN, line, true); BarGeometryAttributeTestAccess::ShiftHold(panel, 1100ms);
			(void)panel.ConsumeHoldWake(owner, std::chrono::steady_clock::now()); pointer(WM_LBUTTONUP, line, false); tick(60);
			expect(pinned() && badge(line).progress.val == 1 && badge(arrow).progress.val == 1, "repeated hold cannot unpin line");

			// 切换到其他图形清除固定；回到原图形也不恢复。
			pointer(WM_LBUTTONDOWN, ellipse, true); BarGeometryAttributeTestAccess::ShiftHold(panel, 1100ms);
			(void)panel.ConsumeHoldWake(owner, std::chrono::steady_clock::now()); pointer(WM_LBUTTONUP, ellipse, false); tick(60);
			expect(pinned(), "ellipse pinned again");
			click(rect); expect(!pinned() && !GetStateModeSnapshot().Shape.pinnedPreset, "switching category clears the pin");
			click(ellipse); expect(!pinned() && badge(ellipse).progress.val == 0, "returning to the old category does not resurrect the pin");

			// 关闭动画时角标直接跳到终态。
			UiAnimationEnabled = false;
			pointer(WM_LBUTTONDOWN, ellipse, true); BarGeometryAttributeTestAccess::ShiftHold(panel, 1100ms);
			(void)panel.ConsumeHoldWake(owner, std::chrono::steady_clock::now()); pointer(WM_LBUTTONUP, ellipse, false); tick(1);
			expect(pinned() && badge(ellipse).progress.val == 1, "animation-disabled badge jumps to its end state");
			UiAnimationEnabled = true;
			click(rect); tick(60);

			// 实际绘制面板，按顶边中点右/左两侧与环色的色差判断进度环是否画出（约四成进度时）。
			const auto ringContrast = [&](int item)
			{
				auto* dc = owner.specTarget.DeviceContext(); owner.spec.SetFrameZoom(zoom);
				owner.PrepareFrameLighting(1.0 / 60, {}, ResolveMainBarFrameLightingInput(&owner, static_cast<int>(StateModeSelectEnum::IdtShape), 0, GetPenColor(GetStateModeSnapshot()), 0));
				dc->BeginDraw(); dc->SetTransform(D2D1::IdentityMatrix()); dc->Clear(D2D1::ColorF(0, 0, 0, 0)); panel.Draw(owner.spec, dc);
				expect(SUCCEEDED(dc->EndDraw()), "panel draws for ring sampling");
				const auto pixels = ReadPixels(dc, owner.specTarget.TargetBitmap()); const auto r = panel.PresentationSnapshot().items[item];
				const UINT width = owner.specTarget.TargetBitmap()->GetPixelSize().width;
				const COLORREF ringColor = GetThemeColor(ThemeColorEnum::IconPrimary);
				const auto distance = [&](size_t i) { return std::abs(static_cast<int>(pixels[i]) - GetBValue(ringColor)) + std::abs(static_cast<int>(pixels[i + 1]) - GetGValue(ringColor))
					+ std::abs(static_cast<int>(pixels[i + 2]) - GetRValue(ringColor)); };
				int right = 765, left = 765;
				for (int y = static_cast<int>(r.top); y < static_cast<int>(r.top) + 10; ++y) for (int dx = 4; dx < 9; ++dx)
				{
					const size_t a = (static_cast<size_t>(y) * width + static_cast<size_t>(EraserRectCenterX(r) + dx)) * 4;
					const size_t b = (static_cast<size_t>(y) * width + static_cast<size_t>(EraserRectCenterX(r) - dx)) * 4;
					if (b + 3 < pixels.size() && a + 3 < pixels.size()) { right = (std::min)(right, distance(a)); left = (std::min)(left, distance(b)); }
				}
				return left - right;
			};

			// 正常提交：进度环在已接受的提交后定格满格。
			pointer(WM_LBUTTONDOWN, ellipse, true); BarGeometryAttributeTestAccess::ShiftHold(panel, 680ms);
			(void)panel.ConsumeHoldWake(owner, std::chrono::steady_clock::now()); tick(30);
			BarGeometryAttributeTestAccess::ShiftHold(panel, 420ms);
			expect(panel.ConsumeHoldWake(owner, std::chrono::steady_clock::now()) == std::chrono::steady_clock::time_point{}, "accepted hold commits on the deadline");
			tick(1);
			expect(pinned() && BarGeometryAttributeTestAccess::RingProgress(panel) == 1, "accepted hold freezes the ring at full progress");
			pointer(WM_LBUTTONUP, ellipse, false); tick(60);
			click(rect); tick(60); expect(!pinned(), "switching back to rectangle clears the accepted pin");

			// revision 过期：保留按下时守卫，不固定；环不定格满格，按退出时序淡出。
			{
				pointer(WM_LBUTTONDOWN, ellipse, true); BarGeometryAttributeTestAccess::ShiftHold(panel, 680ms);
				(void)panel.ConsumeHoldWake(owner, std::chrono::steady_clock::now()); tick(30);
				BarGeometryAttributeTestAccess::ShiftHold(panel, 160ms); tick(1);
				const double lastProgress = BarGeometryAttributeTestAccess::RingProgress(panel);
				expect(lastProgress > .7 && lastProgress < .95 && BarGeometryAttributeTestAccess::RingOpacity(panel) == 1, "ring is mid-progress before the stale commit");
				// 其他来源推进 revision（值不变也计一次），使按下时的 revision 过期。
				{ const auto s = GetStateModeVersionedSnapshot(); (void)SetShapeLinePatternIfRevision(GetShapeStyle(s.state).linePattern, s.revision); }
				const auto staleRevision = StateModeTransitionRevision();
				BarGeometryAttributeTestAccess::ShiftHold(panel, 260ms);
				expect(panel.ConsumeHoldWake(owner, std::chrono::steady_clock::now()) == std::chrono::steady_clock::time_point{}, "stale hold deadline is consumed once");
				expect(!pinned() && GetShapeToolSpec(GetStateModeSnapshot()).preset == PresetKind::Rectangle && StateModeTransitionRevision() == staleRevision,
					"stale revision neither selects nor pins and is not retried");
				tick(1);
				const double fading = BarGeometryAttributeTestAccess::RingOpacity(panel);
				expect(BarGeometryAttributeTestAccess::RingProgress(panel) == lastProgress && BarGeometryAttributeTestAccess::RingOpacityTarget(panel) == 0 && fading < 1,
					"rejected commit keeps the last progress instead of a full ring and starts fading");
				tick(2);
				expect(BarGeometryAttributeTestAccess::RingProgress(panel) < 1 && BarGeometryAttributeTestAccess::RingOpacity(panel) < fading, "rejected ring keeps fading out");
				pointer(WM_LBUTTONUP, ellipse, false); tick(40);
				expect(!pinned() && GetShapeToolSpec(GetStateModeSnapshot()).preset == PresetKind::Rectangle && !pointerAccepted && BarGeometryAttributeTestAccess::RingItem(panel) < 0,
					"release after a rejected hold does not short-tap and the ring is gone");
			}

			// 进度环出现后在按钮内超出移动阈值：中止，抬起既不选中也不固定。
			{
				const auto before = GetStateModeVersionedSnapshot();
				pointer(WM_LBUTTONDOWN, ellipse, true); BarGeometryAttributeTestAccess::ShiftHold(panel, 360ms);
				(void)panel.ConsumeHoldWake(owner, std::chrono::steady_clock::now()); tick(2);
				const auto fr = panel.PresentationSnapshot(); const auto r = fr.items[ellipse];
				ExMessage drift{}; drift.message = WM_MOUSEMOVE; drift.lbutton = true;
				drift.x = static_cast<short>(std::lround(EraserRectCenterX(r) * fr.zoom) + 8); drift.y = static_cast<short>(std::lround(EraserRectCenterY(r) * fr.zoom));
				panel.Pointer(owner, drift);
				expect(BarGeometryAttributeTestAccess::HoldItem(panel) < 0, "drift after the ring appeared aborts the hold");
				pointer(WM_LBUTTONUP, ellipse, false); tick(60);
				expect(SameBusinessSnapshot(before, GetStateModeVersionedSnapshot()) && !pinned() && !pointerAccepted, "drift abort after the ring neither selects nor pins on release");
			}

			// Up 在 1.0s 之后、到期唤醒之前被处理：长按恰好提交一次，不触发短按。
			{
				pointer(WM_LBUTTONDOWN, ellipse, true); BarGeometryAttributeTestAccess::ShiftHold(panel, 1050ms);
				const auto beforeUp = StateModeTransitionRevision();
				pointer(WM_LBUTTONUP, ellipse, false);
				expect(pinned() && GetShapeToolSpec(GetStateModeSnapshot()).preset == PresetKind::Ellipse && StateModeTransitionRevision() == beforeUp + 1 && !pointerAccepted,
					"up after the deadline commits the hold exactly once without a short tap");
				expect(panel.ConsumeHoldWake(owner, std::chrono::steady_clock::now()) == std::chrono::steady_clock::time_point{} && StateModeTransitionRevision() == beforeUp + 1,
					"late deadline wake does not commit again");
				tick(60); click(rect); tick(60); expect(!pinned(), "late-up pin cleared by switching category");
			}

			// 注入进度环绘制失败：同一次长按内不重试，下一次长按重新绘制。
			{
				pointer(WM_LBUTTONDOWN, rect, true); BarGeometryAttributeTestAccess::ShiftHold(panel, 520ms);
				(void)panel.ConsumeHoldWake(owner, std::chrono::steady_clock::now()); tick(30);
				expect(ringContrast(rect) > 150, "ring draws before the injected failure");
				BarGeometryAttributeTestAccess::FailRing(panel); tick(1);
				expect(ringContrast(rect) < 150 && BarGeometryAttributeTestAccess::RingFailed(panel), "failed ring is not retried within the same hold");
				pointer(WM_LBUTTONUP, rect, false); tick(60);
				expect(BarGeometryAttributeTestAccess::RingItem(panel) < 0, "aborted ring fades out");
				pointer(WM_LBUTTONDOWN, rect, true); BarGeometryAttributeTestAccess::ShiftHold(panel, 520ms);
				(void)panel.ConsumeHoldWake(owner, std::chrono::steady_clock::now()); tick(30);
				const int contrast = ringContrast(rect);
				report << "HoldRingRetry contrast=" << contrast << '\n';
				expect(!BarGeometryAttributeTestAccess::RingFailed(panel) && contrast > 150, "next long press clears the failure and draws the ring again");
				pointer(WM_LBUTTONUP, rect, false); tick(60);
				expect(GetShapeToolSpec(GetStateModeSnapshot()).preset == PresetKind::Rectangle && !pinned(), "ring retry holds have no business side effects");
			}

			// 页码加载环抽取前后逐像素一致；进度模式整圈闭合。
			{
				auto* dc = owner.specTarget.DeviceContext(); ButtonFrameRing shared;
				const COLORREF color = GetThemeColor(ThemeColorEnum::IconPrimary);
				for (const float z : {1.0F, 1.5F})
					for (const auto& arc : {PageLoadingArc{0,.10},PageLoadingArc{.37,.42},PageLoadingArc{.9,.95},PageLoadingArcAt(.8,true)})
					{
						const EraserAttributeRect r{100.25,60.5,170.25,95.5};
						dc->BeginDraw(); dc->SetTransform(D2D1::IdentityMatrix()); dc->Clear(D2D1::ColorF(0,0,0,0));
						const bool legacyOk = DrawLegacyPageRing(dc, r, 8, z, arc, color, .8F); expect(SUCCEEDED(dc->EndDraw()) && legacyOk, "legacy page ring reference draws");
						const auto legacy = ReadPixels(dc, owner.specTarget.TargetBitmap());
						dc->BeginDraw(); dc->SetTransform(D2D1::IdentityMatrix()); dc->Clear(D2D1::ColorF(0,0,0,0));
						const bool sharedOk = shared.Draw(dc, r.left, r.top, r.Width(), r.Height(), 8, static_cast<float>(ButtonFrameThicknessDip), 1, z, {arc.start, arc.length, false}, color, .8F);
						expect(SUCCEEDED(dc->EndDraw()) && sharedOk, "shared page ring draws");
						const auto current = ReadPixels(dc, owner.specTarget.TargetBitmap());
						expect(!legacy.empty() && legacy == current && std::any_of(current.begin(), current.end(), [](auto v){ return v != 0; }), "page loading ring is pixel-identical after extraction");
					}
				const EraserAttributeRect r{100,60,170,130};
				dc->BeginDraw(); dc->SetTransform(D2D1::IdentityMatrix()); dc->Clear(D2D1::ColorF(0,0,0,0));
				(void)shared.Draw(dc, r.left, r.top, r.Width(), r.Height(), 8, static_cast<float>(ButtonFrameThicknessDip), 1, 1, {0, 1, true}, color, 1);
				expect(SUCCEEDED(dc->EndDraw()), "full progress ring draws");
				const auto full = ReadPixels(dc, owner.specTarget.TargetBitmap()); const UINT width = owner.specTarget.TargetBitmap()->GetPixelSize().width;
				const auto alphaAt = [&](double x, double y) { const size_t i = (static_cast<size_t>(y) * width + static_cast<size_t>(x)) * 4 + 3; return i < full.size() ? full[i] : 0; };
				const double edge = 3.25; // 边框 + 间隙 + 半线宽
				expect(alphaAt(135, 60 + edge) > 128 && alphaAt(131, 60 + edge) > 128 && alphaAt(170 - edge, 95) > 128 && alphaAt(135, 130 - edge) > 128 && alphaAt(100 + edge, 95) > 128,
					"completed progress ring closes around the button");
			}
			caseContext.clear();
		}

		// 正式资源、三语、DPI、上下展开、主题与设备代次均通过实际面板渲染。
		int frames = 0;
		for (const wchar_t* language : {L"zh-CN", L"zh-TW", L"en-US"})
		{
			expect(I18n::load(1, L"JSON", language), "all bundled languages load"); owner.ConfigureLocalizedTypography();
			for (bool dark : {false, true}) for (double scale : {0.75, 1.0, 1.5, 2.0}) for (bool below : {false, true}) for (const auto category : {ShapeToolbarCategory::Regular,ShapeToolbarCategory::Solid,ShapeToolbarCategory::CoordinateFunction})
			{
				zoom = scale; work = {0, 0, static_cast<LONG>(1200 * zoom), static_cast<LONG>(800 * zoom)};
				owner.barStyle.zoom = zoom; owner.barStyle.darkStyle = dark; owner.barState.widgetPosition.primaryBar = below;
				root->y.SetDirect(below ? 120 : 600); UiAnimationEnabled = true;
				const auto epoch = RenderPipeline::GetDeviceEpoch();
				expect(SUCCEEDED(owner.specTarget.Ensure(epoch, work.right, work.bottom)), "DPI target resources"); owner.spec.SetFrameZoom(zoom);
				preset(below ? PresetKind::Circle : PresetKind::RegularPolygon); selectCategory(category); tick(60);
				f = owner.geometryAttribute.PresentationSnapshot();
				const double expectedContentHeight = category == ShapeToolbarCategory::Regular ? 220 : category == ShapeToolbarCategory::Solid ? 70 : 145;
				const double expectedWidth = (std::max)(455.0,f.items[GeometryAuxBegin].right+5-f.panel.left);
				report << "GeometrySize width=" << f.panel.Width() << " expected=" << expectedWidth << " height=" << f.panel.Height() << " scale=" << scale << '\n';
				// 此矩阵选择的圆/正多边形均属于常规分类；浏览其他分类不应保留其43.5 DIP参数行。
				const double expectedParameterHeight = category == ShapeToolbarCategory::Regular ? 43.5 : 0.0;
				expect(std::abs(f.panel.Width()-expectedWidth) < 0.01 && std::abs(f.panel.Height()-(expectedContentHeight+91.0+expectedParameterHeight)) < 0.01 && f.scrollMaximum == 0,"category dimensions preserve fixed header and include parameters only for the selected tool category");
				expect((f.items[below ? GeometryCircleCenter : GeometrySidesBegin].Width()>0)==(category==ShapeToolbarCategory::Regular),"parameter-row space agrees with presented circle or polygon controls");
				expect(f.visible && f.acceptsInput && f.panel.left >= 0 && f.panel.top >= 0 && f.panel.right <= 1200 && f.panel.bottom <= 800, "panel and parameters fit both expansion sides");
				for (int i = 0; i < GeometryCategoryCount; ++i)
				{
					const auto& b = BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, i);
					expect(owner.spec.MeasureText(b.name.content.GetVal(), b.name.size.val, DWRITE_FONT_WEIGHT_BOLD).width <= b.name.w.val + 0.5, "localized category label fits shared standard slot");
				}
				owner.PrepareFrameLighting(1.0 / 60, {}, ResolveMainBarFrameLightingInput(&owner, static_cast<int>(StateModeSelectEnum::IdtShape), 0, GetPenColor(GetStateModeSnapshot()), 0));
				auto* dc = owner.specTarget.DeviceContext(); dc->BeginDraw(); dc->SetTransform(D2D1::IdentityMatrix()); dc->Clear(D2D1::ColorF(0, 0, 0, 0));
				owner.geometryAttribute.Draw(owner.spec, dc); const HRESULT hr = dc->EndDraw(); expect(SUCCEEDED(hr), "production panel draw completes");
				const auto pixels = ReadPixels(dc, owner.specTarget.TargetBitmap());
				expect(!pixels.empty() && std::any_of(pixels.begin(), pixels.end(), [](auto v) { return v != 0; }), "production panel produces actual pixels");
				for (int i = 0; i < GeometryCategoryCount; ++i) if (catalog[i].category == category) expect(BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, i).icon.cacheBitmap != nullptr, "visible category rasterizes embedded SVG");
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
		owner.specTarget.Discard(); expect(SUCCEEDED(owner.specTarget.Ensure(RenderPipeline::GetDeviceEpoch(), work.right, work.bottom)), "device resources rebuild");
		UiAnimationEnabled = false; owner.barState.geometryAttribute = false; tick(1, 0);
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
		owner.specTarget.Discard();

		// 走完整生产协调器，检查新列表及原子树的实际绘制/成功呈现命中。
		owner.barWindow.x = owner.barWindow.y = 0; owner.barWindow.w = 1200; owner.barWindow.h = 800;
		(void)owner.InitializeStartupPlacement(Inkeys::UI::StartupPlacement::BottomLeftFloating);
		InitializeUI(owner); BarGeometryAttributeTestAccess::Display(owner, 1200, 800);
		owner.barStyle.dpiZoom = 1; owner.barStyle.configZoom = 1; owner.barStyle.zoom = 1;
		owner.barButtonSet.Load(); owner.barButtonSet.StateUpdate();
		{
			caseContext = "main-geometry-badge";
			auto* geometry = owner.barButtonSet.preset[static_cast<int>(BarButtonPresetEnum::Geometry)];
			expect(geometry && geometry->badge.has_value(), "main geometry button owns the optional badge");
			int otherBadges = 0;
			for (int id = 0; id < owner.barButtonSet.tot; ++id) if (auto* b = owner.barButtonSet.buttonList.Get(id); b && b != geometry && b->badge) ++otherBadges;
			expect(otherBadges == 0, "other main-bar buttons stay without badges");
			const ButtonAnimationContext frame{1.0 / 60, 1, true, false, true, true};
			const auto advance = [&](int frames) { for (int i = 0; i < frames; ++i) (void)AdvanceButtonVisual(*geometry, frame); };
			const auto savedPreset = GetShapeToolSpec(GetStateModeSnapshot()).preset;
			geometry->button.w.SetDirect(70); geometry->button.h.SetDirect(70);
			const UiInheritClass origin{200, 20};
			ChangeStateModeToShape(); (void)SetShapeToolPresetIfRevision(PresetKind::Rectangle, StateModeTransitionRevision());
			owner.barButtonSet.StateUpdate(); advance(60);
			expect(!geometry->badge->shown && geometry->badge->progress.val == 0, "unpinned rectangle has no main badge");
			(void)ToggleShapeToolPinIfRevision(PresetKind::Rectangle, StateModeTransitionRevision()); owner.barButtonSet.StateUpdate();
			expect(geometry->badge->shown, "user-pinned shape publishes the main badge target");
			advance(3);
			auto g = ResolveButtonBadgeGeometry(*geometry, origin);
			expect(g.scale > 0 && g.scale < 1.2 && g.angle < 0 && g.angle > -45, "main badge enters scaling up with rotation");
			const RECT entering = ResolveButtonBadgeBounds(*geometry, origin, 1.5);
			const double envelope = g.diameter * g.scale * 0.7071 * 1.5;
			expect(entering.right - entering.left >= static_cast<LONG>(2 * envelope) && entering.left <= static_cast<LONG>(g.centerX * 1.5 - envelope), "badge dirty bounds cover the rotated envelope");
			advance(60); g = ResolveButtonBadgeGeometry(*geometry, origin);
			expect(geometry->badge->progress.val == 1 && std::abs(g.angle) < .000001 && std::abs(g.centerX - (origin.x + 70 - 9)) < .01 && std::abs(g.centerY - (origin.y + 9)) < .01,
				"main badge settles at the top-right corner");
			// 主栏倒转：换到右下角并随换边过渡。
			geometry->badge->belowTarget = true; advance(6); g = ResolveButtonBadgeGeometry(*geometry, origin);
			expect(geometry->badge->below.val > 0 && geometry->badge->below.val < 1 && g.centerY > origin.y + 9 && g.centerY < origin.y + 61, "inverted bar moves the badge along the right edge");
			advance(60); g = ResolveButtonBadgeGeometry(*geometry, origin);
			expect(std::abs(g.centerY - (origin.y + 70 - 9)) < .01 && std::abs(g.centerX - (origin.x + 61)) < .01, "inverted bar places the badge at the bottom-right corner");
			geometry->button.h.SetDirect(32.5); g = ResolveButtonBadgeGeometry(*geometry, origin);
			expect(std::abs(g.centerY - (origin.y + 32.5 - 9)) < .01 && g.diameter == 10, "button size changes translate without rescaling the badge");
			geometry->button.h.SetDirect(70); geometry->badge->belowTarget = false; advance(60);
			// 切到其他图形清除固定，角标旋转离场。
			(void)SetShapeToolPresetIfRevision(PresetKind::Ellipse, StateModeTransitionRevision()); owner.barButtonSet.StateUpdate(); advance(9);
			g = ResolveButtonBadgeGeometry(*geometry, origin);
			expect(!geometry->badge->shown && g.scale > 0 && g.angle > 0, "switching shape hides the main badge with exit rotation");
			advance(60); expect(geometry->badge->progress.val == 0 && ResolveButtonBadgeBounds(*geometry, origin, 1).right == 0, "hidden badge leaves no dirty bounds");
			{
				// 进出场中途反向：角度沿当前值连续变化，不按新目标重算出现瞬跳。
				const auto angle = [&] { return ResolveButtonBadgeGeometry(*geometry, origin).angle; };
				const ButtonAnimationContext still{0.0, 1, true, false, true, true};
				double referenceStep = 0;
				geometry->badge->shown = true; advance(1);
				for (double previous = angle(), i = 0; i < 60; ++i) { advance(1); const double a = angle(); referenceStep = (std::max)(referenceStep, std::abs(a - previous)); previous = a; }
				geometry->badge->shown = false; advance(60);
				expect(geometry->badge->progress.val == 0 && std::abs(angle() - 45) < .000001 && referenceStep > 0, "uninterrupted badge cycle settles hidden at the exit angle");
				const auto reverse = [&](bool shown, const char* label)
				{
					const double before = angle();
					geometry->badge->shown = shown; (void)AdvanceButtonVisual(*geometry, still);
					expect(std::abs(angle() - before) < .000001, label);
					double previous = angle(), maxStep = 0, firstStep = -1;
					for (int i = 0; i < 60; ++i)
					{
						advance(1); const double a = angle(); const double step = std::abs(a - previous);
						if (firstStep < 0) firstStep = step;
						maxStep = (std::max)(maxStep, step); previous = a;
					}
					report << "BadgeReverse shown=" << shown << " before=" << before << " first=" << firstStep << " max=" << maxStep << " reference=" << referenceStep << '\n';
					// 反向距离最多 90 度（参考整段为 45 度），单帧变化不超过参考单帧步长的两倍；首帧从零速度起步。
					expect(firstStep < referenceStep && maxStep <= referenceStep * 2 + .000001, "reversed badge rotation stays within a single-frame step");
				};
				geometry->badge->shown = true; advance(4);
				expect(geometry->badge->progress.val > 0 && angle() < 0 && angle() > -45, "badge is mid-entry before reversing");
				reverse(false, "reversing mid-entry keeps the current angle");
				expect(geometry->badge->progress.val == 0 && std::abs(angle() - 45) < .000001, "reversed entry settles at the exit angle");
				geometry->badge->shown = true; advance(60); geometry->badge->shown = false; advance(4);
				expect(geometry->badge->progress.val > 0 && angle() > -10 && angle() < 45, "badge is mid-exit before reversing");
				reverse(true, "reversing mid-exit keeps the current angle instead of restarting at -45");
				expect(geometry->badge->progress.val == 1 && std::abs(angle()) < .000001, "reversed exit settles upright");
				geometry->badge->shown = false; advance(60);
			}
			(void)SetShapeToolPresetIfRevision(PresetKind::Line, StateModeTransitionRevision()); owner.barButtonSet.StateUpdate();
			expect(geometry->badge->shown, "line tool always shows the main badge");
			ChangeStateModeToPen(); owner.barButtonSet.StateUpdate();
			expect(!geometry->badge->shown, "badge only follows the selected shape tool");
			UiAnimationEnabled = false; ChangeStateModeToShape(); owner.barButtonSet.StateUpdate();
			(void)AdvanceButtonVisual(*geometry, {1.0 / 60, 1, false, false, true, true});
			expect(geometry->badge->progress.val == 1, "animation-disabled main badge jumps to the end state");
			UiAnimationEnabled = true; (void)SetShapeToolPresetIfRevision(savedPreset, StateModeTransitionRevision()); owner.barButtonSet.StateUpdate(); advance(60);
			caseContext.clear();
		}
		owner.barState.fold = false; owner.barState.geometryAttribute = true;
		owner.barState.drawAttribute = false; UiAnimationEnabled = true;
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
			const auto size = owner.specTarget.TargetBitmap()->GetPixelSize();
			const bool cropValid = receipt.source.x >= 0 && receipt.source.y >= 0 &&
				static_cast<unsigned>(receipt.source.x + receipt.size.cx) <= size.width &&
				static_cast<unsigned>(receipt.source.y + receipt.size.cy) <= size.height;
			expect(cropValid, "actual ULW source crop stays within production bitmap");
			if (!cropValid) std::cerr << "[GeometryCrop] source=" << receipt.source.x << ',' << receipt.source.y
				<< " window=" << receipt.size.cx << ',' << receipt.size.cy << " bitmap=" << size.width << ',' << size.height
				<< " commit=" << actualCommits << " captured=" << capturedCommit << " context=" << caseContext << std::endl;
			if (!cropValid) return;
			const D2D1_RECT_U crop{static_cast<UINT32>(receipt.source.x), static_cast<UINT32>(receipt.source.y),
				static_cast<UINT32>(receipt.source.x + receipt.size.cx), static_cast<UINT32>(receipt.source.y + receipt.size.cy)};
			scenePixels = ReadPixels(owner.specTarget.DeviceContext(), owner.specTarget.TargetBitmap(), &crop);
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
			const auto size = owner.specTarget.TargetBitmap()->GetPixelSize();
			const auto bytes = ReadPixels(owner.specTarget.DeviceContext(), owner.specTarget.TargetBitmap());
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
		// 自然尺寸面板允许越过屏幕；只要求仍在屏幕中的部分进入ULW源裁剪。
		auto onScreen = [&](EraserAttributeRect crop) {
			const auto mapping=owner.BottomDockPresentedSnapshot();
			return Intersect(crop,{double(mapping.workArea.left-receipt.destination.x),double(mapping.workArea.top-receipt.destination.y),double(mapping.workArea.right-receipt.destination.x),double(mapping.workArea.bottom-receipt.destination.y)});
		};
		auto fullyOnScreen = [&](EraserAttributeRect crop) { const auto visible=onScreen(crop); return std::abs(visible.Width()-crop.Width())<.01 && std::abs(visible.Height()-crop.Height())<.01; };
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
				if (catalog[i].preset || catalog[i].category != presented.category) continue;
				const auto& button = BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, i);
				const EraserAttributeRect image{button.icon.inhX, button.icon.inhY, button.icon.inhX + button.icon.w.val, button.icon.inhY + button.icon.h.val};
				const auto clipped = Intersect(image, presented.categoryClip);
				if (clipped.Width() < image.Width() - 0.01 || clipped.Height() < image.Height() - 0.01) continue;
				expect(button.icon.cacheBitmap != nullptr && !button.icon.svg.GetVal().empty() && FindResourceW(nullptr, ShapeCategoryCatalog()[i].resource, L"UI"),
					"visible future category rasterizes its own formally embedded SVG");
				const auto icon = clientRect(image, presented), cell = clientRect(presented.items[i], presented);
				if (!fullyOnScreen(cell)) continue;
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
				const auto crop = onScreen(clientRect(rect, presented));
				if(crop.Width()>0 && crop.Height()>0) expect(crop.left >= -1 && crop.top >= -1 && crop.right <= receipt.size.cx + 1 && crop.bottom <= receipt.size.cy + 1,
					"on-screen attribute portion fits successfully emitted viewport crop");
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
			const auto panel = onScreen(clientRect(presented.panel, presented));
			if(panel.left < -1 || panel.top < -1 || panel.right > receipt.size.cx+1 || panel.bottom > receipt.size.cy+1)
				report << "PanelCropDiagnostic panel=" << panel.left << "," << panel.top << "," << panel.right << "," << panel.bottom
					<< " receipt=" << receipt.destination.x << "," << receipt.destination.y << "," << receipt.size.cx << "," << receipt.size.cy
					<< " zoom=" << presented.zoom << " pose=" << presented.pose.scale << " commits=" << actualCommits << " [" << caseContext << "]\n";
			expect(panel.left >= -1 && panel.top >= -1 && panel.right <= receipt.size.cx + 1 && panel.bottom <= receipt.size.cy + 1,
				"on-screen production panel lies within its committed ULW crop");
			expect(presented.pose.scale > .99, "settled panel keeps natural scale even when its content extends beyond the work area");
			const bool closed = GetToolCapabilities(wanted).fill;
			const bool stroke = !closed || fill != draw3::shapes::FillMode::Fill;
			expect(presented.categoryViewportHeight > 0 && presented.categoryContentHeight >= presented.categoryViewportHeight - 0.01 &&
				presented.scrollOffset >= -0.01 && presented.scrollOffset <= presented.scrollMaximum + 0.01, "category viewport and offset obey actual production scroll limits");
			expect(presented.categoryClip.left >= presented.panel.left && presented.categoryClip.top >= presented.panel.top &&
				presented.categoryClip.right <= presented.panel.right && presented.categoryClip.bottom <= presented.panel.bottom, "category clip remains inside panel while fixed controls stay outside its scroll content");
			for (int i = 0; i < GeometryControlCount; ++i)
			{
				const bool auxiliaryOption = i >= GeometryAuxBegin && i < GeometryTriangleModeBegin;
				const bool constraintOption = i >= GeometryTriangleModeBegin && i <= GeometryTrapezoidMode;
				const std::array<Auxiliary,3> bits{Auxiliary::Center,Auxiliary::Foci,Auxiliary::Diagonals};
				const bool matchingCategory = std::any_of(catalog.begin(),catalog.end(),[&](const auto& entry){return entry.preset==wanted && entry.category==presented.category;});
				const bool shown = (i < GeometryCategoryCount && catalog[i].category == presented.category) || i == GeometryCloseButton || i >= GeometrySegmentBegin
					|| ((i == GeometryCircleCenter || i == GeometryCircleBox) && matchingCategory && wanted == PresetKind::Circle)
					|| (i >= GeometrySidesBegin && i < GeometrySidesBegin+(wanted == PresetKind::RegularPolygon ? GeometrySidesCount : 6) && matchingCategory && GetToolCapabilities(wanted).regularSides)
					|| (auxiliaryOption && matchingCategory && stroke && (SupportedAuxiliary(wanted) & bits[i-GeometryAuxBegin]) != Auxiliary::None)
					|| (constraintOption && matchingCategory && ((wanted == PresetKind::Triangle && i < GeometryTrapezoidMode) || ((wanted == PresetKind::Trapezoid || wanted == PresetKind::Prism || wanted == PresetKind::Pyramid) && i == GeometryTrapezoidMode)))
					|| (i >= GeometryStyleBegin && i < GeometryStyleBegin+2 && closed) || (i >= GeometryPatternBegin && i < GeometryCloseButton && stroke);
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
					const auto& word=BarGeometryAttributeTestAccess::Button(owner.geometryAttribute,i).name;
					if(word.enable.val&&word.fontTargetSize>=0)expect(std::abs(word.fontTargetSize-word.size.val)<.00001,
						"settled shape text target matches final fitted font size in the production scene");
					const auto emitted = onScreen(clientRect(visibleRect, presented));
					if(emitted.Width()>0 && emitted.Height()>0) expect(emitted.left >= -1 && emitted.top >= -1 && emitted.right <= receipt.size.cx + 1 && emitted.bottom <= receipt.size.cy + 1,
						"all on-screen shape control pixels survive complete production crop");
					if (i >= GeometryCircleCenter && i < GeometryStyleBegin)
						expect(rect.top >= presented.parameterClip.top - 0.01 && rect.bottom <= presented.parameterClip.bottom + 0.01,
							"stable parameter buttons fit the exact clip used by production draw");
					const bool actionable = i >= GeometryCategoryCount || catalog[i].preset.has_value();
					expect(hit.enabled == actionable && HitTestButton(hit, EraserRectCenterX(visibleRect), EraserRectCenterY(visibleRect)) == actionable,
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
						expect(button.state->state == WidgetState::Disable && std::abs(button.icon.pct.val - disabledOpacity()) < 0.000001 && std::abs(button.name.pct.val - disabledOpacity()) < 0.000001,
							"disabled future category retains shared theme opacity without becoming selected");
				}
				if (i >= GeometryPatternBegin && i < GeometryCloseButton)
				{
					const auto target = clientRect(rect, presented); if (!fullyOnScreen(target)) continue; unsigned ink = 0;
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
		captureScene = true; UiAnimationEnabled = false; owner.geometryAttribute.ResetPointerFeedback();
		unsigned sceneCases = 0;
		for (const wchar_t* language : {L"zh-CN", L"zh-TW", L"en-US"})
		{
			expect(I18n::load(1, L"JSON", language), "production matrix language loads"); owner.ConfigureLocalizedTypography();
			for (double scale : {0.75, 1.0, 1.5, 2.0}) for (bool smallWork : {false, true}) for (bool below : {false, true}) for (bool dark : {false,true})
			{
				const LONG width = smallWork ? 460 : 1200, height = smallWork ? 420 : 800;
				owner.barStyle.darkStyle = dark; owner.barStyle.configZoom = 1;
				BarGeometryAttributeTestAccess::Display(owner, static_cast<LONG>(width * scale), static_cast<LONG>(height * scale), static_cast<UINT>(96 * scale));
				sceneTick(2);
				BarGeometryAttributeTestAccess::MoveRoot(owner, 80, below ? 80 : height - 80); sceneTick(2);
				expect(static_cast<bool>(owner.barState.widgetPosition.primaryBar) == below, "actual main-bar position chooses intended expansion side");
				for (const auto& entry : catalog) if (entry.preset) for (int fill = 0; fill < 3; ++fill)
				{
					const int shape = static_cast<int>(*entry.preset);
					caseContext = "scene=" + std::to_string(++sceneCases) + " shape=" + std::to_string(shape) + " fill=" + std::to_string(fill) +
						" scale=" + std::to_string(scale) + " small=" + std::to_string(smallWork) + " below=" + std::to_string(below);
					const auto tool = GetStateModeVersionedSnapshot(); SetShapeToolPresetIfRevision(static_cast<PresetKind>(shape), tool.revision);
					const auto style = GetStateModeVersionedSnapshot(); SetShapeFillModeIfRevision(static_cast<draw3::shapes::FillMode>(fill), style.revision);
					owner.barState.fold = false; owner.barState.geometryAttribute = true;
					BarGeometryAttributeTestAccess::Execute(owner.geometryAttribute,owner,GeometrySegmentBegin+static_cast<int>(entry.category),StateModeTransitionRevision());
					owner.UpdateRendering(); sceneTick(2);
					checkStableScene(static_cast<PresetKind>(shape), static_cast<draw3::shapes::FillMode>(fill));
					if (language == std::wstring(L"zh-CN") && scale == 1 && shape == static_cast<int>(PresetKind::Circle) && !below && fill == 2)
						expect(SavePng(smallWork ? L"Build/shape-p2-ui/scene-shape-small.png" : L"Build/shape-p2-ui/scene-shape-complete.png",
							scenePixels, receipt.size.cx, receipt.size.cy), "actual cropped shape scene writes pixel evidence");
				}
				(void)SetShapeToolPresetIfRevision(PresetKind::Line,StateModeTransitionRevision());
				for (const auto category : {ShapeToolbarCategory::Regular,ShapeToolbarCategory::Solid,ShapeToolbarCategory::CoordinateFunction})
				{
					BarGeometryAttributeTestAccess::Execute(owner.geometryAttribute,owner,GeometrySegmentBegin+static_cast<int>(category),StateModeTransitionRevision()); sceneTick(2);
					checkStableScene(PresetKind::Line,draw3::shapes::FillMode::Outline);
					const auto current = owner.geometryAttribute.PresentationSnapshot();
					const double contentHeight = category == ShapeToolbarCategory::Regular ? 220 : category == ShapeToolbarCategory::Solid ? 70 : 145;
					expect(current.category == category && current.categoryContentHeight == contentHeight,"all three category grids have the approved row count in every theme DPI and work area");
					if (!smallWork) expect(std::abs(current.panel.Width()-455.0) < 0.01 && std::abs(current.panel.Height()-(contentHeight+91)) < 0.01 && current.scrollMaximum == 0,"default category panel size meets exact acceptance bounds without a parameter row");
				}
			}
		}
		for (int i = 0; i < GeometryCategoryCount; ++i) if (!catalog[i].preset) expect(futurePixelCases[i] > 0, "all fifteen future resources have real emitted pixel evidence");
		report << "Complete production scene configurations=" << sceneCases << " AA pixel cases=" << antialiasAccepted << '\n'; caseContext = "animated attributes";
		const auto attributes = BarGeometryAttributeTestAccess::Attributes(owner.geometryAttribute);
		expect(attributes.size() == 11 && attributes[2]->preset == BarButtonPresetEnum::Divider && attributes[7]->preset == BarButtonPresetEnum::Divider,
			"attributes retain two style entries with synchronized divider slots");
		expect(std::find_if(attributes.begin(),attributes.end(),[&](const auto& item)
		{ return item.get() == &BarGeometryAttributeTestAccess::Button(owner.geometryAttribute,GeometryStyleBegin+2); }) == attributes.end(),
			"legacy mixed mode has no attribute list entry");
		BarGeometryAttributeTestAccess::Display(owner, 1200, 800); sceneTick(2);
		BarGeometryAttributeTestAccess::MoveRoot(owner, 80, 650); sceneTick(2);
		owner.barStyle.darkStyle = false;
		auto selectScene = [&](PresetKind p, draw3::shapes::FillMode fill)
		{
			const auto tool = GetStateModeVersionedSnapshot(); SetShapeToolPresetIfRevision(p, tool.revision);
			const auto style = GetStateModeVersionedSnapshot(); SetShapeFillModeIfRevision(fill, style.revision);
			owner.barState.fold = false; owner.barState.geometryAttribute = true;
			for (const auto& entry : catalog) if (entry.preset == p) BarGeometryAttributeTestAccess::Execute(owner.geometryAttribute,owner,GeometrySegmentBegin+static_cast<int>(entry.category),StateModeTransitionRevision());
			owner.UpdateRendering();
		};
		auto panelPoint = [&](USHORT type, double x, double y, bool held = false, bool contact = false, short wheel = 0)
		{
			const auto current = owner.geometryAttribute.PresentationSnapshot(); ExMessage message{};
			message.message=type; message.lbutton=held; message.x=static_cast<short>(std::lround(x*current.zoom)); message.y=static_cast<short>(std::lround(y*current.zoom)); message.wheel=wheel;
			return owner.geometryAttribute.Pointer(owner,message,false,contact);
		};
		// 屏幕不足仅改变展开方向；内容高度、按钮尺度和完整类别不再压缩。
		caseContext = "natural size on restricted workspace";
		selectScene(PresetKind::Circle, draw3::shapes::FillMode::Outline); sceneTick(2);
		const auto spacious = owner.geometryAttribute.PresentationSnapshot();
		BarGeometryAttributeTestAccess::Display(owner,460,420); sceneTick(2); BarGeometryAttributeTestAccess::MoveRoot(owner,80,340); sceneTick(2);
		const auto restricted = owner.geometryAttribute.PresentationSnapshot();
		expect(restricted.scrollMaximum == 0 && std::abs(restricted.categoryViewportHeight-restricted.categoryContentHeight)<.01,"restricted workspace retains the complete category rather than introducing scrolling");
		expect(std::abs(restricted.panel.Width()-spacious.panel.Width())<.01 && std::abs(restricted.panel.Height()-spacious.panel.Height())<.01,"changing screen space does not resize shape content");
		expect(std::abs(restricted.pose.scale-spacious.pose.scale)<.01,"restricted workspace does not shrink buttons or fonts");
		for (int i=0;i<GeometryCategoryCount;++i) if (catalog[i].preset && catalog[i].category==restricted.category)
			expect(restricted.items[i].top>=restricted.categoryClip.top-.01 && restricted.items[i].bottom<=restricted.categoryClip.bottom+.01,"all implemented tools remain inside the full content viewport");
		BarGeometryAttributeTestAccess::Display(owner,1200,800); sceneTick(2); BarGeometryAttributeTestAccess::MoveRoot(owner,80,650); sceneTick(2);
		caseContext = "animated attributes";
		// 淡出像素专项留足上下空间，避免把允许出屏的部分当作透明度失败。
		BarGeometryAttributeTestAccess::Display(owner,1600,1200); sceneTick(2); BarGeometryAttributeTestAccess::MoveRoot(owner,400,600); sceneTick(2);
		selectScene(PresetKind::Rectangle, draw3::shapes::FillMode::Outline); sceneTick(2);
		auto previewContrast = [&](int item, const GeometryAttributePresentation& current)
		{
			if (current.items[item].Width() <= 0) return 0.0;
			const auto rect = clientRect(current.items[item], current);
			const auto ink = pixel(EraserRectCenterX(rect), EraserRectCenterY(rect));
			const auto background = pixel(rect.left + rect.Width() * 0.75, rect.top + rect.Height() * 0.75);
			const double contrast = static_cast<double>(std::abs(static_cast<int>(ink[0]) - background[0]) +
				std::abs(static_cast<int>(ink[1]) - background[1]) + std::abs(static_cast<int>(ink[2]) - background[2]));
			report << "FADE_SAMPLE contrast=" << contrast << " opacity=" << BarGeometryAttributeTestAccess::Button(owner.geometryAttribute,item).name.pct.val
				<< " rect=" << rect.left << "," << rect.top << "," << rect.right << "," << rect.bottom << " receipt=" << receipt.size.cx << "," << receipt.size.cy
				<< " ink=" << int(ink[0]) << "," << int(ink[1]) << "," << int(ink[2]) << "," << int(ink[3])
				<< " background=" << int(background[0]) << "," << int(background[1]) << "," << int(background[2]) << "," << int(background[3]) << '\n';
			return contrast;
		};
		auto animateAttributes = [&](PresetKind nextShape, draw3::shapes::FillMode nextFill)
		{
			const auto start = owner.geometryAttribute.PresentationSnapshot();
			std::array<double,10> startOpacity{};
			for (int i = 0; i < 10; ++i) startOpacity[i] = BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, GeometryStyleBegin + i).name.pct.val;
			const double firstContrast = previewContrast(GeometryThicknessBegin + 1, start);
			const bool thicknessOnScreen = fullyOnScreen(clientRect(start.items[GeometryThicknessBegin + 1],start));
			std::array<std::vector<double>,10> positions, opacities;
			std::vector<double> partialThicknessContrast;
			UiAnimationEnabled = true; selectScene(nextShape, nextFill);
			for (int frame = 0; frame < 42; ++frame)
			{
				sceneTick(1); const auto current = owner.geometryAttribute.PresentationSnapshot(); rowGeometry(current);
				if (frame == 1)
				{
					const int item = current.items[GeometryPatternBegin].Width() > 0 ? GeometryPatternBegin : GeometryStyleBegin;
					const auto rect = current.items[item]; const auto before = GetStateModeVersionedSnapshot();
					ExMessage down{}; down.message = WM_LBUTTONDOWN; down.lbutton = true;
					down.x = static_cast<short>(std::lround(EraserRectCenterX(rect) * current.zoom));
					down.y = static_cast<short>(std::lround(EraserRectCenterY(rect) * current.zoom));
					owner.geometryAttribute.Pointer(owner, down);
					ExMessage up = down; up.message = WM_LBUTTONUP; up.lbutton = false; owner.geometryAttribute.Pointer(owner, up);
					expect(StateModeTransitionRevision() == before.revision, "intermediate attribute reflow Down Up cannot mutate tool or style");
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
			// 小工作区允许底行超屏；像素淡出证据只取实际在屏内的起始行，动画值检查仍覆盖全部场景。
			if (thicknessOnScreen && startOpacity[8] > 0.99 && BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, GeometryThicknessBegin + 1).name.pct.val < 0.01)
				expect((report<<"FADE_DIAGNOSTIC first="<<firstContrast<<" samples="<<partialThicknessContrast.size()<<" values=", [&]{for(auto v:partialThicknessContrast)report<<v<<","; report<<"\n";return true;}()) && firstContrast > 10 && std::any_of(partialThicknessContrast.begin(), partialThicknessContrast.end(), [&](double v) { return v > 1 && v < firstContrast * 0.9; }),
					"actual thickness content fades in emitted pixels before leaving the list");
			checkStableScene(nextShape, nextFill);
		};
		// 矩形已有辅助参数行：切到圆时面板及属性位置不变，只有参数内容淡入退场。
		const auto rectangleRows = owner.geometryAttribute.PresentationSnapshot();
		UiAnimationEnabled = true; selectScene(PresetKind::Circle,draw3::shapes::FillMode::Outline); sceneTick(2);
		const auto fadingParameters = owner.geometryAttribute.PresentationSnapshot();
		expect(fadingParameters.acceptsInput && fadingParameters.panel == rectangleRows.panel
			&& fadingParameters.items[GeometryPatternBegin] == rectangleRows.items[GeometryPatternBegin],
			"rectangle to circle regression keeps panel height and attribute rectangles unchanged");
		const auto& incomingParameter = BarGeometryAttributeTestAccess::Button(owner.geometryAttribute,GeometryCircleCenter).icon.pct;
		const auto& retiringAuxiliary = BarGeometryAttributeTestAccess::Button(owner.geometryAttribute,GeometryAuxBegin+2).icon.pct;
		expect(incomingParameter.val != incomingParameter.tar && retiringAuxiliary.val != retiringAuxiliary.tar,
			"same height regression includes incoming construction and retiring auxiliary content");
		const auto beforeParameterInput = GetStateModeVersionedSnapshot();
		for (int item = GeometryCircleCenter; item < GeometryCloseButton; ++item) if (fadingParameters.hits[item].visible)
		{
			expect(!fadingParameters.hits[item].enabled, "parameter fade gates both parameter and attribute rows despite unchanged geometry");
			for (bool contact : {false,true})
			{
				const auto rect = fadingParameters.items[item];
				panelPoint(WM_LBUTTONDOWN,EraserRectCenterX(rect),EraserRectCenterY(rect),true,contact);
				panelPoint(WM_LBUTTONUP,EraserRectCenterX(rect),EraserRectCenterY(rect),false,contact);
			}
			expect(SameBusinessSnapshot(beforeParameterInput,GetStateModeVersionedSnapshot()), "same height parameter fade rejects mouse and contact without any business mutation");
		}
		expect(fadingParameters.categoriesAcceptInput && fadingParameters.hits[0].enabled, "stationary categories retain row animation redirection eligibility");
		sceneTick(90); checkStableScene(PresetKind::Circle,draw3::shapes::FillMode::Outline);
		UiAnimationEnabled = false; selectScene(PresetKind::Rectangle,draw3::shapes::FillMode::Outline); sceneTick(2);
		animateAttributes(PresetKind::Line, draw3::shapes::FillMode::Outline);
		animateAttributes(PresetKind::Rectangle, draw3::shapes::FillMode::Outline);
		animateAttributes(PresetKind::Circle, draw3::shapes::FillMode::OutlineAndFill);
		animateAttributes(PresetKind::Circle, draw3::shapes::FillMode::Fill);
		animateAttributes(PresetKind::Circle, draw3::shapes::FillMode::Outline);
		UiAnimationEnabled = false; BarGeometryAttributeTestAccess::Display(owner, 460, 420); sceneTick(2);
		BarGeometryAttributeTestAccess::MoveRoot(owner, 80, 80); sceneTick(2);
		animateAttributes(PresetKind::RegularPolygon, draw3::shapes::FillMode::Fill);
		animateAttributes(PresetKind::Rectangle, draw3::shapes::FillMode::OutlineAndFill);
		// 提交失败时实际动画值可以前进，交互只能继续使用上次成功呈现的位置。
		caseContext = "failed presentation and input"; UiAnimationEnabled = false;
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
		owner.specTarget.Discard(); previousPixels.clear(); sceneTick(2); checkStableScene(PresetKind::Circle, draw3::shapes::FillMode::Outline);
		for (int item = 0; item < GeometryCategoryCount; ++item)
			if (owner.geometryAttribute.PresentationSnapshot().hits[item].visible)
				expect(BarGeometryAttributeTestAccess::Button(owner.geometryAttribute, item).icon.cacheBitmap != nullptr,
					"visible production resources rasterize after device cache rebuild");
		owner.geometryAttribute.ResetPointerFeedback(); sceneTick(360);
		const unsigned settledCommits = actualCommits; sceneTick(20, false);
		expect(actualCommits == settledCommits && sceneResult == RenderPipeline::FrameResult::Idle && !owner.geometryAttribute.Active(),
			"actual settled scene sleeps with no animation-disabled carousel or repeated presentation");
		if (auto* geometry = owner.barButtonSet.preset[static_cast<int>(BarButtonPresetEnum::Geometry)]; geometry && geometry->badge)
			expect(static_cast<bool>(geometry->badge->belowTarget) == static_cast<bool>(owner.barState.widgetPosition.primaryBar), "render loop publishes the inverted side to the main badge");
		owner.geometryAttribute.Close(owner); sceneTick(2); const unsigned hiddenCommits = actualCommits; sceneTick(20, false);
		expect(actualCommits == hiddenCommits && !owner.geometryAttribute.PresentationSnapshot().visible && !BarGeometryAttributeTestAccess::CarouselArmed(owner.geometryAttribute),
			"hidden actual scene has no residual shape-list animation or timer wake");
		captureScene = false; previousPixels.clear(); caseContext.clear();
		BarGeometryAttributeTestAccess::Display(owner, 1200, 800); sceneTick(2); BarGeometryAttributeTestAccess::MoveRoot(owner, 80, 650); sceneTick(2);
		UiAnimationEnabled = true;
		// 固定70DIP主栏槽位独立于展开面板：全部类别在三语下都不能被NO_WRAP+CLIP截断。
		ChangeStateModeToShape(); owner.barState.fold = false; owner.barState.geometryAttribute = false;
		for (const wchar_t* language : {L"zh-CN", L"zh-TW", L"en-US"})
		{
			expect(I18n::load(1, L"JSON", language), "main geometry label language loads"); owner.ConfigureLocalizedTypography();
			for (const auto& entry : catalog) if (entry.preset)
			{
				const int i = static_cast<int>(*entry.preset);
				const auto s = GetStateModeVersionedSnapshot(); SetShapeToolPresetIfRevision(*entry.preset, s.revision);
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
					"all twenty localized main labels fit original slot");
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
			const auto checkWord=[&](const UiWordClass& word)
			{
				expect(word.size.val>0&&word.w.val>0&&word.h.val>0&&!word.content.GetVal().empty(),"construction prompt uses a real positive-size localized word slot");
				const EraserAttributeRect slot{word.inhX,word.inhY,word.inhX+word.w.val,word.inhY+word.h.val};
				const auto evidence=ink(clientRect(slot,coordinates),word.color.val);
				expect(evidence.first>=3&&evidence.second>.35,"actual construction prompt label or action has foreground text pixels beyond its surface");
				const EraserAttributeRect blank{panel.left+6*frame.zoom,panel.top+2*frame.zoom,panel.right-6*frame.zoom,panel.top+4*frame.zoom};
				const auto negative=ink(blank,word.color.val);expect(negative.first==0,"construction text oracle rejects actual blank panel padding");
			};
			checkWord(BarGeometryAttributeTestAccess::PromptLabel(owner.constructionPrompt));
			if(frame.ring)checkWord(BarGeometryAttributeTestAccess::PromptRingLabel(owner.constructionPrompt));
			for(int i=0;i<4;++i)if(frame.buttons[i].visible)
			{
				const auto& icon=BarGeometryAttributeTestAccess::PromptButton(owner.constructionPrompt,i).icon;
				expect(icon.w.val>0&&icon.h.val>0&&!icon.svg.GetVal().empty(),"prompt actions have real standard OneOne SVG slots");
				const EraserAttributeRect slot{icon.inhX,icon.inhY,icon.inhX+icon.w.val,icon.inhY+icon.h.val};
				const auto evidence=ink(clientRect(slot,coordinates),GetThemeColor(ThemeColorEnum::TextPrimary));
				expect(evidence.first>=3&&evidence.second>(frame.buttons[i].enabled?.35:.12),"actual prompt icon pixels include enabled and disabled content");
				expect(!frame.accessibleNames[i].empty()&&!frame.tooltips[i].empty(),"each action publishes localized accessible name and tooltip");
			}
			std::filesystem::create_directories(L"Build/shape-p34-ui");
			const auto path=std::filesystem::path(L"Build/shape-p34-ui")/filename;
			expect(SavePng(path,scenePixels,receipt.size.cx,receipt.size.cy),"construction prompt PNG uses actual emitted production ULW pixels");
			std::cout<<"Construction screenshot: "<<std::filesystem::absolute(path).string()<<'\n';
		};
		captureScene=true;
		// 构造提示读取完整生产场景：深浅主题、两档倍率及真实成功呈现几何。
		owner.barState.fold=false;owner.barState.geometryAttribute=false;
		expect(I18n::load(1,L"JSON",L"zh-CN"),"prompt base language loads");owner.ConfigureLocalizedTypography();
		unsigned promptSession=80;
		const auto publishPrompt=[&](const std::shared_ptr<Inkeys::Drawing::Draw3::ShapeUiSnapshot>& snapshot)
		{Inkeys::Drawing::Draw3::PublishShapeUiSnapshot(snapshot);owner.UpdateRendering(false);};
		const auto promptScreenCenter=[&](const auto& rect,double frameZoom)
		{
			GeometryAttributePresentation coordinates;coordinates.zoom=frameZoom;
			const auto client=clientRect({rect.left,rect.top,rect.right,rect.bottom},coordinates);
			return POINT{static_cast<LONG>(std::lround(EraserRectCenterX(client)+receipt.destination.x)),
				static_cast<LONG>(std::lround(EraserRectCenterY(client)+receipt.destination.y))};
		};
		const auto checkPromptTouch=[&]()
		{
			using Inkeys::Drawing::Draw3::ShapeUiAction;
			using Inkeys::Drawing::Draw3::ShapeUiActionKind;
			const auto frame=BarGeometryAttributeTestAccess::PromptFrame(owner.constructionPrompt);
			const std::array actions{ShapeUiActionKind::ConstructionBack,ShapeUiActionKind::ConstructionForward,
				ShapeUiActionKind::ConstructionComplete,ShapeUiActionKind::ConstructionCancel};
			std::vector<ShapeUiAction> intents;
			BarGeometryAttributeTestAccess::PromptSubmit(owner.constructionPrompt,[&](const ShapeUiAction& action){intents.push_back(action);return true;});
			struct ResetSubmit { BarConstructionPrompt& prompt; ~ResetSubmit(){BarGeometryAttributeTestAccess::PromptSubmit(prompt,{});} } reset{owner.constructionPrompt};
			unsigned checked=0;
			for(size_t i=0;i<frame.buttons.size();++i)
			{
				const auto& button=frame.buttons[i];if(!button.visible)continue;
				const auto& r=button.boundsDip;
				const auto screen=promptScreenCenter(r,frame.zoom);
				const bool enabled=frame.input&&button.enabled;
				const auto count=intents.size();
				// 触摸与鼠标同准入；禁用按钮由提示自身的命中逻辑忽略，不再依赖原生名单阻挡。
				const bool routed=RunConstructionPromptTouchRouteTest(screen);
				if(enabled)
					expect(routed&&intents.size()==count+1&&intents.back().kind==actions[i]&&intents.back().identity==frame.identity,
						"each enabled prompt touch emits exactly one intent with the presented action and identity");
				else expect(!routed&&intents.size()==count,"disabled prompt button ignores touch like mouse");
				++checked;
			}
			expect(checked>0,"touch admission checks visible construction prompt buttons");
		};
		const auto screenshot=[&](const std::wstring& filename)
		{
			std::filesystem::create_directories(L"Build/shape-p34-ui");
			const auto path=std::filesystem::path(L"Build/shape-p34-ui")/filename;
			expect(SavePng(path,scenePixels,receipt.size.cx,receipt.size.cy),"prompt motion screenshot writes actual committed pixels");
			std::cout<<"Construction screenshot: "<<std::filesystem::absolute(path).string()<<'\n';
		};
		// 首次呈现失败后重试，继续验证成功呈现提示的触摸行为。
		Inkeys::Drawing::Draw3::PublishShapeUiSnapshot({});sceneTick(90);UiAnimationEnabled=false;
		{
			auto construction=std::make_shared<Inkeys::Drawing::Draw3::ShapeUiSnapshot>();
			construction->identity.session=++promptSession;construction->construction.active=true;construction->construction.preset=PresetKind::Arc;
			construction->construction.step=1;construction->construction.totalSteps=2;publishPrompt(construction);
			rejectedPresent=false;rejectNextPresent=true;sceneTick(1);
			expect(rejectedPresent&&owner.constructionPrompt.Visible(),"failed first presentation keeps the candidate prompt visible");
			previousPixels.clear();sceneTick(90);
			checkPromptTouch();
		}
		Inkeys::Drawing::Draw3::PublishShapeUiSnapshot({});sceneTick(2);UiAnimationEnabled=true;
		for(const bool dark:{true,false})for(const double scale:{1.,2.})
		{
			Inkeys::Drawing::Draw3::PublishShapeUiSnapshot({});sceneTick(90);
			owner.barStyle.darkStyle=dark;owner.barStyle.configZoom=1;
			BarGeometryAttributeTestAccess::Display(owner,static_cast<LONG>(1200*scale),static_cast<LONG>(800*scale),static_cast<UINT>(96*scale));
			sceneTick(90);BarGeometryAttributeTestAccess::MoveRoot(owner,400,650);sceneTick(90);
			const std::wstring suffix=std::wstring(dark?L"dark-":L"light-")+(scale==1?L"100":L"200")+L".png";
			auto construction=std::make_shared<Inkeys::Drawing::Draw3::ShapeUiSnapshot>();
			construction->identity.session=++promptSession;construction->identity.generation=1;
			construction->construction.active=true;construction->construction.preset=PresetKind::Arc;
			construction->construction.step=1;construction->construction.totalSteps=2;
			publishPrompt(construction);
			bool rebound=false,intermediate=false;
			for(int sample=0;sample<50;++sample)
			{
				sceneTick(1);const auto frame=BarGeometryAttributeTestAccess::PromptFrame(owner.constructionPrompt);
				rebound|=frame.scale>1.0001;intermediate|=frame.scale>0.01&&frame.scale<.99;
				expect(std::abs(frame.opacity-(std::clamp)(frame.scale,0.,1.))<.000001,"appear opacity clamps Back overshoot while scale retains rebound");
				if(sample==10)screenshot(L"construction-appear-"+suffix);
			}
			expect(rebound&&intermediate,"appearance traverses rebound and intermediate scale");sceneTick(90);
			auto frame=BarGeometryAttributeTestAccess::PromptFrame(owner.constructionPrompt);
			expect(frame.visible&&frame.input&&frame.ring&&std::abs(frame.panel.Height()-42.5)<.01,"fixed-step prompt keeps final height and visible ring");
			expect(!frame.buttons[0].enabled&&!frame.buttons[1].enabled&&!frame.buttons[2].visible&&frame.buttons[3].enabled,"arc first step exposes disabled Back/Forward and enabled Cancel");
			checkPromptTouch();
			expect(BarGeometryAttributeTestAccess::PromptRingLabel(owner.constructionPrompt).content.GetVal()==L"1/2"&&BarGeometryAttributeTestAccess::PromptRingProgress(owner.constructionPrompt)==0,"first-step ring shows 1/2 with empty progress");
			const auto& surface=BarGeometryAttributeTestAccess::PromptSurface(owner.constructionPrompt);
			expect(surface.themeSurface&&surface.residentSurface&&surface.framePrimaryLightEnabled&&surface.frameCursorLightIntensityScale==1.,"prompt uses resident material and complete primary/cursor lighting");
			checkPromptPixels((L"construction-arc-1-"+suffix).c_str());
			construction=std::make_shared<Inkeys::Drawing::Draw3::ShapeUiSnapshot>(*construction);++construction->identity.generation;
			construction->construction.step=2;construction->construction.confirmedSteps=1;construction->construction.canBack=true;publishPrompt(construction);sceneTick(120);
			checkPromptTouch();
			expect(BarGeometryAttributeTestAccess::PromptRingLabel(owner.constructionPrompt).content.GetVal()==L"2/2"&&std::abs(BarGeometryAttributeTestAccess::PromptRingProgress(owner.constructionPrompt)-.5)<.000001,"second-step ring shows 2/2 and half completed");
			checkPromptPixels((L"construction-arc-2-"+suffix).c_str());
			construction=std::make_shared<Inkeys::Drawing::Draw3::ShapeUiSnapshot>(*construction);++construction->identity.generation;
			construction->construction.active=false;construction->construction.finished=true;
			construction->completedConstructionSession=construction->identity.session;++construction->identity.session;publishPrompt(construction);
			bool filledDuringClose=false,fullWhileVisible=false;
			for(int sample=0;sample<45;++sample)
			{
				sceneTick(1);const auto closing=BarGeometryAttributeTestAccess::PromptFrame(owner.constructionPrompt);
				const double value=BarGeometryAttributeTestAccess::PromptRingProgress(owner.constructionPrompt);
				if(value>.51&&value<1)filledDuringClose|=closing.visible;
				fullWhileVisible|=value==1&&closing.visible;
				if(sample<12)expect(closing.visible,"completed ring remains visible while the panel contracts");
			}
			expect(filledDuringClose&&fullWhileVisible&&std::abs(BarGeometryAttributeTestAccess::PromptRingProgress(owner.constructionPrompt)-1)<.000001,"normal completion fills the ring from one half during contraction");
			sceneTick(60);
			construction=std::make_shared<Inkeys::Drawing::Draw3::ShapeUiSnapshot>(*construction);++construction->identity.generation;
			construction->construction.active=true;construction->construction.finished=false;publishPrompt(construction);sceneTick(60);
			construction=std::make_shared<Inkeys::Drawing::Draw3::ShapeUiSnapshot>(*construction);++construction->identity.generation;
			construction->construction.active=false;publishPrompt(construction);sceneTick(60);
			expect(BarGeometryAttributeTestAccess::PromptRingProgress(owner.constructionPrompt)<1,"cancel at the last step never fills the ring");
			construction=std::make_shared<Inkeys::Drawing::Draw3::ShapeUiSnapshot>(*construction);++construction->identity.generation;
			construction->construction.active=true;publishPrompt(construction);sceneTick(60);
			construction=std::make_shared<Inkeys::Drawing::Draw3::ShapeUiSnapshot>(*construction);++construction->identity.generation;
			construction->construction.active=false;construction->construction.finished=true;
			construction->completedConstructionSession=construction->identity.session+1;publishPrompt(construction);sceneTick(60);
			expect(BarGeometryAttributeTestAccess::PromptRingProgress(owner.constructionPrompt)<1,"completion from another construction session never fills the ring");
			construction=std::make_shared<Inkeys::Drawing::Draw3::ShapeUiSnapshot>(*construction);++construction->identity.generation;
			construction->construction.active=true;construction->construction.finished=false;
			construction->construction.step=1;construction->construction.confirmedSteps=0;construction->construction.canBack=false;construction->construction.canForward=true;publishPrompt(construction);sceneTick(120);
			frame=BarGeometryAttributeTestAccess::PromptFrame(owner.constructionPrompt);
			expect(!frame.buttons[0].enabled&&frame.buttons[1].enabled,"backed-out step enables only Forward and Cancel");
			checkPromptTouch();
			construction=std::make_shared<Inkeys::Drawing::Draw3::ShapeUiSnapshot>(*construction);++construction->identity.generation;
			construction->construction.preset=PresetKind::Polygon;construction->construction.totalSteps=0;construction->construction.step=3;
			construction->construction.confirmedSteps=2;construction->construction.canBack=true;construction->construction.canForward=false;publishPrompt(construction);sceneTick(120);
			frame=BarGeometryAttributeTestAccess::PromptFrame(owner.constructionPrompt);
			expect(!frame.ring&&frame.buttons[2].visible&&!frame.buttons[2].enabled,"polygon has no ring and Complete waits for three points");
			checkPromptTouch();
			construction=std::make_shared<Inkeys::Drawing::Draw3::ShapeUiSnapshot>(*construction);++construction->identity.generation;
			construction->construction.step=4;construction->construction.confirmedSteps=3;construction->construction.canComplete=true;publishPrompt(construction);sceneTick(120);
			checkPromptTouch();
			expect(BarGeometryAttributeTestAccess::PromptLabel(owner.constructionPrompt).content.GetVal().find(L"已确定 3 个点")!=std::wstring::npos,"polygon labels confirmed points");
			checkPromptPixels((L"construction-polygon-"+suffix).c_str());
			const auto& cancelButton=BarGeometryAttributeTestAccess::PromptButton(owner.constructionPrompt,3).button;
			const auto& completeButton=BarGeometryAttributeTestAccess::PromptButton(owner.constructionPrompt,2).button;
			expect(cancelButton.inhX<completeButton.inhX&&std::abs(cancelButton.inhY-completeButton.inhY)<.01,"Cancel stays left of Complete on the same row");
			frame=BarGeometryAttributeTestAccess::PromptFrame(owner.constructionPrompt);
			const auto currentMain=owner.shapeMap[BarUISetShapeEnum::MainBar];
			const EraserAttributeRect mainRect{currentMain->inhX,currentMain->inhY,currentMain->inhX+currentMain->w.val,currentMain->inhY+currentMain->h.val};
			const auto anchorRoot=owner.superellipseMap[BarUISetSuperellipseEnum::MainButton];
			const double visibleTop=(std::min)(static_cast<double>(anchorRoot->inhY),static_cast<double>(mainRect.top));
			expect(frame.panel.bottom>mainRect.top&&std::abs(frame.panel.bottom-(visibleTop+BarBottomDockIndicatorHeightDip/2))<.01,"prompt embeds into visible main bar by the bottom dock indicator depth");
			construction=std::make_shared<Inkeys::Drawing::Draw3::ShapeUiSnapshot>(*construction);++construction->identity.generation;construction->constructionContactActive=true;publishPrompt(construction);sceneTick(90);
			frame=BarGeometryAttributeTestAccess::PromptFrame(owner.constructionPrompt);for(const auto& action:frame.buttons)expect(!action.enabled,"all actions disable during physical contact");
			checkPromptTouch();
			construction=std::make_shared<Inkeys::Drawing::Draw3::ShapeUiSnapshot>(*construction);++construction->identity.generation;construction->constructionContactActive=false;publishPrompt(construction);sceneTick(90);
			// 人工收起仍展示提示，并以主按钮而非已隐藏主栏的中心定位。
			BarGeometryAttributeTestAccess::MoveRoot(owner,600,650);sceneTick(90);owner.barState.fold=true;sceneTick(120);
			frame=BarGeometryAttributeTestAccess::PromptFrame(owner.constructionPrompt);const auto currentRoot=owner.superellipseMap[BarUISetSuperellipseEnum::MainButton];
			expect(frame.visible&&frame.input&&std::abs((frame.panel.left+frame.panel.right)/2-(currentRoot->inhX+currentRoot->w.val/2))<.1,"manual collapse retains prompt anchored to main button");
			checkPromptTouch();
			screenshot(L"construction-folded-"+suffix);
			Inkeys::Drawing::Draw3::PublishShapeUiSnapshot({});bool retiring=false;
			for(int sample=0;sample<50;++sample)
			{
				sceneTick(1);const auto retiringFrame=BarGeometryAttributeTestAccess::PromptFrame(owner.constructionPrompt);retiring|=retiringFrame.scale>.01&&retiringFrame.scale<.99;
				if(sample==12)screenshot(L"construction-disappear-"+suffix);
			}
			expect(retiring&&!owner.constructionPrompt.Visible(),"disappearance traverses intermediate scale and retires fully");
			owner.barState.fold=false;sceneTick(90);
		}
		// 窄屏提示改成双列换行，叉勾仍必须同排；动作索引不随视觉顺序变动。
		for(const auto preset:{draw3::shapes::PresetKind::Polygon,draw3::shapes::PresetKind::Polyline})for(const LONG width:{90L,140L,200L})
		{
			BarConstructionPrompt narrow;
			auto construction=std::make_shared<Inkeys::Drawing::Draw3::ShapeUiSnapshot>();
			construction->identity.session=++promptSession;construction->construction.active=true;construction->construction.preset=preset;
			construction->construction.confirmedSteps=3;construction->construction.canBack=true;construction->construction.canComplete=true;
			for(int i=0;i<180;++i)narrow.Advance(owner,construction,.017,1,1,{0,0,width,800},{},0,0,nullptr);
			narrow.CommitPresented();
			const auto& cancel=BarGeometryAttributeTestAccess::PromptButton(narrow,3).button;
			const auto& complete=BarGeometryAttributeTestAccess::PromptButton(narrow,2).button;
			expect(cancel.inhX<complete.inhX&&std::abs(cancel.inhY-complete.inhY)<.01,"narrow polygon/polyline prompt keeps Cancel left of Complete on same row");
		}
		// 生产换字顺序：先扩到足够宽；扩框中重定向短文案，换字后才缩框。
		auto construction=std::make_shared<Inkeys::Drawing::Draw3::ShapeUiSnapshot>();construction->identity.session=++promptSession;construction->identity.generation=1;
		construction->construction.active=true;construction->construction.preset=PresetKind::Polygon;construction->construction.confirmedSteps=1;construction->construction.step=2;construction->construction.canBack=true;
		publishPrompt(construction);sceneTick(120);const auto firstLabel=BarGeometryAttributeTestAccess::PromptLabel(owner.constructionPrompt).content.GetVal();
		construction=std::make_shared<Inkeys::Drawing::Draw3::ShapeUiSnapshot>(*construction);++construction->identity.generation;construction->construction.confirmedSteps=100;publishPrompt(construction);sceneTick(3);
		expect(BarGeometryAttributeTestAccess::PromptContentPhase(owner.constructionPrompt)==BarBottomDockIndicatorContentPhase::ExpandingFrame&&BarGeometryAttributeTestAccess::PromptLabel(owner.constructionPrompt).content.GetVal()==firstLabel,"expansion precedes any text replacement");
		construction=std::make_shared<Inkeys::Drawing::Draw3::ShapeUiSnapshot>(*construction);++construction->identity.generation;construction->construction.confirmedSteps=2;publishPrompt(construction);
		bool swapped=false,shrunk=false;for(int sample=0;sample<180;++sample){sceneTick(1);const auto phase=BarGeometryAttributeTestAccess::PromptContentPhase(owner.constructionPrompt);if(phase==BarBottomDockIndicatorContentPhase::SwappingText)swapped=true;if(phase==BarBottomDockIndicatorContentPhase::ShrinkingFrame){expect(swapped,"shrinking follows text transition");shrunk=true;}}
		expect(swapped&&shrunk,"shared three-phase helper completes expansion, text swap and shrink");
		BarGeometryAttributeTestAccess::MoveRoot(owner,600,60);sceneTick(120);
		const auto belowPrompt=BarGeometryAttributeTestAccess::PromptFrame(owner.constructionPrompt);
		const auto topRoot=owner.superellipseMap[BarUISetSuperellipseEnum::MainButton];
		const auto topMain=owner.shapeMap[BarUISetShapeEnum::MainBar];
		expect(std::abs(belowPrompt.panel.top-((std::max)(topRoot->inhY+topRoot->h.val,topMain->inhY+topMain->h.val)-BarBottomDockIndicatorHeightDip/2))<.01,"insufficient room above flips prompt below and embeds it into visible bar");
		// 小尺寸：步骤栏不再嵌入主栏，翻到下方时与主栏保持属性窗间隙；回到上方同理。
		owner.barStyle.requestedSizeMode=1;sceneTick(150);
		{
			const auto smallBelow=BarGeometryAttributeTestAccess::PromptFrame(owner.constructionPrompt);
			const auto smallRoot=owner.superellipseMap[BarUISetSuperellipseEnum::MainButton];const auto smallMain=owner.shapeMap[BarUISetShapeEnum::MainBar];
			const double gap=ResolveBarSizeMetrics(BarSizeMode::Compact).attachedGap;
			expect(std::abs(smallMain->h.val-42.5)<.01&&std::abs(smallRoot->h.val-42.5)<.01,"compact size lands a 42.5 DIP main bar and main button");
			expect(std::abs(smallBelow.panel.top-((std::max)(smallRoot->inhY+smallRoot->h.val,smallMain->inhY+smallMain->h.val)+gap))<.01,"compact prompt flips below with the attached-panel gap instead of embedding");
			BarGeometryAttributeTestAccess::MoveRoot(owner,600,650);sceneTick(120);
			const auto smallAbove=BarGeometryAttributeTestAccess::PromptFrame(owner.constructionPrompt);
			const auto aboveRoot=owner.superellipseMap[BarUISetSuperellipseEnum::MainButton];const auto aboveMain=owner.shapeMap[BarUISetShapeEnum::MainBar];
			const double smallTop=(std::min)(static_cast<double>(aboveRoot->inhY),static_cast<double>(aboveMain->inhY));
			expect(std::abs(smallAbove.panel.bottom-(smallTop-gap))<.01,"compact prompt sits above the main bar with the attached-panel gap");
		}
		owner.barStyle.requestedSizeMode=0;sceneTick(150);
		BarGeometryAttributeTestAccess::MoveRoot(owner,600,60);sceneTick(120);
		BarGeometryAttributeTestAccess::Display(owner,460,420,192);owner.barStyle.configZoom=1;sceneTick(120);
		const auto compact=BarGeometryAttributeTestAccess::PromptFrame(owner.constructionPrompt);
		expect(compact.visible&&compact.input,"restricted workspace prompt settles and accepts input");
		checkPromptTouch();
		for(const auto& action:compact.buttons)if(action.visible)expect(action.boundsDip.left>=compact.panel.left&&action.boundsDip.right<=compact.panel.right&&action.boundsDip.top>=compact.panel.top&&action.boundsDip.bottom<=compact.panel.bottom,"wrapped prompt preserves full standard button bounds");
		checkPromptPixels(L"construction-compact-200.png");
		Inkeys::Drawing::Draw3::PublishShapeUiSnapshot({});owner.UpdateRendering(false);sceneTick(90);
		expect(!owner.constructionPrompt.Visible(),"finished or cancelled construction prompt retires fully");
		scene = {}; owner.specTarget.Discard();
		report << "Actual scene commits=" << actualCommits << '\n';
		globalPath = originalGlobalPath;
		report << "Geometry UI checks=" << checks << " frames=" << frames << " failures=" << failures << '\n';
		std::cerr << "Geometry UI checks=" << checks << " frames=" << frames << " failures=" << failures << '\n'; report.close();

		RenderPipeline::Shutdown(); CoUninitialize(); return failures == 0 ? 0 : 1;
	}
}
