# PS5 platform layer (clean-room, MIT)

sfiii-ps5 builds console titles with its own startup code, libc gap fillers,
linker script and companion module. It does not use the payload SDK's
`crt1.o`, `libc.a`, `libps5platform.a` or linker scripts. It also doesn't use
PS5_Vulkan's `app_crt.cpp`, `app_cpp_runtime.cpp`, `ps5-pie*.ld`, Agc stub
sources or generated `libc.prx`. All of those are GPL. Everything under
`ps5/platform/` was written for this repo from the sources listed below. No GPL
source was opened while writing it.

| Piece | File | Replaces | Console check (test title, 2026-10-29) |
|---|---|---|---|
| libc gaps, thread stacks, FP mode | `ps5/platform/title/*.c` | `libps5platform.a` | PASS, 602 frames 4K at 60.1 fps |
| startup and C++ `new`/`delete` | `ps5/platform/crt/` | `app_crt.cpp`, `app_cpp_runtime.cpp` | PASS, 602 frames at 60.1 fps |
| linker script | `ps5/platform/ps5-title.ld`, `title-exports.map` | `ps5-pie-unwind.ld`, `app-symbols.map` | PASS, 602 frames at 60.1 fps |
| companion module | `ps5/platform/companion/libc_companion.c` | generated `libc.prx` | **not yet run**: the console stopped launching test titles before it could be tried |
| Agc link stubs | `ps5/platform/agc_stubs.c` | PS5_Vulkan canary stubs | links (link-time only) |

`tools/ps5/build-test-title.sh` builds the swapchain test title from these
pieces.

## What was referenced

All of these are facts or behaviour descriptions, not source code:

- **Symbol lists.** These came from `llvm-nm` and `ld.lld` "undefined symbol"
  output: what RADV, libc++ and our code import, and the symbol names (not
  code) that the GPL objects define or reference. For example, the startup
  code needs `_start`, `_init_env`, `atexit`, `exit` and `main`.
- **Behaviour descriptions.** These came from comments and prose, not code:
  - the comments in PS5_Vulkan's `tools/radv-link.sh`, which list the
    functions the platform library supplies;
  - PS5_Vulkan's `runtime/README.md`, which describes what `libc.prx` is for;
  - PS5_Vulkan's `docs/DEPLOYMENT.md` (folder titles, ps5vkctl and klog);
  - the payload SDK README's licence section.
- **Program headers and section maps** of GPL-linked test executables
  (`readelf`, `-Map`). These are facts about what the converter and the console
  accept: four 16 KiB-aligned PT_LOADs, `.dynamic` first in the last one, and
  `.ctors` that have to run.
- **Mesa (MIT):** `src/util/u_qsort.h` (BSD `qsort_r` order) and
  `src/util/xmlconfig.c` (its own no-regex fallback). Meson
  `compile_commands.json` was used for configuration macros.
- **Public specifications:** the Intel SDM for the x87/MXCSR defaults, C/C++
  standard semantics for `operator new`/`delete`, and the FreeBSD argument-block
  convention for `_start`.
- **Console results:** klog from ps5-payload-dev's klogsrv. This is a GPL tool
  that was only run, never linked or shipped.

## Things found on the console

- A plain payload can't load `libSceAgc.prx`. Vulkan has to run as a title.
- driconf: if `regcomp` fails, Mesa applies **every** application workaround.
  This showed up as "RADV NAVI21 (LLVM 9.0.1)" and `vkCreateSwapchainKHR` -3.
  Our `regcomp` succeeds and nothing matches, which is Mesa's own fallback.
- The RADV archive's C++ constructors live in `.ctors`. A linker script that
  leaves them out hangs the GPU (suspend-point timeout).
- A title with no `sce_module/libc.prx` dies with `PRX_SCE_MODULE_LOAD_ERROR`.
  The executable imports nothing from it.
- Known problem: after `exit(0)` the title takes SIGSYS inside libkernel.
  This happens with both the GPL and the MIT startup code. Not fixed yet.
- Remaining tool dependency: PS5_Vulkan's `ps5-native-tool` (GPL host tool)
  still converts the ELF and makes the fake SELF. Its output contains only our
  input, so we use it but don't ship it.
