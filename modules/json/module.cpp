// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The signatures are written once, in the declaration module `stdlib/json.cml` (design
 * principles 6.4). What is left here is what only C++ knows: the kernels
 * in operators.cpp, the executor that runs them, the operator traits declared at load, and the
 * two type rules a declaration cannot express:
 *
 *   - `encode`: the value's type must have a JSON form, a condition on the argument type;
 *   - `decode_as`: the result type is the type the call names (`decode_as::<T>`) or the one
 *     its position determines.
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

namespace {

/// `decode_as::<T>(source)`: the type T is named at the call, or determined by the position the
/// call stands in (`let cfg: Config! = text->decode_as`); the result is a `T!`. A call that names
/// no type and stands where none is determined is refused, and so is a type with no JSON form.
class DecodeAsResolver final : public FuncTypeResolver {
  public:
    std::optional<FunctionType *>
    resolve(const type_vec_t &, const type_vec_t &, const ModifierSet &) const override {
        return std::nullopt;
    }

    std::optional<FunctionType *> resolveForCall(
        const type_vec_t &types, Type *expected, const type_vec_t &with, const type_vec_t &norm,
        static_args_t, const ModifierSet &) const override {
        if (!with.empty() || norm.size() != 1 ||
            (norm[0]->code() != TypeCode::String && !isJsonType(norm[0]))) {
            return std::nullopt;
        }
        Type *target = nullptr;
        if (types.size() == 1) {
            target = types[0];
        } else if (types.empty() && expected != nullptr) {
            target = determinedByPosition(expected);
        }
        if (target == nullptr || !jsonTypeHasForm(target)) {
            return std::nullopt;
        }
        // A type carries no modifiers (`sync`, `macro`): one identity per type, so the signature
        // built here has none, and the call's own node keeps what it was written with.
        return FunctionType::create(
            {},
            {{norm[0], false}},
            TupleType::fallible(target));
    }

    std::string signature() const override { return "::<T>(source: string | Json) => T!"; }

    bool parameterNames(std::vector<std::string> &with, std::vector<std::string> &norm)
        const override {
        with.clear();
        norm = {"source"};
        return true;
    }
};

} // namespace

JsonModule::JsonModule(context_ptr_t ctx) : BuiltinModule("json", ctx) {
    // `Json`: a JSON value whose shape is known only when the program runs (value.h). Its
    // static type is definite; a program looks into it through the operators below (design
    // principles 11.2: `decode` gives a self-describing `Json`, not an `any`). Text that is
    // not JSON gives an error value: `decode` returns a `Json!` (design principles 11.2).
    exportType(Reference("Json"), JsonType::get());

    // encode takes a value of every type that has a JSON form, a Json among them (a type
    // rule, not a parameter of type `any`), and an indent that may be absent or null. A function
    // or a tensor is refused when the call is compiled.
    const auto encodable = [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
        -> std::optional<Type *> {
        if (!jsonTypeEncodable(norm[0]) ||
            (with.size() == 1 &&
             !entersImplicitly(TupleType::nullable(Type::Int64()), with[0]))) {
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
                  {{1, {false}}, {1, {false}}},
                  "<indent: int?> (value: T) => string",
                  encodable)}}));
    exportEntity(
        "decode_as",
        OperatorGroup::create("decode_as", {{"json:decode_as", std::make_shared<DecodeAsResolver>()}}));
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
