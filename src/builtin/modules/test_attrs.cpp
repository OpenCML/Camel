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
 * Author: Camel Contributors
 * Created: Oct. 19, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "test_attrs.h"

#include "camel/core/context/context.h"
#include "camel/core/data/primary.h"
#include "camel/core/module/attr_decorator.h"
#include "camel/core/node_attrs.h"
#include "camel/core/operator_table.h"
#include "camel/execute/executor.h"

#include <cstdlib>

using namespace camel::core;
using namespace camel::core::module;

namespace {

bool enabled() { return std::getenv("CAMEL_ENABLE_TEST_PASSES") != nullptr; }

} // namespace

TestAttrsBuiltinModule::TestAttrsBuiltinModule(context::context_ptr_t ctx)
    : BuiltinModule("test_attrs", ctx) {
    if (!enabled()) {
        return;
    }
    exportEntity("mark", attrDecoratorGroup("mark", "test_attrs:mark", 0, "(fn: Function) => Function"));
    exportEntity(
        "flags",
        attrDecoratorGroup(
            "flags",
            "test_attrs:flags",
            1,
            "<n: int>(fn: Function) => Function",
            [](const type::type_vec_t &with) {
                return with[0]->code() == type::TypeCode::Int64;
            }));
    exportEntity(
        "level",
        attrDecoratorGroup(
            "level",
            "test_attrs:level",
            1,
            "<n: int>(fn: Function) => Function",
            [](const type::type_vec_t &with) {
                return with[0]->code() == type::TypeCode::Int64;
            }));
    exportEntity(
        "plain",
        attrDecoratorGroup("plain", "test_attrs:plain", 0, "(fn: Function) => Function"));
}

bool TestAttrsBuiltinModule::load() {
    if (loaded_) {
        return true;
    }
    loaded_ = true;
    if (!enabled()) {
        return true;
    }
    context_->registerExecutorFactory("test_attrs", [ctx = context_]() -> executor_ptr_t {
        return std::make_shared<Executor>(
            ctx,
            std::unordered_map<std::string, operator_t>{
                {"mark", &attrDecoratorIdentity},
                {"flags", &attrDecoratorIdentity},
                {"level", &attrDecoratorIdentity},
                {"plain", &attrDecoratorIdentity}});
    });
    auto &table = OperatorTable::instance();
    for (const char *uri :
         {"test_attrs:mark", "test_attrs:flags", "test_attrs:level", "test_attrs:plain"}) {
        table.set(uri, attrDecoratorTraits());
    }
    // The three kinds of key, each with its rules.
    attr::registerKey(
        "test.mark",
        {.kind = attr::ValueKind::Unit, .combine = attr::rules::inner(), .merge = attr::rules::equalOnly()});
    attr::registerKey(
        "test.flags",
        {.kind    = attr::ValueKind::Flags,
         .combine = attr::rules::unionFlags(),
         .merge   = attr::rules::intersectFlags()});
    attr::registerKey(
        "test.level",
        {.kind = attr::ValueKind::Int, .combine = attr::rules::inner(), .merge = attr::rules::equalOnly()});
    // The constant written, an int (a LongData).
    const auto intArg = [](std::span<const data::data_ptr_t> with) -> std::optional<int64_t> {
        const auto *n =
            with.size() == 1 ? dynamic_cast<const data::LongData *>(with[0].get()) : nullptr;
        return n ? std::optional<int64_t>(n->data()) : std::nullopt;
    };
    table.declareGrant("test_attrs:mark", {.key = "test.mark"});
    table.declareGrant(
        "test_attrs:flags",
        {.key   = "test.flags",
         .value = [intArg](std::span<const data::data_ptr_t> with)
             -> std::optional<attr::AttrValueId> {
             const auto n = intArg(with);
             if (!n || *n < 0 || *n > 1000) {
                 return std::nullopt;
             }
             const uint32_t bit = static_cast<uint32_t>(*n);
             return attr::flags(std::span<const uint32_t>(&bit, 1));
         }});
    table.declareGrant(
        "test_attrs:level",
        {.key   = "test.level",
         .value = [intArg](std::span<const data::data_ptr_t> with)
             -> std::optional<attr::AttrValueId> {
             const auto n = intArg(with);
             return n ? std::optional<attr::AttrValueId>(attr::integer(*n)) : std::nullopt;
         }});
    return true;
}

module_ptr_t TestAttrsBuiltinModule::create(context::context_ptr_t ctx) {
    return std::make_shared<TestAttrsBuiltinModule>(ctx);
}
