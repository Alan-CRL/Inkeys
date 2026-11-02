"""Apply reviewed leaf-value edits while preserving JSONC layout and file bytes."""

import json
import re
import sys
from collections import defaultdict
from pathlib import Path

root = Path(__file__).resolve().parents[4]
changes = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8-sig"))
grouped = defaultdict(dict)
for change in changes:
    grouped[change["locale"]][change["key"]] = change

value_pattern = re.compile(r'^(\s*"((?:\\.|[^"\\])*)"\s*:\s*)("(?:\\.|[^"\\])*")(\s*,?\s*(?://.*)?)$')
object_pattern = re.compile(r'^\s*"((?:\\.|[^"\\])*)"\s*:\s*$')
for locale, edits in grouped.items():
    path = root / "Inkeys/src/i18n" / f"{locale}.jsonc"
    raw = path.read_bytes()
    bom = b"\xef\xbb\xbf" if raw.startswith(b"\xef\xbb\xbf") else b""
    lines = raw[len(bom):].decode("utf-8").splitlines(keepends=True)
    stack = []
    pending = None
    applied = set()
    for index, original_line in enumerate(lines):
        line = original_line.rstrip("\r\n")
        ending = original_line[len(line):]
        match = value_pattern.fullmatch(line)
        if match:
            name = json.loads('"' + match[2] + '"')
            key = "/".join([*stack, name])
            if key in edits:
                change = edits[key]
                old = json.loads(match[3])
                assert old == change["before"], (locale, key, old, change["before"])
                lines[index] = match[1] + json.dumps(change["after"], ensure_ascii=False) + match[4] + ending
                applied.add(key)
            continue
        declaration = object_pattern.fullmatch(line)
        if declaration:
            pending = json.loads('"' + declaration[1] + '"')
        elif line.strip() == "{":
            if pending is not None:
                stack.append(pending)
                pending = None
        elif line.strip() in ("}", "},") and stack:
            stack.pop()
    assert applied == edits.keys(), (locale, "unmatched keys", edits.keys() - applied)
    path.write_bytes(bom + "".join(lines).encode("utf-8"))
    print(f"{locale}: applied {len(applied)} reviewed text changes")
