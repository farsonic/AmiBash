VBCC ?= /home/user/toolchain/vbcc
NDK  ?= /home/user/toolchain/ndk/Include_H
export VBCC
export PATH := $(VBCC)/bin:$(PATH)

InstallTTF: installttf.c charsets.h
	vc +aos68k -c99 -O1 -I$(NDK) installttf.c -o $@ -lamiga

charsets.h: mkcharsets.py
	python3 mkcharsets.py
