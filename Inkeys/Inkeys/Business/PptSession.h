#pragma once

#include "../Drawing/Draw3/Draw3.Presentation.h"
#include "PptSessionToken.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Inkeys::Business
{
	enum class PptLifecycle { Inactive, Active, Unknown };
	enum class PptPageStatus { Valid, EndScreen, Unknown };
	struct PptSessionSnapshot
	{
		std::uint64_t stateRevision = 0;
		std::uint64_t showSessionRevision = 0;
		std::uint64_t bindingRevision = 0;
		PptLifecycle lifecycle = PptLifecycle::Unknown;
		PptPageStatus pageStatus = PptPageStatus::Unknown;
		Drawing::Draw3::PresentationDescriptor descriptor;
	};
	struct PptSessionParseResult
	{
		std::optional<PptSessionSnapshot> snapshot;
		std::string error;
	};
	[[nodiscard]] PptSessionParseResult ParsePptSessionSnapshot(
		std::wstring_view json) noexcept;
	struct PptControlPage
	{
		int currentPage = -1;
		int totalPage = -1;
		bool known = false;
	};
	[[nodiscard]] constexpr bool CanStartPptControlSession(
		const PptSessionSnapshot& state) noexcept
	{
		return state.lifecycle == PptLifecycle::Active
			&& state.showSessionRevision != 0 && state.bindingRevision != 0;
	}
	[[nodiscard]] PptControlPage ResolvePptControlPage(
		const PptSessionSnapshot& state, int rawPage, int rawTotal) noexcept;

	// 描述符已观察不等于目标已应用；raw 暂时落后时保留待解析状态。
	class PptTargetCache
	{
	public:
		std::optional<Drawing::Draw3::Bridge::PresentationTarget> target;

		bool Update(bool descriptorChanged, const Drawing::Draw3::PresentationDescriptor* descriptor,
			bool trustedPage, bool trustedEndScreen, std::uint64_t session) noexcept
		{
			pending_ = pending_ || descriptorChanged || !target
				|| (target && target->sessionRevision != session);
			if (!pending_ || !descriptor || session == 0 || (!trustedPage && !trustedEndScreen))
				return false;
			auto resolved = trustedEndScreen
				? Drawing::Draw3::ResolveEndScreenTarget(*descriptor)
				: Drawing::Draw3::ResolvePresentationTarget(*descriptor);
			if (!resolved) return false;
			resolved->sessionRevision = session;
			target = std::move(resolved);
			pending_ = false;
			return true;
		}

		[[nodiscard]] bool Current() const noexcept { return target.has_value() && !pending_; }
	private:
		bool pending_ = true;
	};

	[[nodiscard]] inline bool MatchesPptPageUi(
		const Drawing::Draw3::Bridge::PresentationTarget& target,
		std::uint64_t session, std::uint64_t revision, int page, int total) noexcept
	{
		return target.sessionRevision == session && target.targetRevision == revision
			&& total > 0 && target.totalPages == static_cast<std::uint32_t>(total)
			&& (target.pageKind == Drawing::Draw3::Bridge::PresentationPageKind::EndScreen
				? page == -1 && target.pageIndex == target.totalPages
				: page > 0 && target.pageIndex == static_cast<std::uint32_t>(page - 1));
	}

	[[nodiscard]] constexpr bool MatchesPptShowSession(const PptSessionToken& session,
		std::uint64_t serviceGeneration, const PptSessionSnapshot& state) noexcept
	{
		return session.active && session.serviceGeneration == serviceGeneration
			&& session.showSessionRevision == state.showSessionRevision
			&& session.bindingRevision == state.bindingRevision;
	}
	[[nodiscard]] constexpr bool IsPptWindowConfirmedEnded(const PptSessionToken& session,
		bool exists, std::uint32_t processId, std::uint64_t processStartTime = 0) noexcept
	{
		// HWND 未知时只能依赖 COM 场次；查询失败也不能被推断成退出。
		return session.active && session.showWindow != 0 && (!exists
			|| (processId != 0 && processId != session.processId)
			|| (session.processStartTime != 0 && processStartTime != 0
				&& session.processStartTime != processStartTime));
	}

	[[nodiscard]] constexpr bool IsFirstPptWindowAcquisition(const PptSessionToken& previous,
		std::uintptr_t acquiredWindow) noexcept
	{
		// 已知锚点因 COM 暂时失效而恢复只刷新窗口，不能把后台轮询当作进入放映。
		return previous.active && previous.showWindow == 0 && acquiredWindow != 0;
	}
	[[nodiscard]] constexpr bool MatchesPptFocusForeground(const PptFocusRequest& request,
		const PptFocusForeground& foreground) noexcept
	{
		return (request.session.showWindow != 0 && foreground.window == request.session.showWindow)
			|| (request.foreground.window != 0 && request.foreground.processId != 0
				&& foreground.window == request.foreground.window
				&& foreground.processId == request.foreground.processId);
	}
	struct PptFocusState
	{
		PptSessionToken session;
		PptFocusForeground foreground;
		bool controlAvailable = false;
		bool workspaceAllowed = false;
		bool modalActive = false;
		bool targetExists = false;
		bool targetVisible = false;
		std::uint32_t targetProcessId = 0;
		std::uint64_t targetProcessStartTime = 0;
	};
	[[nodiscard]] constexpr bool CanFocusPptSession(const PptFocusRequest& request,
		const PptFocusState& state) noexcept
	{
		// 无激活控件可从原外部前台显式交接；排队后切走、旧场次及模态期间均拒绝。
		return MatchesPptSession(request.session, state.session)
			&& state.controlAvailable && state.workspaceAllowed && !state.modalActive
			&& request.session.showWindow != 0 && request.session.processId != 0
			&& state.targetExists && state.targetVisible
			&& state.targetProcessId == request.session.processId
			&& !IsPptWindowConfirmedEnded(request.session, state.targetExists,
				state.targetProcessId, state.targetProcessStartTime)
			&& MatchesPptFocusForeground(request, state.foreground);
	}
	class PptFocusHandoff
	{
		std::optional<PptFocusRequest> pending;
	public:
		void Begin(const PptFocusRequest& request) noexcept
		{
			pending = request.session.active && request.session.showWindow != 0
				? std::optional<PptFocusRequest>(request) : std::nullopt;
		}
		void Reset() noexcept { pending.reset(); }
		void RebaseAfterWindowTransition(const PptFocusForeground& before,
			const PptFocusForeground& after, bool allowForegroundFallback = false) noexcept
		{
			if (!pending) return;
			const bool unchanged = before.window == after.window && before.processId == after.processId;
			// 仅本段实际隐藏的白板前台窗口可回退；外部 A→B 不能被窗口事务重新授权。
			if (!MatchesPptFocusForeground(*pending, before) || (!unchanged && !allowForegroundFallback))
				pending.reset();
			else pending->foreground = after;
		}
		[[nodiscard]] std::optional<PptFocusRequest> Take() noexcept
		{
			auto result = pending;
			pending.reset();
			return result;
		}
	};
}
