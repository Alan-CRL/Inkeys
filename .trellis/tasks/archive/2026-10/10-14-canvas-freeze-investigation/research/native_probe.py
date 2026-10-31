"""本任务的原生 Win32 采集脚本；只在用户授权的调查过程中使用。"""

import argparse
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import struct
import time
import zlib


u = c.WinDLL("user32", use_last_error=True)
g = c.WinDLL("gdi32", use_last_error=True)
u.SetProcessDpiAwarenessContext.argtypes = [c.c_void_p]
u.SetProcessDpiAwarenessContext(c.c_void_p(-4))
u.GetWindowLongPtrW.argtypes = [w.HWND, c.c_int]
u.GetWindowLongPtrW.restype = c.c_ssize_t
u.GetWindow.argtypes = [w.HWND, w.UINT]
u.GetWindow.restype = w.HWND
u.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
u.GetWindowThreadProcessId.restype = w.DWORD
u.GetForegroundWindow.restype = w.HWND
u.SetForegroundWindow.argtypes = [w.HWND]
u.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.GetDC.argtypes = [w.HWND]
u.GetDC.restype = w.HDC
u.ReleaseDC.argtypes = [w.HWND, w.HDC]
g.CreateCompatibleDC.argtypes = [w.HDC]
g.CreateCompatibleDC.restype = w.HDC
g.SelectObject.argtypes = [w.HDC, w.HANDLE]
g.SelectObject.restype = w.HANDLE
g.CreateDIBSection.argtypes = [w.HDC, c.c_void_p, w.UINT,
                              c.POINTER(c.c_void_p), w.HANDLE, w.DWORD]
g.CreateDIBSection.restype = w.HANDLE
g.BitBlt.argtypes = [w.HDC, c.c_int, c.c_int, c.c_int, c.c_int,
                     w.HDC, c.c_int, c.c_int, w.DWORD]
g.DeleteObject.argtypes = [w.HANDLE]
g.DeleteDC.argtypes = [w.HDC]


class GUIINFO(c.Structure):
    _fields_ = [("cbSize", w.DWORD), ("flags", w.DWORD),
                ("active", w.HWND), ("focus", w.HWND), ("capture", w.HWND),
                ("menuOwner", w.HWND), ("moveSize", w.HWND), ("caret", w.HWND),
                ("caretRect", w.RECT)]


def windows(process_id=0):
    records = []

    @c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    def visit(hwnd, _):
        pid = w.DWORD()
        tid = u.GetWindowThreadProcessId(hwnd, c.byref(pid))
        if process_id and pid.value != process_id:
            return True
        title = c.create_unicode_buffer(512)
        cls = c.create_unicode_buffer(256)
        u.GetWindowTextW(hwnd, title, len(title))
        u.GetClassNameW(hwnd, cls, len(cls))
        rect = w.RECT()
        u.GetWindowRect(hwnd, c.byref(rect))
        info = GUIINFO()
        info.cbSize = c.sizeof(info)
        known = u.GetGUIThreadInfo(tid, c.byref(info))
        records.append(dict(hwnd=hex(hwnd), pid=pid.value, tid=tid,
                            title=title.value, cls=cls.value,
                            visible=bool(u.IsWindowVisible(hwnd)),
                            enabled=bool(u.IsWindowEnabled(hwnd)),
                            rect=[rect.left, rect.top, rect.right, rect.bottom],
                            owner=hex(u.GetWindow(hwnd, 4) or 0),
                            exstyle=hex(u.GetWindowLongPtrW(hwnd, -20)),
                            foreground=hwnd == u.GetForegroundWindow(),
                            capture=hex(info.capture or 0) if known else None,
                            focus=hex(info.focus or 0) if known else None))
        return True

    u.EnumWindows(visit, 0)
    return records


def screenshot(path):
    # 使用 GDI 直接保存屏幕，避免引入产品依赖或安装图像库。
    x, y = u.GetSystemMetrics(76), u.GetSystemMetrics(77)
    width, height = u.GetSystemMetrics(78), u.GetSystemMetrics(79)
    screen = u.GetDC(None)
    memory = g.CreateCompatibleDC(screen)
    header = struct.pack("<IiiHHIIiiII", 40, width, -height,
                         1, 32, 0, width * height * 4, 0, 0, 0, 0)
    header_buffer = c.create_string_buffer(header)
    bits = c.c_void_p()
    bitmap = g.CreateDIBSection(screen, header_buffer, 0, c.byref(bits), None, 0)
    if not bitmap:
        raise c.WinError(c.get_last_error())
    previous = g.SelectObject(memory, bitmap)
    try:
        if not g.BitBlt(memory, 0, 0, width, height, screen, x, y, 0x40CC0020):
            raise c.WinError(c.get_last_error())
        bgra = c.string_at(bits, width * height * 4)
        rgb = bytearray(width * height * 3)
        rgb[0::3], rgb[1::3], rgb[2::3] = bgra[2::4], bgra[1::4], bgra[0::4]
        scan = b"".join(b"\0" + rgb[row * width * 3:(row + 1) * width * 3]
                        for row in range(height))

        def chunk(kind, data):
            return (struct.pack(">I", len(data)) + kind + data +
                    struct.pack(">I", zlib.crc32(kind + data)))

        output = (b"\x89PNG\r\n\x1a\n" +
                  chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
                  chunk(b"IDAT", zlib.compress(scan, 3)) + chunk(b"IEND", b""))
        Path(path).write_bytes(output)
    finally:
        g.SelectObject(memory, previous)
        g.DeleteObject(bitmap)
        g.DeleteDC(memory)
        u.ReleaseDC(None, screen)
    return dict(path=str(Path(path).resolve()), bounds=[x, y, width, height])


def mouse(flags):
    u.mouse_event(flags, 0, 0, 0, 0)


def desktop_state():
    u.OpenInputDesktop.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    u.OpenInputDesktop.restype = w.HANDLE
    u.GetUserObjectInformationW.argtypes = [w.HANDLE, c.c_int, c.c_void_p,
                                           w.DWORD, c.POINTER(w.DWORD)]
    u.CloseDesktop.argtypes = [w.HANDLE]
    handle = u.OpenInputDesktop(0, False, 1)
    result = dict(foreground=hex(u.GetForegroundWindow() or 0), inputDesktopReadable=bool(handle))
    if handle:
        name, needed = c.create_unicode_buffer(256), w.DWORD()
        if u.GetUserObjectInformationW(handle, 2, name, c.sizeof(name), c.byref(needed)):
            result["inputDesktop"] = name.value
        u.CloseDesktop(handle)
    else:
        result["error"] = c.get_last_error()
    wts = c.WinDLL("wtsapi32", use_last_error=True)
    wts.WTSQuerySessionInformationW.argtypes = [w.HANDLE, w.DWORD, c.c_int,
                                               c.POINTER(c.c_void_p), c.POINTER(w.DWORD)]
    wts.WTSFreeMemory.argtypes = [c.c_void_p]
    data, size = c.c_void_p(), w.DWORD()
    if wts.WTSQuerySessionInformationW(None, 0xFFFFFFFF, 25, c.byref(data), c.byref(size)):
        try:
            # Win11 的 WTSINFOEX 对齐后 SessionFlags 位于偏移16；仅读取锁屏标志。
            if size.value >= 20:
                raw = c.string_at(data, 20)
                flags = struct.unpack_from("<i", raw, 16)[0]
                result["sessionFlags"] = flags
                result["sessionLocked"] = flags == 0
        finally:
            wts.WTSFreeMemory(data)
    return result


def click(x, y):
    u.SetCursorPos(x, y)
    time.sleep(0.1)
    mouse(2)
    time.sleep(0.08)
    mouse(4)


def stroke(x, y, dx=260, dy=70):
    u.SetCursorPos(x, y)
    time.sleep(0.1)
    mouse(2)
    try:
        for step in range(1, 33):
            u.SetCursorPos(x + dx * step // 32, y + dy * step // 32)
            time.sleep(0.016)
    finally:
        mouse(4)  # 调查脚本异常时也必须释放注入的左键。


def wait_chains(process_id):
    kernel = c.WinDLL("kernel32", use_last_error=True)
    advapi = c.WinDLL("advapi32", use_last_error=True)

    class THREADENTRY(c.Structure):
        _fields_ = [("size", w.DWORD), ("usage", w.DWORD), ("tid", w.DWORD),
                    ("pid", w.DWORD), ("basePriority", w.LONG),
                    ("deltaPriority", w.LONG), ("flags", w.DWORD)]

    class LOCK(c.Structure):
        _fields_ = [("name", w.WCHAR * 128), ("timeout", c.c_int64), ("alertable", w.BOOL)]

    class THREAD(c.Structure):
        _fields_ = [("pid", w.DWORD), ("tid", w.DWORD),
                    ("waitMs", w.DWORD), ("switches", w.DWORD)]

    class DETAIL(c.Union):
        _fields_ = [("lock", LOCK), ("thread", THREAD)]

    class NODE(c.Structure):
        _fields_ = [("kind", w.DWORD), ("status", w.DWORD), ("detail", DETAIL)]

    kernel.CreateToolhelp32Snapshot.argtypes = [w.DWORD, w.DWORD]
    kernel.CreateToolhelp32Snapshot.restype = w.HANDLE
    kernel.Thread32First.argtypes = [w.HANDLE, c.POINTER(THREADENTRY)]
    kernel.Thread32Next.argtypes = [w.HANDLE, c.POINTER(THREADENTRY)]
    kernel.CloseHandle.argtypes = [w.HANDLE]
    kernel.OpenThread.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenThread.restype = w.HANDLE
    kernel.GetThreadDescription.argtypes = [w.HANDLE, c.POINTER(c.c_void_p)]
    kernel.LocalFree.argtypes = [c.c_void_p]
    advapi.OpenThreadWaitChainSession.argtypes = [w.DWORD, c.c_void_p]
    advapi.OpenThreadWaitChainSession.restype = w.HANDLE
    advapi.GetThreadWaitChain.argtypes = [w.HANDLE, c.c_size_t, w.DWORD, w.DWORD,
                                         c.POINTER(w.DWORD), c.POINTER(NODE), c.POINTER(w.BOOL)]
    advapi.CloseThreadWaitChainSession.argtypes = [w.HANDLE]
    snapshot = kernel.CreateToolhelp32Snapshot(4, 0)
    session = advapi.OpenThreadWaitChainSession(0, None)
    results = []
    try:
        entry = THREADENTRY()
        entry.size = c.sizeof(entry)
        more = kernel.Thread32First(snapshot, c.byref(entry))
        while more:
            if entry.pid == process_id:
                count, cycle = w.DWORD(16), w.BOOL()
                nodes = (NODE * 16)()
                ok = advapi.GetThreadWaitChain(session, 0, 7, entry.tid,
                                              c.byref(count), nodes, c.byref(cycle))
                row = dict(tid=entry.tid, ok=bool(ok), cycle=bool(cycle), nodes=[])
                thread = kernel.OpenThread(0x800, False, entry.tid)
                if thread:
                    description = c.c_void_p()
                    try:
                        if kernel.GetThreadDescription(thread, c.byref(description)) >= 0 and description:
                            row["description"] = c.wstring_at(description)
                    finally:
                        if description:
                            kernel.LocalFree(description)
                        kernel.CloseHandle(thread)
                if ok:
                    for item in nodes[:count.value]:
                        node = dict(kind=item.kind, status=item.status)
                        if item.kind == 8:
                            node.update(pid=item.detail.thread.pid, tid=item.detail.thread.tid,
                                        waitMs=item.detail.thread.waitMs)
                        else:
                            node["name"] = item.detail.lock.name
                        row["nodes"].append(node)
                else:
                    row["error"] = c.get_last_error()
                results.append(row)
            more = kernel.Thread32Next(snapshot, c.byref(entry))
    finally:
        advapi.CloseThreadWaitChainSession(session)
        kernel.CloseHandle(snapshot)
    return results


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=["windows", "screen", "click", "stroke", "close", "activate", "wct", "desktop"])
    parser.add_argument("values", nargs="*")
    parser.add_argument("--pid", type=int, default=0)
    args = parser.parse_args()
    if args.command == "windows":
        result = windows(args.pid)
    elif args.command == "wct":
        result = wait_chains(args.pid)
    elif args.command == "desktop":
        result = desktop_state()
    elif args.command == "screen":
        result = screenshot(args.values[0])
    elif args.command == "click":
        click(*map(int, args.values))
        result = dict(clicked=args.values)
    elif args.command == "stroke":
        stroke(*map(int, args.values))
        result = dict(stroke=args.values)
    elif args.command == "close":
        result = bool(u.PostMessageW(int(args.values[0], 0), 0x10, 0, 0))
    elif args.command == "activate":
        result = bool(u.SetForegroundWindow(int(args.values[0], 0)))
    print(json.dumps(result, ensure_ascii=True, indent=2))


if __name__ == "__main__":
    main()
