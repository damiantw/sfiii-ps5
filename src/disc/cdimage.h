// The source disc: CUE/BIN (one file per track or one for all), CHD (libchdr), and the ISO 9660
// file system on the data track. A C++ port of sfiii-native tools/sf3patch/cdimage.py (MIT).
// A CHD is presented exactly as `chdman extractcd` would write it (one BIN, all tracks), so the
// rest of the code sees the same tracks, offsets and bytes either way.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace sf3::disc {

constexpr int RAW = 2352;
constexpr int USER = 2048;

struct DiscError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Random-access bytes: a file, or the virtual BIN of a CHD.
class ByteSource {
public:
    virtual ~ByteSource() = default;
    virtual uint64_t size() const = 0;
    // Reads exactly n bytes at off, or fewer at the end; returns the count.
    virtual size_t read(uint64_t off, void* buf, size_t n) = 0;
    virtual std::string name() const = 0;
};

std::shared_ptr<ByteSource> open_file(const std::string& path);

struct Track {
    int number = 0;
    std::string mode;                       // "MODE1/2352", "MODE1/2048", "AUDIO", ...
    std::shared_ptr<ByteSource> src;
    std::string path;                       // the file it lives in (for CHD, the .chd)
    std::map<int, int> indexes;             // index number -> frame offset in the file
    int pregap = 0;                         // PREGAP frames (not stored)
    uint64_t start = 0, end = 0;            // byte range in the file

    int sector_size() const;
    bool is_audio() const { return mode == "AUDIO"; }
    uint64_t index1() const { return uint64_t(indexes.at(1)) * sector_size(); }
    struct Hashes { std::string sha1, sha256; uint64_t size; };
    Hashes hashes() const;                  // as Redump stores the track: the PREGAP counts as zeros
    std::vector<uint8_t> audio_from_index1() const;
};

// Parses CUE text. `dir` resolves FILE names (case-insensitively when the exact name is missing).
// `open` maps a resolved path to its bytes (the default opens the file).
std::vector<Track> parse_cue_text(const std::string& text, const std::string& dir, const std::string& what,
                                  std::shared_ptr<ByteSource> (*open)(const std::string&) = open_file);
std::vector<Track> parse_cue(const std::string& cue_path);

// A CHD as chdman extractcd would write it: the CUE text and the one BIN.
struct ChdImage {
    std::string cue_text;
    std::shared_ptr<ByteSource> bin;
};
ChdImage open_chd(const std::string& path);

struct Record {
    std::string name;     // UTF-8, without ";1"
    uint32_t lba = 0, size = 0;
    uint8_t flags = 0;
    uint8_t time[7] = {};
    int xa_attr = -1;     // -1 when there is no XA field
    bool is_dir = false;
    bool is_cdda() const { return xa_attr >= 0 && (xa_attr & 0x4000); }
};

class Disc {
public:
    explicit Disc(const std::string& path);   // .cue or .chd
    std::string cue;                          // the CUE path (or the CHD's)
    std::vector<Track> tracks;
    Track& data() { return tracks[0]; }
    const Track& data() const { return tracks[0]; }
    std::vector<uint8_t> ip_raw, pvd;
    std::string volume_id;                    // 32 characters, UTF-8
    Record root;
    std::vector<Record> files;

    std::vector<uint8_t> read_sectors(uint32_t lba, uint32_t count) const;
    std::vector<uint8_t> read_file(const Record& r) const;
    std::vector<const Track*> audio_tracks() const;
private:
    void init();
    static Record record(const uint8_t* buf, size_t len, size_t off);
    std::vector<Record> list_root() const;
};

// The time Windows' CDFS reports for a record, as seconds since 1970 UTC; false for none.
// mode "iso" applies the GMT offset, "local" ignores it.
bool iso_time_to_utc(const uint8_t t[7], const std::string& mode, int64_t& out);

// Helpers shared with the patcher.
std::string ascii_replace(const uint8_t* p, size_t n);   // bytes >= 0x80 become U+FFFD
std::string to_lower_ascii(std::string s);
std::string to_upper_ascii(std::string s);
std::string path_join(const std::string& dir, const std::string& name);
std::string path_dir(const std::string& p);
std::string path_name(const std::string& p);
std::string path_suffix_lower(const std::string& p);
std::vector<std::string> list_dir(const std::string& dir);   // names only, unsorted
bool is_dir(const std::string& p);
bool file_exists(const std::string& p);

}  // namespace sf3::disc
