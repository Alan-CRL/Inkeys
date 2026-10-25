#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Setting.EraserDemoGpu.h"
#include "Setting.EraserDemoData.g.h"
#include "../../Drawing/Draw3/Assets/EraserGripVisual.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace Inkeys::UI::Setting::Design
{
	namespace
	{
		using Microsoft::WRL::ComPtr;
		constexpr double FrameInterval = 1.0 / EraserDemoData::FramesPerSecond;
		constexpr unsigned SourceWidth = 768, SourceHeight = 432;
		constexpr const char* Shader = R"hlsl(
cbuffer Playback : register(b0)
{
    float4 cursor; // x, y, fixed diameter, speed diameter
    float4 playback; // erase progress, cursor alpha, dark, source pixels per output pixel
    float4 speedPose; // independent speed cursor x, y, alpha
    float4 grip; // outline, stripe radius, stripe half height, stripe offset
    float4 material; // opacity, outline gray
};
Texture2D atlas : register(t0);
SamplerState linearClamp : register(s0);
struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Vertex VS(uint id : SV_VertexID)
{
    Vertex result;
    result.uv = float2((id << 1) & 2, id & 2);
    result.position = float4(result.uv * float2(2,-2) + float2(-1,1),0,1);
    return result;
}
float2 AtlasUv(float2 pixel, float column)
{
    return (clamp(pixel,float2(0.5,0.5),float2(767.5,431.5)) + float2(column*768,0)) / float2(2304,432);
}
float Coverage(float distance)
{
    return saturate(0.5-distance/playback.w);
}
float Capsule(float2 p, float radius, float halfHeight)
{
    p.y -= clamp(p.y,-max(halfHeight-radius,0),max(halfHeight-radius,0));
    return length(p)-radius;
}
float4 PS(Vertex input) : SV_Target
{
    float side = input.uv.x >= 0.5 ? 1 : 0;
    float2 pixel = float2(input.uv.x*2-side,input.uv.y)*float2(768,432);
    float4 ink = atlas.SampleLevel(linearClamp,AtlasUv(pixel,0),0);
    float erased = 0;
    [branch] if (playback.x > 0)
    {
        float4 mask = atlas.SampleLevel(linearClamp,AtlasUv(pixel,side+1),0);
        erased = playback.x >= 1 ? mask.b
            : mask.b*saturate((playback.x-mask.r)/max(mask.g-mask.r,1.0/255.0));
    }
    float3 background = playback.z > 0.5 ? float3(18,59,50)/255 : float3(247,248,250)/255;
    float3 color = ink.rgb;
    if (playback.z > 0.5)
    {
        color = ink.b > ink.r+0.15 ? float3(111,200,236)/255
            : (ink.r > ink.b+0.25 ? float3(242,192,104)/255 : float3(234,242,239)/255);
    }
    float3 result = lerp(background,color,ink.a*(1-erased));
    float diameter = side > 0.5 ? cursor.w : cursor.z;
    float cursorAlpha = side > 0.5 ? speedPose.z : playback.y;
    float2 local = pixel-(side > 0.5 ? speedPose.xy : cursor.xy);
    [branch] if (cursorAlpha > 0 && max(abs(local.x),abs(local.y)) < diameter*0.5+playback.w)
    {
        float distance = length(local)-diameter*0.5;
        float outer = Coverage(distance), inner = Coverage(distance+diameter*grip.x);
        float outlineAlpha = max(0,outer-inner)*material.x*cursorAlpha;
        float fillAlpha = inner*material.x*cursorAlpha;
        float alpha = outlineAlpha+fillAlpha;
        float3 rgb = alpha > 0 ? (outlineAlpha*material.y+fillAlpha)/alpha : 0;
        float stripe = min(inner,max(
            Coverage(Capsule(local-float2(-diameter*grip.w,0),diameter*grip.y,diameter*grip.z)),
            Coverage(Capsule(local-float2(diameter*grip.w,0),diameter*grip.y,diameter*grip.z))));
        rgb += stripe*(material.y-rgb);
        alpha += stripe*(material.x*cursorAlpha-alpha);
        result = rgb*alpha+result*(1-alpha);
    }
    return float4(result,1);
}
)hlsl";

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
			ComPtr<ID3D11Buffer> constants;
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
				context->VSGetShader(vertex.GetAddressOf(),classes[0],&classCounts[0]);
				context->PSGetShader(pixel.GetAddressOf(),classes[1],&classCounts[1]);
				context->GSGetShader(geometry.GetAddressOf(),classes[2],&classCounts[2]);
				context->HSGetShader(hull.GetAddressOf(),classes[3],&classCounts[3]);
				context->DSGetShader(domain.GetAddressOf(),classes[4],&classCounts[4]);
				context->PSGetConstantBuffers(0,1,constants.GetAddressOf());
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
				context->VSSetShader(vertex.Get(),classes[0],classCounts[0]);
				context->PSSetShader(pixel.Get(),classes[1],classCounts[1]);
				context->GSSetShader(geometry.Get(),classes[2],classCounts[2]);
				context->HSSetShader(hull.Get(),classes[3],classCounts[3]);
				context->DSSetShader(domain.Get(),classes[4],classCounts[4]);
				context->PSSetConstantBuffers(0,1,constants.GetAddressOf());
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
		ComPtr<ID3D11Buffer> constants;
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
			ComPtr<ID3DBlob> vs, ps, errors;
			auto compile=[&](const char* entry, const char* profile, ID3DBlob** output) {
				errors.Reset();
				const HRESULT status=D3DCompile(Shader,std::strlen(Shader),"Setting.EraserDemo",nullptr,nullptr,entry,
					profile,D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,output,errors.GetAddressOf());
				if (FAILED(status) && errors) error.assign(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize());
				return status;
			};
			HRESULT status=compile("VS","vs_4_0",vs.GetAddressOf()); if (FAILED(status)) return status;
			status=compile("PS","ps_4_0",ps.GetAddressOf()); if (FAILED(status)) return status;
			status=device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,vertex.GetAddressOf());
			if (FAILED(status)) return status;
			status=device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,pixel.GetAddressOf());
			if (FAILED(status)) return status;
			D3D11_BUFFER_DESC buffer{};
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
			context->IASetInputLayout(nullptr); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(vertex.Get(),nullptr,0); context->PSSetShader(pixel.Get(),nullptr,0);
			context->GSSetShader(nullptr,nullptr,0); context->HSSetShader(nullptr,nullptr,0); context->DSSetShader(nullptr,nullptr,0);
			context->PSSetConstantBuffers(0,1,constants.GetAddressOf());
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
		// 初始化失败仅在显式 Reset 或新 epoch 后重试，避免每帧编译 shader 或重复分配。
		if (!current.vertex || !current.pixel || !current.constants || !current.sampler || !current.raster || !current.depth) return {};
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
