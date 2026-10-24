# Spike: the mark as a carrier (MRK-2)

The twmfetch design document leaves the in-pixel mark's encoding open and
asks for a spike before the mark becomes the paste-path transport. This is
that spike. Nothing here ships: `docs/spikes/mark.py` reproduces the numbers
(Python, numpy, PIL), and twm's fetch shots keep carrying the look in the
PNG's text chunk only.

## What it must carry

| Text | Raw | Deflated |
|------|----:|---------:|
| The built-in defaults (a whole `.twmrc`) | 3501 B | 1316 B |
| The owner's own `.twmrc` | 216 B | 175 B |
| Appearance keys only (SHR-2) | 345 B | 210 B |

A whole `.twmrc` will never fit in three letters. But sharing takes only
appearance keys (SHR-2), and only the ones that differ from the defaults
matter: an accent is 3 bytes, a title height 1, a squeeze rule a few. A
typical look, as key/value pairs against a fixed key table, is 10–40 bytes.

## What fits

Modules are cells at least 92 % inside the "twm" glyphs (IBM Plex Mono
Bold), so a decoder never has to guess which count.

| Mark width | Cell | Modules | Bytes at 3 bits | Bytes at 2 bits |
|-----------:|-----:|--------:|----------------:|----------------:|
| 500 px | 8 px | 511 | 191 | 127 |
| 500 px | 10 px | 294 | 110 | 73 |
| 500 px | 12 px | 189 | 70 | 47 |
| 300 px | 8 px | 157 | 58 | 39 |
| 300 px | 12 px | 38 | 14 | 9 |
| 200 px | 8 px | 41 | 15 | 10 |

Below about 300 px the glyph strokes are too thin to carry anything; the
design's "solid accent at favicon size" is right.

## What survives

Random symbols on a 500 px mark, rendered on `ground`, re-encoded, decoded
by sampling cell centres (shade: nearest of the four; size: ink in the band
between the small and large square). Symbol error rate:

| Cell, channel | PNG | JPEG q75 | JPEG q60 | q75, half size and back | q60, half size and back |
|---------------|----:|---------:|---------:|------------------------:|------------------------:|
| 8 px, shade + size (3 bits) | 39 % | 39 % | 39 % | 44 % | 49 % |
| 8 px, shade only (2 bits) | 0 % | 0 % | 0 % | 4.5 % | 15 % |
| 10 px, shade + size | 0 % | 2 % | 9 % | 38 % | 41 % |
| 10 px, shade only | 0 % | 0 % | 0 % | 0.3 % | 3.4 % |
| 12 px, shade + size | 0 % | 0 % | 0 % | 16 % | 17 % |
| 12 px, shade only | 0 % | 0 % | 0 % | 0 % | 0 % |
| 15 px, shade + size | 0 % | 0 % | 0 % | 8 % | 11 % |

The size channel does not survive what chat apps do (downscale, then JPEG
with 4:2:0 chroma). Shade alone does, with room to spare at 10–12 px cells.

## Recommendation

1. **Shade carries data, size is decoration.** Four shades (dark, dim,
   bright, accent) are 2 bits a module; sizes vary only for the look B10
   draws, from a fixed pattern the decoder ignores.
2. **The payload is the look's delta**, not text: a version nibble, a
   length, then key-index/value pairs for appearance keys that differ from
   the defaults, then a CRC-16.
3. **Reed–Solomon over GF(256)** with about a third of the bytes as parity:
   at 500 px and 10 px cells that is 73 B raw → ~48 B of look, enough for
   any realistic accent, title height, squeeze and font choice; 12 px cells
   (47 B raw → ~31 B) when the shot will be shrunk hard.
4. **Finding it** needs no extra marks: the glyphs' geometry is fixed by the
   font, so a decoder finds "twm" by its outline at a few scale steps, then
   reads the grid inside. The scatter outside the glyphs stays decorative.
5. **Not tested yet:** the real pipelines (Signal, WhatsApp, Discord,
   forums) and photographed screens. That, and a C encoder and decoder,
   are the next step if the paste path is wanted; until then the text
   chunk (SHOT-2) is the only carrier, and dropping or opening the file is
   the way to share a look.
