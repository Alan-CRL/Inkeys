#pragma once

#include "../Drawing/Draw3/Draw3.Presentation.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

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
