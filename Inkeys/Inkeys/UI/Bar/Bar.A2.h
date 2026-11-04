#pragma once

#include <functional>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <utility>

namespace Inkeys::UI::Bar
{
	enum class BarWhiteboardButtonAction { Open, Close, Restore };
	[[nodiscard]] constexpr BarWhiteboardButtonAction ResolveBarWhiteboardButtonAction(
		bool requested, bool minimized) noexcept
	{
		return minimized ? BarWhiteboardButtonAction::Restore
			: requested ? BarWhiteboardButtonAction::Close : BarWhiteboardButtonAction::Open;
	}
	template<class Restore, class Request>
	void DispatchBarWhiteboardButtonAction(bool requested, bool minimized,
		Restore&& restore, Request&& request)
	{
		const auto action = ResolveBarWhiteboardButtonAction(requested, minimized);
		if (action == BarWhiteboardButtonAction::Restore)
			// 恢复失败也保持原白板，不回落成退出或重新进入事务。
			std::forward<Restore>(restore)();
		else std::forward<Request>(request)(action == BarWhiteboardButtonAction::Open);
	}
	template<class Restore, class Request, class CancelFailure>
	void DispatchBarWhiteboardButtonAction(bool requested, bool minimized, std::uint64_t failureRevision,
		Restore&& restore, Request&& request, CancelFailure&& cancel)
	{
		// 失败恢复仍可能实际最小化；此时按钮优先取消捕获的故障，不再重复无效 Resume。
		if (requested && failureRevision) { std::forward<CancelFailure>(cancel)(failureRevision); return; }
		DispatchBarWhiteboardButtonAction(requested, minimized, std::forward<Restore>(restore), std::forward<Request>(request));
	}
	struct BarA2Projection
	{
		bool whiteboardTwoTwo = false;
		bool freezeVisible = true;
		bool endShowVisible = false;
		bool freezeTwoTwo = true;
		bool pierceVisible = false;
		bool pierceTwoTwo = false;
	};

	[[nodiscard]] constexpr BarA2Projection ResolveBarA2Projection(
		bool presentationActive, bool whiteboardActive,
		bool whiteboardFeatureEnabled = true, bool separateMode = false,
		bool selectionEmpty = false, bool freezeAvailable = true) noexcept
	{
		if (whiteboardActive) return { whiteboardFeatureEnabled, false, false };
		BarA2Projection result;
		result.whiteboardTwoTwo = whiteboardFeatureEnabled;
		result.freezeVisible = freezeAvailable && !presentationActive;
		result.endShowVisible = presentationActive;
		// 穿透依附定格的位置；空页选择不需要穿透，PPT 禁用定格时让穿透独占一格。
		result.pierceVisible = separateMode && !selectionEmpty;
		result.freezeTwoTwo = !result.pierceVisible;
		result.pierceTwoTwo = !result.freezeVisible;
		return result;
	}

	[[nodiscard]] constexpr bool IsLegacyBarA2Pair(
		std::string_view first, std::string_view second) noexcept
	{
		return (first == "Inkeys.Bar.Whiteboard" && second == "Inkeys.Bar.Freeze")
			|| (first == "Inkeys.Bar.Freeze" && second == "Inkeys.Bar.Whiteboard");
	}

	class BarA2CallbackDispatcher
	{
	public:
		void Set(std::function<void()> callback)
		{
			SetStamped(callback ? std::function<void(std::uint64_t)>(
				[callback = std::move(callback)](std::uint64_t) { callback(); }) : nullptr);
		}
		void SetStamped(std::function<void(std::uint64_t)> callback)
		{
			std::scoped_lock lock(mutex_);
			callback_ = std::move(callback);
			if (!callback_) outstanding_ = 0;
		}
		[[nodiscard]] bool Dispatch()
		{
			std::function<void(std::uint64_t)> callback;
			std::uint64_t request = 0;
			{
				std::scoped_lock lock(mutex_);
				if (!callback_ || outstanding_ != 0) return false;
				callback = callback_;
				request = outstanding_ = ++sequence_;
			}
			// 锁外投递；异常也结束本次请求，旧完成不能解除新请求的 single-flight。
			try { callback(request); }
			catch (...) { Complete(request); return false; }
			return true;
		}
		void Complete(std::uint64_t request = 0) noexcept
		{
			std::scoped_lock lock(mutex_);
			if (request == 0 || request == outstanding_) outstanding_ = 0;
		}
	private:
		std::mutex mutex_;
		std::function<void(std::uint64_t)> callback_;
		std::uint64_t sequence_ = 0;
		std::uint64_t outstanding_ = 0;
	};
}
