#!/usr/bin/env bash
# Records a played session on the Redmi: launches the game with FPS logging, waits for the player,
# pulls console.log after SECONDS, then clears debug.basara.env. The debug.basara.gfx* properties
# (set beforehand) are left alone.
# Usage: bash tools/b-redmi-record.sh SECONDS OUT ["ENV;ENV"]
export MSYS_NO_PATHCONV=1
cd "$(dirname "$0")/.."
SERIAL=${SERIAL:-d06bd2fa0013}
PKG=${PKG:-com.shiinobu.basara}
HOMEDIR=${HOMEDIR:-Basara2Heroes}
A=(third_party/android-emu/platform-tools/adb.exe -s "$SERIAL")
"${A[@]}" shell am force-stop "$PKG"
"${A[@]}" shell setprop debug.basara.env "'BASARA_LOG_FPS=1;$3'"
BEFORE=$("${A[@]}" shell "ls -d /sdcard/$HOMEDIR/Logs/2*/ | tail -n 1" | tr -d '\r')
"${A[@]}" shell monkey -p "$PKG" -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
for ((i=0; i<60; i++)); do
  sleep 1
  NEWEST=$("${A[@]}" shell "ls -d /sdcard/$HOMEDIR/Logs/2*/ | tail -n 1" | tr -d '\r')
  [ -n "$NEWEST" ] && [ "$NEWEST" != "$BEFORE" ] && break
done
LOG=${NEWEST}log/console.log
echo "log: $LOG (recording $1 s)"
sleep "$1"
"${A[@]}" pull "$LOG" "$2" >/dev/null 2>&1
"${A[@]}" shell setprop debug.basara.env "''"
echo "$2: $(wc -c < "$2") bytes"
