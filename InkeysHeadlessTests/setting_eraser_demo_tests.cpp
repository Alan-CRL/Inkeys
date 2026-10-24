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

	void CheckComposite(Checks& checks, const Image& image, const Atlas& atlas, int frameIndex, bool dark)
	{
		if (!checks.Expect(image.width==CanvasWidth*2 && image.height==CanvasHeight,"pixel audit uses full native source resolution")) return;
		const auto& frame=Data::Frames[0][frameIndex];
		const std::array<double,3> background=dark ? std::array<double,3>{18,59,50} : std::array<double,3>{247,248,250};
		size_t mismatch=0, opaque=0, checked=0;
		for (unsigned side=0;side<2;++side)
			for (unsigned y=0;y<CanvasHeight;++y)
				for (unsigned x=0;x<CanvasWidth;++x)
				{
					const auto* output=image.At(x+side*CanvasWidth,y);
					opaque+=output[3]!=255;
					const double radius=(side ? frame.speedDiameter : frame.fixedDiameter)*0.5+2;
					if (frame.cursorAlpha>0 && std::abs(x+0.5-frame.x)<radius && std::abs(y+0.5-frame.y)<radius) continue;
					const auto* ink=atlas.source.At(x,y); const auto* mask=atlas.source.At(x+(side+1)*CanvasWidth,y);
					double erased=0;
					if (frame.eraseProgress>=1) erased=mask[2]/255.0;
					else if (frame.eraseProgress>0) erased=mask[2]/255.0*std::clamp(
						(static_cast<double>(frame.eraseProgress)*255-mask[0])/std::max(1,static_cast<int>(mask[1])-mask[0]),0.0,1.0);
					std::array<double,3> color{static_cast<double>(ink[0]),static_cast<double>(ink[1]),static_cast<double>(ink[2])};
					if (dark) color=ink[2]>ink[0]+0.15*255 ? std::array<double,3>{111,200,236}
						: (ink[0]>ink[2]+0.25*255 ? std::array<double,3>{242,192,104} : std::array<double,3>{234,242,239});
					bool bad=false;
					for (unsigned channel=0;channel<3;++channel)
					{
						const double expected=background[channel]+(color[channel]-background[channel])*ink[3]/255.0*(1-erased);
						bad|=std::abs(output[channel]-expected)>2.1;
					}
					mismatch+=bad; ++checked;
				}
		checks.Expect(opaque==0,"composite always preserves an opaque board including erased pixels");
		if (mismatch) std::cerr << "[EraserDemo] pixel mismatches=" << mismatch << '/' << checked << '\n';
		checks.Expect(mismatch==0,"GPU erasure and palette match the baked atlas at every non-cursor pixel");
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
		const D3D11_VIEWPORT viewport{1,2,6,4,0.25F,0.75F};
		context->OMSetRenderTargets(1,view.GetAddressOf(),nullptr); context->RSSetViewports(1,&viewport);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
		context->PSSetShaderResources(0,1,&images[1]); context->PSSetSamplers(0,1,sampler.GetAddressOf());
		context->PSSetConstantBuffers(0,1,constants.GetAddressOf());
		const auto result=player.Prepare(device,context,images,true,768,0,false,1,true);
		checks.Expect(result.texture!=0,"offscreen rendering succeeds with existing host state");
		ComPtr<ID3D11RenderTargetView> actualView; ComPtr<ID3D11ShaderResourceView> actualImage;
		ComPtr<ID3D11SamplerState> actualSampler; ComPtr<ID3D11Buffer> actualConstants;
		ComPtr<ID3D11PixelShader> actualPixel; ComPtr<ID3D11VertexShader> actualVertex;
		context->OMGetRenderTargets(1,actualView.GetAddressOf(),nullptr);
		context->PSGetShaderResources(0,1,actualImage.GetAddressOf()); context->PSGetSamplers(0,1,actualSampler.GetAddressOf());
		context->PSGetConstantBuffers(0,1,actualConstants.GetAddressOf());
		context->PSGetShader(actualPixel.GetAddressOf(),nullptr,nullptr); context->VSGetShader(actualVertex.GetAddressOf(),nullptr,nullptr);
		D3D11_VIEWPORT actualViewport{}; UINT count=1; context->RSGetViewports(&count,&actualViewport);
		D3D11_PRIMITIVE_TOPOLOGY topology{}; context->IAGetPrimitiveTopology(&topology);
		const bool restored=checks.Expect(actualView.Get()==view.Get() && actualImage.Get()==images[1]
			&& actualSampler.Get()==sampler.Get() && actualConstants.Get()==constants.Get()
			&& !actualPixel && !actualVertex && count==1 && std::memcmp(&viewport,&actualViewport,sizeof(viewport))==0
			&& topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST,"shared context targets, shaders, resources, viewport and topology are restored");
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
	Image fineStart, fineActive, fineResult, sweepStart, sweepActive, sweepResult, darkResult, loop;
	if (!Readback(checks,device.Get(),context.Get(),presentation,fineStart)) return checks.failures;
	Save(checks,factory.Get(),output/L"fine-start.png",fineStart); CheckComposite(checks,fineStart,atlases[0],0,false);
	const auto initialRevision=player.Revision();
	for (int i=0;i<200;++i) prepare(0);
	checks.Expect(player.Revision()==initialRevision && player.Stats().renders==1,"unchanged frame performs no GPU work");
	for (int i=1;i<=75;++i) presentation=prepare();
	if (Readback(checks,device.Get(),context.Get(),presentation,fineActive))
	{
		Save(checks,factory.Get(),output/L"fine-active.png",fineActive);
		CheckComposite(checks,fineActive,atlases[0],75,false);
	}
	for (int i=76;i<=226;++i) presentation=prepare();
	if (!Readback(checks,device.Get(),context.Get(),presentation,fineResult)) return checks.failures;
	Save(checks,factory.Get(),output/L"fine-result.png",fineResult); CheckComposite(checks,fineResult,atlases[0],226,false);
	checks.Expect(RemovedFraction(fineStart,fineResult,1,{541,304,557,333})>0.98,"speed eraser removes the intended fine subscript");
	checks.Expect(RemovedFraction(fineStart,fineResult,1,{558,280,594,330})<0.005,"speed eraser preserves neighboring O");
	checks.Expect(RemovedFraction(fineStart,fineResult,0,{558,280,594,330})>0.15,"fixed eraser demonstrates collateral erasure of neighboring O");
	for (int i=227;i<=240;++i) presentation=prepare();
	checks.Expect(presentation.scene==1 && player.Stats().frame==0,"carousel switches to the second baked scene");
	if (!Readback(checks,device.Get(),context.Get(),presentation,sweepStart)) return checks.failures;
	Save(checks,factory.Get(),output/L"sweep-start.png",sweepStart);
	for (int i=1;i<=110;++i) presentation=prepare();
	if (Readback(checks,device.Get(),context.Get(),presentation,sweepActive)) Save(checks,factory.Get(),output/L"sweep-active.png",sweepActive);
	for (int i=111;i<=226;++i) presentation=prepare();
	if (!Readback(checks,device.Get(),context.Get(),presentation,sweepResult)) return checks.failures;
	Save(checks,factory.Get(),output/L"sweep-result.png",sweepResult); CheckComposite(checks,sweepResult,atlases[1],226,false);
	checks.Expect(RemovedFraction(sweepStart,sweepResult,1,{320,270,610,405})>0.995,"speed eraser fully clears the large candidate structure");
	checks.Expect(RemovedFraction(sweepStart,sweepResult,0,{320,270,610,405})<0.3,"fixed eraser leaves substantial candidate structure fragments");
	checks.Expect(RemovedFraction(sweepStart,sweepResult,1,{0,0,768,250})<0.005,"large eraser preserves the protected explanation above");
	for (int i=227;i<=240;++i) presentation=prepare();
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
		Save(checks,factory.Get(),output/L"fine-dark-result.png",darkResult); CheckComposite(checks,darkResult,atlases[0],226,true);
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
