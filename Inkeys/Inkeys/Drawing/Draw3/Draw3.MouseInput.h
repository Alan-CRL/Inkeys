#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <windows.h>

#include "Draw3.SpeedEraser.h"

namespace Inkeys::Drawing::Draw3::Detail
{
	inline constexpr unsigned kMouseInputOwnerMessage = 0x8000u + 0x3D4u;
	inline constexpr std::uintptr_t kMouseInputStop = 0;
	inline constexpr std::uintptr_t kMouseInputStart = 1;

	// 兜底鼠标的“选择隔离”：白板移动模式下鼠标左键是平移触点，必须送达控制器；
	// 桌面/PPT 的 Selection 仍隔离。
	constexpr bool MouseSelectionIsolates(bool selectionMode, bool canvasPanMode) noexcept
	{
		return selectionMode && !canvasPanMode;
	}

	// 选择位与已确认工作区直接派生平移模式，避免两个独立模式发布之间丢失首个 Down。
	class MouseSelectionModeState
	{
	public:
		bool Selection() const noexcept { return selection_.load(std::memory_order_acquire); }
		bool SetSelection(bool enabled) noexcept
		{ return selection_.exchange(enabled, std::memory_order_acq_rel) != enabled; }
		void SetWhiteboard(bool enabled) noexcept { whiteboard_.store(enabled, std::memory_order_release); }
		// 新窗口/Host 代次必须重新确认工作区，默认 Desktop 不会另发就绪回调。
		void ResetWorkspace() noexcept { SetWhiteboard(false); }
		bool CanvasPan() const noexcept { return Selection() && whiteboard_.load(std::memory_order_acquire); }
		bool Isolates() const noexcept { return Selection() && !whiteboard_.load(std::memory_order_acquire); }
	private:
		std::atomic<bool> selection_ = true;
		std::atomic<bool> whiteboard_ = false;
	};

	// 只有窗口 owner 读写 contact；跨线程 Stop 先关准入，再由 owner 串行取消并确认。
	// 模板让普通头和模块共用真实 Coordinator，不在无窗口测试中复制输入决策。
	template<class Coordinator, class Snapshot, class Device, class Phase>
	class MouseInputState
	{
	public:
		static constexpr std::uint32_t Left = 1, Right = 2;
		void Start(std::uint32_t heldButtons = 0) noexcept
		{
			physicalButtons_ = 0;
			quarantinedButtons_ = heldButtons;
			admissionHeldButtons_ = 0;
			activeButton_ = 0;
			closed_.store(false, std::memory_order_release);
		}
		void CloseAdmission() noexcept { closed_.store(true, std::memory_order_release); }
		bool Active() const noexcept { return activeButton_ != 0; }
		// 光标接触态：被准入拒收但仍按住的键也算接触，光标按画布帧率跟随（不出墨）；选择隔离不算。
		bool CursorHeld() const noexcept
		{ return Active() || (admissionHeldButtons_ & physicalButtons_) != 0; }
		bool Open() const noexcept { return !closed_.load(std::memory_order_acquire); }

		bool Down(Coordinator& input, std::uint32_t button, float x, float y,
			std::int64_t qpc, bool selectionMode)
		{
			if (((physicalButtons_ | quarantinedButtons_) & button) != 0) return false;
			physicalButtons_ |= button;
			if (!Open()) return false;
			// 左右键只保留一条流；旧键稍后 Up 只清物理位，不能结束新键。
			if (Active()) End(input, x, y, qpc, false);
			if (selectionMode || input.AdmissionBlocked())
			{
				quarantinedButtons_ |= button;
				if (!selectionMode) admissionHeldButtons_ |= button;
				return false;
			}
			Snapshot snapshot = MakeSnapshot(x, y, qpc, Phase::Down);
			if (++contactId_ == 0) ++contactId_;
			snapshot.source.generation = contactId_;
			if (!input.PublishDown(kContext, contactId_,
				button == Left ? Device::MouseLeft : Device::MouseRight, snapshot)) return false;
			activeButton_ = button;
			last_ = snapshot;
			return true;
		}
		void Up(Coordinator& input, std::uint32_t button, float x, float y,
			std::int64_t qpc) noexcept
		{
			physicalButtons_ &= ~button;
			quarantinedButtons_ &= ~button;
			admissionHeldButtons_ &= ~button;
			if (activeButton_ == button) End(input, x, y, qpc, !Open());
		}
		void Move(Coordinator& input, std::uint32_t heldButtons, float x, float y,
			std::int64_t qpc, bool selectionMode) noexcept
		{
			if (!Active())
			{
				// 缺少可靠 Up 时以消息里的按键状态收回准入拒收的光标接触态。
				admissionHeldButtons_ &= heldButtons;
				return;
			}
			// 缺按键、页边界或选择切换不是可靠 Up，收为 Cancelled。
			if (!Open() || selectionMode || input.AdmissionBlocked() ||
				(heldButtons & activeButton_) == 0)
			{
				if (selectionMode || input.AdmissionBlocked()) quarantinedButtons_ |= activeButton_;
				if (!selectionMode && input.AdmissionBlocked() && (heldButtons & activeButton_) != 0)
					admissionHeldButtons_ |= activeButton_;
				Cancel(input, qpc);
				return;
			}
			last_.position = { x, y };
			last_.qpc = qpc;
			last_.phase = Phase::Move;
			input.PublishMove(kContext, contactId_, last_);
		}
		void Cancel(Coordinator& input, std::int64_t qpc) noexcept
		{
			if (Active()) End(input, last_.position.x, last_.position.y, qpc, true);
		}
		void CaptureLost(Coordinator& input, std::int64_t qpc) noexcept
		{
			Cancel(input, qpc);
			// 失捕获后 Up 可在别窗消费；下一次真实 Down 必须重新建立物理跟踪。
			// 启动/选择/页边界隔离独立保留，不能借失捕获重新接纳仍按住的旧键。
			physicalButtons_ = 0;
		}
		void Stop(Coordinator& input, std::int64_t qpc) noexcept
		{
			CloseAdmission();
			Cancel(input, qpc);
		}

	private:
		static constexpr std::uint32_t kContext = 0xFFFFFFFEu;
		static Snapshot MakeSnapshot(float x, float y, std::int64_t qpc, Phase phase) noexcept
		{
			Snapshot snapshot{};
			snapshot.position = { x, y };
			snapshot.qpc = qpc;
			snapshot.phase = phase;
			snapshot.source.kind = decltype(snapshot.source.kind)::Mouse;
			snapshot.source.contextId = kContext;
			return snapshot; // 压力、面积及识别方式保留未知，不冒称 RTS 能力。
		}
		void End(Coordinator& input, float x, float y, std::int64_t qpc, bool cancelled) noexcept
		{
			activeButton_ = 0; // 先清本地状态，终态/捕获丢失重入保持幂等。
			last_.position = { x, y };
			last_.qpc = qpc;
			last_.phase = cancelled ? Phase::Cancelled : Phase::Up;
			if (cancelled) input.PublishCancelled(kContext, contactId_, last_);
			else input.PublishUp(kContext, contactId_, last_);
		}
		std::atomic_bool closed_ = true;
		std::uint32_t physicalButtons_ = 0, quarantinedButtons_ = 0, activeButton_ = 0, contactId_ = 0;
		std::uint32_t admissionHeldButtons_ = 0;
		Snapshot last_{};
	};
}

namespace Inkeys::Drawing::Draw3::PointerInput
{
	inline float Pressure(bool valid, uint32_t pressure) noexcept
	{
		return valid && pressure <= 1024 ? pressure / 1024.0f : -1.0f;
	}

	struct Angles { float tilt = -1.0f, orientation = -1.0f; };

	inline Angles Tilt(bool validX, bool validY, int tiltX, int tiltY) noexcept
	{
		if (!validX || !validY || tiltX < -90 || tiltX > 90 || tiltY < -90 || tiltY > 90)
			return {};
		// 直立笔投影无方向；与 RTS 一致给 0，避免首包 −1 使 Modeler 整笔方位未知。
		if (!tiltX && !tiltY) return { 0.0f, 0.0f };
		constexpr double radians = std::numbers::pi / 180.0;
		const double x = std::tan(tiltX * radians);
		const double y = std::tan(tiltY * radians);
		// Pointer 的正 Y 指向使用者；模型方位角从 +X 逆时针计。
		double orientation = std::atan2(-y, x);
		if (orientation < 0.0) orientation += 2.0 * std::numbers::pi;
		// double 转 float 可能恰好舍入为 2π，超出模型的 [0, 2π) 约定。
		float bearing = static_cast<float>(orientation);
		if (bearing >= static_cast<float>(2.0 * std::numbers::pi)) bearing = 0.0f;
		return { static_cast<float>(std::atan(std::hypot(x, y))), bearing };
	}

	inline SpeedEraser::ContactAreaSample ContactArea(bool valid, const RECT& rect,
		float scaleX = 1.0f, float scaleY = 1.0f) noexcept
	{
		if (!valid || rect.right <= rect.left || rect.bottom <= rect.top ||
			!(scaleX > 0.0f) || !(scaleY > 0.0f)) return {};
		const float w = static_cast<float>(static_cast<double>(rect.right) - rect.left);
		const float h = static_cast<float>(static_cast<double>(rect.bottom) - rect.top);
		if (!std::isfinite(w * scaleX) || !std::isfinite(h * scaleY)) return {};
		return { w, h, w * scaleX, h * scaleY, SpeedEraser::ContactAreaUnits::CanvasPixels };
	}

	struct Position { float x, y; };

	inline Position MapPosition(POINT rawPixel, POINT rawHimetric, POINT clientOrigin,
		const RECT* device, const RECT* display) noexcept
	{
		double x = rawPixel.x, y = rawPixel.y;
		if (device && display && device->right > device->left && device->bottom > device->top &&
			display->right > display->left && display->bottom > display->top &&
			rawHimetric.x >= device->left && rawHimetric.x <= device->right &&
			rawHimetric.y >= device->top && rawHimetric.y <= device->bottom &&
			rawPixel.x >= display->left && rawPixel.x < display->right &&
			rawPixel.y >= display->top && rawPixel.y < display->bottom)
		{
			const auto mapAxis = [](LONG value, LONG from, LONG to, LONG start, LONG end)
			{
				return start + (static_cast<double>(value) - from) *
					(static_cast<double>(end) - start) / (static_cast<double>(to) - from);
			};
			const double mx = mapAxis(rawHimetric.x, device->left, device->right, display->left, display->right);
			const double my = mapAxis(rawHimetric.y, device->top, device->bottom, display->top, display->bottom);
			// 转屏、驱动映射或显示拓扑不一致时，以同包的带符号原始像素为准。
			if (mx < display->right && my < display->bottom &&
				std::abs(mx - x) <= 1.0 && std::abs(my - y) <= 1.0)
			{
				x = mx;
				y = my;
			}
		}
		return { static_cast<float>(x - clientOrigin.x), static_cast<float>(y - clientOrigin.y) };
	}

	inline uint64_t PerformanceCount(uint64_t current, uint64_t previous, int64_t arrivalQpc) noexcept
	{
		// 相等、倒退及晚于到达时刻的样本无法用来测量采样延迟。
		return arrivalQpc > 0 && current > previous && current <= static_cast<uint64_t>(arrivalQpc)
			? current : 0;
	}
}
