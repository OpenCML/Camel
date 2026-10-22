// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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

// One reduced element reads a strided value and adds it into an accumulator: about two
// elementwise elements (parallel.h, kNsPerFloat).
constexpr int64_t kLaneGrain = grainForWork(kNsPerFloat * 2.0);

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

/// exp in place by the exp operator's own loop: the f32 vector loop for f32, the definition in
/// double for f64.
template <typename T> void expInPlace(T *data, int64_t count) {
    if constexpr (std::is_same_v<T, float>) {
        unaryF32(UnaryOp::Exp, data, data, count);
    } else {
        parallelFor(count, grainForWork(kNsPerFloat * 8.0), [&](int64_t begin, int64_t end) {
            for (int64_t i = begin; i < end; ++i) {
                data[i] = std::exp(data[i]);
            }
        });
    }
}

/// log of one value by the log operator's own loop.
template <typename T> T logOf(T value) {
    if constexpr (std::is_same_v<T, float>) {
        float out = 0.0f;
        unaryF32(UnaryOp::Log, &value, &out, 1);
        return out;
    } else {
        return std::log(value);
    }
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

Array *reduceAxis(
    ReduceOp op, const Array *input, int64_t axisArg, bool keepDims,
    mm::IAllocator &allocator) {
    const size_t axis     = normalizeAxis(axisArg, input->rank());
    const AxisSplit split = splitAt(input->shapeSpan(), axis);
    const Shape outShape  = reducedShape(input->shapeSpan(), axis, keepDims);
    if ((op == ReduceOp::Max || op == ReduceOp::Min || op == ReduceOp::ArgMax ||
         op == ReduceOp::ArgMin) &&
        split.extent == 0) {
        throw std::invalid_argument("Cannot take max/min/argmax/argmin over an empty axis");
    }

    TypeCode outType = input->dtype();
    if (op == ReduceOp::Mean) {
        // A mean of floats keeps the element type (an f64 tensor averages in f64); a mean of
        // integers and bools has always been f32.
        outType = outType == TypeCode::Float64 ? TypeCode::Float64 : TypeCode::Float32;
    } else if (op == ReduceOp::ArgMax || op == ReduceOp::ArgMin) {
        outType = TypeCode::Int64;
    } else if (op == ReduceOp::Sum && outType == TypeCode::Bool) {
        outType = TypeCode::Int64;
    }
    Array *out = createTensor(outType, outShape, allocator);

    dispatchDType(input->dtype(), [&]<typename T>() {
        const T *src = input->dataAs<T>();
        forEachLane(split, [&](int64_t lane, int64_t base) {
            const int64_t stride = split.inner;
            switch (op) {
            case ReduceOp::Sum:
            case ReduceOp::Mean: {
                // f64 accumulation for floats; integers and bools exactly in int64, wrapping on
                // overflow as integer arithmetic does (design principles 5.1, 7.7): the
                // addition is done on uint64_t, where it is defined to wrap.
                using Acc = std::conditional_t<std::is_floating_point_v<T>, double, uint64_t>;
                Acc acc   = 0;
                for (int64_t k = 0; k < split.extent; ++k) {
                    if constexpr (std::is_floating_point_v<T>) {
                        acc += static_cast<double>(src[base + k * stride]);
                    } else {
                        acc += static_cast<uint64_t>(static_cast<int64_t>(src[base + k * stride]));
                    }
                }
                if (op == ReduceOp::Mean) {
                    double total = 0.0;
                    if constexpr (std::is_floating_point_v<T>) {
                        total = acc;
                    } else {
                        total = static_cast<double>(static_cast<int64_t>(acc));
                    }
                    const double mean = total / static_cast<double>(split.extent);
                    if (outType == TypeCode::Float64) {
                        out->dataAs<double>()[lane] = mean;
                    } else {
                        out->dataAs<float>()[lane] = static_cast<float>(mean);
                    }
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
            case ReduceOp::ArgMax:
            case ReduceOp::ArgMin: {
                // The position of the first maximum (minimum) by IEEE 754-2019 maximum
                // (minimum): the first NaN if there is one, +0 above -0, the first on a tie
                // (design principles 7.7; beyondExtreme in minmax.h).
                const bool max  = op == ReduceOp::ArgMax;
                T best          = src[base];
                int64_t bestIdx = 0;
                for (int64_t k = 1; k < split.extent; ++k) {
                    const T v = src[base + k * stride];
                    if (max ? beyondExtreme<true>(v, best) : beyondExtreme<false>(v, best)) {
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

double sumAll(const Array *input) {
    return dispatchDType(input->dtype(), [&]<typename T>() {
        const T *src = input->dataAs<T>();
        if constexpr (std::is_floating_point_v<T>) {
            double acc = 0.0;
            for (uint64_t i = 0; i < input->numel(); ++i) {
                acc += static_cast<double>(src[i]);
            }
            return acc;
        } else {
            // int64 and bool: exactly in int64, wrapping on overflow (design principles 5.1,
            // 7.7), then converted to the float result once.
            uint64_t acc = 0;
            for (uint64_t i = 0; i < input->numel(); ++i) {
                acc += static_cast<uint64_t>(static_cast<int64_t>(src[i]));
            }
            return static_cast<double>(static_cast<int64_t>(acc));
        }
    });
}

namespace {

enum class SoftmaxKind { Softmax, LogSoftmax };

/**
 * softmax and log_softmax along `axis`.
 *
 * softmax is computed exactly as its definition (design principles 7.1: the definition is
 * canonical, a fused kernel must agree with it bit for bit):
 *
 *     m = max(x, axis, keepdims)          IEEE maximum (minmax.h)
 *     e = exp(x - m)                      f32 subtraction, then the exp operator's own loop
 *     s = sum(e, axis, keepdims)          f64 accumulation from +0 in ascending order, one
 *                                         rounding to f32 at the end (sum_axis)
 *     softmax = e / s                     f32 division (not a multiplication by 1 / s)
 *
 * log_softmax shares the first three steps and returns (x - m) - log(s): s rounded to f32 as
 * above, its logarithm by the loop of the log operator, one f32 subtraction (its definition in
 * stdlib/tensor_definitions.cml, d - log(sum(exp(d), axis, keepdims)) with d = x - m).
 *
 * The work is three passes so the middle one can call the vectorized exp loop on the whole
 * tensor: per lane, the maximum and x - m; exp in place; per lane, the sum and the result.
 */
template <typename T>
Array *softmaxImpl(
    SoftmaxKind kind, const Array *input, int64_t axisArg, mm::IAllocator &allocator) {
    const size_t axis     = normalizeAxis(axisArg, input->rank());
    const AxisSplit split = splitAt(input->shapeSpan(), axis);
    Array *out            = createTensor(dtypeOf<T>(), input->shapeSpan(), allocator);
    const T *src          = input->dataAs<T>();
    T *dst                = out->dataAs<T>();
    const int64_t stride  = split.inner;

    // Pass 1: m per lane (kept for log_softmax) and dst = x - m.
    std::vector<T> laneMax(static_cast<size_t>(split.outer * split.inner));
    forEachLane(split, [&](int64_t lane, int64_t base) {
        T m = maximumIdentity<T>();
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
    expInPlace(dst, static_cast<int64_t>(out->numel()));

    // Pass 3: s and the result.
    forEachLane(split, [&](int64_t lane, int64_t base) {
        double sum = 0.0;
        for (int64_t k = 0; k < split.extent; ++k) {
            sum += static_cast<double>(dst[base + k * stride]);
        }
        if (kind == SoftmaxKind::Softmax) {
            const auto s = static_cast<T>(sum);
            for (int64_t k = 0; k < split.extent; ++k) {
                dst[base + k * stride] /= s;
            }
        } else {
            const T m      = laneMax[static_cast<size_t>(lane)];
            const T s      = static_cast<T>(sum);
            const T logSum = logOf(s);
            for (int64_t k = 0; k < split.extent; ++k) {
                const int64_t idx = base + k * stride;
                dst[idx]          = src[idx] - m - logSum;
            }
        }
    });
    return out;
}

} // namespace

Array *softmax(const Array *input, int64_t axis, mm::IAllocator &allocator) {
    if (input->dtype() == TypeCode::Float64) {
        return softmaxImpl<double>(SoftmaxKind::Softmax, input, axis, allocator);
    }
    if (input->dtype() == TypeCode::Float32) {
        return softmaxImpl<float>(SoftmaxKind::Softmax, input, axis, allocator);
    }
    refuseTensorElementType("softmax", input, {TypeCode::Float32, TypeCode::Float64});
}

Array *logSoftmax(const Array *input, int64_t axis, mm::IAllocator &allocator) {
    if (input->dtype() == TypeCode::Float64) {
        return softmaxImpl<double>(SoftmaxKind::LogSoftmax, input, axis, allocator);
    }
    if (input->dtype() == TypeCode::Float32) {
        return softmaxImpl<float>(SoftmaxKind::LogSoftmax, input, axis, allocator);
    }
    refuseTensorElementType("log_softmax", input, {TypeCode::Float32, TypeCode::Float64});
}

template <typename T>
Array *softmaxGradImpl(
    const Array *output, const Array *dy, const AxisSplit &split, mm::IAllocator &allocator) {
    Array *out        = createTensor(dtypeOf<T>(), output->shapeSpan(), allocator);
    const T *py       = output->dataAs<T>();
    const T *pg       = dy->dataAs<T>();
    T *dst            = out->dataAs<T>();
    forEachLane(split, [&](int64_t, int64_t base) {
        const int64_t stride = split.inner;
        double dot           = 0.0;
        for (int64_t k = 0; k < split.extent; ++k) {
            const int64_t idx = base + k * stride;
            dot += static_cast<double>(py[idx]) * static_cast<double>(pg[idx]);
        }
        for (int64_t k = 0; k < split.extent; ++k) {
            const int64_t idx = base + k * stride;
            dst[idx]          = static_cast<T>(py[idx] * (pg[idx] - dot));
        }
    });
    return out;
}

Array *softmaxGrad(
    const Array *output, const Array *dy, int64_t axisArg,
    mm::IAllocator &allocator) {
    if (!output->sameShape(dy)) {
        throw ShapeError("softmax_grad requires output and dy shapes to match");
    }
    if (output->dtype() != dy->dtype()) {
        throw std::invalid_argument(
            "softmax_grad requires both tensors to have the same element type");
    }
    const size_t axis     = normalizeAxis(axisArg, output->rank());
    const AxisSplit split = splitAt(output->shapeSpan(), axis);
    if (output->dtype() == TypeCode::Float64) {
        return softmaxGradImpl<double>(output, dy, split, allocator);
    }
    if (output->dtype() == TypeCode::Float32) {
        return softmaxGradImpl<float>(output, dy, split, allocator);
    }
    refuseTensorElementType("softmax_grad", output, {TypeCode::Float32, TypeCode::Float64});
}

template <typename T>
Array *layerNormImpl(
    const Array *input, const Array *gamma, const Array *beta, double eps,
    mm::IAllocator &allocator) {
    const auto width = static_cast<int64_t>(gamma->numel());
    const auto rows  = static_cast<int64_t>(input->numel()) / std::max<int64_t>(width, 1);
    Array *out       = createTensor(dtypeOf<T>(), input->shapeSpan(), allocator);
    const T *src     = input->dataAs<T>();
    const T *pw      = gamma->dataAs<T>();
    const T *pb      = beta->dataAs<T>();
    T *dst           = out->dataAs<T>();
    parallelFor(
        rows,
        std::max<int64_t>(1, kLaneGrain / std::max<int64_t>(width, 1)),
        [&](int64_t begin, int64_t end) {
            for (int64_t r = begin; r < end; ++r) {
                const T *row = src + r * width;
                double mean  = 0.0;
                for (int64_t k = 0; k < width; ++k) {
                    mean += static_cast<double>(row[k]);
                }
                mean /= static_cast<double>(width);
                double var = 0.0;
                for (int64_t k = 0; k < width; ++k) {
                    const double d = static_cast<double>(row[k]) - mean;
                    var += d * d;
                }
                var /= static_cast<double>(width);
                const auto inv = static_cast<T>(1.0 / std::sqrt(var + eps));
                const auto mu  = static_cast<T>(mean);
                T *outRow      = dst + r * width;
                for (int64_t k = 0; k < width; ++k) {
                    outRow[k] = static_cast<T>((static_cast<double>(row[k]) - mu) * inv *
                                               static_cast<double>(pw[k]) +
                                               static_cast<double>(pb[k]));
                }
            }
        });
    return out;
}

Array *layerNorm(
    const Array *input, const Array *gamma, const Array *beta, double eps,
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
    if (gamma->dtype() != input->dtype() || beta->dtype() != input->dtype()) {
        throw std::invalid_argument(
            "layer_norm requires the input, gamma and beta to have the same element type");
    }
    if (input->dtype() == TypeCode::Float64) {
        return layerNormImpl<double>(input, gamma, beta, eps, allocator);
    }
    if (input->dtype() == TypeCode::Float32) {
        return layerNormImpl<float>(input, gamma, beta, eps, allocator);
    }
    refuseTensorElementType("layer_norm", input, {TypeCode::Float32, TypeCode::Float64});
}

} // namespace camel::tensor::kernels
