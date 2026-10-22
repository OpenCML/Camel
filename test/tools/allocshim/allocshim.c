// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * allocshim: a counting allocator shim for measuring a Camel run (Linux, glibc).
 *
 *   cc -O2 -fPIC -shared -o allocshim.so allocshim.c
 *   LD_PRELOAD=./allocshim.so camel --time-phases report.json prog.cml std::fvm
 *
 * It interposes malloc, calloc, realloc, free and the aligned variants, forwards each call to
 * glibc (`__libc_malloc` and friends, so nothing is looked up at run time and nothing can
 * recurse), and keeps five totals:
 *
 *   allocations   calls that returned memory
 *   bytes         bytes those calls asked for
 *   frees         calls of free with a non-null pointer
 *   live_bytes    usable bytes handed out and not yet freed (malloc_usable_size)
 *   peak_bytes    the highest live_bytes seen
 *
 * `camel_allocshim_counters` copies them out. The phase recorder of libcamel
 * (camel/utils/phase_timer.h) finds that symbol when the shim is preloaded and adds the
 * difference over each phase to its report; nothing in Camel links against this file.
 *
 * C++ `operator new` and `operator delete` of libstdc++ call malloc and free, so they are
 * counted. Memory that Camel takes from the system directly (the memory spaces of
 * camel/core/mm, which reserve address space with mmap) is not heap and is not counted here:
 * the peak resident set size of the report covers it.
 *
 * Where the allocations come from. With CAMEL_ALLOCSHIM_SITES=<file> every N-th allocation
 * (CAMEL_ALLOCSHIM_EVERY, default 97) records the return addresses of its callers; at exit
 * the samples and the memory map of the process are written to <file>, and
 * benchmarks/compile/hotspots.py turns them into tables by function. Sampling costs time (an
 * unwind per sample); do not read wall-clock times from such a run.
 *
 * Where the time goes. With CAMEL_ALLOCSHIM_CPU=<file> the shim is a sampling profiler
 * instead: a CPU-time timer (CAMEL_ALLOCSHIM_HZ samples per second of CPU time, default 1000)
 * interrupts the process and the handler records the stack in the same format. This is what
 * `perf record -g` would give where perf is not available. The two kinds of sampling are not
 * combined: when both variables are set, the CPU profile wins. Nothing is counted during a
 * CPU profile (the totals stay zero), so that the profile shows the allocator and not the
 * bookkeeping of this file.
 *
 * The counters are atomics and the shim is safe to use from several threads. It is a
 * measuring tool for the benchmark scripts and the tests of this repository, not a part of
 * the product.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <malloc.h>
#include <signal.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

extern void *__libc_malloc(size_t size);
extern void *__libc_calloc(size_t count, size_t size);
extern void *__libc_realloc(void *ptr, size_t size);
extern void *__libc_memalign(size_t alignment, size_t size);
extern void __libc_free(void *ptr);

/* The layout libcamel reads (AllocCounters in src/utils/phase_timer.cpp). */
struct camel_allocshim_totals {
    uint64_t allocations;
    uint64_t bytes;
    uint64_t frees;
    uint64_t live_bytes;
    uint64_t peak_bytes;
};

static atomic_uint_fast64_t g_allocations;
static atomic_uint_fast64_t g_bytes;
static atomic_uint_fast64_t g_frees;
static atomic_uint_fast64_t g_live;
static atomic_uint_fast64_t g_peak;

/* ---- call-site sampling ---------------------------------------------------------------- */

enum { SITE_FRAMES = 48, SITE_CAPACITY = 1 << 16 };

struct site_sample {
    uint32_t depth;
    uint32_t size;
    void *frames[SITE_FRAMES];
};

static const char *g_sites_path;         /* NULL: sampling is off */
static int g_cpu_profile;                /* samples are taken by the timer, not by allocations */
static uint64_t g_sites_every = 97;      /* one sample per this many allocations */
static struct site_sample *g_sites;      /* SITE_CAPACITY samples, taken from glibc directly */
static atomic_uint_fast64_t g_sites_count;
static __thread int t_in_shim;           /* set while the shim itself calls into glibc */

static void sample_site(size_t size, uint64_t ordinal) {
    if (g_sites == NULL || g_cpu_profile || ordinal % g_sites_every != 0 || t_in_shim) {
        return;
    }
    t_in_shim = 1; /* backtrace() may allocate (it loads the unwinder on its first call) */
    const uint64_t slot = atomic_fetch_add(&g_sites_count, 1);
    if (slot < SITE_CAPACITY) {
        struct site_sample *sample = &g_sites[slot];
        sample->size               = size > UINT32_MAX ? UINT32_MAX : (uint32_t)size;
        sample->depth              = (uint32_t)backtrace(sample->frames, SITE_FRAMES);
    }
    t_in_shim = 0;
}

/* The handler of the profiling timer. backtrace() is not on the list of functions a signal
 * handler may call; it is safe here because it was called once before the timer started (so
 * it has loaded what it loads lazily) and because it only reads the stack afterwards. The
 * first frames of a sample are the handler and the signal trampoline; hotspots.py drops them. */
static void on_profile_tick(int signal_number) {
    (void)signal_number;
    const int saved_errno = errno;
    const uint64_t slot   = atomic_fetch_add(&g_sites_count, 1);
    if (slot < SITE_CAPACITY) {
        struct site_sample *sample = &g_sites[slot];
        sample->size               = 0;
        sample->depth              = (uint32_t)backtrace(sample->frames, SITE_FRAMES);
    }
    errno = saved_errno;
}

static void start_cpu_profile(uint64_t hertz) {
    void *prime[4];
    backtrace(prime, 4);
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_handler = on_profile_tick;
    action.sa_flags   = SA_RESTART;
    sigaction(SIGPROF, &action, NULL);
    struct itimerval timer;
    timer.it_interval.tv_sec  = 0;
    timer.it_interval.tv_usec = (suseconds_t)(1000000 / hertz);
    timer.it_value            = timer.it_interval;
    setitimer(ITIMER_PROF, &timer, NULL);
}

static void copy_file(int out, const char *path) {
    char buffer[4096];
    const int in = open(path, O_RDONLY);
    if (in < 0) {
        return;
    }
    for (ssize_t n; (n = read(in, buffer, sizeof buffer)) > 0;) {
        if (write(out, buffer, (size_t)n) != n) {
            break;
        }
    }
    close(in);
}

/* The samples as text: the memory map, a separator, then one line per sample: the size and
 * the return addresses, innermost first. Addresses are resolved against the map offline. */
__attribute__((destructor)) static void write_sites(void) {
    if (g_sites == NULL || g_sites_path == NULL) {
        return;
    }
    if (g_cpu_profile) {
        struct itimerval off;
        memset(&off, 0, sizeof off);
        setitimer(ITIMER_PROF, &off, NULL);
    }
    t_in_shim     = 1;
    const int out = open(g_sites_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) {
        return;
    }
    copy_file(out, "/proc/self/maps");
    dprintf(out, "--- samples kind=%s every=%llu\n", g_cpu_profile ? "cpu" : "alloc",
            (unsigned long long)g_sites_every);
    uint64_t count = atomic_load(&g_sites_count);
    if (count > SITE_CAPACITY) {
        count = SITE_CAPACITY;
    }
    for (uint64_t i = 0; i < count; ++i) {
        const struct site_sample *sample = &g_sites[i];
        dprintf(out, "%u", sample->size);
        for (uint32_t k = 0; k < sample->depth; ++k) {
            dprintf(out, " %p", sample->frames[k]);
        }
        dprintf(out, "\n");
    }
    close(out);
}

__attribute__((constructor)) static void read_settings(void) {
    const char *cpu  = getenv("CAMEL_ALLOCSHIM_CPU");
    const char *path = cpu != NULL && *cpu != '\0' ? cpu : getenv("CAMEL_ALLOCSHIM_SITES");
    if (path == NULL || *path == '\0') {
        return;
    }
    g_cpu_profile     = path == cpu;
    const char *every = getenv(g_cpu_profile ? "CAMEL_ALLOCSHIM_HZ" : "CAMEL_ALLOCSHIM_EVERY");
    if (every != NULL && atoll(every) > 0) {
        g_sites_every = (uint64_t)atoll(every);
    } else if (g_cpu_profile) {
        g_sites_every = 1000;
    }
    t_in_shim    = 1;
    g_sites_path = path;
    g_sites      = __libc_calloc(SITE_CAPACITY, sizeof(struct site_sample));
    t_in_shim    = 0;
    if (g_cpu_profile && g_sites != NULL) {
        start_cpu_profile(g_sites_every);
    }
}

/* ---- counting -------------------------------------------------------------------------- */

static void note_alloc(void *ptr, size_t requested) {
    if (g_cpu_profile) {
        return; /* the profile should show the allocator, not this bookkeeping */
    }
    const uint64_t ordinal = atomic_fetch_add_explicit(&g_allocations, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&g_bytes, requested, memory_order_relaxed);
    const uint64_t live =
        atomic_fetch_add_explicit(&g_live, malloc_usable_size(ptr), memory_order_relaxed) +
        malloc_usable_size(ptr);
    uint64_t peak = atomic_load_explicit(&g_peak, memory_order_relaxed);
    while (live > peak &&
           !atomic_compare_exchange_weak_explicit(
               &g_peak, &peak, live, memory_order_relaxed, memory_order_relaxed)) {
    }
    sample_site(requested, ordinal);
}

static void note_free(void *ptr) {
    if (g_cpu_profile) {
        return;
    }
    atomic_fetch_add_explicit(&g_frees, 1, memory_order_relaxed);
    atomic_fetch_sub_explicit(&g_live, malloc_usable_size(ptr), memory_order_relaxed);
}

void camel_allocshim_counters(struct camel_allocshim_totals *out) {
    out->allocations = atomic_load(&g_allocations);
    out->bytes       = atomic_load(&g_bytes);
    out->frees       = atomic_load(&g_frees);
    out->live_bytes  = atomic_load(&g_live);
    out->peak_bytes  = atomic_load(&g_peak);
}

/* ---- the interposed functions ---------------------------------------------------------- */

void *malloc(size_t size) {
    void *ptr = __libc_malloc(size);
    if (ptr != NULL) {
        note_alloc(ptr, size);
    }
    return ptr;
}

void *calloc(size_t count, size_t size) {
    void *ptr = __libc_calloc(count, size);
    if (ptr != NULL) {
        note_alloc(ptr, count * size);
    }
    return ptr;
}

/* A realloc counts as an allocation of the new size and a free of the old block, so that
 * allocations - frees stays the number of live blocks. */
void *realloc(void *old, size_t size) {
    if (old == NULL) {
        return malloc(size);
    }
    if (size == 0) {
        free(old);
        return NULL;
    }
    const size_t before = malloc_usable_size(old);
    void *ptr           = __libc_realloc(old, size);
    if (ptr != NULL && !g_cpu_profile) {
        atomic_fetch_add_explicit(&g_frees, 1, memory_order_relaxed);
        atomic_fetch_sub_explicit(&g_live, before, memory_order_relaxed);
        note_alloc(ptr, size);
    }
    return ptr;
}

void free(void *ptr) {
    if (ptr != NULL) {
        note_free(ptr);
        __libc_free(ptr);
    }
}

void *memalign(size_t alignment, size_t size) {
    void *ptr = __libc_memalign(alignment, size);
    if (ptr != NULL) {
        note_alloc(ptr, size);
    }
    return ptr;
}

void *aligned_alloc(size_t alignment, size_t size) { return memalign(alignment, size); }

int posix_memalign(void **out, size_t alignment, size_t size) {
    if (alignment % sizeof(void *) != 0 || (alignment & (alignment - 1)) != 0 || alignment == 0) {
        return EINVAL;
    }
    void *ptr = memalign(alignment, size);
    if (ptr == NULL) {
        return ENOMEM;
    }
    *out = ptr;
    return 0;
}
