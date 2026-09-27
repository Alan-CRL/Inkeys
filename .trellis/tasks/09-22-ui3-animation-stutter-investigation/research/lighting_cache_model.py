"""只验证源码中的分支/调用量，不调用 D2D，也不是性能或故障现场复现。"""

from collections import deque
import csv
import math
from pathlib import Path
import struct


def f32(value):
    return struct.unpack("f", struct.pack("f", value))[0]


def quarter(value):
    # 当前研究输入全为非负数；对应 C++ lround 的半入方向。
    return math.floor(float(value) * 4.0 + 0.5)


def mask_key(radius, stroke, zoom):
    return (quarter(radius), quarter(radius), max(1, quarter(stroke)),
            max(1, quarter(f32(zoom))))


def slice_count(rect, radius, stroke, zoom):
    """Rendering.cpp:1946-2014，忽略 exact 分支的同构标量模型。"""
    left, top, right, bottom = rect
    key = mask_key(radius, stroke, zoom)
    cached_radius = f32(key[0] / 4.0)
    padding = float(math.ceil(key[3] / 4.0 * 3.0 + key[2] / 4.0 * 0.5 + 1.0))
    mid_left = min(left + radius, (left + right) * 0.5)
    mid_right = max(right - radius, mid_left)
    mid_top = min(top + radius, (top + bottom) * 0.5)
    mid_bottom = max(bottom - radius, mid_top)
    if abs(radius - cached_radius) <= 0.001:
        xs = [left - padding, mid_left, mid_right, right + padding]
        ys = [top - padding, mid_top, mid_bottom, bottom + padding]
    else:
        xs = [left - padding, left, mid_left, mid_right, right, right + padding]
        ys = [top - padding, top, mid_top, mid_bottom, bottom, bottom + padding]
    return sum(b > a for a, b in zip(xs, xs[1:])) * sum(
        b > a for a, b in zip(ys, ys[1:]))


def intersects(rect, cursor, stroke, zoom):
    """Rendering.cpp:2244-2270，圆角外扩与第三光 ellipse 测试。"""
    left, top, right, bottom = rect
    outset = stroke + 6.0 * zoom
    nearest_x = min(max(cursor[0], left - outset), right + outset)
    nearest_y = min(max(cursor[1], top - outset), bottom + outset)
    radius = 240.0 * zoom
    dx = (cursor[0] - nearest_x) / radius
    dy = (cursor[1] - nearest_y) / radius
    return dx * dx + dy * dy <= 1.0


# Initialization.cpp:364-484 的 11 个普通色块；RenderLoop.cpp:3102-3419 的位置。
shapes_dip = [(5.0 + 35.0 * (i // 2), 5.0 if i % 2 == 0 else 40.0, 30.0, 30.0)
              for i in range(11)]
# RenderLoop.cpp:3586-3613、3626-3652：4 个启用笔型按钮。
shapes_dip += [(250.0, y, 115.0, 30.0) for y in (40.0, 75.0, 110.0, 145.0)]

for zoom in (1.0, 1.3):
    z = f32(zoom)
    shapes = [tuple(f32(v * z) for v in (x, y, x + w, y + h))
              for x, y, w, h in shapes_dip]
    radius, stroke = f32(4.0 * z), f32(z)
    center = (185.0 * zoom, 92.5 * zoom)
    illuminated = [r for r in shapes if intersects(r, center, stroke, zoom)]
    total = sum(slice_count(r, radius, stroke, zoom) for r in illuminated)
    keys = {mask_key(radius, stroke, zoom) for _ in illuminated}
    assert len(illuminated) == 15
    assert len(keys) == 1
    assert total == (135 if zoom == 1.0 else 375)
    assert not any(intersects(r, (-500.0 * zoom, -500.0 * zoom), stroke, zoom)
                   for r in shapes)
    print(f"zoom={zoom}: illuminated=15, unique_parent_mask_keys={len(keys)}, "
          f"FillOpacityMask_calls={total}, far_cursor_calls=0")


def exact_transform_gate(matrix):
    # Rendering.cpp:1761-1764；当前检查连纯平移也拒绝。
    return tuple(matrix) == (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)


assert exact_transform_gate((1, 0, 0, 1, 0, 0))
assert not exact_transform_gate((1, 0, 0, 1, 1, 0))
print("exact_transform_gate: identity=True, integer_translation=False")


def fifo_frame_misses(working_set, capacity=24, frame_count=5):
    # 先填充历史 key；比较当前工作集小于容量与大于容量的区别。
    cache = deque(range(100, 100 + capacity))
    misses = []
    for _ in range(frame_count):
        frame_misses = 0
        for key in working_set:
            if key in cache:
                continue
            frame_misses += 1
            if len(cache) >= capacity:
                cache.popleft()
            cache.append(key)
        misses.append(frame_misses)
    return misses


assert fifo_frame_misses(range(6)) == [6, 0, 0, 0, 0]
assert fifo_frame_misses(range(25)) == [25, 25, 25, 25, 25]
print("FIFO_24: 6_stable_keys_misses=" + str(fifo_frame_misses(range(6)))
      + ", 25_stable_keys_misses=" + str(fifo_frame_misses(range(25))))


# 独立动画调查者生成的真实公式轨迹；本段只接下游 FLOAT 转换和 mask key。
trace_path = Path(__file__).with_name("input-animation-ft-trace.csv")
if trace_path.exists():
    with trace_path.open(encoding="utf-8", newline="") as trace_file:
        trace = list(csv.DictReader(trace_file))
    for zoom in (1.0, 1.3):
        z = f32(zoom)
        stable = mask_key(f32(4.0 * z), z, zoom)
        for column in ("ft_repeated", "ft_single"):
            keys = []
            for row in trace:
                if float(row["parent_progress"]) >= 1.0:
                    continue
                scale = float(row["panel_scale"])
                inverse_scale = f32(1.0 / scale)
                radius = f32(f32(4.0 * scale * z) * inverse_scale)
                stroke = f32(f32(float(row[column]) * z) * inverse_scale)
                keys.append(mask_key(radius, stroke, zoom))
            cache = deque([stable])
            new_per_cycle = []
            for _ in range(2):
                new = 0
                for key in keys:
                    if key in cache:
                        continue
                    if len(cache) >= 24:
                        cache.popleft()
                    cache.append(key)
                    new += 1
                new_per_cycle.append(new)
            if column == "ft_single":
                assert set(keys) == {stable}
                assert new_per_cycle == [0, 0]
            else:
                assert new_per_cycle == ([6, 0] if zoom == 1.0 else [7, 0])
            print(f"close_formula zoom={zoom}, {column}: "
                  f"stroke_quarters={sorted({key[2] for key in keys})}, "
                  f"new_parent_masks_first/repeat={new_per_cycle}")
else:
    print("close_formula not run: input-animation-ft-trace.csv unavailable")
