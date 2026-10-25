#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include "setting_home_warp_tests.h"
#include "../Inkeys/Inkeys/UI/Setting/Setting.Pages.h"
#include "../Inkeys/Inkeys/UI/Setting/Setting.HomeEntryCache.h"
#include "../Inkeys/Inkeys/UI/Setting/Setting.FrameSubmission.h"
#include <dxgi.h>
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
#ifdef _DEBUG
	constexpr const char* BuildName="Debug";
#else
	constexpr const char* BuildName="Release";
#endif
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

	struct Offscreen;
	Offscreen* activeRenderer=nullptr;
	struct Offscreen
	{
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		ComPtr<ID3D11Texture2D> target, staging;
		ComPtr<ID3D11RenderTargetView> view;
		bool initialized = false;
		Design::HomeEntryPageCache pageCache;
		Design::FrameSubmissionCache submittedFrame;
		struct Counters { std::uint64_t submissions=0, skips=0, clears=0, copies=0, captures=0; } counters;
		bool frameSkipped=false;
		bool frameClearOmitted=false;
		double compareUs=0;
		const char* driverName="warp";
		ComPtr<ID3D11Query> completed;
		double backendUs=0, waitUs=0;
		ImGuiBackendFlags oldBackendFlags = ImGui::GetIO().BackendFlags;
		ImVec2 oldDisplay = ImGui::GetIO().DisplaySize;
		ImVec2 oldFramebufferScale = ImGui::GetIO().DisplayFramebufferScale;
		float oldDelta = ImGui::GetIO().DeltaTime;
		ImGuiStyle oldStyle = ImGui::GetStyle();
		bool oldDark = Design::IsDarkMode();

		bool Initialize(Checks& checks, D3D_DRIVER_TYPE driver=D3D_DRIVER_TYPE_WARP)
		{
			// 显式限于 Win7 平台更新可用的 FL11.0；不请求 swap chain 或窗口。
			const D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_11_0;
			D3D_FEATURE_LEVEL actual{};
			const HRESULT create=D3D11CreateDevice(nullptr, driver, nullptr,
				D3D11_CREATE_DEVICE_BGRA_SUPPORT, &requested, 1, D3D11_SDK_VERSION,
				device.GetAddressOf(), &actual, context.GetAddressOf());
			if (FAILED(create) && driver==D3D_DRIVER_TYPE_HARDWARE)
			{
				std::cout<<"[HomeOffscreen] driver=hardware unavailable HRESULT=0x"<<std::hex<<static_cast<unsigned long>(create)<<std::dec<<'\n';
				return false;
			}
			if (!checks.Result(create,"create no-window FL11.0 device")) return false;
			checks.Expect(actual == requested, "offscreen driver uses explicitly requested FL11.0");
			driverName=driver==D3D_DRIVER_TYPE_WARP ? "warp" : "hardware";
			ComPtr<IDXGIDevice> dxgi; ComPtr<IDXGIAdapter> adapter; DXGI_ADAPTER_DESC adapterDesc{};
			if (SUCCEEDED(device.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(adapter.GetAddressOf()))
				&& SUCCEEDED(adapter->GetDesc(&adapterDesc)))
				std::wcout<<L"[HomeOffscreen] driver="<<driverName<<L" adapter="<<adapterDesc.Description
					<<L" vendor="<<adapterDesc.VendorId<<L" device="<<adapterDesc.DeviceId
					<<L" feature-level=11.0 width="<<Width<<L" height="<<Height<<L"\n";
			D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};
			if (!checks.Result(device->CreateQuery(&query,completed.GetAddressOf()),"create bounded render completion query")) return false;
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
			activeRenderer=this;
			return checks.Expect(ImGui_ImplDX11_CreateDeviceObjects(), "load exact production embedded CSO resources");
		}

		~Offscreen()
		{
			pageCache.Reset(); if (activeRenderer==this) activeRenderer=nullptr;
			if (initialized) ImGui_ImplDX11_Shutdown();
			if (context) { context->ClearState(); context->Flush(); }
			auto& io = ImGui::GetIO(); io.BackendFlags = oldBackendFlags;
			io.DisplaySize = oldDisplay; io.DisplayFramebufferScale = oldFramebufferScale; io.DeltaTime = oldDelta;
			Design::ApplyPalette(oldDark); ImGui::GetStyle() = oldStyle;
			Design::ConfigureRowAnimations(0, false, 1);
			io.AddMousePosEvent(Away.x, Away.y);
		}

		Design::FrameSubmissionKey SubmissionKey(bool solidClear, const Design::HomeMotionState* state) const
		{
			const auto color=ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
			Design::FrameSubmissionKey key;
			key.output=state && state->preparation.keyValid ? state->preparation.key
				: Design::MakeHomeEntryFrameKey({0,Design::Pixels(32),static_cast<float>(Width),static_cast<float>(Height)},0,
					1,1,0,ImGui::GetIO().Fonts->Fonts[0],nullptr,nullptr,solidClear ? 1 : 0);
			key.clear={solidClear ? color.x*color.w : 0,solidClear ? color.y*color.w : 0,
				solidClear ? color.z*color.w : 0,solidClear ? color.w : 0};
			key.showSerial=state ? state->showSerial : 0;
			key.target=reinterpret_cast<std::uintptr_t>(view.Get()); key.cacheRevision=pageCache.Revision();
			return key;
		}

		bool Readback(Checks& checks, Pixels* pixels)
		{
			context->CopyResource(staging.Get(),target.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (!checks.Result(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"complete separate correctness readback")) return false;
			if (pixels)
			{
				pixels->resize(static_cast<size_t>(Width)*Height);
				for (UINT row=0;row<Height;++row)
					std::memcpy(pixels->data()+static_cast<size_t>(row)*Width,
						static_cast<const char*>(mapped.pData)+static_cast<size_t>(row)*mapped.RowPitch,Width*4);
			}
			context->Unmap(staging.Get(),0); return true;
		}

		bool Submit(Checks& checks, ImDrawData* data, Pixels* pixels = nullptr, HRESULT expectedResult = S_OK,
			bool solidClear = false, Design::HomeMotionState* state = nullptr, bool deduplicate = false, bool forceCopy = false)
		{
			frameSkipped=false; compareUs=backendUs=waitUs=0;
			if (!deduplicate) submittedFrame.Reset();
			const auto key=SubmissionKey(solidClear,state);
			const auto compareBegin=Clock::now();
			const bool duplicate=deduplicate && submittedFrame.CanSkip(data,key,
				expectedResult!=S_OK || GImGui->InputEventsTrail.Size>0 || FAILED(ImGui_ImplDX11_GetLastResult())
				|| FAILED(device->GetDeviceRemovedReason()) || (state && Design::RequiresHomeEntrySubmission(*state)));
			compareUs=std::chrono::duration<double,std::micro>(Clock::now()-compareBegin).count();
			if (duplicate)
			{
				// 保留CPU帧/真实时钟；这里不清屏、不复制、不上传、不绘制，也不声称完成一次GPU帧。
				frameSkipped=true; ++counters.skips;
				return !pixels || Readback(checks,pixels);
			}
			++counters.submissions;
			const auto backendBegin=Clock::now();
			ID3D11RenderTargetView* rtv = view.Get();
			context->OMSetRenderTargets(1, &rtv, nullptr);
			if (!frameClearOmitted) { context->ClearRenderTargetView(rtv,key.clear.data()); ++counters.clears; }
			bool captured=false; Design::HomeEntryPageCache::RenderStats stats;
			HRESULT result;
			if (state) result=pageCache.Render(device.Get(),context.Get(),rtv,*state,data,ImGui::GetForegroundDrawList(),captured,&stats,forceCopy);
			else { ImGui_ImplDX11_RenderDrawData(data); result=ImGui_ImplDX11_GetLastResult(); }
			counters.copies+=stats.copies; counters.captures+=stats.captures;
			const auto backendEnd=Clock::now();
			backendUs=std::chrono::duration<double,std::micro>(backendEnd-backendBegin).count();
			if (state) Design::CompleteHomeEntryFrame(*state,SUCCEEDED(result),Design::HomeEntryTexturesReady(data),captured);
			if (state && (!state->EntryActive() || FAILED(result))) pageCache.Reset();
			if (FAILED(result) || expectedResult!=S_OK) submittedFrame.Reset();
			if (!checks.ExpectedResult(result, expectedResult, "production backend preserves expected render HRESULT")) return false;
			if (!pixels && expectedResult==S_OK)
			{
				// EVENT+Flush 只等待本帧完成，不含 CPU readback；有界忙等且不 Sleep/Present/限频。
				context->End(completed.Get()); context->Flush();
				BOOL done=FALSE; HRESULT completion=S_FALSE;
				const auto deadline=backendEnd+std::chrono::seconds(10);
				while (completion==S_FALSE && Clock::now()<deadline)
					completion=context->GetData(completed.Get(),&done,sizeof(done),D3D11_ASYNC_GETDATA_DONOTFLUSH);
				waitUs=std::chrono::duration<double,std::micro>(Clock::now()-backendEnd).count();
				if (!checks.Expect(completion==S_OK && done,"bounded EVENT confirms draw/copy execution completed"))
				{ submittedFrame.Reset(); return false; }
				if (deduplicate) submittedFrame.ConfirmPresented(data,SubmissionKey(solidClear,state));
				return true;
			}
			if (!Readback(checks,pixels)) { submittedFrame.Reset(); return false; }
			if (deduplicate && result==S_OK) submittedFrame.ConfirmPresented(data,SubmissionKey(solidClear,state));
			return true;
		}
	};

	enum class OverlaySubset { All, CoverOnly, InkOnly, None };
	bool SelectOverlaySubset(ImDrawData* data, const Design::HomeMotionState& state, OverlaySubset subset)
	{
		if (subset==OverlaySubset::All) return true;
		auto* overlay=ImGui::GetForegroundDrawList();
		const bool covered=Design::ResolveHomeEntryPose(state).opacity>0;
		if (subset==OverlaySubset::None || (subset==OverlaySubset::CoverOnly && !covered))
		{
			int count=0;
			for (auto* list : data->CmdLists) if (list!=overlay) data->CmdLists[count++]=list;
			data->CmdLists.resize(count); data->CmdListsCount=count;
		}
		else if (covered)
		{
			// 仅消融测试切分生产矩形的首个四顶点/六索引；原始字标网格不重采样。
			ImDrawCmd* command=nullptr;
			for (auto& candidate : overlay->CmdBuffer)
			{
				if (candidate.UserCallback || candidate.VtxOffset!=0) return false;
				if (candidate.ElemCount) { if (command) return false; command=&candidate; }
			}
			if (!command || command->IdxOffset!=0 || overlay->VtxBuffer.Size<4 || overlay->IdxBuffer.Size<6) return false;
			const auto bounds=state.clientContent;
			if (overlay->VtxBuffer[0].pos.x!=bounds.x || overlay->VtxBuffer[0].pos.y!=bounds.y
				|| overlay->VtxBuffer[2].pos.x!=bounds.z || overlay->VtxBuffer[2].pos.y!=bounds.w) return false;
			for (int index=0;index<6;++index) if (overlay->IdxBuffer[index]>=4) return false;
			const int vertexBegin=subset==OverlaySubset::InkOnly ? 4 : 0;
			const int indexBegin=subset==OverlaySubset::InkOnly ? 6 : 0;
			const int vertexCount=subset==OverlaySubset::InkOnly ? overlay->VtxBuffer.Size-4 : 4;
			const int indexCount=subset==OverlaySubset::InkOnly ? overlay->IdxBuffer.Size-6 : 6;
			std::memmove(overlay->VtxBuffer.Data,overlay->VtxBuffer.Data+vertexBegin,vertexCount*sizeof(ImDrawVert));
			std::memmove(overlay->IdxBuffer.Data,overlay->IdxBuffer.Data+indexBegin,indexCount*sizeof(ImDrawIdx));
			overlay->VtxBuffer.resize(vertexCount); overlay->IdxBuffer.resize(indexCount);
			// AddDrawList 会验证内部写入游标；裁剪已完成的输出后同步列表合同，保留原 sanity checks。
			overlay->_VtxWritePtr=overlay->VtxBuffer.Data+vertexCount;
			overlay->_IdxWritePtr=overlay->IdxBuffer.Data+indexCount;
			overlay->_VtxCurrentIdx=static_cast<unsigned>(vertexCount);
			for (auto& index : overlay->IdxBuffer) index=static_cast<ImDrawIdx>(index-vertexBegin);
			const auto selected=*command; overlay->CmdBuffer.resize(1); overlay->CmdBuffer[0]=selected;
			overlay->CmdBuffer[0].ElemCount=indexCount; overlay->CmdBuffer[0].IdxOffset=0;
		}
		data->TotalVtxCount=data->TotalIdxCount=0;
		for (const auto* list : data->CmdLists)
		{ data->TotalVtxCount+=list->VtxBuffer.Size; data->TotalIdxCount+=list->IdxBuffer.Size; }
		return true;
	}

	bool PrepareExperimentalHomeEntryTargetClear(const Design::HomeMotionState& state, bool solidBackdrop,
		float styleAlpha, ImVec4 windowBackground)
	{
		// 仅离屏实验：实测无稳定收益，产品仍对每个真实提交使用原 ClearRenderTargetView。
		if (!Design::CanOmitHomeEntryRootBackground(state,solidBackdrop,styleAlpha,windowBackground)) return false;
		const ImU32 color=ImGui::ColorConvertFloat4ToU32(windowBackground);
		const auto quantized=ImGui::ColorConvertU32ToFloat4(color);
		if (windowBackground.x!=quantized.x || windowBackground.y!=quantized.y || windowBackground.z!=quantized.z) return false;
		const auto& prep=state.preparation;
		if (!prep.ready || (!prep.revealReady && (!prep.skipBackground || Design::ResolveHomeEntryPose(state).opacity!=1))) return false;
		const auto& io=ImGui::GetIO(); const auto* viewport=ImGui::GetMainViewport();
		const auto bounds=state.clientContent; const auto pos=viewport->Pos, scale=io.DisplayFramebufferScale;
		if (viewport->Size.x!=io.DisplaySize.x || viewport->Size.y!=io.DisplaySize.y
			|| bounds.x!=pos.x || bounds.z!=pos.x+io.DisplaySize.x || bounds.w!=pos.y+io.DisplaySize.y
			|| !(bounds.y>=pos.y && bounds.y<bounds.w) || !(scale.x>0 && scale.y>0)) return false;
		auto integral=[](float value) { return std::isfinite(value) && value==std::floor(value); };
		if (!integral(io.DisplaySize.x*scale.x) || !integral(io.DisplaySize.y*scale.y)
			|| !integral((bounds.y-pos.y)*scale.y)) return false;
		ImGui::GetBackgroundDrawList()->AddRectFilled(pos,{bounds.z,bounds.y},color);
		return true;
	}

	struct EntryLayoutProbe
	{
		ImVec2 viewport{static_cast<float>(Width),static_cast<float>(Height)};
		const char* root="##home-warp-production-root";
		bool animated=true, settle=true, blockInput=false;
	};

	std::array<float,8> LayoutDiagnostics(ImGuiWindow* window)
	{
		if (!window) return {};
		return {window->ContentSize.y, static_cast<float>(ImTrunc64(window->DC.CursorMaxPos.y-window->DC.CursorStartPos.y)),
			window->ContentSizeIdeal.y, window->ScrollMax.y, window->Scroll.y, window->InnerRect.GetHeight(),
			window->StateStorage.GetFloat(window->GetID("##scrollbar-thumb-top")),window->StateStorage.GetFloat(window->GetID("##scrollbar-thumb-bottom"))};
	}

	struct FrameInfo { int vertices = 0, indices = 0, batches = 0; ImRect pageClip; bool skipped = false; Design::HomeAction action = Design::HomeAction::None; bool rootSuppressed = false; bool subsetValid = true; bool clearOmitted = false; ImGuiWindow* content = nullptr; float contentWidth = 0; bool inputBlocked = false; ImRect contentWorkRect, pageWorkRect; std::array<float,8> contentLayout{}, pageLayout{}; };
	FrameInfo Frame(Design::HomeMotionState* motion, ImVec2 mouse, float delta = 1.0F / 60.0F,
		bool enabled = true, std::uint64_t serial = 1, bool optimized = false, float rate = 1, bool solidBackdrop = false, bool suppressRoot = false,
		OverlaySubset subset = OverlaySubset::All, bool candidateClear = false, const EntryLayoutProbe* layout = nullptr)
	{
		auto& io = ImGui::GetIO(); io.DisplaySize = layout ? layout->viewport : ImVec2(static_cast<float>(Width), static_cast<float>(Height));
		io.DisplayFramebufferScale = { 1, 1 }; io.DeltaTime = 1.0F / 60.0F;
		io.AddMouseSourceEvent(ImGuiMouseSource_Mouse); io.AddMousePosEvent(mouse.x, mouse.y);
		io.AddMouseButtonEvent(0, false);
		ImGui_ImplDX11_NewFrame(); ImGui::NewFrame(); Design::ConfigureRowAnimations(layout ? io.DeltaTime : 0, layout && layout->animated, rate);
		if (motion) Design::ConfigureHomeMotion(*motion, true, serial, delta, enabled, rate);
		const bool entryBlocked=(motion && Design::CancelHomeEntryOnInput(*motion)) || (layout && layout->blockInput);
		ImGui::SetNextWindowPos({ 0, 0 }); ImGui::SetNextWindowSize(io.DisplaySize);
		const bool rootSuppressed=suppressRoot && motion && Design::CanOmitHomeEntryRootBackground(*motion,
			solidBackdrop,ImGui::GetStyle().Alpha,ImGui::GetStyle().Colors[ImGuiCol_WindowBg]);
		if (rootSuppressed) ImGui::SetNextWindowBgAlpha(0);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
		ImGui::Begin(layout ? layout->root : "##home-warp-production-root", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PopStyleVar();
		Design::NavigationState navigation; navigation.animationsEnabled=false;
		Design::AdvanceNavigationAnimations(navigation,0,false,1);
		const auto shell=Design::ResolveShellGeometry(io.DisplaySize.x/Design::Pixels(1),io.DisplaySize.y/Design::Pixels(1),navigation);
		ImGui::GetWindowDrawList()->AddRectFilled({0,0},{static_cast<float>(Width),Design::Pixels(32)},Design::AppBase);
		auto* rootDraw=ImGui::GetWindowDrawList();
		bool skipped=false;
		if (optimized && motion)
		{
			const auto key=Design::MakeHomeEntryFrameKey({Design::Pixels(shell.content.left),Design::Pixels(shell.content.top),
				Design::Pixels(shell.content.right),Design::Pixels(shell.content.bottom)},Design::Pixels(shell.gutter),
				1,1,0,ImGui::GetFont(),nullptr,nullptr,solidBackdrop ? 1 : 0);
			if (activeRenderer) activeRenderer->pageCache.Validate(*motion,key);
			skipped=Design::BeginHomeEntryFrame(*motion,key);
		}
		const bool clearOmitted=candidateClear && motion && PrepareExperimentalHomeEntryTargetClear(*motion,solidBackdrop,
			ImGui::GetStyle().Alpha,ImGui::GetStyle().Colors[ImGuiCol_WindowBg]);
		if (activeRenderer) activeRenderer->frameClearOmitted=clearOmitted;
		ImRect pageClip, contentWorkRect, pageWorkRect; ImGuiWindow* content=nullptr; ImGuiWindow* page=nullptr; float contentWidth=0; Design::HomeAction homeAction=Design::HomeAction::None;
		if (!skipped)
		{

		ImGui::SetCursorPos({Design::Pixels(shell.content.left),Design::Pixels(shell.content.top)});
		ImGui::PushStyleColor(ImGuiCol_ChildBg,Design::PageSurface);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{Design::Pixels(shell.gutter),Design::Pixels(28)});
		Design::BeginScrollChild("##setting-content", {Design::Pixels(shell.content.Width()),Design::Pixels(shell.content.Height())}, ImGuiChildFlags_AlwaysUseWindowPadding,entryBlocked ? ImGuiWindowFlags_NoInputs : ImGuiWindowFlags_None,false,
			motion && (!layout || layout->settle) && (motion->EntryActive() || motion->entry==Design::HomeMotionState::Entry::Pending));
		content=ImGui::GetCurrentWindow();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
		const auto outerClip=ImGui::GetCurrentWindow()->ClipRect;
		if (motion) Design::ConfigureHomeEntryViewport(*motion,{0,Design::Pixels(32),io.DisplaySize.x,io.DisplaySize.y},
			{outerClip.Min.x,outerClip.Min.y,outerClip.Max.x,outerClip.Max.y});
		const float available = ImGui::GetContentRegionAvail().x; contentWidth=available;
		const float pageWidth=(std::min)(available,Design::Pixels(Design::PageMaximumWidth));
		ImGui::SetCursorPosX(ImGui::GetCursorPosX()+(available-pageWidth)*.5F);
		Design::BeginScrollChild("##setting-page-content", { pageWidth, 0 }, ImGuiChildFlags_AutoResizeY);
		page=ImGui::GetCurrentWindow(); pageClip = page->ClipRect;
		ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha,1); ImGui::BeginDisabled(entryBlocked);
		homeAction=Design::RenderHome({ 0, 1, "3.0.0-dev.3981", 0, 0, 1 }, motion);
		contentWorkRect=content->WorkRect; pageWorkRect=ImGui::GetCurrentWindow()->WorkRect;
		ImGui::EndDisabled(); ImGui::PopStyleVar();
		ImGui::EndChild();
		if (motion) Design::CompleteHomeEntryLayout(*motion);
		ImGui::EndChild();
		ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha,1); ImGui::BeginDisabled(entryBlocked);
		Design::RenderNavigationPane(shell,navigation,Design::NavigationDestination{});
		ImGui::EndDisabled(); ImGui::PopStyleVar();
		}
		if (motion) Design::RenderHomeEntryOverlay(*motion);
		ImGui::End(); ImGui::Render();
		if (optimized && motion) Design::FilterHomeEntryDrawData(*motion,ImGui::GetDrawData(),rootDraw,nullptr,ImGui::GetForegroundDrawList());
		if (clearOmitted)
		{
			auto* data=ImGui::GetDrawData(); auto* background=ImGui::GetBackgroundDrawList();
			if (std::find(data->CmdLists.begin(),data->CmdLists.end(),background)==data->CmdLists.end())
			{
				// 标准生产Filter仍原样使用；实验额外背景通过正常AddDrawList保留写游标/几何计数合同。
				const int previous=data->CmdLists.Size; data->AddDrawList(background);
				if (data->CmdLists.Size>previous)
					std::rotate(data->CmdLists.Data,data->CmdLists.Data+previous,data->CmdLists.Data+data->CmdLists.Size);
			}
		}
		const bool subsetValid=!motion || SelectOverlaySubset(ImGui::GetDrawData(),*motion,subset);
		const auto* data = ImGui::GetDrawData(); FrameInfo info{ data->TotalVtxCount, data->TotalIdxCount, 0, pageClip, skipped, homeAction, rootSuppressed, subsetValid, clearOmitted, content, contentWidth, entryBlocked, contentWorkRect, pageWorkRect, LayoutDiagnostics(content), LayoutDiagnostics(page) };
		for (const auto* list : data->CmdLists) for (const auto& command : list->CmdBuffer)
			if (command.ElemCount && !command.UserCallback) ++info.batches;
		return info;
	}

	bool SubmitPrepared(Checks& checks, Offscreen& renderer, Design::HomeMotionState& state, Pixels* pixels=nullptr, bool solidClear=false)
	{
		const bool success=renderer.Submit(checks,ImGui::GetDrawData(),pixels,S_OK,solidClear,&state);
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

	void CheckSharedShaderMode(Checks& checks, Offscreen& renderer)
	{
		Frame(nullptr, Away);
		Pixels reference, actual;
		renderer.Submit(checks,ImGui::GetDrawData(),&reference);
		// 宿主残留演示模式时，普通 ImGui 提交仍应逐像素一致，并归还原来的 PS b1。
		float values[20]{}; values[7]=1; values[18]=1;
		D3D11_BUFFER_DESC desc{}; desc.ByteWidth=sizeof(values);
		desc.Usage=D3D11_USAGE_IMMUTABLE; desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
		D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem=values;
		ComPtr<ID3D11Buffer> mode;
		if (!checks.Result(renderer.device->CreateBuffer(&desc,&initial,mode.GetAddressOf()),"create foreign demo mode buffer")) return;
		renderer.context->PSSetConstantBuffers(1,1,mode.GetAddressOf());
		renderer.Submit(checks,ImGui::GetDrawData(),&actual);
		ComPtr<ID3D11Buffer> restored; renderer.context->PSGetConstantBuffers(1,1,restored.GetAddressOf());
		checks.Expect(!reference.empty() && reference==actual,"normal ImGui pixels ignore a foreign eraser demo mode");
		checks.Expect(restored.Get()==mode.Get(),"normal ImGui submission restores the foreign PS b1 buffer");
		ID3D11Buffer* empty=nullptr; renderer.context->PSSetConstantBuffers(1,1,&empty);
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

	void CheckEntryScrollbarContinuity(Checks& checks, Offscreen& renderer)
	{
		const float oldScale=ImGui::GetStyle().FontScaleDpi;
		const auto oldCounters=renderer.counters;
		// 与生产相同的动画时钟及冷 child 层级；普通条宽补间作为能复现跳变的控制路径。
		for (bool dark : {false,true}) for (int variant=0;variant<3;++variant)
		{
			Design::ApplyPalette(dark);
			ImGui::GetStyle().FontScaleDpi=std::array{1.0F,1.25F,.6F}[variant];
			EntryLayoutProbe layout; layout.viewport.x=variant==1 ? 760.0F : static_cast<float>(Width);
			const std::string name="##entry-scrollbar-cold-"+std::to_string(dark)+"-"+std::to_string(variant);
			layout.root=name.c_str(); Design::HomeMotionState state; FrameInfo captured;
			const std::uint64_t serial=201+dark*10+variant;
			auto frame=[&](float delta=0, OverlaySubset subset=OverlaySubset::None) {
				return Frame(&state,Away,delta,true,serial,true,1,false,false,subset,false,&layout);
			};
			for (int warm=0;warm<8 && !state.preparation.ready;++warm)
			{ frame(); if (!SubmitPrepared(checks,renderer,state)) return; }
			checks.Expect(state.preparation.ready && state.preparation.warmFrames==2,"animated cold entry uses the existing two successful warm submissions");
			state.entryElapsed=.76F;
			for (int absent=0;absent<12;++absent)
			{ const auto info=frame(); checks.Expect(info.skipped,"actual entry omits cold page children before capture"); if (!SubmitPrepared(checks,renderer,state)) return; }
			state.entryElapsed=Design::HomeEntryTiming::WritingEnd-.001F;
			for (int warm=0;warm<6 && !state.preparation.revealReady && state.EntryActive();++warm)
			{ captured=frame(.05F); checks.Expect(!captured.skipped,"capture preparation really submits the complete page"); if (!SubmitPrepared(checks,renderer,state)) return; }
			checks.Expect(state.preparation.revealReady && renderer.pageCache.Ready(),"animated cold page capture completes within the existing bounded preparation budget");
			if (!captured.content || !state.preparation.revealReady) continue;
			checks.Expect(state.preparation.layoutReady && state.preparation.revealFrames>=2,"capture waits for native consumed sizes to match current measured content");
			const unsigned captureFrames=state.preparation.revealFrames;
			const bool scrollbar=captured.content->ScrollbarY;
			const float reserve=captured.content->ScrollbarSizes.x, contentWidth=captured.contentWidth;
			const auto brand=state.brandOrigin, hero=state.heroOrigin, heroSize=state.heroSize;
			const float brandWidth=state.brandWidth; const auto buttons=state.buttonBounds; const auto clip=state.contentClip;
			checks.Expect(scrollbar== (variant!=2),"normal and narrow pages overflow naturally while the roomy scaled page has no forced rail");
			checks.Expect(reserve==(scrollbar ? Design::Pixels(Design::ScrollbarTrackSize) : 0),"captured native rail has terminal width or exactly zero reservation without overflow");
			Pixels cached,first;
			state.entryElapsed=2.65F; auto info=frame();
			if (!SubmitPrepared(checks,renderer,state,&cached)) return;
			checks.Expect(info.skipped && !info.content,"docking cached page performs no child layout");
			// 恢复仍在缓存下等本帧实际布局就绪；第一帧实时输出不能依赖额外事后暖帧才正确。
			state.entryElapsed=Design::HomeEntryTiming::Total-.001F;
			for (int warm=0;warm<6 && !state.preparation.restoreReady && state.EntryActive();++warm)
			{ info=frame(.01F); if (!SubmitPrepared(checks,renderer,state)) return; }
			checks.Expect(state.preparation.restoreReady && state.preparation.layoutReady && state.preparation.restoreFrames>=2 && state.preparation.restoreFrames<=6,
				"bounded restoration waits for actual native layout readiness after at least two successful submissions");
			const unsigned restoreFrames=state.preparation.restoreFrames;
			info=frame(.01F);
			if (!SubmitPrepared(checks,renderer,state,&first)) return;
			checks.Expect(!state.EntryActive() && !info.skipped && info.content,"comparison uses the first real page immediately after entry finishes");
			const float firstLiveWidth=info.contentWidth;
			auto logLayout=[&](const char* stage,const FrameInfo& value) {
				std::cout<<"[HomeScrollbarLayout] dark="<<dark<<" variant="<<variant<<" stage="<<stage;
				for (const auto& rect : {value.contentWorkRect,value.pageWorkRect})
					std::cout<<" work="<<rect.Min.x<<','<<rect.Min.y<<','<<rect.Max.x<<','<<rect.Max.y;
				for (const auto& values : {value.contentLayout,value.pageLayout}) { std::cout<<" layout="; for (float v : values) std::cout<<v<<','; }
				std::cout<<'\n';
			};
			logLayout("capture",captured); logLayout("first-live",info);
			auto sameRect=[](const ImRect& left,const ImRect& right) { return left.Min.x==right.Min.x && left.Min.y==right.Min.y
				&& left.Max.x==right.Max.x && left.Max.y==right.Max.y; };
			checks.Expect(sameRect(captured.contentWorkRect,info.contentWorkRect) && sameRect(captured.pageWorkRect,info.pageWorkRect),
				"capture and first live frame preserve actual outer and nested WorkRect coordinates exactly");
			checks.Expect(info.content && info.content->ScrollbarY==scrollbar && info.content->ScrollbarSizes.x==reserve
				&& info.contentWidth==contentWidth && state.brandOrigin.x==brand.x && state.brandOrigin.y==brand.y && state.brandWidth==brandWidth
				&& state.heroOrigin.x==hero.x && state.heroOrigin.y==hero.y && state.heroSize.x==heroSize.x && state.heroSize.y==heroSize.y
				&& state.contentClip.x==clip.x && state.contentClip.y==clip.y && state.contentClip.z==clip.z && state.contentClip.w==clip.w
				&& std::memcmp(buttons.data(),state.buttonBounds.data(),sizeof(buttons))==0,"capture and first live frame preserve rail, available width, clip, hero, brand destination and button geometry exactly");
			// 截图本来无字标，终态内联字标会出现；只剔除已验证坐标不变的字标 AA 包围矩形。
			// 蒙版/运动字标已用真实 foreground 选择器移除，其他整个 target（含右侧滚动条）逐像素 RGBA 比较。
			ImRect ink(brand,{brand.x+brandWidth,brand.y+Brand::Height(brandWidth)}); ink.Expand(Design::Pixels(14));
			std::uint64_t compared=0,different=0; ImRect differences({FLT_MAX,FLT_MAX},{-FLT_MAX,-FLT_MAX});
			for (UINT y=0;y<Height;++y) for (UINT x=0;x<Width;++x)
				if (!ink.Contains({x+.5F,y+.5F})) { ++compared; if (cached[static_cast<size_t>(y)*Width+x]!=first[static_cast<size_t>(y)*Width+x])
				{ ++different; differences.Add({static_cast<float>(x),static_cast<float>(y)}); } }
			std::cout<<"[HomeScrollbarPixelDifference] dark="<<dark<<" variant="<<variant<<" pixels="<<different<<" bounds="
				<<differences.Min.x<<','<<differences.Min.y<<','<<differences.Max.x<<','<<differences.Max.y<<'\n';
			checks.Expect(different==0 && compared>static_cast<std::uint64_t>(Width)*Height/2,"cached no-overlay background equals first live page RGBA exactly outside the sole inline brand region");
			// 终态下一帧可正常消费原生滚动目标，不延长输入屏障。
			if (scrollbar) { ImGui::SetScrollY(info.content,Design::Pixels(20)); info=frame();
				checks.Expect(info.content && info.content->Scroll.y>0 && !(info.content->Flags & ImGuiWindowFlags_NoInputs),"native scrolling resumes on the next live frame"); if (!SubmitPrepared(checks,renderer,state)) return; }
			// 新 Show 的首个实际输入取消；复用生产 NoInputs 防止该帧点击/按键穿透。
			state.showSerial=0; frame(); if (!SubmitPrepared(checks,renderer,state)) return;
			ImGui::GetIO().AddKeyEvent(ImGuiKey_Tab,true); info=frame();
			checks.Expect(!state.EntryActive() && info.inputBlocked && info.content && (info.content->Flags & ImGuiWindowFlags_NoInputs)==ImGuiWindowFlags_NoInputs
				&& info.action==Design::HomeAction::None,"real cancellation frame blocks inputs via the production NoInputs child flags");
			if (!SubmitPrepared(checks,renderer,state)) return;
			ImGui::GetIO().AddKeyEvent(ImGuiKey_Tab,false); info=frame();
			checks.Expect(!info.inputBlocked && info.content && !(info.content->Flags & ImGuiWindowFlags_NoInputs),"input barrier clears after the cancellation event");
			if (!SubmitPrepared(checks,renderer,state)) return;
			std::cout<<"[HomeScrollbarCache] dark="<<dark<<" scale="<<ImGui::GetStyle().FontScaleDpi<<" width="<<layout.viewport.x
				<<" bar="<<scrollbar<<" reserve="<<reserve<<" capture-width="<<contentWidth<<" first-live-width="<<firstLiveWidth
				<<" exact-background-pixels="<<compared<<" differing="<<different<<" capture-frames="<<captureFrames<<" restore-frames="<<restoreFrames<<" native-layout-ready=1\n";
		}
		// 未启用入场稳定策略的旧条宽动画，两个冷布局帧之后尚未到最终宽度；不是靠关闭所有动效修复。
		ImGui::GetStyle().FontScaleDpi=1; Design::ApplyPalette(false);
		EntryLayoutProbe control; control.root="##entry-scrollbar-original-control"; control.settle=false;
		Design::HomeMotionState state; FrameInfo info;
		auto draw=[&]() { return Frame(&state,Away,0,true,299,false,1,false,false,OverlaySubset::None,false,&control); };
		for (int warm=0;warm<2;++warm) { info=draw(); if (!renderer.Submit(checks,ImGui::GetDrawData())) return; }
		const float coldWidth=info.contentWidth, coldReserve=info.content ? info.content->ScrollbarSizes.x : 0;
		for (int warm=0;warm<30;++warm) { info=draw(); if (!renderer.Submit(checks,ImGui::GetDrawData())) return; }
		checks.Expect(info.content && info.content->ScrollbarY && info.content->ScrollbarSizes.x==Design::Pixels(Design::ScrollbarTrackSize)
			&& coldWidth>info.contentWidth && coldReserve<info.content->ScrollbarSizes.x,"ordinary animated scrollbar control reproduces the pre-fix cold-to-settled page width jump");
		std::cout<<"[HomeScrollbarCache] original-control cold-width="<<coldWidth<<" terminal-width="<<info.contentWidth
			<<" width-gap="<<coldWidth-info.contentWidth<<" cold-reserve="<<coldReserve<<" terminal-reserve="<<info.content->ScrollbarSizes.x<<'\n';
		ImGui::GetStyle().FontScaleDpi=oldScale; renderer.pageCache.Reset(); renderer.submittedFrame.Reset(); renderer.counters=oldCounters;
		Design::ConfigureRowAnimations(0,false,1); ImGui::GetIO().AddMousePosEvent(Away.x,Away.y);
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
		state.entryElapsed=Design::HomeEntryTiming::WritingEnd-.001F;
		for (int frame=0;frame<2;++frame)
		{
			info=Frame(&state,Away,.05F,true,41,true,5);
			checks.Expect(!info.skipped && state.entryElapsed==Design::HomeEntryTiming::WritingEnd
				&& Design::ResolveHomeEntryPose(state).opacity==1,"capture follows writing with two real warm frames under opaque cover");
			if (!SubmitPrepared(checks,renderer,state)) return;
		}
		checks.Expect(state.preparation.revealReady && renderer.pageCache.Ready(),"GPU cache is published only after real background render/copy and successful submission");
		// 比较整张真实 RGBA，包括字标内/外透明 AA、标题边界和底层文字；不放宽像素误差。
		for (float time : {1.6F,1.91F,2.16F,2.44F,2.54F,2.65F})
		{
			state.entryElapsed=time;
			info=Frame(&state,Away,0,true,41,true);
			if (!SubmitPrepared(checks,renderer,state,&fast)) return;
			checks.Expect(info.skipped && info.pageClip.GetHeight()==0,"hold/dock/settle never generate actual page or navigation children");
			reference=state; reference.preparation={};
			for (int warmFrame=0;warmFrame<3;++warmFrame) Frame(&reference,Away,0,true,41,false);
			if (!renderer.Submit(checks,ImGui::GetDrawData(),&full)) return;
			checks.Expect(fast==full,"GPU snapshot plus dynamic foreground exactly matches full-page RGBA including translucent AA without a second logo");
		}
		// 真实复用事务的失败不允许留下可提交的截图；状态机重走有限准备，不重播 Show。
		Frame(&state,Away,0,true,41,true);
		bool captured=false;
		checks.ExpectedResult(renderer.pageCache.Render(nullptr,renderer.context.Get(),renderer.view.Get(),
			state,ImGui::GetDrawData(),ImGui::GetForegroundDrawList(),captured),E_INVALIDARG,"cache resource failure reports HRESULT");
		Design::CompleteHomeEntryFrame(state,false,false);
		checks.Expect(!renderer.pageCache.Ready() && !state.preparation.ready && !captured,"failed cache transaction discards resource and ready publication");
		for (int frame=0;frame<6 && !state.preparation.revealReady;++frame)
		{ Frame(&state,Away,0,true,41,true); if (!SubmitPrepared(checks,renderer,state)) return; }
		checks.Expect(state.EntryActive() && state.entries==entries && state.preparation.revealReady,"failed cache recovers without replaying handwriting or retaining old pixels");
		state.entryElapsed=Design::HomeEntryTiming::Total-.005F;
		for (int frame=0;frame<3;++frame)
		{ Frame(&state,Away,.01F,true,41,true); if (!SubmitPrepared(checks,renderer,state,&fast)) return; }
		checks.Expect(!state.EntryActive() && fast==baseline && !renderer.pageCache.Ready(),"terminal warm frames return exactly to original static page and release GPU cache");

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
			auto full=state; full.entry=Design::HomeMotionState::Entry::Playing; full.preparation={};
			for (int warm=0;warm<3;++warm) Frame(&full,Away,0,true,73,false,1,true,false);
			if (!renderer.Submit(checks,ImGui::GetDrawData(),&control,S_OK,true)) return;
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

	void CheckMaterialCache(Checks& checks, Offscreen& renderer)
	{
		const auto originalAlpha=ImGui::GetStyle().Alpha;
		const bool dark=Design::IsDarkMode();
		const auto originalBackground=ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
		for (int material=0;material<6;++material)
		{
			const bool solid=material!=1 && material<4;
			// 实际分层调色板也走 GPU 捕获/重放，与纯透明根背景分别验证。
			Design::ApplyPalette(dark, material==4 ? Inkeys::UI::Setting::BackdropMode::Mica
				: material==5 ? Inkeys::UI::Setting::BackdropMode::Acrylic : Inkeys::UI::Setting::BackdropMode::Solid);
			ImGui::GetStyle().Alpha=material==2 ? .6F : originalAlpha;
			ImGui::GetStyle().Colors[ImGuiCol_WindowBg]=originalBackground;
			ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w=!solid ? 0 : material==3 ? .6F : originalBackground.w;
			Design::HomeMotionState state; const std::uint64_t serial=80+material;
			for (int frame=0;frame<8 && !state.preparation.revealReady;++frame)
			{
				Frame(&state,Away,0,true,serial,true,1,solid,true);
				if (state.preparation.ready) state.entryElapsed=Design::HomeEntryTiming::WritingEnd;
				if (!SubmitPrepared(checks,renderer,state,nullptr,solid)) return;
			}
			checks.Expect(state.preparation.revealReady && renderer.pageCache.Ready(),"all supported alpha/backdrop materials capture real GPU pixels");
			for (float time : {1.6F,2.16F,2.54F})
			{
				Pixels cached,full; state.entryElapsed=time;
				Frame(&state,Away,0,true,serial,true,1,solid,true);
				const auto copiesBefore=renderer.counters.copies;
				if (!SubmitPrepared(checks,renderer,state,&cached,solid)) return;
				if (time==1.6F)
					checks.Expect(renderer.counters.copies-copiesBefore==(material==2 ? 1U : 0U),
						"only truly opaque style-alpha-one hold omits hidden cache replay; partial style alpha retains copy");
				auto reference=state; reference.preparation={};
				for (int warm=0;warm<3;++warm) Frame(&reference,Away,0,true,serial,false,1,solid,false);
				if (!renderer.Submit(checks,ImGui::GetDrawData(),&full,S_OK,solid)) return;
				checks.Expect(cached==full,"Solid/Mica/partial-alpha cached RGBA including transparent AA equals real full render exactly");
			}
		}
		Design::ApplyPalette(dark);
		ImGui::GetStyle().Alpha=originalAlpha; ImGui::GetStyle().Colors[ImGuiCol_WindowBg]=originalBackground;
		renderer.pageCache.Reset();
		std::cout<<"[HomeWARP] cache-materials=6 phases-per-material=3 comparison=exact-RGBA\n";
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
		checks.Expect(info.pageClip.GetHeight()>0 && state.heroVisible,"input cancellation immediately restores an unclipped real homepage child");
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
		Design::HomeMotionState state; renderer.pageCache.Reset();
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
				state.entry=Design::HomeMotionState::Entry::Playing;
				state.entryElapsed=(mode==4 || mode==8 || mode==10) ? 0 : Design::HomeEntryTiming::DockStart;
				Frame(&state,Away,0,true,1,true,1,solidClear,suppressRoot);
				if (!SubmitPrepared(checks,renderer,state,nullptr,solidClear)) return;
			}
		}
		const ImVec2 center(state.brandOrigin.x + state.brandWidth * .5F, state.brandOrigin.y + Brand::Height(state.brandWidth) * .5F);
		std::vector<double> cpu, submit, backend, wait, total;
		cpu.reserve(100); submit.reserve(100); backend.reserve(100); wait.reserve(100); total.reserve(100);
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
				state.entryElapsed=mode==10 ? 0 : mode==11 ? Design::HomeEntryTiming::Total-.005F
					: mode==8 ? .12F : mode==9 ? Design::HomeEntryTiming::WritingEnd
					: mode==4 ? .26F+1.0F*((frame+20)%100)/100
					: mode==5 ? 1.6F : mode==6 ? 1.92F+.50F*((frame+20)%100)/100 : 2.46F+.18F*((frame+20)%100)/100;
				if (mode==10) { renderer.pageCache.Reset(); state.preparation={}; }
				if (mode==11) { state.preparation.restorePending=true; state.preparation.restoreReady=false; state.preparation.restoreFrames=0; }
				if (mode==9)
				{
					renderer.pageCache.Reset(); state.preparation.revealReady=false; state.preparation.revealFrames=1;
					state.preparation.revealPending=true;
				}

			}
			const auto begin = Clock::now();
			const auto countersBefore=renderer.counters;
			info = Frame(mode == 0 ? nullptr : &state, mouse, mode==11 ? .01F : mode>=4 ? 0 : 1.0F / 60.0F, true,1,optimized,1,solidClear,suppressRoot);
			const auto drawn = Clock::now();
			if (!renderer.Submit(checks, ImGui::GetDrawData(),nullptr,S_OK,solidClear,optimized ? &state : nullptr)) return;
			const auto completed = Clock::now();
			if (optimized && mode==9) checks.Expect(state.preparation.layoutReady && renderer.counters.captures==countersBefore.captures+1,
				"capture diagnostic records one actual snapshot only after the production native-size readiness check");
			if (optimized && mode==11) checks.Expect(state.preparation.layoutReady && state.preparation.restoreFrames==1 && !state.preparation.restoreReady
				&& renderer.counters.copies==countersBefore.copies+1,"restore diagnostic measures a genuine hidden layout submission with current sizes and original successful-frame accounting");
			if (frame >= 0) { cpu.push_back(Micros(begin, drawn)); submit.push_back(Micros(drawn, completed));
				backend.push_back(renderer.backendUs); wait.push_back(renderer.waitUs); total.push_back(Micros(begin,completed)); }
			checks.Expect(info.vertices<=baseline.vertices+165 && info.indices<=baseline.indices+7*108+6
				&& info.batches<=baseline.batches+1,
				"entrance adds only a cover and bounded activity caps with one foreground draw batch");
		}
		if (mode!=9) checks.Expect(allocations.allocations==allocatedBefore,"warmed actual homepage motion performs no additional ImGui allocations");
		checks.Expect(state.cache.allocations==cacheBefore,"warm original brand cache never grows during benchmark");
		checks.Expect(capacity()==geometryCapacity,"all original stroke/index/section capacities remain fixed during actual motion submissions");
		if (mode == 1 || mode >=4) checks.Expect(state.cache.rebuilds == rebuiltBefore, "idle and entrance reuse original curve geometry");
		std::cout << "[HomeWARP] benchmark=" << name << " frames=100 warmup=20 scale=1 theme=light build="<<BuildName<<" cpu-us median=" << Percentile(cpu, .5)
			<< " p95=" << Percentile(cpu, .95) << " submit-complete-event-us median=" << Percentile(submit, .5)
			<< " p95=" << Percentile(submit, .95) << " vertices=" << info.vertices << " indices=" << info.indices
			<< " batches=" << info.batches << " driver="<<renderer.driverName
			<< " backend-us median="<<Percentile(backend,.5)<<" p95="<<Percentile(backend,.95)
			<< " wait-us median="<<Percentile(wait,.5)<<" p95="<<Percentile(wait,.95)
			<< " total-us median="<<Percentile(total,.5)<<" p95="<<Percentile(total,.95)
			<< " unlimited-fps median="<<1000000/Percentile(total,.5)<<" p95-time-fps="<<1000000/Percentile(total,.95)
			<< " imgui-allocs=" << allocations.allocations - allocatedBefore
			<< " opaque-fast="<<info.skipped<<" solid-clear="<<solidClear<<" root-suppressed="<<info.rootSuppressed<<" prep-state-bytes="<<sizeof(state.preparation)
			<<" cache-growth=" << state.cache.allocations - cacheBefore << " cache-rebuilds=" << state.cache.rebuilds - rebuiltBefore << '\n';
	}

	bool PrepareCache(Checks& checks, Offscreen& renderer, Design::HomeMotionState& state, std::uint64_t serial, bool solid=false)
	{
		renderer.pageCache.Reset(); renderer.submittedFrame.Reset();
		for (int frame=0;frame<8 && !state.preparation.revealReady;++frame)
		{
			Frame(&state,Away,0,true,serial,true,1,solid);
			if (!SubmitPrepared(checks,renderer,state,nullptr,solid)) return false;
			if (state.preparation.ready) state.entryElapsed=Design::HomeEntryTiming::WritingEnd;
		}
		return checks.Expect(state.preparation.revealReady && renderer.pageCache.Ready(),"fixed-phase diagnostics use genuinely captured ready production pixels");
	}

	void CheckDuplicateSubmissions(Checks& checks, Offscreen& renderer)
	{
		for (int warm=0;warm<30;++warm) { Frame(nullptr,Away,0); renderer.Submit(checks,ImGui::GetDrawData()); }
		renderer.submittedFrame.Reset(); const auto before=renderer.counters;
		Pixels baseline,current; Frame(nullptr,Away,0);
		if (!renderer.Submit(checks,ImGui::GetDrawData(),&baseline,S_OK,false,nullptr,true)) return;
		for (int frame=0;frame<12;++frame)
		{
			Frame(nullptr,Away,0);
			if (!renderer.Submit(checks,ImGui::GetDrawData(),&current,S_OK,false,nullptr,true)) return;
			checks.Expect(renderer.frameSkipped && current==baseline,"unchanged real static homepage skips all DX11 work and preserves exact RGBA");
		}
		checks.Expect(renderer.counters.submissions-before.submissions==1 && renderer.counters.skips-before.skips==12
			&& renderer.counters.clears-before.clears==1,"static first output submits once and twelve repeated CPU frames do not clear");
		Frame(nullptr,Away,0);
		ImGui::FindWindowByName("##home-warp-production-root")->DrawList->AddCallback(ImDrawCallback_ResetRenderState,nullptr);
		if (!renderer.Submit(checks,ImGui::GetDrawData(),&current,S_OK,false,nullptr,true)) return;
		checks.Expect(!renderer.frameSkipped && current==baseline,"actual reset-render callback forces backend execution even when resulting RGBA is identical");
		Frame(nullptr,Away,0);
		if (!renderer.Submit(checks,ImGui::GetDrawData(),nullptr,S_OK,false,nullptr,true)) return;
		ImTextureData pending; pending.Create(ImTextureFormat_RGBA32,8,8);
		std::memset(pending.Pixels,255,pending.GetSizeInBytes());
		ImDrawData transaction=*ImGui::GetDrawData();
		ImVector<ImTextureData*> textures=*transaction.Textures; textures.push_back(&pending); transaction.Textures=&textures;
		if (!renderer.Submit(checks,&transaction,&current,S_OK,false,nullptr,true)) return;
		checks.Expect(!renderer.frameSkipped && pending.Status==ImTextureStatus_OK && current==baseline,
			"pending real texture creation forces upload while preserving unchanged page pixels");
		pending.Updates.push_back({0,0,8,8}); pending.SetStatus(ImTextureStatus_WantUpdates);
		if (!renderer.Submit(checks,&transaction,&current,S_OK,false,nullptr,true)) return;
		checks.Expect(!renderer.frameSkipped && pending.Status==ImTextureStatus_OK,"pending real texture update forces same-output backend transaction");
		pending.UnusedFrames=1; pending.WantDestroyNextFrame=true; pending.SetStatus(ImTextureStatus_WantDestroy);
		if (!renderer.Submit(checks,&transaction,&current,S_OK,false,nullptr,true)) return;
		checks.Expect(!renderer.frameSkipped && pending.BackendUserData==nullptr,"pending texture destruction cannot be suppressed by duplicate page pixels");
		pending.DestroyPixels();
		ImGui_ImplDX11_TestFailNext(ImGui_ImplDX11_TestFailure_IndexMap,E_OUTOFMEMORY);
		Frame(nullptr,Away,0);
		if (!renderer.Submit(checks,ImGui::GetDrawData(),&current,E_OUTOFMEMORY,false,nullptr,true)) return;
		Frame(nullptr,Away,0);
		if (!renderer.Submit(checks,ImGui::GetDrawData(),&current,S_OK,false,nullptr,true)) return;
		checks.Expect(!renderer.frameSkipped && current==baseline,"failed genuine Map revokes snapshot and identical retry really redraws successfully");

		Design::HomeMotionState state;
		if (!PrepareCache(checks,renderer,state,153)) return;
		state.entryElapsed=Design::HomeEntryTiming::WritingEnd+.02F;
		Frame(&state,Away,0,true,153,true);
		if (!renderer.Submit(checks,ImGui::GetDrawData(),&baseline,S_OK,false,&state,true)) return;
		const float elapsed=state.entryElapsed; const auto holdBefore=renderer.counters;
		for (int frame=0;frame<10;++frame)
		{
			Frame(&state,Away,.02F,true,153,true);
			if (!renderer.Submit(checks,ImGui::GetDrawData(),&current,S_OK,false,&state,true)) return;
			checks.Expect(renderer.frameSkipped && current==baseline,"complete opaque art hold skips GPU while keeping exact presented frame");
		}
		checks.Expect(state.entryElapsed>elapsed+.19F && renderer.counters.skips-holdBefore.skips==10
			&& renderer.counters.copies==holdBefore.copies && renderer.counters.clears==holdBefore.clears,
			"duplicate hold advances real entry time without copy clear or submit");
		state.entryElapsed=Design::HomeEntryTiming::DockStart-.001F;
		Frame(&state,Away,.02F,true,153,true);
		if (!renderer.Submit(checks,ImGui::GetDrawData(),&current,S_OK,false,&state,true)) return;
		checks.Expect(!renderer.frameSkipped && current!=baseline,"first fade/dock output changes immediately and really submits");
		for (int frame=0;frame<60 && state.EntryActive();++frame)
		{
			Frame(&state,Away,.02F,true,153,true);
			const bool transaction=Design::RequiresHomeEntrySubmission(state);
			if (!renderer.Submit(checks,ImGui::GetDrawData(),nullptr,S_OK,false,&state,true)) return;
			if (transaction) checks.Expect(!renderer.frameSkipped,"restore preparation counts only actual backend completions");
		}
		checks.Expect(!state.EntryActive() && !renderer.pageCache.Ready() && state.heroVisible,
			"deduplicated entry reaches normal unclipped terminal page and releases screenshot");
		std::cout<<"[HomeSubmission] exact-static initial-submit=1 skips=12 hold-skips=10 timer-advanced=true failure-real-retry=true\n";
	}

	struct WidgetInfo { ImVec2 hover; bool tooltip=false, inputActive=false; };
	WidgetInfo WidgetFrame(ImVec2 mouse, char* text, bool focus=false, const char* caption="Settings", const char* status="Download 1%")
	{
		auto& io=ImGui::GetIO(); io.DisplaySize={static_cast<float>(Width),static_cast<float>(Height)};
		io.DisplayFramebufferScale={1,1}; io.DeltaTime=1.0F/60.0F;
		io.AddMousePosEvent(mouse.x,mouse.y); ImGui_ImplDX11_NewFrame(); ImGui::NewFrame();
		if (activeRenderer) activeRenderer->frameClearOmitted=false;
		ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize(io.DisplaySize);
		ImGui::Begin("##submission-live-controls",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoResize);
		ImGui::TextUnformatted(caption); ImGui::TextUnformatted(status);
		ImFluent::Button("Hover for delayed tooltip",{260,40});
		WidgetInfo result; const auto first=ImGui::GetItemRectMin(),last=ImGui::GetItemRectMax();
		result.hover={(first.x+last.x)*.5F,(first.y+last.y)*.5F};
		result.tooltip=ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
		ImFluent::SetItemTooltip("The real Fluent tooltip still appears after its timer");
		if (focus) ImGui::SetKeyboardFocusHere();
		ImGui::SetNextItemWidth(300); ImGui::InputText("Input with blinking caret",text,64);
		result.inputActive=ImGui::IsItemActive();
		ImGui::End(); ImGui::Render(); return result;
	}

	void CheckLiveControls(Checks& checks, Offscreen& renderer)
	{
		auto& io=ImGui::GetIO(); char input[64]="original"; WidgetInfo info;
		const auto savedHover=ImGui::GetStyle().HoverDelayNormal; const bool savedBlink=io.ConfigInputTextCursorBlink;
		ImGui::GetStyle().HoverDelayNormal=.7F; io.ConfigInputTextCursorBlink=true;
		for (int frame=0;frame<40;++frame)
		{ info=WidgetFrame(Away,input); if (!renderer.Submit(checks,ImGui::GetDrawData())) return; }
		renderer.submittedFrame.Reset();
		const auto before=renderer.counters; const double started=ImGui::GetTime();
		bool sawDelay=false,appeared=false,appearedSubmitted=false,hoverSubmitted=false;
		for (int frame=0;frame<110;++frame)
		{
			info=WidgetFrame(info.hover,input);
			if (!renderer.Submit(checks,ImGui::GetDrawData(),nullptr,S_OK,false,nullptr,true)) break;
			sawDelay|=!info.tooltip;
			if (!renderer.frameSkipped && frame<10) hoverSubmitted=true;
			if (info.tooltip && !appeared) { appeared=true; appearedSubmitted=!renderer.frameSkipped; }
		}
		checks.Expect(sawDelay && appeared && appearedSubmitted && hoverSubmitted && ImGui::GetTime()>started+1.5,
			"real Fluent hover and delayed tooltip continue CPU timers and submit on appearance without further mouse input");
		checks.Expect(renderer.counters.skips>before.skips,"stable portions of live hover/tooltip output can skip without stopping timer");
		for (int frame=0;frame<2;++frame)
		{ info=WidgetFrame(Away,input,true); if (!renderer.Submit(checks,ImGui::GetDrawData(),nullptr,S_OK,false,nullptr,true)) break; }
		io.AddInputCharactersUTF8("xyz"); info=WidgetFrame(Away,input);
		if (!renderer.Submit(checks,ImGui::GetDrawData(),nullptr,S_OK,false,nullptr,true)) return;
		checks.Expect(info.inputActive && std::strstr(input,"xyz") && !renderer.frameSkipped,"real InputText keyboard characters change output and force an actual submission");
		const auto caretBefore=renderer.counters;
		for (int frame=0;frame<150;++frame)
		{
			info=WidgetFrame(Away,input);
			if (!renderer.Submit(checks,ImGui::GetDrawData(),nullptr,S_OK,false,nullptr,true)) break;
		}
		checks.Expect(info.inputActive && renderer.counters.submissions-caretBefore.submissions>=2
			&& renderer.counters.skips-caretBefore.skips>100,"real 1.2s input caret cycle redraws visibility transitions and skips identical intervals");
		info=WidgetFrame(Away,input,false,"Settings changed","Download 1%");
		if (!renderer.Submit(checks,ImGui::GetDrawData(),nullptr,S_OK,false,nullptr,true)) return;
		checks.Expect(!renderer.frameSkipped,"caption change immediately submits even without new pointer events");
		info=WidgetFrame(Away,input,false,"Settings changed","Download 2%");
		if (!renderer.Submit(checks,ImGui::GetDrawData(),nullptr,S_OK,false,nullptr,true)) return;
		checks.Expect(!renderer.frameSkipped,"polled data text change immediately submits even without input events");
		ImGui::ClearActiveID(); ImGui::GetStyle().HoverDelayNormal=savedHover; io.ConfigInputTextCursorBlink=savedBlink;
		renderer.submittedFrame.Reset();
		std::cout<<"[HomeSubmission] live-controls hover=true delayed-tooltip="<<appeared
			<<" actual-key-input=true caret-submits="<<renderer.counters.submissions-caretBefore.submissions
			<<" caret-skips="<<renderer.counters.skips-caretBefore.skips<<" caption/data-changes=true\n";
	}

	void CheckOverlayDecomposition(Checks& checks, Offscreen& renderer)
	{
		Design::HomeMotionState state;
		if (!PrepareCache(checks,renderer,state,151)) return;
		for (float phase : {1.6F,2.16F,2.54F})
		{
			Pixels full,separate; state.entryElapsed=phase;
			Frame(&state,Away,0,true,151,true);
			if (!renderer.Submit(checks,ImGui::GetDrawData(),&full,S_OK,false,&state,false,true)) return;
			state.entryElapsed=phase;
			auto info=Frame(&state,Away,0,true,151,true,1,false,false,OverlaySubset::CoverOnly);
			checks.Expect(info.subsetValid,"diagnostic cover is the exact production rectangle with original blend and clip");
			if (!renderer.Submit(checks,ImGui::GetDrawData(),nullptr,S_OK,false,&state,false,true)) return;
			state.entryElapsed=phase;
			info=Frame(&state,Away,0,true,151,true,1,false,false,OverlaySubset::InkOnly);
			checks.Expect(info.subsetValid,"diagnostic ink retains exact original production vertices UV colours and indices");
			const auto* source=ImGui::GetDrawData(); ImDrawData ink;
			ink.Valid=true; ink.DisplayPos=source->DisplayPos; ink.DisplaySize=source->DisplaySize;
			ink.FramebufferScale=source->FramebufferScale; ink.Textures=source->Textures;
			ink.AddDrawList(ImGui::GetForegroundDrawList());
			// 第二次仅绘制原字标，不清屏/重复标题；分解后的混合顺序必须逐像素等于完整输出。
			ImGui_ImplDX11_RenderDrawData(&ink);
			if (!checks.Result(ImGui_ImplDX11_GetLastResult(),"submit original ink over independently rendered cover")
				|| !renderer.Readback(checks,&separate)) return;
			checks.Expect(full==separate,"cover-plus-ink decomposition preserves complete frame RGBA exactly at fixed hold dock settle phases");
		}
		std::cout<<"[HomeWARP] overlay-decomposition fixed-phases=hold,dock,settle comparison=exact-RGBA\n";
	}

	void CheckClearAndCopyCandidates(Checks& checks, Offscreen& renderer)
	{
		const auto savedAlpha=ImGui::GetStyle().Alpha;
		const auto savedBackground=ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
		for (int material=0;material<7;++material)
		{
			const bool solid=material!=1;
			ImGui::GetStyle().Alpha=material==2 ? .6F : savedAlpha;
			ImGui::GetStyle().Colors[ImGuiCol_WindowBg]=savedBackground;
			if (material==3) ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w=.6F;
			if (material==6) ImGui::GetStyle().Colors[ImGuiCol_WindowBg].x=2.5F/255;
			Design::HomeMotionState state;
			if (!PrepareCache(checks,renderer,state,160+material,solid)) break;
			if (material==4) state.clientContent.y+=.25F;
			if (material==5) state.clientContent.x+=.25F;
			for (float phase : {.76F,1.6F,2.16F,2.54F})
			{
				Pixels control,candidate,copyControl; state.entryElapsed=phase;
				Frame(&state,Away,0,true,160+material,true,1,solid,true);
				if (!renderer.Submit(checks,ImGui::GetDrawData(),&control,S_OK,solid,&state)) break;
				state.entryElapsed=phase; Frame(&state,Away,0,true,160+material,true,1,solid,true);
				if (!renderer.Submit(checks,ImGui::GetDrawData(),&copyControl,S_OK,solid,&state,false,true)) break;
				checks.Expect(control==copyControl,"opaque replay omission preserves exact RGBA versus forced original cache copy at all materials and phases");
				state.entryElapsed=phase;
				const auto info=Frame(&state,Away,0,true,160+material,true,1,solid,true,OverlaySubset::All,true);
				// 故意污染整个目标，证明省clear不依赖上一张discard buffer或离屏保留像素。
				const float poison[4]{.9F,.1F,.8F,.3F}; renderer.context->ClearRenderTargetView(renderer.view.Get(),poison);
				const auto clears=renderer.counters.clears;
				if (!renderer.Submit(checks,ImGui::GetDrawData(),&candidate,S_OK,solid,&state)) break;
				checks.Expect(info.clearOmitted==(material==0),"only integer complete solid alpha-one coverage permits clear omission");
				checks.Expect(renderer.counters.clears-clears==(info.clearOmitted ? 0U : 1U),"candidate either truly omits clear or executes legacy fallback");
				checks.Expect(control==candidate,"poisoned-target clear candidate and all alpha/noninteger/partial/Mica fallbacks preserve exact whole-frame RGBA");
			}
		}
		ImGui::GetStyle().Alpha=savedAlpha; ImGui::GetStyle().Colors[ImGuiCol_WindowBg]=savedBackground;
		renderer.frameClearOmitted=false; renderer.pageCache.Reset();
		std::cout<<"[HomeSubmission] clear/copy-candidates materials=7 phases=4 poisoned-target=true comparison=exact-RGBA\n";
	}

	void BenchmarkFixedSubmission(Checks& checks, Offscreen& renderer, const char* name, float phase,
		OverlaySubset subset=OverlaySubset::All, bool deduplicate=false, bool forceCopy=true, bool staticPage=false,
		bool solid=false, bool candidateClear=false)
	{
		Design::HomeMotionState state;
		if (!staticPage && !PrepareCache(checks,renderer,state,152,solid)) return;
		renderer.submittedFrame.Reset();
		std::vector<double> cpu,comparison,backend,wait,total;
		cpu.reserve(100); comparison.reserve(100); backend.reserve(100); wait.reserve(100); total.reserve(100);
		Offscreen::Counters before; std::size_t capacity=0; FrameInfo info;
		for (int frame=-20;frame<100;++frame)
		{
			if (frame==0) { before=renderer.counters; capacity=renderer.submittedFrame.Capacity(); }
			if (!staticPage) state.entryElapsed=phase;
			const auto begin=Clock::now();
			info=Frame(staticPage ? nullptr : &state,Away,0,true,152,!staticPage,1,solid,solid,subset,candidateClear);
			const auto generated=Clock::now();
			if (!checks.Expect(info.subsetValid,"fixed benchmark removes only requested original foreground ranges")
				|| !renderer.Submit(checks,ImGui::GetDrawData(),nullptr,S_OK,solid,staticPage ? nullptr : &state,deduplicate,forceCopy)) return;
			const auto done=Clock::now();
			if (frame>=0)
			{
				cpu.push_back(Micros(begin,generated)); comparison.push_back(renderer.compareUs);
				backend.push_back(renderer.backendUs); wait.push_back(renderer.waitUs); total.push_back(Micros(begin,done));
			}
		}
		const auto submitted=renderer.counters.submissions-before.submissions, skipped=renderer.counters.skips-before.skips;
		checks.Expect(submitted+skipped==100,"each fixed-phase sample records one real submission or one skip");
		if (deduplicate) checks.Expect(skipped>90 && renderer.submittedFrame.Capacity()==capacity,
			"warmed identical output predominantly skips DX11 with bounded unchanged snapshot capacity");
		else checks.Expect(submitted==100 && skipped==0,"render throughput control really submits every sample");
		std::cout<<"[HomeSubmission] benchmark="<<name<<" driver="<<renderer.driverName<<" phase="<<phase
			<<" frames=100 warmup=20 width="<<Width<<" height="<<Height<<" theme=light build="<<BuildName
			<<" cpu-us median="<<Percentile(cpu,.5)<<" p95="<<Percentile(cpu,.95)
			<<" compare-us median="<<Percentile(comparison,.5)<<" p95="<<Percentile(comparison,.95)
			<<" backend-us median="<<Percentile(backend,.5)<<" p95="<<Percentile(backend,.95)
			<<" wait-us median="<<Percentile(wait,.5)<<" p95="<<Percentile(wait,.95)
			<<" total-us median="<<Percentile(total,.5)<<" p95="<<Percentile(total,.95)
			<<" actual-submits="<<submitted<<" skips="<<skipped<<" clears="<<renderer.counters.clears-before.clears
			<<" copies="<<renderer.counters.copies-before.copies<<" captures="<<renderer.counters.captures-before.captures
			<<" snapshot-bytes="<<renderer.submittedFrame.StoredBytes()<<" snapshot-capacity="<<renderer.submittedFrame.Capacity()
			<<" solid="<<solid<<" candidate-clear="<<candidateClear<<" clear-omitted="<<info.clearOmitted
			<<" clear-policy="<<(candidateClear ? "experiment-only-not-in-product" : "production-clear")
			<<" vertices="<<info.vertices<<" indices="<<info.indices<<" batches="<<info.batches;
		if (!deduplicate) std::cout<<" unlimited-render-fps="<<1000000/Percentile(total,.5);
		else std::cout<<" rate-label=CPU-callback-only-no-display-FPS";
		std::cout<<'\n';
	}
}

int RunSettingHomeWarpProbe(ImFont* regular, ImFont* strong, ImFont* icons)
{
	Checks checks;
	{
	Offscreen renderer;
	if (!checks.Expect(ImGui::GetIO().BackendRendererUserData == nullptr && ImGui::GetFrameCount() == 0,
		"probe runs before any CPU fake texture materialization") || !renderer.Initialize(checks)) return checks.failures;
	Design::BindTextFonts(regular, strong); ImFluent::SetIconFont(icons);
	CheckColdEntry(checks, renderer);
	CheckBackendFailures(checks, renderer);
	CheckSharedShaderMode(checks, renderer);
	CheckEntryScrollbarContinuity(checks,renderer);
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
		CheckMaterialCache(checks,renderer);
		CheckDuplicateSubmissions(checks,renderer);
		CheckClearAndCopyCandidates(checks,renderer);
		CheckOverlayDecomposition(checks,renderer);
	}
	CheckPreparedKeyboard(checks,renderer);
	CheckLiveControls(checks,renderer);
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
	Benchmark(checks,renderer,"wait-full",8,baseline);
	Benchmark(checks,renderer,"wait-fast",8,baseline,true);
	Benchmark(checks,renderer,"prepare-warm-fonts",10,baseline,true);
	Benchmark(checks,renderer,"restore",11,baseline,true);
	Benchmark(checks,renderer,"capture",9,baseline,true);
	for (int phase=0;phase<3;++phase)
	{
		const float time=std::array{1.6F,2.16F,2.54F}[phase];
		const char* stage=std::array{"hold","dock","settle"}[phase];
		for (int subset=0;subset<4;++subset)
		{
			const auto selection=std::array{OverlaySubset::All,OverlaySubset::CoverOnly,OverlaySubset::InkOnly,OverlaySubset::None}[subset];
			const auto name=std::string(stage)+std::array{"-fixed-full","-fixed-no-ink","-fixed-no-cover","-fixed-copy-title"}[subset];
			BenchmarkFixedSubmission(checks,renderer,name.c_str(),time,selection);
		}
		BenchmarkFixedSubmission(checks,renderer,(std::string(stage)+"-solid-clear-control").c_str(),time,OverlaySubset::All,false,false,false,true,false);
		BenchmarkFixedSubmission(checks,renderer,(std::string(stage)+"-solid-clear-candidate").c_str(),time,OverlaySubset::All,false,false,false,true,true);
	}
	BenchmarkFixedSubmission(checks,renderer,"hold-copy-omitted",1.6F,OverlaySubset::All,false,false);
	BenchmarkFixedSubmission(checks,renderer,"hold-duplicate-skip",1.6F,OverlaySubset::All,true,false);
	BenchmarkFixedSubmission(checks,renderer,"static-duplicate-skip",0,OverlaySubset::All,true,false,true);
	}
	// 使用同一生产页面/字体/CSO，独立硬件设备仅作吞吐诊断，不改变产品默认 WARP 后端。
	{
		Offscreen hardware;
		if (hardware.Initialize(checks,D3D_DRIVER_TYPE_HARDWARE))
		{
			Design::BindTextFonts(regular,strong); ImFluent::SetIconFont(icons); Design::ApplyPalette(false);
			FrameInfo baseline;
			for (int frame=0;frame<20;++frame)
			{ baseline=Frame(nullptr,Away); if (!hardware.Submit(checks,ImGui::GetDrawData())) break; }
			Benchmark(checks,hardware,"static",0,baseline);
			for (int mode : {8,4,5,6,7})
			{
				const char* name=mode==8 ? "wait" : mode==4 ? "writing" : mode==5 ? "hold" : mode==6 ? "dock" : "settle";
				Benchmark(checks,hardware,(std::string(name)+"-full").c_str(),mode,baseline);
				Benchmark(checks,hardware,(std::string(name)+(mode==8 || mode==4 ? "-fast" : "-cached")).c_str(),mode,baseline,true);
			}
			Benchmark(checks,hardware,"capture",9,baseline,true);
			Benchmark(checks,hardware,"prepare-warm-fonts",10,baseline,true);
			Benchmark(checks,hardware,"restore",11,baseline,true);
		}
	}
	std::cout << "[HomeOffscreen] Timed samples use EVENT/Flush completion, no Sleep/Present/readback or FPS cap; "
		"pixel correctness uses separate staging Copy/Map. CPU build/backend-call/wait/total are wall times, not pure GPU query time. "
		"1000x1100 Home+Navigation+Overlay excludes native caption/business images/shared clients/Present; throughput is not display FPS. "
		"HomeWARP benchmark lines force real rendering; HomeSubmission duplicate samples report CPU callbacks and actual-submit/skip counts without display FPS. "
		"Ablations hold fixed original phases/geometry, retain clear and cache/title, and remove only original ink/cover ranges.\n";
	return checks.failures;
}
