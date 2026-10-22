// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Custom derivative rules (see rules.h).
 */

#include "rules.h"

#include "camel/core/mm.h"
#include "camel/core/type/composite/tuple.h"

namespace camel::autodiff {

bool hasRule(const camel::runtime::GCGraph *graph) {
    return graph != nullptr && !graph->ruleGraphs().empty();
}

::Function *ruleOf(const camel::runtime::Program *program, const camel::runtime::GCGraph *graph) {
    if (program == nullptr || graph == nullptr || graph->ruleGraphs().empty()) {
        return nullptr;
    }
    // The first graph the declaration names is the rule (`#vjp<rule>` has one parameter).
    camel::runtime::GCGraph *ruleGraph = program->graph(graph->ruleGraphs().front());
    if (ruleGraph == nullptr) {
        return nullptr;
    }
    // A rule is a function of the program, not a closure: an empty closure layout.
    auto *closure =
        camel::core::type::TupleType::create(std::vector<camel::core::type::Type *>{});
    return ::Function::create(
        program, ruleGraph->id(), closure, camel::core::mm::autoSpace());
}

::Function *ruleOf(const ::Function *function) {
    if (function == nullptr) {
        return nullptr;
    }
    return ruleOf(function->program(), function->graph());
}

} // namespace camel::autodiff
