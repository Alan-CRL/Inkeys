#include "Draw3.Bridge.h"

#include <algorithm>

namespace Inkeys::Drawing::Draw3::Bridge
{
	namespace
	{
		bool SamePresentationTargetIgnoringRevision(
			const PresentationTarget& left, const PresentationTarget& right) noexcept
		{
			return left.key == right.key && left.bindingMode == right.bindingMode &&
				left.sourceIdentity == right.sourceIdentity &&
				left.presentationName == right.presentationName &&
				left.provider == right.provider && left.bindingToken == right.bindingToken &&
				left.slideIds == right.slideIds && left.slideId == right.slideId &&
				left.pageIndex == right.pageIndex && left.totalPages == right.totalPages &&
				left.bindingRevision == right.bindingRevision &&
				left.processLocalIdentity == right.processLocalIdentity;
		}
	}

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
		if (state.workspace == Workspace::Presentation && !state.presentationTarget)
			state.presentationTarget = state_.presentationTarget;
		else if (state.workspace != Workspace::Presentation)
			state.presentationTarget.reset();
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
		if (clearPage)
		{
			state_.page = 0;
			state_.hasPage = false;
			state_.presentationTarget.reset();
		}
		++state_.revision;
		return true;
	}

	std::optional<std::uint64_t> StateBridge::PublishPresentationTarget(
		const PresentationTarget& target, bool* changed) noexcept
	{
		if (changed) *changed = false;
		if (target.key.IsZero() || target.sourceIdentity.empty() ||
			target.totalPages == 0 ||
			target.totalPages > kMaximumPresentationPages ||
			target.pageIndex >= target.totalPages)
			return std::nullopt;
		if (target.bindingMode == SlideBindingMode::StableSlideId)
		{
			if (!target.slideId || target.slideIds.size() != target.totalPages ||
				target.slideIds[target.pageIndex] != *target.slideId)
				return std::nullopt;
		}
		else if (target.slideId || !target.slideIds.empty()) return std::nullopt;

		std::scoped_lock lock(mutex_);
		if (!running_) return std::nullopt;
		if (state_.workspace == Workspace::Presentation && state_.presentationTarget)
		{
			if (SamePresentationTargetIgnoringRevision(*state_.presentationTarget, target))
				return state_.presentationTarget->targetRevision;
		}
		if (nextTargetRevision_ == 0) nextTargetRevision_ = 1;
		try
		{
			auto published = std::make_shared<PresentationTarget>(target);
			published->targetRevision = nextTargetRevision_++;
			state_.workspace = Workspace::Presentation;
			state_.page = published->pageIndex;
			state_.hasPage = true;
			state_.presentationTarget = std::move(published);
		}
		catch (...)
		{
			return std::nullopt;
		}
		++state_.revision;
		if (changed) *changed = true;
		return state_.presentationTarget->targetRevision;
	}

	bool StateBridge::ClearPresentationTarget() noexcept
	{
		std::scoped_lock lock(mutex_);
		if (!running_) return false;
		if (state_.workspace == Workspace::Presentation &&
			!state_.presentationTarget && !state_.hasPage) return false;
		state_.workspace = Workspace::Presentation;
		state_.presentationTarget.reset();
		state_.page = 0;
		state_.hasPage = false;
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
			Command command{ type, nextSequence_, state_.workspace };
			// 命令固定发布时的文稿身份，避免稍后的 latest state 改变其作用画布。
			if (state_.workspace == Workspace::Presentation)
				command.presentationTarget = state_.presentationTarget;
			commands_.push_back(std::move(command));
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
			command = std::move(commands_.front());
			commands_.pop_front();
			return true;
		}
		if (!finalCommand_) return false;
		command = std::move(*finalCommand_);
		finalCommand_.reset();
		return true;
	}

	void StateBridge::Reset() noexcept
	{
		std::scoped_lock lock(mutex_);
		commands_.clear();
		finalCommand_.reset();
		nextSequence_ = 1;
		nextTargetRevision_ = 1;
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
		try
		{
			Command command{ finalCommand, nextSequence_, state_.workspace };
			if (state_.workspace == Workspace::Presentation)
				command.presentationTarget = state_.presentationTarget;
			// 固定槽不受业务队列容量影响，并且只在队列排空后消费。
			finalCommand_ = std::move(command);
			++nextSequence_;
			running_ = false;
			return true;
		}
		catch (...)
		{
			running_ = false;
			return false;
		}
	}

	bool StateBridge::Running() const noexcept
	{
		std::scoped_lock lock(mutex_);
		return running_;
	}
}
