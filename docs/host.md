# The PS5 host (`src/host`)

saturn-recomp's machine calls a small host API (`runtime/src/host/host.h`). The desktop
builds use SDL3 for it (`host.cpp`); sfiii-ps5 has its own, in `host_runtime.cpp` on top of
`PlatformHost`. All of these are called from the machine thread.

| call | what it does |
|---|---|
| `host_present(Frame)` | `VkScene::upload_picture` copies the field (0x00RRGGBB) into the last pass. `present_to` places it with `place()` (4:3 at an integer scale when one fills ≥90 % of the height; 16:9 uses the game-composed columns `origin_x`/`native_w`, else 4:3). Sharp pixels picks the nearest-neighbour filter. The menu or a save/load message is drawn over it. |
| `host_wants_frame` | true on every `speed`-th VBlank. A field that isn't shown still polls the pad and the menu. |
| `host_pace(now)` | `Pacer`: field N is due at base + now/speed. More than 100 ms off (a load, the menu) moves base, so the game never races to catch up. |
| `host_pad(0)` | `saturn_buttons`. The game gets nothing while the menu is up. |
| `host_audio_push` | `AudioOut`: 44.1 kHz in, 48 kHz out (`SDL_AudioStream`). |

## The menu while playing
The touchpad opens it. `host_present` then runs the menu (one frame per vsync, FIFO) until it
closes, and the machine thread waits there. `RuntimeHost` raises `g_host_paused`, so the stall
watch doesn't fire, and sound is paused. When the menu closes, the pacer restarts from that
moment. A save or load runs at the runtime's next safe moment, after the menu closes. Its
result shows for 3 s in a corner and on the menu's Game page.

## Buttons
sfiii-native's default layout (`native/controllers.h`):

| PS5 | Saturn |
|---|---|
| Square | A |
| Cross | B |
| Circle | C |
| Triangle | X |
| L2 | Y |
| R2 | Z (R3 while hold-to-fast-forward is on, since R2 is fast-forward then) |
| L1 / R1 | L / R |
| Options | START (in game; in the launcher Options also toggles the menu) |
| D-pad or left stick | directions (an opposite pair cancels, as in sfiii-native) |
| Touchpad | the menu |

## Speed and sound
At 2x/3x the pacer waits now/speed, and only every 2nd/3rd field is drawn. The audio stream
takes the game's samples as 88.2/132.3 kHz, so the sound plays faster and higher, as it does
on the desktop. The queue to the device is kept at 200 ms or less; above that a chunk is
dropped. When it runs empty, 60 ms of silence goes in first, so a stall clicks once instead of
crackling. `host_audio_report` prints the underrun and drop counts.

## Launch and quit: one disc per process
Each scenario is its own recompiled program, and the runtime keeps machine-wide state. So a
disc always starts in a fresh process instead of being torn down and rebuilt in place:
- Without `--cue`, the app starts in the launcher. Picking a disc there starts the runtime in
  the same process.
- **Quit to launcher** (or another disc from the in-game launcher page) calls `state_stop`.
  The runtime ends the run, writes the backup memory and states, then calls the run-over hook
  (`sat_set_run_over_hook`) before it ends the process with `_Exit`. `saturn_main` does not
  return. From the hook the app relaunches itself, with no arguments or with `--cue <image>`.
- On PC that's `execv` of `/proc/self/exe` (tested in `host_test`). On PS5 (`SF3_PS5_CONSOLE`,
  `SF3_PS5_TITLE_ID`) it's `sceSystemServiceLaunchApp(own title ID, argv)` for the foreground
  user, followed by `_Exit(0)`.

Constraints, unverified until a console test:
- Whether the system accepts a launch request for the app that is making it, and starts the
  new instance once the old one exits. If it refuses, the error is logged and the app exits
  cleanly. The player then starts it again from the home screen. Saves and settings are
  already on disk, so nothing is lost.
- How a process exit behaves on the console. A known issue, an exit that ends in SIGSYS, is
  still being looked into, which is why the exit path uses `_Exit` after the run has already
  flushed its files.

## Files
- saves: `<data>/saves/backup.bin` (+ `.cartridge`), one backup memory shared by the scenarios,
  as sfiii-native's shared saves
- states: `<data>/states/<disc id>/slotN.sf3state`, with `SF3_STATE_ID` = build id + disc id
- logs: `<data>/logs`

`<data>` is `/data/shiningforceiii` (or `$SF3_PS5_DATA`).
