# Shining Force III recomp: PS5 port design plan

Status: design draft, 2026-10-28 (America/New_York). Nothing here has run on a console. The console (192.168.1.70) was not touched while writing this.
Repo: fork damiantw/sfiii-native (local `/workspace/sfiii-native`, HEAD 932146d; the Premium Disc profile is on branch `premium-disc`, commit aa71e7c), upstream mattkuhnQAAI/sfiii-native.
Read-only references: `/workspace/PS5_Shipwright-notes-for-sfiii.md` ("Notes"), `/workspace/PS5_Shipwright{,-devtools}`, `/workspace/PS5_PayloadSDK/platform/docs/PROBE.md` ("PROBE").

## 0. Facts this plan rests on (with sources)

| Fact | Source |
|---|---|
| The recompiler is **Python** (`saturnrecomp`: `recomp/discover.py`, `recomp/emit.py`, `build.py`). It emits **C++**, one function per discovered SH-2 function, and builds it with CMake + clang++ | `.local/saturn-recomp/src/saturnrecomp/recomp/emit.py` header; `build.py:88` |
| The output is big: linked Linux binaries are 107 MB (Sc1), 146 MB (Sc2) and larger for Sc3. Function counts: 108,972 (Sc1), ~150,900 (Sc2), 174,166 (Sc3) | `/workspace/sweep/bins-63be8cd`; `sweep/STATUS.md` coverage table |
| Computed jumps the static analysis can't resolve go to `sh2_call` dispatch. An unknown target is FATAL today; there is **no runtime interpreter** in C++. The reference interpreter `sh2emu.py` exists only in Python, for tests | emit.py header; STATUS.md (map 0x70 FATAL); DEVELOPMENT.md:53 |
| The BIOS is **HLE** (`runtime/src/machine/bios.cpp`). No BIOS file is needed | bios.cpp:1-10 |
| The runtime (VDP1/VDP2/SCSP/SCU-DSP/CD) is C++ in `runtime/src/{machine,video,sound}`. Our changes sit in `tools/runtime-diagnostics.patch` (469 KB) on top of saturn-recomp c8b724d. Both are **MIT** | `dependencies.json`; both LICENSE files |
| The GPU renderer is SDL3 **SDL_GPU** using SPIR-V shaders compiled offline from HLSL with DXC (`-spirv`) | `native/gpu-renderer.cpp:20`, `tools/package-desktop.py:111-115` |
| **On PS5, executable memory works.** You map direct memory RW and then `sceKernelMprotect` it to RX or RWX; mapping with exec directly is refused (0x80020016). A protection change costs ~26 µs. Dual RW/RX views and SIGSEGV backpatching work. `sceKernelJit*SharedMemory` is **refused** (0x80020001) | PROBE "Executable code in direct memory", "The shared-memory JIT interface" (fw 12.09) |
| **LLVM already runs inside a PS5 title**: the RPCS3 core ("LLVM compilers reserve 768 MiB each") | PROBE, end of the "Reservations" section |
| Memory: 12 GiB direct memory (11.87 GiB in one allocation), 448 MiB flexible. Shipwright's param.json sets flexibleMemorySize 1 GiB | PROBE "The pools"; Notes §1 |
| File writes run ~230-260 MiB/s with `write()` in chunks of 1 MiB or more; stdio is 10-100x slower. Reads run 1.9-3.7 GiB/s | PROBE "Files" |
| Known-good Vulkan path: RADV (PS5_Mesa) linked statically, FIFO present, 3840x2160 at 60 measured. SDL **2** (2.32.10 + sdl.patch) video, audio and joystick drivers. Audio is `sceAudioOutOpen` at 48 kHz s16 stereo | Notes §2-3 |
| On-console asset extraction precedent: Torch builds oot.o2r on console in 30-34 s, with a stamp file and auto-rebuild | Notes §4 |
| Translation patcher: `tools/sf3patch` (Python). It reproduces the SF3Patch.exe wizard (reads the official package, applies xdelta-style patches, rebuilds the ISO: `isobuild.py`, `edcecc.py`, `cdimage.py`) | `docs/translation-patcher.md`, `tools/sf3patch/*` |

## 1. The central question: producing game code on the console

Everything the recompiler outputs comes from the disc, so it can't be shipped. Whatever turns SH-2 into host code has to run **on the PS5**, against the user's ISOs in `/data/shiningforceiii`. The shipped eboot holds only the runtime, the analyzer/compiler and the profiles, which hold addresses and hashes but no disc bytes. (Exception to check: the profile JSONs list module names, addresses and hashes. That is identifying metadata and already ships on PC.)

### Option A: run the existing pipeline (Python recompiler + clang) on the PS5
- Python: there's no PS5 CPython port in our trees. Porting CPython plus its native modules is a multi-week side project, and `discover.py` depends on `analysis/`, `disc.py` and more.
- clang/LLVM on PS5: LLVM-as-a-library is **proven** to load and JIT in a title (RPCS3, PROBE). A standalone `clang` driver run as a subprocess is unproven: titles can't simply `exec` a second ELF. You'd link clang's libraries in-process. That means the clang frontend plus libLLVM in the eboot: about 60-100 MB of code and a 768 MiB reservation per compiler.
- tcc: tiny and fast, but it is **LGPL**, so it breaks the MIT-only rule. It can't be used.
- Cost: our C++ output at -O2 is very large (~150k functions, ~100+ MB binary). Building it on an 8-core host takes minutes (exact time UNKNOWN; it isn't in the sweep logs). The PS5 CPU is 8 Zen 2 cores at ~3.5 GHz with fewer usable for titles, so expect **tens of minutes to more than an hour per scenario**. A C++ frontend is the slowest part of that.
- Verdict: **feasible but heavy**: Python port + clang in-process + long first-run build. Risk is high (C++ frontend memory, LLVM's PS5 TLS/kqueue quirks per PROBE). Effort ~8-12 weeks.

### Option B: in-process SH-2 → host code backend (no C++ in the middle) — **RECOMMENDED**
- Port `discover.py` + `emit.py` from Python to C++ as `sf3recomp` (MIT, our code, a translation of MIT saturn-recomp). The emit step stops printing C++ and instead drives one of two backends:
  - **B1 (direct x86-64 emitter)**: template lowering per SH-2 instruction, a small register cache for R0-R15/T/PR, and the same calling convention and `SH2_POLL`/`SH2_RET` semantics as emit.py. SH-2 is a simple 16-bit fixed-width RISC with ~140 opcodes, so this is a well-understood dynarec-style backend. Compile speed is ~ms per thousand functions, so a whole scenario builds in seconds. Code quality is about 2-4x worse than clang -O2, but a 28.6 MHz SH-2 pair on Zen 2 has huge headroom (the PC build runs 2-3x speed on PC: USAGE/DESKTOP.md "Scenario 3 … 2× request").
  - **B2 (LLVM IR via the LLVM C API, ORC or object emission)**: the same IR-building code emits LLVM IR instead of bytes. That gives near-clang quality without a C++ frontend. Precedent: RPCS3 on PS5. Use it as an optional "optimize" pass run in the background and cached.
- Output goes to an **on-console code cache** `/data/shiningforceiii/cache/<content_sha>/<build_id>.sf3code`: relocatable blobs plus a function table. It's written with large `write()` calls (PROBE: use descriptors, not stdio) and loaded at boot into direct memory mapped RW, then `sceKernelMprotect` RX, placed within ±2 GiB of the eboot so rel32 calls reach runtime helpers (PROBE: "Placement near an anchor … all ran").
- The cache is keyed by disc content hash (profile `content_sha256`), the eboot build id and the patch set. If any of them is missing or changed, rebuild, exactly like Shipwright's oot.o2r stamp (Notes §4).
- Equivalence: the new backend must be **bit-identical in behavior** to the C++ path. That's provable on the box with the existing harness. Build a Linux host version of B1 and run the sweep and differential (`tools/sf3-autoplay.py`, `tools/sf3-block-coverage.py`, the testing-plan routes) against today's clang binaries: same `--progress`/state block, same shots, same `--save`. The harness exists (STATUS.md: deterministic agent runs, identical coverage across builds).
- Effort: port discover/analysis ~3-4 weeks; B1 backend + Linux-hosted validation ~4-6 weeks; B2 optional +3 weeks. Risk is medium: the semantics are pinned by `sh2emu.py` and the 77 runtime tests plus generated-code selftests (`recomp/selftest.py`).
- Extra win: the same B1 engine serves as **Option C** for free.

### Option C: fallback interpreter / dynarec for uncovered code
- Today an unresolved computed jump or an unknown overlay is FATAL (STATUS: "returned to … expected …"). On PS5 we can't regenerate on the host, so add a C++ **SH-2 interpreter** ported from `sh2emu.py`, plus optional **lazy JIT** through B1: on a `sh2_call` miss, discover from that address, compile, insert into the dispatch table, and log it into the cache so the next boot has it AOT.
- Behavior risk: the interpreter must match the recompiled timing model (SH2_POLL safe points, cycle accounting in `runtime/src/core`). Validate it on Linux by forcing interpreter-only runs through the sweep.
- Not a primary path. On its own (no AOT) it would also work and is the lowest-effort way to *boot*, but it risks frame pacing in battles.

### Option D: other ideas, evaluated
- **D1: precompiled disc-independent code**: impossible. The code *is* the disc.
- **D2: user runs the PC pipeline and copies the build over**: violates "any build step with disc images happens on console". Can be kept as an undocumented dev path only.
- **D3: ship deltas/"patch bytecode" against the disc**: still disc-derived. Rejected.
- **D4: Beetle Saturn as the engine**: GPL, and the user's rule is black-box/reference only. Rejected for the port.

**Recommendation: B1 (C++ analyzer + direct x86-64 backend + on-console code cache), with C (interpreter + lazy JIT on misses) from day one, and B2 (LLVM) as an optional later optimization.** Prototype first on Linux x86-64 (the same ISA as the PS5) so all equivalence testing happens on the box, then move to the console only for the mprotect/placement smoke test.

## 2. Pipeline on console (first launch)
1. Scan `/data/shiningforceiii` for `.iso` / `.bin+.cue` (CHD only if a license-checked MIT/BSD decoder is used; to verify). Identify by content (port `desktop/disc_input.py` + `tools/disc_profile.py` + `tools/sf3patch/identify.py` to C++). Every candidate gets a reason line, like Shipwright's "Game files needed" screen.
2. Optional patch step (§8) → patched image (in place or new file).
3. Analyze + compile per profile (`profiles/scenario1-usa-v25_1.json`, `scenario2-v25_1-v2_000.json`/`-v1_003`, `scenario3-v25_1(-redump).json`, `premium-disc-v25_1.json`), with a progress bar. Expected time with B1: seconds to under a minute. Write the cache plus a stamp.
4. Launch the 4-slot launcher (Sc1/Sc2/Sc3/Premium) with shared saves, same as `desktop/launcher_posix.py` + `desktop/shared_saves.py`.

Translation profiles: the JP discs are only usable translated. Slots require Sc2/Sc3/Premium in a V25.1 state, which means the on-console patcher is effectively **required** for JP discs. Alternatively, the user supplies already-patched images (they're identified by content either way).

## 3. Video: Vulkan, 4K, widescreen
- Port: SDL_GPU (SDL3) isn't in the PS5 stack (only SDL2 2.32.10). Two paths:
  - (a) port SDL3 + the SDL_GPU Vulkan backend to PS5 on top of PS5_Vulkan/RADV. The SDL_ps5video/joystick/audio drivers from PS5_Shipwright `ps5/sdl/*` would be rebased to SDL3.
  - (b) write a thin direct-Vulkan backend for `native/gpu-renderer.cpp` + `hd-gpu.cpp` (only ~390 lines of SDL_GPU calls total), reusing the existing SPIR-V shaders (`package-desktop.py` already emits `-spirv -fspv-target-env=vulkan1.0`).
  Recommend **(b)**: smaller, no SDL3 port, full control of present (FIFO), and the swapchain at 3840x2160 like Shipwright.
- 4K: internal-resolution upscale of VDP1 polygons/sprites (existing 1x/2x/5x machinery; add a "fit 2160p" scale: 240 lines × 9 = 2160 for 4:3 content). VDP2 planes are rendered at native res and scaled with nearest/sharp-bilinear unless the GPU-plane path covers them. Pixel-exact source sampling is unchanged, so game logic is unaffected.
- Widescreen: the existing "Aspect ratio (beta) 16:9" (USAGE.md:73) widens the rotation-floor viewport and VDP2 scroll window. Game-side culling uses 4:3 assumptions, so there's pop-in at the edges. **Behavior risk**: if widening is done by patching game culling constants in SH-2 code, that *changes* game RAM/logic and can desync saves/states and shift RNG timing. User priority is "no game behavior change", so:
  - Default **4:3 pillarboxed** at 4K (zero risk), with an option for **presentation-only 16:9** (renderer-side extension only: no game RAM writes; edge pop-in accepted).
  - Any culling hack (game-RAM writes, like SoH's WidescreenActorCulling, Notes §2) is off by default and labeled "changes game behavior".
  - Saturn 320 vs 352 horizontal modes: scale to 2160p height and derive width from the TV-mode pixel aspect (352 lines are not wider FOV, just narrower pixels). Handle both in the presenter.
- Lessons from Shipwright to apply: growable vertex/uniform rings with high-water logging; pipeline precompile at startup plus a `/data/shiningforceiii/cache/` pipeline cache; no CPU readbacks outside a frame; keep screenshot readbacks out of perf windows.

## 4. Audio
SCSP (`runtime/src/sound/scsp.cpp`) produces 44.1 kHz. Resample to 48 kHz (a small linear/polyphase resampler, MIT) and push grains on a dedicated thread through the platform layer `audio_start()` → `sceAudioOutOpen(0xff,0,0,GRAIN,48000,1)` / `sceAudioOutOutput` (Notes §3). Keep the existing speed-up audio path (1x/2x/3x) behind it. CDDA tracks go through the same mixer.

## 5. Input
Use the PS5_PayloadSDK pad layer directly (DualSense), or Shipwright's SDL2 joystick driver for the shared mapping. Map to Saturn pad slots (`runtime/src/host/pad_slots.h`). The touchpad click opens the menu (same as Shipwright). Handle the "first press only wakes the controller" gotcha.

## 6. CD / ISO layer
The runtime's CD block plus accurate-drive timing (HEAD 932146d) read CUE/BIN. Add an ISO (2048-byte sectors) adapter and a 2352-byte BIN path. Use `pread` with ≥1 MiB read-ahead (reads run 1.9-3.7 GiB/s, so no need to load whole discs). Audio tracks come from BIN+CUE. A pure-ISO dump has no CDDA, so warn the user.

## 7. Saves and states
- Backup RAM/cartridge files live in `/data/shiningforceiii/saves/` and keep the PC formats so `tools/import-saturn-save.py` and shared Sc1→2→3 transfer (`desktop/shared_saves.py`; Premium writes Sc3 SFORCE33_NN records) work unchanged.
- Save states: the existing native states (`runtime/src/machine/state.cpp`) are build-specific, so key them to the code-cache build id. 10 slots per scenario plus thumbnails.
- Small files can use stdio; states (MBs) go through `write()` in 1 MiB chunks.

## 8. Patcher on console
- Port `tools/sf3patch` (Python: identify, xdelta-style apply, `isobuild.py` ISO rebuild, `edcecc.py` EDC/ECC, `selection.py` for add-ons) to C++.
- The translation package (SF3Patch.exe and V25+ add-ons) is **not distributed**. The user drops it into `/data/shiningforceiii/patches/`. Optional in-app download is only possible if the user opts in and the console has internet (fetch.py does this on PC). That's a user decision.
- Output: in place (after a backup, or with an explicit warning) or a new image (`<name>-v25_1.bin/cue`). Validate it against `content_sha256` from the profiles (translation-patcher.md "New disc profiles from content_sha256").
- Check the license of any xdelta decoder: xdelta3 is Apache-2.0 (OK) or GPL depending on version. Verify, or write a clean-room VCDIFF decoder (it's small).

## 9. Menu (ImGui, Shipwright-style)
ImGui is already vendored (`runtime/third_party/imgui`). Port Shipwright's PS5 navigation (touchpad toggle, amber focus, L1/R1 tabs, scale 1.5-3.0x, PS glyphs; Notes §5). Pages:
- Launcher: 4 slots + disc status + build/patch.
- In-game: save/load state, speed 1x/2x/3x + fast-forward hold, display (scale, 4:3/16:9 presentation, filter, sharp/nearest), audio, controls remap, saves import/export, about/licenses.

## 10. Packaging
- Title folder `PPSA<new id>/`: `eboot.bin` (link via `link-title.sh`-style recipe with PS5_Vulkan RADV + CRT + fake-SELF tool), `sce_sys/param.json` (new titleId/contentId, `kernel.flexibleMemorySize` ≥1 GiB; the code cache lives in direct memory, not flexible), icon0/pic, `sce_module/`, `licenses/`.
- `release.sh`-equivalent refuses ISOs, caches, saves and patch packages, and CI greps for disc bytes. Install to `/data/homebrew/PPSA…/` via ShadowMountPlus; data in `/data/shiningforceiii/` with the title-folder fallback and `env.txt` override (Notes §3).

## 11. Phases and milestones
| Phase | Deliverable | Exit criteria | Est. |
|---|---|---|---|
| PD | Debug server + crash handler (sfiii-ps5 `src/devtools`, dev builds only, OFF in release): agent-protocol superset over TCP (pad, step/pause/run, read/write memory and flags, frame/screenshot, save/load state, log); sf3-story/sf3test drive it via `sf3dev.Client` | Linux fake-machine test passes (**done**); on console: listen on fw 13.x, then drives the first P3 tests (needs console permission) | 1 w, started early |
| P0 | C++ port of discover/analysis (`sf3recomp`), Linux | Same function set/entries as Python for all 5 profiles (diff the tables) | 3-4 w |
| P1 | B1 x86-64 backend + loader + code cache, Linux | Sweep/differential identical to clang builds on all profiles | 4-6 w |
| P2 | C++ SH-2 interpreter + lazy compile on miss | Interpreter-only sweep matches; FATAL-on-miss gone | 2-3 w |
| P3 | PS5 bring-up: eboot, mprotect loader smoke test, CD/ISO, audio, pad, headless runs | Sc1 title on console from user ISO, cache built on console | 2-3 w |
| P4 | Direct Vulkan renderer, 4K present, 4:3/16:9 presentation | 60 fps locked at 2160p in battle scenes (measure like Notes §2) | 3-4 w |
| P5 | ImGui menu, save states, launcher 4 slots, shared saves | All slots; Sc1→Sc2 transfer | 2-3 w |
| P6 | On-console patcher (C++ port) | Patched images match profile `content_sha256` | 3-4 w |
| P7 | Packaging/CI/release; optional B2 LLVM | Release zip; no disc-data check | 2 w+ |

## 12. Risks
- Backend bugs give wrong behavior quietly. Mitigation: Linux-hosted equivalence against the C++ path; the interpreter as a cross-check (e.g. a debug mode running both).
- The firmware/jailbreak exec policy may differ from fw 12.09 (PROBE). The user's console reported fw 13.x (Notes §1). Re-run the PROBE exec test on that console in P3 before committing.
- Discovery gaps: still only 3-4% of functions covered (STATUS), so lazy compile plus logging is essential.
- Memory: code cache ~100-300 MB per scenario in direct memory. Fine against 12 GiB.

## 13. Decisions (approved)
- Route B1 (own generator plus an SH-2 interpreter fallback); Vulkan; 4K.
- Video: 4:3 by default, with an optional 16:9 mode that changes only the renderer and writes no game memory.
- Translation patches: an opt-in on-console download. Both patch-in-place and write-new-images are offered.
- Disc formats: BIN/CUE and CHD. CHD uses libchdr (BSD-3). Its parts are lzma (PD), miniz (unlicense), zstd (we take the BSD side of its BSD/GPLv2 dual license) and dr_flac (PD/MIT-0). No GPL/LGPL.
- First release: all four discs plus both Sc2 versions. The store release comes later.
- Title ID **PPSA93303**, contentId UP9000-PPSA93303_00-SF3RECOMP0000000. It isn't used in any checked catalog; the evidence is in title-id.md.
- Repo split:
  - **damiantw/sfiii-ps5** (public, MIT): sfiii-native is a submodule at external/sfiii-native. It holds packaging, the title ID, the Vulkan backend, the on-console patcher, the menu and the dev tools.
  - The fork **damiantw/sfiii-native**: the generator port and the interpreter fallback, added as fork PRs (branch `ps5-port`).
- Dev tools (phase PD): this follows Shipwright's dev-tools branch. The debug server is built only when SF3_PS5_DEVTOOLS=ON, and only listens when `/data/sfiii/devtools.enable` exists. The crash handler is in every build. See sfiii-ps5 docs/devtools.md.

## 14. Original open questions (answered in §13)
1. Accept B1 (own x86-64 backend, fast on-console build) over A (clang/LLVM, slow on-console build, heavier)?
2. Widescreen default: 4:3 pillarbox at 4K (no behavior change) with presentation-only 16:9 opt-in, plus game-culling hacks off/labeled. OK?
3. Translation package source: user copies SF3Patch package manually, or allow opt-in on-console download?
4. Patch in place: allowed by default, or always write new images?
5. Disc formats to support: ISO only, BIN/CUE, CHD? (CHD needs a license-checked decoder.) Note that a plain ISO loses CDDA music.
6. Title ID / name / icon for the package; fw version of the target console(s).
7. Should Sc2 offer both V25.1 v2.000 and v1.003 profiles, and is Premium Disc in scope for v1 (profile is experimental)?
