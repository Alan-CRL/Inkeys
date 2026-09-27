// 研究程序：只读生产呈现决策 header；不创建窗口、图形设备或产品测试入口。
// 时钟模型逐句对应 Bar.FramePacing.cppm:100-107；这里没有导入生产 module。
#include "Inkeys/Inkeys/UI/Bar/Bar.PresentDecision.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace Inkeys::UI::Bar;

struct ClockModel {
    double reckon = 0.0;
    double Tick(double now) {
        const double elapsed = now - reckon;
        reckon = now;
        if (!std::isfinite(elapsed) || elapsed < 0.0) return 0.0;
        return std::clamp(elapsed, 0.0, 0.05);
    }
    void Rebase(double now) { reckon = now; }
};

struct Simulation {
    std::vector<std::uint64_t> attemptedFrames;
    std::uint64_t skipped = 0;
    std::uint64_t lastDelay = 0;
    unsigned lastFailureCount = 0;
    double animationSeconds = 0.0;
};

Simulation Run(std::uint64_t frames, bool notifyEachFrame) {
    BarPresentDecision decision;
    decision.AddDemand({true, true, false});
    ClockModel clock;
    Simulation result;
    std::uint64_t demand = 10;
    constexpr auto failure = BarPresentAttemptResult::Acquired(
        S_OK, FALSE, S_OK, S_OK, RECT{0, 0, 100, 100});
    for (std::uint64_t serial = 1; serial <= frames; ++serial) {
        if (notifyEachFrame) ++demand;
        // 与 WakeAndSnapshot -> Retry 门 -> AdvanceAnimations -> CompleteAttempt 顺序相同。
        decision.ObserveDemandGeneration(demand);
        const double dt = clock.Tick(static_cast<double>(serial) / 60.0);
        if (decision.HasFailureBackoff() && !decision.CanAttemptPresent(serial)) {
            ++result.skipped;
            continue;
        }
        result.animationSeconds += dt;
        result.attemptedFrames.push_back(serial);
        (void)decision.CompleteAttempt(failure, 7, demand, serial);
        result.lastDelay = decision.RetryDelayFrames();
        result.lastFailureCount = decision.ConsecutiveFailureCount();
    }
    return result;
}

bool Near(double a, double b) { return std::abs(a - b) < 1e-9; }

int main() {
    std::cout << std::fixed << std::setprecision(6);
    const auto quiet = Run(124, false);
    const std::vector<std::uint64_t> expected{1, 2, 4, 8, 16, 32, 64, 124};
    assert(quiet.attemptedFrames == expected);
    assert(quiet.skipped == 116);
    assert(Near(quiet.animationSeconds, 8.0 / 60.0));
    assert(quiet.lastDelay == 60 && quiet.lastFailureCount == 7);
    std::cout << "persistent_failure wall_s=" << 124.0 / 60.0
              << " advanced_s=" << quiet.animationSeconds
              << " attempts=" << quiet.attemptedFrames.size()
              << " skipped=" << quiet.skipped << " serials=";
    for (const auto value : quiet.attemptedFrames) std::cout << value << ',';
    std::cout << '\n';

    const auto longFailure = Run(724, false);
    assert(longFailure.attemptedFrames.size() == 18);
    assert(Near(longFailure.animationSeconds, 0.3));
    std::cout << "advance_300ms_while_failing wall_s=" << 724.0 / 60.0
              << " advanced_s=" << longFailure.animationSeconds << '\n';

    const auto notifying = Run(124, true);
    assert(notifying.attemptedFrames.size() == 124 && notifying.skipped == 0);
    assert(notifying.lastDelay == 1 && notifying.lastFailureCount == 1);
    assert(Near(notifying.animationSeconds, 124.0 / 60.0));
    std::cout << "notified_failure attempts=" << notifying.attemptedFrames.size()
              << " last_delay=" << notifying.lastDelay
              << " failure_count=" << notifying.lastFailureCount
              << " advanced_s=" << notifying.animationSeconds << '\n';

    ClockModel idleBefore;
    idleBefore.Rebase(0.016); // 当前生产调用点：返回 Idle 前。
    const double wrongWake = idleBefore.Tick(2.018);
    ClockModel idleAfter;
    idleAfter.Rebase(2.016); // 仅作合同对照：完成 idle 后。
    const double correctWake = idleAfter.Tick(2.018);
    assert(Near(wrongWake, 0.05) && Near(correctWake, 0.002));
    std::cout << "idle_wake current_order_dt_s=" << wrongWake
              << " after_wake_rebase_dt_s=" << correctWake << '\n';

    // 已恢复设备的通知没有修改 Bar demand：旧退避门会先挡住新 epoch 的观察。
    BarPresentDecision delayedEpoch;
    for (std::uint64_t serial = 1; serial <= 7; ++serial)
        delayedEpoch.RecordFailure(BarPresentFailureClass::UpdateLayeredWindow, 7, 10, serial);
    assert(delayedEpoch.NextRetryFrame() == 67);
    delayedEpoch.ObserveDemandGeneration(10);
    assert(!delayedEpoch.CanAttemptPresent(8));
    delayedEpoch.ObserveDeviceGeneration(8);
    assert(delayedEpoch.CanAttemptPresent(8));
    std::cout << "new_epoch_gate old_gate_blocks=1 early_epoch_observation_unblocks=1\n";

    for (const double interval : {0.0166666666666667, 0.05, 0.1, 0.2}) {
        ClockModel longFrame;
        const auto used = longFrame.Tick(interval);
        std::cout << "long_frame raw_s=" << interval << " used_s=" << used
                  << " speed_ratio=" << used / interval << '\n';
    }
    std::cout << "PASS: production failure-decision assertions; documented clock model assertions\n";
}
