#!/bin/sh
# build.sh OPENSSH_DIR - configure and build ssh and ssh-keygen for
# AmigaOS 3.x (68020+) with bebbo's m68k-amigaos-gcc in $AMIGCC.
set -e
D=$(cd "$1" && pwd)
A=$(cd "$(dirname "$0")" && pwd)
AMIGCC=${AMIGCC:-/home/user/toolchain/amiga}
export PATH=$AMIGCC/bin:$PATH
cd "$D"
./configure --host=m68k-amigaos \
    CC="m68k-amigaos-gcc -noixemul -m68020" CFLAGS="-O2" LIBS="-lsocket" \
    --prefix=/ssh \
    --without-openssl --without-zlib --without-pam --without-selinux \
    --without-kerberos5 --without-libedit --without-ldns \
    --disable-lastlog --disable-utmp --disable-utmpx --disable-wtmp --disable-wtmpx \
    --disable-pkcs11 --disable-security-key --without-security-key-builtin \
    --with-privsep-path=T: --with-sandbox=no --disable-strip >configure.out 2>&1 ||
    { tail -20 configure.out; exit 1; }
"$A/config_fix.sh" "$D"
make -j4 ssh ssh-keygen >make.out 2>&1 || { grep -E "error|undefined" make.out | head -20; exit 1; }
m68k-amigaos-strip -o ssh.strip ssh
m68k-amigaos-strip -o ssh-keygen.strip ssh-keygen
ls -l ssh.strip ssh-keygen.strip
