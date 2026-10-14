#pragma once

#include <chrono>
#include <future>
#include <optional>

namespace Inkeys::Window::Detail
{
	using CommandDeadline = std::optional<std::chrono::steady_clock::time_point>;

	[[nodiscard]] inline bool CommandExpired(const CommandDeadline& deadline) noexcept
	{
		return deadline && std::chrono::steady_clock::now() >= *deadline;
	}

	[[nodiscard]] inline bool WaitForCommandResult(std::future<bool>& result,
		const CommandDeadline& deadline)
	{
		// 超时只拒绝本次等待；promise 由队列持有，迟到完成不会引用调用线程栈。
		if (deadline && result.wait_until(*deadline) != std::future_status::ready)
			return false;
		return result.get();
	}
}
