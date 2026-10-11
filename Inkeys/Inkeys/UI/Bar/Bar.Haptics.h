#pragma once

#include "../../Drawing/Draw3/Draw3.HapticRequest.h"
#include <windows.h>
#include <utility>

namespace Inkeys::UI::Bar::Haptics
{
	using Drawing::Draw3::Haptics::Click;
	using Drawing::Draw3::Haptics::Context;
	inline constexpr unsigned SlotCount = 8;
	inline constexpr short MarkerBase = -32768;
	inline bool IsMarker(UINT message, short value) noexcept
	{
		return (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_MOUSEMOVE) &&
			value >= MarkerBase && value < MarkerBase + static_cast<int>((SlotCount + 1) * 4);
	}
	inline unsigned Token(UINT message, short value) noexcept
	{
		return IsMarker(message, value) ? static_cast<unsigned>(value - MarkerBase) / 4 : 0;
	}
	inline short Marker(unsigned token, bool cancel, bool screen) noexcept
	{
		return static_cast<short>(MarkerBase + static_cast<int>(token <= SlotCount ? token : 0) * 4 +
			(cancel ? 1 : 0) + (screen ? 2 : 0));
	}

	struct PointerSample
	{
		UINT id = 0, cursor = 0;
		HANDLE device = nullptr;
		POINT point{};
		DWORD time = 0;
	};
	class ContactTokens
	{
	public:
		// 调用方串行锁只覆盖内存和入/出队记账，不跨业务回调或 WinRT。
		void Invalidate(UINT pointer = 0) noexcept
		{
			for (auto& slot : slots_)
				if (slot.source && (!pointer || slot.source->pointerId == pointer))
					slot.source->valid.store(false, std::memory_order_release);
		}
		void Observe(UINT phase, const PointerSample& sample, Context context,
			std::uint64_t now) noexcept
		{
			if (phase == WM_POINTERDOWN)
			{
				Invalidate(sample.id);
				for (auto& slot : slots_)
					if (slot.source && slot.down.time != sample.time) slot.source->valid.store(false, std::memory_order_release);
				if (!context.run || !sample.id || !sample.device || !sample.cursor) return;
				for (auto& slot : slots_)
				{
					// wire token 仅在队列、按下 session 与 Host 都不再引用后复用。
					if (slot.queued || (slot.source && (slot.source.use_count() != 1 ||
						(slot.source->valid && now - slot.source->downAt <= Drawing::Draw3::Haptics::ContactLifetimeMs)))) continue;
					try { slot.source = std::make_shared<Drawing::Draw3::Haptics::ClickSource>(); }
					catch (...) { return; }
					slot.source->context = context;
					slot.source->pointerId = sample.id;
					slot.source->downAt = now;
					slot.down = sample;
					slot.up = {};
					slot.touch = 0;
					slot.bound = false;
					return;
				}
				return;
			}
			for (auto& slot : slots_)
			{
				if (!slot.source || slot.source->pointerId != sample.id || !slot.source->valid) continue;
				if (sample.device != slot.down.device || sample.cursor != slot.down.cursor)
					slot.source->valid.store(false, std::memory_order_release);
				else if (phase == WM_POINTERUP)
				{
					slot.up = sample;
					slot.source->upAt.store(now, std::memory_order_release);
				}
			}
		}
		unsigned BindDown(const TOUCHINPUT& touch, POINT point, std::uint64_t now) noexcept
		{
			if (!(touch.dwFlags & TOUCHEVENTF_PEN) || !(touch.dwMask & TOUCHINPUTMASKF_TIMEFROMSYSTEM)) return 0;
			unsigned result = 0;
			for (unsigned i = 0; i < slots_.size(); ++i)
			{
				auto& slot = slots_[i];
				if (!slot.source || !slot.source->valid || slot.bound || now < slot.source->downAt ||
					now - slot.source->downAt > 100 || !Matches(slot.down, touch, point)) continue;
				if (result) return 0; // 身份歧义时保留点击，只省略触觉。
				result = i + 1;
			}
			if (result)
			{
				auto& slot = slots_[result - 1];
				slot.touch = touch.dwID;
				slot.bound = true;
			}
			return result;
		}
		unsigned Touch(unsigned token, const TOUCHINPUT& touch, POINT point, bool up) noexcept
		{
			if (!token || token > SlotCount) return 0;
			auto& slot = slots_[token - 1];
			if (!slot.source || !slot.source->valid || !slot.bound) return 0;
			if (!(touch.dwFlags & TOUCHEVENTF_PEN) || slot.touch != touch.dwID ||
				slot.down.device != touch.hSource || (up && (!(touch.dwMask & TOUCHINPUTMASKF_TIMEFROMSYSTEM) ||
				!slot.source->upAt || !Matches(slot.up, touch, point))))
			{
				slot.source->valid.store(false, std::memory_order_release);
				return 0;
			}
			return token;
		}
		void Cancel(unsigned token) noexcept
		{
			if (token && token <= SlotCount && slots_[token - 1].source)
				slots_[token - 1].source->valid.store(false, std::memory_order_release);
		}
		Click Hold(unsigned token) noexcept { return token && token <= SlotCount ? slots_[token - 1].source : Click{}; }
		void Enqueued(unsigned token) noexcept { if (token && token <= SlotCount) ++slots_[token - 1].queued; }
		Click Dequeued(unsigned token) noexcept
		{
			if (!token || token > SlotCount) return {};
			auto& slot = slots_[token - 1];
			if (!slot.queued) return {};
			--slot.queued;
			return slot.source;
		}
		void Cleared() noexcept
		{
			for (auto& slot : slots_) slot.queued = 0;
		}
	private:
		static bool Matches(const PointerSample& pointer, const TOUCHINPUT& touch, POINT point) noexcept
		{
			// WM_TOUCH ID 与 Pointer ID 没有等价保证；仅接受同源、同系统时刻、同屏幕点。
			return pointer.device == touch.hSource && pointer.time == touch.dwTime &&
				pointer.point.x == point.x && pointer.point.y == point.y;
		}
		struct Slot
		{
			Click source;
			PointerSample down, up;
			DWORD touch = 0;
			unsigned queued = 0;
			bool bound = false;
		};
		std::array<Slot, SlotCount> slots_{};
	};

	inline Click AcceptedClick(const Click& down, const Click& up, UINT message, bool cancel) noexcept
	{
		return message == WM_LBUTTONUP && !cancel && down && down == up ? down : Click{};
	}
	// 仅交互线程的真实 callback scope 设置；程序化按钮回调没有凭据。
	inline thread_local Click acceptedClick;
	struct AcceptedClickScope
	{
		Click previous;
		explicit AcceptedClickScope(Click source) : previous(std::move(acceptedClick)) { acceptedClick = std::move(source); }
		~AcceptedClickScope() { acceptedClick = std::move(previous); }
	};
}
