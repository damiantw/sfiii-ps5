// .7z archives for patch add-ons, through the LZMA SDK's 7z decoder (external/lzma-sdk, public
// domain). The tree matches what `7z x` would leave on disk: paths as stored, NTFS mtimes.
// MIT, see LICENSE.
#include "vfs.h"
#include "disc/cdimage.h"
#include <cstdio>
#include "dotnet.h"
#include <cstring>
#include <memory>

extern "C" {
#include "7z.h"
#include "7zAlloc.h"
#include "7zCrc.h"
}

namespace sf3::vfs {

namespace {
struct MemStream {
    ISeekInStream vt;      // first: the SDK passes &vt back to us
    const std::vector<uint8_t>* data;
    size_t pos = 0;
};
SRes mem_read(ISeekInStreamPtr p, void* buf, size_t* size) {
    auto* s = (MemStream*)p;
    size_t n = std::min(*size, s->data->size() - s->pos);
    std::memcpy(buf, s->data->data() + s->pos, n);
    s->pos += n;
    *size = n;
    return SZ_OK;
}
SRes mem_seek(ISeekInStreamPtr p, Int64* pos, ESzSeek origin) {
    auto* s = (MemStream*)p;
    Int64 base = origin == SZ_SEEK_SET ? 0 : origin == SZ_SEEK_CUR ? Int64(s->pos) : Int64(s->data->size());
    Int64 to = base + *pos;
    if (to < 0 || to > Int64(s->data->size())) return SZ_ERROR_READ;
    s->pos = size_t(to);
    *pos = to;
    return SZ_OK;
}
const ISzAlloc kAlloc = {SzAlloc, SzFree};

std::string utf16_to_utf8(const UInt16* w, size_t n) {
    dotnet::U32 u;
    for (size_t i = 0; i < n; i++) {
        uint32_t c = w[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < n && w[i + 1] >= 0xDC00 && w[i + 1] < 0xE000)
            c = 0x10000 + ((c - 0xD800) << 10) + (w[++i] - 0xDC00);
        u.push_back(char32_t(c));
    }
    return dotnet::utf8_encode(u);
}
const char* sres_text(SRes r) {
    switch (r) {
    case SZ_ERROR_UNSUPPORTED: return "uses a compression method or filter this build does not support";
    case SZ_ERROR_CRC: return "is damaged (CRC error)";
    case SZ_ERROR_MEM: return "needs more memory than is available";
    case SZ_ERROR_NO_ARCHIVE: return "is not a 7z archive";
    case SZ_ERROR_ARCHIVE: case SZ_ERROR_DATA: return "is damaged";
    default: return "could not be read";
    }
}
}  // namespace

std::unique_ptr<Node> from_7z(const std::string& path) {
    std::string label = disc::path_name(path);
    auto bytes = std::make_shared<std::vector<uint8_t>>();
    if (FILE* fp = std::fopen(path.c_str(), "rb")) {
        uint8_t buf[1 << 16];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, fp)) > 0) bytes->insert(bytes->end(), buf, buf + n);
        std::fclose(fp);
    } else throw VfsError("cannot open " + path);
    static bool crc_ready = (CrcGenerateTable(), true);
    (void)crc_ready;

    MemStream ms{{mem_read, mem_seek}, bytes.get()};
    CLookToRead2 look;
    LookToRead2_CreateVTable(&look, False);
    std::vector<Byte> lookbuf(1 << 16);
    look.buf = lookbuf.data();
    look.bufSize = lookbuf.size();
    look.realStream = &ms.vt;
    LookToRead2_INIT(&look);

    CSzArEx db;
    SzArEx_Init(&db);
    struct Guard { CSzArEx* db; Byte** out; ~Guard() { if (*out) ISzAlloc_Free(&kAlloc, *out); SzArEx_Free(db, &kAlloc); } };
    Byte* out = nullptr;
    Guard guard{&db, &out};
    if (SRes r = SzArEx_Open(&db, &look.vt, &kAlloc, &kAlloc); r != SZ_OK) throw VfsError(label + " " + sres_text(r));

    auto root = std::make_unique<Node>();
    std::string stem = label;
    size_t dot = stem.rfind('.');
    if (dot != std::string::npos && dot > 0) stem = stem.substr(0, dot);
    root->name = stem;
    root->dir = true;

    UInt32 block = 0xFFFFFFFF;
    size_t out_size = 0;
    std::vector<UInt16> name;
    for (UInt32 i = 0; i < db.NumFiles; i++) {
        size_t len = SzArEx_GetFileNameUtf16(&db, i, nullptr);   // with the terminating 0
        name.resize(len);
        SzArEx_GetFileNameUtf16(&db, i, name.data());
        std::string nm = utf16_to_utf8(name.data(), len ? len - 1 : 0);
        std::vector<std::string> parts;
        size_t st = 0;
        for (;;) {
            size_t sl = nm.find_first_of("/\\", st);   // 7-Zip stores '/'; a Windows-made name may hold '\'
            std::string part = nm.substr(st, sl == std::string::npos ? std::string::npos : sl - st);
            if (part == "..") throw VfsError(label + ": unsafe path '" + nm + "' in archive");
            if (!part.empty() && part != ".") parts.push_back(part);
            if (sl == std::string::npos) break;
            st = sl + 1;
        }
        if (!nm.empty() && (nm[0] == '/' || nm[0] == '\\')) throw VfsError(label + ": unsafe path '" + nm + "' in archive");
        if (parts.empty()) continue;
        if (SzArEx_IsDir(&db, i)) { ensure_path(root.get(), parts); continue; }
        Node* dir = ensure_path(root.get(), {parts.begin(), parts.end() - 1});
        Node* f = dir->child_exact(parts.back());
        if (f && f->dir) throw VfsError(f->path() + " is a file and a folder in the archive");
        if (!f) {
            auto n = std::make_unique<Node>();
            n->name = parts.back();
            n->parent = dir;
            dir->kids.push_back(std::move(n));
            f = dir->kids.back().get();
        }
        size_t offset = 0, got = 0;
        if (SRes r = SzArEx_Extract(&db, &look.vt, i, &block, &out, &out_size, &offset, &got, &kAlloc, &kAlloc); r != SZ_OK)
            throw VfsError(label + ": " + nm + " " + sres_text(r));
        f->mem = std::make_shared<const std::vector<uint8_t>>(out + offset, out + offset + got);
        f->size = got;
        if (SzBitWithVals_Check(&db.MTime, i)) {
            const CNtfsFileTime& t = db.MTime.Vals[i];
            uint64_t ft = (uint64_t(t.High) << 32) | t.Low;
            f->mtime = {int64_t(ft / 10000000ULL) - 11644473600LL, int32_t(ft % 10000000ULL * 100)};
        }
    }
    return root;
}

}  // namespace sf3::vfs
