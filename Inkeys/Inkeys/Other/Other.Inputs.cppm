module;

#include "../../IdtMain.h"

#undef max
#undef min
#include "libcuckoo/cuckoohash_map.hh"
#include <atomic>

export module Inkeys.Other.Inputs;

libcuckoo::cuckoohash_map<BYTE, bool> downMap;
std::atomic_uint mouseButtons = 0;

unsigned MouseButtonBit(BYTE key) noexcept
{
	return key == VK_LBUTTON ? 1U : key == VK_MBUTTON ? 2U : key == VK_RBUTTON ? 4U : 0U;
}

namespace Inkeys::Inputs
{
	export void SetKeyBoardDown(BYTE key, bool down)
	{
		// 鼠标 Hook 只更新固定原子位；键盘保留原缓存与原调用合同。
		if (const auto bit = MouseButtonBit(key))
		{
			if (down) mouseButtons.fetch_or(bit, std::memory_order_relaxed);
			else mouseButtons.fetch_and(~bit, std::memory_order_relaxed);
			return;
		}
		downMap.insert_or_assign(key, down);
	}
	export bool IsKeyBoardDown(BYTE key)
	{
		if (const auto bit = MouseButtonBit(key))
			return (mouseButtons.load(std::memory_order_relaxed) & bit) != 0;
		bool down = false;
		downMap.find(key, down);
		return down;
	}
}