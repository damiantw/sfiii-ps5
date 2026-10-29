// See prepare.h. MIT, see LICENSE.
#include "prepare.h"
#include "disc/cdimage.h"
#include "disc/manifest.h"
#include "code_cache.h"      // sfiii-native native/recomp/gen
#include "profile_gen.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace sf3::prepare {
namespace {

// The image's files (the .cue/.chd and each track's file) with size and mtime: a cheap check that
// the image is the one hashed before.
std::string stamp(const disc::Disc& d) {
    std::vector<std::string> paths{d.cue};
    for (auto& t : d.tracks) if (!t.path.empty()) paths.push_back(t.path);
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    std::string s;
    for (auto& p : paths) {
        std::error_code ec;
        auto size = fs::file_size(p, ec);
        auto t = fs::last_write_time(p, ec).time_since_epoch().count();
        s += fs::absolute(p).string() + "|" + std::to_string((unsigned long long)size) + "|" + std::to_string((long long)t) + ";";
    }
    return s;
}

std::string index_path(const std::string& data) { return data + "/cache/images.tsv"; }

std::string cached_id(const std::string& data, const std::string& st) {
    std::ifstream f(index_path(data));
    std::string line;
    while (std::getline(f, line)) {
        auto tab = line.rfind('\t');
        if (tab != std::string::npos && line.compare(0, tab, st) == 0) return line.substr(tab + 1);
    }
    return {};
}

void remember(const std::string& data, const std::string& st, const std::string& id) {
    std::vector<std::string> keep;
    {
        std::ifstream f(index_path(data));
        std::string line;
        while (std::getline(f, line)) if (line.rfind(st + "\t", 0) != 0) keep.push_back(line);
    }
    keep.push_back(st + "\t" + id);
    std::string tmp = index_path(data) + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        for (auto& l : keep) f << l << "\n";
    }
    std::error_code ec;
    fs::rename(tmp, index_path(data), ec);
}

bool complete(const std::string& dir) { std::error_code ec; return fs::exists(dir + "/modules.sf3m", ec); }

}  // namespace

std::string set_dir_for(const std::string& data, const std::string& id) {
    return data + "/cache/" + id + "-abi" + std::to_string(gen::kAbi);
}

Result prepare_disc(const std::string& image, const std::string& data, const std::string& profiles,
                    const std::function<void(double, const std::string&)>& progress) {
    Result r;
    auto say = [&](double p, const std::string& what) { if (progress) progress(p, what); };
    std::string work;
    try {
        std::error_code ec;
        fs::create_directories(data + "/cache", ec);
        say(0, "Reading the disc");
        disc::Disc d(image);
        std::string st = stamp(d);
        std::string id = cached_id(data, st);
        if (!id.empty() && complete(set_dir_for(data, id))) {
            r.ok = r.cached = true, r.content_id = id, r.set_dir = set_dir_for(data, id);
            return r;
        }
        work = data + "/cache/work-" + std::to_string(getpid());
        fs::remove_all(work, ec);
        fs::create_directories(work + "/extract");
        disc::Probe p = disc::probe(d, work + "/extract", [&](uint64_t done, uint64_t total) {
            say(total ? 0.35 * double(done) / double(total) : 0, "Reading the disc");
        });
        r.content_id = p.content_id;
        remember(data, st, p.content_id);
        r.set_dir = set_dir_for(data, p.content_id);
        if (complete(r.set_dir)) { fs::remove_all(work, ec); r.ok = r.cached = true; return r; }
        r.profile = disc::match_profile(profiles, p.content_id);
        if (r.profile.empty())
            throw std::runtime_error("this disc's contents are not a supported revision (content ID " + p.content_id + ")");
        { std::ofstream(work + "/manifest.json", std::ios::binary) << p.manifest_json; }
        say(0.35, "Finding the game's code");
        std::string candidate = gen::audit_profile(work, r.profile);
        { std::ofstream(work + "/candidate.json", std::ios::binary) << candidate; }
        std::string tmp = r.set_dir + ".tmp";
        fs::remove_all(tmp, ec);
        gen::GenOptions o;
        o.work = work;
        o.profile = work + "/candidate.json";
        o.x64_set = tmp;
        o.progress = [&](size_t done, size_t total, const std::string& name) {
            say(0.45 + 0.55 * double(done) / double(total ? total : 1), "Translating " + name);
        };
        gen::generate_from_profile(o);
        if (!complete(tmp)) throw std::runtime_error("the generator wrote no module set");
        fs::remove_all(r.set_dir, ec);
        fs::rename(tmp, r.set_dir);
        fs::remove_all(work, ec);
        say(1, "Ready");
        r.ok = true;
    } catch (const std::exception& e) {
        r.why = e.what();
        std::error_code ec;
        if (!work.empty()) fs::remove_all(work, ec);
        if (!r.set_dir.empty()) fs::remove_all(r.set_dir + ".tmp", ec);
    }
    return r;
}

}  // namespace sf3::prepare
