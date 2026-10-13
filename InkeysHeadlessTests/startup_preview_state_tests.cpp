#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>
#include <d2d1_1.h>
#include <wrl/client.h>
#include "../Inkeys/Inkeys/UI/StartupPlacement.h"
#include "../Inkeys/Inkeys/UI/StartupPreview/StartupPreview.Drawing.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")

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

#include <json/json.h>

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
		document["UI"]["Bar"]["Zoom"] = 1.25;
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
			"UI.Bar.Zoom" });
		return {
			read,
			config.Experimental.Inkeys3.UI3.StartupPreview.Enable.load(),
			config.Experimental.Inkeys3.UI3.StartupPreview.
				CachedStartupBarWidthDip.load(),
			config.UI.Bar.Zoom.load(),
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
		for (const Json::Value value : { Json::Value(), Json::Value(-1), Json::Value(0),
			Json::Value(1), Json::Value(2), Json::Value(3), Json::Value("left") })
		{
			Json::Value document(Json::objectValue);
			if (!value.isNull()) document["UI"]["Bar"]["StartupPlacement"] = value;
			document["UI"]["Bar"]["Zoom"] = 1.25;
			if (!Expect(write(document), "write startup placement input fixture")) { ++failures; continue; }
			Inkeys::Config mini;
			const bool miniRead = mini.ReadMini({ "UI.Bar.StartupPlacement", "UI.Bar.Zoom" });
			Inkeys::Config all;
			const bool allRead = all.ReadAll();
			const auto expected = NormalizeStartupPlacement(value.isInt() ? value.asInt() : 0);
			if (!Expect(miniRead && allRead && mini.UI.Bar.Zoom.load() == 1.25
				&& NormalizeStartupPlacement(mini.UI.Bar.StartupPlacement.load()) == expected
				&& NormalizeStartupPlacement(all.UI.Bar.StartupPlacement.load()) == expected,
				"Mini/All normalize missing, illegal and all three startup placements consistently")) ++failures;
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
		float scale = 1.f;
	public:
		UINT width = 0, height = 0;
		HRESULT Initialize(bool isFloating, float pixelScale)
		{
			using namespace Inkeys::UI::StartupPreview;
			floating = isFloating; scale = pixelScale;
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

		std::vector<BgraPixel> Render(double ratio, bool failed, bool showRing, double shimmerPhase)
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
			if (showRing) draw = Drawing::DrawRing(context.Get(), ringStroke.Get(), track.Get(), indicator.Get(),
				D2D1::Point2F(width*.5f, height*.5f), scale, ratio, 1.f, failed);
			const HRESULT end = context->EndDraw(); context->SetTarget(nullptr);
			if (FAILED(draw) || FAILED(end) || FAILED(readable->CopyFromBitmap(nullptr, target.Get(), nullptr))) return {};
			D2D1_MAPPED_RECT mapped{};
			if (FAILED(readable->Map(D2D1_MAP_OPTIONS_READ, &mapped))) return {};
			std::vector<BgraPixel> pixels(static_cast<size_t>(width)*height);
			for (UINT y=0; y<height; ++y) std::memcpy(pixels.data()+static_cast<size_t>(y)*width,
				mapped.bits+static_cast<size_t>(y)*mapped.pitch, static_cast<size_t>(width)*sizeof(BgraPixel));
			(void)readable->Unmap(); return pixels;
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

	int RunStartupPreviewPixelTests()
	{
		int failures=0;
		for (const bool floating : { false, true })
			for (const float scale : { 1.f, 1.5625f, 2.f, 2.25f })
			{
				OffscreenPreview renderer;
				const HRESULT init=renderer.Initialize(floating,scale);
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
				if (floating && !Expect(at(base,5.*scale,5.*scale).alpha==0 && at(base,12.*scale,12.*scale).alpha>0,
					"production n3 superellipse is rounder than a rounded square and fuller than an ellipse")) ++failures;
				if (!Expect(!Blue(at(zero,cx,cy-radius)) && Blue(at(half,cx+radius,cy))
					&& !Blue(at(half,cx-radius,cy)) && Blue(at(full,cx-radius,cy))
					&& at(full,cx,cy).alpha==at(base,cx,cy).alpha,
					"real ring pixels reflect 0/50/100 percent clockwise progress with hollow center")) ++failures;
				if (!Expect(Red(at(fatal,cx+radius,cy)) && !Blue(at(fatal,cx+radius,cy))
					&& Red(at(fatalZero,cx+radius,cy)),"first fatal pixels are red, including zero actual progress without a fake arc")) ++failures;
				if (scale>=1.5f && !Expect(Blue(at(half,cx-scale,cy-radius)) && Blue(at(half,cx-scale,cy+radius)),
					"production open ring arc uses round caps extending beyond top/bottom endpoints")) ++failures;
				int minX=static_cast<int>(renderer.width), maxX=-1;
				int minY=static_cast<int>(renderer.height), maxY=-1;
				for (UINT y=0;y<renderer.height;++y) for (UINT x=0;x<renderer.width;++x)
					if (Blue(full[static_cast<size_t>(y)*renderer.width+x]))
					{ minX=(std::min)(minX,static_cast<int>(x)); maxX=(std::max)(maxX,static_cast<int>(x));
					  minY=(std::min)(minY,static_cast<int>(y)); maxY=(std::max)(maxY,static_cast<int>(y)); }
				int radialStrokePixels=0;
				for (int x=static_cast<int>(cx+8.*scale);x<=static_cast<int>(cx+20.*scale);++x)
					if (Blue(at(full,x,cy))) ++radialStrokePixels;
				if (!Expect(std::abs((maxX-minX+1)-32.*scale)<=2.
					&& std::abs((maxY-minY+1)-32.*scale)<=2.
					&& std::abs(radialStrokePixels-3.*scale)<=2.,
					"production ring preserves 32 DIP outer diameter and 3 DIP stroke at fractional DPI times zoom")) ++failures;
				const std::wstring prefix=(floating?L"corner-":L"center-")+std::to_wstring(scale)+L"x-";
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
	int failures = RunStartupPlacementConfigTests() + RunStartupPlacementGeometryTests() + RunStartupPreviewPixelTests();

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
		&& legacyConfig.zoom == 1.25,
		"legacy main.json without cached width keeps the 470 DIP default"))
		++failures;
	if (!Expect(!ShouldWriteCachedStartupBarWidthDip(470.0, 470.0)
		&& !ShouldWriteCachedStartupBarWidthDip(470.0, 470.005)
		&& ShouldWriteCachedStartupBarWidthDip(470.0, 545.0)
		&& !ShouldWriteCachedStartupBarWidthDip(470.0,
			(std::numeric_limits<double>::quiet_NaN)()),
		"cached width write suppresses noise and rejects invalid candidates")) ++failures;

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
