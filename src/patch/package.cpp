// MIT, see LICENSE. Port of sfiii-native tools/sf3patch/package.py and selection.py.
#include "package.h"
#include "netjson.h"
#include "disc/cdimage.h"
#include <algorithm>
#include <functional>
#include <regex>
#include <set>
#include <sys/stat.h>

namespace sf3::package {
using namespace dotnet;
using vfs::Node;

static const char* CATEGORIES[] = {"general", "bugfixes", "qol", "mods"};

bool PatchDir::applies_to(const std::string& serial, const std::string& version) const {
    auto top = dir->files({serial});
    if (top && !top->empty()) return true;
    auto sub = dir->files({serial, version});
    return sub && !sub->empty();
}

static void read_metadata(PatchDir& p) {
    const Node* meta = p.dir->get({"metadata.json"});
    try {
        auto doc = netjson::loads(netjson::read_text(meta->read()));
        if (doc.kind != netjson::Value::Object) throw netjson::JsonError("metadata.json is not an object");
        auto g = [&](const char* k) { return doc.get(utf8_decode(std::string(k))); };
        p.category = lower(netjson::category_string(g("category")));
        p.text = netjson::to_net_string(g("text"));
        p.enabled = netjson::to_net_bool(g("enabled"));
        const netjson::Value* pr = g("priority");
        if (pr && pr->kind == netjson::Value::Null) throw netjson::JsonError("Cannot convert null to 'int'");
        p.priority = pr ? netjson::to_net_int(pr) : 9999;
        p.title = netjson::to_net_string(g("title"));
        p.desc = netjson::to_net_string(g("desc"));
    } catch (const std::exception& e) {
        p.meta_error = "Error with patch file:" + p.name() + " (" + e.what() + ")";
    }
}

static Mods patch_dirs(const Node* parent, const std::string& source) {
    Mods out;
    if (!parent || !parent->dir) return out;
    for (Node* d : parent->sorted()) {
        if (!d->dir) continue;
        if (d->get({"metadata.json"})) {
            auto p = std::make_shared<PatchDir>();
            p->dir = d;
            p->source = source;
            out.push_back(p);
        }
    }
    return out;
}

static void find_mod_dirs(const Node* d, const std::string& source, Mods& out) {
    // os.walk top-down; sub-folders in NTFS order; stop below a folder with metadata.json.
    for (auto& k : d->kids)
        if (!k->dir && lower(utf8_decode(k->name)) == U"metadata.json") {
            auto p = std::make_shared<PatchDir>();
            p->dir = d;
            p->source = source;
            out.push_back(p);
            return;
        }
    for (Node* k : d->sorted()) if (k->dir) find_mod_dirs(k, source, out);
}

static const Node* find_wizard_root(const Node* d) {
    bool patches = false, order = false;
    std::vector<const Node*> subs;
    for (auto& k : d->kids)
        if (k->dir) {
            U32 u = upper(utf8_decode(k->name));
            patches |= u == U"PATCHES";
            order |= u == U"DRIVEORDER";
            subs.push_back(k.get());
        }
    if (patches && order) return d;
    std::sort(subs.begin(), subs.end(), [](const Node* a, const Node* b) {
        return utf8_decode(a->name) < utf8_decode(b->name);
    });
    for (auto* s : subs) if (auto r = find_wizard_root(s)) return r;
    return nullptr;
}

std::vector<std::string> Package::drive_order(const std::string& serial) const {
    const Node* p = root->get({"DriveOrder", serial + ".txt"});
    if (!p) throw PackageError("the package has no DriveOrder/" + serial + ".txt, so the wizard cannot patch this disc");
    U32 t = netjson::read_text(p->read());
    U32 text;
    for (size_t i = 0; i < t.size(); i++) {
        if (t[i] == U'\r' && i + 1 < t.size() && t[i + 1] == U'\n') { text += U'\n'; i++; }
        else text += t[i];
    }
    std::vector<std::string> out;
    size_t st = 0;
    for (;;) {
        size_t nl = text.find(U'\n', st);
        U32 line = text.substr(st, nl == U32::npos ? U32::npos : nl - st);
        out.push_back(utf8_encode(trim(line.substr(0, line.find(U',')))));
        if (nl == U32::npos) break;
        st = nl + 1;
    }
    return out;
}

static bool is_file(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::unique_ptr<Package> load(const std::string& path, vfs::Offset off) {
    auto pkg = std::make_unique<Package>();
    std::unique_ptr<Node> tree;
    if (is_file(path)) {
        bool py = disc::path_suffix_lower(path) == ".zip";
        tree = vfs::from_zip(path, py, off);
    } else tree = vfs::from_disk(path);
    const Node* root = find_wizard_root(tree.get());
    if (!root) throw PackageError(path + " does not look like the patch wizard folder (no Patches/ and DriveOrder/)");
    pkg->root = root;
    const Node* data = root->get({"Data", "data.json"});
    if (!data) throw PackageError(root->path() + " has no Data/data.json");
    auto doc = netjson::loads(netjson::read_text(data->read()));
    std::optional<U32> version;
    if (doc.kind == netjson::Value::Object) version = netjson::to_net_string(doc.get(U"version"));
    if (!version || version->empty()) throw PackageError("Data/data.json has no version");
    pkg->version = utf8_encode(*version);
    pkg->patches = patch_dirs(root->get({"Patches"}), "Patches");
    for (auto& p : patch_dirs(root->get({"Custom Mods"}), "Custom Mods")) pkg->patches.push_back(p);
    for (auto& p : pkg->patches) read_metadata(*p);
    pkg->trees.push_back(std::move(tree));
    return pkg;
}

Mods load_mod(Package& owner, const std::string& path, vfs::Offset off) {
    std::unique_ptr<Node> tree;
    if (is_file(path)) {
        std::string suf = disc::path_suffix_lower(path);
        if (suf == ".7z") tree = vfs::from_7z(path);
        else if (suf == ".zip") tree = vfs::from_zip(path, true, off);
        else throw PackageError(disc::path_name(path) + ": mods must be a .zip, .7z or folder");
    } else tree = vfs::from_disk(path);
    Mods out;
    find_mod_dirs(tree.get(), "mod:" + tree->name, out);
    for (auto& p : out) read_metadata(*p);
    owner.trees.push_back(std::move(tree));
    return out;
}

static const std::set<std::string> SKIP = {"SF3Translation_PatchWizard_WinScalingFix.zip"};

std::vector<std::string> addon_archives(const std::string& dir) {
    std::vector<std::pair<U32, std::string>> v;
    for (auto& n : disc::list_dir(dir)) {
        std::string s = disc::path_suffix_lower(n);
        if ((s == ".zip" || s == ".7z") && !SKIP.count(n)) v.emplace_back(lower(utf8_decode(n)), n);
    }
    std::stable_sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first < b.first; });
    std::vector<std::string> out;
    for (auto& [k, n] : v) out.push_back(disc::path_join(dir, n));
    return out;
}

// ------------------------------------------------------------------ selection
static int compare(const std::shared_ptr<PatchDir>& x, const std::shared_ptr<PatchDir>& y) {
    if (x->priority == y->priority) return culture_compare(x->text.value_or(U""), y->text.value_or(U""));
    return x->priority - y->priority;
}

Listed listed(const Mods& patches, const std::string& serial, const std::string& version) {
    Listed r;
    std::vector<Mods> groups(4);
    for (auto& p : patches) {
        if (!p->applies_to(serial, version)) continue;
        if (!p->meta_error.empty()) { r.errors.push_back(p->meta_error); continue; }
        for (int c = 0; c < 4; c++)
            if (p->category == utf8_decode(std::string(CATEGORIES[c]))) groups[size_t(c)].push_back(p);
    }
    for (auto& g : groups) {
        introsort(g, compare);
        r.ordered.insert(r.ordered.end(), g.begin(), g.end());
    }
    return r;
}

static std::shared_ptr<PatchDir> match(const Mods& patches, const std::string& name) {
    U32 key = lower(python_strip(utf8_decode(name)));
    Mods hits;
    for (auto& p : patches) if (lower(utf8_decode(p->label())) == key) hits.push_back(p);
    if (hits.empty()) for (auto& p : patches) if (lower(utf8_decode(p->name())) == key) hits.push_back(p);
    if (hits.empty()) for (auto& p : patches) if (lower(p->title.value_or(U"")) == key) hits.push_back(p);
    if (hits.empty()) throw PackageError("no patch named '" + name + "' for this disc (see `sf3patch list`)");
    if (hits.size() > 1) {
        std::string l;
        for (auto& p : hits) l += (l.empty() ? "" : ", ") + p->label();
        throw PackageError("'" + name + "' matches several patches: " + l + "; use the SOURCE/FOLDER form");
    }
    return hits[0];
}

Mods choose(const Mods& ordered, const std::vector<std::string>& enable, const std::vector<std::string>& disable,
            const std::optional<std::vector<std::string>>& only) {
    std::map<const PatchDir*, bool> state;
    for (auto& p : ordered) state[p.get()] = only ? false : p->enabled;
    if (only) for (auto& n : *only) state[match(ordered, n).get()] = true;
    for (auto& n : enable) state[match(ordered, n).get()] = true;
    for (auto& n : disable) state[match(ordered, n).get()] = false;
    Mods out;
    for (auto& p : ordered) if (state[p.get()]) out.push_back(p);
    return out;
}

static const std::vector<std::pair<std::string, std::vector<std::string>>> SUPERSEDES = {
    {"Blacksmith Interface Update v3.1", {"Blacksmith Interface Update v2.02"}},
    {"Blacksmith Results v2.1", {"Blacksmith Results v1.4"}},
    {"Critical and Counter Fix v1.11", {"Critical and Counter Fix"}},
    {"Irene Insane Buff 1.02", {"Irene Insane Buff"}},
    {"Rogan Sprite Fix v5", {"Rogan Sprite Fix"}},
};
static const std::vector<std::pair<std::string, std::string>> ADDON_DEFAULT_OFF = {
    {"Magic Resistance Icons v2", "alternative to Elemental Icons 3.0 + Mod Essentials 0.5.1 (same X017.BIN bytes)"},
};

static U32 stem(const std::string& name) {
    // re.sub(r"\s+(v?\d+(\.\d+)*[a-z]?|rc\d+)$", "", name.strip(), flags=re.I).lower()
    std::string s = utf8_encode(python_strip(utf8_decode(name)));
    static const std::regex re(R"([ \t\n\r\f\v]+(v?[0-9]+(\.[0-9]+)*[a-z]?|rc[0-9]+)$)", std::regex::icase);
    return lower(utf8_decode(std::regex_replace(s, re, "", std::regex_constants::format_first_only)));
}

Layered layer(const Mods& base, const Mods& mods, bool supersede) {
    Layered r;
    std::set<U32> drop;
    std::set<std::string> mod_names;
    for (auto& m : mods) mod_names.insert(m->name());
    if (supersede)
        for (auto& [nw, olds] : SUPERSEDES)
            if (mod_names.count(nw)) for (auto& o : olds) drop.insert(lower(utf8_decode(o)));
    for (auto& m : mods)
        for (auto& [n, why] : ADDON_DEFAULT_OFF)
            if (m->name() == n && m->enabled) {
                m->enabled = false;
                r.notes.push_back("unticked " + m->label() + " by default: " + why);
            }
    Mods all = base;
    all.insert(all.end(), mods.begin(), mods.end());
    for (auto& p : all) {
        if (drop.count(lower(utf8_decode(p->name())))) { r.notes.push_back("dropped " + p->label() + " (superseded by a newer add-on)"); continue; }
        r.patches.push_back(p);
    }
    std::set<U32> known;
    for (auto& [nw, olds] : SUPERSEDES) for (auto& o : olds) known.insert(lower(utf8_decode(o)));
    for (auto& m : mods)
        for (auto& p : base)
            if (!known.count(lower(utf8_decode(p->name()))) && p->name() != m->name() && stem(p->name()) == stem(m->name()))
                r.notes.push_back("warning: " + m->label() + " looks like a newer version of " + p->label() + "; both are kept");
    return r;
}

}  // namespace sf3::package
