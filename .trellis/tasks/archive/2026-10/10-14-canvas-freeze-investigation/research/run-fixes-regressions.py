"""按真实产品入口串行验证 F1–F4，保留退出码、输出和被测二进制身份。"""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--include-window", action="store_true")
    parser.add_argument("--only", nargs="*")
    parser.add_argument("--output-label", default="")
    args = parser.parse_args()
    # 归档层级会变化，以 solution 标记定位仓库。
    repo = next((p for p in Path(__file__).resolve().parents if (p / "InkeysRepo.sln").is_file()), None)
    if repo is None:
        raise RuntimeError("Repository root (InkeysRepo.sln) was not found.")
    binaries = repo / "Build/ARM64/Debug"
    output = repo / "Build/CanvasFreezeInvestigation/fixes/regressions"
    if args.output_label:
        if not all(char.isalnum() or char == "-" for char in args.output_label):
            parser.error("Output label must contain only letters, digits, or hyphens")
        output /= args.output_label
    output.mkdir(parents=True, exist_ok=True)
    report = output / ("results-window.json" if args.include_window else "results.json")
    cases = [
        ("f1-diagnostic-output", "Inkeys.exe", ["--draw3-diagnostic-output-test"], 0, 180),
        ("f1-bad-argument", "Inkeys.exe", ["--draw3-diagnostic-output-test", "unexpected"], 2, 30),
        ("f2-current-load-retry", "Inkeys.exe", ["--draw3-ppt-current-load-retry-test"], 0, 180),
        ("f2-pending-topology", "Inkeys.exe", ["--draw3-pending-topology-load-test"], 0, 180),
        ("f3-control-fence", "Inkeys.exe", ["--draw3-control-fence-test"], 0, 240),
        ("headless-no-window", "InkeysHeadlessTests.exe", ["--no-window"], 0, 240),
    ]
    if args.include_window:
        cases.append(("headless-with-window", "InkeysHeadlessTests.exe", [], 0, 240))
    if args.only:
        selected = set(args.only)
        known = {case[0] for case in cases}
        if selected - known:
            parser.error("Unknown or disabled case: " + ", ".join(sorted(selected - known)))
        cases = [case for case in cases if case[0] in selected]
    results = []
    for name, filename, arguments, expected, timeout in cases:
        executable = binaries / filename
        started = time.monotonic()
        result = dict(name=name, command=[str(executable), *arguments],
                      expected_exit=expected, timeout_seconds=timeout,
                      binary_sha256=hashlib.sha256(executable.read_bytes()).hexdigest())
        try:
            # communicate 持续排空两条标准流，避免验证器自己制造输出背压。
            process = subprocess.run(result["command"], cwd=repo, capture_output=True,
                                     timeout=timeout, check=False)
            result.update(exit=process.returncode, timed_out=False,
                          passed=process.returncode == expected)
            stdout, stderr = process.stdout, process.stderr
        except subprocess.TimeoutExpired as error:
            result.update(exit=None, timed_out=True, passed=False)
            stdout, stderr = error.stdout or b"", error.stderr or b""
        result["seconds"] = time.monotonic() - started
        for stream, data in (("stdout", stdout), ("stderr", stderr)):
            log = output / (name + "." + stream + ".log")
            log.write_bytes(data)
            result[stream + "_log"] = str(log)
        results.append(result)
        report.write_text(json.dumps(results, indent=2), encoding="utf-8")
        print(json.dumps({key: result[key] for key in ("name", "exit", "passed", "seconds")}), flush=True)
    return 0 if all(result["passed"] for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
