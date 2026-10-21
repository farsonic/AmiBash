#!/usr/bin/env python3
"""Write an icon with a 2-bitplane ">_" terminal image.
usage: mkicon.py out.info                          the tool icon (bash.info)
       mkicon.py out.info DEFAULTTOOL [TOOLTYPE...]  a project icon"""
import struct, sys

W, H = 48, 24
# pens: 0 grey, 1 black, 2 white, 3 blue
img = [[0] * W for _ in range(H)]
for y in range(H):
    for x in range(W):
        if x in (0, W - 1) or y in (0, H - 1):
            img[y][x] = 1                      # frame
        elif y < 5:
            img[y][x] = 3                      # title bar
        else:
            img[y][x] = 1                      # screen background (black)
prompt = ["#.......", ".##.....", "..##....", "...##...", "..##....", ".##.....", "#......."]
for dy, row in enumerate(prompt):
    for dx, c in enumerate(row):
        if c == '#':
            img[8 + dy][5 + dx] = 2            # ">" in white
for x in range(15, 23):
    img[14][x] = img[15][x] = 2                # "_"
words = (W + 15) // 16
data = b''
for plane in range(2):
    for y in range(H):
        bits = ''.join('1' if (img[y][x] >> plane) & 1 else '0' for x in range(words * 16))
        data += int(bits, 2).to_bytes(words * 2, 'big')

gadget = struct.pack('>IhhhhHHHIIIIIHI', 0, 0, 0, W, H + 1, 5, 3, 1, 1, 0, 0, 0, 0, 0, 1)
# the tabbed terminal opens by default; remove the brackets to use a
# plain console window instead
tooltypes = [b'(WINDOW=CON:0/12/640/220/Bash/CLOSE)']
deftool = b''
if len(sys.argv) > 2:
    deftool = sys.argv[2].encode('latin-1')
    tooltypes = [t.encode('latin-1') for t in sys.argv[3:]]
kind = 4 if deftool else 3                     # WBPROJECT / WBTOOL
do = struct.pack('>HH', 0xE310, 1) + gadget + struct.pack('>BBIIiiIIi', kind, 0, 1 if deftool else 0,
                                                          1 if tooltypes else 0, -0x80000000, -0x80000000, 0, 0,
                                                          65536 if not deftool else 16384)
image = struct.pack('>hhhhhIBBI', 0, 0, W, H, 2, 1, 3, 0, 0)
tt = b''
if tooltypes:
    tt = struct.pack('>I', (len(tooltypes) + 1) * 4) + b''.join(struct.pack('>I', len(t) + 1) + t + b'\0' for t in tooltypes)
dt = struct.pack('>I', len(deftool) + 1) + deftool + b'\0' if deftool else b''
open(sys.argv[1], 'wb').write(do + image + data + dt + tt)
print('wrote', sys.argv[1], len(do), 'byte header')
