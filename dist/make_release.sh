#!/bin/sh
# Build both Amiga binaries (pure/resident builds), InstallTTF, ssh and
# ssh-keygen (needs bebbo's m68k-amigaos-gcc, see contrib/ssh) and pack
# AmiBash.lha (needs jlha-utils "lha") and AmiBash-<version>-bin.zip.
set -e
cd "$(dirname "$0")/.."
make pure pure020
make -C contrib/installttf
make -C contrib/top
make -C contrib/ssh
V=$(sed -n 's/^#define AMIBASH_SHORTVER "\(.*\)"/\1/p' src/sh.h)
R=$(mktemp -d)
mkdir -p "$R/AmiBash/S" "$R/AmiBash/src" "$R/AmiBash/tools"
cp build/bash.pure "$R/AmiBash/bash"
cp build/bash020.pure "$R/AmiBash/bash020"
cp dist/bash.info "$R/AmiBash/"
cp contrib/installttf/InstallTTF contrib/top/top "$R/AmiBash/"
mkdir -p "$R/AmiBash/SSH"
cp contrib/ssh/ssh contrib/ssh/ssh-keygen contrib/ssh/ssh-copy-id "$R/AmiBash/SSH/"
cp contrib/ssh/README.md "$R/AmiBash/SSH/ssh.readme"
cp dist/Install-AmiBash "$R/AmiBash/"
python3 tools/mkicon.py "$R/AmiBash/Install-AmiBash.info" Installer APPNAME=AmiBash MINUSER=AVERAGE >/dev/null
mkdir -p "$R/AmiBash/Icons"
python3 tools/mkicon.py "$R/AmiBash/Icons/AmiBash Terminal.info" C:bash >/dev/null
cp -r dist/Fonts "$R/AmiBash/"
cp tools/pcf2amigafont.py tools/ttf2amigafont.py tools/mkicon.py "$R/AmiBash/tools/"
cp dist/S/bashrc "$R/AmiBash/S/"
cp README.md LICENSE dist/AmiBash.readme "$R/AmiBash/"
cp src/*.c src/*.h Makefile "$R/AmiBash/src/"
rm -f dist/AmiBash.lha
(cd "$R" && jlha a "$OLDPWD/dist/AmiBash.lha" AmiBash >/dev/null)
rm -rf "$R/AmiBash/src"
rm -f dist/AmiBash-$V-bin.zip
(cd "$R" && zip -qr "$OLDPWD/dist/AmiBash-$V-bin.zip" AmiBash)
rm -r "$R"
ls -l dist/AmiBash.lha dist/AmiBash-$V-bin.zip
