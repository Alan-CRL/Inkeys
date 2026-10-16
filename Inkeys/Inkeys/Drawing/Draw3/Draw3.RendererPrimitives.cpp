module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Draw3.Performance.h"

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
			if (useDefaultStructuredUpload_ || !mapNoOverwriteOnDynamicBufferSRV ||
				m_bufferHead + batchCount > kMaxBufferCapacity)
			{
				// 不支持动态 SRV 扩展时每批丢弃；支持时仅在环形缓冲区写满后丢弃。
				mapType = D3D11_MAP_WRITE_DISCARD;
				m_bufferHead = 0;
			}

			const auto bufferOffset = static_cast<uint32_t>(m_bufferHead);
			if (FAILED(UploadStructuredBuffer(inkDataBuffer.Get(), points.data() + startIndex,
				static_cast<UINT>(batchCount * sizeof(InkPoint)), bufferOffset * sizeof(InkPoint),
				mapType, PerformanceGraphicsRole::InkData,
				PerformanceGraphicsSite("draw_stroke_map_ink_data"), PerformanceGraphicsSite("draw_stroke_memcpy_ink_data"),
				PerformanceGraphicsSite("draw_stroke_unmap_1"), PerformanceGraphicsSite("draw_stroke_update_ink_data")))) return -1;

			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (FAILED(PerformanceMap(performance_, context.Get(), globalCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped, 0, PerformanceGraphicsRole::GlobalConstants,
			PerformanceGraphicsSite("draw_stroke_map_global_constants"), performancePass_))) return -1;
			auto* constants = static_cast<GlobalShaderConstants*>(mapped.pData);
			// 单独计量原有逐字段写入；不更改生产写法或补写 padding。
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::WriteConstant, PerformanceGraphicsRole::GlobalConstants,
				PerformanceGraphicsSite("draw_stroke_field_write_global_constants"), [&] { return TraceArguments(globalCB.Get(), 36); }, [&] {
				constants->width = viewportWidth;
				constants->height = viewportHeight;
				constants->color = color;
				constants->shapeType = static_cast<float>(static_cast<uint32_t>(shape));
				constants->bufferOffset = bufferOffset; // 上传后 head 已推进，着色器仍使用本批起点。
				constants->operatorKind = static_cast<uint32_t>(operatorKind);
			});
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Unmap, PerformanceGraphicsRole::GlobalConstants,
			PerformanceGraphicsSite("draw_stroke_unmap_2"), [&] { return TraceArguments(globalCB.Get(), 0, 0); }, [&] { context->Unmap(globalCB.Get(), 0); });

			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw_stroke_i_a_set_input_layout_1"), [&] { return TraceArguments(); }, [&] { context->IASetInputLayout(nullptr); });
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw_stroke_i_a_set_primitive_topology_1"), [&] { return TraceArguments(); }, [&] { context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); });
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw_stroke_v_s_set_shader_1"), [&] { return TraceArguments(); }, [&] { context->VSSetShader(vertexShader.Get(), nullptr, 0); });
			ID3D11ShaderResourceView* shaderResources[] = { inkDataSRV.Get() };
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::LayerTexture,
			PerformanceGraphicsSite("draw_stroke_v_s_set_shader_resources_1"), [&] { return TraceArguments(TraceViewResource((shaderResources)[0]), 0, 0, nullptr, 0, 1); }, [&] { context->VSSetShaderResources(0, 1, shaderResources); });
			ID3D11Buffer* constantBuffers[] = { globalCB.Get() };
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::GlobalConstants,
			PerformanceGraphicsSite("draw_stroke_v_s_set_constant_buffers_1"), [&] { return TraceArguments((constantBuffers)[0], 0, 0, nullptr, 0, 1); }, [&] { context->VSSetConstantBuffers(0, 1, constantBuffers); });
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw_stroke_p_s_set_shader_1"), [&] { return TraceArguments(); }, [&] { context->PSSetShader(pixelShader.Get(), nullptr, 0); });
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::GlobalConstants,
			PerformanceGraphicsSite("draw_stroke_p_s_set_constant_buffers_1"), [&] { return TraceArguments((constantBuffers)[0], 0, 0, nullptr, 0, 1); }, [&] { context->PSSetConstantBuffers(0, 1, constantBuffers); }); // operatorKind 由像素着色器读取，必须单独绑定到 PS。
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw_stroke_o_m_set_blend_state_1"), [&] { return TraceArguments(); }, [&] { context->OMSetBlendState(strokeOperatorBlendState.Get(), nullptr, 0xFFFFFFFF); }); // Add 取 MAX、Retain 取 MIN，整笔覆盖率只累计一次。
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw_stroke_r_s_set_state_1"), [&] { return TraceArguments(); }, [&] { context->RSSetState(rasterState.Get()); });
			PerformanceCall(performance_, PerformanceStage::DrawSubmit, [&] { PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Draw, PerformanceGraphicsRole::InkData,
			PerformanceGraphicsSite("draw_stroke_draw_1"), [&] { return TraceArguments(inkDataBuffer.Get(), 0, 0, nullptr, (static_cast<UINT>(batchCount) - 1) * 6, 1); }, [&] { context->Draw((static_cast<UINT>(batchCount) - 1) * 6, 0); }); });
			if (performance_) performance_->Draw(); // 每两个相邻点生成一个形状段，段数乘 6 个顶点。

			ID3D11ShaderResourceView* nullResources[] = { nullptr };
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::LayerTexture,
			PerformanceGraphicsSite("draw_stroke_v_s_set_shader_resources_2"), [&] { return TraceArguments(TraceViewResource((nullResources)[0]), 0, 0, nullptr, 0, 1); }, [&] { context->VSSetShaderResources(0, 1, nullResources); });
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
				static_cast<UINT>(batchCount * sizeof(HighlighterPrimitive)), 0, D3D11_MAP_WRITE_DISCARD,
				PerformanceGraphicsRole::HighlighterData,
				PerformanceGraphicsSite("draw_highlighter_primitives_map_highlighter_data"), PerformanceGraphicsSite("draw_highlighter_primitives_memcpy_highlighter_data"),
				PerformanceGraphicsSite("draw_highlighter_primitives_unmap_1"), PerformanceGraphicsSite("draw_highlighter_primitives_update_highlighter_data")))) return -1;

			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (FAILED(PerformanceMap(performance_, context.Get(), globalCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped, 0, PerformanceGraphicsRole::GlobalConstants,
			PerformanceGraphicsSite("draw_highlighter_primitives_map_global_constants"), performancePass_))) return -1;
			auto* constants = static_cast<GlobalShaderConstants*>(mapped.pData);
			// 单独计量原有逐字段写入；不更改生产写法或补写 padding。
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::WriteConstant, PerformanceGraphicsRole::GlobalConstants,
				PerformanceGraphicsSite("draw_highlighter_primitives_field_write_global_constants"), [&] { return TraceArguments(globalCB.Get(), 36); }, [&] {
				constants->width = viewportWidth;
				constants->height = viewportHeight;
				constants->color = color;
				constants->shapeType = 3.0f;
				constants->bufferOffset = 0; // 两种上传策略均逐批从零开始，不依赖 NO_OVERWRITE。
				constants->operatorKind = static_cast<uint32_t>(operatorKind);
			});
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Unmap, PerformanceGraphicsRole::GlobalConstants,
			PerformanceGraphicsSite("draw_highlighter_primitives_unmap_2"), [&] { return TraceArguments(globalCB.Get(), 0, 0); }, [&] { context->Unmap(globalCB.Get(), 0); });

			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw_highlighter_primitives_i_a_set_input_layout_1"), [&] { return TraceArguments(); }, [&] { context->IASetInputLayout(nullptr); });
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw_highlighter_primitives_i_a_set_primitive_topology_1"), [&] { return TraceArguments(); }, [&] { context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); });
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw_highlighter_primitives_v_s_set_shader_1"), [&] { return TraceArguments(); }, [&] { context->VSSetShader(vertexShader.Get(), nullptr, 0); });
			ID3D11ShaderResourceView* primitiveResources[] = { highlighterPrimitiveSRV.Get() };
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::LayerTexture,
			PerformanceGraphicsSite("draw_highlighter_primitives_v_s_set_shader_resources_1"), [&] { return TraceArguments(TraceViewResource((primitiveResources)[0]), 0, 0, nullptr, 3, 1); }, [&] { context->VSSetShaderResources(3, 1, primitiveResources); });
			ID3D11Buffer* constantBuffers[] = { globalCB.Get() };
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::GlobalConstants,
			PerformanceGraphicsSite("draw_highlighter_primitives_v_s_set_constant_buffers_1"), [&] { return TraceArguments((constantBuffers)[0], 0, 0, nullptr, 0, 1); }, [&] { context->VSSetConstantBuffers(0, 1, constantBuffers); });
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw_highlighter_primitives_p_s_set_shader_1"), [&] { return TraceArguments(); }, [&] { context->PSSetShader(pixelShader.Get(), nullptr, 0); });
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::GlobalConstants,
			PerformanceGraphicsSite("draw_highlighter_primitives_p_s_set_constant_buffers_1"), [&] { return TraceArguments((constantBuffers)[0], 0, 0, nullptr, 0, 1); }, [&] { context->PSSetConstantBuffers(0, 1, constantBuffers); });
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw_highlighter_primitives_o_m_set_blend_state_1"), [&] { return TraceArguments(); }, [&] { context->OMSetBlendState(strokeOperatorBlendState.Get(), nullptr, 0xFFFFFFFF); });
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw_highlighter_primitives_r_s_set_state_1"), [&] { return TraceArguments(); }, [&] { context->RSSetState(rasterState.Get()); });
			PerformanceCall(performance_, PerformanceStage::DrawSubmit, [&] { PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Draw, PerformanceGraphicsRole::HighlighterData,
			PerformanceGraphicsSite("draw_highlighter_primitives_draw_1"), [&] { return TraceArguments(highlighterPrimitiveBuffer.Get(), 0, 0, nullptr, static_cast<UINT>(batchCount) * 6, 1); }, [&] { context->Draw(static_cast<UINT>(batchCount) * 6, 0); }); });
			if (performance_) performance_->Draw();

			ID3D11ShaderResourceView* nullResource[] = { nullptr };
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::LayerTexture,
			PerformanceGraphicsSite("draw_highlighter_primitives_v_s_set_shader_resources_2"), [&] { return TraceArguments(TraceViewResource((nullResource)[0]), 0, 0, nullptr, 3, 1); }, [&] { context->VSSetShaderResources(3, 1, nullResource); });
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
			if (useDefaultStructuredUpload_ || !mapNoOverwriteOnDynamicBufferSRV ||
				m_bufferHead + pointCount > kMaxBufferCapacity)
			{
				// Shape 与普通笔共用动态 SRV，能力不足时也必须逐批从零偏移上传。
				mapType = D3D11_MAP_WRITE_DISCARD;
				m_bufferHead = 0;
			}

			const auto bufferOffset = static_cast<uint32_t>(m_bufferHead);
			if (FAILED(UploadStructuredBuffer(inkDataBuffer.Get(), primitives.data() + startIndex,
				static_cast<UINT>(batchCount * sizeof(ShapePrimitive)), bufferOffset * sizeof(InkPoint),
				mapType, PerformanceGraphicsRole::InkData,
				PerformanceGraphicsSite("draw_shape_primitives_map_ink_data"), PerformanceGraphicsSite("draw_shape_primitives_memcpy_ink_data"),
				PerformanceGraphicsSite("draw_shape_primitives_unmap_1"), PerformanceGraphicsSite("draw_shape_primitives_update_ink_data")))) return -1;

			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (FAILED(PerformanceMap(performance_, context.Get(), globalCB.Get(), 0,
				D3D11_MAP_WRITE_DISCARD, 0, &mapped, 0, PerformanceGraphicsRole::GlobalConstants,
			PerformanceGraphicsSite("draw_shape_primitives_map_global_constants"), performancePass_))) return -1;
			auto* constants = static_cast<GlobalShaderConstants*>(mapped.pData);
			// 单独计量原有逐字段写入；不更改生产写法或补写 padding。
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::WriteConstant, PerformanceGraphicsRole::GlobalConstants,
				PerformanceGraphicsSite("draw_shape_primitives_field_write_global_constants"), [&] { return TraceArguments(globalCB.Get(), 48); }, [&] {
				constants->width = viewportWidth;
				constants->height = viewportHeight;
				constants->color = color;
				constants->shapeType = static_cast<float>(static_cast<uint32_t>(kind));
				constants->bufferOffset = bufferOffset;
				constants->operatorKind = static_cast<uint32_t>(operatorKind);
				constants->padding[0] = shapeRoundedCornerRadiusPixels_;
				constants->padding[1] = 0.0f;
				constants->padding[2] = 0.0f;
			});
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Unmap, PerformanceGraphicsRole::GlobalConstants,
			PerformanceGraphicsSite("draw_shape_primitives_unmap_2"), [&] { return TraceArguments(globalCB.Get(), 0, 0); }, [&] { context->Unmap(globalCB.Get(), 0); });

			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw_shape_primitives_i_a_set_input_layout_1"), [&] { return TraceArguments(); }, [&] { context->IASetInputLayout(nullptr); });
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw_shape_primitives_i_a_set_primitive_topology_1"), [&] { return TraceArguments(); }, [&] { context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); });
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw_shape_primitives_v_s_set_shader_1"), [&] { return TraceArguments(); }, [&] { context->VSSetShader(vertexShader.Get(), nullptr, 0); });
			ID3D11ShaderResourceView* shaderResources[] = { inkDataSRV.Get() };
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::LayerTexture,
			PerformanceGraphicsSite("draw_shape_primitives_v_s_set_shader_resources_1"), [&] { return TraceArguments(TraceViewResource((shaderResources)[0]), 0, 0, nullptr, 0, 1); }, [&] { context->VSSetShaderResources(0, 1, shaderResources); });
			ID3D11Buffer* constantBuffers[] = { globalCB.Get() };
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::GlobalConstants,
			PerformanceGraphicsSite("draw_shape_primitives_v_s_set_constant_buffers_1"), [&] { return TraceArguments((constantBuffers)[0], 0, 0, nullptr, 0, 1); }, [&] { context->VSSetConstantBuffers(0, 1, constantBuffers); });
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw_shape_primitives_p_s_set_shader_1"), [&] { return TraceArguments(); }, [&] { context->PSSetShader(pixelShader.Get(), nullptr, 0); });
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::GlobalConstants,
			PerformanceGraphicsSite("draw_shape_primitives_p_s_set_constant_buffers_1"), [&] { return TraceArguments((constantBuffers)[0], 0, 0, nullptr, 0, 1); }, [&] { context->PSSetConstantBuffers(0, 1, constantBuffers); });
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw_shape_primitives_o_m_set_blend_state_1"), [&] { return TraceArguments(); }, [&] { context->OMSetBlendState(strokeOperatorBlendState.Get(), nullptr, 0xFFFFFFFF); });
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw_shape_primitives_r_s_set_state_1"), [&] { return TraceArguments(); }, [&] { context->RSSetState(rasterState.Get()); });
			PerformanceCall(performance_, PerformanceStage::DrawSubmit, [&] { PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Draw, PerformanceGraphicsRole::InkData,
			PerformanceGraphicsSite("draw_shape_primitives_draw_1"), [&] { return TraceArguments(inkDataBuffer.Get(), 0, 0, nullptr, static_cast<UINT>(batchCount) * 6, 1); }, [&] { context->Draw(static_cast<UINT>(batchCount) * 6, 0); }); });
			if (performance_) performance_->Draw();

			ID3D11ShaderResourceView* nullResource[] = { nullptr };
			PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::LayerTexture,
			PerformanceGraphicsSite("draw_shape_primitives_v_s_set_shader_resources_2"), [&] { return TraceArguments(TraceViewResource((nullResource)[0]), 0, 0, nullptr, 0, 1); }, [&] { context->VSSetShaderResources(0, 1, nullResource); });
			startIndex += batchCount;
		}
		return 0;
	}

	void InkRenderer::WarmUpShapeShaders() noexcept
	{
		if (!context || !layerL0.addRTV || !layerL0.retainRTV) return;
		D3D11_VIEWPORT savedViewport = {};
		UINT viewportCount = 1;
		PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Other, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("warm_up_shape_shaders_r_s_get_viewports_1"), [&] { return TraceArguments(); }, [&] { context->RSGetViewports(&viewportCount, &savedViewport); });
		const D3D11_VIEWPORT emptyViewport = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
		PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("warm_up_shape_shaders_r_s_set_viewports_1"), [&] { return TraceArguments(); }, [&] { context->RSSetViewports(1, &emptyViewport); });
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
		PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("warm_up_shape_shaders_r_s_set_viewports_2"), [&] { return TraceArguments(); }, [&] { context->RSSetViewports(1, &savedViewport); });
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
				PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Other, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw_transient_drawing_cursor_r_s_get_viewports_1"), [&] { return TraceArguments(); }, [&] { context->RSGetViewports(&viewportCount, viewports.data()); });
				PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Other, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw_transient_drawing_cursor_o_m_get_render_targets_1"), [&] { return TraceArguments(); }, [&] { context->OMGetRenderTargets(1, boundRtv.GetAddressOf(), nullptr); });
				if (boundRtv) boundRtv->GetResource(boundResource.GetAddressOf());
				if (boundResource) boundResource.As(&targetIdentity);
				if (backBufferTexture) backBufferTexture.As(&backBufferIdentity);
				PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Other, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw_transient_drawing_cursor_v_s_get_shader_1"), [&] { return TraceArguments(); }, [&] { context->VSGetShader(boundVs.GetAddressOf(), nullptr, nullptr); });
				PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Other, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw_transient_drawing_cursor_p_s_get_shader_1"), [&] { return TraceArguments(); }, [&] { context->PSGetShader(boundPs.GetAddressOf(), nullptr, nullptr); });
				PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Other, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw_transient_drawing_cursor_v_s_get_constant_buffers_1"), [&] { return TraceArguments(); }, [&] { context->VSGetConstantBuffers(0, 1, boundVsCb.GetAddressOf()); });
				PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Other, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw_transient_drawing_cursor_p_s_get_constant_buffers_1"), [&] { return TraceArguments(); }, [&] { context->PSGetConstantBuffers(0, 1, boundPsCb.GetAddressOf()); });
				PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Other, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw_transient_drawing_cursor_v_s_get_shader_resources_1"), [&] { return TraceArguments(); }, [&] { context->VSGetShaderResources(0, 1, boundSrv.GetAddressOf()); });
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

		const HRESULT inkUploadResult = UploadStructuredBuffer(inkDataBuffer.Get(), cursorData, sizeof(cursorData),
			0, D3D11_MAP_WRITE_DISCARD, PerformanceGraphicsRole::InkData,
			PerformanceGraphicsSite("draw_transient_drawing_cursor_map_ink_data"), PerformanceGraphicsSite("draw_transient_drawing_cursor_memcpy_ink_data"),
			PerformanceGraphicsSite("draw_transient_drawing_cursor_unmap_1"), PerformanceGraphicsSite("draw_transient_drawing_cursor_update_ink_data"));
		if (FAILED(inkUploadResult))
		{
			recordGpu("upload-ink-data", inkUploadResult, false);
			return;
		}

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		const HRESULT constantsMapResult = PerformanceMap(performance_, context.Get(), globalCB.Get(), 0,
			D3D11_MAP_WRITE_DISCARD, 0, &mapped, 0, PerformanceGraphicsRole::GlobalConstants,
			PerformanceGraphicsSite("draw_transient_drawing_cursor_map_global_constants"), performancePass_);
		if (FAILED(constantsMapResult))
		{
			recordGpu("map-global-cb", constantsMapResult, false);
			return;
		}
		auto* constants = static_cast<GlobalShaderConstants*>(mapped.pData);
		// 单独计量原有逐字段写入；不更改生产写法或补写 padding。
		PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::WriteConstant, PerformanceGraphicsRole::GlobalConstants,
			PerformanceGraphicsSite("draw_transient_drawing_cursor_field_write_global_constants"), [&] { return TraceArguments(globalCB.Get(), 48); }, [&] {
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
		});
		PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Unmap, PerformanceGraphicsRole::GlobalConstants,
			PerformanceGraphicsSite("draw_transient_drawing_cursor_unmap_2"), [&] { return TraceArguments(globalCB.Get(), 0, 0); }, [&] { context->Unmap(globalCB.Get(), 0); });

		recordGpu("upload-success", S_OK, false);

		SetOMTarget(backBufferRTV.Get());
		PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw_transient_drawing_cursor_i_a_set_input_layout_1"), [&] { return TraceArguments(); }, [&] { context->IASetInputLayout(nullptr); });
		PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw_transient_drawing_cursor_i_a_set_primitive_topology_1"), [&] { return TraceArguments(); }, [&] { context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); });
		PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw_transient_drawing_cursor_v_s_set_shader_1"), [&] { return TraceArguments(); }, [&] { context->VSSetShader(vertexShader.Get(), nullptr, 0); });
		ID3D11ShaderResourceView* shaderResources[] = { inkDataSRV.Get() };
		PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::LayerTexture,
			PerformanceGraphicsSite("draw_transient_drawing_cursor_v_s_set_shader_resources_1"), [&] { return TraceArguments(TraceViewResource((shaderResources)[0]), 0, 0, nullptr, 0, 1); }, [&] { context->VSSetShaderResources(0, 1, shaderResources); });
		ID3D11Buffer* constantBuffers[] = { globalCB.Get() };
		PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::GlobalConstants,
			PerformanceGraphicsSite("draw_transient_drawing_cursor_v_s_set_constant_buffers_1"), [&] { return TraceArguments((constantBuffers)[0], 0, 0, nullptr, 0, 1); }, [&] { context->VSSetConstantBuffers(0, 1, constantBuffers); });
		PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw_transient_drawing_cursor_p_s_set_shader_1"), [&] { return TraceArguments(); }, [&] { context->PSSetShader(pixelShader.Get(), nullptr, 0); });
		PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::GlobalConstants,
			PerformanceGraphicsSite("draw_transient_drawing_cursor_p_s_set_constant_buffers_1"), [&] { return TraceArguments((constantBuffers)[0], 0, 0, nullptr, 0, 1); }, [&] { context->PSSetConstantBuffers(0, 1, constantBuffers); });
		// Cursor PS 直接输出 premultiplied Add 和 Retain，复用 resolve blend 叠到 backbuffer。
		PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw_transient_drawing_cursor_o_m_set_blend_state_1"), [&] { return TraceArguments(); }, [&] { context->OMSetBlendState(operatorResolveBlendState.Get(), nullptr, 0xFFFFFFFF); });
		PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::Other,
			PerformanceGraphicsSite("draw_transient_drawing_cursor_r_s_set_state_1"), [&] { return TraceArguments(); }, [&] { context->RSSetState(rasterState.Get()); });
		recordGpu("before-draw", S_OK, true);
		PerformanceCall(performance_, PerformanceStage::DrawSubmit, [&] { PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Draw, PerformanceGraphicsRole::InkData,
			PerformanceGraphicsSite("draw_transient_drawing_cursor_draw_1"), [&] { return TraceArguments(inkDataBuffer.Get(), 0, 0, nullptr, 6, 1); }, [&] { context->Draw(6, 0); }); });
			if (performance_) performance_->Draw();
		// Draw 无 HRESULT；这里只证明命令已发出，像素仍由读回诊断独立验证。
		recordGpu("after-draw-issued", S_OK, true);

		ID3D11ShaderResourceView* nullResource[] = { nullptr };
		PerformanceGraphicsVoid(performance_, PerformanceGraphicsOperation::Bind, PerformanceGraphicsRole::LayerTexture,
			PerformanceGraphicsSite("draw_transient_drawing_cursor_v_s_set_shader_resources_2"), [&] { return TraceArguments(TraceViewResource((nullResource)[0]), 0, 0, nullptr, 0, 1); }, [&] { context->VSSetShaderResources(0, 1, nullResource); });
	}

}
