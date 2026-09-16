module;
#include "../../../IdtMain.h"
#include <d2d1_1helper.h>
#include "../../../IdtState.h"
#include "../../../IdtI18n.h"
#include "../../Drawing/Draw3/Assets/EraserGripVisual.h"
#include "../../../resource.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <crtdbg.h>
#include <wincodec.h>
#include <array>
#include <thread>
#pragma comment(lib,"windowscodecs.lib")

module Inkeys.UI.Bar;
import :Main;
import :Theme;
import Inkeys.UI.RenderPipeline;
import Inkeys.Other.Config;
import Inkeys.Text.Font;

namespace Inkeys::UI::Bar
{
	namespace
	{
		std::array<unsigned char,4> ReadEraserTestPixel(ID2D1DeviceContext* context,ID2D1Bitmap1* source,UINT x,UINT y)
		{
			ComPtr<ID2D1Bitmap1> readable;
			const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,source->GetPixelFormat());
			if(FAILED(context->CreateBitmap(source->GetPixelSize(),nullptr,0,&props,&readable)) || FAILED(readable->CopyFromBitmap(nullptr,source,nullptr)))return {};
			D2D1_MAPPED_RECT mapped{};if(FAILED(readable->Map(D2D1_MAP_OPTIONS_READ,&mapped)))return {};
			const auto* pixel=mapped.bits+y*mapped.pitch+x*4;std::array<unsigned char,4> result{pixel[0],pixel[1],pixel[2],pixel[3]};
			readable->Unmap();return result;
		}
		HRESULT SaveEraserTestPng(ID2D1DeviceContext* context,ID2D1Bitmap1* source,const std::filesystem::path& path)
		{
			ComPtr<ID2D1Bitmap1> readable;
			const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,source->GetPixelFormat());
			HRESULT hr=context->CreateBitmap(source->GetPixelSize(),nullptr,0,&props,&readable);if(FAILED(hr))return hr;
			hr=readable->CopyFromBitmap(nullptr,source,nullptr);if(FAILED(hr))return hr;
			D2D1_MAPPED_RECT map{};hr=readable->Map(D2D1_MAP_OPTIONS_READ,&map);if(FAILED(hr))return hr;
			ComPtr<IWICImagingFactory> factory;ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> encoder;
			ComPtr<IWICBitmapFrameEncode> frame;ComPtr<IPropertyBag2> options;
			hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
			if(SUCCEEDED(hr))hr=factory->CreateStream(&stream);
			if(SUCCEEDED(hr))hr=stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE);
			if(SUCCEEDED(hr))hr=factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder);
			if(SUCCEEDED(hr))hr=encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache);
			if(SUCCEEDED(hr))hr=encoder->CreateNewFrame(&frame,&options);
			if(SUCCEEDED(hr))hr=frame->Initialize(options.Get());
			const auto size=source->GetPixelSize();
			if(SUCCEEDED(hr))hr=frame->SetSize(size.width,size.height);
			WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;
			if(SUCCEEDED(hr))hr=frame->SetPixelFormat(&format);
			if(SUCCEEDED(hr))hr=frame->WritePixels(size.height,map.pitch,map.pitch*size.height,map.bits);
			if(SUCCEEDED(hr))hr=frame->Commit();if(SUCCEEDED(hr))hr=encoder->Commit();
			readable->Unmap();return hr;
		}
	}

	int RunEraserAttributeOffscreenTest()
	{
		// 此命令只初始化共享UI设备并离屏绘制，不创建或显示任何HWND。
		SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
		_CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
		const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
		if(FAILED(com))return 2;
		if(FAILED(RenderPipeline::Initialize())){CoUninitialize();return 3;}
		IdtFontFileLoader::IsLoaderInitialized();IdtFontCollectionLoader::IsLoaderInitialized();
		const std::array<UINT,2> fonts{IDR_TTF1,IDR_TTF7};
		(void)RenderPipeline::InitializeFontCollection(IdtFontFileLoader::GetLoader(),IdtFontCollectionLoader::GetLoader(),fonts);
		std::ofstream report("Build/eraser-b/offscreen-results.log");
		int failures=0;
		auto expect=[&](bool ok,const char* why){if(!ok){++failures;report<<"[EraserVisual] FAIL "<<why<<'\n';}};
		auto& owner=barUISet;owner.barButtonSet.PresetInitialization();owner.barMedia.LoadFormat();
		SetThemeStyleSource(&owner.barStyle);
		auto root=std::make_shared<BarUiSuperellipseClass>(100,430,BarMainButtonWidthDip,BarMainButtonHeightDip,3.0,
			BarButtonFrameThicknessDip,GetThemeColor(BarThemeColorEnum::Surface),GetThemeColor(BarThemeColorEnum::SurfaceFrame));
		root->enable.Initialization(true);owner.superellipseMap[BarUISetSuperellipseEnum::MainButton]=root;
		auto main=std::make_shared<BarUiShapeClass>(230,0,BarDefaultMainBarLayoutWidthDip,BarMainBarHeightDip,
			BarMainBarCornerRadiusDip,BarMainBarCornerRadiusDip,BarButtonFrameThicknessDip,
			GetThemeColor(BarThemeColorEnum::Surface),GetThemeColor(BarThemeColorEnum::SurfaceFrame));
		main->enable.Initialization(true);main->pct.SetDirect(BarMainBarFillOpacity);
		main->framePct.emplace(BarMainBarFrameOpacity);main->frameLightPct.emplace(BarMainBarFrameOpacity);
		main->frameRendering=BarUiFrameRenderingEnum::PointLight;owner.shapeMap[BarUISetShapeEnum::MainBar]=main;
		stateMode.StateModeSelect=StateModeSelectEnum::IdtEraser;owner.barState.fold=false;
		owner.barState.widgetPosition.mainBar=true;owner.barState.widgetPosition.primaryBar=false;
		const auto metrics=ResolveBarButtonVisualMetrics(BarButtonVisualLayoutKind::StandardTwoTwo);
		const std::array presets{BarButtonPresetEnum::Select,BarButtonPresetEnum::Draw,BarButtonPresetEnum::Eraser,BarButtonPresetEnum::More,BarButtonPresetEnum::Setting};
		for(size_t i=0;i<presets.size();++i)
		{
			auto* b=presets[i]==BarButtonPresetEnum::More?owner.barButtonSet.GetMoreButton():owner.barButtonSet.preset[static_cast<int>(presets[i])];
			b->button.x.SetDirect(BarButtonGapDip+metrics.buttonWidthDip/2+i*BarDefaultButtonColumnStepDip);
			b->button.y.SetDirect(BarMainBarHeightDip/2);b->button.w.SetDirect(metrics.buttonWidthDip);b->button.h.SetDirect(metrics.buttonHeightDip);
			b->name.y.SetDirect(metrics.primaryOffsetYDip);b->name.w.SetDirect(metrics.primarySlotWidthDip);b->name.h.SetDirect(metrics.primarySlotHeightDip);b->name.size.SetDirect(metrics.primaryFontSizeDip);
			b->icon.y.SetDirect(metrics.iconOffsetYDip);b->icon.SetWH(metrics.iconSizeDip,metrics.iconSizeDip);
			b->icon.w.SetDirect(b->icon.w.tar);b->icon.h.SetDirect(b->icon.h.tar);
			b->button.pct.SetDirect(presets[i]==BarButtonPresetEnum::Eraser?0.2:0);
			b->name.pct.SetDirect(1);b->icon.pct.SetDirect(1);
		}
		std::filesystem::create_directories(L"Build/eraser-b/visuals");
		const auto preferencesBeforeOpen=EraserPreferencesSnapshot();
		auto* eraserButton=owner.barButtonSet.preset[static_cast<int>(BarButtonPresetEnum::Eraser)];
		stateMode.StateModeSelect=StateModeSelectEnum::IdtPen;
		eraserButton->clickFunc();
		expect(stateMode.StateModeSelect==StateModeSelectEnum::IdtEraser && !owner.barState.eraserAttribute,"first eraser click selects tool only");
		owner.barState.drawAttribute=true;owner.barState.geometryAttribute=true;owner.barState.moreExpanded=true;
		eraserButton->clickFunc();
		expect(owner.barState.eraserAttribute && !owner.barState.drawAttribute && !owner.barState.geometryAttribute && !owner.barState.moreExpanded,"immediate second click opens eraser and closes other panels");
		std::this_thread::sleep_for(std::chrono::milliseconds(310));eraserButton->clickFunc();
		expect(!owner.barState.eraserAttribute && EraserPreferencesSnapshot()==preferencesBeforeOpen,"next toggle closes without changing eraser configuration");
		for(int scenario=0;scenario<21;++scenario)
		{
			const bool dark=(scenario%2)==0;
			const UINT dpis[]={96,144,192};const double scales[]={0.65,1.0,1.5};
			const UINT dpi=scenario<18?dpis[scenario/6]:192;
			const double ui=scenario<18?scales[((scenario/2)+1)%3]:scenario==18?0.35:1.0,zoom=dpi/96.0*ui;
			expect(I18n::load(1,L"JSON",scenario==19?L"en-US":scenario==20?L"zh-TW":L"zh-CN"),"bundled translations load");
			BarUiEdgeLightingEnabled=scenario!=16;BarUiDynamicEdgeLightingEnabled=scenario!=17;
			owner.barStyle.darkStyle=dark;owner.barStyle.zoom=zoom;
			owner.barState.eraserAttribute=true;owner.barState.eraserSensitivityOpen=true;
			owner.barState.widgetPosition.mainBar=scenario!=3;
			owner.barState.widgetPosition.primaryBar=scenario==3;
			root->y.SetDirect(scenario==3?140:430);
			Inkeys::config.Drawing.Eraser.Automatic=scenario!=2;
			const UINT width=static_cast<UINT>(1000*zoom),height=static_cast<UINT>(620*zoom);
			const auto epoch=RenderPipeline::GetDeviceEpoch();
			expect(SUCCEEDED(owner.spec.EnsureDeviceResources(epoch,width,height)),"shared renderer resource setup");
			owner.spec.SetFrameZoom(zoom);owner.spec.PrepareFrameLighting(1.0/60,static_cast<int>(StateModeSelectEnum::IdtEraser),0,0,0);
			for(int f=0;f<90;++f)owner.eraserAttribute.Advance(owner,1.0/60,1,zoom,dpi,{0,0,static_cast<LONG>(width),static_cast<LONG>(height)},{0,0},0,0);
			expect(!owner.eraserAttribute.Active(),"animation settles without polling");
			owner.eraserAttribute.CommitPresented();const auto presentation=owner.eraserAttribute.PresentationSnapshot();
			auto lighting=owner.spec.SnapshotFrameLighting();
			lighting.cursorLight=D2D1::Point2F(static_cast<float>(EraserRectCenterX(presentation.geometry.automatic)*zoom),static_cast<float>(EraserRectCenterY(presentation.geometry.automatic)*zoom));
			lighting.cursorRadius=static_cast<float>(240*zoom);lighting.cursorIntensity=scenario==17?0.0f:1.0f;lighting.cursorLightVisible=scenario!=17;
			owner.spec.SetFrameLightingSnapshot(lighting);owner.spec.SetFrameCursorLightLocalGeometry(lighting.cursorLight,D2D1::SizeF(lighting.cursorRadius,lighting.cursorRadius));
			auto* dc=owner.spec.GetDeviceContext();dc->BeginDraw();dc->SetTransform(D2D1::IdentityMatrix());
			dc->Clear(dark?D2D1::ColorF(0.13f,0.14f,0.16f,1):D2D1::ColorF(0.91f,0.93f,0.95f,1));
			owner.eraserAttribute.Draw(owner.spec,dc);
			main->fill->SetDirect(GetThemeColor(BarThemeColorEnum::Surface));main->frame->SetDirect(GetThemeColor(BarThemeColorEnum::SurfaceFrame));
			DrawBarBackgroundVisual(owner.spec,dc,*main,BarUiInheritClass(main->inhX,main->inhY));
			for(const auto preset:presets)
			{
				auto* b=preset==BarButtonPresetEnum::More?owner.barButtonSet.GetMoreButton():owner.barButtonSet.preset[static_cast<int>(preset)];
				b->name.color.SetDirect(GetThemeColor(BarThemeColorEnum::TextPrimary));b->icon.color1->SetDirect(GetThemeColor(BarThemeColorEnum::TextPrimary));
				DrawBarButtonVisual(owner.spec,dc,*b,b->button.Inherit(BarUiInheritEnum::CenterFromTopLeft,*main));
			}
			expect(SUCCEEDED(dc->EndDraw()),"production panel renders successfully");
			owner.eraserAttribute.CommitPresented();
			const auto path=std::filesystem::path(L"Build/eraser-b/visuals")/(std::to_wstring(scenario)+(dark?L"-dark.png":L"-light.png"));
			expect(SUCCEEDED(SaveEraserTestPng(dc,owner.spec.GetTargetBitmap(),path)),"PNG readback succeeds");
			report<<"[EraserVisual] "<<path.string()<<" dpi="<<dpi<<" ui="<<ui<<'\n';
			for(size_t i=0;i<3;++i)
			{
				const auto rect=presentation.geometry.previews[i];
				expect(std::abs(rect.Width()*zoom-static_cast<int>(Inkeys::Drawing::Draw3::SpeedEraser::BaseSizePresets[i])*dpi/96.0)<0.001,"actual stable presented preview matches tool pixels");
				const auto pixel=ReadEraserTestPixel(dc,owner.spec.GetTargetBitmap(),static_cast<UINT>(EraserRectCenterX(rect)*zoom),static_cast<UINT>(EraserRectCenterY(rect)*zoom));
				expect(pixel[0]>=250 && pixel[1]>=250 && pixel[2]>=250,"Contact white remains opaque at all theme/DPI/UI scales");
			}
			expect(ERASER_GRIP_OPACITY==0.5f,"canvas Hover opacity remains unchanged");
			if(scenario==0)
			{
				EraserAttributeLayoutInput input;input.main={main->inhX,main->inhY,main->inhX+main->w.val,main->inhY+main->h.val};
				auto* anchor=owner.barButtonSet.preset[static_cast<int>(BarButtonPresetEnum::Eraser)];
				input.anchor={anchor->button.inhX,anchor->button.inhY,anchor->button.inhX+anchor->button.w.val,anchor->button.inhY+anchor->button.h.val};
				input.work={0,0,1000,620};const auto layout=ResolveEraserAttributeLayout(input);
				const auto circle=layout.items[2];
				const auto white=ReadEraserTestPixel(dc,owner.spec.GetTargetBitmap(),static_cast<UINT>((circle.left+circle.right)/2),static_cast<UINT>((circle.top+circle.bottom)/2));
				expect(white[0]>=250 && white[1]>=250 && white[2]>=250,"stable preview uses opaque Contact white, not Hover alpha0.5");
				auto click=[&](int id,bool cancel=false)
				{
					const auto rect=layout.items[id];ExMessage m{};m.x=static_cast<short>((rect.left+rect.right)/2);m.y=static_cast<short>((rect.top+rect.bottom)/2);
					m.message=WM_LBUTTONDOWN;m.lbutton=true;expect(owner.eraserAttribute.Pointer(owner,m),"control accepts down");
					m.message=WM_LBUTTONUP;m.lbutton=false;expect(owner.eraserAttribute.Pointer(owner,m,cancel),"control accepts up");
				};
				click(8);expect(EraserPreferencesSnapshot().sensitivity==Inkeys::Drawing::Draw3::SpeedEraser::Sensitivity::High && owner.barState.eraserSensitivityOpen,"sensitivity updates without closing menu");
				click(9);expect(owner.barState.eraserSensitivityOpen,"disabled settings does nothing");
				click(1);expect(EraserPreferencesSnapshot().baseSize==Inkeys::Drawing::Draw3::SpeedEraser::BaseSize::Small && owner.barState.eraserAttribute && !owner.barState.eraserSensitivityOpen,"outside menu click closes child then applies preset once");
				click(3,true);expect(EraserPreferencesSnapshot().baseSize==Inkeys::Drawing::Draw3::SpeedEraser::BaseSize::Small,"cancelled pointer does not select");
				click(4);expect(Inkeys::Drawing::Draw3::SpeedEraser::GetAutomaticState(EraserPreferencesSnapshot())==Inkeys::Drawing::Draw3::SpeedEraser::AutomaticState::Off && owner.barState.eraserAttribute,"body toggles only the master gate and stays open");
				click(5);expect(owner.barState.eraserSensitivityOpen && Inkeys::Drawing::Draw3::SpeedEraser::GetAutomaticState(EraserPreferencesSnapshot())==Inkeys::Drawing::Draw3::SpeedEraser::AutomaticState::Off,"arrow opens while auto is off without toggling");
				click(0);expect(owner.barState.eraserAttribute,"empty clear is disabled without closing or changing tool");
				Inkeys::config.Drawing.Eraser.MouseLeft=0;Inkeys::config.Drawing.Eraser.MouseRight=1;
				click(4);const auto restored=EraserPreferencesSnapshot();
				expect(Inkeys::Drawing::Draw3::SpeedEraser::GetAutomaticState(restored)==Inkeys::Drawing::Draw3::SpeedEraser::AutomaticState::On &&
					restored.entries[0].kind==Inkeys::Drawing::Draw3::SpeedEraser::EraserKind::Fixed &&
					restored.entries[1].kind==Inkeys::Drawing::Draw3::SpeedEraser::EraserKind::Speed,
					"reenabling master gate preserves mixed per-entry preferences");
				Inkeys::config.Drawing.Eraser.BaseDiameterDip=32;Inkeys::config.Drawing.Eraser.Sensitivity=1;
				owner.barState.eraserSensitivityOpen=true;
			}
			owner.eraserAttribute.Keyboard(owner,VK_ESCAPE,true);expect(owner.barState.eraserAttribute && !owner.barState.eraserSensitivityOpen,"Escape closes child first");
			owner.eraserAttribute.Keyboard(owner,VK_ESCAPE,true);expect(!owner.barState.eraserAttribute,"Escape then closes panel");
			for(int f=0;f<90;++f)owner.eraserAttribute.Advance(owner,1.0/60,1,zoom,dpi,{0,0,static_cast<LONG>(width),static_cast<LONG>(height)},{0,0},0,0);
			owner.eraserAttribute.CommitPresented();
			const auto regions=owner.eraserAttribute.PresentedRegions();expect(regions[0].right==regions[0].left,"closed panel has no hit region");
		}
		// 实际组件逐帧渲染：所有PNG与CSV都来自同一Advance/Draw/CommitPresented路径。
		{
			I18n::load(1,L"JSON",L"zh-CN");owner.barStyle.darkStyle=true;owner.barStyle.zoom=1;
			BarUiEdgeLightingEnabled=true;BarUiDynamicEdgeLightingEnabled=true;BarUiAnimationEnabled=true;
			root->y.SetDirect(430);owner.barState.widgetPosition.primaryBar=false;owner.barState.widgetPosition.mainBar=true;
			const auto epoch=RenderPipeline::GetDeviceEpoch();expect(SUCCEEDED(owner.spec.EnsureDeviceResources(epoch,1000,620)),"animation target setup");
			owner.spec.SetFrameZoom(1);auto* dc=owner.spec.GetDeviceContext();
			std::filesystem::create_directories(L"Build/eraser-b/frames");
			std::ofstream trace("Build/eraser-b/frames.csv");trace<<"case,frame,panel_scale,menu_scale,panel_alpha,menu_alpha,panel_x,panel_y,panel_w,panel_h,menu_x,menu_y,menu_w,menu_h,bounds_l,bounds_t,bounds_r,bounds_b\n";
			auto tick=[&](const std::string& name,int f,double dt=1.0/60,bool save=false)
			{
				owner.eraserAttribute.Advance(owner,dt,BarUiAnimationSpeedRate,1,96,{0,0,1000,620},{0,0},0,0);
				auto light=owner.spec.SnapshotFrameLighting();light.primaryLight={330,470};light.primaryRadius=480;light.primaryLightVisible=true;
				light.cursorLight={440,340};light.cursorRadius=240;light.cursorIntensity=1;light.cursorLightVisible=true;
				owner.spec.SetFrameLightingSnapshot(light);owner.spec.SetFrameCursorLightLocalGeometry(light.cursorLight,D2D1::SizeF(240,240));
				dc->BeginDraw();dc->SetTransform(D2D1::IdentityMatrix());dc->Clear(D2D1::ColorF(0.13f,0.14f,0.16f,1));
				owner.eraserAttribute.Draw(owner.spec,dc);
				main->fill->SetDirect(GetThemeColor(BarThemeColorEnum::Surface));main->frame->SetDirect(GetThemeColor(BarThemeColorEnum::SurfaceFrame));
				DrawBarBackgroundVisual(owner.spec,dc,*main,BarUiInheritClass(main->inhX,main->inhY));
				for(const auto preset:presets)
				{
					auto* b=preset==BarButtonPresetEnum::More?owner.barButtonSet.GetMoreButton():owner.barButtonSet.preset[static_cast<int>(preset)];
					b->name.color.SetDirect(GetThemeColor(BarThemeColorEnum::TextPrimary));b->icon.color1->SetDirect(GetThemeColor(BarThemeColorEnum::TextPrimary));
					DrawBarButtonVisual(owner.spec,dc,*b,b->button.Inherit(BarUiInheritEnum::CenterFromTopLeft,*main));
				}
				expect(SUCCEEDED(dc->EndDraw()),"animation frame draws without D2D error");owner.eraserAttribute.CommitPresented();
				const auto s=owner.eraserAttribute.PresentationSnapshot();const auto b=owner.eraserAttribute.Bounds();
				const auto p=s.geometry.panel,m=s.geometry.menu;
				trace<<name<<','<<f<<','<<s.panelPose.scale<<','<<s.menuPose.scale<<','<<s.panelOpacity<<','<<s.menuOpacity<<','<<p.left<<','<<p.top<<','<<p.Width()<<','<<p.Height()<<','<<m.left<<','<<m.top<<','<<m.Width()<<','<<m.Height()<<','<<b.left<<','<<b.top<<','<<b.right<<','<<b.bottom<<'\n';
				auto covered=[&](EraserAttributeRect r){return b.left<=r.left && b.top<=r.top && b.right>=r.right && b.bottom>=r.bottom;};
				if(s.panelVisible)expect(covered(p),"Bounds covers presented shell including geometric overshoot");
				if(s.menuVisible)expect(covered(m),"Bounds covers current child shell");
				expect(s.panelOpacity>=0 && s.panelOpacity<=1 && s.menuOpacity>=0 && s.menuOpacity<=1,"rendered alpha bounded separately from geometry");
				if(save)expect(SUCCEEDED(SaveEraserTestPng(dc,owner.spec.GetTargetBitmap(),std::filesystem::path(L"Build/eraser-b/frames")/(std::wstring(name.begin(),name.end())+L"-"+std::to_wstring(f)+L".png"))),"animation PNG readback");
				return s;
			};
			owner.eraserAttribute.Close(owner);for(int f=0;f<60;++f)tick("settle",f);
			owner.barState.eraserAttribute=true;double openPeak=0,menuOpenPeak=0,closePeak=0,menuClosePeak=0;bool mainOutsidePixel=false,menuOutsidePixel=false;
			EraserAttributePresentation stable;
			for(int f=0;f<36;++f)
			{
				const auto s=tick("panel-open",f,1.0/60,true);openPeak=(std::max)(openPeak,s.panelPose.scale);
				if(s.panelPose.scale>1.01 && s.geometry.panel.top+2<300)
				{
					const auto pixel=ReadEraserTestPixel(dc,owner.spec.GetTargetBitmap(),static_cast<UINT>(EraserRectCenterX(s.geometry.panel)),static_cast<UINT>(s.geometry.panel.top+2));
					mainOutsidePixel|=std::abs(static_cast<int>(pixel[0])-41)>2 || std::abs(static_cast<int>(pixel[1])-36)>2 || std::abs(static_cast<int>(pixel[2])-33)>2;
				}
				stable=s;
			}
			expect(openPeak>1.01 && stable.panelPose.scale==1 && mainOutsidePixel,"main opening overshoot exists in actual pixels outside final rectangle");
			// 没有Down票据的旧Up，即使当前中央Clear在命中点，也不能形成命令。
			ExMessage oldUp{};oldUp.message=WM_LBUTTONUP;oldUp.x=static_cast<short>(EraserRectCenterX(stable.geometry.items[0]));oldUp.y=static_cast<short>(EraserRectCenterY(stable.geometry.items[0]));
			owner.eraserAttribute.Pointer(owner,oldUp);expect(owner.barState.eraserAttribute && ResolveEraserAttributeRelease(-1,0,true,false)==-1,"opening release cannot trigger center Clear");
			owner.barState.eraserSensitivityOpen=true;
			for(int f=0;f<36;++f)
			{
				const auto s=tick("menu-open",f,1.0/60,true);menuOpenPeak=(std::max)(menuOpenPeak,s.menuPose.scale);
				if(s.menuPose.scale>1.01 && s.geometry.menu.top+2<205)
				{
					const auto pixel=ReadEraserTestPixel(dc,owner.spec.GetTargetBitmap(),static_cast<UINT>(EraserRectCenterX(s.geometry.menu)),static_cast<UINT>(s.geometry.menu.top+2));
					menuOutsidePixel|=std::abs(static_cast<int>(pixel[0])-41)>2 || std::abs(static_cast<int>(pixel[1])-36)>2 || std::abs(static_cast<int>(pixel[2])-33)>2;
				}
			}
			expect(menuOpenPeak>1.01 && menuOutsidePixel,"child opening overshoot is visible, not clipped by final menu bounds");
			owner.barState.eraserSensitivityOpen=false;
			for(int f=0;f<36;++f){const auto s=tick("menu-close",f,1.0/60,true);menuClosePeak=(std::max)(menuClosePeak,s.menuPose.scale);if(f<20)expect(s.menuVisible,"closing child remains visible during early Back phase");}
			expect(menuClosePeak>1.01 && !owner.eraserAttribute.PresentationSnapshot().menuVisible,"child close overshoots then ends");
			owner.barState.eraserSensitivityOpen=true;for(int f=0;f<45;++f)tick("child-ready",f);
			owner.eraserAttribute.Close(owner);
			for(int f=0;f<40;++f)
			{
				const auto s=tick("both-close",f,1.0/60,true);closePeak=(std::max)(closePeak,s.panelPose.scale);
				if(f<20)expect(s.panelVisible && s.menuVisible,"parent and child both finish their closing transition");
			}
			expect(closePeak>1.01 && !owner.eraserAttribute.PresentationSnapshot().panelVisible,"parent close has geometric rebound and no leftover hit region");
			for(int f=0;f<45;++f)tick("closed-idle",f);expect(!owner.eraserAttribute.Active(),"settled closed component sleeps");
			owner.barState.eraserAttribute=true;for(int f=0;f<7;++f)tick("reverse-start",f,1.0/60,true);
			const auto before=owner.eraserAttribute.PresentationSnapshot();owner.barState.eraserAttribute=false;const auto after=tick("reverse-latch",0,0,true);
			expect(std::abs(before.panelPose.scale-after.panelPose.scale)<0.000001 && std::abs(before.geometry.panel.top-after.geometry.panel.top)<0.000001,"halfway reversal does not reset to compact or target");
			for(int f=0;f<6;++f)tick("reverse-close",f,1.0/60,true);owner.barState.eraserAttribute=true;
			for(int f=0;f<40;++f)tick("reverse-open",f,1.0/60,true);
			owner.barState.eraserSensitivityOpen=true;for(int f=0;f<8;++f)tick("child-reverse-start",f);
			const auto beforeChild=owner.eraserAttribute.PresentationSnapshot();owner.barState.eraserSensitivityOpen=false;const auto afterChild=tick("child-reverse-latch",0,0);
			expect(std::abs(beforeChild.menuPose.scale-afterChild.menuPose.scale)<0.000001,"child reversal retains current geometry");
			for(int f=0;f<40;++f)tick("child-reverse-close",f);
			// 控件内跨动作区不重播整体Hover；两种方向都只执行Down所属动作。
			for(int source=0;source<3;++source)
			{
				const auto s=owner.eraserAttribute.PresentationSnapshot();auto message=[&](UINT kind,int item,bool held){ExMessage m{};m.message=static_cast<USHORT>(kind);m.x=static_cast<short>(EraserRectCenterX(s.geometry.items[item]));m.y=static_cast<short>(EraserRectCenterY(s.geometry.items[item]));m.lbutton=held;if(source)m.wheel=SHRT_MIN;return m;};
				owner.barState.eraserSensitivityOpen=false;const auto initial=Inkeys::Drawing::Draw3::SpeedEraser::GetAutomaticState(EraserPreferencesSnapshot());
				owner.eraserAttribute.Pointer(owner,message(WM_LBUTTONDOWN,4,true),false,source!=0);owner.eraserAttribute.Pointer(owner,message(WM_MOUSEMOVE,5,true),false,source!=0);owner.eraserAttribute.Pointer(owner,message(WM_LBUTTONUP,5,false),false,source!=0);
				expect(Inkeys::Drawing::Draw3::SpeedEraser::GetAutomaticState(EraserPreferencesSnapshot())!=initial && !owner.barState.eraserSensitivityOpen,"normalized Mouse/Pen/Touch body-to-arrow executes body only");
				const auto afterBody=Inkeys::Drawing::Draw3::SpeedEraser::GetAutomaticState(EraserPreferencesSnapshot());std::this_thread::sleep_for(std::chrono::milliseconds(310));
				owner.eraserAttribute.Pointer(owner,message(WM_LBUTTONDOWN,5,true),false,source!=0);owner.eraserAttribute.Pointer(owner,message(WM_MOUSEMOVE,4,true),false,source!=0);owner.eraserAttribute.Pointer(owner,message(WM_LBUTTONUP,4,false),false,source!=0);
				expect(Inkeys::Drawing::Draw3::SpeedEraser::GetAutomaticState(EraserPreferencesSnapshot())==afterBody && owner.barState.eraserSensitivityOpen,"normalized arrow-to-body executes menu only");
				for(int f=0;f<45;++f)tick("input-settle",f);
			}
			const auto beforeLeave=Inkeys::Drawing::Draw3::SpeedEraser::GetAutomaticState(EraserPreferencesSnapshot());
			const auto leaveFrame=owner.eraserAttribute.PresentationSnapshot();ExMessage leave{};leave.x=static_cast<short>(EraserRectCenterX(leaveFrame.geometry.items[4]));leave.y=static_cast<short>(EraserRectCenterY(leaveFrame.geometry.items[4]));leave.message=WM_LBUTTONDOWN;leave.lbutton=true;
			owner.eraserAttribute.Pointer(owner,leave);owner.eraserAttribute.ResetPointerFeedback();leave.message=WM_LBUTTONUP;leave.lbutton=false;owner.eraserAttribute.Pointer(owner,leave);
			expect(Inkeys::Drawing::Draw3::SpeedEraser::GetAutomaticState(EraserPreferencesSnapshot())==beforeLeave,"window leave cancels stale press ownership");
			owner.barState.eraserSensitivityOpen=true;for(int f=0;f<45;++f)tick("swap-ready",f);
			owner.barState.widgetPosition.mainBar=false;owner.barState.widgetPosition.primaryBar=true;
			for(int f=0;f<65;++f){root->y.SetDirect(430-290*(std::min)(1.0,f/24.0));tick("side-swap",f,1.0/60,f%3==0);}
			const auto reversed=owner.eraserAttribute.PresentationSnapshot();expect(reversed.geometry.reversed && reversed.geometry.below && reversed.panelPose.scale==1,"side swap settles with upright group order");
			BarUiAnimationEnabled=false;owner.eraserAttribute.Close(owner);const auto disabled=tick("disabled-close",0,0,true);expect(!disabled.panelVisible && !disabled.menuVisible,"disabled animation closes immediately");
			owner.barState.eraserAttribute=true;owner.barState.eraserSensitivityOpen=true;const auto direct=tick("disabled-open",0,0,true);expect(direct.panelPose.scale==1 && direct.menuPose.scale==1 && direct.panelOpacity==1,"disabled animation opens at true size");
			BarUiAnimationEnabled=true;BarUiAnimationSpeedRate=2;owner.eraserAttribute.Close(owner);for(int f=0;f<15;++f)tick("double-speed-close",f);expect(!owner.eraserAttribute.PresentationSnapshot().panelVisible,"speed setting affects both surfaces");BarUiAnimationSpeedRate=1;
			report<<"[EraserFrames] panelOpenPeak="<<openPeak<<" menuOpenPeak="<<menuOpenPeak<<" panelClosePeak="<<closePeak<<" menuClosePeak="<<menuClosePeak<<" pixelOvershoot="<<mainOutsidePixel<<','<<menuOutsidePixel<<'\n';
		}
		// 新SVG在真实标准图标尺寸和三种状态下栅格化，不使用字体绘制A。
		for(bool dark:{false,true})for(UINT dpi:{96u,144u,192u})for(double ui:{0.65,1.0,1.5})
		{
			owner.barStyle.darkStyle=dark;const double zoom=dpi/96.0*ui;
			expect(SUCCEEDED(owner.spec.EnsureDeviceResources(RenderPipeline::GetDeviceEpoch(),static_cast<UINT>(250*zoom),static_cast<UINT>(90*zoom))),"SVG state target setup");owner.spec.SetFrameZoom(zoom);
			auto* dc=owner.spec.GetDeviceContext();dc->BeginDraw();dc->SetTransform(D2D1::IdentityMatrix());dc->Clear(dark?D2D1::ColorF(0.13f,0.14f,0.16f,1):D2D1::ColorF(0.91f,0.93f,0.95f,1));
			for(int state=0;state<3;++state)
			{
				BarButtonClass button;button.hide=false;button.button.Initialization(0,0,BarButtonTwoSideDip,BarButtonTwoSideDip,BarButtonCornerRadiusDip,BarButtonCornerRadiusDip,BarButtonFrameThicknessDip,GetThemeColor(BarThemeColorEnum::PressedFill),GetThemeColor(BarThemeColorEnum::SurfaceFrame));
				button.button.enable.Initialization(true);button.button.framePct.emplace(0);button.button.frameLightPct.emplace(0);
				button.icon.Initialization(0,metrics.iconOffsetYDip,GetThemeColor(BarThemeColorEnum::TextPrimary),std::nullopt);button.icon.InitializationFromResource(L"UI",L"barAutoEraser");button.icon.enable.Initialization(true);button.icon.SetWH(metrics.iconSizeDip,metrics.iconSizeDip);button.icon.w.SetDirect(button.icon.w.tar);button.icon.h.SetDirect(button.icon.h.tar);
				button.name.Initialization(0,metrics.primaryOffsetYDip,metrics.primarySlotWidthDip,metrics.primarySlotHeightDip,state==0?L"常规":state==1?L"选中":L"禁用",metrics.primaryFontSizeDip,GetThemeColor(BarThemeColorEnum::TextPrimary));button.name.enable.Initialization(true);
				RetargetBarButtonInteractionVisual(button,true,state!=2,state==1,0.0);
				const BarUiAnimationAdvanceContextClass instant{0,1,false,true};BarUiAdvanceAnimation(button.button.pct,instant);BarUiAdvanceAnimation(*button.button.fill,instant);BarUiAdvanceAnimation(button.icon.pct,instant);BarUiAdvanceAnimation(*button.icon.color1,instant);BarUiAdvanceAnimation(button.name.pct,instant);BarUiAdvanceAnimation(button.name.color,instant);
				DrawBarButtonVisual(owner.spec,dc,button,BarUiInheritClass(10+state*80,10));
				expect(button.icon.cacheBitmap && button.icon.svg.GetVal().find(L"<text")==std::wstring::npos,"A icon rasterizes from path in normal/selected/disabled state");
			}
			expect(SUCCEEDED(dc->EndDraw()),"SVG states draw");
			const auto file=std::filesystem::path(L"Build/eraser-b/visuals")/(L"icon-"+std::to_wstring(dpi)+L"-"+std::to_wstring(static_cast<int>(ui*100))+(dark?L"-dark.png":L"-light.png"));expect(SUCCEEDED(SaveEraserTestPng(dc,owner.spec.GetTargetBitmap(),file)),"icon states PNG");
		}
		owner.spec.DiscardDeviceResources();RenderPipeline::Shutdown();CoUninitialize();
		report<<"[EraserVisual] failures="<<failures<<'\n';return failures?1:0;
	}
}
