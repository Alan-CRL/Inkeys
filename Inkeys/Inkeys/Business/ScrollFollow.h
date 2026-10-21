#pragma once

#include <cstdint>

namespace Inkeys::Business::ScrollFollow
{
	enum class State : std::uint8_t { Off, Initializing, Tracking, Paused, Stopping };
	enum class EndReason : std::uint8_t { Toggle, ToolIntent, CanvasCommand, Freeze, Workspace, Geometry, Device, Shutdown };
	enum class ActionState : std::uint8_t { Pending, Ready, Retired, Failed };
	struct ActionTicket { std::uint64_t id = 0; };
	struct ActionPreparation { ActionTicket ticket{}; ActionState state = ActionState::Failed; };
	struct Snapshot { State state = State::Off; std::uint64_t session = 0; bool available = false; };
	void RequestToggle() noexcept;
	void RequestStop(EndReason reason = EndReason::Toggle) noexcept;
	Snapshot GetSnapshot() noexcept;
	bool Available() noexcept;
	ActionPreparation PrepareAction(EndReason = EndReason::ToolIntent) noexcept;
	ActionState PollAction(ActionTicket) noexcept;
	void CompleteAction(ActionTicket, std::uint64_t publishedStateRevision = 0) noexcept;
	void CancelAction(ActionTicket) noexcept;
	void RetireScene(EndReason) noexcept;
	void RequestShutdown() noexcept;
	// 致命退场只发布单调标志，不初始化控制线程、不取业务锁。
	void RequestShutdownNoWait() noexcept;
	void SetDiagnostics(bool enabled, bool verbose) noexcept;
}
