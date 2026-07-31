module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../resource.h"
#include "../laserParticleResource.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <d3d11.h>
#include <DirectXMath.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <wrl/client.h>

module draw3.renderer;

namespace draw3
{
	namespace
	{
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

		// 该结构必须满足 D3D11 常量缓冲区的 16 字节对齐要求。
		struct GlobalShaderConstants
		{
			float width;
			float height;
			float shapeType;
			uint32_t bufferOffset;
			DirectX::XMFLOAT4 color;
			uint32_t operatorKind;
			float padding[3];
		};

		static_assert(sizeof(GlobalShaderConstants) == 48);
		static_assert(sizeof(GlobalShaderConstants) % 16 == 0);

		void ReportLaserParticleUnavailableOnce() noexcept
		{
			static bool reported = false;
			if (reported) return;
			reported = true;
			::OutputDebugStringW(
				L"[Draw3] D3D11 Compute Shader 激光粒子不可用，已仅保留激光主体。\n");
		}
	}

	int InkRenderer::DrawStrokeOrDot(const std::vector<InkPoint>& points, DirectX::XMFLOAT4 color,
		StrokeShape shape, InkOperatorKind operatorKind)
	{
		if (points.empty()) return 0;
		if (points.size() >= 2) return DrawStroke(points, color, shape, operatorKind);

		std::vector<InkPoint> dotPoints;
		dotPoints.reserve(2);
		// 圆角工具使用极短胶囊段生成点击圆点；平头笔由独立 primitive 路径处理。
		dotPoints.push_back(points.front());
		InkPoint dotEnd = points.front();
		dotEnd.x += std::max(0.25f, dotEnd.r * 0.05f);
		dotPoints.push_back(dotEnd);
		return DrawStroke(dotPoints, color, shape, operatorKind);
	}

	int InkRenderer::DrawStroke(const std::vector<InkPoint>& points, DirectX::XMFLOAT4 color,
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
			if (m_bufferHead + batchCount > kMaxBufferCapacity)
			{
				mapType = D3D11_MAP_WRITE_DISCARD; // 环形缓冲区写满后丢弃旧内容从头写。
				m_bufferHead = 0;
			}

			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (SUCCEEDED(context->Map(inkDataBuffer.Get(), 0, mapType, 0, &mapped)))
			{
				auto* destination = static_cast<InkPoint*>(mapped.pData);
				std::memcpy(destination + m_bufferHead, points.data() + startIndex, batchCount * sizeof(InkPoint)); // 把本批点写入结构化缓冲区。
				context->Unmap(inkDataBuffer.Get(), 0);
			}

			if (SUCCEEDED(context->Map(globalCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			{
				auto* constants = static_cast<GlobalShaderConstants*>(mapped.pData);
				constants->width = viewportWidth;
				constants->height = viewportHeight;
				constants->color = color;
				constants->shapeType = static_cast<float>(static_cast<uint32_t>(shape));
				constants->bufferOffset = static_cast<uint32_t>(m_bufferHead); // 着色器用偏移定位当前批次的起点。
				constants->operatorKind = static_cast<uint32_t>(operatorKind);
				context->Unmap(globalCB.Get(), 0);
			}

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

	int InkRenderer::DrawHighlighterPrimitives(const std::vector<HighlighterPrimitive>& primitives,
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

	void InkRenderer::DrawTransientDrawingCursor(const DrawingCursorVisual& visual)
	{
		if (!visual.visible || !IsValidDrawingCursorAppearance(visual.appearance) ||
			!backBufferRTV || !inkDataBuffer || !globalCB) return;
		const DrawingCursorAppearance& appearance = visual.appearance;
		const float shapeType = appearance.shape == DrawingCursorShape::Circle ? 4.0f
			: appearance.shape == DrawingCursorShape::Rectangle ? 5.0f : 6.0f;
		const InkPoint cursorData[2] = {
			{ visual.x, visual.y, appearance.width * 0.5f, appearance.height * 0.5f },
			{ appearance.outlineWidth, appearance.fillAlpha, 0.0f, 0.0f }
		};

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (FAILED(context->Map(inkDataBuffer.Get(), 0,
			D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
		std::memcpy(mapped.pData, cursorData, sizeof(cursorData));
		context->Unmap(inkDataBuffer.Get(), 0);
		m_bufferHead = 2;

		if (FAILED(context->Map(globalCB.Get(), 0,
			D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
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
		context->Draw(6, 0);

		ID3D11ShaderResourceView* nullResource[] = { nullptr };
		context->VSSetShaderResources(0, 1, nullResource);
	}

	void InkRenderer::ConfigureLaserStyle(float dpiScale) noexcept
	{
		const float scale = std::isfinite(dpiScale) ? std::max(dpiScale, 0.01f) : 1.0f;
		const float solidRadius = LaserSolidRadius(scale);
		const float coreRadius = LaserCoreRadius(solidRadius);
		laserStyleConstants_.radii = DirectX::XMFLOAT4(
			coreRadius, solidRadius, LaserDiffuseExtent(scale),
			coreRadius * kLaserScatterHalfWidthToCoreRatio);
		laserStyleConstants_.coreColor = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		laserStyleConstants_.scatterColor = DirectX::XMFLOAT4(
			1.0f, 240.0f / 255.0f, 243.0f / 255.0f, 0.94f);
		laserStyleConstants_.borderColor = DirectX::XMFLOAT4(
			1.0f, 11.0f / 255.0f, 30.0f / 255.0f, 0.98f);
		laserStyleConstants_.edgeColor = DirectX::XMFLOAT4(
			1.0f, 112.0f / 255.0f, 128.0f / 255.0f, 0.72f);
		// 漫反射在实体边界达到满 alpha；edgeColor.a 只控制粉色高光的 RGB 混合强度。
		laserStyleConstants_.glowColor = DirectX::XMFLOAT4(1.0f, 0.04f, 0.10f, 1.0f);
		// z/w 是红色实体外侧漫反射曲线的边缘高亮阈值。
		laserStyleConstants_.parameters = DirectX::XMFLOAT4(
			1.0f, scale, 0.20f, 0.29f);
	}

	void InkRenderer::ConfigureLaserParticles(
		const LaserParticleConfig& configuration, float dpiScale) noexcept
	{
		const LaserParticleConfig effectiveConfiguration =
			IsValidLaserParticleConfig(configuration)
			? configuration : LaserParticleConfig{};
		laserParticleGlowRadiusScale_ =
			effectiveConfiguration.glowRadiusScale;
		laserParticleGlowRed_ = effectiveConfiguration.glowRed;
		laserParticleGlowGreen_ = effectiveConfiguration.glowGreen;
		laserParticleGlowBlue_ = effectiveConfiguration.glowBlue;
		laserParticleGlowAlpha_ = effectiveConfiguration.glowAlpha;
		laserParticleCoreColorWhiteMix_ =
			effectiveConfiguration.coreColorWhiteMix;
		laserParticleSystem_.Configure(
			effectiveConfiguration, dpiScale, kLaserCoreDiameterRatio);
	}

	bool InkRenderer::LaserParticlesAvailable() const noexcept
	{
		return laserParticleSystem_.IsAvailable();
	}

	void InkRenderer::SimulateLaserParticles(
		float wallDeltaSeconds, float motionDeltaSeconds) noexcept
	{
		laserParticleSystem_.Simulate(wallDeltaSeconds, motionDeltaSeconds);
	}

	void InkRenderer::EmitLaserParticles(
		const LaserParticleEmissionRequest& request) noexcept
	{
		laserParticleSystem_.Emit(request);
	}

	void InkRenderer::ResetLaserParticles() noexcept
	{
		laserParticleSystem_.Reset();
	}

	bool InkRenderer::UpdateLaserStyleConstants(float opacity)
	{
		if (!laserStyleCB) return false;
		laserStyleConstants_.parameters.x = std::clamp(opacity, 0.0f, 1.0f);
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (FAILED(context->Map(laserStyleCB.Get(), 0,
			D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
		std::memcpy(mapped.pData, &laserStyleConstants_, sizeof(laserStyleConstants_));
		context->Unmap(laserStyleCB.Get(), 0);
		return true;
	}

	int InkRenderer::DrawLaserCoverage(const std::vector<InkPoint>& points)
	{
		if (points.empty()) return 0;
		if (!UpdateLaserStyleConstants(1.0f)) return -1;
		std::vector<InkPoint> dotPoints;
		const std::vector<InkPoint>* drawPoints = &points;
		if (points.size() == 1)
		{
			dotPoints = points;
			InkPoint dotEnd = points.front();
			dotEnd.x += 0.25f;
			dotPoints.push_back(dotEnd);
			drawPoints = &dotPoints;
		}

		size_t startIndex = 0;
		while (startIndex + 1 < drawPoints->size())
		{
			const size_t remaining = drawPoints->size() - startIndex;
			const size_t batchCount = std::min(remaining, kMaxBufferCapacity);
			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (FAILED(context->Map(inkDataBuffer.Get(), 0,
				D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return -1;
			std::memcpy(mapped.pData, drawPoints->data() + startIndex,
				batchCount * sizeof(InkPoint));
			context->Unmap(inkDataBuffer.Get(), 0);

			if (FAILED(context->Map(globalCB.Get(), 0,
				D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return -1;
			auto* constants = static_cast<GlobalShaderConstants*>(mapped.pData);
			constants->width = viewportWidth;
			constants->height = viewportHeight;
			constants->shapeType = 7.0f;
			constants->bufferOffset = 0;
			constants->color = {};
			constants->operatorKind = static_cast<uint32_t>(InkOperatorKind::Draw);
			context->Unmap(globalCB.Get(), 0);

			context->IASetInputLayout(nullptr);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(vertexShader.Get(), nullptr, 0);
			ID3D11ShaderResourceView* inkResources[] = { inkDataSRV.Get() };
			context->VSSetShaderResources(0, 1, inkResources);
			ID3D11Buffer* constantBuffers[] = { globalCB.Get(), laserStyleCB.Get() };
			context->VSSetConstantBuffers(0, ARRAYSIZE(constantBuffers), constantBuffers);
			context->PSSetShader(pixelShader.Get(), nullptr, 0);
			context->PSSetConstantBuffers(0, ARRAYSIZE(constantBuffers), constantBuffers);
			context->OMSetBlendState(laserCoverageBlendState.Get(), nullptr, 0xFFFFFFFF);
			context->RSSetState(rasterState.Get());
			context->Draw((static_cast<UINT>(batchCount) - 1) * 6, 0);

			ID3D11ShaderResourceView* nullResource[] = { nullptr };
			context->VSSetShaderResources(0, 1, nullResource);
			startIndex += batchCount - 1;
		}
		return 0;
	}

	void InkRenderer::DrawLaserRectPass(ID3D11RenderTargetView* dstRTV, RECT rect,
		float opacity, float shapeType, ID3D11ShaderResourceView* source,
		UINT sourceSlot, ID3D11BlendState* blendState)
	{
		if (!dstRTV || !UpdateLaserStyleConstants(opacity)) return;
		rect.left = std::max(0L, rect.left);
		rect.top = std::max(0L, rect.top);
		rect.right = std::min(static_cast<LONG>(viewportWidth), rect.right);
		rect.bottom = std::min(static_cast<LONG>(viewportHeight), rect.bottom);
		if (rect.left >= rect.right || rect.top >= rect.bottom) return;

		const InkPoint rectPoints[2] = {
			{ static_cast<float>(rect.left), static_cast<float>(rect.top), 0.0f, 0.0f },
			{ static_cast<float>(rect.right), static_cast<float>(rect.bottom), 0.0f, 0.0f }
		};
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (FAILED(context->Map(inkDataBuffer.Get(), 0,
			D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
		std::memcpy(mapped.pData, rectPoints, sizeof(rectPoints));
		context->Unmap(inkDataBuffer.Get(), 0);
		if (FAILED(context->Map(globalCB.Get(), 0,
			D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
		auto* constants = static_cast<GlobalShaderConstants*>(mapped.pData);
		constants->width = viewportWidth;
		constants->height = viewportHeight;
		constants->shapeType = shapeType;
		constants->bufferOffset = 0;
		constants->color = {};
		constants->operatorKind = static_cast<uint32_t>(InkOperatorKind::Draw);
		context->Unmap(globalCB.Get(), 0);

		// 先解绑两张 Laser SRV，再把其中任意一张安全地切换为 RTV。
		ID3D11ShaderResourceView* nullLaserResources[] = { nullptr, nullptr };
		context->PSSetShaderResources(6, ARRAYSIZE(nullLaserResources), nullLaserResources);
		SetOMTarget(dstRTV);
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(vertexShader.Get(), nullptr, 0);
		ID3D11ShaderResourceView* inkResource[] = { inkDataSRV.Get() };
		context->VSSetShaderResources(0, 1, inkResource);
		ID3D11Buffer* constantBuffers[] = { globalCB.Get(), laserStyleCB.Get() };
		context->VSSetConstantBuffers(0, ARRAYSIZE(constantBuffers), constantBuffers);
		context->PSSetShader(pixelShader.Get(), nullptr, 0);
		context->PSSetConstantBuffers(0, ARRAYSIZE(constantBuffers), constantBuffers);
		if (source && sourceSlot >= 6 && sourceSlot <= 7)
		{
			ID3D11ShaderResourceView* sourceResources[] = { source };
			context->PSSetShaderResources(sourceSlot, 1, sourceResources);
			ID3D11SamplerState* samplers[] = { operatorSampler.Get() };
			context->PSSetSamplers(0, 1, samplers);
		}
		context->OMSetBlendState(blendState, nullptr, 0xFFFFFFFF);
		context->RSSetState(rasterState.Get());
		context->Draw(6, 0);

		ID3D11ShaderResourceView* nullInk[] = { nullptr };
		ID3D11SamplerState* nullSampler[] = { nullptr };
		context->VSSetShaderResources(0, 1, nullInk);
		context->PSSetShaderResources(6, ARRAYSIZE(nullLaserResources), nullLaserResources);
		context->PSSetSamplers(0, 1, nullSampler);
	}

	void InkRenderer::ResolveLaserStrokeCoverage(
		ID3D11RenderTargetView* dstRTV, RECT rect, float opacity)
	{
		if (!laserStrokeCoverage.srv) return;
		DrawLaserRectPass(dstRTV, rect, opacity, 8.0f,
			laserStrokeCoverage.srv.Get(), 7, operatorResolveBlendState.Get());
	}

	void InkRenderer::ResolveLaserCompositedColor(
		ID3D11RenderTargetView* dstRTV, RECT rect, float opacity)
	{
		if (!laserCompositedColor.srv) return;
		DrawLaserRectPass(dstRTV, rect, opacity, 11.0f,
			laserCompositedColor.srv.Get(), 6, operatorResolveBlendState.Get());
	}

	void InkRenderer::ClearLaserCoverageRect(RECT rect)
	{
		if (!laserStrokeCoverage.rtv) return;
		DrawLaserRectPass(laserStrokeCoverage.rtv.Get(), rect, 1.0f,
			12.0f, nullptr, 0, nullptr);
	}

	void InkRenderer::DrawLaserDots(const std::vector<LaserDot>& dots)
	{
		if (dots.empty() || !backBufferRTV || !UpdateLaserStyleConstants(1.0f)) return;
		size_t startIndex = 0;
		while (startIndex < dots.size())
		{
			const size_t batchCount = std::min(dots.size() - startIndex, kMaxBufferCapacity);
			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (FAILED(context->Map(inkDataBuffer.Get(), 0,
				D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
			std::memcpy(mapped.pData, dots.data() + startIndex,
				batchCount * sizeof(LaserDot));
			context->Unmap(inkDataBuffer.Get(), 0);
			if (FAILED(context->Map(globalCB.Get(), 0,
				D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
			auto* constants = static_cast<GlobalShaderConstants*>(mapped.pData);
			constants->width = viewportWidth;
			constants->height = viewportHeight;
			constants->shapeType = 9.0f;
			constants->bufferOffset = 0;
			constants->color = {};
			constants->operatorKind = static_cast<uint32_t>(InkOperatorKind::Draw);
			context->Unmap(globalCB.Get(), 0);

			SetOMTarget(backBufferRTV.Get());
			context->IASetInputLayout(nullptr);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(vertexShader.Get(), nullptr, 0);
			ID3D11ShaderResourceView* inkResources[] = { inkDataSRV.Get() };
			context->VSSetShaderResources(0, 1, inkResources);
			ID3D11Buffer* constantBuffers[] = { globalCB.Get(), laserStyleCB.Get() };
			context->VSSetConstantBuffers(0, ARRAYSIZE(constantBuffers), constantBuffers);
			context->PSSetShader(pixelShader.Get(), nullptr, 0);
			context->PSSetConstantBuffers(0, ARRAYSIZE(constantBuffers), constantBuffers);
			context->OMSetBlendState(operatorResolveBlendState.Get(), nullptr, 0xFFFFFFFF);
			context->RSSetState(rasterState.Get());
			context->Draw(static_cast<UINT>(batchCount) * 6, 0);
			ID3D11ShaderResourceView* nullResource[] = { nullptr };
			context->VSSetShaderResources(0, 1, nullResource);
			startIndex += batchCount;
		}
	}

	void InkRenderer::DrawLaserParticles()
	{
		ID3D11ShaderResourceView* particleResource =
			laserParticleSystem_.ParticleShaderResourceView();
		if (!particleResource || !backBufferRTV ||
			!UpdateLaserStyleConstants(1.0f)) return;

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (FAILED(context->Map(globalCB.Get(), 0,
			D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
		auto* constants = static_cast<GlobalShaderConstants*>(mapped.pData);
		constants->width = viewportWidth;
		constants->height = viewportHeight;
		constants->shapeType = 10.0f;
		constants->bufferOffset = 0;
		// shape 10 的辉光随核心半径缩放；baseBrightness 决定稳定的核心色相层级。
		constants->color = DirectX::XMFLOAT4(
			laserParticleGlowRadiusScale_, 0.0f,
			laserParticleGlowGreen_, laserParticleGlowBlue_);
		constants->operatorKind = static_cast<uint32_t>(InkOperatorKind::Draw);
		constants->padding[0] = laserParticleGlowRed_;
		constants->padding[1] = laserParticleGlowAlpha_;
		constants->padding[2] = laserParticleCoreColorWhiteMix_;
		context->Unmap(globalCB.Get(), 0);

		SetOMTarget(backBufferRTV.Get());
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(vertexShader.Get(), nullptr, 0);
		context->VSSetShaderResources(8, 1, &particleResource);
		ID3D11Buffer* constantBuffers[] = { globalCB.Get(), laserStyleCB.Get() };
		context->VSSetConstantBuffers(0, ARRAYSIZE(constantBuffers), constantBuffers);
		context->PSSetShader(pixelShader.Get(), nullptr, 0);
		context->PSSetConstantBuffers(0, ARRAYSIZE(constantBuffers), constantBuffers);
		context->OMSetBlendState(operatorResolveBlendState.Get(), nullptr, 0xFFFFFFFF);
		context->RSSetState(rasterState.Get());
		context->DrawInstanced(6, kLaserParticleCapacity, 0, 0);

		ID3D11ShaderResourceView* nullParticle[] = { nullptr };
		context->VSSetShaderResources(8, 1, nullParticle);
	}

	void InkRenderer::WarmUpLaserShaders() noexcept
	{
		// 在主循环开始前，用零宽高视口提交一次各激光 shape 的 draw call。
		// GPU 驱动（尤其 Qualcomm/Adreno 的延迟 JIT 模型）会在收到 draw call 时立即编译
		// 对应的着色器执行路径，而非等到第一次真实绘制。
		// 对已在 CreateShader 阶段完成编译的驱动（Nvidia/AMD/Intel/WARP），此函数
		// 只提交几个零像素 draw call，无可见副作用，开销可忽略。
		if (!context || !backBufferRTV) return;

		// 保存当前视口，替换为零宽高视口以抑制任何像素写入。
		D3D11_VIEWPORT savedVP = {};
		UINT vpCount = 1;
		context->RSGetViewports(&vpCount, &savedVP);
		const D3D11_VIEWPORT emptyVP = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
		context->RSSetViewports(1, &emptyVP);

		// shape 10: 粒子 VS/PS（DrawLaserParticles 内部含所有绑定与 DrawInstanced）
		DrawLaserParticles();

		// shape 9: 激光笔尖 LaserDot（需至少1条数据以通过 dots.empty() 检查）
		const LaserDot warmUpDot{ 0.0f, 0.0f, 1.0f };
		DrawLaserDots({ warmUpDot });

		// shape 7/8/11/12: 笔画 coverage 生成与 resolve 路径
		if (laserStrokeCoverage.rtv && laserCompositedColor.rtv && laserCompositedColor.srv)
		{
			const RECT warmUpRect{ 0, 0, 1, 1 };
			const std::vector<InkPoint> warmUpPts{
				{ 0.0f, 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 1.0f, 0.0f }
			};
			ClearLaserCoverageRect(warmUpRect);                              // shape 12
			SetLaserCoverageTarget(laserStrokeCoverage);
			DrawLaserCoverage(warmUpPts);                                    // shape 7
			ResolveLaserStrokeCoverage(                                      // shape 8
				laserCompositedColor.rtv.Get(), warmUpRect);
			ResolveLaserCompositedColor(                                     // shape 11
				backBufferRTV.Get(), warmUpRect, 1.0f);
		}

		// 恢复视口，不留任何可见副作用。
		context->RSSetViewports(1, &savedVP);
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

	void InkRenderer::ApplyOperatorLayers(ID3D11RenderTargetView* dstRTV,
		const OperatorLayerResources& stableLayer, const OperatorLayerResources& liveLayer,
		RECT rect, OperatorLayerMergeMode mergeMode)
	{
		if (!dstRTV || !stableLayer.addSRV || !stableLayer.retainSRV ||
			!liveLayer.addSRV || !liveLayer.retainSRV) return;
		rect.left = std::max(0L, rect.left);
		rect.top = std::max(0L, rect.top);
		rect.right = std::min(static_cast<LONG>(viewportWidth), rect.right);
		rect.bottom = std::min(static_cast<LONG>(viewportHeight), rect.bottom);
		if (rect.left >= rect.right || rect.top >= rect.bottom) return;

		InkPoint rectPoints[2] = {
			{ static_cast<float>(rect.left), static_cast<float>(rect.top), 0.0f, 0.0f },
			{ static_cast<float>(rect.right), static_cast<float>(rect.bottom), 0.0f, 0.0f }
		};
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (FAILED(context->Map(inkDataBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
		std::memcpy(mapped.pData, rectPoints, sizeof(rectPoints)); // 复用墨迹缓冲区传入要混合的矩形范围。
		context->Unmap(inkDataBuffer.Get(), 0);
		m_bufferHead = 2;

		if (FAILED(context->Map(globalCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
		auto* constants = static_cast<GlobalShaderConstants*>(mapped.pData);
		constants->width = viewportWidth;
		constants->height = viewportHeight;
		constants->color = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
		constants->shapeType = mergeMode == OperatorLayerMergeMode::Ordered ? 2.0f : 1.0f;
		constants->bufferOffset = 0;
		constants->operatorKind = static_cast<uint32_t>(InkOperatorKind::Draw);
		context->Unmap(globalCB.Get(), 0);

		SetOMTarget(dstRTV);
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(vertexShader.Get(), nullptr, 0);
		ID3D11ShaderResourceView* shaderResources[] = { inkDataSRV.Get() };
		context->VSSetShaderResources(0, 1, shaderResources);
		ID3D11Buffer* constantBuffers[] = { globalCB.Get() };
		context->VSSetConstantBuffers(0, 1, constantBuffers);
		context->PSSetShader(pixelShader.Get(), nullptr, 0);
		context->PSSetConstantBuffers(0, 1, constantBuffers);
		ID3D11ShaderResourceView* compositeResources[] = {
			stableLayer.addSRV.Get(),
			stableLayer.retainSRV.Get(),
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

		ID3D11ShaderResourceView* nullResources[] = { nullptr, nullptr, nullptr, nullptr, nullptr };
		ID3D11SamplerState* nullSampler[] = { nullptr };
		context->VSSetShaderResources(0, 1, nullResources);
		context->PSSetShaderResources(1, ARRAYSIZE(nullResources), nullResources); // 解除 SRV 绑定，避免后续作为 RTV 时冲突。
		context->PSSetSamplers(0, 1, nullSampler);
	}

	void InkRenderer::SetScreenSize(float width, float height)
	{
		viewportWidth = width;
		viewportHeight = height;
		D3D11_VIEWPORT viewport = { 0, 0, width, height, 0.0f, 1.0f };
		context->RSSetViewports(1, &viewport);
	}

	void InkRenderer::SetOMTarget(ID3D11RenderTargetView* renderTargetView)
	{
		ID3D11RenderTargetView* targets[] = { renderTargetView };
		context->OMSetRenderTargets(1, targets, nullptr);
		if (dsState) context->OMSetDepthStencilState(dsState.Get(), 0);
	}

	void InkRenderer::SetOperatorTarget(const OperatorLayerResources& layer)
	{
		ID3D11RenderTargetView* targets[] = { layer.addRTV.Get(), layer.retainRTV.Get() };
		context->OMSetRenderTargets(ARRAYSIZE(targets), targets, nullptr);
		if (dsState) context->OMSetDepthStencilState(dsState.Get(), 0);
	}

	void InkRenderer::SetLaserCoverageTarget(const LaserCoverageResources& layer)
	{
		ID3D11RenderTargetView* targets[] = { layer.rtv.Get() };
		context->OMSetRenderTargets(1, targets, nullptr);
		if (dsState) context->OMSetDepthStencilState(dsState.Get(), 0);
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
	}

	void InkRenderer::ClearLaserCoverage(const LaserCoverageResources& layer)
	{
		if (layer.rtv) ClearRTV(layer.rtv.Get(), kTransparentLayerClearColor);
	}

	void InkRenderer::ClearAllLaserCoverage()
	{
		ClearLaserCoverage(laserCompositedColor);
		ClearLaserCoverage(laserStrokeCoverage);
	}

	bool InkRenderer::CreateOperatorLayerResources(UINT width, UINT height, OperatorLayerResources& layer)
	{
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

	bool InkRenderer::CreateSizeDependentResources(IDXGISwapChain1* swapChain, UINT width, UINT height)
	{
		if (!device || !context || !swapChain || width == 0 || height == 0) return false;
		if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
			reinterpret_cast<void**>(backBufferTexture.ReleaseAndGetAddressOf())))) return false; // 取得最终呈现缓冲区。
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
		if (!CreateOperatorLayerResources(width, height, layerL1)) return false; // L1 保存当前笔画已确认前缀操作。
		if (!CreateOperatorLayerResources(width, height, layerL0)) return false; // L0 保存每帧变化的笔锋和预测操作。
		if (!CreateLaserCoverageResources(width, height, laserCompositedColor)) return false;
		if (!CreateLaserCoverageResources(width, height, laserStrokeCoverage)) return false;

		SetScreenSize(static_cast<float>(width), static_cast<float>(height));
		return true;
	}

	void InkRenderer::ReleaseSizeDependentResources()
	{
		if (context)
		{
			ID3D11ShaderResourceView* nullResources[] = {
				nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
			context->OMSetRenderTargets(0, nullptr, nullptr);
			context->VSSetShaderResources(0, ARRAYSIZE(nullResources), nullResources); // t8 粒子 SRV 也随 Resize 显式解绑。
			context->PSSetShaderResources(0, ARRAYSIZE(nullResources), nullResources); // 释放前先解绑，避免 D3D 仍持有引用。
			context->Flush();
		}
		backBufferRTV.Reset();
		backBufferTexture.Reset();
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
		laserCompositedColor.rtv.Reset();
		laserCompositedColor.srv.Reset();
		laserCompositedColor.texture.Reset();
		laserStrokeCoverage.rtv.Reset();
		laserStrokeCoverage.srv.Reset();
		laserStrokeCoverage.texture.Reset();
	}

	void InkRenderer::ReleaseResources()
	{
		laserParticleSystem_.Release();
		ReleaseSizeDependentResources();
		vertexShader.Reset();
		pixelShader.Reset();
		globalCB.Reset();
		laserStyleCB.Reset();
		inkDataBuffer.Reset();
		inkDataSRV.Reset();
		highlighterPrimitiveBuffer.Reset();
		highlighterPrimitiveSRV.Reset();
		operatorSampler.Reset();
		strokeOperatorBlendState.Reset();
		operatorResolveBlendState.Reset();
		laserCoverageBlendState.Reset();
		rasterState.Reset();
		dsState.Reset();
		device.Reset();
		context.Reset();
		m_bufferHead = 0;
		viewportWidth = 0.0f;
		viewportHeight = 0.0f;
	}

	bool InkRenderer::Resize(IDXGISwapChain1* swapChain, UINT width, UINT height)
	{
		if (!swapChain || width == 0 || height == 0) return false;
		DXGI_SWAP_CHAIN_DESC1 swapChainDescription = {};
		if (FAILED(swapChain->GetDesc1(&swapChainDescription))) return false;

		const UINT oldWidth = static_cast<UINT>(viewportWidth);
		const UINT oldHeight = static_cast<UINT>(viewportHeight);
		Microsoft::WRL::ComPtr<ID3D11Texture2D> oldL2Texture = layerL2Texture; // 临时保留旧稳定层用于拷贝。
		Microsoft::WRL::ComPtr<ID3D11Texture2D> oldL1AddTexture = layerL1.addTexture;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> oldL1RetainTexture = layerL1.retainTexture; // L1 的完整仿射操作都要保留。
		Microsoft::WRL::ComPtr<ID3D11Texture2D> oldLaserCompositedTexture =
			laserCompositedColor.texture;
		ReleaseSizeDependentResources();

		// waitable swapchain 在部分驱动上要求 resize 时原样保留 BufferCount/Format/Flags。
		if (FAILED(swapChain->ResizeBuffers(swapChainDescription.BufferCount, width, height,
			swapChainDescription.Format, swapChainDescription.Flags))) return false;
		if (!CreateSizeDependentResources(swapChain, width, height)) return false;

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
			if (oldLaserCompositedTexture)
				CopyResource(laserCompositedColor.texture.Get(),
					oldLaserCompositedTexture.Get(), keepRect);
		}
		return true;
	}

	bool InkRenderer::Init(ID3D11Device* inDevice, ID3D11DeviceContext* inContext, IDXGISwapChain1* swapChain, UINT width, UINT height)
	{
		device = inDevice; // 渲染器只借用外部统一创建的 D3D 设备。
		context = inContext;
		if (!CreateSizeDependentResources(swapChain, width, height)) return false;

		D3D11_BUFFER_DESC constantBufferDescription = {
			sizeof(GlobalShaderConstants), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER,
			D3D11_CPU_ACCESS_WRITE, 0, 0
		};
		if (FAILED(device->CreateBuffer(&constantBufferDescription, nullptr, globalCB.ReleaseAndGetAddressOf()))) return false;
		constantBufferDescription.ByteWidth = sizeof(LaserStyleConstants);
		if (FAILED(device->CreateBuffer(&constantBufferDescription, nullptr,
			laserStyleCB.ReleaseAndGetAddressOf()))) return false;
		ConfigureLaserStyle(1.0f);

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

		D3D11_BUFFER_DESC inkBufferDescription = {};
		inkBufferDescription.ByteWidth = static_cast<UINT>(kMaxBufferCapacity * sizeof(InkPoint));
		inkBufferDescription.Usage = D3D11_USAGE_DYNAMIC;
		inkBufferDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		inkBufferDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
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

		D3D11_SAMPLER_DESC samplerDescription = {};
		samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; // 混合图层按像素采样，不做线性模糊。
		samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDescription.ComparisonFunc = D3D11_COMPARISON_NEVER;
		samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(device->CreateSamplerState(&samplerDescription, operatorSampler.ReleaseAndGetAddressOf()))) return false;
		return LoadShaders();
	}

	bool InkRenderer::LoadShaders()
	{
		const ShaderBlob vertexShaderBlob = LoadShaderFromResource(IDR_VS1);
		const ShaderBlob pixelShaderBlob = LoadShaderFromResource(IDR_PS1);
		if (!vertexShaderBlob.data || !pixelShaderBlob.data) return false;
		if (FAILED(device->CreateVertexShader(vertexShaderBlob.data, vertexShaderBlob.size, nullptr, vertexShader.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(device->CreatePixelShader(pixelShaderBlob.data, pixelShaderBlob.size, nullptr, pixelShader.ReleaseAndGetAddressOf()))) return false;
		const ShaderBlob updateShaderBlob =
			LoadShaderFromResource(IDR_LASER_PARTICLE_UPDATE_CS);
		const ShaderBlob emitShaderBlob =
			LoadShaderFromResource(IDR_LASER_PARTICLE_EMIT_CS);
		const bool particlesReady = updateShaderBlob.data && emitShaderBlob.data &&
			laserParticleSystem_.Initialize(device.Get(), context.Get(),
				{ updateShaderBlob.data, updateShaderBlob.size },
				{ emitShaderBlob.data, emitShaderBlob.size });
		if (!particlesReady)
			ReportLaserParticleUnavailableOnce();
		return true;
	}
}
