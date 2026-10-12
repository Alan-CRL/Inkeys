#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include "setting_home_warp_tests.h"
#include "../Inkeys/Inkeys/UI/Setting/Setting.Pages.h"
#include "../Inkeys/Inkeys/UI/Setting/Setting.SessionState.h"
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
		bool ExpectedResult(HRESULT actual, HRESULT expected, const char* message)
		{
			if (actual == expected) return true;
			std::cerr << "[HomeWARP] actual HRESULT=0x" << std::hex << static_cast<unsigned long>(actual)
				<< " expected=0x" << static_cast<unsigned long>(expected) << std::dec << '\n';
			return Expect(false, message);
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

		bool Submit(Checks& checks, ImDrawData* data, Pixels* pixels = nullptr, HRESULT expectedResult = S_OK, bool solidClear = false)
		{
			const auto color=ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
			const float clear[4]{solidClear ? color.x*color.w : 0,solidClear ? color.y*color.w : 0,
				solidClear ? color.z*color.w : 0,solidClear ? color.w : 0};
			ID3D11RenderTargetView* rtv = view.Get();
			context->OMSetRenderTargets(1, &rtv, nullptr);
			context->ClearRenderTargetView(rtv, clear);
			ImGui_ImplDX11_RenderDrawData(data);
			const HRESULT result = ImGui_ImplDX11_GetLastResult();
			if (!checks.ExpectedResult(result, expectedResult, "production backend preserves expected render HRESULT")) return false;
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

	struct FrameInfo { int vertices = 0, indices = 0, batches = 0; ImRect pageClip; bool skipped = false; Design::HomeAction action = Design::HomeAction::None; bool rootSuppressed = false; };
	FrameInfo Frame(Design::HomeMotionState* motion, ImVec2 mouse, float delta = 1.0F / 60.0F,
		bool enabled = true, std::uint64_t serial = 1, bool optimized = false, float rate = 1, bool solidBackdrop = false, bool suppressRoot = false)
	{
		auto& io = ImGui::GetIO(); io.DisplaySize = { static_cast<float>(Width), static_cast<float>(Height) };
		io.DisplayFramebufferScale = { 1, 1 }; io.DeltaTime = 1.0F / 60.0F;
		io.AddMouseSourceEvent(ImGuiMouseSource_Mouse); io.AddMousePosEvent(mouse.x, mouse.y);
		io.AddMouseButtonEvent(0, false);
		ImGui_ImplDX11_NewFrame(); ImGui::NewFrame(); Design::ConfigureRowAnimations(0, false, 1);
		if (motion) Design::ConfigureHomeMotion(*motion, true, serial, delta, enabled, rate);
		const bool entryBlocked=motion && Design::CancelHomeEntryOnInput(*motion);
		ImGui::SetNextWindowPos({ 0, 0 }); ImGui::SetNextWindowSize(io.DisplaySize);
		const bool rootSuppressed=suppressRoot && motion && Design::CanOmitHomeEntryRootBackground(*motion,
			solidBackdrop,ImGui::GetStyle().Alpha,ImGui::GetStyle().Colors[ImGuiCol_WindowBg]);
		if (rootSuppressed) ImGui::SetNextWindowBgAlpha(0);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
		ImGui::Begin("##home-warp-production-root", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PopStyleVar();
		Design::NavigationState navigation; navigation.animationsEnabled=false;
		Design::AdvanceNavigationAnimations(navigation,0,false,1);
		const auto shell=Design::ResolveShellGeometry(static_cast<float>(Width)/Design::Pixels(1),static_cast<float>(Height)/Design::Pixels(1),navigation);
		ImGui::GetWindowDrawList()->AddRectFilled({0,0},{static_cast<float>(Width),Design::Pixels(32)},Design::AppBase);
		auto* rootDraw=ImGui::GetWindowDrawList();
		const bool skipped=optimized && motion && Design::BeginHomeEntryFrame(*motion,
			Design::MakeHomeEntryFrameKey({Design::Pixels(shell.content.left),Design::Pixels(shell.content.top),
				Design::Pixels(shell.content.right),Design::Pixels(shell.content.bottom)},Design::Pixels(shell.gutter),
				1,1,0,ImGui::GetFont(),nullptr,nullptr));
		ImRect pageClip; Design::HomeAction homeAction=Design::HomeAction::None;
		if (!skipped)
		{

		ImGui::SetCursorPos({Design::Pixels(shell.content.left),Design::Pixels(shell.content.top)});
		ImGui::PushStyleColor(ImGuiCol_ChildBg,Design::PageSurface);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{Design::Pixels(shell.gutter),Design::Pixels(28)});
		Design::BeginScrollChild("##setting-content", {Design::Pixels(shell.content.Width()),Design::Pixels(shell.content.Height())}, ImGuiChildFlags_AlwaysUseWindowPadding);
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
		const auto outerClip=ImGui::GetCurrentWindow()->ClipRect;
		if (motion) Design::ConfigureHomeEntryViewport(*motion,{0,Design::Pixels(32),static_cast<float>(Width),static_cast<float>(Height)},
			{outerClip.Min.x,outerClip.Min.y,outerClip.Max.x,outerClip.Max.y});
		const float available = ImGui::GetContentRegionAvail().x;
		Design::BeginScrollChild("##setting-page-content", { available, 0 }, ImGuiChildFlags_AutoResizeY);
		pageClip = ImGui::GetCurrentWindow()->ClipRect;
		ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha,1); ImGui::BeginDisabled(entryBlocked);
		homeAction=Design::RenderHome({ 0, 1, "3.0.0-dev.3981", 0, 0, 1 }, motion);
		ImGui::EndDisabled(); ImGui::PopStyleVar();
		ImGui::EndChild(); ImGui::EndChild();
		ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha,1); ImGui::BeginDisabled(entryBlocked);
		Design::RenderNavigationPane(shell,navigation,Design::NavigationDestination{});
		ImGui::EndDisabled(); ImGui::PopStyleVar();
		}
		if (motion) Design::RenderHomeEntryOverlay(*motion);
		ImGui::End(); ImGui::Render();
		if (optimized && motion) Design::FilterHomeEntryDrawData(*motion,ImGui::GetDrawData(),rootDraw,nullptr,ImGui::GetForegroundDrawList());
		const auto* data = ImGui::GetDrawData(); FrameInfo info{ data->TotalVtxCount, data->TotalIdxCount, 0, pageClip, skipped, homeAction, rootSuppressed };
		for (const auto* list : data->CmdLists) for (const auto& command : list->CmdBuffer)
			if (command.ElemCount && !command.UserCallback) ++info.batches;
		return info;
	}

	bool SubmitPrepared(Checks& checks, Offscreen& renderer, Design::HomeMotionState& state, Pixels* pixels=nullptr, bool solidClear=false)
	{
		const bool success=renderer.Submit(checks,ImGui::GetDrawData(),pixels,S_OK,solidClear);
		Design::CompleteHomeEntryFrame(state,success,Design::HomeEntryTexturesReady(ImGui::GetDrawData()));
		return success;
	}

	void CheckBackendFailures(Checks& checks, Offscreen& renderer)
	{
		using Inkeys::UI::Setting::IsSharedDeviceLoss;
		ImTextureData texture;
		texture.Create(ImTextureFormat_RGBA32, 8, 8);
		std::memset(texture.Pixels, 255, static_cast<size_t>(texture.GetSizeInBytes()));
		// 某些 WARP 版本接受超过声明上限的尺寸；零宽描述必须由真实 D3D API 拒绝。
		texture.Width = 0;
		ImGui_ImplDX11_UpdateTexture(&texture);
		checks.ExpectedResult(ImGui_ImplDX11_GetLastResult(), E_INVALIDARG, "real WARP rejects zero-width texture description");
		checks.Expect(texture.Status == ImTextureStatus_WantCreate
			&& texture.TexID == ImTextureID_Invalid && texture.BackendUserData == nullptr,
			"actual FL11.0 WARP zero-width creation fails transactionally without publishing OK or backend data");
		checks.Expect(!IsSharedDeviceLoss(ImGui_ImplDX11_GetLastResult(), renderer.device->GetDeviceRemovedReason()),
			"real invalid resource creation leaves WARP healthy and follows ordinary retry");

		// 直接提交真实页面 DrawData 并替换其待上传列表；失败帧应只有清屏，不能继续绘制页面。
		Frame(nullptr, Away);
		ImDrawData data = *ImGui::GetDrawData();
		ImVector<ImTextureData*> textures; textures.push_back(&texture); data.Textures = &textures;
		Pixels pixels;
		renderer.Submit(checks, &data, &pixels, E_INVALIDARG);
		checks.Expect(!pixels.empty() && std::all_of(pixels.begin(), pixels.end(), [](auto pixel) { return pixel == 0; }),
			"texture upload failure aborts all real WARP draw commands before invalid texture getters");

		// 即使无效描述意外被接受，也先通过生产销毁路径归还 owner，不能手改状态泄漏资源。
		texture.WantDestroyNextFrame = true; texture.UnusedFrames = 1;
		texture.SetStatus(ImTextureStatus_WantDestroy); ImGui_ImplDX11_UpdateTexture(&texture);
		checks.Expect(texture.BackendUserData == nullptr && texture.TexID == ImTextureID_Invalid
			&& texture.Status == ImTextureStatus_Destroyed, "fixture teardown safely handles unexpected creation success");
		texture.DestroyPixels(); texture.WantDestroyNextFrame = false;
		texture.Create(ImTextureFormat_RGBA32, 8, 8);
		std::memset(texture.Pixels, 255, static_cast<size_t>(texture.GetSizeInBytes()));
		for (auto stage : { ImGui_ImplDX11_TestFailure_Texture, ImGui_ImplDX11_TestFailure_TextureView })
			for (HRESULT failure : { E_OUTOFMEMORY, DXGI_ERROR_DEVICE_REMOVED, DXGI_ERROR_DEVICE_RESET })
			{
				// 单次 HRESULT 注入只替换失败的 API；其余创建、销毁和像素提交仍用真实 WARP/backend。
				ImGui_ImplDX11_TestFailNext(stage, failure);
				renderer.Submit(checks, &data, &pixels, failure);
				checks.Expect(texture.Status == ImTextureStatus_WantCreate && texture.TexID == ImTextureID_Invalid
					&& texture.BackendUserData == nullptr, "both create phases roll back all partial backend publication");
				checks.Expect(std::all_of(pixels.begin(), pixels.end(), [](auto pixel) { return pixel == 0; }),
					"injected OOM or device loss aborts draw submission with a transparent cleared target");
				checks.Expect(IsSharedDeviceLoss(failure, renderer.device->GetDeviceRemovedReason()) == (failure != E_OUTOFMEMORY),
					"injected device errors follow shared recovery while OOM on a healthy WARP device remains retry");
				ImGui_ImplDX11_UpdateTexture(&texture);
				checks.Expect(ImGui_ImplDX11_GetLastResult() == S_OK && texture.Status == ImTextureStatus_OK
					&& texture.TexID != ImTextureID_Invalid && texture.BackendUserData != nullptr,
					"same texture retries successfully after either creation phase fails");
				texture.SetStatus(ImTextureStatus_WantUpdates); texture.Updates.push_back({ 0, 0, 8, 8 });
				ImGui_ImplDX11_UpdateTexture(&texture);
				checks.Expect(ImGui_ImplDX11_GetLastResult() == S_OK && texture.Status == ImTextureStatus_OK,
					"successful recreated WARP texture accepts dynamic atlas block updates");
				texture.Updates.clear(); texture.UnusedFrames = 1;
				texture.SetStatus(ImTextureStatus_WantDestroy); ImGui_ImplDX11_UpdateTexture(&texture);
				checks.Expect(texture.Status == ImTextureStatus_WantCreate && texture.BackendUserData == nullptr
					&& texture.TexID == ImTextureID_Invalid, "cleanup returns retained CPU pixels to a clean recreatable state");
			}

		D3D11_TEXTURE2D_DESC desc{}; desc.Width = desc.Height = 8; desc.MipLevels = desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE; desc.Usage = D3D11_USAGE_DEFAULT;
		ComPtr<ID3D11Texture2D> partial;
		if (checks.Result(renderer.device->CreateTexture2D(&desc, nullptr, partial.GetAddressOf()), "create real partial texture fixture"))
			for (ID3D11Texture2D* value : { static_cast<ID3D11Texture2D*>(nullptr), partial.Get() })
			{
				ImGui_ImplDX11_TestPublishPartialTexture(&texture, value);
				texture.SetStatus(ImTextureStatus_WantUpdates); ImGui_ImplDX11_UpdateTexture(&texture);
				checks.Expect(ImGui_ImplDX11_GetLastResult() == E_FAIL && texture.Status == ImTextureStatus_WantCreate
					&& texture.TexID == ImTextureID_Invalid && texture.BackendUserData == nullptr,
					"legacy OK with null SRV or null texture is safely revoked before update");
				ImGui_ImplDX11_TestPublishPartialTexture(&texture, value);
				texture.WantDestroyNextFrame = true; texture.RefCount = 1;
				ImGui::GetPlatformIO().Textures.push_back(&texture);
				ImGui_ImplDX11_InvalidateDeviceObjects(); ImGui_ImplDX11_InvalidateDeviceObjects();
				ImGui::GetPlatformIO().Textures.pop_back();
				checks.Expect(texture.Status == ImTextureStatus_Destroyed && texture.BackendUserData == nullptr
					&& texture.TexID == ImTextureID_Invalid, "actual repeated device invalidation safely destroys screenshot-like null partial resources");
				texture.WantDestroyNextFrame = false; texture.SetStatus(ImTextureStatus_WantCreate);
				checks.Expect(ImGui_ImplDX11_CreateDeviceObjects(), "backend recreates embedded shaders after repeated partial cleanup");
			}

		Frame(nullptr, Away); renderer.Submit(checks, ImGui::GetDrawData());
		ImGui_ImplDX11_TestFailNext(ImGui_ImplDX11_TestFailure_IndexMap, E_OUTOFMEMORY);
		renderer.Submit(checks, ImGui::GetDrawData(), &pixels, E_OUTOFMEMORY);
		checks.Expect(std::all_of(pixels.begin(), pixels.end(), [](auto pixel) { return pixel == 0; }),
			"second buffer Map failure unwinds mapped vertex buffer before returning");
		renderer.Submit(checks, ImGui::GetDrawData(), &pixels);
		checks.Expect(std::any_of(pixels.begin(), pixels.end(), [](auto pixel) { return pixel != 0; }),
			"subsequent genuine WARP draw succeeds after Map rollback");

		// 模拟 epoch 交接：撤销旧 backend 并以同一健康测试设备新建，断言真实图集可重传。
		ImGui_ImplDX11_Shutdown(); renderer.initialized = false;
		long initResult = S_OK;
		checks.Expect(!ImGui_ImplDX11_Init(nullptr, renderer.context.Get(), &initResult) && initResult == E_INVALIDARG
			&& ImGui::GetIO().BackendRendererUserData == nullptr && ImGui::GetIO().BackendRendererName == nullptr
			&& (ImGui::GetIO().BackendFlags & (ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset)) == 0,
			"failed backend initialization preserves exact HRESULT without publishing owner or capabilities");
		checks.Expect(!ImGui_ImplDX11_Init(renderer.device.Get(), nullptr, &initResult) && initResult == E_INVALIDARG
			&& ImGui::GetIO().BackendRendererUserData == nullptr,
			"invalid context initialization preserves exact HRESULT without a backend error slot");
		renderer.initialized = ImGui_ImplDX11_Init(renderer.device.Get(), renderer.context.Get(), &initResult);
		checks.Expect(initResult == S_OK, "successful initialization resets local HRESULT after prior failures");
		if (checks.Expect(renderer.initialized && ImGui_ImplDX11_CreateDeviceObjects(), "backend can recover after shutdown and recreate"))
		{
			Frame(nullptr, Away); renderer.Submit(checks, ImGui::GetDrawData(), &pixels);
			checks.Expect(std::any_of(pixels.begin(), pixels.end(), [](auto pixel) { return pixel != 0; }),
				"real homepage and font atlas render again after backend recreation");
		}
		std::cout << "[HomeWARP] texture lifecycle: real E_INVALIDARG, both-phase injected OOM/removed/reset, legacy null partial, repeated teardown, Map rollback and recreation checked; device removal is simulated.\n";
	}

	void CheckColdEntry(Checks& checks, Offscreen& renderer)
	{
		// 首次动态字体/AutoResizeY 尚未测量；不先用静态帧掩盖首开字形与提交准备。
		Design::ApplyPalette(false); Design::HomeMotionState state;
		constexpr float delta=1.0F/60.0F; int clipped=0,visible=0,skipped=0;
		for (int frame=0;frame<12;++frame)
		{
			const float before=state.entryElapsed; const bool ready=state.preparation.ready;
			const auto info=Frame(&state,Away,delta,true,1,true);
			checks.Expect(state.entries==1 && state.EntryActive(),"cold actual outer viewport starts one entry before AutoResizeY has grown");
			if (!info.skipped)
			{
				ImRect envelope(state.brandOrigin,{state.brandOrigin.x+state.brandWidth,state.brandOrigin.y+Brand::Height(state.brandWidth)});
				envelope.Expand(Design::Pixels(14));
				const bool actual=info.pageClip.GetWidth()>0 && info.pageClip.GetHeight()>0 && envelope.Overlaps(info.pageClip);
				checks.Expect(state.brandVisible==actual,"cold full warm frame uses actual nonempty page clip");
				if (state.heroVisible) ++visible; else ++clipped;
			}
			else ++skipped;
			checks.Expect(!ready ? state.entryElapsed==before : state.entryElapsed<=before+delta+.00001F,
				"cold font preparation freezes time and prepared motion consumes at most actual delta");
			if (!SubmitPrepared(checks,renderer,state)) return;
		}
		checks.Expect(clipped>0 && visible>0 && skipped>1 && state.preparation.ready,
			"cold test covers clipped first child, successful full warm glyph frames and opaque fast frames");
		std::cout<<"[HomeWARP] cold-entry clipped="<<clipped<<" full-visible="<<visible<<" opaque-skipped="<<skipped
			<<" entries="<<state.entries<<" elapsed="<<state.entryElapsed<<'\n';
	}

	bool StableBaseline(Checks& checks, Offscreen& renderer, Design::HomeMotionState& state, Pixels& baseline)
	{
		// 现有 Fluent 控件以 0.083 s 指数补间主题色，关闭主页动效不关闭该颜色通道。
		// 等真实读回连续稳定再采基线，不能把浅→深过渡误计为字标越界的像素变化。
		// Fluent 的 U32 颜色补间可能在主题目标附近量化停留；正常页面缺席后重现会回到精确目标色。
		// 用真实非主页帧建立该既有生命周期基线，不放宽 RGBA 或改全局控件配色。
		for (int absent=0;absent<2;++absent)
		{
			ImGui_ImplDX11_NewFrame(); ImGui::NewFrame();
			ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
			ImGui::Begin("##home-warp-baseline-other-page",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoSavedSettings);
			ImGui::TextUnformatted("Other settings page"); ImGui::End(); ImGui::Render();
			if (!renderer.Submit(checks,ImGui::GetDrawData())) return false;
		}
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

	void CheckPreparedEntry(Checks& checks, Offscreen& renderer, const Pixels& baseline)
	{
		Design::HomeMotionState state; FrameInfo info;
		const auto coldBegin=Clock::now(); unsigned warm=0;
		for (;warm<8;++warm)
		{
			info=Frame(&state,Away,0,true,41,true,5);
			checks.Expect(state.entryElapsed==0 && !info.skipped,"real cold glyph/layout preparation does not consume animation time even at rate 5");
			if (!SubmitPrepared(checks,renderer,state)) return;
			if (state.preparation.ready) break;
		}
		const auto prepared=Clock::now();
		checks.Expect(warm<6 && state.preparation.ready && state.preparation.warmFrames==2,
			"two actual successful backend submissions prepare current glyph sizes within bounded warm frames");
		const auto entries=state.entries; Pixels fast,full;
		state.entryElapsed=.76F;
		info=Frame(&state,Away,0,true,41,true);
		if (!SubmitPrepared(checks,renderer,state,&fast)) return;
		checks.Expect(info.skipped && state.entries==entries,
			"opaque production helper omits page/nav generation and submits only root/caption and foreground");
		const auto* data=ImGui::GetDrawData(); bool onlyOwn=true;
		for (const auto* list : data->CmdLists) onlyOwn &= list==ImGui::FindWindowByName("##home-warp-production-root")->DrawList
			|| list==ImGui::GetForegroundDrawList();
		checks.Expect(onlyOwn,"real DX11 DrawData contains no obscured page/navigation lists");
		int roots=0,foregrounds=0; bool legal=true;
		for (const auto* list : data->CmdLists) for (const auto& command : list->CmdBuffer)
		{
			if (!command.ElemCount || command.UserCallback) continue;
			const bool rootList=list!=ImGui::GetForegroundDrawList();
			if (rootList) ++roots; else ++foregrounds;
			legal &= command.GetTexID()!=ImTextureID_Invalid && (rootList ? command.ClipRect.w<=state.clientContent.y
				: command.ClipRect.y>=state.clientContent.y);
			std::cout<<"[HomeWARP] opaque-command list="<<(rootList ? "root/caption" : "foreground")
				<<" elems="<<command.ElemCount<<" vtx-offset="<<command.VtxOffset<<" texture="<<command.GetTexID()
				<<" clip="<<command.ClipRect.x<<','<<command.ClipRect.y<<','<<command.ClipRect.z<<','<<command.ClipRect.w<<'\n';
		}
		checks.Expect(legal && roots==2 && foregrounds==1 && info.batches==roots+foregrounds,
			"root decoration/inner caption clip commands and one brand foreground are the only legitimate opaque batches");
		Design::HomeMotionState reference=state; reference.preparation={};
		Frame(&reference,Away,0,true,41,false); if (!renderer.Submit(checks,ImGui::GetDrawData(),&full)) return;
		checks.Expect(fast==full,"opaque writing readback equals the unoptimized real full-page submission exactly");
		state.entryElapsed=Design::HomeEntryTiming::DockStart-.001F;
		for (int frame=0;frame<2;++frame)
		{
			info=Frame(&state,Away,.05F,true,41,true,5);
			checks.Expect(!info.skipped && state.entryElapsed==Design::HomeEntryTiming::DockStart
				&& Design::ResolveHomeEntryPose(state).opacity==1,"fastest rate cannot expose stale AutoResize children before two full reveal warm submissions");
			if (!SubmitPrepared(checks,renderer,state)) return;
		}
		checks.Expect(state.preparation.revealReady,"pre-fade child geometry is confirmed through real backend twice");
		info=Frame(&state,Away,.01F,true,41,true);
		if (!SubmitPrepared(checks,renderer,state,&fast)) return;
		checks.Expect(!info.skipped && info.pageClip.GetHeight()>0 && Design::ResolveHomeEntryPose(state).opacity<1,
			"first translucent frame restores measured page and navigation immediately");
		reference=state; reference.preparation={}; Frame(&reference,Away,0,true,41,false);
		if (!renderer.Submit(checks,ImGui::GetDrawData(),&full)) return;
		checks.Expect(fast==full,"first translucent real WARP pixels match the full-page control frame without pop or old UVs");
		state.entryElapsed=Design::HomeEntryTiming::Total-.005F;
		Frame(&state,Away,.01F,true,41,true); if (!SubmitPrepared(checks,renderer,state,&fast)) return;
		checks.Expect(!state.EntryActive() && fast==baseline,"prepared fast path finishes at exactly the static homepage pixels");

		std::cout<<"[HomeWARP] prepared-entry theme="<<(Design::IsDarkMode() ? "dark" : "light")
			<<" full-warm-frames="<<warm+1<<" elapsed-preparation-us="
			<<std::chrono::duration<double,std::micro>(prepared-coldBegin).count()<<" fast-frames="<<state.preparation.skippedFrames
			<<" bounded-state-bytes="<<sizeof(state.preparation)<<'\n';
	}

	void CheckSolidRootBackground(Checks& checks, Offscreen& renderer)
	{
		using Timing=Design::HomeEntryTiming;
		Design::HomeMotionState state;
		Frame(&state,Away,0,false,73); renderer.Submit(checks,ImGui::GetDrawData());
		for (int warm=0;warm<4;++warm)
		{
			state.entry=Design::HomeMotionState::Entry::Playing; state.entryElapsed=Timing::DockStart;
			Frame(&state,Away,0,true,73,true,1,true,true);
			if (!SubmitPrepared(checks,renderer,state,nullptr,true)) return;
		}
		Pixels control,optimized; int comparisons=0,edgePixels=0;
		for (float time : {0.0F,Timing::Wait+.01F,.76F,Timing::WritingEnd+.15F,Timing::DockStart,
			Timing::DockStart+Timing::Dock*.5F,Timing::SettleStart+Timing::Settle*.5F,Timing::Total})
		{
			state.entry=Design::HomeMotionState::Entry::Playing; state.entryElapsed=time;
			const auto info=Frame(&state,Away,0,true,73,true,1,true,true);
			if (!SubmitPrepared(checks,renderer,state,&optimized,true)) return;
			checks.Expect(info.rootSuppressed,"active Solid/alpha1 entry uses root alpha override while preserving original window flags");
			auto full=state; full.entry=Design::HomeMotionState::Entry::Playing; full.entryElapsed=time;
			Frame(&full,Away,0,true,73,true,1,true,false);
			if (!SubmitPrepared(checks,renderer,full,&control,true)) return;
			checks.Expect(control==optimized,"production Solid clear with suppressed root exactly equals full root at every entrance phase including title/border");
			bool edges=true;
			for (UINT y=0;y<Height;++y) for (UINT x=0;x<Width;++x)
				if (y<32 || x<2 || y<2 || x>=Width-2 || y>=Height-2)
				{ ++edgePixels; edges &= control[static_cast<size_t>(y)*Width+x]==optimized[static_cast<size_t>(y)*Width+x]; }
			checks.Expect(edges,"root alpha suppression leaves titlebar and rounded outside border pixels exactly unchanged");
			++comparisons;
		}
		const auto staticInfo=Frame(&state,Away,0,false,73,true,1,true,true);
		checks.Expect(!staticInfo.rootSuppressed,"finished/disabled static homepage retains legacy root rendering");
		const float originalAlpha=ImGui::GetStyle().Alpha;
		const ImVec4 originalBackground=ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
		for (int fallback=0;fallback<3;++fallback)
		{
			const bool solid=fallback!=0;
			ImGui::GetStyle().Alpha=fallback==1 ? .6F : originalAlpha;
			ImGui::GetStyle().Colors[ImGuiCol_WindowBg]=originalBackground;
			if (fallback!=1) ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w=fallback==0 ? 0.0F : .6F;
			state.entry=Design::HomeMotionState::Entry::Playing; state.entryElapsed=Timing::DockStart+Timing::Dock*.5F;
			const auto info=Frame(&state,Away,0,true,73,true,1,solid,true);
			if (!SubmitPrepared(checks,renderer,state,&optimized,solid)) break;
			auto full=state; Frame(&full,Away,0,true,73,true,1,solid,false);
			if (!SubmitPrepared(checks,renderer,full,&control,solid)) break;
			checks.Expect(!info.rootSuppressed && optimized==control,
				"Mica/transparent backdrop, partial style alpha and partial background alpha take unchanged full-root fallback");
		}
		ImGui::GetStyle().Alpha=originalAlpha; ImGui::GetStyle().Colors[ImGuiCol_WindowBg]=originalBackground;
		std::cout<<"[HomeWARP] solid-root theme="<<(Design::IsDarkMode() ? "dark" : "light")
			<<" exact-phase-comparisons="<<comparisons<<" exact-title-border-pixels="<<edgePixels<<" fallback-cases=3\n";
	}

	void CheckPreparedKeyboard(Checks& checks, Offscreen& renderer)
	{
		Design::HomeMotionState state; FrameInfo info;
		// 跨多次缺席帧后，用真实 Tab 取消并恢复普通 child 导航；不伪造 Focus/ID 来掩盖问题。
		auto& io=ImGui::GetIO(); const auto oldFlags=io.ConfigFlags; const bool oldTrickle=io.ConfigInputTrickleEventQueue;
		io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard; io.ConfigInputTrickleEventQueue=false;
		for (int frame=0;frame<16;++frame)
		{
			info=Frame(&state,Away,.02F,true,42,true);
			if (!SubmitPrepared(checks,renderer,state)) break;
		}
		checks.Expect(info.skipped,"actual keyboard recovery fixture starts after opaque skipped child frames");
		io.AddKeyEvent(ImGuiKey_Tab,true); info=Frame(&state,Away,.02F,true,42,true); SubmitPrepared(checks,renderer,state);
		checks.Expect(!state.EntryActive() && !info.skipped && info.action==Design::HomeAction::None,
			"real Tab cancels fast entry and cannot activate underlying action on cancellation frame");
		io.AddKeyEvent(ImGuiKey_Tab,false); Frame(&state,Away,.02F,true,42,true); SubmitPrepared(checks,renderer,state);
		auto* root=ImGui::FindWindowByName("##home-warp-production-root"); bool reachable=false;
		for (int step=0;step<10 && !reachable;++step)
		{
			io.AddKeyEvent(ImGuiKey_Tab,true); Frame(&state,Away,.02F,true,42,true); SubmitPrepared(checks,renderer,state);
			io.AddKeyEvent(ImGuiKey_Tab,false); Frame(&state,Away,.02F,true,42,true); SubmitPrepared(checks,renderer,state);
			reachable=GImGui->NavWindow && GImGui->NavWindow->RootWindow==root && GImGui->NavId!=0;
		}
		checks.Expect(reachable,"real keyboard focus reaches setting root children again after fast entry without mouse/forced focus");
		io.ConfigFlags=oldFlags; io.ConfigInputTrickleEventQueue=oldTrickle;
		// 使用真实鼠标位移退出导航 hover/focus 绘制，避免键盘夹具污染后续独立性能对照。
		Frame(&state,{Away.x+2,Away.y+2},0,false,42,true); SubmitPrepared(checks,renderer,state);
		Frame(&state,Away,0,false,42,true); SubmitPrepared(checks,renderer,state);
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

	void CheckWritingInk(Checks& checks, Offscreen& renderer, const Design::HomeMotionState& state)
	{
		// 从真实入场前景提取六笔前缀与活动圆头，保留透明清屏，避免纯色覆盖掩盖内部裂缝。
		const auto pose=Design::ResolveHomeEntryPose(state); const auto& mesh=Brand::Geometry();
		const float nativeScale=pose.width/mesh.size.x; const auto* data=ImGui::GetDrawData();
		const auto* source=ImGui::GetForegroundDrawList(); int begin=-1;
		for (int i=0;i+static_cast<int>(mesh.vertices.size())<=source->VtxBuffer.Size;++i)
		{
			const auto& v=source->VtxBuffer[i]; const auto& expected=mesh.vertices.front();
			if (v.pos.x==pose.origin.x+expected.pos.x*nativeScale && v.pos.y==pose.origin.y+expected.pos.y*nativeScale
				&& v.col==(Design::IsDarkMode() ? expected.dark : expected.light)) { begin=i; break; }
		}
		if (!checks.Expect(begin>=0 && pose.writing>0 && pose.writing<1,"locate actual incomplete foreground stroke vertices")) return;
		std::vector<unsigned> submitted;
		ImDrawList extracted(ImGui::GetDrawListSharedData()); extracted._ResetForNewFrame();
		extracted.PushClipRect({0,0},{static_cast<float>(Width),static_cast<float>(Height)});
		bool texturePushed=false;
		for (const auto& command : source->CmdBuffer)
		{
			if (command.UserCallback) continue;
			for (unsigned i=command.IdxOffset;i+2<command.IdxOffset+command.ElemCount;i+=3)
			{
				const unsigned a=command.VtxOffset+source->IdxBuffer[static_cast<int>(i)],
					b=command.VtxOffset+source->IdxBuffer[static_cast<int>(i+1)],c=command.VtxOffset+source->IdxBuffer[static_cast<int>(i+2)];
				if (a<static_cast<unsigned>(begin) || b<static_cast<unsigned>(begin) || c<static_cast<unsigned>(begin)) continue;
				if (!texturePushed) { extracted.PushTexture(command.GetTexID()); texturePushed=true; }
				extracted.PrimReserve(3,3);
				for (unsigned index : {a,b,c})
				{
					submitted.push_back(index-begin); const auto& v=source->VtxBuffer[static_cast<int>(index)];
					extracted.PrimWriteIdx(static_cast<ImDrawIdx>(extracted._VtxCurrentIdx)); extracted.PrimWriteVtx(v.pos,v.uv,v.col);
				}
			}
		}
		if (texturePushed) extracted.PopTexture(); extracted.PopClipRect();
		std::vector<bool> referenced(mesh.vertices.size(),false);
		for (unsigned index : submitted) if (index<referenced.size()) referenced[index]=true;
		std::array<const Brand::Mesh::Section*,6> tips{}; unsigned completed=0; bool prefix=true;
		for (size_t strokeIndex=0;strokeIndex<mesh.strokes.size();++strokeIndex)
		{
			const auto& stroke=mesh.strokes[strokeIndex]; float latest=-1;
			for (const auto& section : stroke.sections)
				if (std::any_of(section.vertices.begin(),section.vertices.end(),[&](unsigned index) { return referenced[index]; }))
					if (section.progress>=latest) { latest=section.progress; tips[strokeIndex]=&section; }
			const float phase=Brand::Smooth((pose.writing-static_cast<float>(strokeIndex)*.012F)/(1-static_cast<float>(strokeIndex)*.012F));
			checks.Expect(tips[strokeIndex] && latest>0 && latest<=phase && latest<1,
				"all six actual strokes have partial prefixes with no future section or premature terminal cap");
			for (const auto& block : stroke.blocks) if (block.progress<=phase)
				for (unsigned i=block.first;i<block.first+block.count;++i)
				{ prefix &= completed<submitted.size() && submitted[completed]==mesh.indices[i]; ++completed; }
		}
		checks.Expect(prefix && completed>0 && submitted.size()==completed+7*108,
			"actual foreground indices contain only completed original blocks and seven bounded activity caps");
		ImDrawData onlyInk; onlyInk.Valid=true; onlyInk.DisplayPos=data->DisplayPos;
		onlyInk.DisplaySize=data->DisplaySize; onlyInk.FramebufferScale=data->FramebufferScale; onlyInk.AddDrawList(&extracted);
		std::vector<ImVec2> original; original.reserve(extracted.VtxBuffer.Size);
		for (const auto& v : extracted.VtxBuffer) original.push_back(v.pos);
		for (float zoom : {1.0F,1.875F,4.0F})
		{
			int tested=0,gaps=0;
			for (const auto* tip : tips)
			{
				if (!tip) continue;
				const ImVec2 center(pose.origin.x+tip->center.x*nativeScale,pose.origin.y+tip->center.y*nativeScale);
				for (int i=0;i<extracted.VtxBuffer.Size;++i)
					extracted.VtxBuffer[i].pos={500+(original[i].x-center.x)*zoom,500+(original[i].y-center.y)*zoom};
				Pixels enlarged; if (!renderer.Submit(checks,&onlyInk,&enlarged)) return;
				const float radius=tip->radius*nativeScale*zoom; const int extent=static_cast<int>(std::ceil(radius));
				for (int y=500-extent;y<=500+extent;++y) for (int x=500-extent;x<=500+extent;++x)
				{
					const float dx=x+.5F-500,dy=y+.5F-500,along=dx*tip->normal.y-dy*tip->normal.x;
					if (std::hypot(dx,dy)>=radius-1.3F || along<-.5F) continue;
					++tested; if ((enlarged[static_cast<size_t>(y)*Width+x]>>24)<250) ++gaps;
				}
			}
			checks.Expect(tested>60 && gaps==0,"all six real active cap/shaft joins remain opaque on transparent WARP readback at multiple scales");
			std::cout<<"[HomeWARP] writing-caps zoom="<<zoom<<" solid-samples="<<tested<<" gaps="<<gaps<<'\n';
		}
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
		checks.Expect(geometry && vertexAlpha && alphaChanges==0 && changed==0 && maximum==0,
			"actual text behind entrance retains exact geometry, UV, RGBA and antialiasing");
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

	void Benchmark(Checks& checks, Offscreen& renderer, const char* name, int mode, const FrameInfo& baseline, bool optimized = false, bool solidClear = false, bool suppressRoot = false)
	{
		Design::HomeMotionState state;
		Frame(&state, Away, 0, false);
		if (mode>=4)
		{
			state.entry=Design::HomeMotionState::Entry::Playing; state.entryElapsed=1.6F; Frame(&state,Away,0,true);
			state.entry=Design::HomeMotionState::Entry::Playing; state.entryElapsed=1.28F; Frame(&state,Away,0,true);
		}
		if (optimized)
		{
			for (int frame=0;frame<6;++frame)
			{
				state.entry=Design::HomeMotionState::Entry::Playing; state.entryElapsed=Design::HomeEntryTiming::DockStart;
				Frame(&state,Away,0,true,1,true,1,solidClear,suppressRoot);
				if (!SubmitPrepared(checks,renderer,state,nullptr,solidClear)) return;
			}
		}
		const ImVec2 center(state.brandOrigin.x + state.brandWidth * .5F, state.brandOrigin.y + Brand::Height(state.brandWidth) * .5F);
		std::vector<double> cpu, submit; cpu.reserve(100); submit.reserve(100);
		AllocationScope allocations;
		auto capacity=[] {
			const auto& mesh=Brand::Geometry(); size_t total=mesh.vertices.capacity()+mesh.indices.capacity();
			for (const auto& stroke : mesh.strokes) total+=stroke.blocks.capacity()+stroke.sections.capacity();
			return total;
		};
		const auto geometryCapacity=capacity();
		std::uint64_t allocatedBefore = 0, cacheBefore = 0, rebuiltBefore = 0;
		FrameInfo info;
		for (int frame = -20; frame < 100; ++frame)
		{
			if (frame == 0) { allocatedBefore = allocations.allocations; cacheBefore = state.cache.allocations; rebuiltBefore = state.cache.rebuilds; }
			ImVec2 mouse = Away;
			if (mode == 2) mouse = { center.x + std::sin(static_cast<float>(frame) * .2F) * state.brandWidth * .6F,
				center.y + std::cos(static_cast<float>(frame) * .17F) * Brand::Height(state.brandWidth) * .6F };
			// 回正每周期先通过真实鼠标追踪建立偏转，再记录鼠标离开后的帧。
			if (mode == 3 && frame % 20 == 0)
				for (int settle = 0; settle < 20; ++settle) Frame(&state, { center.x + state.brandWidth * .5F, center.y }, .05F);
			if (mode>=4)
			{
				state.entry=Design::HomeMotionState::Entry::Playing;
				state.entryElapsed=mode==4 ? .26F+1.0F*((frame+20)%100)/100
					: mode==5 ? 1.6F : mode==6 ? 1.92F+.50F*((frame+20)%100)/100 : 2.46F+.18F*((frame+20)%100)/100;
			}
			const auto begin = Clock::now();
			info = Frame(mode == 0 ? nullptr : &state, mouse, mode>=4 ? 0 : 1.0F / 60.0F, true,1,optimized,1,solidClear,suppressRoot);
			const auto drawn = Clock::now();
			if (!renderer.Submit(checks, ImGui::GetDrawData(),nullptr,S_OK,solidClear)) return;
			if (optimized) Design::CompleteHomeEntryFrame(state,true,Design::HomeEntryTexturesReady(ImGui::GetDrawData()));
			const auto completed = Clock::now();
			if (frame >= 0) { cpu.push_back(Micros(begin, drawn)); submit.push_back(Micros(drawn, completed)); }
			checks.Expect(info.vertices<=baseline.vertices+165 && info.indices<=baseline.indices+7*108+6
				&& info.batches<=baseline.batches+1,
				"entrance adds only a cover and bounded activity caps with one foreground draw batch");
		}
		checks.Expect(allocations.allocations==allocatedBefore,"warmed actual homepage motion performs no additional ImGui allocations");
		checks.Expect(state.cache.allocations==cacheBefore,"warm original brand cache never grows during benchmark");
		checks.Expect(capacity()==geometryCapacity,"all original stroke/index/section capacities remain fixed during actual motion submissions");
		if (mode == 1 || mode >=4) checks.Expect(state.cache.rebuilds == rebuiltBefore, "idle and entrance reuse original curve geometry");
		std::cout << "[HomeWARP] benchmark=" << name << " frames=100 cpu-us median=" << Percentile(cpu, .5)
			<< " p95=" << Percentile(cpu, .95) << " submit-complete-copy-us median=" << Percentile(submit, .5)
			<< " p95=" << Percentile(submit, .95) << " vertices=" << info.vertices << " indices=" << info.indices
			<< " batches=" << info.batches << " imgui-allocs=" << allocations.allocations - allocatedBefore
			<< " opaque-fast="<<info.skipped<<" solid-clear="<<solidClear<<" root-suppressed="<<info.rootSuppressed<<" prep-state-bytes="<<sizeof(state.preparation)
			<<" cache-growth=" << state.cache.allocations - cacheBefore << " cache-rebuilds=" << state.cache.rebuilds - rebuiltBefore << '\n';
	}
}

int RunSettingHomeWarpProbe(ImFont* regular, ImFont* strong, ImFont* icons)
{
	Checks checks; Offscreen renderer;
	if (!checks.Expect(ImGui::GetIO().BackendRendererUserData == nullptr && ImGui::GetFrameCount() == 0,
		"probe runs before any CPU fake texture materialization") || !renderer.Initialize(checks)) return checks.failures;
	Design::BindTextFonts(regular, strong); ImFluent::SetIconFont(icons);
	CheckColdEntry(checks, renderer);
	CheckBackendFailures(checks, renderer);
	for (bool dark : { false, true })
	{
		Design::ApplyPalette(dark); Design::HomeMotionState state;
		Pixels baseline; if (!StableBaseline(checks, renderer, state, baseline)) return checks.failures;
		checks.Expect(state.brandVisible && state.brandWidth > 400, "production brand fully visible at benchmark viewport");
		CheckPreparedEntry(checks,renderer,baseline);
		Frame(&state,Away,0,false); renderer.Submit(checks,ImGui::GetDrawData());
		CheckTransparentProductionInk(checks, renderer, state);
		CheckProductionText(checks,renderer,state);
		Snapshot(dark ? "soft-dark-static" : "soft-light-static",baseline);
		Pixels changed;
		Frame(&state,Away,0,true,2);
		renderer.Submit(checks,ImGui::GetDrawData(),&changed);
		checks.Expect(state.EntryActive() && state.entryElapsed==0,"new Show serial starts full-window entry at zero");
		const auto openingRebuilds=state.cache.rebuilds; const auto openingBounds=state.buttonBounds;
		Pixels held;
		for (float time : {.26F,.76F,1.30F,1.60F,1.898F,2.16F,2.54F})
		{
			state.entryElapsed=time; Frame(&state,Away,0,true,2);
			renderer.Submit(checks,ImGui::GetDrawData(),&changed);
			const auto pose=Design::ResolveHomeEntryPose(state);
			if (time==.76F) CheckWritingInk(checks,renderer,state);
			int outside=0,navChanged=0;
			for (UINT y=0;y<Height;++y) for (UINT x=0;x<Width;++x)
			{
				const auto index=static_cast<size_t>(y)*Width+x;
				if (changed[index]!=baseline[index]) { outside+=y<32; navChanged+=x<200 && y>=32; }
			}
			checks.Expect(outside==0,"full-window foreground excludes titlebar exactly");
			if (time<Design::HomeEntryTiming::SettleStart) checks.Expect(navChanged>100,"entry cover includes actual navigation, not only hero background");
			if (time>=Design::HomeEntryTiming::WritingEnd && time<Design::HomeEntryTiming::DockStart)
			{
				checks.Expect(pose.opacity==1 && pose.writing==1,"completed original six strokes stay opaque throughout 600ms hold");
				if (held.empty()) held=changed; else checks.Expect(held==changed,"entire WARP readback is identical during opaque full-logo hold");
			}
			for (size_t i=0;i<openingBounds.size();++i)
				checks.Expect(std::memcmp(&openingBounds[i],&state.buttonBounds[i],sizeof(ImVec4))==0,"entry never moves actual Fluent button hit rectangles");
			CheckProductionText(checks,renderer,state,true);
			Snapshot((std::string(dark ? "entry-dark-" : "entry-light-")+std::to_string(static_cast<int>(time*1000))).c_str(),changed);
		}
		checks.Expect(state.cache.rebuilds==openingRebuilds,"writing and docking never sample curves or rebuild the parallax cache");
		for (int i = 0; i < 16; ++i) Frame(&state, Away,1.0F/60,true,2);
		renderer.Submit(checks, ImGui::GetDrawData(), &changed);
		checks.Expect(changed == baseline && !state.EntryActive(), "finished entry returns exactly to original static page RGBA");
		CheckTransparentProductionInk(checks,renderer,state,true);
		for (ImVec2 corner : { ImVec2(-1, -1), ImVec2(1, -1), ImVec2(-1, 1), ImVec2(1, 1) })
		{
			const ImVec2 mouse(state.brandOrigin.x + state.brandWidth * (.5F + corner.x * .6F),
				state.brandOrigin.y + Brand::Height(state.brandWidth) * (.5F + corner.y * .6F));
			for (int i = 0; i < 60; ++i) Frame(&state, mouse,1.0F/60,true,2);
			renderer.Submit(checks, ImGui::GetDrawData(), &changed); CheckDifference(checks, baseline, changed, state, false);
		}
		Frame(&state, Away, 0, false,2); renderer.Submit(checks, ImGui::GetDrawData(), &changed);
		checks.Expect(changed == baseline, "disabling animations restores exact static pixels");
		CheckSolidRootBackground(checks,renderer);
	}
	CheckPreparedKeyboard(checks,renderer);
	Design::ApplyPalette(false);
	FrameInfo baseline; for (int i = 0; i < 20; ++i) baseline = Frame(nullptr, Away);
	for (int mode = 0; mode < 8; ++mode)
		Benchmark(checks, renderer, std::array{ "static", "idle", "moving", "returning", "writing", "hold", "dock", "settle" }[mode], mode, baseline);
	for (int mode : {4,5,6,7})
		Benchmark(checks,renderer,std::array{"writing-fast","hold-fast","dock-prepared","settle-prepared"}[mode-4],mode,baseline,true);
	for (int mode : {4,5,6,7})
	{
		Benchmark(checks,renderer,std::array{"solid-writing-full-root","solid-hold-full-root","solid-dock-full-root","solid-settle-full-root"}[mode-4],mode,baseline,true,true,false);
		Benchmark(checks,renderer,std::array{"solid-writing-suppressed-root","solid-hold-suppressed-root","solid-dock-suppressed-root","solid-settle-suppressed-root"}[mode-4],mode,baseline,true,true,true);
	}
	std::cout << "[HomeWARP] CPU excludes WARP submission; completed submission includes copy/Map wait. "
		"ImGui allocator counts exclude existing std::string/vector work; brand cache growth is counted separately.\n";
	return checks.failures;
}
