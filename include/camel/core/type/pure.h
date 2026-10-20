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

/*
 * Pure data: the type-system predicate behind the data block (design principles 10.5).
 *
 * A type is pure data when a value of it contains no reference to a Camel object: a number, a
 * boolean, a complex number, or a struct or tuple whose fields are all pure data. Such a value
 * can live in a data block, be handed to another runtime without a copy, and cannot make a
 * cycle; the collector does not scan it.
 *
 * The predicate is structural and cached per type in a side table keyed by the type object
 * (src/core/type/pure.cpp). Types are interned and live for the process, so a cached answer
 * (and the layout it carries) stays valid. The cache is deliberately not stored on `Type`: a
 * Type lives in the permanent space, and growing every type object would move the point at
 * which that space is exhausted. Recursive types are rejected by a recursion guard: a cycle of
 * pure types would have no finite layout.
 *
 * Tagged tuples (`T?`, `T!`) and cells are not pure data even when their fields are: their
 * representation carries the null/non-null or failure distinction (or a mutable cell identity)
 * outside the field list, so the bytes alone do not describe the value. A tagged form of a
 * struct additionally holds a reference. When a nullable numeric gets a flat representation,
 * this predicate can admit it; today it must not.
 *
 * The layout is the compact representation used by an array of the type: fields laid out one
 * after another with natural alignment, `size` rounded up to the alignment, and `fieldOffsets`
 * the byte offset of each field. Padding is part of the layout and is kept deterministic
 * (writers zero an element before filling it), so two equal values have equal bytes and
 * memcmp can compare compact elements.
 */

#pragma once

#include "camel/core/type/base.h"

#include <cstddef>

namespace camel::core::type {

/// The compact byte layout of a pure-data type.
struct DataLayout {
    /// Bytes per element, including tail padding (0 for an empty struct).
    size_t size = 0;
    /// Natural alignment of an element (1 when there is no field).
    size_t alignment = 1;
    /// True for a struct or tuple (the element is materialized to a Struct/Tuple on access).
    bool aggregate = false;
    /// The scalar code for a leaf type; Void for an aggregate.
    TypeCode scalar = TypeCode::Void;
    size_t fieldCount = 0;
    /// fieldCount byte offsets into an element; null for a leaf.
    const size_t *fieldOffsets = nullptr;
    /// True when every field is an 8-byte leaf with no padding, so the compact bytes equal the
    /// slot data of a real Struct/Tuple (a bulk copy is then possible).
    bool slotShaped = false;
};

/// The layout of a pure-data type, or null when the type is not pure data. The pointer is
/// owned by the type's cache and stays valid for the process.
const DataLayout *pureDataLayout(const Type *type);

/// Whether a value of `type` contains no reference to a Camel object. Cached per type.
bool isPureDataType(const Type *type);

/// The layout of a numeric scalar type (the leaf case), or null when `code` is not one.
/// Kept next to isPureDataType so the data block and DLPack agree on what "number" means.
const DataLayout *scalarDataLayout(TypeCode code);

} // namespace camel::core::type