module;

#include <atomic>

export module Inkeys.UI.Freeze;

export namespace Inkeys::UI::Freeze
{
	// 定格只由用户切换或进入工作区时改变，绘制/选择工具切换不参与状态机。
	[[nodiscard]] bool IsActive() noexcept;
	[[nodiscard]] bool IsAvailable() noexcept;
	void Toggle() noexcept;
	void SetPresentationActive(bool active) noexcept;
	void SetWhiteboardActive(bool active) noexcept;
}

namespace
{
	// 三个标志共用一个状态字，避免按钮点击与工作区切换交错。
	constexpr unsigned char freezeBit = 0x01;
	constexpr unsigned char presentationBit = 0x02;
	constexpr unsigned char whiteboardBit = 0x04;
	std::atomic<unsigned char> freezeState = 0;

	void SetWorkspaceBit(unsigned char bit, bool active) noexcept
	{
		unsigned char current = freezeState.load(std::memory_order_acquire);
		for (;;)
		{
			unsigned char desired = active ?
				static_cast<unsigned char>((current | bit) & ~freezeBit) :
				static_cast<unsigned char>(current & ~bit);
			if (freezeState.compare_exchange_weak(current, desired,
				std::memory_order_acq_rel, std::memory_order_acquire)) return;
		}
	}
}

namespace Inkeys::UI::Freeze
{
	bool IsActive() noexcept
	{
		return (freezeState.load(std::memory_order_acquire) & freezeBit) != 0;
	}

	bool IsAvailable() noexcept
	{
		return (freezeState.load(std::memory_order_acquire)
			& (presentationBit | whiteboardBit)) == 0;
	}

	void Toggle() noexcept
	{
		unsigned char current = freezeState.load(std::memory_order_acquire);
		for (;;)
		{
			if ((current & (presentationBit | whiteboardBit)) != 0) return;
			const unsigned char desired = current ^ freezeBit;
			if (freezeState.compare_exchange_weak(current, desired,
				std::memory_order_acq_rel, std::memory_order_acquire)) return;
		}
	}

	void SetPresentationActive(bool active) noexcept
	{
		SetWorkspaceBit(presentationBit, active);
	}

	void SetWhiteboardActive(bool active) noexcept
	{
		SetWorkspaceBit(whiteboardBit, active);
	}
}
