#!/usr/bin/env bash
# Title-idle battle demo benchmark for the release app (no launcher): start the game, pass the
# no-save dialog, let the title demo battle run, then pull the run's folder (console.log, and
# profile-stacks.txt when BASARA_PROFILE is set).
# Usage: bash tools/b-bench.sh SECONDS OUTDIR "ENV=1;ENV=2"
#   SERIAL=<adb serial> (default Redmi d06bd2fa0013), PKG=<package> (default com.shiinobu.basara)
export MSYS_NO_PATHCONV=1
cd "$(dirname "$0")/.."
SERIAL=${SERIAL:-d06bd2fa0013}
PKG=${PKG:-com.shiinobu.basara}
HOME_DIR=/sdcard/Basara2Heroes
[ "$PKG" = com.shiinobu.basara.beta ] && HOME_DIR=/sdcard/Basara2Heroes-Beta
A=(third_party/android-emu/platform-tools/adb.exe -s "$SERIAL")
SECONDS_TOTAL=$1 OUT=$2
mkdir -p "$OUT"
"${A[@]}" shell am force-stop $PKG
"${A[@]}" shell setprop debug.basara.env "'BASARA_FRAMESKIP=0;BASARA_LOG_FPS=1;$3'"
"${A[@]}" shell monkey -p $PKG -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
sleep 6
RUN=$("${A[@]}" shell "ls -d $HOME_DIR/Logs/2*/ | tail -n 1" | tr -d '\r')
LOG=${RUN}log/console.log
echo "run: $RUN"
for ((t=0; t<60; t+=5)); do
  sleep 5
  # The no-save dialog draws 79 draws a frame: cursor left to "hai", then A (gamepad A key).
  if [ -z "$PASSED" ] && "${A[@]}" shell "grep 'fps.*draws' $LOG | tail -n 1" | grep -q " 79 draws"; then
    "${A[@]}" shell input keyevent --longpress 21; sleep 1; "${A[@]}" shell input keyevent 96
    PASSED=1; echo "t=$t dialog: left + A"
  fi
done
sleep $(( SECONDS_TOTAL > 60 ? SECONDS_TOTAL - 60 : 0 ))
"${A[@]}" shell am force-stop $PKG
"${A[@]}" shell setprop debug.basara.env "''"
"${A[@]}" pull "$RUN" "$OUT" >/dev/null 2>&1
find "$OUT" -type f -printf "%s %p\n"
