#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include "setting_font_benchmark_tests.h"
#include "setting_home_warp_tests.h"
#include "../Inkeys/Inkeys/UI/Setting/Setting.Typography.h"
#include "imgui/imgui_internal.h"
#include "imgui/imgui_impl_dx11.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <unordered_map>
#include <vector>

namespace
{
	using Microsoft::WRL::ComPtr;
	namespace Design = Inkeys::UI::Setting::Design;
	using Clock = std::chrono::steady_clock;
	struct Meter
	{
		std::unordered_map<void*, size_t> sizes;
		size_t live = 0, peak = 0;
		void Add(void* p, size_t bytes) { if (p) { sizes.emplace(p, bytes); live += bytes; peak = (std::max)(peak, live); } }
		void Remove(void* p) { auto it = sizes.find(p); if (it != sizes.end()) { live -= it->second; sizes.erase(it); } }
	};
	struct Allocators
	{
		ImGuiMemAllocFunc oldAlloc{}; ImGuiMemFreeFunc oldFree{}; void* oldUser{};
		Meter imgui, freetype;
		Allocators()
		{
			ImGui::GetAllocatorFunctions(&oldAlloc, &oldFree, &oldUser);
			ImGui::SetAllocatorFunctions([](size_t n, void* u) -> void* { auto& a = *static_cast<Allocators*>(u); void* p = a.oldAlloc(n, a.oldUser); a.imgui.Add(p, n); return p; },
				[](void* p, void* u) { auto& a = *static_cast<Allocators*>(u); a.imgui.Remove(p); a.oldFree(p, a.oldUser); }, this);
			// FreeType独立统计，不再经ImGui allocator，避免重复计数。
			ImGuiFreeType::SetAllocatorFunctions([](size_t n, void* u) -> void* { void* p = std::malloc(n); static_cast<Meter*>(u)->Add(p,n); return p; },
				[](void* p, void* u) { static_cast<Meter*>(u)->Remove(p); std::free(p); }, &freetype);
		}
		~Allocators()
		{
			ImGuiFreeType::SetAllocatorFunctions([](size_t n, void*) -> void* { return ImGui::MemAlloc(n); }, [](void* p, void*) { ImGui::MemFree(p); }, nullptr);
			ImGui::SetAllocatorFunctions(oldAlloc, oldFree, oldUser);
		}
	};
	struct Source { std::vector<char> bytes; };
	using Sources = std::array<Source,8>;
	const char* sourceNames[] = { "HarmonyOS_Sans_SC_Regular.ttf", "HarmonyOS_SansSC_Bold.ttf", "HarmonyOS_Sans_TC_Regular.ttf", "HarmonyOS_SansTC_Bold.ttf", "NotoSansJP-Regular.ttf", "NotoSansJP-Bold.ttf", "NotoSansKR-Regular.ttf", "NotoSansKR-Bold.ttf" };
	struct Locale { const char* name; int primary; const char* text; };
	const Locale locales[] = {
		{"zh-CN",0,"设置 选择与穿透 操作模式 字体显示 清晰度"}, {"en-US",0,"Settings Selection and click-through Typography readability"},
		{"zh-TW",2,"設定 選取與穿透 操作模式 字體顯示 清晰度"}, {"zh-HK",2,"設定 選取及穿透 操作模式 字體顯示 清晰度"},
		{"ja-JP",4,"設定 言語を選択 インターフェイスの表示言語を設定します。"}, {"ko-KR",6,"설정 표시 언어를 선택하세요 화면에 표시되는 텍스트입니다."},
		{"fr-FR",0,"Paramètres Sélection et clics traversants Écran français"}, {"de-DE",0,"Einstellungen Auswahl und Durchklicken Größe überprüfen"},
		{"es-ES",0,"Configuración Selección y clics a través Tamaño de lápiz"}, {"pt-BR",0,"Configurações Seleção e passagem de cliques Anotações"},
		{"ru-RU",0,"Настройки Выбор языка Размер пера Настройки интерфейса"},
		{"id-ID",0,"Pengaturan Pilih bahasa Ukuran pena"},
		{"vi-VN",0,"Cài đặt Tiếng Việt Đđ Ăă Ââ Êê Ôô Ơơ Ưư ẮằẶẤầẬẾềỆỐồỘỚờỢỨừỰ"},
		{"tr-TR",0,"Ayarlar Türkçe Ğğ İı Şş Öö Üü"},
		{"it-IT",0,"Impostazioni Italiano ÀÈÉÌÒÙ àèéìòù"},
		{"pl-PL",0,"Ustawienia Polski ĄąĆćĘęŁłŃńÓóŚśŹźŻż"},
		{"uk-UA",0,"Налаштування Українська ҐґЄєІіЇї"},
		{"es-MX",0,"Configuración Español México Tamaño de pluma"},
		{"ro-RO",0,"Setări Română ĂăÂâÎîȘșȚț"},
		{"en-GB",0,"Settings Colour Centred Customise £"}
	};
	// 不调用生产配置构造旧基线，避免候选改动让A/B同时改变。
	ImFontConfig Baseline(float scale, bool noto, bool design, bool primaryNoto)
	{
		ImFontConfig c; c.FontDataOwnedByAtlas = false; c.OversampleH = c.OversampleV = 1; c.RasterizerDensity = scale;
		c.ExtraSizeScale = noto ? (design ? 1.448F*.97F : 1.448F/1.213F) : (design ? 1.213F*.97F : 1.0F);
		c.GlyphOffset.y = 30.0F*((design ? .025F : 0.0F) - (primaryNoto ? 1160.0F/1448.0F - 928.0F/1213.0F : 0.0F));
		return c;
	}
	using Faces = std::array<ImFont*,4>;
	Faces Load(Sources& sources, int primary, float scale, bool candidate)
	{
		auto* atlas = ImGui::GetIO().Fonts;
		if (candidate) { Design::ConfigureFreeTypeAtlas(*atlas);char snap[4]{},trial[4]{};GetEnvironmentVariableA("INKEYS_FONT_SNAP_BASELINE",snap,4);GetEnvironmentVariableA("INKEYS_FONT_GRID_TRIAL",trial,4);if(trial[0]=='1' && snap[0]!='1')atlas->FontLoaderFlags&=~ImGuiFreeTypeLoaderFlags_SnapAscentToPixelGrid;else if(snap[0]=='1')atlas->FontLoaderFlags|=ImGuiFreeTypeLoaderFlags_SnapAscentToPixelGrid; }
		else atlas->SetFontLoader(ImFontAtlasGetFontLoaderForStbTruetype());
		Faces faces{};
		for (int role=0; role<4; ++role)
		{
			const int weight=role%2; const bool design=role>=2;
			const int first=primary+weight;
			auto add=[&](int source, bool merge) {
				auto c=candidate ? Design::FreeTypeTextFontConfig(source>=4,design?Design::TextFontRole::Design:Design::TextFontRole::Legacy,primary>=4)
					: Baseline(scale,source>=4,design,primary>=4);
				c.MergeMode=merge;
				return atlas->AddFontFromMemoryTTF(sources[source].bytes.data(),static_cast<int>(sources[source].bytes.size()),30,&c);
			};
			faces[role]=add(first,false);
			for (int source=weight; source<8; source+=2) if (source!=first) add(source,true);
		}
		ImGui::GetIO().FontDefault=faces[0];
		return faces;
	}
	int CheckPixelBaselineContract(Sources& sources)
	{
		int failures=0,compared=0;float largestShift=0;
		for(int primary:{0,2,4,6})
		{
			auto* previous=ImGui::GetCurrentContext();auto* context=ImGui::CreateContext();ImGui::SetCurrentContext(context);
			auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1600,900};io.DeltaTime=1.0F/60;
			io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;io.Fonts->RendererHasTextures=true;
			Design::ConfigureFreeTypeAtlas(*io.Fonts);io.Fonts->FontLoaderFlags&=~ImGuiFreeTypeLoaderFlags_SnapAscentToPixelGrid;
			ImFont* faces[2][4]{};
			for(int variant=0;variant<2;++variant)for(int role=0;role<4;++role)
			{
				const int weight=role%2;
				auto add=[&](int source,bool merge){
					auto config=Design::FreeTypeTextFontConfig(source>=4,role>=2?Design::TextFontRole::Design:Design::TextFontRole::Legacy,primary>=4);
					if(variant)config.FontLoaderFlags|=ImGuiFreeTypeLoaderFlags_SnapAscentToPixelGrid;
					config.MergeMode=merge;
					return io.Fonts->AddFontFromMemoryTTF(sources[source].bytes.data(),static_cast<int>(sources[source].bytes.size()),30,&config);
				};
				faces[variant][role]=add(primary+weight,false);
				for(int source=weight;source<8;source+=2)if(source!=primary+weight)add(source,true);
			}
			io.FontDefault=faces[0][0];ImGui::NewFrame();
			for(int role=0;role<4;++role)for(float scale:{1.0F,1.875F})for(float size:{12.0F,13.0F,14.0F,28.0F})
			{
				auto* plain=faces[0][role]->GetFontBaked(size*scale);
				auto* snapped=faces[1][role]->GetFontBaked(size*scale);
				for(ImWchar point:{ImWchar('H'),ImWchar('g'),ImWchar('p'),ImWchar('q'),ImWchar('y'),ImWchar(0x4e2d),ImWchar(0xd55c)})
				{
					const auto* oldPointer=plain->FindGlyphNoFallback(point);if(!oldPointer){++failures;continue;}const ImFontGlyph old=*oldPointer;
					const auto* newPointer=snapped->FindGlyphNoFallback(point);if(!newPointer){++failures;continue;}const ImFontGlyph next=*newPointer;
					const float shift=next.Y0-old.Y0;largestShift=(std::max)(largestShift,std::abs(shift));++compared;
					if(std::abs(next.AdvanceX-old.AdvanceX)>0.0001F || next.X0!=old.X0 || next.X1!=old.X1
						|| std::abs((next.Y1-next.Y0)-(old.Y1-old.Y0))>0.0001F || std::abs(next.Y1-old.Y1-shift)>0.0001F
						|| std::abs(shift)>.5001F/snapped->RasterizerDensity)++failures;
					const auto* oldRect=ImFontAtlasPackGetRect(io.Fonts,old.PackId);const auto* newRect=ImFontAtlasPackGetRect(io.Fonts,next.PackId);
					if(!oldRect || !newRect || oldRect->w!=newRect->w || oldRect->h!=newRect->h){++failures;continue;}
					for(int y=0;y<oldRect->h;++y)
						if(std::memcmp(io.Fonts->TexData->GetPixelsAt(oldRect->x,oldRect->y+y),io.Fonts->TexData->GetPixelsAt(newRect->x,newRect->y+y),oldRect->w*io.Fonts->TexData->BytesPerPixel)!=0){++failures;break;}
				}
			}
			if(ImGuiFreeType::HasFontLoadFailure(io.Fonts))++failures;
			ImGui::EndFrame();ImGui::DestroyContext(context);ImGui::SetCurrentContext(previous);
		}
		std::cout<<"[FontGrid] paired-glyphs="<<compared<<" max-baseline-shift="<<largestShift<<" failures="<<failures<<'\n';
		return failures;
	}

	void Draw(const Locale& locale, const Faces& faces, float scale, bool dark)
	{
		ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
		ImGui::Begin("##font-benchmark",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoMove);
		const ImU32 color=dark ? IM_COL32(235,235,235,255) : IM_COL32(25,25,25,255);
		float y=20*scale;
		for(int role=0;role<4;++role) for(float size : {12.0F,13.0F,14.0F,28.0F})
		{
			ImGui::PushFont(faces[role],size);
			const std::string text=std::string(locale.text)+" 0123456789 Agjpqy";
			ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(),ImGui::GetFontSize(),{20*scale,y},color,text.c_str());
			ImGui::PopFont(); y+=(size+10)*scale;
		}
		ImGui::PushFont(faces[2],14);
		ImGui::GetWindowDrawList()->AddText({20*scale,y},color,"简体中文 繁體中文 日本語 한국어 Français Deutsch Español Português Русский");
		ImGui::PopFont(); ImGui::End();
	}
	struct Renderer
	{
		ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> dc;
		ComPtr<ID3D11Texture2D> target; ComPtr<ID3D11RenderTargetView> view; ComPtr<ID3D11Query> query;
		bool backend=false;
		double backendUs=0,waitUs=0;
		bool Init(float scale)
		{
			const D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0;
			if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,&level,1,D3D11_SDK_VERSION,&device,nullptr,&dc))) return false;
			D3D11_TEXTURE2D_DESC d{}; d.Width=static_cast<UINT>(1000*scale); d.Height=static_cast<UINT>(700*scale); d.MipLevels=d.ArraySize=1;
			d.Format=DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count=1; d.BindFlags=D3D11_BIND_RENDER_TARGET;
			D3D11_QUERY_DESC q{D3D11_QUERY_EVENT,0};
			if(FAILED(device->CreateTexture2D(&d,nullptr,&target)) || FAILED(device->CreateRenderTargetView(target.Get(),nullptr,&view)) || FAILED(device->CreateQuery(&q,&query))) return false;
			backend=ImGui_ImplDX11_Init(device.Get(),dc.Get()); return backend && ImGui_ImplDX11_CreateDeviceObjects();
		}
		bool Submit(bool dark)
		{
			const auto begin=Clock::now();
			float color[4]={dark?.08F:.95F,dark?.08F:.95F,dark?.08F:.95F,1};
			dc->OMSetRenderTargets(1,view.GetAddressOf(),nullptr); dc->ClearRenderTargetView(view.Get(),color);
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
			if(FAILED(ImGui_ImplDX11_GetLastResult())) return false;
			const auto submitted=Clock::now();backendUs=std::chrono::duration<double,std::micro>(submitted-begin).count();
			dc->End(query.Get()); dc->Flush(); const auto deadline=Clock::now()+std::chrono::seconds(5);
			HRESULT hr; while((hr=dc->GetData(query.Get(),nullptr,0,0))==S_FALSE) if(Clock::now()>deadline) return false;
			waitUs=std::chrono::duration<double,std::micro>(Clock::now()-submitted).count();
			return SUCCEEDED(hr);
		}
		~Renderer() { if(backend) ImGui_ImplDX11_Shutdown(); if(dc) dc->ClearState(); }
	};
	size_t GpuBytes()
	{
		size_t bytes=0;
		for(auto* texture:ImGui::GetIO().Fonts->TexList) if(texture->TexID!=ImTextureID_Invalid)
		{
			auto* srv=reinterpret_cast<ID3D11ShaderResourceView*>(texture->TexID);
			ComPtr<ID3D11Resource> r; srv->GetResource(&r); ComPtr<ID3D11Texture2D> t;
			if(SUCCEEDED(r.As(&t))) { D3D11_TEXTURE2D_DESC d{}; t->GetDesc(&d); bytes+=size_t(d.Width)*d.Height*(d.Format==DXGI_FORMAT_R8_UNORM?1:4); }
		}
		return bytes;
	}
	double Percentile(std::vector<double> values, double p) { std::sort(values.begin(),values.end()); return values[static_cast<size_t>((values.size()-1)*p)]; }
	struct Result { size_t steady=0, imgui=0, ft=0, atlas=0, gpu=0, peak=0, imguiPeak=0, ftPeak=0; bool snapped=false; double median=0,p95=0,maximum=0,setupUs=0,coldFrameUs=0,cpuMedian=0,cpuP95=0,submitMedian=0,submitP95=0,backendMedian=0,backendP95=0,waitMedian=0,waitP95=0; };
	Result Run(Sources& sources, const Locale& locale, float scale, bool candidate, bool dark, bool timed, const std::filesystem::path& output, int& failures)
	{
		Result result;
		auto* previous=ImGui::GetCurrentContext(); Allocators memory;
		auto* measuredContext=ImGui::CreateContext(); ImGui::SetCurrentContext(measuredContext); auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.DisplaySize={1000*scale,700*scale}; io.DeltaTime=1.0F/60;
		ImGui::GetStyle().FontScaleDpi=scale;
		if(dark) ImGui::StyleColorsDark(); else ImGui::StyleColorsLight();
		io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures; io.Fonts->RendererHasTextures=true;
		const auto setupStart=Clock::now();
		const auto faces=Load(sources,locale.primary,scale,candidate);
		if(!io.Fonts->Build()) ++failures;
		result.snapped=candidate && (io.Fonts->FontLoaderFlags & ImGuiFreeTypeLoaderFlags_SnapAscentToPixelGrid)!=0;
		result.setupUs=std::chrono::duration<double,std::micro>(Clock::now()-setupStart).count();
		const std::string name=std::string(locale.name)+"-"+std::to_string(static_cast<int>(scale*100))+"-"+(candidate?"freetype":"stb")+(dark?"-dark":"-light");
		{
			Renderer renderer;
			if(!renderer.Init(scale)) { ++failures; }
			else
			{
				std::vector<double> times,cpuTimes,submitTimes,backendTimes,waitTimes;
				const int count=timed?1260:4;
				for(int frame=0;frame<count;++frame)
				{
					const auto start=Clock::now(); ImGui_ImplDX11_NewFrame(); ImGui::NewFrame(); Draw(locale,faces,scale,dark); ImGui::Render();
					const auto generated=Clock::now();
					if(ImGuiFreeType::HasFontLoadFailure(io.Fonts) || !renderer.Submit(dark)) { ++failures; break; }
					const auto completed=Clock::now();
					const double us=std::chrono::duration<double,std::micro>(completed-start).count();
					if(frame==0) result.coldFrameUs=us;
					// 三个独立采样区块，每组先预热120帧再取300帧；不重启context。
					if(!timed || frame%420>=120) { times.push_back(us);cpuTimes.push_back(std::chrono::duration<double,std::micro>(generated-start).count());submitTimes.push_back(std::chrono::duration<double,std::micro>(completed-generated).count());backendTimes.push_back(renderer.backendUs);waitTimes.push_back(renderer.waitUs); }
					result.gpu=GpuBytes(); result.peak=(std::max)(result.peak,memory.imgui.live+memory.freetype.live+result.gpu);
				}
				result.imguiPeak=memory.imgui.peak; result.ftPeak=memory.freetype.peak; result.imgui=memory.imgui.live; result.ft=memory.freetype.live; result.steady=result.imgui+result.ft+result.gpu;
				for(auto* texture:io.Fonts->TexList) if(texture->Pixels) result.atlas+=size_t(texture->Width)*texture->Height*texture->BytesPerPixel;
				if(!times.empty()) { result.median=Percentile(times,.5); result.p95=Percentile(times,.95); result.maximum=*std::max_element(times.begin(),times.end()); result.cpuMedian=Percentile(cpuTimes,.5);result.cpuP95=Percentile(cpuTimes,.95);result.submitMedian=Percentile(submitTimes,.5);result.submitP95=Percentile(submitTimes,.95);result.backendMedian=Percentile(backendTimes,.5);result.backendP95=Percentile(backendTimes,.95);result.waitMedian=Percentile(waitTimes,.5);result.waitP95=Percentile(waitTimes,.95); }
			}
		}
		// 像素读回放在计时之后，避免截图预热污染冷首帧。
		failures+=SaveSettingFontComparison(output/(name+".png"),scale,[&]{Draw(locale,faces,scale,dark);});
		ImGui::DestroyContext(measuredContext); ImGui::SetCurrentContext(previous);
		if(memory.imgui.live || memory.freetype.live) { ++failures; std::cerr<<"[FontBenchmark] unreleased allocations "<<name<<" imgui="<<memory.imgui.live<<" ft="<<memory.freetype.live<<'\n'; }
		return result;
	}
}

int RunSettingFontBenchmarkTests()
{
	int failures=0;
	auto root=std::filesystem::current_path();
	while(!std::filesystem::exists(root/"Inkeys/src/ttf") && root.has_parent_path() && root!=root.parent_path()) root=root.parent_path();
	Sources sources; size_t sourceBytes=0;
	for(size_t i=0;i<sources.size();++i) { std::ifstream file(root/"Inkeys/src/ttf"/sourceNames[i],std::ios::binary); sources[i].bytes.assign(std::istreambuf_iterator<char>(file),{}); sourceBytes+=sources[i].bytes.size(); if(sources[i].bytes.empty()) return 1; }
	failures+=CheckPixelBaselineContract(sources);
	char grid[4]{},snap[4]{};GetEnvironmentVariableA("INKEYS_FONT_GRID_TRIAL",grid,4);GetEnvironmentVariableA("INKEYS_FONT_SNAP_BASELINE",snap,4);
	const bool gridTrial=grid[0]=='1';
	const auto output=root/(gridTrial ? (snap[0]=='1'?"Build/setting-fonts/grid-snap":"Build/setting-fonts/grid-original") : "Build/setting-fonts/benchmark"); std::filesystem::create_directories(output);
	std::ofstream csv(output/"metrics.csv");
	csv<<"locale,scale,dark,loader,shared_source_bytes,imgui_live_including_cpu_atlas,ft_live,cpu_atlas_subset,gpu_atlas,total_excluding_shared_source,observed_frame_peak,imgui_allocation_peak,ft_allocation_peak,median_us,p95_us,max_us,timed,cold_font_setup_us,cold_first_frame_us,warm_cpu_median_us,warm_cpu_p95_us,submit_complete_median_us,submit_complete_p95_us,pixel_baseline_snap,backend_median_us,backend_p95_us,completion_wait_median_us,completion_wait_p95_us,order_pass,sampling_order\n";
	for(const auto& locale:locales) for(float scale:{1.0F,1.25F,1.5F,1.875F,2.0F,4.0F})
	{
		if(gridTrial && (std::string(locale.name)!="zh-CN" && std::string(locale.name)!="ja-JP" && std::string(locale.name)!="ko-KR")) continue;
		if(gridTrial && scale!=1.0F && scale!=1.875F) continue;
		char filter[32]{}; GetEnvironmentVariableA("INKEYS_FONT_BENCHMARK_LOCALE",filter,32);
		if(filter[0] && std::string(filter)!=locale.name) continue;
		char scaleFilter[16]{};GetEnvironmentVariableA("INKEYS_FONT_BENCHMARK_SCALE",scaleFilter,16);
		if(scaleFilter[0] && std::to_string(static_cast<int>(scale*100))!=scaleFilter) continue;
		if(scale==4 && locale.primary==0 && std::string(locale.name)!="zh-CN" && std::string(locale.name)!="en-US") continue;
		for(bool dark:{true,false})
		{
			// 长时间吞吐只取代表字族；其他组合仍真实渲染、计量和读回。
			const bool timed=!gridTrial && dark && (scale==1 || scale==2) && (std::string(locale.name)=="zh-CN" || std::string(locale.name)=="en-US" || std::string(locale.name)=="ja-JP" || std::string(locale.name)=="ko-KR");
			Result baseline;
			char order[8]{};GetEnvironmentVariableA("INKEYS_FONT_BENCHMARK_ORDER",order,8);const bool abba=std::string(order)=="ABBA";
			for(int pass=0;pass<(abba?4:2);++pass)
			{
				const bool candidate=abba?(pass==1 || pass==2):(pass==1);
				const auto r=Run(sources,locale,scale,candidate,dark,timed,output,failures);
				csv<<locale.name<<','<<scale<<','<<dark<<','<<(candidate?"freetype":"stb")<<','<<sourceBytes<<','<<r.imgui<<','<<r.ft<<','<<r.atlas<<','<<r.gpu<<','<<r.steady<<','<<r.peak<<','<<r.imguiPeak<<','<<r.ftPeak<<','<<r.median<<','<<r.p95<<','<<r.maximum<<','<<timed<<','<<r.setupUs<<','<<r.coldFrameUs<<','<<r.cpuMedian<<','<<r.cpuP95<<','<<r.submitMedian<<','<<r.submitP95<<','<<r.snapped<<','<<r.backendMedian<<','<<r.backendP95<<','<<r.waitMedian<<','<<r.waitP95<<','<<pass<<','<<(abba?"ABBA":"AB")<<'\n'; csv.flush();
				if(!candidate) baseline=r;
				else if(r.steady>baseline.steady) { ++failures; std::cerr<<"[FontBenchmark] BUDGET EXCEEDED "<<locale.name<<" scale="<<scale<<" old="<<baseline.steady<<" new="<<r.steady<<'\n'; }
			}
		}
	}
	std::cout<<"[FontBenchmark] metrics="<<output/"metrics.csv"<<" failures="<<failures<<'\n';
	return failures;
}
