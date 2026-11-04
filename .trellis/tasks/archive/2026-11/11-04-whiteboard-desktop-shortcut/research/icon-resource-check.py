"""无窗口校验最终 EXE 的白板图标资源和按资源 ID 提取路径。"""
import ctypes
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path
import struct
import sys

exe = Path(sys.argv[1]).resolve()
source = Path(sys.argv[2]).read_bytes()
kernel = ctypes.WinDLL('kernel32', use_last_error=True)
user = ctypes.WinDLL('user32', use_last_error=True)
kernel.LoadLibraryExW.argtypes = [w.LPCWSTR, w.HANDLE, w.DWORD]
kernel.LoadLibraryExW.restype = w.HMODULE
kernel.FindResourceW.argtypes = [w.HMODULE, ctypes.c_void_p, ctypes.c_void_p]
kernel.FindResourceW.restype = w.HANDLE
kernel.LoadResource.argtypes = [w.HMODULE, w.HANDLE]
kernel.LoadResource.restype = w.HANDLE
kernel.LockResource.argtypes = [w.HANDLE]
kernel.LockResource.restype = ctypes.c_void_p
kernel.SizeofResource.argtypes = [w.HMODULE, w.HANDLE]
kernel.SizeofResource.restype = w.DWORD
kernel.FreeLibrary.argtypes = [w.HMODULE]
user.PrivateExtractIconsW.argtypes = [w.LPCWSTR, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.POINTER(w.HANDLE), ctypes.POINTER(w.UINT), w.UINT, w.UINT]
user.PrivateExtractIconsW.restype = w.UINT
user.DestroyIcon.argtypes = [w.HANDLE]
module = kernel.LoadLibraryExW(str(exe), None, 2)
assert module, ctypes.get_last_error()

def resource(identity, kind):
    found = kernel.FindResourceW(module, identity, kind)
    assert found, (identity, kind, ctypes.get_last_error())
    size = kernel.SizeofResource(module, found)
    pointer = kernel.LockResource(kernel.LoadResource(module, found))
    assert pointer and size
    return ctypes.string_at(pointer, size)

try:
    # RC 中主程序图标仍为最小图标组，白板独立为 332。
    resource(191, 14)
    group = resource(332, 14)
    reserved, kind, count = struct.unpack_from('<HHH', group)
    assert (reserved, kind, count) == (0, 1, 9)
    result = []
    for index in range(count):
        width, height, colors, reserved, planes, bits, size, identity = struct.unpack_from('<BBBBHHIH', group, 6 + index * 14)
        png = resource(identity, 3)
        sw, sh, _, _, sp, sb, ss, offset = struct.unpack_from('<BBBBHHII', source, 6 + index * 16)
        assert (width, height, planes, bits, size) == (sw, sh, sp, sb, ss)
        assert png == source[offset:offset + ss]
        target_size = width or 256
        icon, extracted_id = w.HANDLE(), w.UINT()
        extracted = user.PrivateExtractIconsW(str(exe), -332, target_size, target_size, ctypes.byref(icon), ctypes.byref(extracted_id), 1, 0)
        assert extracted == 1 and icon.value, (target_size, extracted, ctypes.get_last_error())
        user.DestroyIcon(icon)
        result.append({'size': target_size, 'resource': identity, 'pngSHA256': hashlib.sha256(png).hexdigest(), 'negativeResourceIdExtracted': True})
    print(json.dumps({'exe': str(exe), 'status': 'PASS', 'primaryIconId': 191, 'whiteboardIconId': 332, 'frames': result}, indent=2))
finally:
    kernel.FreeLibrary(module)
