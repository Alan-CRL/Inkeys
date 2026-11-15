module;
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../../../IdtMain.h"
#include "../../../IdtState.h"
#include "../../../IdtI18n.h"
#include "../../../IdtI18nKeys.g.h"
#include "../../Drawing/Draw3/Draw3.ShapeInteraction.h"
#include "../../Drawing/Draw3/Draw3.Bridge.h"
#include "Bar.InteractionWorker.h"
#include "Bar.ShapeGesture.h"
#include <ink_stroke_modeler/stroke_modeler.h>
#include "Bar.ThemeMaterial.h"
#include <d2d1_1helper.h>
#include <wincodec.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <condition_variable>
#include <limits>
#include <atomic>
#include <vector>
#include <deque>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <crtdbg.h>
module Inkeys.UI.Bar;
import :Scene;
import :Main;
import :Layout;
import :Theme;
import Inkeys.UI.Bar.FramePacing;
import Inkeys.UI.Bar.EraserAttributeMotion;
import Inkeys.Drawing.Draw3.drawing_controller;
import Inkeys.Drawing.Draw3.ink_prediction;
extern const double BarUiDividerRadius;
extern const double BarUiDividerCursorLightIntensity;
extern const double BarDrawAttributeSurfaceOpacity;
namespace Inkeys::UI::Bar
{
	using namespace Inkeys::Drawing::Draw3;
	using namespace draw3::shapes;
	BarSurfaceSharedLighting ReadShapeEditorSharedLighting() noexcept;
	BarUiFrameLightingSnapshot ResolveBarSurfaceFrameLightingSnapshot(const BarSurfaceSharedLighting&, const RECT&, LONG) noexcept;
	namespace
	{
		constexpr unsigned Color = 1, Style = 2, Pattern = 5, Thickness = 9, Sides = 12, Swatch = Sides+GeometrySidesCount, Tone = Swatch+11, Rotate = Tone+1;
		constexpr unsigned SidesEntry = Rotate+1, Aux = SidesEntry+1, Custom = Aux+3, PickerClose = Custom+1;
		constexpr unsigned TriangleMode = PickerClose+1, TrapezoidMode = TriangleMode+3, Finish = TrapezoidMode+1, FlipHorizontal = Finish+1, FlipVertical = FlipHorizontal+1, Rotate45 = FlipVertical+1, Rotate90 = Rotate45+1, Count = Rotate90+1;
		constexpr unsigned PaletteHit = 100, OutsideLayerHit = 101;
		constexpr std::array<Auxiliary, 3> AuxiliaryBits{Auxiliary::Center, Auxiliary::Foci, Auxiliary::Diagonals};
		constexpr std::array<unsigned, 23> ToolbarItems{Color, Style, Style+1, Pattern, Pattern+1, Pattern+2, Pattern+3,
			Thickness, Thickness+1, Thickness+2, SidesEntry, TriangleMode, TriangleMode+1, TriangleMode+2, TrapezoidMode, Aux, Aux+1, Aux+2, FlipHorizontal, FlipVertical, Rotate45, Rotate90, Finish};
		constexpr std::array<unsigned, 8> GroupStarts{Style, Pattern, Thickness, SidesEntry, TriangleMode, TrapezoidMode, Aux, Finish};
		unsigned EditorPanel(unsigned id)
		{
			return id >= Sides && id<Swatch ? 1 : (id >= Swatch && id<Tone) || id == Custom ? 2 : id == Tone || id == PickerClose ? 3 : 0;
		}
		double ShapeWidthScale(const ShapeObject& object)
		{
			const auto& t = object.transform;
			return std::sqrt(std::abs(static_cast<double>(t.m11)*t.m22-static_cast<double>(t.m12)*t.m21));
		}
		D2D1_RECT_F EditorRect(EraserAttributeRect r)
		{
			return {static_cast<FLOAT>(r.left), static_cast<FLOAT>(r.top), static_cast<FLOAT>(r.right), static_cast<FLOAT>(r.bottom)};
		}
		EraserAttributeRect EditorRect(D2D1_RECT_F r)
		{
			return {r.left, r.top, r.right, r.bottom};
		}
		EraserSurfacePose FitEditorPose(EraserSurfacePose pose, EraserAttributeRect shell, EraserAttributeRect work)
		{
			// FitEraserSurfacePose只平移；先按GeometryAttribute同一方式约束回弹峰值，再夹紧位置。
			const double bounded = (std::min)({pose.scale, work.Width()/shell.Width(), work.Height()/shell.Height()});
			if (bounded>0 && bounded<pose.scale)
			{
				const auto current = pose.Apply(shell); const double cx = EraserRectCenterX(current), cy = EraserRectCenterY(current);
				pose.scale = bounded; pose.x = cx-EraserRectCenterX(shell)*bounded; pose.y = cy-EraserRectCenterY(shell)*bounded;
			}
			pose = FitEraserSurfacePose(pose, shell, work);
			// 贴边包络相减可能多出浮点尾差，不能因此跳过整轴夹紧；直接约束平移量。
			const double left = work.left-shell.left*pose.scale, top = work.top-shell.top*pose.scale;
			const double right = work.right-shell.right*pose.scale, bottom = work.bottom-shell.bottom*pose.scale;
			pose.x = (std::clamp)(pose.x, left, right >= left ? right : left);
			pose.y = (std::clamp)(pose.y, top, bottom >= top ? bottom : top);
			return pose;
		}
		EraserSurfacePose ResolveEditorPickerPose(const BarAttributePanelMotion& motion, EraserAttributeRect work,
			EraserAttributeRect barRect, EraserAttributeRect paletteRect, double triggerX, double triggerY, bool preferBelow, bool barBelow, bool& placedBelow)
		{
			const EraserAttributeRect panel{0, 0, 300, 209};
			// 两个下层表面最多切出16个空闲矩形；回弹全过程也在同一个空闲包络内适配。
			std::array<EraserAttributeRect, 16> regions{};
			regions[0] = work;
			size_t regionCount = 1;
			for (const auto blocker : {barRect, paletteRect})
			{
				std::array<EraserAttributeRect, 16> remaining{};
				size_t count = 0;
				for (size_t i = 0; i<regionCount; ++i)
				{
					const auto r = regions[i];
					if (r.right <= blocker.left || r.left >= blocker.right || r.bottom <= blocker.top || r.top >= blocker.bottom)
					{
						remaining[count++] = r;
						continue;
					}
					for (const auto freeRect : std::array<EraserAttributeRect, 4>{{
						{r.left, r.top, (std::min)(r.right, blocker.left), r.bottom},
						{(std::max)(r.left, blocker.right), r.top, r.right, r.bottom},
						{r.left, r.top, r.right, (std::min)(r.bottom, blocker.top)},
						{r.left, (std::max)(r.top, blocker.bottom), r.right, r.bottom}}})
						if (freeRect.Width()>0 && freeRect.Height()>0) remaining[count++] = freeRect;
				}
				regions = remaining; regionCount = count;
			}
			const double alignedX = (std::clamp)(triggerX-panel.Width()/2, work.left, (std::max)(work.left, work.right-panel.Width()));
			const std::array<EraserSurfacePose, 4> candidates{{
				{1, alignedX, preferBelow ? paletteRect.bottom+8 : paletteRect.top-8-panel.Height()},
				{1, paletteRect.right+8, paletteRect.top},
				{1, paletteRect.left-8-panel.Width(), paletteRect.top},
				{1, alignedX, barBelow ? barRect.top-8-panel.Height() : barRect.bottom+8}}};
			EraserSurfacePose base = candidates[0];
			EraserAttributeRect envelope{};
			bool found = false;
			// 按设计顺序取第一个完整容纳的位置，不能先夹紧到工作区而盖住下层。
			for (const auto pose : candidates)
			{
				const auto target = pose.Apply(panel);
				for (size_t i = 0; i<regionCount; ++i)
				{
					const auto r = regions[i];
					if (target.left >= r.left && target.top >= r.top && target.right <= r.right && target.bottom <= r.bottom)
					{
						base = pose; envelope = r; found = true; break;
					}
				}
				if (found) break;
			}
			if (!found)
			{
				double bestScale = 0, bestDistance = (std::numeric_limits<double>::max)();
				for (size_t i = 0; i<regionCount; ++i)
				{
					const auto pose = FitEditorPose(candidates[0], panel, regions[i]);
					const double distance = std::hypot(pose.x-candidates[0].x, pose.y-candidates[0].y);
					if (pose.scale>bestScale || (pose.scale == bestScale && distance<bestDistance))
					{
						bestScale = pose.scale; bestDistance = distance; envelope = regions[i];
					}
				}
				// 工作区被下层完全占满时保留零面积姿态，绝不覆盖下层。
				if (bestScale <= 0) return {0, work.left, work.top};
				base = FitEditorPose(candidates[0], panel, envelope);
			}
			placedBelow = EraserRectCenterY(base.Apply(panel)) >= EraserRectCenterY(paletteRect);
			const double ax = (triggerX-base.x)/base.scale, ay = (triggerY-base.y)/base.scale;
			const auto frame = ResolveBarAttributePanelFrame(motion, panel, {ax, ay, ax, ay}, 60);
			return FitEditorPose(ComposeEraserPose(base, frame.pose), panel, envelope);
		}
		bool EmptyRect(RECT r)
		{
			return r.right <= r.left || r.bottom <= r.top;
		}

		bool Inside(D2D1_RECT_F r, double x, double y)
		{
			return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
		}

		COLORREF ShapeRgbToUiColor(std::uint32_t rgb) noexcept
		{
			return RGB((rgb>>16)&255, (rgb>>8)&255, rgb&255);
		}

		std::uint32_t UiColorToShapeRgb(COLORREF color) noexcept
		{
			return (static_cast<std::uint32_t>(GetRValue(color))<<16)|(static_cast<std::uint32_t>(GetGValue(color))<<8)|GetBValue(color);
		}

		void InitShape(BarUiShapeClass& p, double radius, double opacity)
		{
			p.Initialization(0, 0, 1, 1, radius, radius, BarButtonFrameThicknessDip, GetThemeColor(BarThemeColorEnum::Surface),
				GetThemeColor(BarThemeColorEnum::SurfaceFrame));
			p.enable.Initialization(true);
			p.pct.SetDirect(opacity);
			p.framePct.emplace(BarMainBarFrameOpacity);
			p.frameLightPct.emplace(BarMainBarFrameOpacity);
			p.frameRendering = BarUiFrameRenderingEnum::PointLight;
			p.frameLightColor = BarUiFrameLightColorEnum::PenWhenDrawing;
		}

		void PlaceShape(BarUiShapeClass& p, double x, double y, double w, double h)
		{
			p.x.SetDirect(x+w/2);
			p.y.SetDirect(y+h/2);
			p.w.SetDirect(w);
			p.h.SetDirect(h);
			p.UpInh({x, y});
		}

		bool HasOpacity(BarButtonClass& p)
		{
			return p.icon.pct.val > .000001 || p.name.pct.val > .000001 || p.button.pct.val > .000001;
		}

		bool SameEditorLighting(const BarSurfaceSharedLighting& a, const BarSurfaceSharedLighting& b) noexcept
		{
			return a.primaryScreenX == b.primaryScreenX && a.primaryScreenY == b.primaryScreenY && a.primaryRadiusPixels == b.primaryRadiusPixels
				&& a.cursorScreenX == b.cursorScreenX && a.cursorScreenY == b.cursorScreenY && a.cursorRadiusPixels == b.cursorRadiusPixels
				&& a.cursorIntensity == b.cursorIntensity && a.drawingPenColor == b.drawingPenColor
				&& a.drawingPenColorBlend == b.drawingPenColorBlend && a.drawingLightOpacity == b.drawingLightOpacity
				&& a.primaryVisible == b.primaryVisible && a.cursorVisible == b.cursorVisible && a.edgeLightingEnabled == b.edgeLightingEnabled;
		}

		bool SaveEditorPng(const std::filesystem::path& path, const BYTE* bytes, UINT width, UINT height, UINT stride)
		{
			Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
			Microsoft::WRL::ComPtr<IWICStream> stream;
			Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
			Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
			Microsoft::WRL::ComPtr<IPropertyBag2> properties;
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
			if (SUCCEEDED(hr)) hr = frame->WritePixels(height, stride, height*stride, const_cast<BYTE*>(bytes));
			if (SUCCEEDED(hr)) hr = frame->Commit();
			if (SUCCEEDED(hr)) hr = encoder->Commit();
			return SUCCEEDED(hr);
		}

		std::vector<BYTE> ReadEditorPixels(BarUIRendering& renderer, SIZE extent)
		{
			auto* dc = renderer.GetDeviceContext();
			auto* target = renderer.GetTargetBitmap();
			if (!dc || !target || extent.cx <= 0 || extent.cy <= 0) return {};
			Microsoft::WRL::ComPtr<ID2D1Bitmap1> readable;
			const auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW, target->GetPixelFormat());
			const D2D1_RECT_U crop{0, 0, static_cast<UINT32>(extent.cx), static_cast<UINT32>(extent.cy)};
			if (FAILED(dc->CreateBitmap({crop.right, crop.bottom}, nullptr, 0, &props, &readable)) || FAILED(readable->CopyFromBitmap(nullptr,
				target, &crop))) return {};
			D2D1_MAPPED_RECT mapped{};
			if (FAILED(readable->Map(D2D1_MAP_OPTIONS_READ, &mapped))) return {};
			std::vector<BYTE> pixels(static_cast<size_t>(extent.cx)*extent.cy*4);
			for (LONG row = 0; row<extent.cy; ++row) std::memcpy(pixels.data()+static_cast<size_t>(row)*extent.cx*4,
				mapped.bits+static_cast<size_t>(row)*mapped.pitch, extent.cx*4);
			readable->Unmap();
			return pixels;
		}

		std::array<double, 3> EditorPixelRgb(const std::vector<BYTE>& pixels, SIZE extent, int x, int y)
		{
			if (x<0 || y<0 || x >= extent.cx || y >= extent.cy || pixels.size() != static_cast<size_t>(extent.cx)*extent.cy*4) return {};
			const size_t p = (static_cast<size_t>(y)*extent.cx+x)*4;
			const double alpha = pixels[p+3];
			if (alpha<180) return {-1, -1, -1};
			return {(std::min)(255., pixels[p+2]*255./alpha), (std::min)(255., pixels[p+1]*255./alpha), (std::min)(255., pixels[p]*255./alpha)};
		}

		struct EditorGlyphEvidence
		{
			unsigned pixels = 0;
			double peak = 0;
		};
		EditorGlyphEvidence InspectEditorGlyph(const std::vector<BYTE>& pixels, SIZE extent, D2D1_RECT_F rect, POINT background,
			COLORREF foreground, std::optional<COLORREF> sourceBackground = std::nullopt)
		{
			EditorGlyphEvidence result;
			if (rect.right <= rect.left || rect.bottom <= rect.top) return result;
			const auto base = sourceBackground ? std::array<double, 3>{double(GetRValue(*sourceBackground)), double(GetGValue(*sourceBackground)),
				double(GetBValue(*sourceBackground))} : EditorPixelRgb(pixels, extent, background.x, background.y);
			if (base[0]<0) return result;
			const std::array<double, 3> target{double(GetRValue(foreground)), double(GetGValue(foreground)), double(GetBValue(foreground))};
			std::array<double, 3> delta{};
			double norm = 0;
			for (unsigned c = 0; c<3; ++c)
			{
				delta[c] = target[c]-base[c];
				norm += delta[c]*delta[c];
			}
			if (norm<100) return result;
			for (int y = (std::max)(0, static_cast<int>(std::ceil(rect.top))); y<(std::min)(extent.cy,
				static_cast<LONG>(rect.bottom)); ++y) for (int x = (std::max)(0, static_cast<int>(std::ceil(rect.left))); x<(std::min)(extent.cx,
				static_cast<LONG>(rect.right)); ++x)
			{
				const auto actual = EditorPixelRgb(pixels, extent, x, y);
				if (actual[0]<0) continue;
				double projection = 0;
				for (unsigned c = 0; c<3; ++c) projection += (actual[c]-base[c])*delta[c];
				projection /= norm;
				double residual = 0;
				for (unsigned c = 0; c<3; ++c)
				{
					const double error = actual[c]-base[c]-projection*delta[c];
					residual += error*error;
				}
				if (projection>.20 && projection<1.15 && residual<(sourceBackground ? 36. : (std::max)(324., norm*.04)))
				{
					++result.pixels;
					result.peak = (std::max)(result.peak, projection);
				}
			}
			return result;
		}

		struct EditorFrameVisual
		{
			D2D1_ROUNDED_RECT rounded;
			D2D1::Matrix3x2F transform;
			FLOAT thickness;
		};
		EditorFrameVisual ResolveEditorFrame(const ShapeUiSnapshot& snapshot, RECT bounds, double scale)
		{
			const auto& t = snapshot.object->transform;
			const double sx = std::hypot(t.m11, t.m12);
			const double sy = (t.m11 * t.m22 - t.m12 * t.m21) / sx;
			const auto& f = snapshot.frameRect;
			const FLOAT left = static_cast<FLOAT>((std::min)(f.left * sx, f.right * sx));
			const FLOAT top = static_cast<FLOAT>((std::min)(f.top * sy, f.bottom * sy));
			const FLOAT width = static_cast<FLOAT>((std::max)(scale, (f.right - f.left) * sx));
			const FLOAT height = static_cast<FLOAT>((std::max)(scale, std::abs((f.bottom - f.top) * sy)));
			// 轴向缩放烘焙进矩形尺寸，D2D矩阵只保留旋转和平移，线宽与圆角不随对象缩放。
			return {D2D1::RoundedRect({left, top, left + width, top + height}, static_cast<FLOAT>(6 * scale), static_cast<FLOAT>(6 * scale)),
				D2D1::Matrix3x2F(static_cast<FLOAT>(t.m11 / sx), static_cast<FLOAT>(t.m12 / sx), static_cast<FLOAT>(-t.m12 / sx),
				static_cast<FLOAT>(t.m11 / sx), static_cast<FLOAT>(snapshot.canvasOriginX + t.dx - bounds.left),
				static_cast<FLOAT>(snapshot.canvasOriginY + t.dy - bounds.top)), static_cast<FLOAT>(2 * scale)};
		}
		PresetKind EditorPreset(const ShapeObject& object) noexcept
		{
			return object.construction ? object.construction->tool.preset : object.kind == Kind::Line ? PresetKind::Line
				: object.kind == Kind::Circle ? PresetKind::Circle : object.kind == Kind::Ellipse ? PresetKind::Ellipse : PresetKind::Rectangle;
		}
		bool ShowEditorFrame(PresetKind preset) noexcept
		{
			return preset != PresetKind::Line && preset != PresetKind::Arrow && preset != PresetKind::Rectangle;
		}

		void DrawEditorInputCoverage(BarUIRendering& renderer, ID2D1DeviceContext* dc,
			const ShapeUiSnapshot& snapshot, RECT bounds, double scale)
		{
			// ULW输入需要非零alpha；COPY使重叠热区仍恰为1，不进入材质、阴影或内容透明度通道。
			auto* brush = renderer.ContentSolidBrush(RGB(0, 0, 0), 1.0 / 255.0);
			if (!brush) return;
			const auto blend = dc->GetPrimitiveBlend();
			const auto aa = dc->GetAntialiasMode();
			dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY);
			dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
			const auto& b = snapshot.bounds;
			// 构造期只覆盖顶点热区，空白包围盒必须让原生触摸落回画布加点。
			if (b.valid && !snapshot.construction.active) dc->FillRectangle({static_cast<FLOAT>(snapshot.canvasOriginX+b.left-bounds.left-3*scale),
				static_cast<FLOAT>(snapshot.canvasOriginY+b.top-bounds.top-3*scale),
				static_cast<FLOAT>(snapshot.canvasOriginX+b.right-bounds.left+3*scale),
				static_cast<FLOAT>(snapshot.canvasOriginY+b.bottom-bounds.top+3*scale)}, brush);
			if (snapshot.object && !snapshot.construction.active && snapshot.frameRect.valid
				&& EditorPreset(*snapshot.object)!=PresetKind::Line && EditorPreset(*snapshot.object)!=PresetKind::Arrow)
			{
				const auto& t=snapshot.object->transform; const auto& f=snapshot.frameRect;
				D2D1_MATRIX_3X2_F original; dc->GetTransform(&original);
				dc->SetTransform(D2D1::Matrix3x2F(t.m11,t.m12,t.m21,t.m22,
					t.dx+snapshot.canvasOriginX-bounds.left,t.dy+snapshot.canvasOriginY-bounds.top)*original);
				dc->FillRectangle({f.left,f.top,f.right,f.bottom},brush); dc->SetTransform(original);
			}
			for (const auto& h : snapshot.handles) dc->FillEllipse({{static_cast<FLOAT>(snapshot.canvasOriginX+h.position.x-bounds.left),
				static_cast<FLOAT>(snapshot.canvasOriginY+h.position.y-bounds.top)}, static_cast<FLOAT>(22*scale), static_cast<FLOAT>(22*scale)}, brush);
			dc->SetAntialiasMode(aa);
			dc->SetPrimitiveBlend(blend);
		}

		std::wstring Resource(unsigned id)
		{
			if (id >= Style && id < Pattern) return std::array<const wchar_t*, 3>{L"barShapeOutline", L"barShapeFill", L"barShapeOutlineFill"}[id-Style];
			if (id >= Pattern && id < Thickness) return std::array<const wchar_t*, 4>{L"barShapeSolid", L"barShapeDashed", L"barShapeDotted",
				L"barShapeDashDot"}[id-Pattern];
			if (id >= Aux && id<Aux+3) return std::array<const wchar_t*, 3>{L"barShapeAuxCenter", L"barShapeAuxFoci", L"barShapeAuxDiagonals"}[id-Aux];
			if (id >= TriangleMode && id<TriangleMode+3) return L"barShapeTriangle";
			if (id == TrapezoidMode) return L"barShapeTrapezoid";
			if (id == Finish) return L"fluentCheckmark12Filled";
			if (id == FlipHorizontal) return L"barShapeFlipHorizontal";
			if (id == FlipVertical) return L"barShapeFlipVertical";
			if (id == Rotate45) return L"barShapeRotate45";
			if (id == Rotate90) return L"barShapeRotate90";
			if (id == Rotate) return L"barShapeRotate";
			if (id == Tone) return L"colorSun";
			if ((id >= Swatch && id<Tone) || id == Custom) return L"colorSelect";
			return {};
		}
	}

	namespace
	{
		// 只保留队列拥塞的终结意图；重试由既有输入worker的surface tick驱动。
		class ShapeIntentSender
		{
			public : using Publisher = std::function<Bridge::CommandResult(const ShapeUiAction&)>;
			using Snapshot = std::function<std::shared_ptr<const ShapeUiSnapshot>()>;
			ShapeIntentSender(Publisher publish, Snapshot snapshot) : publish_(std::move(publish)), snapshot_(std::move(snapshot))
			{
			}
			bool Send(const ShapeUiAction& action)
			{
				std::scoped_lock lock(mutex_);
				const auto current = snapshot_();
				std::erase_if(pending_, [&](const ShapeUiAction& old)
				{
					return !Current(current, old);
				});
				const bool terminal = Terminal(action.kind);
				if (!pending_.empty())
				{
					if (!terminal) return false;
					for (auto& old : pending_) if (old.identity.session == action.identity.session
						&& old.identity.documentGeneration == action.identity.documentGeneration
						&& old.identity.displayGeneration == action.identity.displayGeneration && old.identity.workspace == action.identity.workspace
						&& old.identity.pageId == action.identity.pageId && old.identity.pageIndex == action.identity.pageIndex
						&& old.identity.objectId == action.identity.objectId && old.kind == action.kind)
					{
						old = action;
						return true;
					}
					// 同一当前会话最多五类终结，各类只保留最后一次意图。
					if (pending_.size() >= 5) return false;
					pending_.push_back(action);
					return true;
				}
				const auto result = publish_(action);
				if (result == Bridge::CommandResult::Accepted) return true;
				if (terminal && result == Bridge::CommandResult::QueueFull)
				{
					pending_.push_back(action);
					return true;
				}
				return false;
			}
			std::chrono::steady_clock::time_point Tick(std::chrono::steady_clock::time_point now)
			{
				std::scoped_lock lock(mutex_);
				while (!pending_.empty())
				{
					const auto& action = pending_.front();
					const auto current = snapshot_();
					if (!Current(current, action))
					{
						pending_.pop_front();
						continue;
					}
					if (publish_(action) == Bridge::CommandResult::QueueFull) return now+std::chrono::milliseconds(16);
					pending_.pop_front();
				}
				return std::chrono::steady_clock::time_point::max();
			}
			void Clear()
			{
				std::scoped_lock lock(mutex_);
				pending_.clear();
			}
			private : static bool Terminal(ShapeUiActionKind kind)
			{
				return kind == ShapeUiActionKind::CommitEdit || kind == ShapeUiActionKind::CancelEdit || kind == ShapeUiActionKind::CloseEditor
					|| kind == ShapeUiActionKind::ConstructionCommitPointEdit || kind == ShapeUiActionKind::ConstructionCancelPointEdit
					|| kind == ShapeUiActionKind::ConstructionComplete || kind == ShapeUiActionKind::ConstructionCancel
					// 下一步按 PRD R2 沿用终结意图的有界重试，队列满时不能丢失用户的前进操作。
					|| kind == ShapeUiActionKind::ConstructionForward;
			}
			static bool Current(const std::shared_ptr<const ShapeUiSnapshot>& s, const ShapeUiAction& action)
			{
				if (!s) return false;
				const auto& a = action.identity;
				const auto& b = s->identity;
				if (a.session != b.session || a.documentGeneration != b.documentGeneration || a.displayGeneration != b.displayGeneration
					|| a.workspace != b.workspace || a.pageId != b.pageId || a.pageIndex != b.pageIndex) return false;
				// 构造快照没有 object；前进与完成/取消都按同一会话及对象身份重试。
				if (action.kind == ShapeUiActionKind::ConstructionBeginPointEdit || action.kind == ShapeUiActionKind::ConstructionPreviewPointEdit || action.kind == ShapeUiActionKind::ConstructionCommitPointEdit || action.kind == ShapeUiActionKind::ConstructionCancelPointEdit || action.kind == ShapeUiActionKind::ConstructionComplete || action.kind == ShapeUiActionKind::ConstructionCancel
					|| action.kind == ShapeUiActionKind::ConstructionForward) return s->construction.active && a.objectId == b.objectId;
				return s->object && a.objectId == b.objectId;
			}
			std::mutex mutex_;
			std::deque<ShapeUiAction> pending_;
			Publisher publish_;
			Snapshot snapshot_;
		};
		ShapeIntentSender& IntentSender()
		{
			static ShapeIntentSender sender{[](const auto& a)
			{
				return PublishShapeUiAction(a);
			}, []
			{
				return ProductShapeUiSnapshot();
			}};
			return sender;
		}
	}

	bool SubmitShapeUiIntent(const ShapeUiAction& action) noexcept
	{
		try
		{
			const bool accepted = IntentSender().Send(action);
			WakeInteractionWorker();
			return accepted;
		}
		catch (...)
		{
			return false;
		}
	}

	std::chrono::steady_clock::time_point RetryShapeUiIntents(std::chrono::steady_clock::time_point now) noexcept
	{
		try
		{
			return IntentSender().Tick(now);
		}
		catch (...)
		{
			return now+std::chrono::milliseconds(16);
		}
	}

	void ClearShapeUiIntents() noexcept
	{
		IntentSender().Clear();
	}

	// 沿用绘制的建模参数与预测模式；输入为真实接触时间，压力固定且不会进入图形样式。
	struct ShapeDragPredictor
	{
		ShapeContactGesture::Key key;
		ink::stroke_model::StrokeModeler model;
		ink::stroke_model::StrokeModelParams params;
		std::vector<ink::stroke_model::Result> output, prediction;
		double start = 0, last = 0;
		Vec2 previous{};
		bool ready = false;
		void Begin(ShapeContactGesture::Key identity, Vec2 point, double seconds, const ShapeUiSnapshot& snapshot)
		{
			key=identity; start=last=seconds; previous=point;
			const auto config=CreateStrokeModelConfiguration(static_cast<int>(snapshot.displayScale*96),static_cast<StrokeTimingProfileId>(snapshot.timingProfile));
			params=config.modelParams;
			ApplyPredictionMode(params,config.kalmanPredictorParams,static_cast<InkPredictionMode>(snapshot.predictionMode));
			ready=snapshot.predictionMode!=0 && model.Reset(params).ok();
			if (ready) ready=model.Update({.event_type=ink::stroke_model::Input::EventType::kDown,
				.position={point.x,point.y},.time=ink::stroke_model::Time(0),.pressure=1},output).ok();
		}
		std::optional<Vec2> Move(Vec2 point, double seconds)
		{
			if (!ready) return {};
			// 重复/倒退时钟不补造采样；长时间无输入后清空旧速度，避免停笔后突然外推。
			if (!std::isfinite(seconds) || seconds<=last) return {};
			if (seconds-last>.1)
			{
				start=last=seconds; previous=point; output.clear(); prediction.clear();
				ready=model.Reset(params).ok() && model.Update({.event_type=ink::stroke_model::Input::EventType::kDown,
					.position={point.x,point.y},.time=ink::stroke_model::Time(0),.pressure=1},output).ok();
				return {};
			}
			const Vec2 delta{point.x-previous.x,point.y-previous.y}; previous=point; last=seconds;
			output.clear(); prediction.clear();
			if (!model.Update({.event_type=ink::stroke_model::Input::EventType::kMove,.position={point.x,point.y},
				.time=ink::stroke_model::Time(seconds-start),.pressure=1},output).ok() || !model.Predict(prediction).ok()) { ready=false; return {}; }
			if (prediction.empty()) return {};
			const auto end=prediction.back().position;
			// 模型的平滑追赶可能落后于原始指针；编辑始终至少显示真实位置，不新增拖动迟滞。
			if (!std::isfinite(end.x) || !std::isfinite(end.y) || (end.x-point.x)*delta.x+(end.y-point.y)*delta.y<=0) return {};
			return Vec2{end.x,end.y};
		}
	};

	struct ShapeEditorSurface::Impl
	{
		BarUIRendering renderer;
		BarButtonListClass attributes;
		std::array<std::shared_ptr<BarButtonClass>, Count> buttons;
		std::array<std::shared_ptr<BarButtonClass>, GroupStarts.size()> dividers;
		BarUiShapeClass toolbar, sidesPanel, palettePanel, pickerPanel;
		std::array<BarUiShapeClass, 12> swatchSurfaces;
		BarUiPNGClass colorWheel;
		std::array<BarUiWordClass, 8> pickerWords;
		BarUiWordClass holdWord;
		BarUiShapeClass pickerPreview, holdSurface;
		std::array<BarUiValueClass, Count> x, y;
		std::array<BarUiValueClass, GroupStarts.size()> dividerX, dividerY;
		BarUiValueClass toolbarWidthValue{0}, toolbarHeightValue{0}, toolbarX{0}, toolbarY{0};
		BarUiTimelineClass memberBatch;
		std::array<bool, Count> memberWanted{};
		std::array<bool, GroupStarts.size()> dividerWanted{};
		BarAttributePanelMotion toolbarMotion, paletteMotion, sidesMotion, pickerMotion;
		bool twoRows = false;
		bool toolbarBelow = true;
		int toolbarPlacement = 0;
		BarUiValueClass dockResidualX{0}, dockResidualY{0};
		double dockTargetX = 0, dockTargetY = 0;
		double toolbarFit = 1;
		std::array<double, 3> auxiliaryWidths{};
		std::array<double, 4> constraintWidths{};
		std::array<std::wstring, 3> auxiliaryLabels{};
		std::array<std::wstring, 4> constraintLabels{};
		std::wstring auxiliaryFontFamily, auxiliaryLocale;
		std::uint64_t auxiliaryFontGeneration = 0;
		BarUiPctClass editOpacity{0};
		FrameAnimationClock clock;
		std::shared_ptr<const ShapeUiSnapshot> snapshot;
		GeometryResult geometry;
		RECT work{}, bar{}, bounds{};
		double scale = 1;
		bool active = false, initialized = false, requestedVisible = false;
		std::atomic_bool colorOpen = false, sidesOpen = false, pickerOpen = false, darkTone = false;
		std::uint64_t deviceGeneration = 0;
		std::array<float, 3> widths{2, 4, 6};
		struct Frame
		{
			std::shared_ptr<const ShapeUiSnapshot> snapshot;
			GeometryResult geometry;
			std::vector<EditHandle> visualHandles;
			RECT bounds{}, work{}, mainBar{};
			double scale = 1, opacity = 0;
			std::uint64_t presentationGeneration = 0;
			D2D1_RECT_F toolbar{}, parameters{}, colors{}, picker{}, palette{};
			std::array<EraserSurfacePose, 4> poses{};
			std::array<double, 4> panelOpacity{};
			bool paletteLayer = false, sidesLayer = false, pickerLayer = false, pickerBelow = true;
			COLORREF pickerColor = 0;
			double markerX = 0, markerY = 0;
			bool markerVisible = false;
			std::array<BarButtonHitSnapshot, Count> buttons{};
			bool visible = false, input = false, darkTone = false;
			std::array<float, 3> widths{};
		};
		Frame candidate, presented;
		mutable std::mutex inputMutex;
		// 输入worker与渲染线程共用期限；锚点存屏幕像素，DPI变化时才重新换算。
		std::chrono::steady_clock::time_point toolbarHoldUntil{};
		std::function<std::chrono::steady_clock::time_point()> toolbarNow = [] { return std::chrono::steady_clock::now(); };
		double toolbarHoldX = 0, toolbarHoldY = 0;
		bool toolbarHoldFrozen = false, toolbarHoldResume = false, toolbarOperationActive = false;
		// 当前已配置的编辑场景；旧呈现帧上的迟到操作不得把旧锚点冻结到新图形上。
		ShapeUiIdentity toolbarScene{};
		static bool SameToolbarScene(const ShapeUiIdentity& a, const ShapeUiIdentity& b)
		{
			return a.session == b.session && a.objectId == b.objectId && a.documentGeneration == b.documentGeneration
				&& a.displayGeneration == b.displayGeneration && a.workspace == b.workspace && a.pageId == b.pageId && a.pageIndex == b.pageIndex;
		}
		ButtonPointerState pointer;
		unsigned hover = 0, pressed = 0, appliedHover = 0, appliedPressed = 0;
		std::atomic_uint focused = 0;
		bool constructionGesture = false;
		bool gesture = false, paletteGesture = false, dismissGesture = false;
		ShapeStyle pickerBaseStyle;
		ShapeUiIdentity pickerIdentity;
		bool pickerDragging = false, pickerLocked = false; // inputMutex保护本地取色反馈，不写工具默认值。
		POINT pickerStablePoint{};
		std::chrono::steady_clock::time_point pickerStableTime{};
		BarUiPctClass holdOpacity{0}, holdRingOpacity{0}, holdTextMix{0}, toneMix{0};
		std::array<BarUiValueClass, 4> pickerDisplay;
		bool holdOnTop = false, holdWasVisible = false;
		double holdProgress = 0;
		COLORREF pickerDragColor = 0;
		double pickerDragX = 0, pickerDragY = 0;
		ShapeUiIdentity gestureIdentity;
		Vec2 down, pointerOffset{};
		unsigned handleDockSide = 0;
		std::optional<float> dockDeterminant;
		float dockTangentSign = 1;
		Vec2 handleDock{}, handleTangent{1,0}, handleEdge{}, dockFitOffset{};
		std::uint32_t handle = 0;
		std::function<void(const ShapeUiAction&)> actionSink;
		struct HandleVisual
		{
			std::uint32_t id = 0;
			EditHandleKind kind = EditHandleKind::Vertex;
			Vec2 position;
			BarUiValueClass offsetX{0}, offsetY{0};
			BarUiSVGClass glyph; std::wstring glyphResource;
			BarUiValueClass centerDiameter{12}, accent{1}, iconScale{1}, scale{.75}, bracketThickness{4};
			BarUiPctClass opacity{0};
			bool wanted = false;
		};
		std::vector<std::unique_ptr<HandleVisual>> handles;
		std::atomic_uint32_t hoveredHandle = UINT32_MAX, pressedHandle = UINT32_MAX;
		// 拖动跟手：交互worker用共享核心EditSession先行计算候选，绘制线程快照只用于校正与收尾。
		ShapeContactGesture contacts;
		std::vector<std::unique_ptr<ShapeDragPredictor>> predictors;
		std::optional<ShapeContactGesture::Key> contactOwner;
		bool contactTouch=false, contactPen=false;
		std::optional<Vec2> inputPrediction;
		std::optional<ShapeUiAction> predictionAction;
		std::chrono::steady_clock::time_point predictionDeadline{};
		EditSession localEdit;
		bool localEditActive = false;
		std::uint64_t localRevision = 0;
		std::shared_ptr<const ShapeUiSnapshot> localSnapshot;
		std::shared_ptr<const GeometryResult> localGeometry;
		bool localPending = false;
		std::uint64_t localUpGeneration = 0;
		std::chrono::steady_clock::time_point localUpTime{};
		// 渲染线程私有：本帧输入来源与是否需要重新呈现。
		std::shared_ptr<const ShapeUiSnapshot> sourceSnapshot, sourceLocal;
		bool tracking = false, holdingLocal = false, presentPending = true, lightingValid = false;
		BarSurfaceSharedLighting presentedLighting{};
		double appliedSurfaceOpacity = -1, appliedContentOpacity = -1, appliedLightWeight = -1;
		Microsoft::WRL::ComPtr<ID2D1StrokeStyle> connectorStyle;
		Microsoft::WRL::ComPtr<ID2D1PathGeometry> bracketGeometry;
		FLOAT bracketRadius = 0;
		double bracketScale = 0;
		std::uint64_t bracketGeometryCreations = 0; // 离屏回归计数，不参与渲染决策。
		Impl()
		{
			renderer.ConfigureLocalizedTypography();
			InitShape(toolbar, BarMainBarCornerRadiusDip, BarMainBarFillOpacity);
			for (auto* p : {&sidesPanel, &palettePanel, &pickerPanel}) InitShape(*p, 8, BarDrawAttributeSurfaceOpacity);
			pickerPanel.frameLightPct->SetDirect(1);
			InitShape(pickerPreview, 4, 1);
			InitShape(holdSurface, 4, .82);
			colorWheel.Initialization(0, 0);
			colorWheel.InitializationFromResource(L"PNG", L"colorCustom");
			colorWheel.SetWH(30, 30);
			colorWheel.w.SetDirect(30); colorWheel.h.SetDirect(30);
			colorWheel.enable.Initialization(true); colorWheel.pct.SetDirect(1);
			for (auto& word : pickerWords) { word.Initialization(0, 0, 1, 37, L"", 13); word.enable.Initialization(true); word.pct.SetDirect(1); }
			holdWord.Initialization(0, 0, 1, 28, L"", 12); holdWord.enable.Initialization(true); holdWord.pct.SetDirect(1);
			for (auto& swatch : swatchSurfaces)
			{
				InitShape(swatch, 4, 1);
				swatch.framePct->SetDirect(.18);
				swatch.frameLightPct.reset(); // 与主栏一致，点光透明度直接取色块自身pct。
				swatch.framePrimaryLightEnabled = false;
				swatch.frameLightColor = BarUiFrameLightColorEnum::Frame;
				swatch.frameCursorLightIntensityScale = .50; // 与主栏色块的鼠标点光强度一致。
				swatch.frameLightOpacitySource = BarUiFrameLightOpacitySourceEnum::ObjectPct;
			}
			for (unsigned id = 1; id<Count; ++id)
			{
				auto& p = buttons[id];
				p = std::make_shared<BarButtonClass>();
				p->hide = false;
				p->size = BarButtonSizeEnum::oneOne;
				InitShape(p->button, BarButtonCornerRadiusDip, 0);
				p->button.framePct->SetDirect(0);
				p->button.frameLightPct->SetDirect(0);
				p->button.frameLightColor = BarUiFrameLightColorEnum::Frame;
				p->button.framePrimaryLightEnabled = false;
				p->button.frameCursorLightIntensityScale = BarButtonCursorLightIntensity;
				p->icon.Initialization(0, 0, GetThemeColor(BarThemeColorEnum::TextPrimary), GetThemeColor(BarThemeColorEnum::TextPrimary));
				p->name.Initialization(0, 0, BarButtonOneSideDip, BarButtonOneSideDip, L"", 12, GetThemeColor(BarThemeColorEnum::TextPrimary));
				const auto resource = Resource(id);
				p->icon.enable.Initialization(!resource.empty());
				if (!resource.empty()) p->icon.InitializationFromResource(L"UI", resource);
				// 公共oneOne布局必须在载荷就绪后解析SVG比例；它默认没有文字槽。
				ConfigureBarButtonVisualTargets(*p, BarButtonVisualLayoutKind::StandardOneOne, 0, 1, true, id >= Swatch
					&& id<Tone || id == Custom ? std::optional<double>(15) : std::nullopt);
				p->name.enable.Initialization(true);
				if ((id >= Sides && id<Swatch) || id == SidesEntry)
				{
					p->name.SetStringImmediate(id == SidesEntry ? L"3" : std::to_wstring(id-Sides+3));
					p->name.x.SetDirect(0);
					p->name.y.SetDirect(0);
					p->name.w.SetDirect(BarButtonOneSideDip);
					p->name.h.SetDirect(BarButtonOneSideDip);
					p->name.size.SetDirect(13);
				}
				if (id == Custom) { p->icon.x.SetDirect(7.5); p->icon.y.SetDirect(7.5); }
				if (id == Rotate)
				{
					p->button.rw->SetDirect(BarButtonOneSideDip/2);
					p->button.rh->SetDirect(BarButtonOneSideDip/2);
				}
			}
			std::vector<std::shared_ptr<BarButtonClass>> list;
			for (unsigned id : ToolbarItems) if (id)
			{
				const auto group = std::find(GroupStarts.begin(), GroupStarts.end(), id);
				if (group != GroupStarts.end())
				{
					auto& d = dividers[static_cast<size_t>(group-GroupStarts.begin())];
					d = std::make_shared<BarButtonClass>();
					d->preset = BarButtonPresetEnum::Divider; d->size = BarButtonSizeEnum::oneTwo; d->hide = false;
					InitShape(d->button, BarUiDividerRadius, .30);
					d->button.framePct->SetDirect(0); d->button.frameLightPct->SetDirect(1);
					d->button.frameLightColor = BarUiFrameLightColorEnum::Frame;
					d->button.framePrimaryLightEnabled = false;
					d->button.frameCursorLightIntensityScale = BarUiDividerCursorLightIntensity;
					d->icon.enable.Initialization(false); d->name.enable.Initialization(false);
					list.push_back(d);
				}
				list.push_back(buttons[id]);
			}
			attributes.Replace(std::move(list));
		}

		void HoldToolbar(const Frame& frame, bool inProgress = false)
		{
			std::scoped_lock lock(inputMutex);
			if (!frame.snapshot || !SameToolbarScene(frame.snapshot->identity, toolbarScene)) return;
			if (!toolbarHoldFrozen)
			{
				// 必须从成功呈现的坐标冻结，不能锁存尚未上屏的候选位置。
				toolbarHoldX = frame.bounds.left+frame.toolbar.left*frame.scale;
				toolbarHoldY = frame.bounds.top+frame.toolbar.top*frame.scale;
				toolbarHoldFrozen = true; toolbarHoldResume = false;
			}
			toolbarOperationActive = inProgress;
			toolbarHoldUntil = toolbarNow()+std::chrono::seconds(3);
		}
		void ClearToolbarHold(bool animate)
		{
			std::scoped_lock lock(inputMutex);
			toolbarHoldResume = animate && (toolbarHoldFrozen || toolbarHoldResume);
			toolbarHoldFrozen = toolbarOperationActive = false;
			toolbarHoldUntil = {};
		}
		std::chrono::steady_clock::time_point TickToolbarIdle(std::chrono::steady_clock::time_point now)
		{
			std::scoped_lock lock(inputMutex);
			if (!toolbarHoldFrozen || toolbarOperationActive) return std::chrono::steady_clock::time_point::max();
			if (now < toolbarHoldUntil) return toolbarHoldUntil;
			toolbarHoldFrozen = false; toolbarHoldResume = true;
			return now; // 只返回一次到期信号，随后由既有残差动画续帧。
		}
		bool Emit(ShapeUiAction action)
		{
			std::function<void(const ShapeUiAction&)> sink;
			{
				std::scoped_lock lock(inputMutex);
				sink = actionSink;
			}
			if (sink)
			{
				sink(action);
				return true;
			}
			return SubmitShapeUiIntent(action);
		}

		Vec2 CanvasPoint(const Frame& frame, POINT p) const
		{
			return {static_cast<float>(p.x-frame.snapshot->canvasOriginX), static_cast<float>(p.y-frame.snapshot->canvasOriginY)};
		}

		std::uint64_t localBaseRevision = 0;
		// 与绘制线程同一EditSession算法：本地候选只驱动控制点/外框显示，不进入文档。
		void TrackLocal(const Frame& frame, Vec2 point, bool up, std::uint64_t upGeneration,
			std::optional<Vec2> predicted = {}, const ShapeGestureTransform* transform = nullptr, const ShapeGestureTransform* predictedTransform = nullptr)
		{
			if (!localEditActive || !frame.snapshot) return;
			std::shared_ptr<ShapeUiSnapshot> local;
			std::shared_ptr<GeometryResult> world;
			const auto revision=localBaseRevision+(++localRevision);
			const bool valid=transform ? localEdit.PreviewTransform(transform->pivot,transform->translation,transform->scale,transform->rotation,revision) : localEdit.Preview(point,revision);
			auto visual=localEdit; // 预测不能累计真实旋转角或成为下一次真实输入的基准。
			if (!up && valid)
			{
				if (predictedTransform) visual.PreviewTransform(predictedTransform->pivot,predictedTransform->translation,predictedTransform->scale,predictedTransform->rotation,revision);
				else if (predicted) visual.Preview(*predicted,revision);
			}
			if (valid) if (const auto& next = visual.Candidate())
			{
				local = std::make_shared<ShapeUiSnapshot>(*frame.snapshot);
				local->object = *next;
				local->editGestureActive = true;
				local->handles = BuildEditHandles(*next, {local->displayScale, .25f}, &local->frameRect);
				world = std::make_shared<GeometryResult>();
				if (BuildGeometry(*next, *world, {local->displayScale, .25f})) local->bounds = world->bounds;
				else world.reset();
			}
			std::scoped_lock lock(inputMutex);
			if (local)
			{
				localSnapshot = std::move(local);
				localGeometry = std::move(world);
			}
			// 抬手后保持最后候选直到绘制线程发布手势结束快照，避免松手瞬间回弹再追赶。
			if (up)
			{
				localEditActive = false;
				if (localSnapshot)
				{
					localPending = true;
					localUpTime = std::chrono::steady_clock::now();
					localUpGeneration = upGeneration;
				}
			}
		}

		ShapeEditorHit Hit(const Frame& frame, POINT p) const
		{
			ShapeEditorHit hit;
			if (!frame.visible || !frame.snapshot || !frame.snapshot->object || frame.snapshot->drawingPreview) return hit;
			hit.identity = frame.snapshot->identity;
			const double lx = (p.x-frame.bounds.left)/frame.scale, ly = (p.y-frame.bounds.top)/frame.scale;
			// 弹层同时充当输入屏障；外部按下只关闭最上层，不继续命中浮条或对象。
			const auto layerHit = [&](D2D1_RECT_F rect, unsigned first, unsigned last)
			{
				hit.occupied = true;
				if (!Inside(rect, lx, ly)) { hit.button = OutsideLayerHit; return; }
				for (unsigned id = first; id<last; ++id) if (HitTestBarButton(frame.buttons[id], lx, ly)) { hit.button = id; return; }
			};
			if (frame.snapshot->construction.active) {
				if (frame.snapshot->construction.preset!=PresetKind::Polyline && frame.snapshot->construction.preset!=PresetKind::Polygon) return hit;
				const Vec2 point=CanvasPoint(frame,p); float nearest=22*22*frame.scale*frame.scale;
				for(const auto& h:frame.visualHandles) { const float dx=point.x-h.position.x,dy=point.y-h.position.y,d=dx*dx+dy*dy; if(h.kind==EditHandleKind::Vertex && d<=nearest) {nearest=d;hit.occupied=true;hit.handle=h.id;} }
				return hit;
			}
			if (frame.pickerLayer)
			{
				layerHit(frame.picker, Tone, Tone+1);
				if (Inside(frame.picker, lx, ly))
				{
					if (HitTestBarButton(frame.buttons[PickerClose], lx, ly)) hit.button = PickerClose;
					else if (frame.input && frame.buttons[Tone].enabled && Inside(frame.palette, lx, ly) && pickerOpen.load()) hit.button = PaletteHit;
				}
				return hit;
			}
			if (frame.paletteLayer)
			{
				layerHit(frame.colors, Swatch, Tone);
				if (Inside(frame.colors, lx, ly) && HitTestBarButton(frame.buttons[Custom], lx, ly)) hit.button = Custom;
				return hit;
			}
			if (frame.sidesLayer) { layerHit(frame.parameters, Sides, Swatch); return hit; }
			for (unsigned id : ToolbarItems) if (id && HitTestBarButton(frame.buttons[id], lx, ly))
			{
				hit.occupied = true; hit.button = id; return hit;
			}
			if (Inside(frame.toolbar, lx, ly)) { hit.occupied = true; return hit; }
			if (!frame.input || frame.snapshot->construction.active) return hit;
			const Vec2 point = CanvasPoint(frame, p);
			float nearest = std::numeric_limits<float>::max();
			int priority = 4;
			for (const auto& h : frame.visualHandles)
			{
				const float dx = point.x-h.position.x, dy = point.y-h.position.y, d = dx*dx+dy*dy;
				const int rank = (h.kind == EditHandleKind::Rotate || h.kind == EditHandleKind::Move) ? 0 : h.kind == EditHandleKind::Scale ? 2 : 1;
				// 旋转钮优先；其余控制点取最近者，仅在等距时让专属点优先。
				const bool closer = rank == 0 ? priority != 0 || d < nearest
					: priority != 0 && (d < nearest || (d == nearest && (rank < priority || (h.kind == EditHandleKind::Focus && hit.handle == 100))));
				if (d <= 22*22*frame.scale*frame.scale && closer)
				{
					priority = rank;
					nearest = d;
					hit.occupied = true;
					hit.handle = h.id; hit.body = h.kind == EditHandleKind::Move;
				}
			}
			if (hit.occupied) return hit;
			const auto body = HitTest(*frame.snapshot->object, frame.geometry, point, static_cast<float>(3*frame.scale));
			const auto preset=EditorPreset(*frame.snapshot->object);
			const auto& t=frame.snapshot->object->transform;
			const float determinant=t.m11*t.m22-t.m12*t.m21;
			const Vec2 delta{point.x-t.dx,point.y-t.dy};
			const Vec2 local{(t.m22*delta.x-t.m21*delta.y)/determinant,(-t.m12*delta.x+t.m11*delta.y)/determinant};
			const auto& box=frame.snapshot->frameRect;
			const bool centerArea=preset!=PresetKind::Line && preset!=PresetKind::Arrow && box.valid
				&& local.x>=box.left && local.x<=box.right && local.y>=box.top && local.y<=box.bottom;
			if (centerArea || body.kind != HitKind::None)
			{
				hit.occupied = true;
				hit.body = true;
			}
			return hit;
		}

		void Configure(std::shared_ptr<const ShapeUiSnapshot> next, RECT nextWork, RECT nextBar)
		{
			if (!next || !next->object || (snapshot && !SameToolbarScene(next->identity, snapshot->identity))) ClearToolbarHold(false);
			{ std::scoped_lock lock(inputMutex); toolbarScene = next ? next->identity : ShapeUiIdentity{}; }
			const auto updateWidths = [&]
			{
				if (!snapshot || !snapshot->object) return;
				const auto state = GetStateModeSnapshot();
				std::array<float, 3> nextWidths{};
				for (int i = 0; i<3; ++i) nextWidths[i] = static_cast<float>(GetBarThicknessPresetMetrics(PenModeSelectEnum::IdtPenSoftPen, i,
					state).resolvedWidthDip*scale);
				if (nextWidths != widths)
				{
					widths = nextWidths;
					presentPending = true;
				}
			};
			std::shared_ptr<const ShapeUiSnapshot> local;
			std::shared_ptr<const GeometryResult> localWorld;
			{
				std::scoped_lock lock(inputMutex);
				if (localSnapshot)
				{
					const auto& a = localSnapshot->identity;
					bool drop = !next || !next->object || next->identity.session != a.session
						|| next->identity.documentGeneration != a.documentGeneration || next->identity.displayGeneration != a.displayGeneration
						|| next->identity.pageId != a.pageId;
					// 抬手后等绘制线程发布手势结束的新快照再交还；Commit被拒或丢失时限时回落到权威快照。
					if (!drop && localPending) drop = (next->identity.generation != localUpGeneration && !next->editGestureActive)
						|| std::chrono::steady_clock::now()-localUpTime>std::chrono::milliseconds(500);
					if (drop)
					{
						localSnapshot.reset();
						localGeometry.reset();
						localPending = false;
					}
					else
					{
						local = localSnapshot;
						localWorld = localGeometry;
					}
				}
				holdingLocal = localSnapshot && localPending;
			}
			tracking = local != nullptr;
			if (next == sourceSnapshot && local == sourceLocal && EqualRect(&work, &nextWork) && EqualRect(&bar, &nextBar))
			{
				updateWidths();
				return;
			}
			presentPending = true;
			sourceSnapshot = next;
			sourceLocal = local;
			if (local && next && next->object && local->object)
			{
				// 身份与场景沿用最新权威快照，对象/控制点/外框取本地候选；世界几何已由worker构建一次。
				auto merged = std::make_shared<ShapeUiSnapshot>(*next);
				merged->object = local->object;
				merged->handles = local->handles;
				merged->bounds = local->bounds;
				merged->frameRect = local->frameRect;
				merged->editGestureActive = true;
				if (localWorld) geometry = *localWorld;
				else
				{
					GeometryResult built;
					if (BuildGeometry(*merged->object, built, {merged->displayScale})) geometry = std::move(built);
				}
				next = std::move(merged);
			}
			else if (next && next->object && (!snapshot || snapshot->object != next->object || snapshot->displayScale != next->displayScale || !snapshot->object))
			{
				GeometryResult built;
				if (BuildGeometry(*next->object, built, {next->displayScale}))
				{
					geometry = std::move(built);
				}
				else next.reset();
			}
			if (!next || !next->object || (snapshot && next->identity.session != snapshot->identity.session))
			{
				std::scoped_lock lock(inputMutex);
				colorOpen = sidesOpen = pickerOpen = false;
				focused = 0;
			}
			requestedVisible = next && next->object.has_value();
			if (requestedVisible) snapshot = std::move(next);
			work = nextWork;
			bar = nextBar;
			const double nextScale = snapshot ? (std::max)(.5f, snapshot->displayScale) : 1;
			if (scale != nextScale) initialized = false;
			scale = nextScale;
			toolbarMotion.Retarget(requestedVisible && snapshot && !snapshot->construction.active && !snapshot->drawingPreview, requestedVisible ? .4 : .24);
			const bool entering = requestedVisible && (!candidate.snapshot || candidate.snapshot->identity.session != snapshot->identity.session
				|| candidate.snapshot->identity.objectId != snapshot->identity.objectId || !candidate.visible);
			if (entering)
			{
				handles.clear();
				initialized = false;
				toolbarBelow = true; handleDockSide = 0; dockDeterminant.reset(); dockTangentSign = 1;
				memberWanted.fill(false); dividerWanted.fill(false);
				memberBatch.Restart(0);
				toolbarMotion = {}; paletteMotion = {}; sidesMotion = {}; pickerMotion = {};
				toolbarMotion.Retarget(!snapshot->construction.active && !snapshot->drawingPreview, .4);
				colorOpen = sidesOpen = pickerOpen = false;
				{ std::scoped_lock lock(inputMutex); pickerDragging = pickerLocked = false; focused = 0; }
				holdOpacity.SetDirect(0); holdRingOpacity.SetDirect(0); holdTextMix.SetDirect(0); holdWasVisible = false;
				editOpacity.SetDirect(0);
			}
			const auto curve = requestedVisible ? BarUiCurveEnum::EaseOutCubic : BarUiCurveEnum::EaseInCubic;
			editOpacity.SetTar(requestedVisible ? 1 : 0, requestedVisible ? .28 : .16, std::nullopt, false, {curve, curve, 0.0, false});
			updateWidths();
		}

		bool Advance(std::chrono::steady_clock::time_point now)
		{
			const double dt = clock.Tick(now), speed = (std::max)(.01, static_cast<double>(BarUiAnimationSpeedRate));
			const BarUiAnimationAdvanceContextClass anim{dt, speed, static_cast<bool>(BarUiAnimationEnabled), false};
			active = false;
			bool changed = false;
			const double surfaceOpacity = GetThemeResidentSurfaceOpacity(BarMainBarFillOpacity);
			const double contentOpacity = GetThemeResidentContentOpacity();
			const double lightWeight = GetThemeLightWeight();
			if (surfaceOpacity != appliedSurfaceOpacity || contentOpacity != appliedContentOpacity || lightWeight != appliedLightWeight)
			{
				appliedSurfaceOpacity = surfaceOpacity;
				appliedContentOpacity = contentOpacity;
				appliedLightWeight = lightWeight;
				presentPending = true;
			}
			const auto step = [&](auto& value)
			{
				const auto r = BarUiAdvanceAnimation(value, anim);
				active |= r.active;
				changed |= r.changed;
			};
			// 新会话的首帧必须呈现紧凑态，不能把会话建立前的时钟间隔计入入场。
			if (initialized) changed |= toolbarMotion.Advance(anim);
			active |= toolbarMotion.Active();
			step(editOpacity);
			if (!snapshot || !snapshot->object)
			{
				candidate.visible = false;
				candidate.input = false;
				return changed;
			}
			const auto& object = *snapshot->object;
			const auto preset = EditorPreset(object);
			const auto capabilities = GetToolCapabilities(preset);
			const bool showStyle = capabilities.fill, showStroke = object.style.stroke || !showStyle;
			const bool openColor = requestedVisible && colorOpen.load(), openSides = requestedVisible && sidesOpen.load() && capabilities.regularSides;
			const bool openPicker = openColor && pickerOpen.load();
			unsigned hoverId, pressedId;
			{
				std::scoped_lock lock(inputMutex); hoverId = hover; pressedId = pressed;
			}
			paletteMotion.Retarget(openColor, openColor ? .4 : .24);
			sidesMotion.Retarget(openSides, openSides ? .4 : .24);
			pickerMotion.Retarget(openPicker, openPicker ? .4 : .24);
			for (auto* motion : {&paletteMotion, &sidesMotion, &pickerMotion}) { changed |= motion->Advance(anim); active |= motion->Active(); }
			const bool auxiliaryTypographyChanged = auxiliaryFontFamily != renderer.GetFontFamily() || auxiliaryLocale != renderer.GetTextLocale()
				|| auxiliaryFontGeneration != renderer.GetDeviceGeneration();
			bool auxiliaryLayoutChanged = false;
			for (size_t bit = 0; bit<AuxiliaryBits.size(); ++bit)
			{
				const auto label = ShapeAuxiliaryLabel(AuxiliaryBits[bit],preset);
				if (auxiliaryTypographyChanged || auxiliaryLabels[bit] != label || auxiliaryWidths[bit] <= 0)
				{
					auxiliaryLabels[bit] = label;
					const double width = ceil(renderer.MeasureText(label, 12).width)+44;
					auxiliaryLayoutChanged |= auxiliaryWidths[bit] != width;
					auxiliaryWidths[bit] = width;
				}
				auto& p = *buttons[Aux+bit];
				if (p.name.TargetContent() != label) changed |= p.name.SetStringImmediate(label);
				const double textWidth = auxiliaryWidths[bit]-44;
				p.name.enable.Initialization(true); p.name.x.SetDirect(12); p.name.y.SetDirect(0);
				p.name.w.SetDirect(textWidth); p.name.h.SetDirect(BarButtonOneSideDip); p.name.size.SetDirect(12); p.name.fontTargetSize = 12;
				p.icon.x.SetDirect(-(textWidth+4)/2); p.icon.y.SetDirect(0); p.icon.SetWH(20, 20);
				p.icon.w.SetDirect(p.icon.w.tar); p.icon.h.SetDirect(p.icon.h.tar);
			}
			for (size_t mode = 0; mode<constraintWidths.size(); ++mode)
			{
				const auto constraint = static_cast<PlanarConstraint>(mode == 3 ? 1 : mode);
				const auto label = mode == 3 && (preset == PresetKind::Prism || preset == PresetKind::Pyramid) ? I18n::getWOr(preset == PresetKind::Prism ? "UI/Bar/GeometryAttributes/RightPrismLabel" : "UI/Bar/GeometryAttributes/RightPyramidLabel",preset == PresetKind::Prism ? L"正棱柱" : L"正棱锥") : ShapePlanarConstraintLabel(constraint);
				if (auxiliaryTypographyChanged || constraintLabels[mode] != label || constraintWidths[mode] <= 0)
				{
					constraintLabels[mode] = label;
					const double width = ceil(renderer.MeasureText(label,12).width)+44+(mode==1?12:0);
					auxiliaryLayoutChanged |= constraintWidths[mode] != width; constraintWidths[mode] = width;
				}
				const unsigned id = mode == 3 ? TrapezoidMode : TriangleMode+static_cast<unsigned>(mode);
				auto& p = *buttons[id];
				if (p.name.TargetContent() != label) changed |= p.TransitionContent(mode == 3 ? (preset == PresetKind::Prism ? L"barShapePrism" : preset == PresetKind::Pyramid ? L"barShapePyramid" : L"barShapeTrapezoid") : L"barShapeTriangle",label);
				const double textWidth = constraintWidths[mode]-44-(mode==1?12:0);
				p.name.enable.Initialization(true); p.name.x.SetDirect(12-(mode==1?6:0)); p.name.y.SetDirect(0);
				p.name.w.SetDirect(textWidth); p.name.h.SetDirect(BarButtonOneSideDip); p.name.size.SetDirect(12); p.name.fontTargetSize = 12;
				p.icon.x.SetDirect(-(textWidth+4)/2-(mode==1?6:0)); p.icon.y.SetDirect(0); p.icon.SetWH(20,20);
				p.icon.w.SetDirect(p.icon.w.tar); p.icon.h.SetDirect(p.icon.h.tar);
			}
			for (unsigned id : {FlipHorizontal,FlipVertical}) {
				buttons[id]->name.SetStringImmediate(I18n::getWOr(id == FlipHorizontal ? "UI/Bar/GeometryAttributes/FlipHorizontalLabel" : "UI/Bar/GeometryAttributes/FlipVerticalLabel",id == FlipHorizontal ? L"水平翻转" : L"竖直翻转"));
				buttons[id]->name.enable.Initialization(false); // 保留语义名称，按钮仅显示专属图标。
			}
			for (unsigned id : {Rotate45,Rotate90}) {
				buttons[id]->name.SetStringImmediate(I18n::getWOr(id == Rotate45 ? "UI/Bar/GeometryAttributes/Rotate45Label" : "UI/Bar/GeometryAttributes/Rotate90Label",id == Rotate45 ? L"顺时针旋转45°" : L"顺时针旋转90°"));
				buttons[id]->name.enable.Initialization(false);
			}
			auxiliaryFontFamily = renderer.GetFontFamily(); auxiliaryLocale = renderer.GetTextLocale();
			auxiliaryFontGeneration = renderer.GetDeviceGeneration();
			std::array<bool, Count> wanted{};
			for (unsigned id : ToolbarItems) if (id)
			{
				// 圆在任意平面角度下镜像外观不变；其余预设保留屏幕轴翻转。
			wanted[id] = id == Color || id == Finish || ((id == FlipHorizontal || id == FlipVertical) && preset != PresetKind::Circle) || (id >= Style && id<Pattern && showStyle) || (id >= Pattern && id<Sides && showStroke)
					|| ((id == Rotate45 || id == Rotate90) && preset != PresetKind::Circle && preset < PresetKind::Cuboid)
					|| (id == SidesEntry && capabilities.regularSides) || (id >= Aux && id<Aux+3 && showStroke && (SupportedAuxiliary(preset)&AuxiliaryBits[id-Aux]) != Auxiliary::None)
					|| (id >= TriangleMode && id<TriangleMode+3 && preset == PresetKind::Triangle) || (id == TrapezoidMode && (preset == PresetKind::Trapezoid || preset == PresetKind::Prism || preset == PresetKind::Pyramid));
				buttons[id]->hide = !wanted[id];
			}
			std::array<bool, GroupStarts.size()> nextDividers{};
			// 分组顺序由浮条目录决定，不能拿非连续的控件ID范围判断可见性。
			const auto visible = [&](unsigned id) { return wanted[id]; };
			for (size_t group = 0; group<GroupStarts.size(); ++group)
			{
				const auto begin = std::find(ToolbarItems.begin(),ToolbarItems.end(),GroupStarts[group]);
				const auto end = group+1<GroupStarts.size() ? std::find(begin,ToolbarItems.end(),GroupStarts[group+1]) : ToolbarItems.end();
				nextDividers[group] = std::any_of(ToolbarItems.begin(),begin,visible) && std::any_of(begin,end,visible);
				dividers[group]->hide = !nextDividers[group];
			}
			auto layout = ResolveBarButtonListLayout(attributes.Snapshot(), BarButtonListLayoutKind::SingleRow);
			double widthShift = 0;
			for (auto& placement : layout.placements)
			{
				double width = placement.width;
				for (size_t bit = 0; bit<AuxiliaryBits.size(); ++bit) if (placement.button == buttons[Aux+bit]) width = auxiliaryWidths[bit];
				for (size_t mode = 0; mode<constraintWidths.size(); ++mode)
					if (placement.button == buttons[mode == 3 ? TrapezoidMode : TriangleMode+static_cast<unsigned>(mode)]) width = constraintWidths[mode];
				const double delta = width-placement.width;
				placement.x += widthShift+delta/2; placement.width = width;
				if (placement.button->IsVisible()) widthShift += delta;
			}
			layout.width += widthShift;
			const double available = (std::max)(1., (work.right-work.left)/scale-10);
			const bool nextTwoRows = true;
			// 两行按目录语义分组，不按非连续的按钮 ID 大小判断所在行。
			const std::array<unsigned, 10> firstRow{Color, Style, Style+1, Pattern, Pattern+1, Pattern+2, Pattern+3, Thickness, Thickness+1, Thickness+2};
			const auto rowOf = [&](unsigned id) { return std::find(firstRow.begin(),firstRow.end(),id) != firstRow.end() ? 0u : 1u; };
			std::array<double, Count> targetX{}, targetY{};
			std::array<double, GroupStarts.size()> targetDividerX{}, targetDividerY{};
			std::array<double,2> rowWidth{5,5};
			std::array<bool,2> rowHasButton{};
			for (const auto& p : layout.placements)
			{
				unsigned id = 0; size_t divider = dividers.size();
				for (unsigned k : ToolbarItems) if (buttons[k] == p.button) { id = k; break; }
				for (size_t k = 0; k<dividers.size(); ++k) if (dividers[k] == p.button) { divider = k; break; }
				if (!id && divider == dividers.size()) continue;
				const unsigned row = rowOf(id ? id : GroupStarts[divider]);
				const bool visible = id ? wanted[id] : nextDividers[divider] && rowHasButton[row];
				if (!id) { nextDividers[divider] = visible; dividers[divider]->hide = !visible; }
				const double px = rowWidth[row]+p.width/2, py = 5+BarButtonOneSideDip/2+row*(BarButtonOneSideDip+5);
				if (id) { targetX[id] = px; targetY[id] = py; if (visible) rowHasButton[row] = true; }
				else { targetDividerX[divider] = px; targetDividerY[divider] = py; }
				if (visible) rowWidth[row] += p.width+5;
			}
			const double firstWidth = rowWidth[0], secondWidth = rowWidth[1];
			// 完成按钮始终靠右下角；其分隔线随按钮一起对齐。
			const double finishShift = (std::max)(firstWidth,secondWidth)-secondWidth;
			targetX[Finish] += finishShift; targetDividerX.back() += finishShift;
			// 隐藏成员收向本帧前一可见成员；不存在前项时才使用后一项。
			for (size_t i = 0; i<ToolbarItems.size(); ++i) if (const unsigned id = ToolbarItems[i]; id && !wanted[id])
			{
				unsigned anchor = 0;
				for (size_t j = i; j>0; --j) if (ToolbarItems[j-1] && wanted[ToolbarItems[j-1]]) { anchor = ToolbarItems[j-1]; break; }
				if (!anchor) for (size_t j = i+1; j<ToolbarItems.size(); ++j) if (ToolbarItems[j] && wanted[ToolbarItems[j]]) { anchor = ToolbarItems[j]; break; }
				targetX[id] = targetX[anchor]; targetY[id] = targetY[anchor];
			}
			for (size_t di = 0; di<dividers.size(); ++di) if (!nextDividers[di])
			{
				unsigned anchor = Color;
				for (unsigned id : ToolbarItems) { if (id == GroupStarts[di]) break; if (wanted[id]) anchor = id; }
				targetDividerX[di] = targetX[anchor]; targetDividerY[di] = targetY[anchor];
			}
			const double targetWidth = nextTwoRows ? (std::max)(firstWidth, secondWidth) : layout.width;
			const double targetHeight = BarButtonOneSideDip+10+(nextTwoRows ? BarButtonOneSideDip+5 : 0);
			bool layoutChanged = !initialized || auxiliaryLayoutChanged || wanted != memberWanted || nextDividers != dividerWanted || nextTwoRows != twoRows;
			double batchDuration = .4, phase = 0;
			bool restart = false;
			if (layoutChanged && initialized)
			{
				if (memberBatch.CanJoin()) { batchDuration = memberBatch.GetRemainingDuration(); phase = memberBatch.GetProgress(); }
				else { memberBatch.Restart(.4); restart = true; }
			}
			const BarUiCurveSpecClass batchCurve{BarUiCurveEnum::EaseOutCubic, BarUiCurveEnum::EaseOutCubic, phase, phase>0};
			const auto layoutValue = [&](auto& value, double target)
			{
				if (!initialized) value.SetDirect(target);
				else if (layoutChanged) value.SetTar(target, batchDuration, std::nullopt, restart, batchCurve);
				step(value);
			};
			layoutValue(toolbarWidthValue, targetWidth); layoutValue(toolbarHeightValue, targetHeight);
			const double toolbarWidth = toolbarWidthValue.val, toolbarHeight = toolbarHeightValue.val;
			// 两行仍超宽时继续等比缩小；0.75只决定何时分行，不是最终缩放下限。
			toolbarFit = (std::min)({1., available/(std::max)(1., (std::max)(targetWidth, toolbarWidth)),
				available/(std::max)(targetHeight, toolbarHeight)});
			memberWanted = wanted; dividerWanted = nextDividers; twoRows = nextTwoRows;
			const auto& f = snapshot->frameRect;
			// 旋转后的屏幕包围框决定水平停靠边，不沿局部底边移动到侧面。
			const std::array<Vec2,4> corners{ApplyTransform(object.transform,{f.left,f.top}),ApplyTransform(object.transform,{f.right,f.top}),
				ApplyTransform(object.transform,{f.right,f.bottom}),ApplyTransform(object.transform,{f.left,f.bottom})};
			float minX=corners[0].x,maxX=minX,minY=corners[0].y,maxY=minY;
			for (const auto& p : corners) { minX=(std::min)(minX,p.x); maxX=(std::max)(maxX,p.x); minY=(std::min)(minY,p.y); maxY=(std::max)(maxY,p.y); }
			// 控件停靠仅在闲置且当前边超屏时换边；手势期间沿同一外框边跟随。
			const bool dockCapsule = std::any_of(snapshot->handles.begin(),snapshot->handles.end(),[](const auto& h){return h.kind==EditHandleKind::Rotate;});
			const bool hasDock = dockCapsule || std::any_of(snapshot->handles.begin(),snapshot->handles.end(),[](const auto& h){return h.kind==EditHandleKind::Move;});
			const auto previousDock=handleDock,previousTangent=handleTangent;
			if(hasDock && f.valid) {
				std::array<Vec2,4> centers{}, tangents{}, edges{}; std::array<double,4> overflow{};
				const Vec2 middle{(corners[0].x+corners[2].x)/2,(corners[0].y+corners[2].y)/2};
				for(unsigned side=0;side<4;++side) {
					const auto a=corners[side],b=corners[(side+1)%4]; const double length=(std::max)(1.,static_cast<double>(std::hypot(b.x-a.x,b.y-a.y)));
					tangents[side]={static_cast<float>((b.x-a.x)/length),static_cast<float>((b.y-a.y)/length)};
					edges[side]={(a.x+b.x)/2,(a.y+b.y)/2}; Vec2 normal{tangents[side].y,-tangents[side].x};
					if((edges[side].x-middle.x)*normal.x+(edges[side].y-middle.y)*normal.y<0) {normal.x=-normal.x;normal.y=-normal.y;}
					centers[side]={edges[side].x+static_cast<float>(30*scale)*normal.x,edges[side].y+static_cast<float>(30*scale)*normal.y};
					const double rx=(10+(dockCapsule?10*std::abs(tangents[side].x):0))*scale,ry=(10+(dockCapsule?10*std::abs(tangents[side].y):0))*scale;
					const double x=centers[side].x+snapshot->canvasOriginX,y=centers[side].y+snapshot->canvasOriginY;
					overflow[side]=(std::max)(0.,work.left+rx-x)+(std::max)(0.,x+rx-work.right)+(std::max)(0.,work.top+ry-y)+(std::max)(0.,y+ry-work.bottom);
				}
				const auto& transform=object.transform;
				const float determinant=transform.m11*transform.m22-transform.m12*transform.m21;
				if(!dockDeterminant && determinant<0) {
					// 重开镜像对象没有上一帧可参考，仍从屏幕上侧开始，而非镜像后的局部顶边。
					for(unsigned side=1;side<4;++side) if(centers[side].y<centers[handleDockSide].y) handleDockSide=side;
					const auto tangent=tangents[handleDockSide];
					dockTangentSign=tangent.x<0 || (tangent.x==0 && tangent.y<0) ? -1.f : 1.f;
				}
				if(dockDeterminant && determinant*(*dockDeterminant)<0) {
					// 镜像的是图形，不是编辑器：选靠近原控件的框边，并保持两个按钮的朝向顺序。
					unsigned nearest=0; float distance=std::numeric_limits<float>::max();
					for(unsigned side=0;side<4;++side) {
						const float d=std::hypot(centers[side].x-previousDock.x,centers[side].y-previousDock.y);
						if(d<distance) {nearest=side;distance=d;}
					}
					handleDockSide=nearest;
					dockTangentSign=tangents[nearest].x*previousTangent.x+tangents[nearest].y*previousTangent.y<0 ? -1.f : 1.f;
				}
				dockDeterminant=determinant;
				if(!tracking && pressedHandle==UINT32_MAX && overflow[handleDockSide]>0) {
					unsigned best=handleDockSide; double distance=0;
					for(unsigned side=0;side<4;++side) { const double d=std::hypot(centers[side].x-centers[handleDockSide].x,centers[side].y-centers[handleDockSide].y);
						if(overflow[side]<overflow[best] || (overflow[side]==overflow[best] && d<distance)) {best=side;distance=d;} }
					handleDockSide=best;
				}
				handleDock=centers[handleDockSide];handleTangent={tangents[handleDockSide].x*dockTangentSign,tangents[handleDockSide].y*dockTangentSign};handleEdge=edges[handleDockSide];
				if(tracking || pressedHandle!=UINT32_MAX) {handleDock.x+=dockFitOffset.x;handleDock.y+=dockFitOffset.y;} else dockFitOffset={};
				if(!tracking && pressedHandle==UINT32_MAX && overflow[handleDockSide]>0) {
					const float rx=static_cast<float>((10+(dockCapsule?10*std::abs(handleTangent.x):0))*scale),ry=static_cast<float>((10+(dockCapsule?10*std::abs(handleTangent.y):0))*scale);
					handleDock.x=std::clamp(handleDock.x,work.left-snapshot->canvasOriginX+rx,(std::max)(work.left-snapshot->canvasOriginX+rx,work.right-snapshot->canvasOriginX-rx));
					handleDock.y=std::clamp(handleDock.y,work.top-snapshot->canvasOriginY+ry,(std::max)(work.top-snapshot->canvasOriginY+ry,work.bottom-snapshot->canvasOriginY-ry));
					dockFitOffset={handleDock.x-centers[handleDockSide].x,handleDock.y-centers[handleDockSide].y};
				}
			}
			changed|=previousDock!=handleDock || previousTangent!=handleTangent;
			float topDockY=minY,bottomDockY=maxY;
			// 旋转钮伸出外框，顶部停靠须在其外再留间距，否则浮条会遮住可操作区域。
			for (const auto& h : snapshot->handles) if(h.kind==EditHandleKind::Rotate || h.kind==EditHandleKind::Move) { topDockY=(std::min)(topDockY,handleDock.y-static_cast<float>(20*scale)); bottomDockY=(std::max)(bottomDockY,handleDock.y+static_cast<float>(20*scale)); }
			const Vec2 topAnchor{(minX+maxX)/2,topDockY}, bottomAnchor{(minX+maxX)/2,bottomDockY};
			const double fittedWidth = toolbarWidth*toolbarFit, fittedHeight = toolbarHeight*toolbarFit;
			const EraserAttributeRect safeWork{work.left/scale+5, work.top/scale+5, work.right/scale-5, work.bottom/scale-5};
			const auto candidatePose = [&](bool below)
			{
				const auto& anchor = below ? bottomAnchor : topAnchor;
				// 横向钳制不代表上下空间不足，必须在判断换边之前完成。
				const double x=(std::clamp)((snapshot->canvasOriginX+anchor.x)/scale-fittedWidth/2,safeWork.left,(std::max)(safeWork.left,safeWork.right-fittedWidth));
				return EraserSurfacePose{toolbarFit, x,
					(snapshot->canvasOriginY+anchor.y)/scale+(below ? 16 : -16-fittedHeight)};
			};
			const auto fits = [&](const EraserSurfacePose& pose)
			{
				const auto rect = pose.Apply({0, 0, toolbarWidth, toolbarHeight});
				constexpr double epsilon = .000001;
				if (rect.left<safeWork.left-epsilon || rect.top<safeWork.top-epsilon
					|| rect.right>safeWork.right+epsilon || rect.bottom>safeWork.bottom+epsilon) return false;
				RECT screen{static_cast<LONG>(std::floor(rect.left*scale)), static_cast<LONG>(std::floor(rect.top*scale)),
					static_cast<LONG>(std::ceil(rect.right*scale)), static_cast<LONG>(std::ceil(rect.bottom*scale))}, overlap{};
				return EmptyRect(bar) || !IntersectRect(&overlap, &screen, &bar);
			};
			const bool currentFits = fits(candidatePose(toolbarBelow));
			const bool nextBelow = !currentFits && fits(candidatePose(!toolbarBelow)) ? !toolbarBelow : toolbarBelow;
			int nextPlacement = nextBelow ? 0 : 1;
			auto position = FitEditorPose(candidatePose(nextBelow), {0, 0, toolbarWidth, toolbarHeight}, safeWork);
			if (!currentFits && !fits(candidatePose(!nextBelow)))
			{
				// 两侧都放不下时优先避开主栏和图形，再选择离当前显示位置最近的候选，避免来回跳边。
				const EraserAttributeRect shapeRect{(snapshot->canvasOriginX+minX)/scale,(snapshot->canvasOriginY+minY)/scale,
					(snapshot->canvasOriginX+maxX)/scale,(snapshot->canvasOriginY+maxY)/scale};
				const EraserAttributeRect mainRect{bar.left/scale,bar.top/scale,bar.right/scale,bar.bottom/scale};
				auto overlap = [](const EraserAttributeRect& a,const EraserAttributeRect& b) { return (std::max)(0.,(std::min)(a.right,b.right)-(std::max)(a.left,b.left))*(std::max)(0.,(std::min)(a.bottom,b.bottom)-(std::max)(a.top,b.top)); };
				auto score = [&](const EraserSurfacePose& pose) { const auto r=pose.Apply({0,0,toolbarWidth,toolbarHeight}); return std::array<double,3>{overlap(r,mainRect),overlap(r,shapeRect),std::hypot(pose.x-toolbarX.val,pose.y-toolbarY.val)}; };
				const std::array<EraserSurfacePose,4> choices{candidatePose(nextBelow),candidatePose(!nextBelow),
					EraserSurfacePose{toolbarFit,shapeRect.left-16-fittedWidth,EraserRectCenterY(shapeRect)-fittedHeight/2},
					EraserSurfacePose{toolbarFit,shapeRect.right+16,EraserRectCenterY(shapeRect)-fittedHeight/2}};
				for (size_t index=0;index<choices.size();++index) { auto fitted=FitEditorPose(choices[index],{0,0,toolbarWidth,toolbarHeight},safeWork); if (score(fitted)<score(position)) { position=fitted; nextPlacement=index<2 ? (index==0 ? (nextBelow?0:1) : (nextBelow?1:0)) : static_cast<int>(index); } }
			}
			toolbarFit = position.scale;
			TickToolbarIdle(now);
			bool frozen, resume; double frozenX, frozenY;
			{
				std::scoped_lock lock(inputMutex);
				frozen = toolbarHoldFrozen; resume = toolbarHoldResume; toolbarHoldResume = false;
				frozenX = toolbarHoldX/scale; frozenY = toolbarHoldY/scale;
			}
			// 保护期只更新期望停靠点；解除时同一分支也从冻结点消退残差。
			if (!initialized || frozen) { dockResidualX.SetDirect(0); dockResidualY.SetDirect(0); }
			else if (resume || toolbarPlacement != nextPlacement) {
				dockResidualX.SetDirect((resume ? frozenX : static_cast<double>(toolbarX.val))-position.x); dockResidualY.SetDirect((resume ? frozenY : static_cast<double>(toolbarY.val))-position.y);
				const BarUiCurveSpecClass curve{BarUiCurveEnum::EaseOutCubic,BarUiCurveEnum::EaseOutCubic,0,false};
				dockResidualX.SetTar(0,.4,std::nullopt,true,curve); dockResidualY.SetTar(0,.4,std::nullopt,true,curve);
			}
			// 到期前的空闲间隔不计入新残差动画，首帧仍从冻结位置开始。
			const BarUiAnimationAdvanceContextClass dockAnim{resume ? 0.0 : dt,speed,static_cast<bool>(BarUiAnimationEnabled),false};
			for (auto* value : {&dockResidualX,&dockResidualY}) { const auto r=BarUiAdvanceAnimation(*value,dockAnim); active|=r.active; changed|=r.changed; }
			if (!frozen) { toolbarPlacement=nextPlacement; toolbarBelow=nextBelow; }
			dockTargetX=position.x; dockTargetY=position.y;
			if (frozen) { position.x=frozenX; position.y=frozenY; position=FitEditorPose(position,{0,0,toolbarWidth,toolbarHeight},safeWork); }
			toolbarX.SetDirect(position.x+dockResidualX.val); toolbarY.SetDirect(position.y+dockResidualY.val);
			const auto& anchor = toolbarBelow ? bottomAnchor : topAnchor;
			const double anchorX = ((snapshot->canvasOriginX+anchor.x)/scale-toolbarX.val)/toolbarFit;
			const double anchorY = ((snapshot->canvasOriginY+anchor.y)/scale-toolbarY.val)/toolbarFit;
			auto barFrame = ResolveBarAttributePanelFrame(toolbarMotion, {0, 0, toolbarWidth, toolbarHeight}, {anchorX, anchorY, anchorX, anchorY}, 60);
			std::array<EraserSurfacePose, 4> poses{};
			poses[0] = FitEditorPose(ComposeEraserPose({toolbarFit, toolbarX.val, toolbarY.val}, barFrame.pose), {0, 0, toolbarWidth, toolbarHeight}, safeWork);
			if (frozen)
			{
				// 显隐和尺寸动画照常推进，只锁存最终表面的左上锚点。
				poses[0].x=position.x; poses[0].y=position.y;
				poses[0]=FitEditorPose(poses[0],{0,0,toolbarWidth,toolbarHeight},safeWork);
				std::scoped_lock lock(inputMutex); toolbarHoldX=poses[0].x*scale; toolbarHoldY=poses[0].y*scale;
			}
			const bool below = toolbarBelow;
			const auto layerPose = [&](const BarAttributePanelMotion& motion, EraserAttributeRect panel, EraserAttributeRect parent,
				double triggerX, double triggerY, bool preferBelow, bool& placedBelow)
			{
				const double fit = (std::min)({1., safeWork.Width()/panel.Width(), safeWork.Height()/panel.Height()});
				placedBelow = preferBelow;
				double top = preferBelow ? parent.bottom+8 : parent.top-8-panel.Height()*fit;
				if (top<safeWork.top || top+panel.Height()*fit>safeWork.bottom) { placedBelow = !preferBelow; top = placedBelow ? parent.bottom+8 : parent.top-8-panel.Height()*fit; }
				auto base = FitEditorPose({fit, triggerX-panel.Width()*fit/2, top}, panel, safeWork);
				const double ax = (triggerX-base.x)/fit, ay = (triggerY-base.y)/fit;
				const auto frame = ResolveBarAttributePanelFrame(motion, panel, {ax, ay, ax, ay}, 60);
				return FitEditorPose(ComposeEraserPose(base, frame.pose), panel, safeWork);
			};
			const auto barRect = poses[0].Apply({0, 0, toolbarWidth, toolbarHeight});
			bool layerBelow = below, sidesBelow = below, pickerBelow = below;
			const double sidesPanelWidth = preset == PresetKind::RegularPolygon ? 192.5 : 117.5, sidesPanelHeight = preset == PresetKind::RegularPolygon ? 117.5 : 80;
			poses[1] = layerPose(sidesMotion, {0, 0, sidesPanelWidth, sidesPanelHeight}, barRect, poses[0].x+targetX[SidesEntry]*poses[0].scale,
				poses[0].y+targetY[SidesEntry]*poses[0].scale, below, sidesBelow);
			poses[2] = layerPose(paletteMotion, {0, 0, 215, 75}, barRect, poses[0].x+targetX[Color]*poses[0].scale,
				poses[0].y+targetY[Color]*poses[0].scale, below, layerBelow);
			const auto paletteRect = poses[2].Apply({0, 0, 215, 75});
			if (pickerMotion.Visible()) poses[3] = ResolveEditorPickerPose(pickerMotion, safeWork, barRect, paletteRect,
				poses[2].x+195*poses[2].scale, poses[2].y+55*poses[2].scale, layerBelow, below, pickerBelow);
			RECT nextBounds{};
			const auto include = [&](EraserAttributeRect rect)
			{
				RECT pixels{static_cast<LONG>(std::floor((rect.left-8)*scale)), static_cast<LONG>(std::floor((rect.top-8)*scale)),
					static_cast<LONG>(std::ceil((rect.right+8)*scale)), static_cast<LONG>(std::ceil((rect.bottom+8)*scale))};
				if (EmptyRect(nextBounds)) nextBounds = pixels; else UnionRect(&nextBounds, &nextBounds, &pixels);
			};
			include(barRect);
			if(hasDock) include({(snapshot->canvasOriginX+handleDock.x)/scale-24,(snapshot->canvasOriginY+handleDock.y)/scale-24,(snapshot->canvasOriginX+handleDock.x)/scale+24,(snapshot->canvasOriginY+handleDock.y)/scale+24});
			// 退场点仍在绘制，窗口不能只按新快照中的点收缩。
			for(const auto& v:handles) if(v->opacity.val>0) include({(snapshot->canvasOriginX+v->position.x)/scale-18,(snapshot->canvasOriginY+v->position.y)/scale-18,(snapshot->canvasOriginX+v->position.x)/scale+18,(snapshot->canvasOriginY+v->position.y)/scale+18});
			if (sidesMotion.Visible()) include(poses[1].Apply({0, 0, sidesPanelWidth, sidesPanelHeight}));
			if (paletteMotion.Visible()) include(paletteRect);
			if (pickerMotion.Visible()) include(poses[3].Apply({0, 0, 300, 209}));
			// 同窗弹层的窗外点击也必须被消费；仅在弹层期间为工作区铺1alpha输入屏障。
			if (paletteMotion.Visible() || sidesMotion.Visible() || pickerMotion.Visible()) UnionRect(&nextBounds, &nextBounds, &work);
			for (const auto& h : snapshot->handles)
			{
				nextBounds.left = (std::min)(nextBounds.left, static_cast<LONG>(std::floor(snapshot->canvasOriginX+h.position.x-56*scale)));
				nextBounds.right = (std::max)(nextBounds.right, static_cast<LONG>(std::ceil(snapshot->canvasOriginX+h.position.x+56*scale)));
				nextBounds.top = (std::min)(nextBounds.top, static_cast<LONG>(std::floor(snapshot->canvasOriginY+h.position.y-56*scale)));
				nextBounds.bottom = (std::max)(nextBounds.bottom, static_cast<LONG>(std::ceil(snapshot->canvasOriginY+h.position.y+56*scale)));
			}
			if (snapshot->bounds.valid)
			{
				const auto& b = snapshot->bounds;
				RECT body{static_cast<LONG>(snapshot->canvasOriginX+b.left-8*scale), static_cast<LONG>(snapshot->canvasOriginY+b.top-8*scale),
					static_cast<LONG>(snapshot->canvasOriginX+b.right+8*scale), static_cast<LONG>(snapshot->canvasOriginY+b.bottom+8*scale)};
				UnionRect(&nextBounds, &nextBounds, &body);
			}
			if (snapshot->frameRect.valid && !snapshot->construction.active)
			{
				const auto& f = snapshot->frameRect;
				const std::array<Vec2, 4> corners{{{f.left, f.top}, {f.right, f.top}, {f.right, f.bottom}, {f.left, f.bottom}}};
				for (const auto local : corners)
				{
					const auto point = ApplyTransform(object.transform, local);
					nextBounds.left = (std::min)(nextBounds.left, static_cast<LONG>(std::floor(snapshot->canvasOriginX+point.x-(2.5*scale+1))));
					nextBounds.top = (std::min)(nextBounds.top, static_cast<LONG>(std::floor(snapshot->canvasOriginY+point.y-(2.5*scale+1))));
					nextBounds.right = (std::max)(nextBounds.right, static_cast<LONG>(std::ceil(snapshot->canvasOriginX+point.x+(2.5*scale+1))));
					nextBounds.bottom = (std::max)(nextBounds.bottom, static_cast<LONG>(std::ceil(snapshot->canvasOriginY+point.y+(2.5*scale+1))));
				}
			}
			if (tracking && initialized && !EmptyRect(bounds))
			{
				// 拖动期间窗口只按块外扩、不收缩，避免逐帧重建D2D target；手势结束后再按内容收回。
				RECT grown{};
				UnionRect(&grown, &bounds, &nextBounds);
				if (!EqualRect(&grown, &bounds))
				{
					const double block = (std::max)(64., std::round(256*scale));
					if (grown.left<bounds.left) grown.left = static_cast<LONG>(std::floor(grown.left/block)*block);
					if (grown.top<bounds.top) grown.top = static_cast<LONG>(std::floor(grown.top/block)*block);
					if (grown.right>bounds.right) grown.right = static_cast<LONG>(std::ceil(grown.right/block)*block);
					if (grown.bottom>bounds.bottom) grown.bottom = static_cast<LONG>(std::ceil(grown.bottom/block)*block);
				}
				nextBounds = grown;
			}
			if (!EqualRect(&bounds, &nextBounds)) changed = true;
			bounds = nextBounds;
			Frame next;
			next.snapshot = snapshot; next.geometry = geometry; next.bounds = bounds; next.work = work; next.mainBar = bar; next.scale = scale;
			next.widths = widths; next.opacity = toolbarMotion.Opacity();
			next.visible = toolbarMotion.Visible() || editOpacity.val>.000001;
			// 已呈现的圆点立即可拖；浮条的入场动画不能把首个按下漏给画布。
			next.input = requestedVisible && editOpacity.val>.000001;
			const bool toolbarInput = !toolbarMotion.Active() && toolbarMotion.Opacity()>.99 && editOpacity.IsSame();
			next.darkTone = darkTone; next.pickerBelow = pickerBelow;
			next.paletteLayer = paletteMotion.Visible(); next.sidesLayer = sidesMotion.Visible(); next.pickerLayer = pickerMotion.Visible();
			next.panelOpacity = {toolbarMotion.Opacity(), sidesMotion.Opacity(), paletteMotion.Opacity(), pickerMotion.Opacity()};
			for (auto& pose : poses) { pose.x -= bounds.left/scale; pose.y -= bounds.top/scale; }
			next.poses = poses;
			next.toolbar = EditorRect(poses[0].Apply({0, 0, toolbarWidth, toolbarHeight}));
			if (next.sidesLayer) next.parameters = EditorRect(poses[1].Apply({0, 0, sidesPanelWidth, sidesPanelHeight}));
			if (next.paletteLayer) next.colors = EditorRect(poses[2].Apply({0, 0, 215, 75}));
			if (next.pickerLayer) { next.picker = EditorRect(poses[3].Apply({0, 0, 300, 209})); next.palette = EditorRect(poses[3].Apply({5, 40, 295, 172})); }
			PlaceShape(toolbar, 0, 0, toolbarWidth, toolbarHeight); PlaceShape(sidesPanel, 0, 0, sidesPanelWidth, sidesPanelHeight);
			PlaceShape(palettePanel, 0, 0, 215, 75); PlaceShape(pickerPanel, 0, 0, 300, 209);
			for (unsigned k = 0; k<GeometrySidesCount; ++k) { wanted[Sides+k] = next.sidesLayer && (preset == PresetKind::RegularPolygon || k < 6); const unsigned columns = preset == PresetKind::RegularPolygon ? 5 : 3; targetX[Sides+k] = 21.25+(k%columns)*37.5; targetY[Sides+k] = 21.25+(k/columns)*37.5; }
			for (unsigned k = 0; k<11; ++k) { wanted[Swatch+k] = next.paletteLayer; targetX[Swatch+k] = 20+(k%6)*35; targetY[Swatch+k] = 20+(k/6)*35; }
			wanted[Custom] = next.paletteLayer; targetX[Custom] = 195; targetY[Custom] = 55;
			wanted[Tone] = wanted[PickerClose] = next.pickerLayer; targetX[Tone] = 20; targetY[Tone] = 20; targetX[PickerClose] = 280; targetY[PickerClose] = 20;
			buttons[SidesEntry]->name.SetStringImmediate(std::to_wstring(object.construction ? object.construction->tool.sides : 3));
			const COLORREF currentColor = ShapeRgbToUiColor(object.style.stroke ? object.style.strokeRgb : object.style.fillRgb);
			bool rowStable = toolbarWidthValue.IsSame() && toolbarHeightValue.IsSame();
			for (unsigned id = 1; id<Count; ++id) if (id != Rotate)
			{
				auto& p = *buttons[id]; const unsigned panel = EditorPanel(id);
				const bool selected = id == Color ? openColor : id == SidesEntry ? openSides : id >= Aux && id<Aux+3 ? (object.style.auxiliary&AuxiliaryBits[id-Aux]) != Auxiliary::None
					: id >= TriangleMode && id<TriangleMode+3 ? object.construction && object.construction->tool.constraint == static_cast<PlanarConstraint>(id-TriangleMode)
					: id == TrapezoidMode ? object.construction && ((preset == PresetKind::Prism || preset == PresetKind::Pyramid) ? object.construction->tool.rightSolid : object.construction->tool.constraint == PlanarConstraint::Isosceles)
					: id >= Swatch && id<Tone ? currentColor == (GetPresetColor(static_cast<BarThemePresetColorEnum>(id-Swatch))&0x00FFFFFF)
					: id == Custom ? !IsBarPresetColor(currentColor) : id >= Style && id<Pattern ? (id == Style ? object.style.stroke
						: id == Style+1 ? object.style.fill : object.style.stroke && object.style.fill)
					: id >= Pattern && id<Thickness ? static_cast<unsigned>(object.style.linePattern) == id-Pattern
					: id >= Thickness && id<Sides ? std::abs(object.style.width*ShapeWidthScale(object)-widths[id-Thickness])<.01
					: id >= Sides && id<Swatch ? object.construction && object.construction->tool.sides == id-Sides+3 : false;
				p.state->state = selected ? BarWidgetState::Selected : BarWidgetState::None;
				const bool badge = (id >= Swatch && id<Tone) || id == Custom;
				RetargetBarButtonInteractionVisual(p, wanted[id], true, selected, panel ? .12 : batchDuration,
					badge ? std::optional<COLORREF>(GetThemeColor(BarThemeColorEnum::TextPrimary)) : std::nullopt, badge ? selected : !Resource(id).empty());
				if (!panel && layoutChanged) { p.icon.pct.SetTar(p.icon.pct.tar, batchDuration, std::nullopt, true, batchCurve); p.name.pct.SetTar(p.name.pct.tar, batchDuration, std::nullopt, true, batchCurve); }
				if (id == Tone) p.icon.TransitionToResource(L"UI", darkTone ? L"colorMoon" : L"colorSun");
				if (id == hoverId && id != appliedHover) StartBarButtonHoverVisual(p);
				if (id == appliedHover && id != hoverId) StopBarButtonHoverVisual(p, false);
				if (id == pressedId || id == appliedPressed) SetBarButtonPressedVisual(p, id == pressedId);
				if (!panel) { layoutValue(x[id], targetX[id]); layoutValue(y[id], targetY[id]); }
				else { x[id].SetDirect(targetX[id]); y[id].SetDirect(targetY[id]); }
				if (!initialized || panel) { p.icon.pct.SetDirect(wanted[id] && (!badge || selected) ? 1 : 0); p.name.pct.SetDirect(wanted[id] ? 1 : 0); }
				if (!initialized) { p.button.pct.SetDirect(p.button.pct.tar); p.button.frameLightPct->SetDirect(p.button.frameLightPct->tar); }
				if (badge) { p.button.pct.SetDirect(0); p.button.frameLightPct->SetDirect(0); }
				const auto result = AdvanceBarButtonVisual(p, {dt, speed, static_cast<bool>(BarUiAnimationEnabled), false, wanted[id] || HasOpacity(p), true});
				active |= result.active; changed |= result.changed; p.hide = !wanted[id] && !HasOpacity(p);
				const bool auxiliary = id >= Aux && id<Aux+3;
				const bool constraint = id >= TriangleMode && id<TriangleMode+3 || id == TrapezoidMode;
				const double side = badge || panel == 3 ? 30 : BarButtonOneSideDip;
				const double width = auxiliary ? auxiliaryWidths[id-Aux] : constraint ? constraintWidths[id == TrapezoidMode ? 3 : id-TriangleMode] : side;
				PlaceShape(p.button, x[id].val-width/2, y[id].val-side/2, width, side);
				next.buttons[id] = ResolveBarButtonHitSnapshot(p, {p.button.inhX, p.button.inhY});
				next.buttons[id].boundsDip = EditorRect(poses[panel].Apply(EditorRect(next.buttons[id].boundsDip)));
				next.buttons[id].visible = next.visible && !p.hide.load();
				const bool panelInput = panel == 1 ? openSides && !sidesMotion.Active() : panel == 2 ? openColor && !paletteMotion.Active() && !next.pickerLayer : panel == 3 ? openPicker && !pickerMotion.Active() : !next.paletteLayer && !next.sidesLayer;
				next.buttons[id].enabled &= wanted[id] && next.input && toolbarInput && panelInput;
				if (!panel) rowStable &= x[id].IsSame() && y[id].IsSame() && p.icon.pct.IsSame() && p.name.pct.IsSame();
			}
			for (unsigned id : ToolbarItems) if (id) next.buttons[id].enabled &= rowStable;
			for (size_t di = 0; di<dividers.size(); ++di)
			{
				auto& d = *dividers[di]; layoutValue(dividerX[di], targetDividerX[di]); layoutValue(dividerY[di], targetDividerY[di]);
				if (!initialized) { d.button.pct.SetDirect(nextDividers[di] ? .30 : 0); d.button.frameLightPct->SetDirect(nextDividers[di] ? 1 : 0); }
				else if (layoutChanged) { d.button.pct.SetTar(nextDividers[di] ? .30 : 0, batchDuration, std::nullopt, restart, batchCurve); d.button.frameLightPct->SetTar(nextDividers[di] ? 1 : 0, batchDuration, std::nullopt, restart, batchCurve); }
				const auto result = AdvanceBarButtonVisual(d, {dt, speed, static_cast<bool>(BarUiAnimationEnabled), false, true, false});
				active |= result.active; changed |= result.changed;
				PlaceShape(d.button, dividerX[di].val-.5, dividerY[di].val-(BarButtonOneSideDip-4)/2, 1, BarButtonOneSideDip-4);
				d.button.fill->SetDirect(GetThemeColor(BarThemeColorEnum::Divider));
				d.button.frame->SetDirect(GetThemeColor(BarThemeColorEnum::Divider));
			}
			if (static_cast<bool>(BarUiAnimationEnabled)) memberBatch.Advance(dt, speed); else memberBatch.Restart(0);
			active |= memberBatch.IsActive(); appliedHover = hoverId; appliedPressed = pressedId;
			// 子控件与面板逐帧共用同一材质权重，避免浅色按钮仍解析为深色点光。
			for (auto& p : buttons) if (p) p->button.lightMaterial = lightWeight;
			for (auto& p : dividers) p->button.lightMaterial = lightWeight;
			for (auto& p : swatchSurfaces) p.lightMaterial = lightWeight;
			pickerPreview.lightMaterial = holdSurface.lightMaterial = lightWeight;
			for (auto* p : {&toolbar, &sidesPanel, &palettePanel, &pickerPanel})
			{
				p->themeSurface = true; p->residentSurface = p == &toolbar; p->lightMaterial = lightWeight;
				p->lightFillOpacityScale = p == &toolbar ? BarThemeMaterial::LightResidentSurfaceOpacityScale : 1;
				p->pct.SetDirect(p == &toolbar ? BarMainBarFillOpacity : BarDrawAttributeSurfaceOpacity);
				p->fill->SetDirect(GetThemeColor(BarThemeModeEnum::Dark, BarThemeColorEnum::Surface));
				p->frame->SetDirect(GetThemeColor(BarThemeModeEnum::Dark, BarThemeColorEnum::SurfaceFrame));
			}
			next.pickerColor = currentColor;
			bool draggingPicker = false;
			bool exact = false;
			ProjectBarColorPickerColor(currentColor, next.darkTone, pickerBelow, next.markerX, next.markerY, exact);
			next.markerVisible = exact && !IsBarPresetColor(currentColor);
			{
				std::scoped_lock lock(inputMutex);
				draggingPicker = pickerDragging;
				if (pickerDragging) { next.pickerColor = pickerDragColor; next.markerX = pickerDragX; next.markerY = pickerDragY; next.markerVisible = true; }
				const double still = pickerDragging ? std::chrono::duration<double>(now-pickerStableTime).count() : 0;
				holdProgress = pickerDragging ? (std::clamp)((still-.5)/1.5, 0., 1.) : 0;
				if (holdProgress >= 1) pickerLocked = true;
				const bool holdVisible = pickerDragging && (still >= .5 || pickerLocked);
				if (holdVisible && !holdWasVisible) holdOnTop = next.markerY >= .5;
				holdWasVisible = holdVisible;
				holdOpacity.SetTar(holdVisible ? 1 : 0, .18); holdRingOpacity.SetTar(holdVisible && !pickerLocked ? 1 : 0, .18);
				holdTextMix.SetTar(pickerLocked ? 1 : 0, .18); active |= pickerDragging;
			}
			toneMix.SetTar(darkTone.load() ? 1 : 0, .4);
			step(toneMix); step(holdOpacity); step(holdRingOpacity); step(holdTextMix);
			const std::array<double, 4> display{double(GetRValue(next.pickerColor)), double(GetGValue(next.pickerColor)), double(GetBValue(next.pickerColor)),
				100.*(object.style.stroke ? object.style.strokeOpacity : object.style.fillOpacity)};
			for (size_t i = 0; i<display.size(); ++i)
			{
				if (!initialized || draggingPicker) pickerDisplay[i].SetDirect(display[i]);
				else pickerDisplay[i].SetTar(display[i], BarButtonDefaultOperationDurationSeconds);
				step(pickerDisplay[i]);
			}
			const BarUiCurveSpecClass stateCurve{BarUiCurveEnum::EaseOutCubic, BarUiCurveEnum::EaseOutCubic, 0.0, false};
			for (auto& v : handles) v->wanted = false;
			auto uiHandles = snapshot->handles;
			for (const auto& h : snapshot->handles) if (h.kind == EditHandleKind::Rotate) { uiHandles.push_back({0,EditHandleKind::Move,h.position}); break; }
			const bool capsule = std::any_of(uiHandles.begin(),uiHandles.end(),[](const auto& h){return h.kind==EditHandleKind::Rotate;});
			// 焦点成对选位：短轴镜像优先，不能因逐点贪心使左右落在任意两侧。
			std::array<Vec2,2> focusOffsets{};std::vector<EditHandle> focusPair;bool focusPairFits=false;
			for(const auto& h:uiHandles)if(h.kind==EditHandleKind::Focus)focusPair.push_back(h);
			if(focusPair.size()==2) {
				Vec2 axis{focusPair[0].position.x-focusPair[1].position.x,focusPair[0].position.y-focusPair[1].position.y};
				double length=std::hypot(axis.x,axis.y);if(length<.001){axis={object.transform.m11,object.transform.m12};length=(std::max)(.001,static_cast<double>(std::hypot(axis.x,axis.y)));}
				axis={static_cast<float>(axis.x/length),static_cast<float>(axis.y/length)};const Vec2 normal{-axis.y,axis.x};
				double best=std::numeric_limits<double>::max();
				for(float side:{-1.f,1.f})for(float outward:{1.f,-1.f}) {
					std::array<Vec2,2> offsets{};double penalty=0;
					for(unsigned i=0;i<2;++i) {
						const float sign=i? -outward:outward;offsets[i]={static_cast<float>(17*scale)*(axis.x*sign+normal.x*side),static_cast<float>(17*scale)*(axis.y*sign+normal.y*side)};
						const Vec2 q{focusPair[i].position.x+offsets[i].x,focusPair[i].position.y+offsets[i].y};const double x=q.x+snapshot->canvasOriginX,y=q.y+snapshot->canvasOriginY;
						if(x<work.left+12*scale||x>work.right-12*scale||y<work.top+12*scale||y>work.bottom-12*scale)penalty+=1000000;
						if(x/scale>barRect.left-12&&x/scale<barRect.right+12&&y/scale>barRect.top-12&&y/scale<barRect.bottom+12)penalty+=1000000;
						for(const auto& other:uiHandles)if(other.id!=focusPair[i].id)penalty+=(std::max)(0.,30*scale-std::hypot(q.x-other.position.x,q.y-other.position.y))*100;
						for(const auto& old:handles)if(old->id==focusPair[i].id)penalty+=std::hypot(offsets[i].x-old->offsetX.tar,offsets[i].y-old->offsetY.tar);
					}
					penalty+=(std::max)(0.,24*scale-std::hypot(focusPair[0].position.x+offsets[0].x-focusPair[1].position.x-offsets[1].x,focusPair[0].position.y+offsets[0].y-focusPair[1].position.y-offsets[1].y))*100;
					if(penalty<best){best=penalty;focusOffsets=offsets;focusPairFits=penalty<1000000;}
				}
			}
			for (const auto& h : uiHandles)
			{
				if(snapshot->drawingPreview) continue;
				auto found = std::find_if(handles.begin(), handles.end(), [&](const auto& v)
				{
					return v->id == h.id;
				});
				if (found == handles.end())
				{
					// 退场后已移除的点不可从保留快照重建，否则中心点会反复启动状态动画。
					if (!requestedVisible) continue;
					auto v = std::make_unique<HandleVisual>();
					v->id = h.id;
					v->glyph.Initialization(0,0,GetThemeColor(BarThemeColorEnum::TextPrimary),GetThemeColor(BarThemeColorEnum::TextPrimary));
					v->glyph.enable.Initialization(true);
					handles.push_back(std::move(v));
					found = handles.end() - 1;
				}
				auto& v = **found;
				v.wanted = requestedVisible;
				v.kind = h.kind;
				const bool auxiliary = object.style.stroke && ((h.kind == EditHandleKind::Center && (object.style.auxiliary&Auxiliary::Center) != Auxiliary::None)
					|| (h.kind == EditHandleKind::Focus && (object.style.auxiliary&Auxiliary::Foci) != Auxiliary::None));
				// 显示点时将拖动柄移到旁边；几何位置与视觉偏移分别保存。
				Vec2 offset{};
				if (h.kind==EditHandleKind::Rotate || h.kind==EditHandleKind::Move) { const float shift=static_cast<float>(capsule?(h.kind==EditHandleKind::Rotate?-10:10)*scale:0); offset={handleDock.x+shift*handleTangent.x-h.position.x,handleDock.y+shift*handleTangent.y-h.position.y}; }
				if (auxiliary) {
					const std::array<Vec2,8> offsets{{{17,-17},{-17,-17},{17,17},{-17,17},{0,-24},{0,24},{24,0},{-24,0}}};
					double best=std::numeric_limits<double>::max();
					for (const auto candidateOffset : offsets) {
						const Vec2 candidate{h.position.x+static_cast<float>(candidateOffset.x*scale),h.position.y+static_cast<float>(candidateOffset.y*scale)};
						double penalty=0;
						const double px=snapshot->canvasOriginX+candidate.x,py=snapshot->canvasOriginY+candidate.y;
						if (px<work.left+12*scale || px>work.right-12*scale || py<work.top+12*scale || py>work.bottom-12*scale) penalty+=1000000;
						// 小图形的避让柄可能越过外框，选位同时避开正在显示的样式栏。
						if (px/scale > barRect.left-12 && px/scale < barRect.right+12 &&
							py/scale > barRect.top-12 && py/scale < barRect.bottom+12) penalty+=1000000;
						for (const auto& other : snapshot->handles) if (other.id!=h.id) penalty+=(std::max)(0.,30*scale-std::hypot(candidate.x-other.position.x,candidate.y-other.position.y))*100;
						for (const auto& other : next.visualHandles) penalty+=(std::max)(0.,30*scale-std::hypot(candidate.x-other.position.x,candidate.y-other.position.y))*100;
						penalty+=std::hypot(candidateOffset.x*scale-v.offsetX.tar,candidateOffset.y*scale-v.offsetY.tar);
						if(penalty<best) {best=penalty; offset={static_cast<float>(candidateOffset.x*scale),static_cast<float>(candidateOffset.y*scale)};}
					}
				}
				if(auxiliary && h.kind==EditHandleKind::Focus && focusPair.size()==2 && focusPairFits) offset=focusOffsets[h.id==focusPair[0].id?0:1];
				if (h.kind==EditHandleKind::Rotate || h.kind==EditHandleKind::Move) { v.offsetX.SetDirect(offset.x);v.offsetY.SetDirect(offset.y); } else { v.offsetX.SetTar(offset.x,.24); v.offsetY.SetTar(offset.y,.24); }
				if (h.kind==EditHandleKind::Center || h.kind==EditHandleKind::Focus || h.kind==EditHandleKind::Move) {
					const std::wstring resource=(auxiliary || h.kind==EditHandleKind::Move) ? L"barShapeMove" : h.kind==EditHandleKind::Focus ? L"barShapeAuxFoci" : L"barShapeAuxCenter";
					if(v.glyphResource!=resource) { if(v.glyphResource.empty()) v.glyph.InitializationFromResource(L"UI",resource); else v.glyph.TransitionToResource(L"UI",resource); v.glyphResource=resource; }
					const auto advanced=AdvanceBarSvgVisual(v.glyph,{dt,speed,static_cast<bool>(BarUiAnimationEnabled),false,true,true}); changed|=advanced.changed; active|=advanced.active;
				}
				step(v.offsetX); step(v.offsetY);
				v.position = {h.position.x+static_cast<float>(v.offsetX.val),h.position.y+static_cast<float>(v.offsetY.val)};
				next.visualHandles.push_back({h.id,h.kind,v.position});
				const bool pressed = pressedHandle == h.id, hovered = hoveredHandle == h.id;
				const double normal = h.kind == EditHandleKind::Center ? 8 : 12;
				v.centerDiameter.SetTar(normal + (pressed ? -2 : hovered ? 2 : 0), .12, std::nullopt, false, stateCurve);
				v.accent.SetTar(pressed ? .80 : hovered && h.kind != EditHandleKind::Scale ? .90 : 1.0, .12, std::nullopt, false, stateCurve);
				v.iconScale.SetTar(pressed ? .92 : hovered ? 1.08 : 1.0, .12, std::nullopt, false, stateCurve);
				v.bracketThickness.SetTar(hovered && !pressed ? 5.0 : 4.0, .12, std::nullopt, false, stateCurve);
			}
			for (auto& v : handles)
			{
				const auto opacityCurve = v->wanted ? BarUiCurveEnum::EaseOutCubic : BarUiCurveEnum::EaseInCubic;
				const auto scaleCurve = v->wanted ? BarUiCurveEnum::EaseOutBack : BarUiCurveEnum::EaseInCubic;
				v->opacity.SetTar(v->wanted ? 1.0 : 0.0, v->wanted ? .28 : .16, std::nullopt, false, {opacityCurve, opacityCurve, 0.0, false});
				v->scale.SetTar(v->wanted ? 1.0 : .75, v->wanted ? .28 : .16, std::nullopt, false, {scaleCurve, scaleCurve, 0.0, false});
				step(v->opacity);
				step(v->scale);
				step(v->centerDiameter);
				step(v->accent);
				step(v->iconScale);
				step(v->bracketThickness);
			}
			// 胶囊整体入场缩放时，两半中心和命中也使用相同的当前缩放。
			if(capsule)for(auto& v:handles)if(v->kind==EditHandleKind::Rotate||v->kind==EditHandleKind::Move) {
				const float offset=static_cast<float>((v->kind==EditHandleKind::Rotate?-10:10)*scale*v->scale.val);
				v->position={handleDock.x+offset*handleTangent.x,handleDock.y+offset*handleTangent.y};
				for(auto& h:next.visualHandles)if(h.id==v->id)h.position=v->position;
			}
			std::erase_if(handles, [](const auto& v)
			{
				return !v->wanted && v->opacity.IsSame() && v->opacity.val <= .000001;
			});
			if (!requestedVisible && !toolbarMotion.Visible() && editOpacity.IsSame() && handles.empty())
			{
				// 所有可见退场通道结束后释放会话，隐藏布局不再续帧。
				snapshot.reset();
				memberWanted.fill(false); dividerWanted.fill(false); memberBatch.Restart(0);
				for (unsigned id = 1; id<Count; ++id) { x[id].SetDirect(0); y[id].SetDirect(0); }
				for (size_t di = 0; di<dividers.size(); ++di) { dividerX[di].SetDirect(0); dividerY[di].SetDirect(0); }
				toolbarWidthValue.SetDirect(0); toolbarHeightValue.SetDirect(0);
				geometry = {};
				candidate = {};
				initialized = false;
				active = false;
				presentPending = true;
				return true;
			}
			const auto lighting = ReadShapeEditorSharedLighting();
			if (!lightingValid || !SameEditorLighting(lighting, presentedLighting))
			{
				presentPending = true;
				presentedLighting = lighting;
				lightingValid = true;
			}
			renderer.SetFrameLightingSnapshot(ResolveBarSurfaceFrameLightingSnapshot(lighting, bounds, 0));
			candidate = std::move(next);
			initialized = true;
			presentPending |= changed || active;
			// 抬手后等待权威快照期间保持续帧，Commit被拒且无新快照时也能按500ms期限回落，不残留本地候选。
			active |= holdingLocal;
			return changed || active;
		}

		void DrawButton(unsigned id)
		{
			auto& p = *buttons[id];
			if (p.hide) return;
			BarButtonDrawOptions options;
			const bool badge = (id >= Swatch && id<Tone) || id == Custom;
			if (id == Color || badge || id == PickerClose || (id >= Thickness && id<Sides))
			{
				options.content = [&, id, badge](BarUIRendering& r, ID2D1DeviceContext* dc, BarButtonClass& button, const BarButtonVisualInheritance& inh)
				{
					const double z = scale;
					const FLOAT cx = static_cast<FLOAT>((inh.button.x+button.button.w.val/2)*z), cy = static_cast<FLOAT>((inh.button.y+button.button.h.val/2)*z);
					const double content = EditorPanel(id) ? 1 : appliedContentOpacity;
					if (id == PickerClose)
					{
						const FLOAT half = static_cast<FLOAT>(5*z);
						if (auto* brush = r.ContentSolidBrush(GetThemeColor(BarThemeColorEnum::IconPrimary), button.name.pct.val))
						{
							dc->DrawLine({cx-half, cy-half}, {cx+half, cy+half}, brush, static_cast<FLOAT>((std::max)(1., 1.6*z)));
							dc->DrawLine({cx+half, cy-half}, {cx-half, cy+half}, brush, static_cast<FLOAT>((std::max)(1., 1.6*z)));
						}
					}
					else if (id >= Thickness && id<Sides)
					{
						const FLOAT diameter = static_cast<FLOAT>(24*z), width = (std::min)(diameter, (std::max)(1.f, widths[id-Thickness]));
						if (auto* brush = r.ContentSolidBrush(button.name.color.val, button.name.pct.val*content))
						{
							D2D1_MATRIX_3X2_F transform; dc->GetTransform(&transform);
							dc->SetTransform(D2D1::Matrix3x2F::Rotation(-45, {cx, cy})*transform);
							dc->FillRoundedRectangle(D2D1::RoundedRect({cx-diameter/2, cy-width/2, cx+diameter/2, cy+width/2}, width/2, width/2), brush);
							dc->SetTransform(transform);
						}
					}
					else if (id == Color)
					{
						// 取色拖动只消费本地反馈色，当前对象仍只在松手时提交一次SetStyle。
						const auto rgb = candidate.pickerColor;
						const auto rect = D2D1::RoundedRect({cx-static_cast<FLOAT>(10*z), cy-static_cast<FLOAT>(10*z), cx+static_cast<FLOAT>(10*z), cy+static_cast<FLOAT>(10*z)}, static_cast<FLOAT>(4*z), static_cast<FLOAT>(4*z));
						// 共享画刷逐次消费，后取的描边色不能覆盖先前的填充色。
						if (auto* fill = r.ContentSolidBrush(rgb, button.name.pct.val*content)) dc->FillRoundedRectangle(rect, fill);
						if (auto* frame = r.ContentSolidBrush(GetThemeColor(BarThemeColorEnum::SwatchFrame), .18*button.name.pct.val*content)) dc->DrawRoundedRectangle(rect, frame, static_cast<FLOAT>(z));
					}
					else
					{
						const unsigned index = id == Custom ? 11 : id-Swatch;
						auto& surface = swatchSurfaces[index];
						PlaceShape(surface, inh.button.x, inh.button.y, 30, 30);
						surface.frame->SetDirect(GetThemeColor(BarThemeColorEnum::SwatchFrame));
						if (!surface.fill) surface.fill.emplace(0);
						surface.fill->SetDirect(id == Custom ? candidate.pickerColor : GetPresetColor(static_cast<BarThemePresetColorEnum>(index)));
						if (id == Custom)
						{
							r.Png(dc, colorWheel, {inh.button.x, inh.button.y});
							surface.fill.reset(); surface.rw->SetDirect(15); surface.rh->SetDirect(15);
							r.Shape(dc, surface, {surface.inhX, surface.inhY});
							const D2D1_ELLIPSE core{{cx, cy}, static_cast<FLOAT>(10*z), static_cast<FLOAT>(10*z)};
							if (auto* fill = r.ContentSolidBrush(candidate.pickerColor, 1)) dc->FillEllipse(core, fill);
							if (auto* frame = r.ContentSolidBrush(GetThemeColor(BarThemeColorEnum::SwatchFrame), .18)) dc->DrawEllipse(core, frame, static_cast<FLOAT>(z));
						}
						else r.Shape(dc, surface, {surface.inhX, surface.inhY});
						r.Svg(dc, button.icon, inh.icon);
					}
					return true;
				};
			}
			if (id == Color || badge) options.drawIcon = false;
			DrawBarButtonVisual(renderer, renderer.GetDeviceContext(), p, {p.button.inhX, p.button.inhY}, options);
		}

		void DrawPicker(ID2D1DeviceContext* dc)
		{
			D2D1_MATRIX_3X2_F transform; dc->GetTransform(&transform);
			// ColorPalette直接消费像素矩形；在局部DIP矩阵下复用，4DIP圆角与主栏保持一致。
			dc->SetTransform(D2D1::Matrix3x2F::Scale(static_cast<FLOAT>(scale), static_cast<FLOAT>(scale))*transform);
			const auto rounded = D2D1::RoundedRect({5, 40, 295, 172}, 4, 4);
			if (auto* hue = renderer.GetColorPickerHueGradientBrush(dc, {5, 40}, {295, 40}))
			{
				hue->SetOpacity(1); dc->FillRoundedRectangle(rounded, hue);
				const D2D1_POINT_2F nearPoint{5, candidate.pickerBelow ? 40.f : 172.f}, farPoint{5, candidate.pickerBelow ? 172.f : 40.f};
				if (auto* light = renderer.GetColorPickerToneGradientBrush(dc, false, farPoint, nearPoint, static_cast<FLOAT>(1-toneMix.val))) dc->FillRoundedRectangle(rounded, light);
				if (auto* dark = renderer.GetColorPickerToneGradientBrush(dc, true, nearPoint, farPoint, static_cast<FLOAT>(toneMix.val))) dc->FillRoundedRectangle(rounded, dark);
			}
			dc->SetTransform(transform);
			if (candidate.markerVisible)
			{
				const D2D1_ELLIPSE marker{{static_cast<FLOAT>((5+candidate.markerX*290)*scale), static_cast<FLOAT>((40+candidate.markerY*132)*scale)}, static_cast<FLOAT>(5.5*scale), static_cast<FLOAT>(5.5*scale)};
				if (auto* brush = renderer.ContentSolidBrush(RGB(0, 0, 0), .72)) dc->DrawEllipse(marker, brush, static_cast<FLOAT>(3*scale));
				if (auto* brush = renderer.ContentSolidBrush(RGB(255, 255, 255), 1)) dc->DrawEllipse(marker, brush, static_cast<FLOAT>(1.5*scale));
			}
			DrawButton(Tone); DrawButton(PickerClose);
			const auto channel = [&](size_t i) { return static_cast<int>(std::lround(static_cast<double>(pickerDisplay[i].val))); };
			PlaceShape(pickerPreview, 40, 5, 220, 30); pickerPreview.fill->SetDirect(RGB(channel(0), channel(1), channel(2)));
			pickerPreview.framePct->SetDirect(0); pickerPreview.frameLightPct->SetDirect(0);
			renderer.Shape(dc, pickerPreview, {40, 5});
			const std::array<std::wstring, 4> labels{IW(I18nKey.UI.Bar.ColorPicker.RedChannelLabel), IW(I18nKey.UI.Bar.ColorPicker.GreenChannelLabel),
				IW(I18nKey.UI.Bar.ColorPicker.BlueChannelLabel), IW(I18nKey.UI.Bar.ColorPicker.OpacityLabel)};
			const auto textSize = renderer.MeasureText(L"Ag", 13);
			const double padding = (std::clamp)((37-textSize.height)/2., 0., 18.5);
			const double valueWidth = renderer.MeasureText(L"255", 13).width, opacityWidth = renderer.MeasureText(L"100%", 13).width;
			double labelWidth = 0; for (unsigned i = 0; i<3; ++i) labelWidth = (std::max)(labelWidth, static_cast<double>(renderer.MeasureText(labels[i], 13).width));
			const double opacityLabelWidth = renderer.MeasureText(labels[3], 13).width;
			const double budget = (std::max)(0., 300-padding*2-labelWidth*3-valueWidth*3-opacityLabelWidth-opacityWidth);
			const double innerGap = (std::min)(3., budget/4), columnGap = (std::min)(6., (std::max)(0., (budget-innerGap*4)/2));
			const double columnWidth = labelWidth+innerGap+valueWidth;
			const std::array<std::wstring, 4> values{std::to_wstring(channel(0)), std::to_wstring(channel(1)),
				std::to_wstring(channel(2)), std::to_wstring(channel(3))+L"%"};
			for (unsigned i = 0; i<4; ++i)
			{
				const double left = i<3 ? padding+i*(columnWidth+columnGap) : 300-padding-opacityLabelWidth-innerGap-opacityWidth;
				const double width = i<3 ? labelWidth : opacityLabelWidth;
				for (unsigned part = 0; part<2; ++part)
				{
					auto& word = pickerWords[i+part*4]; word.SetStringImmediate(part ? values[i] : labels[i]);
					word.w.SetDirect(part ? (i<3 ? valueWidth : opacityWidth) : width); word.h.SetDirect(37); word.color.SetDirect(GetThemeColor(BarThemeColorEnum::TextPrimary));
					renderer.Word(dc, word, {left+(part ? width+innerGap : 0), 172}, DWRITE_FONT_WEIGHT_NORMAL, part ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING);
				}
			}
			if (holdOpacity.val>.000001)
			{
				holdWord.SetStringImmediate(IW(I18nKey.UI.Bar.ColorPicker.HoldColorLabel));
				const double width = (std::min)(280., renderer.MeasureText(holdWord.TargetContent(), 12).width+29.);
				const double left = 5+(290-width)/2, top = holdOnTop ? 45 : 139;
				PlaceShape(holdSurface, left, top, width, 28); holdSurface.fill->SetDirect(GetThemeColor(BarThemeColorEnum::Surface));
				holdSurface.pct.SetDirect(.82*holdOpacity.val); holdSurface.framePct->SetDirect(0); holdSurface.frameLightPct->SetDirect(0);
				renderer.Shape(dc, holdSurface, {left, top});
				holdWord.w.SetDirect(width-24); holdWord.h.SetDirect(28); holdWord.pct.SetDirect(holdOpacity.val);
				holdWord.color.SetDirect(MixBarUiColor(MixBarUiColor(GetThemeColor(BarThemeColorEnum::TextPrimary), GetThemeColor(BarThemeColorEnum::Surface), .45), GetThemeColor(BarThemeColorEnum::TextPrimary), holdTextMix.val));
				renderer.Word(dc, holdWord, {left+5, top}, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING);
				renderer.DrawProgressRing(dc, {static_cast<FLOAT>((left+width-12)*scale), static_cast<FLOAT>((top+14)*scale)}, static_cast<FLOAT>(7*scale), static_cast<FLOAT>(2*scale), static_cast<FLOAT>(holdProgress),
					MixBarUiColor(GetThemeColor(BarThemeColorEnum::TextPrimary), GetThemeColor(BarThemeColorEnum::Surface), .70),
					MixBarUiColor(GetThemeColor(BarThemeColorEnum::TextPrimary), GetThemeColor(BarThemeColorEnum::Surface), .35), static_cast<FLOAT>(holdRingOpacity.val*.45), static_cast<FLOAT>(holdRingOpacity.val));
			}
		}

		ID2D1PathGeometry* BracketGeometry(ID2D1DeviceContext* dc, FLOAT radius)
		{
			if (bracketGeometry && bracketRadius == radius && bracketScale == scale) return bracketGeometry.Get();
			bracketGeometry.Reset();
			Microsoft::WRL::ComPtr<ID2D1Factory> factory;
			Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
			Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
			dc->GetFactory(&factory);
			if (FAILED(factory->CreatePathGeometry(&path))) return nullptr;
			++bracketGeometryCreations;
			if (FAILED(path->Open(&sink))) return nullptr;
			// 缓存左上角标准L，其他角只做镜像/平移；线宽由DrawGeometry独立消费，无须重建路径。
			const FLOAT arm = radius + static_cast<FLOAT>(12 * scale);
			sink->BeginFigure({arm, 0}, D2D1_FIGURE_BEGIN_HOLLOW);
			sink->AddLine({radius, 0});
			sink->AddArc(D2D1::ArcSegment({0, radius}, {radius, radius}, 0,
				D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
			sink->AddLine({0, arm});
			sink->EndFigure(D2D1_FIGURE_END_OPEN);
			if (FAILED(sink->Close())) return nullptr;
			bracketGeometry = std::move(path); bracketRadius = radius; bracketScale = scale;
			return bracketGeometry.Get();
		}

		void Draw()
		{
			if (!candidate.visible) return;
			auto* dc = renderer.GetDeviceContext();
			if (!dc) return;
			renderer.SetFrameZoom(scale);
			if (candidate.input && !snapshot->drawingPreview) { auto coverage = *snapshot; coverage.handles = candidate.visualHandles; DrawEditorInputCoverage(renderer, dc, coverage, bounds, scale); }
			if (candidate.paletteLayer || candidate.sidesLayer || candidate.pickerLayer)
			{
				// 弹层空白屏障同样用COPY，叠加后的热区alpha仍为1。
				const auto blend = dc->GetPrimitiveBlend(); dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY);
				if (auto* brush = renderer.ContentSolidBrush(RGB(0, 0, 0), 1./255)) dc->FillRectangle({static_cast<FLOAT>(work.left-bounds.left), static_cast<FLOAT>(work.top-bounds.top), static_cast<FLOAT>(work.right-bounds.left), static_cast<FLOAT>(work.bottom-bounds.top)}, brush);
				dc->SetPrimitiveBlend(blend);
			}
			const auto& object = *snapshot->object;
			const auto accent = MixBarUiColor(GetThemeColor(BarThemeModeEnum::Dark, BarThemeColorEnum::Accent),
				GetThemeColor(BarThemeModeEnum::Light, BarThemeColorEnum::Accent), appliedLightWeight);
			// 两腰提示仅属于编辑层，不进入文档和导出几何。
			if (object.construction && object.construction->tool.preset == PresetKind::Triangle && object.construction->tool.constraint == PlanarConstraint::Isosceles)
			{
				std::array<Vec2,3> points{}; unsigned count=0;
				for (const auto& h : snapshot->handles) if (h.kind == EditHandleKind::Vertex && count<3) points[count++]=h.position;
				if (count==3) if (auto* brush=renderer.ContentSolidBrush(accent,.90*appliedContentOpacity*editOpacity.val)) {
					const auto apex=object.construction->tool.constraintVertex%3;
					for (unsigned i=0;i<3;++i) if (i!=apex) {
						const double dx=points[i].x-points[apex].x,dy=points[i].y-points[apex].y,length=std::hypot(dx,dy); if(length<1) continue;
						const double tx=dx/length,ty=dy/length,cx=snapshot->canvasOriginX+(points[i].x+points[apex].x)/2-bounds.left,cy=snapshot->canvasOriginY+(points[i].y+points[apex].y)/2-bounds.top;
						// 两条固定屏幕尺度的斜杠只属于编辑提示，随边方向旋转。
						for(double shift:{-2.5,2.5}) { const double x=cx+shift*scale*tx,y=cy+shift*scale*ty,sx=(tx-(i==(apex+1)%3?1:-1)*ty)*3*scale,sy=(ty+(i==(apex+1)%3?1:-1)*tx)*3*scale;
							dc->DrawLine({static_cast<FLOAT>(x-sx),static_cast<FLOAT>(y-sy)},{static_cast<FLOAT>(x+sx),static_cast<FLOAT>(y+sy)},brush,static_cast<FLOAT>(1.5*scale),connectorStyle.Get()); }
					}
				}
			}
			if(snapshot->drawingPreview) return;

			const double frameOpacity = .90 * appliedContentOpacity * editOpacity.val;
			if (!connectorStyle)
			{
				Microsoft::WRL::ComPtr<ID2D1Factory> factory;
				dc->GetFactory(&factory);
				D2D1_STROKE_STYLE_PROPERTIES properties{};
				properties.startCap = properties.endCap = D2D1_CAP_STYLE_ROUND;
				properties.lineJoin = D2D1_LINE_JOIN_ROUND;
				properties.miterLimit = 10;
				(void) factory->CreateStrokeStyle(properties, nullptr, 0, &connectorStyle);
			}
			// 外框、连接线与角标统一合成，重叠描边不重复增加内容alpha。
			renderer.PushPanelOpacity(dc, {0, 0, static_cast<FLOAT>(bounds.right-bounds.left), static_cast<FLOAT>(bounds.bottom-bounds.top)}, frameOpacity);
			if (snapshot->frameRect.valid)
			{
				if (ShowEditorFrame(EditorPreset(object)))
				{
					const auto visual = ResolveEditorFrame(*snapshot, bounds, scale);
					D2D1_MATRIX_3X2_F original;
					dc->GetTransform(&original);
					dc->SetTransform(visual.transform * original);
					if (auto* brush = renderer.ContentSolidBrush(accent, 1.0)) dc->DrawRoundedRectangle(visual.rounded, brush, visual.thickness);
					// 没有整体缩放柄时，仅四角可直接拖动的矩形类补相同角标；椭圆等不支持四角拖动的图形只画外框。
					const auto framePreset=EditorPreset(object);
					if((framePreset==PresetKind::Rectangle||framePreset==PresetKind::RoundedRectangle)&&!std::any_of(snapshot->handles.begin(),snapshot->handles.end(),[](const auto& h){return h.kind==EditHandleKind::Scale;})) {
						const auto& r=visual.rounded.rect; const FLOAT radius=(std::min)({visual.rounded.radiusX,(r.right-r.left)/2,(r.bottom-r.top)/2});
						if(auto* path=BracketGeometry(dc,radius)) for(unsigned i=0;i<4;++i) {
							const bool left=i==0||i==3,top=i<2; dc->SetTransform(D2D1::Matrix3x2F(left?1.f:-1.f,0,0,top?1.f:-1.f,left?r.left:r.right,top?r.top:r.bottom)*visual.transform*original);
							if(auto* brush=renderer.ContentSolidBrush(accent,1))dc->DrawGeometry(path,brush,static_cast<FLOAT>(4*scale),connectorStyle.Get());
						}
					}
					dc->SetTransform(original);
				}
				for (const auto& h : snapshot->handles) if (h.kind == EditHandleKind::Rotate)
				{
					const auto top = handleEdge;
					const D2D1_POINT_2F start{static_cast<FLOAT>(snapshot->canvasOriginX + top.x - bounds.left),
						static_cast<FLOAT>(snapshot->canvasOriginY + top.y - bounds.top)};
					const D2D1_POINT_2F end{static_cast<FLOAT>(snapshot->canvasOriginX + handleDock.x - bounds.left), static_cast<FLOAT>(snapshot->canvasOriginY + handleDock.y - bounds.top)};
					if (auto* brush = renderer.ContentSolidBrush(accent, 1.0)) dc->DrawLine(start, end, brush, static_cast<FLOAT>(1.5 * scale), connectorStyle.Get());
				}
			}
			const auto rotateHandle=std::find_if(handles.begin(),handles.end(),[](const auto& h){return h->kind==EditHandleKind::Rotate;});
			const auto drawCapsule = [&] {
				if(rotateHandle==handles.end())return;const auto& v=**rotateHandle;
				D2D1_MATRIX_3X2_F original;dc->GetTransform(&original);
				const FLOAT x=static_cast<FLOAT>(snapshot->canvasOriginX+handleDock.x-bounds.left),y=static_cast<FLOAT>(snapshot->canvasOriginY+handleDock.y-bounds.top),r=static_cast<FLOAT>(10*scale*v.scale.val),half=static_cast<FLOAT>(10*scale*v.scale.val);
				// 与普通圆柄复用相同材质公式，独立于外框透明层，不叠加青色边框。
				const auto darkFill=MixBarUiColor(GetThemeColor(BarThemeModeEnum::Dark,BarThemeColorEnum::Surface),GetThemeColor(BarThemeModeEnum::Dark,BarThemeColorEnum::TextPrimary),.20);
				const auto fill=MixBarUiColor(darkFill,RGB(255,255,255),appliedLightWeight);
				const auto darkFrame=MixBarUiColor(darkFill,GetThemeColor(BarThemeModeEnum::Dark,BarThemeColorEnum::TextPrimary),.12);
				const auto lightFrame=MixBarUiColor(GetThemeColor(BarThemeModeEnum::Light,BarThemeColorEnum::Surface),GetThemeColor(BarThemeModeEnum::Light,BarThemeColorEnum::TextPrimary),BarThemeMaterial::LightSliderThumbBorderOpacity);
				renderer.PushPanelOpacity(dc,{0,0,static_cast<FLOAT>(bounds.right-bounds.left),static_cast<FLOAT>(bounds.bottom-bounds.top)},appliedContentOpacity*v.opacity.val);
				dc->SetTransform(D2D1::Matrix3x2F(handleTangent.x,handleTangent.y,-handleTangent.y,handleTangent.x,x,y)*original);
				if(auto* brush=renderer.ContentSolidBrush(MixBarUiColor(darkFrame,lightFrame,appliedLightWeight),1))dc->FillRoundedRectangle(D2D1::RoundedRect({-half-r,-r,half+r,r},r,r),brush);
				const FLOAT inner=(std::max)(0.f,r-static_cast<FLOAT>(scale));
				if(auto* brush=renderer.ContentSolidBrush(fill,1))dc->FillRoundedRectangle(D2D1::RoundedRect({-half-inner,-inner,half+inner,inner},inner,inner),brush);
				dc->SetTransform(original);
				for(const auto& h:handles)if(h->kind==EditHandleKind::Rotate||h->kind==EditHandleKind::Move) {
					const bool rotate=h->kind==EditHandleKind::Rotate;const FLOAT cx=static_cast<FLOAT>(snapshot->canvasOriginX+h->position.x-bounds.left),cy=static_cast<FLOAT>(snapshot->canvasOriginY+h->position.y-bounds.top);
					if(rotate)PlaceShape(buttons[Rotate]->button,cx/scale-10,cy/scale-10,20,20);
					auto& icon=rotate?buttons[Rotate]->icon:h->glyph;const double d=16*h->iconScale.val*h->scale.val;const auto color=MixBarUiColor(fill,accent,h->accent.val);
					icon.w.SetDirect(d);icon.h.SetDirect(d);icon.pct.SetDirect(1);if(icon.color1)icon.color1->SetDirect(color);if(icon.color2)icon.color2->SetDirect(color);
					dc->SetTransform(D2D1::Matrix3x2F::Translation(-cx,-cy)*D2D1::Matrix3x2F(handleTangent.x,handleTangent.y,-handleTangent.y,handleTangent.x,cx,cy)*original);
					renderer.Svg(dc,icon,icon.UpInh({cx/scale-d/2,cy/scale-d/2}));
				}
				dc->SetTransform(original);dc->PopLayer();
			};
			// 外框角点先画，专属点覆盖其上，整体移动中心点最后画。
			for (int layer = 0; layer < 3; ++layer)
			{
				if (layer == 1) {dc->PopLayer();drawCapsule();}
				for (const auto& v : handles)
				{
					const int rank = v->kind == EditHandleKind::Scale ? 0 : v->kind == EditHandleKind::Center ? 2 : 1;
					if (rank != layer || v->opacity.val <= .000001) continue;
					const D2D1_POINT_2F center{static_cast<FLOAT>(snapshot->canvasOriginX + v->position.x - bounds.left),
						static_cast<FLOAT>(snapshot->canvasOriginY + v->position.y - bounds.top)};
					if (v->kind == EditHandleKind::Scale)
					{
						const auto visual = ResolveEditorFrame(*snapshot, bounds, scale);
						auto inverse = visual.transform;
						if (!inverse.Invert()) continue;
						const auto local = inverse.TransformPoint(center);
						const auto& rect = visual.rounded.rect;
						const bool left = local.x < (rect.left + rect.right) / 2, top = local.y < (rect.top + rect.bottom) / 2;
						const FLOAT cornerX = left ? rect.left : rect.right, cornerY = top ? rect.top : rect.bottom;
						const FLOAT sx = left ? 1.0F : -1.0F, sy = top ? 1.0F : -1.0F;
						const FLOAT radius = (std::min)({visual.rounded.radiusX, (rect.right-rect.left)/2, (rect.bottom-rect.top)/2});
						auto* path = BracketGeometry(dc, radius);
						if (!path) continue;
						D2D1_MATRIX_3X2_F original;
						dc->GetTransform(&original);
						dc->SetTransform(D2D1::Matrix3x2F(sx, 0, 0, sy, cornerX, cornerY) * visual.transform * original);
						const auto surface = MixBarUiColor(GetThemeColor(BarThemeModeEnum::Dark, BarThemeColorEnum::Surface),
							GetThemeColor(BarThemeModeEnum::Light, BarThemeColorEnum::Surface), appliedLightWeight);
						if (auto* brush = renderer.ContentSolidBrush(MixBarUiColor(surface, accent, v->accent.val), 1.0)) dc->DrawGeometry(path, brush,
							static_cast<FLOAT>(v->bracketThickness.val * scale), connectorStyle.Get());
						dc->SetTransform(original);
						continue;
					}
					const bool rotate = v->kind == EditHandleKind::Rotate;
					D2D1_MATRIX_3X2_F original;
					dc->GetTransform(&original);
					BarSliderThumbStyle thumbStyle;
					thumbStyle.centerPx = center;
					thumbStyle.outerDiameterPx = static_cast<FLOAT>(20 * scale * v->scale.val);
					const bool movePoint = v->kind == EditHandleKind::Center || v->kind == EditHandleKind::Focus || v->kind == EditHandleKind::Move;
					thumbStyle.centerDiameterPx = movePoint ? 0 : static_cast<FLOAT>(v->centerDiameter.val * scale * v->scale.val);
					thumbStyle.accentOpacity = v->accent.val;
					thumbStyle.lightWeight = appliedLightWeight;
					thumbStyle.opacity = appliedContentOpacity * v->opacity.val;
					thumbStyle.frameThicknessPx = static_cast<FLOAT>(scale);
					thumbStyle.surfaceColor = MixBarUiColor(GetThemeColor(BarThemeModeEnum::Dark, BarThemeColorEnum::Surface),
						GetThemeColor(BarThemeModeEnum::Light, BarThemeColorEnum::Surface), appliedLightWeight);
					thumbStyle.accentColor = accent;
					thumbStyle.compositeOpacity = true;
					if (rotateHandle==handles.end() || (!rotate && v->kind!=EditHandleKind::Move))
						DrawBarSliderThumb(renderer, dc, thumbStyle, movePoint ? &v->glyph : nullptr, v->iconScale.val * v->scale.val);
					dc->SetTransform(original);
				}
			}
			D2D1_MATRIX_3X2_F original; dc->GetTransform(&original);
			const std::array<BarUiShapeClass*, 4> surfaces{&toolbar, &sidesPanel, &palettePanel, &pickerPanel};
			for (unsigned panel = 0; panel<4; ++panel)
			{
				if (candidate.panelOpacity[panel] <= .000001) continue;
				const auto pose = candidate.poses[panel];
				renderer.PushPanelOpacity(dc, {0, 0, static_cast<FLOAT>(bounds.right-bounds.left), static_cast<FLOAT>(bounds.bottom-bounds.top)}, candidate.panelOpacity[panel]);
				dc->SetTransform(D2D1::Matrix3x2F(static_cast<FLOAT>(pose.scale), 0, 0, static_cast<FLOAT>(pose.scale), static_cast<FLOAT>(pose.x*scale), static_cast<FLOAT>(pose.y*scale))*original);
				renderer.SetFrameDiffuseMaskGeometryScale(1./(std::max)(.01, pose.scale));
				auto& surface = *surfaces[panel]; DrawBarBackgroundVisual(renderer, dc, surface, {0, 0});
				renderer.SetResidentContentOpacity(panel ? 1 : appliedContentOpacity);
				if (!panel)
				{
					// 退场成员先画，可见成员覆盖；不使用随宽度动画的裁剪揭示内容。
					for (bool visible : {false, true}) for (unsigned id : ToolbarItems) if (id && memberWanted[id] == visible) DrawButton(id);
					if (memberWanted[TriangleMode+1] && object.construction) {
						const auto& b=*buttons[TriangleMode+1]; const auto apex=object.construction->tool.constraint == PlanarConstraint::Isosceles ? object.construction->tool.constraintVertex%3 : 3;
						for(unsigned i=0;i<3;++i) if(auto* brush=renderer.ContentSolidBrush(i==apex ? accent : GetThemeColor(BarThemeColorEnum::TextPrimary),i==apex ? 1 : .3)) dc->FillEllipse({{static_cast<FLOAT>((b.button.inhX+b.button.w.val-6)*scale),static_cast<FLOAT>((b.button.inhY+b.button.h.val/2+(static_cast<int>(i)-1)*5)*scale)},static_cast<FLOAT>(1.5*scale),static_cast<FLOAT>(1.5*scale)},brush);
					}
					for (auto& d : dividers) if (d->button.pct.val>.000001)
					{
						renderer.PushPanelOpacity(dc, {0, 0, static_cast<FLOAT>((std::max)(static_cast<double>(toolbarWidthValue.val), static_cast<double>(toolbarWidthValue.tar))*scale), static_cast<FLOAT>((std::max)(static_cast<double>(toolbarHeightValue.val), static_cast<double>(toolbarHeightValue.tar))*scale)}, appliedContentOpacity);
						DrawBarButtonVisual(renderer, dc, *d, {d->button.inhX, d->button.inhY}); dc->PopLayer();
					}
				}
				else if (panel == 1) for (unsigned id = Sides; id<Swatch; ++id) DrawButton(id);
				else if (panel == 2) { for (unsigned id = Swatch; id<Tone; ++id) DrawButton(id); DrawButton(Custom); }
				else DrawPicker(dc);
				renderer.SetResidentContentOpacity(1); renderer.SetFrameDiffuseMaskGeometryScale(1);
				dc->SetTransform(original); dc->PopLayer();
			}
		}

		bool CloseTopLayer()
		{
			if (pickerOpen.exchange(false) || colorOpen.exchange(false) || sidesOpen.exchange(false))
			{
				focused = 0; // 层已关闭就立即释放内容焦点，不等待下一次成功呈现。
				return true;
			}
			return false;
		}
		void UpdatePickerPoint(const Frame& frame, POINT point)
		{
			const auto& r = frame.palette;
			std::scoped_lock lock(inputMutex);
			if (pickerLocked || r.right <= r.left || r.bottom <= r.top) return;
			if (std::hypot(double(point.x-pickerStablePoint.x), double(point.y-pickerStablePoint.y))>5*(std::max)(1., frame.scale))
			{
				pickerStablePoint = point; pickerStableTime = std::chrono::steady_clock::now();
			}
			pickerDragX = (std::clamp)(((point.x-frame.bounds.left)/frame.scale-r.left)/(r.right-r.left), 0., 1.);
			pickerDragY = (std::clamp)(((point.y-frame.bounds.top)/frame.scale-r.top)/(r.bottom-r.top), 0., 1.);
			pickerDragColor = GetBarColorPickerColor(pickerDragX, pickerDragY, frame.darkTone, frame.pickerBelow);
		}
		void Activate(unsigned id, const Frame& frame, POINT screen)
		{
			if (!frame.snapshot || !frame.snapshot->object) return;
			if (id == Finish) ClearToolbarHold(false); else HoldToolbar(frame);
			if (id == Color)
			{
				std::scoped_lock lock(inputMutex);
				colorOpen = !colorOpen.load(); sidesOpen = false; pickerOpen = false;
				return;
			}
			if (id == SidesEntry) { sidesOpen = !sidesOpen.load(); colorOpen = pickerOpen = false; return; }
			if (id == Custom) { pickerOpen = true; return; }
			if (id == PickerClose) { pickerOpen = false; return; }
			if (id == Finish) { ShapeUiAction a; a.identity = frame.snapshot->identity; a.kind = ShapeUiActionKind::CloseEditor; a.finishEditing = true; colorOpen = sidesOpen = pickerOpen = false; Emit(a); return; }
			if (id == Tone)
			{
				std::scoped_lock lock(inputMutex);
				darkTone = !darkTone.load();
				return;
			}
			ShapeUiAction a;
			a.identity = frame.snapshot->identity;
			a.kind = ShapeUiActionKind::SetStyle;
			a.style = frame.snapshot->object->style;
			if (id >= Style && id<Pattern)
			{
				a.style.stroke = id != Style+1;
				a.style.fill = id != Style;
			}
			else if (id == FlipHorizontal || id == FlipVertical) a.kind = id == FlipHorizontal ? ShapeUiActionKind::FlipHorizontal : ShapeUiActionKind::FlipVertical;
			else if (id == Rotate45 || id == Rotate90) a.kind = id == Rotate45 ? ShapeUiActionKind::Rotate45Clockwise : ShapeUiActionKind::Rotate90Clockwise;
			else if (id >= Pattern && id<Thickness) a.style.linePattern = static_cast<LinePattern>(id-Pattern);
			else if (id >= Thickness && id<Sides) a.style.width = static_cast<float>(frame.widths[id-Thickness]/ShapeWidthScale(*frame.snapshot->object));
			else if (id >= Sides && id<Swatch)
			{
				a.kind = ShapeUiActionKind::SetSides;
				a.sides = id-Sides+3;
			}
			else if (id >= TriangleMode && id<TriangleMode+3)
			{
				a.kind = ShapeUiActionKind::SetConstraint; a.constraint = static_cast<PlanarConstraint>(id-TriangleMode);
			}
			else if (id == TrapezoidMode && (EditorPreset(*frame.snapshot->object) == PresetKind::Prism || EditorPreset(*frame.snapshot->object) == PresetKind::Pyramid))
			{ a.kind = ShapeUiActionKind::SetRightSolid; a.rightSolid = !frame.snapshot->object->construction->tool.rightSolid; }
			else if (id == TrapezoidMode)
			{
				a.kind = ShapeUiActionKind::SetConstraint;
				a.constraint = frame.snapshot->object->construction && frame.snapshot->object->construction->tool.constraint == PlanarConstraint::Isosceles
					? PlanarConstraint::Free : PlanarConstraint::Isosceles;
			}
			else if (id >= Aux && id<Aux+3)
			{
				// 对象属性沿用SetStyle/Replace事务，不能写下一笔的工具辅助默认值。
				a.style.auxiliary = static_cast<Auxiliary>(static_cast<uint8_t>(a.style.auxiliary)^static_cast<uint8_t>(AuxiliaryBits[id-Aux]));
			}
			else if (id >= Swatch && id<Tone) a.style.strokeRgb = a.style.fillRgb = UiColorToShapeRgb(GetPresetColor(static_cast<BarThemePresetColorEnum>(id-Swatch)));
			else if (id == PaletteHit)
			{
				const auto r = frame.palette;
				const double x = ((screen.x-frame.bounds.left)/frame.scale-r.left)/(r.right-r.left), y = ((screen.y-frame.bounds.top)/frame.scale-r.top)/(r.bottom-r.top);
				a.style.strokeRgb = a.style.fillRgb = UiColorToShapeRgb(GetBarColorPickerColor(x, y, frame.darkTone, frame.pickerBelow));
			}
			else return;
			Emit(a);
		}
	};
	ShapeEditorSurface::ShapeEditorSurface() : impl_(std::make_unique<Impl>())
	{
	}

	ShapeEditorSurface::~ShapeEditorSurface() = default;
	void ShapeEditorSurface::Configure(std::shared_ptr<const ShapeUiSnapshot> s, RECT work, RECT bar)
	{
		impl_->Configure(std::move(s), work, bar);
	}

	bool ShapeEditorSurface::Advance(std::chrono::steady_clock::time_point now)
	{
		return impl_->Advance(now);
	}
	std::chrono::steady_clock::time_point ShapeEditorSurface::TickToolbarIdle(std::chrono::steady_clock::time_point now)
	{
		return impl_->TickToolbarIdle(now);
	}

	bool ShapeEditorSurface::Visible() const noexcept
	{
		return impl_->candidate.visible;
	}

	bool ShapeEditorSurface::AnimationActive() const noexcept
	{
		return impl_->active;
	}

	RECT ShapeEditorSurface::Bounds() const noexcept
	{
		return impl_->bounds;
	}

	HRESULT ShapeEditorSurface::EnsureDeviceResources(const RenderPipeline::DeviceEpoch& epoch)
	{
		auto& p = *impl_;
		if (p.deviceGeneration != epoch.generation)
		{
			for (auto& b : p.buttons) if (b) b->icon.ResetCache();
			p.colorWheel.ResetCache();
			p.bracketGeometry.Reset(); p.connectorStyle.Reset();
			p.deviceGeneration = epoch.generation;
		}

		return p.renderer.EnsureDeviceResources(epoch, (std::max)(1L, p.bounds.right-p.bounds.left), (std::max)(1L, p.bounds.bottom-p.bounds.top));
	}

	BarUIRendering& ShapeEditorSurface::Renderer() noexcept
	{
		return impl_->renderer;
	}

	void ShapeEditorSurface::Draw()
	{
		impl_->Draw();
	}

	HRESULT ShapeEditorSurface::Present(const std::function<BOOL(const UPDATELAYEREDWINDOWINFO&)>& sink)
	{
		auto& p = *impl_;
		auto* dc = p.renderer.GetDeviceContext();
		auto* gdi = p.renderer.GetGdiInteropRenderTarget();
		if (!dc || !gdi || !sink) return E_UNEXPECTED;
		dc->SetTransform(D2D1::Matrix3x2F::Identity());
		p.renderer.BeginFrameDraw(dc, {0, 0, static_cast<float>(p.bounds.right-p.bounds.left), static_cast<float>(p.bounds.bottom-p.bounds.top)});
		p.Draw();
		p.renderer.FinishFrameDrawCommands(dc);
		HDC source = nullptr;
		const HRESULT get = gdi->GetDC(D2D1_DC_INITIALIZE_MODE_COPY, &source);
		HRESULT release = S_OK;
		bool presented = false;
		if (SUCCEEDED(get) && source)
		{
			POINT dst{p.bounds.left, p.bounds.top}, src{};
			SIZE size{p.bounds.right-p.bounds.left, p.bounds.bottom-p.bounds.top};
			BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
			UPDATELAYEREDWINDOWINFO info{sizeof(info)};
			info.hdcSrc = source;
			info.pptDst = &dst;
			info.psize = &size;
			info.pptSrc = &src;
			info.pblend = &blend;
			info.dwFlags = ULW_ALPHA;
			presented = sink(info) != FALSE;
			const RECT modified{};
			release = gdi->ReleaseDC(&modified);
		}

		const HRESULT end = p.renderer.EndFrameDraw(dc);
		if (FAILED(end)) return end;
		if (FAILED(get)) return get;
		if (FAILED(release)) return release;
		return presented ? S_OK : E_FAIL;
	}

	void ShapeEditorSurface::CommitPresented(bool enabled)
	{
		std::scoped_lock lock(impl_->inputMutex);
		const auto generation = impl_->presented.presentationGeneration+1;
		impl_->presented = impl_->candidate;
		impl_->presented.presentationGeneration = generation;
		impl_->presented.input&=enabled;
		impl_->presentPending = !enabled;
	}

	ShapeEditorHit ShapeEditorSurface::Hit(POINT screen) const
	{
		std::scoped_lock lock(impl_->inputMutex);
		return impl_->Hit(impl_->presented, screen);
	}

	bool ShapeEditorSurface::PreservePointerTrajectory(POINT down) const
	{
		std::scoped_lock lock(impl_->inputMutex);
		const auto& frame = impl_->presented;
		const auto hit = impl_->Hit(frame, down);
		if (hit.button || !hit.handle || !frame.snapshot) return false;
		return std::any_of(frame.snapshot->handles.begin(), frame.snapshot->handles.end(), [&](const EditHandle& h)
		{
			return h.id == hit.handle && (h.kind == EditHandleKind::Angle || h.kind == EditHandleKind::Rotate);
		});
	}

	void ShapeEditorSurface::SetActionSink(std::function<void(const ShapeUiAction&)> sink)
	{
		std::scoped_lock lock(impl_->inputMutex);
		impl_->actionSink = std::move(sink);
	}

	void ShapeEditorSurface::ReleaseDeviceResources()
	{
		impl_->renderer.DiscardDeviceResources();
		for (auto& b : impl_->buttons) if (b) b->icon.ResetCache();
		impl_->colorWheel.ResetCache();
		impl_->bracketGeometry.Reset(); impl_->connectorStyle.Reset();
	}

	std::uint64_t ShapeEditorSurface::PresentedGeneration() const
	{
		std::scoped_lock lock(impl_->inputMutex);
		return impl_->presented.presentationGeneration;
	}

	bool ShapeEditorSurface::Pointer(UINT message, POINT point, bool captureOwned, std::uint64_t expectedPresentation, short wheelDelta)
	{
		auto& p = *impl_;
		Impl::Frame frame;
		ShapeEditorHit hit;
		// 拖动中只需最新帧做坐标换算，跳过逐move的控制点/本体命中。
		{
			std::scoped_lock lock(p.inputMutex);
			frame = p.presented;
			if (!p.gesture) hit = p.Hit(frame, point);
		}
		// Down/悬停按已成功呈现的最新帧命中；只要事件对应的帧确实呈现过，就不因之后又呈现了新帧而吞掉按下。
		const bool presentedEarlier = (message == WM_LBUTTONDOWN || message == WM_MOUSEMOVE) && expectedPresentation<frame.presentationGeneration;
		if ((expectedPresentation && expectedPresentation != frame.presentationGeneration && !presentedEarlier && !p.gesture
			&& !p.paletteGesture) || message == WM_CANCELMODE || message == WM_CAPTURECHANGED || (!captureOwned && (p.gesture || p.paletteGesture)))
		{
			CancelPointer();
			return true;
		}

		if (message == WM_MOUSEWHEEL) return hit.occupied;
		if (p.dismissGesture)
		{
			if (message == WM_LBUTTONUP) { p.dismissGesture = false; p.HoldToolbar(frame); }
			return true;
		}
		if (message == WM_LBUTTONDOWN && hit.button == OutsideLayerHit)
		{
			p.HoldToolbar(frame,true);
			p.CloseTopLayer(); p.dismissGesture = true; p.focused = 0;
			return true;
		}

		if (p.gesture)
		{
			if (!frame.snapshot || frame.snapshot->identity.session != p.gestureIdentity.session
				|| frame.snapshot->identity.documentGeneration != p.gestureIdentity.documentGeneration
				|| frame.snapshot->identity.displayGeneration != p.gestureIdentity.displayGeneration)
			{
				CancelPointer();
				return true;
			}
			if (message == WM_MOUSEMOVE || message == WM_LBUTTONUP)
			{
				const bool up = message == WM_LBUTTONUP;
				std::uint64_t upGeneration = 0;
				if (up)
				{
					const auto current = ProductShapeUiSnapshot();
					upGeneration = current ? current->identity.generation : frame.snapshot->identity.generation;
				}
				ShapeUiAction a;
				a.identity = p.gestureIdentity;
				a.kind = p.constructionGesture ? (up ? ShapeUiActionKind::ConstructionCommitPointEdit : ShapeUiActionKind::ConstructionPreviewPointEdit) : (up ? ShapeUiActionKind::CommitEdit : ShapeUiActionKind::PreviewEdit);
				a.handleId = p.handle;
				a.down = p.down;
				a.point = p.CanvasPoint(frame, point); a.point.x -= p.pointerOffset.x; a.point.y -= p.pointerOffset.y;
				if (!up) a.predictedPoint=p.inputPrediction;
				if (!p.constructionGesture) { p.TrackLocal(frame, a.point, up, upGeneration,a.predictedPoint); a.angleDelta = p.localEdit.AngleDelta(); }
				p.Emit(a);
				p.predictionAction = a.predictedPoint ? std::optional<ShapeUiAction>(a) : std::nullopt;
				p.predictionDeadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(30);
				if (up)
				{
					p.gesture = false;
					p.pressedHandle = UINT32_MAX;
				}
			}
			return true;
		}

		if (p.paletteGesture)
		{
			if (!frame.snapshot || frame.snapshot->identity.session != p.pickerIdentity.session
				|| frame.snapshot->identity.objectId != p.pickerIdentity.objectId || !p.pickerOpen.load()) { CancelPointer(); return true; }
			if (message == WM_MOUSEMOVE || message == WM_LBUTTONUP) { p.HoldToolbar(frame,message != WM_LBUTTONUP); p.UpdatePickerPoint(frame, point); }
			if (message == WM_LBUTTONUP)
			{
				ShapeUiAction action; action.kind = ShapeUiActionKind::SetStyle; action.identity = p.pickerIdentity;
				action.style = p.pickerBaseStyle;
				{
					std::scoped_lock lock(p.inputMutex);
					action.style.strokeRgb = action.style.fillRgb = UiColorToShapeRgb(p.pickerDragColor);
					p.pickerDragging = false; p.pickerLocked = false;
				}
				// 当前Draw3没有样式预览事务；松手仅提交一次，绝不逐Move产生历史。
				p.paletteGesture = false; p.Emit(action);
			}
			return true;
		}

		if (message == WM_LBUTTONDOWN && hit.occupied && !hit.button && (hit.body || hit.handle))
		{
			p.ClearToolbarHold(true);
			p.focused = 0;
			p.gesture = true; p.constructionGesture = frame.snapshot->construction.active;
			p.gestureIdentity = hit.identity;
			p.handle = hit.body ? 0 : hit.handle;
			p.down = p.CanvasPoint(frame, point); p.pointerOffset = {};
			if (p.handle) {
				const auto real = std::find_if(frame.snapshot->handles.begin(),frame.snapshot->handles.end(),[&](const auto& h){return h.id == p.handle;});
				const auto visual = std::find_if(frame.visualHandles.begin(),frame.visualHandles.end(),[&](const auto& h){return h.id == p.handle;});
				if (real != frame.snapshot->handles.end() && visual != frame.visualHandles.end() && real->kind != EditHandleKind::Rotate) { p.pointerOffset = {visual->position.x-real->position.x,visual->position.y-real->position.y}; p.down.x -= p.pointerOffset.x; p.down.y -= p.pointerOffset.y; }
			}
			p.pressedHandle = p.handle;
			ShapeUiAction a;
			a.kind = p.constructionGesture ? ShapeUiActionKind::ConstructionBeginPointEdit : ShapeUiActionKind::BeginEdit;
			a.identity = hit.identity;
			a.handleId = p.handle;
			a.down = a.point = p.down;
			if (!p.Emit(a))
			{
				p.gesture = false;
				p.pressedHandle = UINT32_MAX;
				return true;
			}
			p.localEditActive = !p.constructionGesture && p.localEdit.Begin(*frame.snapshot->object, p.handle, p.down, {frame.snapshot->displayScale, .25f});
			p.localBaseRevision = frame.snapshot->object->revision;
			p.localRevision = 0;
			return true;
		}

		if (message == WM_LBUTTONDOWN && hit.button == PaletteHit)
		{
			p.HoldToolbar(frame,true);
			p.paletteGesture = true;
			p.pickerIdentity = frame.snapshot->identity; p.pickerBaseStyle = frame.snapshot->object->style;
			{
				std::scoped_lock lock(p.inputMutex); p.pickerDragging = true; p.pickerLocked = false;
				p.pickerStablePoint = point; p.pickerStableTime = std::chrono::steady_clock::now();
			}
			p.UpdatePickerPoint(frame, point);
			return true;
		}

		if (message == WM_LBUTTONDOWN && !hit.button) p.focused = 0;
		if (message == WM_LBUTTONDOWN && hit.button) p.HoldToolbar(frame,true);
		if (message == WM_LBUTTONUP)
		{
			std::scoped_lock lock(p.inputMutex);
			if (p.toolbarOperationActive) { p.toolbarOperationActive=false; p.toolbarHoldUntil=p.toolbarNow()+std::chrono::seconds(3); }
		}
		ButtonPointerEvent event = message == WM_LBUTTONDOWN ? ButtonPointerEvent::Down : message == WM_LBUTTONUP ? ButtonPointerEvent::Up : message == WM_MOUSELEAVE ? ButtonPointerEvent::Leave : ButtonPointerEvent::Move;
		unsigned activate = 0;
		{
			std::scoped_lock lock(p.inputMutex);
			auto r = p.pointer.Update(event, hit.button);
			activate = static_cast<unsigned>(r.activate);
			p.hover = static_cast<unsigned>(r.hover);
			p.pressed = static_cast<unsigned>(r.pressed);
			p.hoveredHandle = hit.occupied && !hit.button ? hit.handle : UINT32_MAX;
			if (r.activate) p.focused = static_cast<unsigned>(r.activate);
		}

		if (activate) p.Activate(activate, frame, point);
		return hit.occupied;
	}

	bool ShapeEditorSurface::Contact(UINT message, POINT screen, std::uint64_t source, std::uint32_t contactId,
		bool touch, bool pen, double seconds, bool captureOwned, std::uint64_t presentedRevision)
	{
		auto& p=*impl_;
		const ShapeContactGesture::Key key{source,contactId};
		// 捕获中的 Leave 只是悬停边界，不是取消；无接触的鼠标仍走普通按钮悬停。
		if (message==WM_MOUSELEAVE) return p.contactOwner ? true : Pointer(message,screen,captureOwned,presentedRevision);
		if (message==WM_MOUSEMOVE && !p.contactOwner) return Pointer(message,screen,captureOwned,presentedRevision);
		if (message==WM_CANCELMODE || message==WM_CAPTURECHANGED || !captureOwned) { CancelPointer(); return true; }
		Impl::Frame frame;
		{ std::scoped_lock lock(p.inputMutex); frame=p.presented; }
		if (!frame.snapshot || !frame.snapshot->object) { CancelPointer(); return false; }
		if (p.gesture && (frame.snapshot->identity.session!=p.gestureIdentity.session
			|| frame.snapshot->identity.documentGeneration!=p.gestureIdentity.documentGeneration
			|| frame.snapshot->identity.displayGeneration!=p.gestureIdentity.displayGeneration)) { CancelPointer(); return true; }
		const auto rawPoint=p.CanvasPoint(frame,screen);
		const auto addPredictor=[&](Vec2 point)
		{
			auto predictor=std::make_unique<ShapeDragPredictor>(); predictor->Begin(key,point,seconds,*frame.snapshot);
			p.predictors.push_back(std::move(predictor));
		};
		if (message==WM_LBUTTONDOWN)
		{
			if (p.contactOwner)
			{
				// 控件/按钮仍由首指独占；只有中心区域的同源触摸可组成多指变换。
				if (!touch || pen || !p.contactTouch || p.contactPen || !p.gesture || !p.contacts.Count()) return true;
				const auto hit=Hit(screen);
				if (!hit.occupied || hit.button || (!hit.body && hit.handle!=100)) return true;
				// 新指加入前先收回旧预测，静止的新接触也不能让外推位置滞留。
				TickPrediction(std::chrono::steady_clock::time_point::max());
				if (p.contacts.Add(key,rawPoint,true)) addPredictor(rawPoint);
				return true;
			}
			p.inputPrediction.reset(); p.predictionAction.reset(); p.contacts.Reset(); p.predictors.clear();
			const bool accepted=Pointer(message,screen,captureOwned,presentedRevision);
			if (!accepted) return false;
			p.contactOwner=key; p.contactTouch=touch; p.contactPen=pen;
			if (p.gesture)
			{
				if (!p.constructionGesture && (p.handle==0 || p.handle==100))
				{
					const auto preset=EditorPreset(*frame.snapshot->object);
					p.contacts.Add(key,rawPoint,preset<PresetKind::Cuboid || preset>PresetKind::Pyramid);
					addPredictor(rawPoint);
				}
				else addPredictor({rawPoint.x-p.pointerOffset.x,rawPoint.y-p.pointerOffset.y});
			}
			return true;
		}
		if (!p.contactOwner) return false;
		if (touch!=p.contactTouch || pen!=p.contactPen) return true;
		const bool up=message==WM_LBUTTONUP;
		if (message!=WM_MOUSEMOVE && !up) return Pointer(message,screen,captureOwned,presentedRevision);
		if (p.contacts.Count())
		{
			if (!p.contacts.Contains(key)) return true;
			p.contacts.Update(key,rawPoint);
			std::optional<ShapeGestureTransform> predicted;
			if (!up) for (const auto& predictor:p.predictors) if (predictor->key==key)
				if (const auto point=predictor->Move(rawPoint,seconds)) predicted=p.contacts.Predicted(key,*point);
			const auto transform=p.contacts.Current();
			if (up) { p.contacts.Remove(key); std::erase_if(p.predictors,[&](const auto& value){return value->key==key;}); }
			const bool final=up && !p.contacts.Count();
			ShapeUiAction action; action.identity=p.gestureIdentity; action.handleId=p.handle; action.down=p.down;
			action.point=rawPoint; action.kind=final?ShapeUiActionKind::CommitEdit:ShapeUiActionKind::PreviewEdit;
			action.gestureTransform=transform; action.predictedGestureTransform=predicted;
			const auto current=ProductShapeUiSnapshot();
			p.TrackLocal(frame,rawPoint,final,current?current->identity.generation:frame.snapshot->identity.generation,
				{},&transform,predicted?&*predicted:nullptr);
			p.Emit(action);
			p.predictionAction=predicted?std::optional<ShapeUiAction>(action):std::nullopt;
			p.predictionDeadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(30);
			if (final) { p.gesture=false; p.pressedHandle=UINT32_MAX; p.contactOwner.reset(); p.contacts.Reset(); p.predictors.clear(); }
			return true;
		}
		if (key!=*p.contactOwner) return true;
		p.inputPrediction.reset();
		if (!up && p.gesture) for (const auto& predictor:p.predictors) if (predictor->key==key)
			p.inputPrediction=predictor->Move({rawPoint.x-p.pointerOffset.x,rawPoint.y-p.pointerOffset.y},seconds);
		const bool accepted=Pointer(message,screen,captureOwned,presentedRevision);
		p.inputPrediction.reset();
		if (up) { p.contactOwner.reset(); p.predictors.clear(); p.predictionAction.reset(); }
		return accepted;
	}

	std::chrono::steady_clock::time_point ShapeEditorSurface::TickPrediction(std::chrono::steady_clock::time_point now)
	{
		auto& p=*impl_;
		if (!p.predictionAction) return std::chrono::steady_clock::time_point::max();
		if (now<p.predictionDeadline) return p.predictionDeadline;
		auto action=*p.predictionAction; p.predictionAction.reset();
		Impl::Frame frame; { std::scoped_lock lock(p.inputMutex); frame=p.presented; }
		if (p.gesture && frame.snapshot && frame.snapshot->identity.session==action.identity.session)
		{
			// 停指后预测只能存活一个有限窗口，回到最后真实位置，不向历史追加一笔。
			action.predictedPoint.reset(); action.predictedGestureTransform.reset();
			if (!p.constructionGesture) p.TrackLocal(frame,action.point,false,0,{},action.gestureTransform?&*action.gestureTransform:nullptr);
			p.Emit(action);
		}
		return std::chrono::steady_clock::time_point::max();
	}

	void ShapeEditorSurface::CancelPointer()
	{
		auto& p = *impl_;
		const bool dragging = p.gesture;
		p.contacts.Reset(); p.predictors.clear(); p.contactOwner.reset(); p.inputPrediction.reset(); p.predictionAction.reset();
		if (p.gesture)
		{
			ShapeUiAction a;
			a.kind = p.constructionGesture ? ShapeUiActionKind::ConstructionCancelPointEdit : ShapeUiActionKind::CancelEdit;
			a.identity = p.gestureIdentity;
			p.Emit(a);
		}

		std::scoped_lock lock(p.inputMutex);
		if (p.toolbarOperationActive) { p.toolbarOperationActive=false; p.toolbarHoldUntil=p.toolbarNow()+std::chrono::seconds(3); }
		p.gesture = p.paletteGesture = p.dismissGesture = false;
		p.pickerDragging = p.pickerLocked = false;
		p.pointer.Reset();
		p.hover = p.pressed = 0;
		p.hoveredHandle = UINT32_MAX;
		p.pressedHandle = UINT32_MAX;
		// 取消拖动立即退回权威快照；已抬手等待提交的本地候选不受普通取消影响。
		if (dragging)
		{
			p.localEdit.Cancel();
			p.localEditActive = false;
			p.localSnapshot.reset();
			p.localGeometry.reset();
			p.localPending = false;
		}
	}

	bool ShapeEditorSurface::NeedsPresent() const noexcept
	{
		return impl_->presentPending;
	}

	bool ShapeEditorSurface::HasKeyboardFocus() const
	{
		std::scoped_lock lock(impl_->inputMutex);
		const auto id = impl_->focused.load();
		return id>0 && id<Count && impl_->presented.input && impl_->presented.buttons[id].visible && impl_->presented.buttons[id].enabled;
	}

	bool ShapeEditorSurface::Keyboard(BYTE key, bool down)
	{
		auto& p = *impl_;
		Impl::Frame frame;
		{
			std::scoped_lock lock(p.inputMutex);
			frame = p.presented;
		}

		if (!frame.snapshot || !frame.visible) return false;
		if (key == VK_ESCAPE)
		{
			if (down)
			{
				if (p.CloseTopLayer()) { p.HoldToolbar(frame); CancelPointer(); return true; }
				CancelPointer();
				p.ClearToolbarHold(false);
				ShapeUiAction a;
				a.kind = ShapeUiActionKind::CloseEditor;
				a.identity = frame.snapshot->identity;
				p.Emit(a);
			}
			return true;
		}

		// 弹层收回、成员隐藏或布局运动后，旧focus id不能继续占用前台应用的按键。
		const auto focusId = p.focused.load();
		if (!frame.input || !focusId || focusId >= Count || !frame.buttons[focusId].visible || !frame.buttons[focusId].enabled) return false;
		if (key == VK_TAB || key == VK_RIGHT || key == VK_LEFT || key == VK_UP || key == VK_DOWN)
		{
			if (down)
			{
				const int step = ((key == VK_LEFT || key == VK_UP) || (key == VK_TAB && (GetKeyState(VK_SHIFT)&0x8000))) ? -1 : 1;
				for (unsigned k = 0; k<Count; ++k)
				{
					p.focused = (p.focused.load()+Count+step)%Count;
					if (frame.buttons[p.focused.load()].visible && frame.buttons[p.focused.load()].enabled) break;
				}
			}
			return true;
		}

		if (key == VK_RETURN || key == VK_SPACE)
		{
			if (down && p.focused && frame.buttons[p.focused.load()].enabled) p.Activate(p.focused.load(), frame, {});
			return true;
		}

		return false;
	}

	int RunShapeEditorOffscreenTest()
	{
		_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
		const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if (FAILED(com)) return 2;
		if (FAILED(RenderPipeline::Initialize()))
		{
			CoUninitialize();
			return 3;
		}

		std::filesystem::create_directories(L"Build/shape-p34-ui");
		std::ofstream report("Build/shape-p34-ui/results.log");
		int checks = 0, failures = 0, scenes = 0, commits = 0;
		auto expect = [&](bool ok, const char* why)
		{
			++checks;
			if (!ok)
			{
				++failures;
				report<<"FAIL "<<why<<" case="<<scenes<<'\n';
			}
		};
		{
			ShapeContactGesture gesture;
			const ShapeContactGesture::Key first{1,7}, second{2,7};
			expect(gesture.Add(first,{0,0},true) && gesture.Add(second,{100,0},true),"same contact id on different sources is independent");
			gesture.Update(second,{0,200});
			expect(std::abs(gesture.Current().scale-2)<.001f && std::abs(gesture.Current().rotation-1.5707963f)<.001f,"two contacts resolve uniform scale and rotation");
			const auto before=gesture.Current(); gesture.Remove(first);
			expect(gesture.Current().translation==before.translation && gesture.Current().scale==before.scale,"lifting first finger does not jump");
			gesture.Update(second,{20,220});
			expect(std::abs(gesture.Current().translation.x-before.translation.x-20)<.001f && std::abs(gesture.Current().translation.y-before.translation.y-20)<.001f,"remaining finger continues panning after topology change");
			gesture.Reset(); gesture.Add(first,{0,0},false); gesture.Add(second,{100,0},false); gesture.Update(second,{0,200});
			expect(gesture.Current().rotation==0 && std::abs(gesture.Current().scale-2)<.001f,"3D contact transform rejects rotation but permits scale");
			const auto raw=gesture.Current(); (void)gesture.Predicted(second,{20,220});
			expect(gesture.Current().translation==raw.translation && gesture.Current().scale==raw.scale,"forecast copy cannot alter real contact transform");
			ShapeUiSnapshot state; state.predictionMode=2;
			ShapeDragPredictor predictor; predictor.Begin(first,{0,0},10,state);
			bool predicted=false; for(int i=1;i<40;++i) predicted|=predictor.Move({static_cast<float>(i*8),0},10+i*.008).has_value();
			expect(predicted,"editor uses drawing model to forecast a real moving contact");
			(void)predictor.Move({312,0},10.312); expect(predictor.ready,"duplicate timestamp does not poison future prediction");
			(void)predictor.Move({312,0},11); predicted=false;
			for(int i=1;i<40;++i) predicted|=predictor.Move({static_cast<float>(312+i*8),0},11+i*.008).has_value();
			expect(predicted,"prediction resumes after a genuine stationary pause");
			state.predictionMode=0; predictor.Begin(first,{0,0},20,state);
			expect(!predictor.Move({100,0},20.01),"disabled drawing prediction also disables editor prediction");
		}
		// 复用现有无窗口CPU事务测试，UI3命令同时验证辅助开关真实的一条Replace及撤销/重做。
		expect(RunShapeDocumentProductionTest() == 0, "production shape journal auxiliary toggles, undo and redo pass");
		expect(ShapeRgbToUiColor(0x1E8CDCu) == RGB(30, 140, 220) && UiColorToShapeRgb(RGB(30, 140, 220)) == 0x1E8CDCu,
			"shape RGB and UI COLORREF keep explicit red and blue channels");
		expect(ShapeRgbToUiColor(0x1E8CDCu) != RGB(220, 140, 30) && UiColorToShapeRgb(RGB(30, 140, 220)) != 0xDC8C1Eu,
			"red-blue-swapped negative color fixture is rejected");
		// 容量为零的发布端模拟QueueFull，验证已开始手势的终结不会丢失或持续唤醒。
		for (auto terminal : {ShapeUiActionKind::CommitEdit, ShapeUiActionKind::CancelEdit, ShapeUiActionKind::CloseEditor,
			ShapeUiActionKind::ConstructionComplete, ShapeUiActionKind::ConstructionCancel, ShapeUiActionKind::ConstructionForward})
		{
			auto state = std::make_shared<ShapeUiSnapshot>();
			state->identity.session = 77;
			state->identity.objectId[0] = 1;
			const bool construction = terminal == ShapeUiActionKind::ConstructionComplete || terminal == ShapeUiActionKind::ConstructionCancel
				|| terminal == ShapeUiActionKind::ConstructionForward;
			// 生产构造快照仅有身份和步骤状态，不包含可编辑 object。
			if (construction)
			{
				state->construction.active = true;
				state->construction.preset = PresetKind::Arc;
				state->construction.totalSteps = 2;
				state->construction.canForward = true;
			}
			else
			{
				state->object = ShapeObject{};
				state->object->id = state->identity.objectId;
			}
			bool full = false;
			std::vector<ShapeUiAction> accepted;
			ShapeIntentSender sender{[&](const ShapeUiAction& action)
			{
				if (full) return Bridge::CommandResult::QueueFull;
				accepted.push_back(action);
				return Bridge::CommandResult::Accepted;
			}, [&]
			{
				return state;
			}};
			if (!construction)
			{
				ShapeUiAction begin;
				begin.kind = ShapeUiActionKind::BeginEdit;
				begin.identity = state->identity;
				expect(sender.Send(begin), "capacity fixture first accepts BeginEdit");
			}
			const std::size_t expected = construction ? 1 : 2;
			full = true;
			ShapeUiAction finish;
			finish.kind = terminal;
			finish.identity = state->identity;
			expect(sender.Send(finish), "QueueFull retains one terminal intent");
			for (unsigned revision = 1; revision <= 128; ++revision)
			{
				finish.identity.generation = revision;
				expect(sender.Send(finish), "same-scene generation changes coalesce to one bounded terminal slot");
			}
			const auto now = std::chrono::steady_clock::now();
			expect(sender.Tick(now) != std::chrono::steady_clock::time_point::max(), "pending terminal alone requests a finite worker tick");
			full = false;
			expect(sender.Tick(now) == std::chrono::steady_clock::time_point::max() && accepted.size() == expected && accepted.back().kind == terminal,
				"restored capacity delivers terminal once and allows worker sleep");
			full = true;
			expect(sender.Send(finish), "second terminal can become pending");
			++state->identity.session;
			full = false;
			expect(sender.Tick(now) == std::chrono::steady_clock::time_point::max() && accepted.size() == expected,
				"old-session pending terminal never crosses a new editor or construction session");
			const auto sleeping = sender.Tick(now);
			if (construction) for (int changed = 0; changed < 7; ++changed)
			{
				state->identity = finish.identity; state->construction.active = true;
				full = true; expect(sender.Send(finish), "construction identity fixture retains pending intent");
				switch (changed)
				{
				case 0: ++state->identity.documentGeneration; break;
				case 1: ++state->identity.displayGeneration; break;
				case 2: ++state->identity.workspace; break;
				case 3: state->identity.pageId[0] ^= 1; break;
				case 4: ++state->identity.pageIndex; break;
				case 5: state->identity.objectId[0] ^= 1; break;
				case 6: state->construction.active = false; break;
				}
				full = false;
				expect(sender.Tick(now) == sleeping && accepted.size() == expected,
					"retired construction scene, object or inactive session never receives a pending terminal");
			}
		}

		auto now = std::chrono::steady_clock::now();
		BarStyleClass style;
		SetThemeStyleSource(&style);
		// 对照完整旧内联公式：主题过渡、缩放、半透明入场和悬停/按下中途均逐像素一致。
		for (double weight : {0.0, .5, 1.0}) for (int state = 0; state < 3; ++state) for (bool moving : {false, true})
		{
			BarUIRendering thumbRenderer;
			expect(SUCCEEDED(thumbRenderer.EnsureDeviceResources(RenderPipeline::GetDeviceEpoch(), 256, 128)), "Thumb regression target initializes");
			auto* dc = thumbRenderer.GetDeviceContext();
			if (!dc) continue;
			const FLOAT zoom = static_cast<FLOAT>(1 + weight);
			const double panelScale = moving ? .63 : 1.0, thumbScale = moving ? .89 : 1.0;
			const double progress = moving ? BarUiApplyCurve(BarUiCurveEnum::EaseOutCubic, .5) : 1.0;
			const double accentOpacity = 1 + (state == 1 ? -.10 : state == 2 ? -.20 : 0) * progress;
			const double centerDiameter = 12 + (state == 1 ? 2 : state == 2 ? -2 : 0) * progress;
			const double opacity = moving ? .37 : 1.0;
			const COLORREF surface = MixBarUiColor(RGB(19, 23, 27), RGB(251, 251, 251), moving ? .31 : weight);
			const COLORREF accent = GetThemeColor(weight < 1 ? BarThemeModeEnum::Dark : BarThemeModeEnum::Light, BarThemeColorEnum::Accent);
			const FLOAT diameterPx = static_cast<FLOAT>(20 * panelScale * thumbScale * zoom);
			const FLOAT centerPx = static_cast<FLOAT>(centerDiameter) * static_cast<FLOAT>(panelScale * thumbScale) * zoom;
			thumbRenderer.SetFrameZoom(zoom);
			thumbRenderer.BeginFrameDraw(dc, {0, 0, 256, 128});
			BarSliderThumbStyle thumbStyle;
			thumbStyle.centerPx = {64, 64};
			thumbStyle.outerDiameterPx = diameterPx;
			thumbStyle.centerDiameterPx = centerPx;
			thumbStyle.accentOpacity = accentOpacity;
			thumbStyle.lightWeight = weight;
			thumbStyle.opacity = opacity;
			thumbStyle.frameThicknessPx = static_cast<FLOAT>(panelScale * zoom);
			thumbStyle.surfaceColor = surface;
			thumbStyle.accentColor = accent;
			DrawBarSliderThumb(thumbRenderer, dc, thumbStyle);
			const auto darkFill = MixBarUiColor(GetThemeColor(BarThemeModeEnum::Dark, BarThemeColorEnum::Surface),
				GetThemeColor(BarThemeModeEnum::Dark, BarThemeColorEnum::TextPrimary), .20);
			const auto darkFrame = MixBarUiColor(darkFill, GetThemeColor(BarThemeModeEnum::Dark, BarThemeColorEnum::TextPrimary), .12);
			const auto lightFrame = MixBarUiColor(GetThemeColor(BarThemeModeEnum::Light, BarThemeColorEnum::Surface),
				GetThemeColor(BarThemeModeEnum::Light, BarThemeColorEnum::TextPrimary), BarThemeMaterial::LightSliderThumbBorderOpacity);
			const auto oldCircle = [&](COLORREF color, FLOAT radius)
			{
				if (radius > 0) if (auto* brush = thumbRenderer.ContentSolidBrush(color, opacity))
					dc->FillEllipse(D2D1::Ellipse({192, 64}, radius, radius), brush);
			};
			const FLOAT radius = diameterPx / 2.0F;
			oldCircle(MixBarUiColor(darkFrame, lightFrame, weight), radius);
			oldCircle(MixBarUiColor(darkFill, RGB(255, 255, 255), weight),
				(std::max)(0.0F, radius - static_cast<FLOAT>(panelScale * zoom)));
			oldCircle(MixBarUiColor(surface, accent, accentOpacity), (std::min)(radius * .70F, centerPx / 2.0F));
			thumbRenderer.FinishFrameDrawCommands(dc);
			expect(SUCCEEDED(thumbRenderer.EndFrameDraw(dc)), "Thumb regression draw completes");
			const auto pixels = ReadEditorPixels(thumbRenderer, {256, 128});
			bool identical = !pixels.empty();
			for (int y = 0; y < 128 && identical; ++y) for (int x = 0; x < 128; ++x)
			{
				const auto offset = (static_cast<size_t>(y) * 256 + x) * 4;
				if (std::memcmp(pixels.data() + offset, pixels.data() + offset + 128 * 4, 4) != 0) identical = false;
			}
			expect(identical, "complete old Thumb formula matches helper pixels including center, animated stroke, alpha and intermediate material");
		}

		// 覆盖重叠输入热区及主题/内容倍率，检查可见Thumb外环的最终alpha，不只检查brush参数。
		for (bool dark : {false, true}) for (int background : {0, 50, 100}) for (double content : {0.0, .6, 1.0}) for (float zoom : {1.f, 2.f})
		{
			style.darkStyle = dark;
			style.backgroundOpacity = background;
			style.contentOpacity = static_cast<int>(content * 100);
			BarUIRendering hitRenderer;
			expect(SUCCEEDED(hitRenderer.EnsureDeviceResources(RenderPipeline::GetDeviceEpoch(), 128, 128)), "invisible input regression target initializes");
			auto* dc = hitRenderer.GetDeviceContext();
			if (!dc) continue;
			hitRenderer.SetFrameZoom(zoom);
			hitRenderer.SetResidentContentOpacity(content);
			ShapeUiSnapshot hitSnapshot;
			hitSnapshot.handles = {{100, EditHandleKind::Center, {64, 64}}, {101, EditHandleKind::Radius, {68, 64}}};
			hitSnapshot.bounds.valid = true;
			hitSnapshot.bounds.left = hitSnapshot.bounds.top = 48;
			hitSnapshot.bounds.right = hitSnapshot.bounds.bottom = 80;
			hitRenderer.BeginFrameDraw(dc, {0, 0, 128, 128});
			DrawEditorInputCoverage(hitRenderer, dc, hitSnapshot, {}, zoom);
			BarSliderThumbStyle thumbStyle;
			thumbStyle.centerPx = {64, 64};
			thumbStyle.outerDiameterPx = 20 * zoom;
			thumbStyle.centerDiameterPx = 12 * zoom;
			thumbStyle.frameThicknessPx = zoom;
			thumbStyle.lightWeight = GetThemeLightWeight();
			thumbStyle.opacity = content;
			thumbStyle.surfaceColor = GetThemeColor(BarThemeColorEnum::Surface);
			thumbStyle.accentColor = GetThemeColor(BarThemeColorEnum::Accent);
			thumbStyle.compositeOpacity = true;
			DrawBarSliderThumb(hitRenderer, dc, thumbStyle);
			hitRenderer.SetResidentContentOpacity(1);
			hitRenderer.FinishFrameDrawCommands(dc);
			expect(SUCCEEDED(hitRenderer.EndFrameDraw(dc)), "invisible input regression draw completes");
			const auto pixels = ReadEditorPixels(hitRenderer, {128, 128});
			bool invisible = !pixels.empty();
			unsigned covered = 0;
			for (int y = 0; y < 128 && invisible; ++y) for (int x = 0; x < 128; ++x)
			{
				const double distance = std::hypot(x + .5 - 64, y + .5 - 64);
				if (distance < 10 * zoom + 2 || distance > 22 * zoom - 1) continue;
				const BYTE alpha = pixels[(static_cast<size_t>(y) * 128 + x) * 4 + 3];
				invisible &= alpha <= 1;
				covered += alpha == 1;
			}
			expect(invisible && covered > 100, "overlapping hit areas outside Thumb stay alpha <= 1 under every theme and resident opacity");
		}
		style.backgroundOpacity = 0;
		style.contentOpacity = 100;

		for (bool dark : {false, true}) for (const wchar_t* language : {L"zh-CN", L"zh-TW", L"en-US"}) for (float scale : {.75f, 1.f, 1.5f,
			2.f}) for (unsigned presetIndex = 0; presetIndex <= static_cast<unsigned>(PresetKind::Pyramid); ++presetIndex)
		{
			++scenes;
			style.darkStyle = dark;
			expect(I18n::load(1, L"JSON", language), "language resources load");
			const auto preset = static_cast<PresetKind>(presetIndex);
			ToolSpec tool;
			tool.preset = preset;
			tool.creationMode = DefaultCreationMode(preset);
			tool.sides = 5;
			ShapeStyle shapeStyle;
			shapeStyle.width = 4*scale;
			shapeStyle.strokeRgb = shapeStyle.fillRgb = 0x1E8CDCu;
			shapeStyle.strokeOpacity = shapeStyle.fillOpacity = .6f;
			if (preset == PresetKind::Circle) shapeStyle.auxiliary = Auxiliary::Center;
			Construction construction;
			construction.tool = tool;
			construction.start = {130*scale, 150*scale};
			construction.end = {300*scale, 250*scale};
			if (GetToolCapabilities(preset).points) construction.parameters = VertexConstruction{{{130*scale, 130*scale}, {310*scale, 150*scale},
				{280*scale, 280*scale}, {180*scale, 200*scale}}};
			else if (preset == PresetKind::Arc || preset == PresetKind::Sector) construction.parameters = ArcConstruction{4.3f};
			else if (preset == PresetKind::Cuboid || preset == PresetKind::Cube) construction.parameters = SolidConstruction{-120*scale,{120*scale,120*scale}};
			else if (preset >= PresetKind::Cylinder) construction.parameters = SolidConstruction{-120*scale,{120*scale,120*scale}};
			ObjectId id{};
			id[0] = 1;
			id[1] = static_cast<unsigned char>(presetIndex+1);
			auto object = MakeConstructedShape(construction, shapeStyle, id, 1);
			expect(object.has_value(), "approved preset yields editable construction");
			if (!object) continue;
			auto snapshot = std::make_shared<ShapeUiSnapshot>();
			snapshot->object = *object;
			snapshot->identity.session = 1;
			snapshot->identity.generation = 1;
			snapshot->identity.objectId = id;
			snapshot->identity.objectRevision = 1;
			snapshot->identity.documentGeneration = 1;
			snapshot->identity.displayGeneration = 1;
			snapshot->canvasOriginX = -1200;
			snapshot->canvasOriginY = -300;
			snapshot->displayScale = scale;
			snapshot->handles = BuildEditHandles(*object, {scale}, &snapshot->frameRect);
			GeometryResult geometry;
			expect(BuildGeometry(*object, geometry, {scale}), "production geometry builds");
			snapshot->bounds = geometry.bounds;
			expect(snapshot->frameRect.valid, "every preset publishes the shared gapped frame");
			expect(std::count_if(snapshot->handles.begin(), snapshot->handles.end(), [](const EditHandle& h)
			{
				return h.kind == EditHandleKind::Scale;
			}) == (preset >= PresetKind::Cuboid ? 4 : 0), "only solids expose four frame corners; planar frames have no scale handles");
			ShapeEditorSurface scene;
			std::vector<ShapeUiAction> actions;
			scene.SetActionSink([&](const auto& action)
			{
				actions.push_back(action);
			});
			const RECT work{-1200, -300, static_cast<LONG>(-1200+900*scale), static_cast<LONG>(-300+700*scale)};
			scene.Configure(snapshot, work, {});
			now += std::chrono::milliseconds(17); scene.Advance(now);
			for (unsigned id = 1; id<Count; ++id) if (id != Rotate)
				expect(scene.impl_->x[id].IsSame() && scene.impl_->y[id].IsSame(), "first frame places every member directly at its target, including hidden members");
			for (size_t di = 0; di<scene.impl_->dividers.size(); ++di)
			{
				const auto& d = *scene.impl_->dividers[di];
				expect(scene.impl_->dividerX[di].IsSame() && scene.impl_->dividerY[di].IsSame(), "first-frame divider position equals target");
				expect(d.button.w.val == 1 && d.button.h.val == BarButtonOneSideDip-4 && d.button.frameLightColor == BarUiFrameLightColorEnum::Frame
					&& !d.button.framePrimaryLightEnabled && d.button.frameCursorLightIntensityScale == BarUiDividerCursorLightIntensity
					&& d.button.framePct->val == 0
					&& d.button.fill->val == GetThemeColor(BarThemeColorEnum::Divider)
					&& d.button.frame->val == GetThemeColor(BarThemeColorEnum::Divider), "divider parameters and color equal GeometryAttribute SingleRow template");
			}
			expect(scene.impl_->toolbarMotion.Active() && scene.impl_->toolbarMotion.Geometry() == 0, "new session opens using compact panel motion");
			// 同一实例切换主题，不能仅验证重新构造时的分隔线默认色。
			style.darkStyle = !dark; now += std::chrono::milliseconds(17); scene.Advance(now);
			for (const auto& d : scene.impl_->dividers) expect(d->button.fill->val == GetThemeColor(BarThemeColorEnum::Divider)
				&& d->button.frame->val == GetThemeColor(BarThemeColorEnum::Divider)
				&& d->button.lightMaterial == GetThemeLightWeight(), "theme switch updates divider fill, frame and material in place");
			style.darkStyle = dark; now += std::chrono::milliseconds(17); scene.Advance(now);
			for (int frame = 0; frame<70; ++frame)
			{
				now += std::chrono::milliseconds(17);
				scene.Advance(now);
			}
			expect(scene.Visible(), "editor opens with shared animation");
			expect(!scene.AnimationActive(), "settled editor allows render sleep");
			bool selectedIcon = false;
			for (unsigned id = Style; id<Thickness; ++id)
			{
				const auto& b = *scene.impl_->buttons[id];
				if (b.state->state != BarWidgetState::Selected) continue;
				selectedIcon = true;
				expect(b.icon.color1->val == GetThemeColor(BarThemeColorEnum::Accent)
					&& b.icon.color2->val == GetThemeColor(BarThemeColorEnum::Accent)
					&& b.name.color.val == GetThemeColor(BarThemeColorEnum::Accent), "selected editor icon and text use the common Accent role");
			}
			expect(selectedIcon, "selected-icon assertion exercises a selected production button");
			for (const auto& b : scene.impl_->buttons) if (b) expect(b->button.lightMaterial == GetThemeLightWeight(), "every button shares the frame light-material weight");
			for (const auto& swatch : scene.impl_->swatchSurfaces) expect(swatch.lightMaterial == GetThemeLightWeight(), "every swatch shares the frame light-material weight");
			expect(scene.impl_->pickerPreview.lightMaterial == GetThemeLightWeight() && scene.impl_->holdSurface.lightMaterial == GetThemeLightWeight(), "picker shapes share the frame light-material weight");
			if (!dark)
			{
				const auto& b = scene.impl_->buttons[Pattern+1]->button;
				auto material = BarThemeMaterial::Resolve(b.lightMaterial, b.fill->val, b.frame->val);
				material.edgeLight = BarThemeMaterial::ObjectEdgeLight(material.lightWeight, b.frame->val);
				const auto lighting = BarThemeMaterial::ResolveCursorLighting(material, RGB(0, 0, 0), 0);
				expect(lighting.color == BarThemeMaterial::LightColor(BarThemeMaterial::ColorRole::EdgeLight)
					&& std::abs(lighting.diffuseOpacity-BarThemeMaterial::LightFrameDiffuseOpacity)<.000001
					&& material.primaryLightRadiusScale == BarThemeMaterial::LightPrimaryRadiusScale
					&& material.cursorLightRadiusScale == BarThemeMaterial::LightCursorRadiusScale, "light editor button resolves light-theme point-light color, diffuse and radii");
			}
			expect(SUCCEEDED(scene.EnsureDeviceResources(RenderPipeline::GetDeviceEpoch())), "shared epoch creates private editor target");
			unsigned nontransparent = 0;
			SIZE receipt{};
			auto sink = [&](const UPDATELAYEREDWINDOWINFO& info)->BOOL
			{
				expect(info.hdcSrc && info.psize && info.psize->cx>0 && info.psize->cy>0, "production ULW sink receives actual DC and extent");
				receipt = *info.psize;
				++commits;
				return TRUE;
			};
			auto present = [&]()
			{
				// 与生产Render同序：布局改变ULW尺寸后，先重建当前target，不能读取旧目标外的零像素。
				const HRESULT resource = scene.EnsureDeviceResources(RenderPipeline::GetDeviceEpoch());
				if (FAILED(resource)) return resource;
				const HRESULT hr = scene.Present(sink);
				if (FAILED(hr)) return hr;
				const auto targetSize = scene.Renderer().GetTargetBitmap()->GetPixelSize();
				expect(targetSize.width == receipt.cx && targetSize.height == receipt.cy, "actual ULW extent matches this frame target before content readback");
				const auto pixels = ReadEditorPixels(scene.Renderer(), receipt);
				expect(pixels.size() == static_cast<size_t>(receipt.cx)*receipt.cy*4, "actual committed ULW source crop can be read after EndDraw");
				nontransparent = 0;
				for (size_t pixel = 3; pixel<pixels.size(); pixel += 4) if (pixels[pixel])++nontransparent;
				if (scene.impl_->appliedContentOpacity<.99) return hr;
				// 低透明度矩阵另测alpha，字形证据要求不透明像素。
				const auto& candidate = scene.impl_->candidate;
				if(candidate.snapshot && candidate.snapshot->drawingPreview) {
					// 首笔只读提示不画工具栏；验证真实斜杠像素，不能用旧编辑器控件坐标取样。
					expect(nontransparent>0 && nontransparent<500 && scene.impl_->handles.empty()&&!scene.impl_->toolbarMotion.Visible(),"read-only preview source contains marks instead of editor chrome");return hr;
				}
				auto glyph = [&](unsigned id, bool word, COLORREF expected)
				{
					const auto& button = *scene.impl_->buttons[id];
					const double z = candidate.scale;
					D2D1_RECT_F dip = word ? D2D1_RECT_F{static_cast<float>(button.name.inhX+3), static_cast<float>(button.name.inhY+3),
						static_cast<float>(button.name.inhX+button.name.w.val-3),
						static_cast<float>(button.name.inhY+button.name.h.val-3)} : D2D1_RECT_F{static_cast<float>(button.icon.inhX),
						static_cast<float>(button.icon.inhY), static_cast<float>(button.icon.inhX+button.icon.w.val), static_cast<float>(button.icon.inhY+button.icon.h.val)};
					const auto pose = candidate.poses[EditorPanel(id)];
					if (id != Rotate) dip = EditorRect(pose.Apply(EditorRect(dip)));
					const bool badge = id >= Swatch && id<Tone;
					const std::optional<COLORREF> badgeBase = badge ? std::optional<COLORREF>(RGB(0, 200, 129)) : std::nullopt;
					if (badge)
					{
						// 原colorSelect白勾位于固定绿色圆内；裁掉外圈，不能按主题选中底块推算细线覆盖。
						const float insetX = (dip.right-dip.left)/4, insetY = (dip.bottom-dip.top)/4;
						dip.left += insetX;
						dip.right -= insetX;
						dip.top += insetY;
						dip.bottom -= insetY;
					}
					D2D1_RECT_F rect{static_cast<float>(dip.left*z), static_cast<float>(dip.top*z), static_cast<float>(dip.right*z), static_cast<float>(dip.bottom*z)};
					const POINT blank{static_cast<LONG>((id == Rotate ? button.button.inhX+button.button.w.val/2 : pose.x+(button.button.inhX+button.button.w.val/2)*pose.scale)*z),
						static_cast<LONG>((id == Rotate ? button.button.inhY+2 : pose.y+(button.button.inhY+2)*pose.scale)*z)};
					if (id == Rotate)
					{
						// 上层色板/取色器按既定z序遮住控制点时，不能要求读取下层图标的像素。
						const auto covers = [&](D2D1_RECT_F layer) { return layer.left*z<=rect.left && layer.top*z<=rect.top && layer.right*z>=rect.right && layer.bottom*z>=rect.bottom && Inside(layer,blank.x/z,blank.y/z); };
						if ((candidate.pickerLayer && covers(candidate.picker)) || (candidate.paletteLayer && covers(candidate.colors)) || (candidate.sidesLayer && covers(candidate.parameters))) {
							expect(blank.x>=0 && blank.y>=0 && blank.x<receipt.cx && blank.y<receipt.cy && pixels[(static_cast<size_t>(blank.y)*receipt.cx+blank.x)*4+3]>1,"occluding popup has actual rendered coverage above the rotation handle"); return;
						}
					}
					const auto evidence = InspectEditorGlyph(pixels, receipt, rect, blank, expected, badgeBase);
					if (id==Rotate && scenes==322 && !(evidence.pixels>=2 && evidence.peak>.30)) {
						report<<"ROTATE_DIAGNOSTIC bounds="<<candidate.bounds.left<<','<<candidate.bounds.top<<" toolbar="<<candidate.toolbar.left<<','<<candidate.toolbar.top<<','<<candidate.toolbar.right<<','<<candidate.toolbar.bottom<<" colors="<<candidate.colors.left<<','<<candidate.colors.top<<','<<candidate.colors.right<<','<<candidate.colors.bottom<<" picker="<<candidate.picker.left<<','<<candidate.picker.top<<','<<candidate.picker.right<<','<<candidate.picker.bottom<<" opacity="<<candidate.opacity<<" edit="<<scene.impl_->editOpacity.val<<" glyph="<<button.icon.inhX<<','<<button.icon.inhY<<" transform="<<candidate.snapshot->object->transform.m11<<','<<candidate.snapshot->object->transform.m12<<'\n';
						(void)SaveEditorPng(L"Build/shape-p34-ui/rotate-diagnostic-322.png",pixels.data(),receipt.cx,receipt.cy,receipt.cx*4);
					}

					if (!(evidence.pixels >= 2 && evidence.peak>.30)) report<<"GLYPH id="<<id<<" word="<<word<<" count="<<evidence.pixels<<" peak="<<evidence.peak<<" width="<<rect.right-rect.left<<" height="<<rect.bottom-rect.top<<" rect="<<rect.left<<','<<rect.top<<" bg="<<blank.x<<','<<blank.y<<" bgRgb="<<EditorPixelRgb(pixels, receipt, blank.x, blank.y)[0]<<','<<EditorPixelRgb(pixels, receipt, blank.x, blank.y)[1]<<','<<EditorPixelRgb(pixels, receipt, blank.x, blank.y)[2]<<'\n';
					expect(evidence.pixels >= 2 && evidence.peak>.30,
						word ? "actual emitted side number has foreground ink beyond selected background" : "actual emitted SVG has foreground ink beyond alpha1 hit region and selected background");
					const D2D1_RECT_F empty{0, 0, static_cast<float>(20*z), static_cast<float>(20*z)};
					const auto negative = InspectEditorGlyph(pixels, receipt, empty, {1, 1}, expected, badgeBase);
					expect(negative.pixels == 0, "glyph oracle rejects an actual blank source crop");
					if (badge)
					{
						const double cy = pose.y+(button.button.inhY+button.button.h.val/2)*pose.scale;
						const D2D1_RECT_F padding{static_cast<float>((pose.x+(button.button.inhX+2)*pose.scale)*z), static_cast<float>((cy-2*pose.scale)*z),
							static_cast<float>((pose.x+(button.button.inhX+4)*pose.scale)*z), static_cast<float>((cy+2*pose.scale)*z)};
						const auto baseOnly = InspectEditorGlyph(pixels, receipt, padding, blank, expected, badgeBase);
						expect(baseOnly.pixels == 0, "badge white-stroke oracle rejects the real selected button background without its SVG");
					}
					if (id == Rotate)
					{
						const auto surface = EditorPixelRgb(pixels, receipt, blank.x, blank.y);
						const auto theme = dark ? MixBarUiColor(GetThemeColor(BarThemeModeEnum::Dark, BarThemeColorEnum::Surface),
							GetThemeColor(BarThemeModeEnum::Dark, BarThemeColorEnum::TextPrimary), .20) : RGB(255, 255, 255);
						expect(surface[0] >= 0 && std::abs(surface[0]-GetRValue(theme))<14 && std::abs(surface[1]-GetGValue(theme))<14
							&& std::abs(surface[2]-GetBValue(theme))<14, "rotation control uses the shared Thumb outer fill rather than alpha1 hit coverage");
					}
				};
				for (unsigned id = Style; id<Thickness; ++id) if (candidate.buttons[id].visible) glyph(id, false, scene.impl_->buttons[id]->icon.color1->val);
				for (const auto& h : snapshot->handles) if (h.kind == EditHandleKind::Rotate) glyph(Rotate, false, scene.impl_->buttons[Rotate]->icon.color1->val);
				for (unsigned id = Sides; id<Swatch; ++id) if (candidate.buttons[id].visible) glyph(id, true, scene.impl_->buttons[id]->name.color.val);
				for (unsigned bit = 0; bit<AuxiliaryBits.size(); ++bit) if (candidate.buttons[Aux+bit].visible)
				{
					const auto& button = *scene.impl_->buttons[Aux+bit];
					expect(button.name.enable.val && button.name.content.GetVal() == ShapeAuxiliaryLabel(AuxiliaryBits[bit],preset)
						&& scene.Renderer().MeasureText(button.name.content.GetVal(),button.name.size.val).width <= button.name.w.val+.5
						&& button.button.w.val > BarButtonOneSideDip,"floating auxiliary entry reserves a real measured label slot beside its icon");
					glyph(Aux+bit,false,button.icon.color1->val); glyph(Aux+bit,true,button.name.color.val);
				}
				if (scene.impl_->colorOpen.load())
				{
					if (candidate.pickerLayer) glyph(Tone, false, scene.impl_->buttons[Tone]->icon.color1->val);
					for (unsigned id = Color; id<Tone; ++id) if (id == Color || id >= Swatch)
					{
						const auto& b = *scene.impl_->buttons[id];
						const auto& cell = candidate.buttons[id].boundsDip;
						const double cx = (cell.left+cell.right)/2, cy = (cell.top+cell.bottom)/2;
						const auto color = EditorPixelRgb(pixels, receipt, static_cast<int>(cx*candidate.scale), static_cast<int>(cy*candidate.scale));
						const auto expected = id == Color ? candidate.pickerColor : GetPresetColor(static_cast<BarThemePresetColorEnum>(id-Swatch));
						if (id >= Swatch && b.state->state == BarWidgetState::Selected) glyph(id, false, RGB(255, 255, 255));
						else
						{
							const bool rgb = color[0] >= 0 && std::abs(color[0]-GetRValue(expected))<14 && std::abs(color[1]-GetGValue(expected))<14
								&& std::abs(color[2]-GetBValue(expected))<14;
							if (!rgb) report<<"COLOR id="<<id<<" x="<<cx*candidate.scale<<" y="<<cy*candidate.scale<<" actual="<<color[0]<<','<<color[1]<<','<<color[2]<<" expected="<<int(GetRValue(expected))<<','<<int(GetGValue(expected))<<','<<int(GetBValue(expected))<<'\n';
							expect(rgb, "actual swatch center keeps its theme RGB color and never aliases the gray frame brush");
						}
					}
				}
				if (scale == 1 && std::wstring(language) == L"zh-CN" && scene.impl_->colorOpen.load() && !pixels.empty())
				{
					const auto image = std::filesystem::path(L"Build/shape-p34-ui")/(L"editor-"+std::to_wstring(presetIndex)+(dark ? L"-dark.png" : L"-light.png"));
					expect(SaveEditorPng(image, pixels.data(), receipt.cx, receipt.cy, receipt.cx*4), "production editor source emits PNG evidence");
				}
				return hr;
			};
			expect(SUCCEEDED(present()), "editor shared draw GetDC ReleaseDC EndDraw transaction succeeds");
			scene.CommitPresented();
			expect(nontransparent>100, "actual emitted editor bitmap contains controls");
			for(const auto& h : snapshot->handles) if(h.kind==EditHandleKind::Rotate) {
				const auto& frame=scene.impl_->presented;
				const double cx=(snapshot->canvasOriginX+h.position.x-frame.bounds.left)/scale,cy=(snapshot->canvasOriginY+h.position.y-frame.bounds.top)/scale;
				expect(frame.toolbar.right<=cx-14 || frame.toolbar.left>=cx+14 || frame.toolbar.bottom<=cy-14 || frame.toolbar.top>=cy+14,"settled toolbar leaves the full rotation control visible and operable");
			}
			if(preset==PresetKind::Circle && scale==1 && dark && std::wstring(language)==L"zh-CN") {
				const auto visual=ResolveEditorFrame(*snapshot,scene.Bounds(),scale);const auto& rect=visual.rounded.rect;
				bool covered=true;for(const D2D1_POINT_2F q:std::array<D2D1_POINT_2F,4>{{{rect.left,rect.top},{rect.right,rect.top},{rect.right,rect.bottom},{rect.left,rect.bottom}}}) {
					const auto v=visual.transform.TransformPoint(q);covered&=v.x>=3&&v.y>=3&&v.x+3<=receipt.cx&&v.y+3<=receipt.cy;
				}
				expect(covered,"circle frame bounds include thick decorative corner stroke and antialias fringe");
			}
			if(preset==PresetKind::Rectangle && scale==1 && dark && std::wstring(language)==L"zh-CN") {
				// 换边只改变UI起点；旋转必须围绕真实中心跟随指针，不能减去原顶部偏移。
				const auto& f=snapshot->frameRect;const auto center=ApplyTransform(snapshot->object->transform,{(f.left+f.right)/2,(f.top+f.bottom)/2});
				for(unsigned side:{1u,2u,3u}) {
					scene.impl_->handleDockSide=side;scene.Configure(snapshot,work,{});now+=std::chrono::milliseconds(17);scene.Advance(now);expect(SUCCEEDED(present()),"side dock rotation frame presents");scene.CommitPresented();
					const auto& hs=scene.impl_->presented.visualHandles;const auto r=std::find_if(hs.begin(),hs.end(),[](const auto& h){return h.kind==EditHandleKind::Rotate;});if(r==hs.end())continue;
					const POINT down{static_cast<LONG>(snapshot->canvasOriginX+r->position.x),static_cast<LONG>(snapshot->canvasOriginY+r->position.y)};
					const Vec2 localDown{static_cast<float>(down.x-snapshot->canvasOriginX),static_cast<float>(down.y-snapshot->canvasOriginY)};
					const POINT end{snapshot->canvasOriginX+static_cast<LONG>(center.x-(localDown.y-center.y)),snapshot->canvasOriginY+static_cast<LONG>(center.y+(localDown.x-center.x))};
					const auto priorSink=scene.impl_->actionSink;std::vector<ShapeUiAction> trajectory;scene.SetActionSink([&](const auto& action){trajectory.push_back(action);});
					scene.Pointer(WM_LBUTTONDOWN,down,true,scene.PresentedGeneration());scene.Pointer(WM_MOUSEMOVE,end,true,scene.PresentedGeneration());
					expect(!trajectory.empty()&&std::hypot(trajectory.front().down.x-localDown.x,trajectory.front().down.y-localDown.y)<.01,"side-docked rotate starts at actual visual pointer");
					EditSession turn;if(!trajectory.empty()&&turn.Begin(*snapshot->object,r->id,trajectory.front().down)&&turn.Preview(trajectory.back().point,2)) {
						const auto result=turn.Candidate();expect(result&&std::abs(std::atan2(result->transform.m12,result->transform.m11)-std::atan2(snapshot->object->transform.m12,snapshot->object->transform.m11)-1.57079632679)<.03,"side-docked quarter circle produces quarter turn");
					}
					scene.CancelPointer();scene.SetActionSink(priorSink);
				}
				scene.impl_->handleDockSide=0;scene.Configure(snapshot,work,{});now+=std::chrono::milliseconds(17);scene.Advance(now);expect(SUCCEEDED(present()),"restore dock after rotation trajectory");scene.CommitPresented();
			}
			if(preset==PresetKind::Ellipse && scale==1 && dark && std::wstring(language)==L"zh-CN") {
				auto foci=std::make_shared<ShapeUiSnapshot>(*snapshot);foci->object->style.auxiliary=foci->object->style.auxiliary|Auxiliary::Foci;++foci->identity.generation;
				scene.Configure(foci,work,{});for(int i=0;i<30;++i){now+=std::chrono::milliseconds(17);scene.Advance(now);}expect(SUCCEEDED(present()),"paired auxiliary foci render");scene.CommitPresented();
				std::vector<EditHandle> real,visual;for(const auto& h:foci->handles)if(h.kind==EditHandleKind::Focus)real.push_back(h);for(const auto& h:scene.impl_->presented.visualHandles)if(h.kind==EditHandleKind::Focus)visual.push_back(h);
				if(real.size()==2&&visual.size()==2) {
					const double ax=real[0].position.x-real[1].position.x,ay=real[0].position.y-real[1].position.y,len=(std::max)(1.,std::hypot(ax,ay));
					const double x0=visual[0].position.x-real[0].position.x,y0=visual[0].position.y-real[0].position.y,x1=visual[1].position.x-real[1].position.x,y1=visual[1].position.y-real[1].position.y;
					expect(std::abs(((x0+x1)*ax+(y0+y1)*ay)/len)<.1&&std::abs((-(x0-x1)*ay+(y0-y1)*ax)/len)<.1,"unobstructed focus controls mirror across ellipse short axis");
				}
				scene.Configure(snapshot,work,{});for(int i=0;i<30;++i){now+=std::chrono::milliseconds(17);scene.Advance(now);}expect(SUCCEEDED(present()),"restore auxiliary setting after paired regression");scene.CommitPresented();
			}
			if(preset==PresetKind::Polyline && scale==1 && dark && std::wstring(language)==L"zh-CN") {
				auto points=std::make_shared<ShapeUiSnapshot>(*snapshot);const Vec2 departingPoint{static_cast<float>(scene.Bounds().right-snapshot->canvasOriginX+200),static_cast<float>(scene.Bounds().bottom-snapshot->canvasOriginY+200)};
				points->handles.push_back({9999,EditHandleKind::Vertex,departingPoint});++points->identity.generation;
				scene.Configure(points,work,{});for(int i=0;i<30;++i){now+=std::chrono::milliseconds(17);scene.Advance(now);}expect(SUCCEEDED(present()),"outlying confirmed point enters full window");scene.CommitPresented();
				auto removed=std::make_shared<ShapeUiSnapshot>(*points);removed->handles.pop_back();++removed->identity.generation;
				scene.Configure(removed,work,{});now+=std::chrono::milliseconds(17);scene.Advance(now);expect(SUCCEEDED(present()),"removed point exit frame renders");scene.CommitPresented();
				const auto pixels=ReadEditorPixels(scene.Renderer(),receipt);const int x=static_cast<int>(snapshot->canvasOriginX+departingPoint.x-scene.Bounds().left),y=static_cast<int>(snapshot->canvasOriginY+departingPoint.y-scene.Bounds().top);
				bool complete=x>=12&&y>=12&&x+12<receipt.cx&&y+12<receipt.cy;
				if(complete)for(const POINT d:std::array<POINT,4>{{{-5,0},{5,0},{0,-5},{0,5}}})complete&=pixels[(static_cast<size_t>(y+d.y)*receipt.cx+x+d.x)*4+3]>1;
				expect(complete,"undo point exit retains all four sides of its circle inside actual ULW bitmap");
				scene.Configure(snapshot,work,{});for(int i=0;i<30;++i){now+=std::chrono::milliseconds(17);scene.Advance(now);}expect(SUCCEEDED(present()),"removed point fully retires");scene.CommitPresented();
			}
			if (preset==PresetKind::Rectangle && scale==1 && dark && std::wstring(language)==L"zh-CN") {
				const auto originalDock=scene.impl_->handleDock;RECT clipped=work;clipped.top=static_cast<LONG>(snapshot->canvasOriginY+originalDock.y+1);
				scene.impl_->pressedHandle=1;scene.Configure(snapshot,clipped,{});now+=std::chrono::milliseconds(17);scene.Advance(now);
				expect(scene.impl_->handleDockSide==0,"pressed control retains docking edge even when it leaves work area");
				scene.impl_->pressedHandle=UINT32_MAX;now+=std::chrono::milliseconds(17);scene.Advance(now);
				const auto chosen=scene.impl_->handleDockSide;expect(chosen!=0,"idle overflowing top control moves to another frame side");
				scene.Configure(snapshot,work,{});now+=std::chrono::milliseconds(17);scene.Advance(now);expect(scene.impl_->handleDockSide==chosen,"restored space does not return sticky control to top");
				const auto& t=scene.impl_->handleTangent;const auto& hs=scene.impl_->candidate.visualHandles;
				const auto r=std::find_if(hs.begin(),hs.end(),[](const auto& h){return h.kind==EditHandleKind::Rotate;}),m=std::find_if(hs.begin(),hs.end(),[](const auto& h){return h.kind==EditHandleKind::Move;});
				if(r!=hs.end()&&m!=hs.end())expect(std::abs((m->position.x-r->position.x)*t.y-(m->position.y-r->position.y)*t.x)<.01,"capsule halves follow selected edge tangent");
				scene.impl_->handleDockSide=0;now+=std::chrono::milliseconds(17);scene.Advance(now);expect(SUCCEEDED(present()),"restore top control after docking regression");scene.CommitPresented();
			}
			if ((preset==PresetKind::Rectangle || preset==PresetKind::Cube || preset==PresetKind::Cylinder) && scale==1 && dark && std::wstring(language)==L"zh-CN") {
				// 通过真实镜像对象重建并呈现，覆盖单按钮、胶囊及撤回后的停靠。
				const auto originalDock=scene.impl_->handleDock,originalTangent=scene.impl_->handleTangent;
				for(bool horizontal:{false,true}) {
					const auto mirrored=FlipShape(*snapshot->object,horizontal,snapshot->object->revision+1,{scale,.25f});
					expect(mirrored.has_value(),"dock fixture mirrors actual shape");if(!mirrored)continue;
					auto next=std::make_shared<ShapeUiSnapshot>(*snapshot);next->object=*mirrored;++next->identity.generation;
					next->handles=BuildEditHandles(*next->object,{scale,.25f},&next->frameRect);
					scene.Configure(next,work,{});for(int i=0;i<30;++i){now+=std::chrono::milliseconds(17);scene.Advance(now);}
					expect(SUCCEEDED(present()),"mirrored dock frame renders");scene.CommitPresented();
					expect(std::hypot(scene.impl_->handleDock.x-originalDock.x,scene.impl_->handleDock.y-originalDock.y)<.1,"mirror keeps top controls on original screen edge");
					expect(scene.impl_->handleTangent.x*originalTangent.x+scene.impl_->handleTangent.y*originalTangent.y>.99,"mirror keeps capsule button order");
					ShapeEditorSurface reopened;
					for(int i=0;i<30;++i){reopened.Configure(next,work,{});now+=std::chrono::milliseconds(17);reopened.Advance(now);}
					expect(std::hypot(reopened.impl_->handleDock.x-originalDock.x,reopened.impl_->handleDock.y-originalDock.y)<.1,"reopening mirrored object starts controls on screen top");
					if(preset==PresetKind::Rectangle) expect(reopened.impl_->handleTangent.x*originalTangent.x+reopened.impl_->handleTangent.y*originalTangent.y>.99,"reopening mirrored object retains capsule order along its docked edge");
					scene.Configure(snapshot,work,{});for(int i=0;i<30;++i){now+=std::chrono::milliseconds(17);scene.Advance(now);}
					expect(SUCCEEDED(present()),"undo mirrored dock renders");scene.CommitPresented();
					expect(std::hypot(scene.impl_->handleDock.x-originalDock.x,scene.impl_->handleDock.y-originalDock.y)<.1,"undo mirror retains top controls");
				}
			}
			if (preset==PresetKind::Rectangle && scale==1 && dark && std::wstring(language)==L"zh-CN") {
				const auto h=scene.impl_->presented.visualHandles.back(); const POINT point{snapshot->canvasOriginX+static_cast<LONG>(h.position.x),snapshot->canvasOriginY+static_cast<LONG>(h.position.y)};
				const RECT main{point.x-30,point.y-30,point.x+30,point.y+30};
				scene.Configure(snapshot,work,main); for(int i=0;i<30;++i){now+=std::chrono::milliseconds(17);scene.Advance(now);} expect(SUCCEEDED(present()),"main bar overlap renders");scene.CommitPresented();
				const auto pixels=ReadEditorPixels(scene.Renderer(),receipt);const int x=point.x-scene.Bounds().left,y=point.y-scene.Bounds().top;
				expect(scene.Hit(point).occupied&&x>=0&&y>=0&&x<receipt.cx&&y<receipt.cy&&!pixels.empty()&&pixels[(static_cast<size_t>(y)*receipt.cx+x)*4+3]>0,"transparent main window bounds do not cut editor pixels or hit testing");
				scene.Configure(snapshot,work,{});for(int i=0;i<30;++i){now+=std::chrono::milliseconds(17);scene.Advance(now);}expect(SUCCEEDED(present()),"restore editor after main bar exclusion");scene.CommitPresented();
			}
			if(preset==PresetKind::Triangle && scale==1 && dark && std::wstring(language)==L"zh-CN") {
				auto preview=std::make_shared<ShapeUiSnapshot>(*snapshot);preview->drawingPreview=true;preview->identity.session=0;preview->object->construction->tool.constraint=PlanarConstraint::Isosceles;
				scene.Configure(preview,work,{});for(int i=0;i<30;++i){now+=std::chrono::milliseconds(17);scene.Advance(now);}expect(SUCCEEDED(present()),"read-only triangle drawing hints render");scene.CommitPresented();
				expect(nontransparent>0&&scene.impl_->handles.empty()&&!scene.impl_->toolbarMotion.Visible()&&!scene.Hit({snapshot->canvasOriginX+130,snapshot->canvasOriginY+150}).occupied,"first-stroke isosceles preview renders marks without editing handles toolbar or input coverage");
				scene.Configure(snapshot,work,{});for(int i=0;i<30;++i){now+=std::chrono::milliseconds(17);scene.Advance(now);}expect(SUCCEEDED(present()),"restore editor after drawing preview");scene.CommitPresented();
			}
			const auto normalPixels = ReadEditorPixels(scene.Renderer(),receipt);
			const auto normalFrame = ResolveEditorFrame(*snapshot,scene.Bounds(),scale);
			const int frameX = static_cast<int>(std::floor(normalFrame.rounded.rect.left+(normalFrame.rounded.rect.right-normalFrame.rounded.rect.left)*.25+normalFrame.transform._31));
			const int frameY = static_cast<int>(std::floor(normalFrame.rounded.rect.top+normalFrame.transform._32));
			if (!normalPixels.empty())
			{
				const int alpha = normalPixels[(static_cast<size_t>(frameY)*receipt.cx+frameX)*4+3];
				expect(ShowEditorFrame(preset) ? alpha>100 : alpha<=1,
					"only Line Arrow and Rectangle omit the rounded frame in actual emitted pixels");
			}
			if (preset == PresetKind::Rectangle) for (const auto& h : snapshot->handles) if (h.kind == EditHandleKind::Rotate)
			{
				const auto& f = snapshot->frameRect;
				const auto top = ApplyTransform(object->transform,{(f.left+f.right)/2,f.top});
				const double axis = std::hypot(object->transform.m21,object->transform.m22);
				const double startX = snapshot->canvasOriginX+top.x-scene.Bounds().left;
				const double startY = snapshot->canvasOriginY+top.y-scene.Bounds().top;
				const double endX = snapshot->canvasOriginX+h.position.x-scene.Bounds().left+object->transform.m21/axis*14*scale;
				const double endY = snapshot->canvasOriginY+h.position.y-scene.Bounds().top+object->transform.m22/axis*14*scale;
				const int px = static_cast<int>(std::floor((startX+endX)/2)),py = static_cast<int>(std::floor((startY+endY)/2));
				if (!normalPixels.empty()) expect(normalPixels[(static_cast<size_t>(py)*receipt.cx+px)*4+3]>100,
					"Rectangle hides only the rounded frame and retains the actual rotation connector");
			}
			// 无焦点场景必须在点击辅助按钮之前验证；真实按钮激活应当取得内容焦点。
			for (BYTE key : std::array<BYTE, 7>{VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN, VK_SPACE, VK_RETURN, VK_TAB})
				expect(!scene.HasKeyboardFocus() && !scene.Keyboard(key, true) && !scene.Keyboard(key, false),
					"object-only editor leaves navigation keys to the foreground application");
			if (preset == PresetKind::Rectangle && scale == 1 && std::wstring(language) == L"zh-CN")
			{
				ShapeEditorKeyboardOwnership ownership;
				unsigned acquisitions = 0, continuations = 0;
				const auto key = [&](BYTE vk, bool down)
				{
					return ownership.Route(vk, down, [&]
					{
						++acquisitions;
						return scene.HasKeyboardFocus() && scene.Keyboard(vk, true);
					}, [&] { ++continuations; (void)scene.Keyboard(vk, down); });
				};
				scene.impl_->focused = Color;
				expect(key(VK_SPACE, true) && scene.impl_->colorOpen.load(), "owned Space Down opens the real palette layer");
				for (int f = 0; f<70; ++f) { now += std::chrono::milliseconds(17); scene.Advance(now); }
				expect(SUCCEEDED(present()), "Space-opened palette presents"); scene.CommitPresented();
				expect(!scene.HasKeyboardFocus(), "opening a layer disables the previously focused toolbar entry");
				expect(key(VK_SPACE, true) && key(VK_SPACE, false) && acquisitions == 1 && continuations == 2,
					"owned Space repeat and Up stay owned after layer focus changes");
				scene.Keyboard(VK_ESCAPE, true);
				for (int f = 0; f<70; ++f) { now += std::chrono::milliseconds(17); scene.Advance(now); }
				expect(SUCCEEDED(present()), "keyboard layer close presents"); scene.CommitPresented();
				expect(!key(VK_SPACE, true), "external Space Down remains forwarded");
				scene.impl_->focused = Color;
				expect(scene.HasKeyboardFocus() && !key(VK_SPACE, true) && !key(VK_SPACE, false)
					&& acquisitions == 2 && continuations == 2 && !scene.impl_->colorOpen.load(),
					"external Space repeat and Up never acquire ownership after editor focus changes");
				expect(key(VK_LEFT, true) && key(VK_LEFT, false) && acquisitions == 3 && continuations == 3,
					"a fresh virtual-key press independently acquires and releases editor ownership");
				scene.impl_->focused = 0;
			}
			if (preset == PresetKind::Rectangle || preset == PresetKind::Circle || preset == PresetKind::Ellipse)
			{
				const auto defaults = GetShapeStyle(GetStateModeSnapshot());
				for (unsigned bit = 0; bit<AuxiliaryBits.size(); ++bit) if ((SupportedAuxiliary(preset)&AuxiliaryBits[bit]) != Auxiliary::None)
				{
					actions.clear(); const auto frame = scene.impl_->presented; const auto rect = frame.buttons[Aux+bit].boundsDip;
					const POINT point{frame.bounds.left+static_cast<LONG>((rect.left+rect.right)*scale/2), frame.bounds.top+static_cast<LONG>((rect.top+rect.bottom)*scale/2)};
					expect(scene.Hit(point).button == Aux+bit, "supported auxiliary icon-and-label entry keeps one production hit");
					scene.Pointer(WM_LBUTTONDOWN, point); scene.Pointer(WM_LBUTTONUP, point);
					expect(actions.size() == 1 && actions[0].kind == ShapeUiActionKind::SetStyle
						&& static_cast<uint8_t>(actions[0].style.auxiliary) == (static_cast<uint8_t>(object->style.auxiliary)^static_cast<uint8_t>(AuxiliaryBits[bit])), "auxiliary click emits exactly one current-object style action");
					if (actions.size() == 1)
					{
						const auto changed = BuildShapePropertyCandidate(*object, actions[0], 2, {scale});
						expect(changed.has_value(), "auxiliary action enters canonical property candidate path");
						if (changed)
						{
							auto feedback = frame; auto applied = std::make_shared<ShapeUiSnapshot>(*frame.snapshot); applied->object = *changed; feedback.snapshot = applied;
							actions.clear(); scene.impl_->Activate(Aux+bit, feedback, point);
							expect(actions.size() == 1 && actions[0].style.auxiliary == object->style.auxiliary, "second toggle removes the bit through one style action");
						}
					}
					expect(GetShapeStyle(GetStateModeSnapshot()) == defaults, "object auxiliary changes preserve next-stroke tool defaults");
					expect(scene.HasKeyboardFocus() && scene.Keyboard(VK_SPACE, false), "activated bar content owns navigation focus");
				}
				actions.clear();
			}
			const auto frameVisual = ResolveEditorFrame(*snapshot, scene.Bounds(), scale);
			expect(std::abs(frameVisual.thickness-2*scale)<.001 && std::abs(frameVisual.rounded.radiusX-6*scale)<.001
				&& std::abs(frameVisual.rounded.radiusY-6*scale)<.001, "frame uses exact 2DIP stroke and 6DIP radii");
			expect(std::abs(std::hypot(frameVisual.transform._11, frameVisual.transform._12)-1)<.001
				&& std::abs(std::hypot(frameVisual.transform._21, frameVisual.transform._22)-1)<.001, "frame transform contains no object scale");
			expect(std::abs(snapshot->frameRect.left-(geometry.bounds.left-8*scale))<.01
				&& std::abs(snapshot->frameRect.top-(geometry.bounds.top-8*scale))<.01
				&& std::abs(snapshot->frameRect.right-(geometry.bounds.right+8*scale))<.01
				&& std::abs(snapshot->frameRect.bottom-(geometry.bounds.bottom+8*scale))<.01, "shared frame keeps 8DIP gap from stroke-inclusive bounds");
			for (const auto& h : snapshot->handles) if (h.kind == EditHandleKind::Rotate)
			{
				const auto& f = snapshot->frameRect;
				const auto top = ApplyTransform(object->transform, {(f.left+f.right)/2, f.top});
				expect(std::abs(std::hypot(h.position.x-top.x, h.position.y-top.y)-24*scale)<.01, "rotate center is 24DIP outward from frame top midpoint");
			}

			const bool matrix = std::wstring(language) == L"zh-CN" && scale >= 1 && (preset == PresetKind::Rectangle
				|| preset == PresetKind::Circle || preset == PresetKind::Cuboid);
			if (matrix)
			{
				auto save = [&](const wchar_t* state)
				{
					const auto pixels = ReadEditorPixels(scene.Renderer(), receipt);
					const auto path = std::filesystem::path(L"Build/shape-p34-ui")/(L"visual-"+std::to_wstring(presetIndex)+(dark ? L"-dark-" : L"-light-")+std::to_wstring(static_cast<int>(scale*100))+L"-"+state+L".png");
					expect(!pixels.empty() && SaveEditorPng(path, pixels.data(), receipt.cx, receipt.cy, receipt.cx*4),
						"visual matrix writes the actual production source bitmap");
					report<<"SCREENSHOT "<<path.string()<<'\n';
					std::fprintf(stderr, "Shape editor screenshot: %s\n", path.string().c_str());
				};
				save(L"normal");
				if (preset == PresetKind::Cuboid)
				{
					const auto pixels = ReadEditorPixels(scene.Renderer(), receipt);
					for (const auto& h : snapshot->handles) if (h.kind == EditHandleKind::Scale)
					{
						const double px = snapshot->canvasOriginX + h.position.x - scene.Bounds().left,
							py = snapshot->canvasOriginY + h.position.y - scene.Bounds().top;
						const double midX = (snapshot->bounds.left + snapshot->bounds.right) / 2,
							midY = (snapshot->bounds.top + snapshot->bounds.bottom) / 2;
						// 取角点外侧像素，避开4DIP描边的AA覆盖边界。
						const int cx = static_cast<int>(h.position.x < midX ? std::floor(px) - 1 : std::ceil(px)),
							cy = static_cast<int>(h.position.y < midY ? std::floor(py) - 1 : std::ceil(py));
						if (!pixels.empty()) expect(pixels[(static_cast<size_t>(cy) * receipt.cx + cx) * 4 + 3] <= 1,
							"frame corner bracket follows rounded arc and leaves the corner center free of a solid Thumb disc");
					}
				}
				if (preset == PresetKind::Cuboid)
				{
					for (const auto& h : snapshot->handles) if (h.kind == EditHandleKind::Scale) { scene.impl_->hoveredHandle = h.id; break; }
					for (int f = 0; f<12; ++f) { now += std::chrono::milliseconds(17); scene.Advance(now); }
					expect(SUCCEEDED(present()), "hover bracket cache warms through production Draw");
					const auto created = scene.impl_->bracketGeometryCreations;
					expect(created == 1, "four corners share one canonical bracket path across stroke-width animation");
					for (int f = 0; f<5; ++f)
					{
						now += std::chrono::milliseconds(17); scene.Advance(now);
						expect(SUCCEEDED(present()), "steady hover redraw succeeds");
					}
					expect(scene.impl_->bracketGeometryCreations == created, "steady hover frames create no new bracket geometry");
					scene.ReleaseDeviceResources();
					expect(SUCCEEDED(present()) && scene.impl_->bracketGeometryCreations == created+1, "device resource release rebuilds the bracket cache once");
					scene.impl_->deviceGeneration ^= 1; // 模拟缓存来自旧epoch，下一帧仍用生产Ensure入口处理。
					expect(SUCCEEDED(present()) && scene.impl_->bracketGeometryCreations == created+2, "device epoch mismatch invalidates the bracket cache once");
					scene.impl_->hoveredHandle = UINT32_MAX;
				}
				for (int state = 0; state<3; ++state)
				{
					for (const auto& v : scene.impl_->handles)
					{
						scene.impl_->hoveredHandle = state == 1 ? v->id : UINT32_MAX;
						scene.impl_->pressedHandle = state == 2 ? v->id : UINT32_MAX;
						for (int f = 0; f<12; ++f)
						{
							now += std::chrono::milliseconds(17);
							scene.Advance(now);
						}
						const double expected = (v->kind == EditHandleKind::Center ? 8 : 12)+(state == 1 ? 2 : state == 2 ? -2 : 0);
						expect(std::abs(v->centerDiameter.val-expected)<.001 && std::abs(v->accent.val-(state == 2 ? .80 : state == 1 && v->kind != EditHandleKind::Scale ? .90 : 1.0))<.001,
							"normal hover and pressed Thumb inner diameter and accent match spec");
						expect(std::abs(v->scale.val-1)<.001, "state changes keep the outer Thumb diameter fixed");
						if (v->kind == EditHandleKind::Scale) expect(std::abs(v->bracketThickness.val-(state == 1 ? 5.0 : 4.0))<.001,
							"frame corner bracket uses 4DIP normal/pressed and 5DIP hover stroke");
						if (v->kind == EditHandleKind::Rotate) expect(std::abs(v->iconScale.val-(state == 1 ? 1.08 : state == 2 ? .92 : 1.0))<.001,
							"rotate icon uses the specified state scale");
					}
					expect(SUCCEEDED(present()), "Thumb state matrix presents");
					scene.CommitPresented();
					save(state == 0 ? L"normal" : state == 1 ? L"hover" : L"pressed");
				}
				scene.impl_->hoveredHandle = scene.impl_->pressedHandle = UINT32_MAX;
				for (int f = 0; f<20; ++f)
				{
					now += std::chrono::milliseconds(17);
					scene.Advance(now);
				}
				expect(SUCCEEDED(present()), "normal Thumb state restores");
				scene.CommitPresented();
				expect(!scene.NeedsPresent(), "settled successful frame clears presentation demand");
				style.backgroundOpacity = 50;
				style.contentOpacity = 60;
				now += std::chrono::milliseconds(17);
				scene.Advance(now);
				expect(scene.NeedsPresent() && std::abs(scene.impl_->appliedSurfaceOpacity-.5)<.001
					&& std::abs(scene.impl_->appliedContentOpacity-.6)<.001, "changed resident opacity requests repaint without a new object snapshot");
				expect(SUCCEEDED(present()), "resident content opacity presents");
				scene.CommitPresented();
				save(L"opacity-50-60");
				const auto faded = ReadEditorPixels(scene.Renderer(), receipt);
				// floor选中线穿过的像素中心；lround在1x时可能跳到只有75%覆盖的AA边缘。
				const int fx = static_cast<int>(std::floor(frameVisual.rounded.rect.left+(frameVisual.rounded.rect.right-frameVisual.rounded.rect.left)*.25+frameVisual.transform._31));
				const int fy = static_cast<int>(std::floor(frameVisual.rounded.rect.top+frameVisual.transform._32));
				if (!faded.empty())
				{
					const int alpha = faded[(static_cast<size_t>(fy)*receipt.cx+fx)*4+3];
					expect(ShowEditorFrame(preset) ? std::abs(alpha-138) <= 2 : alpha <= 1,
						"frame pixel follows resident content only for presets with a visible rounded frame");
				}
				if (scene.impl_->connectorStyle) expect(scene.impl_->connectorStyle->GetStartCap() == D2D1_CAP_STYLE_ROUND
					&& scene.impl_->connectorStyle->GetEndCap() == D2D1_CAP_STYLE_ROUND, "rotate connector uses round caps");
				for (const auto& v : scene.impl_->handles) if (v->kind != EditHandleKind::Rotate && v->kind != EditHandleKind::Scale)
				{
					const auto point = v->position;
					const int px = static_cast<int>(snapshot->canvasOriginX+point.x-scene.Bounds().left),
						py = static_cast<int>(snapshot->canvasOriginY+point.y-scene.Bounds().top);
					if (!faded.empty()) expect(std::abs(int(faded[(static_cast<size_t>(py)*receipt.cx+px)*4+3])-153) <= 2,
						"Thumb pixel alpha follows resident content without auxiliary transparency");
					break;
				}
				if (preset == PresetKind::Circle)
				{
					const auto center = std::find_if(scene.impl_->handles.begin(),scene.impl_->handles.end(),[](const auto& v)
					{ return v->kind == EditHandleKind::Center; });
					expect(center != scene.impl_->handles.end() && (object->style.auxiliary&Auxiliary::Center) != Auxiliary::None,
						"Center opacity fixture enables the supported auxiliary bit and keeps its handle");
					if (center != scene.impl_->handles.end())
					{
						const int px = static_cast<int>(snapshot->canvasOriginX+(*center)->position.x-scene.Bounds().left);
						const int py = static_cast<int>(snapshot->canvasOriginY+(*center)->position.y-scene.Bounds().top);
						if (!faded.empty()) expect(std::abs(int(faded[(static_cast<size_t>(py)*receipt.cx+px)*4+3])-153) <= 2,
							"offset Center Thumb retains the full resident content opacity");
						const POINT point{snapshot->canvasOriginX+static_cast<LONG>(std::lround((*center)->position.x)),
							snapshot->canvasOriginY+static_cast<LONG>(std::lround((*center)->position.y))};
						expect(scene.Hit(point).handle == (*center)->id,"offset Center exposes its animated input region");
					}
				}
				// 用真实浮条shape与主栏初始化参数分别绘制，避免仅断言相同的策略公式。
				BarUIRendering materialRenderer;
				expect(SUCCEEDED(materialRenderer.EnsureDeviceResources(RenderPipeline::GetDeviceEpoch(), receipt.cx, receipt.cy)), "resident material comparison target initializes");
				if (auto* dc = materialRenderer.GetDeviceContext())
				{
					materialRenderer.SetFrameZoom(scale);
					const D2D1_RECT_F all{0, 0, static_cast<FLOAT>(receipt.cx), static_cast<FLOAT>(receipt.cy)};
					auto drawSurface = [&](BarUiShapeClass& surface)
					{
						materialRenderer.BeginFrameDraw(dc, all);
						DrawBarBackgroundVisual(materialRenderer, dc, surface, {surface.inhX, surface.inhY});
						materialRenderer.FinishFrameDrawCommands(dc);
						expect(SUCCEEDED(materialRenderer.EndFrameDraw(dc)), "resident surface comparison draw completes");
						return ReadEditorPixels(materialRenderer, receipt);
					};
					auto& toolbar = scene.impl_->toolbar;
					const auto floatingPixels = drawSurface(toolbar);
					BarUiShapeClass mainBar;
					InitShape(mainBar, BarMainBarCornerRadiusDip, BarMainBarFillOpacity);
					mainBar.fill->SetDirect(GetThemeColor(BarThemeModeEnum::Dark, BarThemeColorEnum::Surface));
					mainBar.frame->SetDirect(GetThemeColor(BarThemeModeEnum::Dark, BarThemeColorEnum::SurfaceFrame));
					mainBar.themeSurface = mainBar.residentSurface = true;
					mainBar.lightMaterial = GetThemeLightWeight();
					mainBar.lightFillOpacityScale = BarThemeMaterial::LightResidentSurfaceOpacityScale;
					PlaceShape(mainBar, toolbar.inhX, toolbar.inhY, toolbar.w.val, toolbar.h.val);
					const auto mainPixels = drawSurface(mainBar);
					expect(!floatingPixels.empty() && floatingPixels == mainPixels,
						"background 50/content 60 floating surface has exactly the main bar material pixels");
					const int bx = static_cast<int>((toolbar.inhX + toolbar.w.val / 2) * scale),
						by = static_cast<int>((toolbar.inhY + toolbar.h.val / 2) * scale);
					if (!floatingPixels.empty()) expect(std::abs(int(floatingPixels[(static_cast<size_t>(by) * receipt.cx + bx) * 4 + 3])-128) <= 1,
						"resident background alpha is 50 percent independently of content 60 percent");
					auto drawContent = [&](double opacity)
					{
						materialRenderer.BeginFrameDraw(dc, all);
						materialRenderer.SetResidentContentOpacity(opacity);
						// 取当前可见的第一个属性图标；直线等无填充组的图形会隐藏样式组，不能固定取 Style。
						int visibleId = Color;
						for (int id = Style; id < Sides; ++id)
						{
							if (scene.impl_->buttons[id]->icon.pct.val > .99)
							{
								visibleId = id;
								break;
							}
						}
						auto& icon = scene.impl_->buttons[visibleId]->icon;
						materialRenderer.Svg(dc, icon, {icon.inhX, icon.inhY});
						materialRenderer.SetResidentContentOpacity(1);
						materialRenderer.FinishFrameDrawCommands(dc);
						expect(SUCCEEDED(materialRenderer.EndFrameDraw(dc)), "resident content comparison draw completes");
						return ReadEditorPixels(materialRenderer, receipt);
					};
					const auto fullContent = drawContent(1), fadedContent = drawContent(GetThemeResidentContentOpacity());
					bool scaled = !fullContent.empty() && fadedContent.size() == fullContent.size();
					unsigned ink = 0;
					for (size_t p = 3; p < fullContent.size() && scaled; p += 4)
					{
						ink += fullContent[p] > 10;
						scaled &= std::abs(int(fadedContent[p]) - int(std::lround(fullContent[p] * .6))) <= 1;
					}
					expect(scaled && ink > 10, "resident SVG content alpha is scaled by exactly 0.6");
				}
				style.backgroundOpacity = 0;
				style.contentOpacity = 100;
				now += std::chrono::milliseconds(17);
				scene.Advance(now);
				expect(SUCCEEDED(present()), "resident opacity restores");
				scene.CommitPresented();
			}
			if (preset == PresetKind::Cuboid)
			{
				for (const auto& h : snapshot->handles) if (h.kind == EditHandleKind::Scale || h.kind == EditHandleKind::Vertex)
				{
					const POINT point{snapshot->canvasOriginX + static_cast<LONG>(std::lround(h.position.x)),
						snapshot->canvasOriginY + static_cast<LONG>(std::lround(h.position.y))};
					expect(scene.Hit(point).handle == h.id, h.kind == EditHandleKind::Scale
						? "cuboid frame corner selects scale even inside a dedicated handle hit area"
						: "cuboid front-face corner selects its vertex even inside a frame corner hit area");
				}
				// 专属点后列时仍能赢得等距；旋转钮即使更远也优先。
				auto fixture = scene.impl_->presented;
				auto input = std::make_shared<ShapeUiSnapshot>(*snapshot);
				input->canvasOriginX = input->canvasOriginY = 0;
				input->handles = {{10, EditHandleKind::Scale, {100, 100}}, {1000, EditHandleKind::Vertex, {120, 100}}};
				fixture.snapshot = input; fixture.visualHandles = input->handles;
				fixture.bounds = {};
				fixture.scale = 1;
				fixture.toolbar = {};
				fixture.paletteLayer = fixture.sidesLayer = fixture.pickerLayer = false;
				fixture.buttons = {};
				expect(scene.impl_->Hit(fixture, {110, 100}).handle == 1000, "equal distance chooses dedicated handle independently of iteration order");
				std::reverse(input->handles.begin(), input->handles.end()); fixture.visualHandles = input->handles;
				expect(scene.impl_->Hit(fixture, {110, 100}).handle == 1000, "reversed equal-distance order still chooses dedicated handle");
				input->handles.push_back({1, EditHandleKind::Rotate, {100, 110}}); fixture.visualHandles = input->handles;
				expect(scene.impl_->Hit(fixture, {100, 100}).handle == 1, "rotation hit wins before nearest corner or dedicated handle");
				// 椭圆退化为圆时焦点与中心重合，隐藏辅助标记也必须能够重新拉开焦距。
				fixture.visualHandles = {{100,EditHandleKind::Center,{100,100}},{106,EditHandleKind::Focus,{100,100}},{107,EditHandleKind::Focus,{100,100}}};
				expect(scene.impl_->Hit(fixture,{100,100}).handle == 106,"coincident hidden foci remain adjustable instead of being captured by center move");
			}
			if (preset == PresetKind::Polygon && scale == 1 && dark && std::wstring(language)==L"zh-CN") {
				const auto saved=scene.impl_->presented; auto fixture=saved; auto input=std::make_shared<ShapeUiSnapshot>(*snapshot);
				input->construction.active=true; input->construction.preset=PresetKind::Polygon; input->canvasOriginX=input->canvasOriginY=0; fixture.snapshot=input;fixture.bounds={};fixture.scale=1;fixture.input=false;
				fixture.visualHandles={{1000,EditHandleKind::Vertex,{100,100}}};scene.impl_->presented=fixture;actions.clear();
				expect(scene.Hit({100,100}).handle==1000&&!scene.Hit({300,300}).occupied,"active construction captures only confirmed vertices and leaves blank canvas for adding points");
				scene.Pointer(WM_LBUTTONDOWN,{100,100},true);scene.Pointer(WM_MOUSEMOVE,{110,120},true);scene.Pointer(WM_LBUTTONUP,{110,120},true);
				expect(actions.size()==3&&actions[0].kind==ShapeUiActionKind::ConstructionBeginPointEdit&&actions[1].kind==ShapeUiActionKind::ConstructionPreviewPointEdit&&actions[2].kind==ShapeUiActionKind::ConstructionCommitPointEdit&&!scene.impl_->localEditActive,"construction point drag uses its own three runtime actions without completed-object edit session");
				scene.impl_->presented=saved;
			}
			expect(!scene.Hit({work.right-1, work.top+1}).occupied, "blank canvas stays transparent and cannot reselect old objects");
			const auto& uiHandles=scene.impl_->presented.visualHandles;
			const auto uiRotate=std::find_if(uiHandles.begin(),uiHandles.end(),[](const auto& h){return h.kind==EditHandleKind::Rotate;});
			const auto uiMove=std::find_if(uiHandles.begin(),uiHandles.end(),[](const auto& h){return h.kind==EditHandleKind::Move;});
			if(uiRotate!=uiHandles.end()) expect(uiMove!=uiHandles.end()&&std::abs(std::hypot(uiMove->position.x-uiRotate->position.x,uiMove->position.y-uiRotate->position.y)-20*scale)<.01,"planar capsule has separate aligned rotate and move halves");
			if(preset>=PresetKind::Cuboid) expect(uiRotate==uiHandles.end()&&uiMove!=uiHandles.end(),"fixed projection solid exposes movement without two-dimensional rotation");
			const auto visibleHandles = scene.impl_->presented.visualHandles;
			for (const auto& h : visibleHandles)
			{
				// 每个控件都从权威原形独立起步，不能让前一柄的局部预览污染后续命中。
				scene.CancelPointer();
				// 模拟 Draw3 的终态确认；Up 后正常保留的 localPending 只在新权威代次交还。
				snapshot=std::make_shared<ShapeUiSnapshot>(*snapshot); ++snapshot->identity.generation;
				scene.Configure(snapshot,work,{});
				now+=std::chrono::milliseconds(17); scene.Advance(now);
				expect(SUCCEEDED(present()),"each handle restores its original presented shape"); scene.CommitPresented();
				const auto& currentHandles=scene.impl_->presented.visualHandles;
				const auto current=std::find_if(currentHandles.begin(),currentHandles.end(),[&](const auto& v){return v.id==h.id;});
				expect(current!=currentHandles.end(),"restored shape keeps every dedicated handle");
				if(current==currentHandles.end()) continue;
				POINT screen{snapshot->canvasOriginX+static_cast<LONG>(std::lround(current->position.x)), snapshot->canvasOriginY+static_cast<LONG>(std::lround(current->position.y))};
				const auto hit = scene.Hit(screen);
				expect(hit.occupied, "every current handle can receive input");
				const bool requiredCorner=h.kind==EditHandleKind::Scale || preset==PresetKind::Rectangle && h.kind==EditHandleKind::Vertex;
				const bool solidBase=preset>=PresetKind::Cuboid && preset<=PresetKind::Pyramid && h.id==1001;
				if(requiredCorner||solidBase) expect(hit.handle==h.id&&!hit.body,"each scale corner and solid base must win over body hit");
				if (hit.handle != h.id) continue;
				if (preset >= PresetKind::Cuboid && preset <= PresetKind::Pyramid)
					expect(!scene.PreservePointerTrajectory(screen),"solid position and base Z controls permit latest-position coalescing");
				// 相邻44DIP区域按优先级选最近点。
				actions.clear();
				scene.Contact(WM_LBUTTONDOWN, screen, 71, 1, true, false, 1., true, scene.PresentedGeneration());
				scene.Contact(WM_MOUSEMOVE, {screen.x+7, screen.y-4}, 71, 1, true, false, 1.02, true, scene.PresentedGeneration());
				scene.Contact(WM_LBUTTONUP, {screen.x+7, screen.y-4}, 71, 1, true, false, 1.04, true, scene.PresentedGeneration());
				expect(actions.size() == 3 && actions.front().kind == ShapeUiActionKind::BeginEdit
					&& actions.back().kind == ShapeUiActionKind::CommitEdit, "one handle drag emits begin preview and one commit");
				if (actions.size() == 3) expect(actions[0].identity == actions[2].identity && actions[2].point.x-actions[0].down.x == 7,
					"gesture keeps observed identity and document coordinates");
				if(requiredCorner||solidBase) {
					expect(actions.size()==3&&actions.front().handleId==h.id&&!actions.back().gestureTransform,
						"every corner and solid base stays in its dedicated edit transaction");
					expect(scene.impl_->localSnapshot&&scene.impl_->localSnapshot->object
						&&(scene.impl_->localSnapshot->object->geometry!=snapshot->object->geometry
							||scene.impl_->localSnapshot->object->parts!=snapshot->object->parts),
						"each corner and solid base drag changes geometry instead of only revision");
				}
			}
			scene.CancelPointer(); snapshot=std::make_shared<ShapeUiSnapshot>(*snapshot); ++snapshot->identity.generation;
			scene.Configure(snapshot,work,{});
			now+=std::chrono::milliseconds(17); scene.Advance(now);
			expect(SUCCEEDED(present()),"all-handle regression restores original presentation"); scene.CommitPresented();
			const auto oldBounds = scene.Bounds();
			snapshot = std::make_shared<ShapeUiSnapshot>(*snapshot);
			snapshot->canvasOriginX -= 50;
			++snapshot->identity.generation;
			scene.Configure(snapshot, work, {});
			now += std::chrono::milliseconds(17);
			scene.Advance(now);
			expect(FAILED(scene.Present([](const auto&)->BOOL
			{
				return FALSE;
			})), "failed ULW remains a failure");
			const auto& old = scene.impl_->presented;
			expect(EqualRect(&old.bounds, &oldBounds), "failed presentation preserves old hit snapshot");
			for (int frame = 0; frame<70; ++frame)
			{
				now += std::chrono::milliseconds(17);
				scene.Advance(now);
			}
			expect(SUCCEEDED(present()), "new state can recover after failed ULW");
			scene.CommitPresented();
			const auto& row = scene.impl_->presented;
			const auto color = row.buttons[Color].boundsDip;
			const POINT colorPoint{row.bounds.left+static_cast<LONG>((color.left+color.right)*row.scale/2),
				row.bounds.top+static_cast<LONG>((color.top+color.bottom)*row.scale/2)};
			scene.Pointer(WM_LBUTTONDOWN, colorPoint);
			scene.Pointer(WM_LBUTTONUP, colorPoint);
			for (int frame = 0; frame<70; ++frame)
			{
				now += std::chrono::milliseconds(17);
				scene.Advance(now);
			}
			expect(scene.impl_->colorOpen.load(), "only approved color control opens object palette");
						expect(SUCCEEDED(present()), "object palette reuses production gradient and swatch rendering");
			scene.CommitPresented();
			const auto saveLayer = [&](const wchar_t* layer)
			{
				if (!matrix) return;
				const auto pixels = ReadEditorPixels(scene.Renderer(), receipt);
				const auto path = std::filesystem::path(L"Build/shape-p34-ui")/(L"v2-"+std::to_wstring(presetIndex)+(dark ? L"-dark-" : L"-light-")+std::to_wstring(static_cast<int>(scale*100))+L"-"+layer+L".png");
				expect(!pixels.empty() && SaveEditorPng(path, pixels.data(), receipt.cx, receipt.cy, receipt.cx*4), "bar and layer matrix writes actual production bitmap");
				report<<"SCREENSHOT "<<path.string()<<'\n'; std::fprintf(stderr, "Shape editor screenshot: %s\n", path.string().c_str());
			};
			saveLayer(L"palette");
			scene.impl_->Activate(Custom, scene.impl_->presented, {});
			for (int f = 0; f<70; ++f) { now += std::chrono::milliseconds(17); scene.Advance(now); }
			expect(scene.impl_->candidate.pickerLayer && scene.impl_->candidate.paletteLayer, "custom entry opens picker above palette");
			expect(SUCCEEDED(present()), "picker marker, RGB and preview use production source"); scene.CommitPresented();
						saveLayer(L"picker");
			actions.clear();
			const auto pickerFrame = scene.impl_->presented; const auto palette = pickerFrame.palette;
			const POINT pick{pickerFrame.bounds.left+static_cast<LONG>((palette.left+(palette.right-palette.left)*.3)*scale),
				pickerFrame.bounds.top+static_cast<LONG>((palette.top+(palette.bottom-palette.top)*.4)*scale)};
			scene.Pointer(WM_LBUTTONDOWN, pick); scene.Pointer(WM_MOUSEMOVE, {pick.x+5, pick.y+3});
			expect(actions.empty(), "picker moves emit no object preview or history action");
			now += std::chrono::milliseconds(17); scene.Advance(now);
			expect(scene.impl_->candidate.snapshot->object->style == pickerFrame.snapshot->object->style
				&& scene.impl_->candidate.pickerColor == scene.impl_->pickerDragColor && scene.impl_->candidate.pickerColor != pickerFrame.pickerColor
				&& scene.impl_->candidate.markerVisible
				&& scene.impl_->candidate.markerX == scene.impl_->pickerDragX && scene.impl_->candidate.markerY == scene.impl_->pickerDragY,
				"picker marker and preview change locally while object style stays authoritative");
			expect(SUCCEEDED(present()), "picker drag paints local feedback including actual bar color entry"); scene.CommitPresented();
			scene.Pointer(WM_LBUTTONUP, {pick.x+5, pick.y+3});
			expect(std::count_if(actions.begin(), actions.end(), [](const auto& action) { return action.kind == ShapeUiActionKind::SetStyle; }) == 1
				&& actions.back().kind == ShapeUiActionKind::SetStyle && actions.back().identity == pickerFrame.snapshot->identity,
				"picker release commits one style action with frozen identity");
			actions.clear();
			scene.Pointer(WM_LBUTTONDOWN, pick); scene.Pointer(WM_MOUSEMOVE, {pick.x+8, pick.y+4}); scene.CancelPointer();
			expect(actions.empty() && !scene.impl_->pickerDragging, "cancelled picker drag emits no object transaction");
			scene.Keyboard(VK_ESCAPE, true);
			expect(actions.empty() && !scene.impl_->pickerOpen.load() && scene.impl_->colorOpen.load(), "Escape closes picker and preserves palette");
			for (int f = 0; f<70; ++f) { now += std::chrono::milliseconds(17); scene.Advance(now); }
			expect(SUCCEEDED(present()), "palette remains after picker exit"); scene.CommitPresented();
			actions.clear();
			scene.impl_->Activate(Swatch+2, scene.impl_->presented, {});
			expect(actions.size() == 1 && actions[0].kind == ShapeUiActionKind::SetStyle && actions[0].style.strokeRgb == 0xFF8B00u
				&& actions[0].style.fillRgb == 0xFF8B00u, "orange preset COLORREF maps explicitly to Shape 0xRRGGBB without swapping red and blue");
			if (!actions.empty())
			{
				snapshot = std::make_shared<ShapeUiSnapshot>(*snapshot);
				const auto restyled = RestyleShape(*snapshot->object, actions[0].style, snapshot->object->revision+1,
				{
					scale
				});
				expect(restyled.has_value(), "color fixture applies the same typed restyle operation as production editing");
				if (restyled) snapshot->object = *restyled;
				++snapshot->identity.generation;
				scene.Configure(snapshot, work, {});
				for (int f = 0; f<70; ++f)
				{
					now += std::chrono::milliseconds(17);
					scene.Advance(now);
				}
				expect(SUCCEEDED(present()), "restyling moves the existing colorSelect check to the actual object color");
				scene.CommitPresented();
				expect(scene.impl_->buttons[Swatch+2]->state->state == BarWidgetState::Selected && scene.impl_->buttons[Swatch+2]->icon.pct.val == 1,
					"selected swatch uses shared colorSelect SVG state");
			}
			actions.clear();
			scene.Keyboard(VK_ESCAPE, true);
			expect(actions.empty() && !scene.impl_->colorOpen.load(), "first Escape only closes the existing palette layer");
			scene.Keyboard(VK_ESCAPE, true);
			expect(!actions.empty() && actions.back().kind == ShapeUiActionKind::CloseEditor, "next Escape closes the editor after the palette");
			actions.clear(); scene.impl_->Activate(Finish,scene.impl_->presented,{});
			expect(actions.size()==1 && actions.back().kind==ShapeUiActionKind::CloseEditor && scene.impl_->memberWanted[Finish],"finish check is available and closes the current editor through the terminal action");
			for (unsigned id : {FlipHorizontal,FlipVertical}) {
				expect(scene.impl_->memberWanted[id] == (preset!=PresetKind::Circle),"only rotationally symmetric circle omits screen-axis flips");
				actions.clear(); scene.impl_->Activate(id,scene.impl_->presented,{});
				expect(actions.size()==1 && actions.back().kind==(id==FlipHorizontal ? ShapeUiActionKind::FlipHorizontal : ShapeUiActionKind::FlipVertical),"flip button publishes its distinct object action");
				expect(!scene.impl_->buttons[id]->icon.svg.GetVal().empty(),"flip button uses embedded dedicated icon");
			}
			for (unsigned id : {Rotate45,Rotate90}) {
				expect(scene.impl_->memberWanted[id] == (preset!=PresetKind::Circle && preset<PresetKind::Cuboid),"rotation is visible only for non-circle planar presets");
				actions.clear(); scene.impl_->Activate(id,scene.impl_->presented,{});
				expect(actions.size()==1 && actions.back().kind==(id==Rotate45 ? ShapeUiActionKind::Rotate45Clockwise : ShapeUiActionKind::Rotate90Clockwise),"rotation button publishes its distinct property action");
				expect(!scene.impl_->buttons[id]->icon.svg.GetVal().empty(),"rotation button uses its embedded dedicated icon");
				if (scene.impl_->memberWanted[id]) expect(scene.impl_->y[id].tar==scene.impl_->y[Finish].tar && scene.impl_->x[id].tar>scene.impl_->x[FlipVertical].tar && scene.impl_->x[id].tar<scene.impl_->x[Finish].tar,"rotation follows flips on the bottom row before Finish");
			}
			expect(ToolbarItems.back()==Finish && (preset==PresetKind::Circle || preset>=PresetKind::Cuboid || scene.impl_->x[Rotate45].tar<scene.impl_->x[Rotate90].tar),"45 then 90 then Finish preserves explicit toolbar order");
			expect(scene.impl_->twoRows && scene.impl_->y[Finish].tar>scene.impl_->y[Color].tar && (preset==PresetKind::Circle || scene.impl_->x[Finish].tar>scene.impl_->x[FlipVertical].tar),"two-row toolbar places finish after both flips on the bottom row");
			scene.ReleaseDeviceResources();
			expect(SUCCEEDED(scene.EnsureDeviceResources(RenderPipeline::GetDeviceEpoch())), "editor device resources rebuild");
			expect(SUCCEEDED(present()), "rebuilt resources still present actual SVG controls");
			scene.Configure({}, work, {});
			for (int frame = 0; frame<70; ++frame)
			{
				now += std::chrono::milliseconds(17);
				scene.Advance(now);
				if (frame == 10) expect(scene.impl_->handles.empty(), "all handle exit animations finish after their 0.16 second duration");
				if (frame == 16) expect(!scene.Visible() && !scene.AnimationActive(), "editor retires after the 0.24 second surface exit");
			}
			expect(!scene.Visible() && !scene.AnimationActive(), "closed editor retires and stops animation wakeups");
			scene.CommitPresented(false);
			for (int f = 0; f < 3; ++f)
			{
				scene.Configure({}, work, {});
				now += std::chrono::milliseconds(17);
				expect(!scene.Advance(now) && !scene.AnimationActive() && !scene.Visible() && !scene.impl_->snapshot,
					"retired editor remains asleep without recreating snapshot handles");
			}
		}

		// 每个候选都有单独的阻塞布局，确保优先级和最终fit都经过生产放置入口。
		{
			BarAttributePanelMotion motion; motion.Retarget(true, .4); motion.Advance({1, 1, true, false});
			struct PickerPlacementCase
			{
				EraserAttributeRect work, barRect, paletteRect;
				double triggerX, triggerY, expectedX, expectedY;
				bool paletteBelow, barBelow, fitted;
			};
			const std::array<PickerPlacementCase, 6> placements{{
				{{0, 0, 1000, 1000}, {100, 200, 500, 242.5}, {100, 250.5, 315, 325.5}, 295, 305.5, 145, 333.5, true, true, false},
				{{0, 0, 1000, 600}, {100, 289.5, 500, 332}, {100, 340, 315, 415}, 295, 395, 323, 340, true, true, false},
				{{0, 0, 560, 600}, {100, 289.5, 500, 332}, {320, 340, 535, 415}, 515, 395, 12, 340, true, true, false},
				{{0, 0, 500, 600}, {20, 289.5, 480, 332}, {100, 340, 315, 415}, 295, 395, 145, 72.5, true, true, false},
				{{0, 0, 250, 210}, {5, 50, 245, 92.5}, {5, 100.5, 220, 175.5}, 200, 155.5, 0, 0, true, true, true},
				{{0, 0, 1000, 1000}, {100, 757.5, 500, 800}, {100, 674.5, 315, 749.5}, 295, 694.5, 145, 457.5, false, false, false}}};
			for (const auto& c : placements)
			{
				bool placedBelow = c.paletteBelow;
				const auto pose = ResolveEditorPickerPose(motion, c.work, c.barRect, c.paletteRect,
					c.triggerX, c.triggerY, c.paletteBelow, c.barBelow, placedBelow);
				const auto rect = pose.Apply({0, 0, 300, 209});
				const auto disjoint = [&](EraserAttributeRect r) { return rect.right <= r.left+.000001 || rect.left >= r.right-.000001
					|| rect.bottom <= r.top+.000001 || rect.top >= r.bottom-.000001; };
				expect(rect.left >= c.work.left-.000001 && rect.top >= c.work.top-.000001 && rect.right <= c.work.right+.000001
					&& rect.bottom <= c.work.bottom+.000001 && disjoint(c.barRect) && disjoint(c.paletteRect), "ordered picker placement fits without covering either lower layer");
				expect(c.fitted ? pose.scale>0 && pose.scale<1 : std::abs(pose.scale-1)<.000001
					&& std::abs(pose.x-c.expectedX)<.000001 && std::abs(pose.y-c.expectedY)<.000001,
					"picker chooses far side, right, left, object side, then uniform fit in that order");
			}
		}
		// 四角与固定像素小工作区：命中、适配和批次都使用生产surface，不复制布局算法。
		for (float scale : {1.f, 1.5f, 2.f}) for (unsigned workSize = 0; workSize<3; ++workSize) for (unsigned corner = 0; corner<4; ++corner)
		{
			++scenes;
			const bool smallWork = workSize != 0;
			const RECT work{0, 0, workSize == 2 ? 160L : smallWork ? 560L : static_cast<LONG>(900*scale),
				workSize == 2 ? 300L : smallWork ? 420L : static_cast<LONG>(700*scale)};
			ToolSpec tool; tool.preset = PresetKind::RegularPolygon; tool.creationMode = CreationMode::CenterRadius; tool.sides = 5;
			ObjectId id{}; id[0] = 7; ShapeStyle objectStyle; objectStyle.width = 4;
			auto shape = MakePresetShape(tool, {0, 0}, {35, 25}, objectStyle, id);
			expect(shape.has_value(), "corner fixture creates regular polygon"); if (!shape) continue;
			shape->transform.dx = static_cast<float>(corner&1 ? work.right-35 : 35);
			shape->transform.dy = static_cast<float>(corner&2 ? work.bottom-25 : 25);
			auto snapshot = std::make_shared<ShapeUiSnapshot>(); snapshot->object = *shape;
			snapshot->identity.session = 17; snapshot->identity.objectId = id; snapshot->displayScale = scale;
			snapshot->handles = BuildEditHandles(*shape, {scale}, &snapshot->frameRect);
			GeometryResult geometry; BuildGeometry(*shape, geometry, {scale}); snapshot->bounds = geometry.bounds;
			ShapeEditorSurface scene; std::vector<ShapeUiAction> actions;
			scene.SetActionSink([&](const auto& action) { actions.push_back(action); });
			const auto click = [&](unsigned id)
			{
				const auto frame = scene.impl_->presented; const auto r = frame.buttons[id].boundsDip;
				const POINT point{frame.bounds.left+static_cast<LONG>((r.left+r.right)*scale/2), frame.bounds.top+static_cast<LONG>((r.top+r.bottom)*scale/2)};
				expect(scene.Hit(point).button == id, "visible member is located by transformed production hit snapshot");
				scene.Pointer(WM_LBUTTONDOWN, point); scene.Pointer(WM_LBUTTONUP, point);
			};
			const auto contained = [&](D2D1_RECT_F r)
			{
				const auto b = scene.Bounds();
				return b.left+r.left*scale >= work.left+5*scale-.01 && b.top+r.top*scale >= work.top+5*scale-.01
					&& b.left+r.right*scale <= work.right-5*scale+.01 && b.top+r.bottom*scale <= work.bottom-5*scale+.01;
			};
			const auto settle = [&]()
			{
				for (int f = 0; f<35; ++f)
				{
					now += std::chrono::milliseconds(17); scene.Advance(now); const auto& frame = scene.impl_->candidate;
					if (frame.visible)
					{
						expect(contained(frame.toolbar), "bar remains in work area during every panel motion frame");
						if (frame.sidesLayer) expect(contained(frame.parameters), "sides motion remains in work area");
						if (frame.paletteLayer) expect(contained(frame.colors), "palette motion remains in work area");
						if (frame.pickerLayer)
						{
							expect(contained(frame.picker), "picker Back overshoot remains in work area");
							const auto disjoint = [&](D2D1_RECT_F r) { return frame.picker.right <= r.left+.001f || frame.picker.left >= r.right-.001f
								|| frame.picker.bottom <= r.top+.001f || frame.picker.top >= r.bottom-.001f; };
							expect(frame.picker.right>frame.picker.left && frame.picker.bottom>frame.picker.top
								&& disjoint(frame.toolbar) && disjoint(frame.colors), "picker never covers bar or palette during any motion frame");
						}
					}
				}
					scene.CommitPresented();
				std::vector<bool> separators;
				for (const auto& item : scene.impl_->attributes.Snapshot())
				{
					if (item->preset == BarButtonPresetEnum::Divider)
					{
						const auto found = std::find(scene.impl_->dividers.begin(), scene.impl_->dividers.end(), item);
						if (found != scene.impl_->dividers.end() && scene.impl_->dividerWanted[static_cast<size_t>(found-scene.impl_->dividers.begin())]) separators.push_back(true);
					}
					else for (unsigned id : ToolbarItems) if (id && scene.impl_->buttons[id] == item && scene.impl_->memberWanted[id]) separators.push_back(false);
				}
				if (!separators.empty())
				{
					expect(!separators.front() && !separators.back(), "visible groups have no leading or trailing divider");
					for (size_t i = 1; i<separators.size(); ++i) expect(!(separators[i-1] && separators[i]), "hidden groups never leave adjacent dividers");
				}
			};
			scene.Configure(snapshot, work, {}); settle();
			expect(contained(scene.impl_->candidate.toolbar), "bar fits each corner including work areas requiring less than .75 scale");
			expect(scene.impl_->toolbarFit>0 && scene.impl_->toolbarFit <= 1 && (!smallWork || scale<2 || scene.impl_->twoRows), "overflow uses uniform fit then thickness row");
			if (workSize == 2) expect(scene.impl_->twoRows && scene.impl_->toolbarFit<.75, "two rows continue uniform scaling below .75 until they fit");
			click(SidesEntry); settle();
			expect(scene.impl_->candidate.sidesLayer && contained(scene.impl_->candidate.parameters), "sides layer fits work area");
						if (workSize == 1 && corner == 0)
			{
				SIZE receipt{};
				expect(SUCCEEDED(scene.EnsureDeviceResources(RenderPipeline::GetDeviceEpoch())), "small-work sides target initializes");
				expect(SUCCEEDED(scene.Present([&](const UPDATELAYEREDWINDOWINFO& info)->BOOL { receipt = *info.psize; return TRUE; })), "small-work sides emits real ULW source");
				const auto pixels = ReadEditorPixels(scene.Renderer(), receipt); unsigned visibleNumbers = 0;
				for (unsigned id = Sides; id<Swatch; ++id)
				{
					const auto& b = *scene.impl_->buttons[id]; const auto pose = scene.impl_->candidate.poses[1];
					const auto rect = pose.Apply({b.name.inhX+3, b.name.inhY+3, b.name.inhX+b.name.w.val-3, b.name.inhY+b.name.h.val-3});
					const POINT blank{static_cast<LONG>((pose.x+(b.button.inhX+b.button.w.val/2)*pose.scale)*scale), static_cast<LONG>((pose.y+(b.button.inhY+2)*pose.scale)*scale)};
					const auto evidence = InspectEditorGlyph(pixels, receipt, {static_cast<FLOAT>(rect.left*scale), static_cast<FLOAT>(rect.top*scale), static_cast<FLOAT>(rect.right*scale), static_cast<FLOAT>(rect.bottom*scale)}, blank, b.name.color.val);
					if (evidence.pixels >= 2 && evidence.peak>.30) ++visibleNumbers;
				}
				expect(visibleNumbers == 12, "small-work sides layer keeps all twelve real number glyphs reachable");
				const auto path = std::filesystem::path(L"Build/shape-p34-ui")/(L"v2-sides-small-"+std::to_wstring(static_cast<int>(scale*100))+L".png");
				expect(!pixels.empty() && SaveEditorPng(path, pixels.data(), receipt.cx, receipt.cy, receipt.cx*4), "small-work sides writes actual PNG evidence");
				report<<"SCREENSHOT "<<path.string()<<'\n'; std::fprintf(stderr, "Shape editor screenshot: %s\n", path.string().c_str());
			}
			click(Sides+11);
			expect(actions.size() == 1 && actions.back().kind == ShapeUiActionKind::SetSides && actions.back().sides == 14 && scene.impl_->sidesOpen.load(), "5x3 sides layer applies fourteen and stays open");
			if (!actions.empty()) { const auto changed = BuildShapePropertyCandidate(*shape, actions.back(), 2); expect(changed && changed->construction->tool.sides == 14, "sides action uses existing SetRegularSides candidate path"); }
			scene.Keyboard(VK_ESCAPE, true); settle(); actions.clear();
			click(Color); settle();
			expect(contained(scene.impl_->candidate.toolbar) && contained(scene.impl_->candidate.colors), "bar and palette fit four corners and small work area");
			click(Custom); settle();
			expect(contained(scene.impl_->candidate.toolbar) && contained(scene.impl_->candidate.colors) && contained(scene.impl_->candidate.picker), "bar plus both layers remain inside work area");
			expect(scene.impl_->palettePanel.pct.val == BarDrawAttributeSurfaceOpacity && !scene.impl_->palettePanel.residentSurface
				&& scene.impl_->pickerPanel.pct.val == BarDrawAttributeSurfaceOpacity && !scene.impl_->pickerPanel.residentSurface, "layers retain fixed .95 surface opacity independently of resident settings");
			// 工作区空白也属于弹层输入屏障；一次按下/抬起不能继续触发下层动作。
			const POINT outside{work.left+1, work.top+1};
			expect(scene.Hit(outside).button == OutsideLayerHit, "topmost layer captures an outside work-area click");
			scene.Pointer(WM_LBUTTONDOWN, outside); scene.Pointer(WM_LBUTTONUP, outside);
			expect(!scene.impl_->pickerOpen.load() && scene.impl_->colorOpen.load() && actions.empty(), "outside click closes only picker and emits no lower-layer action"); settle();
			scene.Pointer(WM_LBUTTONDOWN, outside); scene.Pointer(WM_LBUTTONUP, outside);
			expect(!scene.impl_->colorOpen.load() && actions.empty(), "second outside click closes only palette");
			expect(!scene.HasKeyboardFocus() && !scene.Keyboard(VK_RIGHT, true) && !scene.Keyboard(VK_SPACE, true),
				"closed layers release navigation keys immediately"); settle();
			scene.impl_->focused = Custom;
			expect(!scene.HasKeyboardFocus() && !scene.Keyboard(VK_TAB, true) && !scene.Keyboard(VK_RETURN, true),
				"stale hidden layer focus cannot claim navigation or activate a button");
			// 同一surface切换对象，旧会话所有位置与透明度必须丢弃。
			auto switched = std::make_shared<ShapeUiSnapshot>(*snapshot); ++switched->identity.session; switched->identity.objectId[1] = 1;
			const auto other = MakePresetShape({PresetKind::Rectangle, CreationMode::Drag}, {0, 0}, {60, 40}, objectStyle, switched->identity.objectId);
			expect(other.has_value(), "second object has different members and position"); if (!other) continue;
			switched->object = *other; switched->object->transform.dx = static_cast<float>(corner&1 ? 40 : work.right-80);
			switched->object->transform.dy = static_cast<float>(corner&2 ? 40 : work.bottom-80);
			switched->handles = BuildEditHandles(*switched->object, {scale}, &switched->frameRect);
			BuildGeometry(*switched->object, geometry, {scale}); switched->bounds = geometry.bounds;
			scene.Configure(switched, work, {}); now += std::chrono::milliseconds(17); scene.Advance(now);
			for (unsigned id = 1; id<Count; ++id) if (id != Rotate) expect(scene.impl_->x[id].IsSame() && scene.impl_->y[id].IsSame(), "switch-object first frame equals all member targets");
			for (size_t di = 0; di<scene.impl_->dividers.size(); ++di) expect(scene.impl_->dividerX[di].IsSame() && scene.impl_->dividerY[di].IsSame(), "switch-object divider never flies from previous session");
			expect(scene.impl_->toolbarMotion.Active() && scene.impl_->toolbarMotion.Geometry()<1, "switch-object restarts full compact panel motion"); settle();
			auto mixed = std::make_shared<ShapeUiSnapshot>(*switched); mixed->object->style.stroke = mixed->object->style.fill = true;
			scene.Configure(mixed, work, {}); now += std::chrono::milliseconds(17); scene.Advance(now); scene.CommitPresented();
			expect(!scene.impl_->memberWanted[Style+2] && scene.impl_->buttons[Style]->state->state == BarWidgetState::Selected
				&& scene.impl_->buttons[Style+1]->state->state == BarWidgetState::Selected,"legacy mixed object selects outline and fill without a third entry");
			actions.clear(); scene.impl_->Activate(Style+1,scene.impl_->presented,{});
			expect(actions.size() == 1 && !actions.back().style.stroke && actions.back().style.fill && mixed->object->style.stroke && mixed->object->style.fill,
				"only clicking a retained entry converts legacy mixed object to single fill mode");
			// 切换填充后，隐藏的线型与粗细锚到前一可见样式；同一批次在前半程可加入。
			auto fill = std::make_shared<ShapeUiSnapshot>(*switched); fill->object->style.stroke = false; fill->object->style.fill = true;
			const double beforeX = scene.impl_->x[Pattern].val;
			scene.Configure(fill, work, {}); now += std::chrono::milliseconds(17); scene.Advance(now);
			expect(std::abs(scene.impl_->x[Pattern].dur-.4)<.001 && std::abs(scene.impl_->toolbarWidthValue.dur-.4)<.001
				&& scene.impl_->x[Pattern].tar == scene.impl_->x[Style+1].tar && scene.impl_->y[Pattern].tar == scene.impl_->y[Style+1].tar,
				"hidden group and toolbar width use .4 second anchored batch");
			expect(!scene.impl_->memberWanted[Pattern] && !scene.impl_->memberWanted[Thickness] && !scene.impl_->memberWanted[Aux+2],
				"pure fill hides stroke and auxiliary controls while retaining their object style bits");
			expect(fill->object->style.auxiliary == switched->object->style.auxiliary,"fill fixture preserves auxiliary state until an explicit toggle");
			expect(std::abs(scene.impl_->x[Pattern].val-beforeX)<std::abs(scene.impl_->x[Pattern].tar-beforeX), "group hide starts continuously without jumping to anchor");
			now += std::chrono::milliseconds(50); scene.Advance(now);
			const double remaining = scene.impl_->memberBatch.GetRemainingDuration();
			scene.Configure(switched, work, {}); now += std::chrono::milliseconds(17); scene.Advance(now);
			expect(std::abs(scene.impl_->toolbarWidthValue.dur-remaining)<.001, "early reversal joins existing batch remaining duration"); settle();
			scene.Configure({}, work, {}); now += std::chrono::milliseconds(17); scene.Advance(now);
			expect(scene.impl_->toolbarMotion.Active() && scene.impl_->toolbarMotion.TargetGeometry() == 0, "close uses panel motion to compact anchor"); settle();
			expect(!scene.impl_->snapshot && !scene.impl_->initialized && !scene.Visible(), "close retires snapshot and layout after .24 second motion");
		}
		// 高频拖动：真实Bridge对同一手势预览latest-wins，终结意图在队列拥塞时仍按序送达。
		{
			++scenes;
			auto state = std::make_shared<ShapeUiSnapshot>();
			state->identity.session = 88;
			state->identity.objectId[0] = 8;
			state->object = ShapeObject{};
			state->object->id = state->identity.objectId;
			Bridge::StateBridge bridge(6);
			bool full = false;
			ShapeIntentSender sender{[&](const ShapeUiAction& a)
			{
				return full ? Bridge::CommandResult::QueueFull : bridge.PublishShapeAction(a);
			}, [&]
			{
				return state;
			}};
			ShapeUiAction a;
			a.identity = state->identity;
			a.kind = ShapeUiActionKind::BeginEdit;
			a.handleId = 3;
			a.down = {10, 10};
			expect(sender.Send(a), "coalescing fixture accepts BeginEdit");
			a.kind = ShapeUiActionKind::PreviewEdit;
			bool previewsAccepted = true;
			for (int i = 0; i<1000; ++i)
			{
				a.point = {static_cast<float>(i), static_cast<float>(2*i)};
				previewsAccepted&=sender.Send(a);
			}
			expect(previewsAccepted, "1000 high-frequency moves never hit queue capacity");
			full = true;
			a.kind = ShapeUiActionKind::CommitEdit;
			a.point = {2000, 3000};
			expect(sender.Send(a), "congested commit is retained as a terminal intent");
			a.kind = ShapeUiActionKind::PreviewEdit;
			expect(!sender.Send(a), "a preview behind a pending terminal is dropped instead of overtaking it");
			full = false;
			const auto now = std::chrono::steady_clock::now();
			expect(sender.Tick(now) == std::chrono::steady_clock::time_point::max(), "retry delivers the pending commit");
			Bridge::Command command;
			std::vector<ShapeUiAction> drained;
			while (bridge.TryConsume(command)) if (command.shapeAction) drained.push_back(*command.shapeAction);
			expect(drained.size() == 3 && drained[0].kind == ShapeUiActionKind::BeginEdit && drained[1].kind == ShapeUiActionKind::PreviewEdit
				&& drained[2].kind == ShapeUiActionKind::CommitEdit, "Begin, one latest preview and Commit remain in FIFO order");
			if (drained.size() == 3) expect(drained[1].point.x == 999 && drained[1].point.y == 1998 && drained[2].point.x == 2000,
				"surviving preview is the newest move and Commit carries the final point");
		}
		// 本地跟手：Draw3快照不更新时控制点仍按共享核心候选移动；拖动期间窗口不逐帧重建；抬手后等权威快照交还。
		{
			++scenes;
			ToolSpec tool;
			tool.preset = PresetKind::Rectangle;
			tool.creationMode = DefaultCreationMode(PresetKind::Rectangle);
			ShapeStyle shapeStyle;
			shapeStyle.width = 4;
			ObjectId id{};
			id[0] = 9;
			auto shape = MakePresetShape(tool, {200, 200}, {420, 330}, shapeStyle, id);
			expect(shape.has_value(), "tracking fixture creates a rectangle");
			if (shape)
			{
				constexpr float angle = .37f;
				shape->transform.m11 = std::cos(angle); shape->transform.m12 = std::sin(angle);
				shape->transform.m21 = -std::sin(angle); shape->transform.m22 = std::cos(angle);
				shape->transform.dx = 400; // 让整条浮条无需边缘钳制，专测逐Move的锚点偏移。
				auto snapshot = std::make_shared<ShapeUiSnapshot>();
				snapshot->object = *shape;
				snapshot->identity.session = 5;
				snapshot->identity.generation = 1;
				snapshot->identity.objectId = id;
				snapshot->identity.documentGeneration = 1;
				snapshot->identity.displayGeneration = 1;
				snapshot->displayScale = 1;
				snapshot->handles = BuildEditHandles(*shape, {1.f, .25f}, &snapshot->frameRect);
				GeometryResult geometry;
				BuildGeometry(*shape, geometry, {1.f, .25f});
				snapshot->bounds = geometry.bounds;
				ShapeEditorSurface scene;
				std::vector<ShapeUiAction> actions;
				scene.SetActionSink([&](const auto& action)
				{
					actions.push_back(action);
				});
				const RECT work{0, 0, 1600, 1000};
				auto frame = [&]
				{
					scene.Configure(snapshot, work, {});
					now += std::chrono::milliseconds(17);
					scene.Advance(now);
					const bool needed = scene.NeedsPresent();
					if (needed && SUCCEEDED(scene.EnsureDeviceResources(RenderPipeline::GetDeviceEpoch())) && SUCCEEDED(scene.Present([](const auto&)->BOOL
					{
						return TRUE;
					}))) scene.CommitPresented();
					return needed;
				};
				for (int f = 0; f<70; ++f) frame();
				expect(!frame(), "settled editor with unchanged snapshot skips readback and ULW");
				const EditHandle* scaleHandle = nullptr;
				for (const auto& h : snapshot->handles) if (h.kind == EditHandleKind::Vertex && h.id == 1001)
				{
					scaleHandle = &h;
					break;
				}
				expect(scaleHandle != nullptr, "rectangle exposes its upper-right corner away from the bottom toolbar");
				if (scaleHandle)
				{
					const POINT down{static_cast<LONG>(scaleHandle->position.x), static_cast<LONG>(scaleHandle->position.y)};
					// 事件携带的是更早一帧的呈现代次；按下仍以最近成功呈现帧命中，不被吞掉。
					const auto presented = scene.PresentedGeneration();
					actions.clear();
					scene.Pointer(WM_LBUTTONDOWN, down, true, presented>1 ? presented-1 : presented);
					expect(!actions.empty() && actions.front().kind == ShapeUiActionKind::BeginEdit,
						"Down against an earlier successfully presented frame still begins the gesture");
					const auto sameShape = [](ShapeObject a, ShapeObject b)
					{
						a.revision = b.revision = 0;
						return a == b;
					};
					const std::uint32_t begun = actions.empty() ? scaleHandle->id : actions.front().handleId;
					EditSession reference;
					expect(reference.Begin(*shape, begun, {static_cast<float>(down.x), static_cast<float>(down.y)}, {1.f, .25f}), "reference session begins");
					// 断言屏幕包围框，而不是旋转后的局部底边中点。
					const auto screenDockAnchor = [](const ShapeUiSnapshot& state, bool below)
					{
						const auto& f = state.frameRect; const auto& transform = state.object->transform;
						const std::array<Vec2,4> corners{ApplyTransform(transform,{f.left,f.top}),ApplyTransform(transform,{f.right,f.top}),
							ApplyTransform(transform,{f.right,f.bottom}),ApplyTransform(transform,{f.left,f.bottom})};
						float left=corners[0].x,right=left,top=corners[0].y,bottom=top;
						for (const auto& corner : corners) { left=(std::min)(left,corner.x); right=(std::max)(right,corner.x); top=(std::min)(top,corner.y); bottom=(std::max)(bottom,corner.y); }
						return Vec2{(left+right)*.5f,below ? bottom : top};
					};
					unsigned recreated = 0, tracked = 0;
					auto* target = scene.Renderer().GetTargetBitmap();
					RECT previousBounds = scene.Bounds();
					bool monotonic = true;
					for (int step = 1; step <= 40; ++step)
					{
						const POINT move{down.x+step*5, down.y+step*3};
						scene.Pointer(WM_MOUSEMOVE, move, true, scene.PresentedGeneration());
						frame();
						if (scene.Renderer().GetTargetBitmap() != target)
						{
							++recreated;
							target = scene.Renderer().GetTargetBitmap();
						}
						const RECT b = scene.Bounds();
						monotonic&=b.left <= previousBounds.left && b.top <= previousBounds.top && b.right >= previousBounds.right && b.bottom >= previousBounds.bottom;
						previousBounds = b;
						if (reference.Preview({static_cast<float>(move.x), static_cast<float>(move.y)}, 100+step) && reference.Candidate())
						{
							const auto& shown = scene.impl_->presented.snapshot;
							const auto expectedHandles = BuildEditHandles(*reference.Candidate(), {1.f, .25f});
							if (shown && shown->object && sameShape(*shown->object, *reference.Candidate()) && shown->handles == expectedHandles)++tracked;
						}
						const auto& current = scene.impl_->snapshot;
						const auto& frameRect = current->frameRect;
						const auto anchor = screenDockAnchor(*current,true);
						const double expectedX = (std::clamp)(current->canvasOriginX+anchor.x-scene.impl_->toolbarWidthValue.val*scene.impl_->toolbarFit/2,5.,1595.-scene.impl_->toolbarWidthValue.val*scene.impl_->toolbarFit);
						const double expectedY = current->canvasOriginY+anchor.y+16;
						if (!(scene.impl_->toolbarBelow && std::abs(scene.impl_->toolbarX.tar-expectedX)<.001 && std::abs(scene.impl_->toolbarY.tar-expectedY)<.001)) report<<"TRACK_DIAGNOSTIC step="<<step<<" expected="<<expectedX<<","<<expectedY<<" actual="<<scene.impl_->toolbarX.tar<<","<<scene.impl_->toolbarY.tar<<" below="<<scene.impl_->toolbarBelow<<" width="<<scene.impl_->toolbarWidthValue.val<<" fit="<<scene.impl_->toolbarFit<<" frame="<<frameRect.left<<","<<frameRect.top<<","<<frameRect.right<<","<<frameRect.bottom<<"\n";
						expect(scene.impl_->toolbarBelow && std::abs(scene.impl_->toolbarX.tar-expectedX)<.001
							&& std::abs(scene.impl_->toolbarY.tar-expectedY)<.001 && scene.impl_->toolbarX.IsSame() && scene.impl_->toolbarY.IsSame(),
							"every Move retargets the horizontal dock while its position animation remains active");
					}
					expect(tracked == 40, "every move shows the shared-core candidate handles before any Draw3 snapshot arrives");
					expect(monotonic && recreated <= 4, "drag window grows in blocks and avoids per-frame target recreation");
					expect(actions.size() == 41, "each move still emits one preview intent for Bridge coalescing");
					const POINT up{down.x+200, down.y+120};
					scene.Pointer(WM_LBUTTONUP, up, true, scene.PresentedGeneration());
					frame();
					expect(actions.back().kind == ShapeUiActionKind::CommitEdit, "Up emits the commit with the final point");
					reference.Preview({static_cast<float>(up.x), static_cast<float>(up.y)}, 200);
					const auto held = scene.impl_->presented.snapshot;
					expect(held && held->object && reference.Candidate() && sameShape(*held->object, *reference.Candidate()),
						"after Up the local candidate stays until Draw3 confirms, so handles do not snap back");
					if (reference.Candidate())
					{
						snapshot = std::make_shared<ShapeUiSnapshot>(*snapshot);
						snapshot->object = *reference.Candidate();
						snapshot->identity.generation = 7;
						snapshot->handles = BuildEditHandles(*snapshot->object, {1.f, .25f}, &snapshot->frameRect);
						GeometryResult g;
						BuildGeometry(*snapshot->object, g, {1.f, .25f});
						snapshot->bounds = g.bounds;
					}
					frame();
					const auto& committed = scene.impl_->snapshot;
					const auto& committedFrame = committed->frameRect;
					const auto committedAnchor = screenDockAnchor(*committed,scene.impl_->toolbarBelow);
					const double committedX = (std::clamp)(committed->canvasOriginX+committedAnchor.x-scene.impl_->toolbarWidthValue.val*scene.impl_->toolbarFit/2,5.,1595.-scene.impl_->toolbarWidthValue.val*scene.impl_->toolbarFit);
					const double committedY = committed->canvasOriginY+committedAnchor.y+(scene.impl_->toolbarBelow ? 16 : -16-scene.impl_->toolbarHeightValue.val*scene.impl_->toolbarFit);
					report<<"COMMIT_TRACK expected="<<committedX<<","<<committedY<<" actual="<<scene.impl_->toolbarX.tar<<","<<scene.impl_->toolbarY.tar<<" below="<<scene.impl_->toolbarBelow<<"\n";
					expect(std::abs(scene.impl_->toolbarX.tar-committedX)<.001 && std::abs(scene.impl_->toolbarY.tar-committedY)<.001,"Up and authoritative commit preserve the animated target without restarting at the initial anchor");
					for (int f = 0; f<70; ++f) frame();
					expect(scene.impl_->presented.snapshot && scene.impl_->presented.snapshot->identity.generation == 7 && !scene.impl_->tracking,
						"authoritative post-commit snapshot replaces the local candidate");
					expect(!frame(), "after the gesture settles no further presentation is requested");
				}
			}
		}
		// 真实构造快照经Present回读alpha，覆盖ULW原生触摸与逻辑命中的一致性。
		for (auto preset : {PresetKind::Polyline,PresetKind::Polygon}) for (float displayScale : {1.f,1.5f})
		{
			++scenes; ObjectId id{}; id[0]=31;
			ConstructionSession construction;
			expect(construction.Begin({preset,CreationMode::Points},{},id),"coverage fixture begins real point construction");
			for (auto point : {Vec2{600,300},Vec2{1000,700},Vec2{1000,300}})
				expect(construction.ConfirmDrag(point,point),"coverage fixture confirms construction vertex");
			auto state=std::make_shared<ShapeUiSnapshot>(); state->object=construction.Candidate(); state->construction=construction.Status();
			state->identity.session=131; state->identity.objectId=id; state->displayScale=displayScale;
			state->canvasOriginX=-180; state->canvasOriginY=-80;
			for (size_t i=0;i<construction.Points().size();++i) state->handles.push_back({static_cast<uint32_t>(1000+i),EditHandleKind::Vertex,construction.Points()[i]});
			if (!state->object) { expect(false,"coverage fixture has live preview"); continue; }
			GeometryResult geometry; expect(BuildGeometry(*state->object,geometry,{displayScale}),"coverage preview builds"); state->bounds=geometry.bounds;
			ShapeEditorSurface scene; scene.Configure(state,{-200,-100,1400,1000},{});
			for (int i=0;i<40;++i) { now+=std::chrono::milliseconds(17); scene.Advance(now); }
			SIZE extent{};
			expect(SUCCEEDED(scene.EnsureDeviceResources(RenderPipeline::GetDeviceEpoch())),"construction coverage target initializes");
			expect(SUCCEEDED(scene.Present([&](const UPDATELAYEREDWINDOWINFO& info)->BOOL { extent=*info.psize; return TRUE; })),"construction coverage uses real Present");
			scene.CommitPresented();
			const auto pixels=ReadEditorPixels(scene.Renderer(),extent); const auto bounds=scene.Bounds();
			const auto alpha=[&](POINT canvas)->int {
				const int x=canvas.x+state->canvasOriginX-bounds.left,y=canvas.y+state->canvasOriginY-bounds.top;
				if (x<0 || y<0 || x>=extent.cx || y>=extent.cy || pixels.size()!=static_cast<size_t>(extent.cx)*extent.cy*4) return -1;
				return pixels[(static_cast<size_t>(y)*extent.cx+x)*4+3];
			};
			expect(alpha({800,450})==0,"construction bounds blank pixel stays alpha zero for native canvas touch");
			expect(alpha({600,300})>0,"construction vertex retains nonzero native input alpha");
			expect(!scene.Hit({800+state->canvasOriginX,450+state->canvasOriginY}).occupied && scene.Hit({600+state->canvasOriginX,300+state->canvasOriginY}).handle==1000,"real construction frame accepts vertices and leaves bounds blank unoccupied");
		}
		// 注入墙钟，确认三秒期限不会被帧dt或同一停靠分支的目标变化绕过。
		{
			++scenes; ObjectId id{}; id[0]=32;
			const auto object=MakePresetShape({PresetKind::Rectangle,CreationMode::Drag},{600,300},{1000,500},{},id);
			expect(object.has_value(),"toolbar hold fixture creates rectangle");
			if (object)
			{
				auto state=std::make_shared<ShapeUiSnapshot>(); state->object=*object; state->identity.session=132; state->identity.objectId=id;
				ShapeEditorSurface scene; scene.impl_->toolbarNow=[&] { return now; };
				scene.SetActionSink([](const ShapeUiAction&){});
				const auto geometry=[&] {
					state->handles=BuildEditHandles(*state->object,{1,.25f},&state->frameRect);
					GeometryResult g; expect(BuildGeometry(*state->object,g),"hold fixture geometry builds"); state->bounds=g.bounds;
				};
				geometry();
				const auto frame=[&] {
					scene.Configure(state,{0,0,1600,1000},{}); scene.Advance(now);
					expect(SUCCEEDED(scene.EnsureDeviceResources(RenderPipeline::GetDeviceEpoch())),"hold frame target initializes");
					expect(SUCCEEDED(scene.Present([](const UPDATELAYEREDWINDOWINFO&)->BOOL { return TRUE; })),"hold frame presents"); scene.CommitPresented();
				};
				const auto settle=[&] { for(int i=0;i<40;++i) { now+=std::chrono::milliseconds(17); frame(); } };
				const auto anchor=[&] { const auto& f=scene.impl_->presented; return std::array<double,2>{f.bounds.left+f.toolbar.left*f.scale,f.bounds.top+f.toolbar.top*f.scale}; };
				const auto button=[&](unsigned id) { const auto& f=scene.impl_->presented; const auto r=f.buttons[id].boundsDip; return POINT{f.bounds.left+static_cast<LONG>((r.left+r.right)*f.scale/2),f.bounds.top+static_cast<LONG>((r.top+r.bottom)*f.scale/2)}; };
				const auto click=[&](unsigned id) { const auto p=button(id); expect(scene.Hit(p).button==id,"hold operation hits actual presented button"); scene.Pointer(WM_LBUTTONDOWN,p); scene.Pointer(WM_LBUTTONUP,p); };
				const auto moveTarget=[&](float y) { state=std::make_shared<ShapeUiSnapshot>(*state); state->object->transform.dy=y; geometry(); };
				const auto same=[&](const auto& a,const auto& b) { return std::hypot(a[0]-b[0],a[1]-b[1])<.001; };
				settle(); const auto frozen=anchor(); click(Pattern+1); const auto deadline=scene.TickToolbarIdle(now);
				expect(deadline==now+std::chrono::seconds(3),"toolbar operation exposes one finite three-second deadline");
				moveTarget(120); now+=std::chrono::milliseconds(2999); frame();
				expect(same(anchor(),frozen) && scene.impl_->toolbarHoldFrozen,"2999ms keeps last presented anchor despite changed target");
				now+=std::chrono::milliseconds(1);
				expect(scene.TickToolbarIdle(now)==now && scene.TickToolbarIdle(now)==std::chrono::steady_clock::time_point::max(),"3000ms wake is emitted exactly once"); frame();
				expect(!scene.impl_->toolbarHoldFrozen && !scene.impl_->dockResidualY.IsSame() && scene.AnimationActive(),"expiry starts existing residual animation even on same placement branch");
				settle(); expect(!same(anchor(),frozen),"expired hold reaches latest target");
				const auto renewed=anchor(); click(Pattern+2); moveTarget(180); now+=std::chrono::seconds(2); frame(); click(Pattern+3);
				now+=std::chrono::milliseconds(2999); frame(); expect(same(anchor(),renewed),"second operation renews deadline without recapturing target");
				now+=std::chrono::milliseconds(1); frame(); expect(!scene.impl_->toolbarHoldFrozen,"renewed hold expires at its own 3000ms"); settle();
				const auto longPress=anchor(); const auto p=button(Pattern); scene.Pointer(WM_LBUTTONDOWN,p); moveTarget(220);
				now+=std::chrono::milliseconds(3500); frame(); expect(same(anchor(),longPress) && scene.TickToolbarIdle(now)==std::chrono::steady_clock::time_point::max(),"active button gesture stays frozen beyond three seconds without idle wake");
				scene.Pointer(WM_LBUTTONUP,p); expect(scene.TickToolbarIdle(now)==now+std::chrono::seconds(3),"button release starts full three-second idle deadline");
				const POINT body{800,620}; expect(scene.Hit(body).body,"hold fixture shape body remains directly draggable"); scene.Pointer(WM_LBUTTONDOWN,body);
				expect(!scene.impl_->toolbarHoldFrozen && scene.impl_->toolbarHoldResume,"shape manipulation immediately clears hold with animated resume");
				now+=std::chrono::milliseconds(17); frame(); expect(!scene.impl_->dockResidualY.IsSame(),"shape drag resumes residual motion from frozen position"); scene.CancelPointer(); settle();
				click(Pattern+1); click(Finish); expect(!scene.impl_->toolbarHoldFrozen && !scene.impl_->toolbarHoldResume,"Finish clears the hold immediately");
				click(Pattern+1); state=std::make_shared<ShapeUiSnapshot>(*state); ++state->identity.session; frame(); expect(!scene.impl_->toolbarHoldFrozen,"new editor session clears old hold");
				// 新场景已配置但尚未呈现时，旧帧上的迟到点击不能重新冻结。
				settle(); state=std::make_shared<ShapeUiSnapshot>(*state); ++state->identity.session; scene.Configure(state,{0,0,1600,1000},{});
				click(Pattern+1); expect(!scene.impl_->toolbarHoldFrozen && !scene.impl_->toolbarOperationActive,"stale presented frame cannot re-arm hold for the new scene"); frame();
				settle(); click(Color); settle(); click(Custom); settle();
				const auto& picker=scene.impl_->presented; const auto palette=picker.palette;
				const POINT pick{picker.bounds.left+static_cast<LONG>((palette.left+palette.right)*picker.scale/2),picker.bounds.top+static_cast<LONG>((palette.top+palette.bottom)*picker.scale/2)};
				expect(scene.Hit(pick).button==PaletteHit,"hold fixture reaches actual picker palette");
				scene.Pointer(WM_LBUTTONDOWN,pick); now+=std::chrono::milliseconds(3500); frame();
				expect(scene.impl_->toolbarHoldFrozen && scene.TickToolbarIdle(now)==std::chrono::steady_clock::time_point::max(),"active picker gesture suppresses idle expiry");
				scene.Pointer(WM_MOUSEMOVE,{pick.x+3,pick.y+3}); scene.Pointer(WM_LBUTTONUP,{pick.x+3,pick.y+3});
				expect(scene.TickToolbarIdle(now)==now+std::chrono::seconds(3),"picker Move and Up renew the full hold deadline");
				scene.Pointer(WM_LBUTTONDOWN,pick); scene.CancelPointer();
				expect(scene.TickToolbarIdle(now)==now+std::chrono::seconds(3),"cancelled picker gesture releases the hold into idle countdown");
			}
		}
		// 换边粘性与主栏遮挡用真实Advance放置，双侧不足时重复帧必须保持稳定。
		{
			++scenes; ObjectId id{}; id[0]=11;
			const auto object=MakePresetShape({PresetKind::Rectangle,CreationMode::Drag},{300,200},{520,330},{},id);
			expect(object.has_value(),"placement fixture creates rectangle");
			if(object)
			{
				auto snapshot=std::make_shared<ShapeUiSnapshot>(); snapshot->object=*object;
				snapshot->identity.session=45; snapshot->identity.objectId=id; snapshot->displayScale=1;
				ShapeEditorSurface scene; RECT work{0,0,1600,1000},bar{};
				const auto place=[&](float dy)
				{
					snapshot=std::make_shared<ShapeUiSnapshot>(*snapshot); snapshot->object->transform.dy=dy;
					snapshot->handles=BuildEditHandles(*snapshot->object,{1,.25f},&snapshot->frameRect);
					GeometryResult geometry; BuildGeometry(*snapshot->object,geometry); snapshot->bounds=geometry.bounds;
					scene.Configure(snapshot,work,bar);
					for(int f=0;f<40;++f){now+=std::chrono::milliseconds(17);scene.Advance(now);scene.CommitPresented();}
				};
				place(0); expect(scene.impl_->toolbarBelow,"new editor defaults below when both sides fit");
				place(600); expect(!scene.impl_->toolbarBelow,"bottom overflow selects the usable top side");
				place(0); expect(!scene.impl_->toolbarBelow,"restored bottom space does not undo the top-side choice");
				place(-160); expect(scene.impl_->toolbarBelow,"top overflow can select the bottom side again");
				place(0); const auto r=scene.impl_->candidate.toolbar; const auto b=scene.Bounds();
				bar={b.left+static_cast<LONG>(std::floor(r.left)),b.top+static_cast<LONG>(std::floor(r.top)),
					b.left+static_cast<LONG>(std::ceil(r.right)),b.top+static_cast<LONG>(std::ceil(r.bottom))};
				place(0); expect(!scene.impl_->toolbarBelow,"main bar obstruction also makes the current side unavailable");
				bar={}; work={0,0,1600,40}; place(0);
				const double x=scene.impl_->toolbarX.val,y=scene.impl_->toolbarY.val; const bool below=scene.impl_->toolbarBelow;
				place(0); expect(scene.impl_->toolbarBelow==below&&scene.impl_->toolbarX.val==x&&scene.impl_->toolbarY.val==y,
					"both unavailable sides keep the current choice and stable fitted position");
			}
		}
		// UI先累计完整轨迹，再发送latest-wins意图；绘制线程只收到终点也不能丢圈数。
		{
			++scenes;
			constexpr float pi = 3.14159265358979323846f;
			Construction c;
			c.tool = {PresetKind::Arc, CreationMode::MultiStage};
			c.start = {400, 300};
			c.end = {400, 420};
			c.parameters = ArcConstruction{pi/36}; // 起角90°、终角95°；到300°的积压批次跨过180°，缺陷版本必然丢方向。
			ObjectId id{};
			id[0] = 10;
			const auto shape = MakeConstructedShape(c, {}, id);
			expect(shape.has_value(), "angle tracking fixture creates an arc");
			if (shape)
			{
				auto state = std::make_shared<ShapeUiSnapshot>();
				state->object = *shape;
				state->identity.session = 6;
				state->identity.objectId = id;
				state->identity.documentGeneration = 1;
				state->identity.displayGeneration = 1;
				state->displayScale = 1;
				state->handles = BuildEditHandles(*shape, {}, &state->frameRect);
				GeometryResult geometry;
				BuildGeometry(*shape, geometry);
				state->bounds = geometry.bounds;
				ShapeEditorSurface scene;
				std::vector<ShapeUiAction> actions;
				scene.SetActionSink([&](const auto& a)
				{
					actions.push_back(a);
				});
				const RECT work{0, 0, 1600, 1000};
				auto frame = [&]
				{
					scene.Configure(state, work, {});
					now += std::chrono::milliseconds(17);
					scene.Advance(now);
					if (scene.NeedsPresent() && SUCCEEDED(scene.EnsureDeviceResources(RenderPipeline::GetDeviceEpoch())) && SUCCEEDED(scene.Present([](const auto&)->BOOL
					{
						return TRUE;
					}))) scene.CommitPresented();
				};
				for (int i = 0; i<70; ++i) frame();
				const auto end = std::find_if(state->handles.begin(), state->handles.end(), [](const EditHandle& h)
				{
					return h.id == 103;
				});
				expect(end != state->handles.end(), "angle tracking finds the typed end-angle handle");
				if (end != state->handles.end())
				{
					const POINT down{static_cast<LONG>(std::lround(end->position.x)), static_cast<LONG>(std::lround(end->position.y))};
					const auto hit = scene.Hit(down);
					expect(hit.occupied && hit.handle == 103, "arc endpoint remains reachable with dedicated handle priority");
					// 暂停真实worker，整批原始Move入生产邮箱后再一次性消费，禁止用Pointer直调用掩盖合并丢轨迹。
					struct Batch
					{
						ShapeEditorSurface* scene = nullptr;
						std::mutex mutex;
						std::condition_variable changed;
						bool waiting = false, released = false, done = false;
						unsigned moves = 0, hoverMoves = 0;
						POINT hoverPoint{};
						static void Input(const SurfaceInputEvent& e, void* context)
						{
							auto& self = *static_cast<Batch*>(context);
							if (e.message == WM_NULL)
							{
								std::unique_lock lock(self.mutex);
								self.waiting = true; self.changed.notify_all();
								(void)self.changed.wait_for(lock, std::chrono::seconds(5), [&] { return self.released; });
								return;
							}
							self.scene->Pointer(e.message, e.screenPoint, e.captureOwned, e.presentedRevision);
							if (e.message == WM_MOUSEMOVE)
							{
								if (e.preserveMoveSamples) ++self.moves;
								else { ++self.hoverMoves; self.hoverPoint = e.screenPoint; }
							}
							if (e.message == WM_LBUTTONUP)
							{
								std::scoped_lock lock(self.mutex); self.done = true; self.changed.notify_all();
							}
						}
						static void Cancel(const SurfaceInputEvent&, void* context) { static_cast<Batch*>(context)->scene->CancelPointer(); }
					} batch;
					batch.scene = &scene;
					auto worker = CreateOffscreenInteractionWorker();
					expect(static_cast<bool>(worker), "arc mailbox fixture acquires the real offscreen worker");
					const HWND identity = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(0x5348));
					const bool registered = worker && RegisterInteractionSurface(identity, 1, Batch::Input, nullptr, Batch::Cancel, &batch, false);
					expect(registered, "arc mailbox fixture registers a non-native surface");
					if (registered)
					{
						SurfaceInputEvent event; event.window = identity; event.generation = 1;
						event.message = WM_NULL;
						expect(SubmitSurfaceInput(event), "mailbox barrier is admitted");
						{
							std::unique_lock lock(batch.mutex);
							expect(batch.changed.wait_for(lock, std::chrono::seconds(5), [&] { return batch.waiting; }), "worker waits before the complete input burst");
						}
						for (LONG x = 10; x<20; ++x)
						{
							event.message = WM_MOUSEMOVE; event.screenPoint = {x, 10};
							expect(SubmitSurfaceInput(event), "ordinary hover burst enters the same mailbox");
						}
						event.captureOwned = true; event.captureTicket = 1; event.presentedRevision = scene.PresentedGeneration();
						event.preserveMoveSamples = scene.PreservePointerTrajectory(down);
						expect(event.preserveMoveSamples, "native Down classification preserves angle-handle samples before worker dispatch");
						event.message = WM_LBUTTONDOWN; event.screenPoint = down; event.wParam = MK_LBUTTON;
						expect(SubmitSurfaceInput(event), "arc Down enters the real mailbox");
						for (int angle = 90; angle <= 300; angle += 5)
						{
							const float radians = angle*pi/180;
							event.screenPoint = {static_cast<LONG>(std::lround(400+120*std::cos(radians))), static_cast<LONG>(std::lround(300+120*std::sin(radians)))};
							event.message = WM_MOUSEMOVE;
							expect(SubmitSurfaceInput(event), "arc raw sample enters the coalescing path");
						}
						event.message = WM_LBUTTONUP; event.wParam = 0;
						expect(SubmitSurfaceInput(event), "arc Up follows the whole raw burst");
						{
							std::unique_lock lock(batch.mutex); batch.released = true; batch.changed.notify_all();
							expect(batch.changed.wait_for(lock, std::chrono::seconds(5), [&] { return batch.done; }), "worker completes the batched trajectory");
						}
						UnregisterInteractionSurface(identity); expect(DrainInteractionSurfaceCallbacks(), "arc mailbox callbacks drain before fixture destruction");
						expect(batch.moves == 43, "all raw samples from90 to300 survive adjacent-Move coalescing under worker backlog");
						expect(batch.hoverMoves == 1 && batch.hoverPoint.x == 19, "ordinary hover still coalesces to the latest sample in the real mailbox");
					}
					worker.reset();
					expect(!actions.empty() && actions.back().kind == ShapeUiActionKind::CommitEdit && actions.back().angleDelta.has_value(),
						"final UI intent carries the cumulative angle");
					Bridge::StateBridge bridge(6);
					bool published = true;
					for (const auto& action : actions) published &= bridge.PublishShapeAction(action) == Bridge::CommandResult::Accepted;
					Bridge::Command command;
					std::vector<ShapeUiAction> drained;
					while (bridge.TryConsume(command)) if (command.shapeAction) drained.push_back(*command.shapeAction);
					expect(published && drained.size() == 3 && drained[0].kind == ShapeUiActionKind::BeginEdit
						&& drained[1].kind == ShapeUiActionKind::PreviewEdit && drained[2].kind == ShapeUiActionKind::CommitEdit
						&& drained[1].angleDelta == drained[2].angleDelta, "raw mailbox samples still produce one latest-wins Bridge preview before Commit");
					if (!actions.empty() && actions.back().angleDelta)
					{
						EditSession drawing;
						expect(drawing.Begin(*shape, 103, actions.front().down), "drawing-side session starts from the same Down");
						expect(drawing.Preview(actions.back().point, 2, nullptr, actions.back().angleDelta), "one surviving terminal reconstructs the full arc trajectory");
						const auto result = drawing.Commit();
						expect(result && std::abs(std::get<ArcConstruction>(result->construction->parameters).sweepRadians-210*pi/180)<.02f,
							"UI trajectory from90 to300 keeps a210-degree sweep after coalescing");
					}
				}
			}
		}

		{
			ObjectId id{}; id[0]=234;
			auto object=MakePresetShape({PresetKind::Rectangle,CreationMode::Drag},{200,200},{600,500},{},id);
			expect(object.has_value(),"round6 native contact fixture creates rectangle");
			if(object)
			{
				auto state=std::make_shared<ShapeUiSnapshot>(); state->object=*object; state->identity.session=991;
				state->identity.objectId=id; state->identity.objectRevision=1; state->predictionMode=0;
				GeometryResult geometry; BuildGeometry(*object,geometry); state->bounds=geometry.bounds;
				state->handles=BuildEditHandles(*object,{},&state->frameRect);
				ShapeEditorSurface scene; std::vector<ShapeUiAction> actions;
				scene.SetActionSink([&](const ShapeUiAction& action){actions.push_back(action);});
				auto now=std::chrono::steady_clock::now();
				for(int i=0;i<75;++i)
				{
					scene.Configure(state,{0,0,1400,1000},{}); now+=std::chrono::milliseconds(17); scene.Advance(now);
					if(SUCCEEDED(scene.EnsureDeviceResources(RenderPipeline::GetDeviceEpoch())) && SUCCEEDED(scene.Present([](const auto&)->BOOL{return TRUE;}))) scene.CommitPresented();
					if(i==3) {
						// 首帧入场尚未结束时走真实 Contact，而非只验证稳定后的 Pointer。
						expect(scene.impl_->toolbarMotion.Active(),"first corner regression runs while toolbar is entering");
						const POINT start{200,200},end{180,175}; actions.clear();
						expect(scene.Hit(start).handle==1000&&!scene.Hit(start).body,"visible first corner takes priority over interior during entry");
						scene.Contact(WM_LBUTTONDOWN,start,51,1,true,false,.1,true,scene.PresentedGeneration());
						scene.Contact(WM_MOUSEMOVE,end,51,1,true,false,.12,true,scene.PresentedGeneration());
						scene.Contact(WM_LBUTTONUP,end,51,1,true,false,.14,true,scene.PresentedGeneration());
						expect(actions.size()==3&&actions.front().handleId==1000&&actions.back().kind==ShapeUiActionKind::CommitEdit
							&&!actions.back().gestureTransform,"first contact resizes the corner instead of starting body translation");
						expect(scene.impl_->localSnapshot&&scene.impl_->localSnapshot->object!=state->object,"first visible corner drag changes the local shape");
						scene.CancelPointer(); actions.clear();
					}
				}
				expect(scene.Hit({340,350}).body && scene.Hit({480,350}).body,"unfilled rectangle interior is a movable editor region");
				const auto send=[&](UINT message,POINT point,unsigned finger,double time,bool touch=true,bool pen=false)
				{ return scene.Contact(message,point,42,finger,touch,pen,time,true,scene.PresentedGeneration()); };
				send(WM_LBUTTONDOWN,{340,350},1,1); send(WM_LBUTTONDOWN,{480,350},2,1.01);
				send(WM_MOUSEMOVE,{480,430},2,1.02); const auto transform=scene.impl_->contacts.Current();
				scene.Contact(WM_MOUSELEAVE,{1500,1100},0,0,false,false,1.025,false,scene.PresentedGeneration());
				expect(scene.impl_->gesture && scene.impl_->contacts.Count()==2,"mouse leave during capture does not cancel a multi-contact drag");
				send(WM_LBUTTONUP,{340,350},1,1.03);
				expect(scene.impl_->gesture && scene.impl_->contacts.Count()==1 && actions.back().kind==ShapeUiActionKind::PreviewEdit,"first finger Up does not commit or end body gesture");
				expect(scene.impl_->contacts.Current().translation==transform.translation,"first finger Up preserves displayed transform");
				send(WM_MOUSEMOVE,{500,450},2,1.04); send(WM_LBUTTONUP,{500,450},2,1.05);
				expect(std::count_if(actions.begin(),actions.end(),[](const auto& a){return a.kind==ShapeUiActionKind::BeginEdit;})==1
					&& std::count_if(actions.begin(),actions.end(),[](const auto& a){return a.kind==ShapeUiActionKind::CommitEdit;})==1,"complete multi-finger gesture emits one Begin and one Commit");
				expect(actions.back().gestureTransform && !actions.back().predictedGestureTransform && !scene.impl_->contactOwner,"last Up commits real transform and retires contact owner");
				for(bool pen:{false,true})
				{
					scene.CancelPointer(); actions.clear(); send(WM_LBUTTONDOWN,{340,350},1,2,false,pen);
					send(WM_MOUSEMOVE,{360,370},1,2.02,false,pen); send(WM_LBUTTONUP,{360,370},1,2.03,false,pen);
					expect(!actions.empty() && actions.back().kind==ShapeUiActionKind::CommitEdit && actions.back().gestureTransform
						&& actions.back().gestureTransform->translation==Vec2{20,20},"mouse and screen pen body gestures translate without pressure");
				}
				scene.CancelPointer(); actions.clear(); send(WM_LBUTTONDOWN,{340,350},1,3); send(WM_MOUSEMOVE,{380,390},1,3.02);
				scene.CancelPointer(); expect(!scene.impl_->contactOwner && scene.impl_->contacts.Count()==0 && actions.back().kind==ShapeUiActionKind::CancelEdit,"cancellation retires every contact and rolls back the transaction");
			}
		}

		SetThemeStyleSource(nullptr);
		RenderPipeline::Shutdown();
		CoUninitialize();
		report<<"scenes="<<scenes<<" commits="<<commits<<" checks="<<checks<<" failures="<<failures<<'\n';
		std::fprintf(stderr, "Shape editor scenes=%d commits=%d checks=%d failures=%d\n", scenes, commits, checks, failures);
		return failures ? 1 : 0;
	}
}
