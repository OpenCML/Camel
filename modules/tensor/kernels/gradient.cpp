// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Gradient kernels (see gradient.h).
 */

#include "gradient.h"

#include "elementwise.h"
#include "gemm.h"
#include "layout.h"
#include "parallel.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace camel::tensor::kernels {

using type::TypeCode;

namespace {

/// The gradient and update kernels compute in f32 only. An f64 or integer tensor is refused,
/// naming the argument type and the supported set, never narrowed to f32 (design principles
/// 5.1: no implicit conversion between numeric types).
const Array *asFloat(const Array *t, mm::IAllocator &allocator) {
    (void)allocator;
    if (t->dtype() != TypeCode::Float32) {
        refuseTensorElementType("the gradient kernels", t, {TypeCode::Float32});
    }
    return t;
}

std::string shapeText(std::span<const int64_t> shape) {
    std::string text = "[";
    for (size_t i = 0; i < shape.size(); ++i) {
        text += (i ? ", " : "") + std::to_string(shape[i]);
    }
    return text + "]";
}

/// Strides of `small` (right-aligned against `big`, broadcast axes stride 0), or throws when
/// `small` does not broadcast to `big`.
std::vector<int64_t>
broadcastStrides(std::span<const int64_t> small, std::span<const int64_t> big, const char *what) {
    if (small.size() > big.size()) {
        throw ShapeError(
            std::string(what) + ": cannot broadcast " + shapeText(small) + " to " + shapeText(big));
    }
    std::vector<int64_t> strides(big.size(), 0);
    int64_t stride     = 1;
    const size_t shift = big.size() - small.size();
    for (size_t d = small.size(); d-- > 0;) {
        const int64_t extent = small[d];
        if (extent != 1 && extent != big[d + shift]) {
            throw ShapeError(
                std::string(what) + ": cannot broadcast " + shapeText(small) + " to " +
                shapeText(big));
        }
        strides[d + shift] = extent == 1 ? 0 : stride;
        stride *= extent;
    }
    return strides;
}

/// Calls fn(bigIndex, smallIndex) for every element of `big`, where smallIndex is the element
/// of the broadcast operand it reads.
template <typename Fn>
void forEachBroadcast(std::span<const int64_t> big, const std::vector<int64_t> &strides, Fn &&fn) {
    const size_t rank = big.size();
    const uint64_t n  = numelOf(big);
    std::vector<int64_t> counter(rank, 0);
    int64_t small = 0;
    for (uint64_t i = 0; i < n; ++i) {
        fn(i, small);
        for (size_t d = rank; d-- > 0;) {
            if (++counter[d] < big[d]) {
                small += strides[d];
                break;
            }
            small -= strides[d] * (big[d] - 1);
            counter[d] = 0;
        }
    }
}

} // namespace

Array *
sumTo(const Array *g, std::span<const int64_t> shape, mm::IAllocator &allocator) {
    if (g->shapeSpan().size() == shape.size() &&
        std::equal(shape.begin(), shape.end(), g->shapeSpan().begin())) {
        return const_cast<Array *>(g);
    }
    const Array *src = asFloat(g, allocator);
    const auto strides      = broadcastStrides(shape, src->shapeSpan(), "sum_to");
    Array *out       = createTensor(TypeCode::Float32, shape, allocator, true);
    const float *in         = src->dataAs<float>();
    float *acc              = out->dataAs<float>();
    const auto big          = src->shapeSpan();
    // Common case (bias gradients): only leading axes are reduced, so the result is the sum of
    // the contiguous rows of `g`.
    size_t lead = big.size();
    while (lead > 0 && strides[lead - 1] != 0) {
        --lead;
    }
    bool leadingOnly = true;
    // Every axis in the reduced prefix must actually be broadcast. A preserved
    // axis before a broadcast middle/trailing axis needs the general index walk.
    for (size_t d = 0; d < lead; ++d) {
        leadingOnly &= big[d] == 1 || strides[d] == 0;
    }
    for (size_t d = lead; d < big.size(); ++d) {
        leadingOnly &= big[d] == 1 || strides[d] != 0;
    }
    if (leadingOnly) {
        const int64_t cols = static_cast<int64_t>(numelOf(big.subspan(lead)));
        const int64_t rows = static_cast<int64_t>(numelOf(big.subspan(0, lead)));
        parallelFor(
            cols,
            std::max<int64_t>(64, grainForWork(kNsPerFloat * 2.0) / std::max<int64_t>(rows, 1)),
            [&](int64_t begin, int64_t end) {
                for (int64_t r = 0; r < rows; ++r) {
                    const float *row = in + r * cols;
                    for (int64_t c = begin; c < end; ++c) {
                        acc[c] += row[c];
                    }
                }
            });
        return out;
    }
    forEachBroadcast(big, strides, [&](uint64_t i, int64_t j) { acc[j] += in[i]; });
    return out;
}

/// A pure layout op: every element type is copied as it is, so a bool or int64 tensor broadcasts
/// without being converted (the element it is written with is the element it keeps).
Array *
broadcastTo(const Array *t, std::span<const int64_t> shape, mm::IAllocator &allocator) {
    const auto strides = broadcastStrides(t->shapeSpan(), shape, "broadcast");
    Array *out         = createTensor(t->dtype(), shape, allocator);
    dispatchDType(t->dtype(), [&]<typename T>() {
        const T *in = t->dataAs<T>();
        T *dst      = out->dataAs<T>();
        forEachBroadcast(shape, strides, [&](uint64_t i, int64_t j) { dst[i] = in[j]; });
    });
    return out;
}

Array *fill(double value, TypeCode dtype, std::span<const int64_t> shape, mm::IAllocator &allocator) {
    return full(dtype, shape, value, allocator);
}

Array *expandAxis(
    const Array *g, std::span<const int64_t> shape, int64_t axis, bool keepDims,
    mm::IAllocator &allocator) {
    const auto rank = static_cast<int64_t>(shape.size());
    const int64_t a = axis < 0 ? axis + rank : axis;
    if (a < 0 || a >= rank) {
        throw std::invalid_argument("reduction gradient axis out of range");
    }
    Shape reduced(shape.begin(), shape.end());
    reduced[static_cast<size_t>(a)] = 1;
    const Array *kept        = keepDims ? g : reshape(g, reduced, allocator);
    return broadcastTo(kept, shape, allocator);
}

Array *geluGrad(const Array *x, const Array *dy, mm::IAllocator &allocator) {
    const Array *xs = asFloat(x, allocator);
    const Array *gs = asFloat(dy, allocator);
    if (!xs->sameShape(gs)) {
        throw ShapeError("gelu gradient: dy shape differs from the input");
    }
    const int64_t n          = static_cast<int64_t>(xs->numel());
    constexpr float kScale   = 0.7978845608028654f; // sqrt(2 / pi)
    constexpr float kCubic   = 0.044715f;
    // A polynomial element of this gradient is a few multiply-adds, about twice a plain
    // elementwise element (parallel.h, kNsPerFloat).
    const int64_t grain      = grainForWork(kNsPerFloat * 2.0);
    const float *px          = xs->dataAs<float>();
    // tanh goes through the vectorized unary kernel; the polynomial parts vectorize as they are.
    Array *inner = createTensor(TypeCode::Float32, xs->shapeSpan(), allocator);
    float *pu           = inner->dataAs<float>();
    parallelFor(n, grain, [&](int64_t begin, int64_t end) {
        for (int64_t i = begin; i < end; ++i) {
            const float v = px[i];
            pu[i]         = kScale * (v + kCubic * v * v * v);
        }
    });
    const float *pt   = unary(UnaryOp::Tanh, inner, allocator)->dataAs<float>();
    const float *pg   = gs->dataAs<float>();
    Array *out = createTensor(TypeCode::Float32, xs->shapeSpan(), allocator);
    float *po         = out->dataAs<float>();
    parallelFor(n, grain, [&](int64_t begin, int64_t end) {
        for (int64_t i = begin; i < end; ++i) {
            const float v = px[i], t = pt[i];
            const float slope = 0.5f * (1.0f + t) +
                                0.5f * v * (1.0f - t * t) * kScale * (1.0f + 3.0f * kCubic * v * v);
            po[i]             = pg[i] * slope;
        }
    });
    return out;
}

Array *erfGrad(const Array *x, const Array *dy, mm::IAllocator &allocator) {
    const Array *xs = asFloat(x, allocator);
    const Array *gs = asFloat(dy, allocator);
    if (!xs->sameShape(gs)) {
        throw ShapeError("erf gradient: dy shape differs from the input");
    }
    Array *out      = createTensor(TypeCode::Float32, xs->shapeSpan(), allocator);
    const float *px        = xs->dataAs<float>();
    const float *pg        = gs->dataAs<float>();
    float *po              = out->dataAs<float>();
    constexpr float kScale = 1.1283791670955126f; // 2 / sqrt(pi)
    for (uint64_t i = 0; i < xs->numel(); ++i) {
        po[i] = pg[i] * kScale * std::exp(-px[i] * px[i]);
    }
    return out;
}

Array *layerNormGrad(
    const Array *x, const Array *gamma, const Array *dy, double eps,
    LayerNormGrad which, mm::IAllocator &allocator) {
    const Array *xs = asFloat(x, allocator);
    const Array *ws = asFloat(gamma, allocator);
    const Array *gs = asFloat(dy, allocator);
    if (!xs->sameShape(gs)) {
        throw ShapeError("layer_norm gradient: dy shape differs from the input");
    }
    const auto width = static_cast<int64_t>(ws->numel());
    const auto rows  = static_cast<int64_t>(xs->numel()) / std::max<int64_t>(width, 1);
    const float *px  = xs->dataAs<float>();
    const float *pw  = ws->dataAs<float>();
    const float *pg  = gs->dataAs<float>();

    if (which == LayerNormGrad::Input) {
        Array *out = createTensor(TypeCode::Float32, xs->shapeSpan(), allocator);
        float *po         = out->dataAs<float>();
        parallelFor(rows, 16, [&](int64_t begin, int64_t end) {
            for (int64_t r = begin; r < end; ++r) {
                const float *row = px + r * width;
                const float *g   = pg + r * width;
                double mean = 0.0, var = 0.0;
                for (int64_t k = 0; k < width; ++k) {
                    mean += row[k];
                }
                mean /= static_cast<double>(width);
                for (int64_t k = 0; k < width; ++k) {
                    const double d = row[k] - mean;
                    var += d * d;
                }
                var /= static_cast<double>(width);
                const double inv = 1.0 / std::sqrt(var + eps);
                // dx = inv * (h - mean(h) - xhat * mean(h * xhat)), h = dy * gamma.
                double sumH = 0.0, sumHX = 0.0;
                for (int64_t k = 0; k < width; ++k) {
                    const double h = static_cast<double>(g[k]) * pw[k];
                    sumH += h;
                    sumHX += h * (row[k] - mean) * inv;
                }
                const double meanH  = sumH / static_cast<double>(width);
                const double meanHX = sumHX / static_cast<double>(width);
                float *o            = po + r * width;
                for (int64_t k = 0; k < width; ++k) {
                    const double h    = static_cast<double>(g[k]) * pw[k];
                    const double xhat = (row[k] - mean) * inv;
                    o[k]              = static_cast<float>(inv * (h - meanH - xhat * meanHX));
                }
            }
        });
        return out;
    }

    Array *out = createTensor(TypeCode::Float32, ws->shapeSpan(), allocator, true);
    float *po         = out->dataAs<float>();
    std::vector<double> acc(static_cast<size_t>(width), 0.0);
    for (int64_t r = 0; r < rows; ++r) {
        const float *row = px + r * width;
        const float *g   = pg + r * width;
        if (which == LayerNormGrad::Beta) {
            for (int64_t k = 0; k < width; ++k) {
                acc[static_cast<size_t>(k)] += g[k];
            }
            continue;
        }
        double mean = 0.0, var = 0.0;
        for (int64_t k = 0; k < width; ++k) {
            mean += row[k];
        }
        mean /= static_cast<double>(width);
        for (int64_t k = 0; k < width; ++k) {
            const double d = row[k] - mean;
            var += d * d;
        }
        var /= static_cast<double>(width);
        const double inv = 1.0 / std::sqrt(var + eps);
        for (int64_t k = 0; k < width; ++k) {
            acc[static_cast<size_t>(k)] += static_cast<double>(g[k]) * (row[k] - mean) * inv;
        }
    }
    for (int64_t k = 0; k < width; ++k) {
        po[k] = static_cast<float>(acc[static_cast<size_t>(k)]);
    }
    return out;
}

Array *sliceGrad(
    const Array *dy, std::span<const int64_t> shape, int64_t axisArg, int64_t start,
    int64_t end, int64_t step, mm::IAllocator &allocator) {
    if (step <= 0) {
        throw std::invalid_argument("slice step must be positive");
    }
    const auto rank    = static_cast<int64_t>(shape.size());
    const int64_t axis = axisArg < 0 ? axisArg + rank : axisArg;
    if (axis < 0 || axis >= rank) {
        throw std::invalid_argument("slice axis out of range");
    }
    const int64_t extent = shape[static_cast<size_t>(axis)];
    // The forward slice's bounds, normalized the same way (the forward slice faulted when they
    // were out of range, so they are in range here unless the gradient was built by hand).
    const auto range = normalizeSliceBounds(extent, start, end);
    if (!range) {
        throw std::out_of_range("slice bounds out of range");
    }
    start = range->start;
    end   = range->end;
    const int64_t count     = end > start ? (end - start + step - 1) / step : 0;
    const Array *src = asFloat(dy, allocator);
    if (src->rank() != shape.size() || src->dim(static_cast<size_t>(axis)) != count) {
        throw ShapeError("slice gradient: dy shape does not match the slice");
    }
    Array *out = createTensor(TypeCode::Float32, shape, allocator, true);
    uint64_t outer = 1, inner = 1;
    for (int64_t d = 0; d < axis; ++d) {
        outer *= static_cast<uint64_t>(shape[static_cast<size_t>(d)]);
    }
    for (int64_t d = axis + 1; d < rank; ++d) {
        inner *= static_cast<uint64_t>(shape[static_cast<size_t>(d)]);
    }
    const float *in = src->dataAs<float>();
    float *dst      = out->dataAs<float>();
    for (uint64_t o = 0; o < outer; ++o) {
        for (int64_t k = 0; k < count; ++k) {
            const uint64_t to =
                (o * static_cast<uint64_t>(extent) + static_cast<uint64_t>(start + k * step)) *
                inner;
            const uint64_t from =
                (o * static_cast<uint64_t>(count) + static_cast<uint64_t>(k)) * inner;
            std::copy(in + from, in + from + inner, dst + to);
        }
    }
    return out;
}

namespace {

/// Operands of matmul viewed as matrices: vectors get the unit axis matmul gives them, and dy
/// gets it back.
struct MatmulView {
    const Array *lhs;
    const Array *rhs;
    const Array *dy;
};

MatmulView matrixView(
    const Array *dy, const Array *lhs, const Array *rhs,
    mm::IAllocator &allocator) {
    MatmulView view{asFloat(lhs, allocator), asFloat(rhs, allocator), asFloat(dy, allocator)};
    Shape dyShape = view.dy->shapeVector();
    if (lhs->rank() == 1) {
        view.lhs = reshape(view.lhs, Shape{1, lhs->dim(0)}, allocator);
        dyShape.insert(dyShape.end() - (rhs->rank() == 1 ? 0 : 1), 1);
    }
    if (rhs->rank() == 1) {
        view.rhs = reshape(view.rhs, Shape{rhs->dim(0), 1}, allocator);
        dyShape.push_back(1);
    }
    view.dy = reshape(view.dy, dyShape, allocator);
    return view;
}

} // namespace

namespace {

enum class MatmulOperand { Lhs, Rhs };

/// The gradient of a float matmul operand as GEMMs over the operands' storage (sgemm reads the
/// transposes in place), or nullptr for shapes this does not cover: vectors and broadcast batches.
///   rhs a matrix:        lhs viewed as [M, K] with M the product of its leading axes; one GEMM.
///   equal batch shapes:  one GEMM per batch matrix.
Array *matmulGradGemm(
    MatmulOperand which, const Array *dy, const Array *lhs, const Array *rhs,
    mm::IAllocator &allocator) {
    if (lhs->dtype() != TypeCode::Float32 || rhs->dtype() != TypeCode::Float32 ||
        dy->dtype() != TypeCode::Float32 || lhs->rank() < 2 || rhs->rank() < 2) {
        return nullptr;
    }
    const size_t lr = lhs->rank(), rr = rhs->rank();
    const int64_t K = lhs->dim(lr - 1), N = rhs->dim(rr - 1);
    int64_t batches = 1, M = lhs->dim(lr - 2);
    if (rr == 2) {
        M = static_cast<int64_t>(lhs->numel()) / std::max<int64_t>(K, 1);
    } else {
        if (lr != rr || !std::equal(
                            lhs->shapeSpan().begin(),
                            lhs->shapeSpan().end() - 2,
                            rhs->shapeSpan().begin())) {
            return nullptr;
        }
        batches = static_cast<int64_t>(numelOf(lhs->shapeSpan().first(lr - 2)));
    }
    if (static_cast<int64_t>(dy->numel()) != batches * M * N) {
        throw ShapeError("matmul gradient: dy shape does not match the product");
    }
    const float *a = lhs->dataAs<float>(), *b = rhs->dataAs<float>(), *g = dy->dataAs<float>();
    const bool forLhs = which == MatmulOperand::Lhs;
    Array *out =
        createTensor(TypeCode::Float32, (forLhs ? lhs : rhs)->shapeSpan(), allocator);
    float *o            = out->dataAs<float>();
    const int64_t aSize = M * K, bSize = K * N, gSize = M * N;
    parallelFor(batches, 1, [&](int64_t begin, int64_t end) {
        for (int64_t i = begin; i < end; ++i) {
            if (forLhs) { // dA = dY @ B^T : [M, K]
                sgemm(
                    false,
                    true,
                    M,
                    K,
                    N,
                    1.0f,
                    g + i * gSize,
                    N,
                    b + i * bSize,
                    N,
                    0.0f,
                    o + i * aSize,
                    K);
            } else { // dB = A^T @ dY : [K, N]
                sgemm(
                    true,
                    false,
                    K,
                    N,
                    M,
                    1.0f,
                    a + i * aSize,
                    K,
                    g + i * gSize,
                    N,
                    0.0f,
                    o + i * bSize,
                    N);
            }
        }
    });
    return out;
}

} // namespace

Array *matmulGradLhs(
    const Array *dy, const Array *lhs, const Array *rhs,
    mm::IAllocator &allocator) {
    if (Array *g = matmulGradGemm(MatmulOperand::Lhs, dy, lhs, rhs, allocator)) {
        return g;
    }
    const MatmulView v = matrixView(dy, lhs, rhs, allocator);
    Array *g    = matmul(v.dy, transposeLast2(v.rhs, allocator), allocator);
    g                  = sumTo(g, v.lhs->shapeSpan(), allocator);
    return reshape(g, lhs->shapeSpan(), allocator);
}

Array *matmulGradRhs(
    const Array *dy, const Array *lhs, const Array *rhs,
    mm::IAllocator &allocator) {
    if (Array *g = matmulGradGemm(MatmulOperand::Rhs, dy, lhs, rhs, allocator)) {
        return g;
    }
    const MatmulView v = matrixView(dy, lhs, rhs, allocator);
    Array *g    = matmul(transposeLast2(v.lhs, allocator), v.dy, allocator);
    g                  = sumTo(g, v.rhs->shapeSpan(), allocator);
    return reshape(g, rhs->shapeSpan(), allocator);
}

} // namespace camel::tensor::kernels
