#!/bin/sh
# Dev helper: copy dhewm3.rpx to Cemu's emulated SD card, boot it, and dump the
# engine's console output after a while. Usage: cemu-run.sh [seconds] [logfile]
# (Cemu only writes files on the SD card when they're closed, so use the console)
cd "$(dirname "$0")/../../.."
CEMU=/Applications/Cemu_Metal_arm64.app/Contents/MacOS/Cemu_Metal_arm64
SD="$HOME/Library/Application Support/Cemu/sdcard/wiiu/apps/dhewm3"
WAIT=${1:-60}
LOG=${2:-build-wiiu/cemu.log}
pkill -f "$CEMU" 2>/dev/null
mkdir -p "$SD"
cp build-wiiu/dhewm3.rpx "$SD/"
"$CEMU" --verbose -g "$SD/dhewm3.rpx" > "$LOG" 2>&1 &
sleep "$WAIT"
grep "OSConsole" "$LOG" | sed 's/\[OSConsole\] //'
if grep -q "Trace starting" "$LOG"; then
	echo "=== CRASH"; grep -A12 "Trace starting" "$LOG"
	grep -A3 "Active PPC instance" "$HOME/Library/Application Support/Cemu/log.txt"
fi
