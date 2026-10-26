// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The signatures are written once, in the declaration module `stdlib/json.cml` (design
 * principles 6.4). What is left here is what only C++ knows: the kernels
 * in operators.cpp, the executor that runs them, the operator traits declared at load, and the
 * two type rules a declaration cannot express:
 *
 *   - `encode`: the value's type must have a JSON form, a condition on the argument type;
 *   - `decode_as`: the result type is the type of the `shape` argument.
 *
 * Their resolvers below are the one source of those two signatures; the rest come from the
 * declarations.
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

using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::type;
using camel::json::isJsonType;
using camel::json::JsonType;

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
    // DESIGN-QUESTION(json-typed-decode): decoding to a given type. A call cannot be handed a
    // type, so the type is that of a `shape` argument whose value is not read:
    // `decode_as<[] as Map<string, int>>(text)` or `decode_as(text, [] as Map<string, int>)`.
    // The result is a `T!`. What is decoded is a text, or a Json decoded before (a member of
    // it whose shape is known). The result type comes from the shape argument, which a
    // declaration cannot express, so the resolver here is the one source.
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
