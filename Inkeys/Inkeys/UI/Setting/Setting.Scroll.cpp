#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Setting.Scroll.h"
#include "Setting.Controls.h"
#include "Setting.PointerInput.h"
#include "imgui/imgui_internal.h"
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace Inkeys::UI::Setting::Scroll
{
	namespace
	{
		constexpr const char* OverlayLabel = "##scrolldraggingoverlay";

		// 以下状态只在设置渲染线程（ImGui 帧内）访问。
		struct Gesture
		{
			ImGuiID window = 0;
			std::uint64_t generation = 0; // 设置 Pointer 轨迹的接触代次；0 表示无轨迹（如 Win7 笔），回退帧采样
			float overpan = 0.0F;          // 越界累计（像素，顶部为负、底部为正）
			bool active = false;
		} gesture;
		VelocityEstimator frameSamples;

		std::array<PointerInput::TrackSample, 64> history{};
		std::size_t historyCount = 0, historyNext = 0;
		std::uint64_t trackCursor = 0;

		InertiaCurve inertia;
		ImGuiID inertiaWindow = 0;
		double inertiaLast = 0.0;

		PanningFeedback feedback{};
		bool feedbackDirty = false;
		bool panningAvailable = true;
		std::uint64_t lastBoundGeneration = 0;
		// 惯性撞边界的回弹在宿主本帧真正 Begin 时才排队，被省略的宿主不触发整窗回弹。
		struct PendingBounce { ImGuiID window = 0; int frame = -2; float velocity = 0.0F; } pendingBounce;

		float Sign(float value) noexcept { return value < 0.0F ? -1.0F : 1.0F; }
		float MaxOffsetPixels() noexcept { return panningAvailable ? MaxOverpanPixels : 0.0F; }

		void PullTrack() noexcept
		{
			std::array<PointerInput::TrackSample, PointerInput::TrackRing::Capacity> buffer{};
			bool overflowed = false;
			const auto count = PointerInput::ReadTrack(trackCursor, buffer, overflowed);
			for (std::size_t index = 0; index < count; ++index)
			{
				history[historyNext] = buffer[index];
				historyNext = (historyNext + 1) % history.size();
				historyCount = (std::min)(historyCount + 1, history.size());
			}
		}

		// 按 ImGui 实际消费的按下位置与设备匹配轨迹中的 Down，取最新且尚未绑定过的一项：
		// 同一位置先前未被抓取的点按不会被误绑；设备不符（如 Win7 笔走兼容鼠标、无轨迹）不绑定。
		std::uint64_t PressGeneration(const ImVec2& press, ImGuiMouseSource source) noexcept
		{
			const auto expected = source == ImGuiMouseSource_Pen ? PointerInput::Source::Pen : PointerInput::Source::Touch;
			for (std::size_t offset = 1; offset <= historyCount; ++offset)
			{
				const auto& sample = history[(historyNext + history.size() - offset) % history.size()];
				if (sample.kind == PointerInput::Kind::Down && sample.source == expected && sample.generation > lastBoundGeneration
					&& std::abs(sample.x - press.x) <= 1.0F && std::abs(sample.y - press.y) <= 1.0F)
					return lastBoundGeneration = sample.generation;
			}
			return 0;
		}

		// 抬起时手指速度（像素/秒，向下为正）：优先用设置 Pointer 轨迹的输入时间戳，无轨迹时回退帧采样。
		float ReleaseFingerVelocity()
		{
			if (gesture.generation != 0)
			{
				VelocityEstimator track;
				double release = 0.0;
				bool cancelled = false;
				for (std::size_t offset = historyCount; offset > 0; --offset)
				{
					const auto& sample = history[(historyNext + history.size() - offset) % history.size()];
					if (sample.generation != gesture.generation) continue;
					if (sample.kind == PointerInput::Kind::Cancel) { cancelled = true; continue; }
					track.Add(sample.timeSeconds, sample.y);
					release = sample.timeSeconds;
				}
				// 系统取消的接触不产生惯性。
				if (cancelled) return 0.0F;
				if (track.Count() >= 2) return track.Estimate(release);
			}
			return frameSamples.Estimate(GImGui->Time);
		}

		void SetDragFeedback(float overpan)
		{
			const bool active = overpan != 0.0F;
			const int offset = static_cast<int>(std::lround(-OverpanOffset(overpan, MaxOffsetPixels())));
			if (active == feedback.active && offset == feedback.offsetY) return;
			feedback.active = active;
			feedback.offsetY = active ? offset : 0;
			feedbackDirty = true;
		}

		void QueueBounce(float velocity)
		{
			if (!panningAvailable) return;
			// 顶部撞击（向上滚动，速度为负）窗口下移，底部撞击窗口上移。
			const float magnitude = (std::min)(std::abs(velocity) * BounceSecondsPerVelocity, MaxOffsetPixels());
			const int offset = static_cast<int>(std::lround(-Sign(velocity) * magnitude));
			if (offset == 0) return;
			feedback.bounce = true;
			feedback.bounceY = offset;
			feedbackDirty = true;
		}

		bool DragEligible(ImGuiWindow* window, const Design::NavigationState& state, bool submitted)
		{
			if (!window || (submitted ? !window->Active : !window->WasActive) || window->Hidden
				|| window->ScrollMax.y <= 0.0F
				|| (window->Flags & (ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_Tooltip))) return false;
			for (auto* ancestor = window; ancestor; ancestor = ancestor->ParentWindow)
				if ((ancestor->Flags & ImGuiWindowFlags_Popup)
					&& !ImGui::IsPopupOpen(ancestor->PopupId, ImGuiPopupFlags_AnyPopupLevel)) return false;
			if (window->ID == state.flyoutWindowId) return state.flyoutOpen && !state.mouseReleaseBarrier;
			return !state.BlockContentInput() || (window->ID == state.navigationScrollWindowId && !state.mouseReleaseBarrier);
		}

		void CaptureDrag(ImGuiWindow* owner)
		{
			ImGuiContext& g = *GImGui;
			ImGui::SetActiveID(WindowStateId(owner, OverlayLabel), owner);
			g.ActiveIdMouseButton = ImGuiMouseButton_Left;
			g.MovingWindow = nullptr;
			owner->StateStorage.SetFloat(WindowStateId(owner, "##drag-scroll-position"), owner->Scroll.y);
			// 新抓取：按本次按下位置绑定设置 Pointer 接触的代次（找不到则回退帧采样），速度样本与越界从零开始。
			gesture = {};
			gesture.window = owner->ID;
			gesture.generation = g.IO.MouseSource != ImGuiMouseSource_Mouse
				? PressGeneration(g.IO.MouseClickedPos[ImGuiMouseButton_Left], g.IO.MouseSource) : 0;
			gesture.active = true;
			frameSamples.Reset();
		}

		void FinishGesture(ImGuiWindow* window, bool released)
		{
			ImGuiContext& g = *GImGui;
			// 越界中松手只回弹，不再起惯性；鼠标不产生惯性。
			if (released && window && gesture.overpan == 0.0F && g.IO.MouseSource != ImGuiMouseSource_Mouse)
			{
				if (ImGui::IsMousePosValid()) frameSamples.Add(g.Time, g.IO.MousePos.y);
				const float velocity = -ReleaseFingerVelocity();
				if (std::abs(velocity) >= Design::Pixels(MinFlingDipPerSecond))
				{
					const float limit = Design::Pixels(MaxFlingDipPerSecond);
					inertia.Start(window->Scroll.y, std::clamp(velocity, -limit, limit), g.Time);
					inertiaWindow = window->ID;
					inertiaLast = g.Time;
				}
			}
			gesture = {};
			frameSamples.Reset();
			SetDragFeedback(0.0F);
		}

		void StopInertia() noexcept
		{
			inertia.Stop();
			inertiaWindow = 0;
		}

		void AdvanceInertia(const Design::NavigationState& state)
		{
			if (!inertia.Active()) return;
			ImGuiContext& g = *GImGui;
			ImGuiWindow* window = ImGui::FindWindowByID(inertiaWindow);
			const double now = g.Time;
			// 滚轮、键盘/导航设置的原生目标、滚动条、宿主消失或长时间未出帧都立即停止。
			if (!window || !DragEligible(window, state, false) || now - inertiaLast > 0.25
				|| g.IO.MouseWheel != 0.0F || g.IO.MouseWheelH != 0.0F || window->ScrollTarget.y < FLT_MAX
				|| g.ActiveId == ImGui::GetWindowScrollbarID(window, ImGuiAxis_Y))
			{
				StopInertia();
				return;
			}
			if (g.IO.MouseClicked[ImGuiMouseButton_Left] || g.IO.MouseClicked[ImGuiMouseButton_Right])
			{
				const float speed = std::abs(inertia.Velocity(now));
				StopInertia();
				// 惯性中按下立即停住；高速时这次按下由滚动覆盖层接住，不激活下方控件（与 WinUI3 一致）。
				// 触摸/笔接住后可继续拖动滚动；鼠标只停住不拖动。不抢弹层外的按下，留给 ImGui 原生关闭弹层流程。
				if (speed >= Design::Pixels(CatchDipPerSecond)
					&& g.IO.MouseDown[ImGuiMouseButton_Left] && g.HoveredWindow
					&& ImGui::IsWindowContentHoverable(g.HoveredWindow, ImGuiHoveredFlags_None)
					&& (g.HoveredWindow == window || ImGui::IsWindowChildOf(g.HoveredWindow, window, false)))
				{
					CaptureDrag(window);
					// 抓住的这一帧只停住：按下前后的指针跳变不能当作拖动位移套用到内容上。
					window->StateStorage.SetInt(WindowStateId(window, "##drag-scroll-sampled-frame"), g.FrameCount);
					// 锁定左键直到松开：直接轮询 IsMouseClicked/IsMouseDown 的控件（如下拉框）也读不到这次按下。
					ImGui::SetKeyOwner(ImGuiKey_MouseLeft, WindowStateId(window, OverlayLabel), ImGuiInputFlags_LockUntilRelease);
				}
				return;
			}
			inertiaLast = now;
			const float maximum = window->ScrollMax.y;
			float position = inertia.Position(now);
			const float velocity = inertia.Velocity(now);
			if (position <= 0.0F || position >= maximum)
			{
				// 内容在边界硬停，剩余动量交给窗口位移回弹（宿主本帧提交时才排队）。
				position = std::clamp(position, 0.0F, maximum);
				pendingBounce = { window->ID, g.FrameCount, velocity };
				StopInertia();
			}
			else if (std::abs(velocity) < Design::Pixels(StopDipPerSecond)) StopInertia();
			// 与拖动相同在宿主本帧真正 Begin 时投递，未提交的宿主不留下过期的原生目标。
			window->StateStorage.SetFloat(WindowStateId(window, "##inertia-position"), position);
			window->StateStorage.SetInt(WindowStateId(window, "##inertia-frame"), g.FrameCount);
		}
	}

	void VelocityEstimator::Add(double time, float position) noexcept
	{
		samples_[next_] = { time, position };
		next_ = (next_ + 1) % Capacity;
		count_ = (std::min)(count_ + 1, Capacity);
	}

	float VelocityEstimator::Estimate(double releaseTime) const noexcept
	{
		if (count_ < 2) return 0.0F;
		const auto at = [&](std::size_t back) -> const Sample& { return samples_[(next_ + Capacity - 1 - back) % Capacity]; };
		const Sample& last = at(0);
		// 停住再抬起：最后一次位置变化距抬起过久时不产生惯性。
		double lastMove = -1.0;
		for (std::size_t back = 1; back < count_; ++back)
			if (std::abs(at(back).position - last.position) > 0.5F) { lastMove = at(back - 1).time; break; }
		if (lastMove < 0.0 || releaseTime - lastMove > StillReleaseSeconds) return 0.0F;
		double sumT = 0.0, sumY = 0.0;
		std::size_t n = 0;
		for (std::size_t back = 0; back < count_; ++back)
		{
			const Sample& sample = at(back);
			if (releaseTime - sample.time > VelocityWindowSeconds) break;
			sumT += sample.time; sumY += sample.position; ++n;
		}
		if (n < 2) return 0.0F;
		const double meanT = sumT / static_cast<double>(n), meanY = sumY / static_cast<double>(n);
		double covariance = 0.0, variance = 0.0;
		for (std::size_t back = 0; back < n; ++back)
		{
			const Sample& sample = at(back);
			covariance += (sample.time - meanT) * (sample.position - meanY);
			variance += (sample.time - meanT) * (sample.time - meanT);
		}
		return variance > 1e-9 ? static_cast<float>(covariance / variance) : 0.0F;
	}

	void InertiaCurve::Start(float position, float velocity, double time) noexcept
	{
		origin_ = position;
		velocity_ = velocity;
		start_ = time;
		active_ = velocity != 0.0F;
	}

	namespace
	{
		const double DecayLambda = -std::log(1.0 - static_cast<double>(InertiaDecayRate));
	}

	float InertiaCurve::Position(double time) const noexcept
	{
		const double elapsed = (std::max)(0.0, time - start_);
		return origin_ + static_cast<float>(velocity_ * (1.0 - std::exp(-DecayLambda * elapsed)) / DecayLambda);
	}

	float InertiaCurve::Velocity(double time) const noexcept
	{
		const double elapsed = (std::max)(0.0, time - start_);
		return static_cast<float>(velocity_ * std::exp(-DecayLambda * elapsed));
	}

	float OverpanOffset(float overpan, float maxOffset) noexcept
	{
		return std::clamp(overpan * OverpanResistance, -maxOffset, maxOffset);
	}

	float OverpanLimit(float maxOffset) noexcept
	{
		return maxOffset / OverpanResistance;
	}

	bool TakePanningFeedback(PanningFeedback& out) noexcept
	{
		if (!feedbackDirty) return false;
		out = feedback;
		feedback.bounce = false;
		feedback.bounceY = 0;
		feedbackDirty = false;
		return true;
	}

	bool Animating() noexcept
	{
		return inertia.Active();
	}

	void SetPanningFeedbackAvailable(bool available) noexcept
	{
		panningAvailable = available;
		if (available) return;
		// 降级为只硬停：丢弃已累计但不再有视觉反馈的越界量，反向拖动立即滚动内容。
		gesture.overpan = 0.0F;
		pendingBounce = {};
		if (feedback.active || feedback.bounce) { feedback = {}; feedbackDirty = true; }
	}

	void Cancel() noexcept
	{
		StopInertia();
		gesture = {};
		frameSamples.Reset();
		if (feedback.active) { feedback.active = false; feedback.offsetY = 0; feedbackDirty = true; }
	}

	ImGuiID WindowStateId(ImGuiWindow* window, const char* label)
	{
		// 未提交页面经 ImGui 内存回收后 IDStack 为空；窗口状态始终以稳定窗口 ID 为种子。
		return ImHashStr(label, 0, window->ID);
	}

	void ConsumePendingScroll(ImGuiWindow* window)
	{
		if (!window) return;
		// 惯性位置同样只在本帧真正提交此宿主时投递；他人已设置原生目标时让位。
		if (window->StateStorage.GetInt(WindowStateId(window, "##inertia-frame"), -2) == GImGui->FrameCount)
		{
			if (window->ScrollTarget.y == FLT_MAX)
				ImGui::SetScrollY(window, window->StateStorage.GetFloat(WindowStateId(window, "##inertia-position")));
			window->StateStorage.SetInt(WindowStateId(window, "##inertia-frame"), -2);
			if (pendingBounce.window == window->ID && pendingBounce.frame == GImGui->FrameCount) QueueBounce(pendingBounce.velocity);
			pendingBounce = {};
		}
		// 只在本帧真正提交此宿主时消费；未提交/关闭的旧 Popup 不留下 target。
		if (GImGui->ActiveIdWindow == window && GImGui->ActiveId == WindowStateId(window, OverlayLabel)
			&& window->StateStorage.GetInt(WindowStateId(window, "##drag-scroll-frame"), -2) == GImGui->FrameCount)
		{
			if (window->ScrollTarget.y == FLT_MAX)
				ImGui::SetScrollY(window, window->StateStorage.GetFloat(WindowStateId(window, "##drag-scroll-position")));
			else window->StateStorage.SetInt(WindowStateId(window, "##drag-scroll-rebase"), 1);
			window->StateStorage.SetInt(WindowStateId(window, "##drag-scroll-frame"), -2);
		}
	}

	void ResetWindow(ImGuiWindow* window)
	{
		if (!window) return;
		// 必须在 Begin 消费旧惯性目标之前归零，第一帧布局直接使用顶部坐标。
		window->Scroll.y = 0; window->ScrollTarget.y = FLT_MAX;
		window->StateStorage.SetFloat(WindowStateId(window, "##drag-scroll-position"), 0);
		window->StateStorage.SetInt(WindowStateId(window, "##drag-scroll-frame"), -2);
		window->StateStorage.SetInt(WindowStateId(window, "##drag-scroll-sampled-frame"), -2);
		window->StateStorage.SetInt(WindowStateId(window, "##drag-scroll-rebase"), 1);
		window->StateStorage.SetInt(WindowStateId(window, "##inertia-frame"), -2);
		if (inertiaWindow == window->ID) StopInertia();
	}

#ifdef DRAW3_CONTACT_TESTING
	void Testing::Reset() noexcept
	{
		gesture = {};
		frameSamples.Reset();
		historyCount = historyNext = 0;
		std::array<PointerInput::TrackSample, PointerInput::TrackRing::Capacity> drain{};
		bool overflowed = false;
		while (PointerInput::ReadTrack(trackCursor, drain, overflowed) != 0) {}
		StopInertia();
		feedback = {};
		feedbackDirty = false;
		panningAvailable = true;
		lastBoundGeneration = 0;
		pendingBounce = {};
	}
#endif
}

namespace Inkeys::UI::Setting::Design
{
	using Scroll::WindowStateId;

	void AllowContentDrag()
	{
		// 仅登记内容型点击项；越过原生阈值才转抓，滑块/输入等保持原操作。
		auto* window = ImGui::GetCurrentWindow();
		if (GImGui->ActiveId == GImGui->LastItemData.ID)
			window->StateStorage.SetInt(ImHashStr("##content-drag-button", 0, window->ID), static_cast<int>(GImGui->ActiveId));
	}

	void BeginDragScrolling(const NavigationState& state)
	{
		ImGuiContext& g = *GImGui;
		Scroll::PullTrack();
		Scroll::AdvanceInertia(state);
		auto* owner = g.ActiveIdWindow;
		if (!g.IO.MouseDown[0] || !g.IO.MouseDownOwned[0]) return;
		// 内容型按钮越过原生阈值转为页面滚动；鼠标不拖动滚动内容（只用滚轮与滚动条）。
		if (g.IO.MouseSource != ImGuiMouseSource_Mouse && owner && g.ActiveId
			&& g.ActiveId == static_cast<ImGuiID>(owner->StateStorage.GetInt(ImHashStr("##content-drag-button", 0, owner->ID)))
			&& ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			while (owner && !Scroll::DragEligible(owner, state, false))
			{
				if (owner->Flags & ImGuiWindowFlags_Popup) { owner = nullptr; break; }
				owner = owner->ParentWindow;
			}
			if (owner) Scroll::CaptureDrag(owner);
		}
		if (!Scroll::DragEligible(owner, state, false) || g.ActiveId != WindowStateId(owner, Scroll::OverlayLabel)) return;
		ImGui::KeepAliveID(g.ActiveId);
		// 鼠标只可能经惯性接住进入覆盖层：保持接住直到松开，但不拖动滚动内容。
		if (g.IO.MouseSource == ImGuiMouseSource_Mouse) return;
		if (ImGui::IsMousePosValid()) Scroll::frameSamples.Add(g.Time, g.IO.MousePos.y);
		if (g.IO.MouseDelta.y == 0.0F) return;
		const ImGuiID sampledId = WindowStateId(owner, "##drag-scroll-sampled-frame");
		if (owner->StateStorage.GetInt(sampledId, -2) == g.FrameCount) return;
		owner->StateStorage.SetInt(sampledId, g.FrameCount);
		// 原生滚轮/键盘已有目标时先让它消费，本帧不覆盖它，下一帧从真实 Scroll 重基准。
		const ImGuiID rebaseId = WindowStateId(owner, "##drag-scroll-rebase");
		if (owner->ScrollTarget.y < FLT_MAX) { owner->StateStorage.SetInt(rebaseId, 1); return; }
		const ImGuiID positionId = WindowStateId(owner, "##drag-scroll-position");
		float position = owner->StateStorage.GetFloat(positionId, owner->Scroll.y);
		if (owner->StateStorage.GetInt(rebaseId) || std::abs((std::min)(ImRound64(position), owner->ScrollMax.y) - owner->Scroll.y) > 0.01F) position = owner->Scroll.y;
		owner->StateStorage.SetInt(rebaseId, 0);
		// 越界：内容在边界硬停，超出量累计为窗口位移；反向时先回收越界再滚动内容。
		float delta = -g.IO.MouseDelta.y;
		float& overpan = Scroll::gesture.overpan;
		if (overpan < 0.0F && delta > 0.0F) { const float take = (std::min)(delta, -overpan); overpan += take; delta -= take; }
		else if (overpan > 0.0F && delta < 0.0F) { const float take = (std::min)(-delta, overpan); overpan -= take; delta += take; }
		const float raw = position + delta;
		position = std::clamp(raw, 0.0F, owner->ScrollMax.y);
		const float limit = Scroll::OverpanLimit(Scroll::MaxOffsetPixels());
		overpan = std::clamp(overpan + raw - position, -limit, limit);
		Scroll::SetDragFeedback(overpan);
		owner->StateStorage.SetFloat(positionId, position);
		// 暂存单宿主意图；实际首次 Begin 前投递原生目标，不后移光标或顶点。
		owner->StateStorage.SetInt(WindowStateId(owner, "##drag-scroll-frame"), g.FrameCount);
	}

	void ApplyDragScrolling(const NavigationState& state)
	{
		ImGuiContext& g = *GImGui;
		auto* captured = g.ActiveIdWindow;
		if (captured && g.ActiveId == WindowStateId(captured, Scroll::OverlayLabel))
		{
			if (!g.IO.MouseDown[0] || !Scroll::DragEligible(captured, state, true))
			{
				const bool released = !g.IO.MouseDown[0] && Scroll::DragEligible(captured, state, true);
				ImGui::ClearActiveID();
				Scroll::FinishGesture(captured, released);
			}
			else ImGui::KeepAliveID(g.ActiveId);
			return;
		}
		// 抓取被外部清除（会话重置、其他控件夺取）时收尾越界反馈，不起惯性。
		if (Scroll::gesture.active) Scroll::FinishGesture(nullptr, false);
		if (!g.IO.MouseClicked[0] || !g.IO.MouseDownOwned[0] || !g.HoveredWindow) return;
		// 鼠标只用滚轮与滚动条，按住内容拖动不滚动。
		if (g.IO.MouseSource == ImGuiMouseSource_Mouse) return;
		// 弹层外的按下留给 ImGui 原生关闭流程，不能先取得滚动捕获而阻断 EndFrame 的焦点处理。
		if (!ImGui::IsWindowContentHoverable(g.HoveredWindow, ImGuiHoveredFlags_None)) return;
		// 真正业务控件的 ActiveId 保留；child/root 的 MoveId 属于可抓取内容空白。
		if (g.ActiveId && g.ActiveId != g.HoveredWindow->MoveId
			&& g.ActiveId != g.HoveredWindow->RootWindow->MoveId) return;
		auto* owner = g.HoveredWindow;
		while (owner && !Scroll::DragEligible(owner, state, true))
		{
			// 无滚动范围的浮层不向其背景页面传播拖动。
			if (owner->Flags & ImGuiWindowFlags_Popup) { owner = nullptr; break; }
			owner = owner->ParentWindow;
		}
		if (owner) Scroll::CaptureDrag(owner);
	}
}
