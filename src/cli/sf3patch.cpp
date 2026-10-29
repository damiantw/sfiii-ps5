// sf3patch: the translation patcher as a command-line tool, for testing the console code on a
// PC against sfiii-native's Python tools/sf3patch (same options, same output bytes). MIT.
#include "disc/identify.h"
#include "patch/build.h"
#include "patch/netjson.h"
#include "patch/dfr.h"
#include "patch/fetch.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <regex>
#include <sys/stat.h>

using namespace sf3;

static void usage() {
    fprintf(stderr,
            "usage: sf3patch fetch --dest DIR [--wizard-only|--addons-only] | fetch --list\n"
            "       sf3patch list  DISC --package P [opts]\n"
            "       sf3patch patch DISC --package P -o OUT.bin [opts]\n"
            "opts: --addons DIR  --mod ZIP|DIR  --no-supersede  --enable N  --disable N  --only N...\n"
            "      --host-utc-offset +HH:MM  --allow-conflicts  --any-disc  --no-hash  --cdfs-time iso|local\n"
            "      --now ISO8601  --release-date YYYYMMDD  --reproducible  --in-place\n");
}

static int64_t parse_offset(const std::string& t) {
    std::smatch m;
    static const std::regex re(R"(\s*([+-])?(\d{1,2})(?::?(\d{2}))?\s*)");
    if (!std::regex_match(t, m, re)) throw std::runtime_error("use +HH:MM or -HH:MM");
    int64_t v = std::stoi(m[2]) * 3600 + (m[3].matched ? std::stoi(m[3]) * 60 : 0);
    return m[1] == "-" ? -v : v;
}
static int64_t parse_now(const std::string& t) {
    // YYYY-MM-DD[T ]HH:MM[:SS[.f]][Z|+HH:MM]
    std::smatch m;
    static const std::regex re(R"((\d{4})-(\d{2})-(\d{2})(?:[T ](\d{2}):(\d{2})(?::(\d{2})(?:\.\d+)?)?)?(Z|[+-]\d{2}:?\d{2})?)");
    if (!std::regex_match(t, m, re)) throw std::runtime_error("bad --now time");
    auto g = [&](int i) { return m[i].matched ? std::stoi(m[i]) : 0; };
    int64_t v = vfs::civil_to_epoch(g(1), g(2), g(3), g(4), g(5), g(6));
    if (m[7].matched && m[7] != "Z") v -= parse_offset(m[7]);
    return v;
}
static int64_t local_offset_now() {
    time_t t = time(nullptr);
    struct tm lt;
    localtime_r(&t, &lt);
    return lt.tm_gmtoff;
}

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "fetch") {
        std::string dest;
        bool w = true, ad = true, list_only = false;
        for (int i = 2; i < argc; i++) {
            std::string a = argv[i];
            if (a == "--dest" && i + 1 < argc) dest = argv[++i];
            else if (a == "--wizard-only") ad = false;
            else if (a == "--addons-only") w = false;
            else if (a == "--list") list_only = true;
            else { usage(); return 2; }
        }
        try {
            auto t = fetch::curl_transport();
            if (list_only) {
                for (auto& [n, u] : fetch::addon_urls(*t)) printf("%s %s\n", n.c_str(), u.c_str());
                return 0;
            }
            if (dest.empty()) { usage(); return 2; }
            fetch::fetch(*t, dest, w, ad, [](const std::string& m) { printf("%s\n", m.c_str()); });
            printf("saved under %s; do not redistribute these files\n", dest.c_str());
            return 0;
        } catch (const std::exception& e) {
            fprintf(stderr, "sf3patch: %s\n", e.what());
            return 2;
        }
    }
    if (argc < 3) { usage(); return 2; }
    std::string cmd = argv[1], disc_path = argv[2], pkg_path, out, addons, cdfs = "iso", release = "20240825";
    std::vector<std::string> mods, enable, disable;
    std::optional<std::vector<std::string>> only;
    std::optional<int64_t> host_off, now;
    bool no_supersede = false, allow_conflicts = false, any_disc = false, no_hash = false, reproducible = false, in_place = false;
    for (int i = 3; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { if (i + 1 >= argc) { usage(); exit(2); } return argv[++i]; };
        if (a == "--package") pkg_path = next();
        else if (a == "-o" || a == "--output") out = next();
        else if (a == "--addons") addons = next();
        else if (a == "--mod") mods.push_back(next());
        else if (a == "--no-supersede") no_supersede = true;
        else if (a == "--enable") enable.push_back(next());
        else if (a == "--disable") disable.push_back(next());
        else if (a == "--only") { only.emplace(); while (i + 1 < argc && argv[i + 1][0] != '-') only->push_back(argv[++i]); }
        else if (a == "--host-utc-offset") host_off = parse_offset(next());
        else if (a == "--allow-conflicts") allow_conflicts = true;
        else if (a == "--any-disc") any_disc = true;
        else if (a == "--no-hash") no_hash = true;
        else if (a == "--cdfs-time") cdfs = next();
        else if (a == "--now") now = parse_now(next());
        else if (a == "--release-date") release = next();
        else if (a == "--reproducible") reproducible = true;
        else if (a == "--in-place") in_place = true;
        else if (a == "--no-content-id" || a == "--keep-work") {}
        else if (a == "--work") next();
        else { usage(); return 2; }
    }
    try {
        disc::Disc d(disc_path);
        auto id = disc::identify(d, !no_hash && cmd == "patch");
        if (cmd == "patch") {
            if (id.known) printf("source: %s %s (Redump %d)\n", id.serial.c_str(), id.version.c_str(), id.known->redump);
            else printf("source: %s %s (IP.BIN)\n", id.serial.c_str(), id.version.c_str());
            for (auto& w : id.warnings) printf("warning: %s\n", w.c_str());
            if (!no_hash && !id.known && !any_disc) throw package::PackageError("unknown dump; pass --any-disc to patch it anyway");
        }
        if (pkg_path.empty()) throw package::PackageError("give --package (SF3Patch.exe or the folder it unpacks to)");
        auto pkg = package::load(pkg_path, host_off);
        package::Mods loaded;
        for (auto& m : mods) for (auto& p : package::load_mod(*pkg, m, host_off)) loaded.push_back(p);
        if (!addons.empty())
            for (auto& z : package::addon_archives(addons))
                for (auto& p : package::load_mod(*pkg, z, host_off)) loaded.push_back(p);
        auto lay = package::layer(pkg->patches, loaded, !no_supersede);
        for (auto& n : lay.notes) printf("%s\n", n.c_str());
        auto lst = package::listed(lay.patches, id.serial, id.version);
        if (!lst.errors.empty()) {
            std::string m = "Failed. An error has occured preparing patches:";
            for (auto& e : lst.errors) m += "\n" + e;
            throw package::PackageError(m);
        }
        auto chosen = package::choose(lst.ordered, enable, disable, only);
        std::vector<std::string> names;
        for (auto& r : d.files) names.push_back(r.name);
        auto issues = build::check_conflicts(chosen, names, id.serial, id.version);
        if (cmd == "list") {
            printf("%s %s, package V%s: %zu patches listed, %zu ticked (in apply order)\n", id.serial.c_str(), id.version.c_str(),
                   pkg->version.c_str(), lst.ordered.size(), chosen.size());
            for (auto& p : lst.ordered) {
                bool on = std::find(chosen.begin(), chosen.end(), p) != chosen.end();
                printf("  [%s] %-9s %5d  %-45s %s\n", on ? "x" : " ", dotnet::utf8_encode(p->category).c_str(), p->priority,
                       p->name().c_str(), p->source.c_str());
            }
            for (auto& is : issues) printf("%s: %s\n", is.severity.c_str(), is.message.c_str());
            return 0;
        }
        if (cmd != "patch") { usage(); return 2; }
        if (in_place) {
            if (!out.empty()) throw package::PackageError("give -o or --in-place, not both");
            if (disc::path_suffix_lower(disc_path) != ".cue") throw package::PackageError("--in-place needs a .cue source");
        } else if (out.size() < 4 || out.compare(out.size() - 4, 4, ".bin") != 0)
            throw package::PackageError("the output must be a .bin (its .cue is written next to it)");
        printf("package V%s: applying %zu of %zu listed patches:\n", pkg->version.c_str(), chosen.size(), lst.ordered.size());
        for (auto& p : chosen) printf("  %s\n", p->label().c_str());
        bool err = false;
        for (auto& is : issues) { printf("%s: %s\n", is.severity.c_str(), is.message.c_str()); err |= is.severity == "error"; }
        if (err && !allow_conflicts) throw package::PackageError("conflicting patches (use --allow-conflicts to build anyway)");
        build::Options o;
        o.cdfs_time = cdfs;
        o.host_utc_offset = host_off ? *host_off : local_offset_now();
        o.now = now;
        o.release_date = release;
        if (reproducible) {
            o.host_utc_offset = 0;
            if (!o.now) {
                int64_t t;
                o.now = disc::iso_time_to_utc(d.root.time, cdfs, t) ? t : vfs::civil_to_epoch(2000, 1, 1, 0, 0, 0);
            }
        }
        build::Build b(d, *pkg, chosen, o);
        size_t changed = 0, grown = 0, added = 0;
        for (auto& pl : b.plans) {
            if (pl.steps.empty()) continue;
            changed++;
            if (pl.size != pl.original_size && pl.original_size) grown++;
            if (!pl.original_size && pl.steps[0].second == "new") added++;
        }
        printf("%zu files changed (%zu changed size), %zu added\n", changed, grown, added);
        auto layout = b.layout();
        // In place: write beside the source, then replace its BIN and CUE once complete.
        std::string target = out;
        std::string src_cue = disc_path;
        if (in_place) target = src_cue.substr(0, src_cue.size() - 4) + ".sf3patch-new.bin";
        auto w = build::write_image(target, layout, b.audio_data());
        if (in_place) {
            std::string final_bin = src_cue.substr(0, src_cue.size() - 4) + ".bin";
            // The new CUE names the new BIN; rewrite it for the final name, then swap both in.
            std::string cue = build::cue_text(disc::path_name(final_bin), w.sectors, b.audio_data().size());
            FILE* c = fopen(w.cue_path.c_str(), "wb");
            fwrite(cue.data(), 1, cue.size(), c);
            fclose(c);
            std::vector<std::string> old_files;
            for (auto& t : d.tracks)
                if (std::find(old_files.begin(), old_files.end(), t.path) == old_files.end()) old_files.push_back(t.path);
            d.tracks.clear();   // close the source files before replacing them
            // Keep the originals as .bak (as the menu does), then swap the new image in.
            old_files.push_back(src_cue);
            for (auto& f : old_files)
                if (rename(f.c_str(), (f + ".bak").c_str()) != 0) throw package::PackageError("could not back up " + f);
            if (rename(target.c_str(), final_bin.c_str()) != 0 || rename(w.cue_path.c_str(), src_cue.c_str()) != 0)
                throw package::PackageError("could not replace " + src_cue + " (originals are in the .bak files)");
            printf("patched %s in place (%llu data sectors); originals kept as .bak\n", src_cue.c_str(), (unsigned long long)w.sectors);
        } else
            printf("wrote %s and %s (%llu data sectors)\n", target.c_str(), disc::path_name(w.cue_path).c_str(), (unsigned long long)w.sectors);
        printf("track 1 sha1 %s\n", w.track1_sha1.c_str());
        return 0;
    } catch (const std::exception& e) {
        fprintf(stderr, "sf3patch: %s\n", e.what());
        return 2;
    }
}
