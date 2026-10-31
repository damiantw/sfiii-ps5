// MIT, see LICENSE.
#include "jobs.h"
#include "patch/build.h"
#include "patch/package.h"
#include "disc/identify.h"
#include "disc/sha.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <strings.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctime>

#if SF3_PS5_CONSOLE
namespace sf3::disc { extern int sf3_last_getdents, sf3_last_getdents_errno; }
using sf3::disc::sf3_last_getdents; using sf3::disc::sf3_last_getdents_errno;
#endif

namespace sf3::menu {

namespace {

int64_t local_offset_now() {
    time_t t = time(nullptr);
    struct tm lt;
    localtime_r(&t, &lt);
    return lt.tm_gmtoff;
}

struct Prepared {
    std::unique_ptr<package::Package> pkg;
    package::Listed lst;
    package::Mods chosen;
    std::vector<std::string> notes;
    std::vector<build::Issue> issues;
};

Prepared prepare(const disc::Disc& d, const disc::Identity& id, const PatchRequest& r) {
    if (r.package.empty()) throw package::PackageError("no translation package: download it or copy SF3Patch.exe into the data folder");
    Prepared p;
    p.pkg = package::load(r.package, std::nullopt);
    package::Mods loaded;
    if (!r.addons_dir.empty())
        for (auto& z : package::addon_archives(r.addons_dir))
            for (auto& m : package::load_mod(*p.pkg, z, std::nullopt)) loaded.push_back(m);
    auto lay = package::layer(p.pkg->patches, loaded, true);
    p.notes = lay.notes;
    p.lst = package::listed(lay.patches, id.serial, id.version);
    if (!p.lst.errors.empty()) {
        std::string m = "Failed. An error has occured preparing patches:";
        for (auto& e : p.lst.errors) m += "\n" + e;
        throw package::PackageError(m);
    }
    p.chosen = package::choose(p.lst.ordered, r.enable, r.disable, std::nullopt);
    std::vector<std::string> names;
    for (auto& f : d.files) names.push_back(f.name);
    p.issues = build::check_conflicts(p.chosen, names, id.serial, id.version);
    return p;
}

}  // namespace

PatchList list_patches(const PatchRequest& r) {
    disc::Disc d(r.source);
    auto id = disc::identify(d, false);
    auto p = prepare(d, id, r);
    PatchList out;
    out.serial = id.serial;
    out.version = id.version;
    out.package_version = p.pkg->version;
    out.notes = p.notes;
    for (auto& is : p.issues) out.issues.push_back(is.severity + ": " + is.message);
    for (auto& m : p.lst.ordered)
        out.entries.push_back({m->name(), m->label(), dotnet::utf8_encode(m->category), m->priority,
                               std::find(p.chosen.begin(), p.chosen.end(), m) != p.chosen.end()});
    return out;
}

std::string patch_disc(const PatchRequest& r, const std::function<void(const std::string&)>& log,
                       const std::function<void(double)>& progress) {
    disc::Disc d(r.source);
    auto id = disc::identify(d, true);
    log("source: " + id.serial + " " + id.version + (id.known ? " (Redump verified)" : " (unrecognised dump)"));
    for (auto& w : id.warnings) log("warning: " + w);
    if (!id.known) throw package::PackageError("unknown dump: only the four Redump-verified discs can be patched here");
    auto p = prepare(d, id, r);
    for (auto& n : p.notes) log(n);
    for (auto& is : p.issues) {
        log(is.severity + ": " + is.message);
        if (is.severity == "error") throw package::PackageError("conflicting patches; untick one of them");
    }
    log("package V" + p.pkg->version + ": applying " + std::to_string(p.chosen.size()) + " patches");
    std::string src = r.source, target = r.output;
    if (r.in_place) {
        if (disc::path_suffix_lower(src) != ".cue") throw package::PackageError("in-place patching needs a .cue image");
        target = src.substr(0, src.size() - 4) + ".sf3patch-new.bin";
    } else if (target.size() < 4 || target.compare(target.size() - 4, 4, ".bin") != 0)
        throw package::PackageError("the output must be a .bin");
    build::Options o;
    o.host_utc_offset = local_offset_now();
    if (r.reproducible) {
        o.host_utc_offset = 0;
        int64_t t;
        o.now = disc::iso_time_to_utc(d.root.time, o.cdfs_time, t) ? t : vfs::civil_to_epoch(2000, 1, 1, 0, 0, 0);
    }
    build::Build b(d, *p.pkg, p.chosen, o);
    auto layout = b.layout();
    auto audio = b.audio_data();
    auto w = build::write_image(target, layout, audio, [&](uint64_t done, uint64_t total) {
        if (total) progress(double(done) / double(total));
    });
    if (r.in_place) {
        // Keep the originals as .bak (the menu's backup), then swap the new image in.
        std::string final_bin = src.substr(0, src.size() - 4) + ".bin";
        std::string cue = build::cue_text(disc::path_name(final_bin), w.sectors, audio.size());
        FILE* c = fopen(w.cue_path.c_str(), "wb");
        if (!c || fwrite(cue.data(), 1, cue.size(), c) != cue.size() || fclose(c) != 0)
            throw package::PackageError("could not write " + w.cue_path);
        std::vector<std::string> old;
        for (auto& t : d.tracks)
            if (std::find(old.begin(), old.end(), t.path) == old.end()) old.push_back(t.path);
        d.tracks.clear();
        old.push_back(src);
        for (auto& f : old)
            if (rename(f.c_str(), (f + ".bak").c_str()) != 0) throw package::PackageError("could not back up " + f);
        if (rename(target.c_str(), final_bin.c_str()) != 0 || rename(w.cue_path.c_str(), src.c_str()) != 0)
            throw package::PackageError("could not replace " + src + " (originals are in the .bak files)");
        log("patched in place; originals kept as .bak");
    } else
        log("wrote " + target);
    log("track 1 sha1 " + w.track1_sha1);
    return w.track1_sha1;
}

Jobs::~Jobs() { join(); }

void Jobs::join() { if (th_.joinable()) th_.join(); }

JobStatus Jobs::status() const { std::lock_guard<std::mutex> l(mu_); return st_; }

JobStatus Jobs::last(JobKind k) const { std::lock_guard<std::mutex> l(mu_); return last_[int(k)]; }

disc::ScanResult Jobs::scan_result() const { std::lock_guard<std::mutex> l(mu_); return scan_; }

void Jobs::log(const std::string& m) {
    std::lock_guard<std::mutex> l(mu_);
    st_.log.push_back(m);
    st_.message = m;
}

void Jobs::progress(double p) { std::lock_guard<std::mutex> l(mu_); st_.progress = p; }

void Jobs::run(JobKind kind, const std::string& title, std::function<void()> body) {
    if (running_.exchange(true)) return;
    join();
    { std::lock_guard<std::mutex> l(mu_); st_ = {}; st_.id = ++next_id_; st_.kind = kind; st_.running = true; st_.title = title; st_.progress = -1; }
    th_ = std::thread([this, body] {
        bool failed = false;
        try { body(); } catch (const std::exception& e) { log(std::string("error: ") + e.what()); failed = true; }
        { std::lock_guard<std::mutex> l(mu_); st_.running = false; st_.done = true; st_.failed = failed; last_[int(st_.kind)] = st_; }
        running_ = false;
    });
}

void Jobs::start_prepare(const std::string& title, PrepareBody body) {
    run(JobKind::Prepare, title, [this, body] {
        std::string last;
        body([this, &last](double p, const std::string& what) {
            progress(p);
            if (what != last) { last = what; log(what); }
        });
    });
}

void Jobs::start_scan(const std::string& dir, bool refresh) {
    run(JobKind::Scan, "Scanning " + dir, [this, dir, refresh] {
        disc::ScanOptions o;
        o.cache_path = scan_cache, o.refresh = refresh;
        int reused = 0;
        o.reused = &reused;
        o.progress = [this](const std::string& p) { log("reading " + disc::path_name(p)); };
        auto r = disc::scan(dir, o);
        { std::lock_guard<std::mutex> l(mu_); scan_ = r; }
        log("found " + std::to_string(r.all.size()) + " images" +
            (reused ? " (" + std::to_string(reused) + " unchanged since the last scan)" : ""));
        if (r.all.empty()) {  // say why, on one line (the status shows the last message)
            auto err = [](bool ok) { return ok ? std::string("ok") : std::string(std::strerror(errno)); };
            struct stat st;
            std::string m = "stat /data " + err(stat("/data", &st) == 0);
            int fd = open(dir.c_str(), O_RDONLY | O_DIRECTORY);
            m += "; open " + err(fd >= 0);
            if (fd >= 0) close(fd);
            DIR* d = opendir(dir.c_str());
            m += "; opendir " + err(d != nullptr);
            if (d) closedir(d);
            auto names = disc::list_dir(dir);
            m += "; " + std::to_string(names.size()) + " entries";
#if SF3_PS5_CONSOLE
            m += " (getdents " + std::to_string(sf3_last_getdents) +
                 (sf3_last_getdents_errno ? std::string(" ") + std::strerror(sf3_last_getdents_errno) : std::string()) + ")";
#endif
            for (size_t i = 0; i < names.size() && i < 3; i++)
                m += (i ? ", " : ": ") + names[i] + (disc::is_dir(disc::path_join(dir, names[i])) ? "/" : "");
            log(m);
        }
    });
}

void Jobs::start_patch(const PatchRequest& r) {
    run(JobKind::Patch, "Patching " + disc::path_name(r.source), [this, r] {
        patch_disc(r, [this](const std::string& m) { log(m); }, [this](double p) { progress(p); });
    });
}

void Jobs::start_fetch(const std::string& dest, std::shared_ptr<fetch::Transport> t) {
    run(JobKind::Fetch, "Downloading the translation package", [this, dest, t] {
        fetch::fetch(*t, dest, true, true, [this](const std::string& m) { log(m); },
                     [this](const std::string&, uint64_t done, uint64_t total) { if (total) progress(double(done) / double(total)); });
    });
}

namespace {
bool file_size(const std::string& p, uint64_t& n) {
    struct stat st;
    if (stat(p.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
    n = uint64_t(st.st_size);
    return true;
}
std::string dir_of(const std::string& p) {
    auto s = p.find_last_of('/');
    return s == std::string::npos ? std::string() : p.substr(0, s + 1);
}
// FILE "name" entries of a cue sheet, in order.
std::vector<std::string> cue_files(const std::string& cue_path) {
    std::vector<std::string> out;
    FILE* f = fopen(cue_path.c_str(), "rb");
    if (!f) return out;
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        const char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (strncasecmp(p, "FILE", 4) != 0) continue;
        const char* a = strchr(p, '"');
        const char* b = a ? strchr(a + 1, '"') : nullptr;
        if (a && b) out.emplace_back(a + 1, b);
    }
    fclose(f);
    return out;
}
std::string sha1_of(const std::string& p, uint64_t total, uint64_t& done, uint64_t all,
                    const std::function<void(double)>& progress) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) throw package::PackageError("could not read " + disc::path_name(p));
    disc::Sha1 h;
    std::vector<uint8_t> buf(1 << 20);
    uint64_t got = 0;
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), f)) > 0) {
        h.update(buf.data(), n);
        got += n; done += n;
        if (all) progress(double(done) / double(all));
    }
    fclose(f);
    if (got != total) throw package::PackageError("could not read all of " + disc::path_name(p));
    return h.hex();
}
}  // namespace

std::string restore_precheck(const std::string& cue) {
    uint64_t n;
    if (!file_size(cue + ".bak", n)) return "no backup: " + disc::path_name(cue) + ".bak is missing";
    auto files = cue_files(cue + ".bak");
    if (files.empty()) return "the backup cue lists no tracks";
    for (auto& t : files)
        if (!file_size(dir_of(cue) + t + ".bak", n)) return "backup missing: " + t + ".bak";
    return "";
}

void restore_original(const std::string& cue, const std::function<void(const std::string&)>& log,
                      const std::function<void(double)>& progress) {
    std::string why = restore_precheck(cue);
    if (!why.empty()) throw package::PackageError("not restored, " + why);
    std::string dir = dir_of(cue);
    auto files = cue_files(cue + ".bak");
    if (files.size() < 2) throw package::PackageError("not restored, the backup cue should list 2 tracks");
    // The recorded hashes: track 1 picks the disc, then every track must match it.
    std::vector<uint64_t> sizes;
    uint64_t all = 0;
    for (auto& t : files) { uint64_t n = 0; file_size(dir + t + ".bak", n); sizes.push_back(n); all += n; }
    uint64_t done = 0;
    log("checking " + std::to_string(files.size()) + " backup files against the recorded hashes");
    std::string s1 = sha1_of(dir + files[0] + ".bak", sizes[0], done, all, progress);
    const disc::Known* k = disc::by_track1(s1);
    if (!k || sizes[0] != k->track1_size)
        throw package::PackageError("not restored: " + files[0] + ".bak does not match a known original (sha1 " + s1 + ")");
    log(files[0] + ".bak: " + k->title + " track 1 OK");
    std::string s2 = sha1_of(dir + files[1] + ".bak", sizes[1], done, all, progress);
    if (s2 != k->track2_sha1 || sizes[1] != k->track2_size)
        throw package::PackageError("not restored: " + files[1] + ".bak does not match the recorded " + k->title + " track 2 (sha1 " + s2 + ")");
    log(files[1] + ".bak: track 2 OK");
    if (files.size() > 2) throw package::PackageError("not restored: the backup cue lists more tracks than the recorded original");
    // The translated image: whatever the current cue points at, unless it is one of the originals.
    std::vector<std::string> cur = cue_files(cue);
    for (auto& c : cur)
        for (auto& t : files)
            if (c == t) throw package::PackageError("not restored: " + c + " is both the translated image and an original track");
    // Swap: translated cue aside, originals back, then drop the translated files.
    std::string aside = cue + ".translated";
    if (rename(cue.c_str(), aside.c_str()) != 0) throw package::PackageError("could not move " + disc::path_name(cue) + " aside");
    for (auto& t : files)
        if (rename((dir + t + ".bak").c_str(), (dir + t).c_str()) != 0)
            throw package::PackageError("could not restore " + t + " (the translated cue is " + disc::path_name(aside) + ")");
    if (rename((cue + ".bak").c_str(), cue.c_str()) != 0) throw package::PackageError("could not restore " + disc::path_name(cue));
    for (auto& c : cur) { unlink((dir + c).c_str()); log("removed the translated " + c); }
    unlink(aside.c_str());
    progress(1);
    log("restored the original " + std::string(k->title));
}

void Jobs::start_restore(const std::string& cue) {
    run(JobKind::Restore, "Restoring " + disc::path_name(cue), [this, cue] {
        restore_original(cue, [this](const std::string& m) { log(m); }, [this](double p) { progress(p); });
    });
}

}  // namespace sf3::menu
