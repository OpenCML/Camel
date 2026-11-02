// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/passes/opt/constant_bytes.h"

#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/base.h"

namespace camel::passes::opt {
namespace type = camel::core::type;
using ::Array;
using ::Struct;
using ::String;
using ::Tuple;

/// What a constant's own shape costs on top of the bytes of its elements: the object header and
/// the table of extents and strides an array carries, the header of a tuple, a struct or a string.
/// Counted so that a value made of many small pieces is not free of charge.
constexpr size_t kConstantShapeOverheadBytes = 4 * sizeof(slot_t);

/// How deep the walk below follows a value's members. A constant that shares a member with itself
/// (or a value whose contents this estimate does not know) ends here instead of walking forever.
constexpr size_t kConstantEstimateDepth = 8;

/**
 * The bytes a value takes once it is a constant of the graph: its elements (count x element size)
 * plus the shape that describes them (kConstantShapeOverheadBytes). Folding replaces a computation
 * by the value it produces, so this is the compile-time memory that buys the run time; a value past
 * the pass's budget is not worth it and stays the computation that builds it. The estimate stops
 * once it passes `stopAt` -- the caller only asks whether the value fits, never how big it is --
 * and so neither allocates nor walks a large value whole.
 *
 * It counts what the value occupies, not what it costs to build: an array of a hundred million
 * `int` is 800 MB here, as it would be in the graph.
 */
size_t constantBytesOf(
    slot_t value, const type::Type *type, size_t stopAt, size_t depth) {
    if (value == NullSlot || type == nullptr || !type::isGCTraced(type->code())) {
        return sizeof(slot_t);
    }
    const auto *object = camel::core::rtdata::fromSlot<const camel::core::rtdata::Object *>(value);
    if (object == nullptr || stopAt == 0 || depth >= kConstantEstimateDepth) {
        return 0;
    }
    const auto fits = [&](size_t taken) { return taken < stopAt ? stopAt - taken : 0; };
    size_t bytes    = kConstantShapeOverheadBytes;
    switch (type->code()) {
    case type::TypeCode::Array: {
        const auto *array = static_cast<const Array *>(object);
        // The elements, plus the extent and stride table the rank adds to the header.
        bytes += sizeof(Array) + array->rank() * 2 * sizeof(int64_t) +
                 array->byteSize();
        // An element that is a reference is one slot, which the element bytes above already
        // count; the objects the slots point at are counted here.
        if (array->isSlots() && array->elemType() != nullptr &&
            type::isGCTraced(array->elemType()->code())) {
            for (uint64_t i = 0; i < array->numel() && bytes < stopAt; ++i) {
                bytes += constantBytesOf(
                    array->getSlot(i), array->elemType(), fits(bytes), depth + 1);
            }
        }
        break;
    }
    case type::TypeCode::Tuple: {
        const auto *tuple     = static_cast<const Tuple *>(object);
        const auto *tupleType = static_cast<const type::TupleType *>(type);
        bytes += sizeof(Tuple) + tuple->size() * sizeof(slot_t);
        for (size_t i = 0; i < tuple->size() && i < tupleType->size() && bytes < stopAt; ++i) {
            const type::Type *field = tupleType->typeAt(i);
            if (field != nullptr && type::isGCTraced(field->code())) {
                bytes += constantBytesOf(tuple->slotAt(i), field, fits(bytes), depth + 1);
            }
        }
        break;
    }
    case type::TypeCode::Struct: {
        const auto *record     = static_cast<const Struct *>(object);
        const auto *structType = static_cast<const type::StructType *>(type);
        bytes += sizeof(Struct) + record->size() * sizeof(slot_t);
        for (size_t i = 0; i < record->size() && i < structType->size() && bytes < stopAt; ++i) {
            const type::Type *field = structType->typeAt(i);
            if (field != nullptr && type::isGCTraced(field->code())) {
                bytes += constantBytesOf(record->slotAt(i), field, fits(bytes), depth + 1);
            }
        }
        break;
    }
    case type::TypeCode::String: {
        const auto *text = static_cast<const String *>(object);
        bytes += sizeof(String) + text->size() + 1;
        break;
    }
    case type::TypeCode::Complex64:
    case type::TypeCode::Complex128: {
        // Two parts, the wider of the two types: a number is not a container.
        bytes += 2 * sizeof(double);
        break;
    }
    default:
        // A closure, a stream, an enumeration or a module type: this estimate does not know what
        // its contents are, so it counts the header and not the members.
        break;
    }
    return bytes;
}

} // namespace camel::passes::opt
