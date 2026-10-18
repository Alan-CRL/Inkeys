module;

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../../../resource.h"
#include "../../../../inkStrokeModelerTest/draw3/shape_core.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <d3d11.h>
#include <DirectXMath.h>
#include <utility>
#include <windows.h>
#include <wrl/client.h>

module Inkeys.Drawing.Draw3.renderer;

namespace Inkeys::Drawing::Draw3
{
	namespace
	{
		// 独立 b4；字段偏移同时由 shape.hlsli 的声明固定。
		struct ShapeConstants
		{
			DirectX::XMFLOAT2 screen;
			uint32_t bufferOffset = 0, pass = 0;
			DirectX::XMFLOAT4 color;
			DirectX::XMFLOAT2 origin;
			float dashOffset = 0;
			uint32_t dashKind = 0;
			DirectX::XMFLOAT4 rect;
		};
		static_assert(sizeof(ShapeConstants) == 64 && sizeof(ShapeConstants) % 16 == 0);
		static_assert(offsetof(ShapeConstants, color) == 16 &&
			offsetof(ShapeConstants, origin) == 32 && offsetof(ShapeConstants, rect) == 48);
		static_assert(sizeof(::draw3::shapes::GpuPrimitive) == 4 * sizeof(InkPoint));
		static_assert(offsetof(::draw3::shapes::GpuPrimitive, halfWidth) == 40 &&
			offsetof(::draw3::shapes::GpuPrimitive, flags) == 52);

		bool UploadConstants(ID3D11DeviceContext* context, ID3D11Buffer* buffer,
			const ShapeConstants& constants)
		{
			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (FAILED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
			std::memcpy(mapped.pData, &constants, sizeof(constants));
			context->Unmap(buffer, 0);
			return true;
		}

		bool LoadShapeShader(int id, const void*& bytes, size_t& size)
		{
			const HMODULE module = GetModuleHandleW(nullptr);
			const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(id), L"SHADER");
			if (!resource) return false;
			const HGLOBAL memory = LoadResource(module, resource);
			bytes = memory ? LockResource(memory) : nullptr;
			size = SizeofResource(module, resource);
			return bytes && size;
		}
	}

	bool InkRenderer::EnsureShapePipeline()
	{
		if (shapeVertexShader_ && shapePixelShader_ && shapeCB_ && shapeOrderedBlend_) return true;
		if (!device || !context) return false;
		// 局部候选保证任一 shader/资源创建失败时不发布半套 pipeline。
		Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel;
		Microsoft::WRL::ComPtr<ID3D11Buffer> constants;
		Microsoft::WRL::ComPtr<ID3D11BlendState> ordered;
		const void* bytes = nullptr;
		size_t size = 0;
		if (!LoadShapeShader(IDR_DRAW3_SHAPE_VERTEX_SHADER, bytes, size) ||
			FAILED(device->CreateVertexShader(bytes, size, nullptr, vertex.GetAddressOf()))) return false;
		if (!LoadShapeShader(IDR_DRAW3_SHAPE_PIXEL_SHADER, bytes, size) ||
			FAILED(device->CreatePixelShader(bytes, size, nullptr, pixel.GetAddressOf()))) return false;
		const D3D11_BUFFER_DESC description = { sizeof(ShapeConstants), D3D11_USAGE_DYNAMIC,
			D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
		if (FAILED(device->CreateBuffer(&description, nullptr, constants.GetAddressOf()))) return false;
		D3D11_BLEND_DESC blend = {};
		blend.IndependentBlendEnable = TRUE;
		blend.RenderTarget[0].BlendEnable = TRUE;
		blend.RenderTarget[0].SrcBlend = blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blend.RenderTarget[0].DestBlend = blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		blend.RenderTarget[0].BlendOp = blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		blend.RenderTarget[1].BlendEnable = TRUE;
		blend.RenderTarget[1].SrcBlend = blend.RenderTarget[1].SrcBlendAlpha = D3D11_BLEND_ZERO;
		blend.RenderTarget[1].DestBlend = D3D11_BLEND_SRC_COLOR;
		blend.RenderTarget[1].DestBlendAlpha = D3D11_BLEND_SRC_ALPHA;
		blend.RenderTarget[1].BlendOp = blend.RenderTarget[1].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blend.RenderTarget[1].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED;
		if (FAILED(device->CreateBlendState(&blend, ordered.GetAddressOf()))) return false;
		shapeVertexShader_ = vertex; shapePixelShader_ = pixel;
		shapeCB_ = constants; shapeOrderedBlend_ = ordered;
		return true;
	}

	bool InkRenderer::EnsureShapeLayers()
	{
		if (!(viewportWidth > 0 && viewportHeight > 0)) return false;
		if (shapeMaterialLayer_.addTexture && shapeCompositeLayer_.addTexture)
		{
			D3D11_TEXTURE2D_DESC description = {};
			shapeMaterialLayer_.addTexture->GetDesc(&description);
			if (description.Width == static_cast<UINT>(viewportWidth) &&
				description.Height == static_cast<UINT>(viewportHeight)) return true;
		}
		OperatorLayerResources material, composite;
		if (!CreateOperatorLayerResources(static_cast<UINT>(viewportWidth),
			static_cast<UINT>(viewportHeight), material) ||
			!CreateOperatorLayerResources(static_cast<UINT>(viewportWidth),
				static_cast<UINT>(viewportHeight), composite)) return false;
		shapeMaterialLayer_ = std::move(material);
		shapeCompositeLayer_ = std::move(composite);
		return true;
	}

	bool InkRenderer::DrawOrderedLegacyShapePrimitive(const ShapePrimitive& primitive,
		ShapePrimitiveKind kind, DirectX::XMFLOAT4 color)
	{
		if (!context || !EnsureShapePipeline() || !EnsureShapeLayers()) return false;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> saved[2];
		ID3D11RenderTargetView* targets[2] = {};
		context->OMGetRenderTargets(2, targets, nullptr);
		saved[0].Attach(targets[0]); saved[1].Attach(targets[1]);
		if (!targets[0] || !targets[1]) return false;
		struct Restore
		{
			ID3D11DeviceContext* context;
			ID3D11RenderTargetView** targets;
			~Restore()
			{
				ID3D11ShaderResourceView* nulls[] = { nullptr, nullptr };
				ID3D11Buffer* buffer = nullptr;
				context->VSSetShaderResources(0, 1, nulls);
				context->PSSetShaderResources(1, 2, nulls);
				context->VSSetConstantBuffers(4, 1, &buffer);
				context->PSSetConstantBuffers(4, 1, &buffer);
				context->OMSetRenderTargets(2, targets, nullptr);
			}
		} restore{ context.Get(), targets };
		ClearOperatorLayer(shapeMaterialLayer_);
		SetOperatorTarget(shapeMaterialLayer_);
		if (DrawShapePrimitives(std::span<const ShapePrimitive>(&primitive, 1), kind, color) < 0)
			return false;
		// 旧图元仍由原 shader 产生，统一有序解析仅用于混合通用图形的活动层。
		return ResolveShapeLayer(shapeMaterialLayer_, targets,
			{ 0, 0, static_cast<LONG>(viewportWidth), static_cast<LONG>(viewportHeight) });
	}

	bool InkRenderer::DrawShapeObject(const ::draw3::shapes::ShapeObject& object,
		float originX, float originY, float displayScale)
	{
		if (!std::isfinite(displayScale) || displayScale <= 0) return false;
		const auto found = std::find_if(shapeRenderCache_.begin(), shapeRenderCache_.end(),
			[&](const ShapeRenderCacheEntry& entry) { return entry.id == object.id &&
				entry.revision == object.revision && entry.displayScale == displayScale; });
		if (found != shapeRenderCache_.end()) return DrawShapeRenderData(found->data, originX, originY);
		::draw3::shapes::RenderData data;
		if (!::draw3::shapes::BuildRenderData(object, data, { displayScale, 0.25f })) return false;
		// 候选绘制成功后才进入缓存，失败资源不会留下可复用的成功记录。
		if (!DrawShapeRenderData(data, originX, originY)) return false;
		// 有界缓存避免长会话删除对象后永久保留所有旧修订。
		if (shapeRenderCache_.size() >= 128) shapeRenderCache_.erase(shapeRenderCache_.begin());
		shapeRenderCache_.push_back({ object.id, object.revision, displayScale, std::move(data) });
		return true;
	}

	void InkRenderer::ClearShapeRenderCache() noexcept
	{
		shapeRenderCache_.clear();
	}

	void InkRenderer::InvalidateShapeCache(const ::draw3::shapes::ObjectId& id) noexcept
	{
		shapeRenderCache_.erase(std::remove_if(shapeRenderCache_.begin(), shapeRenderCache_.end(),
			[&](const ShapeRenderCacheEntry& entry) { return entry.id == id; }), shapeRenderCache_.end());
	}

	bool InkRenderer::DrawShapeBatch(const ::draw3::shapes::RenderBatch& batch,
		float originX, float originY)
	{
		using ::draw3::shapes::GpuPrimitive;
		const auto& style = batch.style;
		const uint32_t rgb = batch.fill ? style.fillRgb : style.strokeRgb;
		ShapeConstants constants = {};
		constants.screen = { viewportWidth, viewportHeight };
		constants.origin = { originX, originY };
		constants.color = { static_cast<float>((rgb >> 16) & 255) / 255,
			static_cast<float>((rgb >> 8) & 255) / 255, static_cast<float>(rgb & 255) / 255,
			batch.fill ? style.fillOpacity : style.strokeOpacity };
		constants.dashKind = batch.fill ? 0 : static_cast<uint32_t>(style.linePattern);
		constants.dashOffset = style.dashOffset;
		constexpr size_t capacity = kMaxBufferCapacity / 4;
		for (size_t start = 0; start < batch.primitives.size();)
		{
			const size_t count = std::min(capacity, batch.primitives.size() - start);
			D3D11_MAP mode = D3D11_MAP_WRITE_NO_OVERWRITE;
			if (!mapNoOverwriteOnDynamicBufferSRV || m_bufferHead + count * 4 > kMaxBufferCapacity)
			{
				mode = D3D11_MAP_WRITE_DISCARD;
				m_bufferHead = 0;
			}
			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (FAILED(context->Map(inkDataBuffer.Get(), 0, mode, 0, &mapped))) return false;
			std::memcpy(static_cast<InkPoint*>(mapped.pData) + m_bufferHead,
				batch.primitives.data() + start, count * sizeof(GpuPrimitive));
			context->Unmap(inkDataBuffer.Get(), 0);
			constants.bufferOffset = static_cast<uint32_t>(m_bufferHead);
			if (!UploadConstants(context.Get(), shapeCB_.Get(), constants)) return false;
			// 上传后立即推进；后续失败也不能再次 NO_OVERWRITE 到已经提交的区域。
			m_bufferHead += count * 4;
			SetOperatorTarget(shapeMaterialLayer_);
			context->IASetInputLayout(nullptr);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(shapeVertexShader_.Get(), nullptr, 0);
			context->PSSetShader(shapePixelShader_.Get(), nullptr, 0);
			ID3D11Buffer* buffer = shapeCB_.Get();
			context->VSSetConstantBuffers(4, 1, &buffer);
			context->PSSetConstantBuffers(4, 1, &buffer);
			ID3D11ShaderResourceView* source = inkDataSRV.Get();
			context->VSSetShaderResources(0, 1, &source);
			context->OMSetBlendState(strokeOperatorBlendState.Get(), nullptr, 0xFFFFFFFF);
			context->RSSetState(rasterState.Get());
			context->Draw(static_cast<UINT>(count * 6), 0);
			source = nullptr;
			context->VSSetShaderResources(0, 1, &source);
			start += count;
		}
		return true;
	}

	bool InkRenderer::ResolveShapeLayer(const OperatorLayerResources& source,
		ID3D11RenderTargetView* const* targets, RECT rect)
	{
		ShapeConstants constants = {};
		constants.screen = { viewportWidth, viewportHeight };
		constants.pass = 1;
		constants.rect = { static_cast<float>(rect.left), static_cast<float>(rect.top),
			static_cast<float>(rect.right), static_cast<float>(rect.bottom) };
		// 在切换目标前完成最后一个可同步失败的步骤，保证调用方内容原子性。
		if (!UploadConstants(context.Get(), shapeCB_.Get(), constants)) return false;
		context->OMSetRenderTargets(2, targets, nullptr);
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(shapeVertexShader_.Get(), nullptr, 0);
		context->PSSetShader(shapePixelShader_.Get(), nullptr, 0);
		ID3D11Buffer* buffer = shapeCB_.Get();
		context->VSSetConstantBuffers(4, 1, &buffer);
		context->PSSetConstantBuffers(4, 1, &buffer);
		ID3D11ShaderResourceView* resources[] = { source.addSRV.Get(), source.retainSRV.Get() };
		context->PSSetShaderResources(1, 2, resources);
		context->OMSetBlendState(shapeOrderedBlend_.Get(), nullptr, 0xFFFFFFFF);
		context->RSSetState(rasterState.Get());
		context->Draw(6, 0);
		ID3D11ShaderResourceView* nulls[] = { nullptr, nullptr };
		context->PSSetShaderResources(1, 2, nulls);
		return true;
	}

	bool InkRenderer::DrawShapeRenderData(const ::draw3::shapes::RenderData& data,
		float originX, float originY)
	{
		if (!context || !inkDataBuffer || !inkDataSRV || !strokeOperatorBlendState ||
			!std::isfinite(originX) || !std::isfinite(originY)) return false;
		if (data.batches.empty() || !data.bounds.valid) return true;
		RECT rect = { static_cast<LONG>(std::floor(data.bounds.left - originX - 1)),
			static_cast<LONG>(std::floor(data.bounds.top - originY - 1)),
			static_cast<LONG>(std::ceil(data.bounds.right - originX + 1)),
			static_cast<LONG>(std::ceil(data.bounds.bottom - originY + 1)) };
		rect.left = std::max(0L, rect.left); rect.top = std::max(0L, rect.top);
		rect.right = std::min(static_cast<LONG>(viewportWidth), rect.right);
		rect.bottom = std::min(static_cast<LONG>(viewportHeight), rect.bottom);
		if (rect.left >= rect.right || rect.top >= rect.bottom) return true;
		if (!EnsureShapePipeline() || !EnsureShapeLayers()) return false;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> saved[2];
		ID3D11RenderTargetView* targets[2] = {};
		context->OMGetRenderTargets(2, targets, nullptr);
		saved[0].Attach(targets[0]); saved[1].Attach(targets[1]);
		if (!targets[0] || !targets[1]) return false;
		struct Restore
		{
			ID3D11DeviceContext* context;
			ID3D11RenderTargetView** targets;
			~Restore()
			{
				ID3D11ShaderResourceView* nulls[] = { nullptr, nullptr };
				ID3D11Buffer* buffer = nullptr;
				context->VSSetShaderResources(0, 1, nulls);
				context->PSSetShaderResources(1, 2, nulls);
				context->VSSetConstantBuffers(4, 1, &buffer);
				context->PSSetConstantBuffers(4, 1, &buffer);
				context->OMSetRenderTargets(2, targets, nullptr);
			}
		} restore{ context.Get(), targets };
		ClearOperatorLayer(shapeCompositeLayer_);
		for (size_t first = 0; first < data.batches.size();)
		{
			const auto& material = data.batches[first];
			size_t end = first + 1;
			if (!material.fill)
			{
				// 同色同透明度的相邻描边共用覆盖层；每批仍保留自己的虚线相位。
				// 不跨越填充或其他材质，保证复合图形原有 source-over 层序。
				for (; end < data.batches.size(); ++end)
				{
					const auto& next = data.batches[end];
					if (next.fill || next.style.strokeRgb != material.style.strokeRgb ||
						next.style.strokeOpacity != material.style.strokeOpacity ||
						next.style.width != material.style.width) break;
				}
			}
			ClearOperatorLayer(shapeMaterialLayer_);
			for (size_t index = first; index < end; ++index)
				if (!DrawShapeBatch(data.batches[index], originX, originY)) return false;
			ID3D11RenderTargetView* composite[] = {
				shapeCompositeLayer_.addRTV.Get(), shapeCompositeLayer_.retainRTV.Get() };
			if (!ResolveShapeLayer(shapeMaterialLayer_, composite, rect)) return false;
			first = end;
		}
		// 此前所有 pass 只写独立 scratch；最后一次才发布完整逻辑对象。
		return ResolveShapeLayer(shapeCompositeLayer_, targets, rect);
	}
}
