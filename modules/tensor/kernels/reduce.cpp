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
#include "parallel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

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
            case ReduceOp::Min:
            case ReduceOp::ArgMax: {
                T best          = src[base];
                int64_t bestIdx = 0;
                for (int64_t k = 1; k < split.extent; ++k) {
                    const T v = src[base + k * stride];
                    if (op == ReduceOp::Min ? v < best : v > best) {
                        best    = v;
                        bestIdx = k;
                    }
                }
                if (op == ReduceOp::ArgMax) {
                    out->dataAs<int64_t>()[lane] = bestIdx;
                } else {
                    out->dataAs<T>()[lane] = best;
                }
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

TensorObject *softmaxImpl(
    SoftmaxKind kind, const TensorObject *input, int64_t axisArg, mm::IAllocator &allocator) {
    const size_t axis     = normalizeAxis(axisArg, input->rank());
    const AxisSplit split = splitAt(input->shapeSpan(), axis);
    const TensorObject *x = requireFloat(input, "softmax", allocator);
    TensorObject *out     = TensorObject::create(TypeCode::Float32, input->shapeSpan(), allocator);
    const float *src      = x->dataAs<float>();
    float *dst            = out->dataAs<float>();
    forEachLane(split, [&](int64_t, int64_t base) {
        const int64_t stride = split.inner;
        float maxValue       = -std::numeric_limits<float>::infinity();
        for (int64_t k = 0; k < split.extent; ++k) {
            maxValue = std::max(maxValue, src[base + k * stride]);
        }
        double sum = 0.0;
        for (int64_t k = 0; k < split.extent; ++k) {
            sum += std::exp(static_cast<double>(src[base + k * stride] - maxValue));
        }
        if (kind == SoftmaxKind::Softmax) {
            const double inv = 1.0 / sum;
            for (int64_t k = 0; k < split.extent; ++k) {
                const int64_t idx = base + k * stride;
                dst[idx] =
                    static_cast<float>(std::exp(static_cast<double>(src[idx] - maxValue)) * inv);
            }
        } else {
            const float logSum = static_cast<float>(std::log(sum));
            for (int64_t k = 0; k < split.extent; ++k) {
                const int64_t idx = base + k * stride;
                dst[idx]          = src[idx] - maxValue - logSum;
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
