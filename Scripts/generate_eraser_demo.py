"""Bake Setting's handwritten eraser demonstration; no GUI or image API required.

Run with Python 3 + numpy. Assets contain straight-alpha ink and two erase-time
maps; playback never constructs pen paths or runs the speed-eraser controller.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import struct
import zlib

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
WIDTH, HEIGHT = 768, 432
FPS, FRAME_COUNT = 30, 240
ERASE_BEGIN, ERASE_END = 1.35, 4.45
INK = (28, 35, 42)
BLUE = (30, 112, 179)
ORANGE = (202, 112, 29)


def smooth(value):
    value = np.clip(value, 0.0, 1.0)
    return value * value * (3.0 - 2.0 * value)


def write_png(path, pixels):
    """Small deterministic PNG writer; avoid a build dependency on Pillow."""
    pixels = np.ascontiguousarray(pixels, dtype=np.uint8)
    height, width, channels = pixels.shape
    assert channels == 4

    def chunk(tag, value):
        return struct.pack(">I", len(value)) + tag + value + struct.pack(
            ">I", zlib.crc32(tag + value) & 0xFFFFFFFF
        )

    rows = b"".join(b"\0" + row.tobytes() for row in pixels)
    data = b"\x89PNG\r\n\x1a\n"
    data += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b"")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def capsule_distance(x, y, begin, end, r1, r2):
    """Draw3 GetInkDist_Convex / sdUnevenCapsule_Vertical geometry."""
    dx, dy = end[0] - begin[0], end[1] - begin[1]
    length = math.hypot(dx, dy)
    if length < 1e-5 or abs(r1 - r2) >= length:
        center = end if r2 > r1 else begin
        return np.hypot(x - center[0], y - center[1]) - max(r1, r2)
    px, py = x - begin[0], y - begin[1]
    local_x = np.abs((-dy * px + dx * py) / length)
    local_y = (dx * px + dy * py) / length
    b = (r1 - r2) / length
    a = math.sqrt(max(0.0, 1.0 - b * b))
    k = -b * local_x + a * local_y
    return np.where(
        k < 0.0,
        np.hypot(local_x, local_y) - r1,
        np.where(
            k > a * length,
            np.hypot(local_x, local_y - length) - r2,
            a * local_x + b * local_y - r1,
        ),
    )


def capsule_coverage(begin, end, r1, r2):
    extent = max(r1, r2) + 3.0
    x0 = max(0, math.floor(min(begin[0], end[0]) - extent))
    x1 = min(WIDTH, math.ceil(max(begin[0], end[0]) + extent))
    y0 = max(0, math.floor(min(begin[1], end[1]) - extent))
    y1 = min(HEIGHT, math.ceil(max(begin[1], end[1]) + extent))
    if x0 >= x1 or y0 >= y1:
        return (slice(0, 0), slice(0, 0)), np.empty((0, 0), np.float32)
    y, x = np.mgrid[y0:y1, x0:x1].astype(np.float32)
    x += 0.5
    y += 0.5
    distance = capsule_distance(x, y, begin, end, r1, r2)
    # 与墨迹 shader 相同：1.5 * fwidth 距离，smoothstep 抗锯齿。
    derivative = np.abs(capsule_distance(x + 1.0, y, begin, end, r1, r2) - distance)
    derivative += np.abs(capsule_distance(x, y + 1.0, begin, end, r1, r2) - distance)
    aa = np.maximum(1.5 * derivative, 1e-5)
    coverage = 1.0 - smooth((distance + aa * 0.5) / aa)
    return (slice(y0, y1), slice(x0, x1)), coverage


def sample_curve(points, samples=24):
    points = np.asarray(points, np.float64)
    if len(points) == 2:
        return np.linspace(points[0], points[1], samples)
    t = np.linspace(0.0, 1.0, samples)[:, None]
    if len(points) == 3:
        return (1.0 - t) ** 2 * points[0] + 2 * (1.0 - t) * t * points[1] + t**2 * points[2]
    return (
        (1.0 - t) ** 3 * points[0]
        + 3 * (1.0 - t) ** 2 * t * points[1]
        + 3 * (1.0 - t) * t**2 * points[2]
        + t**3 * points[3]
    )


class InkCanvas:
    def __init__(self):
        self.premultiplied = np.zeros((HEIGHT, WIDTH, 4), np.float32)

    def stroke(self, points, color=INK, width=3.2, phase=0.0):
        points = np.asarray(points)
        coverage = np.zeros((HEIGHT, WIDTH), np.float32)
        # 轻微笔压变化和倾斜来自手写路径本身，不使用排版字体或随机噪声。
        pressure = 1.0 + 0.075 * np.sin(np.linspace(phase, phase + 2.8, len(points)))
        pressure *= np.minimum(1.0, np.linspace(0.86, 1.6, len(points)))
        for index in range(len(points) - 1):
            rect, segment = capsule_coverage(
                points[index], points[index + 1],
                width * 0.5 * pressure[index], width * 0.5 * pressure[index + 1],
            )
            coverage[rect] = np.maximum(coverage[rect], segment)
        alpha = coverage[..., None]
        rgb = np.asarray(color, np.float32) / 255.0
        self.premultiplied[..., :3] = rgb * alpha + self.premultiplied[..., :3] * (1.0 - alpha)
        self.premultiplied[..., 3:] = alpha + self.premultiplied[..., 3:] * (1.0 - alpha)

    def curve(self, points, color=INK, width=3.2, phase=0.0):
        self.stroke(sample_curve(points), color, width, phase)

    def rgba(self):
        image = self.premultiplied.copy()
        image[..., :3] /= np.maximum(image[..., 3:], 1e-8)
        return np.uint8(np.clip(np.rint(image * 255.0), 0, 255))


# 每个字由独立手写笔画构成；控制点采用字框内的相对坐标。
GLYPHS = {
    "C": [[(.88, .16), (.10, -.12), (-.12, .85), (.40, .96)],
          [(.40, .96), (.64, 1.00), (.84, .88)]],
    "H": [[(.16, .04), (.10, .48), (.05, .99)],
          [(.91, .01), (.87, .48), (.82, 1.00)],
          [(.12, .52), (.44, .46), (.87, .47)]],
    "O": [[(.51, .03), (-.01, -.06), (-.13, .89), (.35, .99)],
          [(.35, .99), (.92, 1.08), (1.09, .14), (.51, .03)]],
    "A": [[(.03, 1.00), (.49, -.05), (.62, .03)],
          [(.62, .03), (.75, .50), (.91, .99)],
          [(.23, .59), (.56, .56), (.78, .56)]],
    "B": [[(.18, .03), (.12, .48), (.06, .99)],
          [(.18, .03), (.98, -.14), (.99, .49), (.14, .48)],
          [(.14, .48), (1.12, .32), (1.13, 1.12), (.06, .99)]],
    "2": [[(.09, .16), (.50, -.18), (1.02, .04), (.74, .42)],
          [(.74, .42), (.47, .69), (.02, 1.01)],
          [(.02, 1.01), (.45, .92), (.93, .96)]],
    "3": [[(.06, .09), (.87, -.14), (1.07, .41), (.37, .47)],
          [(.37, .47), (1.11, .34), (1.06, 1.15), (.05, .94)]],
    "6": [[(.82, .03), (.13, .11), (-.09, .80), (.21, .97)],
          [(.21, .97), (.74, 1.22), (1.06, .62), (.62, .51)],
          [(.62, .51), (.37, .42), (.10, .58)]],
    "7": [[(.06, .08), (.43, .02), (.94, .08)],
          [(.94, .08), (.55, .51), (.35, 1.00)]],
    "8": [[(.45, .02), (-.10, .02), (.00, .41), (.48, .52)],
          [(.48, .52), (1.14, .66), (.86, 1.10), (.28, .98)],
          [(.28, .98), (-.20, .88), (.00, .59), (.48, .52)],
          [(.48, .52), (1.12, .32), (.98, -.06), (.45, .02)]],
    "?": [[(.08, .20), (.48, -.19), (1.09, .02), (.78, .43)],
          [(.78, .43), (.51, .61), (.43, .72)],
          [(.40, .96), (.42, .99)]],
    "-": [[(.02, .53), (.48, .49), (.96, .48)]],
}


def glyph(canvas, char, x, y, width, height, color=INK, pen=3.2):
    for index, points in enumerate(GLYPHS[char]):
        path = [(x + px * width, y + py * height) for px, py in points]
        canvas.curve(path, color, pen, phase=index * 0.65 + x * 0.01)


def formula(canvas, text, x, y, height=35.0, color=INK, pen=3.0):
    cursor = x
    for index, char in enumerate(text):
        subscript = char.isdigit()
        h = height * (0.53 if subscript else 1.0)
        w = h * (0.55 if subscript else 0.64)
        glyph(canvas, char, cursor, y + (height * 0.57 if subscript else 0.0),
              w, h, color, pen * (0.80 if subscript else 1.0))
        cursor += w + (5.5 if subscript else 6.0)
    return cursor


def benzene(canvas, cx, cy, radius, color=INK, pen=3.25):
    vertices = [
        (cx + radius * math.cos(math.radians(-90 + i * 60)),
         cy + radius * math.sin(math.radians(-90 + i * 60)))
        for i in range(6)
    ]
    for index in range(6):
        a, b = vertices[index], vertices[(index + 1) % 6]
        midpoint = ((a[0] + b[0]) * 0.5 + .55 * math.sin(index),
                    (a[1] + b[1]) * 0.5 + .48 * math.cos(index))
        canvas.curve([a, midpoint, b], color, pen, phase=index * .44)
    for index in (0, 2, 4):
        a = np.asarray(vertices[index])
        b = np.asarray(vertices[(index + 1) % 6])
        center = np.array((cx, cy))
        first = center + ((a * .86 + b * .14) - center) * .80
        second = center + ((a * .14 + b * .86) - center) * .80
        canvas.curve([first, (first + second) * .5 + (.15, -.4), second], color, pen * .9)
    return vertices[1]


def structure(canvas, cx, cy, radius, side_chain, height=35.0):
    right_vertex = benzene(canvas, cx, cy, radius)
    connection_end = (right_vertex[0] + 31.0, right_vertex[1] - 3.2)
    canvas.curve([right_vertex, (right_vertex[0] + 15.0, right_vertex[1] - 1.0), connection_end])
    formula(canvas, side_chain, connection_end[0] + 7.0, connection_end[1] - height * .49, height)


def arrow(canvas, x1, x2, y, color=BLUE):
    canvas.curve([(x1, y + 1.8), ((x1 + x2) * .5, y - .8), (x2, y)], color, 3.2)
    canvas.curve([(x2 - 12, y - 9), (x2, y), (x2 - 12, y + 8)], color, 3.0)


def reaction(canvas):
    structure(canvas, 131, 139, 47, "CH2OH", 33)
    structure(canvas, 496, 139, 47, "CHO", 35)
    arrow(canvas, 337, 410, 138)
    # 脱去 2H 的化学计量系数保持基线，分子式中的数字仍按下标绘制。
    glyph(canvas, "-", 343, 79, 11, 25, BLUE, 2.7)
    glyph(canvas, "2", 359, 79, 13, 25, BLUE, 2.7)
    glyph(canvas, "H", 380, 79, 16, 25, BLUE, 2.7)
    glyph(canvas, "A", 119, 213, 23, 30, BLUE, 2.5)
    glyph(canvas, "B", 484, 213, 22, 30, BLUE, 2.5)


def make_scene(scene):
    canvas = InkCanvas()
    if scene == 0:
        reaction(canvas)
        formula(canvas, "C7H8O", 99, 285, 43, INK, 3.3)
        # 数字 8 置于邻字之间：16px 可完整擦除，32px 会触及 H/O。
        glyph(canvas, "C", 465, 280, 28, 46)
        glyph(canvas, "7", 495, 307, 13, 22, pen=2.65)
        glyph(canvas, "H", 510, 280, 30, 46)
        glyph(canvas, "8", 542, 307, 13, 22, pen=2.55)
        glyph(canvas, "O", 559, 280, 30, 46)
        glyph(canvas, "?", 622, 289, 19, 31, ORANGE, 2.8)
    else:
        reaction(canvas)
        structure(canvas, 382, 326, 45, "CH2OH", 34)
        glyph(canvas, "?", 292, 311, 20, 32, ORANGE, 2.8)
        canvas.curve([(352, 390), (424, 394), (519, 389), (566, 390)], ORANGE, 2.5)
    return canvas.rgba()


def contact_pose(scene, progress):
    progress = float(np.clip(progress, 0.0, 1.0))
    if scene == 0:
        distance = float(smooth(progress))
        return 548.2 + .60 * math.sin(math.pi * distance), 308.0 + 17.7 * distance, 16.0
    distance = float(smooth(progress))
    x = 300.0 + 280.0 * distance
    y = 339.0 - 7.0 * math.sin(math.pi * distance)
    diameter = 16.0 + 144.0 * float(smooth(progress / .39))
    return x, y, diameter


def frame_at(scene, index):
    time = index / FPS
    progress = float(np.clip((time - ERASE_BEGIN) / (ERASE_END - ERASE_BEGIN), 0.0, 1.0))
    x, y, speed = contact_pose(scene, progress)
    # 悬停到接触之间连续衔接；抬起后只淡出，不从未擦除的位置跳走。
    if time < ERASE_BEGIN:
        approach = 1.0 - float(smooth((time - .50) / (ERASE_BEGIN - .50)))
        y -= 26.0 * approach
        x -= 10.0 * approach
        speed += 16.0 * approach
    cursor_alpha = float(smooth((time - .45) / .40)) * (1.0 - float(smooth((time - 4.80) / .45)))
    opacity = float(smooth(time / .40)) * (1.0 - float(smooth((time - 7.55) / .45)))
    return x, y, 32.0, speed, cursor_alpha, progress, opacity


def bake_mask(scene, speed):
    partial_time = np.ones((HEIGHT, WIDTH), np.float32)
    complete_time = np.ones((HEIGHT, WIDTH), np.float32)
    coverage = np.zeros((HEIGHT, WIDTH), np.float32)
    # 预计算采用高于播放帧率的采样；扫过段是连续变半径胶囊，不能只画离散圆点。
    count = 384
    previous = contact_pose(scene, 0.0)
    for index in range(1, count + 1):
        progress = index / count
        current = contact_pose(scene, progress)
        r1 = previous[2] * .5 if speed else 16.0
        r2 = current[2] * .5 if speed else 16.0
        rect, segment = capsule_coverage(previous[:2], current[:2], r1, r2)
        old = coverage[rect]
        newly_touched = (segment > (1.0 / 255.0)) & (old <= (1.0 / 255.0))
        partial_time[rect] = np.where(newly_touched, progress, partial_time[rect])
        increased = segment > old + (1.0 / 65535.0)
        complete_time[rect] = np.where(increased, progress, complete_time[rect])
        # 同一擦除笔画按几何并集累积，重叠采样不重复削薄抗锯齿边缘。
        coverage[rect] = np.maximum(old, segment)
        previous = current
    mask = np.zeros((HEIGHT, WIDTH, 4), np.uint8)
    mask[..., 0] = np.uint8(np.clip(np.rint(partial_time * 255.0), 1, 255))
    mask[..., 1] = np.maximum(mask[..., 0], np.uint8(np.rint(complete_time * 255.0)))
    mask[..., 2] = np.uint8(np.rint(coverage * 255.0))
    mask[..., 3] = 255
    return mask


def paint(ink, mask, progress, dark=False):
    background = np.array((18, 59, 50) if dark else (247, 248, 250), np.float32)
    rgb = ink[..., :3].astype(np.float32)
    if dark:
        is_blue = (rgb[..., 2] > rgb[..., 0] + 30)
        is_orange = (rgb[..., 0] > rgb[..., 2] + 60)
        rgb[:] = (234, 242, 239)
        rgb[is_blue] = (111, 200, 236)
        rgb[is_orange] = (242, 192, 104)
    begin = mask[..., 0] / 255.0
    end = mask[..., 1] / 255.0
    erased = np.clip((progress - begin) / np.maximum(end - begin, 1.0 / 255.0), 0.0, 1.0)
    # 最终静帧必须使用完整遮罩，包含落在最后一个时间量化桶的边缘。
    if progress >= 1.0:
        erased[:] = 1.0
    erased *= mask[..., 2] / 255.0
    alpha = ink[..., 3:] / 255.0 * (1.0 - erased[..., None])
    pixels = np.empty((HEIGHT, WIDTH, 4), np.uint8)
    pixels[..., :3] = np.uint8(np.rint(rgb * alpha + background * (1.0 - alpha)))
    pixels[..., 3] = 255
    return pixels


def erased_fraction(ink, mask, rect):
    x0, y0, x1, y1 = rect
    alpha = ink[y0:y1, x0:x1, 3].astype(np.float64)
    erased = mask[y0:y1, x0:x1, 2].astype(np.float64) / 255.0
    return float(np.sum(alpha * erased) / max(np.sum(alpha), 1e-8))


def validate(scenes):
    checks = {}
    fine_ink, fixed, speed = scenes[0]
    checks["fine_target_speed_erased"] = erased_fraction(fine_ink, speed, (541, 304, 557, 333))
    checks["fine_neighbor_speed_erased"] = erased_fraction(fine_ink, speed, (558, 280, 594, 330))
    checks["fine_neighbor_fixed_erased"] = erased_fraction(fine_ink, fixed, (558, 280, 594, 330))
    broad_ink, fixed, speed = scenes[1]
    checks["sweep_candidate_speed_erased"] = erased_fraction(broad_ink, speed, (320, 270, 610, 405))
    checks["sweep_candidate_fixed_erased"] = erased_fraction(broad_ink, fixed, (320, 270, 610, 405))
    checks["sweep_context_speed_erased"] = erased_fraction(broad_ink, speed, (0, 0, WIDTH, 250))
    assert checks["fine_target_speed_erased"] > .995, checks
    assert checks["fine_neighbor_speed_erased"] < .01, checks
    assert checks["fine_neighbor_fixed_erased"] > .10, checks
    assert checks["sweep_candidate_speed_erased"] > .995, checks
    assert checks["sweep_candidate_fixed_erased"] < .65, checks
    assert checks["sweep_context_speed_erased"] < .001, checks
    for scene, (ink, fixed, speed) in enumerate(scenes):
        for mask in (fixed, speed):
            assert np.all(mask[..., 0] <= mask[..., 1])
            assert np.all(mask[..., 3] == 255)
        frames = np.asarray([frame_at(scene, i) for i in range(FRAME_COUNT)])
        assert np.isfinite(frames).all()
        assert np.all((frames[:, 3] >= 16.0) & (frames[:, 3] <= 160.0))
        assert np.all(np.diff(frames[:, 5]) >= 0.0)
        assert frames[0, 6] == 0.0 and frames[-1, 6] < .02
    return checks


def emit_header(path):
    lines = [
        "#pragma once", "", "#include <array>", "",
        "// 由 Scripts/generate_eraser_demo.py 生成；运行时只播放，不计算笔速或擦除路径。",
        "namespace Inkeys::UI::Setting::Design::EraserDemoData {", "",
        "struct Frame {", "    float x;", "    float y;", "    float fixedDiameter;",
        "    float speedDiameter;", "    float cursorAlpha;", "    float eraseProgress;",
        "    float opacity;", "};", "",
        f"inline constexpr int FramesPerScene = {FRAME_COUNT};",
        f"inline constexpr int FramesPerSecond = {FPS};", "",
        "inline constexpr std::array<std::array<Frame, FramesPerScene>, 2> Frames = {{",
    ]
    for scene in range(2):
        lines.append("    {{")
        for index in range(FRAME_COUNT):
            values = ", ".join(f"{value:.6f}F" for value in frame_at(scene, index))
            lines.append("        { " + values + " },")
        lines.append("    }},")
    lines += ["}};", "", "} // namespace Inkeys::UI::Setting::Design::EraserDemoData", ""]
    path.write_bytes(("\ufeff" + "\r\n".join(lines)).encode("utf-8"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preview-dir", type=Path, help="Optional static image review output")
    args = parser.parse_args()
    scenes = []
    for scene in range(2):
        ink = make_scene(scene)
        scenes.append((ink, bake_mask(scene, False), bake_mask(scene, True)))
    checks = validate(scenes)
    for name, data in zip(("Fine", "Sweep"), scenes):
        write_png(ROOT / f"Inkeys/src/setting/EraserDemo{name}.png", np.concatenate(data, axis=1))
    emit_header(ROOT / "Inkeys/Inkeys/UI/Setting/Setting.EraserDemoData.g.h")
    if args.preview_dir:
        for scene, (ink, fixed, speed) in enumerate(scenes):
            for dark in (False, True):
                rows = []
                for progress in (0.0, .45, 1.0):
                    rows.append(np.concatenate((paint(ink, fixed, progress, dark), paint(ink, speed, progress, dark)), axis=1))
                write_png(args.preview_dir / f"scene-{scene}-{'dark' if dark else 'light'}.png", np.concatenate(rows, axis=0))
    print(json.dumps(checks, indent=2))


if __name__ == "__main__":
    main()
