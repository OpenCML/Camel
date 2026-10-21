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
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/type/pure.h"

#include "camel/core/type/composite/struct.h"
#include "camel/utils/assert.h"
#include "camel/core/type/composite/tuple.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace camel::core::type {

namespace {

const DataLayout kInt32{4, 4, false, TypeCode::Int32, 0, nullptr, true, false};
const DataLayout kInt64{8, 8, false, TypeCode::Int64, 0, nullptr, true, false};
const DataLayout kFloat32{4, 4, false, TypeCode::Float32, 0, nullptr, true, false};
const DataLayout kFloat64{8, 8, false, TypeCode::Float64, 0, nullptr, true, false};
const DataLayout kBool{1, 1, false, TypeCode::Bool, 0, nullptr, true, false};
// A complex number is pure data, but its compact bytes are two floating-point numbers, not the
// bytes of a slot: it is not slot-resident. c64 is 8 bytes and would pass a size-only test;
// the type-level flag is what keeps `{x: c64}` from taking the bulk-copy path.
const DataLayout kComplex64{8, 4, false, TypeCode::Complex64, 0, nullptr, false, false};
const DataLayout kComplex128{16, 8, false, TypeCode::Complex128, 0, nullptr, false, false};

size_t alignUp(size_t value, size_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

// The answer is cached per type object. Types are interned and live for the process, so the
// pointer key stays valid and no entry is ever removed. The cache is a side table rather than
// fields on Type: a Type is allocated in the permanent space, and growing every type object
// would shift the point at which that space is exhausted (a diagnostic the tests pin down).
struct CacheEntry {
    const DataLayout *layout = nullptr;
    int8_t pure              = 0; // 0 unknown, 1 pure, -1 not pure
};

std::mutex &cacheMutex() {
    static auto *mutex = new std::mutex();
    return *mutex;
}

std::unordered_map<const Type *, CacheEntry> &cache() {
    static auto *map = new std::unordered_map<const Type *, CacheEntry>();
    return *map;
}

// Guards the structural walk against a cycle of pure types (a recursive struct whose fields
// are references is already rejected by the field codes; this is belt and braces).
thread_local std::vector<const Type *> visiting;

bool computePure(const Type *type) {
    if (std::find(visiting.begin(), visiting.end(), type) != visiting.end()) {
        return false;
    }
    switch (type->code()) {
    case TypeCode::Int32:
    case TypeCode::Int64:
    case TypeCode::Float32:
    case TypeCode::Float64:
    case TypeCode::Bool:
    case TypeCode::Complex64:
    case TypeCode::Complex128:
        return true;
    case TypeCode::Struct:
    case TypeCode::Tuple: {
        // A tagged tuple's null/failure distinction and a cell's identity live outside the
        // field list, so their bytes alone do not describe the value.
        if (type->code() == TypeCode::Tuple) {
            const auto *tuple = static_cast<const TupleType *>(type);
            if (tuple->isTagged() || tuple->isCell()) {
                return false;
            }
        }
        visiting.push_back(type);
        const size_t size = type->code() == TypeCode::Struct
                                ? static_cast<const StructType *>(type)->size()
                                : static_cast<const TupleType *>(type)->size();
        for (size_t i = 0; i < size; ++i) {
            const Type *field = type->code() == TypeCode::Struct
                                    ? static_cast<const StructType *>(type)->typeAt(i)
                                    : static_cast<const TupleType *>(type)->typeAt(i);
            if (!isPureDataType(field)) {
                visiting.pop_back();
                return false;
            }
        }
        visiting.pop_back();
        return true;
    }
    default:
        // Strings, arrays, functions, enums, references and tagged forms hold references.
        return false;
    }
}

const DataLayout *computeLayout(const Type *type) {
    if (const DataLayout *scalar = scalarDataLayout(type->code())) {
        return scalar;
    }
    if (type->code() != TypeCode::Struct && type->code() != TypeCode::Tuple) {
        return nullptr;
    }
    if (!isPureDataType(type)) {
        return nullptr;
    }
    const size_t fieldCount = type->code() == TypeCode::Struct
                                  ? static_cast<const StructType *>(type)->size()
                                  : static_cast<const TupleType *>(type)->size();
    size_t offset        = 0;
    size_t alignment     = 1;
    size_t *fieldOffsets = fieldCount > 0 ? new size_t[fieldCount] : nullptr;
    bool slotShaped      = fieldCount > 0;
    for (size_t i = 0; i < fieldCount; ++i) {
        const Type *field = type->code() == TypeCode::Struct
                                ? static_cast<const StructType *>(type)->typeAt(i)
                                : static_cast<const TupleType *>(type)->typeAt(i);
        const DataLayout *fieldLayout = pureDataLayout(field);
        ASSERT(fieldLayout != nullptr, "A pure-data field has a layout");
        // Type-level, not size-level: a field takes part in the slot-shaped bulk copy only when
        // its own bytes are a slot's bytes (slotResident) and it fills a slot exactly. A c64
        // field is 8 bytes but is not slot-resident, so `{x: c64}` is not slot-shaped.
        slotShaped      = slotShaped && fieldLayout->slotResident && !fieldLayout->aggregate &&
                     fieldLayout->size == sizeof(int64_t) &&
                     offset == i * sizeof(int64_t);
        offset          = alignUp(offset, fieldLayout->alignment);
        alignment       = std::max(alignment, fieldLayout->alignment);
        fieldOffsets[i] = offset;
        offset += fieldLayout->size;
    }
    auto *layout         = new DataLayout{};
    layout->size         = alignUp(offset, alignment);
    layout->alignment    = alignment;
    layout->aggregate    = true;
    layout->scalar       = TypeCode::Void;
    layout->fieldCount   = fieldCount;
    layout->fieldOffsets = fieldOffsets;
    layout->slotShaped   = slotShaped;
    return layout;
}

} // namespace

const DataLayout *scalarDataLayout(TypeCode code) {
    switch (code) {
    case TypeCode::Int32:
        return &kInt32;
    case TypeCode::Int64:
        return &kInt64;
    case TypeCode::Float32:
        return &kFloat32;
    case TypeCode::Float64:
        return &kFloat64;
    case TypeCode::Bool:
        return &kBool;
    case TypeCode::Complex64:
        return &kComplex64;
    case TypeCode::Complex128:
        return &kComplex128;
    default:
        return nullptr;
    }
}

bool isPureDataType(const Type *type) {
    if (type == nullptr) {
        return false;
    }
    // Numeric leaves are the common case and need no cache lookup or lock.
    if (scalarDataLayout(type->code()) != nullptr) {
        return true;
    }
    {
        std::lock_guard<std::mutex> lock(cacheMutex());
        const auto it = cache().find(type);
        if (it != cache().end() && it->second.pure != 0) {
            return it->second.pure == 1;
        }
    }
    const bool pure = computePure(type);
    {
        std::lock_guard<std::mutex> lock(cacheMutex());
        cache()[type].pure = pure ? 1 : -1;
    }
    return pure;
}

const DataLayout *pureDataLayout(const Type *type) {
    if (type == nullptr) {
        return nullptr;
    }
    if (const DataLayout *scalar = scalarDataLayout(type->code())) {
        return scalar;
    }
    {
        std::lock_guard<std::mutex> lock(cacheMutex());
        const auto it = cache().find(type);
        if (it != cache().end() && it->second.layout != nullptr) {
            return it->second.layout;
        }
    }
    if (!isPureDataType(type)) {
        return nullptr;
    }
    const DataLayout *layout = computeLayout(type);
    if (layout != nullptr) {
        std::lock_guard<std::mutex> lock(cacheMutex());
        CacheEntry &entry = cache()[type];
        if (entry.layout == nullptr) {
            entry.layout = layout;
        }
    }
    return layout;
}

} // namespace camel::core::type