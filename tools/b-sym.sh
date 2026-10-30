#!/usr/bin/env bash
# Symbolize the last "[sample N]" lines of generated/b/console-live.txt (libmain.so offsets).
cd "$(dirname "$0")/.."
SYM="$LOCALAPPDATA/Android/Sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-symbolizer.exe"
LINE=$(tail -c 300000 generated/b/console-live.txt | grep -a "\[sample" | tail -${1:-1} | head -1)
echo "$LINE" | cut -c1-80
ADDRS=$(echo "$LINE" | grep -oE "0x[0-9a-f]+" | tail -n +2)
"$SYM" --obj=generated/android-build/libmain.so --functions=short --no-inlines $ADDRS | grep -v "^$" | paste - - | awk '{print $1}'
