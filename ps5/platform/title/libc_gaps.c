// SPDX-License-Identifier: MIT
// sfiii-ps5 platform layer: the C library functions RADV's archive (libvulkan_radeon.ps5.a)
// imports that the console's libc does not give a process. Written for this project from the
// symbol list a link reports and the functions' documented (POSIX / FreeBSD man page) behaviour;
// see docs/platform.md. Debug-only paths (popen, open_memstream, regex) fail cleanly.
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct tm;
struct passwd;
typedef struct _sf3_dir DIR;
typedef struct { int dummy; } sf3_regex_t;

void __assert(const char* func, const char* file, int line, const char* expr) {
    fprintf(stderr, "assertion \"%s\" failed: %s:%d (%s)\n", expr, file, line, func ? func : "?");
    abort();
}

// FreeBSD's uname() backend: five fields of namesize bytes each (sysname, nodename, release,
// version, machine).
int __xuname(int namesize, void* namebuf) {
    static const char* v[5] = {"PS5", "ps5", "0", "sfiii-ps5", "amd64"};
    char* p = (char*)namebuf;
    for (int i = 0; i < 5; ++i, p += namesize) { strncpy(p, v[i], (size_t)namesize); p[namesize - 1] = 0; }
    return 0;
}

typedef struct { const char* dli_fname; void* dli_fbase; const char* dli_sname; void* dli_saddr; } sf3_dl_info;
int dladdr(const void* addr, sf3_dl_info* info) { (void)addr; (void)info; return 0; }   // 0: not found

// Locales: only the C locale exists here, so the _l functions are the plain ones.
#include <xlocale.h>
static int g_c_locale;
locale_t newlocale(int mask, const char* name, locale_t base) { (void)mask; (void)name; (void)base; return (locale_t)&g_c_locale; }
int freelocale(locale_t l) { (void)l; return 0; }
double strtod_l(const char* s, char** end, locale_t l) { (void)l; return strtod(s, end); }
float strtof_l(const char* s, char** end, locale_t l) { (void)l; return strtof(s, end); }

int getpwuid_r(unsigned uid, struct passwd* pw, char* buf, size_t len, struct passwd** out) {
    (void)uid; (void)pw; (void)buf; (void)len; *out = NULL; return ENOENT;
}

// UTC (the console's zone is not needed by the driver's debug dumps); fields per <time.h>.
struct sf3_tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst; long tm_gmtoff; char* tm_zone; };
struct tm* localtime_r(const time_t* t, struct tm* out) {
    struct sf3_tm* r = (struct sf3_tm*)out;
    int64_t s = (int64_t)*t, days = s / 86400, rem = s % 86400;
    if (rem < 0) rem += 86400, --days;
    r->tm_hour = (int)(rem / 3600), r->tm_min = (int)(rem / 60 % 60), r->tm_sec = (int)(rem % 60);
    r->tm_wday = (int)(((days + 4) % 7 + 7) % 7);
    // days since 1970-01-01 to civil date (Howard Hinnant's public-domain algorithm)
    int64_t z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = yoe + era * 400, doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    int64_t d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
    y += m <= 2;
    r->tm_year = (int)(y - 1900), r->tm_mon = (int)(m - 1), r->tm_mday = (int)d;
    static const int before[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    r->tm_yday = before[m - 1] + (int)d - 1 + (leap && m > 2);
    r->tm_isdst = 0, r->tm_gmtoff = 0, r->tm_zone = (char*)"UTC";
    return out;
}

int mkstemps(char* tmpl, int suffixlen) {
    size_t n = strlen(tmpl);
    if ((size_t)suffixlen > n || n - (size_t)suffixlen < 6) { errno = EINVAL; return -1; }
    char* x = tmpl + n - suffixlen - 6;
    static const char abc[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    static uint32_t seed = 0x9E3779B9u;
    for (int tries = 0; tries < 100; ++tries) {
        seed ^= (uint32_t)(uintptr_t)tmpl + (uint32_t)time(NULL) + (uint32_t)tries;
        for (int i = 0; i < 6; ++i) { seed = seed * 1103515245u + 12345u; x[i] = abc[(seed >> 16) % 36]; }
        int fd = open(tmpl, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0 || errno != EEXIST) return fd;
    }
    errno = EEXIST;
    return -1;
}

FILE* open_memstream(char** buf, size_t* size) { (void)buf; (void)size; errno = ENOTSUP; return NULL; }
void openlog(const char* ident, int opt, int facility) { (void)ident; (void)opt; (void)facility; }
FILE* popen(const char* cmd, const char* mode) { (void)cmd; (void)mode; errno = ENOTSUP; return NULL; }
int pclose(FILE* f) { (void)f; errno = ECHILD; return -1; }

int posix_fallocate(int fd, off_t offset, off_t len) {
    off_t end = offset + len, cur = lseek(fd, 0, SEEK_END);
    if (cur < 0) return errno;
    if (cur < end && ftruncate(fd, end) != 0) return errno;
    return 0;
}

// BSD order: the thunk comes before the comparator and is passed first.
static void swap_bytes(char* a, char* b, size_t n) { while (n--) { char t = *a; *a++ = *b; *b++ = t; } }
void qsort_r(void* base, size_t n, size_t size, void* thunk, int (*cmp)(void*, const void*, const void*)) {
    char* b = (char*)base;
    // heapsort: O(n log n), no recursion, no allocation
    for (size_t start = n / 2; start-- > 0;) {
        for (size_t root = start;;) {
            size_t child = 2 * root + 1;
            if (child >= n) break;
            if (child + 1 < n && cmp(thunk, b + child * size, b + (child + 1) * size) < 0) ++child;
            if (cmp(thunk, b + root * size, b + child * size) >= 0) break;
            swap_bytes(b + root * size, b + child * size, size); root = child;
        }
    }
    for (size_t end = n; end-- > 1;) {
        swap_bytes(b, b + end * size, size);
        for (size_t root = 0;;) {
            size_t child = 2 * root + 1;
            if (child >= end) break;
            if (child + 1 < end && cmp(thunk, b + child * size, b + (child + 1) * size) < 0) ++child;
            if (cmp(thunk, b + root * size, b + child * size) >= 0) break;
            swap_bytes(b + root * size, b + child * size, size); root = child;
        }
    }
}

// driconf's regexes (xmlconfig.c): no driconf files here; compiling fails, so nothing matches.
// Same contract as Mesa's own no-regex fallback (src/util/xmlconfig.c, MIT):
// compiling succeeds and nothing ever matches, so driconf ignores every
// application-specific section instead of applying all of them.
int regcomp(sf3_regex_t* r, const char* pattern, int flags) { (void)r; (void)pattern; (void)flags; return 0; }
int regexec(const sf3_regex_t* r, const char* s, size_t n, void* m, int flags) { (void)r; (void)s; (void)n; (void)m; (void)flags; return 1; /* REG_NOMATCH */ }
void regfree(sf3_regex_t* r) { (void)r; }
size_t regerror(int e, const sf3_regex_t* r, char* buf, size_t n) { (void)e; (void)r; if (n) buf[0] = 0; return 1; }
