"""Verify edited tracked text retains its encoding/BOM and newline convention."""

import json
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[4]


def git(*args):
    return subprocess.run(["git", *args], cwd=root, capture_output=True, check=True).stdout


def describe(data):
    if data.startswith(b"\xef\xbb\xbf"):
        encoding = "utf-8-sig"
    elif data.startswith(b"\xff\xfe"):
        encoding = "utf-16"
    else:
        try:
            data.decode("utf-8")
            encoding = "utf-8"
        except UnicodeDecodeError:
            encoding = "gbk"
    text = data.decode(encoding)
    crlf = text.count("\r\n")
    bare_lf = text.count("\n") - crlf
    return {"encoding": encoding, "crlf": crlf, "bare_lf": bare_lf}


results = []
failures = []
extensions = {".cpp", ".cppm", ".h", ".rc", ".vcxproj", ".filters", ".sln", ".jsonc", ".md"}
for name in git("diff", "--name-only", "--ignore-submodules").decode("utf-8").splitlines():
    if Path(name).suffix not in extensions:
        continue
    # core.autocrlf=true 时 Git blob 是 LF，必须与检出规则后的 CRLF 比较。
    before = describe(git("cat-file", "--filters", f"HEAD:{name}"))
    after = describe((root / name).read_bytes())
    valid = before["encoding"] == after["encoding"]
    if before["bare_lf"] == 0:
        valid &= after["bare_lf"] == 0
    if before["crlf"] == 0:
        valid &= after["crlf"] == 0
    results.append({"path": name, "before": before, "after": after, "pass": bool(valid)})
    if not valid:
        failures.append(name)

for path in (root / "Inkeys/src/i18n").glob("*.jsonc"):
    info = describe(path.read_bytes())
    if info["encoding"] != "utf-8" or info["bare_lf"]:
        failures.append(str(path.relative_to(root)))

output = Path(__file__).with_name("file-format-audit.json")
output.write_text(json.dumps(results, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(f"Checked {len(results)} tracked text files and all locale assets; failures={len(failures)}")
if failures:
    raise SystemExit("\n".join(failures))
