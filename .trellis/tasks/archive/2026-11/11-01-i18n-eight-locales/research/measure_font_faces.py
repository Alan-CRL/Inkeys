"""Research only: per-face coverage and ImGui-stb / em-space font metrics."""
import json
import re
import sys
import unicodedata
from pathlib import Path

root = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(root / ".trellis/.runtime/i18n-fonttools"))
from fontTools.ttLib import TTFont
from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.areaPen import AreaPen

token = re.compile(r'"(?:\\.|[^"\\])*"|//[^\r\n]*')
def leaves(node):
    if isinstance(node, str):
        yield node
    elif isinstance(node, dict):
        for child in node.values():
            yield from leaves(child)

texts = {}
for locale in ("zh-CN", "ja-JP", "ko-KR"):
    text = (root / f"Inkeys/src/i18n/{locale}.jsonc").read_text(encoding="utf-8-sig")
    data = json.loads(token.sub(lambda m: m[0] if m[0].startswith('"') else "", text))
    texts[locale] = {ord(c) for c in "".join(leaves(data)) if not unicodedata.category(c).startswith("C")}

files = (
    "HarmonyOS_Sans_SC_Regular.ttf", "HarmonyOS_SansSC_Bold.ttf",
    "HarmonyOS_Sans_TC_Regular.ttf", "HarmonyOS_SansTC_Bold.ttf",
    "NotoSansJP-Regular.ttf", "NotoSansJP-Bold.ttf",
    "NotoSansKR-Regular.ttf", "NotoSansKR-Bold.ttf",
)
results = []
for filename in files:
    font = TTFont(root / "Inkeys/src/ttf" / filename)
    cmap = font.getBestCmap()
    upm = font["head"].unitsPerEm
    ascent, descent = font["hhea"].ascent, font["hhea"].descent
    span = ascent - descent
    extra = 1.213 * .97 if filename.startswith("Harmony") else 1.0
    scripts = {}
    if "GSUB" in font:
        scripts = {r.ScriptTag: [l.LangSysTag for l in r.Script.LangSysRecord]
                   for r in font["GSUB"].table.ScriptList.ScriptRecord}
    glyphs = {}
    glyphset = font.getGlyphSet()
    for char in "設定言語基本漢骨直返あいうホーム한글설정언어永Hag":
        if ord(char) not in cmap:
            continue
        glyph = glyphset[cmap[ord(char)]]
        bounds = BoundsPen(glyphset)
        glyph.draw(bounds)
        if bounds.bounds is None:
            continue
        x0, y0, x1, y1 = bounds.bounds
        area = AreaPen(glyphset)
        glyph.draw(area)
        glyphs[char] = {
            "bounds_units": [x0, y0, x1, y1],
            "imgui_14_ink_height": (y1-y0)*14*extra/span,
            "em_14_ink_height": (y1-y0)*14/upm,
            "normalized_ink_area": abs(area.value)/upm**2,
        }
    coverage = {}
    for locale, points in texts.items():
        missing = sorted(points-set(cmap))
        coverage[locale] = {
            "total": len(points), "missing_count": len(missing),
            "missing": [f"U+{c:04X} {chr(c)}" for c in missing],
        }
    results.append({
        "file": filename, "family": font["name"].getBestFamilyName(),
        "style": font["name"].getBestSubFamilyName(), "weight": font["OS/2"].usWeightClass,
        "upm": upm, "hhea_ascent": ascent, "hhea_descent": descent,
        "hhea_linegap": font["hhea"].lineGap, "stb_span": span,
        "extra_size_scale": extra, "imgui_em_at_nominal_14": 14*extra*upm/span,
        "gsub_scripts": scripts, "coverage": coverage, "glyphs": glyphs,
    })
    print(f"{filename}: UPM={upm}, span={span}, extra={extra:.5f}, effective_em_at14={14*extra*upm/span:.3f}; "
          + ", ".join(f"{lang} missing={val['missing_count']}" for lang,val in coverage.items()))
    font.close()
output = Path(__file__).with_name("font-face-measurements.json")
output.write_text(json.dumps(results, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
