"""已授权 GUI 调查用的单触点注入；不直接调用产品回调。"""

import ctypes as c
from ctypes import wintypes as w
import time


class POINTER_INFO(c.Structure):
    _fields_ = [("pointerType", w.UINT), ("pointerId", w.UINT),
                ("frameId", w.UINT), ("pointerFlags", w.UINT),
                ("sourceDevice", w.HANDLE), ("hwndTarget", w.HWND),
                ("ptPixelLocation", w.POINT), ("ptHimetricLocation", w.POINT),
                ("ptPixelLocationRaw", w.POINT), ("ptHimetricLocationRaw", w.POINT),
                ("dwTime", w.DWORD), ("historyCount", w.UINT),
                ("InputData", c.c_int32), ("dwKeyStates", w.DWORD),
                ("PerformanceCount", c.c_uint64), ("ButtonChangeType", w.UINT)]


class POINTER_TOUCH_INFO(c.Structure):
    _fields_ = [("pointerInfo", POINTER_INFO), ("touchFlags", w.UINT),
                ("touchMask", w.UINT), ("rcContact", w.RECT),
                ("rcContactRaw", w.RECT), ("orientation", w.UINT),
                ("pressure", w.UINT)]


class TouchProbe:
    def __init__(self):
        self.u = c.WinDLL("user32", use_last_error=True)
        self.u.InitializeTouchInjection.argtypes = [w.UINT, w.DWORD]
        self.u.InitializeTouchInjection.restype = w.BOOL
        self.u.InjectTouchInput.argtypes = [w.UINT, c.POINTER(POINTER_TOUCH_INFO)]
        self.u.InjectTouchInput.restype = w.BOOL
        if c.sizeof(POINTER_INFO) != 96 or c.sizeof(POINTER_TOUCH_INFO) != 144:
            raise RuntimeError("This probe expects the tested 64-bit Win32 structure layout")
        if not self.u.InitializeTouchInjection(1, 3):
            raise c.WinError(c.get_last_error())
        self.events = []

    def send(self, flags, x, y):
        packet = POINTER_TOUCH_INFO()
        packet.pointerInfo.pointerType = 2  # PT_TOUCH
        packet.pointerInfo.pointerId = 0
        packet.pointerInfo.pointerFlags = flags
        packet.pointerInfo.ptPixelLocation = w.POINT(x, y)
        packet.touchMask = 7
        packet.rcContact = w.RECT(x - 3, y - 3, x + 3, y + 3)
        packet.orientation = 90
        packet.pressure = 512
        ok = self.u.InjectTouchInput(1, c.byref(packet))
        error = 0 if ok else c.get_last_error()
        self.events.append(dict(flags=flags, x=x, y=y, ok=bool(ok), error=error))
        if not ok:
            raise c.WinError(error)

    def stroke(self, start, end, steps=24):
        # 始终正常抬起，不能以漏发 Up 制造并不存在的产品故障。
        x, y = start
        self.send(0x10006, x, y)
        try:
            for step in range(1, steps + 1):
                time.sleep(0.025)
                next_x = round(start[0] + (end[0] - start[0]) * step / steps)
                next_y = round(start[1] + (end[1] - start[1]) * step / steps)
                self.send(0x20006, next_x, next_y)
                x, y = next_x, next_y
        finally:
            # Up 必须沿用最后成功 UPDATE 的位置；异常会使本次实验无效。
            self.send(0x40000, x, y)
