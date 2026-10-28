# Vim for AmigaOS 3.x on a 68020 or better, with bebbo's m68k-amigaos-gcc
# and libnix (toolchain/README.md, section 7), from Vim's own Amiga port
# plus vim-amiga.patch:
#   - the size of AmiBash's Terminal (and other consoles without a
#     console.device unit) from the window bounds report, $LINES/$COLUMNS
#     as a last resort, so Vim fills the whole window and follows resizes
#   - functions Vim 9.2 needs that the Amiga port lacks
#
#   make          fetch Vim $(VIM_TAG), patch it, build vim and the runtime
#   make clean
#
# To move to a newer Vim: set VIM_TAG and VIM_COMMIT (git ls-remote --tags
# $(VIM_GIT) $(VIM_TAG)^{}), make clean all, and refresh the patch if it no
# longer applies.

VIM_TAG    := v9.2.1252
VIM_COMMIT := 90d3806e3986da398373a5720b57bf2df9d0b690
VIM_GIT    := https://github.com/vim/vim.git
AMIGCC     ?= /home/user/toolchain/amiga
SRC        := vim-src

PATCHLEVEL := $(patsubst v%,%,$(VIM_TAG))
PATCHLEVEL := $(lastword $(subst ., ,$(PATCHLEVEL)))

all: vim runtime/.done

CC     := $(AMIGCC)/bin/m68k-amigaos-gcc
CFLAGS := -O2 -m68020 -noixemul -std=gnu99 -fno-strict-aliasing -DAMIGA \
	  -DNO_ARP -DUSE_TMPNAM -DHAVE_TGETENT -DHAVE_TERMCAP -DNEW_SHELLSIZE \
	  -DWORDS_BIGENDIAN -DHAVE_ERRNO_H -DHAVE_MATH_H \
	  -DFEAT_BROWSE -DFEAT_MOUSE -DFEAT_HUGE \
	  -DPATCHLEVEL=\"$(PATCHLEVEL)\" -DBUILDDATE=\"$(shell date +%d.%m.%Y)\" \
	  -I$(SRC)/src -I$(SRC)/src/proto -Wno-attributes -w

# the sources are the ones Vim's Make_ami.mak lists (its conditionals do
# not parse with GNU make 4, so it is not run itself)
SRCS = $(shell sed -n '/^SRC +=/,/^$$/{s/^[[:space:]]*//;s/[[:space:]]*\\$$//;/\.c$$/p}' $(SRC)/src/Make_ami.mak)
OBJS = $(addprefix build/,$(SRCS:.c=.o))

vim: $(SRC)/.patched
	$(MAKE) build/vim
	$(AMIGCC)/bin/m68k-amigaos-strip build/vim -o $@

build/vim: $(OBJS)
	$(CC) -noixemul -o $@ $(OBJS) -lm

build/%.o: $(SRC)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# the runtime files the package carries (see runtime.list)
runtime/.done: $(SRC)/.patched runtime.list
	rm -rf runtime
	cd $(SRC)/runtime && grep -v '^#' ../../runtime.list | xargs -I{} sh -c \
	    'for f in {}; do mkdir -p ../../runtime/$$(dirname $$f); cp $$f ../../runtime/$$f; done'
	touch $@

$(SRC)/.patched: vim-amiga.patch
	rm -rf $(SRC)
	git -c advice.detachedHead=false clone -q --depth 1 --branch $(VIM_TAG) $(VIM_GIT) $(SRC)
	test "$$(git -C $(SRC) rev-parse HEAD)" = $(VIM_COMMIT)
	cd $(SRC) && patch -s -p1 <../vim-amiga.patch
	touch $@

clean:
	rm -rf $(SRC) build runtime vim

.PHONY: all clean
