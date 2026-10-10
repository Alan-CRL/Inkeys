module;

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "../../../IdtAtomic.h"

#include <algorithm>
#include <cmath>

module Inkeys.UI.Bar.Animation;

using namespace std;

IdtAtomic<double> BarUiDefaultDes = 600.0; // 全局默认速度 px/s
IdtAtomic<double> BarUiDefaultOperationDur = 0.4; // 默认操作过程时长 s
IdtAtomic<bool> BarUiAnimationEnabled = true;
IdtAtomic<double> BarUiAnimationSpeedRate = 1.00;

void BarUiAnimationTransactionClass::Lock() noexcept
{
	unsigned int spinCount = 0;
	while (locked.test_and_set(memory_order_acquire))
	{
		// 临界区只覆盖一次属性更新；持续竞争时让出时间片，避免渲染线程空转。
		if ((++spinCount & 63U) == 0) SwitchToThread();
		else YieldProcessor();
	}
}

void BarUiAnimationTransactionClass::Unlock() noexcept
{
	locked.clear(memory_order_release);
}

namespace
{
	double ApplyAnimationCurve(BarUiCurveEnum curve, double progress,
		double timelineStartProgress, bool continueTimelinePhase)
	{
		if (!continueTimelinePhase) return BarUiApplyCurve(curve, progress);
		double startProgress = clamp(timelineStartProgress, 0.0, 1.0);
		double absoluteProgress = startProgress
			+ (1.0 - startProgress) * clamp(progress, 0.0, 1.0);
		return BarUiApplyCurveRange(curve, startProgress, absoluteProgress);
	}

	void FinishValue(BarUiValueClass& value, double targetValue)
	{
		// 旧状态或外部调用即使带入非有限值，也只能在这里回落到有限坐标。
		double currentValue = value.val;
		double resolvedValue = isfinite(targetValue) ? targetValue
			: (isfinite(currentValue) ? currentValue : 0.0);
		value.tar = resolvedValue;
		value.val = resolvedValue;
		value.startV = resolvedValue;
		value.progress = 0.0;
		value.dur = 0.0;
		value.hasMiddleV = false;
		value.timelineStartProgress = 0.0;
		value.continueTimelinePhase = false;
	}

	void FinishColor(BarUiColorClass& color, COLORREF targetColor)
	{
		color.val = targetColor;
		color.startColor = targetColor;
		color.progress = 0.0;
		color.dur = 0.0;
		color.timelineStartProgress = 0.0;
		color.continueTimelinePhase = false;
	}

	void FinishPct(BarUiPctClass& pct, double targetPct)
	{
		targetPct = isfinite(targetPct) ? clamp(targetPct, 0.0, 1.0) : 0.0;
		pct.tar = targetPct;
		pct.val = targetPct;
		pct.startV = targetPct;
		pct.progress = 0.0;
		pct.dur = 0.0;
		pct.hasMiddleV = false;
		pct.timelineStartProgress = 0.0;
		pct.continueTimelinePhase = false;
	}
}

COLORREF MixBarUiColor(COLORREF startColor, COLORREF targetColor, double progress)
{
	// UI 颜色动画统一按 RGB 三通道共享同一条曲线进度。
	progress = clamp(progress, 0.0, 1.0);
	auto MixChannel = [progress](BYTE start, BYTE target)
		{
			double value = static_cast<double>(start)
				+ static_cast<double>(static_cast<int>(target)
					- static_cast<int>(start)) * progress;
			return static_cast<BYTE>(clamp(value, 0.0, 255.0) + 0.5);
		};
	return RGB(
		MixChannel(GetRValue(startColor), GetRValue(targetColor)),
		MixChannel(GetGValue(startColor), GetGValue(targetColor)),
		MixChannel(GetBValue(startColor), GetBValue(targetColor)));
}

BarUiAnimationAdvanceResultClass BarUiAdvanceAnimation(
	BarUiStateClass& state, const BarUiAnimationAdvanceContextClass&)
{
	bool target = state.tar;
	bool changed = static_cast<bool>(state.val) != target;
	state.val = target;
	return { changed, false };
}

BarUiAnimationAdvanceResultClass BarUiAdvanceAnimation(
	BarUiValueClass& value, const BarUiAnimationAdvanceContextClass& context)
{
	BarUiAnimationTransactionGuardClass guard(value.transaction);
	double previousValue = value.val;
	BarUiValueModeEnum mode = value.mod;
	BarUiCurveEnum curve = value.activeCurve;
	double targetValue = value.tar;
	double startValue = value.startV;
	double duration = value.dur;

	if (context.forceReplace || mode == BarUiValueModeEnum::Once
		|| !isfinite(targetValue) || !isfinite(startValue)
		|| (value.hasMiddleV && !isfinite(static_cast<double>(value.middleV)))
		|| !isfinite(duration) || duration <= 0.0
		|| !isfinite(context.speedRate) || context.speedRate <= 0.0
		|| context.dtSeconds <= 0.0)
	{
		FinishValue(value, targetValue);
		return { previousValue != static_cast<double>(value.val), false };
	}

	double progress = clamp(static_cast<double>(value.progress)
		+ context.dtSeconds * context.speedRate / duration, 0.0, 1.0);
	double nextValue = 0.0;
	if (value.hasMiddleV)
	{
		double middleValue = value.middleV;
		double phaseStart = value.continueTimelinePhase
			? clamp(static_cast<double>(value.timelineStartProgress), 0.0, 1.0) : 0.0;
		double absoluteProgress = phaseStart + (1.0 - phaseStart) * progress;
		if (absoluteProgress < 0.5)
		{
			double segmentStart = clamp(phaseStart * 2.0, 0.0, 1.0);
			double segmentProgress = clamp(absoluteProgress * 2.0, segmentStart, 1.0);
			double localProgress = BarUiApplyCurveRange(
				curve, segmentStart, segmentProgress);
			nextValue = startValue + (middleValue - startValue) * localProgress;
		}
		else
		{
			double localProgress = BarUiApplyCurve(
				value.activeMiddleCurve, (absoluteProgress - 0.5) * 2.0);
			nextValue = middleValue + (targetValue - middleValue) * localProgress;
		}
	}
	else
	{
		nextValue = startValue + (targetValue - startValue) * ApplyAnimationCurve(
			curve, progress, value.timelineStartProgress, value.continueTimelinePhase);
	}
	if (!isfinite(nextValue) || progress >= 1.0)
	{
		FinishValue(value, targetValue);
		return { previousValue != static_cast<double>(value.val), false };
	}

	value.val = nextValue;
	value.progress = progress;
	return { previousValue != nextValue, true };
}

BarUiAnimationAdvanceResultClass BarUiAdvanceAnimation(
	BarUiColorClass& color, const BarUiAnimationAdvanceContextClass& context)
{
	BarUiAnimationTransactionGuardClass guard(color.transaction);
	COLORREF previousColor = color.val;
	COLORREF targetColor = color.tar;
	COLORREF startColor = color.startColor;
	double duration = color.dur;
	double speedRate = !context.animationEnabled && color.animateWhenDisabled
		? 1.0 : context.speedRate;
	if (context.forceReplace || startColor == targetColor
		|| !isfinite(duration) || duration <= 0.0
		|| !isfinite(speedRate) || speedRate <= 0.0 || context.dtSeconds <= 0.0)
	{
		FinishColor(color, targetColor);
		return { previousColor != targetColor, false };
	}

	double progress = clamp(static_cast<double>(color.progress)
		+ context.dtSeconds * speedRate / duration, 0.0, 1.0);
	double curveProgress = ApplyAnimationCurve(color.activeCurve, progress,
		color.timelineStartProgress, color.continueTimelinePhase);
	COLORREF nextColor = MixBarUiColor(
		startColor, targetColor, clamp(curveProgress, 0.0, 1.0));
	if (progress >= 1.0)
	{
		FinishColor(color, targetColor);
		return { previousColor != targetColor, false };
	}

	color.val = nextColor;
	color.progress = progress;
	return { previousColor != nextColor, true };
}

BarUiAnimationAdvanceResultClass BarUiAdvanceAnimation(
	BarUiPctClass& pct, const BarUiAnimationAdvanceContextClass& context)
{
	BarUiAnimationTransactionGuardClass guard(pct.transaction);
	constexpr double pctEpsilon = 0.000001;
	double previousPct = pct.val;
	double targetPct = pct.tar;
	double startPct = pct.startV;
	double duration = pct.dur;
	double speedRate = !context.animationEnabled && pct.animateWhenDisabled
		? 1.0 : context.speedRate;
	if (context.forceReplace || !isfinite(targetPct) || !isfinite(startPct)
		|| (!pct.hasMiddleV && abs(targetPct - startPct) <= pctEpsilon)
		|| !isfinite(duration) || duration <= 0.0
		|| !isfinite(speedRate) || speedRate <= 0.0 || context.dtSeconds <= 0.0)
	{
		FinishPct(pct, targetPct);
		return { previousPct != static_cast<double>(pct.val), false };
	}

	double progress = clamp(static_cast<double>(pct.progress)
		+ context.dtSeconds * speedRate / duration, 0.0, 1.0);
	double nextPct = 0.0;
	if (pct.hasMiddleV)
	{
		double middlePct = pct.middleV;
		double phaseStart = pct.continueTimelinePhase
			? clamp(static_cast<double>(pct.timelineStartProgress), 0.0, 1.0) : 0.0;
		double absoluteProgress = phaseStart + (1.0 - phaseStart) * progress;
		if (absoluteProgress < 0.5)
		{
			double segmentStart = clamp(phaseStart * 2.0, 0.0, 1.0);
			double segmentProgress = clamp(absoluteProgress * 2.0, segmentStart, 1.0);
			double localProgress = BarUiApplyCurveRange(
				pct.activeCurve, segmentStart, segmentProgress);
			nextPct = startPct + (middlePct - startPct) * localProgress;
		}
		else
		{
			double localProgress = BarUiApplyCurve(
				pct.activeMiddleCurve, (absoluteProgress - 0.5) * 2.0);
			nextPct = middlePct + (targetPct - middlePct) * localProgress;
		}
	}
	else
	{
		nextPct = startPct + (targetPct - startPct) * ApplyAnimationCurve(
			pct.activeCurve, progress, pct.timelineStartProgress,
			pct.continueTimelinePhase);
	}
	nextPct = clamp(nextPct, 0.0, 1.0);
	if (!isfinite(nextPct) || progress >= 1.0)
	{
		FinishPct(pct, targetPct);
		return { previousPct != static_cast<double>(pct.val), false };
	}

	pct.val = nextPct;
	pct.progress = progress;
	return { previousPct != nextPct, true };
}
