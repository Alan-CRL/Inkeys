"""只分析 native_probe 保存的 RGB PNG，用新像素辅助核验真实新笔迹。"""

from pathlib import Path
import struct
import zlib


def read_native_rgb(path):
    data = Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("Expected PNG")
    offset = 8
    compressed = bytearray()
    width = height = None
    while offset < len(data):
        length = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + length]
        if kind == b"IHDR":
            width, height, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", payload)
            if (depth, color, compression, filtering, interlace) != (8, 2, 0, 0, 0):
                raise ValueError("Expected native_probe noninterlaced RGB PNG")
        elif kind == b"IDAT":
            compressed.extend(payload)
        offset += length + 12
    raw = zlib.decompress(compressed)
    stride = width * 3 + 1
    if len(raw) != height * stride or any(raw[y * stride] for y in range(height)):
        raise ValueError("Expected native_probe filter-zero rows")
    return width, height, raw, stride


def new_red_pixels(before, after, rect):
    left = read_native_rgb(before)
    right = read_native_rgb(after)
    if left[:2] != right[:2]:
        raise ValueError("Screenshots changed dimensions")
    width, height, old, stride = left
    new = right[2]
    x0, y0, x1, y1 = rect
    if not (0 <= x0 < x1 <= width and 0 <= y0 < y1 <= height):
        raise ValueError("Crop must stay inside the screenshot")
    count = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            i = y * stride + 1 + x * 3
            r, g, b = new[i:i + 3]
            if r >= 180 and g < 100 and b < 100 and old[i:i + 3] != new[i:i + 3]:
                count += 1
    return dict(new_red_pixels=count, rect=rect, dimensions=[width, height])
