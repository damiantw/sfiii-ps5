// from_7z against the same files on disk: names, bytes and mtimes (to the second) must match.
//   sevenzip_test ARCHIVE.7z FOLDER
#include "patch/vfs.h"
#include <cstdio>
#include <map>
using namespace sf3::vfs;
static void walk(const Node* n, const Node* root, std::map<std::string, const Node*>& out) {
    for (auto& k : n->kids) {
        if (k->dir) walk(k.get(), root, out);
        else out[k->relative_to(root)] = k.get();
    }
}
int main(int argc, char** argv) {
    if (argc != 3) { fprintf(stderr, "usage: sevenzip_test ARCHIVE.7z FOLDER\n"); return 2; }
    std::unique_ptr<Node> a, d;
    try { a = from_7z(argv[1]); d = from_disk(argv[2]); }
    catch (const std::exception& e) { fprintf(stderr, "sevenzip_test: %s\n", e.what()); return 1; }
    std::map<std::string, const Node*> fa, fd;
    walk(a.get(), a.get(), fa);
    walk(d.get(), d.get(), fd);
    int bad = 0;
    for (auto& [p, n] : fd) {
        auto it = fa.find(p);
        if (it == fa.end()) { printf("missing %s\n", p.c_str()); bad++; continue; }
        if (it->second->read() != n->read()) { printf("bytes differ %s\n", p.c_str()); bad++; }
        if (it->second->mtime.sec != n->mtime.sec) { printf("mtime differs %s\n", p.c_str()); bad++; }
    }
    if (fa.size() != fd.size()) { printf("%zu files in the archive, %zu on disk\n", fa.size(), fd.size()); bad++; }
    printf("%s: %zu files, %s\n", argv[1], fa.size(), bad ? "FAILED" : "ok");
    return bad ? 1 : 0;
}
