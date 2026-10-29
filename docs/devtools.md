# Dev tools: debug server and crash handler

Goal: automate playthroughs and debugging on the console without Remote Play: drive input,
save/load states, read and write memory and story flags, take screenshots, read logs.
Modelled on the PS5 Shipwright dev-tools branch (debug server + crash handler, dev builds only).

## Build rules
- `SF3_PS5_DEVTOOLS=ON` builds the server. Release and store builds keep it **OFF**, so
  the code isn't compiled in at all. `tools/check-release.sh ELF` refuses an ELF carrying `devtools_` symbols.
- Even in a dev build nothing listens unless `/data/sfiii/devtools.enable` exists. It can hold these lines:
  `port=9200` (default; a title is refused other ports such as 9300 and 9301 with EACCES), `token=SECRET` (clients must `auth SECRET` first), and `bind=0.0.0.0`.
- The crash handler (`src/devtools/crash_handler.cpp`) is in every build. It only writes
  `/data/sfiii/crash/crash-<time>.txt` (signal, fault address, return addresses) and has no network code.
  Symbolize these files on the box.

## Protocol
TCP, one text command per line. The server sends `hello sf3dev 1` on connect. It is a superset of
saturn-recomp's `--agent` protocol (runtime/src/machine/agent.cpp), so sf3test / `tools/sf3-story.py`
logic (drive, replay segments, story-flag checks) runs against the console unchanged through
`tools/devtools/sf3dev.py` (`Client` has the `agent.Run` methods: step, pad, read, write, frame, vblank).

| command | answer |
|---|---|
| `ping` | `pong VBLANK` |
| `info` | `info ...`: disc, build, title ID |
| `pause` | `ok`, then `vblank N` at the next VBlank-IN; the run stays paused |
| `step N` | runs N VBlanks, then `vblank N` (paused again) |
| `run` | `ok`; the game runs free (a fresh client starts free-running) |
| `pad [P.]BUTTONS` | `ok`, as `--input` would |
| `read ADDR LEN` / `write ADDR HEX` | `data HEX` / `ok`, as the CPU sees memory |
| `frame` | `frame W H` + W*H*3 RGB bytes; `sf3dev.py shot x.png` |
| `save PATH` / `saved` / `load PATH` | native states at the next safe point |
| `log [N]` | `log K` + the last K log lines |
| `throw` | `caught devtools throw`: a C++ exception thrown three calls deep and caught, to check unwinding on the console |
| `detach` (or `quit`) | `bye`; the game keeps running (quit never ends the title) |

Commands are served on the emulation thread at VBlank-IN only, so a paused run is exactly as
deterministic as `--agent`: the same presses at the same VBlanks give the same result.

## Status
- Done: the server, client and crash handler are tested on Linux against a fake machine
  (`cmake -DSF3_PS5_DEVTOOLS=ON`, then `tools/devtools/test_sf3dev.py build/test_debug_server`).
- Wired to the runtime: `src/devtools/runtime_agent.cpp` replaces the runtime's `machine/agent.cpp` in the PS5 build.
  It provides `agent_poll()`, which `machine.cpp` calls at every VBlank-IN, and fills the hooks: `ld8`/`st8`, `smpc_press`, `vdp2_compose`, `state_request_save`.
  In a release build, `agent_poll()` is empty and has no `devtools_` references.
  It compiles against the runtime headers. Running it inside a real runtime build waits for the PS5 build (or a desktop test build).
- Next:
  - Add `load` to the runtime. Today it only has `--state-in` at start-up.
  - Add a `--agent-url tcp://...` option to sf3-story (fork PR).
  - First console tests: does bind/listen work in this title on fw 13.x? Shipwright saw it work on 12.x.
    This needs the console and the user's permission.

## Compile check against the runtime
`cmake -DSF3_PS5_RUNTIME_DIR=<patched saturn-recomp> [-DSF3_PS5_DEVTOOLS=ON]` builds `runtime_agent.cpp` against the real runtime headers. In release mode the object has no `devtools_` symbols. In dev mode it references `devtools_start/poll`, `state_request_save` and `smpc_press`. `load PATH` loads a native state live at the next safe moment (fork PR #105, runtime patch), and `loaded` reports `done 1` (loaded) or `done -1` (no safe moment came within 120 VBlanks).

## Launcher (dev builds, PS5)

Build with `SF3_PS5_DEVTOOLS=ON tools/ps5/build-title.sh`. The launcher reads `/data/sfiii/devtools.enable` too:

- `port=N`: the launcher listens for the same protocol, and `pad [N.]BUTTON+...` holds menu buttons (UP DOWN LEFT RIGHT CROSS CIRCLE SQUARE TRIANGLE L1 R1 OPTIONS TOUCHPAD) for N frames. Use port 9200: a title's bind elsewhere (9300, 9301) is refused with EACCES. The launcher's last line shows the result.
- `launch=/data/shiningforceiii/DIR/NAME.cue`: start that disc right away with no input, for automated runs.
