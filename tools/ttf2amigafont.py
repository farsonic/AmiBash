#!/usr/bin/env python3
"""Render a monospaced TrueType font into an Amiga bitmap font family.

usage: ttf2amigafont.py font.ttf NAME OUTDIR [--tall] SIZE...

Every SIZE (pixels per em) is rendered by FreeType with the font's own
hinting in monochrome, so each size is drawn for its pixel grid (crisp,
not a scaled bitmap).  Writes OUTDIR/NAME.font (all sizes) and
OUTDIR/NAME/<height>, characters 32-255 (ISO 8859-1).

--tall renders for screens whose pixels are twice as tall as wide
(non-interlaced hires): half the vertical resolution, autohinted, and
the font is flagged FPF_TALLDOT.

Needs freetype-py (pip install freetype-py)."""
import os, struct, sys
import freetype

args = sys.argv[1:]
tall = '--tall' in args
args = [a for a in args if a != '--tall']
src, name, outdir = args[0], args[1], args[2]
sizes = [int(s) for s in args[3:]]
LO, HI = 32, 255
FPF_TALLDOT, FPF_DESIGNED = 0x08, 0x40
flags = FPF_DESIGNED | (FPF_TALLDOT if tall else 0)
# the font's own hinting is best on square pixels; FreeType's autohinter
# copes far better with the squashed grid of tall pixels
LOAD = freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO
if tall:
    LOAD |= freetype.FT_LOAD_FORCE_AUTOHINT


def render(px):
    face = freetype.Face(src)
    # 72 dpi: points = pixels; tall pixels: half the vertical resolution
    face.set_char_size(width=px * 64, height=px * 64, hres=72, vres=36 if tall else 72)
    m = face.size
    asc = (m.ascender + 63) >> 6
    desc = (-m.descender + 63) >> 6
    height = asc + desc
    face.load_char('M', LOAD)
    cw = face.glyph.advance.x >> 6
    n = HI - LO + 2                       # + the default glyph
    modulo = (n * cw + 15) // 16 * 2      # bytes per row, word aligned
    rows = [bytearray(modulo) for _ in range(height)]
    widths = []
    for i in range(n):
        ch = LO + i if i < n - 1 else 0xFFFD
        if 127 <= ch < 160 or face.get_char_index(ch) == 0:
            widths.append(0 if i < n - 1 else cw)
            if i == n - 1:                # default glyph: a box
                for y in range(1, height - 1):
                    for x in (1, cw - 2):
                        rows[y][(i * cw + x) // 8] |= 0x80 >> ((i * cw + x) % 8)
                for x in range(1, cw - 1):
                    for y in (1, height - 2):
                        rows[y][(i * cw + x) // 8] |= 0x80 >> ((i * cw + x) % 8)
            continue
        widths.append(cw)
        face.load_char(ch, LOAD)
        g = face.glyph
        bm = g.bitmap
        for r in range(bm.rows):
            ty = asc - g.bitmap_top + r
            if not 0 <= ty < height:
                continue
            for c in range(bm.width):
                if bm.buffer[r * bm.pitch + c // 8] & (0x80 >> (c % 8)):
                    tx = g.bitmap_left + c
                    if 0 <= tx < cw:
                        x = i * cw + tx
                        rows[ty][x // 8] |= 0x80 >> (x % 8)
    chardata = b''.join(bytes(r) for r in rows)
    charloc = b''.join(struct.pack('>HH', i * cw, widths[i]) for i in range(n))
    return height, cw, asc, modulo, chardata, charloc


def write_size(height, cw, asc, modulo, chardata, charloc):
    TF = 58                               # offset of struct TextFont
    hdr = bytearray(110)
    struct.pack_into('>I', hdr, 0, 0x70FF4E75)             # moveq #-1,d0 ; rts
    hdr[4 + 8] = 12                                        # dfh_DF.ln_Type NT_FONT
    struct.pack_into('>H', hdr, 18, 0x0F80)                # DFH_ID
    hdr[26:26 + len(name)] = name.encode()                 # dfh_Name[32]
    hdr[TF + 8] = 12                                       # tf_Message ln_Type
    struct.pack_into('>H', hdr, TF + 18, 52)               # mn_Length
    struct.pack_into('>HBBHHHH', hdr, TF + 20, height, 0, flags, cw, asc - 1, 1, 0)
    hdr[TF + 32] = LO
    hdr[TF + 33] = HI
    cd_off = len(hdr)
    cl_off = cd_off + len(chardata)
    cl_off += cl_off & 2                                   # long align
    body = bytes(hdr) + chardata + bytes(cl_off - cd_off - len(chardata)) + charloc
    body += bytes((-len(body)) % 4)
    body = bytearray(body)
    struct.pack_into('>I', body, 4 + 10, 26)               # ln_Name -> dfh_Name
    struct.pack_into('>I', body, TF + 10, 26)
    struct.pack_into('>I', body, TF + 34, cd_off)          # tf_CharData
    struct.pack_into('>H', body, TF + 38, modulo)          # tf_Modulo
    struct.pack_into('>I', body, TF + 40, cl_off)          # tf_CharLoc
    relocs = [4 + 10, TF + 10, TF + 34, TF + 40]
    hunk = struct.pack('>IIIII', 0x3F3, 0, 1, 0, 0) + struct.pack('>I', len(body) // 4)
    hunk += struct.pack('>II', 0x3E9, len(body) // 4) + bytes(body)
    hunk += struct.pack('>III', 0x3EC, len(relocs), 0) + b''.join(struct.pack('>I', r) for r in relocs)
    hunk += struct.pack('>II', 0, 0x3F2)
    open(os.path.join(outdir, name, str(height)), 'wb').write(hunk)


os.makedirs(os.path.join(outdir, name), exist_ok=True)
done = {}
for px in sizes:
    r = render(px)
    if r[0] in done:
        continue
    write_size(*r)
    done[r[0]] = r[1]
    print('%s/%d: %dx%d (%d px em)' % (name, r[0], r[1], r[0], px))
heights = sorted(done)
fc = struct.pack('>HH', 0x0F00, len(heights))
for h in heights:
    fc += ('%s/%d' % (name, h)).encode().ljust(256, b'\0') + struct.pack('>HBB', h, 0, flags)
open(os.path.join(outdir, name + '.font'), 'wb').write(fc)
