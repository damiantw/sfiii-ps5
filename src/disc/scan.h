// Finding the user's discs: every .cue and .chd under a folder (default /data/shiningforceiii),
// identified by hash, grouped into the four launcher slots. Any combination of discs may be present.
#pragma once
#include "identify.h"
#include <functional>
#include <string>
#include <vector>

namespace sf3::disc {

struct Candidate {
    std::string path;
    bool ok = false;
    std::string error;          // why it could not be read
    Identity id;
    Classified cls;
};

struct ScanResult {
    std::vector<Candidate> all;
    // The chosen disc per slot (index into all, -1 = none): a translated image first, else a
    // verified original, else any readable one for that slot.
    int slot[5] = {-1, -1, -1, -1, -1};
};

struct ScanOptions {
    int max_depth = 3;
    bool hash = true;
    std::function<void(const std::string& path)> progress;
    // Results file (empty: none). An image whose folder is unchanged (every file's size and
    // mtime) reuses its cached identity instead of being read and hashed again.
    std::string cache_path;
    bool refresh = false;       // ignore the cache (the user's Rescan), then rewrite it
    int* reused = nullptr;      // out: how many came from the cache
};

std::vector<std::string> find_images(const std::string& root, int max_depth = 3);
ScanResult scan(const std::string& root, const ScanOptions& opt = {});

}  // namespace sf3::disc
