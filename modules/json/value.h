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
 * Author: Zhenjie Wei
 * Created: Oct. 14, 2026
 * Updated: Oct. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The type `Json` of the json module and its run-time object (design principles 2.12;
 * docs/design/any-typing/README.cn.md, 4.4).
 *
 * `json.decode` gives a JSON value whose shape is known only when the program runs. Its static
 * type is `Json`, a definite type: what varies is inside the object, which describes itself.
 * A JsonValue is a kind and one slot of payload; the static type of the payload follows from
 * the kind and is fixed:
 *
 *   kind     payload                                   payload type
 *   Null     -                                         -
 *   Bool     the bool                                  bool
 *   Int      the integer                               int
 *   Float    the number                                float
 *   String   String *                                  string
 *   Array    Array * of JsonValue *                    Json[]
 *   Object   Table * from string to JsonValue *        Map<string, Json>
 *
 * So the collector reaches every reference below a Json with a static type: updateRefs
 * relocates the payload with its type, and from there arrays and maps trace their Json
 * elements, which trace themselves. Nothing about a Json is an untyped slot. A JsonValue is
 * built in one operator call and never changed afterwards (rule 6 of
 * docs/technical/36_gc_handle_rules.md holds trivially; create() still passes the payload
 * through the write barrier, as every module object does).
 *
 * This is not a general dynamic type: the language has no run-time type discrimination
 * (design principles 2.9). A program looks into a Json only through the operators of the
 * module (kind, get, as_int, ...), each of which gives a value of a definite type.
 */

#pragma once

#include "camel/core/mm.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/other.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <span>
#include <string>

namespace camel::json {

namespace mm     = camel::core::mm;
namespace rtdata = camel::core::rtdata;
namespace type   = camel::core::type;

class JsonType : public type::OtherType {
  public:
    static type::TypeCode typeCode();
    /// The one `Json` type object (in the permanent space).
    static JsonType *get();

    std::string toString() const override;
    std::string mangle() const override;
    type::Type *clone(bool deep = false) const override;
    bool equals(type::Type *type) const override;
    type::CastSafety castSafetyFrom(type::Type *sourceType) const override;
    bool assignableFrom(type::Type *sourceType) const override;
    type::OtherType *cloneWithParams(std::span<type::Type *const> params) const override;

  private:
    JsonType();
};

/// Whether `type` is `Json`.
bool isJsonType(const type::Type *type);
/// `Json[]`: the payload type of a JSON array and the result of `as_array`.
type::ArrayType *jsonArrayType();
/// `Map<string, Json>`: the payload type of a JSON object and the result of `as_object`.
type::MapType *jsonObjectType();

class JsonValue : public rtdata::Object {
  public:
    enum class Kind : uint8_t { Null, Bool, Int, Float, String, Array, Object };

    JsonValue(const JsonValue &)            = delete;
    JsonValue &operator=(const JsonValue &) = delete;

    /// A value of `kind` holding `payload` (of payloadType(kind)).
    static JsonValue *create(Kind kind, slot_t payload, mm::IAllocator &allocator);
    /// The value a parsed JSON document describes, built in the auto space. An integer that
    /// does not fit an `int` becomes a Float.
    static JsonValue *fromJson(const nlohmann::json &document);

    Kind kind() const { return kind_; }
    slot_t payload() const { return payload_; }
    /// The document this value describes (for encode, print and decode_as).
    nlohmann::json toJson() const;

    /// The name `kind(j)` gives: "null", "bool", "int", "float", "string", "array", "object".
    static const char *kindName(Kind kind);
    /// The static type of the payload of `kind`; nullptr for Null.
    static type::Type *payloadType(Kind kind);

    bool
    equals(const rtdata::Object *other, const type::Type *type, bool deep = false) const override;
    rtdata::Object *
    clone(mm::IAllocator &allocator, const type::Type *type, bool deep = false) const override;
    /// Prints the JSON text on one line, as `encode` writes it.
    void print(std::ostream &os, const type::Type *type) const override;
    void onMoved() override {}
    void updateRefs(const rtdata::Object::RefRelocator &relocate, const type::Type *type) override;

  private:
    JsonValue(Kind kind, slot_t payload) : kind_(kind), payload_(payload) {}

    Kind kind_;
    slot_t payload_;
};

} // namespace camel::json
