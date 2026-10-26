#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>
#include "setting_eraser_demo_tests.h"
#include "../Inkeys/Inkeys/UI/Setting/Setting.EraserDemoGpu.h"
#include "../Inkeys/Inkeys/UI/Setting/Setting.EraserDemoData.g.h"
#include "../Inkeys/Inkeys/Drawing/Draw3/Assets/EraserGripVisual.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
	namespace Design = Inkeys::UI::Setting::Design;
	namespace Data = Design::EraserDemoData;
	using Microsoft::WRL::ComPtr;
	using Clock = std::chrono::steady_clock;
	constexpr unsigned CanvasWidth=768, CanvasHeight=432;
	constexpr float Step=1.0F/Data::FramesPerSecond;
	struct Checks
	{
		int failures=0;
		bool Expect(bool value, const char* message)
		{
			if (!value) { ++failures; std::cerr << "[EraserDemo] failed: " << message << '\n'; }
			return value;
		}
		bool Result(HRESULT result, const char* message)
		{
			if (SUCCEEDED(result)) return true;
			std::cerr << "[EraserDemo] HRESULT=0x" << std::hex << static_cast<unsigned long>(result) << std::dec << '\n';
			return Expect(false,message);
		}
	};
	struct Image
	{
		unsigned width=0, height=0;
		std::vector<std::uint8_t> pixels;
		const std::uint8_t* At(unsigned x, unsigned y) const
		{
			return pixels.data()+(static_cast<size_t>(y)*width+x)*4;
		}
	};
	struct Atlas
	{
		Image source;
		ComPtr<ID3D11Texture2D> texture;
		ComPtr<ID3D11ShaderResourceView> view;
	};
	struct Apartment
	{
		HRESULT result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
		~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
	};

	std::filesystem::path Repository()
	{
		// 支持从仓库及 Build/ARM64/Debug 运行，不依赖开发机器的绝对路径。
		for (auto root : {std::filesystem::current_path(),std::filesystem::absolute(std::filesystem::path(__FILE__)).parent_path()})
		{
			for (int depth=0;depth<8 && !root.empty();++depth)
			{
				if (std::filesystem::exists(root/L"Inkeys/src/setting/EraserDemoFine.png")) return root;
				const auto parent=root.parent_path(); if (parent==root) break; root=parent;
			}
		}
		return {};
	}

	bool Upload(Checks& checks, ID3D11Device* device, Atlas& atlas)
	{
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width=atlas.source.width; desc.Height=atlas.source.height;
		desc.MipLevels=desc.ArraySize=1; desc.SampleDesc.Count=1;
		desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.Usage=D3D11_USAGE_IMMUTABLE;
		desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA data{atlas.source.pixels.data(),atlas.source.width*4,0};
		return checks.Result(device->CreateTexture2D(&desc,&data,atlas.texture.ReleaseAndGetAddressOf()),"upload generated atlas")
			&& checks.Result(device->CreateShaderResourceView(atlas.texture.Get(),nullptr,atlas.view.ReleaseAndGetAddressOf()),"create generated atlas SRV");
	}
	bool Load(Checks& checks, IWICImagingFactory* factory, ID3D11Device* device,
		const std::filesystem::path& path, Atlas& atlas)
	{
		ComPtr<IWICBitmapDecoder> decoder; ComPtr<IWICBitmapFrameDecode> frame; ComPtr<IWICFormatConverter> converter;
		if (!checks.Result(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,
			WICDecodeMetadataCacheOnLoad,decoder.GetAddressOf()),"decode generated atlas PNG")
			|| !checks.Result(decoder->GetFrame(0,frame.GetAddressOf()),"get generated atlas frame")
			|| !checks.Result(factory->CreateFormatConverter(converter.GetAddressOf()),"create RGBA converter")
			|| !checks.Result(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,
				nullptr,0,WICBitmapPaletteTypeCustom),"convert straight RGBA atlas")
			|| !checks.Result(converter->GetSize(&atlas.source.width,&atlas.source.height),"get atlas dimensions")) return false;
		if (!checks.Expect(atlas.source.width==CanvasWidth*3 && atlas.source.height==CanvasHeight,"three-column source atlas dimensions")) return false;
		atlas.source.pixels.resize(static_cast<size_t>(atlas.source.width)*atlas.source.height*4);
		return checks.Result(converter->CopyPixels(nullptr,atlas.source.width*4,static_cast<UINT>(atlas.source.pixels.size()),
			atlas.source.pixels.data()),"read atlas RGBA pixels") && Upload(checks,device,atlas);
	}
	bool Readback(Checks& checks, ID3D11Device* device, ID3D11DeviceContext* context,
		Design::EraserDemoPresentation presentation, Image& image)
	{
		if (!checks.Expect(presentation.texture!=0,"demo has a usable output texture")) return false;
		auto* srv=reinterpret_cast<ID3D11ShaderResourceView*>(presentation.texture);
		ComPtr<ID3D11Resource> resource; ComPtr<ID3D11Texture2D> source, staging;
		srv->GetResource(resource.GetAddressOf());
		if (!checks.Result(resource.As(&source),"output is a 2D texture")) return false;
		D3D11_TEXTURE2D_DESC desc{}; source->GetDesc(&desc);
		if (!checks.Expect(desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM,"output has known straight RGBA format")) return false;
		image.width=desc.Width; image.height=desc.Height;
		desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
		if (!checks.Result(device->CreateTexture2D(&desc,nullptr,staging.GetAddressOf()),"create readback target")) return false;
		context->CopyResource(staging.Get(),source.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (!checks.Result(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"map actual GPU output")) return false;
		image.pixels.resize(static_cast<size_t>(image.width)*image.height*4);
		for (unsigned y=0;y<image.height;++y)
			std::memcpy(image.pixels.data()+static_cast<size_t>(y)*image.width*4,
				static_cast<const std::uint8_t*>(mapped.pData)+static_cast<size_t>(y)*mapped.RowPitch,image.width*4);
		context->Unmap(staging.Get(),0); return true;
	}
	bool Save(Checks& checks, IWICImagingFactory* factory, const std::filesystem::path& path, const Image& image)
	{
		ComPtr<IWICStream> stream; ComPtr<IWICBitmapEncoder> encoder;
		ComPtr<IWICBitmapFrameEncode> frame; ComPtr<IPropertyBag2> options;
		if (!checks.Result(factory->CreateStream(stream.GetAddressOf()),"create export stream")
			|| !checks.Result(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE),"open PNG export")
			|| !checks.Result(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,encoder.GetAddressOf()),"create PNG encoder")
			|| !checks.Result(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"initialize PNG encoder")
			|| !checks.Result(encoder->CreateNewFrame(frame.GetAddressOf(),options.GetAddressOf()),"create PNG frame")
			|| !checks.Result(frame->Initialize(options.Get()),"initialize PNG frame")
			|| !checks.Result(frame->SetSize(image.width,image.height),"size PNG export")) return false;
		WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;
		if (!checks.Result(frame->SetPixelFormat(&format),"set PNG output format")
			|| !checks.Expect(IsEqualGUID(format,GUID_WICPixelFormat32bppBGRA),"PNG codec accepts BGRA")) return false;
		auto bgra=image.pixels;
		for (size_t i=0;i<bgra.size();i+=4) std::swap(bgra[i],bgra[i+2]);
		return checks.Result(frame->WritePixels(image.height,image.width*4,static_cast<UINT>(bgra.size()),bgra.data()),"encode GPU pixels")
			&& checks.Result(frame->Commit(),"commit PNG frame") && checks.Result(encoder->Commit(),"commit PNG image");
	}

	void CheckComposite(Checks& checks, const Image& image, const Atlas& atlas, int scene, int frameIndex, bool dark)
	{
		if (!checks.Expect(image.width==CanvasWidth*2 && image.height==CanvasHeight,"pixel audit uses full native source resolution")) return;
		const auto& frame=Data::Frames[scene][frameIndex];
		const std::array<double,3> background=dark ? std::array<double,3>{18,59,50} : std::array<double,3>{247,248,250};
		size_t mismatch=0, opaque=0, checked=0;
		for (unsigned side=0;side<2;++side)
			for (unsigned y=0;y<CanvasHeight;++y)
				for (unsigned x=0;x<CanvasWidth;++x)
				{
					const auto* output=image.At(x+side*CanvasWidth,y);
					opaque+=output[3]!=255;
					const auto* ink=atlas.source.At(x,y); const auto* mask=atlas.source.At(x+(side+1)*CanvasWidth,y);
					double erased=0;
					if (frame.eraseProgress>=1) erased=mask[2]/255.0;
					else if (frame.eraseProgress>0) erased=mask[2]/255.0*std::clamp(
						(static_cast<double>(frame.eraseProgress)*255-mask[0])/std::max(1,static_cast<int>(mask[1])-mask[0]),0.0,1.0);
					std::array<double,3> color{static_cast<double>(ink[0]),static_cast<double>(ink[1]),static_cast<double>(ink[2])};
					if (dark) color=ink[2]>ink[0]+0.15*255 ? std::array<double,3>{111,200,236}
						: (ink[0]>ink[2]+0.25*255 ? std::array<double,3>{242,192,104} : std::array<double,3>{234,242,239});
					// 审计左右独立的光标位置、尺寸与抬起透明度，包含光标像素而不再跳过整块区域。
					const double diameter=side ? frame.speedDiameter : frame.fixedDiameter;
					const double localX=x+0.5-(side ? frame.speedX : frame.x), localY=y+0.5-(side ? frame.speedY : frame.y);
					const double cursorOpacity=(side ? frame.speedCursorAlpha : frame.cursorAlpha)*ERASER_GRIP_OPACITY;
					double cursorAlpha=0, cursorColor=0;
					if (cursorOpacity>0 && std::max(std::abs(localX),std::abs(localY))<diameter*0.5+1)
					{
						const double distance=std::hypot(localX,localY)-diameter*0.5;
						const double outer=std::clamp(0.5-distance,0.0,1.0);
						const double inner=std::clamp(0.5-distance-diameter*ERASER_GRIP_OUTLINE_RATIO,0.0,1.0);
						const double outlineAlpha=(outer-inner)*cursorOpacity, fillAlpha=inner*cursorOpacity;
						cursorAlpha=outlineAlpha+fillAlpha;
						cursorColor=cursorAlpha>0 ? (outlineAlpha*ERASER_GRIP_OUTLINE_CHANNEL+fillAlpha)/cursorAlpha : 0;
						const double radius=diameter*ERASER_GRIP_STRIPE_RADIUS_RATIO;
						const double halfSegment=diameter*(ERASER_GRIP_STRIPE_HALF_HEIGHT_RATIO-ERASER_GRIP_STRIPE_RADIUS_RATIO);
						const double stripeY=localY-std::clamp(localY,-halfSegment,halfSegment);
						auto stripeCoverage=[&](double offset) {
							return std::clamp(0.5-std::hypot(localX-offset,stripeY)+radius,0.0,1.0);
						};
						const double stripe=std::min(inner,std::max(stripeCoverage(-diameter*ERASER_GRIP_STRIPE_OFFSET_RATIO),
							stripeCoverage(diameter*ERASER_GRIP_STRIPE_OFFSET_RATIO)));
						cursorColor+=stripe*(ERASER_GRIP_OUTLINE_CHANNEL-cursorColor);
						cursorAlpha+=stripe*(cursorOpacity-cursorAlpha);
					}
					bool bad=false;
					for (unsigned channel=0;channel<3;++channel)
					{
						const double inkComposite=background[channel]+(color[channel]-background[channel])*ink[3]/255.0*(1-erased);
						const double expected=cursorColor*255*cursorAlpha+inkComposite*(1-cursorAlpha);
						bad|=std::abs(output[channel]-expected)>2.1;
					}
					mismatch+=bad; ++checked;
				}
		checks.Expect(opaque==0,"composite always preserves an opaque board including erased pixels");
		if (mismatch) std::cerr << "[EraserDemo] pixel mismatches=" << mismatch << '/' << checked << '\n';
		checks.Expect(mismatch==0,"GPU ink, erasure, palette and both independent cursors match the baked frame");
	}

	double InkEnergy(const Image& image, unsigned side, std::array<unsigned,4> rectangle)
	{
		double energy=0;
		for (unsigned y=rectangle[1];y<rectangle[3];++y)
			for (unsigned x=rectangle[0];x<rectangle[2];++x)
			{
				const auto* pixel=image.At(x+side*CanvasWidth,y);
				energy+=std::abs(247-static_cast<int>(pixel[0]))+std::abs(248-static_cast<int>(pixel[1]))+std::abs(250-static_cast<int>(pixel[2]));
			}
		return energy;
	}
	double RemovedFraction(const Image& before, const Image& after, unsigned side, std::array<unsigned,4> rectangle)
	{
		return 1-InkEnergy(after,side,rectangle)/std::max(1.0,InkEnergy(before,side,rectangle));
	}

	void CheckNoEarlyErasure(Checks& checks, const Image& image, const Atlas& atlas, int scene, int frameIndex)
	{
		// 独立于时间遮罩，用已经走过的光标包络排除“尚未经过却提前淡掉”的像素。
		const auto& frames=Data::Frames[scene]; const auto& current=frames[frameIndex];
		size_t ahead=0;
		for (unsigned side=0;side<2;++side)
			for (unsigned y=0;y<CanvasHeight;++y)
				for (unsigned x=0;x<CanvasWidth;++x)
				{
					const auto* ink=atlas.source.At(x,y); if (ink[3]<64) continue;
					const double cursorX=side ? current.speedX : current.x, cursorY=side ? current.speedY : current.y;
					const double cursorRadius=(side ? current.speedDiameter : current.fixedDiameter)*0.5+2;
					if ((side ? current.speedCursorAlpha : current.cursorAlpha)>0
						&& std::abs(x+0.5-cursorX)<cursorRadius && std::abs(y+0.5-cursorY)<cursorRadius) continue;
					const auto* actual=image.At(x+side*CanvasWidth,y);
					const double original=(std::abs(247-static_cast<int>(ink[0]))+std::abs(248-static_cast<int>(ink[1]))
						+std::abs(250-static_cast<int>(ink[2])))*ink[3]/255.0;
					const double remaining=std::abs(247-static_cast<int>(actual[0]))+std::abs(248-static_cast<int>(actual[1]))
						+std::abs(250-static_cast<int>(actual[2]));
					if (original-remaining<12) continue;
					bool covered=false;
					for (int i=1;i<=frameIndex && !covered;++i)
					{
						const auto& a=frames[i-1]; const auto& b=frames[i];
						if (b.eraseProgress<=0 || (side ? b.speedCursorAlpha : b.cursorAlpha)<=0) continue;
						const double ax=side ? a.speedX : a.x, ay=side ? a.speedY : a.y;
						const double dx=(side ? b.speedX : b.x)-ax, dy=(side ? b.speedY : b.y)-ay;
						const double lengthSquared=dx*dx+dy*dy;
						const double along=lengthSquared>0 ? std::clamp(((x+0.5-ax)*dx+(y+0.5-ay)*dy)/lengthSquared,0.0,1.0) : 0;
						const double radius=(side ? std::max(a.speedDiameter,b.speedDiameter) : std::max(a.fixedDiameter,b.fixedDiameter))*0.5;
						// 3px 覆盖离线 1.5*fwidth AA 和密集轨迹相对 30Hz 帧表的曲率差。
						covered=std::hypot(x+0.5-ax-along*dx,y+0.5-ay-along*dy)<=radius+3;
					}
					ahead+=!covered;
				}
		if (ahead) std::cerr << "[EraserDemo] prematurely erased pixels=" << ahead << " scene=" << scene << " frame=" << frameIndex << '\n';
		checks.Expect(ahead==0,"no ink disappears ahead of either side's already traversed erase path");
	}

	void CheckFineProtectedInk(Checks& checks, const Image& result, const Atlas& atlas)
	{
		size_t changed=0; unsigned firstX=0, firstY=0;
		const int background[]{247,248,250};
		for (unsigned y=0;y<CanvasHeight;++y)
			for (unsigned x=0;x<CanvasWidth;++x)
			{
				const auto* ink=atlas.source.At(x,y);
				if (ink[3]==0 || ink[2]>ink[0]+0.15*255) continue;
				const auto* actual=result.At(x+CanvasWidth,y);
				bool different=false;
				for (unsigned channel=0;channel<3;++channel)
				{
					const double expected=background[channel]+(static_cast<int>(ink[channel])-background[channel])*ink[3]/255.0;
					different|=std::abs(actual[channel]-expected)>2.1;
				}
				if (different && changed++==0) { firstX=x; firstY=y; }
			}
		if (changed) std::cerr << "[EraserDemo] non-target geometry pixels changed=" << changed << " first=" << firstX << ',' << firstY << '\n';
		checks.Expect(changed==0,"fine speed erasure preserves every non-blue geometric stroke and label");
	}

	void CheckMotionFrames(Checks& checks)
	{
		const auto& fine=Data::Frames[0]; const auto& sweep=Data::Frames[1];
		checks.Expect(fine[Data::FineActiveFrame].fixedDiameter>=fine[Data::FineActiveFrame].speedDiameter*2,
			"fine comparison uses a visibly thicker fixed eraser");
		checks.Expect(fine[Data::FineSettledFrame].eraseProgress==1 && fine[Data::FineSettledFrame].cursorAlpha==0
			&& fine[Data::FineSettledFrame].speedCursorAlpha==0,"named fine hold starts after both erasers finish and lift");
		const float initialDiameter=sweep.front().speedDiameter;
		int firstGrowth=-1, previousDirection=0, reversalsBeforeGrowth=0;
		double maxStep=0, maxDiameterStep=0, independentDistance=0;
		for (int i=1;i<Data::FramesPerScene;++i)
		{
			const auto& previous=sweep[i-1]; const auto& frame=sweep[i];
			if (firstGrowth<0 && frame.speedDiameter>initialDiameter+0.5F) firstGrowth=i;
			if (firstGrowth<0 && frame.eraseProgress>0 && frame.speedCursorAlpha>0)
			{
				const float delta=frame.speedX-previous.speedX;
				const int direction=delta>0.25F ? 1 : (delta<-0.25F ? -1 : 0);
				if (direction && previousDirection && direction!=previousDirection) ++reversalsBeforeGrowth;
				if (direction) previousDirection=direction;
			}
			if (frame.cursorAlpha>0 && frame.speedCursorAlpha>0)
				independentDistance=std::max(independentDistance,std::hypot(static_cast<double>(frame.x)-frame.speedX,frame.y-frame.speedY));
			for (unsigned side=0;side<2;++side)
			{
				const float opacity=side ? frame.speedCursorAlpha : frame.cursorAlpha;
				const float beforeOpacity=side ? previous.speedCursorAlpha : previous.cursorAlpha;
				if (opacity<=0 || beforeOpacity<=0 || previous.eraseProgress<=0) continue;
				const double dx=side ? frame.speedX-previous.speedX : frame.x-previous.x;
				const double dy=side ? frame.speedY-previous.speedY : frame.y-previous.y;
				maxStep=std::max(maxStep,std::hypot(dx,dy));
			}
			maxDiameterStep=std::max(maxDiameterStep,std::abs(static_cast<double>(frame.speedDiameter)-previous.speedDiameter));
		}
		checks.Expect(firstGrowth>Data::SweepWarmupFrame && firstGrowth<Data::SweepGrowingFrame && reversalsBeforeGrowth>=1,
			"speed eraser remains small through an initial return pass before gradually growing");
		checks.Expect(sweep[Data::SweepWarmupFrame].speedDiameter<=initialDiameter+0.5F
			&& sweep[Data::SweepGrowingFrame].speedDiameter>initialDiameter*2,"named warmup and growth frames expose delayed adaptation");
		checks.Expect(maxStep<32 && maxDiameterStep<12,"baked cursor movement and diameter changes have bounded frame-to-frame steps");
		checks.Expect(independentDistance>20,"fixed patching and speed erasing have independently authored paths");
		checks.Expect(sweep[Data::SweepSpeedFinishedFrame].speedCursorAlpha==0 && sweep[Data::SweepSpeedFinishedFrame].cursorAlpha>0,
			"speed eraser lifts while fixed eraser still patches missed areas");
		checks.Expect(sweep[Data::SweepFixedFinishedFrame].cursorAlpha==0 && sweep[Data::SweepFixedFinishedFrame].speedCursorAlpha==0,
			"both cursors lift before the readable final hold");
		for (const auto& scene : Data::Frames)
			checks.Expect(scene[Data::ResultFrame].eraseProgress==1 && scene[Data::ResultFrame].cursorAlpha==0
				&& scene[Data::ResultFrame].speedCursorAlpha==0 && scene[Data::ResultFrame].opacity==1,"result keyframe is a fully visible settled result");
	}

	bool ContextRestored(Checks& checks, ID3D11Device* device, ID3D11DeviceContext* context,
		Design::EraserDemoPlayer& player, const std::array<ID3D11ShaderResourceView*,2>& images)
	{
		ComPtr<ID3D11Texture2D> target; ComPtr<ID3D11RenderTargetView> view;
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width=desc.Height=8; desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
		desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.BindFlags=D3D11_BIND_RENDER_TARGET;
		if (!checks.Result(device->CreateTexture2D(&desc,nullptr,target.GetAddressOf()),"create sentinel target")
			|| !checks.Result(device->CreateRenderTargetView(target.Get(),nullptr,view.GetAddressOf()),"create sentinel view")) return false;
		ComPtr<ID3D11SamplerState> sampler;
		D3D11_SAMPLER_DESC sample{}; sample.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
		sample.AddressU=sample.AddressV=sample.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
		if (!checks.Result(device->CreateSamplerState(&sample,sampler.GetAddressOf()),"create sentinel sampler")) return false;
		ComPtr<ID3D11Buffer> constants;
		D3D11_BUFFER_DESC buffer{}; buffer.ByteWidth=32; buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
		if (!checks.Result(device->CreateBuffer(&buffer,nullptr,constants.GetAddressOf()),"create sentinel constants")) return false;
		ComPtr<ID3D11Buffer> vertices;
		buffer.ByteWidth=64; buffer.BindFlags=D3D11_BIND_VERTEX_BUFFER;
		if (!checks.Result(device->CreateBuffer(&buffer,nullptr,vertices.GetAddressOf()),"create sentinel vertices")) return false;
		const UINT stride=24, offset=4;
		context->IASetVertexBuffers(0,1,vertices.GetAddressOf(),&stride,&offset);
		const D3D11_VIEWPORT viewport{1,2,6,4,0.25F,0.75F};
		context->OMSetRenderTargets(1,view.GetAddressOf(),nullptr); context->RSSetViewports(1,&viewport);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
		context->PSSetShaderResources(0,1,&images[1]); context->PSSetSamplers(0,1,sampler.GetAddressOf());
		context->PSSetConstantBuffers(0,1,constants.GetAddressOf());
		context->PSSetConstantBuffers(1,1,constants.GetAddressOf());
		context->VSSetConstantBuffers(0,1,constants.GetAddressOf());
		const auto result=player.Prepare(device,context,images,true,768,0,false,1,true);
		checks.Expect(result.texture!=0,"offscreen rendering succeeds with existing host state");
		ComPtr<ID3D11RenderTargetView> actualView; ComPtr<ID3D11ShaderResourceView> actualImage;
		ComPtr<ID3D11SamplerState> actualSampler; ComPtr<ID3D11Buffer> actualConstants, actualMode, actualProjection, actualVertices;
		ComPtr<ID3D11PixelShader> actualPixel; ComPtr<ID3D11VertexShader> actualVertex;
		context->OMGetRenderTargets(1,actualView.GetAddressOf(),nullptr);
		context->PSGetShaderResources(0,1,actualImage.GetAddressOf()); context->PSGetSamplers(0,1,actualSampler.GetAddressOf());
		context->PSGetConstantBuffers(0,1,actualConstants.GetAddressOf());
		context->PSGetConstantBuffers(1,1,actualMode.GetAddressOf());
		context->VSGetConstantBuffers(0,1,actualProjection.GetAddressOf());
		UINT actualStride=0, actualOffset=0;
		context->IAGetVertexBuffers(0,1,actualVertices.GetAddressOf(),&actualStride,&actualOffset);
		context->PSGetShader(actualPixel.GetAddressOf(),nullptr,nullptr); context->VSGetShader(actualVertex.GetAddressOf(),nullptr,nullptr);
		D3D11_VIEWPORT actualViewport{}; UINT count=1; context->RSGetViewports(&count,&actualViewport);
		D3D11_PRIMITIVE_TOPOLOGY topology{}; context->IAGetPrimitiveTopology(&topology);
		const bool restored=checks.Expect(actualView.Get()==view.Get() && actualImage.Get()==images[1]
			&& actualSampler.Get()==sampler.Get() && actualConstants.Get()==constants.Get()
			&& actualMode.Get()==constants.Get() && actualProjection.Get()==constants.Get()
			&& actualVertices.Get()==vertices.Get() && actualStride==stride && actualOffset==offset
			&& !actualPixel && !actualVertex && count==1 && std::memcmp(&viewport,&actualViewport,sizeof(viewport))==0
			&& topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST,"shared context targets, shaders, resources, VS b0, PS b1, vertices, viewport and topology are restored");
		context->ClearState(); return restored;
	}
}

int RunSettingEraserDemoTests()
{
	Checks checks; Apartment apartment;
	if (!checks.Expect(SUCCEEDED(apartment.result) || apartment.result==RPC_E_CHANGED_MODE,"initialize WIC COM apartment")) return checks.failures;
	ComPtr<IWICImagingFactory> factory;
	if (!checks.Result(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(factory.GetAddressOf())),"create WIC factory")) return checks.failures;
	ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
	const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0};
	if (!checks.Result(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
		levels,1,D3D11_SDK_VERSION,device.GetAddressOf(),nullptr,context.GetAddressOf()),"create WARP without HWND or swap chain")) return checks.failures;
	const auto root=Repository();
	if (!checks.Expect(!root.empty(),"locate checked-in generated atlases")) return checks.failures;
	std::array<Atlas,2> atlases;
	if (!Load(checks,factory.Get(),device.Get(),root/L"Inkeys/src/setting/EraserDemoFine.png",atlases[0])
		|| !Load(checks,factory.Get(),device.Get(),root/L"Inkeys/src/setting/EraserDemoSweep.png",atlases[1])) return checks.failures;
	const std::array<ID3D11ShaderResourceView*,2> images{atlases[0].view.Get(),atlases[1].view.Get()};
	const auto output=root/L".trellis/tasks/09-06-setting-winui3-design-rebuild/research/eraser-demo-visuals";
	std::filesystem::create_directories(output);
	Design::EraserDemoPlayer player;
	auto prepare=[&](float dt=Step, bool animations=true, float rate=1, bool dark=false, bool visible=true, float width=768) {
		return player.Prepare(device.Get(),context.Get(),images,visible,width,dt,animations,rate,dark);
	};
	checks.Expect(!prepare(0,true,1,false,false).texture && player.Stats().shaderBuilds==0,"hidden demo does not initialize GPU resources");
	for (int i=0;i<100;++i)
		checks.Expect(!player.Prepare(device.Get(),context.Get(),{},true,768,Step,true,1,false).texture,"missing atlas degrades only illustration");
	checks.Expect(player.Stats().shaderBuilds==0 && player.Stats().renders==0,"failed input is not compiled or rendered repeatedly");
	auto presentation=prepare(0);
	if (!checks.Expect(presentation.texture!=0,"valid atlas initializes player"))
	{
		std::cerr << "[EraserDemo] " << player.LastError() << '\n'; return checks.failures;
	}
	checks.Expect(player.Stats().renders==1 && player.Stats().shaderBuilds==1 && player.Stats().targetAllocations==1,"single initial pipeline and output allocation");
	CheckMotionFrames(checks);
	Image fineStart, fineActive, fineResult, sweepStart, sweepWarmup, sweepActive, sweepSpeedFinished,
		sweepFixedFinished, sweepResult, darkResult, loop;
	if (!Readback(checks,device.Get(),context.Get(),presentation,fineStart)) return checks.failures;
	Save(checks,factory.Get(),output/L"fine-start.png",fineStart); CheckComposite(checks,fineStart,atlases[0],0,0,false);
	const auto initialRevision=player.Revision();
	for (int i=0;i<200;++i) prepare(0);
	checks.Expect(player.Revision()==initialRevision && player.Stats().renders==1,"unchanged frame performs no GPU work");
	int playbackFrame=0;
	auto advanceTo=[&](int absoluteFrame) {
		while (playbackFrame<absoluteFrame) { presentation=prepare(); ++playbackFrame; }
	};
	advanceTo(Data::FineActiveFrame);
	if (Readback(checks,device.Get(),context.Get(),presentation,fineActive))
	{
		Save(checks,factory.Get(),output/L"fine-active.png",fineActive);
		CheckComposite(checks,fineActive,atlases[0],0,Data::FineActiveFrame,false);
		CheckNoEarlyErasure(checks,fineActive,atlases[0],0,Data::FineActiveFrame);
	}
	advanceTo(Data::FineSettledFrame); const auto fineHoldRenders=player.Stats().renders;
	advanceTo(Data::ResultFrame);
	checks.Expect(Data::FineSettledFrame<Data::SweepFixedFinishedFrame && player.Stats().renders==fineHoldRenders,
		"fine result hold stops output rendering before the longer broad scene finishes");
	if (!Readback(checks,device.Get(),context.Get(),presentation,fineResult)) return checks.failures;
	Save(checks,factory.Get(),output/L"fine-result.png",fineResult); CheckComposite(checks,fineResult,atlases[0],0,Data::ResultFrame,false);
	checks.Expect(RemovedFraction(fineStart,fineResult,1,Data::FineTargetRegion)>0.98,"speed eraser removes the geometric construction line");
	checks.Expect(RemovedFraction(fineStart,fineResult,1,Data::FineLetterRegion)<0.005,"speed eraser preserves the neighboring geometric label");
	checks.Expect(RemovedFraction(fineStart,fineResult,0,Data::FineLetterRegion)>0.05,"thicker fixed eraser damages the neighboring geometric label");
	checks.Expect(RemovedFraction(fineStart,fineResult,1,Data::FineLineRegion)<0.005,"speed eraser preserves the unrelated lower geometric edge");
	checks.Expect(RemovedFraction(fineStart,fineResult,0,Data::FineLineRegion)>0.15,"thicker fixed eraser damages the unrelated lower geometric edge");
	CheckFineProtectedInk(checks,fineResult,atlases[0]);
	advanceTo(Data::FramesPerScene);
	checks.Expect(presentation.scene==1 && player.Stats().frame==0,"carousel switches to the second baked scene");
	if (!Readback(checks,device.Get(),context.Get(),presentation,sweepStart)) return checks.failures;
	Save(checks,factory.Get(),output/L"sweep-start.png",sweepStart);
	advanceTo(Data::FramesPerScene+Data::SweepWarmupFrame);
	if (!Readback(checks,device.Get(),context.Get(),presentation,sweepWarmup)) return checks.failures;
	Save(checks,factory.Get(),output/L"sweep-warmup.png",sweepWarmup);
	CheckComposite(checks,sweepWarmup,atlases[1],1,Data::SweepWarmupFrame,false);
	CheckNoEarlyErasure(checks,sweepWarmup,atlases[1],1,Data::SweepWarmupFrame);
	checks.Expect(RemovedFraction(sweepStart,sweepWarmup,0,Data::SweepEarlyErasedRegion)>0.6
		&& RemovedFraction(sweepStart,sweepWarmup,1,Data::SweepEarlyErasedRegion)>0.6,"both early passes already erase crossed ink before a future revisit");
	checks.Expect(RemovedFraction(sweepStart,sweepWarmup,1,Data::SweepLateRegion)<0.03,"initial small passes do not erase the still-unvisited corner");
	advanceTo(Data::FramesPerScene+Data::SweepGrowingFrame);
	if (!Readback(checks,device.Get(),context.Get(),presentation,sweepActive)) return checks.failures;
	Save(checks,factory.Get(),output/L"sweep-active.png",sweepActive);
	CheckComposite(checks,sweepActive,atlases[1],1,Data::SweepGrowingFrame,false);
	CheckNoEarlyErasure(checks,sweepActive,atlases[1],1,Data::SweepGrowingFrame);
	advanceTo(Data::FramesPerScene+Data::SweepSpeedFinishedFrame);
	if (!Readback(checks,device.Get(),context.Get(),presentation,sweepSpeedFinished)) return checks.failures;
	Save(checks,factory.Get(),output/L"sweep-speed-finished.png",sweepSpeedFinished);
	CheckComposite(checks,sweepSpeedFinished,atlases[1],1,Data::SweepSpeedFinishedFrame,false);
	checks.Expect(RemovedFraction(sweepStart,sweepSpeedFinished,1,Data::SweepTargetRegion)>0.995,"speed eraser clears the full candidate before fixed patching finishes");
	checks.Expect(RemovedFraction(sweepStart,sweepSpeedFinished,0,Data::SweepTargetRegion)<0.97,"fixed side still contains visible missed ink when speed side finishes");
	advanceTo(Data::FramesPerScene+Data::SweepFixedFinishedFrame);
	if (!Readback(checks,device.Get(),context.Get(),presentation,sweepFixedFinished)) return checks.failures;
	Save(checks,factory.Get(),output/L"sweep-fixed-finished.png",sweepFixedFinished);
	CheckComposite(checks,sweepFixedFinished,atlases[1],1,Data::SweepFixedFinishedFrame,false);
	checks.Expect(InkEnergy(sweepSpeedFinished,0,Data::SweepTargetRegion)-InkEnergy(sweepFixedFinished,0,Data::SweepTargetRegion)
		>InkEnergy(sweepStart,0,Data::SweepTargetRegion)*0.01,"fixed eraser continues meaningful patching after the speed side has lifted");
	advanceTo(Data::FramesPerScene+Data::ResultFrame);
	if (!Readback(checks,device.Get(),context.Get(),presentation,sweepResult)) return checks.failures;
	Save(checks,factory.Get(),output/L"sweep-result.png",sweepResult); CheckComposite(checks,sweepResult,atlases[1],1,Data::ResultFrame,false);
	checks.Expect(RemovedFraction(sweepStart,sweepResult,1,Data::SweepTargetRegion)>0.995,"speed eraser fully clears the large candidate structure");
	checks.Expect(RemovedFraction(sweepStart,sweepResult,0,Data::SweepTargetRegion)<0.95,"fixed side retains meaningful missed ink after its longer patching sequence");
	checks.Expect(RemovedFraction(sweepStart,sweepResult,0,Data::SweepCornerRegion)<0.9,"fixed eraser leaves visible corner fragments despite extra patching");
	checks.Expect(RemovedFraction(sweepStart,sweepResult,1,Data::SweepProtectedRegion)<0.005,"large eraser preserves the protected explanation above");
	advanceTo(Data::FramesPerScene*2);
	if (Readback(checks,device.Get(),context.Get(),presentation,loop))
		checks.Expect(loop.pixels==fineStart.pixels && presentation.scene==0,"full loop restores identical original ink with no accumulated erasure");
	presentation=prepare(0,false);
	if (Readback(checks,device.Get(),context.Get(),presentation,loop))
		checks.Expect(loop.pixels==fineResult.pixels && presentation.opacity==1,"disabled animation displays the meaningful final result");
	auto baseline=player.Stats(); const auto disabledRevision=player.Revision();
	for (int i=0;i<500;++i) prepare(Step,false,10);
	checks.Expect(player.Stats().renders==baseline.renders && player.Revision()==disabledRevision,"disabled animation causes no repeated output rendering");
	presentation=prepare(0,false,1,true);
	if (Readback(checks,device.Get(),context.Get(),presentation,darkResult))
	{
		Save(checks,factory.Get(),output/L"fine-dark-result.png",darkResult); CheckComposite(checks,darkResult,atlases[0],0,Data::ResultFrame,true);
	}
	checks.Expect(player.Stats().shaderBuilds==1 && player.Stats().targetAllocations==1,"theme and scene changes reuse the pipeline and target");
	presentation=prepare(0,true,0); baseline=player.Stats();
	for (int i=0;i<200;++i) prepare(Step,true,0);
	checks.Expect(player.Stats().renders==baseline.renders && player.Stats().frame==baseline.frame,"zero playback rate pauses without extra GPU work");
	for (int i=0;i<100;++i) prepare(60,true,1,false,false);
	prepare(60);
	checks.Expect(player.Stats().renders==baseline.renders && player.Stats().frame==baseline.frame,"offpage time is not accumulated on resume");
	prepare(); checks.Expect(player.Stats().frame==baseline.frame+1,"visible playback resumes by one normal frame");
	player.Reset(); presentation=prepare(0); baseline=player.Stats();
	const auto timingBegin=Clock::now();
	for (int i=0;i<240;++i) prepare(1.0F/240.0F,true,10);
	ComPtr<ID3D11Query> completed;
	D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};
	if (checks.Result(device->CreateQuery(&query,completed.GetAddressOf()),"create no-window completion query"))
	{
		context->End(completed.Get()); context->Flush();
		const auto deadline=Clock::now()+std::chrono::seconds(5);
		HRESULT status=S_FALSE;
		while ((status=context->GetData(completed.Get(),nullptr,0,0))==S_FALSE && Clock::now()<deadline) Sleep(1);
		checks.Expect(status==S_OK,"bounded WARP playback completes without a GUI message loop");
	}
	const auto elapsed=std::chrono::duration<double,std::milli>(Clock::now()-timingBegin).count();
	const auto movingRenders=player.Stats().renders-baseline.renders;
	checks.Expect(movingRenders>0 && movingRenders<=30,"global 10x rate still submits at most 30 output updates per wallclock second");
	checks.Expect(player.Stats().shaderBuilds==baseline.shaderBuilds && player.Stats().targetAllocations==baseline.targetAllocations,"moving playback never rebuilds GPU resources");
	prepare(0,true,0,false,true,650); baseline=player.Stats();
	prepare(0,true,0,false,true,651);
	checks.Expect(player.Stats().targetAllocations==baseline.targetAllocations && player.Stats().width==1344,"nearby layout widths reuse a 32-pixel bucket");
	prepare(0,true,0,false,true,10000);
	checks.Expect(player.Stats().width==1536 && player.Stats().height==432,"extreme DPI/layout cannot exceed the bounded output size");
	ContextRestored(checks,device.Get(),context.Get(),player,images);
	const auto beforeReset=player.Revision(); baseline=player.Stats(); player.Reset();
	checks.Expect(player.Revision()>beforeReset && player.Stats().width==0,"reset invalidates the previous output revision");
	presentation=prepare(0);
	checks.Expect(player.Stats().shaderBuilds==baseline.shaderBuilds+1 && player.Stats().targetAllocations==baseline.targetAllocations+1,"explicit epoch reset recreates each owned resource once");
	if (Readback(checks,device.Get(),context.Get(),presentation,loop)) checks.Expect(loop.pixels==fineStart.pixels,"reset returns to the exact original scene");
	// 新设备只请求 10.0，顺便覆盖 SM4 播放管线，不能把这一项当成 Windows 7 实机验证。
	ComPtr<ID3D11Device> nextDevice; ComPtr<ID3D11DeviceContext> nextContext;
	const D3D_FEATURE_LEVEL oldFeature[]{D3D_FEATURE_LEVEL_10_0};
	if (checks.Result(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
		oldFeature,1,D3D11_SDK_VERSION,nextDevice.GetAddressOf(),nullptr,nextContext.GetAddressOf()),"create replacement SM4-compatible device epoch"))
	{
		std::array<Atlas,2> nextAtlases;
		for (unsigned i=0;i<2;++i) { nextAtlases[i].source=atlases[i].source; Upload(checks,nextDevice.Get(),nextAtlases[i]); }
		const std::array<ID3D11ShaderResourceView*,2> nextImages{nextAtlases[0].view.Get(),nextAtlases[1].view.Get()};
		baseline=player.Stats();
		presentation=player.Prepare(nextDevice.Get(),nextContext.Get(),nextImages,true,768,0,true,1,false);
		checks.Expect(player.Stats().shaderBuilds==baseline.shaderBuilds+1 && player.Stats().targetAllocations==baseline.targetAllocations+1,
			"device identity change discards old GPU resources and rebuilds exactly once");
		if (Readback(checks,nextDevice.Get(),nextContext.Get(),presentation,loop))
			checks.Expect(loop.pixels==fineStart.pixels,"replacement device restores identical original content");
		nextContext->ClearState(); nextContext->Flush();
	}
	std::cout << "[EraserDemo] WARP: " << movingRenders << " renders, " << elapsed << " ms including completion; max output 1536x432; "
		<< "exports=" << output.string() << "; failures=" << checks.failures << '\n';
	context->ClearState(); context->Flush(); return checks.failures;
}
