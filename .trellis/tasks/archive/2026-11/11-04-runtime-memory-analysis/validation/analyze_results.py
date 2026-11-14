"""Summarize measured memory and compare existing history correctness records."""
from pathlib import Path
import collections
import csv
import json
import statistics

root = Path(__file__).resolve().parent
labels = ("before-startup", "before-startup-repeat", "after-startup", "after-startup-repeat")
samples = {label: json.loads((root / (label + ".summary.json")).read_text()) for label in labels}
identities = {phase: json.loads((root / (phase + "-identity.json")).read_text()) for phase in ("before", "after")}
for label, result in samples.items():
    phase = label.split("-", 1)[0]
    assert result["exe_sha256"] == identities[phase]["files"][0]["sha256"]
    assert not result["timed_out"] and not result["forced_cleanup"]
    assert result["duration_seconds"] >= 60 and result["sample_count"] >= 230


def history(label):
    rows = {}
    for line in (root / (label + ".stdout.log")).read_text().splitlines():
        if line.startswith("Draw3HistoryBenchmark scenario=") and "phase=measured" in line:
            row = dict(item.split("=", 1) for item in line.split()[1:] if "=" in item)
            rows[(row["scenario"], row["block"])] = row
    return rows


before, after = history("before-history"), history("after-history")
assert before.keys() == after.keys() and len(before) == 66
fields = ("digest", "strokes", "undo_tiles", "composition_tiles", "visible_tiles", "pieces",
          "stored_strokes", "retained_items", "visible_items", "redo_items", "eraser_strokes")
assert all(before[key][field] == after[key][field] for key in before for field in fields)
history_rows = []
for scenario in dict.fromkeys(key[0] for key in before):
    left = [value for key, value in before.items() if key[0] == scenario]
    right = [value for key, value in after.items() if key[0] == scenario]
    b = statistics.median(int(row["private_before"]) for row in left)
    a = statistics.median(int(row["private_before"]) for row in right)
    history_rows.append({"scenario": scenario, "before_private_bytes": b,
                         "after_private_bytes": a, "saved_bytes": b - a})

metrics = ("tail_private_median", "tail_working_set_median", "peak_private_bytes", "peak_working_set_bytes")
aggregate = {}
for metric in metrics:
    b = statistics.mean(samples[label][metric] for label in labels[:2])
    a = statistics.mean(samples[label][metric] for label in labels[2:])
    aggregate[metric] = {"before_bytes": b, "after_bytes": a, "saved_bytes": b - a,
                         "before_MiB": b / 1048576, "after_MiB": a / 1048576,
                         "saved_MiB": (b - a) / 1048576, "reduction_percent": 100 * (b - a) / b}

rows = []
for label, data in samples.items():
    row = {"run": label, "pid": data["pid"], "exit_code_after_WM_CLOSE": data["exit_code"],
           "duration_seconds": data["duration_seconds"], "samples": data["sample_count"],
           "gdi_handles_final": data["last_sample"]["gdi_handles"]}
    row.update({metric + "_MiB": data[metric] / 1048576 for metric in metrics})
    rows.append(row)
with (root / "startup-comparison.csv").open("w", newline="", encoding="utf-8") as output:
    writer = csv.DictWriter(output, fieldnames=rows[0].keys())
    writer.writeheader()
    writer.writerows(rows)

tests = json.loads((root / "test-results.json").read_text())
retry = json.loads((root / "uink-file-retry.result.json").read_text())
assert retry["exit_code"] == 0
assert all(row["exit_code"] == 0 for row in tests if row["name"] != "uink-file")
for name in ("build-before-direct.result.json", "build-after.result.json", "build-uink.result.json"):
    assert json.loads((root / name).read_text())["exitCode"] == 0

regions = {}
for label, data in samples.items():
    snapshot = data["regions"][-1]
    sizes = collections.Counter(item["bytes"] for item in snapshot["large_regions"] if item["type"] == "private_commit")
    regions[label] = {"at_seconds": snapshot["elapsed_seconds"], "totals": snapshot["totals"],
                      "private_regions_2880x1920_BGRA": sizes[2880 * 1920 * 4]}

result = {"statistics": "Mean of two independent process medians over each run's last 20 samples (~5s)",
          "configuration": "Debug|ARM64", "physical_primary_pixels": [2880, 1920],
          "startup_runs": rows, "aggregate": aggregate, "history_digest_records_equal": 66,
          "history_private": history_rows, "regions": regions,
          "correctness_tests_final_passed": len(tests), "uink_file_fixture_retry": retry,
          "wpr": json.loads((root / "wpr-before.result.json").read_text())}
(root / "memory-results.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(json.dumps({"aggregate": aggregate, "history_digest_records_equal": 66,
                  "tests_final_passed": len(tests), "regions": regions}, ensure_ascii=False))
