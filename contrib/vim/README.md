# Vim for AmigaOS

Vim 9.2 (patch 1252) for AmigaOS 3.x on a 68020 or better. It's the "huge"
build, without the GUI, built from Vim's own Amiga port with a few changes.

## In the AmiBash Terminal

* **Window size:** Vim uses the whole window. When the window is resized, or
  the text gets bigger or smaller, Vim redraws at the new size.
* **Colours:** syntax highlighting works, as far as the screen has
  colours (a 4-colour Workbench shows few).

The terminal gives Vim its size in the same ways as the Amiga console:

* **`ACTION_DISK_INFO` packet:** the reply carries a console unit holding the
  window's columns and rows.
* **Window bounds report:** the terminal answers `CSI 0 SPACE q`.
* **Resize events:** sent after `CSI 12 {`.

That also helps other console programs, including the Vim on Aminet.

## Installing

`Install-AmiBash` does this when you tick "Vim 9.2". By hand:

```
Copy Vim/vim C:
Copy Vim/runtime ALL TO <drawer>      ; e.g. SYS:Utilities/Vim
Assign VIM: <drawer>                  ; also in S:User-Startup
```

If you already have a Vim from Aminet, copy the runtime files into its `VIM:`
drawer. They replace older files with the same names, and the 9.2 binary
expects them. The Installer keeps an existing `VIM:vimrc` as `vimrc.old`.

## Memory, text and Markdown

* **Memory:** it runs in 8 MB of Fast RAM; Vim and an open file take about
  3 to 4 MB. Vim on the Amiga uses the backtracking regexp engine
  (`regexpengine=1`) by default. With the NFA engine, file type detection
  alone took over 10 MB and many seconds on a 68030, and failed with
  "Out of memory" on an 8 MB machine.
* **Text encoding:** `VIM:vimrc` makes Vim work in UTF-8 and show Latin-1,
  the Amiga's character set. UTF-8 and Latin-1 files both open correctly
  (`café` in either); characters Latin-1 doesn't have, like an em dash,
  show as `¿`. New files are written in Latin-1.
* **Markdown:** `VIM:vimfiles/after/syntax/markdown.vim` stops Vim marking
  `_` inside words (`os_amiga.c`) as errors, as GitHub's Markdown does.

## What is included

The runtime files are Vim's own, without its test files:

* `defaults.vim` and the rest of the startup scripts;
* file type detection;
* syntax, indent and file type plugins for every file type Vim knows, with
  the autoload scripts they use;
* compiler settings and the standard plugins;
* all the colour schemes;
* the complete help (`:help`).

Left out: the GUI menus, keymaps, spell files, translations and the tutor.

Put your settings in `S:.vimrc`, `VIM:.vimrc` or `HOME:.vimrc`.

## Building

```
make -C contrib/vim
```

This needs bebbo's m68k-amigaos-gcc (see `toolchain/README.md`, section 7). It:

1. clones the Vim tag named in the Makefile and checks its commit;
2. applies `vim-amiga.patch`;
3. builds `vim` and collects the runtime files listed in `runtime.list`.

To move to a newer Vim:

1. Set `VIM_TAG` and `VIM_COMMIT` in the Makefile.
2. Run `make clean all`.
3. If `vim-amiga.patch` no longer applies, update it.

What `vim-amiga.patch` changes:

* **`os_amiga.c`, window size:** when a console has no console.device unit,
  Vim asks it with the window bounds report, then falls back to
  `$LINES`/`$COLUMNS`.
* **`os_amiga.c`, missing functions:** `mch_calc_cell_size()` and
  `mch_get_cmd_output_direct()`, which Vim 9.2 needs and the Amiga port
  doesn't have.
* **`os_amiga.c`, `--version`, `--help` and `-es`:** these no longer open a
  window.
* **`term.c`:** 8 ANSI colours with the `amiga` terminal when
  `TERM_PROGRAM=AmiBash`.
* **`optiondefs.h`:** `regexpengine` defaults to 1 on the Amiga.
