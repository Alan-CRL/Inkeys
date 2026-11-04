"""状态指示器图标设计源（复用 11-03 图标语言）。
用法: uv run --with shapely --with svgpathtools python status_icon_build.py <输出目录>"""
import math
import sys
from pathlib import Path

LIB = next(Path(__file__).resolve().parents[2].glob("**/11-03-bar-icon-redesign/research"))
sys.path.insert(0, str(LIB))
from icon_lib import U, arrow_head, box, circ, knock, line, poly, rotate, rr, scale, soften, move, to_svg  # noqa: E402

OUT = Path(sys.argv[1])
OUT.mkdir(parents=True, exist_ok=True)


def fill(g, size=23.0):
    b = g.bounds
    s = size / max(b[2] - b[0], b[3] - b[1])
    g = scale(g, s, origin=((b[0] + b[2]) / 2, (b[1] + b[3]) / 2))
    b = g.bounds
    return move(g, 12 - (b[0] + b[2]) / 2, 12 - (b[1] + b[3]) / 2)


def big_stand():
    return U(box(10.4, 17, 13.6, 21.5), rr(5.5, 20.5, 13, 3, 1.5))


def slideshow():
    # 投影幕 + 镂空上升柱状图：与“返回放映”（播放三角）区分。
    scr = rr(0.5, 0.75, 23, 17.25, 3.5)
    bars = U(rr(6.25, 9.75, 2.5, 4.5, 1.25), rr(10.75, 7.0, 2.5, 7.25, 1.25), rr(15.25, 4.25, 2.5, 10.0, 1.25))
    return U(scr, knock(big_stand(), scr, 0)).difference(bars)


def pin():
    # Inkeys2 大头针：圆角帽、收腰、宽托、针，整体 45° 倾斜。
    head = rr(7.0, 1.0, 10.0, 4.5, 1.75)
    neck = poly([(8.75, 5.0), (15.25, 5.0), (16.25, 11.25), (7.75, 11.25)])
    collar = rr(4.5, 10.75, 15.0, 3.5, 1.75)
    needle = line([(12, 14), (12, 22.75)], 2.5)
    g = U(head, soften(neck, 0.5), collar, needle)
    return fill(rotate(g, 45, (12, 12)))


def new_version():
    # 实心圆 + 镂空向上箭头（升级）。
    arrow = U(arrow_head((12, 5.25), -90, 5.25, 5.0), line([(12, 9.5), (12, 17.75)], 2.5))
    return circ(12, 12, 11.5).difference(arrow)


def star():
    pts = []
    for k in range(10):
        r = 11.75 if k % 2 == 0 else 5.25
        a = math.radians(-90 + 36 * k)
        pts.append((12 + r * math.cos(a), 12.75 + r * math.sin(a)))
    return fill(soften(poly(pts), 1.0))


def bell():
    dome = U(circ(12, 9.5, 7.5).intersection(box(4.5, 2, 19.5, 9.5)),
             poly([(4.5, 9.5), (19.5, 9.5), (20.25, 16.0), (22.75, 18.25), (1.25, 18.25), (3.75, 16.0)]))
    dome = U(soften(dome, 0.75), rr(1.25, 16.75, 21.5, 2.5, 1.25), circ(12, 1.75, 1.75))
    clapper = circ(12, 20.75, 2.75).intersection(box(9, 19.75, 15, 23.5))
    return fill(U(knock(dome, clapper), clapper))


def message():
    # 对话气泡 + 三点镂空（通用通知）。
    bubble = U(rr(0.5, 1.5, 23, 16.5, 4.5), soften(poly([(5.5, 16), (11.5, 16), (5.5, 22.75)]), 0.6))
    dots = U(circ(7, 9.75, 1.75), circ(12, 9.75, 1.75), circ(17, 9.75, 1.75))
    return bubble.difference(dots)


def bolt():
    return fill(soften(poly([(14.5, 0.5), (3.5, 13.75), (11.0, 13.75), (9.0, 23.5), (20.5, 9.75), (13.0, 9.75)]), 0.75))


ICONS = {
    "barStatusSlideshow": slideshow,
    "barStatusPin": pin,
    "barStatusNewVersion": new_version,
    "barStatusStar": star,
    "barStatusBell": bell,
    "barStatusInfo": message,
    "barStatusBolt": bolt,
}

if __name__ == "__main__":
    for name, fn in ICONS.items():
        (OUT / f"{name}.svg").write_text(to_svg(fn()), encoding="utf-8")
    print(f"{len(ICONS)} icons -> {OUT}")
