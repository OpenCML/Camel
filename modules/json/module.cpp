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
 * Updated: Oct. 13, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "module.h"
#include "camel/core/context/context.h"
#include "camel/core/operator.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/resolver.h"
#include "camel/core/operator_traits.h"
#include "executor.h"
#include "operators.h"

#include <optional>

using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::type;

JsonModule::JsonModule(context_ptr_t ctx) : BuiltinModule("json", ctx) {
    // encode takes a value of every type that has a JSON form (a type rule, not a parameter
    // of type `any`: design principles 2.12). A function or a tensor is refused when the call
    // is compiled; it used to be the run-time fault JsonEncodeUnsupported.
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
              DynamicFuncTypeResolver::create({{0, {}}, {1, {false}}}, "(value: T) => string",
                                              encodable)},
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
              StaticFuncTypeResolver::create({}, {{Type::String(), false}}, Type::Any())}}));
    // DESIGN-QUESTION(json-typed-decode): decoding to a given type. A call cannot be handed a
    // type, so the type is that of a `shape` argument whose value is not read:
    // `decode_as<[] as Map<string, int>>(text)` or `decode_as(text, [] as Map<string, int>)`.
    // The result is a `T!`. `decode` above is unchanged.
    exportEntity(
        "decode_as",
        OperatorGroup::create(
            "decode_as",
            {{"json:decode_as",
              DynamicFuncTypeResolver::create(
                  {{-1, {}}, {-1, {}}},
                  "<shape: T> (text: string) => T!",
                  [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      Type *shape = nullptr;
                      if (with.size() == 1 && norm.size() == 1) {
                          shape = with[0];
                      } else if (with.empty() && norm.size() == 2) {
                          shape = norm[1];
                      }
                      if (shape == nullptr || norm[0]->code() != TypeCode::String ||
                          !jsonTypeHasForm(shape)) {
                          return std::nullopt;
                      }
                      return TupleType::fallible(shape);
                  })}}));
}

module_ptr_t JsonModule::create(context_ptr_t ctx) { return std::make_shared<JsonModule>(ctx); }

bool JsonModule::load() {
    if (loaded_)
        return true;
    context_->registerExecutorFactory("json", [ctx = context_]() {
        return createJsonExecutor(ctx);
    });
    // The purity of every operator (design principles 2.9).
    camel::core::OperatorTable::instance().declare(
        "json",
        {
            // Functions of their argument: the same text or value gives the same result.
            {"encode", true},
            {"decode", true},
            {"decode_as", true},
        });
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new JsonModule(ctx->shared_from_this()); }
}
