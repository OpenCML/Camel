// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Measures the canonical GEMM (modules/tensor/kernels/gemm_blocked.inl) at the shapes a step of
 * examples/models/naive-in-expert-out issues, and says where the time goes.
 *
 * It is a translation unit of its own: it defines CAMEL_GEMM_PROBE, instantiates the AVX2 unit
 * (8-wide, 6 x 16) from the same file the build compiles, and supplies the two hook functions the
 * file calls under that macro. Nothing in the Camel build defines the macro, so a release unit is
 * unchanged. The worker pool is the real one (kernels/parallel.cpp), so a run with more than one
 * thread measures the same region and the same handoff a program sees.
 *
 * The host is shared, so every duration here is CPU time of the measuring thread
 * (CLOCK_THREAD_CPUTIME_ID), not wall time: a preempted sample changes the wall clock and not the
 * work the kernel did. The wall median and the host's load average are printed next to it, so a
 * reader can see how quiet the window was. GFLOP/s is 2*M*N*K over CPU seconds.
 *
 * Per shape:
 *
 *   total      the entry call, end to end
 *   setup      the worker/tile computation before the region
 *   region     the parallelFor call (on the calling thread)
 *   chunk      the sum of the chunk bodies' durations across workers
 *   packB/A    the packings, summed over tiles and K blocks (with bytes moved)
 *   micro      the full (mr == 6, nr == 16) and edge micro tiles, with arithmetic
 *   residual   chunk - (packB + packA + micro), the loop bookkeeping inside a chunk
 *   join       region * workers - chunk, the time a worker spent not in a chunk
 *
 * and, as the denominators of the percentage:
 *
 *   micro_ceiling  the same micro kernel over packed blocks in L1: the inner loop's own rate
 *   peak           the core's FMA peak: independent vfloat accumulators and two operands
 *
 * plus two comparators at the same shape on the same core: a naive i-j-k triple loop in the
 * canonical order (what blocking and packing buy) and the system OpenBLAS at one thread (what a
 * tuned kernel does). Both are loaded at run time, so the probe builds with no BLAS on its link
 * line.
 *
 * Build and run on the build host (never on the workstation):
 *
 *   sh test/tools/gemmprobe/run.sh
 *   sh test/tools/gemmprobe/run.sh --threads 1,8 --reps 7
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <thread>
#include <string>
#include <vector>

#include <dlfcn.h>
#include <immintrin.h>

namespace camel::tensor::kernels {
// parallel.cpp calls this when the pool is resized; the probe has no library GEMM to size.
void setGemmThreads(int) {}
} // namespace camel::tensor::kernels

// The kernel before the band-major change had no hooks: the A/B build (CAMEL_GEMM_PROBE_OLD)
// supplies the phase names the probe's counters use, and the call-level CPU time, which is what
// the comparison needs, does not depend on them.
#ifdef CAMEL_GEMM_PROBE_OLD
namespace camel::tensor::kernels::gemmprobe {
enum Phase {
    kSetup,
    kPackB,
    kPackA,
    kMicroFull,
    kMicroEdge,
    kChunk,
    kRegion,
    kTotal,
    kPhaseCount
};
} // namespace camel::tensor::kernels::gemmprobe
#endif

#define CAMEL_GEMM_VEC_WIDTH 8
#define CAMEL_GEMM_MR 6
#define CAMEL_GEMM_NR 16
#define CAMEL_GEMM_ENTRY sgemmAvx2Probe
#define CAMEL_GEMM_PROBE 1
#include "gemm_blocked.inl"

namespace camel::tensor::kernels::gemmprobe {

struct Counters {
    std::atomic<unsigned long long> ns{0};
    std::atomic<long long> flops{0};
    std::atomic<long long> bytes{0};
    std::atomic<unsigned long long> calls{0};
};

Counters gCounters[kPhaseCount];

void add(int phase, unsigned long long ns, long long flops, long long bytes) {
    Counters &c = gCounters[phase];
    c.ns.fetch_add(ns, std::memory_order_relaxed);
    c.flops.fetch_add(flops, std::memory_order_relaxed);
    c.bytes.fetch_add(bytes, std::memory_order_relaxed);
    c.calls.fetch_add(1, std::memory_order_relaxed);
}

// Nanoseconds elapsed on the cycle counter. Called twice per micro tile, so it must be cheap:
// clock_gettime(CLOCK_THREAD_CPUTIME_ID) is a system call on Linux and costs more than a tile.
// gTscToNs is calibrated against CPU time once at start-up; an idle round then reads the same
// nanoseconds a wall clock would, and a preempted round reads more (the medians drop those).
double gTscToNs = 1.0;

unsigned long long now() {
    return static_cast<unsigned long long>(static_cast<double>(__builtin_ia32_rdtsc()) * gTscToNs);
}

void reset() {
    for (Counters &c : gCounters) {
        c.ns.store(0, std::memory_order_relaxed);
        c.flops.store(0, std::memory_order_relaxed);
        c.bytes.store(0, std::memory_order_relaxed);
        c.calls.store(0, std::memory_order_relaxed);
    }
}

} // namespace camel::tensor::kernels::gemmprobe

namespace {

using namespace camel::tensor::kernels;

// Loaded at run time: CBLAS row-major, no transpose, one thread.
using CblasSgemm = void (*)(
    int order, int transA, int transB, int M, int N, int K, float alpha, const float *A, int lda,
    const float *B, int ldb, float beta, float *C, int ldc);
using SetThreads = void (*)(int);

CblasSgemm gCblas          = nullptr;
SetThreads gSetBlasThreads = nullptr;

void loadBlas() {
    for (const char *name : {"libopenblas.so.0", "libopenblas.so", "libblas.so.3", "libmkl_rt.so"}) {
        void *handle = dlopen(name, RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            continue;
        }
        gCblas = reinterpret_cast<CblasSgemm>(dlsym(handle, "cblas_sgemm"));
        gSetBlasThreads = reinterpret_cast<SetThreads>(dlsym(handle, "openblas_set_num_threads"));
        if (gSetBlasThreads) {
            gSetBlasThreads(1);
        }
        if (gCblas) {
            return;
        }
    }
}

constexpr int kRowMajor = 101;
constexpr int kNoTrans = 111;
constexpr int kTrans   = 112;

double wallNow() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// Thread CPU nanoseconds: for a whole call (a handful of system calls does not matter).
unsigned long long cpuNs() {
    timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return static_cast<unsigned long long>(ts.tv_sec) * 1000000000ull +
           static_cast<unsigned long long>(ts.tv_nsec);
}

double loadAverage() {
    FILE *f = std::fopen("/proc/loadavg", "r");
    if (!f) {
        return 0.0;
    }
    double one = 0.0;
    if (std::fscanf(f, "%lf", &one) != 1) {
        one = 0.0;
    }
    std::fclose(f);
    return one;
}

struct Shape {
    int64_t m, n, k;
    bool transA = false, transB = false; // op(A) = A^T / op(B) = B^T
    int64_t lda = 0, ldb = 0;            // 0 means "dense in the orientation written"
    int64_t rowsA() const { return transA ? k : m; }
    int64_t colsA() const { return transA ? m : k; }
    int64_t rowsB() const { return transB ? n : k; }
    int64_t colsB() const { return transB ? k : n; }
    int64_t strideA() const { return lda ? lda : colsA(); }
    int64_t strideB() const { return ldb ? ldb : colsB(); }
};

/// Deterministic values in [-0.09, 0.09], no zeros of a special kind and no denormals.
void fill(std::vector<float> &v, uint32_t seed) {
    for (size_t i = 0; i < v.size(); ++i) {
        seed = seed * 1664525u + 1013904223u;
        v[i] = (static_cast<float>((seed >> 8) & 1023u) - 511.5f) * 0.00018f;
    }
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const size_t n = values.size();
    if (n == 0) {
        return 0.0;
    }
    return n % 2 ? values[n / 2] : 0.5 * (values[n / 2 - 1] + values[n / 2]);
}

/// CPU seconds of one call of `fn`, medians over `rounds` calls; the wall is kept beside it.
template <typename Fn> double callCpuSeconds(Fn &&fn, int rounds, double *wallOut = nullptr) {
    std::vector<double> cpu, wall;
    for (int r = 0; r < rounds; ++r) {
        const double c0 = static_cast<double>(cpuNs());
        const double w0 = wallNow();
        fn();
        const double c1 = static_cast<double>(cpuNs());
        const double w1 = wallNow();
        cpu.push_back((c1 - c0) / 1e9);
        wall.push_back(w1 - w0);
    }
    if (wallOut) {
        *wallOut = median(wall);
    }
    return median(cpu);
}

/**
 * The core's FMA peak: N independent accumulators, two operands, one fused multiply-add each per
 * iteration. Twelve is what the 6 x 16 tile has; sixteen leaves no register for the operands.
 */
template <int N> double peakGflops(int64_t iterations) {
    __m256 a = _mm256_set1_ps(1.000001f);
    __m256 b = _mm256_set1_ps(0.999999f);
    __m256 acc[N];
    for (int i = 0; i < N; ++i) {
        acc[i] = _mm256_set1_ps(0.5f + i);
    }
    std::vector<double> samples;
    for (int round = 0; round < 5; ++round) {
        const double start = static_cast<double>(cpuNs());
        for (int64_t i = 0; i < iterations; ++i) {
            for (int j = 0; j < N; ++j) {
                acc[j] = _mm256_fmadd_ps(a, b, acc[j]);
            }
        }
        const double seconds = (static_cast<double>(cpuNs()) - start) / 1e9;
        samples.push_back(2.0 * 8.0 * N * iterations / seconds / 1e9);
    }
    __m256 sink = acc[0];
    for (int i = 1; i < N; ++i) {
        sink = _mm256_add_ps(sink, acc[i]);
    }
    volatile float keep = _mm256_cvtss_f32(sink);
    (void)keep;
    return median(samples);
}

/// One micro tile over packed blocks that stay in L1: the inner loop's own rate.
double microCeilingGflops(int64_t reps) {
    std::vector<float> packedA(detail::kMR * detail::kKC), packedB(detail::kKC * detail::kNR),
        c(detail::kMR * detail::kNR);
    fill(packedA, 1);
    fill(packedB, 2);
    std::vector<double> samples;
    for (int round = 0; round < 5; ++round) {
        const double start = static_cast<double>(cpuNs());
        for (int64_t i = 0; i < reps; ++i) {
            detail::microKernel(
                detail::kKC,
                packedA.data(),
                packedB.data(),
                nullptr,
                true,
                true,
                c.data(),
                detail::kNR,
                1.0f,
                0.0f,
                detail::kMR,
                detail::kNR);
        }
        const double seconds = (static_cast<double>(cpuNs()) - start) / 1e9;
        samples.push_back(2.0 * detail::kMR * detail::kNR * detail::kKC * reps / seconds / 1e9);
    }
    volatile float keep = c[0];
    (void)keep;
    return median(samples);
}

/// The canonical order, unblocked: acc = fma(a, b, acc) in ascending k, one element at a time.
double naiveGflops(
    const Shape &s, const std::vector<float> &a, const std::vector<float> &b, std::vector<float> &c,
    int rounds) {
    auto body = [&] {
        for (int64_t i = 0; i < s.m; ++i) {
            for (int64_t j = 0; j < s.n; ++j) {
                float acc = 0.0f;
                for (int64_t k = 0; k < s.k; ++k) {
                    acc = std::fma(
                        detail::elemA(a.data(), s.strideA(), s.transA, i, k),
                        detail::elemB(b.data(), s.strideB(), s.transB, k, j),
                        acc);
                }
                c[i * s.n + j] = acc;
            }
        }
    };
    const double seconds = callCpuSeconds(body, rounds);
    volatile float keep = c[0];
    (void)keep;
    return 2.0 * s.m * s.n * s.k / seconds / 1e9;
}

double blasGflops(
    const Shape &s, const std::vector<float> &a, const std::vector<float> &b, std::vector<float> &c,
    int reps) {
    if (!gCblas) {
        return 0.0;
    }
    auto body = [&] {
        for (int i = 0; i < reps; ++i) {
            gCblas(
                kRowMajor,
                s.transA ? kTrans : kNoTrans,
                s.transB ? kTrans : kNoTrans,
                static_cast<int>(s.m),
                static_cast<int>(s.n),
                static_cast<int>(s.k),
                1.0f,
                a.data(),
                static_cast<int>(s.strideA()),
                b.data(),
                static_cast<int>(s.strideB()),
                0.0f,
                c.data(),
                static_cast<int>(s.n));
        }
    };
    const double seconds = callCpuSeconds(body, 3) / reps;
    volatile float keep = c[0];
    (void)keep;
    return 2.0 * s.m * s.n * s.k / seconds / 1e9;
}

struct Result {
    Shape shape;
    int threads = 1;
    double cpuMs = 0.0;
    double wallMs = 0.0;
    double gflops = 0.0;
    double phaseMs[gemmprobe::kPhaseCount] = {};
    double packGbs = 0.0;
    double microGflops = 0.0;
    double naiveGflops = 0.0;
    double blasGflops = 0.0;
    long long chunks = 0;
    long long microCalls = 0;
    long long edgeCalls = 0;
    double load = 0.0;
};

Result measure(const Shape &s, int threads, int reps) {
    Result r;
    r.shape   = s;
    r.threads = threads;
    const int64_t lda  = s.strideA();
    const int64_t ldb  = s.strideB();
    const size_t aSize = static_cast<size_t>(s.rowsA() * lda);
    const size_t bSize = static_cast<size_t>(s.rowsB() * ldb);
    const size_t cSize = static_cast<size_t>(s.m * s.n);
    std::vector<float> a(aSize), b(bSize), c(cSize);
    fill(a, 1);
    fill(b, 2);

    setNumThreads(threads);
    for (int i = 0; i < 2; ++i) {
        detail::sgemmAvx2Probe(
            s.transA, s.transB, s.m, s.n, s.k, 1.0f, a.data(), lda, b.data(), ldb, 0.0f, c.data(),
            s.n);
    }

    std::vector<double> cpuSamples, wallSamples;
    std::vector<std::vector<double>> phaseSamples(gemmprobe::kPhaseCount);
    long long microFlops = 0, packBytes = 0;
    for (int round = 0; round < reps; ++round) {
        gemmprobe::reset();
        const double c0 = static_cast<double>(cpuNs());
        const double w0 = wallNow();
        detail::sgemmAvx2Probe(
            s.transA, s.transB, s.m, s.n, s.k, 1.0f, a.data(), lda, b.data(), ldb, 0.0f, c.data(),
            s.n);
        const double c1 = static_cast<double>(cpuNs());
        const double w1 = wallNow();
        cpuSamples.push_back((c1 - c0) / 1e6);
        wallSamples.push_back((w1 - w0) * 1e3);
        for (int p = 0; p < gemmprobe::kPhaseCount; ++p) {
            phaseSamples[p].push_back(gemmprobe::gCounters[p].ns.load() / 1e6);
        }
        microFlops = gemmprobe::gCounters[gemmprobe::kMicroFull].flops.load() +
                     gemmprobe::gCounters[gemmprobe::kMicroEdge].flops.load();
        packBytes = gemmprobe::gCounters[gemmprobe::kPackA].bytes.load() +
                    gemmprobe::gCounters[gemmprobe::kPackB].bytes.load();
        r.chunks     = gemmprobe::gCounters[gemmprobe::kChunk].calls.load();
        r.microCalls = gemmprobe::gCounters[gemmprobe::kMicroFull].calls.load() +
                       gemmprobe::gCounters[gemmprobe::kMicroEdge].calls.load();
        r.edgeCalls = gemmprobe::gCounters[gemmprobe::kMicroEdge].calls.load();
    }
    // One coherent sample: the round whose whole call was the shortest. Its phases add up the way
    // the call does (pack and micro inside chunk, chunk inside region, region inside the call),
    // and preemption, which only ever makes a round longer, is the least present in it. The call
    // rate uses that round's CPU time, so a rate and a breakdown never come from different runs.
    size_t best = 0;
    for (size_t i = 1; i < cpuSamples.size(); ++i) {
        if (phaseSamples[gemmprobe::kTotal][i] < phaseSamples[gemmprobe::kTotal][best]) {
            best = i;
        }
    }
    r.cpuMs  = cpuSamples[best];
    r.wallMs = wallSamples[best];
    r.gflops = 2.0 * s.m * s.n * s.k / (r.cpuMs / 1e3) / 1e9;
    r.load   = loadAverage();
    const double totalMs = phaseSamples[gemmprobe::kTotal][best];
    const double scale   = totalMs > 0.0 ? r.cpuMs / totalMs : 1.0;
    for (int p = 0; p < gemmprobe::kPhaseCount; ++p) {
        r.phaseMs[p] = phaseSamples[p][best] * scale;
    }
    const double microMs =
        r.phaseMs[gemmprobe::kMicroFull] + r.phaseMs[gemmprobe::kMicroEdge];
    r.microGflops = microMs > 0.0 ? microFlops / 1e9 / (microMs / 1e3) : 0.0;
    const double packMs = r.phaseMs[gemmprobe::kPackA] + r.phaseMs[gemmprobe::kPackB];
    r.packGbs           = packMs > 0.0 ? packBytes / 1e6 / (packMs / 1e3) : 0.0;
    r.naiveGflops = s.m * s.n * s.k <= 40000000 ? naiveGflops(s, a, b, c, 3) : 0.0;
    r.blasGflops  = blasGflops(s, a, b, c, std::max(3, reps));
    return r;
}

void report(const Result &r, double peak, double ceiling) {
    const double work = r.phaseMs[gemmprobe::kMicroFull] + r.phaseMs[gemmprobe::kMicroEdge];
    std::printf(
        "shape=%lldx%lldx%lld%s%s threads=%d cpu_ms=%.4f wall_ms=%.4f gflops=%.2f peak_pct=%.1f "
        "micro_gflops=%.2f micro_ceiling=%.2f chunks=%lld micro_calls=%lld edge_calls=%lld "
        "pack_gbs=%.1f load=%.1f\n",
        (long long)r.shape.m,
        (long long)r.shape.n,
        (long long)r.shape.k,
        r.shape.transA ? "tA" : "",
        r.shape.transB ? "tB" : "",
        r.threads,
        r.cpuMs,
        r.wallMs,
        r.gflops,
        100.0 * r.gflops / peak,
        r.microGflops,
        ceiling,
        r.chunks,
        r.microCalls,
        r.edgeCalls,
        r.packGbs,
        r.load);
    std::printf(
        "  phases_ms total=%.4f setup=%.4f region=%.4f chunk=%.4f packB=%.4f packA=%.4f "
        "micro_full=%.4f micro_edge=%.4f micro=%.4f residual=%.4f join=%.4f\n",
        r.phaseMs[gemmprobe::kTotal],
        r.phaseMs[gemmprobe::kSetup],
        r.phaseMs[gemmprobe::kRegion],
        r.phaseMs[gemmprobe::kChunk],
        r.phaseMs[gemmprobe::kPackB],
        r.phaseMs[gemmprobe::kPackA],
        r.phaseMs[gemmprobe::kMicroFull],
        r.phaseMs[gemmprobe::kMicroEdge],
        work,
        r.phaseMs[gemmprobe::kChunk] - r.phaseMs[gemmprobe::kPackB] - r.phaseMs[gemmprobe::kPackA] -
            work,
        r.phaseMs[gemmprobe::kRegion] * r.threads - r.phaseMs[gemmprobe::kChunk]);
    std::printf(
        "  compare naive_gflops=%.2f blas_gflops=%.2f vs_blas=%.2f vs_naive=%.2f\n",
        r.naiveGflops,
        r.blasGflops,
        r.blasGflops > 0 ? r.gflops / r.blasGflops : 0.0,
        r.naiveGflops > 0 ? r.gflops / r.naiveGflops : 0.0);
}

} // namespace

int main(int argc, char **argv) {
    std::vector<int> threads{1};
    int reps = 5;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--threads" && i + 1 < argc) {
            threads.clear();
            const char *p = argv[++i];
            while (*p) {
                threads.push_back(static_cast<int>(std::strtol(p, const_cast<char **>(&p), 10)));
                if (*p == ',') {
                    ++p;
                }
            }
        } else if (arg == "--reps" && i + 1 < argc) {
            reps = std::atoi(argv[++i]);
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
            return 2;
        }
    }

    // The shapes one d=256 step issues (batch 16 x sequence 32 = 512 tokens, d_model 256,
    // d_ff 512, 16 heads of width 16), the two projections of the task's list, and the batched
    // attention product. 1024^3 is the large reference: the same kernel where the per-tile costs
    // are amortised.
    const Shape shapes[] = {
        // The two projections of the task's list, the attention score product of a d=256 step
        // (batch 16 x heads 16, sequence 32, d_head 16: one call), and the four forward
        // projections of the two blocks.
        {256, 64, 256},
        {256, 256, 64},
        {32, 32, 16},
        {512, 768, 256},
        {512, 256, 256},
        {512, 512, 256},
        {512, 256, 512},
        // The backward products of those same linears: matmul_grad_lhs takes B transposed,
        // matmul_grad_rhs takes A transposed, so the packers see the strided orientations.
        {512, 256, 768, true, false},
        {256, 768, 512, false, true},
        {512, 256, 512, true, false},
        {512, 256, 512, false, true},
        {512, 256, 256, true, false},
        {256, 256, 512, false, true},
        // The large reference: the same kernel where per-tile costs are amortised.
        {1024, 1024, 1024},
    };

    loadBlas();
    {
        // The cycle counter ticks at a constant rate. Calibrate it against the wall clock while
        // the thread sleeps: both advance during a preemption, so the ratio is the same on a
        // loaded host as on an idle one, and a phase's ticks then read as the nanoseconds that
        // passed inside it (the median over rounds drops the rounds that were preempted).
        const unsigned long long tsc0 = __builtin_ia32_rdtsc();
        const double wall0            = wallNow();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        const unsigned long long tsc1 = __builtin_ia32_rdtsc();
        const double wall1            = wallNow();
        gemmprobe::gTscToNs =
            (wall1 - wall0) * 1e9 / static_cast<double>(tsc1 - tsc0);
        std::printf(
            "clock tsc_ns_per_tick=%.6f tsc_ghz=%.3f load=%.1f\n",
            gemmprobe::gTscToNs,
            1.0 / gemmprobe::gTscToNs,
            loadAverage());
    }
    const double peak    = peakGflops<12>(20000000);
    const double peak8   = peakGflops<8>(20000000);
    const double ceiling = microCeilingGflops(20000);
    std::printf(
        "machine cpu=\"AMD EPYC 7343\" cores=16 threads=32 peak12_gflops=%.2f "
        "peak8_gflops=%.2f micro_ceiling_gflops=%.2f load=%.1f blas=%s\n",
        peak,
        peak8,
        ceiling,
        loadAverage(),
        gCblas ? "loaded" : "none");
    std::fflush(stdout);

    for (int t : threads) {
        for (const Shape &s : shapes) {
            const Result r = measure(s, t, reps);
            report(r, peak, ceiling);
            std::fflush(stdout);
        }
    }
    return 0;
}