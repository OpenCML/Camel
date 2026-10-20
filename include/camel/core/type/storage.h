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
 * Created: Oct. 13, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Storage of type objects.
 *
 * Every type object of the core type system lives in the permanent space: it is allocated
 * once and never freed or moved, so a `Type *` is valid for the life of the process and may be
 * held by graphs, drafts, frames and the collector without bookkeeping.
 *
 * allocTypeStorage() is the one place those allocations are made. It also counts them by kind,
 * which is what `CAMEL_TYPE_STATS=1` prints at exit: the permanent space is otherwise a single
 * number, and telling which kind of type a program keeps allocating (per-graph frame layouts,
 * operator signatures, function types) is what the interning work of the graph data model
 * (docs/design/graph-model, section 3.8) is measured by.
 *
 * Thread-safety: allocTypeStorage() may be called from any thread (the permanent space
 * synchronizes itself; the counters are atomic).
 */

#pragma once

#include <cstddef>
#include <ostream>

namespace camel::core::type {

/// What a block of type storage holds. One counter each in the `CAMEL_TYPE_STATS` report.
enum class TypeStorageKind : unsigned {
    Primitive,  // Type objects of the built-in scalar and auxiliary types
    Array,      // ArrayType
    Tuple,      // TupleType, including variable cells, `T?` / `T!` and frame layouts
    Struct,     // StructType
    Function,   // FunctionType (parameter names inline)
    Collection, // MapType, SetType
    Enum,       // EnumType
    TypeVar,    // TypeVarType, TypeFunctionType (compile time only)
    Params,     // parameter arrays of module-defined (Other) types
    Count,
};

/// `size` bytes of permanent storage for a type object of the given kind. Never returns
/// nullptr: the permanent space reports exhaustion itself (MemoryExhausted).
void *allocTypeStorage(TypeStorageKind kind, size_t size, size_t align);

/// Storage for one `T`, or for a `T` followed by `size - sizeof(T)` bytes of trailing data.
template <typename T> void *allocTypeStorage(TypeStorageKind kind, size_t size = sizeof(T)) {
    return allocTypeStorage(kind, size, alignof(T));
}

/// One line (`[camel] types: ...`) with, per kind, the number of objects allocated so far and
/// their bytes, then the size of the type table and the constructions it answered without
/// allocating (`interned=`, `intern_hits=`). Printed at exit when CAMEL_TYPE_STATS is set.
void reportTypeStorage(std::ostream &os);

} // namespace camel::core::type
