#!/usr/bin/env python3
"""Write a Workbench icon (.info) in the four standard colours
(0 grey, 1 black, 2 white, 3 blue).

usage: mkinfo.py drawer OUT.info
       mkinfo.py doc    OUT.info [DEFAULTTOOL]   (default SYS:Utilities/MultiView)
       mkinfo.py tool   OUT.info [TOOLTYPE...]
"""
import struct, sys

NO_POSITION = -0x80000000


def picture(kind):
    if kind == 'drawer':                       # a folder
        W, H = 40, 20
        img = [[0] * W for _ in range(H)]
        for y in range(3, H):
            for x in range(1, W - 1):
                img[y][x] = 3                  # blue body
        for x in range(2, 16):
            img[1][x] = img[2][x] = 3          # the tab
        for x in range(1, W - 1):
            img[3][x] = 2                      # light top edge
            img[H - 1][x] = 1
        for y in range(3, H):
            img[y][1] = 2
            img[y][W - 2] = 1
        return W, H, img
    if kind == 'doc':                          # a page with lines
        W, H = 28, 24
        img = [[0] * W for _ in range(H)]
        for y in range(1, H - 1):
            for x in range(3, W - 3):
                img[y][x] = 2
        for y in range(1, H - 1):
            img[y][3] = img[y][W - 4] = 1
        for x in range(3, W - 3):
            img[1][x] = img[H - 2][x] = 1
        for y in range(5, H - 4, 3):
            for x in range(6, W - 7 if y % 2 else W - 10):
                img[y][x] = 1                  # text lines
        return W, H, img
    # tool: a small window with a ">_" prompt
    W, H = 40, 22
    img = [[0] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            img[y][x] = 1 if (x in (0, W - 1) or y in (0, H - 1)) else (3 if y < 5 else 1)
    for dy, row in enumerate(["#....", ".##..", "..##.", ".##..", "#...."]):
        for dx, c in enumerate(row):
            if c == '#':
                img[8 + dy][5 + dx] = 2
    for x in range(12, 19):
        img[12][x] = 2
    return W, H, img


def planes(W, H, img):
    words = (W + 15) // 16
    data = b''
    for plane in range(2):
        for y in range(H):
            bits = ''.join('1' if x < W and (img[y][x] >> plane) & 1 else '0' for x in range(words * 16))
            data += int(bits, 2).to_bytes(words * 2, 'big')
    return data


def strings(lst):
    return struct.pack('>I', (len(lst) + 1) * 4) + b''.join(
        struct.pack('>I', len(t) + 1) + t + b'\0' for t in lst)


def main():
    kind, out = sys.argv[1], sys.argv[2]
    rest = [a.encode('latin-1') for a in sys.argv[3:]]
    W, H, img = picture(kind)
    deftool, tooltypes = b'', []
    if kind == 'doc':
        deftool = rest[0] if rest else b'SYS:Utilities/MultiView'
    elif kind == 'tool':
        tooltypes = rest
    wbtype = {'drawer': 2, 'tool': 3, 'doc': 4}[kind]
    # Gadget: width, height (+1 for the highlight), flags GADGIMAGE|GADGHCOMP,
    # activation, type BOOLGADGET, a render image, UserData 1 (OS 2 icon)
    gadget = struct.pack('>IhhhhHHHIIIIIHI', 0, 0, 0, W, H + 1, 5, 3, 1, 1, 0, 0, 0, 0, 0, 1)
    do = struct.pack('>HH', 0xE310, 1) + gadget + struct.pack(
        '>BBIIiiIIi', wbtype, 0, 1 if deftool else 0, 1 if tooltypes else 0,
        NO_POSITION, NO_POSITION, 1 if kind == 'drawer' else 0, 0,
        16384 if kind == 'tool' else 0)
    drawer = b''
    if kind == 'drawer':
        # NewWindow for the drawer's window, then its current scroll position
        drawer = struct.pack('>hhhhBBIIIIIIIhhhhH', 60, 40, 420, 220, 255, 255, 0, 0,
                             0, 0, 0, 0, 0, 90, 40, -1, -1, 1) + struct.pack('>ii', 0, 0)
    image = struct.pack('>hhhhhIBBI', 0, 0, W, H, 2, 1, 3, 0, 0) + planes(W, H, img)
    tail = b''
    if deftool:
        tail += struct.pack('>I', len(deftool) + 1) + deftool + b'\0'
    if tooltypes:
        tail += strings(tooltypes)
    if kind == 'drawer':
        tail += struct.pack('>IH', 0, 0)       # DrawerData2: flags, view mode (defaults)
    open(out, 'wb').write(do + drawer + image + tail)


main()
