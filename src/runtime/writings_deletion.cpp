// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The deletion permission: whether a rule may drop a computation (design principles 9.7).

#include "camel/runtime/writings.h"
#include "camel/runtime/operator_instance.h"
#include "camel/runtime/reachable.h"

#include "camel/core/operator_table.h"
#include "camel/core/tags.h"
#include "camel/core/type/intern.h"
#include "camel/core/type/text.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/draft_writes.h"
#include "camel/runtime/effect_liveness.h"
#include "camel/runtime/graph_txn.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace camel::runtime {

/// Whether deletion is admitted by the caller's selected local guards. Unknown calls and
/// control-region summaries are conservative. This is not an exact execution-set analysis:
/// outside users may keep a discarded binding running.
///
/// One question here is not the caller's to answer: whether the computation is an external access
/// or a write of a variable that outlives the running function (design principles 9.7). Those are
/// kept by the region or the exit that holds them, and only the graph interface's dedicated
/// effect-deleting operation removes one, so this engine refuses the discard instead of letting a
/// guard decide it: `protect_order` reaches the order a computation carries and not them, and
/// `protect_faults` is what the caller still chooses. A computation whose value nobody reads and
/// which does neither is the one the guards are about.
bool DraftWritings::deletionAllowed(gc_node_ref_t node) {
    std::vector<gc_node_ref_t> work{node};
    std::vector<gc_node_ref_t> seen;
    while (!work.empty()) {
        const gc_node_ref_t at = work.back();
        work.pop_back();
        if (std::find(seen.begin(), seen.end(), at) != seen.end()) {
            continue;
        }
        seen.push_back(at);
        const auto *h = draft_.header(at);
        if (h == nullptr || !draft_.alive(at)) {
            continue;
        }
        // The order guard keeps the computations whose place in the order is not the program's to
        // drop: one that carries an ordering edge, one something waits on, one whose value is
        // written in place. It asks only when the fault guard is selected too -- the conservative
        // compatibility guard the two selections have always bought together.
        if (protection_.order && protection_.faults &&
            (carriesOrdering(draft_, at, true) || !draft_.waitInputsOf(at).empty() ||
             writtenInPlace(at))) {
            return false;
        }
        if (protection_.faults && nodeMayFault(draft_, at, true)) {
            return false;
        }
        // The effect itself, which no selected guard may take away (9.7): an external access, a
        // write of state the program owns, or a write of a value the program keeps in a variable.
        // `writtenInPlace` is the tree's answer to "some user writes this value" -- the cell a
        // variable lives in, or an argument an operator or a `var` parameter writes -- which is the
        // write that outlives the call when the variable is not this function's own. A call, a
        // loop and a control region carry no local summary, so they are kept as if they had one.
        bool effect = writtenInPlace(at);
        if (h->kind == GCNodeKind::Oper) {
            const auto bytes = draft_.payloadOf(at);
            effect |= bytes.size() < sizeof(GCOperBody) ||
                      !camel::core::OperatorTable::instance().mayBeRecomputed(
                          reinterpret_cast<const GCOperBody *>(bytes.data())->oper);
        } else if (
            h->kind == GCNodeKind::Call || h->kind == GCNodeKind::Func ||
            h->kind == GCNodeKind::Loop || h->kind == GCNodeKind::Brch ||
            h->kind == GCNodeKind::Join) {
            effect = true; // No local summary proves these computations effect-free.
        }
        if (effect) {
            return false;
        }
        for (auto part :
             {draft_.normInputsOf(at),
              draft_.withInputsOf(at),
              draft_.ctrlInputsOf(at),
              draft_.syncInputsOf(at)}) {
            for (gc_node_ref_t in : part) {
                work.push_back(in);
            }
        }
    }
    return true;
}

bool DraftWritings::dropsProtected(
    const Rule &rule, std::span<const gc_node_ref_t> bind, const MatchRemainder &remainder) {
    std::vector<uint8_t> read(bind.size(), 0);
    const std::function<void(const Pattern &)> mark = [&](const Pattern &p) {
        if (p.kind == Pattern::Kind::Var) {
            if (p.var < read.size()) {
                read[p.var] = 1;
            }
            return;
        }
        for (const Pattern &arg : p.args) {
            mark(arg);
        }
    };
    mark(rule.rhs);
    for (size_t var = 0; var < bind.size(); ++var) {
        if (bind[var] == kInvalidNodeRef || read[var]) {
            continue;
        }
        if (remainder.set && remainder.var == var) {
            // The terms the left side did not name: the right side builds them into one value, so
            // they are dropped only if it does not read that variable at all.
            for (gc_node_ref_t term : remainder.terms) {
                if (!deletionAllowed(term)) {
                    return true;
                }
            }
            continue;
        }
        if (!deletionAllowed(bind[var])) {
            return true;
        }
    }
    return false;
}

} // namespace camel::runtime
