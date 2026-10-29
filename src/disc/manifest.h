// What identifies a disc's contents, whatever the image's layout: a C++ port of saturn-recomp's
// disc.manifest() and sfiii-native tools/disc_profile.py content_id() (MIT). The console runs this,
// then sfiii-native's generator (module audit and x64 lowering) on the extracted files. MIT, see LICENSE.
#pragma once
#include "cdimage.h"
#include <functional>
#include <string>

namespace sf3::disc {

struct Probe {
    std::string manifest_json;   // canonical: sorted keys, no spaces (what the content ID hashes)
    std::string content_id;      // sha256 of manifest_json
};

// Reads every file and audio track once. When extract_dir is set, writes each root *.BIN file there
// (the generator's inputs; nothing else is needed). progress(done, total) counts bytes.
Probe probe(const Disc& d, const std::string& extract_dir = {},
            const std::function<void(uint64_t, uint64_t)>& progress = {});

// The profile in profiles_dir/*.json whose content_sha256 is content_id; empty when none.
std::string match_profile(const std::string& profiles_dir, const std::string& content_id);

}  // namespace sf3::disc
