#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>

import Inkeys.UI.Bar.Animation;

// 用原始 Animation module 执行 RenderLoop 的目标提交顺序；不创建产品窗口。
int main()
{
    constexpr double compact = 60.0 / 370.0;
    constexpr double dt = 1.0 / 60.0;
    BarUiValueClass panelWidth{370.0};
    BarUiValueClass repeatedThickness{1.0};
    BarUiValueClass singleTargetControl{1.0};
    BarUiTimelineClass timeline;
    timeline.Restart(0.4);
    double maximumNormalized = 0.0;
    double maximumControlError = 0.0;
    for (int frame = 1; frame <= 25; ++frame)
    {
        const double remaining = timeline.GetRemainingDuration();
        const double phase = timeline.GetProgress();
        const BarUiCurveSpecClass curve{BarUiCurveEnum::EaseInBack,
            BarUiCurveEnum::EaseInBack, phase, phase > 0.0};
        auto Sync = [&](BarUiValueClass& value)
        {
            if (value.IsSame() || value.progress != 0.0) return;
            value.dur = remaining;
            value.activeCurve = curve.first;
            value.activeMiddleCurve = curve.second;
            value.timelineStartProgress = curve.timelineStartProgress;
            value.continueTimelinePhase = curve.continueTimelinePhase;
        };
        panelWidth.SetTar(60.0);
        repeatedThickness.SetTar(1.0);
        repeatedThickness.SetTar(compact);
        singleTargetControl.SetTar(compact);
        Sync(panelWidth);
        Sync(repeatedThickness);
        Sync(singleTargetControl);
        const BarUiAnimationAdvanceContextClass context{dt, 1.0, true, false};
        if (!panelWidth.IsSame()) BarUiAdvanceAnimation(panelWidth, context);
        if (!repeatedThickness.IsSame()) BarUiAdvanceAnimation(repeatedThickness, context);
        if (!singleTargetControl.IsSame()) BarUiAdvanceAnimation(singleTargetControl, context);
        timeline.Advance(dt, 1.0);
        const double scale = panelWidth.val / 370.0;
        const double normalized = repeatedThickness.val / scale;
        const double controlError = std::abs(singleTargetControl.val / scale - 1.0);
        maximumNormalized = (std::max)(maximumNormalized, normalized);
        maximumControlError = (std::max)(maximumControlError, controlError);
        if (frame == 18 || frame == 21 || frame >= 23)
            std::printf("frame=%d scale=%.9f repeated_ft=%.9f normalized=%.9f control_ft=%.9f\n",
                frame, scale, static_cast<double>(repeatedThickness.val), normalized,
                static_cast<double>(singleTargetControl.val));
    }
    const bool settled = repeatedThickness.val == compact && panelWidth.val == 60.0;
    const bool reproduced = maximumNormalized > 4.0 && maximumControlError < 1e-12 && settled;
    std::printf("production_module: max_normalized=%.9f control_error=%.3g settled=%d reproduced=%d\n",
        maximumNormalized, maximumControlError, settled, reproduced);
    return reproduced ? 0 : 1;
}
