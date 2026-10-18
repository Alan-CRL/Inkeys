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
#include <iostream>
#include <span>
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
			if (!mapNoOverwriteOnDynamicBufferSRV ||
				m_bufferHead + batchCount > kMaxBufferCapacity)
			{
				// 不支持动态 SRV 扩展时每批丢弃；支持时仅在环形缓冲区写满后丢弃。
				mapType = D3D11_MAP_WRITE_DISCARD;
				m_bufferHead = 0;
			}

			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (FAILED(context->Map(inkDataBuffer.Get(), 0, mapType, 0, &mapped))) return -1;
			auto* destination = static_cast<InkPoint*>(mapped.pData);
			std::memcpy(destination + m_bufferHead, points.data() + startIndex, batchCount * sizeof(InkPoint)); // 把本批点写入结构化缓冲区。
			context->Unmap(inkDataBuffer.Get(), 0);

			if (FAILED(context->Map(globalCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return -1;
			auto* constants = static_cast<GlobalShaderConstants*>(mapped.pData);
			constants->width = viewportWidth;
			constants->height = viewportHeight;
			constants->color = color;
			constants->shapeType = static_cast<float>(static_cast<uint32_t>(shape));
			constants->bufferOffset = static_cast<uint32_t>(m_bufferHead); // 着色器用偏移定位当前批次的起点。
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
			m_bufferHead += batchCount;
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
			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (FAILED(context->Map(highlighterPrimitiveBuffer.Get(), 0,
				D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return -1;
			auto* destination = static_cast<HighlighterPrimitive*>(mapped.pData);
			std::memcpy(destination, primitives.data() + startIndex,
				batchCount * sizeof(HighlighterPrimitive)); // 每个 primitive 可独立批处理，不再共享易翻转的四边形端点。
			context->Unmap(highlighterPrimitiveBuffer.Get(), 0);

			if (FAILED(context->Map(globalCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return -1;
			auto* constants = static_cast<GlobalShaderConstants*>(mapped.pData);
			constants->width = viewportWidth;
			constants->height = viewportHeight;
			constants->color = color;
			constants->shapeType = 3.0f;
			constants->bufferOffset = 0; // DISCARD 在 D3D11 核心路径即可使用，不依赖动态 SRV 的 NO_OVERWRITE 扩展。
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
		ShapePrimitiveKind kind, DirectX::XMFLOAT4 color, InkOperatorKind operatorKind)
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
			if (!mapNoOverwriteOnDynamicBufferSRV ||
				m_bufferHead + pointCount > kMaxBufferCapacity)
			{
				// Shape 与普通笔共用动态 SRV，能力不足时也必须逐批从零偏移上传。
				mapType = D3D11_MAP_WRITE_DISCARD;
				m_bufferHead = 0;
			}

			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (FAILED(context->Map(inkDataBuffer.Get(), 0, mapType, 0, &mapped))) return -1;
			auto* destination = static_cast<InkPoint*>(mapped.pData);
			std::memcpy(destination + m_bufferHead, primitives.data() + startIndex,
				batchCount * sizeof(ShapePrimitive)); // 两个连续 InkPoint 槽描述一个 analytic shape。
			context->Unmap(inkDataBuffer.Get(), 0);

			if (FAILED(context->Map(globalCB.Get(), 0,
				D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return -1;
			auto* constants = static_cast<GlobalShaderConstants*>(mapped.pData);
			constants->width = viewportWidth;
			constants->height = viewportHeight;
			constants->color = color;
			constants->shapeType = static_cast<float>(static_cast<uint32_t>(kind));
			constants->bufferOffset = static_cast<uint32_t>(m_bufferHead);
			constants->operatorKind = static_cast<uint32_t>(operatorKind);
			constants->padding[0] = shapeRoundedCornerRadiusPixels_;
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
			context->OMSetBlendState(strokeOperatorBlendState.Get(), nullptr, 0xFFFFFFFF);
			context->RSSetState(rasterState.Get());
			context->Draw(static_cast<UINT>(batchCount) * 6, 0);

			ID3D11ShaderResourceView* nullResource[] = { nullptr };
			context->VSSetShaderResources(0, 1, nullResource);
			m_bufferHead += pointCount;
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
			if (length > 0) std::cout.write(line, static_cast<std::streamsize>(
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
		const float shapeType = appearance.shape == DrawingCursorShape::Circle ? 4.0f
			: appearance.shape == DrawingCursorShape::Rectangle ? 5.0f : 6.0f;
		const InkPoint cursorData[2] = {
			{ visual.x, visual.y, appearance.width * 0.5f, appearance.height * 0.5f },
			{ appearance.outlineWidth, appearance.fillAlpha, 0.0f, 0.0f }
		};

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		const HRESULT inkMapResult = context->Map(inkDataBuffer.Get(), 0,
			D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (FAILED(inkMapResult))
		{
			recordGpu("map-ink-data", inkMapResult, false);
			return;
		}
		std::memcpy(mapped.pData, cursorData, sizeof(cursorData));
		context->Unmap(inkDataBuffer.Get(), 0);
		m_bufferHead = 2;

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
		// Cursor PS 直接输出 premultiplied Add 和 Retain，复用 resolve blend 叠到 backbuffer。
		context->OMSetBlendState(operatorResolveBlendState.Get(), nullptr, 0xFFFFFFFF);
		context->RSSetState(rasterState.Get());
		recordGpu("before-draw", S_OK, true);
		context->Draw(6, 0);
		// Draw 无 HRESULT；这里只证明命令已发出，像素仍由读回诊断独立验证。
		recordGpu("after-draw-issued", S_OK, true);

		ID3D11ShaderResourceView* nullResource[] = { nullptr };
		context->VSSetShaderResources(0, 1, nullResource);
	}

}
