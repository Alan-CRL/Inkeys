#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <unordered_map>

#include "../Inkeys/Inkeys/UI/Freeze/Freeze.MagnifierCoordinator.h"

import Inkeys.UI.Freeze;

namespace
{
	using namespace Inkeys::UI::Freeze::MagnifierInternal;

	int failures = 0;
	Coordinator* freezeObserverCoordinator = nullptr;

	void ForwardFreezeState(Inkeys::UI::Freeze::StateSnapshot snapshot) noexcept
	{
		if (freezeObserverCoordinator)
			freezeObserverCoordinator->Publish({ snapshot.revision, snapshot.active });
	}

	void Check(bool condition, std::string_view name)
	{
		if (condition) return;
		++failures;
		std::cerr << "FAIL " << name << '\n';
	}

	struct FakeOperations
	{
		Coordinator* coordinator = nullptr;
		Request replacement{};
		Stage publishDuring = Stage::Hidden;
		bool stopDuring = false;
		bool enterPresentationDuringReveal = false;
		bool targetsCurrent = true;
		bool prepareResult = true;
		bool filterResult = true;
		bool sourceResult = true;
		bool invalidateResult = true;
		bool redrawResult = true;
		bool revealResult = true;
		bool concealResult = true;
		int prepareCalls = 0;
		int filterCalls = 0;
		int sourceCalls = 0;
		int invalidateCalls = 0;
		int redrawCalls = 0;
		int revealCalls = 0;
		int concealCalls = 0;

		void MaybePublish(Stage stage)
		{
			if (!coordinator || publishDuring != stage) return;
			if (stopDuring) coordinator->Stop();
			else coordinator->Publish(replacement);
		}

		bool TargetsCurrent() const noexcept { return targetsCurrent; }
		bool PrepareHidden()
		{
			++prepareCalls;
			MaybePublish(Stage::PrepareHidden);
			return prepareResult;
		}
		bool SubmitFilter()
		{
			++filterCalls;
			MaybePublish(Stage::Filter);
			return filterResult;
		}
		bool SubmitSource()
		{
			++sourceCalls;
			MaybePublish(Stage::Source);
			return sourceResult;
		}
		bool Invalidate()
		{
			++invalidateCalls;
			MaybePublish(Stage::Invalidate);
			return invalidateResult;
		}
		bool Redraw()
		{
			++redrawCalls;
			MaybePublish(Stage::Redraw);
			return redrawResult;
		}
		bool Reveal()
		{
			++revealCalls;
			if (enterPresentationDuringReveal)
				Inkeys::UI::Freeze::SetPresentationActive(true);
			MaybePublish(Stage::Reveal);
			return revealResult;
		}
		bool Conceal()
		{
			++concealCalls;
			return concealResult;
		}
	};

	bool Finish(Coordinator& coordinator, Request request,
		TransactionResult result, FakeOperations& operations)
	{
		if (result.stage == Stage::Applied && coordinator.CommitApplied(request))
			return true;
		if (result.stage == Stage::Applied)
			result.concealed = operations.Conceal();
		coordinator.CompleteWithoutApply(request, result.concealed);
		return false;
	}

	void TestSingleExecutionAndFailurePropagation()
	{
		Coordinator coordinator;
		coordinator.Publish({ 1, true });
		const Request request = *coordinator.WaitNext();
		FakeOperations success;
		const auto result = ExecutePresent(coordinator, request, success);
		Check(Finish(coordinator, request, result, success),
			"magnifier successful request commits applied state");
		Check(success.sourceCalls == 1 && success.revealCalls == 1,
			"magnifier successful request submits source exactly once");

		Coordinator filterCoordinator;
		filterCoordinator.Publish({ 1, true });
		const Request filterRequest = *filterCoordinator.WaitNext();
		FakeOperations filterFailure;
		filterFailure.filterResult = false;
		const auto filterResult = ExecutePresent(
			filterCoordinator, filterRequest, filterFailure);
		Check(filterResult.stage == Stage::Filter && filterFailure.sourceCalls == 0 &&
			filterFailure.revealCalls == 0,
			"magnifier filter failure blocks source and reveal");
		Check(!Finish(filterCoordinator, filterRequest, filterResult, filterFailure) &&
			!filterCoordinator.Snapshot().visible,
			"magnifier filter failure does not mark visible");

		filterCoordinator.Publish({ 2, true });
		const Request recoveryRequest = *filterCoordinator.WaitNext();
		FakeOperations recovery;
		const auto recoveryResult = ExecutePresent(
			filterCoordinator, recoveryRequest, recovery);
		Check(Finish(filterCoordinator, recoveryRequest, recoveryResult, recovery),
			"magnifier later request recovers after failure");

		Coordinator sourceCoordinator;
		sourceCoordinator.Publish({ 1, true });
		const Request sourceRequest = *sourceCoordinator.WaitNext();
		FakeOperations sourceFailure;
		sourceFailure.sourceResult = false;
		const auto sourceResult = ExecutePresent(
			sourceCoordinator, sourceRequest, sourceFailure);
		Check(sourceResult.stage == Stage::Source && sourceFailure.revealCalls == 0,
			"magnifier source failure blocks reveal");
		(void)Finish(sourceCoordinator, sourceRequest, sourceResult, sourceFailure);
	}

	void TestLatestWinsAndCancellation()
	{
		Coordinator coalesced;
		coalesced.Publish({ 1, true });
		coalesced.Publish({ 2, false });
		coalesced.Publish({ 3, true });
		const Request latest = *coalesced.WaitNext();
		Check(latest.revision == 3 && latest.active,
			"magnifier open close open selects newest request");
		FakeOperations latestOperations;
		const auto latestResult = ExecutePresent(coalesced, latest, latestOperations);
		Check(Finish(coalesced, latest, latestResult, latestOperations) &&
			latestOperations.sourceCalls == 1,
			"magnifier newest reopen captures once");

		Coordinator preparing;
		preparing.Publish({ 1, true });
		const Request preparingRequest = *preparing.WaitNext();
		FakeOperations cancelDuringPrepare;
		cancelDuringPrepare.coordinator = &preparing;
		cancelDuringPrepare.replacement = { 2, false };
		cancelDuringPrepare.publishDuring = Stage::PrepareHidden;
		const auto cancelled = ExecutePresent(
			preparing, preparingRequest, cancelDuringPrepare);
		Check(cancelled.stage == Stage::Superseded &&
			cancelDuringPrepare.filterCalls == 0 &&
			cancelDuringPrepare.sourceCalls == 0 &&
			cancelDuringPrepare.concealCalls == 1,
			"magnifier cancellation during prepare prevents capture");
		(void)Finish(preparing, preparingRequest, cancelled, cancelDuringPrepare);

		Coordinator late;
		late.Publish({ 1, true });
		const Request lateRequest = *late.WaitNext();
		FakeOperations lateCancel;
		lateCancel.coordinator = &late;
		lateCancel.replacement = { 2, false };
		lateCancel.publishDuring = Stage::Reveal;
		const auto lateResult = ExecutePresent(late, lateRequest, lateCancel);
		Check(lateResult.stage == Stage::Superseded && lateCancel.revealCalls == 1 &&
			lateCancel.concealCalls == 1,
			"magnifier late reveal is immediately concealed");
		Check(!Finish(late, lateRequest, lateResult, lateCancel) &&
			!late.Snapshot().visible,
			"magnifier late result cannot commit visible state");

		Coordinator stopped;
		stopped.Publish({ 1, true });
		const Request stoppedRequest = *stopped.WaitNext();
		FakeOperations stopDuringSource;
		stopDuringSource.coordinator = &stopped;
		stopDuringSource.publishDuring = Stage::Source;
		stopDuringSource.stopDuring = true;
		const auto stoppedResult = ExecutePresent(
			stopped, stoppedRequest, stopDuringSource);
		Check(stoppedResult.stage == Stage::Stopped &&
			stopDuringSource.sourceCalls == 1 && stopDuringSource.revealCalls == 0 &&
			stopDuringSource.concealCalls == 1,
			"magnifier stop during capture invalidates late result");

		using namespace Inkeys::UI::Freeze;
		SetStateObserver(nullptr);
		SetPresentationActive(false);
		SetWhiteboardActive(false);
		if (IsActive()) Toggle();
		Coordinator workspace;
		freezeObserverCoordinator = &workspace;
		SetStateObserver(&ForwardFreezeState);
		Toggle();
		const Request workspaceRequest = *workspace.WaitNext();
		FakeOperations workspaceSwitch;
		workspaceSwitch.enterPresentationDuringReveal = true;
		const auto workspaceResult = ExecutePresent(
			workspace, workspaceRequest, workspaceSwitch);
		Check(workspaceResult.stage == Stage::Superseded &&
			workspaceSwitch.revealCalls == 1 && workspaceSwitch.concealCalls == 1 &&
			!workspace.Snapshot().latest.active,
			"magnifier workspace switch invalidates and conceals a late reveal");
		Check(!Finish(workspace, workspaceRequest,
			workspaceResult, workspaceSwitch) && !workspace.Snapshot().visible,
			"magnifier workspace switch cannot commit stale visible state");
		SetStateObserver(nullptr);
		freezeObserverCoordinator = nullptr;
		SetPresentationActive(false);
	}

	void TestRequiredDisplayFailure()
	{
		Coordinator prepareCoordinator;
		prepareCoordinator.Publish({ 1, true });
		const Request prepareRequest = *prepareCoordinator.WaitNext();
		FakeOperations prepareFailure;
		prepareFailure.prepareResult = false;
		const auto prepareResult = ExecutePresent(
			prepareCoordinator, prepareRequest, prepareFailure);
		Check(prepareResult.stage == Stage::PrepareHidden &&
			prepareFailure.filterCalls == 0 && prepareFailure.revealCalls == 0,
			"magnifier prepare failure stops transaction");
		Check(!Finish(prepareCoordinator, prepareRequest,
			prepareResult, prepareFailure) &&
			prepareCoordinator.Snapshot().appliedRevision == 0,
			"magnifier display failure does not converge applied state");

		Coordinator invalidateCoordinator;
		invalidateCoordinator.Publish({ 1, true });
		const Request invalidateRequest = *invalidateCoordinator.WaitNext();
		FakeOperations invalidateFailure;
		invalidateFailure.invalidateResult = false;
		const auto invalidateResult = ExecutePresent(
			invalidateCoordinator, invalidateRequest, invalidateFailure);
		Check(invalidateResult.stage == Stage::Invalidate &&
			invalidateFailure.sourceCalls == 1 && invalidateFailure.redrawCalls == 0 &&
			invalidateFailure.revealCalls == 0,
			"magnifier invalidate failure blocks redraw and reveal");
		Check(!Finish(invalidateCoordinator, invalidateRequest,
			invalidateResult, invalidateFailure) &&
			invalidateCoordinator.Snapshot().appliedRevision == 0,
			"magnifier invalidate failure cannot mark applied");

		Coordinator redrawCoordinator;
		redrawCoordinator.Publish({ 1, true });
		const Request redrawRequest = *redrawCoordinator.WaitNext();
		FakeOperations redrawFailure;
		redrawFailure.redrawResult = false;
		const auto redrawResult = ExecutePresent(
			redrawCoordinator, redrawRequest, redrawFailure);
		Check(redrawResult.stage == Stage::Redraw &&
			redrawFailure.redrawCalls == 1 && redrawFailure.revealCalls == 0,
			"magnifier redraw failure blocks reveal");
		Check(!Finish(redrawCoordinator, redrawRequest,
			redrawResult, redrawFailure) &&
			redrawCoordinator.Snapshot().appliedRevision == 0,
			"magnifier redraw failure cannot mark applied");

		Coordinator revealCoordinator;
		revealCoordinator.Publish({ 1, true });
		const Request revealRequest = *revealCoordinator.WaitNext();
		FakeOperations revealFailure;
		revealFailure.revealResult = false;
		const auto revealResult = ExecutePresent(
			revealCoordinator, revealRequest, revealFailure);
		Check(revealResult.stage == Stage::Reveal &&
			revealFailure.revealCalls == 1 && revealFailure.concealCalls == 1,
			"magnifier reveal failure immediately enters cleanup");
		Check(!Finish(revealCoordinator, revealRequest,
			revealResult, revealFailure) &&
			revealCoordinator.Snapshot().appliedRevision == 0 &&
			!revealCoordinator.Snapshot().visible,
			"magnifier reveal failure cannot mark visible or applied");
	}

	void TestLifecycleReset()
	{
		Coordinator coordinator;
		coordinator.Publish({ 1, true });
		coordinator.Stop();
		coordinator.Reset();
		coordinator.Publish({ 2, true });
		const Request request = *coordinator.WaitNext();
		FakeOperations operations;
		const auto result = ExecutePresent(coordinator, request, operations);
		Check(Finish(coordinator, request, result, operations) &&
			coordinator.Snapshot().appliedRevision == 2,
			"magnifier rebuilt lifecycle accepts requests after stop");
	}

	void TestFilterListValidationAndRefresh()
	{
		using Handle = std::uintptr_t;
		struct Metadata { bool valid; std::uint32_t process; bool child; };
		std::unordered_map<Handle, Metadata> metadata = {
			{ 1, { true, 42, false } },
			{ 2, { false, 42, false } },
			{ 3, { true, 7, false } },
			{ 4, { true, 42, true } },
			{ 5, { true, 42, false } },
			{ 6, { true, 42, false } },
		};
		auto build = [&](const auto& candidates)
			{
				return BuildFilterList<Handle>(candidates, 42,
					[&](Handle handle) { return metadata[handle].valid; },
					[&](Handle handle) { return metadata[handle].process; },
					[&](Handle handle) { return metadata[handle].child; });
			};

		const std::array<Handle, 8> first = { 1, 0, 2, 3, 4, 1, 5, 0 };
		const auto firstResult = build(first);
		Check(firstResult == std::vector<Handle>({ 1, 5 }),
			"magnifier filter drops null invalid foreign child and duplicates");

		const std::array<Handle, 4> refreshed = { 6, 0, 5, 6 };
		const auto refreshedResult = build(refreshed);
		Check(refreshedResult == std::vector<Handle>({ 6, 5 }),
			"magnifier filter rebuild uses changed current handles");
	}
}

int RunMagnifierCoordinatorTests()
{
	TestSingleExecutionAndFailurePropagation();
	TestLatestWinsAndCancellation();
	TestRequiredDisplayFailure();
	TestLifecycleReset();
	TestFilterListValidationAndRefresh();
	return failures;
}
