#include "../Inkeys/Inkeys/UI/Bar/Bar.PresentationAlpha.h"
#include "../Inkeys/Inkeys/UI/Bar/Bar.PresentDecision.h"

#include <array>
#include <iostream>

namespace
{
	bool Expect(bool condition, const char* name)
	{
		if (!condition) std::cerr << "[BarPresentationAlpha] failed: " << name << '\n';
		return condition;
	}
}

int RunBarPresentationAlphaTests()
{
	using Inkeys::UI::Bar::PresentationAlphaState;
	int failures = 0;

	PresentationAlphaState ordinary(false);
	auto attempt = ordinary.BeginAttempt();
	if (!Expect(!attempt.required && !attempt.fullWindow
		&& ordinary.CommittedAlpha() == 255,
		"no preview keeps alpha 255 without presentation demand")) ++failures;
	ordinary.CompleteAttempt(false);
	if (!Expect(!ordinary.HasDemand(),
		"completing a nonexistent attempt is a no-op")) ++failures;

	PresentationAlphaState preview(true);
	attempt = preview.BeginAttempt();
	if (!Expect(attempt.required && attempt.fullWindow && attempt.alpha == 0
		&& preview.CommittedAlpha() == 255,
		"preview starts with requested zero but no false commit")) ++failures;
	const auto initialRevision = preview.RequestedRevision();
	if (!Expect(!preview.Request(0)
		&& preview.RequestedRevision() == initialRevision,
		"duplicate request remains idempotent while demand exists")) ++failures;
	preview.CompleteAttempt(false);
	if (!Expect(preview.CommittedAlpha() == 255 && preview.HasDemand()
		&& preview.NeedsFullWindowPresent(),
		"failed attempt retains committed alpha and full retry")) ++failures;
	attempt = preview.BeginAttempt();
	preview.CompleteAttempt(true);
	if (!Expect(preview.CommittedAlpha() == 0 && !preview.HasDemand(),
		"successful retry commits attempted alpha")) ++failures;

	if (!Expect(preview.Request(255), "handoff requests visible Bar")) ++failures;
	attempt = preview.BeginAttempt();
	if (!Expect(preview.Request(96),
		"new alpha can arrive while an attempt is in flight")) ++failures;
	preview.CompleteAttempt(true);
	if (!Expect(preview.CommittedAlpha() == 255 && preview.RequestedAlpha() == 96
		&& preview.HasDemand(),
		"in-flight change survives older successful commit")) ++failures;
	attempt = preview.BeginAttempt();
	preview.CompleteAttempt(true);
	if (!Expect(preview.CommittedAlpha() == 96 && !preview.HasDemand(),
		"newer in-flight request commits on the next full frame")) ++failures;
	attempt = preview.BeginAttempt();
	if (!Expect(!attempt.required, "settled alpha has no new attempt")) ++failures;
	preview.CompleteAttempt(false);
	if (!Expect(preview.CommittedAlpha() == 96 && !preview.HasDemand(),
		"unrelated business present failure cannot create alpha retry")) ++failures;

	using Inkeys::UI::Bar::BarPresentAttemptResult;
	using Inkeys::UI::Bar::BarPresentDecision;
	constexpr RECT bounds{ 0, 0, 40, 20 };
	constexpr std::array stageFailures{
		BarPresentAttemptResult::Acquired(E_FAIL, TRUE, S_OK, S_OK, bounds),
		BarPresentAttemptResult::Acquired(S_OK, FALSE, S_OK, S_OK, bounds),
		BarPresentAttemptResult::Acquired(S_OK, TRUE, E_FAIL, S_OK, bounds),
		BarPresentAttemptResult::Acquired(S_OK, TRUE, S_OK, E_FAIL, bounds),
	};
	for (const auto& stageFailure : stageFailures)
	{
		PresentationAlphaState failure(true);
		(void)failure.BeginAttempt();
		BarPresentDecision decision;
		decision.AddDemand({ true, false, false });
		const auto completion = decision.CompleteAttempt(stageFailure);
		failure.CompleteAttempt(completion.IsCommitted());
		if (!Expect(failure.CommittedAlpha() == 255 && failure.HasDemand()
			&& failure.NeedsFullWindowPresent() && decision.NeedsFullDirty(),
			"each GetDC/ULW/ReleaseDC/EndDraw failure rolls back alpha")) ++failures;
	}
	return failures;
}
