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
"${A[@]}" shell monkey -p $PKG -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
sleep 5
"${A[@]}" shell uiautomator dump /sdcard/ui.xml >/dev/null 2>&1
XY=$("${A[@]}" shell cat /sdcard/ui.xml | grep -o 'text="\(Play\|Main\)"[^>]*bounds="\[[0-9]*,[0-9]*\]\[[0-9]*,[0-9]*\]"' | head -1 |
     sed 's/.*bounds="\[\([0-9]*\),\([0-9]*\)\]\[\([0-9]*\),\([0-9]*\)\]"/\1 \2 \3 \4/')
set -- "$1" "$2" "$3" $XY
"${A[@]}" shell input tap $(( ($4 + $6) / 2 )) $(( ($5 + $7) / 2 ))
# The no-save dialog (79 draws): cursor left to "hai", then A (touch button at 2250,540).
sleep 4
LOG=$("${A[@]}" shell "ls -d /sdcard/${HOMEDIR:-Basara2Heroes}/Logs/2*/ | tail -n 1" | tr -d '\r')log/console.log
echo "log: $LOG"
for ((t=0; t<60; t+=5)); do
  sleep 5
  if [ -z "$PASSED" ] && "${A[@]}" shell "grep 'draws' $LOG | tail -n 1" | grep -q " 79 draws"; then
    "${A[@]}" shell input keyevent --longpress 21; sleep 1; "${A[@]}" shell input swipe 2250 540 2250 540 200
    PASSED=1; echo "t=$t dialog: left + A"
  fi
done
"${A[@]}" shell dumpsys SurfaceFlinger | grep -A1 "SurfaceView\[com.shiinobu.basara.beta" | grep -E "CLIENT|DEVICE" > "$2.sf"
sleep $(( $1 - 60 ))
"${A[@]}" pull "$LOG" "$2" >/dev/null 2>&1
"${A[@]}" shell am force-stop $PKG
"${A[@]}" shell setprop debug.basara.env "''"
echo "$2: $(wc -c < "$2") bytes, sf: $(cat "$2.sf" | tr -s ' ' | head -1)"
