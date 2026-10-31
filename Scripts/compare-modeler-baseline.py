#!/usr/bin/env python3
"""Compare exact Modeler replay and run paired, window-free benchmarks."""

import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import statistics
import subprocess
import sys


EXPECTED_VERSION_PAIR = ("20250512.0", "20250512.1")
TIMING_FIELDS = {f"{operation}_ns_per_call" for operation in ("move", "predict", "up", "reset")}


def read_report(path):
    with Path(path).open(encoding="utf-8-sig") as stream:
        report = json.load(stream)
    if report.get("failures") != []:
        raise ValueError(f"{path}: harness failures: {report.get('failures')}")
    return report


def difference(old, new, path="$", limit=12):
    """Return field paths; bits are compared too, with no numeric tolerance."""
    found = []
    if type(old) is not type(new):
        return [f"{path}: type {type(old).__name__} != {type(new).__name__}"]
    if isinstance(old, dict):
        if old.keys() != new.keys():
            return [f"{path}: keys {sorted(old)} != {sorted(new)}"]
        for key in old:
            found.extend(difference(old[key], new[key], f"{path}.{key}", limit - len(found)))
            if len(found) >= limit:
                break
    elif isinstance(old, list):
        if len(old) != len(new):
            return [f"{path}: length {len(old)} != {len(new)}"]
        for index, (a, b) in enumerate(zip(old, new)):
            found.extend(difference(a, b, f"{path}[{index}]", limit - len(found)))
            if len(found) >= limit:
                break
    elif old != new:
        found.append(f"{path}: {old!r} != {new!r}")
    return found


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_report(path, value):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def compare_behavior(old, new):
    versions = (old.get("abseil_version"), new.get("abseil_version"))
    if versions != EXPECTED_VERSION_PAIR:
        raise ValueError(f"Unsupported Abseil version pair {versions!r}; expected {EXPECTED_VERSION_PAIR!r}")
    # 只豁免用户明确批准的根版本字段；ABI、每个 bits 和其它元数据仍精确比较。
    normalized = dict(new)
    normalized["abseil_version"] = old["abseil_version"]
    return difference(old, normalized), {
        "legacy_abseil_version": versions[0],
        "source_abseil_version": versions[1],
        "expected_dependency_metadata_changes": [
            {"path": "$.abseil_version", "legacy": versions[0], "source": versions[1]}
        ],
    }


def replay(args):
    old, new = read_report(args.legacy), read_report(args.source)
    if old.get("schema") != "modeler-baseline-replay-v1" or new.get("schema") != old["schema"]:
        raise ValueError("Unexpected replay report schema")
    diffs, metadata = compare_behavior(old, new)
    result = {
        "schema": "modeler-baseline-comparison-v1",
        "reports_exact_match": old == new,
        "exact_modeler_behavior_match": not diffs,
        "legacy_sha256": sha256(args.legacy),
        "source_sha256": sha256(args.source),
        "cases": len(old["cases"]),
        "assertions": old["assertions"],
        "differences": diffs,
        **metadata,
    }
    if args.summary:
        write_report(args.summary, result)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 1 if diffs else 0


def percentile(values, fraction):
    values = sorted(values)
    index = (len(values) - 1) * fraction
    lower, upper = math.floor(index), math.ceil(index)
    return values[lower] + (values[upper] - values[lower]) * (index - lower)


def distribution(values):
    if not values or any(not math.isfinite(x) or x <= 0 for x in values):
        raise ValueError("Benchmark contains missing, nonfinite or nonpositive timings")
    median = statistics.median(values)
    return {
        "median_ns": median,
        "mean_ns": statistics.mean(values),
        "p95_ns": percentile(values, 0.95),
        "mad_ns": statistics.median(abs(value - median) for value in values),
        "min_ns": min(values),
        "max_ns": max(values),
    }


def median_interval(values):
    # 固定随机种子只用于统计重采样，性能输入本身完全固定。
    generator = random.Random(20250512)
    samples = [statistics.median(generator.choices(values, k=len(values))) for _ in range(10000)]
    return [percentile(samples, 0.025), percentile(samples, 0.975)]


def workload(report):
    """Keep every semantic field, removing only the four timing arrays."""
    return {
        **report,
        "cases": [
            {key: value for key, value in case.items() if key not in TIMING_FIELDS}
            for case in report["cases"]
        ],
    }


def run_harness(program, mode, path, timeout, backend):
    try:
        # Windows 上也禁止控制台窗口；仅在失败时展示捕获的诊断。
        subprocess.run(
            [str(program), mode, str(path)],
            check=True, timeout=timeout,
            creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, encoding="utf-8", errors="replace",
        )
    except subprocess.CalledProcessError as error:
        raise ValueError(
            f"{backend}: {mode} exited {error.returncode}\n{error.stdout or ''}"
        ) from error
    except subprocess.TimeoutExpired as error:
        output = error.stdout or ""
        if isinstance(output, bytes):
            output = output.decode("utf-8", errors="replace")
        raise ValueError(f"{backend}: {mode} timed out\n{output}") from error
    return read_report(path)


def benchmark_version(report, backend, replay_version):
    expected = EXPECTED_VERSION_PAIR[0 if backend == "legacy" else 1]
    if replay_version != expected:
        raise ValueError(f"{backend}: unexpected same-executable replay version {replay_version!r}")
    if "abseil_version" not in report:
        # 冻结的旧 EXE 未在 benchmark 输出版本，仅允许用同 EXE 的 replay 证明 .0。
        if backend != "legacy":
            raise ValueError("source: benchmark is missing Abseil version")
        return {**report, "abseil_version": replay_version}, "same_executable_replay"
    if report["abseil_version"] != expected:
        raise ValueError(f"{backend}: unexpected benchmark version {report['abseil_version']!r}")
    return report, "benchmark_report"


def benchmark(args):
    if args.rounds < 30:
        raise ValueError("Acceptance benchmark requires at least 30 paired rounds")
    out = Path(args.out_dir).resolve()
    out.mkdir(parents=True, exist_ok=True)
    programs = {"legacy": Path(args.legacy).resolve(), "source": Path(args.source).resolve()}
    preflight = {}
    for backend, program in programs.items():
        path = out / f"{backend}-replay.json"
        report = run_harness(program, "--modeler-replay", path, args.timeout, backend)
        if report.get("schema") != "modeler-baseline-replay-v1":
            raise ValueError(f"{path}: unexpected replay schema")
        preflight[backend] = report
    replay_diffs, metadata = compare_behavior(preflight["legacy"], preflight["source"])
    if replay_diffs:
        raise ValueError(f"Benchmark preflight replay differs: {replay_diffs}")
    reports = {key: [] for key in programs}
    expected_workloads = {}
    version_origins = {}
    for index in range(args.rounds):
        # 成对轮次交替顺序，原始报告始终保留以便排查系统噪声。
        order = ("legacy", "source") if index % 2 == 0 else ("source", "legacy")
        for backend in order:
            path = out / f"{index + 1:02d}-{backend}.json"
            report = run_harness(programs[backend], "--modeler-benchmark", path, args.timeout, backend)
            if report.get("schema") != "modeler-baseline-benchmark-v1":
                raise ValueError(f"{path}: unexpected benchmark schema")
            report, origin = benchmark_version(report, backend, preflight[backend]["abseil_version"])
            if backend in version_origins and origin != version_origins[backend]:
                raise ValueError(f"{path}: benchmark version metadata origin changed between rounds")
            version_origins[backend] = origin
            current = workload(report)
            if backend not in expected_workloads:
                expected_workloads[backend] = current
            diffs = difference(expected_workloads[backend], current)
            if diffs:
                raise ValueError(f"{path}: workload/results changed: {diffs}")
            reports[backend].append(report)
        diffs, _ = compare_behavior(workload(reports["legacy"][-1]), workload(reports["source"][-1]))
        if diffs:
            raise ValueError(f"Paired round {index + 1}: workload/results changed: {diffs}")
        print(f"Modeler benchmark paired round {index + 1}/{args.rounds}", flush=True)

    summary = {
        "schema": "modeler-baseline-paired-benchmark-v1",
        "rounds": args.rounds,
        "legacy_exe_sha256": sha256(programs["legacy"]),
        "source_exe_sha256": sha256(programs["source"]),
        "workload_exact_match": True,
        "same_executable_replay_behavior_exact_match": True,
        "benchmark_version_origins": version_origins,
        "replay_evidence_sha256": {
            backend: sha256(out / f"{backend}-replay.json") for backend in programs
        },
        **metadata,
        "decision_rule": "regression requires positive paired 95% median log-ratio CI, "
            "positive median in both halves, and effect exceeding combined within-run MAD noise",
        "operations": [],
    }
    regression = False
    for case_index, case in enumerate(expected_workloads["legacy"]["cases"]):
        for operation in ("move", "predict", "up", "reset"):
            field = f"{operation}_ns_per_call"
            values = {}
            medians = {}
            noise = {}
            for backend in programs:
                batches = [report["cases"][case_index][field] for report in reports[backend]]
                values[backend] = [value for batch in batches for value in batch]
                medians[backend] = [distribution(batch)["median_ns"] for batch in batches]
                noise[backend] = statistics.median(
                    1.4826 * distribution(batch)["mad_ns"] / distribution(batch)["median_ns"]
                    for batch in batches
                )
            ratios = [math.log(b / a) for a, b in zip(medians["legacy"], medians["source"])]
            interval = median_interval(ratios)
            median_log_ratio = statistics.median(ratios)
            split = len(ratios) // 2
            half_medians = [statistics.median(ratios[:split]), statistics.median(ratios[split:])]
            noise_bound = math.hypot(noise["legacy"], noise["source"])
            slower = interval[0] > 0 and min(half_medians) > 0 and median_log_ratio > noise_bound
            regression |= slower
            summary["operations"].append({
                "predictor": case["name"],
                "operation": operation,
                "legacy": distribution(values["legacy"]),
                "source": distribution(values["source"]),
                "paired_median_source_over_legacy": math.exp(median_log_ratio),
                "paired_median_ratio_95pct_ci": [math.exp(x) for x in interval],
                "first_half_median_ratio": math.exp(half_medians[0]),
                "second_half_median_ratio": math.exp(half_medians[1]),
                "relative_within_run_noise": noise_bound,
                "repeatable_regression_beyond_noise": slower,
            })
    summary["passed"] = not regression
    write_report(out / "summary.json", summary)
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 1 if regression else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    compare = commands.add_parser("replay", help="Compare two replay JSON reports exactly")
    compare.add_argument("legacy")
    compare.add_argument("source")
    compare.add_argument("--summary")
    compare.set_defaults(run=replay)
    paired = commands.add_parser("benchmark", help="Run 30 or more alternating paired rounds")
    paired.add_argument("legacy")
    paired.add_argument("source")
    paired.add_argument("--out-dir", required=True)
    paired.add_argument("--rounds", type=int, default=30)
    paired.add_argument("--timeout", type=float, default=60)
    paired.set_defaults(run=benchmark)
    args = parser.parse_args()
    try:
        return args.run(args)
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f"Modeler comparison failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
