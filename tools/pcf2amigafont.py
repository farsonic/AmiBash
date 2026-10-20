#!/usr/bin/env python3
"""Convert a fixed-width PCF bitmap font to an Amiga diskfont.

usage: pcf2amigafont.py font.pcf[.gz] NAME OUTDIR
writes OUTDIR/NAME.font and OUTDIR/NAME/<height> (chars 32..126)."""
import gzip, io, os, struct, sys
from PIL import PcfFontFile, Image

src, name, outdir = sys.argv[1], sys.argv[2], sys.argv[3]
data = open(src, 'rb').read()
if src.endswith('.gz'):
    data = gzip.decompress(data)
pcf = PcfFontFile.PcfFontFile(io.BytesIO(data))
# PIL's glyph table can be offset from the encoding: find the shift that
# makes the space character blank and '!' non-blank
def blank(g):
    if not g: return True
    im = g[3].convert('1')
    return not any(im.getpixel((x, y)) for x in range(im.size[0]) for y in range(im.size[1]))
SHIFT = 0
for cand in (0, -1, 1):
    if blank(pcf.glyph[32 + cand]) and not blank(pcf.glyph[33 + cand]):
        SHIFT = cand
        break
glyphs = pcf.glyph
asc = max(-g[1][1] for g in glyphs if g) if glyphs else 0
lo, hi = 32, 126
cw = max(g[0][0] for g in glyphs[lo:hi + 1] if g)
desc = max(g[1][3] for g in glyphs[lo:hi + 1] if g)
height = asc + desc
n = hi - lo + 2                       # + default glyph
modulo = (n * cw + 15) // 16 * 2      # bytes per row, word aligned
bits = [[0] * (modulo * 8) for _ in range(height)]
for i in range(n):
    ch = lo + i if i < n - 1 else ord('?')
    ch += SHIFT
    g = glyphs[ch] if 0 <= ch < len(glyphs) else None
    if not g:
        continue
    (dx, dy), (x0, y0, x1, y1), _src, im = g
    im = im.convert('1')
    for y in range(im.size[1]):
        for x in range(im.size[0]):
            if im.getpixel((x, y)):
                ty = asc + y0 + y
                tx = i * cw + x0 + x
                if 0 <= ty < height and 0 <= tx - i * cw < cw:
                    bits[ty][tx] = 1
chardata = b''.join(
    bytes(int(''.join(map(str, row[b * 8:b * 8 + 8])), 2) for b in range(modulo)) for row in bits)
charloc = b''.join(struct.pack('>HH', i * cw, cw) for i in range(n))

# --- code hunk: return code, DiskFontHeader, TextFont, data ---------------
TF = 58                               # offset of struct TextFont
hdr = bytearray(110)
struct.pack_into('>I', hdr, 0, 0x70FF4E75)             # moveq #-1,d0 ; rts
hdr[4 + 8] = 12                                        # dfh_DF.ln_Type NT_FONT
struct.pack_into('>H', hdr, 18, 0x0F80)                # DFH_ID
hdr[26:26 + len(name)] = name.encode()                 # dfh_Name[32]
hdr[TF + 8] = 12                                       # tf_Message ln_Type
struct.pack_into('>H', hdr, TF + 18, 52)               # mn_Length
struct.pack_into('>HBBHHHH', hdr, TF + 20, height, 0, 0x40, cw, asc - 1, 1, 0)
hdr[TF + 32] = lo
hdr[TF + 33] = hi
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
os.makedirs(os.path.join(outdir, name), exist_ok=True)
open(os.path.join(outdir, name, str(height)), 'wb').write(hunk)
fc = struct.pack('>HH', 0x0F00, 1) + ('%s/%d' % (name, height)).encode().ljust(256, b'\0') + struct.pack('>HBB', height, 0, 0x40)
open(os.path.join(outdir, name + '.font'), 'wb').write(fc)
print('%s: %dx%d, baseline %d, glyph shift %d' % (name, cw, height, asc - 1, SHIFT))
