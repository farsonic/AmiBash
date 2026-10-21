# InstallTTF

Installs TrueType/OpenType fonts (`.ttf`, `.otf`, `.ttc`) for
diskfont.library from the Shell, like AATextManager or FTManager do, without
a GUI, so it also works on systems without ReAction (OS 2.04 to 3.1.4).
It writes `FONTS:<name>.otag` and `FONTS:<name>.font`; programs then open
the font with OpenDiskFont() through the font engine (aatext.library from
AAText, or freetype2.library), and AAText draws it anti-aliased on RTG.

    InstallTTF FILES/M/A,NAME/K,FACE/N,CHARSET/K,ENGINE/K,SIZES/K,TO/K,COPY/S,REPLACE/S,INFO/S,NOCHECK/S

| option | |
|---|---|
| `FILES` | font files; every face of a `.ttc` unless `FACE` picks one |
| `NAME` | name in FONTS: (default: family, plus the style unless it is Regular/Book) |
| `FACE` | face number in a collection (`.ttc`) |
| `CHARSET` | code page written into the .otag: `latin1` (default for aatext), `latin2`, `latin3`, `latin4`, `latin5` (Turkish), `latin9`, `latin10`, `cp1250`, `cyrillic`, `koi8r`, or `none` |
| `ENGINE` | `aatext` (default when LIBS:aatext.library is there) or `freetype2` |
| `SIZES` | sizes font requesters list, default `8-16,18,20,24` (every size works) |
| `COPY` | copy the font file to `FONTS:_ttf` (or `TO`) and refer to it there |
| `TO` | drawer to copy the font file to (implies `COPY`) |
| `REPLACE` | overwrite a font of the same name |
| `INFO` | only show what would be installed |
| `NOCHECK` | do not open the installed font as a check |

Examples:

    InstallTTF Fonts/DejaVuSansMono.ttf COPY
    InstallTTF Work:Fonts/#?.ttf COPY CHARSET=latin2
    InstallTTF cambria.ttc FACE=1 NAME="Cambria Math"

With `ENGINE=freetype2` no code page is written unless `CHARSET` asks for
one: freetype2.library takes it from `ENV:ftcodepage`, and some versions of
it hang on a code page in the .otag.

When you reinstall a font that is already in use, run `Avail FLUSH` (or
reboot) afterwards, so programs load the new version instead of the copy
still in memory.

Build: `make` (vbcc, NDK 3.2); `mkcharsets.py` regenerates `charsets.h`.
