// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Test-only reading: **every OPER node a graph reports, and whether it names an operator.**
 *
 * `graph_view.h` folds CAST, COPY, FILL and ACCS into `NodeKind::Oper` while the front end and the
 * passes still build them, and `operatorOf` answers `kNoOper` for those four because none of them
 * carries an operator record. So one question -- "which operator is this node" -- takes two steps
 * for a caller today: `kindOf(ref) == NodeKind::Oper && operatorOf(ref) != kNoOper`. The two steps
 * can be deleted exactly when every OPER node answers an operator, and that is a reading, not an
 * opinion:
 *
 *     [OO] oper=N answered=M missing=K graphs=G ok=<0|1>
 *
 * N is how many OPER nodes were asked, M how many answered an operator, K how many did not, G how
 * many graphs were walked. Every OPER node is asked on **every** graph of the program, so N is not
 * a sample: a program that builds a CAST somewhere is counted there. `ok` is 1 only when N > 0:
 * **an empty set must not read as agreement** (`M == N == 0` is what a probe that walked nothing
 * prints), so a run that asked nothing says `ok=0`.
 *
 * Each node that could not answer is named on its own line, with the origin the construction point
 * registered on it -- `gir.cast`, `gir.fill`, `gir.access` are the front end's three, and a node a
 * pass built carries that pass's own label or none:
 *
 *     [OO] missing graph=<id> ref=<ref> origin=<label|none> at <file>:<line> norm=<n> with=<m>
 *
 * The label is why this line exists: without it a run shows *that* some OPER node names nothing,
 * and not *who built it*. The tally after the list is the same fact summed by label, so a corpus of
 * a hundred nodes reads as "which construction points still produce a kind".
 *
 * Printed with the summary line last: a failing case's stdout is read from its tail, so the line a
 * plan pins has to be the one at the end. The per-node lines are capped at kListedMissing, with the
 * rest counted on the tally, so a large corpus cannot bury the summary either.
 */

#include "camel/passes/test/operator_records.h"

#include "camel/execute/pass/registration.h"
#include "graph/callee_graph_of.h"
#include "graph/gc_graph_view.h"
#include "graph/graph_view.h"

#include "camel/core/source/manager.h"

#include <algorithm>
#include <format>
#include <map>
#include <string>
#include <vector>

using camel::execute::makePass;
using camel::execute::PassKernel;
using camel::graph::FrozenGraphAdapter;
using camel::graph::NodeKind;
using camel::graph::NodeRef;
using camel::graph::PortGroup;
using camel::runtime::GCGraph;
using camel::runtime::GraphId;
using camel::runtime::Program;

namespace {

/// One OPER node that names no operator.
struct Unnamed {
    uint32_t graph = 0; // the graph's id, as the number it is; `GraphId` is an enum class
    NodeRef ref    = camel::graph::kNoNode;
    std::string label; // the construction point's own label, or "none"
    std::string where; // "<file>:<line>" when the origin resolves to a source position
    size_t norm = 0;
    size_t with = 0;
};

/// How many unnamed nodes are printed one by one; the rest are counted on the tally line.
constexpr size_t kListedMissing = 24;

} // namespace

const Program *OperatorRecordsTestPass::apply(const Program *program, std::ostream &os) {
    if (program == nullptr) {
        return program;
    }
    // A callee is named by value, and the program is the table that holds those graphs
    // (graph/callee_graph_of.h): the same resolver `test::graph_faces` reads its graphs through.
    const camel::graph::CalleeGraphOf calleeGraphOf =
        [program](GraphId id) -> const camel::graph::Graph * { return program->graph(id); };
    // Held for the whole walk: the manager is the context's, and a temporary shared_ptr would be a
    // lock the walk no longer needs to take twice.
    const auto sourceContext = context_ ? context_->sourceContext() : nullptr;
    const camel::source::SourceContext *source = sourceContext.get();

    size_t graphs   = 0;
    size_t oper     = 0;
    size_t answered = 0;
    std::vector<Unnamed> unnamed;
    std::map<std::string, size_t> byLabel;

    for (GCGraph *graph : program->graphs()) {
        if (graph == nullptr) {
            continue;
        }
        ++graphs;
        const FrozenGraphAdapter view(*graph, calleeGraphOf);
        for (const NodeRef ref : view.nodes()) {
            if (view.kindOf(ref) != NodeKind::Oper) {
                continue;
            }
            ++oper;
            if (view.operatorOf(ref) != camel::core::kNoOper) {
                ++answered;
                continue;
            }
            Unnamed one;
            one.graph = static_cast<uint32_t>(graph->id());
            one.ref   = ref;
            one.norm  = view.inputs(ref, PortGroup::Normal).size();
            one.with  = view.inputs(ref, PortGroup::Context).size();
            one.where = "";
            one.label = "none";
            if (source != nullptr) {
                const uint64_t origin = view.originOf(ref);
                if (origin != camel::source::kInvalidOriginId) {
                    if (const auto *record = source->origin(origin); record != nullptr) {
                        if (!record->label.empty()) {
                            one.label = record->label;
                        }
                        if (record->primarySpan != camel::source::kInvalidSpanId) {
                            // The file's own name, not the path: the runner prints a head of a
                            // failing case's stdout, and an absolute path per line is what eats
                            // that budget -- the line a plan pins would be the one cut off.
                            const std::string path = source->pathForOrigin(origin);
                            const size_t slash     = path.find_last_of("/\\");
                            const std::string name =
                                slash == std::string::npos ? path : path.substr(slash + 1);
                            const auto range = source->resolveOrigin(origin);
                            one.where        = std::format("{}:{}", name, range.start.line + 1);
                        }
                    }
                }
            }
            ++byLabel[one.label];
            unnamed.push_back(std::move(one));
        }
    }

    // The named nodes first, the summary last. A failing case's stdout is printed from its tail,
    // and the program's own output is flushed before this pass speaks, so the head of this block is
    // what gets cut -- the summary line is the one a plan pins, so it goes at the end where nothing
    // can push it out.
    const size_t listed = std::min(unnamed.size(), kListedMissing);
    for (size_t i = 0; i < listed; ++i) {
        const Unnamed &one = unnamed[i];
        os << "[OO] missing graph=" << one.graph << " ref=" << one.ref << " origin=" << one.label
           << (one.where.empty() ? "" : (" at " + one.where)) << " norm=" << one.norm
           << " with=" << one.with << "\n";
    }
    if (unnamed.size() > listed) {
        os << "[OO] missing more=" << (unnamed.size() - listed) << "\n";
    }
    if (!byLabel.empty()) {
        os << "[OO] built-by";
        for (const auto &[label, count] : byLabel) {
            os << " " << label << "=" << count;
        }
        os << "\n";
    }
    if (oper == 0) {
        os << "[OO] refused: the walk asked no OPER node, so nothing was measured\n";
    }
    // The line a plan pins. `ok` is 1 only when something was asked: N = 0 is a probe that walked
    // no OPER node, and "every OPER node answers" over an empty set is not an answer.
    os << "[OO] oper=" << oper << " answered=" << answered << " missing=" << unnamed.size()
       << " graphs=" << graphs << " ok=" << (oper > 0 ? 1 : 0) << "\n";
    return program;
}

namespace {

/// The test-only passes this file implements. Each name is the constant its own header declares,
/// so the name a pipeline writes and the class that answers it are one fact.
const PassKernel operatorRecordsKernel(
    kOperatorRecordsTestPass, makePass<OperatorRecordsTestPass>());

} // namespace