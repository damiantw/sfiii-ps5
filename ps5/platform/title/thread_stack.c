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
