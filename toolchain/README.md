# Amiga development environment

How the cross-compilers, headers and emulators that build and test AmiBash
were set up on a Linux machine (Ubuntu 24.04, x86-64), such as a Claude Code
cloud session. Everything goes under `/home/user/toolchain`; the AmiBash
`Makefile` expects that path (override with `make VBCC=... NDK=...`).

| Part | What for | Where |
|---|---|---|
| vbcc 0.9h, vasm, vlink | the C compiler, assembler and linker for AmiBash and InstallTTF | `toolchain/vbcc` |
| NDK 3.2 R4 | AmigaOS headers and link libraries | `toolchain/ndk` |
| amitools (`vamos`) | runs Amiga Shell programs on Linux | pip |
| FS-UAE + AROS m68k | a whole Amiga with an RTG screen, to test the terminal | apt, `/tmp/claude-0/arosfs` |
| Xvfb, xdotool, ImageMagick | a virtual display, typing and screenshots for FS-UAE | apt |
| lha, zip | release archives | apt |
| InstallerLG | runs `dist/Install-AmiBash` on Linux | built from source |
| bebbo's m68k-amigaos-gcc | `ssh` and `ssh-keygen` (`contrib/ssh`), and other ports that need gcc and libnix | `toolchain/amiga` |

## 1. System packages

```sh
apt-get update
apt-get install -y build-essential git wget python3-pip \
    lhasa jlha-utils zip unzip \
    fs-uae xvfb xdotool imagemagick
```

## 2. vbcc, vasm and vlink

The sources come from the authors' sites (Volker Barthelmann and Frank Wille):

```sh
mkdir -p /home/user/toolchain/{dl,src,vbcc/bin} && cd /home/user/toolchain/dl
wget http://phoenix.owl.de/tags/vbcc0_9h.tar.gz
wget http://phoenix.owl.de/vbcc/current/vbcc_target_m68k-amigaos.lha
wget http://phoenix.owl.de/vbcc/current/vbcc_unix_config.tar.gz
wget http://sun.hasenbraten.de/vasm/release/vasm.tar.gz
wget http://sun.hasenbraten.de/vlink/release/vlink.tar.gz

cd ../src
tar xzf ../dl/vbcc0_9h.tar.gz && tar xzf ../dl/vasm.tar.gz && tar xzf ../dl/vlink.tar.gz
```

**vbcc.** The build first makes `dtgen`, which asks about the host's data
types. These answers (from bebbo's amiga-gcc build) work on x86-64:

```sh
cd /home/user/toolchain/src/vbcc && mkdir -p bin
make TARGET=m68k bin/dtgen
printf 'y\ny\nsigned char\ny\nunsigned char\nn\ny\nsigned short\nn\ny\nunsigned short\nn\ny\nsigned int\nn\ny\nunsigned int\nn\ny\nsigned long long\nn\ny\nunsigned long long\nn\ny\nfloat\nn\ny\ndouble\n' \
  | bin/dtgen machines/m68k/machine.dt machines/m68k/dt.h machines/m68k/dt.c
make TARGET=m68k
cp bin/vbccm68k bin/vc bin/vprof bin/dtgen /home/user/toolchain/vbcc/bin/
```

**vasm and vlink:**

```sh
cd /home/user/toolchain/src/vasm  && make CPU=m68k SYNTAX=mot
cp vasmm68k_mot vobjdump /home/user/toolchain/vbcc/bin/
cd /home/user/toolchain/src/vlink && make
cp vlink /home/user/toolchain/vbcc/bin/
```

**The m68k-amigaos target and the `vc` configs:**

```sh
cd /home/user/toolchain/vbcc
lha xq ../dl/vbcc_target_m68k-amigaos.lha      # gives vbcc_target_m68k-amigaos/
mkdir -p targets && mv vbcc_target_m68k-amigaos/targets/m68k-amigaos targets/
tar xzf ../dl/vbcc_unix_config.tar.gz          # gives config/ (aos68k, aos68kr, ...)
```

AmiBash uses these configs: `+aos68k` for ordinary programs and `+aos68kr`
for the pure (resident) build.

**Environment**, for using `vc` by hand (the Makefiles set it themselves):

```sh
export VBCC=/home/user/toolchain/vbcc
export PATH=$VBCC/bin:$PATH
```

To check it, build hello world: `vc +aos68k hello.c -o hello`.

## 3. NDK 3.2

Release 4 of the NDK for AmigaOS 3.2, from Aminet `dev/misc/NDK3.2.lha`:

```sh
cd /home/user/toolchain/dl && wget https://aminet.net/dev/misc/NDK3.2.lha
mkdir -p ../ndk && cd ../ndk && lha xq ../dl/NDK3.2.lha
```

The C headers are in `ndk/Include_H`. vbcc is given `-I$(NDK)/Include_H`.
The headers work for 2.04 to 3.1.4 programs too, as long as you only call
what the older OS has.

## 4. vamos (amitools)

```sh
pip install amitools machine68k==0.3.0
vamos -- build/bash SELFTEST          # from the amibash directory
```

`vamos` runs a Shell program on Linux with emulated exec and dos libraries.
That is enough for bash's self test, but not for Intuition.

## 5. FS-UAE with AROS

The terminal, fonts and installer need a real Workbench screen. FS-UAE
(apt) runs AROS m68k, which is free and has its own Kickstart ROM:

* `emu/a1200.fs-uae` and `emu/a500.fs-uae` use FS-UAE's built-in AROS ROM
  and boot `emu/dh0` as `DH0:`.
* `emu/arosrtg.fs-uae` is an A4000/040 with a UAEGFX RTG card and 64 MB of
  Zorro III RAM. It boots a full AROS system from `/tmp/claude-0/arosfs`
  with `emu/dh0` as `Work:`. AAText and TrueType fonts are tested on this
  one.

**Making `arosfs`.** Download an AROS nightly `amiga-m68k-boot-iso` build,
e.g. `AROS-20261020-amiga-m68k-boot-iso.zip` from the
[AROS nightly builds](https://sourceforge.net/projects/aros/files/nightly2/).
Then copy the files off the ISO with pycdlib, since 7z and bsdtar aren't
installed:

```sh
pip install pycdlib
mkdir -p /tmp/claude-0/aros && cd /tmp/claude-0/aros
unzip -q AROS-*-amiga-m68k-boot-iso.zip
python3 - <<'EOF'
import os, pycdlib
iso = pycdlib.PyCdlib(); iso.open('AROS-20261020-amiga-m68k-boot-iso/aros-amiga-m68k.iso')
out = '/tmp/claude-0/arosfs'
for root, dirs, files in iso.walk(rr_path='/'):
    os.makedirs(out + root, exist_ok=True)
    for f in files:
        iso.get_file_from_iso(out + root.rstrip('/') + '/' + f, rr_path=root.rstrip('/') + '/' + f)
EOF
```

The Kickstart files are `arosfs/boot/amiga/aros-rom.bin` and
`aros-ext.bin`, which `arosrtg.fs-uae` points at.

To run tests automatically, add this to the end of `arosfs/S/User-Startup`.
It runs `Work:autorun` once `Work:` is mounted:

```
Lab waitwork
If NOT EXISTS Work:
    Wait 1
    Skip waitwork BACK
EndIf
If EXISTS Work:autorun
    Run >NIL: Execute Work:autorun
EndIf
```

**The test scripts** (in `amibash/emu`):

| Script | What it does |
|---|---|
| `run.sh a1200` (or `arosrtg`) | starts `Xvfb :99` if needed and boots FS-UAE in it |
| `type.sh 'text' shot.png` | types a line plus Return, then takes a screenshot |
| `aros_run.sh SCRIPT [TIMEOUT]` | boots the RTG setup, runs `SCRIPT` (AmigaDOS commands) as `Work:autorun`, waits for `Work:autorun.done`, takes a screenshot (`/tmp/claude-0/aros_last.png`) and stops FS-UAE |

End every autorun script with `Echo >Work:autorun.done done`. A black
screenshot after a timeout usually means AROS crashed.

Tips:

* xdotool `super+<key>` is Left-Amiga.
* Mouse clicks need `xdotool mousedown 1`, a short pause, then `mouseup 1`.
* `mouse_speed = 300` in the config makes pointer movement predictable.
* AROS's freetype2.library sometimes hangs on DejaVu fonts; use
  `ENGINE=aatext` with InstallTTF.

## 6. InstallerLG (optional)

AROS's own Installer fails on every script here, so `dist/Install-AmiBash`
is tested with InstallerLG built for Linux. Get its source archive from
Aminet (`util/sys`, InstallerLG 1.0.3), unpack it, and run:

```sh
cd InstallerLG-1.0.3/build && make INTERFACE=CLI
```

## 7. bebbo's m68k-amigaos-gcc

bash itself is built with vbcc, but `contrib/ssh` (OpenSSH) and `contrib/vim` are built with
gcc and libnix, so `dist/make_release.sh` needs it. The build takes about
20 minutes with 4 cores and 4 GB of disk.

```sh
apt-get install -y gperf texinfo rsync libgmp-dev libmpfr-dev libmpc-dev bison flex autoconf
cd /home/user/toolchain
git clone https://codeberg.org/bebbo/amiga-gcc     # no longer on GitHub
cd amiga-gcc && make min ndk -j4 PREFIX=/home/user/toolchain/amiga
```

The result is `/home/user/toolchain/amiga/bin/m68k-amigaos-gcc`; link with
`-noixemul` for libnix.

Two things to know about libnix when porting:

* **`__stack`:** setting it alone doesn't give the program a bigger stack.
  Also refer to `__stkinit` (`void *ref = __stkinit;`), or the startup code
  that switches stacks isn't linked in.
* **Socket calls:** libnix's `getaddrinfo()`/`getnameinfo()` call
  Roadshow-only `bsdsocket.library` functions without checking that they
  exist, so avoid them on other TCP/IP stacks.

To test networking, add `bsdsocket_library = 1` to an FS-UAE config. That
gives the Amiga a `bsdsocket.library` that uses the host's network (the
RTG config `emu/arosrtg.fs-uae` has it), so a test `sshd` on the host is
reachable at 127.0.0.1.

## 8. The reference GNU bash

The test corpus compares AmiBash with the GNU bash release it follows
(`UPSTREAM_BASH`). `make upstream` downloads that release and its official
patches from ftp.gnu.org, checks their signatures with `gpgv` against the
GNU keyring, and builds it in `toolchain/bash-ref` (beside the repository):

```
sudo apt install gpgv build-essential bison    # gpgv, a C compiler, make
make upstream
```

`tests/run_compat.sh` then uses `toolchain/bash-ref/bash` by default.

## In a cloud session

* The container is reset when the session ends, so everything above has to
  be installed again. A SessionStart hook or the environment's setup script
  can do that automatically.
* Downloads use the session's proxy. phoenix.owl.de, sun.hasenbraten.de,
  aminet.net, sourceforge.net, codeberg.org and franke.ms were all reachable
  here.
* `/tmp/claude-0` doesn't survive a new session either: unpack AROS again,
  or keep `arosfs` somewhere else and change `emu/arosrtg.fs-uae`.
