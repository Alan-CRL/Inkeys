module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Draw3.DiagnosticOutput.h"
#include "Draw3.EraserCursor.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <d3d11.h>
#include <DirectXMath.h>
#include <iostream>
#include <span>
#include <utility>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

module Inkeys.Drawing.Draw3.renderer;

import Inkeys.Drawing.Draw3.diagnostics;

namespace Inkeys::Drawing::Draw3
{
	// 本实现单元只负责普通笔、荧光笔和瞬态光标 primitive。
	using renderer_detail::GlobalShaderConstants;

	float ClampShapeRoundedCornerRadius(
		const ShapePrimitive& primitive, float configuredRadius) noexcept
	{
		if (!std::isfinite(configuredRadius)) return 0.0f;
		const float halfWidth = std::abs(primitive.end.x - primitive.start.x) * 0.5f;
		const float halfHeight = std::abs(primitive.end.y - primitive.start.y) * 0.5f;
		return std::clamp(configuredRadius, 0.0f, std::min(halfWidth, halfHeight));
	}

	int InkRenderer::DrawStrokeOrDot(std::span<const InkPoint> points, DirectX::XMFLOAT4 color,
		StrokeShape shape, InkOperatorKind operatorKind)
	{
		if (points.empty()) return 0;
		if (points.size() >= 2) return DrawStroke(points, color, shape, operatorKind);

		std::array<InkPoint, 2> dotPoints = {};
		// 圆角工具使用极短胶囊段生成点击圆点；平头笔由独立 primitive 路径处理。
		dotPoints[0] = points.front();
		InkPoint dotEnd = points.front();
		if (shape != StrokeShape::EraseRectSweep)
			dotEnd.x += std::max(0.25f, dotEnd.r * 0.05f);
		dotPoints[1] = dotEnd;
		return DrawStroke(dotPoints, color, shape, operatorKind);
	}

	int InkRenderer::DrawStroke(std::span<const InkPoint> points, DirectX::XMFLOAT4 color,
		StrokeShape shape, InkOperatorKind operatorKind)
	{
		const size_t totalPoints = points.size();
		if (totalPoints < 2) return 0;

		size_t startIndex = 0;
		while (startIndex < totalPoints - 1)
		{
			const size_t remaining = totalPoints - startIndex;
			const size_t batchCount = std::min(remaining, kMaxBufferCapacity);
			D3D11_MAP mapType = D3D11_MAP_WRITE_NO_OVERWRITE;
			if (useDefaultStructuredUpload_ || !mapNoOverwriteOnDynamicBufferSRV ||
				m_bufferHead + batchCount > kMaxBufferCapacity)
			{
				// 不支持动态 SRV 扩展时每批丢弃；支持时仅在环形缓冲区写满后丢弃。
				mapType = D3D11_MAP_WRITE_DISCARD;
				m_bufferHead = 0;
			}

			const auto bufferOffset = static_cast<uint32_t>(m_bufferHead);
			if (FAILED(UploadStructuredBuffer(inkDataBuffer.Get(), points.data() + startIndex,
				static_cast<UINT>(batchCount * sizeof(InkPoint)), bufferOffset * sizeof(InkPoint), mapType))) return -1;

			D3D11_MAPPED_SUBRESOURCE mapped = {};

			if (FAILED(context->Map(globalCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return -1;
			auto* constants = static_cast<GlobalShaderConstants*>(mapped.pData);
			constants->width = viewportWidth;
			constants->height = viewportHeight;
			constants->color = color;
			constants->shapeType = static_cast<float>(static_cast<uint32_t>(shape));
			constants->bufferOffset = bufferOffset; // 上传后 head 已推进，着色器仍使用本批起点。
			constants->operatorKind = static_cast<uint32_t>(operatorKind);
			context->Unmap(globalCB.Get(), 0);

			context->IASetInputLayout(nullptr);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(vertexShader.Get(), nullptr, 0);
			ID3D11ShaderResourceView* shaderResources[] = { inkDataSRV.Get() };
			context->VSSetShaderResources(0, 1, shaderResources);
			ID3D11Buffer* constantBuffers[] = { globalCB.Get() };
			context->VSSetConstantBuffers(0, 1, constantBuffers);
			context->PSSetShader(pixelShader.Get(), nullptr, 0);
			context->PSSetConstantBuffers(0, 1, constantBuffers); // operatorKind 由像素着色器读取，必须单独绑定到 PS。
			context->OMSetBlendState(strokeOperatorBlendState.Get(), nullptr, 0xFFFFFFFF); // Add 取 MAX、Retain 取 MIN，整笔覆盖率只累计一次。
			context->RSSetState(rasterState.Get());
			context->Draw((static_cast<UINT>(batchCount) - 1) * 6, 0); // 每两个相邻点生成一个形状段，段数乘 6 个顶点。

			ID3D11ShaderResourceView* nullResources[] = { nullptr };
			context->VSSetShaderResources(0, 1, nullResources);
			// 相邻批次共享一个端点，避免分段处出现断裂。
			startIndex += batchCount - 1;
		}
		return 0;
	}

	int InkRenderer::DrawHighlighterPrimitives(std::span<const HighlighterPrimitive> primitives,
		DirectX::XMFLOAT4 color, InkOperatorKind operatorKind)
	{
		if (primitives.empty()) return 0;

		size_t startIndex = 0;
		while (startIndex < primitives.size())
		{
			const size_t batchCount = std::min(primitives.size() - startIndex, kMaxBufferCapacity);
			if (FAILED(UploadStructuredBuffer(highlighterPrimitiveBuffer.Get(), primitives.data() + startIndex,
				static_cast<UINT>(batchCount * sizeof(HighlighterPrimitive)), 0, D3D11_MAP_WRITE_DISCARD))) return -1;

			D3D11_MAPPED_SUBRESOURCE mapped = {};

			if (FAILED(context->Map(globalCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return -1;
			auto* constants = static_cast<GlobalShaderConstants*>(mapped.pData);
			constants->width = viewportWidth;
			constants->height = viewportHeight;
			constants->color = color;
			constants->shapeType = 3.0f;
			constants->bufferOffset = 0; // 两种上传策略均逐批从零开始，不依赖 NO_OVERWRITE。
			constants->operatorKind = static_cast<uint32_t>(operatorKind);
			context->Unmap(globalCB.Get(), 0);

			context->IASetInputLayout(nullptr);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(vertexShader.Get(), nullptr, 0);
			ID3D11ShaderResourceView* primitiveResources[] = { highlighterPrimitiveSRV.Get() };
			context->VSSetShaderResources(3, 1, primitiveResources);
			ID3D11Buffer* constantBuffers[] = { globalCB.Get() };
			context->VSSetConstantBuffers(0, 1, constantBuffers);
			context->PSSetShader(pixelShader.Get(), nullptr, 0);
			context->PSSetConstantBuffers(0, 1, constantBuffers);
			context->OMSetBlendState(strokeOperatorBlendState.Get(), nullptr, 0xFFFFFFFF);
			context->RSSetState(rasterState.Get());
			context->Draw(static_cast<UINT>(batchCount) * 6, 0);

			ID3D11ShaderResourceView* nullResource[] = { nullptr };
			context->VSSetShaderResources(3, 1, nullResource);
			startIndex += batchCount;
		}
		return 0;
	}

	void InkRenderer::ConfigureShapePrimitives(float dpiScale) noexcept
	{
		const float scale = std::isfinite(dpiScale) ? std::max(dpiScale, 0.01f) : 1.0f;
		shapeRoundedCornerRadiusPixels_ = kShapeRoundedCornerRadiusAt96Dpi * scale;
	}

	int InkRenderer::DrawShapePrimitives(std::span<const ShapePrimitive> primitives,
		ShapePrimitiveKind kind, DirectX::XMFLOAT4 color, InkOperatorKind operatorKind,
		ID3D11BlendState* blendStateOverride, float cornerRadiusOverride)
	{
		if (primitives.empty()) return 0;
		if (!IsLineShapePrimitive(kind) && !IsRectangleShapePrimitive(kind)) return -1;
		constexpr size_t kMaximumPrimitiveBatch = kMaxBufferCapacity / 2;

		size_t startIndex = 0;
		while (startIndex < primitives.size())
		{
			const size_t batchCount = std::min(
				primitives.size() - startIndex, kMaximumPrimitiveBatch);
			const size_t pointCount = batchCount * 2;
			D3D11_MAP mapType = D3D11_MAP_WRITE_NO_OVERWRITE;
			if (useDefaultStructuredUpload_ || !mapNoOverwriteOnDynamicBufferSRV ||
				m_bufferHead + pointCount > kMaxBufferCapacity)
			{
				// Shape 与普通笔共用动态 SRV，能力不足时也必须逐批从零偏移上传。
				mapType = D3D11_MAP_WRITE_DISCARD;
				m_bufferHead = 0;
			}

			const auto bufferOffset = static_cast<uint32_t>(m_bufferHead);
			if (FAILED(UploadStructuredBuffer(inkDataBuffer.Get(), primitives.data() + startIndex,
				static_cast<UINT>(batchCount * sizeof(ShapePrimitive)), bufferOffset * sizeof(InkPoint), mapType))) return -1;

			D3D11_MAPPED_SUBRESOURCE mapped = {};

			if (FAILED(context->Map(globalCB.Get(), 0,
				D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return -1;
			auto* constants = static_cast<GlobalShaderConstants*>(mapped.pData);
			constants->width = viewportWidth;
			constants->height = viewportHeight;
			constants->color = color;
			constants->shapeType = static_cast<float>(static_cast<uint32_t>(kind));
			constants->bufferOffset = bufferOffset;
			constants->operatorKind = static_cast<uint32_t>(operatorKind);
			constants->padding[0] = cornerRadiusOverride >= 0.0f
				? cornerRadiusOverride : shapeRoundedCornerRadiusPixels_;
			constants->padding[1] = 0.0f;
			constants->padding[2] = 0.0f;
			context->Unmap(globalCB.Get(), 0);

			context->IASetInputLayout(nullptr);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(vertexShader.Get(), nullptr, 0);
			ID3D11ShaderResourceView* shaderResources[] = { inkDataSRV.Get() };
			context->VSSetShaderResources(0, 1, shaderResources);
			ID3D11Buffer* constantBuffers[] = { globalCB.Get() };
			context->VSSetConstantBuffers(0, 1, constantBuffers);
			context->PSSetShader(pixelShader.Get(), nullptr, 0);
			context->PSSetConstantBuffers(0, 1, constantBuffers);
			context->OMSetBlendState(blendStateOverride ? blendStateOverride
				: strokeOperatorBlendState.Get(), nullptr, 0xFFFFFFFF);
			context->RSSetState(rasterState.Get());
			context->Draw(static_cast<UINT>(batchCount) * 6, 0);

			ID3D11ShaderResourceView* nullResource[] = { nullptr };
			context->VSSetShaderResources(0, 1, nullResource);
			startIndex += batchCount;
		}
		return 0;
	}

	void InkRenderer::WarmUpShapeShaders() noexcept
	{
		if (!context || !layerL0.addRTV || !layerL0.retainRTV) return;
		D3D11_VIEWPORT savedViewport = {};
		UINT viewportCount = 1;
		context->RSGetViewports(&viewportCount, &savedViewport);
		const D3D11_VIEWPORT emptyViewport = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
		context->RSSetViewports(1, &emptyViewport);
		SetOperatorTarget(layerL0);
		const std::array<ShapePrimitive, 1> primitive = { ShapePrimitive{
			{ 0.0f, 0.0f, 2.5f, 0.0f }, { 16.0f, 12.0f, 0.0f, 0.0f } } };
		DrawShapePrimitives(primitive, ShapePrimitiveKind::SolidLine,
			DirectX::XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f));
		DrawShapePrimitives(primitive, ShapePrimitiveKind::DashedLine,
			DirectX::XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f));
		DrawShapePrimitives(primitive, ShapePrimitiveKind::OutlineRectangle,
			DirectX::XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f));
		DrawShapePrimitives(primitive, ShapePrimitiveKind::FilledRectangle,
			DirectX::XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f));
		context->RSSetViewports(1, &savedViewport);
	}

	void InkRenderer::InvalidateEraserCursorCache() noexcept
	{
		if (context)
		{
			ID3D11ShaderResourceView* empty[] = { nullptr };
			context->PSSetShaderResources(15, 1, empty);
		}
		std::vector<EraserCursorSprite>().swap(eraserCursorSprites_);
	}

	InkRenderer::EraserCursorSprite* InkRenderer::EnsureEraserCursorSprite(
		const DrawingCursorAppearance& appearance) noexcept
	{
		if (!device || !eraserCursorSampler_) return nullptr;
		const float drawShortSide = std::min(appearance.width, appearance.height);
		if (!std::isfinite(appearance.eraserBaseShortSide) || appearance.eraserBaseShortSide <= 0.0f ||
			!std::isfinite(appearance.eraserDpiScale) || appearance.eraserDpiScale <= 0.0f)
			return nullptr;
		try
		{
			auto found = std::find_if(eraserCursorSprites_.begin(), eraserCursorSprites_.end(),
				[&](const EraserCursorSprite& sprite) { return sprite.recipe.appearance == appearance.eraserCursor; });
			const EraserCursorRecipe recipe = ResolveEraserCursorRecipe(appearance.eraserCursor,
				appearance.eraserBaseShortSide, appearance.eraserDpiScale, drawShortSide,
				found == eraserCursorSprites_.end() ? nullptr : &found->recipe);
			if (found != eraserCursorSprites_.end() && found->recipe == recipe)
				return found->srv ? &*found : nullptr;
			if (found == eraserCursorSprites_.end())
			{
				eraserCursorSprites_.emplace_back();
				found = eraserCursorSprites_.end() - 1;
			}
			// 先登记尝试配方；失败后等外观/尺寸/DPI或设备变化，避免逐帧重复分配。
			found->texture.Reset();
			found->srv.Reset();
			found->recipe = recipe;
			found->width = found->height = 0;
			const auto raster = RasterizeEraserCursor(recipe.appearance, recipe.shortSidePx,
				ResolveEraserCursorCornerRadius(recipe.baseShortSidePx, recipe.dpiScale, recipe.shortSidePx));
			if (raster.levels.empty()) return nullptr;
			std::vector<D3D11_SUBRESOURCE_DATA> initialData(raster.levels.size());
			for (size_t level = 0; level < raster.levels.size(); ++level)
			{
				initialData[level].pSysMem = raster.levels[level].bgra.data();
				initialData[level].SysMemPitch = raster.levels[level].width * 4;
			}
			D3D11_TEXTURE2D_DESC description = {};
			description.Width = raster.levels.front().width;
			description.Height = raster.levels.front().height;
			description.MipLevels = static_cast<UINT>(raster.levels.size());
			description.ArraySize = 1;
			description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			description.SampleDesc.Count = 1;
			description.Usage = D3D11_USAGE_IMMUTABLE;
			description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
			// Win7 WARP 也直接上传完整 mip，不依赖共享设备或 GenerateMips。
			if (FAILED(device->CreateTexture2D(&description, initialData.data(), texture.GetAddressOf())) ||
				FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, srv.GetAddressOf())))
				return nullptr;
			found->width = description.Width;
			found->height = description.Height;
			found->texture = std::move(texture);
			found->srv = std::move(srv);
			return &*found;
		}
		catch (...)
		{
			return nullptr;
		}
	}

	void InkRenderer::DrawTransientDrawingCursor(const DrawingCursorVisual& visual)
	{
		if (!visual.visible) return;
		// 只在绘制线程采样首个请求及每秒一次；关闭诊断时不查询 GPU 状态或格式化日志。
		static thread_local ULONGLONG previousDiagnosticTick = 0;
		static thread_local bool diagnosticSampled = false;
		bool inspectGpu = false;
		ULONGLONG diagnosticTick = 0;
		if (StartupEnvironmentDiagnosticsEnabled())
		{
			diagnosticTick = GetTickCount64();
			inspectGpu = !diagnosticSampled || diagnosticTick - previousDiagnosticTick >= 1000;
			if (inspectGpu)
			{
				diagnosticSampled = true;
				previousDiagnosticTick = diagnosticTick;
			}
		}
		const auto recordGpu = [&](const char* stage, HRESULT result, bool inspectBindings)
		{
			if (!inspectGpu) return;
			using Microsoft::WRL::ComPtr;
			std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports = {};
			UINT viewportCount = 0;
			ComPtr<ID3D11RenderTargetView> boundRtv;
			ComPtr<ID3D11Resource> boundResource;
			ComPtr<IUnknown> targetIdentity, backBufferIdentity;
			ComPtr<ID3D11VertexShader> boundVs;
			ComPtr<ID3D11PixelShader> boundPs;
			ComPtr<ID3D11Buffer> boundVsCb, boundPsCb;
			ComPtr<ID3D11ShaderResourceView> boundSrv;
			if (inspectBindings && context)
			{
				viewportCount = static_cast<UINT>(viewports.size());
				context->RSGetViewports(&viewportCount, viewports.data());
				context->OMGetRenderTargets(1, boundRtv.GetAddressOf(), nullptr);
				if (boundRtv) boundRtv->GetResource(boundResource.GetAddressOf());
				if (boundResource) boundResource.As(&targetIdentity);
				if (backBufferTexture) backBufferTexture.As(&backBufferIdentity);
				context->VSGetShader(boundVs.GetAddressOf(), nullptr, nullptr);
				context->PSGetShader(boundPs.GetAddressOf(), nullptr, nullptr);
				context->VSGetConstantBuffers(0, 1, boundVsCb.GetAddressOf());
				context->PSGetConstantBuffers(0, 1, boundPsCb.GetAddressOf());
				context->VSGetShaderResources(0, 1, boundSrv.GetAddressOf());
			}
			char line[1536] = {};
			const int length = std::snprintf(line, sizeof(line),
				"[Draw3Diag][cursor-gpu] tick=%llu stage=%s hr=0x%08lx removed=0x%08lx "
				"resources=(ctx:%u,rtv:%u,ink:%u,cb:%u,vs:%u,ps:%u,srv:%u) "
				"bindings=%u vpCount=%u vp=(%.1f,%.1f,%.1f,%.1f,%.2f,%.2f) constants=(%.1f,%.1f) "
				"xy=(%.1f,%.1f) size=(%.1f,%.1f) opacity=%.3f "
				"rtv=%p resource=%p backbuffer=%p targetMatch=%u "
				"bound=(vs:%u,ps:%u,vsCB:%u,psCB:%u,srv:%u) match=(vs:%u,ps:%u,vsCB:%u,psCB:%u,srv:%u)\n",
				static_cast<unsigned long long>(diagnosticTick), stage, static_cast<unsigned long>(result),
				static_cast<unsigned long>(device ? device->GetDeviceRemovedReason() : E_POINTER),
				context ? 1u : 0u, backBufferRTV ? 1u : 0u, inkDataBuffer ? 1u : 0u, globalCB ? 1u : 0u,
				vertexShader ? 1u : 0u, pixelShader ? 1u : 0u, inkDataSRV ? 1u : 0u,
				inspectBindings ? 1u : 0u, viewportCount, viewports[0].TopLeftX, viewports[0].TopLeftY,
				viewports[0].Width, viewports[0].Height, viewports[0].MinDepth, viewports[0].MaxDepth,
				viewportWidth, viewportHeight, visual.x, visual.y, visual.appearance.width,
				visual.appearance.height, visual.appearance.opacity,
				static_cast<void*>(boundRtv.Get()), static_cast<void*>(boundResource.Get()),
				static_cast<void*>(backBufferTexture.Get()),
				targetIdentity && backBufferIdentity && targetIdentity.Get() == backBufferIdentity.Get() ? 1u : 0u,
				boundVs ? 1u : 0u, boundPs ? 1u : 0u, boundVsCb ? 1u : 0u,
				boundPsCb ? 1u : 0u, boundSrv ? 1u : 0u,
				boundVs && boundVs.Get() == vertexShader.Get() ? 1u : 0u,
				boundPs && boundPs.Get() == pixelShader.Get() ? 1u : 0u,
				boundVsCb && boundVsCb.Get() == globalCB.Get() ? 1u : 0u,
				boundPsCb && boundPsCb.Get() == globalCB.Get() ? 1u : 0u,
				boundSrv && boundSrv.Get() == inkDataSRV.Get() ? 1u : 0u);
			if (length > 0) DiagnosticOutput::Write(DiagnosticOutput::Stream::Output, line, static_cast<std::streamsize>(
				std::min(static_cast<size_t>(length), sizeof(line) - 1)));
		};
		if (!IsValidDrawingCursorAppearance(visual.appearance))
		{
			recordGpu("invalid-appearance", E_INVALIDARG, false);
			return;
		}
		if (!backBufferRTV || !inkDataBuffer || !globalCB)
		{
			recordGpu("missing-resources", E_POINTER, false);
			return;
		}
		const DrawingCursorAppearance& appearance = visual.appearance;
		const bool spriteCursor = appearance.shape == DrawingCursorShape::EraserSprite;
		EraserCursorSprite* sprite = spriteCursor ? EnsureEraserCursorSprite(appearance) : nullptr;
		if (spriteCursor && !sprite) return;
		const float shapeType = appearance.shape == DrawingCursorShape::Circle ? 4.0f
			: appearance.shape == DrawingCursorShape::Rectangle ? 5.0f
			: appearance.shape == DrawingCursorShape::PenIcon ? 21.0f
			: appearance.shape == DrawingCursorShape::Arrow ? 22.0f
			: spriteCursor ? 24.0f : 6.0f;
		const float spriteScale = spriteCursor
			? std::min(appearance.width, appearance.height) / sprite->recipe.shortSidePx : 1.0f;
		const float quadHalfWidth = spriteCursor ? sprite->width * 0.5f * spriteScale : appearance.width * 0.5f;
		const float quadHalfHeight = spriteCursor ? sprite->height * 0.5f * spriteScale : appearance.height * 0.5f;
		// 精灵包含透明 AA 留白，整张纹理随形状等比缩放；输入热点仍是原中心。
		const InkPoint cursorData[2] = {
			{ visual.x + appearance.centerOffsetX, visual.y + appearance.centerOffsetY,
				quadHalfWidth, quadHalfHeight },
			{ appearance.outlineWidth, appearance.fillAlpha, appearance.outlineOpacity + 1.0f, appearance.outerOutlineWidth }
		};

		const HRESULT inkUploadResult = UploadStructuredBuffer(inkDataBuffer.Get(), cursorData, sizeof(cursorData),
			0, D3D11_MAP_WRITE_DISCARD);
		if (FAILED(inkUploadResult))
		{
			recordGpu("upload-ink-data", inkUploadResult, false);
			return;
		}

		D3D11_MAPPED_SUBRESOURCE mapped = {};

		const HRESULT constantsMapResult = context->Map(globalCB.Get(), 0,
			D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (FAILED(constantsMapResult))
		{
			recordGpu("map-global-cb", constantsMapResult, false);
			return;
		}
		auto* constants = static_cast<GlobalShaderConstants*>(mapped.pData);
		constants->width = viewportWidth;
		constants->height = viewportHeight;
		constants->shapeType = shapeType;
		constants->bufferOffset = 0;
		constants->color = DirectX::XMFLOAT4(
			appearance.red, appearance.green, appearance.blue, appearance.opacity);
		constants->operatorKind = static_cast<uint32_t>(InkOperatorKind::Draw);
		constants->padding[0] = appearance.outlineRed;
		constants->padding[1] = appearance.outlineGreen;
		constants->padding[2] = appearance.outlineBlue;
		context->Unmap(globalCB.Get(), 0);

		recordGpu("upload-success", S_OK, false);

		SetOMTarget(backBufferRTV.Get());
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(vertexShader.Get(), nullptr, 0);
		ID3D11ShaderResourceView* shaderResources[] = { inkDataSRV.Get() };
		context->VSSetShaderResources(0, 1, shaderResources);
		ID3D11Buffer* constantBuffers[] = { globalCB.Get() };
		context->VSSetConstantBuffers(0, 1, constantBuffers);
		context->PSSetShader(pixelShader.Get(), nullptr, 0);
		context->PSSetConstantBuffers(0, 1, constantBuffers);
		if (spriteCursor)
		{
			ID3D11ShaderResourceView* spriteResources[] = { sprite->srv.Get() };
			ID3D11SamplerState* spriteSamplers[] = { eraserCursorSampler_.Get() };
			context->PSSetShaderResources(15, 1, spriteResources);
			context->PSSetSamplers(1, 1, spriteSamplers);
		}
		// Cursor PS 直接输出 premultiplied Add 和 Retain，复用 resolve blend 叠到 backbuffer。
		context->OMSetBlendState(operatorResolveBlendState.Get(), nullptr, 0xFFFFFFFF);
		context->RSSetState(rasterState.Get());
		recordGpu("before-draw", S_OK, true);
		context->Draw(6, 0);
		// Draw 无 HRESULT；这里只证明命令已发出，像素仍由读回诊断独立验证。
		recordGpu("after-draw-issued", S_OK, true);

		ID3D11ShaderResourceView* nullResource[] = { nullptr };
		context->VSSetShaderResources(0, 1, nullResource);
		if (spriteCursor) context->PSSetShaderResources(15, 1, nullResource);
	}

}
