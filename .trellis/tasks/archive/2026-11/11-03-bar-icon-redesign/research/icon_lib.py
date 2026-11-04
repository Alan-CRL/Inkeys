"""Inkeys Bar 图标几何工具：统一线宽、间隙与 SVG 输出。"""
import math

from shapely import affinity
from shapely.geometry import LineString, MultiPolygon, Point, Polygon, box
from shapely.geometry.polygon import orient
from shapely.ops import unary_union
from svgpathtools import parse_path

TAG = "rgba(10,0,7,0)"
LINE = 2.5   # 统一线宽
GAP = 1.5    # 统一镂空/分离间隙
QS = 12      # 圆弧细分


def rr(x, y, w, h, r):
    return box(x + r, y + r, x + w - r, y + h - r).buffer(r, quad_segs=QS)


def circ(cx, cy, r):
    return Point(cx, cy).buffer(r, quad_segs=QS * 2)


def line(pts, w=LINE, cap="round"):
    return LineString(pts).buffer(w / 2, quad_segs=QS, cap_style=cap, join_style="round")


def poly(pts):
    return Polygon(pts)


def soften(g, r=0.5):
    """圆化凸角（尖三角、箭头）。"""
    return g.buffer(-r, quad_segs=QS, join_style="round").buffer(r, quad_segs=QS, join_style="round")


def knock(base, cutter, gap=GAP):
    """主体让出 cutter 外扩 gap 的区域，保证统一分离间隙。"""
    return base.difference(cutter.buffer(gap, quad_segs=QS))


def U(*gs):
    return unary_union(list(gs))


def arc_pts(cx, cy, r, a0, a1, n=24):
    return [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
             cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]


def arrow_head(tip, direction_deg, length=4.5, half=4.25, r=0.45):
    """实心箭头三角，tip 为尖端，direction 为箭头指向（度，屏幕坐标）。"""
    a = math.radians(direction_deg)
    dx, dy = math.cos(a), math.sin(a)
    bx, by = tip[0] - dx * length, tip[1] - dy * length
    nx, ny = -dy, dx
    return soften(poly([tip, (bx + nx * half, by + ny * half), (bx - nx * half, by - ny * half)]), r)


def from_svg_paths(ds, scale=1.0, dx=0.0, dy=0.0):
    """把现有 SVG 路径（evenodd 语义）转换为几何，用于复用基准图标外形。"""
    acc = Polygon()
    for d in ds:
        sub = Polygon()
        for sp in parse_path(d).continuous_subpaths():
            n = max(16, int(sp.length() * 6))
            pts = [sp.point(i / n) for i in range(n)]
            ring = Polygon([(p.real, p.imag) for p in pts]).buffer(0)
            sub = sub.symmetric_difference(ring)
        acc = acc.union(sub)
    return affinity.translate(affinity.scale(acc, scale, scale, origin=(0, 0)), dx, dy)


def rotate(g, deg, origin):
    return affinity.rotate(g, deg, origin=origin)


def move(g, dx, dy):
    return affinity.translate(g, dx, dy)


def scale(g, s, origin=(12, 12)):
    return affinity.scale(g, s, s, origin=origin)


def _ring(coords):
    pts = [f"{x:.2f} {y:.2f}" for x, y in list(coords)[:-1]]
    return "M" + pts[0] + "L" + " ".join(pts[1:]) + "Z"


def to_svg(g):
    g = g.buffer(0).simplify(0.01, preserve_topology=True)
    polys = list(g.geoms) if isinstance(g, MultiPolygon) else [g]
    d = ""
    for p in polys:
        if p.is_empty:
            continue
        p = orient(p)
        d += _ring(p.exterior.coords)
        for hole in p.interiors:
            d += _ring(hole.coords)
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none">'
            f'<path fill-rule="evenodd" fill="{TAG}" d="{d}"/></svg>\n')
