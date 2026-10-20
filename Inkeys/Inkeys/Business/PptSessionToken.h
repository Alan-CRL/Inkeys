#pragma once

#include <cstdint>

namespace Inkeys::Business
{
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
	// 捕获与执行保持同一身份，禁止将旧输入重新归属到新放映。
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
	struct PptFocusForeground
	{
		std::uintptr_t window = 0;
		std::uint32_t processId = 0;
	};
	struct PptFocusRequest
	{
		PptSessionToken session;
		PptFocusForeground foreground;
	};
	[[nodiscard]] PptFocusRequest CapturePptFocusRequest() noexcept;
	void QueuePptFocusRequest(const PptFocusRequest& request) noexcept;
	[[nodiscard]] PptSessionToken CapturePptInputSession() noexcept;
	[[nodiscard]] bool IsCurrentPptInputSession(const PptSessionToken& token) noexcept;
	// 仅生成 PPT 业务命令，不经通用工作区回调回落到白板。
	void QueuePptInputDirection(const PptSessionToken& token,
		bool next, int currentPage, int totalPage) noexcept;

	// 在业务队列 mutex 内使用；旧完成不能释放新场次的 pending/in-flight。
	class PptEndShowGate
	{
		PptSessionToken outstanding;
	public:
		[[nodiscard]] bool TryBegin(const PptSessionToken& token) noexcept
		{
			if (!token.active || !token.localSession || MatchesPptSession(token, outstanding)) return false;
			outstanding = token;
			return true;
		}
		void Complete(const PptSessionToken& token) noexcept
		{
			if (MatchesPptSession(token, outstanding)) outstanding = {};
		}
		void Reset() noexcept { outstanding = {}; }
	};
}
