// SPDX-License-Identifier: MIT
// sce_module/libc.prx for sfiii-ps5: an empty companion module.
// A title's loader insists on loading /app0/sce_module/libc.prx before the
// executable runs (without it the launch dies with
// PRX_SCE_MODULE_LOAD_ERROR, seen on the console). The executable imports
// nothing from it: its C library calls go to libSceLibcInternal. So the
// module only has to load; it exports nothing and does nothing.
int module_start(unsigned long argc, const void* argv) { (void)argc; (void)argv; return 0; }
int module_stop(unsigned long argc, const void* argv) { (void)argc; (void)argv; return 0; }

// The ELF converter needs a .got in its input; one GOT-loaded reference to
// libkernel gives the module one without changing what it does.
extern int sceKernelGetCpumode(void);
int sf3_companion_cpumode(void) { return sceKernelGetCpumode(); }
