module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <d3d11.h>
#include <DirectXMath.h>
#include <span>
#include <utility>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

module Inkeys.Drawing.Draw3.renderer;

import Inkeys.Drawing.Draw3.canvas_debug;

namespace Inkeys::Drawing::Draw3
{
	using renderer_detail::GlobalShaderConstants;
	using renderer_detail::TrustedL2SnapshotShaderConstants;

	// 本实现单元只服务画布调试覆盖层：板子（L2 之下）与标记标签/形状（光标之后）。
	// 全部复用既有 shape 20（矩形采样）与 shape 16..19 着色器，不新增 HLSL。

	bool InkRenderer::DebugOverlayResourcesReady() const noexcept
	{
		return debugPlateTexture_ && debugPlateSRV_ && debugLabelTexture_ && debugLabelSRV_ &&
			debugOverlayCB_ && debugOverlaySampler_ && debugUnderBlendState_;
	}

	void InkRenderer::ReleaseDebugOverlayResources() noexcept
	{
		if (context)
		{
			ID3D11ShaderResourceView* nullSnapshot[] = { nullptr };
			context->PSSetShaderResources(14, 1, nullSnapshot);
		}
		debugPlateSRV_.Reset();
		debugPlateTexture_.Reset();
		debugLabelSRV_.Reset();
		debugLabelTexture_.Reset();
		debugOverlayCB_.Reset();
		debugOverlaySampler_.Reset();
		debugUnderBlendState_.Reset();
		debugPlateWidth_ = debugPlateHeight_ = 0;
		debugLabelWidth_ = debugLabelHeight_ = debugLabelSlots_ = 0;
		debugPlateRect_ = {};
		debugPlateVisible_ = false;
	}

	bool InkRenderer::EnsureDebugOverlayResources(UINT plateWidth, UINT plateHeight,
		UINT labelWidth, UINT labelHeight, UINT labelSlots, bool* recreated) noexcept
	{
		if (recreated) *recreated = false;
		if (!device || plateWidth == 0 || plateHeight == 0 ||
			labelWidth == 0 || labelHeight == 0 || labelSlots == 0) return false;
		if (DebugOverlayResourcesReady() && debugPlateWidth_ == plateWidth &&
			debugPlateHeight_ == plateHeight && debugLabelWidth_ == labelWidth &&
			debugLabelHeight_ == labelHeight && debugLabelSlots_ == labelSlots) return true;

		ReleaseDebugOverlayResources();
		const auto createTexture = [&](UINT width, UINT height,
			Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture,
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& srv)
		{
			D3D11_TEXTURE2D_DESC description = {};
			description.Width = width;
			description.Height = height;
			description.MipLevels = 1;
			description.ArraySize = 1;
			description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			description.SampleDesc.Count = 1;
			description.Usage = D3D11_USAGE_DEFAULT;
			description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			return SUCCEEDED(device->CreateTexture2D(&description, nullptr, texture.ReleaseAndGetAddressOf())) &&
				SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, srv.ReleaseAndGetAddressOf()));
		};
		bool ok = createTexture(plateWidth, plateHeight, debugPlateTexture_, debugPlateSRV_) &&
			createTexture(labelWidth, labelHeight * labelSlots, debugLabelTexture_, debugLabelSRV_);

		if (ok)
		{
			D3D11_BUFFER_DESC bufferDescription = {};
			bufferDescription.ByteWidth = sizeof(TrustedL2SnapshotShaderConstants);
			bufferDescription.Usage = D3D11_USAGE_DYNAMIC;
			bufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			bufferDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			ok = SUCCEEDED(device->CreateBuffer(&bufferDescription, nullptr,
				debugOverlayCB_.ReleaseAndGetAddressOf()));
		}
		if (ok)
		{
			// 位图与屏幕像素 1:1，使用 point 采样避免任何边缘模糊。
			D3D11_SAMPLER_DESC samplerDescription = {};
			samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDescription.ComparisonFunc = D3D11_COMPARISON_NEVER;
			samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
			ok = SUCCEEDED(device->CreateSamplerState(&samplerDescription,
				debugOverlaySampler_.ReleaseAndGetAddressOf()));
		}
		if (ok)
		{
			// 已合成内容在 destination：板子只补在其透明度之下（预乘 alpha）。
			D3D11_BLEND_DESC blendDescription = {};
			blendDescription.RenderTarget[0].BlendEnable = TRUE;
			blendDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_INV_DEST_ALPHA;
			blendDescription.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
			blendDescription.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
			blendDescription.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_INV_DEST_ALPHA;
			blendDescription.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
			blendDescription.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			blendDescription.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			ok = SUCCEEDED(device->CreateBlendState(&blendDescription,
				debugUnderBlendState_.ReleaseAndGetAddressOf()));
		}
		if (!ok)
		{
			ReleaseDebugOverlayResources();
			return false;
		}
		debugPlateWidth_ = plateWidth;
		debugPlateHeight_ = plateHeight;
		debugLabelWidth_ = labelWidth;
		debugLabelHeight_ = labelHeight;
		debugLabelSlots_ = labelSlots;
		if (recreated) *recreated = true;
		return true;
	}

	bool InkRenderer::UploadDebugPlate(const std::uint32_t* pixels, UINT width, UINT height,
		UINT stridePixels) noexcept
	{
		if (!context || !debugPlateTexture_ || !pixels || width == 0 || height == 0 ||
			width > debugPlateWidth_ || height > debugPlateHeight_ || stridePixels < width) return false;
		const D3D11_BOX box = { 0, 0, 0, width, height, 1 };
		context->UpdateSubresource(debugPlateTexture_.Get(), 0, &box, pixels, stridePixels * 4, 0);
		return true;
	}

	bool InkRenderer::UploadDebugLabel(UINT slot, const std::uint32_t* pixels, UINT width, UINT height,
		UINT stridePixels) noexcept
	{
		if (!context || !debugLabelTexture_ || !pixels || slot >= debugLabelSlots_ || width == 0 ||
			height == 0 || width > debugLabelWidth_ || height > debugLabelHeight_ ||
			stridePixels < width) return false;
		const UINT top = slot * debugLabelHeight_;
		const D3D11_BOX box = { 0, top, 0, width, top + height, 1 };
		context->UpdateSubresource(debugLabelTexture_.Get(), 0, &box, pixels, stridePixels * 4, 0);
		return true;
	}

	void InkRenderer::SetDebugPlate(RECT screenRect, bool visible) noexcept
	{
		debugPlateRect_ = screenRect;
		debugPlateVisible_ = visible && screenRect.right > screenRect.left &&
			screenRect.bottom > screenRect.top;
	}

	// 复用 shape 20：target 为屏幕像素矩形，sourceLeft/Top 为源纹理像素原点。
	bool InkRenderer::DrawDebugTexture(ID3D11ShaderResourceView* source, UINT sourceWidth,
		UINT sourceHeight, RECT target, int sourceLeft, int sourceTop,
		ID3D11BlendState* blendState) noexcept
	{
		if (!context || !source || !blendState || !backBufferRTV || !globalCB || !vertexShader ||
			!pixelShader || !rasterState || !debugOverlayCB_ || !debugOverlaySampler_ ||
			viewportWidth <= 0.0f || viewportHeight <= 0.0f || sourceWidth == 0 || sourceHeight == 0)
			return false;
		const int width = target.right - target.left;
		const int height = target.bottom - target.top;
		if (width <= 0 || height <= 0) return false;

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (FAILED(context->Map(globalCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
		auto* global = static_cast<GlobalShaderConstants*>(mapped.pData);
		*global = {};
		global->width = viewportWidth;
		global->height = viewportHeight;
		global->shapeType = 20.0f;
		context->Unmap(globalCB.Get(), 0);

		if (FAILED(context->Map(debugOverlayCB_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
		auto* constants = static_cast<TrustedL2SnapshotShaderConstants*>(mapped.pData);
		const float inverseWidth = 1.0f / static_cast<float>(sourceWidth);
		const float inverseHeight = 1.0f / static_cast<float>(sourceHeight);
		constants->targetRect = { static_cast<float>(target.left), static_cast<float>(target.top),
			static_cast<float>(target.right), static_cast<float>(target.bottom) };
		constants->sourceUvRect = { sourceLeft * inverseWidth, sourceTop * inverseHeight,
			(sourceLeft + width) * inverseWidth, (sourceTop + height) * inverseHeight };
		constants->blurUv = { 0.0f, 0.0f, 0.0f, 0.0f }; // 零模糊：单次采样。
		context->Unmap(debugOverlayCB_.Get(), 0);

		SetOMTarget(backBufferRTV.Get());
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(vertexShader.Get(), nullptr, 0);
		ID3D11Buffer* globalBuffers[] = { globalCB.Get() };
		ID3D11Buffer* overlayBuffers[] = { debugOverlayCB_.Get() };
		context->VSSetConstantBuffers(0, 1, globalBuffers);
		context->VSSetConstantBuffers(3, 1, overlayBuffers);
		context->PSSetShader(pixelShader.Get(), nullptr, 0);
		context->PSSetConstantBuffers(0, 1, globalBuffers);
		context->PSSetConstantBuffers(3, 1, overlayBuffers);
		ID3D11ShaderResourceView* resources[] = { source };
		context->PSSetShaderResources(14, 1, resources);
		ID3D11SamplerState* samplers[] = { debugOverlaySampler_.Get() };
		context->PSSetSamplers(0, 1, samplers);
		context->OMSetBlendState(blendState, nullptr, 0xFFFFFFFF);
		context->RSSetState(rasterState.Get());
		context->Draw(6, 0);

		ID3D11ShaderResourceView* nullResources[] = { nullptr };
		ID3D11Buffer* nullBuffers[] = { nullptr };
		ID3D11SamplerState* nullSamplers[] = { nullptr };
		context->PSSetShaderResources(14, 1, nullResources);
		context->VSSetConstantBuffers(3, 1, nullBuffers);
		context->PSSetConstantBuffers(3, 1, nullBuffers);
		context->PSSetSamplers(0, 1, nullSamplers);
		return true;
	}

	bool InkRenderer::DrawDebugPlateUnder(RECT dirty) noexcept
	{
		if (!debugPlateVisible_ || !DebugOverlayResourcesReady()) return true; // 关闭/不可用时为空操作。
		RECT target = {
			std::max({ dirty.left, debugPlateRect_.left, 0L }),
			std::max({ dirty.top, debugPlateRect_.top, 0L }),
			std::min({ dirty.right, debugPlateRect_.right, static_cast<LONG>(viewportWidth) }),
			std::min({ dirty.bottom, debugPlateRect_.bottom, static_cast<LONG>(viewportHeight) }) };
		if (target.left >= target.right || target.top >= target.bottom) return true;
		return DrawDebugTexture(debugPlateSRV_.Get(), debugPlateWidth_, debugPlateHeight_, target,
			static_cast<int>(target.left - debugPlateRect_.left),
			static_cast<int>(target.top - debugPlateRect_.top), debugUnderBlendState_.Get());
	}

	bool InkRenderer::DrawDebugLabel(UINT slot, int x, int y, UINT width, UINT height) noexcept
	{
		if (!DebugOverlayResourcesReady() || slot >= debugLabelSlots_ || width == 0 || height == 0 ||
			width > debugLabelWidth_ || height > debugLabelHeight_) return false;
		RECT target = {
			std::max(x, 0), std::max(y, 0),
			std::min(x + static_cast<int>(width), static_cast<int>(viewportWidth)),
			std::min(y + static_cast<int>(height), static_cast<int>(viewportHeight)) };
		if (target.left >= target.right || target.top >= target.bottom) return true;
		return DrawDebugTexture(debugLabelSRV_.Get(), debugLabelWidth_, debugLabelHeight_ * debugLabelSlots_,
			target, target.left - x, static_cast<int>(slot * debugLabelHeight_) + (target.top - y),
			operatorResolveBlendState.Get());
	}

	int InkRenderer::DrawDebugMarkerShapes(std::span<const ShapePrimitive> primitives,
		ShapePrimitiveKind kind, DirectX::XMFLOAT4 color, float cornerRadiusPx)
	{
		if (!backBufferRTV || !operatorResolveBlendState) return -1;
		SetOMTarget(backBufferRTV.Get());
		// operatorResolveBlendState 的双源混合等价于预乘 source-over，直接叠在 backbuffer 最上层。
		return DrawShapePrimitives(primitives, kind, color, InkOperatorKind::Draw,
			operatorResolveBlendState.Get(), std::max(0.0f, cornerRadiusPx));
	}

	int RunCanvasDebugOverlayValidation() noexcept
	{
		try
		{
			int failures = 0;
			const auto check = [&](bool condition, const char* name) {
				if (!condition) { ++failures; std::fprintf(stderr, "[CanvasDebugGpu] FAIL %s\n", name); }
				return condition;
			};
			Microsoft::WRL::ComPtr<ID3D11Device> device;
			Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
			constexpr D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
			D3D_FEATURE_LEVEL actualLevel = {};
			if (!check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
				D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
				device.GetAddressOf(), &actualLevel, context.GetAddressOf())), "offscreen WARP device")) return 1;

			constexpr UINT W = 480, H = 320;
			InkRenderer renderer;
			renderer.device = device;
			renderer.context = context;
			if (!check(renderer.CreateFixedResources(D3D_DRIVER_TYPE_WARP), "production fixed resources")) return 1;
			D3D11_TEXTURE2D_DESC description = {};
			description.Width = W;
			description.Height = H;
			description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
			description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			description.Usage = D3D11_USAGE_DEFAULT;
			description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			if (!check(SUCCEEDED(device->CreateTexture2D(&description, nullptr, renderer.backBufferTexture.GetAddressOf())) &&
				SUCCEEDED(device->CreateRenderTargetView(renderer.backBufferTexture.Get(), nullptr, renderer.backBufferRTV.GetAddressOf())) &&
				SUCCEEDED(device->CreateTexture2D(&description, nullptr, renderer.layerL2Texture.GetAddressOf())) &&
				renderer.CreateOperatorLayerResources(W, H, renderer.layerL1) &&
				renderer.CreateOperatorLayerResources(W, H, renderer.layerL0), "offscreen layers")) return 1;
			renderer.SetScreenSize(static_cast<float>(W), static_cast<float>(H));
			renderer.ClearOperatorLayer(renderer.layerL1);
			renderer.ClearOperatorLayer(renderer.layerL0);

			// 调试板（CPU 栅格化来自与生产相同的 Overlay）。
			CanvasDebug::Overlay overlay;
			CanvasDebug::FrameParams params;
			params.nowMs = 1000.0;
			params.canvasWidth = static_cast<int>(W);
			params.canvasHeight = static_cast<int>(H);
			params.dpiScale = 1.0f;
			params.targetFps = 60.0;
			overlay.Update(params);
			const RECT plate = overlay.PlateRect();
			if (!check(overlay.PlateVisible() && plate.right <= static_cast<LONG>(W) && plate.bottom <= static_cast<LONG>(H),
				"plate fits the test canvas")) return 1;
			const int top = plate.top;

			// L2：板内不透明绿块、半透明红块；板外不透明蓝块；其余透明。
			std::vector<std::uint32_t> l2(static_cast<size_t>(W) * H, 0u);
			const auto fill = [&](int x0, int y0, int x1, int y1, std::uint32_t value) {
				for (int y = y0; y < y1; ++y)
					for (int x = x0; x < x1; ++x) l2[static_cast<size_t>(y) * W + x] = value;
			};
			fill(20, top + 20, 60, top + 60, 0xFF00C800u);
			fill(100, top + 20, 140, top + 60, 0x80800000u);
			fill(300, 10, 340, 50, 0xFF0000FFu);
			context->UpdateSubresource(renderer.layerL2Texture.Get(), 0, nullptr, l2.data(), W * 4, 0);

			description.Usage = D3D11_USAGE_STAGING;
			description.BindFlags = 0;
			description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
			if (!check(SUCCEEDED(device->CreateTexture2D(&description, nullptr, staging.GetAddressOf())), "staging texture")) return 1;
			const auto readBack = [&]() {
				std::vector<std::uint32_t> out(static_cast<size_t>(W) * H, 0u);
				context->OMSetRenderTargets(0, nullptr, nullptr);
				context->CopyResource(staging.Get(), renderer.backBufferTexture.Get());
				D3D11_MAPPED_SUBRESOURCE mapped = {};
				if (!check(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "readback map")) return out;
				for (UINT y = 0; y < H; ++y)
					std::memcpy(out.data() + static_cast<size_t>(y) * W,
						static_cast<const unsigned char*>(mapped.pData) + y * mapped.RowPitch, W * 4);
				context->Unmap(staging.Get(), 0);
				return out;
			};
			// 与 DrawingController::CompositeLayersToBackBuffer 完全相同的调用顺序。
			const auto composite = [&](RECT dirty) {
				context->OMSetRenderTargets(0, nullptr, nullptr);
				renderer.CopyResource(renderer.backBufferTexture.Get(), renderer.layerL2Texture.Get(), dirty);
				const bool applied = renderer.ApplyOperatorLayers(renderer.backBufferRTV.Get(),
					renderer.layerL1, renderer.layerL0, dirty);
				if (applied) (void)renderer.DrawDebugPlateUnder(dirty);
				return applied;
			};
			const RECT full = { 0, 0, static_cast<LONG>(W), static_cast<LONG>(H) };
			const auto at = [&](const std::vector<std::uint32_t>& pixels, int x, int y) {
				return pixels[static_cast<size_t>(y) * W + x];
			};
			const auto closeTo = [](std::uint32_t a, std::uint32_t b, int tolerance) {
				for (int shift = 0; shift < 32; shift += 8)
					if (std::abs(static_cast<int>((a >> shift) & 0xFF) - static_cast<int>((b >> shift) & 0xFF)) > tolerance) return false;
				return true;
			};

			// 1. 关闭：不设置板子，合成结果就是 L2（单位 L1/L0），并作为字节基线。
			check(composite(full), "baseline composite");
			const std::vector<std::uint32_t> baseline = readBack();
			check(baseline == l2, "disabled composite equals L2 exactly");

			// 2. 开启：资源就绪、上传、整屏合成。
			bool recreated = false;
			check(renderer.EnsureDebugOverlayResources(static_cast<UINT>(overlay.PlateCapacityWidth()),
				static_cast<UINT>(overlay.PlateCapacityHeight()), static_cast<UINT>(overlay.LabelTileWidth()),
				static_cast<UINT>(overlay.LabelTileHeight()), static_cast<UINT>(CanvasDebug::kMaxContacts), &recreated) &&
				recreated, "overlay resources are created");
			bool again = true;
			check(renderer.EnsureDebugOverlayResources(static_cast<UINT>(overlay.PlateCapacityWidth()),
				static_cast<UINT>(overlay.PlateCapacityHeight()), static_cast<UINT>(overlay.LabelTileWidth()),
				static_cast<UINT>(overlay.LabelTileHeight()), static_cast<UINT>(CanvasDebug::kMaxContacts), &again) &&
				!again, "overlay resources are reused when nothing changed");
			check(renderer.UploadDebugPlate(overlay.PlatePixels(), static_cast<UINT>(overlay.PlateWidth()),
				static_cast<UINT>(overlay.PlateHeight()), static_cast<UINT>(overlay.PlateCapacityWidth())), "plate upload");
			renderer.SetDebugPlate(plate, true);
			check(composite(full), "plate composite");
			const std::vector<std::uint32_t> withPlate = readBack();
			const std::uint32_t* plateBits = overlay.PlatePixels();
			const int plateStride = overlay.PlateCapacityWidth();
			bool outsideSame = true;
			for (int y = 0; y < static_cast<int>(H); ++y)
				for (int x = 0; x < static_cast<int>(W); ++x)
				{
					const bool inside = x >= plate.left && x < plate.right && y >= plate.top && y < plate.bottom;
					if (!inside && at(withPlate, x, y) != at(baseline, x, y)) outsideSame = false;
				}
			check(outsideSame, "pixels outside the plate rectangle are byte-identical to the baseline");
			// 板内透明 L2：显示板子颜色（1:1、无偏移）；取左缘（直角）和中部几处。
			bool plateShown = true;
			for (const auto& point : { std::pair<int, int>{ 1, 5 }, { 80, 70 }, { 150, 100 }, { 5, plate.bottom - top - 4 } })
			{
				const std::uint32_t expected = plateBits[point.second * plateStride + point.first];
				if (l2[static_cast<size_t>(top + point.second) * W + point.first] == 0 &&
					!closeTo(at(withPlate, point.first, top + point.second), expected, 1)) plateShown = false;
			}
			check(plateShown, "plate colour shows where L2 is transparent");
			check(at(withPlate, 1, top + 5) != 0u, "plate is not empty");
			check(at(withPlate, 40, top + 40) == 0xFF00C800u, "opaque L2 pixels stay on top of the plate");
			// 半透明 L2：plate 只补在其透明度之下：out = plate * (1 - a) + dst。
			{
				const std::uint32_t below = plateBits[40 * plateStride + 120];
				const std::uint32_t destination = 0x80800000u;
				std::uint32_t expected = 0;
				for (int shift = 0; shift < 32; shift += 8)
				{
					const int value = static_cast<int>(std::lround(((below >> shift) & 0xFF) * (255 - 128) / 255.0)) +
						static_cast<int>((destination >> shift) & 0xFF);
					expected |= static_cast<std::uint32_t>(std::min(value, 255)) << shift;
				}
				check(closeTo(at(withPlate, 120, top + 40), expected, 2), "translucent L2 composites over the plate (plate under L2)");
			}

			// 3. 局部脏区只改 dirty ∩ 板子矩形，其余板内像素保持。
			renderer.SetDebugPlate({}, false);
			check(composite(full), "reset to baseline");
			renderer.SetDebugPlate(plate, true);
			const RECT partial = { 10, top + 10, 110, top + 30 };
			check(composite(partial), "partial plate composite");
			const std::vector<std::uint32_t> partialPixels = readBack();
			bool partialOk = true;
			for (int y = 0; y < static_cast<int>(H); ++y)
				for (int x = 0; x < static_cast<int>(W); ++x)
				{
					const bool inPartial = x >= partial.left && x < partial.right && y >= partial.top && y < partial.bottom;
					if (!inPartial && at(partialPixels, x, y) != at(baseline, x, y)) partialOk = false;
				}
			check(partialOk, "a partial dirty rectangle only touches its own pixels");
			check(at(partialPixels, 12, top + 12) == plateBits[12 * plateStride + 12], "partial dirty draws the matching plate texels");

			// 4. L1 橡皮擦除 L2 墨迹后露出的是板子（板在全部墨迹层之下），关闭时则是透明。
			renderer.SetOperatorTarget(renderer.layerL1);
			const std::array<InkPoint, 2> erase = { InkPoint{ 40.0f, static_cast<float>(top + 40), 30.0f, 0.0f },
				InkPoint{ 42.0f, static_cast<float>(top + 40), 30.0f, 1.0f } };
			check(renderer.DrawStroke(erase, DirectX::XMFLOAT4(0, 0, 0, 1), StrokeShape::RoundCapsule, InkOperatorKind::Erase) == 0,
				"eraser stroke on L1");
			renderer.SetDebugPlate(plate, true);
			check(composite(full), "composite with eraser and plate");
			const std::vector<std::uint32_t> erasedOn = readBack();
			check(closeTo(at(erasedOn, 40, top + 40), plateBits[40 * plateStride + 40], 2), "an erased stroke reveals the plate, not transparency");
			renderer.SetDebugPlate({}, false);
			check(composite(full), "composite with eraser, plate off");
			const std::vector<std::uint32_t> erasedOff = readBack();
			check(at(erasedOff, 40, top + 40) == 0u, "plate off: the erased area is transparent");

			// 5. 关闭后与基线字节一致（清掉橡皮层后）。
			renderer.ClearOperatorLayer(renderer.layerL1);
			renderer.SetDebugPlate({}, false);
			check(composite(full), "plate off composite");
			check(readBack() == baseline, "disabled composite is byte-identical after a debug session");

			// 6. 标记原语：标签（over）、虚线/实线、圆点与圆框。
			renderer.ClearRTV(renderer.backBufferRTV.Get(), kTransparentLayerClearColor);
			context->OMSetRenderTargets(0, nullptr, nullptr);
			renderer.CopyResource(renderer.backBufferTexture.Get(), renderer.layerL2Texture.Get(), full);
			std::vector<std::uint32_t> tile(static_cast<size_t>(overlay.LabelTileWidth()) * overlay.LabelTileHeight(), 0x80000000u);
			check(renderer.UploadDebugLabel(1, tile.data(), 10, 10, static_cast<UINT>(overlay.LabelTileWidth())), "label upload");
			check(renderer.DrawDebugLabel(1, 310, 20, 10, 10), "label draw");
			constexpr DirectX::XMFLOAT4 kRed = { 1.0f, 0.0f, 0.0f, 1.0f };
			const std::array<ShapePrimitive, 1> dashed = { ShapePrimitive{ { 20, 300, 1, 0 }, { 220, 300, 0, 0 } } };
			check(renderer.DrawDebugMarkerShapes(dashed, ShapePrimitiveKind::DashedLine, kRed, 0.0f) == 0, "dashed line");
			const std::array<ShapePrimitive, 1> solid = { ShapePrimitive{ { 20, 280, 1, 0 }, { 220, 280, 0, 0 } } };
			check(renderer.DrawDebugMarkerShapes(solid, ShapePrimitiveKind::SolidLine, kRed, 0.0f) == 0, "solid line");
			const std::array<ShapePrimitive, 1> box = { ShapePrimitive{ { 350, 200, 1, 0 }, { 430, 260, 0, 0 } } };
			check(renderer.DrawDebugMarkerShapes(box, ShapePrimitiveKind::OutlineRectangle, kRed, 0.0f) == 0, "outline rectangle");
			DrawingCursorVisual visual;
			visual.visible = true;
			visual.x = 400.0f;
			visual.y = 100.0f;
			DrawingCursorAppearance& appearance = visual.appearance;
			appearance.shape = DrawingCursorShape::Circle;
			appearance.red = appearance.outlineRed = 0.0f;
			appearance.green = appearance.outlineGreen = 0.90f;
			appearance.blue = appearance.outlineBlue = 0.46f;
			appearance.opacity = 1.0f;
			appearance.width = appearance.height = 40.0f;
			appearance.fillAlpha = 0.0f;
			appearance.outlineWidth = 2.0f;
			renderer.DrawTransientDrawingCursor(visual);
			const std::vector<std::uint32_t> ringOnly = readBack();
			appearance.width = appearance.height = 12.0f;
			appearance.fillAlpha = 1.0f;
			appearance.outlineWidth = 0.0f;
			renderer.DrawTransientDrawingCursor(visual);
			const std::vector<std::uint32_t> markers = readBack();

			const auto alpha = [](std::uint32_t value) { return static_cast<int>(value >> 24); };
			// 蓝块上的半透明黑标签：B=255*(127/255)=127，A=255。
			check(closeTo(at(markers, 315, 25), 0xFF00007Fu, 2), "label composites over the content below");
			check(at(markers, 330, 25) == 0xFF0000FFu, "pixels beside the label are untouched");
			check(alpha(at(markers, 24, 300)) > 200 && alpha(at(markers, 34, 300)) < 10 && alpha(at(markers, 44, 300)) > 200,
				"dashed line alternates dash and gap");
			check(alpha(at(markers, 24, 280)) > 200 && alpha(at(markers, 34, 280)) > 200, "solid line has no gap");
			check(((at(markers, 24, 300) >> 16) & 0xFF) > 200 && (at(markers, 24, 300) & 0xFF) < 10, "marker lines are red");
			check(alpha(at(markers, 390, 200)) > 200 && alpha(at(markers, 390, 230)) < 10, "outline rectangle draws only its border");
			check(alpha(at(ringOnly, 400, 100)) < 10 && alpha(at(ringOnly, 419, 100)) > 200,
				"the ring has no fill and an outline");
			check(alpha(at(markers, 400, 100)) > 240 && ((at(markers, 400, 100) >> 8) & 0xFF) > 200 &&
				((at(markers, 400, 100) >> 16) & 0xFF) < 10, "the dot is a filled green circle");

			context->ClearState();
			renderer.ReleaseResources();
			std::fprintf(stderr, "[CanvasDebugGpu] %s failures=%d no_hwnd=1 no_swapchain=1\n", failures ? "FAIL" : "PASS", failures);
			return failures ? 1 : 0;
		}
		catch (...) { std::fputs("[CanvasDebugGpu] FAIL unexpected exception\n", stderr); return 1; }
	}
}
