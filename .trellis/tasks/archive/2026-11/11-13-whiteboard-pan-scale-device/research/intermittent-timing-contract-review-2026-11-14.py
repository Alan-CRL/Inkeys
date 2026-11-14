"""Offline specialization of the current tracker for positive straight lines.

This is Python reasoning evidence, not execution of the C++ module or a device.
Both report rates use the same 10MHz QPC, path and terminal age. The omitted
reversal/cancellation branches are unreachable for these positive monotonic
inputs; the current timing reset, history retention, evidence selection, fit,
recent-tail cap and output-cap branches are reproduced below.
"""

import hashlib
import json
import math
from pathlib import Path


FREQUENCY = 10_000_000
ORIGIN = 5_000_000_000
EPSILON_FLOAT = 2 ** -23
OUTPUT_CAP = 1800.0
DURATION_SECONDS = 0.100
UP_AGE_SECONDS = 0.001


def evidence(samples):
    result = []
    for point in reversed(samples):
        if not result or (result[-1][0] - point[0]) / FREQUENCY + 1e-9 >= 0.002:
            result.append(point)
    return list(reversed(result))


def replay(report_hz, speed_mm_s):
    history = []
    timing_rejected = False
    reset_count = 0
    number = round(report_hz * DURATION_SECONDS)
    for index in range(number + 1):
        point = (ORIGIN + round(index * FREQUENCY / report_hz), speed_mm_s * index / report_hz)
        if history:
            previous = history[-1]
            dt = (point[0] - previous[0]) / FREQUENCY
            distance = point[1] - previous[1]
            allowance = EPSILON_FLOAT * (abs(point[1]) + abs(previous[1]))
            compressed = dt < 0.002 and distance > OUTPUT_CAP * (dt + 1 / FREQUENCY) + allowance
            if compressed:
                timing_rejected = True
                compressed = False
                for anchor in reversed(history):
                    span = (point[0] - anchor[0]) / FREQUENCY
                    if span + 1e-9 < 0.002:
                        continue
                    allowance = EPSILON_FLOAT * (abs(point[1]) + abs(anchor[1]))
                    compressed = point[1] - anchor[1] > OUTPUT_CAP * (span + 1 / FREQUENCY) + allowance
                    break
            if compressed:
                history.clear()
                timing_rejected = True
                reset_count += 1
            elif dt >= 0.040:
                history.clear()
                timing_rejected = False
        while len(history) > 1 and (point[0] - history[1][0]) / FREQUENCY > 0.050:
            history.pop(0)
        if len(history) == 64:
            history.pop(0)
        history.append(point)
        if len(history) >= 3 and (history[-1][0] - history[0][0]) / FREQUENCY + 1e-9 >= 0.020:
            qualified = evidence(history)
            if len(qualified) >= 3 and (qualified[-1][0] - qualified[0][0]) / FREQUENCY + 1e-9 >= 0.020:
                timing_rejected = False

    qualified = evidence(history)
    reason = (
        "compressed-time" if timing_rejected else
        "insufficient-samples" if len(qualified) < 3 else
        "insufficient-span" if (qualified[-1][0] - qualified[0][0]) / FREQUENCY + 1e-9 < 0.020 else
        "ready"
    )
    raw = 0.0
    if reason == "ready":
        times = [(p[0] - qualified[-1][0]) / FREQUENCY for p in qualified]
        positions = [p[1] - qualified[-1][1] for p in qualified]
        mean_t, mean_x = sum(times) / len(times), sum(positions) / len(positions)
        raw = sum((t - mean_t) * (x - mean_x) for t, x in zip(times, positions)) / sum(
            (t - mean_t) ** 2 for t in times)
    tail_cap = OUTPUT_CAP
    if raw > 0:
        for anchor in reversed(history[:-1]):
            dt = (history[-1][0] - anchor[0]) / FREQUENCY
            distance = history[-1][1] - anchor[1]
            if dt + 1e-9 < 0.002 or distance < 0.15:
                continue
            tail_cap = distance / dt
            break
    return {
        "reportHz": report_hz,
        "inputSpeedMmPerSecond": speed_mm_s,
        "durationMs": DURATION_SECONDS * 1000,
        "upAgeMs": UP_AGE_SECONDS * 1000,
        "inputRecordsIncludingDown": number + 1,
        "cumulativeTimingResets": reset_count,
        "retainedRawCount": len(history),
        "independentAnchorCount": len(qualified),
        "retainedSpanMs": (history[-1][0] - history[0][0]) / FREQUENCY * 1000,
        "reason": reason,
        "rawEstimateMmPerSecond": raw,
        "selectedAfterTailAndOutputCapMmPerSecond": min(raw, OUTPUT_CAP, tail_cap) if raw > 0 else 0,
    }


def main():
    cases = [replay(hz, speed) for speed in (1700, 1800, 1801, 2500, 3500) for hz in (240, 960)]
    for case in cases:
        if case["inputSpeedMmPerSecond"] <= OUTPUT_CAP or case["reportHz"] == 240:
            assert case["reason"] == "ready" and case["cumulativeTimingResets"] == 0
            assert 0 < case["selectedAfterTailAndOutputCapMmPerSecond"] <= OUTPUT_CAP
        else:
            assert case["reason"] == "compressed-time" and case["cumulativeTimingResets"] == 48
            assert case["selectedAfterTailAndOutputCapMmPerSecond"] == 0
    path = Path(__file__)
    result = {
        "evidenceType": "offline Python specialization of current positive-straight-line tracker branches; not native execution",
        "scriptSha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "qpcFrequency": FREQUENCY,
        "qpcOrigin": ORIGIN,
        "outputVelocityCapMmPerSecond": OUTPUT_CAP,
        "sampling": "qpc=origin+round(index*10000000/reportHz), x_mm=speed*index/reportHz, y=0",
        "cases": cases,
    }
    output = path.with_suffix(".json")
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(output), "cases": len(cases), "assertions": "PASS"}, ensure_ascii=False))


if __name__ == "__main__":
    main()
