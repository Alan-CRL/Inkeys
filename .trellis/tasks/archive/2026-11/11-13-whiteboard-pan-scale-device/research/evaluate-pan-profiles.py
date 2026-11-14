"""离线检查规划参数；不是产品实现、输入重放或设备手感测试。"""

import json
import math
from pathlib import Path


def smoothstep(value):
    value = min(1.0, max(0.0, value))
    return value * value * (3.0 - 2.0 * value)


def friction(speed, tau, deceleration):
    if speed <= 0:
        return 0.0, 0.0
    duration = tau * math.log1p(speed / (deceleration * tau))
    distance = speed * tau - deceleration * tau * duration
    return distance, duration


def touch_physical_parameters(hand_speed, weight):
    action = weight * smoothstep((hand_speed - 600.0) / 900.0)
    return 0.30 + 0.12 * action, 200.0 * (1.0 - 0.25 * action)


def touch_fallback_parameters(hand_speed, large_screen, intent_gain):
    # gain 只改变快甩响应选参，不乘几何、实际初速或输出位移。
    action = 0.5 * smoothstep((hand_speed * intent_gain - 750.0) / 1500.0) if large_screen else 0.0
    return 0.30 + 0.12 * action, 1000.0 * (1.0 - 0.25 * action)


def row(name, unit, hand_speed, initial_speed, tau, deceleration, **extras):
    distance, duration = friction(initial_speed, tau, deceleration)
    return dict(profile=name, unit=unit, selectedHandSpeed=hand_speed,
                effectiveInitialSpeed=initial_speed, tauSeconds=tau,
                constantDeceleration=deceleration, distance=distance,
                durationSeconds=duration, **extras)


def evaluate():
    rows = []
    for speed in (30, 100, 300, 500, 600, 750, 1000, 1200, 1500, 1800, 2400):
        for weight in (0.0, 0.25, 0.5, 1.0):
            tau, deceleration = touch_physical_parameters(speed, weight)
            rows.append(row("TouchPhysical", "mm", speed, speed, tau, deceleration, weight=weight))
        pen_speed = min(speed, 1800.0)
        rows.append(row("ScreenPenPhysical", "mm", pen_speed, pen_speed, 0.20, 350.0,
                        inputSpeedBeforeCap=speed))
    for speed in (100, 500, 750, 1500, 2250, 3000, 6000, 12000):
        for large in (False, True):
            for gain in (0.25, 1.0, 1.5, 2.0):
                tau, deceleration = touch_fallback_parameters(speed, large, gain)
                rows.append(row("TouchFallback", "system-DIP", speed, speed, tau, deceleration,
                                largeScreen=large, intentGain=gain))

    checks = []
    # 理想解析模型：参数和端点随手速、尺寸权重连续单调，不代替实际像素输入验证。
    for weight in (i / 40.0 for i in range(41)):
        previous = (-1.0, -1.0)
        for speed in (i * 2.0 for i in range(1, 1201)):
            tau, deceleration = touch_physical_parameters(speed, weight)
            distance, duration = friction(speed, tau, deceleration)
            base_distance, base_duration = friction(speed, 0.30, 200.0)
            assert distance >= previous[0] - 1e-10 and duration >= previous[1] - 1e-10
            assert base_distance - 1e-9 <= distance <= 1.4 * base_distance + 1e-9
            assert base_duration - 1e-9 <= duration <= 1.4 * base_duration + 1e-9
            if speed <= 1800:
                pen_distance, pen_duration = friction(speed, 0.20, 350.0)
                assert pen_distance < distance and pen_duration < duration
            previous = distance, duration
    checks.append("Physical Touch: speed monotonic; equal-initial-speed distance/time <=1.4x base; Pen shorter")
    for speed in (100.0, 500.0, 750.0, 1000.0, 1500.0, 2400.0):
        previous = (-1.0, -1.0)
        for weight in (i / 1000.0 for i in range(1001)):
            tau, deceleration = touch_physical_parameters(speed, weight)
            value = friction(speed, tau, deceleration)
            assert value[0] >= previous[0] - 1e-10 and value[1] >= previous[1] - 1e-10
            previous = value
    checks.append("Physical Touch: weight monotonic at fixed hand/initial speed")
    for gain in (0.25, 0.5, 1.0, 1.5, 2.0):
        previous = (-1.0, -1.0)
        for speed in (i * 5.0 for i in range(1, 2401)):
            tau, deceleration = touch_fallback_parameters(speed, True, gain)
            distance, duration = friction(speed, tau, deceleration)
            base_distance, base_duration = friction(speed, 0.30, 1000.0)
            assert distance >= previous[0] - 1e-10 and duration >= previous[1] - 1e-10
            assert base_distance - 1e-9 <= distance <= 1.2 * base_distance + 1e-9
            assert base_duration - 1e-9 <= duration <= 1.2 * base_duration + 1e-9
            if speed <= 9000:
                pen_distance, pen_duration = friction(speed, 0.20, 1750.0)
                assert pen_distance < distance and pen_duration < duration
            previous = distance, duration
    checks.append("DIP Touch fallback: speed monotonic; equal-initial-speed distance/time <=1.2x base; Pen shorter")
    for hand in (100.0, 600.0, 900.0, 1500.0):
        for initial in (hand, 1800.0, 2400.0):
            if initial < hand:
                continue
            tau, deceleration = touch_physical_parameters(hand, 1.0)
            distance, duration = friction(initial, tau, deceleration)
            base_distance, base_duration = friction(initial, 0.30, 200.0)
            assert distance <= 1.4 * base_distance + 1e-9
            assert duration <= 1.4 * base_duration + 1e-9
    checks.append("Boosted initial speed: response chosen from pre-boost hand speed; same-v0 bounds retained")
    limits = [row("TouchPhysicalMaximum", "mm", 2400.0, 2400.0, 0.42, 150.0),
              row("ScreenPenPhysicalMaximum", "mm", 1800.0, 1800.0, 0.20, 350.0),
              row("ScreenPenFallbackMaximum", "system-DIP", 9000.0, 9000.0, 0.20, 1750.0),
              row("TouchFallbackMaximum", "system-DIP", 12000.0, 12000.0, 0.36, 875.0),
              row("IndirectDipMaximum", "system-DIP", 12000.0, 12000.0, 0.30, 1000.0)]
    for limit in limits:
        assert math.isfinite(limit["distance"]) and 0.0 < limit["durationSeconds"] < 1.6
    checks.append("All proposed per-flight cap endpoints finite; maximum theoretical duration <1.6s")
    return dict(status="PASS", evidence="independent algebra; no C++/GUI/hardware test",
                parameterVersion="proposal-v1", checks=checks, limits=limits, rows=rows)


if __name__ == "__main__":
    output = evaluate()
    path = Path(__file__).with_name("proposed-profile-analysis.json")
    path.write_bytes((json.dumps(output, ensure_ascii=False, indent=2) + "\n").replace("\n", "\r\n").encode("utf-8"))
    print(json.dumps(dict(status=output["status"], checks=output["checks"], limits=output["limits"]),
                     ensure_ascii=False, indent=2))
