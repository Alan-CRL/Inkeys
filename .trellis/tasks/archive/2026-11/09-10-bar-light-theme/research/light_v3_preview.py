"""浅色主栏 v3 配色设计稿：现状（青瓷）对比两套候选，使用真实 SVG 与真实尺寸。
用法: python light_v3_preview.py <仓库根>  → 输出 research/light-v3-preview.html"""
import base64
import re
import sys
from pathlib import Path

ROOT = Path(sys.argv[1])
UI = ROOT / "Inkeys/src/UI"
OUT = Path(__file__).parent / "light-v3-preview.html"

# 每套方案：表面/边框/图标/文字/选中/分隔/阴影/主按钮
SCHEMES = {
    "现状 青瓷": dict(
        surface="rgba(247,248,250,.88)", frame="rgba(107,114,120,.315)", shadow="none",
        icon="#474D52", text="#22272B", accent="#00625C", sel="rgba(0,163,153,.12)", pill=None,
        divider="rgba(138,144,150,.30)", hover="rgba(221,225,229,.18)",
        swatch_frame="rgba(107,114,120,.18)",
        logo=dict(pen="#474D52", screen="#474D52", screen_op=.22, outline_op=.85),
        page_icon="#22272B",
        note="中性灰 #474D52 带 207° 青蓝色相，与青色强调叠加后整体发绿；边框 #6B7278@31.5% 偏重、发脏；"
             "图标/文字压在 88% 半透明底上对比不足；主按钮笔身灰 + 深色描边 + 22% 屏幕，像描线稿。"),
    "定稿 v3 中性雾白 + 品牌青": dict(
        surface="rgba(251,251,251,.92)", frame="rgba(0,0,0,.08)",
        shadow="0 2px 6px rgba(0,0,0,.08), 0 0 1px rgba(0,0,0,.10)",
        icon="#2B2B2B", text="#1A1A1A", accent="#006A63", sel="rgba(0,106,99,.10)", pill=None,
        divider="rgba(0,0,0,.10)", hover="rgba(0,0,0,.04)", swatch_frame="rgba(0,0,0,.22)",
        logo=dict(pen="#2B2B2B", screen="#000000", screen_op=.09, outline_op=0),
        page_icon="#2B2B2B",
        note="与设置窗口同一套无色相中性灰（WinUI 3 Light）：图标 #2B2B2B、文字 #1A1A1A，对表面约 13:1 / 16:1；"
             "外框改为 8% 黑 + 一层均匀柔和投影代替厚灰边；选中态 = 品牌青 10% 浅底 + 青色内容；"
             "品牌青只出现在选中态。"),
}

BUTTONS = [("barSelect", "选择"), ("barBrush1", "画笔"), ("barEraser", "橡皮"), ("barClean", "清空"), ("barWhiteboard", "白板")]
SELECTED = 1
SWATCHES = ["#FFFFFF", "#000000", "#FF8B00", "#321EB5", "#FFC510", "#FF1000", "#4EA1B7", "#326ED9", "#66D552", "#306C00", "#FF1ECF"]
BACKGROUNDS = [("白色课件", "#FFFFFF"), ("深色课件", "#1F2A36"),
               ("照片/彩色", "linear-gradient(120deg,#3A6B8C,#C9A66B 55%,#7A3E3E)")]


def svg_uri(name, color):
    t = (UI / f"{name}.svg").read_text(encoding="utf-8")
    t = t.replace("rgba(10,0,7,0)", color).replace("rgba(9,0,2,0)", color)
    return "data:image/svg+xml;base64," + base64.b64encode(t.encode()).decode()


def set_attr(svg, node_id, attr, value):
    def repl(m):
        tag = m.group(0)
        if re.search(rf'\s{attr}="[^"]*"', tag):
            return re.sub(rf'\s{attr}="[^"]*"', f' {attr}="{value}"', tag)
        return tag.replace(f'id="{node_id}"', f'id="{node_id}" {attr}="{value}"')
    return re.sub(rf'<[^>]*id="{node_id}"[^>]*>', repl, svg)


def logo_uri(pen, screen, screen_op, outline_op):
    t = (UI / "logo1.svg").read_text(encoding="utf-8")
    for n in ("pen-cap", "pen-barrel", "pen-tip", "pen-nib"):
        t = set_attr(t, n, "fill", pen)
    for n in ("screen-1", "screen-2"):
        t = set_attr(t, n, "fill", screen)
        t = set_attr(t, n, "fill-opacity", screen_op)
    t = set_attr(t, "pen-outline", "stroke-opacity", outline_op)
    return "data:image/svg+xml;base64," + base64.b64encode(t.encode()).decode()


def contrast_outline(pen):
    """方案 A/B：笔色对表面对比越低描边越实，深色笔不描边。"""
    r, g, b = (int(pen[i:i + 2], 16) / 255 for i in (1, 3, 5))
    lin = lambda c: c / 12.92 if c <= .04045 else ((c + .055) / 1.055) ** 2.4
    lum = .2126 * lin(r) + .7152 * lin(g) + .0722 * lin(b)
    ratio = (0.955 + .05) / (lum + .05)
    return max(0.0, min(1.0, (3.0 - ratio) / 2.0)) * 0.55


def bar(s, logo_pen=None, outline=None):
    l = s["logo"]
    pen = logo_pen or l["pen"]
    op = l["outline_op"] if outline is None else outline
    main = (f'<div class="surf main" style="background:{s["surface"]};box-shadow:inset 0 0 0 1px {s["frame"]},{s["shadow"]}">'
            f'<img src="{logo_uri(pen, l["screen"], l["screen_op"], op)}" style="width:62px;height:62px"></div>')
    btns = []
    for i, (n, lab) in enumerate(BUTTONS):
        sel = i == SELECTED
        c_icon = s["accent"] if sel else s["icon"]
        c_text = s["accent"] if sel else s["text"]
        bg = s["sel"] if sel else "transparent"
        pill = f'<i class="pill" style="background:{s["pill"]}"></i>' if sel and s["pill"] else ""
        btns.append(f'<div class="btn" style="background:{bg}"><img src="{svg_uri(n, c_icon)}" style="height:28px">'
                    f'<span style="color:{c_text}">{lab}</span>{pill}</div>')
        if i == 2:
            btns.append(f'<i class="div" style="background:{s["divider"]}"></i>')
    hover = f'<div class="btn hover" style="background:{s["hover"]}"><img src="{svg_uri("barSetting", s["icon"])}" style="height:28px"><span style="color:{s["text"]}">悬停</span></div>'
    return (f'<div class="row">{main}<div class="surf mainbar" style="background:{s["surface"]};'
            f'box-shadow:inset 0 0 0 1px {s["frame"]},{s["shadow"]}">{"".join(btns)}{hover}</div></div>')


def palette(s):
    sw = "".join(f'<i class="sw" style="background:{c};box-shadow:inset 0 0 0 1px {s["swatch_frame"]}"></i>' for c in SWATCHES)
    return (f'<div class="surf pal" style="background:{s["surface"]};box-shadow:inset 0 0 0 1px {s["frame"]},{s["shadow"]}">'
            f'<span style="color:{s["text"]}">颜色</span>{sw}</div>')


def page(s):
    return (f'<div class="surf page" style="background:{s["surface"]};box-shadow:inset 0 0 0 1px {s["frame"]},{s["shadow"]}">'
            f'<img src="{svg_uri("barMore", s["page_icon"])}" style="height:20px;transform:rotate(-90deg)">'
            f'<b style="color:{s["text"]}">3 / 12</b>'
            f'<img src="{svg_uri("barMore", s["page_icon"])}" style="height:20px;transform:rotate(90deg)"></div>')


def main():
    body = ['<h1>浅色主栏 v3 配色设计稿</h1>',
            '<p class="lead">真实 SVG 与真实尺寸（主栏高 80、按钮 75、图标 28、标签 13 粗体、圆角 8）。'
            '投影与指示条为设计意图示意；动态边缘光无法在网页中精确模拟，单独在规划中说明。</p>']
    for name, s in SCHEMES.items():
        body.append(f'<h2>{name}</h2><p class="note">{s["note"]}</p><div class="bgs">')
        for bg_name, bg in BACKGROUNDS:
            body.append(f'<div class="bg" style="background:{bg}"><em>{bg_name}</em>{bar(s)}'
                        f'<div class="row">{palette(s)}{page(s)}</div></div>')
        body.append('</div>')
        if name != "现状 青瓷":
            pens = "".join(f'<div class="pen"><div class="surf main" style="background:{s["surface"]};box-shadow:inset 0 0 0 1px {s["frame"]},{s["shadow"]}">'
                           f'<img src="{logo_uri(c, s["logo"]["screen"], s["logo"]["screen_op"], contrast_outline(c))}" style="width:62px;height:62px"></div><code>{c}</code></div>'
                           for c in ["#2B2B2B", "#000000", "#FFFFFF", "#FFC510", "#FF1000", "#326ED9", "#66D552"])
            body.append(f'<p class="note">主按钮颜色指示（绘制态整笔真实色；仅低对比笔色才出现描边，深色笔无描边；屏幕为 9% 中性黑）：</p>'
                        f'<div class="bg" style="background:#FFFFFF;display:flex;gap:14px;flex-wrap:wrap">{pens}</div>')
    css = """body{font:13px "Segoe UI","Microsoft YaHei",sans-serif;margin:24px;background:#E9EBEE;color:#222}
h2{margin:28px 0 4px}.lead,.note{color:#444;max-width:1180px}.bgs{display:flex;flex-direction:column;gap:10px}
.bg{position:relative;padding:28px 24px 20px;border-radius:12px}.bg em{position:absolute;left:12px;top:6px;font-size:11px;color:#888;font-style:normal}
.row{display:flex;gap:10px;align-items:flex-start;margin-bottom:10px}
.surf{border-radius:8px;backdrop-filter:blur(0px)}.main{width:80px;height:80px;display:flex;align-items:center;justify-content:center}
.mainbar{height:80px;display:flex;align-items:center;padding:0 5px;gap:0}
.btn{position:relative;width:75px;height:70px;border-radius:4px;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:6px}
.btn span{font-weight:700;font-size:13px}.pill{position:absolute;bottom:2px;width:16px;height:3px;border-radius:2px}
.div{width:1px;height:44px;margin:0 2px}.pal{display:flex;align-items:center;gap:8px;padding:10px 12px}
.pal span{font-weight:700;font-size:13px;margin-right:4px}.sw{width:30px;height:30px;border-radius:4px;display:inline-block}
.page{display:flex;align-items:center;gap:18px;padding:12px 16px;height:26px}.page b{font-size:14px}
.pen{display:flex;flex-direction:column;align-items:center;gap:4px}.pen code{font-size:10px;color:#666}"""
    OUT.write_text(f'<!doctype html><meta charset="utf-8"><title>light-v3</title><style>{css}</style>{"".join(body)}', encoding="utf-8")
    print(OUT)


main()
