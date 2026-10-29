// An in-memory file tree for patch packages: a folder on disk, or a zip unpacked the way the
// Linux sf3patch tool unpacks it (Python's zipfile for add-on .zip files, 7-Zip for
// SF3Patch.exe), without writing anything. Each file keeps the modification time the
// unpacked file would have, because the patcher records it in the disc image.
// MIT, see LICENSE.
#pragma once
#include "dotnet.h"
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace sf3::vfs {

using dotnet::U32;

struct VfsError : std::runtime_error { using std::runtime_error::runtime_error; };

struct FTime { int64_t sec = 0; int32_t nsec = 0; };

class ZipArchive;

struct Node {
    std::string name;                 // UTF-8
    bool dir = false;
    Node* parent = nullptr;
    std::vector<std::unique_ptr<Node>> kids;   // creation order
    // files
    std::string disk_path;
    std::shared_ptr<ZipArchive> zip;
    size_t entry = 0;
    uint64_t size = 0;
    FTime mtime;

    std::vector<uint8_t> read() const;
    std::string path() const;                       // "/"-joined from the tree root
    std::string relative_to(const Node* ancestor) const;
    Node* child_exact(const std::string& n) const;

    // CaseDir: NTFS-ordered children; get() looks up each part like Windows (first of a
    // case-fold group in NTFS order wins); files() is the top-level files of a subfolder.
    const std::vector<Node*>& sorted() const;
    const Node* get(std::initializer_list<std::string> parts) const;
    const Node* get(const std::vector<std::string>& parts) const;
    std::optional<std::vector<const Node*>> files(const std::vector<std::string>& parts) const;

private:
    mutable bool cached_ = false;
    mutable std::vector<Node*> sorted_;
    mutable std::map<U32, Node*> upper_;
};

// Host time-zone rule for zip DOS times (Python zipfile path): nullopt = this machine's zone.
using Offset = std::optional<int64_t>;   // seconds east of UTC

std::unique_ptr<Node> from_disk(const std::string& path);
// .zip unpacked like Python's zipfile (DOS times), anything else like 7-Zip (NTFS times).
std::unique_ptr<Node> from_zip(const std::string& path, bool python_semantics, Offset host_utc_offset);

// datetime.fromtimestamp(st_mtime, utc).replace(microsecond=0) of a (sec, nsec) mtime.
int64_t python_mtime_seconds(FTime t);

// Seconds since 1970 of a naive local time in this machine's zone (datetime.timestamp()).
int64_t local_to_epoch(int y, int mo, int d, int h, int mi, int s);
int64_t civil_to_epoch(int y, int mo, int d, int h, int mi, int s);
void epoch_to_civil(int64_t t, int& y, int& mo, int& d, int& h, int& mi, int& s);

}  // namespace sf3::vfs
