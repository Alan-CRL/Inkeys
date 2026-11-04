"""校验图标 SVG 是否符合设计语言。用法: python lint_icons.py <文件或目录>..."""
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

TAG = "rgba(10,0,7,0)"
ALLOWED_ATTRS = {"xmlns", "width", "height", "viewBox", "fill", "fill-rule", "d"}
FORBIDDEN_TOKENS = ("#", "rgb(", "opacity", "gradient", "clip", "transform", "stroke", "<text", "id=")


def lint(path):
    errs = []
    text = path.read_text(encoding="utf-8")
    if text.startswith("﻿"):
        errs.append("含 BOM")
    for tok in FORBIDDEN_TOKENS:
        if tok in text.replace(TAG, ""):
            errs.append(f"禁止内容: {tok}")
    root = ET.fromstring(text)
    if (root.get("width"), root.get("height"), root.get("viewBox")) != ("24", "24", "0 0 24 24"):
        errs.append("画布必须为 24x24 / viewBox 0 0 24 24")
    for el in root.iter():
        tag = el.tag.split("}")[-1]
        if tag not in ("svg", "path"):
            errs.append(f"非法元素 <{tag}>")
        for a, v in el.attrib.items():
            if a not in ALLOWED_ATTRS:
                errs.append(f"非法属性 {a}")
            if a == "fill" and tag == "path" and v != TAG:
                errs.append(f"非占位色 fill={v}")
    for num in re.findall(r"-?\d+\.?\d*", " ".join(p.get("d", "") for p in root.iter() if p.get("d"))):
        if not -0.01 <= float(num) <= 24.01:
            errs.append(f"坐标越界 {num}")
            break
    return errs


def main():
    files = []
    for arg in sys.argv[1:]:
        p = Path(arg)
        files += sorted(p.glob("*.svg")) if p.is_dir() else [p]
    bad = 0
    for f in files:
        errs = lint(f)
        if errs:
            bad += 1
            print(f"FAIL {f.name}: {'; '.join(sorted(set(errs)))}")
    print(f"{len(files) - bad}/{len(files)} passed")
    sys.exit(1 if bad else 0)


main()
