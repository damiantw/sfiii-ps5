// MIT, see LICENSE.
// A PS5 title has no working directory, so the system getcwd and realpath fail
// with EPERM. These replacements never touch a working directory:
//  - getcwd reports "/app0", the title's own image, which is where a relative
//    path is resolved against here.
//  - realpath normalizes the path lexically ("//", "." and ".." removed), puts
//    relative paths under getcwd, and checks that the result exists with stat.
//    Symbolic links are not followed (homebrew paths on the PS5 have none).
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

static const char sf3_cwd[] = "/app0";

char* getcwd(char* buf, size_t size) {
    size_t n = sizeof sf3_cwd;
    if (!buf) {
        if (size == 0) size = n;
        buf = malloc(size);
        if (!buf) { errno = ENOMEM; return NULL; }
    } else if (size == 0) { errno = EINVAL; return NULL; }
    if (size < n) { errno = ERANGE; return NULL; }
    memcpy(buf, sf3_cwd, n);
    return buf;
}

char* realpath(const char* path, char* resolved) {
    if (!path) { errno = EINVAL; return NULL; }
    if (!*path) { errno = ENOENT; return NULL; }
    char out[PATH_MAX];
    size_t len = 0;
    if (path[0] != '/') {  // relative: start from the reported working directory
        len = strlen(sf3_cwd);
        memcpy(out, sf3_cwd, len);
    }
    const char* p = path;
    while (*p) {
        while (*p == '/') p++;
        const char* s = p;
        while (*p && *p != '/') p++;
        size_t k = (size_t)(p - s);
        if (k == 0 || (k == 1 && s[0] == '.')) continue;
        if (k == 2 && s[0] == '.' && s[1] == '.') {
            while (len > 0 && out[len - 1] != '/') len--;
            if (len > 0) len--;
            continue;
        }
        if (len + 1 + k >= sizeof out) { errno = ENAMETOOLONG; return NULL; }
        out[len++] = '/';
        memcpy(out + len, s, k);
        len += k;
    }
    if (len == 0) out[len++] = '/';
    out[len] = 0;
    struct stat st;
    if (stat(out, &st) != 0) return NULL;  // errno from stat
    if (!resolved) {
        resolved = malloc(len + 1);
        if (!resolved) { errno = ENOMEM; return NULL; }
    }
    memcpy(resolved, out, len + 1);
    return resolved;
}
