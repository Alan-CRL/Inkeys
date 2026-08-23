#include "../Inkeys/Inkeys/UI/Bar/Bar.PresentDecision.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace
{
	using Inkeys::UI::Bar::BarPresentAttemptResult;
	using Inkeys::UI::Bar::BarPresentCompletionKind;
	using Inkeys::UI::Bar::BarPresentDecision;
	using Inkeys::UI::Bar::BarPresentDemand;
	using Inkeys::UI::Bar::BarPresentFailureClass;
	using Inkeys::UI::Bar::BarPresentMappingMode;
	using Inkeys::UI::Bar::BarPresentMappingTracker;
	using Inkeys::UI::Bar::BarPresentMappingTuple;
	using Inkeys::UI::Bar::IsBarSharedDeviceLost;
	using Inkeys::UI::Bar::ShouldForceBarFullWindowReplacement;

	int failureCount = 0;

	void Check(bool condition, std::string_view name)
	{
		if (condition) return;
		++failureCount;
		std::cerr << "FAIL " << name << '\n';
	}

	[[nodiscard]] constexpr bool SameRect(const RECT& left, const RECT& right) noexcept
	{
		return left.left == right.left && left.top == right.top
			&& left.right == right.right && left.bottom == right.bottom;
	}

	void TestDemandAccumulationAndPriority()
	{
		BarPresentDecision decision;
		Check(!decision.ShouldPresent(), "present starts idle");

		decision.AddDemand({ false, true, false });
		Check(decision.ShouldPresent(), "lighting requests a present");
		Check(!decision.NeedsInteractivePass(), "lighting-only frame is cosmetic");

		decision.AddDemand({ true, false, true });
		Check(decision.HasPendingVisual(), "visual demand accumulates");
		Check(decision.HasPendingRenderOnce(), "render-once demand accumulates");
		Check(decision.NeedsInteractivePass(),
			"visual or render-once demand is interactive");
	}

	void TestCosmeticLeaseSkipRetainsPendingAndBounds()
	{
		constexpr RECT initialBounds{ 1, 2, 30, 40 };
		BarPresentDecision decision(initialBounds);
		decision.AddDemand({ false, true, false });

		const auto completion = decision.CompleteAttempt(
			BarPresentAttemptResult::CosmeticLeaseSkipped());
		Check(completion.kind == BarPresentCompletionKind::Deferred,
			"cosmetic lease skip is deferred");
		Check(decision.HasPendingLighting(),
			"cosmetic lease skip retains final lighting");
		Check(!decision.NeedsFullDirty(),
			"cosmetic lease skip does not invent dirty recovery");
		Check(SameRect(decision.LastPresentedBounds(), initialBounds),
			"cosmetic lease skip retains last-presented bounds");
	}

	void TestEveryPresentStageMustSucceed()
	{
		constexpr RECT initialBounds{ 1, 2, 30, 40 };
		constexpr RECT candidateBounds{ 5, 6, 70, 80 };
		struct FailureCase
		{
			BarPresentAttemptResult result;
			BarPresentFailureClass failureClass;
			std::string_view name;
		};
		constexpr std::array failures{
			FailureCase{ BarPresentAttemptResult::Acquired(
				E_FAIL, TRUE, S_OK, S_OK, candidateBounds),
				BarPresentFailureClass::GetDc, "GetDC failure" },
			FailureCase{ BarPresentAttemptResult::Acquired(
				S_OK, FALSE, S_OK, S_OK, candidateBounds),
				BarPresentFailureClass::UpdateLayeredWindow, "ULW failure" },
			FailureCase{ BarPresentAttemptResult::Acquired(
				S_OK, TRUE, E_FAIL, S_OK, candidateBounds),
				BarPresentFailureClass::ReleaseDc, "ReleaseDC failure" },
			FailureCase{ BarPresentAttemptResult::Acquired(
				S_OK, TRUE, S_OK, E_FAIL, candidateBounds),
				BarPresentFailureClass::EndDraw, "EndDraw failure" },
		};
		constexpr auto success = BarPresentAttemptResult::Acquired(
			S_OK, TRUE, S_OK, S_OK, candidateBounds);

		for (const auto& failureCase : failures)
		{
			BarPresentDecision decision(initialBounds);
			decision.AddDemand({ true, true, true });
			const auto failure = decision.CompleteAttempt(
				failureCase.result, 7, 11, 20);
			Check(failure.kind == BarPresentCompletionKind::Retry
				&& decision.ActiveFailureClass() == failureCase.failureClass
				&& decision.HasPendingVisual()
				&& decision.HasPendingLighting()
				&& decision.HasPendingRenderOnce()
				&& decision.NeedsFullDirty()
				&& SameRect(decision.LastPresentedBounds(), initialBounds),
				failureCase.name);

			Check(decision.CompleteAttempt(success).IsCommitted(),
				"complete present commits");
			Check(!decision.HasFailureBackoff(),
				"complete present clears failure backoff");
			Check(!decision.ShouldPresent(),
				"complete present clears pending demands");
			Check(!decision.NeedsFullDirty(),
				"complete present clears full-dirty retry");
			Check(SameRect(decision.LastPresentedBounds(), candidateBounds),
				"complete present advances last-presented bounds");
		}
	}

	void TestRecreateTargetRequestsResourceReset()
	{
		constexpr RECT initialBounds{ 1, 2, 30, 40 };
		constexpr RECT candidateBounds{ 5, 6, 70, 80 };
		constexpr std::array recreateFailures{
			BarPresentAttemptResult::Acquired(
				D2DERR_RECREATE_TARGET, TRUE, S_OK, S_OK, candidateBounds),
			BarPresentAttemptResult::Acquired(
				S_OK, TRUE, D2DERR_RECREATE_TARGET, S_OK, candidateBounds),
			BarPresentAttemptResult::Acquired(
				S_OK, TRUE, S_OK, D2DERR_RECREATE_TARGET, candidateBounds),
		};

		for (const auto& failure : recreateFailures)
		{
			BarPresentDecision decision(initialBounds);
			decision.AddDemand({ true, false, false });
			const auto completion = decision.CompleteAttempt(failure);
			Check(completion.NeedsTargetRecreation()
				&& decision.HasPendingVisual()
				&& decision.NeedsFullDirty()
				&& SameRect(decision.LastPresentedBounds(), initialBounds),
				"D2DERR_RECREATE_TARGET retains transaction and bounds");
		}
	}

	void TestSharedDeviceLossClassification()
	{
		constexpr RECT bounds{ 5, 6, 70, 80 };
		constexpr std::array<HRESULT, 3> sharedFailures{
			DXGI_ERROR_DEVICE_REMOVED,
			DXGI_ERROR_DEVICE_RESET,
			DXGI_ERROR_DRIVER_INTERNAL_ERROR,
		};
		for (const HRESULT failure : sharedFailures)
		{
			Check(IsBarSharedDeviceLost(failure),
				"DXGI shared-device failure is recognized");
			Check(BarPresentAttemptResult::Acquired(
				failure, TRUE, S_OK, S_OK, bounds).HasSharedDeviceLoss(),
				"GetDC shared-device failure is propagated");
			Check(BarPresentAttemptResult::Acquired(
				S_OK, TRUE, failure, S_OK, bounds).HasSharedDeviceLoss(),
				"ReleaseDC shared-device failure is propagated");
			Check(BarPresentAttemptResult::Acquired(
				S_OK, TRUE, S_OK, failure, bounds).HasSharedDeviceLoss(),
				"EndDraw shared-device failure is propagated");
		}
		Check(!IsBarSharedDeviceLost(D2DERR_RECREATE_TARGET),
			"local target recreation stays per-window");
		Check(!BarPresentAttemptResult::Acquired(
			E_FAIL, TRUE, S_OK, S_OK, bounds).HasSharedDeviceLoss(),
			"ordinary present failure stays local retry");
	}

	void TestBoundsAdvanceOnlyOnSuccess()
	{
		constexpr RECT initialBounds{ 1, 2, 30, 40 };
		constexpr RECT failedBounds{ 5, 6, 70, 80 };
		constexpr RECT committedBounds{ 9, 10, 90, 100 };
		BarPresentDecision decision(initialBounds);
		decision.AddDemand({ true, false, false });

		(void)decision.CompleteAttempt(BarPresentAttemptResult::Acquired(
			S_OK, FALSE, S_OK, S_OK, failedBounds));
		Check(SameRect(decision.LastPresentedBounds(), initialBounds),
			"failed present cannot advance bounds");

		const auto completion = decision.CompleteAttempt(
			BarPresentAttemptResult::Acquired(
				S_OK, TRUE, S_OK, S_OK, committedBounds));
		Check(completion.IsCommitted()
			&& SameRect(decision.LastPresentedBounds(), committedBounds),
			"successful present advances bounds exactly once");
	}

	void TestResourceResetForcesFullDirtyWithoutLosingDemand()
	{
		BarPresentDecision decision;
		decision.AddDemand({ true, false, false });
		decision.RequireFullDirtyRetry();
		Check(decision.NeedsFullDirty(), "resource reset forces full dirty");
		Check(decision.HasPendingVisual(), "resource reset retains visual demand");
	}

	void TestRepeatedFailureUsesBoundedBackoff()
	{
		constexpr RECT candidateBounds{ 5, 6, 70, 80 };
		constexpr auto failure = BarPresentAttemptResult::Acquired(
			E_FAIL, TRUE, S_OK, S_OK, candidateBounds);
		constexpr std::array<std::uint64_t, 8> expectedDelays{
			1, 2, 4, 8, 16, 32, 60, 60 };
		BarPresentDecision decision;
		decision.AddDemand({ true, false, false });

		std::uint64_t frameSerial = 100;
		for (std::size_t index = 0; index < expectedDelays.size(); ++index)
		{
			const auto completion = decision.CompleteAttempt(
				failure, 42, 9, frameSerial);
			const std::uint64_t expectedDelay = expectedDelays[index];
			const unsigned int expectedCount = static_cast<unsigned int>(
				index < 7 ? index + 1 : 7);
			Check(completion.kind == BarPresentCompletionKind::Retry,
				"repeated failure remains retryable");
			Check(decision.ConsecutiveFailureCount() == expectedCount,
				"repeated failure count saturates");
			Check(decision.RetryDelayFrames() == expectedDelay,
				"repeated failure applies bounded backoff");
			Check(!decision.CanAttemptPresent(frameSerial),
				"failure defers the current frame");
			if (expectedDelay > 1)
				Check(!decision.CanAttemptPresent(
					frameSerial + expectedDelay - 1),
					"backoff blocks early retry");
			frameSerial += expectedDelay;
			Check(decision.CanAttemptPresent(frameSerial),
				"backoff allows bounded retry");
		}
	}

	void TestFailureRecoveryTriggers()
	{
		constexpr RECT candidateBounds{ 5, 6, 70, 80 };
		constexpr auto getDcFailure = BarPresentAttemptResult::Acquired(
			E_FAIL, TRUE, S_OK, S_OK, candidateBounds);
		constexpr auto releaseDcFailure = BarPresentAttemptResult::Acquired(
			S_OK, TRUE, E_FAIL, S_OK, candidateBounds);
		constexpr auto success = BarPresentAttemptResult::Acquired(
			S_OK, TRUE, S_OK, S_OK, candidateBounds);
		BarPresentDecision decision;
		decision.AddDemand({ true, false, false });

		(void)decision.CompleteAttempt(getDcFailure, 5, 10, 1);
		decision.ObserveDemandGeneration(10);
		Check(decision.HasFailureBackoff(),
			"same demand generation keeps backoff");
		decision.ObserveDemandGeneration(11);
		Check(!decision.HasFailureBackoff()
			&& decision.CanAttemptPresent(1)
			&& decision.HasPendingVisual(),
			"new demand resets backoff without losing pending state");

		(void)decision.CompleteAttempt(getDcFailure, 5, 11, 2);
		decision.ObserveDeviceGeneration(5);
		Check(decision.HasFailureBackoff(),
			"same device generation keeps backoff");
		decision.ObserveDeviceGeneration(6);
		Check(!decision.HasFailureBackoff(),
			"new device generation resets backoff");

		decision.RecordFailure(
			BarPresentFailureClass::DeviceResources, 6, 11, 3);
		decision.RecordFailure(
			BarPresentFailureClass::ReleaseDc, 6, 11, 4);
		Check(decision.ActiveFailureClass() == BarPresentFailureClass::ReleaseDc
			&& decision.ConsecutiveFailureCount() == 1,
			"failure class change starts a new series");

		Check(decision.CompleteAttempt(success, 6, 11, 5).IsCommitted()
			&& !decision.HasFailureBackoff()
			&& !decision.ShouldPresent(),
			"successful present clears retry and demand");
	}

	void TestPresentMappingUsesSuccessfulTuple()
	{
		constexpr BarPresentMappingTuple baseline{
			POINT{ 10, 20 }, SIZE{ 300, 80 }, SIZE{ 512, 256 }, 7 };
		BarPresentMappingTracker tracker;
		Check(tracker.Resolve(baseline)
			== BarPresentMappingMode::FullReplacement,
			"first mapping replaces the full window");

		tracker.CommitPresented(baseline);
		Check(tracker.Resolve(baseline) == BarPresentMappingMode::LocalDirty,
			"stable successful mapping allows local dirty");
		Check(!ShouldForceBarFullWindowReplacement(
				false, BarPresentMappingMode::LocalDirty)
			&& ShouldForceBarFullWindowReplacement(
				true, BarPresentMappingMode::LocalDirty)
			&& ShouldForceBarFullWindowReplacement(
				false, BarPresentMappingMode::FullReplacement),
			"viewport and present mapping share one full replacement decision");

		constexpr std::array changedMappings{
			BarPresentMappingTuple{
				POINT{ 11, 20 }, SIZE{ 300, 80 }, SIZE{ 512, 256 }, 7 },
			BarPresentMappingTuple{
				POINT{ 10, 20 }, SIZE{ 301, 80 }, SIZE{ 512, 256 }, 7 },
			BarPresentMappingTuple{
				POINT{ 10, 20 }, SIZE{ 300, 80 }, SIZE{ 513, 256 }, 7 },
			BarPresentMappingTuple{
				POINT{ 10, 20 }, SIZE{ 300, 80 }, SIZE{ 512, 256 }, 8 },
		};
		for (const auto& changed : changedMappings)
			Check(tracker.Resolve(changed)
				== BarPresentMappingMode::FullReplacement,
				"each mapping tuple field requires full replacement");

		const auto failedCandidate = changedMappings.front();
		Check(tracker.Resolve(failedCandidate)
			== BarPresentMappingMode::FullReplacement
			&& tracker.Resolve(failedCandidate)
				== BarPresentMappingMode::FullReplacement,
			"failed candidate remains full replacement on retry");
		Check(tracker.Resolve(baseline) == BarPresentMappingMode::LocalDirty,
			"failed candidate does not advance the successful tuple");
		tracker.CommitPresented(failedCandidate);
		Check(tracker.Resolve(failedCandidate)
			== BarPresentMappingMode::LocalDirty,
			"successful retry commits the candidate mapping");
	}
}

int RunPresentDecisionTests()
{
	TestDemandAccumulationAndPriority();
	TestCosmeticLeaseSkipRetainsPendingAndBounds();
	TestEveryPresentStageMustSucceed();
	TestRecreateTargetRequestsResourceReset();
	TestSharedDeviceLossClassification();
	TestBoundsAdvanceOnlyOnSuccess();
	TestResourceResetForcesFullDirtyWithoutLosingDemand();
	TestRepeatedFailureUsesBoundedBackoff();
	TestFailureRecoveryTriggers();
	TestPresentMappingUsesSuccessfulTuple();
	return failureCount;
}
