#!/bin/sh
# config_fix.sh OPENSSH_DIR - correct what configure gets wrong when
# cross-compiling for AmigaOS, and include amiga_compat.h everywhere.
set -e
D=$1
A=$(cd "$(dirname "$0")" && pwd)
cfg() {   # cfg NAME VALUE|undef
    sed -i "/^#define $1 /d; /^\/\* #undef $1 \*\//d" "$D/config.h"
    if [ "$2" = undef ]; then echo "#undef $1" >>"$D/config.h"
    else echo "#define $1 $2" >>"$D/config.h"; fi
}
cfg HAVE_SETSID 1
cfg HAVE_WCWIDTH 1
cfg HAVE_GETENTROPY 1     # port-amiga.c
cfg HAVE_PPOLL 1          # port-amiga-io.c; the pselect() emulation needs pipes
cfg HAVE_PATHS_H undef    # its _PATH_DEVNULL is /dev/null: amiga_compat.h has NIL:
# libnix's getaddrinfo() and friends call bsdsocket.library 4 (Roadshow)
# without checking: on AmiTCP, Miami, Genesis or UAE's bsdsocket they jump
# past the library's vectors.  OpenSSH's own versions use gethostbyname().
for f in HAVE_GETADDRINFO HAVE_FREEADDRINFO HAVE_GAI_STRERROR HAVE_CONST_GAI_STRERROR_PROTO HAVE_GETNAMEINFO; do
    cfg $f undef
done
cfg SOCK_HAS_LEN undef    # sockaddr_in6 has no sin6_len; the stack sets sa_len
for m in "$D/Makefile" "$D/openbsd-compat/Makefile"; do
    grep -q amiga_compat.h "$m" ||
        sed -i "s#^CPPFLAGS=\(.*\)#CPPFLAGS=-include $A/amiga_compat.h \1#" "$m"
done
# the Amiga support code, as one of OpenSSH's port-*.c files
cp "$A/port-amiga.c" "$A/port-amiga-io.c" "$D/openbsd-compat/"
grep -q port-amiga.o "$D/openbsd-compat/Makefile" ||
    sed -i 's#^PORTS=\tport-aix.o#PORTS=\tport-amiga.o port-amiga-io.o port-aix.o#' "$D/openbsd-compat/Makefile"
# soft-float support for the few doubles (timeouts)
grep -q '^LIBS=.* -lm' "$D/Makefile" || sed -i 's#^LIBS=\(.*\)#LIBS=\1 -lm#' "$D/Makefile"
# ssh_config and the known hosts of everyone: ENVARC:ssh
sed -i 's#^sysconfdir=.*#sysconfdir=ENVARC:ssh#' "$D/Makefile"
