// Building the patched disc the way the wizard's CDTools.GenerateISO does: patch planning,
// conflict checks, the ISO 9660 layout of its DiscUtils fork, Mode 1 EDC/ECC and the BIN/CUE.
// Port of sfiii-native tools/sf3patch/{patcher,conflicts,isobuild,edcecc,output}.py (MIT).
#pragma once
#include "disc/cdimage.h"
#include "package.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sf3::build {

using dotnet::U32;
using Time = std::optional<int64_t>;   // seconds since 1970, UTC wall clock; nullopt = blank

struct BuildError : std::runtime_error { using std::runtime_error::runtime_error; };
struct PatchError : std::runtime_error { using std::runtime_error::runtime_error; };

// ---------------------------------------------------------------- conflicts
struct Issue { std::string severity, message; };   // "error" or "warning"
std::vector<Issue> check_conflicts(const package::Mods& patches, const std::vector<std::string>& disc_names,
                                   const std::string& serial, const std::string& version);

// ---------------------------------------------------------------- isobuild
U32 normalize_file_name(const U32& name);
U32 short_file_name(const U32& name);
int member_compare(const U32& x, const U32& y);

struct BuildFile {
    std::string name;
    uint64_t size = 0;
    Time time;
    std::function<std::vector<uint8_t>()> read;
    bool is_cd() const { return name.rfind("CD", 0) == 0; }
};

struct Identifiers {
    std::string system = "SEGA SEGASATURN", volume, volume_set, publisher = "SEGA ENTERPRISES,LTD.",
                preparer = "CAMELOT,LTD.", application, copyright = "COPYRIGH.TXT", abstract = "ABSTRACT.TXT",
                bibliographic = "BIBLIOGR.TXT";
};

class Layout {
public:
    Layout(std::vector<uint8_t> ip, std::vector<BuildFile> files, Time root_time,
           const std::vector<uint64_t>& audio_bytes, Identifiers ids);
    uint64_t data_sectors = 0, volume_space = 0, root_size = 0;
    std::vector<uint8_t> system_area() const;     // sectors 0-19 plus the root directory
    // Calls sink with the track's user data in whole sectors (post-gap included).
    void user_data(const std::function<void(const uint8_t*, size_t)>& sink) const;
private:
    std::vector<uint8_t> ip_;
    std::vector<BuildFile> files_;
    Time root_time_;
    Identifiers ids_;
    std::vector<std::pair<U32, size_t>> members_;
    std::map<std::string, uint32_t> lba_;
    std::vector<size_t> order_;
    std::vector<uint8_t> root_;
    std::vector<uint8_t> root_directory() const;
};

// ---------------------------------------------------------------- edcecc
void mode1(const uint8_t* user, uint32_t lba, uint8_t out[2352]);

// ---------------------------------------------------------------- output
std::string cue_text(const std::string& bin_name, uint64_t data_sectors, size_t audio_tracks);
struct Written { uint64_t sectors; std::string track1_sha1, whole_sha1, cue_path; };
Written write_image(const std::string& bin_path, const Layout& layout,
                    const std::vector<std::function<void(const std::function<void(const uint8_t*, size_t)>&)>>& audio,
                    const std::function<void(uint64_t, uint64_t)>& progress = nullptr);

// ---------------------------------------------------------------- patcher
struct Options {
    Time now;                            // stamped on DFR-patched files; nullopt = current time
    int64_t host_utc_offset = 0;         // seconds east of UTC of the "wizard machine"
    std::string cdfs_time = "iso";
    std::string release_date = "20240825";
    Time root_time;
};

struct FilePlan { std::string name; std::vector<std::pair<std::string, std::string>> steps; uint64_t size = 0, original_size = 0; };

class Build {
public:
    Build(const disc::Disc& disc, const package::Package& pkg, const package::Mods& patches, Options opts);
    std::string serial, version;
    std::vector<FilePlan> plans;
    std::vector<BuildFile> files;
    Time root_time_local;
    Layout layout() const;
    std::vector<std::function<void(const std::function<void(const uint8_t*, size_t)>&)>> audio_data() const;
private:
    const disc::Disc& disc_;
    const package::Package& pkg_;
    Options opts_;
};

std::string stem(const std::string& name);   // pathlib's Path(name).stem

}  // namespace sf3::build
