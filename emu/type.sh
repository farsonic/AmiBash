#!/bin/bash
# type.sh 'text' [shot.png]  - type a line into the emulator (+Return) and screenshot
export DISPLAY=:99
W=$(xdotool search --name "FS-UAE" | head -1)
xdotool windowfocus $W 2>/dev/null
xdotool type --delay 60 -- "$1"
xdotool key Return
sleep ${WAIT:-3}
[ -n "$2" ] && import -window root "$2"
exit 0
