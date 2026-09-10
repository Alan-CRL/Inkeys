"""读取临时 Bar JSONL，导出成功帧轨迹；诊断量不自动等同于视觉故障。"""
from __future__ import annotations

import argparse
import bisect
import collections
import csv
import glob
import json
import math
from pathlib import Path

GRAB_VALID = 32
OS_WINDOW_VALID = 8
CAPTURE_ACTIVE = 256
MODE_NAMES = {0: "Floating", 1: "BottomDocked"}
PHASE_NAMES = {0: "Stable", 1: "Capturing", 2: "Dragging", 3: "Detaching", 4: "Recovering"}
GROUP_NAMES = ("serial", "state", "env", "input", "geometry", "spring", "timing", "window", "result", "presented_snapshot")


def finite(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def element(group, name, index):
    value = group.get(name)
    return value[index] if isinstance(value, list) and len(value) > index and finite(value[index]) else None


def subtract(left, right):
    return left - right if finite(left) and finite(right) else None


def milliseconds(ticks, frequency):
    return ticks * 1000.0 / frequency if finite(ticks) and finite(frequency) and frequency > 0 else None


def input_time(record):
    # pointer.snapshot_qpc 是实际取样时刻，qpc 是发布后入队时刻。
    return record.get("snapshot_qpc", 0) or record.get("qpc", 0)


def expand_paths(arguments):
    paths = set()
    for argument in arguments:
        path = Path(argument)
        matches = list(path.glob("bar-bottom-dock-trace-*.jsonl")) if path.is_dir() else [
            Path(match) for match in glob.glob(argument)
        ]
        if not matches:
            raise ValueError(f"找不到日志：{argument}")
        paths.update(match.resolve() for match in matches if match.is_file())
    return sorted(paths)


def load_traces(paths):
    runs = collections.defaultdict(list)
    frequencies, issues, seen = {}, [], {}
    for path in paths:
        with path.open(encoding="utf-8-sig") as stream:
            for line_number, line in enumerate(stream, 1):
                if not line.strip():
                    continue
                try:
                    record = json.loads(line)
                except json.JSONDecodeError as error:
                    issues.append(f"{path.name}:{line_number}: 非完整 JSON：{error.msg}")
                    continue
                if not isinstance(record, dict) or record.get("schema") != 1:
                    raise ValueError(f"{path.name}:{line_number}: 不支持的 trace schema")
                run = record.get("run_id")
                if run is None:
                    raise ValueError(f"{path.name}:{line_number}: 缺少 run_id")
                if "qpc_frequency" in record:
                    frequency = record["qpc_frequency"]
                    if not finite(frequency) or frequency <= 0:
                        raise ValueError(f"{path.name}:{line_number}: 无效 QPC frequency")
                    if run in frequencies and frequencies[run] != frequency:
                        raise ValueError(f"run {run}: QPC frequency 不一致")
                    frequencies[run] = frequency
                sequence = record.get("seq")
                if sequence is not None:
                    key = (run, sequence)
                    if key in seen:
                        if seen[key] != record:
                            raise ValueError(f"run {run}: seq {sequence} 对应两条不同记录")
                        continue
                    seen[key] = record
                runs[run].append(record)
    if not runs:
        raise ValueError("没有可解析的 trace 记录")
    for run in runs:
        runs[run].sort(key=lambda item: (item.get("qpc", 0), item.get("seq", 0)))
        sequences = [record["seq"] for record in runs[run] if "seq" in record]
        if sequences and min(sequences) > 1:
            issues.append(f"run {run}: 最早 seq={min(sequences)}，较早记录未提供或已按容量删除")
        ordered = sorted(sequences)
        gaps = sum(max(0, right - left - 1) for left, right in zip(ordered, ordered[1:]))
        if gaps:
            issues.append(f"run {run}: seq 中间缺少 {gaps} 项，请结合 writer_dropped 和分段文件检查")
        if run not in frequencies:
            issues.append(f"run {run}: 缺少元数据，时间列无法换算；请提供同次运行的全部分段文件")
    return runs, frequencies, issues


def merge_frame(target, record):
    for key, value in record.items():
        if key in GROUP_NAMES:
            target.setdefault(key, {}).update(value)
        else:
            target[key] = value


def frame_metrics(record, normalized_y, consumed_pointer, latest_pointer, frequency, first_qpc, previous_qpc):
    env, geometry, window = (record.get(name, {}) for name in ("env", "geometry", "window"))
    state, serial, spring = (record.get(name, {}) for name in ("state", "serial", "spring"))
    timing = record.get("timing", {})
    qpc, flags = record.get("qpc", 0), record.get("flags", 0)
    row = {
        "run_id": record["run_id"], "gesture_id": record.get("gesture_id", 0),
        "frame": record.get("frame", 0), "seq": record.get("seq"), "qpc": qpc,
        "flags": flags, "reason": record.get("reason"),
        "device_generation": record.get("device_generation"),
        "time_ms": milliseconds(qpc - first_qpc, frequency),
        "consumed_serial": serial.get("frame"), "tagged_serial": serial.get("tagged"),
        "current_serial": serial.get("current"), "deferred_serial": serial.get("deferred"),
        "presented_serial": serial.get("presented"),
        "mode": MODE_NAMES.get(state.get("mode"), state.get("mode")),
        "phase": PHASE_NAMES.get(state.get("phase"), state.get("phase")),
        "center_mode": state.get("center_mode"), "drag": state.get("drag"),
        "capture_active": bool(flags & CAPTURE_ACTIVE),
        "capture_seeded": bool(flags & 128), "grip_active": bool(flags & 512),
        "grip_recovery_seeded": bool(flags & 4096),
        "vertical_handoff_seeded": bool(flags & 8192),
        "consumed_elastic_y_dip": element(state, "elastic_dip", 0),
        "consumed_elastic_x_dip": element(state, "elastic_dip", 1),
        "animations_enabled": bool(flags & 64), "animation_speed": timing.get("animation_speed"),
        "grip_dip": element(spring, "grip", 0), "capture_dip": element(spring, "capture", 0),
        "raw_dt_ms": milliseconds(timing.get("raw_dt"), 1),
        "frame_dt_ms": milliseconds(timing.get("frame_dt"), 1),
        "integrated_dt_ms": milliseconds(timing.get("integrated_dt"), 1),
        "grip_dt_ms": milliseconds(timing.get("grip_dt"), 1),
        "capture_dt_ms": milliseconds(timing.get("capture_dt"), 1),
        "snapshot_to_commit_ms": milliseconds(
            qpc - record["snapshot_qpc"], frequency) if record.get("snapshot_qpc", 0) > 0 else None,
        "present_gap_ms": milliseconds(qpc - previous_qpc, frequency) if previous_qpc is not None else None,
    }
    ulw_qpc = element(record.get("result", {}), "stage_qpc", 1)
    row["ulw_qpc"] = ulw_qpc if finite(ulw_qpc) and ulw_qpc > 0 else None
    row["snapshot_to_ulw_ms"] = milliseconds(
        ulw_qpc - record["snapshot_qpc"], frequency
    ) if row["ulw_qpc"] and record.get("snapshot_qpc", 0) > 0 else None
    row["sample_to_snapshot_ms"] = milliseconds(
        record["snapshot_qpc"] - input_time(consumed_pointer), frequency
    ) if consumed_pointer is not None and record.get("snapshot_qpc", 0) > 0 else None
    quality = []
    zoom, scale = env.get("zoom"), geometry.get("scale_y")
    base_top, base_bottom = (element(geometry, "base_y", index) for index in (0, 1))
    visual_top, visual_bottom = (element(geometry, "visual_y", index) for index in (0, 1))
    root_y, actual_height = element(geometry, "root", 1), element(geometry, "main_size", 1)
    stroke, dock = geometry.get("stroke"), env.get("dock_line")
    destination, source, capacity = (element(window, name, 1) for name in ("destination", "source", "capacity_origin"))
    size = element(window, "size", 1)
    valid = all(finite(value) for value in (
        zoom, scale, base_top, base_bottom, visual_top, visual_bottom,
        root_y, actual_height, stroke, destination, source, capacity, size
    )) and zoom > 0 and scale > 0 and actual_height > 0 and size > 0
    metric_names = (
        "dock_line_px", "selected_translation_y_px", "frame_translation_y_px", "post_snapshot_translation_y_px",
        "candidate_grab_y_px", "grab_error_candidate_px", "grab_error_effective_px",
        "solver_normalized_y", "solver_raw_pointer_y_px", "solver_logical_grab_dip", "solver_desired_grab_dip",
        "solver_effective_grab_dip", "solver_constrained", "window_mapping_error_px", "os_destination_error_px",
        "base_bottom_error_px", "model_bottom_error_px", "main_bottom_error_px", "bar_bottom_error_px",
        "visible_bottom_error_px", "actual_grab_y_px", "pointer_consumed_y_px", "pointer_latest_y_px",
        "base_bottom_y_px", "model_bottom_y_px", "main_bottom_y_px", "bar_bottom_y_px", "visible_bottom_y_px",
        "grab_error_consumed_px", "grab_error_latest_px", "consumed_pointer_qpc",
        "latest_pointer_qpc", "proxy_grip_y_px", "actual_minus_proxy_grip_px",
    )
    row.update({name: None for name in metric_names})
    if valid:
        # 使用最终成功 ULW 的 source/destination；不把候选位移当成实际上屏位移。
        screen_offset = destination - source - capacity

        def screen_y(local_y):
            return screen_offset + zoom * (visual_top + (local_y - base_top) * scale)

        row["dock_line_px"] = dock if finite(dock) else None
        if not finite(dock):
            quality.append("unknown_render_dock_line")
        row["selected_translation_y_px"] = element(window, "actual", 1)
        row["frame_translation_y_px"] = element(window, "frame_translation", 1)
        row["post_snapshot_translation_y_px"] = subtract(
            row["selected_translation_y_px"], row["frame_translation_y_px"])
        origin_y = element(env, "origin", 1)
        if finite(origin_y) and finite(row["selected_translation_y_px"]):
            row["window_mapping_error_px"] = screen_offset - origin_y - row["selected_translation_y_px"]
        if flags & OS_WINDOW_VALID:
            row["os_destination_error_px"] = subtract(element(window, "os", 1), destination)
        row["base_bottom_y_px"] = screen_offset + zoom * base_bottom
        row["model_bottom_y_px"] = screen_offset + zoom * visual_bottom
        row["main_bottom_y_px"] = screen_y(root_y + (actual_height + stroke) / 2)
        for kind in ("base", "model", "main"):
            row[f"{kind}_bottom_error_px"] = subtract(row[f"{kind}_bottom_y_px"], dock)
        bar_y, bar_height = element(geometry, "bar_bounds", 1), element(geometry, "bar_bounds", 3)
        if all(finite(value) for value in (bar_y, bar_height, geometry.get("bar_stroke"))) and bar_height > 0:
            row["bar_bottom_y_px"] = screen_y(bar_y + bar_height + geometry["bar_stroke"] / 2)
            row["bar_bottom_error_px"] = subtract(row["bar_bottom_y_px"], dock)
        row["visible_bottom_y_px"] = max(
            value for value in (row["main_bottom_y_px"], row["bar_bottom_y_px"]) if finite(value))
        row["visible_bottom_error_px"] = subtract(row["visible_bottom_y_px"], dock)
        if finite(normalized_y):
            row["actual_grab_y_px"] = screen_y(root_y + (normalized_y - 0.5) * actual_height)
        else:
            quality.append("missing_successful_grab_basis")
        solver = geometry.get("grab_solver")
        if isinstance(solver, dict):
            for source_name, output_name in (("normalized_y", "solver_normalized_y"),
                                             ("raw_screen_y", "solver_raw_pointer_y_px"),
                                             ("logical_dip", "solver_logical_grab_dip"),
                                             ("desired_dip", "solver_desired_grab_dip"),
                                             ("effective_dip", "solver_effective_grab_dip")):
                value = solver.get(source_name)
                row[output_name] = value if finite(value) else None
            if isinstance(solver.get("constrained"), bool):
                row["solver_constrained"] = solver["constrained"]
            if finite(row["solver_effective_grab_dip"]):
                row["grab_error_effective_px"] = subtract(
                    row["actual_grab_y_px"], screen_offset + zoom * row["solver_effective_grab_dip"])
        row["proxy_grip_y_px"] = element(geometry, "raw_grip_screen", 1)
        if row["proxy_grip_y_px"] is None:
            row["proxy_grip_y_px"] = element(record.get("input", {}), "raw_grip", 1)
        row["actual_minus_proxy_grip_px"] = subtract(row["actual_grab_y_px"], row["proxy_grip_y_px"])
    else:
        quality.append("incomplete_committed_geometry_or_window")
    for label, pointer in (("consumed", consumed_pointer), ("latest", latest_pointer)):
        if pointer is not None:
            row[f"pointer_{label}_y_px"] = element(pointer.get("input", {}), "pointer", 1)
            row[f"{label}_pointer_qpc"] = input_time(pointer)
            row[f"grab_error_{label}_px"] = subtract(row["actual_grab_y_px"], row[f"pointer_{label}_y_px"])
        elif state.get("drag"):
            quality.append(f"missing_{label}_pointer")
    # 剔除快照后已成功直移的量，避免把新输入带来的窗口追赶误判为形变错误。
    row["candidate_grab_y_px"] = subtract(row["actual_grab_y_px"], row["post_snapshot_translation_y_px"])
    row["grab_error_candidate_px"] = subtract(row["candidate_grab_y_px"], row["pointer_consumed_y_px"])
    if record.get("dropped", 0):
        quality.append("trace_has_dropped_records")
    row["data_quality"] = ";".join(quality)
    return row


def absorption_pair(before, after):
    pair = {"before": before, "after": after}

    def endpoints(record):
        snapshot = record.get("presented_snapshot", {})
        origin, translation = element(snapshot, "origin", 1), element(snapshot, "direct_translation", 1)
        zoom = snapshot.get("zoom")
        top, bottom = (element(snapshot, "visual_y", index) for index in (0, 1))
        if all(finite(value) for value in (origin, translation, zoom, top, bottom)) and zoom > 0:
            return [origin + translation + zoom * top, origin + translation + zoom * bottom]
        return [None, None]

    first, last = endpoints(before), endpoints(after)
    os_shift = None
    if before.get("flags", 0) & OS_WINDOW_VALID and after.get("flags", 0) & OS_WINDOW_VALID:
        os_shift = subtract(element(after.get("window", {}), "os", 1), element(before.get("window", {}), "os", 1))
    pair["metrics"] = {
        "presented_top_before_px": first[0], "presented_bottom_before_px": first[1],
        "presented_top_after_px": last[0], "presented_bottom_after_px": last[1],
        "snapshot_top_shift_px": subtract(last[0], first[0]),
        "snapshot_bottom_shift_px": subtract(last[1], first[1]),
        "actual_window_shift_y_px": os_shift,
    }
    return pair


def analyze(runs, frequencies):
    frames, gestures, absorption = [], [], []
    for run, records in runs.items():
        frequency = frequencies.get(run)
        starts, pointers, by_serial = {}, collections.defaultdict(list), {}
        for record in records:
            gesture = record.get("gesture_id", 0)
            if record.get("event") == "gesture_start":
                starts[gesture] = record
            if record.get("event") in ("gesture_start", "pointer") and "input" in record:
                pointers[gesture].append(record)
                serial = record.get("serial", {}).get("frame")
                if serial is not None:
                    by_serial[(gesture, serial)] = record
        for values in pointers.values():
            values.sort(key=lambda item: (input_time(item), item.get("seq", 0)))
        pointer_times = {gesture: [input_time(item) for item in values] for gesture, values in pointers.items()}
        candidates, successes = {}, {}
        before_absorb = None
        for record in records:
            event, frame = record.get("event"), record.get("frame", 0)
            if event in ("absorb_before", "absorb_after"):
                if event == "absorb_before":
                    before_absorb = record
                elif before_absorb is not None:
                    absorption.append(absorption_pair(before_absorb, record))
                    before_absorb = None
                continue
            if frame <= 0 or event == "gesture_basis":
                continue
            candidate = candidates.setdefault(frame, {})
            merge_frame(candidate, record)
            if event == "committed" or (event == "present_result" and record.get("result", {}).get("committed")):
                successes[frame] = json.loads(json.dumps(candidate))
        previous = {}
        first_qpc = min(record.get("qpc", 0) for record in records)
        for frame, record in sorted(successes.items(), key=lambda item: item[1].get("qpc", 0)):
            gesture, qpc = record.get("gesture_id", 0), record.get("qpc", 0)
            start = starts.get(gesture, {})
            normalized_y = element(start.get("input", {}), "normalized_down", 1) if start.get("flags", 0) & GRAB_VALID else None
            serial = record.get("serial", {}).get("frame")
            consumed = by_serial.get((gesture, serial))
            values, times = pointers.get(gesture, []), pointer_times.get(gesture, [])
            ulw_qpc = element(record.get("result", {}), "stage_qpc", 1)
            latest_time = ulw_qpc if finite(ulw_qpc) and ulw_qpc > 0 else qpc
            latest_index = bisect.bisect_right(times, latest_time) - 1
            latest = values[latest_index] if latest_index >= 0 else None
            row = frame_metrics(record, normalized_y, consumed, latest, frequency,
                                first_qpc, previous.get(gesture))
            previous[gesture] = qpc
            frames.append(row)
        gesture_ids = sorted({record.get("gesture_id", 0) for record in records} - {0})
        for gesture in gesture_ids:
            selected = [row for row in frames if row["run_id"] == run and row["gesture_id"] == gesture]
            events = collections.Counter(record.get("event") for record in records if record.get("gesture_id") == gesture)
            docked = [row for row in selected if row["mode"] == "BottomDocked"]
            settled = [row for row in docked if not row["capture_active"] and
                       finite(row["capture_dip"]) and abs(row["capture_dip"]) <= 0.15]

            def largest(rows, name):
                values = [abs(row[name]) for row in rows if finite(row[name])]
                return max(values) if values else None

            gestures.append({
                "run_id": run, "gesture_id": gesture, "committed_frames": len(selected),
                "events": dict(events),
                "max_base_bottom_error_px": largest(docked, "base_bottom_error_px"),
                "max_settled_nominal_bottom_error_px": largest(settled, "visible_bottom_error_px"),
                "max_consumed_grab_error_px": largest([row for row in selected if row["drag"]], "grab_error_consumed_px"),
                "max_candidate_grab_error_px": largest([row for row in selected if row["drag"]], "grab_error_candidate_px"),
                "max_effective_grab_error_px": largest([row for row in selected if row["drag"]], "grab_error_effective_px"),
                "solver_constrained_frames": sum(row["solver_constrained"] is True for row in selected),
                "max_snapshot_to_commit_ms": largest(selected, "snapshot_to_commit_ms"),
                "max_present_gap_ms": largest(selected, "present_gap_ms"),
                "frames_with_quality_notes": sum(bool(row["data_quality"]) for row in selected),
            })
    return frames, gestures, absorption


def write_report(output, runs, frequencies, issues, frames, gestures, absorption):
    output.mkdir(parents=True, exist_ok=True)
    with (output / "frames.csv").open("w", encoding="utf-8-sig", newline="") as stream:
        if frames:
            writer = csv.DictWriter(stream, fieldnames=list(frames[0]))
            writer.writeheader()
            writer.writerows(frames)
    metadata = {}
    for run, records in runs.items():
        metadata[str(run)] = {
            "qpc_frequency": frequencies.get(run), "records": len(records),
            **{key: max((record.get(key, 0) for record in records), default=0)
               for key in ("dropped", "probe_misses", "writer_errors", "writer_dropped", "overwritten", "retention_removed")},
        }
    summary = {"data_quality": issues, "runs": metadata, "gestures": gestures}
    (output / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    (output / "absorption.json").write_text(json.dumps(absorption, ensure_ascii=False, indent=2), encoding="utf-8")
    lines = [
        "# Bar 底栏追踪分析", "",
        "以下数值来自成功提交的名义几何，不能单独证明截图中的像素闪动。捕获弹簧运行时底边差值可非零。",
        "candidate 抓点误差先剔除快照后窗口成功直移再对照消费输入；consumed 保留该窗口追赶量；latest 对照 ULW 完成时最近输入（无该时间时回退提交记录时刻），还包含输入延迟。没有采样实际 DWM 像素。缺失值保留为空，不按零处理。",
        "新修复版的 effective 指标仍使用独立 Down probe 还原抓点，再对照生产求解器的有效目标；solver_constrained 表示屏幕/正高度保护生效。旧日志没有该字段时保留为空。",
        "出现 dropped、probe_misses 或 data_quality 时先检查记录完整性。ABSORB 前后原始记录单独保存在 absorption.json。", "",
        "| Run / 手势 | 成功帧 | 基准底边最大误差 px | 捕获收敛后名义底边最大误差 px | candidate 抓点最大误差 px | effective 抓点最大误差 px | consumed 抓点最大误差 px | 数据备注帧 |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]

    def display(value):
        return f"{value:.3f}" if finite(value) else "缺数据"

    for item in gestures:
        lines.append(
            f"| {item['run_id']} / {item['gesture_id']} | {item['committed_frames']} | "
            f"{display(item['max_base_bottom_error_px'])} | {display(item['max_settled_nominal_bottom_error_px'])} | "
            f"{display(item['max_candidate_grab_error_px'])} | {display(item['max_effective_grab_error_px'])} | "
            f"{display(item['max_consumed_grab_error_px'])} | {item['frames_with_quality_notes']} |")
    lines += ["", "## 运行元数据", ""]
    for run, info in metadata.items():
        lines.append(f"- Run {run}: records={info['records']}, dropped={info['dropped']}, writer_dropped={info['writer_dropped']}, "
                     f"probe_misses={info['probe_misses']}, writer_errors={info['writer_errors']}, "
                     f"retention_removed={info['retention_removed']}, qpc_frequency={info['qpc_frequency']}")
    if absorption:
        lines += ["", "## 位移吸收", "",
                  "下表只对比生产 presented_snapshot 重建端点和实际 HWND 位移；候选 geometry 与独立图像 probe 不替代该快照。",
                  "", "| Run / 手势 / frame | 快照底边变化 px | 实际窗口 Y 变化 px |",
                  "| --- | ---: | ---: |"]
        for pair in absorption:
            record, metrics = pair["after"], pair["metrics"]
            lines.append(f"| {record['run_id']} / {record.get('gesture_id', 0)} / {record.get('frame', 0)} | "
                         f"{display(metrics['snapshot_bottom_shift_px'])} | "
                         f"{display(metrics['actual_window_shift_y_px'])} |")
    if issues:
        lines += ["", "## 读取问题", ""] + [f"- {issue}" for issue in issues]
    (output / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", help="JSONL 文件、通配符或包含追踪文件的目录")
    parser.add_argument("--out", type=Path, required=True, help="分析输出目录")
    arguments = parser.parse_args()
    try:
        runs, frequencies, issues = load_traces(expand_paths(arguments.logs))
        frames, gestures, absorption = analyze(runs, frequencies)
        write_report(arguments.out, runs, frequencies, issues, frames, gestures, absorption)
    except (OSError, ValueError, TypeError) as error:
        parser.exit(2, f"分析失败：{error}\n")
    print(f"Analyzed {len(runs)} runs, {len(gestures)} gestures, {len(frames)} committed frames.")
    print(f"Report: {(arguments.out / 'summary.md').resolve()}")


if __name__ == "__main__":
    main()
