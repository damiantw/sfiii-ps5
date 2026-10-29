// MIT, see LICENSE. Port of sfiii-native tools/sf3patch/identify.py.
#include "identify.h"
#include "../patch/ipbin.h"
#include <cstring>

namespace sf3::disc {

#define STD_AUDIO "021edf3c2293243d6b6f52028fed44f569946249", 1818096
#define STD_AUDIO_SHA256 "f5b6d03d99bb85f42066a4691df5b43cc4e64f792e434e38d68ebf15f10ea047"

// Redump.org dumps (the audio track includes its 150-sector pregap). Hashes only: no disc bytes.
const Known KNOWN[] = {
    {"sc1-usa", "Scenario 1 (USA)", "MK-81383", "V1.001", 20315, "9b1033304091b3dea7c2fd6d88914a1c7d0c6727", 245934528,
     "b372a4c92930d2b93213460a443a2220e74a973a", 1820448,
     "089c77470dec14f1dbffaad1ab50d584f06df5e28b475fc712b8e67e5890f951",
     "6bed8e1f830b32833676b75bc3cec9c831b44e8f0789a37eb4c1a0efb9d25e1e",
     "54aa1d2178407d7273a7fb54a7aef4b36c8364cc3b203acb997613e95eea81a9"},
    {"sc1-eu", "Scenario 1 (Europe)", "MK-81383", "V1.005", 3494, "9d8f601c9938acb39c7a2a5181dbc77e70117a6f", 245017248,
     STD_AUDIO, nullptr, nullptr, nullptr},
    {"sc1-jp-1.006", "Scenario 1 (Japan) V1.006", "GS-9175", "V1.006", 17380, "6ae4c9cc601ee02a432f871373e25cad3a65b8b8",
     247987824, STD_AUDIO, nullptr, nullptr, nullptr},
    {"sc1-jp-2.005", "Scenario 1 (Japan) V2.005", "GS-9175", "V2.005", 3509, "4169bfb491046eac26688054c314233656d08c6b",
     247717344, STD_AUDIO, nullptr, nullptr, nullptr},
    {"sc2-1.003", "Scenario 2 (Japan) V1.003", "GS-9188", "V1.003", 20117, "82c157dbad2f50bd8d1fd9b417233221ab9fd8c8",
     307023024, STD_AUDIO, "81dfba75da4687d272f46ad4148a7de39b6a564a604dfa47e45ac0c0cef6ad1b", STD_AUDIO_SHA256,
     "a855681edd77c5dd816033dc73dfc891c7e09048ecb9e45c85375dfc1543cc22"},
    {"sc2-2.000", "Scenario 2 (Japan) V2.000", "GS-9188", "V2.000", 3508, "e21f7ad51af3ef57af9f9f7db58afb2dd642e3ad",
     307093584, STD_AUDIO, "2b385904b6f177885d2c0264555a79f5cb1333c731666de5ac1687b6338921e7", STD_AUDIO_SHA256,
     "d5def91943d7212174583fd0ca0f5ac0d031915c610aa4a2e4fe447fd2b16e38"},
    {"sc3", "Scenario 3 (Japan)", "GS-9203", "V1.001", 3507, "6a2646030ff74a98b666726b78003e6505d24285", 508594128,
     STD_AUDIO, "ee31a9be6552a075266d95c92c881a5db3cb4958ceaa3cea793f43c9cd8b4a1f", STD_AUDIO_SHA256,
     "598c40eab6a453e2a1fb8f4306b7d4d87f7ff8effea7f5dea43877fc6c886356"},
    {"pd", "Premium Disc (Japan)", "6106979", "V1.000", 28747, "1b5de342984618965d25494f329cb317452b7a6e", 555869328,
     STD_AUDIO, "f024e086293812f0dfa0b46626a9235ef0d54397d1b4a42d2f56036833da358a", STD_AUDIO_SHA256,
     "9a422bc58ae0dd9b00faad42c731e099c7a39a668fc4ebc43f4d08f0369bd5bb"},
};
const size_t KNOWN_COUNT = sizeof KNOWN / sizeof KNOWN[0];

const Known* by_track1(const std::string& sha1) {
    for (auto& k : KNOWN) if (sha1 == k.track1_sha1) return &k;
    return nullptr;
}

Identity identify(const Disc& d, bool hash_tracks) {
    auto h = ipbin::parse(d.ip_raw);
    Identity r;
    r.serial = dotnet::utf8_encode(h.product);
    r.version = dotnet::utf8_encode(h.version);
    r.areas = dotnet::utf8_encode(h.areas);
    r.title = dotnet::utf8_encode(h.title);
    if (!hash_tracks) return r;
    r.hashed = true;
    if (d.data().sector_size() == RAW) {
        auto x = d.data().hashes();
        r.has_track1 = true;
        r.track1_sha1 = x.sha1; r.track1_sha256 = x.sha256; r.track1_size = x.size;
    }
    auto audio = d.audio_tracks();
    if (!audio.empty()) {
        auto x = audio[0]->hashes();
        r.has_track2 = true;
        r.track2_sha1 = x.sha1; r.track2_sha256 = x.sha256; r.track2_size = x.size;
    }
    r.known = r.has_track1 ? by_track1(r.track1_sha1) : nullptr;
    const Known* k = r.known;
    if (!k) {
        r.warnings.push_back("the data track does not match a Redump dump of a Shining Force III disc "
                             "(a different dump, a modified image, or a 2048-byte track)");
        return r;
    }
    if (r.serial != k->serial || r.version != k->version)
        r.warnings.push_back("IP.BIN says " + r.serial + " " + r.version + " but the hash is " + k->serial + " " + k->version);
    if (r.track1_size != k->track1_size) r.warnings.push_back("data track size differs from Redump");
    if (k->track1_sha256 && r.track1_sha256 != k->track1_sha256)
        r.warnings.push_back("data track SHA-1 matches Redump but SHA-256 does not (" + r.track1_sha256 + ")");
    if (!r.has_track2 || r.track2_sha1 != k->track2_sha1)
        r.warnings.push_back("the audio track differs from Redump (" + (r.has_track2 ? r.track2_sha1 : std::string("missing")) +
                             "); the output's audio will differ from a wizard run on the original disc");
    else if (k->track2_sha256 && r.track2_sha256 != k->track2_sha256)
        r.warnings.push_back("audio track SHA-1 matches Redump but SHA-256 does not (" + r.track2_sha256 + ")");
    return r;
}

const char* slot_name(Slot s) {
    switch (s) {
    case Slot::Scenario1: return "Scenario 1";
    case Slot::Scenario2: return "Scenario 2";
    case Slot::Scenario3: return "Scenario 3";
    case Slot::PremiumDisc: return "Premium Disc";
    default: return "none";
    }
}
const char* state_name(State s) {
    switch (s) {
    case State::Original: return "original";
    case State::Translated: return "translated";
    default: return "unknown";
    }
}

Classified classify(const Identity& id) {
    Classified c;
    if (id.known) {
        std::string k = id.known->key;
        c.slot = k.rfind("sc1", 0) == 0 ? Slot::Scenario1 : k.rfind("sc2", 0) == 0 ? Slot::Scenario2
               : k == "sc3" ? Slot::Scenario3 : Slot::PremiumDisc;
        c.state = State::Original;
        c.detail = std::string(id.known->title) + " (Redump " + std::to_string(id.known->redump) + ")";
        return c;
    }
    // Not a Redump original: name the game from IP.BIN's title (the first 32 characters survive the wizard).
    std::string t = id.title.substr(0, 32);
    if (t.rfind("SHINING FORCE 3 SCENARIO 1", 0) == 0) c.slot = Slot::Scenario1;
    else if (t.rfind("SHINING FORCE 3 SCENARIO 2", 0) == 0) c.slot = Slot::Scenario2;
    else if (t.rfind("SHINING FORCE 3 SCENARIO 3", 0) == 0) c.slot = Slot::Scenario3;
    else if (t.rfind("SHINING FORCE 3 PREMIUM DISC", 0) == 0) c.slot = Slot::PremiumDisc;
    if (c.slot == Slot::None) { c.detail = "not a Shining Force III disc (" + id.serial + ")"; return c; }
    if (id.serial == "SF3TRANS") {
        c.state = State::Translated;
        c.detail = "translation patch " + id.version;
    } else {
        c.detail = id.serial + " " + id.version + ", not a known Redump dump";
    }
    return c;
}

}  // namespace sf3::disc
