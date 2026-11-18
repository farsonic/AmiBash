# AmiBash 0.3

A bash‑compatible shell for AmigaOS 3.x, with its own tabbed terminal. This release follows **GNU bash 5.3.20** and adds **Vim 9.2**, a much richer terminal, and **OpenSSH 10.6p2**.

## Downloads

| File | What it is |
|---|---|
| `AmiBash-0.3.lha` | The full package with the Installer. Double‑click `Install-AmiBash`. |
| `AmiBash-0.3-bin.zip` | The same files without the sources, for unpacking on another machine. |

`amibash-update INSTALL` (68020+, AmiSSL v5) finds and installs this release automatically.

## What's new

### bash 5.3 compatibility
AmiBash now follows **GNU bash 5.3.20**: `bash --version` and `BASH_VERSINFO` say so, and the compatibility tests are checked against a real bash 5.3.20.

New bash 5.3 features:
- `${ command; }` and `${| command; }`: command substitution without a subshell.
- `GLOBSORT`: sort glob results by name, size or time.
- `BASH_MONOSECONDS`, `BASH_TRAPSIG`, `source -p PATH`, `printf %Q`, `compgen -V`, `read -E`, `trap -P`.
- New `shopt` options, and bash 5.3's error messages.

### Vim 9.2 (68020+)
A current **Vim 9.2.1364** (the "huge" build) with its runtime files and the complete `:help`. It's an Installer option.
- **Window size:** in the AmiBash Terminal, Vim uses the whole window and follows resizes and text‑size changes.
- **Colours:** syntax highlighting in colour.
- **Text:** UTF‑8 files display correctly; new files are written in Latin‑1 like other Amiga programs.
- **Memory:** runs in **8 MB** of Fast RAM.
- **Fixes:** a hang on `.txt` files caused by a Vim bug on 68k CPUs, cursor keys inserting `<9b>A`, and red error boxes in Markdown.
- **Install:** an existing `VIM:` (e.g. from the Aminet Vim) is updated; your `VIM:vimrc` is kept as `vimrc.old`.

### The AmiBash Terminal
- **Amiga Shell in a tab:** **Amiga‑S** switches a tab between bash and the Amiga Shell (`EndShell` returns). There's a new `amigashell` builtin and a ready‑made *Amiga Shell* profile; its tabs close on `EndShell`, like a Shell window.
- **Preferences in the iTerm style:**
  - **Colors page:** Basic, Cursor and ANSI normal/bright swatches.
  - **Presets page:** a preview and **916 colour schemes** from the iTerm2‑Color‑Schemes gallery.
  - **Import…** reads iTerm2 `.itermcolors` files.
  - **Online…** fetches the newest gallery (`amibash-update SCHEMES`).
  - **P** / **Shift‑P** change page; the cursor keys and Return pick a scheme.
- **Transparent background:** with **NewOS** running and its transparency on, the terminal background is NewOS glass (Dither on any screen, Alpha/Composite on RTG). It follows NewOS Preferences as they change; without NewOS, nothing changes.
- **Window size for console programs:** the terminal tells console programs its real size and resizes, the way `CON:` does. That also helps the Aminet Vim, `less` and others. bash keeps `$COLUMNS` and `$LINES` up to date.

### ssh
**OpenSSH 10.6p2** client and key generator (68020+), with the post‑quantum `ssh-mldsa44-ed25519` CA signatures and the 10.6p2 fixes.

## Requirements
- AmigaOS 3.1.4 or 3.2 (also runs on 2.04+ and AROS m68k). bash runs on a 68000; Vim, ssh and amibash‑update need a 68020 or better.
- **Vim's speed:** on a 14 MHz 68030, Vim's start‑up scripts take about 20 seconds. A faster accelerator divides that. For a quick editor, put your own `S:.vimrc`: Vim then skips file‑type detection and syntax highlighting unless you turn them on.
- **ssh, Online… and amibash-update** need a TCP/IP stack. Online… and amibash-update also need AmiSSL v5.

## Upgrading from 0.2
Run `amibash-update INSTALL`, or unpack the archive and run the Installer. Your terminal profiles are kept. New profiles have *Transparent background* on; it only takes effect while NewOS glass is available.

## Tested
On AmigaOS 3.1.4 (A1200, 68030, 8 MB Fast RAM, in FS‑UAE):
- the Installer, bash and the terminal;
- shell switching and the Preferences pages;
- Vim (file types, cursor keys, the Vim9 checks);
- ssh login and `ssh-keygen`;
- NewOS glass in Dither mode, using a stand‑in for NewOS.

Not tested on real hardware or with NewOS's Alpha/Composite modes.
