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
 * Reduction and normalization kernels (see reduce.h).
 *
 * All kernels share the [outer, extent, inner] decomposition. Work is split
 * across threads over the independent (outer, inner) lanes; each lane walks
 * the reduced dimension with stride `inner`.
 */

#include "reduce.h"

#include "elementwise.h"
#include "minmax.h"
#include "parallel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace camel::tensor::kernels {

using type::TypeCode;

namespace {

constexpr int64_t kLaneGrain = 1 << 12;

struct AxisSplit {
    int64_t outer  = 1;
    int64_t extent = 1;
    int64_t inner  = 1;
};

AxisSplit splitAt(std::span<const int64_t> shape, size_t axis) {
    AxisSplit split;
    for (size_t d = 0; d < shape.size(); ++d) {
        if (d < axis) {
            split.outer *= shape[d];
        } else if (d == axis) {
            split.extent = shape[d];
        } else {
            split.inner *= shape[d];
        }
    }
    return split;
}

/// Calls fn(lane, base) for every independent lane; base is the offset of element 0 of the lane.
template <typename Fn> void forEachLane(const AxisSplit &split, Fn &&fn) {
    const int64_t lanes = split.outer * split.inner;
    const int64_t grain = std::max<int64_t>(1, kLaneGrain / std::max<int64_t>(split.extent, 1));
    parallelFor(lanes, grain, [&](int64_t begin, int64_t end) {
        for (int64_t lane = begin; lane < end; ++lane) {
            const int64_t o = lane / split.inner;
            const int64_t i = lane % split.inner;
            fn(lane, o * split.extent * split.inner + i);
        }
    });
}

const TensorObject *
requireFloat(const TensorObject *input, const char *what, mm::IAllocator &allocator) {
    if (input->dtype() == TypeCode::Float32) {
        return input;
    }
    (void)what;
    return cast(input, TypeCode::Float32, allocator);
}

} // namespace

size_t normalizeAxis(int64_t axis, size_t rank) {
    const auto signedRank = static_cast<int64_t>(rank);
    if (axis < 0) {
        axis += signedRank;
    }
    if (axis < 0 || axis >= signedRank) {
        throw std::invalid_argument(
            "axis " + std::to_string(axis) + " is out of range for a rank-" + std::to_string(rank) +
            " tensor");
    }
    return static_cast<size_t>(axis);
}

Shape reducedShape(std::span<const int64_t> shape, size_t axis, bool keepDims) {
    Shape out;
    for (size_t d = 0; d < shape.size(); ++d) {
        if (d == axis) {
            if (keepDims) {
                out.push_back(1);
            }
            continue;
        }
        out.push_back(shape[d]);
    }
    return out;
}

TensorObject *reduceAxis(
    ReduceOp op, const TensorObject *input, int64_t axisArg, bool keepDims,
    mm::IAllocator &allocator) {
    const size_t axis     = normalizeAxis(axisArg, input->rank());
    const AxisSplit split = splitAt(input->shapeSpan(), axis);
    const Shape outShape  = reducedShape(input->shapeSpan(), axis, keepDims);
    if ((op == ReduceOp::Max || op == ReduceOp::Min || op == ReduceOp::ArgMax) &&
        split.extent == 0) {
        throw std::invalid_argument("Cannot take max/min/argmax over an empty axis");
    }

    TypeCode outType = input->dtype();
    if (op == ReduceOp::Mean) {
        outType = TypeCode::Float32;
    } else if (op == ReduceOp::ArgMax) {
        outType = TypeCode::Int64;
    } else if (op == ReduceOp::Sum && outType == TypeCode::Bool) {
        outType = TypeCode::Int64;
    }
    TensorObject *out = TensorObject::create(outType, outShape, allocator);

    dispatchDType(input->dtype(), [&]<typename T>() {
        const T *src = input->dataAs<T>();
        forEachLane(split, [&](int64_t lane, int64_t base) {
            const int64_t stride = split.inner;
            switch (op) {
            case ReduceOp::Sum:
            case ReduceOp::Mean: {
                // double accumulation for floats, exact int64 accumulation otherwise
                using Acc = std::conditional_t<std::is_same_v<T, float>, double, int64_t>;
                Acc acc   = 0;
                for (int64_t k = 0; k < split.extent; ++k) {
                    acc += static_cast<Acc>(src[base + k * stride]);
                }
                if (op == ReduceOp::Mean) {
                    out->dataAs<float>()[lane] = static_cast<float>(
                        static_cast<double>(acc) / static_cast<double>(split.extent));
                } else if (outType == TypeCode::Int64) {
                    out->dataAs<int64_t>()[lane] = static_cast<int64_t>(acc);
                } else {
                    out->dataAs<T>()[lane] = static_cast<T>(acc);
                }
                break;
            }
            case ReduceOp::Max:
            case ReduceOp::Min: {
                // IEEE 754-2019 maximum / minimum folded from the identity (minmax.h). The
                // operation is exactly associative and commutative, so this order is one of
                // many that give the same bits; a NaN anywhere gives the canonical NaN.
                T best = op == ReduceOp::Max ? maximumIdentity<T>() : minimumIdentity<T>();
                for (int64_t k = 0; k < split.extent; ++k) {
                    const T v = src[base + k * stride];
                    best      = op == ReduceOp::Max ? maximum(best, v) : minimum(best, v);
                }
                out->dataAs<T>()[lane] = best;
                break;
            }
            case ReduceOp::ArgMax: {
                // The index of the first element greater than every earlier one. Unchanged by
                // the maximum rule above: which index a NaN or a tie of -0 and +0 yields is
                // not settled (docs/design/op-semantics, phase 1 questions).
                T best          = src[base];
                int64_t bestIdx = 0;
                for (int64_t k = 1; k < split.extent; ++k) {
                    const T v = src[base + k * stride];
                    if (v > best) {
                        best    = v;
                        bestIdx = k;
                    }
                }
                out->dataAs<int64_t>()[lane] = bestIdx;
                break;
            }
            }
        });
    });
    return out;
}

double sumAll(const TensorObject *input) {
    return dispatchDType(input->dtype(), [&]<typename T>() {
        const T *src = input->dataAs<T>();
        double acc   = 0.0;
        for (uint64_t i = 0; i < input->numel(); ++i) {
            acc += static_cast<double>(src[i]);
        }
        return acc;
    });
}

namespace {

enum class SoftmaxKind { Softmax, LogSoftmax };

/**
 * softmax and log_softmax along `axis`.
 *
 * softmax is computed exactly as its definition (design principles 2.13: the definition is
 * canonical, a fused kernel must agree with it bit for bit):
 *
 *     m = max(x, axis, keepdims)          IEEE maximum (minmax.h)
 *     e = exp(x - m)                      f32 subtraction, then the exp operator's own loop
 *     s = sum(e, axis, keepdims)          f64 accumulation from +0 in ascending order, one
 *                                         rounding to f32 at the end (sum_axis)
 *     softmax = e / s                     f32 division (not a multiplication by 1 / s)
 *
 * log_softmax shares the first three steps and returns (x - m) - f32(log(f64 sum)).
 *
 * The work is three passes so the middle one can call the vectorized exp loop on the whole
 * tensor: per lane, the maximum and x - m; exp in place; per lane, the sum and the result.
 */
TensorObject *softmaxImpl(
    SoftmaxKind kind, const TensorObject *input, int64_t axisArg, mm::IAllocator &allocator) {
    const size_t axis     = normalizeAxis(axisArg, input->rank());
    const AxisSplit split = splitAt(input->shapeSpan(), axis);
    const TensorObject *x = requireFloat(input, "softmax", allocator);
    TensorObject *out     = TensorObject::create(TypeCode::Float32, input->shapeSpan(), allocator);
    const float *src      = x->dataAs<float>();
    float *dst            = out->dataAs<float>();
    const int64_t stride  = split.inner;

    // Pass 1: m per lane (kept for log_softmax) and dst = x - m.
    std::vector<float> laneMax(static_cast<size_t>(split.outer * split.inner));
    forEachLane(split, [&](int64_t lane, int64_t base) {
        float m = maximumIdentity<float>();
        for (int64_t k = 0; k < split.extent; ++k) {
            m = maximum(m, src[base + k * stride]);
        }
        laneMax[static_cast<size_t>(lane)] = m;
        for (int64_t k = 0; k < split.extent; ++k) {
            const int64_t idx = base + k * stride;
            dst[idx]          = src[idx] - m;
        }
    });

    // Pass 2: e = exp(x - m), in place, by the loop of the exp operator.
    unaryF32(UnaryOp::Exp, dst, dst, static_cast<int64_t>(out->numel()));

    // Pass 3: s and the result.
    forEachLane(split, [&](int64_t lane, int64_t base) {
        double sum = 0.0;
        for (int64_t k = 0; k < split.extent; ++k) {
            sum += static_cast<double>(dst[base + k * stride]);
        }
        if (kind == SoftmaxKind::Softmax) {
            const auto s = static_cast<float>(sum);
            for (int64_t k = 0; k < split.extent; ++k) {
                dst[base + k * stride] /= s;
            }
        } else {
            const float m      = laneMax[static_cast<size_t>(lane)];
            const float logSum = static_cast<float>(std::log(sum));
            for (int64_t k = 0; k < split.extent; ++k) {
                const int64_t idx = base + k * stride;
                dst[idx]          = src[idx] - m - logSum;
            }
        }
    });
    return out;
}

} // namespace

TensorObject *softmax(const TensorObject *input, int64_t axis, mm::IAllocator &allocator) {
    return softmaxImpl(SoftmaxKind::Softmax, input, axis, allocator);
}

TensorObject *logSoftmax(const TensorObject *input, int64_t axis, mm::IAllocator &allocator) {
    return softmaxImpl(SoftmaxKind::LogSoftmax, input, axis, allocator);
}

TensorObject *softmaxGrad(
    const TensorObject *output, const TensorObject *dy, int64_t axisArg,
    mm::IAllocator &allocator) {
    if (!output->sameShape(dy)) {
        throw ShapeError("softmax_grad requires output and dy shapes to match");
    }
    const size_t axis     = normalizeAxis(axisArg, output->rank());
    const AxisSplit split = splitAt(output->shapeSpan(), axis);
    const TensorObject *y = requireFloat(output, "softmax_grad", allocator);
    const TensorObject *g = requireFloat(dy, "softmax_grad", allocator);
    TensorObject *out     = TensorObject::create(TypeCode::Float32, output->shapeSpan(), allocator);
    const float *py       = y->dataAs<float>();
    const float *pg       = g->dataAs<float>();
    float *dst            = out->dataAs<float>();
    forEachLane(split, [&](int64_t, int64_t base) {
        const int64_t stride = split.inner;
        double dot           = 0.0;
        for (int64_t k = 0; k < split.extent; ++k) {
            const int64_t idx = base + k * stride;
            dot += static_cast<double>(py[idx]) * static_cast<double>(pg[idx]);
        }
        for (int64_t k = 0; k < split.extent; ++k) {
            const int64_t idx = base + k * stride;
            dst[idx]          = static_cast<float>(py[idx] * (pg[idx] - dot));
        }
    });
    return out;
}

TensorObject *layerNorm(
    const TensorObject *input, const TensorObject *gamma, const TensorObject *beta, double eps,
    mm::IAllocator &allocator) {
    const size_t normRank = gamma->rank();
    if (normRank == 0 || normRank > input->rank() || !gamma->sameShape(beta)) {
        throw ShapeError("layer_norm: gamma and beta must share the normalized trailing shape");
    }
    const size_t offset = input->rank() - normRank;
    for (size_t d = 0; d < normRank; ++d) {
        if (input->dim(offset + d) != gamma->dim(d)) {
            throw ShapeError(
                "layer_norm: gamma shape does not match the input's trailing dimensions");
        }
    }
    const auto width      = static_cast<int64_t>(gamma->numel());
    const auto rows       = static_cast<int64_t>(input->numel()) / std::max<int64_t>(width, 1);
    const TensorObject *x = requireFloat(input, "layer_norm", allocator);
    const TensorObject *w = requireFloat(gamma, "layer_norm", allocator);
    const TensorObject *b = requireFloat(beta, "layer_norm", allocator);
    TensorObject *out     = TensorObject::create(TypeCode::Float32, input->shapeSpan(), allocator);
    const float *src      = x->dataAs<float>();
    const float *pw       = w->dataAs<float>();
    const float *pb       = b->dataAs<float>();
    float *dst            = out->dataAs<float>();
    parallelFor(
        rows,
        std::max<int64_t>(1, kLaneGrain / std::max<int64_t>(width, 1)),
        [&](int64_t begin, int64_t end) {
            for (int64_t r = begin; r < end; ++r) {
                const float *row = src + r * width;
                double mean      = 0.0;
                for (int64_t k = 0; k < width; ++k) {
                    mean += row[k];
                }
                mean /= static_cast<double>(width);
                double var = 0.0;
                for (int64_t k = 0; k < width; ++k) {
                    const double d = row[k] - mean;
                    var += d * d;
                }
                var /= static_cast<double>(width);
                const auto inv = static_cast<float>(1.0 / std::sqrt(var + eps));
                const auto mu  = static_cast<float>(mean);
                float *outRow  = dst + r * width;
                for (int64_t k = 0; k < width; ++k) {
                    outRow[k] = (row[k] - mu) * inv * pw[k] + pb[k];
                }
            }
        });
    return out;
}

} // namespace camel::tensor::kernels
