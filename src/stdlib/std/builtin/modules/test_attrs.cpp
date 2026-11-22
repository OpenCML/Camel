// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "test_attrs.h"

#include "camel/core/context/context.h"
#include "camel/core/data/primary.h"
#include "camel/core/module/attr_decorator.h"
#include "camel/core/node_attrs.h"
#include "camel/core/operator_table.h"
#include "camel/core/tags.h"
#include "camel/execute/executor.h"

#include <cstdlib>

using namespace camel::core;
using namespace camel::core::module;

TestAttrsBuiltinModule::TestAttrsBuiltinModule(context::context_ptr_t ctx)
    : BuiltinModule("test_attrs", ctx) {
    exportEntity(
        "mark",
        attrDecoratorGroup("mark", "test_attrs:mark", 0, "(fn: Function) => Function"));
    exportEntity(
        "flags",
        attrDecoratorGroup(
            "flags",
            "test_attrs:flags",
            1,
            "<n: int>(fn: Function) => Function",
            [](const type::type_vec_t &with) { return with[0]->code() == type::TypeCode::Int64; }));
    exportEntity(
        "level",
        attrDecoratorGroup(
            "level",
            "test_attrs:level",
            1,
            "<n: int>(fn: Function) => Function",
            [](const type::type_vec_t &with) { return with[0]->code() == type::TypeCode::Int64; }));
    exportEntity(
        "plain",
        attrDecoratorGroup("plain", "test_attrs:plain", 0, "(fn: Function) => Function"));
}

bool TestAttrsBuiltinModule::load() {
    if (loaded_) {
        return true;
    }
    loaded_ = true;
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
    // Each attribute is a tag: one call declares the key, the decorator that grants it and the
    // tag itself (design principles 6.2, camel/core/tags.h). `#mark` and `@mark` are the same
    // fact from here on.
    {
        using namespace camel::core::tag;
        declareNodeTag(
            "test.mark",
            static_cast<Positions>(Func | Nodes),
            {.decorator = "test_attrs:mark",
             .kind      = attr::ValueKind::Unit,
             .combine   = attr::rules::inner(),
             .merge     = attr::rules::equalOnly()});
        // The constant written, an int (a LongData).
        const auto intArg = [](std::span<const data::data_ptr_t> with) -> std::optional<int64_t> {
            const auto *n =
                with.size() == 1 ? dynamic_cast<const data::LongData *>(with[0].get()) : nullptr;
            return n ? std::optional<int64_t>(n->data()) : std::nullopt;
        };
        declareNodeTag(
            "test.flags",
            static_cast<Positions>(Func | Nodes),
            {.decorator = "test_attrs:flags",
             .value =
                 [intArg](std::span<const data::data_ptr_t> with)
                 -> std::optional<attr::AttrValueId> {
                 const auto n = intArg(with);
                 if (!n || *n < 0 || *n > 1000) {
                     return std::nullopt;
                 }
                 const uint32_t bit = static_cast<uint32_t>(*n);
                 return attr::flags(std::span<const uint32_t>(&bit, 1));
             },
             .kind    = attr::ValueKind::Flags,
             .combine = attr::rules::unionFlags(),
             .merge   = attr::rules::intersectFlags()});
        declareNodeTag(
            "test.level",
            static_cast<Positions>(Func | Nodes),
            {.decorator = "test_attrs:level",
             .value =
                 [intArg](std::span<const data::data_ptr_t> with)
                 -> std::optional<attr::AttrValueId> {
                 const auto n = intArg(with);
                 return n ? std::optional<attr::AttrValueId>(attr::integer(*n)) : std::nullopt;
             },
             .kind    = attr::ValueKind::Int,
             .combine = attr::rules::inner(),
             .merge   = attr::rules::equalOnly()});
    }
    return true;
}

module_ptr_t TestAttrsBuiltinModule::create(context::context_ptr_t ctx) {
    return std::make_shared<TestAttrsBuiltinModule>(ctx);
}
