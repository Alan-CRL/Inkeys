#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Setting.EraserDemoGpu.h"
#include "Setting.EraserDemoData.g.h"
#include "../../Drawing/Draw3/Assets/EraserGripVisual.h"
#include <d3d11.h>
#include "../../../resource.h"
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#pragma comment(lib, "d3d11.lib")

namespace Inkeys::UI::Setting::Design
{
	namespace
	{
		using Microsoft::WRL::ComPtr;
		constexpr double FrameInterval = 1.0 / EraserDemoData::FramesPerSecond;
		constexpr unsigned SourceWidth = 768, SourceHeight = 432;
		// 与 ImGui 共用预编译 VS，PS 使用演示专用资源；运行时不依赖 D3DCompiler DLL。
		HRESULT LoadShaderResource(UINT id, const void*& bytecode, DWORD& size)
		{
			const HMODULE module = GetModuleHandleW(nullptr);
			const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(id), L"SHADERS");
			if (!resource) return HRESULT_FROM_WIN32(GetLastError());
			const HGLOBAL loaded = LoadResource(module, resource);
			if (!loaded) return HRESULT_FROM_WIN32(GetLastError());
			bytecode = LockResource(loaded);
			size = SizeofResource(module, resource);
			return bytecode && size ? S_OK : E_FAIL;
		}

		// 不依赖后续 ImGui 恰好覆盖状态；恢复共享 immediate context 的所有本次修改项。
		struct ContextScope
		{
			ID3D11DeviceContext* context;
			ID3D11RenderTargetView* targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
			ComPtr<ID3D11DepthStencilView> depthView;
			ComPtr<ID3D11BlendState> blend;
			ComPtr<ID3D11DepthStencilState> depth;
			ComPtr<ID3D11RasterizerState> raster;
			ComPtr<ID3D11InputLayout> layout;
			ComPtr<ID3D11VertexShader> vertex;
			ComPtr<ID3D11PixelShader> pixel;
			ComPtr<ID3D11GeometryShader> geometry;
			ComPtr<ID3D11HullShader> hull;
			ComPtr<ID3D11DomainShader> domain;
			ID3D11ClassInstance* classes[5][256]{};
			UINT classCounts[5]{256,256,256,256,256};
			ComPtr<ID3D11Buffer> constants, projection, vertexBuffer;
			UINT vertexStride = 0, vertexOffset = 0;
			ComPtr<ID3D11ShaderResourceView> image;
			ComPtr<ID3D11SamplerState> sampler;
			ComPtr<ID3D11Predicate> predicate;
			BOOL predicateValue = FALSE;
			D3D11_PRIMITIVE_TOPOLOGY topology{};
			D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
			UINT viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE, sampleMask = 0, stencilRef = 0;
			FLOAT blendFactor[4]{};

			explicit ContextScope(ID3D11DeviceContext* value) : context(value)
			{
				context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,targets,depthView.GetAddressOf());
				context->OMGetBlendState(blend.GetAddressOf(),blendFactor,&sampleMask);
				context->OMGetDepthStencilState(depth.GetAddressOf(),&stencilRef);
				context->RSGetState(raster.GetAddressOf()); context->RSGetViewports(&viewportCount,viewports);
				context->IAGetInputLayout(layout.GetAddressOf()); context->IAGetPrimitiveTopology(&topology);
				context->IAGetVertexBuffers(0,1,vertexBuffer.GetAddressOf(),&vertexStride,&vertexOffset);
				context->VSGetConstantBuffers(0,1,projection.GetAddressOf());
				context->VSGetShader(vertex.GetAddressOf(),classes[0],&classCounts[0]);
				context->PSGetShader(pixel.GetAddressOf(),classes[1],&classCounts[1]);
				context->GSGetShader(geometry.GetAddressOf(),classes[2],&classCounts[2]);
				context->HSGetShader(hull.GetAddressOf(),classes[3],&classCounts[3]);
				context->DSGetShader(domain.GetAddressOf(),classes[4],&classCounts[4]);
				context->PSGetConstantBuffers(1,1,constants.GetAddressOf());
				context->PSGetShaderResources(0,1,image.GetAddressOf());
				context->PSGetSamplers(0,1,sampler.GetAddressOf());
				context->GetPredication(predicate.GetAddressOf(),&predicateValue);
			}
			~ContextScope()
			{
				ID3D11ShaderResourceView* empty = nullptr;
				context->PSSetShaderResources(0,1,&empty);
				context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,targets,depthView.Get());
				context->OMSetBlendState(blend.Get(),blendFactor,sampleMask);
				context->OMSetDepthStencilState(depth.Get(),stencilRef);
				context->RSSetState(raster.Get()); context->RSSetViewports(viewportCount,viewports);
				context->IASetInputLayout(layout.Get()); context->IASetPrimitiveTopology(topology);
				context->IASetVertexBuffers(0,1,vertexBuffer.GetAddressOf(),&vertexStride,&vertexOffset);
				context->VSSetConstantBuffers(0,1,projection.GetAddressOf());
				context->VSSetShader(vertex.Get(),classes[0],classCounts[0]);
				context->PSSetShader(pixel.Get(),classes[1],classCounts[1]);
				context->GSSetShader(geometry.Get(),classes[2],classCounts[2]);
				context->HSSetShader(hull.Get(),classes[3],classCounts[3]);
				context->DSSetShader(domain.Get(),classes[4],classCounts[4]);
				context->PSSetConstantBuffers(1,1,constants.GetAddressOf());
				context->PSSetShaderResources(0,1,image.GetAddressOf());
				context->PSSetSamplers(0,1,sampler.GetAddressOf());
				context->SetPredication(predicate.Get(),predicateValue);
				for (auto* target : targets) if (target) target->Release();
				for (unsigned stage=0;stage<5;++stage)
					for (UINT i=0;i<classCounts[stage];++i) if (classes[stage][i]) classes[stage][i]->Release();
			}
		};

		struct Constants
		{
			float cursor[4], playback[4], speedPose[4];
			float grip[4]{ERASER_GRIP_OUTLINE_RATIO,ERASER_GRIP_STRIPE_RADIUS_RATIO,
				ERASER_GRIP_STRIPE_HALF_HEIGHT_RATIO,ERASER_GRIP_STRIPE_OFFSET_RATIO};
			float material[4]{ERASER_GRIP_OPACITY,ERASER_GRIP_OUTLINE_CHANNEL,0,0};
		};
		static_assert(sizeof(Constants)==80, "Match the eraser demo pixel shader buffer.");
		bool SamePixels(const EraserDemoData::Frame& left, const EraserDemoData::Frame& right)
		{
			if (left.eraseProgress!=right.eraseProgress || left.cursorAlpha!=right.cursorAlpha
				|| left.speedCursorAlpha!=right.speedCursorAlpha) return false;
			// 左右各自抬起后忽略其不可见位置；完成较早的一侧不再跟随另一侧补擦。
			return (left.cursorAlpha==0 || (left.x==right.x && left.y==right.y && left.fixedDiameter==right.fixedDiameter))
				&& (left.speedCursorAlpha==0 || (left.speedX==right.speedX && left.speedY==right.speedY
					&& left.speedDiameter==right.speedDiameter));
		}
		constexpr int FinalFrame()
		{
			for (int i=EraserDemoData::FramesPerScene-1;i>=0;--i)
			{
				const auto& frame=EraserDemoData::Frames[0][i];
				if (frame.eraseProgress>=1 && frame.cursorAlpha==0 && frame.speedCursorAlpha==0 && frame.opacity==1) return i;
			}
			return EraserDemoData::FramesPerScene-1;
		}
		constexpr int StaticResultFrame = FinalFrame();
	}

	struct EraserDemoPlayer::State
	{
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11VertexShader> vertex;
		ComPtr<ID3D11PixelShader> pixel;
		ComPtr<ID3D11InputLayout> layout;
		ComPtr<ID3D11Buffer> constants, projection, vertices;
		ComPtr<ID3D11SamplerState> sampler;
		ComPtr<ID3D11RasterizerState> raster;
		ComPtr<ID3D11DepthStencilState> depth;
		ComPtr<ID3D11Texture2D> target;
		ComPtr<ID3D11RenderTargetView> targetView;
		ComPtr<ID3D11ShaderResourceView> targetImage;
		std::array<ID3D11ShaderResourceView*,2> atlasKey{};
		EraserDemoStats stats;
		EraserDemoPresentation presentation;
		EraserDemoData::Frame renderedFrame{};
		std::uint64_t revision = 0;
		HRESULT result = S_OK, allocationFailure = S_OK;
		std::string error;
		double seconds = 0, renderBudget = FrameInterval;
		unsigned failedWidth = 0;
		bool triedInitialize = false, triedAtlases = false, validAtlases = false, rendered = false,
			wasVisible = false, wasAnimated = true, renderedDark = false, failedRender = false;

		HRESULT Initialize()
		{
			triedInitialize=true; ++stats.shaderBuilds;
			const void* bytecode = nullptr;
			DWORD size = 0;
			HRESULT status=LoadShaderResource(IDR_SHADERS2,bytecode,size);
			if (FAILED(status)) { error="Missing embedded ImGui vertex shader."; return status; }
			status=device->CreateVertexShader(bytecode,size,nullptr,vertex.GetAddressOf());
			if (FAILED(status)) return status;
			const D3D11_INPUT_ELEMENT_DESC elements[] = {
				{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,offsetof(ImDrawVert,pos),D3D11_INPUT_PER_VERTEX_DATA,0},
				{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,offsetof(ImDrawVert,uv),D3D11_INPUT_PER_VERTEX_DATA,0},
				{"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,offsetof(ImDrawVert,col),D3D11_INPUT_PER_VERTEX_DATA,0}
			};
			status=device->CreateInputLayout(elements,3,bytecode,size,layout.GetAddressOf()); if (FAILED(status)) return status;
			status=LoadShaderResource(IDR_SETTING_ERASER_DEMO_PS,bytecode,size);
			if (FAILED(status)) { error="Missing embedded eraser demo pixel shader."; return status; }
			status=device->CreatePixelShader(bytecode,size,nullptr,pixel.GetAddressOf());
			if (FAILED(status)) return status;
			// 标准 ImDrawVert 全屏三角形配合单位投影，直接复用 ImGui VS 的输入合同。
			const ImDrawVert triangle[] = {
				{{-1,1},{0,0},IM_COL32_WHITE}, {{3,1},{2,0},IM_COL32_WHITE}, {{-1,-3},{0,2},IM_COL32_WHITE}
			};
			D3D11_BUFFER_DESC buffer{}; buffer.ByteWidth=sizeof(triangle);
			buffer.Usage=D3D11_USAGE_IMMUTABLE; buffer.BindFlags=D3D11_BIND_VERTEX_BUFFER;
			D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem=triangle;
			status=device->CreateBuffer(&buffer,&initial,vertices.GetAddressOf()); if (FAILED(status)) return status;
			const float identity[4][4] = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
			buffer.ByteWidth=sizeof(identity); buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER; initial.pSysMem=identity;
			status=device->CreateBuffer(&buffer,&initial,projection.GetAddressOf()); if (FAILED(status)) return status;
			buffer.ByteWidth=sizeof(Constants); buffer.Usage=D3D11_USAGE_DYNAMIC;
			buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER; buffer.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
			status=device->CreateBuffer(&buffer,nullptr,constants.GetAddressOf()); if (FAILED(status)) return status;
			D3D11_SAMPLER_DESC sampling{};
			sampling.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			sampling.AddressU=sampling.AddressV=sampling.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
			sampling.MaxLOD=D3D11_FLOAT32_MAX;
			status=device->CreateSamplerState(&sampling,sampler.GetAddressOf()); if (FAILED(status)) return status;
			D3D11_RASTERIZER_DESC rasterizer{};
			rasterizer.FillMode=D3D11_FILL_SOLID; rasterizer.CullMode=D3D11_CULL_NONE; rasterizer.DepthClipEnable=TRUE;
			status=device->CreateRasterizerState(&rasterizer,raster.GetAddressOf()); if (FAILED(status)) return status;
			D3D11_DEPTH_STENCIL_DESC depthState{};
			depthState.DepthEnable=FALSE; depthState.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
			depthState.DepthFunc=D3D11_COMPARISON_ALWAYS;
			return device->CreateDepthStencilState(&depthState,depth.GetAddressOf());
		}

		bool ValidateAtlases(const std::array<ID3D11ShaderResourceView*,2>& images)
		{
			if (triedAtlases && atlasKey==images) return validAtlases;
			atlasKey=images; triedAtlases=true; validAtlases=false; rendered=false;
			for (auto* image : images)
			{
				if (!image) return false;
				ComPtr<ID3D11Device> imageDevice; image->GetDevice(imageDevice.GetAddressOf());
				if (imageDevice.Get()!=device.Get()) return false;
				ComPtr<ID3D11Resource> resource; ComPtr<ID3D11Texture2D> texture;
				image->GetResource(resource.GetAddressOf());
				if (FAILED(resource.As(&texture))) return false;
				D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
				if (desc.Width!=SourceWidth*3 || desc.Height!=SourceHeight || desc.ArraySize!=1) return false;
			}
			validAtlases=true; return true;
		}

		HRESULT Resize(unsigned width)
		{
			if (target && stats.width==width*2) return S_OK;
			if (failedWidth==width) return allocationFailure;
			failedWidth=width;
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width=width*2; desc.Height=width*SourceHeight/SourceWidth;
			desc.MipLevels=desc.ArraySize=1; desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
			desc.SampleDesc.Count=1; desc.Usage=D3D11_USAGE_DEFAULT;
			desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
			ComPtr<ID3D11Texture2D> newTarget; ComPtr<ID3D11RenderTargetView> newView;
			ComPtr<ID3D11ShaderResourceView> newImage;
			HRESULT status=device->CreateTexture2D(&desc,nullptr,newTarget.GetAddressOf()); if (FAILED(status)) return allocationFailure=status;
			status=device->CreateRenderTargetView(newTarget.Get(),nullptr,newView.GetAddressOf()); if (FAILED(status)) return allocationFailure=status;
			status=device->CreateShaderResourceView(newTarget.Get(),nullptr,newImage.GetAddressOf()); if (FAILED(status)) return allocationFailure=status;
			target=std::move(newTarget); targetView=std::move(newView); targetImage=std::move(newImage);
			stats.width=desc.Width; stats.height=desc.Height; ++stats.targetAllocations;
			failedWidth=0; rendered=false; failedRender=false; return S_OK;
		}

		HRESULT Render(ID3D11DeviceContext* context, int scene, const EraserDemoData::Frame& frame, bool dark)
		{
			Constants values{{frame.x,frame.y,frame.fixedDiameter,frame.speedDiameter},
				{frame.eraseProgress,frame.cursorAlpha,dark ? 1.0F : 0.0F,SourceWidth/(stats.width*0.5F)},
				{frame.speedX,frame.speedY,frame.speedCursorAlpha,0}};
			D3D11_MAPPED_SUBRESOURCE mapped{};
			HRESULT status=context->Map(constants.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped);
			if (FAILED(status)) return status;
			std::memcpy(mapped.pData,&values,sizeof(values)); context->Unmap(constants.Get(),0);
			ContextScope restore(context);
			ID3D11ShaderResourceView* empty=nullptr;
			context->PSSetShaderResources(0,1,&empty);
			context->OMSetRenderTargets(1,targetView.GetAddressOf(),nullptr);
			context->OMSetBlendState(nullptr,nullptr,0xffffffff);
			context->OMSetDepthStencilState(depth.Get(),0);
			context->RSSetState(raster.Get());
			const D3D11_VIEWPORT viewport{0,0,static_cast<float>(stats.width),static_cast<float>(stats.height),0,1};
			context->RSSetViewports(1,&viewport);
			const UINT stride=sizeof(ImDrawVert), offset=0;
			context->IASetInputLayout(layout.Get()); context->IASetVertexBuffers(0,1,vertices.GetAddressOf(),&stride,&offset);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetConstantBuffers(0,1,projection.GetAddressOf());
			context->VSSetShader(vertex.Get(),nullptr,0); context->PSSetShader(pixel.Get(),nullptr,0);
			context->GSSetShader(nullptr,nullptr,0); context->HSSetShader(nullptr,nullptr,0); context->DSSetShader(nullptr,nullptr,0);
			context->PSSetConstantBuffers(1,1,constants.GetAddressOf());
			context->PSSetSamplers(0,1,sampler.GetAddressOf());
			context->PSSetShaderResources(0,1,&atlasKey[scene]);
			context->SetPredication(nullptr,FALSE);
			context->Draw(3,0);
			status=device->GetDeviceRemovedReason(); if (FAILED(status)) return status;
			++stats.renders; stats.renderedPixels+=static_cast<std::uint64_t>(stats.width)*stats.height; ++revision;
			renderedFrame=frame; renderedDark=dark; rendered=true; renderBudget=0;
			presentation.texture=reinterpret_cast<ImTextureID>(targetImage.Get()); presentation.scene=scene;
			return S_OK;
		}
	};

	EraserDemoPlayer::EraserDemoPlayer() : state(std::make_unique<State>()) {}
	EraserDemoPlayer::~EraserDemoPlayer() = default;
	void EraserDemoPlayer::Reset() noexcept
	{
		const auto stats=state->stats;
		const auto revision=state->revision+(state->targetImage ? 1 : 0);
		*state=State{}; state->stats=stats; state->stats.width=state->stats.height=0;
		state->stats.scene=state->stats.frame=0; state->revision=revision;
	}
	std::uint64_t EraserDemoPlayer::Revision() const noexcept { return state->revision; }
	const EraserDemoStats& EraserDemoPlayer::Stats() const noexcept { return state->stats; }
	long EraserDemoPlayer::LastResult() const noexcept { return state->result; }
	const char* EraserDemoPlayer::LastError() const noexcept { return state->error.c_str(); }

	EraserDemoPresentation EraserDemoPlayer::Prepare(ID3D11Device* device, ID3D11DeviceContext* context,
		const std::array<ID3D11ShaderResourceView*,2>& atlases, bool visible, float canvasWidthPixels,
		float delta, bool animations, float rate, bool dark)
	{
		auto& current=*state;
		if (!visible) { current.wasVisible=false; return current.presentation; }
		if (!device || !context) { current.result=E_INVALIDARG; return {}; }
		if (current.device.Get()!=device) { Reset(); current.device=device; }
		if (!current.ValidateAtlases(atlases)) { current.result=E_INVALIDARG; return {}; }
		if (!current.triedInitialize) current.result=current.Initialize();
		if (FAILED(current.result) && !current.pixel) return {};
		// 初始化失败仅在显式 Reset 或新 epoch 后重试，避免每帧重复创建资源。
		if (!current.vertex || !current.pixel || !current.layout || !current.vertices || !current.projection
			|| !current.constants || !current.sampler || !current.raster || !current.depth) return {};
		const float boundedWidth=std::clamp(std::isfinite(canvasWidthPixels) ? canvasWidthPixels : 256.0F,256.0F,768.0F);
		const unsigned width=std::min(768u,static_cast<unsigned>(std::ceil(boundedWidth/32.0F))*32u);
		if (current.failedRender && current.stats.width==width*2) return {};
		current.result=current.Resize(width); if (FAILED(current.result)) return {};
		const double dt=current.wasVisible && std::isfinite(delta) ? std::clamp(static_cast<double>(delta),0.0,0.1) : 0;
		current.wasVisible=true; current.renderBudget=std::min(FrameInterval,current.renderBudget+dt);
		const bool modeChanged=current.wasAnimated!=animations; current.wasAnimated=animations;
		if (animations && std::isfinite(rate))
		{
			const double length=2.0*EraserDemoData::FramesPerScene/EraserDemoData::FramesPerSecond;
			current.seconds=std::fmod(current.seconds+dt*std::clamp(static_cast<double>(rate),0.0,10.0),length);
		}
		const int absoluteFrame=static_cast<int>(std::floor(current.seconds*EraserDemoData::FramesPerSecond+0.00001));
		const int scene=animations ? (absoluteFrame/EraserDemoData::FramesPerScene)%2 : 0;
		const int frame=animations ? absoluteFrame%EraserDemoData::FramesPerScene : StaticResultFrame;
		const auto& sample=EraserDemoData::Frames[scene][frame];
		current.stats.scene=scene; current.stats.frame=frame;
		const bool invalid=!current.rendered || current.renderedDark!=dark || modeChanged;
		const bool pixelsChanged=invalid || current.presentation.scene!=scene || !SamePixels(current.renderedFrame,sample);
		// 时间表可随全局速率推进，离屏更新仍按未缩放的可见时间限于 30 FPS；停留/渐隐只复用纹理。
		if (pixelsChanged && (invalid || current.renderBudget+0.000001>=FrameInterval))
		{
			current.result=current.Render(context,scene,sample,dark);
			if (FAILED(current.result)) { current.failedRender=true; return {}; }
		}
		if (current.presentation.scene==scene)
			current.presentation.opacity=animations ? sample.opacity : 1.0F;
		return current.presentation;
	}
}
