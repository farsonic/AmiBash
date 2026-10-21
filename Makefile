# AmiBash - bash-compatible shell for AmigaOS
#   make            host (Linux) build for testing  -> build/amibash
#   make amiga      AmigaOS 68000 build with vbcc  -> build/bash
#   make amiga020   AmigaOS 68020 build            -> build/bash020
#   make pure       resident ("pure") 68000 build  -> build/bash.pure
#   make pure020    resident 68020 build           -> build/bash020.pure
#   make test       run the comparison suite against GNU bash

SRCS = util.c io.c path.c parse.c pattern.c expand.c arith.c vars.c exec.c \
       test.c builtins.c lineedit.c main.c selftest.c regex.c timeutil.c \
       utils_fs.c utils_text.c utils_misc.c
HOSTSRCS = $(SRCS) os_posix.c
AMIGASRCS = $(SRCS) os_amiga.c term_amiga.c term_prefs.c

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
	$(VC) $(AFLAGS) -DAMIBASH_CPU=68000 $(addprefix src/,$(AMIGASRCS)) -o $@ -lmsoft -lamiga

amiga020: build/bash020
build/bash020: $(addprefix src/,$(AMIGASRCS)) src/sh.h src/os.h
	@mkdir -p build
	$(VC) $(AFLAGS) -cpu=68020 -DAMIBASH_CPU=68020 $(addprefix src/,$(AMIGASRCS)) -o $@ -lmsoft -lamiga

# resident builds: shared code, per-run data (vbcc minres start-up)
PFLAGS = $(subst +aos68k ,+aos68kr ,$(AFLAGS)) -DAMIBASH_PURE
PURESRCS = $(AMIGASRCS) pure_amiga.c

pure: build/bash.pure
build/bash.pure: $(addprefix src/,$(PURESRCS)) src/sh.h src/os.h
	@mkdir -p build
	$(VC) $(PFLAGS) -DAMIBASH_CPU=68000 $(addprefix src/,$(PURESRCS)) -o $@ -lmsofts -lamigas >$@.log 2>&1 || (cat $@.log; rm -f $@; false)
	@cat $@.log; if grep -q "Absolute reference" $@.log; then rm -f $@; echo "absolute data references: not pure"; false; fi

pure020: build/bash020.pure
build/bash020.pure: $(addprefix src/,$(PURESRCS)) src/sh.h src/os.h
	@mkdir -p build
	$(VC) $(PFLAGS) -cpu=68020 -DAMIBASH_CPU=68020 $(addprefix src/,$(PURESRCS)) -o $@ -lmsofts -lamigas >$@.log 2>&1 || (cat $@.log; rm -f $@; false)
	@cat $@.log; if grep -q "Absolute reference" $@.log; then rm -f $@; echo "absolute data references: not pure"; false; fi

test: build/amibash
	tests/run_compat.sh

clean:
	rm -rf build
.PHONY: all amiga amiga020 test clean
