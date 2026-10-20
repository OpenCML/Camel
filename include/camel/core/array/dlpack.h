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
 * Created: Oct. 20, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * DLPack, zero-copy (design: docs/design/arrays-memory/README.cn.md 3.3).
 *
 * A Camel array is compact numeric data behind a reference-counted Buffer, so it can be handed
 * to another DLPack consumer without a copy: the exported managed tensor holds a reference to
 * the buffer, and the buffer's data address does not move while the array is alive. Importing
 * goes the other way: the data is adopted as an external Buffer and released through the
 * producer's deleter when the last camel array that reads it goes away.
 *
 * The leaf rule is what makes this safe: a compact buffer holds numbers only, so another
 * framework cannot introduce a reference or a cycle into Camel's heap.
 */

#pragma once

#include "camel/core/mm/alloc/allocator.h"
#include "camel/core/rtdata/array.h"

#include <cstdint>

namespace camel::core::array {

enum DLDeviceType : int32_t {
    kDLCPU = 1,
};

struct DLDevice {
    int32_t device_type;
    int32_t device_id;
};

struct DLDataType {
    uint8_t code;
    uint8_t bits;
    uint16_t lanes;
};

struct DLTensor {
    void *data;
    DLDevice device;
    int32_t ndim;
    DLDataType dtype;
    int64_t *shape;
    int64_t *strides;
    uint64_t byte_offset;
};

struct DLManagedTensor {
    DLTensor dl_tensor;
    void *manager_ctx;
    void (*deleter)(DLManagedTensor *self);
};

/**
 * Exports a tensor as a DLPack managed tensor without copying. The argument must be a compact
 * array (a numeric tensor); an inline one is promoted first, which copies it once. The buffer
 * is retained; the caller (the consumer) owns the returned managed tensor and must call its
 * deleter exactly once.
 */
DLManagedTensor *toDLPack(Array *tensor);

/**
 * Imports a DLPack tensor without copying: the array reads the producer's memory through an
 * external buffer, and the producer's deleter runs when the last camel array that shares it is
 * gone. Only CPU, contiguous, 1-lane tensors are accepted; the data must be row-major.
 *
 * `allocator` is the heap the array object itself is allocated on. For a heap other than
 * mm::autoSpace (which cannot run the array's finalizer) the elements are copied instead,
 * matching the old behaviour of the tensor module's import.
 */
Array *fromDLPack(const DLManagedTensor *managed, camel::core::mm::IAllocator &allocator);

} // namespace camel::core::array