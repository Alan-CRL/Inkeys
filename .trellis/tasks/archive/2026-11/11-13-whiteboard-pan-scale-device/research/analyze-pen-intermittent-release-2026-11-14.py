"""Read-only numeric reconstruction of current ScreenPen arrival guard (no product run)."""
import collections
import hashlib
import json
import math
from pathlib import Path
import re
import struct


ATTACHMENT = Path(r"C:/Users/alan-/.codex/attachments/c5d97310-dfa6-440a-a4d7-dca0cb31e3c8/已粘贴的文本.txt")
OUTPUT = Path(__file__).with_name("pen-intermittent-release-investigation-2026-11-14.json")
FREQUENCY = 10_000_000  # Inferred from recorded consumeQpc/sampleQpc/delay-ms, not hardware time.
MOTION_PER_PIXEL = (.5 * .19444445, .5 * .19791666)
EPSILON = 2 ** -23


def f32(x):
    return struct.unpack("f", struct.pack("f", x))[0]


def evidence(points, minimum_interval=.002):
    selected = []
    for point in reversed(points):
        if not selected or (selected[-1]["qpc"] - point["qpc"]) / FREQUENCY + 1e-9 >= minimum_interval:
            selected.append(point)
    return list(reversed(selected))


def fit(points):
    if len(points) < 2:
        return None
    times = [(point["qpc"] - points[-1]["qpc"]) / FREQUENCY for point in points]
    mean_t = sum(times) / len(times)
    mean_x = sum(point["xm"] for point in points) / len(times)
    mean_y = sum(point["ym"] for point in points) / len(times)
    denominator = sum((t - mean_t) ** 2 for t in times)
    if not denominator:
        return None
    vx = sum((t - mean_t) * (point["xm"] - mean_x) for t, point in zip(times, points)) / denominator
    vy = sum((t - mean_t) * (point["ym"] - mean_y) for t, point in zip(times, points)) / denominator
    return dict(vx_mm_s=vx, vy_mm_s=vy, speed_mm_s=math.hypot(vx, vy))


def replay(points, validation_interval=.002, suppress_compression_reset=False):
    history, direction, reverse_anchor, timing_rejected = [], (0, 0), None, False
    resets, reversals, guard_checks = [], [], []
    for point in points:
        if point["phase"] >= 2:
            continue
        if history:
            previous = history[-1]
            dt = (point["qpc"] - previous["qpc"]) / FREQUENCY
            if dt <= 0:
                continue
            dx, dy = point["xm"] - previous["xm"], point["ym"] - previous["ym"]
            distance = math.hypot(dx, dy)
            error = EPSILON * (math.hypot(point["xm"], point["ym"]) + math.hypot(previous["xm"], previous["ym"]))
            compressed = dt < .002 and distance > 1800 * (dt + 1 / FREQUENCY) + error
            if compressed:
                timing_rejected = True
                compressed = False
                anchor = next((old for old in reversed(history) if (point["qpc"] - old["qpc"]) / FREQUENCY + 1e-9 >= validation_interval), None)
                check = dict(current_line=point["line"], previous_line=previous["line"], adjacent_ms=dt * 1000, adjacent_distance_mm=distance)
                if anchor:
                    span = (point["qpc"] - anchor["qpc"]) / FREQUENCY
                    cross_distance = math.hypot(point["xm"] - anchor["xm"], point["ym"] - anchor["ym"])
                    cross_error = EPSILON * (math.hypot(point["xm"], point["ym"]) + math.hypot(anchor["xm"], anchor["ym"]))
                    compressed = cross_distance > 1800 * (span + 1 / FREQUENCY) + cross_error
                    check.update(anchor_line=anchor["line"], span_ms=span * 1000, distance_mm=cross_distance, average_mm_s=cross_distance / span)
                check["destructive_reset"] = compressed
                guard_checks.append(check)
            if compressed and not suppress_compression_reset:
                resets.append(guard_checks[-1])
                history, direction, reverse_anchor, timing_rejected = [], (0, 0), None, True
            elif dt >= .040:
                history, direction, reverse_anchor, timing_rejected = [], (0, 0), None, False
            elif distance > 0:
                along = dx * direction[0] + dy * direction[1]
                if direction != (0, 0) and along < 0:
                    reverse_anchor = reverse_anchor or previous
                    rx, ry = point["xm"] - reverse_anchor["xm"], point["ym"] - reverse_anchor["ym"]
                    if rx * direction[0] + ry * direction[1] <= -.15:
                        history = [reverse_anchor]
                        length = math.hypot(rx, ry)
                        direction, reverse_anchor, timing_rejected = (f32(rx / length), f32(ry / length)), None, False
                        reversals.append(point["line"])
                else:
                    reverse_anchor = None
                    if distance >= .15 or direction == (0, 0):
                        direction = (f32(dx / distance), f32(dy / distance))
        while len(history) > 1 and (point["qpc"] - history[1]["qpc"]) / FREQUENCY > .050:
            history.pop(0)
        if len(history) == 64:
            history.pop(0)
        history.append(point)
        chosen = evidence(history)
        span = (history[-1]["qpc"] - history[0]["qpc"]) / FREQUENCY
        if len(history) >= 3 and span + 1e-9 >= .020 and timing_rejected:
            if len(chosen) >= 3 and (chosen[-1]["qpc"] - chosen[0]["qpc"]) / FREQUENCY + 1e-9 >= .020:
                timing_rejected = False
    chosen = evidence(history)
    result = dict(raw_count=len(history), raw_span_ms=(history[-1]["qpc"] - history[0]["qpc"]) / 10000 if history else 0,
                  independent_count=len(chosen), independent_span_ms=(chosen[-1]["qpc"] - chosen[0]["qpc"]) / 10000 if chosen else 0,
                  evidence_lines=[point["line"] for point in chosen], final_direction=direction,
                  timing_rejected=timing_rejected, resets=resets, reversals=reversals, guard_checks=guard_checks,
                  numeric_fit=fit(chosen))
    if result["numeric_fit"]:
        vector = result["numeric_fit"]
        result["fit_dot_final_direction"] = vector["vx_mm_s"] * direction[0] + vector["vy_mm_s"] * direction[1]
    return result


def main():
    lines = ATTACHMENT.read_text(encoding="utf-8-sig").splitlines()
    grouped, pen_cursor = collections.defaultdict(list), []
    nav_pattern = re.compile(r"input ordinal=(\d+).*?key=(\d+) class=(\d+).*?phase=(\d+) pos-px=\(([-\d.]+),([-\d.]+)\) sample-qpc=(\d+)")
    for line, text in enumerate(lines, 1):
        match = nav_pattern.search(text)
        if match and int(match[3]) in (3, 4):
            x, y = float(match[5]), float(match[6])
            grouped[match[2]].append(dict(line=line, ordinal=int(match[1]), source_class=int(match[3]), phase=int(match[4]),
                                         qpc=int(match[7]), x_px=x, y_px=y, xm=x * MOTION_PER_PIXEL[0], ym=y * MOTION_PER_PIXEL[1]))
        match = re.search(r"pen-sample contact=(\d+) inverted=(\d+) x=([-\d.]+) y=([-\d.]+) qpc=(\d+)", text)
        if match and match[1] == "1":
            pen_cursor.append(dict(line=line, qpc=int(match[5]), x_px=float(match[3]), y_px=float(match[4])))
    result = dict(attachment=str(ATTACHMENT), bytes=ATTACHMENT.stat().st_size, sha256=hashlib.sha256(ATTACHMENT.read_bytes()).hexdigest(),
                  lines=len(lines), frequency_assumption=FREQUENCY, timing="callback-arrival, not hardware clock", physical_motion_per_pixel=MOTION_PER_PIXEL,
                  current_code_sha256=hashlib.sha256(Path("Inkeys/Inkeys/Drawing/Draw3/Draw3.CanvasNavigation.cpp").read_bytes()).hexdigest(), gestures={})
    for key, points in grouped.items():
        gaps = [dict(after_line=left["line"], before_line=right["line"], missing_ordinals=list(range(left["ordinal"] + 1, right["ordinal"])))
                for left, right in zip(points, points[1:]) if right["ordinal"] - left["ordinal"] != 1]
        present_qpc = {point["qpc"] for point in points}
        supplements = [point for point in pen_cursor if points[0]["qpc"] < point["qpc"] < points[-1]["qpc"] and point["qpc"] not in present_qpc]
        rounded_reconstruction = points.copy()
        for point in supplements:
            rounded_reconstruction.append(dict(point, source_class=3, phase=1,
                                                xm=point["x_px"] * MOTION_PER_PIXEL[0], ym=point["y_px"] * MOTION_PER_PIXEL[1],
                                                provenance="cursor log 0.1px precision, missing navigation input inferred"))
        rounded_reconstruction.sort(key=lambda point: point["qpc"])
        result["gestures"][key] = dict(events=points, has_down=points[0]["phase"] == 0, has_terminal=points[-1]["phase"] >= 2, gaps=gaps,
                                       missing_input_cursor_evidence=supplements,
                                       current_replay=replay(points),
                                       rounded_cursor_reconstruction=replay(rounded_reconstruction),
                                       diagnostic_without_compression_clear=replay(points, suppress_compression_reset=True),
                                       hypothetical_4ms_validation=replay(points, validation_interval=.004),
                                       hypothetical_8ms_validation=replay(points, validation_interval=.008))
    OUTPUT.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\r\n")
    for key, gesture in result["gestures"].items():
        print(key, "input", len(gesture["events"]), "gaps", gesture["gaps"])
        for scenario in ("current_replay", "diagnostic_without_compression_clear", "hypothetical_4ms_validation", "hypothetical_8ms_validation"):
            out = gesture[scenario]
            print(scenario, {name: out[name] for name in ("raw_count", "raw_span_ms", "independent_count", "independent_span_ms", "timing_rejected", "resets", "reversals", "numeric_fit")})


if __name__ == "__main__":
    main()
