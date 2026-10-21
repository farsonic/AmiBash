#!/bin/bash
# aros_run.sh SCRIPTFILE [TIMEOUT] - boot the RTG AROS setup (arosrtg.fs-uae),
# run SCRIPTFILE (AmigaDOS commands) as Work:autorun, wait for it to finish
# (it should end with: Echo >Work:autorun.done done), then stop FS-UAE.
cd "$(dirname "$0")"
T=${2:-300}
rm -f dh0/autorun.done dh0/autorun.done.uaem
cp "$1" dh0/autorun
./run.sh arosrtg
for i in $(seq 1 $T); do
    [ -f dh0/autorun.done ] && break
    sleep 1
done
sleep 2
DISPLAY=:99 import -window root /tmp/claude-0/aros_last.png
pkill -x fs-uae
rm -f dh0/autorun dh0/autorun.uaem
[ -f dh0/autorun.done ] && echo "finished after ${i}s" || echo "TIMEOUT"
