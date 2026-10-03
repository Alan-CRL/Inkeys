module;
#include "../../../IdtMain.h"
#include <d2d1_1helper.h>
#include "../../../IdtState.h"
#include "../../../IdtI18n.h"
#include "../../../IdtI18nKeys.g.h"
#include "../../Drawing/Draw3/Assets/EraserGripVisual.h"
#include "../../Drawing/Draw3/Draw3.Product.h"
#include "../../../resource.h"
#include <d3d11.h>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <crtdbg.h>
#include <wincodec.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <span>
#include <thread>
#include <vector>
#include <bit>
#include <limits>
#include "Bar.PresentationProbe.h"
#pragma comment(lib,"windowscodecs.lib")

module Inkeys.UI.Bar;
import :Main;
import :Scene;
import :Theme;
import Inkeys.UI.RenderPipeline;
import Inkeys.UI.PageControl;
import Inkeys.Other.Config;
import Inkeys.Text.Font;

extern const double BarGeometryAttributeShapeButtonSize;
extern const double BarThicknessTooltipPadding;
extern const double BarThicknessTooltipCloseReserve;
extern const double BarThicknessTooltipTitleFontSize;
extern const double BarThicknessTooltipBodyFontSize;
extern const double BarThicknessTooltipLineGap;
extern const double BarColorPickerPanelWidth;

namespace Inkeys::UI::Bar
{
	namespace
	{
		std::vector<unsigned char> ReadSvgProofPixels(ID2D1DeviceContext* context, ID2D1Bitmap1* source)
		{
			if (!context || !source) return {};
			const auto size = source->GetPixelSize();
			const std::uint64_t pixelsCount = std::uint64_t{size.width} * size.height;
			if (size.width == 0 || size.height == 0 || pixelsCount > 64 * 1024 * 1024 / 4) return {};
			const auto bytes = pixelsCount * 4;
			ComPtr<ID2D1Bitmap1> readable;
			const auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, source->GetPixelFormat());
			if (FAILED(context->CreateBitmap(size, nullptr, 0, &props, &readable)) || FAILED(readable->CopyFromBitmap(nullptr, source, nullptr))) return {};
			D2D1_MAPPED_RECT mapped{};
			if (FAILED(readable->Map(D2D1_MAP_OPTIONS_READ, &mapped))) return {};
			std::vector<unsigned char> pixels(static_cast<std::size_t>(bytes));
			for (UINT y = 0; y < size.height; ++y) std::memcpy(pixels.data() + std::size_t{y} * size.width * 4, mapped.bits + std::size_t{y} * mapped.pitch, std::size_t{size.width} * 4);
			readable->Unmap(); return pixels;
		}
		std::array<unsigned char,4> ReadEraserTestPixel(ID2D1DeviceContext* context,ID2D1Bitmap1* source,UINT x,UINT y)
		{
			// 离屏几何可能越出目标；断言读取应返回失败像素，不能访问映射外内存。
			if (!context || !source) return {};
			const auto size = source->GetPixelSize();
			if (x >= size.width || y >= size.height) return {};
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
		const std::array<UINT,4> fonts{IDR_TTF1,IDR_TTF7,IDR_TTF3,IDR_TTF8};
		const HRESULT fontCollectionHr=RenderPipeline::InitializeFontCollection(
			IdtFontFileLoader::GetLoader(),IdtFontCollectionLoader::GetLoader(),fonts);
		std::ofstream report("Build/eraser-b/offscreen-results.log");
		int failures=0;
		auto expect=[&](bool ok,const char* why){if(!ok){++failures;report<<"[EraserVisual] FAIL "<<why<<'\n';}};
		expect(SUCCEEDED(fontCollectionHr),"SC/TC font collection initializes");
		auto& owner=barUISet;
		expect(I18n::load(1,L"JSON",L"zh-CN"),"default bundled translation loads");
		owner.spec.ConfigureLocalizedTypography();
		owner.barButtonSet.PresetInitialization();owner.barMedia.LoadFormat();
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
		const auto twoOneMetrics=ResolveBarButtonVisualMetrics(BarButtonVisualLayoutKind::StandardTwoOne);
		const std::array presets{BarButtonPresetEnum::Select,BarButtonPresetEnum::Draw,BarButtonPresetEnum::Eraser,BarButtonPresetEnum::More,BarButtonPresetEnum::Setting};
		const std::array<const char*,5> localizedPresetLabelKeys{
			I18nKey.UI.Bar.MainButtons.SelectLabel,
			I18nKey.UI.Bar.MainButtons.DrawLabel,
			I18nKey.UI.Bar.MainButtons.EraserLabel,
			I18nKey.UI.Bar.MainButtons.MoreLabel,
			I18nKey.UI.Bar.MainButtons.SettingsLabel};
		auto updateLocalizedPresetLabels=[&]()
		{
			for(size_t i=0;i<presets.size();++i)
			{
				auto* b=presets[i]==BarButtonPresetEnum::More?owner.barButtonSet.GetMoreButton():owner.barButtonSet.preset[static_cast<int>(presets[i])];
				const auto label=IW(localizedPresetLabelKeys[i]);b->name.content.SetVal(label);b->name.content.SetTar(label);
			}
		};
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
		struct LocalizedBarCase{const wchar_t* language;const wchar_t* fileTag;const wchar_t* family;const wchar_t* locale;};
		const std::array localizedBarCases{
			LocalizedBarCase{L"zh-CN",L"zh-cn",L"HarmonyOS Sans SC",L"zh-cn"},
			LocalizedBarCase{L"zh-TW",L"zh-tw",L"HarmonyOS Sans TC",L"zh-tw"},
			LocalizedBarCase{L"en-US",L"en-us",L"HarmonyOS Sans SC",L"en-us"}};
		const std::array<const char*,20> fixedLabelKeys{
			I18nKey.UI.Bar.MainButtons.SelectLabel,I18nKey.UI.Bar.MainButtons.MoveLabel,
			I18nKey.UI.Bar.MainButtons.DrawLabel,I18nKey.UI.Bar.MainButtons.LaserLabel,
			I18nKey.UI.Bar.MainButtons.HighlighterLabel,I18nKey.UI.Bar.MainButtons.HardPenLabel,
			I18nKey.UI.Bar.MainButtons.SoftPenLabel,I18nKey.UI.Bar.MainButtons.EraserLabel,
			I18nKey.UI.Bar.MainButtons.AreaEraserLabel,I18nKey.UI.Bar.MainButtons.GeometryLabel,
			I18nKey.UI.Bar.MainButtons.RectangleLabel,I18nKey.UI.Bar.MainButtons.StraightLineLabel,
			I18nKey.UI.Bar.MainButtons.UndoLabel,I18nKey.UI.Bar.MainButtons.ClearLabel,
			I18nKey.UI.Bar.MainButtons.WhiteboardLabel,I18nKey.UI.Bar.MainButtons.CloseWhiteboardLabel,
			I18nKey.UI.Bar.MainButtons.FreezeLabel,I18nKey.UI.Bar.MainButtons.EndPresentationLabel,
			I18nKey.UI.Bar.MainButtons.SettingsLabel,I18nKey.UI.Bar.MainButtons.MoreLabel};
		const std::array<const char*,7> penMenuLabelKeys{
			I18nKey.UI.Bar.DrawAttributes.BrushLabel,I18nKey.UI.Bar.DrawAttributes.LaserLabel,
			I18nKey.UI.Bar.DrawAttributes.HighlighterLabel,I18nKey.UI.Bar.DrawAttributes.HardPenLabel,
			I18nKey.UI.Bar.DrawAttributes.SoftPenLabel,I18nKey.UI.Bar.DrawAttributes.FreeLineLabel,
			I18nKey.UI.Bar.DrawAttributes.AnnotationLineLabel};
		for(const auto& language:localizedBarCases)
		{
			expect(I18n::load(1,L"JSON",language.language),"bar language loads for layout audit");
			owner.spec.ConfigureLocalizedTypography();updateLocalizedPresetLabels();
			expect(owner.spec.GetFontFamily()==language.family && owner.spec.GetTextLocale()==language.locale,
				"localized font family and DWrite locale match language");
			for(const auto* key:fixedLabelKeys)
				expect(owner.spec.MeasureText(IW(key),metrics.primaryFontSizeDip,DWRITE_FONT_WEIGHT_BOLD).width<=metrics.primarySlotWidthDip+0.5f,
					"localized fixed label fits the existing 70 DIP slot");
			for(const auto* key:{I18nKey.UI.Bar.MainButtons.WhiteboardLabel,I18nKey.UI.Bar.MainButtons.FreezeLabel})
				expect(owner.spec.MeasureText(IW(key),twoOneMetrics.primaryFontSizeDip,DWRITE_FONT_WEIGHT_BOLD).width<=twoOneMetrics.primarySlotWidthDip+0.5f,
					"localized Whiteboard and Freeze labels fit the existing 37 DIP slot");
			for(const auto* key:penMenuLabelKeys)
				expect(owner.spec.MeasureText(IW(key),12.0,DWRITE_FONT_WEIGHT_NORMAL).width<=80.5f,
					"localized pen menu label fits its measured text slot");
			for(const auto* key:{I18nKey.UI.Bar.GeometryAttributes.StraightLineLabel,I18nKey.UI.Bar.GeometryAttributes.RectangleLabel})
				expect(owner.spec.MeasureText(IW(key),11.0,DWRITE_FONT_WEIGHT_NORMAL).width<=BarGeometryAttributeShapeButtonSize,
					"localized geometry label fits its button");
			const auto annotationBody=owner.spec.MeasureText(IW(I18nKey.UI.Bar.DrawAttributes.AnnotationDescription),BarThicknessTooltipBodyFontSize,DWRITE_FONT_WEIGHT_NORMAL);
			const auto overflowBody=owner.spec.MeasureText(IW(I18nKey.UI.Bar.DrawAttributes.ThicknessOverflowBody),BarThicknessTooltipBodyFontSize,DWRITE_FONT_WEIGHT_NORMAL);
			for(const auto* key:{I18nKey.UI.Bar.DrawAttributes.AnnotationUnavailableTitle,I18nKey.UI.Bar.DrawAttributes.AnnotationFixedUnsupportedTitle})
			{
				const auto title=owner.spec.MeasureText(IW(key),BarThicknessTooltipTitleFontSize,DWRITE_FONT_WEIGHT_SEMI_BOLD);
				const double width=std::ceil((std::max)(title.width,annotationBody.width))+BarThicknessTooltipPadding*2+BarThicknessTooltipCloseReserve;
				expect(width>0 && width<=1000,"localized annotation popup expands within the offscreen work area");
			}
			const auto annotationTitleText=IW(I18nKey.UI.Bar.DrawAttributes.AnnotationFixedUnsupportedTitle);
			const auto annotationBodyText=IW(I18nKey.UI.Bar.DrawAttributes.AnnotationDescription);
			const auto annotationTitle=owner.spec.MeasureText(annotationTitleText,BarThicknessTooltipTitleFontSize,DWRITE_FONT_WEIGHT_SEMI_BOLD);
			const double annotationWidth=std::ceil((std::max)(annotationTitle.width,annotationBody.width))+BarThicknessTooltipPadding*2+BarThicknessTooltipCloseReserve;
			const double annotationHeight=std::ceil(annotationTitle.height+BarThicknessTooltipLineGap+annotationBody.height)+BarThicknessTooltipPadding*2;
			const auto overflowTitleText=IW(I18nKey.UI.Bar.DrawAttributes.ThicknessOverflowTitle);
			const auto overflowBodyText=IW(I18nKey.UI.Bar.DrawAttributes.ThicknessOverflowBody);
			const auto overflowTitle=owner.spec.MeasureText(IW(I18nKey.UI.Bar.DrawAttributes.ThicknessOverflowTitle),BarThicknessTooltipTitleFontSize,DWRITE_FONT_WEIGHT_SEMI_BOLD);
			const double overflowWidth=std::ceil((std::max)(overflowTitle.width,overflowBody.width))+BarThicknessTooltipPadding*2+BarThicknessTooltipCloseReserve;
			const double overflowHeight=std::ceil(overflowTitle.height+BarThicknessTooltipLineGap+overflowBody.height)+BarThicknessTooltipPadding*2;
			expect(overflowWidth>0 && overflowWidth<=1000,"localized overflow popup expands within the offscreen work area");
			const double rgbLabelWidth=(std::max)({owner.spec.MeasureText(IW(I18nKey.UI.Bar.ColorPicker.RedChannelLabel),13.0).width,
				owner.spec.MeasureText(IW(I18nKey.UI.Bar.ColorPicker.GreenChannelLabel),13.0).width,
				owner.spec.MeasureText(IW(I18nKey.UI.Bar.ColorPicker.BlueChannelLabel),13.0).width});
			const double rgbValueWidth=owner.spec.MeasureText(L"255",13.0).width;
			const double opacityLabelWidth=owner.spec.MeasureText(IW(I18nKey.UI.Bar.ColorPicker.OpacityLabel),13.0).width;
			const double opacityValueWidth=owner.spec.MeasureText(L"100%",13.0).width;
			const double footerRequiredWidth=(rgbLabelWidth+rgbValueWidth)*3+opacityLabelWidth+opacityValueWidth+4*3.0+2*6.0+10.0;
			expect(footerRequiredWidth<=BarColorPickerPanelWidth,"localized color footer keeps RGB and opacity columns separate");

			for(const double zoom:{0.65,1.0,1.5})
			{
				owner.barStyle.darkStyle=true;owner.barStyle.zoom=zoom;
				const UINT width=static_cast<UINT>(700*zoom),height=static_cast<UINT>(245*zoom);
				expect(SUCCEEDED(owner.spec.EnsureDeviceResources(RenderPipeline::GetDeviceEpoch(),width,height)),"localized bar target setup");
				owner.spec.SetFrameZoom(zoom);owner.spec.PrepareFrameLighting(1.0/60,static_cast<int>(StateModeSelectEnum::IdtEraser),0,0,0);
				auto* dc=owner.spec.GetDeviceContext();dc->BeginDraw();dc->SetTransform(D2D1::IdentityMatrix());dc->Clear(D2D1::ColorF(0.13f,0.14f,0.16f,1));
				main->fill->SetDirect(GetThemeColor(BarThemeColorEnum::Surface));main->frame->SetDirect(GetThemeColor(BarThemeColorEnum::SurfaceFrame));
				DrawBarBackgroundVisual(owner.spec,dc,*main,BarUiInheritClass(main->inhX,main->inhY));
				for(const auto preset:presets)
				{
					auto* b=preset==BarButtonPresetEnum::More?owner.barButtonSet.GetMoreButton():owner.barButtonSet.preset[static_cast<int>(preset)];
					b->name.color.SetDirect(GetThemeColor(BarThemeColorEnum::TextPrimary));b->icon.color1->SetDirect(GetThemeColor(BarThemeColorEnum::TextPrimary));
					DrawBarButtonVisual(owner.spec,dc,*b,b->button.Inherit(BarUiInheritEnum::CenterFromTopLeft,*main));
				}
				auto drawAuditSurface=[&](double x,double y,double w,double h)
				{
					BarUiShapeClass surface(0,0,w,h,8,8,1,GetThemeColor(BarThemeColorEnum::Surface),GetThemeColor(BarThemeColorEnum::SurfaceFrame));
					surface.enable.Initialization(true);surface.pct.SetDirect(0.96);surface.framePct.emplace(0.18);surface.frameLightPct.emplace(0);
					owner.spec.Shape(dc,surface,BarUiInheritClass(x,y));
				};
				auto drawAuditText=[&](const std::wstring& text,double x,double y,double w,double h,double size,DWRITE_FONT_WEIGHT weight=DWRITE_FONT_WEIGHT_NORMAL)
				{
					BarUiWordClass word(0,0,w,h,text,size,GetThemeColor(BarThemeColorEnum::TextPrimary));
					word.enable.Initialization(true);word.pct.SetDirect(1);
					owner.spec.Word(dc,word,BarUiInheritClass(x,y),weight,DWRITE_TEXT_ALIGNMENT_LEADING);
				};
				const double annotationLeft=10,annotationTop=95;
				drawAuditSurface(annotationLeft,annotationTop,annotationWidth,annotationHeight);
				drawAuditText(annotationTitleText,annotationLeft+BarThicknessTooltipPadding,annotationTop+BarThicknessTooltipPadding,
					annotationWidth-BarThicknessTooltipPadding*2-BarThicknessTooltipCloseReserve,annotationTitle.height,
					BarThicknessTooltipTitleFontSize,DWRITE_FONT_WEIGHT_SEMI_BOLD);
				drawAuditText(annotationBodyText,annotationLeft+BarThicknessTooltipPadding,
					annotationTop+BarThicknessTooltipPadding+annotationTitle.height+BarThicknessTooltipLineGap,
					annotationWidth-BarThicknessTooltipPadding*2,annotationBody.height,BarThicknessTooltipBodyFontSize);
				const double overflowLeft=10,overflowTop=170;
				drawAuditSurface(overflowLeft,overflowTop,overflowWidth,overflowHeight);
				drawAuditText(overflowTitleText,overflowLeft+BarThicknessTooltipPadding,overflowTop+BarThicknessTooltipPadding,
					overflowWidth-BarThicknessTooltipPadding*2-BarThicknessTooltipCloseReserve,overflowTitle.height,
					BarThicknessTooltipTitleFontSize,DWRITE_FONT_WEIGHT_SEMI_BOLD);
				drawAuditText(overflowBodyText,overflowLeft+BarThicknessTooltipPadding,
					overflowTop+BarThicknessTooltipPadding+overflowTitle.height+BarThicknessTooltipLineGap,
					overflowWidth-BarThicknessTooltipPadding*2,overflowBody.height,BarThicknessTooltipBodyFontSize);
				const double footerLeft=390,footerTop=170,footerHeight=55,footerPadding=5,footerLabelGap=3,footerColumnGap=6;
				drawAuditSurface(footerLeft,footerTop,BarColorPickerPanelWidth,footerHeight);
				const double rgbColumnWidth=rgbLabelWidth+footerLabelGap+rgbValueWidth;
				const double rX=footerLeft+footerPadding,gX=rX+rgbColumnWidth+footerColumnGap,bX=gX+rgbColumnWidth+footerColumnGap;
				const double opacityX=footerLeft+BarColorPickerPanelWidth-footerPadding-opacityLabelWidth-footerLabelGap-opacityValueWidth;
				const std::array<const char*,4> footerLabelKeys{I18nKey.UI.Bar.ColorPicker.RedChannelLabel,I18nKey.UI.Bar.ColorPicker.GreenChannelLabel,I18nKey.UI.Bar.ColorPicker.BlueChannelLabel,I18nKey.UI.Bar.ColorPicker.OpacityLabel};
				const std::array<double,4> footerLabelX{rX,gX,bX,opacityX};
				const std::array<const wchar_t*,4> footerValues{L"255",L"255",L"255",L"100%"};
				for(size_t i=0;i<footerLabelKeys.size();++i)
				{
					const double labelWidth=i<3?rgbLabelWidth:opacityLabelWidth;
					const double valueWidth=i<3?rgbValueWidth:opacityValueWidth;
					drawAuditText(IW(footerLabelKeys[i]),footerLabelX[i],footerTop,labelWidth,footerHeight,13);
					drawAuditText(footerValues[i],footerLabelX[i]+labelWidth+footerLabelGap,footerTop,valueWidth,footerHeight,13);
				}
				expect(SUCCEEDED(dc->EndDraw()),"localized fixed bar renders");
				const auto file=std::filesystem::path(L"Build/eraser-b/visuals")/(L"bar-i18n-"+std::wstring(language.fileTag)+L"-"+std::to_wstring(static_cast<int>(zoom*100))+L".png");
				expect(SUCCEEDED(SaveEraserTestPng(dc,owner.spec.GetTargetBitmap(),file)),"localized fixed bar PNG readback");
			}
		}
		expect(I18n::load(1,L"JSON",L"zh-CN"),"restore default test language");owner.spec.ConfigureLocalizedTypography();updateLocalizedPresetLabels();
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
			owner.spec.ConfigureLocalizedTypography();updateLocalizedPresetLabels();
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
				const auto beforeCorner=EraserPreferencesSnapshot().baseSize;const auto smallHit=layout.items[1];ExMessage corner{};
				corner.x=static_cast<short>(smallHit.left+0.5);corner.y=static_cast<short>(smallHit.top+0.5);
				corner.message=WM_LBUTTONDOWN;corner.lbutton=true;owner.eraserAttribute.Pointer(owner,corner);
				corner.message=WM_LBUTTONUP;corner.lbutton=false;owner.eraserAttribute.Pointer(owner,corner);
				expect(EraserPreferencesSnapshot().baseSize==beforeCorner,"circle bounding-box corner is not clickable");
				ExMessage hover{};hover.message=WM_MOUSEMOVE;hover.x=static_cast<short>(EraserRectCenterX(layout.previews[0]));hover.y=static_cast<short>(EraserRectCenterY(layout.previews[0]));
				owner.eraserAttribute.Pointer(owner,hover);for(int f=0;f<20;++f)owner.eraserAttribute.Advance(owner,1.0/60,1,zoom,dpi,{0,0,static_cast<LONG>(width),static_cast<LONG>(height)},{0,0},0,0);
				owner.eraserAttribute.CommitPresented();const auto hoverRegions=owner.eraserAttribute.PresentedRegions();
				expect(hoverRegions[2].right==hoverRegions[2].left && hoverRegions[2].bottom==hoverRegions[2].top,"hover no longer publishes a rectangular content tooltip");
				click(1);expect(EraserPreferencesSnapshot().baseSize==Inkeys::Drawing::Draw3::SpeedEraser::BaseSize::Small && owner.barState.eraserAttribute && !owner.barState.eraserSensitivityOpen,"outside menu click closes child then applies preset once");
				click(3,true);expect(EraserPreferencesSnapshot().baseSize==Inkeys::Drawing::Draw3::SpeedEraser::BaseSize::Small,"cancelled pointer does not select");
				click(4);expect(Inkeys::Drawing::Draw3::SpeedEraser::GetAutomaticState(EraserPreferencesSnapshot())==Inkeys::Drawing::Draw3::SpeedEraser::AutomaticState::Off && owner.barState.eraserAttribute,"body toggles only the master gate and stays open");
				click(5);expect(owner.barState.eraserSensitivityOpen && Inkeys::Drawing::Draw3::SpeedEraser::GetAutomaticState(EraserPreferencesSnapshot())==Inkeys::Drawing::Draw3::SpeedEraser::AutomaticState::Off,"arrow opens while auto is off without toggling");
				expect(Inkeys::Drawing::Draw3::PublishProductCommand(
					Inkeys::Drawing::Draw3::Bridge::CommandType::Clear) ==
					Inkeys::Drawing::Draw3::Bridge::CommandResult::NotRunning,
					"unstarted Draw3 product rejects clear command");
				click(0);expect(owner.barState.eraserAttribute,"rejected empty clear stays open without changing tool");
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
		// 窄工作区保持生产正常布局并裁剪溢出，截图确认不会切入紧凑留白。
		{
			constexpr UINT narrowWidth=300,narrowHeight=620;
			I18n::load(1,L"JSON",L"zh-CN");owner.spec.ConfigureLocalizedTypography();updateLocalizedPresetLabels();owner.barStyle.darkStyle=true;owner.barStyle.zoom=1;
			owner.barState.widgetPosition.mainBar=true;owner.barState.widgetPosition.primaryBar=false;
			owner.barState.eraserAttribute=true;owner.barState.eraserSensitivityOpen=false;
			Inkeys::config.Drawing.Eraser.Automatic=true;
			root->x.SetDirect(-70);root->y.SetDirect(430);
			const auto epoch=RenderPipeline::GetDeviceEpoch();
			expect(SUCCEEDED(owner.spec.EnsureDeviceResources(epoch,narrowWidth,narrowHeight)),"narrow target setup");
			owner.spec.SetFrameZoom(1);owner.spec.PrepareFrameLighting(1.0/60,static_cast<int>(StateModeSelectEnum::IdtEraser),0,0,0);
			for(int f=0;f<90;++f)owner.eraserAttribute.Advance(owner,1.0/60,1,1,96,{0,0,narrowWidth,narrowHeight},{0,0},0,0);
			owner.eraserAttribute.CommitPresented();const auto narrow=owner.eraserAttribute.PresentationSnapshot();
			const double narrowCircleEndGap=narrow.geometry.reversed
				?narrow.geometry.previews[0].left-narrow.geometry.dividers[1].right
				:narrow.geometry.dividers[0].left-narrow.geometry.previews[2].right;
			const double narrowAutomaticGap=narrow.geometry.reversed
				?narrow.geometry.dividers[0].left-narrow.geometry.automatic.right
				:narrow.geometry.automatic.left-narrow.geometry.dividers[1].right;
			expect(narrow.panelVisible && narrow.geometry.horizontalOverflow && narrow.geometry.panel.Width()<=narrowWidth &&
				std::abs((narrow.geometry.previews[1].left-narrow.geometry.previews[0].right)-EraserAttributeCircleGapDip)<0.001 &&
				std::abs((narrow.geometry.previews[2].left-narrow.geometry.previews[1].right)-EraserAttributeCircleGapDip)<0.001 &&
				std::abs(narrowCircleEndGap-EraserAttributeCircleGapDip)<0.001 &&
				std::abs(narrow.geometry.items[0].left-narrow.geometry.dividers[0].right-EraserAttributeStandardGapDip)<0.001 &&
				std::abs(narrow.geometry.dividers[1].left-narrow.geometry.items[0].right-EraserAttributeStandardGapDip)<0.001 &&
				std::abs(narrowAutomaticGap-EraserAttributeStandardGapDip)<0.001 &&
				std::abs(narrow.geometry.automatic.Width()-BarButtonTwoSideDip-EraserAttributeAutomaticArrowWidthDip)<0.001 &&
				std::abs(narrow.geometry.items[4].Width()-BarButtonTwoSideDip)<0.001 &&
				std::abs(narrow.geometry.items[5].Width()-EraserAttributeAutomaticArrowWidthDip)<0.001,
				"narrow work area clips the normal eraser layout without compacting normal gaps or controls");
			auto lighting=owner.spec.SnapshotFrameLighting();
			lighting.cursorLight={static_cast<float>(EraserRectCenterX(narrow.geometry.automatic)),static_cast<float>(EraserRectCenterY(narrow.geometry.automatic))};
			lighting.cursorRadius=240;lighting.cursorIntensity=1;lighting.cursorLightVisible=true;
			owner.spec.SetFrameLightingSnapshot(lighting);owner.spec.SetFrameCursorLightLocalGeometry(lighting.cursorLight,D2D1::SizeF(240,240));
			auto* dc=owner.spec.GetDeviceContext();dc->BeginDraw();dc->SetTransform(D2D1::IdentityMatrix());dc->Clear(D2D1::ColorF(0.13f,0.14f,0.16f,1));
			owner.eraserAttribute.Draw(owner.spec,dc);
			main->fill->SetDirect(GetThemeColor(BarThemeColorEnum::Surface));main->frame->SetDirect(GetThemeColor(BarThemeColorEnum::SurfaceFrame));
			DrawBarBackgroundVisual(owner.spec,dc,*main,BarUiInheritClass(main->inhX,main->inhY));
			for(const auto preset:presets)
			{
				auto* b=preset==BarButtonPresetEnum::More?owner.barButtonSet.GetMoreButton():owner.barButtonSet.preset[static_cast<int>(preset)];
				b->name.color.SetDirect(GetThemeColor(BarThemeColorEnum::TextPrimary));b->icon.color1->SetDirect(GetThemeColor(BarThemeColorEnum::TextPrimary));
				DrawBarButtonVisual(owner.spec,dc,*b,b->button.Inherit(BarUiInheritEnum::CenterFromTopLeft,*main));
			}
			expect(SUCCEEDED(dc->EndDraw()),"narrow production panel renders successfully");owner.eraserAttribute.CommitPresented();
			const std::filesystem::path narrowPath(L"Build/eraser-b/visuals/narrow-eraser-attribute.png");
			expect(SUCCEEDED(SaveEraserTestPng(dc,owner.spec.GetTargetBitmap(),narrowPath)),"narrow PNG readback succeeds");
			report<<"[EraserVisual] "<<narrowPath.string()<<" width="<<narrowWidth<<" height="<<narrowHeight<<'\n';
			root->x.SetDirect(100);
		}
		// 实际组件逐帧渲染：所有PNG与CSV都来自同一Advance/Draw/CommitPresented路径。
		{
			I18n::load(1,L"JSON",L"zh-CN");owner.spec.ConfigureLocalizedTypography();updateLocalizedPresetLabels();owner.barStyle.darkStyle=true;owner.barStyle.zoom=1;
			BarUiEdgeLightingEnabled=true;BarUiDynamicEdgeLightingEnabled=true;BarUiAnimationEnabled=true;
			root->y.SetDirect(430);owner.barState.widgetPosition.primaryBar=false;owner.barState.widgetPosition.mainBar=true;
			const auto epoch=RenderPipeline::GetDeviceEpoch();expect(SUCCEEDED(owner.spec.EnsureDeviceResources(epoch,1000,620)),"animation target setup");
			owner.spec.SetFrameZoom(1);auto* dc=owner.spec.GetDeviceContext();
			std::filesystem::create_directories(L"Build/eraser-b/frames");
			std::ofstream trace("Build/eraser-b/frames.csv");trace<<"case,frame,panel_scale,menu_scale,panel_alpha,menu_alpha,panel_x,panel_y,panel_w,panel_h,menu_x,menu_y,menu_w,menu_h,bounds_l,bounds_t,bounds_r,bounds_b\n";
			auto tick=[&](const std::string& name,int f,double dt=1.0/60,bool save=false,
				RECT workArea={0,0,1000,620},POINT origin={0,0},bool dragPlacementLocked=false,
				const BarUiTimelineClass* parentTimeline=nullptr)
			{
				owner.eraserAttribute.Advance(owner,dt,BarUiAnimationSpeedRate,1,96,workArea,origin,0,0,parentTimeline,dragPlacementLocked);
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
			// 直拖期间工作区相对坐标会越过主栏；应维持已呈现方向，松手后才走既有收拢再展开路径。
			const auto beforeDrag=stable;
			for(int f=0;f<12;++f)
			{
				const auto held=tick("drag-hold",f,1.0/60,f==0,{0,0,1000,1000},{0,-550},true);
				expect(held.geometry.below==beforeDrag.geometry.below &&
					std::abs(held.geometry.panel.top-beforeDrag.geometry.panel.top)<0.000001,
					"direct drag does not snap the presented eraser panel side or position");
			}
			owner.barState.widgetPosition.mainBar=false;owner.barState.widgetPosition.primaryBar=true;root->y.SetDirect(140);
			BarUiTimelineClass parentTimeline;parentTimeline.Restart(BarUiDefaultOperationDur);
			parentTimeline.Advance(BarUiDefaultOperationDur/4,1);
			const auto release=tick("drag-release",0,0,true,{0,0,1000,620},{0,0},false,&parentTimeline);
			const double absorbedY=EraserRectCenterY(release.geometry.anchor)-EraserRectCenterY(beforeDrag.geometry.anchor);
			expect(release.geometry.below==beforeDrag.geometry.below && owner.eraserAttribute.Active() &&
				std::abs(release.geometry.panel.top-beforeDrag.geometry.panel.top-absorbedY)<0.000001,
				"release first frame rebases the old panel without a work-area clamp flash");
			const int joinedSwitchFrames=static_cast<int>(std::ceil(parentTimeline.GetRemainingDuration()/(1.0/60)));
			const int joinedCollapseFrames=static_cast<int>(std::ceil((BarUiDefaultOperationDur/4)/(1.0/60)));
			for(int f=0;f<joinedCollapseFrames;++f)tick("drag-release",f+1,1.0/60,f%3==0);
			const auto switched=owner.eraserAttribute.PresentationSnapshot();
			expect(switched.geometry.below && switched.geometry.reversed && owner.eraserAttribute.Active(),
				"release commits the new direction on the parent midpoint frame without a late old-side frame");
			for(int f=joinedCollapseFrames;f<joinedSwitchFrames;++f)tick("drag-release",f+1,1.0/60,f%3==0);
			const auto released=owner.eraserAttribute.PresentationSnapshot();
			expect(released.geometry.below && released.geometry.reversed && released.panelPose.scale==1,
				"release side switch joins the draw-attribute parent batch and settles at the same deadline");
			owner.barState.widgetPosition.mainBar=true;owner.barState.widgetPosition.primaryBar=false;root->y.SetDirect(430);
			BarUiTimelineClass lateParentTimeline;lateParentTimeline.Restart(BarUiDefaultOperationDur);
			lateParentTimeline.Advance(BarUiDefaultOperationDur*0.75,1);
			tick("drag-reset",0,0,true,{0,0,1000,620},{0,0},false,&lateParentTimeline);
			const int lateParentFrames=static_cast<int>(std::ceil(lateParentTimeline.GetRemainingDuration()/(1.0/60)));
			for(int f=0;f<lateParentFrames;++f)tick("drag-reset",f+1,1.0/60,f%3==0);
			expect(owner.eraserAttribute.Active() && owner.eraserAttribute.PresentationSnapshot().geometry.below,
				"side switch after the parent midpoint starts a full independent draw-attribute batch");
			const int independentFrames=static_cast<int>(std::ceil(BarUiDefaultOperationDur/(1.0/60)));
			for(int f=lateParentFrames;f<independentFrames;++f)tick("drag-reset",f+1,1.0/60,f%3==0);
			stable=owner.eraserAttribute.PresentationSnapshot();
			expect(!stable.geometry.below && !stable.geometry.reversed && stable.panelPose.scale==1,
				"drag test restores the original presented side before interaction coverage");
			// RenderLoop先推进父时间线；即使该帧从中点前跨到中点后，仍应加入同一绘制属性批次。
			owner.barState.widgetPosition.mainBar=false;owner.barState.widgetPosition.primaryBar=true;root->y.SetDirect(140);
			BarUiTimelineClass crossingParentTimeline;crossingParentTimeline.Restart(BarUiDefaultOperationDur);
			crossingParentTimeline.Advance(BarUiDefaultOperationDur*0.49,1);
			crossingParentTimeline.Advance(1.0/60,BarUiAnimationSpeedRate);
			const auto crossed=tick("crossing-join",0,1.0/60,true,{0,0,1000,620},{0,0},false,&crossingParentTimeline);
			expect(crossed.geometry.below && crossed.geometry.reversed,
				"parent frame crossing 50 percent keeps the eraser in the joined draw-attribute batch");
			const int crossingFrames=static_cast<int>(std::ceil(crossingParentTimeline.GetRemainingDuration()/(1.0/60)));
			for(int f=0;f<crossingFrames;++f)tick("crossing-join",f+1,1.0/60,f%3==0);
			expect(owner.eraserAttribute.PresentationSnapshot().panelPose.scale==1,
				"joined crossing frame carries its midpoint remainder into the expansion segment");
			owner.barState.widgetPosition.mainBar=true;owner.barState.widgetPosition.primaryBar=false;root->y.SetDirect(430);
			for(int f=0;f<independentFrames;++f)tick("crossing-restore",f,1.0/60,f%3==0);
			stable=owner.eraserAttribute.PresentationSnapshot();
			expect(!stable.geometry.below && !stable.geometry.reversed && stable.panelPose.scale==1,
				"crossing-frame timing test restores the original side");
			// 保存真实按压及选中环交接帧，避免只有稳定态截图而遗漏交互动画。
			auto sizeMessage=[&](UINT kind,int item,bool held){ExMessage m{};m.message=static_cast<USHORT>(kind);m.x=static_cast<short>(EraserRectCenterX(stable.geometry.items[item]));m.y=static_cast<short>(EraserRectCenterY(stable.geometry.items[item]));m.lbutton=held;return m;};
			owner.eraserAttribute.Pointer(owner,sizeMessage(WM_LBUTTONDOWN,3,true));
			tick("size-press",0,1.0/60,true);expect(owner.eraserAttribute.Active(),"size press starts the shared button feedback animation");
			for(int f=1;f<10;++f)tick("size-press",f,1.0/60,true);
			owner.eraserAttribute.Pointer(owner,sizeMessage(WM_LBUTTONUP,3,false));
			tick("size-selection-fade",0,1.0/60,true);expect(owner.eraserAttribute.Active(),"selection ring fade and press release request continuation frames");
			for(int f=1;f<30;++f)tick("size-selection-fade",f,1.0/60,true);
			expect(!owner.eraserAttribute.Active(),"size press and reversible selection-ring fade settle to idle");
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
		// B3只共用真实CacheBitmap/Svg/D2D离屏事务；不把数值observer组合称为主栏ULW。
		{
			constexpr UINT svgSize = 96;
			constexpr std::uint32_t svgTag = 0x10000;
			const auto epoch = RenderPipeline::GetDeviceEpoch();
			BarUIRendering renderer(nullptr);
			const HRESULT setup = renderer.EnsureDeviceResources(epoch, svgSize, svgSize);
			auto* dc = renderer.GetDeviceContext();
			const bool deviceReady = SUCCEEDED(setup) && dc && epoch.generation != 0;
			expect(deviceReady, "B300 SVG proof premise: real WARP/D2D epoch and 96x96 target");
			if (deviceReady)
			{
				auto probe = std::make_unique<Ui3SvgProbe>(0xB301);
				BarUiSVGClass svg;
				bool bound = false;
				{
					SvgObservationScope ownedInit(probe.get(), false, true);
					svg.Initialization(0, 0, RGB(220, 110, 40), std::nullopt);
					svg.InitializationFromString(LR"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32"><rect width="32" height="32" fill="rgba(10,0,7,0)"/></svg>)SVG");
					svg.w.SetDirect(32); svg.h.SetDirect(32); svg.pct.SetDirect(1); svg.enable.Initialization(true);
					bound = svg.BindObservationTag(svgTag);
				}
				expect(bound && svg.ObservationState().semanticKnown && svg.ObservationState().valueRevision == 1
					&& !svg.cacheBitmap && svg.svg.IsSame(), "B300 SVG proof premise: owned value built before tag, no old bitmap");
				if (bound && svg.ObservationState().semanticKnown)
				{
					renderer.SetFrameZoom(1.0);
					std::uint64_t attempt = 0;
					bool transactionOk = false, submitted = false;
					struct SvgTestFrame
					{
						bool paint = true, clear = true, commit = true, outerClip = true, nested = false, unknownWrite = false;
						D2D1_MATRIX_3X2_F transform = D2D1::IdentityMatrix();
						D2D1_RECT_F nestedClip = D2D1::RectF(20, 20, 30, 30);
						std::uint32_t dpi = 96, alpha = 255;
						double zoom = 1.0, x = 16.0, y = 16.0;
						std::uint64_t revision = 1, surface = 1;
					} options;
					auto currentEpoch = epoch;
					auto drawSvg = [&](const D2D1_RECT_F& clip, bool clocks)
					{
						SvgObservationScope rendering(probe.get(), clocks);
						renderer.SetFrameZoom(options.zoom);
						probe->BeginFrame(options.revision, currentEpoch.generation, ++attempt);
						svg.ObserveFiniteRequirement(true);
						Ui3SvgFrameTarget target;
						target.revision = options.revision; target.epoch = currentEpoch.generation; target.surfaceSerial = options.surface; target.frameAttemptSerial = attempt;
						target.width = target.height = target.backingWidth = target.backingHeight = svgSize;
						target.dpi = options.dpi; target.windowAlpha = options.alpha; target.zoomBits = std::bit_cast<std::uint64_t>(options.zoom);
						for (unsigned i = 0; i < 4; ++i) target.viewportBits[i] = std::bit_cast<std::uint32_t>(i < 2 ? 0.0f : static_cast<float>(svgSize));
						probe->BeginBackingWrite(dc, target);
						dc->BeginDraw(); dc->SetTransform(D2D1::IdentityMatrix());
						if (options.outerClip) renderer.PushFrameDirtyClip(dc, clip);
						if (options.nested)
						{
							dc->PushAxisAlignedClip(options.nestedClip, D2D1_ANTIALIAS_MODE_ALIASED);
							const float rect[4]{options.nestedClip.left, options.nestedClip.top, options.nestedClip.right, options.nestedClip.bottom};
							const float matrix[6]{1,0,0,1,0,0}; std::uint32_t r[4], m[6];
							for (unsigned i = 0; i < 4; ++i) r[i] = std::bit_cast<std::uint32_t>(rect[i]);
							for (unsigned i = 0; i < 6; ++i) m[i] = std::bit_cast<std::uint32_t>(matrix[i]);
							ObserveUi3SvgClipPush(dc, r, m);
						}
						if (options.clear) { ObserveUi3SvgClear(dc); dc->Clear(D2D1::ColorF(0, 0, 0, 0)); }
						dc->SetTransform(options.transform);
						submitted = options.paint && renderer.Svg(dc, svg, BarUiInheritClass(options.x, options.y));
						if (options.unknownWrite) { ObserveUi3SvgUnknownWrite(dc); dc->Clear(D2D1::ColorF(0, 0, 0, 0)); }
						dc->SetTransform(D2D1::IdentityMatrix());
						if (options.nested) { ObserveUi3SvgClipPop(dc); dc->PopAxisAlignedClip(); }
						if (options.outerClip) renderer.PopFrameDirtyClip(dc);
						const auto proof = probe->FinishDrawing();
						transactionOk = SUCCEEDED(dc->EndDraw());
						probe->CompleteAttempt(transactionOk && options.commit);
						return proof;
					};
					const auto full = drawSvg(D2D1::RectF(0, 0, svgSize, svgSize), false);
					const auto actual = probe->Observation(svgTag);
					const auto counts = probe->CountersAfterOwnerStopped();
					const auto pixelOff = ReadEraserTestPixel(dc, renderer.GetTargetBitmap(), 32, 32);
					const bool cacheReady = transactionOk && submitted && svg.cacheBitmap && actual.used.ready && actual.used.semanticKnown
						&& actual.used.valueRevision == 1 && actual.used.epoch == epoch.generation && actual.used.surfaceSerial == 1
						&& actual.used.dpi == 96 && actual.used.pixelWidth == 32 && actual.used.pixelHeight == 32 && pixelOff[3] != 0;
					expect(cacheReady, "B300 SVG proof premise: actual bitmap/upload/draw/EndDraw and nonempty BGRA");
					expect(counts.lookupMiss == 1 && counts.createAttempt == 1 && counts.createSuccess == 1 && counts.createFailure == 0
						&& counts.parseCalls == 1 && counts.rasterCalls == 1 && counts.uploadCalls == 1 && counts.drawSubmit == 1
						&& counts.readyEntries == 1 && counts.logicalReadyBytes == 32 * 32 * 4 && counts.unknownReadyEntries == 0
						&& probe->InitializationCountersAfterOwnerStopped().parseCalls == 1,
						"B301 real SVG first creation counts separate initialization parse from frame parse");
					if (cacheReady)
					{
						expect(full.required == 1 && full.verified == 1 && full.failed == 0 && full.unverified == 0
							&& actual.coverage == Ui3SvgCoverage::FullVisibleCoverage && actual.use == Ui3SvgUse::DrawnVerified,
							"B302 actual full clip SVG must yield staged complete paint proof");
						Ui3FiniteSignature initial;
						initial.flags = 65; initial.stateMode = 1; initial.penMode = 0; initial.penColorRgb = RGB(220, 110, 40);
						initial.penWidthBits = std::bit_cast<std::uint32_t>(3.0f); initial.dpi = 96;
						initial.toolRevision = 1; initial.displaySerial = 2; initial.configZoomBits = std::bit_cast<std::uint64_t>(1.0);
						initial.validMask = Ui3FiniteRequiredMask;
						auto publication = std::make_unique<Ui3FinitePublication>(0xB303, initial);
						auto observer = std::make_unique<Ui3FiniteObserver>(*publication);
						auto goal = initial; goal.flags = 64;
						auto mutation = publication->BeginMutation(1, Ui3FiniteScene::MainFold, 1);
						publication->MarkBusinessAccepted(mutation); publication->ObserveBusinessWrite(mutation);
						publication->FinishAtRenderRequest(mutation, goal, 0);
						Ui3FiniteAccepted accepted;
						const bool stable = observer->SnapshotAccepted(accepted);
						observer->BeginFrame(accepted, epoch.generation, 1);
						const bool consumed = observer->MarkConsumed(goal, 1, 1);
						for (unsigned role = 1; role <= 6; ++role) observer->ObserveProperty(static_cast<Ui3PropertyRole>(role), false, true);
						auto candidate = observer->SettleCandidate(1, svgSize, svgSize);
						const bool bridgeReady = stable && consumed && candidate.settled && !candidate.svgProofComplete;
						expect(bridgeReady,
							"B300 finite bridge premise: legal signature/publication/consumed/layout; no pre-draw resource proof");
						const auto layoutCount = observer->CountersAfterRenderStopped().layoutSettled;
						expect(publication->CompletedRevision() == 0, "B303 complete receipt is absent before actual full resource transaction");
						candidate = observer->FinalizeResources(candidate, full);
						Ui3FiniteCommitIdentity identity;
						identity.epoch = epoch.generation; identity.surfaceSerial = 1; identity.frameAttemptSerial = 1;
						identity.targetWidth = identity.targetHeight = svgSize;
						const auto outcome = observer->CompleteAttempt(candidate, true, false, 0, identity);
						if (bridgeReady) expect(candidate.svgProofComplete && outcome == Ui3FiniteStatus::CompletedLayoutAndSvg,
							"B303 real SVG late resource finalization completes the same finite candidate");
						if (bridgeReady) expect(publication->CompletedRevision() == accepted.revision,
							"B303 acquire getter exposes only real completed revision receipt");
						expect(observer->CountersAfterRenderStopped().layoutSettled == layoutCount,
							"B304 late resource finalization does not repeat layout/consume/clock");
					}
					const auto partial = drawSvg(D2D1::RectF(0, 0, 28, svgSize), false);
					expect(transactionOk && submitted && partial.required == 1 && partial.verified == 0
						&& probe->Observation(svgTag).coverage == Ui3SvgCoverage::Partial,
						"B305 actual partial dirty clip cannot certify full SVG coverage");
					(void)drawSvg(D2D1::RectF(0, 0, svgSize, svgSize), true);
					const auto pixelOn = ReadEraserTestPixel(dc, renderer.GetTargetBitmap(), 32, 32);
					const auto after = probe->CountersAfterOwnerStopped();
					expect(transactionOk && pixelOff == pixelOn && counts.clockReads == 0 && after.clockReads > 0
						&& after.lookupHit == 2 && after.createSuccess == 1 && after.uploadCalls == 1,
						"B306 existing bitmap reuse and gated clocks retain actual BGRA pixel");
					expect(FitsUi3SvgCaptureBudget(32768, sizeof(RenderPipeline::RawCallbackSample), sizeof(RenderPipeline::RawBatchSample))
						&& !FitsUi3SvgCaptureBudget(1, (std::numeric_limits<std::size_t>::max)(), (std::numeric_limits<std::size_t>::max)()),
						"B307 combined 64MiB capture budget rejects multiplication/addition overflow");
					const auto fullClip = D2D1::RectF(0, 0, svgSize, svgSize);
					const auto empty = drawSvg(D2D1::RectF(0, 0, 1, 1), false);
					expect(transactionOk && empty.verified == 1 && empty.unverified == 0
						&& probe->Observation(svgTag).coverage == Ui3SvgCoverage::Empty
						&& probe->Observation(svgTag).use == Ui3SvgUse::RetainedVerified,
						"B310 known disjoint clip retains the exact previously verified SVG proof");
					options.outerClip = false;
					const auto unknown = drawSvg(fullClip, false);
					expect(transactionOk && unknown.unverified == 1 && probe->Observation(svgTag).coverage == Ui3SvgCoverage::Unknown,
						"B311 absent clip state cannot certify SVG");
					options = {}; options.nested = true;
					const auto nested = drawSvg(fullClip, false);
					expect(transactionOk && nested.verified == 0 && probe->Observation(svgTag).coverage == Ui3SvgCoverage::Partial,
						"B312 actual nested clip intersection is partial");
					options = {}; options.paint = options.clear = false;
					const auto retained = drawSvg(fullClip, false);
					const auto missingDraw = retained;
						// 首版不能以本帧没有重画/旧cache仍在来给retained证书。
					expect(transactionOk && retained.required == 1 && retained.verified == 0 && probe->Observation(svgTag).use != Ui3SvgUse::RetainedVerified,
						"B312 no actual redraw keeps required retained SVG unverified");
					options = {}; options.transform = D2D1::Matrix3x2F::Translation(7, 5); svg.angle.SetDirect(20);
					const auto transformed = drawSvg(fullClip, false);
					expect(transactionOk && transformed.verified == 1 && probe->Observation(svgTag).transformBits[4] != std::bit_cast<std::uint32_t>(0.0f),
						"B313 actual SVG rotation and translation retain full coverage proof");
					options = {}; svg.angle.SetDirect(0); svg.pct.SetDirect(0.5);
					const auto opacity = drawSvg(fullClip, false);
					expect(transactionOk && opacity.verified == 1 && probe->Observation(svgTag).finalOpacityBits == std::bit_cast<std::uint32_t>(0.5f),
						"B314 actual half opacity is captured with current expected opacity");
					options.alpha = 0;
					expect(drawSvg(fullClip, false).verified == 0, "B315 zero window alpha cannot certify expected visible SVG");
					options = {}; svg.pct.SetDirect(1); options.unknownWrite = true;
					expect(drawSvg(fullClip, false).verified == 0, "B316 real target write after SVG invalidates staged paint");
					options = {}; (void)drawSvg(fullClip, false);
					svg.enable.Initialization(false); svg.pct.SetDirect(0); options.paint = options.clear = false;
					expect(drawSvg(fullClip, false).verified == 0, "B317 hidden flag alone does not remove prior visible pixels");
					options.clear = true;
					const auto hidden = drawSvg(fullClip, false);
					expect(transactionOk && hidden.required == 1 && hidden.verified == 1 && probe->Observation(svgTag).use == Ui3SvgUse::HiddenExpected
						&& ReadEraserTestPixel(dc, renderer.GetTargetBitmap(), 32, 32)[3] == 0,
						"B318 real complete Clear certifies Hidden with old bounds");
					options = {}; svg.enable.Initialization(true); svg.pct.SetDirect(1); (void)drawSvg(fullClip, false);
					options.x = options.y = 44; options.clear = options.commit = false;
					(void)drawSvg(fullClip, false); // 真D2D写入新位置，模拟之后ULW失败/deferred，不叫真实ULW。
					expect(transactionOk && probe->Observation(svgTag).use == Ui3SvgUse::Unverified, "B319 failed/deferred completion revokes staged SVG proof");
					svg.enable.Initialization(false); svg.pct.SetDirect(0); options = {}; options.paint = false;
					const auto oldOnly = drawSvg(D2D1::RectF(0, 0, 52, 52), false);
					expect(transactionOk && oldOnly.verified == 0 && ReadEraserTestPixel(dc, renderer.GetTargetBitmap(), 60, 60)[3] != 0,
						"B320 Clear of old bounds cannot hide new region written by failed attempt");
					expect(drawSvg(fullClip, false).verified == 1 && ReadEraserTestPixel(dc, renderer.GetTargetBitmap(), 60, 60)[3] == 0,
						"B321 real full backing Clear repairs unknown Hidden lineage");
					options = {}; svg.enable.Initialization(true); svg.pct.SetDirect(1);
					const auto beforeColor = probe->CountersAfterOwnerStopped(); svg.color1->Initialization(RGB(40, 170, 90));
					const auto color = drawSvg(fullClip, false);
					expect(transactionOk && color.verified == 1 && probe->CountersAfterOwnerStopped().replacement == beforeColor.replacement + 1
						&& probe->Observation(svgTag).used.color1Rgb == RGB(40, 170, 90), "B322 real color replacement carries actual color and cache replacement");
					{
						SvgObservationScope writing(probe.get());
						svg.InitializationFromString(LR"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32"><circle cx="16" cy="16" r="15" fill="rgba(10,0,7,0)"/></svg>)SVG");
					}
					const auto content = drawSvg(fullClip, false);
					expect(transactionOk && content.verified == 1 && probe->Observation(svgTag).used.valueRevision == 2,
						"B323 actual value commit increments revision and creates matching bitmap");
					svg.w.SetDirect(40); svg.h.SetDirect(40);
					expect(drawSvg(fullClip, false).verified == 1 && probe->Observation(svgTag).used.pixelWidth == 40,
						"B324 actual stable size follows original SVG raster size policy");
					svg.w.SetDirect(32); svg.h.SetDirect(32);
					const auto imageOffProof = drawSvg(fullClip, false); const auto imageOff = ReadSvgProofPixels(dc, renderer.GetTargetBitmap());
					const auto imageOnProof = drawSvg(fullClip, true); const auto imageOn = ReadSvgProofPixels(dc, renderer.GetTargetBitmap());
					expect(imageOffProof.verified == 1 && imageOnProof.verified == 1 && imageOff.size() == svgSize * svgSize * 4 && imageOff == imageOn,
						"B325 entire actual BGRA target is equal with instrumentation clocks on/off");
					const auto nullBefore = probe->CountersAfterOwnerStopped();
					{
						SvgObservationScope absent(nullptr, true); Ui3SvgStageTimer timer(Ui3SvgStage::Parse);
						dc->BeginDraw(); dc->SetTransform(D2D1::IdentityMatrix()); dc->Clear(D2D1::ColorF(0,0,0,0));
						(void)renderer.Svg(dc, svg, BarUiInheritClass(16,16)); expect(SUCCEEDED(dc->EndDraw()), "B326 default absent-scope actual draw succeeds");
					}
					expect(CurrentUi3SvgScope() == nullptr && probe->CountersAfterOwnerStopped().clockReads == nullBefore.clockReads
						&& probe->CountersAfterOwnerStopped().drawSubmit == nullBefore.drawSubmit, "B326 null scope adds no observation clock or draw counter");
					// H1：全Clear之后的未知写不能因成功EndDraw被早先Clear追认。
					auto unknownReplayFrame = [&](bool unknownBeforeClear)
					{
						SvgObservationScope scope(probe.get()); auto target = probe->Target(); target.frameAttemptSerial = ++attempt;
						probe->BeginFrame(1,currentEpoch.generation,attempt); svg.ObserveFiniteRequirement(true); probe->BeginBackingWrite(dc,target);
						dc->BeginDraw(); dc->SetTransform(D2D1::IdentityMatrix()); renderer.PushFrameDirtyClip(dc,fullClip);
						auto ReplayOutsideOldBounds = [&]()
						{
							ObserveUi3SvgUnknownWrite(dc);
							dc->DrawBitmap(svg.cacheBitmap.Get(),D2D1::RectF(64,64,80,80),1.0f,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,nullptr);
						};
						if (unknownBeforeClear) ReplayOutsideOldBounds();
						ObserveUi3SvgClear(dc); dc->Clear(D2D1::ColorF(0,0,0,0));
						if (!unknownBeforeClear) ReplayOutsideOldBounds();
						renderer.PopFrameDirtyClip(dc); const auto resources = probe->FinishDrawing();
						const bool ended = SUCCEEDED(dc->EndDraw()); probe->CompleteAttempt(ended);
						expect(ended && resources.required == 1 && resources.verified == 0,
							"H100 unknown-replay premise: actual source bitmap, D2D writes, unverified frame and successful completion");
						return ended;
					};
					options = {}; svg.enable.Initialization(true); svg.pct.SetDirect(1);
					const auto h1Seed = drawSvg(fullClip,false);
					expect(transactionOk && h1Seed.verified == 1 && svg.cacheBitmap,
						"H100 seed premise: real successful SVG establishes old bounds");
					svg.enable.Initialization(false); svg.pct.SetDirect(0); options.paint = false;
					const bool replayed = unknownReplayFrame(false);
					const auto outsideBefore = ReadEraserTestPixel(dc,renderer.GetTargetBitmap(),72,72);
					expect(replayed && outsideBefore[3] != 0, "H100 hostile replay exists outside old 14..50 bounds");
					const auto hostileHidden = drawSvg(D2D1::RectF(0,0,52,52),false);
					const auto outsideAfter = ReadEraserTestPixel(dc,renderer.GetTargetBitmap(),72,72);
					expect(transactionOk && outsideAfter[3] != 0, "H100 small old-bounds Clear leaves actual outside replay pixels");
					if (replayed && outsideBefore[3] != 0 && transactionOk && outsideAfter[3] != 0)
						expect(hostileHidden.verified == 0 && probe->Observation(svgTag).use != Ui3SvgUse::HiddenExpected,
							"H101 earlier full Clear cannot certify Hidden after later unknown write and successful completion");
					// 反向正确序：先未知写，再真正全Clear，成功后的小Clear可以恢复Hidden证明。
					options = {}; svg.enable.Initialization(true); svg.pct.SetDirect(1); (void)drawSvg(fullClip,false);
					svg.enable.Initialization(false); svg.pct.SetDirect(0); options.paint = false;
					const bool orderedClear = unknownReplayFrame(true);
					const auto orderedHidden = drawSvg(D2D1::RectF(0,0,52,52),false);
					expect(orderedClear && transactionOk && orderedHidden.verified == 1
						&& ReadEraserTestPixel(dc,renderer.GetTargetBitmap(),72,72)[3] == 0,
						"H102 last full Clear after unknown write restores Hidden lineage without stale outside pixels");
					options = {}; svg.enable.Initialization(true); svg.pct.SetDirect(1); (void)drawSvg(fullClip,false);
					{
						SvgObservationScope writing(probe.get()); svg.InitializationFromString(L"not an SVG");
					}
					const auto parseFailure = drawSvg(fullClip, false);
					expect(transactionOk && !submitted && parseFailure.required == 1 && parseFailure.failed == 1 && parseFailure.verified == 0
						&& parseFailure.firstUnverifiedSvgTag == svgTag && parseFailure.firstUnverifiedReason != 0
						&& !svg.cacheBitmap, "B327 natural parse failure retains failure/tag/reason and never ready");
					{
						SvgObservationScope writing(probe.get()); svg.InitializationFromString(LR"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32"><rect width="32" height="32" fill="rgba(10,0,7,0)"/></svg>)SVG");
					}
					probe->SetOffscreenFault(Ui3SvgOffscreenFault::RejectRasterResult);
					const auto rasterFailure = drawSvg(fullClip, false);
					expect(transactionOk && !submitted && rasterFailure.failed == 1 && !svg.cacheBitmap && probe->CountersAfterOwnerStopped().rasterFailure == 1,
						"B328 bounded injected rejection after actual raster call never commits bitmap");
					probe->SetOffscreenFault(Ui3SvgOffscreenFault::InvalidUploadAlpha);
					const auto uploadFailure = drawSvg(fullClip, false);
					expect(transactionOk && !submitted && uploadFailure.failed == 1 && !svg.cacheBitmap && probe->CountersAfterOwnerStopped().uploadFailure == 1,
						"B329 fixed injected invalid alpha makes actual CreateBitmap fail without ready commit");
					probe->SetOffscreenFault(Ui3SvgOffscreenFault::None); (void)drawSvg(fullClip, false);
					svg.w.SetDirect(42); svg.h.SetDirect(42); probe->SetOffscreenFault(Ui3SvgOffscreenFault::RejectRasterResult);
					const auto fallback = drawSvg(fullClip, false);
					expect(transactionOk && submitted && fallback.failed == 1 && fallback.verified == 0 && svg.cacheBitmap
						&& probe->Observation(svgTag).use == Ui3SvgUse::QualityFallback, "B330 original old-bitmap quality fallback draws but cannot certify new size");
					probe->SetOffscreenFault(Ui3SvgOffscreenFault::None); svg.w.SetDirect(32); svg.h.SetDirect(32);
					// R1：真实资源summary只经共享observer/停后吸收；NoChange保留引用而不造新commit。
					auto checkLedger = [&](const Ui3FiniteResourceProof& resource)
					{
						Ui3FiniteSignature initial;
						initial.flags = 65; initial.stateMode = 1; initial.penColorRgb = RGB(220,110,40);
						initial.penWidthBits = std::bit_cast<std::uint32_t>(3.0f); initial.dpi = 96; initial.toolRevision = 1;
						initial.displaySerial = 2; initial.configZoomBits = std::bit_cast<std::uint64_t>(1.0); initial.validMask = Ui3FiniteRequiredMask;
						auto publication = std::make_unique<Ui3FinitePublication>(0xB331, initial);
						auto observer = std::make_unique<Ui3FiniteObserver>(*publication);
						auto goal = initial; goal.flags = 64;
						auto mutation = publication->BeginMutation(1, Ui3FiniteScene::MainFold, 1);
						publication->MarkBusinessAccepted(mutation); publication->FinishAtRenderRequest(mutation, goal, 0);
						Ui3FiniteAccepted accepted; const bool stable = observer->SnapshotAccepted(accepted);
						observer->BeginFrame(accepted, resource.epoch, resource.frameAttemptSerial);
						const bool consumed = observer->MarkConsumed(goal, 1, 1);
						for (unsigned role = 1; role <= 6; ++role) observer->ObserveProperty(static_cast<Ui3PropertyRole>(role), false, true);
						auto candidate = observer->SettleCandidate(resource.surfaceSerial, svgSize, svgSize);
						expect(stable && consumed && candidate.settled, "B331 ledger premise: legal shared publication and candidate");
						candidate = observer->FinalizeResources(candidate, resource);
						Ui3FiniteCommitIdentity identity; identity.epoch = resource.epoch; identity.surfaceSerial = resource.surfaceSerial;
						identity.frameAttemptSerial = resource.frameAttemptSerial; identity.targetWidth = identity.targetHeight = svgSize;
						(void)observer->CompleteAttempt(candidate, true, false, 0, identity);
						expect(publication->CompletedRevision() == (resource.verified == resource.required && resource.required != 0 ? accepted.revision : 0),
							"B331 missing/partial/failure does not publish completed revision receipt");
						auto noChange = publication->BeginMutation(2, Ui3FiniteScene::MainFold, 2);
						publication->MarkBusinessAccepted(noChange); publication->FinishAtRenderRequest(noChange, goal, 0);
						observer->SealAfterRenderStopped(); publication->AbsorbAfterOwnersStopped(observer->RecordsAfterRenderStopped());
						const auto rows = publication->RecordsAfterOwnerStopped();
						expect(rows.size() == 2, "B331 two request rows retained");
						for (const auto& row : rows) expect(row.requiredSvg == resource.required && row.verifiedSvg == resource.verified
							&& row.failedSvg == resource.failed && row.unverifiedSvg == resource.unverified
							&& row.requiredSvg == row.verifiedSvg + row.failedSvg + row.unverifiedSvg
							&& row.firstUnverifiedSvgTag == resource.firstUnverifiedSvgTag && row.firstUnverifiedReason == resource.firstUnverifiedReason,
							"B331 full/partial/missing/API failure summary survives Complete and stopped Absorb");
						if (rows.size() == 2) expect(rows[1].reusedRevision == 1 && rows[1].finalCommitTicks == 0 && !rows[1].timingValid,
							"B331 no-change references real resource outcome without new commit time");
					};
					checkLedger(full); checkLedger(partial); checkLedger(missingDraw); checkLedger(parseFailure); checkLedger(uploadFailure);
					// R2：initial publication0经真实CacheBitmap，再由合法目标复用同一个bitmap。
					Ui3FiniteSignature initialDpi;
					initialDpi.flags = 65; initialDpi.stateMode = 1; initialDpi.penColorRgb = RGB(220,110,40);
					initialDpi.penWidthBits = std::bit_cast<std::uint32_t>(3.0f); initialDpi.dpi = 192; initialDpi.toolRevision = 1;
					initialDpi.displaySerial = 2; initialDpi.configZoomBits = std::bit_cast<std::uint64_t>(1.0); initialDpi.validMask = Ui3FiniteRequiredMask;
					auto initialPublication = std::make_unique<Ui3FinitePublication>(0xB332, initialDpi);
					auto initialObserver = std::make_unique<Ui3FiniteObserver>(*initialPublication);
					initialObserver->BeginFrame({}, currentEpoch.generation, attempt + 1, true);
					const bool initialConsumed = initialObserver->MarkConsumed(initialDpi, 1, 1);
					for (unsigned role = 1; role <= 6; ++role) initialObserver->ObserveProperty(static_cast<Ui3PropertyRole>(role), false, true);
					auto initialCandidate = initialObserver->SettleCandidate(1, svgSize, svgSize);
					expect(initialConsumed && initialCandidate.renderDpi == 192 && initialCandidate.accepted.stepId == 0,
						"B332 initial candidate locks real consumed DPI without fabricated accepted goal");
					options = {}; options.revision = 0; options.dpi = initialCandidate.renderDpi; options.zoom = 2.0; options.x = options.y = 8;
					{ SvgObservationScope reset(probe.get()); svg.ResetCache(); }
					const auto initialResources = drawSvg(fullClip, false); const auto initialCreates = probe->CountersAfterOwnerStopped();
					const auto* initialBitmap = svg.cacheBitmap.Get();
					initialCandidate = initialObserver->FinalizeResources(initialCandidate, initialResources);
					Ui3FiniteCommitIdentity initialIdentity; initialIdentity.epoch = currentEpoch.generation; initialIdentity.surfaceSerial = 1;
					initialIdentity.frameAttemptSerial = attempt; initialIdentity.targetWidth = initialIdentity.targetHeight = svgSize;
					(void)initialObserver->CompleteAttempt(initialCandidate, true, false, 0, initialIdentity);
					Ui3FixtureReadyValue initialReady;
					expect(initialResources.verified == 1 && probe->Observation(svgTag).used.dpi == 192 && initialObserver->TryReadReady(initialReady)
						&& (initialReady.flags & Ui3FiniteReadyLayoutStable) != 0, "B332 real initial cache supplies nonzero DPI and pure committed initial layout");
					auto nextSignature = initialDpi; nextSignature.flags = 64;
					auto nextMutation = initialPublication->BeginMutation(1, Ui3FiniteScene::MainFold, 1);
					initialPublication->MarkBusinessAccepted(nextMutation); initialPublication->FinishAtRenderRequest(nextMutation, nextSignature, 0);
					Ui3FiniteAccepted nextAccepted; const bool nextStable = initialObserver->SnapshotAccepted(nextAccepted);
					initialObserver->BeginFrame(nextAccepted, currentEpoch.generation, attempt + 1);
					const bool nextConsumed = initialObserver->MarkConsumed(nextSignature, 1, 1);
					for (unsigned role = 1; role <= 6; ++role) initialObserver->ObserveProperty(static_cast<Ui3PropertyRole>(role), false, true);
					auto nextCandidate = initialObserver->SettleCandidate(1, svgSize, svgSize); options.revision = nextAccepted.revision;
					const auto reused = drawSvg(fullClip, false); nextCandidate = initialObserver->FinalizeResources(nextCandidate, reused);
					initialIdentity.frameAttemptSerial = attempt;
					const auto nextOutcome = initialObserver->CompleteAttempt(nextCandidate, true, false, 0, initialIdentity);
					expect(nextStable && nextConsumed && nextCandidate.renderDpi == 192 && nextOutcome == Ui3FiniteStatus::CompletedLayoutAndSvg
						&& svg.cacheBitmap.Get() == initialBitmap && probe->CountersAfterOwnerStopped().createSuccess == initialCreates.createSuccess
						&& probe->CountersAfterOwnerStopped().uploadCalls == initialCreates.uploadCalls,
						"B333 accepted goal reuses actual initial bitmap without diagnostic rebuild");
					options.dpi = 144; options.zoom = 1.5;
					const auto dpiChanged = drawSvg(fullClip, false);
					expect(transactionOk && dpiChanged.verified == 1 && probe->Observation(svgTag).used.dpi == 144
						&& probe->Observation(svgTag).used.pixelWidth == 48, "B334 changed typed DPI uses original real zoom/raster policy");
					options.dpi = 96; // 相同zoom与bitmap，只有proof的DPI不匹配时不得重建来凑数。
					const auto dpiMismatchBefore = probe->CountersAfterOwnerStopped();
					expect(drawSvg(fullClip, false).verified == 0 && probe->CountersAfterOwnerStopped().uploadCalls == dpiMismatchBefore.uploadCalls,
						"B335 DPI mismatch on reused bitmap remains unverified without extra upload");
					options = {};
					(void)drawSvg(fullClip, false);
					BarUiSVGClass second;
					{
						SvgObservationScope owned(probe.get(), false, true);
						second.Initialization(0,0,RGB(90,40,220),std::nullopt);
						second.InitializationFromString(LR"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24"><rect width="24" height="24" fill="rgba(10,0,7,0)"/></svg>)SVG");
						second.w.SetDirect(24); second.h.SetDirect(24); second.pct.SetDirect(1); second.enable.Initialization(true);
						expect(second.BindObservationTag(svgTag + 1), "B336 second owned SVG tag binds");
					}
					auto drawPair = [&](bool overlaps)
					{
						SvgObservationScope scope(probe.get());
						auto target = probe->Target(); target.revision = 1; target.frameAttemptSerial = ++attempt;
						probe->BeginFrame(1, currentEpoch.generation, attempt);
						svg.ObserveFiniteRequirement(true); second.ObserveFiniteRequirement(true);
						probe->BeginBackingWrite(dc, target); dc->BeginDraw(); dc->SetTransform(D2D1::IdentityMatrix());
						renderer.PushFrameDirtyClip(dc, fullClip); ObserveUi3SvgClear(dc); dc->Clear(D2D1::ColorF(0,0,0,0));
						const bool a = renderer.Svg(dc, svg, BarUiInheritClass(8,8));
						const bool b = renderer.Svg(dc, second, BarUiInheritClass(overlaps ? 24 : 64, overlaps ? 24 : 64));
						renderer.PopFrameDirtyClip(dc); const auto resource = probe->FinishDrawing();
						const bool ended = SUCCEEDED(dc->EndDraw()); probe->CompleteAttempt(ended);
						expect(a && b && ended, "B336 real two-SVG draw/EndDraw premise"); return resource;
					};
					const auto disjoint = drawPair(false);
					expect(disjoint.required == 2 && disjoint.verified == 2, "B336 actual disjoint SVG B preserves A full proof");
					const auto overlapping = drawPair(true);
					expect(overlapping.required == 2 && overlapping.verified == 1 && overlapping.unverified == 1
						&& overlapping.firstUnverifiedSvgTag == svgTag && probe->Observation(svgTag).use == Ui3SvgUse::Unverified,
						"B337 actual overlapping SVG B invalidates already submitted A");

					// B35：真实内嵌主logo的正常着色层叠必须完整证明；普通相交SVG/B337仍保守。
					{
						auto logos = std::make_unique<Ui3SvgProbe>(0xB350);
						BarUiSVGClass baseLogo, inkLogo;
						bool logosBound = false;
						{
							SvgObservationScope initialization(logos.get(), false, true);
							baseLogo.Initialization(0, 0, std::nullopt, std::nullopt); baseLogo.InitializationFromResource(L"UI", L"logo1");
							inkLogo.Initialization(0, 0, RGB(40, 170, 90), std::nullopt); inkLogo.InitializationFromResource(L"UI", L"Frame94");
							for (auto* logo : { &baseLogo, &inkLogo }) { logo->w.SetDirect(48); logo->h.SetDirect(48); logo->pct.SetDirect(1); logo->enable.Initialization(true); }
							logosBound = baseLogo.BindObservationTag(0x10000u) && inkLogo.BindObservationTag(0x10001u);
						}
						expect(logosBound, "B350 actual embedded logo1 and Frame94 owned tags bind");
						std::uint64_t logoAttempt = 0;
						auto paintLogos = [&](unsigned fault, bool observed = true)
						{
							SvgObservationScope scope(observed ? logos.get() : nullptr);
							Ui3SvgFrameTarget target; target.revision = 1; target.epoch = currentEpoch.generation; target.surfaceSerial = 1;
							target.frameAttemptSerial = ++logoAttempt; target.width = target.height = target.backingWidth = target.backingHeight = svgSize;
							target.dpi = 96; target.windowAlpha = 255; target.zoomBits = std::bit_cast<std::uint64_t>(1.0);
							for (unsigned i = 0; i < 4; ++i) target.viewportBits[i] = std::bit_cast<std::uint32_t>(i < 2 ? 0.0f : static_cast<float>(svgSize));
							if (observed) { logos->BeginFrame(1, currentEpoch.generation, logoAttempt); baseLogo.ObserveFiniteRequirement(true); inkLogo.ObserveFiniteRequirement(true); logos->BeginBackingWrite(dc, target); }
							dc->BeginDraw(); dc->SetTransform(D2D1::IdentityMatrix()); renderer.PushFrameDirtyClip(dc, fullClip);
							ObserveUi3SvgClear(dc); dc->Clear(D2D1::ColorF(0, 0, 0, 0));
							const auto savedInk = inkLogo.cacheBitmap;
							if (fault == 2 && observed) logos->DeclareMainLogoInkComposition(dc, &baseLogo, &inkLogo); // 底层还未提交，声明不可授权倒序。
							const bool first = renderer.Svg(dc, fault == 2 ? inkLogo : baseLogo, BarUiInheritClass(16, 16));
							if (fault == 3) { ObserveUi3SvgUnknownWrite(dc); dc->DrawBitmap(baseLogo.cacheBitmap.Get(), D2D1::RectF(72, 72, 88, 88), 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, nullptr); }
							if (observed && fault != 1 && fault != 2) logos->DeclareMainLogoInkComposition(dc, fault == 8 ? &inkLogo : &baseLogo, &inkLogo);
							if (fault == 4) inkLogo.cacheBitmap = baseLogo.cacheBitmap; // 真实错误bitmap对象，typed proof不能追认。
							if (fault == 5) inkLogo.angle.SetDirect(90);
							if (fault == 6)
							{
								// frame dirty入口不允许二次Push；nested反例必须真正压入D2D clip，并沿原producer记录。
								const auto clip = D2D1::RectF(16, 16, 30, 30);
								dc->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
								D2D1_MATRIX_3X2_F effective{}; dc->GetTransform(&effective);
								const float rect[4]{ clip.left, clip.top, clip.right, clip.bottom };
								const float matrix[6]{ effective._11, effective._12, effective._21, effective._22, effective._31, effective._32 };
								std::uint32_t r[4], m[6];
								for (unsigned i = 0; i < 4; ++i) r[i] = std::bit_cast<std::uint32_t>(rect[i]);
								for (unsigned i = 0; i < 6; ++i) m[i] = std::bit_cast<std::uint32_t>(matrix[i]);
								ObserveUi3SvgClipPush(dc, r, m);
							}
							const bool last = renderer.Svg(dc, fault == 2 ? baseLogo : inkLogo, BarUiInheritClass(16, 16));
							if (fault == 6) { ObserveUi3SvgClipPop(dc); dc->PopAxisAlignedClip(); }
							if (fault == 7) { ObserveUi3SvgUnknownWrite(dc); dc->DrawBitmap(baseLogo.cacheBitmap.Get(), D2D1::RectF(72, 72, 88, 88), 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, nullptr); }
							if (fault == 4) inkLogo.cacheBitmap = savedInk;
							inkLogo.angle.SetDirect(0);
							renderer.PopFrameDirtyClip(dc); const auto resources = observed ? logos->FinishDrawing() : Ui3FiniteResourceProof{};
							const bool ended = SUCCEEDED(dc->EndDraw()); if (observed) logos->CompleteAttempt(ended);
							expect(first && last && ended, "B350 real logo pair DrawBitmap/EndDraw premise");
							return resources;
						};
						const auto composed = paintLogos(0); const auto composedPixels = ReadSvgProofPixels(dc, renderer.GetTargetBitmap());
						(void)paintLogos(0, false); const auto ordinaryPixels = ReadSvgProofPixels(dc, renderer.GetTargetBitmap());
						expect(composed.required == 2 && composed.verified == 2 && composed.failed == 0 && composed.unverified == 0
							&& composedPixels.size() == svgSize * svgSize * 4 && composedPixels == ordinaryPixels,
							"B351 declared actual logo composition proves both resources with identical entire ordinary BGRA");
						for (unsigned fault = 1; fault <= 8; ++fault)
						{
							const auto beforeVariant = logos->CountersAfterOwnerStopped();
							const auto resources = paintLogos(fault); const auto ink = logos->Observation(0x10001u);
							const auto afterVariant = logos->CountersAfterOwnerStopped();
							report << "[Ui3B352] variant=" << fault << " required=" << resources.required << " verified=" << resources.verified
								<< " failed=" << resources.failed << " unverified=" << resources.unverified << " firstTag=" << resources.firstUnverifiedSvgTag
								<< " reason=" << resources.firstUnverifiedReason << " inkSemantic=" << ink.used.semanticKnown << " inkQuality=" << ink.qualityMatches
								<< " inkCoverage=" << static_cast<unsigned>(ink.coverage) << " createDelta=" << afterVariant.createSuccess - beforeVariant.createSuccess << '\n';
							expect(resources.verified < 2, "B352 undeclared/reversed/unknown/wrong-bitmap/transform/partial/wrong-object composition remains unverified");
						}
					}
					// 纯Shape/Superellipse/CLIP文字真实同序写入：不相交保持SVG，相交覆盖必须拒证。
					{
						BarUIRendering pure(&owner); pure.ConfigureLocalizedTypography(); pure.SetFrameZoom(1);
						options = {}; svg.enable.Initialization(true); svg.pct.SetDirect(1);
						const auto untouched = drawSvg(fullClip, false); const auto originalPixel = ReadEraserTestPixel(dc, renderer.GetTargetBitmap(), 32, 32);
						auto paintPure = [&](bool overlaps)
						{
							SvgObservationScope scope(probe.get()); auto target = probe->Target(); target.frameAttemptSerial = ++attempt;
							probe->BeginFrame(1, currentEpoch.generation, attempt); svg.ObserveFiniteRequirement(true); probe->BeginBackingWrite(dc, target);
							dc->BeginDraw(); dc->SetTransform(D2D1::IdentityMatrix()); renderer.PushFrameDirtyClip(dc, fullClip);
							ObserveUi3SvgClear(dc); dc->Clear(D2D1::ColorF(0, 0, 0, 0));
							BarUiShapeClass background(0, 0, 32, 32, 0, 0, 1, RGB(255, 255, 255), std::nullopt); background.enable.Initialization(true);
							bool painted = pure.Shape(dc, background, BarUiInheritClass(56, 56));
							painted = renderer.Svg(dc, svg, BarUiInheritClass(16, 16)) && painted;
							BarUiWordClass word(0, 0, 28, 28, L"文", 14, RGB(20, 20, 20)); word.enable.Initialization(true);
							painted = pure.Word(dc, word, BarUiInheritClass(60, 60)) && painted;
							BarUiSuperellipseClass shape(0, 0, 28, 28, 3, 1, RGB(255, 255, 255), std::nullopt); shape.enable.Initialization(true);
							painted = pure.Superellipse(dc, shape, BarUiInheritClass(overlaps ? 24 : 56, overlaps ? 24 : 56)) && painted;
							renderer.PopFrameDirtyClip(dc); const auto resources = probe->FinishDrawing();
							const bool ended = SUCCEEDED(dc->EndDraw()); probe->CompleteAttempt(ended);
							expect(painted && ended, "B353 actual Shape/SVG/CLIP Word/Superellipse same-order D2D premise"); return resources;
						};
						expect(untouched.verified == 1 && paintPure(false).verified == 1
							&& ReadEraserTestPixel(dc, renderer.GetTargetBitmap(), 32, 32) == originalPixel,
							"B354 actual disjoint pure writes retain SVG proof and visible pixel");
						const auto covered = paintPure(true);
						expect(covered.verified == 0 && covered.firstUnverifiedReason == static_cast<unsigned>(Ui3SvgProofReason::Overwrite)
							&& ReadEraserTestPixel(dc, renderer.GetTargetBitmap(), 32, 32) != originalPixel,
							"B355 actual overlapping pure geometry remains rejected with visibly covered SVG");
					}
					// Hidden域外资格仅沿同epoch/surface的实际mapped写域，未知重放及失败事务不能绕过。
					{
						auto hiddenProbe = std::make_unique<Ui3SvgProbe>(0xB356); BarUiSVGClass offViewport;
						{
							SvgObservationScope initialization(hiddenProbe.get(), false, true);
							offViewport.Initialization(0, 0, RGB(220, 110, 40), std::nullopt);
							offViewport.InitializationFromString(LR"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16"><rect width="16" height="16" fill="rgba(10,0,7,0)"/></svg>)SVG");
							offViewport.w.SetDirect(16); offViewport.h.SetDirect(16); offViewport.enable.Initialization(true); offViewport.pct.SetDirect(1);
							expect(offViewport.BindObservationTag(0x10002u), "B356 actual outside SVG owned tag binds");
						}
						std::uint64_t hiddenAttempt = 0;
						auto paintHidden = [&](bool visible, bool mappedInside = false, bool unknownReplay = false, bool committed = true, bool expandedViewport = false)
						{
							SvgObservationScope scope(hiddenProbe.get()); offViewport.enable.Initialization(visible); offViewport.pct.SetDirect(visible ? 1 : 0);
							Ui3SvgFrameTarget target; target.revision = 1; target.epoch = currentEpoch.generation; target.surfaceSerial = 1; target.frameAttemptSerial = ++hiddenAttempt;
							target.width = target.height = visible ? svgSize : expandedViewport ? 48 : 32; target.backingWidth = target.backingHeight = svgSize;
							target.dpi = 96; target.windowAlpha = 255; target.zoomBits = std::bit_cast<std::uint64_t>(1.0);
							for (unsigned i = 0; i < 4; ++i) target.viewportBits[i] = std::bit_cast<std::uint32_t>(
								i < 2 ? (expandedViewport ? 48.0f : 0.0f) : expandedViewport ? 96.0f : static_cast<float>(target.width));
							hiddenProbe->BeginFrame(1, currentEpoch.generation, hiddenAttempt); offViewport.ObserveFiniteRequirement(true); hiddenProbe->BeginBackingWrite(dc, target);
							dc->BeginDraw(); dc->SetTransform(D2D1::IdentityMatrix());
							// negative保留当前viewport中的旧图，不把“已清除”误作“域外”证据。
							renderer.PushFrameDirtyClip(dc, visible ? fullClip : mappedInside ? D2D1::RectF(32, 32, svgSize, svgSize) : D2D1::RectF(0, 0, 32, 32));
							ObserveUi3SvgClear(dc); dc->Clear(D2D1::ColorF(0, 0, 0, 0));
							if (visible) { if (mappedInside) dc->SetTransform(D2D1::Matrix3x2F::Translation(-56, -56)); (void)renderer.Svg(dc, offViewport, BarUiInheritClass(64, 64)); }
							if (unknownReplay) { ObserveUi3SvgUnknownWrite(dc); dc->DrawBitmap(offViewport.cacheBitmap.Get(), D2D1::RectF(8, 8, 24, 24), 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, nullptr); }
							dc->SetTransform(D2D1::IdentityMatrix()); renderer.PopFrameDirtyClip(dc); const auto resources = hiddenProbe->FinishDrawing();
							const bool ended = SUCCEEDED(dc->EndDraw()); hiddenProbe->CompleteAttempt(ended && committed);
							expect(ended, "B356 actual viewport/clear/mapped SVG/EndDraw premise"); return resources;
						};
						expect(paintHidden(true).verified == 1 && ReadEraserTestPixel(dc, renderer.GetTargetBitmap(), 72, 72)[3] != 0, "B356 old full mapped paint established");
						const auto outside = paintHidden(false);
						expect(outside.required == 1 && outside.verified == 1 && hiddenProbe->Observation(0x10002u).outsidePresentedViewport
							&& !hiddenProbe->Observation(0x10002u).clearCoversOldBounds && ReadEraserTestPixel(dc, renderer.GetTargetBitmap(), 72, 72)[3] != 0
							&& ReadEraserTestPixel(dc, renderer.GetTargetBitmap(), 16, 16)[3] == 0,
							"B357 known old SVG entirely outside actual viewport is proven absent there without claiming backing Clear");
						// 不重新绘制SVG，直接把非零源viewport扩回保留旧像素；必须仍Require并拒绝Hidden。
						const auto expanded = paintHidden(false, false, false, true, true);
						expect(expanded.required == 1 && expanded.verified == 0 && expanded.unverified == 1
							&& hiddenProbe->NeedsHiddenProof(0x10002u) && hiddenProbe->Observation(0x10002u).use != Ui3SvgUse::HiddenExpected
							&& std::bit_cast<float>(hiddenProbe->Target().viewportBits[0]) == 48.0f
							&& std::bit_cast<float>(hiddenProbe->Target().viewportBits[1]) == 48.0f
							&& ReadEraserTestPixel(dc, renderer.GetTargetBitmap(), 72, 72)[3] != 0,
							"B362 outside success retains actual old paint: no repaint, source48,48 expansion requires and rejects still-visible old SVG");
					// B363：沿用像素只在先前全覆盖提交、同内容/几何/透明度且本帧clip可知时允许。
					{
						auto retainedProbe = std::make_unique<Ui3SvgProbe>(0xB363);
						BarUiSVGClass retainedSvg;
						{
							SvgObservationScope initialization(retainedProbe.get(), false, true);
							retainedSvg.Initialization(0, 0, RGB(220, 110, 40), std::nullopt);
							retainedSvg.InitializationFromString(LR"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32"><rect width="32" height="32" fill="rgba(220,110,40,0.75)"/></svg>)SVG");
							retainedSvg.w.SetDirect(32); retainedSvg.h.SetDirect(32); retainedSvg.pct.SetDirect(1);
							retainedSvg.enable.Initialization(true);
							expect(retainedSvg.BindObservationTag(0x10003u), "B363 actual owned retained SVG tag binds");
						}
						std::uint64_t retainedAttempt = 0;
						auto paintRetained = [&](const D2D1_RECT_F& clip, bool unknownAfter = false, bool commit = true)
						{
							SvgObservationScope scope(retainedProbe.get()); renderer.SetFrameZoom(1.0);
							Ui3SvgFrameTarget target; target.revision = 1; target.epoch = currentEpoch.generation; target.surfaceSerial = 1;
							target.frameAttemptSerial = ++retainedAttempt; target.width = target.height = target.backingWidth = target.backingHeight = svgSize;
							target.dpi = 96; target.windowAlpha = 255; target.zoomBits = std::bit_cast<std::uint64_t>(1.0);
							for (unsigned i = 0; i < 4; ++i) target.viewportBits[i] = std::bit_cast<std::uint32_t>(i < 2 ? 0.0f : static_cast<float>(svgSize));
							retainedProbe->BeginFrame(1, currentEpoch.generation, retainedAttempt);
							retainedSvg.ObserveFiniteRequirement(true); retainedProbe->BeginBackingWrite(dc, target);
							dc->BeginDraw(); dc->SetTransform(D2D1::IdentityMatrix()); renderer.PushFrameDirtyClip(dc, clip);
							ObserveUi3SvgClear(dc); dc->Clear(D2D1::ColorF(0, 0, 0, 0));
							const bool drawn = renderer.Svg(dc, retainedSvg, BarUiInheritClass(16, 16));
							if (unknownAfter) { ObserveUi3SvgUnknownWrite(dc); dc->DrawBitmap(retainedSvg.cacheBitmap.Get(), D2D1::RectF(20, 20, 44, 44), 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, nullptr); }
							renderer.PopFrameDirtyClip(dc); const auto proof = retainedProbe->FinishDrawing();
							const bool ended = SUCCEEDED(dc->EndDraw()); retainedProbe->CompleteAttempt(ended && commit);
							expect(drawn && ended, "B363 actual same-bitmap D2D draw and EndDraw premise");
							return proof;
						};
						auto reportRetained = [&](const char* stage, const Ui3FiniteResourceProof& proof)
						{
							const auto observation = retainedProbe->Observation(0x10003u);
							report << "[Ui3B363] stage=" << stage << " required=" << proof.required << " verified=" << proof.verified
								<< " failed=" << proof.failed << " unverified=" << proof.unverified
								<< " reason=" << proof.firstUnverifiedReason << " coverage=" << static_cast<unsigned>(observation.coverage)
								<< " use=" << static_cast<unsigned>(observation.use) << " bitmap=" << observation.used.ready << ',' << observation.used.semanticKnown
								<< " quality=" << observation.qualityMatches << " opacity=" << std::bit_cast<float>(observation.finalOpacityBits)
								<< " dest=" << std::bit_cast<float>(observation.destBits[0]) << ',' << std::bit_cast<float>(observation.destBits[1])
								<< ',' << std::bit_cast<float>(observation.destBits[2]) << ',' << std::bit_cast<float>(observation.destBits[3]) << '\n';
						};
						const auto seeded = paintRetained(fullClip);
						const auto seedObservation = retainedProbe->Observation(0x10003u);
						reportRetained("seed", seeded);
						const auto referencePixels = ReadSvgProofPixels(dc, renderer.GetTargetBitmap());
						const auto disjoint = paintRetained(D2D1::RectF(60, 60, 90, 90));
						reportRetained("disjoint", disjoint);
						const auto disjointPixels = ReadSvgProofPixels(dc, renderer.GetTargetBitmap());
						const bool disjointPixelsSame = referencePixels.size() == svgSize * svgSize * 4 && disjointPixels == referencePixels;
						report << "[Ui3B363Pixels] stage=disjoint same=" << disjointPixelsSame << " bytes=" << disjointPixels.size() << '\n';
						expect(seeded.required == 1 && seeded.verified == 1 && seedObservation.use == Ui3SvgUse::DrawnVerified,
							"B363 seed is a fully visible, exact D2D SVG proof");
						expect(disjointPixelsSame, "B363 disjoint dirty Clear/D2D draw preserves exact previously committed BGRA");
						expect(disjoint.required == 1 && disjoint.verified == 1 && disjoint.unverified == 0
							&& retainedProbe->Observation(0x10003u).coverage == Ui3SvgCoverage::Empty
							&& retainedProbe->Observation(0x10003u).use == Ui3SvgUse::RetainedVerified,
							"B363 known disjoint dirty clip inherits only the exact previous full-coverage proof");
						const auto partial = paintRetained(D2D1::RectF(32, 0, 60, svgSize));
						reportRetained("partial", partial);
						const auto partialPixels = ReadSvgProofPixels(dc, renderer.GetTargetBitmap());
						const bool partialPixelsSame = partialPixels == referencePixels;
						report << "[Ui3B363Pixels] stage=partial same=" << partialPixelsSame << " bytes=" << partialPixels.size() << '\n';
						expect(partialPixelsSame, "B363 same-bitmap partial redraw recomposes exact previously committed BGRA");
						expect(partial.required == 1 && partial.verified == 0 && partial.unverified == 1
							&& retainedProbe->Observation(0x10003u).coverage == Ui3SvgCoverage::Partial
							&& retainedProbe->Observation(0x10003u).use == Ui3SvgUse::Unverified,
							"B363 partially overlapping dirty clip remains unverified even when D2D pixels happen to match");
						(void)paintRetained(fullClip);
						retainedSvg.pct.SetDirect(0.5);
						const auto changedOpacity = paintRetained(D2D1::RectF(60, 60, 90, 90));
						reportRetained("opacity", changedOpacity);
						expect(changedOpacity.verified == 0 && changedOpacity.unverified == 1,
							"B363 changed expected opacity cannot borrow previous full-coverage proof");
						retainedSvg.pct.SetDirect(1); (void)paintRetained(fullClip);
						const auto unknown = paintRetained(fullClip, true);
						reportRetained("unknown", unknown);
						expect(unknown.verified == 0 && unknown.unverified == 1,
							"B363 later unknown write still revokes visible SVG proof");
						(void)paintRetained(fullClip);
						const auto failedCommit = paintRetained(D2D1::RectF(60, 60, 90, 90), false, false);
						const auto afterFailedCommit = paintRetained(D2D1::RectF(60, 60, 90, 90));
						reportRetained("after-failed-commit", afterFailedCommit);
						expect(failedCommit.verified == 1 && afterFailedCommit.verified == 0 && afterFailedCommit.unverified == 1,
							"B363 failed presentation cannot leave a reusable retained-pixel proof");
					}
						// 消费者前提必须使用支持的closed-aux标记与偶数display serial，不能拿Unsupported伪作失败提交。
						Ui3FiniteSignature initial; initial.flags = 64; initial.stateMode = 1; initial.penColorRgb = RGB(10, 20, 30); initial.penWidthBits = std::bit_cast<std::uint32_t>(3.0f);
						initial.toolRevision = 1; initial.dpi = 96; initial.displaySerial = 2; initial.configZoomBits = std::bit_cast<std::uint64_t>(1.0); initial.validMask = Ui3FiniteRequiredMask;
						auto publication = std::make_unique<Ui3FinitePublication>(0xB358, initial); auto goal = initial; goal.flags ^= 1;
						auto mutation = publication->BeginMutation(1, Ui3FiniteScene::MainFold, 1); publication->MarkBusinessAccepted(mutation); publication->FinishAtRenderRequest(mutation, goal, 0);
						auto observer = std::make_unique<Ui3FiniteObserver>(*publication); Ui3FiniteAccepted accepted; const bool stable = observer->SnapshotAccepted(accepted); observer->BeginFrame(accepted, outside.epoch, outside.frameAttemptSerial);
						const bool consumed = observer->MarkConsumed(goal, 1, 1); for (unsigned role = 1; role <= 6; ++role) observer->ObserveProperty(static_cast<Ui3PropertyRole>(role), false, true);
						auto candidate = observer->SettleCandidate(outside.surfaceSerial, 32, 32); candidate = observer->FinalizeResources(candidate, outside);
						expect(stable && accepted.status == Ui3FiniteStatus::Accepted && consumed && candidate.settled, "B358 legal accepted/consumed/settled production premise");
						const auto completion = observer->CompleteAttempt(candidate, false, false, 0);
						report << "[Ui3B358] stable=" << stable << " status=" << static_cast<unsigned>(accepted.status) << " flags=" << accepted.signature.flags
							<< " display=" << accepted.signature.displaySerial << " revision=" << accepted.revision << " publication=" << accepted.publicationSerial
							<< " consumed=" << consumed << " settled=" << candidate.settled << " svgComplete=" << candidate.svgProofComplete
							<< " candidate=" << candidate.accepted.revision << ',' << candidate.epoch << ',' << candidate.surfaceSerial << ',' << candidate.frameAttemptSerial
							<< " resource=" << outside.revision << ',' << outside.epoch << ',' << outside.surfaceSerial << ',' << outside.frameAttemptSerial
							<< " required=" << outside.required << " verified=" << outside.verified << " producer=" << outside.producerPresent
							<< " complete=" << static_cast<unsigned>(completion) << " completedRevision=" << publication->CompletedRevision() << '\n';
						expect(candidate.svgProofComplete && completion == Ui3FiniteStatus::Pending && publication->CompletedRevision() == 0, "B358 actual staged outside proof cannot complete failed present transaction");
						(void)paintHidden(true, true); const auto visibleOld = paintHidden(false, true);
						expect(visibleOld.verified == 0 && !hiddenProbe->Observation(0x10002u).outsidePresentedViewport
							&& ReadEraserTestPixel(dc, renderer.GetTargetBitmap(), 16, 16)[3] != 0, "B359 actual mapped old bounds in viewport remain unverified");
						(void)paintHidden(true); const auto replay = paintHidden(false, false, true);
						expect(replay.verified == 0 && ReadEraserTestPixel(dc, renderer.GetTargetBitmap(), 16, 16)[3] != 0,
							"B360 actual unknown replay into viewport prevents outside Hidden qualification");
						(void)paintHidden(true); const auto failedOutside = paintHidden(false, false, false, false);
						const auto afterFailedOutside = paintHidden(false);
						expect(failedOutside.verified == 1 && afterFailedOutside.verified == 0,
							"B361 real D2D write plus failed/deferred software completion revokes later outside Hidden proof");
					}
					// 真实独立WARP/D2D device epoch，局部SVG显式Reset沿原资源释放函数。
					RenderPipeline::DeviceEpoch nextEpoch; nextEpoch.backend = RenderPipeline::Backend::Warp; nextEpoch.generation = epoch.generation + 1;
					const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0};
					HRESULT nextHr = D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,1,D3D11_SDK_VERSION,
						nextEpoch.d3dDevice.GetAddressOf(),&nextEpoch.featureLevel,nextEpoch.immediateContext.GetAddressOf());
					if (SUCCEEDED(nextHr)) nextHr = nextEpoch.d3dDevice.As(&nextEpoch.dxgiDevice);
					const auto factory = RenderPipeline::D2DFactory();
					if (SUCCEEDED(nextHr)) nextHr = factory ? factory->CreateDevice(nextEpoch.dxgiDevice.Get(),nextEpoch.d2dDevice.GetAddressOf()) : E_POINTER;
					expect(SUCCEEDED(nextHr), "B338 independent real WARP/D2D epoch premise");
					if (SUCCEEDED(nextHr))
					{
						{ SvgObservationScope reset(probe.get()); svg.ResetCache(); second.ResetCache(); }
						nextHr = renderer.EnsureDeviceResources(nextEpoch, svgSize, svgSize); dc = renderer.GetDeviceContext();
						expect(SUCCEEDED(nextHr) && dc, "B338 actual context/target recreate premise");
						if (SUCCEEDED(nextHr) && dc)
						{
							currentEpoch = nextEpoch; options = {}; options.surface = 2;
							const auto newEpoch = drawSvg(fullClip, false);
							expect(transactionOk && newEpoch.verified == 1 && probe->Observation(svgTag).used.epoch == nextEpoch.generation
								&& probe->Observation(svgTag).used.surfaceSerial == 2, "B338 typed cache proof follows actual independent epoch upload");
						}
					}
					// 未观察创建的真实旧cache不能由owned-tag绑定追認为known。
					BarUiSVGClass oldCache;
					oldCache.Initialization(0,0,std::nullopt,std::nullopt);
					oldCache.InitializationFromString(LR"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="20" height="20"><rect width="20" height="20" fill="red"/></svg>)SVG");
					oldCache.w.SetDirect(20); oldCache.h.SetDirect(20); oldCache.enable.Initialization(true); oldCache.pct.SetDirect(1);
					expect(oldCache.CacheBitmap(dc,20,20), "B339 real unobserved old-cache premise");
					auto oldProbe = std::make_unique<Ui3SvgProbe>(0xB339);
					{
						SvgObservationScope owned(oldProbe.get(),false,true);
						expect(oldCache.BindObservationTag(0x20000) && !oldCache.ObservedBitmapProof().semanticKnown
							&& oldProbe->CountersAfterOwnerStopped().unknownReadyEntries == 1, "B339 binding tag does not certify unknown old bitmap");
					}
					{
						SvgObservationScope scope(oldProbe.get()); auto target = probe->Target(); target.frameAttemptSerial = 1;
						oldProbe->BeginFrame(1,currentEpoch.generation,1); oldCache.ObserveFiniteRequirement(true); oldProbe->BeginBackingWrite(dc,target);
						dc->BeginDraw(); dc->SetTransform(D2D1::IdentityMatrix()); renderer.PushFrameDirtyClip(dc,fullClip);
						ObserveUi3SvgClear(dc); dc->Clear(D2D1::ColorF(0,0,0,0)); (void)renderer.Svg(dc,oldCache,BarUiInheritClass(16,16));
						renderer.PopFrameDirtyClip(dc); const auto unknownOld = oldProbe->FinishDrawing(); const bool ended = SUCCEEDED(dc->EndDraw()); oldProbe->CompleteAttempt(ended);
						expect(ended && unknownOld.required == 1 && unknownOld.verified == 0 && unknownOld.firstUnverifiedReason != 0,
							"B339 actual drawing of unknown old bitmap stays unverified");
					}
					{
						auto capacityProbe = std::make_unique<Ui3SvgProbe>(0xB340);
						auto objects = std::make_unique<std::array<BarUiSVGClass, Ui3SvgCapacity + 1>>();
						SvgObservationScope owned(capacityProbe.get(),false,true);
						bool first256 = true;
						for (std::size_t i = 0; i < objects->size(); ++i)
						{
							auto& item = (*objects)[i]; item.InitializationFromString(LR"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="1" height="1"><rect width="1" height="1"/></svg>)SVG");
							if (i < Ui3SvgCapacity) first256 &= item.BindObservationTag(0x20000 + static_cast<std::uint32_t>(i));
						}
						expect(first256 && !objects->back().BindObservationTag(0x10000), "B340 exact 256 mappings reject legal distinct 257th object");
						expect((*objects)[0].BindObservationTag(0x20000) && !(*objects)[1].BindObservationTag(0x20000)
							&& !objects->back().BindObservationTag(0x100FF) && !objects->back().BindObservationTag(0xFFFFFFFFu),
							"B341 duplicate object is idempotent; conflicting/unknown/huge tags reject before indexing");
						expect(!Ui3SvgProbe::CapacityEvictionApplicable && capacityProbe->CountersAfterOwnerStopped().capacityEvict == 0,
							"B341 per-object bitmap replacement is not capacity eviction");
					}
					report << "[Ui3SvgProofSize] record=" << sizeof(Ui3FiniteTargetRecord) << " candidate=" << sizeof(Ui3FiniteCandidate)
						<< " producer=" << sizeof(Ui3SvgProbe) << " publication=" << sizeof(Ui3FinitePublication) << " observer=" << sizeof(Ui3FiniteObserver) << '\n';
					report << "[Ui3SvgProof] firstRequired=" << full.required << " firstVerified=" << full.verified
						<< " coverage=" << static_cast<unsigned>(actual.coverage) << " parse=" << counts.parseCalls
						<< " raster=" << counts.rasterCalls << " upload=" << counts.uploadCalls << " readyBytes=" << counts.logicalReadyBytes << '\n';
				}
			}
			renderer.DiscardDeviceResources();
		}
		// 在独立 Scheduler 回调中测生产遮罩提交，TLS 诊断只属于该回调。
		{
			const auto output=std::filesystem::path(L"Build/eraser-b/exact-mask");
			std::filesystem::create_directories(output);
			std::ofstream samples(output/L"measurements.csv");
			samples<<"case,block,frames,mean_ms,slices,exact_hit,transform_fallback,parent_create\n";
			std::atomic_bool finished=false;
			RenderPipeline::Scheduler scheduler;
			const bool sinkReady=scheduler.SetDiagnosticsSink([](std::string_view){return true;});
			const bool started=sinkReady && scheduler.Start();
			const bool registered=started && scheduler.Register(RenderPipeline::Client::Bar,[&](const RenderPipeline::FrameContext&)
				{
					constexpr UINT width=512,height=256;
					BarUIRendering probe(&owner);
					BarUiShapeClass shape(0,0,210,90,16,16,2,std::nullopt,RGB(220,220,220));
					shape.enable.Initialization(true);shape.pct.SetDirect(1);
					shape.framePct.emplace(0);shape.frameLightPct.emplace(1);
					shape.frameRendering=BarUiFrameRenderingEnum::PointLight;
					BarUiFrameLightingSnapshot light{};
					light.primaryLight=D2D1::Point2F(200,125);
					light.primaryRadius=480;light.primaryLightVisible=true;
					light.edgeLightingEnabled=true;
					probe.SetFrameZoom(1);probe.SetFrameLightingSnapshot(light);
					auto* diagnostic=RenderPipeline::CurrentFrameDiagnostics();
					expect(diagnostic!=nullptr,"exact mask probe has production diagnostics");
					const std::array cases{
						std::pair{"identity",D2D1::Matrix3x2F::Identity()},
						std::pair{"integer",D2D1::Matrix3x2F::Translation(32.0F,24.0F)},
						std::pair{"fractional",D2D1::Matrix3x2F::Translation(32.5F,24.0F)}};
					for(const auto& [name,transform]:cases)
					{
						probe.DiscardDeviceResources();
						const HRESULT setup=probe.EnsureDeviceResources(RenderPipeline::GetDeviceEpoch(),width,height);
						expect(SUCCEEDED(setup),"exact mask probe target setup");
						if(FAILED(setup))continue;
						auto* dc=probe.GetDeviceContext();
						auto drawFrame=[&]()
							{
								dc->BeginDraw();dc->SetTransform(transform);
								dc->Clear(D2D1::ColorF(0.0F,0.0F,0.0F,0.0F));
								probe.PushFrameDirtyClip(dc,D2D1::RectF(0,0,static_cast<FLOAT>(width),static_cast<FLOAT>(height)));
								const bool drew=probe.Shape(dc,shape,BarUiInheritClass(100,80));
								probe.PopFrameDirtyClip(dc);
								const HRESULT hr=dc->EndDraw();probe.HandleFrameEndDrawResult(hr);
								return drew && SUCCEEDED(hr);
							};
						for(int warm=0;warm<16;++warm)
							expect(drawFrame(),"exact mask warmup frame draws");
						for(int block=0;block<11;++block)
						{
							const auto beforeSlices=diagnostic?diagnostic->light.slices:0;
							const auto beforeHits=diagnostic?diagnostic->light.exactHit:0;
							const auto beforeFallback=diagnostic?diagnostic->light.exactFallback[static_cast<size_t>(RenderPipeline::ExactFallback::Transform)]:0;
							const auto beforeCreate=diagnostic?diagnostic->light.roundedParentCreate:0;
							const auto began=std::chrono::steady_clock::now();
							for(int frame=0;frame<64;++frame)
								expect(drawFrame(),"exact mask measured frame draws");
							const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count()/64.0;
							const auto slices=diagnostic?diagnostic->light.slices-beforeSlices:0;
							const auto hits=diagnostic?diagnostic->light.exactHit-beforeHits:0;
							const auto fallbacks=diagnostic?diagnostic->light.exactFallback[static_cast<size_t>(RenderPipeline::ExactFallback::Transform)]-beforeFallback:0;
							expect(slices==64*(name==std::string_view("identity")?1:9),"exact mask production FillOpacityMask count");
							expect(hits==(name==std::string_view("identity")?64:0) && fallbacks==(name==std::string_view("identity")?0:64),"exact mask transform eligibility");
							samples<<name<<','<<block<<",64,"<<elapsed<<','
								<<slices<<','<<hits<<','<<fallbacks<<','
								<<(diagnostic?diagnostic->light.roundedParentCreate-beforeCreate:0)<<'\n';
						}
						ComPtr<ID2D1Bitmap1> readable;
						const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,probe.GetTargetBitmap()->GetPixelFormat());
						HRESULT hr=dc->CreateBitmap(D2D1::SizeU(width,height),nullptr,0,&properties,&readable);
						if(SUCCEEDED(hr))hr=readable->CopyFromBitmap(nullptr,probe.GetTargetBitmap(),nullptr);
						D2D1_MAPPED_RECT mapped{};if(SUCCEEDED(hr))hr=readable->Map(D2D1_MAP_OPTIONS_READ,&mapped);
						expect(SUCCEEDED(hr),"exact mask BGRA readback");
						if(SUCCEEDED(hr))
						{
							std::ofstream pixels(output/(std::string(name)+".bgra"),std::ios::binary);
							for(UINT y=0;y<height;++y)pixels.write(reinterpret_cast<const char*>(mapped.bits+y*mapped.pitch),width*4);
							expect(pixels.good(),"exact mask BGRA saved");readable->Unmap();
						}
					}
					probe.DiscardDeviceResources();finished.store(true,std::memory_order_release);
					return RenderPipeline::FrameResult::Idle;
				});
			expect(registered,"exact mask diagnostic scheduler starts");
			if(registered)
			{
				const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
				while(!finished.load(std::memory_order_acquire) && std::chrono::steady_clock::now()<deadline)
					std::this_thread::sleep_for(std::chrono::milliseconds(2));
				expect(finished.load(std::memory_order_acquire),"exact mask diagnostic scheduler finishes");
				scheduler.Unregister(RenderPipeline::Client::Bar);
			}
			if(started)scheduler.Stop();
		}
		// 生产 Scene 的 Widget 自持图标缓存必须随独立 WARP device epoch 重建。
		{
			const auto epochA=RenderPipeline::GetDeviceEpoch();
			RenderPipeline::DeviceEpoch epochB;
			epochB.backend=RenderPipeline::Backend::Warp;
			epochB.generation=epochA.generation+1;
			const D3D_FEATURE_LEVEL featureLevels[]{D3D_FEATURE_LEVEL_11_0};
			HRESULT warpHr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,
				D3D11_CREATE_DEVICE_BGRA_SUPPORT,featureLevels,1,D3D11_SDK_VERSION,
				epochB.d3dDevice.GetAddressOf(),&epochB.featureLevel,
				epochB.immediateContext.GetAddressOf());
			if(SUCCEEDED(warpHr))warpHr=epochB.d3dDevice.As(&epochB.dxgiDevice);
			const auto factory=RenderPipeline::D2DFactory();
			if(SUCCEEDED(warpHr))warpHr=factory
				? factory->CreateDevice(epochB.dxgiDevice.Get(),epochB.d2dDevice.GetAddressOf())
				: E_POINTER;
			expect(SUCCEEDED(warpHr),"independent WARP/D2D epoch initializes");

			BarSurfaceScene scene;
			BarSurfaceBackgroundSpec background;
			background.bounds={0,0,80,80};
			background.visible=false;
			std::array<BarSurfaceWidgetSpec,1> widgets{};
			widgets[0].id=1;
			widgets[0].bounds={5,5,75,75};
			widgets[0].iconResource=L"barSelect";
			widgets[0].iconSizeDip=32;
			widgets[0].useThemeColors=false;
			widgets[0].content=RGB(255,255,255);
			const bool configured=scene.Configure(background,widgets)
				&& scene.SetBounds({0,0,80,80},1.0F);
			expect(configured,"production Scene icon configures");
			const RECT presentation=scene.PresentationBounds();
			const UINT width=static_cast<UINT>(presentation.right-presentation.left);
			const UINT height=static_cast<UINT>(presentation.bottom-presentation.top);
			const auto frameTime=std::chrono::steady_clock::now();
			auto drawScene=[&](const RenderPipeline::DeviceEpoch& epoch,UINT targetWidth,
				UINT targetHeight,std::vector<unsigned char>& pixels)
			{
				if(FAILED(scene.EnsureDeviceResources(epoch,targetWidth,targetHeight)))return false;
				auto* context=scene.DeviceContext();
				if(!context)return false;
				context->BeginDraw();
				context->SetTransform(D2D1::Matrix3x2F::Identity());
				context->Clear(D2D1::ColorF(0,0,0,0));
				const bool rendered=scene.Render(context,frameTime).rendered;
				const HRESULT endDraw=context->EndDraw();
				scene.HandleFrameEndDrawResult(endDraw);
				if(!rendered || FAILED(endDraw))return false;
				ComPtr<ID2D1Image> image;
				context->GetTarget(&image);
				ComPtr<ID2D1Bitmap1> target;
				if(!image || FAILED(image.As(&target)))return false;
				ComPtr<ID2D1Bitmap1> readable;
				const auto props=D2D1::BitmapProperties1(
					D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
					target->GetPixelFormat());
				if(FAILED(context->CreateBitmap(target->GetPixelSize(),nullptr,0,
					&props,&readable)) || FAILED(readable->CopyFromBitmap(nullptr,target.Get(),nullptr)))
					return false;
				D2D1_MAPPED_RECT mapped{};
				if(FAILED(readable->Map(D2D1_MAP_OPTIONS_READ,&mapped)))return false;
				pixels.resize(static_cast<size_t>(targetWidth)*targetHeight*4);
				for(UINT y=0;y<targetHeight;++y)
					std::memcpy(pixels.data()+static_cast<size_t>(y)*targetWidth*4,
						mapped.bits+y*mapped.pitch,static_cast<size_t>(targetWidth)*4);
				readable->Unmap();
				return true;
			};
			if(SUCCEEDED(warpHr) && configured)
			{
				std::vector<unsigned char> baseline,afterRelease,preserved,
					afterImplicitRecreate,afterResize;
				const bool drewBaseline=drawScene(epochA,width,height,baseline);
				expect(drewBaseline,"Scene icon renders in first epoch");
				if(drewBaseline)
				{
					bool visible=false;
					for(size_t pixel=3;pixel<baseline.size();pixel+=4)
						visible|=baseline[pixel]!=0;
					expect(visible,"Scene SVG contributes visible pixels");
					scene.ReleaseDeviceResources();
					const bool drewAfterRelease=drawScene(epochB,width,height,afterRelease);
					expect(drewAfterRelease,"Scene icon renders after explicit release on new epoch");
					if(drewAfterRelease)
					{
						expect(afterRelease==baseline,"new epoch retains exact BGRA icon pixels");
						expect(scene.EnsureDeviceResources(epochA,0,height)==E_INVALIDARG,
						"invalid target does not replace working epoch");
						expect(drawScene(epochB,width,height,preserved)
						&& preserved==afterRelease,"failed setup preserves current Scene pixels");
						expect(drawScene(epochA,width,height,afterImplicitRecreate)
						&& afterImplicitRecreate==baseline,
						"implicit epoch recreation retains exact BGRA icon pixels");
						expect(drawScene(epochA,width+8,height+8,afterResize),
						"target resize recreates Scene resources");
					if(afterResize.size()==static_cast<size_t>(width+8)*(height+8)*4)
					{
						bool samePixels=true;
						for(UINT y=0;y<height && samePixels;++y)
							samePixels=std::memcmp(afterResize.data()+static_cast<size_t>(y)*(width+8)*4,
								baseline.data()+static_cast<size_t>(y)*width*4,
								static_cast<size_t>(width)*4)==0;
						expect(samePixels,"target resize retains exact visible BGRA pixels");
					}
				}
				}
			}
			scene.ReleaseDeviceResources();
		}
		failures += Inkeys::UI::PageControl::RunOffscreenTests();
		// 主按钮展开/反向与点击脉冲：分开量CPU路径和完整离屏绘制，像素读取不计入计时。
		{
			struct PathProbe : BarUIRendering
			{
				using BarUIRendering::BarUIRendering;
				using BarUIRendering::GetSuperellipseGeometry;
			};
			const auto output=std::filesystem::path(L"Build/eraser-b/ui3-path");
			std::filesystem::create_directories(output);
			std::ofstream samples(output/L"measurements.csv");
			samples<<"zoom,block,geometry_frames,geometry_mean_ms,draw_frames,draw_mean_ms,successful_draws\n";
			std::array<std::array<double,2>,48> frames{};
			BarUiValueClass exponent(3.0),size(BarMainButtonWidthDip);
			const BarUiCurveSpecClass pulse{BarUiCurveEnum::EaseOutBack,BarUiCurveEnum::EaseInBack,0.0,false};
			for(size_t frame=0;frame<frames.size();++frame)
			{
				if(frame==0 || frame==12 || frame==24)
				{
					exponent.SetTar(frame==12?3.0:10.0,0.4);
					size.SetTar(BarMainButtonWidthDip,0.4,BarMainButtonWidthDip*1.1,true,pulse);
				}
				const BarUiAnimationAdvanceContextClass advance{1.0/60.0,1.0,true,false};
				BarUiAdvanceAnimation(exponent,advance);BarUiAdvanceAnimation(size,advance);
				frames[frame]={static_cast<double>(size.val),static_cast<double>(exponent.val)};
			}
			for(const double zoom:{1.0,1.5})
			{
				constexpr UINT width=256,height=192;
				PathProbe renderer(&owner);
				const HRESULT setup=renderer.EnsureDeviceResources(RenderPipeline::GetDeviceEpoch(),width,height);
				expect(SUCCEEDED(setup),"UI3 path benchmark target setup");
				if(FAILED(setup))continue;
				renderer.SetFrameZoom(zoom);
				BarUiFrameLightingSnapshot light{};
				light.primaryLight=D2D1::Point2F(105,85);light.primaryRadius=480;
				light.primaryLightVisible=true;light.edgeLightingEnabled=true;
				renderer.SetFrameLightingSnapshot(light);
				BarUiSuperellipseClass shape(0,0,BarMainButtonWidthDip,BarMainButtonHeightDip,
					3.0,BarButtonFrameThicknessDip,RGB(245,245,245),RGB(200,200,200));
				shape.enable.Initialization(true);shape.framePct.emplace(0.4);
				shape.frameRendering=BarUiFrameRenderingEnum::PointLight;
				auto* dc=renderer.GetDeviceContext();
				auto drawFrame=[&](const std::array<double,2>& values)
				{
					shape.w.SetDirect(values[0]);shape.h.SetDirect(values[0]);shape.n->SetDirect(values[1]);
					dc->BeginDraw();dc->SetTransform(D2D1::IdentityMatrix());
					dc->Clear(D2D1::ColorF(0,0,0,0));
					renderer.PushFrameDirtyClip(dc,D2D1::RectF(0,0,width,height));
					const bool drew=renderer.Superellipse(dc,shape,BarUiInheritClass(40,32));
					renderer.PopFrameDirtyClip(dc);
					const HRESULT hr=dc->EndDraw();renderer.HandleFrameEndDrawResult(hr);
					return drew && SUCCEEDED(hr);
				};
				for(const auto& frame:frames)expect(drawFrame(frame),"UI3 path benchmark warm draw");
				for(int block=0;block<7;++block)
				{
					bool geometryOk=true;
					const auto geometryStart=std::chrono::steady_clock::now();
					for(int repeat=0;repeat<32;++repeat)for(const auto& frame:frames)
					{
						const auto extent=static_cast<FLOAT>(frame[0]*zoom);
						const int segments=std::clamp(static_cast<int>((frame[0]*zoom*2)/8.0),24,128);
						geometryOk &= renderer.GetSuperellipseGeometry(40*static_cast<FLOAT>(zoom),32*static_cast<FLOAT>(zoom),
							extent,extent,static_cast<FLOAT>(frame[1]),segments)!=nullptr;
					}
					const double geometryMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-geometryStart).count()/(32*frames.size());
					unsigned successful=0;
					const auto drawStart=std::chrono::steady_clock::now();
					for(int repeat=0;repeat<4;++repeat)for(const auto& frame:frames)successful+=drawFrame(frame)?1:0;
					const double drawMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-drawStart).count()/(4*frames.size());
					expect(geometryOk && successful==4*frames.size(),"UI3 path benchmark all geometry/draws succeed");
					samples<<zoom<<','<<block<<','<<32*frames.size()<<','<<geometryMs<<','<<4*frames.size()<<','<<drawMs<<','<<successful<<'\n';
				}
				std::ofstream pixels(output/(zoom==1.0?L"zoom-1.bgra":L"zoom-1.5.bgra"),std::ios::binary);
				for(const auto& frame:frames)
				{
					expect(drawFrame(frame),"UI3 path snapshot draws");
					const auto data=ReadSvgProofPixels(dc,renderer.GetTargetBitmap());
					expect(data.size()==width*height*4,"UI3 path full BGRA readback");
					pixels.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));
				}
				expect(samples.good() && pixels.good(),"UI3 path benchmark output saved");
				renderer.DiscardDeviceResources();
			}
		}
		owner.spec.DiscardDeviceResources();RenderPipeline::Shutdown();CoUninitialize();
		report<<"[EraserVisual] failures="<<failures<<'\n';return failures?1:0;
	}
}
