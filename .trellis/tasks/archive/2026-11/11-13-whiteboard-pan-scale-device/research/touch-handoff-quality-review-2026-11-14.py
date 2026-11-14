"""Focused read-only Touch handoff evidence; offline arithmetic, no native run."""
import hashlib
import json
import math
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
LOGS = ROOT / "Build/Logs/pan-touch-feedback-2026-11-14"
FOCUS = (4, 25, 26)
FREQUENCY = 10_000_000
PAIR = r"\(([-\deE+.]+),([-\deE+.]+)\)"


def pair(text, name):
    return tuple(map(float, re.search(re.escape(name) + "=" + PAIR, text).groups()))


def field(text, name, cast=float):
    return cast(re.search(r"(?:^|\s)" + re.escape(name) + r"=([^ ]+)", text)[1])


def main():
    snapshot = LOGS / "console.snapshot.log"
    digest = hashlib.sha256(snapshot.read_bytes()).hexdigest()
    assert digest == json.loads((LOGS / "snapshot.json").read_text())["sha256"]
    lines = snapshot.read_text(encoding="utf-8", errors="replace").splitlines()
    overview = json.loads((LOGS / "overview.json").read_text())
    input_pattern = re.compile(r"input ordinal=(\d+) epoch=(\d+) key=(\d+) class=(\d+) context=(\d+).*?phase=(\d+) pos-px=" + PAIR + r" sample-qpc=(\d+) frame-qpc=(\d+)")
    inputs, contexts, examples = {}, {}, []
    for index, text in enumerate(lines, 1):
        if "pan-context key=" in text:
            key = field(text, "key", int)
            if key in FOCUS:
                contexts[key] = {"line": index, "dipPerPixel": pair(text, "dip-per-pixel"), "motionPerDip": pair(text, "motion-per-dip")}
        match = input_pattern.search(text)
        if match and int(match[3]) in FOCUS:
            ordinal, epoch, key, kind, context, phase, x, y, qpc, frame = match.groups()
            inputs.setdefault(int(key), []).append({"line": index, "ordinal": int(ordinal), "epoch": int(epoch),
                "source": (int(kind) << 32) | int(context), "phase": int(phase), "px": (float(x), float(y)), "qpc": int(qpc), "frameQpc": int(frame)})

    for key in FOCUS:
        release = next(r for r in overview["releases"] if r.get("quality") and field(r["quality"]["text"], "key", int) == key)
        quality, profile, text = release["quality"]["text"], release["profile"]["text"], release["release"]
        source = field(quality, "source", int)
        assert source >> 32 == 1 and field(quality, "profile", int) == 1
        points = [point for point in inputs[key] if point["source"] == source]
        assert points[0]["phase"] == 0 and points[-1]["phase"] == 2
        assert all(b["ordinal"] == a["ordinal"] + 1 for a, b in zip(points, points[1:]))
        up = points[-1]
        motion_qpc = field(quality, "motion-last-qpc", int)
        motion = next(point for point in points if point["qpc"] == motion_qpc)
        previous = next(point for point in reversed(points) if point["qpc"] < motion_qpc and point["px"] != motion["px"])
        context = contexts[key]
        convert = lambda vector: tuple(value * factor for value, factor in zip(vector, context["motionPerDip"]))
        raw = math.hypot(*convert(pair(text, "raw")))
        selected_vector = pair(profile, "selected-motion")
        selected = math.hypot(*selected_vector)
        boosted = math.hypot(*convert(pair(text, "boosted")))
        age = (up["qpc"] - motion_qpc) / FREQUENCY
        gate = max(0, min(1, (0.07 - age) / 0.04))
        upper = field(quality, "support-upper")
        raw_vector = convert(pair(text, "raw"))
        u = tuple(v / raw for v in raw_vector)
        tail_dip = field(text, "tail-limit")
        tail_motion = tail_dip / math.hypot(*(v / scale for v, scale in zip(u, context["motionPerDip"]))) if tail_dip else None
        predicted = min(raw, upper, tail_motion if tail_motion is not None else raw, 2400) * gate
        assert abs(predicted - selected) < 0.03

        moving_steps = []
        for index in range(points[0]["line"] - 1, release["line"] - 1):
            row = lines[index]
            if "pan-step phase=2 held=1 " in row and any(pair(row, "delta-px")):
                moving_steps.append({"line": index + 1, "deltaPx": pair(row, "delta-px"),
                    "estimatedSpeedMmPerSecond": math.hypot(*convert(pair(row, "v-content-dip")))})
        last_held = moving_steps[-1]
        segment_delta = tuple((b - a) * dip * scale for a, b, dip, scale in zip(previous["px"], motion["px"], context["dipPerPixel"], context["motionPerDip"]))
        segment_speed = math.hypot(*segment_delta) / ((motion_qpc - previous["qpc"]) / FREQUENCY)

        next_down_index = next((i for i in range(release["line"], len(lines)) if "input " in lines[i] and " phase=0 " in lines[i]), len(lines))
        coast, pending = [], None
        for index in range(release["line"], next_down_index):
            row = lines[index]
            if "pan-step phase=" in row:
                phase = field(row, "phase", int)
                item = {"line": index + 1, "phase": phase, "deltaPx": pair(row, "delta-px"),
                    "speedMmPerSecond": math.hypot(*convert(pair(row, "v-content-dip")))}
                coast.append(item); pending = item
            elif " render frame=" in row and pending is not None:
                pending["frameQpc"] = field(row, "frame-qpc", int)
                pending["submitQpc"] = field(row, "submit-qpc", int)
                pending = None
        # Up's zero-delta step may precede the release record; only subsequent
        # free-motion/terminal deltas are summed. The first later Idle ends it.
        terminal = next((i for i, item in enumerate(coast) if item["phase"] == 0), None)
        if terminal is not None:
            coast = coast[:terminal + 1]
        total = tuple(sum(item["deltaPx"][axis] for item in coast) * context["dipPerPixel"][axis] * context["motionPerDip"][axis] for axis in (0, 1))
        elapsed = (coast[-1]["frameQpc"] - up["frameQpc"]) / FREQUENCY if coast and "frameQpc" in coast[-1] else None
        tau, deceleration = field(profile, "tau"), field(profile, "c")
        assert tau == 0.3 and deceleration == 200.0
        duration = tau * math.log1p(boosted / (deceleration * tau))
        def distance_at(time):
            time = min(time, duration)
            return (boosted + deceleration * tau) * tau * -math.expm1(-time / tau) - deceleration * tau * time
        if elapsed is not None:
            assert abs(distance_at(elapsed) - math.hypot(*total)) < 0.2
        examples.append({"key": key, "source": source, "context": context,
            "releaseLine": release["line"], "qualityLine": release["quality"]["line"],
            "rawMmPerSecond": raw, "supportUpperMmPerSecond": upper,
            "tailUpperMmPerSecondApprox": tail_motion, "motionAgeMs": age * 1000, "ageGate": gate,
            "predictedSelectedMmPerSecond": predicted, "selectedMmPerSecond": selected,
            "boostedMmPerSecond": boosted,
            "selectedToRawRatio": selected / raw,
            "lastMovingHeld": last_held, "selectedToLastMovingHeldEstimate": selected / last_held["estimatedSpeedMmPerSecond"],
            "boostedToLastMovingHeldEstimate": boosted / last_held["estimatedSpeedMmPerSecond"],
            "lastCallbackSegmentSpeedMmPerSecond": segment_speed,
            "motionEvent": motion, "previousMotionEvent": previous, "upEvent": up,
            "postReleaseCoastSteps": coast, "sumCoastDeltaMm": math.hypot(*total),
            "elapsedToLastVisibleCoastFrameMs": elapsed * 1000 if elapsed is not None else None,
            "predictedDistanceAtVisibleElapsedMm": distance_at(elapsed) if elapsed is not None else None,
            "visibleTerminalIdle": terminal is not None,
            "nextDownLine": next_down_index + 1 if next_down_index < len(lines) else None,
            "loggedTheoreticalDistanceMm": field(profile, "target-distance"),
            "loggedTheoreticalDurationMs": field(profile, "duration-s") * 1000,
        })

    zero_examples = []
    for key in (46, 48, 64, 68, 174):
        record = next(r for r in overview["releases"] if r.get("quality") and field(r["quality"]["text"], "key", int) == key)
        source = field(record["quality"]["text"], "source", int)
        group_keys = (47, 48) if key == 48 else (key,)
        group_inputs, registrations = [], []
        for index, row in enumerate(lines, 1):
            match = input_pattern.search(row)
            if match and int(match[3]) in group_keys:
                ordinal, epoch, item_key, kind, context, phase, x, y, qpc, frame = match.groups()
                if (int(kind) << 32) | int(context) == source:
                    group_inputs.append({"line": index, "key": int(item_key), "ordinal": int(ordinal),
                        "phase": int(phase), "qpc": int(qpc), "px": (float(x), float(y))})
            if "pan-register key=" in row and field(row, "key", int) in group_keys:
                payload = row.split("pan-register ", 1)[1]
                registrations.append({"line": index, "key": field(payload, "key", int), "downQpc": field(payload, "qpc", int)})
        assert registrations
        for item in registrations:
            down = next((point for point in group_inputs if point["key"] == item["key"] and point["phase"] == 0), None)
            if down:
                assert down["qpc"] == item["downQpc"]
        start_line = min(item["line"] for item in registrations)
        held_steps = []
        for index in range(start_line - 1, record["line"] - 1):
            row = lines[index]
            if "pan-step phase=2 " in row:
                held_steps.append({"line": index + 1, "held": field(row, "held", int), "deltaPx": pair(row, "delta-px")})
        delta = tuple(sum(item["deltaPx"][axis] for item in held_steps) for axis in (0, 1))
        terminal = next(item for item in reversed(group_inputs) if item["key"] == key and item["phase"] == 2)
        group_first_qpc = min(item["downQpc"] for item in registrations)
        zero_examples.append({"finalKey": key, "groupKeys": group_keys, "source": source,
            "releaseLine": record["line"], "reason": record["reason"], "registrations": registrations,
            "groupDurationMs": (terminal["qpc"] - group_first_qpc) / FREQUENCY * 1000,
            "visibleInputRecords": group_inputs, "maxHeld": max(item["held"] for item in held_steps),
            "heldSteps": held_steps, "sumHeldDeltaPx": delta,
            "sumHeldDeltaMmApprox": math.hypot(delta[0] * .5 * .19444445, delta[1] * .5 * .19791666),
            "rawCount": field(record["quality"]["text"], "raw-points", int),
            "motionCount": field(record["quality"]["text"], "motion-points", int),
            "rawSpanMs": field(record["release"], "span-ms"),
            "dataCaveat": "key174 Down input log absent; actual registration/admission and quality raw-last QPC agree on Down. Positive-intervals zero can mean early return, not absence of motion." if key == 174 else "Input records correlated by actual source/key. This does not establish physical gesture intent."})

    output = Path(__file__).with_suffix(".json")
    output.write_text(json.dumps({"evidenceType": "read-only focused log correlation and offline arithmetic; not native execution or physical hand-speed measurement",
        "snapshotSha256": digest, "qpcFrequency": FREQUENCY, "coordinatePrecision": "rounded log px and velocity fields",
        "scriptSha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), "examples": examples,
        "zeroExamples": zero_examples}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(output), "examples": len(examples), "zeroExamples": len(zero_examples), "checks": "PASS"}))


if __name__ == "__main__":
    main()
