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
 * Author: Zhenjie Wei
 * Created: Jul. 29, 2025
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "module.h"
#include "camel/core/context/context.h"
#include "camel/core/operator.h"
#include "camel/core/operator_traits.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/resolver.h"
#include "executor.h"
#include "operators.h"
#include "value.h"

#include <optional>
#include <string>
#include <utility>

using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::type;
using camel::json::isJsonType;
using camel::json::JsonType;

namespace {

/// The result type of an accessor on an argument of type `type`: `R?` for a `Json` or a
/// `Json?`, and `R?!` for a `Json!` or a `Json?!`, so that calls chain over a failed document
/// and its error is passed on unchanged (design principles 11.2). nullptr when `type` is none
/// of those.
Type *accessorResultType(Type *type, Type *result) {
    TupleType *tagged = asTagged(type);
    if (tagged == nullptr) {
        return isJsonType(type) ? TupleType::nullable(result) : nullptr;
    }
    if (tagged->isNullable()) {
        Type *payload = tagged->payloadType();
        if (isJsonType(payload)) {
            return TupleType::nullable(result);
        }
        TupleType *inner = asTagged(payload);
        if (inner != nullptr && inner->isFallible() && isJsonType(inner->payloadType())) {
            return TupleType::nullable(TupleType::fallible(result));
        }
        return nullptr;
    }
    if (tagged->isFallible() && isJsonType(tagged->payloadType())) {
        return TupleType::nullable(TupleType::fallible(result));
    }
    return nullptr;
}

/// An accessor `(j: Json | Json? | Json!) => R?` (`R?!` for a `Json!`).
std::pair<std::string, resolver_ptr_t> accessor(const char *uri, const char *text, Type *result) {
    return {
        uri,
        DynamicFuncTypeResolver::create(
            {{0, {}}, {1, {false}}},
            text,
            [result](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                -> std::optional<Type *> { return accessorResultType(norm[0], result); }),
    };
}

/// The type rule of `get<key>` (keyCode String) and `get<index>` (keyCode Int64).
ResolverFunc memberRule(TypeCode keyCode) {
    return [keyCode](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
               -> std::optional<Type *> {
        if (with[0]->code() != keyCode) {
            return std::nullopt;
        }
        return accessorResultType(norm[0], JsonType::get());
    };
}

} // namespace

JsonModule::JsonModule(context_ptr_t ctx) : BuiltinModule("json", ctx) {
    // `Json`: a JSON value whose shape is known only when the program runs (value.h). Its
    // static type is definite; a program looks into it through the operators below (design
    // principles 11.2: `decode` gives a self-describing `Json`, not an `any`). Text that is
    // not JSON gives an error value: `decode` returns a `Json!` (design principles 11.2).
    exportType(Reference("Json"), JsonType::get());

    // encode takes a value of every type that has a JSON form, a Json among them (a type
    // rule, not a parameter of type `any`). A function or a tensor is refused when the call
    // is compiled.
    const auto encodable = [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
        -> std::optional<Type *> {
        if (!jsonTypeEncodable(norm[0]) ||
            (norm.size() == 2 && norm[1]->code() != TypeCode::Int64)) {
            return std::nullopt;
        }
        return Type::String();
    };
    exportEntity(
        "encode",
        OperatorGroup::create(
            "encode",
            {{"json:encode",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {1, {false}}}, "(value: T) => string", encodable)},
             {"json:encode",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {2, {false, false}}},
                  "(value: T, indent: int) => string",
                  encodable)}}));
    exportEntity(
        "decode",
        OperatorGroup::create(
            "decode",
            {{"json:decode",
              StaticFuncTypeResolver::create(
                  {},
                  {{Type::String(), false}},
                  TupleType::fallible(JsonType::get()))}}));
    // DESIGN-QUESTION(json-typed-decode): decoding to a given type. A call cannot be handed a
    // type, so the type is that of a `shape` argument whose value is not read:
    // `decode_as<[] as Map<string, int>>(text)` or `decode_as(text, [] as Map<string, int>)`.
    // The result is a `T!`. What is decoded is a text, or a Json decoded before (a member of
    // it whose shape is known).
    exportEntity(
        "decode_as",
        OperatorGroup::create(
            "decode_as",
            {{"json:decode_as",
              DynamicFuncTypeResolver::create(
                  {{-1, {}}, {-1, {}}},
                  "<shape: T> (text: string | Json) => T!",
                  [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      Type *shape = nullptr;
                      if (with.size() == 1 && norm.size() == 1) {
                          shape = with[0];
                      } else if (with.empty() && norm.size() == 2) {
                          shape = norm[1];
                      }
                      if (shape == nullptr ||
                          (norm[0]->code() != TypeCode::String && !isJsonType(norm[0])) ||
                          !jsonTypeHasForm(shape)) {
                          return std::nullopt;
                      }
                      return TupleType::fallible(shape);
                  })}}));

    // ---- looking into a Json ----
    // `kind` and `is_null` take a `Json` itself: they answer for the value, so a document that
    // failed or is null is not one of their arguments. The accessors below take the tagged
    // forms: `Json?` so that calls chain, and `Json!`/`Json?!` so that the error of a document
    // decoded before is passed on unchanged (design principles 11.2).
    exportEntity(
        "kind",
        OperatorGroup::create(
            "kind",
            {{"json:kind",
              StaticFuncTypeResolver::create({}, {{JsonType::get(), false}}, Type::String())}}));
    exportEntity(
        "is_null",
        OperatorGroup::create(
            "is_null",
            {{"json:is_null",
              StaticFuncTypeResolver::create({}, {{JsonType::get(), false}}, Type::Bool())}}));
    exportEntity(
        "as_bool",
        OperatorGroup::create(
            "as_bool",
            {accessor(
                "json:as_bool",
                "(j: Json | Json? | Json!) => bool? | bool?!",
                Type::Bool())}));
    exportEntity(
        "as_int",
        OperatorGroup::create(
            "as_int",
            {accessor("json:as_int", "(j: Json | Json? | Json!) => int? | int?!", Type::Int64())}));
    exportEntity(
        "as_float",
        OperatorGroup::create(
            "as_float",
            {accessor(
                "json:as_float",
                "(j: Json | Json? | Json!) => float? | float?!",
                Type::Float64())}));
    exportEntity(
        "as_string",
        OperatorGroup::create(
            "as_string",
            {accessor(
                "json:as_string",
                "(j: Json | Json? | Json!) => string? | string?!",
                Type::String())}));
    exportEntity(
        "as_array",
        OperatorGroup::create(
            "as_array",
            {accessor(
                "json:as_array",
                "(j: Json | Json? | Json!) => Json[]? | Json[]?!",
                camel::json::jsonArrayType())}));
    exportEntity(
        "as_object",
        OperatorGroup::create(
            "as_object",
            {accessor(
                "json:as_object",
                "(j: Json | Json? | Json!) => Map<string, Json>? | Map<string, Json>?!",
                camel::json::jsonObjectType())}));
    // get<key>: a member of an object; get<index>: an element of an array. Null when the
    // value is not of that kind or has no such member or element (a lookup that finds
    // nothing is empty, as for a map: design principles 11.3). A `Json!` argument gives a
    // `Json?!`, the error of the document passed on unchanged.
    exportEntity(
        "get",
        OperatorGroup::create(
            "get",
            {{"json:get",
              DynamicFuncTypeResolver::create(
                  {{1, {false}}, {1, {false}}},
                  "<key: string> (j: Json | Json? | Json!) => Json? | Json?!",
                  memberRule(TypeCode::String))},
             {"json:get",
              DynamicFuncTypeResolver::create(
                  {{1, {false}}, {1, {false}}},
                  "<index: int> (j: Json | Json? | Json!) => Json? | Json?!",
                  memberRule(TypeCode::Int64))}}));
}

module_ptr_t JsonModule::create(context_ptr_t ctx) { return std::make_shared<JsonModule>(ctx); }

bool JsonModule::load() {
    if (loaded_)
        return true;
    context_->registerExecutorFactory("json", [ctx = context_]() {
        return createJsonExecutor(ctx);
    });
    // The purity of every operator (design principles 3.3).
    camel::core::OperatorTable::instance().declare(
        "json",
        {
            // Functions of their argument: the same text or value gives the same result.
            {"encode", true},
            {"decode", true},
            {"decode_as", true},
            {"kind", true},
            {"is_null", true},
            {"as_bool", true},
            {"as_int", true},
            {"as_float", true},
            {"as_string", true},
            {"as_array", true},
            {"as_object", true},
            {"get", true},
        });
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new JsonModule(ctx->shared_from_this()); }
}
