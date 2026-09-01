#include "Draw3.Bridge.h"

#include <algorithm>

namespace Inkeys::Drawing::Draw3::Bridge
{
	StateBridge::StateBridge(std::size_t capacity) noexcept
		: capacity_((std::max)(std::size_t{1}, capacity))
	{
	}

	void StateBridge::PublishState(ProductState state) noexcept
	{
		std::scoped_lock lock(mutex_);
		if (!running_) return;
		// 普通工具快照不应把 PPT 已发布的绝对页重置到第一页。
		if (!state.hasPage && state_.hasPage)
		{
			state.page = state_.page;
			state.hasPage = true;
		}
		state.presentationVisitEpoch = state_.presentationVisitEpoch;
		state.revision = state_.revision + 1;
		state_ = state;
	}

	bool StateBridge::PublishWorkspace(Workspace workspace) noexcept
	{
		std::scoped_lock lock(mutex_);
		if (!running_) return false;
		const bool clearPage = workspace != Workspace::Presentation;
		if (state_.workspace == workspace && (!clearPage || !state_.hasPage))
			return false;
		state_.workspace = workspace;
		if (workspace == Workspace::Presentation)
			++state_.presentationVisitEpoch;
		if (clearPage)
		{
			state_.page = 0;
			state_.hasPage = false;
		}
		++state_.revision;
		return true;
	}

	ProductState StateBridge::Snapshot() const noexcept
	{
		std::scoped_lock lock(mutex_);
		return state_;
	}

	CommandResult StateBridge::Publish(CommandType type) noexcept
	{
		std::scoped_lock lock(mutex_);
		if (!running_) return CommandResult::NotRunning;
		// 尚未准备好的能力保留显式空接口，产品入口可据此隐藏。
		if (type == CommandType::Save || type == CommandType::SuperRecovery ||
			type == CommandType::AutoStraighten || type == CommandType::InputTest ||
			type == CommandType::PrepareExitAutoSave)
			return CommandResult::Unsupported;
		if (commands_.size() >= capacity_) return CommandResult::QueueFull;
		try
		{
			commands_.push_back({ type, nextSequence_ });
			++nextSequence_;
		}
		catch (...)
		{
			return CommandResult::QueueFull;
		}
		return CommandResult::Accepted;
	}

	bool StateBridge::TryConsume(Command& command) noexcept
	{
		std::scoped_lock lock(mutex_);
		if (!commands_.empty())
		{
			command = commands_.front();
			commands_.pop_front();
			return true;
		}
		if (!finalCommand_) return false;
		command = *finalCommand_;
		finalCommand_.reset();
		return true;
	}

	void StateBridge::Reset() noexcept
	{
		std::scoped_lock lock(mutex_);
		commands_.clear();
		finalCommand_.reset();
		nextSequence_ = 1;
		state_ = {};
		running_ = true;
	}

	void StateBridge::Stop() noexcept
	{
		std::scoped_lock lock(mutex_);
		running_ = false;
	}

	bool StateBridge::StopWithFinalCommand(CommandType finalCommand) noexcept
	{
		std::scoped_lock lock(mutex_);
		if (!running_) return false;
		running_ = false;
		// 固定槽不受业务队列容量影响，并且只在队列排空后消费。
		finalCommand_ = Command{ finalCommand, nextSequence_++ };
		return true;
	}

	bool StateBridge::Running() const noexcept
	{
		std::scoped_lock lock(mutex_);
		return running_;
	}
}
