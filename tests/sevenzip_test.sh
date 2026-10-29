#!/bin/sh
# .7z add-ons: synthetic files (no game data) packed with the 7-Zip CLI in LZMA, LZMA2, PPMd, BCJ2
# and stored form, read back with the built-in decoder.   tests/sevenzip_test.sh [BUILD_DIR]
set -eu
b=${1:-build}; t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
mkdir -p "$t/src/Mod Folder/Sub dir" "$t/src/Ünïcode"
head -c 300000 /dev/urandom > "$t/src/Mod Folder/random.bin"
seq 1 50000 > "$t/src/Mod Folder/Sub dir/numbers.txt"
printf '{"Name": "test mod"}' > "$t/src/Mod Folder/mod.json"
printf 'x' > "$t/src/Ünïcode/ä.txt"
: > "$t/src/empty.txt"
touch -d '2024-10-17 12:34:56' "$t/src/Mod Folder/mod.json"
fail=0
for m in "-m0=LZMA2" "-m0=LZMA" "-m0=PPMd" "-mf=BCJ2 -m0=LZMA" "-m0=Copy"; do
    rm -f "$t/a.7z"
    (cd "$t/src" && 7z a -t7z $m "$t/a.7z" . >/dev/null)
    printf '%s: ' "$m"; "$b/sevenzip_test" "$t/a.7z" "$t/src" || fail=1
done
head -c 2000 "$t/a.7z" > "$t/cut.7z"
if "$b/sevenzip_test" "$t/cut.7z" "$t/src" 2>/dev/null; then echo "a cut archive was accepted"; fail=1; else echo "cut archive: rejected"; fi
exit $fail
