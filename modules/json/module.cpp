// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The signatures are written once, in the declaration module `stdlib/json.cml` (design
 * principles 6.4). What is left here is what only C++ knows: the kernels in operators.cpp, the
 * executor that runs them, and the operator traits declared at load.
 */

#include "camel/core/type/native.h"
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
    camel::core::type::registerNativeType("json", "Json", JsonType::get());
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
