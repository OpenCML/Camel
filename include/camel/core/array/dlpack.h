// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * DLPack, zero-copy (design: docs/design/arrays-memory/README.cn.md 3.3).
 *
 * A Camel array is compact pure data behind a reference-counted data block, so a numeric array
 * can be handed to another DLPack consumer without a copy: the exported managed tensor holds a
 * reference to the block, and the block's data address does not move while the array is alive.
 * Importing goes the other way: the data is adopted as an external block and released through
 * the producer's deleter when the last camel array that reads it goes away.
 *
 * The leaf rule is what makes this safe: DLPack carries numeric dtypes only, so a compact
 * numeric block holds numbers and another framework cannot introduce a reference or a cycle
 * into Camel's heap. Arrays of pure-data structs are compact but are not exported through
 * DLPack (there is no dtype for a struct); they are left to the Arrow item.
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

/// DLPack 1.0: the version of the protocol the structure below belongs to.
struct DLPackVersion {
    uint32_t major;
    uint32_t minor;
};

/// DLPack 1.0 flag: the consumer must not write through the tensor.
inline constexpr uint64_t kDLPackFlagBitmaskReadOnly = 1ull << 0;

/// The versioned managed tensor (DLPack 1.0), which carries the flags the legacy one cannot.
struct DLManagedTensorVersioned {
    DLPackVersion version;
    void *manager_ctx;
    void (*deleter)(DLManagedTensorVersioned *self);
    uint64_t flags;
    DLTensor dl_tensor;
};

/**
 * Exports a tensor as a DLPack managed tensor without copying. The argument must be a compact
 * array (a numeric tensor); an inline one is promoted first, which copies it once. The Block
 * is retained; the caller (the consumer) owns the returned managed tensor and must call its
 * deleter exactly once.
 */
DLManagedTensor *toDLPack(Array *tensor);

/**
 * Exports a tensor as a versioned managed tensor, read-only: the array is a value, and other
 * Camel values may share its block, so a consumer must not write through it (design principles
 * 11.3). The block is retained as in `toDLPack`.
 */
DLManagedTensorVersioned *toDLPackVersioned(Array *tensor);

/**
 * A legacy managed tensor over the same memory as `versioned`, for a consumer that predates
 * DLPack 1.0. The legacy structure has no read-only flag, so the consumer is told by its
 * caller; the versioned tensor's deleter runs when the legacy one is released.
 */
DLManagedTensor *toLegacyDLPack(DLManagedTensorVersioned *versioned);

/**
 * Imports a DLPack tensor without copying: the array reads the producer's memory through an
 * external Block, and the producer's deleter runs when the last camel array that shares it is
 * gone. Only CPU, contiguous, 1-lane tensors are accepted; the data must be row-major.
 *
 * `allocator` is the heap the array object itself is allocated on. For a heap other than
 * mm::autoSpace (which cannot run the array's finalizer) the elements are copied instead,
 * matching the old behaviour of the tensor module's import.
 */
Array *fromDLPack(const DLManagedTensor *managed, camel::core::mm::IAllocator &allocator);

} // namespace camel::core::array