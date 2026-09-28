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
 * Data-movement and creation kernels (see layout.h).
 *
 * Copies work on raw bytes of one element size, so they are dtype agnostic.
 * Permute walks the output in order and gathers from the input through
 * permuted strides; the innermost output dimension is a strided copy loop.
 */

#include "layout.h"

#include "parallel.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>

namespace camel::tensor::kernels {

using type::TypeCode;

namespace {

std::vector<int64_t> contiguousStrides(std::span<const int64_t> shape) {
    std::vector<int64_t> strides(shape.size(), 1);
    for (size_t d = shape.size(); d-- > 1;) {
        strides[d - 1] = strides[d] * shape[d];
    }
    return strides;
}

std::string shapeText(std::span<const int64_t> shape) {
    std::string text = "[";
    for (size_t i = 0; i < shape.size(); ++i) {
        text += (i ? ", " : "") + std::to_string(shape[i]);
    }
    return text + "]";
}

std::mt19937_64 &rng() {
    static std::mt19937_64 engine{std::random_device{}()};
    return engine;
}

std::mutex &rngMutex() {
    static std::mutex mutex;
    return mutex;
}

} // namespace

Shape resolveReshape(std::span<const int64_t> input, std::span<const int64_t> target) {
    Shape result(target.begin(), target.end());
    int64_t inferred = -1;
    uint64_t known   = 1;
    for (size_t i = 0; i < result.size(); ++i) {
        if (result[i] == -1) {
            if (inferred >= 0) {
                throw std::invalid_argument("reshape: at most one extent may be -1");
            }
            inferred = static_cast<int64_t>(i);
            continue;
        }
        if (result[i] < 0) {
            throw std::invalid_argument("reshape: extents must be non-negative");
        }
        known *= static_cast<uint64_t>(result[i]);
    }
    const uint64_t total = numelOf(input);
    if (inferred >= 0) {
        if (known == 0 || total % known != 0) {
            throw ShapeError(
                "reshape: cannot infer -1 for " + shapeText(input) + " -> " + shapeText(target));
        }
        result[static_cast<size_t>(inferred)] = static_cast<int64_t>(total / known);
    } else if (known != total) {
        throw ShapeError(
            "reshape changes tensor element count: " + shapeText(input) + " -> " +
            shapeText(target));
    }
    return result;
}

TensorObject *
reshape(const TensorObject *input, std::span<const int64_t> target, mm::IAllocator &allocator) {
    const Shape shape = resolveReshape(input->shapeSpan(), target);
    TensorObject *out = TensorObject::create(input->dtype(), shape, allocator);
    if (input->byteSize() > 0) {
        std::memcpy(out->rawData(), input->rawData(), input->byteSize());
    }
    return out;
}

Shape permuteShape(std::span<const int64_t> input, std::span<const int64_t> perm) {
    if (perm.size() != input.size()) {
        throw std::invalid_argument("permute: permutation length must equal the tensor rank");
    }
    std::vector<bool> seen(input.size(), false);
    Shape out(input.size());
    for (size_t i = 0; i < perm.size(); ++i) {
        const int64_t axis = perm[i] < 0 ? perm[i] + static_cast<int64_t>(input.size()) : perm[i];
        if (axis < 0 || axis >= static_cast<int64_t>(input.size()) ||
            seen[static_cast<size_t>(axis)]) {
            throw std::invalid_argument("permute: invalid permutation " + shapeText(perm));
        }
        seen[static_cast<size_t>(axis)] = true;
        out[i]                          = input[static_cast<size_t>(axis)];
    }
    return out;
}

TensorObject *
permute(const TensorObject *input, std::span<const int64_t> perm, mm::IAllocator &allocator) {
    const Shape outShape = permuteShape(input->shapeSpan(), perm);
    TensorObject *out    = TensorObject::create(input->dtype(), outShape, allocator);
    const size_t rank    = outShape.size();
    if (out->numel() == 0) {
        return out;
    }
    if (rank == 0) {
        std::memcpy(out->rawData(), input->rawData(), input->byteSize());
        return out;
    }
    const auto inStrides = contiguousStrides(input->shapeSpan());
    // gather stride of each output dimension in the input
    std::vector<int64_t> src(rank);
    for (size_t d = 0; d < rank; ++d) {
        const int64_t axis = perm[d] < 0 ? perm[d] + static_cast<int64_t>(rank) : perm[d];
        src[d]             = inStrides[static_cast<size_t>(axis)];
    }
    const int64_t inner = outShape.back();
    const int64_t rows  = static_cast<int64_t>(out->numel()) / std::max<int64_t>(inner, 1);
    dispatchDType(input->dtype(), [&]<typename T>() {
        const T *in = input->dataAs<T>();
        T *dst      = out->dataAs<T>();
        parallelFor(
            rows,
            std::max<int64_t>(1, 16384 / std::max<int64_t>(inner, 1)),
            [&](int64_t begin, int64_t end) {
                for (int64_t row = begin; row < end; ++row) {
                    // decode the outer index of this row
                    int64_t rem = row, base = 0;
                    for (size_t d = rank - 1; d-- > 0;) {
                        base += (rem % outShape[d]) * src[d];
                        rem /= outShape[d];
                    }
                    T *outRow            = dst + row * inner;
                    const int64_t stride = src[rank - 1];
                    for (int64_t i = 0; i < inner; ++i) {
                        outRow[i] = in[base + i * stride];
                    }
                }
            });
    });
    return out;
}

TensorObject *transposeLast2(const TensorObject *input, mm::IAllocator &allocator) {
    if (input->rank() < 2) {
        throw std::invalid_argument("transpose requires a tensor of rank >= 2");
    }
    std::vector<int64_t> perm(input->rank());
    for (size_t i = 0; i < perm.size(); ++i) {
        perm[i] = static_cast<int64_t>(i);
    }
    std::swap(perm[perm.size() - 1], perm[perm.size() - 2]);
    return permute(input, perm, allocator);
}

Shape concatShape(std::span<const std::span<const int64_t>> inputs, int64_t axisArg) {
    if (inputs.empty()) {
        throw std::invalid_argument("concat requires at least one tensor");
    }
    const size_t rank = inputs[0].size();
    int64_t axis      = axisArg < 0 ? axisArg + static_cast<int64_t>(rank) : axisArg;
    if (axis < 0 || axis >= static_cast<int64_t>(rank)) {
        throw std::invalid_argument("concat axis out of range");
    }
    Shape out(inputs[0].begin(), inputs[0].end());
    for (size_t t = 1; t < inputs.size(); ++t) {
        if (inputs[t].size() != rank) {
            throw ShapeError("concat requires tensors with the same rank");
        }
        for (size_t d = 0; d < rank; ++d) {
            if (static_cast<int64_t>(d) == axis) {
                out[d] += inputs[t][d];
            } else if (inputs[t][d] != out[d]) {
                throw ShapeError("concat requires equal non-axis dimensions");
            }
        }
    }
    return out;
}

TensorObject *
concat(std::span<const TensorObject *const> inputs, int64_t axisArg, mm::IAllocator &allocator) {
    std::vector<std::span<const int64_t>> shapes;
    for (const TensorObject *t : inputs) {
        if (t->dtype() != inputs[0]->dtype()) {
            throw std::invalid_argument("concat currently requires matching dtypes");
        }
        shapes.push_back(t->shapeSpan());
    }
    const Shape outShape = concatShape(shapes, axisArg);
    const size_t axis    = static_cast<size_t>(
        axisArg < 0 ? axisArg + static_cast<int64_t>(outShape.size()) : axisArg);
    TensorObject *out = TensorObject::create(inputs[0]->dtype(), outShape, allocator);
    uint64_t outer = 1, inner = 1;
    for (size_t d = 0; d < axis; ++d) {
        outer *= static_cast<uint64_t>(outShape[d]);
    }
    for (size_t d = axis + 1; d < outShape.size(); ++d) {
        inner *= static_cast<uint64_t>(outShape[d]);
    }
    const size_t itemSize = elementSize(out->dtype());
    std::byte *dst        = out->rawData();
    for (uint64_t o = 0; o < outer; ++o) {
        for (const TensorObject *t : inputs) {
            const uint64_t chunk = static_cast<uint64_t>(t->dim(axis)) * inner;
            std::memcpy(dst, t->rawData() + o * chunk * itemSize, chunk * itemSize);
            dst += chunk * itemSize;
        }
    }
    return out;
}

TensorObject *slice(
    const TensorObject *input, int64_t axisArg, int64_t start, int64_t end, int64_t step,
    mm::IAllocator &allocator) {
    if (step <= 0) {
        throw std::invalid_argument("slice step must be positive");
    }
    const int64_t rank = static_cast<int64_t>(input->rank());
    const int64_t axis = axisArg < 0 ? axisArg + rank : axisArg;
    if (axis < 0 || axis >= rank) {
        throw std::invalid_argument("slice axis out of range");
    }
    const int64_t extent = input->dim(static_cast<size_t>(axis));
    auto clampIndex      = [extent](int64_t v) {
        if (v < 0) {
            v += extent;
        }
        return std::clamp<int64_t>(v, 0, extent);
    };
    start                               = clampIndex(start);
    end                                 = clampIndex(end);
    const int64_t count                 = end > start ? (end - start + step - 1) / step : 0;
    Shape outShape                      = input->shapeVector();
    outShape[static_cast<size_t>(axis)] = count;
    TensorObject *out                   = TensorObject::create(input->dtype(), outShape, allocator);
    uint64_t outer = 1, inner = 1;
    for (int64_t d = 0; d < axis; ++d) {
        outer *= static_cast<uint64_t>(outShape[static_cast<size_t>(d)]);
    }
    for (int64_t d = axis + 1; d < rank; ++d) {
        inner *= static_cast<uint64_t>(outShape[static_cast<size_t>(d)]);
    }
    const size_t itemSize = elementSize(input->dtype());
    for (uint64_t o = 0; o < outer; ++o) {
        for (int64_t k = 0; k < count; ++k) {
            const uint64_t srcIndex =
                (o * static_cast<uint64_t>(extent) + static_cast<uint64_t>(start + k * step)) *
                inner;
            const uint64_t dstIndex =
                (o * static_cast<uint64_t>(count) + static_cast<uint64_t>(k)) * inner;
            std::memcpy(
                out->rawData() + dstIndex * itemSize,
                input->rawData() + srcIndex * itemSize,
                inner * itemSize);
        }
    }
    return out;
}

TensorObject *arange(int64_t start, int64_t stop, int64_t step, mm::IAllocator &allocator) {
    if (step == 0) {
        throw std::invalid_argument("range step cannot be zero");
    }
    int64_t count = 0;
    if (step > 0 && stop > start) {
        count = (stop - start + step - 1) / step;
    } else if (step < 0 && stop < start) {
        count = (start - stop - step - 1) / -step;
    }
    const int64_t shape[] = {count};
    TensorObject *out     = TensorObject::create(TypeCode::Int64, shape, allocator);
    auto *values          = out->dataAs<int64_t>();
    for (int64_t i = 0; i < count; ++i) {
        values[i] = start + i * step;
    }
    return out;
}

TensorObject *eye(int64_t size, mm::IAllocator &allocator) {
    if (size < 0) {
        throw std::invalid_argument("eye size cannot be negative");
    }
    const int64_t shape[] = {size, size};
    TensorObject *out     = TensorObject::create(TypeCode::Float32, shape, allocator, true);
    auto *values          = out->dataAs<float>();
    for (int64_t i = 0; i < size; ++i) {
        values[i * size + i] = 1.0f;
    }
    return out;
}

TensorObject *
randomUniform(std::span<const int64_t> shape, double low, double high, mm::IAllocator &allocator) {
    if (low > high) {
        std::swap(low, high);
    }
    TensorObject *out = TensorObject::create(TypeCode::Float32, shape, allocator);
    std::uniform_real_distribution<float> dist(static_cast<float>(low), static_cast<float>(high));
    std::lock_guard guard(rngMutex());
    float *values = out->dataAs<float>();
    for (uint64_t i = 0; i < out->numel(); ++i) {
        values[i] = dist(rng());
    }
    return out;
}

TensorObject *randomNormal(
    std::span<const int64_t> shape, double mean, double stddev, mm::IAllocator &allocator) {
    TensorObject *out = TensorObject::create(TypeCode::Float32, shape, allocator);
    std::normal_distribution<float> dist(static_cast<float>(mean), static_cast<float>(stddev));
    std::lock_guard guard(rngMutex());
    float *values = out->dataAs<float>();
    for (uint64_t i = 0; i < out->numel(); ++i) {
        values[i] = dist(rng());
    }
    return out;
}

void seedRandom(uint64_t seed) {
    std::lock_guard guard(rngMutex());
    rng().seed(seed);
}

} // namespace camel::tensor::kernels
