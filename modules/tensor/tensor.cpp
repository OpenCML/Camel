// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Tensor construction: a dtype and a shape map to an Array of the matching numeric element
 * type. There is no separate tensor object any more; a view is an Array view of the source's
 * block.
 */

#include "tensor.h"

#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/utils/assert.h"

#include <cstring>
#include <string>

namespace camel::tensor {

using type::TypeCode;

uint64_t numelOf(std::span<const int64_t> shape) {
    uint64_t total = 1;
    for (int64_t extent : shape) {
        if (extent < 0) {
            throw std::invalid_argument("Tensor shape cannot contain negative extents");
        }
        total *= static_cast<uint64_t>(extent);
    }
    return total;
}

Array *
createTensor(TypeCode dtype, std::span<const int64_t> shape, mm::IAllocator &allocator,
             bool zeroInit) {
    if (!isSupportedTensorScalar(dtype)) {
        throw std::invalid_argument("Unsupported Tensor element type");
    }
    const type::ArrayType *arrType = type::ArrayType::create(type::Type::create(dtype));
    return Array::create(allocator, arrType, shape, zeroInit);
}

Array *viewTensor(
    const Array *source, std::span<const int64_t> shape, mm::IAllocator &allocator,
    uint64_t elementOffset) {
    ASSERT(source != nullptr, "A tensor view needs a source tensor");
    const uint64_t elements = numelOf(shape);
    if (elementOffset > source->numel() || elements > source->numel() - elementOffset) {
        throw std::invalid_argument("Tensor view must stay within its source");
    }
    if (&allocator != &mm::autoSpace()) {
        Array *copy = createTensor(source->dtype(), shape, allocator, false);
        const size_t bytes = copy->byteSize();
        if (bytes > 0) {
            std::memcpy(
                copy->rawData(),
                source->rawData() + elementOffset * source->elementSize(),
                bytes);
        }
        return copy;
    }
    Array *unwrapped = const_cast<Array *>(source);
    return Array::view(unwrapped, shape, elementOffset);
}

void refuseTensorElementType(
    std::string_view op, const Array *tensor, std::initializer_list<TypeCode> supported) {
    camel::core::error::throwRuntimeFault(
        camel::core::error::RuntimeDiag::OperandTypeUnsupported,
        std::string(op),
        elementTypeSpelling(tensor->dtype()) + "[..]",
        supportedElementTypes(supported) + " tensors");
}

} // namespace camel::tensor