#pragma once

#include "../Drawing/Draw3/Draw3.Presentation.h"

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

	struct PptSessionToken
	{
		std::uint64_t localSession = 0;
		std::uint64_t serviceGeneration = 0;
		std::uint64_t showSessionRevision = 0;
		std::uint64_t bindingRevision = 0;
		std::uintptr_t showWindow = 0;
		std::uint32_t processId = 0;
		bool active = false;
		bool guardedExitAvailable = false;
		std::uint64_t processStartTime = 0;
	};
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
	// 业务请求捕获时和真正执行时使用相同会话，名称或页码相同不能替代身份。
	[[nodiscard]] constexpr bool MatchesPptSession(
		const PptSessionToken& expected, const PptSessionToken& current) noexcept
	{
		return expected.active && current.active && expected.localSession != 0
			&& expected.localSession == current.localSession
			&& expected.serviceGeneration == current.serviceGeneration
			&& expected.showSessionRevision == current.showSessionRevision
			&& expected.bindingRevision == current.bindingRevision
			&& expected.showWindow == current.showWindow
			&& expected.processId == current.processId
			&& expected.processStartTime == current.processStartTime;
	}
}
