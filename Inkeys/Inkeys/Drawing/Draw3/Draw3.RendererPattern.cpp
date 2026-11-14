module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <d2d1_1.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <dxgi.h>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

#include "Draw3.WhiteboardPattern.h"
#include "../../UI/Whiteboard/Whiteboard.Theme.h"
#include "../../UI/Whiteboard/Whiteboard.PatternFollow.h"

#pragma comment(lib, "d2d1.lib")

module Inkeys.Drawing.Draw3.renderer;

namespace Inkeys::Drawing::Draw3
{
	// 本实现单元只服务白板背景纹理：CPU 生成 shape 16 的零长度/长线 primitive，
	// 以 under 混合写入 backbuffer（已合成内容之下）。不新增 HLSL，不触碰 L2/L1/L0/快照。

	bool InkRenderer::EnsureWhiteboardPatternPipeline() noexcept
	{
		if (whiteboardPatternUnderBlendState_) return true;
		if (!device) return false;
		// 已合成内容在 destination：纹理只补在其透明度之下（预乘 alpha）。
		D3D11_BLEND_DESC blendDescription = {};
		blendDescription.RenderTarget[0].BlendEnable = TRUE;
		blendDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_INV_DEST_ALPHA;
		blendDescription.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blendDescription.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_INV_DEST_ALPHA;
		blendDescription.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blendDescription.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (FAILED(device->CreateBlendState(&blendDescription,
			whiteboardPatternUnderBlendState_.ReleaseAndGetAddressOf())))
		{
			whiteboardPatternUnderBlendState_.Reset();
			return false;
		}
		return true;
	}

	bool InkRenderer::DrawWhiteboardPatternUnder(
		const WhiteboardPatternParams& params, RECT dirty) noexcept
	{
		whiteboardPatternLastPrimitiveCount_ = 0;
		if (!params.Enabled()) return true; // 桌面/PPT/Blank：空操作，保持画布真透明。
		if (!context || !backBufferRTV || !rasterState || viewportWidth <= 0.0f || viewportHeight <= 0.0f)
			return false;
		// under 混合不是幂等的：只写 dirty∩画布，并以 scissor 硬约束像素。
		const RECT target = {
			std::max(dirty.left, 0L), std::max(dirty.top, 0L),
			std::min(dirty.right, static_cast<LONG>(viewportWidth)),
			std::min(dirty.bottom, static_cast<LONG>(viewportHeight)) };
		if (target.left >= target.right || target.top >= target.bottom) return true;
		// scissor 状态缺失时无法保证不越界重复叠加，宁可放弃本帧纹理。
		if (!laserScissorRasterState || !EnsureWhiteboardPatternPipeline()) return false;

		try
		{
			whiteboardPatternPrimitives_.clear();
			EnumerateWhiteboardPatternPrimitives(params, static_cast<int>(target.left),
				static_cast<int>(target.top), static_cast<int>(target.right),
				static_cast<int>(target.bottom),
				[this](float x0, float y0, float x1, float y1, float halfWidth)
				{
					whiteboardPatternPrimitives_.push_back(ShapePrimitive{
						{ x0, y0, halfWidth, 0.0f }, { x1, y1, 0.0f, 0.0f } });
				});
		}
		catch (...)
		{
			whiteboardPatternPrimitives_.clear();
			return false;
		}
		whiteboardPatternLastPrimitiveCount_ = whiteboardPatternPrimitives_.size();
		if (whiteboardPatternPrimitives_.empty()) return true;

		SetOMTarget(backBufferRTV.Get());
		context->RSSetScissorRects(1, &target);
		// PS 对 shape 16 输出 color.rgb * (color.a * coverage)，under 混合只读取 target0。
		const DirectX::XMFLOAT4 color(params.shade, params.shade, params.shade,
			kWhiteboardPatternAlpha);
		const int result = DrawShapePrimitives(
			std::span<const ShapePrimitive>(whiteboardPatternPrimitives_),
			ShapePrimitiveKind::SolidLine, color, InkOperatorKind::Draw,
			whiteboardPatternUnderBlendState_.Get(), 0.0f, laserScissorRasterState.Get());
		context->RSSetState(rasterState.Get()); // 恢复普通光栅状态，避免影响后续 pass。
		return result == 0;
	}

	namespace
	{
		struct ImageDifference
		{
			int maxDelta = 0;
			std::size_t pixelsOver2 = 0;
			std::size_t pixelsDifferent = 0;
			int firstX = -1, firstY = -1;
		};

		// border > 0 时忽略画布四周 border 像素：旧 Freeze 的 D2D 循环以 x<right/y<bottom 丢弃恰好落在
		// 右/下边缘上的点和线，而 GPU 路径会画出该边缘的半个图元，这是参考实现的边界伪影，不属于一致性对象。
		ImageDifference CompareImages(const std::vector<std::uint32_t>& a,
			const std::vector<std::uint32_t>& b, int width = 0, int border = 0) noexcept
		{
			ImageDifference difference;
			if (a.size() != b.size())
			{
				difference.maxDelta = 255;
				difference.pixelsOver2 = difference.pixelsDifferent = (std::max)(a.size(), b.size());
				return difference;
			}
			for (std::size_t index = 0; index < a.size(); ++index)
			{
				if (a[index] == b[index]) continue;
				const int x = width > 0 ? static_cast<int>(index % static_cast<std::size_t>(width)) : 0;
				const int y = width > 0 ? static_cast<int>(index / static_cast<std::size_t>(width)) : 0;
				if (border > 0 && width > 0)
				{
					const int height = static_cast<int>(a.size() / static_cast<std::size_t>(width));
					if (x < border || y < border || x >= width - border || y >= height - border) continue;
				}
				if (difference.firstX < 0) { difference.firstX = x; difference.firstY = y; }
				++difference.pixelsDifferent;
				int pixelMax = 0;
				for (int shift = 0; shift < 32; shift += 8)
					pixelMax = (std::max)(pixelMax, std::abs(
						static_cast<int>((a[index] >> shift) & 0xFF) -
						static_cast<int>((b[index] >> shift) & 0xFF)));
				difference.maxDelta = (std::max)(difference.maxDelta, pixelMax);
				if (pixelMax > 2) ++difference.pixelsOver2;
			}
			return difference;
		}
	}

	int RunWhiteboardPatternValidation() noexcept
	{
		try
		{
			int failures = 0;
			const auto check = [&](bool condition, const char* name) {
				if (!condition) { ++failures; std::fprintf(stderr, "[WhiteboardPatternGpu] FAIL %s\n", name); }
				return condition;
			};
			using Microsoft::WRL::ComPtr;

			// ---------- 0. CPU 几何：相位公式、门控、键、图元数 ----------
			{
				check(WhiteboardPatternPhase(240.0, 0.0, 24.0) == 0.0, "phase at viewport 0 centres on the screen middle");
				check(WhiteboardPatternPhase(250.0, 0.0, 24.0) == 10.0, "phase takes the remainder of the centre");
				check(WhiteboardPatternPhase(240.0, 7.0, 24.0) == 17.0, "positive viewport moves lattice by -viewport mod step");
				check(WhiteboardPatternPhase(240.0, -7.0, 24.0) == 7.0, "negative viewport");
				check(WhiteboardPatternPhase(240.0, 1048576.0, 24.0) == WhiteboardPatternPhase(240.0, 16.0, 24.0),
					"large viewport equals reduced viewport (double fmod)");
				check(WhiteboardPatternPhase(240.0, -1048576.0, 36.0) == WhiteboardPatternPhase(240.0, -1048576.0 + 36.0 * 29127.0, 36.0),
					"large negative viewport is periodic");
				const WhiteboardTheme dots{ 0x123B32, WhiteboardPattern::Dots };
				const WhiteboardTheme blank{ 0xFFFFFF, WhiteboardPattern::Blank };
				check(!ResolveWhiteboardPatternForFrame(false, dots, 0, 0, 480, 320, 1.0f).Enabled(),
					"desktop/presentation workspace never draws the pattern");
				check(!ResolveWhiteboardPatternForFrame(true, blank, 0, 0, 480, 320, 1.0f).Enabled(),
					"blank pattern never draws");
				check(ResolveWhiteboardPatternForFrame(true, dots, 0, 0, 480, 320, 1.0f).Enabled(),
					"whiteboard dots draw");
				check(!ResolveWhiteboardPatternForFrame(true, dots, 0, 0, 480, 320, 0.0f).Enabled() &&
					!ResolveWhiteboardPatternForFrame(true, dots, 0, 0, 480, 320, std::nanf("")).Enabled(),
					"invalid DPI disables the pattern");
			}

			// ---------- 设备与渲染器 ----------
			ComPtr<ID3D11Device> device;
			ComPtr<ID3D11DeviceContext> context;
			constexpr D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
			D3D_FEATURE_LEVEL actualLevel = {};
			if (!check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
				D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
				device.GetAddressOf(), &actualLevel, context.GetAddressOf())), "offscreen WARP device"))
			{
				std::fprintf(stderr, "[WhiteboardPatternGpu] FAIL no D3D11 WARP device; CPU-only checks ran failures=%d\n", failures);
				return 1;
			}

			const auto setup = [&](InkRenderer& renderer, UINT width, UINT height) {
				renderer.device = device;
				renderer.context = context;
				if (!renderer.CreateFixedResources(D3D_DRIVER_TYPE_WARP)) return false;
				D3D11_TEXTURE2D_DESC description = {};
				description.Width = width;
				description.Height = height;
				description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
				description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
				description.Usage = D3D11_USAGE_DEFAULT;
				description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
				if (FAILED(device->CreateTexture2D(&description, nullptr, renderer.backBufferTexture.GetAddressOf())) ||
					FAILED(device->CreateRenderTargetView(renderer.backBufferTexture.Get(), nullptr, renderer.backBufferRTV.GetAddressOf())) ||
					FAILED(device->CreateTexture2D(&description, nullptr, renderer.layerL2Texture.GetAddressOf())) ||
					!renderer.CreateOperatorLayerResources(width, height, renderer.layerL1) ||
					!renderer.CreateOperatorLayerResources(width, height, renderer.layerL0)) return false;
				renderer.SetScreenSize(static_cast<float>(width), static_cast<float>(height));
				renderer.ClearOperatorLayer(renderer.layerL1);
				renderer.ClearOperatorLayer(renderer.layerL0);
				return true;
			};
			const auto makeStaging = [&](UINT width, UINT height) {
				D3D11_TEXTURE2D_DESC description = {};
				description.Width = width;
				description.Height = height;
				description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
				description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
				description.Usage = D3D11_USAGE_STAGING;
				description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				ComPtr<ID3D11Texture2D> staging;
				device->CreateTexture2D(&description, nullptr, staging.GetAddressOf());
				return staging;
			};
			const auto readTexture = [&](ID3D11Texture2D* source, ID3D11Texture2D* staging, UINT width, UINT height) {
				std::vector<std::uint32_t> out(static_cast<std::size_t>(width) * height, 0u);
				context->OMSetRenderTargets(0, nullptr, nullptr);
				context->CopyResource(staging, source);
				D3D11_MAPPED_SUBRESOURCE mapped = {};
				if (!check(SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)), "readback map")) return out;
				for (UINT y = 0; y < height; ++y)
					std::memcpy(out.data() + static_cast<std::size_t>(y) * width,
						static_cast<const unsigned char*>(mapped.pData) + y * mapped.RowPitch, width * 4);
				context->Unmap(staging, 0);
				return out;
			};

			// 与 DrawingController::CompositeLayersToBackBuffer 相同的调用顺序，末尾追加纹理 under pass。
			const auto composite = [&](InkRenderer& renderer, const WhiteboardPatternParams& params, RECT dirty) {
				context->OMSetRenderTargets(0, nullptr, nullptr);
				renderer.CopyResource(renderer.backBufferTexture.Get(), renderer.layerL2Texture.Get(), dirty);
				bool ok = renderer.ApplyOperatorLayers(renderer.backBufferRTV.Get(),
					renderer.layerL1, renderer.layerL0, dirty);
				if (ok) ok = renderer.DrawWhiteboardPatternUnder(params, dirty);
				return ok;
			};

			// D2D 参考：直接调用旧 Freeze 使用的 DrawThemePattern（点/网格、18% 黑白、屏幕中心相位）。
			ComPtr<ID2D1Factory1> d2dFactory;
			ComPtr<ID2D1Device> d2dDevice;
			ComPtr<ID2D1DeviceContext> d2dContext;
			{
				ComPtr<IDXGIDevice> dxgiDevice;
				if (!check(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
						__uuidof(ID2D1Factory1), nullptr, reinterpret_cast<void**>(d2dFactory.GetAddressOf()))) &&
					SUCCEEDED(device.As(&dxgiDevice)) &&
					SUCCEEDED(d2dFactory->CreateDevice(dxgiDevice.Get(), d2dDevice.GetAddressOf())) &&
					SUCCEEDED(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, d2dContext.GetAddressOf())),
					"D2D reference context on the WARP device"))
				{
					std::fprintf(stderr, "[WhiteboardPatternGpu] FAIL D2D reference unavailable failures=%d\n", failures);
					return 1;
				}
			}
			// shifted=false 与旧 Freeze 完全一致（bounds=[0,W]x[0,H] DIP）；shifted=true 把格点锚点移到
			// (anchorX, anchorY) 像素，并用足够大的 bounds 覆盖整屏，以验证视口位移后的相位。
			const auto renderLegacy = [&](UINT width, UINT height, float scale, std::uint32_t rgb,
				int pattern, ID3D11Texture2D* staging, bool shifted, double anchorX, double anchorY) {
				D3D11_TEXTURE2D_DESC description = {};
				description.Width = width;
				description.Height = height;
				description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
				description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
				description.Usage = D3D11_USAGE_DEFAULT;
				description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
				ComPtr<ID3D11Texture2D> texture;
				ComPtr<IDXGISurface> surface;
				ComPtr<ID2D1Bitmap1> bitmap;
				std::vector<std::uint32_t> empty;
				if (!check(SUCCEEDED(device->CreateTexture2D(&description, nullptr, texture.GetAddressOf())) &&
					SUCCEEDED(texture.As(&surface)) &&
					SUCCEEDED(d2dContext->CreateBitmapFromDxgiSurface(surface.Get(),
						D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
							D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f),
						bitmap.GetAddressOf())), "D2D reference target")) return empty;
				D2D1_RECT_F bounds = { 0.0f, 0.0f, static_cast<float>(width / scale), static_cast<float>(height / scale) };
				if (shifted)
				{
					const double step = 24.0 * scale;
					const double halfX = (std::max)(std::fabs(anchorX), std::fabs(width - anchorX)) + 4.0 * step;
					const double halfY = (std::max)(std::fabs(anchorY), std::fabs(height - anchorY)) + 4.0 * step;
					bounds = { static_cast<float>((anchorX - halfX) / scale), static_cast<float>((anchorY - halfY) / scale),
						static_cast<float>((anchorX + halfX) / scale), static_cast<float>((anchorY + halfY) / scale) };
				}
				d2dContext->SetTarget(bitmap.Get());
				d2dContext->BeginDraw();
				d2dContext->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
				d2dContext->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale));
				Inkeys::UI::WhiteboardVisual::DrawThemePattern(d2dContext.Get(), bounds, rgb, pattern);
				const HRESULT drawn = d2dContext->EndDraw();
				d2dContext->SetTarget(nullptr);
				if (!check(SUCCEEDED(drawn), "D2D reference draw")) return empty;
				return readTexture(texture.Get(), staging, width, height);
			};

			constexpr UINT W = 480, H = 320;
			InkRenderer renderer;
			if (!check(setup(renderer, W, H), "production renderer with offscreen layers")) return 1;
			ComPtr<ID3D11Texture2D> staging = makeStaging(W, H);
			if (!check(staging != nullptr, "staging texture")) return 1;
			const RECT full = { 0, 0, static_cast<LONG>(W), static_cast<LONG>(H) };
			const auto at = [&](const std::vector<std::uint32_t>& pixels, int x, int y) {
				return pixels[static_cast<std::size_t>(y) * W + x];
			};
			const auto alphaOf = [](std::uint32_t value) { return static_cast<int>(value >> 24); };
			const auto uploadL2 = [&](const std::vector<std::uint32_t>& pixels) {
				context->UpdateSubresource(renderer.layerL2Texture.Get(), 0, nullptr, pixels.data(), W * 4, 0);
			};
			const auto readBackBuffer = [&]() {
				return readTexture(renderer.backBufferTexture.Get(), staging.Get(), W, H);
			};

			// 产品 L2 内容：不透明绿块、半透明红块、不透明蓝块，其余透明。
			std::vector<std::uint32_t> l2(static_cast<std::size_t>(W) * H, 0u);
			const auto fillBlock = [&](int x0, int y0, int x1, int y1, std::uint32_t value) {
				for (int y = y0; y < y1; ++y)
					for (int x = x0; x < x1; ++x) l2[static_cast<std::size_t>(y) * W + x] = value;
			};
			fillBlock(20, 40, 80, 100, 0xFF00C800u);
			fillBlock(120, 40, 180, 100, 0x80800000u);
			fillBlock(300, 20, 360, 80, 0xFF0000FFu);
			const std::vector<std::uint32_t> zeros(static_cast<std::size_t>(W) * H, 0u);

			struct Config { float scale; std::uint32_t rgb; WhiteboardPattern pattern; const char* name; };
			const std::array<Config, 12> configs = { {
				{ 1.0f, 0xF5F1E6u, WhiteboardPattern::Dots, "100% light dots" },
				{ 1.0f, 0x123B32u, WhiteboardPattern::Dots, "100% dark dots" },
				{ 1.0f, 0xF5F1E6u, WhiteboardPattern::Grid, "100% light grid" },
				{ 1.0f, 0x123B32u, WhiteboardPattern::Grid, "100% dark grid" },
				{ 1.5f, 0xFFFFFFu, WhiteboardPattern::Dots, "150% light dots" },
				{ 1.5f, 0x172A46u, WhiteboardPattern::Dots, "150% dark dots" },
				{ 1.5f, 0xE5E7EBu, WhiteboardPattern::Grid, "150% light grid" },
				{ 1.5f, 0x000000u, WhiteboardPattern::Grid, "150% dark grid" },
				{ 2.0f, 0xF5F1E6u, WhiteboardPattern::Dots, "200% light dots" },
				{ 2.0f, 0x123B32u, WhiteboardPattern::Dots, "200% dark dots" },
				{ 2.0f, 0xF5F1E6u, WhiteboardPattern::Grid, "200% light grid" },
				{ 2.0f, 0x123B32u, WhiteboardPattern::Grid, "200% dark grid" } } };

			// 可信快照资源：同时验证快照不含纹理，并走与产品平移分支相同的合成序列。
			const bool snapshotReady = renderer.CreateTrustedL2SnapshotResources(W, H) &&
				renderer.CreateTrustedL2SnapshotPipeline();
			check(snapshotReady, "trusted L2 snapshot resources for the pan-branch check");

			for (std::size_t configIndex = 0; configIndex < configs.size(); ++configIndex)
			{
				const Config& config = configs[configIndex];
				const WhiteboardTheme theme{ config.rgb, config.pattern };
				const int legacyPattern = config.pattern == WhiteboardPattern::Grid ? 2 : 1;
				const bool grid = config.pattern == WhiteboardPattern::Grid;
				char label[160];
				const auto named = [&](const char* text) {
					std::snprintf(label, sizeof(label), "%s: %s", config.name, text);
					return static_cast<const char*>(label);
				};
				const WhiteboardPatternParams enabled = ResolveWhiteboardPatternForFrame(
					true, theme, 0.0, 0.0, static_cast<int>(W), static_cast<int>(H), config.scale);
				const WhiteboardPatternParams disabled = ResolveWhiteboardPatternForFrame(
					false, theme, 0.0, 0.0, static_cast<int>(W), static_cast<int>(H), config.scale);

				// (a) 空 L2：纹理像素与旧 Freeze 的 D2D 参考比较；关闭时字节为零。
				renderer.ClearOperatorLayer(renderer.layerL1);
				renderer.ClearOperatorLayer(renderer.layerL0);
				uploadL2(zeros);
				check(composite(renderer, disabled, full), named("disabled composite"));
				check(readBackBuffer() == zeros, named("desktop/presentation gating leaves the canvas truly transparent"));
				check(composite(renderer, enabled, full), named("enabled composite"));
				const std::size_t primitiveCount = renderer.whiteboardPatternLastPrimitiveCount_;
				const std::vector<std::uint32_t> patternOnly = readBackBuffer();
				const std::vector<std::uint32_t> legacy = renderLegacy(W, H, config.scale, config.rgb,
					legacyPattern, staging.Get(), false, 0.0, 0.0);
				const ImageDifference phaseZero = CompareImages(patternOnly, legacy, static_cast<int>(W), 8);
				std::size_t legacyNonzero = 0, patternNonzero = 0;
				for (std::uint32_t value : legacy) legacyNonzero += value != 0;
				for (std::uint32_t value : patternOnly) patternNonzero += value != 0;
				std::fprintf(stderr, "[WhiteboardPatternGpu] %s phase0 vs D2D legacy: maxDelta=%d pixelsOver2=%zu differing=%zu nonzero(gpu/d2d)=%zu/%zu primitives=%zu\n",
					config.name, phaseZero.maxDelta, phaseZero.pixelsOver2, phaseZero.pixelsDifferent,
					patternNonzero, legacyNonzero, primitiveCount);
				check(legacyNonzero > 0 && patternNonzero > 0, named("reference and GPU pattern are not empty"));
				// 18% 预乘：黑/白纹理的 alpha 峰值约 46；深色背景为白色（RGB=alpha），浅色为黑色（RGB=0）。
				std::uint32_t peak = 0;
				for (std::uint32_t value : patternOnly) peak = (std::max)(peak, value >> 24);
				// 点在 100% 时单点覆盖约 79%（峰值约 36），网格交点最高约 0.33*255；只做合理范围检查。
				check(peak >= 20 && peak <= 90, named("peak alpha stays within the 18% coverage range"));
				const std::uint32_t sample = *std::max_element(patternOnly.begin(), patternOnly.end(),
					[](std::uint32_t lhs, std::uint32_t rhs) { return (lhs >> 24) < (rhs >> 24); });
				check(IsLightWhiteboardBackground(config.rgb) ? (sample & 0x00FFFFFF) == 0
					: ((sample >> 16) & 0xFF) == (sample >> 24) && ((sample >> 8) & 0xFF) == (sample >> 24) &&
						(sample & 0xFF) == (sample >> 24), named("pattern colour is black on light and white on dark"));
				// 网格在 100%/150%（线宽落在整数像素）与旧 Freeze 逐像素一致；200% 的 2 px 线宽、点的 1 DIP 圆
				// 则受 shader 约 1.5 px 的 smoothstep 抗锯齿斜坡与 D2D 面积覆盖的差异影响，只在边缘尾部相差若干 LSB。
				const int phaseTolerance = grid ? (config.scale < 1.75f ? 2 : 8) : 10;
				check(phaseZero.maxDelta <= phaseTolerance, named(grid
					? (config.scale < 1.75f ? "grid matches the legacy Freeze pattern within 2 LSB"
						: "wide grid matches the legacy Freeze pattern within the AA-tail tolerance")
					: "dots match the legacy Freeze pattern within the AA-tail tolerance"));

				// (e)(f) 墨迹/橡皮/L0 在纹理之上，擦除露出纹理；L2 与快照不含纹理。
				uploadL2(l2);
				renderer.SetOperatorTarget(renderer.layerL1);
				const std::array<InkPoint, 2> erase = { InkPoint{ 50.0f, 70.0f, 14.0f, 0.0f }, InkPoint{ 52.0f, 70.0f, 14.0f, 1.0f } };
				check(renderer.DrawStroke(erase, DirectX::XMFLOAT4(0, 0, 0, 1), StrokeShape::RoundCapsule,
					InkOperatorKind::Erase) == 0, named("eraser stroke on L1"));
				renderer.SetOperatorTarget(renderer.layerL0);
				const std::array<InkPoint, 2> ink = { InkPoint{ 200.0f, 150.0f, 9.0f, 0.0f }, InkPoint{ 290.0f, 170.0f, 9.0f, 1.0f } };
				check(renderer.DrawStroke(ink, DirectX::XMFLOAT4(0.9f, 0.1f, 0.1f, 1.0f), StrokeShape::RoundCapsule,
					InkOperatorKind::Draw) == 0, named("ink stroke on L0"));
				check(composite(renderer, disabled, full), named("baseline composite without the pattern"));
				const std::vector<std::uint32_t> base = readBackBuffer();
				check(composite(renderer, enabled, full), named("composite with the pattern"));
				const std::vector<std::uint32_t> withPattern = readBackBuffer();
				// under 恒等式：out = base + (1 - base.a) * pattern（预乘），误差 <= 2 LSB。
				std::size_t identityBad = 0;
				std::size_t opaqueChanged = 0;
				for (std::size_t index = 0; index < base.size(); ++index)
				{
					const int baseAlpha = alphaOf(base[index]);
					if (baseAlpha == 255 && withPattern[index] != base[index]) ++opaqueChanged;
					for (int shift = 0; shift < 32; shift += 8)
					{
						const double expected = ((base[index] >> shift) & 0xFF) +
							(255 - baseAlpha) / 255.0 * ((patternOnly[index] >> shift) & 0xFF);
						if (std::fabs(((withPattern[index] >> shift) & 0xFF) - (std::min)(expected, 255.0)) > 2.0)
						{ ++identityBad; break; }
					}
				}
				check(identityBad == 0, named("pattern is exactly under the ink: out = base + (1-a)*pattern"));
				check(opaqueChanged == 0, named("opaque ink pixels are never altered by the pattern"));
				check(at(withPattern, 24, 94) == at(base, 24, 94) && alphaOf(at(base, 24, 94)) == 255,
					named("opaque L2 block stays on top"));
				check(alphaOf(at(base, 50, 70)) == 0 && at(withPattern, 50, 70) == at(patternOnly, 50, 70),
					named("erasing L2 ink reveals the pattern (under semantics)"));
				check(alphaOf(at(base, 245, 160)) == 255 && at(withPattern, 245, 160) == at(base, 245, 160),
					named("L0 ink stays on top of the pattern"));
				check(readTexture(renderer.layerL2Texture.Get(), staging.Get(), W, H) == l2,
					named("L2 contains no pattern pixels"));
				if (snapshotReady)
				{
					check(renderer.RefreshTrustedL2Snapshot(0.0f, 0.0f), named("snapshot refresh"));
					check(readTexture(renderer.trustedL2SnapshotTexture_.Get(), staging.Get(), W, H) == l2,
						named("trusted L2 snapshot contains no pattern pixels"));
				}

				// (b) alpha 不累积：重复整帧、分块、重叠矩形都与单次整帧逐字节相同。
				bool repeatedSame = true;
				for (int repeat = 0; repeat < 4; ++repeat)
				{
					composite(renderer, enabled, full);
					repeatedSame = repeatedSame && readBackBuffer() == withPattern;
				}
				check(repeatedSame, named("repeated frames never accumulate alpha (byte-identical)"));
				context->OMSetRenderTargets(0, nullptr, nullptr);
				renderer.ClearRTV(renderer.backBufferRTV.Get(), kTransparentLayerClearColor);
				bool tilesOk = true;
				for (int top = 0; top < static_cast<int>(H); top += 67)
					for (int left = 0; left < static_cast<int>(W); left += 71)
						tilesOk = composite(renderer, enabled, RECT{ left, top,
							(std::min)(left + 71, static_cast<int>(W)), (std::min)(top + 67, static_cast<int>(H)) }) && tilesOk;
				const std::vector<std::uint32_t> tiledImage = readBackBuffer();
				const ImageDifference tiledDifference = CompareImages(tiledImage, withPattern, static_cast<int>(W));
				if (tiledDifference.pixelsDifferent != 0)
				{
					const std::size_t firstIndex = static_cast<std::size_t>(tiledDifference.firstY) * W + tiledDifference.firstX;
					std::fprintf(stderr, "[WhiteboardPatternGpu] %s tiled vs full: differing=%zu maxDelta=%d first=(%d,%d) tiled=%08X full=%08X\n",
						config.name, tiledDifference.pixelsDifferent, tiledDifference.maxDelta,
						tiledDifference.firstX, tiledDifference.firstY, tiledImage[firstIndex], withPattern[firstIndex]);
				}
				check(tilesOk && tiledDifference.pixelsDifferent == 0, named("tiled dirty rectangles equal one full frame byte-for-byte"));
				const std::array<RECT, 5> overlaps = { RECT{ 5, 7, 300, 200 }, RECT{ 100, 50, 400, 310 },
					RECT{ 0, 0, 1, 320 }, RECT{ 477, 0, 480, 320 }, RECT{ 30, 300, 460, 320 } };
				bool overlapOk = true;
				for (const RECT& rect : overlaps) overlapOk = composite(renderer, enabled, rect) && overlapOk;
				const ImageDifference overlapDifference = CompareImages(readBackBuffer(), withPattern, static_cast<int>(W));
				if (overlapDifference.pixelsDifferent != 0)
					std::fprintf(stderr, "[WhiteboardPatternGpu] %s overlap vs full: differing=%zu maxDelta=%d first=(%d,%d)\n",
						config.name, overlapDifference.pixelsDifferent, overlapDifference.maxDelta,
						overlapDifference.firstX, overlapDifference.firstY);
				check(overlapOk && overlapDifference.pixelsDifferent == 0, named("overlapping partial recomposites stay byte-identical"));
				// 反证：无重新拷贝地重复画 under pass 会累积 alpha（说明上面的检查有判别力）。
				composite(renderer, enabled, full);
				check(renderer.DrawWhiteboardPatternUnder(enabled, full), named("deliberate double pass"));
				const std::vector<std::uint32_t> doubled = readBackBuffer();
				std::uint32_t doubledPeak = 0;
				for (std::uint32_t value : doubled) doubledPeak = (std::max)(doubledPeak, value >> 24);
				check(doubledPeak > peak + 10, named("a second under pass without recopy would accumulate alpha"));

				// (c) 相位随视口：整周期位移不变；小数位移与旧函数按锚点平移后的参考一致；超大视口按 double fmod。
				const double step = enabled.stepPx;
				const auto composeAtViewport = [&](double viewportX, double viewportY) {
					renderer.ClearOperatorLayer(renderer.layerL1);
					renderer.ClearOperatorLayer(renderer.layerL0);
					uploadL2(zeros);
					composite(renderer, ResolveWhiteboardPatternForFrame(true, theme, viewportX, viewportY,
						static_cast<int>(W), static_cast<int>(H), config.scale), full);
					return readBackBuffer();
				};
				check(composeAtViewport(step * 3.0, -step * 2.0) == patternOnly,
					named("viewport shifted by whole periods leaves the pattern unchanged"));
				check(composeAtViewport(static_cast<double>(1048576.0f), static_cast<double>(-1048576.0f)) ==
					composeAtViewport(std::fmod(1048576.0, step), -std::fmod(1048576.0, step)),
					named("1048576 px viewport follows the exact fmod phase"));
				for (const std::pair<double, double> shift : { std::pair<double, double>{ 13.25, -7.5 }, { -41.75, 22.0 } })
				{
					const std::vector<std::uint32_t> moved = composeAtViewport(shift.first, shift.second);
					const std::vector<std::uint32_t> reference = renderLegacy(W, H, config.scale, config.rgb,
						legacyPattern, staging.Get(), true, W * 0.5 - shift.first, H * 0.5 - shift.second);
					const ImageDifference difference = CompareImages(moved, reference, static_cast<int>(W), 8);
					std::fprintf(stderr, "[WhiteboardPatternGpu] %s viewport(%.2f,%.2f) vs shifted D2D: maxDelta=%d pixelsOver2=%zu\n",
						config.name, shift.first, shift.second, difference.maxDelta, difference.pixelsOver2);
					check(moved != patternOnly, named("fractional viewport moves the pattern"));
					check(difference.maxDelta <= 12, named("shifted pattern matches the anchor-shifted D2D reference"));
				}
			}

			// 平移分支序列（快照 under -> L1/L0 -> 纹理 under）与恒等式：纹理在快照之下。
			if (snapshotReady)
			{
				const Config& config = configs[0];
				const WhiteboardTheme theme{ config.rgb, config.pattern };
				renderer.ClearOperatorLayer(renderer.layerL1);
				renderer.ClearOperatorLayer(renderer.layerL0);
				uploadL2(l2);
				renderer.RefreshTrustedL2Snapshot(0.0f, 0.0f);
				// 当前 L2 只保留一小块，其余区域由快照（移动 10/6 像素）补洞。
				std::vector<std::uint32_t> partial(static_cast<std::size_t>(W) * H, 0u);
				for (int y = 40; y < 100; ++y)
					for (int x = 20; x < 80; ++x) partial[static_cast<std::size_t>(y) * W + x] = 0xFF00C800u;
				uploadL2(partial);
				const auto panCompose = [&](const WhiteboardPatternParams& params) {
					context->OMSetRenderTargets(0, nullptr, nullptr);
					const bool snapshotOk = renderer.CompositeTrustedL2SnapshotToBackBuffer(
						{ 10.0f, 6.0f, 0.0f, 0.0f, 0.0f, 1.0f, full });
					const bool applied = renderer.ApplyOperatorLayers(renderer.backBufferRTV.Get(),
						renderer.layerL1, renderer.layerL0, full);
					const bool drawn = applied && renderer.DrawWhiteboardPatternUnder(params, full);
					return snapshotOk && applied && drawn;
				};
				const WhiteboardPatternParams enabled = ResolveWhiteboardPatternForFrame(
					true, theme, 10.0, 6.0, static_cast<int>(W), static_cast<int>(H), 1.0f);
				check(panCompose({}), "pan branch composite without the pattern");
				const std::vector<std::uint32_t> panBase = readBackBuffer();
				check(panCompose(enabled), "pan branch composite with the pattern");
				const std::vector<std::uint32_t> panWith = readBackBuffer();
				// 纹理参考：同视口、空层下的纯纹理。
				uploadL2(zeros);
				renderer.InvalidateTrustedL2Snapshot();
				check(composite(renderer, enabled, full), "pan reference pattern");
				const std::vector<std::uint32_t> panPattern = readBackBuffer();
				std::size_t bad = 0, snapshotPixels = 0;
				for (std::size_t index = 0; index < panBase.size(); ++index)
				{
					snapshotPixels += alphaOf(panBase[index]) != 0;
					for (int shift = 0; shift < 32; shift += 8)
					{
						const double expected = ((panBase[index] >> shift) & 0xFF) +
							(255 - alphaOf(panBase[index])) / 255.0 * ((panPattern[index] >> shift) & 0xFF);
						if (std::fabs(((panWith[index] >> shift) & 0xFF) - (std::min)(expected, 255.0)) > 2.0) { ++bad; break; }
					}
				}
				check(snapshotPixels > 3600, "pan branch: the snapshot fills the transparent gaps");
				check(bad == 0, "pan branch: the pattern sits under both the L2 and the trusted snapshot");
			}

			// 4K 成本：只记录，不做断言（WARP 非真实 GPU）。
			{
				struct PerfCase { float scale; WhiteboardPattern pattern; const char* name; };
				const std::array<PerfCase, 6> cases = { {
					{ 1.0f, WhiteboardPattern::Dots, "100% dots" }, { 1.5f, WhiteboardPattern::Dots, "150% dots" },
					{ 2.0f, WhiteboardPattern::Dots, "200% dots" }, { 1.0f, WhiteboardPattern::Grid, "100% grid" },
					{ 1.5f, WhiteboardPattern::Grid, "150% grid" }, { 2.0f, WhiteboardPattern::Grid, "200% grid" } } };
				constexpr UINT W4 = 3840, H4 = 2160;
				InkRenderer big;
				if (check(setup(big, W4, H4), "3840x2160 renderer for the cost record"))
				{
					ComPtr<ID3D11Texture2D> tiny = makeStaging(1, 1);
					const auto settle = [&]() {
						const D3D11_BOX box = { 0, 0, 0, 1, 1, 1 };
						context->OMSetRenderTargets(0, nullptr, nullptr);
						context->CopySubresourceRegion(tiny.Get(), 0, 0, 0, 0, big.backBufferTexture.Get(), 0, &box);
						D3D11_MAPPED_SUBRESOURCE mapped = {};
						if (SUCCEEDED(context->Map(tiny.Get(), 0, D3D11_MAP_READ, 0, &mapped))) context->Unmap(tiny.Get(), 0);
					};
					LARGE_INTEGER frequency = {};
					QueryPerformanceFrequency(&frequency);
					const RECT fullBig = { 0, 0, static_cast<LONG>(W4), static_cast<LONG>(H4) };
					const auto timeFrame = [&](const WhiteboardPatternParams& params) {
						std::array<double, 5> samples = {};
						for (double& sample : samples)
						{
							settle();
							LARGE_INTEGER begin = {}, end = {};
							QueryPerformanceCounter(&begin);
							composite(big, params, fullBig);
							settle();
							QueryPerformanceCounter(&end);
							sample = static_cast<double>(end.QuadPart - begin.QuadPart) * 1000.0 / frequency.QuadPart;
						}
						std::sort(samples.begin(), samples.end());
						return samples[samples.size() / 2];
					};
					composite(big, {}, fullBig);
					const double baseMs = timeFrame({});
					std::fprintf(stderr, "[WhiteboardPatternGpu] perf 3840x2160 WARP composite without pattern median=%.2f ms\n", baseMs);
					for (const PerfCase& perf : cases)
					{
						const WhiteboardPatternParams params = ResolveWhiteboardPatternForFrame(true,
							WhiteboardTheme{ 0x123B32, perf.pattern }, 0.0, 0.0,
							static_cast<int>(W4), static_cast<int>(H4), perf.scale);
						const double withMs = timeFrame(params);
						std::fprintf(stderr, "[WhiteboardPatternGpu] perf 3840x2160 %s primitives=%zu composite+pattern median=%.2f ms (delta=%.2f ms)\n",
							perf.name, big.whiteboardPatternLastPrimitiveCount_, withMs, withMs - baseMs);
					}
					big.ReleaseResources();
				}
			}

			// Freeze 纯色辅助：旧纹理参数已从 Freeze 移除，任意主题都只产生不透明纯色。
			{
				ComPtr<ID3D11Texture2D> texture;
				ComPtr<IDXGISurface> surface;
				ComPtr<ID2D1Bitmap1> bitmap;
				D3D11_TEXTURE2D_DESC description = {};
				description.Width = 64;
				description.Height = 48;
				description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
				description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
				description.Usage = D3D11_USAGE_DEFAULT;
				description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
				ComPtr<ID3D11Texture2D> solidStaging = makeStaging(64, 48);
				if (check(SUCCEEDED(device->CreateTexture2D(&description, nullptr, texture.GetAddressOf())) &&
					SUCCEEDED(texture.As(&surface)) && SUCCEEDED(d2dContext->CreateBitmapFromDxgiSurface(surface.Get(),
						D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
							D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f),
						bitmap.GetAddressOf())) && solidStaging != nullptr, "Freeze solid-colour target"))
				{
					d2dContext->SetTarget(bitmap.Get());
					d2dContext->BeginDraw();
					d2dContext->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
					d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
					Inkeys::UI::WhiteboardVisual::DrawThemeSolid(d2dContext.Get(), D2D1::RectF(0, 0, 64, 48), 0x172A46);
					check(SUCCEEDED(d2dContext->EndDraw()), "Freeze solid-colour draw");
					d2dContext->SetTarget(nullptr);
					const std::vector<std::uint32_t> pixels = readTexture(texture.Get(), solidStaging.Get(), 64, 48);
					bool uniform = !pixels.empty();
					for (std::uint32_t value : pixels) uniform = uniform && value == 0xFF172A46u;
					check(uniform, "DrawThemeSolid (Draw3-owned Freeze fallback) draws only the opaque theme colour");
				}
			}

			// 归属开关：Draw3 路径只在归属 Draw3 时产生像素（门控纯函数），Freeze 归属下 Draw3 侧惰性。
			// 默认归属 Freeze 时 Freeze 画点阵/网格：带相位的辅助函数在视口 (0,0) 与旧 DrawThemePattern 逐位一致
			// （忽略右/下边缘新增的半个点），在非零视口与旧函数按锚点平移后的参考一致。两种归属下这些辅助都保持可用。
			{
				check(WhiteboardPatternDrawnByDraw3(true) == kWhiteboardPatternOwnedByDraw3 &&
					!WhiteboardPatternDrawnByDraw3(false) &&
					WhiteboardPatternDrawnByFreeze() == kWhiteboardPatternOwnedByFreeze &&
					kWhiteboardPatternOwnedByFreeze != kWhiteboardPatternOwnedByDraw3,
					"owner switch is a single exclusive gate");
				std::fprintf(stderr, "[WhiteboardPatternGpu] owner=%s\n", kWhiteboardPatternOwnedByFreeze ? "Freeze" : "Draw3");
				const auto renderPhased = [&](UINT width, UINT height, float scale, std::uint32_t rgb, int pattern,
					double viewportX, double viewportY, ID3D11Texture2D* stagingTexture) {
					D3D11_TEXTURE2D_DESC description = {};
					description.Width = width;
					description.Height = height;
					description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
					description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
					description.Usage = D3D11_USAGE_DEFAULT;
					description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
					ComPtr<ID3D11Texture2D> texture;
					ComPtr<IDXGISurface> surface;
					ComPtr<ID2D1Bitmap1> bitmap;
					std::vector<std::uint32_t> empty;
					if (!check(SUCCEEDED(device->CreateTexture2D(&description, nullptr, texture.GetAddressOf())) &&
						SUCCEEDED(texture.As(&surface)) &&
						SUCCEEDED(d2dContext->CreateBitmapFromDxgiSurface(surface.Get(),
							D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
								D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f),
							bitmap.GetAddressOf())), "Freeze phased target")) return empty;
					const Inkeys::UI::WhiteboardFollow::Phase phase = Inkeys::UI::WhiteboardFollow::ResolvePhase(
						width, height, scale, viewportX, viewportY);
					d2dContext->SetTarget(bitmap.Get());
					d2dContext->BeginDraw();
					d2dContext->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
					d2dContext->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale));
					Inkeys::UI::WhiteboardVisual::DrawThemePatternPhase(d2dContext.Get(),
						D2D1::RectF(0.0f, 0.0f, static_cast<float>(width / scale), static_cast<float>(height / scale)),
						rgb, pattern, phase.xDip, phase.yDip);
					const HRESULT drawn = d2dContext->EndDraw();
					d2dContext->SetTarget(nullptr);
					if (!check(SUCCEEDED(drawn), "Freeze phased draw")) return empty;
					return readTexture(texture.Get(), stagingTexture, width, height);
				};
				constexpr int kEdge = 3; // 忽略右/下边缘新增的半个点/线以及左/上抗锯齿边
				for (const float scale : { 1.0f, 1.5f })
					for (const int pattern : { 1, 2 })
					{
						const std::string caption = std::string(pattern == 1 ? "dots" : "grid") + "@" + (scale == 1.0f ? "1.0" : "1.5");
						const std::vector<std::uint32_t> legacy = renderLegacy(W, H, scale, 0x123B32, pattern, staging.Get(), false, 0, 0);
						const std::vector<std::uint32_t> phased = renderPhased(W, H, scale, 0x123B32, pattern, 0.0, 0.0, staging.Get());
						const ImageDifference zero = CompareImages(phased, legacy, static_cast<int>(W), kEdge);
						std::fprintf(stderr, "[WhiteboardPatternGpu] Freeze %s viewport(0,0) vs legacy DrawThemePattern: maxDelta=%d differing=%zu\n",
							caption.c_str(), zero.maxDelta, zero.pixelsDifferent);
						check(!legacy.empty() && !phased.empty() && phased != std::vector<std::uint32_t>(phased.size(), 0u),
							(caption + ": phased pattern draws pixels").c_str());
						check(zero.maxDelta <= (scale == 1.0f ? 0 : 1), (caption + ": viewport (0,0) equals the legacy Freeze pattern").c_str());
						for (const std::pair<double, double> shift : { std::pair<double, double>{ 137.25, -61.5 }, { -203.75, 44.125 } })
						{
							const std::vector<std::uint32_t> moved = renderPhased(W, H, scale, 0x123B32, pattern, shift.first, shift.second, staging.Get());
							const std::vector<std::uint32_t> reference = renderLegacy(W, H, scale, 0x123B32, pattern, staging.Get(),
								true, W * 0.5 - shift.first, H * 0.5 - shift.second);
							const ImageDifference difference = CompareImages(moved, reference, static_cast<int>(W), 8);
							std::fprintf(stderr, "[WhiteboardPatternGpu] Freeze %s viewport(%.2f,%.2f) vs anchor-shifted legacy: maxDelta=%d pixelsOver2=%zu differing=%zu\n",
								caption.c_str(), shift.first, shift.second, difference.maxDelta, difference.pixelsOver2, difference.pixelsDifferent);
							check(moved != phased, (caption + ": viewport moves the Freeze pattern").c_str());
							// 参考函数用浮点 bounds 推导锚点（如 1.5x 下行中心 147.6667），D2D/WARP 把路径顶点吸附到定点子像素，
							// 约 1e-5 DIP 的参考侧舍入噪声可使圆点边缘跳一个子像素格（约 0.06 px，≈3–4 LSB）；相位错误则至少几十 LSB。
							check(difference.maxDelta <= 6, (caption + ": shifted Freeze pattern matches the anchor-shifted legacy reference").c_str());
						}
						// 整周期位移不改变像素（相位以 double fmod 取模）。
						const double period = 24.0 * scale;
						check(renderPhased(W, H, scale, 0x123B32, pattern, period * 5.0, -period * 3.0, staging.Get()) == phased,
							(caption + ": whole-period viewport leaves the Freeze pattern unchanged").c_str());
					}
			}

			context->ClearState();
			renderer.ReleaseResources();
			std::fprintf(stderr, "[WhiteboardPatternGpu] %s failures=%d no_hwnd=1 no_swapchain=1 device=WARP\n",
				failures ? "FAIL" : "PASS", failures);
			return failures ? 1 : 0;
		}
		catch (...) { std::fputs("[WhiteboardPatternGpu] FAIL unexpected exception\n", stderr); return 1; }
	}
}
