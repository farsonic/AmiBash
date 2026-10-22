# top for AmigaOS, with vbcc and the NDK (as bash)
VBCC ?= /home/user/toolchain/vbcc
NDK  ?= /home/user/toolchain/ndk/Include_H
export VBCC
export PATH := $(VBCC)/bin:$(PATH)

# tasktime.c, tasktime.h and tthooks.s are SysPulse's: keep them the same
top: top.c tasktime.c tasktime.h tthooks.s
	vasmm68k_mot -quiet -Fhunk -o tthooks.o tthooks.s
	vc +aos68k -c99 -O1 -I$(NDK) top.c tasktime.c tthooks.o -o $@ -lamiga

clean:
	rm -f top tthooks.o
