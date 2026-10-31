#pragma once

#include "Setting.Pages.h"
#include "../../../additional/imgui/imgui_impl_dx11.h"
#include <algorithm>
#include <utility>
#include <d3d11.h>
#include <wrl/client.h>

namespace Inkeys::UI::Setting::Design
{
	// 仅设置渲染线程持有；不保存 swap-chain buffer、DrawList 或字体 UV，Hide/epoch 切换直接撤销。
	class HomeEntryPageCache
	{
		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
		HomeEntryFrameKey key;
		std::uint64_t serial = 0, revision = 0;
		D3D11_TEXTURE2D_DESC description{};

		static ImDrawData SelectLists(const ImDrawData& source, ImDrawList* foreground, bool overlay)
		{
			ImDrawData data=source;
			data.CmdLists.resize(0); data.TotalVtxCount=data.TotalIdxCount=0;
			for (auto* list : source.CmdLists)
			{
				if ((list==foreground)!=overlay) continue;
				data.CmdLists.push_back(list);
				data.TotalVtxCount+=list->VtxBuffer.Size; data.TotalIdxCount+=list->IdxBuffer.Size;
			}
			data.CmdListsCount=data.CmdLists.Size;
			return data;
		}

	public:
		struct RenderStats { unsigned captures=0, copies=0; };
		void Reset() noexcept { if (texture) ++revision; texture.Reset(); serial=0; }
		[[nodiscard]] std::uint64_t Revision() const noexcept { return revision; }
		static bool NeedsCopy(const HomeMotionState& state, float styleAlpha, const ImDrawData* data = nullptr) noexcept
		{
			if (!state.EntryActive() || !state.preparation.revealReady) return false;
			if (!data || styleAlpha!=1 || ResolveHomeEntryPose(state).opacity!=1) return true;
			const auto bounds=state.clientContent; const auto pos=data->DisplayPos, scale=data->FramebufferScale;
			auto integral=[](float value) { return std::isfinite(value) && value==std::floor(value); };
			// 只有整像素蒙版完整覆盖 copy 的 scissor 区域才省复制；分数边界仍回写原缓存像素。
			return !(scale.x>0 && scale.y>0 && integral((bounds.x-pos.x)*scale.x) && integral((bounds.z-pos.x)*scale.x)
				&& integral((bounds.y-pos.y)*scale.y) && integral((bounds.w-pos.y)*scale.y));
		}
		[[nodiscard]] bool Ready() const noexcept { return texture!=nullptr; }
		void Validate(HomeMotionState& state, const HomeEntryFrameKey& current) noexcept
		{
			if (!state.EntryActive() || serial!=state.showSerial || key!=current) Reset();
			if (!texture && state.preparation.revealReady)
			{
				state.preparation.revealReady=false; state.preparation.layoutReady=false; state.preparation.revealFrames=0;
				state.preparation.restoreReady=false; state.preparation.restoreFrames=0;
			}
		}

		HRESULT Render(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11RenderTargetView* view,
			HomeMotionState& state, ImDrawData* data, ImDrawList* foreground, bool& captured,
			RenderStats* stats = nullptr, bool forceCopy = false)
		{
			captured=false; if (stats) *stats={};
			const auto& prep=state.preparation;
			const bool capture=state.EntryActive() && prep.ready && prep.revealPending && !prep.revealReady
				&& prep.fullFrame && prep.layoutReady && prep.revealFrames>=1;
			const bool replay=state.EntryActive() && prep.revealReady;
			if (!capture && !replay)
			{
				ImGui_ImplDX11_RenderDrawData(data);
				return ImGui_ImplDX11_GetLastResult();
			}
			if (!device || !context || !view || !data) { Reset(); return E_INVALIDARG; }
			Microsoft::WRL::ComPtr<ID3D11Resource> resource;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> target;
			view->GetResource(resource.GetAddressOf());
			HRESULT result=resource.As(&target);
			if (FAILED(result)) { Reset(); return result; }
			D3D11_TEXTURE2D_DESC desc{}; target->GetDesc(&desc);
			if (capture)
			{
				auto background=SelectLists(*data,foreground,false);
				ImGui_ImplDX11_RenderDrawData(&background);
				result=ImGui_ImplDX11_GetLastResult();
				if (FAILED(result)) { Reset(); return result; }
				// GPU 原样复制预乘 RGBA；不经 AddImage 再混合，Mica/半透明根也不发生 double-alpha。
				Microsoft::WRL::ComPtr<ID3D11Texture2D> candidate;
				auto cacheDesc=desc; cacheDesc.Usage=D3D11_USAGE_DEFAULT;
				cacheDesc.BindFlags=cacheDesc.CPUAccessFlags=cacheDesc.MiscFlags=0;
				result=device->CreateTexture2D(&cacheDesc,nullptr,candidate.GetAddressOf());
				if (FAILED(result)) { Reset(); return result; }
				context->CopyResource(candidate.Get(),target.Get());
				texture=std::move(candidate); description=desc; key=prep.key; serial=state.showSerial; ++revision;
				if (stats) ++stats->captures;
				auto overlay=SelectLists(*data,foreground,true);
				ImGui_ImplDX11_RenderDrawData(&overlay);
				result=ImGui_ImplDX11_GetLastResult();
				captured=SUCCEEDED(result);
			}
			else
			{
				if (!texture || serial!=state.showSerial || key!=prep.key || desc.Width!=description.Width
					|| desc.Height!=description.Height || desc.Format!=description.Format) { Reset(); return E_INVALIDARG; }
				const auto region=state.clientContent;
				const auto pos=data->DisplayPos, scale=data->FramebufferScale;
				// 与 DX11 scissor 相同的像素截断；仅回写客户内容，标题栏继续实时生成和提交。
				auto coordinate=[](float value, UINT limit) { return static_cast<UINT>(std::clamp(value,0.0F,static_cast<float>(limit))); };
				D3D11_BOX box{coordinate((region.x-pos.x)*scale.x,desc.Width),coordinate((region.y-pos.y)*scale.y,desc.Height),0,
					coordinate((region.z-pos.x)*scale.x,desc.Width),coordinate((region.w-pos.y)*scale.y,desc.Height),1};
				if (box.left>=box.right || box.top>=box.bottom) { Reset(); return E_INVALIDARG; }
				if (forceCopy || NeedsCopy(state,prep.key.material[0],data))
				{
					context->CopySubresourceRegion(target.Get(),0,box.left,box.top,0,texture.Get(),0,&box);
					if (stats) ++stats->copies;
				}
				ImGui_ImplDX11_RenderDrawData(data);
				result=ImGui_ImplDX11_GetLastResult();
			}
			const HRESULT removed=device->GetDeviceRemovedReason();
			if (SUCCEEDED(result) && FAILED(removed)) result=removed;
			if (FAILED(result)) { Reset(); captured=false; }
			return result;
		}
	};
}
