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
 * Algorithm (Goto-style):
 *   for each K block of depth KC:
 *     pack B[KC x N] into NR-wide column panels (zero padded)
 *     for each (MC x NC) output tile, in parallel:
 *       pack A[MC x KC] into MR-tall row panels (zero padded)
 *       run the MR x NR micro-kernel over the tile
 * The micro-kernel keeps an MR x NR accumulator block in vector registers
 * using GCC/Clang vector extensions. Transposed operands are handled entirely
 * by the packing routines.
 */

#include "parallel.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

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
constexpr int64_t kNC = 256; // multiple of both NR choices

typedef float vfloat __attribute__((vector_size(kVecWidth * sizeof(float))));

inline vfloat loadVec(const float *p) {
    vfloat v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

inline void storeVec(float *p, vfloat v) { std::memcpy(p, &v, sizeof(v)); }

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

/// Packs B[pc:pc+kc, 0:N] as panels of NR columns: panel p holds kc rows of NR floats.
void packB(const float *B, int64_t ldb, bool transB, int64_t pc, int64_t kc, int64_t N, float *out) {
    const int64_t panels = (N + kNR - 1) / kNR;
    parallelFor(panels, 8, [&](int64_t begin, int64_t end) {
        for (int64_t p = begin; p < end; ++p) {
            float *dst       = out + p * kc * kNR;
            const int64_t j0 = p * kNR;
            const int64_t nr = std::min<int64_t>(kNR, N - j0);
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
    });
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

/// C[mr x nr] += alpha * Ap(panel) * Bp(panel). Full tiles store directly; edges go through a buffer.
void microKernel(
    int64_t kc, const float *Ap, const float *Bp, float *C, int64_t ldc, float alpha, int64_t mr,
    int64_t nr) {
    vfloat acc[kMR][kNVec];
    for (int i = 0; i < kMR; ++i) {
        for (int v = 0; v < kNVec; ++v) {
            acc[i][v] = splat(0.0f);
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
                acc[i][v] += av * b[v];
            }
        }
    }
    const vfloat alphaVec = splat(alpha);
    if (mr == kMR && nr == kNR) {
        for (int i = 0; i < kMR; ++i) {
            for (int v = 0; v < kNVec; ++v) {
                float *dst = C + i * ldc + v * kVecWidth;
                storeVec(dst, loadVec(dst) + alphaVec * acc[i][v]);
            }
        }
        return;
    }
    alignas(64) float tile[kMR][kNR];
    for (int i = 0; i < kMR; ++i) {
        for (int v = 0; v < kNVec; ++v) {
            storeVec(&tile[i][v * kVecWidth], alphaVec * acc[i][v]);
        }
    }
    for (int64_t i = 0; i < mr; ++i) {
        for (int64_t j = 0; j < nr; ++j) {
            C[i * ldc + j] += tile[i][j];
        }
    }
}

void scaleC(int64_t M, int64_t N, float beta, float *C, int64_t ldc) {
    if (beta == 1.0f) {
        return;
    }
    parallelFor(M, std::max<int64_t>(1, 16384 / std::max<int64_t>(N, 1)), [&](int64_t begin, int64_t end) {
        for (int64_t i = begin; i < end; ++i) {
            float *row = C + i * ldc;
            if (beta == 0.0f) {
                std::fill_n(row, N, 0.0f);
            } else {
                for (int64_t j = 0; j < N; ++j) {
                    row[j] *= beta;
                }
            }
        }
    });
}

} // namespace

void CAMEL_GEMM_ENTRY(
    bool transA, bool transB, int64_t M, int64_t N, int64_t K, float alpha, const float *A,
    int64_t lda, const float *B, int64_t ldb, float beta, float *C, int64_t ldc) {
    scaleC(M, N, beta, C, ldc);
    if (M == 0 || N == 0 || K == 0 || alpha == 0.0f) {
        return;
    }
    const int64_t nPanels = (N + kNR - 1) / kNR;
    std::vector<float> packedB(static_cast<size_t>(nPanels * kNR * std::min(kKC, K)));
    const int64_t mTiles = (M + kMC - 1) / kMC;
    const int64_t nTiles = (N + kNC - 1) / kNC;

    for (int64_t pc = 0; pc < K; pc += kKC) {
        const int64_t kc = std::min(kKC, K - pc);
        packB(B, ldb, transB, pc, kc, N, packedB.data());
        parallelFor(mTiles * nTiles, 1, [&](int64_t begin, int64_t end) {
            std::vector<float> packedA(static_cast<size_t>(((kMC + kMR - 1) / kMR) * kMR * kc));
            int64_t packedFor = -1; // m-tile currently held in packedA
            for (int64_t t = begin; t < end; ++t) {
                const int64_t mt = t / nTiles;
                const int64_t nt = t % nTiles;
                const int64_t ic = mt * kMC;
                const int64_t mc = std::min(kMC, M - ic);
                if (packedFor != mt) {
                    packA(A, lda, transA, ic, mc, pc, kc, packedA.data());
                    packedFor = mt;
                }
                const int64_t jc = nt * kNC;
                const int64_t nc = std::min(kNC, N - jc);
                for (int64_t jr = 0; jr < nc; jr += kNR) {
                    const float *Bp = packedB.data() + ((jc + jr) / kNR) * kc * kNR;
                    const int64_t nr = std::min<int64_t>(kNR, nc - jr);
                    for (int64_t ir = 0; ir < mc; ir += kMR) {
                        const float *Ap  = packedA.data() + (ir / kMR) * kc * kMR;
                        const int64_t mr = std::min<int64_t>(kMR, mc - ir);
                        microKernel(kc, Ap, Bp, C + (ic + ir) * ldc + jc + jr, ldc, alpha, mr, nr);
                    }
                }
            }
        });
    }
}


} // namespace camel::tensor::kernels::detail
