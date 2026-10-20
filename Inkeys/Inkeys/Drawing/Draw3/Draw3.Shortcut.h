#pragma once

#include "Draw3.Bridge.h"

#include <array>
#include <mutex>

namespace Inkeys::Drawing::Draw3::Shortcuts
{
	struct Scope
	{
		std::uint64_t hostGeneration = 0;
		std::uint64_t pageEpoch = 0;
		std::uint64_t intentEpoch = 0;
		Bridge::Workspace workspace = Bridge::Workspace::Desktop;
		std::size_t pageIndex = 0;
		std::optional<Bridge::PresentationReadyIdentity> presentation;
		friend bool operator==(const Scope&, const Scope&) noexcept = default;
	};

	struct Context
	{
		Scope scope;
		std::uint64_t contentRevision = 0;
		bool available = false;
		bool hasContent = false;
	};

	enum class Result : std::uint8_t { Applied, NoContent, Stale, Blocked, Failed };

	struct Request
	{
		std::uint64_t requestId = 0;
		Scope scope;
		std::uint64_t leaseId = 0;
		std::optional<std::uint64_t> expectedContentRevision; // Fence 只验证具名 Clear 的后置空态。
	};

	constexpr Result ResolveFence(const Request& request, const Context& actual) noexcept
	{
		return request.leaseId && actual.available && !actual.hasContent &&
			request.expectedContentRevision == actual.contentRevision ? Result::NoContent : Result::Stale;
	}

	struct Completion
	{
		std::uint64_t requestId = 0;
		Result result = Result::Failed;
		Context source;
		Context resultContext;
	};

	// Host 与无窗口测试共用固定回执槽；接纳前预约，消费/退休才释放。
	class State
	{
	public:
		void BeginHost() noexcept
		{
			std::scoped_lock lock(mutex_);
			if (++hostGeneration_ == 0) ++hostGeneration_;
			context_ = {};
			slots_ = {};
			leaseId_ = 0;
		}
		void Stop() noexcept
		{
			std::scoped_lock lock(mutex_);
			context_.available = false;
			slots_ = {};
			leaseId_ = 0;
		}
		void Publish(Context context, std::uint64_t intentEpoch) noexcept
		{
			std::scoped_lock lock(mutex_);
			context.scope.hostGeneration = hostGeneration_;
			context.scope.intentEpoch = intentEpoch;
			context_ = context;
		}
		Context Snapshot() const noexcept
		{
			std::scoped_lock lock(mutex_);
			return context_;
		}
		std::uint64_t Reserve(const Scope& scope) noexcept
		{
			std::scoped_lock lock(mutex_);
			if (!context_.available || context_.scope != scope) return 0;
			for (auto& slot : slots_) if (!slot.completion.requestId)
			{
				if (++nextId_ == 0) ++nextId_;
				slot.completion = { nextId_, Result::Failed, context_, {} };
				slot.ready = false;
				return nextId_;
			}
			return 0;
		}
		Result Authorize(const Request& request, const Context& actual) const noexcept
		{
			std::scoped_lock lock(mutex_);
			bool pending = false;
			for (const auto& slot : slots_)
				if (request.requestId && slot.completion.requestId == request.requestId && !slot.ready)
					pending = true;
			if (!pending || context_.scope != request.scope ||
				actual.scope.pageEpoch != request.scope.pageEpoch ||
				actual.scope.workspace != request.scope.workspace ||
				actual.scope.pageIndex != request.scope.pageIndex ||
				actual.scope.presentation != request.scope.presentation) return Result::Stale;
			if (!context_.available || !actual.available || leaseId_ != request.leaseId)
				return Result::Blocked;
			return Result::Applied;
		}
		void Complete(std::uint64_t id, Result result) noexcept
		{
			std::scoped_lock lock(mutex_);
			for (auto& slot : slots_) if (id && slot.completion.requestId == id && !slot.ready)
			{
				slot.completion.result = result;
				slot.completion.resultContext = context_;
				slot.ready = true;
				return;
			}
		}
		bool Take(std::uint64_t id, Completion& result) noexcept
		{
			std::scoped_lock lock(mutex_);
			for (auto& slot : slots_) if (id && slot.completion.requestId == id && slot.ready)
			{
				result = slot.completion;
				slot = {};
				return true;
			}
			return false;
		}
		void Retire(std::uint64_t id) noexcept
		{
			std::scoped_lock lock(mutex_);
			for (auto& slot : slots_) if (id && slot.completion.requestId == id) slot = {};
		}
		std::uint64_t Acquire(const Context& expected, bool requireContentRevision) noexcept
		{
			std::scoped_lock lock(mutex_);
			if (leaseId_ || !context_.available || context_.scope != expected.scope ||
				(requireContentRevision && context_.contentRevision != expected.contentRevision)) return 0;
			if (++nextLeaseId_ == 0) ++nextLeaseId_;
			return leaseId_ = nextLeaseId_;
		}
		bool Release(std::uint64_t id) noexcept
		{
			std::scoped_lock lock(mutex_);
			if (!id || id != leaseId_) return false;
			leaseId_ = 0;
			return true;
		}
		bool OrdinaryCommandAllowed(Bridge::CommandType command) const noexcept
		{
			std::scoped_lock lock(mutex_);
			// 复合清空/选择短期独占内容事务；不能让普通 Undo 在空态回执与选择之间恢复墨迹。
			return !leaseId_ || (command != Bridge::CommandType::Clear &&
				command != Bridge::CommandType::Undo && command != Bridge::CommandType::Redo);
		}
		std::uint64_t Lease() const noexcept
		{
			std::scoped_lock lock(mutex_);
			return leaseId_;
		}

	private:
		struct Slot { Completion completion; bool ready = false; };
		mutable std::mutex mutex_;
		Context context_;
		std::array<Slot, 64> slots_{};
		std::uint64_t hostGeneration_ = 0, nextId_ = 0;
		std::uint64_t leaseId_ = 0, nextLeaseId_ = 0;
	};
}

namespace Inkeys::Drawing::Draw3
{
	Shortcuts::Context CaptureProductShortcutContext() noexcept;
	std::uint64_t PublishProductShortcutCommand(Bridge::CommandType type,
		const Shortcuts::Scope& expected, std::uint64_t leaseId = 0) noexcept;
	std::uint64_t PublishProductShortcutFence(const Shortcuts::Context& expected,
		std::uint64_t leaseId) noexcept;
	bool TryTakeProductShortcutCompletion(std::uint64_t requestId,
		Shortcuts::Completion& completion) noexcept;
	void RetireProductShortcutRequest(std::uint64_t requestId) noexcept;
	std::uint64_t AcquireProductShortcutLease(const Shortcuts::Context& expected,
		bool requireContentRevision = false) noexcept;
	void ReleaseProductShortcutLease(std::uint64_t leaseId) noexcept;
}
