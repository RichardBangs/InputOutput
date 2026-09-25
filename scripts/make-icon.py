"""Generate the app's simple geometric monitor icon, without graphics dependencies."""
import pathlib
import struct

images = []
for size in (16, 32, 48, 64, 128, 256):
    pixels = bytearray()
    for y in range(size - 1, -1, -1):
        for x in range(size):
            u, v = (x + .5) / size, (y + .5) / size
            inside = .04 <= u <= .96 and .04 <= v <= .96
            corner_x = max(.19 - u, 0, u - .81)
            corner_y = max(.19 - v, 0, v - .81)
            inside = inside and corner_x**2 + corner_y**2 <= .15**2
            screen = .20 <= u <= .80 and .22 <= v <= .66
            cutout = .26 <= u <= .74 and .28 <= v <= .59
            stand = (.46 <= u <= .54 and .64 <= v <= .77) or (.34 <= u <= .66 and .76 <= v <= .81)
            glyph = (screen and not cutout) or stand
            pixels.extend((255, 255, 255, 255) if glyph else (198, 99, 29, 255) if inside else (0, 0, 0, 0))
    mask = bytes(((size + 31) // 32) * 4 * size)
    bitmap = struct.pack('<IiiHHIIiiII', 40, size, size * 2, 1, 32, 0, len(pixels), 0, 0, 0, 0) + pixels + mask
    images.append((size, bitmap))
offset = 6 + 16 * len(images)
header = bytearray(struct.pack('<HHH', 0, 1, len(images)))
for size, bitmap in images:
    header.extend(struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0, 1, 32, len(bitmap), offset))
    offset += len(bitmap)
path = pathlib.Path(__file__).resolve().parents[1] / 'assets' / 'app.ico'
path.write_bytes(header + b''.join(bitmap for _, bitmap in images))
