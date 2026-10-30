#!/usr/bin/env bash
# Drives the emulator UI over adb. Usage:
#   tools/b-ui.sh dump            visible texts with their centre point
#   tools/b-ui.sh tap "Text"      taps the first node whose text or content-desc is exactly "Text"
#   tools/b-ui.sh xy X Y          taps a point
# Device: $BASARA_ADB_SERIAL (default emulator-5554).
export MSYS_NO_PATHCONV=1
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
A=("$ROOT/third_party/android-emu/platform-tools/adb.exe" -s "${BASARA_ADB_SERIAL:-emulator-5554}")
nodes() {
  "${A[@]}" shell uiautomator dump /sdcard/ui.xml >/dev/null 2>&1
  "${A[@]}" shell cat /sdcard/ui.xml | tr '>' '\n' |
    sed -n 's/.*text="\([^"]*\)".*content-desc="\([^"]*\)".*bounds="\[\([0-9]*\),\([0-9]*\)\]\[\([0-9]*\),\([0-9]*\)\]".*/\1|\2|\3 \4 \5 \6/p' |
    awk -F'|' '{split($3,b," "); t=$1; if(t=="")t=$2; if(t!="") printf "%s|%d %d\n", t, (b[1]+b[3])/2, (b[2]+b[4])/2}'
}
case "$1" in
  dump) nodes ;;
  tap)
    p=$(nodes | awk -F'|' -v t="$2" '$1==t{print $2; exit}')
    [ -z "$p" ] && { echo "not found: $2"; exit 1; }
    "${A[@]}" shell input tap $p; echo "tap $2 @ $p" ;;
  xy) "${A[@]}" shell input tap "$2" "$3" ;;
esac
