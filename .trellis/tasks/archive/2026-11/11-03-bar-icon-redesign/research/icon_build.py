"""Inkeys Bar 图标定义（设计源）。用法: python icon_build.py <Inkeys/src/UI> <输出目录>"""
import json
import math
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from icon_lib import (GAP, LINE, QS, U, arc_pts, arrow_head, box, circ, from_svg_paths, knock, line,  # noqa: E402
                      move, poly, rotate, rr, scale, soften, to_svg)

SRC = Path(sys.argv[1])
OUT = Path(sys.argv[2])
OUT.mkdir(parents=True, exist_ok=True)


def src_paths(name):
    return re.findall(r'\sd="([^"]+)"', (SRC / f"{name}.svg").read_text(encoding="utf-8"))


ICONS = {}   # name -> (group, label, geometry)


def icon(name, group, label):
    def deco(fn):
        ICONS[name] = (group, label, fn())
        return fn
    return deco


# ---------- 共享部件 ----------
BADGE_C = (18, 18)

ERASER = from_svg_paths(src_paths("barEraser"))
GEAR = from_svg_paths(src_paths("barSetting"))


def eraser_with_badge(badge):
    body = scale(ERASER, 0.84, origin=(0, 0)).buffer(0)
    body = move(body, 0.5, 0.5)
    return U(knock(body, badge), badge)


def gear_badge(r=4.75):
    g = scale(GEAR, (2 * r) / 24, origin=(0, 0))
    b = g.bounds
    return move(g, BADGE_C[0] - (b[0] + b[2]) / 2, BADGE_C[1] - (b[1] + b[3]) / 2)


def person(cx, head_y=7.0, head_r=3.75, body_top=13.25, body_r=8.0, bottom=21.75):
    head = circ(cx, head_y, head_r)
    body = circ(cx, body_top + body_r, body_r).intersection(box(cx - body_r, body_top, cx + body_r, bottom))
    return U(head, soften(body, 0.75))


def one_person():
    return person(9.75)


def two_people():
    front = person(8.75)
    back = person(15.75, head_y=5.75, head_r=3.0, body_top=10.75, body_r=6.5, bottom=17.5)
    return U(front, knock(back, front))


def fill(g, size=23.0):
    """把 v1 的 20 活动区图标等比放大到与基准图标一致的满幅（最长边 size，居中）。"""
    b = g.bounds
    s = size / max(b[2] - b[0], b[3] - b[1])
    g = scale(g, s, origin=((b[0] + b[2]) / 2, (b[1] + b[3]) / 2))
    b = g.bounds
    return move(g, 12 - (b[0] + b[2]) / 2, 12 - (b[1] + b[3]) / 2)


def with_badge(base, badge):
    return U(knock(base, badge), badge)


def bolt():
    return soften(poly([(19.9, 12.4), (14.1, 18.3), (17.3, 18.3), (16.1, 22.6), (21.9, 16.4), (18.7, 16.4)]), 0.3)


def screen_stand():
    return U(line([(12, 16), (12, 20.25)]), rr(7.25, 19.5, 9.5, 2.5, 1.25))


# v2 满幅版：与画笔/橡皮同为 0.5–23.5 满幅，实心更重
def big_stand():
    return U(box(10.4, 17, 13.6, 21.5), rr(5.5, 20.5, 13, 3, 1.5))


def projection_screen(cut):
    scr = rr(0.5, 0.75, 23, 17.25, 3.5)
    return U(scr, knock(big_stand(), scr, 0)).difference(cut)


def easel(cut):
    board = rr(0.5, 0.75, 23, 16.75, 3.5)
    legs = U(line([(6.75, 16.5), (4.75, 22.25)], 3.25), line([(17.25, 16.5), (19.25, 22.25)], 3.25))
    return U(board, legs).difference(cut)


def pen_shape(tip, end, w=2.8, tip_len=2.6):
    dx, dy = end[0] - tip[0], end[1] - tip[1]
    L = math.hypot(dx, dy)
    ux, uy = dx / L, dy / L
    nx, ny = -uy, ux
    bx, by = tip[0] + ux * tip_len, tip[1] + uy * tip_len
    h = w / 2
    return soften(poly([tip, (bx + nx * h, by + ny * h), (end[0] + nx * h, end[1] + ny * h),
                        (end[0] - nx * h, end[1] - ny * h), (bx - nx * h, by - ny * h)]), 0.3)


def soft_pen_cut(s=0.45, gap=1.25):
    """软笔（barBrush2）外形：圆帽 + 弧形收尖笔身，缩放后帽身间隙保持 gap，并居中到白板板面。"""
    cap = from_svg_paths(["M8.1 1.75A4 4 0 1 1 15.9 1.75Z"])
    body = from_svg_paths(["M8 3.25H16V18.5C16 22.8 13.4 25.2 12.6 27.9A0.75 0.75 0 0 1 11.4 27.9C10.6 25.2 8 22.8 8 18.5Z"])
    body = scale(body, s, origin=(12, 3.25))
    cap = scale(cap, s, origin=(12, 1.75))
    cap = move(cap, 0, (3.25 - gap) - cap.bounds[3])
    pen = rotate(U(cap, body), 45, (12, 12))
    minx, miny, maxx, maxy = pen.bounds
    return move(pen, 12 - (minx + maxx) / 2, 0.75 + 16.75 / 2 - (miny + maxy) / 2)


# ---------- 主栏重绘 ----------
@icon("barWhiteboard", "主栏", "白板")
def _():
    return easel(soft_pen_cut())


@icon("barEndShow", "主栏", "结束放映")
def _():
    return projection_screen(rr(8, 5.25, 8, 8, 1.75))


@icon("barFreeze", "主栏", "冻结")
def _():
    parts = []
    for k in range(6):
        a = math.radians(-90 + 60 * k)
        ux, uy = math.cos(a), math.sin(a)
        tip = (12 + ux * 10.25, 12 + uy * 10.25)
        parts.append(line([(12, 12), tip], 2.75))
        bx, by = 12 + ux * 6.0, 12 + uy * 6.0
        for s in (-1, 1):
            b = a + s * math.radians(50)
            parts.append(line([(bx, by), (bx + math.cos(b) * 4.0, by + math.sin(b) * 4.0)], 2.5))
    hub = poly([(12 + 3.5 * math.cos(math.radians(60 * k)), 12 + 3.5 * math.sin(math.radians(60 * k)))
                for k in range(6)])
    return U(*parts, soften(hub, 0.5))


@icon("barPierce", "主栏", "穿透")
def _():
    cursor = soften(poly([(8.25, 8.25), (23.5, 14.4), (17.4, 17.4), (14.4, 23.5)]), 0.75)
    frame = rr(0.5, 0.5, 17, 17, 4).difference(rr(3.75, 3.75, 10.5, 10.5, 1.5))
    return U(soften(knock(frame, cursor), 0.4), cursor)


# barGeometry：v2 恢复为原版文件（见 KEEP_ORIGINAL），不再由设计源生成。


# ---------- 主栏状态 ----------
@icon("barReturnShow", "主栏状态", "返回放映")
def _():
    return projection_screen(soften(poly([(9.25, 4.75), (9.25, 13.75), (16.5, 9.25)]), 0.75))


@icon("barResumeWhiteboard", "主栏状态", "回到白板")
def _():
    arrow = U(arrow_head((12, 3.5), -90, 5, 5), line([(12, 7), (12, 13.75)], 3, "flat"))
    return easel(arrow)


def fillet(g, r):
    """圆化凹角（指根、虎口），r 需小于要保留的指缝半宽。"""
    return g.buffer(r, quad_segs=QS, join_style="round").buffer(-r, quad_segs=QS, join_style="round")


@icon("barMove", "主栏状态", "移动（手形）")
def _():
    # 外轮廓参照开发者给定的手形：手指粗而紧、中指最高、小指最短；拇指指肚在左，
    # 下沿以凸弧扫到偏右的圆掌底，上沿与食指根之间为凹弧。实心 + 指缝镂空
    W, P = 3.0, 4.25
    xs = [7.5 + P * i for i in range(4)]
    px, xr = xs[0] - W / 2, xs[3] + W / 2
    R = (xr - px) / 2
    heel = U(box(px, 12, xr, 22.75 - R), circ(px + R, 22.75 - R, R)).intersection(box(0, 12, 24, 24))   # 掌底整段大圆弧，右侧直边
    sweep = U(circ(3.75, 13.25, 1.9), heel).convex_hull
    notch = circ(5.0, 8.5, 3.25)
    body = U(sweep, rr(px, 9.5, xr - px, 6, 1)).difference(notch)
    fingers = U(*[line([(x, 12), (x, t)], W) for x, t in zip(xs, (4.5, 2.25, 3.5, 6.75))])
    return fill(fillet(soften(U(fillet(body, 1.0), fingers), 0.4), 0.45), 23)


# ---------- 组件 ----------
@icon("compExplorer", "组件", "文件资源管理器")
def _():
    front = rr(2, 9.25, 20, 11.75, 2.5)
    back = U(rr(2, 3, 8.5, 6, 2), rr(2, 5.25, 20, 6, 2.5))
    return fill(U(knock(back, front), front))


@icon("compTaskManager", "组件", "任务管理器")
def _():
    pulse = line([(5, 13), (8.25, 13), (10.25, 8.25), (13.5, 16.75), (15.5, 11.25), (19, 11.25)], 1.75)
    return fill(rr(2, 3, 20, 18, 3).difference(pulse))


@icon("compControlPanel", "组件", "控制面板")
def _():
    parts = []
    for y, x in ((5, 8), (12, 16), (19, 10.5)):
        knob = circ(x, y, 3)
        parts.append(U(knock(line([(3.25, y), (20.75, y)]), knob), knob))
    return fill(U(*parts))


@icon("compShowDesktop", "组件", "显示桌面")
def _():
    arrow = U(line([(12, 5), (12, 10)], 2.5, "flat"), arrow_head((12, 13.75), 90, 4.25, 4.25))
    mon = rr(2, 2.5, 20, 14, 3)
    return fill(U(mon, screen_stand()).difference(arrow))


@icon("compLockScreen", "组件", "锁屏")
def _():
    shackle = line([(7.75, 11)] + arc_pts(12, 7.75, 4.25, 180, 360) + [(16.25, 11)])
    body = rr(4, 10, 16, 12, 3)
    hole = soften(U(circ(12, 15, 1.75), box(11.2, 15, 12.8, 18.5)), 0.3)
    return fill(U(knock(shackle, body, 0), body).difference(hole))


@icon("compEscape", "组件", "Esc")
def _():
    arrow = U(line([(16, 16), (10, 10)], 2.25, "round"), arrow_head((6.75, 6.75), -135, 5, 4.25))
    return fill(rr(2, 2, 20, 20, 4.5).difference(arrow))


@icon("compAltF4", "组件", "Alt+F4")
def _():
    win = rr(2, 3, 20, 18, 3).difference(box(0, 7.25, 24, 8.75))
    x = U(line([(9.25, 12.25), (14.75, 17.75)], 2.25), line([(14.75, 12.25), (9.25, 17.75)], 2.25))
    return fill(win.difference(x))


@icon("compRollCallRandom", "组件", "IslandCaller 随机点名")
def _():
    die = rr(13, 13, 9.25, 9.25, 2.25)
    pips = U(*[circ(13 + 9.25 * f, 13 + 9.25 * f, 0.95) for f in (0.26, 0.5, 0.74)])
    return fill(with_badge(one_person(), die.difference(pips)))


@icon("compRollCallSingle", "组件", "IslandCaller 抽取一人")
def _():
    target = U(circ(*BADGE_C, 5).difference(circ(*BADGE_C, 3.5)), circ(*BADGE_C, 2))
    return fill(with_badge(one_person(), target))


@icon("compRollCallQuick", "组件", "SecRandom 快速抽取")
def _():
    return fill(with_badge(one_person(), bolt()))


@icon("compRollCallQuick2", "组件", "SecRandom 2 快速抽取")
def _():
    return fill(with_badge(two_people(), bolt()))


@icon("compRollCallQuickCompat", "组件", "SecRandom 2 快速抽取（兼容）")
def _():
    disc = circ(*BADGE_C, 5)
    inner = scale(bolt(), 0.72, origin=(18, 17.5))
    return fill(with_badge(two_people(), disc.difference(inner)))


@icon("compNamePicker", "组件", "NamePicker 随机姓名")
def _():
    # 单人剪影 + 姓名牌角标（牌内镂空两行“名字”），与骰子/靶心/闪电角标区分
    tag = rr(12.25, 13.75, 10.5, 8.25, 2)
    lines = U(line([(15, 16.75), (20, 16.75)], 1.75), line([(15, 19.25), (18.25, 19.25)], 1.75))
    return fill(with_badge(one_person(), tag.difference(lines)))


def schedule():
    grid = rr(2, 2.5, 19, 18.5, 3).difference(box(0, 7, 24, 8.5))
    cells = U(*[rr(x, y, 3, 3, 0.75) for y in (10.75, 15.75) for x in (5, 10, 15)])
    return grid.difference(cells)


@icon("compClassSettings", "组件", "ClassIsland 设置")
def _():
    return fill(with_badge(schedule(), gear_badge()))


@icon("compClassProfile", "组件", "ClassIsland 档案编辑")
def _():
    return fill(with_badge(schedule(), pen_shape((13.25, 22.25), (21.75, 13.75), 3.2, 2.8)))


@icon("compClassSwap", "组件", "ClassIsland 快速换课")
def _():
    a = U(line([(13.25, 14.75), (18.5, 14.75)], 2.25, "flat"), arrow_head((22.75, 14.75), 0, 4.25, 3.25))
    b = U(line([(17.5, 20.75), (22.75, 20.75)], 2.25, "flat"), arrow_head((13.25, 20.75), 180, 4.25, 3.25))
    return fill(with_badge(schedule(), U(a, b)))


# ---------- 橡皮面板 ----------
def a_glyph():
    # 外形与字腔放在同一路径里，按 evenodd 挖空 A 的三角字腔
    return from_svg_paths(["M18.5 27L21.7 17H24.1L27.4 27H25.2L24.5 24.6H21.3L20.6 27H18.5Z"
                           "M21.85 22.65H23.95L22.9 19.1L21.85 22.65Z"], 24 / 28)


@icon("barAutoEraser", "橡皮面板", "自动粗细")
def _():
    a = a_glyph()
    b = a.bounds
    a = move(a, 23.5 - b[2], 23.5 - b[3])
    return eraser_with_badge(a)


# barEraserClearCanvas：v2 取消，橡皮面板「清空画布」恢复使用主栏垃圾桶 barClean。


@icon("barEraserSpeedSetting", "橡皮面板", "极速橡皮设置")
def _():
    return eraser_with_badge(gear_badge())


# ---------- 辅助 ----------
@icon("barCloseSmall", "辅助", "关闭")
def _():
    return U(line([(6.5, 6.5), (17.5, 17.5)]), line([(17.5, 6.5), (6.5, 17.5)]))


@icon("barInfo", "辅助", "信息")
def _():
    return circ(12, 12, 10).difference(U(circ(12, 7.25, 1.5), line([(12, 11), (12, 17)])))


@icon("barQuestion", "辅助", "问号")
def _():
    hook = line(arc_pts(12, 9.25, 2.75, 180, 405) + [(12, 12.75), (12, 13.5)])
    return circ(12, 12, 10).difference(U(hook, circ(12, 17.25, 1.5)))


@icon("barMore", "辅助", "展开 / 翻页箭头（0° 朝上）")
def _():
    return line([(3.5, 16), (12, 7.5), (20.5, 16)], 3)


@icon("barThicknessAdjust", "辅助", "小展开箭头（0° 朝上）")
def _():
    return line([(7.25, 14.25), (12, 9.5), (16.75, 14.25)])


@icon("barAdd", "辅助", "添加页")
def _():
    return U(line([(12, 4.5), (12, 19.5)]), line([(4.5, 12), (19.5, 12)]))


@icon("barMenu", "辅助", "白板菜单")
def _():
    return U(*[line([(4.5, y), (19.5, y)]) for y in (6.5, 12, 17.5)])


@icon("barMinimize", "辅助", "最小化白板")
def _():
    return line([(5.5, 12), (18.5, 12)])


@icon("barPalette", "辅助", "白板主题")
def _():
    body = circ(12, 12, 10).difference(circ(20.5, 18.5, 4.75))
    holes = U(circ(7.5, 10.5, 1.75), circ(10.75, 6.25, 1.75), circ(15.75, 7.25, 1.75), circ(8.5, 15.75, 1.75))
    return soften(body, 0.75).difference(holes)


# ---------- 输出 ----------
KEEP_ORIGINAL = ["barGeometry"]   # 恢复原版文件，不生成

if __name__ == "__main__":
    manifest = []
    for name in KEEP_ORIGINAL:
        manifest.append({"name": name, "group": "主栏", "label": "几何（恢复原版）", "original": True})
    for name, (group, label, g) in ICONS.items():
        (OUT / f"{name}.svg").write_text(to_svg(g), encoding="utf-8")
        manifest.append({"name": name, "group": group, "label": label})
    (OUT / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{len(ICONS)} icons")
