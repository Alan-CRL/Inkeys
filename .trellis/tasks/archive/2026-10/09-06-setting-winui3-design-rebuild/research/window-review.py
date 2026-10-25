"""用户授权的 Inkeys 窗口视觉检查；只操作明确指定 PID 的窗口，不使用 Computer Use。"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import struct
import time

u = c.WinDLL('user32', use_last_error=True)
g = c.WinDLL('gdi32', use_last_error=True)
try:
    u.SetProcessDpiAwarenessContext.argtypes = [w.HANDLE]
    u.SetProcessDpiAwarenessContext(c.c_void_p(-4))
except (AttributeError, OSError):
    pass

class Rect(c.Structure):
    _fields_ = [('left', w.LONG), ('top', w.LONG), ('right', w.LONG), ('bottom', w.LONG)]

class Point(c.Structure):
    _fields_ = [('x', w.LONG), ('y', w.LONG)]

class BitmapInfoHeader(c.Structure):
    _fields_ = [('size', w.DWORD), ('width', w.LONG), ('height', w.LONG),
                ('planes', w.WORD), ('bits', w.WORD), ('compression', w.DWORD),
                ('image_size', w.DWORD), ('xppm', w.LONG), ('yppm', w.LONG),
                ('used', w.DWORD), ('important', w.DWORD)]

u.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
u.GetWindowRect.argtypes = [w.HWND, c.POINTER(Rect)]
u.GetClientRect.argtypes = [w.HWND, c.POINTER(Rect)]
u.ClientToScreen.argtypes = [w.HWND, c.POINTER(Point)]
u.IsWindowVisible.argtypes = [w.HWND]
u.IsWindow.argtypes = [w.HWND]
u.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
u.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
u.GetDC.argtypes = [w.HWND]
u.GetDC.restype = w.HDC
u.ReleaseDC.argtypes = [w.HWND, w.HDC]
u.PrintWindow.argtypes = [w.HWND, w.HDC, w.UINT]
u.SetForegroundWindow.argtypes = [w.HWND]
u.BringWindowToTop.argtypes = [w.HWND]
u.ShowWindow.argtypes = [w.HWND, c.c_int]
u.SetWindowPos.argtypes = [w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT]
u.GetDpiForWindow.argtypes = [w.HWND]
u.GetDpiForWindow.restype = w.UINT
u.WindowFromPoint.argtypes = [Point]
u.WindowFromPoint.restype = w.HWND
u.IsChild.argtypes = [w.HWND, w.HWND]
u.GetForegroundWindow.restype = w.HWND
u.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.PostMessageW.restype = w.BOOL
g.CreateCompatibleDC.argtypes = [w.HDC]
g.CreateCompatibleDC.restype = w.HDC
g.CreateCompatibleBitmap.argtypes = [w.HDC, c.c_int, c.c_int]
g.CreateCompatibleBitmap.restype = w.HBITMAP
g.SelectObject.argtypes = [w.HDC, w.HANDLE]
g.SelectObject.restype = w.HANDLE
g.GetDIBits.argtypes = [w.HDC, w.HBITMAP, w.UINT, w.UINT, c.c_void_p, c.c_void_p, w.UINT]
g.DeleteObject.argtypes = [w.HANDLE]
g.DeleteDC.argtypes = [w.HDC]
g.BitBlt.argtypes = [w.HDC, c.c_int, c.c_int, c.c_int, c.c_int, w.HDC, c.c_int, c.c_int, w.DWORD]

def owner(hwnd):
    pid = w.DWORD()
    u.GetWindowThreadProcessId(hwnd, c.byref(pid))
    return pid.value

def info(hwnd):
    title, kind, rect, client = c.create_unicode_buffer(512), c.create_unicode_buffer(256), Rect(), Rect()
    u.GetWindowTextW(hwnd, title, len(title))
    u.GetClassNameW(hwnd, kind, len(kind))
    u.GetWindowRect(hwnd, c.byref(rect))
    u.GetClientRect(hwnd, c.byref(client))
    return dict(hwnd=int(hwnd), pid=owner(hwnd), title=title.value, kind=kind.value,
                visible=bool(u.IsWindowVisible(hwnd)), dpi=u.GetDpiForWindow(hwnd),
                rect=[rect.left, rect.top, rect.right, rect.bottom],
                client=[client.right, client.bottom])

def windows(pid):
    result = []
    callback_type = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    @callback_type
    def visit(hwnd, _):
        if owner(hwnd) == pid:
            result.append(info(hwnd))
        return True
    u.EnumWindows.argtypes = [callback_type, w.LPARAM]
    u.EnumWindows(visit, 0)
    return result

def desktop_is_unlocked():
    # 本脚本用于当前 Windows 11 主机；先检查会话标志，不对锁屏发送输入。
    api = c.WinDLL('wtsapi32', use_last_error=True)
    api.WTSQuerySessionInformationW.argtypes = [w.HANDLE, w.DWORD, c.c_int,
        c.POINTER(c.c_void_p), c.POINTER(w.DWORD)]
    api.WTSFreeMemory.argtypes = [c.c_void_p]
    data, size = c.c_void_p(), w.DWORD()
    if not api.WTSQuerySessionInformationW(None, 0xffffffff, 25, c.byref(data), c.byref(size)):
        return False
    try:
        if size.value < 20:
            return False
        header = c.string_at(data, 20)
        # WTSINFOEX_LEVEL1 在64位结构中位于偏移8，SessionFlags位于偏移16。
        return struct.unpack_from('<I', header, 0)[0] == 1 and struct.unpack_from('<I', header, 16)[0] == 1
    finally:
        api.WTSFreeMemory(data)

def capture(hwnd, path, screen=False):
    from PIL import Image
    r = info(hwnd)['rect']
    width, height = r[2]-r[0], r[3]-r[1]
    if width <= 0 or height <= 0:
        raise RuntimeError('Window has no capture area')
    source = u.GetDC(None)
    target = g.CreateCompatibleDC(source)
    bitmap = g.CreateCompatibleBitmap(source, width, height)
    previous = g.SelectObject(target, bitmap)
    try:
        # 优先请求目标窗口完整内容；失败时只截取该窗口的屏幕矩形。
        printed = not screen and bool(u.PrintWindow(hwnd, target, 2))
        if not printed:
            if not u.IsWindowVisible(hwnd):
                raise RuntimeError('Hidden window could not be printed')
            if not g.BitBlt(target, 0, 0, width, height, source, r[0], r[1], 0x40CC0020):
                raise c.WinError(c.get_last_error())
        g.SelectObject(target, previous)
        header = BitmapInfoHeader(c.sizeof(BitmapInfoHeader), width, -height, 1, 32, 0, 0, 0, 0, 0, 0)
        pixels = c.create_string_buffer(width * height * 4)
        if g.GetDIBits(source, bitmap, 0, height, pixels, c.byref(header), 0) != height:
            raise c.WinError(c.get_last_error())
        output = Path(path).resolve()
        output.parent.mkdir(parents=True, exist_ok=True)
        Image.frombuffer('RGB', (width, height), pixels.raw, 'raw', 'BGRX', 0, 1).save(output)
        return dict(path=str(output), width=width, height=height, print_window=printed)
    finally:
        g.SelectObject(target, previous)
        g.DeleteObject(bitmap)
        g.DeleteDC(target)
        u.ReleaseDC(None, source)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['list', 'capture', 'focus', 'click', 'move', 'wheel', 'resize', 'escape'])
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--hwnd', type=int)
    parser.add_argument('--x', type=int, default=0)
    parser.add_argument('--y', type=int, default=0)
    parser.add_argument('--width', type=int)
    parser.add_argument('--height', type=int)
    parser.add_argument('--delta', type=int, default=-120)
    parser.add_argument('--output')
    parser.add_argument('--message', action='store_true', help='Use standard messages for the owned window')
    parser.add_argument('--screen', action='store_true', help='Capture the visible owned-window rectangle')
    args = parser.parse_args()
    if args.action == 'list':
        print(json.dumps(windows(args.pid), ensure_ascii=False, indent=2))
        return
    hwnd = args.hwnd
    if not hwnd or not u.IsWindow(hwnd) or owner(hwnd) != args.pid:
        raise RuntimeError('Refusing to operate on a window outside the supplied process')
    if args.action == 'capture':
        print(json.dumps(capture(hwnd, args.output, args.screen), ensure_ascii=False))
        return
    if not desktop_is_unlocked():
        raise RuntimeError('Windows session is locked; waiting for the user to unlock it')
    if args.action == 'focus':
        u.ShowWindow(hwnd, 9)
        u.BringWindowToTop(hwnd)
        u.SetForegroundWindow(hwnd)
    elif args.action == 'resize':
        # width/height指定客户区物理像素；保留当前非客户区厚度。
        state = info(hwnd)
        r, client = state['rect'], state['client']
        if not args.width or not args.height:
            raise RuntimeError('Client dimensions are required')
        outer_w = args.width + r[2]-r[0]-client[0]
        outer_h = args.height + r[3]-r[1]-client[1]
        u.SetWindowPos(hwnd, None, args.x, args.y, outer_w, outer_h, 0x0014)
    elif args.action in ('click', 'move', 'wheel'):
        state = info(hwnd)
        if not (0 <= args.x < state['client'][0] and 0 <= args.y < state['client'][1]):
            raise RuntimeError('Input point is outside the client area')
        point = Point(args.x, args.y)
        u.ClientToScreen(hwnd, c.byref(point))
        hit = u.WindowFromPoint(point)
        if hit != hwnd and not u.IsChild(hwnd, hit):
            raise RuntimeError('Input point is obscured by another window; no input was sent')
        if not u.SetCursorPos(point.x, point.y):
            raise RuntimeError('This script process cannot access the input desktop')
        actual = Point()
        if not u.GetCursorPos(c.byref(actual)) or (actual.x, actual.y) != (point.x, point.y):
            raise RuntimeError('Cursor placement was not confirmed; no input was sent')
        time.sleep(0.15)
        hit = u.WindowFromPoint(point)
        if hit != hwnd and not u.IsChild(hwnd, hit):
            raise RuntimeError('Input point is obscured by another window; no click was sent')
        if args.action == 'click':
            if args.message:
                position = (args.y << 16) | (args.x & 0xffff)
                for msg, flags in [(0x0200, 0), (0x0201, 1), (0x0202, 0)]:
                    if not u.PostMessageW(hwnd, msg, flags, position):
                        raise c.WinError(c.get_last_error())
                    time.sleep(0.12)
            else:
                u.mouse_event(0x0002, 0, 0, 0, 0)
                time.sleep(0.09)
                u.mouse_event(0x0004, 0, 0, 0, 0)
        elif args.action == 'wheel':
            if args.message:
                position = ((point.y & 0xffff) << 16) | (point.x & 0xffff)
                wheel = (args.delta & 0xffff) << 16
                if not u.PostMessageW(hwnd, 0x020A, wheel, position):
                    raise c.WinError(c.get_last_error())
            else:
                u.mouse_event(0x0800, 0, 0, c.c_uint32(args.delta), 0)
    elif args.action == 'escape':
        u.SetForegroundWindow(hwnd)
        if owner(u.GetForegroundWindow()) != args.pid:
            raise RuntimeError('Target did not receive focus; no key was sent')
        u.keybd_event(0x1B, 0, 0, 0)
        u.keybd_event(0x1B, 0, 2, 0)
    time.sleep(0.3)
    print(json.dumps(info(hwnd), ensure_ascii=False))

if __name__ == '__main__':
    main()
