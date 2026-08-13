#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

import Inkeys.UI.Bar.Animation;

int RunWakeSignalTests();
int RunPresentDecisionTests();
int RunSurfaceTests();
int RunMessageTests();
int RunWindowTests();
int RunDirtyRegionTests();
int RunWindowGeometryTests();
int RunFramePacingTests(bool benchmark);
int RunToggleClickCoalescerTests();
int RunRenderSchedulerTests();
int RunPptUiTests();

namespace
{
	int failureCount = 0;
	volatile double benchmarkSink = 0.0;

	void Check(bool condition, std::string_view name)
	{
		if (condition) return;
		++failureCount;
		std::cerr << "FAIL " << name << '\n';
	}

	bool Near(double lhs, double rhs, double epsilon = 0.000001)
	{
		return std::abs(lhs - rhs) <= epsilon;
	}

	double ApplyLegacyPowCurve(BarUiCurveEnum curve, double progress)
	{
		constexpr double back = 1.1;
		progress = std::clamp(progress, 0.0, 1.0);
		switch (curve)
		{
		case BarUiCurveEnum::EaseOutCubic:
			return 1.0 - std::pow(1.0 - progress, 3.0);
		case BarUiCurveEnum::EaseInOutCubic:
			return progress < 0.5
				? 4.0 * progress * progress * progress
				: 1.0 - std::pow(-2.0 * progress + 2.0, 3.0) / 2.0;
		case BarUiCurveEnum::EaseOutBack:
			return 1.0 + (back + 1.0) * std::pow(progress - 1.0, 3.0)
				+ back * std::pow(progress - 1.0, 2.0);
		case BarUiCurveEnum::EaseInOutBack:
		{
			double c2 = back * 1.525;
			return progress < 0.5
				? std::pow(2.0 * progress, 2.0)
					* ((c2 + 1.0) * 2.0 * progress - c2) / 2.0
				: (std::pow(2.0 * progress - 2.0, 2.0)
					* ((c2 + 1.0) * (progress * 2.0 - 2.0) + c2) + 2.0) / 2.0;
		}
		default: return progress;
		}
	}

	void TestCurvesAndTimelines()
	{
		struct CurveSample
		{
			BarUiCurveEnum curve;
			double progress;
			double expected;
			std::string_view name;
		};
		constexpr std::array curveSamples{
			CurveSample{ BarUiCurveEnum::Linear, 0.25, 0.25, "linear quarter" },
			CurveSample{ BarUiCurveEnum::EaseInSine, 0.5, 0.2928932188134524,
				"sine in midpoint" },
			CurveSample{ BarUiCurveEnum::EaseOutSine, 0.5, 0.7071067811865476,
				"sine out midpoint" },
			CurveSample{ BarUiCurveEnum::EaseInOutSine, 0.5, 0.5,
				"sine in-out midpoint" },
			CurveSample{ BarUiCurveEnum::EaseInCubic, 0.5, 0.125,
				"cubic in midpoint" },
			CurveSample{ BarUiCurveEnum::EaseOutCubic, 0.5, 0.875,
				"cubic out midpoint" },
			CurveSample{ BarUiCurveEnum::EaseInOutCubic, 0.25, 0.0625,
				"cubic in-out quarter" },
			CurveSample{ BarUiCurveEnum::EaseInBack, 0.5, -0.0125,
				"back in undershoot" },
			CurveSample{ BarUiCurveEnum::EaseOutBack, 0.5, 1.0125,
				"back out overshoot" },
			CurveSample{ BarUiCurveEnum::EaseInOutBack, 0.25, -0.04234375,
				"back in-out undershoot" },
		};
		for (const auto& sample : curveSamples)
		{
			Check(Near(BarUiApplyCurve(sample.curve, sample.progress), sample.expected),
				sample.name);
		}

		struct LegacyCurveCase
		{
			BarUiCurveEnum curve;
			std::string_view name;
		};
		constexpr std::array legacyCurveCases{
			LegacyCurveCase{ BarUiCurveEnum::EaseOutCubic,
				"optimized cubic out matches legacy pow" },
			LegacyCurveCase{ BarUiCurveEnum::EaseInOutCubic,
				"optimized cubic in-out matches legacy pow" },
			LegacyCurveCase{ BarUiCurveEnum::EaseOutBack,
				"optimized back out matches legacy pow" },
			LegacyCurveCase{ BarUiCurveEnum::EaseInOutBack,
				"optimized back in-out matches legacy pow" },
		};
		constexpr double legacyCurveEpsilon = 1.0e-12;
		for (const auto& legacyCase : legacyCurveCases)
		{
			// 冻结旧公式并逐点比对，确保展开乘法不改变曲线视觉语义。
			bool matchesLegacy = true;
			for (int point = 0; point <= 1000; ++point)
			{
				double progress = static_cast<double>(point) / 1000.0;
				if (Near(BarUiApplyCurve(legacyCase.curve, progress),
					ApplyLegacyPowCurve(legacyCase.curve, progress),
					legacyCurveEpsilon)) continue;
				matchesLegacy = false;
				break;
			}
			Check(matchesLegacy, legacyCase.name);
		}

		constexpr std::array curves{
			BarUiCurveEnum::Linear,
			BarUiCurveEnum::EaseInSine,
			BarUiCurveEnum::EaseOutSine,
			BarUiCurveEnum::EaseInOutSine,
			BarUiCurveEnum::EaseInCubic,
			BarUiCurveEnum::EaseOutCubic,
			BarUiCurveEnum::EaseInOutCubic,
			BarUiCurveEnum::EaseInBack,
			BarUiCurveEnum::EaseOutBack,
			BarUiCurveEnum::EaseInOutBack,
		};
		for (auto curve : curves)
		{
			Check(Near(BarUiApplyCurve(curve, 0.0), 0.0), "curve starts at zero");
			Check(Near(BarUiApplyCurve(curve, 1.0), 1.0), "curve ends at one");
			Check(Near(BarUiApplyCurveRange(curve, 0.35, 0.35), 0.0),
				"curve range starts at zero");
			Check(Near(BarUiApplyCurveRange(curve, 0.35, 1.0), 1.0),
				"curve range ends at one");
		}
		Check(Near(BarUiApplyCurve(BarUiCurveEnum::EaseOutBack, -1.0), 0.0),
			"curve clamps negative progress");
		Check(Near(BarUiApplyCurve(BarUiCurveEnum::EaseInBack, 2.0), 1.0),
			"curve clamps progress above one");

		// 续接区间必须从当前相位重新归一化，Back 则从剩余段重建回弹。
		Check(Near(BarUiApplyCurveRange(
			BarUiCurveEnum::Linear, 0.25, 0.625), 0.5),
			"linear range continuation midpoint");
		Check(Near(BarUiApplyCurveRange(
			BarUiCurveEnum::EaseInCubic, 0.25, 0.625), 0.23214285714285715),
			"cubic range continuation midpoint");
		Check(Near(BarUiApplyCurveRange(
			BarUiCurveEnum::EaseOutBack, 0.25, 0.625), 1.0125),
			"back range continuation rebuilds remaining segment");

		BarUiTimelineClass timeline;
		timeline.Restart(2.0);
		timeline.Advance(1.0, 1.0);
		Check(Near(timeline.GetProgress(), 0.5), "timeline reaches midpoint");
		Check(Near(timeline.GetRemainingDuration(), 1.0), "timeline remaining duration");
		Check(timeline.CanJoin(), "timeline joins at exact midpoint");
		timeline.Advance(0.001, 1.0);
		Check(!timeline.CanJoin(), "timeline rejects after midpoint");

		BarUiKeyframeTimelineClass keyframe;
		keyframe.Start(1.0, 0.5);
		auto first = keyframe.Advance(0.6, 1.0);
		Check(first.reachedKeyframe && Near(first.progress, 0.5),
			"keyframe crossing reports midpoint once");
		auto second = keyframe.Advance(0.1, 1.0);
		Check(!second.reachedKeyframe, "keyframe is not reported twice");
		auto final = keyframe.Advance(1.0, 1.0);
		Check(final.finished && !keyframe.IsActive(), "keyframe timeline finishes");

		// 内容切换依赖同一时间轴：跨中点时替换一次，前后帧都不能重复替换。
		std::wstring content = L"old";
		const std::wstring pendingContent = L"new";
		int contentReplacementCount = 0;
		auto ApplyContentKeyframe = [&](const BarUiKeyframeTimelineResultClass& result)
			{
				if (!result.reachedKeyframe) return;
				content = pendingContent;
				++contentReplacementCount;
			};
		BarUiKeyframeTimelineClass contentTransition;
		contentTransition.Start(1.0, 0.5);
		ApplyContentKeyframe(contentTransition.Advance(0.2, 1.0));
		Check(content == L"old" && contentReplacementCount == 0,
			"content remains old before transition keyframe");
		auto contentCrossing = contentTransition.Advance(0.4, 1.0);
		ApplyContentKeyframe(contentCrossing);
		Check(Near(contentCrossing.progress, 0.5) && content == L"new"
			&& contentReplacementCount == 1,
			"content changes exactly at transition keyframe");
		ApplyContentKeyframe(contentTransition.Advance(0.1, 1.0));
		ApplyContentKeyframe(contentTransition.Advance(1.0, 1.0));
		Check(contentReplacementCount == 1,
			"content transition does not apply target twice");

		BarUiKeyframeTimelineClass immediateContentTransition;
		immediateContentTransition.Start(0.0, 0.5);
		auto immediate = immediateContentTransition.Advance(0.01, 1.0);
		Check(immediate.reachedKeyframe && immediate.finished,
			"zero-duration content transition applies immediately");
	}

	void TestTargetsAndAdvancement()
	{
		BarUiValueClass value(0.0);
		Check(value.SetTar(10.0, 1.0), "new value target starts animation");
		Check(!value.SetTar(10.0, 1.0), "same value target is a no-op");
		BarUiAnimationAdvanceContextClass context{ 0.25, 1.0, true, false };
		auto first = BarUiAdvanceAnimation(value, context);
		Check(first.changed && first.active, "value advances and remains active");
		double interruptedAt = value.val;
		Check(value.SetTar(20.0, 1.0), "interruption accepts a new target");
		Check(Near(value.startV, interruptedAt), "interruption captures current visual value");
		Check(value.SetTar(20.0, 1.0, std::nullopt, true),
			"force restart accepts the same target");

		BarUiValueClass middle(0.0);
		middle.SetTar(1.0, 1.0, 0.8);
		auto midpoint = BarUiAdvanceAnimation(
			middle, BarUiAnimationAdvanceContextClass{ 0.5, 1.0, true, false });
		Check(midpoint.changed && midpoint.active && Near(middle.val, 0.8),
			"middle keyframe uses the exact half-time value");
		auto finished = BarUiAdvanceAnimation(
			middle, BarUiAnimationAdvanceContextClass{ 0.5, 1.0, true, false });
		Check(finished.changed && !finished.active && Near(middle.val, 1.0),
			"final value is changed but no longer active");

		BarUiColorClass quantized(RGB(0, 0, 0));
		quantized.SetTar(RGB(1, 0, 0), 1.0);
		auto quantizedFrame = BarUiAdvanceAnimation(
			quantized, BarUiAnimationAdvanceContextClass{ 0.0001, 1.0, true, false });
		Check(!quantizedFrame.changed && quantizedFrame.active,
			"quantized color remains scheduled when the pixel value is unchanged");

		BarUiColorClass disabledHover(RGB(0, 0, 0));
		disabledHover.animateWhenDisabled = true;
		disabledHover.SetTar(RGB(255, 255, 255), 1.0);
		auto disabledFrame = BarUiAdvanceAnimation(disabledHover,
			BarUiAnimationAdvanceContextClass{ 0.25, 1.0e12, false, false });
		Check(disabledFrame.active,
			"animateWhenDisabled uses real-time speed instead of the finish multiplier");

		BarUiPctClass pct(0.5);
		pct.SetTar(2.0, 0.0);
		auto pctResult = BarUiAdvanceAnimation(
			pct, BarUiAnimationAdvanceContextClass{ 0.01, 1.0, true, false });
		Check(pctResult.changed && !pctResult.active && Near(pct.val, 1.0),
			"opacity clamps and finishes invalid target range");

		BarUiValueClass finiteValue(3.0);
		const double infinity = std::numeric_limits<double>::infinity();
		const double quietNaN = std::numeric_limits<double>::quiet_NaN();
		Check(!finiteValue.SetTar(infinity, 1.0)
			&& Near(finiteValue.tar, 3.0),
			"value rejects a non-finite target without changing the transaction");
		Check(!finiteValue.SetTar(4.0, 1.0, quietNaN)
			&& Near(finiteValue.tar, 3.0),
			"value rejects a non-finite middle keyframe");
		finiteValue.SetDirect(quietNaN);
		Check(Near(finiteValue.val, 3.0) && Near(finiteValue.tar, 3.0),
			"direct value ignores non-finite input");
		// 模拟旧版本已经写入的异常目标，完成路径必须恢复有限状态。
		finiteValue.tar = infinity;
		auto recoveredValue = BarUiAdvanceAnimation(
			finiteValue, BarUiAnimationAdvanceContextClass{ 0.01, 1.0, true, false });
		Check(!recoveredValue.active && std::isfinite(static_cast<double>(finiteValue.val))
			&& std::isfinite(static_cast<double>(finiteValue.tar))
			&& Near(finiteValue.val, 3.0),
			"value completion recovers a legacy non-finite target");

		// 两段曲线分别覆盖进入和回弹，且跨中点帧精确落在 middleV。
		BarUiValueClass customValue(0.0);
		const BarUiCurveSpecClass valueCurve{
			BarUiCurveEnum::EaseInCubic,
			BarUiCurveEnum::EaseOutBack,
			0.0,
			false,
		};
		customValue.SetTar(10.0, 1.0, 8.0, true, valueCurve);
		BarUiAdvanceAnimation(customValue,
			BarUiAnimationAdvanceContextClass{ 0.25, 1.0, true, false });
		Check(Near(customValue.val, 1.0), "custom value first curve quarter");
		BarUiAdvanceAnimation(customValue,
			BarUiAnimationAdvanceContextClass{ 0.25, 1.0, true, false });
		Check(Near(customValue.val, 8.0), "custom value exact middle keyframe");
		BarUiAdvanceAnimation(customValue,
			BarUiAnimationAdvanceContextClass{ 0.25, 1.0, true, false });
		Check(Near(customValue.val, 10.025), "custom value second curve overshoots");
		auto customValueFinal = BarUiAdvanceAnimation(customValue,
			BarUiAnimationAdvanceContextClass{ 0.25, 1.0, true, false });
		Check(customValueFinal.changed && !customValueFinal.active
			&& Near(customValue.val, 10.0),
			"custom value settles on final target");

		BarUiPctClass customPct(0.0);
		const BarUiCurveSpecClass pctCurve{
			BarUiCurveEnum::EaseInSine,
			BarUiCurveEnum::EaseOutSine,
			0.0,
			false,
		};
		customPct.SetTar(1.0, 1.0, 0.4, true, pctCurve);
		BarUiAdvanceAnimation(customPct,
			BarUiAnimationAdvanceContextClass{ 0.25, 1.0, true, false });
		Check(Near(customPct.val, 0.117157287525381),
			"custom opacity first curve quarter");
		BarUiAdvanceAnimation(customPct,
			BarUiAnimationAdvanceContextClass{ 0.25, 1.0, true, false });
		Check(Near(customPct.val, 0.4), "custom opacity exact middle keyframe");
		BarUiAdvanceAnimation(customPct,
			BarUiAnimationAdvanceContextClass{ 0.25, 1.0, true, false });
		Check(Near(customPct.val, 0.824264068711929),
			"custom opacity second curve quarter");
		auto customPctFinal = BarUiAdvanceAnimation(customPct,
			BarUiAnimationAdvanceContextClass{ 0.25, 1.0, true, false });
		Check(customPctFinal.changed && !customPctFinal.active
			&& Near(customPct.val, 1.0),
			"custom opacity settles on final target");

		BarUiTimelineClass continuedTimeline;
		continuedTimeline.Restart(1.0);
		continuedTimeline.Advance(0.25, 1.0);
		BarUiValueClass continuedValue(2.0);
		const BarUiCurveSpecClass continuedCurve{
			BarUiCurveEnum::EaseInOutCubic,
			BarUiCurveEnum::EaseInOutCubic,
			continuedTimeline.GetProgress(),
			true,
		};
		continuedValue.SetTar(10.0, continuedTimeline.GetRemainingDuration(),
			std::nullopt, true, continuedCurve);
		BarUiAdvanceAnimation(continuedValue,
			BarUiAnimationAdvanceContextClass{ 0.375, 1.0, true, false });
		Check(Near(continuedValue.val, 8.2),
			"continued value preserves remaining cubic curve range");
		auto continuedValueFinal = BarUiAdvanceAnimation(continuedValue,
			BarUiAnimationAdvanceContextClass{ 0.375, 1.0, true, false });
		Check(!continuedValueFinal.active && Near(continuedValue.val, 10.0),
			"continued value shares original finish time");

		BarUiValueClass postKeyframeValue(15.0);
		const BarUiCurveSpecClass postKeyframeCurve{
			BarUiCurveEnum::EaseInCubic,
			BarUiCurveEnum::EaseOutBack,
			0.75,
			true,
		};
		postKeyframeValue.SetTar(20.0, 0.25, 12.0, true, postKeyframeCurve);
		Check(!static_cast<bool>(postKeyframeValue.hasMiddleV)
			&& static_cast<BarUiCurveEnum>(postKeyframeValue.activeCurve)
				== BarUiCurveEnum::EaseOutBack
			&& Near(postKeyframeValue.timelineStartProgress, 0.5),
			"continued value after keyframe enters second segment directly");

		BarUiColorClass continuedColor(RGB(10, 20, 30));
		const BarUiCurveSpecClass colorCurve{
			BarUiCurveEnum::EaseInOutSine,
			BarUiCurveEnum::EaseInOutSine,
			0.5,
			true,
		};
		continuedColor.SetTar(RGB(210, 120, 80), 0.5, colorCurve);
		BarUiAdvanceAnimation(continuedColor,
			BarUiAnimationAdvanceContextClass{ 0.25, 1.0, true, false });
		Check(static_cast<COLORREF>(continuedColor.val) == RGB(151, 91, 65),
			"continued color uses remaining sine range");
		auto continuedColorFinal = BarUiAdvanceAnimation(continuedColor,
			BarUiAnimationAdvanceContextClass{ 0.25, 1.0, true, false });
		Check(!continuedColorFinal.active
			&& static_cast<COLORREF>(continuedColor.val) == RGB(210, 120, 80),
			"continued color reaches exact target");
		Check(MixBarUiColor(RGB(1, 3, 5), RGB(2, 4, 6), 0.5) == RGB(2, 4, 6),
			"color mixing rounds channels consistently");
	}

	void TestKeyframeTimelineTransactions()
	{
		static_assert(std::is_copy_constructible_v<BarUiKeyframeTimelineClass>);
		static_assert(std::is_copy_assignable_v<BarUiKeyframeTimelineClass>);
		static_assert(std::is_move_constructible_v<BarUiKeyframeTimelineClass>);
		static_assert(std::is_move_assignable_v<BarUiKeyframeTimelineClass>);

		BarUiKeyframeTimelineClass generationTimeline;
		uint64_t startGeneration = generationTimeline.Start(1.0, 0.5);
		auto generationAdvance = generationTimeline.Advance(0.25, 1.0);
		Check(generationAdvance.generation == startGeneration,
			"timeline advance returns the start generation");
		uint64_t cancelGeneration = generationTimeline.Cancel();
		Check(cancelGeneration != startGeneration && !generationTimeline.IsActive(),
			"timeline cancel advances generation and deactivates");
		bool oldGenerationCurrent = generationTimeline.Transaction(
			[&](BarUiKeyframeTimelineClass::LockedView& timeline)
			{
				return timeline.IsCurrentGeneration(startGeneration);
			});
		Check(!oldGenerationCurrent, "cancel rejects the prior generation");

		std::wstring requestedTarget = L"old";
		uint64_t requestedGeneration = 0;
		auto TransitionTo = [&](const std::wstring& target)
			{
				return generationTimeline.Transaction(
					[&](BarUiKeyframeTimelineClass::LockedView& timeline)
					{
						if (timeline.IsActive() && requestedTarget == target)
							return false;
						requestedTarget = target;
						requestedGeneration = timeline.Start(1.0, 0.5);
						return true;
					});
			};
		Check(TransitionTo(L"new"), "transaction publishes a new content target");
		uint64_t firstRequestedGeneration = requestedGeneration;
		Check(!TransitionTo(L"new")
			&& requestedGeneration == firstRequestedGeneration,
			"same content target is a generation-preserving no-op");

		struct TransitionPayload
		{
			int target = 0;
			int committedTarget = 0;
			uint64_t generation = 0;
		};
		BarUiKeyframeTimelineClass concurrentTimeline;
		TransitionPayload payload;
		concurrentTimeline.Transaction(
			[&](BarUiKeyframeTimelineClass::LockedView& timeline)
			{
				payload.target = 1;
				payload.generation = timeline.Start(1.0, 0.5);
			});

		std::atomic<bool> snapshotReady = false;
		std::atomic<bool> newTargetPublished = false;
		std::atomic<bool> coherentSnapshot = false;
		std::atomic<bool> staleCommitAccepted = false;
		std::thread renderThread([&]()
			{
				struct RenderSnapshot
				{
					BarUiKeyframeTimelineResultClass result;
					int target = 0;
					uint64_t payloadGeneration = 0;
				};
				RenderSnapshot snapshot = concurrentTimeline.Transaction(
					[&](BarUiKeyframeTimelineClass::LockedView& timeline)
					{
						return RenderSnapshot{
							timeline.Advance(0.6, 1.0),
							payload.target,
							payload.generation,
						};
					});
				coherentSnapshot.store(
					snapshot.result.generation == snapshot.payloadGeneration
					&& snapshot.target == 1
					&& snapshot.result.reachedKeyframe
					&& Near(snapshot.result.progress, 0.5),
					std::memory_order_release);
				snapshotReady.store(true, std::memory_order_release);
				while (!newTargetPublished.load(std::memory_order_acquire))
					YieldProcessor();

				bool committed = concurrentTimeline.Transaction(
					[&](BarUiKeyframeTimelineClass::LockedView& timeline)
					{
						if (!timeline.IsCurrentGeneration(snapshot.result.generation))
							return false;
						payload.committedTarget = snapshot.target;
						timeline.Cancel();
						return true;
					});
				staleCommitAccepted.store(committed, std::memory_order_release);
			});

		while (!snapshotReady.load(std::memory_order_acquire)) YieldProcessor();
		concurrentTimeline.Transaction(
			[&](BarUiKeyframeTimelineClass::LockedView& timeline)
			{
				payload.target = 2;
				payload.generation = timeline.Start(1.0, 0.5);
			});
		newTargetPublished.store(true, std::memory_order_release);
		renderThread.join();

		bool newTransactionIntact = concurrentTimeline.Transaction(
			[&](BarUiKeyframeTimelineClass::LockedView& timeline)
			{
				return timeline.IsActive()
					&& timeline.IsCurrentGeneration(payload.generation)
					&& payload.target == 2 && payload.committedTarget == 0;
			});
		Check(coherentSnapshot.load(std::memory_order_acquire),
			"timeline and content payload snapshot as one transaction");
		Check(!staleCommitAccepted.load(std::memory_order_acquire)
			&& newTransactionIntact,
			"old advance cannot commit across a newer start generation");

		BarUiKeyframeTimelineClass sourceTimeline;
		sourceTimeline.Start(2.0, 0.5);
		auto sourceAdvance = sourceTimeline.Advance(0.5, 1.0);
		BarUiKeyframeTimelineClass copiedTimeline(sourceTimeline);
		auto copiedAdvance = copiedTimeline.Advance(0.0, 1.0);
		bool sourceTokenAcceptedByCopy = copiedTimeline.Transaction(
			[&](BarUiKeyframeTimelineClass::LockedView& timeline)
			{
				return timeline.IsCurrentGeneration(sourceAdvance.generation);
			});
		Check(copiedTimeline.IsActive() && Near(copiedAdvance.progress, 0.25)
			&& !sourceTokenAcceptedByCopy,
			"timeline copy preserves state with an independent generation");

		uint64_t copiedGeneration = copiedAdvance.generation;
		BarUiKeyframeTimelineClass movedTimeline(std::move(copiedTimeline));
		auto movedAdvance = movedTimeline.Advance(0.0, 1.0);
		bool copiedTokenAcceptedByMove = movedTimeline.Transaction(
			[&](BarUiKeyframeTimelineClass::LockedView& timeline)
			{
				return timeline.IsCurrentGeneration(copiedGeneration);
			});
		Check(movedTimeline.IsActive() && Near(movedAdvance.progress, 0.25)
			&& !copiedTokenAcceptedByMove,
			"timeline move preserves state with an independent generation");

		BarUiKeyframeTimelineClass copyAssignedTimeline;
		copyAssignedTimeline = sourceTimeline;
		BarUiKeyframeTimelineClass moveAssignedTimeline;
		moveAssignedTimeline = std::move(movedTimeline);
		Check(copyAssignedTimeline.IsActive() && moveAssignedTimeline.IsActive()
			&& Near(copyAssignedTimeline.Advance(0.0, 1.0).progress, 0.25)
			&& Near(moveAssignedTimeline.Advance(0.0, 1.0).progress, 0.25),
			"timeline copy and move assignment preserve active progress");
	}

	void TestConcurrentAnimationPublication()
	{
		constexpr int iterations = 20000;
		const BarUiAnimationAdvanceContextClass context{
			0.000000001, 1.0, true, false };

		auto RunStress = [&](auto& animation, auto reset, auto publish,
			std::string_view name)
			{
				std::atomic<bool> start = false;
				std::atomic<bool> done = false;
				std::atomic<int> publicationPhase = 0;
				std::atomic<int> tornCompletionCount = 0;
				std::thread producer([&]()
					{
						while (!start.load(std::memory_order_acquire)) YieldProcessor();
						for (int index = 0; index < iterations; ++index)
						{
							reset(animation);
							publicationPhase.store(1, std::memory_order_release);
							publish(animation, index);
							publicationPhase.store(0, std::memory_order_release);
						}
						done.store(true, std::memory_order_release);
					});

				int advanceCount = 0;
				start.store(true, std::memory_order_release);
				while (!done.load(std::memory_order_acquire))
				{
					auto result = BarUiAdvanceAnimation(animation, context);
					++advanceCount;
					// 发布中的请求始终带正时长；changed + inactive 表示读到了拆开的字段代次。
					if (publicationPhase.load(std::memory_order_acquire) == 1
						&& result.changed && !result.active)
						tornCompletionCount.fetch_add(1, std::memory_order_relaxed);
				}
				producer.join();
				Check(advanceCount > 0, std::string(name) + " stress advanced");
				Check(tornCompletionCount.load(std::memory_order_relaxed) == 0,
					std::string(name) + " publishes one coherent transaction");
			};

		BarUiValueClass value(0.0);
		RunStress(value,
			[](auto& item) { item.SetDirect(0.0); },
			[](auto& item, int index)
			{
				item.SetTar(index % 2 == 0 ? 1.0 : -1.0,
					10.0, std::nullopt, true);
			},
			"value");

		BarUiPctClass pct(0.5);
		RunStress(pct,
			[](auto& item) { item.SetDirect(0.5); },
			[](auto& item, int index)
			{
				item.SetTar(index % 2 == 0 ? 0.1 : 0.9,
					10.0, std::nullopt, true);
			},
			"pct");

		BarUiColorClass color(RGB(0, 0, 0));
		RunStress(color,
			[](auto& item) { item.SetDirect(RGB(0, 0, 0)); },
			[](auto& item, int index)
			{
				item.SetTar(index % 2 == 0
					? RGB(255, 64, 32) : RGB(32, 128, 255), 10.0);
			},
			"color");
	}

	struct BenchmarkResult
	{
		double medianNanoseconds = 0.0;
		double p95Nanoseconds = 0.0;
		double noisePercent = 0.0;
		std::uint64_t medianCycles = 0;
	};

	template <typename Operation>
	BenchmarkResult Measure(std::uint64_t iterations, Operation&& operation)
	{
		constexpr int warmupRuns = 3;
		constexpr int measuredRuns = 11;
		LARGE_INTEGER frequency{};
		QueryPerformanceFrequency(&frequency);
		std::vector<double> elapsed;
		std::vector<std::uint64_t> cycles;
		elapsed.reserve(measuredRuns);
		cycles.reserve(measuredRuns);

		for (int run = -warmupRuns; run < measuredRuns; ++run)
		{
			ULONG64 cycleStart = 0;
			ULONG64 cycleEnd = 0;
			LARGE_INTEGER start{};
			LARGE_INTEGER end{};
			QueryThreadCycleTime(GetCurrentThread(), &cycleStart);
			QueryPerformanceCounter(&start);
			double sink = 0.0;
			for (std::uint64_t i = 0; i < iterations; ++i) sink += operation(i);
			QueryPerformanceCounter(&end);
			QueryThreadCycleTime(GetCurrentThread(), &cycleEnd);
			benchmarkSink = sink;
			if (run < 0) continue;
			elapsed.push_back(
				static_cast<double>(end.QuadPart - start.QuadPart) * 1.0e9
				/ static_cast<double>(frequency.QuadPart) / static_cast<double>(iterations));
			cycles.push_back((cycleEnd - cycleStart) / iterations);
		}

		std::sort(elapsed.begin(), elapsed.end());
		std::sort(cycles.begin(), cycles.end());
		double median = elapsed[elapsed.size() / 2];
		double p95 = elapsed[static_cast<std::size_t>(
			std::ceil(static_cast<double>(elapsed.size()) * 0.95)) - 1];
		double noise = median > 0.0
			? (elapsed.back() - elapsed.front()) * 100.0 / median : 0.0;
		return { median, p95, noise, cycles[cycles.size() / 2] };
	}

	void PrintBenchmark(std::string_view name, std::uint64_t iterations,
		const BenchmarkResult& result)
	{
		std::cout << "BENCH " << name
			<< " iterations=" << iterations
			<< " median_ns=" << result.medianNanoseconds
			<< " p95_ns=" << result.p95Nanoseconds
			<< " noise_pct=" << result.noisePercent
			<< " median_thread_cycles=" << result.medianCycles << '\n';
	}

	void RunBenchmarks()
	{
		struct CurveBenchmarkCase
		{
			std::string_view name;
			BarUiCurveEnum curve;
		};
		constexpr std::array curveBenchmarks{
			CurveBenchmarkCase{ "curve_linear", BarUiCurveEnum::Linear },
			CurveBenchmarkCase{ "curve_ease_in_sine", BarUiCurveEnum::EaseInSine },
			CurveBenchmarkCase{ "curve_ease_out_sine", BarUiCurveEnum::EaseOutSine },
			CurveBenchmarkCase{ "curve_ease_in_out_sine", BarUiCurveEnum::EaseInOutSine },
			CurveBenchmarkCase{ "curve_ease_in_cubic", BarUiCurveEnum::EaseInCubic },
			CurveBenchmarkCase{ "curve_ease_out_cubic", BarUiCurveEnum::EaseOutCubic },
			CurveBenchmarkCase{ "curve_ease_in_out_cubic", BarUiCurveEnum::EaseInOutCubic },
			CurveBenchmarkCase{ "curve_ease_in_back", BarUiCurveEnum::EaseInBack },
			CurveBenchmarkCase{ "curve_ease_out_back", BarUiCurveEnum::EaseOutBack },
			CurveBenchmarkCase{ "curve_ease_in_out_back", BarUiCurveEnum::EaseInOutBack },
		};
		constexpr std::uint64_t curveIterations = 100000;
		for (const auto& benchmark : curveBenchmarks)
		{
			PrintBenchmark(benchmark.name, curveIterations, Measure(curveIterations,
				[curve = benchmark.curve](std::uint64_t i)
				{
					return BarUiApplyCurve(curve,
						static_cast<double>(i % 1000) / 999.0);
				}));
		}
		PrintBenchmark("curve_range_cubic", curveIterations, Measure(curveIterations,
			[](std::uint64_t i)
			{
				double progress = 0.25
					+ 0.75 * static_cast<double>(i % 1000) / 999.0;
				return BarUiApplyCurveRange(
					BarUiCurveEnum::EaseInOutCubic, 0.25, progress);
			}));
		PrintBenchmark("curve_range_back", curveIterations, Measure(curveIterations,
			[](std::uint64_t i)
			{
				double progress = 0.25
					+ 0.75 * static_cast<double>(i % 1000) / 999.0;
				return BarUiApplyCurveRange(
					BarUiCurveEnum::EaseOutBack, 0.25, progress);
			}));

		constexpr std::uint64_t iterations = 20000;

		BarUiValueClass noOp(1.0);
		PrintBenchmark("same_target_noop", iterations, Measure(iterations,
			[&](std::uint64_t) { return noOp.SetTar(1.0, 0.4) ? 1.0 : 0.0; }));

		std::array<BarUiValueClass, 57> inactive{};
		for (auto& value : inactive) value.SetDirect(1.0);
		PrintBenchmark("inactive_value_scan_57", iterations, Measure(iterations,
			[&](std::uint64_t)
			{
				double active = 0.0;
				for (auto& value : inactive) active += value.IsSame() ? 0.0 : 1.0;
				return active;
			}));

		std::array<BarUiValueClass, 57> oneActive{};
		for (auto& value : oneActive) value.SetDirect(0.0);
		oneActive[28].SetTar(1.0, 1.0);
		PrintBenchmark("one_active_value_scan_57", iterations, Measure(iterations,
			[&](std::uint64_t)
			{
				double changed = 0.0;
				for (auto& value : oneActive)
				{
					if (value.IsSame()) continue;
					auto result = BarUiAdvanceAnimation(value,
						BarUiAnimationAdvanceContextClass{ 1.0 / 60000.0, 1.0, true, false });
					changed += result.changed || result.active ? 1.0 : 0.0;
				}
				if (oneActive[28].IsSame())
				{
					double nextTarget = oneActive[28].tar < 0.5 ? 1.0 : 0.0;
					oneActive[28].SetTar(nextTarget, 1.0);
				}
				return changed;
			}));

		BarUiValueClass benchmarkValue(0.0);
		const BarUiCurveSpecClass benchmarkValueCurve{
			BarUiCurveEnum::EaseInCubic,
			BarUiCurveEnum::EaseOutBack,
			0.0,
			false,
		};
		PrintBenchmark("value_custom_keyframe_advance", iterations, Measure(iterations,
			[&](std::uint64_t i)
			{
				if (i == 0 || benchmarkValue.IsSame())
				{
					benchmarkValue.SetDirect(0.0);
					benchmarkValue.SetTar(
						10.0, 1.0, 8.0, true, benchmarkValueCurve);
				}
				auto result = BarUiAdvanceAnimation(benchmarkValue,
					BarUiAnimationAdvanceContextClass{
						1.0 / 10000.0, 1.0, true, false });
				return static_cast<double>(benchmarkValue.val)
					+ (result.active ? 1.0 : 0.0);
			}));

		BarUiPctClass benchmarkPct(0.0);
		const BarUiCurveSpecClass benchmarkPctCurve{
			BarUiCurveEnum::EaseInSine,
			BarUiCurveEnum::EaseOutSine,
			0.0,
			false,
		};
		PrintBenchmark("pct_custom_keyframe_advance", iterations, Measure(iterations,
			[&](std::uint64_t i)
			{
				if (i == 0 || benchmarkPct.IsSame())
				{
					benchmarkPct.SetDirect(0.0);
					benchmarkPct.SetTar(
						1.0, 1.0, 0.4, true, benchmarkPctCurve);
				}
				auto result = BarUiAdvanceAnimation(benchmarkPct,
					BarUiAnimationAdvanceContextClass{
						1.0 / 10000.0, 1.0, true, false });
				return static_cast<double>(benchmarkPct.val)
					+ (result.active ? 1.0 : 0.0);
			}));

		BarUiColorClass benchmarkColor(RGB(10, 20, 30));
		const BarUiCurveSpecClass benchmarkColorCurve{
			BarUiCurveEnum::EaseInOutSine,
			BarUiCurveEnum::EaseInOutSine,
			0.5,
			true,
		};
		PrintBenchmark("color_continued_advance", iterations, Measure(iterations,
			[&](std::uint64_t i)
			{
				if (i == 0 || benchmarkColor.IsSame())
				{
					benchmarkColor.SetDirect(RGB(10, 20, 30));
					benchmarkColor.SetTar(
						RGB(210, 120, 80), 0.5, benchmarkColorCurve);
				}
				auto result = BarUiAdvanceAnimation(benchmarkColor,
					BarUiAnimationAdvanceContextClass{
						1.0 / 20000.0, 1.0, true, false });
				return static_cast<double>(static_cast<COLORREF>(benchmarkColor.val))
					+ (result.active ? 1.0 : 0.0);
			}));

		BarUiKeyframeTimelineClass benchmarkContentTimeline;
		benchmarkContentTimeline.Start(1.0, 0.5);
		PrintBenchmark("content_transition_timeline_advance", iterations,
			Measure(iterations,
				[&](std::uint64_t i)
				{
					if (i == 0 || !benchmarkContentTimeline.IsActive())
						benchmarkContentTimeline.Start(1.0, 0.5);
					auto result = benchmarkContentTimeline.Advance(
						1.0 / 10000.0, 1.0);
					return result.progress + (result.reachedKeyframe ? 1.0 : 0.0);
				}));
	}
}

int main(int argc, char** argv)
{
	bool benchmark = false;
	bool runWindowTests = true;
	for (int index = 1; index < argc; ++index)
	{
		const std::string_view argument(argv[index]);
		benchmark |= argument == "--benchmark";
		// 受限 CI 可只执行完全不创建 HWND 的测试集。
		runWindowTests &= argument != "--no-window";
	}

	TestCurvesAndTimelines();
	TestTargetsAndAdvancement();
	TestKeyframeTimelineTransactions();
	TestConcurrentAnimationPublication();
	failureCount += RunWakeSignalTests();
	failureCount += RunPresentDecisionTests();
	failureCount += RunSurfaceTests();
	failureCount += RunMessageTests();
	if (runWindowTests) failureCount += RunWindowTests();
	failureCount += RunDirtyRegionTests();
	failureCount += RunWindowGeometryTests();
	failureCount += RunFramePacingTests(benchmark);
	failureCount += RunToggleClickCoalescerTests();
	failureCount += RunRenderSchedulerTests();
	failureCount += RunPptUiTests();
	if (benchmark) RunBenchmarks();

	if (failureCount != 0)
	{
		std::cerr << "FAILED count=" << failureCount << '\n';
		return 1;
	}
	std::cout << "PASS animation correctness" << '\n';
	return 0;
}
