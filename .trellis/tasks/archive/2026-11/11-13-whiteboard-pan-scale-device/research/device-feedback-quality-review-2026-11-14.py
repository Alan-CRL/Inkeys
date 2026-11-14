"""Read-only extraction of three fully correlated Touch quality examples.

Outputs research JSON only. Recomputed values use rounded log coordinates;
they are offline arithmetic, not native execution or hardware measurements.
"""

import hashlib
import json
import math
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[4]
LOG_DIR = ROOT / "Build/Logs/pan-device-feedback-2026-11-14"
FOCUS = {74, 90, 108}
FREQUENCY = 10_000_000
FLOAT_EPSILON = 2 ** -23
INPUT = re.compile(r"input .*?key=(\d+) class=(\d+) context=(\d+).*?phase=(\d+) pos-px=\(([-\d.]+),([-\d.]+)\) sample-qpc=(\d+)")
CONTEXT = re.compile(r"pan-context key=(\d+) profile=(\d+).*?dip-per-pixel=\(([^,]+),([^,]+)\) motion-per-dip=\(([^,]+),([^,]+)\)")
RELEASE = re.compile(r"\[CanvasPan\] seq=(\d+).*?release released=1 reason=(\S+) raw=\(([-\d.]+),([-\d.]+)\) selected=\(([-\d.]+),([-\d.]+)\)")
QUALITY = re.compile(r"\[CanvasPan\] seq=(\d+).*?release-quality key=(\d+) source=(\d+) profile=(\d+) unit=(\S+).*?last-qpc=(\d+) support-first-qpc=(\d+) support-last-qpc=(\d+) support-upper=([\deE+.-]+)")
BATCH = re.compile(r"rts-batch context=(\d+) cursor=(\d+) reported=(\d+) retained=(\d+) published=(\d+) sample-qpc=(\d+)")


def main():
    snapshot = LOG_DIR / "console.snapshot.log"
    metadata = json.loads((LOG_DIR / "snapshot.json").read_text(encoding="utf-8-sig"))
    digest = hashlib.sha256(snapshot.read_bytes()).hexdigest()
    assert digest == metadata["snapshotSha256"]
    events, contexts, batches = {}, {}, {}
    last_release, examples = None, []
    for line_no, text in enumerate(snapshot.open(encoding="utf-8", errors="replace"), 1):
        match = CONTEXT.search(text)
        if match and int(match[1]) in FOCUS:
            contexts[int(match[1])] = {
                "line": line_no, "profile": int(match[2]),
                "dipPerPixel": [float(match[3]), float(match[4])],
                "motionPerDip": [float(match[5]), float(match[6])],
            }
        match = BATCH.search(text)
        if match:
            batches[int(match[6])] = {
                "line": line_no, "context": int(match[1]), "cursor": int(match[2]),
                "reported": int(match[3]), "retained": int(match[4]), "published": int(match[5]),
            }
        match = INPUT.search(text)
        if match and int(match[1]) in FOCUS:
            key, source_class, context, phase, x, y, qpc = match.groups()
            events[(int(key), (int(source_class) << 32) | int(context), int(qpc))] = {
                "line": line_no, "qpc": int(qpc), "phase": int(phase),
                "positionPx": [float(x), float(y)],
            }
        match = RELEASE.search(text)
        if match:
            last_release = {
                "line": line_no, "seq": int(match[1]), "reason": match[2],
                "rawDipPerSecond": [float(match[3]), float(match[4])],
                "selectedDipPerSecond": [float(match[5]), float(match[6])],
            }
        match = QUALITY.search(text)
        if not match or int(match[2]) not in FOCUS:
            continue
        seq, key, source, profile, unit, c_qpc, a_qpc, b_qpc, logged_upper = match.groups()
        key, source = int(key), int(source)
        assert last_release and int(seq) == last_release["seq"] + 1
        context = contexts[key]
        assert context["profile"] == int(profile) == 1 and unit == "mm"
        points = [events[(key, source, int(qpc))] for qpc in (a_qpc, b_qpc, c_qpc)]
        raw = [v * m for v, m in zip(last_release["rawDipPerSecond"], context["motionPerDip"])]
        speed = math.hypot(*raw)
        unit_direction = [v / speed for v in raw]
        segments = []
        for first, last in zip(points, points[1:]):
            p, q = [
                [value * dip * motion for value, dip, motion in zip(point["positionPx"], context["dipPerPixel"], context["motionPerDip"])]
                for point in (first, last)
            ]
            delta = [y - x for x, y in zip(p, q)]
            projection = sum(d * u for d, u in zip(delta, unit_direction))
            dt = (last["qpc"] - first["qpc"]) / FREQUENCY
            error = FLOAT_EPSILON * (math.hypot(*p) + math.hypot(*q))
            upper = (max(0, projection) + 0.25 + error) / (dt - 1 / FREQUENCY)
            segments.append({
                "dtMs": dt * 1000, "distanceMm": math.hypot(*delta),
                "projectedMm": projection, "floatErrorMm": error,
                "recomputedUpperMmPerSecond": upper,
            })
        recomputed = min(segment["recomputedUpperMmPerSecond"] for segment in segments)
        assert abs(recomputed - float(logged_upper)) < 0.01
        tau, friction = 0.3, 200.0
        stop_time = tau * math.log1p(recomputed / (friction * tau))
        target = tau * recomputed - friction * tau * stop_time
        examples.append({
            "key": key, "source": source, "qualityLine": line_no, "context": context,
            "release": last_release, "rawSpeedMmPerSecond": speed,
            "A": points[0], "B": points[1], "C": points[2],
            "batchesABC": [batches.get(point["qpc"]) for point in points],
            "AB": segments[0], "BC": segments[1],
            "loggedSupportUpperMmPerSecond": float(logged_upper),
            "recomputedSupportUpperMmPerSecond": recomputed,
            "idealNoBoostTailMm": target, "idealDurationMs": stop_time * 1000,
        })
    assert {example["key"] for example in examples} == FOCUS
    output = Path(__file__).with_suffix(".json")
    output.write_text(json.dumps({
        "type": "read-only source-log correlation and offline arithmetic; not native or hardware execution",
        "snapshotSha256": digest, "frequencyAssumption": FREQUENCY,
        "coordinatePrecision": "log-rounded to three decimals in px",
        "scriptSha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "examples": examples,
    }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(output), "examples": len(examples), "checks": "PASS"}))


if __name__ == "__main__":
    main()
