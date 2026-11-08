#!/usr/bin/env bash
# PKG (default com.shiinobu.basara) and HOMEDIR (default Basara2Heroes) pick the app.
# Title-idle battle demo on the Redmi (Adreno 618): launch Beta, Play, idle SECONDS, pull the log.
# Usage: bash generated/b/gpu/redmi-bench.sh SECONDS OUT "ENV;ENV"   (debug.basara.env, max 92 chars)
# Also saves OUT.sf = SurfaceFlinger's composition of the game layer 60 s in.
export MSYS_NO_PATHCONV=1 BASARA_ADB_SERIAL=${SERIAL:-d06bd2fa0013}
cd "$(dirname "$0")/.."
PKG=${PKG:-com.shiinobu.basara}
A=(third_party/android-emu/platform-tools/adb.exe -s $BASARA_ADB_SERIAL)
"${A[@]}" shell am force-stop $PKG
"${A[@]}" shell setprop debug.basara.env "'BASARA_FRAMESKIP=${FRAMESKIP:-0};BASARA_LOG_FPS=1;$3'"
BEFORE=$("${A[@]}" shell "ls -d /sdcard/${HOMEDIR:-Basara2Heroes}/Logs/2*/ | tail -n 1" | tr -d '\r')
"${A[@]}" shell monkey -p $PKG -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
sleep 5
"${A[@]}" shell uiautomator dump /sdcard/ui.xml >/dev/null 2>&1
XY=$("${A[@]}" shell cat /sdcard/ui.xml | grep -o 'text="\(Play\|Main\)"[^>]*bounds="\[[0-9]*,[0-9]*\]\[[0-9]*,[0-9]*\]"' | head -1 |
     sed 's/.*bounds="\[\([0-9]*\),\([0-9]*\)\]\[\([0-9]*\),\([0-9]*\)\]"/\1 \2 \3 \4/')
set -- "$1" "$2" "$3" $XY
# Older builds open a launcher with Play; newer ones start the game directly.
[ -n "$4" ] && "${A[@]}" shell input tap $(( ($4 + $6) / 2 )) $(( ($5 + $7) / 2 ))
# The no-save dialog (79 draws): cursor left to "hai", then A.
# Wait (up to 30 s) for this run's own log folder: right after an install the newest one is still
# the last run's.
for ((i=0; i<30; i++)); do
  sleep 1
  NEWEST=$("${A[@]}" shell "ls -d /sdcard/${HOMEDIR:-Basara2Heroes}/Logs/2*/ | tail -n 1" | tr -d '\r')
  [ -n "$NEWEST" ] && [ "$NEWEST" != "$BEFORE" ] && [ $i -ge 3 ] && break
done
LOG=${NEWEST}log/console.log
echo "log: $LOG"
for ((t=0; t<60; t+=5)); do
  sleep 5
  if [ -z "$PASSED" ] && "${A[@]}" shell "grep '\[vi\] fps' $LOG | tail -n 1" | grep -q " 79 draws"; then
    # Modern touch layout (Redmi 2400x1080): d-pad left at 617,862, A at 2285,540.
    sleep 2; "${A[@]}" shell input swipe 617 862 617 862 150; sleep 1; "${A[@]}" shell input swipe 2285 540 2285 540 200
    PASSED=1; echo "t=$t dialog: left + A"
  fi
done
"${A[@]}" shell dumpsys SurfaceFlinger | grep -A1 "SurfaceView\[com.shiinobu.basara.beta" | grep -E "CLIENT|DEVICE" > "$2.sf"
sleep $(( $1 - 60 ))
"${A[@]}" pull "$LOG" "$2" >/dev/null 2>&1
"${A[@]}" shell am force-stop $PKG
"${A[@]}" shell setprop debug.basara.env "''"
echo "$2: $(wc -c < "$2") bytes, sf: $(cat "$2.sf" | tr -s ' ' | head -1)"
