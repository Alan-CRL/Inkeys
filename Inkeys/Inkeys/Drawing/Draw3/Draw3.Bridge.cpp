#include "Draw3.Bridge.h"
#include "Draw3.Shortcut.h"

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
				left.pageKind == right.pageKind &&
				left.pageIndex == right.pageIndex && left.totalPages == right.totalPages &&
				left.bindingRevision == right.bindingRevision &&
				left.sessionRevision == right.sessionRevision &&
				left.processLocalIdentity == right.processLocalIdentity;
		}
	}

	StateBridge::StateBridge(std::size_t capacity) noexcept
		: capacity_((std::max)(std::size_t{1}, capacity))
	{
	}

	std::uint64_t StateBridge::PublishState(ProductState state) noexcept
	{
		std::scoped_lock lock(mutex_);
		if (!running_) return 0;
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
		const bool sceneChanged = state.workspace != state_.workspace ||
			state.hasPage != state_.hasPage || (state.hasPage && state.page != state_.page) ||
			state.presentationTarget != state_.presentationTarget;
		state.shortcutIntentEpoch = state_.shortcutIntentEpoch + (sceneChanged ? 1 : 0);
		state.revision = state_.revision + 1;
		state_ = state;
		return state_.revision;
	}

	bool StateBridge::PublishWorkspace(Workspace workspace) noexcept
	{
		std::scoped_lock lock(mutex_);
		if (!running_) return false;
		const bool clearPage = workspace != Workspace::Presentation;
		if (state_.workspace == workspace && (!clearPage || !state_.hasPage))
			return false;
		++state_.shortcutIntentEpoch;
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
			!ValidPresentationPage(target)) return std::nullopt;

		std::scoped_lock lock(mutex_);
		if (!running_) return std::nullopt;
		if (state_.workspace == Workspace::Presentation && state_.presentationTarget)
		{
			if (targetRefreshRequired_ != ReadyIdentityFor(*state_.presentationTarget) &&
				SamePresentationTargetIgnoringRevision(*state_.presentationTarget, target))
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
			targetRefreshRequired_.reset();
		}
		catch (...)
		{
			return std::nullopt;
		}
		++state_.shortcutIntentEpoch;
		++state_.revision;
		if (changed) *changed = true;
		return state_.presentationTarget->targetRevision;
	}

	bool StateBridge::RequirePresentationTargetRefresh(const PresentationReadyIdentity& expected) noexcept
	{
		std::scoped_lock lock(mutex_);
		if (!running_ || state_.workspace != Workspace::Presentation || !state_.presentationTarget ||
			ReadyIdentityFor(*state_.presentationTarget) != expected) return false;
		targetRefreshRequired_ = expected;
		return true;
	}

	bool StateBridge::ClearPresentationTarget() noexcept
	{
		std::scoped_lock lock(mutex_);
		if (!running_) return false;
		if (state_.workspace == Workspace::Presentation &&
			!state_.presentationTarget && !state_.hasPage) return false;
		++state_.shortcutIntentEpoch;
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

	CommandResult StateBridge::Publish(CommandType type, std::uint64_t followSession) noexcept
	{
		std::scoped_lock lock(mutex_);
		if (!running_) return CommandResult::NotRunning;
		if (type == CommandType::FollowEnd || (type == CommandType::FollowBegin && !followSession)) return CommandResult::Unsupported;
		// 尚未准备好的能力保留显式空接口，产品入口可据此隐藏。
		if (type == CommandType::Save || type == CommandType::SuperRecovery ||
			type == CommandType::AutoStraighten || type == CommandType::InputTest ||
			type == CommandType::PrepareExitAutoSave || type == CommandType::ShortcutFence)
			return CommandResult::Unsupported;
		if (commands_.size() >= capacity_) return CommandResult::QueueFull;
		try
		{
			Command command{ type, nextSequence_, state_.workspace };
			command.followSession = followSession;
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

	CommandResult StateBridge::PublishWhiteboardTheme(WhiteboardTheme theme,
		std::uint64_t generation, std::size_t page) noexcept
	{
		std::scoped_lock lock(mutex_);
		if (!running_) return CommandResult::NotRunning;
		if (state_.workspace != Workspace::Whiteboard || !ValidWhiteboardTheme(theme))
			return CommandResult::Unsupported;
		if (commands_.size() >= capacity_) return CommandResult::QueueFull;
		try
		{
			Command command{ CommandType::SetWhiteboardTheme, nextSequence_, state_.workspace };
			command.whiteboardTheme = theme;
			command.whiteboardGeneration = generation;
			command.whiteboardPage = page;
			commands_.push_back(std::move(command));
			++nextSequence_;
			return CommandResult::Accepted;
		}
		catch (...) { return CommandResult::QueueFull; }
	}

	CommandResult StateBridge::PublishShortcut(CommandType type,
		std::shared_ptr<const Shortcuts::Request> request) noexcept
	{
		std::scoped_lock lock(mutex_);
		if (!running_) return CommandResult::NotRunning;
		if (!request || (type != CommandType::Clear && type != CommandType::Undo &&
			type != CommandType::Redo && type != CommandType::ShortcutFence)) return CommandResult::Unsupported;
		const auto presentation = state_.presentationTarget
			? std::optional(ReadyIdentityFor(*state_.presentationTarget)) : std::nullopt;
		// 与入队共锁核验触发时身份，绝不把旧快捷键重绑定到最新页面。
		if (request->scope.intentEpoch != state_.shortcutIntentEpoch ||
			request->scope.workspace != state_.workspace ||
			request->scope.presentation != presentation) return CommandResult::NotRunning;
		if (commands_.size() >= capacity_) return CommandResult::QueueFull;
		try
		{
			Command command{ type, nextSequence_, state_.workspace };
			command.presentationTarget = state_.presentationTarget;
			command.shortcut = std::move(request);
			commands_.push_back(std::move(command));
			++nextSequence_;
		}
		catch (...) { return CommandResult::QueueFull; }
		return CommandResult::Accepted;
	}

	bool StateBridge::TryConsume(Command& command) noexcept
	{
		std::scoped_lock lock(mutex_);
		if (followEnd_ && (commands_.empty() || followEnd_->sequence < commands_.front().sequence))
		{
			command = std::move(*followEnd_); followEnd_.reset(); return true;
		}
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

	bool StateBridge::PublishFollowEnd(std::uint64_t session) noexcept
	{
		std::scoped_lock lock(mutex_);
		if (!running_ || followEnd_) return false;
		followEnd_.emplace(); followEnd_->type = CommandType::FollowEnd;
		followEnd_->sequence = nextSequence_++; followEnd_->followSession = session;
		return true;
	}

	void StateBridge::Reset() noexcept
	{
		std::scoped_lock lock(mutex_);
		commands_.clear();
		followEnd_.reset();
		finalCommand_.reset();
		nextSequence_ = 1;
		// Host 重启也不复用目标编号，避免旧 UI/ready completion 命中新一代。
		state_ = {};
		targetRefreshRequired_.reset();
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
