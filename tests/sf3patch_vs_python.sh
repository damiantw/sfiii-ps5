#!/bin/bash
# Patch every original with the C++ sf3patch and cmp against the Python patcher's reference images.
# Paths are local test locations; set S, BIN, REF, PKG, ADDONS for another machine.
S=${S:-/workspace/discs/SFII}; BIN=${BIN:-build/sf3patch}
declare -A D=(
 [sc1-usa]="$S/Shining Force III (USA)/Shining Force III (USA).cue"
 [sc2-1.003]="$S/Shining Force III - Scenario 2 - Nerawareta Miko (Japan)/Shining Force III - Scenario 2 - Nerawareta Miko (Japan).cue"
 [sc2-2.000]="$S/Shining Force III - Scenario 2 - Nerawareta Miko (Japan) (Rev A)/Shining Force III - Scenario 2 - Nerawareta Miko (Japan) (Rev A).cue"
 [sc3]="$S/Shining Force III - Scenario 3 - Hyouheki no Jashinguu (Japan)/Shining Force III - Scenario 3 - Hyouheki no Jashinguu (Japan).cue"
 [pd]="$S/Shining Force III Premium Disc (Japan)/Shining Force III Premium Disc (Japan).cue"
)
src="${SRC:-cue}"
fail=0
for k in sc3 sc1-usa sc2-1.003 sc2-2.000 pd; do
  for lane in a b; do
    if [ $lane = a ]; then ref=/workspace/discs/patched/$k-v25_1; extra=(); else ref=/workspace/discs/patched/$k-v25_1-addons; extra=(--addons /workspace/sf3pw/v25plus); fi
    in="${D[$k]}"; [ "$src" = chd ] && in="/workspace/discs/chd/$(basename "${D[$k]}" .cue).chd"
    out=/workspace/discs/cpp/$k-$lane; mkdir -p $out
    $BIN patch "$in" --package /workspace/sf3pw/SF3Patch.exe "${extra[@]}" --reproducible -o $out/$k.bin > $out/log.txt 2>&1 || { echo "$k $lane: exit $?"; tail -3 $out/log.txt; fail=1; continue; }
    if cmp -s $out/$k.bin $ref/$k.bin && cmp -s $out/$k.cue $ref/$k.cue; then echo "$k $lane: identical ($(grep 'track 1 sha1' $out/log.txt))"
    else echo "$k $lane: DIFFERENT"; cmp $out/$k.bin $ref/$k.bin | head -2; fail=1; fi
    rm -f $out/$k.bin
  done
done
exit $fail
