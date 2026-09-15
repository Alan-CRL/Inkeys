module;
#include "../../../IdtMain.h"
#include <d2d1_1helper.h>
#include "../../../IdtState.h"
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
		std::ofstream report("Build/eraser-attribute-offscreen-results.log");
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
		std::filesystem::create_directories(L"Build/eraser-attribute-visuals");
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
		for(int scenario=0;scenario<6;++scenario)
		{
			const bool dark=(scenario%2)==0;const UINT dpi=scenario<2?96:scenario<4?144:192;
			const double ui=scenario<4?1.0:0.65,zoom=dpi/96.0*ui;
			owner.barStyle.darkStyle=dark;owner.barStyle.zoom=zoom;
			owner.barState.eraserAttribute=true;owner.barState.eraserSensitivityOpen=true;
			owner.barState.widgetPosition.mainBar=scenario!=3;
			owner.barState.widgetPosition.primaryBar=scenario==3;
			root->y.SetDirect(scenario==3?140:430);
			Inkeys::config.Drawing.Eraser.MouseLeft=scenario==2?0:1;
			const UINT width=static_cast<UINT>(1000*zoom),height=static_cast<UINT>(620*zoom);
			const auto epoch=RenderPipeline::GetDeviceEpoch();
			expect(SUCCEEDED(owner.spec.EnsureDeviceResources(epoch,width,height)),"shared renderer resource setup");
			owner.spec.SetFrameZoom(zoom);owner.spec.PrepareFrameLighting(1.0/60,static_cast<int>(StateModeSelectEnum::IdtEraser),0,0,0);
			for(int f=0;f<90;++f)owner.eraserAttribute.Advance(owner,1.0/60,1,zoom,dpi,{0,0,static_cast<LONG>(width),static_cast<LONG>(height)},{0,0},0,0);
			expect(!owner.eraserAttribute.Active(),"animation settles without polling");
			auto* dc=owner.spec.GetDeviceContext();dc->BeginDraw();dc->SetTransform(D2D1::IdentityMatrix());
			dc->Clear(dark?D2D1::ColorF(0.13f,0.14f,0.16f,1):D2D1::ColorF(0.91f,0.93f,0.95f,1));
			main->fill->SetDirect(GetThemeColor(BarThemeColorEnum::Surface));main->frame->SetDirect(GetThemeColor(BarThemeColorEnum::SurfaceFrame));
			DrawBarBackgroundVisual(owner.spec,dc,*main,BarUiInheritClass(main->inhX,main->inhY));
			for(const auto preset:presets)
			{
				auto* b=preset==BarButtonPresetEnum::More?owner.barButtonSet.GetMoreButton():owner.barButtonSet.preset[static_cast<int>(preset)];
				b->name.color.SetDirect(GetThemeColor(BarThemeColorEnum::TextPrimary));b->icon.color1->SetDirect(GetThemeColor(BarThemeColorEnum::TextPrimary));
				DrawBarButtonVisual(owner.spec,dc,*b,b->button.Inherit(BarUiInheritEnum::CenterFromTopLeft,*main));
			}
			owner.eraserAttribute.Draw(owner.spec,dc);expect(SUCCEEDED(dc->EndDraw()),"production panel renders successfully");
			owner.eraserAttribute.CommitPresented();
			const auto path=std::filesystem::path(L"Build/eraser-attribute-visuals")/(std::to_wstring(scenario)+(dark?L"-dark.png":L"-light.png"));
			expect(SUCCEEDED(SaveEraserTestPng(dc,owner.spec.GetTargetBitmap(),path)),"PNG readback succeeds");
			report<<"[EraserVisual] "<<path.string()<<" dpi="<<dpi<<" ui="<<ui<<'\n';
			if(scenario==0)
			{
				EraserAttributeLayoutInput input;input.main={main->inhX,main->inhY,main->inhX+main->w.val,main->inhY+main->h.val};
				auto* anchor=owner.barButtonSet.preset[static_cast<int>(BarButtonPresetEnum::Eraser)];
				input.anchor={anchor->button.inhX,anchor->button.inhY,anchor->button.inhX+anchor->button.w.val,anchor->button.inhY+anchor->button.h.val};
				input.work={0,0,1000,620};const auto layout=ResolveEraserAttributeLayout(input);
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
				click(4);expect(Inkeys::Drawing::Draw3::SpeedEraser::GetAutomaticState(EraserPreferencesSnapshot())==Inkeys::Drawing::Draw3::SpeedEraser::AutomaticState::Off && owner.barState.eraserAttribute,"body toggles only kind and stays open");
				click(5);expect(owner.barState.eraserSensitivityOpen && Inkeys::Drawing::Draw3::SpeedEraser::GetAutomaticState(EraserPreferencesSnapshot())==Inkeys::Drawing::Draw3::SpeedEraser::AutomaticState::Off,"arrow opens while auto is off without toggling");
				click(0);expect(owner.barState.eraserAttribute,"empty clear is disabled without closing or changing tool");
				Inkeys::config.Drawing.Eraser.MouseLeft=0;Inkeys::config.Drawing.Eraser.MouseRight=1;
				click(4);expect(Inkeys::Drawing::Draw3::SpeedEraser::GetAutomaticState(EraserPreferencesSnapshot())==Inkeys::Drawing::Draw3::SpeedEraser::AutomaticState::On,"mixed global click unifies all five");
				Inkeys::config.Drawing.Eraser.BaseDiameterDip=32;Inkeys::config.Drawing.Eraser.Sensitivity=1;
				owner.barState.eraserSensitivityOpen=true;
			}
			owner.eraserAttribute.Keyboard(owner,VK_ESCAPE,true);expect(owner.barState.eraserAttribute && !owner.barState.eraserSensitivityOpen,"Escape closes child first");
			owner.eraserAttribute.Keyboard(owner,VK_ESCAPE,true);expect(!owner.barState.eraserAttribute,"Escape then closes panel");
			for(int f=0;f<90;++f)owner.eraserAttribute.Advance(owner,1.0/60,1,zoom,dpi,{0,0,static_cast<LONG>(width),static_cast<LONG>(height)},{0,0},0,0);
			owner.eraserAttribute.CommitPresented();
			const auto regions=owner.eraserAttribute.PresentedRegions();expect(regions[0].right==regions[0].left,"closed panel has no hit region");
		}
		owner.spec.DiscardDeviceResources();RenderPipeline::Shutdown();CoUninitialize();
		report<<"[EraserVisual] failures="<<failures<<'\n';return failures?1:0;
	}
}
