"""生成 icon-preview.html：新旧对比、深浅主题、多尺寸、选中态、主栏模拟。
用法: python preview_build.py <Inkeys/src> <research 目录>"""
import html
import json
import shutil
import sys
from pathlib import Path

SRC = Path(sys.argv[1])          # Inkeys/src
RES = Path(sys.argv[2])          # research/
UI = SRC / "UI"
NEW = RES / "icons"
OLD_PNG = RES / "old"
OLD_PNG.mkdir(exist_ok=True)

THEMES = {
    "light": {"bg": "#F7F8FA", "fg": "#474D52", "accent": "#00625C", "sel": "rgba(0,163,153,.18)", "text": "#22272B"},
    "dark": {"bg": "rgb(24,24,24)", "fg": "#FFFFFF", "accent": "#58FFEC", "sel": "rgba(88,255,236,.16)", "text": "#FFFFFF"},
}
SIZES = (28, 20, 18, 14)
BASELINE = ["barSelect", "barBrush1", "barEraser", "barClean", "barSetting", "barRecall"]

# 新图标 -> 被替换的旧资源（同名重绘不列）
REPLACES = {
    "barReturnShow": "barDismiss", "barResumeWhiteboard": "barWhiteboard", "barMove": "barSelect",
    "barEraserClearCanvas": "barClean", "barEraserSpeedSetting": "barSetting",
    "compExplorer": "quick/explorer.png", "compTaskManager": "quick/taskmgr.png",
    "compControlPanel": "quick/control.png", "compShowDesktop": "quick/desktop.png",
    "compLockScreen": "quick/lockworkstation.png", "compEscape": "quick/keyboardesc.png",
    "compAltF4": "quick/altf4.png", "compRollCallRandom": "IslandCaller.png",
    "compRollCallSingle": "IslandCaller.png", "compRollCallQuick": "SecRandom.png",
    "compRollCallQuick2": "SecRandom.png", "compRollCallQuickCompat": "SecRandom.png",
    "compNamePicker": "NamePicker.png", "compClassSettings": "ClassIsland.png",
    "compClassProfile": "ClassIsland.png", "compClassSwap": "ClassIsland.png",
}
MAIN_BAR = ["barSelect", "barBrush2", "barEraser", "barGeometry", "barRecall", "barClean",
            "barWhiteboard", "barFreeze", "barEndShow", "barSetting", "barMore"]
MAIN_BAR_LABEL = ["选择", "画笔", "橡皮", "几何", "撤销", "清空", "白板", "冻结", "结束放映", "设置", "更多"]


def svg(text, color, size):
    t = text.replace("rgba(10,0,7,0)", color).replace("rgba(9,0,2,0)", color)
    return f'<span class="ic" style="width:{size}px;height:{size}px">{t}</span>'


def old_markup(name, color, size):
    if name.endswith(".png"):
        dst = OLD_PNG / name.replace("/", "_")
        if (SRC / name).exists():   # 原 PNG 已从产品删除时沿用 old/ 中的副本
            shutil.copyfile(SRC / name, dst)
        return f'<img class="ic" src="old/{dst.name}" style="width:{size}px;height:{size}px">'
    return svg((UI / f"{name}.svg").read_text(encoding="utf-8"), color, size)


def row(item, theme):
    t = THEMES[theme]
    name = item["name"]
    new_path = NEW / f"{name}.svg"
    new_text = (new_path if new_path.exists() else UI / f"{name}.svg").read_text(encoding="utf-8")
    old_name = REPLACES.get(name, name if (UI / f"{name}.svg").exists() else None)
    if old_name and not old_name.endswith(".png") and not (UI / f"{old_name}.svg").exists():
        old_name = None   # 旧资源已删除
    old = old_markup(old_name, t["fg"], 28) if old_name else '<span class="na">—</span>'
    sizes = "".join(f'<div class="cell">{svg(new_text, t["fg"], s)}<i>{s}</i></div>' for s in SIZES)
    sel = f'<div class="cell selbox" style="background:{t["sel"]}">{svg(new_text, t["accent"], 28)}<i>选中</i></div>'
    return (f'<div class="row" style="background:{t["bg"]};color:{t["text"]}">'
            f'<div class="meta"><b>{html.escape(item["label"])}</b><code>{name}</code></div>'
            f'<div class="cell old">{old}<i>{html.escape(old_name or "新增")}</i></div><div class="arrow">→</div>'
            f'{sizes}{sel}</div>')


def bar(theme, names, labels):
    t = THEMES[theme]
    cells = []
    for n, lab in zip(names, labels):
        p = NEW / f"{n}.svg"
        text = (p if p.exists() else UI / f"{n}.svg").read_text(encoding="utf-8")
        cells.append(f'<div class="btn">{svg(text, t["fg"], 28)}<span>{lab}</span></div>')
    return f'<div class="bar" style="background:{t["bg"]};color:{t["text"]}">{"".join(cells)}</div>'


def main():
    manifest = json.loads((NEW / "manifest.json").read_text(encoding="utf-8"))
    groups = {}
    for m in manifest:
        groups.setdefault(m["group"], []).append(m)
    base = {"light": [], "dark": []}
    for theme in base:
        for n in BASELINE:
            base[theme].append(svg((UI / f"{n}.svg").read_text(encoding="utf-8"), THEMES[theme]["fg"], 28))
    body = ['<h1>Inkeys Bar 图标重设计预览</h1>',
            '<p class="rule">设计语言：24×24 画布 · 单色实心剪影 + 镂空 · 仅占位色 rgba(10,0,7,0) · 统一线宽 2.5 · 统一间隙 1.5 · 圆角端点</p>',
            '<h2>主栏模拟（新图标替换后）</h2>']
    for theme in THEMES:
        body.append(bar(theme, MAIN_BAR, MAIN_BAR_LABEL))
    body.append('<h2>基准参考（现有实心图标，不改动）</h2><div class="pair">')
    for theme in THEMES:
        body.append(f'<div class="ref" style="background:{THEMES[theme]["bg"]}">{"".join(base[theme])}</div>')
    body.append('</div>')
    for g, items in groups.items():
        body.append(f'<h2>{html.escape(g)}（{len(items)}）</h2><div class="pair">')
        for theme in THEMES:
            body.append(f'<div class="col"><h3>{"浅色" if theme == "light" else "深色"}</h3>'
                        + "".join(row(i, theme) for i in items) + '</div>')
        body.append('</div>')
    css = """body{font:13px "Segoe UI","Microsoft YaHei",sans-serif;margin:20px;background:#e9ebee;color:#222}
h2{margin:28px 0 8px}h3{margin:4px 0}.rule{color:#555}.pair{display:flex;gap:16px;flex-wrap:wrap}.col{flex:1;min-width:560px}
.row{display:flex;align-items:center;gap:10px;padding:8px 12px;border-radius:10px;margin-bottom:6px}
.meta{width:150px;display:flex;flex-direction:column}.meta code{font-size:10px;opacity:.6}
.cell{display:flex;flex-direction:column;align-items:center;gap:3px;min-width:44px}.cell i{font-size:9px;opacity:.55;font-style:normal;white-space:nowrap}
.old{opacity:.85;width:120px;min-width:120px}.old i{max-width:118px;overflow:hidden;text-overflow:ellipsis}.arrow{opacity:.4}.selbox{border-radius:6px;padding:4px}
.ic{display:inline-block}.ic svg{width:100%;height:100%;display:block}.na{opacity:.4}
.bar{display:inline-flex;gap:4px;padding:8px;border-radius:14px;margin:4px 8px 4px 0}
.btn{width:60px;display:flex;flex-direction:column;align-items:center;gap:4px;font-size:11px;padding:6px 0}
.ref{display:flex;gap:18px;padding:12px 18px;border-radius:10px}"""
    (RES / "icon-preview.html").write_text(
        f'<!doctype html><meta charset="utf-8"><title>icon-preview</title><style>{css}</style>{"".join(body)}',
        encoding="utf-8")
    print("ok")


main()
