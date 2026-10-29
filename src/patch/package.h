// Patch packages (the wizard folder or SF3Patch.exe) and add-on mods, plus which patches the
// wizard lists and ticks for a disc. Port of sfiii-native tools/sf3patch/package.py and
// selection.py (MIT).
#pragma once
#include "vfs.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sf3::package {

using dotnet::U32;

struct PackageError : std::runtime_error { using std::runtime_error::runtime_error; };

struct PatchDir {
    const vfs::Node* dir = nullptr;
    std::string source;                 // "Patches", "Custom Mods" or "mod:<archive stem>"
    std::string meta_error;             // empty when the metadata was read
    U32 category;
    std::optional<U32> text, title, desc;
    bool enabled = false;
    int32_t priority = 9999;

    std::string name() const { return dir->name; }
    std::string label() const { return source + "/" + name(); }
    bool applies_to(const std::string& serial, const std::string& version) const;
};

struct Package {
    std::vector<std::unique_ptr<vfs::Node>> trees;   // owns every tree the patches point into
    const vfs::Node* root = nullptr;
    std::string version;
    std::vector<std::shared_ptr<PatchDir>> patches;
    std::vector<std::string> drive_order(const std::string& serial) const;
};

using Mods = std::vector<std::shared_ptr<PatchDir>>;

// path: SF3Patch.exe or the folder it unpacks to.
std::unique_ptr<Package> load(const std::string& path, vfs::Offset host_utc_offset);
// path: a mod .zip or folder; the tree is appended to `owner.trees`.
Mods load_mod(Package& owner, const std::string& path, vfs::Offset host_utc_offset);
// Every .zip in an add-on folder (sorted by lower-case name, minus the wizard scaling fix).
std::vector<std::string> addon_archives(const std::string& dir);

// selection.py
struct Listed { Mods ordered; std::vector<std::string> errors; };
Listed listed(const Mods& patches, const std::string& serial, const std::string& version);
Mods choose(const Mods& ordered, const std::vector<std::string>& enable, const std::vector<std::string>& disable,
            const std::optional<std::vector<std::string>>& only);
struct Layered { Mods patches; std::vector<std::string> notes; };
Layered layer(const Mods& base, const Mods& mods, bool supersede = true);

}  // namespace sf3::package
