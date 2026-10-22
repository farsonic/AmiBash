# ssh and ssh-keygen from OpenSSH (portable, without OpenSSL) for
# AmigaOS 3.x on a 68020 or better, with bebbo's m68k-amigaos-gcc and
# libnix (toolchain/README.md, section 7).
#
#   make          fetch and check OpenSSH, patch it, build ssh and ssh-keygen
#   make clean

OPENSSH := openssh-10.6p1
SHA256  := a9dc9565dffe8640f64d863cd29a32bc4a3dbdec0566a7fc44c5d6ee767d5f39
URL     := https://cdn.openbsd.org/pub/OpenBSD/OpenSSH/portable/$(OPENSSH).tar.gz
AMIGCC  ?= /home/user/toolchain/amiga
export AMIGCC

all: ssh ssh-keygen

ssh ssh-keygen: $(OPENSSH)/.patched $(wildcard amiga/*)
	amiga/build.sh $(OPENSSH)
	cp $(OPENSSH)/ssh.strip ssh
	cp $(OPENSSH)/ssh-keygen.strip ssh-keygen

$(OPENSSH)/.patched: $(OPENSSH).tar.gz openssh-amiga.patch
	rm -rf $(OPENSSH)
	tar xzf $(OPENSSH).tar.gz
	cd $(OPENSSH) && patch -s -p1 <../openssh-amiga.patch
	touch $@

$(OPENSSH).tar.gz:
	wget -q -O $@.part $(URL)
	echo "$(SHA256)  $@.part" | sha256sum -c --quiet
	mv $@.part $@

clean:
	rm -rf $(OPENSSH) $(OPENSSH).tar.gz ssh ssh-keygen

.PHONY: all clean
.NOTPARALLEL:
