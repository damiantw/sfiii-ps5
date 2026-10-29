// Recognising a disc: Redump track hashes first, the IP.BIN header as a fallback. Port of
// sfiii-native tools/sf3patch/identify.py (MIT), plus the slot/state classification the scanner uses.
#pragma once
#include "cdimage.h"
#include <string>
#include <vector>

namespace sf3::disc {

struct Known {
    const char* key;
    const char* title;
    const char* serial;
    const char* version;
    int redump;
    const char* track1_sha1;
    uint64_t track1_size;
    const char* track2_sha1;
    uint64_t track2_size;
    const char* track1_sha256;   // nullptr where not yet checked against a real dump
    const char* track2_sha256;
    const char* content_sha256;  // sfiii-native identity of the unpatched original
};

extern const Known KNOWN[];
extern const size_t KNOWN_COUNT;
const Known* by_track1(const std::string& sha1);

struct Identity {
    std::string serial, version, areas, title;   // UTF-8, from IP.BIN
    const Known* known = nullptr;
    bool hashed = false;
    std::string track1_sha1, track1_sha256, track2_sha1, track2_sha256;
    uint64_t track1_size = 0, track2_size = 0;
    bool has_track1 = false, has_track2 = false;
    std::vector<std::string> warnings;
    bool verified() const { return known && warnings.empty(); }
};

Identity identify(const Disc& d, bool hash_tracks = true);

// Which launcher slot a disc fills, and in what state.
enum class Slot { None, Scenario1, Scenario2, Scenario3, PremiumDisc };
enum class State { Unknown, Original, Translated };
const char* slot_name(Slot s);
const char* state_name(State s);
struct Classified { Slot slot = Slot::None; State state = State::Unknown; std::string detail; };
Classified classify(const Identity& id);

}  // namespace sf3::disc
