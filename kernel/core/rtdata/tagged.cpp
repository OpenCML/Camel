// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Layout of the tagged forms `T?` and `T!`; see camel/core/rtdata/tagged.h.
 */

#include "camel/core/rtdata/tagged.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata.h"
#include "camel/core/type/composite/struct.h"

namespace camel::core::rtdata {

namespace {

constexpr size_t kValueSlot = 0;
constexpr size_t kErrorSlot = 1;

} // namespace

slot_t nullablePayload(slot_t nullable) {
    if (nullable == NullSlot) {
        return NullSlot;
    }
    return fromSlot<::Tuple *>(nullable)->get<slot_t>(kValueSlot);
}

slot_t makeSome(slot_t value, const type::TupleType *nullableType) {
    GUARD(nullableType && nullableType->isNullable(), "makeSome needs a nullable type, and the type it was handed is not one: the value would be tagged as present on a type that has no absent form");
    ::Tuple *box = ::Tuple::create(1, mm::autoSpace());
    box->set<slot_t>(kValueSlot, value, nullableType);
    return toSlot(box);
}

bool isErrorValue(slot_t fallible) {
    GUARD(fallible != NullSlot, "A fallible value is never the null slot: the value being tagged as Ok or Error is the null slot itself");
    return fromSlot<::Tuple *>(fallible)->get<slot_t>(kErrorSlot) != NullSlot;
}

slot_t falliblePayload(slot_t fallible) {
    return fromSlot<::Tuple *>(fallible)->get<slot_t>(kValueSlot);
}

slot_t fallibleError(slot_t fallible) {
    return fromSlot<::Tuple *>(fallible)->get<slot_t>(kErrorSlot);
}

slot_t makeOk(slot_t value, const type::TupleType *fallibleType) {
    GUARD(fallibleType && fallibleType->isFallible(), "makeOk needs a fallible type, and the type it was handed is not one: an Ok would be tagged on a type with no Error arm");
    ::Tuple *box = ::Tuple::create(2, mm::autoSpace());
    box->set<slot_t>(kValueSlot, value, fallibleType);
    return toSlot(box);
}

slot_t makeFailed(slot_t error) {
    GUARD(error != NullSlot, "A failed value needs its Error structure, and the slot it was given is the null slot: a failure with no error is not a failure");
    // The payload slot stays the zero slot whatever its type; only the error slot is a
    // reference, and every fallible type declares it as one.
    static const type::TupleType *anyFallible = type::TupleType::fallible(type::Type::Void());
    ::Tuple *box                              = ::Tuple::create(2, mm::autoSpace());
    box->set<slot_t>(kErrorSlot, error, anyFallible);
    return toSlot(box);
}

slot_t
makeErrorInfo(std::string_view kind, std::string_view message, std::string_view location) {
    const type::StructType *errorType = type::errorType();
    ::Struct *info                    = ::Struct::create(errorType->size(), mm::autoSpace());
    const auto put                    = [&](std::string_view field, std::string_view text) {
        ::String *str = ::String::from(std::string(text), mm::autoSpace());
        info->set<::String *>(type::errorFieldIndex(field), str, errorType);
    };
    put("kind", kind);
    put("message", message);
    put("location", location);
    return toSlot(info);
}

slot_t makeError(std::string_view kind, std::string_view message, std::string_view location) {
    return makeFailed(makeErrorInfo(kind, message, location));
}

void printTaggedSlots(std::ostream &os, const slot_t *slots, const type::TupleType *type) {
    GUARD(type && type->isTagged(), "printTaggedSlots needs a tagged type, and the type it was handed has no tag: the slots would be printed as the wrong shape");
    if (type->isFallible() && slots[kErrorSlot] != NullSlot) {
        const auto *info = fromSlot<const ::Struct *>(slots[kErrorSlot]);
        const auto text  = [&](std::string_view field) {
            const auto *str = info->get<const ::String *>(type::errorFieldIndex(field));
            return str ? str->toString() : std::string();
        };
        os << "error " << text("kind") << ": " << text("message");
        return;
    }
    printSlot(os, slots[kValueSlot], type->payloadType());
}

} // namespace camel::core::rtdata
