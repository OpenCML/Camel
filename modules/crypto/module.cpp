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
 */

#include "module.h"
#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/type.h"
#include "camel/core/type/resolver.h"
#include "executor.h"
#include "operators.h"

using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::type;

namespace {
const std::vector<oper_group_ptr_t> &getOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "sha256",
            {{"crypto:sha256",
              StaticFuncTypeResolver::create({}, {{Type::String(), false}}, Type::String())}}),
        OperatorGroup::create(
            "sha256_rounds",
            {{"crypto:sha256_rounds",
              StaticFuncTypeResolver::create(
                  {},
                  {{Type::String(), false}, {Type::Int64(), false}},
                  Type::String())}}),
    };
    return groups;
}
} // namespace

CryptoModule::CryptoModule(context_ptr_t ctx) : BuiltinModule("crypto", ctx) {
    for (const auto &group : getOperatorGroups())
        exportEntity(group->name(), group);
}

module_ptr_t CryptoModule::create(context_ptr_t ctx) { return std::make_shared<CryptoModule>(ctx); }

bool CryptoModule::load() {
    if (loaded_)
        return true;
    context_->registerExecutorFactory("crypto", [ctx = context_]() {
        return createCryptoExecutor(ctx);
    });
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new CryptoModule(ctx->shared_from_this()); }
}
