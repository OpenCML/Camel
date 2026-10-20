/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 19, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Copy payload bits rather than converting through floating-point arithmetic.
#include "binary.h"
#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>

namespace camel::tensor {
namespace {
void convertEndian(std::string &bytes, size_t width) {
    static_assert(
        std::endian::native == std::endian::little || std::endian::native == std::endian::big);
    if constexpr (std::endian::native == std::endian::big) {
        for (size_t offset = 0; offset < bytes.size(); offset += width) {
            std::reverse(bytes.begin() + offset, bytes.begin() + offset + width);
        }
    }
}

size_t checkedBytes(std::span<const int64_t> shape, size_t width) {
    if (shape.size() > std::numeric_limits<uint32_t>::max()) {
        throw std::invalid_argument("tensor rank exceeds the storage limit");
    }
    bool empty = false;
    for (const auto extent : shape) {
        if (extent < 0) {
            throw ShapeError("binary tensor shape cannot contain negative extents");
        }
        empty |= extent == 0;
    }
    if (empty) {
        return 0;
    }
    size_t size = width;
    for (const auto extent : shape) {
        if (static_cast<uint64_t>(extent) > std::numeric_limits<size_t>::max() / size) {
            throw ShapeError("binary tensor byte count overflows the address space");
        }
        size *= static_cast<size_t>(extent);
    }
    return size;
}
} // namespace

size_t tensorPayloadSize(type::TypeCode dtype, std::span<const int64_t> shape) {
    return checkedBytes(shape, elementSize(normalizeTensorDType(dtype)));
}

std::string tensorToLittleEndian(const Array *tensor) {
    std::string bytes;
    if (tensor->byteSize()) {
        bytes.assign(reinterpret_cast<const char *>(tensor->rawData()), tensor->byteSize());
    }
    convertEndian(bytes, elementSize(tensor->dtype()));
    return bytes;
}

Array *tensorFromLittleEndian(
    std::string_view input, type::TypeCode dtype, std::span<const int64_t> shape,
    mm::IAllocator &allocator) {
    dtype                 = normalizeTensorDType(dtype);
    const size_t width    = elementSize(dtype);
    const size_t expected = tensorPayloadSize(dtype, shape);
    if (input.size() != expected) {
        throw ShapeError(
            "binary tensor payload has " + std::to_string(input.size()) +
            " bytes; shape and dtype require " + std::to_string(expected));
    }
    if (dtype == type::TypeCode::Bool) {
        for (size_t i = 0; i < input.size(); ++i) {
            if (input[i] != 0 && input[i] != 1) {
                throw std::invalid_argument(
                    "binary bool payload must contain only 0 or 1 at byte " + std::to_string(i));
            }
        }
    }
    // Input may belong to the moving Camel heap: take ownership before allocation.
    std::string bytes(input);
    convertEndian(bytes, width);
    const Shape ownedShape(shape.begin(), shape.end());
    Array *result = createTensor(dtype, ownedShape, allocator);
    if (!bytes.empty()) {
        std::memcpy(result->rawData(), bytes.data(), bytes.size());
    }
    return result;
}
} // namespace camel::tensor
