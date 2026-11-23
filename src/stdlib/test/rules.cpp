// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Test-only: the rules of a program written as pairs of functions (`<name>__lhs`,
 * `<name>__rhs`; parameters named `c...` are const), compiled by compileRule, saturate the
 * functions `integer_coefficients`, `integer_chain`, `doubled` and `fault_case`. Prints
 * "RULES <function> add=<n> mul=<n>" for the operators left after extraction.
 */

#include "camel/passes/test/rules.h"

#include "camel/execute/pass/registration.h"
#include "camel/runtime/draft_session.h"
#include "camel/passes/test/cost.h"
#include "camel/runtime/rules.h"

#include <cstdint>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace camel::runtime;

namespace {

/// The rules of the program: every `<name>__lhs` with its `<name>__rhs` compiled into the engine's
/// form. `greedy` is a one-way rewrite, which refuses a rule of a `<=>` and skips the commuting
/// pairs (`_comm`): applied in place they apply to their own result and never stop.
std::vector<Rule> compiledRules(
    const camel::core::context::context_ptr_t &context_, const camel::runtime::Program *program,
    bool greedy, std::ostream &os) {
    std::vector<Rule> rules;
    uint16_t next = 1;
    for (const auto *left : program->graphs()) {
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
        for (const auto *other : program->graphs()) {
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
                    DraftWritings writings(probe.edit(const_cast<GCGraph *>(program->root())).edit());
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
    return rules;
}

void check(const char *name, bool good, std::ostream &os) {
    os << "SAVEPOINT " << name << (good ? " ok" : " FAIL") << "\n";
}

/// The whole frozen graph, node by node, as numbers: the id, the node's own content hash, its kind
/// and the three input runs the graph draws per node. `nodeContentHash` is the node's content
/// complete -- the record, the payload, the order predecessors and the sync predecessor, as the
/// graph defines it -- so two runs that produced the same graph give the same vector, and a node at
/// a wrong id or an edge to a wrong node shows up as a difference, which a count of operators would
/// not.
std::vector<uint64_t> shapeOfGraph(const GCGraph &graph) {
    std::vector<uint64_t> out{graph.nodeCount()};
    for (gc_node_ref_t ref = 0; ref < graph.nodeCount(); ++ref) {
        const GCNode *node = graph.node(ref);
        if (node == nullptr) {
            out.push_back(~uint64_t{0});
            continue;
        }
        out.push_back(ref);
        out.push_back(graph.nodeContentHash(ref));
        out.push_back(static_cast<uint64_t>(node->kind));
        for (auto part :
             {graph.normInputsOf(ref), graph.withInputsOf(ref), graph.ctrlInputsOf(ref)}) {
            out.push_back(part.size());
            for (gc_node_ref_t input : part) {
                out.push_back(input);
            }
        }
    }
    return out;
}

/// The graph of `program` named `name`, mutable, and the shape of the program it becomes after
/// `work` runs on its draft and the session commits. `work` is handed the writings and the draft
/// under them: a case that has to see what the draft did (how many node slots it has spent) reads
/// the draft, and a case that only writes needs the writings.
template<typename Work>
std::optional<std::vector<uint64_t>> shapeAfter(
    const camel::core::context::context_ptr_t &context_, const camel::runtime::Program *program,
    const std::string &name, Work &&work) {
    GCGraph *graph = nullptr;
    for (auto *candidate : program->graphs()) {
        if (candidate->name() == name) {
            graph = candidate;
        }
    }
    if (graph == nullptr) {
        return std::nullopt;
    }
    RuntimeGraphDraftSession session(context_, program);
    auto &draft = session.edit(graph).edit();
    DraftWritings writings(draft);
    work(writings, draft);
    // Detached, as the rewrite and saturation kernels do it: the result is a value the checks read,
    // and the context's own program is left alone (`apply` hands the caller the program it was
    // given).
    const Program *next = session.commit(true);
    return shapeOfGraph(*next->graph(graph->id()));
}

const camel::runtime::Program *run(
    const camel::core::context::context_ptr_t &context_, const camel::runtime::Program *program,
    std::ostream &os, bool greedy) {
    static const std::set<std::string> kTargets{"integer_coefficients", "integer_chain", "doubled", "fault_case"};
    std::vector<Rule> rules = compiledRules(context_, program, greedy, os);

    RuntimeGraphDraftSession session(context_, program);
    std::vector<std::pair<std::string, GraphId>> done;
    for (auto *graph : program->graphs()) {
        if (!kTargets.contains(graph->name())) {
            continue;
        }
        auto &draft = session.edit(graph).edit();
        DraftWritings writings(draft);
        if (greedy) {
            const RewriteStats stats = writings.rewrite(rules);
            os << "RULES " << graph->name() << " rounds=" << stats.rounds
               << " applied=" << stats.applied << " erased=" << stats.erased
               << " fixpoint=" << stats.fixpoint << "\n";
        } else {
            writings.build();
            writings.saturate(rules);
            writings.extract(camel::test::NodeCountCost{}, context_.get());
        }
        done.emplace_back(graph->name(), graph->id());
    }
    const Program *next_program = session.commit();
    for (const auto &[name, id] : done) {
        size_t adds = 0, muls = 0, divs = 0;
        const auto *graph    = next_program->graph(id);
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

const camel::runtime::Program *
SavepointTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    const std::string target       = "integer_chain";
    const std::vector<Rule> oneWay = compiledRules(context_, program, true, os);
    const std::vector<Rule> both   = compiledRules(context_, program, false, os);

    // ① One rewrite, all or nothing. Three things are said of a run that fails partway: the fault
    // was raised after the draft had already taken work (a case that never raised anything proves
    // nothing), the draft it leaves commits to the program it was given, and a run over that same
    // draft produces the program a run that never failed produces -- compared as the whole frozen
    // graph, node by node and edge by edge, not as a count.
    //
    // The fault is not counted off a rule: it is asked of the engine, which asks a condition once
    // for every match it goes on to apply. The second ask throws, so the first application has
    // already been made and the draft holds the work of a rewrite stopped in the middle -- not one
    // stopped before it began. What the run had put in is read at the throw, before the rollback
    // takes it back: the drafts writes it made, the ids it spent, the nodes it took out.
    struct Fault {
        bool raised    = false;
        size_t asked   = 0;
        uint64_t wrote = 0;
        size_t slots   = 0;
        size_t erased  = 0;
    };
    const auto runFaulty = [&](DraftWritings &writings, GraphDraft &draft) {
        Fault fault;
        const size_t before        = draft.nodeSlotCount();
        const size_t liveBefore    = draft.nodeCount();
        const uint64_t wroteBefore = draft.mutations();
        std::vector<Rule> faulty   = oneWay;
        for (Rule &rule : faulty) {
            auto inner = rule.when;
            rule.when  = [&fault, &draft, before, liveBefore, wroteBefore, inner](const Match &m) {
                ++fault.asked;
                if (fault.asked == 2) {
                    fault.raised  = true;
                    fault.wrote   = draft.mutations() - wroteBefore;
                    fault.slots   = draft.nodeSlotCount() - before;
                    fault.erased  = liveBefore > draft.nodeCount() ? liveBefore - draft.nodeCount() : 0;
                    throw std::runtime_error("savepoint case: the rule's condition fails");
                }
                return inner ? inner(m) : true;
            };
        }
        try {
            writings.rewrite(faulty);
        } catch (const std::runtime_error &) {
            // The rewrite put the draft back on its way out; the commit below is what says so.
        }
        return fault;
    };
    const auto pristine =
        shapeAfter(context_, program, target, [](DraftWritings &, GraphDraft &) {});
    Fault failedFault;
    const auto failed =
        shapeAfter(context_, program, target, [&](DraftWritings &writings, GraphDraft &draft) {
            failedFault = runFaulty(writings, draft);
        });
    const auto clean =
        shapeAfter(context_, program, target, [&](DraftWritings &writings, GraphDraft &) {
            writings.rewrite(oneWay);
        });
    Fault rerunFault;
    const auto rerun =
        shapeAfter(context_, program, target, [&](DraftWritings &writings, GraphDraft &draft) {
            rerunFault = runFaulty(writings, draft);
            // A caller that knows nothing was written runs the rewrite again over the same draft.
            writings.rewrite(oneWay);
        });
    os << "SAVEPOINT fault asked=" << failedFault.asked << " wrote=" << failedFault.wrote
       << " slots=" << failedFault.slots << " erased=" << failedFault.erased
       << " rerun_asked=" << rerunFault.asked << " rerun_wrote=" << rerunFault.wrote << "\n";
    check("failed_rewrite_raised", failedFault.raised && failedFault.wrote > 0, os);
    check("failed_rewrite_wrote_nothing", pristine && failed && *pristine == *failed, os);
    check("rerun_is_byte_identical", clean && rerun && *clean == *rerun, os);

    // ② A round the budget cuts. A budget of one writing and one union cuts the first round of
    // `integer_chain` after the application that reaches it: the round reports where it stopped and
    // lands nothing, so a full run over the draft it leaves reaches the fixpoint of a run that was
    // never cut.
    bool cut           = false;
    size_t cutApplied  = 0;
    size_t cutRounds   = 0;
    const auto resumed =
        shapeAfter(context_, program, target, [&](DraftWritings &writings, GraphDraft &) {
            writings.build();
            Budget budget;
            budget.newWritings = 1;
            budget.unions      = 1;
            const SaturateStats stats = writings.saturate(both, budget);
            cut                       = stats.stoppedMidRound;
            cutApplied                = stats.appliedThisRound;
            cutRounds                 = stats.rounds;
            // The draft the cut leaves is where the full run starts: the cut landed nothing, so this
            // is the same draft a run that was never cut starts from.
            writings.saturate(both);
            writings.extract(camel::test::NodeCountCost{}, context_.get());
        });
    const auto never =
        shapeAfter(context_, program, target, [&](DraftWritings &writings, GraphDraft &) {
            writings.build();
            writings.saturate(both);
            writings.extract(camel::test::NodeCountCost{}, context_.get());
        });
    os << "SAVEPOINT budget_cut cut=" << cut << " applied=" << cutApplied
       << " rounds=" << cutRounds << " nodes=" << (clean ? clean->front() : 0) << "\n";
    check("budget_cut_lands_nothing", cut && cutApplied >= 1 && resumed && never && *resumed == *never, os);
    return program;
}

namespace {

using camel::execute::makePass;
using camel::execute::PassKernel;

/// The test-only passes this file implements. Each name is the constant its own header
/// declares, so the name a pipeline writes and the class that answers it are one fact.
const PassKernel rulesKernel(kRulesTestPass, makePass<RulesTestPass>());
const PassKernel rulesGreedyKernel(kRulesGreedyTestPass, makePass<RulesGreedyTestPass>());
const PassKernel savepointKernel(kSavepointTestPass, makePass<SavepointTestPass>());

} // namespace
