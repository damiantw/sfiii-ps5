# LZMA SDK 26.02 (7z decoder subset)

Public domain (see `lzma-sdk.txt`, and the header of each file). Only the C files the 7z
archive decoder needs are copied here, unchanged, from `C/` of the official SDK:

- source: https://www.7-zip.org/a/lzma2602.7z
- sha256: 2878c85f5f43a4a4e0952b1fd4e5fe097c1c143997a8047c7e1e788892aa9357

Used by `src/patch/sevenzip.cpp` for `.7z` patch add-ons (LZMA, LZMA2, PPMd, BCJ/BCJ2,
ARM/ARM64/PPC/SPARC/IA64 branch filters, Delta). Encrypted (AES) archives are not supported.
