#!/usr/bin/env bash
# Lists instructions above ARMv8.0-A in the APK's native libraries (Poco M3 SIGILL check).
# Usage: bash tools/b-isa-scan.sh [lib.so...]   (default: the staged jniLibs)
# Prints, per library, the count of each post-v8.0 mnemonic and the functions holding them
# (symbolized with the unstripped generated/android-build/libmain.so for libmain).
set -u
cd "$(dirname "$0")/.."
BIN="$LOCALAPPDATA/Android/Sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/windows-x86_64/bin"
OUT=generated/b/isa-scan
mkdir -p "$OUT"
LIBS=("$@")
[ ${#LIBS[@]} -eq 0 ] && LIBS=(generated/android-native/jniLibs/arm64-v8a/*.so)
T=$'\t'
# LSE atomics (v8.1), RDM (v8.1), dotprod/fp16 FML (v8.2), RCpc (v8.3), JSCVT (v8.3), FRINTTS (v8.5),
# BF16/I8MM (v8.6), and fp16 arithmetic on h / .4h / .8h registers (v8.2 FP16).
PAT="${T}(ld(add|clr|eor|set|smax|smin|umax|umin)[alh]*|st(add|clr|eor|set|smax|smin|umax|umin)[lh]*|swp[alh]*|casp?[alh]*|sqrdml[as]h|[su]dot|ldapu?r[bh]?|fjcvtzs|frint(32|64)[xz]|bf(dot|mmla|cvt[a-z0-9]*)|[su]mmla|usmmla|fml[as]l2?)${T}|${T}f(add|sub|mul|div|mla|mls|max|min|maxnm|minnm|abs|neg|sqrt|cmp|cmpe|madd|msub|nmul|cvtz[su]|abd|recpe|rsqrte)${T}(h[0-9]+|v[0-9]+\.[48]h)"
for lib in "${LIBS[@]}"; do
  name=$(basename "$lib" .so)
  "$BIN/llvm-objdump.exe" -d --no-show-raw-insn "$lib" | grep -E "$PAT" > "$OUT/$name.txt"
  n=$(wc -l < "$OUT/$name.txt")
  echo "== $name: $n post-v8.0 instructions"
  [ "$n" -eq 0 ] && continue
  cut -f2 "$OUT/$name.txt" | sort | uniq -c | sort -rn | head -15
  if [ "$name" = libmain ]; then
    awk '{sub(":","",$1); print "0x"$1}' "$OUT/$name.txt" | sort -u | head -4000 \
      | "$BIN/llvm-addr2line.exe" -f -C -e generated/android-build/libmain.so \
      | awk 'NR%2==1' | sort | uniq -c | sort -rn | head -25
  fi
done
