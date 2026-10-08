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
	};
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
			&& expected.processId == current.processId;
	}
}
