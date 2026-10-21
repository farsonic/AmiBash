#!/bin/bash
# usage: run.sh a1200|a500   -> boots FS-UAE in Xvfb :99 (background)
export DISPLAY=:99
pgrep -f "Xvfb :99" >/dev/null || (Xvfb :99 -screen 0 1280x1024x24 >/tmp/xvfb.log 2>&1 &)
sleep 1
pkill -x fs-uae
# wait until the old instance has really gone, or the new one stops too
while pgrep -x fs-uae >/dev/null; do sleep 0.5; done
cd "$(dirname "$0")"
SDL_AUDIODRIVER=dummy fs-uae ${1:-a1200}.fs-uae >/tmp/fsuae.log 2>&1 &
