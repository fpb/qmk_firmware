#!/usr/bin/env python3
"""
AK820 Pro LCD asset converter (Stage C -- see docs/LCD_FLASH_LAYER.md).

Decodes the source PNGs into flat .raw pixel files plus a manifest describing each
one (dimensions, depth/format, stride, size, palette). No third-party deps: PNG is
decoded here with stdlib zlib only (Pillow/ImageMagick are not available).

EVERYTHING is emitted as rgb565: 16 bits per pixel, big-endian (hi byte first, matching
how the panel is fed), alpha composited over black. This is deliberate -- these raws are
destined for external flash, and the SPI-to-SPI DMA can only stream raw pixels: it cannot
expand 1bpp or blend fg/bg on the fly. So the on-flash format must already be what the
panel consumes, and keeping the firmware-embedded step in the same format makes the later
move to flash a pure relocation rather than a reformat.

Consequence for fonts: glyph colours are BAKED. Harmless here -- the dashboard is
uniformly COL_FG 0xFFFF on COL_BG 0x0000, which is exactly what the source PNGs are.

Font atlases are self-describing: a magenta (255,0,255) marker sits at each glyph cell's
top-left corner, so marker spacing IS the advance and marker count IS the glyph count.
The markers are metadata and resolve to background in the output.

Usage:
    python3 mkraw.py            # inspect only: report what each PNG contains
    python3 mkraw.py --write    # also emit raw/<name>.raw + raw/manifest.json
"""

import json
import os
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OUTDIR = os.path.join(HERE, "raw")

CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}  # PNG colour type -> samples per pixel


# --------------------------------------------------------------------------- PNG
def png_chunks(blob):
    assert blob[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    i = 8
    while i < len(blob):
        ln = int.from_bytes(blob[i:i + 4], "big")
        typ = blob[i + 4:i + 8]
        data = blob[i + 8:i + 8 + ln]
        yield typ, data
        i += 8 + ln + 4  # skip CRC


def unfilter(raw, h, stride, bpp):
    """Reverse the per-scanline PNG filters. Operates on bytes, not pixels."""
    out = bytearray()
    prev = bytearray(stride)
    i = 0
    for _ in range(h):
        ft = raw[i]
        i += 1
        line = bytearray(raw[i:i + stride])
        i += stride
        if ft == 1:
            for x in range(bpp, stride):
                line[x] = (line[x] + line[x - bpp]) & 0xFF
        elif ft == 2:
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 0xFF
        elif ft == 3:
            for x in range(stride):
                a = line[x - bpp] if x >= bpp else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 0xFF
        elif ft == 4:
            for x in range(stride):
                a = line[x - bpp] if x >= bpp else 0
                b = prev[x]
                c = prev[x - bpp] if x >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 0xFF
        elif ft != 0:
            raise ValueError("bad filter type %d" % ft)
        out += line
        prev = line
    return out


def samples(line, w, nch, depth):
    """Yield per-pixel sample tuples from one unfiltered scanline."""
    if depth == 8:
        for x in range(w):
            yield tuple(line[x * nch + c] for c in range(nch))
    elif depth == 16:
        for x in range(w):
            yield tuple(line[(x * nch + c) * 2] for c in range(nch))  # take hi byte
    else:  # 1, 2, 4 bits, MSB-first
        per = 8 // depth
        mask = (1 << depth) - 1
        for x in range(w):
            vals = []
            for c in range(nch):
                idx = x * nch + c
                byte = line[idx // per]
                shift = 8 - depth * (idx % per + 1)
                vals.append((byte >> shift) & mask)
            yield tuple(vals)


def decode_png(path):
    """-> (w, h, pixels) where pixels is a flat list of (r,g,b,a) 8-bit tuples."""
    blob = open(path, "rb").read()
    w = h = depth = ctype = interlace = None
    plte, trns, idat = None, None, bytearray()
    for typ, data in png_chunks(blob):
        if typ == b"IHDR":
            w = int.from_bytes(data[0:4], "big")
            h = int.from_bytes(data[4:8], "big")
            depth, ctype, interlace = data[8], data[9], data[12]
        elif typ == b"PLTE":
            plte = [tuple(data[i:i + 3]) for i in range(0, len(data), 3)]
        elif typ == b"tRNS":
            trns = data
        elif typ == b"IDAT":
            idat += data
    if interlace:
        raise NotImplementedError("interlaced PNG not supported: " + path)

    nch = CHANNELS[ctype]
    stride = (w * nch * depth + 7) // 8
    bpp = max(1, (nch * depth + 7) // 8)
    lines = unfilter(zlib.decompress(bytes(idat)), h, stride, bpp)

    mx = (1 << depth) - 1
    px = []
    for y in range(h):
        line = lines[y * stride:(y + 1) * stride]
        for s in samples(line, w, nch, depth):
            if ctype == 3:                       # palette
                r, g, b = plte[s[0]]
                a = trns[s[0]] if trns and s[0] < len(trns) else 255
            elif ctype == 0:                     # grey
                v = s[0] * 255 // mx
                r = g = b = v
                a = 255
            elif ctype == 4:                     # grey + alpha
                v = s[0] * 255 // mx
                r = g = b = v
                a = s[1] * 255 // mx
            elif ctype == 2:                     # rgb
                r, g, b = (c * 255 // mx for c in s)
                a = 255
            else:                                # rgba
                r, g, b = (c * 255 // mx for c in s[:3])
                a = s[3] * 255 // mx
            px.append((r, g, b, a))
    return w, h, px


# ----------------------------------------------------------------------- packing
def luma(c):
    return (c[0] * 299 + c[1] * 587 + c[2] * 114) // 1000


MARKER = (255, 0, 255)   # magenta: cell-origin markers in the font atlases


def to_mono1(w, h, px):
    """Pack to 1bpp, MSB-first, byte-aligned rows. Bit 1 = ink (white).

    MARKER pixels are metadata (they mark each glyph cell's top-left corner) and are
    packed as background, not ink. Fonts never carry glyph ink in that corner.
    """
    stride = (w + 7) // 8
    out = bytearray(stride * h)
    for y in range(h):
        for x in range(w):
            p = px[y * w + x]
            if p[:3] == MARKER:
                continue
            if p[3] >= 128 and luma(p) >= 128:    # transparent is never ink
                out[y * stride + (x >> 3)] |= 0x80 >> (x & 7)
    return bytes(out), stride


def font_metrics(w, h, px):
    """Derive the glyph grid from the magenta cell-origin markers in row 0.

    The atlas is self-describing: one marker per cell at its top-left corner, so the
    marker spacing IS the cell advance and the marker count IS the glyph count.
    Returns None when the image carries no markers (i.e. it is not a font atlas).
    """
    xs = sorted(x for x in range(w) if px[x][:3] == MARKER)   # row 0 only
    if len(xs) < 2:
        return None
    deltas = {xs[i + 1] - xs[i] for i in range(len(xs) - 1)}
    if len(deltas) != 1:
        raise ValueError("non-uniform glyph advance: %s" % sorted(deltas))
    return {"cell_w": deltas.pop(), "cell_h": h, "count": len(xs), "first_char": 0x20}


def to_rgb565(w, h, px):
    """16bpp big-endian (hi byte first), alpha composited over black.

    MARKER pixels are metadata (glyph cell origins), not art, so they resolve to the
    background colour rather than magenta.
    """
    out = bytearray()
    for p in px:
        r, g, b, a = p
        if p[:3] == MARKER:
            r = g = b = 0
        elif a != 255:
            r, g, b = r * a // 255, g * a // 255, b * a // 255
        v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        out += bytes((v >> 8, v & 0xFF))
    return bytes(out), w * 2


# -------------------------------------------------------------------------- main
def main():
    write = "--write" in sys.argv
    pngs = sorted(f for f in os.listdir(HERE) if f.lower().endswith(".png"))
    if not pngs:
        print("no PNGs found in", HERE)
        return 1
    if write:
        os.makedirs(OUTDIR, exist_ok=True)

    manifest = []
    print("%-30s %5s %5s  %-7s %6s %8s  %s" % ("source", "w", "h", "format", "colors", "bytes", "notes"))
    print("-" * 88)
    for f in pngs:
        w, h, px = decode_png(os.path.join(HERE, f))
        colors = sorted({p for p in px})
        # A magenta marker row means this is a font atlas -> mono1, drawn with runtime
        # fg/bg. Everything else (icons, splash) keeps its real colours as rgb565:
        # the icons are NOT all monochrome (bluetooth is blue, others carry a grey).
        metrics = font_metrics(w, h, px)
        data, stride, fmt, depth = *to_rgb565(w, h, px), "rgb565", 16
        note = ""
        if metrics:
            note = "font atlas: %d glyphs @ %dx%d, first=0x%02X (fg/bg baked)" % (
                metrics["count"], metrics["cell_w"], metrics["cell_h"], metrics["first_char"])
        name = os.path.splitext(f)[0]
        print("%-30s %5d %5d  %-7s %6d %8d  %s" % (f, w, h, fmt, len(colors), len(data), note))
        entry = {
            "name": name,
            "source": f,
            "raw": "raw/%s.raw" % name,
            "width": w,
            "height": h,
            "format": fmt,          # mono1 | rgb565
            "depth": depth,         # bits per pixel
            "stride": stride,       # bytes per row
            "bytes": len(data),
            "colors": len(colors),
        }
        if metrics:
            entry["font"] = metrics          # cell_w, cell_h, count, first_char
        else:
            entry["palette"] = ["#%02X%02X%02X%02X" % c for c in colors] if len(colors) <= 8 else None
        manifest.append(entry)
        if write:
            with open(os.path.join(OUTDIR, name + ".raw"), "wb") as fh:
                fh.write(data)

    total = sum(e["bytes"] for e in manifest)
    print("-" * 72)
    print("%-30s %25s %8d" % ("TOTAL", "", total))
    if write:
        with open(os.path.join(OUTDIR, "manifest.json"), "w") as fh:
            json.dump({"assets": manifest}, fh, indent=2)
            fh.write("\n")
        print("\nwrote %d raw files + manifest.json to %s" % (len(manifest), OUTDIR))
    else:
        print("\n(inspect only -- rerun with --write to emit raw/ + manifest.json)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
