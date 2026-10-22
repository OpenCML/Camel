// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
#include <stdexcept>

using namespace camel::runtime;

namespace {

const camel::runtime::Program *run(
    const camel::core::context::context_ptr_t &context_, const camel::runtime::Program *program,
    std::ostream &os, bool greedy) {
    static const std::set<std::string> kTargets{"integer_coefficients", "integer_chain", "doubled", "fault_case"};
    std::vector<Rule> rules;
    uint16_t next = 1;
    for (const GCGraph *left : program->graphs()) {
        const std::string &name = left->name();
        if (!name.ends_with("__lhs")) {
            continue;
        }
        const std::string base = name.substr(0, name.size() - 5);
        // Commuting never ends in place: the rewrite applies it to its own result.
        if (greedy && base.ends_with("_comm")) {
            continue;
        }
        const GCGraph *right   = nullptr;
        for (const GCGraph *other : program->graphs()) {
            if (other->name() == base + "__rhs") {
                right = other;
            }
        }
        RuleSource source;
        source.name          = base;
        source.bidirectional = base.ends_with("_both");
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
    if (greedy) {
        // A rule that holds both ways is for a saturation: the rewrite refuses it.
        std::vector<Rule> oneWay;
        for (Rule &rule : rules) {
            if (rule.twoWay) {
                try {
                    RuntimeGraphDraftSession probe(context_, program);
                    DraftWritings writings(probe.edit(const_cast<GCGraph *>(program->root())));
                    writings.rewrite(std::span<const Rule>(&rule, 1));
                } catch (const std::invalid_argument &e) {
                    os << "RULES refused: " << e.what() << "\n";
                }
            } else {
                oneWay.push_back(std::move(rule));
            }
        }
        rules = std::move(oneWay);
    }

    RuntimeGraphDraftSession session(context_, program);
    std::vector<std::pair<std::string, GraphId>> done;
    for (GCGraph *graph : program->graphs()) {
        if (!kTargets.contains(graph->name())) {
            continue;
        }
        GraphDraft &draft = session.edit(graph);
        DraftWritings writings(draft);
        if (greedy) {
            const RewriteStats stats = writings.rewrite(rules);
            os << "RULES " << graph->name() << " rounds=" << stats.rounds
               << " applied=" << stats.applied << " erased=" << stats.erased
               << " fixpoint=" << stats.fixpoint << "\n";
        } else {
            writings.build();
            writings.saturate(rules);
            writings.extract(*findCostModel("nodes"), context_.get());
        }
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

} // namespace

const camel::runtime::Program *
RulesTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    return run(context_, program, os, false);
}

const camel::runtime::Program *
RulesGreedyTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    return run(context_, program, os, true);
}
