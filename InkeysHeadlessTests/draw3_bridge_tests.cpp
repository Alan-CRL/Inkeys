#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.Bridge.h"
#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.h"
#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.PresentationState.h"
#include "../Inkeys/Inkeys/Drawing/Draw3/Draw3.TimerPeriod.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

namespace
{
	using namespace Inkeys::Drawing::Draw3::Bridge;
	using Inkeys::Drawing::Draw3::DrawpadPresentationSurface;
	using Inkeys::Drawing::Draw3::ResolveDrawpadPresentationSurface;
	using Inkeys::Drawing::Draw3::TimerPeriodController;

	struct TimerPeriodCalls
	{
		int beginCount = 0;
		int endCount = 0;
		unsigned int lastPeriod = 0;
		bool beginSucceeds = true;
	};

	bool BeginTimerPeriod(void* context, unsigned int period) noexcept
	{
		auto* calls = static_cast<TimerPeriodCalls*>(context);
		if (!calls) return false;
		++calls->beginCount;
		calls->lastPeriod = period;
		return calls->beginSucceeds;
	}

	void EndTimerPeriod(void* context, unsigned int period) noexcept
	{
		auto* calls = static_cast<TimerPeriodCalls*>(context);
		if (!calls) return;
		++calls->endCount;
		calls->lastPeriod = period;
	}

	bool Expect(bool condition, const char* name)
	{
		if (!condition) std::cerr << "[Draw3Bridge] failed: " << name << '\n';
		return condition;
	}

	void TestProductState(int& failures)
	{
		StateBridge bridge;
		const ProductState initial = bridge.Snapshot();
		if (!Expect(initial.colorRgba == 0x000000FFu,
			"default color uses RGBA black")) ++failures;
		if (!Expect(initial.revision == 0,
			"default snapshot starts at revision zero")) ++failures;
		if (!Expect(initial.selectionMode,
			"default product state starts in selection mode")) ++failures;
		if (!Expect(initial.workspace == Workspace::Presentation,
			"default product state starts in presentation workspace")) ++failures;

		ProductState first{};
		first.colorRgba = 0x12345678u;
		first.widthDip = 4.5f;
		first.revision = 99;
		bridge.PublishState(first);
		const ProductState firstSnapshot = bridge.Snapshot();
		if (!Expect(firstSnapshot.colorRgba == 0x12345678u
			&& firstSnapshot.widthDip == 4.5f
			&& firstSnapshot.revision == 1,
			"publish owns the next revision")) ++failures;

		first.colorRgba = 0xAABBCCDDu;
		bridge.PublishState(first);
		const ProductState secondSnapshot = bridge.Snapshot();
		// 快照按值返回，后续发布不能回写已经取得的产品状态。
		if (!Expect(firstSnapshot.colorRgba == 0x12345678u
			&& firstSnapshot.revision == 1
			&& secondSnapshot.colorRgba == 0xAABBCCDDu
			&& secondSnapshot.revision == 2,
			"snapshots are immutable with monotonic revisions")) ++failures;

		ProductState pageState = secondSnapshot;
		pageState.page = 6;
		pageState.hasPage = true;
		pageState.workspace = Workspace::Whiteboard;
		bridge.PublishState(pageState);
		ProductState toolOnlyState = bridge.Snapshot();
		toolOnlyState.tool = Tool::Laser;
		bridge.PublishState(toolOnlyState);
		const ProductState preservedPage = bridge.Snapshot();
		if (!Expect(preservedPage.tool == Tool::Laser && preservedPage.hasPage
			&& preservedPage.page == 6
			&& preservedPage.workspace == Workspace::Whiteboard,
			"tool state publication preserves page and workspace")) ++failures;
	}

	void TestCommandQueue(int& failures)
	{
		StateBridge bridge(2);
		if (!Expect(bridge.Publish(CommandType::Clear) == CommandResult::Accepted
			&& bridge.Publish(CommandType::Undo) == CommandResult::Accepted,
			"commands enter the bounded queue")) ++failures;
		if (!Expect(bridge.Publish(CommandType::Redo) == CommandResult::QueueFull,
			"queue rejects commands at capacity")) ++failures;

		Command command{};
		if (!Expect(bridge.TryConsume(command)
			&& command.type == CommandType::Clear && command.sequence == 1,
			"first command preserves order and sequence")) ++failures;
		if (!Expect(bridge.TryConsume(command)
			&& command.type == CommandType::Undo && command.sequence == 2,
			"second command preserves order and sequence")) ++failures;
		if (!Expect(!bridge.TryConsume(command),
			"empty queue reports no command")) ++failures;

		StateBridge minimumCapacity(0);
		if (!Expect(minimumCapacity.Publish(CommandType::Redo) == CommandResult::Accepted
			&& minimumCapacity.Publish(CommandType::Clear) == CommandResult::QueueFull,
			"zero capacity is normalized to one")) ++failures;
	}

	void TestUnsupportedAndLifecycle(int& failures)
	{
		StateBridge bridge(2);
		if (!Expect(bridge.Publish(CommandType::Save) == CommandResult::Unsupported
			&& bridge.Publish(CommandType::SuperRecovery) == CommandResult::Unsupported
			&& bridge.Publish(CommandType::AutoStraighten) == CommandResult::Unsupported
			&& bridge.Publish(CommandType::InputTest) == CommandResult::Unsupported,
			"not-ready commands remain explicitly unsupported")) ++failures;

		if (!Expect(bridge.Publish(CommandType::NextPage) == CommandResult::Accepted,
			"unsupported commands do not consume queue sequence")) ++failures;
		Command command{};
		if (!Expect(bridge.TryConsume(command) && command.sequence == 1,
			"first supported command keeps sequence one")) ++failures;

		bridge.Publish(CommandType::PreviousPage);
		bridge.Stop();
		if (!Expect(!bridge.Running()
			&& !bridge.TryConsume(command)
			&& bridge.Publish(CommandType::Clear) == CommandResult::NotRunning
			&& bridge.Publish(CommandType::Save) == CommandResult::NotRunning,
			"stop closes and clears the command bridge")) ++failures;

		ProductState stoppedState{};
		stoppedState.colorRgba = 0xFFFFFFFFu;
		bridge.PublishState(stoppedState);
		if (!Expect(bridge.Snapshot().revision == 0,
			"stopped bridge rejects state publication")) ++failures;

		bridge.Reset();
		const ProductState resetState = bridge.Snapshot();
		if (!Expect(bridge.Running()
			&& resetState.colorRgba == 0x000000FFu
			&& resetState.revision == 0
			&& !bridge.TryConsume(command),
			"reset reopens and clears bridge state")) ++failures;
		if (!Expect(bridge.Publish(CommandType::Redo) == CommandResult::Accepted
			&& bridge.TryConsume(command) && command.sequence == 1,
			"reset restarts command sequence")) ++failures;
	}

	void TestEraserModeMapping(int& failures)
	{
		if (!Expect(NormalizeLegacyEraserMode(0) == 1
			&& NormalizeLegacyEraserMode(1) == 1,
			"legacy pressure eraser normalizes to speed eraser")) ++failures;
		if (!Expect(NormalizeLegacyEraserMode(2) == 2
			&& NormalizeLegacyEraserMode(-1) == 2
			&& NormalizeLegacyEraserMode(99) == 2,
			"unknown eraser modes normalize to fixed eraser")) ++failures;
		if (!Expect(EncodeEraserMode(Tool::SpeedEraser) == 1
			&& EncodeEraserMode(Tool::FixedEraser) == 2
			&& EncodeEraserMode(Tool::Pen) == 2,
			"eraser encoding only emits speed or fixed values")) ++failures;
	}

	void TestTimerPeriodController(int& failures)
	{
		TimerPeriodCalls calls;
		{
			TimerPeriodController timer({ &calls, &BeginTimerPeriod, &EndTimerPeriod });
			if (!Expect(timer.SelectionMode() && !timer.Active()
				&& calls.beginCount == 0 && calls.endCount == 0,
				"selection mode does not request high timer resolution")) ++failures;

			timer.SetSelectionMode(false);
			timer.SetSelectionMode(false);
			if (!Expect(!timer.SelectionMode() && timer.Active()
				&& calls.beginCount == 1 && calls.endCount == 0
				&& calls.lastPeriod == 1,
				"drawing mode begins one timer period idempotently")) ++failures;

			timer.SetSelectionMode(true);
			timer.SetSelectionMode(true);
			if (!Expect(timer.SelectionMode() && !timer.Active()
				&& calls.beginCount == 1 && calls.endCount == 1,
				"returning to selection ends the successful period once")) ++failures;

			timer.SetSelectionMode(false);
			if (!Expect(timer.Active() && calls.beginCount == 2,
				"re-entering drawing begins a new timer period")) ++failures;
		}
		if (!Expect(calls.endCount == 2,
			"controller destruction releases an active timer period")) ++failures;

		TimerPeriodCalls failedCalls;
		failedCalls.beginSucceeds = false;
		TimerPeriodController failedTimer({
			&failedCalls, &BeginTimerPeriod, &EndTimerPeriod });
		failedTimer.SetSelectionMode(false);
		failedTimer.SetSelectionMode(false);
		failedTimer.SetSelectionMode(true);
		if (!Expect(!failedTimer.Active() && failedCalls.beginCount == 1
			&& failedCalls.endCount == 0,
			"failed begin is not retried or paired within the same drawing visit")) ++failures;

		failedCalls.beginSucceeds = true;
		failedTimer.SetSelectionMode(false);
		if (!Expect(failedTimer.Active() && failedCalls.beginCount == 2,
			"failed begin is retried after leaving and re-entering drawing")) ++failures;
		failedTimer.SetSelectionMode(true);
		if (!Expect(!failedTimer.Active() && failedCalls.endCount == 1,
			"successful retry receives exactly one matching end")) ++failures;
	}

	void TestDrawpadPresentationPlan(int& failures)
	{
		if (!Expect(SelectionUsesAuxiliaryOutput(true, Workspace::Presentation)
			&& !SelectionUsesAuxiliaryOutput(true, Workspace::Whiteboard)
			&& !SelectionUsesAuxiliaryOutput(false, Workspace::Whiteboard),
			"whiteboard selection stays on the primary drawpad")) ++failures;
		if (!Expect(ResolveDrawpadPresentationSurface(true, false, true) ==
			DrawpadPresentationSurface::Hidden,
			"clean empty selection hides both drawpad surfaces")) ++failures;
		if (!Expect(ResolveDrawpadPresentationSurface(true, false, false) ==
			DrawpadPresentationSurface::Presentation,
			"empty selection waits for full-frame clean")) ++failures;
		if (!Expect(ResolveDrawpadPresentationSurface(true, true, false) ==
			DrawpadPresentationSurface::Presentation,
			"content selection uses the auxiliary surface")) ++failures;
		if (!Expect(ResolveDrawpadPresentationSurface(false, false, true) ==
			DrawpadPresentationSurface::Primary,
			"drawing mode uses the primary surface")) ++failures;
	}

	void TestPageRuntimeRevisionPolicy(int& failures)
	{
		using Inkeys::Drawing::Draw3::Detail::HostRuntimeRevisionSignal;
		HostRuntimeRevisionSignal signal;
		std::atomic_bool running = true;
		const std::uint64_t initialRevision = signal.Revision();
		if (!Expect(!signal.PublishPageChange(2, 8, 2, 8)
			&& signal.Revision() == initialRevision
			&& !signal.WaitForChange(initialRevision, 1, running),
			"stable page runtime neither publishes nor wakes")) ++failures;

		std::atomic_bool pageWaitCompleted = false;
		std::jthread pageWaiter([&]
			{
				pageWaitCompleted.store(signal.WaitForChange(
					initialRevision, 1000, running), std::memory_order_release);
			});
		const bool pagePublished = signal.PublishPageChange(2, 8, 3, 8);
		pageWaiter.join();
		const std::uint64_t pageRevision = signal.Revision();
		if (!Expect(pagePublished && pageRevision != initialRevision
			&& pageWaitCompleted.load(std::memory_order_acquire),
			"page index change publishes revision and wakes waiter")) ++failures;

		std::atomic_bool countWaitCompleted = false;
		std::jthread countWaiter([&]
			{
				countWaitCompleted.store(signal.WaitForChange(
					pageRevision, 1000, running), std::memory_order_release);
			});
		const bool countPublished = signal.PublishPageChange(3, 8, 3, 9);
		countWaiter.join();
		if (!Expect(countPublished && signal.Revision() != pageRevision
			&& countWaitCompleted.load(std::memory_order_acquire),
			"page count change publishes revision and wakes waiter")) ++failures;

		bool repeatedWakePrompt = true;
		for (int iteration = 0; iteration < 64 && repeatedWakePrompt; ++iteration)
		{
			const std::uint64_t revision = signal.Revision();
			std::atomic_bool waitEntered = false;
			std::atomic_bool waitCompleted = false;
			std::jthread waiter([&]
				{
					waitEntered.store(true, std::memory_order_release);
					waitCompleted.store(signal.WaitForChange(
						revision, 1000, running), std::memory_order_release);
				});
			while (!waitEntered.load(std::memory_order_acquire)) std::this_thread::yield();
			const auto publishedAt = std::chrono::steady_clock::now();
			signal.Publish();
			waiter.join();
			repeatedWakePrompt = waitCompleted.load(std::memory_order_acquire)
				&& std::chrono::steady_clock::now() - publishedAt
				< std::chrono::milliseconds(750);
		}
		if (!Expect(repeatedWakePrompt,
			"repeated revision handoff cannot lose the condition-variable wake")) ++failures;

		const std::uint64_t stopRevision = signal.Revision();
		std::atomic_bool stopWaitCompleted = false;
		std::jthread stopWaiter([&]
			{
				stopWaitCompleted.store(signal.WaitForChange(
					stopRevision, 1000, running), std::memory_order_release);
			});
		running.store(false, std::memory_order_release);
		signal.NotifyAll();
		stopWaiter.join();
		if (!Expect(stopWaitCompleted.load(std::memory_order_acquire),
			"host stop notification releases runtime waiter")) ++failures;
	}
}

int RunDraw3BridgeTests()
{
	int failures = 0;
	TestProductState(failures);
	TestCommandQueue(failures);
	TestUnsupportedAndLifecycle(failures);
	TestEraserModeMapping(failures);
	TestTimerPeriodController(failures);
	TestDrawpadPresentationPlan(failures);
	TestPageRuntimeRevisionPolicy(failures);
	return failures;
}
