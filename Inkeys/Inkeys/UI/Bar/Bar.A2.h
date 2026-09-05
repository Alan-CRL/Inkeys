#pragma once

#include <functional>
#include <mutex>
#include <string_view>
#include <utility>

namespace Inkeys::UI::Bar
{
	struct BarA2Projection
	{
		bool whiteboardTwoTwo = false;
		bool freezeVisible = true;
		bool endShowVisible = false;
	};

	[[nodiscard]] constexpr BarA2Projection ResolveBarA2Projection(
		bool presentationActive, bool whiteboardActive,
		bool whiteboardFeatureEnabled = true) noexcept
	{
		if (!whiteboardFeatureEnabled)
			return { false, true, presentationActive };
		if (whiteboardActive) return { true, false, false };
		// PPT 中保留定格入口，由按钮状态显示为禁用而不是隐藏。
		if (presentationActive) return { false, true, true };
		return { false, true, false };
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
			std::scoped_lock lock(mutex_);
			callback_ = std::move(callback);
			if (!callback_) outstanding_ = false;
		}

		[[nodiscard]] bool Dispatch()
		{
			std::function<void()> callback;
			{
				std::scoped_lock lock(mutex_);
				if (!callback_ || outstanding_) return false;
				callback = callback_;
				outstanding_ = true;
			}
			// 业务回调必须在锁外执行，允许回调安全地反向注销自身。
			callback();
			return true;
		}

		void Complete() noexcept
		{
			std::scoped_lock lock(mutex_);
			outstanding_ = false;
		}

	private:
		std::mutex mutex_;
		std::function<void()> callback_;
		bool outstanding_ = false;
	};
}
