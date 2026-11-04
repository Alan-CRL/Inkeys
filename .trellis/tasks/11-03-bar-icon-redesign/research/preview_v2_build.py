"""生成 icon-preview-v2.html：原版 / v1（当前产品）/ v2 三栏对比 + 主栏实际高度模拟。
用法: python preview_v2_build.py <仓库根>"""
import html
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(sys.argv[1])
RES = Path(__file__).parent
UI = ROOT / "Inkeys/src/UI"
V2 = RES / "icons"   # v2 已于评审门 3 后提升为 icons/
OLD_PNG = RES / "old"
THEMES = {
    "light": {"bg": "#F7F8FA", "fg": "#474D52", "accent": "#00625C", "sel": "rgba(0,163,153,.18)", "text": "#22272B"},
    "dark": {"bg": "rgb(24,24,24)", "fg": "#FFFFFF", "accent": "#58FFEC", "sel": "rgba(88,255,236,.16)", "text": "#FFFFFF"},
}


def head(name):
    r = subprocess.run(["git", "-C", str(ROOT), "show", f"HEAD:Inkeys/src/UI/{name}.svg"], capture_output=True)
    return r.stdout.decode("utf-8") if r.returncode == 0 else None


def cur(name):
    p = UI / f"{name}.svg"
    return p.read_text(encoding="utf-8") if p.exists() else None


def v2(name):
    p = V2 / f"{name}.svg"
    return p.read_text(encoding="utf-8") if p.exists() else cur(name)


def ic(text, color, h):
    """与 Bar 一致：按高度缩放，宽度按 viewBox 比例。"""
    if not text:
        return '<span class="na">—</span>'
    if text.startswith("png:"):
        return f'<img class="ic" src="old/{text[4:]}" style="height:{h}px;width:{h}px">'
    vb = [float(x) for x in re.search(r'viewBox="([^"]+)"', text).group(1).split()]
    t = text.replace("rgba(10,0,7,0)", color).replace("rgba(9,0,2,0)", color)
    t = re.sub(r'<svg([^>]*?)\swidth="[^"]*"', r'<svg\1', t, count=1)
    t = re.sub(r'<svg([^>]*?)\sheight="[^"]*"', r'<svg\1', t, count=1)
    return f'<span class="ic" style="height:{h}px;width:{h * vb[2] / vb[3]:.1f}px">{t}</span>'


def png(name):
    return "png:" + name if (OLD_PNG / name).exists() else None


# (标签, 原版, v1, v2, 说明)；原版/v1/v2 为 svg 文本或 png:
ROWS = [
    ("主栏", [
        ("几何", head("barGeometry"), cur("barGeometry"), v2("barGeometry"), "恢复原版（26×24 宽幅，原样不动）"),
        ("白板", head("barWhiteboard"), cur("barWhiteboard"), v2("barWhiteboard"), "满幅画板 + 粗支腿，镂空笔加大"),
        ("结束放映", head("barEndShow"), cur("barEndShow"), v2("barEndShow"), "满幅屏幕 + 底座（去掉顶杆），停止方块加大"),
        ("冻结", head("barFreeze"), cur("barFreeze"), v2("barFreeze"), "满幅；臂 2.75 / 分叉 2.5，小实心六边形中心"),
        ("穿透", head("barPierce"), cur("barPierce"), v2("barPierce"), "框加粗到 3.25、指针放大"),
        ("更多", head("barMore"), cur("barMore"), v2("barMore"), "恢复原版的 3 线宽与跨度"),
    ]),
    ("主栏状态", [
        ("返回放映", head("barDismiss"), cur("barReturnShow"), v2("barReturnShow"), "与结束放映同外形，镂空播放三角"),
        ("回到白板", head("barWhiteboard"), cur("barResumeWhiteboard"), v2("barResumeWhiteboard"), "与白板同外形，镂空上箭头加粗"),
        ("移动（白板）", head("barSelect"), cur("barMove"), v2("barMove"), "改为实心手形；指尖圆头、指根与虎口圆角过渡"),
    ]),
    ("橡皮面板", [
        ("清空画布", head("barClean"), cur("barEraserClearCanvas"), cur("barClean"), "恢复主栏垃圾桶 barClean，删除 barEraserClearCanvas"),
        ("极速橡皮设置", head("barSetting"), cur("barEraserSpeedSetting"), v2("barEraserSpeedSetting"), "保持 v1"),
        ("自动粗细", head("barAutoEraser"), cur("barAutoEraser"), v2("barAutoEraser"), "补上 A 的三角字腔镂空"),
    ]),
]
COMP = [("文件资源管理器", "compExplorer", "explorer"), ("任务管理器", "compTaskManager", "taskmgr"),
        ("控制面板", "compControlPanel", "control"), ("显示桌面", "compShowDesktop", "desktop"),
        ("锁屏", "compLockScreen", "lockworkstation"), ("Esc", "compEscape", "keyboardesc"),
        ("Alt+F4", "compAltF4", "altf4"), ("IslandCaller 1", "compRollCallRandom", "IslandCaller"),
        ("IslandCaller 2", "compRollCallSingle", "IslandCaller"), ("SecRandom 1", "compRollCallQuick", "SecRandom"),
        ("SecRandom 2", "compRollCallQuick2", "SecRandom"), ("SecRandom 2 兼容", "compRollCallQuickCompat", "SecRandom"),
        ("NamePicker", "compNamePicker", "NamePicker"), ("ClassIsland 设置", "compClassSettings", "ClassIsland"),
        ("ClassIsland 档案", "compClassProfile", "ClassIsland"), ("ClassIsland 换课", "compClassSwap", "ClassIsland")]
ROWS.append(("组件（整体放大到满幅，图形不变）", [
    (lab, png(f"quick_{o}.png") or png(f"{o}.png"), cur(n), v2(n), "") for lab, n, o in COMP]))

BASE = ["barSelect", "barBrush1", "barEraser", "barClean", "barRecall", "barSetting"]
BAR_V1 = [("选择", "barSelect"), ("画笔", "barBrush1"), ("橡皮", "barEraser"), ("几何", cur("barGeometry")),
          ("撤销", "barRecall"), ("清空", "barClean"), ("白板", cur("barWhiteboard")), ("冻结", cur("barFreeze")),
          ("穿透", cur("barPierce")), ("结束放映", cur("barEndShow")), ("设置", "barSetting"), ("更多", cur("barMore"))]
BAR_V2 = [(lab, v2(x) if x.startswith("bar") and (V2 / f"{x}.svg").exists() else x) for lab, x in
          [("选择", "barSelect"), ("画笔", "barBrush1"), ("橡皮", "barEraser"), ("几何", "barGeometry"),
           ("撤销", "barRecall"), ("清空", "barClean"), ("白板", "barWhiteboard"), ("冻结", "barFreeze"),
           ("穿透", "barPierce"), ("结束放映", "barEndShow"), ("设置", "barSetting"), ("更多", "barMore")]]
BAR_BOARD_V1 = [("移动", cur("barMove")), ("画笔", "barBrush1"), ("橡皮", "barEraser"),
                ("返回放映", cur("barReturnShow")), ("回到白板", cur("barResumeWhiteboard"))]
BAR_BOARD_V2 = [("移动", v2("barMove")), ("画笔", "barBrush1"), ("橡皮", "barEraser"),
                ("返回放映", v2("barReturnShow")), ("回到白板", v2("barResumeWhiteboard"))]


def resolve(x):
    return cur(x) if x and x.startswith("bar") else x


def bar(theme, items, title, h=28):
    t = THEMES[theme]
    cells = "".join(f'<div class="btn">{ic(resolve(x), t["fg"], h)}<span>{lab}</span></div>' for lab, x in items)
    return f'<div class="barwrap"><div class="tag">{title}</div><div class="bar" style="background:{t["bg"]};color:{t["text"]}">{cells}</div></div>'


def row(item, theme):
    t = THEMES[theme]
    lab, orig, v1, new, note = item
    sizes = "".join(f'<div class="cell">{ic(new, t["fg"], s)}<i>{s}</i></div>' for s in (28, 20, 14))
    sel = f'<div class="cell selbox" style="background:{t["sel"]}">{ic(new, t["accent"], 28)}<i>选中</i></div>'
    return (f'<div class="row" style="background:{t["bg"]};color:{t["text"]}">'
            f'<div class="meta"><b>{html.escape(lab)}</b><span>{html.escape(note)}</span></div>'
            f'<div class="cell dim">{ic(orig, t["fg"], 28)}<i>原版</i></div>'
            f'<div class="cell dim">{ic(v1, t["fg"], 28)}<i>v1 当前</i></div><div class="arrow">→</div>'
            f'<div class="cell v2">{ic(new, t["fg"], 28)}<i>v2</i></div>{sizes}{sel}</div>')


def main():
    body = ['<h1>Inkeys Bar 图标 v2 修改预览</h1>',
            '<p class="rule">问题：v1 只用 2–22 活动区，而画笔/橡皮/选择是满幅（高 24），Bar 按高度缩放，导致 v1 图标显小、显轻。'
            'v2：主栏与组件图标改为 0.5–23.5 满幅，实心更重；几何恢复原版；移动改手形；橡皮面板清空恢复垃圾桶。</p>',
            '<h2>主栏模拟（28 DIP，按高度缩放，与 Bar 一致）</h2>']
    for theme in THEMES:
        body.append('<div class="pair">' + bar(theme, BAR_V1, "v1 当前") + bar(theme, BAR_V2, "v2") + '</div>')
        body.append('<div class="pair">' + bar(theme, BAR_BOARD_V1, "白板中 v1") + bar(theme, BAR_BOARD_V2, "白板中 v2") + '</div>')
    for g, items in ROWS:
        body.append(f'<h2>{html.escape(g)}</h2><div class="pair">')
        for theme in THEMES:
            body.append('<div class="col">' + "".join(row(i, theme) for i in items) + '</div>')
        body.append('</div>')
    css = """body{font:13px "Segoe UI","Microsoft YaHei",sans-serif;margin:20px;background:#e9ebee;color:#222}
h2{margin:26px 0 8px}.rule{color:#444;max-width:1100px}.pair{display:flex;gap:16px;flex-wrap:wrap}.col{flex:1;min-width:620px}
.row{display:flex;align-items:center;gap:10px;padding:8px 12px;border-radius:10px;margin-bottom:6px}
.meta{width:190px;display:flex;flex-direction:column;gap:2px}.meta span{font-size:10px;opacity:.6}
.cell{display:flex;flex-direction:column;align-items:center;gap:3px;min-width:44px}.cell i{font-size:9px;opacity:.55;font-style:normal;white-space:nowrap}
.dim{opacity:.6}.v2{min-width:50px}.arrow{opacity:.4}.selbox{border-radius:6px;padding:4px}
.ic{display:inline-block}.ic svg{width:100%;height:100%;display:block}.na{opacity:.4}
.barwrap{display:flex;flex-direction:column;gap:2px;margin:4px 0}.tag{font-size:11px;color:#666}
.bar{display:inline-flex;gap:2px;padding:8px;border-radius:14px}
.btn{width:58px;height:58px;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:5px;font-size:11px}"""
    (RES / "icon-preview-v2.html").write_text(
        f'<!doctype html><meta charset="utf-8"><title>icon-preview-v2</title><style>{css}</style>{"".join(body)}',
        encoding="utf-8")
    print("ok")


main()
