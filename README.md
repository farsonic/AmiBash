# AmiBash — a bash shell for AmigaOS

AmiBash brings the bash experience to the Amiga. It provides bash syntax,
line editing, history, tab completion, functions, arrays, `[[ ]]`, `$(( ))` and
the familiar Unix utilities, and it runs natively on AmigaOS 2.04+/3.x
(68000 and up) and AROS. It does **not** need ixemul.library or any other
Unix emulation layer, and it understands both Amiga paths and Unix-style
paths:

```
amiga@amiga:Work:$ cd /work/projects && ls -l *.c
amiga@amiga:Work:projects$ grep -n main *.c | sort | head -3
amiga@amiga:Work:projects$ for f in DH0:S/*; do echo "${f##*/}"; done
amiga@amiga:Work:projects$ cd ..; cd ..; ls          # the virtual root "/"
C  DEVS  DF0  DH0  ENVARC  FONTS  L  LIBS  RAM  RAM Disk  S  SYS  Work
```

## Why a reimplementation and not a port of GNU bash

GNU bash (5.x, and the 2.01 port in Aminet `dev/gg/bash-src` that relies on
ixemul.library) is built around `fork()`. It forks for every subshell, for every
pipeline stage, and for every `$(...)`. AmigaOS has no `fork()` and no MMU to
emulate one. The old ADE/Geek Gadgets port needs ixemul's heavyweight
`vfork()` emulation and a GCC 2.x toolchain. AmiBash is a from-scratch
implementation of the bash language written directly against
`dos.library`. It uses GNU bash 5.2 as the reference for its behaviour: a test
corpus is run under both shells and their outputs are compared. The
fork-dependent features are emulated:

| bash feature | how AmiBash does it on AmigaOS |
|---|---|
| `( list )`, `$( … )`, pipeline stages | run in-process; variables, cwd, options, traps and `$@` are journalled and rolled back afterwards, so changes don't leak (same results as a fork) |
| pipelines `a \| b \| c` | stages run one after another; output is kept in memory and spilled to `T:` only if an external program needs a real file handle |
| `<( cmd )` | output written to a temporary file in `T:` |
| `bash script`, `sh -c …`, `./script.sh` | run as an emulated child shell in-process (no second copy is loaded) |
| external programs | `RunCommand()` on the shell's own process, as the AmigaDOS Shell does: resident commands, `Stack`, Ctrl-C and the console all behave natively |
| `cmd &` | external commands start asynchronously via `SystemTags(SYS_Asynch)`; shell constructs run synchronously |

## Installing

The easy way: double-click **Install-AmiBash** (uses the standard Amiga
Installer, in `SYS:Utilities` or `C:` on OS 3.x). It picks the 68000 or
68020 build for your CPU and lets you choose what to install: bash in
`C:`, the example `S:bashrc` (an existing one is kept as `S:bashrc.old`),
the AmiBashMono fonts, `InstallTTF`, the DejaVu Sans Mono TrueType font for
smooth text with AAText on RTG, an *AmiBash Terminal* icon in `SYS:Tools`,
`ssh` and `ssh-keygen` (68020+), and optionally `Resident C:bash PURE` in
`S:User-Startup`.

By hand:

1. Copy `bash` (68000 build) or `bash020` (68020+, renamed to `bash`) to `C:`.
   For Workbench, copy `bash` and `bash.info` to a drawer (e.g. `SYS:Utilities`).
2. Optionally copy `S/bashrc` to `S:bashrc` (system-wide startup file).
3. In a Shell window type `bash`. To make it the default for new Shell
   windows, put `bash` as the last line of `S:Shell-Startup`.

Requirements: AmigaOS 2.04 (V37) or newer, or AROS m68k. A 68000 with 1 MB
works (tested on an emulated A500). The executable is about 400 KB and
pure, so it can be made resident (`Resident C:bash PURE`). The shell gives
itself a 64 KB stack whatever the `Stack` setting.

## Paths

| You type | Means |
|---|---|
| `DH0:Work/x`, `RAM:`, `SYS:C` | native AmigaDOS paths, as always |
| `/dh0/Work/x`, `/ram/t` | Unix style: the first component is a volume, device or assign |
| `/` | the **virtual root**: lists all volumes, devices and assigns (`ls /`, `cd /`) |
| `..`, `../x` | parent directory (from a volume root, `..` goes to `/`) |
| `:Prefs` | root of the current volume |
| `~` | `$HOME` (default `SYS:`; set `HOME` in `ENV:` or `S:bashrc`) |
| `/tmp`, `/dev/null`, `/dev/tty`, `/bin`, `/etc` | `T:` (or `RAM:`), `NIL:`, `*`, `C:`, `S:` |

A leading `/` therefore means "root", as in bash, and **not** "parent" as in
AmigaDOS. Use `..` for the parent directory. `$PWD` and the prompt use
Amiga notation (`Work:projects`). Globbing is case-insensitive like the
Amiga filesystem (`shopt -u nocaseglob` to change that). `shopt -u amigapaths`
switches off all path translation.

`PATH` is built from your Shell's command path in Unix notation
(`/C:/Workbench/Utilities:…:.`), so `:` can separate entries. You can also
write it with `;` separators and Amiga paths: `PATH='C:;SYS:Utilities;Work:bin'`.

## Running Amiga programs

Any program in `C:`, on the `PATH`, on the resident list, or given by a path
runs directly. Arguments are quoted for `ReadArgs()`, so `Copy "my file" TO
RAM:` and `copy 'my file' to=RAM:` both work. Amiga keyword syntax
(`KEYWORD=value`) is passed through. Return codes become `$?` (WARN=5,
ERROR=10, FAIL=20). Scripts with the `s` protection bit are run with `Execute`.
Files starting with `#!/bin/bash` (or `sh`) are run by AmiBash itself.
`ados 'command line'` passes a whole line to the native AmigaDOS shell (useful
for AmigaDOS redirection or wildcards like `#?`).

Exported variables become local Shell variables, so Amiga programs and
scripts see them (`$name` in AmigaDOS, `GetVar()`).

## What's included

* **Language:** everything in normal bash scripts. That covers quoting
  (`'…'`, `"…"`, `$'…'`), all `${…}` operators (defaults, `#`/`%` trimming,
  `/` replacement, substrings, case modification, `@Q`, indirection `${!x}`),
  indexed and associative arrays, `$(( ))` and `(( ))` with full C operator
  set, brace expansion `{a,b}` `{1..10..2}`, globbing (incl. extglob), here-docs
  and here-strings, all redirections (`2>&1`, `&>`, `>|`, `<>`, fd 3-9, `exec`),
  `if/elif`, `while/until`, `for`, `for ((;;))`, `case` (`;;` `;&` `;;&`), `select`,
  functions with `local` and recursion, `[[ ]]` with `=~` and
  `BASH_REMATCH`, `set -euxo pipefail`, `trap` (EXIT, ERR, INT, RETURN),
  `PIPESTATUS`, aliases, `getopts`, `source`, `eval` …
* **Builtins:** `alias bash bg break builtin caller cd command compgen complete
  continue declare dirs echo enable eval exec exit export false fg getopts hash help
  history jobs kill let local logout mapfile popd printf pushd pwd read
  readarray readonly return set sh shift shopt source test times trap true
  type typeset ulimit umask unalias unset wait which [ : . ados`,
  plus Amiga helpers `bashwin`, `palette`, `setfont` and `showkey`.
* **Utilities built in** (so a bare Amiga gets the usual Unix tools): `ls cat
  cp mv rm mkdir rmdir touch chmod find du df stat realpath head tail wc grep
  egrep fgrep sort uniq cut tr sed tee seq rev nl paste tac basename dirname
  sleep date env printenv clear xargs uname whoami hostname`, and `acopy` /
  `apaste`, the Amiga clipboard from the shell like `pbcopy` / `pbpaste` on a
  Mac (`ls | acopy`, `apaste > notes.txt`). They work on
  Amiga paths: `ls -l` shows the Amiga protection bits (`hsparwed`) and file
  comments, and `chmod +s script` / `chmod -d file` set Amiga bits. To use an
  external command of the same name instead, run `enable -n NAME` or call it
  by path (`C:Sort`).
* **Interactive:** emacs-style line editing (cursor keys, Ctrl-A/E/K/U/W/Y/T/L,
  Alt-B/F/D), Shift-Left/Right for start/end of line, history with Up/Down,
  Shift-Up/Down, Ctrl-R search, `!!`, `!$`, `!n`, `^old^new`, persistent
  history in `$HOME/.bash_history`, Tab completion of commands, files,
  `$variables`, volumes and assigns (`Wo<Tab>` → `Work:`), double-Tab lists.
  Ctrl-C interrupts loops and commands. The prompt understands `\u \h \w \W \$
  \t \d \D{fmt} \[ \]` and ANSI colours.

## The AmiBash Terminal (tabs) and Workbench

The Amiga console has no tabs, so AmiBash brings its own terminal window: a
tab bar like macOS Terminal's, each tab running its own bash.

```
 x        bash: Work:        A1 | x       bash: RAM:        A2 | +
```

Double-click `bash` on Workbench, or type `bash --term` in a Shell or at a
bash prompt, to open it (from bash it starts in the background and the
current shell carries on; if a terminal is already open you get a new tab).
If double-clicking gives a plain "Bash" console window instead, the icon
still has an active `WINDOW=` tool type (as `bash.info` from 0.9.2 did):
use the new `bash.info`, or put that tool type in brackets.

| Key / mouse / command | Action |
|---|---|
| **Amiga-T**, click **+**, or `bashwin` | new tab |
| **Amiga-W**, click a tab's **x**, or `exit` / Ctrl-D | close the tab (the last one closes the window) |
| **Amiga-1** … **Amiga-9**, or click a tab | go to that tab |
| **Amiga-[** / **Amiga-]**, or `bashwin -n` | previous / next tab |
| **Amiga-Q** or the close gadget | close all tabs and the window |
| **Amiga-+** / **Amiga--** / **Amiga-0** | bigger / smaller text, default size |
| **Amiga-F** | choose the font (ASL requester, fixed-width fonts) |
| scroller, mouse wheel, **Shift-Up** / **Shift-Down** | scroll back through earlier output (a page at a time with Shift); typing returns to the bottom |
| drag, double-click, triple-click, Shift-click | select text (characters, a word, a line; extend), also into the scrollback |
| **Amiga-C** / **Amiga-V** / **Amiga-A** | copy the selection, paste, select all (the Amiga clipboard) |
| **Amiga-,** | Preferences: profiles, colours, font, start directory |

The shortcuts work with either Amiga key and are also in the *Terminal*,
*Edit* and *View* menus.

**Selection and the clipboard.** Selected text is copied to the clipboard
right away (switch that off per profile) or with Amiga-C; Amiga-V types the
clipboard into the shell. New output clears the selection.

**Preferences and profiles.** *Terminal > Preferences...* (Amiga-,) edits
profiles, named sets of settings like in iTerm: the colours (foreground,
background, bold, cursor, cursor text, selection, selected text and the 16
ANSI colours, with presets such as Solarized Dark/Light, iTerm2, Workbench
and Green screen), "brighten bold text", the font (or automatic), the
directory new tabs start in (home, the previous tab's, or a fixed one),
text sent to the shell at start, scrollback lines and copy-on-select.
Changes show at once; *Use* keeps them for this session, *Save* in
`ENVARC:AmiBash/terminal.prefs`, *Cancel* puts everything back. The
profile marked `*` is the default; *Terminal > New Tab with Profile*
opens a tab with any other, and each tab keeps its own colours. **Fonts.** The terminal uses *AmiBashMono*, supplied in `Fonts/`: DejaVu
Sans Mono drawn separately for every size from 10 to 43 pixels, using the
font's hinting, so text is sharp at every step instead of being a scaled
bitmap. On screens with tall pixels (non-interlaced PAL/NTSC hires) it
picks *AmiBashMonoNL*, the same font drawn for that pixel shape. Copy
`Fonts/#?` to `FONTS:` (or keep the `Fonts` drawer next to `bash`).
**Amiga-+** and **Amiga--** step through the sizes; the window grows or
shrinks to keep its columns and rows, as far as the screen allows. The
choice is saved (`ENVARC:AMIBASH_FONT`, e.g. `AmiBashMono/19`) and used
next time; any fixed-width font works, including outline fonts, which
can be any size.
`tools/ttf2amigafont.py` makes such a family from any monospaced
TrueType font.

**TrueType on RTG.** For smooth, anti-aliased text like a Mac terminal,
use [AAText](https://aminet.net/package/util/wb/AAText) (free, Aminet
`util/wb/AAText`; AmigaOS 3.2, 68020+, RTG). It patches graphics.library
so that every program, the terminal included, draws TrueType fonts
anti-aliased on 15-bit and deeper RTG screens; fixed-width fonts keep
their character cells, so the terminal grid stays exact. Install a
monospaced TrueType font with AATextManager, FTManager or the supplied
`InstallTTF` (Shell, no ReAction needed: `InstallTTF Fonts/DejaVuSansMono.ttf COPY`;
see `contrib/installttf`), e.g. `DejaVuSansMono.ttf` from `Fonts/`, and start AAText (`Run >NIL: C:AAText`
in `S:Startup-Sequence` before `LoadWB`). On an RTG screen the terminal
then picks such a font by itself (DejaVu Sans Mono, JetBrains Mono,
Liberation Mono, Ubuntu Mono, ... or any installed font with "Mono" in
its name), and Amiga-+/- zoom it a pixel or two at a time. Without AAText
the same font is still drawn, just without anti-aliasing. On palette
(AGA/ECS) screens the terminal stays with the AmiBashMono bitmap fonts.
RTG and palette screens remember their font separately:
`ENVARC:AMIBASH_RTGFONT` and `ENVARC:AMIBASH_FONT`.
Bold uses the font's bold face when one is installed under the same name
plus " Bold" (e.g. `InstallTTF DejaVuSansMono-Bold.ttf COPY`); without
one, TrueType bold is shown by colour alone and bitmap fonts are
overprinted. Colours are the 16 standard ANSI colours
(exact pens on RTG and 8-bit screens, the nearest on 4-colour ones).

Each tab keeps the last 300 lines that scrolled off the top; set
`AMIBASH_SCROLLBACK` (e.g. `setenv AMIBASH_SCROLLBACK 1000`, or 0 for none)
before starting the terminal to change that. While you are scrolled back
the view stays put as new output arrives. A tab's title (and the window's) follows its current directory. The
terminal understands the ANSI/xterm sequences bash and its tools use:
colours, cursor movement, erasing, and window titles (`ESC ] 0 ; title BEL`).
Inside it, `$TERM_PROGRAM` is `AmiBash`.

The binaries are *pure* (re-entrant): every tab runs the code that is already
loaded, so a tab only costs its own data, stack and heap (well under 100 KB),
not another copy of the 400 KB program. For the same reason `bash` can be
made resident: `Resident C:bash PURE`. (A 1 MB Amiga has room for one tab;
2 MB runs several.)

* **Workbench:** put `bash.info` (supplied) next to `bash` and double-click it.
  To get a plain console window instead of the terminal, enable the icon's
  tool type `WINDOW=CON:x/y/w/h/Bash/CLOSE` (remove the brackets). Avoid
  Workbench's *Execute Command*: its "Output window" waits for you to close
  it after any program ends.
* **Shell window:** typing `bash` runs inside that Shell window, which is
  retitled "Bash" while it runs; `exit` returns to the Shell prompt, as bash
  in a terminal does. There, **F1** or `bashwin` opens another Bash console
  window and **F2** or `bashwin -n` brings the next one to the front. To make
  bash the default, add `bash` at the end of `S:Shell-Startup`.
* `uname -a` names the build, e.g. `AmigaOS amiga 3.2 AmiBash 0.1 (68020
  build) 21.10.2026 m68k 68030 Amiga AmigaOS`: Kickstart release, AmiBash
  version and build, CPU. `bash --version` and `$BASH_VERSION` too.

## ssh

The archive's `SSH` drawer has **ssh** and **ssh-keygen** from OpenSSH 10.6p1,
the client and key generator, for a 68020 or better with a TCP/IP stack
(Roadshow, AmiTCP, Miami or Genesis). They're built without OpenSSL:

* **Algorithms:** ed25519 keys; curve25519 and the post-quantum sntrup761
  and ML-KEM key exchanges; ChaCha20-Poly1305 and AES-CTR. That covers any
  current OpenSSH server, but not hosts that only offer RSA.

```
ssh user@host                  # interactive, best in the AmiBash Terminal
ssh user@host 'df -h'          # a command
ssh-keygen -t ed25519          # $HOME/.ssh/id_ed25519
```

* **Files:** `~/.ssh` is `$HOME/.ssh`, which is `SYS:.ssh` unless `HOME` is
  set. System-wide `ssh_config` and `ssh_known_hosts` go in `ENVARC:ssh`.
* **Terminal:** the remote side gets `TERM=ansi`. Full-screen programs such
  as `less` work in the AmiBash Terminal.
* **Not supported:** whatever needs `fork()`, namely ProxyCommand,
  ControlMaster, `-f`, ssh-agent and X11 forwarding, as well as `scp`/`sftp`.

`contrib/ssh/README.md` has the details, including how random numbers are
made without `/dev/urandom` and how to build it.

## Line editing, colours and fonts

Editing keys: Up/Down history (Shift-Up/Down: oldest/newest), Left/Right,
Shift-Left/Right or Ctrl-A/Ctrl-E for start/end of line, Ctrl-B/F, Ctrl-K/U/W
(kill), Ctrl-Y (yank), Ctrl-T, Ctrl-L (clear), Ctrl-D, Ctrl-R (search), Tab.

If these keys do nothing:

* run `showkey` and press them. On an Amiga console Up should print
  `9B 41 CSI A` and Ctrl-A `01 ^A`. If nothing appears, the key never
  reaches the Amiga. **FS-UAE maps the cursor keys and Right-Ctrl to an
  emulated joystick by default**: set `joystick_port_1 = none` (and
  `joystick_port_0 = mouse`) in the config, or use the Left Ctrl key.
* if bash prints "this console has no raw mode", the console (or vamos)
  can't deliver single keys, so bash falls back to plain line input.

Colours: the console understands ANSI colours (`ESC[30m`…`ESC[37m` = pens
0–7, plus bold, italic, underline and inverse), so coloured prompts, `ls
--color` and `grep --color` work. On a 4-colour Workbench only pens 0–3
differ.

* `palette` lists the screen's pens, and `palette 3 ff8800` sets one
  (12-bit `f80` also works; quote a leading `#`). This changes the screen
  palette, as Prefs/Palette does.
* `setfont` shows the console font, and `setfont topaz 9` / `setfont
  xen 8` switch the window to any fixed-width font in `FONTS:`. It uses
  diskfont.library when available, or loads the font file itself. For a
  permanent change set the "System Default Text" font in Prefs/Font.
* `tools/pcf2amigafont.py` converts Linux PCF bitmap fonts into Amiga
  fonts.

## Known differences from GNU bash

* No real concurrency: pipeline stages run one after another, so
  `producer | consumer` pairs that rely on running at the same time (`yes |
  head`, `tail -f | grep`) don't work. Background `&` is only real for
  external commands. `wait`, `jobs`, `fg` and `bg` are minimal.
* No job control or POSIX signals. Ctrl-C (`SIGINT`) is the only signal; `trap … INT`
  works. `kill N` sends Ctrl-C to Amiga CLI process `N`.
* `>( cmd )` process substitution is not supported (`<( cmd )` is).
* Amiga file systems have no symlink-aware `cd -P`, no uid/gid and no Unix
  modes. `-x` is true for files with the `e` bit, which is most of them.
* Arithmetic is 64-bit as in bash; floating point appears only in `printf %f`.

The full list, as found by the test corpus, is in
`tests/compat/KNOWN_DIFFERENCES.md`.

## Building

You need [vbcc](http://sun.hasenbraten.de/vbcc/) with the m68k-amigaos target,
vasm, vlink, and the AmigaOS NDK 3.2 headers (Aminet `dev/misc/NDK3.2`).
[toolchain/README.md](toolchain/README.md) shows how to set them all up on
Linux, along with the FS-UAE and AROS test setup.

```
make pure         # build/bash.pure     (68000, re-entrant: the released bash)
make pure020      # build/bash020.pure  (68020+, re-entrant)
make amiga        # build/bash      (68000, ordinary C start-up)
make amiga020     # build/bash020   (68020+)
make              # build/amibash   Linux build of the same code, for testing
make test         # compare against GNU bash on the test corpus
```

Testing without real hardware:

* `vamos -- build/bash SELFTEST` runs the built-in self test under the
  amitools `vamos` emulator (pip `amitools`, `machine68k==0.3.0`). Note the `--`.
* `emu/run.sh a1200|a500` boots FS-UAE with its built-in AROS ROM in a
  virtual X display (`Xvfb :99`), with `emu/dh0` as `DH0:` (bash in `C:`).
  `emu/type.sh 'command' shot.png` types a line and takes a screenshot.
  FS-UAE's keyboard joystick emulation is turned off in the configs so the
  cursor keys reach the Amiga.

## Licence

GPL-3.0-or-later.
