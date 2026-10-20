#!/bin/sh
# Build both Amiga binaries and pack AmiBash.lha (needs jlha-utils "lha").
set -e
cd "$(dirname "$0")/.."
make amiga amiga020
R=$(mktemp -d)
mkdir -p "$R/AmiBash/S" "$R/AmiBash/src"
cp build/bash build/bash020 "$R/AmiBash/"
cp dist/S/bashrc "$R/AmiBash/S/"
cp README.md LICENSE dist/AmiBash.readme "$R/AmiBash/"
cp src/*.c src/*.h Makefile "$R/AmiBash/src/"
rm -f dist/AmiBash.lha
(cd "$R" && jlha a "$OLDPWD/dist/AmiBash.lha" AmiBash >/dev/null)
rm -r "$R"
ls -l dist/AmiBash.lha
