/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Sep. 28, 2026
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Blocked, packed, register-tiled SGEMM body shared by the per-ISA
 * translation units (gemm_generic.cpp, gemm_avx2.cpp). It is textually
 * included so each unit compiles the same algorithm with its own vector width
 * and target flags; it is not a public header.
 *
 * The arithmetic is the canonical algorithm of matmul (design principles 2.13):
 * every element of the product is one chain of fused multiply-adds in ascending
 * k, starting from +0,
 *
 *     acc = +0;  for k = 0 .. K-1:  acc = fma(A[i][k], B[k][j], acc)
 *
 * and only then combined with C: C = alpha * acc (beta == 0) or
 * C = alpha * acc + beta * C, each step rounded as written. The fused multiply-
 * add is written explicitly (fmaVec), and the units that include this file are
 * compiled with contraction off, so the bits are the same on every processor:
 * with FMA hardware it is one instruction, without it the C library's correctly
 * rounded fmaf (slow, but such x86-64 processors predate 2013).
 *
 * Blocking (Goto-style) changes where the work happens, never the order of the
 * steps of one chain:
 *   for each (MC x NC) output tile, in parallel:
 *     for each K block of depth KC, in ascending order:
 *       pack the tile's B[KC x NC] columns into NR-wide panels (zero padded)
 *       pack the tile's A[MC x KC] rows into MR-tall panels (zero padded)
 *       run the MR x NR micro-kernel over the tile, continuing each chain from
 *       the value the previous K block left in the tile's carry buffer
 * The micro-kernel keeps an MR x NR accumulator block in vector registers
 * using GCC/Clang vector extensions. Transposed operands are handled entirely
 * by the packing routines. The zero padding only fills lanes (rows beyond mr,
 * columns beyond nr) whose results are never stored; k is never padded.
 */

#include "parallel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>

namespace camel::tensor::kernels::detail {

namespace {

// The including translation unit defines CAMEL_GEMM_VEC_WIDTH / _MR / _NR and
// CAMEL_GEMM_ENTRY (the exported function name) before including this file.
constexpr int kVecWidth = CAMEL_GEMM_VEC_WIDTH;
constexpr int kMR       = CAMEL_GEMM_MR;
constexpr int kNR       = CAMEL_GEMM_NR;
constexpr int kNVec   = kNR / kVecWidth;
constexpr int64_t kKC = 256;
constexpr int64_t kMC = 72;  // multiple of both MR choices
constexpr int64_t kNC = 256; // default column-tile width; multiple of both NR choices
// Arithmetic per worker below which adding a thread does not pay off (~10 us of FMA work).
constexpr double kFlopsPerWorker = 2.0 * 1024 * 1024;

typedef float vfloat __attribute__((vector_size(kVecWidth * sizeof(float))));

inline vfloat loadVec(const float *p) {
    vfloat v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

inline void storeVec(float *p, vfloat v) { std::memcpy(p, &v, sizeof(v)); }

/// acc + a * b with one rounding, lane by lane: one step of matmul's canonical algorithm.
inline vfloat fmaVec(vfloat a, vfloat b, vfloat acc) {
#if defined(__has_builtin) && __has_builtin(__builtin_elementwise_fma)
    return __builtin_elementwise_fma(a, b, acc);
#else
    vfloat r;
    for (int i = 0; i < kVecWidth; ++i) {
        r[i] = std::fma(a[i], b[i], acc[i]);
    }
    return r;
#endif
}

inline vfloat splat(float x) {
    vfloat v;
    for (int i = 0; i < kVecWidth; ++i) {
        v[i] = x;
    }
    return v;
}

inline float elemA(const float *A, int64_t lda, bool transA, int64_t i, int64_t k) {
    return transA ? A[k * lda + i] : A[i * lda + k];
}

inline float elemB(const float *B, int64_t ldb, bool transB, int64_t k, int64_t j) {
    return transB ? B[j * ldb + k] : B[k * ldb + j];
}

/// Packs B[pc:pc+kc, jc:jc+nc] as panels of NR columns: panel p holds kc rows of NR floats.
void packB(
    const float *B, int64_t ldb, bool transB, int64_t pc, int64_t kc, int64_t jc, int64_t nc,
    float *out) {
    const int64_t panels = (nc + kNR - 1) / kNR;
    for (int64_t p = 0; p < panels; ++p) {
        float *dst       = out + p * kc * kNR;
        const int64_t j0 = jc + p * kNR;
        const int64_t nr = std::min<int64_t>(kNR, jc + nc - j0);
        for (int64_t k = 0; k < kc; ++k) {
            if (!transB && nr == kNR) {
                std::memcpy(dst + k * kNR, B + (pc + k) * ldb + j0, kNR * sizeof(float));
                continue;
            }
            for (int64_t j = 0; j < kNR; ++j) {
                dst[k * kNR + j] = j < nr ? elemB(B, ldb, transB, pc + k, j0 + j) : 0.0f;
            }
        }
    }
}

/// Packs A[ic:ic+mc, pc:pc+kc] as panels of MR rows: panel p holds kc columns of MR floats.
void packA(
    const float *A, int64_t lda, bool transA, int64_t ic, int64_t mc, int64_t pc, int64_t kc,
    float *out) {
    const int64_t panels = (mc + kMR - 1) / kMR;
    for (int64_t p = 0; p < panels; ++p) {
        float *dst       = out + p * kc * kMR;
        const int64_t i0 = p * kMR;
        const int64_t mr = std::min<int64_t>(kMR, mc - i0);
        for (int64_t k = 0; k < kc; ++k) {
            for (int64_t i = 0; i < kMR; ++i) {
                dst[k * kMR + i] = i < mr ? elemA(A, lda, transA, ic + i0 + i, pc + k) : 0.0f;
            }
        }
    }
}

/// The last step of an element: C = alpha * acc (beta == 0, C is not read) or
/// C = alpha * acc + beta * C. With beta == 1 the second form is the addition of the
/// unfused `matmul(x, w) + bias` (the product on the left), so linear and matmul_add give
/// the bits of that expression.
inline float finish(float acc, float c, float alpha, float beta) {
    const float product = alpha * acc;
    if (beta == 0.0f) {
        return product;
    }
    return beta == 1.0f ? product + c : product + beta * c;
}

/**
 * Runs the chains of one MR x NR block over the kc steps of one K block.
 *
 * `carry` holds the block's MR x NR accumulators between K blocks (contiguous, row-major,
 * padding lanes included). The first K block starts every chain from +0, a later one from
 * `carry`; a block that is not the last stores the accumulators back to `carry`, the last one
 * finishes them into C (only the mr x nr elements that exist).
 */
void microKernel(
    int64_t kc, const float *Ap, const float *Bp, float *carry, bool first, bool last, float *C,
    int64_t ldc, float alpha, float beta, int64_t mr, int64_t nr) {
    vfloat acc[kMR][kNVec];
    for (int i = 0; i < kMR; ++i) {
        for (int v = 0; v < kNVec; ++v) {
            acc[i][v] = first ? splat(0.0f) : loadVec(carry + i * kNR + v * kVecWidth);
        }
    }
    for (int64_t k = 0; k < kc; ++k) {
        vfloat b[kNVec];
        for (int v = 0; v < kNVec; ++v) {
            b[v] = loadVec(Bp + k * kNR + v * kVecWidth);
        }
        const float *a = Ap + k * kMR;
        for (int i = 0; i < kMR; ++i) {
            const vfloat av = splat(a[i]);
            for (int v = 0; v < kNVec; ++v) {
                acc[i][v] = fmaVec(av, b[v], acc[i][v]);
            }
        }
    }
    if (!last) {
        for (int i = 0; i < kMR; ++i) {
            for (int v = 0; v < kNVec; ++v) {
                storeVec(carry + i * kNR + v * kVecWidth, acc[i][v]);
            }
        }
        return;
    }
    if (mr == kMR && nr == kNR && beta == 0.0f) {
        const vfloat alphaVec = splat(alpha);
        for (int i = 0; i < kMR; ++i) {
            for (int v = 0; v < kNVec; ++v) {
                storeVec(C + i * ldc + v * kVecWidth, alphaVec * acc[i][v]);
            }
        }
        return;
    }
    alignas(64) float tile[kMR][kNR];
    for (int i = 0; i < kMR; ++i) {
        for (int v = 0; v < kNVec; ++v) {
            storeVec(&tile[i][v * kVecWidth], acc[i][v]);
        }
    }
    for (int64_t i = 0; i < mr; ++i) {
        for (int64_t j = 0; j < nr; ++j) {
            C[i * ldc + j] = finish(tile[i][j], C[i * ldc + j], alpha, beta);
        }
    }
}

/// Per-thread packing buffer that only grows. Packing writes every element the micro-kernel
/// reads, so the storage is neither zero-filled nor reallocated between calls (for the small
/// products of inference, allocating and clearing fresh buffers cost more than the arithmetic).
float *scratch(std::unique_ptr<float[]> &buffer, size_t &capacity, size_t count) {
    if (capacity < count) {
        buffer.reset(new float[count]);
        capacity = count;
    }
    return buffer.get();
}

/// Finishes the C tile [ic:ic+mc, jc:jc+nc] when there is no product to accumulate (K == 0, or
/// alpha == 0, where A and B are not read): every chain is its starting value +0.
void finishEmptyTile(
    float *C, int64_t ldc, int64_t ic, int64_t mc, int64_t jc, int64_t nc, float alpha,
    float beta) {
    for (int64_t i = ic; i < ic + mc; ++i) {
        float *row = C + i * ldc + jc;
        for (int64_t j = 0; j < nc; ++j) {
            row[j] = finish(0.0f, row[j], alpha, beta);
        }
    }
}

} // namespace

void CAMEL_GEMM_ENTRY(
    bool transA, bool transB, int64_t M, int64_t N, int64_t K, float alpha, const float *A,
    int64_t lda, const float *B, int64_t ldb, float beta, float *C, int64_t ldc) {
    if (M == 0 || N == 0) {
        return;
    }
    // Use only as many threads as the work pays for: waking a worker costs a few microseconds,
    // more than a whole small inference product (e.g. 32x64x128) takes on one core.
    const double flops    = 2.0 * static_cast<double>(M) * static_cast<double>(N) * K;
    const int64_t workers = std::clamp<int64_t>(
        static_cast<int64_t>(flops / kFlopsPerWorker),
        1,
        std::max(1, numThreads()));

    // Tile the output so every worker has work: with few row blocks (small M,
    // typical for inference batches) the column blocks are narrowed, in NR
    // multiples, until there are at least as many tiles as workers.
    const int64_t mTiles = (M + kMC - 1) / kMC;
    int64_t ncTile       = kNC;
    if (mTiles * ((N + kNC - 1) / kNC) < workers) {
        const int64_t wanted = (workers + mTiles - 1) / mTiles;
        const int64_t width  = (N + wanted - 1) / wanted;
        ncTile               = std::max<int64_t>(kNR, (width + kNR - 1) / kNR * kNR);
    }
    const int64_t nTiles = (N + ncTile - 1) / ncTile;
    const int64_t tiles  = mTiles * nTiles;
    // parallelFor splits into chunks of at least `grain` tiles; this grain yields `workers` chunks.
    const int64_t grain = std::max<int64_t>(1, tiles / workers);

    // One parallel region for the whole product: each task owns an output tile
    // and walks all K blocks, packing just the A rows and B columns it needs.
    // Re-packing across tiles costs O(1/MC + 1/ncTile) of the arithmetic.
    parallelFor(tiles, workers == 1 ? tiles : grain, [&](int64_t begin, int64_t end) {
        const int64_t kcMax = std::min(kKC, K);
        thread_local std::unique_ptr<float[]> bufferA, bufferB;
        thread_local size_t capacityA = 0, capacityB = 0;
        float *packedA = scratch(
            bufferA,
            capacityA,
            static_cast<size_t>(((kMC + kMR - 1) / kMR) * kMR * kcMax));
        float *packedB = scratch(
            bufferB,
            capacityB,
            static_cast<size_t>(((ncTile + kNR - 1) / kNR) * kNR * kcMax));
        // The accumulators of a whole tile between K blocks (only when there is more than one
        // K block): one MR x NR block per micro tile.
        const int64_t rowBlocks = (kMC + kMR - 1) / kMR;
        const int64_t colBlocks = (ncTile + kNR - 1) / kNR;
        thread_local std::unique_ptr<float[]> bufferCarry;
        thread_local size_t capacityCarry = 0;
        float *carry                      = nullptr;
        if (K > kKC) {
            carry = scratch(
                bufferCarry,
                capacityCarry,
                static_cast<size_t>(rowBlocks * colBlocks * kMR * kNR));
        }
        for (int64_t t = begin; t < end; ++t) {
            const int64_t ic = (t / nTiles) * kMC;
            const int64_t mc = std::min(kMC, M - ic);
            const int64_t jc = (t % nTiles) * ncTile;
            const int64_t nc = std::min(ncTile, N - jc);
            if (alpha == 0.0f || K == 0) {
                finishEmptyTile(C, ldc, ic, mc, jc, nc, alpha, beta);
                continue;
            }
            for (int64_t pc = 0; pc < K; pc += kKC) {
                const int64_t kc = std::min(kKC, K - pc);
                const bool first = pc == 0;
                const bool last  = pc + kc == K;
                packB(B, ldb, transB, pc, kc, jc, nc, packedB);
                packA(A, lda, transA, ic, mc, pc, kc, packedA);
                for (int64_t jr = 0; jr < nc; jr += kNR) {
                    const float *Bp  = packedB + (jr / kNR) * kc * kNR;
                    const int64_t nr = std::min<int64_t>(kNR, nc - jr);
                    for (int64_t ir = 0; ir < mc; ir += kMR) {
                        const float *Ap  = packedA + (ir / kMR) * kc * kMR;
                        const int64_t mr = std::min<int64_t>(kMR, mc - ir);
                        float *blockCarry =
                            carry ? carry + ((ir / kMR) * colBlocks + jr / kNR) * kMR * kNR
                                  : nullptr;
                        microKernel(
                            kc,
                            Ap,
                            Bp,
                            blockCarry,
                            first,
                            last,
                            C + (ic + ir) * ldc + jc + jr,
                            ldc,
                            alpha,
                            beta,
                            mr,
                            nr);
                    }
                }
            }
        }
    });
}

} // namespace camel::tensor::kernels::detail
