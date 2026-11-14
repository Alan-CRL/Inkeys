"""把图标路径内联进预览模板。用法: python preview_build.py（在任意目录）"""
import json
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
icons = {f.stem: re.search(r' d="([^"]+)"', f.read_text(encoding="utf-8")).group(1)
         for f in sorted((HERE / "icons").glob("*.svg"))}
template = (HERE / "indicator-v2-preview.template.html").read_text(encoding="utf-8")
(HERE / "indicator-v2-preview.html").write_text(template.replace("/*ICONS*/{}", json.dumps(icons)), encoding="utf-8")
print(f"{len(icons)} icons inlined")
