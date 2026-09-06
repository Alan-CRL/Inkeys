"""只读内嵌字体度量；不把 FreeType/浏览器结果当作 ImGui 实际渲染验收。"""
import json
import struct
from pathlib import Path

root = Path(__file__).resolve().parents[4]
results = []
for name in ['HarmonyOS_Sans_SC_Regular.ttf', 'HarmonyOS_SansSC_Bold.ttf',
             'HarmonyOS_Sans_TC_Regular.ttf', 'HarmonyOS_SansTC_Bold.ttf']:
    data = (root / 'Inkeys/src/ttf' / name).read_bytes()
    count = struct.unpack_from('>H', data, 4)[0]
    tables = {}
    for i in range(count):
        tag, _, offset, length = struct.unpack_from('>4sIII', data, 12 + i * 16)
        tables[tag.decode()] = offset
    em = struct.unpack_from('>H', data, tables['head'] + 18)[0]
    ascent, descent, gap = struct.unpack_from('>hhh', data, tables['hhea'] + 4)
    # stb 的 pixel-height 与 em 的比例；仅作为字号校准起点。
    results.append(dict(font=name, units_per_em=em, ascent=ascent,
        descent=descent, line_gap=gap,
        em_to_stb_height=(ascent-descent)/em,
        uncorrected_14dip_em=14*em/(ascent-descent)))
print(json.dumps(results, ensure_ascii=False, indent=2))
