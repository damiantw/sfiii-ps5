/* SPDX-License-Identifier: MIT
 * sfiii-ps5 title layout, written from scratch for lld.
 *
 * Requirements (docs/platform.md, "Linker script"), all taken from what the
 * ELF-to-SELF converter and the console accept, checked on the console:
 *  - text at address 0, every segment 16 KiB aligned (the console page size);
 *  - four PT_LOADs: code (R+X), read-only data (R), the dynamic-link tables
 *    with the GOT and constructor arrays (RW), then .dynamic, .data and .bss
 *    (RW) with .dynamic first;
 *  - no PT_INTERP, PT_PHDR or GNU_* headers.
 */
PHDRS
{
    code    PT_LOAD FLAGS(5);
    rodata  PT_LOAD FLAGS(4);
    tables  PT_LOAD FLAGS(6);
    data    PT_LOAD FLAGS(6);
    dynamic PT_DYNAMIC FLAGS(6);
}

SECTIONS
{
    . = 0;
    .text : ALIGN(0x4000) { PROVIDE_HIDDEN(__sf3_text_start = .); *(.text.unlikely .text.*_unlikely .text.unlikely.*) *(.text.startup .text.startup.*) *(.text.hot .text.hot.*) *(.text .text.* .gnu.linkonce.t.*) *(.init) *(.fini) PROVIDE_HIDDEN(__sf3_text_end = .); } :code

    . = ALIGN(0x4000);
    .eh_frame_hdr : ALIGN(0x4000) { __eh_frame_hdr_start = .; *(.eh_frame_hdr) __eh_frame_hdr_end = .; } :rodata
    /* The SDK's libunwind finds unwind tables through these bounds. */
    .eh_frame : { __eh_frame_start = .; KEEP(*(.eh_frame)) __eh_frame_end = .; } :rodata
    .gcc_except_table : { *(.gcc_except_table .gcc_except_table.*) } :rodata
    .rodata : { *(.rodata .rodata.* .gnu.linkonce.r.*) } :rodata

    . = ALIGN(0x4000);
    .data.rel.ro : ALIGN(0x4000) { *(.data.rel.ro.local* .data.rel.ro .data.rel.ro.*) } :tables
    .got : { *(.got) } :tables
    .got.plt : { *(.got.plt) } :tables
    .preinit_array : { KEEP(*(.preinit_array)) } :tables
    /* The SDK compiler still puts C++ constructors in .ctors (the RADV archive
     * has three); they are folded into .init_array so start.c runs them. */
    .init_array : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*))) KEEP(*(.init_array)) KEEP(*(.ctors .ctors.*)) PROVIDE_HIDDEN(__init_array_end = .); } :tables
    .fini_array : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*))) KEEP(*(.fini_array)) KEEP(*(.dtors .dtors.*)) PROVIDE_HIDDEN(__fini_array_end = .); } :tables
    .dynsym : { *(.dynsym) } :tables
    .gnu.hash : { *(.gnu.hash) } :tables
    .hash : { *(.hash) } :tables
    .dynstr : { *(.dynstr) } :tables
    .rela.dyn : { *(.rela.init) *(.rela.text .rela.text.*) *(.rela.rodata .rela.rodata.*) *(.rela.data .rela.data.*) *(.rela.got) *(.rela.bss .rela.bss.*) *(.rela.dyn) } :tables
    .rela.plt : { *(.rela.plt) } :tables

    . = ALIGN(0x4000);
    .dynamic : ALIGN(0x4000) { *(.dynamic) } :data :dynamic
    .data : { *(.data .data.* .gnu.linkonce.d.*) } :data
    .tdata : { *(.tdata .tdata.*) } :data
    .bss : { *(.dynbss) *(.bss .bss.* .gnu.linkonce.b.*) *(COMMON) } :data

    /DISCARD/ : { *(.interp) *(.note.GNU-stack) *(.gnu_debuglink) *(.gnu.lto_*) *(.comment.*) }
}
