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

#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
#include <memory>
namespace Inkeys::CV { struct MotionFrame; struct MotionResult; }
namespace Inkeys::Business::ScrollFollow::LiveTrace
{
	// 仅显式实屏harness只读观察；不连接TestPorts，不提供Target/场景写入口。
	enum class Operation : std::uint8_t { Initialize, Process, AwaitTracking };
	struct Identity
	{
		std::uint64_t session = 0, captureSession = 0, displayGeneration = 0;
		int sourceLeft = 0, sourceTop = 0, sourceWidth = 0, sourceHeight = 0, roiLeft = 0, roiTop = 0;
		int sourceRight = 0, sourceBottom = 0, workAreaLeft = 0, workAreaTop = 0, workAreaRight = 0, workAreaBottom = 0;
		double pixelsPerPixelX = 1, pixelsPerPixelY = 1;
	};
	struct Observer
	{
		virtual ~Observer() = default;
		virtual void Observe(const Identity&, Operation, const CV::MotionFrame&, const CV::MotionResult&) noexcept = 0;
	};
	void Install(std::shared_ptr<Observer>) noexcept;
}
#endif
