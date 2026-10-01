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
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/draft.h"

#include <string_view>
#include <vector>

namespace camel::autodiff {

namespace {

constexpr std::string_view kRulePrefix = "autodiff.vjp/";

} // namespace

::Function *attachRule(
    const camel::core::context::context_ptr_t &context, ::Function *function, ::Function *rule) {
    (void)context;
    namespace rt = camel::runtime;
    using camel::core::type::TupleType;
    using camel::core::type::Type;

    rt::GCGraph *source = function->graph();
    auto draft          = rt::GraphDraft::decode(source);
    Type *ruleType      = rule->graph()->funcType();
    const auto slot     = draft->allocateRuntimeSlot(ruleType);
    const auto capture  = draft->addPortNode(ruleType, slot);
    draft->appendClosureNode(capture);

    const TupleType *closureType = function->tupleType();
    std::vector<Type *> captured(closureType->types().begin(), closureType->types().end());
    captured.push_back(ruleType);
    auto *withRule = TupleType::create(std::move(captured));
    draft->setClosureType(withRule);

    rt::GCGraph *graph = draft->encode(
        std::string(kRulePrefix) + source->stableId(),
        std::string(kRulePrefix) + source->mangledName(),
        source->name());
    auto *result = ::Function::create(graph, withRule, camel::core::mm::autoSpace());
    for (size_t i = 0; i < closureType->size(); ++i) {
        result->tuple()->set<slot_t>(i, function->tuple()->get<slot_t>(i), withRule);
    }
    result->tuple()->set<slot_t>(
        closureType->size(),
        camel::core::rtdata::toSlot<::Function *>(rule),
        withRule);
    return result;
}

bool carriesRule(const camel::runtime::GCGraph *graph) {
    return graph != nullptr && graph->stableId().starts_with(kRulePrefix);
}

::Function *ruleOf(const ::Function *function) {
    const auto *type = function->tupleType();
    return camel::core::rtdata::fromSlot<::Function *>(
        function->tuple()->get<slot_t>(type->size() - 1));
}

} // namespace camel::autodiff
