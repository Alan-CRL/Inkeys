#pragma once

#include <atomic>
#include <cstdint>

namespace Inkeys::Drawing::Draw3::Detail
{
	inline constexpr unsigned kMouseInputOwnerMessage = 0x8000u + 0x3D4u;
	inline constexpr std::uintptr_t kMouseInputStop = 0;
	inline constexpr std::uintptr_t kMouseInputStart = 1;

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
			activeButton_ = 0;
			closed_.store(false, std::memory_order_release);
		}
		void CloseAdmission() noexcept { closed_.store(true, std::memory_order_release); }
		bool Active() const noexcept { return activeButton_ != 0; }
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
			if (activeButton_ == button) End(input, x, y, qpc, !Open());
		}
		void Move(Coordinator& input, std::uint32_t heldButtons, float x, float y,
			std::int64_t qpc, bool selectionMode) noexcept
		{
			if (!Active()) return;
			// 缺按键、页边界或选择切换不是可靠 Up，收为 Cancelled。
			if (!Open() || selectionMode || input.AdmissionBlocked() ||
				(heldButtons & activeButton_) == 0)
			{
				if (selectionMode || input.AdmissionBlocked()) quarantinedButtons_ |= activeButton_;
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
		Snapshot last_{};
	};
}
