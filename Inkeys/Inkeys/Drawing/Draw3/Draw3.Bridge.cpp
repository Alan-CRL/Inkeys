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
		state.revision = state_.revision + 1;
		state_ = state;
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
			type == CommandType::AutoStraighten || type == CommandType::InputTest)
			return CommandResult::Unsupported;
		if (commands_.size() >= capacity_) return CommandResult::QueueFull;
		commands_.push_back({ type, nextSequence_++ });
		return CommandResult::Accepted;
	}

	bool StateBridge::TryConsume(Command& command) noexcept
	{
		std::scoped_lock lock(mutex_);
		if (commands_.empty()) return false;
		command = commands_.front();
		commands_.pop_front();
		return true;
	}

	void StateBridge::Reset() noexcept
	{
		std::scoped_lock lock(mutex_);
		commands_.clear();
		nextSequence_ = 1;
		state_ = {};
		running_ = true;
	}

	void StateBridge::Stop() noexcept
	{
		std::scoped_lock lock(mutex_);
		running_ = false;
		commands_.clear();
	}

	bool StateBridge::Running() const noexcept
	{
		std::scoped_lock lock(mutex_);
		return running_;
	}
}
