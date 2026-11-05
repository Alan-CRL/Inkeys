"""Run selected existing/authorized native probes without opening a console."""
import argparse
import ctypes
import json
from pathlib import Path
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument("--exe", required=True)
parser.add_argument("--headless", required=True)
parser.add_argument("--output", required=True)
parser.add_argument("--only", action="append", default=[])
opts = parser.parse_args()
# 测试失败留日志和退出码，不留需要挂机用户关闭的系统错误对话框。
ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002 | 0x8000)
root = Path(opts.output).resolve()
root.mkdir(parents=True, exist_ok=True)
product = Path(opts.exe).resolve(strict=True)
headless = Path(opts.headless).resolve(strict=True)
cases = [
    ("setting-texture-recovery", product, ["--setting-texture-recovery-test"], 0),
    ("laser-resources", product, ["--draw3-laser-resources-test"], 0),
    ("palm-eraser", product, ["--draw3-palm-eraser-test"], 0),
    ("auxiliary-resources", product, ["--draw3-auxiliary-resource-lifecycle-test"], 0),
    ("freeze-resources", product, ["--freeze-resource-lifecycle-test"], 0),
    ("ui-idle-capacity", product, ["--ui3-idle-capacity-test"], 0),
    ("bad-setting-argc", product, ["--setting-texture-recovery-test", "--unexpected"], 2),
    ("bad-laser-argc", product, ["--draw3-laser-resources-test", "--unexpected"], 2),
    ("bad-auxiliary-argc", product, ["--draw3-auxiliary-resource-lifecycle-test", "--unexpected"], 2),
    ("bad-freeze-argc", product, ["--freeze-resource-lifecycle-test", "--unexpected"], 2),
    ("bad-ui-idle-argc", product, ["--ui3-idle-capacity-test", "--unexpected"], 2),
    ("device-policy", product, ["--draw3-device-policy-test"], 0),
    ("renderer-upload", product, ["--draw3-renderer-upload-test"], 0),
    ("renderer-pixel-warp", product, ["--draw3-renderer-pixel-test"], 0),
    ("renderer-pixel-hardware", product, ["--draw3-renderer-pixel-test-hardware"], 0),
    ("renderer-map", product, ["--draw3-renderer-map-test"], 0),
    ("renderer-commit-failure", product, ["--draw3-renderer-commit-failure-test"], 0),
    ("laser-raster-failure", product, ["--draw3-laser-raster-failure-test"], 0),
    ("draw3-hidden", product, ["--draw3-hidden-test"], 0),
    ("page-control-hidden", product, ["--page-control-hidden-test"], 0),
    ("parked-desktop-exit", product, ["--draw3-parked-desktop-exit-test"], 0),
    ("parked-ppt-retained", product, ["--draw3-parked-ppt-retained-test"], 0),
    ("loaded-retained-install", product, ["--draw3-loaded-retained-install-test"], 0),
    ("whiteboard-storage", product, ["--draw3-whiteboard-storage-test"], 0),
    ("startup-progress", headless, ["--no-window", "--startup-preview-only"], 0),
]
selected = [case for case in cases if not opts.only or case[0] in opts.only]
if opts.only and set(opts.only) != {case[0] for case in selected}:
    parser.error("Unknown --only case")
results = []
for name, executable, arguments, expected in selected:
    started = time.monotonic()
    print(json.dumps({"starting": name, "args": arguments}), flush=True)
    with (root / (name + ".stdout.log")).open("wb") as stdout, (root / (name + ".stderr.log")).open("wb") as stderr:
        try:
            process = subprocess.run([str(executable), *arguments], stdout=stdout, stderr=stderr,
                                     timeout=600, creationflags=subprocess.CREATE_NO_WINDOW)
            code, timed_out = process.returncode, False
        except subprocess.TimeoutExpired:
            code, timed_out = None, True
    row = {"name": name, "exe": str(executable), "args": arguments, "expected_exit": expected,
           "exit_code": code, "timed_out": timed_out, "seconds": round(time.monotonic() - started, 3),
           "passed": not timed_out and code == expected}
    results.append(row)
    suffix = "-" + "-".join(opts.only) if opts.only else ""
    (root / ("test-results" + suffix + ".json")).write_text(
        json.dumps(results, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(row), flush=True)
    if not row["passed"]:
        print((root / (name + ".stderr.log")).read_text(encoding="utf-8", errors="replace")[-2000:], flush=True)
print("Passed", sum(row["passed"] for row in results), "of", len(results), flush=True)
raise SystemExit(0 if all(row["passed"] for row in results) else 1)
