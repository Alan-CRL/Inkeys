module;
#include "../../../IdtMain.h"
#include <d2d1_1helper.h>
#include "../../../IdtState.h"
#include "../../../IdtConfiguration.h"
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
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <span>
#include <thread>
#include <vector>
#include <bit>
#include <limits>
#include <cmath>
#include "Bar.PresentationProbe.h"
#include "Bar.MainLayoutPolicy.h"
#include "../StartupPlacement.h"
#pragma comment(lib,"windowscodecs.lib")

module Inkeys.UI.Bar;
import :Main;
import :Scene;
import :Theme;
import Inkeys.UI.RenderPipeline;
import Inkeys.UI.PageControl;
import Inkeys.UI.Freeze;
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
	// 测试调用已有 Surface 屏幕光源映射，不增加产品注入接口。
	BarUiFrameLightingSnapshot ResolveBarSurfaceFrameLightingSnapshot(
		const BarSurfaceSharedLighting& lighting,
		const RECT& logicalBounds, LONG presentationOutset) noexcept;
	namespace
	{
		class WhiteboardButtonStyleFixture : public BarButtonSetClass
		{
		public:
			using BarButtonSetClass::UpdateWhiteboardButtonStyle;
		};
		class DragCursorLightFixture : public BarUISetClass
		{
		public:
			void SeedCursor(POINT screenPoint, POINT layoutScreenOrigin)
			{
				// 仅在离屏 fixture 中种入真实输入快照，测试期间不发送任何新鼠标消息。
				std::lock_guard lock(borderCursorLightMutex);
				borderCursorLightScreenPoint = D2D1::Point2F(
					static_cast<FLOAT>(screenPoint.x), static_cast<FLOAT>(screenPoint.y));
				borderCursorLightPoint = D2D1::Point2F(
					static_cast<FLOAT>(screenPoint.x - layoutScreenOrigin.x),
					static_cast<FLOAT>(screenPoint.y - layoutScreenOrigin.y));
				borderCursorLightSerial = 41;
				borderCursorInputAvailable = true;
				borderCursorLightReady = true;
				borderCursorTrackingState = BarBorderCursorTrackingStateEnum::Inside;
			}
			void SetAvailability(bool available, bool ready = true, bool nearRegion = true)
			{
				std::lock_guard lock(borderCursorLightMutex);
				borderCursorInputAvailable = available; borderCursorLightReady = ready;
				borderCursorLightNearVisibleRegion = nearRegion;
				borderCursorTrackingState = !available ? BarBorderCursorTrackingStateEnum::Dormant
					: nearRegion ? BarBorderCursorTrackingStateEnum::Inside : BarBorderCursorTrackingStateEnum::Grace;
			}
			void SetTranslationDistractors(POINT live, POINT presented)
			{
				std::lock_guard lock(directWindowDragMutex);
				directWindowDragTranslationX = live.x; directWindowDragTranslationY = live.y;
				// 渲染已吸收目标位移时，最后成功窗口仍可能保留不为零的实际残差。
				RebaseBottomDockPresentedWindow(presented);
			}
			void Prepare(POINT layoutScreenOrigin)
			{
				(void)spec.PrepareFrameLighting(1.0 / 60.0, layoutScreenOrigin,
					static_cast<int>(StateModeSelectEnum::IdtSelection), 0, 0, 0);
			}
		};

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
		std::filesystem::create_directories(L"Build/eraser-b");
		std::ofstream report("Build/eraser-b/offscreen-results.log");
		int failures=0;
		auto expect=[&](bool ok,const char* why){if(!ok){++failures;report<<"[EraserVisual] FAIL "<<why<<'\n';}};
		expect(SUCCEEDED(fontCollectionHr),"SC/TC font collection initializes");
		// screenPoint/serial 保持不变，验证直移吸收和显示原点变更不依赖下一次 WM_MOUSEMOVE。
		{
			const bool savedEdge = BarUiEdgeLightingEnabled;
			const bool savedDynamic = BarUiDynamicEdgeLightingEnabled;
			const double savedSpeed = BarUiAnimationSpeedRate;
			const bool savedAnimation = BarUiAnimationEnabled;
			BarUiEdgeLightingEnabled = true; BarUiDynamicEdgeLightingEnabled = true;
			BarUiAnimationSpeedRate = 1.0;
			BarUiAnimationEnabled = true;
			struct RebaseCase { POINT screen, beforeOrigin, afterOrigin; };
			const std::array cases{
				RebaseCase{{900,450},{300,0},{0,0}},
				RebaseCase{{-1240,400},{-2220,-180},{-1920,-360}},
				RebaseCase{{-1200,500},{1510,-890},{-1920,400}},
				RebaseCase{{-1250,260},{-1590,-135},{-1920,-300}},
				RebaseCase{{-1250,260},{-1590,-135},{-1960,-325}},
				RebaseCase{{450,300},{-100,120},{-100,120}}};
			for(const double zoom:{0.6,1.0,1.5,2.0})for(const auto& item:cases)
			{
				auto fixture = std::make_unique<DragCursorLightFixture>();
				fixture->spec.SetFrameZoom(zoom); fixture->SeedCursor(item.screen,item.beforeOrigin);
				for(int frame=0;frame<90;++frame)fixture->Prepare(item.beforeOrigin);
				const auto before = fixture->spec.SnapshotFrameLighting();
				const auto beforeBounds = fixture->spec.GetFrameCursorLightDamageBounds();
				const auto atScreen = [&](const BarUiFrameLightingSnapshot& light,POINT origin)
				{
					return std::abs(light.cursorLight.x + origin.x - item.screen.x)<0.01F
						&& std::abs(light.cursorLight.y + origin.y - item.screen.y)<0.01F;
				};
				expect(atScreen(before,item.beforeOrigin) && before.cursorLightVisible
					&& std::abs(before.cursorIntensity-1.0F)<0.0001F,
					"drag cursor baseline reaches full strength in the original layout domain");
				// 同帧捕获值故意与 live/最后成功呈现残差不同，不能在 Prepare 中重新取后两者。
				fixture->SetTranslationDistractors({730,-510},{-40,-25});
				fixture->Prepare(item.afterOrigin);
				const auto after = fixture->spec.SnapshotFrameLighting();
				const auto afterBounds = fixture->spec.GetFrameCursorLightDamageBounds();
				expect(atScreen(after,item.afterOrigin),
					"drag release rebases real Prepare cursor light without a new input serial");
				const bool originChanged = item.beforeOrigin.x!=item.afterOrigin.x || item.beforeOrigin.y!=item.afterOrigin.y;
				expect(fixture->spec.DidFrameCursorLightChange()==originChanged
					&& ((beforeBounds.left!=afterBounds.left || beforeBounds.top!=afterBounds.top)==originChanged)
					&& afterBounds.left<=after.cursorLight.x-after.cursorRadius
					&& afterBounds.right>=after.cursorLight.x+after.cursorRadius
					&& afterBounds.top<=after.cursorLight.y-after.cursorRadius
					&& afterBounds.bottom>=after.cursorLight.y+after.cursorRadius,
					"drag release local light change invalidates complete old and new damage bounds");
				const auto surfaceLight = [&](const BarUiFrameLightingSnapshot& light)
				{
					BarSurfaceSharedLighting shared;
					shared.cursorScreenX=light.cursorScreenLight.x;shared.cursorScreenY=light.cursorScreenLight.y;
					shared.cursorRadiusPixels=light.cursorRadius;shared.cursorIntensity=light.cursorIntensity;
					shared.cursorVisible=light.cursorLightVisible;shared.edgeLightingEnabled=light.edgeLightingEnabled;
					return ResolveBarSurfaceFrameLightingSnapshot(shared,{-2000,-500,-1600,-300},12);
				};
				const auto beforeSurface=surfaceLight(before),afterSurface=surfaceLight(after);
				expect(beforeSurface.cursorLight.x==afterSurface.cursorLight.x
					&& beforeSurface.cursorLight.y==afterSurface.cursorLight.y
					&& beforeSurface.cursorRadius==afterSurface.cursorRadius
					&& beforeSurface.cursorIntensity==afterSurface.cursorIntensity
					&& beforeSurface.cursorLightVisible==afterSurface.cursorLightVisible,
					"drag rebase keeps the real PageControl screen-light mapping continuous");
				bool stable = true;
				for(int frame=0;frame<120;++frame)
				{
					fixture->Prepare(item.afterOrigin);
					const auto light = fixture->spec.SnapshotFrameLighting();
					stable &= atScreen(light,item.afterOrigin) && light.cursorLightVisible
						&& std::abs(light.cursorIntensity-before.cursorIntensity)<0.0001F
						&& std::abs(light.cursorRadius-240.0*zoom)<0.001
						&& light.cursorScreenLight.x==before.cursorScreenLight.x
						&& light.cursorScreenLight.y==before.cursorScreenLight.y
						&& !fixture->spec.DidFrameCursorLightChange();
				}
				expect(stable,"stationary release stays continuous for 120 frames at each zoom");
				report<<"[DragCursorLight] zoom="<<zoom<<" origin="<<item.beforeOrigin.x<<','<<item.beforeOrigin.y
					<<" -> "<<item.afterOrigin.x<<','<<item.afterOrigin.y<<" local="<<before.cursorLight.x<<','<<before.cursorLight.y
					<<" -> "<<after.cursorLight.x<<','<<after.cursorLight.y<<" stable="<<stable<<'\n';
			}
			// 独立调用真实 Prepare/Shape，只保留第三光；用屏幕几何对齐同一边框像素。
			for(const double zoom:{0.6,1.0,1.5,2.0})
			{
				auto fixture=std::make_unique<DragCursorLightFixture>();
				fixture->spec.SetFrameZoom(zoom);fixture->SeedCursor({900,450},{300,0});
				for(int frame=0;frame<90;++frame)fixture->Prepare({300,0});
				expect(SUCCEEDED(fixture->spec.EnsureDeviceResources(RenderPipeline::GetDeviceEpoch(),512,512)),
					"drag third-light pixel target initializes");
				auto* dc=fixture->spec.GetDeviceContext();
				if(!dc)continue;
				BarUiShapeClass border(0,0,160,60,12,12,2,std::nullopt,RGB(255,255,255));
				border.enable.Initialization(true);border.pct.SetDirect(1);
				border.framePct.emplace(0);border.frameLightPct.emplace(1);
				border.frameRendering=BarUiFrameRenderingEnum::PointLight; border.framePrimaryLightEnabled=false;
				const auto draw=[&](POINT origin)
				{
					const auto light=fixture->spec.SnapshotFrameLighting();
					fixture->spec.SetFrameCursorLightLocalGeometry(light.cursorLight,D2D1::SizeF(light.cursorRadius,light.cursorRadius));
					// 几何从固定 screenPoint 派生，不能用待验证的局部光源反推形状而掩盖错误。
					const double left=(900.0-origin.x)/zoom-80.0,top=(450.0-origin.y)/zoom-20.0;
					dc->BeginDraw();dc->SetTransform(D2D1::Matrix3x2F::Translation(
						static_cast<FLOAT>(64.0-left*zoom),static_cast<FLOAT>(64.0-top*zoom)));
					dc->Clear(D2D1::ColorF(0,0,0,0));
					const bool drew=fixture->spec.Shape(dc,border,BarUiInheritClass(left,top));
					const HRESULT hr=dc->EndDraw();fixture->spec.HandleFrameEndDrawResult(hr);
					expect(drew && SUCCEEDED(hr),"real third-light border renders across drag release");
					return ReadSvgProofPixels(dc,fixture->spec.GetTargetBitmap());
				};
				const auto before=draw({300,0});
				fixture->SetTranslationDistractors({730,-510},{-40,-25});fixture->Prepare({0,0});
				const auto after=draw({0,0});
				unsigned maxDelta=0,lit=0;
				if(before.size()==after.size())for(size_t i=0;i<before.size();++i)
				{
					maxDelta=std::max(maxDelta,static_cast<unsigned>(std::abs(static_cast<int>(before[i])-after[i])));
					lit+=before[i]>0;
				}
				expect(before.size()==512*512*4 && after.size()==before.size() && lit>100 && maxDelta<=2,
					"isolated real third-light pixels remain continuous after absorption");
				fixture->SetAvailability(false);
				for(int frame=0;frame<90;++frame)fixture->Prepare({0,0});
				const auto withoutCursor=draw({0,0});
				const auto darkBytes=std::count_if(withoutCursor.begin(),withoutCursor.end(),[](unsigned char pixel){return pixel>0;});
				expect(withoutCursor.size()==after.size() && darkBytes==0 && lit>100,
					"third-light-off control is transparent while the isolated enabled border contributes pixels");
				report<<"[DragCursorPixels] zoom="<<zoom<<" lit="<<lit<<" maxDelta="<<maxDelta<<" offLit="<<darkBytes<<'\n';
				fixture->spec.DiscardDeviceResources();
			}
			// 保持既有 Prepare 门禁；这里只检验生产光照帧，不启动 Raw Input/timer/HWND 状态机。
			{
				auto fixture=std::make_unique<DragCursorLightFixture>();
				fixture->spec.SetFrameZoom(1);fixture->SeedCursor({900,450},{300,0});
				const auto settle=[&](POINT origin){for(int frame=0;frame<90;++frame)fixture->Prepare(origin);};
				settle({300,0});const auto inside=fixture->spec.SnapshotFrameLighting();
				fixture->SetAvailability(true,true,false);fixture->Prepare({0,0});
				const auto grace=fixture->spec.SnapshotFrameLighting();
				expect(grace.cursorLightVisible && grace.cursorIntensity==inside.cursorIntensity,
					"Grace outside the accepting region does not change third-light strength at release");
				fixture->SetAvailability(false);settle({0,0});
				expect(!fixture->spec.SnapshotFrameLighting().cursorLightVisible,"Dormant input fades the third light without new input serial");
				fixture->SetAvailability(true,false);settle({0,0});
				expect(!fixture->spec.SnapshotFrameLighting().cursorLightVisible,"unready cursor input remains hidden across a rebase");
				fixture->SetAvailability(true);settle({0,0});
				BarUiDynamicEdgeLightingEnabled=false;settle({-40,-25});
				expect(!fixture->spec.SnapshotFrameLighting().cursorLightVisible,"dynamic-light gate preserves its fade-out behavior");
				BarUiDynamicEdgeLightingEnabled=true;settle({-40,-25});
				BarUiAnimationEnabled=false;fixture->Prepare({0,0});
				expect(!fixture->spec.SnapshotFrameLighting().cursorLightVisible,"animation gate immediately hides third light during a rebase");
				BarUiAnimationEnabled=true;settle({0,0});
				BarUiEdgeLightingEnabled=false;fixture->Prepare({300,0});
				expect(!fixture->spec.SnapshotFrameLighting().edgeLightingEnabled
					&& !fixture->spec.SnapshotFrameLighting().cursorLightVisible,"total-light gate hides all light during a rebase");
			}
			BarUiAnimationSpeedRate=savedSpeed;
			BarUiEdgeLightingEnabled = savedEdge; BarUiDynamicEdgeLightingEnabled = savedDynamic;
			BarUiAnimationEnabled = savedAnimation;
		}
		auto& owner=barUISet;
		expect(I18n::load(1,L"JSON",L"zh-CN"),"default bundled translation loads");
		owner.spec.ConfigureLocalizedTypography();
		owner.barButtonSet.PresetInitialization();owner.barMedia.LoadFormat();
		// 同一生产 Word 路径对照独立推导的整数字号，不用截图观感代替最终像素尺寸验证。
		{
			// 只在离屏测试局部公开受保护的缓存检查入口，生产接口保持不变。
			struct FontTestRenderer : BarUIRendering
			{
				using BarUIRendering::BarUIRendering;
				using BarUIRendering::GetThicknessFineDialLabelLayout;
			};
			FontTestRenderer fonts(&owner), reference(&owner);
			const bool ready = SUCCEEDED(fonts.EnsureDeviceResources(RenderPipeline::GetDeviceEpoch(), 256, 128))
				&& SUCCEEDED(reference.EnsureDeviceResources(RenderPipeline::GetDeviceEpoch(), 256, 128));
			expect(ready, "font pixel policy creates offscreen targets");
			if (ready)
			{
				struct FontCase { FLOAT dpi, scaleX, scaleY; bool pixels; };
				for (const auto item : {FontCase{96,1,1,false}, FontCase{144,1.03F,0.91F,false},
					FontCase{192,0.95F,0.83F,false}, FontCase{192,1.03F,0.91F,true}})
				{
					constexpr double zoom = 0.6, contentScale = 0.85;
					const double deviceScale = item.scaleY * (item.pixels ? 1.0 : item.dpi / 96.0);
					const double expectedPixels = std::floor(13.0 * zoom * deviceScale);
					const double expectedSize = expectedPixels / deviceScale * contentScale;
					const auto draw = [&](BarUIRendering& renderer, bool allow, double logicalSize)
					{
						renderer.UpdateFontRenderingPolicy(allow); renderer.SetFrameZoom(zoom);
						auto* dc = renderer.GetDeviceContext(); dc->SetDpi(item.dpi, item.dpi);
						dc->SetUnitMode(item.pixels ? D2D1_UNIT_MODE_PIXELS : D2D1_UNIT_MODE_DIPS);
						dc->SetTransform(D2D1::IdentityMatrix());
						const float units = item.pixels ? 1.0F : 96.0F / item.dpi;
						renderer.BeginFrameDraw(dc, D2D1::RectF(0,0,256 * units,128 * units));
						dc->SetTransform(D2D1::Matrix3x2F::Scale(item.scaleX,item.scaleY)
							* D2D1::Matrix3x2F::Translation(3.25F,2.75F));
						BarUiWordClass word(0,0,180,70,L"字号 123",logicalSize,RGB(20,20,20));
						word.enable.Initialization(true); word.pct.SetDirect(1); word.contentScale=contentScale;
						const bool drew = renderer.Word(dc,word,{10,10},DWRITE_FONT_WEIGHT_NORMAL);
						renderer.FinishFrameDrawCommands(dc); dc->SetTransform(D2D1::IdentityMatrix());
						expect(drew && SUCCEEDED(renderer.EndFrameDraw(dc)), "font Word renders through the real D2D path");
						return ReadSvgProofPixels(dc,renderer.GetTargetBitmap());
					};
					const auto actual=draw(fonts,false,13.0);
					const auto expected=draw(reference,true,expectedSize/(zoom*contentScale));
					expect(!actual.empty() && actual==expected,
						"Word pixels retain continuous content scale around the projected stationary target");
					const auto fractional=draw(fonts,true,13.0);
					expect(fractional!=actual, "fractional preference preserves a distinct untruncated Word render");
					expect(draw(fonts,false,13.0)==actual, "font preference switches back without stale cached text");
				}
				auto* dc=fonts.GetDeviceContext(); dc->SetDpi(96,96); dc->SetUnitMode(D2D1_UNIT_MODE_DIPS);
				dc->SetTransform(D2D1::IdentityMatrix()); fonts.UpdateFontRenderingPolicy(false);
				auto* label=fonts.GetThicknessFineDialLabelLayout(15,0.63F,dc);
				expect(label && label->layout && static_cast<IDWriteTextFormat*>(label->layout.Get())->GetFontSize()==6.0F,
					"fine dial uses truncated pixels in its actual DWrite layout");
				expect(label==fonts.GetThicknessFineDialLabelLayout(15,0.63F,dc),
					"fine dial reuses the same effective font layout");
				dc->SetTransform(D2D1::Matrix3x2F::Scale(1.03F,0.91F));
				auto* scaled=fonts.GetThicknessFineDialLabelLayout(15,0.63F,dc);
				expect(scaled && scaled->layout
					&& std::abs(static_cast<IDWriteTextFormat*>(scaled->layout.Get())->GetFontSize()*0.91-5.0)<0.00001,
					"fine dial cache keys include final font size after external transform");
				fonts.UpdateFontRenderingPolicy(true);
				label=fonts.GetThicknessFineDialLabelLayout(15,0.63F,dc);
				expect(label && std::abs(static_cast<IDWriteTextFormat*>(label->layout.Get())->GetFontSize()-6.3F)<0.00001F,
					"fine dial refreshes layout when fractional fonts are enabled");
				fonts.UpdateFontRenderingPolicy(false);
				expect(fonts.ResolveFontSize(dc,0.5)==0.0F, "subpixel font is skipped rather than enlarged");
				dc->SetDpi(120,192); dc->SetTransform(D2D1::Matrix3x2F(1,0,0.6F,0.8F,0,0));
				const double rotatedScale=std::hypot(0.6*1.25,0.8*2.0);
				expect(std::abs(fonts.ResolveFontSize(dc,7.8)*rotatedScale-std::floor(7.8*rotatedScale))<0.00001,
					"font height includes both axes of rotated/sheared DPI mapping");

				// 真实公共按钮绘制覆盖按压目标和 FLOAT 当前矩阵抵消曾导致的 13px->12px 边界。
				{
					dc->SetDpi(96,96);dc->SetUnitMode(D2D1_UNIT_MODE_DIPS);dc->SetTransform(D2D1::IdentityMatrix());
					fonts.SetFrameZoom(1);fonts.UpdateFontRenderingPolicy(false);
					BarButtonClass button;
					button.button.Initialization(0,0,80,80,4,4,1,RGB(240,240,240),RGB(20,20,20));
					button.button.enable.Initialization(true);button.button.pct.SetDirect(1);
					button.name.Initialization(0,0,70,24,L"Press",13,RGB(20,20,20));
					button.name.enable.Initialization(true);button.name.pct.SetDirect(1);
					BarButtonDrawOptions options;options.drawIcon=false;
					auto time=std::chrono::steady_clock::now();
					const auto drawButton=[&](double dt){
						time+=std::chrono::nanoseconds(1)+std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(dt));
						fonts.BeginFontFrame(time,{dt,1,true,false},1);fonts.SetFontTargetScale(1);
						dc->SetTransform(D2D1::IdentityMatrix());fonts.BeginFrameDraw(dc,D2D1::RectF(0,0,256,128));
						expect(DrawBarButtonVisual(fonts,dc,button,{10,10},options),"real font press button draws");
						const double first=button.name.fontCorrection.Current();
						(void)DrawBarButtonVisual(fonts,dc,button,{10,10},options);
						expect(button.name.fontCorrection.Current()==first,"duplicate real button draw advances font once");
						fonts.FinishFrameDrawCommands(dc);expect(SUCCEEDED(fonts.EndFrameDraw(dc)),"font press frame commits D2D commands");
						return 13.0*static_cast<double>(button.pressScale.val)*first;
					};
					for(const double current:{1.0,0.95,0.96,0.99,1.01,0.9,1.0}){
						button.pressScale.SetDirect(current);button.pressScale.SetTar(1,0.2);(void)drawButton(0);
						expect(button.name.fontCorrection.Target()==1.0,"release target stays exactly 13px across FLOAT press matrices");
					}
					button.pressScale.SetDirect(1);button.pressScale.SetTar(0.95,0.2);
					double previous=13;int intermediate=0;
					for(int tick=0;tick<30;++tick){
						if(!button.pressScale.IsSame())(void)BarUiAdvanceAnimation(button.pressScale,{0.01,1,true,false});
						const double pixel=drawButton(0.01);intermediate+=std::abs(pixel-std::round(pixel))>0.00001;
						expect(std::abs(pixel-previous)<0.2,"real pressed font follows a continuous fractional trajectory");previous=pixel;
					}
					expect(intermediate>10&&std::abs(previous-12.0)<0.00001&&!fonts.FontAnimationsActive(),
						"pressed hold settles at floor(13*0.95) and font scheduler becomes idle");
					const double held=previous;button.pressScale.SetTar(1,0.2);
					expect(std::abs(drawButton(0)-held)<0.00001,"release retarget at zero dt preserves current visible font");
					for(int tick=0;tick<30;++tick){
						if(!button.pressScale.IsSame())(void)BarUiAdvanceAnimation(button.pressScale,{0.01,1,true,false});
						previous=drawButton(0.01);
					}
					expect(std::abs(previous-13.0)<0.00001&&std::abs(drawButton(0)-previous)<0.00001,
						"real release ends at 13px without an idle-frame floor jump");
					// 字号目标不变时，内容过渡可连续经过小数及 Back 回弹；末尾只回到同一个 c。
					button.name.contentScale=0.83;const double c=button.name.fontCorrection.Current();(void)drawButton(0.01);
					expect(button.name.fontCorrection.Current()==c,"content midpoint does not retarget stationary font correction");
					button.name.contentScale=1.03;(void)drawButton(0.01);button.name.contentScale=1;(void)drawButton(0.01);
					expect(button.name.fontCorrection.Current()==c,"content overshoot and final frame keep continuous correction");
				}


				// 更多的派生尺寸逐帧变化，但文字必须从首帧就使用最终字号目标。
				for (const auto kind : {BarButtonVisualLayoutKind::StandardTwoTwo,
					BarButtonVisualLayoutKind::StandardTwoOne})
				for (const double zoom : {1.0, 0.6})
				for (const bool allowFractional : {false, true})
				{
					const double nominal=ResolveBarButtonVisualMetrics(kind).primaryFontSizeDip;
					const double expected=allowFractional?nominal*zoom:std::floor(nominal*zoom);
					const double duration=BarUiDefaultOperationDur,dt=duration/32.0;
					fonts.SetFrameZoom(zoom);fonts.UpdateFontRenderingPolicy(allowFractional);
					dc->SetDpi(96,96);dc->SetUnitMode(D2D1_UNIT_MODE_DIPS);dc->SetTransform(D2D1::IdentityMatrix());
					BarButtonClass button;
					button.button.Initialization(0,0,70,70,4,4,1,RGB(240,240,240),RGB(20,20,20));
					button.button.enable.Initialization(true);button.button.pct.SetDirect(1);
					button.name.Initialization(0,0,70,24,kind==BarButtonVisualLayoutKind::StandardTwoTwo?L"设置":L"组件",nominal,RGB(20,20,20));
					button.name.enable.Initialization(true);button.name.pct.SetDirect(1);
					BarButtonDrawOptions options;options.drawIcon=false;
					auto time=std::chrono::steady_clock::now();
					const auto drawFrame=[&](double elapsed){
						time+=std::chrono::nanoseconds(1)+std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(elapsed));
						fonts.BeginFontFrame(time,{elapsed,1,true,false},zoom);fonts.SetFontTargetScale(1);
						dc->SetTransform(D2D1::IdentityMatrix());fonts.BeginFrameDraw(dc,D2D1::RectF(0,0,256,128));
						expect(DrawBarButtonVisual(fonts,dc,button,{10,10},options),"More label draws through the production button path");
						const double size=fonts.ResolveWordFontSize(dc,button.name,button.name.size.val*zoom);
						fonts.FinishFrameDrawCommands(dc);expect(SUCCEEDED(fonts.EndFrameDraw(dc)),"More font frame commits D2D commands");
						return size;
					};
					BarUiValueClass scale{0.01};
					const BarUiCurveSpecClass opening{BarUiCurveEnum::EaseOutBack,BarUiCurveEnum::EaseOutBack,0,false};
					const BarUiCurveSpecClass closing{BarUiCurveEnum::EaseInBack,BarUiCurveEnum::EaseInBack,0,false};
					const auto moreFrame=[&](double elapsed){
						ConfigureBarButtonVisualTargets(button,kind,duration,scale.val,true,std::nullopt,scale.tar);
						return drawFrame(elapsed);
					};
					// 隐藏布局不消费字体修正；覆盖首次可见尺寸尚不足 1px 的边界。
					ConfigureBarButtonVisualTargets(button,kind,duration,0.01,true,std::nullopt,0.01);
					scale.SetTar(1,duration,std::nullopt,false,opening);
					expect(std::abs(moreFrame(0)-expected*0.01)<0.00001,"first visible More label projects the final size even below one pixel");
					bool overshoot=false;
					double settled=0;
					for(int tick=0;tick<36;++tick){
						if(!scale.IsSame())(void)BarUiAdvanceAnimation(scale,{dt,1,true,false});
						overshoot|=scale.val>1;settled=moreFrame(dt);
						expect(std::abs(settled-expected*static_cast<double>(scale.val))<0.00001,
							"More opening retains its raw scale curve without a second font-growth animation");
					}
					expect(overshoot&&scale.IsSame()&&!fonts.FontAnimationsActive()
						&&std::abs(settled-expected)<0.00001&&moreFrame(0)==settled,
						"More opening and overshoot settle at the final font size with no idle-frame growth");
					scale.SetTar(0.16,duration,std::nullopt,false,closing);
					for(int tick=0;tick<8;++tick){(void)BarUiAdvanceAnimation(scale,{dt,1,true,false});(void)moreFrame(dt);}
					const double interrupted=moreFrame(0);
					scale.SetTar(1,duration,std::nullopt,false,opening);
					expect(std::abs(moreFrame(0)-interrupted)<0.00001,"reopening More at zero dt preserves the current label size");
					for(int tick=0;tick<36;++tick){
						if(!scale.IsSame())(void)BarUiAdvanceAnimation(scale,{dt,1,true,false});
						settled=moreFrame(dt);
					}
					expect(!fonts.FontAnimationsActive()&&std::abs(settled-expected)<0.00001&&moreFrame(0)==settled,
						"reversed More opening has no font growth after the panel settles");
					// 同一溢出按钮回到主栏时，不得沿用紧凑面板的静止字号覆盖。
					scale.SetDirect(0.16);(void)moreFrame(duration);
					ConfigureBarButtonVisualTargets(button,kind,duration,1,false);
					button.name.size.SetDirect(button.name.size.tar);
					expect(button.name.fontTargetSize<0&&std::abs(drawFrame(duration)-expected)<0.00001,
						"moving a More button back to the main bar restores its ordinary font target");
				}
			}
			fonts.DiscardDeviceResources(); reference.DiscardDeviceResources();
		}

		// 早期 CLI 未读取配置；准备已规范化的内存布局，避免 Load 触发迁移写盘。
		auto a2Fixture=Inkeys::config.UI.Bar.FixedButtonsA2.Snapshot();
		for(auto& entry:a2Fixture)
			if(entry.Id==Inkeys::BarButtonId::Freeze)entry.Size=Inkeys::BarButtonSizeKind::TwoTwo;
		Inkeys::config.UI.Bar.FixedButtonsA2.Replace(std::move(a2Fixture));
		owner.barButtonSet.Load();
		// UpdateRendering 在 Pen 状态还会刷新粗细文字；离屏 fixture 不加载整套窗口控件。
		owner.wordMap[BarUISetWordEnum::DrawAttributeBar_ThicknessDisplay] =
			std::make_shared<BarUiWordClass>(0.0,0.0,0.0,0.0,L"",13.0);
		// 直接覆盖生产 PositionUpdate 的策略门禁，不能让后续刷新重新按中轴覆盖空间模式。
		{
			const int savedDirection = Inkeys::config.UI.Bar.ExpandDirectionAdjustment.load();
			const auto savedMainButton = owner.superellipseMap[BarUISetSuperellipseEnum::MainButton];
			const auto savedPlacement = owner.startupPlacement;
			const bool savedFold = owner.barState.fold;
			const bool savedSide = owner.barState.widgetPosition.mainBar;
			const bool savedPrimary = owner.barState.widgetPosition.primaryBar;
			const auto savedWidth = owner.barWindow.w, savedHeight = owner.barWindow.h;
			const auto mainButton = std::make_shared<BarUiSuperellipseClass>(800,500,80,80,10.0,1.0,std::nullopt,std::nullopt);
			owner.superellipseMap[BarUISetSuperellipseEnum::MainButton] = mainButton;
			// 早期 CLI 尚未启动交互/渲染，复用公开启动入口准备浮动状态。
			(void)owner.InitializeStartupPlacement(Inkeys::UI::StartupPlacement::BottomLeftFloating);
			expect(!owner.IsBottomDockLayoutLocked(),"PositionUpdate fixture initializes floating state through the production startup entry");
			owner.barWindow.w = 1000; owner.barWindow.h = 1000;
			Inkeys::config.UI.Bar.ExpandDirectionAdjustment = 0;
			owner.barState.widgetPosition.mainBar = true; owner.barState.PositionUpdate(1.0);
			expect(!owner.barState.widgetPosition.mainBar,"explicit centerline setting retains the legacy right-half classification");
			Inkeys::config.UI.Bar.ExpandDirectionAdjustment = 1;
			owner.barState.widgetPosition.mainBar = true;
			for(int frame=0;frame<120;++frame)owner.barState.PositionUpdate(1.0);
			expect(owner.barState.widgetPosition.mainBar,"space strategy survives repeated production PositionUpdate across the centerline");
			mainButton->x.SetDirect(200);mainButton->y.SetDirect(100);owner.barState.widgetPosition.mainBar = false;
			owner.barState.PositionUpdate(1.0);
			expect(!owner.barState.widgetPosition.mainBar && owner.barState.widgetPosition.primaryBar,
				"space strategy keeps the horizontal side while retaining the original upper/lower reversal");
			owner.superellipseMap[BarUISetSuperellipseEnum::MainButton] = savedMainButton;
			(void)owner.InitializeStartupPlacement(savedPlacement);
			owner.barState.fold = savedFold;
			owner.barState.widgetPosition.mainBar = savedSide;owner.barState.widgetPosition.primaryBar = savedPrimary;
			owner.barWindow.w = savedWidth;owner.barWindow.h = savedHeight;
			Inkeys::config.UI.Bar.ExpandDirectionAdjustment = savedDirection;
		}
		// 真实设置入口在无内容变化且工具不变时，也必须同步主栏显隐和清空状态。
		SetContentStateUpdatesReady(true);
		ChangeStateModeToPen();owner.UpdateRendering();
		auto* selectModeButton=owner.barButtonSet.preset[(int)BarButtonPresetEnum::Select];
		auto* pierceModeButton=owner.barButtonSet.preset[(int)BarButtonPresetEnum::Pierce];
		auto* freezeModeButton=owner.barButtonSet.preset[(int)BarButtonPresetEnum::Freeze];
		auto* clearModeButton=owner.barButtonSet.preset[(int)BarButtonPresetEnum::Clean];
		std::vector<BarButtonClass*> stableButtons;
		for(int index=0;index<owner.barButtonSet.tot;++index)
			stableButtons.push_back(owner.barButtonSet.buttonList.Get(index));
		const auto pierceIndex=std::find(stableButtons.begin(),stableButtons.end(),pierceModeButton);
		expect(pierceIndex!=stableButtons.end() && std::count(stableButtons.begin(),stableButtons.end(),pierceModeButton)==1
			&& pierceIndex+1!=stableButtons.end() && *(pierceIndex+1)==freezeModeButton,
			"hidden Pierce is one stable runtime member immediately before Freeze");
		const auto expectStableButtons=[&]()
		{
			bool same=owner.barButtonSet.tot==static_cast<int>(stableButtons.size());
			for(size_t index=0;index<stableButtons.size() && same;++index)
				same=owner.barButtonSet.buttonList.Get(static_cast<int>(index))==stableButtons[index];
			expect(same,"mode, content and Freeze changes retain hidden Pierce identity and list order");
		};
		const bool changedToSeparate=SetSelectAndClickThroughMode(1);
		selectModeButton->name.AdvanceContentTransition(1.0,1.0);
		expect(changedToSeparate && GetStateModeSnapshot().StateModeSelect==StateModeSelectEnum::IdtPen
			&& pierceModeButton->IsVisible() && pierceModeButton->size==BarButtonSizeEnum::twoOne
			&& freezeModeButton->size==BarButtonSizeEnum::twoOne
			&& selectModeButton->name.content.GetTar()==IW(I18nKey.UI.Bar.MainButtons.SelectClearLabel),
			"setting mode updates compact buttons while tool and empty content stay unchanged");
		// 复用设置的真实组件同步链；业务 key 不变时不能覆写分离式的有效半高网格。
		owner.barButtonSet.RegisterBuiltInComponents();
		const bool savedExplorer=setlist.component.shortcutButton.appliance.explorer;
		setlist.component.shortcutButton.appliance.explorer=false;
		owner.barButtonSet.SyncLegacyExtensionButtons();owner.UpdateRendering();
		setlist.component.shortcutButton.appliance.explorer=true;
		owner.barButtonSet.SyncLegacyExtensionButtons();owner.UpdateRendering();
		std::vector<BarButtonClass*> componentButtons;
		for(int index=0;index<owner.barButtonSet.tot;++index)
			componentButtons.push_back(owner.barButtonSet.buttonList.Get(index));
		const auto componentPierce=std::find(componentButtons.begin(),componentButtons.end(),pierceModeButton);
		expect(pierceModeButton->IsVisible() && freezeModeButton->IsVisible()
			&& pierceModeButton->size==BarButtonSizeEnum::twoOne && freezeModeButton->size==BarButtonSizeEnum::twoOne,
			"component reload retains Separate drawing Pierce and Freeze as twoOne");
		expect(componentPierce!=componentButtons.end() && componentPierce+1!=componentButtons.end()
			&& *(componentPierce+1)==freezeModeButton && std::count(componentButtons.begin(),componentButtons.end(),pierceModeButton)==1,
			"component reload preserves stable Pierce/Freeze identities and adjacency");
		expect(componentButtons.size()==stableButtons.size()+1,
			"real Explorer component switch adds one toolbar member");
		setlist.component.shortcutButton.appliance.explorer=savedExplorer;
		owner.barButtonSet.SyncLegacyExtensionButtons();owner.UpdateRendering();
		expect(SetSelectAndClickThroughMode(0) && GetStateModeSnapshot().StateModeSelect==StateModeSelectEnum::IdtPen
			&& !pierceModeButton->IsVisible() && freezeModeButton->size==BarButtonSizeEnum::twoTwo,
			"combined setting immediately hides Pierce and enlarges Freeze");
		// 直接执行生产 Pierce 回调，验证按钮切换身份和三类记忆工具恢复。
		SetSelectAndClickThroughMode(1);
		for(const auto tool:{StateModeSelectEnum::IdtPen,StateModeSelectEnum::IdtShape,StateModeSelectEnum::IdtEraser})
		{
			if(tool==StateModeSelectEnum::IdtPen)ChangeStateModeToPenTool(PenToolSelectionEnum::Highlighter);
			else if(tool==StateModeSelectEnum::IdtShape){ChangeStateModeToShape();SetShapeModeSelect(ShapeModeSelectEnum::IdtShapeRectangle1);}
			else ChangeStateModeToEraser();
			owner.UpdateRendering();
			const auto annotation=GetStateModeSnapshot();
			const auto revision=StateModeTransitionRevision();
			freezeModeButton->clickFunc();
			expect(Freeze::IsActive(),"Freeze fixture is active before the Pierce button click");
			pierceModeButton->clickFunc();owner.UpdateRendering();expectStableButtons();
			expect(GetStateModeSnapshot().StateModeSelect==StateModeSelectEnum::IdtClickThrough &&
				StateModeTransitionRevision()==revision+1 && !Freeze::IsActive() &&
				pierceModeButton->state->state==BarWidgetState::Selected,
				"production Pierce callback enters click-through and selects the same button");
			pierceModeButton->clickFunc();owner.UpdateRendering();expectStableButtons();
			const auto restored=GetStateModeSnapshot();
			const auto restoredPreset=tool==StateModeSelectEnum::IdtPen ? BarButtonPresetEnum::Draw :
				tool==StateModeSelectEnum::IdtShape ? BarButtonPresetEnum::Geometry : BarButtonPresetEnum::Eraser;
			expect(restored.StateModeSelect==tool && restored.lastAnnotationMode==tool &&
				restored.laserActive==annotation.laserActive && restored.Pen.ModeSelect==annotation.Pen.ModeSelect &&
				restored.Shape.ModeSelect==annotation.Shape.ModeSelect && StateModeTransitionRevision()==revision+2 &&
				!Freeze::IsActive() && pierceModeButton->state->state!=BarWidgetState::Selected &&
				owner.barButtonSet.preset[(int)restoredPreset]->state->state==BarWidgetState::Selected,
				"production Pierce callback restores the remembered tool and its selected button without Freeze");
			pierceModeButton->clickFunc();owner.UpdateRendering();
			expect(GetStateModeSnapshot().StateModeSelect==StateModeSelectEnum::IdtClickThrough &&
				pierceModeButton->state->state==BarWidgetState::Selected,"same Pierce callback can reenter click-through");
			pierceModeButton->clickFunc();owner.UpdateRendering();
			expect(GetStateModeSnapshot().StateModeSelect==tool && StateModeTransitionRevision()==revision+4 &&
				!Freeze::IsActive(),"same Pierce callback can exit again with one revision per click");
		}
		SetSelectAndClickThroughMode(0);
		ChangeStateModeToSelection();owner.UpdateRendering();
		expect(clearModeButton->state->state==BarWidgetState::Disable,
			"empty Selection Clear uses unified disabled state");
		ChangeStateModeToPen();owner.UpdateRendering();
		expect(clearModeButton->state->state!=BarWidgetState::Disable,"empty drawing Clear can enter Selection");
		SetSelectAndClickThroughMode(1);ChangeStateModeToClickThrough();owner.UpdateRendering();
		expect(pierceModeButton->state->state==BarWidgetState::Selected &&
			clearModeButton->state->state!=BarWidgetState::Disable,"Click-through highlight and empty Clear are active");
		owner.barButtonSet.ExecuteClearClick(false);owner.UpdateRendering();
		expect(GetStateModeSnapshot().StateModeSelect==StateModeSelectEnum::IdtSelection &&
			clearModeButton->state->state==BarWidgetState::Disable && !pierceModeButton->IsVisible(),
			"empty Click-through Clear enters Selection and removes hidden Pierce hit target");
		SetPptPresentationActive(true);Freeze::SetPresentationActive(true);owner.UpdateRendering();
		expect(!freezeModeButton->IsVisible() && !pierceModeButton->IsVisible(),
			"PPT empty Selection hides both A2 interaction buttons");
		ChangeStateModeToPen();owner.UpdateRendering();
		expect(!freezeModeButton->IsVisible() && pierceModeButton->IsVisible() &&
			pierceModeButton->size==BarButtonSizeEnum::twoTwo,"PPT Pierce fills hidden Freeze slot");
		Freeze::SetPresentationActive(false);SetPptPresentationActive(false);
		SetSelectAndClickThroughMode(0);ChangeStateModeToSelection();owner.UpdateRendering();
		// 用真实两行按钮与位置动画检查 X 策略，隐藏锚点仍共用当前批次目标。
		for(int order=0;order<4;++order)for(bool opensRight:{false,true})
		{
			SetSelectAndClickThroughMode(1);ChangeStateModeToPen();owner.UpdateRendering();
			const double targetX = ResolveBarMainIconX(340.0,380.0,NormalizeBarIconOrder(order),opensRight);
			const BarUiCurveSpecClass curve{BarUiCurveEnum::EaseOutCubic,BarUiCurveEnum::EaseOutCubic,0.0,false};
			pierceModeButton->button.x.SetDirect(180.0);freezeModeButton->button.x.SetDirect(180.0);
			pierceModeButton->button.y.SetDirect(21.25);freezeModeButton->button.y.SetDirect(58.75);
			BarUiSetLayoutPositionTarget(pierceModeButton->button.x,targetX,BarUiDefaultOperationDur,true,curve);
			BarUiSetLayoutPositionTarget(freezeModeButton->button.x,targetX,BarUiDefaultOperationDur,true,curve);
			expect(pierceModeButton->button.x.val==180.0 && freezeModeButton->button.x.val==180.0,
				"sorting retargets from current visible values instead of jumping");
			for(int frame=0;frame<12;++frame)
			{
				const BarUiAnimationAdvanceContextClass tick{0.05,1,true,false};
				BarUiAdvanceAnimation(pierceModeButton->button.x,tick);BarUiAdvanceAnimation(freezeModeButton->button.x,tick);
			}
			expect(pierceModeButton->button.x.val==targetX && freezeModeButton->button.x.val==targetX
				&& pierceModeButton->button.y.val<freezeModeButton->button.y.val,
				"all horizontal policies keep Pierce above Freeze and their shared column");
			SetSelectAndClickThroughMode(0);owner.UpdateRendering();
			AnchorHiddenBarButton(owner.barButtonSet,false,BarButtonPresetEnum::Pierce,BarButtonPresetEnum::Freeze,
				[&](BarUiValueClass& value,double target){BarUiSetLayoutPositionTarget(value,target,BarUiDefaultOperationDur,true,curve);});
			expect(!pierceModeButton->IsVisible() && pierceModeButton->button.x.tar==targetX,
				"hidden Pierce retains the mirrored column without becoming a hit target");
			expectStableButtons();
		}
		// 连续收窄/展开和 PPT 进出：隐藏按钮只改 hide，不能离开列表或保留右侧旧坐标。
		const auto positionTarget=[](BarUiValueClass& value,double target)
			{ value.SetTar(target,BarUiDefaultOperationDur); };
		const auto anchorPierce=[&]()
			{ AnchorHiddenBarButton(owner.barButtonSet,false,BarButtonPresetEnum::Pierce,
				BarButtonPresetEnum::Freeze,positionTarget); };
		const auto advancePierce=[&](double dt)
		{
			const BarUiAnimationAdvanceContextClass frame{dt,1,true,false};
			BarUiAdvanceAnimation(pierceModeButton->button.x,frame);
			BarUiAdvanceAnimation(pierceModeButton->button.y,frame);
		};
		for(int cycle=0;cycle<3;++cycle)
		{
			ChangeStateModeToSelection();SetCurrentPageHasContent(false);owner.UpdateRendering();expectStableButtons();
			pierceModeButton->button.x.SetDirect(1000.0);
			pierceModeButton->button.y.SetDirect(20.0);
			freezeModeButton->button.x.SetDirect(450.0-cycle*75.0);
			freezeModeButton->button.y.SetDirect(40.0);
			anchorPierce();advancePierce(0.0);
			expect(pierceModeButton->button.x.val==1000.0 && pierceModeButton->button.x.tar==freezeModeButton->button.x.tar
				&& pierceModeButton->button.y.tar==freezeModeButton->button.y.tar,
				"hidden Pierce retargets to Freeze without jumping on zero-time advance");
			for(int frame=0;frame<12;++frame){anchorPierce();advancePierce(0.05);}
			expect(pierceModeButton->button.x.val==freezeModeButton->button.x.tar
				&& pierceModeButton->button.y.val==freezeModeButton->button.y.tar,
				"repeated hidden frames settle Pierce at Freeze instead of the old right edge");
			SetSelectAndClickThroughMode(1);ChangeStateModeToPen();owner.UpdateRendering();expectStableButtons();
			expect(pierceModeButton->IsVisible() && pierceModeButton->button.x.val==freezeModeButton->button.x.tar,
				"revealed Pierce starts from the retained Freeze column");
			SetSelectAndClickThroughMode(0);owner.UpdateRendering();expectStableButtons();
			freezeModeButton->button.x.SetDirect(250.0);anchorPierce();
			for(int frame=0;frame<12;++frame){anchorPierce();advancePierce(0.05);}
			expect(!pierceModeButton->IsVisible() && pierceModeButton->button.x.val==250.0,
				"Combined also retains and positions hidden Pierce for the next mode change");
			SetPptPresentationActive(true);Freeze::SetPresentationActive(true);owner.UpdateRendering();expectStableButtons();
			auto* a2Divider=std::distance(stableButtons.begin(),pierceIndex)>=2 ? *(pierceIndex-2) : nullptr;
			auto* whiteboardAnchor=std::distance(stableButtons.begin(),pierceIndex)>=1 ? *(pierceIndex-1) : nullptr;
			expect(a2Divider && a2Divider->preset==BarButtonPresetEnum::Divider && a2Divider->IsVisible(),
				"default A2 boundary remains two positions before Pierce");
			expect(whiteboardAnchor && whiteboardAnchor->preset==BarButtonPresetEnum::Whiteboard && whiteboardAnchor->IsVisible(),
				"restored Whiteboard is the nearest visible predecessor when Freeze is unavailable");
			if(a2Divider){a2Divider->button.x.SetDirect(300.0);a2Divider->button.y.SetDirect(40.0);}
			if(whiteboardAnchor){whiteboardAnchor->button.x.SetDirect(375.0);whiteboardAnchor->button.y.SetDirect(50.0);}
			anchorPierce();
			for(int frame=0;frame<12;++frame){anchorPierce();advancePierce(0.05);}
			expect(!freezeModeButton->IsVisible() && !pierceModeButton->IsVisible() && whiteboardAnchor
				&& pierceModeButton->button.x.val==whiteboardAnchor->button.x.tar
				&& pierceModeButton->button.y.val==whiteboardAnchor->button.y.tar,
				"hidden Pierce anchors to restored Whiteboard when Freeze is unavailable");
			// 保留原来的较远锚点覆盖：白板也不可见时仍应回退到 A2 分隔线。
			if(whiteboardAnchor)
			{
				const bool savedWhiteboardHide=whiteboardAnchor->hide;
				whiteboardAnchor->hide=true;
				for(int frame=0;frame<12;++frame){anchorPierce();advancePierce(0.05);}
				expect(a2Divider && pierceModeButton->button.x.val==a2Divider->button.x.tar
					&& pierceModeButton->button.y.val==a2Divider->button.y.tar,
					"hidden Pierce still falls back to A2 boundary when both Freeze and Whiteboard are unavailable");
				whiteboardAnchor->hide=savedWhiteboardHide;
			}
			SetSelectAndClickThroughMode(1);owner.UpdateRendering();expectStableButtons();
			expect(pierceModeButton->IsVisible() && pierceModeButton->size==BarButtonSizeEnum::twoTwo,
				"PPT reveals the same Pierce member in the full slot");
			Freeze::SetPresentationActive(false);SetPptPresentationActive(false);
		}
		// 真实旧组件0/1/2/3+增删：不执行组件动作，不启动Host或窗口。
		{
			const auto savedShortcuts=setlist.component.shortcutButton;
			const bool savedFold=owner.barState.fold;
			setlist.component.shortcutButton={};
			owner.barState.fold=false;
			const auto reload=[&](int count)
			{
				setlist.component.shortcutButton.appliance.explorer=count>0;
				setlist.component.shortcutButton.appliance.taskmgr=count>1;
				setlist.component.shortcutButton.appliance.control=count>2;
				setlist.component.shortcutButton.system.desktop=count>3;
				owner.barButtonSet.SyncLegacyExtensionButtons();owner.UpdateRendering();
			};
			const auto switchTool=[&](StateModeSelectEnum tool)
			{
				if(tool==StateModeSelectEnum::IdtSelection)ChangeStateModeToSelection();
				else if(tool==StateModeSelectEnum::IdtPen)ChangeStateModeToPen();
				else if(tool==StateModeSelectEnum::IdtShape)ChangeStateModeToShape();
				else if(tool==StateModeSelectEnum::IdtEraser)ChangeStateModeToEraser();
				else ChangeStateModeToClickThrough();
			};
			const auto savedA1=Inkeys::config.UI.Bar.FixedButtonsA1.Snapshot();
			const auto savedA2=Inkeys::config.UI.Bar.FixedButtonsA2.Snapshot();
			int matrixCases=0;
			Inkeys::Testing::WorkspaceSceneScope workspaceScope;
			for(int count:{0,1,2,3,4,2,1,0})
			for(int mode:{0,1})for(int scene:{0,1,2})for(bool content:{false,true})
			for(const auto tool:{StateModeSelectEnum::IdtSelection,StateModeSelectEnum::IdtPen,
				StateModeSelectEnum::IdtShape,StateModeSelectEnum::IdtEraser,StateModeSelectEnum::IdtClickThrough})
			{
				(void)workspaceScope.Set(scene==2 ? WorkspaceScene::Whiteboard : scene==1 ? WorkspaceScene::Presentation : WorkspaceScene::Desktop);
				SetWhiteboardActive(scene==2);Freeze::SetWhiteboardActive(scene==2);
				SetPptPresentationActive(scene==1);Freeze::SetPresentationActive(scene==1);
				SetSelectAndClickThroughMode(mode);SetCurrentPageHasContent(content);switchTool(tool);
				owner.barState.fold=false;reload(count);
				const bool selectionEmpty=tool==StateModeSelectEnum::IdtSelection && !content;
				const bool pierceVisible=mode==1 && scene!=2 && !selectionEmpty;
				expect(pierceModeButton->IsVisible()==pierceVisible && freezeModeButton->IsVisible()==(scene==0)
					&& (!pierceVisible || pierceModeButton->size==(scene==1?BarButtonSizeEnum::twoTwo:BarButtonSizeEnum::twoOne))
					&& (scene!=0 || freezeModeButton->size==(pierceVisible?BarButtonSizeEnum::twoOne:BarButtonSizeEnum::twoTwo)),
					"component matrix keeps the workspace/mode/content A2 grids");
				expect(owner.barButtonSet.preset[(int)BarButtonPresetEnum::Eraser]->IsVisible()==!(scene!=2 && selectionEmpty)
					&& owner.barButtonSet.preset[(int)BarButtonPresetEnum::Geometry]->IsVisible()==!(scene!=2 && selectionEmpty)
					&& (clearModeButton->state->state==BarWidgetState::Disable)==selectionEmpty,
					"component matrix retains compact tools and Clear availability");
				const auto more=owner.barButtonSet.GetMoreButtonSnapshot();
				expect(owner.barButtonSet.tot==static_cast<int>(stableButtons.size())+std::min(count,2)
					&& more.explicitMore.size()==1 && more.forcedOverflow.size()==static_cast<size_t>(std::max(0,count-2)),
					"component capacity keeps two main members, explicit Settings and forced overflow");
				const auto before=owner.barButtonSet.MainLayoutSnapshot();
				owner.barButtonSet.Load();owner.UpdateRendering();
				for(int repeat=0;repeat<3;++repeat)owner.UpdateRendering();
				expect(owner.barButtonSet.MainLayoutSnapshot()==before
					&& owner.barButtonSet.preset[(int)BarButtonPresetEnum::Freeze]==freezeModeButton
					&& owner.barButtonSet.preset[(int)BarButtonPresetEnum::Pierce]==pierceModeButton,
					"repeated Load and projection preserve all effective geometry and stable identities");
				++matrixCases;
			}
			// 真实准备路径必须在三种稳定工作区保持形状面板展开，同时保留统一收起条件。
			auto* geometryModeButton=owner.barButtonSet.preset[(int)BarButtonPresetEnum::Geometry];
			for(int scene:{0,1,2})
			{
				(void)workspaceScope.Set(scene==2 ? WorkspaceScene::Whiteboard : scene==1 ? WorkspaceScene::Presentation : WorkspaceScene::Desktop);
				SetWhiteboardActive(scene==2);Freeze::SetWhiteboardActive(scene==2);
				SetPptPresentationActive(scene==1);Freeze::SetPresentationActive(scene==1);
				SetCurrentPageHasContent(true);owner.barState.fold=false;ChangeStateModeToShape();owner.UpdateRendering();
				owner.barState.geometryAttribute=true;owner.UpdateRendering();
				expect(owner.barState.geometryAttribute,
					"Shape panel stays open through production preparation in Desktop, Presentation and Whiteboard");
				ChangeStateModeToPen();owner.barState.geometryAttribute=true;owner.UpdateRendering();
				expect(!owner.barState.geometryAttribute,"Shape panel closes when the active tool is not Shape");
				ChangeStateModeToShape();owner.barState.fold=false;owner.UpdateRendering();
				owner.barState.fold=true;owner.barState.geometryAttribute=true;owner.UpdateRendering();
				expect(!owner.barState.geometryAttribute,"Shape panel closes when the main bar folds");
				owner.barState.fold=false;owner.UpdateRendering();
				geometryModeButton->hide=true;owner.barState.geometryAttribute=true;
				owner.barButtonSet.PrepareMainPanels(GetStateModeSnapshot());
				expect(!owner.barState.geometryAttribute,"Shape panel closes when its main button is invisible");
				owner.UpdateRendering();
			}
			report<<"[MainPresentation] componentMatrixCases="<<matrixCases<<'\n';
			expect(WhiteboardActive() && !HideWhiteboardSnapIndicator()
				&& ResolveBarBottomDockIndicatorTarget(BarBottomDockMode::BottomDocked,true,true,true)
				&& !ResolveBarBottomDockIndicatorTarget(BarBottomDockMode::BottomDocked,false,true,true)
				&& !ResolveBarBottomDockIndicatorTarget(BarBottomDockMode::BottomDocked,true,true,false),
				"active Whiteboard permits eligible drag feedback while entry and unchanged gestures stay hidden");
			(void)workspaceScope.Set(WorkspaceScene::Desktop);
			SetWhiteboardActive(false);Freeze::SetWhiteboardActive(false);
			SetPptPresentationActive(false);Freeze::SetPresentationActive(false);
			SetSelectAndClickThroughMode(1);ChangeStateModeToPen();SetCurrentPageHasContent(false);reload(0);
			const auto compact=owner.barButtonSet.MainLayoutSnapshot();
			const double compactX=pierceModeButton->button.x.val;
			const double compactY=pierceModeButton->button.y.val;
			const double compactH=freezeModeButton->button.h.val;
			SetSelectAndClickThroughMode(0);owner.UpdateRendering();
			const auto full=owner.barButtonSet.MainLayoutSnapshot();
			expect(compact!=full && pierceModeButton->button.x.val==compactX
				&& pierceModeButton->button.y.val==compactY && freezeModeButton->button.h.val==compactH,
				"equal-width split/full A2 change invalidates geometry without moving current animation values");
			SetSelectAndClickThroughMode(1);owner.UpdateRendering();
			const auto stable=owner.barButtonSet.MainLayoutSnapshot();
			auto* draw=owner.barButtonSet.preset[(int)BarButtonPresetEnum::Draw];
			const auto savedEmph=draw->state->emph.load();const auto savedHover=draw->hoverStage.load();
			draw->state->emph=BarWidgetEmphasize::Pressed;draw->hoverStage=BarButtonHoverStageEnum::Showing;
			draw->pressScale.SetTar(0.9,0.4);draw->name.color.SetTar(RGB(1,2,3),0.4);
			expect(owner.barButtonSet.MainLayoutSnapshot()==stable,
				"hover, press, color and animation values are absent from the layout fingerprint");
			draw->state->emph=savedEmph;draw->hoverStage=savedHover;
			draw->pressScale.SetDirect(1.0);
			owner.barButtonSet.buttonList.Swap(0,1);
			expect(owner.barButtonSet.MainLayoutSnapshot()!=stable,"equal-width member permutation changes the stable layout fingerprint");
			owner.barButtonSet.buttonList.Swap(0,1);
			// 模拟目标提交使用真实timeline/SetTar helper：早期可加入、晚期从当前值完整重开。
			BarUiTimelineClass batch;batch.Restart(BarUiDefaultOperationDur);
			pierceModeButton->button.y.SetDirect(40.0);freezeModeButton->button.y.SetDirect(40.0);
			freezeModeButton->button.h.SetDirect(70.0);
			BarUiSetLayoutPositionTarget(pierceModeButton->button.y,21.25,batch.GetRemainingDuration(),true,BarButtonPressCurve());
			BarUiSetLayoutPositionTarget(freezeModeButton->button.y,58.75,batch.GetRemainingDuration(),true,BarButtonPressCurve());
			freezeModeButton->button.h.SetTar(32.5,batch.GetRemainingDuration());
			const BarUiAnimationAdvanceContextClass zero{0.0,1,true,false};
			BarUiAdvanceAnimation(freezeModeButton->button.h,zero);
			expect(freezeModeButton->button.h.val==70.0 && batch.CanJoin(),"zero-time split targets retain current geometry and permit early batch join");
			batch.Advance(BarUiDefaultOperationDur*0.75,1);
			const BarUiAnimationAdvanceContextClass partial{BarUiDefaultOperationDur*0.75,1,true,false};
			BarUiAdvanceAnimation(freezeModeButton->button.h,partial);
			const double interruptedHeight=freezeModeButton->button.h.val;
			expect(!batch.CanJoin(),"late main-layout update cannot join the old half-finished batch");
			SetSelectAndClickThroughMode(0);owner.UpdateRendering();
			batch.Restart(BarUiDefaultOperationDur);freezeModeButton->button.h.SetTar(70.0,batch.GetRemainingDuration());
			expect(freezeModeButton->button.h.val==interruptedHeight,"late layout retarget starts at the displayed height");
			const auto repeated=owner.barButtonSet.MainLayoutSnapshot();
			for(int frame=0;frame<120;++frame)
			{
				owner.UpdateRendering();
				const BarUiAnimationAdvanceContextClass tick{0.01,1,true,false};
				BarUiAdvanceAnimation(freezeModeButton->button.h,tick);batch.Advance(0.01,1);
				selectModeButton->name.AdvanceContentTransition(0.01,1);
			}
			expect(owner.barButtonSet.MainLayoutSnapshot()==repeated && !batch.IsActive()
				&& freezeModeButton->button.h.val==70.0 && selectModeButton->name.content.GetVal()==selectModeButton->name.content.GetTar(),
				"same-state refresh leaves the batch and content animation free to settle");
			const auto fixedEqual=[](const auto& left,const auto& right)
			{
				if(left.size()!=right.size())return false;
				for(size_t i=0;i<left.size();++i)if(left[i].Id!=right[i].Id || left[i].Size!=right[i].Size)return false;
				return true;
			};
			expect(fixedEqual(savedA1,Inkeys::config.UI.Bar.FixedButtonsA1.Snapshot())
				&& fixedEqual(savedA2,Inkeys::config.UI.Bar.FixedButtonsA2.Snapshot()),"effective presentation never rewrites persisted A1/A2 sizes or order");
			setlist.component.shortcutButton=savedShortcuts;
			owner.barState.fold=savedFold;owner.barButtonSet.SyncLegacyExtensionButtons();owner.UpdateRendering();expectStableButtons();
		}
		// 用真实历史刷新和墙钟到期入口覆盖精简态，保留保护身份但不能再次显示按钮。
		auto* undoModeButton=owner.barButtonSet.preset[(int)BarButtonPresetEnum::Recall];
		auto* redoModeButton=owner.barButtonSet.preset[(int)BarButtonPresetEnum::Redo];
		for(const bool redo:{false,true})
		{
			ChangeStateModeToPen();SetCurrentPageHasContent(true);
			{
				std::scoped_lock lock(owner.recallChromeMutex);
				owner.recallChrome={};
				owner.recallChrome.availability={true,true};
				owner.recallChrome.layout=RecallChromeLayout::Compact;
				owner.recallChrome=ApplyRecallChromeCommandResult(owner.recallChrome,redo,{true,true},true);
				owner.ArmRecallChromeIdleDeadline();
			}
			ChangeStateModeToSelection();SetCurrentPageHasContent(false);owner.UpdateRendering();
			const auto hiddenHistory=[&]()
			{
				const auto chrome=owner.RecallChromeView();
				expect(!undoModeButton->IsVisible() && !redoModeButton->IsVisible()
					&& !owner.barState.recallAttribute && !chrome.popupOpen
					&& !chrome.mainUndoVisible && !chrome.mainRedoVisible,
					"empty Selection chrome stays hidden across refresh and timer projection");
			};
			hiddenHistory();
			const auto protectedHold=owner.recallChrome.hold;
			const auto protectedDeadline=owner.recallChromeIdleDeadline;
			owner.UpdateRendering();hiddenHistory();
			owner.NoteRecallHistoryAvailability(true,true);hiddenHistory();
			owner.NoteRecallHistoryAvailability(false,false);hiddenHistory();
			unsigned retry=0;
			expect(!owner.PollRecallChromeIdleTimer(retry) && retry>0 && !owner.AdvanceRecallChromeIdle(10.0)
				&& owner.recallChrome.hold==protectedHold && owner.recallChromeIdleDeadline==protectedDeadline,
				"compact Selection preserves the 3 second wall-clock protection");
			SetCurrentPageHasContent(true);owner.UpdateRendering();
			expect(redo ? redoModeButton->IsVisible() : undoModeButton->IsVisible(),
				"nonempty Selection retains the protected history layout");
			SetCurrentPageHasContent(false);owner.UpdateRendering();hiddenHistory();
			owner.recallChromeIdleDeadline=std::chrono::steady_clock::now()-std::chrono::seconds(1);
			expect(owner.PollRecallChromeIdleTimer(retry) && owner.AdvanceRecallChromeIdle(0.0),
				"actual idle expiry applies the pending history layout");
			hiddenHistory();
			expect(owner.recallChrome.hold==RecallChromeHold::None && !owner.recallChromeIdleArmed,
				"idle settlement completes without redisplaying hidden history buttons");
			ChangeStateModeToPen();owner.UpdateRendering();
			expect(undoModeButton->IsVisible() && !redoModeButton->IsVisible(),
				"leaving compact Selection restores the original settled history layout");
		}
		// 无Host的真实历史事件/墙钟/拒绝命令入口，不把种入Accepted策略当成产品命令执行。
		{
			ChangeStateModeToPen();SetCurrentPageHasContent(true);owner.barState.fold=false;
			const auto seedHold=[&](bool redo)
			{
				std::scoped_lock lock(owner.recallChromeMutex);
				owner.recallChrome={};owner.recallChrome.availability={true,true};
				owner.recallChrome.layout=RecallChromeLayout::Compact;
				owner.recallChrome=ApplyRecallChromeCommandResult(owner.recallChrome,redo,{true,true},true);
				owner.ArmRecallChromeIdleDeadline();
			};
			for(bool redo:{false,true})
			{
				seedHold(redo);owner.UpdateRendering();
				const auto protectedLayout=owner.barButtonSet.MainLayoutSnapshot();
				const auto protectedHold=owner.recallChrome.hold;
				const auto deadline=owner.recallChromeIdleDeadline;
				unsigned retry=0;
				expect(!owner.PollRecallChromeIdleTimer(retry) && retry>0 && !owner.AdvanceRecallChromeIdle(100.0)
					&& owner.recallChromeIdleDeadline==deadline,"early timer and large animation dt cannot consume the wall-clock hold");
				{
					std::scoped_lock lock(owner.recallChromeMutex);
					owner.recallChromeIdleDeadline=std::chrono::steady_clock::now()-std::chrono::seconds(1);
				}
				if(redo)owner.OpenRecallFromRedo();else owner.OpenRecallFromUndo();
				expect(owner.recallChromeIdleDeadline>std::chrono::steady_clock::now()
					&& !owner.PollRecallChromeIdleTimer(retry) && retry>0 && owner.recallChrome.hold==protectedHold
					&& owner.barButtonSet.MainLayoutSnapshot()==protectedLayout,
					"gray production command extends the hold without directly changing button geometry");
				owner.UpdateRendering();
				expect((redo?redoModeButton:undoModeButton)->state->state!=BarWidgetState::Disable,
					"gray protected history remains eligible for repeat-click feedback");
				const auto refreshedDeadline=owner.recallChromeIdleDeadline;
				owner.NoteRecallPopupDismissed();owner.ClearRecallForDrawing();owner.SettleRecallChromeForWorkspace();
				expect(owner.recallChromeIdleDeadline==refreshedDeadline && owner.recallChrome.hold==protectedHold
					&& owner.barButtonSet.MainLayoutSnapshot()==protectedLayout,
					"popup, drawing and workspace events only suppress popup while protecting main geometry");
				owner.UpdateRendering();
				expect(!owner.RecallChromeView().popupOpen,"unified view publishes suppressed popup identity");
				owner.barButtonSet.SyncLegacyExtensionButtons();owner.UpdateRendering();
				expect(owner.barButtonSet.MainLayoutSnapshot()==protectedLayout,"component Load cannot reset a protected history identity");
				const auto beforeExpiry=owner.barButtonSet.MainLayoutSnapshot();
				{
					std::scoped_lock lock(owner.recallChromeMutex);
					owner.recallChromeIdleDeadline=std::chrono::steady_clock::now()-std::chrono::seconds(1);
				}
				expect(owner.PollRecallChromeIdleTimer(retry) && owner.AdvanceRecallChromeIdle(0.0)
					&& !owner.recallChromeIdleArmed && owner.recallChrome.hold==RecallChromeHold::None
					&& owner.barButtonSet.MainLayoutSnapshot()==beforeExpiry,
					"idle expiry settles state without a second effective-button write path");
				owner.barButtonSet.UpdateMainPresentation(GetStateModeSnapshot());
				expect(undoModeButton->IsVisible() && undoModeButton->size==BarButtonSizeEnum::twoTwo && !redoModeButton->IsVisible(),
					"the frame presentation entry applies the settled history grid before target scanning");
			}
			{
				std::scoped_lock lock(owner.recallChromeMutex);
				owner.recallChrome={};owner.recallChrome.layout=RecallChromeLayout::Compact;
			}
			owner.NoteRecallHistoryAvailability(false,false);owner.UpdateRendering();
			expect(owner.recallChrome.settlePending && owner.recallChromeIdleArmed
				&& undoModeButton->size==BarButtonSizeEnum::twoOne && redoModeButton->size==BarButtonSizeEnum::twoOne,
				"availability-only layout changes also keep a wall-clock pending transition");
			{
				std::scoped_lock lock(owner.recallChromeMutex);
				owner.recallChromeIdleDeadline=std::chrono::steady_clock::now()-std::chrono::seconds(1);
			}
			expect(owner.AdvanceRecallChromeIdle(0.0),"availability-only idle transition is settled without a clicked hold");
			owner.UpdateRendering();
			expect(undoModeButton->size==BarButtonSizeEnum::twoTwo && !redoModeButton->IsVisible(),
				"availability-only settled grid is applied by the same projection");
		}
		SetCurrentPageHasContent(false);SetSelectAndClickThroughMode(0);ChangeStateModeToSelection();owner.UpdateRendering();
		SetContentStateUpdatesReady(false);
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
		// 真实Advance消费已捕获帧，即使锁外又发布空页Selection也不能混用下一帧门禁。
		{
			owner.barState.fold=false;owner.barState.drawAttribute=false;owner.barState.geometryAttribute=false;
			owner.barState.moreExpanded=false;owner.barState.eraserAttribute=false;
			for(const bool redo:{false,true})
			{
				{
					std::scoped_lock lock(owner.recallChromeMutex);
					owner.recallChrome={};owner.recallChrome.availability={true,true};
					owner.recallChrome.layout=RecallChromeLayout::Compact;
					owner.recallChrome=ApplyRecallChromeCommandResult(owner.recallChrome,redo,{true,true},true);
					owner.ArmRecallChromeIdleDeadline();
				}
				auto frameTool=GetStateModeSnapshot();frameTool.StateModeSelect=StateModeSelectEnum::IdtPen;
				owner.barButtonSet.UpdateMainPresentation(frameTool);
				const auto frameChrome=owner.RecallChromeView();
				auto* anchor=owner.barButtonSet.preset[(int)(frameChrome.redoExpanded?BarButtonPresetEnum::Redo:BarButtonPresetEnum::Recall)];
				const bool anchorVisible=anchor->IsVisible();
				anchor->button.x.SetDirect(200.0);anchor->button.y.SetDirect(40.0);
				anchor->button.w.SetDirect(70.0);anchor->button.h.SetDirect(70.0);
				frameTool.StateModeSelect=StateModeSelectEnum::IdtSelection;
				owner.barButtonSet.UpdateMainPresentation(frameTool);
				expect(frameChrome.popupOpen && frameChrome.popupShowsRedo!=redo && anchorVisible
					&& !owner.RecallChromeView().popupOpen && !anchor->IsVisible(),
					"fixture publishes a newer global history view after capturing an open frame and its anchor gate");
				BarRecallAttributePanel framePanel;
				for(int tick=0;tick<90;++tick)
					framePanel.Advance(owner,frameChrome,anchorVisible,1.0/60,1,1,96,{0,0,1000,1000},{0,0},0,0);
				framePanel.CommitPresented();
				expect(framePanel.PresentationSnapshot().panelVisible,
					"real history Advance preserves the captured frame popup despite newer global compact visibility");
				for(int tick=0;tick<90;++tick)
					framePanel.Advance(owner,frameChrome,false,1.0/60,1,1,96,{0,0,1000,1000},{0,0},0,0);
				framePanel.CommitPresented();
				expect(!framePanel.PresentationSnapshot().panelVisible,
					"real history Advance closes when the supplied frame anchor gate becomes hidden");
			}
			owner.barButtonSet.UpdateMainPresentation(GetStateModeSnapshot());
		}
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
				const auto label=IW(localizedPresetLabelKeys[i]);b->name.SetStringImmediate(label);b->TransitionContent(L"",label);b->name.size.SetDirect(b->LabelFontSize(metrics.primaryFontSizeDip));
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
		const std::array<const char*,21> fixedLabelKeys{
			I18nKey.UI.Bar.MainButtons.SelectLabel,I18nKey.UI.Bar.MainButtons.MoveLabel,
			I18nKey.UI.Bar.MainButtons.DrawLabel,I18nKey.UI.Bar.MainButtons.LaserLabel,
			I18nKey.UI.Bar.MainButtons.HighlighterLabel,I18nKey.UI.Bar.MainButtons.HardPenLabel,
			I18nKey.UI.Bar.MainButtons.SoftPenLabel,I18nKey.UI.Bar.MainButtons.EraserLabel,
			I18nKey.UI.Bar.MainButtons.AreaEraserLabel,I18nKey.UI.Bar.MainButtons.GeometryLabel,
			I18nKey.UI.Bar.MainButtons.RectangleLabel,I18nKey.UI.Bar.MainButtons.StraightLineLabel,
			I18nKey.UI.Bar.MainButtons.UndoLabel,I18nKey.UI.Bar.MainButtons.ClearLabel,
			I18nKey.UI.Bar.MainButtons.WhiteboardLabel,I18nKey.UI.Bar.MainButtons.CloseWhiteboardLabel,
			I18nKey.UI.Bar.MainButtons.ResumeWhiteboardLabel,
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
			{
				WhiteboardButtonStyleFixture style;
				auto* button=owner.barButtonSet.preset[(int)BarButtonPresetEnum::Whiteboard];
				style.preset[(int)BarButtonPresetEnum::Whiteboard]=button;
				BarUiSVGClass resumeIcon,closeIcon;
				resumeIcon.InitializationFromResource(L"UI",L"barWhiteboard");
				closeIcon.InitializationFromResource(L"UI",L"barDismiss");
				expect(!resumeIcon.svg.GetVal().empty() && !closeIcon.svg.GetVal().empty(),
					"Whiteboard resume and close SVG resources are available for real content checks");
				const bool savedFold=owner.barState.fold;
				const auto geometry=owner.barButtonSet.MainLayoutSnapshot();
				for(bool folded:{false,true})for(bool minimized:{true,false,true,false})
				{
					owner.barState.fold=folded;
					// 直接消费生产内容更新入口，不投递hover、翻页或任何窗口消息。
					style.UpdateWhiteboardButtonStyle(true,minimized);
					button->name.AdvanceContentTransition(1.0,1.0);
					button->icon.AdvanceContentTransition(1.0,1.0);
					expect(button->name.content.GetVal()==IW(minimized?I18nKey.UI.Bar.MainButtons.ResumeWhiteboardLabel
						:I18nKey.UI.Bar.MainButtons.CloseWhiteboardLabel)
						&& button->icon.svg.GetVal()==(minimized?resumeIcon.svg.GetVal():closeIcon.svg.GetVal())
						&& owner.barButtonSet.MainLayoutSnapshot()==geometry,
						"Whiteboard minimize and restore change real label and SVG without depending on folding or layout changes");
				}
				owner.barState.fold=savedFold;
				style.UpdateWhiteboardButtonStyle(false,true);
				button->name.AdvanceContentTransition(1.0,1.0);
				expect(button->name.content.GetVal()==IW(I18nKey.UI.Bar.MainButtons.WhiteboardLabel),
					"inactive whiteboard ignores a stale minimized flag");
			}
			for(const auto* key:fixedLabelKeys)
				expect(owner.spec.MeasureText(IW(key),metrics.primaryFontSizeDip,DWRITE_FONT_WEIGHT_BOLD).width<=metrics.primarySlotWidthDip+0.5f,
					"localized fixed label fits the existing 70 DIP slot");
			for(const auto* key:{I18nKey.UI.Bar.MainButtons.WhiteboardLabel,I18nKey.UI.Bar.MainButtons.FreezeLabel})
				expect(owner.spec.MeasureText(IW(key),twoOneMetrics.primaryFontSizeDip,DWRITE_FONT_WEIGHT_BOLD).width<=twoOneMetrics.primarySlotWidthDip+0.5f,
					"localized Whiteboard and Freeze labels fit the existing 37 DIP slot");
			for(const auto preset:{BarButtonPresetEnum::Select,BarButtonPresetEnum::Pierce})
			{
				auto* b=owner.barButtonSet.preset[(int)preset];
				auto label=IW(preset==BarButtonPresetEnum::Select ? I18nKey.UI.Bar.MainButtons.SelectClearLabel
					: I18nKey.UI.Bar.MainButtons.ClickThroughLabel);
				// 直接测量真实语言资源，不在测试里重复生产代码的英文改写。
				expect(std::count(label.begin(),label.end(),L'\n')==(I18n::isIdentifying(L"en-US")?1:0),
					"localized Select Clear and Click-through resources keep English two-line and Chinese single-line labels");
				b->name.SetStringImmediate(label);b->TransitionContent(L"",label);
				for(const auto kind:{BarButtonVisualLayoutKind::StandardTwoTwo,BarButtonVisualLayoutKind::StandardTwoOne})
				{
					if(preset==BarButtonPresetEnum::Select && kind==BarButtonVisualLayoutKind::StandardTwoOne)continue;
					const auto slot=ResolveBarButtonVisualMetrics(kind);
					const auto measured=owner.spec.MeasureText(label,b->LabelFontSize(slot.primaryFontSizeDip),DWRITE_FONT_WEIGHT_BOLD);
					expect(measured.width>0 && measured.width<=slot.primarySlotWidthDip+0.5f &&
						measured.height>0 && measured.height<=slot.primarySlotHeightDip+0.5f,
						"Select Clear and Click-through three-language DWrite bounds fit original slots");
					if(label.find(L'\n')!=wstring::npos)
					{
						// 真实换字过渡的中点前后都测量当前字号，防止目标文案提前裁切。
						BarButtonClass transitioning(*b);
						transitioning.name.SetStringImmediate(L"Select");
						transitioning.name.size.SetDirect(slot.primaryFontSizeDip);
						transitioning.TransitionContent(L"",label);
						for(const double dt:{0.0,0.19,0.02,0.05,0.5})
						{
							transitioning.name.AdvanceContentTransition(dt,1.0);
							const auto bounds=owner.spec.MeasureText(transitioning.name.content.GetVal(),
								transitioning.name.size.val,DWRITE_FONT_WEIGHT_BOLD);
							expect(bounds.width<=slot.primarySlotWidthDip+0.5f && bounds.height<=slot.primarySlotHeightDip+0.5f,
								"incoming multiline label fits before, at and after the transparent midpoint");
						}
						transitioning.TransitionContent(L"",L"Select");
						expect(transitioning.LabelFontSize(slot.primaryFontSizeDip)==9.0,
							"outgoing multiline label keeps compact font until content changes");
						transitioning.name.AdvanceContentTransition(1.0,1.0);
						expect(transitioning.LabelFontSize(slot.primaryFontSizeDip)==slot.primaryFontSizeDip,
							"short label restores standard font after content transition");
					}
				}
			}
			updateLocalizedPresetLabels();
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
				owner.spec.SetFrameZoom(zoom);owner.spec.PrepareFrameLighting(1.0/60,{},static_cast<int>(StateModeSelectEnum::IdtEraser),0,0,0);
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
			owner.spec.SetFrameZoom(zoom);owner.spec.PrepareFrameLighting(1.0/60,{},static_cast<int>(StateModeSelectEnum::IdtEraser),0,0,0);
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
					bool accepted=false;
					m.message=WM_LBUTTONUP;m.lbutton=false;expect(owner.eraserAttribute.Pointer(owner,m,cancel,false,&accepted),"control accepts up");
					return accepted;
				};
				click(8);expect(EraserPreferencesSnapshot().sensitivity==Inkeys::Drawing::Draw3::SpeedEraser::Sensitivity::High && owner.barState.eraserSensitivityOpen,"sensitivity updates without closing menu");
				expect(!click(9,true),"cancelled settings gesture is not accepted");
				expect(click(9) && owner.barState.eraserSensitivityOpen
					&& EraserPreferencesSnapshot().sensitivity==Inkeys::Drawing::Draw3::SpeedEraser::Sensitivity::High,
					"enabled settings accepts the pointer without changing eraser preferences");
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
			owner.spec.SetFrameZoom(1);owner.spec.PrepareFrameLighting(1.0/60,{},static_cast<int>(StateModeSelectEnum::IdtEraser),0,0,0);
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
					// 独立数值计量不能安装验真probe，也不能改变真实cache/绘制决策。
					static std::int64_t numericClockReads = 0;
					const auto numericClock = []() noexcept -> std::int64_t { return ++numericClockReads * 1000; };
					numericClockReads = 0;
					Ui3SvgStageTimings outerTiming, innerTiming;
					{
						SvgStageTimingScope absent(nullptr, numericClock);
						Ui3SvgStageTimer timer(Ui3SvgStage::Parse);
					}
					expect(numericClockReads == 0 && CurrentUi3SvgScope() == nullptr, "N01 default/null numeric scope reads no clock and installs no proof");
					{
						SvgStageTimingScope outer(&outerTiming, numericClock);
						{ Ui3SvgStageTimer timer(Ui3SvgStage::Parse); }
						{
							SvgStageTimingScope inner(&innerTiming, numericClock);
							{ Ui3SvgStageTimer timer(Ui3SvgStage::Raster); }
							{ SvgStageTimingScope absent(nullptr, numericClock); Ui3SvgStageTimer timer(Ui3SvgStage::Upload); }
						}
						{ Ui3SvgStageTimer timer(Ui3SvgStage::Draw); }
					}
					expect(numericClockReads == 6 && outerTiming.clockReads == 4 && innerTiming.clockReads == 2
						&& outerTiming.calls[0] == 1 && outerTiming.calls[3] == 1 && innerTiming.calls[1] == 1
						&& outerTiming.elapsedMs[0] > 0 && outerTiming.elapsedMs[3] > 0 && innerTiming.elapsedMs[1] > 0
						&& CurrentUi3SvgScope() == nullptr, "N02 numeric-only stages and nested/null scopes restore without proof state");
					Ui3SvgStageTimings combinedTiming;
					const auto combinedBefore = probe->CountersAfterOwnerStopped();
					const auto numericReadsBefore = numericClockReads;
					{
						SvgObservationScope observed(probe.get(), true);
						auto* originalScope = CurrentUi3SvgScope();
						{ SvgStageTimingScope timing(&combinedTiming, numericClock); Ui3SvgStageTimer timer(Ui3SvgStage::Upload); }
						expect(CurrentUi3SvgScope() == originalScope, "N03 numeric timing preserves the active proof scope identity");
					}
					const auto combinedAfter = probe->CountersAfterOwnerStopped();
					expect(combinedAfter.clockReads == combinedBefore.clockReads + 2 && numericClockReads == numericReadsBefore
						&& combinedTiming.clockReads == 2 && combinedTiming.calls[2] == 1
						&& std::abs(combinedAfter.uploadMs - combinedBefore.uploadMs - combinedTiming.elapsedMs[2]) < 0.0000001,
						"N04 dual sinks share one clock pair and retain original probe timing semantics");
					const auto bitmapBeforeTiming = svg.cacheBitmap.Get();
					auto renderNumeric = [&](Ui3SvgStageTimings* timing)
					{
						SvgStageTimingScope numeric(timing);
						expect(CurrentUi3SvgScope() == nullptr, "N05 numeric-only real renderer keeps proof absent");
						dc->BeginDraw(); dc->SetTransform(D2D1::IdentityMatrix()); dc->Clear(D2D1::ColorF(0,0,0,0));
						(void)renderer.Svg(dc, svg, BarUiInheritClass(16,16));
						expect(SUCCEEDED(dc->EndDraw()), "N05 real numeric timing draw completes");
						return ReadSvgProofPixels(dc, renderer.GetTargetBitmap());
					};
					Ui3SvgStageTimings realTiming;
					const auto numericOffPixels = renderNumeric(nullptr), numericOnPixels = renderNumeric(&realTiming);
					expect(numericOffPixels == numericOnPixels && numericOnPixels.size() == svgSize * svgSize * 4
						&& svg.cacheBitmap.Get() == bitmapBeforeTiming && realTiming.calls[0] == 0 && realTiming.calls[1] == 0
						&& realTiming.calls[2] == 0 && realTiming.calls[3] == 1 && realTiming.clockReads == 2
						&& probe->CountersAfterOwnerStopped().clockReads == combinedAfter.clockReads
						&& probe->CountersAfterOwnerStopped().drawSubmit == combinedAfter.drawSubmit,
						"N06 numeric on/off is BGRA exact with unchanged actual cache and no proof operations");
					Ui3SvgStageTimings overheadTiming;
					constexpr unsigned overheadIterations = 4096;
					const auto measureTimingCost = [&](Ui3SvgStageTimings* timing)
					{
						SvgStageTimingScope numeric(timing);
						const auto started = std::chrono::steady_clock::now();
						for (unsigned i = 0; i < overheadIterations; ++i) { Ui3SvgStageTimer timer(Ui3SvgStage::Draw); }
						return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - started).count() / overheadIterations;
					};
					const double inactivePairNs = measureTimingCost(nullptr), activePairNs = measureTimingCost(&overheadTiming);
					expect(overheadTiming.calls[3] == overheadIterations && overheadTiming.clockReads == overheadIterations * 2,
						"N07 bounded timing overhead control records exactly one clock pair per stage");
					report << "[Ui3SvgNumericTiming] iterations=" << overheadIterations << " inactive-pair-ns=" << inactivePairNs
						<< " active-pair-ns=" << activePairNs << " extra-pair-ns=" << activePairNs - inactivePairNs << '\n';
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
					// 真实生产绘制验证共享光源快照，不能只检查 setter 回读值。
					if(probe.GetDeviceContext())
					{
						auto* dc=probe.GetDeviceContext();
						const auto alphaPixels=[&](const BarUiFrameLightingSnapshot& snapshot)
						{
							probe.SetFrameLightingSnapshot(snapshot);
							dc->SetTransform(D2D1::IdentityMatrix());
							probe.BeginFrameDraw(dc,D2D1::RectF(0,0,width,height));
							(void)probe.Shape(dc,shape,BarUiInheritClass(100,80));
							probe.FinishFrameDrawCommands(dc);
							HDC source=nullptr;
							HRESULT hr=probe.GetGdiInteropRenderTarget()->GetDC(D2D1_DC_INITIALIZE_MODE_COPY,&source);
							if(SUCCEEDED(hr)) {const RECT unchanged{};hr=probe.GetGdiInteropRenderTarget()->ReleaseDC(&unchanged);}
							const HRESULT end=probe.EndFrameDraw(dc);
							if(SUCCEEDED(hr))hr=end;
							ComPtr<ID2D1Bitmap1> readback;
							const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
								probe.GetTargetBitmap()->GetPixelFormat());
							if(SUCCEEDED(hr))hr=dc->CreateBitmap(D2D1::SizeU(width,height),nullptr,0,&properties,&readback);
							if(SUCCEEDED(hr))hr=readback->CopyFromBitmap(nullptr,probe.GetTargetBitmap(),nullptr);
							D2D1_MAPPED_RECT mapped{};if(SUCCEEDED(hr))hr=readback->Map(D2D1_MAP_OPTIONS_READ,&mapped);
							expect(SUCCEEDED(hr),"shared lighting snapshot transaction and alpha readback");
							std::size_t pixels=0;
							if(SUCCEEDED(hr))
							{
								for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x)
									if(mapped.bits[y*mapped.pitch+x*4+3])++pixels;
								readback->Unmap();
							}
							return pixels;
						};
						BarUiFrameLightingSnapshot hidden{};hidden.primaryLight={200,125};hidden.primaryRadius=480;
						hidden.primaryLightVisible=false;hidden.edgeLightingEnabled=true;
						expect(alphaPixels(hidden)==0,"hidden primary light does not draw merely because its radius is retained");
						auto cursor=hidden;cursor.primaryRadius=0;cursor.cursorLight={200,125};
						cursor.cursorRadius=240;cursor.cursorIntensity=1;cursor.cursorLightVisible=true;
						expect(alphaPixels(cursor)>0,"cursor-only lighting renders without a primary radius");
						auto disabled=cursor;disabled.edgeLightingEnabled=false;
						expect(alphaPixels(disabled)==0,"snapshot edge-light disable suppresses all light passes");
						shape.framePct->SetDirect(0.30);
						expect(alphaPixels(hidden)>0,"base frame remains visible when both lights are hidden");
					}
					// 真正的全帧与保留像素局部重绘比较：确保跳过发生，且不是同一裁剪路径自证。
					{
						constexpr UINT targetWidth=1024,targetHeight=512;
						const auto buttonMetrics=ResolveBarButtonVisualMetrics(BarButtonVisualLayoutKind::StandardTwoTwo);
						const auto initializeButton=[&](BarButtonClass& button,const wchar_t* label)
						{
							button.hide=false;
							button.button.Initialization(0,0,70,70,4,4,1,RGB(45,85,110),RGB(220,230,240));
							button.button.enable.Initialization(true);button.button.pct.SetDirect(0.65);
							button.button.framePct.emplace(0.30);button.button.frameLightPct.emplace(1.0);
							button.button.frameRendering=BarUiFrameRenderingEnum::PointLight;
							button.icon.Initialization(0,buttonMetrics.iconOffsetYDip,RGB(255,255,255),std::nullopt);
							button.icon.InitializationFromString(LR"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="28" height="28"><path fill="#fff" d="M4 4H24V24H4Z"/></svg>)SVG");
							button.icon.enable.Initialization(true);button.icon.pct.SetDirect(1);
							button.icon.SetWH(28,28);button.icon.w.SetDirect(button.icon.w.tar);button.icon.h.SetDirect(button.icon.h.tar);
							button.name.Initialization(0,buttonMetrics.primaryOffsetYDip,buttonMetrics.primarySlotWidthDip,
								buttonMetrics.primarySlotHeightDip,label,buttonMetrics.primaryFontSizeDip,RGB(255,255,255));
							button.name.enable.Initialization(true);button.name.pct.SetDirect(1);
						};
						for(const double zoom:{1.0,1.5,2.0})for(const UINT dpi:{96u,144u,192u})
						{
							BarUIRendering full(&owner),retained(&owner);
							const auto epoch=RenderPipeline::GetDeviceEpoch();
							const bool ready=SUCCEEDED(full.EnsureDeviceResources(epoch,targetWidth,targetHeight))
								&&SUCCEEDED(retained.EnsureDeviceResources(epoch,targetWidth,targetHeight));
							expect(ready,"shared dirty cull production target setup");if(!ready)continue;
							std::array<BarButtonClass,2> referenceButtons,partialButtons;
							for(auto* buttons:{&referenceButtons,&partialButtons}){
								initializeButton((*buttons)[0],L"Prev");initializeButton((*buttons)[1],L"Next");
								for(auto& button:*buttons){button.pressScale.SetDirect(dpi==96?1.0:dpi==144?0.94:1.07);
									button.icon.angle.SetDirect(17);button.icon.contentScale=1.08;button.name.contentScale=1.05;}
							}
							BarUiWordClass secondary;secondary.Initialization(0,31,50,13,L"/12",11,RGB(235,235,235));
							secondary.enable.Initialization(true);secondary.pct.SetDirect(1);
							BarUiShapeClass sidePanel(0,0,76,76,7,7,1,RGB(25,30,36),RGB(200,210,220));
							sidePanel.enable.Initialization(true);sidePanel.pct.SetDirect(0.85);sidePanel.frameRendering=BarUiFrameRenderingEnum::PointLight;
							sidePanel.framePct.emplace(0.25);sidePanel.frameLightPct.emplace(1);
							const auto popup=dpi==96?D2D1::Matrix3x2F::Identity():dpi==144
								?D2D1::Matrix3x2F::Translation(24.5F,12.25F)
								:D2D1::Matrix3x2F::Scale(1.03F,0.91F)*D2D1::Matrix3x2F::Rotation(4,D2D1::Point2F(100,100))
									*D2D1::Matrix3x2F::Translation(24.5F,12.25F);
							const auto fullClip=D2D1::RectF(0,0,targetWidth*96.0F/dpi,targetHeight*96.0F/dpi);
							// 独立推导一个包含左按钮和光晕的宽松 dirty，右侧按钮仍明确在外。
							D2D1_RECT_F dirty{100000,100000,-100000,-100000};
							for(const double x:{-15.0*zoom,105.0*zoom})for(const double y:{-15.0*zoom,105.0*zoom}){
								const FLOAT px=static_cast<FLOAT>(x*popup._11+y*popup._21+popup._31);
								const FLOAT py=static_cast<FLOAT>(x*popup._12+y*popup._22+popup._32);
								dirty.left=(std::min)(dirty.left,px);dirty.top=(std::min)(dirty.top,py);
								dirty.right=(std::max)(dirty.right,px);dirty.bottom=(std::max)(dirty.bottom,py);
							}
							const auto draw=[&](BarUIRendering& renderer,std::array<BarButtonClass,2>& buttons,bool changed,const D2D1_RECT_F& clip)
							{
								auto* dc=renderer.GetDeviceContext();dc->SetDpi(static_cast<FLOAT>(dpi),static_cast<FLOAT>(dpi));
								dc->SetUnitMode(D2D1_UNIT_MODE_DIPS);dc->SetTransform(D2D1::IdentityMatrix());
								renderer.SetFrameZoom(zoom);renderer.SetFrameDiffuseReferenceZoom(zoom*1.1);
								renderer.SetFrameDiffuseMaskGeometryScale(1.06);
								BarUiFrameLightingSnapshot lighting{};lighting.primaryLight={180,100};lighting.primaryRadius=480;
								lighting.primaryLightVisible=true;lighting.edgeLightingEnabled=true;renderer.SetFrameLightingSnapshot(lighting);
								buttons[0].button.fill->SetDirect(changed?RGB(210,90,25):RGB(45,85,110));
								buttons[0].name.SetStringImmediate(changed?L"Changed":L"Prev");
								renderer.BeginFrameDraw(dc,clip);dc->SetTransform(popup);
								renderer.PushPanelOpacity(dc,D2D1::RectF(0,0,230*static_cast<FLOAT>(zoom),90*static_cast<FLOAT>(zoom)),0.82);
								(void)DrawBarBackgroundVisual(renderer,dc,sidePanel,BarUiInheritClass(147,7));
								BarButtonDrawOptions options;options.secondary=&secondary;
								(void)DrawBarButtonVisual(renderer,dc,buttons[0],BarUiInheritClass(10,10),options);
								(void)DrawBarButtonVisual(renderer,dc,buttons[1],BarUiInheritClass(150,10),options);
								dc->PopLayer();dc->SetTransform(D2D1::IdentityMatrix());renderer.FinishFrameDrawCommands(dc);
								HDC source=nullptr;HRESULT hr=renderer.GetGdiInteropRenderTarget()->GetDC(D2D1_DC_INITIALIZE_MODE_COPY,&source);
								if(SUCCEEDED(hr)){const RECT unchanged{};hr=renderer.GetGdiInteropRenderTarget()->ReleaseDC(&unchanged);}
								const HRESULT end=renderer.EndFrameDraw(dc);return SUCCEEDED(hr)&&SUCCEEDED(end);
							};
							expect(draw(full,referenceButtons,false,fullClip)&&draw(retained,partialButtons,false,fullClip),"shared dirty cull seed full frames");
							expect(draw(full,referenceButtons,true,fullClip),"shared dirty cull independent full reference");
							const auto expected=ReadSvgProofPixels(full.GetDeviceContext(),full.GetTargetBitmap());
							expect(draw(retained,partialButtons,true,dirty),"shared dirty cull retained partial transaction");
							const auto actual=ReadSvgProofPixels(retained.GetDeviceContext(),retained.GetTargetBitmap());
							expect(!expected.empty()&&actual==expected,"shared dirty cull retains exact BGRA with DPI/zoom/press/popup/light/layer and secondary text");
							expect(retained.GetFrameCulledDrawCount()>=2,"shared dirty cull really rejects standard button and independent panel draws");
							expect(partialButtons[1].button.inhX==150&&partialButtons[1].button.inhY==10,"culled standard button still publishes current inherited hit geometry");
							report<<"[Ui3DirtyCull] zoom="<<zoom<<" dpi="<<dpi<<" rejected="<<retained.GetFrameCulledDrawCount()<<" exactPixels="<<(actual==expected)<<'\n';
							full.DiscardDeviceResources();retained.DiscardDeviceResources();
						}
						// 未知自定义内容可能超出按钮，必须实际执行；普通 glyph/shape 仍可各自裁剪。
						BarUIRendering custom(&owner);const bool customReady=SUCCEEDED(custom.EnsureDeviceResources(RenderPipeline::GetDeviceEpoch(),256,128));
						expect(customReady,"shared dirty cull custom path target");
						if(customReady){
							auto* dc=custom.GetDeviceContext();custom.SetFrameZoom(1);dc->SetTransform(D2D1::IdentityMatrix());
							BarButtonClass button;initializeButton(button,L"Outside");unsigned calls=0;
							BarButtonDrawOptions options;options.content=[&](BarUIRendering& renderer,ID2D1DeviceContext* context,BarButtonClass&,const BarButtonVisualInheritance&){
								++calls;auto* brush=renderer.ContentSolidBrush(RGB(255,0,0));if(!brush)return false;
								context->FillRectangle(D2D1::RectF(5,5,15,15),brush);return true;
							};
							custom.BeginFrameDraw(dc,D2D1::RectF(0,0,40,40));
							(void)DrawBarButtonVisual(custom,dc,button,BarUiInheritClass(150,10),options);
							custom.FinishFrameDrawCommands(dc);expect(SUCCEEDED(custom.EndFrameDraw(dc)),"shared dirty cull custom transaction");
							const auto pixel=ReadEraserTestPixel(dc,custom.GetTargetBitmap(),10,10);
							expect(calls==1&&pixel[2]>0&&pixel[3]>0,"unbounded custom callback is not rejected with button chrome");
							expect(custom.ShouldDrawFrameBounds(dc,D2D1::RectF(1000,1000,1010,1010)),"no active dirty clip always preserves original draw path");
							custom.BeginFrameDraw(dc,D2D1::RectF(0,0,40,40));
							const auto invalid=D2D1::RectF((std::numeric_limits<FLOAT>::quiet_NaN)(),0,10,10);
							expect(custom.ShouldDrawFrameBounds(dc,invalid),"invalid bounds conservatively bypass rejection");
							{auto finiteProbe=std::make_unique<Ui3SvgProbe>(0xB380);SvgObservationScope scope(finiteProbe.get());
								expect(custom.ShouldDrawFrameBounds(dc,D2D1::RectF(1000,1000,1010,1010)),"finite SVG resource observation keeps original draw calls");}
							custom.FinishFrameDrawCommands(dc);expect(SUCCEEDED(custom.EndFrameDraw(dc)),"shared dirty cull fallback transaction");
						}
						custom.DiscardDeviceResources();
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
		// 生产分页按钮的图标缓存必须随独立 WARP device epoch 重建。
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

			BarPageSurface scene;
			BarPageSurfaceContent content;
			content.whiteboard=true;
			content.currentPage=L"1";
			content.totalPage=L"/3";
			const bool configured=scene.ConfigurePagination(content)
				&& scene.SetBounds({0,0,305,80},1.0F);
			(void)scene.Advance(std::chrono::steady_clock::now());
			expect(configured,"production PageSurface icon configures");
			const bool savedFractional=Inkeys::config.Experimental.Inkeys3.UI3.AllowFractionalFontSize.load();
			const auto policyTime=std::chrono::steady_clock::now();
			Inkeys::config.Experimental.Inkeys3.UI3.AllowFractionalFontSize=false;
			(void)scene.Advance(policyTime);(void)scene.ConsumeDamage();
			expect(!scene.IsInvalidated(),"font policy fixture starts with consumed damage");
			Inkeys::config.Experimental.Inkeys3.UI3.AllowFractionalFontSize=true;
			(void)scene.Advance(policyTime);
			expect(scene.IsInvalidated(),"same-time idle PageSurface invalidates on font policy change");
			scene.SetOpacity(0,0);
			(void)scene.Advance(policyTime+std::chrono::milliseconds(1));(void)scene.ConsumeDamage();
			expect(!scene.IsInvalidated(),"hidden font policy fixture starts without geometry damage");
			Inkeys::config.Experimental.Inkeys3.UI3.AllowFractionalFontSize=false;
			(void)scene.Advance(policyTime+std::chrono::milliseconds(1));
			expect(scene.IsInvalidated(),"hidden PageSurface applies font policy before restoring visibility");
			scene.SetOpacity(1,0);
			Inkeys::config.Experimental.Inkeys3.UI3.AllowFractionalFontSize=savedFractional;

			const RECT presentation=scene.PresentationBounds();
			const UINT width=static_cast<UINT>(presentation.right-presentation.left);
			const UINT height=static_cast<UINT>(presentation.bottom-presentation.top);
			auto frameTime=std::chrono::steady_clock::now();
			auto drawPageSurface=[&](const RenderPipeline::DeviceEpoch& epoch,UINT targetWidth,
				UINT targetHeight,std::vector<unsigned char>& pixels)
			{
				if(FAILED(scene.EnsureDeviceResources(epoch,targetWidth,targetHeight)))return false;
				auto* context=scene.DeviceContext();
				if(!context)return false;
				scene.Renderer().BeginFrameDraw(context,D2D1::RectF(0,0,
					static_cast<FLOAT>(targetWidth),static_cast<FLOAT>(targetHeight)));
				const bool rendered=scene.Render(context,frameTime).rendered;
				scene.Renderer().FinishFrameDrawCommands(context);
				const HRESULT endDraw=scene.Renderer().EndFrameDraw(context);
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
				const bool drewBaseline=drawPageSurface(epochA,width,height,baseline);
				expect(drewBaseline,"PageSurface icon renders in first epoch");
				if(drewBaseline)
				{
					bool visible=false;
					for(size_t pixel=3;pixel<baseline.size();pixel+=4)
						visible|=baseline[pixel]!=0;
					expect(visible,"PageSurface SVG contributes visible pixels");
					// 主栏主题切换必须直接作用于分页的真实背景，不能另行固定为深色。
					const bool originalTheme=owner.barStyle.darkStyle;
					for(const bool dark:{false,true})
					{
						owner.barStyle.darkStyle=dark;
						for(int frame=0;frame<40;++frame){frameTime+=std::chrono::milliseconds(20);(void)scene.Advance(frameTime);}
						std::vector<unsigned char> themed;
						const bool rendered=drawPageSurface(epochA,width,height,themed);
						expect(rendered,"PageSurface redraws after the shared main theme changes");
						const UINT sampleX=static_cast<UINT>(77-presentation.left),sampleY=static_cast<UINT>(40-presentation.top);
						if(rendered&&sampleX<width&&sampleY<height)
						{
							const auto at=(static_cast<size_t>(sampleY)*width+sampleX)*4;
							const auto expected=GetThemeColor(BarThemeColorEnum::Surface);
							const auto premultiplied=[](int channel){return static_cast<int>(std::lround(channel*BarMainBarFillOpacity));};
							expect(std::abs(int(themed[at+3])-premultiplied(255))<=1
								&&std::abs(int(themed[at])-premultiplied(GetBValue(expected)))<=2
								&&std::abs(int(themed[at+1])-premultiplied(GetGValue(expected)))<=2
								&&std::abs(int(themed[at+2])-premultiplied(GetRValue(expected)))<=2,
								"PageSurface dark/light pixels follow the same UI3 surface color as the main bar");
						}
					}
					owner.barStyle.darkStyle=originalTheme;
					for(int frame=0;frame<40;++frame){frameTime+=std::chrono::milliseconds(20);(void)scene.Advance(frameTime);}
					scene.ReleaseDeviceResources();
					const bool drewAfterRelease=drawPageSurface(epochB,width,height,afterRelease);
					expect(drewAfterRelease,"PageSurface icon renders after explicit release on new epoch");
					if(drewAfterRelease)
					{
						expect(afterRelease==baseline,"new epoch retains exact BGRA icon pixels");
						expect(scene.EnsureDeviceResources(epochA,0,height)==E_INVALIDARG,
						"invalid target does not replace working epoch");
						expect(drawPageSurface(epochB,width,height,preserved)
						&& preserved==afterRelease,"failed setup preserves current PageSurface pixels");
						expect(drawPageSurface(epochA,width,height,afterImplicitRecreate)
						&& afterImplicitRecreate==baseline,
						"implicit epoch recreation retains exact BGRA icon pixels");
						expect(drawPageSurface(epochA,width+8,height+8,afterResize),
						"target resize recreates PageSurface resources");
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
