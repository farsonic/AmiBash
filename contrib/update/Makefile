# amibash-update for AmigaOS 3.x on a 68020 or better (AmiSSL v5 needs
# one), with bebbo's m68k-amigaos-gcc and libnix (toolchain/README.md,
# section 7) and the AmiSSL SDK, fetched from Aminet (a new SDK there
# replaces the old one: then update SHA256).
#
#   make          fetch and check the AmiSSL SDK, build amibash-update
#   make clean

SDK     := AmiSSL-v5-SDK
SHA256  := 5003bef8c5930354d16b0ce7196d71b2811891c42fad38a9238c5ce4098ad42a
URL     := https://aminet.net/util/libs/$(SDK).lha
AMIGCC  ?= /home/user/toolchain/amiga
INC     := sdk/AmiSSL/Developer/include

amibash-update: amibash-update.c sdk/.unpacked
	$(AMIGCC)/bin/m68k-amigaos-gcc -noixemul -m68020 -O2 -Wall -I$(INC) -o $@ amibash-update.c
	$(AMIGCC)/bin/m68k-amigaos-strip $@

sdk/.unpacked: $(SDK).lha
	rm -rf sdk && mkdir sdk
	cd sdk && jlha xq ../$(SDK).lha >/dev/null
	touch $@

$(SDK).lha:
	wget -q -O $@.part $(URL)
	echo "$(SHA256)  $@.part" | sha256sum -c --quiet
	mv $@.part $@

clean:
	rm -rf amibash-update sdk

.PHONY: clean
