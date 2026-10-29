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

// A sampling profiler for dev runs: with /data/sfiii/sample.enable present, a thread signals the
// thread that called sf3_sample_start every 500 ms for five minutes, and the handler writes that
// thread's rip, rbp and return address ("[sf3 sample]") to the kernel log, to symbolize later.
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
static pthread_t g_sampled;
static void on_sample(int sig, siginfo_t* si, void* ctx) {
    (void)sig, (void)si;
    const mcontext_t* m = &((const ucontext_t*)ctx)->uc_mcontext;
    const uintptr_t* sp = (const uintptr_t*)m->mc_rsp;
    s("[sf3 sample]"), reg("rip", m->mc_rip);
    int k = 0;   // the first words on the stack that point into the eboot's code: its callers
    for (int i = 0; i < 512 && k < 6; ++i)
        if (sp[i] >= 0x400000 && sp[i] < 0xcf0000) s(" "), x(sp[i]), ++k;
    flush();
}
static void* sampler(void* arg) {
    (void)arg;
    for (int i = 0; i < 600; ++i) { usleep(500000); pthread_kill(g_sampled, SIGUSR2); }
    return 0;
}
void sf3_sample_start(void) {
    int fd = open("/data/sfiii/sample.enable", O_RDONLY);
    if (fd < 0) return;
    close(fd);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_sample;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGUSR2, &sa, 0);
    g_sampled = pthread_self();
    pthread_t t;
    pthread_create(&t, 0, sampler, 0);
    s("[sf3 sample] started"), reg("on_sample", (uintptr_t)&on_sample), flush();
}
