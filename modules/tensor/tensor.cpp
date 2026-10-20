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
 * Author: Zhenjie Wei
 * Created: Mar. 10, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Tensor construction: a dtype and a shape map to an Array of the matching numeric element
 * type. There is no separate tensor object any more; a view is an Array view of the source's
 * block.
 */

#include "tensor.h"

#include "camel/core/mm.h"
#include "camel/utils/assert.h"

#include <cstring>

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
    dtype                          = normalizeTensorDType(dtype);
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

} // namespace camel::tensor