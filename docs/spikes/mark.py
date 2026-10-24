#!/usr/bin/env python3
# MRK-2 spike: what the in-pixel mark can carry and what survives a
# re-encode. Run: python3 docs/spikes/mark.py [out.png] (needs numpy, PIL).
import io, sys, zlib, random, math
import numpy as np
from PIL import Image, ImageDraw, ImageFont

FONT = "/usr/share/fonts/truetype/ibm-plex/IBMPlexMono-Bold.ttf"
GROUND = (0x0E, 0x0F, 0x0C)
SHADES = [(0x2A, 0x2C, 0x25), (0x5B, 0x5B, 0x57), (0xBD, 0xBB, 0xB5), (0xFF, 0xB0, 0x00)]  # dark dim bright accent

def payloads():
    import os, re
    home = os.path.expanduser("~/.twmrc")
    texts = {}
    try:
        texts["owner .twmrc"] = open(home).read()
    except OSError:
        pass
    # the built-in defaults, as shipped
    src = open("/home/dann/code/twm/src/settings.c").read()
    m = re.search(r'config_default_twmrc\[\] =\s*(.*?);\n', src, re.S)
    if m:
        body = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1)))
        texts["defaults"] = body.encode().decode("unicode_escape")
    look = "\n".join(l for l in texts.get("defaults", "").splitlines()
                     if l.split()[:1] and l.split()[0] in ("Theme","Accent","Font","TitleHeight","BorderWidth",
                        "TitleHighlight","SqueezeTitle","CloseBox","RootWeave","RootWordmark","RootCard",
                        "IconManager","IconsOnRoot","Effects"))
    texts["appearance keys only (SHR-2)"] = look + "\n"
    for k, t in texts.items():
        raw = t.encode()
        z = zlib.compress(raw, 9)
        print(f"  {k:32s} {len(raw):5d} B raw, {len(z):5d} B deflated")
    return texts

def glyph_mask(width):
    """'twm' in Plex Mono Bold filling `width` px; returns a boolean ink mask."""
    size = 200
    font = ImageFont.truetype(FONT, size)
    l, t, r, b = font.getbbox("twm")
    scale = width / (r - l)
    font = ImageFont.truetype(FONT, int(size * scale))
    l, t, r, b = font.getbbox("twm")
    img = Image.new("L", (r - l + 4, b - t + 4), 0)
    ImageDraw.Draw(img).text((2 - l, 2 - t), "twm", font=font, fill=255)
    return np.array(img) > 127

def cells(mask, cell, inside=0.92):
    """Cells of `cell` px whose area is at least `inside` ink: real modules."""
    h, w = mask.shape
    out = []
    for y in range(0, h - cell + 1, cell):
        for x in range(0, w - cell + 1, cell):
            if mask[y:y + cell, x:x + cell].mean() >= inside:
                out.append((x, y))
    return out

def render(mask, cellpos, cell, syms, size_channel):
    h, w = mask.shape
    img = Image.new("RGB", (w, h), GROUND)
    d = ImageDraw.Draw(img)
    for (x, y), s in zip(cellpos, syms):
        if size_channel:
            shade, big = s >> 1, s & 1
        else:
            shade, big = s, 1
        m = int(cell * (0.85 if big else 0.45))
        o = (cell - m) // 2
        d.rectangle([x + o, y + o, x + o + m - 1, y + o + m - 1], fill=SHADES[shade])
    return img

def degrade(img, quality, scale):
    w, h = img.size
    if scale != 1:
        img = img.resize((max(1, int(w * scale)), max(1, int(h * scale))), Image.BILINEAR)
    buf = io.BytesIO()
    img.save(buf, "JPEG", quality=quality)
    img = Image.open(io.BytesIO(buf.getvalue())).convert("RGB")
    if scale != 1:
        img = img.resize((w, h), Image.BILINEAR)
    return img

def decode(img, cellpos, cell, size_channel):
    a = np.asarray(img).astype(float)
    out = []
    ref = np.array(SHADES, float)
    for (x, y) in cellpos:
        c0 = cell // 2
        r = max(1, cell // 8)
        core = a[y + c0 - r:y + c0 + r + 1, x + c0 - r:x + c0 + r + 1].reshape(-1, 3).mean(0)
        shade = int(np.argmin(((ref - core) ** 2).sum(1)))
        if size_channel:
            # the band between the small square's edge (0.275) and the large
            # one's (0.075): ink there means large
            lo = max(1, int(round(cell * 0.11)))
            hi = max(lo + 1, int(round(cell * 0.24)))
            parts = [a[y + lo:y + hi, x + lo:x + cell - lo], a[y + cell - hi:y + cell - lo, x + lo:x + cell - lo],
                     a[y + hi:y + cell - hi, x + lo:x + hi], a[y + hi:y + cell - hi, x + cell - hi:x + cell - lo]]
            ring = np.concatenate([p.reshape(-1, 3) for p in parts if p.size])
            g = np.array(GROUND, float)
            dist_g = np.sqrt(((ring - g) ** 2).sum(1)).mean()
            dist_s = np.sqrt(((ring - ref[shade]) ** 2).sum(1)).mean()
            big = 1 if dist_s < dist_g else 0
            out.append(shade << 1 | big)
        else:
            out.append(shade)
    return out

def main():
    print("PAYLOAD")
    payloads()
    random.seed(1)
    print("\nCAPACITY  (modules inside the glyphs, 92% ink or more)")
    for width in (500, 300, 200):
        mask = glyph_mask(width)
        for cell in (6, 8, 10, 12, 15):
            n = len(cells(mask, cell))
            print(f"  mark {width:3d} px wide, {cell:2d} px cells: {n:4d} modules = "
                  f"{n * 3 // 8:4d} B at 3 bits, {n * 2 // 8:4d} B at 2 bits")
    print("\nSURVIVAL  (symbol error rate after re-encoding; 500 px mark)")
    conds = [("PNG (lossless)", None, 1.0), ("JPEG q90", 90, 1.0), ("JPEG q75", 75, 1.0),
             ("JPEG q60", 60, 1.0), ("JPEG q75 at 50% and back", 75, 0.5), ("JPEG q60 at 50% and back", 60, 0.5)]
    mask = glyph_mask(500)
    for cell in (8, 10, 12, 15):
        cp = cells(mask, cell)
        for size_channel in (True, False):
            nsym = 8 if size_channel else 4
            syms = [random.randrange(nsym) for _ in cp]
            img = render(mask, cp, cell, syms, size_channel)
            row = []
            for name, q, sc in conds:
                im = img if q is None else degrade(img, q, sc)
                got = decode(im, cp, cell, size_channel)
                err = sum(1 for a, b in zip(syms, got) if a != b) / max(1, len(syms))
                row.append(f"{err * 100:5.1f}%")
            bits = 3 if size_channel else 2
            print(f"  cell {cell:2d}, {bits} bits/module ({len(cp):4d}): " + "  ".join(
                f"{c[0].split()[0]}{'' if c[1] is None else c[1]}{'' if c[2] == 1 else '/2'}={r}" for c, r in zip(conds, row)))
            if cell == 10 and size_channel:
                img.save(sys.argv[1] if len(sys.argv) > 1 else "/tmp/mark.png")

main()
