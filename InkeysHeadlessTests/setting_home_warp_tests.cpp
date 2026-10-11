#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include "setting_home_warp_tests.h"
#include "../Inkeys/Inkeys/UI/Setting/Setting.Pages.h"
#include "../Inkeys/additional/imgui/imgui_internal.h"
#include "../Inkeys/additional/imgui/imgui_impl_dx11.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <fstream>
#include <utility>
#include <vector>

namespace
{
	namespace Design = Inkeys::UI::Setting::Design;
	namespace Brand = Inkeys::UI::Setting::Design::HomeBrand;
	using Microsoft::WRL::ComPtr;
	using Clock = std::chrono::steady_clock;
	constexpr UINT Width = 1000, Height = 1100;
	const ImVec2 Away(-1000.0F, -1000.0F);
	using Pixels = std::vector<std::uint32_t>;
	Pixels inkReference, textReference;

	struct Checks
	{
		int failures = 0;
		bool Expect(bool condition, const char* message)
		{
			if (!condition) { ++failures; std::cerr << "[HomeWARP] failed: " << message << '\n'; }
			return condition;
		}
		bool Result(HRESULT result, const char* message)
		{
			if (SUCCEEDED(result)) return true;
			std::cerr << "[HomeWARP] HRESULT=0x" << std::hex << static_cast<unsigned long>(result) << std::dec << '\n';
			return Expect(false, message);
		}
	};

	struct AllocationScope
	{
		ImGuiMemAllocFunc allocate = nullptr;
		ImGuiMemFreeFunc release = nullptr;
		void* user = nullptr;
		std::uint64_t allocations = 0;
		AllocationScope()
		{
			ImGui::GetAllocatorFunctions(&allocate, &release, &user);
			ImGui::SetAllocatorFunctions([](size_t bytes, void* context) -> void* {
				auto& self = *static_cast<AllocationScope*>(context); ++self.allocations;
				return self.allocate(bytes, self.user);
			}, [](void* pointer, void* context) {
				auto& self = *static_cast<AllocationScope*>(context); self.release(pointer, self.user);
			}, this);
		}
		~AllocationScope() { ImGui::SetAllocatorFunctions(allocate, release, user); }
	};

	struct Offscreen
	{
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		ComPtr<ID3D11Texture2D> target, staging;
		ComPtr<ID3D11RenderTargetView> view;
		bool initialized = false;
		ImGuiBackendFlags oldBackendFlags = ImGui::GetIO().BackendFlags;
		ImVec2 oldDisplay = ImGui::GetIO().DisplaySize;
		ImVec2 oldFramebufferScale = ImGui::GetIO().DisplayFramebufferScale;
		float oldDelta = ImGui::GetIO().DeltaTime;
		ImGuiStyle oldStyle = ImGui::GetStyle();
		bool oldDark = Design::IsDarkMode();

		bool Initialize(Checks& checks)
		{
			// 显式限于 Win7 平台更新可用的 FL11.0；不请求 swap chain 或窗口。
			const D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_11_0;
			D3D_FEATURE_LEVEL actual{};
			if (!checks.Result(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
				D3D11_CREATE_DEVICE_BGRA_SUPPORT, &requested, 1, D3D11_SDK_VERSION,
				device.GetAddressOf(), &actual, context.GetAddressOf()), "create no-window WARP device")) return false;
			checks.Expect(actual == requested, "WARP uses explicitly requested FL11.0");
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = Width; desc.Height = Height; desc.MipLevels = desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_RENDER_TARGET;
			if (!checks.Result(device->CreateTexture2D(&desc, nullptr, target.GetAddressOf()), "create offscreen target")
				|| !checks.Result(device->CreateRenderTargetView(target.Get(), nullptr, view.GetAddressOf()), "create offscreen RTV")) return false;
			desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			if (!checks.Result(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()), "create CPU readback")) return false;
			initialized = ImGui_ImplDX11_Init(device.Get(), context.Get());
			if (!checks.Expect(initialized, "initialize production DX11 backend")) return false;
			return checks.Expect(ImGui_ImplDX11_CreateDeviceObjects(), "load exact production embedded CSO resources");
		}

		~Offscreen()
		{
			if (initialized) ImGui_ImplDX11_Shutdown();
			if (context) { context->ClearState(); context->Flush(); }
			auto& io = ImGui::GetIO(); io.BackendFlags = oldBackendFlags;
			io.DisplaySize = oldDisplay; io.DisplayFramebufferScale = oldFramebufferScale; io.DeltaTime = oldDelta;
			Design::ApplyPalette(oldDark); ImGui::GetStyle() = oldStyle;
			Design::ConfigureRowAnimations(0, false, 1);
			io.AddMousePosEvent(Away.x, Away.y);
		}

		bool Submit(Checks& checks, ImDrawData* data, Pixels* pixels = nullptr)
		{
			const float clear[4]{};
			ID3D11RenderTargetView* rtv = view.Get();
			context->OMSetRenderTargets(1, &rtv, nullptr);
			context->ClearRenderTargetView(rtv, clear);
			ImGui_ImplDX11_RenderDrawData(data);
			context->CopyResource(staging.Get(), target.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			// Map 等待这次真实绘制完成；报告含提交/执行/拷贝/等待的耗时，不冒充纯 GPU 时间。
			if (!checks.Result(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "complete WARP rendering and readback")) return false;
			if (pixels)
			{
				pixels->resize(static_cast<size_t>(Width) * Height);
				for (UINT row = 0; row < Height; ++row)
					std::memcpy(pixels->data() + static_cast<size_t>(row) * Width,
						static_cast<const char*>(mapped.pData) + static_cast<size_t>(row) * mapped.RowPitch, Width * 4);
			}
			context->Unmap(staging.Get(), 0);
			return true;
		}
	};

	struct FrameInfo { int vertices = 0, indices = 0, batches = 0; ImRect pageClip; };
	FrameInfo Frame(Design::HomeMotionState* motion, ImVec2 mouse, float delta = 1.0F / 60.0F,
		bool enabled = true, std::uint64_t serial = 1)
	{
		auto& io = ImGui::GetIO(); io.DisplaySize = { static_cast<float>(Width), static_cast<float>(Height) };
		io.DisplayFramebufferScale = { 1, 1 }; io.DeltaTime = 1.0F / 60.0F;
		io.AddMouseSourceEvent(ImGuiMouseSource_Mouse); io.AddMousePosEvent(mouse.x, mouse.y);
		io.AddMouseButtonEvent(0, false);
		ImGui_ImplDX11_NewFrame(); ImGui::NewFrame(); Design::ConfigureRowAnimations(0, false, 1);
		if (motion) Design::ConfigureHomeMotion(*motion, true, serial, delta, enabled, 1);
		ImGui::SetNextWindowPos({ 0, 0 }); ImGui::SetNextWindowSize(io.DisplaySize);
		ImGui::Begin("##home-warp-production-root", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::SetCursorPos({ 40, 30 });
		Design::BeginScrollChild("##setting-content", { 920, 1040 }, ImGuiChildFlags_AlwaysUseWindowPadding);
		const float available = ImGui::GetContentRegionAvail().x;
		Design::BeginScrollChild("##setting-page-content", { available, 0 }, ImGuiChildFlags_AutoResizeY);
		const ImRect pageClip = ImGui::GetCurrentWindow()->ClipRect;
		Design::RenderHome({ 0, 1, "3.0.0-dev.3981", 0, 0, 1 }, motion);
		ImGui::EndChild(); ImGui::EndChild(); ImGui::End(); ImGui::Render();
		const auto* data = ImGui::GetDrawData(); FrameInfo info{ data->TotalVtxCount, data->TotalIdxCount, 0, pageClip };
		for (const auto* list : data->CmdLists) for (const auto& command : list->CmdBuffer)
			if (command.ElemCount && !command.UserCallback) ++info.batches;
		return info;
	}

	void CheckColdEntry(Checks& checks, Offscreen& renderer)
	{
		// 首次 AutoResizeY 尚未测出页面高度；直接开启动效，不能先用静态帧预热掩盖入口行为。
		Design::ApplyPalette(false); Design::HomeMotionState state;
		constexpr float delta = 1.0F / 60.0F;
		int clippedFrames = 0, visibleFrames = 0;
		for (int frame = 0; frame < 12; ++frame)
		{
			const float elapsed = frame == 0 ? 0.0F : state.shineElapsed;
			const auto rebuilds = state.cache.rebuilds, allocations = state.cache.allocations;
			const bool uncached=state.cache.vertices.empty();
			const auto info = Frame(&state, Away, delta, true);
			ImRect envelope(state.brandOrigin, {state.brandOrigin.x + state.brandWidth,
				state.brandOrigin.y + Brand::Height(state.brandWidth)});
			envelope.Expand(Design::Pixels(14));
			const bool visible = info.pageClip.GetWidth() > 0 && info.pageClip.GetHeight() > 0
				&& envelope.Overlaps(info.pageClip);
			checks.Expect(state.brandVisible == visible, "cold entry uses actual nonempty page clip visibility");
			checks.Expect(state.entries == 1, "cold entry remains one entry while AutoResizeY becomes visible");
			if (!state.heroVisible)
			{
				++clippedFrames;
				checks.Expect(state.shineElapsed == elapsed && state.cache.rebuilds == rebuilds
					&& state.cache.allocations == allocations && state.cache.vertices.empty(),
					"unmeasured clipped cold page consumes no entrance time or brand cache work");
			}
			else
			{
				++visibleFrames;
				checks.Expect(std::abs(state.shineElapsed-(std::min)(.9F,elapsed+delta))<.00001F,
					"cold entry advances by frame delta only after hero becomes visible");
				checks.Expect(state.cache.rebuilds==rebuilds+(visible && uncached ? 1 : 0)
					&& (!visible || !state.cache.vertices.empty()),"cold entry builds visible ink once and reuses it throughout the sheen");
			}
			if (!renderer.Submit(checks, ImGui::GetDrawData())) return;
		}
		checks.Expect(clippedFrames > 0 && visibleFrames > 1,
			"cold AutoResizeY fixture actually covers clipped first frame and subsequent visible frames");
		std::cout << "[HomeWARP] cold-entry clipped=" << clippedFrames << " visible=" << visibleFrames
			<< " entries=" << state.entries << " elapsed=" << state.shineElapsed << '\n';
	}

	bool StableBaseline(Checks& checks, Offscreen& renderer, Design::HomeMotionState& state, Pixels& baseline)
	{
		// 现有 Fluent 控件以 0.083 s 指数补间主题色，关闭主页动效不关闭该颜色通道。
		// 等真实读回连续稳定再采基线，不能把浅→深过渡误计为字标越界的像素变化。
		Pixels previous, current;
		int stableFrames = 0;
		for (int frame = 0; frame < 120; ++frame)
		{
			Frame(&state, Away, 0, false);
			if (!renderer.Submit(checks, ImGui::GetDrawData(), &current)) return false;
			stableFrames = !previous.empty() && previous == current ? stableFrames + 1 : 0;
			if (frame >= 20 && stableFrames >= 3)
			{
				baseline = std::move(current);
				std::cout << "[HomeWARP] stable-theme=" << (Design::IsDarkMode() ? "dark" : "light")
					<< " frames=" << frame + 1 << " identical-readbacks=" << stableFrames << '\n';
				return true;
			}
			previous.swap(current);
		}
		return checks.Expect(false, "actual WARP theme pixels stabilize before motion comparison");
	}

	bool InsideLogo(int x, int y, const Design::HomeMotionState& state, float margin = 18)
	{
		return static_cast<float>(x) >= state.brandOrigin.x - margin
			&& static_cast<float>(x) <= state.brandOrigin.x + state.brandWidth + margin
			&& static_cast<float>(y) >= state.brandOrigin.y - margin
			&& static_cast<float>(y) <= state.brandOrigin.y + Brand::Height(state.brandWidth) + margin;
	}

	bool InsideHero(int x, int y, const Design::HomeMotionState& state)
	{
		const float px=x+.5F-state.heroOrigin.x, py=y+.5F-state.heroOrigin.y;
		if (px<0 || py<0 || px>state.heroSize.x || py>state.heroSize.y) return false;
		const float r=Design::Pixels(8);
		const float dx=(std::max)(0.0F,r-(std::min)(px,state.heroSize.x-px));
		const float dy=(std::max)(0.0F,r-(std::min)(py,state.heroSize.y-py));
		return dx*dx+dy*dy<=r*r;
	}

	void Snapshot(const char* name, const Pixels& pixels)
	{
		// 仅测试输出真实 WARP RGBA，主会话可转换 PNG 审阅；不创建任何窗口。
		std::ofstream file(std::string("Inkeys/Cache/home-design-20261010/")+name+".rgba",std::ios::binary);
		const std::uint32_t dimensions[]{Width,Height};
		file.write(reinterpret_cast<const char*>(dimensions),sizeof(dimensions));
		file.write(reinterpret_cast<const char*>(pixels.data()),pixels.size()*sizeof(pixels[0]));
	}

	void CheckDifference(Checks& checks, const Pixels& before, const Pixels& after,
		const Design::HomeMotionState& state, bool shine)
	{
		if (!checks.Expect(before.size() == after.size() && !before.empty(), "complete matching pixel snapshots")) return;
		int changed = 0, outside = 0, ink = 0, background = 0, maximumBackgroundContrast = 0, maximumContrast = 0;
		float maximumBackgroundDistance=0;
		const ImVec2 center(state.brandOrigin.x+state.brandWidth*.5F,state.brandOrigin.y+Brand::Height(state.brandWidth)*.5F);
		for (int y = 0; y < static_cast<int>(Height); ++y) for (int x = 0; x < static_cast<int>(Width); ++x)
		{
			const size_t index = static_cast<size_t>(y) * Width + x;
			if (before[index] == after[index]) continue;
			++changed; if (shine ? !InsideHero(x,y,state) : !InsideLogo(x,y,state)) ++outside;
			int contrast=0;
			for (int shift : { 0, 8, 16 }) contrast=(std::max)(contrast,std::abs(static_cast<int>((after[index]>>shift)&255)
				-static_cast<int>((before[index]>>shift)&255)));
			maximumContrast=(std::max)(maximumContrast,contrast);
			if (InsideLogo(x,y,state,0)) ++ink;
			if (InsideHero(x,y,state) && shine && inkReference.size()==before.size() && textReference.size()==before.size()
				&& (inkReference[index]>>24)==0 && (textReference[index]>>24)==0 && y<state.buttonBounds[0].y)
			{
				++background; maximumBackgroundContrast=(std::max)(maximumBackgroundContrast,contrast);
				maximumBackgroundDistance=(std::max)(maximumBackgroundDistance,std::hypot(x-center.x,(y-center.y)*1.25F));
			}
		}
		checks.Expect(changed > 100, shine ? "soft sheen changes actual hero pixels" : "parallax changes actual logo pixels");
		checks.Expect(outside==0,shine ? "ripple stays strictly within rounded hero" : "parallax leaves background and foreground fixed");
		if (shine) checks.Expect(background>1000 && maximumBackgroundContrast>0
			&& maximumBackgroundContrast<=(Design::IsDarkMode() ? 26 : 6) && maximumContrast<=(Design::IsDarkMode() ? 32 : 22),
			"neutral background sheen is visible but bounded to a restrained contrast without a strong blue ring");
		std::cout << "[HomeWARP] pixels mode=" << (shine ? "ripple" : "parallax") << " changed=" << changed << " outside=" << outside
			<< " logo-box=" << ink << " background=" << background << " background-max-channel-delta=" << maximumBackgroundContrast
			<< " full-max-channel-delta=" << maximumContrast
			<< " radius=" << maximumBackgroundDistance << '\n';
	}

	void CheckTransparentProductionInk(Checks& checks, Offscreen& renderer, const Design::HomeMotionState& state, bool opening = false)
	{
		// 从刚刚由 RenderHome 提交的顶点/索引提取字标，不调用第二个绘制实现或生成替代几何。
		const auto* data = ImGui::GetDrawData(); const auto& cached = state.cache.vertices;
		const ImDrawList* source = nullptr; int begin = -1;
		if (cached.empty()) { checks.Expect(false, "visible production ink has cached vertices"); return; }
		for (const auto* list : data->CmdLists)
			for (int index = 0; index + static_cast<int>(cached.size()) <= list->VtxBuffer.Size; ++index)
			{
				const auto& v = list->VtxBuffer[index];
				if (v.pos.x != state.brandOrigin.x + cached[0].pos.x || v.pos.y != state.brandOrigin.y + cached[0].pos.y || v.col != cached[0].color) continue;
				bool matching = true;
				for (size_t offset = 0; offset < cached.size(); ++offset)
				{
					const auto& actual = list->VtxBuffer[index + static_cast<int>(offset)];
					matching &= actual.pos.x == state.brandOrigin.x + cached[offset].pos.x
						&& actual.pos.y == state.brandOrigin.y + cached[offset].pos.y && actual.col == cached[offset].color;
				}
				if (matching) { source = list; begin = index; break; }
			}
		if (!checks.Expect(source != nullptr, "locate exact production brand vertices")) return;
		ImDrawList extracted(ImGui::GetDrawListSharedData()); extracted._ResetForNewFrame();
		extracted.PushClipRect({ 0, 0 }, { static_cast<float>(Width), static_cast<float>(Height) });
		bool texturePushed = false;
		for (const auto& command : source->CmdBuffer)
		{
			if (command.UserCallback) continue;
			for (unsigned i = command.IdxOffset; i + 2 < command.IdxOffset + command.ElemCount; i += 3)
			{
				const unsigned a = command.VtxOffset + source->IdxBuffer[static_cast<int>(i)];
				const unsigned b = command.VtxOffset + source->IdxBuffer[static_cast<int>(i + 1)];
				const unsigned c = command.VtxOffset + source->IdxBuffer[static_cast<int>(i + 2)];
				const auto inRange = [&](unsigned index) { return index >= static_cast<unsigned>(begin) && index < static_cast<unsigned>(begin) + cached.size(); };
				if (!inRange(a) || !inRange(b) || !inRange(c)) continue;
				if (!texturePushed) { extracted.PushTexture(command.GetTexID()); texturePushed = true; }
				extracted.PrimReserve(3, 3);
				for (unsigned index : { a, b, c })
				{
					const auto& v = source->VtxBuffer[static_cast<int>(index)];
					extracted.PrimWriteIdx(static_cast<ImDrawIdx>(extracted._VtxCurrentIdx));
					extracted.PrimWriteVtx(v.pos, v.uv, v.col);
				}
			}
		}
		if (texturePushed) extracted.PopTexture(); extracted.PopClipRect();
		checks.Expect(extracted.IdxBuffer.Size == static_cast<int>(Brand::Geometry().indices.size()), "extract every actual brand triangle including descender");
		ImDrawData onlyInk; onlyInk.Valid = true; onlyInk.DisplayPos = data->DisplayPos;
		onlyInk.DisplaySize = data->DisplaySize; onlyInk.FramebufferScale = data->FramebufferScale;
		onlyInk.AddDrawList(&extracted);
		Pixels pixels; if (!renderer.Submit(checks, &onlyInk, &pixels)) return;
		if (opening)
		{
			checks.Expect(pixels==inkReference,"isolated production ink RGBA stays exactly unchanged during background/text sheen");
			std::cout<<"[HomeWARP] ink-sheen identical-rgba="<<(pixels==inkReference)<<'\n';
			Snapshot(Design::IsDarkMode() ? "soft-dark-ink-opening" : "soft-light-ink-opening",pixels);
			return;
		}
		inkReference=pixels;
		int solid = 0, partial = 0, outside = 0, lower = 0;
		for (int y = 0; y < static_cast<int>(Height); ++y) for (int x = 0; x < static_cast<int>(Width); ++x)
		{
			const auto alpha = pixels[static_cast<size_t>(y) * Width + x] >> 24;
			if (alpha == 255) ++solid; else if (alpha) ++partial;
			if (alpha && !InsideLogo(x, y, state, 1)) ++outside;
			if (alpha && static_cast<float>(y) > state.brandOrigin.y + Brand::Height(state.brandWidth) * .8F) ++lower;
		}
		Snapshot(Design::IsDarkMode() ? "soft-dark-ink" : "soft-light-ink",pixels);
		// 在生产 DrawData 上只作仿射缩放和移位，逐像素检查 k 圆头接合的实体内部。
		const auto& mesh=Brand::Geometry(); const float nativeScale=state.brandWidth/mesh.size.x;
		const ImVec2 join(state.brandOrigin.x+mesh.kEnd.x*nativeScale,state.brandOrigin.y+mesh.kEnd.y*nativeScale);
		std::vector<ImVec2> original; original.reserve(extracted.VtxBuffer.Size);
		for (const auto& v : extracted.VtxBuffer) original.push_back(v.pos);
		for (float scale : {1.0F,1.875F,4.0F})
		{
			for (int i=0;i<extracted.VtxBuffer.Size;++i)
				extracted.VtxBuffer[i].pos={500+(original[i].x-join.x)*scale,500+(original[i].y-join.y)*scale};
			Pixels enlarged; if (!renderer.Submit(checks,&onlyInk,&enlarged)) return;
			const float radius=mesh.kRadius*nativeScale*scale;
			int tested=0, gaps=0;
			const int extent=static_cast<int>(std::ceil(radius+3));
			for (int y=500-extent;y<=500+extent;++y) for (int x=500-extent;x<=500+extent;++x)
			{
				const float dx=x+.5F-500,dy=y+.5F-500;
				const float along=dx*mesh.kInward.x+dy*mesh.kInward.y, across=-dx*mesh.kInward.y+dy*mesh.kInward.x;
				const bool interior=std::hypot(dx,dy)<radius-1.2F
					|| (along>=.5F && along<radius+2 && std::abs(across)<radius-.4F);
				if (!interior) continue;
				++tested; if ((enlarged[static_cast<size_t>(y)*Width+x]>>24)<250) ++gaps;
			}
			checks.Expect(tested>50 && gaps==0,"k cap/shaft join has no transparent internal pixels at multiple scales");
			std::cout<<"[HomeWARP] k-cap scale="<<scale<<" solid-samples="<<tested<<" gaps="<<gaps<<'\n';
			if (!Design::IsDarkMode() && scale==4) Snapshot("soft-k-cap-4x",enlarged);
		}
		checks.Expect(solid > 1000 && partial > 100 && outside == 0 && lower > 50,
			"actual backend preserves transparent background, AA fringe and y descender");
		std::cout << "[HomeWARP] transparent-ink solid=" << solid << " AA=" << partial << " lower=" << lower << " outside=" << outside << '\n';
	}

	void CheckProductionText(Checks& checks, Offscreen& renderer, const Design::HomeMotionState& state, bool opening=false)
	{
		// 提取生产字体 UV 三角形，矩形背景/下划线/图标不计入文案像素和 AA 证据。
		const auto* data=ImGui::GetDrawData(); const auto white=ImGui::GetIO().Fonts->TexUvWhitePixel;
		ImDrawList extracted(ImGui::GetDrawListSharedData()); extracted._ResetForNewFrame();
		extracted.PushClipRect({0,0},{static_cast<float>(Width),static_cast<float>(Height)});
		bool texturePushed=false; std::vector<ImDrawVert> vertices;
		const float top=state.brandOrigin.y+Brand::Height(state.brandWidth)+Design::Pixels(24), bottom=state.buttonBounds[0].y;
		for (const auto* list : data->CmdLists) for (const auto& command : list->CmdBuffer)
		{
			if (command.UserCallback) continue;
			for (unsigned i=command.IdxOffset;i+2<command.IdxOffset+command.ElemCount;i+=3)
			{
				const unsigned a=command.VtxOffset+list->IdxBuffer[static_cast<int>(i)],
					b=command.VtxOffset+list->IdxBuffer[static_cast<int>(i+1)], c=command.VtxOffset+list->IdxBuffer[static_cast<int>(i+2)];
				const auto glyph=[&](unsigned index) {
					const auto& v=list->VtxBuffer[static_cast<int>(index)];
					for (const auto& line : ImGui::GetIO().Fonts->TexUvLines)
						if ((v.uv.x==line.x && v.uv.y==line.y) || (v.uv.x==line.z && v.uv.y==line.w)) return false;
					return (v.uv.x!=white.x || v.uv.y!=white.y) && v.pos.y>=top && v.pos.y<=bottom
						&& v.pos.x>=state.heroOrigin.x && v.pos.x<=state.heroOrigin.x+state.heroSize.x;
				};
				if (!glyph(a) || !glyph(b) || !glyph(c)) continue;
				if (!texturePushed) { extracted.PushTexture(command.GetTexID()); texturePushed=true; }
				extracted.PrimReserve(3,3);
				for (unsigned index : {a,b,c})
				{
					const auto& v=list->VtxBuffer[static_cast<int>(index)]; vertices.push_back(v);
					extracted.PrimWriteIdx(static_cast<ImDrawIdx>(extracted._VtxCurrentIdx)); extracted.PrimWriteVtx(v.pos,v.uv,v.col);
				}
			}
		}
		if (texturePushed) extracted.PopTexture(); extracted.PopClipRect();
		if (!checks.Expect(vertices.size()>100,"extract genuine production slogan/open-source/repository font triangles")) return;
		ImDrawData onlyText; onlyText.Valid=true; onlyText.DisplayPos=data->DisplayPos;
		onlyText.DisplaySize=data->DisplaySize; onlyText.FramebufferScale=data->FramebufferScale; onlyText.AddDrawList(&extracted);
		Pixels pixels; if (!renderer.Submit(checks,&onlyText,&pixels)) return;
		static std::vector<ImDrawVert> referenceVertices;
		if (!opening) { textReference=pixels; referenceVertices=std::move(vertices); return; }
		if (!checks.Expect(pixels.size()==textReference.size() && vertices.size()==referenceVertices.size(),"opening preserves production text tessellation")) return;
		bool geometry=true, vertexAlpha=true; int changed=0, alphaChanges=0, maximum=0;
		for (size_t i=0;i<vertices.size();++i)
		{
			const auto& a=vertices[i]; const auto& b=referenceVertices[i];
			geometry &= a.pos.x==b.pos.x && a.pos.y==b.pos.y && a.uv.x==b.uv.x && a.uv.y==b.uv.y;
			vertexAlpha &= (a.col&IM_COL32_A_MASK)==(b.col&IM_COL32_A_MASK);
		}
		for (size_t i=0;i<pixels.size();++i)
		{
			alphaChanges+=(pixels[i]>>24)!=(textReference[i]>>24); changed+=pixels[i]!=textReference[i];
			for (int shift : {0,8,16}) maximum=(std::max)(maximum,std::abs(static_cast<int>((pixels[i]>>shift)&255)-static_cast<int>((textReference[i]>>shift)&255)));
		}
		checks.Expect(geometry && vertexAlpha && alphaChanges==0 && changed>20 && maximum>0 && maximum<=(Design::IsDarkMode() ? 31 : 21),
			"isolated actual text lightly gleams while its geometry, UVs and transparent antialiasing remain identical");
		std::cout<<"[HomeWARP] text-sheen changed="<<changed<<" max-channel-delta="<<maximum<<" alpha-changes="<<alphaChanges<<" stable-geometry="<<geometry<<'\n';
		Snapshot(Design::IsDarkMode() ? "soft-dark-text-opening" : "soft-light-text-opening",pixels);
	}

	double Micros(Clock::time_point start, Clock::time_point end)
	{
		return std::chrono::duration<double, std::micro>(end - start).count();
	}
	double Percentile(std::vector<double> values, double quantile)
	{
		std::sort(values.begin(), values.end());
		return values[static_cast<size_t>(std::floor(static_cast<double>(values.size() - 1) * quantile))];
	}

	void Benchmark(Checks& checks, Offscreen& renderer, const char* name, int mode, const FrameInfo& baseline)
	{
		Design::HomeMotionState state;
		Frame(&state, Away, 0, false);
		const ImVec2 center(state.brandOrigin.x + state.brandWidth * .5F, state.brandOrigin.y + Brand::Height(state.brandWidth) * .5F);
		std::vector<double> cpu, submit; cpu.reserve(100); submit.reserve(100);
		AllocationScope allocations;
		std::uint64_t allocatedBefore = 0, cacheBefore = 0, rebuiltBefore = 0, rippleBefore=0;
		std::uint64_t serial = mode==4 ? 2 : 1;
		FrameInfo info;
		for (int frame = -20; frame < 100; ++frame)
		{
			if (frame == 0) { allocatedBefore = allocations.allocations; cacheBefore = state.cache.allocations; rebuiltBefore = state.cache.rebuilds; rippleBefore=state.ripple.allocations; }
			ImVec2 mouse = Away;
			if (mode == 2) mouse = { center.x + std::sin(static_cast<float>(frame) * .2F) * state.brandWidth * .6F,
				center.y + std::cos(static_cast<float>(frame) * .17F) * Brand::Height(state.brandWidth) * .6F };
			// 回正每周期先通过真实鼠标追踪建立偏转，再记录鼠标离开后的帧。
			if (mode == 3 && frame % 20 == 0)
				for (int settle = 0; settle < 20; ++settle) Frame(&state, { center.x + state.brandWidth * .5F, center.y }, .05F);
			if (mode == 4 && frame % 50 == 0) ++serial;
			const auto begin = Clock::now();
			info = Frame(mode == 0 ? nullptr : &state, mouse, 1.0F / 60.0F, true, serial);
			const auto drawn = Clock::now();
			if (!renderer.Submit(checks, ImGui::GetDrawData())) return;
			const auto completed = Clock::now();
			if (frame >= 0) { cpu.push_back(Micros(begin, drawn)); submit.push_back(Micros(drawn, completed)); }
			const bool ripple=state.shineElapsed>0 && state.shineElapsed<.9F;
			const int extraVertices=mode==4 && ripple ? static_cast<int>(state.ripple.vertices.size()) : 0;
			const int extraIndices=mode==4 && ripple ? static_cast<int>(state.ripple.indices.size()) : 0;
			checks.Expect(info.vertices==baseline.vertices+extraVertices && info.indices==baseline.indices+extraIndices
				&& info.batches==baseline.batches && extraVertices<=3000,
				"opening adds only the bounded cached hero mesh; all modes retain existing draw batches");
		}
		checks.Expect(allocations.allocations==allocatedBefore,"warmed actual homepage motion performs no additional ImGui allocations");
		checks.Expect(state.cache.allocations==cacheBefore && state.ripple.allocations==rippleBefore,
			"warm brand and ripple caches never grow during benchmark");
		if (mode == 1 || mode == 4) checks.Expect(state.cache.rebuilds == rebuiltBefore, "idle and entrance reuse the logo cache without sheen-driven rebuilds");
		std::cout << "[HomeWARP] benchmark=" << name << " frames=100 cpu-us median=" << Percentile(cpu, .5)
			<< " p95=" << Percentile(cpu, .95) << " submit-complete-copy-us median=" << Percentile(submit, .5)
			<< " p95=" << Percentile(submit, .95) << " vertices=" << info.vertices << " indices=" << info.indices
			<< " batches=" << info.batches << " imgui-allocs=" << allocations.allocations - allocatedBefore
			<< " ripple-vertices="<<state.ripple.vertices.size()<<" ripple-cache-growth="<<state.ripple.allocations-rippleBefore
			<< " cache-growth=" << state.cache.allocations - cacheBefore << " cache-rebuilds=" << state.cache.rebuilds - rebuiltBefore << '\n';
	}
}

int RunSettingHomeWarpProbe(ImFont* regular, ImFont* strong, ImFont* icons)
{
	Checks checks; Offscreen renderer;
	if (!checks.Expect(ImGui::GetIO().BackendRendererUserData == nullptr && ImGui::GetFrameCount() == 0,
		"probe runs before any CPU fake texture materialization") || !renderer.Initialize(checks)) return checks.failures;
	Design::BindTextFonts(regular, strong); ImFluent::SetIconFont(icons);
	CheckColdEntry(checks, renderer);
	for (bool dark : { false, true })
	{
		Design::ApplyPalette(dark); Design::HomeMotionState state;
		Pixels baseline; if (!StableBaseline(checks, renderer, state, baseline)) return checks.failures;
		checks.Expect(state.brandVisible && state.brandWidth > 400, "production brand fully visible at benchmark viewport");
		CheckTransparentProductionInk(checks, renderer, state);
		CheckProductionText(checks,renderer,state);
		Snapshot(dark ? "soft-dark-static" : "soft-light-static",baseline);
		Pixels changed;
		Design::ConfigureHomeMotion(state,false,1,0,true,1); Frame(&state,Away,0);
		renderer.Submit(checks,ImGui::GetDrawData(),&changed);
		checks.Expect(changed==baseline,"opening starts from exact static pixels");
		const auto openingRebuilds=state.cache.rebuilds; const auto openingBounds=state.buttonBounds;
		for (int i=0;i<14;++i) Frame(&state,Away);
		checks.Expect(state.cache.rebuilds==openingRebuilds,"changing sheen time never rebuilds a stable logo cache");
		for (size_t i=0;i<openingBounds.size();++i)
			checks.Expect(std::memcmp(&openingBounds[i],&state.buttonBounds[i],sizeof(ImVec4))==0,"opening leaves actual Fluent button hit rectangles unchanged");
		renderer.Submit(checks,ImGui::GetDrawData(),&changed); CheckDifference(checks,baseline,changed,state,true);
		Snapshot(dark ? "soft-dark-middle" : "soft-light-middle",changed);
		CheckProductionText(checks,renderer,state,true);
		CheckTransparentProductionInk(checks,renderer,state,true);
		for (int i=0;i<25;++i) Frame(&state,Away);
		renderer.Submit(checks,ImGui::GetDrawData(),&changed);
		int belowLogo=0; for (int y=0;y<static_cast<int>(Height);++y) for (int x=0;x<static_cast<int>(Width);++x)
			if (y>state.brandOrigin.y+Brand::Height(state.brandWidth)+40 && InsideHero(x,y,state)
				&& changed[static_cast<size_t>(y)*Width+x]!=baseline[static_cast<size_t>(y)*Width+x]) ++belowLogo;
		checks.Expect(belowLogo>1000,"wave visibly propagates beyond the logo into the lower hero background");
		CheckDifference(checks,baseline,changed,state,true);
		CheckTransparentProductionInk(checks,renderer,state,true);
		Snapshot(dark ? "soft-dark-late" : "soft-light-late",changed);
		for (int i = 0; i < 60; ++i) Frame(&state, Away);
		renderer.Submit(checks, ImGui::GetDrawData(), &changed);
		checks.Expect(changed == baseline, "finished reflection returns exactly to static pixels");
		for (ImVec2 corner : { ImVec2(-1, -1), ImVec2(1, -1), ImVec2(-1, 1), ImVec2(1, 1) })
		{
			const ImVec2 mouse(state.brandOrigin.x + state.brandWidth * (.5F + corner.x * .6F),
				state.brandOrigin.y + Brand::Height(state.brandWidth) * (.5F + corner.y * .6F));
			for (int i = 0; i < 60; ++i) Frame(&state, mouse);
			renderer.Submit(checks, ImGui::GetDrawData(), &changed); CheckDifference(checks, baseline, changed, state, false);
		}
		Frame(&state, Away, 0, false); renderer.Submit(checks, ImGui::GetDrawData(), &changed);
		checks.Expect(changed == baseline, "disabling animations restores exact static pixels");
	}
	Design::ApplyPalette(false);
	FrameInfo baseline; for (int i = 0; i < 20; ++i) baseline = Frame(nullptr, Away);
	for (int mode = 0; mode < 5; ++mode)
		Benchmark(checks, renderer, std::array{ "static", "idle", "moving", "returning", "entrance" }[mode], mode, baseline);
	std::cout << "[HomeWARP] CPU excludes WARP submission; completed submission includes copy/Map wait. "
		"ImGui allocator counts exclude existing std::string/vector work; brand cache growth is counted separately.\n";
	return checks.failures;
}
