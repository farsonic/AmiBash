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

1. Copy `bash` (68000 build) or `bash020` (68020+, renamed to `bash`) to `C:`.
2. Optionally copy `S/bashrc` to `S:bashrc` (system-wide startup file).
3. In a Shell window type `bash`. To make it the default for new Shell
   windows, put `bash` as the last line of `S:Shell-Startup`.

Requirements: AmigaOS 2.04 (V37) or newer, or AROS m68k. A 68000 with 1 MB
works (tested on an emulated A500). The executable is about 350 KB. The
shell asks for a 64 KB stack; for very deep recursion use e.g.
`Stack 200000` before starting it.

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
  plus Amiga console helpers `palette`, `setfont` and `showkey`.
* **Utilities built in** (so a bare Amiga gets the usual Unix tools): `ls cat
  cp mv rm mkdir rmdir touch chmod find du df stat realpath head tail wc grep
  egrep fgrep sort uniq cut tr sed tee seq rev nl paste tac basename dirname
  sleep date env printenv clear xargs uname whoami hostname`. They work on
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

```
make amiga        # build/bash      (68000)
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
