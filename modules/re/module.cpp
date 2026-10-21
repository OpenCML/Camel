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
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The regular expression module. A string counts code points (design principles 11.1), so `.` and
 * a character class stand for one code point: text above ASCII is matched as wide characters.
 *
 * The signature is written once, in the declaration module `stdlib/re.cml` (design principles 6.4;
 * handover H19, step 10). What is left here is what only C++ knows: the kernel in operators.cpp,
 * the executor that runs it, and the operator trait declared at load.
 */

#include "module.h"
#include "camel/core/context/context.h"
#include "camel/core/operator_traits.h"
#include "executor.h"

using namespace camel::core::context;
using namespace camel::core::module;

ReModule::ReModule(context_ptr_t ctx) : BuiltinModule("re", ctx) {}

module_ptr_t ReModule::create(context_ptr_t ctx) { return std::make_shared<ReModule>(ctx); }

bool ReModule::load() {
    if (loaded_)
        return true;
    context_->registerExecutorFactory("re", [ctx = context_]() { return createReExecutor(ctx); });
    // A pattern is matched against its arguments, so the operator is pure (design principles 3.3).
    // The purity is a fact of the implementation, so it stays here; the signature is the
    // declaration module's.
    camel::core::OperatorTable::instance().declare(
        "re",
        {
            {"test", true},
        });
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new ReModule(ctx->shared_from_this()); }
}
