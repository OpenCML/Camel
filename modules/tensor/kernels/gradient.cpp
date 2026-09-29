/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
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
 * Created: Sep. 29, 2026
 * Updated: Sep. 29, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Gradient kernels (see gradient.h).
 */

#include "gradient.h"

#include "elementwise.h"
#include "gemm.h"
#include "layout.h"
#include "parallel.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace camel::tensor::kernels {

using type::TypeCode;

namespace {

const TensorObject *asFloat(const TensorObject *t, mm::IAllocator &allocator) {
    return t->dtype() == TypeCode::Float32 ? t : cast(t, TypeCode::Float32, allocator);
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

TensorObject *
sumTo(const TensorObject *g, std::span<const int64_t> shape, mm::IAllocator &allocator) {
    if (g->shapeSpan().size() == shape.size() &&
        std::equal(shape.begin(), shape.end(), g->shapeSpan().begin())) {
        return const_cast<TensorObject *>(g);
    }
    const TensorObject *src = asFloat(g, allocator);
    const auto strides      = broadcastStrides(shape, src->shapeSpan(), "sum_to");
    TensorObject *out       = TensorObject::create(TypeCode::Float32, shape, allocator, true);
    const float *in         = src->dataAs<float>();
    float *acc              = out->dataAs<float>();
    forEachBroadcast(src->shapeSpan(), strides, [&](uint64_t i, int64_t j) { acc[j] += in[i]; });
    return out;
}

TensorObject *
broadcastTo(const TensorObject *t, std::span<const int64_t> shape, mm::IAllocator &allocator) {
    const TensorObject *src = asFloat(t, allocator);
    const auto strides      = broadcastStrides(src->shapeSpan(), shape, "broadcast");
    TensorObject *out       = TensorObject::create(TypeCode::Float32, shape, allocator);
    const float *in         = src->dataAs<float>();
    float *dst              = out->dataAs<float>();
    forEachBroadcast(shape, strides, [&](uint64_t i, int64_t j) { dst[i] = in[j]; });
    return out;
}

TensorObject *fill(double value, std::span<const int64_t> shape, mm::IAllocator &allocator) {
    return full(TypeCode::Float32, shape, value, allocator);
}

TensorObject *expandAxis(
    const TensorObject *g, std::span<const int64_t> shape, int64_t axis, bool keepDims,
    mm::IAllocator &allocator) {
    const auto rank = static_cast<int64_t>(shape.size());
    const int64_t a = axis < 0 ? axis + rank : axis;
    if (a < 0 || a >= rank) {
        throw std::invalid_argument("reduction gradient axis out of range");
    }
    Shape reduced(shape.begin(), shape.end());
    reduced[static_cast<size_t>(a)] = 1;
    const TensorObject *kept        = keepDims ? g : reshape(g, reduced, allocator);
    return broadcastTo(kept, shape, allocator);
}

TensorObject *geluGrad(const TensorObject *x, const TensorObject *dy, mm::IAllocator &allocator) {
    const TensorObject *xs = asFloat(x, allocator);
    const TensorObject *gs = asFloat(dy, allocator);
    if (!xs->sameShape(gs)) {
        throw ShapeError("gelu gradient: dy shape differs from the input");
    }
    TensorObject *out      = TensorObject::create(TypeCode::Float32, xs->shapeSpan(), allocator);
    const float *px        = xs->dataAs<float>();
    const float *pg        = gs->dataAs<float>();
    float *po              = out->dataAs<float>();
    constexpr float kScale = 0.7978845608028654f; // sqrt(2 / pi)
    constexpr float kCubic = 0.044715f;
    parallelFor(static_cast<int64_t>(xs->numel()), 4096, [&](int64_t begin, int64_t end) {
        for (int64_t i = begin; i < end; ++i) {
            const float v     = px[i];
            const float t     = std::tanh(kScale * (v + kCubic * v * v * v));
            const float slope = 0.5f * (1.0f + t) +
                                0.5f * v * (1.0f - t * t) * kScale * (1.0f + 3.0f * kCubic * v * v);
            po[i] = pg[i] * slope;
        }
    });
    return out;
}

TensorObject *erfGrad(const TensorObject *x, const TensorObject *dy, mm::IAllocator &allocator) {
    const TensorObject *xs = asFloat(x, allocator);
    const TensorObject *gs = asFloat(dy, allocator);
    if (!xs->sameShape(gs)) {
        throw ShapeError("erf gradient: dy shape differs from the input");
    }
    TensorObject *out      = TensorObject::create(TypeCode::Float32, xs->shapeSpan(), allocator);
    const float *px        = xs->dataAs<float>();
    const float *pg        = gs->dataAs<float>();
    float *po              = out->dataAs<float>();
    constexpr float kScale = 1.1283791670955126f; // 2 / sqrt(pi)
    for (uint64_t i = 0; i < xs->numel(); ++i) {
        po[i] = pg[i] * kScale * std::exp(-px[i] * px[i]);
    }
    return out;
}

TensorObject *layerNormGrad(
    const TensorObject *x, const TensorObject *gamma, const TensorObject *dy, double eps,
    LayerNormGrad which, mm::IAllocator &allocator) {
    const TensorObject *xs = asFloat(x, allocator);
    const TensorObject *ws = asFloat(gamma, allocator);
    const TensorObject *gs = asFloat(dy, allocator);
    if (!xs->sameShape(gs)) {
        throw ShapeError("layer_norm gradient: dy shape differs from the input");
    }
    const auto width = static_cast<int64_t>(ws->numel());
    const auto rows  = static_cast<int64_t>(xs->numel()) / std::max<int64_t>(width, 1);
    const float *px  = xs->dataAs<float>();
    const float *pw  = ws->dataAs<float>();
    const float *pg  = gs->dataAs<float>();

    if (which == LayerNormGrad::Input) {
        TensorObject *out = TensorObject::create(TypeCode::Float32, xs->shapeSpan(), allocator);
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

    TensorObject *out = TensorObject::create(TypeCode::Float32, ws->shapeSpan(), allocator, true);
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

TensorObject *sliceGrad(
    const TensorObject *dy, std::span<const int64_t> shape, int64_t axisArg, int64_t start,
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
    auto clampIndex      = [extent](int64_t v) {
        if (v < 0) {
            v += extent;
        }
        return std::clamp<int64_t>(v, 0, extent);
    };
    start                   = clampIndex(start);
    end                     = clampIndex(end);
    const int64_t count     = end > start ? (end - start + step - 1) / step : 0;
    const TensorObject *src = asFloat(dy, allocator);
    if (src->rank() != shape.size() || src->dim(static_cast<size_t>(axis)) != count) {
        throw ShapeError("slice gradient: dy shape does not match the slice");
    }
    TensorObject *out = TensorObject::create(TypeCode::Float32, shape, allocator, true);
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
    const TensorObject *lhs;
    const TensorObject *rhs;
    const TensorObject *dy;
};

MatmulView matrixView(
    const TensorObject *dy, const TensorObject *lhs, const TensorObject *rhs,
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

TensorObject *matmulGradLhs(
    const TensorObject *dy, const TensorObject *lhs, const TensorObject *rhs,
    mm::IAllocator &allocator) {
    const MatmulView v = matrixView(dy, lhs, rhs, allocator);
    TensorObject *g    = matmul(v.dy, transposeLast2(v.rhs, allocator), allocator);
    g                  = sumTo(g, v.lhs->shapeSpan(), allocator);
    return reshape(g, lhs->shapeSpan(), allocator);
}

TensorObject *matmulGradRhs(
    const TensorObject *dy, const TensorObject *lhs, const TensorObject *rhs,
    mm::IAllocator &allocator) {
    const MatmulView v = matrixView(dy, lhs, rhs, allocator);
    TensorObject *g    = matmul(transposeLast2(v.lhs, allocator), v.dy, allocator);
    g                  = sumTo(g, v.rhs->shapeSpan(), allocator);
    return reshape(g, rhs->shapeSpan(), allocator);
}

} // namespace camel::tensor::kernels
