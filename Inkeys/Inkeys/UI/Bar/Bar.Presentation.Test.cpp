module;

#include "../../../IdtMain.h"
#include "../../../IdtConfiguration.h"
#include "../../../IdtI18n.h"
#include "../../../IdtState.h"
#include "../../../resource.h"
#include "../../Helper/FailedCleanupDeadline.h"
#include "../../Helper/ShutdownSupervisor.h"
#include "../../Drawing/Draw3/Draw3.Product.h"
#include "../../Window/Window.Legacy.hpp"
#include "Bar.Presentation.Source.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <limits>
#include <optional>
#include <ostream>
#include <streambuf>
#include <thread>

module Inkeys.UI.Bar;
import :Main;
import :Theme;
import Inkeys.Other.Config;
import Inkeys.Other.Inputs;
import Inkeys.Text.Font;
import Inkeys.Window;
import Inkeys.Message;
import Inkeys.Display;
import Inkeys.UI.RenderPipeline;

namespace Inkeys::UI::Bar
{
	// 消息类型留在 named module；普通 Source header 只定义数字 DTO。
	Message::Reply DispatchAuthorizedFixtureIndex(HWND, UINT, WPARAM, LPARAM) noexcept;

	namespace
	{
		using Clock = std::chrono::steady_clock;
		constexpr std::uint32_t NoInput = static_cast<std::uint32_t>(Ui3FixtureMaxInputs);
		enum SourceFlags : std::uint32_t
		{
			Posted = 1, Received = 2, Enqueued = 4, Consumed = 8,
			DownAllowed = 16, CommitAllowed = 32, CallbackAllowed = 64,
			SkippedAlreadyOpen = 128, SkippedNoContact = 256, Failed = 512,
		};
		struct SourceEntry
		{
			std::atomic<std::uint32_t> flags = 0;
			Ui3FixtureBoundPoint bound;
			Ui3FixtureWireValue wire;
			std::int64_t sentTicks = 0, receiveTicks = 0, consumeTicks = 0;
		};
		enum class JobKind { CopyGoal, ReadPixels };
		struct OwnerJob
		{
			HANDLE event = nullptr;
			std::atomic<bool> done = true;
			JobKind kind = JobKind::CopyGoal;
			Ui3FiniteAccepted goal;
			Ui3FiniteTargetRecord outcome;
			Ui3FixturePixelReceipt pixel;
			bool succeeded = false;
		};
		struct FixtureState
		{
			explicit FixtureState(const Ui3FixtureAuthorization& cap) noexcept
				: authorization(&cap), table(CompiledUi3FixtureInputs(static_cast<Ui3FiniteScene>(cap.Input().scene))),
				clocks(cap.Input().capture == 1), runSerial(cap.Input().nonceLo ? cap.Input().nonceLo : cap.Input().nonceHi) {}
			const Ui3FixtureAuthorization* const authorization;
			const std::span<const Ui3FixtureInputV1> table;
			const bool clocks;
			const std::uint64_t runSerial;
			std::optional<Ui3FinitePublication> publication;
			std::optional<Ui3FiniteObserver> observer;
			Ui3SvgProbe svg{ runSerial };
			std::array<SourceEntry, Ui3FixtureMaxInputs> entries{};
			std::array<Ui3FiniteAccepted, Ui3FixtureExpectedSteps> completedGoals{};
		std::array<HANDLE, 4> directoryLeases{};
			OwnerJob job;
			Ui3FiniteTargetRecord warmEnd, measuredEnd;
			Ui3FixtureReadyValue bootstrap;
			Ui3FiniteSignature finalSignature;
			std::unique_ptr<std::uint8_t[]> pixels;
		std::unique_ptr<double[]> callbackStatistics, batchStatistics;
			std::size_t fixedBytes = 0, rawBytes = 0, pixelCapacity = 0, totalBudgetedBytes = 0;
			std::atomic<HWND> window = nullptr;
			std::atomic<DWORD> windowThread = 0, interactionThread = 0;
			std::atomic<std::uint32_t> nextReceive = 0, nextConsume = 0, contact = NoInput;
			std::atomic<std::uint32_t> received = 0, enqueued = 0, consumed = 0;
			std::atomic<std::uint32_t> failure = 0;
			std::atomic<bool> sourceOpen = false, stopping = false, sourceDone = false, equivalenceReady = false;
			std::thread source, interaction;
			std::uint32_t windowCurrent = NoInput, windowDown = NoInput;
			std::uint64_t completed = 0;
			bool sourceJoined = false, interactionJoined = false, displayStarted = false;
			bool pipelineAttempted = false, windowAttempted = false, renderingAttempted = false, comOwned = false;
			bool displayTrackingAttempted = false, bindingInstalled = false, outputsWritten = false;
		std::int64_t scheduleOriginTicks = 0, measuredBeginTicks = 0;
		std::int64_t readbackBeginTicks = 0, readbackEndTicks = 0;
		std::uint32_t backend = 0, featureLevel = 0;
		};
		std::atomic<FixtureState*> installed = nullptr;
		thread_local FixtureState* interactionState = nullptr;
		thread_local std::uint32_t currentConsumed = NoInput;
		thread_local Ui3FiniteOwnerContext finiteContext;

		std::int64_t ReadTimingTicks() noexcept { return Clock::now().time_since_epoch().count(); }
		void Fail(FixtureState& state, Ui3FixtureSourceFailure reason) noexcept
		{
			std::uint32_t empty = 0;
			(void)state.failure.compare_exchange_strong(empty, static_cast<std::uint32_t>(reason), std::memory_order_acq_rel);
			const auto thread = GetCurrentThreadId();
			const auto index = interactionState == &state ? currentConsumed
				: thread == state.windowThread.load(std::memory_order_acquire) ? state.windowCurrent : NoInput;
			if (index < state.table.size()) state.entries[index].flags.fetch_or(Failed, std::memory_order_release);
		}
		FixtureState* AuthorizedState() noexcept
		{
			auto* state = installed.load(std::memory_order_acquire);
			return state && IsAuthorizedUi3Fixture(*state->authorization) ? state : nullptr;
		}
		void PublishStage(FixtureState& state, Ui3FixtureStage stage) noexcept
		{
			InterlockedExchange(reinterpret_cast<LONG*>(&state.authorization->Packet().stage), static_cast<LONG>(stage));
		}
		bool FrozenInputsMatch(const Ui3FiniteSignature& a, const Ui3FiniteSignature& b) noexcept
		{
			return IsUi3FiniteSignatureValid(a) && IsUi3FiniteSignatureValid(b)
				&& (a.flags & ~3u) == (b.flags & ~3u) && (b.flags & 64u) != 0 && (b.flags & 3u) != 3u
				&& a.stateMode == b.stateMode && a.penMode == b.penMode && a.toolRevision == b.toolRevision
				&& a.penColorRgb == b.penColorRgb && a.penWidthBits == b.penWidthBits
				&& a.thicknessView == b.thicknessView && a.darkStyle == b.darkStyle
				&& a.displaySerial == b.displaySerial && a.dpi == b.dpi && a.configZoomBits == b.configZoomBits;
		}
		bool HasFailed(const FixtureState& state) noexcept { return state.failure.load(std::memory_order_acquire) != 0; }
		bool ScheduleContinue(const FixtureState& state) noexcept
		{
			return !HasFailed(state) && !state.stopping.load(std::memory_order_acquire) && !offSignal;
		}

		// 单个在途 job 保活所有借用；超时先 Close，继续等真正结束或原监督终止本进程。
		bool WaitOwnerJob(FixtureState& state) noexcept
		{
			if (WaitForSingleObject(state.job.event, 2000) != WAIT_OBJECT_0)
			{
				Fail(state, Ui3FixtureSourceFailure::Control);
				state.stopping.store(true, std::memory_order_release);
				SetOffSignal(1);
				while (WaitForSingleObject(state.job.event, 100) != WAIT_OBJECT_0) {}
			}
			return state.job.done.load(std::memory_order_acquire) && state.job.succeeded;
		}
		bool PostOwnerJob(FixtureState& state, JobKind kind, const Ui3FiniteAccepted& goal = {})
		{
			if (!state.job.done.load(std::memory_order_acquire) || !ResetEvent(state.job.event)) return false;
			state.job.kind = kind;
			state.job.goal = goal;
			state.job.succeeded = false;
			state.job.done.store(false, std::memory_order_release);
			bool posted = false;
			try
			{
				posted = RenderPipeline::PostControl([&state]() noexcept
				{
					bool succeeded = false;
					try
					{
						if (state.job.kind == JobKind::CopyGoal && state.observer)
						{
							const auto& target = state.job.goal;
							succeeded = state.observer->CopyCompletedOutcomeForCurrentOwner(target.runSerial,
								target.stepId, target.sourceSequence, target.revision, state.job.outcome);
						}
						else if (state.job.kind == JobKind::ReadPixels)
							succeeded = CaptureAuthorizedFixtureFinalBgra(*state.authorization, state.measuredEnd,
								{ state.pixels.get(), state.pixelCapacity }, state.job.pixel);
					}
					catch (...) { succeeded = false; }
					state.job.succeeded = succeeded;
					state.job.done.store(true, std::memory_order_release);
					// SetEvent 是最后一次借用操作，句柄直到 Scheduler 真 join 才关闭。
					(void)SetEvent(state.job.event);
				});
			}
			catch (...) { posted = false; }
			if (!posted)
			{
				state.job.done.store(true, std::memory_order_release);
				return false;
			}
			return WaitOwnerJob(state);
		}

		bool PostInput(FixtureState& state, std::uint32_t index, Clock::time_point due, bool teardown = false) noexcept
		{
			while (Clock::now() < due)
			{
				if (!teardown && !ScheduleContinue(state)) return false;
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			auto& entry = state.entries[index];
			if (entry.flags.load(std::memory_order_acquire) != 0) return false;
			entry.sentTicks = state.clocks ? ReadTimingTicks() : 0;
			entry.flags.store(Posted, std::memory_order_release);
			if (!PostMessageW(state.window.load(std::memory_order_acquire), Ui3FixtureIndexMessage, index, 0))
			{
				entry.flags.fetch_or(Failed, std::memory_order_release);
				Fail(state, Ui3FixtureSourceFailure::Post);
				return false;
			}
			const auto limit = teardown ? Clock::now() + std::chrono::milliseconds(250) : due + std::chrono::milliseconds(750);
			while ((entry.flags.load(std::memory_order_acquire) & Consumed) == 0)
			{
				if ((!teardown && !ScheduleContinue(state)) || Clock::now() >= limit)
				{
					Fail(state, Ui3FixtureSourceFailure::Deadline);
					return false;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			return true;
		}
		void CancelOwnContact(FixtureState& state) noexcept
		{
			state.stopping.store(true, std::memory_order_release);
			const auto index = static_cast<std::uint32_t>(state.table.size() - 1);
			if (state.contact.load(std::memory_order_acquire) == NoInput)
			{
				if (state.entries[index].flags.load(std::memory_order_acquire) == 0)
					state.entries[index].flags.store(SkippedNoContact, std::memory_order_release);
				return;
			}
			// 只取消真实自有 contact；保留 row 不改写 step/坐标，sidecar 沿对应 Down。
			(void)PostInput(state, index, Clock::now(), true);
		}
		bool WaitGoal(FixtureState& state, const Ui3FixtureInputV1& up, Clock::time_point deadline, Ui3FiniteAccepted& out) noexcept
		{
			while (ScheduleContinue(state) && Clock::now() < deadline)
			{
				Ui3FiniteAccepted accepted;
				if (state.publication->TryReadStable(accepted) && accepted.stepId == up.stepId
					&& accepted.sourceSequence == up.sourceSequence && accepted.runSerial == state.runSerial)
				{
					if (accepted.status != Ui3FiniteStatus::Accepted)
					{
						Fail(state, Ui3FixtureSourceFailure::Target);
						return false;
					}
					if (state.publication->CompletedRevision() == accepted.revision)
					{
						out = accepted;
						return true;
					}
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			Fail(state, Ui3FixtureSourceFailure::Deadline);
			return false;
		}
		void RunSource(FixtureState& state) noexcept
		{
			try
			{
				const auto origin = Clock::now(); // 必要固定调度钟；off 不导出为性能时刻。
				state.scheduleOriginTicks = state.clocks ? origin.time_since_epoch().count() : 0;
				std::size_t first = 0;
				if (state.authorization->Input().scene == static_cast<std::uint32_t>(Ui3FiniteScene::DrawAttribute))
				{
					first = 2;
					if ((state.bootstrap.initialStableSignature.flags & 1u) == 0)
					{
						state.entries[0].flags.store(SkippedAlreadyOpen, std::memory_order_release);
						state.entries[1].flags.store(SkippedAlreadyOpen, std::memory_order_release);
						state.nextReceive.store(2, std::memory_order_release);
						state.nextConsume.store(2, std::memory_order_release);
					}
					else
					{
						Ui3FiniteAccepted setup;
						if (!PostInput(state, 0, origin + Clock::duration(state.table[0].dueOffsetTicks))
							|| !PostInput(state, 1, origin + Clock::duration(state.table[1].dueOffsetTicks))
							|| !WaitGoal(state, state.table[1], origin + Clock::duration(state.table[2].dueOffsetTicks), setup)
							|| !PostOwnerJob(state, JobKind::CopyGoal, setup) || (state.job.outcome.accepted.signature.flags & 1u) != 0)
							Fail(state, Ui3FixtureSourceFailure::Target);
					}
				}
				for (std::size_t pair = 0; pair < Ui3FixtureExpectedSteps && ScheduleContinue(state); ++pair)
				{
					const auto down = static_cast<std::uint32_t>(first + pair * 2);
					const auto up = down + 1;
					const auto limit = origin + Clock::duration(state.table[up + 1].dueOffsetTicks);
					if (!PostInput(state, down, origin + Clock::duration(state.table[down].dueOffsetTicks))
						|| !PostInput(state, up, origin + Clock::duration(state.table[up].dueOffsetTicks))
						|| !WaitGoal(state, state.table[up], limit, state.completedGoals[pair])) break;
					++state.completed;
					if (pair + 1 == Ui3FixtureWarmSteps || pair + 1 == Ui3FixtureExpectedSteps)
					{
						if (!PostOwnerJob(state, JobKind::CopyGoal, state.completedGoals[pair]))
						{
							Fail(state, Ui3FixtureSourceFailure::Control);
							break;
						}
						if (pair + 1 == Ui3FixtureWarmSteps) state.warmEnd = state.job.outcome;
						else state.measuredEnd = state.job.outcome;
					}
					if (pair == Ui3FixtureWarmSteps && state.clocks)
						state.measuredBeginTicks = state.entries[down].receiveTicks;
				}
			}
			catch (...) { Fail(state, Ui3FixtureSourceFailure::Control); }
			CancelOwnContact(state);
			state.sourceOpen.store(false, std::memory_order_release);
			state.sourceDone.store(true, std::memory_order_release);
		}
	}

	bool AuthorizedUi3FixtureSourceInstalled() noexcept { return installed.load(std::memory_order_acquire) != nullptr; }
	bool AuthorizedFixtureEquivalenceReady(const Ui3FixtureAuthorization& cap) noexcept
	{
		const auto* state = AuthorizedState();
		return state && state->authorization == &cap && state->equivalenceReady.load(std::memory_order_acquire)
			&& state->sourceJoined && state->interactionJoined && state->completed == Ui3FixtureExpectedSteps
			&& state->contact.load(std::memory_order_acquire) == NoInput && !HasFailed(*state);
	}
	void RejectAuthorizedFixtureSource(Ui3FixtureSourceFailure reason) noexcept
	{
		if (auto* state = installed.load(std::memory_order_acquire)) Fail(*state, reason);
	}
	void EnterAuthorizedFixtureInteraction() noexcept
	{
		if (auto* state = AuthorizedState())
		{
			interactionState = state;
			currentConsumed = NoInput;
			state->interactionThread.store(GetCurrentThreadId(), std::memory_order_release);
			finiteContext = { state->publication ? &*state->publication : nullptr, {}, false, state->clocks,
				state->clocks ? &ReadTimingTicks : nullptr };
			(void)SetUi3FiniteOwnerContext(&finiteContext);
		}
	}
	void LeaveAuthorizedFixtureInteraction() noexcept
	{
		if (!interactionState) return;
		(void)SetUi3FiniteOwnerContext(nullptr);
		finiteContext = {};
		currentConsumed = NoInput;
		interactionState = nullptr;
	}
	bool ReceiveAuthorizedFixtureIndex(HWND window, WPARAM index, LPARAM extra, Ui3FixtureWireValue& out) noexcept
	{
		auto* state = AuthorizedState();
		if (!state || window != state->window.load(std::memory_order_acquire)
			|| GetCurrentThreadId() != state->windowThread.load(std::memory_order_acquire)
			|| index >= state->table.size() || extra != 0 || !state->observer)
		{
			RejectAuthorizedFixtureSource(Ui3FixtureSourceFailure::Authorization);
			return false;
		}
		DWORD process = 0;
		if (GetWindowThreadProcessId(window, &process) != GetCurrentThreadId() || process != GetCurrentProcessId())
		{
			Fail(*state, Ui3FixtureSourceFailure::Authorization);
			return false;
		}
		const auto& row = state->table[index];
		auto& entry = state->entries[index];
		const bool cancel = row.phase == 4 && index + 1 == state->table.size() && state->stopping.load(std::memory_order_acquire);
		if (!IsUi3FixtureInputValid(static_cast<Ui3FiniteScene>(state->authorization->Input().scene), row, index)
			|| entry.flags.load(std::memory_order_acquire) != Posted
			|| (!cancel && (!state->sourceOpen.load(std::memory_order_acquire) || HasFailed(*state) || offSignal
				|| index != state->nextReceive.load(std::memory_order_acquire))))
		{
			Fail(*state, Ui3FixtureSourceFailure::Order);
			return false;
		}
		entry.receiveTicks = state->clocks ? ReadTimingTicks() : 0;
		if (row.phase == 1)
		{
			Ui3FixtureReadyValue ready;
			if (state->contact.load(std::memory_order_acquire) != NoInput || !state->observer->TryReadReady(ready)
				|| !FrozenInputsMatch(state->bootstrap.initialStableSignature, ready.initialStableSignature)
				|| !TryBindUi3FixturePoint(row, ready, state->runSerial, entry.bound))
			{
				Fail(*state, Ui3FixtureSourceFailure::Ready);
				return false;
			}
			state->windowDown = static_cast<std::uint32_t>(index);
		}
		else
		{
			const auto down = state->contact.load(std::memory_order_acquire);
			if (down == NoInput || down != state->windowDown
				|| (!cancel && state->table[down].stepId != row.stepId))
			{
				Fail(*state, Ui3FixtureSourceFailure::Order);
				return false;
			}
			entry.bound = state->entries[down].bound;
		}
		entry.wire = { reinterpret_cast<std::uintptr_t>(window), static_cast<std::uint32_t>(row.phase == 1 ? WM_LBUTTONDOWN : WM_LBUTTONUP),
			row.phase == 1 ? 1u : 0u, 0, static_cast<std::uint32_t>(EM_MOUSE), entry.bound.x, entry.bound.y,
			static_cast<std::int16_t>(cancel ? SHRT_MIN + 3 : SHRT_MIN + 2) };
		state->windowCurrent = static_cast<std::uint32_t>(index);
		// fingerprint 在真实 Enqueue 前 release；consumer 不看 Window latestUp。
		entry.flags.fetch_or(Received, std::memory_order_release);
		state->received.fetch_add(1, std::memory_order_relaxed);
		out = entry.wire;
		return true;
	}
	void FinishAuthorizedFixtureEnqueue(std::uint32_t index, bool succeeded) noexcept
	{
		auto* state = AuthorizedState();
		if (!state || index >= state->table.size() || GetCurrentThreadId() != state->windowThread.load(std::memory_order_acquire)) return;
		if (!succeeded)
		{
			state->entries[index].flags.fetch_or(Failed, std::memory_order_release);
			Fail(*state, Ui3FixtureSourceFailure::Enqueue);
			return;
		}
		if (state->table[index].phase == 1) state->contact.store(index, std::memory_order_release);
		state->entries[index].flags.fetch_or(Enqueued, std::memory_order_release);
		state->enqueued.fetch_add(1, std::memory_order_relaxed);
		state->nextReceive.store(index + 1, std::memory_order_release);
	}
	bool ObserveAuthorizedFixtureDequeue(const Ui3FixtureWireValue& wire) noexcept
	{
		if (!AuthorizedUi3FixtureSourceInstalled()) return true;
		auto* state = AuthorizedState();
		if (!state || interactionState != state || GetCurrentThreadId() != state->interactionThread.load(std::memory_order_acquire))
		{
			RejectAuthorizedFixtureSource(Ui3FixtureSourceFailure::Authorization);
			return false;
		}
		auto index = state->nextConsume.load(std::memory_order_acquire);
		if (wire.wheel == SHRT_MIN + 3 && state->stopping.load(std::memory_order_acquire))
			index = static_cast<std::uint32_t>(state->table.size() - 1);
		if (index >= state->table.size()) { Fail(*state, Ui3FixtureSourceFailure::Wire); return false; }
		auto& entry = state->entries[index];
		// Enqueue 的消费者可能先于 Window 的成功回执运行；Received 已发布完整 fingerprint。
		const auto flags = entry.flags.load(std::memory_order_acquire);
		if ((flags & (Posted | Received | Consumed | Failed)) != (Posted | Received)
			|| !SameUi3FixtureWire(entry.wire, wire))
		{
			Fail(*state, Ui3FixtureSourceFailure::Wire);
			return false;
		}
		entry.consumeTicks = state->clocks ? ReadTimingTicks() : 0;
		currentConsumed = index;
		const auto& row = state->table[index];
		finiteContext.request = { row.stepId, row.sourceSequence, static_cast<Ui3FiniteScene>((row.flags >> 8) & 3u), entry.receiveTicks };
		finiteContext.requestAvailable = row.phase == 3;
		if (row.phase != 1) state->contact.store(NoInput, std::memory_order_release);
		state->consumed.fetch_add(1, std::memory_order_relaxed);
		state->nextConsume.store(index + 1, std::memory_order_release);
		entry.flags.fetch_or(Consumed, std::memory_order_release);
		return true;
	}
	void ObserveAuthorizedFixtureClear(BYTE filter, std::size_t removed) noexcept
	{
		if ((filter & EM_MOUSE) == 0 || removed == 0) return;
		if (auto* state = AuthorizedState())
		{
			for (std::size_t index = 0; index < state->table.size(); ++index)
			{
				const auto flags = state->entries[index].flags.load(std::memory_order_acquire);
				if ((flags & (Received | Consumed | Failed)) == Received)
				{
					Fail(*state, Ui3FixtureSourceFailure::Dropped);
					state->entries[index].flags.fetch_or(Failed, std::memory_order_release);
				}
			}
			Fail(*state, Ui3FixtureSourceFailure::Dropped);
		}
	}
	Ui3FixturePointResult ReadFixturePointerForCurrentOwner(POINT& point, bool& leftDown) noexcept
	{
		if (!AuthorizedUi3FixtureSourceInstalled()) return Ui3FixturePointResult::NotInstalled;
		auto* state = AuthorizedState();
		if (!state) return Ui3FixturePointResult::Unavailable;
		const auto thread = GetCurrentThreadId();
		const auto index = interactionState == state && thread == state->interactionThread.load(std::memory_order_acquire)
			? currentConsumed : thread == state->windowThread.load(std::memory_order_acquire) ? state->windowCurrent : NoInput;
		if (index >= state->table.size())
		{
			Fail(*state, Ui3FixtureSourceFailure::Point);
			return Ui3FixturePointResult::Unavailable;
		}
		const auto& wire = state->entries[index].wire;
		point = { wire.x, wire.y };
		leftDown = wire.buttons == 1;
		return Ui3FixturePointResult::Available;
	}
	bool PermitAuthorizedFixtureAction(Ui3FiniteScene action, Ui3FixtureActionPoint point) noexcept
	{
		if (!AuthorizedUi3FixtureSourceInstalled()) return true;
		auto* state = AuthorizedState();
		if (!state || interactionState != state || currentConsumed >= state->table.size() || HasFailed(*state)
			|| !IsUi3FixtureActionAllowed(state->table[currentConsumed], action, point))
		{
			RejectAuthorizedFixtureSource(Ui3FixtureSourceFailure::Action);
			return false;
		}
		Ui3FiniteSignature signature;
		try { signature = barUISet.ReadFiniteSignature(GetStateModeVersionedSnapshot()); }
		catch (...) { RejectAuthorizedFixtureSource(Ui3FixtureSourceFailure::UnsupportedState); return false; }
		const auto actionFlag = point == Ui3FixtureActionPoint::Down ? DownAllowed : point == Ui3FixtureActionPoint::Commit ? CommitAllowed : CallbackAllowed;
		const auto flags = state->entries[currentConsumed].flags.load(std::memory_order_acquire);
		const bool downPermitted = point == Ui3FixtureActionPoint::Down || (currentConsumed != 0
			&& (state->entries[currentConsumed - 1].flags.load(std::memory_order_acquire) & DownAllowed) != 0);
		if ((flags & actionFlag) != 0 || !downPermitted || (point == Ui3FixtureActionPoint::Callback && (flags & CommitAllowed) == 0)
			|| !FrozenInputsMatch(state->bootstrap.initialStableSignature, signature))
		{
			RejectAuthorizedFixtureSource(Ui3FixtureSourceFailure::Action);
			return false;
		}
		state->entries[currentConsumed].flags.fetch_or(actionFlag, std::memory_order_release);
		return true;
	}
	bool PermitAuthorizedFixturePointerStages(const Ui3FiniteSignature& signature) noexcept
	{
		if (!AuthorizedUi3FixtureSourceInstalled()) return true;
		auto* state = AuthorizedState();
		if (!state || interactionState != state || currentConsumed >= state->table.size() || HasFailed(*state)
			|| !FrozenInputsMatch(state->bootstrap.initialStableSignature, signature))
		{
			RejectAuthorizedFixtureSource(Ui3FixtureSourceFailure::UnsupportedState);
			return false;
		}
		return true;
	}
	bool AuthorizedFixtureWindowMessageMustBeHandled(HWND window, UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result) noexcept
	{
		if (!AuthorizedUi3FixtureSourceInstalled()) return false;
		auto* state = AuthorizedState();
		if (message == WM_CANCELMODE && state && state->stopping.load(std::memory_order_acquire)) return false;
		const bool input = (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST)
			|| (message >= WM_NCMOUSEMOVE && message <= WM_NCXBUTTONDBLCLK)
			|| message == WM_MOUSEHOVER || message == WM_MOUSELEAVE || message == WM_NCMOUSEHOVER || message == WM_NCMOUSELEAVE
			|| (message >= WM_KEYFIRST && message <= WM_KEYLAST) || message == WM_HOTKEY
			|| message == WM_APPCOMMAND || message == WM_TOUCH || message == WM_INPUT
			|| (message >= WM_POINTERUPDATE && message <= WM_POINTERLEAVE)
			|| message == WM_NCPOINTERUPDATE || message == WM_NCPOINTERDOWN || message == WM_NCPOINTERUP || message == WM_POINTERACTIVATE
			|| message == WM_POINTERCAPTURECHANGED || message == WM_POINTERWHEEL || message == WM_POINTERHWHEEL
			|| message == WM_GESTURE || message == WM_GESTURENOTIFY || message == WM_TIMER
			|| (message >= WM_APP + 0x31 && message <= WM_APP + 0x35)
			|| message == WM_CLOSE || message == WM_SYSCOMMAND || message == WM_CAPTURECHANGED || message == WM_CANCELMODE;
		if (message == WM_DPICHANGED || message == WM_DISPLAYCHANGE || message == WM_SETTINGCHANGE)
		{
			if (state && state->sourceOpen.load(std::memory_order_acquire)) Fail(*state, Ui3FixtureSourceFailure::UnexpectedSource);
			return false;
		}
		if (!input) return false;
		if (message == WM_TOUCH) (void)CloseTouchInputHandle(reinterpret_cast<HTOUCHINPUT>(lParam));
		if (message == WM_GESTURE) (void)CloseGestureInfoHandle(reinterpret_cast<HGESTUREINFO>(lParam));
		result = message == WM_INPUT ? DefWindowProcW(window, message, wParam, lParam)
			: message == WM_POINTERACTIVATE ? PA_NOACTIVATE : 0;
		if (!state || !state->stopping.load(std::memory_order_acquire)) RejectAuthorizedFixtureSource(Ui3FixtureSourceFailure::UnexpectedSource);
		return true;
	}

	namespace
	{
		bool SamePrivatePath(const std::wstring& a, const std::wstring& b) noexcept
		{
			return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
		}
		bool OrdinaryFileOrAbsent(const std::wstring& path) noexcept
		{
			HANDLE file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
				nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
			if (file == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
			BY_HANDLE_FILE_INFORMATION info{};
			const bool ordinary = GetFileInformationByHandle(file, &info)
				&& !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) && info.nNumberOfLinks == 1;
			CloseHandle(file);
			return ordinary;
		}
		bool LeasePrivateDirectories(FixtureState& state)
		{
			const auto& bin = state.authorization->BinaryDirectory();
			const std::array<std::wstring, 4> paths{ bin, bin + L"\\Inkeys", bin + L"\\Inkeys\\Config", bin + L"\\opt" };
			for (std::size_t index = 0; index < paths.size(); ++index)
			{
				auto& lease = state.directoryLeases[index];
				lease = CreateFileW(paths[index].c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
					nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
				BY_HANDLE_FILE_INFORMATION info{};
				if (lease == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(lease, &info)
					|| !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
			}
			return true;
		}
		void SeedLegacyDefaults()
		{
			// 与 Main 的合法默认消费者一致；只在本次 private child、所有 owner 开始前赋值。
			setlist.enableAutoUpdate = true; setlist.UpdateChannel = "LTS";
			#if defined(_M_ARM64) || defined(_M_ARM64EC)
			setlist.updateArchitecture = "arm64";
			#elif defined(_M_X64)
			setlist.updateArchitecture = "win64";
			#else
			setlist.updateArchitecture = "win32";
			#endif
			setlist.selectLanguage = 1; setlist.startUp = false;
			setlist.SetSkinMode = 0; setlist.SkinMode = 1; setlist.settingGlobalScale = 1.0f;
			setlist.topSleepTime = 3; setlist.RightClickClose = false;
			setlist.BrushRecover = true; setlist.RubberRecover = false;
			setlist.regularSetting.moveRecover = false; setlist.regularSetting.clickRecover = false;
			setlist.regularSetting.avoidFullScreen = false; setlist.regularSetting.teachingSafetyMode = 0;
			setlist.paintDevice = 1; setlist.disableRTS = false;
			setlist.liftStraighten = false; setlist.waitStraighten = true; setlist.pointAdsorption = true; setlist.smoothWriting = true;
			setlist.eraserSetting.eraserMode = 1; setlist.eraserSetting.eraserSize = 60; setlist.eraserSetting.savedFixedChoice = false;
			setlist.hideTouchPointer = false; setlist.saveSetting.enable = true; setlist.saveSetting.saveDays = 2;
			setlist.performanceSetting.preparationQuantity = 2; setlist.performanceSetting.drawpadFps = 72; setlist.performanceSetting.superDraw = false;
			setlist.presetSetting.memoryWidth = true; setlist.presetSetting.memoryColor = false; setlist.presetSetting.autoDefaultWidth = true;
			setlist.presetSetting.defaultBrush1Width = 3.0f; setlist.presetSetting.defaultHighlighter1Width = 35.0f;
			setlist.shortcutAssistant.correctLnk = true; setlist.shortcutAssistant.createLnk = false;
			setlist.plugInSetting.superTop.enable = false; setlist.plugInSetting.superTop.indicator = true;
			auto& shortcuts = setlist.component.shortcutButton;
			shortcuts.appliance.explorer = false; shortcuts.appliance.taskmgr = false; shortcuts.appliance.control = false;
			shortcuts.system.desktop = false; shortcuts.system.lockWorkStation = false;
			shortcuts.keyboard.keyboardesc = false; shortcuts.keyboard.keyboardAltF4 = false;
			shortcuts.rollCall.IslandCaller1 = false; shortcuts.rollCall.IslandCaller2 = false;
			shortcuts.rollCall.SecRandom1 = false; shortcuts.rollCall.SecRandom2 = false;
			shortcuts.rollCall.SecRandom2Compat = false; shortcuts.rollCall.NamePicker = false;
			shortcuts.linkage.classislandSettings = false; shortcuts.linkage.classislandProfile = false; shortcuts.linkage.classislandClassswap = false;
		}
		bool PrepareStorageAndDefaults(FixtureState& state)
		{
			const auto& input = state.authorization->Input();
			const auto descriptor = GetCompiledUi3FixtureSourceV1(static_cast<Ui3FiniteScene>(input.scene));
			if (descriptor.sourceHash != input.sourceHash || descriptor.trajectoryCount != input.trajectoryCount
				|| descriptor.expectedSteps != input.expectedSteps || descriptor.sourceVersion != input.sourceVersion
				|| state.table.empty() || installed.load(std::memory_order_acquire) || offSignal
				|| Inkeys::Drawing::Draw3::ProductRunning() || Window::GetService().Running() || RenderPipeline::IsInitialized()) return false;
			for (const auto& row : state.table)
				if (!IsUi3FixtureInputValid(static_cast<Ui3FiniteScene>(input.scene), row, row.index)) return false;
			constexpr auto sampleBytes = sizeof(RenderPipeline::RawCallbackSample) + sizeof(RenderPipeline::RawBatchSample);
			// 统计 scratch 也按峰值计入：两个 capacity 大小的 double 数组，功能为 16+16MiB。
			state.fixedBytes = sizeof(FixtureState) + CompiledUi3FixtureStorageBytes()
				+ sizeof(Ui3FixtureAuthorization) + sizeof(Ui3FixturePacketV1) + sizeof(Ui3FixtureFrozenInputV1) + 64 * 1024;
			state.fixedBytes += (state.authorization->Repository().capacity() + state.authorization->PrivateRoot().capacity()
				+ state.authorization->BinaryDirectory().capacity()) * sizeof(wchar_t);
			if (state.fixedBytes > Ui3FixtureFixedStorageBudget || input.capture > 1
				|| (!input.capture && input.capacity) || (input.capture && (input.capacity < 256 || input.capacity > 65536))) return false;
			const auto available = Ui3FixtureTotalStorageBudget - Ui3FixturePixelPayloadLimit - state.fixedBytes;
			if (input.capacity > available / (sampleBytes + 2 * sizeof(double))) return false;
			state.rawBytes = input.capacity * sampleBytes;
			state.totalBudgetedBytes = state.fixedBytes + state.rawBytes + input.capacity * 2 * sizeof(double) + Ui3FixturePixelPayloadLimit;
			state.pixelCapacity = Ui3FixturePixelPayloadLimit / 2;
			state.pixels = std::make_unique<std::uint8_t[]>(state.pixelCapacity);
			if (state.clocks)
			{
				state.callbackStatistics = std::make_unique<double[]>(input.capacity);
				state.batchStatistics = std::make_unique<double[]>(input.capacity);
			}
			state.job.event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			if (!state.job.event || !LeasePrivateDirectories(state)) return false;
			globalPath = state.authorization->BinaryDirectory() + L"\\";
			pluginPath = globalPath;
			const auto mainPath = state.authorization->BinaryDirectory() + L"\\Inkeys\\Config\\main.json";
			const auto deployPath = state.authorization->BinaryDirectory() + L"\\opt\\deploy.json";
			if (!SamePrivatePath(config.GetFilePath(), mainPath) || !OrdinaryFileOrAbsent(mainPath) || !OrdinaryFileOrAbsent(deployPath)) return false;
			SeedLegacyDefaults();
			config.ResetToDefaults();
			config.Experimental.Inkeys3.UI3.EdgeLighting.Enable = true;
			config.Experimental.Inkeys3.UI3.EdgeLighting.Dynamic = true;
			if (!config.Write()) return false;
			const auto deploy = CaptureSettingJson();
			if (deploy.size() > 64 * 1024 || !OrdinaryFileOrAbsent(deployPath) || !WriteSettingJson(deploy)) return false;
			// 需要日志调用者时使用无 sink 的本 child logger，不创建额外线程或外部文件。
			IDTLogger = std::make_shared<spdlog::logger>("ui3_fixture");
			SetAnimationOptions(config.Experimental.Inkeys3.UI3.Animation.Enable, config.Experimental.Inkeys3.UI3.Animation.SpeedRate);
			SetEdgeLightingOptions(true, true);
			SetDebugOptions(config.Experimental.Inkeys3.UI3.Debug.Enable, config.Experimental.Inkeys3.UI3.Debug.ShowFrameRate);
			if (state.clocks && !RenderPipeline::ConfigureRawCapture(input.capacity)) return false;
			FixtureState* empty = nullptr;
			state.bindingInstalled = installed.compare_exchange_strong(empty, &state, std::memory_order_acq_rel);
			return state.bindingInstalled;
		}
		bool BindRealSvgObjects(FixtureState& state)
		{
			std::array<BarUiSVGClass*, Ui3SvgCapacity> bound{};
			std::size_t count = 0;
			auto bind = [&](BarUiSVGClass& object, std::uint32_t tag)
			{
				if (std::find(bound.begin(), bound.begin() + count, &object) != bound.begin() + count) return true;
				if (count == bound.size() || !object.BindObservationTag(tag)) return false;
				bound[count++] = &object;
				return true;
			};
			for (const auto& [id, object] : barUISet.svgMap)
				if (!object || !bind(*object, 0x10000u + static_cast<std::uint32_t>(id))) return false;
			std::uint32_t ordinal = 0;
			for (auto* button : barUISet.barButtonSet.preset)
				if (button && button->iconKind == BarButtonIconKindEnum::Svg && !bind(button->icon, 0x20000u + ordinal++)) return false;
			auto extensions = barUISet.barButtonSet.GetExtensionRegistrations();
			if (extensions.size() > Ui3SvgCapacity) return false;
			std::sort(extensions.begin(), extensions.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
			for (const auto& registration : extensions)
				if (registration.button && registration.button->iconKind == BarButtonIconKindEnum::Svg
					&& !bind(registration.button->icon, 0x20000u + ordinal++)) return false;
			if (auto* button = barUISet.barButtonSet.GetMoreButton(); button && !bind(button->icon, 0x20000u + ordinal)) return false;
			return count != 0;
		}
		bool WaitBootstrap(FixtureState& state, bool interaction)
		{
			constexpr auto base = Ui3FiniteReadyRegistered | Ui3FiniteReadyTransaction | Ui3FiniteReadyLayoutStable | Ui3FiniteReadyAnchors;
			const auto required = base | (interaction ? Ui3FiniteReadyInteraction : 0u);
			const auto end = Clock::now() + std::chrono::seconds(10);
			while (!HasFailed(state) && !offSignal && Clock::now() < end)
			{
				Ui3FixtureReadyValue ready;
				if (state.observer->TryReadReady(ready) && (ready.flags & (required | Ui3FiniteReadyStopped)) == required
					&& ready.generation == state.runSerial && ready.committedCount && IsUi3FiniteSignatureValid(ready.initialStableSignature))
				{
					state.bootstrap = ready;
					return true;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			Fail(state, Ui3FixtureSourceFailure::Ready);
			return false;
		}
		bool Bootstrap(FixtureState& state)
		{
			PublishStage(state, Ui3FixtureStage::Initializing);
			if (!PrepareStorageAndDefaults(state) || !EnsureAuthorizedUi3FixtureDpiAwareness(*state.authorization)) return false;
			const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
			state.comOwned = SUCCEEDED(com);
			if (!state.comOwned) return false;
			state.displayStarted = true;
			if (!Display::Initialize()) return false;
			state.pipelineAttempted = true;
			if (FAILED(RenderPipeline::Initialize())) return false;
			{
				const auto epoch = RenderPipeline::GetDeviceEpoch();
				state.backend = static_cast<std::uint32_t>(epoch.backend);
				state.featureLevel = static_cast<std::uint32_t>(epoch.featureLevel);
			}
			IdtFontFileLoader::IsLoaderInitialized();
			IdtFontCollectionLoader::IsLoaderInitialized();
			constexpr std::array<UINT, 4> fonts{ IDR_TTF1, IDR_TTF7, IDR_TTF3, IDR_TTF8 };
			if (FAILED(RenderPipeline::InitializeFontCollection(IdtFontFileLoader::GetLoader(),
				IdtFontCollectionLoader::GetLoader(), fonts)) || !I18n::load(1, L"JSON", L"zh-CN")) return false;
			Window::WindowSpec spec;
			spec.role = Window::WindowRole::Bar;
			spec.className = L"Inkeys.Ui3.Fixture.Bar";
			spec.title = L"Inkeys UI3 owned fixture";
			spec.style = WS_POPUP | WS_CLIPCHILDREN;
			spec.exStyle = WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
			spec.visible = false;
			spec.windowProc = WindowProc();
			spec.messageCallback = &DispatchAuthorizedFixtureIndex;
			spec.created = [&state](HWND window)
			{
				DWORD process = 0;
				if (GetWindowThreadProcessId(window, &process) != GetCurrentThreadId() || process != GetCurrentProcessId())
					{ Fail(state, Ui3FixtureSourceFailure::Authorization); return; }
				floating_window = window;
				state.windowThread.store(GetCurrentThreadId(), std::memory_order_release);
				state.window.store(window, std::memory_order_release);
			};
			state.windowAttempted = true;
			{
				Shutdown::FailedCleanupDeadline cleanup(&Shutdown::PublishFatalFailedCleanupNoWait);
				cleanup.PrepareOrFatal();
				const bool started = Window::GetService().Start({ std::move(spec) }, cleanup.Signal());
				cleanup.CompleteOrFatal();
				if (!started || HasFailed(state)) return false;
			}
			if (!ChangeStateModeToPen() || !InitializeWindow(barUISet)) return false;
			{
				SvgObservationScope initialization(&state.svg, state.clocks, true);
				barUISet.spec.ConfigureLocalizedTypography();
				InitializeUI(barUISet);
				state.displayTrackingAttempted = true;
				barUISet.StartDisplayTracking();
				barUISet.barMedia.LoadFormat();
				barUISet.barButtonSet.PresetInitialization();
				barUISet.barButtonSet.RegisterBuiltInComponents();
				if (!OrdinaryFileOrAbsent(config.GetFilePath())) return false;
				barUISet.barButtonSet.Load();
				barUISet.barButtonSet.StateUpdate();
				SetContentStateUpdatesReady(true);
				barUISet.barState.PositionUpdate(barUISet.barStyle.zoom);
				if (!BindRealSvgObjects(state)) return false;
			}
			const auto initial = barUISet.ReadFiniteSignature(GetStateModeVersionedSnapshot());
			state.publication.emplace(state.runSerial, initial);
			state.observer.emplace(*state.publication);
			if (!state.observer->EnableBootstrapBaselineBeforeOwnersStart()) return false;
			state.observer->BindSvgProbeBeforeOwnersStart(&state.svg);
			if (SetActiveUi3FiniteObserver(&*state.observer) != nullptr) return false;
			state.renderingAttempted = true;
			if (!barUISet.Rendering() || !WaitBootstrap(state, false)) return false;
			// acquire 初始真 Fit/成功帧后才创建 Interact，plain frozen baseline 不再改写。
			state.interaction = std::thread([&state]() noexcept
			{
				try { barUISet.Interact(); }
				catch (...) { Fail(state, Ui3FixtureSourceFailure::UnsupportedState); }
			});
			if (!WaitBootstrap(state, true)) return false;
			PublishStage(state, Ui3FixtureStage::Ready);
			state.sourceOpen.store(true, std::memory_order_release);
			state.source = std::thread([&state]() noexcept { RunSource(state); });
			PublishStage(state, Ui3FixtureStage::Running);
			return true;
		}
		[[noreturn]] void KeepFailedOwnersAlive() noexcept
		{
			SetOffSignal(1);
			for (;;) Sleep(100); // 不展开仍有借用的栈，由原普通监督守截止。
		}
		void CleanupAndJoin(FixtureState& state)
		{
			state.stopping.store(true, std::memory_order_release);
			state.sourceOpen.store(false, std::memory_order_release);
			PublishStage(state, Ui3FixtureStage::Closing);
			// 正式 Close 必须早于每一个可能阻塞的 join、控制读回与离线 I/O。
			SetOffSignal(1);
			if (state.source.joinable()) state.source.join();
			state.sourceJoined = true;
			if (state.interaction.joinable()) state.interaction.join();
			state.interactionJoined = true;
			if (!state.job.done.load(std::memory_order_acquire)) (void)WaitOwnerJob(state);
			if (!HasFailed(state) && state.completed == Ui3FixtureExpectedSteps
				&& state.contact.load(std::memory_order_acquire) == NoInput
				&& state.measuredEnd.terminalStatus == Ui3FiniteStatus::CompletedLayoutAndSvg)
			{
				state.equivalenceReady.store(true, std::memory_order_release);
				state.readbackBeginTicks = state.clocks ? ReadTimingTicks() : 0;
				if (!PostOwnerJob(state, JobKind::ReadPixels)) Fail(state, Ui3FixtureSourceFailure::Control);
				state.readbackEndTicks = state.clocks ? ReadTimingTicks() : 0;
				state.equivalenceReady.store(false, std::memory_order_release);
			}
			// Unregister 只 drain 帧 callback；单个 control job 已真实结算才可 Seal/reset。
			if (!state.job.done.load(std::memory_order_acquire)) KeepFailedOwnersAlive();
			SetContentStateUpdatesReady(false);
			if (state.displayTrackingAttempted) barUISet.StopDisplayTracking();
			if (state.renderingAttempted) barUISet.StopRendering();
			if (state.windowAttempted) Window::GetService().StopAndJoin(); // overlay + empty-setting 两个 owner。
			if (state.displayStarted) Display::Shutdown();
			if (state.pipelineAttempted || state.clocks) RenderPipeline::Shutdown();
			if (state.observer)
			{
				state.observer->SealAfterRenderStopped();
				state.publication->AbsorbAfterOwnersStopped(state.observer->RecordsAfterRenderStopped());
			}
			state.finalSignature = barUISet.ReadFiniteSignature(GetStateModeVersionedSnapshot());
			(void)SetActiveUi3FiniteObserver(nullptr);
			if (state.bindingInstalled) installed.store(nullptr, std::memory_order_release);
			floating_window = nullptr;
			if (state.comOwned) { CoUninitialize(); state.comOwned = false; }
			PublishStage(state, Ui3FixtureStage::Joined);
		}

		class NewOutput final : public std::streambuf
		{
		public:
			NewOutput(const Ui3FixtureAuthorization& cap, const wchar_t* name)
			{
				setp(bytes_.data(), bytes_.data() + bytes_.size());
				if (!IsAuthorizedUi3Fixture(cap)) return;
				const auto path = cap.PrivateRoot() + L"\\" + name;
				file_ = CreateFileW(path.c_str(), GENERIC_WRITE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
					nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
				BY_HANDLE_FILE_INFORMATION info{};
				if (file_ != INVALID_HANDLE_VALUE && (!GetFileInformationByHandle(file_, &info)
					|| (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) || info.nNumberOfLinks != 1))
				{
					CloseHandle(file_);
					file_ = INVALID_HANDLE_VALUE;
				}
			}
			~NewOutput() override { if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_); }
			bool Valid() const noexcept { return file_ != INVALID_HANDLE_VALUE; }
			bool Close() noexcept
			{
				if (!Valid()) return false;
				const bool succeeded = sync() == 0 && FlushFileBuffers(file_);
				CloseHandle(file_);
				file_ = INVALID_HANDLE_VALUE;
				return succeeded;
			}
		protected:
			int sync() override
			{
				if (!Valid()) return -1;
				const auto size = static_cast<DWORD>(pptr() - pbase());
				DWORD written = 0;
				if (size && (!WriteFile(file_, pbase(), size, &written, nullptr) || written != size)) return -1;
				setp(bytes_.data(), bytes_.data() + bytes_.size());
				return 0;
			}
			int_type overflow(int_type value) override
			{
				if (sync() != 0) return traits_type::eof();
				if (!traits_type::eq_int_type(value, traits_type::eof())) { *pptr() = traits_type::to_char_type(value); pbump(1); }
				return traits_type::not_eof(value);
			}
		private:
			HANDLE file_ = INVALID_HANDLE_VALUE;
			std::array<char, 4096> bytes_{};
		};
		template<typename Writer>
		bool WriteOutput(FixtureState& state, const wchar_t* name, Writer&& writer)
		{
			NewOutput file(*state.authorization, name);
			if (!file.Valid()) return false;
			std::ostream out(&file);
			out << std::setprecision(17);
			writer(out);
			return out.good() && file.Close();
		}
		void Tick(std::ostream& out, bool valid, std::int64_t ticks)
		{
			if (valid) out << ticks; else out << "null";
		}
		void SignatureJson(std::ostream& out, const Ui3FiniteSignature& value)
		{
			out << "{\"flags\":" << value.flags << ",\"state_mode\":" << value.stateMode << ",\"pen_mode\":" << value.penMode
				<< ",\"pen_color_rgb\":" << value.penColorRgb << ",\"pen_width_bits\":" << value.penWidthBits
				<< ",\"main_side\":" << value.mainSide << ",\"primary_side\":" << value.primarySide
				<< ",\"thickness_view\":" << value.thicknessView << ",\"dark_style\":" << value.darkStyle << ",\"dpi\":" << value.dpi
				<< ",\"tool_revision\":" << value.toolRevision << ",\"display_serial\":" << value.displaySerial
				<< ",\"config_zoom_bits\":" << value.configZoomBits << ",\"valid_mask\":" << value.validMask << '}';
		}
		void PixelJson(std::ostream& out, const Ui3FixturePixelReceipt& pixel)
		{
			out << "{\"status\":" << pixel.status << ",\"generation\":" << pixel.generation << ",\"committed_attempt\":" << pixel.committedAttempt
				<< ",\"epoch\":" << pixel.epoch << ",\"surface\":" << pixel.surface << ",\"mutation_serial\":" << pixel.bufferMutationSerial
				<< ",\"invalidation_serial\":" << pixel.targetInvalidationSerial << ",\"bytes\":" << pixel.pixelBytes << ",\"width\":" << pixel.width
				<< ",\"height\":" << pixel.height << ",\"stride\":" << pixel.stride << ",\"source_x\":" << pixel.sourceX
				<< ",\"source_y\":" << pixel.sourceY << ",\"presentation_alpha\":" << pixel.presentationAlpha << '}';
		}
		void StatisticsJson(std::ostream& out, double* samples, std::size_t count, bool available)
		{
			out << "{\"available\":" << (available ? "true" : "false") << ",\"samples\":" << count << ",\"median_ms\":";
			if (!available || !count) { out << "null,\"p95_ms\":null,\"p99_ms\":null}"; return; }
			std::sort(samples, samples + count);
			out << (count % 2 ? samples[count / 2] : (samples[count / 2 - 1] + samples[count / 2]) / 2)
				<< ",\"p95_ms\":" << samples[(count * 95 + 99) / 100 - 1] << ",\"p99_ms\":";
			if (count < 1000) out << "null"; else out << samples[(count * 99 + 99) / 100 - 1];
			out << '}';
		}
		double Milliseconds(std::int64_t ticks) noexcept
		{
			return static_cast<double>(ticks) * Clock::period::num * 1000.0 / Clock::period::den;
		}
		const Ui3FiniteTargetRecord* FindOutcome(const FixtureState& state, std::uint32_t step) noexcept
		{
			if (!state.publication) return nullptr;
			const auto first = state.authorization->Input().scene == 2 ? 2u : 0u;
			const auto up = step == Ui3FixtureSetupStep ? 1u : first + (step - 1) * 2 + 1;
			if (up >= state.table.size()) return nullptr;
			const Ui3FiniteTargetRecord* found = nullptr;
			for (const auto& record : state.publication->RecordsAfterOwnerStopped())
				if (record.accepted.runSerial == state.runSerial && record.accepted.stepId == step
					&& record.accepted.sourceSequence == state.table[up].sourceSequence)
				{
					if (found) return nullptr;
					found = &record;
				}
			return found;
		}
		std::uint64_t CountComplete(const FixtureState& state) noexcept
		{
			std::uint64_t count = 0;
			for (std::uint32_t step = 1; step <= Ui3FixtureExpectedSteps; ++step)
			{
				const auto* row = FindOutcome(state, step);
				if (row && row->terminalStatus == Ui3FiniteStatus::CompletedLayoutAndSvg
					&& row->accepted.status == Ui3FiniteStatus::Accepted && row->trueBarAttemptSerial && row->epoch && row->surfaceSerial
					&& (!state.clocks || (row->timingValid && row->accepted.ownerReceiveTicks > 0
						&& row->accepted.ownerReceiveTicks <= row->accepted.acceptedTicks && row->accepted.acceptedTicks <= row->consumedTicks
						&& row->consumedTicks <= row->settledTicks && row->settledTicks <= row->finalCommitTicks))) ++count;
			}
			return count;
		}
		Ui3FiniteStatus PlannedOutcomeStatus(const FixtureState& state, std::uint32_t step) noexcept
		{
			if (const auto* row = FindOutcome(state, step)) return row->terminalStatus;
			const auto first = state.authorization->Input().scene == 2 ? 2u : 0u;
			const auto down = step == Ui3FixtureSetupStep ? 0u : first + (step - 1) * 2;
			if (down + 1 < state.table.size() && ((state.entries[down].flags.load() | state.entries[down + 1].flags.load()) & Posted))
				return Ui3FiniteStatus::SourceRejected;
			return Ui3FiniteStatus::Pending;
		}
		bool FunctionalResultValid(const FixtureState& state) noexcept
		{
			const auto received = state.received.load(std::memory_order_acquire);
			const auto& pixel = state.job.pixel;
			const bool pixelShape = pixel.width && pixel.height && pixel.width <= UINT32_MAX / 4
				&& pixel.stride == pixel.width * 4 && pixel.height <= state.pixelCapacity / pixel.stride
				&& pixel.pixelBytes == static_cast<std::uint64_t>(pixel.stride) * pixel.height;
			return !HasFailed(state) && CountComplete(state) == Ui3FixtureExpectedSteps && received >= 432
				&& received == state.enqueued.load(std::memory_order_acquire) && received == state.consumed.load(std::memory_order_acquire)
				&& pixelShape && pixel.status == 1 && pixel.pixelBytes > 0 && pixel.pixelBytes <= state.pixelCapacity
				&& pixel.generation == state.runSerial && pixel.presentationAlpha != 0 && pixel.presentationAlpha <= 255
				&& SameUi3FiniteSemanticSignature(state.finalSignature, state.measuredEnd.accepted.signature);
		}
		void OutcomeCsv(std::ostream& out, const FixtureState& state, std::uint32_t step)
		{
			const auto* row = FindOutcome(state, step);
			out << step << ',' << (step == Ui3FixtureSetupStep ? 1 : step <= Ui3FixtureWarmSteps ? 2 : 3) << ','
				<< static_cast<std::uint32_t>(PlannedOutcomeStatus(state, step)) << ','
				<< (row ? "retained" : "missing") << ',';
			const Ui3FiniteTargetRecord empty;
			const auto& value = row ? *row : empty;
			out << value.accepted.runSerial << ',' << value.accepted.sourceSequence << ',' << value.accepted.revision << ',' << value.accepted.publicationSerial
				<< ',' << static_cast<std::uint32_t>(value.accepted.scene) << ',' << static_cast<std::uint32_t>(value.accepted.status)
				<< ',' << value.epoch << ',' << value.surfaceSerial << ',' << value.trueBarAttemptSerial << ',';
			const bool timing = state.clocks && row && value.timingValid;
			Tick(out, timing, value.accepted.ownerReceiveTicks); out << ','; Tick(out, timing, value.accepted.acceptedTicks); out << ',';
			Tick(out, timing, value.consumedTicks); out << ','; Tick(out, timing, value.settledTicks); out << ','; Tick(out, timing, value.finalCommitTicks);
			out << ',' << value.pendingRoles << ',' << value.proofMask << ',' << value.failureFlags << ',' << value.reusedRevision
				<< ',' << value.requiredSvg << ',' << value.verifiedSvg << ',' << value.failedSvg << ',' << value.unverifiedSvg
				<< ',' << value.firstUnverifiedSvgTag << ',' << value.firstUnverifiedReason << ',' << value.accepted.signature.flags
				<< ',' << value.accepted.signature.stateMode << ',' << value.accepted.signature.penMode << ',' << value.accepted.signature.penColorRgb
				<< ',' << value.accepted.signature.penWidthBits << ',' << value.accepted.signature.mainSide << ',' << value.accepted.signature.primarySide
				<< ',' << value.accepted.signature.thicknessView << ',' << value.accepted.signature.darkStyle << ',' << value.accepted.signature.dpi
				<< ',' << value.accepted.signature.toolRevision << ',' << value.accepted.signature.displaySerial << ',' << value.accepted.signature.configZoomBits
				<< ',' << value.accepted.signature.validMask << '\n';
		}
		bool WriteSvg(FixtureState& state, const wchar_t* name, const Ui3SvgCounters& value, bool initialization)
		{
			return WriteOutput(state, name, [&](std::ostream& out)
			{
				out << "{\"scope\":\"" << (initialization ? "owned_initialization" : "render_all")
					<< "\",\"steady_phase_collected\":false,\"warm_phase_counters\":null,\"measured_phase_counters\":null"
					<< ",\"lookup_hit\":" << value.lookupHit << ",\"lookup_miss\":" << value.lookupMiss << ",\"create_attempt\":" << value.createAttempt
					<< ",\"create_success\":" << value.createSuccess << ",\"create_failure\":" << value.createFailure << ",\"parse_calls\":" << value.parseCalls
					<< ",\"parse_failure\":" << value.parseFailure << ",\"raster_calls\":" << value.rasterCalls << ",\"raster_failure\":" << value.rasterFailure
					<< ",\"upload_calls\":" << value.uploadCalls << ",\"upload_failure\":" << value.uploadFailure << ",\"draw_submit\":" << value.drawSubmit
					<< ",\"draw_rejected\":" << value.drawRejected << ",\"invalidation\":" << value.invalidation << ",\"replacement\":" << value.replacement
					<< ",\"capacity_evict\":" << value.capacityEvict << ",\"ready_entries\":" << value.readyEntries << ",\"logical_ready_bytes\":" << value.logicalReadyBytes
					<< ",\"unknown_ready_entries\":" << value.unknownReadyEntries << ",\"invalid\":" << value.invalid << ",\"clock_reads\":" << value.clockReads
					<< ",\"parse_ms\":";
				if (state.clocks) out << value.parseMs; else out << "null";
				out << ",\"raster_and_internal_geometry_ms\":"; if (state.clocks) out << value.rasterAndInternalGeometryMs; else out << "null";
				out << ",\"upload_ms\":"; if (state.clocks) out << value.uploadMs; else out << "null";
				out << ",\"draw_submit_ms\":"; if (state.clocks) out << value.drawSubmitMs; else out << "null";
				out << "}\n";
			});
		}
		bool InMeasuredInterval(const FixtureState& state, const RenderPipeline::RawCallbackSample& sample, std::uint64_t generation) noexcept
		{
			return state.measuredBeginTicks > 0 && state.measuredEnd.timingValid && generation && sample.client == RenderPipeline::Client::Bar
				&& sample.callbackGeneration == generation && sample.contextEpoch == state.measuredEnd.epoch && sample.validTime
				&& sample.startTicks >= state.measuredBeginTicks && sample.endTicks <= state.measuredEnd.finalCommitTicks && sample.endTicks >= sample.startTicks;
		}
		bool WriteRaw(FixtureState& state, const std::optional<RenderPipeline::RawCaptureReport>& raw, std::uint64_t generation)
		{
			bool succeeded = WriteOutput(state, L"raw-callbacks.csv", [&](std::ostream& out)
			{
				out << "run,batch,callback,client,generation,context_epoch,segment,result,requested,continued,retried,valid_time,frame_ticks,start_ticks,end_ticks,previous_active_callback,previous_active_commit,bar_stamp_status,previous_true_commit,success_serial,in_measured,after_end,bar_sampled,animation_advanced,present_attempted,ulw_attempted,ulw_succeeded,present_committed,present_deferred,backoff_skipped,failure_recovery_reset,present_failed,callback_exception,detailed_capture,has_stamp,bar_commit_ticks,bar_attempt,bar_epoch,attempt_frame,raw_dt,animation_dt,resource_hr,getdc_hr,releasedc_hr,enddraw_hr,ulw_error,failure_count,retry_frames,next_retry,frame_epoch,backend,target_width,target_height,capacity_width,capacity_height,viewport_left,viewport_top,viewport_right,viewport_bottom,source_x,source_y,display_capacity_zoom,zoom,light_flags";
				for (unsigned stage = 0; stage < static_cast<unsigned>(RenderPipeline::FrameStage::Count); ++stage) out << ",stage_" << stage << "_ms";
				out << ",rounded_hit,rounded_miss,rounded_create,rounded_failure,geometry_hit,geometry_miss,geometry_create,geometry_failure,rounded_create_ms,geometry_create_ms,exact_hit,slices";
				for (unsigned reason = 0; reason < static_cast<unsigned>(RenderPipeline::ExactFallback::Count); ++reason) out << ",exact_fallback_" << reason;
				out << '\n';
				if (!raw) return;
				for (const auto& sample : raw->callbacks)
				{
					const auto& frame = sample.frame;
					out << sample.runSerial << ',' << sample.batchSerial << ',' << sample.callbackSerial << ',' << static_cast<unsigned>(sample.client)
						<< ',' << sample.callbackGeneration << ',' << sample.contextEpoch << ',' << sample.activitySegment << ',' << static_cast<unsigned>(sample.result)
						<< ',' << sample.requested << ',' << sample.continued << ',' << sample.retried << ',' << sample.validTime << ',' << sample.frameTimeTicks
						<< ',' << sample.startTicks << ',' << sample.endTicks << ',';
					Tick(out, sample.hasPreviousActiveCallback, sample.previousActiveCallbackTicks); out << ',';
					Tick(out, sample.hasPreviousActiveCommit, sample.previousActiveCommitTicks); out << ',' << static_cast<unsigned>(sample.barCommitStatus) << ',';
					Tick(out, sample.hasPreviousTrueBarCommit, sample.previousTrueBarCommitTicks);
					out << ',' << sample.barSuccessSerial << ',' << InMeasuredInterval(state, sample, generation) << ','
						<< (state.measuredEnd.timingValid && sample.endTicks > state.measuredEnd.finalCommitTicks) << ',' << frame.barSampled << ',' << frame.animationAdvanced
						<< ',' << frame.presentAttempted << ',' << frame.ulwAttempted << ',' << frame.ulwSucceeded << ',' << frame.presentCommitted
						<< ',' << frame.presentDeferred << ',' << frame.backoffSkipped << ',' << frame.failureRecoveryReset << ',' << frame.presentFailed
						<< ',' << frame.callbackException << ',' << frame.detailedCaptureEnabled << ',' << frame.hasBarCommitStamp << ',';
					Tick(out, frame.hasBarCommitStamp, frame.barCommitTicks);
					out << ',' << frame.barAttemptSerial << ',' << frame.barCommitEpoch << ',' << frame.presentAttemptFrameSerial << ',' << frame.rawDtSeconds
						<< ',' << frame.animationDtSeconds << ',' << frame.resourceResult << ',' << frame.getDcResult << ',' << frame.releaseDcResult << ',' << frame.endDrawResult
						<< ',' << frame.ulwError << ',' << frame.failureCount << ',' << frame.retryDelayFrames << ',' << frame.nextRetryFrame << ',' << frame.epoch
						<< ',' << static_cast<unsigned>(frame.backend) << ',' << frame.targetSize.cx << ',' << frame.targetSize.cy << ',' << frame.capacitySize.cx << ',' << frame.capacitySize.cy
						<< ',' << frame.viewport.left << ',' << frame.viewport.top << ',' << frame.viewport.right << ',' << frame.viewport.bottom << ',' << frame.source.x << ',' << frame.source.y
						<< ',' << frame.displayCapacityZoom << ',' << frame.zoom << ',' << frame.lightFlags;
					for (auto time : frame.stageMs) out << ',' << time;
					const auto& light = frame.light;
					out << ',' << light.roundedParentHit << ',' << light.roundedParentMiss << ',' << light.roundedParentCreate << ',' << light.roundedParentFailure
						<< ',' << light.geometryParentHit << ',' << light.geometryParentMiss << ',' << light.geometryParentCreate << ',' << light.geometryParentFailure
						<< ',' << light.roundedParentCreateMs << ',' << light.geometryParentCreateMs << ',' << light.exactHit << ',' << light.slices;
					for (auto count : light.exactFallback) out << ',' << count;
					out << '\n';
				}
			});
			const bool batches = WriteOutput(state, L"raw-batches.csv", [&](std::ostream& out)
			{
				out << "run,batch,frame_ticks,begin_ticks,end_ticks,recovery_begin,recovery_end,work,requested,continued,retried,registered,executed,epoch,context_valid,recovery_attempted,recovery_succeeded,device_lost,stopped,valid_time,in_measured,after_end\n";
				if (!raw) return;
				for (const auto& sample : raw->batches)
				{
					const bool measured = state.measuredEnd.timingValid && sample.validTime && sample.contextEpoch == state.measuredEnd.epoch
						&& sample.beginTicks >= state.measuredBeginTicks && sample.endTicks <= state.measuredEnd.finalCommitTicks && sample.endTicks >= sample.beginTicks;
					out << sample.runSerial << ',' << sample.batchSerial << ',' << sample.frameTimeTicks << ',' << sample.beginTicks << ',' << sample.endTicks << ',';
					Tick(out, sample.recoveryAttempted, sample.recoveryStartTicks); out << ','; Tick(out, sample.recoveryAttempted, sample.recoveryEndTicks);
					out << ',' << sample.work << ',' << sample.requested << ',' << sample.continued << ',' << sample.retried << ',' << sample.registered << ',' << sample.executed
						<< ',' << sample.contextEpoch << ',' << sample.contextValid << ',' << sample.recoveryAttempted << ',' << sample.recoverySucceeded << ',' << sample.deviceLost
						<< ',' << sample.stopped << ',' << sample.validTime << ',' << measured << ','
						<< (state.measuredEnd.timingValid && sample.endTicks > state.measuredEnd.finalCommitTicks) << '\n';
				}
			});
			return succeeded && batches;
		}
		void WriteAllOutputs(FixtureState& state)
		{
			// Take 严格一次且在 Scheduler 真 join 后；off 没有 R 数组，也不补计量钟。
			std::optional<RenderPipeline::RawCaptureReport> raw;
			if (state.clocks) raw = RenderPipeline::TakeRawCapture();
			std::uint64_t generation = 0;
			if (raw)
			{
				for (const auto& sample : raw->callbacks)
					if (sample.client == RenderPipeline::Client::Bar && sample.barCommitStatus == RenderPipeline::BarCommitStampStatus::Valid
						&& sample.frame.barAttemptSerial == state.warmEnd.trueBarAttemptSerial && sample.frame.barCommitEpoch == state.warmEnd.epoch)
					{
						if (generation && generation != sample.callbackGeneration) Fail(state, Ui3FixtureSourceFailure::Target);
						generation = sample.callbackGeneration;
					}
			}
			if (state.clocks && (!raw || !raw->sealed || raw->schemaVersion != 2 || raw->callbackDropped || raw->batchDropped
				|| raw->invalidCallbacks || raw->invalidBatches || raw->invalidBarCommitStamps || !generation)) Fail(state, Ui3FixtureSourceFailure::Target);
			if (state.completed != CountComplete(state) || CountComplete(state) != Ui3FixtureExpectedSteps) Fail(state, Ui3FixtureSourceFailure::Target);
			auto keep = [&](bool written) { if (!written) Fail(state, Ui3FixtureSourceFailure::Output); };
			keep(WriteOutput(state, L"source-events.csv", [&](std::ostream& out)
			{
				out << "index,step,phase,anchor,offset_x_dip,offset_y_dip,flags,source_sequence,due_offset_ticks,base_serial,reserved0,reserved1,actual_flags,bound_commits,bound_attempt,bound_epoch,bound_surface,bound_mapping,x,y,wire_message,wire_buttons,wire_modifiers,wire_category,wire_wheel,sent_ticks,owner_receive_ticks,consume_ticks,send_jitter_ticks\n";
				const auto origin = state.scheduleOriginTicks;
				for (std::size_t index = 0; index < state.table.size(); ++index)
				{
					const auto& row = state.table[index]; const auto& entry = state.entries[index];
					const auto flags = entry.flags.load(std::memory_order_acquire);
					out << row.index << ',' << row.stepId << ',' << row.phase << ',' << row.anchor << ',' << row.offsetXDip << ',' << row.offsetYDip << ',' << row.flags
						<< ',' << row.sourceSequence << ',' << row.dueOffsetTicks << ',' << row.expectedBaseCommitSerial << ',' << row.reserved0 << ',' << row.reserved1
						<< ',' << flags << ',' << entry.bound.committedCount << ',' << entry.bound.attempt << ',' << entry.bound.epoch << ',' << entry.bound.surface << ',' << entry.bound.mapping
						<< ',' << entry.bound.x << ',' << entry.bound.y << ',' << entry.wire.message << ',' << entry.wire.buttons << ',' << entry.wire.modifiers << ',' << entry.wire.category << ',' << entry.wire.wheel << ',';
					Tick(out, state.clocks && (flags & Posted), entry.sentTicks); out << ',';
					Tick(out, state.clocks && (flags & Received), entry.receiveTicks); out << ',';
					Tick(out, state.clocks && (flags & Consumed), entry.consumeTicks); out << ',';
					Tick(out, state.clocks && origin && (flags & Posted), entry.sentTicks - origin - row.dueOffsetTicks); out << '\n';
				}
			}));
			keep(WriteOutput(state, L"finite-targets.csv", [&](std::ostream& out)
			{
				out << "planned_step,run_phase,terminal_status,retention,run,source,revision,publication,scene,accepted_status,epoch,surface,attempt,owner_receive_ticks,accepted_ticks,consumed_ticks,settled_ticks,final_commit_ticks,pending_roles,proof_mask,failure_flags,reused_revision,required_svg,verified_svg,failed_svg,unverified_svg,first_unverified_tag,first_unverified_reason,flags,state_mode,pen_mode,pen_color_rgb,pen_width_bits,main_side,primary_side,thickness_view,dark_style,dpi,tool_revision,display_serial,config_zoom_bits,valid_mask\n";
				if (state.authorization->Input().scene == 2) OutcomeCsv(out, state, Ui3FixtureSetupStep);
				for (std::uint32_t step = 1; step <= Ui3FixtureExpectedSteps; ++step) OutcomeCsv(out, state, step);
			}));
			keep(WriteSvg(state, L"svg-cold.json", state.svg.InitializationCountersAfterOwnerStopped(), true));
			keep(WriteSvg(state, L"svg-warm.json", state.svg.CountersAfterOwnerStopped(), false));
			if (state.clocks) keep(WriteRaw(state, raw, generation));
			const auto& pixel = state.job.pixel;
			if (pixel.status == 1 && pixel.pixelBytes && pixel.pixelBytes <= state.pixelCapacity)
			{
				keep(WriteOutput(state, L"equivalence.bgra", [&](std::ostream& out)
				{
					out.write(reinterpret_cast<const char*>(state.pixels.get()), static_cast<std::streamsize>(pixel.pixelBytes));
				}));
				keep(WriteOutput(state, L"BGRAhash", [&](std::ostream& out)
				{
					std::uint64_t hash = 14695981039346656037ULL;
					for (std::size_t index = 0; index < pixel.pixelBytes; ++index) hash = (hash ^ state.pixels[index]) * 1099511628211ULL;
					out << "fnv1a64 " << hash << " bytes " << pixel.pixelBytes << '\n';
				}));
			}
			else Fail(state, Ui3FixtureSourceFailure::Control);
			keep(WriteOutput(state, L"meta.json", [&](std::ostream& out)
			{
				const auto& input = state.authorization->Input();
				out << "{\"schema_version\":2,\"source_version\":" << input.sourceVersion << ",\"source_hash\":" << input.sourceHash
					<< ",\"trajectory_count\":" << input.trajectoryCount << ",\"scene\":" << input.scene << ",\"round\":" << input.round
					<< ",\"capture\":" << (state.clocks ? "true" : "false") << ",\"capacity\":" << input.capacity << ",\"run\":" << state.runSerial
					<< ",\"clock_period_num\":" << Clock::period::num << ",\"clock_period_den\":" << Clock::period::den
					<< ",\"step_ms\":1000,\"up_offset_ms\":20,\"setup_reserved\":" << (input.scene == 2 ? 2 : 0)
					<< ",\"warm_planned\":16,\"measured_planned\":200,\"edge_lighting\":true,\"dynamic_edge_lighting\":true"
					<< ",\"animation_enable\":true,\"animation_speed\":1,\"language\":\"zh-CN\",\"skin_mode\":1,\"legacy_scale\":1"
					<< ",\"legacy_component_switches\":\"all 16 false\",\"logger_sink\":\"none\",\"mouse_light_scene_covered\":false"
					<< ",\"backend\":" << state.backend << ",\"feature_level\":" << state.featureLevel << ",\"fixed_bytes\":" << state.fixedBytes
					<< ",\"raw_bytes\":" << state.rawBytes << ",\"destination_bytes\":" << state.pixelCapacity << ",\"readable_reserved_bytes\":" << Ui3FixturePixelPayloadLimit / 2
					<< ",\"total_budgeted_bytes\":" << state.totalBudgetedBytes << ",\"measurement_begin_ticks\":";
				Tick(out, state.clocks && state.measuredBeginTicks > 0, state.measuredBeginTicks);
				out << ",\"measured_end\":{\"run\":" << state.measuredEnd.accepted.runSerial << ",\"step\":" << state.measuredEnd.accepted.stepId
					<< ",\"source\":" << state.measuredEnd.accepted.sourceSequence << ",\"revision\":" << state.measuredEnd.accepted.revision
					<< ",\"attempt\":" << state.measuredEnd.trueBarAttemptSerial << ",\"epoch\":" << state.measuredEnd.epoch
					<< ",\"surface\":" << state.measuredEnd.surfaceSerial << ",\"final_commit_ticks\":";
				Tick(out, state.clocks && state.measuredEnd.timingValid, state.measuredEnd.finalCommitTicks);
				out << "},\"readback_begin_ticks\":"; Tick(out, state.clocks && state.readbackBeginTicks > 0, state.readbackBeginTicks);
				out << ",\"readback_end_ticks\":"; Tick(out, state.clocks && state.readbackEndTicks > 0, state.readbackEndTicks);
				out << ",\"readback_cost_scope\":\"after MeasuredEnd; excluded from performance\",\"pixel_receipt\":"; PixelJson(out, pixel);
				out << ",\"initial_signature\":"; SignatureJson(out, state.bootstrap.initialStableSignature);
				out << ",\"final_signature\":"; SignatureJson(out, state.finalSignature);
				out << ",\"child_pid\":" << GetCurrentProcessId() << ",\"window_thread\":" << state.windowThread.load()
					<< ",\"interaction_thread\":" << state.interactionThread.load() << ",\"contact_cleared\":" << (state.contact.load() == NoInput ? "true" : "false")
					<< ",\"window_start_attempted\":" << (state.windowAttempted ? "true" : "false") << ",\"pipeline_init_attempted\":" << (state.pipelineAttempted ? "true" : "false");
				out << ",\"source_joined\":" << (state.sourceJoined ? "true" : "false") << ",\"interaction_joined\":" << (state.interactionJoined ? "true" : "false")
					<< ",\"window_owners_joined\":true,\"scheduler_joined\":true,\"display_callbacks_drained\":true,\"source_failure\":" << state.failure.load(std::memory_order_acquire) << "}\n";
			}));
			keep(WriteOutput(state, L"summary.json", [&](std::ostream& out)
			{
				const auto counters = state.publication ? state.publication->CountersAfterOwnerStopped() : Ui3FiniteCounters{};
				const auto observer = state.observer ? state.observer->CountersAfterRenderStopped() : Ui3FiniteObserverCounters{};
				std::array<double, 200> targetTimes{};
				std::size_t valid = 0, warm = 0, measured = 0;
				std::array<std::uint64_t, static_cast<std::size_t>(Ui3FiniteStatus::Overflow) + 1> statuses{};
				for (std::uint32_t step = 1; step <= Ui3FixtureExpectedSteps; ++step)
				{
					const auto* row = FindOutcome(state, step);
					const auto status = static_cast<std::size_t>(PlannedOutcomeStatus(state, step));
					if (status < statuses.size()) ++statuses[status];
					if (!row || row->terminalStatus != Ui3FiniteStatus::CompletedLayoutAndSvg) continue;
					if (step <= Ui3FixtureWarmSteps) ++warm; else ++measured;
					if (step > Ui3FixtureWarmSteps && state.clocks && row->timingValid && row->finalCommitTicks >= row->accepted.ownerReceiveTicks)
						targetTimes[valid++] = Milliseconds(row->finalCommitTicks - row->accepted.ownerReceiveTicks);
				}
				out << "{\"schema_version\":2,\"result\":\"" << (FunctionalResultValid(state) ? "Passed" : "Failed") << "\",\"source_failure\":" << state.failure.load(std::memory_order_acquire)
					<< ",\"planned_steps\":216,\"warm_completed\":" << warm << ",\"measured_completed\":" << measured
					<< ",\"unverified_steps\":" << Ui3FixtureExpectedSteps - CountComplete(state) << ",\"target_status_counts\":[";
				for (std::size_t status = 0; status < statuses.size(); ++status) { if (status) out << ','; out << statuses[status]; }
				out << "],\"publication_seen\":" << counters.seen << ",\"publication_retained\":" << counters.retained << ",\"publication_dropped\":" << counters.dropped
					<< ",\"accepted\":" << counters.accepted << ",\"rejected\":" << counters.rejected << ",\"ambiguous\":" << counters.ambiguous
					<< ",\"no_change\":" << counters.noChange << ",\"invalid\":" << counters.invalid << ",\"observer_resource_unverified_frames\":" << observer.resourceUnverified
					<< ",\"observer_seen\":" << observer.goalsSeen << ",\"observer_retained\":" << observer.retained << ",\"observer_dropped\":" << observer.dropped
					<< ",\"source_received\":" << state.received.load() << ",\"source_enqueued\":" << state.enqueued.load() << ",\"source_consumed\":" << state.consumed.load()
					<< ",\"owner_up_to_exact_complete\":";
				StatisticsJson(out, targetTimes.data(), valid, state.clocks);
				out << ",\"owner_down_to_exact_complete\":";
				valid = 0;
				for (std::uint32_t step = Ui3FixtureWarmSteps + 1; step <= Ui3FixtureExpectedSteps; ++step)
				{
					const auto* row = FindOutcome(state, step);
					const auto down = (state.authorization->Input().scene == 2 ? 2u : 0u) + (step - 1) * 2;
					const auto received = state.entries[down].receiveTicks;
					if (state.clocks && row && row->terminalStatus == Ui3FiniteStatus::CompletedLayoutAndSvg && row->timingValid
						&& received > 0 && row->finalCommitTicks >= received) targetTimes[valid++] = Milliseconds(row->finalCommitTicks - received);
				}
				StatisticsJson(out, targetTimes.data(), valid, state.clocks);
				out << ",\"measured_interval_ms\":";
				if (state.clocks && state.measuredBeginTicks > 0 && state.measuredEnd.timingValid
					&& state.measuredEnd.finalCommitTicks >= state.measuredBeginTicks) out << Milliseconds(state.measuredEnd.finalCommitTicks - state.measuredBeginTicks);
				else out << "null";
				out << ",\"raw_available\":" << (raw ? "true" : "false") << ",\"raw_callback_seen\":" << (raw ? raw->callbackSeen : 0)
					<< ",\"raw_callback_retained\":" << (raw ? raw->callbackRetained : 0) << ",\"raw_callback_dropped\":" << (raw ? raw->callbackDropped : 0)
					<< ",\"raw_batch_seen\":" << (raw ? raw->batchSeen : 0) << ",\"raw_batch_retained\":" << (raw ? raw->batchRetained : 0)
					<< ",\"raw_batch_dropped\":" << (raw ? raw->batchDropped : 0) << ",\"raw_invalid_callbacks\":" << (raw ? raw->invalidCallbacks : 0)
					<< ",\"raw_invalid_batches\":" << (raw ? raw->invalidBatches : 0) << ",\"whole_run_true_bar_commits\":" << (raw ? raw->trueBarCommits : 0)
					<< ",\"whole_run_unverified_bar_commits\":" << (raw ? raw->unverifiedBarCommits : 0) << ",\"whole_run_invalid_bar_stamps\":" << (raw ? raw->invalidBarCommitStamps : 0)
					<< ",\"timed_callback_cost\":";
				std::size_t callbacks = 0, batches = 0;
				if (raw)
				{
					for (const auto& sample : raw->callbacks)
						if (InMeasuredInterval(state, sample, generation)) state.callbackStatistics[callbacks++] = Milliseconds(sample.endTicks - sample.startTicks);
					for (const auto& sample : raw->batches)
						if (state.measuredEnd.timingValid && sample.validTime && sample.contextEpoch == state.measuredEnd.epoch && sample.beginTicks >= state.measuredBeginTicks
							&& sample.endTicks <= state.measuredEnd.finalCommitTicks && sample.endTicks >= sample.beginTicks)
							state.batchStatistics[batches++] = Milliseconds(sample.endTicks - sample.beginTicks);
				}
				StatisticsJson(out, state.callbackStatistics.get(), callbacks, state.clocks && raw.has_value() && generation);
				out << ",\"timed_batch_cost\":"; StatisticsJson(out, state.batchStatistics.get(), batches, state.clocks && raw.has_value() && generation);
				out << ",\"timed_true_commit_gap\":";
				std::size_t gaps = 0;
				std::uint64_t attempts = 0, ulwAttempts = 0, commits = 0, advances = 0, failures = 0;
				if (raw) for (const auto& sample : raw->callbacks)
				{
					if (InMeasuredInterval(state, sample, generation))
					{
						attempts += sample.frame.presentAttempted; ulwAttempts += sample.frame.ulwAttempted;
						commits += sample.frame.presentCommitted; advances += sample.frame.animationAdvanced; failures += sample.frame.presentFailed;
					}
					if (sample.client == RenderPipeline::Client::Bar && sample.callbackGeneration == generation && sample.contextEpoch == state.measuredEnd.epoch
						&& sample.barCommitStatus == RenderPipeline::BarCommitStampStatus::Valid && sample.hasPreviousTrueBarCommit && sample.frame.hasBarCommitStamp
						&& state.measuredEnd.timingValid && sample.previousTrueBarCommitTicks >= state.measuredBeginTicks
						&& sample.frame.barCommitTicks <= state.measuredEnd.finalCommitTicks && sample.frame.barCommitTicks >= sample.previousTrueBarCommitTicks)
						state.callbackStatistics[gaps++] = Milliseconds(sample.frame.barCommitTicks - sample.previousTrueBarCommitTicks);
				}
				StatisticsJson(out, state.callbackStatistics.get(), gaps, state.clocks && raw.has_value() && generation);
				out << ",\"timed_getdc_attempts\":" << attempts << ",\"timed_ulw_attempts\":" << ulwAttempts << ",\"timed_software_commits\":" << commits
					<< ",\"timed_animation_advances\":" << advances << ",\"timed_present_failures\":" << failures;
				out << ",\"timed_stages\":[";
				for (unsigned stage = 0; stage < static_cast<unsigned>(RenderPipeline::FrameStage::Count); ++stage)
				{
					if (stage) out << ',';
					std::size_t count = 0;
					if (raw) for (const auto& sample : raw->callbacks)
						if (InMeasuredInterval(state, sample, generation) && std::isfinite(sample.frame.stageMs[stage]) && sample.frame.stageMs[stage] >= 0)
							state.callbackStatistics[count++] = sample.frame.stageMs[stage];
					StatisticsJson(out, state.callbackStatistics.get(), count, state.clocks && raw.has_value() && generation);
				}
				out << "],\"cpu_at_exact_measured_end\":null,\"gpu_duration\":null,\"optical_latency\":null,\"steady_svg_phase_counters\":null"
					<< ",\"raw_interval_rule\":\"complete matching callback or batch contained in MeasuredBegin..MeasuredEnd; overlaps excluded\""
					<< ",\"pixel_file\":\"equivalence.bgra\",\"pixel_equality\":\"pending fresh-child full-byte comparison\"}\n";
			}));
			state.outputsWritten = true;
		}
	}

	int RunAuthorizedPresentationFixture(const Ui3FixtureAuthorization& cap) noexcept
	{
		if (!IsAuthorizedUi3Fixture(cap)) return 91;
		std::unique_ptr<FixtureState> state;
		try { state = std::make_unique<FixtureState>(cap); }
		catch (...)
		{
			// 未创建任何 owner：空 owner 集合可以真封口 Failed，不继续产品初始化。
			InterlockedExchange(reinterpret_cast<LONG*>(&cap.Packet().result), static_cast<LONG>(Ui3FixtureResult::Failed));
			cap.Packet().unverifiedSteps = Ui3FixtureExpectedSteps;
			InterlockedExchange(reinterpret_cast<LONG*>(&cap.Packet().stage), static_cast<LONG>(Ui3FixtureStage::Sealed));
			return 90;
		}
		try
		{
			if (!Bootstrap(*state)) Fail(*state, Ui3FixtureSourceFailure::Ready);
			else
			{
				const auto limit = Clock::now() + std::chrono::seconds(260);
				while (!state->sourceDone.load(std::memory_order_acquire) && !offSignal && Clock::now() < limit)
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				if (!state->sourceDone.load(std::memory_order_acquire)) Fail(*state, Ui3FixtureSourceFailure::Deadline);
				if (offSignal && !state->sourceDone.load(std::memory_order_acquire)) Fail(*state, Ui3FixtureSourceFailure::UnexpectedSource);
			}
		}
		catch (...) { Fail(*state, Ui3FixtureSourceFailure::UnsupportedState); }
		try { CleanupAndJoin(*state); }
		catch (...) { KeepFailedOwnersAlive(); }
		try { WriteAllOutputs(*state); }
		catch (...) { Fail(*state, Ui3FixtureSourceFailure::Output); }
		const auto completed = CountComplete(*state);
		const bool passed = state->outputsWritten && FunctionalResultValid(*state);
		auto& packet = cap.Packet();
		packet.completedSteps = completed; packet.unverifiedSteps = Ui3FixtureExpectedSteps - completed;
		packet.startedTicks = state->clocks && state->measuredEnd.timingValid ? state->measuredBeginTicks : 0;
		packet.finishedTicks = state->clocks && state->measuredEnd.timingValid ? state->measuredEnd.finalCommitTicks : 0;
		InterlockedExchange(reinterpret_cast<LONG*>(&packet.received), state->received.load());
		InterlockedExchange(reinterpret_cast<LONG*>(&packet.enqueued), state->enqueued.load());
		InterlockedExchange(reinterpret_cast<LONG*>(&packet.consumed), state->consumed.load());
		InterlockedExchange(reinterpret_cast<LONG*>(&packet.result), static_cast<LONG>(passed ? Ui3FixtureResult::Passed : Ui3FixtureResult::Failed));
		if (state->job.event) CloseHandle(state->job.event);
		for (auto lease : state->directoryLeases) if (lease && lease != INVALID_HANDLE_VALUE) CloseHandle(lease);
		// 所有真正 join/控制任务/输出已结算，最后发布 Sealed 才允许 Auth 撤销 capability。
		PublishStage(*state, Ui3FixtureStage::Sealed);
		return passed ? 0 : 90;
	}
}
