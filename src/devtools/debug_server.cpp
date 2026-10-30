// See debug_server.h. Single-threaded: devtools_poll() runs on the emulation
// thread at VBlank-IN, accepts at most one client, and while the client holds
// the run paused it serves commands there, so the game never sees a half-done
// command (the same rule as --agent). A free-running client ("run") is polled
// without blocking, so the game keeps its speed.
#include <stdexcept>
#include "debug_server.h"
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <mutex>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>
#if !defined(MSG_NOSIGNAL)
#define MSG_NOSIGNAL 0
#endif

namespace {
volatile int g_throw_depth = 3;
[[gnu::noinline]] void dev_throw(int d = g_throw_depth) {
    if (d > 0) { dev_throw(d - 1); return; }
    throw std::runtime_error("devtools throw");
}
DevHooks H;
DevConfig C;
int g_listen = -1, g_client = -1;
bool g_authed = false, g_paused = false;
uint64_t g_until = 0;                    // paused client: pause again at this VBlank
std::string g_in;
std::mutex g_log_mu;
std::deque<std::string> g_log;
constexpr size_t kLogLines = 2000;

void drop() {
    if (g_client >= 0) close(g_client);
    g_client = -1, g_in.clear(), g_authed = false, g_paused = false;
}

bool send_all(const void* p, size_t n) {
    auto* c = static_cast<const char*>(p);
    while (n) {
        ssize_t k = ::send(g_client, c, n, MSG_NOSIGNAL);
        if (k < 0 && (errno == EAGAIN || errno == EINTR)) continue;
        if (k <= 0) { drop(); return false; }
        c += k, n -= size_t(k);
    }
    return true;
}
void say(const std::string& s) { std::string l = s + "\n"; send_all(l.data(), l.size()); }

// Whether fd has something to read (a client to accept, data, or the peer gone), not waiting.
// O_NONBLOCK does not take on a PS5 title's sockets: accept and recv waited anyway, holding the
// game at one VBlank per devtools command. poll with no timeout is asked first instead.
bool readable(int fd) {
    pollfd p{fd, POLLIN, 0};
    return ::poll(&p, 1, 0) > 0;
}

void set_blocking(int fd, bool on) {
    int f = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, on ? (f & ~O_NONBLOCK) : (f | O_NONBLOCK));
}

// 1: a line, 0: none yet (non-blocking), -1: client gone
int next_line(std::string& line, bool block) {
    for (;;) {
        size_t nl = g_in.find('\n');
        if (nl != std::string::npos) {
            line = g_in.substr(0, nl), g_in.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return 1;
        }
        if (!block && !readable(g_client)) return 0;
        set_blocking(g_client, block);
        char buf[4096];
        ssize_t n = ::recv(g_client, buf, sizeof buf, 0);
        if (n > 0) { g_in.append(buf, size_t(n)); continue; }
        if (n < 0 && !block && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
        if (n < 0 && errno == EINTR) continue;
        drop();
        return -1;
    }
}

void cmd_read(std::istringstream& a) {
    uint32_t addr = 0, len = 0;
    if (!(a >> std::hex >> addr >> std::dec >> len) || len > 0x100000) return say("error read ADDR LEN, LEN up to 1M");
    static const char k[] = "0123456789ABCDEF";
    std::string out = "data ";
    out.reserve(5 + 2 * len);
    for (uint32_t i = 0; i < len; ++i) { uint8_t b = H.ld8(addr + i); out += k[b >> 4]; out += k[b & 15]; }
    say(out);
}

void cmd_write(std::istringstream& a) {
    uint32_t addr = 0;
    std::string hex;
    if (!(a >> std::hex >> addr >> hex) || hex.size() % 2 ||
        hex.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) return say("error write ADDR HEX");
    for (size_t i = 0; i < hex.size(); i += 2) H.st8(addr + uint32_t(i / 2), uint8_t(std::stoul(hex.substr(i, 2), nullptr, 16)));
    say("ok");
}

void cmd_log(std::istringstream& a) {
    size_t n = 100;
    a >> n;
    std::lock_guard<std::mutex> l(g_log_mu);
    size_t from = g_log.size() > n ? g_log.size() - n : 0;
    say("log " + std::to_string(g_log.size() - from));
    for (size_t i = from; i < g_log.size(); ++i) say(g_log[i]);
}

// returns true when the emulation should go on (a step, run, detach)
bool serve(const std::string& line) {
    std::istringstream a(line);
    std::string cmd;
    a >> cmd;
    if (cmd.empty()) return false;
    if (!C.token.empty() && !g_authed) {
        std::string t;
        if (cmd == "auth" && (a >> t) && t == C.token) { g_authed = true; say("ok"); }
        else { say("error auth TOKEN first"); drop(); return true; }
        return false;
    }
    if (cmd == "ping") say("pong " + std::to_string(H.vblanks()));
    else if (cmd == "auth") say("ok");
    else if (cmd == "info") say("info " + (H.info ? H.info() : std::string()));
    else if (cmd == "pause") { g_paused = true, g_until = H.vblanks(); say("ok"); }
    else if (cmd == "step") {
        uint64_t n = 0;
        if (!(a >> n) || !n) { say("error step N, N at least 1"); return false; }
        g_paused = true, g_until = H.vblanks() + n;
        return true;                      // answered by the next "vblank N"
    } else if (cmd == "run") { g_paused = false; say("ok"); return true; }
    else if (cmd == "pad") {
        std::string spec, err;
        a >> spec;
        if (H.press(spec, err)) say("ok"); else say("error " + err);
    } else if (cmd == "read") cmd_read(a);
    else if (cmd == "write") cmd_write(a);
    else if (cmd == "frame") {
        uint32_t w = 0, h = 0;
        std::string rgb;
        if (!H.frame || !H.frame(w, h, rgb)) say("error no frame");
        else { say("frame " + std::to_string(w) + " " + std::to_string(h)); send_all(rgb.data(), rgb.size()); }
    } else if (cmd == "save" || cmd == "load") {
        std::string path, err;
        if (!(a >> path)) say("error " + cmd + " PATH");
        else if ((cmd == "save" ? H.save_state : H.load_state)(path, err)) say("ok");
        else say("error " + err);
    } else if (cmd == "saved") say("saved " + H.save_status());
    else if (cmd == "loaded") say("loaded " + (H.load_status ? H.load_status() : std::string("done 0")));
    else if (cmd == "log") cmd_log(a);
    else if (cmd == "throw") {   // a C++ exception on this thread: proves the unwinder works here
        try { dev_throw(); say("error not thrown"); }
        catch (const std::exception& e) { say(std::string("caught ") + e.what()); }
    } else if (cmd == "detach" || cmd == "quit") { say("bye"); drop(); return true; }   // quit never ends the game here
    else say("error no command " + cmd);
    return false;
}

void accept_one() {
    if (g_listen < 0 || g_client >= 0 || !readable(g_listen)) return;
    int fd = ::accept(g_listen, nullptr, nullptr);
    if (fd < 0) return;
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    // A frame is 230 KB and the send runs on the emulation thread: with the default send buffer it
    // waited for the peer's ACKs (~0.9 s per frame over Wi-Fi, a visible hitch). Let the kernel take it.
    int sndbuf = 2 << 20;
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof sndbuf);
    g_client = fd, g_paused = false;
    say("hello sf3dev 1");               // a fresh client starts free-running; "pause" or "step" takes over
}
}  // namespace

DevConfig devtools_read_config(const char* path) {
    DevConfig c;
    FILE* f = std::fopen(path, "r");
    if (!f) return c;
    c.enabled = true;
    char buf[512];
    while (std::fgets(buf, sizeof buf, f)) {
        std::string l(buf);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r' || l.back() == ' ')) l.pop_back();
        size_t eq = l.find('=');
        if (l.empty() || l[0] == '#' || eq == std::string::npos) continue;
        std::string k = l.substr(0, eq), v = l.substr(eq + 1);
        if (k == "port") c.port = std::atoi(v.c_str());
        else if (k == "token") c.token = v;
        else if (k == "bind") c.bind = v;
    }
    std::fclose(f);
    return c;
}

bool devtools_start(const DevConfig& cfg, const DevHooks& hooks) {
    C = cfg, H = hooks;
    if (!C.enabled) return false;
    g_listen = ::socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen < 0) {
        char m[64];
        std::snprintf(m, sizeof m, "devtools: socket() failed errno %d", errno);
        devtools_log(m);
        return false;
    }
    int one = 1;
    setsockopt(g_listen, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in sa{};
    sa.sin_family = AF_INET, sa.sin_port = htons(uint16_t(C.port));
    inet_pton(AF_INET, C.bind.c_str(), &sa.sin_addr);
    if (::bind(g_listen, (sockaddr*)&sa, sizeof sa) || ::listen(g_listen, 1)) {
        char m[96];
        std::snprintf(m, sizeof m, "devtools: bind/listen :%d failed errno %d", C.port, errno);
        devtools_log(m);
        close(g_listen), g_listen = -1;
        return false;
    }
    set_blocking(g_listen, false);
    char m[64];
    std::snprintf(m, sizeof m, "devtools: listening on :%d", C.port);
    devtools_log(m);
    return true;
}

void devtools_poll() {
    // The runtime polls devices thousands of times a frame. A socket call each time (accept, recv)
    // costs a syscall that on the PS5 is slow enough to drop the game to a VBlank every few seconds,
    // so free-running the socket is looked at once a VBlank; a paused client waits below as before.
    static uint64_t last = ~0ull;
    if (!g_paused) {
        uint64_t v = H.vblanks ? H.vblanks() : 0;
        if (v == last) return;
        last = v;
    }
    accept_one();
    if (g_client < 0) return;
    std::string line;
    if (!g_paused) {                      // free-running: serve whatever has arrived, never wait
        while (g_client >= 0 && !g_paused && next_line(line, false) == 1)
            if (serve(line) && g_paused) break;
        if (!g_paused || g_client < 0) return;
    }
    if (H.vblanks() < g_until) return;
    say("vblank " + std::to_string(H.vblanks()));
    while (g_client >= 0 && next_line(line, true) == 1)
        if (serve(line)) return;
}

void devtools_log(const char* line) {
    std::lock_guard<std::mutex> l(g_log_mu);
    g_log.emplace_back(line);
    if (g_log.size() > kLogLines) g_log.pop_front();
}

void devtools_stop() {
    drop();
    if (g_listen >= 0) close(g_listen), g_listen = -1;
}
