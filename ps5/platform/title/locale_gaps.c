// The FreeBSD xlocale (*_l) and message-catalog calls libc++'s locale code links against, which
// the console's libc does not export. The console has only the C locale, so each _l call is the
// plain one, and mbsnrtowcs/wcsnrtombs are written out on mbrtowc/wcrtomb. MIT, see LICENSE.
#include <errno.h>
#include <limits.h>
#include <runetype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <xlocale.h>

typedef void* sf3_nl_catd;

_RuneLocale* __runes_for_locale(locale_t l, int* mb_sole_max) {
    (void)l;
    if (mb_sole_max) *mb_sole_max = 1;
    return (_RuneLocale*)&_DefaultRuneLocale;
}
int ___mb_cur_max_l(locale_t l) { (void)l; return __mb_cur_max; }

long long strtoll_l(const char* s, char** e, int b, locale_t l) { (void)l; return strtoll(s, e, b); }
unsigned long long strtoull_l(const char* s, char** e, int b, locale_t l) { (void)l; return strtoull(s, e, b); }
long double strtold_l(const char* s, char** e, locale_t l) { (void)l; return strtold(s, e); }
size_t strftime_l(char* s, size_t n, const char* f, const struct tm* t, locale_t l) { (void)l; return strftime(s, n, f, t); }
size_t wcrtomb_l(char* s, wchar_t w, mbstate_t* ps, locale_t l) { (void)l; return wcrtomb(s, w, ps); }
size_t mbrtowc_l(wchar_t* w, const char* s, size_t n, mbstate_t* ps, locale_t l) { (void)l; return mbrtowc(w, s, n, ps); }
size_t mbrlen_l(const char* s, size_t n, mbstate_t* ps, locale_t l) { (void)l; return mbrlen(s, n, ps); }
int mbtowc_l(wchar_t* w, const char* s, size_t n, locale_t l) { (void)l; return mbtowc(w, s, n); }
size_t mbsrtowcs_l(wchar_t* d, const char** s, size_t n, mbstate_t* ps, locale_t l) { (void)l; return mbsrtowcs(d, s, n, ps); }

size_t mbsnrtowcs(wchar_t* dst, const char** src, size_t nms, size_t len, mbstate_t* ps) {
    static mbstate_t own;
    if (!ps) ps = &own;
    const char* s = *src;
    size_t n = 0;
    wchar_t w;
    while (dst ? n < len : 1) {
        size_t r = mbrtowc(&w, s, nms, ps);
        if (r == (size_t)-1) { if (dst) *src = s; errno = EILSEQ; return (size_t)-1; }
        if (r == (size_t)-2) { s += nms; break; }   // incomplete at the end of the nms bytes
        if (dst) dst[n] = w;
        if (r == 0) { if (dst) *src = NULL; return n; }
        s += r, nms -= r, n++;
    }
    if (dst) *src = s;
    return n;
}

size_t wcsnrtombs(char* dst, const wchar_t** src, size_t nwc, size_t len, mbstate_t* ps) {
    static mbstate_t own;
    if (!ps) ps = &own;
    const wchar_t* s = *src;
    size_t n = 0;
    char buf[MB_LEN_MAX];
    while (nwc--) {
        mbstate_t save = *ps;
        size_t r = wcrtomb(buf, *s, ps);
        if (r == (size_t)-1) { if (dst) *src = s; errno = EILSEQ; return (size_t)-1; }
        if (dst) {
            if (n + r > len) { *ps = save; break; }
            memcpy(dst + n, buf, r);
        }
        if (*s == L'\0') { if (dst) *src = NULL; return n + r - 1; }
        n += r, s++;
    }
    if (dst) *src = s;
    return n;
}
size_t mbsnrtowcs_l(wchar_t* d, const char** s, size_t nms, size_t n, mbstate_t* ps, locale_t l) { (void)l; return mbsnrtowcs(d, s, nms, n, ps); }
size_t wcsnrtombs_l(char* d, const wchar_t** s, size_t nwc, size_t n, mbstate_t* ps, locale_t l) { (void)l; return wcsnrtombs(d, s, nwc, n, ps); }

int snprintf_l(char* s, size_t n, locale_t l, const char* f, ...) {
    (void)l; va_list a; va_start(a, f); int r = vsnprintf(s, n, f, a); va_end(a); return r;
}
int asprintf_l(char** s, locale_t l, const char* f, ...) {
    (void)l; va_list a; va_start(a, f); int r = vasprintf(s, f, a); va_end(a); return r;
}
int sscanf_l(const char* s, locale_t l, const char* f, ...) {
    (void)l; va_list a; va_start(a, f); int r = vsscanf(s, f, a); va_end(a); return r;
}

// No message catalogs: libc++'s std::messages gets "not open", catgets returns the default.
sf3_nl_catd catopen(const char* name, int flag) { (void)name; (void)flag; errno = ENOENT; return (sf3_nl_catd)-1; }
char* catgets(sf3_nl_catd c, int set, int msg, const char* def) { (void)c; (void)set; (void)msg; return (char*)def; }
int catclose(sf3_nl_catd c) { (void)c; return 0; }
