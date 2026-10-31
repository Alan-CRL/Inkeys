#pragma once

#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <optional>

namespace Inkeys::Window::Detail
{
	using CommandDeadline = std::optional<std::chrono::steady_clock::time_point>;
	enum class CommandPhase { Queued, Executing, Committed, Rejected, Expired, Cancelled };

	[[nodiscard]] inline bool CommandExpired(const CommandDeadline& deadline) noexcept
	{
		return deadline && std::chrono::steady_clock::now() >= *deadline;
	}

	class CommandCompletion
	{
	public:
		explicit CommandCompletion(CommandDeadline deadline) : deadline_(deadline) {}
		[[nodiscard]] CommandPhase Phase() const noexcept
		{
			std::scoped_lock lock(mutex_);
			return phase_;
		}
		[[nodiscard]] bool Current() const noexcept
		{
			std::scoped_lock lock(mutex_);
			return !cancelled_ && !CommandExpired(deadline_);
		}
		void Cancel() noexcept
		{
			std::scoped_lock lock(mutex_);
			if (phase_ != CommandPhase::Queued && phase_ != CommandPhase::Executing) return;
			cancelled_ = true;
			if (phase_ == CommandPhase::Queued)
				phase_ = CommandExpired(deadline_) ? CommandPhase::Expired : CommandPhase::Cancelled;
			changed_.notify_all();
		}
		[[nodiscard]] bool Begin() noexcept
		{
			std::scoped_lock lock(mutex_);
			if (phase_ != CommandPhase::Queued) return false;
			if (cancelled_ || CommandExpired(deadline_))
			{
				phase_ = CommandExpired(deadline_) ? CommandPhase::Expired : CommandPhase::Cancelled;
				changed_.notify_all();
				return false;
			}
			phase_ = CommandPhase::Executing;
			return true;
		}
		// 提交与超时争用同一边界；false 时 owner 仍须先撤回物理副作用，才可发布终态。
		[[nodiscard]] bool Commit(bool succeeded) noexcept
		{
			std::scoped_lock lock(mutex_);
			if (cancelled_ || CommandExpired(deadline_)) return false;
			phase_ = succeeded ? CommandPhase::Committed : CommandPhase::Rejected;
			changed_.notify_all();
			return true;
		}
		void Retired() noexcept
		{
			std::scoped_lock lock(mutex_);
			phase_ = CommandExpired(deadline_) ? CommandPhase::Expired : CommandPhase::Cancelled;
			changed_.notify_all();
		}
		[[nodiscard]] bool Wait()
		{
			std::unique_lock lock(mutex_);
			const auto done = [this] { return phase_ != CommandPhase::Queued && phase_ != CommandPhase::Executing; };
			if (deadline_ && !changed_.wait_until(lock, *deadline_, done))
			{
				cancelled_ = true;
				// 在途命令继续标记 Executing，不能把停止等待伪装成物理操作从未发生。
				if (phase_ == CommandPhase::Queued) phase_ = CommandPhase::Expired;
				return false;
			}
			if (!deadline_) changed_.wait(lock, done);
			return phase_ == CommandPhase::Committed;
		}
	private:
		CommandDeadline deadline_;
		mutable std::mutex mutex_;
		std::condition_variable changed_;
		CommandPhase phase_ = CommandPhase::Queued;
		bool cancelled_ = false;
	};

	[[nodiscard]] inline bool WaitForCommandResult(std::future<bool>& result,
		const CommandDeadline& deadline)
	{
		if (deadline && result.wait_until(*deadline) != std::future_status::ready) return false;
		return result.get();
	}
}
