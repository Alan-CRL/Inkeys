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
#include <d2d1_1helper.h>
#include <wincodec.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
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

namespace Inkeys::UI::Bar
{
	using namespace Inkeys::Drawing::Draw3;
	using namespace draw3::shapes;
	BarSurfaceSharedLighting ReadShapeEditorSharedLighting() noexcept;
	BarUiFrameLightingSnapshot ResolveBarSurfaceFrameLightingSnapshot(const BarSurfaceSharedLighting&,const RECT&,LONG) noexcept;
	namespace
	{
		constexpr unsigned Color = 1, Style = 2, Pattern = 5, Thickness = 9, Sides = 12,
			Swatch = 22, Tone = 33, Rotate = 34, Count = 35;
		constexpr unsigned PaletteHit = 100, ScrollHit = 101;
		bool EmptyRect(RECT r) { return r.right <= r.left || r.bottom <= r.top; }
		bool Inside(D2D1_RECT_F r, double x, double y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
		COLORREF ShapeRgbToUiColor(std::uint32_t rgb) noexcept
		{return RGB((rgb>>16)&255,(rgb>>8)&255,rgb&255);}
		std::uint32_t UiColorToShapeRgb(COLORREF color) noexcept
		{return (static_cast<std::uint32_t>(GetRValue(color))<<16)|(static_cast<std::uint32_t>(GetGValue(color))<<8)|GetBValue(color);}

		void InitShape(BarUiShapeClass& p, double radius, double opacity)
		{
			p.Initialization(0,0,1,1,radius,radius,BarButtonFrameThicknessDip,
				GetThemeColor(BarThemeColorEnum::Surface),GetThemeColor(BarThemeColorEnum::SurfaceFrame));
			p.enable.Initialization(true); p.pct.SetDirect(opacity);
			p.framePct.emplace(BarMainBarFrameOpacity); p.frameLightPct.emplace(BarMainBarFrameOpacity);
			p.frameRendering = BarUiFrameRenderingEnum::PointLight;
			p.frameLightColor = BarUiFrameLightColorEnum::PenWhenDrawing;
		}
		void PlaceShape(BarUiShapeClass& p, double x, double y, double w, double h)
		{
			p.x.SetDirect(x+w/2); p.y.SetDirect(y+h/2); p.w.SetDirect(w); p.h.SetDirect(h); p.UpInh({x,y});
		}
		bool HasOpacity(BarButtonClass& p) { return p.icon.pct.val > .000001 || p.name.pct.val > .000001 || p.button.pct.val > .000001; }
		bool SaveEditorPng(const std::filesystem::path& path,const BYTE* bytes,UINT width,UINT height,UINT stride)
		{
			Microsoft::WRL::ComPtr<IWICImagingFactory> factory;Microsoft::WRL::ComPtr<IWICStream> stream;
			Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;Microsoft::WRL::ComPtr<IPropertyBag2> properties;
			HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
			if(SUCCEEDED(hr))hr=factory->CreateStream(&stream);if(SUCCEEDED(hr))hr=stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE);
			if(SUCCEEDED(hr))hr=factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder);
			if(SUCCEEDED(hr))hr=encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache);if(SUCCEEDED(hr))hr=encoder->CreateNewFrame(&frame,&properties);
			if(SUCCEEDED(hr))hr=frame->Initialize(properties.Get());if(SUCCEEDED(hr))hr=frame->SetSize(width,height);
			WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;if(SUCCEEDED(hr))hr=frame->SetPixelFormat(&format);
			if(SUCCEEDED(hr))hr=frame->WritePixels(height,stride,height*stride,const_cast<BYTE*>(bytes));
			if(SUCCEEDED(hr))hr=frame->Commit();if(SUCCEEDED(hr))hr=encoder->Commit();return SUCCEEDED(hr);
		}

		std::vector<BYTE> ReadEditorPixels(BarUIRendering& renderer,SIZE extent)
		{
			auto* dc=renderer.GetDeviceContext();auto* target=renderer.GetTargetBitmap();if(!dc||!target||extent.cx<=0||extent.cy<=0)return {};
			Microsoft::WRL::ComPtr<ID2D1Bitmap1> readable;const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,target->GetPixelFormat());
			const D2D1_RECT_U crop{0,0,static_cast<UINT32>(extent.cx),static_cast<UINT32>(extent.cy)};
			if(FAILED(dc->CreateBitmap({crop.right,crop.bottom},nullptr,0,&props,&readable))||FAILED(readable->CopyFromBitmap(nullptr,target,&crop)))return {};
			D2D1_MAPPED_RECT mapped{};if(FAILED(readable->Map(D2D1_MAP_OPTIONS_READ,&mapped)))return {};
			std::vector<BYTE> pixels(static_cast<size_t>(extent.cx)*extent.cy*4);
			for(LONG row=0;row<extent.cy;++row)std::memcpy(pixels.data()+static_cast<size_t>(row)*extent.cx*4,mapped.bits+static_cast<size_t>(row)*mapped.pitch,extent.cx*4);
			readable->Unmap();return pixels;
		}

		std::array<double,3> EditorPixelRgb(const std::vector<BYTE>& pixels,SIZE extent,int x,int y)
		{
			if(x<0||y<0||x>=extent.cx||y>=extent.cy||pixels.size()!=static_cast<size_t>(extent.cx)*extent.cy*4)return {};
			const size_t p=(static_cast<size_t>(y)*extent.cx+x)*4;const double alpha=pixels[p+3];
			if(alpha<180)return {-1,-1,-1};
			return {(std::min)(255.,pixels[p+2]*255./alpha),(std::min)(255.,pixels[p+1]*255./alpha),(std::min)(255.,pixels[p]*255./alpha)};
		}
		struct EditorGlyphEvidence{unsigned pixels=0;double peak=0;};
		EditorGlyphEvidence InspectEditorGlyph(const std::vector<BYTE>& pixels,SIZE extent,D2D1_RECT_F rect,POINT background,COLORREF foreground,std::optional<COLORREF> sourceBackground=std::nullopt)
		{
			EditorGlyphEvidence result;if(rect.right<=rect.left||rect.bottom<=rect.top)return result;
			const auto base=sourceBackground?std::array<double,3>{double(GetRValue(*sourceBackground)),double(GetGValue(*sourceBackground)),double(GetBValue(*sourceBackground))}:EditorPixelRgb(pixels,extent,background.x,background.y);if(base[0]<0)return result;
			const std::array<double,3> target{double(GetRValue(foreground)),double(GetGValue(foreground)),double(GetBValue(foreground))};
			std::array<double,3> delta{};double norm=0;for(unsigned c=0;c<3;++c){delta[c]=target[c]-base[c];norm+=delta[c]*delta[c];}if(norm<100)return result;
			for(int y=(std::max)(0,static_cast<int>(std::ceil(rect.top)));y<(std::min)(extent.cy,static_cast<LONG>(rect.bottom));++y)
			for(int x=(std::max)(0,static_cast<int>(std::ceil(rect.left)));x<(std::min)(extent.cx,static_cast<LONG>(rect.right));++x)
			{
				const auto actual=EditorPixelRgb(pixels,extent,x,y);if(actual[0]<0)continue;
				double projection=0;for(unsigned c=0;c<3;++c)projection+=(actual[c]-base[c])*delta[c];projection/=norm;
				double residual=0;for(unsigned c=0;c<3;++c){const double error=actual[c]-base[c]-projection*delta[c];residual+=error*error;}
				if(projection>.20&&projection<1.15&&residual<(sourceBackground?36.:(std::max)(324.,norm*.04))){++result.pixels;result.peak=(std::max)(result.peak,projection);}
			}return result;
		}

		std::wstring Resource(unsigned id)
		{
			if (id >= Style && id < Pattern) return std::array<const wchar_t*,3>{L"barShapeOutline",L"barShapeFill",L"barShapeOutlineFill"}[id-Style];
			if (id >= Pattern && id < Thickness) return std::array<const wchar_t*,4>{L"barShapeSolid",L"barShapeDashed",L"barShapeDotted",L"barShapeDashDot"}[id-Pattern];
			if (id == Rotate) return L"barShapeRotate";
			if (id == Tone) return L"colorSun";
			if (id>=Swatch && id<Tone) return L"colorSelect";
			return {};
		}
	}
	namespace
	{
		// 只保留队列拥塞的终结意图；重试由既有输入worker的surface tick驱动。
		class ShapeIntentSender
		{
		public:
			using Publisher=std::function<Bridge::CommandResult(const ShapeUiAction&)>;
			using Snapshot=std::function<std::shared_ptr<const ShapeUiSnapshot>()>;
			ShapeIntentSender(Publisher publish,Snapshot snapshot):publish_(std::move(publish)),snapshot_(std::move(snapshot)){}
			bool Send(const ShapeUiAction& action)
			{
				std::scoped_lock lock(mutex_);
				const auto current=snapshot_();
				std::erase_if(pending_,[&](const ShapeUiAction& old){return !Current(current,old);});
				const bool terminal=Terminal(action.kind);
				if(!pending_.empty())
				{
					if(!terminal)return false;
					for(auto& old:pending_)if(old.identity.session==action.identity.session&&old.identity.documentGeneration==action.identity.documentGeneration&&old.identity.displayGeneration==action.identity.displayGeneration&&old.identity.workspace==action.identity.workspace&&old.identity.pageId==action.identity.pageId&&old.identity.pageIndex==action.identity.pageIndex&&old.identity.objectId==action.identity.objectId&&old.kind==action.kind){old=action;return true;}
					// 同一当前会话最多五类终结，各类只保留最后一次意图。
					if(pending_.size()>=5)return false;
					pending_.push_back(action);return true;
				}
				const auto result=publish_(action);
				if(result==Bridge::CommandResult::Accepted)return true;
				if(terminal&&result==Bridge::CommandResult::QueueFull){pending_.push_back(action);return true;}return false;
			}
			std::chrono::steady_clock::time_point Tick(std::chrono::steady_clock::time_point now)
			{
				std::scoped_lock lock(mutex_);
				while(!pending_.empty())
				{
					const auto& action=pending_.front();const auto current=snapshot_();
					if(!Current(current,action)){pending_.pop_front();continue;}
					if(publish_(action)==Bridge::CommandResult::QueueFull)return now+std::chrono::milliseconds(16);
					pending_.pop_front();
				}
				return std::chrono::steady_clock::time_point::max();
			}
			void Clear(){std::scoped_lock lock(mutex_);pending_.clear();}
		private:
			static bool Terminal(ShapeUiActionKind kind)
			{
				return kind==ShapeUiActionKind::CommitEdit||kind==ShapeUiActionKind::CancelEdit||kind==ShapeUiActionKind::CloseEditor||
					kind==ShapeUiActionKind::ConstructionComplete||kind==ShapeUiActionKind::ConstructionCancel;
			}
			static bool Current(const std::shared_ptr<const ShapeUiSnapshot>& s,const ShapeUiAction& action)
			{
				if(!s)return false;const auto& a=action.identity;const auto& b=s->identity;
				if(a.session!=b.session||a.documentGeneration!=b.documentGeneration||a.displayGeneration!=b.displayGeneration||a.workspace!=b.workspace||a.pageId!=b.pageId||a.pageIndex!=b.pageIndex)return false;
				if(action.kind==ShapeUiActionKind::ConstructionComplete||action.kind==ShapeUiActionKind::ConstructionCancel)return s->construction.active;
				return s->object&&a.objectId==b.objectId;
			}
			std::mutex mutex_;std::deque<ShapeUiAction> pending_;Publisher publish_;Snapshot snapshot_;
		};
		ShapeIntentSender& IntentSender()
		{
			static ShapeIntentSender sender{[](const auto& a){return PublishShapeUiAction(a);},[]{return ProductShapeUiSnapshot();}};return sender;
		}
	}
	bool SubmitShapeUiIntent(const ShapeUiAction& action) noexcept
	{
		try{const bool accepted=IntentSender().Send(action);WakeInteractionWorker();return accepted;}catch(...){return false;}
	}
	std::chrono::steady_clock::time_point RetryShapeUiIntents(std::chrono::steady_clock::time_point now) noexcept
	{try{return IntentSender().Tick(now);}catch(...){return now+std::chrono::milliseconds(16);}}
	void ClearShapeUiIntents() noexcept {IntentSender().Clear();}

	struct ShapeEditorSurface::Impl
	{
		BarUIRendering renderer;
		BarButtonListClass attributes, sideList, swatches;
		std::array<std::shared_ptr<BarButtonClass>,Count> buttons;
		std::array<std::shared_ptr<BarButtonClass>,2> dividers;
		BarUiShapeClass toolbar, parameterPanel, colorPanel, scrollRail, scrollThumb, rotateSurface;
		BarUiValueClass scroll{0};
		std::atomic<double> scrollRequest=0;
		bool scrollDragging=false;
		double scrollDown=0,scrollAtDown=0;
		RECT scrollWork{},scrollBounds{};
		std::array<BarUiValueClass,Count> x,y;
		BarUiPctClass opacity{0};
		FrameAnimationClock clock;
		std::shared_ptr<const ShapeUiSnapshot> snapshot;
		GeometryResult geometry;
		 draw3::shapes::Bounds localBounds;
		std::array<BarUiValueClass,2> dividerX, dividerY;
		BarUiValueClass parameterExtent{0}, colorExtent{0}, toolbarWidthValue{0}, toolbarHeightValue{0};
		RECT work{}, bar{}, bounds{};
		double scale=1;
		bool active=false, initialized=false, requestedVisible=false;
		std::atomic_bool colorOpen=false, darkTone=false;
		std::uint64_t deviceGeneration=0;
		std::array<float,3> widths{2,4,6};
		struct Frame
		{
			std::shared_ptr<const ShapeUiSnapshot> snapshot;
			GeometryResult geometry;
			RECT bounds{}, work{};
			double scale=1, opacity=0;
			std::uint64_t presentationGeneration=0;
			D2D1_RECT_F toolbar{}, parameters{}, colors{}, palette{}, extensionClip{}, scrollTrack{}, scrollThumb{};
			double scrollOffset=0,scrollMaximum=0;
			std::array<BarButtonHitSnapshot,Count> buttons{};
			bool visible=false, input=false, darkTone=false;
			std::array<float,3> widths{};
		};
		Frame candidate, presented;
		mutable std::mutex inputMutex;
		ButtonPointerState pointer;
		unsigned hover=0, pressed=0, appliedHover=0, appliedPressed=0;
		std::atomic_uint focused=0;
		bool gesture=false, paletteGesture=false;
		ShapeUiIdentity gestureIdentity;
		Vec2 down;
		std::uint32_t handle=0;
		std::function<void(const ShapeUiAction&)> actionSink;
		struct HandleVisual { std::uint32_t id; BarUiShapeClass circle; BarUiValueClass press{1}; };
		std::vector<std::unique_ptr<HandleVisual>> handles;
		std::atomic_uint32_t hoveredHandle=UINT32_MAX, pressedHandle=UINT32_MAX;
		Impl()
		{
			renderer.ConfigureLocalizedTypography();
			InitShape(toolbar,BarMainBarCornerRadiusDip,BarMainBarFillOpacity);
			InitShape(parameterPanel,BarMainBarCornerRadiusDip,BarMainBarFillOpacity);
			InitShape(colorPanel,BarMainBarCornerRadiusDip,BarMainBarFillOpacity);
			InitShape(rotateSurface,BarButtonOneSideDip/2,BarMainBarFillOpacity);
			InitShape(scrollRail,2,.08);InitShape(scrollThumb,2,.35);scrollRail.framePct->SetDirect(0);scrollThumb.framePct->SetDirect(0);
			for (unsigned id=1; id<Count; ++id)
			{
				auto& p=buttons[id]; p=std::make_shared<BarButtonClass>(); p->hide=false; p->size=BarButtonSizeEnum::oneOne;
				InitShape(p->button,BarButtonCornerRadiusDip,0);
				p->button.framePct->SetDirect(0); p->button.frameLightPct->SetDirect(0);
				p->button.framePrimaryLightEnabled=false;
				p->button.frameCursorLightIntensityScale=BarButtonCursorLightIntensity;
				p->icon.Initialization(0,0,GetThemeColor(BarThemeColorEnum::TextPrimary),GetThemeColor(BarThemeColorEnum::TextPrimary));
				p->name.Initialization(0,0,BarButtonOneSideDip,BarButtonOneSideDip,L"",12,GetThemeColor(BarThemeColorEnum::TextPrimary));
				const auto resource=Resource(id);
				p->icon.enable.Initialization(!resource.empty());
				if(!resource.empty())p->icon.InitializationFromResource(L"UI",resource);
				// 公共oneOne布局必须在载荷就绪后解析SVG比例；它默认没有文字槽。
				ConfigureBarButtonVisualTargets(*p,BarButtonVisualLayoutKind::StandardOneOne,0,1,true,
					id>=Swatch&&id<Tone?std::optional<double>(15):std::nullopt);
				p->name.enable.Initialization(true);
				if (id>=Sides && id<Swatch)
				{
					p->name.SetStringImmediate(std::to_wstring(id-Sides+3));
					p->name.x.SetDirect(0);p->name.y.SetDirect(0);p->name.w.SetDirect(BarButtonOneSideDip);p->name.h.SetDirect(BarButtonOneSideDip);p->name.size.SetDirect(12);
				}
				if (id==Rotate) {p->button.rw->SetDirect(BarButtonOneSideDip/2);p->button.rh->SetDirect(BarButtonOneSideDip/2);}
			}
			std::vector<std::shared_ptr<BarButtonClass>> list;
			for(unsigned id=Color;id<Sides;++id)
			{
				if(id==Pattern || id==Thickness)
				{
					auto& d=dividers[id==Pattern?0:1]; d=std::make_shared<BarButtonClass>();
					d->preset=BarButtonPresetEnum::Divider; d->hide=false; InitShape(d->button,.5,.30);
					d->button.framePct->SetDirect(0); d->icon.enable.Initialization(false);d->name.enable.Initialization(false);
					list.push_back(d);
				}
				list.push_back(buttons[id]);
			}
			attributes.Replace(std::move(list)); list.clear();
			for(unsigned id=Sides;id<Swatch;++id)list.push_back(buttons[id]);sideList.Replace(std::move(list));list.clear();
			for(unsigned id=Swatch;id<Tone;++id)list.push_back(buttons[id]);swatches.Replace(std::move(list));
		}
		bool Emit(ShapeUiAction action)
		{
			std::function<void(const ShapeUiAction&)> sink;
			{std::scoped_lock lock(inputMutex);sink=actionSink;}
			if(sink){sink(action);return true;}return SubmitShapeUiIntent(action);
		}
		Vec2 CanvasPoint(const Frame& frame, POINT p) const
		{return {static_cast<float>(p.x-frame.snapshot->canvasOriginX),static_cast<float>(p.y-frame.snapshot->canvasOriginY)};}
		ShapeEditorHit Hit(const Frame& frame, POINT p) const
		{
			ShapeEditorHit hit;if(!frame.visible||!frame.snapshot||!frame.snapshot->object)return hit;
			hit.identity=frame.snapshot->identity;
			const double lx=(p.x-frame.bounds.left)/frame.scale,ly=(p.y-frame.bounds.top)/frame.scale;
			if(frame.scrollMaximum>0&&Inside(frame.scrollTrack,lx,ly)){hit.occupied=true;hit.button=ScrollHit;return hit;}
			const bool inExtension=Inside(frame.extensionClip,lx,ly);
			for(unsigned id=1;id<Count;++id)if(id!=Rotate&&(id<Sides||inExtension)&&HitTestBarButton(frame.buttons[id],lx,ly))
				{hit.occupied=true;hit.button=id;return hit;}
			if(inExtension&&Inside(frame.colors,lx,ly))
			{
				hit.occupied=true;if(frame.input&&Inside(frame.palette,lx,ly)&&frame.buttons[Tone].enabled)hit.button=PaletteHit;return hit;
			}
			if(Inside(frame.toolbar,lx,ly)||(inExtension&&Inside(frame.parameters,lx,ly))){hit.occupied=true;return hit;}
			if(!frame.input)return hit;
			const Vec2 point=CanvasPoint(frame,p);
			float nearest=std::numeric_limits<float>::max();int priority=4;
			for(const auto& h:frame.snapshot->handles)
			{
				const float dx=point.x-h.position.x,dy=point.y-h.position.y,d=dx*dx+dy*dy;
				const int rank=h.kind==EditHandleKind::Rotate?0:h.kind==EditHandleKind::Scale?2:1;
				if(d<=22*22*frame.scale*frame.scale&&(rank<priority||(rank==priority&&d<nearest)))
				{priority=rank;nearest=d;hit.occupied=true;hit.handle=h.id;}
			}
			if(hit.occupied)return hit;
			const auto body=HitTest(*frame.snapshot->object,frame.geometry,point,static_cast<float>(3*frame.scale));
			if(body.kind!=HitKind::None){hit.occupied=true;hit.body=true;}
			return hit;
		}
		void Configure(std::shared_ptr<const ShapeUiSnapshot> next, RECT nextWork, RECT nextBar)
		{
			const bool same=snapshot&&next&&snapshot->identity==next->identity;
			if(!same || !EqualRect(&work,&nextWork) || !EqualRect(&bar,&nextBar))
			{
				if(next&&next->object)
				{
					GeometryResult built;if(BuildGeometry(*next->object,built,{next->displayScale}))
					{
						geometry=std::move(built);auto local=*next->object;local.transform={};
						GeometryResult localGeometry;if(BuildGeometry(local,localGeometry,{next->displayScale}))localBounds=localGeometry.bounds;
					}
					else next.reset();
				}
			}
			if(!next || !next->object || (snapshot&&next->identity.session!=snapshot->identity.session))
			{
				std::scoped_lock lock(inputMutex);colorOpen=false;focused=0;scrollRequest=0;
			}
			requestedVisible=next&&next->object.has_value();
			if(requestedVisible)snapshot=std::move(next);work=nextWork;bar=nextBar;
			const double nextScale=snapshot?(std::max)(.5f,snapshot->displayScale):1;
			if(scale!=nextScale)initialized=false;scale=nextScale;
			opacity.SetTar(requestedVisible?1:0,BarButtonDefaultOperationDurationSeconds);
			if(!snapshot||!snapshot->object)return;
			const auto state=GetStateModeSnapshot();
			for(int i=0;i<3;++i)widths[i]=static_cast<float>(GetBarThicknessPresetMetrics(PenModeSelectEnum::IdtPenSoftPen,i,state).resolvedWidthDip*scale);
		}
		bool Advance(std::chrono::steady_clock::time_point now)
		{
			const double dt=clock.Tick(now),speed=(std::max)(.01,static_cast<double>(BarUiAnimationSpeedRate));
			const BarUiAnimationAdvanceContextClass anim{dt,speed,static_cast<bool>(BarUiAnimationEnabled),false};
			active=false;bool changed=false;
			const auto step=[&](auto& value){const auto r=BarUiAdvanceAnimation(value,anim);active|=r.active;changed|=r.changed;};step(opacity);
			if(!snapshot||!snapshot->object){candidate.visible=false;candidate.input=false;return changed;}
			const auto& object=*snapshot->object;const auto preset=object.construction?object.construction->tool.preset:object.kind==Kind::Line?PresetKind::Line:PresetKind::Rectangle;
			const auto capabilities=GetToolCapabilities(preset); const bool showStyle=capabilities.fill, showStroke=object.style.stroke||!showStyle;
			bool openColor;unsigned hoverId,pressedId;
			{std::scoped_lock lock(inputMutex);openColor=colorOpen;hoverId=hover;pressedId=pressed;}
			for(unsigned id=Color;id<Sides;++id)buttons[id]->hide= !(id==Color||(id<Pattern?showStyle:showStroke));
			dividers[0]->hide=!(showStyle&&showStroke);dividers[1]->hide=!showStroke;
			const auto layout=ResolveBarButtonListLayout(attributes.Snapshot(),BarButtonListLayoutKind::SingleRow);
			const double available=(std::max)(BarButtonOneSideDip+10,(work.right-work.left)/scale-10);
			const double targetToolbarWidth=(std::min)(layout.width,available), rowHeight=BarButtonOneSideDip+5;
			if(!initialized)toolbarWidthValue.SetDirect(targetToolbarWidth);else toolbarWidthValue.SetTar(targetToolbarWidth);
			step(toolbarWidthValue);const double toolbarWidth=(std::max)(BarButtonOneSideDip+10,static_cast<double>(toolbarWidthValue.val));
			// 小工作区按整枚按钮换行，保持触控尺寸，不缩放整个编辑栏。
			double extraY=0,lastX=0;std::array<D2D1_RECT_F,Count> slots{};
			for(const auto& p:layout.placements)
			{
				unsigned id=0;for(unsigned k=Color;k<Sides;++k)if(buttons[k]==p.button){id=k;break;}
				if(!id||p.button->hide)continue;
				double left=p.x-p.width/2-lastX;
				if(left+p.width>targetToolbarWidth-5&&left>5){lastX=p.x-p.width/2-5;left=5;extraY+=rowHeight;}
				slots[id]={static_cast<float>(left),static_cast<float>(5+extraY),static_cast<float>(left+p.width),static_cast<float>(5+extraY+p.height)};
			}
			const double targetToolbarHeight=BarButtonOneSideDip+10+extraY;
			if(!initialized)toolbarHeightValue.SetDirect(targetToolbarHeight);else toolbarHeightValue.SetTar(targetToolbarHeight);
			step(toolbarHeightValue);const double toolbarHeight=(std::max)(BarButtonOneSideDip+10,static_cast<double>(toolbarHeightValue.val));
			const int sideColumns=(std::max)(1,(std::min)(10,static_cast<int>((toolbarWidth-42.5)/rowHeight)));
			const int colorColumns=(std::max)(1,(std::min)(6,static_cast<int>((toolbarWidth-42.5)/rowHeight)));
			const double parameterTarget=capabilities.regularSides?std::ceil(10./sideColumns)*rowHeight+5:0;
			const double colorTarget=openColor?std::ceil(12./colorColumns)*rowHeight+133:0;
			parameterExtent.SetTar(parameterTarget);colorExtent.SetTar(colorTarget);step(parameterExtent);step(colorExtent);
			const double parameterHeight=(std::max)(0.,static_cast<double>(parameterExtent.val)),colorHeight=(std::max)(0.,static_cast<double>(colorExtent.val));
			const double fullExtension=parameterHeight+(parameterHeight&&colorHeight?5:0)+colorHeight;
			const double extensionHeight=(std::min)(fullExtension,(std::max)(BarButtonOneSideDip,(work.bottom-work.top)/scale-toolbarHeight-20));
			const double maximum=(std::max)(0.,fullExtension-extensionHeight);
			const double requested=(std::clamp)(scrollRequest.load(),0.,maximum);scrollRequest=requested;scroll.SetTar(requested);step(scroll);
			const double totalHeight=toolbarHeight+(extensionHeight>0?extensionHeight+5:0);
			const auto& b=snapshot->bounds;
			double screenX=snapshot->canvasOriginX+(b.left+b.right)/2-toolbarWidth*scale/2;
			double screenY=snapshot->canvasOriginY+b.bottom+16*scale;
			if(screenY+totalHeight*scale>work.bottom)screenY=snapshot->canvasOriginY+b.top-(totalHeight+16)*scale;
			screenX=(std::clamp)(screenX,static_cast<double>(work.left+5*scale),(std::max)(static_cast<double>(work.left+5*scale),work.right-(toolbarWidth+5)*scale));
			screenY=(std::clamp)(screenY,static_cast<double>(work.top+5*scale),(std::max)(static_cast<double>(work.top+5*scale),work.bottom-(totalHeight+5)*scale));
			RECT toolbarScreen{static_cast<LONG>(screenX),static_cast<LONG>(screenY),static_cast<LONG>(std::ceil(screenX+toolbarWidth*scale)),static_cast<LONG>(std::ceil(screenY+totalHeight*scale))};
			RECT overlap{};if(!EmptyRect(bar)&&IntersectRect(&overlap,&toolbarScreen,&bar))
			{
				const double above=bar.top-(totalHeight+5)*scale,below=bar.bottom+5*scale;
				if(above>=work.top)screenY=above;else if(below+totalHeight*scale<=work.bottom)screenY=below;
			}
			RECT nextBounds{static_cast<LONG>(std::floor(screenX-8*scale)),static_cast<LONG>(std::floor(screenY-8*scale)),
				static_cast<LONG>(std::ceil(screenX+(toolbarWidth+8)*scale)),static_cast<LONG>(std::ceil(screenY+(totalHeight+8)*scale))};
			for(const auto& h:snapshot->handles)
			{
				nextBounds.left=(std::min)(nextBounds.left,static_cast<LONG>(std::floor(snapshot->canvasOriginX+h.position.x-26*scale)));
				nextBounds.right=(std::max)(nextBounds.right,static_cast<LONG>(std::ceil(snapshot->canvasOriginX+h.position.x+26*scale)));
				nextBounds.top=(std::min)(nextBounds.top,static_cast<LONG>(std::floor(snapshot->canvasOriginY+h.position.y-26*scale)));
				nextBounds.bottom=(std::max)(nextBounds.bottom,static_cast<LONG>(std::ceil(snapshot->canvasOriginY+h.position.y+26*scale)));
			}
			if(b.valid)
			{
				RECT body{static_cast<LONG>(snapshot->canvasOriginX+b.left-8*scale),static_cast<LONG>(snapshot->canvasOriginY+b.top-8*scale),static_cast<LONG>(snapshot->canvasOriginX+b.right+8*scale),static_cast<LONG>(snapshot->canvasOriginY+b.bottom+8*scale)};
				UnionRect(&nextBounds,&nextBounds,&body);
			}
			if(!EqualRect(&bounds,&nextBounds))
			{
				changed=true;
				if(initialized)
				{
					const double dx=(bounds.left-nextBounds.left)/scale,dy=(bounds.top-nextBounds.top)/scale;
					for(unsigned id=1;id<Rotate;++id){x[id].ShiftOrigin(dx);y[id].ShiftOrigin(dy);}
					for(unsigned di=0;di<2;++di){dividerX[di].ShiftOrigin(dx);dividerY[di].ShiftOrigin(dy);}
				}
			}bounds=nextBounds;
			const double tx=(screenX-bounds.left)/scale,ty=(screenY-bounds.top)/scale;
			Frame next;next.snapshot=snapshot;next.geometry=geometry;next.bounds=bounds;next.work=work;next.scale=scale;next.opacity=opacity.val;next.widths=widths;
			next.visible=opacity.val>.000001;next.input=requestedVisible&&opacity.IsSame()&&opacity.val>.99;next.darkTone=darkTone;
			next.toolbar={static_cast<float>(tx),static_cast<float>(ty),static_cast<float>(tx+toolbarWidth),static_cast<float>(ty+toolbarHeight)};
			PlaceShape(toolbar,tx,ty,toolbarWidth,toolbarHeight);
			const double clipTop=ty+toolbarHeight+5;
			if(extensionHeight>0)next.extensionClip={static_cast<float>(tx),static_cast<float>(clipTop),static_cast<float>(tx+toolbarWidth),static_cast<float>(clipTop+extensionHeight)};
			next.scrollOffset=scroll.val;next.scrollMaximum=maximum;
			if(maximum>0)
			{
				next.scrollTrack={static_cast<float>(tx+toolbarWidth-32.5),static_cast<float>(clipTop+5),static_cast<float>(tx+toolbarWidth),static_cast<float>(clipTop+extensionHeight-5)};
				const double trackHeight=next.scrollTrack.bottom-next.scrollTrack.top,thumbHeight=(std::max)(16.,trackHeight*extensionHeight/fullExtension);
				const double thumbTop=next.scrollTrack.top+(trackHeight-thumbHeight)*scroll.val/maximum;
				next.scrollThumb={static_cast<float>(tx+toolbarWidth-9),static_cast<float>(thumbTop),static_cast<float>(tx+toolbarWidth-5),static_cast<float>(thumbTop+thumbHeight)};
				PlaceShape(scrollRail,tx+toolbarWidth-9,next.scrollTrack.top,4,trackHeight);PlaceShape(scrollThumb,next.scrollThumb.left,next.scrollThumb.top,4,thumbHeight);
				scrollRail.fill->SetDirect(GetThemeColor(BarThemeColorEnum::TextPrimary));scrollThumb.fill->SetDirect(GetThemeColor(BarThemeColorEnum::TextPrimary));
			}
			const double py=clipTop-scroll.val;
			if(parameterHeight)
			{
				next.parameters={static_cast<float>(tx),static_cast<float>(py),static_cast<float>(tx+toolbarWidth),static_cast<float>(py+parameterHeight)};
				PlaceShape(parameterPanel,tx,py,toolbarWidth,parameterHeight);
				const auto sideLayout=ResolveBarButtonListLayout(sideList.Snapshot(),BarButtonListLayoutKind::SingleRow);
				for(unsigned k=0;k<10;++k){const unsigned col=k%sideColumns,row=k/sideColumns;const double sx=tx+5+col*rowHeight;slots[Sides+k]={static_cast<float>(sx),static_cast<float>(py+5+row*rowHeight),static_cast<float>(sx+BarButtonOneSideDip),static_cast<float>(py+5+row*rowHeight+BarButtonOneSideDip)};}
			}
			const double cy=py+(parameterHeight?parameterHeight+5:0);
			if(colorHeight>.000001)
			{
				next.colors={static_cast<float>(tx),static_cast<float>(cy),static_cast<float>(tx+toolbarWidth),static_cast<float>(cy+colorHeight)};
				PlaceShape(colorPanel,tx,cy,toolbarWidth,colorHeight);
				for(unsigned k=0;k<11;++k){const double sx=tx+5+(k%colorColumns)*rowHeight,sy=cy+5+(k/colorColumns)*rowHeight;slots[Swatch+k]={static_cast<float>(sx),static_cast<float>(sy),static_cast<float>(sx+BarButtonOneSideDip),static_cast<float>(sy+BarButtonOneSideDip)};}
				slots[Tone]={static_cast<float>(tx+5+(11%colorColumns)*rowHeight),static_cast<float>(cy+5+(11/colorColumns)*rowHeight),static_cast<float>(tx+5+(11%colorColumns)*rowHeight+BarButtonOneSideDip),static_cast<float>(cy+5+(11/colorColumns)*rowHeight+BarButtonOneSideDip)};
				next.palette={static_cast<float>(tx+5),static_cast<float>(cy+5+std::ceil(12./colorColumns)*rowHeight),static_cast<float>(tx+toolbarWidth-37.5),static_cast<float>(cy+colorHeight-5)};
			}
			bool rowStable=toolbarWidthValue.IsSame()&&toolbarHeightValue.IsSame();
			for(unsigned id=1;id<Rotate;++id)
			{
				auto& p=*buttons[id]; const bool wanted=id<Sides?!p.hide.load():id<Swatch?capabilities.regularSides:id<=Tone?openColor:false;
				bool selected=id==Color?openColor:id>=Swatch&&id<Tone?ShapeRgbToUiColor(object.style.stroke?object.style.strokeRgb:object.style.fillRgb)==(GetPresetColor(static_cast<BarThemePresetColorEnum>(id-Swatch))&0x00FFFFFF):id>=Style&&id<Pattern?(object.style.fill?(object.style.stroke?2u:1u):0u)==id-Style:
					id>=Pattern&&id<Thickness?static_cast<unsigned>(object.style.linePattern)==id-Pattern:
					id>=Thickness&&id<Sides?std::abs(object.style.width-widths[id-Thickness])<.01:
					id>=Sides&&id<Swatch?object.construction&&object.construction->tool.sides==id-Sides+3:false;
				const auto text=GetThemeColor(BarThemeColorEnum::TextPrimary);
				p.state->state=selected?BarWidgetState::Selected:BarWidgetState::None;
				RetargetBarButtonInteractionVisual(p,wanted,true,selected,BarButtonDefaultOperationDurationSeconds,text,id>=Swatch&&id<Tone?selected:!Resource(id).empty()||id==Tone);
				if(id==Tone)p.icon.TransitionToResource(L"UI",darkTone?L"colorMoon":L"colorSun");
				if(id==hoverId&&id!=appliedHover)StartBarButtonHoverVisual(p);
				if(id==appliedHover&&id!=hoverId)StopBarButtonHoverVisual(p,false);
				if(id==pressedId||id==appliedPressed)SetBarButtonPressedVisual(p,id==pressedId);
				if(wanted)
				{
					auto r=slots[id];if(id<Sides){r.left+=static_cast<float>(tx);r.right+=static_cast<float>(tx);r.top+=static_cast<float>(ty);r.bottom+=static_cast<float>(ty);}
					const double cx=(r.left+r.right)/2,cy2=(r.top+r.bottom)/2;
					if(!initialized){x[id].SetDirect(cx);y[id].SetDirect(cy2);}else{x[id].SetTar(cx);y[id].SetTar(cy2);}
				}
				step(x[id]);step(y[id]);
				const auto result=AdvanceBarButtonVisual(p,{dt,speed,static_cast<bool>(BarUiAnimationEnabled),false,wanted||HasOpacity(p),true});
				active|=result.active;changed|=result.changed; p.hide=!wanted&&!HasOpacity(p);
				PlaceShape(p.button,x[id].val-BarButtonOneSideDip/2,y[id].val-BarButtonOneSideDip/2,BarButtonOneSideDip,BarButtonOneSideDip);
				next.buttons[id]=ResolveBarButtonHitSnapshot(p,{p.button.inhX,p.button.inhY});next.buttons[id].visible=next.visible&&!p.hide.load();next.buttons[id].enabled&=wanted&&next.input;
				if(id<Sides)rowStable&=x[id].IsSame()&&y[id].IsSame()&&p.icon.pct.IsSame()&&p.name.pct.IsSame();
			}
			for(unsigned id=Color;id<Sides;++id)next.buttons[id].enabled&=rowStable;
			for(unsigned id=Sides;id<Swatch;++id)next.buttons[id].enabled&=scroll.IsSame()&&parameterExtent.IsSame()&&x[id].IsSame()&&y[id].IsSame();
			for(unsigned id=Swatch;id<=Tone;++id)next.buttons[id].enabled&=scroll.IsSame()&&colorExtent.IsSame();
			for(unsigned di=0;di<2;++di)
			{
				auto& d=*dividers[di];const unsigned following=di==0?Pattern:Thickness;const bool wanted=di==0?showStyle&&showStroke:showStroke;
				const double dx=x[following].val-BarButtonOneSideDip/2-3;
				dividerX[di].SetTar(dx);dividerY[di].SetTar(y[following].val);step(dividerX[di]);step(dividerY[di]);
				d.button.pct.SetTar(wanted?.30:0);d.button.frameLightPct->SetTar(wanted?1:0);
				const auto result=AdvanceBarButtonVisual(d,{dt,speed,static_cast<bool>(BarUiAnimationEnabled),false,true,false});active|=result.active;changed|=result.changed;
				PlaceShape(d.button,dividerX[di].val,dividerY[di].val-(BarButtonOneSideDip-4)/2,1,BarButtonOneSideDip-4);
				d.button.fill->SetDirect(GetThemeColor(BarThemeColorEnum::SurfaceFrame));
			}
			appliedHover=hoverId;appliedPressed=pressedId;
			for(auto* p:{&toolbar,&parameterPanel,&colorPanel,&rotateSurface})
			{
				p->fill->SetTar(GetThemeColor(BarThemeColorEnum::Surface));p->frame->SetTar(GetThemeColor(BarThemeColorEnum::SurfaceFrame));
				const auto r=AdvanceBarShapeVisual(*p,{dt,speed,static_cast<bool>(BarUiAnimationEnabled)});active|=r.active;changed|=r.changed;
			}
			for(const auto& h:snapshot->handles)if(h.kind==EditHandleKind::Rotate)
			{
				auto& r=*buttons[Rotate];const double hx=(snapshot->canvasOriginX+h.position.x-bounds.left)/scale,hy=(snapshot->canvasOriginY+h.position.y-bounds.top)/scale;
				PlaceShape(r.button,hx-BarButtonOneSideDip/2,hy-BarButtonOneSideDip/2,BarButtonOneSideDip,BarButtonOneSideDip);
				PlaceShape(rotateSurface,r.button.inhX,r.button.inhY,BarButtonOneSideDip,BarButtonOneSideDip);
				RetargetBarButtonInteractionVisual(r,true,true,false,BarButtonHoverTransitionDuration,GetThemeColor(BarThemeColorEnum::TextPrimary));
				SetBarButtonPressedVisual(r,pressedHandle==h.id);
				if(hoveredHandle==h.id)StartBarButtonHoverVisual(r);else StopBarButtonHoverVisual(r,false);
				const auto result=AdvanceBarButtonVisual(r,{dt,speed,static_cast<bool>(BarUiAnimationEnabled)});active|=result.active;changed|=result.changed;r.hide=false;
			}
			for(const auto& h:snapshot->handles)if(h.kind!=EditHandleKind::Rotate)
			{
				auto found=std::find_if(handles.begin(),handles.end(),[&](const auto& v){return v->id==h.id;});
				if(found==handles.end()){auto v=std::make_unique<HandleVisual>();v->id=h.id;InitShape(v->circle,6,1);v->circle.framePct->SetDirect(1);v->circle.frameLightPct->SetDirect(1);handles.push_back(std::move(v));found=handles.end()-1;}
				auto& v=**found;const bool pressed=pressedHandle==h.id;
				// 几何点与现有圆形控件共用按压比例/曲线；悬停沿共享边缘光反馈，不另造缩放动画。
				v.press.SetTar(pressed?BarButtonPressScale:1.0,BarUiDefaultOperationDur,
					std::nullopt,false,pressed?BarButtonPressCurve():BarButtonReleaseCurve());step(v.press);
				v.circle.frameLightPct->SetTar(hoveredHandle==h.id?BarButtonPressedLightOpacity:0.0,BarButtonHoverTransitionDuration);
				v.circle.fill->SetTar(GetThemeColor(BarThemeColorEnum::Surface));v.circle.frame->SetTar(GetThemeColor(BarThemeColorEnum::Accent));
				const double d=12*v.press.val;PlaceShape(v.circle,(snapshot->canvasOriginX+h.position.x-bounds.left)/scale-d/2,(snapshot->canvasOriginY+h.position.y-bounds.top)/scale-d/2,d,d);
				v.circle.rw->SetDirect(d/2);v.circle.rh->SetDirect(d/2);
				const auto r=AdvanceBarShapeVisual(v.circle,{dt,speed,static_cast<bool>(BarUiAnimationEnabled)});active|=r.active;changed|=r.changed;
			}
			std::erase_if(handles,[&](const auto& v){return std::none_of(snapshot->handles.begin(),snapshot->handles.end(),[&](const auto& h){return h.id==v->id;});});
			renderer.SetFrameLightingSnapshot(ResolveBarSurfaceFrameLightingSnapshot(ReadShapeEditorSharedLighting(),bounds,0));
			candidate=std::move(next);initialized=true;return changed||active;
		}
		void DrawButton(unsigned id)
		{
			auto& p=*buttons[id];if(p.hide)return;
			BarButtonDrawOptions options;
			// 独立圆形主题表面也置于公共按钮变换内，按压回弹与图标始终同心。
			if(id==Rotate)options.underlay=[&](auto& target,auto* dc,auto&,const auto& inh){return DrawBarBackgroundVisual(target,dc,rotateSurface,inh.button);};
			if(id==Color||(id>=Swatch&&id<Tone)|| (id>=Thickness&&id<Sides))
			{
				options.content=[&,id](BarUIRendering& r,ID2D1DeviceContext* dc,BarButtonClass& button,const BarButtonVisualInheritance& inh)
				{
					const double z=scale;const float cx=static_cast<float>((inh.button.x+button.button.w.val/2)*z),cy=static_cast<float>((inh.button.y+button.button.h.val/2)*z);
					if(id>=Thickness&&id<Sides)
					{
						const float diameter=24*static_cast<float>(z),w=(std::min)(diameter,(std::max)(1.f,widths[id-Thickness]));
						auto* brush=r.ContentSolidBrush(button.name.color.val,button.name.pct.val);if(!brush)return false;
						D2D1_MATRIX_3X2_F transform;dc->GetTransform(&transform);dc->SetTransform(D2D1::Matrix3x2F::Rotation(-45,{cx,cy})*transform);
						dc->FillRoundedRectangle(D2D1::RoundedRect({cx-diameter/2,cy-w/2,cx+diameter/2,cy+w/2},w/2,w/2),brush);dc->SetTransform(transform);
					}
					else
					{
						const auto rgb=id==Color?ShapeRgbToUiColor(snapshot->object->style.stroke?snapshot->object->style.strokeRgb:snapshot->object->style.fillRgb):GetPresetColor(static_cast<BarThemePresetColorEnum>(id-Swatch));
						const D2D1_ELLIPSE ellipse{{cx,cy},static_cast<float>(10*z),static_cast<float>(10*z)};
						// 公共solid brush只在本次draw内有效；获取另一颜色前先消费当前刷，避免把fill一起改成灰色。
						if(auto* fill=r.ContentSolidBrush(rgb,button.name.pct.val))dc->FillEllipse(ellipse,fill);
						if(auto* frame=r.ContentSolidBrush(GetThemeColor(BarThemeColorEnum::SwatchFrame),button.name.pct.val))dc->DrawEllipse(ellipse,frame,static_cast<float>(z));
						if(id>=Swatch&&id<Tone)r.Svg(dc,button.icon,inh.icon);
					}
					return true;
				};
			}
			if(id==Color||(id>=Swatch&&id<Tone))options.drawIcon=false;
			DrawBarButtonVisual(renderer,renderer.GetDeviceContext(),p,{p.button.inhX,p.button.inhY},options);
		}
		void Draw()
		{
			if(!candidate.visible)return;auto* dc=renderer.GetDeviceContext();if(!dc)return;
			renderer.SetFrameZoom(scale);
			if(candidate.input)
			{
				auto* input=renderer.ContentSolidBrush(GetThemeColor(BarThemeColorEnum::Surface),1.0/255);
				if(input)
				{
					const auto& b=snapshot->bounds;
					if(b.valid)dc->FillRectangle({static_cast<float>(snapshot->canvasOriginX+b.left-bounds.left-3*scale),static_cast<float>(snapshot->canvasOriginY+b.top-bounds.top-3*scale),static_cast<float>(snapshot->canvasOriginX+b.right-bounds.left+3*scale),static_cast<float>(snapshot->canvasOriginY+b.bottom-bounds.top+3*scale)},input);
					for(const auto& h:snapshot->handles)dc->FillEllipse({{static_cast<float>(snapshot->canvasOriginX+h.position.x-bounds.left),static_cast<float>(snapshot->canvasOriginY+h.position.y-bounds.top)},static_cast<float>(22*scale),static_cast<float>(22*scale)},input);
				}
			}
			renderer.PushPanelOpacity(dc,{0,0,static_cast<float>(bounds.right-bounds.left),static_cast<float>(bounds.bottom-bounds.top)},candidate.opacity);
			// 本体只有视觉外框，空白内部仍穿透；旋转外框从本地包围盒变换生成。
			const auto& object=*snapshot->object;
			if(localBounds.valid&&object.kind!=Kind::Line&&object.kind!=Kind::Polyline&&object.kind!=Kind::Circle)
			{
				const auto& b=localBounds;const std::array<Vec2,4> points{{{b.left,b.top},{b.right,b.top},{b.right,b.bottom},{b.left,b.bottom}}};
				auto* brush=renderer.ContentSolidBrush(GetThemeColor(BarThemeColorEnum::Accent),.70);
				if(brush)for(unsigned i=0;i<4;++i)
				{
					const auto a=ApplyTransform(object.transform,points[i]),c=ApplyTransform(object.transform,points[(i+1)%4]);
					dc->DrawLine({static_cast<float>(snapshot->canvasOriginX+a.x-bounds.left),static_cast<float>(snapshot->canvasOriginY+a.y-bounds.top)},
						{static_cast<float>(snapshot->canvasOriginX+c.x-bounds.left),static_cast<float>(snapshot->canvasOriginY+c.y-bounds.top)},brush,static_cast<float>(scale));
				}
			}
			for(const auto& h:snapshot->handles)
			{
				if(h.kind==EditHandleKind::Rotate){DrawButton(Rotate);continue;}
				const auto it=std::find_if(handles.begin(),handles.end(),[&](const auto& v){return v->id==h.id;});
				if(it!=handles.end())renderer.Shape(dc,(*it)->circle,{(*it)->circle.inhX,(*it)->circle.inhY});
			}
			DrawBarBackgroundVisual(renderer,dc,toolbar,{toolbar.inhX,toolbar.inhY});
			dc->PushAxisAlignedClip({static_cast<float>(candidate.toolbar.left*scale),static_cast<float>(candidate.toolbar.top*scale),static_cast<float>(candidate.toolbar.right*scale),static_cast<float>(candidate.toolbar.bottom*scale)},D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			for(unsigned id=1;id<Sides;++id)DrawButton(id);
			for(auto& d:dividers)if(d->button.pct.val>.000001)renderer.Shape(dc,d->button,{d->button.inhX,d->button.inhY});dc->PopAxisAlignedClip();
			if(candidate.extensionClip.bottom>candidate.extensionClip.top)
			{
				const auto r=candidate.extensionClip;
				dc->PushAxisAlignedClip({static_cast<float>(r.left*scale),static_cast<float>(r.top*scale),static_cast<float>(r.right*scale),static_cast<float>(r.bottom*scale)},D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
				if(candidate.parameters.right>candidate.parameters.left)DrawBarBackgroundVisual(renderer,dc,parameterPanel,{parameterPanel.inhX,parameterPanel.inhY});
				if(candidate.colors.right>candidate.colors.left)
				{
					DrawBarBackgroundVisual(renderer,dc,colorPanel,{colorPanel.inhX,colorPanel.inhY});
					D2D1_RECT_F p=candidate.palette;p.left*=static_cast<float>(scale);p.right*=static_cast<float>(scale);p.top*=static_cast<float>(scale);p.bottom*=static_cast<float>(scale);
					if(p.right>p.left&&p.bottom>p.top)renderer.ColorPalette(dc,p,darkTone,true);
				}
				for(unsigned id=Sides;id<Rotate;++id)DrawButton(id);dc->PopAxisAlignedClip();
				if(candidate.scrollMaximum>0){renderer.Shape(dc,scrollRail,{scrollRail.inhX,scrollRail.inhY});renderer.Shape(dc,scrollThumb,{scrollThumb.inhX,scrollThumb.inhY});}
			}

			dc->PopLayer();
		}
		void Activate(unsigned id, const Frame& frame, POINT screen)
		{
			if(!frame.snapshot||!frame.snapshot->object)return;
			if(id==Color){std::scoped_lock lock(inputMutex);colorOpen=!colorOpen.load();return;}
			if(id==Tone){std::scoped_lock lock(inputMutex);darkTone=!darkTone.load();return;}
			ShapeUiAction a;a.identity=frame.snapshot->identity;a.kind=ShapeUiActionKind::SetStyle;a.style=frame.snapshot->object->style;
			if(id>=Style&&id<Pattern){a.style.stroke=id!=Style+1;a.style.fill=id!=Style;}
			else if(id>=Pattern&&id<Thickness)a.style.linePattern=static_cast<LinePattern>(id-Pattern);
			else if(id>=Thickness&&id<Sides)a.style.width=frame.widths[id-Thickness];
			else if(id>=Sides&&id<Swatch){a.kind=ShapeUiActionKind::SetSides;a.sides=id-Sides+3;}
			else if(id>=Swatch&&id<Tone)a.style.strokeRgb=a.style.fillRgb=UiColorToShapeRgb(GetPresetColor(static_cast<BarThemePresetColorEnum>(id-Swatch)));
			else if(id==PaletteHit)
			{
				const auto r=frame.palette;const double x=((screen.x-frame.bounds.left)/frame.scale-r.left)/(r.right-r.left),y=((screen.y-frame.bounds.top)/frame.scale-r.top)/(r.bottom-r.top);
				a.style.strokeRgb=a.style.fillRgb=UiColorToShapeRgb(GetBarColorPickerColor(x,y,frame.darkTone,true));
			}
			else return;Emit(a);
		}
	};
	ShapeEditorSurface::ShapeEditorSurface():impl_(std::make_unique<Impl>()){}
	ShapeEditorSurface::~ShapeEditorSurface()=default;
	void ShapeEditorSurface::Configure(std::shared_ptr<const ShapeUiSnapshot> s,RECT work,RECT bar){impl_->Configure(std::move(s),work,bar);}
	bool ShapeEditorSurface::Advance(std::chrono::steady_clock::time_point now){return impl_->Advance(now);}
	bool ShapeEditorSurface::Visible()const noexcept{return impl_->candidate.visible;}
	bool ShapeEditorSurface::AnimationActive()const noexcept{return impl_->active;}
	RECT ShapeEditorSurface::Bounds()const noexcept{return impl_->bounds;}
	HRESULT ShapeEditorSurface::EnsureDeviceResources(const RenderPipeline::DeviceEpoch& epoch)
	{
		auto& p=*impl_;if(p.deviceGeneration!=epoch.generation){for(auto& b:p.buttons)if(b)b->icon.ResetCache();p.deviceGeneration=epoch.generation;}
		return p.renderer.EnsureDeviceResources(epoch,(std::max)(1L,p.bounds.right-p.bounds.left),(std::max)(1L,p.bounds.bottom-p.bounds.top));
	}
	BarUIRendering& ShapeEditorSurface::Renderer()noexcept{return impl_->renderer;}
	void ShapeEditorSurface::Draw(){impl_->Draw();}
	HRESULT ShapeEditorSurface::Present(const std::function<BOOL(const UPDATELAYEREDWINDOWINFO&)>& sink)
	{
		auto& p=*impl_;auto* dc=p.renderer.GetDeviceContext();auto* gdi=p.renderer.GetGdiInteropRenderTarget();
		if(!dc||!gdi||!sink)return E_UNEXPECTED;
		p.renderer.BeginFrameDraw(dc,{0,0,static_cast<float>(p.bounds.right-p.bounds.left),static_cast<float>(p.bounds.bottom-p.bounds.top)});
		p.Draw();p.renderer.FinishFrameDrawCommands(dc);HDC source=nullptr;
		const HRESULT get=gdi->GetDC(D2D1_DC_INITIALIZE_MODE_COPY,&source);HRESULT release=S_OK;bool presented=false;
		if(SUCCEEDED(get)&&source)
		{
			POINT dst{p.bounds.left,p.bounds.top},src{};SIZE size{p.bounds.right-p.bounds.left,p.bounds.bottom-p.bounds.top};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
			UPDATELAYEREDWINDOWINFO info{sizeof(info)};info.hdcSrc=source;info.pptDst=&dst;info.psize=&size;info.pptSrc=&src;info.pblend=&blend;info.dwFlags=ULW_ALPHA;
			presented=sink(info)!=FALSE;const RECT modified{};release=gdi->ReleaseDC(&modified);
		}
		const HRESULT end=p.renderer.EndFrameDraw(dc);
		if(FAILED(end))return end;if(FAILED(get))return get;if(FAILED(release))return release;return presented?S_OK:E_FAIL;
	}
	void ShapeEditorSurface::CommitPresented(bool enabled){std::scoped_lock lock(impl_->inputMutex);const auto generation=impl_->presented.presentationGeneration+1;impl_->presented=impl_->candidate;impl_->presented.presentationGeneration=generation;impl_->presented.input&=enabled;}
	ShapeEditorHit ShapeEditorSurface::Hit(POINT screen)const{std::scoped_lock lock(impl_->inputMutex);return impl_->Hit(impl_->presented,screen);}
	void ShapeEditorSurface::SetActionSink(std::function<void(const ShapeUiAction&)> sink){std::scoped_lock lock(impl_->inputMutex);impl_->actionSink=std::move(sink);}
	void ShapeEditorSurface::ReleaseDeviceResources(){impl_->renderer.DiscardDeviceResources();for(auto& b:impl_->buttons)if(b)b->icon.ResetCache();}
	std::uint64_t ShapeEditorSurface::PresentedGeneration()const{std::scoped_lock lock(impl_->inputMutex);return impl_->presented.presentationGeneration;}
	bool ShapeEditorSurface::Pointer(UINT message,POINT point,bool captureOwned,std::uint64_t expectedPresentation,short wheelDelta)
	{
		auto& p=*impl_;Impl::Frame frame;ShapeEditorHit hit;
		{std::scoped_lock lock(p.inputMutex);frame=p.presented;hit=p.Hit(frame,point);}
		if((expectedPresentation&&expectedPresentation!=frame.presentationGeneration&&!p.gesture&&!p.scrollDragging)||message==WM_CANCELMODE||message==WM_CAPTURECHANGED||(!captureOwned&&(p.gesture||p.paletteGesture||p.scrollDragging))){CancelPointer();return true;}
		if(message==WM_MOUSEWHEEL)
		{
			const double lx=(point.x-frame.bounds.left)/frame.scale,ly=(point.y-frame.bounds.top)/frame.scale;
			if(Inside(frame.extensionClip,lx,ly)&&frame.scrollMaximum>0){p.scrollRequest=(std::clamp)(frame.scrollOffset-wheelDelta/120.*75.,0.,frame.scrollMaximum);return true;}
			return hit.occupied;
		}
		if(p.scrollDragging)
		{
			if(!EqualRect(&frame.work,&p.scrollWork)||!EqualRect(&frame.bounds,&p.scrollBounds)){CancelPointer();return true;}
			const double travel=(frame.scrollTrack.bottom-frame.scrollTrack.top)-(frame.scrollThumb.bottom-frame.scrollThumb.top);
			if(message==WM_MOUSEMOVE&&travel>0)p.scrollRequest=(std::clamp)(p.scrollAtDown+(point.y-p.scrollDown)/frame.scale*frame.scrollMaximum/travel,0.,frame.scrollMaximum);
			if(message==WM_LBUTTONUP)p.scrollDragging=false;return true;
		}
		if(message==WM_LBUTTONDOWN&&hit.button==ScrollHit)
		{p.scrollDragging=true;p.scrollDown=point.y;p.scrollAtDown=frame.scrollOffset;p.scrollWork=frame.work;p.scrollBounds=frame.bounds;return true;}
		if(p.gesture)
		{
			if(!frame.snapshot||frame.snapshot->identity.session!=p.gestureIdentity.session||frame.snapshot->identity.documentGeneration!=p.gestureIdentity.documentGeneration||frame.snapshot->identity.displayGeneration!=p.gestureIdentity.displayGeneration){CancelPointer();return true;}
			if(message==WM_MOUSEMOVE||message==WM_LBUTTONUP)
			{
				ShapeUiAction a;a.identity=p.gestureIdentity;a.kind=message==WM_LBUTTONUP?ShapeUiActionKind::CommitEdit:ShapeUiActionKind::PreviewEdit;a.handleId=p.handle;a.down=p.down;a.point=p.CanvasPoint(frame,point);p.Emit(a);
				if(message==WM_LBUTTONUP){p.gesture=false;p.pressedHandle=UINT32_MAX;}
			}return true;
		}
		if(p.paletteGesture)
		{
			if(message==WM_LBUTTONUP){p.paletteGesture=false;if(hit.button==PaletteHit)p.Activate(PaletteHit,frame,point);}return true;
		}
		if(message==WM_LBUTTONDOWN&&hit.occupied&&!hit.button&&(hit.body||hit.handle))
		{
			p.gesture=true;p.gestureIdentity=hit.identity;p.handle=hit.body?0:hit.handle;p.down=p.CanvasPoint(frame,point);p.pressedHandle=p.handle;
			ShapeUiAction a;a.kind=ShapeUiActionKind::BeginEdit;a.identity=hit.identity;a.handleId=p.handle;a.down=a.point=p.down;if(!p.Emit(a)){p.gesture=false;p.pressedHandle=UINT32_MAX;}return true;
		}
		if(message==WM_LBUTTONDOWN&&hit.button==PaletteHit){p.paletteGesture=true;return true;}
		ButtonPointerEvent event=message==WM_LBUTTONDOWN?ButtonPointerEvent::Down:message==WM_LBUTTONUP?ButtonPointerEvent::Up:message==WM_MOUSELEAVE?ButtonPointerEvent::Leave:ButtonPointerEvent::Move;
		unsigned activate=0;
		{std::scoped_lock lock(p.inputMutex);auto r=p.pointer.Update(event,hit.button);activate=static_cast<unsigned>(r.activate);p.hover=static_cast<unsigned>(r.hover);p.pressed=static_cast<unsigned>(r.pressed);p.hoveredHandle=hit.occupied&&!hit.button?hit.handle:UINT32_MAX;
			if(r.activate)p.focused=static_cast<unsigned>(r.activate);}
		if(activate)p.Activate(activate,frame,point);
		return hit.occupied;
	}
	void ShapeEditorSurface::CancelPointer()
	{
		auto& p=*impl_;if(p.gesture){ShapeUiAction a;a.kind=ShapeUiActionKind::CancelEdit;a.identity=p.gestureIdentity;p.Emit(a);}
		std::scoped_lock lock(p.inputMutex);p.gesture=p.paletteGesture=p.scrollDragging=false;p.pointer.Reset();p.hover=p.pressed=0;p.hoveredHandle=UINT32_MAX;p.pressedHandle=UINT32_MAX;
	}
	bool ShapeEditorSurface::Keyboard(BYTE key,bool down)
	{
		auto& p=*impl_;Impl::Frame frame;{std::scoped_lock lock(p.inputMutex);frame=p.presented;}
		if(!frame.snapshot||!frame.visible)return false;
		if(key==VK_ESCAPE){if(down){CancelPointer();ShapeUiAction a;a.kind=ShapeUiActionKind::CloseEditor;a.identity=frame.snapshot->identity;p.Emit(a);}return true;}
		if(key==VK_TAB||key==VK_RIGHT||key==VK_LEFT)
		{
			if(down){const int step=(key==VK_LEFT||(key==VK_TAB&&(GetKeyState(VK_SHIFT)&0x8000)))?-1:1;for(unsigned k=0;k<Count;++k){p.focused=(p.focused.load()+Count+step)%Count;if(frame.buttons[p.focused.load()].visible&&frame.buttons[p.focused.load()].enabled)break;}}return true;
		}
		if(key==VK_RETURN||key==VK_SPACE){if(down&&p.focused&&frame.buttons[p.focused.load()].enabled)p.Activate(p.focused.load(),frame,{});return true;}return false;
	}
	int RunShapeEditorOffscreenTest()
	{
		_CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
		const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(FAILED(com))return 2;
		if(FAILED(RenderPipeline::Initialize())){CoUninitialize();return 3;}
		std::filesystem::create_directories(L"Build/shape-p34-ui");std::ofstream report("Build/shape-p34-ui/results.log");
		int checks=0,failures=0,scenes=0,commits=0;
		auto expect=[&](bool ok,const char* why){++checks;if(!ok){++failures;report<<"FAIL "<<why<<" case="<<scenes<<'\n';}};
		expect(ShapeRgbToUiColor(0x1E8CDCu)==RGB(30,140,220)&&UiColorToShapeRgb(RGB(30,140,220))==0x1E8CDCu,
			"shape RGB and UI COLORREF keep explicit red and blue channels");
		expect(ShapeRgbToUiColor(0x1E8CDCu)!=RGB(220,140,30)&&UiColorToShapeRgb(RGB(30,140,220))!=0xDC8C1Eu,
			"red-blue-swapped negative color fixture is rejected");
		// 容量为零的发布端模拟QueueFull，验证已开始手势的终结不会丢失或持续唤醒。
		for(auto terminal:{ShapeUiActionKind::CommitEdit,ShapeUiActionKind::CancelEdit,ShapeUiActionKind::CloseEditor,ShapeUiActionKind::ConstructionComplete,ShapeUiActionKind::ConstructionCancel})
		{
			auto state=std::make_shared<ShapeUiSnapshot>();state->identity.session=77;state->identity.objectId[0]=1;state->object=ShapeObject{};state->object->id=state->identity.objectId;state->construction.active=true;
			bool full=false;std::vector<ShapeUiAction> accepted;
			ShapeIntentSender sender{[&](const ShapeUiAction& action){if(full)return Bridge::CommandResult::QueueFull;accepted.push_back(action);return Bridge::CommandResult::Accepted;},[&]{return state;}};
			ShapeUiAction begin;begin.kind=ShapeUiActionKind::BeginEdit;begin.identity=state->identity;
			expect(sender.Send(begin),"capacity fixture first accepts BeginEdit");full=true;
			ShapeUiAction finish;finish.kind=terminal;finish.identity=state->identity;expect(sender.Send(finish),"QueueFull retains one terminal intent");
			for(unsigned revision=1;revision<=128;++revision){finish.identity.generation=revision;expect(sender.Send(finish),"same-scene generation changes coalesce to one bounded terminal slot");}
			const auto now=std::chrono::steady_clock::now();expect(sender.Tick(now)!=std::chrono::steady_clock::time_point::max(),"pending terminal alone requests a finite worker tick");
			full=false;expect(sender.Tick(now)==std::chrono::steady_clock::time_point::max()&&accepted.size()==2&&accepted.back().kind==terminal,"restored capacity delivers terminal once and allows worker sleep");
			full=true;expect(sender.Send(finish),"second terminal can become pending");++state->identity.session;full=false;
			expect(sender.Tick(now)==std::chrono::steady_clock::time_point::max()&&accepted.size()==2,"old-session pending terminal never crosses a new editor or construction session");
		}

		auto now=std::chrono::steady_clock::now();BarStyleClass style;SetThemeStyleSource(&style);
		for(bool dark:{false,true})for(const wchar_t* language:{L"zh-CN",L"zh-TW",L"en-US"})for(float scale:{.75f,1.f,2.f})
		for(unsigned presetIndex=0;presetIndex<=static_cast<unsigned>(PresetKind::Pyramid);++presetIndex)
		{
			++scenes;style.darkStyle=dark;expect(I18n::load(1,L"JSON",language),"language resources load");
			const auto preset=static_cast<PresetKind>(presetIndex);ToolSpec tool;tool.preset=preset;tool.creationMode=DefaultCreationMode(preset);tool.sides=5;
			ShapeStyle shapeStyle;shapeStyle.width=4*scale;shapeStyle.strokeRgb=shapeStyle.fillRgb=0x1E8CDCu;shapeStyle.strokeOpacity=shapeStyle.fillOpacity=.6f;
			Construction construction;construction.tool=tool;construction.start={130*scale,150*scale};construction.end={300*scale,250*scale};
			if(GetToolCapabilities(preset).points)construction.parameters=VertexConstruction{{{130*scale,130*scale},{310*scale,150*scale},{280*scale,280*scale},{180*scale,200*scale}}};
			else if(preset==PresetKind::Arc||preset==PresetKind::Sector)construction.parameters=ArcConstruction{4.3f};
			else if(preset==PresetKind::Cuboid||preset==PresetKind::Cube)construction.parameters=SolidConstruction{{60*scale,-45*scale},0};
			else if(preset>=PresetKind::Cylinder)construction.parameters=SolidConstruction{{},-120*scale};
			ObjectId id{};id[0]=1;id[1]=static_cast<unsigned char>(presetIndex+1);
			auto object=MakeConstructedShape(construction,shapeStyle,id,1);
			expect(object.has_value(),"approved preset yields editable construction");if(!object)continue;
			auto snapshot=std::make_shared<ShapeUiSnapshot>();snapshot->object=*object;snapshot->identity.session=1;snapshot->identity.generation=1;
			snapshot->identity.objectId=id;snapshot->identity.objectRevision=1;snapshot->identity.documentGeneration=1;snapshot->identity.displayGeneration=1;
			snapshot->canvasOriginX=-1200;snapshot->canvasOriginY=-300;snapshot->displayScale=scale;snapshot->handles=BuildEditHandles(*object,{scale});
			GeometryResult geometry;expect(BuildGeometry(*object,geometry,{scale}),"production geometry builds");snapshot->bounds=geometry.bounds;
			ShapeEditorSurface scene;std::vector<ShapeUiAction> actions;scene.SetActionSink([&](const auto& action){actions.push_back(action);});
			const RECT work{-1200,-300,static_cast<LONG>(-1200+900*scale),static_cast<LONG>(-300+700*scale)};
			scene.Configure(snapshot,work,{});for(int frame=0;frame<70;++frame){now+=std::chrono::milliseconds(17);scene.Advance(now);}
			expect(scene.Visible(),"editor opens with shared animation");expect(!scene.AnimationActive(),"settled editor allows render sleep");
			expect(SUCCEEDED(scene.EnsureDeviceResources(RenderPipeline::GetDeviceEpoch())),"shared epoch creates private editor target");
			unsigned nontransparent=0;SIZE receipt{};
			auto sink=[&](const UPDATELAYEREDWINDOWINFO& info)->BOOL
			{
				expect(info.hdcSrc&&info.psize&&info.psize->cx>0&&info.psize->cy>0,"production ULW sink receives actual DC and extent");
				receipt=*info.psize;++commits;return TRUE;
			};
			auto present=[&]()
			{
				// 与生产Render同序：布局改变ULW尺寸后，先重建当前target，不能读取旧目标外的零像素。
				const HRESULT resource=scene.EnsureDeviceResources(RenderPipeline::GetDeviceEpoch());if(FAILED(resource))return resource;
				const HRESULT hr=scene.Present(sink);if(FAILED(hr))return hr;
				const auto targetSize=scene.Renderer().GetTargetBitmap()->GetPixelSize();expect(targetSize.width==receipt.cx&&targetSize.height==receipt.cy,"actual ULW extent matches this frame target before content readback");
				const auto pixels=ReadEditorPixels(scene.Renderer(),receipt);expect(pixels.size()==static_cast<size_t>(receipt.cx)*receipt.cy*4,"actual committed ULW source crop can be read after EndDraw");
				nontransparent=0;for(size_t pixel=3;pixel<pixels.size();pixel+=4)if(pixels[pixel])++nontransparent;
				const auto& candidate=scene.impl_->candidate;
				auto glyph=[&](unsigned id,bool word,COLORREF expected)
				{
					const auto& button=*scene.impl_->buttons[id];const double z=candidate.scale;
					D2D1_RECT_F dip=word?D2D1_RECT_F{static_cast<float>(button.name.inhX+3),static_cast<float>(button.name.inhY+3),static_cast<float>(button.name.inhX+button.name.w.val-3),static_cast<float>(button.name.inhY+button.name.h.val-3)}:
						D2D1_RECT_F{static_cast<float>(button.icon.inhX),static_cast<float>(button.icon.inhY),static_cast<float>(button.icon.inhX+button.icon.w.val),static_cast<float>(button.icon.inhY+button.icon.h.val)};
					if(id>=Sides&&id<Rotate&&(dip.top<candidate.extensionClip.top||dip.bottom>candidate.extensionClip.bottom))return;
					const bool badge=id>=Swatch&&id<Tone;
					const std::optional<COLORREF> badgeBase=badge?std::optional<COLORREF>(RGB(0,200,129)):std::nullopt;
					if(badge)
					{
						// 原colorSelect白勾位于固定绿色圆内；裁掉外圈，不能按主题选中底块推算细线覆盖。
						const float insetX=(dip.right-dip.left)/4,insetY=(dip.bottom-dip.top)/4;
						dip.left+=insetX;dip.right-=insetX;dip.top+=insetY;dip.bottom-=insetY;
					}
					D2D1_RECT_F rect{static_cast<float>(dip.left*z),static_cast<float>(dip.top*z),static_cast<float>(dip.right*z),static_cast<float>(dip.bottom*z)};
					const POINT blank{static_cast<LONG>((button.button.inhX+button.button.w.val/2)*z),static_cast<LONG>((button.button.inhY+2)*z)};
					const auto evidence=InspectEditorGlyph(pixels,receipt,rect,blank,expected,badgeBase);
					if(!(evidence.pixels>=2&&evidence.peak>.30))report<<"GLYPH id="<<id<<" word="<<word<<" count="<<evidence.pixels<<" peak="<<evidence.peak<<" width="<<rect.right-rect.left<<" height="<<rect.bottom-rect.top<<" rect="<<rect.left<<','<<rect.top<<" bg="<<blank.x<<','<<blank.y<<" bgRgb="<<EditorPixelRgb(pixels,receipt,blank.x,blank.y)[0]<<','<<EditorPixelRgb(pixels,receipt,blank.x,blank.y)[1]<<','<<EditorPixelRgb(pixels,receipt,blank.x,blank.y)[2]<<'\n';
					expect(evidence.pixels>=2&&evidence.peak>.30,word?"actual emitted side number has foreground ink beyond selected background":"actual emitted SVG has foreground ink beyond alpha1 hit region and selected background");
					const D2D1_RECT_F empty{0,0,static_cast<float>(20*z),static_cast<float>(20*z)};
					const auto negative=InspectEditorGlyph(pixels,receipt,empty,{1,1},expected,badgeBase);expect(negative.pixels==0,"glyph oracle rejects an actual blank source crop");
					if(badge)
					{
						const double cy=button.button.inhY+button.button.h.val/2;
						const D2D1_RECT_F padding{static_cast<float>((button.button.inhX+2)*z),static_cast<float>((cy-2)*z),static_cast<float>((button.button.inhX+4)*z),static_cast<float>((cy+2)*z)};
						const auto baseOnly=InspectEditorGlyph(pixels,receipt,padding,blank,expected,badgeBase);
						expect(baseOnly.pixels==0,"badge white-stroke oracle rejects the real selected button background without its SVG");
					}
					if(id==Rotate)
					{
						const auto surface=EditorPixelRgb(pixels,receipt,blank.x,blank.y);const auto theme=GetThemeColor(BarThemeColorEnum::Surface);
						expect(surface[0]>=0&&std::abs(surface[0]-GetRValue(theme))<14&&std::abs(surface[1]-GetGValue(theme))<14&&std::abs(surface[2]-GetBValue(theme))<14,"rotation control has a visible circular theme surface rather than alpha1 canvas hit coverage");
					}
				};
				for(unsigned id=Style;id<Thickness;++id)if(candidate.buttons[id].visible)glyph(id,false,scene.impl_->buttons[id]->icon.color1->val);
				for(const auto& h:snapshot->handles)if(h.kind==EditHandleKind::Rotate)glyph(Rotate,false,scene.impl_->buttons[Rotate]->icon.color1->val);
				for(unsigned id=Sides;id<Swatch;++id)if(candidate.buttons[id].visible)glyph(id,true,scene.impl_->buttons[id]->name.color.val);
				if(scene.impl_->colorOpen.load())
				{
					glyph(Tone,false,scene.impl_->buttons[Tone]->icon.color1->val);
					for(unsigned id=Color;id<Tone;++id)if(id==Color||id>=Swatch)
					{
						const auto& b=*scene.impl_->buttons[id];const auto& cell=candidate.buttons[id].boundsDip;
						const double cx=(cell.left+cell.right)/2,cy=(cell.top+cell.bottom)/2;
						if(id>=Swatch&&(cy<candidate.extensionClip.top||cy>=candidate.extensionClip.bottom))continue;
						const auto color=EditorPixelRgb(pixels,receipt,static_cast<int>(cx*candidate.scale),static_cast<int>(cy*candidate.scale));
						const auto expected=id==Color?ShapeRgbToUiColor(candidate.snapshot->object->style.stroke?candidate.snapshot->object->style.strokeRgb:candidate.snapshot->object->style.fillRgb):GetPresetColor(static_cast<BarThemePresetColorEnum>(id-Swatch));
						if(id>=Swatch&&b.state->state==BarWidgetState::Selected)glyph(id,false,RGB(255,255,255));
						else
						{
							const bool rgb=color[0]>=0&&std::abs(color[0]-GetRValue(expected))<14&&std::abs(color[1]-GetGValue(expected))<14&&std::abs(color[2]-GetBValue(expected))<14;
							if(!rgb)report<<"COLOR id="<<id<<" x="<<cx*candidate.scale<<" y="<<cy*candidate.scale<<" actual="<<color[0]<<','<<color[1]<<','<<color[2]<<" expected="<<int(GetRValue(expected))<<','<<int(GetGValue(expected))<<','<<int(GetBValue(expected))<<'\n';
							expect(rgb,"actual swatch center keeps its theme RGB color and never aliases the gray frame brush");
						}
					}
				}

				if(scale==1&&std::wstring(language)==L"zh-CN"&&scene.impl_->colorOpen.load()&&!pixels.empty())
				{
					const auto image=std::filesystem::path(L"Build/shape-p34-ui")/(L"editor-"+std::to_wstring(presetIndex)+(dark?L"-dark.png":L"-light.png"));
					expect(SaveEditorPng(image,pixels.data(),receipt.cx,receipt.cy,receipt.cx*4),"production editor source emits PNG evidence");
				}return hr;
			};
			expect(SUCCEEDED(present()),"editor shared draw GetDC ReleaseDC EndDraw transaction succeeds");scene.CommitPresented();
			expect(nontransparent>100,"actual emitted editor bitmap contains controls");
			expect(!scene.Hit({work.right-1,work.top+1}).occupied,"blank canvas stays transparent and cannot reselect old objects");
			for(const auto& h:snapshot->handles)
			{
				POINT screen{snapshot->canvasOriginX+static_cast<LONG>(h.position.x),snapshot->canvasOriginY+static_cast<LONG>(h.position.y)};
				const auto hit=scene.Hit(screen);expect(hit.occupied,"every current handle can receive input");
				if(hit.handle!=h.id)continue; // 相邻44DIP区域按优先级选最近点。
				actions.clear();scene.Pointer(WM_LBUTTONDOWN,screen,true);scene.Pointer(WM_MOUSEMOVE,{screen.x+7,screen.y-4},true);scene.Pointer(WM_LBUTTONUP,{screen.x+7,screen.y-4},true);
				expect(actions.size()==3&&actions.front().kind==ShapeUiActionKind::BeginEdit&&actions.back().kind==ShapeUiActionKind::CommitEdit,"one handle drag emits begin preview and one commit");
				if(actions.size()==3)expect(actions[0].identity==actions[2].identity&&actions[2].point.x-actions[0].down.x==7,"gesture keeps observed identity and document coordinates");
				break;
			}
			const auto oldBounds=scene.Bounds();snapshot=std::make_shared<ShapeUiSnapshot>(*snapshot);snapshot->canvasOriginX-=50;++snapshot->identity.generation;
			scene.Configure(snapshot,work,{});now+=std::chrono::milliseconds(17);scene.Advance(now);
			expect(FAILED(scene.Present([](const auto&)->BOOL{return FALSE;})),"failed ULW remains a failure");
			const auto& old=scene.impl_->presented;expect(EqualRect(&old.bounds,&oldBounds),"failed presentation preserves old hit snapshot");
			for(int frame=0;frame<70;++frame){now+=std::chrono::milliseconds(17);scene.Advance(now);}
			expect(SUCCEEDED(present()),"new state can recover after failed ULW");scene.CommitPresented();
			const auto& row=scene.impl_->presented;const auto color=row.buttons[Color].boundsDip;
			const POINT colorPoint{row.bounds.left+static_cast<LONG>((color.left+color.right)*row.scale/2),row.bounds.top+static_cast<LONG>((color.top+color.bottom)*row.scale/2)};
			scene.Pointer(WM_LBUTTONDOWN,colorPoint);scene.Pointer(WM_LBUTTONUP,colorPoint);
			for(int frame=0;frame<70;++frame){now+=std::chrono::milliseconds(17);scene.Advance(now);}
			expect(scene.impl_->colorOpen.load(),"only approved color control opens object palette");
			expect(SUCCEEDED(present()),"object palette reuses production gradient and swatch rendering");scene.CommitPresented();
			actions.clear();scene.impl_->Activate(Swatch+2,scene.impl_->presented,{});
			expect(actions.size()==1&&actions[0].kind==ShapeUiActionKind::SetStyle&&actions[0].style.strokeRgb==0xFF8B00u&&actions[0].style.fillRgb==0xFF8B00u,
				"orange preset COLORREF maps explicitly to Shape 0xRRGGBB without swapping red and blue");
			if(!actions.empty())
			{
				snapshot=std::make_shared<ShapeUiSnapshot>(*snapshot);
				const auto restyled=RestyleShape(*snapshot->object,actions[0].style,snapshot->object->revision+1,{scale});
				expect(restyled.has_value(),"color fixture applies the same typed restyle operation as production editing");
				if(restyled)snapshot->object=*restyled;++snapshot->identity.generation;
				scene.Configure(snapshot,work,{});for(int f=0;f<70;++f){now+=std::chrono::milliseconds(17);scene.Advance(now);}
				expect(SUCCEEDED(present()),"restyling moves the existing colorSelect check to the actual object color");scene.CommitPresented();
				expect(scene.impl_->buttons[Swatch+2]->state->state==BarWidgetState::Selected&&scene.impl_->buttons[Swatch+2]->icon.pct.val==1,"selected swatch uses shared colorSelect SVG state");
			}

			actions.clear();scene.Keyboard(VK_ESCAPE,true);expect(!actions.empty()&&actions.back().kind==ShapeUiActionKind::CloseEditor,"Escape closes current editor");
			scene.ReleaseDeviceResources();expect(SUCCEEDED(scene.EnsureDeviceResources(RenderPipeline::GetDeviceEpoch())),"editor device resources rebuild");
			expect(SUCCEEDED(present()),"rebuilt resources still present actual SVG controls");
			scene.Configure({},work,{});for(int frame=0;frame<70;++frame){now+=std::chrono::milliseconds(17);scene.Advance(now);}
			expect(!scene.Visible()&&!scene.AnimationActive(),"closed editor retires and stops animation wakeups");
		}
		for(float scale:{1.5f,2.f})
		{
			++scenes;ToolSpec tool;tool.preset=PresetKind::RegularPolygon;tool.creationMode=CreationMode::CenterRadius;tool.sides=12;
			ObjectId id{};id[0]=7;ShapeStyle shapeStyle;shapeStyle.width=4;
			auto shape=MakePresetShape(tool,{70,60},{140,110},shapeStyle,id);expect(shape.has_value(),"small workspace fixture creates a regular polygon");if(!shape)continue;
			auto snapshot=std::make_shared<ShapeUiSnapshot>();snapshot->object=*shape;snapshot->identity.session=17;snapshot->identity.objectId=id;snapshot->displayScale=scale;
			snapshot->handles=BuildEditHandles(*shape,{scale});GeometryResult geometry;BuildGeometry(*shape,geometry,{scale});snapshot->bounds=geometry.bounds;
			ShapeEditorSurface scene;scene.SetActionSink([](const auto&){});const RECT work{0,0,460,420};scene.Configure(snapshot,work,{});
			for(int f=0;f<70;++f){now+=std::chrono::milliseconds(17);scene.Advance(now);}scene.CommitPresented();
			const auto c=scene.impl_->presented.buttons[Color].boundsDip;const auto b=scene.Bounds();const POINT entry{b.left+static_cast<LONG>((c.left+c.right)*scale/2),b.top+static_cast<LONG>((c.top+c.bottom)*scale/2)};
			scene.Pointer(WM_LBUTTONDOWN,entry);scene.Pointer(WM_LBUTTONUP,entry);
			for(int f=0;f<70;++f){now+=std::chrono::milliseconds(17);scene.Advance(now);}scene.CommitPresented();
			auto frame=scene.impl_->presented;expect(frame.scrollMaximum>0,"fixed-pixel high-DPI workspace creates a local expansion viewport");
			expect(frame.bounds.top+frame.toolbar.top*scale>=work.top&&frame.bounds.top+frame.extensionClip.bottom*scale<=work.bottom,
				"attribute row and extension viewport remain reachable within fixed pixel work area");
			const POINT wheel{frame.bounds.left+static_cast<LONG>((frame.extensionClip.left+frame.extensionClip.right)*scale/2),frame.bounds.top+static_cast<LONG>((frame.extensionClip.top+frame.extensionClip.bottom)*scale/2)};
			scene.Pointer(WM_MOUSEWHEEL,wheel,true,0,-1200);
			for(int f=0;f<70;++f){now+=std::chrono::milliseconds(17);scene.Advance(now);}scene.CommitPresented();frame=scene.impl_->presented;
			expect(std::abs(frame.scrollOffset-frame.scrollMaximum)<.01,"wheel makes final palette content reachable without shrinking controls");
			POINT thumb{frame.bounds.left+static_cast<LONG>((frame.scrollTrack.left+frame.scrollTrack.right)*scale/2),frame.bounds.top+static_cast<LONG>((frame.scrollThumb.top+frame.scrollThumb.bottom)*scale/2)};
			scene.Pointer(WM_LBUTTONDOWN,thumb);scene.Pointer(WM_MOUSEMOVE,{thumb.x,thumb.y-300});scene.Pointer(WM_LBUTTONUP,{thumb.x,thumb.y-300});
			for(int f=0;f<70;++f){now+=std::chrono::milliseconds(17);scene.Advance(now);}scene.CommitPresented();
			expect(scene.impl_->presented.scrollOffset<.01,"captured touch-compatible rail drag restores first parameter row");
			expect(SUCCEEDED(scene.EnsureDeviceResources(RenderPipeline::GetDeviceEpoch())),"small-work editor target initializes");
			SIZE receipt{};
			expect(SUCCEEDED(scene.Present([&](const UPDATELAYEREDWINDOWINFO& info)->BOOL{receipt=*info.psize;++commits;return TRUE;})),"small-work production viewport renders via real ULW source");
			const auto pixels=ReadEditorPixels(scene.Renderer(),receipt);
			expect(!pixels.empty(),"small-work emitted source is readable after successful present");
			unsigned visibleNumbers=0;
			for(unsigned id=Sides;id<Swatch;++id)
			{
				const auto& button=*scene.impl_->buttons[id];const auto& clip=scene.impl_->candidate.extensionClip;
				const D2D1_RECT_F dip{static_cast<float>(button.name.inhX+3),static_cast<float>(button.name.inhY+3),static_cast<float>(button.name.inhX+button.name.w.val-3),static_cast<float>(button.name.inhY+button.name.h.val-3)};
				if(dip.top<clip.top||dip.bottom>clip.bottom)continue;
				const D2D1_RECT_F rect{dip.left*scale,dip.top*scale,dip.right*scale,dip.bottom*scale};
				const POINT background{static_cast<LONG>((button.button.inhX+button.button.w.val/2)*scale),static_cast<LONG>((button.button.inhY+2)*scale)};
				const auto evidence=InspectEditorGlyph(pixels,receipt,rect,background,button.name.color.val);
				expect(evidence.pixels>=2&&evidence.peak>.30,"fixed-pixel small-work viewport has real visible side-number glyph pixels");++visibleNumbers;
			}
			expect(visibleNumbers>0,"small-work parameter viewport exposes at least one full number control");

			if(!pixels.empty())expect(SaveEditorPng(std::filesystem::path(L"Build/shape-p34-ui")/(L"editor-small-"+std::to_wstring(static_cast<int>(scale*100))+L".png"),pixels.data(),receipt.cx,receipt.cy,receipt.cx*4),"small-work fixed-pixel preview emits actual PNG evidence");
		}

		SetThemeStyleSource(nullptr);RenderPipeline::Shutdown();CoUninitialize();
		report<<"scenes="<<scenes<<" commits="<<commits<<" checks="<<checks<<" failures="<<failures<<'\n';
		std::fprintf(stderr,"Shape editor scenes=%d commits=%d checks=%d failures=%d\n",scenes,commits,checks,failures);return failures?1:0;
	}

}
