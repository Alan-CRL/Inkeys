from pathlib import Path
import struct
import zlib

root = Path('Build/StartupPreviewVisualQA')


def read(name):
    data = (root / name).read_bytes()
    offset = struct.unpack_from('<I', data, 10)[0]
    width, height = struct.unpack_from('<ii', data, 18)
    assert height < 0
    return width, -height, data[offset:]


def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)


def save(name, width, height, rgb):
    data = b''.join(b'\0' + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    (root / name).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
                             + chunk(b'IDAT', zlib.compress(data, 7)) + chunk(b'IEND', b''))


def put(destination, canvas_width, image, left, top, display_width=None):
    width, height, pixels = image
    dw = display_width or width
    dh = round(height * dw / width)
    for y in range(dh):
        sy = min(height - 1, y * height // dh)
        for x in range(dw):
            sx = min(width - 1, x * width // dw)
            i = (sy * width + sx) * 4
            blue, green, red, alpha = pixels[i:i + 4]
            target = ((top + y) * canvas_width + left + x) * 3
            background = 224 if (x // 18 + y // 18) % 2 else 239
            # 原生BMP是预乘BGRA；这里只合成检查底纹，不改变渲染结果的颜色/透明度。
            destination[target:target + 3] = bytes(min(255, c + round((255 - alpha) * background / 255))
                                                  for c in (red, green, blue))


width, height = 1002, 129
image = bytearray([239, 241, 243]) * (width * height)
for col, theme in enumerate([1, 2]):
    put(image, width, read(f'effect-1-theme{theme}-pos0-scale1.000000-ready.bmp'), 12 + col * 495, 24)
save('native-rainbow-review.png', width, height, image)
width, height = 1002, 774
image = bytearray([239, 241, 243]) * (width * height)
for row, time in enumerate([.2, .8, 1.6, 2.4, 3.0]):
    for col, theme in enumerate([1, 2]):
        put(image, width, read(f'effect-2-theme{theme}-pos0-scale1.000000-t{time:.6f}.bmp'),
            12 + col * 495, 10 + row * 153, 471)
save('native-particle-review.png', width, height, image)
print('Refreshed native rainbow and particle review images.')
