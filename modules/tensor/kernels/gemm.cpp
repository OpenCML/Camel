// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * GEMM backend selection and the matmul/linear operators built on it.
 *
 * The builtin backend is compiled from gemm_blocked.inl once per ISA: a
 * portable unit, an AVX2+FMA unit, and an AVX-512F unit (x86-64 builds). The
 * widest unit the CPU and OS support is selected at first use, so default
 * builds stay portable while still using wide vectors where available. Every
 * operator uses it (matmul's canonical algorithm, design principles 7.1). With
 * CAMEL_TENSOR_USE_CBLAS, sgemmLibrary forwards to cblas_sgemm; the
 * tensor:matmul kernel takes it when the node's `tensor.kernel` attribute says
 * "library", which tensor::fuse writes for a matmul the 7.3 rule permits (the
 * operator stays `matmul`, design principles 9.6). The library path keeps the
 * canonical kernel below a measured size (libraryPays), so a batched attention
 * product does not pay a per-call library overhead it cannot repay.
 *
 * The thread count is the run's budget, and the tensor pool spends it. The
 * library's own pool is one thread for every call: a whole-work product splits
 * its output rows over the tensor pool, one library thread per chunk, and a
 * product inside a batch loop that splits over the pool is one library thread
 * per batch chunk because the pool already spends the budget. Rows only, never
 * K: every output element's accumulation stays inside one call. The curve that
 * decided this (the library's own pool beside the tensor pool loses from four
 * threads up, and one library thread beats the canonical kernel at -t 1) is in
 * docs/technical/25_tensor_stack_and_onnx_export.md, "The library GEMM's
 * thread budget"; `gemmThreadsUsed()` and `gemmLibraryThreadsUsed()` are what a
 * program can observe.
 */

#include "gemm.h"

#include "cpu.h"
#include "elementwise.h"
#include "minmax.h"
#include "parallel.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <vector>

#if defined(CAMEL_TENSOR_USE_CBLAS)
#include <cblas.h>
#endif

namespace camel::tensor::kernels {

namespace detail {
using SgemmFn = void (*)(
    bool, bool, int64_t, int64_t, int64_t, float, const float *, int64_t, const float *, int64_t,
    float, float *, int64_t);
void sgemmGeneric(
    bool transA, bool transB, int64_t M, int64_t N, int64_t K, float alpha, const float *A,
    int64_t lda, const float *B, int64_t ldb, float beta, float *C, int64_t ldc);
#if defined(CAMEL_TENSOR_HAS_AVX2_UNIT)
void sgemmAvx2(
    bool transA, bool transB, int64_t M, int64_t N, int64_t K, float alpha, const float *A,
    int64_t lda, const float *B, int64_t ldb, float beta, float *C, int64_t ldc);
void sgemmAvx512(
    bool transA, bool transB, int64_t M, int64_t N, int64_t K, float alpha, const float *A,
    int64_t lda, const float *B, int64_t ldb, float beta, float *C, int64_t ldc);
#endif
} // namespace detail

using type::TypeCode;

namespace {

struct BuiltinBackend {
    detail::SgemmFn fn;
    std::string_view name;
};

const BuiltinBackend &builtinBackend() {
    static const BuiltinBackend backend = [] {
#if defined(CAMEL_TENSOR_HAS_AVX2_UNIT)
        const CpuFeatures &cpu = cpuFeatures();
        if (cpu.avx512f) {
            return BuiltinBackend{&detail::sgemmAvx512, "builtin-avx512"};
        }
        if (cpu.avx2Fma) {
            return BuiltinBackend{&detail::sgemmAvx2, "builtin-avx2"};
        }
#endif
        return BuiltinBackend{&detail::sgemmGeneric, "builtin"};
    }();
    return backend;
}

/// Integer matmul for int32 and int64 (and promoted bool) operands; not performance critical.
/// The accumulator is the element type, so int32 products and sums wrap the way int32
/// arithmetic does (design principles 5.1, 7.7).
template <typename T>
void integerGemm(int64_t M, int64_t N, int64_t K, const T *A, const T *B, T *C) {
    for (int64_t i = 0; i < M; ++i) {
        for (int64_t j = 0; j < N; ++j) {
            T acc = 0;
            for (int64_t k = 0; k < K; ++k) {
                acc = static_cast<T>(acc + static_cast<T>(A[i * K + k] * B[k * N + j]));
            }
            C[i * N + j] = acc;
        }
    }
}

/// f64 matmul: the same summation order as sgemm (ascending k), in double. A separate loop
/// rather than the f32 kernel: an f64 tensor is computed in f64, never narrowed to f32.
void dgemm(int64_t M, int64_t N, int64_t K, const double *A, const double *B, double *C) {
    for (int64_t i = 0; i < M; ++i) {
        for (int64_t j = 0; j < N; ++j) {
            double acc = 0.0;
            for (int64_t k = 0; k < K; ++k) {
                acc += A[i * K + k] * B[k * N + j];
            }
            C[i * N + j] = acc;
        }
    }
}

struct MatmulPlan {
    Shape outShape;
    Shape batchShape;
    int64_t M = 1, N = 1, K = 1;
    Shape lhsBatch, rhsBatch; // batch extents right-aligned to batchShape
};

MatmulPlan planMatmul(std::span<const int64_t> lhs, std::span<const int64_t> rhs) {
    if (lhs.empty() || rhs.empty()) {
        throw std::invalid_argument("matmul does not accept rank-0 tensors");
    }
    MatmulPlan plan;
    const bool lhsVector = lhs.size() == 1;
    const bool rhsVector = rhs.size() == 1;
    plan.M               = lhsVector ? 1 : lhs[lhs.size() - 2];
    plan.K               = lhs.back();
    const int64_t rhsK   = rhsVector ? rhs[0] : rhs[rhs.size() - 2];
    plan.N               = rhsVector ? 1 : rhs.back();
    if (plan.K != rhsK) {
        throw ShapeError(
            "matmul inner dimensions differ: " + std::to_string(plan.K) + " vs " +
            std::to_string(rhsK));
    }
    const auto lhsBatch = lhs.subspan(0, lhs.size() - (lhsVector ? 1 : 2));
    const auto rhsBatch = rhs.subspan(0, rhs.size() - (rhsVector ? 1 : 2));
    plan.batchShape     = broadcastShapes(lhsBatch, rhsBatch);
    plan.lhsBatch.assign(plan.batchShape.size() - lhsBatch.size(), 1);
    plan.lhsBatch.insert(plan.lhsBatch.end(), lhsBatch.begin(), lhsBatch.end());
    plan.rhsBatch.assign(plan.batchShape.size() - rhsBatch.size(), 1);
    plan.rhsBatch.insert(plan.rhsBatch.end(), rhsBatch.begin(), rhsBatch.end());
    plan.outShape = plan.batchShape;
    if (!lhsVector) {
        plan.outShape.push_back(plan.M);
    }
    if (!rhsVector) {
        plan.outShape.push_back(plan.N);
    }
    return plan;
}

/// Offset (in matrices) of batch index `b` within an operand whose batch extents are `extents`.
int64_t batchOffset(const Shape &batchShape, const Shape &extents, int64_t b) {
    int64_t offset = 0, stride = 1;
    for (size_t d = batchShape.size(); d-- > 0;) {
        const int64_t idx = b % batchShape[d];
        b /= batchShape[d];
        if (extents[d] != 1) {
            offset += idx * stride;
        }
        stride *= extents[d];
    }
    return offset;
}

} // namespace

#if defined(CAMEL_TENSOR_HAS_OPENBLAS)
extern "C" void openblas_set_num_threads(int);
#endif
#if defined(CAMEL_TENSOR_HAS_MKL)
extern "C" void mkl_set_num_threads(int);
#endif

namespace {

/// Sets the library's own pool. Clamped to at least one; a build without a library has no pool to
/// size and the canonical kernel uses the tensor pool instead.
void applyLibraryThreads(int threads) {
    threads = std::max(threads, 1);
#if defined(CAMEL_TENSOR_HAS_OPENBLAS)
    openblas_set_num_threads(threads);
#elif defined(CAMEL_TENSOR_HAS_MKL)
    mkl_set_num_threads(threads);
#else
    (void)threads;
#endif
}

/// The thread count the GEMM path had for its most recent call (gemm.h, `gemmThreadsUsed`).
std::atomic<int> gGemmThreadsUsed{1};

void noteGemmThreads(int threads) { gGemmThreadsUsed.store(std::max(threads, 1)); }

/// The library's own thread count for the most recent call (gemm.h, `gemmLibraryThreadsUsed`):
/// 1 when a library call ran, 0 when the canonical kernel did.
std::atomic<int> gGemmLibraryThreadsUsed{1};

void noteGemmLibraryThreads(int threads) {
    gGemmLibraryThreadsUsed.store(std::max(threads, 0));
}

/**
 * The library GEMM's own pool: one thread, for the duration of one matmul and after it.
 *
 * The library used to take the run's whole budget when the product was the whole work (one call,
 * as the large 2-D products of a training step are). That made a second pool beside the tensor
 * pool, and on a loaded host the two pools compete for the same cores: the curve in
 * `docs/technical/25_tensor_stack_and_onnx_export.md` has the uncapped library losing to the
 * canonical kernel from four threads up, while one library thread beats the canonical
 * kernel at `-t 1` (3921 against 7260 ms on the 1024^3 cell). So the budget stays with the tensor
 * pool and the library runs one thread per call: a whole-work product splits its output rows over
 * the pool (one library call per chunk), and a batched product gives the pool the budget and each
 * library call one thread.
 *
 * The library's initial count is one before it loads as well: OpenBLAS creates its worker pool in
 * a constructor, at the machine's count, and a setting applied later leaves those threads alive
 * and unused (measured with `strace`: 31 threads at `-t 1` without the seed). The seed is
 * `camel::initialize()`; see the note there.
 */
class LibraryThreads {
  public:
    LibraryThreads() { applyLibraryThreads(1); }
    ~LibraryThreads() { applyLibraryThreads(1); }
    LibraryThreads(const LibraryThreads &)            = delete;
    LibraryThreads &operator=(const LibraryThreads &) = delete;
};

} // namespace

void sgemm(
    bool transA, bool transB, int64_t M, int64_t N, int64_t K, float alpha, const float *A,
    int64_t lda, const float *B, int64_t ldb, float beta, float *C, int64_t ldc) {
    // The canonical kernel splits its output tiles over the tensor pool, and runs serially inside
    // a region. It reports the width of that split, not the library's one thread per call: the
    // canonical path is the one that spends the pool.
    noteGemmThreads(insideParallelRegion() ? 1 : numThreads());
    noteGemmLibraryThreads(0);
    builtinBackend().fn(transA, transB, M, N, K, alpha, A, lda, B, ldb, beta, C, ldc);
}

namespace {

/// Whether one GEMM call has enough work for the library to repay its call and
/// packing overhead. Paired macrobenchmarks, one thread, same process, OpenBLAS 0.3.20 against
/// the canonical kernel (median of 10, `sum` forcing the result; raw library before this guard),
/// library vs canonical:
///   1024^3                    20.2 vs 24.0 ms  -> library
///   2048x1024x1024            39.8 vs 46.7 ms  -> library
///   2048x1024x2048            79.3 vs 93.1 ms  -> library
///   2048x2048x1024            76.8 vs 89.2 ms  -> library
///   1024x2048x1024            38.8 vs 44.5 ms  -> library
///   32x64x64x16 (134M)        15.7 vs 12.1 ms  -> canonical
///   256x64x128 (2.1M)          0.081 vs 0.075 ms -> canonical
/// The crossover lies between 1.3e8 and 1.1e9 multiply-adds per call; 2^30 is the measured
/// large side and the natural "one large GEMM" boundary. A batched matmul is decided per call,
/// so many small attention products stay on the canonical kernel. After the guard the small
/// cells run the same kernel both ways and their difference is measurement noise.
constexpr int64_t kLibraryMinProducts = int64_t(1) << 30;

bool libraryPays(int64_t M, int64_t N, int64_t K) {
    if (M <= 0 || N <= 0 || K <= 0) {
        return false;
    }
    // Long double keeps the product from wrapping on a shape no real tensor has, and the
    // comparison is exact for the integers in range.
    return static_cast<long double>(M) * N * K >=
           static_cast<long double>(kLibraryMinProducts);
}

/**
 * One library call, with the library's own pool at one thread: the unit a row block hands to the
 * library. `gemm_threads()` reports the tensor-pool chunks that split the product, not this call;
 * `gemm_library_threads()` reports this one thread.
 */
void sgemmLibraryCall(
    bool transA, bool transB, int64_t M, int64_t N, int64_t K, float alpha, const float *A,
    int64_t lda, const float *B, int64_t ldb, float beta, float *C, int64_t ldc) {
#if defined(CAMEL_TENSOR_USE_CBLAS)
    noteGemmLibraryThreads(1);
    cblas_sgemm(
        CblasRowMajor,
        transA ? CblasTrans : CblasNoTrans,
        transB ? CblasTrans : CblasNoTrans,
        static_cast<int>(M),
        static_cast<int>(N),
        static_cast<int>(K),
        alpha,
        A,
        static_cast<int>(lda),
        B,
        static_cast<int>(ldb),
        beta,
        C,
        static_cast<int>(ldc));
#else
    // No library: the path is the canonical kernel, which reports itself as 0 library threads.
    noteGemmLibraryThreads(0);
    builtinBackend().fn(transA, transB, M, N, K, alpha, A, lda, B, ldb, beta, C, ldc);
#endif
}

} // namespace

void sgemmLibrary(
    bool transA, bool transB, int64_t M, int64_t N, int64_t K, float alpha, const float *A,
    int64_t lda, const float *B, int64_t ldb, float beta, float *C, int64_t ldc) {
#if defined(CAMEL_TENSOR_USE_CBLAS)
    if (!libraryPays(M, N, K)) {
        // The canonical kernel, through `sgemm` so that `gemm_threads()` reports the pool it
        // actually split over, rather than leaving the library's one thread beside a split.
        sgemm(transA, transB, M, N, K, alpha, A, lda, B, ldb, beta, C, ldc);
        return;
    }
    // A product that is the whole work splits its output rows over the tensor pool: the pool
    // spends the run's budget, one library thread per chunk. Rows only, never K, so every output
    // element's accumulation stays inside one call and the bits are the ones one library call
    // would produce. Inside a batch chunk the caller is already in the pool region,
    // `parallelWouldSplit` is false, and the call is the one thread the pool left it.
    if (parallelWouldSplit(M, 1)) {
        std::atomic<int> chunks{0};
        parallelFor(M, 1, [&](int64_t begin, int64_t end) {
            // A row block: with A transposed the rows of C are the columns of A, so the block
            // starts one element in, not one leading dimension in. C's block always starts a
            // leading dimension in. B does not depend on the row.
            const int64_t aOffset = transA ? begin : begin * lda;
            sgemmLibraryCall(
                transA,
                transB,
                end - begin,
                N,
                K,
                alpha,
                A + aOffset,
                lda,
                B,
                ldb,
                beta,
                C + begin * ldc,
                ldc);
            chunks.fetch_add(1, std::memory_order_relaxed);
        });
        noteGemmThreads(chunks.load(std::memory_order_relaxed));
        return;
    }
    noteGemmThreads(1);
    sgemmLibraryCall(transA, transB, M, N, K, alpha, A, lda, B, ldb, beta, C, ldc);
#else
    sgemm(transA, transB, M, N, K, alpha, A, lda, B, ldb, beta, C, ldc);
#endif
}

std::string_view gemmBackendName() { return builtinBackend().name; }

std::string_view libraryGemmName() {
#if defined(CAMEL_TENSOR_USE_CBLAS)
    return "cblas";
#else
    return builtinBackend().name;
#endif
}

void setGemmThreads(int threads) {
    // The run's budget is the tensor pool's: `-t`, whatever it is. The library's own pool does not
    // take a share of it: one thread per library call, so the process has one pool, not two.
    // The parameter is the caller's budget and is deliberately not applied to the library; it
    // stays in the signature because `setNumThreads` is the one caller and the declaration is
    // the module's. The library's own count is 1; `gemm_threads()` reports the pool chunks that
    // split a whole-work product, and `gemm_library_threads()` reports this one.
    (void)threads;
    applyLibraryThreads(1);
    noteGemmThreads(1);
}

int gemmThreadsUsed() { return gGemmThreadsUsed.load(); }

int gemmLibraryThreadsUsed() { return gGemmLibraryThreadsUsed.load(); }

Shape matmulShape(std::span<const int64_t> lhs, std::span<const int64_t> rhs) {
    return planMatmul(lhs, rhs).outShape;
}

Array *matmul(
    const Array *lhs, const Array *rhs, mm::IAllocator &allocator, bool library) {
    const auto gemm = library ? &sgemmLibrary : &sgemm;
    const MatmulPlan plan  = planMatmul(lhs->shapeSpan(), rhs->shapeSpan());
    // The product of two arrays is computed in the one element type they state: matmul refuses a
    // pair of different element types before it answers a type (ops/linalg_ops.cpp,
    // refuseMixedElementTypes), so a graph reaching this kernel with such a pair was built
    // without the operator's inference. The kernel reports that rather than converting silently.
    if (lhs->dtype() != rhs->dtype()) {
        throw std::invalid_argument(
            "the two operands have different element types ('" +
            elementTypeSpelling(lhs->dtype()) + "' and '" + elementTypeSpelling(rhs->dtype()) +
            "'): a tensor operator computes in one element type, and there is no implicit "
            "conversion between numeric types");
    }
    const TypeCode outType = lhs->dtype();
    // A bf16 or f16 matmul is the f32 multiplication of the exactly widened operands, rounded
    // once into the element type when the result is stored: the same one arithmetic path as
    // the elementwise operators (dtype.h), with f32 as the accumulator the declaration states.
    // The result type stays the element type the program wrote.
    if (outType == TypeCode::BFloat16 || outType == TypeCode::Float16) {
        Array *wideLhs = lhs->dtype() == TypeCode::Float32 ? const_cast<Array *>(lhs)
                                                           : cast(lhs, TypeCode::Float32, allocator);
        Array *wideRhs = rhs->dtype() == TypeCode::Float32 ? const_cast<Array *>(rhs)
                                                           : cast(rhs, TypeCode::Float32, allocator);
        return cast(matmul(wideLhs, wideRhs, allocator, library), outType, allocator);
    }
    const TypeCode compute = outType == TypeCode::Bool ? TypeCode::Int64 : outType;
    const Array *a  = lhs->dtype() == compute ? lhs : cast(lhs, compute, allocator);
    const Array *b  = rhs->dtype() == compute ? rhs : cast(rhs, compute, allocator);
    Array *out      = createTensor(compute, plan.outShape, allocator);

    const int64_t batches = static_cast<int64_t>(numelOf(plan.batchShape));
    const int64_t aSize = plan.M * plan.K, bSize = plan.K * plan.N, cSize = plan.M * plan.N;
    // One pool: the tensor pool spends the run's budget for a batched product, and a library call
    // is one thread whether the product is the whole work or one chunk of the batch. The library's
    // own pool was the second one; `LibraryThreads` keeps it at one for these calls.
    std::optional<LibraryThreads> libraryThreads;
    if (compute == TypeCode::Float32 && library) {
        libraryThreads.emplace();
    }
    // Many small matrices: parallelize over the batch (inner GEMMs then run serially).
    parallelFor(batches, 1, [&](int64_t begin, int64_t end) {
        for (int64_t batch = begin; batch < end; ++batch) {
            const int64_t aOff = batchOffset(plan.batchShape, plan.lhsBatch, batch) * aSize;
            const int64_t bOff = batchOffset(plan.batchShape, plan.rhsBatch, batch) * bSize;
            if (compute == TypeCode::Float32) {
                gemm(
                    false,
                    false,
                    plan.M,
                    plan.N,
                    plan.K,
                    1.0f,
                    a->dataAs<float>() + aOff,
                    plan.K,
                    b->dataAs<float>() + bOff,
                    plan.N,
                    0.0f,
                    out->dataAs<float>() + batch * cSize,
                    plan.N);
            } else if (compute == TypeCode::Float64) {
                dgemm(
                    plan.M,
                    plan.N,
                    plan.K,
                    a->dataAs<double>() + aOff,
                    b->dataAs<double>() + bOff,
                    out->dataAs<double>() + batch * cSize);
            } else if (compute == TypeCode::Int32) {
                integerGemm<int32_t>(
                    plan.M,
                    plan.N,
                    plan.K,
                    a->dataAs<int32_t>() + aOff,
                    b->dataAs<int32_t>() + bOff,
                    out->dataAs<int32_t>() + batch * cSize);
            } else {
                integerGemm<int64_t>(
                    plan.M,
                    plan.N,
                    plan.K,
                    a->dataAs<int64_t>() + aOff,
                    b->dataAs<int64_t>() + bOff,
                    out->dataAs<int64_t>() + batch * cSize);
            }
        }
    });
    if (compute != outType) {
        return cast(out, outType, allocator);
    }
    return out;
}

Array *linear(
    const Array *x, const Array *weight, const Array *bias,
    mm::IAllocator &allocator) {
    if (weight->rank() != 2) {
        throw std::invalid_argument("linear expects a rank-2 weight [in, out]");
    }
    if (x->rank() == 0 || x->dim(x->rank() - 1) != weight->dim(0)) {
        throw ShapeError("linear: input feature dimension does not match weight rows");
    }
    const int64_t out = weight->dim(1);
    if (bias && !(bias->rank() == 1 && bias->dim(0) == out)) {
        throw ShapeError("linear expects a rank-1 bias [out]");
    }
    const bool allFloat = x->dtype() == TypeCode::Float32 && weight->dtype() == TypeCode::Float32 &&
                          (!bias || bias->dtype() == TypeCode::Float32);
    if (!allFloat) {
        Array *product = matmul(x, weight, allocator);
        return bias ? binary(BinaryOp::Add, Operand::of(product), Operand::of(bias), allocator)
                    : product;
    }
    const int64_t in = weight->dim(0);
    const int64_t M  = static_cast<int64_t>(x->numel()) / std::max<int64_t>(in, 1);
    Shape outShape   = x->shapeVector();
    outShape.back()  = out;
    Array *y  = createTensor(TypeCode::Float32, outShape, allocator);
    float *dst       = y->dataAs<float>();
    float beta       = 0.0f;
    if (bias) {
        const float *pb = bias->dataAs<float>();
        for (int64_t i = 0; i < M; ++i) {
            std::memcpy(dst + i * out, pb, static_cast<size_t>(out) * sizeof(float));
        }
        beta = 1.0f;
    }
    sgemm(
        false,
        false,
        M,
        out,
        in,
        1.0f,
        x->dataAs<float>(),
        in,
        weight->dataAs<float>(),
        out,
        beta,
        dst,
        out);
    return y;
}

} // namespace camel::tensor::kernels
