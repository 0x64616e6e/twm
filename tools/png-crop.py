#!/usr/bin/env python3
"""Crop (and optionally upscale) a test screenshot into a compressed PNG.

Dev tool for docs/shots; the tests themselves write uncompressed PNGs with no
dependencies. Standard library only.

    tools/png-crop.py IN.png OUT.png X Y W H [SCALE]
"""
import struct
import sys
import zlib


def read_png(path):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos, idat, width, height = 8, b"", 0, 0
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, ctype = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and ctype == 2, "expects 8-bit RGB (as the tests write)"
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 3 + 1
    rows = []
    for y in range(height):
        row = raw[y * stride:(y + 1) * stride]
        assert row[0] == 0, "expects unfiltered rows (as the tests write)"
        rows.append(row[1:])
    return width, height, rows


def write_png(path, width, height, rows):
    def chunk(kind, body):
        crc = zlib.crc32(kind + body) & 0xFFFFFFFF
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", crc)

    raw = b"".join(b"\x00" + r for r in rows)
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
                chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    if len(sys.argv) not in (7, 8):
        sys.exit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    x, y, w, h = (int(v) for v in sys.argv[3:7])
    scale = int(sys.argv[7]) if len(sys.argv) == 8 else 1
    _, _, rows = read_png(src)
    out = []
    for row in rows[y:y + h]:
        cut = row[x * 3:(x + w) * 3]
        wide = b"".join(cut[i:i + 3] * scale for i in range(0, len(cut), 3))
        out.extend([wide] * scale)
    write_png(dst, w * scale, h * scale, out)


if __name__ == "__main__":
    main()
