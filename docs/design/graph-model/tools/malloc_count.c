/*
 * Counts heap allocations of a process (LD_PRELOAD shim, glibc).
 *
 *   clang -O2 -shared -fPIC -o /tmp/malloc_count.so docs/design/graph-model/tools/malloc_count.c -ldl
 *   LD_PRELOAD=/tmp/malloc_count.so camel x.cml std::rgir > /dev/null
 *
 * At exit it prints one line on stderr:
 *   [malloc_count] calls=<malloc+calloc+realloc+aligned> frees=<n> bytes=<total requested>
 *                  peak_live=<largest sum of usable sizes alive at once>
 *                  big_calls=<requests of 1 MiB or more> big_bytes=<their total>
 * calls, bytes and peak_live leave out the requests of 1 MiB or more: those are the segments
 * the compiler reserves for its own spaces (graph, permanent, meta, the collected heap), whose
 * use CAMEL_GC_STATS=1 reports. Sizes in peak_live are what malloc_usable_size reports.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <malloc.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void *(*real_malloc)(size_t);
static void *(*real_calloc)(size_t, size_t);
static void *(*real_realloc)(void *, size_t);
static void (*real_free)(void *);
static void *(*real_memalign)(size_t, size_t);
static int (*real_posix_memalign)(void **, size_t, size_t);
static void *(*real_aligned_alloc)(size_t, size_t);

static atomic_ullong calls, frees, bytes, big_calls, big_bytes;
#define BIG ((size_t)1 << 20)
static atomic_llong live, peak;

/* dlsym itself may call calloc before the real one is known: serve it from a small buffer. */
static char bootstrap[65536];
static size_t bootstrap_used;
static __thread int resolving;

static void resolve(void) {
    if (real_malloc || resolving) {
        return;
    }
    resolving = 1;
    real_malloc = dlsym(RTLD_NEXT, "malloc");
    real_calloc = dlsym(RTLD_NEXT, "calloc");
    real_realloc = dlsym(RTLD_NEXT, "realloc");
    real_free = dlsym(RTLD_NEXT, "free");
    real_memalign = dlsym(RTLD_NEXT, "memalign");
    real_posix_memalign = dlsym(RTLD_NEXT, "posix_memalign");
    real_aligned_alloc = dlsym(RTLD_NEXT, "aligned_alloc");
    resolving = 0;
}

static int from_bootstrap(void *p) {
    return (char *)p >= bootstrap && (char *)p < bootstrap + sizeof(bootstrap);
}

static void *bootstrap_alloc(size_t n) {
    size_t at = (bootstrap_used + 15) & ~(size_t)15;
    if (at + n > sizeof(bootstrap)) {
        return NULL;
    }
    bootstrap_used = at + n;
    return bootstrap + at;
}

static void note_alloc(void *p, size_t requested) {
    if (!p) {
        return;
    }
    if (requested >= BIG) {
        atomic_fetch_add(&big_calls, 1);
        atomic_fetch_add(&big_bytes, requested);
        return;
    }
    atomic_fetch_add(&calls, 1);
    atomic_fetch_add(&bytes, requested);
    long long now = atomic_fetch_add(&live, (long long)malloc_usable_size(p)) + (long long)malloc_usable_size(p);
    long long seen = atomic_load(&peak);
    while (now > seen && !atomic_compare_exchange_weak(&peak, &seen, now)) {
    }
}

static void note_free(void *p) {
    if (!p || malloc_usable_size(p) >= BIG) {
        return;
    }
    atomic_fetch_add(&frees, 1);
    atomic_fetch_sub(&live, (long long)malloc_usable_size(p));
}

void *malloc(size_t n) {
    resolve();
    if (!real_malloc) {
        return bootstrap_alloc(n);
    }
    void *p = real_malloc(n);
    note_alloc(p, n);
    return p;
}

void *calloc(size_t a, size_t b) {
    resolve();
    if (!real_calloc) {
        void *p = bootstrap_alloc(a * b);
        if (p) {
            memset(p, 0, a * b);
        }
        return p;
    }
    void *p = real_calloc(a, b);
    note_alloc(p, a * b);
    return p;
}

void *realloc(void *old, size_t n) {
    resolve();
    if (from_bootstrap(old)) {
        void *p = malloc(n);
        if (p && old) {
            memcpy(p, old, n);
        }
        return p;
    }
    note_free(old);
    void *p = real_realloc(old, n);
    note_alloc(p, n);
    return p;
}

void free(void *p) {
    resolve();
    if (!p || from_bootstrap(p) || !real_free) {
        return;
    }
    note_free(p);
    real_free(p);
}

void *memalign(size_t align, size_t n) {
    resolve();
    void *p = real_memalign(align, n);
    note_alloc(p, n);
    return p;
}

int posix_memalign(void **out, size_t align, size_t n) {
    resolve();
    int rc = real_posix_memalign(out, align, n);
    if (rc == 0) {
        note_alloc(*out, n);
    }
    return rc;
}

void *aligned_alloc(size_t align, size_t n) {
    resolve();
    void *p = real_aligned_alloc(align, n);
    note_alloc(p, n);
    return p;
}

__attribute__((destructor)) static void report(void) {
    char line[256];
    int len = snprintf(line, sizeof(line),
                       "[malloc_count] calls=%llu frees=%llu bytes=%llu peak_live=%lld "
                       "big_calls=%llu big_bytes=%llu\n",
                       (unsigned long long)atomic_load(&calls),
                       (unsigned long long)atomic_load(&frees),
                       (unsigned long long)atomic_load(&bytes), (long long)atomic_load(&peak),
                       (unsigned long long)atomic_load(&big_calls),
                       (unsigned long long)atomic_load(&big_bytes));
    if (len > 0) {
        (void)!write(2, line, (size_t)len);
    }
}
