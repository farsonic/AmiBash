#!/usr/bin/env python3
"""itermcolors.py DIR [COMMIT] > colorschemes - iTerm2 colour schemes for AmiBash.

Reads every DIR/*.itermcolors (an XML property list, as in the
iTerm2-Color-Schemes gallery, https://github.com/mbadolato/iTerm2-Color-Schemes,
MIT licence) and prints one line per scheme for the terminal's Presets page
(ENVARC:AmiBash/colorschemes):

    NAME<TAB>RRGGBB RRGGBB ...

23 colours in the order of src/term.h: foreground, background, bold, cursor,
cursor text, selection, selected text, then ANSI 0-15.  A colour the file
does not have is taken as iTerm2 takes it (bold: the foreground, ...).
"""
import os
import plistlib
import sys

KEYS = ["Foreground Color", "Background Color", "Bold Color", "Cursor Color",
        "Cursor Text Color", "Selection Color", "Selected Text Color"] + \
       ["Ansi %d Color" % i for i in range(16)]
FALLBACK = {"Bold Color": "Foreground Color", "Cursor Color": "Foreground Color",
            "Cursor Text Color": "Background Color",
            "Selected Text Color": "Foreground Color"}


def rgb(d):
    c = [d.get(k + " Component", 0.0) for k in ("Red", "Green", "Blue")]
    return "%02X%02X%02X" % tuple(max(0, min(255, int(round(float(v) * 255)))) for v in c)


def scheme(path):
    with open(path, "rb") as f:
        p = plistlib.load(f)
    out = []
    for k in KEYS:
        d = p.get(k) or p.get(FALLBACK.get(k, ""))
        if d is None:
            if k == "Selection Color":
                out.append("B5D5FF")
                continue
            return None
        out.append(rgb(d))
    return out


HEADER = """# Colour schemes for the AmiBash Terminal's Presets page.
# From iTerm2-Color-Schemes, https://github.com/mbadolato/iTerm2-Color-Schemes
# (commit %s), converted by AmiBash's tools/itermcolors.py.
# MIT License. Copyright (c) 2011 to Present Mark Badolato. Permission is
# hereby granted, free of charge, to any person obtaining a copy of this
# software and associated documentation files (the "Software"), to deal in
# the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions: The above
# copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED
# "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
"""


def main(d, commit="?"):
    sys.stdout.write(HEADER % commit)
    for f in sorted(os.listdir(d), key=str.lower):
        if not f.endswith(".itermcolors"):
            continue
        try:
            cols = scheme(os.path.join(d, f))
        except Exception as e:
            print("itermcolors.py: %s: %s" % (f, e), file=sys.stderr)
            continue
        if not cols:
            continue
        # the Amiga's character set, and the list view's width
        name = f[:-len(".itermcolors")].encode("latin-1", "replace").decode("latin-1")[:39]
        sys.stdout.write(name + "\t" + " ".join(cols) + "\n")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else "?")
