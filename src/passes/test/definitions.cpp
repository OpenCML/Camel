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
 * Created: Oct. 16, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * TEST-ONLY checks of the definition graphs of operators (see definitions.h).
 */

#include "definitions.h"

#include "camel/core/context/context.h"
#include "camel/core/operator_table.h"
#include "camel/runtime/definitions.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/reachable.h"

#include <format>
#include <iostream>
#include <map>
#include <ostream>
#include <stdexcept>

namespace {

using camel::core::OperatorTable;
using namespace camel::core::sem;
namespace rt = camel::runtime;

DefinitionParam operand(uint8_t index) {
    return DefinitionParam{.kind = DefinitionParam::Kind::Norm, .operand = index};
}

/// What the store answers for the operator `uri`: "built ..." or the reason it refuses.
std::string answer(camel::core::context::Context &context, std::string_view uri) {
    const auto id = OperatorTable::instance().lookup(uri);
    if (!id) {
        return "unknown operator";
    }
    std::string problem;
    const rt::DefinitionGraph *d = context.definitions().definitionOf(*id, &problem);
    if (d == nullptr) {
        return "refused: " + problem;
    }
    return std::format(
        "built {} graph {} params {} hash {:016x}",
        d->key.toString(),
        d->graph->name(),
        d->graph->normPorts().size(),
        d->contentHash);
}

} // namespace

const camel::runtime::Program *
DefinitionsTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    OperatorTable &table = OperatorTable::instance();

    // ---- The definitions the tensor module declares. ----
    for (const char *uri :
         {"tensor:softmax", "tensor:log_softmax", "tensor:mean", "tensor:relu", "tensor:sigmoid"}) {
        os << "DEFINITION " << uri << " " << answer(*context_, uri) << "\n";
    }
    const auto softmax = *table.lookup("tensor:softmax");
    const auto *first  = context_->definitions().definitionOf(softmax);
    const auto *again  = context_->definitions().definitionOf(softmax);
    os << "DEFINITION built_once " << (first != nullptr && first == again ? "yes" : "no") << "\n";
    os << "DEFINITION none " << answer(*context_, "tensor:matmul") << "\n";

    // Same-version replacement is legal in the registry. Neither a previously built
    // graph nor a remembered refusal may survive a different declaration record.
    const auto original           = *table.semantics(softmax);
    auto replaced                 = original;
    replaced.definition->function = "no_such_definition";
    table.declareSemantics("tensor:softmax", replaced);
    std::string replacementProblem;
    const bool refused =
        context_->definitions().definitionOf(softmax, &replacementProblem) == nullptr;
    table.declareSemantics("tensor:softmax", original);
    const bool recovered = context_->definitions().definitionOf(softmax) != nullptr;
    os << "DEFINITION same_version_replacement " << (refused && recovered ? "yes" : "no") << "\n";
    if (!refused || !recovered)
        throw std::runtime_error("stale definition after same-version replacement");

    // ---- Registration (check 6). ----
    try {
        table.declareDefinition(
            "tensor:tanh",
            Definition{.module = "bad_definitions", .params = {operand(0)}});
        os << "DEFINITION without_function accepted\n";
    } catch (const std::invalid_argument &e) {
        os << "DEFINITION without_function rejected: " << e.what() << "\n";
    }

    // ---- Refused when built: each declared on a tensor operator, from bad_definitions. ----
    const auto declare =
        [&](const char *uri, const char *module, const char *function, size_t arity) {
            Definition d{.module = module, .function = function};
            for (size_t i = 0; i < arity; ++i) {
                d.params.push_back(operand(static_cast<uint8_t>(i)));
            }
            table.declareDefinition(uri, std::move(d));
        };
    const auto originalExp = *table.semantics(*table.lookup("tensor:exp"));
    declare("tensor:exp", "bad_definitions", "exp_through_softmax", 1);
    declare("tensor:abs", "bad_definitions", "abs_itself", 1);
    declare("tensor:neg", "bad_definitions", "two", 1);
    declare("tensor:sqrt", "bad_definitions", "noisy", 1);
    declare("tensor:rsqrt", "bad_definitions", "nope", 1);
    declare("tensor:log", "bad_definitions", "generic", 1);
    declare("tensor:gelu", "broken_definitions", "gelu", 1);
    declare("tensor:erf", "no_such_library", "erf", 1);
    for (const auto &[check, uri] : std::map<std::string, std::string>{
             {"cycle", "tensor:exp"},
             {"reaches_itself", "tensor:abs"},
             {"arity", "tensor:neg"},
             {"effect_of_pure", "tensor:sqrt"},
             {"missing_function", "tensor:rsqrt"},
             {"generic", "tensor:log"},
             {"library_does_not_compile", "tensor:gelu"},
             {"library_not_found", "tensor:erf"},
         }) {
        os << "DEFINITION " << check << " " << answer(*context_, uri) << "\n";
    }
    // softmax, built and accepted before exp had a definition, is on the cycle now too.
    os << "DEFINITION cycle_refuses_softmax " << answer(*context_, "tensor:softmax") << "\n";
    auto restoredExp = originalExp;
    restoredExp.key  = table.semantics(*table.lookup("tensor:exp"))->key;
    table.declareSemantics("tensor:exp", restoredExp);
    const bool cycleCleared = context_->definitions().definitionOf(softmax) != nullptr;
    os << "DEFINITION removed_cycle_recovered " << (cycleCleared ? "yes" : "no") << "\n";
    if (!cycleCleared)
        throw std::runtime_error("definition refusal survived removal of its cycle");
    return program;
}

const camel::runtime::Program *
ExpandDefinitionsTestPass::apply(const camel::runtime::Program *program, std::ostream &) {
    camel::runtime::GCGraph *graph = program->root();
    const auto &table = OperatorTable::instance();
    rt::RuntimeGraphDraftSession session(context_, program);
    std::map<std::string, size_t> expanded;
    for (rt::GCGraph *g : rt::collectReachableGraphs(graph)) {
        std::vector<rt::gc_node_ref_t> nodes;
        for (rt::gc_node_ref_t ref = 0; ref < g->nodeCount(); ++ref) {
            if (g->node(ref)->kind != rt::GCNodeKind::Oper) {
                continue;
            }
            const auto *s = table.semantics(g->nodeBodyAs<rt::GCOperBody>(ref)->oper);
            if (s != nullptr && s->definition) {
                nodes.push_back(ref);
            }
        }
        if (nodes.empty()) {
            continue;
        }
        rt::GraphDraft &draft = session.edit(g);
        for (rt::gc_node_ref_t ref : nodes) {
            const std::string uri(g->nodeBodyAs<rt::GCOperBody>(ref)->uri());
            const rt::ExpandResult result =
                rt::expandDefinition(session, draft, draft.draftIdOfSourceRef(ref));
            if (result) {
                ++expanded[uri];
            } else {
                std::cerr << "NOT EXPANDED " << uri << ": " << result.problem << "\n";
            }
        }
    }
    for (const auto &[uri, count] : expanded) {
        std::cerr << "EXPANDED " << uri << " " << count << "\n";
    }
    return session.commit();
}
