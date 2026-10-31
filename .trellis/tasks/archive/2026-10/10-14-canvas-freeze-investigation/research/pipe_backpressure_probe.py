"""隔离调查：保留真实 stdout 管道，只暂停读端，观察原程序的阻塞与恢复。"""

import argparse
import ctypes as c
from ctypes import wintypes as w
import json
import msvcrt
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("run_directory", type=Path)
    parser.add_argument("output_directory", type=Path)
    args = parser.parse_args()
    run = args.run_directory.resolve()
    output = args.output_directory.resolve()
    if run.name != "run-head" or run.parent.name != "CanvasFreezeInvestigation":
        raise ValueError("Only the task's isolated run-head directory is allowed")
    output.mkdir(parents=True, exist_ok=True)
    control = output / "control.txt"
    control.write_text("read", encoding="utf-8")
    kernel = c.WinDLL("kernel32", use_last_error=True)
    kernel.PeekNamedPipe.argtypes = [w.HANDLE, c.c_void_p, w.DWORD,
                                    c.c_void_p, c.POINTER(w.DWORD), c.c_void_p]
    kernel.ReadFile.argtypes = [w.HANDLE, c.c_void_p, w.DWORD,
                               c.POINTER(w.DWORD), c.c_void_p]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    with (output / "stderr.log").open("wb") as errors, (output / "stdout.log").open("wb") as log:
        process = subprocess.Popen([str(run / "Inkeys.exe")], cwd=run,
                                   stdout=subprocess.PIPE, stderr=errors,
                                   startupinfo=startup, bufsize=0)
        handle = msvcrt.get_osfhandle(process.stdout.fileno())
        total = 0
        started = time.monotonic()
        last_status = 0
        mode = "read"
        try:
            while process.poll() is None:
                requested = control.read_text(encoding="utf-8").strip()
                if requested in ("read", "pause", "stop"):
                    mode = requested
                if mode == "stop":
                    break
                available = w.DWORD()
                if not kernel.PeekNamedPipe(handle, None, 0, None, c.byref(available), None):
                    raise c.WinError(c.get_last_error())
                if mode == "read" and available.value:
                    buffer = c.create_string_buffer(min(available.value, 65536))
                    received = w.DWORD()
                    if not kernel.ReadFile(handle, buffer, len(buffer), c.byref(received), None):
                        raise c.WinError(c.get_last_error())
                    log.write(buffer.raw[:received.value])
                    log.flush()
                    total += received.value
                now = time.monotonic()
                if now - last_status > 0.25:
                    status = dict(pid=process.pid, mode=mode, elapsed=now-started,
                                  available=available.value, bytes_read=total,
                                  exe=str(run / "Inkeys.exe"))
                    (output / "status.json").write_text(json.dumps(status, indent=2), encoding="utf-8")
                    last_status = now
                time.sleep(0.01)
        finally:
            # 只清理本脚本创建并持有句柄的隔离进程，避免结束用户自己的实例。
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=10)
            process.stdout.close()
            (output / "exit.json").write_text(json.dumps(dict(pid=process.pid, exit=process.returncode)), encoding="utf-8")


if __name__ == "__main__":
    main()
