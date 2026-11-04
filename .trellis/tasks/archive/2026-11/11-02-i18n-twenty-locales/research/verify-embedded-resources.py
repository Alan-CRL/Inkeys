"""Read the executable as data and verify all locale resources match final JSONCs."""
from pathlib import Path
import ctypes
from ctypes import wintypes
import hashlib
import json
import re

# 按solution定位仓库，任务归档后目录层级会变化。
root = next(parent for parent in Path(__file__).resolve().parents
            if (parent / "InkeysRepo.sln").is_file())
kernel = ctypes.WinDLL("kernel32", use_last_error=True)
kernel.LoadLibraryExW.argtypes = [wintypes.LPCWSTR, wintypes.HANDLE, wintypes.DWORD]
kernel.LoadLibraryExW.restype = wintypes.HMODULE
kernel.FindResourceW.argtypes = [wintypes.HMODULE, ctypes.c_void_p, wintypes.LPCWSTR]
kernel.FindResourceW.restype = wintypes.HRSRC
kernel.LoadResource.argtypes = [wintypes.HMODULE, wintypes.HRSRC]
kernel.LoadResource.restype = wintypes.HGLOBAL
kernel.LockResource.argtypes = [wintypes.HGLOBAL]
kernel.LockResource.restype = ctypes.c_void_p
kernel.SizeofResource.argtypes = [wintypes.HMODULE, wintypes.HRSRC]
kernel.SizeofResource.restype = wintypes.DWORD
kernel.FreeLibrary.argtypes = [wintypes.HMODULE]

ids = {name: int(value) for name, value in re.findall(
    r"#define\s+(IDR_\w+)\s+(\d+)",
    (root / "Inkeys/resource.h").read_text(encoding="utf-8-sig"))}
locales = re.findall(r'\{ L"([a-z]{2}-[A-Z]{2})", "[^"]+", "[^"]+", 0x[0-9a-f]+, (IDR_\w+),',
                     (root / "Inkeys/IdtI18nLocales.h").read_text(encoding="utf-8-sig"))
assert len(locales) == 20
# LOAD_LIBRARY_AS_DATAFILE prevents execution of the application entry point.
module = kernel.LoadLibraryExW(str(root / "Build/ARM64/Debug/Inkeys.exe"), None, 2)
if not module:
    raise ctypes.WinError(ctypes.get_last_error())
results = []
try:
    for code, name in locales:
        resource = kernel.FindResourceW(module, ctypes.c_void_p(ids[name]), "JSON")
        assert resource, code
        size = kernel.SizeofResource(module, resource)
        memory = kernel.LockResource(kernel.LoadResource(module, resource))
        assert memory and size, code
        embedded = ctypes.string_at(memory, size)
        source = (root / "Inkeys/src/i18n" / (code + ".jsonc")).read_bytes()
        assert embedded == source, f"stale or mismatched embedded JSONC: {code}"
        parsed = json.loads(re.sub(r"^\s*//.*$", "", embedded.decode("utf-8-sig"), flags=re.M))
        assert parsed["Language"] == code
        results.append({"locale": code, "resource": ids[name], "bytes": size,
                        "sha256": hashlib.sha256(embedded).hexdigest()})
finally:
    kernel.FreeLibrary(module)
(Path(__file__).parent / "embedded-resource-parity.json").write_text(
    json.dumps(results, indent=2) + "\n", encoding="utf-8")
print(f"PASS: {len(results)} executable JSON resources match final source files byte for byte.")
