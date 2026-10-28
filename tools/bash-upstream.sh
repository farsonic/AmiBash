#!/bin/sh
# bash-upstream.sh - follow GNU bash releases.
#
#   tools/bash-upstream.sh            the newest bash release, with its patches
#   tools/bash-upstream.sh 5.3        a given release
#   tools/bash-upstream.sh -n         only say what is new, build nothing
#
# 1. finds the newest release (and its official patches) on ftp.gnu.org,
# 2. downloads them and checks their GPG signatures against the GNU keyring,
# 3. builds that bash in $BASHREF_DIR (default ../toolchain/bash-ref) as
#    the reference shell,
# 4. prints what changed since the version in UPSTREAM_BASH (the one
#    AmiBash follows now): the NEWS of a newer release, and the bug
#    description of every newer official patch, and
# 5. runs tests/run_compat.sh against it: every difference is something
#    to implement (or to note in tests/compat/KNOWN_DIFFERENCES.md).
#
# When AmiBash has caught up, put the new version (e.g. 5.3.20) in
# UPSTREAM_BASH and in AMIBASH_VERSION (src/sh.h), and commit.
set -e
cd "$(dirname "$0")/.."
ROOT=$PWD
FTP=https://ftp.gnu.org/gnu/bash
DIR=${BASHREF_DIR:-$ROOT/../toolchain/bash-ref}
NOBUILD=0
[ "$1" = -n ] && { NOBUILD=1; shift; }
HAVE=$(sed -n 's/^\([0-9][0-9.]*\).*/\1/p' UPSTREAM_BASH)

mkdir -p "$DIR"
DIR=$(cd "$DIR" && pwd)
cd "$DIR"

# the release: X.Y (no betas or release candidates)
REL=$1
if [ -z "$REL" ]; then
    REL=$(curl -fsSL --retry 5 --retry-all-errors "$FTP/" | grep -oE 'bash-[0-9]+\.[0-9]+\.tar\.gz"' |
          sed 's/bash-\(.*\)\.tar\.gz"/\1/' | sort -t. -k1,1n -k2,2n | tail -1)
fi
[ -n "$REL" ] || { echo "bash-upstream: can't read $FTP" >&2; exit 1; }
NODOT=$(echo "$REL" | tr -d .)
PATCHES=$(curl -fsSL --retry 5 --retry-all-errors "$FTP/bash-$REL-patches/" 2>/dev/null |
          grep -oE "\"bash$NODOT-[0-9]+\"" | tr -d '"' | sort -u || true)
NP=$(echo "$PATCHES" | grep -c . || true)
echo "Newest GNU bash: $REL with $NP patches ($REL.$NP); AmiBash follows $HAVE."

get() {    # url file (kept: downloaded once)
    [ -s "$2" ] && return 0
    curl -fsSL --retry 5 --retry-all-errors -o "$2.part" "$1" || { echo "bash-upstream: can't fetch $1" >&2; exit 1; }
    mv "$2.part" "$2"
}
# the GNU keyring: the release managers' keys
get https://ftp.gnu.org/gnu/gnu-keyring.gpg gnu-keyring.gpg
verify() { # file
    get "$2.sig" "$1.sig"
    gpgv --keyring "$DIR/gnu-keyring.gpg" "$1.sig" "$1" 2>/dev/null ||
        { echo "bash-upstream: BAD SIGNATURE on $1" >&2; rm -f "$1" "$1.sig"; exit 1; }
}
get "$FTP/bash-$REL.tar.gz" "bash-$REL.tar.gz"
verify "bash-$REL.tar.gz" "$FTP/bash-$REL.tar.gz"
mkdir -p "patches-$REL"
for p in $PATCHES; do
    get "$FTP/bash-$REL-patches/$p" "patches-$REL/$p"
    verify "patches-$REL/$p" "$FTP/bash-$REL-patches/$p"
done
echo "Signatures good."

SRC=$DIR/bash-$REL.$NP
if [ ! -d "$SRC" ]; then
    rm -rf "bash-$REL"
    tar xzf "bash-$REL.tar.gz"
    for p in $PATCHES; do (cd "bash-$REL" && patch -s -p0 <"$DIR/patches-$REL/$p"); done
    mv "bash-$REL" "$SRC"
fi

# what is new since the version AmiBash follows
echo
echo "==== NEWS since bash $HAVE ===="
awk -v have="$HAVE" '
    /^This is a terse description of the new features added to bash-/ {
        v = $0; sub(/.*bash-/, "", v); sub(/ since.*/, "", v)
        split(v, a, "."); split(have, b, ".")
        show = (a[1] + 0 > b[1] + 0) || (a[1] + 0 == b[1] + 0 && a[2] + 0 > b[2] + 0)
    }
    show' "$SRC/NEWS"
echo "==== end of NEWS ===="

# the official patches newer than the patch level AmiBash follows
HAVE_REL=$(echo "$HAVE" | cut -d. -f1-2)
HAVE_PL=$(echo "$HAVE" | cut -d. -f3)
[ "$HAVE_REL" = "$REL" ] || HAVE_PL=0
echo
echo "==== patches to bash $REL after $REL.${HAVE_PL:-0} ===="
for p in $PATCHES; do
    n=$(echo "$p" | sed 's/.*-0*//')
    [ "$n" -gt "${HAVE_PL:-0}" ] || continue
    echo "--- $p"
    awk '/^Bug-Description:/ {on = 1; next} /^Patch \(apply/ {next} /^(\*\*\* |diff |Index: )/ {exit} on' \
        "$DIR/patches-$REL/$p" | sed '/^$/d'
done
echo "==== end of patches ===="
if [ "$HAVE" = "$REL.$NP" ]; then
    echo "AmiBash already follows bash $REL.$NP."
fi
[ $NOBUILD = 1 ] && exit 0

if [ ! -x "$SRC/bash" ]; then
    echo "Building bash $REL.$NP in $SRC ..."
    (cd "$SRC" && ./configure --without-bash-malloc >configure.log 2>&1 && make -j4 >make.log 2>&1) ||
        { echo "bash-upstream: build failed, see $SRC/make.log" >&2; exit 1; }
fi
ln -sf "$SRC/bash" "$DIR/bash"
"$DIR/bash" --version | head -1

echo
echo "==== tests/run_compat.sh against bash $REL.$NP ===="
cd "$ROOT"
[ -x build/amibash ] || make build/amibash >/dev/null
BASH_REF=$DIR/bash tests/run_compat.sh -q || true
echo "(tests/run_compat.sh -v NAME shows a test's differences)"
if [ "$HAVE" != "$REL.$NP" ]; then
    echo
    echo "To do: what the NEWS and patches above change that AmiBash should do"
    echo "too, a test for each in tests/compat/, then $REL.$NP in UPSTREAM_BASH"
    echo "and AMIBASH_VERSION (src/sh.h)."
fi
