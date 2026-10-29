// SPDX-License-Identifier: MIT
// Linked with --wrap=pthread_create: threads created without a stack size get 2 MiB, enough for
// RADV's shader compiler (ACO / NIR recursion), instead of the console's small default.
#include <pthread.h>
#define SF3_STACK (2u << 20)
int __real_pthread_create(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);
int __wrap_pthread_create(pthread_t* t, const pthread_attr_t* attr, void* (*fn)(void*), void* arg) {
    pthread_attr_t own;
    size_t size = 0;
    if (!attr) {
        pthread_attr_init(&own);
        pthread_attr_setstacksize(&own, SF3_STACK);
        int r = __real_pthread_create(t, &own, fn, arg);
        pthread_attr_destroy(&own);
        return r;
    }
    if (pthread_attr_getstacksize(attr, &size) == 0 && size < SF3_STACK)
        pthread_attr_setstacksize((pthread_attr_t*)attr, SF3_STACK);
    return __real_pthread_create(t, attr, fn, arg);
}

// thread_local destructors (libc++abi's __cxa_thread_atexit), which the console's libc does not
// export: a per-thread list run by a pthread key's destructor when the thread ends.
#include <stdlib.h>
struct sf3_tls_dtor { void (*fn)(void*); void* obj; struct sf3_tls_dtor* next; };
static pthread_key_t sf3_tls_key;
static pthread_once_t sf3_tls_once = PTHREAD_ONCE_INIT;
static void sf3_tls_run(void* p) {
    struct sf3_tls_dtor* d = (struct sf3_tls_dtor*)p;
    while (d) {   // last registered first; a destructor may register more
        pthread_setspecific(sf3_tls_key, d->next);
        d->fn(d->obj);
        free(d);
        d = (struct sf3_tls_dtor*)pthread_getspecific(sf3_tls_key);
    }
}
static void sf3_tls_init(void) { pthread_key_create(&sf3_tls_key, sf3_tls_run); }
int __cxa_thread_atexit_impl(void (*fn)(void*), void* obj, void* dso) {
    (void)dso;
    pthread_once(&sf3_tls_once, sf3_tls_init);
    struct sf3_tls_dtor* d = (struct sf3_tls_dtor*)malloc(sizeof *d);
    if (!d) return -1;
    d->fn = fn, d->obj = obj, d->next = (struct sf3_tls_dtor*)pthread_getspecific(sf3_tls_key);
    pthread_setspecific(sf3_tls_key, d);
    return 0;
}
