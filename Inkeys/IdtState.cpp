#include "IdtState.h"

#include "IdtConfiguration.h"
#include "IdtDraw.h"
#include "IdtFreezeFrame.h"
#include "IdtPlug-in.h"
#include "Inkeys/Business/LegacyDrawState.hpp"
#include "Inkeys/Business/PenToolState.hpp"
#include "Inkeys/Business/PptSession.h"
#include "Inkeys/Drawing/Draw3/Draw3.Product.h"
#include "Inkeys/Drawing/Draw3/Draw3.PresentationState.h"
#include "Inkeys/Window/Window.Legacy.hpp"
#include "Inkeys/UI/Bar/Bar.A2.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <limits>
#include <optional>
#include <thread>

import Inkeys.Other.Config;
import Inkeys.UI.Bar;
import Inkeys.UI.Setting;
import Inkeys.UI.Ppt;
import Inkeys.UI.Whiteboard;
import Inkeys.UI.Freeze;
import Inkeys.Window;

StateModeClass stateMode;

namespace
{
	using Inkeys::Drawing::Draw3::Bridge::ProductState;
	using Inkeys::Drawing::Draw3::Bridge::Tool;
	using Inkeys::Drawing::Draw3::Bridge::Workspace;
	std::mutex draw3PresentationMutex;
	std::mutex stateModeTransitionMutex;
	Workspace stateModeWorkspace = Workspace::Desktop;
	std::array<std::optional<StateModeClass>, 3> workspaceStateModes;
	std::atomic_uint64_t stateModeTransitionRevision = 0;
	std::atomic_uint64_t draw3ExitSelectionModeRevision = 0;
	std::mutex eraserPreferencesMutex;
	Inkeys::Drawing::Draw3::SpeedEraser::InputSettings ReadEraserPreferences()
	{
		using namespace Inkeys::Drawing::Draw3::SpeedEraser;
		std::scoped_lock lock(eraserPreferencesMutex);
		auto& saved=Inkeys::config.Drawing.Eraser;
		const std::array fields{&saved.MouseLeft,&saved.MouseRight,&saved.Touch,&saved.PenTip,&saved.PenTail};
		InputSettings result;result.automaticEnabled=saved.Automatic.load();
		result.automaticPenSupported=AutomaticPenResponseAvailable();
		result.baseSize=RestoreBaseSize(saved.BaseDiameterDip.load());
		result.sensitivity=RestoreSensitivity(saved.Sensitivity.load());
		for(size_t i=0;i<fields.size();++i)
			result.entries[i].kind=RestoreEraserKind(fields[i]->load(),-1,static_cast<InputEntry>(i));
		result.entries[3].penResponse=RestorePenResponse(saved.PenTipResponse.load(),result.automaticPenSupported);
		result.entries[4].penResponse=RestorePenResponse(saved.PenTailResponse.load(),result.automaticPenSupported);
		return result;
	}
	bool ExitSelectionHandoffPending() noexcept
	{
		const auto revision = draw3ExitSelectionModeRevision.load(
			std::memory_order_acquire);
		return revision != 0 && revision == stateModeTransitionRevision.load(
			std::memory_order_acquire);
	}
	std::atomic_bool draw3PresentationRetryPending = false;
	bool draw3PresentationFailureActive = false;
	bool draw3SelectionWaitActive = false;
	bool draw3PresentationExitRefreshPending = false;
	enum class Draw3PresentationGateWaitReason : std::uint8_t
	{
		None,
		BridgeModeMismatch,
		SelectionHandoff,
		FirstFrame,
		SelectionOutput,
	};
	Draw3PresentationGateWaitReason draw3PresentationGateWaitReason =
		Draw3PresentationGateWaitReason::None;
	// 低位保存期望 owner 状态，高位版本保证旧请求成功也不能覆盖更新后的期望。
	std::atomic_uint64_t settingOwnerDesiredState = 0;
	std::atomic_uint64_t settingOwnerAppliedState = 0;
	std::mutex settingOwnerApplyMutex;

	std::uint64_t PublishSettingOwnerDesiredState(bool enabled) noexcept
	{
		auto current = settingOwnerDesiredState.load(std::memory_order_relaxed);
		std::uint64_t next = 0;
		do
		{
			next = ((current + 2) & ~std::uint64_t{ 1 }) |
				(enabled ? std::uint64_t{ 1 } : std::uint64_t{ 0 });
		} while (!settingOwnerDesiredState.compare_exchange_weak(
			current, next, std::memory_order_release, std::memory_order_relaxed));
		return next;
	}

	void ApplySettingOwnerDesiredState(std::uint64_t desiredState)
	{
		std::scoped_lock lock(settingOwnerApplyMutex);
		// 等锁期间可能已经发布更新版本；过期请求不得再覆盖当前 owner。
		if (settingOwnerDesiredState.load(std::memory_order_acquire) != desiredState)
			return;
		const bool enabled = (desiredState & std::uint64_t{ 1 }) != 0;
		if (Inkeys::Window::GetService().SetSettingOwnedByDrawpad(enabled))
			// 写回实际完成的版本；若调用期间期望已更新，版本不等会继续触发重试。
			settingOwnerAppliedState.store(desiredState, std::memory_order_release);
	}

	void ReconcileSettingOwnerDesiredState()
	{
		const auto desiredState = settingOwnerDesiredState.load(
			std::memory_order_acquire);
		if (settingOwnerAppliedState.load(std::memory_order_acquire) != desiredState)
			ApplySettingOwnerDesiredState(desiredState);
	}

	Workspace CurrentPrimaryWorkspace() noexcept
	{
		return PptSessionActive()
			? Workspace::Presentation : Workspace::Desktop;
	}

	enum class Draw3PresentationReconcileResult : std::uint8_t
	{
		Waiting,
		Applied,
		Retry,
	};

	enum class WhiteboardPhase : std::uint8_t
	{
		Inactive,
		Entering,
		Active,
		Exiting,
	};


	std::atomic_bool whiteboardDesired = false;
	// 偶数属于可返回的 PPT 意图；奇数先于 desired 发布，封住进入白板时的并发准入缝隙。
	std::atomic_uint64_t whiteboardInputEpoch = 2;
	std::atomic_bool whiteboardPreviousRequested = false;
	std::atomic_bool whiteboardNextRequested = false;
	std::atomic<WhiteboardPhase> whiteboardPhase = WhiteboardPhase::Inactive;
	// 只有本协调快照决定稳定工作区；旧 phase/desired 是对外兼容投影。
	struct WorkspaceHandoff
	{
		using Clock = std::chrono::steady_clock;
		Workspace desired = Workspace::Desktop;
		Workspace committed = Workspace::Desktop;
		std::uint64_t revision = 2;
		std::uint64_t hostGeneration = 0;
		bool pending = false;
		bool minimized = false;
		bool committedMinimized = false;
		bool whiteboardInvolved = false;
		unsigned failures = 0;
		Clock::time_point retryAt{};
		bool failed = false;
		bool activate = false;

		bool Request(Workspace target, bool viewMinimized = false, bool force = false, bool allowActivation = true) noexcept
		{
			viewMinimized = target == Workspace::Whiteboard && viewMinimized;
			if (!force && desired == target && minimized == viewMinimized) return false;
			whiteboardInvolved = whiteboardInvolved || desired == Workspace::Whiteboard ||
				committed == Workspace::Whiteboard || target == Workspace::Whiteboard;
			activate = allowActivation && target == Workspace::Whiteboard && !viewMinimized &&
				((pending && activate && desired == target) || desired != Workspace::Whiteboard || minimized);
			desired = target;
			minimized = viewMinimized;
			revision = ((revision + 2) & ~std::uint64_t{ 1 }) |
				(target == Workspace::Whiteboard ? 1U : 0U);
			hostGeneration = 0;
			pending = true; // 取消回到 committed 也仍欠旧物理操作的退休。
			failures = 0;
			retryAt = {};
			failed = false;
			return true;
		}
		bool Commit(std::uint64_t expected, bool targetReady, bool backgroundReady, bool nativeReady) noexcept
		{
			if (!pending || failed || revision != expected || !targetReady || !backgroundReady || !nativeReady) return false;
			committed = desired;
			committedMinimized = minimized;
			whiteboardInvolved = committed == Workspace::Whiteboard;
			pending = false;
			return true;
		}
		bool Fail(std::uint64_t expected, Clock::time_point now) noexcept
		{
			if (!pending || revision != expected || failed) return false;
			if (++failures >= 4) { failed = true; return true; }
			retryAt = now + std::chrono::milliseconds(250U << (failures - 1));
			return false;
		}
		bool Ready(Clock::time_point now) const noexcept { return pending && !failed && now >= retryAt; }
		bool Retry(std::uint64_t expected) noexcept
		{
			if (!pending || !failed || revision != expected) return false;
			// 显式重试是新请求；退休 fence 的旧版本不能再次打开。
			return Request(desired, minimized, true);
		}
	};
	[[nodiscard]] bool ShouldRestoreWhiteboardSourcePreview(const WorkspaceHandoff& request,
		Inkeys::Window::CommandPhase phase) noexcept
	{
		return phase == Inkeys::Window::CommandPhase::Rejected &&
			request.committed == Workspace::Whiteboard && !request.committedMinimized;
	}
	enum class WorkspaceWaitStage : std::uint8_t
	{
		None,
		BackgroundInactive,
		TargetReady,
		BackgroundPrepared,
		FinalBackgroundActive,
	};
	struct WorkspaceWaitTracker
	{
		using Clock = std::chrono::steady_clock;
		Clock::time_point checkedAt{};
		WorkspaceWaitStage stage = WorkspaceWaitStage::None;
		std::uint64_t outputRevision = 0;
		std::uint64_t contentRevision = 0;
		std::optional<Inkeys::Drawing::Draw3::Bridge::PresentationReadyIdentity> presentation;
		std::uint64_t backgroundRevision = 0;
		bool backgroundReady = false;

		[[nodiscard]] bool Stalled(WorkspaceWaitStage next,
			const Inkeys::Drawing::Draw3::HostRuntimeSnapshot& runtime,
			std::uint64_t nextBackgroundRevision, bool nextBackgroundReady,
			Clock::time_point now = Clock::now()) noexcept
		{
			const bool changed = stage != next || outputRevision != runtime.readyOutputRevision ||
				contentRevision != runtime.presentedContentRevision || presentation != runtime.presentationUiReady ||
				backgroundRevision != nextBackgroundRevision || backgroundReady != nextBackgroundReady;
			if (changed)
			{
				stage = next;
				outputRevision = runtime.readyOutputRevision;
				contentRevision = runtime.presentedContentRevision;
				presentation = runtime.presentationUiReady;
				backgroundRevision = nextBackgroundRevision;
				backgroundReady = nextBackgroundReady;
				checkedAt = now;
				return false;
			}
			// 异步完成可在一拍内多次唤醒；通知次数不能缩短原有 250ms 无进展预算。
			if (now < checkedAt + std::chrono::milliseconds(250)) return false;
			checkedAt = now;
			return true;
		}
	};
	[[nodiscard]] bool WorkspaceNativePending(Inkeys::Window::CommandPhase phase) noexcept
	{
		return phase == Inkeys::Window::CommandPhase::Queued || phase == Inkeys::Window::CommandPhase::Executing;
	}
	[[nodiscard]] bool CancelWorkspaceNativeAndRetired(const Inkeys::Window::WorkspaceWindowReceipt& receipt)
	{
		receipt.Cancel();
		return !WorkspaceNativePending(receipt.Phase());
	}
	struct WorkspaceNativeOperation
	{
		Inkeys::Window::WorkspaceWindowReceipt receipt;
		Inkeys::Window::WorkspaceWindowPolicy policy;
		std::function<bool()> stillDesired;
		bool completing = false;
		Inkeys::Business::PptFocusForeground focusBefore;
		bool focusMayFallBack = false;
		WorkspaceHandoff::Clock::time_point submittedAt = WorkspaceHandoff::Clock::now();
		bool timeoutNoted = false;

		[[nodiscard]] bool CanConsume(bool complete) const
		{ return completing == complete && stillDesired && stillDesired() && receipt.Current(); }
		bool NoteExecutingTimeout(WorkspaceHandoff::Clock::time_point now = WorkspaceHandoff::Clock::now())
		{
			if (timeoutNoted || !WorkspaceNativePending(receipt.Phase())
				|| now < submittedAt + std::chrono::milliseconds(1500)) return false;
			timeoutNoted = true;
			receipt.Cancel();
			// 与原同步期限一致：排队过期不计 API 故障，在途超时只计一次且继续保留恢复责任。
			return receipt.Phase() == Inkeys::Window::CommandPhase::Executing;
		}
	};
	std::mutex workspaceHandoffMutex;
	WorkspaceHandoff workspaceHandoff;
	std::atomic_bool whiteboardViewChanged = false;
	enum class NativeWhiteboardRequest : std::uint64_t { Restore = 1, Minimize = 2, Close = 3 };
	std::atomic_uint64_t whiteboardNativeRequest = 0;
	std::atomic_uint64_t whiteboardNativeViewProjection = 2U << 1;
	std::optional<Inkeys::Business::PptSessionToken> basicControlSession;

	WorkspaceHandoff ReadWorkspaceHandoff() noexcept
	{
		std::scoped_lock lock(workspaceHandoffMutex);
		return workspaceHandoff;
	}
	void ProjectWorkspaceHandoffLocked() noexcept
	{
		whiteboardInputEpoch.store(workspaceHandoff.revision, std::memory_order_release);
		whiteboardDesired.store(workspaceHandoff.desired == Workspace::Whiteboard, std::memory_order_release);
		const auto phase = workspaceHandoff.pending
			? (workspaceHandoff.desired == Workspace::Whiteboard ? WhiteboardPhase::Entering
				: workspaceHandoff.whiteboardInvolved ? WhiteboardPhase::Exiting : WhiteboardPhase::Inactive)
			: workspaceHandoff.committed == Workspace::Whiteboard ? WhiteboardPhase::Active : WhiteboardPhase::Inactive;
		whiteboardPhase.store(phase, std::memory_order_release);
		whiteboardNativeViewProjection.store((workspaceHandoff.revision << 1) | (workspaceHandoff.minimized ? 1U : 0U), std::memory_order_release);
	}
	WorkspaceHandoff RequestWorkspace(Workspace target, bool minimized = false, bool force = false, bool allowActivation = true) noexcept
	{
		std::scoped_lock lock(workspaceHandoffMutex);
		workspaceHandoff.Request(target, minimized, force, allowActivation);
		ProjectWorkspaceHandoffLocked();
		return workspaceHandoff;
	}
	std::optional<WorkspaceHandoff> RequestWorkspaceIfRevision(Workspace target, bool minimized,
		bool force, std::uint64_t expectedRevision) noexcept
	{
		std::scoped_lock lock(workspaceHandoffMutex);
		if (workspaceHandoff.revision != expectedRevision) return std::nullopt;
		workspaceHandoff.Request(target, minimized, force);
		ProjectWorkspaceHandoffLocked();
		return workspaceHandoff;
	}
	bool WorkspaceRequestCurrent(std::uint64_t revision) noexcept
	{
		const auto queued = whiteboardNativeRequest.load(std::memory_order_acquire);
		return whiteboardInputEpoch.load(std::memory_order_acquire) == revision && (!queued || (queued >> 2) != revision);
	}
	void RetireNativeWhiteboardRequest(std::uint64_t captured) noexcept
	{
		if (captured) (void)whiteboardNativeRequest.compare_exchange_strong(captured, 0,
			std::memory_order_acq_rel, std::memory_order_acquire);
	}
	void QueueNativeWhiteboardRequest(NativeWhiteboardRequest action) noexcept
	{
		const auto projected = whiteboardNativeViewProjection.load(std::memory_order_acquire);
		const auto epoch = projected >> 1;
		if (!(epoch & 1U) || whiteboardInputEpoch.load(std::memory_order_acquire) != epoch) return;
		auto queued = whiteboardNativeRequest.load(std::memory_order_acquire);
		for (;;)
		{
			const auto desiredAction = (projected & 1U) ? NativeWhiteboardRequest::Minimize : NativeWhiteboardRequest::Restore;
			const bool oppositeQueued = queued && (queued >> 2) == epoch &&
				static_cast<NativeWhiteboardRequest>(queued & 3U) != action;
			// 相同视图只在撤销尚未消费的相反意图时排队；普通重复 SC 保持幂等。
			if (action != NativeWhiteboardRequest::Close && action == desiredAction && !oppositeQueued) return;
			if (whiteboardNativeViewProjection.load(std::memory_order_acquire) != projected ||
				whiteboardInputEpoch.load(std::memory_order_acquire) != epoch) return;
			const auto next = (epoch << 2) | static_cast<std::uint64_t>(action);
			if (queued == next) return;
			if (whiteboardNativeRequest.compare_exchange_weak(queued, next,
				std::memory_order_acq_rel, std::memory_order_acquire))
			{ Inkeys::Drawing::Draw3::NotifyProductWorkspaceProgress(); return; }
		}
	}
	bool CloseWorkspaceAdmission(const WorkspaceHandoff& request) noexcept
	{
		if (!request.pending) return true;
		const auto hostGeneration = Inkeys::Drawing::Draw3::ProductRuntimeSnapshot().hostGeneration;
		// 绝不持协调锁进入 Product；旧 run 的同数字 epoch 也没有新 Host 的开关权限。
		if (!Inkeys::Drawing::Draw3::SetProductWorkspaceTransitionPending(request.revision, true, hostGeneration)) return false;
		std::scoped_lock lock(workspaceHandoffMutex);
		if (workspaceHandoff.revision != request.revision || !workspaceHandoff.pending) return false;
		workspaceHandoff.hostGeneration = hostGeneration;
		return true;
	}

	template<class PublishNotice>
	void FailWorkspaceRequestImpl(std::uint64_t revision, PublishNotice&& publish)
	{
		bool firstFailure = false;
		bool whiteboard = false, involved = false;
		{
			std::scoped_lock lock(workspaceHandoffMutex);
			firstFailure = workspaceHandoff.Fail(revision, WorkspaceHandoff::Clock::now());
			whiteboard = workspaceHandoff.desired == Workspace::Whiteboard;
			involved = workspaceHandoff.whiteboardInvolved;
		}
		// 通知仅当前故障边沿一次，且不能持协调锁进入既有 UI FIFO。
		if (firstFailure) publish(revision, whiteboard, involved);
	}
	void FailWorkspaceRequest(std::uint64_t revision) noexcept
	{
		try
		{
			FailWorkspaceRequestImpl(revision, [](auto token, bool whiteboard, bool involved)
			{ Inkeys::UI::Setting::QueueWorkspaceTransitionFailureNotice(token, whiteboard, involved); });
		}
		catch (...) { if (IDTLogger) IDTLogger->warn("[Workspace] failure notice queue rejected"); }
	}

	std::mutex whiteboardReturnFocusMutex;
	Inkeys::Business::PptFocusHandoff whiteboardReturnFocus;

	WorkspaceHandoff ConsumeNativeWhiteboardRequests()
	{
		auto request = ReadWorkspaceHandoff();
		auto captured = whiteboardNativeRequest.load(std::memory_order_acquire);
		const auto sourceRevision = request.revision;
		if (!captured) return request;
		if ((captured >> 2) != sourceRevision || request.desired != Workspace::Whiteboard)
		{
			RetireNativeWhiteboardRequest(captured);
			return request;
		}
		for (;;)
		{
			const auto action = static_cast<NativeWhiteboardRequest>(captured & 3U);
			const bool close = action == NativeWhiteboardRequest::Close;
			const bool minimize = action == NativeWhiteboardRequest::Minimize;
			const bool allowed = !minimize || !PptSessionActive();
			const auto target = close ? CurrentPrimaryWorkspace() : Workspace::Whiteboard;
			const bool targetMinimized = close ? false : allowed ? minimize : request.minimized;
			const auto next = RequestWorkspaceIfRevision(target, targetMinimized,
				target == request.desired && targetMinimized == request.minimized, request.revision);
			if (!next) return ReadWorkspaceHandoff(); // 并发 public 请求已胜出，旧批不能覆盖它。
			request = *next;
			if (close)
			{
				std::scoped_lock lock(whiteboardReturnFocusMutex);
				whiteboardReturnFocus.Begin(Inkeys::Business::CapturePptFocusRequest());
			}
			auto expected = captured;
			if (whiteboardNativeRequest.compare_exchange_strong(expected, 0,
				std::memory_order_acq_rel, std::memory_order_acquire)) return request;
			// 接受旧包期间到达的同来源新意图仍属本批；在任何窗口写入前合并它。
			// 不清新来源包，它由下一轮按自己的当前 epoch 消费。
			if (!expected || (expected >> 2) != sourceRevision) return ReadWorkspaceHandoff();
			captured = expected;
		}
	}

	bool ForegroundMatchesWindowRole(const Inkeys::Business::PptFocusForeground& foreground,
		Inkeys::Window::WindowRole role)
	{
		return foreground.window != 0 && foreground.processId == GetCurrentProcessId()
			&& foreground.window == reinterpret_cast<std::uintptr_t>(
				Inkeys::Window::GetService().Handle(role));
	}
	void RebaseWhiteboardReturnFocus(const Inkeys::Business::PptFocusForeground& before,
		bool allowForegroundFallback)
	{
		const auto after = Inkeys::Business::CapturePptFocusRequest().foreground;
		std::scoped_lock lock(whiteboardReturnFocusMutex);
		whiteboardReturnFocus.RebaseAfterWindowTransition(before, after, allowForegroundFallback);
	}

	template<class Queue>
	bool CompleteWhiteboardReturnFocus(Queue&& queue)
	{
		if (WhiteboardTransactionActive()) return false;
		std::optional<Inkeys::Business::PptFocusRequest> request;
		{
			std::scoped_lock lock(whiteboardReturnFocusMutex);
			request = whiteboardReturnFocus.Take();
		}
		if (!request) return false;
		queue(*request);
		return true;
	}

	std::uint32_t ColorRefToRgba(COLORREF color) noexcept
	{
		return (static_cast<std::uint32_t>(GetRValue(color)) << 24) |
			(static_cast<std::uint32_t>(GetGValue(color)) << 16) |
			(static_cast<std::uint32_t>(GetBValue(color)) << 8) | 0xffu;
	}

	Tool CurrentDraw3Tool() noexcept
	{
		if (IsLaserToolActive(stateMode)) return Tool::Laser;
		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtEraser)
		{
			return Tool::ConfiguredEraser;
		}

		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtShape)
		{
			switch (stateMode.Shape.ModeSelect)
			{
			case ShapeModeSelectEnum::IdtShapeStraightLine1:
				return Tool::SolidLine;
			case ShapeModeSelectEnum::IdtShapeDashedLine1:
				return Tool::DashedLine;
			case ShapeModeSelectEnum::IdtShapeRectangle1:
				return Tool::OutlineRectangle;
			default:
				return Tool::SolidLine;
			}
		}

		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen &&
			stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1)
			return Tool::Highlighter;
		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen &&
			stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHardPen)
			return Tool::HardPen;
		return Tool::Pen;
	}

	bool ResolvePenToolMode(PenToolSelectionEnum tool, PenModeSelectEnum& penMode) noexcept
	{
		switch (tool)
		{
		case PenToolSelectionEnum::SoftPen: penMode = PenModeSelectEnum::IdtPenSoftPen; return true;
		case PenToolSelectionEnum::HardPen: penMode = PenModeSelectEnum::IdtPenHardPen; return true;
		case PenToolSelectionEnum::Highlighter: penMode = PenModeSelectEnum::IdtPenHighlighter1; return true;
		case PenToolSelectionEnum::Laser: return true;
		default: return false;
		}
	}

	bool PenToolSelectionWouldChangeLocked(PenToolSelectionEnum tool,
		PenModeSelectEnum penMode) noexcept
	{
		const bool laser = tool == PenToolSelectionEnum::Laser;
		return stateMode.StateModeSelect != StateModeSelectEnum::IdtPen ||
			stateMode.laserActive != laser ||
			(!laser && stateMode.Pen.ModeSelect != penMode);
	}

	void ApplyPenModeTransitionLocked()
	{
		// 笔型与顶层模式共用一次修订，PPT 的旧条件回调不能覆盖较新的用户选择。
		stateMode.StateModeSelectTarget = StateModeSelectEnum::IdtPen;
		stateMode.StateModeSelect = StateModeSelectEnum::IdtPen;
		stateMode.StateModeSelectEcho = StateModeSelectEnum::IdtPen;
		stateMode.lastAnnotationMode = StateModeSelectEnum::IdtPen;
		BackgroundColorMode = computeContrast(GetPenColor(stateMode), RGB(255, 255, 255)) >= 3 ? 0 : 1;
		stateModeTransitionRevision.fetch_add(1, std::memory_order_release);
	}

	void PublishDraw3StateLocked() noexcept
	{
		// 调用方持模式锁；这里只发布 bridge，窗口 owner 提交必须留在锁外。
		ProductState state{};
		state.tool = CurrentDraw3Tool();
		state.paintDevice = stateMode.penWidthContext.paintDevice;
		state.penWidthContext = stateMode.penWidthContext;
		state.eraserInputs = ReadEraserPreferences();
		// 光标偏好和当前工具一起发布；设置线程不直接访问绘制对象或 HWND。
		const auto& cursor = Inkeys::config.Drawing.Cursor;
		state.cursorSettings.mouseStyle = Inkeys::Drawing::Draw3::RestoreMouseCursorStyle(cursor.MouseStyle.load());
		state.cursorSettings.penStyle = Inkeys::Drawing::Draw3::RestorePenCursorStyle(cursor.PenStyle.load());
		state.widthDip = GetPenWidthMetrics(stateMode).resolvedWidthDip;
		state.colorRgba = ColorRefToRgba(GetPenColor(stateMode));
		state.selectionMode =
			IsComputerInteractionMode(stateMode.StateModeSelect);
		state.autoSaveEnabled = setlist.saveSetting.enable;
		Inkeys::Drawing::Draw3::PublishProductState(state);
	}

	void PublishDraw3State() noexcept
	{
		std::scoped_lock modeLock(stateModeTransitionMutex);
		PublishDraw3StateLocked();
	}

	[[nodiscard]] bool Draw3WorkspaceReady(
		const Inkeys::Drawing::Draw3::HostRuntimeSnapshot& runtime,
		Workspace workspace) noexcept
	{
		if (!runtime.firstFrameReady || runtime.workspace != workspace)
			return false;
		const auto expectedTarget =
			Inkeys::Drawing::Draw3::Bridge::SelectionUsesAuxiliaryOutput(
				runtime.selectionMode, workspace)
			? Inkeys::Drawing::Draw3::HostOutputTarget::SelectionUlw
			: Inkeys::Drawing::Draw3::HostOutputTarget::PrimaryDrawpad;
		if (workspace != Workspace::Whiteboard && runtime.selectionMode &&
			!runtime.currentPageHasContent && !runtime.auxiliaryFullFrameClean)
			return false;
		return runtime.requestedOutputTarget == expectedTarget &&
			runtime.readyOutputTarget == expectedTarget &&
			runtime.readyOutputRevision == runtime.requestedOutputRevision &&
			runtime.presentedContentRevision == runtime.contentRevision;
	}

	[[nodiscard]] bool Draw3RecoveryNeedsUiRelease(
		const Inkeys::Drawing::Draw3::HostRuntimeSnapshot& runtime,
		const Inkeys::Drawing::Draw3::Bridge::ProductState& desired) noexcept
	{
		using namespace Inkeys::Drawing::Draw3;
		const auto status = runtime.recovery.status;
		return RecoveryMatchesDesired(runtime.recovery, desired) &&
			(status == CanvasRecoveryStatus::ProtectedConflict ||
				status == CanvasRecoveryStatus::ManualRetryRequired || status == CanvasRecoveryStatus::Failed);
	}

	[[nodiscard]] bool Draw3FatalRecoveryMatches(
		const Inkeys::Drawing::Draw3::HostRuntimeSnapshot& runtime,
		const Inkeys::Drawing::Draw3::Bridge::ProductState& desired) noexcept
	{
		using namespace Inkeys::Drawing::Draw3;
		return runtime.consumerFatal && runtime.recovery.status == CanvasRecoveryStatus::Failed &&
			runtime.recovery.reason == CanvasRecoveryReason::ConsumerFatal &&
			RecoveryMatchesDesired(runtime.recovery, desired);
	}

	bool WorkspaceTargetReady(const WorkspaceHandoff& request, const ProductState& desired,
		const Inkeys::Drawing::Draw3::HostRuntimeSnapshot& runtime, bool basicControl,
		bool safeRelease, const RECT& output) noexcept
	{
		using namespace Inkeys::Drawing::Draw3;
		if (safeRelease)
			return request.desired != Workspace::Whiteboard && runtime.hostGeneration != 0 && runtime.recovery.revision != 0 &&
				Draw3RecoveryNeedsUiRelease(runtime, desired);
		if (!runtime.running || runtime.consumerFatal || desired.workspace != request.desired ||
			(request.hostGeneration != 0 && runtime.hostGeneration != request.hostGeneration)) return false;
		if (request.minimized)
			return request.committed == Workspace::Whiteboard && request.desired == Workspace::Whiteboard;
		if (basicControl)
			return request.desired == Workspace::Presentation && !desired.presentationTarget;
		return runtime.recovery.status == CanvasRecoveryStatus::None &&
			(request.desired != Workspace::Whiteboard || !runtime.whiteboardLoadPending) &&
			runtime.committedWidth == output.right - output.left && runtime.committedHeight == output.bottom - output.top &&
			Draw3WorkspaceReady(runtime, request.desired) && runtime.selectionMode == desired.selectionMode &&
			runtime.appliedProductRevision == desired.revision && runtime.appliedTool == desired.tool &&
			DrawpadPresentationReady(desired, runtime) &&
			(request.desired != Workspace::Presentation || (desired.presentationTarget &&
				runtime.presentationUiReady == Bridge::ReadyIdentityFor(*desired.presentationTarget) &&
				runtime.presentationSuspended != Bridge::ReadyIdentityFor(*desired.presentationTarget)));
	}

	bool WorkspacePrimaryIdentityMatches(const WorkspaceHandoff& request, const ProductState& desired,
		const std::optional<Inkeys::Business::PptSessionToken>& preparedSession,
		const Inkeys::Business::PptSessionToken& current) noexcept
	{
		if (request.desired == Workspace::Whiteboard) return true;
		if (request.desired != (current.active ? Workspace::Presentation : Workspace::Desktop)) return false;
		if (request.desired != Workspace::Presentation) return true;
		if (!preparedSession || !Inkeys::Business::MatchesPptSession(*preparedSession, current)) return false;
		// 可信墨迹 target 仍须属于当前场次；BasicControl 没有 target，使用完整控制 token。
		return desired.workspace != Workspace::Presentation || !desired.presentationTarget ||
			(desired.presentationTarget->sessionRevision == current.localSession &&
				desired.presentationTarget->bindingRevision == current.bindingRevision);
	}

	bool ReleaseFatalDraw3Surfaces(
		const Inkeys::Drawing::Draw3::HostRuntimeSnapshot& runtime,
		const Inkeys::Drawing::Draw3::Bridge::ProductState& desired,
		std::function<bool()> stillDesired)
	{
		if (!Draw3FatalRecoveryMatches(runtime, desired)) return false;
		const auto guard = [expected = runtime.recovery, revision = desired.revision,
			outer = std::move(stillDesired)]() noexcept
		{
			const auto current = Inkeys::Drawing::Draw3::ProductRuntimeSnapshot();
			const auto latest = Inkeys::Drawing::Draw3::ProductHost().ProductBridge().Snapshot();
			return current.recovery == expected && latest.revision == revision &&
				Draw3FatalRecoveryMatches(current, latest) && (!outer || outer());
		};
		// fatal 后消费者可能已结束；仅允许隐藏/穿透清理，不能依赖 Running 重新为真。
		// 精确故障令牌在 Reset 后失效，旧清理不能触碰新一轮 Host。
		auto& service = Inkeys::Window::GetService();
		const bool hidden = service.SetDrawpadSurfaceVisibility(
			Inkeys::Window::DrawpadSurfaceVisibility::Hidden, guard);
		const bool auxiliary = !service.Handle(Inkeys::Window::WindowRole::DrawpadPresentation) ||
			service.SetClickThrough(Inkeys::Window::WindowRole::DrawpadPresentation, true, guard);
		const bool primary = service.SetClickThrough(Inkeys::Window::WindowRole::Drawpad, true, guard);
		return hidden && auxiliary && primary;
	}

	[[nodiscard]] const char* OutputTargetName(
		Inkeys::Drawing::Draw3::HostOutputTarget target) noexcept
	{
		return target == Inkeys::Drawing::Draw3::HostOutputTarget::SelectionUlw
			? "selection-ulw" : "primary-drawpad";
	}

	[[nodiscard]] const char* SurfaceVisibilityName(
		Inkeys::Window::DrawpadSurfaceVisibility visibility) noexcept
	{
		switch (visibility)
		{
		case Inkeys::Window::DrawpadSurfaceVisibility::Primary:
			return "primary";
		case Inkeys::Window::DrawpadSurfaceVisibility::Presentation:
			return "presentation";
		case Inkeys::Window::DrawpadSurfaceVisibility::Hidden:
		default:
			return "hidden";
		}
	}

	[[nodiscard]] const char* Draw3PresentationGateWaitReasonName(
		Draw3PresentationGateWaitReason reason) noexcept
	{
		switch (reason)
		{
		case Draw3PresentationGateWaitReason::BridgeModeMismatch:
			return "bridge-mode-mismatch";
		case Draw3PresentationGateWaitReason::SelectionHandoff:
			return "selection-handoff";
		case Draw3PresentationGateWaitReason::FirstFrame:
			return "first-frame";
		case Draw3PresentationGateWaitReason::SelectionOutput:
			return "selection-output";
		case Draw3PresentationGateWaitReason::None:
		default:
			return "none";
		}
	}

	void LogDraw3PresentationGateWait(
		Draw3PresentationGateWaitReason reason,
		const Inkeys::Drawing::Draw3::HostRuntimeSnapshot& runtime,
		std::uint64_t bridgeRevision, std::uint64_t modeRevision) noexcept
	{
		if (draw3PresentationGateWaitReason == reason) return;
		draw3PresentationGateWaitReason = reason;
		if (!IDTLogger) return;
		IDTLogger->info(
			"[状态线程][Draw3Gate] state=wait reason={} bridgeRevision={} modeRevision={} "
			"workspace={} selection={} pageContent={} firstFrame={} output={}@{} ready={}@{} "
			"content={}/{} auxiliaryClean={} commandPending={} uiReady={} inputReady={}",
			Draw3PresentationGateWaitReasonName(reason), bridgeRevision, modeRevision,
			static_cast<unsigned>(runtime.workspace), runtime.selectionMode,
			runtime.currentPageHasContent, runtime.firstFrameReady,
			OutputTargetName(runtime.requestedOutputTarget), runtime.requestedOutputRevision,
			OutputTargetName(runtime.readyOutputTarget), runtime.readyOutputRevision,
			runtime.presentedContentRevision, runtime.contentRevision,
			runtime.auxiliaryFullFrameClean, runtime.commandScenePending,
			runtime.presentationUiReady.has_value(), runtime.presentationInputReady);
	}

	void LogDraw3PresentationGateReady(
		const Inkeys::Drawing::Draw3::HostRuntimeSnapshot& runtime,
		std::uint64_t bridgeRevision, std::uint64_t modeRevision) noexcept
	{
		if (draw3PresentationGateWaitReason ==
			Draw3PresentationGateWaitReason::None) return;
		const auto previous = draw3PresentationGateWaitReason;
		draw3PresentationGateWaitReason =
			Draw3PresentationGateWaitReason::None;
		if (!IDTLogger) return;
		IDTLogger->info(
			"[状态线程][Draw3Gate] state=ready wake=state-reconcile previousReason={} "
			"bridgeRevision={} modeRevision={} firstFrame={} output={}@{} ready={}@{} "
			"content={}/{}",
			Draw3PresentationGateWaitReasonName(previous), bridgeRevision, modeRevision,
			runtime.firstFrameReady, OutputTargetName(runtime.requestedOutputTarget),
			runtime.requestedOutputRevision, OutputTargetName(runtime.readyOutputTarget),
			runtime.readyOutputRevision, runtime.presentedContentRevision,
			runtime.contentRevision);
	}

	void LogDraw3PresentationFailure(
		const Inkeys::Drawing::Draw3::HostRuntimeSnapshot& runtime,
		Inkeys::Window::DrawpadSurfaceVisibility visibility) noexcept
	{
		if (!IDTLogger) return;
		auto& service = Inkeys::Window::GetService();
		const HWND primary = service.Handle(Inkeys::Window::WindowRole::Drawpad);
		const HWND presentation = service.Handle(
			Inkeys::Window::WindowRole::DrawpadPresentation);
		RECT primaryBounds{};
		RECT presentationBounds{};
		if (primary) (void)GetWindowRect(primary, &primaryBounds);
		if (presentation) (void)GetWindowRect(presentation, &presentationBounds);
		const HWND primaryOwner = primary ? GetWindow(primary, GW_OWNER) : nullptr;
		const HWND presentationOwner = presentation
			? GetWindow(presentation, GW_OWNER) : nullptr;
		const LONG_PTR primaryExStyle = primary
			? GetWindowLongPtrW(primary, GWL_EXSTYLE) : 0;
		const LONG_PTR presentationExStyle = presentation
			? GetWindowLongPtrW(presentation, GWL_EXSTYLE) : 0;
		IDTLogger->warn(
			"[状态线程][ReconcileDraw3Presentation] surface 切换失败, "
			"surface={}, requested={}@{}, ready={}@{}, content={}/{}, "
			"firstFrame={}, clean={}, primary={{hwnd=0x{:X},owner=0x{:X},visible={},topmost={},rect=({},{},{},{})}}, "
			"presentation={{hwnd=0x{:X},owner=0x{:X},visible={},topmost={},rect=({},{},{},{})}}",
			SurfaceVisibilityName(visibility),
			OutputTargetName(runtime.requestedOutputTarget),
			runtime.requestedOutputRevision,
			OutputTargetName(runtime.readyOutputTarget),
			runtime.readyOutputRevision,
			runtime.presentedContentRevision, runtime.contentRevision,
			runtime.firstFrameReady, runtime.auxiliaryFullFrameClean,
			static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(primary)),
			static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(primaryOwner)),
			primary && IsWindowVisible(primary),
			(primaryExStyle & WS_EX_TOPMOST) != 0,
			primaryBounds.left, primaryBounds.top,
			primaryBounds.right, primaryBounds.bottom,
			static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(presentation)),
			static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(presentationOwner)),
			presentation && IsWindowVisible(presentation),
			(presentationExStyle & WS_EX_TOPMOST) != 0,
			presentationBounds.left, presentationBounds.top,
			presentationBounds.right, presentationBounds.bottom);
	}

	void TraceSelectionWindowState(
		const Inkeys::Drawing::Draw3::Bridge::ProductState& desired,
		const Inkeys::Drawing::Draw3::HostRuntimeSnapshot& runtime,
		const char* reason, bool hidden, bool force = false) noexcept
	{
		static const bool enabled = []() noexcept
		{
			wchar_t value[8]{};
			return GetEnvironmentVariableW(L"INKEYS_PPT_EXIT_TRACE", value, 8) > 0 &&
				value[0] == L'1';
		}();
		if (!enabled || !IDTLogger) return;
		// 等待期最多每两秒采一组真实 HWND，避免 250ms 重试刷屏。
		static auto last = std::chrono::steady_clock::time_point{};
		const auto now = std::chrono::steady_clock::now();
		if (!force && last != std::chrono::steady_clock::time_point{} &&
			now - last < std::chrono::seconds(2)) return;
		last = now;
		auto& service = Inkeys::Window::GetService();
		GUITHREADINFO threadInfo{ sizeof(GUITHREADINFO) };
		const DWORD ownerThread = service.OwnerThreadId(
			Inkeys::Window::WindowRole::Drawpad);
		const bool captureKnown = ownerThread &&
			GetGUIThreadInfo(ownerThread, &threadInfo) != FALSE;
		IDTLogger->info(
			"[PptExitSurface] reason={} hidden={} desired={} selection={} bridgeRev={} "
			"runtime={} selection={} runtimeRev={} first={} presentOk={} inputReady={} "
			"session={} target={} output={}@{}/{}@{} content={}/{} "
			"clean={} captureKnown={} capture=0x{:X}",
			reason, hidden, static_cast<int>(desired.workspace), desired.selectionMode,
			desired.revision, static_cast<int>(runtime.workspace), runtime.selectionMode,
			runtime.runtimeRevision, runtime.firstFrameReady,
			runtime.lastPresentSucceeded, runtime.presentationInputReady,
			desired.presentationTarget ? desired.presentationTarget->sessionRevision : 0,
			desired.presentationTarget ? desired.presentationTarget->targetRevision : 0,
			OutputTargetName(runtime.requestedOutputTarget),
			runtime.requestedOutputRevision,
			OutputTargetName(runtime.readyOutputTarget),
			runtime.readyOutputRevision, runtime.presentedContentRevision,
			runtime.contentRevision, runtime.auxiliaryFullFrameClean, captureKnown,
			static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(
				captureKnown ? threadInfo.hwndCapture : nullptr)));
		IDTLogger->info(
			"[PptExitSurface] ready={}@{} uiReady={}@{} suspended={}@{} commandPending={}",
			runtime.presentationReady ? runtime.presentationReady->sessionRevision : 0,
			runtime.presentationReady ? runtime.presentationReady->targetRevision : 0,
			runtime.presentationUiReady ? runtime.presentationUiReady->sessionRevision : 0,
			runtime.presentationUiReady ? runtime.presentationUiReady->targetRevision : 0,
			runtime.presentationSuspended ? runtime.presentationSuspended->sessionRevision : 0,
			runtime.presentationSuspended ? runtime.presentationSuspended->targetRevision : 0,
			runtime.commandScenePending);
		constexpr Inkeys::Window::WindowRole roles[] = {
			Inkeys::Window::WindowRole::Drawpad,
			Inkeys::Window::WindowRole::DrawpadPresentation,
			Inkeys::Window::WindowRole::Bar,
			Inkeys::Window::WindowRole::Freeze,
			Inkeys::Window::WindowRole::MagnifierHost,
		};
		for (const auto role : roles)
		{
			const HWND hwnd = service.Handle(role);
			const HWND owner = hwnd ? GetWindow(hwnd, GW_OWNER) : nullptr;
			const LONG_PTR exStyle = hwnd ? GetWindowLongPtrW(hwnd, GWL_EXSTYLE) : 0;
			IDTLogger->info(
				"[PptExitSurface] role={} hwnd=0x{:X} owner=0x{:X} valid={} "
				"visible={} enabled={} exStyle=0x{:X}",
				static_cast<int>(role),
				static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(hwnd)),
				static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(owner)),
				hwnd && IsWindow(hwnd), hwnd && IsWindowVisible(hwnd),
				hwnd && IsWindowEnabled(hwnd),
				static_cast<unsigned long long>(exStyle));
		}
	}

	std::function<bool()> WhiteboardTransitionGuard(WhiteboardPhase phase, std::uint64_t epoch)
	{
		return [phase, epoch]() noexcept
		{
			return whiteboardPhase.load(std::memory_order_acquire) == phase &&
				whiteboardInputEpoch.load(std::memory_order_acquire) == epoch &&
				whiteboardDesired.load(std::memory_order_acquire) == ((epoch & 1U) != 0);
		};
	}

	std::function<bool()> WhiteboardActivationGuard(std::uint64_t epoch)
	{
		return [epoch]() noexcept
		{
			const auto phase = whiteboardPhase.load(std::memory_order_acquire);
			// 激活异步执行时，真实就绪可能已从 Entering 提交到 Active；退出或新意图仍必须失效。
			return (phase == WhiteboardPhase::Entering || phase == WhiteboardPhase::Active) &&
				WorkspaceRequestCurrent(epoch) &&
				whiteboardDesired.load(std::memory_order_acquire) && (epoch & 1U) != 0;
		};
	}

	// 样式去重属于窗口 owner；调用方只提交带当前事务身份的意图，不共享 HWND/布尔缓存。
	bool SyncDrawpadClickThrough(bool primaryClickThrough, std::function<bool()> stillDesired)
	{
		auto& service = Inkeys::Window::GetService();
		const bool auxiliary = !service.Handle(Inkeys::Window::WindowRole::DrawpadPresentation) ||
			service.SetClickThrough(Inkeys::Window::WindowRole::DrawpadPresentation, true);
		const bool primary = service.SetClickThrough(Inkeys::Window::WindowRole::Drawpad,
			primaryClickThrough, [guard = std::move(stillDesired)]() noexcept
			{ return Inkeys::Drawing::Draw3::ProductRunning() && (!guard || guard()); });
		return auxiliary && primary;
	}

	[[nodiscard]] Draw3PresentationReconcileResult
		ReconcileDraw3PresentationState()
	{
		const auto scene = ReadWorkspaceHandoff();
		if (scene.pending || (scene.committed == Workspace::Whiteboard && scene.committedMinimized))
			return Draw3PresentationReconcileResult::Waiting;
		const auto failure = Inkeys::Drawing::Draw3::ProductRuntimeSnapshot();
		const auto failureDesired = Inkeys::Drawing::Draw3::ProductHost().ProductBridge().Snapshot();
		if (Draw3FatalRecoveryMatches(failure, failureDesired) && !WhiteboardTransactionActive())
		{
			std::scoped_lock lock(draw3PresentationMutex);
			const bool released = ReleaseFatalDraw3Surfaces(failure, failureDesired,
				[]() noexcept { return !WhiteboardTransactionActive(); });
			draw3PresentationRetryPending.store(!released, std::memory_order_release);
			return released ? Draw3PresentationReconcileResult::Applied : Draw3PresentationReconcileResult::Retry;
		}
		// 绘图宿主已停时，旧 ready/revision 不得让异步窗口命令重新显示拦截面。
		if (!Inkeys::Drawing::Draw3::ProductRunning())
		{
			draw3PresentationRetryPending.store(false, std::memory_order_release);
			return Draw3PresentationReconcileResult::Waiting;
		}
		std::uint64_t modeRevision = 0;
		bool modeSelection = false;
		{
			std::scoped_lock modeLock(stateModeTransitionMutex);
			modeRevision = stateModeTransitionRevision.load(std::memory_order_relaxed);
			modeSelection = IsComputerInteractionMode(stateMode.StateModeSelect);
		}
		std::scoped_lock lock(draw3PresentationMutex);
		const auto desired =
			Inkeys::Drawing::Draw3::ProductHost().ProductBridge().Snapshot();
		const auto runtime = Inkeys::Drawing::Draw3::ProductRuntimeSnapshot();
		// 曾观测到放映后，桌面就绪交接必须恢复根层级；不绑定旧退出工具版本。
		if (desired.workspace == Workspace::Presentation ||
			runtime.workspace == Workspace::Presentation)
			draw3PresentationExitRefreshPending = true;
		// 新工具已选中但 bridge 尚未发布时，不提交旧画布可见性。
		if (modeRevision != stateModeTransitionRevision.load(std::memory_order_acquire) ||
			modeSelection != desired.selectionMode)
		{
			LogDraw3PresentationGateWait(
				Draw3PresentationGateWaitReason::BridgeModeMismatch, runtime,
				desired.revision, modeRevision);
			draw3PresentationRetryPending.store(true, std::memory_order_release);
			return Draw3PresentationReconcileResult::Retry;
		}
		if (!WhiteboardTransactionActive() && Draw3RecoveryNeedsUiRelease(runtime, desired))
		{
			// 受保护的失败不是 ready。隐藏旧场景的拦截面，保留工具栏/分页的恢复入口。
			const auto guard = [revision = desired.revision, modeRevision, expected = runtime.recovery]() noexcept
			{
				return Inkeys::Drawing::Draw3::ProductRunning() && !WhiteboardTransactionActive()
					&& Inkeys::Drawing::Draw3::ProductRuntimeSnapshot().recovery == expected
					&& stateModeTransitionRevision.load(std::memory_order_acquire) == modeRevision
					&& Inkeys::Drawing::Draw3::ProductHost().ProductBridge().Snapshot().revision == revision;
			};
			auto& service = Inkeys::Window::GetService();
			const bool hidden = service.SetDrawpadSurfaceVisibility(
				Inkeys::Window::DrawpadSurfaceVisibility::Hidden, guard);
			const bool clickThrough = SyncDrawpadClickThrough(true, guard);
			const bool released = hidden && clickThrough;
			draw3PresentationRetryPending.store(!released, std::memory_order_release);
			return released ? Draw3PresentationReconcileResult::Applied : Draw3PresentationReconcileResult::Retry;
		}
		// 白板进出期间由相位机暂时改穿透；稳定态必须跟当前选择模式，不能停在离场时的那一次。
		if (!WhiteboardTransactionActive() && !SyncDrawpadClickThrough(desired.selectionMode,
			[revision = desired.revision, modeRevision]() noexcept
			{
				return !WhiteboardTransactionActive() &&
					stateModeTransitionRevision.load(std::memory_order_acquire) == modeRevision &&
					Inkeys::Drawing::Draw3::ProductHost().ProductBridge().Snapshot().revision == revision;
			}))
		{
			// 样式失败也保留 250ms 重试；没有新 runtime revision 时仍能恢复窗口命中。
			draw3PresentationRetryPending.store(true, std::memory_order_release);
			return Draw3PresentationReconcileResult::Retry;
		}
		const auto exitSelectionRevision = draw3ExitSelectionModeRevision.load(
			std::memory_order_acquire);
		const bool selectionMode = runtime.selectionMode;
		const bool whiteboard = runtime.workspace ==
			Inkeys::Drawing::Draw3::Bridge::Workspace::Whiteboard;
		const auto bridgeGuard = [](std::uint64_t revision, bool requireSelection,
			std::uint64_t expectedModeRevision)
		{
			return [revision, requireSelection, expectedModeRevision]() noexcept
			{
				const auto current = Inkeys::Drawing::Draw3::ProductHost()
					.ProductBridge().Snapshot();
				return Inkeys::Drawing::Draw3::ProductRunning() &&
					current.revision == revision &&
					(!requireSelection || (current.selectionMode &&
						current.workspace != Workspace::Whiteboard)) &&
					(current.workspace == Workspace::Whiteboard ||
						(!whiteboardDesired.load(std::memory_order_acquire) &&
						whiteboardPhase.load(std::memory_order_acquire) ==
							WhiteboardPhase::Inactive)) &&
					stateModeTransitionRevision.load(std::memory_order_acquire) ==
						expectedModeRevision;
			};
		};
		auto& service = Inkeys::Window::GetService();
		using Inkeys::Window::WindowRole;

		// 白板空页仍保留完整绘制栏和主 Drawpad；选择只表示拖动态，不切辅助 ULW。
		Inkeys::UI::Bar::SetCurrentPageHasContent(
			whiteboard || runtime.currentPageHasContent);
		Inkeys::UI::Bar::NoteRecallHistoryAvailability(
			runtime.canUndo, runtime.canRedo);
		const bool targetReady = Inkeys::Drawing::Draw3::DrawpadPresentationReady(
			desired, runtime);
		if (!targetReady)
		{
			const bool exitWaiting = ExitSelectionHandoffPending() &&
				desired.selectionMode && desired.workspace == Workspace::Desktop;
			LogDraw3PresentationGateWait(
				exitWaiting ? Draw3PresentationGateWaitReason::SelectionHandoff :
				!runtime.firstFrameReady ? Draw3PresentationGateWaitReason::FirstFrame :
				Draw3PresentationGateWaitReason::SelectionOutput,
				runtime, desired.revision, modeRevision);
			// 保留已呈现的表面，直到目标工作区/模式的完整帧就绪再成对切换。
			// 旧 DComp 主窗在此期间不保证系统穿透，不能把输入门关闭当成穿透。
			draw3PresentationRetryPending.store(true, std::memory_order_release);
			draw3PresentationFailureActive = false;
			if (exitWaiting)
			{
				draw3SelectionWaitActive = true;
				TraceSelectionWindowState(desired, runtime, "selection-output-not-ready", false);
			}
			return Draw3PresentationReconcileResult::Waiting;
		}

		Inkeys::Window::DrawpadSurfaceVisibility visibility;
		if (whiteboard)
		{
			visibility = Inkeys::Window::DrawpadSurfaceVisibility::Primary;
		}
		else switch (Inkeys::Drawing::Draw3::ResolveDrawpadPresentationSurface(
			selectionMode, runtime.currentPageHasContent,
			runtime.auxiliaryFullFrameClean))
		{
		case Inkeys::Drawing::Draw3::DrawpadPresentationSurface::Primary:
			visibility = Inkeys::Window::DrawpadSurfaceVisibility::Primary; break;
		case Inkeys::Drawing::Draw3::DrawpadPresentationSurface::Presentation:
			visibility = Inkeys::Window::DrawpadSurfaceVisibility::Presentation; break;
		case Inkeys::Drawing::Draw3::DrawpadPresentationSurface::Hidden:
		default:
			visibility = Inkeys::Window::DrawpadSurfaceVisibility::Hidden; break;
		}

		// 等待窗口提交期间若期望场景已变，旧 ready 不得重新显示主拦截窗。
		const auto latest =
			Inkeys::Drawing::Draw3::ProductHost().ProductBridge().Snapshot();
		if (latest.revision != desired.revision)
		{
			draw3PresentationRetryPending.store(true, std::memory_order_release);
			return Draw3PresentationReconcileResult::Retry;
		}
		if (!service.SetDrawpadSurfaceVisibility(visibility,
			bridgeGuard(desired.revision, false, modeRevision)))
		{
			draw3PresentationRetryPending.store(true, std::memory_order_release);
			const auto current = Inkeys::Drawing::Draw3::ProductHost()
				.ProductBridge().Snapshot();
			if (current.revision != desired.revision ||
				modeRevision != stateModeTransitionRevision.load(std::memory_order_acquire))
			{
				return Draw3PresentationReconcileResult::Retry;
			}
			if (!draw3PresentationFailureActive)
				LogDraw3PresentationFailure(runtime, visibility);
			draw3PresentationFailureActive = true;
			return Draw3PresentationReconcileResult::Retry;
		}

		const auto after =
			Inkeys::Drawing::Draw3::ProductHost().ProductBridge().Snapshot();
		if (after.revision != desired.revision ||
			modeRevision != stateModeTransitionRevision.load(std::memory_order_acquire))
		{
			// 提交期间目标改变时保留现有画面，下次仅提交最新目标的就绪帧。
			draw3PresentationRetryPending.store(true, std::memory_order_release);
			return Draw3PresentationReconcileResult::Retry;
		}
		if (draw3PresentationExitRefreshPending && desired.workspace == Workspace::Desktop)
		{
			// 用户退出后选择新工具也立即恢复根层级；失败按最新目标继续重试。
			if (!service.RequestTopmostRefresh())
			{
				draw3PresentationRetryPending.store(true, std::memory_order_release);
				return Draw3PresentationReconcileResult::Retry;
			}
			draw3PresentationExitRefreshPending = false;
		}
		LogDraw3PresentationGateReady(runtime, desired.revision, modeRevision);
		draw3PresentationRetryPending.store(false, std::memory_order_release);
		if (ExitSelectionHandoffPending() && desired.selectionMode &&
			desired.workspace == Workspace::Desktop)
		{
			auto handoffRevision = exitSelectionRevision;
			(void)draw3ExitSelectionModeRevision.compare_exchange_strong(
				handoffRevision, 0, std::memory_order_release,
				std::memory_order_relaxed);
		}
		if (draw3SelectionWaitActive)
		{
			TraceSelectionWindowState(desired, runtime, "ready-applied",
				visibility == Inkeys::Window::DrawpadSurfaceVisibility::Hidden, true);
			draw3SelectionWaitActive = false;
		}
		if (draw3PresentationFailureActive && IDTLogger)
			IDTLogger->info(
				"[状态线程][ReconcileDraw3Presentation] surface 切换已恢复, surface={}",
				SurfaceVisibilityName(visibility));
		draw3PresentationFailureActive = false;
		const HWND drawpad = service.Handle(WindowRole::Drawpad);
		const HWND presentation = service.Handle(WindowRole::DrawpadPresentation);
		IdtWindowsIsVisible.drawpadWindow = drawpad && presentation &&
			service.Ready(WindowRole::Drawpad) &&
			service.Ready(WindowRole::DrawpadPresentation) &&
			runtime.firstFrameReady;
		return Draw3PresentationReconcileResult::Applied;
	}
}

bool AutomaticPenResponseAvailable() noexcept
{
	static const bool available=GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetPointerType")!=nullptr;
	return available;
}
void InitializeEraserInputPreferences()
{
	using namespace Inkeys::Drawing::Draw3::SpeedEraser;
	std::scoped_lock lock(eraserPreferencesMutex);
	auto& saved=Inkeys::config.Drawing.Eraser;
	const std::array fields{&saved.MouseLeft,&saved.MouseRight,&saved.Touch,&saved.PenTip,&saved.PenTail};
	for(size_t i=0;i<fields.size();++i)
		*fields[i]=static_cast<int>(RestoreEraserKind(fields[i]->load(),
			setlist.eraserSetting.savedFixedChoice?2:-1,static_cast<InputEntry>(i)));
	saved.BaseDiameterDip=static_cast<int>(RestoreBaseSize(saved.BaseDiameterDip.load()));
	saved.Sensitivity=static_cast<int>(RestoreSensitivity(saved.Sensitivity.load()));
	if(saved.PenTipResponse.load()==-1)saved.PenTipResponse=AutomaticPenResponseAvailable()?0:2;
	if(saved.PenTailResponse.load()==-1)saved.PenTailResponse=AutomaticPenResponseAvailable()?0:2;
}
int EraserWidthPreference(int entry)
{
	const auto prefs=ReadEraserPreferences();
	return entry>=0 && entry<5?static_cast<int>(prefs.entries[entry].kind):1;
}
int EraserPenResponsePreference(int entry)
{
	const auto prefs=ReadEraserPreferences();
	return static_cast<int>(prefs.entries[entry==4?4:3].penResponse);
}
void SetEraserInputPreference(int entry,int kind,int penResponse)
{
	if(entry<0 || entry>=5)return;
	{
		std::scoped_lock lock(eraserPreferencesMutex);
		auto& saved=Inkeys::config.Drawing.Eraser;
		const std::array fields{&saved.MouseLeft,&saved.MouseRight,&saved.Touch,&saved.PenTip,&saved.PenTail};
		*fields[entry]=kind==0?0:1;
		if(penResponse>=0 && (entry==3 || entry==4))
			(entry==3?saved.PenTipResponse:saved.PenTailResponse)=static_cast<int>(
				Inkeys::Drawing::Draw3::SpeedEraser::RestorePenResponse(penResponse,AutomaticPenResponseAvailable()));
	}
	SyncDraw3State(); // 写盘仍由设置页的既有合并队列负责。
	barUISet.UpdateRendering(false);
}

Inkeys::Drawing::Draw3::SpeedEraser::InputSettings EraserPreferencesSnapshot()
{
	return ReadEraserPreferences();
}
void SetGlobalEraserPreference(int baseDiameterDip, int sensitivity, int automatic)
{
	using namespace Inkeys::Drawing::Draw3::SpeedEraser;
	bool changed = false;
	{
		std::scoped_lock lock(eraserPreferencesMutex);
		auto& saved = Inkeys::config.Drawing.Eraser;
		if (baseDiameterDip != -1)
		{
			const int value = static_cast<int>(RestoreBaseSize(baseDiameterDip));
			changed |= saved.BaseDiameterDip.load() != value;
			saved.BaseDiameterDip = value;
		}
		if (sensitivity != -1)
		{
			const int value = static_cast<int>(RestoreSensitivity(sensitivity));
			changed |= saved.Sensitivity.load() != value;
			saved.Sensitivity = value;
		}
		if (automatic != -1)
		{
			// 总开关只门控解析，不销毁五入口各自保存的 Fixed/Speed 偏好。
			const bool value = automatic != 0;
			changed |= saved.Automatic.load() != value;
			saved.Automatic = value;
		}
		if (changed) Inkeys::UI::Setting::RequestConfigWrite();
	}
	if (!changed) return;
	SyncDraw3State();
	barUISet.UpdateRendering(false);
}

namespace { void ResolveWidthSlotsLocked(); }

void SyncDraw3State()
{
	if (Inkeys::UI::Setting::DeferDraw3SyncIfRendering()) return;
    // 设备更新必须在同锁内只改设备字段，避免用旧显示快照覆盖新的目标 DPI。
    {
        std::scoped_lock lock(stateModeTransitionMutex);
        const int paintDevice = setlist.paintDevice.load();
        if (stateMode.penWidthContext.paintDevice != paintDevice)
        {
            stateMode.penWidthContext.paintDevice = paintDevice;
            ResolveWidthSlotsLocked();
            stateModeTransitionRevision.fetch_add(1, std::memory_order_release);
        }
    }
	// 输入只提交最新业务/owner 期望；原生调用由既有协调线程消费，不能阻塞共享 UI worker。
	{
		std::scoped_lock modeLock(stateModeTransitionMutex);
		(void)PublishSettingOwnerDesiredState(!IsComputerInteractionMode(stateMode.StateModeSelect));
		PublishDraw3StateLocked();
	}
	draw3PresentationRetryPending.store(true, std::memory_order_release);
	Inkeys::Drawing::Draw3::NotifyProductWorkspaceProgress();
}

void ReconcileDraw3Presentation()
{
	(void)ReconcileDraw3PresentationState();
}

StateModeClass GetStateModeSnapshot()
{
	std::scoped_lock lock(stateModeTransitionMutex);
	return stateMode;
}

bool TryGetDesktopStateModeSnapshot(StateModeClass& snapshot)
{
	std::scoped_lock lock(stateModeTransitionMutex);
	// 工作区身份与值同锁读取；不得把白板/PPT 工具写入无工作区字段的全局 memory。
	if (stateModeWorkspace != Workspace::Desktop) return false;
	snapshot = stateMode;
	return true;
}

StateModeVersionedSnapshot GetStateModeVersionedSnapshot()
{
	std::scoped_lock lock(stateModeTransitionMutex);
	return { stateMode, stateModeTransitionRevision.load(std::memory_order_relaxed) };
}

bool IsLaserPenSelected() noexcept
{
	return GetStateModeSnapshot().laserActive;
}

bool IsLaserToolActive(const StateModeClass& snapshot) noexcept
{
	return Inkeys::Business::IsLaserToolActive(
		snapshot.StateModeSelect == StateModeSelectEnum::IdtPen,
		snapshot.laserActive);
}

bool IsLaserToolActive() noexcept
{
	return IsLaserToolActive(GetStateModeSnapshot());
}

WorkspaceSceneSnapshot GetWorkspaceSceneSnapshot() noexcept
{
	const auto scene = ReadWorkspaceHandoff();
	const auto project = [](Workspace workspace) noexcept
	{
		return workspace == Workspace::Whiteboard ? WorkspaceScene::Whiteboard :
			workspace == Workspace::Presentation ? WorkspaceScene::Presentation : WorkspaceScene::Desktop;
	};
	return { project(scene.desired), project(scene.committed),
		scene.revision, scene.pending, scene.minimized, scene.committedMinimized };
}

CommittedWorkspaceStateSnapshot GetCommittedWorkspaceStateSnapshot()
{
	const auto scene = GetWorkspaceSceneSnapshot();
	const auto committed = scene.committed == WorkspaceScene::Whiteboard ? Workspace::Whiteboard :
		scene.committed == WorkspaceScene::Presentation ? Workspace::Presentation : Workspace::Desktop;
	// 不跨两个锁：工具锁下读取指定已提交工作区的现有记忆，同一渲染帧沿用这份场景身份。
	std::scoped_lock lock(stateModeTransitionMutex);
	if (stateModeWorkspace == committed) return { scene, stateMode };
	const auto& saved = workspaceStateModes[static_cast<std::size_t>(committed)];
	return { scene, saved ? *saved : stateMode };
}

StateModeClass GetCommittedStateModeSnapshot()
{
	return GetCommittedWorkspaceStateSnapshot().state;
}

std::uint64_t WorkspaceTransitionFailureRevision() noexcept
{
	const auto request = ReadWorkspaceHandoff();
	return request.pending && request.failed ? request.revision : 0;
}

bool RetryWorkspaceTransitionIfRevision(std::uint64_t revision) noexcept
{
	WorkspaceHandoff request;
	{
		std::scoped_lock lock(workspaceHandoffMutex);
		if (!workspaceHandoff.Retry(revision)) return false;
		ProjectWorkspaceHandoffLocked();
		request = workspaceHandoff;
	}
	(void)CloseWorkspaceAdmission(request);
	Inkeys::UI::Bar::RefreshToolState();
	return true;
}

bool CancelFailedWhiteboardTransitionIfRevision(std::uint64_t revision) noexcept
{
	const auto captured = whiteboardNativeRequest.load(std::memory_order_acquire);
	const auto primary = CurrentPrimaryWorkspace();
	WorkspaceHandoff request;
	{
		std::scoped_lock lock(workspaceHandoffMutex);
		if (!revision || workspaceHandoff.revision != revision || !workspaceHandoff.pending ||
			!workspaceHandoff.failed || workspaceHandoff.desired != Workspace::Whiteboard) return false;
		workspaceHandoff.Request(primary);
		ProjectWorkspaceHandoffLocked();
		request = workspaceHandoff;
	}
	RetireNativeWhiteboardRequest(captured);
	{ std::scoped_lock lock(whiteboardReturnFocusMutex); whiteboardReturnFocus.Begin(Inkeys::Business::CapturePptFocusRequest()); }
	whiteboardPreviousRequested.store(false, std::memory_order_release);
	whiteboardNextRequested.store(false, std::memory_order_release);
	(void)CloseWorkspaceAdmission(request);
	return true;
}

void NotifyWhiteboardWindowViewRequested(bool minimized) noexcept
{
	QueueNativeWhiteboardRequest(minimized ? NativeWhiteboardRequest::Minimize : NativeWhiteboardRequest::Restore);
}

void NotifyWhiteboardWindowCloseRequested() noexcept
{
	QueueNativeWhiteboardRequest(NativeWhiteboardRequest::Close);
}

bool RequestWhiteboardWindowView(bool minimized) noexcept
{
	const auto captured = whiteboardNativeRequest.load(std::memory_order_acquire);
	const auto before = ReadWorkspaceHandoff();
	if (before.desired != Workspace::Whiteboard || (minimized && PptSessionActive())) return false;
	const bool cancelQueued = captured && (captured >> 2) == before.revision;
	const auto request = RequestWorkspace(Workspace::Whiteboard, minimized, cancelQueued);
	// 先发布新版本再清捕获的旧投递，旧 guard 中间也不能复活；新投递不被覆盖。
	RetireNativeWhiteboardRequest(captured);
	return CloseWorkspaceAdmission(request);
}

void NotifyWhiteboardWindowMinimizedChanged() noexcept
{
	// WndProc 只发通知；禁止在窗口 owner 内等待 Product 的控制锁。
	whiteboardViewChanged.store(true, std::memory_order_release);
	Inkeys::Drawing::Draw3::NotifyProductWorkspaceProgress();
}

bool PptScenePreparationAllowed() noexcept
{
	return ReadWorkspaceHandoff().desired != Workspace::Whiteboard;
}

void PublishPptWorkspaceControlState(const Inkeys::Business::PptSessionToken& session,
	bool basicControl) noexcept
{
	if (!Inkeys::Business::MatchesPptSession(session, Inkeys::Business::CapturePptInputSession())) return;
	std::scoped_lock lock(workspaceHandoffMutex);
	basicControlSession = basicControl ? std::optional(session) : std::nullopt;
}

void RequestWhiteboardActive(bool active) noexcept
{
	const auto captured = whiteboardNativeRequest.load(std::memory_order_acquire);
	if (!IsWhiteboardFeatureEnabled()) active = false;
	const auto before = ReadWorkspaceHandoff();
	{
		std::scoped_lock lock(whiteboardReturnFocusMutex);
		if (active && before.desired != Workspace::Whiteboard) whiteboardReturnFocus.Reset();
		else if (!active && before.desired == Workspace::Whiteboard)
			whiteboardReturnFocus.Begin(Inkeys::Business::CapturePptFocusRequest());
	}
	const auto request = RequestWorkspace(active ? Workspace::Whiteboard : CurrentPrimaryWorkspace(),
		active && before.desired == Workspace::Whiteboard && before.minimized,
		captured && (captured >> 2) == before.revision);
	RetireNativeWhiteboardRequest(captured);
	// 请求先真正关 Host Down，再允许监视线程发布任何目标和工具。
	(void)CloseWorkspaceAdmission(request);
	if (!active)
	{
		whiteboardPreviousRequested.store(false, std::memory_order_release);
		whiteboardNextRequested.store(false, std::memory_order_release);
	}
}

bool WhiteboardRequested() noexcept
{
	return IsWhiteboardFeatureEnabled() &&
		whiteboardDesired.load(std::memory_order_acquire);
}

bool WhiteboardActive() noexcept
{
	return IsWhiteboardFeatureEnabled() &&
		whiteboardPhase.load(std::memory_order_acquire) ==
		WhiteboardPhase::Active;
}

bool WhiteboardTransactionActive() noexcept
{
	return IsWhiteboardFeatureEnabled() &&
		(whiteboardDesired.load(std::memory_order_acquire) ||
		whiteboardPhase.load(std::memory_order_acquire) !=
		WhiteboardPhase::Inactive);
}

std::uint64_t PptInputWorkspaceEpoch() noexcept
{
	const auto epoch=whiteboardInputEpoch.load(std::memory_order_acquire);
	return (epoch&1U)==0 && !ReadWorkspaceHandoff().pending && !WhiteboardTransactionActive() ? epoch : 0;
}

std::uint64_t PptSceneWorkspaceEpoch() noexcept
{
	const auto epoch = whiteboardInputEpoch.load(std::memory_order_acquire);
	const auto phase = whiteboardPhase.load(std::memory_order_acquire);
	return (epoch & 1U) == 0 && !whiteboardDesired.load(std::memory_order_acquire) &&
		(phase == WhiteboardPhase::Inactive || phase == WhiteboardPhase::Exiting) ? epoch : 0;
}

void RequestWhiteboardTheme(unsigned int rgb, int pattern, std::uint64_t generation, int currentPage) noexcept
{
	if (!WhiteboardActive() || pattern < 0 || pattern > 2) return;
	const auto snapshot = Inkeys::Drawing::Draw3::ProductRuntimeSnapshot();
	if (snapshot.whiteboardLoadPending || currentPage < 1
		|| snapshot.whiteboardGeneration != generation
		|| snapshot.currentPageIndex != static_cast<std::size_t>(currentPage - 1)) return;
	// 页面身份随主题请求一起投递，迟到的菜单动作不能改写下一页。
	(void)Inkeys::Drawing::Draw3::RequestWhiteboardTheme(
		{ rgb, static_cast<Inkeys::Drawing::Draw3::WhiteboardPattern>(pattern) },
		snapshot.whiteboardGeneration, snapshot.currentPageIndex);
}

void RequestWhiteboardPreviousPage() noexcept
{
	if (WhiteboardActive())
	{
		whiteboardPreviousRequested.store(true, std::memory_order_release);
		Inkeys::Drawing::Draw3::NotifyProductWorkspaceProgress();
	}
}

void RequestWhiteboardNextPage() noexcept
{
	if (WhiteboardActive())
	{
		whiteboardNextRequested.store(true, std::memory_order_release);
		Inkeys::Drawing::Draw3::NotifyProductWorkspaceProgress();
	}
}

namespace
{
    using namespace Inkeys::Business::PenWidth;
    void ResolveWidthSlotsLocked()
    {
        const auto resolve = [&](auto& slot, Family family)
        {
            slot.width = static_cast<float>(Resolve(slot.widthSelection, family,
                stateMode.penWidthContext).resolvedWidthDip);
        };
        resolve(stateMode.Pen.Brush1, Family::Brush);
        resolve(stateMode.Pen.Highlighter1, Family::Highlighter);
        resolve(stateMode.Pen.Laser, Family::Laser);
        resolve(stateMode.Shape.StraightLine1, Family::Brush);
        resolve(stateMode.Shape.Rectangle1, Family::Brush);
        stateMode.Pen.Brush1.widthPreset = static_cast<float>(PresetDip(Preset::Medium, Family::Brush, stateMode.penWidthContext));
        stateMode.Pen.Highlighter1.widthPreset = static_cast<float>(PresetDip(Preset::Medium, Family::Highlighter, stateMode.penWidthContext));
    }
	void ApplyExitSelectionLocked()
	{
		// 版本比较与模式赋值串行；窗口 owner 提交在锁外执行，避免互等。
		stateMode.StateModeSelectTarget = StateModeSelectEnum::IdtSelection;
		if (state == 1.1) state = 1;
		stateMode.StateModeSelect = StateModeSelectEnum::IdtSelection;
		stateMode.StateModeSelectEcho = StateModeSelectEnum::IdtSelection;
		BackgroundColorMode = 0;
		// 先登记真退出版本，避免并发协调线程把普通 Selection 当成退出交接。
		const auto nextRevision = stateModeTransitionRevision.load(
			std::memory_order_relaxed) + 1;
		draw3ExitSelectionModeRevision.store(nextRevision, std::memory_order_release);
		stateModeTransitionRevision.store(nextRevision, std::memory_order_release);
	}
	// 调用方持模式锁；这里只迁移工具记忆/所属工作区，不发布 Bridge 或执行窗口同步。
	bool RestoreWorkspaceStateModeLocked(Workspace workspace)
	{
		if (stateModeWorkspace == workspace) return false;
		workspaceStateModes[static_cast<std::size_t>(stateModeWorkspace)] = stateMode;
		auto& saved = workspaceStateModes[static_cast<std::size_t>(workspace)];
		if (!saved)
		{
			// 新工作区沿用宽色偏好；白板默认绘制，其他工作区默认选择/拖拽。
			saved = stateMode;
			const auto defaultMode = workspace == Workspace::Whiteboard
				? StateModeSelectEnum::IdtPen : StateModeSelectEnum::IdtSelection;
			saved->StateModeSelect = defaultMode;
			saved->StateModeSelectTarget = defaultMode;
			saved->StateModeSelectEcho = defaultMode;
			saved->lastAnnotationMode = StateModeSelectEnum::IdtPen;
			saved->laserActive = false;
			saved->Pen.ModeSelect = PenModeSelectEnum::IdtPenSoftPen;
			saved->Shape.ModeSelect = ShapeModeSelectEnum::IdtShapeStraightLine1;
		}
		const bool exitSelectionPending = ExitSelectionHandoffPending();
		const auto widthContext = stateMode.penWidthContext;
		stateMode = *saved;
		// 合成式设置生效后，旧工作区记忆中的独立穿透态也要回落选择。
		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtClickThrough &&
			GetSelectAndClickThroughMode() == 0)
		{
			stateMode.StateModeSelect = StateModeSelectEnum::IdtSelection;
			stateMode.StateModeSelectTarget = StateModeSelectEnum::IdtSelection;
			stateMode.StateModeSelectEcho = StateModeSelectEnum::IdtSelection;
		}
		// 显示器和设备属于当前环境，不能随旧工具快照退回旧 DPI。
		stateMode.penWidthContext = widthContext;
		ResolveWidthSlotsLocked();
		stateModeWorkspace = workspace;
		if (IsComputerInteractionMode(stateMode.StateModeSelect) && state == 1.1) state = 1;
		BackgroundColorMode = IsComputerInteractionMode(stateMode.StateModeSelect) ||
			stateMode.StateModeSelect == StateModeSelectEnum::IdtEraser ? 0 :
			(computeContrast(GetPenColor(stateMode), RGB(255, 255, 255)) >= 3 ? 0 : 1);
		const auto revision = stateModeTransitionRevision.load(std::memory_order_relaxed) + 1;
		if (exitSelectionPending && workspace == Workspace::Desktop &&
			IsComputerInteractionMode(stateMode.StateModeSelect))
			draw3ExitSelectionModeRevision.store(revision, std::memory_order_release);
		stateModeTransitionRevision.store(revision, std::memory_order_release);
		return true;
	}

	bool CommitWorkspaceHandoffLocked(std::uint64_t revision, bool targetReady,
		bool backgroundReady, bool nativeReady, bool restoreToolOwner)
	{
		// 调用方已持模式锁；安全退场必须在同一逻辑提交里迁移真实工具 owner。
		// Bridge 和当前完整故障身份继续保留给 Host 恢复，不能用 Sync 伪造目标已应用。
		std::scoped_lock lock(workspaceHandoffMutex);
		if (!WorkspaceRequestCurrent(revision) || !workspaceHandoff.pending || workspaceHandoff.failed ||
			workspaceHandoff.revision != revision || !targetReady || !backgroundReady || !nativeReady) return false;
		if (restoreToolOwner)
		{
			(void)RestoreWorkspaceStateModeLocked(workspaceHandoff.desired);
			(void)PublishSettingOwnerDesiredState(!IsComputerInteractionMode(stateMode.StateModeSelect));
		}
		if (!workspaceHandoff.Commit(revision, targetReady, backgroundReady, nativeReady)) return false;
		ProjectWorkspaceHandoffLocked();
		return true;
	}

	bool SwitchWorkspaceStateMode(Workspace workspace, bool primaryOnly = false,
		const std::uint64_t* exitRevision = nullptr, bool* exitSelected = nullptr,
		const std::function<bool()>& stillDesired = {}, std::uint64_t expectedPptWorkspaceEpoch = 0)
	{
		{
			// 意图、工具和场景提交按此顺序串行；窗口同步仍在两锁之外。
			std::scoped_lock workspaceLock(whiteboardReturnFocusMutex);
			std::scoped_lock lock(stateModeTransitionMutex);
			if ((stillDesired && !stillDesired()) ||
				(expectedPptWorkspaceEpoch != 0 && PptInputWorkspaceEpoch() != expectedPptWorkspaceEpoch))
				return false;
			// PPT 后台场次变化不能改写正在进入、使用或退出的白板工具。
			if ((primaryOnly && WhiteboardTransactionActive()) ||
				(exitRevision && CurrentPrimaryWorkspace() != Workspace::Desktop))
				return false;
			// 退出版本校验、原工具记忆与 Desktop 选择收尾必须是同一事务。
			const bool selectOnExit = exitRevision &&
				stateModeTransitionRevision.load(std::memory_order_relaxed) == *exitRevision;
			const bool changed = stateModeWorkspace != workspace || selectOnExit;
			(void)RestoreWorkspaceStateModeLocked(workspace);
			if (selectOnExit)
			{
				ApplyExitSelectionLocked();
				if (exitSelected) *exitSelected = true;
			}
			// 门控、工具恢复与工作区发布同锁完成，PPT 旧快照不能夹在白板交接中间。
			if (changed) PublishDraw3StateLocked();
			// Presentation 必须随完整 target 发布，先恢复工具不能制造无身份场景。
			if (workspace != Workspace::Presentation)
				Inkeys::Drawing::Draw3::PublishProductWorkspace(workspace, stillDesired);
			if (!changed) return false;
		}
		SyncDraw3State();
		// 恢复工具后必须走主栏 update，重新计算高亮、图标和粗细文字。
		Inkeys::UI::Bar::RefreshToolState();
		return true;
	}

    bool SetPenWidthSelectionImpl(Selection selection, bool setMemory,
        const std::uint64_t* expectedRevision)
    {
        if (!IsValidSelection(selection)) return false;
        std::unique_lock lock(stateModeTransitionMutex);
        if (expectedRevision && stateModeTransitionRevision.load(std::memory_order_relaxed) != *expectedRevision)
            return false;
        if (IsLaserToolActive(stateMode)) stateMode.Pen.Laser.widthSelection = selection;
        else if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen)
        {
            if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1)
                stateMode.Pen.Highlighter1.widthSelection = selection;
            else if (stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenSoftPen ||
                stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHardPen)
                stateMode.Pen.Brush1.widthSelection = selection;
            else return false;
        }
        else if (stateMode.StateModeSelect == StateModeSelectEnum::IdtShape)
        {
            if (stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeRectangle1)
                stateMode.Shape.Rectangle1.widthSelection = selection;
            else if (stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeStraightLine1 ||
                stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeDashedLine1)
                stateMode.Shape.StraightLine1.widthSelection = selection;
            else return false;
        }
        else return false;
        ResolveWidthSlotsLocked();
        stateModeTransitionRevision.fetch_add(1, std::memory_order_release);
        lock.unlock();
        // 磁盘和 bridge 副作用始终放在状态锁外。
        if (setMemory) SetMemory();
        PublishDraw3State();
        return true;
    }
}

void SyncPrimaryWorkspaceStateMode(std::uint64_t expectedWorkspaceEpoch)
{
	if (expectedWorkspaceEpoch == 0) expectedWorkspaceEpoch = PptSceneWorkspaceEpoch();
	if (expectedWorkspaceEpoch == 0 || PptSceneWorkspaceEpoch() != expectedWorkspaceEpoch) return;
	const auto before = ReadWorkspaceHandoff();
	if (before.desired == Workspace::Whiteboard) return;
	const auto request = RequestWorkspace(CurrentPrimaryWorkspace());
	if (!CloseWorkspaceAdmission(request) || request.revision != expectedWorkspaceEpoch) return;
	(void)SwitchWorkspaceStateMode(request.desired, false, nullptr, nullptr,
		[expectedWorkspaceEpoch]() noexcept { return PptSceneWorkspaceEpoch() == expectedWorkspaceEpoch; });
}

std::optional<std::uint64_t> PublishPrimaryPresentationTarget(
	const Inkeys::Drawing::Draw3::Bridge::PresentationTarget* target, std::uint64_t expectedWorkspaceEpoch)
{
	if (expectedWorkspaceEpoch == 0) expectedWorkspaceEpoch = PptSceneWorkspaceEpoch();
	const auto stillDesired = [expectedWorkspaceEpoch]() noexcept
	{ return expectedWorkspaceEpoch != 0 && PptSceneWorkspaceEpoch() == expectedWorkspaceEpoch; };
	const auto handoff = ReadWorkspaceHandoff();
	if (handoff.pending && !CloseWorkspaceAdmission(handoff)) return std::nullopt;
	std::scoped_lock workspaceLock(whiteboardReturnFocusMutex);
	std::scoped_lock lock(stateModeTransitionMutex);
	// Exiting 可以恢复场景；完整往返后的旧 epoch 仍不得发布或清除目标。
	if (!stillDesired() || stateModeWorkspace != Workspace::Presentation)
		return std::nullopt;
	if (target) return Inkeys::Drawing::Draw3::PublishProductPresentationTarget(*target, stillDesired);
	Inkeys::Drawing::Draw3::ClearProductPresentationTarget(stillDesired);
	return std::nullopt;
}

bool CompletePresentationExitStateMode(std::uint64_t expectedRevision, std::uint64_t expectedPptWorkspaceEpoch)
{
	if (expectedPptWorkspaceEpoch != 0 && PptInputWorkspaceEpoch() != expectedPptWorkspaceEpoch) return false;
	if (WhiteboardTransactionActive() || CurrentPrimaryWorkspace() != Workspace::Desktop) return false;
	const auto request = RequestWorkspace(Workspace::Desktop);
	if (!CloseWorkspaceAdmission(request)) return false;
	bool selected = false;
	(void)SwitchWorkspaceStateMode(Workspace::Desktop, true, &expectedRevision, &selected,
		[revision = request.revision]() noexcept { return WorkspaceRequestCurrent(revision); });
	return selected;
}

bool SetPenWidth(double targetWidth, bool setMemory)
{
    return SetPenWidthSelectionImpl({ Inkeys::Business::PenWidth::Source::ExplicitDip,
        Inkeys::Business::PenWidth::Preset::Medium, targetWidth }, setMemory, nullptr);
}
bool SetPenWidthIfRevision(double targetWidth, std::uint64_t expectedRevision, bool setMemory)
{
    return SetPenWidthSelectionImpl({ Inkeys::Business::PenWidth::Source::ExplicitDip,
        Inkeys::Business::PenWidth::Preset::Medium, targetWidth }, setMemory, &expectedRevision);
}
bool SetPenWidthPresetIfRevision(int index, std::uint64_t expectedRevision, bool setMemory)
{
    if (index < 0 || index > 2) return false;
    return SetPenWidthSelectionImpl({ Inkeys::Business::PenWidth::Source::Preset,
        static_cast<Inkeys::Business::PenWidth::Preset>(index), 3.0 }, setMemory, &expectedRevision);
}
Inkeys::Business::PenWidth::Selection GetPenWidthSelection(const StateModeClass& snapshot) noexcept
{
    if (IsLaserToolActive(snapshot)) return snapshot.Pen.Laser.widthSelection;
    if (snapshot.StateModeSelect == StateModeSelectEnum::IdtShape)
        return snapshot.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeRectangle1
            ? snapshot.Shape.Rectangle1.widthSelection : snapshot.Shape.StraightLine1.widthSelection;
    return snapshot.StateModeSelect == StateModeSelectEnum::IdtPen &&
        snapshot.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1
        ? snapshot.Pen.Highlighter1.widthSelection : snapshot.Pen.Brush1.widthSelection;
}
Inkeys::Business::PenWidth::Family GetPenWidthFamily(const StateModeClass& snapshot) noexcept
{
    using namespace Inkeys::Business::PenWidth;
    if (IsLaserToolActive(snapshot)) return Family::Laser;
    return snapshot.StateModeSelect == StateModeSelectEnum::IdtPen &&
        snapshot.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1 ? Family::Highlighter : Family::Brush;
}
Inkeys::Business::PenWidth::Metrics GetPenWidthMetrics(const StateModeClass& snapshot) noexcept
{
    return Inkeys::Business::PenWidth::Resolve(GetPenWidthSelection(snapshot),
        GetPenWidthFamily(snapshot), snapshot.penWidthContext);
}
Inkeys::Business::PenWidth::Context GetPenWidthContext()
{
    return GetStateModeSnapshot().penWidthContext;
}
bool ApplyPenWidthContext(Inkeys::Business::PenWidth::Context context)
{
    {
        std::scoped_lock lock(stateModeTransitionMutex);
        if (Inkeys::Business::PenWidth::EquivalentContext(context, stateMode.penWidthContext)) return false;
        stateMode.penWidthContext = context;
        ResolveWidthSlotsLocked();
        // 显示/设备变化也使旧滑条手势失效，但不写盘或重置显式 DIP。
        stateModeTransitionRevision.fetch_add(1, std::memory_order_release);
    }
    PublishDraw3State();
    return true;
}
void RestorePenWidthSelections(Inkeys::Business::PenWidth::Selection brush,
    Inkeys::Business::PenWidth::Selection highlighter, Inkeys::Business::PenWidth::Selection laser,
    const COLORREF* brushColor, const COLORREF* highlighterColor)
{
    {
        std::scoped_lock lock(stateModeTransitionMutex);
        using namespace Inkeys::Business::PenWidth;
        if (!IsValidSelection(brush)) brush = {};
        if (!IsValidSelection(highlighter)) highlighter = {};
        if (!IsValidSelection(laser)) laser = {};
        stateMode.Pen.Brush1.widthSelection = brush;
        stateMode.Pen.Highlighter1.widthSelection = highlighter;
        stateMode.Pen.Laser.widthSelection = laser;
        if (brushColor) stateMode.Pen.Brush1.color = *brushColor;
        if (highlighterColor) stateMode.Pen.Highlighter1.color = *highlighterColor;
        ResolveWidthSlotsLocked();
        stateModeTransitionRevision.fetch_add(1, std::memory_order_release);
    }
    PublishDraw3State();
}

bool SetPenColor(COLORREF targetColor, bool setMemory)
{
	std::unique_lock lock(stateModeTransitionMutex);
	if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen)
	{
		const auto colorSlot = Inkeys::Business::ResolvePenColorStateSlot(
			IsLaserToolActive(stateMode),
			stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1);
		if (colorSlot == Inkeys::Business::PenColorStateSlot::Laser)
			stateMode.Pen.Laser.color = targetColor;
		else if (colorSlot == Inkeys::Business::PenColorStateSlot::Brush)
			stateMode.Pen.Brush1.color = targetColor;
		else if (colorSlot == Inkeys::Business::PenColorStateSlot::Highlighter)
			stateMode.Pen.Highlighter1.color = targetColor;
		else
			return false;
	}
	else if (stateMode.StateModeSelect == StateModeSelectEnum::IdtShape)
	{
		if (stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeStraightLine1 ||
			stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeDashedLine1)
			stateMode.Shape.StraightLine1.color = targetColor;
		else if (stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeRectangle1)
			stateMode.Shape.Rectangle1.color = targetColor;
		else
			return false;
	}
	else
		return false;

	BackgroundColorMode = computeContrast(targetColor, RGB(255, 255, 255)) >= 3 ? 0 : 1;
	lock.unlock();
	if (setMemory) SetMemory();
	PublishDraw3State();
	return true;
}

float GetPenWidth()
{
	return GetPenWidth(GetStateModeSnapshot());
}

float GetPenWidth(const StateModeClass& snapshot) noexcept
{
	if (IsLaserToolActive(snapshot)) return snapshot.Pen.Laser.width;
	if (snapshot.StateModeSelect == StateModeSelectEnum::IdtPen)
	{
		return snapshot.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1
			? snapshot.Pen.Highlighter1.width
			: snapshot.Pen.Brush1.width;
	}
	if (snapshot.StateModeSelect == StateModeSelectEnum::IdtShape)
	{
		return snapshot.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeRectangle1
			? snapshot.Shape.Rectangle1.width
			: snapshot.Shape.StraightLine1.width;
	}
	return snapshot.Pen.Brush1.width;
}

COLORREF GetPenColor()
{
	return GetPenColor(GetStateModeSnapshot());
}

COLORREF GetPenColor(const StateModeClass& snapshot) noexcept
{
	if (snapshot.StateModeSelect == StateModeSelectEnum::IdtPen)
	{
		const auto colorSlot = Inkeys::Business::ResolvePenColorStateSlot(
			IsLaserToolActive(snapshot),
			snapshot.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1);
		if (colorSlot == Inkeys::Business::PenColorStateSlot::Laser)
			return snapshot.Pen.Laser.color;
		return colorSlot == Inkeys::Business::PenColorStateSlot::Highlighter
			? snapshot.Pen.Highlighter1.color : snapshot.Pen.Brush1.color;
	}
	if (snapshot.StateModeSelect == StateModeSelectEnum::IdtShape)
	{
		return snapshot.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeRectangle1
			? snapshot.Shape.Rectangle1.color
			: snapshot.Shape.StraightLine1.color;
	}
	return snapshot.Pen.Brush1.color;
}

float GetEffectivePenOpacity()
{
	return GetEffectivePenOpacity(GetStateModeSnapshot());
}

float GetEffectivePenOpacity(const StateModeClass& snapshot) noexcept
{
	// Laser 不复用已记忆的 Pen.ModeSelect；当前只有荧光笔使用固定半透明合成。
	return !IsLaserToolActive(snapshot) &&
		snapshot.StateModeSelect == StateModeSelectEnum::IdtPen &&
		snapshot.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1
		? Inkeys::Drawing::Draw3::Bridge::kHighlighterCompositeOpacity : 1.0f;
}

namespace
{
	void ApplySelectionTransitionLocked()
	{
		stateMode.StateModeSelectTarget = StateModeSelectEnum::IdtSelection;
		if (state == 1.1) state = 1;
		stateMode.StateModeSelect = StateModeSelectEnum::IdtSelection;
		stateMode.StateModeSelectEcho = StateModeSelectEnum::IdtSelection;
		BackgroundColorMode = 0;
		stateModeTransitionRevision.fetch_add(1, std::memory_order_release);
	}

	template<class ReadContent>
	bool SetSelectAndClickThroughModeImpl(int mode, ReadContent readContent)
	{
		mode = mode == 1 ? 1 : 0;
		{
			std::scoped_lock lock(stateModeTransitionMutex);
			if (GetSelectAndClickThroughMode() == mode) return false;
			Inkeys::config.UI.Bar.SelectAndClickThroughMode = mode;
			// 内容由 Host 独立发布；等模式锁之后再采样，避免使用排队前的旧值。
			// 设置变更只改工具身份，不发布 Clear，因此已有墨迹和历史不会丢失。
			if (mode == 1 && !WhiteboardActive() && !WhiteboardRequested() &&
				stateMode.StateModeSelect == StateModeSelectEnum::IdtSelection && readContent())
				stateMode.StateModeSelect = StateModeSelectEnum::IdtClickThrough;
			else if (mode == 0 && stateMode.StateModeSelect == StateModeSelectEnum::IdtClickThrough)
				stateMode.StateModeSelect = StateModeSelectEnum::IdtSelection;
			stateMode.StateModeSelectTarget = stateMode.StateModeSelect;
			stateMode.StateModeSelectEcho = stateMode.StateModeSelect;
			stateModeTransitionRevision.fetch_add(1, std::memory_order_release);
		}
		if (GetStateModeSnapshot().StateModeSelect == StateModeSelectEnum::IdtClickThrough)
			(void)Inkeys::UI::Freeze::DeactivateIfRevision(Inkeys::UI::Freeze::Snapshot().revision);
		SyncDraw3State();
		Inkeys::UI::Bar::RefreshToolState();
		return true;
	}

	template<class PublishClear>
	bool ChangeStateModeToSelectionFromButtonImpl(bool hasContent, PublishClear publishClear)
	{
		const auto expected = GetStateModeVersionedSnapshot();
		if (GetSelectAndClickThroughMode() == 1 && !WhiteboardActive() && hasContent)
		{
			// 必须先进入 Draw3 FIFO；拒绝时保留原工具，不能假装已经清空。
			if (publishClear() != Inkeys::Drawing::Draw3::Bridge::CommandResult::Accepted)
				return false;
		}
		{
			std::scoped_lock lock(stateModeTransitionMutex);
			// Clear 发布期间若用户已选新工具，旧请求不得再覆盖新意图。
			if (stateModeTransitionRevision.load(std::memory_order_relaxed) != expected.revision)
				return false;
			ApplySelectionTransitionLocked();
		}
		SyncDraw3State();
		return true;
	}
}

int GetSelectAndClickThroughMode() noexcept
{
	return Inkeys::config.UI.Bar.SelectAndClickThroughMode.load() == 1 ? 1 : 0;
}

bool SetSelectAndClickThroughMode(int mode)
{
	return SetSelectAndClickThroughModeImpl(mode, []
	{
		return Inkeys::Drawing::Draw3::ProductRuntimeSnapshot().currentPageHasContent;
	});
}

bool ChangeStateModeToSelectionFromButton()
{
	return ChangeStateModeToSelectionFromButtonImpl(
		Inkeys::Drawing::Draw3::ProductRuntimeSnapshot().currentPageHasContent, []
		{
			return Inkeys::Drawing::Draw3::PublishProductCommand(
				Inkeys::Drawing::Draw3::Bridge::CommandType::Clear);
		});
}

static bool ChangeStateModeToClickThroughImpl(bool toggle)
{
	{
		std::scoped_lock lock(stateModeTransitionMutex);
		if (GetSelectAndClickThroughMode() != 1 || WhiteboardActive() || WhiteboardRequested() ||
			WhiteboardTransactionActive()) return false;
		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtClickThrough)
		{
			if (!toggle) return false;
			// 再点按钮才恢复记忆工具；锁内决定方向，避免旧快照覆盖较新的用户选择。
			stateMode.StateModeSelect = stateMode.lastAnnotationMode;
			BackgroundColorMode = stateMode.StateModeSelect == StateModeSelectEnum::IdtEraser ? 0 :
				(computeContrast(GetPenColor(stateMode), RGB(255, 255, 255)) >= 3 ? 0 : 1);
		}
		else
		{
			stateMode.StateModeSelect = StateModeSelectEnum::IdtClickThrough;
			BackgroundColorMode = 0;
		}
		stateMode.StateModeSelectTarget = stateMode.StateModeSelect;
		stateMode.StateModeSelectEcho = stateMode.StateModeSelect;
		stateModeTransitionRevision.fetch_add(1, std::memory_order_release);
	}
	// 退出也保持定格关闭，不恢复进入穿透前的定格状态。
	(void)Inkeys::UI::Freeze::DeactivateIfRevision(Inkeys::UI::Freeze::Snapshot().revision);
	SyncDraw3State();
	return true;
}

bool ChangeStateModeToClickThrough()
{
	return ChangeStateModeToClickThroughImpl(false);
}

bool ToggleClickThroughFromButton()
{
	return ChangeStateModeToClickThroughImpl(true);
}

bool ToggleFreezeFromButton()
{
	if (!Inkeys::UI::Freeze::IsAvailable()) return false;
	bool restored = false;
	{
		std::scoped_lock lock(stateModeTransitionMutex);
		if (stateMode.StateModeSelect == StateModeSelectEnum::IdtClickThrough)
		{
			stateMode.StateModeSelect = stateMode.lastAnnotationMode;
			stateMode.StateModeSelectTarget = stateMode.StateModeSelect;
			stateMode.StateModeSelectEcho = stateMode.StateModeSelect;
			BackgroundColorMode = stateMode.StateModeSelect == StateModeSelectEnum::IdtEraser ? 0 :
				(computeContrast(GetPenColor(stateMode), RGB(255, 255, 255)) >= 3 ? 0 : 1);
			stateModeTransitionRevision.fetch_add(1, std::memory_order_release);
			restored = true;
		}
	}
	if (restored) SyncDraw3State();
	const bool wasActive = Inkeys::UI::Freeze::IsActive();
	Inkeys::UI::Freeze::Toggle();
	const bool isActive = Inkeys::UI::Freeze::IsActive();
	if (!wasActive && isActive)
		(void)Inkeys::Window::GetService().RequestTopmostRefreshAsync();
	// 按下后工作区可能使冻结不可用，仅为真正生效的按钮动作确认反馈。
	return wasActive != isActive;
}

bool ChangeStateModeToSelection()
{
	{
		std::scoped_lock lock(stateModeTransitionMutex);
		ApplySelectionTransitionLocked();
	}
	SyncDraw3State();
	return true;
}

std::uint64_t StateModeTransitionRevision() noexcept
{
	return stateModeTransitionRevision.load(std::memory_order_acquire);
}

bool ChangeStateModeForShortcutIfRevision(bool restoreAnnotation,
	std::uint64_t expectedRevision, std::uint64_t freezeRevision)
{
	{
		std::scoped_lock lock(stateModeTransitionMutex);
		if (stateModeTransitionRevision.load(std::memory_order_acquire) != expectedRevision ||
			WhiteboardTransactionActive()) return false;
		if (restoreAnnotation)
		{
			if (!IsComputerInteractionMode(stateMode.StateModeSelect)) return false;
			auto mode = stateMode.lastAnnotationMode;
			if (mode != StateModeSelectEnum::IdtPen && mode != StateModeSelectEnum::IdtShape &&
				mode != StateModeSelectEnum::IdtEraser) mode = StateModeSelectEnum::IdtPen;
			// 只恢复工具身份，Pen/Shape 子型、激光及宽色记忆均保持原值。
			stateMode.StateModeSelect = mode;
			stateMode.StateModeSelectTarget = mode;
			stateMode.StateModeSelectEcho = mode;
			BackgroundColorMode = mode == StateModeSelectEnum::IdtEraser ? 0 :
				(computeContrast(GetPenColor(stateMode), RGB(255, 255, 255)) >= 3 ? 0 : 1);
			stateModeTransitionRevision.fetch_add(1, std::memory_order_release);
		}
		else ApplySelectionTransitionLocked();
	}
	// 不覆盖等待清空期间由其它意图建立的新定格。
	(void)Inkeys::UI::Freeze::DeactivateIfRevision(freezeRevision);
	SyncDraw3State();
	Inkeys::UI::Bar::RefreshToolState();
	return true;
}

bool ToggleFreezeForShortcutIfRevision(std::uint64_t expectedRevision)
{
	const auto before = GetStateModeVersionedSnapshot();
	if (before.revision != expectedRevision ||
		IsComputerInteractionMode(before.state.StateModeSelect) ||
		!Inkeys::UI::Freeze::IsAvailable()) return false;
	Inkeys::UI::Freeze::Toggle();
	const auto toggled = Inkeys::UI::Freeze::Snapshot();
	if (StateModeTransitionRevision() != expectedRevision)
	{
		(void)Inkeys::UI::Freeze::DeactivateIfRevision(toggled.revision);
		return false;
	}
	if (toggled.active) (void)Inkeys::Window::GetService().RequestTopmostRefreshAsync();
	return true;
}

bool ChangeStateModeToSelectionIfRevision(std::uint64_t expectedRevision,
	std::uint64_t expectedPptWorkspaceEpoch)
{
	{
		// 与白板新意图共锁确认业务权限；只锁短状态提交，不能跨锁调用窗口/产品同步。
		std::unique_lock workspaceLock(whiteboardReturnFocusMutex, std::defer_lock);
		if (expectedPptWorkspaceEpoch != 0) workspaceLock.lock();
		std::scoped_lock lock(stateModeTransitionMutex);
		if ((expectedPptWorkspaceEpoch != 0 && PptInputWorkspaceEpoch() != expectedPptWorkspaceEpoch) ||
			stateModeTransitionRevision.load(std::memory_order_acquire) != expectedRevision)
			return false;
		ApplyExitSelectionLocked();
	}
	SyncDraw3State();
	return true;
}

bool ChangeStateModeToPen()
{
	{
		std::scoped_lock lock(stateModeTransitionMutex);
		ApplyPenModeTransitionLocked();
	}
	SyncDraw3State();
	return true;
}

bool PenToolSelectionWouldChange(PenToolSelectionEnum tool)
{
	PenModeSelectEnum penMode = PenModeSelectEnum::IdtPenSoftPen;
	if (!ResolvePenToolMode(tool, penMode)) return false;
	std::scoped_lock lock(stateModeTransitionMutex);
	return PenToolSelectionWouldChangeLocked(tool, penMode);
}

bool ChangeStateModeToPenTool(PenToolSelectionEnum tool)
{
	PenModeSelectEnum penMode = PenModeSelectEnum::IdtPenSoftPen;
	if (!ResolvePenToolMode(tool, penMode)) return false;
	{
		std::scoped_lock lock(stateModeTransitionMutex);
		const bool laser = tool == PenToolSelectionEnum::Laser;
		if (!PenToolSelectionWouldChangeLocked(tool, penMode))
		{
			// 重复点击仍是较新的用户意图，但没有视觉变化时不重复发布 Draw3 命令。
			stateModeTransitionRevision.fetch_add(1, std::memory_order_release);
			return false;
		}
		stateMode.laserActive = laser;
		if (!laser) stateMode.Pen.ModeSelect = penMode;
		ApplyPenModeTransitionLocked();
	}
	SyncDraw3State();
	return true;
}

bool ChangeStateModeToPptAnnotation(PptAnnotationTool tool, std::uint64_t expectedRevision)
{
	if (tool != PptAnnotationTool::Pen && tool != PptAnnotationTool::Laser &&
		tool != PptAnnotationTool::Highlighter) return false;
	{
		std::scoped_lock lock(stateModeTransitionMutex);
		if (stateModeTransitionRevision.load(std::memory_order_acquire) != expectedRevision ||
			WhiteboardTransactionActive() || stateModeWorkspace == Workspace::Whiteboard)
			return false;
		// PPT 接管的笔型和模式在同一次修订中发布，Draw3 不会读到旧工具。
		stateMode.laserActive = tool == PptAnnotationTool::Laser;
		if (tool == PptAnnotationTool::Pen)
			stateMode.Pen.ModeSelect = PenModeSelectEnum::IdtPenBrush1;
		else if (tool == PptAnnotationTool::Highlighter)
			stateMode.Pen.ModeSelect = PenModeSelectEnum::IdtPenHighlighter1;
		ApplyPenModeTransitionLocked();
	}
	SyncDraw3State();
	return true;
}

bool ChangeStateModeToShape()
{
	{
		std::scoped_lock lock(stateModeTransitionMutex);
		stateMode.StateModeSelectTarget = StateModeSelectEnum::IdtShape;
		stateMode.StateModeSelect = StateModeSelectEnum::IdtShape;
		stateMode.StateModeSelectEcho = StateModeSelectEnum::IdtShape;
		stateMode.lastAnnotationMode = StateModeSelectEnum::IdtShape;
		BackgroundColorMode = computeContrast(GetPenColor(stateMode), RGB(255, 255, 255)) >= 3 ? 0 : 1;
		stateModeTransitionRevision.fetch_add(1, std::memory_order_release);
	}
	SyncDraw3State();
	return true;
}

static bool SetShapeModeSelectImpl(ShapeModeSelectEnum shape,
	const std::uint64_t* expectedRevision)
{
	if (shape != ShapeModeSelectEnum::IdtShapeStraightLine1 &&
		shape != ShapeModeSelectEnum::IdtShapeDashedLine1 &&
		shape != ShapeModeSelectEnum::IdtShapeRectangle1)
		return false;
	{
		std::scoped_lock lock(stateModeTransitionMutex);
		if (expectedRevision && stateModeTransitionRevision.load(
			std::memory_order_relaxed) != *expectedRevision)
			return false;
		// 同形状重复点击只更新用户意图版本，旧 PPT 结果仍不能回写。
		stateModeTransitionRevision.fetch_add(1, std::memory_order_release);
		if (stateMode.Shape.ModeSelect == shape) return false;
		stateMode.Shape.ModeSelect = shape;
	}
	SyncDraw3State();
	return true;
}

bool SetShapeModeSelect(ShapeModeSelectEnum shape)
{
	return SetShapeModeSelectImpl(shape, nullptr);
}

bool SetShapeModeSelectIfRevision(ShapeModeSelectEnum shape,
	std::uint64_t expectedRevision)
{
	return SetShapeModeSelectImpl(shape, &expectedRevision);
}

bool ChangeStateModeToEraser()
{
	{
		std::scoped_lock lock(stateModeTransitionMutex);
		stateMode.StateModeSelectTarget = StateModeSelectEnum::IdtEraser;
		stateMode.StateModeSelect = StateModeSelectEnum::IdtEraser;
		stateMode.StateModeSelectEcho = StateModeSelectEnum::IdtEraser;
		stateMode.lastAnnotationMode = StateModeSelectEnum::IdtEraser;
		BackgroundColorMode = 0;
		stateModeTransitionRevision.fetch_add(1, std::memory_order_release);
	}
	SyncDraw3State();
	return true;
}

bool ChangeStateModeToTouchTest()
{
	// 输入测试尚未接入 Draw3，入口由设置页隐藏。
	return false;
}

struct Inkeys::Testing::WorkspaceSceneScope::Impl
{
	WorkspaceHandoff coordinator = ReadWorkspaceHandoff();
	StateModeClass mode;
	Workspace workspace;
	std::array<std::optional<StateModeClass>, 3> memories;
	std::uint64_t modeRevision;
	std::uint64_t exitRevision;
	ProductState bridge;
	Impl()
	{
		std::scoped_lock lock(stateModeTransitionMutex);
		mode = stateMode; workspace = stateModeWorkspace; memories = workspaceStateModes;
		modeRevision = stateModeTransitionRevision.load(std::memory_order_acquire);
		exitRevision = draw3ExitSelectionModeRevision.load(std::memory_order_acquire);
		bridge = Inkeys::Drawing::Draw3::ProductHost().ProductBridge().Snapshot();
	}
};

Inkeys::Testing::WorkspaceSceneScope::WorkspaceSceneScope(WorkspaceScene scene)
{
	if (Inkeys::Drawing::Draw3::ProductRunning() || Inkeys::Window::GetService().Running()) return;
	impl_ = std::make_unique<Impl>();
	(void)Set(scene);
}

Inkeys::Testing::WorkspaceSceneScope::~WorkspaceSceneScope()
{
	if (!impl_ || Inkeys::Drawing::Draw3::ProductRunning() || Inkeys::Window::GetService().Running()) return;
	{
		std::scoped_lock lock(stateModeTransitionMutex);
		stateMode = impl_->mode; stateModeWorkspace = impl_->workspace; workspaceStateModes = impl_->memories;
		stateModeTransitionRevision.store(impl_->modeRevision, std::memory_order_release);
		draw3ExitSelectionModeRevision.store(impl_->exitRevision, std::memory_order_release);
	}
	{ std::scoped_lock lock(workspaceHandoffMutex); workspaceHandoff = impl_->coordinator; ProjectWorkspaceHandoffLocked(); }
	Inkeys::Drawing::Draw3::ProductHost().ProductBridge().PublishState(impl_->bridge);
}

Inkeys::Testing::WorkspaceSceneScope::operator bool() const noexcept { return impl_ != nullptr; }

bool Inkeys::Testing::WorkspaceSceneScope::Set(WorkspaceScene scene)
{
	if (!impl_ || Inkeys::Drawing::Draw3::ProductRunning() || Inkeys::Window::GetService().Running()) return false;
	const auto target = scene == WorkspaceScene::Whiteboard ? Workspace::Whiteboard :
		scene == WorkspaceScene::Presentation ? Workspace::Presentation : Workspace::Desktop;
	const auto request = RequestWorkspace(target);
	(void)SwitchWorkspaceStateMode(target);
	{
		std::scoped_lock lock(workspaceHandoffMutex);
		// 这是明确无 HWND/Host 的 fixture 布置，不作为真实窗口或帧验证的成功凭据。
		if (workspaceHandoff.pending) (void)workspaceHandoff.Commit(request.revision, true, true, true);
		ProjectWorkspaceHandoffLocked();
	}
	return true;
}

int RunStateModeProductionTest() noexcept
{
	const char* stage = "startup-isolation";
	try
	{
		using namespace Inkeys::Drawing::Draw3;
		// 此入口只属于独立的早期测试进程；未启动的 owner 自然拒绝 HWND 副作用。
		if (ProductRunning() || Inkeys::Window::GetService().Running()) return 2;
		auto& bridge = ProductHost().ProductBridge();
		const auto require = [](bool passed, const char* check)
		{
			if (!passed) throw check;
		};
		const auto setProbePhase = [](WhiteboardPhase phase)
		{
			// 无窗口工具/焦点用例只布置 canonical fixture；物理完成由下列真实协调 helper 另测。
			std::scoped_lock lock(workspaceHandoffMutex);
			const bool whiteboard = phase == WhiteboardPhase::Entering || phase == WhiteboardPhase::Active;
			const auto target = whiteboard ? Workspace::Whiteboard : CurrentPrimaryWorkspace();
			workspaceHandoff.Request(target);
			if (phase == WhiteboardPhase::Entering || phase == WhiteboardPhase::Exiting)
			{
				workspaceHandoff.pending = true;
				workspaceHandoff.whiteboardInvolved = true;
			}
			else
			{
				workspaceHandoff.committed = target;
				workspaceHandoff.pending = false;
				workspaceHandoff.whiteboardInvolved = whiteboard;
			}
			ProjectWorkspaceHandoffLocked();
		};
		const auto sameState = [](const StateModeClass& a, const StateModeClass& b)
		{
			return a.StateModeSelect == b.StateModeSelect &&
				a.StateModeSelectTarget == b.StateModeSelectTarget &&
				a.StateModeSelectEcho == b.StateModeSelectEcho &&
				a.lastAnnotationMode == b.lastAnnotationMode &&
				a.laserActive == b.laserActive && a.Pen.ModeSelect == b.Pen.ModeSelect &&
				a.Pen.Brush1.width == b.Pen.Brush1.width && a.Pen.Brush1.color == b.Pen.Brush1.color &&
				a.Pen.Highlighter1.width == b.Pen.Highlighter1.width && a.Pen.Highlighter1.color == b.Pen.Highlighter1.color &&
				a.Pen.Laser.width == b.Pen.Laser.width && a.Pen.Laser.color == b.Pen.Laser.color &&
				a.Pen.Brush1.widthPreset == b.Pen.Brush1.widthPreset &&
				a.Pen.Highlighter1.widthPreset == b.Pen.Highlighter1.widthPreset &&
				a.Pen.Laser.widthPreset[0] == b.Pen.Laser.widthPreset[0] &&
				a.Pen.Laser.widthPreset[1] == b.Pen.Laser.widthPreset[1] &&
				a.Pen.Laser.widthPreset[2] == b.Pen.Laser.widthPreset[2] &&
				a.Shape.ModeSelect == b.Shape.ModeSelect &&
				a.Shape.StraightLine1.width == b.Shape.StraightLine1.width && a.Shape.StraightLine1.color == b.Shape.StraightLine1.color &&
				a.Shape.Rectangle1.width == b.Shape.Rectangle1.width && a.Shape.Rectangle1.color == b.Shape.Rectangle1.color;
		};
		const auto sameProjection = [](const Bridge::ProductState& a, const Bridge::ProductState& b)
		{
			return a.tool == b.tool && a.colorRgba == b.colorRgba && a.widthDip == b.widthDip &&
				a.paintDevice == b.paintDevice && a.eraserInputs == b.eraserInputs && a.cursorSettings == b.cursorSettings &&
				a.selectionMode == b.selectionMode && a.autoSaveEnabled == b.autoSaveEnabled &&
				a.page == b.page && a.hasPage == b.hasPage && a.workspace == b.workspace &&
				a.presentationTarget == b.presentationTarget;
		};
		const auto checkProjection = [&](Bridge::Tool tool)
		{
			const auto snapshot = GetStateModeVersionedSnapshot();
			const auto published = bridge.Snapshot();
			require(published.tool == tool, "bridge-tool");
			require(published.widthDip == GetPenWidthMetrics(snapshot.state).resolvedWidthDip, "bridge-width");
			require(published.colorRgba == ColorRefToRgba(GetPenColor(snapshot.state)), "bridge-color");
			require(published.selectionMode == IsComputerInteractionMode(snapshot.state.StateModeSelect), "bridge-selection");
			require(snapshot.state.StateModeSelect == snapshot.state.StateModeSelectTarget &&
				snapshot.state.StateModeSelect == snapshot.state.StateModeSelectEcho, "mode-projection");
		};

		stage = "workspace-handoff-production";
		{
			for (const auto source : { Workspace::Desktop, Workspace::Presentation, Workspace::Whiteboard })
			for (const auto target : { Workspace::Desktop, Workspace::Presentation, Workspace::Whiteboard })
			{
				WorkspaceHandoff handoff;
				handoff.desired = handoff.committed = source;
				handoff.revision = source == Workspace::Whiteboard ? 3 : 2;
				const auto old = handoff.revision;
				const bool changed = handoff.Request(target);
				require(changed == (source != target), "three-workspace-directed-and-same-target-requests");
				if (!changed) { require(!handoff.pending && handoff.revision == old, "same-target-no-new-handoff"); continue; }
				const auto current = handoff.revision;
				require(handoff.committed == source && handoff.pending && !handoff.Request(target) && handoff.revision == current,
					"pending-keeps-source-and-repeated-request-is-idempotent");
				require(!handoff.Commit(old, true, true, true) && !handoff.Commit(current, false, true, true) &&
					!handoff.Commit(current, true, false, true) && !handoff.Commit(current, true, true, false),
					"old-ack-or-missing-real-owner-receipt-cannot-commit");
				require(handoff.Commit(current, true, true, true) && handoff.committed == target && !handoff.pending &&
					!handoff.Commit(current, true, true, true), "complete-once-without-next-input");
			}
			WorkspaceHandoff cancel;
			cancel.Request(Workspace::Whiteboard); const auto entering = cancel.revision;
			cancel.Request(Workspace::Desktop);
			require(cancel.desired == cancel.committed && cancel.pending && cancel.whiteboardInvolved &&
				!cancel.Commit(entering, true, true, true), "cancel-to-source-retains-old-native-obligation");
			require(cancel.Commit(cancel.revision, true, true, true), "cancel-retires-only-after-real-completion");
			WorkspaceHandoff view;
			view.Request(Workspace::Whiteboard); view.Commit(view.revision, true, true, true);
			view.Request(Workspace::Whiteboard, true); const auto hidden = view.revision;
			require((hidden & 1U) && view.Commit(hidden, true, true, true) && view.committedMinimized && !view.pending,
				"minimized-remains-whiteboard-and-keeps-current-page");
			view.Request(Workspace::Whiteboard, false);
			require(view.revision != hidden && (view.revision & 1U) && !view.Commit(hidden, true, true, true) && view.activate,
				"restore-gets-new-view-version-and-rejects-old-hide");
			for (const auto target : { Workspace::Whiteboard, Workspace::Desktop })
			{
				WorkspaceHandoff failed;
				if (target == Workspace::Desktop) { failed.Request(Workspace::Whiteboard); failed.Commit(failed.revision, true, true, true); }
				failed.Request(target); const auto ticket = failed.revision;
				auto now = WorkspaceHandoff::Clock::time_point{};
				for (unsigned attempt = 0; attempt != 4; ++attempt)
				{
					require(failed.Ready(now), "entry-and-exit-have-finite-native-attempts");
					failed.Fail(ticket, now);
					if (attempt != 3) require(!failed.Ready(now + std::chrono::milliseconds(249)), "native-backoff-ignores-early-runtime-wake");
					now += std::chrono::seconds(2);
				}
				require(failed.failed && failed.pending && !failed.Ready(now) && !failed.Commit(ticket, true, true, true),
					"budget-exhaustion-retains-responsibility-without-repeating-native-show-hide");
				require(!failed.Retry(ticket - 2) && failed.Retry(ticket) && failed.revision != ticket && !failed.failed,
					"only-current-failure-token-retries-as-new-request");
			}
			WorkspaceHandoff ink; ink.Request(Workspace::Whiteboard); ink.hostGeneration = 7;
			ProductState inkDesired; inkDesired.workspace = Workspace::Whiteboard; inkDesired.revision = 12;
			inkDesired.selectionMode = false; inkDesired.tool = Tool::HardPen;
			HostRuntimeSnapshot inkReady; inkReady.running = true; inkReady.hostGeneration = 7; inkReady.workspace = Workspace::Whiteboard;
			inkReady.selectionMode = false; inkReady.firstFrameReady = true;
			inkReady.committedWidth = 800; inkReady.committedHeight = 600;
			inkReady.appliedProductRevision = 12; inkReady.appliedTool = Tool::HardPen;
			inkReady.requestedOutputRevision = inkReady.readyOutputRevision = 8;
			inkReady.contentRevision = inkReady.presentedContentRevision = 5;
			const RECT inkOutput{0, 0, 800, 600};
			require(WorkspaceTargetReady(ink, inkDesired, inkReady, false, false, inkOutput), "actual-tool-frame-and-host-ready-without-input");
			inkReady.appliedTool = Tool::Pen;
			require(!WorkspaceTargetReady(ink, inkDesired, inkReady, false, false, inkOutput), "bridge-tool-does-not-stand-in-for-owner-applied-tool");
			inkReady.appliedTool = Tool::HardPen; --inkReady.appliedProductRevision;
			require(!WorkspaceTargetReady(ink, inkDesired, inkReady, false, false, inkOutput), "old-applied-product-revision-rejected");
			++inkReady.appliedProductRevision; ++inkReady.hostGeneration;
			require(!WorkspaceTargetReady(ink, inkDesired, inkReady, false, false, inkOutput), "old-host-run-frame-rejected");
			--inkReady.hostGeneration; --inkReady.presentedContentRevision;
			require(!WorkspaceTargetReady(ink, inkDesired, inkReady, false, false, inkOutput), "terminal-contact-content-needs-current-present");
			++inkReady.presentedContentRevision;
			ink.desired = Workspace::Presentation; inkDesired.workspace = inkReady.workspace = Workspace::Presentation;
			auto inkTarget = std::make_shared<Bridge::PresentationTarget>();
			inkTarget->sessionRevision = 2;
			inkTarget->targetRevision = 3;
			inkDesired.presentationTarget = std::move(inkTarget);
			inkReady.presentationReady = Bridge::ReadyIdentityFor(*inkDesired.presentationTarget);
			require(!WorkspaceTargetReady(ink, inkDesired, inkReady, false, false, inkOutput), "ppt-pending-prepares-ui-before-committed-scene");
			inkReady.presentationUiReady = inkReady.presentationReady;
			require(WorkspaceTargetReady(ink, inkDesired, inkReady, false, false, inkOutput), "ppt-real-ui-ready-completes-without-scene-wait-cycle");
			WorkspaceHandoff controls; controls.Request(Workspace::Presentation);
			ProductState desired; desired.workspace = Workspace::Presentation;
			HostRuntimeSnapshot unavailable; unavailable.running = true;
			const RECT output{0, 0, 800, 600};
			require(!WorkspaceTargetReady(controls, desired, unavailable, false, false, output) &&
				WorkspaceTargetReady(controls, desired, unavailable, true, false, output) &&
				!controls.Commit(controls.revision, true, true, false),
				"basic-control-needs-real-hidden-native-ack-not-nonexistent-inkready");
			unavailable.consumerFatal = true; unavailable.hostGeneration = 1;
			unavailable.recovery.revision = 1;
			unavailable.recovery.workspace = desired.workspace;
			unavailable.recovery.status = CanvasRecoveryStatus::Failed;
			unavailable.recovery.reason = CanvasRecoveryReason::ConsumerFatal;
			require(WorkspaceTargetReady(controls, desired, unavailable, false, true, output), "matching-fatal-may-safe-exit-without-running-frame");
			unavailable.recovery.workspace = Workspace::Whiteboard;
			require(!WorkspaceTargetReady(controls, desired, unavailable, false, true, output), "stale-fault-never-authorizes-safe-exit");
			Inkeys::Business::PptSessionToken controlToken{11, 12, 13, 14, 15, 16, true, true, 17};
			const std::optional preparedControl = controlToken;
			require(WorkspacePrimaryIdentityMatches(controls, desired, preparedControl, controlToken), "basic-control-terminal-identity-matches-current-session");
			auto replacedControl = controlToken; ++replacedControl.localSession;
			require(!WorkspacePrimaryIdentityMatches(controls, desired, preparedControl, replacedControl), "old-basic-control-native-complete-rejects-replaced-session");
			auto endedControl = controlToken; endedControl.active = false;
			require(!WorkspacePrimaryIdentityMatches(controls, desired, preparedControl, endedControl), "old-basic-control-native-complete-rejects-ended-primary");
			WorkspaceHandoff desktopPrimary;
			require(!WorkspacePrimaryIdentityMatches(desktopPrimary, desired, {}, controlToken), "desktop-final-commit-rejects-newly-active-presentation");
			const auto savedCoordinator = ReadWorkspaceHandoff();
			const auto injected = RequestWorkspace(Workspace::Whiteboard);
			int noticeEdges = 0;
			const auto notice = [&](auto token, bool target, bool involved) { require(token == injected.revision && target && involved, "failure-notice-captures-exact-scene"); ++noticeEdges; };
			for (unsigned i = 0; i != 8; ++i) FailWorkspaceRequestImpl(injected.revision, notice);
			require(noticeEdges == 1, "failure-notice-edge-deduplicates-after-four-real-failures");
			require(WorkspaceTransitionFailureRevision() == injected.revision &&
				!RetryWorkspaceTransitionIfRevision(injected.revision - 2) && RetryWorkspaceTransitionIfRevision(injected.revision) &&
				WorkspaceTransitionFailureRevision() == 0 && ReadWorkspaceHandoff().pending,
				"settings-business-retry-captures-and-revalidates-visible-failure-token");
			const auto latestShow = ReadWorkspaceHandoff().revision;
			const auto oldActivation = WhiteboardActivationGuard(latestShow);
			NotifyWhiteboardWindowViewRequested(true);
			require(!WorkspaceRequestCurrent(latestShow) && !oldActivation(), "queued-system-min-cancels-old-show-and-activation");
			const auto queuedView = whiteboardNativeRequest.load();
			(void)RequestWorkspace(Workspace::Whiteboard, true);
			require(!oldActivation(), "old-guard-stays-false-before-native-packet-retirement");
			NotifyWhiteboardWindowViewRequested(false); // 新 epoch 的新包先到，清理旧包不能覆盖它。
			const auto newerView = whiteboardNativeRequest.load();
			RetireNativeWhiteboardRequest(queuedView);
			require(whiteboardNativeRequest.load() == newerView && !oldActivation(), "cas-retirement-preserves-new-packet-and-never-revives-old-guard");
			(void)ConsumeNativeWhiteboardRequests();
			const auto showing = ReadWorkspaceHandoff();
			require(!showing.minimized && whiteboardNativeRequest.load() == 0 && !oldActivation(), "new-view-consumes-without-clearing-before-new-revision");
			NotifyWhiteboardWindowViewRequested(false);
			require(whiteboardNativeRequest.load() == 0 && ReadWorkspaceHandoff().revision == showing.revision, "same-desired-system-view-is-idempotent");
			for (bool initialMinimized : {false, true})
			{
				(void)RequestWorkspace(Workspace::Whiteboard, initialMinimized);
				const auto beforeCancel = ReadWorkspaceHandoff().revision;
				NotifyWhiteboardWindowViewRequested(!initialMinimized);
				NotifyWhiteboardWindowViewRequested(initialMinimized);
				(void)ConsumeNativeWhiteboardRequests();
				require(ReadWorkspaceHandoff().minimized == initialMinimized && ReadWorkspaceHandoff().revision != beforeCancel &&
					!WorkspaceRequestCurrent(beforeCancel), "latest-opposite-system-intent-cancels-unconsumed-min-or-restore");
			}
			(void)RequestWorkspace(Workspace::Whiteboard, false);
			const auto publicCancel = ReadWorkspaceHandoff().revision;
			NotifyWhiteboardWindowViewRequested(true);
			RequestWhiteboardActive(true);
			require(!WorkspaceRequestCurrent(publicCancel) && whiteboardNativeRequest.load() == 0 && !ReadWorkspaceHandoff().minimized,
				"public-same-target-cancel-retires-queued-opposite-before-cas-cleanup");
			const auto publicViewCancel = ReadWorkspaceHandoff().revision;
			NotifyWhiteboardWindowViewRequested(true);
			(void)RequestWhiteboardWindowView(false);
			require(!WorkspaceRequestCurrent(publicViewCancel) && whiteboardNativeRequest.load() == 0, "public-view-cancel-does-not-open-aba-gap");
			NotifyWhiteboardWindowCloseRequested();
			(void)ConsumeNativeWhiteboardRequests();
			require(ReadWorkspaceHandoff().desired == Workspace::Desktop, "window-close-only-queues-until-coordinator-accepts");
			NotifyWhiteboardWindowViewRequested(true);
			require(whiteboardNativeRequest.load() == 0, "native-view-callback-rejects-even-primary-epoch");
			{ std::scoped_lock lock(workspaceHandoffMutex); workspaceHandoff = savedCoordinator; ProjectWorkspaceHandoffLocked(); }
			WorkspaceHandoff background; background.Request(Workspace::Whiteboard); background.Commit(background.revision, true, true, true);
			const auto wbRevision = background.revision;
			require(!background.Request(Workspace::Whiteboard) && background.revision == wbRevision,
				"background-ppt-facts-cannot-retire-active-whiteboard");
		}

		stage = "whiteboard-button-failed-restore-cancel";
		{
			Inkeys::Testing::WorkspaceSceneScope fixture(WorkspaceScene::Whiteboard);
			const auto minimized = RequestWorkspace(Workspace::Whiteboard, true);
			{ std::scoped_lock lock(stateModeTransitionMutex); require(CommitWorkspaceHandoffLocked(minimized.revision, true, true, true, false), "button-minimized-fixture"); }
			const auto restoring = RequestWorkspace(Workspace::Whiteboard, false);
			const auto silentNotice = [](auto, bool, bool) {};
			for (int i = 0; i != 4; ++i) FailWorkspaceRequestImpl(restoring.revision, silentNotice);
			int restores = 0, requests = 0, cancels = 0;
			const auto restore = [&] { ++restores; };
			const auto request = [&](bool) { ++requests; };
			const auto cancel = [&](std::uint64_t token) { if (CancelFailedWhiteboardTransitionIfRevision(token)) ++cancels; };
			Inkeys::UI::Bar::DispatchBarWhiteboardButtonAction(true, true, restoring.revision, restore, request, cancel);
			require(cancels == 1 && restores == 0 && requests == 0 && ReadWorkspaceHandoff().desired != Workspace::Whiteboard,
				"failed-restore-button-cancels-exact-ticket-before-physical-min-resume");
			const auto newer = RequestWorkspace(Workspace::Whiteboard, false);
			for (int i = 0; i != 4; ++i) FailWorkspaceRequestImpl(newer.revision, silentNotice);
			cancel(restoring.revision);
			require(cancels == 1 && WorkspaceTransitionFailureRevision() == newer.revision, "stale-button-failure-token-cannot-cancel-new-transition");
			const auto stableMin = RequestWorkspace(Workspace::Whiteboard, true);
			{ std::scoped_lock lock(stateModeTransitionMutex); require(CommitWorkspaceHandoffLocked(stableMin.revision, true, true, true, false), "button-stable-minimized-fixture"); }
			Inkeys::UI::Bar::DispatchBarWhiteboardButtonAction(true, true, WorkspaceTransitionFailureRevision(), restore, request, cancel);
			require(restores == 1 && requests == 0 && cancels == 1, "stable-minimized-button-keeps-normal-resume-contract");
		}

		stage = "workspace-protected-commit-tool-owner";
		for (const auto target : {Workspace::Desktop, Workspace::Presentation})
		{
			Inkeys::Testing::WorkspaceSceneScope fixture;
			require(static_cast<bool>(fixture), "safe-commit-fixture-requires-no-host-or-window");
			ChangeStateModeToPenTool(PenToolSelectionEnum::HardPen);
			require(SetPenWidth(7.0, false), "safe-source-desktop-width-memory");
			(void)SetPenColor(RGB(37, 71, 109), false);
			const auto desktopMemory = GetStateModeSnapshot();
			require(fixture.Set(WorkspaceScene::Whiteboard), "safe-commit-source-whiteboard");
			ChangeStateModeToPenTool(PenToolSelectionEnum::Highlighter);
			require(SetPenWidth(18.0, false), "safe-source-whiteboard-width-memory");
			(void)SetPenColor(RGB(101, 53, 19), false);
			const auto whiteboardMemory = GetStateModeSnapshot();
			if (target == Workspace::Presentation)
			{
				std::scoped_lock lock(stateModeTransitionMutex);
				workspaceStateModes[static_cast<std::size_t>(target)].reset();
			}
			const auto bridgeBefore = bridge.Snapshot();
			const auto runtimeBefore = ProductRuntimeSnapshot();
			const auto request = RequestWorkspace(target);
			HostRuntimeSnapshot fatal;
			fatal.hostGeneration = 1; fatal.consumerFatal = true;
			fatal.recovery = {CanvasRecoveryStatus::Failed, CanvasRecoveryReason::ConsumerFatal,
				bridgeBefore.workspace, {}, 0, false, 7};
			const RECT hiddenOutput{};
			require(WorkspaceTargetReady(request, bridgeBefore, fatal, false, true, hiddenOutput),
				"matching-fatal-token-allows-safe-retirement-without-normal-frame");
			{
				std::scoped_lock lock(stateModeTransitionMutex);
				require(!CommitWorkspaceHandoffLocked(request.revision, true, true, false, true) &&
					stateModeWorkspace == Workspace::Whiteboard, "missing-native-safety-ack-keeps-source-tool-owner");
				require(CommitWorkspaceHandoffLocked(request.revision, true, true, true, true),
					"safe-final-commit-moves-real-tool-owner-under-mode-lock");
			}
			const auto after = GetStateModeSnapshot();
			require(stateModeWorkspace == target && !ReadWorkspaceHandoff().pending &&
				ReadWorkspaceHandoff().committed == target, "committed-and-real-tool-owner-match-after-safe-exit");
			require(target == Workspace::Desktop ? sameState(after, desktopMemory) :
				after.StateModeSelect == StateModeSelectEnum::IdtSelection,
				"safe-exit-restores-memory-or-new-target-default-selection");
			require(bridge.Snapshot().revision == bridgeBefore.revision && sameProjection(bridge.Snapshot(), bridgeBefore) &&
				ProductRuntimeSnapshot().recovery == runtimeBefore.recovery &&
				ProductRuntimeSnapshot().consumerFatal == runtimeBefore.consumerFatal &&
				ProductRuntimeSnapshot().hostGeneration == runtimeBefore.hostGeneration,
				"pure-safe-commit-preserves-bridge-and-full-host-recovery-identity");
			require((settingOwnerDesiredState.load(std::memory_order_acquire) & 1U) ==
				static_cast<std::uint64_t>(!IsComputerInteractionMode(after.StateModeSelect)),
				"safe-tool-owner-migration-recomputes-setting-owner-intent-without-window-sync");
			StateModeClass desktop;
			require(TryGetDesktopStateModeSnapshot(desktop) == (target == Workspace::Desktop),
				"safe-desktop-commit-restores-real-save-eligibility");
			ChangeStateModeToPen();
			require(stateModeWorkspace == target, "following-tool-change-belongs-to-returned-workspace");
			require(fixture.Set(WorkspaceScene::Whiteboard) && sameState(GetStateModeSnapshot(), whiteboardMemory),
				"following-target-tool-change-cannot-overwrite-whiteboard-memory");
		}

		stage = "ppt-focus-business";
		require(RunPptFocusProductionTest() == 0, "ppt-focus-business-dispatch");
		{std::scoped_lock lock(whiteboardReturnFocusMutex);whiteboardReturnFocus.Reset();}
		{
			const Inkeys::Business::PptFocusRequest expected{
				{ 1, 2, 3, 4, 5, 6, true, true, 7 }, { 11, 12 } };
			int queued = 0;
			const auto queue = [&](const Inkeys::Business::PptFocusRequest& request)
			{
				require(Inkeys::Business::MatchesPptSession(request.session, expected.session),
					"whiteboard-return-keeps-original-session");
				++queued;
			};
			whiteboardReturnFocus.Begin(expected);
			setProbePhase(WhiteboardPhase::Exiting);
			require(!CompleteWhiteboardReturnFocus(queue) && queued == 0,
				"whiteboard-return-waits-for-inactive");
			setProbePhase(WhiteboardPhase::Inactive);
			require(CompleteWhiteboardReturnFocus(queue) && queued == 1
				&& !CompleteWhiteboardReturnFocus(queue), "whiteboard-return-one-queue");
			auto inactive = expected;
			inactive.session.active = false;
			whiteboardReturnFocus.Begin(inactive);
			require(!CompleteWhiteboardReturnFocus(queue) && queued == 1,
				"whiteboard-return-without-ppt-does-not-queue");
			const Inkeys::Business::PptFocusForeground externalA{ 21, 22 }, externalB{ 31, 32 };
			whiteboardReturnFocus.Begin(expected);
			whiteboardReturnFocus.RebaseAfterWindowTransition(expected.foreground, externalA, false);
			require(!CompleteWhiteboardReturnFocus(queue), "external-foreground-cannot-change-during-return");
			whiteboardReturnFocus.Begin(expected);
			whiteboardReturnFocus.RebaseAfterWindowTransition(expected.foreground, externalA, true);
			whiteboardReturnFocus.RebaseAfterWindowTransition(externalA, externalB, false);
			require(!CompleteWhiteboardReturnFocus(queue), "controlled-hide-cannot-authorize-later-external-switch");
			whiteboardReturnFocus.Begin(expected);
			whiteboardReturnFocus.RebaseAfterWindowTransition(expected.foreground, externalA, true);
			whiteboardReturnFocus.RebaseAfterWindowTransition(externalA, externalA, false);
			require(CompleteWhiteboardReturnFocus(queue) && queued == 2,
				"controlled-hide-with-unchanged-external-foreground-queues-once");
			whiteboardReturnFocus.Begin(expected);
			RequestWhiteboardActive(true);
			RequestWhiteboardActive(false);
			require(!CompleteWhiteboardReturnFocus(queue), "whiteboard-reentry-cancels-old-return");
			setProbePhase(WhiteboardPhase::Inactive);
		}

		stage = "workspace-tool-isolation";
		ChangeStateModeToPenTool(PenToolSelectionEnum::HardPen);
		require(SetPenWidth(7.0f, false) && SetPenColor(RGB(17, 31, 47), false), "desktop-tool-memory");
		const auto desktopTool = GetStateModeVersionedSnapshot();
		require(SwitchWorkspaceStateMode(Workspace::Whiteboard) &&
			GetStateModeSnapshot().StateModeSelect == StateModeSelectEnum::IdtPen &&
			GetStateModeSnapshot().Pen.ModeSelect == PenModeSelectEnum::IdtPenSoftPen,
			"first-whiteboard-defaults-to-soft-pen-without-inheriting-desktop-tool");
		require(!ChangeStateModeToPptAnnotation(PptAnnotationTool::Laser, desktopTool.revision) &&
			!ChangeStateModeToPptAnnotation(PptAnnotationTool::Laser, StateModeTransitionRevision()) &&
			!SetPenWidthIfRevision(30.0, desktopTool.revision, false), "whiteboard-rejects-old-tool-and-ppt");
		ChangeStateModeToPenTool(PenToolSelectionEnum::Highlighter);
		require(SetPenWidth(18.0f, false) && SetPenColor(RGB(61, 79, 97), false), "whiteboard-tool-memory");
		const auto whiteboardTool = GetStateModeSnapshot();
		require(SwitchWorkspaceStateMode(Workspace::Desktop) &&
			sameState(desktopTool.state, GetStateModeSnapshot()), "whiteboard-restores-desktop-tool");
		require(SwitchWorkspaceStateMode(Workspace::Presentation) &&
			GetStateModeSnapshot().StateModeSelect == StateModeSelectEnum::IdtSelection &&
			bridge.Snapshot().workspace == Workspace::Desktop,
			"first-ppt-restores-tool-without-publishing-incomplete-scene");
		ChangeStateModeToPenTool(PenToolSelectionEnum::Laser);
		require(SetPenWidth(6.0f, false) && SetPenColor(RGB(113, 131, 149), false), "ppt-tool-memory");
		const auto presentationTool = GetStateModeSnapshot();
		for (int index = 0; index < 16; ++index)
		{
			require(SwitchWorkspaceStateMode(Workspace::Whiteboard) &&
				sameState(whiteboardTool, GetStateModeSnapshot()), "rapid-reentry-restores-whiteboard");
			const auto revision = StateModeTransitionRevision();
			require(!SwitchWorkspaceStateMode(Workspace::Whiteboard) &&
				StateModeTransitionRevision() == revision, "same-workspace-does-not-resnapshot");
			require(SwitchWorkspaceStateMode(Workspace::Presentation) &&
				sameState(presentationTool, GetStateModeSnapshot()), "whiteboard-restores-ppt-tool");
		}
		require(SwitchWorkspaceStateMode(Workspace::Whiteboard), "whiteboard-before-background-ppt-end");
		setProbePhase(WhiteboardPhase::Active);
		SyncPrimaryWorkspaceStateMode();
		require(sameState(whiteboardTool, GetStateModeSnapshot()), "background-ppt-end-preserves-whiteboard-tool");
		setProbePhase(WhiteboardPhase::Exiting);
		require(SwitchWorkspaceStateMode(Workspace::Desktop) &&
			sameState(desktopTool.state, GetStateModeSnapshot()), "ppt-ended-whiteboard-returns-desktop-tool");
		setProbePhase(WhiteboardPhase::Inactive);
		const auto cancelledEntry = GetStateModeVersionedSnapshot();
		RequestWhiteboardActive(true);
		SyncPrimaryWorkspaceStateMode();
		RequestWhiteboardActive(false);
		require(sameState(cancelledEntry.state, GetStateModeSnapshot()) &&
			StateModeTransitionRevision() == cancelledEntry.revision,
			"cancel-before-whiteboard-entry-preserves-tool");
		setProbePhase(WhiteboardPhase::Inactive);
		// 旧快照只恢复工具偏好，跨屏/设备上下文继续使用当前值。
		auto changedWidthContext = GetPenWidthContext();
		changedWidthContext.effectiveDpi = changedWidthContext.effectiveDpi == 144.0 ? 192.0 : 144.0;
		require(ApplyPenWidthContext(changedWidthContext) &&
			SwitchWorkspaceStateMode(Workspace::Whiteboard) &&
			Inkeys::Business::PenWidth::EquivalentContext(GetPenWidthContext(), changedWidthContext),
			"whiteboard-restore-keeps-current-display");
		require(SwitchWorkspaceStateMode(Workspace::Desktop) &&
			Inkeys::Business::PenWidth::EquivalentContext(GetPenWidthContext(), changedWidthContext) &&
			GetPenWidth() == 7.0f, "desktop-restore-keeps-width-selection-and-current-display");
		ChangeStateModeToSelection();
		require(SwitchWorkspaceStateMode(Workspace::Presentation), "ppt-before-exit");
		const auto beforeExit = GetStateModeSnapshot();
		require(CompletePresentationExitStateMode(StateModeTransitionRevision()) &&
			GetStateModeSnapshot().StateModeSelect == StateModeSelectEnum::IdtSelection &&
			ExitSelectionHandoffPending() && bridge.Snapshot().workspace == Workspace::Desktop &&
			bridge.Snapshot().selectionMode, "ppt-exit-keeps-selection-surface-handoff");
		require(SwitchWorkspaceStateMode(Workspace::Presentation) &&
			sameState(beforeExit, GetStateModeSnapshot()), "ppt-exit-retains-presentation-tool");
		const auto staleExit = StateModeTransitionRevision();
		ChangeStateModeToPenTool(PenToolSelectionEnum::Highlighter);
		const auto newPresentationTool = GetStateModeSnapshot();
		require(!CompletePresentationExitStateMode(staleExit) &&
			GetStateModeSnapshot().StateModeSelect == StateModeSelectEnum::IdtSelection &&
			bridge.Snapshot().workspace == Workspace::Desktop,
			"old-exit-restores-desktop-without-selection-write");
		ChangeStateModeToPenTool(PenToolSelectionEnum::HardPen);
		const auto newDesktopTool = GetStateModeVersionedSnapshot();
		require(!CompletePresentationExitStateMode(staleExit) &&
			sameState(newDesktopTool.state, GetStateModeSnapshot()) &&
			StateModeTransitionRevision() == newDesktopTool.revision, "old-exit-preserves-new-desktop-tool");
		require(SwitchWorkspaceStateMode(Workspace::Presentation) &&
			sameState(newPresentationTool, GetStateModeSnapshot()), "old-exit-keeps-new-ppt-tool");
		require(SwitchWorkspaceStateMode(Workspace::Whiteboard), "whiteboard-before-exit-callback");
		setProbePhase(WhiteboardPhase::Active);
		const auto protectedWhiteboard = GetStateModeVersionedSnapshot();
		require(!CompletePresentationExitStateMode(protectedWhiteboard.revision) &&
			sameState(protectedWhiteboard.state, GetStateModeSnapshot()) &&
			StateModeTransitionRevision() == protectedWhiteboard.revision, "ppt-exit-preserves-whiteboard-transaction");
		setProbePhase(WhiteboardPhase::Inactive);
		require(SwitchWorkspaceStateMode(Workspace::Desktop), "restore-desktop-after-exit-tests");

		stage = "workspace-publication-race";
		for (const auto phase : { WhiteboardPhase::Entering, WhiteboardPhase::Active, WhiteboardPhase::Exiting })
		{
			require(SwitchWorkspaceStateMode(Workspace::Presentation), "presentation-before-publish-race");
			const bool staleWhiteboard = WhiteboardTransactionActive();
			const auto staleSceneEpoch = PptSceneWorkspaceEpoch();
			const auto exitRevision = StateModeTransitionRevision();
			Bridge::PresentationTarget target{};
			target.key.bytes[0] = 1;
			target.sourceIdentity = "process-local:workspace-publication-test";
			target.totalPages = 1;
			require(bridge.PublishPresentationTarget(target).has_value(), "seed-presentation-before-clear");
			(void)PublishPrimaryPresentationTarget(nullptr);
			require(bridge.Snapshot().workspace == Workspace::Presentation &&
				!bridge.Snapshot().presentationTarget && !bridge.Snapshot().hasPage,
				"primary-presentation-clear-remains-available");
			RequestWhiteboardActive(true);
			require(!staleWhiteboard && SwitchWorkspaceStateMode(Workspace::Whiteboard),
				"whiteboard-after-ppt-snapshot");
			setProbePhase(phase);
			if (phase == WhiteboardPhase::Exiting) RequestWhiteboardActive(false);
			const auto protectedState = GetStateModeVersionedSnapshot();
			const auto protectedBridge = bridge.Snapshot();
			bool selected = false;
			// 模拟 PPT 已读到非白板快照，发布前另一线程完成了白板切换。
			std::thread pending([&]
			{
				if (staleWhiteboard) return;
				SyncPrimaryWorkspaceStateMode(staleSceneEpoch);
				(void)PublishPrimaryPresentationTarget(nullptr, staleSceneEpoch);
				(void)PublishPrimaryPresentationTarget(&target, staleSceneEpoch);
				selected = CompletePresentationExitStateMode(exitRevision, staleSceneEpoch);
			});
			pending.join();
			require(!selected && sameState(protectedState.state, GetStateModeSnapshot()) &&
				StateModeTransitionRevision() == protectedState.revision &&
				protectedBridge.workspace == Workspace::Whiteboard &&
				bridge.Snapshot().revision == protectedBridge.revision,
				"stale-ppt-publish-cannot-overwrite-whiteboard");
			RequestWhiteboardActive(false);
			setProbePhase(WhiteboardPhase::Inactive);
			require(SwitchWorkspaceStateMode(Workspace::Desktop), "restore-desktop-after-publish-race");
		}

		stage = "workspace-global-memory-isolation";
		{
			// 独立临时根覆盖真实 SetMemory 调用；异常退出也必须恢复路径并回收本次目录。
			struct MemoryScope
			{
				std::wstring savedPath = globalPath;
				std::filesystem::path directory;
				StateModeClass savedState = GetStateModeSnapshot();
				Workspace savedWorkspace = stateModeWorkspace;
				decltype(workspaceStateModes) savedWorkspaces = workspaceStateModes;
				~MemoryScope()
				{
					globalPath = savedPath;
					{
						std::scoped_lock lock(stateModeTransitionMutex);
						stateMode = savedState;
						stateModeWorkspace = savedWorkspace;
						workspaceStateModes = savedWorkspaces;
					}
					if (!directory.empty())
					{
						std::error_code error;
						std::filesystem::remove_all(directory, error);
					}
				}
			} memoryScope;
			const auto temporary = std::filesystem::temp_directory_path() /
				(L"InkeysWorkspaceMemoryTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
			require(std::filesystem::create_directory(temporary), "create-private-memory-root");
			memoryScope.directory = temporary;
			globalPath = temporary.wstring();
			const auto memory = temporary / L"Inkeys" / L"Memory" / L"memory.json";
			std::filesystem::create_directories(memory.parent_path());
			require(Inkeys::PptSettings::WriteAtomically(memory, "{}"), "seed-private-memory");
			const auto readMemory = [&]
			{
				std::ifstream input(memory, std::ios::binary);
				require(input.is_open(), "read-private-memory");
				return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
			};
			StateModeClass desktopSnapshot;
			require(TryGetDesktopStateModeSnapshot(desktopSnapshot) && SetMemory(), "desktop-memory-write");
			const auto desktopMemory = readMemory();
			for (const auto workspace : { Workspace::Whiteboard, Workspace::Presentation })
			{
				require(SwitchWorkspaceStateMode(workspace), "enter-private-memory-workspace");
				ChangeStateModeToPenTool(PenToolSelectionEnum::SoftPen);
				require(!TryGetDesktopStateModeSnapshot(desktopSnapshot) &&
					SetPenColor(RGB(201, 102, 53), false) && SetPenWidth(21.0, false) && SetMemory() &&
					readMemory() == desktopMemory, "non-desktop-global-memory-unchanged");
			}
			require(SwitchWorkspaceStateMode(Workspace::Desktop) &&
				SetPenColor(RGB(202, 103, 54)) && readMemory() != desktopMemory, "desktop-memory-still-updates");
		}

		stage = "cross-entry-equivalence";
		ChangeStateModeToPenTool(PenToolSelectionEnum::SoftPen);
		require(SetPenWidth(7.0f, false) && SetPenColor(RGB(17, 31, 47), false), "seed-soft-memory");
		ChangeStateModeToPenTool(PenToolSelectionEnum::Highlighter);
		require(SetPenWidth(15.0f, false) && SetPenColor(RGB(61, 79, 97), false), "seed-highlighter-memory");
		ChangeStateModeToPenTool(PenToolSelectionEnum::Laser);
		require(SetPenWidth(6.0f, false) && SetPenColor(RGB(113, 131, 149), false), "seed-laser-memory");
		struct EquivalentEntry
		{
			PenToolSelectionEnum pen;
			PptAnnotationTool ppt;
			Bridge::Tool tool;
		};
		const EquivalentEntry entries[]{
			{ PenToolSelectionEnum::SoftPen, PptAnnotationTool::Pen, Bridge::Tool::Pen },
			{ PenToolSelectionEnum::Highlighter, PptAnnotationTool::Highlighter, Bridge::Tool::Highlighter },
			{ PenToolSelectionEnum::Laser, PptAnnotationTool::Laser, Bridge::Tool::Laser },
		};
		for (const auto& entry : entries)
		{
			// Laser 保留最后普通笔记忆；两条入口从相同 Highlighter 记忆出发。
			ChangeStateModeToPenTool(PenToolSelectionEnum::Highlighter);
			ChangeStateModeToSelection();
			const auto revision = StateModeTransitionRevision();
			require(ChangeStateModeToPenTool(entry.pen), "pen-entry-accepted");
			require(StateModeTransitionRevision() == revision + 1, "pen-entry-revision");
			const auto direct = GetStateModeSnapshot();
			const auto directBridge = bridge.Snapshot();
			checkProjection(entry.tool);
			ChangeStateModeToPenTool(PenToolSelectionEnum::Highlighter);
			ChangeStateModeToSelection();
			const auto pptRevision = StateModeTransitionRevision();
			require(ChangeStateModeToPptAnnotation(entry.ppt, pptRevision), "ppt-entry-accepted");
			require(StateModeTransitionRevision() == pptRevision + 1, "ppt-entry-revision");
			require(sameState(direct, GetStateModeSnapshot()) &&
				sameProjection(directBridge, bridge.Snapshot()), "cross-entry-equivalence");
		}

		stage = "repeated-intent-and-rapid-switch";
		const PenToolSelectionEnum tools[]{ PenToolSelectionEnum::SoftPen,
			PenToolSelectionEnum::HardPen, PenToolSelectionEnum::Highlighter, PenToolSelectionEnum::Laser };
		const Bridge::Tool projectedTools[]{ Bridge::Tool::Pen, Bridge::Tool::HardPen,
			Bridge::Tool::Highlighter, Bridge::Tool::Laser };
		for (int index = 0; index < 32; ++index)
		{
			const int slot = index % 4;
			const auto before = StateModeTransitionRevision();
			require(ChangeStateModeToPenTool(tools[slot]), "rapid-tool-accepted");
			require(StateModeTransitionRevision() == before + 1, "rapid-tool-revision");
			checkProjection(projectedTools[slot]);
			const auto stable = GetStateModeVersionedSnapshot();
			const auto stableBridge = bridge.Snapshot();
			require(!ChangeStateModeToPenTool(tools[slot]), "repeated-tool-no-visual-change");
			require(StateModeTransitionRevision() == stable.revision + 1 &&
				sameState(stable.state, GetStateModeSnapshot()), "repeated-intent-revision");
			require(bridge.Snapshot().revision == stableBridge.revision &&
				sameProjection(stableBridge, bridge.Snapshot()), "repeated-tool-no-bridge-publication");
			require(!ChangeStateModeToPptAnnotation(PptAnnotationTool::Pen, stable.revision), "repeated-intent-rejects-old-ppt");
		}

		stage = "late-callback-rejection";
		ChangeStateModeToPenTool(PenToolSelectionEnum::SoftPen);
		const auto old = GetStateModeVersionedSnapshot();
		std::mutex gateMutex;
		std::condition_variable gate;
		bool release = false;
		bool rejected = false;
		// 确定性排队旧回调，再提交新选择；不靠 Sleep 猜测线程交错。
		std::thread late([&]
		{
			std::unique_lock lock(gateMutex);
			gate.wait(lock, [&] { return release; });
			lock.unlock();
			const bool width = SetPenWidthIfRevision(99.0f, old.revision, false);
			const bool shape = SetShapeModeSelectIfRevision(ShapeModeSelectEnum::IdtShapeRectangle1, old.revision);
			const bool selection = ChangeStateModeToSelectionIfRevision(old.revision);
			const bool ppt = ChangeStateModeToPptAnnotation(PptAnnotationTool::Highlighter, old.revision);
			rejected = !width && !shape && !selection && !ppt;
		});
		ChangeStateModeToPenTool(PenToolSelectionEnum::HardPen);
		const auto current = GetStateModeVersionedSnapshot();
		const auto currentBridge = bridge.Snapshot();
		{
			std::scoped_lock lock(gateMutex);
			release = true;
		}
		gate.notify_one();
		late.join();
		require(rejected && current.revision == StateModeTransitionRevision() &&
			sameState(current.state, GetStateModeSnapshot()) &&
			currentBridge.revision == bridge.Snapshot().revision &&
			sameProjection(currentBridge, bridge.Snapshot()), "late-callbacks-preserve-new-state");
		require(old.state.Pen.ModeSelect == PenModeSelectEnum::IdtPenSoftPen &&
			current.state.Pen.ModeSelect == PenModeSelectEnum::IdtPenHardPen, "value-snapshot-remains-frozen");

		stage = "valid-revision-and-slot-isolation";
		require(SetPenWidthIfRevision(11.0f, current.revision, false), "valid-width-accepted");
		const auto widths = GetStateModeSnapshot();
		require(widths.Pen.Brush1.width == 11.0f && widths.Pen.Highlighter1.width == 15.0f &&
			widths.Pen.Laser.width == 6.0f, "valid-width-only-target-slot");
		checkProjection(Bridge::Tool::HardPen);
		ChangeStateModeToShape();
		require(SetShapeModeSelectIfRevision(ShapeModeSelectEnum::IdtShapeRectangle1,
			StateModeTransitionRevision()), "valid-shape-accepted");
		const auto rectangle = GetStateModeVersionedSnapshot();
		require(SetPenWidthIfRevision(13.0f, rectangle.revision, false), "valid-shape-width-accepted");
		require(GetStateModeSnapshot().Shape.Rectangle1.width == 13.0f &&
			GetStateModeSnapshot().Shape.StraightLine1.width == rectangle.state.Shape.StraightLine1.width,
			"shape-width-only-target-slot");
		checkProjection(Bridge::Tool::OutlineRectangle);
		const auto shapeRevision = StateModeTransitionRevision();
		const auto shapeBridgeRevision = bridge.Snapshot().revision;
		require(!SetShapeModeSelectIfRevision(ShapeModeSelectEnum::IdtShapeRectangle1,
			shapeRevision) && StateModeTransitionRevision() == shapeRevision + 1 &&
			bridge.Snapshot().revision == shapeBridgeRevision, "repeated-shape-intent-no-publication");
		require(ChangeStateModeToSelectionIfRevision(StateModeTransitionRevision()), "valid-selection-accepted");
		checkProjection(Bridge::Tool::Pen);

		stage = "laser-memory-and-top-level-mode";
		ChangeStateModeToPenTool(PenToolSelectionEnum::Laser);
		ChangeStateModeToEraser();
		require(GetStateModeSnapshot().laserActive && !IsLaserToolActive(), "eraser-overrides-laser-memory");
		checkProjection(Bridge::Tool::ConfiguredEraser);
		ChangeStateModeToShape();
		checkProjection(Bridge::Tool::OutlineRectangle);
		ChangeStateModeToSelection();
		checkProjection(Bridge::Tool::Pen);
		ChangeStateModeToPen();
		require(IsLaserToolActive(), "pen-restores-laser-memory");
		checkProjection(Bridge::Tool::Laser);
		stage = "select-and-clickthrough-modes";
		require(GetSelectAndClickThroughMode() == 0 && !ChangeStateModeToClickThrough() &&
			!ToggleClickThroughFromButton(), "combined-default-no-clickthrough");
		ChangeStateModeToSelection();
		const auto beforeSeparate = bridge.Snapshot();
		require(SetSelectAndClickThroughModeImpl(1, [] { return true; }) &&
			GetStateModeSnapshot().StateModeSelect == StateModeSelectEnum::IdtClickThrough,
			"separate-maps-retained-selection");
		require(bridge.Snapshot().selectionMode && sameProjection(beforeSeparate, bridge.Snapshot()),
			"mode-mapping-preserves-bridge-and-ink");
		const auto repeated = GetStateModeVersionedSnapshot();
		require(!ChangeStateModeToClickThrough() && StateModeTransitionRevision() == repeated.revision &&
			!SetSelectAndClickThroughMode(1), "explicit-clickthrough-and-mode-remain-idempotent");
		require(SetSelectAndClickThroughMode(0) &&
			GetStateModeSnapshot().StateModeSelect == StateModeSelectEnum::IdtSelection,
			"combined-restores-selection");
		require(SetSelectAndClickThroughModeImpl(1, [] { return false; }) &&
			GetStateModeSnapshot().StateModeSelect == StateModeSelectEnum::IdtSelection,
			"separate-empty-selection-remains-selection");
		stage = "mode-switch-content-after-lock-wait";
		const auto checkQueuedContent = [&](bool before, bool after)
		{
			SetSelectAndClickThroughMode(0);
			ChangeStateModeToSelection();
			const auto initial = GetStateModeVersionedSnapshot();
			const auto initialBridge = bridge.Snapshot();
			std::atomic_bool content = before;
			std::mutex queueMutex;
			std::condition_variable queueReady;
			bool queued = false;
			bool accepted = false;
			bool sampledContent = before;
			int sampledMode = -1;
			int readCount = 0;
			std::unique_lock modeLock(stateModeTransitionMutex);
			std::thread pending([&]
			{
				{
					std::scoped_lock queueLock(queueMutex);
					queued = true;
				}
				queueReady.notify_one();
				accepted = SetSelectAndClickThroughModeImpl(1, [&]
				{
					++readCount;
					sampledMode = GetSelectAndClickThroughMode();
					sampledContent = content.load(std::memory_order_acquire);
					return sampledContent;
				});
			});
			{
				std::unique_lock queueLock(queueMutex);
				queueReady.wait(queueLock, [&] { return queued; });
			}
			// 模拟 Host 不持模式锁的内容发布；设置请求只能在解锁后读取新值。
			content.store(after, std::memory_order_release);
			modeLock.unlock();
			pending.join();
			require(accepted && readCount == 1 && sampledContent == after && sampledMode == 1,
				"queued-mode-switch-samples-current-content-in-transaction");
			require(StateModeTransitionRevision() == initial.revision + 1 &&
				GetStateModeSnapshot().StateModeSelect == (after ?
					StateModeSelectEnum::IdtClickThrough : StateModeSelectEnum::IdtSelection) &&
				sameProjection(initialBridge, bridge.Snapshot()), "queued-mode-switch-maps-latest-content");
			require(!SetSelectAndClickThroughModeImpl(1, [&] { ++readCount; return before; }) &&
				readCount == 1, "unchanged-mode-does-not-sample-content");
		};
		checkQueuedContent(true, false);
		checkQueuedContent(false, true);
		// 真实公开入口仍读取未启动 Host 的空页；不为测试启动画布或写配置。
		require(SetSelectAndClickThroughMode(0) && SetSelectAndClickThroughMode(1) &&
			GetStateModeSnapshot().StateModeSelect == StateModeSelectEnum::IdtSelection,
			"public-mode-switch-samples-empty-host");
		const StateModeSelectEnum rememberedTools[]{StateModeSelectEnum::IdtPen,
			StateModeSelectEnum::IdtShape, StateModeSelectEnum::IdtEraser};
		for (const auto tool : rememberedTools)
		{
			if (tool == StateModeSelectEnum::IdtPen) ChangeStateModeToPenTool(PenToolSelectionEnum::Laser);
			else if (tool == StateModeSelectEnum::IdtShape) ChangeStateModeToShape();
			else ChangeStateModeToEraser();
			const auto annotation = GetStateModeSnapshot();
			ToggleFreezeFromButton();
			require(Inkeys::UI::Freeze::IsActive() && ChangeStateModeToClickThrough() &&
				!Inkeys::UI::Freeze::IsActive() && bridge.Snapshot().selectionMode,
				"clickthrough-cancels-freeze-for-all-tools");
			ToggleFreezeFromButton();
			require(Inkeys::UI::Freeze::IsActive() && sameState(annotation, GetStateModeSnapshot()) &&
				!bridge.Snapshot().selectionMode, "freeze-restores-tool-and-all-attributes");
			ToggleFreezeFromButton();
			require(!Inkeys::UI::Freeze::IsActive(), "freeze-second-click-deactivates");

			const auto annotationVersion = GetStateModeVersionedSnapshot();
			const auto annotationBridge = bridge.Snapshot();
			const auto runtimeBefore = ProductRuntimeSnapshot();
			Bridge::Command historyCommand;
			require(!bridge.TryConsume(historyCommand) &&
				bridge.Publish(Bridge::CommandType::Undo) == Bridge::CommandResult::Accepted &&
				bridge.Publish(Bridge::CommandType::Redo) == Bridge::CommandResult::Accepted,
				"seed-history-fifo-before-clickthrough-toggle");
			ToggleFreezeFromButton();
			require(ToggleClickThroughFromButton() &&
				StateModeTransitionRevision() == annotationVersion.revision + 1 &&
				GetStateModeSnapshot().StateModeSelect == StateModeSelectEnum::IdtClickThrough &&
				bridge.Snapshot().selectionMode && !Inkeys::UI::Freeze::IsActive(),
				"button-enters-clickthrough-and-cancels-freeze");
			const auto clickthroughVersion = GetStateModeVersionedSnapshot();
			const auto clickthroughBridge = bridge.Snapshot();
			require(!ChangeStateModeToClickThrough() &&
				StateModeTransitionRevision() == clickthroughVersion.revision &&
				bridge.Snapshot().revision == clickthroughBridge.revision,
				"explicit-entry-does-not-toggle-selected-clickthrough");
			require(ToggleClickThroughFromButton() &&
				StateModeTransitionRevision() == clickthroughVersion.revision + 1 &&
				sameState(annotation, GetStateModeSnapshot()) &&
				sameProjection(annotationBridge, bridge.Snapshot()) &&
				!bridge.Snapshot().selectionMode && !Inkeys::UI::Freeze::IsActive(),
				"button-exits-to-remembered-tool-and-all-attributes-without-freeze");
			require(!ChangeStateModeToSelectionIfRevision(clickthroughVersion.revision) &&
				!ChangeStateModeToPptAnnotation(PptAnnotationTool::Pen, annotationVersion.revision),
				"toggle-rejects-old-selection-and-ppt-callbacks");
			require(ToggleClickThroughFromButton() && bridge.Snapshot().selectionMode &&
				ToggleClickThroughFromButton() && sameState(annotation, GetStateModeSnapshot()) &&
				sameProjection(annotationBridge, bridge.Snapshot()) && !Inkeys::UI::Freeze::IsActive(),
				"button-reenters-and-exits-same-remembered-tool");
			const auto runtimeAfter = ProductRuntimeSnapshot();
			require(runtimeBefore.currentPageHasContent == runtimeAfter.currentPageHasContent &&
				runtimeBefore.canUndo == runtimeAfter.canUndo && runtimeBefore.canRedo == runtimeAfter.canRedo &&
				runtimeBefore.contentRevision == runtimeAfter.contentRevision &&
				bridge.TryConsume(historyCommand) && historyCommand.type == Bridge::CommandType::Undo &&
				bridge.TryConsume(historyCommand) && historyCommand.type == Bridge::CommandType::Redo &&
				!bridge.TryConsume(historyCommand), "toggle-preserves-content-and-history-fifo-without-clear");
		}
		stage = "shortcut-conditional-roundtrip";
		for (int style = 0; style < 2; ++style)
		{
			(void)SetSelectAndClickThroughMode(style);
			for (const auto tool : rememberedTools)
			{
				if (tool == StateModeSelectEnum::IdtPen) ChangeStateModeToPenTool(PenToolSelectionEnum::Laser);
				else if (tool == StateModeSelectEnum::IdtShape) ChangeStateModeToShape();
				else ChangeStateModeToEraser();
				const auto annotation = GetStateModeVersionedSnapshot();
				const auto exitMarker = draw3ExitSelectionModeRevision.load(std::memory_order_acquire);
				require(ToggleFreezeForShortcutIfRevision(annotation.revision), "shortcut-freeze-annotation");
				const auto freeze = Inkeys::UI::Freeze::Snapshot();
				require(freeze.active && ChangeStateModeForShortcutIfRevision(false,
					annotation.revision, freeze.revision) && !Inkeys::UI::Freeze::IsActive() &&
					GetStateModeSnapshot().StateModeSelect == StateModeSelectEnum::IdtSelection,
					"shortcut-selection-turns-off-captured-freeze");
				require(draw3ExitSelectionModeRevision.load(std::memory_order_acquire) == exitMarker,
					"shortcut-selection-is-not-ppt-exit-marker");
				require(!ToggleFreezeForShortcutIfRevision(StateModeTransitionRevision()),
					"shortcut-freeze-refuses-selection");
				require(ChangeStateModeForShortcutIfRevision(true, StateModeTransitionRevision(),
					Inkeys::UI::Freeze::Snapshot().revision) && sameState(annotation.state, GetStateModeSnapshot()) &&
					!Inkeys::UI::Freeze::IsActive(), "shortcut-restores-all-tool-attributes-without-freeze");
				require(!ChangeStateModeForShortcutIfRevision(false, annotation.revision, freeze.revision),
					"shortcut-rejects-stale-mode");
				const auto capturedFreeze = Inkeys::UI::Freeze::Snapshot();
				require(ToggleFreezeForShortcutIfRevision(StateModeTransitionRevision()), "shortcut-new-freeze");
				require(ChangeStateModeForShortcutIfRevision(false, StateModeTransitionRevision(), capturedFreeze.revision) &&
					Inkeys::UI::Freeze::IsActive(), "shortcut-old-freeze-ticket-preserves-new-intent");
				(void)Inkeys::UI::Freeze::DeactivateIfRevision(Inkeys::UI::Freeze::Snapshot().revision);
			}
		}
		require(Inkeys::UI::Bar::RunKeyboardShortcutBusinessProductionTest() == 0, "shortcut-business-owner");
		(void)SetSelectAndClickThroughMode(1);
		Inkeys::UI::Freeze::SetPresentationActive(true);
		require(ChangeStateModeToClickThrough(), "ppt-clickthrough-still-available");
		const auto pptClickthrough = GetStateModeVersionedSnapshot();
		require(!ToggleFreezeFromButton(), "ppt-freeze-unavailable-rejects-button-feedback");
		require(StateModeTransitionRevision() == pptClickthrough.revision &&
			!Inkeys::UI::Freeze::IsActive(), "ppt-freeze-unavailable-no-tool-change");
		require(ToggleClickThroughFromButton() && !bridge.Snapshot().selectionMode &&
			!Inkeys::UI::Freeze::IsActive() && ToggleClickThroughFromButton(),
			"ppt-clickthrough-button-exits-and-reenters-with-freeze-unavailable");
		Inkeys::UI::Freeze::SetPresentationActive(false);
		RequestWhiteboardActive(true);
		ChangeStateModeToPen();
		// 功能关闭时真实请求会被拒绝，不能把不存在的白板事务当作穿透门禁。
		if (IsWhiteboardFeatureEnabled())
			require(WhiteboardRequested() && !ChangeStateModeToClickThrough() && !ToggleClickThroughFromButton(),
				"whiteboard-request-rejects-clickthrough");
		else
			require(!WhiteboardRequested() && !WhiteboardTransactionActive() && ChangeStateModeToClickThrough(),
				"closed-whiteboard-gate-preserves-desktop-clickthrough");
		RequestWhiteboardActive(false);
		setProbePhase(WhiteboardPhase::Inactive);
		ChangeStateModeToClickThrough();
		const auto clearRejected = GetStateModeVersionedSnapshot();
		require(!ChangeStateModeToSelectionFromButtonImpl(true, [] { return Bridge::CommandResult::QueueFull; }) &&
			StateModeTransitionRevision() == clearRejected.revision &&
			sameState(clearRejected.state, GetStateModeSnapshot()), "selection-clear-rejection-preserves-tool");
		Bridge::Command queued;
		while (bridge.TryConsume(queued)) {}
		require(ChangeStateModeToSelectionFromButtonImpl(true, [&] { return bridge.Publish(Bridge::CommandType::Clear); }) &&
			GetStateModeSnapshot().StateModeSelect == StateModeSelectEnum::IdtSelection &&
			bridge.TryConsume(queued) && queued.type == Bridge::CommandType::Clear &&
			!bridge.TryConsume(queued), "selection-clear-accepted-one-fifo-command");
		ChangeStateModeToPen();
		require(!ChangeStateModeToSelectionFromButtonImpl(true, []
			{
				ChangeStateModeToShape();
				return Bridge::CommandResult::Accepted;
			}) && GetStateModeSnapshot().StateModeSelect == StateModeSelectEnum::IdtShape,
			"selection-clear-late-result-preserves-new-tool");
		require(ChangeStateModeToSelectionFromButtonImpl(false, [] { return Bridge::CommandResult::NotRunning; }),
			"empty-selection-does-not-publish-clear");
		require(SetSelectAndClickThroughMode(0), "restore-default-mode");
		stage = "canvas-recovery-ui-release";
		{
			HostRuntimeSnapshot failed;
			Bridge::ProductState desired;
			desired.workspace = Bridge::Workspace::Desktop;
			failed.recovery.workspace = desired.workspace;
			failed.recovery.status = CanvasRecoveryStatus::ProtectedConflict;
			require(Draw3RecoveryNeedsUiRelease(failed, desired), "terminal-scene-failure-can-release-window-handoff-without-ready");
			failed.recovery.status = CanvasRecoveryStatus::Retrying;
			require(!Draw3RecoveryNeedsUiRelease(failed, desired), "normal-retry-retains-ready-handoff");
			failed.recovery.status = CanvasRecoveryStatus::ManualRetryRequired;
			failed.recovery.reason = CanvasRecoveryReason::ConsumerFatal;
			failed.recovery.status = CanvasRecoveryStatus::Failed;
			failed.consumerFatal = true;
			require(Draw3FatalRecoveryMatches(failed, desired), "stopped-fatal-consumer-allows-only-safe-surface-release");
			failed.consumerFatal = false;
			require(!Draw3FatalRecoveryMatches(failed, desired), "normal-stopped-host-is-not-a-fatal-cleanup-ticket");
			failed.consumerFatal = true;
			failed.recovery.workspace = Bridge::Workspace::Whiteboard;
			require(!Draw3RecoveryNeedsUiRelease(failed, desired), "old-workspace-error-cannot-release-current-window-transaction");
		}
		stage = "whiteboard-background-readiness";
		{
			HostRuntimeSnapshot ready;
			ready.workspace = Workspace::Whiteboard;
			ready.firstFrameReady = true;
			ready.requestedOutputTarget = ready.readyOutputTarget = HostOutputTarget::PrimaryDrawpad;
			ready.requestedOutputRevision = ready.readyOutputRevision = 7;
			ready.contentRevision = ready.presentedContentRevision = 9;
			require(!Inkeys::Window::GetService().RequestWhiteboardActivation() &&
				Draw3WorkspaceReady(ready, Workspace::Whiteboard),
				"activation-request-rejection-does-not-revoke-real-frame-readiness");
			ready.selectionMode = true;
			require(Draw3WorkspaceReady(ready, Workspace::Whiteboard),
				"background-whiteboard-selection-still-uses-presented-primary-surface");
			ready.firstFrameReady = false;
			require(!Draw3WorkspaceReady(ready, Workspace::Whiteboard), "background-entry-still-waits-real-first-frame");
			ready.firstFrameReady = true;
			--ready.readyOutputRevision;
			require(!Draw3WorkspaceReady(ready, Workspace::Whiteboard), "background-entry-rejects-old-output");
			++ready.readyOutputRevision;
			--ready.presentedContentRevision;
			require(!Draw3WorkspaceReady(ready, Workspace::Whiteboard), "background-entry-rejects-old-content");
		}
		stage = "scene-publication-ownership";
		ChangeStateModeToPen();
		const auto primaryTool = GetStateModeVersionedSnapshot();
		const auto primaryEpoch = PptSceneWorkspaceEpoch();
		require(primaryEpoch != 0 && primaryEpoch == PptInputWorkspaceEpoch(),
			"primary-input-and-scene-share-stable-epoch");
		const auto oldPrimaryGuard = [primaryEpoch]() noexcept
		{ return PptSceneWorkspaceEpoch() == primaryEpoch; };
		RequestWhiteboardActive(true);
		const auto enteringEpoch = whiteboardInputEpoch.load(std::memory_order_acquire);
		setProbePhase(WhiteboardPhase::Entering);
		require(!ChangeStateModeToSelectionIfRevision(primaryTool.revision, primaryEpoch) &&
			StateModeTransitionRevision() == primaryTool.revision &&
			sameState(GetStateModeSnapshot(), primaryTool.state),
			"late-ppt-exit-cannot-change-tool-after-whiteboard-intent");
		require(PptSceneWorkspaceEpoch() == 0 && PptInputWorkspaceEpoch() == 0,
			"whiteboard-entering-retires-primary-scene-and-input");
		PublishProductWorkspace(Bridge::Workspace::Whiteboard,
			WhiteboardTransitionGuard(WhiteboardPhase::Entering, enteringEpoch));
		PublishProductWorkspace(Bridge::Workspace::Desktop, oldPrimaryGuard);
		ClearProductPresentationTarget(oldPrimaryGuard);
		require(bridge.Snapshot().workspace == Bridge::Workspace::Whiteboard,
			"late-desktop-and-clear-cannot-overwrite-whiteboard-at-product-commit");
		const auto oldWhiteboardGuard = WhiteboardTransitionGuard(WhiteboardPhase::Entering, enteringEpoch);
		const auto activationGuard = WhiteboardActivationGuard(enteringEpoch);
		require(activationGuard(), "current-entry-accepts-its-activation-intent");
		setProbePhase(WhiteboardPhase::Active);
		require(!oldWhiteboardGuard() && activationGuard() && PptSceneWorkspaceEpoch() == 0,
			"active-whiteboard-keeps-queued-activation-without-reviving-entering-command");
		RequestWhiteboardActive(false);
		require(!activationGuard(), "close-intent-rejects-queued-activation-before-phase-handoff");
		RequestWhiteboardActive(true);
		require(!activationGuard() && WhiteboardActivationGuard(
			whiteboardInputEpoch.load(std::memory_order_acquire))(),
			"new-whiteboard-epoch-cannot-revive-old-activation");
		RequestWhiteboardActive(false);
		setProbePhase(WhiteboardPhase::Exiting);
		require(!activationGuard() && !WhiteboardActivationGuard(
			whiteboardInputEpoch.load(std::memory_order_acquire))(), "exiting-whiteboard-rejects-activation");
		const auto returningEpoch = PptSceneWorkspaceEpoch();
		require(returningEpoch != 0 && returningEpoch != primaryEpoch && PptInputWorkspaceEpoch() == 0,
			"exiting-accepts-current-primary-scene-while-business-input-remains-closed");
		require(!CompletePresentationExitStateMode(primaryTool.revision, returningEpoch) &&
			!ChangeStateModeToSelectionIfRevision(primaryTool.revision, returningEpoch) &&
			StateModeTransitionRevision() == primaryTool.revision &&
			sameState(GetStateModeSnapshot(), primaryTool.state),
			"exiting-scene-authority-does-not-authorize-ppt-tool-tail");
		PublishProductWorkspace(Bridge::Workspace::Desktop, [returningEpoch]() noexcept
		{ return PptSceneWorkspaceEpoch() == returningEpoch; });
		PublishProductWorkspace(Bridge::Workspace::Whiteboard, oldWhiteboardGuard);
		require(bridge.Snapshot().workspace == Bridge::Workspace::Desktop,
			"retired-whiteboard-publication-cannot-overwrite-current-return");
		setProbePhase(WhiteboardPhase::Inactive);
		require(PptSceneWorkspaceEpoch() == returningEpoch && PptInputWorkspaceEpoch() == returningEpoch
			&& !oldPrimaryGuard(), "whiteboard-roundtrip-never-revives-old-primary-publisher");
		require(!CompletePresentationExitStateMode(primaryTool.revision, primaryEpoch) &&
			!ChangeStateModeToSelectionIfRevision(primaryTool.revision, primaryEpoch) &&
			StateModeTransitionRevision() == primaryTool.revision,
			"whiteboard-roundtrip-retires-ppt-exit-tool-effect-with-unchanged-mode-revision");
		ChangeStateModeToShape();
		require(!ChangeStateModeToSelectionIfRevision(primaryTool.revision, returningEpoch) &&
			GetStateModeSnapshot().StateModeSelect == StateModeSelectEnum::IdtShape,
			"current-workspace-does-not-authorize-stale-ppt-exit-tool-revision");
		require(CompletePresentationExitStateMode(StateModeTransitionRevision(), returningEpoch) &&
			GetStateModeSnapshot().StateModeSelect == StateModeSelectEnum::IdtSelection,
			"current-primary-ppt-exit-retains-normal-selection-tail");
		// 返回 PPT 时先恢复工具，再由带当前场景身份的 target/clear 提交 Presentation。
		setProbePhase(WhiteboardPhase::Exiting);
		require(SwitchWorkspaceStateMode(Workspace::Presentation, false, nullptr, nullptr,
			[returningEpoch]() noexcept { return PptSceneWorkspaceEpoch() == returningEpoch; }) &&
			bridge.Snapshot().workspace == Workspace::Desktop,
			"exiting-restores-ppt-tool-before-complete-scene");
		(void)PublishPrimaryPresentationTarget(nullptr, returningEpoch);
		require(bridge.Snapshot().workspace == Workspace::Desktop &&
			!bridge.Snapshot().presentationTarget, "stopped-host-cannot-publish-target-without-real-transition-fence");
		stage = "workspace-failure-wait-and-source-preview";
		{
			WorkspaceHandoff source;
			source.committed = Workspace::Whiteboard;
			require(!ShouldRestoreWhiteboardSourcePreview(source, Inkeys::Window::CommandPhase::Executing) &&
				ShouldRestoreWhiteboardSourcePreview(source, Inkeys::Window::CommandPhase::Rejected),
				"source-preview-restores-only-after-owner-rollback-retires");
			source.committedMinimized = true;
			require(!ShouldRestoreWhiteboardSourcePreview(source, Inkeys::Window::CommandPhase::Rejected),
				"minimized-source-does-not-force-background-visible");
			source.committed = Workspace::Desktop;
			require(!ShouldRestoreWhiteboardSourcePreview(source, Inkeys::Window::CommandPhase::Rejected),
				"desktop-source-does-not-create-whiteboard-preview");

			HostRuntimeSnapshot progress;
			progress.readyOutputRevision = 4;
			progress.presentedContentRevision = 7;
			WorkspaceWaitTracker tracker;
			const auto start = WorkspaceWaitTracker::Clock::time_point{};
			const auto at = [start](int ms) { return start + std::chrono::milliseconds(ms); };
			require(!tracker.Stalled(WorkspaceWaitStage::TargetReady, progress, 12, false, at(0)) &&
				!tracker.Stalled(WorkspaceWaitStage::TargetReady, progress, 12, false, at(0)) &&
				!tracker.Stalled(WorkspaceWaitStage::TargetReady, progress, 12, false, at(249)) &&
				tracker.Stalled(WorkspaceWaitStage::TargetReady, progress, 12, false, at(250)),
				"extra-native-wakes-preserve-250ms-no-progress-budget");
			++progress.presentedContentRevision;
			require(!tracker.Stalled(WorkspaceWaitStage::TargetReady, progress, 12, false, at(300)) &&
				!tracker.Stalled(WorkspaceWaitStage::TargetReady, progress, 12, false, at(549)),
				"new-presented-content-resets-wait-progress");
			require(tracker.Stalled(WorkspaceWaitStage::TargetReady, progress, 12, false, at(550)),
				"same-new-content-remains-stalled-at-original-cadence");
			progress.presentationUiReady = Bridge::PresentationReadyIdentity{};
			progress.presentationUiReady->targetRevision = 9;
			require(!tracker.Stalled(WorkspaceWaitStage::TargetReady, progress, 12, false, at(600)),
				"ppt-ui-ready-identity-is-real-progress");
			require(!tracker.Stalled(WorkspaceWaitStage::FinalBackgroundActive, progress, 12, false, at(600)),
				"background-stage-change-does-not-fail-in-place");
			require(!tracker.Stalled(WorkspaceWaitStage::FinalBackgroundActive, progress, 12, true, at(600)),
				"background-ready-change-resets-wait-progress");
		}
		stage = "workspace-async-native-and-progress";
		{
			using namespace Inkeys::Window;
			CommandPhase phase = CommandPhase::Queued;
			bool physicalCurrent = true;
			unsigned cancels = 0;
			WorkspaceWindowPrepared payload;
			payload.policy.revision = 17;
			WorkspaceWindowReceipt receipt{17, CommandReceipt{[&] { return phase; }, [&]
			{
				++cancels;
				if (phase == CommandPhase::Queued) phase = CommandPhase::Cancelled;
			}}, [&] { return std::optional(payload); }, [&] { return physicalCurrent; }};
			require(receipt && !receipt.Prepared() && !receipt.Current() && WorkspaceNativePending(receipt.Phase()),
				"queued-prepare-receipt-is-not-native-ready");
			phase = CommandPhase::Executing;
			require(!CancelWorkspaceNativeAndRetired(receipt) && cancels == 1 && !receipt.Prepared(),
				"cancelled-executing-receipt-retains-native-rollback-responsibility");
			std::uint64_t currentPolicy = 17, currentProduct = 3, currentHost = 4;
			WorkspaceNativeOperation operation{receipt, payload.policy, [&]
			{ return currentPolicy == 17 && currentProduct == 3 && currentHost == 4; }, true};
			operation.submittedAt = WorkspaceHandoff::Clock::time_point{};
			require(!operation.NoteExecutingTimeout(operation.submittedAt + std::chrono::milliseconds(1499)) &&
				operation.NoteExecutingTimeout(operation.submittedAt + std::chrono::milliseconds(1500)) &&
				!operation.NoteExecutingTimeout(operation.submittedAt + std::chrono::milliseconds(5000)) &&
				receipt.Phase() == CommandPhase::Executing,
				"executing-timeout-charges-once-at-1500ms-without-dropping-owner-responsibility");
			phase = CommandPhase::Committed;
			require(operation.CanConsume(true) && !operation.CanConsume(false),
				"complete-receipt-cannot-be-consumed-as-prepare");
			++currentProduct;
			require(!operation.CanConsume(true), "product-change-rejects-late-complete");
			--currentProduct; ++currentHost;
			require(!operation.CanConsume(true), "host-generation-change-rejects-late-complete");
			--currentHost; ++currentPolicy;
			require(!operation.CanConsume(true), "workspace-policy-change-rejects-late-complete");
			phase = CommandPhase::Rejected;
			require(CancelWorkspaceNativeAndRetired(receipt) && !receipt.Current() && !receipt.Prepared(),
				"only-terminal-retirement-permits-replacement-after-native-failure");
			phase = CommandPhase::Committed;
			require(receipt.Current() && receipt.Prepared()->policy.revision == 17,
				"committed-prepare-publishes-exact-captured-payload");
			physicalCurrent = false;
			require(!receipt.Current(), "late-native-receipt-cannot-confirm-new-hwnd-or-safety-generation");
			WorkspaceHandoff handoff;
			(void)handoff.Request(Workspace::Whiteboard);
			require(!handoff.Commit(handoff.revision, true, true, receipt.Current()) && handoff.pending,
				"full-ui-and-host-ready-still-require-current-native-complete");
			const auto before = ProductRuntimeSnapshot();
			const auto beforeBridge = bridge.Snapshot();
			NotifyProductWorkspaceProgress();
			const auto after = ProductRuntimeSnapshot();
			require(after.runtimeRevision != before.runtimeRevision && !after.running &&
				after.firstFrameReady == before.firstFrameReady && after.readyOutputRevision == before.readyOutputRevision &&
				after.presentedContentRevision == before.presentedContentRevision && after.presentationUiReady == before.presentationUiReady &&
				bridge.Snapshot().revision == beforeBridge.revision,
				"external-workspace-progress-wakes-without-fabricating-host-or-ui-ready");
			require(WaitForProductRuntimeRevision(before.runtimeRevision, 0, true) &&
				!WaitForProductRuntimeRevision(after.runtimeRevision, 0, true),
				"stopped-host-workspace-wait-retains-completion-before-wait");
			const auto applied = settingOwnerAppliedState.load(std::memory_order_acquire);
			SyncDraw3State();
			require(settingOwnerAppliedState.load(std::memory_order_acquire) == applied &&
				draw3PresentationRetryPending.load(std::memory_order_acquire) &&
				ProductRuntimeSnapshot().runtimeRevision != after.runtimeRevision,
				"ui-tool-sync-publishes-native-intent-without-synchronous-window-application");
		}
		stage = "workspace-legacy-restart-callback";
		{
			unsigned restarts = 0;
			ConfigureProductLegacyRestart([&] { ++restarts; return true; });
			RequestProductLegacyRecovery(nullptr);
			require(TryProductLegacyRestart() && restarts == 1 && !TryProductLegacyRestart(),
				"stopped-host-legacy-restart-runs-once-and-retires-request");
			ConfigureProductLegacyRestart([] { return false; });
			RequestProductLegacyRecovery(nullptr);
			require(!TryProductLegacyRestart(), "failed-legacy-restart-retains-failure-instead-of-faking-ready");
		}
		setProbePhase(WhiteboardPhase::Inactive);
		require(SwitchWorkspaceStateMode(Workspace::Desktop), "restore-desktop-after-scene-tests");
		require(!ProductRunning() && !Inkeys::Window::GetService().Running(), "startup-stays-isolated");
		std::fputs("[StateModeProduction] PASS: workspace tool isolation, cross-entry, repeated intent, rapid switch, late callbacks, slot isolation, laser memory, operation modes, clickthrough toggle/freezing, shortcut roundtrip, clear transaction\n", stderr);
		return 0;
	}
	catch (const char* check)
	{
		std::fprintf(stderr, "[StateModeProduction] FAIL: stage=%s check=%s\n", stage, check);
	}
	catch (...)
	{
		std::fprintf(stderr, "[StateModeProduction] FAIL: stage=%s exception\n", stage);
	}
	return 1;
}

void StateMonitoring()
{
	using namespace Inkeys::Drawing::Draw3;
	using namespace Inkeys::Window;
	auto& service = GetService();
	service.SetWorkspaceProgressObserver(NotifyProductWorkspaceProgress);
	struct ProgressSubscription
	{
		Service& service;
		~ProgressSubscription() { service.SetWorkspaceProgressObserver({}); }
	} progressSubscription{service};
	auto snapshot = ProductRuntimeSnapshot();
	std::uint64_t runtimeRevision = snapshot.runtimeRevision;
	std::uint64_t observedHostGeneration = 0;
	std::uint64_t preparedRevision = 0;
	std::uint64_t preparedProductRevision = 0;
	std::uint64_t preparedHostGeneration = 0;
	std::uint64_t uiPreparedRevision = 0;
	std::optional<WorkspaceWindowPrepared> prepared;
	std::optional<WorkspaceNativeOperation> nativeOperation;
	std::optional<WorkspaceWindowReceipt> completedNative;
	WorkspaceWaitTracker waitTracker;
	bool pageSwitching = false;
	bool requestedPreviousPage = false;
	std::size_t requestedPageIndex = 0;
	std::uint64_t pageCommandCountBeforeRequest = 0;
	while (!offSignal)
	{
		// 同一通知版本覆盖 Host、背景与原生回执，停止态安全退场也能被真实完成即时唤醒。
		(void)WaitForProductRuntimeRevision(runtimeRevision, 250, true);
		if (offSignal) break;
		if (!ReadWorkspaceHandoff().pending) ReconcileSettingOwnerDesiredState();
		snapshot = ProductRuntimeSnapshot();
		const bool runtimeChanged = snapshot.runtimeRevision != runtimeRevision;
		runtimeRevision = snapshot.runtimeRevision;
		auto request = ConsumeNativeWhiteboardRequests();
		// Host 重启不能继承旧消费者的 Frame/工具/fence 回执。
		if (ProductRunning() && snapshot.hostGeneration != observedHostGeneration)
		{
			observedHostGeneration = snapshot.hostGeneration;
			request = RequestWorkspace(request.desired, request.minimized, true);
		}
		if (request.desired != Workspace::Whiteboard && request.desired != CurrentPrimaryWorkspace())
			request = RequestWorkspace(CurrentPrimaryWorkspace());
		// PPT 恰在最小化请求后开始时，撤销尚未实际执行的最小化，保持原白板视图；不抢前台。
		if (request.pending && request.desired == Workspace::Whiteboard && request.minimized &&
			PptSessionActive() && !service.WhiteboardWindowGroupMinimized())
			request = RequestWorkspace(Workspace::Whiteboard, false, false, false);
		if (whiteboardViewChanged.exchange(false, std::memory_order_acq_rel) && !request.pending &&
			request.desired == Workspace::Whiteboard)
			request = RequestWorkspace(Workspace::Whiteboard, service.WhiteboardWindowGroupMinimized());

		if (nativeOperation && (!request.pending || request.failed || !nativeOperation->stillDesired()))
		{
			// Cancel 只撤意图；Executing 仍持有真实恢复责任，必须先等其退休再开始新窗口事务。
			if (!CancelWorkspaceNativeAndRetired(nativeOperation->receipt)) continue;
			nativeOperation.reset(); prepared.reset(); completedNative.reset();
		}
		if (nativeOperation && nativeOperation->NoteExecutingTimeout())
		{
			FailWorkspaceRequest(request.revision);
			request = ReadWorkspaceHandoff();
		}
		if (completedNative && !completedNative->Current())
		{ completedNative.reset(); prepared.reset(); }

		if (!request.pending)
		{
			if (runtimeChanged || draw3PresentationRetryPending.load(std::memory_order_acquire))
				ReconcileDraw3Presentation();
			if (request.committed != Workspace::Whiteboard || request.committedMinimized) continue;
			if (pageSwitching)
			{
				const std::uint64_t completed = requestedPreviousPage
					? snapshot.previousPageCommandCount
					: snapshot.nextPageCommandCount;
				if (snapshot.currentPageIndex == requestedPageIndex ||
					completed != pageCommandCountBeforeRequest)
					pageSwitching = false;
			}
			if (!pageSwitching && !snapshot.whiteboardLoadPending && snapshot.workspace == Workspace::Whiteboard)
			{
				const bool previous = whiteboardPreviousRequested.exchange(
					false, std::memory_order_acq_rel);
				const bool next = whiteboardNextRequested.exchange(
					false, std::memory_order_acq_rel);
				if (previous && snapshot.currentPageIndex > 0)
				{
					requestedPageIndex = snapshot.currentPageIndex - 1;
					requestedPreviousPage = true;
					pageCommandCountBeforeRequest = snapshot.previousPageCommandCount;
					pageSwitching = Inkeys::Drawing::Draw3::PublishProductCommand(
						Inkeys::Drawing::Draw3::Bridge::CommandType::PreviousPage) ==
						Inkeys::Drawing::Draw3::Bridge::CommandResult::Accepted;
				}
				else if (next)
				{
					requestedPageIndex = snapshot.currentPageIndex + 1;
					requestedPreviousPage = false;
					pageCommandCountBeforeRequest = snapshot.nextPageCommandCount;
					pageSwitching = Inkeys::Drawing::Draw3::PublishProductCommand(
						Inkeys::Drawing::Draw3::Bridge::CommandType::NextPage) ==
						Inkeys::Drawing::Draw3::Bridge::CommandResult::Accepted;
				}
			}
			Inkeys::UI::Whiteboard::PublishPageSnapshot(
				static_cast<int>(snapshot.currentPageIndex + 1),
				static_cast<int>(snapshot.pageCount), pageSwitching || snapshot.whiteboardLoadPending,
				snapshot.whiteboardTheme.backgroundRgb, static_cast<int>(snapshot.whiteboardTheme.pattern),
				snapshot.whiteboardUnsaved, snapshot.whiteboardGeneration);
			continue;
		}

		const auto guard = [epoch = request.revision]() noexcept { return WorkspaceRequestCurrent(epoch); };
		if (!request.Ready(WorkspaceHandoff::Clock::now())) continue;
		if (preparedRevision != request.revision)
		{
			prepared.reset(); completedNative.reset();
			preparedRevision = request.revision;
			preparedProductRevision = 0;
		}
		// fatal 消费者可能已停：当前完整故障 token 允许安全隐藏退场，不能假装 normal ready。
		auto desired = ProductHost().ProductBridge().Snapshot();
		const bool currentFault = request.desired != Workspace::Whiteboard && snapshot.hostGeneration != 0 && snapshot.recovery.revision != 0;
		const bool fatalSafetyExit = currentFault && Draw3FatalRecoveryMatches(snapshot, desired);
		const bool stoppedSafetyExit = currentFault && !ProductRunning() && Draw3RecoveryNeedsUiRelease(snapshot, desired);
		const bool stoppedOrFatalSafetyExit = fatalSafetyExit || stoppedSafetyExit;
		if (stoppedSafetyExit && TryProductLegacyRestart())
		{
			snapshot = ProductRuntimeSnapshot();
			observedHostGeneration = 0;
			request = RequestWorkspace(request.desired, request.minimized, true);
			continue;
		}
		if (!CloseWorkspaceAdmission(request) && !stoppedOrFatalSafetyExit) continue;
		const auto admitted = ReadWorkspaceHandoff();
		if (admitted.revision != request.revision) continue;
		request.hostGeneration = admitted.hostGeneration;
		if (!stoppedOrFatalSafetyExit && (request.hostGeneration == 0 || snapshot.hostGeneration != request.hostGeneration)) continue;
		if (preparedHostGeneration != request.hostGeneration)
		{
			prepared.reset(); completedNative.reset();
			preparedHostGeneration = request.hostGeneration;
		}
		if (!guard()) continue;

		if (uiPreparedRevision != request.revision)
		{
			// 新意图只收尾一次；同 ticket 的原生失败不反复折叠用户打开的面板。
			Inkeys::UI::Whiteboard::CancelPointerCapture();
			Inkeys::UI::Bar::CollapseAuxiliaryPanels(true);
			Inkeys::UI::Whiteboard::PublishExpandedLayoutTarget(request.desired == Workspace::Whiteboard);
			if (request.whiteboardInvolved)
				Inkeys::UI::Whiteboard::PublishPageState(static_cast<int>(snapshot.currentPageIndex + 1), static_cast<int>(snapshot.pageCount), true);
			SetProductActivationAllowed(false);
			pageSwitching = false;
			uiPreparedRevision = request.revision;
		}
		if (!stoppedOrFatalSafetyExit)
		{
			(void)SwitchWorkspaceStateMode(request.desired, false, nullptr, nullptr, guard);
			desired = ProductHost().ProductBridge().Snapshot();
			if (desired.workspace != request.desired) continue; // PPT 由可信 session/target 发布。
		}
		bool basicControl = false;
		std::optional<Inkeys::Business::PptSessionToken> preparedSession;
		if (request.desired == Workspace::Presentation) preparedSession = Inkeys::Business::CapturePptInputSession();
		if (request.desired == Workspace::Presentation && !desired.presentationTarget)
		{
			std::optional<Inkeys::Business::PptSessionToken> control;
			{ std::scoped_lock lock(workspaceHandoffMutex); control = basicControlSession; }
			basicControl = control && preparedSession && Inkeys::Business::MatchesPptSession(*control, *preparedSession);
			if (basicControl) preparedSession = control;
		}
		const bool protectedExit = request.desired != Workspace::Whiteboard &&
			Draw3RecoveryNeedsUiRelease(snapshot, desired);
		const bool safeRelease = stoppedOrFatalSafetyExit || protectedExit;
		const WorkspaceWindowPolicy policy{ request.revision, request.desired == Workspace::Whiteboard,
			desired.selectionMode, basicControl || safeRelease || (!request.minimized && request.desired != Workspace::Whiteboard &&
				desired.selectionMode && snapshot.workspace == request.desired && !snapshot.currentPageHasContent && snapshot.auxiliaryFullFrameClean), request.minimized };
		const auto nativeGuard = [epoch = request.revision, safeRelease, expected = snapshot.recovery,
			revision = desired.revision, generation = snapshot.hostGeneration, request, desired, preparedSession]() noexcept
		{
			if (!WorkspaceRequestCurrent(epoch)) return false;
			if (request.desired != Workspace::Whiteboard && !WorkspacePrimaryIdentityMatches(request, desired, preparedSession,
				Inkeys::Business::CapturePptInputSession())) return false;
			const auto current = ProductRuntimeSnapshot();
			return current.hostGeneration == generation && current.recovery == expected &&
				(safeRelease || (current.running && !current.consumerFatal)) &&
				ProductHost().ProductBridge().Snapshot().revision == revision;
		};
		const auto nativeFailed = [&](CommandPhase phase)
		{
			if (!guard()) return;
			// 取消/真实几何变化只欠重新准备，不属于当前 Native API 故障预算。
			if (phase != CommandPhase::Cancelled && phase != CommandPhase::Expired)
				FailWorkspaceRequest(request.revision);
			prepared.reset(); completedNative.reset();
			if (ShouldRestoreWhiteboardSourcePreview(request, phase))
			{
				SetWhiteboardFreezeSurfaceOwned(true);
				Inkeys::UI::Whiteboard::PublishExpandedLayoutTarget(true);
				Inkeys::UI::Whiteboard::PublishPageSnapshot(
					static_cast<int>(snapshot.currentPageIndex + 1), static_cast<int>(snapshot.pageCount), false,
					snapshot.whiteboardTheme.backgroundRgb, static_cast<int>(snapshot.whiteboardTheme.pattern),
					snapshot.whiteboardUnsaved, snapshot.whiteboardGeneration);
				Inkeys::UI::Whiteboard::PublishActive(true, request.revision);
			}
		};
		if (nativeOperation && (nativeOperation->policy != policy || !nativeOperation->stillDesired()))
		{
			if (!CancelWorkspaceNativeAndRetired(nativeOperation->receipt)) continue;
			nativeOperation.reset(); prepared.reset(); completedNative.reset();
		}
		if (!policy.whiteboard)
		{
			Inkeys::UI::Whiteboard::PublishActive(false, request.revision);
			const bool backgroundReady = Inkeys::UI::Whiteboard::BackgroundMatchesActive(false, request.revision);
			if (!backgroundReady && waitTracker.Stalled(WorkspaceWaitStage::BackgroundInactive,
				snapshot, request.revision, backgroundReady)) FailWorkspaceRequest(request.revision);
			if (!backgroundReady) continue;
		}
		if (prepared && (prepared->policy != policy || preparedProductRevision != desired.revision))
		{ prepared.reset(); completedNative.reset(); }
		if (!prepared)
		{
			if (!nativeOperation)
			{
				if (policy.whiteboard)
				{
					SetWhiteboardFreezeSurfaceOwned(true);
					Inkeys::UI::Freeze::SetWhiteboardActive(true);
					FreezePPT = false;
				}
				auto receipt = service.RequestPrepareWorkspaceWindowPolicy(policy, nativeGuard);
				if (!receipt) { nativeFailed(CommandPhase::Rejected); continue; }
				nativeOperation = WorkspaceNativeOperation{std::move(receipt), policy, nativeGuard};
			}
			const auto phase = nativeOperation->receipt.Phase();
			if (WorkspaceNativePending(phase)) continue;
			auto operation = std::move(*nativeOperation); nativeOperation.reset();
			if (phase != CommandPhase::Committed) { nativeFailed(phase); continue; }
			if (!operation.CanConsume(false)) continue;
			prepared = operation.receipt.Prepared();
			if (!prepared) { nativeFailed(CommandPhase::Rejected); continue; }
			preparedProductRevision = desired.revision;
			// Prepare 回执之后才读取实际目标 geometry；owner 从不等待 Host/背景/UI。
		}
		if (!guard()) continue;

		const auto& output = Bridge::SelectionUsesAuxiliaryOutput(desired.selectionMode, request.desired)
			? prepared->presentation.bounds : prepared->primary.bounds;
		if (!WorkspaceTargetReady(request, desired, snapshot, basicControl, safeRelease, output))
		{
			if (waitTracker.Stalled(WorkspaceWaitStage::TargetReady, snapshot, request.revision, false))
				FailWorkspaceRequest(request.revision);
			continue;
		}

		if (policy.whiteboard)
		{
			Inkeys::UI::Whiteboard::PublishPageSnapshot(static_cast<int>(snapshot.currentPageIndex + 1),
				static_cast<int>(snapshot.pageCount), true, snapshot.whiteboardTheme.backgroundRgb,
				static_cast<int>(snapshot.whiteboardTheme.pattern), snapshot.whiteboardUnsaved, snapshot.whiteboardGeneration);
			Inkeys::UI::Whiteboard::PublishActive(true, request.revision);
			const bool backgroundReady = request.minimized || Inkeys::UI::Whiteboard::BackgroundPrepared(request.revision);
			if (!backgroundReady && waitTracker.Stalled(WorkspaceWaitStage::BackgroundPrepared,
				snapshot, request.revision, backgroundReady)) FailWorkspaceRequest(request.revision);
			if (!backgroundReady) continue;
		}
		else if (!Inkeys::UI::Whiteboard::BackgroundMatchesActive(false, request.revision)) continue;
		if (!completedNative)
		{
			if (!nativeOperation)
			{
				const auto focusBefore = Inkeys::Business::CapturePptFocusRequest().foreground;
				const bool focusMayFallBack = !policy.whiteboard && service.WhiteboardWindowMode() &&
					(ForegroundMatchesWindowRole(focusBefore, WindowRole::Freeze) || ForegroundMatchesWindowRole(focusBefore, WindowRole::Drawpad));
				auto receipt = service.RequestCompleteWorkspaceWindowPolicy(policy, *prepared, nativeGuard);
				if (!receipt) { nativeFailed(CommandPhase::Rejected); continue; }
				nativeOperation = WorkspaceNativeOperation{std::move(receipt), policy, nativeGuard, true, focusBefore, focusMayFallBack};
			}
			const auto phase = nativeOperation->receipt.Phase();
			if (WorkspaceNativePending(phase)) continue;
			auto operation = std::move(*nativeOperation); nativeOperation.reset();
			if (phase != CommandPhase::Committed) { nativeFailed(phase); continue; }
			if (!operation.CanConsume(true)) { prepared.reset(); continue; }
			completedNative = std::move(operation.receipt);
			// 前台基线在本次 Complete 入队前捕获，仅其真实提交回执允许一次受控 fallback。
			if (!policy.whiteboard) RebaseWhiteboardReturnFocus(operation.focusBefore, operation.focusMayFallBack);
		}
		if (policy.whiteboard && !request.minimized &&
			!Inkeys::UI::Whiteboard::BackgroundMatchesActive(true, request.revision))
		{
			if (waitTracker.Stalled(WorkspaceWaitStage::FinalBackgroundActive, snapshot, request.revision, false))
				FailWorkspaceRequest(request.revision);
			continue;
		}
		std::unique_lock finalModeLock(stateModeTransitionMutex);
		if (!nativeGuard() || !completedNative->Current()
			|| (!safeRelease && ProductHost().ProductBridge().Snapshot().revision != desired.revision)) continue;
		const auto completedRuntime = ProductRuntimeSnapshot();
		const bool completedTargetReady = (request.desired == Workspace::Whiteboard ||
			WorkspacePrimaryIdentityMatches(request, desired, preparedSession, Inkeys::Business::CapturePptInputSession())) &&
			WorkspaceTargetReady(request, desired, completedRuntime, basicControl, safeRelease, output);
		// 一个提交点：Bar/工具/基线观察同一版本；新请求在锁外继续保留其自己的 fence。
		if (!CommitWorkspaceHandoffLocked(request.revision, completedTargetReady, true, true,
			stoppedOrFatalSafetyExit)) continue;
		finalModeLock.unlock();
		Inkeys::UI::Whiteboard::PublishPageState(static_cast<int>(completedRuntime.currentPageIndex + 1),
			static_cast<int>(completedRuntime.pageCount), false);
		Inkeys::UI::Bar::SetWhiteboardActive(policy.whiteboard);
		Inkeys::UI::Bar::RefreshToolState();
		Inkeys::UI::Freeze::SetWhiteboardActive(policy.whiteboard);
		if (!policy.whiteboard) SetWhiteboardFreezeSurfaceOwned(false);
		Inkeys::UI::Ppt::PublishPresentationVisible(request.desired == Workspace::Presentation);
		SetProductActivationAllowed(policy.whiteboard && !request.minimized);
		// 最小化继续禁止新 Down；恢复会获得新的奇数版本并重取真实输出。
		if (!request.minimized) (void)SetProductWorkspaceTransitionPending(request.revision, false, request.hostGeneration);
		if (request.activate && !request.minimized)
			(void)service.RequestWhiteboardActivation(WhiteboardActivationGuard(request.revision));
		if (!policy.whiteboard)
			(void)CompleteWhiteboardReturnFocus(Inkeys::Business::QueuePptFocusRequest);
		prepared.reset(); completedNative.reset();
	}
	// 退出后恢复责任仍由 Window owner 保留，不能在状态线程析构时伪造退休。
	if (nativeOperation) nativeOperation->receipt.Cancel();
}

bool GetStateMode_Discard(StateModeStruct_Discard* stateModeInfo)
{
	if (!stateModeInfo) return false;
	const auto stateMode = GetStateModeSnapshot();
	stateModeInfo->brushWidth = GetPenWidth(stateMode);
	stateModeInfo->brushColor = GetPenColor(stateMode);
	if (stateMode.StateModeSelect == StateModeSelectEnum::IdtPen)
		stateModeInfo->brushMode = stateMode.Pen.ModeSelect == PenModeSelectEnum::IdtPenHighlighter1 ? 2.0f : 1.0f;
	else if (stateMode.StateModeSelect == StateModeSelectEnum::IdtShape)
		stateModeInfo->brushMode = stateMode.Shape.ModeSelect == ShapeModeSelectEnum::IdtShapeRectangle1 ? 4.0f : 3.0f;
	else
		return false;
	return true;
}

int RunPenWidthStateProductionTest() noexcept
{
    try
    {
        using namespace Inkeys::Business::PenWidth;
        const auto require = [](bool ok) { if (!ok) throw 1; };
        setlist.paintDevice = 0;
        Context large{ 1, 0, 264.0, 2560, 1440 };
        ApplyPenWidthContext(large);
        RestorePenWidthSelections({}, {}, {});
        ChangeStateModeToPenTool(PenToolSelectionEnum::SoftPen);
        require(std::abs(GetPenWidthMetrics(GetStateModeSnapshot()).idealCanvasWidthPx - 16.0 / 3.0) < 1e-9);
        auto revision = StateModeTransitionRevision();
        require(SetPenWidthPresetIfRevision(1, revision, false));
        require(GetPenWidthSelection(GetStateModeSnapshot()).source == Source::Preset);
        require(!SetPenWidthIfRevision(9.0, revision, false));
        require(SetPenWidth(1.9393939393939394, false));
        const auto explicitState = GetStateModeSnapshot();
        const auto enteringDisplayRevision = StateModeTransitionRevision();
        large.effectiveDpi = 96.0;
        require(ApplyPenWidthContext(large));
        require(!SetPenWidthIfRevision(42.0, enteringDisplayRevision, false));
        require(explicitState.penWidthContext.effectiveDpi == 264.0
            && GetStateModeSnapshot().penWidthContext.effectiveDpi == 96.0);
        require(GetPenWidthSelection(GetStateModeSnapshot()).explicitDip == GetPenWidthSelection(explicitState).explicitDip);
        revision = StateModeTransitionRevision();
        require(!ApplyPenWidthContext(large) && revision == StateModeTransitionRevision());
        require(!SetPenWidth(0.0, false) && !SetPenWidth(-1.0, false) &&
            !SetPenWidth(std::numeric_limits<double>::quiet_NaN(), false) &&
            !SetPenWidth(std::numeric_limits<double>::infinity(), false) && !SetPenWidth(MaximumDip + 1, false));
        ChangeStateModeToPenTool(PenToolSelectionEnum::HardPen);
        require(GetPenWidthSelection(GetStateModeSnapshot()).explicitDip == GetPenWidthSelection(explicitState).explicitDip);
        ChangeStateModeToPenTool(PenToolSelectionEnum::Laser);
        require(GetPenWidthMetrics(GetStateModeSnapshot()).resolvedWidthDip == 5.0);
        std::puts("pen-width-state-production-test: PASS");
        return 0;
    }
    catch (...) { std::puts("pen-width-state-production-test: FAIL"); return 1; }
}
