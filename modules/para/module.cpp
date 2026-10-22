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
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "module.h"
#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/operator.h"
#include "camel/core/operator_traits.h"
#include "camel/core/type.h"
#include "camel/core/type/resolver.h"
#include "camel/utils/type.h"
#include "executor.h"

#include <optional>

using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::type;

/*
 * Unordered iteration. Every signature is a declaration of `stdlib/para.cml`: the fixed one of
 * `rendezvous` and the two that take the element type of their array argument
 * (`unordered_foreach`, `unordered_reduce`), which a head declaration `::<T>` states exactly
 * (design principles 6.4; handover H19, step 10, H202). What is left here is what only C++ knows:
 * the kernels in operators.cpp, the executor that runs them, and the operator traits and
 * resources declared at load.
 */

ParaModule::ParaModule(context_ptr_t ctx) : BuiltinModule("para", ctx) {}

module_ptr_t ParaModule::create(context_ptr_t ctx) { return std::make_shared<ParaModule>(ctx); }

bool ParaModule::load() {
    if (loaded_)
        return true;
    context_ptr_t ctx = context_;
    context_->registerExecutorFactory("para", [ctx]() { return createParaExecutor(ctx); });
    // The purity of every operator (design principles 3.3).
    camel::core::OperatorTable::instance().declare(
        "para",
        {
            // Blocks until other calls arrive.
            {"rendezvous", false},
        });
    // The unordered counterparts of foreach and reduce are declared with the other higher-order
    // operators (the builtin traits and element calls): they call graphs like them.
    // The one meeting point of the process (docs/design/op-semantics 8).
    camel::core::OperatorTable::instance().declareResources(
        "para",
        {{"rendezvous",
          camel::core::sem::externalAmbient(
              "para:rendezvous",
              camel::core::sem::AccessMode::ReadModifyWrite)}});
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new ParaModule(ctx->shared_from_this()); }
}
