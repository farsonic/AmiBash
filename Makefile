# AmiBash - bash-compatible shell for AmigaOS
#   make            host (Linux) build for testing  -> build/amibash
#   make amiga      AmigaOS 68000 build with vbcc  -> build/bash
#   make amiga020   AmigaOS 68020 build            -> build/bash020
#   make test       run the comparison suite against GNU bash

SRCS = util.c io.c path.c parse.c pattern.c expand.c arith.c vars.c exec.c \
       test.c builtins.c lineedit.c main.c selftest.c regex.c timeutil.c \
       utils_fs.c utils_text.c utils_misc.c
HOSTSRCS = $(SRCS) os_posix.c
AMIGASRCS = $(SRCS) os_amiga.c

VBCC ?= /home/user/toolchain/vbcc
NDK ?= /home/user/toolchain/ndk
VC = PATH=$(VBCC)/bin:$$PATH VBCC=$(VBCC) vc

CFLAGS = -g -O1 -Wall -Wno-unused-function
AFLAGS = +aos68k -rmcfg-amiga-softfloat -c99 -O2 -DAMIGA -dontwarn=153 -I$(NDK)/Include_H -Isrc

all: build/amibash

build/amibash: $(addprefix src/,$(HOSTSRCS)) src/sh.h src/os.h
	@mkdir -p build
	gcc $(CFLAGS) -Isrc -o $@ $(addprefix src/,$(HOSTSRCS))

amiga: build/bash
build/bash: $(addprefix src/,$(AMIGASRCS)) src/sh.h src/os.h
	@mkdir -p build
	$(VC) $(AFLAGS) $(addprefix src/,$(AMIGASRCS)) -o $@ -lmsoft -lamiga

amiga020: build/bash020
build/bash020: $(addprefix src/,$(AMIGASRCS)) src/sh.h src/os.h
	@mkdir -p build
	$(VC) $(AFLAGS) -cpu=68020 $(addprefix src/,$(AMIGASRCS)) -o $@ -lmsoft -lamiga

test: build/amibash
	tests/run_compat.sh

clean:
	rm -rf build
.PHONY: all amiga amiga020 test clean
