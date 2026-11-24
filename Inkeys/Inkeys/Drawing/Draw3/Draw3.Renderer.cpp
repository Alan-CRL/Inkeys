module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Draw3.DiagnosticOutput.h"
#include "../../../resource.h"
#include "../../../IdtStart.h"
#include "Draw3.ShaderCompatibility.h"
#include "Draw3.CursorSettings.h"
#include "Draw3.EraserCursor.h"
#include "Draw3.SpeedEraser.h"
#include "Draw3.Win7GuardedShader.h"
#include "../../../../inkStrokeModelerTest/draw3/shape_core.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <d3d11.h>
#include <d3d11_1.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <dxgi1_2.h>
#include <limits>
#include <span>
#include <utility>
#include <vector>
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#include <wrl/client.h>

module Inkeys.Drawing.Draw3.renderer;

namespace Inkeys::Drawing::Draw3
{
	using renderer_detail::GlobalShaderConstants;

	HRESULT InkRenderer::UploadStructuredBuffer(ID3D11Buffer* buffer, const void* data, UINT bytes,
		UINT offset, D3D11_MAP mapType)
	{
		if (!buffer || !data || !context || !device || !bytes) return E_INVALIDARG;
		if (useDefaultStructuredUpload_)
		{
			// DEFAULT 每批从零更新实际字节；保留大容量，但避开大动态资源的映射成本。
			if (offset != 0) return E_INVALIDARG;
			const D3D11_BOX range = { 0, 0, 0, bytes, 1, 1 };
			context->UpdateSubresource(buffer, 0, &range, data, 0, 0);
			// UpdateSubresource 是 void；设备失效单独查询，不能伪造该 API 的 HRESULT。
			return device->GetDeviceRemovedReason();
		}
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		const HRESULT result = context->Map(buffer, 0, mapType, 0, &mapped);
		if (FAILED(result)) return result;
		std::memcpy(static_cast<unsigned char*>(mapped.pData) + offset, data, bytes);
		context->Unmap(buffer, 0);
		// Laser/Cursor 的 DISCARD 同样开启新一代墨迹数据，后续 NO_OVERWRITE 必须越过已用区。
		if (buffer == inkDataBuffer.Get()) m_bufferHead = (offset + bytes) / sizeof(InkPoint);
		return result;
	}

	namespace
	{
		constexpr bool UseZhaoxinDefaultUpload(bool enabled, D3D_DRIVER_TYPE driver,
			bool identityAvailable, UINT vendor) noexcept
		{
			return enabled && driver == D3D_DRIVER_TYPE_HARDWARE && identityAvailable && vendor == 0x1D17;
		}

		struct ShaderBlob
		{
			const void* data;
			size_t size;
		};

		// 从当前模块的 SHADER 资源中读取已编译字节码。
		ShaderBlob LoadShaderFromResource(int resourceID)
		{
			HMODULE module = ::GetModuleHandle(nullptr);
			HRSRC resource = ::FindResource(module, MAKEINTRESOURCE(resourceID), L"SHADER");
			if (!resource) return { nullptr, 0 };

			HGLOBAL memory = ::LoadResource(module, resource);
			if (!memory) return { nullptr, 0 };
			return { ::LockResource(memory), static_cast<size_t>(::SizeofResource(module, resource)) };
		}

		bool ExactDiagnosticEnvironmentEnabled(const wchar_t* name) noexcept
		{
			wchar_t value[2] = {};
			return GetEnvironmentVariableW(name, value, ARRAYSIZE(value)) == 1 && value[0] == L'1';
		}

		// 仅诊断初始化时计算实际所选字节码的 SHA；不把生成时的预期 hash 冒充当前资源身份。
		void ReportSelectedPixelShader(const ShaderBlob& blob, bool guarded, HRESULT result) noexcept
		{
			char hash[65] = "unavailable";
			BCRYPT_ALG_HANDLE algorithm = nullptr;
			BCRYPT_HASH_HANDLE state = nullptr;
			DWORD objectSize = 0, returned = 0;
			std::array<UCHAR, 32> digest = {};
			std::array<UCHAR, 4096> object = {};
			if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0)
			{
				if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize),
					sizeof(objectSize), &returned, 0) >= 0 && objectSize > 0 && objectSize <= object.size())
				{
					if (BCryptCreateHash(algorithm, &state, object.data(), objectSize, nullptr, 0, 0) >= 0)
					{
						if (BCryptHashData(state, static_cast<PUCHAR>(const_cast<void*>(blob.data)),
							static_cast<ULONG>(blob.size), 0) >= 0 &&
							BCryptFinishHash(state, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0)
							for (size_t index = 0; index < digest.size(); ++index)
								std::snprintf(hash + index * 2, 3, "%02X", digest[index]);
						BCryptDestroyHash(state);
					}
				}
				BCryptCloseAlgorithmProvider(algorithm, 0);
			}
			char message[256] = {};
			std::snprintf(message, sizeof(message),
				"[Draw3ShaderSelection] actual=%s sha256=%s bytes=%zu create_hr=0x%08X\n",
				guarded ? "guarded" : "original", hash, blob.size, static_cast<unsigned int>(result));
			DiagnosticOutput::Text(DiagnosticOutput::Stream::Output, message, true);
		}

		void ReportLaserParticleUnavailableOnce() noexcept
		{
			static bool reported = false;
			if (reported) return;
			reported = true;
			// 绘制线程只做非阻塞日志投递（源文件按 /utf-8 编译，窄字面量即 UTF-8）。
			(void)Inkeys::Log::TryWrite(Inkeys::Log::Level::Warn, Inkeys::Log::Module::Draw3, std::nullopt,
				"画布粒子着色器不可用，已仅保留激光主体与点击光波。");
		}
	}
	void InkRenderer::CopyResource(ID3D11Texture2D* dst, ID3D11Texture2D* src, RECT rect)
	{
		D3D11_BOX sourceRegion = {};
		sourceRegion.left = rect.left;
		sourceRegion.top = rect.top;
		sourceRegion.front = 0;
		sourceRegion.right = rect.right;
		sourceRegion.bottom = rect.bottom;
		sourceRegion.back = 1;
		context->CopySubresourceRegion(dst, 0, rect.left, rect.top, 0, src, 0, &sourceRegion); // 只复制脏矩形，减少 backbuffer 更新量。
	}

	bool InkRenderer::ApplyOperatorLayers(ID3D11RenderTargetView* dstRTV,
		const OperatorLayerResources& stableLayer, const OperatorLayerResources& liveLayer,
		RECT rect, OperatorLayerMergeMode mergeMode)
	{
		return ApplyOperatorLayers(dstRTV, stableLayer, liveLayer, std::span<const RECT>(&rect, 1), mergeMode);
	}

	bool InkRenderer::ApplyOperatorLayers(ID3D11RenderTargetView* dstRTV,
		const OperatorLayerResources& stableLayer, const OperatorLayerResources& liveLayer,
		std::span<const RECT> rects, OperatorLayerMergeMode mergeMode)
	{
		return ResolveOperatorLayers(dstRTV, &stableLayer, liveLayer, rects,
			mergeMode == OperatorLayerMergeMode::Ordered ? 2.0f : 1.0f);
	}

	bool InkRenderer::ApplyOperatorLayer(ID3D11RenderTargetView* dstRTV, const OperatorLayerResources& layer, RECT rect)
	{
		return ApplyOperatorLayer(dstRTV, layer, std::span<const RECT>(&rect, 1));
	}

	bool InkRenderer::ApplyOperatorLayer(ID3D11RenderTargetView* dstRTV, const OperatorLayerResources& layer,
		std::span<const RECT> rects)
	{
		return ResolveOperatorLayers(dstRTV, nullptr, layer, rects, 25.0f); // PS 类型 25 只读 t4/t5。
	}

	bool InkRenderer::ResolveOperatorLayers(ID3D11RenderTargetView* dstRTV, const OperatorLayerResources* stableLayer,
		const OperatorLayerResources& liveLayer, RECT rect, float shapeType)
	{
		return ResolveOperatorLayers(dstRTV, stableLayer, liveLayer, std::span<const RECT>(&rect, 1), shapeType);
	}

	bool InkRenderer::ResolveOperatorLayers(ID3D11RenderTargetView* dstRTV, const OperatorLayerResources* stableLayer,
		const OperatorLayerResources& liveLayer, std::span<const RECT> rects, float shapeType)
	{
		if (!dstRTV || !compositeVertexBuffer || !compositeVertexSRV ||
			(stableLayer && (!stableLayer->addSRV || !stableLayer->retainSRV)) ||
			!liveLayer.addSRV || !liveLayer.retainSRV) return false;
		const auto clamp = [&](const RECT& rect) {
			return RECT{ (std::max)(0L, rect.left), (std::max)(0L, rect.top),
				(std::min)(static_cast<LONG>(viewportWidth), rect.right),
				(std::min)(static_cast<LONG>(viewportHeight), rect.bottom) };
		};
		const auto empty = [](const RECT& rect) {
			return rect.left >= rect.right || rect.top >= rect.bottom;
		};

		size_t first = 0;
		RECT firstRect = {};
		for (; first < rects.size(); ++first)
		{
			firstRect = clamp(rects[first]);
			if (!empty(firstRect)) break;
		}
		if (first == rects.size()) return true;

#ifndef NDEBUG
		// 算子合成不幂等，正面积重叠会重复应用；共边可以合成。
		for (size_t i = first + 1; i < rects.size(); ++i)
		{
			const RECT rect = clamp(rects[i]);
			if (empty(rect)) continue;
			for (size_t j = first; j < i; ++j)
			{
				const RECT previous = clamp(rects[j]);
				assert((std::max)(rect.left, previous.left) >= (std::min)(rect.right, previous.right) ||
					(std::max)(rect.top, previous.top) >= (std::min)(rect.bottom, previous.bottom));
			}
		}
#endif

		const auto uploadRect = [&](const RECT& rect) {
			const InkPoint rectPoints[2] = {
				{ static_cast<float>(rect.left), static_cast<float>(rect.top), 0.0f, 0.0f },
				{ static_cast<float>(rect.right), static_cast<float>(rect.bottom), 0.0f, 0.0f }
			};
			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (FAILED(context->Map(compositeVertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
			std::memcpy(mapped.pData, rectPoints, sizeof(rectPoints)); // 合成仅更新独立小资源，不能打断墨迹环形追加。
			context->Unmap(compositeVertexBuffer.Get(), 0);
			return true;
		};

		// 保留首个顶点上传和常量上传的顺序，任一失败时都还没有更改绑定。
		if (!uploadRect(firstRect)) return false;
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (FAILED(context->Map(globalCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
		auto* constants = static_cast<GlobalShaderConstants*>(mapped.pData);
		constants->width = viewportWidth;
		constants->height = viewportHeight;
		constants->color = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
		constants->shapeType = shapeType;
		constants->bufferOffset = 0;
		constants->operatorKind = static_cast<uint32_t>(InkOperatorKind::Draw);
		context->Unmap(globalCB.Get(), 0);

		SetOMTarget(dstRTV);
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(vertexShader.Get(), nullptr, 0);
		ID3D11ShaderResourceView* shaderResources[] = { compositeVertexSRV.Get() };
		context->VSSetShaderResources(0, 1, shaderResources);
		ID3D11Buffer* constantBuffers[] = { globalCB.Get() };
		context->VSSetConstantBuffers(0, 1, constantBuffers);
		context->PSSetShader(pixelShader.Get(), nullptr, 0);
		context->PSSetConstantBuffers(0, 1, constantBuffers);
		ID3D11ShaderResourceView* compositeResources[] = {
			stableLayer ? stableLayer->addSRV.Get() : nullptr,
			stableLayer ? stableLayer->retainSRV.Get() : nullptr,
			nullptr, // t3 留给 VS 的荧光笔结构化缓冲区。
			liveLayer.addSRV.Get(),
			liveLayer.retainSRV.Get()
		};
		context->PSSetShaderResources(1, ARRAYSIZE(compositeResources), compositeResources);
		ID3D11SamplerState* samplers[] = { operatorSampler.Get() };
		context->PSSetSamplers(0, 1, samplers);
		context->OMSetBlendState(operatorResolveBlendState.Get(), nullptr, 0xFFFFFFFF);
		context->RSSetState(rasterState.Get());
		context->Draw(6, 0); // 双源混合一次完成 Result = Add + Retain * Below。
		++(stableLayer ? operatorCounters_.dualResolves : operatorCounters_.singleResolves);
		bool succeeded = true;
		for (const RECT& input : rects.subspan(first + 1))
		{
			const RECT rect = clamp(input);
			if (empty(rect)) continue;
			if (!uploadRect(rect))
			{
				succeeded = false;
				break;
			}
			context->Draw(6, 0);
		}

		ID3D11ShaderResourceView* nullResources[] = { nullptr, nullptr, nullptr, nullptr, nullptr };
		ID3D11SamplerState* nullSampler[] = { nullptr };
		context->VSSetShaderResources(0, 1, nullResources);
		context->PSSetShaderResources(1, ARRAYSIZE(nullResources), nullResources); // 解除 SRV 绑定，避免后续作为 RTV 时冲突。
		context->PSSetSamplers(0, 1, nullSampler);
		return succeeded;
	}

	void InkRenderer::SetScreenSize(float width, float height)
	{
		viewportWidth = width;
		viewportHeight = height;
		D3D11_VIEWPORT viewport = { 0, 0, width, height, 0.0f, 1.0f };
		context->RSSetViewports(1, &viewport);
	}

	double InkRenderer::QueryVideoMemoryUsageMiB() const noexcept
	{
		if (!videoMemoryAdapter_) return -1.0;
		DXGI_QUERY_VIDEO_MEMORY_INFO local = {};
		DXGI_QUERY_VIDEO_MEMORY_INFO nonLocal = {};
		const bool localAvailable = SUCCEEDED(videoMemoryAdapter_->QueryVideoMemoryInfo(
			0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local));
		const bool nonLocalAvailable = SUCCEEDED(videoMemoryAdapter_->QueryVideoMemoryInfo(
			0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonLocal));
		if (!localAvailable && !nonLocalAvailable) return -1.0;
		const UINT64 usageBytes = (localAvailable ? local.CurrentUsage : 0) +
			(nonLocalAvailable ? nonLocal.CurrentUsage : 0);
		return static_cast<double>(usageBytes) / (1024.0 * 1024.0);
	}

	void InkRenderer::SetOMTarget(ID3D11RenderTargetView* renderTargetView)
	{
		ID3D11RenderTargetView* targets[] = { renderTargetView };
		context->OMSetRenderTargets(1, targets, nullptr);
		if (dsState) context->OMSetDepthStencilState(dsState.Get(), 0);
	}

	void InkRenderer::SetOperatorTarget(const OperatorLayerResources& layer)
	{
		NoteOperatorContent(layer, nullptr);
		ID3D11RenderTargetView* targets[] = { layer.addRTV.Get(), layer.retainRTV.Get() };
		context->OMSetRenderTargets(ARRAYSIZE(targets), targets, nullptr);
		if (dsState) context->OMSetDepthStencilState(dsState.Get(), 0);
	}

	void InkRenderer::SetOperatorTarget(const OperatorLayerResources& layer, const RECT& writeBounds)
	{
		NoteOperatorContent(layer, &writeBounds);
		ID3D11RenderTargetView* targets[] = { layer.addRTV.Get(), layer.retainRTV.Get() };
		context->OMSetRenderTargets(ARRAYSIZE(targets), targets, nullptr);
		if (dsState) context->OMSetDepthStencilState(dsState.Get(), 0);
	}

	void InkRenderer::BindOperatorTarget(const OperatorLayerResources& layer)
	{
		ID3D11RenderTargetView* targets[] = { layer.addRTV.Get(), layer.retainRTV.Get() };
		context->OMSetRenderTargets(ARRAYSIZE(targets), targets, nullptr);
		if (dsState) context->OMSetDepthStencilState(dsState.Get(), 0);
	}

	void InkRenderer::NoteOperatorWrite(const OperatorLayerResources& layer, const RECT& writeBounds) noexcept
	{
		NoteOperatorContent(layer, &writeBounds);
		if (&layer == &layerL1) ++stableLayerEpoch_; // 入库笔画、焦点层等整笔写入不属于“本笔追加前缀”。
	}

	InkRenderer::OperatorContentTracking* InkRenderer::ContentTrackingFor(const OperatorLayerResources& layer) noexcept
	{
		return &layer == &layerL0 ? &l0Content_ : &layer == &layerL1 ? &l1Content_ : nullptr;
	}

	const InkRenderer::OperatorContentTracking* InkRenderer::ContentTrackingFor(const OperatorLayerResources& layer) const noexcept
	{
		return &layer == &layerL0 ? &l0Content_ : &layer == &layerL1 ? &l1Content_ : nullptr;
	}

	bool InkRenderer::OperatorLayerEmpty(const OperatorLayerResources& layer) const noexcept
	{
		const OperatorContentTracking* tracking = ContentTrackingFor(layer);
		return tracking && !tracking->untracked && tracking->count == 0;
	}

	void InkRenderer::MarkOperatorLayerUntracked(const OperatorLayerResources& layer) noexcept
	{
		if (OperatorContentTracking* tracking = ContentTrackingFor(layer)) tracking->untracked = true;
		if (&layer == &layerL1) ++stableLayerEpoch_;
	}

	// 在绑定时记账（先于绘制），批次上传中途失败也只会多清，不会漏清。
	void InkRenderer::NoteOperatorContent(const OperatorLayerResources& layer, const RECT* writeBounds) noexcept
	{
		OperatorContentTracking* tracking = ContentTrackingFor(layer);
		if (!tracking) return;
		if (!writeBounds)
		{
			tracking->untracked = true;
			if (&layer == &layerL1) ++stableLayerEpoch_;
			return;
		}
		RECT add = *writeBounds;
		// 声明范围可远超画布（甚至是饱和值）：先收进 ±2^24，后续外扩、相邻判断和面积计算都不会溢出；
		// 画布远小于该范围，收拢不改变与画布的交集。
		constexpr LONG kCoordinateLimit = 1L << 24;
		add.left = std::clamp(add.left, -kCoordinateLimit, kCoordinateLimit);
		add.top = std::clamp(add.top, -kCoordinateLimit, kCoordinateLimit);
		add.right = std::clamp(add.right, -kCoordinateLimit, kCoordinateLimit);
		add.bottom = std::clamp(add.bottom, -kCoordinateLimit, kCoordinateLimit);
		if (add.left >= add.right || add.top >= add.bottom) return;
		const auto area = [](const RECT& rect) noexcept
		{
			return static_cast<long long>(rect.right - rect.left) * static_cast<long long>(rect.bottom - rect.top);
		};
		const auto unite = [](const RECT& first, const RECT& second) noexcept
		{
			return RECT{ (std::min)(first.left, second.left), (std::min)(first.top, second.top),
				(std::max)(first.right, second.right), (std::max)(first.bottom, second.bottom) };
		};
		// 与已有矩形相交或相邻（含清除外扩后的重叠）时合并，同一触点逐帧追加的写入保持为一个矩形。
		// 合并后的矩形可能又碰到别的矩形，循环到不再变化；每轮至少减少一个矩形，必然终止。
		for (bool merged = true; merged;)
		{
			merged = false;
			for (uint32_t index = 0; index < tracking->count; ++index)
			{
				const RECT& existing = tracking->rects[index];
				if (add.left > existing.right + 2 * kOperatorContentClearMargin || existing.left > add.right + 2 * kOperatorContentClearMargin ||
					add.top > existing.bottom + 2 * kOperatorContentClearMargin || existing.top > add.bottom + 2 * kOperatorContentClearMargin) continue;
				add = unite(add, existing);
				tracking->rects[index] = tracking->rects[--tracking->count];
				merged = true;
				break;
			}
		}
		if (tracking->count < tracking->rects.size())
		{
			tracking->rects[tracking->count++] = add;
			return;
		}
		// 登记已满：并入合并后面积增量最小的一个，保持“登记矩形之外为单位操作”的不变量。
		uint32_t best = 0;
		long long bestGrowth = (std::numeric_limits<long long>::max)();
		for (uint32_t index = 0; index < tracking->count; ++index)
		{
			const long long growth = area(unite(add, tracking->rects[index])) - area(tracking->rects[index]);
			if (growth < bestGrowth) { bestGrowth = growth; best = index; }
		}
		tracking->rects[best] = unite(add, tracking->rects[best]);
	}

	void InkRenderer::BindLaserCoverageTarget(const LaserCoverageResources& layer)
	{
		UnbindLaserCoverageShaderResources();
		SetOMTarget(layer.rtv.Get());
	}

	void InkRenderer::NoteLaserCoverageWrite(LaserCoverageResources& layer, RECT bounds) noexcept
	{
		if (bounds.left >= bounds.right || bounds.top >= bounds.bottom) return;
		RECT& written = layer.writtenBounds;
		if (written.left >= written.right || written.top >= written.bottom)
			written = bounds;
		else
			written = { (std::min)(written.left, bounds.left), (std::min)(written.top, bounds.top),
				(std::max)(written.right, bounds.right), (std::max)(written.bottom, bounds.bottom) };
	}

	void InkRenderer::SetLaserCoverageTarget(LaserCoverageResources& layer)
	{
		layer.contentUnknown = true;
		BindLaserCoverageTarget(layer);
	}

	void InkRenderer::SetLaserCoverageTarget(LaserCoverageResources& layer, RECT writeBounds)
	{
		// 批量上传可能在已经画过一部分后失败，先登记整个可能写入的范围。
		NoteLaserCoverageWrite(layer, writeBounds);
		BindLaserCoverageTarget(layer);
	}

	void InkRenderer::SetLaserLiveCoverageTarget(RECT writeBounds)
	{
		SetLaserCoverageTarget(laserLiveCoverage, writeBounds);
	}

	void InkRenderer::ClearRTV(ID3D11RenderTargetView* renderTargetView, DirectX::XMFLOAT4 color)
	{
		const float clearColor[4] = { color.x, color.y, color.z, color.w };
		context->ClearRenderTargetView(renderTargetView, clearColor);
	}

	void InkRenderer::ClearOperatorLayer(const OperatorLayerResources& layer)
	{
		ClearRTV(layer.addRTV.Get(), kTransparentLayerClearColor);
		ClearRTV(layer.retainRTV.Get(), DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f)); // Retain=1 表示不改变下层。
		if (OperatorContentTracking* tracking = ContentTrackingFor(layer))
		{
			*tracking = { {}, 0, false };
			++operatorCounters_.fullClears;
			if (&layer == &layerL1) stableLayerOwnedEpoch_ = ++stableLayerEpoch_;
		}
	}

	namespace
	{
		RECT ExpandOperatorClearRect(const RECT& bounds, LONG width, LONG height) noexcept
		{
			// 声明允许使用饱和坐标，先提升精度再外扩和裁剪。
			return {
				static_cast<LONG>(std::clamp<long long>(static_cast<long long>(bounds.left) - kOperatorContentClearMargin, 0, width)),
				static_cast<LONG>(std::clamp<long long>(static_cast<long long>(bounds.top) - kOperatorContentClearMargin, 0, height)),
				static_cast<LONG>(std::clamp<long long>(static_cast<long long>(bounds.right) + kOperatorContentClearMargin, 0, width)),
				static_cast<LONG>(std::clamp<long long>(static_cast<long long>(bounds.bottom) + kOperatorContentClearMargin, 0, height))
			};
		}
	}

	void InkRenderer::ClearOperatorLayerRects(const OperatorLayerResources& layer, std::span<const RECT> rects) noexcept
	{
		const float transparent[4] = { kTransparentLayerClearColor.x, kTransparentLayerClearColor.y,
			kTransparentLayerClearColor.z, kTransparentLayerClearColor.w };
		const float retainIdentity[4] = { 1.0f, 1.0f, 1.0f, 1.0f }; // Retain=1 表示不改变下层。
		context1_->ClearView(layer.addRTV.Get(), transparent, rects.data(), static_cast<UINT>(rects.size()));
		context1_->ClearView(layer.retainRTV.Get(), retainIdentity, rects.data(), static_cast<UINT>(rects.size()));
	}

	bool InkRenderer::CanClearOperatorLayerRegion(const OperatorLayerResources& layer) const noexcept
	{
		const OperatorContentTracking* tracking = ContentTrackingFor(layer);
		return tracking && !tracking->untracked && context1_ && layer.addTexture && layer.addRTV && layer.retainRTV;
	}

	bool InkRenderer::ClearOperatorLayerRegion(const OperatorLayerResources& layer, std::span<const RECT> rects) noexcept
	{
		if (!CanClearOperatorLayerRegion(layer)) return false;
		D3D11_TEXTURE2D_DESC description = {};
		layer.addTexture->GetDesc(&description);
		std::array<RECT, kOperatorContentRectCapacity> clearRects = {};
		size_t clearCount = 0;
		bool cleared = false;
		for (const RECT& bounds : rects)
		{
			if (bounds.left >= bounds.right || bounds.top >= bounds.bottom) continue;
			const RECT rect = ExpandOperatorClearRect(bounds,
				static_cast<LONG>(description.Width), static_cast<LONG>(description.Height));
			if (rect.left >= rect.right || rect.top >= rect.bottom) continue;
			clearRects[clearCount++] = rect;
			if (clearCount == clearRects.size())
			{
				ClearOperatorLayerRects(layer, clearRects);
				clearCount = 0;
				cleared = true;
			}
		}
		if (clearCount != 0) ClearOperatorLayerRects(layer, std::span(clearRects).first(clearCount));
		if (cleared || clearCount != 0) ++operatorCounters_.partialClears;
		return true;
	}

	void InkRenderer::ClearOperatorLayerContent(const OperatorLayerResources& layer)
	{
		OperatorContentTracking* tracking = ContentTrackingFor(layer);
		if (!tracking)
		{
			ClearOperatorLayer(layer);
			return;
		}
		// 自上次清除以来没有任何写入（如纯橡皮帧的 L0、纯激光接触）：该层已处处为单位操作，不清除。
		if (!tracking->untracked && tracking->count == 0)
		{
			++operatorCounters_.skippedClears;
			return;
		}
		const LONG width = (std::max)(0L, static_cast<LONG>(viewportWidth));
		const LONG height = (std::max)(0L, static_cast<LONG>(viewportHeight));
		// 外扩覆盖形状合成矩形比几何包络多出的 1 像素等取整误差，再裁剪到画布；完全在画布外的矩形丢弃。
		std::array<RECT, kOperatorContentRectCapacity> clearRects = {};
		UINT clearCount = 0;
		long long area = 0;
		for (uint32_t index = 0; index < tracking->count; ++index)
		{
			const RECT rect = ExpandOperatorClearRect(tracking->rects[index], width, height);
			if (rect.left >= rect.right || rect.top >= rect.bottom) continue;
			area += static_cast<long long>(rect.right - rect.left) * static_cast<long long>(rect.bottom - rect.top);
			clearRects[clearCount++] = rect;
		}
		if (!tracking->untracked && clearCount == 0)
		{
			*tracking = { {}, 0, false }; // 写入范围完全在画布外，画布内没有内容。
			++operatorCounters_.skippedClears;
			return;
		}
		// 未声明写入、无 ClearView 或范围过大时退回整屏，保证不留残影。分界线保守取画布的 1/16：
		// 小矩形在各类设备上都不慢于整屏清，大矩形在没有快速矩形清的驱动上可能反而更慢。
		if (tracking->untracked || !context1_ ||
			area * kOperatorContentPartialClearDivisor > static_cast<long long>(width) * height)
		{
			ClearOperatorLayer(layer);
			return;
		}
		ClearOperatorLayerRects(layer, std::span(clearRects).first(clearCount));
		*tracking = { {}, 0, false };
		++operatorCounters_.partialClears;
		if (&layer == &layerL1) stableLayerOwnedEpoch_ = ++stableLayerEpoch_;
	}

	void InkRenderer::ClearLaserCoverage(LaserCoverageResources& layer)
	{
		if (!layer.rtv) return;
		UnbindLaserCoverageShaderResources();
		ClearRTV(layer.rtv.Get(), kTransparentLayerClearColor);
		layer.writtenBounds = {};
		layer.contentUnknown = false;
		++laserCounters_.fullClears;
	}

	void InkRenderer::ClearLaserCoverageContent(LaserCoverageResources& layer)
	{
		if (!layer.rtv) return;
		if (layer.contentUnknown)
		{
			ClearLaserCoverage(layer);
			return;
		}
		const RECT canvas = { 0, 0, static_cast<LONG>(viewportWidth), static_cast<LONG>(viewportHeight) };
		RECT rect = {};
		if (!IntersectRect(&rect, &layer.writtenBounds, &canvas))
		{
			layer.writtenBounds = {};
			++laserCounters_.skippedClears;
			return;
		}
		// 激光写域已经包含抗锯齿余量，直接用裁剪后的面积判断 1/16 分界线。
		const auto area = static_cast<long long>(rect.right - rect.left) * (rect.bottom - rect.top);
		if (area * kOperatorContentPartialClearDivisor > static_cast<long long>(canvas.right) * canvas.bottom)
		{
			ClearLaserCoverage(layer);
			return;
		}
		if (context1_)
		{
			const float transparent[4] = {};
			UnbindLaserCoverageShaderResources();
			context1_->ClearView(layer.rtv.Get(), transparent, &rect, 1);
			++laserCounters_.rectClears;
		}
		else
		{
			// 类型 12 提交失败时仍须清掉旧像素，避免下一批继承残影。
			if (!ClearLaserRect(layer, rect))
			{
				ClearLaserCoverage(layer);
				return;
			}
			context->OMSetRenderTargets(0, nullptr, nullptr);
		}
		layer.writtenBounds = {};
	}

	void InkRenderer::UnbindLaserCoverageShaderResources()
	{
		if (!context) return;
		ID3D11ShaderResourceView* nullLaserResources[] = {
			nullptr, nullptr, nullptr, nullptr };
		context->PSSetShaderResources(6, ARRAYSIZE(nullLaserResources), nullLaserResources);
	}

	void InkRenderer::ClearLaserCompositedColor()
	{
		ClearLaserCoverageContent(laserCompositedColor);
	}

	void InkRenderer::ClearAllLaserCoverage()
	{
		ClearLaserCompositedColor();
		ClearLaserIncrementalCoverage();
		if (context) context->OMSetRenderTargets(0, nullptr, nullptr);
		laserBakeScratchColor_.Reset();
		laserBakeBackupRect_ = {};
	}

	bool InkRenderer::BeginLaserBake(RECT bounds, bool needsBackup)
	{
		laserBakeBackupRect_ = {};
		if (!device || !context || !laserCompositedColor.texture ||
			!laserCompositedColor.rtv) return false;
		if (!needsBackup) return true;

		D3D11_TEXTURE2D_DESC description = {};
		laserCompositedColor.texture->GetDesc(&description);
		const RECT canvas = { 0, 0, static_cast<LONG>(description.Width), static_cast<LONG>(description.Height) };
		RECT backup = {};
		if (!IntersectRect(&backup, &bounds, &canvas)) return true;
		if (!laserBakeScratchColor_)
		{
			// 副本只用于区域拷贝，尺寸随已烘颜色重建，不需要 RTV 或 SRV。
			description.BindFlags = 0;
			if (FAILED(device->CreateTexture2D(&description, nullptr,
				laserBakeScratchColor_.GetAddressOf()))) return false;
			++laserCounters_.backupAllocations;
		}

		UnbindLaserCoverageShaderResources();
		context->OMSetRenderTargets(0, nullptr, nullptr);
		CopyResource(laserBakeScratchColor_.Get(), laserCompositedColor.texture.Get(), backup);
		laserBakeBackupRect_ = backup;
		++laserCounters_.bakeCopies;
		return true;
	}

	ID3D11RenderTargetView* InkRenderer::LaserBakeTarget() const noexcept
	{
		return laserCompositedColor.rtv.Get();
	}

	void InkRenderer::CommitLaserBake(RECT bounds) noexcept
	{
		// 只有整批成功才扩展已烘写域，回滚后仍保留批前的清除范围。
		NoteLaserCoverageWrite(laserCompositedColor, bounds);
		laserBakeBackupRect_ = {};
		UnbindLaserCoverageShaderResources();
		if (context) context->OMSetRenderTargets(0, nullptr, nullptr);
	}

	void InkRenderer::AbortLaserBake() noexcept
	{
		const RECT backup = laserBakeBackupRect_;
		laserBakeBackupRect_ = {};
		if (backup.left >= backup.right || backup.top >= backup.bottom) return;
		UnbindLaserCoverageShaderResources();
		context->OMSetRenderTargets(0, nullptr, nullptr);
		CopyResource(laserCompositedColor.texture.Get(), laserBakeScratchColor_.Get(), backup);
		++laserCounters_.bakeCopies;
	}

	bool InkRenderer::CreateOperatorLayerResources(UINT width, UINT height, OperatorLayerResources& layer)
	{
		// 新纹理没有初始像素，在整层清除前保持未知。分配中途失败也不能沿用旧登记。
		if (auto* tracking = ContentTrackingFor(layer)) *tracking = {};
		if (&layer == &layerL1) ++stableLayerEpoch_;
		D3D11_TEXTURE2D_DESC textureDescription = {};
		textureDescription.Width = width;
		textureDescription.Height = height;
		textureDescription.MipLevels = 1;
		textureDescription.ArraySize = 1;
		textureDescription.SampleDesc.Count = 1;
		textureDescription.Usage = D3D11_USAGE_DEFAULT;
		textureDescription.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

		textureDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		if (FAILED(device->CreateTexture2D(&textureDescription, nullptr,
			layer.addTexture.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(device->CreateRenderTargetView(layer.addTexture.Get(), nullptr,
			layer.addRTV.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(device->CreateShaderResourceView(layer.addTexture.Get(), nullptr,
			layer.addSRV.ReleaseAndGetAddressOf()))) return false;

		textureDescription.Format = DXGI_FORMAT_R16_FLOAT;
		if (FAILED(device->CreateTexture2D(&textureDescription, nullptr,
			layer.retainTexture.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(device->CreateRenderTargetView(layer.retainTexture.Get(), nullptr,
			layer.retainRTV.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(device->CreateShaderResourceView(layer.retainTexture.Get(), nullptr,
			layer.retainSRV.ReleaseAndGetAddressOf()))) return false;
		return true;
	}

	bool InkRenderer::CreateLaserCoverageResources(
		UINT width, UINT height, LaserCoverageResources& layer)
	{
		D3D11_TEXTURE2D_DESC description = {};
		description.Width = width;
		description.Height = height;
		description.MipLevels = 1;
		description.ArraySize = 1;
		description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		description.SampleDesc.Count = 1;
		description.Usage = D3D11_USAGE_DEFAULT;
		description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		if (FAILED(device->CreateTexture2D(&description, nullptr,
			layer.texture.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(device->CreateRenderTargetView(layer.texture.Get(), nullptr,
			layer.rtv.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(device->CreateShaderResourceView(layer.texture.Get(), nullptr,
			layer.srv.ReleaseAndGetAddressOf()))) return false;
		return true;
	}

	bool InkRenderer::LaserResourcesAvailable() const noexcept
	{
		return laserCompositedColor.texture && laserCompositedColor.rtv &&
			laserCompositedColor.srv && laserStrokeCoverage.texture &&
			laserStrokeCoverage.rtv && laserStrokeCoverage.srv;
	}

	uint64_t InkRenderer::LaserResourceBytes() const noexcept
	{
		uint64_t bytes = 0;
		for (ID3D11Texture2D* texture : { laserCompositedColor.texture.Get(),
			laserStrokeCoverage.texture.Get(), laserLiveCoverage.texture.Get(), laserBakeScratchColor_.Get() })
		{
			if (!texture) continue;
			D3D11_TEXTURE2D_DESC description = {};
			texture->GetDesc(&description);
			bytes += static_cast<uint64_t>(description.Width) * description.Height * 4;
		}
		return bytes;
	}

	bool InkRenderer::EnsureLaserResources(UINT width, UINT height)
	{
		if (!device || !context || width == 0 || height == 0) return false;
		if (LaserResourcesAvailable())
		{
			D3D11_TEXTURE2D_DESC description = {};
			laserCompositedColor.texture->GetDesc(&description);
			if (description.Width == width && description.Height == height) return true;
		}
		LaserCoverageResources color, coverage;
		if (!CreateLaserCoverageResources(width, height, color)) return false;
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
		if (failLaserPreparationAfterColor_) return false;
#endif
		if (!CreateLaserCoverageResources(width, height, coverage)) return false;
		// 候选完整后才替换；失败时旧会话保持，候选由 ComPtr 自动归还。
		ReleaseLaserResources();
		laserCompositedColor = std::move(color);
		laserStrokeCoverage = std::move(coverage);
		ClearAllLaserCoverage();
		WarmUpLaserShaders();
		return true;
	}

	void InkRenderer::ReleaseLaserResources() noexcept
	{
		UnbindLaserCoverageShaderResources();
		if (context) context->OMSetRenderTargets(0, nullptr, nullptr);
		laserCompositedColor = {};
		laserStrokeCoverage = {};
		laserLiveCoverage = {};
		laserBakeScratchColor_.Reset();
		laserBakeBackupRect_ = {};
		laserUndoSavedCoverage_ = {};
		laserIncrementalCoverageEnabled_ = false;
		laserIncrementalCoverageUnavailable_ = false;
		// 粒子池与激光纹理各管各的：清空飘散的粒子不需要激光纹理，释放或重建纹理也不能把正在飘的粒子清掉。
	}

	bool InkRenderer::CreateSizeDependentResources(IDXGISwapChain1* swapChain, UINT width, UINT height)
	{
		if (!device || !context || width == 0 || height == 0) return false;
		if (swapChain)
		{
			if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
				reinterpret_cast<void**>(backBufferTexture.ReleaseAndGetAddressOf())))) return false;
		}
		else
		{
			// 无窗口测试仍创建正式 renderer 的全部离屏资源，不需要 HWND 或 Present。
			D3D11_TEXTURE2D_DESC offscreen = {};
			offscreen.Width = width; offscreen.Height = height;
			offscreen.MipLevels = 1; offscreen.ArraySize = 1;
			offscreen.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			offscreen.SampleDesc.Count = 1;
			offscreen.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			if (FAILED(device->CreateTexture2D(&offscreen, nullptr,
				backBufferTexture.ReleaseAndGetAddressOf()))) return false;
		}
		if (FAILED(device->CreateRenderTargetView(backBufferTexture.Get(), nullptr,
			backBufferRTV.ReleaseAndGetAddressOf()))) return false;

		D3D11_TEXTURE2D_DESC textureDescription = {};
		textureDescription.Width = width;
		textureDescription.Height = height;
		textureDescription.MipLevels = 1;
		textureDescription.ArraySize = 1;
		textureDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		textureDescription.SampleDesc.Count = 1;
		textureDescription.Usage = D3D11_USAGE_DEFAULT;
		textureDescription.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

		if (FAILED(device->CreateTexture2D(&textureDescription, nullptr, layerL2Texture.ReleaseAndGetAddressOf()))) return false; // L2 保存已经落定的完整画布。

		D3D11_RENDER_TARGET_VIEW_DESC renderTargetDescription = {};
		renderTargetDescription.Format = textureDescription.Format;
		renderTargetDescription.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		if (FAILED(device->CreateRenderTargetView(layerL2Texture.Get(), &renderTargetDescription, layerL2RTV.ReleaseAndGetAddressOf()))) return false;
		// 快照是可选视觉兜底；分配失败时权威 L2 与逐 tile 恢复仍可继续。
		CreateTrustedL2SnapshotResources(width, height);
		if (!CreateOperatorLayerResources(width, height, layerL1)) return false; // L1 保存当前笔画已确认前缀操作。
		if (!CreateOperatorLayerResources(width, height, layerL0)) return false; // L0 保存每帧变化的笔锋和预测操作。
		// 可选 Laser 主体由工具选择控制；普通画布不预留整屏激光纹理。

		SetScreenSize(static_cast<float>(width), static_cast<float>(height));
		return true;
	}

	void InkRenderer::ReleaseSizeDependentResources()
	{
		if (context)
		{
			ID3D11ShaderResourceView* nullResources[] = {
				nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
			context->OMSetRenderTargets(0, nullptr, nullptr);
			context->VSSetShaderResources(0, ARRAYSIZE(nullResources), nullResources); // t8 粒子 SRV 也随 Resize 显式解绑。
			context->PSSetShaderResources(0, ARRAYSIZE(nullResources), nullResources); // 释放前先解绑，避免 D3D 仍持有引用。
			context->Flush();
		}
		backBufferRTV.Reset();
		backBufferTexture.Reset();
		ReleaseTrustedL2SnapshotResources();
		shapeMaterialLayer_ = {};
		shapeCompositeLayer_ = {};
		layerL2RTV.Reset();
		layerL2Texture.Reset();
		layerL1.addRTV.Reset();
		layerL1.addSRV.Reset();
		layerL1.addTexture.Reset();
		layerL1.retainRTV.Reset();
		layerL1.retainSRV.Reset();
		layerL1.retainTexture.Reset();
		layerL0.addRTV.Reset();
		layerL0.addSRV.Reset();
		layerL0.addTexture.Reset();
		layerL0.retainRTV.Reset();
		layerL0.retainSRV.Reset();
		layerL0.retainTexture.Reset();
		l0Content_ = l1Content_ = {};
		ReleaseLaserResources();
		ReleaseCanvasClearSnapshot(); // 与画布同尺寸，尺寸变化后不能再用。
	}

	void InkRenderer::ReleaseResources()
	{
		laserParticleSystem_.Release();
		ReleaseSizeDependentResources();
		shapeVertexShader_.Reset();
		shapePixelShader_.Reset();
		shapeCB_.Reset();
		shapeOrderedBlend_.Reset();
		shapeRenderCache_.clear();
		vertexShader.Reset();
		pixelShader.Reset();
		globalCB.Reset();
		laserStyleCB.Reset();
		inkDataBuffer.Reset();
		inkDataSRV.Reset();
		compositeVertexBuffer.Reset();
		compositeVertexSRV.Reset();
		highlighterPrimitiveBuffer.Reset();
		highlighterPrimitiveSRV.Reset();
		operatorSampler.Reset();
		InvalidateEraserCursorCache();
		eraserCursorSampler_.Reset();
		ReleaseTrustedL2SnapshotPipeline();
		ReleaseDebugOverlayResources();
		whiteboardPatternUnderBlendState_.Reset();
		whiteboardPatternPrimitives_.clear();
		whiteboardPatternPrimitives_.shrink_to_fit();
		strokeOperatorBlendState.Reset();
		operatorResolveBlendState.Reset();
		laserCoverageBlendState.Reset();
		rasterState.Reset();
		laserScissorRasterState.Reset();
		dsState.Reset();
		videoMemoryAdapter_.Reset();
		context1_.Reset();
		device.Reset();
		context.Reset();
		laserStyleCacheValid_ = false;
		uploadedLaserStyleGeneration_ = 0;
		uploadedLaserStyleOpacity_ = -1.0f;
		laserIncrementalCoverageEnabled_ = false;
		laserIncrementalCoverageUnavailable_ = false;
		m_bufferHead = 0;
		mapNoOverwriteOnDynamicBufferSRV = false;
		useDefaultStructuredUpload_ = false;
		viewportWidth = 0.0f;
		viewportHeight = 0.0f;
	}

	bool InkRenderer::Resize(IDXGISwapChain1* swapChain, UINT width, UINT height)
	{
		if (width == 0 || height == 0) return false;
		DXGI_SWAP_CHAIN_DESC1 swapChainDescription = {};
		// ULW 与无窗口测试没有交换链，最终目标是独立纹理：跳过 GetDesc1/ResizeBuffers，其余保留逻辑相同。
		if (swapChain && FAILED(swapChain->GetDesc1(&swapChainDescription))) return false;

		const UINT oldWidth = static_cast<UINT>(viewportWidth);
		const UINT oldHeight = static_cast<UINT>(viewportHeight);
		Microsoft::WRL::ComPtr<ID3D11Texture2D> oldL2Texture = layerL2Texture; // 临时保留旧稳定层用于拷贝。
		Microsoft::WRL::ComPtr<ID3D11Texture2D> oldL1AddTexture = layerL1.addTexture;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> oldL1RetainTexture = layerL1.retainTexture; // L1 的完整仿射操作都要保留。
		const bool restoreLaserIncremental = LaserIncrementalCoverageAvailable();
		Microsoft::WRL::ComPtr<ID3D11Texture2D> oldLaserCompositedTexture =
			laserCompositedColor.texture;
		const RECT oldLaserWrittenBounds = laserCompositedColor.writtenBounds;
		const bool oldLaserContentUnknown = laserCompositedColor.contentUnknown;
		ReleaseSizeDependentResources();

		// waitable swapchain 在部分驱动上要求 resize 时原样保留 BufferCount/Format/Flags。
		if (swapChain && FAILED(swapChain->ResizeBuffers(swapChainDescription.BufferCount, width, height,
			swapChainDescription.Format, swapChainDescription.Flags))) return false;
		if (!CreateSizeDependentResources(swapChain, width, height)) return false;
		if (oldLaserCompositedTexture && EnsureLaserResources(width, height) && restoreLaserIncremental)
			EnsureLaserIncrementalCoverageResources();

		ClearRTV(layerL2RTV.Get(), kTransparentLayerClearColor);
		ClearOperatorLayer(layerL1);
		ClearOperatorLayer(layerL0);
		ClearAllLaserCoverage();
		ClearRTV(backBufferRTV.Get(), kTransparentLayerClearColor);

		// 缩放后只保留新旧画布左上角的交集，不拉伸已有内容。
		const UINT copyWidth = std::min(oldWidth, width);
		const UINT copyHeight = std::min(oldHeight, height);
		if (copyWidth > 0 && copyHeight > 0)
		{
			RECT keepRect = { 0, 0, static_cast<LONG>(copyWidth), static_cast<LONG>(copyHeight) };
			if (oldL2Texture) CopyResource(layerL2Texture.Get(), oldL2Texture.Get(), keepRect); // 保留已完成笔迹。
			if (oldL1AddTexture) CopyResource(layerL1.addTexture.Get(), oldL1AddTexture.Get(), keepRect);
			if (oldL1RetainTexture) CopyResource(layerL1.retainTexture.Get(), oldL1RetainTexture.Get(), keepRect); // 保留正在绘制的稳定前缀。
			MarkOperatorLayerUntracked(layerL1); // 拷贝绕过了绑定记账：L1 内容范围未知，下一次清除必须整屏。
			if (oldLaserCompositedTexture && laserCompositedColor.texture)
			{
				CopyResource(laserCompositedColor.texture.Get(),
					oldLaserCompositedTexture.Get(), keepRect);
				// 新纹理在复制区之外已清零。旧内容未知时，整个复制区都可能有像素。
				if (oldLaserContentUnknown) laserCompositedColor.writtenBounds = keepRect;
				else IntersectRect(&laserCompositedColor.writtenBounds, &oldLaserWrittenBounds, &keepRect);
			}
		}
		return true;
	}

	bool InkRenderer::Init(ID3D11Device* inDevice, ID3D11DeviceContext* inContext, IDXGISwapChain1* swapChain, UINT width, UINT height, D3D_DRIVER_TYPE driver,
		bool enableZhaoxinCompatibility)
	{
		device = inDevice; // 渲染器只借用外部统一创建的 D3D 设备。
		context = inContext;
		context1_.Reset();
		useDefaultStructuredUpload_ = false;
		if (!device || !context)
		{
			mapNoOverwriteOnDynamicBufferSRV = false;
			return false;
		}
		D3D11_FEATURE_DATA_D3D11_OPTIONS deviceOptions = {};
		// Win7 Platform Update/旧驱动可拒绝动态 SRV 的 NO_OVERWRITE；按本次设备查询，失败即回退。
		const bool deviceOptionsAvailable = SUCCEEDED(device->CheckFeatureSupport(
			D3D11_FEATURE_D3D11_OPTIONS, &deviceOptions, sizeof(deviceOptions)));
		mapNoOverwriteOnDynamicBufferSRV = deviceOptionsAvailable &&
			deviceOptions.MapNoOverwriteOnDynamicBufferSRV != FALSE;
		// L0 局部清除需要 ID3D11DeviceContext1 且驱动声明支持 ClearView（Win7 平台更新下旧驱动可能有接口无能力）；
		// 任一缺失时 context1_ 保持为空，局部清除退回整屏清除。
		if (deviceOptionsAvailable && deviceOptions.ClearView != FALSE) context.As(&context1_);
		Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
		Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
		if (SUCCEEDED(device.As(&dxgiDevice)) && dxgiDevice &&
			SUCCEEDED(dxgiDevice->GetAdapter(adapter.ReleaseAndGetAddressOf())) && adapter)
			adapter.As(&videoMemoryAdapter_); // Win7/旧 DXGI 查询失败时仅禁用显存指标。
		DXGI_ADAPTER_DESC adapterDescription = {};
		const HRESULT adapterResult = adapter ? adapter->GetDesc(&adapterDescription) : E_NOINTERFACE;
		const bool adapterIdentityAvailable = SUCCEEDED(adapterResult);
		// 按实际绘图 GPU 选择，不把兆芯 CPU + 其他显卡误判为兼容设备。
		useDefaultStructuredUpload_ = UseZhaoxinDefaultUpload(enableZhaoxinCompatibility,
			driver, adapterIdentityAvailable, adapterDescription.VendorId);
		{
			// 每次设备初始化记一条：用于统计有多少设备落在“整屏清除”一档（无 ClearView 能力）。
			char message[160] = {};
			std::snprintf(message, sizeof(message),
				"Draw3 操作层局部清除：ClearView=%d options=%d driver=%d vendor=0x%04X",
				context1_ ? 1 : 0, deviceOptionsAvailable ? 1 : 0, static_cast<int>(driver),
				adapterIdentityAvailable ? static_cast<unsigned int>(adapterDescription.VendorId) : 0u);
			(void)Inkeys::Log::TryWrite(Inkeys::Log::Level::Info, Inkeys::Log::Module::Draw3, std::nullopt, message);
		}
		if (!CreateSizeDependentResources(swapChain, width, height)) return false;
		return CreateFixedResources(driver);
	}

	bool InkRenderer::CreateFixedResources(D3D_DRIVER_TYPE driver)
	{
		D3D11_BUFFER_DESC constantBufferDescription = {
			sizeof(GlobalShaderConstants), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER,
			D3D11_CPU_ACCESS_WRITE, 0, 0
		};
		if (FAILED(device->CreateBuffer(&constantBufferDescription, nullptr, globalCB.ReleaseAndGetAddressOf()))) return false;
		constantBufferDescription.ByteWidth = sizeof(LaserStyleConstants);
		if (FAILED(device->CreateBuffer(&constantBufferDescription, nullptr,
			laserStyleCB.ReleaseAndGetAddressOf()))) return false;
		ConfigureLaserStyle(1.0f,
			DirectX::XMFLOAT4(1.0f, 16.0f / 255.0f, 0.0f, 1.0f));

		D3D11_BLEND_DESC blendDescription = {};
		blendDescription.IndependentBlendEnable = TRUE;
		blendDescription.RenderTarget[0].BlendEnable = TRUE;
		// Add 取最大值，避免同一墨迹的胶囊段在抗锯齿边缘重复叠加。
		blendDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[0].BlendOp = D3D11_BLEND_OP_MAX;
		blendDescription.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_MAX;
		blendDescription.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		// Retain 从 1 开始取最小值，与 Add 的最大覆盖率保持同一个并集。
		blendDescription.RenderTarget[1].BlendEnable = TRUE;
		blendDescription.RenderTarget[1].SrcBlend = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[1].DestBlend = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[1].BlendOp = D3D11_BLEND_OP_MIN;
		blendDescription.RenderTarget[1].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[1].DestBlendAlpha = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[1].BlendOpAlpha = D3D11_BLEND_OP_MIN;
		blendDescription.RenderTarget[1].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED;
		if (FAILED(device->CreateBlendState(&blendDescription, strokeOperatorBlendState.ReleaseAndGetAddressOf()))) return false;

		blendDescription = {};
		blendDescription.RenderTarget[0].BlendEnable = TRUE;
		// PS 的 SV_Target1 提供 Retain，双源混合一次计算 Add + Retain * Destination。
		blendDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[0].DestBlend = D3D11_BLEND_SRC1_COLOR;
		blendDescription.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blendDescription.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_SRC1_ALPHA;
		blendDescription.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blendDescription.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (FAILED(device->CreateBlendState(&blendDescription, operatorResolveBlendState.ReleaseAndGetAddressOf()))) return false;

		blendDescription = {};
		blendDescription.RenderTarget[0].BlendEnable = TRUE;
		blendDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[0].BlendOp = D3D11_BLEND_OP_MAX;
		blendDescription.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_MAX;
		blendDescription.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (FAILED(device->CreateBlendState(&blendDescription,
			laserCoverageBlendState.ReleaseAndGetAddressOf()))) return false;

		D3D11_DEPTH_STENCIL_DESC depthStencilDescription = {};
		if (FAILED(device->CreateDepthStencilState(&depthStencilDescription, dsState.ReleaseAndGetAddressOf()))) return false;

		D3D11_RASTERIZER_DESC rasterizerDescription = {};
		rasterizerDescription.FillMode = D3D11_FILL_SOLID;
		rasterizerDescription.CullMode = D3D11_CULL_NONE;
		rasterizerDescription.DepthClipEnable = TRUE;
		if (FAILED(device->CreateRasterizerState(&rasterizerDescription, rasterState.ReleaseAndGetAddressOf()))) return false;
		rasterizerDescription.ScissorEnable = TRUE;
		if (FAILED(device->CreateRasterizerState(&rasterizerDescription,
			laserScissorRasterState.ReleaseAndGetAddressOf())))
		{
			// scissor 只减少激光局部 pass 的像素工作，创建失败不应阻断 renderer 初始化。
			laserScissorRasterState.Reset();
		}

		D3D11_BUFFER_DESC inkBufferDescription = {};
		inkBufferDescription.ByteWidth = static_cast<UINT>(kMaxBufferCapacity * sizeof(InkPoint));
		inkBufferDescription.Usage = useDefaultStructuredUpload_ ? D3D11_USAGE_DEFAULT : D3D11_USAGE_DYNAMIC;
		inkBufferDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		inkBufferDescription.CPUAccessFlags = useDefaultStructuredUpload_ ? 0 : D3D11_CPU_ACCESS_WRITE;
		inkBufferDescription.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		inkBufferDescription.StructureByteStride = sizeof(InkPoint);
		if (FAILED(device->CreateBuffer(&inkBufferDescription, nullptr, inkDataBuffer.ReleaseAndGetAddressOf()))) return false; // CPU 每帧写点，GPU 着色器按结构化缓冲区读取。

		D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceDescription = {};
		shaderResourceDescription.Format = DXGI_FORMAT_UNKNOWN;
		shaderResourceDescription.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		shaderResourceDescription.Buffer.NumElements = static_cast<UINT>(kMaxBufferCapacity);
		if (FAILED(device->CreateShaderResourceView(inkDataBuffer.Get(), &shaderResourceDescription, inkDataSRV.ReleaseAndGetAddressOf()))) return false;

		D3D11_BUFFER_DESC highlighterBufferDescription = inkBufferDescription;
		highlighterBufferDescription.ByteWidth = static_cast<UINT>(kMaxBufferCapacity * sizeof(HighlighterPrimitive));
		highlighterBufferDescription.StructureByteStride = sizeof(HighlighterPrimitive);
		if (FAILED(device->CreateBuffer(&highlighterBufferDescription, nullptr,
			highlighterPrimitiveBuffer.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(device->CreateShaderResourceView(highlighterPrimitiveBuffer.Get(), &shaderResourceDescription,
			highlighterPrimitiveSRV.ReleaseAndGetAddressOf()))) return false; // 荧光笔几何单独上传，避免改变普通墨迹点布局。

		// 合成矩形只需两个点，专用小动态资源避免 DISCARD 整个 3.2 MB 墨迹缓冲区。
		D3D11_BUFFER_DESC compositeDescription = inkBufferDescription;
		compositeDescription.ByteWidth = 256;
		compositeDescription.Usage = D3D11_USAGE_DYNAMIC;
		compositeDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(device->CreateBuffer(&compositeDescription, nullptr,
			compositeVertexBuffer.ReleaseAndGetAddressOf()))) return false;
		shaderResourceDescription.Buffer.NumElements = compositeDescription.ByteWidth / sizeof(InkPoint);
		if (FAILED(device->CreateShaderResourceView(compositeVertexBuffer.Get(), &shaderResourceDescription,
			compositeVertexSRV.ReleaseAndGetAddressOf()))) return false;

		D3D11_SAMPLER_DESC samplerDescription = {};
		samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; // 混合图层按像素采样，不做线性模糊。
		samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDescription.ComparisonFunc = D3D11_COMPARISON_NEVER;
		samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(device->CreateSamplerState(&samplerDescription, operatorSampler.ReleaseAndGetAddressOf()))) return false;
		// 精灵独占线性 clamp 采样器，不能改变操作层的 point 采样。
		samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		if (FAILED(device->CreateSamplerState(&samplerDescription,
			eraserCursorSampler_.ReleaseAndGetAddressOf()))) return false;
		CreateTrustedL2SnapshotPipeline(); // 可选 pass 失败时仅禁用动态快照兜底。
		return LoadShaders(driver) && EnsureShapePipeline();
	}

	bool InkRenderer::LoadShaders(D3D_DRIVER_TYPE driver)
	{
		const ShaderBlob vertexShaderBlob =
			LoadShaderFromResource(IDR_DRAW3_INK_VERTEX_SHADER);
		const bool diagnostics = ExactDiagnosticEnvironmentEnabled(L"INKEYS_DRAW3_WIN7_DIAGNOSTICS");
		wchar_t guardedOverride[2] = {};
		const DWORD overrideLength = GetEnvironmentVariableW(L"INKEYS_DRAW3_WIN7_GUARDED_SHADER",
			guardedOverride, ARRAYSIZE(guardedOverride));
		// 复用 RtlGetVersion 的真实版本查询，不依赖正常启动才赋值的全局版本状态。
		const auto version = GetWindowsVersion();
		const bool guarded = shader_compatibility::UseGuardedPixelShader(
			version.majorVersion, version.minorVersion, driver, diagnostics,
			overrideLength == 1 ? std::wstring_view(guardedOverride, 1) : std::wstring_view{});
		const ShaderBlob pixelShaderBlob = guarded
			? ShaderBlob{ Draw3Win7GuardedShader::Bytecode, sizeof(Draw3Win7GuardedShader::Bytecode) }
			: LoadShaderFromResource(IDR_DRAW3_INK_PIXEL_SHADER);
		if (!vertexShaderBlob.data || !pixelShaderBlob.data) return false;
		if (FAILED(device->CreateVertexShader(vertexShaderBlob.data, vertexShaderBlob.size, nullptr, vertexShader.ReleaseAndGetAddressOf()))) return false;
		const HRESULT pixelResult = device->CreatePixelShader(pixelShaderBlob.data, pixelShaderBlob.size, nullptr, pixelShader.ReleaseAndGetAddressOf());
		if (diagnostics) ReportSelectedPixelShader(pixelShaderBlob, guarded, pixelResult);
		if (FAILED(pixelResult)) return false; // 不静默切回原版，否则会把候选创建失败误记为实测通过。
		// 粒子系统在这里只登记字节码；着色器与粒子池等到第一次有粒子时才创建。
		const ShaderBlob particleVertexBlob =
			LoadShaderFromResource(IDR_DRAW3_LASER_PARTICLE_VS);
		const ShaderBlob particlePixelBlob =
			LoadShaderFromResource(IDR_DRAW3_LASER_PARTICLE_PS);
		const ShaderBlob particleSpawnBlob =
			LoadShaderFromResource(IDR_DRAW3_LASER_PARTICLE_SPAWN_CS);
		const bool particlesReady = particleVertexBlob.data && particlePixelBlob.data &&
			laserParticleSystem_.Initialize(device.Get(), context.Get(),
				{ particleVertexBlob.data, particleVertexBlob.size },
				{ particlePixelBlob.data, particlePixelBlob.size },
				{ particleSpawnBlob.data, particleSpawnBlob.size });
		if (!particlesReady)
			ReportLaserParticleUnavailableOnce();
		return true;
	}

	int RunLaserResourceLifecycleValidation() noexcept
	{
#if !INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
		return 2;
#else
		try
		{
			int failures = 0;
			constexpr auto lowTier = kLaserParticleLimitTiers[static_cast<size_t>(LaserParticleLimit::Low)];
			constexpr auto mediumCapacity = kLaserParticleLimitTiers[static_cast<size_t>(LaserParticleLimit::Medium)].poolCapacity;
			constexpr auto trailPass = LaserParticlePass::OverBody;
			constexpr auto driftPass = LaserParticlePass::UnderBody;
			const auto check = [&](bool condition, const char* name)
			{
				if (!condition) { ++failures; std::fprintf(stderr, "[LaserResources] FAIL %s\n", name); }
				return condition;
			};
			// 逐批覆盖两种风向和三种缩放，核对所有字段及整数像素包络。
			{
				std::vector<LaserDriftParticle> records;
				std::vector<LaserParticleSeed> reference;
				for (float dpiScale : { 1.0f, 1.25f, 2.0f })
				{
					for (float windAngle : { kLaserDissolveParameters.leftUpWindAngleDegrees,
						kLaserDissolveParameters.rightUpWindAngleDegrees })
					{
						const auto drift = MakeDriftParticleContext(dpiScale, windAngle);
						LaserParticleBatchEnvelope envelope;
						records.clear();
						reference.clear();
						for (uint32_t i = 0; i < 2048; ++i)
						{
							const float x = 0.25f * static_cast<float>(i * 919 % 15433) - 256.0f;
							const float y = 0.125f * static_cast<float>(i * 677 % 11311) - 128.0f;
							const float white = static_cast<float>(i % 129) / 128.0f;
							const float color[4] = { static_cast<float>(i % 101) / 100.0f, white, 1.0f - white,
								static_cast<float>(i % 256) / 255.0f };
							const uint32_t group = i % 7, delay = i % 421, random = i * 0x9E3779B9u;
							records.push_back(MakeDriftParticle(drift, envelope, x, y, white, color, random, group, delay));
							AppendReferenceDriftParticleSeed(reference, x, y, white, dpiScale, color, random, group, windAngle, delay);
						}
						check(MatchesReferenceDriftBatch(records, reference, envelope), "drift fields and batch envelope match the seed path");
					}
				}
			}
			const auto referenceDriftRecord = [](const LaserParticleSeed& seed)
			{
				return LaserDriftParticle{ PackLaserParticle(seed, 0, 0), seed.group };
			};
			Microsoft::WRL::ComPtr<ID3D11Device> testDevice;
			Microsoft::WRL::ComPtr<ID3D11DeviceContext> testContext;
			constexpr D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
			D3D_FEATURE_LEVEL level = {};
			if (!check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
				D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
				testDevice.GetAddressOf(), &level, testContext.GetAddressOf())), "real WARP device")) return 1;
			Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
			Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
			Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
			if (!check(SUCCEEDED(testDevice.As(&dxgiDevice)) &&
				SUCCEEDED(dxgiDevice->GetAdapter(adapter.GetAddressOf())) &&
				SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf()))), "composition factory without HWND")) return 1;
			DXGI_SWAP_CHAIN_DESC1 description = {};
			description.Width = description.Height = 64;
			description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			description.SampleDesc.Count = 1;
			description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			description.BufferCount = 2;
			description.Scaling = DXGI_SCALING_STRETCH;
			description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
			description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
			Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
			if (!check(SUCCEEDED(factory->CreateSwapChainForComposition(testDevice.Get(), &description,
				nullptr, swapChain.GetAddressOf())), "offscreen composition swapchain")) return 1;
			InkRenderer renderer;
			if (!check(renderer.Init(testDevice.Get(), testContext.Get(), swapChain.Get(),
				64, 64, D3D_DRIVER_TYPE_WARP, true), "normal renderer initialization")) return 1;
			const auto report = [&](const char* stage)
			{
				std::fprintf(stdout, "LaserResources stage=%s resource_bytes=%llu width=%.0f height=%.0f hwnd=0\n",
					stage, static_cast<unsigned long long>(renderer.LaserResourceBytes()),
					renderer.viewportWidth, renderer.viewportHeight);
			};
			const auto pixels = [&](ID3D11Texture2D* texture)
			{
				std::vector<unsigned char> result;
				if (!texture) { check(false, "readback source exists"); return result; }
				D3D11_TEXTURE2D_DESC readDescription = {};
				texture->GetDesc(&readDescription);
				readDescription.Usage = D3D11_USAGE_STAGING;
				readDescription.BindFlags = 0;
				readDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				Microsoft::WRL::ComPtr<ID3D11Texture2D> readback;
				if (!check(SUCCEEDED(testDevice->CreateTexture2D(&readDescription, nullptr,
					readback.GetAddressOf())), "real staging texture")) return result;
				testContext->OMSetRenderTargets(0, nullptr, nullptr);
				testContext->CopyResource(readback.Get(), texture);
				D3D11_MAPPED_SUBRESOURCE mapped = {};
				if (!check(SUCCEEDED(testContext->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
					"real pixel readback")) return result;
				result.resize(static_cast<size_t>(readDescription.Width) * readDescription.Height * 4);
				for (UINT row = 0; row < readDescription.Height; ++row)
					std::memcpy(result.data() + static_cast<size_t>(row) * readDescription.Width * 4,
						static_cast<const unsigned char*>(mapped.pData) + static_cast<size_t>(row) * mapped.RowPitch,
						static_cast<size_t>(readDescription.Width) * 4);
				testContext->Unmap(readback.Get(), 0);
				return result;
			};
			check(!renderer.LaserResourcesAvailable() && renderer.LaserResourceBytes() == 0,
				"ordinary startup owns no Laser textures");
			report("ordinary_init");
			check(renderer.Resize(swapChain.Get(), 80, 72) && renderer.LaserResourceBytes() == 0 &&
				renderer.Resize(swapChain.Get(), 64, 64) && renderer.LaserResourceBytes() == 0,
				"ordinary resize never creates Laser storage");
			renderer.ClearOperatorLayer(renderer.layerL1);
			renderer.ClearOperatorLayer(renderer.layerL0);
			renderer.ClearRTV(renderer.backBufferRTV.Get(), kTransparentLayerClearColor);
			renderer.SetOperatorTarget(renderer.layerL1);
			const std::array<InkPoint, 2> pen{ InkPoint{ 8, 8, 2, 0 }, InkPoint{ 40, 8, 2, 1 } };
			check(renderer.DrawStroke(pen, DirectX::XMFLOAT4(1, 0, 0, 1)) == 0 &&
				renderer.ApplyOperatorLayers(renderer.backBufferRTV.Get(), renderer.layerL1, renderer.layerL0,
					{ 0, 0, 64, 64 }), "ordinary Pen composite without Laser");
			const auto ordinaryPixels = pixels(renderer.backBufferTexture.Get());
			check(!ordinaryPixels.empty() && ordinaryPixels[(8 * 64 + 20) * 4 + 3] > 240,
				"ordinary Pen produces actual opaque pixels");
			renderer.failLaserPreparationAfterColor_ = true;
			check(!renderer.EnsureLaserResources(64, 64) && renderer.LaserResourceBytes() == 0,
				"partial allocation failure publishes no Laser group");
			renderer.failLaserPreparationAfterColor_ = false;
			{
				LaserParticleActiveRing ring;
				if (!check(ring.Initialize(lowTier.poolCapacity), "particle ring metadata allocation")) return 1;
				ring.Append(lowTier.poolCapacity - 2, 10);
				ring.Expire(10);
				ring.Append(4, 20);
				check(ring.Range().start == lowTier.poolCapacity - 2 && ring.Range().count == 4 &&
					ring.Cursor() == 2, "particle range crosses the ring end");
				ring.Append(lowTier.poolCapacity - 2, 30);
				check(ring.Range().start == 0 && ring.Range().count == lowTier.poolCapacity,
					"particle ring caps an overwrite and keeps the surviving old slots");
				ring.Expire(20);
				check(ring.Range().start == 2 && ring.Range().count == lowTier.poolCapacity - 2,
					"expired front batch contracts the ring");
				ring.Append(2, 25);
				ring.Expire(25);
				check(ring.Range().start == 2 && ring.Range().count == lowTier.poolCapacity - 2,
					"expired tail batch contracts the ring");
				ring.Expire(30);
				check(ring.Range().count == 0, "expired particle ring is empty");

				check(!ring.Initialize(0) && !ring.Initialize(63) &&
					!ring.Initialize(kLaserParticleMaximumCapacity * 2u), "ring rejects invalid capacities");
				for (const uint32_t capacity : { 64u, mediumCapacity })
				{
					if (!check(ring.Initialize(capacity), "non-default ring capacity")) return 1;
					ring.Append(capacity - 3, 10);
					ring.Expire(10);
					ring.Append(1, 100);
					const auto dissolveId = ring.Append(4, 100, true);
					auto latest = ring.LatestDissolveRange();
					check(ring.Range().start == capacity - 3 && ring.Range().count == 5 && ring.Cursor() == 2 &&
						latest.start == capacity - 2 && latest.count == 4, "dissolve wraps in a non-default ring");

					ring.Append(3, 20);
					const bool trimmedBehindTail = ring.TrimTail(dissolveId, 2);
					latest = ring.LatestDissolveRange();
					check(!trimmedBehindTail && ring.Cursor() == 5 && ring.Range().count == 8 &&
						latest.start == capacity - 2 && latest.count == 4,
						"trim leaves a dissolve batch behind the ring tail untouched");
					ring.Expire(20);
					check(!ring.TrimTail(dissolveId, 5) && ring.Cursor() == 2 && ring.LatestDissolveRange().count == 4,
						"trim cannot grow the tail batch");
					const bool trimmed = ring.TrimTail(dissolveId, 3);
					latest = ring.LatestDissolveRange();
					check(trimmed && ring.Cursor() == 1 && ring.Range().start == capacity - 3 &&
						ring.Range().count == 4 && latest.start == capacity - 2 && latest.count == 3,
						"trim shortens the tail batch and backs up the cursor");
					check(ring.TrimTail(dissolveId, 0) && ring.Cursor() == capacity - 2 && ring.Range().count == 1 &&
						ring.LatestDissolveRange().count == 0 && !ring.TrimTail(dissolveId, 0),
						"zero trim removes the dissolve batch and leaves the preceding batch");

					ring.Append(4, 100, true);
					ring.Append(capacity, 100);
					check(ring.Range().start == 2 && ring.Range().count == capacity && ring.Cursor() == 2 &&
						ring.LatestDissolveRange().count == 0, "full overwrite evicts the old dissolve batch");
				}

				ring.Clear();
				const auto evicted = ring.Append(3, 40, true);
				for (uint32_t i = 1; i < kLaserParticleMaximumBatches; ++i) ring.Append(1, 40);
				check(ring.Range().start == 0 && ring.Range().count == kLaserParticleMaximumBatches + 2 &&
					ring.LatestDissolveRange().count == 3, "all batch entries may be occupied");
				const auto reused = ring.Append(2, 50);
				check(evicted.index == reused.index && evicted.generation != reused.generation &&
					!ring.TrimTail(evicted, 0), "a stale batch id cannot trim a reused table entry");
				check(ring.Range().start == 3 && ring.Range().count == kLaserParticleMaximumBatches + 1 &&
					ring.LatestDissolveRange().count == 0, "full batch table evicts the entire oldest batch");
				ring.Expire(40);
				check(ring.Range().start == kLaserParticleMaximumBatches + 2 && ring.Range().count == 2,
					"wrapped batch table retains the newest batch after expiry");
				check(ring.TrimTail(reused, 0) && ring.Range().count == 0, "zero trim removes an ordinary tail batch");
			}
			{
				// CPU 上传路径只在出生时写一次记录，状态由顶点着色器求值。
				// 这里先用 CPU 镜像核对运动模型，再用真实绘制核对着色器与镜像一致。
				auto& system = renderer.laserParticleSystem_;
				if (!check(renderer.LaserParticlesAvailable() && !renderer.HasActiveLaserParticles() &&
					!system.ResourcesCreated(trailPass) && !system.ResourcesCreated(driftPass),
					"particle pools allocate nothing before the first particle")) return 1;
				const LaserParticleConfig config;
				const float trailColor[] = { 0.15f, 0.5f, 1.0f, 1.0f };
				std::vector<LaserParticleSeed> seeds;
				AppendLaserTrailParticleSeeds(seeds, 30, 30, 1, 0, 2, 1.0f, trailColor, 3, 76, 14, config);
				if (!check(seeds.size() == 3, "trail seeds")) return 1;
				check(std::abs(seeds[0].velocity[1]) > std::abs(seeds[0].velocity[0]) &&
					std::abs(std::abs(seeds[0].position[1] - 30.0f) - (2.0f * config.trailBirthOffsetRatio + seeds[0].radius)) < 0.0001f &&
					(seeds[0].position[1] - 30.0f) * seeds[0].velocity[1] > 0.0f &&
					seeds[0].radius >= config.minimumRadiusDip && seeds[0].radius <= config.maximumRadiusDip,
					"trail is born at the body edge and launches outward; size stays in the configured range");
				{
					const LaserGpuParticle packed = PackLaserParticle(seeds[0], 1000, 0);
					const float trailLife = seeds[0].lifetimeMilliseconds * 0.001f, trailU = 0.050f / trailLife;
					const float trailTravel = trailLife * (trailU - trailU * trailU * trailU * (1.0f - 0.5f * trailU));
					const auto moved = EvaluateLaserParticle(packed, { 1050, 0, 0, 1.0f }, 1.0f, config);
					check(packed.kindAndBatch == 0 && packed.releaseMilliseconds == 1000 && moved.visible &&
						std::abs(moved.x - (seeds[0].position[0] + seeds[0].velocity[0] * trailTravel)) < 0.001f &&
						std::abs(moved.y - (seeds[0].position[1] + seeds[0].velocity[1] * trailTravel)) < 0.001f,
						"trail deceleration follows the lifetime curve");
					check(!EvaluateLaserParticle(packed, { 999, 0, 0, 1.0f }, 1.0f, config).visible &&
						!EvaluateLaserParticle(packed, { 1000 + seeds[0].lifetimeMilliseconds, 0, 0, 1.0f }, 1.0f, config).visible,
						"unreleased and expired particles are culled by one unsigned comparison");
				}
				{
					// 点击光波是独立图元：CPU 按曲线求值，不占粒子池。
					const float clickRadius = LaserSolidRadius();
					const LaserClickWave wave = { 32.0f, 32.0f, clickRadius, { 1.0f, 0.2f, 0.0f, 1.0f }, 1000, 15 };
					LaserClickWaveVisual ring;
					const float ringFade = 1.0f - 150.0f / config.clickWaveLifetimeMilliseconds;
					check(EvaluateLaserClickWave(wave, 1150, 1000, config, ring) && ring.x == 32.0f && ring.y == 32.0f &&
						std::abs(ring.radius - clickRadius * (1.0f + (1.0f - ringFade * ringFade * ringFade) *
							config.clickWaveExpansionRatio)) < 0.01f &&
						std::abs(ring.halfWidth - 0.5f * config.clickWaveLineWidthRatio * clickRadius * ringFade) < 0.001f &&
						std::abs(ring.opacity - 0.9f * ringFade * ringFade) < 0.001f && ring.color[1] == 0.2f,
						"wave radius, line width and opacity follow the configured curves");
					check(!EvaluateLaserClickWave(wave, 1000 + config.clickWaveLifetimeMilliseconds, 1000, config, ring),
						"the wave ends at its lifetime");
					const RECT waveBounds = LaserClickWaveBounds(wave, config);
					check(waveBounds.left < 32 - static_cast<LONG>(clickRadius * config.clickWaveExpansionRatio) &&
						waveBounds.right > 32 + static_cast<LONG>(clickRadius * config.clickWaveExpansionRatio),
						"wave bounds cover the fully expanded ring");
					// 真实绘制：环在半径处有颜色，圆心是空的；整个过程粒子池没有被创建。
					const LaserClickWaveVisual drawn = { 32.0f, 32.0f, 12.0f, 1.5f, { 1.0f, 0.0f, 0.0f }, 1.0f };
					renderer.ClearRTV(renderer.backBufferRTV.Get(), { 0.0f, 0.0f, 0.0f, 0.0f });
					renderer.DrawLaserClickWaves(std::span<const LaserClickWaveVisual>(&drawn, 1));
					const auto wavePixels = pixels(renderer.backBufferTexture.Get());
					const size_t onRing = (32 * 64 + 44) * 4, atCenter = (32 * 64 + 32) * 4;
					check(wavePixels.size() == 64 * 64 * 4 && wavePixels[onRing + 2] > 200 && wavePixels[onRing + 3] > 200 &&
						wavePixels[atCenter + 3] == 0 && !system.ResourcesCreated(trailPass) && !system.ResourcesCreated(driftPass),
						"the wave renders as a ring through the ink pipeline without touching the particle pool");
				}

				renderer.StepLaserParticles(1000, 1000, seeds);
				if (!check(system.ActiveRange(trailPass).count == seeds.size() && system.ResourcesCreated(trailPass) &&
					!system.ResourcesCreated(driftPass),
					"first upload creates the pool and activates only its own slots")) return 1;
				renderer.StepLaserParticles(2000, 1000, {});
				check(!renderer.HasActiveLaserParticles(), "expiry empties the CPU range");
				Microsoft::WRL::ComPtr<ID3D11Query> query;
				const D3D11_QUERY_DESC queryDescription = { D3D11_QUERY_PIPELINE_STATISTICS, 0 };
				if (!check(SUCCEEDED(testDevice->CreateQuery(&queryDescription, query.GetAddressOf())),
					"particle empty-pool statistics query")) return 1;
				D3D11_QUERY_DATA_PIPELINE_STATISTICS stats = {};
				const auto finishQuery = [&]
				{
					testContext->End(query.Get());
					testContext->Flush();
					HRESULT result = S_FALSE;
					for (int attempt = 0; attempt < 1000 && result == S_FALSE; ++attempt)
						result = testContext->GetData(query.Get(), &stats, sizeof(stats), 0);
					return result == S_OK;
				};
				testContext->Begin(query.Get());
				renderer.StepLaserParticles(2001, 1000, {});
				renderer.DrawLaserParticles();
				renderer.DrawLaserParticles(LaserParticlePass::OverBody);
				check(finishQuery() && stats.CSInvocations == 0 && stats.IAVertices == 0,
					"empty pool submits no draw");

				// 游标已跳过第一批；真实 VS 必须加区间起点，PS 必须用每粒自己的颜色。
				std::array<LaserParticleSeed, 2> sprites = {};
				for (auto& sprite : sprites)
				{
					sprite.position[1] = 16.5f;
					sprite.radius = 2.0f;
					sprite.color[3] = 1.0f;
					sprite.lifetimeMilliseconds = 100;
				}
				sprites[0].position[0] = 12.5f;
				sprites[0].color[0] = 1.0f;
				sprites[1].position[0] = 50.5f;
				sprites[1].color[2] = 1.0f;
				renderer.ClearRTV(renderer.backBufferRTV.Get(), { 0.0f, 0.0f, 0.0f, 0.0f });
				renderer.StepLaserParticles(2002, 1000, sprites);
				testContext->Begin(query.Get());
				// 行进粒子只在主体之上那一次绘制里出现；主体之下那一次因为没有飘散粒子而整个跳过。
				renderer.DrawLaserParticles();
				renderer.DrawLaserParticles(LaserParticlePass::OverBody);
				check(finishQuery() && stats.CSInvocations == 0 && stats.IAVertices == 8,
					"two live particles cost one instanced draw of four vertices each and no compute");
				const auto spritePixels = pixels(renderer.backBufferTexture.Get());
				const size_t red = (16 * 64 + 12) * 4, blue = (16 * 64 + 50) * 4;
				check(spritePixels.size() == 64 * 64 * 4 &&
					spritePixels[red + 2] > 200 && spritePixels[red] < 8 &&
					spritePixels[blue] > 200 && spritePixels[blue + 2] < 8,
					"active instances render two independent premultiplied colors");
				renderer.ResetLaserParticles();

				// 着色器与 CPU 镜像的一致性：画出一粒运动中的粒子，量它的透明度重心。
				const auto centroid = [&](float& x, float& y)
				{
					const auto image = pixels(renderer.backBufferTexture.Get());
					double sum = 0.0, sumX = 0.0, sumY = 0.0;
					for (size_t py = 0; py < 64 && image.size() == 64 * 64 * 4; ++py)
						for (size_t px = 0; px < 64; ++px)
						{
							const double alpha = image[(py * 64 + px) * 4 + 3];
							sum += alpha;
							sumX += alpha * (static_cast<double>(px) + 0.5);
							sumY += alpha * (static_cast<double>(py) + 0.5);
						}
					if (sum <= 0.0) return false;
					x = static_cast<float>(sumX / sum);
					y = static_cast<float>(sumY / sum);
					return true;
				};
				const float white[] = { 1.0f, 1.0f, 1.0f, 1.0f };
				{
					LaserParticleSeed mover;
					mover.position[0] = 20.5f;
					mover.position[1] = 20.5f;
					mover.velocity[0] = 40.0f;
					mover.velocity[1] = 10.0f;
					std::copy_n(white, 4, mover.color);
					mover.radius = 2.0f;
					mover.lifetimeMilliseconds = 1000;
					mover.maximumTravelDistance = 0.5f * std::hypot(40.0f, 10.0f);
					renderer.StepLaserParticles(5000, 1000, std::span<const LaserParticleSeed>(&mover, 1));
					renderer.StepLaserParticles(5250, 1000, {});
					renderer.ClearRTV(renderer.backBufferRTV.Get(), { 0.0f, 0.0f, 0.0f, 0.0f });
					renderer.DrawLaserParticles(LaserParticlePass::OverBody);
					const auto expected = EvaluateLaserParticle(PackLaserParticle(mover, 0, 0), system.Clock(),
						system.DipPerPixel(), config);
					float x = 0.0f, y = 0.0f;
					check(expected.visible && expected.x > 28.0f && expected.radius < 2.0f && centroid(x, y) &&
						std::abs(x - expected.x) < 0.2f && std::abs(y - expected.y) < 0.2f,
						"the vertex shader moves a trail particle exactly where the CPU model says");
					renderer.ResetLaserParticles();
				}
				{
					std::vector<LaserDriftParticle> drifting;
					LaserParticleBatchEnvelope envelope;
					const auto drift = MakeDriftParticleContext(1.0f, kLaserDissolveParameters.leftUpWindAngleDegrees);
					AppendLaserDissolveParticle(drifting, envelope, drift, MakeLaserDissolveFront(), 48, 48, 1, 0, 3, 1,
						white, 91, 14);
					auto& particle = drifting.back().particle;
					particle.releaseMilliseconds = 0;
					particle.lifetimeMilliseconds = 2000;
					particle.shapeParameters[0] = 2.0f;
					renderer.StepLaserParticles(6000, 1000, {}, drifting, {}, drift.dipPerPixel);
					renderer.StepLaserParticles(6600, 1000, {});
					renderer.ClearRTV(renderer.backBufferRTV.Get(), { 0.0f, 0.0f, 0.0f, 0.0f });
					renderer.DrawLaserParticles();
					const auto expected = EvaluateLaserParticle(particle, system.Clock(),
						system.DipPerPixel(), config);
					float x = 0.0f, y = 0.0f;
					check(expected.visible && expected.x < particle.position[0] - 4.0f && centroid(x, y) &&
						std::abs(x - expected.x) < 0.2f && std::abs(y - expected.y) < 0.2f,
						"the vertex shader drifts a particle along the wind exactly where the CPU model says");
					renderer.ResetLaserParticles();
				}

				// 撤回、汇聚与停止都由 CPU 改写槽或下发时钟完成：读回粒子池核对被改写的正是该改的槽。
				std::vector<LaserDriftParticle> dust;
				const auto front = MakeLaserDissolveFront();
				const auto dustContext = MakeDriftParticleContext(1.25f, kLaserDissolveParameters.leftUpWindAngleDegrees);
				LaserParticleBatchEnvelope dustEnvelope;
				for (uint32_t i = 0; i < 4; ++i)
				{
					AppendLaserDissolveParticle(dust, dustEnvelope, dustContext, front, 32, 32, 1, 0, 3, 1,
						trailColor, 76 + i, i < 2 ? 14 : 15);
					dust.back().particle.releaseMilliseconds = i % 2 == 0 ? 30 : 300;
					dust.back().particle.lifetimeMilliseconds = 600;
				}
				const auto bytes = static_cast<UINT>(dust.size() * sizeof(LaserGpuParticle));
				D3D11_BUFFER_DESC stagingDescription = {};
				stagingDescription.ByteWidth = bytes;
				stagingDescription.Usage = D3D11_USAGE_STAGING;
				stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				Microsoft::WRL::ComPtr<ID3D11Buffer> staging;
				if (!check(SUCCEEDED(testDevice->CreateBuffer(&stagingDescription, nullptr, staging.GetAddressOf())),
					"particle readback staging buffer")) return 1;
				std::vector<LaserGpuParticle> states(dust.size());
				const auto readParticles = [&]
				{
					const D3D11_BOX range = { 0, 0, 0, bytes, 1, 1 };
					testContext->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, system.ParticleBuffer(driftPass), 0, &range);
					D3D11_MAPPED_SUBRESOURCE mapped = {};
					if (!check(SUCCEEDED(testContext->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
						"particle state readback")) return false;
					std::memcpy(states.data(), mapped.pData, bytes);
					testContext->Unmap(staging.Get(), 0);
					return true;
				};
				const auto evaluate = [&](const LaserGpuParticle& particle)
				{
					return EvaluateLaserParticle(particle, system.Clock(), system.DipPerPixel(), config);
				};
				renderer.StepLaserParticles(3000, 1000, {}, {}, dust, dustContext.dipPerPixel);
				renderer.StepLaserParticles(3150, 1000, {});
				if (!readParticles()) return 1;
				check(evaluate(states[0]).visible && states[1].lifetimeMilliseconds != 0 && !evaluate(states[1]).visible &&
					(states[0].kindAndBatch >> 2) != 0 && states[0].kindAndBatch == states[3].kindAndBatch,
					"dissolve has both released and queued particles in one batch");
				const auto untouched = states[2];
				const auto moving = evaluate(states[2]);
				renderer.RemoveLaserParticleGroup(14);
				if (!readParticles()) return 1;
				check(states[0].lifetimeMilliseconds == 0 && states[1].lifetimeMilliseconds == 0 &&
					states[3].lifetimeMilliseconds != 0 && std::memcmp(&untouched, &states[2], sizeof(untouched)) == 0,
					"undo empties both states of its own group and leaves the other unchanged");
				renderer.StepLaserParticles(3325, 1000, {}, {}, {}, 1.0f, { 3150, 0.5f, false });
				const auto gathered = evaluate(states[2]);
				check(system.Clock().gatherBatch == (states[2].kindAndBatch >> 2) && system.Clock().gatherMilliseconds == 150 &&
					gathered.visible && !evaluate(states[3]).visible &&
					std::abs(gathered.x - std::lerp(moving.x, dust[2].particle.position[0], 0.5f)) < 0.001f &&
					std::abs(gathered.y - std::lerp(moving.y, dust[2].particle.position[1], 0.5f)) < 0.001f,
					"gather uses frozen analytic positions and cancels pending release");
				renderer.StepLaserParticles(3500, 1000, {}, {}, {}, 1.0f, { 3150, 1.0f, true });
				if (!readParticles()) return 1;
				check(states[2].lifetimeMilliseconds == 0 && states[3].lifetimeMilliseconds == 0 &&
					!renderer.HasActiveLaserParticles(), "completed gather empties the remaining slots and the range");
				renderer.ResetLaserParticles();

				// 非零出生时刻和两类同帧批次：普通批保持 0，激光飘散批分配批号。
				const std::span<const LaserDriftParticle> records = dust;
				renderer.StepLaserParticles(4000, 1000, {});
				renderer.StepLaserParticles(4050, 1000, {}, records.first(2), records.last(2), dustContext.dipPerPixel);
				if (!readParticles()) return 1;
				bool uploadMatches = system.DipPerPixel() == dustContext.dipPerPixel;
				const auto uploadedBatch = states[2].kindAndBatch >> 2;
				for (size_t i = 0; i < dust.size(); ++i)
				{
					auto expected = dust[i].particle;
					expected.releaseMilliseconds += 50;
					if (i >= 2) expected.kindAndBatch |= uploadedBatch << 2;
					uploadMatches &= std::memcmp(&states[i], &expected, sizeof(expected)) == 0;
				}
				check(uploadedBatch != 0 && uploadMatches,
					"direct upload sets birth time, batch id and DPI without changing other fields");
				const std::array otherGroup = { states[2], states[3] };
				renderer.RemoveLaserParticleGroup(14);
				if (!readParticles()) return 1;
				check(states[0].lifetimeMilliseconds == 0 && states[1].lifetimeMilliseconds == 0 &&
					std::memcmp(otherGroup.data(), states.data() + 2, sizeof(LaserGpuParticle) * otherGroup.size()) == 0,
					"removing the plain group keeps the other direct upload group");
				renderer.ResetLaserParticles();

				// 粒子池与激光纹理互不牵连：创建或释放激光纹理不会清掉正在飘的粒子（清空画布的飘散靠这一点）。
				renderer.ReleaseLaserResources();
				renderer.StepLaserParticles(7000, 1000, {}, dust, {}, dustContext.dipPerPixel);
				check(renderer.EnsureLaserResources(64, 64) && renderer.HasActiveLaserParticles(),
					"creating laser textures keeps live particles");
				renderer.ReleaseLaserResources();
				check(renderer.HasActiveLaserParticles(), "releasing laser textures keeps live particles");
				renderer.ResetLaserParticles();
			}
			{
				// 清空飘散：只在有墨迹的像素上出生，颜色还原为直通色，方向跟随所选风向。
				std::vector<uint32_t> canvas(64u * 64u, 0u);
				for (uint32_t y = 20; y < 30; ++y)
					for (uint32_t x = 10; x < 50; ++x) canvas[y * 64u + x] = 0x80800000u; // 预乘 BGRA：半透明红
				std::vector<LaserDriftParticle> cleared;
				LaserParticleBatchEnvelope clearEnvelope;
				const auto clearContext = MakeDriftParticleContext(1.0f, kLaserDissolveParameters.rightUpWindAngleDegrees);
				const auto clearFront = MakeLaserDissolveFront();
				RECT inkBounds = {};
				const auto clearedCount = AppendCanvasClearParticleSeeds(cleared, clearEnvelope, canvas.data(), 64u * 4u, 64, 64,
					clearContext, 7, clearFront, lowTier.maximumSeeds, &inkBounds);
				// 粒子的释放时刻与碎裂遮罩对齐：每粒都在遮罩碎完之前、自己那个像素消失的时刻出现，而且先后不一。
				const auto crumbleEndMilliseconds = static_cast<uint32_t>(std::ceil(LaserDissolveCrumbleEndProgress() *
					kLaserDissolveParameters.durationMilliseconds));
				check(inkBounds.left <= 10 && inkBounds.top <= 20 && inkBounds.right >= 50 && inkBounds.bottom >= 30 &&
					inkBounds.left >= 8 && inkBounds.bottom <= 32 && !cleared.empty() &&
					std::all_of(cleared.begin(), cleared.end(), [&](const LaserDriftParticle& drift)
					{
						auto vanished = clearFront;
						vanished.progress = (drift.particle.releaseMilliseconds + 1) /
							static_cast<float>(kLaserDissolveParameters.durationMilliseconds);
						return drift.particle.releaseMilliseconds <= crumbleEndMilliseconds + 1 &&
							LaserDissolveCoverage(vanished, drift.particle.position[0], drift.particle.position[1]) == 0.0f;
					}) &&
					std::any_of(cleared.begin(), cleared.end(), [&](const LaserDriftParticle& drift)
						{ return drift.particle.releaseMilliseconds != cleared.front().particle.releaseMilliseconds; }),
					"clear particles appear as their own pixel crumbles, inside the reported ink bounds");
				check(clearedCount == cleared.size() && clearedCount > 100 && clearedCount <= 400 &&
					std::all_of(cleared.begin(), cleared.end(), [](const LaserDriftParticle& drift)
					{
						return drift.particle.position[0] >= 10.0f && drift.particle.position[0] <= 50.0f &&
							drift.particle.position[1] >= 20.0f && drift.particle.position[1] <= 30.0f &&
							drift.particle.kindAndBatch == static_cast<uint32_t>(LaserParticleKind::Dissolve) &&
							drift.group == kCanvasClearParticleGroup && drift.particle.color == 0x800000FFu &&
							drift.particle.shapeParameters[1] == 0.0f &&
							drift.particle.motion[0] > 0.0f && drift.particle.motion[1] < 0.0f;
					}), "clear dissolve seeds are born on inked pixels with their straight color and the chosen wind");
				std::vector<LaserParticleSeed> reference;
				RECT referenceBounds = {};
				const auto count1x = AppendReferenceCanvasClearParticleSeeds(reference, canvas.data(), 64u * 4u, 64, 64, 1.0f,
					kLaserDissolveParameters.rightUpWindAngleDegrees, 7, clearFront, lowTier.maximumSeeds, &referenceBounds);
				check(count1x == clearedCount && EqualRect(&referenceBounds, &inkBounds) &&
					MatchesReferenceDriftBatch(cleared, reference, clearEnvelope), "1x clear matches the old seeds and ink bounds");
				const std::vector<uint32_t> emptyCanvas(64u * 64u, 0u);
				check(AppendCanvasClearParticleSeeds(cleared, clearEnvelope, emptyCanvas.data(), 64u * 4u, 64, 64,
					MakeDriftParticleContext(1.0f, kLaserDissolveParameters.leftUpWindAngleDegrees), 7, clearFront, lowTier.maximumSeeds) == 0,
					"an empty canvas emits no clear particles");
				// 两倍缩放：按 2×2 抽样，同样的墨迹面积（DIP）得到同量级的粒子，出生位置仍落在墨迹上。
				std::vector<LaserDriftParticle> sampled;
				LaserParticleBatchEnvelope sampledEnvelope;
				const auto sampledContext = MakeDriftParticleContext(2.0f, kLaserDissolveParameters.leftUpWindAngleDegrees);
				const auto sampledCount = AppendCanvasClearParticleSeeds(sampled, sampledEnvelope, canvas.data(), 64u * 4u, 64, 64,
					sampledContext, 7, clearFront, lowTier.maximumSeeds, &inkBounds);
				check(sampledCount > 20 && sampledCount <= 100 &&
					std::all_of(sampled.begin(), sampled.end(), [](const LaserDriftParticle& drift)
					{
						return drift.particle.position[0] >= 10.0f && drift.particle.position[0] <= 52.0f &&
							drift.particle.position[1] >= 20.0f && drift.particle.position[1] <= 32.0f && drift.particle.motion[0] < 0.0f;
					}), "high-DPI clear samples a coarser grid at the same density per DIP");
				reference.clear();
				const auto count2x = AppendReferenceCanvasClearParticleSeeds(reference, canvas.data(), 64u * 4u, 64, 64, 2.0f,
					kLaserDissolveParameters.leftUpWindAngleDegrees, 7, clearFront, lowTier.maximumSeeds, &referenceBounds);
				check(count2x == sampledCount && EqualRect(&referenceBounds, &inkBounds) &&
					MatchesReferenceDriftBatch(sampled, reference, sampledEnvelope), "2x clear matches the old seeds and ink bounds");
				struct Readback { size_t pitch = 0; uint32_t width = 0, height = 0; bool hasPixels = false; } readback;
				check(renderer.VisitCanvasPixels([](void* context, const uint32_t* canvasPixels, size_t rowPitchBytes,
					uint32_t canvasWidth, uint32_t canvasHeight)
					{
						*static_cast<Readback*>(context) = { rowPitchBytes, canvasWidth, canvasHeight, canvasPixels != nullptr };
					}, &readback) && readback.width == 64 && readback.height == 64 && readback.pitch >= 64u * 4u &&
					readback.hasPixels, "canvas readback maps the settled layer in place");

				// 清空过渡：清空前的画布留一份快照，用碎裂遮罩逐渐画没。进度 0 原样、中途部分像素消失、碎完后全空。
				renderer.ClearRTV(renderer.layerL2RTV.Get(), { 0.0f, 0.0f, 0.5f, 0.5f });
				check(!renderer.HasCanvasClearSnapshot() && renderer.CaptureCanvasClearSnapshot() &&
					renderer.HasCanvasClearSnapshot(), "clearing keeps one snapshot of the canvas");
				renderer.ClearRTV(renderer.layerL2RTV.Get(), { 0.0f, 0.0f, 0.0f, 0.0f });
				const RECT wholeCanvas = { 0, 0, 64, 64 };
				const auto crumbleAt = [&](float progress, size_t& opaque, size_t& transparent)
				{
					auto front = clearFront;
					front.progress = progress;
					renderer.ClearRTV(renderer.backBufferRTV.Get(), { 0.0f, 0.0f, 0.0f, 0.0f });
					if (!renderer.ResolveCanvasClearSnapshot(renderer.backBufferRTV.Get(), wholeCanvas, front)) return false;
					const auto image = pixels(renderer.backBufferTexture.Get());
					opaque = transparent = 0;
					for (size_t i = 3; i < image.size(); i += 4)
					{
						opaque += image[i] > 120;
						transparent += image[i] == 0;
					}
					return image.size() == 64 * 64 * 4;
				};
				size_t opaque = 0, transparent = 0;
				check(crumbleAt(0.0f, opaque, transparent) && opaque == 64 * 64, "the snapshot starts as the untouched canvas");
				check(crumbleAt(0.3f, opaque, transparent) && opaque > 400 && transparent > 400,
					"halfway through, the snapshot has crumbled pixel by pixel");
				check(crumbleAt(LaserDissolveCrumbleEndProgress(), opaque, transparent) && transparent == 64 * 64,
					"at the crumble end nothing of the snapshot is left");
				renderer.ReleaseCanvasClearSnapshot();
				check(!renderer.HasCanvasClearSnapshot() &&
					!renderer.ResolveCanvasClearSnapshot(renderer.backBufferRTV.Get(), wholeCanvas, clearFront),
					"the snapshot is released after the transition");

				// 粒子池闲时归还：没有存活粒子时释放，系统仍可用，下次有粒子时重建。
				auto& system = renderer.laserParticleSystem_;
				renderer.StepLaserParticles(8000, 1000, {}, cleared, {}, clearContext.dipPerPixel);
				renderer.ReleaseIdleLaserParticles();
				check(system.ResourcesCreated(driftPass) && renderer.HasActiveLaserParticles(), "a pool with live particles is kept");
				renderer.ResetLaserParticles();
				renderer.ReleaseIdleLaserParticles();
				check(!system.ResourcesCreated(driftPass) && renderer.LaserParticlesAvailable(), "an idle pool is returned but stays available");
				renderer.StepLaserParticles(9000, 1000, {}, cleared, {}, clearContext.dipPerPixel);
				check(system.ResourcesCreated(driftPass) && renderer.HasActiveLaserParticles(), "the pool is rebuilt on the next particle");
				renderer.ResetLaserParticles();
				renderer.ReleaseIdleLaserParticles();
			}
			{
				// 用中档容量验证 VS 确实读取掩码；写到末尾附近，再跨过环尾和暂存块边界。
				auto& system = renderer.laserParticleSystem_;
				const auto vertex = LoadShaderFromResource(IDR_DRAW3_LASER_PARTICLE_VS);
				const auto pixel = LoadShaderFromResource(IDR_DRAW3_LASER_PARTICLE_PS);
				const auto spawn = LoadShaderFromResource(IDR_DRAW3_LASER_PARTICLE_SPAWN_CS);
				if (!check(system.Initialize(testDevice.Get(), testContext.Get(),
					{ vertex.data, vertex.size }, { pixel.data, pixel.size }, { spawn.data, spawn.size }),
					"initialize the production particle shaders")) return 1;
				renderer.SetLaserParticleLimit(LaserParticleLimit::Medium);
				constexpr uint32_t start = mediumCapacity - 2;
				LaserParticleSeed inactive;
				inactive.kind = LaserParticleKind::Dissolve;
				renderer.StepLaserParticles(1000, 1000, {}, std::vector<LaserDriftParticle>(start, referenceDriftRecord(inactive)));
				constexpr uint32_t killedCount = 20000;
				constexpr uint32_t count = killedCount + 1;
				std::vector<LaserParticleSeed> batch(count, inactive);
				for (uint32_t i = 0; i < count; ++i)
				{
					auto& seed = batch[i];
					seed.position[0] = -1000.0f - static_cast<float>(i);
					seed.dragPerSecond = 1.0f;
					seed.brightness = 0.0f;
					seed.position[1] = 16.5f;
					seed.radius = 2.0f;
					seed.color[3] = 1.0f;
					seed.lifetimeMilliseconds = 500 + i;
					seed.group = 14;
				}
				batch[0].position[0] = 12.5f;
				batch[0].color[0] = 1.0f;
				batch[2].position[0] = 50.5f;
				batch[2].color[2] = 1.0f;
				batch.back().group = 15;
				std::vector<LaserDriftParticle> batchRecords;
				batchRecords.reserve(batch.size());
				for (const auto& seed : batch) batchRecords.push_back(referenceDriftRecord(seed));
				renderer.StepLaserParticles(1010, 1000, {}, batchRecords);
				if (!check(system.ParticleBuffer(driftPass) && system.ActiveRange(driftPass).start == start &&
					system.ActiveRange(driftPass).count == count, "one upload batch spans the ring end and multiple staging chunks"))
					return 1;

				constexpr auto stride = static_cast<UINT>(sizeof(LaserGpuParticle));
				D3D11_BUFFER_DESC stagingDescription = {};
				stagingDescription.ByteWidth = count * stride;
				stagingDescription.Usage = D3D11_USAGE_STAGING;
				stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				Microsoft::WRL::ComPtr<ID3D11Buffer> staging;
				if (!check(SUCCEEDED(testDevice->CreateBuffer(&stagingDescription, nullptr, staging.GetAddressOf())),
					"wrapped particle readback staging buffer")) return 1;
				const auto checkRecords = [&](bool removed)
				{
					constexpr uint32_t firstCount = mediumCapacity - start;
					const D3D11_BOX tailBox = { start * stride, 0, 0, mediumCapacity * stride, 1, 1 };
					const D3D11_BOX headBox = { 0, 0, 0, (count - firstCount) * stride, 1, 1 };
					testContext->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, system.ParticleBuffer(driftPass), 0, &tailBox);
					testContext->CopySubresourceRegion(staging.Get(), 0, firstCount * stride, 0, 0,
						system.ParticleBuffer(driftPass), 0, &headBox);
					D3D11_MAPPED_SUBRESOURCE mapped = {};
					if (!check(SUCCEEDED(testContext->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
						"wrapped particle readback maps")) return false;
					const auto* records = static_cast<const unsigned char*>(mapped.pData);
					bool matches = true;
					for (uint32_t i = 0; i < count && matches; ++i)
					{
						const auto expected = removed && i < killedCount ? LaserGpuParticle{} :
							PackLaserParticle(batch[i], 10, 0);
						matches = std::memcmp(records + i * stride, &expected, stride) == 0;
					}
					testContext->Unmap(staging.Get(), 0);
					return matches;
				};
				check(checkRecords(false), "every wrapped and chunked upload record matches its seed");

				// 环尾的红点和绕回第 0 槽的蓝点都必须由实际的粒子 VS 画出。
				renderer.ClearRTV(renderer.backBufferRTV.Get(), kTransparentLayerClearColor);
				renderer.DrawLaserParticles(driftPass);
				const auto wrappedPixels = pixels(renderer.backBufferTexture.Get());
				const size_t red = (16 * 64 + 12) * 4, blue = (16 * 64 + 50) * 4;
				check(wrappedPixels.size() == 64 * 64 * 4 &&
					wrappedPixels[red + 2] > 200 && wrappedPixels[red] < 8 &&
					wrappedPixels[blue] > 200 && wrappedPixels[blue + 2] < 8,
					"the particle vertex shader uses the non-default pool mask");

				renderer.RemoveLaserParticleGroup(14);
				check(checkRecords(true), "chunked removal clears the wrapped group and keeps its neighbor");
				renderer.ResetLaserParticles();
				renderer.ReleaseIdleLaserParticles();
			}
			{
				auto& system = renderer.laserParticleSystem_;
				constexpr auto stride = static_cast<UINT>(sizeof(LaserGpuParticle));
				D3D11_BUFFER_DESC stagingDescription = {};
				stagingDescription.ByteWidth = stride;
				stagingDescription.Usage = D3D11_USAGE_STAGING;
				stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				Microsoft::WRL::ComPtr<ID3D11Buffer> staging;
				if (!check(SUCCEEDED(testDevice->CreateBuffer(&stagingDescription, nullptr, staging.GetAddressOf())),
					"single particle readback staging buffer")) return 1;
				const auto readSlot = [&](LaserParticlePass pass, uint32_t slot, LaserGpuParticle& result)
				{
					auto* buffer = system.ParticleBuffer(pass);
					if (!buffer) return false;
					D3D11_BUFFER_DESC desc = {};
					buffer->GetDesc(&desc);
					if (slot >= desc.ByteWidth / stride) return false;
					const D3D11_BOX box = { slot * stride, 0, 0, (slot + 1) * stride, 1, 1 };
					testContext->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, buffer, 0, &box);
					D3D11_MAPPED_SUBRESOURCE mapped = {};
					if (FAILED(testContext->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
					std::memcpy(&result, mapped.pData, stride);
					testContext->Unmap(staging.Get(), 0);
					return true;
				};
				const auto matchesSlot = [&](LaserParticlePass pass, uint32_t slot, const LaserGpuParticle& expected)
				{
					LaserGpuParticle actual = {};
					return readSlot(pass, slot, actual) && std::memcmp(&actual, &expected, stride) == 0;
				};
				const auto capacity = [&](LaserParticlePass pass)
				{
					D3D11_BUFFER_DESC desc = {};
					if (auto* buffer = system.ParticleBuffer(pass)) buffer->GetDesc(&desc);
					return desc.ByteWidth / stride;
				};
				const auto releasePools = [&]
				{
					renderer.ResetLaserParticles();
					renderer.ReleaseIdleLaserParticles();
				};
				LaserParticleSeed trail;
				trail.position[0] = 12.5f;
				trail.position[1] = 16.5f;
				trail.radius = 2.0f;
				trail.lifetimeMilliseconds = 500;
				trail.color[0] = trail.color[3] = 1.0f;
				trail.group = 14;
				LaserParticleSeed clear = trail;
				clear.kind = LaserParticleKind::Dissolve;
				clear.position[0] = 32.5f;
				clear.velocity[0] = clear.dragPerSecond = clear.breathingFrequencyHz = 1.0f;
				clear.brightness = 0.0f;
				clear.color[0] = 0.0f;
				clear.color[1] = 1.0f;
				LaserParticleSeed dissolve = clear;
				dissolve.position[0] = 50.5f;
				dissolve.color[1] = 0.0f;
				dissolve.color[2] = 1.0f;
				dissolve.group = 16;

				// 每档把游标推到最后一槽，再写两粒跨到第 0 槽；极致档实际创建 64 MB。
				LaserParticleSeed expired = clear;
				expired.lifetimeMilliseconds = 0;
				const auto clearDrift = referenceDriftRecord(clear);
				const auto dissolveDrift = referenceDriftRecord(dissolve);
				const std::vector<LaserDriftParticle> cursorRecords(kLaserParticleStagingChunk, referenceDriftRecord(expired));
				const std::array wrapRecords = { clearDrift, dissolveDrift };
				const std::span<const LaserDriftParticle> clearSpan(&clearDrift, 1);
				const std::span<const LaserDriftParticle> dissolveSpan(&dissolveDrift, 1);
				for (size_t i = 0; i < kLaserParticleLimitTiers.size(); ++i)
				{
					const auto limit = static_cast<LaserParticleLimit>(i);
					const auto tier = kLaserParticleLimitTiers[i];
					renderer.SetLaserParticleLimit(limit);
					for (uint32_t remaining = tier.poolCapacity - 1; remaining != 0;)
					{
						const auto count = (std::min)(remaining, kLaserParticleStagingChunk);
						renderer.StepLaserParticles(1000, 1000, {}, std::span<const LaserDriftParticle>(cursorRecords).first(count));
						remaining -= count;
					}
					renderer.StepLaserParticles(1001, 1000, {}, wrapRecords);
					const auto range = system.ActiveRange(driftPass);
					check(system.EffectiveLimit() == limit && renderer.LaserParticleMaximumSeeds() == tier.maximumSeeds &&
						capacity(driftPass) == tier.poolCapacity && !system.ResourcesCreated(trailPass) &&
						range.start == tier.poolCapacity - 1 && range.count == wrapRecords.size() &&
						matchesSlot(driftPass, range.start, PackLaserParticle(clear, 1, 0)) &&
						matchesSlot(driftPass, 0, PackLaserParticle(dissolve, 1, 0)),
						"each tier creates its real capacity and writes both ends of the ring");
					releasePools();
				}

				renderer.SetLaserParticleLimit(LaserParticleLimit::Low);
				renderer.PrepareLaserParticles();
				check(capacity(trailPass) == kLaserTrailPoolCapacity && !system.ResourcesCreated(driftPass) &&
					!renderer.HasActiveLaserParticles(), "preparation only creates and warms the trail pool");
				auto neighborTrail = trail, neighborClear = clear;
				neighborTrail.group = neighborClear.group = 15;
				neighborTrail.position[0] = 16.5f;
				neighborClear.position[0] = 36.5f;
				const std::array mixedTrail = { trail, neighborTrail };
				const std::array mixedClear = { clearDrift, referenceDriftRecord(neighborClear) };
				renderer.StepLaserParticles(1000, 1000, mixedTrail, mixedClear, dissolveSpan);
				LaserGpuParticle laserRecord = {};
				const bool gotLaser = readSlot(driftPass, 2, laserRecord);
				const auto batch = laserRecord.kindAndBatch >> 2;
				check(system.ActiveRange(trailPass).start == 0 && system.ActiveRange(trailPass).count == 2 &&
					system.ActiveRange(driftPass).start == 0 && system.ActiveRange(driftPass).count == 3 &&
					matchesSlot(trailPass, 0, PackLaserParticle(trail, 0, 0)) &&
					matchesSlot(trailPass, 1, PackLaserParticle(neighborTrail, 0, 0)) &&
					matchesSlot(driftPass, 0, PackLaserParticle(clear, 0, 0)) &&
					matchesSlot(driftPass, 1, PackLaserParticle(neighborClear, 0, 0)) && gotLaser && batch != 0 &&
					matchesSlot(driftPass, 2, PackLaserParticle(dissolve, 0, batch)),
					"one step keeps the order and batch identity of all three particle sources");
				const auto drawPass = [&](LaserParticlePass pass)
				{
					renderer.ClearRTV(renderer.backBufferRTV.Get(), kTransparentLayerClearColor);
					renderer.DrawLaserParticles(pass);
					return pixels(renderer.backBufferTexture.Get());
				};
				const size_t red = (16 * 64 + 12) * 4, green = (16 * 64 + 32) * 4, blue = (16 * 64 + 50) * 4;
				const auto trailPixels = drawPass(trailPass);
				const auto driftPixels = drawPass(driftPass);
				check(trailPixels.size() == 64 * 64 * 4 && trailPixels[red + 2] > 200 &&
					trailPixels[green + 3] == 0 && trailPixels[blue + 3] == 0,
					"over-body pixels contain only trail particles");
				check(driftPixels.size() == 64 * 64 * 4 && driftPixels[red + 3] == 0 &&
					driftPixels[green + 1] > 200 && driftPixels[blue] > 200,
					"under-body pixels contain only clear and laser dissolve particles");

				renderer.StepLaserParticles(1100, 1000, {}, {}, {}, 1.0f, { 1050, 0.5f, false, 1300 });
				check(system.Clock().gatherBatch == batch && batch != 0 && system.Clock().gatherRemaining == 0.5f,
					"gather selects the laser dissolve batch in the mixed step");
				renderer.StepLaserParticles(1200, 1000, {}, {}, {}, 1.0f, { 1050, 1.0f, true });
				check(system.ActiveRange(driftPass).count == 2 && system.ActiveRange(trailPass).count == 2 &&
					matchesSlot(driftPass, 2, LaserGpuParticle{}) &&
					matchesSlot(driftPass, 0, PackLaserParticle(clear, 0, 0)) &&
					matchesSlot(trailPass, 0, PackLaserParticle(trail, 0, 0)),
					"gather completion kills only the laser dissolve batch");
				renderer.RemoveLaserParticleGroup(14);
				check(matchesSlot(trailPass, 0, LaserGpuParticle{}) && matchesSlot(driftPass, 0, LaserGpuParticle{}) &&
					matchesSlot(trailPass, 1, PackLaserParticle(neighborTrail, 0, 0)) &&
					matchesSlot(driftPass, 1, PackLaserParticle(neighborClear, 0, 0)),
					"removing one group clears both pools and keeps both neighbors");
				releasePools();

				const std::span<const LaserParticleSeed> trailSpan(&trail, 1);
				renderer.StepLaserParticles(1000, 1000, trailSpan, clearSpan);
				std::vector<LaserParticleSeed> overflow(kLaserTrailPoolCapacity + 1, trail);
				overflow[0].position[0] = -1.0f;
				overflow[1].position[0] = 8.5f;
				overflow.back().position[0] = 10.5f;
				renderer.StepLaserParticles(1001, 1000, overflow);
				check(system.ActiveRange(trailPass).start == 1 &&
					system.ActiveRange(trailPass).count == kLaserTrailPoolCapacity &&
					matchesSlot(trailPass, 1, PackLaserParticle(overflow[1], 1, 0)) &&
					matchesSlot(trailPass, 0, PackLaserParticle(overflow.back(), 1, 0)) &&
					system.ActiveRange(driftPass).count == 1 &&
					matchesSlot(driftPass, 0, PackLaserParticle(clear, 0, 0)),
					"trail overflow keeps its last slots and leaves the drift pool intact");
				const auto lastTrail = PackLaserParticle(overflow.back(), 1, 0);
				std::vector<LaserDriftParticle> driftOverflow(lowTier.poolCapacity, clearDrift);
				driftOverflow[0].particle.position[0] = -1.0f;
				auto firstKeptClear = clear;
				firstKeptClear.position[0] = 24.5f;
				driftOverflow[1] = referenceDriftRecord(firstKeptClear);
				renderer.StepLaserParticles(1002, 1000, {}, driftOverflow, dissolveSpan);
				LaserGpuParticle lastDrift = {};
				const bool gotLastDrift = readSlot(driftPass, 0, lastDrift);
				const auto lastBatch = lastDrift.kindAndBatch >> 2;
				check(system.ActiveRange(driftPass).start == 1 &&
					system.ActiveRange(driftPass).count == lowTier.poolCapacity &&
					matchesSlot(driftPass, 1, PackLaserParticle(firstKeptClear, 2, 0)) &&
					gotLastDrift && lastBatch != 0 && matchesSlot(driftPass, 0, PackLaserParticle(dissolve, 2, lastBatch)) &&
					system.ActiveRange(trailPass).count == kLaserTrailPoolCapacity &&
					matchesSlot(trailPass, 0, lastTrail),
					"drift overflow keeps its last slots and leaves the trail pool intact");

				auto* const trailBuffer = system.ParticleBuffer(trailPass);
				auto* const driftBuffer = system.ParticleBuffer(driftPass);
				renderer.SetLaserParticleLimit(LaserParticleLimit::Low);
				check(system.ParticleBuffer(driftPass) == driftBuffer &&
					system.ActiveRange(driftPass).count == lowTier.poolCapacity &&
					matchesSlot(driftPass, 0, lastDrift), "setting the same tier preserves live drift records");
				renderer.SetLaserParticleLimit(LaserParticleLimit::Medium);
				check(!system.ResourcesCreated(driftPass) && system.ActiveRange(driftPass).count == 0 &&
					system.ParticleBuffer(trailPass) == trailBuffer &&
					system.ActiveRange(trailPass).count == kLaserTrailPoolCapacity &&
					matchesSlot(trailPass, 0, lastTrail), "changing tier drops only the drift pool");
				releasePools();

				for (const auto limit : { LaserParticleLimit::Low, LaserParticleLimit::Medium })
				{
					renderer.SetLaserParticleLimit(limit);
					const auto maximum = renderer.LaserParticleMaximumSeeds();
					const auto tier = kLaserParticleLimitTiers[static_cast<size_t>(limit)];
					std::vector<LaserDriftParticle> clearBatch(maximum, clearDrift), laserBatch(maximum, dissolveDrift);
					auto lastClearSeed = clear, lastLaserSeed = dissolve;
					lastClearSeed.position[0] = 34.5f;
					lastLaserSeed.position[0] = 52.5f;
					clearBatch.back() = referenceDriftRecord(lastClearSeed);
					laserBatch.back() = referenceDriftRecord(lastLaserSeed);
					renderer.StepLaserParticles(1000, 1000, {}, clearBatch);
					renderer.StepLaserParticles(1001, 1000, {}, {}, laserBatch);
					LaserGpuParticle firstLaser = {};
					const bool gotFirstLaser = readSlot(driftPass, maximum, firstLaser);
					const auto fullBatch = firstLaser.kindAndBatch >> 2;
					check(maximum == tier.maximumSeeds && system.ActiveRange(driftPass).start == 0 &&
						system.ActiveRange(driftPass).count == tier.poolCapacity &&
						matchesSlot(driftPass, 0, PackLaserParticle(clear, 0, 0)) &&
						matchesSlot(driftPass, maximum - 1, PackLaserParticle(lastClearSeed, 0, 0)) &&
						gotFirstLaser && fullBatch != 0 &&
						matchesSlot(driftPass, maximum, PackLaserParticle(dissolve, 1, fullBatch)) &&
						matchesSlot(driftPass, tier.poolCapacity - 1, PackLaserParticle(lastLaserSeed, 1, fullBatch)),
						"full clear and laser batches coexist at the low and medium tiers");
					releasePools();
				}

				// 分别让行进池和飘散池先变空，共享资源应等到最后一个池空了才归还。
				auto longClear = clear;
				longClear.lifetimeMilliseconds = 1000;
				const std::array longClearBatch = { referenceDriftRecord(longClear) };
				renderer.StepLaserParticles(1000, 1000, trailSpan, longClearBatch);
				renderer.StepLaserParticles(1600, 1000, {});
				renderer.ReleaseIdleLaserParticles();
				check(!system.ResourcesCreated(trailPass) && system.ResourcesCreated(driftPass) &&
					system.ActiveRange(driftPass).count == 1 && LaserParticleSystemTestAccess::DrawResourcesMatch(system, true),
					"empty trail pool is released while drift and shared resources stay live");
				releasePools();
				auto longTrail = trail;
				longTrail.lifetimeMilliseconds = 1000;
				renderer.StepLaserParticles(1000, 1000, std::span<const LaserParticleSeed>(&longTrail, 1), clearSpan);
				renderer.StepLaserParticles(1600, 1000, {});
				renderer.ReleaseIdleLaserParticles();
				check(system.ResourcesCreated(trailPass) && !system.ResourcesCreated(driftPass) &&
					system.ActiveRange(trailPass).count == 1 && LaserParticleSystemTestAccess::DrawResourcesMatch(system, true),
					"empty drift pool is released while trail and shared resources stay live");
				renderer.StepLaserParticles(2100, 1000, {});
				renderer.ReleaseIdleLaserParticles();
				check(!system.ResourcesCreated(trailPass) && !system.ResourcesCreated(driftPass) &&
					LaserParticleSystemTestAccess::DrawResourcesMatch(system, false) && renderer.LaserParticlesAvailable(),
					"both empty pools release shaders, constants and staging while keeping the device session");

				renderer.StepLaserParticles(1000, 1000, std::span<const LaserParticleSeed>(&longTrail, 1), clearSpan);
				auto* const liveTrailBuffer = system.ParticleBuffer(trailPass);
				auto* const liveDriftBuffer = system.ParticleBuffer(driftPass);
				renderer.ReleaseIdleLaserDriftPool();
				check(liveDriftBuffer && system.ParticleBuffer(driftPass) == liveDriftBuffer &&
					system.ActiveRange(driftPass).count == 1,
					"drift-only release keeps a live drift pool");
				renderer.StepLaserParticles(1600, 1000, {});
				renderer.ReleaseIdleLaserDriftPool();
				check(!system.ResourcesCreated(driftPass) && liveTrailBuffer &&
					system.ParticleBuffer(trailPass) == liveTrailBuffer &&
					system.ActiveRange(trailPass).count == 1 && LaserParticleSystemTestAccess::DrawResourcesMatch(system, true),
					"drift-only release returns an expired pool and keeps trail and shared resources");
				releasePools();

				const auto vertex = LoadShaderFromResource(IDR_DRAW3_LASER_PARTICLE_VS);
				const auto pixel = LoadShaderFromResource(IDR_DRAW3_LASER_PARTICLE_PS);
				const auto spawn = LoadShaderFromResource(IDR_DRAW3_LASER_PARTICLE_SPAWN_CS);
				const auto reinitialize = [&]
				{
					system.Release();
					return system.Initialize(testDevice.Get(), testContext.Get(),
						{ vertex.data, vertex.size }, { pixel.data, pixel.size }, { spawn.data, spawn.size });
				};
				LaserParticleSystemTestAccess::SetMaximumDriftTier(system, static_cast<int>(LaserParticleLimit::Medium));
				renderer.SetLaserParticleLimit(LaserParticleLimit::Extreme);
				renderer.StepLaserParticles(1000, 1000, trailSpan, clearSpan);
				check(system.EffectiveLimit() == LaserParticleLimit::Medium && capacity(driftPass) == mediumCapacity,
					"success at an intermediate tier stops the downgrade there");
				if (!check(reinitialize(), "reset the particle device session")) return 1;
				LaserParticleSystemTestAccess::SetMaximumDriftTier(system, static_cast<int>(LaserParticleLimit::Low));
				renderer.SetLaserParticleLimit(LaserParticleLimit::Extreme);
				renderer.StepLaserParticles(1000, 1000, trailSpan, clearSpan);
				check(system.EffectiveLimit() == LaserParticleLimit::Low && capacity(driftPass) == lowTier.poolCapacity &&
					renderer.LaserParticleMaximumSeeds() == lowTier.maximumSeeds &&
					system.ActiveRange(driftPass).count == 1 && system.ActiveRange(trailPass).count == 1,
					"failed higher allocations fall back to low without interrupting trail");
				renderer.SetLaserParticleLimit(LaserParticleLimit::High);
				check(system.EffectiveLimit() == LaserParticleLimit::Low && system.ActiveRange(driftPass).count == 1,
					"requests above the session ceiling keep the current pool");
				if (!check(reinitialize() && system.EffectiveLimit() == LaserParticleLimit::Low,
					"reinitialization restores the default effective tier")) return 1;
				{
					// 建池降档必须先于满额记录生成；两批在同一次推进中各占实际容量的一半。
					renderer.SetLaserParticleLimit(LaserParticleLimit::Medium);
					const uint32_t maximum = renderer.PrepareLaserDriftPool();
					if (check(maximum == lowTier.maximumSeeds && system.EffectiveLimit() == LaserParticleLimit::Low &&
						capacity(driftPass) == lowTier.poolCapacity, "preparation returns the downgraded spawn limit"))
					{
						const std::vector<LaserDriftParticle> clearBatch(maximum, clearDrift), laserBatch(maximum, dissolveDrift);
						const auto accepted = renderer.StepLaserParticles(1000, 1000, {}, clearBatch, laserBatch);
						LaserGpuParticle firstLaser = {};
						const bool gotFirstLaser = readSlot(driftPass, maximum, firstLaser);
						const auto batch = firstLaser.kindAndBatch >> 2;
						check(!accepted.trailAccepted && accepted.clearAccepted && accepted.dissolveAccepted &&
							system.ActiveRange(driftPass).start == 0 && system.ActiveRange(driftPass).count == lowTier.poolCapacity &&
							matchesSlot(driftPass, 0, PackLaserParticle(clear, 0, 0)) &&
							matchesSlot(driftPass, maximum - 1, PackLaserParticle(clear, 0, 0)) &&
							gotFirstLaser && batch != 0 &&
							matchesSlot(driftPass, maximum, PackLaserParticle(dissolve, 0, batch)) &&
							matchesSlot(driftPass, lowTier.poolCapacity - 1, PackLaserParticle(dissolve, 0, batch)),
							"one downgraded step keeps both full batches in their entirety");
					}
				}
				if (!check(reinitialize(), "reset the particle device session after testing the spawn limit")) return 1;
				renderer.SetLaserParticleLimit(LaserParticleLimit::Extreme);
				check(system.EffectiveLimit() == LaserParticleLimit::Extreme, "device recreation resets the ceiling");

				LaserParticleSystemTestAccess::SetMaximumDriftTier(system, -1);
				check(renderer.PrepareLaserDriftPool() == 0, "preparation returns zero when no drift tier can be created");
				const auto accepted = renderer.StepLaserParticles(1000, 1000, trailSpan, clearSpan, dissolveSpan);
				check(accepted.trailAccepted && !accepted.clearAccepted && !accepted.dissolveAccepted,
					"rejected drift batches leave trail acceptance intact");
				const auto failurePixels = drawPass(trailPass);
				check(renderer.LaserParticlesAvailable() && !system.ResourcesCreated(driftPass) &&
					system.ActiveRange(driftPass).count == 0 && system.ActiveRange(trailPass).count == 1 &&
					failurePixels.size() == 64 * 64 * 4 && failurePixels[red + 2] > 200 && failurePixels[green + 3] == 0,
					"all drift tiers failing still permits trail pixels to draw");
				LaserParticleSystemTestAccess::SetMaximumDriftTier(system, static_cast<int>(LaserParticleLimit::Extreme));
				renderer.StepLaserParticles(1001, 1000, {}, clearSpan);
				check(!system.ResourcesCreated(driftPass) && system.ActiveRange(driftPass).count == 0,
					"all drift failures disable that pool until device recreation");
				if (!check(reinitialize(), "restore default particle resources after failure injection")) return 1;

				LaserParticleSystemTestAccess::SetTrailCreationFailure(system, true);
				const auto reverseAccepted = renderer.StepLaserParticles(1000, 1000, trailSpan, clearSpan, dissolveSpan);
				const auto reversePixels = drawPass(driftPass);
				check(system.IsAvailable() && !reverseAccepted.trailAccepted &&
					reverseAccepted.clearAccepted && reverseAccepted.dissolveAccepted &&
					!system.ResourcesCreated(trailPass) && system.ActiveRange(trailPass).count == 0 &&
					system.ActiveRange(driftPass).count == 2 &&
					reversePixels.size() == 64 * 64 * 4 && reversePixels[red + 3] == 0 &&
					reversePixels[green + 1] > 200 && reversePixels[blue] > 200,
					"trail creation failure drops trail and still uploads and draws both drift batches");
				LaserParticleSystemTestAccess::SetTrailCreationFailure(system, false);
				if (!check(reinitialize(), "restore default particle resources after trail failure injection")) return 1;
			}
			{
				auto& system = renderer.laserParticleSystem_;
				constexpr uint32_t side = 64, reserved = side * side;
				constexpr int64_t frequency = 1000, origin = 1000, birth = 1075;
				constexpr uint32_t seedBase = 16;
				const auto front = MakeLaserDissolveFront();
				constexpr float wind = kLaserDissolveParameters.rightUpWindAngleDegrees;
				// 2×2 格内使用同一颜色，避免有意引入代表像素漏采；边缘仍覆盖 alpha 47 和 48。
				constexpr uint32_t colors[] = { 0u, 0x2F1C1408u, 0x30171109u, 0x60421824u,
					0x80804020u, 0xFF8877DDu, 0xFFACE123u, 0xC04B5295u };
				std::vector<uint32_t> canvas(reserved);
				for (uint32_t y = 8; y < 56; ++y)
					for (uint32_t x = 8; x < 56; ++x)
						canvas[y * side + x] = colors[(x / 2 + 3 * (y / 2)) % ARRAYSIZE(colors)];
				testContext->OMSetRenderTargets(0, nullptr, nullptr);
				testContext->UpdateSubresource(renderer.layerL2Texture.Get(), 0, nullptr, canvas.data(), side * 4u, 0);
				if (!check(renderer.GpuClearSpawnAvailable() && renderer.CaptureCanvasClearSnapshot(),
					"GPU clear is available with a production canvas snapshot")) return 1;
				const LaserClearSpawnRequest request = { { 8, 8, 56, 56 }, front, 1.0f, wind, seedBase, reserved };

				const auto readSlots = [&](uint32_t start, uint32_t count)
				{
					std::vector<LaserGpuParticle> records;
					ID3D11Buffer* buffer = system.ParticleBuffer(driftPass);
					if (!check(buffer != nullptr, "GPU clear owns a drift buffer")) return records;
					constexpr auto stride = static_cast<UINT>(sizeof(LaserGpuParticle));
					D3D11_BUFFER_DESC description = {};
					buffer->GetDesc(&description);
					const uint32_t first = (std::min)(count, description.ByteWidth / stride - start);
					description = {};
					description.ByteWidth = count * stride;
					description.Usage = D3D11_USAGE_STAGING;
					description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
					Microsoft::WRL::ComPtr<ID3D11Buffer> staging;
					if (!check(SUCCEEDED(testDevice->CreateBuffer(&description, nullptr, staging.GetAddressOf())),
						"GPU clear particle staging")) return records;
					const D3D11_BOX tail = { start * stride, 0, 0, (start + first) * stride, 1, 1 };
					testContext->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, buffer, 0, &tail);
					if (first < count)
					{
						const D3D11_BOX head = { 0, 0, 0, (count - first) * stride, 1, 1 };
						testContext->CopySubresourceRegion(staging.Get(), 0, first * stride, 0, 0, buffer, 0, &head);
					}
					records.resize(count);
					D3D11_MAPPED_SUBRESOURCE mapped = {};
					// 仅自检阻塞读记录：这个 Copy 在 counters Copy 之后，完成时可立即轮询。
					if (!check(SUCCEEDED(testContext->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
						"GPU clear particle readback")) { records.clear(); return records; }
					std::memcpy(records.data(), mapped.pData, description.ByteWidth);
					testContext->Unmap(staging.Get(), 0);
					return records;
				};
				const auto submit = [&](const LaserClearSpawnRequest& spawn, int64_t qpc)
				{
					return renderer.StepLaserParticles(qpc, frequency, {}, {}, {}, 1.0f, {}, &spawn).clearSpawnAccepted;
				};
				const auto cpuClear = [&](float dpiScale)
				{
					std::vector<LaserDriftParticle> expected;
					LaserParticleBatchEnvelope envelope;
					const auto drift = MakeDriftParticleContext(dpiScale, wind);
					AppendCanvasClearParticleSeeds(expected, envelope, canvas.data(), side * 4u, side, side,
						drift, seedBase, front, lowTier.maximumSeeds);
					return expected;
				};

				// 带非零出生时刻提交，并故意留下 VS 绑定，验证派发前后两个方向的资源解绑。
				if (!check(renderer.PrepareLaserDriftPool() == lowTier.maximumSeeds, "prepare drift pool for GPU clear")) return 1;
				ID3D11ShaderResourceView* previousSrv = LaserParticleSystemTestAccess::DriftSrv(system);
				testContext->VSSetShaderResources(0, 1, &previousSrv);
				renderer.StepLaserParticles(origin, frequency, {});
				if (!check(submit(request, birth) && system.ActiveRange(driftPass).count == reserved,
					"GPU clear reserves an ordinary batch in Step")) return 1;
				renderer.StepLaserParticles(birth + 550, frequency, {});
				renderer.ClearRTV(renderer.backBufferRTV.Get(), kTransparentLayerClearColor);
				renderer.DrawLaserParticles(driftPass);
				const auto rendered = pixels(renderer.backBufferTexture.Get());
				bool visible = false;
				for (size_t i = 3; i < rendered.size(); i += 4) visible |= rendered[i] != 0;
				check(visible, "the VS draws GPU-born particles after the UAV is unbound");

				auto actual = readSlots(0, reserved);
				const auto count = renderer.PollClearSpawn();
				auto expected = cpuClear(1.0f);
				if (!check(count && count->written > 0 && count->written == expected.size() && count->written <= actual.size() &&
					system.ActiveRange(driftPass).count == count->written, "1x GPU count and trimmed range match CPU")) return 1;
				actual.resize(count->written);
				const auto byBirth = [](const LaserGpuParticle& a, const LaserGpuParticle& b)
				{
					if (a.position[0] != b.position[0]) return a.position[0] < b.position[0];
					if (a.position[1] != b.position[1]) return a.position[1] < b.position[1];
					return a.releaseMilliseconds < b.releaseMilliseconds;
				};
				std::sort(actual.begin(), actual.end(), byBirth);
				std::sort(expected.begin(), expected.end(),
					[&](const LaserDriftParticle& a, const LaserDriftParticle& b) { return byBirth(a.particle, b.particle); });
				bool integerFields = true;
				struct FloatComparison
				{
					const char* name;
					float relativeLimit = 1e-4f;
					float absoluteLimit = (std::numeric_limits<float>::infinity)();
					float maximumRelative = 0.0f;
					float maximumAbsolute = 0.0f;
				};
				// WARP 旧样本的最大相对误差为 8.087e-4。出生风场或散开分量接近零时，
				// sin/cos 与点积舍入会放大相对误差；只给这些分量约两倍余量。
				// 再约束绝对误差：1× 散开偏差每轴至多 0.00004 px，风场偏差乘 22 px 仍小于 0.001 px。
				std::array<FloatComparison, 12> fields = {{
					{ "position.x" }, { "position.y" },
					{ "motion.x" }, { "motion.y" },
					{ "drag" }, { "meander" },
					{ "burst.x", 1.7e-3f, 4e-5f }, { "burst.y", 1.7e-3f, 4e-5f },
					{ "radius" }, { "white", 0.0f },
					{ "birthWind.x", 1.7e-3f, 4e-5f }, { "birthWind.y", 1.7e-3f, 4e-5f }
				}};
				const auto compareFloat = [&](size_t index, float gpu, float cpu)
				{
					auto& field = fields[index];
					const float scale = (std::max)(std::abs(gpu), std::abs(cpu));
					const bool finite = std::isfinite(gpu) && std::isfinite(cpu);
					const float absolute = finite ? std::abs(gpu - cpu) : (std::numeric_limits<float>::infinity)();
					const float relative = !finite ? absolute : (scale != 0.0f ? absolute / scale : 0.0f);
					field.maximumAbsolute = (std::max)(field.maximumAbsolute, absolute);
					field.maximumRelative = (std::max)(field.maximumRelative, relative);
				};
				for (size_t i = 0; i < actual.size(); ++i)
				{
					const auto& gpu = actual[i];
					const auto& cpu = expected[i].particle;
					integerFields &= gpu.color == cpu.color && gpu.lifetimeMilliseconds == cpu.lifetimeMilliseconds &&
						gpu.releaseMilliseconds == cpu.releaseMilliseconds + static_cast<uint32_t>(birth - origin) &&
						gpu.kindAndBatch == cpu.kindAndBatch;
					for (size_t field = 0; field < 2; ++field)
					{
						compareFloat(field, gpu.position[field], cpu.position[field]);
						compareFloat(2 + field, gpu.motion[field], cpu.motion[field]);
					}
					for (size_t field = 0; field < 4; ++field)
					{
						compareFloat(4 + field, gpu.motionParameters[field], cpu.motionParameters[field]);
						compareFloat(8 + field, gpu.shapeParameters[field], cpu.shapeParameters[field]);
					}
				}
				check(integerFields, "1x clear color, lifetime, release and kind match exactly");
				const bool floatFields = check(std::all_of(fields.begin(), fields.end(), [](const FloatComparison& field)
				{
					return field.maximumRelative <= field.relativeLimit && field.maximumAbsolute <= field.absoluteLimit;
				}), "1x clear float fields match within field-specific tolerances");
				for (const auto& field : fields)
					if (!integerFields || !floatFields || field.maximumRelative > 1e-4f)
						std::fprintf(stderr, "[LaserResources] clear spawn %s max_relative=%.9g max_absolute=%.9g\n",
							field.name, field.maximumRelative, field.maximumAbsolute);

				renderer.ResetLaserParticles();
				auto scaled = request;
				scaled.dpiScale = 2.0f;
				if (!check(submit(scaled, birth), "2x GPU clear accepted")) return 1;
				actual = readSlots(0, reserved);
				const auto scaledCount = renderer.PollClearSpawn();
				if (!check(scaledCount && scaledCount->written > 0 && scaledCount->written <= actual.size(), "2x GPU clear counters")) return 1;
				const auto expectedScaled = cpuClear(2.0f);
				constexpr uint32_t blockSide = 16, blocksPerSide = side / blockSide;
				const auto blockIndex = [](uint32_t x, uint32_t y) { return (y / blockSide) * blocksPerSide + x / blockSide; };
				std::array<uint32_t, blocksPerSide * blocksPerSide> gpuBlocks{}, cpuBlocks{}, inkedBlocks{};
				for (uint32_t y = 0; y < side; ++y)
					for (uint32_t x = 0; x < side; ++x)
						inkedBlocks[blockIndex(x, y)] += (canvas[y * side + x] >> 24) >= kCanvasClearParticleParameters.minimumAlpha;
				bool withinCanvas = true;
				for (const auto& particle : std::span(actual).first(scaledCount->written))
				{
					const float x = particle.position[0], y = particle.position[1];
					if (x >= 0.0f && x < side && y >= 0.0f && y < side)
						++gpuBlocks[blockIndex(static_cast<uint32_t>(x), static_cast<uint32_t>(y))];
					else withinCanvas = false;
				}
				for (const auto& record : expectedScaled)
					++cpuBlocks[blockIndex(static_cast<uint32_t>(record.particle.position[0]),
						static_cast<uint32_t>(record.particle.position[1]))];
				// 两条路径共用像素哈希，正相关会减小差值的方差；忽略协方差得到保守的 4σ。
				const auto withinFourSigma = [](size_t gpu, size_t cpu, uint32_t inked)
				{
					const double variance = inked * (0.125 * 0.875 + 0.25 * 0.5 * 0.5);
					return std::abs(static_cast<double>(gpu) - static_cast<double>(cpu)) <= 4.0 * std::sqrt(variance);
				};
				uint32_t totalInked = 0;
				for (size_t i = 0; i < inkedBlocks.size(); ++i)
				{
					totalInked += inkedBlocks[i];
					withinCanvas &= withinFourSigma(gpuBlocks[i], cpuBlocks[i], inkedBlocks[i]);
				}
				check(withinFourSigma(scaledCount->written, expectedScaled.size(), totalInked) && withinCanvas,
					"2x clear count and every 16x16 block agree within four sigma");

				// 触顶样本用固定哈希和较大的纹理，命中数超过低档上限且左侧留透明带。
				renderer.ResetLaserParticles();
				constexpr uint32_t largeSide = 256, transparentWidth = 8;
				std::vector<uint32_t> large(largeSide * largeSide, 0u);
				for (uint32_t y = 0; y < largeSide; ++y)
					std::fill_n(large.data() + y * largeSide + transparentWidth, largeSide - transparentWidth, 0xFF5577DDu);
				D3D11_TEXTURE2D_DESC textureDescription = {};
				textureDescription.Width = textureDescription.Height = largeSide;
				textureDescription.MipLevels = textureDescription.ArraySize = 1;
				textureDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
				textureDescription.SampleDesc.Count = 1;
				textureDescription.Usage = D3D11_USAGE_IMMUTABLE;
				textureDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				const D3D11_SUBRESOURCE_DATA source = { large.data(), largeSide * 4u, 0 };
				Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
				Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> largeSnapshot;
				if (!check(SUCCEEDED(testDevice->CreateTexture2D(&textureDescription, &source, texture.GetAddressOf())) &&
					SUCCEEDED(testDevice->CreateShaderResourceView(texture.Get(), nullptr, largeSnapshot.GetAddressOf())),
					"large WARP snapshot for the cap")) return 1;
				auto capped = request;
				capped.pixelRect = { 0, 0, largeSide, largeSide };
				capped.reservedCount = lowTier.maximumSeeds;
				capped.snapshot = largeSnapshot.Get();
				if (!check(system.Step(birth, frequency, {}, {}, {}, 1.0f, {}, &capped).clearSpawnAccepted,
					"capped GPU clear accepted")) return 1;
				actual = readSlots(0, lowTier.maximumSeeds);
				const auto cappedCount = renderer.PollClearSpawn();
				check(cappedCount && cappedCount->written == lowTier.maximumSeeds && actual.size() == cappedCount->written &&
					std::all_of(actual.begin(), actual.end(), [](const LaserGpuParticle& p)
					{
						return p.lifetimeMilliseconds != 0 && p.position[0] >= transparentWidth && p.position[0] < largeSide &&
							p.position[1] >= 0.0f && p.position[1] < largeSide;
					}), "capped clear fills the entire limit only from ink pixels");

				// 保留池内旧记录，再从同一段预留：空查询必须真正清槽，不能只把环缩回去。
				renderer.ResetLaserParticles();
				auto empty = request;
				empty.pixelRect = { 12, 12, 12, 30 };
				if (!check(submit(empty, birth), "empty rectangle reserves a batch")) return 1;
				actual = readSlots(0, reserved);
				const auto emptyCount = renderer.PollClearSpawn();
				check(emptyCount && emptyCount->written == 0 && system.ActiveRange(driftPass).count == 0 &&
					LaserParticleSystemTestAccess::DriftEndMilliseconds(system) == 0 && actual.size() == reserved &&
					std::all_of(actual.begin(), actual.end(), [](const LaserGpuParticle& p) { return p.lifetimeMilliseconds == 0; }),
					"empty rectangle clears old slots, removes the batch and resets its deadline");

				auto noInk = request;
				noInk.pixelRect = { 0, 0, 8, 8 };
				const auto previousClear = std::span<const LaserDriftParticle>(expected).first(1);
				renderer.StepLaserParticles(birth, frequency, {}, previousClear);
				const auto previousDeadline = LaserParticleSystemTestAccess::DriftEndMilliseconds(system);
				if (!check(submit(noInk, birth), "transparent rectangle accepted")) return 1;
				if (readSlots(1, 1).empty()) return 1;
				const auto noInkCount = renderer.PollClearSpawn();
				check(noInkCount && noInkCount->written == 0 && system.ActiveRange(driftPass).count == 1 &&
					LaserParticleSystemTestAccess::DriftEndMilliseconds(system) == previousDeadline,
					"zero-particle clear leaves the preceding batch and its deadline intact");

				renderer.ResetLaserParticles();
				if (!check(submit(request, birth), "GPU clear before a newer laser batch")) return 1;
				renderer.StepLaserParticles(birth + 1, frequency, {}, {}, previousClear);
				if (readSlots(reserved, 1).empty()) return 1;
				const auto behindTail = renderer.PollClearSpawn();
				check(behindTail && behindTail->written == expected.size() && system.ActiveRange(driftPass).count == reserved + 1,
					"readback cannot trim a clear batch behind a newer laser batch");

				renderer.ResetLaserParticles();
				if (!check(submit(request, birth) && submit(noInk, birth + 1), "consecutive GPU clears share the staging buffer")) return 1;
				if (readSlots(reserved, 1).empty()) return 1;
				const auto newest = renderer.PollClearSpawn();
				check(newest && newest->written == 0 && !renderer.PollClearSpawn() && system.ActiveRange(driftPass).count == reserved,
					"only the latest pending counters can trim a batch");

				renderer.ResetLaserParticles();
				if (!check(submit(request, birth), "GPU clear before tier change")) return 1;
				renderer.SetLaserParticleLimit(LaserParticleLimit::Medium);
				renderer.StepLaserParticles(birth, frequency, {}, previousClear);
				check(!renderer.PollClearSpawn() && system.ActiveRange(driftPass).count == 1,
					"tier change drops pending readback without touching the new pool");

				renderer.ResetLaserParticles();
				if (!check(submit(request, birth), "GPU clear before resetting the device generation")) return 1;
				const auto vertex = LoadShaderFromResource(IDR_DRAW3_LASER_PARTICLE_VS);
				const auto pixel = LoadShaderFromResource(IDR_DRAW3_LASER_PARTICLE_PS);
				const auto spawn = LoadShaderFromResource(IDR_DRAW3_LASER_PARTICLE_SPAWN_CS);
				system.Release();
				if (!check(system.Initialize(testDevice.Get(), testContext.Get(), { vertex.data, vertex.size },
					{ pixel.data, pixel.size }, { spawn.data, spawn.size }), "restore particle device session")) return 1;
				renderer.StepLaserParticles(birth, frequency, {}, previousClear);
				check(!renderer.PollClearSpawn() && system.ActiveRange(driftPass).count == 1,
					"device recreation drops pending readback without touching the new session");
				renderer.ResetLaserParticles();
				LaserParticleSystemTestAccess::SetClearSpawnCreationFailure(system, true);
				check(!submit(request, birth) && !renderer.GpuClearSpawnAvailable() && renderer.LaserParticlesAvailable() &&
					system.ActiveRange(driftPass).count == 0 && !renderer.PollClearSpawn(),
					"GPU resource failure disables only clear spawn and leaves no pending batch");
				LaserParticleSystemTestAccess::SetClearSpawnCreationFailure(system, false);
				const float trailColor[] = { 0.15f, 0.5f, 1.0f, 1.0f };
				std::vector<LaserParticleSeed> trail;
				AppendLaserTrailParticleSeeds(trail, 30, 30, 1, 0, 2, 1.0f, trailColor, 1, seedBase, 14, {});
				const auto fallback = renderer.StepLaserParticles(birth, frequency, trail, previousClear, previousClear);
				check(fallback.trailAccepted && fallback.clearAccepted && fallback.dissolveAccepted &&
					!renderer.GpuClearSpawnAvailable() && !renderer.PollClearSpawn() &&
					system.ActiveRange(driftPass).count == 2 && system.ActiveRange(trailPass).count == 1,
					"unavailable GPU clear stays disabled while CPU clear, laser drift and trail still work");
				renderer.ReleaseCanvasClearSnapshot();
				renderer.ClearRTV(renderer.layerL2RTV.Get(), kTransparentLayerClearColor);
				renderer.ResetLaserParticles();
				renderer.ReleaseIdleLaserParticles();
			}
			std::vector<unsigned char> laserReference;
			for (int cycle = 0; cycle < 3; ++cycle)
			{
				if (!check(renderer.EnsureLaserResources(64, 64), "Laser selection prepares full group")) return 1;
				check(renderer.LaserResourceBytes() == 2ull * 64 * 64 * 4, "two full-sized selected textures");
				report("selected");
				ID3D11Texture2D* selectedColor = renderer.laserCompositedColor.texture.Get();
				check(renderer.EnsureLaserResources(64, 64) &&
					renderer.laserCompositedColor.texture.Get() == selectedColor,
					"same selection does not recreate resources");
				renderer.failLaserPreparationAfterColor_ = true;
				check(!renderer.EnsureLaserResources(96, 80) &&
					renderer.laserCompositedColor.texture.Get() == selectedColor,
					"replacement failure retains previous complete group");
				renderer.failLaserPreparationAfterColor_ = false;
				renderer.ConfigureLaserStyle(1, DirectX::XMFLOAT4(1, 0, 0, 1));
				renderer.SetLaserCoverageTarget(renderer.laserStrokeCoverage);
				const std::array<InkPoint, 2> laser{ InkPoint{ 8, 24, 2, 0 }, InkPoint{ 40, 24, 2, 1 } };
				const RECT resolveRect = { 0, 0, 64, 64 };
				if (check(renderer.DrawLaserCoverage(laser) == 0 &&
					renderer.ResolveLaserStrokeCoverage(renderer.LaserBakeTarget(), resolveRect),
					"selected Laser renders into real committed pixels"))
					renderer.CommitLaserBake(resolveRect);
				const auto colorPixels = pixels(renderer.laserCompositedColor.texture.Get());
				check(!colorPixels.empty() && colorPixels[(24 * 64 + 20) * 4 + 3] > 0,
					"selected Laser pixels are visible");
				if (cycle == 0) laserReference = colorPixels;
				else check(colorPixels == laserReference, "re-entry pixel output is identical");
				check(renderer.EnsureLaserIncrementalCoverageResources() && renderer.BeginLaserBake(resolveRect, true) &&
					renderer.LaserResourceBytes() == 4ull * 64 * 64 * 4, "live and transactional scratch are accounted");
				report("live_and_bake");
				renderer.SetOMTarget(renderer.laserStrokeCoverage.rtv.Get());
				ID3D11ShaderResourceView* boundColor[] = { renderer.laserCompositedColor.srv.Get() };
				testContext->PSSetShaderResources(6, 1, boundColor);
				renderer.ReleaseLaserResources();
				Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> boundSource;
				Microsoft::WRL::ComPtr<ID3D11RenderTargetView> boundTarget;
				testContext->PSGetShaderResources(6, 1, boundSource.GetAddressOf());
				testContext->OMGetRenderTargets(1, boundTarget.GetAddressOf(), nullptr);
				check(!boundSource && !boundTarget && !renderer.LaserIncrementalCoverageAvailable() &&
					renderer.LaserResourceBytes() == 0, "leaving releases every view/texture and binding");
				report("left");
				renderer.ClearRTV(renderer.backBufferRTV.Get(), kTransparentLayerClearColor);
				check(renderer.ApplyOperatorLayers(renderer.backBufferRTV.Get(), renderer.layerL1, renderer.layerL0,
					{ 0, 0, 64, 64 }) && pixels(renderer.backBufferTexture.Get()) == ordinaryPixels,
					"ordinary output has no Laser ghost after release");
			}
			check(renderer.EnsureLaserResources(64, 64) && renderer.EnsureLaserIncrementalCoverageResources(),
				"select before resize");
			renderer.SetLaserCoverageTarget(renderer.laserStrokeCoverage);
			const std::array<InkPoint, 2> resizeLaser{ InkPoint{ 8, 24, 2, 0 }, InkPoint{ 40, 24, 2, 1 } };
			const RECT resizeResolveRect = { 0, 0, 64, 64 };
			if (check(renderer.DrawLaserCoverage(resizeLaser) == 0 &&
				renderer.ResolveLaserStrokeCoverage(renderer.LaserBakeTarget(), resizeResolveRect),
				"real committed content before resize"))
				renderer.CommitLaserBake(resizeResolveRect);
			const auto beforeResize = pixels(renderer.laserCompositedColor.texture.Get());
			check(renderer.Resize(swapChain.Get(), 96, 80) && renderer.LaserResourcesAvailable() &&
				renderer.LaserResourceBytes() == 3ull * 96 * 80 * 4, "selected resize rebuilds only selected group");
			const auto afterResize = pixels(renderer.laserCompositedColor.texture.Get());
			bool resizePixelsMatch = beforeResize.size() == 64 * 64 * 4 && afterResize.size() == 96 * 80 * 4;
			for (size_t row = 0; resizePixelsMatch && row < 64; ++row)
				resizePixelsMatch = std::memcmp(beforeResize.data() + row * 64 * 4,
					afterResize.data() + row * 96 * 4, 64 * 4) == 0;
			check(resizePixelsMatch, "resize preserves committed Laser pixels in the intersection");
			report("selected_resized");
			renderer.ReleaseLaserResources();
			check(renderer.Resize(swapChain.Get(), 32, 48) && renderer.LaserResourceBytes() == 0,
				"released resize stays without Laser");
			{
				// ULW 无交换链：最终目标是独立纹理，Resize 同样保留 L2/L1 交集并重建最终纹理。
				InkRenderer offscreen;
				if (check(offscreen.Init(testDevice.Get(), testContext.Get(), nullptr, 64, 64,
					D3D_DRIVER_TYPE_WARP, true), "offscreen renderer initialization"))
				{
					offscreen.ClearRTV(offscreen.layerL2RTV.Get(), DirectX::XMFLOAT4(0.25f, 0.5f, 0.75f, 1.0f));
					offscreen.ClearOperatorLayer(offscreen.layerL1);
					offscreen.ClearOperatorLayer(offscreen.layerL0);
					offscreen.SetOperatorTarget(offscreen.layerL1);
					check(offscreen.DrawStroke(pen, DirectX::XMFLOAT4(1, 0, 0, 1)) == 0, "offscreen L1 stroke");
					// retain 层是 R16_FLOAT（每像素 2 字节），按实际像素字节数读回逐行比较。
					const auto readRows = [&](ID3D11Texture2D* texture, size_t bytesPerPixel)
					{
						std::vector<unsigned char> result;
						if (!texture) return result;
						D3D11_TEXTURE2D_DESC readDescription = {};
						texture->GetDesc(&readDescription);
						readDescription.Usage = D3D11_USAGE_STAGING;
						readDescription.BindFlags = 0;
						readDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
						Microsoft::WRL::ComPtr<ID3D11Texture2D> readback;
						if (FAILED(testDevice->CreateTexture2D(&readDescription, nullptr, readback.GetAddressOf()))) return result;
						testContext->OMSetRenderTargets(0, nullptr, nullptr);
						testContext->CopyResource(readback.Get(), texture);
						D3D11_MAPPED_SUBRESOURCE mapped = {};
						if (FAILED(testContext->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return result;
						const size_t rowBytes = static_cast<size_t>(readDescription.Width) * bytesPerPixel;
						result.resize(rowBytes * readDescription.Height);
						for (UINT row = 0; row < readDescription.Height; ++row)
							std::memcpy(result.data() + row * rowBytes,
								static_cast<const unsigned char*>(mapped.pData) + static_cast<size_t>(row) * mapped.RowPitch, rowBytes);
						testContext->Unmap(readback.Get(), 0);
						return result;
					};
					const auto beforeL2 = readRows(offscreen.layerL2Texture.Get(), 4);
					const auto beforeL1Add = readRows(offscreen.layerL1.addTexture.Get(), 4);
					const auto beforeL1Retain = readRows(offscreen.layerL1.retainTexture.Get(), 2);
					check(std::any_of(beforeL1Add.begin(), beforeL1Add.end(), [](unsigned char value) { return value != 0; }),
						"offscreen stroke wrote real L1 pixels");
					ID3D11Texture2D* oldFinal = offscreen.backBufferTexture.Get();
					check(offscreen.Resize(nullptr, 96, 80), "offscreen resize without swapchain");
					D3D11_TEXTURE2D_DESC finalDescription = {};
					if (offscreen.backBufferTexture) offscreen.backBufferTexture->GetDesc(&finalDescription);
					check(offscreen.backBufferTexture && offscreen.backBufferTexture.Get() != oldFinal &&
						finalDescription.Width == 96 && finalDescription.Height == 80 &&
						(finalDescription.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0 && offscreen.backBufferRTV,
						"offscreen final texture rebuilt at new size");
					const auto keepsIntersection = [&](const std::vector<unsigned char>& before, ID3D11Texture2D* texture,
						size_t bytesPerPixel)
					{
						const auto after = readRows(texture, bytesPerPixel);
						bool same = before.size() == 64 * 64 * bytesPerPixel && after.size() == 96 * 80 * bytesPerPixel;
						for (size_t row = 0; same && row < 64; ++row)
							same = std::memcmp(before.data() + row * 64 * bytesPerPixel,
								after.data() + row * 96 * bytesPerPixel, 64 * bytesPerPixel) == 0;
						return same;
					};
					check(keepsIntersection(beforeL2, offscreen.layerL2Texture.Get(), 4), "offscreen resize preserves L2 intersection");
					check(keepsIntersection(beforeL1Add, offscreen.layerL1.addTexture.Get(), 4), "offscreen resize preserves L1 add intersection");
					check(keepsIntersection(beforeL1Retain, offscreen.layerL1.retainTexture.Get(), 2), "offscreen resize preserves L1 retain intersection");
					offscreen.ReleaseResources();
				}
			}
			renderer.ReleaseResources();
			Microsoft::WRL::ComPtr<ID3D11Device> recoveredDevice;
			Microsoft::WRL::ComPtr<ID3D11DeviceContext> recoveredContext;
			if (!check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
				D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
				recoveredDevice.GetAddressOf(), &level, recoveredContext.GetAddressOf())), "new WARP device epoch")) return 1;
			Microsoft::WRL::ComPtr<IDXGIDevice> recoveredDxgiDevice;
			Microsoft::WRL::ComPtr<IDXGIAdapter> recoveredAdapter;
			Microsoft::WRL::ComPtr<IDXGIFactory2> recoveredFactory;
			if (!check(SUCCEEDED(recoveredDevice.As(&recoveredDxgiDevice)) &&
				SUCCEEDED(recoveredDxgiDevice->GetAdapter(recoveredAdapter.GetAddressOf())) &&
				SUCCEEDED(recoveredAdapter->GetParent(IID_PPV_ARGS(recoveredFactory.GetAddressOf()))),
				"factory belongs to recovered device epoch")) return 1;
			Microsoft::WRL::ComPtr<IDXGISwapChain1> recoveredSwapChain;
			description.Width = 32;
			description.Height = 48;
			if (!check(SUCCEEDED(recoveredFactory->CreateSwapChainForComposition(recoveredDevice.Get(), &description,
				nullptr, recoveredSwapChain.GetAddressOf())), "new epoch swapchain without HWND")) return 1;
			check(renderer.Init(recoveredDevice.Get(), recoveredContext.Get(), recoveredSwapChain.Get(), 32, 48,
				D3D_DRIVER_TYPE_WARP, true) && renderer.LaserResourceBytes() == 0 &&
				renderer.EnsureLaserResources(32, 48), "fresh graphics recovery prepares only after selection");
			Microsoft::WRL::ComPtr<ID3D11Device> resourceDevice;
			if (renderer.laserCompositedColor.texture)
				renderer.laserCompositedColor.texture->GetDevice(resourceDevice.GetAddressOf());
			check(resourceDevice.Get() == recoveredDevice.Get(), "recovered textures belong to the new device");
			renderer.ReleaseLaserResources();
			report("recovery_left");
			renderer.ReleaseResources();
			std::fprintf(stdout, "LaserResources failures=%d hwnd=0 resource_bytes_are_nominal=1\n", failures);
			return failures ? 1 : 0;
		}
		catch (...) { std::fputs("[LaserResources] unexpected exception\n", stderr); return 2; }
#endif
	}

	static int RunSweepOnlyStrokeGeometryValidation(InkRenderer& renderer, const OperatorLayerResources& layer)
	{
		const auto check = [](bool condition, const char* name) {
			if (!condition) std::fprintf(stderr, "[Draw3Sweep] FAIL %s\n", name);
			return condition;
		};
		const auto createReadback = [&](ID3D11Texture2D* source,
			Microsoft::WRL::ComPtr<ID3D11Texture2D>& staging) {
			D3D11_TEXTURE2D_DESC description = {};
			source->GetDesc(&description);
			description.Usage = D3D11_USAGE_STAGING;
			description.BindFlags = description.MiscFlags = 0;
			description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			return renderer.device->CreateTexture2D(&description, nullptr, staging.GetAddressOf());
		};
		Microsoft::WRL::ComPtr<ID3D11Texture2D> addStaging, retainStaging;
		D3D11_BUFFER_DESC constantsDescription = {};
		renderer.globalCB->GetDesc(&constantsDescription);
		constantsDescription.Usage = D3D11_USAGE_STAGING;
		constantsDescription.BindFlags = 0;
		constantsDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		Microsoft::WRL::ComPtr<ID3D11Buffer> constantsStaging;
		if (!check(SUCCEEDED(createReadback(layer.addTexture.Get(), addStaging)) &&
			SUCCEEDED(createReadback(layer.retainTexture.Get(), retainStaging)) &&
			SUCCEEDED(renderer.device->CreateBuffer(&constantsDescription, nullptr,
				constantsStaging.GetAddressOf())), "create readbacks")) return 1;

		D3D11_TEXTURE2D_DESC target = {};
		layer.addTexture->GetDesc(&target);
		renderer.SetScreenSize(static_cast<float>(target.Width), static_cast<float>(target.Height));
		const size_t pixelCount = static_cast<size_t>(target.Width) * target.Height;
		struct Snapshot
		{
			std::vector<uint32_t> add;
			std::vector<uint16_t> retain;
			float stripCount = 0.0f;
		};
		Snapshot full{ std::vector<uint32_t>(pixelCount), std::vector<uint16_t>(pixelCount) };
		Snapshot sweep{ std::vector<uint32_t>(pixelCount), std::vector<uint16_t>(pixelCount) };
		const auto readTexture = [&](ID3D11Texture2D* source, ID3D11Texture2D* staging,
			void* destination, size_t rowBytes) {
			renderer.context->CopyResource(staging, source);
			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (FAILED(renderer.context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) return false;
			for (UINT y = 0; y < target.Height; ++y)
				std::memcpy(static_cast<unsigned char*>(destination) + y * rowBytes,
					static_cast<const unsigned char*>(mapped.pData) + y * static_cast<size_t>(mapped.RowPitch), rowBytes);
			renderer.context->Unmap(staging, 0);
			return true;
		};
		const auto readSnapshot = [&](Snapshot& snapshot) {
			renderer.context->OMSetRenderTargets(0, nullptr, nullptr);
			if (!readTexture(layer.addTexture.Get(), addStaging.Get(), snapshot.add.data(),
				target.Width * sizeof(snapshot.add.front())) ||
				!readTexture(layer.retainTexture.Get(), retainStaging.Get(), snapshot.retain.data(),
					target.Width * sizeof(snapshot.retain.front()))) return false;
			renderer.context->CopyResource(constantsStaging.Get(), renderer.globalCB.Get());
			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (FAILED(renderer.context->Map(constantsStaging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
			// 从提交后的常量缓冲读回末批的 K，防止两个路径都退化为原四边形。
			snapshot.stripCount = static_cast<const GlobalShaderConstants*>(mapped.pData)->padding[1];
			renderer.context->Unmap(constantsStaging.Get(), 0);
			return true;
		};

		// 第一批保持细笔并把重复点置于屏外，跨真实上传边界而不放大 WARP 的绘制成本。
		std::vector<InkPoint> crossBatch(InkRenderer::kMaxBufferCapacity + 1, { -10, 300, 4, 0 });
		crossBatch[InkRenderer::kMaxBufferCapacity - 2] = { 300, 300, 4, 0 };
		crossBatch[InkRenderer::kMaxBufferCapacity - 1] = { 310, 300, 23, 0 };
		crossBatch.back() = { 320, 300, 30, 0 };
		// 两批都是粗笔：第二批含两条线段，第一条不挖洞、第二条挖洞。重复点是零长线段，按原四边形画在屏外。
		std::vector<InkPoint> crossBatchThick(InkRenderer::kMaxBufferCapacity + 2, { -1000, 300, 30, 0 });
		crossBatchThick[InkRenderer::kMaxBufferCapacity - 2] = { 290, 300, 30, 0 };
		crossBatchThick[InkRenderer::kMaxBufferCapacity - 1] = { 300, 300, 30, 0 };
		crossBatchThick[InkRenderer::kMaxBufferCapacity] = { 310, 300, 30, 0 };
		crossBatchThick[InkRenderer::kMaxBufferCapacity + 1] = { 320, 300, 30, 0 };
		struct SweepCase
		{
			const char* name;
			std::vector<InkPoint> points;
			float expectedStrips;
		};
		const SweepCase cases[] = {
			{ "radius-30", { { 330, 335, 30, 0 }, { 340, 335, 30, 0 }, { 350, 335, 30, 0 } }, 8 },
			{ "radius-150", { { 330, 335, 150, 0 }, { 340, 335, 150, 0 }, { 350, 335, 150, 0 } }, 32 },
			{ "radius-300", { { 330, 335, 300, 0 }, { 340, 335, 300, 0 }, { 350, 335, 300, 0 } }, 64 },
			{ "growing", { { 200, 300, 40, 0 }, { 210, 300, 95, 0 }, { 310, 300, 120, 0 },
				{ 320, 300, 180, 0 }, { 420, 300, 200, 0 } }, 64 },
			{ "shrinking", { { 200, 300, 200, 0 }, { 210, 300, 180, 0 }, { 310, 300, 120, 0 },
				{ 320, 300, 95, 0 }, { 420, 300, 40, 0 } }, 64 },
			{ "turn-90", { { 300, 300, 150, 0 }, { 310, 300, 150, 0 }, { 310, 310, 150, 0 } }, 32 },
			{ "turn-170", { { 300, 300, 150, 0 }, { 310, 300, 150, 0 }, { 300.15192f, 301.73648f, 150, 0 } }, 32 },
			{ "zigzag", { { 300, 300, 30, 0 }, { 310, 310, 30, 0 }, { 300, 320, 30, 0 },
				{ 310, 330, 30, 0 }, { 300, 340, 30, 0 } }, 8 },
			{ "length-0.5", { { 330, 335, 150, 0 }, { 330.5f, 335, 150, 0 }, { 331, 335, 150, 0 } }, 32 },
			{ "length-400", { { 120, 335, 30, 0 }, { 520, 335, 30, 0 }, { 120, 335, 30, 0 } }, 8 },
			{ "two-points", { { 300, 300, 30, 0 }, { 310, 300, 30, 0 } }, 8 },
			{ "zero-length", { { 300, 300, 150, 0 }, { 310, 300, 150, 0 }, { 310, 300, 150, 0 },
				{ 320, 300, 150, 0 } }, 32 },
			{ "clipped-40-to-200", { { 300, 300, 40, 0 }, { 310, 300, 200, 0 } }, 64 },
			{ "angled-clip-edge", { { 300.5f, 300.5f, 40, 0 }, { 306.5f, 308.5f, 200, 0 } }, 64 },
			{ "clipped-predecessor", { { 300, 300, 4, 0 }, { 300, 300, 4, 0 }, { 300, 300, 4, 0 },
				{ 310, 300, 23, 0 }, { 320, 300, 30, 0 } }, 8 },
			{ "cross-batch", std::move(crossBatch), 8 },
			{ "cross-batch-thick", std::move(crossBatchThick), 8 },
			// 同一批里有很粗的点时，细线段不得被切成几分之一像素高的横条（审阅复现过超出容差的差别）。
			{ "thin-with-huge-point", { { 300.39642334f, 300.51046753f, 1.15000427f, 0 },
				{ 310.36483765f, 291.69979858f, 1.22317386f, 0 }, { 700, 50, 256, 0 } }, 64 },
			{ "mixed-tiers", { { 200, 300, 30, 0 }, { 210, 300, 30, 0 }, { 220, 300, 30, 0 },
				{ 560, 300, 300, 0 }, { 570, 300, 300, 0 } }, 64 },
			// 前一个点半径低于 1，后一条线段虽然很粗也不得挖洞。
			{ "thin-predecessor", { { 300, 300, 0.6f, 0 }, { 345, 300, 40, 0 }, { 355, 300, 40, 0 } }, 8 },
			// 笔锋收尾：半径低于 1 的线段走细线覆盖公式，其后一条不得挖洞。
			{ "taper-out", { { 300, 300, 30, 0 }, { 310, 300, 30, 0 }, { 330, 300, 12, 0 },
				{ 340, 300, 3, 0 }, { 343, 300, 0.6f, 0 } }, 8 },
			{ "taper-in", { { 343, 300, 0.6f, 0 }, { 340, 300, 3, 0 }, { 330, 300, 12, 0 },
				{ 310, 300, 30, 0 }, { 300, 300, 30, 0 } }, 8 },
			{ "negative-radius-batch", { { -10, 300, -1, 0 }, { 300, 300, 30, 0 }, { 310, 300, 30, 0 } }, 0 },
			{ "large-coordinate-batch", { { 300, 300, 30, 0 }, { 310, 300, 30, 0 }, { 1e8f, 300, 30, 0 } }, 0 },
		};

		using DirectX::PackedVector::XMConvertHalfToFloat;
		constexpr uint16_t halfOne = 0x3c00;
		constexpr uint16_t halfHalf = 0x3800;
		constexpr float tolerance = 1.0f / 255.0f;
		int failures = 0;
		for (const SweepCase& test : cases)
		{
			for (InkOperatorKind kind : { InkOperatorKind::Erase, InkOperatorKind::Draw })
			{
				for (float opacity : { 1.0f, 0.5f })
				{
					const DirectX::XMFLOAT4 color(0.25f, 0.5f, 0.75f, opacity);
					const auto draw = [&] {
						renderer.ClearOperatorLayer(layer);
						renderer.SetOperatorTarget(layer);
						return renderer.DrawStroke(test.points, color, StrokeShape::RoundCapsule, kind);
					};
					renderer.SetSweepOnlyStrokeGeometryEnabled(false);
					const int fullResult = draw();
					renderer.SetSweepOnlyStrokeGeometryEnabled(true);
					if (!check(fullResult == 0 && readSnapshot(full) && draw() == 0 &&
						readSnapshot(sweep), test.name)) return failures + 1;
					if (!check(full.stripCount == 0.0f && sweep.stripCount == test.expectedStrips, test.name))
						++failures;

					const uint16_t saturatedRetain = kind == InkOperatorKind::Draw && opacity == 0.5f ? halfHalf : 0;
					size_t differingPixels = 0;
					size_t differingEndpointPixels = 0;
					float maxDifference = 0.0f;
					bool hasCoverage = false;
					for (size_t i = 0; i < pixelCount; ++i)
					{
						hasCoverage |= full.add[i] != 0 || full.retain[i] != halfOne;
						if (full.add[i] == sweep.add[i] && full.retain[i] == sweep.retain[i]) continue;
						++differingPixels;
						// 擦除的 Add 始终为零，透明与饱和都通过 Retain 的端值判断。
						if ((full.retain[i] == halfOne && full.add[i] == 0) || full.retain[i] == saturatedRetain)
							++differingEndpointPixels;
						const float retainDifference = std::abs(
							XMConvertHalfToFloat(full.retain[i]) - XMConvertHalfToFloat(sweep.retain[i]));
						maxDifference = (std::max)(maxDifference, std::isfinite(retainDifference)
							? retainDifference : std::numeric_limits<float>::infinity());
						for (unsigned shift = 0; shift < 32; shift += 8)
						{
							const int before = static_cast<int>((full.add[i] >> shift) & 255);
							const int after = static_cast<int>((sweep.add[i] >> shift) & 255);
							maxDifference = (std::max)(maxDifference, std::abs(before - after) / 255.0f);
						}
					}
					std::fprintf(stderr,
						"[Draw3Sweep] case=%s operator=%s opacity=%.1f K=%.0f differing_pixels=%zu max_diff=%.9g endpoint_differences=%zu\n",
						test.name, kind == InkOperatorKind::Erase ? "erase" : "draw", opacity,
						sweep.stripCount == 0.0f ? 1.0f : sweep.stripCount,
						differingPixels, maxDifference, differingEndpointPixels);
					if (!check(hasCoverage, "legacy draw has coverage")) ++failures;
					if (!check(maxDifference <= tolerance, "Add/Retain difference within 1/255")) ++failures;
					// 透明与饱和端值上的差别同样只来自不同三角形的插值噪声，已由上面的 1/255 上限约束；
					// 这里只统计并输出，便于发现异常增多。
				}
			}
		}

		// 首批的半尺寸低于门槛；第二批共享首批末点，并在第二段真正挖洞。
		std::vector<InkPoint> rectCrossBatch(InkRenderer::kMaxBufferCapacity + 2, { -1000, 300, 20, 0 });
		rectCrossBatch[InkRenderer::kMaxBufferCapacity - 2] = { 285, 290, 20, 0 };
		rectCrossBatch[InkRenderer::kMaxBufferCapacity - 1] = { 300, 300, 20, 0 };
		rectCrossBatch[InkRenderer::kMaxBufferCapacity] = { 310, 310, 48, 0 };
		rectCrossBatch.back() = { 355, 315, 48, 0 };
		struct RectSweepCase
		{
			const char* name;
			std::vector<InkPoint> points;
			size_t holeModeSegments;
			bool expectLessShading = false;
		};
		const RectSweepCase rectCases[] = {
			{ "straight", { { 280, 300, 48, 0 }, { 300, 300, 48, 0 }, { 320, 300, 48, 0 } }, 2, true },
			{ "diagonal", { { 280.25f, 280.5f, 48, 0 }, { 308, 301, 48, 0 }, { 337, 325, 48, 0 } }, 2 },
			{ "sharp-turn", { { 280, 300, 48, 0 }, { 330, 300, 48, 0 }, { 315, 320, 48, 0 } }, 2 },
			{ "reversal", { { 280, 300, 48, 0 }, { 330, 300, 48, 0 }, { 280, 300, 48, 0 } }, 2 },
			{ "growing", { { 280, 300, 24, 0 }, { 285, 303, 48, 0 }, { 370, 335, 64, 0 } }, 2 },
			{ "shrinking", { { 280, 300, 64, 0 }, { 285, 303, 48, 0 }, { 370, 335, 24, 0 } }, 2 },
			{ "short-segments", { { 300.25f, 300, 48, 0 }, { 300.75f, 300.25f, 48, 0 }, { 301, 301, 48, 0 } }, 2 },
			{ "long-segments", { { 120, 150, 36, 0 }, { 590, 330, 36, 0 }, { 120, 520, 36, 0 } }, 2 },
			{ "zero-length", { { 280, 300, 48, 0 }, { 300, 300, 48, 0 }, { 300, 300, 60, 0 },
				{ 320, 315, 48, 0 } }, 3 },
			{ "two-points", { { 280, 300, 48, 0 }, { 300, 310, 48, 0 } }, 0 },
			{ "below-threshold", { { 280, 300, 20, 0 }, { 300, 310, 20, 0 }, { 320, 300, 20, 0 } }, 0 },
			{ "small-hole-fallback", { { 280, 300, 48, 0 }, { 300, 310, 4, 0 }, { 320, 300, 48, 0 } }, 2 },
			{ "cross-batch", std::move(rectCrossBatch), 2 },
		};
		Microsoft::WRL::ComPtr<ID3D11Query> query;
		const D3D11_QUERY_DESC queryDescription = { D3D11_QUERY_PIPELINE_STATISTICS, 0 };
		if (!check(SUCCEEDED(renderer.device->CreateQuery(&queryDescription, query.GetAddressOf())),
			"rectangular sweep statistics query")) return failures + 1;
		for (const EraserShape shape : { EraserShape::Rectangle, EraserShape::Square })
		{
			for (const RectSweepCase& test : rectCases)
			{
				std::vector<InkPoint> upload;
				upload.reserve(test.points.size());
				for (const InkPoint& point : test.points)
				{
					const auto half = ResolveEraserHalfSize(shape, point.r);
					upload.push_back({ point.x, point.y, half.x, half.y });
				}
				const auto draw = [&](bool enabled, Snapshot& snapshot, D3D11_QUERY_DATA_PIPELINE_STATISTICS& stats) {
					renderer.SetSweepOnlyStrokeGeometryEnabled(enabled);
					renderer.ClearOperatorLayer(layer);
					renderer.SetOperatorTarget(layer);
					renderer.context->Begin(query.Get());
					const int result = renderer.DrawStroke(upload, { 0.0f, 0.0f, 0.0f, 0.0f },
						StrokeShape::EraseRectSweep, InkOperatorKind::Erase);
					renderer.context->End(query.Get());
					if (result != 0 || !readSnapshot(snapshot)) return false;
					HRESULT queryResult = S_FALSE;
					for (int attempt = 0; attempt < 1000 && queryResult == S_FALSE; ++attempt)
						queryResult = renderer.context->GetData(query.Get(), &stats, sizeof(stats), 0);
					return queryResult == S_OK;
				};
				D3D11_QUERY_DATA_PIPELINE_STATISTICS fullStats = {}, sweepStats = {};
				if (!check(draw(false, full, fullStats) && draw(true, sweep, sweepStats), test.name))
					return failures + 1;

				size_t differingPixels = 0;
				size_t differingOffBandPixels = 0;
				float maxDifference = 0.0f;
				bool hasCoverage = false;
				for (size_t i = 0; i < pixelCount; ++i)
				{
					hasCoverage |= full.retain[i] != halfOne;
					if (full.add[i] == sweep.add[i] && full.retain[i] == sweep.retain[i]) continue;
					++differingPixels;
					const size_t x = i % target.Width, y = i / target.Width;
					// 端值紧贴 AA 带时也可能有插值差异；用四邻域识别远离边缘的像素。
					const uint16_t retain = full.retain[i];
					if ((retain == 0 || retain == halfOne) && x > 0 && x + 1 < target.Width &&
						y > 0 && y + 1 < target.Height && full.retain[i - 1] == retain && full.retain[i + 1] == retain &&
						full.retain[i - target.Width] == retain && full.retain[i + target.Width] == retain)
						++differingOffBandPixels;
					const float difference = std::abs(
						XMConvertHalfToFloat(full.retain[i]) - XMConvertHalfToFloat(sweep.retain[i]));
					maxDifference = (std::max)(maxDifference, std::isfinite(difference)
						? difference : std::numeric_limits<float>::infinity());
				}
				std::fprintf(stderr,
					"[Draw3Sweep] shape=%s case=%s hole_mode=%.0f differing_pixels=%zu max_diff=%.9g off_band_differences=%zu\n",
					shape == EraserShape::Rectangle ? "rectangle" : "square", test.name, sweep.stripCount,
					differingPixels, maxDifference, differingOffBandPixels);

				const size_t legacyVertices = 6 * (upload.size() - 1);
				if (!check(fullStats.IAVertices == legacyVertices && full.stripCount == 0.0f &&
					sweepStats.IAVertices == legacyVertices + 18 * test.holeModeSegments &&
					sweep.stripCount == (test.holeModeSegments > 0 ? 1.0f : 0.0f),
					"rectangular sweep used the expected batch geometry")) ++failures;
				if (test.expectLessShading && !check(sweepStats.PSInvocations < fullStats.PSInvocations,
					"rectangular holes reduce pixel shader invocations")) ++failures;
				if (!check(hasCoverage && full.add == sweep.add && differingOffBandPixels == 0 &&
					maxDifference <= tolerance && (test.holeModeSegments > 0 || differingPixels == 0),
					"rectangular sweep preserves Add and Retain outside AA, with AA within 1/255")) ++failures;
			}
		}
		return failures;
	}

	int RunRendererUploadValidation() noexcept
	{
		try
		{
			int failures = 0;
			const auto check = [&](bool condition, const char* name) {
				if (!condition) { ++failures; std::fprintf(stderr, "[Draw3Upload] FAIL %s\n", name); }
				return condition;
			};
			check(UseZhaoxinDefaultUpload(true, D3D_DRIVER_TYPE_HARDWARE, true, 0x1D17) &&
				!UseZhaoxinDefaultUpload(false, D3D_DRIVER_TYPE_HARDWARE, true, 0x1D17) &&
				!UseZhaoxinDefaultUpload(true, D3D_DRIVER_TYPE_WARP, true, 0x1D17) &&
				!UseZhaoxinDefaultUpload(true, D3D_DRIVER_TYPE_HARDWARE, false, 0x1D17) &&
				!UseZhaoxinDefaultUpload(true, D3D_DRIVER_TYPE_HARDWARE, true, 0x5143), "compatibility policy gates");
			Microsoft::WRL::ComPtr<ID3D11Device> testDevice;
			Microsoft::WRL::ComPtr<ID3D11DeviceContext> testContext;
			constexpr D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
			D3D_FEATURE_LEVEL actualLevel = {};
			if (!check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
				D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
				testDevice.GetAddressOf(), &actualLevel, testContext.GetAddressOf())), "offscreen WARP device")) return 1;
			D3D11_FEATURE_DATA_D3D11_OPTIONS options = {};
			const bool canAppend = SUCCEEDED(testDevice->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS,
				&options, sizeof(options))) && options.MapNoOverwriteOnDynamicBufferSRV;
			std::vector<unsigned char> referencePixels;
			{
				// 用独立的渲染器实例：这组用例会推进结构化缓冲的游标，不能影响后面按游标断言的上传用例。
				InkRenderer sweepRenderer;
				sweepRenderer.device = testDevice;
				sweepRenderer.context = testContext;
				OperatorLayerResources sweepLayer;
				if (!check(sweepRenderer.CreateFixedResources(D3D_DRIVER_TYPE_WARP) &&
					sweepRenderer.CreateOperatorLayerResources(768, 672, sweepLayer),
					"sweep renderer resources")) return 1;
				failures += RunSweepOnlyStrokeGeometryValidation(sweepRenderer, sweepLayer);
			}
			// 两种动态能力分支与 DEFAULT 都调用产品 Draw 方法，不通过测试替身模拟上传。
			for (int mode = 0; mode < 3; ++mode)
			{
				if (mode == 1 && !canAppend) continue;
				InkRenderer renderer;
				renderer.device = testDevice;
				renderer.context = testContext;
				renderer.useDefaultStructuredUpload_ = mode == 2;
				renderer.mapNoOverwriteOnDynamicBufferSRV = mode == 1;
				if (!check(renderer.CreateFixedResources(D3D_DRIVER_TYPE_WARP), "production fixed resources")) return 1;
				D3D11_BUFFER_DESC description = {};
				renderer.inkDataBuffer->GetDesc(&description);
				check(description.ByteWidth == 3200000 && description.Usage ==
					(mode == 2 ? D3D11_USAGE_DEFAULT : D3D11_USAGE_DYNAMIC) &&
					description.CPUAccessFlags == (mode == 2 ? 0u : D3D11_CPU_ACCESS_WRITE), "ink resource policy");
				renderer.highlighterPrimitiveBuffer->GetDesc(&description);
				check(description.ByteWidth == 4800000 && description.Usage ==
					(mode == 2 ? D3D11_USAGE_DEFAULT : D3D11_USAGE_DYNAMIC), "highlighter resource policy");
				renderer.compositeVertexBuffer->GetDesc(&description);
				check(description.ByteWidth == 256 && description.Usage == D3D11_USAGE_DYNAMIC &&
					description.StructureByteStride == sizeof(InkPoint), "independent composite resource");
				D3D11_TEXTURE2D_DESC targetDescription = {};
				targetDescription.Width = targetDescription.Height = 64;
				targetDescription.MipLevels = targetDescription.ArraySize = targetDescription.SampleDesc.Count = 1;
				targetDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
				targetDescription.Usage = D3D11_USAGE_DEFAULT;
				targetDescription.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
				if (!check(SUCCEEDED(testDevice->CreateTexture2D(&targetDescription, nullptr,
					renderer.backBufferTexture.GetAddressOf())) && SUCCEEDED(testDevice->CreateRenderTargetView(
					renderer.backBufferTexture.Get(), nullptr, renderer.backBufferRTV.GetAddressOf())) &&
					renderer.CreateOperatorLayerResources(64, 64, renderer.layerL1) &&
					renderer.CreateOperatorLayerResources(64, 64, renderer.layerL0) &&
					renderer.CreateLaserCoverageResources(64, 64, renderer.laserStrokeCoverage), "offscreen render targets")) return 1;
				renderer.SetScreenSize(64, 64);
				renderer.ClearRTV(renderer.backBufferRTV.Get(), kTransparentLayerClearColor);
				renderer.ClearOperatorLayer(renderer.layerL1);
				renderer.ClearOperatorLayer(renderer.layerL0);
				const auto readBytes = [&](ID3D11Buffer* source, UINT offset, const void* expected,
					UINT bytes, const char* name) {
					D3D11_BUFFER_DESC readDescription = {};
					source->GetDesc(&readDescription);
					readDescription.Usage = D3D11_USAGE_STAGING;
					readDescription.BindFlags = readDescription.MiscFlags = readDescription.StructureByteStride = 0;
					readDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
					Microsoft::WRL::ComPtr<ID3D11Buffer> readback;
					if (!check(SUCCEEDED(testDevice->CreateBuffer(&readDescription, nullptr,
						readback.GetAddressOf())), "create upload readback")) return false;
					// 常量缓冲区也按完整资源复制，避免测试使用不支持的局部 CB copy。
					testContext->CopyResource(readback.Get(), source);
					D3D11_MAPPED_SUBRESOURCE mapped = {};
					if (!check(SUCCEEDED(testContext->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
						"map upload readback")) return false;
					const bool same = std::memcmp(static_cast<unsigned char*>(mapped.pData) + offset, expected, bytes) == 0;
					testContext->Unmap(readback.Get(), 0);
					return check(same, name);
				};
				const auto checkOffset = [&](UINT expected) {
					readBytes(renderer.globalCB.Get(), 12, &expected, sizeof(expected), "shader batch offset");
				};
				const auto composite = [&] {
					const size_t head = renderer.m_bufferHead;
					check(renderer.ApplyOperatorLayers(renderer.backBufferRTV.Get(), renderer.layerL1,
						renderer.layerL0, { 0, 0, 64, 64 }), "production composite submission");
					check(renderer.m_bufferHead == head, "composite preserves ink head");
					const InkPoint rectangle[] = { { 0, 0, 0, 0 }, { 64, 64, 0, 0 } };
					readBytes(renderer.compositeVertexBuffer.Get(), 0, rectangle, sizeof(rectangle), "composite rectangle bytes");
				};
				D3D11_BUFFER_DESC rejectedDescription = {};
				renderer.compositeVertexBuffer->GetDesc(&rejectedDescription);
				rejectedDescription.Usage = D3D11_USAGE_DEFAULT;
				rejectedDescription.CPUAccessFlags = 0;
				Microsoft::WRL::ComPtr<ID3D11Buffer> rejectedComposite;
				if (!check(SUCCEEDED(testDevice->CreateBuffer(&rejectedDescription, nullptr,
					rejectedComposite.GetAddressOf())), "non-mappable composite fault resource")) return 1;
				const auto writableComposite = renderer.compositeVertexBuffer;
				renderer.compositeVertexBuffer = rejectedComposite;
				check(!renderer.ApplyOperatorLayers(renderer.backBufferRTV.Get(), renderer.layerL1,
					renderer.layerL0, { 0, 0, 64, 64 }) && renderer.m_bufferHead == 0, "composite Map failure preserves ink state");
				renderer.compositeVertexBuffer = writableComposite;
				const std::array<InkPoint, 2> first = { InkPoint{ 8, 12, 3, 0 }, InkPoint{ 52, 12, 3, 1 } };
				const std::array<InkPoint, 2> second = { InkPoint{ 8, 28, 3, 2 }, InkPoint{ 52, 28, 3, 3 } };
				const DirectX::XMFLOAT4 red(1, 0, 0, 1), green(0, 1, 0, 1);
				if (mode == 2)
				{
					const InkPoint seed[] = { first[0], first[1], second[0], second[1] };
					const D3D11_BOX seedRange = { 0, 0, 0, sizeof(seed), 1, 1 };
					testContext->UpdateSubresource(renderer.inkDataBuffer.Get(), 0, &seedRange, seed, 0, 0);
				}
				renderer.SetOperatorTarget(renderer.layerL1);
				check(renderer.DrawStroke(first, red) == 0, "first production stroke");
				if (mode == 2) readBytes(renderer.inkDataBuffer.Get(), sizeof(first), second.data(), sizeof(second), "DEFAULT only updates active bytes");
				checkOffset(0);
				composite();
				readBytes(renderer.inkDataBuffer.Get(), 0, first.data(), sizeof(first), "composite leaves ink bytes intact");
				renderer.SetOperatorTarget(renderer.layerL0);
				check(renderer.DrawStroke(second, green) == 0, "second production stroke");
				const UINT secondOffset = mode == 1 ? 2 : 0;
				checkOffset(secondOffset);
				readBytes(renderer.inkDataBuffer.Get(), secondOffset * sizeof(InkPoint), second.data(), sizeof(second), "second stroke byte range");
				if (mode == 1) readBytes(renderer.inkDataBuffer.Get(), 0, first.data(), sizeof(first), "NO_OVERWRITE retains first batch");
				composite();
				const std::array<ShapePrimitive, 1> shape = { ShapePrimitive{ { 14, 42, 2, 0 }, { 48, 42, 0, 0 } } };
				const UINT shapeOffset = mode == 1 ? static_cast<UINT>(renderer.m_bufferHead) : 0;
				renderer.SetOperatorTarget(renderer.layerL0);
				check(renderer.DrawShapePrimitives(shape, ShapePrimitiveKind::SolidLine, red) == 0, "production analytic shape");
				checkOffset(shapeOffset);
				readBytes(renderer.inkDataBuffer.Get(), shapeOffset * sizeof(InkPoint), shape.data(), sizeof(shape), "shape bytes");
				const std::array<HighlighterPrimitive, 1> highlighter = { HighlighterPrimitive{ { 30, 32 }, { 34, 32 }, { 1.25f, 12 } } };
				check(renderer.DrawHighlighterPrimitives(highlighter, green) == 0, "production highlighter");
				checkOffset(0);
				readBytes(renderer.highlighterPrimitiveBuffer.Get(), 0, highlighter.data(), sizeof(highlighter), "highlighter bytes");
				composite();
				DrawingCursorVisual cursor{};
				cursor.visible = true; cursor.x = 54; cursor.y = 54;
				cursor.appearance.width = cursor.appearance.height = 8;
				cursor.appearance.red = 1;
				renderer.DrawTransientDrawingCursor(cursor);
				const InkPoint cursorData[] = { { 54, 54, 4, 4 }, { 0.5f, 0.5f, 0, 0 } };
				readBytes(renderer.inkDataBuffer.Get(), 0, cursorData, sizeof(cursorData), "cursor uses selected upload path");
				checkOffset(0);
				renderer.SetOperatorTarget(renderer.layerL0);
				check(renderer.DrawStroke(first, red) == 0, "cursor to stroke interleave");
				checkOffset(mode == 1 ? 2 : 0);
				renderer.ClearLaserCoverage(renderer.laserStrokeCoverage);
				renderer.SetLaserCoverageTarget(renderer.laserStrokeCoverage);
				check(renderer.DrawLaserCoverage(first) == 0, "production laser coverage");
				readBytes(renderer.inkDataBuffer.Get(), 0, first.data(), sizeof(first), "laser coverage upload bytes");
				renderer.SetOperatorTarget(renderer.layerL0);
				check(renderer.DrawShapePrimitives(shape, ShapePrimitiveKind::SolidLine, red) == 0, "laser to shape interleave");
				checkOffset(mode == 1 ? 2 : 0);
				check(renderer.ResolveLaserStrokeCoverage(renderer.backBufferRTV.Get(), { 0, 0, 64, 64 }), "laser resolve");
				const std::array<LaserDot, 1> dots = { LaserDot{ 12, 52, 2.5f, 1 } };
				renderer.DrawLaserDots(dots);
				readBytes(renderer.inkDataBuffer.Get(), 0, dots.data(), sizeof(dots), "laser dot upload bytes");
				checkOffset(0);
				renderer.SetOperatorTarget(renderer.layerL0);
				check(renderer.DrawStroke(second, green) == 0, "laser dots to stroke interleave");
				checkOffset(mode == 1 ? 1 : 0);

				// 参数图形和普通笔共用 InkData，三种上传策略都必须保留批次边界和实际像素。
				::draw3::shapes::ShapeObject parameterShape;
				parameterShape.id[0] = 1;
				parameterShape.kind = ::draw3::shapes::Kind::Circle;
				parameterShape.geometry.start = { 44, 18 };
				parameterShape.geometry.radius = 6;
				parameterShape.style.strokeRgb = 0xFF0000;
				parameterShape.style.fill = true;
				parameterShape.style.fillRgb = 0x2040FF;
				check(renderer.DrawShapeObject(parameterShape), "production parametric shape selected upload path");
				const UINT afterShapeOffset = mode == 1 ? static_cast<UINT>(renderer.m_bufferHead) : 0;
				renderer.SetOperatorTarget(renderer.layerL0);
				check(renderer.DrawStroke(second, green) == 0, "parametric shape to stroke interleave");
				checkOffset(afterShapeOffset);
				composite();

				// 同一命令序列的实际像素必须一致，避免只验证 CPU 内容而漏掉 Draw 顺序/偏移。
				testContext->OMSetRenderTargets(0, nullptr, nullptr);
				targetDescription.Usage = D3D11_USAGE_STAGING;
				targetDescription.BindFlags = 0;
				targetDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				Microsoft::WRL::ComPtr<ID3D11Texture2D> pixelReadback;
				if (!check(SUCCEEDED(testDevice->CreateTexture2D(&targetDescription, nullptr,
					pixelReadback.GetAddressOf())), "pixel staging texture")) return 1;
				testContext->CopyResource(pixelReadback.Get(), renderer.backBufferTexture.Get());
				D3D11_MAPPED_SUBRESOURCE pixels = {};
				if (!check(SUCCEEDED(testContext->Map(pixelReadback.Get(), 0, D3D11_MAP_READ, 0, &pixels)), "pixel readback")) return 1;
				std::vector<unsigned char> currentPixels(64 * 64 * 4);
				for (UINT row = 0; row < 64; ++row) std::memcpy(currentPixels.data() + row * 64 * 4,
					static_cast<unsigned char*>(pixels.pData) + row * pixels.RowPitch, 64 * 4);
				testContext->Unmap(pixelReadback.Get(), 0);
				if (mode == 0) referencePixels = currentPixels;
				else check(currentPixels == referencePixels, "dynamic/DEFAULT tool and composite pixels identical");
				check(std::any_of(currentPixels.begin(), currentPixels.end(), [](unsigned char value) { return value != 0; }),
					"rendered pixels are nonempty");

				// 两次更新和 Draw 之间不回读/等待，实际像素验证旧批次没有被后一次更新覆盖。
				renderer.ClearOperatorLayer(renderer.layerL1);
				renderer.ClearOperatorLayer(renderer.layerL0);
				renderer.ClearRTV(renderer.backBufferRTV.Get(), kTransparentLayerClearColor);
				renderer.SetOperatorTarget(renderer.layerL1);
				check(renderer.DrawStroke(first, red) == 0 && renderer.DrawStroke(second, green) == 0,
					"consecutive update and Draw submissions");
				composite();
				testContext->OMSetRenderTargets(0, nullptr, nullptr);
				testContext->CopyResource(pixelReadback.Get(), renderer.backBufferTexture.Get());
				if (!check(SUCCEEDED(testContext->Map(pixelReadback.Get(), 0, D3D11_MAP_READ, 0, &pixels)),
					"consecutive Draw pixel readback")) return 1;
				const auto* redPixel = static_cast<const unsigned char*>(pixels.pData) + 12 * pixels.RowPitch + 12 * 4;
				const auto* greenPixel = static_cast<const unsigned char*>(pixels.pData) + 28 * pixels.RowPitch + 12 * 4;
				check(redPixel[2] > 240 && redPixel[1] < 8 && redPixel[0] < 8 && redPixel[3] > 240 &&
					greenPixel[1] > 240 && greenPixel[2] < 8 && greenPixel[0] < 8 && greenPixel[3] > 240,
					"both consecutive batches rendered with their own data");
				testContext->Unmap(pixelReadback.Get(), 0);

				// R7：操作层清除的真实像素读回（Add BGRA8=0、Retain R16F=1.0 即单位操作）。
				if (mode == 0)
				{
					D3D11_TEXTURE2D_DESC retainReadDescription = targetDescription;
					retainReadDescription.Format = DXGI_FORMAT_R16_FLOAT;
					Microsoft::WRL::ComPtr<ID3D11Texture2D> retainReadback;
					if (!check(SUCCEEDED(testDevice->CreateTexture2D(&retainReadDescription, nullptr,
						retainReadback.GetAddressOf())), "operator retain staging texture")) return 1;
					const auto layerRegion = [&](const OperatorLayerResources& layer,
						RECT region, bool requireIdentity, const char* name)
					{
						bool allIdentity = true;
						testContext->OMSetRenderTargets(0, nullptr, nullptr);
						for (int plane = 0; plane < 2; ++plane)
						{
							auto* source = plane == 0 ? layer.addTexture.Get() : layer.retainTexture.Get();
							auto* staging = plane == 0 ? pixelReadback.Get() : retainReadback.Get();
							testContext->CopyResource(staging, source);
							D3D11_MAPPED_SUBRESOURCE mapped = {};
							if (!check(SUCCEEDED(testContext->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)), "operator region readback"))
								return false;
							for (LONG y = region.top; y < region.bottom; ++y)
								for (LONG x = region.left; x < region.right; ++x)
								{
									const auto* row = static_cast<const unsigned char*>(mapped.pData) + y * mapped.RowPitch;
									if (plane == 0)
									{
										const auto* pixel = row + x * 4;
										if (pixel[0] || pixel[1] || pixel[2] || pixel[3]) allIdentity = false;
									}
									else
									{
										std::uint16_t half = 0;
										std::memcpy(&half, row + x * 2, sizeof(half));
										if (half != 0x3C00) allIdentity = false; // 半精度 1.0
									}
								}
							testContext->Unmap(staging, 0);
						}
						return check(requireIdentity ? allIdentity : !allIdentity, name);
					};
					enum class ClearPath { Full, Partial, Skip };
					const auto clearContent = [&](const OperatorLayerResources& layer, ClearPath path, const char* name)
					{
						const auto before = renderer.OperatorCounters();
						renderer.ClearOperatorLayerContent(layer);
						const auto& after = renderer.OperatorCounters();
						const auto full = after.fullClears - before.fullClears;
						const auto partial = after.partialClears - before.partialClears;
						const bool expected = full == (path == ClearPath::Full) && partial == (path == ClearPath::Partial);
						check(expected && after.skippedClears == before.skippedClears + (path == ClearPath::Skip), name);
					};
					const auto bindRaw = [&](const OperatorLayerResources& layer)
					{
						// 模拟声明范围以外的旧内容，绑定两张 RTV 时绕过写域登记。
						ID3D11RenderTargetView* targets[] = { layer.addRTV.Get(), layer.retainRTV.Get() };
						testContext->OMSetRenderTargets(ARRAYSIZE(targets), targets, nullptr);
					};
					const RECT whole = { 0, 0, 64, 64 };
					const RECT leftBounds = { 0, 4, 6, 10 }, rightBounds = { 59, 48, 64, 54 };
					const std::array<InkPoint, 2> leftInk = { InkPoint{ 2, 7, 1.5f, 0 }, InkPoint{ 4, 7, 1.5f, 1 } };
					const std::array<InkPoint, 2> rightInk = { InkPoint{ 61, 51, 1.25f, 2 }, InkPoint{ 62, 51, 1.25f, 3 } };
					const auto drawBoth = [&](const OperatorLayerResources& layer, const RECT& left, const RECT& right)
					{
						if (&layer == &renderer.layerL1) renderer.MarkStableLayerForeignWrite();
						renderer.SetOperatorTarget(layer, left);
						check(renderer.DrawStroke(leftInk, red) == 0, "declared left write");
						renderer.SetOperatorTarget(layer, right);
						check(renderer.DrawStroke(rightInk, green) == 0, "declared right write");
					};
					auto& l0 = renderer.layerL0;
					auto& l1 = renderer.layerL1;
					const auto originalContext1 = renderer.context1_;
					Microsoft::WRL::ComPtr<ID3D11DeviceContext1> clearViewContext;
					if (!check(options.ClearView && SUCCEEDED(testContext.As(&clearViewContext)), "WARP supports ClearView")) return 1;
					renderer.context1_ = clearViewContext;
					// 两处写域外扩并裁剪后合计 8×10 + 7×10 = 150，小于 256 像素。
					renderer.ClearOperatorLayer(l0);
					drawBoth(l0, leftBounds, rightBounds);
					layerRegion(l0, leftBounds, false, "declared left write reaches L0");
					layerRegion(l0, rightBounds, false, "declared right write reaches L0");
					clearContent(l0, ClearPath::Partial, "L0 two declared writes count one partial clear");
					layerRegion(l0, whole, true, "clear of declared writes restores identity");

					renderer.ClearOperatorLayer(l0);
					bindRaw(l0);
					check(renderer.DrawStroke(rightInk, green) == 0, "L0 raw right write");
					renderer.SetOperatorTarget(l0, leftBounds);
					check(renderer.DrawStroke(leftInk, red) == 0, "L0 declared left write");
					clearContent(l0, ClearPath::Partial, "L0 declared left write counts one partial clear");
					layerRegion(l0, leftBounds, true, "declared bind then partial clear restores the declared rectangle");
					layerRegion(l0, rightBounds, false, "partial clear leaves content outside the declared rectangle");
					clearContent(l0, ClearPath::Skip, "L0 no recorded write counts one skipped clear");
					layerRegion(l0, rightBounds, false, "no recorded write skips the clear");
					// 保留右侧旧内容，验证两种回退都清到声明范围之外。
					renderer.SetOperatorTarget(l0);
					check(renderer.DrawStroke(leftInk, red) == 0, "L0 write without declared bounds");
					clearContent(l0, ClearPath::Full, "L0 unknown write counts one full clear");
					layerRegion(l0, whole, true, "undeclared write falls back to full clear");

					renderer.context1_.Reset();
					bindRaw(l0);
					check(renderer.DrawStroke(rightInk, green) == 0, "L0 raw right write without ClearView");
					renderer.SetOperatorTarget(l0, leftBounds);
					check(renderer.DrawStroke(leftInk, red) == 0, "L0 left write without ClearView");
					clearContent(l0, ClearPath::Full, "missing ClearView counts one full clear");
					layerRegion(l0, whole, true, "missing ClearView falls back to full clear");
					renderer.context1_ = clearViewContext;

					const RECT sentinelPixel = { 28, 27, 29, 28 };
					const std::array<InkPoint, 2> sentinelInk = { InkPoint{ 27, 27, 2, 0 }, InkPoint{ 30, 27, 2, 1 } };
					const auto drawSentinel = [&](const OperatorLayerResources& layer)
					{
						bindRaw(layer);
						check(renderer.DrawStroke(sentinelInk, green) == 0, "raw sentinel write");
						layerRegion(layer, sentinelPixel, false, "raw sentinel pixel reaches operator layer");
					};
					// 哨兵位于两个登记矩形之间，合用一个外包矩形会擦掉它。
					for (auto* layer : { &l0, &l1 })
					{
						renderer.ClearOperatorLayer(*layer);
						drawBoth(*layer, leftBounds, rightBounds);
						layerRegion(*layer, leftBounds, false, "multi-rect left write reaches operator layer");
						layerRegion(*layer, rightBounds, false, "multi-rect right write reaches operator layer");
						drawSentinel(*layer);
						clearContent(*layer, ClearPath::Partial, layer == &l0 ?
							"L0 multi-rect clear counts once" : "L1 multi-rect clear counts once");
						layerRegion(*layer, leftBounds, true, "multi-rect clear restores left write");
						layerRegion(*layer, rightBounds, true, "multi-rect clear restores right write");
						layerRegion(*layer, sentinelPixel, false, "multi-rect clear preserves the sentinel between rectangles");
					}

					// 64×64 的 1/16 为 256。每边外扩 2，再裁剪到画布：
					// 左上贴边、右侧贴边，两矩形面积和分别为 8×18 + 7×16 = 256、8×19 + 7×15 = 257。
					for (const bool aboveThreshold : { false, true })
					{
						renderer.ClearOperatorLayer(l0);
						drawBoth(l0, { 0, 0, 6, aboveThreshold ? 17 : 16 },
							{ 59, aboveThreshold ? 43 : 42, 64, 54 });
						layerRegion(l0, leftBounds, false, "threshold left write reaches L0");
						layerRegion(l0, rightBounds, false, "threshold right write reaches L0");
						drawSentinel(l0);
						clearContent(l0, aboveThreshold ? ClearPath::Full : ClearPath::Partial, aboveThreshold ?
							"257 pixels count one full clear" : "256 pixels count one partial clear");
						layerRegion(l0, leftBounds, true, "threshold clear restores left write");
						layerRegion(l0, rightBounds, true, "threshold clear restores right write");
						layerRegion(l0, sentinelPixel, aboveThreshold, aboveThreshold ?
							"257 pixels clear the sentinel" : "256 pixels preserve the sentinel");
					}

					const auto drawDot = [&](RECT bounds)
					{
						const std::array<InkPoint, 1> dot = { InkPoint{
							(bounds.left + bounds.right) * 0.5f, (bounds.top + bounds.bottom) * 0.5f, 0.5f, 0 } };
						renderer.SetOperatorTarget(l0, bounds);
						check(renderer.DrawStrokeOrDot(dot, red) == 0, "declared tiny write");
						layerRegion(l0, bounds, false, "declared tiny write reaches L0");
					};
					// 九个 1×1 写入间隔 8px。第九个并入横向或纵向最近的一个，外扩后
					// 面积为 7×5×5 + 13×5 = 240，小于 64×64/16 = 256。
					for (LONG i = 0; i < 9; ++i)
					{
						const LONG x = 8 + (i % 3) * 8, y = 12 + (i / 3) * 8;
						drawDot({ x, y, x + 1, y + 1 });
					}
					bindRaw(l0);
					check(renderer.DrawStroke(rightInk, green) == 0, "raw capacity sentinel write");
					layerRegion(l0, rightBounds, false, "capacity sentinel reaches L0");
					clearContent(l0, ClearPath::Partial, "nine disjoint writes count one partial clear");
					layerRegion(l0, { 0, 0, 32, 64 }, true, "capacity merge clears all nine writes");
					layerRegion(l0, rightBounds, false, "capacity clear preserves the sentinel");
					renderer.ClearOperatorLayer(l0);

					// A、B 相隔 5px，先独立登记；C 桥接后外扩面积为 21×12 = 252。
					// 若只吞并一侧，剩余两个外扩矩形的面积和为 (15+10)×12 = 300，会退回整屏清。
					const RECT a = { 10, 8, 16, 16 }, b = { 21, 8, 27, 16 }, c = { 16, 8, 21, 16 };
					drawDot(a);
					drawDot(b);
					drawDot(c);
					drawSentinel(l0);
					clearContent(l0, ClearPath::Partial, "bridge merges both neighbors and counts one partial clear");
					layerRegion(l0, a, true, "bridge clear restores A");
					layerRegion(l0, b, true, "bridge clear restores B");
					layerRegion(l0, c, true, "bridge clear restores C");
					layerRegion(l0, sentinelPixel, false, "bridge clear preserves the sentinel");
					renderer.ClearOperatorLayer(l0);

					// 四次相邻追加分别外扩会达 324px，合并后只清 216px。
					check(renderer.OperatorLayerEmpty(l0), "L0 empty before adjacent writes");
					for (LONG x = 14; x < 34; x += 5) drawDot({ x, 31, x + 5, 36 });
					check(!renderer.OperatorLayerEmpty(l0), "L0 nonempty after adjacent writes");
					clearContent(l0, ClearPath::Partial, "adjacent writes merge into one partial clear");
					layerRegion(l0, whole, true, "adjacent write clear restores identity");
					check(renderer.OperatorLayerEmpty(l0), "L0 empty after adjacent write clear");

					// 饱和坐标外扩后仍要覆盖实际写入，不能因溢出丢弃矩形。
					constexpr LONG minCoord = (std::numeric_limits<LONG>::min)();
					constexpr LONG maxCoord = (std::numeric_limits<LONG>::max)();
					renderer.SetOperatorTarget(l0, { minCoord, minCoord, maxCoord, maxCoord });
					check(renderer.DrawStroke(leftInk, red) == 0, "saturated bounds write");
					layerRegion(l0, leftBounds, false, "saturated bounds write reaches L0");
					clearContent(l0, ClearPath::Full, "saturated bounds count one full clear");
					layerRegion(l0, whole, true, "saturated bounds clear restores identity");

					renderer.SetOperatorTarget(l0, { rightBounds.left, rightBounds.top, maxCoord, rightBounds.bottom });
					check(renderer.DrawStroke(rightInk, green) == 0, "saturated right edge write");
					layerRegion(l0, rightBounds, false, "saturated right edge write reaches L0");
					clearContent(l0, ClearPath::Partial, "saturated right edge counts one partial clear");
					layerRegion(l0, whole, true, "saturated right edge clear restores identity");

					renderer.SetOperatorTarget(l0, { -16, 8, -8, 16 });
					renderer.SetOperatorTarget(l0, { 70, 48, 78, 54 });
					check(!renderer.OperatorLayerEmpty(l0), "off-canvas declarations are tracked before clear");
					clearContent(l0, ClearPath::Skip, "off-canvas declarations count one skipped clear");
					check(renderer.OperatorLayerEmpty(l0), "off-canvas declarations reset to empty after clear");

					// L1 的哨兵仍在，未声明的绑定必须连同哨兵整层清掉。
					renderer.SetOperatorTarget(l1);
					check(renderer.DrawStroke(leftInk, red) == 0, "L1 write without declared bounds");
					layerRegion(l1, leftBounds, false, "unbounded write reaches L1");
					clearContent(l1, ClearPath::Full, "L1 unknown write counts one full clear");
					layerRegion(l1, whole, true, "L1 unknown write clears all pixels including sentinel");

					check(renderer.OperatorLayerEmpty(l0), "L0 empty before marking untracked");
					renderer.MarkOperatorLayerUntracked(l0);
					check(!renderer.OperatorLayerEmpty(l0), "marking empty L0 untracked makes it nonempty");
					drawSentinel(l0);
					clearContent(l0, ClearPath::Full, "marking L0 untracked counts one full clear");
					layerRegion(l0, whole, true, "marked untracked layer clears the raw sentinel");
					check(renderer.OperatorLayerEmpty(l0), "L0 empty after untracked clear");

					// 显式加载生产版，避免 Win7 或诊断环境选中保护版时，两次合成用了同一字节码。
					const auto originalShader = renderer.pixelShader;
					const auto productionBlob = LoadShaderFromResource(IDR_DRAW3_INK_PIXEL_SHADER);
					Microsoft::WRL::ComPtr<ID3D11PixelShader> productionShader, guardedShader;
					if (!check(productionBlob.data && SUCCEEDED(testDevice->CreatePixelShader(productionBlob.data,
						productionBlob.size, nullptr, productionShader.GetAddressOf())),
						"production operator resolve pixel shader")) return 1;
					if (!check(SUCCEEDED(testDevice->CreatePixelShader(Draw3Win7GuardedShader::Bytecode,
						sizeof(Draw3Win7GuardedShader::Bytecode), nullptr, guardedShader.GetAddressOf())),
						"guarded operator resolve pixel shader")) return 1;
					const auto readBackBuffer = [&](std::vector<unsigned char>& result)
					{
						testContext->OMSetRenderTargets(0, nullptr, nullptr);
						testContext->CopyResource(pixelReadback.Get(), renderer.backBufferTexture.Get());
						D3D11_MAPPED_SUBRESOURCE mapped = {};
						if (!check(SUCCEEDED(testContext->Map(pixelReadback.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
							"operator resolve backbuffer readback")) return false;
						for (UINT row = 0; row < 64; ++row)
							std::memcpy(result.data() + row * 64 * 4,
								static_cast<const unsigned char*>(mapped.pData) + row * mapped.RowPitch, 64 * 4);
						testContext->Unmap(pixelReadback.Get(), 0);
						return true;
					};
					const DirectX::XMFLOAT4 background(0.2f, 0.4f, 0.6f, 1.0f), penColor(0.8f, 0.2f, 0.4f, 0.65f);
					const RECT resolveRect = { 8, 16, 48, 32 };
					const std::array<InkPoint, 2> resolveInk = { InkPoint{ 14.25f, 23.25f, 3.25f, 0 },
						InkPoint{ 40.75f, 23.25f, 3.25f, 1 } };
					std::vector<unsigned char> backgroundPixels(64 * 64 * 4), dualPixels(64 * 64 * 4),
						singlePixels(64 * 64 * 4), guardedPixels(64 * 64 * 4);
					auto* dst = renderer.backBufferRTV.Get();
					renderer.ClearRTV(dst, background);
					if (!readBackBuffer(backgroundPixels)) return 1;
					for (auto* layer : { &l1, &l0 })
						for (const auto kind : { InkOperatorKind::Draw, InkOperatorKind::Erase })
						{
							renderer.pixelShader = productionShader;
							renderer.ClearOperatorLayer(l1);
							renderer.ClearOperatorLayer(l0);
							if (layer == &l1) renderer.MarkStableLayerForeignWrite();
							renderer.SetOperatorTarget(*layer, resolveRect);
							check(renderer.DrawStroke(resolveInk, kind == InkOperatorKind::Draw ?
								penColor : kTransparentLayerClearColor, StrokeShape::RoundCapsule, kind) == 0,
								"operator resolve source stroke");
							layerRegion(layer == &l1 ? l0 : l1, whole, true, "other resolve layer is identity");
							const auto before = renderer.OperatorCounters();

							renderer.ClearRTV(dst, background);
							check(renderer.ApplyOperatorLayers(dst, l1, l0, resolveRect), "dual operator resolve submission");
							if (!readBackBuffer(dualPixels)) return 1;
							check(renderer.OperatorCounters().dualResolves == before.dualResolves + 1 &&
								renderer.OperatorCounters().singleResolves == before.singleResolves, "dual resolve counts once");
							// 检查笔画核心，避免两条合成路径都没有改变底色时出现假阳性。
							constexpr size_t center = (23 * 64 + 24) * 4;
							const unsigned char transparentPixel[4] = {};
							check(kind == InkOperatorKind::Erase ?
								std::memcmp(dualPixels.data() + center, transparentPixel, 4) == 0 :
								std::memcmp(dualPixels.data() + center, backgroundPixels.data() + center, 4) != 0,
								"dual resolve draws ink or fully erases the opaque background at stroke center");

							renderer.ClearRTV(dst, background);
							check(renderer.ApplyOperatorLayer(dst, *layer, resolveRect), "single operator resolve submission");
							if (!readBackBuffer(singlePixels)) return 1;
							check(singlePixels == dualPixels, "single and dual operator resolve pixels are identical");
							check(renderer.OperatorCounters().singleResolves == before.singleResolves + 1 &&
								renderer.OperatorCounters().dualResolves == before.dualResolves + 1, "single resolve counts once");

							// 保留同一份操作层纹理，只替换单层合成使用的像素着色器。
							renderer.ClearRTV(dst, background);
							renderer.pixelShader = guardedShader;
							const bool guardedResolved = renderer.ApplyOperatorLayer(dst, *layer, resolveRect);
							renderer.pixelShader = originalShader;
							testContext->PSSetShader(originalShader.Get(), nullptr, 0);
							check(guardedResolved, "guarded single operator resolve submission");
							if (!readBackBuffer(guardedPixels)) return 1;
							check(guardedPixels == singlePixels, "guarded and production single operator resolve pixels are identical");
							check(renderer.OperatorCounters().singleResolves == before.singleResolves + 2 &&
								renderer.OperatorCounters().dualResolves == before.dualResolves + 1, "guarded single resolve counts once");
						}
					renderer.ClearOperatorLayer(l0);
					renderer.ClearOperatorLayer(l1);
					renderer.context1_ = originalContext1;
				}

				// 光标尺寸必须由真实生产 PS 像素读回证明，不能用 appearance 的理论盒代替。
				if (mode == 0)
				{
					const auto originalShader = renderer.pixelShader;
					Microsoft::WRL::ComPtr<ID3D11PixelShader> guardedShader;
					if (!check(SUCCEEDED(testDevice->CreatePixelShader(Draw3Win7GuardedShader::Bytecode,
						sizeof(Draw3Win7GuardedShader::Bytecode), nullptr, guardedShader.GetAddressOf())), "guarded cursor pixel shader")) return 1;
					DrawingCursorSample pen{ .x = 8.0f, .y = 8.0f, .valid = true };
					pen.source = { SpeedEraser::SourceKind::IntegratedPen, SpeedEraser::SourceRecognition::RtsCapabilities };
					CursorSettings settings; settings.screenPen = CursorStyle::Arrow;
					DrawingCursorAppearance base; base.width = base.height = 5;
					for (int route = 0; route < 2; ++route)
						for (const float dpi : { 96.0f, 144.0f, 192.0f, 264.0f })
						{
							renderer.pixelShader = route ? guardedShader : originalShader;
							const auto arrow = ResolvePrimaryDrawingCursorVisual(pen, {}, DrawingCursorPointerAuthority::Pen,
								base, base, false, false, false, true, &settings, true, dpi / 96.0f);
							renderer.ClearRTV(renderer.backBufferRTV.Get(), kTransparentLayerClearColor);
							renderer.DrawTransientDrawingCursor(arrow);
							testContext->OMSetRenderTargets(0, nullptr, nullptr);
							testContext->CopyResource(pixelReadback.Get(), renderer.backBufferTexture.Get());
							if (!check(SUCCEEDED(testContext->Map(pixelReadback.Get(), 0, D3D11_MAP_READ, 0, &pixels)), "arrow pixel readback")) return 1;
							int left = 64, top = 64, right = 0, bottom = 0, count = 0;
							for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x)
							{
								const auto* pixel = static_cast<const unsigned char*>(pixels.pData) + y * pixels.RowPitch + x * 4;
								if (!pixel[3]) continue;
								left = (std::min)(left, x); top = (std::min)(top, y);
								right = (std::max)(right, x + 1); bottom = (std::max)(bottom, y + 1); ++count;
							}
							if (dpi == 192.0f)
							{
								// 取竖直左边的中段：x=8 为黑边，紧邻 x=9 应已是白色填充；旧 2px 边会使后者仍黑。
								const auto* row = static_cast<const unsigned char*>(pixels.pData) + 24 * pixels.RowPitch;
								const auto* edge = row + 8 * 4;
								const auto* fill = row + 9 * 4;
								std::fprintf(stderr, "[Draw3CursorEdge] shader=%s dpi=192 edge_bgra=(%u,%u,%u,%u) fill_bgra=(%u,%u,%u,%u) no_hwnd=1\n",
									route ? "guarded" : "production", edge[0], edge[1], edge[2], edge[3], fill[0], fill[1], fill[2], fill[3]);
								check(edge[0] <= 8 && edge[1] <= 8 && edge[2] <= 8 && edge[3] >= 248 &&
									fill[0] >= 247 && fill[1] >= 247 && fill[2] >= 247 && fill[3] >= 248,
									"192 DPI arrow has one black edge pixel followed immediately by opaque white fill");
							}
							testContext->Unmap(pixelReadback.Get(), 0);
							std::fprintf(stderr, "[Draw3CursorPixels] shader=%s dpi=%.0f appearance=(%.2f,%.2f) hotspot=(%.1f,%.1f) bbox=(%d,%d,%d,%d) visible=(%d,%d) count=%d no_hwnd=1\n",
								route ? "guarded" : "production", dpi, arrow.appearance.width, arrow.appearance.height,
								arrow.x, arrow.y, left, top, right, bottom, right - left, bottom - top, count);
							check(count > 0 && left >= 7 && left <= 9 && top >= 7 && top <= 9 &&
								right - left >= arrow.appearance.width * 0.85f && right - left <= arrow.appearance.width + 2 &&
								bottom - top >= arrow.appearance.height * 0.85f && bottom - top <= arrow.appearance.height + 2,
								"production and guarded cursor pixels follow DPI once and retain the tip hotspot");
							if (dpi == 192.0f)
								check(left == 8 && top == 8 && right - left == 25 && bottom - top == 39,
									"192 DPI arrow pixels match the measured native 25 by 39 visible bounds");
						}
					renderer.pixelShader = originalShader;
				}

				// 越界拆批仍共享笔端点；最终批次与回卷偏移由真实上传读回验证。
				std::vector<InkPoint> longStroke(InkRenderer::kMaxBufferCapacity + 2, { -100, -100, 0, 0 });
				longStroke[InkRenderer::kMaxBufferCapacity - 1] = { 20, 20, 2, 1 };
				longStroke[InkRenderer::kMaxBufferCapacity] = { 28, 20, 2, 2 };
				longStroke.back() = { 36, 20, 2, 3 };
				renderer.SetOperatorTarget(renderer.layerL0);
				check(renderer.DrawStroke(longStroke, red) == 0, "multi-batch stroke and wrap");
				check(renderer.m_bufferHead == (mode == 2 ? 0u : 3u), "final stroke batch head");
				checkOffset(0);
				readBytes(renderer.inkDataBuffer.Get(), 0, longStroke.data() + InkRenderer::kMaxBufferCapacity - 1,
					3 * sizeof(InkPoint), "multi-batch shared endpoint");
				std::vector<ShapePrimitive> shapes(InkRenderer::kMaxBufferCapacity / 2 + 1,
					ShapePrimitive{ { -100, -100, 0, 0 }, { -90, -90, 0, 0 } });
				shapes.back() = shape.front();
				check(renderer.DrawShapePrimitives(shapes, ShapePrimitiveKind::SolidLine, red) == 0, "multi-batch shapes");
				checkOffset(0);
				readBytes(renderer.inkDataBuffer.Get(), 0, &shapes.back(), sizeof(ShapePrimitive), "final shape batch");
				std::vector<HighlighterPrimitive> highlights(InkRenderer::kMaxBufferCapacity + 1,
					HighlighterPrimitive{ { -100, -100 }, { -90, -90 }, { 1.25f, 25 } });
				highlights.back() = highlighter.front();
				check(renderer.DrawHighlighterPrimitives(highlights, green) == 0, "multi-batch highlighter");
				readBytes(renderer.highlighterPrimitiveBuffer.Get(), 0, &highlights.back(), sizeof(HighlighterPrimitive), "final highlighter batch");
				if (mode == 0)
				{
					// 走真实绘制入口检查 D3D 纹理身份；持有旧 ComPtr，避免地址复用造成假阳性。
					DrawingCursorVisual eraser;
					eraser.visible = true; eraser.x = eraser.y = 32;
					auto& appearance = eraser.appearance;
					appearance.shape = DrawingCursorShape::EraserSprite;
					appearance.width = appearance.height = appearance.eraserBaseShortSide = 32;
					appearance.eraserDpiScale = 1;
					renderer.InvalidateEraserCursorCache();
					renderer.DrawTransientDrawingCursor(eraser);
					if (!check(renderer.eraserCursorSprites_.size() == 1 &&
						renderer.eraserCursorSprites_.front().texture && renderer.eraserCursorSprites_.front().srv,
						"eraser drawing creates a real cached texture and SRV")) return 1;
					const auto firstTexture = renderer.eraserCursorSprites_.front().texture;
					D3D11_TEXTURE2D_DESC spriteDescription = {};
					firstTexture->GetDesc(&spriteDescription);
					check(spriteDescription.Width == 36 && spriteDescription.Height == 36 &&
						spriteDescription.MipLevels == 6 && spriteDescription.Format == DXGI_FORMAT_B8G8R8A8_UNORM &&
						spriteDescription.Usage == D3D11_USAGE_IMMUTABLE && spriteDescription.MiscFlags == 0 &&
						spriteDescription.BindFlags == D3D11_BIND_SHADER_RESOURCE,
						"eraser uploads immutable BGRA with complete CPU mip chain and no shared flags");
					renderer.DrawTransientDrawingCursor(eraser);
					eraser.x = 40; eraser.y = 28;
					renderer.DrawTransientDrawingCursor(eraser);
					appearance.width = appearance.height = 40;
					renderer.DrawTransientDrawingCursor(eraser);
					check(renderer.eraserCursorSprites_.size() == 1 &&
						renderer.eraserCursorSprites_.front().texture.Get() == firstTexture.Get(),
						"same appearance, movement and exact 1.25 scale reuse the same GPU texture");
					appearance.width = appearance.height = 40.01f;
					renderer.DrawTransientDrawingCursor(eraser);
					const auto enlargedTexture = renderer.eraserCursorSprites_.front().texture;
					if (!check(enlargedTexture && enlargedTexture.Get() != firstTexture.Get(),
						"crossing 1.25 scale replaces the real GPU texture")) return 1;
					enlargedTexture->GetDesc(&spriteDescription);
					check(spriteDescription.Width == 68 && spriteDescription.Height == 68 && spriteDescription.MipLevels == 7,
						"scale upgrade uploads the next power-of-two bucket and complete mip chain");
					appearance.width = appearance.height = 32;
					renderer.DrawTransientDrawingCursor(eraser);
					check(renderer.eraserCursorSprites_.front().texture.Get() == enlargedTexture.Get(),
						"shrinking a dynamic eraser retains its upgraded GPU texture");
					appearance.eraserCursor.shape = EraserShape::Rectangle;
					appearance.height = 48;
					renderer.DrawTransientDrawingCursor(eraser);
					check(renderer.eraserCursorSprites_.size() == 2 &&
						renderer.eraserCursorSprites_.front().texture.Get() == enlargedTexture.Get() &&
						renderer.eraserCursorSprites_.back().texture &&
						renderer.eraserCursorSprites_.back().texture.Get() != enlargedTexture.Get(),
						"normal and independent palm appearances hold distinct simultaneous GPU textures");
					appearance.eraserCursor.shape = EraserShape::Circle;
					appearance.height = 32;
					appearance.eraserBaseShortSide = 64;
					renderer.DrawTransientDrawingCursor(eraser);
					const auto rebasedTexture = renderer.eraserCursorSprites_.front().texture;
					check(rebasedTexture && rebasedTexture.Get() != enlargedTexture.Get(),
						"base size change recreates GPU texture even within the same bucket");
					appearance.eraserDpiScale = 2;
					renderer.DrawTransientDrawingCursor(eraser);
					const auto dpiTexture = renderer.eraserCursorSprites_.front().texture;
					check(dpiTexture && dpiTexture.Get() != rebasedTexture.Get(),
						"DPI change recreates the GPU texture");
					ID3D11ShaderResourceView* boundSprite[] = { renderer.eraserCursorSprites_.front().srv.Get() };
					testContext->PSSetShaderResources(15, 1, boundSprite);
					renderer.InvalidateEraserCursorCache();
					Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> staleSprite;
					testContext->PSGetShaderResources(15, 1, staleSprite.GetAddressOf());
					check(renderer.eraserCursorSprites_.empty() && !staleSprite,
						"explicit eraser invalidation clears all appearances and unbinds SRV");
					renderer.DrawTransientDrawingCursor(eraser);
					if (!check(renderer.eraserCursorSprites_.size() == 1 &&
						renderer.eraserCursorSprites_.front().texture &&
						renderer.eraserCursorSprites_.front().texture.Get() != dpiTexture.Get(),
						"first drawing after invalidation rebuilds the GPU cache")) return 1;
					const auto beforeRecoveryTexture = renderer.eraserCursorSprites_.front().texture;
					renderer.ReleaseResources();
					check(renderer.eraserCursorSprites_.empty() && !renderer.eraserCursorSampler_,
						"device release retires every eraser sprite and sampler");
					Microsoft::WRL::ComPtr<ID3D11Device> recoveredDevice;
					Microsoft::WRL::ComPtr<ID3D11DeviceContext> recoveredContext;
					if (!check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
						D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
						recoveredDevice.GetAddressOf(), &actualLevel, recoveredContext.GetAddressOf())),
						"eraser recovery creates an independent WARP device epoch")) return 1;
					InkRenderer recovered;
					recovered.device = recoveredDevice; recovered.context = recoveredContext;
					if (!check(recovered.CreateFixedResources(D3D_DRIVER_TYPE_WARP), "recovered eraser fixed resources")) return 1;
					const auto* restoredSprite = recovered.EnsureEraserCursorSprite(appearance);
					if (!check(restoredSprite && restoredSprite->texture.Get() != beforeRecoveryTexture.Get(),
						"recovered eraser prepares a new texture instead of sharing the old device resource")) return 1;
					Microsoft::WRL::ComPtr<ID3D11Device> textureDevice;
					restoredSprite->texture->GetDevice(textureDevice.GetAddressOf());
					check(textureDevice.Get() == recoveredDevice.Get(), "recovered eraser texture belongs to the new device");
					recovered.ReleaseResources();
				}
				testContext->ClearState();
				renderer.ReleaseResources();
				std::fprintf(stderr, "[Draw3Upload] mode=%s completed\n", mode == 2 ? "DEFAULT" : mode == 1 ? "dynamic_append" : "dynamic_discard");
			}
			{
				// 单独使用一个渲染器，并放在上传用例之后，避免修改其缓冲游标、计数和绑定。
				InkRenderer renderer;
				renderer.device = testDevice;
				renderer.context = testContext;
				constexpr LONG kSize = 64;
				constexpr UINT kPitch = kSize * sizeof(uint32_t);
				if (!check(renderer.CreateFixedResources(D3D_DRIVER_TYPE_WARP) &&
					renderer.CreateOperatorLayerResources(kSize, kSize, renderer.layerL1) &&
					renderer.CreateOperatorLayerResources(kSize, kSize, renderer.layerL0),
					"multi-rect resolve renderer resources")) return 1;
				renderer.SetScreenSize(kSize, kSize);

				std::vector<uint32_t> below(kSize * kSize), boundingPixels(kSize * kSize), candidatePixels(kSize * kSize);
				for (LONG y = 0; y < kSize; ++y)
					for (LONG x = 0; x < kSize; ++x)
						below[y * kSize + x] = 0xB0000020u | ((32u + x) << 16) | ((40u + y) << 8);

				D3D11_TEXTURE2D_DESC description = {};
				description.Width = description.Height = kSize;
				description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
				description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
				description.Usage = D3D11_USAGE_DEFAULT;
				description.BindFlags = D3D11_BIND_RENDER_TARGET;
				struct ResolveTarget
				{
					Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
					Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
				};
				ResolveTarget reference, candidate;
				for (auto* target : { &reference, &candidate })
				{
					if (!check(SUCCEEDED(testDevice->CreateTexture2D(&description, nullptr,
						target->texture.GetAddressOf())) && SUCCEEDED(testDevice->CreateRenderTargetView(
						target->texture.Get(), nullptr, target->rtv.GetAddressOf())),
						"multi-rect resolve destination")) return 1;
				}
				description.Usage = D3D11_USAGE_STAGING;
				description.BindFlags = 0;
				description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
				if (!check(SUCCEEDED(testDevice->CreateTexture2D(&description, nullptr, staging.GetAddressOf())),
					"multi-rect resolve staging texture")) return 1;
				const auto readPixels = [&](const ResolveTarget& target, std::vector<uint32_t>& pixels) {
					testContext->OMSetRenderTargets(0, nullptr, nullptr);
					testContext->CopyResource(staging.Get(), target.texture.Get());
					D3D11_MAPPED_SUBRESOURCE mapped = {};
					if (!check(SUCCEEDED(testContext->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
						"multi-rect resolve readback")) return false;
					for (LONG y = 0; y < kSize; ++y)
						std::memcpy(pixels.data() + y * kSize,
							static_cast<const unsigned char*>(mapped.pData) + y * mapped.RowPitch, kPitch);
					testContext->Unmap(staging.Get(), 0);
					return true;
				};
				const auto drawLayerInk = [&](const OperatorLayerResources& layer, float drawY, float eraseY,
					const DirectX::XMFLOAT4& color) {
					const InkPoint ink[] = { { 1.25f, drawY, 2.5f, 0 }, { 63.25f, drawY, 2.5f, 1 } };
					const InkPoint eraser[] = { { 1.25f, eraseY, 3.5f, 0 }, { 63.25f, eraseY, 3.5f, 1 } };
					renderer.ClearOperatorLayer(layer);
					if (&layer == &renderer.layerL1) renderer.MarkStableLayerForeignWrite();
					renderer.SetOperatorTarget(layer, { 0, 0, kSize, kSize });
					return renderer.DrawStroke(ink, color) == 0 &&
						renderer.DrawStroke(eraser, kTransparentLayerClearColor, StrokeShape::RoundCapsule,
							InkOperatorKind::Erase) == 0;
				};
				if (!check(drawLayerInk(renderer.layerL1, 12.25f, 18.25f, { 0.8f, 0.2f, 0.4f, 0.65f }) &&
					drawLayerInk(renderer.layerL0, 13.25f, 20.25f, { 0.15f, 0.7f, 0.5f, 0.55f }),
					"multi-rect resolve source strokes")) return 1;

				constexpr RECT bounds = { 4, 8, kSize, 24 };
				// 前两项不产生像素。其余三项覆盖共边、远隔和部分超出视口的情况。
				constexpr RECT rects[] = {
					{ 68, 8, 72, 24 }, { 32, 8, 32, 24 },
					{ 4, 8, 15, 24 }, { 15, 8, 22, 24 }, { 52, 8, 71, 24 }
				};
				const std::span<const RECT> regions(rects);
				struct ResolveMode
				{
					bool single;
					OperatorLayerMergeMode merge;
					const char* name;
				};
				constexpr ResolveMode modes[] = {
					{ false, OperatorLayerMergeMode::CoverageUnion, "coverage union" },
					{ false, OperatorLayerMergeMode::Ordered, "ordered" },
					{ true, OperatorLayerMergeMode::CoverageUnion, "single" },
				};
				for (const auto& mode : modes)
				{
					const int failuresBefore = failures;
					const auto resolve = [&](ID3D11RenderTargetView* target, const auto& region) {
						return mode.single ? renderer.ApplyOperatorLayer(target, renderer.layerL0, region) :
							renderer.ApplyOperatorLayers(target, renderer.layerL1, renderer.layerL0, region, mode.merge);
					};
					testContext->UpdateSubresource(reference.texture.Get(), 0, nullptr, below.data(), kPitch, 0);
					testContext->UpdateSubresource(candidate.texture.Get(), 0, nullptr, below.data(), kPitch, 0);
					if (!check(resolve(reference.rtv.Get(), bounds), "bounding rect resolve submission")) return 1;
					const auto before = renderer.OperatorCounters();
					check(resolve(candidate.rtv.Get(), regions.first(2)) &&
						renderer.OperatorCounters().dualResolves == before.dualResolves &&
						renderer.OperatorCounters().singleResolves == before.singleResolves,
						"no-op resolve for clipped or empty rectangles");
					if (!check(resolve(candidate.rtv.Get(), regions), "multi-rect resolve submission")) return 1;
					const auto after = renderer.OperatorCounters();
					check(after.dualResolves == before.dualResolves + (mode.single ? 0 : 1) &&
						after.singleResolves == before.singleResolves + (mode.single ? 1 : 0),
						"multi-rect resolve counts once");
					if (!readPixels(reference, boundingPixels) || !readPixels(candidate, candidatePixels)) return 1;

					bool insideMatches = true, outsideMatches = true;
					for (LONG y = 0; y < kSize; ++y)
						for (LONG x = 0; x < kSize; ++x)
						{
							const bool inside = std::any_of(regions.begin(), regions.end(), [&](const RECT& rect) {
								return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
							});
							const size_t pixel = y * kSize + x;
							if (inside) insideMatches &= candidatePixels[pixel] == boundingPixels[pixel];
							else outsideMatches &= candidatePixels[pixel] == below[pixel];
						}
					check(insideMatches, "multi-rect resolve pixels match the bounding rect");
					check(outsideMatches, "multi-rect resolve preserves all outside pixels");
					// 三个矩形以及外接矩形的空隙内都要有墨迹，否则漏画或整块误画可能不影响像素。
					for (LONG x : { 8L, 18L, 58L, 32L })
					{
						const size_t pixel = 12 * kSize + x;
						check(boundingPixels[pixel] != below[pixel], "bounding rect resolve changes each probe pixel");
					}
					if (failures != failuresBefore)
						std::fprintf(stderr, "[Draw3Upload] multi-rect mode=%s\n", mode.name);
				}
				testContext->ClearState();
			}
			if (!canAppend) std::fputs("[Draw3Upload] NO_OVERWRITE unavailable on WARP; append case skipped\n", stderr);
			std::fprintf(stderr, "[Draw3Upload] %s failures=%d no_hwnd=1 no_swapchain=1\n", failures ? "FAIL" : "PASS", failures);
			return failures ? 1 : 0;
		}
		catch (...) { std::fputs("[Draw3Upload] FAIL unexpected exception\n", stderr); return 1; }
	}
}
