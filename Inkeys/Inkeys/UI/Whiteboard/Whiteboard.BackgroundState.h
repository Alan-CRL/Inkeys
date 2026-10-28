#pragma once

#include <windows.h>
#include <cstdint>
#include <mutex>
#include <optional>

namespace Inkeys::UI::WhiteboardDetail
{
	// 同一业务 revision 仍保留本生命周期序号，防止初始化/关闭及旧兼容调用发生 ABA。
	class BackgroundState
	{
	public:
		struct Request
		{
			bool active = false;
			std::uint64_t revision = 0, serial = 0;
			bool operator==(const Request&) const = default;
		};
		struct Surface
		{
			HWND hwnd = nullptr;
			std::uint64_t generation = 0;
			bool operator==(const Surface&) const = default;
		};
		struct Frame
		{
			Request request;
			HWND hwnd = nullptr;
			std::uint64_t generation = 0;
			RECT bounds{};
		};
		void Reset()
		{
			std::scoped_lock lock(mutex_);
			desired_ = {false, 0, desired_.serial + 1};
			committed_ = desired_;
			prepared_.reset(); held_.reset(); owned_ = false;
		}
		bool Publish(bool active, std::uint64_t revision)
		{
			std::scoped_lock lock(mutex_);
			if (revision < desired_.revision) return false;
			if (desired_.active == active && desired_.revision == revision) return false;
			desired_ = {active, revision, desired_.serial + 1};
			owned_ |= active;
			prepared_.reset();
			if (!active && !owned_) committed_ = desired_;
			return true;
		}
		[[nodiscard]] Request Desired() const { std::scoped_lock lock(mutex_); return desired_; }
		[[nodiscard]] bool Current(const Request& request) const
		{ std::scoped_lock lock(mutex_); return desired_ == request; }
		[[nodiscard]] bool InactiveCommitted(const Request& request) const
		{ std::scoped_lock lock(mutex_); return !request.active && request == desired_ && request == committed_; }
		bool CommitInactive(const Request& request)
		{
			std::scoped_lock lock(mutex_);
			if (request.active || request != desired_) return false;
			committed_ = request; prepared_.reset(); held_.reset(); owned_ = false; return true;
		}
		bool Bind(const Request& request, Surface surface)
		{
			std::scoped_lock lock(mutex_);
			if (!request.active || request != desired_ || !surface.hwnd || !surface.generation) return false;
			if (held_ && *held_ != surface) return false;
			if (!held_) { held_ = surface; prepared_.reset(); }
			owned_ = true; return true;
		}
		[[nodiscard]] std::optional<Surface> Held() const
		{ std::scoped_lock lock(mutex_); return held_; }
		void RetireHeld(Surface surface)
		{
			std::scoped_lock lock(mutex_);
			if (!held_ || *held_ != surface) return;
			// 只退休被销毁的旧资源，不对 role 后来的 HWND 产生隐藏或可见性确认。
			held_.reset(); prepared_.reset();
			if (!desired_.active) owned_ = false;
		}
		bool Prepare(const Frame& frame)
		{
			std::scoped_lock lock(mutex_);
			if (!frame.request.active || frame.request != desired_ || !frame.hwnd || !frame.generation) return false;
			const Surface surface{frame.hwnd, frame.generation};
			if (held_ && *held_ != surface) return false;
			held_ = surface; owned_ = true;
			prepared_ = frame; return true;
		}
		[[nodiscard]] std::optional<Frame> Prepared(std::uint64_t revision) const
		{
			std::scoped_lock lock(mutex_);
			return desired_.active && (!revision || revision == desired_.revision)
				&& prepared_ && prepared_->request == desired_ ? prepared_ : std::nullopt;
		}
		void Invalidate() { std::scoped_lock lock(mutex_); prepared_.reset(); }
	private:
		mutable std::mutex mutex_;
		Request desired_{}, committed_{};
		std::optional<Frame> prepared_;
		std::optional<Surface> held_;
		bool owned_ = false;
	};

	template<class Hide>
	[[nodiscard]] bool CompleteBackgroundInactive(BackgroundState& state, const BackgroundState::Request& request, Hide hide)
	{
		if (request.active) return false;
		if (state.InactiveCommitted(request)) return true;
		if (!state.Current(request)) return false;
		const auto held = state.Held();
		// 未提交任何白板表面或旧代际已经退休，不得按当前 role 隐藏别的用途窗口。
		return (!held || hide(*held)) && state.CommitInactive(request);
	}
}
