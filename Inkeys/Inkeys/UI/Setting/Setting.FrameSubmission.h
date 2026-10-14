#pragma once

#include "Setting.Pages.h"
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>

namespace Inkeys::UI::Setting::Design
{
	struct FrameSubmissionKey
	{
		HomeEntryFrameKey output;
		std::array<float,4> clear{};
		std::uint64_t showSerial=0, hideSerial=0, themeSerial=0, cacheRevision=0;
		std::uintptr_t target=0;
		bool operator==(const FrameSubmissionKey&) const = default;
	};

	inline bool RequiresHomeEntrySubmission(const HomeMotionState& state) noexcept
	{
		const auto& prep=state.preparation;
		// 准备、捕获和恢复计数必须来自真实提交；只有普通稳定帧可以复用上次呈现。
		return state.entry==HomeMotionState::Entry::Pending || (state.EntryActive()
			&& (!prep.managed || !prep.ready || prep.fullFrame
				|| (prep.revealPending && !prep.revealReady) || (prep.restorePending && !prep.restoreReady)));
	}

	class FrameSubmissionCache
	{
		std::unique_ptr<std::byte[]> bytes;
		std::size_t size=0;
		FrameSubmissionKey key;
		bool valid=false;

		template<class Consume>
		static bool Visit(const ImDrawData* data, Consume consume) noexcept
		{
			if (!data || !data->Valid || data->CmdListsCount!=data->CmdLists.Size
				|| data->TotalVtxCount<0 || data->TotalIdxCount<0) return false;
			if (data->Textures)
				for (const auto* texture : *data->Textures)
					if (!texture || texture->Status!=ImTextureStatus_OK) return false;
			auto value=[&](const auto& field) { return consume(&field,sizeof(field)); };
			if (!value(data->DisplayPos) || !value(data->DisplaySize) || !value(data->FramebufferScale)
				|| !value(data->CmdListsCount) || !value(data->TotalVtxCount) || !value(data->TotalIdxCount)) return false;
			// 本仓固定顶点布局无填充；命令含指针和填充，必须逐个实际渲染字段比较。
			static_assert(std::is_trivially_copyable_v<ImDrawVert> && sizeof(ImDrawVert)==20
				&& offsetof(ImDrawVert,pos)==0 && offsetof(ImDrawVert,uv)==8 && offsetof(ImDrawVert,col)==16);
			for (const auto* list : data->CmdLists)
			{
				if (!list || list->VtxBuffer.Size<0 || list->IdxBuffer.Size<0 || list->CmdBuffer.Size<0
					|| static_cast<std::size_t>(list->VtxBuffer.Size)>BudgetBytes/sizeof(ImDrawVert)
					|| static_cast<std::size_t>(list->IdxBuffer.Size)>BudgetBytes/sizeof(ImDrawIdx)) return false;
				if (!value(list->VtxBuffer.Size) || !value(list->IdxBuffer.Size) || !value(list->CmdBuffer.Size)
					|| !consume(list->VtxBuffer.Data,list->VtxBuffer.Size*sizeof(ImDrawVert))
					|| !consume(list->IdxBuffer.Data,list->IdxBuffer.Size*sizeof(ImDrawIdx))) return false;
				for (const auto& command : list->CmdBuffer)
				{
					if (command.UserCallback || (command.TexRef._TexData
						&& (command.TexRef._TexData->Status!=ImTextureStatus_OK
							|| command.TexRef._TexData->GetTexID()==ImTextureID_Invalid))) return false;
					const auto texture=command.GetTexID();
					if (!value(command.ClipRect) || !value(texture) || !value(command.VtxOffset)
						|| !value(command.IdxOffset) || !value(command.ElemCount)) return false;
				}
			}
			return true;
		}

	public:
		static constexpr std::size_t BudgetBytes=1024*1024;
		void Reset() noexcept { valid=false; size=0; }
		[[nodiscard]] std::size_t StoredBytes() const noexcept { return size; }
		[[nodiscard]] std::size_t Capacity() const noexcept { return bytes ? BudgetBytes : 0; }
		bool CanSkip(const ImDrawData* data, const FrameSubmissionKey& current, bool force=false) const noexcept
		{
			if (force || !valid || current!=key) return false;
			std::size_t offset=0;
			const bool equal=Visit(data,[&](const void* source,std::size_t length) {
				if (length>size-offset || (length && std::memcmp(bytes.get()+offset,source,length)!=0)) return false;
				offset+=length; return true;
			});
			return equal && offset==size;
		}
		// 只由 backend 成功且 Present==S_OK 的路径调用；失败帧绝不能变成比较基准。
		void ConfirmPresented(const ImDrawData* data, const FrameSubmissionKey& current) noexcept
		{
			std::size_t required=0;
			if (!Visit(data,[&](const void*,std::size_t length) {
				if (length>BudgetBytes-required) return false;
				required+=length; return true;
			})) { Reset(); return; }
			if (!bytes) bytes.reset(new (std::nothrow) std::byte[BudgetBytes]);
			if (!bytes) { Reset(); return; }
			std::size_t offset=0;
			if (!Visit(data,[&](const void* source,std::size_t length) {
				if (length) std::memcpy(bytes.get()+offset,source,length);
				offset+=length; return true;
			})) { Reset(); return; }
			key=current; size=required; valid=true;
		}
	};
}
