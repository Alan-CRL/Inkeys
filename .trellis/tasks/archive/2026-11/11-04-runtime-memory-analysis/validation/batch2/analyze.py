"""Summarize batch2 steady desktop and whiteboard-return measurements."""
import argparse
import csv
import json
from pathlib import Path
import statistics

parser = argparse.ArgumentParser()
parser.add_argument("--after-prefix", default="after")
opts = parser.parse_args()
root = Path(__file__).resolve().parent


def load(name):
    return json.loads((root / name).read_text(encoding="utf-8-sig"))


before = [load("before-startup.summary.json"), load("before-startup-repeat.summary.json")]
after = [load(opts.after_prefix + "-startup.summary.json"), load(opts.after_prefix + "-startup-repeat.summary.json")]
for result in before + after:
    assert not result["timed_out"] and not result["forced_cleanup"] and result["sample_count"] >= 230
metrics = {}
for key in ("tail_private_median", "tail_working_set_median", "peak_private_bytes", "peak_working_set_bytes"):
    b = statistics.mean(row[key] for row in before)
    a = statistics.mean(row[key] for row in after)
    metrics[key] = {"before_MiB": b / 1048576, "after_MiB": a / 1048576,
                    "saved_MiB": (b - a) / 1048576, "percent": 100 * (b - a) / b}
board_before = load("before-whiteboard-cycle.summary.json")
board_after = load(opts.after_prefix + "-whiteboard-cycle.summary.json")
for result in (board_before, board_after):
    assert result["failure"] is None and not result["forced_cleanup"]
    assert all(key in result["statistics"] for key in ("desktop", "whiteboard", "returned"))
phases = {}
for key, result in (("before", board_before), ("after", board_after)):
    phases[key] = {phase: {"private_MiB": data["private_bytes"] / 1048576,
                          "working_set_MiB": data["working_set_bytes"] / 1048576,
                          "gdi_handles": data["gdi_handles"]}
                   for phase, data in result["statistics"].items()}
rows = []
for key, group in (("before", before), ("after", after)):
    for index, result in enumerate(group, 1):
        rows.append({"phase": key, "run": index, "exe_sha256": result["exe_sha256"],
                     "private_MiB": result["tail_private_median"] / 1048576,
                     "working_set_MiB": result["tail_working_set_median"] / 1048576,
                     "gdi_handles": result["last_sample"]["gdi_handles"],
                     "samples": result["sample_count"], "cleanup_exit": result["exit_code"]})
with (root / "startup-comparison.csv").open("w", newline="", encoding="utf-8") as output:
    writer = csv.DictWriter(output, fieldnames=rows[0].keys())
    writer.writeheader()
    writer.writerows(rows)
summary = {"configuration": "Debug|ARM64", "screen_example": [2880, 1920],
           "statistics": "Mean of two independent process tail-20-sample medians (~5s)",
           "after_prefix": opts.after_prefix, "desktop": metrics, "runs": rows,
           "whiteboard_phases": phases,
           "whiteboard_return_increase": {
               key: {metric: phases[key]["returned"][metric] - phases[key]["desktop"][metric]
                     for metric in ("private_MiB", "working_set_MiB", "gdi_handles")}
               for key in phases},
           "cleanup_note": "Owned-window WM_CLOSE may stop Drawpad Host and return development code6; not an exit0 correctness assertion."}
(root / "memory-results.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
print(json.dumps(summary, ensure_ascii=False))
