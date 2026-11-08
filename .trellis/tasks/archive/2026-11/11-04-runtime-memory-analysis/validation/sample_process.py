"""Collect memory for an owned CLI probe or an isolated product startup."""
import argparse
import ctypes
from ctypes import wintypes
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import time


class MemoryCounters(ctypes.Structure):
    _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD)] + [
        (name, ctypes.c_size_t) for name in (
            "PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage",
            "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage", "QuotaNonPagedPoolUsage",
            "PagefileUsage", "PeakPagefileUsage", "PrivateUsage")]


class Region(ctypes.Structure):
    _fields_ = [("BaseAddress", ctypes.c_void_p), ("AllocationBase", ctypes.c_void_p),
                ("AllocationProtect", wintypes.DWORD), ("PartitionId", wintypes.WORD),
                ("RegionSize", ctypes.c_size_t), ("State", wintypes.DWORD),
                ("Protect", wintypes.DWORD), ("Type", wintypes.DWORD)]


kernel = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)
user = ctypes.WinDLL("user32", use_last_error=True)
kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel.OpenProcess.restype = wintypes.HANDLE
kernel.CloseHandle.argtypes = [wintypes.HANDLE]
kernel.VirtualQueryEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p,
                                  ctypes.POINTER(Region), ctypes.c_size_t]
kernel.VirtualQueryEx.restype = ctypes.c_size_t
psapi.GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(MemoryCounters), wintypes.DWORD]
user.GetGuiResources.argtypes = [wintypes.HANDLE, wintypes.DWORD]
user.GetGuiResources.restype = wintypes.DWORD
kernel.GetProcessHandleCount.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]


class StartupInfo(ctypes.Structure):
    _fields_ = [("cb", wintypes.DWORD), ("reserved", wintypes.LPWSTR),
                ("desktop", wintypes.LPWSTR), ("title", wintypes.LPWSTR)] + [
                    (name, wintypes.DWORD) for name in (
                        "x", "y", "width", "height", "x_chars", "y_chars", "fill", "flags")
                ] + [("show", wintypes.WORD), ("reserved_count", wintypes.WORD),
                     ("reserved_data", ctypes.c_void_p), ("stdin", wintypes.HANDLE),
                     ("stdout", wintypes.HANDLE), ("stderr", wintypes.HANDLE)]


class ProcessInfo(ctypes.Structure):
    _fields_ = [("process", wintypes.HANDLE), ("thread", wintypes.HANDLE),
                ("pid", wintypes.DWORD), ("tid", wintypes.DWORD)]


class ProductProcess:
    def __init__(self, executable, arguments):
        kernel.CreateProcessW.argtypes = [wintypes.LPCWSTR, wintypes.LPWSTR,
            ctypes.c_void_p, ctypes.c_void_p, wintypes.BOOL, wintypes.DWORD,
            ctypes.c_void_p, wintypes.LPCWSTR, ctypes.POINTER(StartupInfo), ctypes.POINTER(ProcessInfo)]
        kernel.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
        kernel.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
        startup = StartupInfo()
        startup.cb = ctypes.sizeof(startup)
        startup.flags = 0x100  # 不继承采样器管道，三个标准流明确为空。
        result = ProcessInfo()
        command = ctypes.create_unicode_buffer(subprocess.list2cmdline([str(executable), *arguments]))
        if not kernel.CreateProcessW(str(executable), command, None, None, False,
                                    subprocess.CREATE_NO_WINDOW, None, str(executable.parent),
                                    ctypes.byref(startup), ctypes.byref(result)):
            raise ctypes.WinError(ctypes.get_last_error())
        kernel.CloseHandle(result.thread)
        self.handle, self.pid, self.returncode = result.process, result.pid, None

    def poll(self):
        value = wintypes.DWORD()
        if kernel.GetExitCodeProcess(self.handle, ctypes.byref(value)) and value.value != 259:
            self.returncode = value.value
        return self.returncode

    def wait(self, timeout):
        deadline = time.monotonic() + timeout
        while self.poll() is None:
            if time.monotonic() >= deadline:
                raise subprocess.TimeoutExpired(str(self.pid), timeout)
            time.sleep(0.05)
        return self.returncode

    def terminate(self):
        kernel.TerminateProcess(self.handle, 0xEE)


def read_memory(handle):
    counters = MemoryCounters()
    counters.cb = ctypes.sizeof(counters)
    if not psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb):
        return None
    handles = wintypes.DWORD()
    kernel.GetProcessHandleCount(handle, ctypes.byref(handles))
    return {"private_bytes": counters.PrivateUsage, "working_set_bytes": counters.WorkingSetSize,
            "peak_working_set_bytes": counters.PeakWorkingSetSize,
            "peak_pagefile_bytes": counters.PeakPagefileUsage,
            "handles": handles.value, "gdi_handles": user.GetGuiResources(handle, 0),
            "user_handles": user.GetGuiResources(handle, 1)}


def region_summary(handle):
    totals = {"private_commit": 0, "mapped_commit": 0, "image_commit": 0,
              "other_commit": 0, "reserve_address_bytes": 0}
    large = []
    address = 0
    while address < 0x7FFFFFFFFFFF:
        item = Region()
        if not kernel.VirtualQueryEx(handle, ctypes.c_void_p(address), ctypes.byref(item), ctypes.sizeof(item)):
            break
        size = item.RegionSize
        base = item.BaseAddress or 0
        if item.State == 0x1000:
            kind = {0x20000: "private_commit", 0x40000: "mapped_commit", 0x1000000: "image_commit"}.get(item.Type, "other_commit")
            totals[kind] += size
            if size >= 1024 * 1024:
                large.append({"base": hex(base), "bytes": size, "type": kind,
                              "protect": hex(item.Protect)})
        elif item.State == 0x2000:
            totals["reserve_address_bytes"] += size
        next_address = base + size
        if next_address <= address:
            break
        address = next_address
    return {"totals": totals, "large_regions": large}


def close_owned_windows(process_id):
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    @callback_type
    def visit(window, parameter):
        owner = wintypes.DWORD()
        user.GetWindowThreadProcessId(window, ctypes.byref(owner))
        if owner.value == process_id:
            user.PostMessageW(window, 0x0010, 0, 0)
        return True
    user.EnumWindows(visit, 0)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--duration", type=float, default=60)
    parser.add_argument("--interval", type=float, default=0.25)
    parser.add_argument("--startup", action="store_true")
    parser.add_argument("args", nargs=argparse.REMAINDER)
    opts = parser.parse_args()
    executable = Path(opts.exe).resolve(strict=True)
    output = Path(opts.output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    arguments = opts.args[1:] if opts.args[:1] == ["--"] else opts.args
    stdout_path = output.with_suffix(".stdout.log")
    stderr_path = output.with_suffix(".stderr.log")
    started = time.monotonic()
    if opts.startup:
        # 正常产品基线不重定向标准流，避免额外启用诊断记录池。
        stdout_file = stderr_file = None
    else:
        stdout_file = stdout_path.open("wb")
        stderr_file = stderr_path.open("wb")
    process = ProductProcess(executable, arguments) if opts.startup else subprocess.Popen(
        [str(executable), *arguments], cwd=executable.parent,
        stdout=stdout_file, stderr=stderr_file, creationflags=subprocess.CREATE_NO_WINDOW)
    handle = kernel.OpenProcess(0x0400 | 0x0010, False, process.pid)
    if not handle:
        process.terminate()
        raise ctypes.WinError(ctypes.get_last_error())
    print(json.dumps({"started_pid": process.pid, "exe": str(executable), "args": arguments}), flush=True)
    samples = []
    region_snapshots = []
    forced = False
    timed_out = False
    try:
        next_region_at = 1.0
        while process.poll() is None:
            elapsed = time.monotonic() - started
            current = read_memory(handle)
            if current:
                current["elapsed_seconds"] = round(elapsed, 4)
                samples.append(current)
            if elapsed >= next_region_at:
                region_snapshots.append({"elapsed_seconds": round(elapsed, 4), **region_summary(handle)})
                next_region_at = elapsed + 15
            if elapsed >= opts.duration:
                timed_out = not opts.startup
                if opts.startup:
                    close_owned_windows(process.pid)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        forced = True
                        process.terminate()
                else:
                    forced = True
                    process.terminate()
                break
            time.sleep(opts.interval)
        process.wait(timeout=15)
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=15)
        kernel.CloseHandle(handle)
        if isinstance(process, ProductProcess):
            kernel.CloseHandle(process.handle)
        if stdout_file:
            stdout_file.close()
            stderr_file.close()
    if samples:
        with output.with_suffix(".samples.csv").open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=samples[0].keys())
            writer.writeheader()
            writer.writerows(samples)
    tail = samples[-min(20, len(samples)):] if samples else []
    summary = {"exe": str(executable), "exe_sha256": hashlib.file_digest(executable.open("rb"), "sha256").hexdigest(),
               "pid": process.pid, "args": arguments, "startup": opts.startup,
               "exit_code": process.returncode, "forced_cleanup": forced, "timed_out": timed_out,
               "duration_seconds": round(time.monotonic() - started, 3), "sample_count": len(samples),
               "last_sample": samples[-1] if samples else None,
               "peak_private_bytes": max((sample["private_bytes"] for sample in samples), default=0),
               "peak_working_set_bytes": max((sample["peak_working_set_bytes"] for sample in samples), default=0),
               "tail_private_median": sorted(sample["private_bytes"] for sample in tail)[len(tail)//2] if tail else None,
               "tail_working_set_median": sorted(sample["working_set_bytes"] for sample in tail)[len(tail)//2] if tail else None,
               "regions": region_snapshots}
    output.with_suffix(".summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in summary.items() if key != "regions"}, ensure_ascii=False), flush=True)
    return 1 if timed_out else 0


if __name__ == "__main__":
    raise SystemExit(main())
