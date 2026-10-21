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
 * Author: Camel Contributors
 * Created: Jul. 29, 2025
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The terminal module (H19 step 4: `io` is the terminal until `Stream` exists). Its operators are
 * the readers of stdin and the writers of the terminal's mode and buffer; the standard streams and
 * `print`/`println`/`input`/`eprintln` are the prelude's.
 *
 * This step keeps the signatures in the operator groups below; the next step moves them to the
 * declaration module `stdlib/io.cml` (H19 step 10) and leaves here what only C++ knows: the
 * kernels, the executor and the operator traits.
 */

#include "module.h"
#include "camel/core/context/context.h"
#include "camel/core/operator_traits.h"
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
            "set_terminal_raw_mode",
            {{"io:set_terminal_raw_mode",
              StaticFuncTypeResolver::create({}, {{Type::Bool(), false}}, Type::Void())}}),
        OperatorGroup::create(
            "has_input",
            {{"io:has_input", StaticFuncTypeResolver::create({}, {}, Type::Bool())}}),
        OperatorGroup::create(
            "get_char",
            {{"io:get_char", StaticFuncTypeResolver::create({}, {}, Type::String())}}),
        OperatorGroup::create(
            "get_chars",
            {{"io:get_chars",
              StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::String())}}),
        OperatorGroup::create(
            "clear_input_buffer",
            {{"io:clear_input_buffer", StaticFuncTypeResolver::create({}, {}, Type::Void())}}),
    };
    return groups;
}
} // namespace

IOModule::IOModule(context_ptr_t ctx) : BuiltinModule("io", ctx) {
    for (const auto &group : getOperatorGroups())
        exportEntity(group->name(), group);
}

module_ptr_t IOModule::create(context_ptr_t ctx) { return std::make_shared<IOModule>(ctx); }

bool IOModule::load() {
    if (loaded_)
        return true;
    context_ptr_t ctx = context_;
    context_->registerExecutorFactory("io", [ctx]() { return createIOExecutor(ctx); });
    // The terminal is not a function of its arguments: every operator is effectful (design
    // principles 3.3).
    camel::core::OperatorTable::instance().declare(
        "io",
        {
            {"set_terminal_raw_mode", false},
            {"has_input", false},
            {"get_char", false},
            {"get_chars", false},
            {"clear_input_buffer", false},
        });
    // What each touches (docs/design/op-semantics 8): raw mode writes the terminal, the readers
    // read or read-modify-write stdin. They keep the registered names until H19 step 9 renames
    // resources, so that they stay ordered against `:io/input`.
    {
        using camel::core::sem::AccessMode;
        using camel::core::sem::externalAmbient;
        camel::core::OperatorTable::instance().declareResources(
            "io",
            {
                {"set_terminal_raw_mode", externalAmbient("io:terminal", AccessMode::Write)},
                {"has_input", externalAmbient("io:stdin", AccessMode::Read)},
                {"get_char", externalAmbient("io:stdin", AccessMode::ReadModifyWrite)},
                {"get_chars", externalAmbient("io:stdin", AccessMode::ReadModifyWrite)},
                {"clear_input_buffer", externalAmbient("io:stdin", AccessMode::ReadModifyWrite)},
            });
    }
    loaded_ = true;
    return true;
}

extern "C" {

Module *camel_module_create(Context *ctx) { return new IOModule(ctx->shared_from_this()); }
}