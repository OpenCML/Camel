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
 * Created: Jul. 29, 2025
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <format>
#include <optional>

#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/collection.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/base.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "operators.h"
#include "value.h"

namespace mm = camel::core::mm;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::type;
using namespace camel::core::rtdata;
using camel::json::isJsonType;
using camel::json::JsonType;
using camel::json::JsonValue;

namespace {

using json = nlohmann::json;

// How deeply a JSON text may nest. nlohmann's parser, its serializer and this module's
// fromJson/toJson all walk a document with native recursion, so a text deeper than the native
// stack allows would crash the process instead of failing (docs/design-principles.cn.md 7.7:
// a limit that a program can reach must be diagnosed). The limit is checked on the text before
// parsing, so a document deeper than this is the error value JsonTooDeep, never a crash (design
// principles 11.2: a document that is too deep is distinguished from text that is not JSON).
// The default leaves room for the recursive walk on every scheduler's stack; a program that
// needs deeper documents raises it with CAMEL_JSON_MAX_DEPTH (whole levels, at least 1).
size_t jsonMaxDepth() {
    static const size_t limit = [] {
        const char *text = std::getenv("CAMEL_JSON_MAX_DEPTH");
        if (text == nullptr || *text == '\0') {
            return size_t{10000};
        }
        char *end             = nullptr;
        const unsigned long long value = std::strtoull(text, &end, 10);
        if (end == text || *end != '\0' || value == 0) {
            return size_t{10000};
        }
        return static_cast<size_t>(value);
    }();
    return limit;
}

// Whether `text` nests arrays/objects deeper than `limit`, ignoring brackets inside strings.
// When it does, `line` and `column` (1-based) are the position of the bracket that goes too
// deep: the diagnostic points at the text, as a parse error does.
bool jsonTooDeep(std::string_view text, size_t limit, size_t &line, size_t &column) {
    size_t depth         = 0;
    size_t currentLine   = 1;
    size_t currentColumn = 1;
    bool inString        = false;
    bool escaped         = false;
    for (const char c : text) {
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
        } else if (c == '"') {
            inString = true;
        } else if (c == '[' || c == '{') {
            if (++depth > limit) {
                line   = currentLine;
                column = currentColumn;
                return true;
            }
        } else if ((c == ']' || c == '}') && depth > 0) {
            --depth;
        }
        if (c == '\n') {
            ++currentLine;
            currentColumn = 1;
        } else if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
            // Columns count characters, not bytes (design principles 11.1: strings are UTF-8).
            ++currentColumn;
        }
    }
    return false;
}

std::string jsonTooDeepMessage(size_t limit, size_t line, size_t column) {
    return std::format(
        "the JSON document nests deeper than the limit of {} levels (at line {}, column {}); "
        "decoding a deeper document would exhaust the native stack: decode a shallower "
        "document or set CAMEL_JSON_MAX_DEPTH to a larger limit",
        limit,
        line,
        column);
}

// 递归：将 Camel 值转为 JSON。失败时返回 nullopt 并报告诊断。
static std::optional<json> camelToJson(Type *t, slot_t s, Context &ctx) {
    if (!t) {
        throwRuntimeFault(RuntimeDiag::JsonEncodeUnsupported, "<unknown>");
    }
    if (isJsonType(t)) {
        return fromSlot<JsonValue *>(s)->toJson();
    }
    switch (t->code()) {
    case TypeCode::Int32:
        return json(static_cast<int64_t>(fromSlot<Int32>(s)));
    case TypeCode::Int64:
        return json(fromSlot<Int64>(s));
    case TypeCode::Float32:
        return json(fromSlot<Float32>(s));
    case TypeCode::Float64:
        return json(fromSlot<Float64>(s));
    case TypeCode::Bool:
        return json(fromSlot<Bool>(s));
    case TypeCode::Byte:
        return json(static_cast<int>(static_cast<unsigned>(fromSlot<Byte>(s))));
    case TypeCode::Void:
        return json(nullptr);
    case TypeCode::String: {
        String *str = fromSlot<String *>(s);
        return json(str ? str->toString() : "");
    }
    case TypeCode::Array: {
        Array *arr = fromSlot<Array *>(s);
        if (!arr)
            return json::array();
        const ArrayType *arrayType = static_cast<const ArrayType *>(t);
        Type *elemType             = arrayType->elemType();
        json jarr = json::array();
        for (size_t i = 0; i < arr->size(); ++i) {
            auto elem = camelToJson(elemType, arr->data()[i], ctx);
            if (!elem)
                return std::nullopt;
            jarr.push_back(std::move(*elem));
        }
        return jarr;
    }
    case TypeCode::Tuple: {
        Tuple *tuple = fromSlot<Tuple *>(s);
        if (!tuple)
            return json::array();
        const TupleType *tupleType = static_cast<const TupleType *>(t);
        json jarr                  = json::array();
        for (size_t i = 0; i < tupleType->size(); ++i) {
            Type *elemType = tupleType->typeAt(i);
            auto elem = camelToJson(elemType, tuple->data()[i], ctx);
            if (!elem)
                return std::nullopt;
            jarr.push_back(std::move(*elem));
        }
        return jarr;
    }
    case TypeCode::Struct: {
        Struct *st = fromSlot<Struct *>(s);
        if (!st)
            return json::object();
        const StructType *structType = static_cast<const StructType *>(t);
        json jobj;
        for (size_t i = 0; i < structType->size(); ++i) {
            Type *fieldType = structType->typeAt(i);
            auto val = camelToJson(fieldType, st->data()[i], ctx);
            if (!val)
                return std::nullopt;
            jobj[std::string(structType->fieldName(i))] = std::move(*val);
        }
        return jobj;
    }
    default:
        break;
    }
    // A set is an array; a map with string keys is an object (other key types have no JSON
    // form). Both come out in key order.
    if (const CollectionType *coll = asCollectionType(t)) {
        const Table *table = fromSlot<Table *>(s);
        const bool object  = coll->isMap() && coll->keyType()->code() == TypeCode::String;
        if (!coll->isMap() || object) {
            json out    = object ? json::object() : json::array();
            bool failed = false;
            if (table != nullptr) {
                table::forEachEntry(table, coll, [&](slot_t key, slot_t value) {
                    if (failed) {
                        return;
                    }
                    auto encoded = object ? camelToJson(coll->valueType(), value, ctx)
                                          : camelToJson(coll->keyType(), key, ctx);
                    if (!encoded) {
                        failed = true;
                    } else if (object) {
                        out[fromSlot<String *>(key)->toString()] = std::move(*encoded);
                    } else {
                        out.push_back(std::move(*encoded));
                    }
                });
            }
            if (failed)
                return std::nullopt;
            return out;
        }
    }
    throwRuntimeFault(RuntimeDiag::JsonEncodeUnsupported, t->toString());
}

const char *jsonKindName(const json &j) {
    if (j.is_null())
        return "null";
    if (j.is_boolean())
        return "a boolean";
    if (j.is_number_integer())
        return "an integer";
    if (j.is_number())
        return "a number";
    if (j.is_string())
        return "a string";
    if (j.is_array())
        return "an array";
    return "an object";
}

/**
 * Decoding to a given type (DESIGN-QUESTION(json-typed-decode)). The JSON value `j`, found at
 * `path` of the document, as a value of `type`; false with `error` set when it does not have
 * the shape the type asks for. Nothing is converted: an integer is not a float's JSON form
 * unless it is written as a number without a fraction (JSON has one number type, so `1` is
 * accepted for a float), and a number is never a string.
 *
 *   bool, int, i32, float, string   the JSON scalar of that kind
 *   T[]                             an array of T
 *   Set<T>                          an array of T (duplicates are dropped)
 *   Map<string, T>                  an object whose values are T
 *   (A, B, ...)                     an array of exactly that length
 *   { f: A, g: B }                  an object with exactly those members
 *   T?                              null, or a T
 */
bool jsonToTyped(const json &j, Type *type, const std::string &path, slot_t &out, std::string &error) {
    const auto mismatch = [&](const std::string &wanted) {
        error = "at " + path + ": expected " + wanted + ", found " + jsonKindName(j);
        return false;
    };
    // A member typed Json takes whatever is there: the part of the shape left open.
    if (isJsonType(type)) {
        out = toSlot(JsonValue::fromJson(j));
        return true;
    }
    switch (type->code()) {
    case TypeCode::Bool:
        if (!j.is_boolean())
            return mismatch("a boolean");
        out = toSlot<Bool>(j.get<bool>());
        return true;
    case TypeCode::Int64:
        if (!j.is_number_integer() || (j.is_number_unsigned() && j.get<uint64_t>() > INT64_MAX))
            return mismatch("an integer");
        out = toSlot<Int64>(j.get<Int64>());
        return true;
    case TypeCode::Int32: {
        if (!j.is_number_integer() || j.is_number_unsigned() && j.get<uint64_t>() > INT32_MAX)
            return mismatch("a 32-bit integer");
        const Int64 value = j.get<Int64>();
        if (value < INT32_MIN || value > INT32_MAX)
            return mismatch("a 32-bit integer");
        out = toSlot<Int32>(static_cast<Int32>(value));
        return true;
    }
    case TypeCode::Float64:
        if (!j.is_number())
            return mismatch("a number");
        out = toSlot<Float64>(j.get<Float64>());
        return true;
    case TypeCode::Float32:
        if (!j.is_number())
            return mismatch("a number");
        out = toSlot<Float32>(j.get<Float32>());
        return true;
    case TypeCode::String:
        if (!j.is_string())
            return mismatch("a string");
        out = toSlot(String::from(j.get<std::string>(), mm::autoSpace()));
        return true;
    case TypeCode::Array: {
        if (!j.is_array())
            return mismatch("an array");
        auto *arrayType = static_cast<ArrayType *>(type);
        Array *arr      = Array::create(mm::autoSpace(), j.size());
        for (size_t i = 0; i < j.size(); ++i) {
            slot_t elem = NullSlot;
            if (!jsonToTyped(j[i], arrayType->elemType(), path + "[" + std::to_string(i) + "]", elem, error))
                return false;
            arr->set<slot_t>(i, elem, arrayType);
        }
        out = toSlot(arr);
        return true;
    }
    case TypeCode::Tuple: {
        auto *tupleType = static_cast<TupleType *>(type);
        if (tupleType->isNullable()) {
            if (j.is_null()) {
                out = NullSlot;
                return true;
            }
            slot_t payload = NullSlot;
            if (!jsonToTyped(j, tupleType->payloadType(), path, payload, error))
                return false;
            out = makeSome(payload, tupleType);
            return true;
        }
        if (tupleType->isTagged() || tupleType->isCell())
            break;
        if (!j.is_array() || j.size() != tupleType->size())
            return mismatch("an array of " + std::to_string(tupleType->size()) + " element(s)");
        Tuple *tuple = Tuple::create(tupleType->size(), mm::autoSpace());
        for (size_t i = 0; i < tupleType->size(); ++i) {
            slot_t elem = NullSlot;
            if (!jsonToTyped(j[i], tupleType->typeAt(i), path + "[" + std::to_string(i) + "]", elem, error))
                return false;
            tuple->set<slot_t>(i, elem, tupleType);
        }
        out = toSlot(tuple);
        return true;
    }
    case TypeCode::Struct: {
        auto *structType = static_cast<StructType *>(type);
        if (!j.is_object())
            return mismatch("an object");
        Struct *st = Struct::create(structType->size(), mm::autoSpace());
        for (size_t i = 0; i < structType->size(); ++i) {
            const std::string name(structType->fieldName(i));
            const auto member = j.find(name);
            if (member == j.end()) {
                error = "at " + path + ": the member '" + name + "' is missing";
                return false;
            }
            slot_t field = NullSlot;
            if (!jsonToTyped(*member, structType->typeAt(i), path + "." + name, field, error))
                return false;
            st->set<slot_t>(i, field, structType);
        }
        if (j.size() != structType->size()) {
            for (auto &[name, value] : j.items()) {
                if (!structType->findField(name).has_value()) {
                    error = "at " + path + ": the member '" + name + "' is not a field of " +
                            structType->toString();
                    return false;
                }
            }
        }
        out = toSlot(st);
        return true;
    }
    default:
        break;
    }
    if (const CollectionType *coll = asCollectionType(type)) {
        // Entries are gathered in an array of the type `as` takes, then converted the way
        // `as` converts: one sort, the later of two equal keys kept.
        ArrayType *sourceType = coll->sourceArrayType();
        if (!coll->isMap()) {
            if (!j.is_array())
                return mismatch("an array");
            slot_t elements = NullSlot;
            if (!jsonToTyped(j, sourceType, path, elements, error))
                return false;
            out = toSlot(table::fromArray(fromSlot<Array *>(elements), coll));
            return true;
        }
        if (coll->keyType()->code() == TypeCode::String) {
            if (!j.is_object())
                return mismatch("an object");
            const auto *mapType  = static_cast<const MapType *>(coll);
            TupleType *itemType  = mapType->itemType();
            Array *entries       = Array::create(mm::autoSpace(), j.size());
            size_t index         = 0;
            for (auto &[name, value] : j.items()) {
                slot_t decoded = NullSlot;
                if (!jsonToTyped(value, mapType->valueType(), path + "." + name, decoded, error))
                    return false;
                Tuple *entry = Tuple::create(2, mm::autoSpace());
                entry->set<Object *>(0, String::from(name, mm::autoSpace()), itemType);
                entry->set<slot_t>(1, decoded, itemType);
                entries->set<Object *>(index++, entry, sourceType);
            }
            out = toSlot(table::fromArray(entries, coll));
            return true;
        }
    }
    error = "at " + path + ": a value of type '" + type->toString() + "' has no JSON form";
    return false;
}

/// Whether jsonToTyped can produce a value of `type` at all, so that a call asking for a
/// type without a JSON form is rejected when it is compiled.
bool hasJsonForm(Type *type) {
    if (isJsonType(type)) {
        return true;
    }
    switch (type->code()) {
    case TypeCode::Bool:
    case TypeCode::Int64:
    case TypeCode::Int32:
    case TypeCode::Float64:
    case TypeCode::Float32:
    case TypeCode::String:
        return true;
    case TypeCode::Array:
        return hasJsonForm(static_cast<ArrayType *>(type)->elemType());
    case TypeCode::Tuple: {
        auto *tupleType = static_cast<TupleType *>(type);
        if (tupleType->isNullable())
            return hasJsonForm(tupleType->payloadType());
        if (tupleType->isTagged() || tupleType->isCell())
            return false;
        for (Type *member : tupleType->types()) {
            if (!hasJsonForm(member))
                return false;
        }
        return true;
    }
    case TypeCode::Struct:
        for (Type *member : static_cast<StructType *>(type)->types()) {
            if (!hasJsonForm(member))
                return false;
        }
        return true;
    default:
        break;
    }
    if (const CollectionType *coll = asCollectionType(type)) {
        if (!coll->isMap())
            return hasJsonForm(coll->keyType());
        return coll->keyType()->code() == TypeCode::String && hasJsonForm(coll->valueType());
    }
    return false;
}

} // namespace

slot_t __json_encode__(ArgsView &, ArgsView &norm, Context &ctx) {
    Type *t  = norm.type(0);
    slot_t s = norm.slot(0);
    auto opt = camelToJson(t, s, ctx);
    if (!opt)
        return NullSlot;
    int indent = norm.size() >= 2 ? static_cast<int>(fromSlot<Int64>(norm.slot(1))) : -1;
    if (indent < 0)
        indent = -1;
    std::string out = indent >= 0 ? opt->dump(indent, ' ') : opt->dump();
    return toSlot(String::from(out, mm::autoSpace()));
}

// decode(text): a `Json!`. Text that is not JSON is an error value `JsonParseError`, and a
// document nested deeper than the limit is `JsonTooDeep`, not a fault (design principles 11.2):
// decoding is an operation declared to fail, like parse_int.
slot_t __json_decode__(ArgsView &, ArgsView &norm, Context &) {
    static const TupleType *result = TupleType::fallible(JsonType::get());
    String *str                    = norm.get<String *>(0);
    const std::string_view text    = str ? str->view() : std::string_view();
    size_t line = 0, column = 0;
    if (const size_t limit = jsonMaxDepth(); jsonTooDeep(text, limit, line, column)) {
        return makeError("JsonTooDeep", jsonTooDeepMessage(limit, line, column), "<json::decode>");
    }
    try {
        return makeOk(toSlot(JsonValue::fromJson(json::parse(text.begin(), text.end()))), result);
    } catch (const json::exception &e) {
        // nlohmann's text is "[json.exception.parse_error.101] parse error at line 1, column
        // 2: ...": the bracketed id is the library's, the rest says where and what.
        std::string what = e.what();
        if (auto close = what.find("] "); what.starts_with("[") && close != std::string::npos) {
            what = what.substr(close + 2);
        }
        // The start of the text, on one line, so that the message shows what was decoded.
        std::string head(text);
        const bool cut = head.size() > 40;
        head           = head.substr(0, 40);
        for (char &c : head) {
            if (c == '\n' || c == '\r' || c == '\t') {
                c = ' ';
            }
        }
        // The line and column are positions in the decoded text, not in the program.
        return makeError(
            "JsonParseError",
            std::format(
                "the text is not valid JSON ({}); the text begins: '{}{}'",
                what,
                head,
                cut ? "..." : ""),
            "<json::decode>");
    }
}

bool jsonTypeHasForm(Type *type) { return type != nullptr && hasJsonForm(type); }

bool jsonTypeEncodable(Type *type) {
    if (type == nullptr) {
        return false;
    }
    if (isJsonType(type)) {
        return true;
    }
    // The cases of camelToJson, which faults on anything else.
    switch (type->code()) {
    case TypeCode::Int32:
    case TypeCode::Int64:
    case TypeCode::Float32:
    case TypeCode::Float64:
    case TypeCode::Bool:
    case TypeCode::Byte:
    case TypeCode::Void:
    case TypeCode::String:
        return true;
    case TypeCode::Array:
        return jsonTypeEncodable(static_cast<ArrayType *>(type)->elemType());
    case TypeCode::Tuple:
        for (Type *member : static_cast<TupleType *>(type)->types()) {
            if (!jsonTypeEncodable(member))
                return false;
        }
        return true;
    case TypeCode::Struct:
        for (Type *member : static_cast<StructType *>(type)->types()) {
            if (!jsonTypeEncodable(member))
                return false;
        }
        return true;
    default:
        break;
    }
    if (const CollectionType *coll = asCollectionType(type)) {
        if (!coll->isMap())
            return jsonTypeEncodable(coll->keyType());
        return coll->keyType()->code() == TypeCode::String && jsonTypeEncodable(coll->valueType());
    }
    return false;
}

// decode_as<shape>(text), decode_as(text, shape): the type of `shape` is the type decoded to;
// its value is not read (DESIGN-QUESTION(json-typed-decode)). The result is a `T!`: text that
// is not JSON, JSON that does not have the shape of `T`, and a document deeper than the limit
// (JsonTooDeep) are error values, not faults.
slot_t __json_decode_as__(ArgsView &with, ArgsView &norm, Context &ctx) {
    static const char *kWhere = "<json::decode_as>";
    Type *target              = with.size() > 0 ? with.type(0) : norm.type(1);
    const TupleType *result   = TupleType::fallible(target);
    json document;
    if (isJsonType(norm.type(0))) {
        // A Json decoded before: its document, read again with a type.
        document = fromSlot<JsonValue *>(norm.slot(0))->toJson();
    } else {
        String *str = norm.get<String *>(0);
        const std::string_view text = str ? str->view() : std::string_view();
        size_t line = 0, column = 0;
        if (const size_t limit = jsonMaxDepth(); jsonTooDeep(text, limit, line, column)) {
            return makeError("JsonTooDeep", jsonTooDeepMessage(limit, line, column), kWhere);
        }
        try {
            document = json::parse(text);
        } catch (const json::exception &e) {
            std::string what = e.what();
            if (auto close = what.find("] ");
                what.starts_with("[") && close != std::string::npos) {
                what = what.substr(close + 2);
            }
            return makeError("JsonParseError", what, kWhere);
        }
    }
    slot_t value = NullSlot;
    std::string error;
    if (!jsonToTyped(document, target, "$", value, error)) {
        return makeError("JsonTypeError", error, kWhere);
    }
    return makeOk(value, result);
}

// ---- looking into a Json (value.h) ----

namespace {

/// The argument of an accessor, with the layers its static type declares taken off. An
/// accessor takes a `Json`, a `Json?`, or a `Json!` (also the `Json!` inside a `Json?!`, so
/// that calls chain over a document decoded before); of a `Json!` it passes the error of the
/// document on unchanged (design principles 11.2).
struct JsonAccessorArg {
    /// The Json being looked into; nullptr when the argument is null or has failed.
    const JsonValue *value = nullptr;
    /// The failed `Json!` itself, passed on unchanged; NullSlot when the argument did not fail.
    slot_t failed = NullSlot;
    /// Whether the argument has the `Json!` layer: the result then carries the same one.
    bool fallible = false;
};

JsonAccessorArg jsonAccessorArg(ArgsView &norm, size_t i) {
    JsonAccessorArg arg;
    slot_t slot       = norm.slot(i);
    TupleType *tagged = asTagged(norm.type(i));
    if (tagged == nullptr) {
        arg.value = fromSlot<JsonValue *>(slot);
        return arg;
    }
    if (tagged->isNullable()) {
        if (isNullValue(slot)) {
            return arg; // a null `Json?`, or the null of a `Json?!`
        }
        slot = nullablePayload(slot);
        if (asFallible(tagged->payloadType()) == nullptr) {
            arg.value = fromSlot<JsonValue *>(slot);
            return arg;
        }
    }
    // A `Json!`, or the `Json!` of a `Json?!` whose null was handled above.
    arg.fallible = true;
    if (isErrorValue(slot)) {
        arg.failed = slot;
        return arg;
    }
    arg.value = fromSlot<JsonValue *>(falliblePayload(slot));
    return arg;
}

/// The result of an accessor: `value` as the value of the result type, null when there is
/// none, or the error of the document passed on unchanged. `result` is the type of the value
/// a success carries (`Json`, `int`, ...); the result itself is a `R?`, or a `R?!` when the
/// argument had the `Json!` layer.
slot_t jsonAccessorResult(const JsonAccessorArg &arg, Type *result, slot_t value) {
    if (arg.failed != NullSlot) {
        return makeSome(arg.failed, TupleType::nullable(TupleType::fallible(result)));
    }
    if (value == NullSlot) {
        return NullSlot;
    }
    if (arg.fallible) {
        return makeSome(
            makeOk(value, TupleType::fallible(result)),
            TupleType::nullable(TupleType::fallible(result)));
    }
    return makeSome(value, TupleType::nullable(result));
}

/// The payload of the Json argument when it is of `kind`, null otherwise.
slot_t payloadIf(ArgsView &norm, JsonValue::Kind kind, Type *payloadType) {
    const JsonAccessorArg arg = jsonAccessorArg(norm, 0);
    const bool present        = arg.value != nullptr && arg.value->kind() == kind;
    return jsonAccessorResult(arg, payloadType, present ? arg.value->payload() : NullSlot);
}

} // namespace

slot_t __json_kind__(ArgsView &, ArgsView &norm, Context &) {
    const JsonValue *value = fromSlot<JsonValue *>(norm.slot(0));
    return toSlot(String::from(JsonValue::kindName(value->kind()), mm::autoSpace()));
}

slot_t __json_is_null__(ArgsView &, ArgsView &norm, Context &) {
    return toSlot<Bool>(fromSlot<JsonValue *>(norm.slot(0))->kind() == JsonValue::Kind::Null);
}

slot_t __json_as_bool__(ArgsView &, ArgsView &norm, Context &) {
    return payloadIf(norm, JsonValue::Kind::Bool, Type::Bool());
}

slot_t __json_as_int__(ArgsView &, ArgsView &norm, Context &) {
    return payloadIf(norm, JsonValue::Kind::Int, Type::Int64());
}

// JSON has one number type: an integer is also a float (as decode_as reads it), a fraction is
// never an int.
slot_t __json_as_float__(ArgsView &, ArgsView &norm, Context &) {
    const JsonAccessorArg arg = jsonAccessorArg(norm, 0);
    if (arg.value == nullptr) {
        return jsonAccessorResult(arg, Type::Float64(), NullSlot);
    }
    if (arg.value->kind() == JsonValue::Kind::Int) {
        const auto number = static_cast<Float64>(fromSlot<Int64>(arg.value->payload()));
        return jsonAccessorResult(arg, Type::Float64(), toSlot<Float64>(number));
    }
    const bool present = arg.value->kind() == JsonValue::Kind::Float;
    return jsonAccessorResult(arg, Type::Float64(), present ? arg.value->payload() : NullSlot);
}

slot_t __json_as_string__(ArgsView &, ArgsView &norm, Context &) {
    return payloadIf(norm, JsonValue::Kind::String, Type::String());
}

slot_t __json_as_array__(ArgsView &, ArgsView &norm, Context &) {
    return payloadIf(norm, JsonValue::Kind::Array, camel::json::jsonArrayType());
}

slot_t __json_as_object__(ArgsView &, ArgsView &norm, Context &) {
    return payloadIf(norm, JsonValue::Kind::Object, camel::json::jsonObjectType());
}

// get<key>(j) and get<index>(j): the member or element, or null.
slot_t __json_get__(ArgsView &with, ArgsView &norm, Context &) {
    const JsonAccessorArg arg = jsonAccessorArg(norm, 0);
    Type *result              = camel::json::JsonType::get();
    if (arg.value == nullptr) {
        return jsonAccessorResult(arg, result, NullSlot);
    }
    if (with.type(0)->code() == TypeCode::Int64) {
        const Int64 index = fromSlot<Int64>(with.slot(0));
        if (arg.value->kind() != JsonValue::Kind::Array) {
            return jsonAccessorResult(arg, result, NullSlot);
        }
        const Array *elements = fromSlot<Array *>(arg.value->payload());
        if (index < 0 || static_cast<size_t>(index) >= elements->size()) {
            return jsonAccessorResult(arg, result, NullSlot);
        }
        return jsonAccessorResult(arg, result, elements->data()[index]);
    }
    if (arg.value->kind() != JsonValue::Kind::Object) {
        return jsonAccessorResult(arg, result, NullSlot);
    }
    slot_t member = NullSlot;
    if (!table::lookup(
            fromSlot<Table *>(arg.value->payload()),
            with.slot(0),
            Type::String(),
            member)) {
        return jsonAccessorResult(arg, result, NullSlot);
    }
    return jsonAccessorResult(arg, result, member);
}

std::unordered_map<std::string, operator_t> getJsonOpsMap() {
    return {
        {"encode", __json_encode__},
        {"decode", __json_decode__},
        {"decode_as", __json_decode_as__},
        {"kind", __json_kind__},
        {"is_null", __json_is_null__},
        {"as_bool", __json_as_bool__},
        {"as_int", __json_as_int__},
        {"as_float", __json_as_float__},
        {"as_string", __json_as_string__},
        {"as_array", __json_as_array__},
        {"as_object", __json_as_object__},
        {"get", __json_get__},
    };
}
