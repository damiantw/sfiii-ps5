// Crash handler (every build: it writes a file, no network). On SIGSEGV/SIGBUS/SIGILL/SIGFPE/SIGABRT
// it writes the signal, fault address and a raw return-address walk to CRASH_DIR/crash-<time>.txt
// with only async-signal-safe calls, then re-raises. Symbolize on the box against the .sym file.
#include <csignal>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <initializer_list>
#include <unistd.h>

static char g_dir[256] = "/data/sfiii/crash";

static void put(int fd, const char* s) { ssize_t r = write(fd, s, strlen(s)); (void)r; }
static void put_hex(int fd, uintptr_t v) {
    char b[19] = "0x";
    for (int i = 0; i < 16; ++i) b[2 + i] = "0123456789abcdef"[(v >> (60 - 4 * i)) & 15];
    b[18] = 0;
    put(fd, b);
}

static void on_crash(int sig, siginfo_t* si, void*) {
    char path[320];
    char num[24];
    long t = (long)time(nullptr);
    int i = 23;
    num[i] = 0;
    do { num[--i] = char('0' + t % 10); t /= 10; } while (t && i);
    strcpy(path, g_dir), strcat(path, "/crash-"), strcat(path, num + i), strcat(path, ".txt");
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        put(fd, "signal "), put_hex(fd, (uintptr_t)sig), put(fd, "\nfault "), put_hex(fd, (uintptr_t)si->si_addr), put(fd, "\nframes\n");
        void** fp = (void**)__builtin_frame_address(0);
        for (int k = 0; k < 64 && fp && ((uintptr_t)fp & 7) == 0; ++k) {   // needs -fno-omit-frame-pointer
            put_hex(fd, (uintptr_t)fp[1]), put(fd, "\n");
            void** next = (void**)fp[0];
            if (next <= fp) break;
            fp = next;
        }
        close(fd);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

void crash_handler_install(const char* dir) {
    if (dir) strncpy(g_dir, dir, sizeof g_dir - 1);
    struct sigaction sa{};
    sa.sa_sigaction = on_crash;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    for (int s : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) sigaction(s, &sa, nullptr);
}
