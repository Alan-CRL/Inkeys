"""修复后 GUI 验证：分别控制真实 stdout/stderr 读端，不暂停产品线程。"""

import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import msvcrt
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("run_directory", type=Path)
    parser.add_argument("output_directory", type=Path)
    parser.add_argument("--max-seconds", type=float, default=240)
    args = parser.parse_args()
    run = args.run_directory.resolve()
    repo = Path(__file__).resolve().parents[4]
    allowed = (repo / "Build/CanvasFreezeInvestigation/fixes").resolve()
    if not run.is_relative_to(allowed) or not run.name.startswith("run-"):
        raise ValueError("Only this task's isolated fixes/run-* directory is allowed")
    output = args.output_directory.resolve()
    if not output.is_relative_to(allowed):
        raise ValueError("Output must stay inside this task's fixes directory")
    output.mkdir(parents=True, exist_ok=True)
    control = output / "control.json"
    mode = {"stdout": "read", "stderr": "read", "stop": False}
    control.write_text(json.dumps(mode), encoding="utf-8")
    kernel = c.WinDLL("kernel32", use_last_error=True)
    kernel.PeekNamedPipe.argtypes = [w.HANDLE, c.c_void_p, w.DWORD,
                                    c.c_void_p, c.POINTER(w.DWORD), c.c_void_p]
    kernel.ReadFile.argtypes = [w.HANDLE, c.c_void_p, w.DWORD,
                               c.POINTER(w.DWORD), c.c_void_p]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    process = subprocess.Popen([str(run / "Inkeys.exe")], cwd=run,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               startupinfo=startup, bufsize=0)
    started = time.monotonic()
    counts = {"stdout": 0, "stderr": 0}
    availability = {"stdout": 0, "stderr": 0}
    handles = {name: msvcrt.get_osfhandle(getattr(process, name).fileno())
               for name in counts}
    forced = False
    reason = "normal-process-exit"
    last_status = 0
    logs = {name: (output / (name + ".log")).open("wb") for name in counts}
    try:
        while process.poll() is None:
            try:
                mode = json.loads(control.read_text(encoding="utf-8"))
            except (OSError, ValueError):
                pass
            if mode.get("stop") or time.monotonic() - started > args.max_seconds:
                reason = "requested-cleanup" if mode.get("stop") else "probe-time-limit"
                break
            for name, handle in handles.items():
                available = w.DWORD()
                ok = kernel.PeekNamedPipe(handle, None, 0, None, c.byref(available), None)
                availability[name] = available.value if ok else 0
                if ok and mode.get(name) == "read" and available.value:
                    buffer = c.create_string_buffer(min(available.value, 65536))
                    received = w.DWORD()
                    if kernel.ReadFile(handle, buffer, len(buffer), c.byref(received), None):
                        logs[name].write(buffer.raw[:received.value])
                        logs[name].flush()
                        counts[name] += received.value
            now = time.monotonic()
            if now - last_status > .2:
                status = dict(pid=process.pid, elapsed=now-started, mode=mode,
                              bytes_read=counts, available=availability, exe=str(run / "Inkeys.exe"))
                (output / "status.json").write_text(json.dumps(status, indent=2), encoding="utf-8")
                last_status = now
            time.sleep(.01)
    finally:
        # 只有自己的子进程可以强制清理；一旦发生便明确标记为验证失败。
        if process.poll() is None:
            forced = True
            process.terminate()
        process.wait(timeout=10)
        for name, log in logs.items():
            log.close()
            getattr(process, name).close()
        result = dict(pid=process.pid, exit=process.returncode, forced=forced,
                      reason=reason, elapsed=time.monotonic()-started,
                      bytes_read=counts, last_available=availability,
                      exe_sha256=hashlib.sha256((run / "Inkeys.exe").read_bytes()).hexdigest())
        (output / "exit.json").write_text(json.dumps(result, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
