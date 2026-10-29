# Running the PS5 app on PC

`src/host/host_runtime.cpp` is the PS5 app's `main`. On Linux it builds against a generated
saturn-recomp game tree and runs the same host, menu and pacer as on the console, in a window
(SDL2) or offscreen. The generated tree comes from your own disc, so the build is game-derived:
keep it outside this repo and never commit or share it.

## Build

```sh
cmake -G Ninja -S <generated tree> -B <build dir> -DCMAKE_CXX_COMPILER=clang++ \
  -DSATURN_RUNTIME=<saturn-recomp>/runtime \
  -DSATURN_EXTRA=<sfiii-ps5>/tools/pc/saturn_extra.cmake -DSF3_PS5_ROOT=<sfiii-ps5>
ninja -C <build dir> sf3ps5
```

`tools/pc/saturn_extra.cmake` adds this repo to the generated build and links the runtime's
machine with our host instead of the runtime's SDL3 host.

## Run

```sh
SF3_X64_DIR=<code cache> sf3ps5 [--cue <image.cue>]
```

With no `--cue` it opens the launcher. Settings, saves (`saves/backup.bin`) and states
(`states/<disc>/slotN.sf3state`) go in `SF3_PS5_DATA` (default `./data`).

| Variable | Use |
| --- | --- |
| `SF3_PS5_DATA` | data folder |
| `SF3_PS5_OFFSCREEN=1` | no window (CI, headless boxes) |
| `SF3_PS5_SCRIPT=<file>` | the test driver below |

A disc only runs if the code cache was generated from that same disc. A cache built from the
translated Scenario 1 disc stops on any other image with "not an entry of an active module".

## Generic build (no disc-derived code)

Building against sfiii-native's `native/recomp/generic` instead of a generated tree gives an app
with no code made from a disc, which is the build the console title uses:

```sh
cmake -G Ninja -S external/sfiii-native/native/recomp/generic -B <build dir> -DCMAKE_CXX_COMPILER=clang++ \
  -DSATURN_RUNTIME=<saturn-recomp>/runtime \
  -DSATURN_EXTRA=<sfiii-ps5>/tools/pc/saturn_extra.cmake -DSF3_PS5_ROOT=<sfiii-ps5>
ninja -C <build dir> sf3ps5
```

On the first launch of each disc it shows a **Preparing** screen. It reads the disc, matches a
profile, runs discovery and the x64 lowering in C++ (no Python), and writes the module set to
`<data>/cache/<content id>-abi3/`. `<data>/cache/images.tsv` maps an image's size and mtime to
its content ID, so later launches skip the work. Delete `<data>/cache` to redo it. On the
console this is `/data/shiningforceiii/cache`.

| Variable | Use |
| --- | --- |
| `SF3_X64_SET=<dir>` | use a set made elsewhere and skip the Preparing step (dev path) |
| `SF3_PROFILES=<dir>` | profile JSONs (default: the submodule's `profiles/`; `/app0/profiles` on the console) |

`build/sf3prepare IMAGE DATA_DIR PROFILES_DIR` runs the same step on its own.

## One disc per process

Once a run stops, the runtime writes the backup memory and states, then ends the process with
`_Exit`, so `saturn_main` never returns. The app registers `sat_set_run_over_hook` (part of
sfiii-native's runtime patch), and the runtime calls it right before `_Exit`. When a disc was
chosen in the menu, or the player quit to the launcher, the hook relaunches the app with the new
arguments: `execv` on PC, `sceSystemServiceLaunchApp` on the PS5 (not yet tried on the console).

## Test driver

`SF3_PS5_SCRIPT` replaces the pad with a script. Each line is `GEN TICK op args`. GEN is the
process generation: 0, then +1 at each relaunch (`SF3_PS5_GEN`). TICK counts host frames.

| op | does |
| --- | --- |
| `press BUTTONS [TICKS]` | holds pad buttons (`cross`, `options`, `r2`, `down`, ...) |
| `save N` / `load N` | queues a save or load of state slot N |
| `menu` | opens the in-game menu |
| `launch IMAGE` | chooses a disc, as the launcher does |
| `quit` | quits to the launcher |
| `shot FILE.ppm` | writes the current frame |
| `exit` | ends the run, with no relaunch |

## Tested (translated Scenario 1, x64 cache, offscreen)

- Boot to the title, 2001 VBlanks in 33.4 s (paced at 1x). Sound at 48 kHz, not silent.
- Options as START and d-pad moves on the title menu. The menu overlay opens.
- Save to slot 0 and load it, in the same run and again after a relaunch.
- R2 fast-forward moves the game on.
- Choosing a disc in the menu ends the run and relaunches into it (generation 1). Quit to the
  launcher relaunches into the launcher (generation 2), and a launch from there boots the disc
  in the same process.
