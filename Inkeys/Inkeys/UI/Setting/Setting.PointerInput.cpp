#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Setting.PointerInput.h"
#include "../../Input/UiTouchRouting.h"
#include <cfloat>
#include <mutex>

import Inkeys.Message;

namespace Inkeys::UI::Setting::PointerInput
{
	namespace
	{
		namespace Touch = Input::UiPointer::Touch;

		// 仅设置窗口线程访问。
		ContactArbiter arbiter;

		// 窗口线程写、渲染线程读；临界区只做值拷贝。
		enum class OverrideMode { None, Contact, Parked };
		std::mutex sharedMutex;
		TrackRing track;
		OverrideMode overrideMode = OverrideMode::None;
		PointerOverride overrideValue{};
		bool parkReleased = false; // 仅设置窗口线程访问。

		double InputSeconds(std::uint64_t performanceCount, DWORD milliseconds) noexcept
		{
			static const double frequency = []() noexcept
			{
				LARGE_INTEGER value{};
				return QueryPerformanceFrequency(&value) && value.QuadPart > 0 ? static_cast<double>(value.QuadPart) : 0.0;
			}();
			// Pointer 的 PerformanceCount 精度高于毫秒时间；缺失（Win7 WM_TOUCH）时回退 dwTime。
			if (performanceCount != 0 && frequency > 0.0) return static_cast<double>(performanceCount) / frequency;
			return static_cast<double>(milliseconds) / 1000.0;
		}

		void Deliver(HWND hwnd, PointerEvent::Type type, const Sample& sample, double seconds, EventSink sink) noexcept
		{
			POINT client = sample.screen;
			if (hwnd) (void)ScreenToClient(hwnd, &client);
			const PointerEvent event{ type, sample.source, static_cast<float>(client.x), static_cast<float>(client.y) };
			const Kind kind = type == PointerEvent::Type::Begin ? Kind::Down
				: type == PointerEvent::Type::End ? Kind::Up
				: type == PointerEvent::Type::Cancel ? Kind::Cancel : Kind::Move;
			// 轨迹先于 ImGui 事件发布：渲染线程处理到这次按下/抬起时，已能读到对应样本。
			{
				std::lock_guard lock(sharedMutex);
				track.Push({ kind, sample.source, event.x, event.y, seconds, sample.generation });
			}
			// 先写 ImGui，再发布覆盖位置：渲染线程不会在按下/抬起事件入队前读到更新的停放状态。
			if (sink) sink(event);
			std::lock_guard lock(sharedMutex);
			if (type == PointerEvent::Type::Begin || type == PointerEvent::Type::Update)
			{
				overrideMode = OverrideMode::Contact;
				overrideValue = { sample.source, event.x, event.y };
			}
			else if (sample.source == Source::Touch)
			{
				// 触摸没有悬停：抬起后指针停放在窗口外，直到真实鼠标消息到来。
				overrideMode = OverrideMode::Parked;
				overrideValue = { Source::Touch, -FLT_MAX, -FLT_MAX };
			}
			else overrideMode = OverrideMode::None; // 笔抬起后由兼容鼠标继续提供悬停位置。
		}

		void Apply(HWND hwnd, const Sample& sample, bool primary, double seconds, EventSink sink) noexcept
		{
			const auto result = arbiter.Accept(sample, primary, ContactSlopPixels);
			if (result.replaced) Deliver(hwnd, PointerEvent::Type::Cancel, result.replacedLatest, seconds, sink);
			switch (result.verdict)
			{
			case ContactArbiter::Verdict::Begin: Deliver(hwnd, PointerEvent::Type::Begin, arbiter.Latest(), seconds, sink); break;
			case ContactArbiter::Verdict::Update: Deliver(hwnd, PointerEvent::Type::Update, arbiter.Latest(), seconds, sink); break;
			case ContactArbiter::Verdict::End: Deliver(hwnd, PointerEvent::Type::End, arbiter.Latest(), seconds, sink); break;
			default: break;
			}
		}

		void CancelOwned(HWND hwnd, DWORD contact, EventSink sink) noexcept
		{
			Sample cancelled;
			if (arbiter.CancelContact(contact, &cancelled))
				Deliver(hwnd, PointerEvent::Type::Cancel, cancelled, InputSeconds(0, GetTickCount()), sink);
		}

		bool HandleTouch(HWND hwnd, WPARAM wParam, LPARAM lParam, EventSink sink) noexcept
		{
			const auto handle = reinterpret_cast<HTOUCHINPUT>(lParam);
			// PointerMode 下不注册 WM_TOUCH；若仍收到，只是 Pointer 的派生副本。
			if (Touch::PointerMode()) { CloseTouchInputHandle(handle); return true; }
			std::array<TOUCHINPUT, 16> inputs{};
			const UINT count = (std::min)(static_cast<UINT>(LOWORD(wParam)), static_cast<UINT>(inputs.size()));
			if (count == 0 || !GetTouchInputInfo(handle, count, inputs.data(), sizeof(TOUCHINPUT)))
			{
				CloseTouchInputHandle(handle);
				CancelContact(sink);
				return true;
			}
			for (UINT index = 0; index < count; ++index)
			{
				const auto& input = inputs[index];
				// Win7 的笔经兼容鼠标进入 ImGui，不从 WM_TOUCH 重复输入。
				if (input.dwFlags & TOUCHEVENTF_PEN) continue;
				Sample sample;
				sample.source = Source::Touch;
				sample.kind = (input.dwFlags & TOUCHEVENTF_DOWN) ? Kind::Down : (input.dwFlags & TOUCHEVENTF_UP) ? Kind::Up : Kind::Move;
				sample.screen = { TOUCH_COORD_TO_PIXEL(input.x), TOUCH_COORD_TO_PIXEL(input.y) };
				sample.messageTime = input.dwTime;
				sample.device = reinterpret_cast<std::uintptr_t>(input.hSource);
				sample.contact = input.dwID;
				Apply(hwnd, sample, (input.dwFlags & TOUCHEVENTF_PRIMARY) != 0, InputSeconds(0, input.dwTime), sink);
			}
			CloseTouchInputHandle(handle);
			return true;
		}
	}

	ContactArbiter::Result ContactArbiter::Accept(Sample sample, bool primary, LONG slopPixels) noexcept
	{
		Result result;
		switch (sample.kind)
		{
		case Kind::Down:
			// 同一 pointerId 再次 Down，或同设备出现新的主接触：旧接触已丢失终态，先撤销（与主栏规则一致）。
			if (active_ && latest_.device == sample.device && (latest_.contact == sample.contact || primary))
			{
				result.replaced = true;
				result.replacedLatest = latest_;
				result.replacedLatest.kind = Kind::Cancel;
				active_ = false;
			}
			// 单指针 UI：已有活动接触时，其他手指/笔被吞掉；只有主接触能开始。
			if (active_ || !primary) return result;
			sample.generation = ++generation_;
			latest_ = sample;
			down_ = sample.screen;
			slop_ = (std::max)(LONG{ 0 }, slopPixels);
			moved_ = false;
			active_ = true;
			result.verdict = Verdict::Begin;
			return result;
		case Kind::Move:
		case Kind::Up:
		{
			if (!active_ || sample.device != latest_.device || sample.contact != latest_.contact) return result;
			if (!moved_)
			{
				const LONGLONG dx = sample.screen.x - down_.x, dy = sample.screen.y - down_.y;
				// 容差内的抖动不进入 ImGui；抬起仍在容差内时按按下点抬起，点按落点稳定。
				if (dx * dx + dy * dy <= static_cast<LONGLONG>(slop_) * slop_)
				{
					if (sample.kind == Kind::Move) return result;
					sample.screen = down_;
				}
				else moved_ = true;
			}
			sample.generation = generation_;
			latest_ = sample;
			if (sample.kind == Kind::Up) { active_ = false; result.verdict = Verdict::End; }
			else result.verdict = Verdict::Update;
			return result;
		}
		default:
			return result;
		}
	}

	bool ContactArbiter::Cancel(Sample* cancelled) noexcept
	{
		if (!active_) return false;
		active_ = false;
		if (cancelled) { *cancelled = latest_; cancelled->kind = Kind::Cancel; }
		return true;
	}

	bool ContactArbiter::CancelContact(DWORD contact, Sample* cancelled) noexcept
	{
		return active_ && latest_.contact == contact && Cancel(cancelled);
	}

	bool ShouldBlockChannelMouse(UINT message, WPARAM wParam, ULONG_PTR extraInfo,
		bool pointerMode, bool contactActive) noexcept
	{
		if (!Inkeys::Message::IsPointerGeneratedMouseMessage(message, extraInfo)) return false;
		if (!pointerMode) return Inkeys::Message::IsTouchGeneratedMouseMessage(message, extraInfo);
		constexpr WPARAM buttons = MK_LBUTTON | MK_RBUTTON | MK_MBUTTON | MK_XBUTTON1 | MK_XBUTTON2;
		// 用户决定保留笔悬停 hover：无按键、无活动接触的笔签名移动放行，backend 会标为 Pen。
		const bool penHover = message == WM_MOUSEMOVE && Inkeys::Message::IsPenGeneratedMouseMessage(message, extraInfo)
			&& (wParam & buttons) == 0 && !contactActive;
		return !penHover;
	}

	void TrackRing::Push(const TrackSample& sample) noexcept
	{
		samples_[written_ % Capacity] = sample;
		++written_;
	}

	std::size_t TrackRing::Read(std::uint64_t& cursor, std::span<TrackSample> out, bool& overflowed) const noexcept
	{
		overflowed = false;
		if (cursor > written_) cursor = written_;
		if (written_ - cursor > Capacity) { cursor = written_ - Capacity; overflowed = true; }
		std::size_t count = 0;
		while (cursor < written_ && count < out.size()) out[count++] = samples_[(cursor++) % Capacity];
		return count;
	}

	void ConfigureWindow(HWND hwnd) noexcept
	{
		if (hwnd && !Touch::PointerMode()) (void)RegisterTouchWindow(hwnd, 0);
	}

	bool HandleWindowMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, EventSink sink) noexcept
	{
		if (message == WM_TOUCH) return HandleTouch(hwnd, wParam, lParam, sink);
		if (message != WM_POINTERDOWN && message != WM_POINTERUPDATE && message != WM_POINTERUP && message != WM_POINTERCAPTURECHANGED)
			return false;
		if (!Touch::PointerMode()) return false;
		Touch::DecodedPointer decoded;
		switch (Touch::DecodePointer(message, wParam, decoded))
		{
		case Touch::PointerDecode::NotHandled:
			return false;
		case Touch::PointerDecode::CaptureChanged:
			// 系统手势或其他窗口接管后不会再有该 id 的 Up；仍交 DefWindowProc。
			CancelOwned(hwnd, decoded.id, sink);
			return false;
		case Touch::PointerDecode::QueryFailed:
			if (message != WM_POINTERDOWN) CancelOwned(hwnd, decoded.id, sink);
			return false;
		case Touch::PointerDecode::Canceled:
			CancelOwned(hwnd, decoded.id, sink);
			return true;
		case Touch::PointerDecode::Swallow:
			// 第二键/笔桶键不产生右键语义；同 id 的旧接触先撤销。
			CancelOwned(hwnd, decoded.id, sink);
			return true;
		case Touch::PointerDecode::Contact:
			break;
		}
		Apply(hwnd, decoded.sample, (decoded.pointerFlags & POINTER_FLAG_PRIMARY) != 0,
			InputSeconds(decoded.performanceCount, decoded.sample.messageTime), sink);
		return true;
	}

	void CancelContact(EventSink sink) noexcept
	{
		Sample cancelled;
		if (arbiter.Cancel(&cancelled)) Deliver(nullptr, PointerEvent::Type::Cancel, cancelled, InputSeconds(0, GetTickCount()), sink);
	}

	bool FilterChannelMouse(UINT message, WPARAM wParam) noexcept
	{
		if (message < WM_MOUSEFIRST || message > WM_MOUSELAST) return false;
		const auto extraInfo = static_cast<ULONG_PTR>(GetMessageExtraInfo());
		const bool block = ShouldBlockChannelMouse(message, wParam, extraInfo, Touch::PointerMode(), arbiter.Active());
		if (!block)
		{
			// 真实鼠标或笔悬停接管指针位置，结束触摸抬起后的停放。
			std::lock_guard lock(sharedMutex);
			if (overrideMode == OverrideMode::Parked) { overrideMode = OverrideMode::None; parkReleased = true; }
		}
		return block;
	}

	bool TakeParkRelease() noexcept
	{
		const bool released = parkReleased;
		parkReleased = false;
		return released;
	}

	bool ReadPointerOverride(PointerOverride& out) noexcept
	{
		std::lock_guard lock(sharedMutex);
		if (overrideMode == OverrideMode::None) return false;
		out = overrideValue;
		return true;
	}

	std::size_t ReadTrack(std::uint64_t& cursor, std::span<TrackSample> out, bool& overflowed) noexcept
	{
		std::lock_guard lock(sharedMutex);
		return track.Read(cursor, out, overflowed);
	}

#ifdef DRAW3_CONTACT_TESTING
	void Testing::Reset() noexcept
	{
		arbiter = {};
		std::lock_guard lock(sharedMutex);
		track.Reset();
		overrideMode = OverrideMode::None;
		overrideValue = {};
		parkReleased = false;
	}
#endif
}
