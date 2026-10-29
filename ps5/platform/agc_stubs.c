// SPDX-License-Identifier: MIT
// Link-time stand-ins for the console's libSceAgc / libSceAgcDriver exports that RADV's PS5 winsys
// calls: built into libSceAgc.so / libSceAgcDriver.so stubs (soname *.prx) so the linker records
// the imports. They are never run; the console's modules provide the real functions.
#ifdef SF3_STUB_AGC
void sceAgcInit(void) {}
void sceAgcSuspendPoint(void) {}
#endif
#ifdef SF3_STUB_AGC_DRIVER
void sceAgcDriverSetHsOffchipParam(void) {}
void sceAgcDriverSetTFRing(void) {}
void sceAgcDriverSubmitDcb(void) {}
#endif
