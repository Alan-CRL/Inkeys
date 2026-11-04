#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>
#include <d2d1_1.h>
#include <wrl/client.h>
#include "../Inkeys/Inkeys/UI/StartupPlacement.h"
#include "../Inkeys/Inkeys/UI/StartupPreview/StartupPreview.Effects.Drawing.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dxguid.lib")

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <cstring>
#include <vector>
#include <utility>

#include <json/json.h>

import Inkeys.Display;
import Inkeys.Other.Config;
import Inkeys.UI.StartupPreview;

// Config 生产模块通过 IdtMain.h 引用该路径；headless fixture 将其限定到临时目录。
std::wstring globalPath;
std::wstring editionVersion;
std::wstring editionDate;
std::wstring userId;
std::wstring programArchitecture;
std::wstring targetArchitecture;
std::wstring windowsEdition;

namespace
{
	bool Expect(bool condition, const char* name)
	{
		if (!condition)
			std::cerr << "[StartupPreviewState] failed: " << name << '\n';
		return condition;
	}

	struct LegacyConfigResult final
	{
		bool read = false;
		bool enabled = true;
		double widthDip = 0.0;
		double zoom = 0.0;
		int taskbarBase = 0;
	};

	LegacyConfigResult ReadLegacyStartupPreviewFixture()
	{
		namespace fs = std::filesystem;
		const fs::path root = fs::temp_directory_path()
			/ (L"inkeys-startup-preview-config-"
				+ std::to_wstring(GetCurrentProcessId()) + L"-"
				+ std::to_wstring(GetTickCount64()));
		struct Cleanup final
		{
			fs::path root;
			std::wstring previousGlobalPath;
			~Cleanup()
			{
				globalPath = previousGlobalPath;
				std::error_code ignored;
				fs::remove_all(root, ignored);
			}
		} cleanup{ root, globalPath };

		std::error_code error;
		const fs::path configPath = root / L"Inkeys" / L"Config" / L"main.json";
		fs::create_directories(configPath.parent_path(), error);
		if (error) return {};

		Json::Value document(Json::objectValue);
		auto& preview = document["Experimental"]["Inkeys3"]["UI3"]
			["StartupPreview"];
		preview["Enable"] = false;
		// 模拟旧图片方案遗留字段；新 schema 应忽略它且补上缺失宽度默认值。
		preview["RetiredField"] = "ignored";
		document["UI"]["Bar"]["Zoom"] = 1.75;
		Json::StreamWriterBuilder writer;
		writer["indentation"] = "";
		{
			std::ofstream output(configPath, std::ios::binary | std::ios::trunc);
			if (!output) return {};
			output << Json::writeString(writer, document);
			if (!output) return {};
		}

		globalPath = root.wstring() + L"\\";
		Inkeys::Config config;
		const bool read = config.ReadMini({
			"Experimental.Inkeys3.UI3.StartupPreview.Enable",
			"Experimental.Inkeys3.UI3.StartupPreview.CachedStartupBarWidthDip",
			"UI.Bar.Zoom2", "UI.Bar.UseTaskbarHeightAsScaleBase" });
		return {
			read,
			config.Experimental.Inkeys3.UI3.StartupPreview.Enable.load(),
			config.Experimental.Inkeys3.UI3.StartupPreview.
				CachedStartupBarWidthDip.load(),
			config.UI.Bar.Zoom2.load(),
			config.UI.Bar.UseTaskbarHeightAsScaleBase.load(),
		};
	}
	int RunStartupPlacementConfigTests()
	{
		using namespace Inkeys::UI;
		namespace fs = std::filesystem;
		int failures = 0;
		const fs::path root = fs::temp_directory_path() / (L"inkeys-startup-placement-"
			+ std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
		const std::wstring previousPath = globalPath;
		struct Cleanup final
		{
			fs::path root;
			std::wstring previous;
			~Cleanup() { globalPath = previous; std::error_code ignored; fs::remove_all(root, ignored); }
		} cleanup{ root, previousPath };
		const fs::path path = root / L"Inkeys" / L"Config" / L"main.json";
		std::error_code error;
		fs::create_directories(path.parent_path(), error);
		if (!Expect(!error, "create startup placement config fixture")) return 1;
		globalPath = root.wstring() + L"\\";
		auto write = [&](const Json::Value& document)
		{
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			Json::StreamWriterBuilder writer; writer["indentation"] = "";
			file << Json::writeString(writer, document);
			return file.good();
		};
		// 预期值由案例明确给出，不重复生产类型判断；通过真实 JSON 写入和 Mini/All 读取验证。
		const std::pair<Json::Value, int> cases[] = {
			{ Json::Value(), 0 }, { Json::Value(-1), 0 },
			{ Json::Value(0), 0 }, { Json::Value(1), 1 }, { Json::Value(2), 2 }, { Json::Value(3), 0 },
			{ Json::Value(Json::UInt(1)), 1 }, { Json::Value(Json::UInt(2)), 2 },
			{ Json::Value(1.0), 0 }, { Json::Value(2.0), 0 }, { Json::Value(1.5), 0 },
			{ Json::Value(true), 0 }, { Json::Value(false), 0 },
			{ Json::Value("1"), 0 }, { Json::Value("left"), 0 },
			{ Json::Value(Json::UInt64(4294967297ULL)), 0 },
			{ Json::Value(Json::UInt64(4294967298ULL)), 0 },
			{ Json::Value((std::numeric_limits<Json::UInt64>::max)()), 0 },
			{ Json::Value((std::numeric_limits<Json::Int64>::min)()), 0 },
			{ Json::Value((std::numeric_limits<Json::Int64>::max)()), 0 },
			{ Json::Value(Json::arrayValue), 0 }, { Json::Value(Json::objectValue), 0 }
		};
		for (const auto& [value, expected] : cases)
		{
			Json::Value document(Json::objectValue);
			if (!value.isNull()) document["UI"]["Bar"]["StartupPlacement"] = value;
			document["UI"]["Bar"]["Zoom2"] = 1.25;
			document["UI"]["Bar"]["UseTaskbarHeightAsScaleBase"] = false;
			if (!Expect(write(document), "write startup placement input fixture")) { ++failures; continue; }
			Inkeys::Config mini;
			const bool miniRead = mini.ReadMini({ "UI.Bar.StartupPlacement", "UI.Bar.Zoom2", "UI.Bar.UseTaskbarHeightAsScaleBase" });
			Inkeys::Config all;
			const bool allRead = all.ReadAll();
			if (!Expect(miniRead && allRead && mini.UI.Bar.Zoom2.load() == 1.25
				&& mini.UI.Bar.StartupPlacement.load() == expected
				&& all.UI.Bar.StartupPlacement.load() == expected
				&& mini.UI.Bar.UseTaskbarHeightAsScaleBase.load() == 0
				&& all.UI.Bar.UseTaskbarHeightAsScaleBase.load() == 0,
				"Mini/All normalize missing, illegal and all three startup placements consistently")) ++failures;
		}
		const std::pair<Json::Value, int> scaleModes[] = {
			{ Json::Value(false), 0 }, { Json::Value(true), 2 },
			{ Json::Value(0), 0 }, { Json::Value(1), 1 }, { Json::Value(2), 2 }
		};
		for (const auto& [value, expected] : scaleModes)
		{
			Json::Value document(Json::objectValue);
			document["UI"]["Bar"]["UseTaskbarHeightAsScaleBase"] = value;
			document["UI"]["Bar"]["Zoom2"] = 1.25;
			if (!Expect(write(document), "write startup scale mode fixture")) { ++failures; continue; }
			Inkeys::Config config;
			if (!Expect(config.ReadMini({ "UI.Bar.Zoom2", "UI.Bar.UseTaskbarHeightAsScaleBase" })
				&& config.UI.Bar.UseTaskbarHeightAsScaleBase.load() == expected,
				"startup Mini distinguishes legacy bool from all three integer modes")) { ++failures; continue; }
			StartupPreview::StartOptions frozen;
			frozen.dpi = 144;
			frozen.barZoom = config.UI.Bar.Zoom2.load();
			frozen.uiBaseScale = Inkeys::Display::ResolveUiScaleBase(144, 96, 0, 0,
				static_cast<Inkeys::Display::UiScaleBaseMode>(config.UI.Bar.UseTaskbarHeightAsScaleBase.load()));
			document["UI"]["Bar"]["UseTaskbarHeightAsScaleBase"] = (expected + 1) % 3;
			document["UI"]["Bar"]["Zoom2"] = 2.0;
			const double expectedHeight = expected == 0 ? 120.0 : expected == 1 ? 72.0 : 96.0;
			if (!Expect(write(document) && config.ReadAll()
				&& config.UI.Bar.UseTaskbarHeightAsScaleBase.load() == (expected + 1) % 3
				&& StartupPreview::ResolveStartupPreviewPixelSize(470.0, frozen.dpi,
					frozen.barZoom, frozen.uiBaseScale).height == std::lround(expectedHeight * 1.25),
				"full config read cannot mutate frozen startup scale mode or multiplier")) ++failures;
		}
		Json::Value document(Json::objectValue);
		document["UI"]["Bar"]["StartupPlacement"] = 1;
		if (!Expect(write(document), "write frozen-startup fixture")) return failures + 1;
		Inkeys::Config config;
		if (!Expect(config.ReadMini({ "UI.Bar.StartupPlacement" }), "read startup mini")) return failures + 1;
		StartupPreview::StartOptions frozen;
		frozen.startupPlacement = NormalizeStartupPlacement(config.UI.Bar.StartupPlacement.load());
		document["UI"]["Bar"]["StartupPlacement"] = 2;
		if (!Expect(write(document) && config.ReadAll()
			&& config.UI.Bar.StartupPlacement.load() == 2
			&& frozen.startupPlacement == StartupPlacement::BottomLeftFloating,
			"full config read cannot mutate this startup's frozen Preview choice")) ++failures;
		for (int placement = 0; placement != 3; ++placement)
		{
			config.UI.Bar.StartupPlacement = placement;
			Inkeys::Config payload;
			payload = config; // 与设置FIFO相同的值快照，不引用活动config。
			config.UI.Bar.StartupPlacement = (placement + 1) % 3;
			const bool saved = payload.Write();
			Inkeys::Config next;
			if (!Expect(saved && next.ReadMini({ "UI.Bar.StartupPlacement" })
				&& next.UI.Bar.StartupPlacement.load() == placement
				&& frozen.startupPlacement == StartupPlacement::BottomLeftFloating,
				"frozen write payload round-trips selected placement only for next launch")) ++failures;
		}
		return failures;
	}

	int RunStartupPlacementGeometryTests()
	{
		using namespace Inkeys::UI;
		int failures = 0;
		const RECT monitor{ -2560, -400, 0, 1040 };
		const RECT work{ -2500, -360, 0, 1000 };
		for (const UINT dpi : { 96U, 120U, 144U, 192U })
			for (const double zoom : { .75, 1.0, 1.25, 1.5 })
			{
				const double scale = StartupPreview::ResolveStartupPreviewScale(dpi, zoom);
				const LONG margin = static_cast<LONG>(std::lround(24.0 * scale));
				for (const auto placement : { StartupPlacement::BottomLeftFloating, StartupPlacement::BottomRightFloating })
				{
					const auto bounds = StartupPreview::ResolveStartupPreviewBounds(monitor, work,
						470.0, 80.0, dpi, zoom, placement);
					const auto shared = ResolveStartupPlacementBounds(placement, monitor, work, 81.0, 81.0,
						scale, static_cast<double>(dpi) / 96.0);
					if (!Expect(EqualRect(&bounds, &shared)
						&& bounds.right - bounds.left == std::lround(81.0 * scale)
						&& bounds.bottom - bounds.top == std::lround(81.0 * scale)
						&& work.bottom - bounds.bottom == margin
						&& (placement == StartupPlacement::BottomLeftFloating
							? bounds.left - work.left : work.right - bounds.right) == margin,
						"negative-origin Preview/formal visible stroke envelope has equal 24 DIP margins")) ++failures;
				}
			}
		for (double desktopLine : { 992.0, 1034.0, 1040.0 })
		{
			const auto preview = StartupPreview::ResolveStartupPreviewBounds(monitor, work,
				470.0, 80.0, 120U, 1.75, StartupPlacement::CenteredBottomBar, .6, desktopLine);
			const auto formal = ResolveStartupPlacementBounds(StartupPlacement::CenteredBottomBar,
				monitor, work, 471.0, 81.0, .6 * 1.75, 1.25, desktopLine);
			if (!Expect(EqualRect(&preview, &formal) && formal.bottom == desktopLine,
				"centered Preview and formal Bar share raised/hidden/side taskbar line at independent UI scale")) ++failures;
		}
		return failures;
	}

	struct BgraPixel final { unsigned char blue, green, red, alpha; };

	class OffscreenPreview final
	{
		using MicrosoftComPtr = Microsoft::WRL::ComPtr<ID2D1DeviceContext>;
		Microsoft::WRL::ComPtr<ID3D11Device> device;
		Microsoft::WRL::ComPtr<ID2D1Factory1> factory;
		Microsoft::WRL::ComPtr<ID2D1Device> d2d;
		MicrosoftComPtr context;
		Microsoft::WRL::ComPtr<ID2D1Bitmap1> target, mask, readable;
		Microsoft::WRL::ComPtr<ID2D1Geometry> shape;
		Microsoft::WRL::ComPtr<ID2D1StrokeStyle> ringStroke;
		Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fill, frame, track, indicator, maskFill, maskStroke;
		Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> shimmer;
		bool floating = false;
		Inkeys::UI::StartupPlacement placement = Inkeys::UI::StartupPlacement::CenteredBottomBar;
		float scale = 1.f;
		Inkeys::UI::StartupPreview::Drawing::EffectRenderer effectRenderer;
		Inkeys::UI::StartupPreview::Effects::Appearance effectAppearance;
		Inkeys::UI::StartupPreview::Effects::Rect effectTarget;
		Inkeys::UI::StartupPreviewEffect effectType = Inkeys::UI::StartupPreviewEffect::Shimmer;
	public:
		UINT width = 0, height = 0;
		HRESULT Initialize(Inkeys::UI::StartupPlacement startupPlacement, float pixelScale)
		{
			using namespace Inkeys::UI::StartupPreview;
			placement = startupPlacement;
			floating = placement != Inkeys::UI::StartupPlacement::CenteredBottomBar; scale = pixelScale;
			width = static_cast<UINT>(std::lround((floating ? 81.0 : 471.0) * scale));
			height = static_cast<UINT>(std::lround(81.0 * scale));
			HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
				D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, nullptr);
			if (FAILED(hr)) return hr;
			Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
			hr = device.As(&dxgi); if (FAILED(hr)) return hr;
			hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()); if (FAILED(hr)) return hr;
			hr = factory->CreateDevice(dxgi.Get(), &d2d); if (FAILED(hr)) return hr;
			hr = d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context); if (FAILED(hr)) return hr;
			const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
				D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
			hr = context->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0, &properties, &target); if (FAILED(hr)) return hr;
			const auto maskProperties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
				D2D1::PixelFormat(DXGI_FORMAT_A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
			hr = context->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0, &maskProperties, &mask); if (FAILED(hr)) return hr;
			const auto readProperties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
				D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
			hr = context->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0, &readProperties, &readable); if (FAILED(hr)) return hr;
			hr = Drawing::CreateShape(factory.Get(), static_cast<float>(width), static_cast<float>(height), scale, floating, &shape); if (FAILED(hr)) return hr;
			hr = Drawing::CreateRingStroke(factory.Get(), &ringStroke); if (FAILED(hr)) return hr;
			hr = context->CreateSolidColorBrush(D2D1::ColorF(24.f/255.f, 24.f/255.f, 24.f/255.f), &fill); if (FAILED(hr)) return hr;
			hr = context->CreateSolidColorBrush(D2D1::ColorF(1.f, 1.f, 1.f), &frame); if (FAILED(hr)) return hr;
			hr = context->CreateSolidColorBrush(D2D1::ColorF(1.f, 1.f, 1.f, .24f), &track); if (FAILED(hr)) return hr;
			hr = context->CreateSolidColorBrush(D2D1::ColorF(.38f, .80f, 1.f), &indicator); if (FAILED(hr)) return hr;
			hr = context->CreateSolidColorBrush(D2D1::ColorF(1.f, 1.f, 1.f), &maskFill); if (FAILED(hr)) return hr;
			hr = context->CreateSolidColorBrush(D2D1::ColorF(1.f, 1.f, 1.f), &maskStroke); if (FAILED(hr)) return hr;
			const auto gradient = ResolveStartupPreviewShimmerGradient(width, height);
			hr = Drawing::CreateShimmer(context.Get(), D2D1::Point2F(static_cast<float>(gradient.startX), static_cast<float>(gradient.startY)),
				D2D1::Point2F(static_cast<float>(gradient.endX), static_cast<float>(gradient.endY)), &shimmer); if (FAILED(hr)) return hr;
			context->SetTarget(mask.Get()); context->BeginDraw(); context->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 0.f));
			Drawing::DrawShape(context.Get(), shape.Get(), maskFill.Get(), maskStroke.Get(), scale,
				static_cast<float>(floating ? StartupPreviewFloatingFillAlpha : StartupPreviewFillAlpha),
				static_cast<float>(StartupPreviewFrameAlpha));
			hr = context->EndDraw(); context->SetTarget(nullptr); return hr;
		}

		std::vector<BgraPixel> Render(double ratio, bool failed, bool showProgress, double shimmerPhase)
		{
			using namespace Inkeys::UI::StartupPreview;
			context->SetTarget(target.Get()); context->BeginDraw(); context->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 0.f));
			Drawing::DrawShape(context.Get(), shape.Get(), fill.Get(), frame.Get(), scale,
				static_cast<float>(floating ? StartupPreviewFloatingFillAlpha : StartupPreviewFillAlpha),
				static_cast<float>(StartupPreviewFrameAlpha));
			if (shimmerPhase >= 0.0)
			{
				const auto gradient = ResolveStartupPreviewShimmerGradient(width, height);
				const auto travel = ResolveShimmerTravel({ 0., 0., static_cast<double>(width), static_cast<double>(height) },
					gradient, (std::max)(8.0, std::hypot(gradient.endX-gradient.startX, gradient.endY-gradient.startY)*.10));
				const auto translation = ResolveShimmerTranslation(travel, shimmerPhase);
				shimmer->SetStartPoint(D2D1::Point2F(static_cast<float>(gradient.startX+translation.x), static_cast<float>(gradient.startY+translation.y)));
				shimmer->SetEndPoint(D2D1::Point2F(static_cast<float>(gradient.endX+translation.x), static_cast<float>(gradient.endY+translation.y)));
				Drawing::DrawShimmer(context.Get(), mask.Get(), shimmer.Get(), static_cast<float>(width), static_cast<float>(height));
			}
			HRESULT draw = S_OK;
			if (showProgress) draw = Drawing::DrawProgress(context.Get(), ringStroke.Get(), track.Get(), indicator.Get(),
				placement, static_cast<float>(width), static_cast<float>(height), scale, ratio, 1.f, failed);
			const HRESULT end = context->EndDraw(); context->SetTarget(nullptr);
			if (FAILED(draw) || FAILED(end) || FAILED(readable->CopyFromBitmap(nullptr, target.Get(), nullptr))) return {};
			D2D1_MAPPED_RECT mapped{};
			if (FAILED(readable->Map(D2D1_MAP_OPTIONS_READ, &mapped))) return {};
			std::vector<BgraPixel> pixels(static_cast<size_t>(width)*height);
			for (UINT y=0; y<height; ++y) std::memcpy(pixels.data()+static_cast<size_t>(y)*width,
				mapped.bits+static_cast<size_t>(y)*mapped.pitch, static_cast<size_t>(width)*sizeof(BgraPixel));
			(void)readable->Unmap(); return pixels;
		}
		HRESULT InitializeEffect(Inkeys::UI::StartupPlacement startupPlacement, float pixelScale,
			Inkeys::UI::StartupPreviewEffect effect, int theme, int background = 0, int content = 100, bool allowSpriteBatch = true)
		{
			using namespace Inkeys::UI::StartupPreview;
			HRESULT hr = Initialize(startupPlacement, pixelScale); if (FAILED(hr)) return hr;
			effectType = effect;
			const float w = static_cast<float>(width), h = static_cast<float>(height);
			const Effects::Rect work{0.f, 0.f, std::round(1200.f * scale), std::round(440.f * scale)};
			const float x = startupPlacement == Inkeys::UI::StartupPlacement::BottomLeftFloating ? std::round(24.f * scale)
				: startupPlacement == Inkeys::UI::StartupPlacement::BottomRightFloating ? work.right - std::round(24.f * scale) - w
				: std::round((work.right - w) * .5f);
			const float bottom = std::round(400.f * scale);
			const Effects::Rect destination{x, bottom-h, x+w, bottom};
			const auto scatter = effect == Inkeys::UI::StartupPreviewEffect::PixelAssembly
				? Effects::ScatterBounds(destination, work, floating) : destination;
			const auto presentation = effect == Inkeys::UI::StartupPreviewEffect::PixelAssembly
				? Effects::PresentationBounds(destination, scatter, work, scale) : destination;
			effectTarget = {destination.left-presentation.left,destination.top-presentation.top,
				destination.right-presentation.left,destination.bottom-presentation.top};
			const Effects::Rect localScatter{scatter.left-presentation.left,scatter.top-presentation.top,
				scatter.right-presentation.left,scatter.bottom-presentation.top};
			width = static_cast<UINT>(Effects::Width(presentation)); height = static_cast<UINT>(Effects::Height(presentation));
			const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
				D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
			hr=context->CreateBitmap(D2D1::SizeU(width,height),nullptr,0,&properties,&target); if(FAILED(hr))return hr;
			const auto readProperties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
				D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
			hr=context->CreateBitmap(D2D1::SizeU(width,height),nullptr,0,&readProperties,&readable); if(FAILED(hr))return hr;
			effectAppearance=Effects::ResolveAppearance(theme,background,content,floating);
			return effectRenderer.Initialize(context.Get(),width,height,effectTarget,localScatter,scale,floating,effect,effectAppearance,50312,allowSpriteBatch);
		}
		const auto& EffectParticles() const noexcept { return effectRenderer.Particles(); }
		const auto& EffectTarget() const noexcept { return effectTarget; }
		RECT EffectContentBounds() const noexcept { return effectRenderer.ContentBounds(); }
		bool EffectUsesSpriteBatch() const noexcept { return effectRenderer.UsesSpriteBatch(); }
		std::vector<BgraPixel> RenderEffect(double elapsed,double ratio,bool failed,bool showProgress)
		{
			using namespace Inkeys::UI::StartupPreview;
			const double w=Effects::Width(effectTarget),h=Effects::Height(effectTarget);
			const auto gradient=ResolveStartupPreviewShimmerGradient(w,h);
			const auto travel=ResolveShimmerTravel({0.,0.,w,h},gradient,
				(std::max)(8.,std::hypot(gradient.endX-gradient.startX,gradient.endY-gradient.startY)*.1));
			const double shimmerTime=effectType==Inkeys::UI::StartupPreviewEffect::PixelAssembly
				? (std::max)(0.,elapsed-Effects::AssemblySeconds):elapsed;
			const auto movement=ResolveShimmerTranslation(travel,EaseShimmerPhase(std::fmod(shimmerTime,4.)/4.));
			HRESULT hr=effectRenderer.Prepare(elapsed,D2D1::Point2F(static_cast<float>(gradient.startX+movement.x),static_cast<float>(gradient.startY+movement.y)),
				D2D1::Point2F(static_cast<float>(gradient.endX+movement.x),static_cast<float>(gradient.endY+movement.y)));
			if(FAILED(hr)){std::cerr<<"Effect prepare HRESULT="<<std::hex<<hr<<std::dec<<'\n';return {};}
			context->SetTarget(target.Get());context->SetTransform(D2D1::Matrix3x2F::Identity());context->BeginDraw();context->Clear(D2D1::ColorF(0.f,0.f,0.f,0.f));
			effectRenderer.DrawBackground(context.Get());
			if(showProgress){context->SetTransform(D2D1::Matrix3x2F::Translation(effectTarget.left,effectTarget.top));
				hr=Drawing::DrawProgress(context.Get(),ringStroke.Get(),track.Get(),indicator.Get(),placement,
					static_cast<float>(w),static_cast<float>(h),scale,ratio,effectAppearance.contentOpacity,failed,effectAppearance.light);}
			context->SetTransform(D2D1::Matrix3x2F::Identity());const HRESULT end=context->EndDraw();context->SetTarget(nullptr);
			if(FAILED(hr)||FAILED(end)||FAILED(readable->CopyFromBitmap(nullptr,target.Get(),nullptr)))return {};
			D2D1_MAPPED_RECT mapped{};if(FAILED(readable->Map(D2D1_MAP_OPTIONS_READ,&mapped)))return {};
			std::vector<BgraPixel> pixels(static_cast<size_t>(width)*height);
			for(UINT y=0;y<height;++y)std::memcpy(pixels.data()+static_cast<size_t>(y)*width,mapped.bits+static_cast<size_t>(y)*mapped.pitch,static_cast<size_t>(width)*sizeof(BgraPixel));
			(void)readable->Unmap();return pixels;
		}

	};

	bool Blue(const BgraPixel& pixel) { return pixel.blue > pixel.red + 30 && pixel.green > pixel.red + 15; }
	bool Red(const BgraPixel& pixel) { return pixel.red > pixel.blue + 12 && pixel.red > pixel.green + 12; }

	void SavePreviewBitmap(const std::filesystem::path& name, UINT width, UINT height, const std::vector<BgraPixel>& pixels)
	{
		if (pixels.empty()) return;
		std::error_code ignored;
		const auto directory = std::filesystem::current_path() / L"Build" / L"StartupPreviewVisualQA";
		std::filesystem::create_directories(directory, ignored);
		BITMAPFILEHEADER file{}; file.bfType=0x4d42; file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);
		file.bfSize=file.bfOffBits+width*height*4;
		BITMAPINFOHEADER info{}; info.biSize=sizeof(info); info.biWidth=static_cast<LONG>(width);
		info.biHeight=-static_cast<LONG>(height); info.biPlanes=1; info.biBitCount=32; info.biCompression=BI_RGB;
		std::ofstream output(directory/name,std::ios::binary|std::ios::trunc);
		output.write(reinterpret_cast<const char*>(&file),sizeof(file)); output.write(reinterpret_cast<const char*>(&info),sizeof(info));
		output.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()*sizeof(BgraPixel)));
	}

	// 两条路径都为 ALIASED；仅速度线/微光的线性采样在 SpriteBatch 与 DrawBitmap 间有亚纹素差异（实测孤立最大146、均值≤0.26）；缺失整块像素会产生≥200的差值。
	constexpr int SpriteFallbackMaximumDelta = 160;
	constexpr double SpriteFallbackMeanDelta = .6;

	int RunStartupEffectTests()
	{
		using namespace Inkeys::UI;
		using namespace Inkeys::UI::StartupPreview;
		int failures=0;
		auto check=[&](bool condition,const char* name){if(!Expect(condition,name))++failures;};
		const auto dark=Effects::ResolveAppearance(1,0,100,false),light=Effects::ResolveAppearance(2,0,100,false),corner=Effects::ResolveAppearance(2,0,100,true);
		check(std::abs(dark.backgroundOpacity-StartupPreviewFillAlpha)<1e-6
			&& std::abs(corner.backgroundOpacity-StartupPreviewFloatingFillAlpha)<1e-6
			&& std::abs(dark.canonicalFrameOpacity*dark.backgroundOpacity-StartupPreviewFrameAlpha)<1e-6
			&& std::abs((dark.surface&255)/255.0-StartupPreviewSurfaceColorChannel)<1e-6,
			"effect appearance stays anchored to shared Bar surface and opacity metrics");
		check(light.light&&!corner.light&&std::abs(light.backgroundOpacity-.92f)<1e-6f,
			"light bar uses mist surface while both corner placements remain dark");
		for(int theme:{0,1,2,3})for(bool floating:{false,true})for(int background:{-1,0,30,64,100,150})
		{
			const auto a=Effects::ResolveAppearance(theme,background,30,floating);
			check(a.backgroundOpacity>0&&a.backgroundOpacity<=1&&std::abs(a.contentOpacity-.3f)<1e-6f,
				"theme opacity resolver independently normalizes background and content");
		}
		check(!Effects::AssemblyReady(StartupPreviewEffect::PixelAssembly,2.399)
			&& Effects::AssemblyReady(StartupPreviewEffect::PixelAssembly,2.4)
			&& Effects::AssemblyReady(StartupPreviewEffect::Aurora,0),"only pixel assembly gates fast success for 2.4 seconds");
		check(Effects::LatestArrivalSeconds+Effects::ArrivalFlashSeconds<=Effects::AssemblySeconds
			&&Effects::ParticleOpacity(0.f)==0.f&&Effects::ParticleOpacity(1.f)==1.f&&Effects::ParticleScale(1.f)==1.f&&Effects::ParticleScale(0.f)>.54f,
			"last arrival glint and particle fade/scale all finish before assembly completes");
		{
			// 柔光流彩色场：深浅都连续铺满，始终是粉紫系（绿色通道最低）且保持明显彩度；深色只用低明度色并保留明暗层次。
			for(bool lightTheme:{false,true}){
				const auto palette=Effects::MakeGlowPalette(lightTheme);
				std::vector<std::uint32_t> field(140*14);
				int minimumChroma=255,worstGreenLead=-255,brightest=0,darkest=255;bool opaque=true;
				for(double t=0.;t<60.;t+=2.3){
					Effects::GlowField(palette,t,140,14,560.f,56.f,field.data());
					for(const auto pixel:field){
						opaque=opaque&&(pixel>>24)==255;
						const int r=(pixel>>16)&255,g=(pixel>>8)&255,b=pixel&255,top=(std::max)({r,g,b});
						minimumChroma=(std::min)(minimumChroma,top-(std::min)({r,g,b}));
						worstGreenLead=(std::max)(worstGreenLead,g-(std::min)(r,b));
						brightest=(std::max)(brightest,top);darkest=(std::min)(darkest,top);
					}
				}
				if(minimumChroma<45||worstGreenLead>6||!lightTheme&&(brightest>230||darkest>120||brightest-darkest<60))
					std::cerr<<"[GlowField] light="<<lightTheme<<" minimumChroma="<<minimumChroma<<" worstGreenLead="<<worstGreenLead<<" brightest="<<brightest<<" darkest="<<darkest<<'\n';
				check(opaque&&minimumChroma>=45&&worstGreenLead<=6,"soft glow field stays continuous, saturated and locked to the lavender-pink family");
				if(!lightTheme)check(brightest<=230&&darkest<=120&&brightest-darkest>=60,"dark soft glow uses only deep tones with brighter cores over a deep plum base");
			}
		}
		std::vector<BgraPixel> fixture(9000,{24,24,24,255});
		const auto* bytes=reinterpret_cast<const std::uint8_t*>(fixture.data());
		check(Effects::BuildParticles(bytes,9000,1,36000,{0,0,9000,1},{-4500,-2,13500,1},.1f,false,1).size()<=Effects::MaximumParticles,
			"particle count is strictly bounded for a very wide one-pixel strip");
		check(Effects::BuildParticles(bytes,2,2,8,{}, {0,0,2,2},1,false,1).empty()
			&&Effects::BuildParticles(bytes,2,2,8,{0,0,2,2},{0,0,2,2},std::numeric_limits<float>::quiet_NaN(),false,1).empty(),
			"invalid geometry or scale cannot enter particle origin rejection sampling");
		const Effects::Rect target{-500,-100,-29,-19},work{-1600,-700,600,0};
		const auto area=Effects::ScatterBounds(target,work,false);
		check(std::abs(Effects::Width(area)-706.5)<.001&&std::abs(Effects::Height(area)-194.4)<.001&&area.bottom==target.bottom
			&&std::abs((area.left+area.right)*.5f-(target.left+target.right)*.5f)<.001,
			"center particle source area is 1.5W by 2.4H and keeps the bottom center on negative coordinates");
		const auto clippedArea=Effects::ScatterBounds(target,{-1200,-700,0,0},false);
		check(clippedArea.left==area.left&&std::abs(clippedArea.top-area.top)<.001&&clippedArea.right==0&&clippedArea.bottom==target.bottom,
			"source area clips the working-area edge instead of preserving width outside the screen");
		for(const auto placement:{StartupPlacement::CenteredBottomBar,StartupPlacement::BottomLeftFloating,StartupPlacement::BottomRightFloating})
			for(int theme:{1,2})for(float scale:{.75f,1.f,1.5625f})
				for(const auto effect:{StartupPreviewEffect::Shimmer,StartupPreviewEffect::Aurora,StartupPreviewEffect::PixelAssembly})
				{
					OffscreenPreview renderer;
					const HRESULT initialized=renderer.InitializeEffect(placement,scale,effect,theme);
					if(!Expect(SUCCEEDED(initialized),"create production effect renderer on HWND-free WARP/D2D"))
					{std::cerr<<"Effect init HRESULT="<<std::hex<<initialized<<std::dec<<'\n';++failures;continue;}
					const auto base=renderer.RenderEffect(3.6,0,false,false);
					const auto half=renderer.RenderEffect(3.6,.5,false,true);
					const auto zero=renderer.RenderEffect(3.6,0,false,true);
					const auto full=renderer.RenderEffect(3.6,1,false,true);
					const auto fatal=renderer.RenderEffect(3.6,0,true,true);
					const auto count=static_cast<size_t>(renderer.width)*renderer.height;
					if(!Expect(base.size()==count&&half.size()==count&&zero.size()==count&&full.size()==count&&fatal.size()==count,"production background/progress/failure effect images render")){++failures;continue;}
					const auto r=renderer.EffectTarget();
					const auto at=[&](const auto& pixels,float x,float y)->const BgraPixel&{return pixels[static_cast<size_t>(std::clamp(static_cast<int>(y),0,static_cast<int>(renderer.height)-1))*renderer.width+std::clamp(static_cast<int>(x),0,static_cast<int>(renderer.width)-1)];};
					const bool floating=placement!=StartupPlacement::CenteredBottomBar;
					const auto appearance=Effects::ResolveAppearance(theme,0,100,floating);
					check(std::abs(at(base,r.left+Effects::Width(r)*.5f,r.top+Effects::Height(r)*.5f).alpha-appearance.backgroundOpacity*255)<2,
						"background group keeps intended alpha under soft glow/shimmer overlap");
					const float px=floating?r.left+Effects::Width(r)*.5f+14.5f*scale:r.left+Effects::Width(r)*.5f-48.f*scale;
					const float py=r.top+Effects::Height(r)*.5f;
					if(!Red(at(fatal,px,py))){const auto& f=at(fatal,px,py);std::cerr<<"[FailureTrack] effect="<<static_cast<int>(effect)<<" theme="<<theme<<" placement="<<static_cast<int>(placement)<<" scale="<<scale<<" bgra="<<int(f.blue)<<','<<int(f.green)<<','<<int(f.red)<<','<<int(f.alpha)<<'\n';}
					check(Red(at(fatal,px,py)),"theme-appropriate failure track stays visible at zero progress");
					check(at(half,px,py).alpha>at(base,px,py).alpha,"foreground progress remains above the background effect");
					const std::wstring prefix=L"effect-"+std::to_wstring(static_cast<int>(effect))+L"-theme"+std::to_wstring(theme)+L"-pos"+std::to_wstring(static_cast<int>(placement))+L"-scale"+std::to_wstring(scale);
					SavePreviewBitmap(prefix+L"-ready.bmp",renderer.width,renderer.height,half);
					SavePreviewBitmap(prefix+L"-zero.bmp",renderer.width,renderer.height,zero);
					SavePreviewBitmap(prefix+L"-full.bmp",renderer.width,renderer.height,full);
					SavePreviewBitmap(prefix+L"-failure.bmp",renderer.width,renderer.height,fatal);
					if(effect==StartupPreviewEffect::PixelAssembly)
					{
						OffscreenPreview steady;
						const HRESULT steadyInit=steady.InitializeEffect(placement,scale,StartupPreviewEffect::Shimmer,theme);
						const auto steadyPixels=SUCCEEDED(steadyInit)?steady.RenderEffect(3.6-Effects::AssemblySeconds,0,false,false):std::vector<BgraPixel>{};
						bool sameBase=steadyPixels.size()==static_cast<size_t>(Effects::Width(r))*static_cast<size_t>(Effects::Height(r));
						std::vector<BgraPixel> actualCrop;size_t different=0;int maxDelta=0,maxAlphaDelta=0;
						if(sameBase){actualCrop.resize(steadyPixels.size());for(UINT y=0;y<steady.height;++y)for(UINT x=0;x<steady.width;++x){
							const size_t index=static_cast<size_t>(y)*steady.width+x;
							const auto& expected=steadyPixels[index];const auto& actual=base[static_cast<size_t>(y+static_cast<UINT>(r.top))*renderer.width+x+static_cast<UINT>(r.left)];actualCrop[index]=actual;
							if(std::memcmp(&expected,&actual,sizeof(BgraPixel))!=0)++different;
							maxAlphaDelta=(std::max)(maxAlphaDelta,std::abs(static_cast<int>(expected.alpha)-actual.alpha));
							maxDelta=(std::max)({maxDelta,std::abs(static_cast<int>(expected.blue)-actual.blue),std::abs(static_cast<int>(expected.green)-actual.green),std::abs(static_cast<int>(expected.red)-actual.red),maxAlphaDelta});
						}
							// 相同本地相位下，扩大画布的整数平移仍可能让D2D渐变RGB量化差1；实测暗色主栏仅1~3像素。
							// alpha/轮廓必须逐像素完全相同，颜色最多允许一个8位量化单位，不容忍位移或材质跳变。
							sameBase=maxAlphaDelta==0&&maxDelta<=1;
						}
						if(different>0||!sameBase){std::cerr<<"[StartupEffectEquivalence] theme="<<theme<<" placement="<<static_cast<int>(placement)<<" scale="<<scale<<" mismatched="<<different<<" maxRGBA="<<maxDelta<<" maxAlpha="<<maxAlphaDelta<<'\n';
							SavePreviewBitmap(prefix+L"-assembled-expected.bmp",steady.width,steady.height,steadyPixels);SavePreviewBitmap(prefix+L"-assembled-actual.bmp",steady.width,steady.height,actualCrop);}
						check(sameBase,"assembled pixel surface matches shimmer with exact alpha and at most one RGB quantization unit at identical local phase");
						const auto& particles=renderer.EffectParticles();size_t moving=0;double centerArrival=0,edgeArrival=0;size_t centerCount=0,edgeCount=0;
						const float centerX=r.left+Effects::Width(r)*.5f;
						for(const auto& p:particles)
						{
							const auto end=Effects::Position(p,2.4),later=Effects::Position(p,20.);
							check(end.x==p.target.x&&end.y==p.target.y&&later.x==end.x&&later.y==end.y,"particles reach the exact bitmap and stay stationary");
							check(p.begin>=0.f&&p.begin<p.arrive&&p.arrive<=static_cast<float>(Effects::LatestArrivalSeconds),"particle flight window is ordered and ends before the last glint");
							const auto middle=Effects::Position(p,(p.begin+p.arrive)*.5);
							if(std::hypot(middle.x-p.target.x,middle.y-p.target.y)>4.f*scale)++moving;
							const float spread=std::abs(p.target.x-centerX)/(Effects::Width(r)*.5f);
							if(spread<.2f){centerArrival+=p.arrive;++centerCount;}else if(spread>.8f){edgeArrival+=p.arrive;++edgeCount;}
						}
						check(!particles.empty()&&particles.size()<=Effects::MaximumParticles&&moving>=particles.size()*.95,
							"converging particles visibly travel and respect the halved particle budget");
						check(centerCount&&edgeCount&&centerArrival/centerCount+.4<edgeArrival/edgeCount,"particles settle in waves from the center outward");
						(void)renderer.RenderEffect(1.2,0,false,false);const RECT flying=renderer.EffectContentBounds();
						(void)renderer.RenderEffect(3.0,0,false,false);const RECT settled=renderer.EffectContentBounds();
						check(settled.left<=static_cast<LONG>(r.left)&&settled.right>=static_cast<LONG>(r.right)&&settled.top<=static_cast<LONG>(r.top)
							&&settled.bottom>=static_cast<LONG>(r.bottom)&&(settled.right-settled.left)*(settled.bottom-settled.top)<static_cast<LONG>(renderer.width*renderer.height)
							&&(flying.right-flying.left)*(flying.bottom-flying.top)>(settled.right-settled.left)*(settled.bottom-settled.top),
							"dirty content bounds cover flying particles and shrink back to the bar once assembled");
						OffscreenPreview fallback;
						if(SUCCEEDED(fallback.InitializeEffect(placement,scale,effect,theme,0,100,false))&&renderer.EffectUsesSpriteBatch()){
							for(double t:{.7,1.3,2.1}){
								const auto batched=renderer.RenderEffect(t,0,false,false),looped=fallback.RenderEffect(t,0,false,false);
								int spriteDelta=0;std::uint64_t spriteSum=0;
								if(batched.size()==looped.size())for(size_t k=0;k<batched.size();++k){
									const auto& a=batched[k];const auto& b=looped[k];
									const int d=(std::max)({std::abs(a.blue-b.blue),std::abs(a.green-b.green),std::abs(a.red-b.red),std::abs(a.alpha-b.alpha)});
									spriteSum+=std::abs(a.blue-b.blue)+std::abs(a.green-b.green)+std::abs(a.red-b.red)+std::abs(a.alpha-b.alpha);spriteDelta=(std::max)(spriteDelta,d);
								}
								const double spriteMean=batched.empty()?0.:static_cast<double>(spriteSum)/batched.size();
								const bool spriteMatches=batched.size()==looped.size()&&spriteDelta<=SpriteFallbackMaximumDelta&&spriteMean<=SpriteFallbackMeanDelta;
								if(!spriteMatches){std::cerr<<"[SpriteFallback] t="<<t<<" scale="<<scale<<" maxDelta="<<spriteDelta<<" mean="<<spriteMean<<'\n';
									SavePreviewBitmap(prefix+L"-sprite-t"+std::to_wstring(t)+L".bmp",renderer.width,renderer.height,batched);
									SavePreviewBitmap(prefix+L"-fallback-t"+std::to_wstring(t)+L".bmp",fallback.width,fallback.height,looped);}
								check(spriteMatches,"sprite batch and aliased DrawBitmap fallback produce matching particle frames");
							}
						}
						for(double t:{.2,.8,1.6,2.4,3.0}){const auto frame=renderer.RenderEffect(t,.3,false,t>=3.);check(frame.size()==count,"production particle entry/return/trail frame renders");SavePreviewBitmap(prefix+L"-t"+std::to_wstring(t)+L".bmp",renderer.width,renderer.height,frame);}
					}
				}
		// 背景与内容分别改变，像素比较必须经过同一生产整组绘制。
		OffscreenPreview low,high;
		check(SUCCEEDED(low.InitializeEffect(StartupPlacement::CenteredBottomBar,1.f,StartupPreviewEffect::Aurora,2,30,30))
			&&SUCCEEDED(high.InitializeEffect(StartupPlacement::CenteredBottomBar,1.f,StartupPreviewEffect::Aurora,2,30,100)),"create independent-opacity effect fixtures");
		const auto lowPixels=low.RenderEffect(3.6,.5,false,true),highPixels=high.RenderEffect(3.6,.5,false,true);
		if(!lowPixels.empty()&&lowPixels.size()==highPixels.size()){
			const size_t backgroundIndex=10*low.width+low.width/2,progressIndex=(low.height/2)*low.width+low.width/2-48;
			check(std::memcmp(&lowPixels[backgroundIndex],&highPixels[backgroundIndex],sizeof(BgraPixel))==0
				&&lowPixels[progressIndex].alpha<highPixels[progressIndex].alpha,"content opacity changes foreground without dimming the soft glow background");
			SavePreviewBitmap(L"effect-opacity-bg30-content30.bmp",low.width,low.height,lowPixels);SavePreviewBitmap(L"effect-opacity-bg30-content100.bmp",high.width,high.height,highPixels);
		}else check(false,"opacity fixture pixels available");
		OffscreenPreview opaqueBackground;
		check(SUCCEEDED(opaqueBackground.InitializeEffect(StartupPlacement::CenteredBottomBar,1.f,StartupPreviewEffect::Aurora,2,100,100)),"create full-background opacity fixture");
		const auto opaqueProgress=opaqueBackground.RenderEffect(3.6,.5,false,true);
		if(!highPixels.empty()&&opaqueProgress.size()==highPixels.size()){
			const size_t progressIndex=(high.height/2)*high.width+high.width/2-48;
			check(std::memcmp(&highPixels[progressIndex],&opaqueProgress[progressIndex],sizeof(BgraPixel))==0,
				"opaque foreground progress is unchanged when only background opacity changes from30 to100");
		}else check(false,"foreground isolation pixels available");
		std::vector<BgraPixel> cadenceReference;
		for(int hz:{30,60,120}){
			OffscreenPreview cadence;
			const HRESULT init=cadence.InitializeEffect(StartupPlacement::CenteredBottomBar,1.f,StartupPreviewEffect::PixelAssembly,1);
			if(!Expect(SUCCEEDED(init),"create cadence comparison WARP renderer")){++failures;continue;}
			std::vector<BgraPixel> pixels;
			for(int frame=0;frame<=hz*8/5;++frame){pixels=cadence.RenderEffect(static_cast<double>(frame)/hz,0,false,false);if(pixels.empty())break;}
			check(!pixels.empty(),"all fixed-step cadence frames render");
			if(hz==30)cadenceReference=pixels;
			else check(!pixels.empty()&&pixels.size()==cadenceReference.size()
				&&std::memcmp(pixels.data(),cadenceReference.data(),pixels.size()*sizeof(BgraPixel))==0,
				"particle frame is byte-identical at1.6s under30/60/120Hz preparation cadence");
		}
		return failures;
	}

	int RunStartupPreviewPixelTests()
	{
		int failures=0;
		using Inkeys::UI::StartupPlacement;
		if (!Expect(Inkeys::UI::StartupPreview::Drawing::DrawProgress(nullptr, nullptr, nullptr, nullptr,
			StartupPlacement::CenteredBottomBar, 24.f, 81.f, 1.f, .5, 1.f, false) == S_FALSE,
			"too narrow linear progress does not claim a visible draw")) ++failures;
		for (const auto placement : { StartupPlacement::CenteredBottomBar,
			StartupPlacement::BottomLeftFloating, StartupPlacement::BottomRightFloating })
			for (const float scale : { .75f, 1.f, 1.5625f, 2.f, 2.25f })
			{
				OffscreenPreview renderer;
				const bool floating = placement != StartupPlacement::CenteredBottomBar;
				const HRESULT init=renderer.Initialize(placement,scale);
				if (!Expect(SUCCEEDED(init),"create HWND-free production D2D/WARP pixel fixture"))
				{ std::cerr << "D2D HRESULT=" << std::hex << init << std::dec << '\n'; ++failures; continue; }
				const auto base=renderer.Render(0.,false,false,-1.);
				const auto start=renderer.Render(0.,false,false,0.);
				const auto middle=renderer.Render(0.,false,false,.5);
				const auto finish=renderer.Render(0.,false,false,1.);
				const auto zero=renderer.Render(0.,false,true,-1.);
				const auto half=renderer.Render(.5,false,true,-1.);
				const auto full=renderer.Render(1.,false,true,-1.);
				const auto fatal=renderer.Render(.5,true,true,-1.);
				const auto fatalZero=renderer.Render(0.,true,true,-1.);
				const size_t count=static_cast<size_t>(renderer.width)*renderer.height;
				if (!Expect(base.size()==count&&start.size()==count&&middle.size()==count&&finish.size()==count
					&&zero.size()==count&&half.size()==count&&full.size()==count&&fatal.size()==count&&fatalZero.size()==count,
					"all production preview bitmap draws and CPU readbacks succeed")) { ++failures; continue; }
				auto at=[&](const auto& pixels,double x,double y)->const BgraPixel&
				{ return pixels[static_cast<size_t>(std::lround(y))*renderer.width+static_cast<size_t>(std::lround(x))]; };
				const double cx=renderer.width*.5, cy=renderer.height*.5, radius=14.5*scale;
				if (!Expect(base.front().alpha==0 && base.back().alpha==0
					&& std::abs(static_cast<int>(at(base,cx,cy).alpha)-(floating?153:204))<=1,
					"actual shape has transparent corners and correct center per-pixel fill alpha")) ++failures;
				bool endpointsEqual=true,changed=false,maskClipped=true;
				for (size_t i=0;i<count;++i)
				{
					endpointsEqual &= std::memcmp(&base[i],&start[i],sizeof(BgraPixel))==0
						&& std::memcmp(&base[i],&finish[i],sizeof(BgraPixel))==0;
					changed |= middle[i].red>base[i].red+5;
					if (base[i].alpha==0) maskClipped &= middle[i].alpha==0;
				}
				if (!Expect(endpointsEqual&&changed&&maskClipped,"real shimmer crosses the opacity mask and is absent at both offscreen endpoints")) ++failures;
				// 反光等强度线与梯度垂直；实际像素沿(x+d,y-d)保持一致，覆盖所有DPI与外形。
				const int sampleX=static_cast<int>(cx), sampleY=static_cast<int>(cy);
				const int diagonalOffset=static_cast<int>(std::lround(10.*scale));
				const int centerLight=at(middle,sampleX,sampleY).red-at(base,sampleX,sampleY).red;
				const int upperLight=at(middle,sampleX+diagonalOffset,sampleY-diagonalOffset).red
					-at(base,sampleX+diagonalOffset,sampleY-diagonalOffset).red;
				const int lowerLight=at(middle,sampleX-diagonalOffset,sampleY+diagonalOffset).red
					-at(base,sampleX-diagonalOffset,sampleY+diagonalOffset).red;
				if (!Expect(centerLight>5 && std::abs(centerLight-upperLight)<=1 && std::abs(centerLight-lowerLight)<=1,
					"real shimmer equal-intensity pixels form a 45 degree band independent of shape and scale")) ++failures;
				if (floating && !Expect(at(base,5.*scale,5.*scale).alpha==0 && at(base,12.*scale,12.*scale).alpha>0,
					"production n3 superellipse is rounder than a rounded square and fuller than an ellipse")) ++failures;
				int minX=static_cast<int>(renderer.width), maxX=-1;
				int minY=static_cast<int>(renderer.height), maxY=-1;
				for (UINT y=0;y<renderer.height;++y) for (UINT x=0;x<renderer.width;++x)
					if (Blue(full[static_cast<size_t>(y)*renderer.width+x]))
					{ minX=(std::min)(minX,static_cast<int>(x)); maxX=(std::max)(maxX,static_cast<int>(x));
					  minY=(std::min)(minY,static_cast<int>(y)); maxY=(std::max)(maxY,static_cast<int>(y)); }
				if (floating)
				{
					if (!Expect(!Blue(at(zero,cx,cy-radius)) && Blue(at(half,cx+radius,cy))
						&& !Blue(at(half,cx-radius,cy)) && Blue(at(full,cx-radius,cy))
						&& at(full,cx,cy).alpha==at(base,cx,cy).alpha,
						"real corner ring pixels reflect 0/50/100 percent clockwise progress with hollow center")) ++failures;
					if (!Expect(Red(at(fatal,cx+radius,cy)) && !Blue(at(fatal,cx+radius,cy))
						&& Red(at(fatalZero,cx+radius,cy)),"first fatal ring pixels are red, including zero progress without a fake arc")) ++failures;
					if (scale>=1.5f && !Expect(Blue(at(half,cx-scale,cy-radius)) && Blue(at(half,cx-scale,cy+radius)),
						"production open ring arc uses round caps extending beyond top/bottom endpoints")) ++failures;
					int radialStrokePixels=0;
					for (int x=static_cast<int>(cx+8.*scale);x<=static_cast<int>(cx+20.*scale);++x)
						if (Blue(at(full,x,cy))) ++radialStrokePixels;
					if (!Expect(std::abs((maxX-minX+1)-32.*scale)<=2.
						&& std::abs((maxY-minY+1)-32.*scale)<=2.
						&& std::abs(radialStrokePixels-3.*scale)<=2.,
						"production ring preserves 32 DIP diameter and 3 DIP stroke at fractional DPI times zoom")) ++failures;
				}
				else
				{
					if (!Expect(!Blue(at(zero,cx-48.*scale,cy)) && Blue(at(half,cx-48.*scale,cy))
						&& !Blue(at(half,cx+48.*scale,cy)) && Blue(at(full,cx+48.*scale,cy)),
						"real centered linear pixels reflect empty/half/full horizontal progress")) ++failures;
					if (!Expect(Red(at(fatal,cx-48.*scale,cy)) && !Blue(at(fatal,cx-48.*scale,cy))
						&& Red(at(fatalZero,cx,static_cast<int>(cy))),"first fatal linear pixels are red, including the zero progress track")) ++failures;
					const double fillHeight=(std::max)(3.,3.*scale), trackHeight=(std::max)(1.,static_cast<double>(scale));
					if (!Expect(std::abs((maxX-minX+1)-192.*scale)<=2.
						&& std::abs((maxY-minY+1)-fillHeight)<=2.
						&& minX>=cx-96.*scale-1. && maxX<=cx+96.*scale+1.,
						"center restores the original centered 192 DIP width and minimum 3 pixel/DIP fill")) ++failures;
					// 0%仅有白色细轨道；顶部/底部不能画成环，也不能带蓝色填充。
					const int trackY=static_cast<int>(cy);
					const auto& trackPixel=at(zero,cx-48.*scale,trackY);
					if (!Expect(trackPixel.red==trackPixel.green && trackPixel.green==trackPixel.blue
						&& trackPixel.red>at(base,cx-48.*scale,trackY).red
						&& at(zero,cx-48.*scale,cy+trackHeight+1.).alpha==at(base,cx-48.*scale,cy+trackHeight+1.).alpha,
						"normal linear track is original white and one pixel/DIP tall")) ++failures;
					if (scale>=1.5f)
					{
						// 同Y比较外角与内部覆盖，避免取整后采样点落入合法圆头；矩形端点会同强度。
						const int capLeft=static_cast<int>(std::floor(cx-96.*scale));
						const int capTop=static_cast<int>(std::floor(cy-fillHeight*.5));
						const auto& outer=at(full,capLeft,capTop);
						const auto& inner=at(full,std::floor(cx-96.*scale+6.*scale),capTop);
						const int outerChroma=static_cast<int>(outer.blue)-outer.red;
						const int innerChroma=static_cast<int>(inner.blue)-inner.red;
						if (!Expect(Blue(at(full,cx-96.*scale+.5*scale,cy))
							&& innerChroma-outerChroma>30 && outerChroma*2<innerChroma,
							"linear progress preserves rounded fill ends")) ++failures;
					}
				}
				const std::wstring prefix=(placement==StartupPlacement::BottomLeftFloating ? L"left-"
					: placement==StartupPlacement::BottomRightFloating ? L"right-" : L"center-")+std::to_wstring(scale)+L"x-";
				SavePreviewBitmap(prefix+L"0.bmp",renderer.width,renderer.height,zero);
				SavePreviewBitmap(prefix+L"50.bmp",renderer.width,renderer.height,half);
				SavePreviewBitmap(prefix+L"100.bmp",renderer.width,renderer.height,full);
				SavePreviewBitmap(prefix+L"fatal.bmp",renderer.width,renderer.height,fatal);
				SavePreviewBitmap(prefix+L"shimmer.bmp",renderer.width,renderer.height,middle);
			}
		return failures;
	}

}

int RunStartupPreviewStateTests()
{
	using namespace Inkeys::UI::StartupPreview;
	using namespace std::chrono_literals;
	int failures = RunStartupPlacementConfigTests() + RunStartupPlacementGeometryTests() + RunStartupPreviewPixelTests() + RunStartupEffectTests();

	if (!Expect(DefaultMainBarBodyWidthDip == 380.0
		&& DefaultCachedStartupBarWidthDip == 470.0
		&& DefaultStartupBarHeightDip == 80.0
		&& DefaultStartupBarCornerRadiusDip == 8.0
		&& StartupPreviewSurfaceColorChannel == 24.0 / 255.0
		&& StartupPreviewSurfaceFrameColorChannel == 1.0
		&& StartupPreviewFillAlpha == 0.8
		&& StartupPreviewFrameAlpha == 0.18
		&& StartupPreviewEnabledByDefault
		&& CalculateStartupBarTotalWidthDip(80.0, 380.0) == 470.0,
		"preview uses MainBar dark surface visuals and derives 380/470 DIP geometry"))
		++failures;
	if (!Expect(CalculateStartupBarTotalWidthDip(80.0, 380.0, false) == 80.0,
		"collapsed nominal geometry contains only the main button")) ++failures;
	for (const double invalid : { 0.0, -1.0, 79.99, 4096.01,
		(std::numeric_limits<double>::quiet_NaN)(),
		(std::numeric_limits<double>::infinity)(),
		-(std::numeric_limits<double>::infinity)() })
		if (!Expect(ResolveCachedStartupBarWidthDip(invalid)
			== DefaultCachedStartupBarWidthDip,
			"invalid cached width falls back to 470 DIP")) ++failures;
	if (!Expect(ResolveCachedStartupBarWidthDip(80.0) == 80.0
		&& ResolveCachedStartupBarWidthDip(612.5) == 612.5,
		"valid cached width remains an unscaled DIP value")) ++failures;
	const auto legacyConfig = ReadLegacyStartupPreviewFixture();
	if (!Expect(legacyConfig.read && !legacyConfig.enabled
		&& legacyConfig.widthDip == DefaultCachedStartupBarWidthDip
		&& legacyConfig.zoom == 1.0 && legacyConfig.taskbarBase
			== (Inkeys::ConfigDetail::GetDefaultLargeScreenDevice() ? 1 : 0),
		"legacy main.json keeps cached width and uses frozen device default for taskbar base"))
		++failures;
	if (!Expect(!ShouldWriteCachedStartupBarWidthDip(470.0, 470.0)
		&& !ShouldWriteCachedStartupBarWidthDip(470.0, 470.005)
		&& ShouldWriteCachedStartupBarWidthDip(470.0, 545.0)
		&& !ShouldWriteCachedStartupBarWidthDip(470.0,
			(std::numeric_limits<double>::quiet_NaN)()),
		"cached width write suppresses noise and rejects invalid candidates")) ++failures;

	for (const UINT dpi : { 0U, 96U, 144U, 192U })
		for (const int thickness : { 0, 24, 96 })
			for (const double user : { 0.5, 1.0, 1.5, 2.0 })
				for (const auto mode : { Inkeys::Display::UiScaleBaseMode::Dpi,
					Inkeys::Display::UiScaleBaseMode::TaskbarBasic, Inkeys::Display::UiScaleBaseMode::TaskbarActual })
				{
					const double dpiScale = (dpi ? dpi : 96) / 96.0;
					const double base = Inkeys::Display::ResolveUiScaleBase(dpi, thickness, 0, 0, mode);
					const double height = mode == Inkeys::Display::UiScaleBaseMode::TaskbarBasic
						? 48 * dpiScale : mode == Inkeys::Display::UiScaleBaseMode::TaskbarActual && thickness > 0
						? thickness : 80 * dpiScale;
					const auto pixels = ResolveStartupPreviewPixelSize(470.0, dpi, user, base);
					if (!Expect(pixels.height == std::lround(height * user)
						&& std::abs(ResolveStartupPreviewScale(dpi, user, base) - base * user) < 1e-9,
						"startup mode matches Bar height across DPI, actual thickness, missing Shell and user multiplier")) ++failures;
				}
	const auto pixels96 = ResolveStartupPreviewPixelSize(470.0, 96.0, 1.0);
	const auto pixels120 = ResolveStartupPreviewPixelSize(470.0, 120.0, 1.25);
	const auto pixels144 = ResolveStartupPreviewPixelSize(470.0, 144.0, 1.5);
	if (!Expect(pixels96.width == 470 && pixels96.height == 80
		&& pixels120.width == 734 && pixels120.height == 125
		&& pixels144.width == 1058 && pixels144.height == 180,
		"DPI times Bar zoom uses consistent rounding")) ++failures;
	if (!Expect(ResolveStartupPreviewPixelSize(
		(std::numeric_limits<double>::quiet_NaN)(), 96.0, 1.0).width == 470,
		"invalid cached width still produces default preview pixels")) ++failures;

	const RECT negativeMonitor{ -1920, 0, 0, 1080 };
	const RECT negativeWorkArea{ -1920, 0, 0, 1040 };
	const RECT centered = ResolveStartupPreviewBounds(
		negativeMonitor, negativeWorkArea, 470.0, 80.0, 96, 1.0);
	if (!Expect(centered.left == -1196 && centered.right == -725
		&& centered.top == 959 && centered.bottom == 1040,
		"complete placeholder envelope stays centered in negative work area")) ++failures;
	const RECT sideTaskbarWorkArea{ -1870, 0, 0, 1080 };
	const RECT sideTaskbar = ResolveStartupPreviewBounds(
		negativeMonitor, sideTaskbarWorkArea, 470.0, 80.0, 96, 1.0);
	if (!Expect(sideTaskbar.left == -1196 && sideTaskbar.right == -725
		&& sideTaskbar.top == 999 && sideTaskbar.bottom == 1080,
		"side taskbar keeps the envelope centered on the monitor")) ++failures;

	for (const double widthDip : { 81., 471. })
		for (const double scale : { .75, 1., 1.5625, 2., 2.25 })
		{
			const double width=std::lround(widthDip*scale), height=std::lround(81.*scale);
			const auto gradient=ResolveStartupPreviewShimmerGradient(width,height);
			const double dx=gradient.endX-gradient.startX, dy=gradient.endY-gradient.startY;
			const double priorLength=std::hypot((std::max)(160.,width*.5),height*2.);
			if (!Expect(dx>0. && std::abs(dx-dy)<1e-9
				&& std::abs(std::hypot(dx,dy)-priorLength)<1e-9,
				"all preview sizes use a 45 degree shimmer while preserving its original support length")) ++failures;
		}
	for (const auto pixels : { pixels96, pixels120, pixels144 })
	{
		const GeometryRect bounds{ 0.0, 0.0,
			static_cast<double>(pixels.width), static_cast<double>(pixels.height) };
		const auto gradient = ResolveStartupPreviewShimmerGradient(
			static_cast<double>(pixels.width), static_cast<double>(pixels.height));
		const double supportLength = std::hypot(gradient.endX - gradient.startX,
			gradient.endY - gradient.startY);
		const auto travel = ResolveShimmerTravel(bounds, gradient,
			(std::max)(8.0, supportLength * 0.10));
		const auto startTranslation = ResolveShimmerTranslation(travel, 0.0);
		const auto middleTranslation = ResolveShimmerTranslation(travel, 0.5);
		const auto endTranslation = ResolveShimmerTranslation(travel, 1.0);
		if (!Expect(travel.valid
			&& gradient.endX > gradient.startX
			&& gradient.endY > gradient.startY
			&& startTranslation.x < endTranslation.x
			&& startTranslation.y < endTranslation.y
			&& IsShimmerSupportOutsideMask(bounds, gradient,
				startTranslation.x, startTranslation.y)
			&& !IsShimmerSupportOutsideMask(bounds, gradient,
				middleTranslation.x, middleTranslation.y)
			&& IsShimmerSupportOutsideMask(bounds, gradient,
				endTranslation.x, endTranslation.y),
			"diagonal shimmer soft-tail support is offscreen at endpoints and crosses the mask")) ++failures;
	}
	const auto shimmerEpoch = std::chrono::steady_clock::time_point(100h);
	const auto shimmerPeriod =
		std::chrono::duration<double>(StartupPreviewShimmerCycleSeconds);
	const auto quarterSweepCycle = ResolveShimmerCycleRatio(
		shimmerEpoch + 700ms, shimmerEpoch, shimmerPeriod);
	const auto middleSweepCycle = ResolveShimmerCycleRatio(
		shimmerEpoch + 1400ms, shimmerEpoch, shimmerPeriod);
	const auto threeQuarterSweepCycle = ResolveShimmerCycleRatio(
		shimmerEpoch + 2100ms, shimmerEpoch, shimmerPeriod);
	const auto sweepEndCycle = ResolveShimmerCycleRatio(
		shimmerEpoch + 2800ms, shimmerEpoch, shimmerPeriod);
	const auto holdCycle = ResolveShimmerCycleRatio(
		shimmerEpoch + 3400ms, shimmerEpoch, shimmerPeriod);
	const auto beforeWrapCycle = ResolveShimmerCycleRatio(
		shimmerEpoch + 3999ms, shimmerEpoch, shimmerPeriod);
	const auto wrappedCycle = ResolveShimmerCycleRatio(
		shimmerEpoch + 4000ms, shimmerEpoch, shimmerPeriod);
	const GeometryRect cycleBounds{ 0.0, 0.0, 470.0, 80.0 };
	const auto cycleGradient = ResolveStartupPreviewShimmerGradient(470.0, 80.0);
	const double cycleSupportLength = std::hypot(
		cycleGradient.endX - cycleGradient.startX,
		cycleGradient.endY - cycleGradient.startY);
	const auto cycleTravel = ResolveShimmerTravel(cycleBounds, cycleGradient,
		(std::max)(8.0, cycleSupportLength * 0.10));
	const auto sweepEndTranslation = ResolveShimmerTranslation(
		cycleTravel, EaseShimmerPhase(sweepEndCycle));
	const auto holdTranslation = ResolveShimmerTranslation(
		cycleTravel, EaseShimmerPhase(holdCycle));
	const auto beforeWrapTranslation = ResolveShimmerTranslation(
		cycleTravel, EaseShimmerPhase(beforeWrapCycle));
	const auto wrappedTranslation = ResolveShimmerTranslation(
		cycleTravel, EaseShimmerPhase(wrappedCycle));
	if (!Expect(std::abs(sweepEndCycle - 0.70) < 0.000001
		&& std::abs(holdCycle - 0.85) < 0.000001
		&& beforeWrapCycle > 0.99 && wrappedCycle == 0.0
		&& EaseShimmerPhase(quarterSweepCycle) < 0.25
		&& std::abs(EaseShimmerPhase(middleSweepCycle) - 0.5) < 0.000001
		&& EaseShimmerPhase(threeQuarterSweepCycle) > 0.75
		&& sweepEndTranslation.x == holdTranslation.x
		&& sweepEndTranslation.y == holdTranslation.y
		&& IsShimmerSupportOutsideMask(cycleBounds, cycleGradient,
			sweepEndTranslation.x, sweepEndTranslation.y)
		&& IsShimmerSupportOutsideMask(cycleBounds, cycleGradient,
			beforeWrapTranslation.x, beforeWrapTranslation.y)
		&& IsShimmerSupportOutsideMask(cycleBounds, cycleGradient,
			wrappedTranslation.x, wrappedTranslation.y),
		"shimmer eases its sweep and holds fully offscreen between passes"))
		++failures;

	std::uint8_t previous = 0;
	for (int elapsed = 0; elapsed <= 300; ++elapsed)
	{
		const auto alpha = ResolveFadeInAlpha(
			std::chrono::milliseconds(elapsed), StartupPreviewFadeInDuration);
		if (!Expect(alpha >= previous, "preview fade-in alpha is monotonic")) ++failures;
		previous = alpha;
	}
	if (!Expect(ResolveFadeInAlpha(0ms, StartupPreviewFadeInDuration) == 0
		&& ResolveFadeInAlpha(300ms, StartupPreviewFadeInDuration) == 255
		&& ResolveFadeOutAlpha(0ms, StartupPreviewQuickHandoffDuration) == 255
		&& ResolveFadeOutAlpha(300ms, StartupPreviewQuickHandoffDuration) == 0
		&& ResolveFadeOutAlphaFrom(96, 0ms,
			StartupPreviewProgressHandoffDuration) == 96
		&& ResolveFadeOutAlphaFrom(96, 1000ms,
			StartupPreviewProgressHandoffDuration) == 0
		&& ResolveFadeOutAlpha(159ms,
			StartupPreviewExitFadeOutDuration) > 0
		&& ResolveFadeOutAlpha(160ms,
			StartupPreviewExitFadeOutDuration) == 0,
		"fade helpers have exact endpoints")) ++failures;

	OrderedHandoffReducer handoff;
	if (!Expect(handoff.PreviewFirstAlpha0Committed().state
		== HandoffState::PreviewVisible,
		"alpha-zero preview commit starts visible phase")) ++failures;
	if (!Expect(!handoff.BarAlpha0Committed(false).requestPreviewFadeOut
		&& handoff.State() == HandoffState::WaitingForBar,
		"Bar alpha zero waits for real startup completion")) ++failures;
	const auto fadePreview = handoff.BarAlpha0Committed(true);
	if (!Expect(fadePreview.state == HandoffState::PreviewFadeOut
		&& fadePreview.requestPreviewFadeOut && !fadePreview.requestBarFadeIn,
		"successful handoff fades Preview first")) ++failures;
	if (!Expect(!handoff.BarAlpha255Committed().destroyPreview
		&& handoff.State() == HandoffState::PreviewFadeOut,
		"Bar cannot skip Preview fade-out")) ++failures;
	const auto fadeBar = handoff.PreviewFadeOutCommitted();
	if (!Expect(fadeBar.state == HandoffState::WaitingForBarOpaque
		&& fadeBar.requestBarFadeIn && !fadeBar.destroyPreview,
		"Preview zero commit starts Bar fade-in")) ++failures;
	if (!Expect(handoff.BarAlpha255Committed().destroyPreview
		&& handoff.State() == HandoffState::Stopped,
		"Preview destruction waits for Bar alpha 255 commit")) ++failures;
	OrderedHandoffReducer recovery;
	const auto bypass = recovery.Bypass();
	if (!Expect(bypass.state == HandoffState::Bypassed
		&& bypass.requestBarFadeIn && !bypass.destroyPreview,
		"failure recovery first requests opaque Bar")) ++failures;
	if (!Expect(recovery.BarAlpha255Committed().destroyPreview
		&& recovery.State() == HandoffState::Stopped,
		"failure cleanup waits for Bar alpha 255")) ++failures;

	HandoffTimingReducer quickTiming;
	quickTiming.PreviewFrameCommitted(true, 0);
	quickTiming.PreviewFrameCommitted(false, 255);
	if (!Expect(quickTiming.FreezeDuration()
			== StartupPreviewQuickHandoffDuration
		&& !quickTiming.ProgressEverCommittedVisible(),
		"uncommitted or invisible progress keeps the 300 ms handoff")) ++failures;
	quickTiming.PreviewFrameCommitted(true, 255);
	if (!Expect(quickTiming.FreezeDuration()
			== StartupPreviewQuickHandoffDuration
		&& !quickTiming.ProgressEverCommittedVisible(),
		"handoff timing stays frozen after handoff starts")) ++failures;
	HandoffTimingReducer progressTiming;
	progressTiming.PreviewFrameCommitted(true, 1);
	if (!Expect(progressTiming.ProgressEverCommittedVisible()
			&& progressTiming.FreezeDuration()
				== StartupPreviewProgressHandoffDuration,
		"a committed visible progress frame selects the 1000 ms handoff"))
		++failures;

	if (!Expect(IsBarStartupReady(BarStartupState::FirstFrameCommitted)
		&& !IsBarStartupReady(BarStartupState::RenderClientRegistered)
		&& IsBarStartupFailure(BarStartupState::WindowMissing)
		&& IsBarStartupFailure(BarStartupState::StoppedBeforeReady),
		"Bar readiness distinguishes committed and terminal states")) ++failures;

	const auto start = std::chrono::steady_clock::time_point(10s);
	ProgressVisualReducer zeroEpochProgress;
	(void)zeroEpochProgress.Update({}, 0.4, false, false);
	auto zeroEpochVisual = zeroEpochProgress.Update(
		std::chrono::steady_clock::time_point(150ms), 0.4, false, false);
	if (!Expect(std::abs(zeroEpochVisual.displayedRatio - 0.2) < 0.000001,
		"progress animation supports a zero steady-clock epoch")) ++failures;
	ProgressVisualReducer progress;
	progress.MarkPreviewShown(start);
	auto visual = progress.Update(start, 0.4, false, false);
	if (!Expect(visual.displayedRatio == 0.0,
		"progress starts its own time-based animation without jumping")) ++failures;
	visual = progress.Update(start + 150ms, 0.4, false, false);
	if (!Expect(std::abs(visual.displayedRatio - 0.2) < 0.000001,
		"progress ratio uses smoothstep at the animation midpoint")) ++failures;
	ProgressVisualReducer frameIndependent;
	frameIndependent.MarkPreviewShown(start);
	(void)frameIndependent.Update(start, 0.4, false, false);
	const auto quarterVisual = frameIndependent.Update(
		start + 75ms, 0.4, false, false);
	if (!Expect(quarterVisual.displayedRatio < 0.1,
		"progress smoothstep starts slower than linear interpolation")) ++failures;
	const auto independentVisual = frameIndependent.Update(
		start + 150ms, 0.4, false, false);
	if (!Expect(std::abs(independentVisual.displayedRatio
			- visual.displayedRatio) < 0.000001,
		"progress interpolation is frame-rate independent")) ++failures;
	visual = progress.Update(start + 150ms, 0.8, false, false);
	if (!Expect(std::abs(visual.displayedRatio - 0.2) < 0.000001,
		"progress retarget keeps the current displayed ratio continuous")) ++failures;
	visual = progress.Update(start + 300ms, 0.8, false, false);
	if (!Expect(std::abs(visual.displayedRatio - 0.5) < 0.000001,
		"retargeted progress continues on a fresh smooth timeline")) ++failures;
	visual = progress.Update(start + 2999ms, 0.8, false, false);
	if (!Expect(visual.state == ProgressVisualState::Hidden
		&& visual.opacity == 0.0 && visual.displayedRatio <= 0.8,
		"progress stays hidden for 2999 ms")) ++failures;
	visual = progress.Update(start + 3s, 0.8, false, false);
	if (!Expect(visual.state == ProgressVisualState::FadingIn
		&& visual.displayedRatio <= 0.8,
		"three-second gate starts progress fade")) ++failures;
	visual = progress.Update(start + 3180ms, 0.8, false, false);
	if (!Expect(visual.state == ProgressVisualState::Visible
		&& visual.opacity == 1.0 && visual.displayedRatio <= 0.8,
		"progress becomes visible after its opacity gate")) ++failures;
	visual = progress.Update(start + 3181ms, 1.0, true, false);
	if (!Expect(visual.state == ProgressVisualState::Visible
		&& visual.displayedRatio == 0.8 && visual.opacity == 1.0,
		"completion retargets to 100 percent without jumping")) ++failures;
	const double fadeStartRatio = visual.displayedRatio;
	visual = progress.Update(start + 3331ms, 1.0, true, false);
	if (!Expect(visual.displayedRatio > fadeStartRatio
			&& visual.displayedRatio < 1.0
			&& progressTiming.FreezeDuration()
				== StartupPreviewProgressHandoffDuration,
		"progress keeps advancing during the frozen 1000 ms handoff fade"))
		++failures;
	visual = progress.Update(start + 3481ms, 1.0, true, false);
	if (!Expect(visual.displayedRatio == 1.0,
		"completed progress reaches 100 percent on its own timeline")) ++failures;
	ProgressVisualReducer boundedCompletion;
	(void)boundedCompletion.Update(start, 0.6, false, false);
	visual = boundedCompletion.Update(start + 300ms, 0.6, true, false);
	if (!Expect(visual.displayedRatio == 0.6,
		"completed progress never exceeds the supplied actual ratio")) ++failures;
	ProgressVisualReducer quick;
	quick.MarkPreviewShown(start);
	visual = quick.Update(start + 2900ms, 1.0, true, false);
	if (!Expect(visual.state == ProgressVisualState::Hidden
		&& visual.opacity == 0.0 && visual.displayedRatio == 0.0,
		"quick startup never shows progress")) ++failures;
	ProgressVisualReducer retry;
	retry.MarkPreviewShown(start);
	(void)retry.Update(start, 0.2, false, false);
	visual = retry.Update(start + 300ms, 0.2, false, false);
	visual = retry.Update(start + 4s, 0.2, false, false);
	if (!Expect(visual.failureColorProgress == 0.0,
		"automatic retry remains normal-colored")) ++failures;
	visual = retry.Update(start + 4001ms, 0.5, false, true);
	if (!Expect(visual.state == ProgressVisualState::Failure
		&& visual.failureColorProgress == 1.0
		&& visual.opacity == 1.0
		&& std::abs(visual.displayedRatio - 0.2) < 0.000001,
		"fatal failure is red immediately without jumping its length"))
		++failures;
	const auto failureQuarterVisual = retry.Update(
		start + 4076ms, 0.9, false, false);
	if (!Expect(failureQuarterVisual.failureColorProgress == 1.0,
		"failed progress remains red after the first failure frame"))
		++failures;
	visual = retry.Update(start + 4151ms, 0.9, false, false);
	if (!Expect(std::abs(visual.displayedRatio - 0.35) < 0.000001
			&& visual.failureColorProgress == 1.0,
		"failed progress length animates while color stays red"))
		++failures;
	visual = retry.Update(start + 4301ms, 0.9, false, false);
	if (!Expect(visual.displayedRatio == 0.5
			&& visual.failureColorProgress == 1.0,
		"failed progress reaches red and ignores later actual changes")) ++failures;
	visual = retry.Update(start + 4302ms, 0.3, false, false);
	if (!Expect(visual.state == ProgressVisualState::Failure
			&& visual.failureColorProgress == 1.0
			&& visual.displayedRatio == 0.5,
		"first failure keeps its red color and frozen actual target sticky"))
		++failures;

	NotifyBarFirstCommittedFrame(80.0, 380.0, true);
	double publishedWidth = 0.0;
	if (!Expect(TakeCommittedStartupBarWidthDip(publishedWidth)
		&& publishedWidth == 470.0 && !TakeCommittedStartupBarWidthDip(publishedWidth),
		"expanded committed target publishes total width once")) ++failures;
	NotifyBarFirstCommittedFrame(80.0, 380.0, false);
	if (!Expect(TakeCommittedStartupBarWidthDip(publishedWidth)
		&& publishedWidth == 470.0,
		"collapsed committed target preserves expanded width cache")) ++failures;
	NotifyBarFirstCommittedFrame(80.0,
		(std::numeric_limits<double>::quiet_NaN)(), true);
	if (!Expect(!TakeCommittedStartupBarWidthDip(publishedWidth),
		"invalid committed geometry is not published")) ++failures;
	NotifyBarFirstCommittedFrame(80.0, 5000.0, true);
	if (!Expect(!TakeCommittedStartupBarWidthDip(publishedWidth),
		"oversized committed geometry is not replaced by a persisted fallback"))
		++failures;
	return failures;
}
