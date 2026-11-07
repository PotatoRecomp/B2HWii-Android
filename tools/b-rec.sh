#!/usr/bin/env bash
# Records a phone test session: waits for the next game run's log folder on the phone, then streams
# its console.log and the crash-relevant logcat to generated/b/rec/<run>/ until stopped.
#   bash tools/b-rec.sh [SERIAL]
set -u
export MSYS_NO_PATHCONV=1
cd "$(dirname "$0")/.."
ADB=third_party/android-emu/platform-tools/adb.exe
SERIAL=${1:-d06bd2fa0013}
LOGS=/sdcard/Basara2Heroes/Logs
adb() { "$ADB" -s "$SERIAL" "$@"; }

before=$(adb shell ls -t "$LOGS" 2>/dev/null | tr -d '\r' | head -1)
echo "waiting for a new run (last: $before)"
run=""
while [ -z "$run" ]; do
  sleep 2
  latest=$(adb shell ls -t "$LOGS" 2>/dev/null | tr -d '\r' | head -1)
  [ -n "$latest" ] && [ "$latest" != "$before" ] && run=$latest
done
out=generated/b/rec/$run
mkdir -p "$out"
echo "recording run $run -> $out"
adb logcat -c
adb logcat -v time 'Basara:V' 'AndroidRuntime:E' 'DEBUG:V' 'libc:F' '*:F' > "$out/logcat.txt" &
lc=$!
trap 'kill $lc 2>/dev/null' EXIT
# console.log appears a moment after the folder; tail -F follows it once it exists.
adb shell "tail -F -n +1 $LOGS/$run/log/console.log 2>/dev/null" | tr -d '\r' > "$out/console.log"
