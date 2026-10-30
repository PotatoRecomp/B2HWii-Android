#!/bin/bash
# One test cycle: native build -> stage -> APK -> install -> run on the phone -> fetch the log.
# Pakai: bash tools/b-cycle.sh [detik_tunggu]
cd "$(dirname "$0")/.." || exit 1
export MSYS_NO_PATHCONV=1
S=D423214102E7; W=${1:-25}
NDK28="$LOCALAPPDATA/Android/Sdk/ndk/28.2.13676358"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/b-android.ps1 -Mode build -Jobs 12 || exit 1
grep -m5 "error:\|undefined symbol" generated/b/build.log | cut -c1-240
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/b-android.ps1 -Mode stage -Ndk "$NDK28" | tail -1
export JAVA_HOME='C:\Program Files\Eclipse Adoptium\jdk-17.0.20.101-hotspot' ANDROID_HOME="$LOCALAPPDATA\Android\Sdk"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
cmd.exe /c "third_party\gradle\bin\gradle.bat assembleRelease --no-daemon --console=plain" 2>&1 | tail -2
adb -s $S install -r app/build/outputs/apk/release/app-release.apk 2>&1 | tail -1
adb -s $S shell am force-stop com.shiinobu.basara; adb -s $S logcat -c; adb -s $S shell "rm -rf /sdcard/Android/data/com.shiinobu.basara/files/Logs/*"
# stream the log live to a file
pkill -f "adb.*logcat -v time" 2>/dev/null; pkill -f "adb.*tail -F" 2>/dev/null
echo "===== RUN $(date +%T) =====" >> generated/b/console-live.txt
(adb -s $S logcat -v time >> generated/b/logcat-live.txt 2>&1 &)
adb -s $S shell am start -n com.shiinobu.basara/.LauncherActivity >/dev/null; sleep 3
adb -s $S shell input tap 558 728
for i in $(seq 1 20); do
  L=$(adb -s $S shell "ls -t /sdcard/Android/data/com.shiinobu.basara/files/Logs 2>/dev/null | head -1" | tr -d '')
  [ -n "$L" ] && adb -s $S shell "test -f /sdcard/Android/data/com.shiinobu.basara/files/Logs/$L/console.log && echo y" | grep -q y && break; sleep 1
done
(adb -s $S shell "tail -n +1 -F /sdcard/Android/data/com.shiinobu.basara/files/Logs/$L/console.log" >> generated/b/console-live.txt 2>&1 &)
sleep "$W"
adb -s $S shell "pidof com.shiinobu.basara:game" | sed 's/^/game pid: /'
tail -n 80 generated/b/console-live.txt | grep -n "Runtime error\|fatal dialog\|stopped because\|Caller LR\|PC=" generated/b/console-live.txt | cut -c1-220 | head -12
