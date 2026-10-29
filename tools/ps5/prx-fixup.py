#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Turn the converter's output for a shared module into a loadable PRX.

ps5-native-tool `link` always writes an application: e_type ET_SCE_DYNEXEC (0xFE10) and a
PT_SCE_PROCPARAM segment. rtld rejects that as a module ("Unsupported ELF e_type"). A module is
ET_SCE_DYNAMIC (0xFE18) and carries PT_SCE_MODULE_PARAM (0x61000002) instead. The module param is
{u64 size=0x20, u32 magic 0x3C13F4BF, u32 version 3, u64 sdk words copied from the proc param,
u64 1}; it fits in the proc param's space, so the file layout does not change.
usage: prx-fixup.py <in.elf> <out.elf>"""
import struct, sys
d = bytearray(open(sys.argv[1], "rb").read())
assert d[:4] == b"\x7fELF" and struct.unpack_from("<H", d, 16)[0] == 0xFE10, "not an ET_SCE_DYNEXEC"
struct.pack_into("<H", d, 16, 0xFE18)
phoff, = struct.unpack_from("<Q", d, 0x20); phentsize, phnum = struct.unpack_from("<HH", d, 0x36)
for i in range(phnum):
    p = phoff + i * phentsize
    if struct.unpack_from("<I", d, p)[0] != 0x61000001: continue
    off, = struct.unpack_from("<Q", d, p + 8)
    size, magic, _ver, sdk = struct.unpack_from("<QIIQ", d, off)
    assert size >= 0x20 and magic == 0x4942524F, "unexpected proc param"
    d[off:off + size] = bytes(size)
    struct.pack_into("<QIIQQ", d, off, 0x20, 0x3C13F4BF, 3, sdk, 1)
    struct.pack_into("<I", d, p, 0x61000002)
    for f in (0x20, 0x28): struct.pack_into("<Q", d, p + f, 0x20)  # p_filesz, p_memsz
    break
else: sys.exit("no PT_SCE_PROCPARAM")
# The module's identity: DT_SONAME, module info version 1.1 and an export library for the module's
# own name, flagged as exported. The converter leaves these out; there is room in the slots of the
# zero-valued tags it writes (DEBUG, INIT/FINI and the empty init-array tags), which are dropped.
DROP = {0x15, 0x0C, 0x0D, 0x19, 0x1A, 0x1B, 0x1C, 0x20, 0x21}
for i in range(phnum):
    p = phoff + i * phentsize
    if struct.unpack_from("<I", d, p)[0] == 2: doff, dsz = struct.unpack_from("<Q", d, p + 8)[0], struct.unpack_from("<Q", d, p + 0x20)[0]
ents = [struct.unpack_from("<QQ", d, doff + j) for j in range(0, dsz, 16)]
ents = ents[:[t for t, _ in ents].index(0)]
info = next(v for t, v in ents if t == 0x61000043); name = info & 0xFFFFFFFF
strtab = next(v for t, v in ents if t == 0x05); strsz = next(v for t, v in ents if t == 0x0A)
def file_off(va):  # the dynlib data sits in a flagless segment mapped after the others
    hits = [o + va - v for o, v, n in ((struct.unpack_from("<Q", d, phoff + i * phentsize + 8)[0],
            struct.unpack_from("<Q", d, phoff + i * phentsize + 16)[0], struct.unpack_from("<Q", d, phoff + i * phentsize + 32)[0])
            for i in range(phnum) if struct.unpack_from("<I", d, phoff + i * phentsize)[0] == 1) if v <= va < v + n]
    return hits[-1]
strs = bytes(d[file_off(strtab): file_off(strtab) + strsz]); modname = strs[name:strs.index(b"\0", name)]
soname = strs.find(modname + b".prx\0"); assert soname >= 0, "no <module>.prx string for DT_SONAME"
used = [v >> 48 for t, v in ents if t == 0x61000049]; lib_id = max(used + [0]) + 1  # library ids only
ents = [(t, v) for t, v in ents if not (t in DROP and v == 0)]
ents = [(t, (v & ~(0xFFFF << 32)) | (0x0101 << 32)) if t == 0x61000043 else (t, v) for t, v in ents]
ents += [(0x0E, soname), (0x61000047, (lib_id << 48) | (1 << 32) | name), (0x61000017, (lib_id << 48) | 1)]
assert (len(ents) + 1) * 16 <= dsz, "no room in .dynamic"
d[doff:doff + dsz] = b"".join(struct.pack("<QQ", t, v) for t, v in ents) + bytes(dsz - len(ents) * 16)
open(sys.argv[2], "wb").write(d)
