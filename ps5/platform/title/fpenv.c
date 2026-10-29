// SPDX-License-Identifier: MIT
// Default IEEE-754 floating-point environment: x87 control word 0x037F
// (all exceptions masked, 64-bit precision, round to nearest) and MXCSR 0x1F80
// (all SSE exceptions masked, round to nearest, no FTZ/DAZ). Values from the
// Intel SDM vol. 1 (8.1.5, 10.2.3), the power-on defaults FNINIT/LDMXCSR give.
void sf3_fp_ieee(void) {
    unsigned short cw = 0x037F;
    unsigned int mxcsr = 0x1F80;
    __asm__ volatile("fninit\n\tfldcw %0" : : "m"(cw));
    __asm__ volatile("ldmxcsr %0" : : "m"(mxcsr));
}
void ps5_fp_ieee(void) __attribute__((alias("sf3_fp_ieee")));
