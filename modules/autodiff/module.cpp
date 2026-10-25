// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * autodiff module wiring.
 */

#include "module.h"

#include "operators.h"

#include "camel/core/context/context.h"
#include "camel/core/node_attrs.h"
#include "camel/core/tags.h"
#include "camel/execute/executor.h"

using namespace camel::core::context;
using namespace camel::core::module;

namespace {

/// The declaration tag of a custom derivative rule (design principles 6.2, 8):
/// `tag vjp<rule: func> on func`. Its fact is the graph reference of the rule function; the GIR
/// builder keeps that graph in the reference table of the function's graph
/// (kGraphRefRule), and this module reads it while it builds a gradient. No pipeline pass reads
/// it, so it names no reader: it is read by the operators of this module.
constexpr std::string_view kVjpTag = "autodiff::vjp";

void declareVjpTag() {
    camel::core::attr::registerKey(kVjpTag, {.kind = camel::core::attr::ValueKind::Graph});
    camel::core::tag::Decl decl;
    decl.module = "autodiff";
    decl.ident  = "vjp";
    decl.name   = std::string(kVjpTag);
    decl.on     = camel::core::tag::Func;
    decl.params.push_back({"rule", "func"});
    camel::core::tag::declare(std::move(decl));
}

class AutodiffExecutor : public Executor {
  public:
    explicit AutodiffExecutor(context_ptr_t ctx)
        : Executor(std::move(ctx), camel::autodiff::operatorKernels()) {}
};

} // namespace

AutodiffModule::AutodiffModule(context_ptr_t ctx) : BuiltinModule("autodiff", ctx) {
    camel::autodiff::registerOperatorMetadata();
    for (const auto &group : camel::autodiff::operatorGroups()) {
        exportEntity(group->name(), group);
    }
    declareVjpTag();
    exportTag(std::string(kVjpTag));
}

module_ptr_t AutodiffModule::create(context_ptr_t ctx) {
    return std::make_shared<AutodiffModule>(ctx);
}

bool AutodiffModule::load() {
    if (loaded_) {
        return true;
    }
    context_->registerExecutorFactory("autodiff", [ctx = context_]() -> executor_ptr_t {
        return std::make_shared<AutodiffExecutor>(ctx);
    });
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new AutodiffModule(ctx->shared_from_this()); }
}
