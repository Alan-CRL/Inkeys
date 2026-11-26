#pragma once

#include <Windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include "../../Input/UiPointerInput.h"

// 设置窗口的触摸/笔输入：与主栏同一分层（Pointer 解码 → 接触裁决 → 值型样本 → 消费者），
// 但设置窗口位于 setting owner 线程，不登记进五窗触摸路由表，而使用本地单窗口、单接触路由。
// 消费者是 ImGui IO：窗口线程把裁决结果交给 EventSink，由 Setting.cpp 在 ImGui 锁内写入。
namespace Inkeys::UI::Setting::PointerInput
{
	using Sample = Input::UiPointer::Sample;
	using Source = Input::UiPointer::Source;
	using Kind = Input::UiPointer::Kind;

	// 触摸/笔起始容差：越过前报给 ImGui 的位置钉在按下点，吸收按住时的自然抖动
	// （系统派生兼容鼠标时同样会吸收）；越过后如实跟随。
	// 取值与主栏主按钮点击/拖动分界一致（ShouldAllowBarBottomDockClick：20 物理像素，不随 DPI 缩放）。
	inline constexpr LONG ContactSlopPixels = 20;

	// 单接触裁决：只裁决，不访问 ImGui/Win32，便于无窗口测试。
	class ContactArbiter
	{
	public:
		enum class Verdict { Ignore, Begin, Update, End };
		struct Result
		{
			Verdict verdict = Verdict::Ignore;
			bool replaced = false;   // 新 Down 先撤销了丢失终态的旧接触
			Sample replacedLatest{};
		};
		// Down/Move/Up；Begin 时分配新代次，Update/End 只接受同设备、同触点的活动接触。
		// slopPixels 仅在 Down 时记录：越过前的 Move 返回 Ignore，越过前的 Up 位置回到按下点。
		Result Accept(Sample sample, bool primary, LONG slopPixels = 0) noexcept;
		bool Cancel(Sample* cancelled = nullptr) noexcept;
		// 只撤销指定 pointerId 的活动接触（捕获丢失、查询失败、系统取消）。
		bool CancelContact(DWORD contact, Sample* cancelled = nullptr) noexcept;
		bool Active() const noexcept { return active_; }
		const Sample& Latest() const noexcept { return latest_; }
	private:
		Sample latest_{};
		std::uint64_t generation_ = 0;
		POINT down_{};
		LONG slop_ = 0;
		bool active_ = false;
		bool moved_ = false;
	};

	// 写入 ImGui 的事件；坐标为客户区物理像素。
	struct PointerEvent
	{
		enum class Type { Begin, Update, End, Cancel };
		Type type = Type::Update;
		Source source = Source::Touch;
		float x = 0.0F;
		float y = 0.0F;
	};
	using EventSink = void (*)(const PointerEvent&);

	// HiMsg 回调的纯判定：true 表示该派生兼容鼠标消息不得进入 ImGui。
	// PointerMode：带 Pointer 签名的鼠标消息全部拦截，唯独无按键、无活动接触时的笔悬停移动放行；
	// Win7（无 Pointer）：只拦截触摸签名，笔仍经兼容鼠标进入。
	[[nodiscard]] bool ShouldBlockChannelMouse(UINT message, WPARAM wParam, ULONG_PTR extraInfo,
		bool pointerMode, bool contactActive) noexcept;

	// 接触样本轨迹：窗口线程写入，渲染线程增量读取，供页面滚动估计速度。
	struct TrackSample
	{
		Kind kind = Kind::Move;
		Source source = Source::Touch;
		float x = 0.0F;
		float y = 0.0F;
		double timeSeconds = 0.0;
		std::uint64_t generation = 0;
	};
	class TrackRing
	{
	public:
		static constexpr std::size_t Capacity = 64;
		void Push(const TrackSample& sample) noexcept;
		// cursor 为已读总数；落后超过容量时从最旧可用样本继续并置 overflowed。
		std::size_t Read(std::uint64_t& cursor, std::span<TrackSample> out, bool& overflowed) const noexcept;
		void Reset() noexcept { written_ = 0; }
	private:
		std::array<TrackSample, Capacity> samples_{};
		std::uint64_t written_ = 0;
	};

	// ---- 设置窗口线程 ----
	// WM_CREATE：Win7 回退模式注册 WM_TOUCH；PointerMode 下不注册，避免派生副本。
	void ConfigureWindow(HWND hwnd) noexcept;
	// 返回 true 表示已消费，窗口过程直接 return 0。
	bool HandleWindowMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, EventSink sink) noexcept;
	// 隐藏、销毁、取消模式、DPI 变化时结束当前接触，不产生点击。
	void CancelContact(EventSink sink) noexcept;
	// HiMsg 回调：读取 GetMessageExtraInfo 判定；放行的鼠标消息同时结束触摸后的指针停放。
	[[nodiscard]] bool FilterChannelMouse(UINT message, WPARAM wParam) noexcept;

	// ---- 设置渲染线程 ----
	// backend 在窗口有焦点却未跟踪鼠标时会用系统光标补位置；触摸不移动系统光标，
	// 因此每帧在 ImGui_ImplWin32_NewFrame 之后用接触位置（或触摸抬起后的窗口外位置）覆盖。
	struct PointerOverride
	{
		Source source = Source::Touch;
		float x = 0.0F;
		float y = 0.0F;
	};
	[[nodiscard]] bool ReadPointerOverride(PointerOverride& out) noexcept;
	std::size_t ReadTrack(std::uint64_t& cursor, std::span<TrackSample> out, bool& overflowed) noexcept;

#ifdef DRAW3_CONTACT_TESTING
	namespace Testing
	{
		void Reset() noexcept;
	}
#endif
}
