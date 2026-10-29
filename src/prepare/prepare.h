// First launch of a disc: identify its contents, run sfiii-native's generator on them (module audit,
// discovery and x86-64 lowering) and keep the result, the module set the generic build installs
// (sf3_x64_install_set). Everything it writes comes from the player's own disc and stays under
// the data folder; the app ships no disc-derived code. MIT, see LICENSE.
//
//   DATA/cache/<content_id>-abi<N>/   <MODULE>.sf3c caches and modules.sf3m (written last)
//   DATA/cache/images.tsv             image file, size, mtime, content ID: skips the rehash next time
//   DATA/cache/work-<pid>/            the probe's manifest and root .BIN files; removed afterwards
#pragma once
#include <functional>
#include <string>

namespace sf3::prepare {

struct Result {
    bool ok = false;
    bool cached = false;          // the set was already there
    std::string set_dir, content_id, profile, why;
};

// progress(0..1, what is happening). profiles_dir holds the shipped revision profiles (*.json).
Result prepare_disc(const std::string& image, const std::string& data_dir, const std::string& profiles_dir,
                    const std::function<void(double, const std::string&)>& progress = {});

// The set directory for a content ID (whether or not it exists yet).
std::string set_dir_for(const std::string& data_dir, const std::string& content_id);

}  // namespace sf3::prepare
