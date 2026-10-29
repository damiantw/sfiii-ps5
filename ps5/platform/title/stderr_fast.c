// MIT, see LICENSE.
// stderr without the system's stdio. On the console a write to the system libc's stderr
// FILE takes 40-100 ms (its path is not our fd 2), and RADV logs a line from its submit path
// every 10 s: each was a visible hitch. The stdio calls in the executable are routed here
// with the linker's --wrap (tools/ps5/saturn_ps5.cmake); for stderr the text is formatted
// into a buffer and written to sf3_stderr_fd (a pipe that host_runtime.cpp drains into
// /data/sfiii/stderr.txt from a thread of its own). Other streams, or before the pipe is
// set up, go to the system functions.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int sf3_stderr_fd = -1;

int __real_vfprintf(FILE*, const char*, va_list);
int __real_fputs(const char*, FILE*);
size_t __real_fwrite(const void*, size_t, size_t, FILE*);
int __real_fputc(int, FILE*);
int __real_fflush(FILE*);

static int fast(FILE* f) { return f == stderr && sf3_stderr_fd >= 0; }

static int put(const char* p, size_t n) {
    size_t done = 0;
    while (done < n) {
        ssize_t w = write(sf3_stderr_fd, p + done, n - done);
        if (w <= 0) return -1;
        done += (size_t)w;
    }
    return (int)n;
}

int __wrap_vfprintf(FILE* f, const char* fmt, va_list ap) {
    if (!fast(f)) return __real_vfprintf(f, fmt, ap);
    char buf[2048];
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    if (n < 0) return n;
    return put(buf, (size_t)n < sizeof buf ? (size_t)n : sizeof buf - 1) < 0 ? -1 : n;
}

int __wrap_fprintf(FILE* f, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = __wrap_vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

int __wrap_fputs(const char* s, FILE* f) {
    if (!fast(f)) return __real_fputs(s, f);
    return put(s, strlen(s)) < 0 ? EOF : 0;
}

size_t __wrap_fwrite(const void* p, size_t size, size_t count, FILE* f) {
    if (!fast(f)) return __real_fwrite(p, size, count, f);
    return put((const char*)p, size * count) < 0 ? 0 : count;
}

int __wrap_fputc(int c, FILE* f) {
    if (!fast(f)) return __real_fputc(c, f);
    char ch = (char)c;
    return put(&ch, 1) < 0 ? EOF : (unsigned char)c;
}

int __wrap_fflush(FILE* f) {
    if (f && fast(f)) return 0;   // nothing is buffered
    return __real_fflush(f);
}
