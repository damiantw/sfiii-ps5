// SPDX-License-Identifier: MIT
// sce_module/libc.prx for sfiii-ps5: an empty companion module.
// A title's loader insists on loading /app0/sce_module/libc.prx before the
// executable runs, and libSceLibcInternal looks up its "libc" and
// "libc_setjmp" exports (tools/ps5/libc-exports.tsv; added after conversion
// by tools/ps5/companion-exports.py, which also holds the few real ones).
// The executable imports nothing from it: its C library calls go to
// libSceLibcInternal.
int module_start(unsigned long argc, const void* argv) { (void)argc; (void)argv; return 0; }
int module_stop(unsigned long argc, const void* argv) { (void)argc; (void)argv; return 0; }

// The ELF converter needs a .got in its input; one GOT-loaded reference to
// libkernel gives the module one without changing what it does.
extern int sceKernelGetCpumode(void);
int sf3_companion_cpumode(void) { return sceKernelGetCpumode(); }

// Import the same system libraries the module interface expects
// (libSceSysmodule); never called. No libSceLibcInternal import: that module is the one loading us.
extern int sceSysmoduleIsLoaded(unsigned int);
void* sf3_companion_imports(int which) { return (void*)(long)sceSysmoduleIsLoaded((unsigned)which); }

// DT_INIT. The loader checks its result: a bare ret (eax left over) made
// libSceLibcInternal reject the module with PRX_SCE_MODULE_LOAD_ERROR.
int sf3_companion_init(void) { return 0; }
