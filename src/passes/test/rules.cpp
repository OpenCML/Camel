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
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */


/*
 * Test-only: the rules of a program written as pairs of functions (`<name>__lhs`,
 * `<name>__rhs`; parameters named `c...` are const), compiled by compileRule, saturate the
 * functions `integer_coefficients`, `integer_chain`, `doubled` and `fault_case`. Prints
 * "RULES <function> add=<n> mul=<n>" for the operators left after extraction.
 */
#include "camel/passes/test/rules.h"

#include "camel/runtime/draft_session.h"
#include "camel/runtime/rules.h"

#include <set>

using namespace camel::runtime;

const camel::runtime::Program *
RulesTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    static const std::set<std::string> kTargets{"integer_coefficients", "integer_chain", "doubled", "fault_case"};
    std::vector<Rule> rules;
    uint16_t next = 1;
    for (const GCGraph *left : program->graphs()) {
        const std::string &name = left->name();
        if (!name.ends_with("__lhs")) {
            continue;
        }
        const std::string base = name.substr(0, name.size() - 5);
        const GCGraph *right   = nullptr;
        for (const GCGraph *other : program->graphs()) {
            if (other->name() == base + "__rhs") {
                right = other;
            }
        }
        RuleSource source;
        source.name    = base;
        source.id      = next++;
        source.program = program;
        source.lhs     = left->id();
        source.rhs     = right->id();
        const auto names = left->funcType()->argNames();
        for (size_t i = 0; i < left->normPorts().size(); ++i) {
            source.constParams.push_back(i < names.size() && names[i].starts_with("c") ? 1 : 0);
        }
        try {
            for (Rule &rule : compileRule(*context_, source)) {
                rules.push_back(std::move(rule));
            }
        } catch (const RuleCompileError &e) {
            os << "RULES error " << base << ": " << e.what() << "\n";
        }
    }
    os << "RULES compiled=" << rules.size() << "\n";

    RuntimeGraphDraftSession session(context_, program);
    std::vector<std::pair<std::string, GraphId>> done;
    for (GCGraph *graph : program->graphs()) {
        if (!kTargets.contains(graph->name())) {
            continue;
        }
        GraphDraft &draft = session.edit(graph);
        DraftWritings writings(draft);
        writings.build();
        writings.saturate(rules);
        writings.extract(*findCostModel("nodes"), context_.get());
        done.emplace_back(graph->name(), graph->id());
    }
    const Program *next_program = session.commit();
    for (const auto &[name, id] : done) {
        size_t adds = 0, muls = 0, divs = 0;
        const GCGraph *graph = next_program->graph(id);
        for (gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
            if (graph->node(ref)->kind != GCNodeKind::Oper) {
                continue;
            }
            const auto uri = graph->nodeBodyAs<GCOperBody>(ref)->uri();
            adds += uri.starts_with(":op/add_");
            muls += uri.starts_with(":op/mul_");
            divs += uri.starts_with(":op/div_");
        }
        os << "RULES " << name << " add=" << adds << " mul=" << muls << " div=" << divs << "\n";
    }
    return next_program;
}
