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
 * Created: Sep. 29, 2026
 * Updated: Sep. 29, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
