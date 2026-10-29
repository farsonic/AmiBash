#!/bin/bash
# setup_os314.sh KICKROM ADFZIP [DIR]
#
# Make a bootable AmigaOS 3.1.4 system drawer for FS-UAE (emu/os314.fs-uae)
# from your own Kickstart 3.1.4 ROM and the 3.1.4 disks (a zip with
# Workbench3_1_4.adf, Install3_1_4.adf, Fonts.adf ...). DIR defaults to
# /tmp/claude-0/os314. ROMs and OS files are copyrighted: they stay outside
# the repository.
#
# The system boots from DIR/sys; S:User-Startup runs Work:autorun (emu/dh0)
# like the AROS setup, so emu/aros_run.sh-style tests work with
#   CFG=os314 ./aros_run.sh script
set -e
ROM=$1 ZIP=$2 O=${3:-/tmp/claude-0/os314}
[ -f "$ROM" ] && [ -f "$ZIP" ] || { echo "usage: $0 KICKROM ADFZIP [DIR]" >&2; exit 2; }
rm -rf "$O"
mkdir -p "$O/adf" "$O/x"
cp "$ROM" "$O/kick.rom"
unzip -q "$ZIP" -x '__MACOSX/*' -d "$O/adf"
for a in "$O"/adf/*.adf; do
    [ -s "$a" ] || continue
    n=$(basename "$a" .adf)
    mkdir -p "$O/x/$n"
    xdftool "$a" unpack "$O/x/$n" >/dev/null
done
vol() { ls -d "$O"/x/$1/*/ 2>/dev/null | head -1; }
WB=$(vol 'Workbench*') INST=$(vol 'Install*') FONTS=$(vol 'Fonts')
[ -n "$WB" ] || { echo "no Workbench disk in $ZIP" >&2; exit 1; }
cp -r "$WB" "$O/sys"
# workbench.library and icon.library are on the Install disk
[ -n "$INST" ] && cp --update=none "$INST"/Libs/* "$O/sys/Libs/" && cp "$INST/Installer" "$O/sys/C/"
[ -n "$FONTS" ] && mkdir -p "$O/sys/Fonts" && cp -r "$FONTS"/* "$O/sys/Fonts/"
# the floppy Startup-Sequence works from a hard disk too; add the test hook
cat >>"$O/sys/S/User-Startup" <<'EOF'
; AmiBash test harness: run Work:autorun once Work: is there
Lab waitwork
If NOT EXISTS Work:
    Wait 1
    Skip waitwork BACK
EndIf
If EXISTS Work:autorun
    Run >NIL: Execute Work:autorun
EndIf
EOF
rm -rf "$O/x" "$O/adf"
echo "AmigaOS 3.1.4 system in $O/sys, ROM $O/kick.rom"
