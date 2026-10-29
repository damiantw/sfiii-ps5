#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Wrap a PS5 ELF (eboot or PRX) in an unsigned "fake SELF" container that a jailbroken console
accepts. Written from the container's documented layout (psdevwiki "SELF File Format") and from
inspecting produced bytes only; no third-party code.

Layout: 0x20 header | one 0x20 entry per (hash table, segment) pair | ELF header + program
headers | 0x40 extended info (authority id, program type, versions, sha256 of the ELF as the loader
rebuilds it) | 0x30 block | zeroed metadata | entry data, 16-byte aligned | the PT_SCE_VERSION
segment appended right after the last entry (the header's file size is that end rounded to 16). Hash tables are 0x20 per 16 KiB block, all zero.
Output is a pure function of the input bytes (deterministic).

usage: make-fself.py [--module] <in.elf> <out>
  --module  PS5 module magic (0xEEF51454) and metadata size, as the console's own modules use;
            without it the PS4-style magic (0x1D3D154F) that our eboot already runs with."""
import hashlib, struct, sys

args = sys.argv[1:]; module = "--module" in args; args = [a for a in args if a != "--module"]
e = bytearray(open(args[0], "rb").read())
assert e[:4] == b"\x7fELF"
phoff, = struct.unpack_from("<Q", e, 0x20); phnum, = struct.unpack_from("<H", e, 0x38)
ph = [struct.unpack_from("<IIQQQQQQ", e, phoff + i * 56) for i in range(phnum)]  # type flags off va pa fsz msz align
segs = [i for i, p in enumerate(ph) if p[0] in (1, 0x6FFFFF00) and p[5]]
version = next((p for p in ph if p[0] == 0x6FFFFF01), None)

# what the loader reconstructs: ELF header + program headers + the listed segments, zeros elsewhere
img = bytearray(len(e)); img[:phoff + phnum * 56] = e[:phoff + phnum * 56]
for i in segs + [i for i, p in enumerate(ph) if p[0] == 0x6FFFFF01]: o, n = ph[i][2], ph[i][5]; img[o:o + n] = e[o:o + n]
digest = hashlib.sha256(img).digest()

n = 2 * len(segs); align = lambda x: (x + 15) & ~15
hdr_elf = 0x20 + n * 0x20; ext = align(hdr_elf + phoff + phnum * 56)
hs = ext + 0x40 + 0x30; ms = 0x610 if module else 0x510
entries, blobs, off = [], [], hs + ms
for k, i in enumerate(segs):
    data = bytes(e[ph[i][2]:ph[i][2] + ph[i][5]]); table = bytes(0x20 * -(-len(data) // 0x4000))
    for props, blob in ((((2 * k + 1) << 20) | 0x10004, table), ((i << 20) | 0x2804, data)):
        off = align(off); entries.append(struct.pack("<QQQQ", props, off, len(blob), len(blob)))
        blobs.append((off, blob)); off += len(blob)
filesz = align(off)  # counted size is rounded; the version segment follows the unrounded end
out = bytearray(off)
struct.pack_into("<IBBBBIHHQHHI", out, 0, 0xEEF51454 if module else 0x1D3D154F, 0, 1, 1, 0x12, 0x101,
                 hs, ms, filesz, n, 0x22, 0)
out[0x20:hdr_elf] = b"".join(entries)
out[hdr_elf:hdr_elf + phoff + phnum * 56] = e[:phoff + phnum * 56]
struct.pack_into("<QQQQ", out, ext, 0x3100000000000002, 1, 0, 0); out[ext + 0x20:ext + 0x40] = digest
out[ext + 0x40] = 3
out[hs + 0x3F2] = 1
for o, b in blobs: out[o:o + len(b)] = b
if version: out += e[version[2]:version[2] + version[5]]
open(args[1], "wb").write(out)
