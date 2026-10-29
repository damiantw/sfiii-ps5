// sf3disc: identify one image, or scan a folder for the four discs. A Linux test front end for
// src/disc (the console calls the same code from the menu). MIT, see LICENSE.
#include "../disc/manifest.h"
#include "../disc/scan.h"
#include <cstdio>
#include <cstring>

using namespace sf3::disc;

static void print_identity(const Disc& d, const Identity& r) {
    printf("serial   %s\nversion  %s\nareas    %s\ntitle    %s\n", r.serial.c_str(), r.version.c_str(), r.areas.c_str(), r.title.c_str());
    std::string v = d.volume_id;
    while (!v.empty() && v.back() == ' ') v.pop_back();
    printf("volume   %s\n", v.c_str());
    if (r.has_track1) printf("track 1  %s (%llu bytes)\n         sha256 %s\n", r.track1_sha1.c_str(), (unsigned long long)r.track1_size, r.track1_sha256.c_str());
    if (r.has_track2) printf("track 2  %s (%llu bytes)\n         sha256 %s\n", r.track2_sha1.c_str(), (unsigned long long)r.track2_size, r.track2_sha256.c_str());
    if (r.known) printf("redump   %s, http://redump.org/disc/%d/\n", r.known->key, r.known->redump);
    for (auto& w : r.warnings) printf("warning: %s\n", w.c_str());
}

int main(int argc, char** argv) {
    if (argc < 3 || (strcmp(argv[1], "identify") && strcmp(argv[1], "scan") && strcmp(argv[1], "files") && strcmp(argv[1], "probe"))) {
        fprintf(stderr, "usage: sf3disc identify IMAGE [--no-hash] | files IMAGE | scan DIR [--no-hash] | probe IMAGE [EXTRACT_DIR [PROFILES_DIR]]\n");
        return 2;
    }
    bool hash = !(argc > 3 && !strcmp(argv[3], "--no-hash"));
    try {
        if (!strcmp(argv[1], "identify")) {
            Disc d(argv[2]);
            Identity r = identify(d, hash);
            print_identity(d, r);
            return (r.known || !hash) ? 0 : 1;
        }
        if (!strcmp(argv[1], "probe")) {   // the manifest (stdout) and content ID (stderr), as tools/probe.py
            Disc d(argv[2]);
            Probe p = probe(d, argc > 3 ? argv[3] : "");
            printf("%s\n", p.manifest_json.c_str());
            fprintf(stderr, "content %s\n", p.content_id.c_str());
            if (argc > 4) fprintf(stderr, "profile %s\n", match_profile(argv[4], p.content_id).c_str());
            return 0;
        }
        if (!strcmp(argv[1], "files")) {
            Disc d(argv[2]);
            for (auto& t : d.tracks)
                printf("track %d %s start %llu end %llu index1 %llu pregap %d\n", t.number, t.mode.c_str(),
                       (unsigned long long)t.start, (unsigned long long)t.end, (unsigned long long)t.index1(), t.pregap);
            for (auto& f : d.files) {
                printf("%-14s lba %7u size %10u flags %02x xa %d time", f.name.c_str(), f.lba, f.size, f.flags, f.xa_attr);
                for (int i = 0; i < 7; i++) printf(" %d", f.time[i]);
                printf("\n");
            }
            return 0;
        }
        ScanOptions o;
        o.hash = hash;
        o.progress = [](const std::string& p) { fprintf(stderr, "reading %s\n", p.c_str()); };
        ScanResult r = scan(argv[2], o);
        for (auto& c : r.all) {
            if (!c.ok) { printf("%s: unreadable: %s\n", c.path.c_str(), c.error.c_str()); continue; }
            printf("%s: %s, %s: %s%s\n", c.path.c_str(), slot_name(c.cls.slot), state_name(c.cls.state), c.cls.detail.c_str(),
                   c.id.warnings.empty() ? "" : " (with warnings)");
            for (auto& w : c.id.warnings) printf("  warning: %s\n", w.c_str());
        }
        for (int s = 1; s <= 4; s++)
            printf("slot %-12s %s\n", slot_name(Slot(s)), r.slot[s] < 0 ? "(missing)" : r.all[size_t(r.slot[s])].path.c_str());
        return 0;
    } catch (const std::exception& e) {
        fprintf(stderr, "sf3disc: %s\n", e.what());
        return 2;
    }
}
