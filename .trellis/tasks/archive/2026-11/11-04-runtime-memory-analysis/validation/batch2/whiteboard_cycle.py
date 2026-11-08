"""Sample an isolated product through its existing whiteboard command pipe."""
import argparse
import ctypes
from ctypes import wintypes
import csv
import hashlib
import json
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from sample_process import ProductProcess, kernel, user, read_memory, region_summary, close_owned_windows

parser = argparse.ArgumentParser()
parser.add_argument("--exe", required=True)
parser.add_argument("--output", required=True)
opts = parser.parse_args()
executable = Path(opts.exe).resolve(strict=True)
output = Path(opts.output).resolve()
output.parent.mkdir(parents=True, exist_ok=True)
kernel.SetErrorMode(0x0001 | 0x0002 | 0x8000)
callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user.GetWindowLongPtrW.argtypes = [wintypes.HWND, ctypes.c_int]
user.GetWindowLongPtrW.restype = ctypes.c_ssize_t
user.IsWindowVisible.argtypes = [wintypes.HWND]
user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]


def freeze_window(pid):
    result = []
    @callback_type
    def visit(window, unused):
        owner = wintypes.DWORD()
        user.GetWindowThreadProcessId(window, ctypes.byref(owner))
        if owner.value == pid:
            name = ctypes.create_unicode_buffer(256)
            user.GetClassNameW(window, name, len(name))
            if name.value.startswith("Inkeys1;"):
                result.append({"hwnd": int(window), "class": name.value,
                               "exstyle": user.GetWindowLongPtrW(window, -20),
                               "visible": bool(user.IsWindowVisible(window))})
        return True
    user.EnumWindows(visit, 0)
    return result[0] if result else None


process = ProductProcess(executable, [])
handle = kernel.OpenProcess(0x0400 | 0x0010, False, process.pid)
if not handle:
    process.terminate()
    raise ctypes.WinError(ctypes.get_last_error())
print(json.dumps({"started_pid": process.pid, "exe": str(executable)}), flush=True)
start = phase_start = time.monotonic()
phase = "desktop"
samples, events, regions = [], [], []
failure = None
forced = False
forwarder = None
next_region = 1.0
try:
    while process.poll() is None:
        now = time.monotonic()
        elapsed = now - start
        if elapsed > 180:
            failure = "cycle exceeded 180 seconds"
            break
        window = freeze_window(process.pid)
        value = read_memory(handle)
        if value:
            value.update({"elapsed_seconds": round(elapsed, 4), "phase": phase,
                          "freeze_exstyle": window["exstyle"] if window else 0,
                          "freeze_visible": int(window["visible"]) if window else 0})
            samples.append(value)
        if elapsed >= next_region:
            regions.append({"elapsed_seconds": round(elapsed, 3), "phase": phase, **region_summary(handle)})
            next_region = elapsed + 15
        if phase == "desktop" and now - phase_start >= 20:
            forwarder = ProductProcess(executable, ["-Whiteboard"])
            events.append({"action": "command_pipe_whiteboard", "seconds": round(elapsed, 3), "pid": forwarder.pid})
            phase, phase_start = "entering", now
        elif phase == "entering":
            if window and window["exstyle"] & 0x00040000 and window["visible"]:
                events.append({"action": "whiteboard_anchor_visible", "seconds": round(elapsed, 3), **window})
                phase, phase_start = "whiteboard", now
            elif now - phase_start > 40:
                failure = "whiteboard did not acquire a visible APPWINDOW Freeze anchor"
                break
        elif phase == "whiteboard" and now - phase_start >= 25:
            if not window or not user.PostMessageW(window["hwnd"], 0x0010, 0, 0):
                failure = "could not post whiteboard-close to owned Freeze anchor"
                break
            events.append({"action": "close_whiteboard_anchor", "seconds": round(elapsed, 3), **window})
            phase, phase_start = "exiting", now
        elif phase == "exiting":
            if window and not (window["exstyle"] & 0x00040000):
                events.append({"action": "returned_desktop_anchor", "seconds": round(elapsed, 3), **window})
                phase, phase_start = "returned", now
            elif now - phase_start > 40:
                failure = "whiteboard anchor did not retire to desktop"
                break
        elif phase == "returned" and now - phase_start >= 35:
            break
        time.sleep(0.25)
finally:
    if process.poll() is None:
        # 只清理自有实例；此路径可能因Drawpad先关闭而触发既有Host受控退出码6。
        close_owned_windows(process.pid)
        try:
            process.wait(timeout=10)
        except Exception:
            forced = True
            process.terminate()
            process.wait(timeout=15)
    if forwarder:
        if forwarder.poll() is None:
            close_owned_windows(forwarder.pid)
            try:
                forwarder.wait(timeout=5)
            except Exception:
                forwarder.terminate()
                forwarder.wait(timeout=10)
        events.append({"action": "forwarder_exit", "exit_code": forwarder.returncode})
        kernel.CloseHandle(forwarder.handle)
    kernel.CloseHandle(handle)
    kernel.CloseHandle(process.handle)

if phase != "returned" and failure is None:
    failure = "main process exited before returned-desktop observation completed"
with output.with_suffix(".samples.csv").open("w", newline="", encoding="utf-8") as stream:
    if samples:
        writer = csv.DictWriter(stream, fieldnames=samples[0].keys())
        writer.writeheader()
        writer.writerows(samples)
statistics = {}
for key in ("desktop", "whiteboard", "returned"):
    group = [row for row in samples if row["phase"] == key][-20:]
    if group:
        statistics[key] = {name: sorted(row[name] for row in group)[len(group)//2]
                           for name in ("private_bytes", "working_set_bytes", "gdi_handles")}
with executable.open("rb") as stream:
    digest = hashlib.file_digest(stream, "sha256").hexdigest()
result = {"exe": str(executable), "sha256": digest, "pid": process.pid, "exit_code": process.returncode,
          "forced_cleanup": forced, "failure": failure, "statistics": statistics,
          "events": events, "regions": regions, "sample_count": len(samples)}
output.with_suffix(".summary.json").write_text(json.dumps(result, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
print(json.dumps({key: value for key, value in result.items() if key != "regions"}, ensure_ascii=False), flush=True)
raise SystemExit(1 if failure else 0)
