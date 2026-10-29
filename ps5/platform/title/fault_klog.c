// A title's fault handler: on SIGSEGV/SIGBUS/SIGILL/SIGFPE/SIGABRT it writes the signal, the
// registers, 32 stack words and the load addresses (eboot, companion libc.prx, the generated
// code) to the kernel log with sceKernelDebugOutText, then re-raises. Formatting is done by hand
// (no printf in a signal handler). MIT, see LICENSE.
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <ucontext.h>

int sceKernelDebugOutText(int channel, const char* text);
void* malloc(unsigned long);
int sceKernelAvailableFlexibleMemorySize(unsigned long* size);
unsigned long sf3_heap_committed(void), sf3_heap_used(void);   // heap.c

// set by the code-cache loader (loader_ps5.cpp)
volatile uintptr_t sf3_fault_gen_base, sf3_fault_gen_size;

static char g_line[256];
static int g_n;

static void s(const char* t) { while (*t && g_n < (int)sizeof g_line - 2) g_line[g_n++] = *t++; }
static void x(uintptr_t v) {
    s("0x");
    for (int i = 0; i < 16; ++i) { char c[2] = {"0123456789abcdef"[(v >> (60 - 4 * i)) & 15], 0}; s(c); }
}
static void flush(void) { g_line[g_n++] = '\n'; g_line[g_n] = 0; sceKernelDebugOutText(0, g_line); g_n = 0; }
static void reg(const char* name, uintptr_t v) { s(" "), s(name), s("="), x(v); }

// Free flexible memory (the budget param.json sets; the C heap grows from it) to the kernel log.
void sf3_fault_memory(const char* tag) {
    unsigned long fm = 0;
    int r = sceKernelAvailableFlexibleMemorySize(&fm);
    s("[sf3 mem] "), s(tag), s(": flexible free "), x(fm), s(" ret "), x((uintptr_t)(unsigned)r);
    s(" heap used "), x(sf3_heap_used()), s(" of "), x(sf3_heap_committed()), flush();
}

static void on_fault(int sig, siginfo_t* si, void* ctx) {
    const mcontext_t* m = &((const ucontext_t*)ctx)->uc_mcontext;
    s("[sf3 fault] signal "), x((uintptr_t)sig), s(" addr "), x((uintptr_t)si->si_addr), flush();
    s("[sf3 fault]"), reg("rip", m->mc_rip), reg("rsp", m->mc_rsp), reg("rbp", m->mc_rbp), flush();
    s("[sf3 fault]"), reg("rax", m->mc_rax), reg("rbx", m->mc_rbx), reg("rcx", m->mc_rcx), flush();
    s("[sf3 fault]"), reg("rdx", m->mc_rdx), reg("rsi", m->mc_rsi), reg("rdi", m->mc_rdi), flush();
    s("[sf3 fault]"), reg("r8", m->mc_r8), reg("r9", m->mc_r9), reg("r10", m->mc_r10), reg("r11", m->mc_r11), flush();
    s("[sf3 fault]"), reg("r12", m->mc_r12), reg("r13", m->mc_r13), reg("r14", m->mc_r14), reg("r15", m->mc_r15), flush();
    s("[sf3 fault] eboot on_fault="), x((uintptr_t)&on_fault), s(" companion malloc="), x((uintptr_t)&malloc), flush();
    s("[sf3 fault] generated "), x(sf3_fault_gen_base), s(" size "), x(sf3_fault_gen_size), flush();
    sf3_fault_memory("at fault");
    const uintptr_t* sp = (const uintptr_t*)m->mc_rsp;
    for (int i = 0; i < 32 && sp; i += 4) {
        s("[sf3 fault] rsp+"), x((uintptr_t)i * 8), s(":");
        for (int k = 0; k < 4; ++k) s(" "), x(sp[i + k]);
        flush();
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

void sf3_fault_install(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_fault;
    sa.sa_flags = SA_SIGINFO;
    int sigs[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
    for (unsigned i = 0; i < sizeof sigs / sizeof *sigs; ++i) sigaction(sigs[i], &sa, 0);
    sceKernelDebugOutText(0, "[sf3 fault] handler installed (build " __DATE__ " " __TIME__ ")\n");
    sf3_fault_memory("start");
}

// A sampling profiler for dev runs, enabled by /data/sfiii/sample.enable.
// - Empty file (or other text): every 500 ms for five minutes the handler writes the sampled
//   thread's rip and callers ("[sf3 sample]") to the kernel log, to symbolize later.
// - "perf" in the file: 1000 samples a second, classified as generated x64 code (the code-cache
//   ranges below), eboot code (runtime, host), waiting (the host's pace/present wait, flagged by
//   sf3_perf_wait) or other (system libraries, kernel). Every 10 s a "[sf3 perf]" summary with the
//   frame times (sf3_perf_frame) goes to the kernel log and /data/sfiii/perf.txt.
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <time.h>
static pthread_t g_sampled;
static int g_perf, g_perf_file, g_perf_frames_only;
#define GEN_MAX 256
static volatile uintptr_t g_gen_b[GEN_MAX], g_gen_e[GEN_MAX];
void sf3_gen_range(uintptr_t base, uintptr_t size, int add) {
    for (int i = 0; i < GEN_MAX; ++i) {
        if (add && !g_gen_e[i]) { g_gen_b[i] = base, g_gen_e[i] = base + size; return; }
        if (!add && g_gen_b[i] == base) { g_gen_e[i] = 0, g_gen_b[i] = 0; return; }
    }
}
volatile int sf3_perf_wait;   // set by the host while it waits for the frame's time or the display
static volatile unsigned long g_n_gen, g_n_eboot, g_n_wait, g_n_other;
static volatile unsigned long g_hist[5], g_present_ns, g_present_last, g_presents;
extern unsigned long sf3_display_acquire_ns, sf3_display_draw_ns, sf3_display_queue_ns;
static volatile unsigned long g_spike[8][6], g_nspike;   // work, present, frame of frames over 25 ms
void sf3_perf_present(unsigned long ns) { g_present_ns += ns, g_present_last = ns, g_presents++; }
static volatile unsigned long g_frames, g_work_ns, g_frame_ns, g_frame_max, g_over;
void sf3_perf_frame(unsigned long work_ns, unsigned long frame_ns) {
    g_frames++, g_work_ns += work_ns, g_frame_ns += frame_ns;
    if (frame_ns > g_frame_max) g_frame_max = frame_ns;
    if (frame_ns > 17500000ul) g_over++;
    if (frame_ns > 25000000ul && g_nspike < 8)
        g_spike[g_nspike][0] = work_ns, g_spike[g_nspike][1] = g_present_last, g_spike[g_nspike][2] = frame_ns, g_spike[g_nspike][3] = sf3_display_acquire_ns,
        g_spike[g_nspike][4] = sf3_display_draw_ns, g_spike[g_nspike][5] = sf3_display_queue_ns, g_nspike++;
    g_present_last = 0;
    g_hist[frame_ns < 15000000ul ? 0 : frame_ns <= 17500000ul ? 1 : frame_ns <= 25000000ul ? 2 : frame_ns <= 34000000ul ? 3 : 4]++;
}
extern char __sf3_text_start[], __sf3_text_end[];   // ps5-title.ld
static void on_sample(int sig, siginfo_t* si, void* ctx) {
    (void)sig, (void)si;
    const mcontext_t* m = &((const ucontext_t*)ctx)->uc_mcontext;
    uintptr_t rip = m->mc_rip;
    if (g_perf) {
        if (sf3_perf_wait) { g_n_wait++; return; }
        for (int i = 0; i < GEN_MAX; ++i)
            if (rip >= g_gen_b[i] && rip < g_gen_e[i]) { g_n_gen++; return; }
        uintptr_t lo = (uintptr_t)__sf3_text_start, hi = (uintptr_t)__sf3_text_end;
        if (rip >= lo && rip < hi) g_n_eboot++; else g_n_other++;
        return;
    }
    const uintptr_t* sp = (const uintptr_t*)m->mc_rsp;
    s("[sf3 sample]"), reg("rip", rip);
    int k = 0;   // the first words on the stack that point into the eboot's code: its callers
    for (int i = 0; i < 512 && k < 6; ++i)
        if (sp[i] >= 0x400000 && sp[i] < 0xcf0000) s(" "), x(sp[i]), ++k;
    flush();
}
static void perf_report(void) {
    unsigned long g = g_n_gen, e = g_n_eboot, w = g_n_wait, o = g_n_other, f = g_frames;
    unsigned long wk = g_work_ns, fr = g_frame_ns, mx = g_frame_max, ov = g_over;
    g_n_gen = g_n_eboot = g_n_wait = g_n_other = 0, g_frames = g_work_ns = g_frame_ns = g_frame_max = g_over = 0;
    unsigned long t = g + e + w + o, busy = g + e + o;
    char line[1024];
    snprintf(line, sizeof line,
             "[sf3 perf] %lu frames, frame %.2f ms avg %.2f max, work %.2f ms avg, %lu over 17.5 ms; "
             "samples %lu: x64 %.1f%% eboot %.1f%% other %.1f%% wait %.1f%%; x64 share of busy %.1f%%\n",
             f, f ? fr / 1e6 / f : 0.0, mx / 1e6, f ? wk / 1e6 / f : 0.0, ov, t,
             t ? 100.0 * g / t : 0.0, t ? 100.0 * e / t : 0.0, t ? 100.0 * o / t : 0.0, t ? 100.0 * w / t : 0.0,
             busy ? 100.0 * g / busy : 0.0);
    sceKernelDebugOutText(0, line);
    {
        char h[160];
        snprintf(h, sizeof h, "[sf3 perf] frame ms histogram: <15 %lu, 15-17.5 %lu, 17.5-25 %lu, 25-34 %lu, >34 %lu\n",
                 g_hist[0], g_hist[1], g_hist[2], g_hist[3], g_hist[4]);
        g_hist[0] = g_hist[1] = g_hist[2] = g_hist[3] = g_hist[4] = 0;
        char q[400];
        int k = snprintf(q, sizeof q, "[sf3 perf] present %.2f ms avg over %lu; spikes (work/present/frame ms [acquire/draw/queue]):",
                         g_presents ? g_present_ns / 1e6 / g_presents : 0.0, g_presents);
        for (unsigned long j = 0; j < g_nspike && k < 300; ++j)
            k += snprintf(q + k, sizeof q - k, " %.1f/%.1f/%.1f[%.1f/%.1f/%.1f]", g_spike[j][0] / 1e6, g_spike[j][1] / 1e6, g_spike[j][2] / 1e6,
                          g_spike[j][3] / 1e6, g_spike[j][4] / 1e6, g_spike[j][5] / 1e6);
        snprintf(q + k, sizeof q - k, "\n");
        g_present_ns = g_presents = g_nspike = 0;
        sceKernelDebugOutText(0, q);
        size_t n = strlen(line);
        if (n + strlen(h) < sizeof line) strcpy(line + n, h);
        n = strlen(line);
        if (n + strlen(q) < sizeof line) strcpy(line + n, q);
        sceKernelDebugOutText(0, h);
    }
    if (!g_perf_file) return;   // "perf file": also append to perf.txt (a /data write every 10 s)
    FILE* fp = fopen("/data/sfiii/perf.txt", "a");
    if (fp) fputs(line, fp), fclose(fp);
}
static void* sampler(void* arg) {
    (void)arg;
    if (g_perf) {
        for (unsigned long i = 1;; ++i) {
            usleep(1000);
            if (!g_perf_frames_only) pthread_kill(g_sampled, SIGUSR2);
            if (i % 7000 == 0) perf_report();
        }
    }
    for (int i = 0; i < 600; ++i) { usleep(500000); pthread_kill(g_sampled, SIGUSR2); }
    return 0;
}
void sf3_sample_start(void) {
    int fd = open("/data/sfiii/sample.enable", O_RDONLY);
    if (fd < 0) return;
    char buf[16] = {0};
    if (read(fd, buf, sizeof buf - 1) > 0 && strstr(buf, "perf")) g_perf = 1, g_perf_file = strstr(buf, "file") != NULL,
        g_perf_frames_only = strstr(buf, "frames") != NULL;   // "perf frames": frame times only, no sampling
    close(fd);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_sample;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGUSR2, &sa, 0);
    g_sampled = pthread_self();
    pthread_t t;
    pthread_create(&t, 0, sampler, 0);
    s(g_perf ? "[sf3 perf] started" : "[sf3 sample] started"), reg("on_sample", (uintptr_t)&on_sample), flush();
}
