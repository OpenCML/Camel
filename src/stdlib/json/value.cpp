// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The type `Json` and its object (see value.h for the layout and how it is traced).
 */

#include "value.h"

#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/collection.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/intern.h"
#include "camel/utils/assert.h"

#include <cstdint>
#include <ostream>

namespace camel::json {

using namespace camel::core::type;
using namespace camel::core::rtdata;

// ---- the type ----

JsonType::JsonType() : OtherType(typeCode()) {}

TypeCode JsonType::typeCode() {
    static TypeCode code = registerOtherType("Json", TypeFlag::GC_Traced);
    return code;
}

JsonType *JsonType::get() {
    // One object for the one `Json` code, from the type table (see intern.h).
    static JsonType *type = static_cast<JsonType *>(type::TypeTable::instance().internOther(
        typeCode(), std::span<const uintptr_t>(), [&]() -> type::Type * {
            void *mem = mm::permSpace().alloc(sizeof(JsonType), alignof(JsonType));
            ASSERT(mem != nullptr, "Failed to allocate JsonType from permSpace");
            return new (mem) JsonType();
        }));
    return type;
}

std::string JsonType::toString() const { return "Json"; }

std::string JsonType::mangle() const { return "Json"; }

Type *JsonType::clone(bool deep) const {
    (void)deep;
    return get();
}

bool JsonType::equals(Type *type) const { return isJsonType(type); }

CastSafety JsonType::castSafetyFrom(Type *sourceType) const {
    // Nothing converts into a Json: decode makes one from text.
    return isJsonType(sourceType) ? CastSafety::Safe : CastSafety::Forbidden;
}

bool JsonType::assignableFrom(Type *sourceType) const { return isJsonType(sourceType); }

OtherType *JsonType::cloneWithParams(std::span<Type *const> params) const {
    (void)params;
    return get();
}

bool isJsonType(const Type *type) { return type != nullptr && type->code() == JsonType::typeCode(); }

ArrayType *jsonArrayType() {
    static ArrayType *type = ArrayType::create(JsonType::get());
    return type;
}

MapType *jsonObjectType() {
    static MapType *type = MapType::get(Type::String(), JsonType::get());
    return type;
}

// ---- the object ----

JsonValue *JsonValue::create(Kind kind, slot_t payload, mm::IAllocator &allocator) {
    void *mem = allocator.alloc(sizeof(JsonValue), alignof(JsonValue));
    if (mem == nullptr) {
        throw std::bad_alloc();
    }
    auto *value = new (mem) JsonValue(kind, payload);
    if (Type *payloadType = JsonValue::payloadType(kind);
        payloadType != nullptr && payloadType->isGCTraced()) {
        mm::writeBarrier(value, nullptr, payload, payloadType);
    }
    return value;
}

JsonValue *JsonValue::fromJson(const nlohmann::json &document) {
    mm::IAllocator &space = mm::autoSpace();
    if (document.is_null()) {
        return create(Kind::Null, NullSlot, space);
    }
    if (document.is_boolean()) {
        return create(Kind::Bool, toSlot<Bool>(document.get<bool>()), space);
    }
    if (document.is_number_unsigned() && document.get<uint64_t>() > INT64_MAX) {
        return create(Kind::Float, toSlot<Float64>(document.get<Float64>()), space);
    }
    if (document.is_number_integer()) {
        return create(Kind::Int, toSlot<Int64>(document.get<Int64>()), space);
    }
    if (document.is_number()) {
        return create(Kind::Float, toSlot<Float64>(document.get<Float64>()), space);
    }
    if (document.is_string()) {
        return create(Kind::String, toSlot(String::from(document.get<std::string>(), space)), space);
    }
    if (document.is_array()) {
        // Every object here is created in this call: typed stores, no safepoint in between.
        Array *elements = Array::create(space, document.size());
        for (size_t i = 0; i < document.size(); ++i) {
            elements->set<Object *>(i, fromJson(document[i]), jsonArrayType());
        }
        return create(Kind::Array, toSlot(elements), space);
    }
    // An object: its members as the items of a Map<string, Json>, built the way `as` builds
    // a map (one sort; of two equal keys the later one is kept).
    MapType *objectType   = jsonObjectType();
    TupleType *itemType   = objectType->itemType();
    ArrayType *sourceType = objectType->sourceArrayType();
    Array *items          = Array::create(space, document.size());
    size_t index          = 0;
    for (const auto &[name, member] : document.items()) {
        ::Tuple *item = ::Tuple::create(2, space);
        item->set<Object *>(0, String::from(name, space), itemType);
        item->set<Object *>(1, fromJson(member), itemType);
        items->set<Object *>(index++, item, sourceType);
    }
    return create(Kind::Object, toSlot(table::fromArray(items, objectType)), space);
}

nlohmann::json JsonValue::toJson() const {
    switch (kind_) {
    case Kind::Null:
        return nullptr;
    case Kind::Bool:
        return fromSlot<Bool>(payload_);
    case Kind::Int:
        return fromSlot<Int64>(payload_);
    case Kind::Float:
        return fromSlot<Float64>(payload_);
    case Kind::String:
        return fromSlot<String *>(payload_)->toString();
    case Kind::Array: {
        const Array *elements = fromSlot<Array *>(payload_);
        nlohmann::json out    = nlohmann::json::array();
        for (size_t i = 0; i < elements->size(); ++i) {
            out.push_back(fromSlot<JsonValue *>(elements->getSlot(i))->toJson());
        }
        return out;
    }
    case Kind::Object: {
        nlohmann::json out = nlohmann::json::object();
        table::forEachEntry(fromSlot<Table *>(payload_), jsonObjectType(), [&](slot_t k, slot_t v) {
            out[fromSlot<String *>(k)->toString()] = fromSlot<JsonValue *>(v)->toJson();
        });
        return out;
    }
    }
    ASSERT(false, "Unknown JSON kind");
    return nullptr;
}

const char *JsonValue::kindName(Kind kind) {
    switch (kind) {
    case Kind::Null:
        return "null";
    case Kind::Bool:
        return "bool";
    case Kind::Int:
        return "int";
    case Kind::Float:
        return "float";
    case Kind::String:
        return "string";
    case Kind::Array:
        return "array";
    case Kind::Object:
        return "object";
    }
    return "null";
}

Type *JsonValue::payloadType(Kind kind) {
    switch (kind) {
    case Kind::Null:
        return nullptr;
    case Kind::Bool:
        return Type::Bool();
    case Kind::Int:
        return Type::Int64();
    case Kind::Float:
        return Type::Float64();
    case Kind::String:
        return Type::String();
    case Kind::Array:
        return jsonArrayType();
    case Kind::Object:
        return jsonObjectType();
    }
    return nullptr;
}

bool JsonValue::equals(const Object *other, const Type *type, bool deep) const {
    (void)type;
    (void)deep;
    const auto *rhs = dynamic_cast<const JsonValue *>(other);
    if (rhs == nullptr) {
        return false;
    }
    // By content, as JSON compares (1 and 1.0 are equal numbers).
    return this == rhs || toJson() == rhs->toJson();
}

Object *JsonValue::clone(mm::IAllocator &allocator, const Type *type, bool deep) const {
    (void)type;
    slot_t payload    = payload_;
    Type *payloadType = JsonValue::payloadType(kind_);
    // The payload is immutable and has no identity: a shallow copy shares it.
    if (deep && payloadType != nullptr && payloadType->isGCTraced()) {
        payload = toSlot(Object::clone(fromSlot<Object *>(payload_), allocator, payloadType, true));
    }
    return create(kind_, payload, allocator);
}

void JsonValue::print(std::ostream &os, const Type *type) const {
    (void)type;
    os << toJson().dump();
}

void JsonValue::updateRefs(const Object::RefRelocator &relocate, const Type *type) {
    Type *payloadType = JsonValue::payloadType(kind_);
    if (payloadType == nullptr || !payloadType->isGCTraced() || payload_ == NullSlot) {
        return;
    }
    // The payload type follows from the kind, so the reference is traced with a static type
    // like every other one (value.h).
    payload_ = toSlot(relocate(
        fromSlot<Object *>(payload_),
        payloadType,
        RefTraceInfo{
            .owner     = this,
            .ownerType = type,
            .slotType  = payloadType,
            .ownerKind = "JsonValue",
            .slotName  = "payload",
            .slotIndex = RefTraceInfo::npos,
        }));
}

} // namespace camel::json
