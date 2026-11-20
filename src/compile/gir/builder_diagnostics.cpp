// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The diagnostics and the effect analysis of a built program: effects that are dropped,
 * values that nothing takes, and the notes that explain a conversion.
 */

#include "builder.h"
#include "builder_helpers.h"
#include "builder_support.h"

#include "camel/compile/gir/static_function.h"
#include "camel/core/data/composite/array.h"
#include "camel/core/numeric_convert.h"
#include "camel/core/operator_names.h"
#include "camel/core/resource_semantics.h"
#include "camel/core/rtdata/string.h"
#include "camel/execute/entry_args.h"
#include "camel/runtime/control.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/region_roots.h"
#include "camel/utils/log.h"
#include "camel/utils/scope.h"
#include "camel/utils/str.h"
#include "camel/utils/type.h"
#include "core/module/entity_internal.h"

#include "camel/core/operator_traits.h"
#include "camel/core/rtdata/half.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/other.h"

#include <atomic>
#include <cctype>
#define DEBUG_LEVEL -1

using namespace std;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::data;
using namespace camel::core::type;

namespace sem = camel::core::sem;

namespace camel::compile::gir {

std::set<node_handle_t> Builder::capturedWritesOf(DraftGraphBuilder *root) const {
    const auto isOpen = [&](DraftGraphBuilder *graph) {
        auto state = bodyStates_.find(graph);
        return state != bodyStates_.end() && state->second == BodyState::Open;
    };
    const auto summaryOf = [&](DraftGraphBuilder *graph) -> const CaptureSummary * {
        auto summary = captures_.find(graph);
        return summary == captures_.end() ? nullptr : &summary->second;
    };
    // A graph writes only variables it captured, and a write reaches `root` only through
    // graphs that all captured the variable: only the callees that captured one of root's
    // variables matter. (With all modules on one root, the calls reach across the whole
    // program; walking every callee made this quadratic in the length of a call chain.)
    const auto *rootSummary = summaryOf(root);
    if (rootSummary == nullptr || rootSummary->variables.empty()) {
        return {};
    }
    const std::unordered_set<node_handle_t> rootVariables(
        rootSummary->variables.begin(),
        rootSummary->variables.end());
    const auto sharesVariable = [&](const CaptureSummary &summary) {
        return std::ranges::any_of(summary.variables, [&](node_handle_t variable) {
            return rootVariables.contains(variable);
        });
    };
    // The graphs reachable through direct calls and arms. An open body is not looked into:
    // what it will write is not known yet, so all it captured stands for it.
    std::vector<DraftGraphBuilder *> reached{root};
    std::unordered_set<DraftGraphBuilder *> seen{root};
    std::unordered_map<DraftGraphBuilder *, std::set<node_handle_t>> writes;
    for (size_t i = 0; i < reached.size(); ++i) {
        DraftGraphBuilder *graph = reached[i];
        const auto *summary      = summaryOf(graph);
        if (isOpen(graph)) {
            writes[graph].insert(summary->variables.begin(), summary->variables.end());
            continue;
        }
        writes[graph] = summary->written;
        for (DraftGraphBuilder *callee : summary->callees) {
            const auto *calleeSummary = summaryOf(callee);
            if (calleeSummary != nullptr && sharesVariable(*calleeSummary) &&
                seen.insert(callee).second) {
                reached.push_back(callee);
            }
        }
    }
    // Least fixpoint of: a graph writes what its callees write, as far as it captured the
    // variable itself (a callee's write of the graph's own local variable stops there). A
    // recursive call adds nothing to the function's own set, so recursion needs no special
    // case here.
    for (bool changed = true; changed;) {
        changed = false;
        for (DraftGraphBuilder *graph : reached) {
            const auto *summary = summaryOf(graph);
            if (isOpen(graph)) {
                continue;
            }
            auto &own = writes[graph];
            for (DraftGraphBuilder *callee : summary->callees) {
                auto calleeWrites = writes.find(callee);
                if (calleeWrites == writes.end()) {
                    continue; // not reached: it captured none of root's variables
                }
                for (node_handle_t variable : calleeWrites->second) {
                    if (std::ranges::find(summary->variables, variable) !=
                            summary->variables.end() &&
                        own.insert(variable).second) {
                        changed = true;
                    }
                }
            }
        }
    }
    return writes[root];
}

std::vector<Builder::CapturedUse> Builder::capturedUsesOf(const compile_graph_ptr_t &callee) {
    auto &callees = captures_[currGraph_.get()].callees;
    if (std::ranges::find(callees, callee.get()) == callees.end()) {
        callees.push_back(callee.get());
    }
    if (ArmBuild *arm = activeArm()) {
        arm->callees.push_back(callee.get());
    }
    auto summary = captures_.find(callee.get());
    if (summary == captures_.end() || summary->second.variables.empty()) {
        return {};
    }
    // Capturing into the current graph may add to its summary: copies.
    const std::vector<node_handle_t> variables = summary->second.variables;
    const std::set<node_handle_t> written      = capturedWritesOf(callee.get());
    std::vector<CapturedUse> uses;
    for (node_handle_t origin : variables) {
        // The callee's variable, as the current graph sees it (see captureInto) -- but only when
        // this graph is where that variable lives or lives inside it. A callee reached through a
        // value was made somewhere else, and the cells of its captures travel inside that value:
        // they are not this graph's to name, and naming them here is what a capture it cannot
        // make is. A callee of an enclosing graph is unaffected, which is every direct call.
        if (!enclosesOrigin(origin)) {
            continue;
        }
        node_handle_t variable = captureInto(currGraph_, origin, captureNameOf(origin, ""));
        uses.push_back({variable, origin, written.contains(origin)});
    }
    return uses;
}

void Builder::reportIgnoredCapturedWrites() {
    for (const PendingIgnoredWrite &pending : unit_->pendingIgnoredWrites) {
        // A write to a variable the callee captured and did not hand back at the call. A `var`
        // write is not an effect (design principles 3.1), so `pending.variables` names a variable
        // only when one of the bodies this statement runs writes it.
        std::set<node_handle_t> written;
        for (DraftGraphBuilder *graph : pending.graphs) {
            written.merge(capturedWritesOf(graph));
        }
        std::vector<std::string> names;
        for (node_handle_t variable : pending.variables) {
            if (written.contains(variable)) {
                names.push_back("'" + captureNameOf(variable, originNames_[variable]) + "'");
            }
        }
        if (names.empty()) {
            continue;
        }
        diags_->of(SemanticDiag::IgnoredSideEffect)
            .atOrigin(pending.origin)
            .commit(
                pending.subject,
                std::format(
                    "{} the captured variable{} {}",
                    pending.verb,
                    names.size() > 1 ? "s" : "",
                    strutil::join(names, ", ", [](const std::string &name) { return name; })));
    }
    unit_->pendingIgnoredWrites.clear();
}

bool Builder::isEffectNode(DraftGraphBuilder *graph, node_handle_t node) const {
    // What a call touches is in the resources of the value's type, not in a flag on it: `sync` is
    // a property of a body block, not of a type (design principles 3.3, effects plan 5). A clause
    // that names a resource means the call touches it, `pure` means it touches nothing.
    // A `var` parameter is not an effect of the call (design principles 3.1 and 3.5):
    // what it writes is a value passed in and handed back, written into the caller's
    // cell where the call stands, so the shape of a signature says nothing about anything outside
    // the program. An assignment is that shape as well -- its first parameter is a `var` cell --
    // and it is declared state-only for the same reason (builtin_semantics.cpp).
    const auto clauseSaysEffect = [](const FunctionType *func) {
        return func->resources().mode == sem::ResourceSignature::Mode::Concrete &&
               !func->resources().pure();
    };
    const auto calleeOf = [&](node_handle_t func) { return graph->funcTarget(func); };
    switch (nodeKindOf(node)) {
    case runtime::GCNodeKind::Oper: {
        const auto &op = graph->operTarget(node);
        if (op == nullptr) {
            return false;
        }
        // The version edge is not a statement of its own: it reads nothing of what it is given and
        // writes nothing, it orders the call it carries (docs/design/effects/edges.cn.md 3.1, 5.6).
        // The effect is that call's, and it is reported by its own name when it is not run; calling
        // the reader of a version an unexecuted statement would name an operator no program can
        // write, which is how the warning says it looked at the position and not at the thing.
        if (op->uri() == ":var/after") {
            return false;
        }
        // A higher-order operator's accesses are the function operand's (design principles 3.3):
        // the same reading the Call case below makes of a value's type, at the
        // operand the declaration names. A value that is not there, or a type that writes no
        // clause, keeps the answer the table gives -- a `map` over a pure function is not an
        // effect, and one over a function that prints is.
        const auto &table = camel::core::OperatorTable::instance();
        const auto id     = table.lookup(op->uri());
        if (id.has_value()) {
            if (const auto *facts = table.semantics(*id);
                facts != nullptr && facts->resources && facts->resources->functionOperand) {
                const auto &where = *facts->resources->functionOperand;
                if (where.kind == sem::ResourceBinding::Kind::Norm ||
                    where.kind == sem::ResourceBinding::Kind::With) {
                    const auto operands = where.kind == sem::ResourceBinding::Kind::Norm
                                              ? normInputsOf(node)
                                              : withInputsOf(node);
                    if (where.index < operands.size()) {
                        Type *type = nodeTypeOf(nodeDraftOf(node)->ref(operands[where.index]));
                        const auto *value =
                            type != nullptr && type->code() == TypeCode::Function
                                ? tt::as_ptr<FunctionType>(type)
                                : nullptr;
                        if (const auto clause =
                                camel::core::functionOperandClause(table, *id, value)) {
                            return !clause->pure();
                        }
                    }
                }
            }
        }
        return table.hasExternalEffect(op->uri());
    }
    case runtime::GCNodeKind::Func: {
        // A call of a function this compiler has a body for is judged by that body first
        // (effectfulGraphs_); its signature is the summary of the same body, so a clause that
        // names a resource says the same thing. A signature with no clause is one the measuring
        // walk could not write down, which is not evidence of an effect.
        const auto callee = calleeOf(node);
        if (callee == nullptr) {
            return false;
        }
        if (effectfulGraphs_.contains(callee.get())) {
            return true;
        }
        const FunctionType *type = callee->funcType();
        return type != nullptr && clauseSaysEffect(type);
    }
    case runtime::GCNodeKind::Call: {
        // A call through a function value: the value's type is all there is to read at this call,
        // and one that writes no clause promises nothing -- what it reaches is fixed by the
        // instantiation, which may write a cell or print -- so it is read conservatively. Only a
        // type that promises the empty set says the call is not an effect.
        const auto callee = withInputsOf(node);
        if (callee.empty()) {
            return false;
        }
        Type *type = nodeTypeOf(nodeDraftOf(node)->ref(callee[0]));
        if (type == nullptr || type->code() != TypeCode::Function) {
            return false;
        }
        const auto *func = tt::as_ptr<FunctionType>(type);
        return !func->resources().pure();
    }
    case runtime::GCNodeKind::Loop: {
        // Each round runs the condition and the body, regions of the graph.
        const auto &draft = graph->transaction().graph();
        for (auto part : {runtime::GCRegionKind::LoopCond, runtime::GCRegionKind::LoopBody}) {
            const uint32_t region = draft.loopRegionOf(nodeIdOf(node), part);
            if (region == 0) {
                continue;
            }
            for (draft_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
                if (draft.alive(id) && draft.regionWithin(draft.nodeRegion(id), region) &&
                    isEffectNode(graph, draft.ref(id))) {
                    return true;
                }
            }
        }
        return false;
    }
    case runtime::GCNodeKind::Join: {
        // The branch is an effect when what an arm gives depends on one inside the arm.
        const auto &draft           = graph->transaction().graph();
        const draft_node_ref_t join = nodeIdOf(node);
        const auto *body =
            reinterpret_cast<const runtime::GCJoinBody *>(draft.payloadOf(join).data());
        std::vector<uint32_t> arms;
        for (size_t k = 0; k < draft.withInputsOf(join).size(); ++k) {
            if (const uint32_t region = draft.armRegionOf(body->brch, k); region != 0) {
                arms.push_back(region);
            }
        }
        const auto inArms = [&](draft_node_ref_t id) {
            const uint32_t region = draft.nodeRegion(id);
            return region != 0 && std::ranges::any_of(arms, [&](uint32_t arm) {
                       return draft.regionWithin(region, arm);
                   });
        };
        std::unordered_set<draft_node_ref_t> seen;
        std::vector<draft_node_ref_t> work(
            draft.withInputsOf(join).begin(),
            draft.withInputsOf(join).end());
        while (!work.empty()) {
            const draft_node_ref_t id = work.back();
            work.pop_back();
            if (!inArms(id) || !seen.insert(id).second) {
                continue;
            }
            if (isEffectNode(graph, draft.ref(id))) {
                return true;
            }
            for (auto inputs :
                 {draft.normInputsOf(id), draft.withInputsOf(id), draft.ctrlInputsOf(id)}) {
                work.insert(work.end(), inputs.begin(), inputs.end());
            }
            // A sync edge carries "used" (docs/design/effects/edges.cn.md 1.5).
            work.insert(work.end(), draft.syncInputsOf(id).begin(), draft.syncInputsOf(id).end());
        }
        return false;
    }
    default:
        return false;
    }
}

bool Builder::callIsChainStatement(node_handle_t node) {
    // What a `sync` block's chain carries is decided by what a call runs (design principles 3.4:
    // "调用按被调函数推出的事实判：函数体含可能出故障的计算、或函数是递归的（递归可能超过深度上限）
    // 就算可能出故障"). Everything but a direct call is asked of the node itself: an operator that
    // is not pure, a cast that may fail, a branch or a loop that carries a region.
    if (!runtime::isChainStatement(*nodeDraftOf(node), nodeIdOf(node))) {
        return false;
    }
    if (!nodeIsKind(node, runtime::GCNodeKind::Func)) {
        // An effect of its own, a computation that may fault, or a call through a function value:
        // the last one runs something no body of this program names, and nothing says it cannot
        // fault.
        return true;
    }
    if (isEffectNode(currGraph_.get(), node)) {
        // The declaration is the whole truth a call site reads (design principles 3.3): a clause
        // that names a resource, or a `var` parameter, says that a call of this reaches something
        // which is not the graph -- and the body may well touch nothing, which is the promise the
        // clause makes to its callers. `test/cases/effects/declared.cml` is that shape: `consume`
        // writes the handle its clause names and its body only returns 0.
        return true;
    }
    const compile_graph_ptr_t callee = nodeGraphOf(node)->funcTarget(node);
    return callee == nullptr || !calleeIsFreelyArranged(callee.get());
}

bool Builder::calleeIsFreelyArranged(DraftGraphBuilder *callee) {
    const auto known = freelyArrangedGraphs_.find(callee);
    if (known != freelyArrangedGraphs_.end()) {
        return known->second;
    }
    const auto isBuilt = [&](DraftGraphBuilder *graph) {
        const auto state = bodyStates_.find(graph);
        return state != bodyStates_.end() && state->second == BodyState::Built;
    };
    // A body that is not finished holds code this walk cannot see yet, so nothing is claimed about
    // it (this is the recursive call, seen from inside the body it calls). A body the walk reaches
    // a second time is one that reaches itself: that is a recursion, which may go deeper than the
    // depth limit, so it is a call that may fault (design principles 3.4). Neither answer is
    // memoized -- the first is about a body that is not finished, and the second is about this walk
    // and not about the graph it names.
    if (!isBuilt(callee) || !freelyArrangedActive_.insert(callee).second) {
        return false;
    }
    const bool free = bodyHoldsNoChainStatement(callee);
    freelyArrangedActive_.erase(callee);
    freelyArrangedGraphs_[callee] = free;
    return free;
}

bool Builder::bodyHoldsNoChainStatement(DraftGraphBuilder *graph) {
    const auto &draft = graph->transaction().graph();
    for (draft_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        if (!draft.alive(id)) {
            continue;
        }
        const node_handle_t node = draft.ref(id);
        switch (nodeKindOf(node)) {
        case runtime::GCNodeKind::Func: {
            // A direct call: the body it names is what this call runs, so the walk follows it.
            const compile_graph_ptr_t callee = graph->funcTarget(node);
            if (callee == nullptr || !calleeIsFreelyArranged(callee.get())) {
                return false;
            }
            break;
        }
        case runtime::GCNodeKind::Call:
            // A call through a function value: whose body it runs is not known here, and nothing
            // says that body cannot fault or reach a resource.
            return false;
        case runtime::GCNodeKind::Loop:
            // A loop may run longer than the depth limit and may fault (camel/runtime/graph.cpp,
            // `effectSummaryOf`): nothing says it does not, so a body holding one is not arranged
            // freely. Its parts are nodes of this same draft and are walked as they come.
            return false;
        case runtime::GCNodeKind::Brch:
        case runtime::GCNodeKind::Join:
            // A branch as a value: choosing an arm is nothing to keep. What the arms hold is nodes
            // of this draft, and they are walked as they come.
            break;
        case runtime::GCNodeKind::Gate:
            // A GATE computes nothing and cannot fault. Where one stands, the block that wrote it
            // had a statement on its chain, and that statement is a node of this draft.
            break;
        case runtime::GCNodeKind::Data:
        case runtime::GCNodeKind::Port:
        case runtime::GCNodeKind::Copy:
        case runtime::GCNodeKind::Fill:
        case runtime::GCNodeKind::Accs:
            // A constant, a port, a copy, a closure value, a field read: computed from what it
            // reads, with nothing to fail and nothing to order.
            break;
        default:
            // An operator or a conversion, asked exactly as the chain asks it: an effect, a
            // computation that may fault, or (for a cast) a value check that cannot be moved.
            if (runtime::isChainStatement(draft, id)) {
                return false;
            }
            break;
        }
    }
    return true;
}

bool Builder::writesForRegionOf(DraftGraphBuilder *graph, node_handle_t node) const {
    const runtime::gc_node_ref_t id = nodeIdOf(node);
    if (const auto known = regionWrites_.find(id); known != regionWrites_.end()) {
        return known->second;
    }
    // The answer is the node's *effect as it is declared or inferred*, never the operator's name
    // (an operator, a call, a branch and a loop alike), and it is
    // asked of the node's own kind, because the three kinds answer it from different places. Read
    // once per node: the tables do not change while a body is laid out.
    const auto answer = [&](bool writes) {
        regionWrites_.emplace(id, writes);
        return writes;
    };
    switch (nodeKindOf(node)) {
    case runtime::GCNodeKind::Oper: {
        // The operator's declaration, which is the effect inference for an operator: what its
        // access list itemizes, and the conservative reading for one that declares an effect and
        // itemizes nothing (`OperatorSemantics::writesForTheRegion`, and the same call the
        // withdrawn region roots made -- reading it from the *body's promise* instead would answer
        // the wrong question: `footprintOf(graph, id)` places an access only when the written value
        // is one of the body's own ports, so a write to a box the body computed would be missed,
        // and `set` on an array element is exactly that).
        const auto &op = graph->operTarget(node);
        return answer(
            op != nullptr && camel::core::OperatorTable::instance().writesForTheRegion(op->uri()));
    }
    case runtime::GCNodeKind::Func:
    case runtime::GCNodeKind::Call: {
        // A call is answered by the **signature of the function it calls** -- the clause it wrote,
        // or, when it wrote none, the set its body was measured to reach (design principles 3.3;
        // the same reading `reachOfFunc` takes).
        // The *body's footprint* would answer a different question: `footprintOf`
        // places an access only when the written value is one of this body's own ports, so a call
        // that writes a box this body computed reads as writing nothing -- `state::set(c, 7)` on a
        // `let c = state::ref(0)` is exactly that (`test/cases/compile/ref/refwrite_shapes.cml`).
        const runtime::GraphTransaction &transaction = graph->transaction();
        const auto &draft                            = transaction.graph();
        compile_graph_ptr_t callee;
        if (graph->hasFuncTarget(draft.ref(id))) {
            callee = graph->funcTarget(draft.ref(id));
        }
        if (callee != nullptr && callee->funcType() != nullptr) {
            const auto &declared = callee->funcType()->resources();
            if (declared.mode == camel::core::sem::ResourceSignature::Mode::Concrete) {
                return answer(camel::core::sem::writesForTheRegion(declared));
            }
            // A signature that is not concrete -- no clause at all, or one a type parameter leaves
            // open -- is read conservatively, and *only* from the declaration: the set the callee's
            // body was measured to reach (`inferredResources_`) is the link step's answer and is
            // not there yet for a module that has not been built, so reading it here made the same
            // program lay different graphs depending on the order the modules loaded
            // (`perf_fib_smoke`: the summary line `...->format<...>->println` printed or did not,
            // and the 20 rounds and the summary were 21 lines in one run and 20 in the next).
            // Design principles 3.3 puts the conservative reading first, and it is deterministic.
            return answer(true);
        }
        const auto reach = footprintOf(graph, id);
        // A call whose callee this builder never registered is a node a **rewrite** made; the
        // footprint of the body it stands in cannot place its accesses (they are the callee's), so
        // the conservative reading is taken rather than "writes nothing" (design principles 3.3:
        // an operator nobody declared is read conservatively). A module's body-less kernel is this
        // shape, and `state::set` on a box is a write of a handle -- a write of the region it is
        // written in (`test/cases/compile/ref/refwrite_shapes.cml`, line 53).
        return answer(true);
    }
    default:
        // A branch and a loop are writes of the region they are written in when the region *they
        // own* writes. That is not a fact about this node's own operands, so it is settled over the
        // region table once the whole body has been collected (`layRegionWriteEdges`), not here.
        return answer(false);
    }
}

void Builder::layRegionWriteEdges() {
    // Each region's exit holds one `GATE` (design principles 3.1): its first input is the value
    // the region hands out, and the rest are the nodes of the region that are inferred to write an
    // external resource or a handle (an operator, a call, a branch and a loop alike), whether or
    // not anyone reads their values; no edge joins the writes to one another.
    //
    // So what is laid here is a GATE, not a fan-in of sync edges: the exit receives the writes as
    // **normal inputs** and nothing joins the writes to one another. That is what makes the
    // promise keepable at all -- a node has one sync predecessor (`GraphDraft::setSyncInputs`
    // refuses two), so an exit cannot receive one edge per statement the region owes, while a GATE
    // holds as many inputs as it likes.
    //
    // Three rules, and they are the whole of what this lays:
    //
    //   * every region gets **one** GATE: its `norm` is the value the region hands out first and
    //     the region's writes after it, in that order;
    //   * the writes are *not* ordered among themselves -- no sync edge joins two of them, and no
    //     sync predecessor is invented. The order of two writes on one resource is what
    //     `orderEffectsOnOneResource` lays, by the order they are written in, and the order of two
    //     writes on two resources is nothing the graph has to keep(that is
    //     `std::gir` evidence: swapping a `println` with a write to a file leaves the graph the
    //     same);
    //   * nothing searches for another root afterwards. The graph holds no rule about regions and
    //     writes: a GATE's inputs are read by it like any other input, so the walks that ask what
    //     runs (`computeExecutionOrder`, `liveNodesOf`, `reportDroppedEffects`) reach the writes
    //     through ordinary edges and derive nothing (design principles 9.7).
    //
    // What the GATE is *for* is the half no edge could carry: the value a region hands out is read
    // by its owner (the JOIN's `with` input, the LOOP's), so a write hung on that value is live
    // exactly when the owner is -- and a write inside an arm or a loop body is now live because the
    // arm or the body hands its value out through a GATE that reads it.
    //
    // The exit is `GCRegion::result` for a region, and the graph's own `exit`/`output`/`return`
    // node for the body (region 0 has no result: `setRegionResult` takes only ids above 0). A
    // region whose GATE cannot be reached -- no result, no owner that reads it, no JOIN to wait on
    // it -- is left as it is rather than given a dead GATE: an unreachable GATE would be a write
    // carried by nothing, which is worse than the reading it replaces because it looks like a
    // carrier.
    //
    // `exit` first, and the three are not interchangeable: `output` and `exit` are the node the
    // graph's execution ends at, which is what a write is carried by, while `return` is the node a
    // *value* is read from -- for a body whose statements end in a value nobody returns, `return`
    // names a node of another type (`test/cases/undefined/chain_place_after_a_branch.cml`).
    regionWrites_.clear();
    for (const ModuleUnit *unit : unitOrder_) {
        for (const compile_graph_ptr_t &graph : unit->builtBodies) {
            regionWrites_.clear();
            DraftGraphBuilder *body = graph.get();
            auto &draft             = body->transaction().edit();
            const auto exitOf       = [&draft](uint32_t region) -> runtime::gc_node_ref_t {
                if (region == 0) {
                    for (runtime::gc_node_ref_t root :
                         {draft.exitNode(), draft.outputNode(), draft.returnNode()}) {
                        if (root != runtime::kInvalidNodeRef) {
                            return root;
                        }
                    }
                    return runtime::kInvalidNodeRef;
                }
                return region < draft.regionCount() ? draft.region(region).result
                                                    : runtime::kInvalidNodeRef;
            };
            // The exit is a node of the region it is the exit of. The body is region 0, and a node
            // of an arm, of a loop's parts or of a block is not in the body: only a node written
            // there is.
            //
            // The one shape that is not a node *of* its region is a region's own `result`: what an
            // arm hands out need not be built inside the arm. A constant the arm ends with is
            // placed in the region that encloses it (the arm's `result` names it all the same), and
            // the write rule is about the value the reader takes, not about where the value was
            // built -- `test/cases/compile/ref/refwrite_shapes.cml` writes into a box and then
            // hands out a `1`, and refusing that exit left the write carried by nothing.
            const auto exitInRegion = [&draft](uint32_t region, runtime::gc_node_ref_t exit) {
                if (exit == runtime::kInvalidNodeRef || !draft.alive(exit)) {
                    return false;
                }
                if (region != 0 && region < draft.regionCount() &&
                    draft.region(region).result == exit) {
                    return true;
                }
                const uint32_t at = draft.nodeRegion(exit);
                return region == 0 ? at == 0 : (at != 0 && draft.regionWithin(at, region));
            };
            // Whether `from` reaches `target` through the edges that carry "used" -- data, `with`,
            // control and sync: the reading is "`target` is upstream of `from`", because the walk
            // follows inputs.
            const auto reachesBack =
                [&draft](runtime::gc_node_ref_t from, runtime::gc_node_ref_t target) {
                    std::vector<runtime::gc_node_ref_t> work{from};
                    std::unordered_set<runtime::gc_node_ref_t> seen{from};
                    while (!work.empty()) {
                        const runtime::gc_node_ref_t at = work.back();
                        work.pop_back();
                        if (at == target) {
                            return true;
                        }
                        for (std::span<const runtime::gc_node_ref_t> inputs :
                             {draft.normInputsOf(at),
                              draft.withInputsOf(at),
                              draft.ctrlInputsOf(at),
                              draft.syncInputsOf(at)}) {
                            for (runtime::gc_node_ref_t input : inputs) {
                                if (seen.insert(input).second) {
                                    work.push_back(input);
                                }
                            }
                        }
                    }
                    return false;
                };
            // Whether the statement *writes* the value the region's exit names -- the box a region
            // hands out, where the statement is the write into it (the operator's declaration says
            // which operand of it, if any, the operator writes in place).
            //
            // It is the one shape the "the statement reads the exit" reading below must not leave
            // out. A write into the box a region hands out reads that box, so the reading sees the
            // statement and the exit as the same value and concludes the box carries the write --
            // which is false: the box carries its own value, the write changes what it holds, and
            // no edge says so. `test/cases/compile/ref/refwrite_shapes.cml`'s `r6` shape is that
            // body (`return c` after `state::set(c, 3)`): it answered 0, the value the box started
            // with, because the write was left on no chain and no GATE read it.
            const auto writesTheExit = [&draft, body](
                                           runtime::gc_node_ref_t statement,
                                           runtime::gc_node_ref_t exit) {
                if (nodeKindOf(draft.ref(statement)) != runtime::GCNodeKind::Oper) {
                    return false;
                }
                const auto &op = body->operTarget(draft.ref(statement));
                if (op == nullptr) {
                    return false;
                }
                const auto &table = camel::core::OperatorTable::instance();
                const auto oper   = table.lookup(op->uri());
                if (!oper.has_value()) {
                    return false;
                }
                for (const auto &[isWith, inputs] :
                     {std::pair{false, draft.normInputsOf(statement)},
                      std::pair{true, draft.withInputsOf(statement)}}) {
                    for (size_t index = 0; index < inputs.size(); ++index) {
                        if (inputs[index] == exit && table.writesOperand(*oper, isWith, index)) {
                            return true;
                        }
                    }
                }
                return false;
            };
            // Whether the value the region hands out is the one its reader takes. An arm is read
            // by the JOIN that closes its branch, and the JOIN takes the arm's `with` slot: a
            // branch whose arms write one `var` puts the value that write stored in that slot
            // instead of the arm's own result (`Builder::buildBranch`, the carried write), and then
            // the arm's exit -- the statement the arm ends with, or the GATE the block hands its
            // value out through -- is read by nothing. Everything below that says "it runs with the
            // exit" is then false: the exit does not run, so nothing behind it does.
            const auto readerTakesExit = [&draft](
                                             uint32_t region, runtime::gc_node_ref_t exit) {
                if (region == 0 || region >= draft.regionCount() ||
                    draft.region(region).kind != runtime::GCRegionKind::Arm) {
                    return true;
                }
                const runtime::gc_node_ref_t owner = runtime::decidingOwnerOfDraft(draft, region);
                if (owner == runtime::kInvalidNodeRef || !draft.alive(owner) ||
                    nodeKindOf(draft.ref(owner)) != runtime::GCNodeKind::Brch) {
                    return true;
                }
                const runtime::gc_node_ref_t join = runtime::branchJoinOf(draft, owner);
                if (join == runtime::kInvalidNodeRef || !draft.alive(join)) {
                    return true;
                }
                const auto slots    = draft.withInputsOf(join);
                const uint32_t slot = draft.region(region).index;
                if (slot >= slots.size() || !draft.alive(slots[slot])) {
                    return true;
                }
                return slots[slot] == exit;
            };
            // What a region owes, per region, in node order: every statement in it that writes,
            // whatever else the statement already waits on.
            //
            // Whether the region runs is *not* asked here (design principles 3.1):
            // the writes are the front end's promise, and the
            // GATE below is what makes them live. Reading
            // `liveNodesOf` here would make the promise conditional on a liveness walk that runs
            // before the GATE exists, and then a body whose writes are not (yet) live would collect
            // none of them and no GATE would ever be built.
            std::unordered_map<uint32_t, std::vector<runtime::gc_node_ref_t>> chains;
            for (draft_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
                if (!draft.alive(id)) {
                    continue;
                }
                const auto ref = static_cast<runtime::gc_node_ref_t>(id);
                // A branch written as a statement is a statement of the region it is written in,
                // and it joins that region's chain as the JOIN that closes it: the BRCH stands
                // outside its arms, so an edge from it would cross the branch's cut (the branch-arm
                // invariant refuses that), while the JOIN is a node of the region and runs once the
                // arm that is taken is done -- which is what orders the statements after the branch
                // behind it (`region_roots.branch_statement_kinds`: the box an arm writes must hold
                // its value by the time the next statement reads it).
                runtime::gc_node_ref_t statement = runtime::kInvalidNodeRef;
                if (nodeKindOf(draft.ref(id)) == runtime::GCNodeKind::Brch) {
                    // A branch that joins no value: the JOIN that closes it has no value type, so
                    // the branch is a statement of the region it is written in (design principles
                    // 3.1, 3.7). A branch whose JOIN joins a value nobody reads is not this shape
                    // (`region_roots.branch_value_dropped`), which is why the test is on the JOIN.
                    const runtime::gc_node_ref_t join = runtime::branchJoinOf(draft, ref);
                    if (join != runtime::kInvalidNodeRef && draft.alive(join) &&
                        isVoidGraphExitType(nodeTypeOf(draft.ref(join)))) {
                        statement = join;
                    }
                } else if (writesForRegionOf(body, draft.ref(id))) {
                    statement = ref;
                }
                if (statement == runtime::kInvalidNodeRef || !draft.alive(statement)) {
                    continue;
                }
                const uint32_t region = runtime::decidingRegionOfDraft(draft, statement);
                const runtime::gc_node_ref_t exit = exitOf(region);
                // A statement that only *reads* the exit runs whenever the exit does; when the
                // reader takes another value the exit runs for no one, so the statement is hung
                // here like any other.
                const bool exitIsRead =
                    exit != runtime::kInvalidNodeRef && readerTakesExit(region, exit);
                if (exit == runtime::kInvalidNodeRef || (exit == statement && exitIsRead)) {
                    continue; // a write that is what the region hands out already runs with it
                }
                if (!exitInRegion(region, exit)) {
                    continue; // the exit is not a node of this region: the seed answers this write
                }
                // A statement already on its region's chain is hung on the exit anyway. "It is
                // already carried, so it needs no second reason to run" is true of the write
                // **inside a region that runs** and false of the region itself: a `sync` block
                // whose value nobody takes runs only because something reads what it holds, and the
                // something is this GATE. Hanging every write, chained or not, is also what
                // the design asks for: the region's exit reads the region's writes
                // (`test/ cases/opt/e9/rc23_seed429_sync_arm.cml`: `q(5)` is the last statement of
                // the block `case _ => sync { q(5); return 0; }`, and with it hung on the exit the
                // block's own GATE is read, the arm runs and `q(5)` prints).
                // A statement that reads the exit is not hung a second time when its own value is
                // read: the exit is a value it reads, so it runs whenever its reader runs, and its
                // reader is behind the exit -- an edge from it would close a cycle the graph
                // refuses, and a second reason for it to run would say nothing new. That reading
                // holds only while the exit runs (`exitIsRead`) and only while something takes the
                // statement's value (`normUsersOf`): a statement nobody reads runs for no one, and
                // then the exit is not what carries it. `arm_read`'s printed form is that shape --
                // `{ let cml1 = t + 1; t = cml1; report('int', before, cml1); cml1 }`, where the
                // `report` call takes the value the arm hands out and nothing takes the call: the
                // call was left off the arm's exit and the printed program printed one line of the
                // two the origin printed.
                //
                // A write into the exit is the exception, and it is the whole reason this reading
                // is asked rather than assumed: the box a region hands out is a value the write
                // *changes*, not one it is behind, so the box carries nothing of it
                // (`writesTheExit` above, and `refwrite_shapes.cml`'s `r6`).
                if (exitIsRead && reachesBack(statement, exit) &&
                    !writesTheExit(statement, exit) &&
                    (!draft.normUsersOf(statement).empty() ||
                     !draft.withUsersOf(statement).empty())) {
                    continue;
                }
                chains[region].push_back(statement);
            }
            // A branch or a loop written in a region is one of that region's writes when the region
            // it owns writes (operators, calls, branches and loops alike).
            // The GATE inside the arm carries the arm's write but cannot make the arm run --
            // an arm runs because its owner runs -- so what makes it run is the owner being one of
            // the enclosing region's GATE's inputs. Walked to a fixpoint, because an arm may hold a
            // loop whose body writes, and then the loop's owner is a write of the arm.
            const auto markOwnerOf = [&draft, &chains](uint32_t region) {
                if (region == 0 || region >= draft.regionCount()) {
                    return false;
                }
                const runtime::gc_node_ref_t owner = runtime::decidingOwnerOfDraft(draft, region);
                if (owner == runtime::kInvalidNodeRef || !draft.alive(owner)) {
                    return false;
                }
                const runtime::DraftNodeHeader *header = draft.header(owner);
                if (header == nullptr) {
                    return false;
                }
                // The JOIN and not the BRCH, for the reason the collection above names: the BRCH
                // stands outside its arms and an edge that names it crosses the branch's cut, while
                // the JOIN is a node of the region the branch is written in and runs once the arm
                // that is taken is done.
                runtime::gc_node_ref_t statement = runtime::kInvalidNodeRef;
                if (header->kind == runtime::GCNodeKind::Brch) {
                    statement = runtime::branchJoinOf(draft, owner);
                } else if (header->kind == runtime::GCNodeKind::Loop) {
                    statement = owner;
                }
                if (statement == runtime::kInvalidNodeRef || !draft.alive(statement)) {
                    return false;
                }
                std::vector<runtime::gc_node_ref_t> &bucket =
                    chains[runtime::decidingRegionOfDraft(draft, statement)];
                if (std::ranges::find(bucket, statement) != bucket.end()) {
                    return false;
                }
                bucket.push_back(statement);
                return true;
            };
            for (bool grew = true; grew;) {
                grew = false;
                std::vector<uint32_t> owing;
                owing.reserve(chains.size());
                for (const auto &[region, writes] : chains) {
                    if (!writes.empty()) {
                        owing.push_back(region);
                    }
                }
                for (uint32_t region : owing) {
                    grew = markOwnerOf(region) || grew;
                }
            }
            // One GATE, one region: its `norm` is the value the region hands out first and the
            // region's writes after it, in that order, with no edge between the writes (the
            // author's "这些写之间不加边"). Nothing is invented as a sync predecessor; an order
            // that has to be kept is `orderEffectsOnOneResource`'s, laid by the order the
            // statements are written in.
            //
            // The GATE has to be reachable from the graph's exit, or it carries nothing: the
            // execution walk refuses a region whose result nothing the exit depends on reads, and a
            // write hanging off an unreachable GATE would be exactly the silent effect the promise
            // is about. So the value the region hands out is re-pointed at the GATE -- the owner's
            // input where the owner reads it (the JOIN's `with`, the LOOP's), and the JOIN's place
            // in its block's chain where it does not (a branch written as a statement joins no
            // value). A region whose GATE can be made reachable neither way is left as it was.
            const auto gateOf =
                [&](uint32_t region,
                    const std::vector<runtime::gc_node_ref_t> &writes) -> runtime::gc_node_ref_t {
                const runtime::gc_node_ref_t result = exitOf(region);
                if (result == runtime::kInvalidNodeRef || !draft.alive(result)) {
                    return runtime::kInvalidNodeRef;
                }
                // The value the region hands out is not always the node the region's own exit
                // names. A branch carries a `var` its arms write as the branch's value:
                // the arm `then sync { x = 3; x = 4; println(1); println(2) }` hands the JOIN the
                // value the write stored, while its own exit is the `sync` block's GATE over the
                // block's tail
                // -- the second `println`, whose type is `void`. The JOIN then reads the write's
                // value and never the arm's exit, so the arm's statements would be hung on a node
                // nothing reads (`test/cases/linear/runtime/order_edges_regions.cml`: the two
                // prints did not run and the graph lost them). What the region hands out is what
                // its reader takes from it -- for an arm, the JOIN's `with` slot of that arm -- and
                // the GATE below forwards that value with the region's writes beside it, which is
                // the very node the reader was reading.
                runtime::gc_node_ref_t owner  = runtime::kInvalidNodeRef;
                runtime::gc_node_ref_t join   = runtime::kInvalidNodeRef;
                runtime::gc_node_ref_t reader = runtime::kInvalidNodeRef;
                bool readerReadsGiven         = false;
                runtime::gc_node_ref_t given  = result;
                if (region != 0) {
                    owner = runtime::decidingOwnerOfDraft(draft, region);
                    if (owner == runtime::kInvalidNodeRef || !draft.alive(owner)) {
                        return runtime::kInvalidNodeRef;
                    }
                    if (const runtime::DraftNodeHeader *header = draft.header(owner);
                        header != nullptr && header->kind == runtime::GCNodeKind::Brch) {
                        join = runtime::branchJoinOf(draft, owner);
                        if (join != runtime::kInvalidNodeRef && !draft.alive(join)) {
                            join = runtime::kInvalidNodeRef;
                        }
                    }
                    reader           = join != runtime::kInvalidNodeRef ? join : owner;
                    const auto reads = [&draft](
                                           std::span<const runtime::gc_node_ref_t>
                                               inputs,
                                           runtime::gc_node_ref_t target) {
                        return std::ranges::find(inputs, target) != inputs.end();
                    };
                    readerReadsGiven = reads(draft.normInputsOf(reader), given) ||
                                       reads(draft.withInputsOf(reader), given);
                    if (!readerReadsGiven && join != runtime::kInvalidNodeRef &&
                        draft.region(region).kind == runtime::GCRegionKind::Arm) {
                        // The arm's own slot: the arm reads nothing here, the JOIN does.
                        const uint32_t index = draft.region(region).index;
                        const auto slots     = draft.withInputsOf(join);
                        if (index < slots.size() && slots[index] != runtime::kInvalidNodeRef &&
                            draft.alive(slots[index]) &&
                            nodeTypeOf(draft.ref(slots[index])) != nullptr) {
                            given            = slots[index];
                            readerReadsGiven = true; // the slot is the reader's own input
                        }
                    }
                }
                Type *type = nodeTypeOf(draft.ref(given));
                if (type == nullptr) {
                    return runtime::kInvalidNodeRef;
                }
                // The value first, then the writes. A write the value already reaches is upstream
                // of the exit, so it runs whenever the exit does: adding it back would only draw a
                // second edge to what is already behind the value.
                std::vector<runtime::gc_node_ref_t> norm;
                norm.push_back(given);
                for (runtime::gc_node_ref_t write : writes) {
                    if (!draft.alive(write) || std::ranges::find(norm, write) != norm.end()) {
                        continue;
                    }
                    if (reachesBack(given, write)) {
                        continue;
                    }
                    norm.push_back(write);
                }
                if (norm.size() < 2) {
                    return runtime::kInvalidNodeRef; // nothing for the GATE to carry
                }
                if (region == 0) {
                    // The body has no region result (`setRegionResult` takes only ids above 0): its
                    // exit is the graph's own field, and every field naming the value the body
                    // hands on moves to the GATE. The GATE forwards that value as its first input,
                    // so the graph still returns the same value -- what changes is that the writes
                    // are read by the node that returns it. A field that named another node stays
                    // as it is: `exit`, `output` and `return` are three answers to three questions,
                    // and the draft is the place that wrote them.
                    const runtime::gc_node_ref_t gate = draft.addGateNode(type);
                    draft.setNodeRegion(gate, 0);
                    draft.setNormInputs(gate, norm);
                    const runtime::GCReturnKind kind = draft.returnKind();
                    if (draft.exitNode() == result) {
                        draft.setExitNode(gate);
                    }
                    if (draft.outputNode() == result) {
                        draft.setOutputNode(gate);
                    }
                    if (draft.returnNode() == result) {
                        draft.setReturnNode(gate, kind);
                    }
                    return gate;
                }
                // Who reads what the region hands out. For an arm, the region's `owner` is the BRCH
                // -- the node whose running is the arm's running -- while the value the arm hands
                // out is read by the JOIN the branch closes, as that JOIN's `with` input: an arm's
                // value is nobody's norm input and the BRCH's inputs are the condition. A GATE hung
                // on the BRCH would therefore be read by nothing, and the arm's writes would be
                // laid on a carrier that never runs; the reader asked here is the JOIN, which is
                // the "the JOIN's `with`" this function's own rule names. The region's value is
                // re-pointed at the GATE in that input, so the arm runs -- and its writes run --
                // exactly when the value it hands out is read.
                //
                // The owner's inputs are read before the GATE is made: the spans look into the
                // draft and a new node may move what they point at.
                std::vector<runtime::gc_node_ref_t> readerNorm(
                    draft.normInputsOf(reader).begin(),
                    draft.normInputsOf(reader).end());
                std::vector<runtime::gc_node_ref_t> readerWith(
                    draft.withInputsOf(reader).begin(),
                    draft.withInputsOf(reader).end());
                std::vector<runtime::gc_node_ref_t> joinSync;
                if (!readerReadsGiven) {
                    if (join == runtime::kInvalidNodeRef) {
                        return runtime::kInvalidNodeRef; // nothing would read the GATE
                    }
                    joinSync.assign(
                        draft.syncInputsOf(join).begin(),
                        draft.syncInputsOf(join).end());
                }
                const runtime::gc_node_ref_t gate = draft.addGateNode(type);
                draft.setNodeRegion(gate, region);
                draft.setNormInputs(gate, norm);
                if (readerReadsGiven) {
                    for (runtime::gc_node_ref_t &input : readerNorm) {
                        if (input == given) {
                            input = gate;
                        }
                    }
                    for (runtime::gc_node_ref_t &input : readerWith) {
                        if (input == given) {
                            input = gate;
                        }
                    }
                    draft.setNormInputs(reader, readerNorm);
                    draft.setWithInputs(reader, readerWith);
                } else {
                    // Nothing reads what the region hands out, so there is no data input to
                    // re-point and the GATE takes the JOIN's place in the block's chain instead:
                    // the JOIN's old sync predecessor becomes the GATE's, and the JOIN waits on the
                    // GATE. That is one order edge on a JOIN that joins no value, which carries no
                    // type and is legal there. This is the `match` arm of
                    // `test/cases/opt/e9/rc23_seed429_sync_arm.cml`, whose JOIN joins no value at
                    // all.
                    //
                    // Legal only when the JOIN is in the GATE's own region: a sync edge that joins
                    // two blocks is what the commit refuses ("sync edge 28 -> 5 joins two blocks:
                    // 28 is in the block of region 2 and 5 in region 0"). A region whose exit
                    // nothing of its own reads is then left as it is, like any other unreachable
                    // GATE
                    // (`reachesBack`, above), rather than given a carrier that crosses a block cut.
                    if (draft.nodeRegion(join) != region) {
                        return runtime::kInvalidNodeRef;
                    }
                    if (!joinSync.empty()) {
                        draft.setSyncInputs(gate, joinSync);
                    }
                    const std::span<const runtime::gc_node_ref_t> one{&gate, 1};
                    draft.setSyncInputs(join, one);
                }
                draft.setRegionResult(region, gate);
                return gate;
            };
            // Ascending, so that the GATEs -- and the ids a dump prints -- are laid the same way
            // every time the same program is built.
            std::vector<uint32_t> owing;
            owing.reserve(chains.size());
            for (const auto &[region, writes] : chains) {
                if (!writes.empty()) {
                    owing.push_back(region);
                }
            }
            std::sort(owing.begin(), owing.end());
            for (uint32_t region : owing) {
                gateOf(region, chains.at(region));
            }
        }
    }
    regionWrites_.clear();
}

void Builder::settleEffectfulGraphs() {
    // Whether a call is an effect depends on its callee's body, which may be written later,
    // in another module, or be the caller itself: the set of effectful graphs is the least
    // fixpoint over the bodies of the whole program (a part of the link step), and only then
    // are the warnings issued, per module, in the order its bodies were built.
    for (size_t known = effectfulGraphs_.size() + 1; known != effectfulGraphs_.size();) {
        known = effectfulGraphs_.size();
        for (const ModuleUnit *unit : unitOrder_) {
            for (const compile_graph_ptr_t &graph : unit->builtBodies) {
                reportDroppedEffects(graph, false);
            }
        }
    }
}

void Builder::reportResourceViolation(
    const compile_graph_ptr_t &graph, const camel::core::sem::ResourceSignature &promised,
    const camel::core::sem::ResourceSignature &measured, camel::source::origin_id_t noteOrigin) {
    // What the clause does not promise, said in the terms the clause is written in: the caller of
    // this function can see only the clause, so the report names the resources the body reaches
    // that the clause leaves out.
    std::vector<std::string> unnamed;
    // A promise that names `any` at its target has already said it may reach anything: nothing the
    // body reaches is left out of it.
    const bool anyPromised =
        std::any_of(promised.anyOf.begin(), promised.anyOf.end(), [](auto cls) {
            return cls == sem::ResourceClass::Unknown;
        });
    for (const auto &use : measured.ambient) {
        const bool promisedUse =
            std::any_of(promised.ambient.begin(), promised.ambient.end(), [&](const auto &other) {
                return other.name == use.name;
            });
        if (!promisedUse && !anyPromised) {
            unnamed.push_back(sem::resourceDisplayName(use.name));
        }
    }
    for ([[maybe_unused]] uint8_t capture : measured.captures) {
        if (!anyPromised) {
            // A capture is a variable of the function around this one: it lives longer than this
            // call, so writing it is an effect of this one too (design principles 3.3).
            unnamed.push_back("a variable it captured from the function around it");
        }
    }
    for (sem::ResourceClass cls : measured.anyOf) {
        if (!anyPromised) {
            // A class the clause has no name for, or a cell of this function's own making that
            // outlives the call because a closure holding it was handed out.
            unnamed.push_back(std::format("any {}", sem::resourceClassName(cls)));
        }
    }
    for (const auto &use : measured.params) {
        const bool promisedUse =
            std::any_of(promised.params.begin(), promised.params.end(), [&](const auto &other) {
                return other.index == use.index;
            });
        if (!promisedUse && !use.name.empty()) {
            unnamed.push_back(use.name);
        }
    }
    if (measured.external && !promised.external) {
        unnamed.push_back("an effect outside the program");
    }
    // One builder, mutated in place and committed once: `of` hands out a value, and assigning a
    // builder from the chain of another would copy state that is not meant to be copied.
    DiagnosticBuilder builder = diags_->of(SemanticDiag::UsesPromiseViolated);
    if (const auto sourceContext = context_ ? context_->sourceContext() : nullptr) {
        const auto origin = sourceContext->debugMap().graphOrigin(graph->graphId());
        if (origin != camel::source::kInvalidOriginId) {
            builder.atOrigin(origin);
        }
    }
    if (noteOrigin != 0) {
        builder.relatedOrigin(
            noteOrigin,
            "the body reaches past the promise from here on: this is the first use the signature "
            "does not name");
    }
    std::sort(unnamed.begin(), unnamed.end());
    unnamed.erase(std::unique(unnamed.begin(), unnamed.end()), unnamed.end());
    builder.commit(
        graph->funcType()->toString(),
        unnamed.empty()
            ? std::string("an effect outside the program")
            : strutil::join(unnamed, ", ", [](const std::string &name) { return name; }));
}

void Builder::reportSpellingNotDeclared(const compile_graph_ptr_t &graph) {
    // The capability half of the same promise the resource check makes (design principles 9.5).
    // Anchored at the declaration, like the resource report, and with a note at the first node
    // whose own reach reads the spelling: that is the place the writer has to look at.
    camel::source::origin_id_t firstRead = 0;
    for (draft_node_ref_t id = 0; id < graph->view().nodeSlotCount(); ++id) {
        if (!graph->view().alive(id)) {
            continue;
        }
        const node_handle_t node = graph->view().ref(id);
        const auto reach         = footprintOf(graph.get(), id);
        if (!reach.has_value() || !reach->spelling) {
            continue;
        }
        firstRead = nodeDraftOf(node)->nodeOrigin(nodeIdOf(node));
        break;
    }
    DiagnosticBuilder builder = diags_->of(SemanticDiag::SpellingNotDeclared);
    const auto sourceContext  = context_ ? context_->sourceContext() : nullptr;
    if (sourceContext != nullptr) {
        const auto origin = sourceContext->debugMap().graphOrigin(graph->graphId());
        if (origin != camel::source::kInvalidOriginId) {
            builder.atOrigin(origin);
        }
    }
    if (firstRead != 0) {
        builder.relatedOrigin(
            firstRead,
            "the first reading of the spelling in this body: it is what the type would have to "
            "declare");
    }
    // No type text: by the time this runs, a body that wrote no clause has its settled set written
    // to its type, so quoting the type here would show the very `spelling` the message says is
    // missing. The position is the declaration and the note is the read.
    builder.commit();
}

void Builder::reportResourceConflict(
    const ResourceConflict &conflict, DraftGraphBuilder *leftOwner, DraftGraphBuilder *rightOwner) {
    // Anchored at the declaration that wrote no clause when the two met: the position cannot repair
    // the one that already says what it writes, and a clause is the repair this asks for. Both are
    // named, because what the position needs is for the two to agree.
    DraftGraphBuilder *leftUnwritten  = conflict.leftUnwritten ? leftOwner : nullptr;
    DraftGraphBuilder *rightUnwritten = conflict.rightUnwritten ? rightOwner : nullptr;
    DraftGraphBuilder *anchor         = leftUnwritten != nullptr ? leftUnwritten : rightUnwritten;
    if (anchor == nullptr) {
        anchor = leftOwner != nullptr ? leftOwner : rightOwner;
    }
    if (anchor == nullptr) {
        return;
    }
    const auto nameOf = [](DraftGraphBuilder *graph, const camel::core::type::Type *type) {
        return graph != nullptr ? graph->name() : camel::core::type::typeToString(type);
    };
    DiagnosticBuilder builder = diags_->of(SemanticDiag::ResourceInferenceConflict);
    const auto sourceContext  = context_ ? context_->sourceContext() : nullptr;
    const auto originOf       = [&](DraftGraphBuilder *graph) {
        return sourceContext != nullptr && graph != nullptr
                   ? sourceContext->debugMap().graphOrigin(graph->graphId())
                   : camel::source::kInvalidOriginId;
    };
    const auto origin = originOf(anchor);
    if (origin != camel::source::kInvalidOriginId) {
        builder.atOrigin(origin);
    }
    DraftGraphBuilder *other = anchor == leftOwner ? rightOwner : leftOwner;
    const auto otherOrigin   = originOf(other);
    if (otherOrigin != camel::source::kInvalidOriginId) {
        builder.relatedOrigin(
            otherOrigin,
            "the other value this position is given: the two have to write the same resources");
    }
    builder.commit(
        nameOf(leftOwner, conflict.left),
        nameOf(rightOwner, conflict.right),
        camel::core::type::typeToString(conflict.left),
        camel::core::type::typeToString(conflict.right));
}

void Builder::reportDroppedEffectsOfBodies() {
    for (const compile_graph_ptr_t &graph : unit_->builtBodies) {
        reportDroppedEffects(graph, true);
    }
    unit_->builtBodies.clear();
    // Every body of this unit has been reported: what a `fail` left behind is answered, and the
    // entries of a graph nothing reports (a body of a library that was not built here) are not
    // this unit's to keep.
    statementsAfterFail_.clear();
}

void Builder::reportDroppedEffects(const compile_graph_ptr_t &graph, bool report) {
    auto &draft = graph->transaction().edit();
    // What the exit depends on, through data, control and sync edges.
    std::unordered_set<draft_node_ref_t> needed;
    std::vector<draft_node_ref_t> work;
    const auto reach = [&] {
        while (!work.empty()) {
            const draft_node_ref_t id = work.back();
            work.pop_back();
            for (auto inputs :
                 {draft.normInputsOf(id), draft.withInputsOf(id), draft.ctrlInputsOf(id)}) {
                for (draft_node_ref_t input : inputs) {
                    if (needed.insert(input).second) {
                        work.push_back(input);
                    }
                }
            }
            // A statement of a `sync` block is used by the one written after it (and the block's
            // value by its chain tail): liveness follows a sync edge as it follows a data input.
            for (draft_node_ref_t input : draft.syncInputsOf(id)) {
                if (needed.insert(input).second) {
                    work.push_back(input);
                }
            }
        }
    };
    for (draft_node_ref_t root : {draft.outputNode(), draft.exitNode(), draft.returnNode()}) {
        if (root != runtime::kInvalidNodeRef && needed.insert(root).second) {
            work.push_back(root);
        }
    }
    if (work.empty()) {
        return; // the body did not build (an error was reported)
    }
    reach();

    // Which graphs hold an effect that runs. `settleEffectfulGraphs` reads this, so it is asked
    // whether anything is reported here or not. A statement that does not run is not reported: the
    // rule of design principles 3.1 makes what a region writes a root of
    // that region, so a call, a loop and a block run with the region they are written in even when
    // nothing takes the value they give. What such a statement leaves behind is a value nobody
    // uses, and that is a value -- never an effect that failed to run.
    for (draft_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        if (!draft.alive(id) || !isEffectNode(graph.get(), draft.ref(id))) {
            continue;
        }
        if (needed.contains(id)) {
            effectfulGraphs_.insert(graph.get());
        }
    }
    if (!report) {
        return;
    }
    auto sourceContext = context_ ? context_->sourceContext() : nullptr;
    // A write to a variable whose value nothing reads (design principles 3.1 and 3.5:
    // assigning to a `var` is not an effect). What such a write drops is a value, and a
    // dropped value is what this says: the same thing one rule says about any value nobody uses
    // (UnusedPureResult, UnusedFallibleValue), and never a word about an effect. A write is not an
    // effect, and whether the statement it stands in runs is a fact about that statement, not about
    // this value (design principles 3.1 and 3.3: a write of a handle is
    // used by the region it is written into, by the same rule as an outside write).
    //
    // What makes such a write's value dead is that no read takes it, and the walk above does not
    // answer that: it answers whether a read goes *through the write's own node*. There are two
    // ways a read takes the value and leaves that node with no user of its own.
    //
    // The first is the fold (Builder::collapseNonEscapingVariableReads): a read of a `var` the
    // builder could keep out of a cell takes the value the write stores and not the write -- the
    // read is handed the write's second operand -- so the write's node ends up with no consumer
    // while the value it stored is read by everything after it. Asking the walk alone reported
    // exactly those writes (`t = t + 1` followed by a read of `t`, the ordinary way a counter is
    // written), which made the warning contradict its own text and `-Werror` refuse a legal
    // program.
    //
    // The second is a variable that kept its cell: there the read is a `:var/get` of the cell and
    // the edge runs from the write to that read, because the store's result is the cell's new
    // state. `needed` cannot answer this one either -- a `sync` body chains every statement into
    // the next one's CTRL and SYNC inputs, so the walk holds a store whose result nobody reads.
    //
    // So the value is read when any of these holds: the node computing the stored value has a user
    // other than the write; the write has a user that takes its result as a value; or the stored
    // value is a root the walk started from (the result of a body is a node and not an edge to one
    // -- `setGraphReturnFromResult` names `returnNode` as the value itself -- so a returned value
    // has no user to be found). A compound assignment (`+=`, `-=`, ...) computes the value it
    // stores itself, so it has no operand to ask about and the walk is its whole answer.
    const auto storedValueIsRead = [&](draft_node_ref_t writeId, GCT::AssignmentForm form) {
        if (form != GCT::AssignmentForm::Plain && form != GCT::AssignmentForm::Owned) {
            return false;
        }
        const auto operands = draft.normInputsOf(writeId);
        if (operands.size() < 2) {
            return false;
        }
        // The stored value itself, taken by anything that is not this write.
        for (auto users : {draft.normUsersOf(operands[1]), draft.withUsersOf(operands[1])}) {
            for (draft_node_ref_t user : users) {
                if (user != writeId && draft.alive(user)) {
                    return true;
                }
            }
        }
        // The write's result, taken as a value: the `:var/get` of a variable that kept its cell,
        // and every read standing after it. Another store of the same variable is not a read --
        // `t = 1; t = 2` hands the cell's state on and reads neither value -- so a store is
        // skipped.
        for (auto users : {draft.normUsersOf(writeId), draft.withUsersOf(writeId)}) {
            for (draft_node_ref_t user : users) {
                if (!draft.alive(user)) {
                    continue;
                }
                node_handle_t userNode = draft.ref(user);
                if (nodeKindOf(userNode) != runtime::GCNodeKind::Oper) {
                    return true;
                }
                if (!writeNodes_.contains(userNode)) {
                    return true;
                }
            }
        }
        // The stored value as a root of the walk, asked only of a write the walk does not hold: a
        // write it does hold hands its own operand into the set, and that says nothing about a
        // read.
        return !needed.contains(writeId) && needed.contains(operands[1]);
    };
    // Whether this graph is the whole story of the cell a write stores into: a cell of this graph,
    // not a `var` parameter (a `Port`, whose value the caller reads after the call: `func swap(var
    // x: int, v: int)` stores `v` for its caller and nothing in its own body reads it), and used by
    // no one but the writes of this body. Any other user -- a read of the cell, a closure that
    // captured it, a call it was handed to as a `var` argument -- is a way for the value to be read
    // that this graph cannot see, and then nothing here can say the value is read by nobody.
    const auto cellIsThisGraphsAlone = [&](draft_node_ref_t writeId) {
        const auto operands = draft.normInputsOf(writeId);
        if (operands.empty()) {
            return false;
        }
        const draft_node_ref_t cell = operands[0];
        if (!draft.isNamedBy(draft.ref(cell)) || !draft.alive(cell)) {
            return false;
        }
        if (nodeKindOf(draft.ref(cell)) == runtime::GCNodeKind::Port) {
            return false;
        }
        for (auto users :
             {draft.normUsersOf(cell), draft.withUsersOf(cell), draft.ctrlUsersOf(cell)}) {
            for (draft_node_ref_t user : users) {
                if (!draft.alive(user)) {
                    continue;
                }
                node_handle_t userNode = draft.ref(user);
                if (nodeKindOf(userNode) != runtime::GCNodeKind::Oper) {
                    return false;
                }
                if (!writeNodes_.contains(userNode)) {
                    return false;
                }
            }
        }
        return true;
    };
    for (draft_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        if (!draft.alive(id)) {
            continue;
        }
        if (graph == rootGraph_ && !(id < rootNodeOwners_.size() && rootNodeOwners_[id] == unit_)) {
            continue; // another module's top-level write, reported by that module's own pass
        }
        node_handle_t node = draft.ref(id);
        if (nodeKindOf(node) != runtime::GCNodeKind::Oper) {
            continue;
        }
        const auto target = graph->operTarget(node);
        if (target == nullptr) {
            continue;
        }
        // The stores this builder made: a node of another kind, an ordinary call, or a store of
        // another body is not one of them.
        const auto write = writeNodes_.find(node);
        if (write == writeNodes_.end()) {
            continue;
        }
        if (storedValueIsRead(id, write->second)) {
            continue;
        }
        // A write that does not run cannot have a reader: nothing it would store is ever there, so
        // the report stands wherever the cell lives. A write that runs is another matter. Then,
        // when its cell leaves this graph -- a `var` parameter a caller reads after the call, a
        // module-level `var` another body reads -- some read this graph cannot see may take the
        // value, and nothing here can say it is read by nobody. A `sync` block is where a write
        // whose value nobody reads still runs, because it is a statement of the chain; that is why
        // this is asked of the write that runs and not of the walk above, which holds it there.
        if (needed.contains(id) && !cellIsThisGraphsAlone(id)) {
            continue;
        }
        camel::source::origin_id_t origin = nodeDraftOf(node)->nodeOrigin(nodeIdOf(node));
        if (origin == camel::source::kInvalidOriginId && sourceContext) {
            origin = sourceContext->debugMap().graphOrigin(graph->graphId());
        }
        diags_->of(SemanticDiag::UnusedAssignedValue).atOrigin(origin).commit();
    }
    // A write to a `var` built as a value stores nothing: the
    // name is rebound to the value, so the value nobody reads is a value node with no user, and
    // that is the same fact the check above reads off a store that the graph no longer has. A
    // value the region goes on to hand on -- the body's result, a statement of a sync chain -- is
    // seeded into `needed` and is read; a value nothing takes at all is the dropped one. What is
    // reported, and where, is what the store's own report said: the assignment as written.
    for (const auto &[value, origin] : ssa_.writes()) {
        if (value == nullptr || nodeGraphOf(value) != graph.get() ||
            needed.contains(nodeIdOf(value))) {
            continue;
        }
        bool read = false;
        for (auto users :
             {draft.normUsersOf(nodeIdOf(value)), draft.withUsersOf(nodeIdOf(value))}) {
            for (draft_node_ref_t user : users) {
                if (draft.alive(user)) {
                    read = true;
                    break;
                }
            }
            if (read) {
                break;
            }
        }
        if (read) {
            continue;
        }
        diags_->of(SemanticDiag::UnusedAssignedValue).atOrigin(origin).commit();
    }
    // A `fail` written as a statement whose value nothing takes (design principles 3.9).
    // It gives no value and is no
    // effect of its own, so where its value is not wanted nothing runs it and the fault is never
    // raised -- which is what the writer has to be told, because the program was meant to stop
    // there. Inside a `sync` block the statement is on the chain and runs; on the path the data
    // takes its value is wanted. The warning is its own so that a `fail` is not read as a call
    // with an effect.
    for (draft_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        if (!draft.alive(id) || needed.contains(id)) {
            continue;
        }
        if (graph == rootGraph_ && !(id < rootNodeOwners_.size() && rootNodeOwners_[id] == unit_)) {
            continue; // another module's top-level `fail`, reported by that module's own pass
        }
        node_handle_t node = draft.ref(id);
        if (!isFailCall(node)) {
            continue;
        }
        camel::source::origin_id_t origin = nodeDraftOf(node)->nodeOrigin(nodeIdOf(node));
        if (origin == camel::source::kInvalidOriginId && sourceContext) {
            origin = sourceContext->debugMap().graphOrigin(graph->graphId());
        }
        diags_->of(SemanticDiag::FailNotExecuted)
            .atOrigin(origin)
            .commit("nothing needs it and it is not in a sync block");
    }
    // The statements a `fail` of this block leaves behind (design principles 3.9):
    // they are reached only if the `fail` is not run at all, which the walk
    // over `needed` above has just answered -- a `fail` nothing needs does not run, and then these
    // statements do run (FailNotExecuted tells the writer why). Where the `fail` is needed, it
    // raises its fault first and nothing after it is reached, whatever the statements are.
    for (const StatementsAfterFail &after : statementsAfterFail_) {
        if (after.fail == nullptr || after.statements.empty() || after.graph != graph) {
            continue;
        }
        if (!needed.contains(nodeIdOf(after.fail))) {
            continue;
        }
        for (camel::source::origin_id_t origin : after.statements) {
            diags_->of(SemanticDiag::StatementAfterFail).atOrigin(origin).commit();
        }
    }
}

std::vector<Builder::RuleCondition> Builder::ruleConditionsIn(
    const compile_graph_ptr_t &graph, std::unordered_set<const void *> &visited,
    bool ownedByUnit) const {
    std::vector<RuleCondition> conditions;
    if (graph == nullptr || !visited.insert(graph.get()).second) {
        return conditions;
    }
    auto sourceContext = context_ ? context_->sourceContext() : nullptr;
    const auto &draft  = graph->transaction().graph();
    for (draft_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        if (!draft.alive(id)) {
            continue;
        }
        if (ownedByUnit && id < rootNodeOwners_.size() && rootNodeOwners_[id] != unit_) {
            // Another module's top-level value: its own pass over the root graph reports it.
            continue;
        }
        const node_handle_t node = draft.ref(id);
        if (nodeIsKind(node, runtime::GCNodeKind::Func)) {
            // A rule written inside a function: its maker call is in that function's graph.
            std::vector<RuleCondition> inner =
                ruleConditionsIn(graph->funcTarget(node), visited, false);
            for (RuleCondition &found : inner) {
                conditions.push_back(std::move(found));
            }
            continue;
        }
        if (!nodeIsKind(node, runtime::GCNodeKind::Oper)) {
            continue;
        }
        const auto &op = graph->operTarget(node);
        if (op == nullptr) {
            continue;
        }
        const bool conditional = op->uri() == "rewrite:rule_when";
        // `__rule_generic(name, two_way, needs, consts, <the id of its templates>)`: a rule whose
        // head declares type parameters has no condition function to look at here, because what
        // its sides are is decided per type tuple. Its template's body is scanned instead, and the
        // instance it was asked for is checked where it is made.
        const bool generic = op->uri() == "rewrite:rule_generic";
        if (!conditional && !generic) {
            continue;
        }
        // Where the rule is written: the maker call is the declaration (`rule ...`), so its
        // position is the one an editor jumps to from the effect.
        camel::source::origin_id_t where = nodeDraftOf(node)->nodeOrigin(nodeIdOf(node));
        if (where == camel::source::kInvalidOriginId && sourceContext) {
            where = sourceContext->debugMap().graphOrigin(graph->graphId());
        }
        if (generic) {
            const auto arguments = draft.normInputsOf(id);
            const std::optional<slot_t> id =
                arguments.empty() ? std::nullopt : staticValueOf(draft.ref(arguments.back()));
            const GenericRuleEntry *entry =
                id.has_value() && context_ != nullptr
                    ? genericRuleEntry(*context_, static_cast<uint64_t>(*id))
                    : nullptr;
            if (entry != nullptr) {
                conditions.push_back(RuleCondition{nullptr, where, entry});
            }
            continue;
        }
        // The condition is the argument that is one of the rule's functions: the front end names
        // it `<rule>__when`, and the other two are `<rule>__lhs` and `<rule>__rhs`. A rule value
        // is a constant, so its sides are static function values (a direct call of the side, or
        // the value the call passes).
        const auto conditionOf = [&](node_handle_t argument) -> compile_graph_ptr_t {
            Type *type = nodeTypeOf(argument);
            if (type == nullptr || type->code() != TypeCode::Function) {
                return nullptr;
            }
            if (nodeIsKind(argument, runtime::GCNodeKind::Fill)) {
                // A function value whose body captured values: the node fills the captures into
                // the value it holds, which is its first ordinary input (`gc.fill`).
                const auto held = normInputsOf(argument);
                if (held.empty()) {
                    return nullptr;
                }
                argument = nodeDraftOf(argument)->ref(held.front());
                type     = nodeTypeOf(argument);
                if (type == nullptr || type->code() != TypeCode::Function) {
                    return nullptr;
                }
            }
            if (nodeIsKind(argument, runtime::GCNodeKind::Func)) {
                return graph->funcTarget(argument);
            }
            const std::optional<slot_t> value = staticValueOf(argument);
            if (!value.has_value()) {
                return nullptr;
            }
            // A transform's value names a graph of its own program, not one of this program's
            // (StaticProgramFunction, static_function.h).
            auto *held = camel::core::rtdata::fromSlot<camel::core::rtdata::Object *>(*value);
            if (!isStaticFunction(held)) {
                return nullptr;
            }
            auto *function = camel::core::rtdata::fromSlot<StaticFunction *>(*value);
            return function != nullptr ? function->graph() : nullptr;
        };
        for (draft_node_ref_t ref : draft.normInputsOf(id)) {
            const compile_graph_ptr_t condition = conditionOf(draft.ref(ref));
            if (condition == nullptr || !std::string_view(condition->name()).ends_with("__when")) {
                continue;
            }
            conditions.push_back(RuleCondition{condition, where, nullptr});
        }
    }
    return conditions;
}

std::string Builder::describeEffect(
    const compile_graph_ptr_t &inside, node_handle_t effect, bool insideACall) const {
    std::string subject;
    switch (nodeKindOf(effect)) {
    case runtime::GCNodeKind::Oper:
        subject = std::format("The call of '{}'", inside->operTarget(effect)->name());
        break;
    case runtime::GCNodeKind::Func:
        subject = std::format("The call of '{}'", inside->funcTarget(effect)->name());
        break;
    case runtime::GCNodeKind::Join:
        subject = "The branch";
        break;
    case runtime::GCNodeKind::Loop:
        subject = "The loop";
        break;
    default:
        subject = "The effect";
        break;
    }
    if (insideACall) {
        // Inside a function the condition calls: the position of the effect is in that
        // function, and the report says which one so that the condition is still found.
        subject = std::format("{} in the function '{}'", subject, inside->name());
    }
    return subject;
}

bool Builder::calleeIsGeneric(const std::string &name) const {
    return !name.empty() && templateScope_ != nullptr && templateScope_->get(name).has_value();
}

bool Builder::operatorGroupHasEffect(const std::string &name) const {
    const auto &table = camel::core::OperatorTable::instance();
    for (const std::string &uri : table.groupOperators(name)) {
        if (table.hasExternalEffect(uri)) {
            return true;
        }
    }
    return false;
}

Builder::TemplateEffect Builder::effectOfConditionTemplate(const GenericRuleEntry &entry) {
    TemplateEffect effect;
    const auto tmpl = std::static_pointer_cast<GenericTemplate>(entry.when);
    if (tmpl == nullptr || tmpl->func == nullptr || tmpl->func->size() < 2) {
        return effect;
    }
    // The condition of a rule whose head declares type parameters is a template: its body is
    // built once per type tuple, and where the rule is written only the tree the declaration
    // wrote exists. Reading that tree is what finds the effects the rule itself writes: a call of
    // a function that is not generic is followed into the graph of that function (the same
    // question `effectOfGraph` asks of a built body), and a call of a generic function is left to
    // the instance, where the type arguments decide which candidate it takes.
    std::unordered_set<const void *> visited;
    std::function<bool(const GCT::node_ptr_t &)> walk = [&](const GCT::node_ptr_t &node) {
        if (node == nullptr) {
            return false;
        }
        if (node->type() == GCT::NodeKind::LINK) {
            const std::string callee = writtenCallName(node);
            if (!callee.empty() && !calleeIsGeneric(callee)) {
                if (operatorGroupHasEffect(callee)) {
                    effect.found   = true;
                    effect.subject = std::format("The call of '{}'", callee);
                    effect.origin  = node->origin();
                    return true;
                }
                if (const auto graphs = graphsAt(callee); graphs.has_value()) {
                    for (const compile_graph_ptr_t &graph : *graphs.value()) {
                        compile_graph_ptr_t inside;
                        const node_handle_t found = effectOfGraph(graph, visited, inside);
                        if (found == nullptr) {
                            continue;
                        }
                        effect.found   = true;
                        effect.subject = describeEffect(inside, found, true);
                        effect.origin  = nodeDraftOf(found)->nodeOrigin(nodeIdOf(found));
                        if (effect.origin == camel::source::kInvalidOriginId) {
                            effect.origin = node->origin();
                        }
                        return true;
                    }
                }
            }
        }
        for (size_t i = 0; i < node->size(); ++i) {
            if (walk(node->at(i))) {
                return true;
            }
        }
        return false;
    };
    walk(tmpl->func->at(1));
    return effect;
}

node_handle_t Builder::effectOfGraph(
    const compile_graph_ptr_t &graph, std::unordered_set<const void *> &visited,
    compile_graph_ptr_t &inside) const {
    if (graph == nullptr || !visited.insert(graph.get()).second) {
        return nullptr;
    }
    const auto &draft = graph->transaction().graph();
    // The calls of the graph come first: what the graph itself does is a closer answer than what
    // a function it calls does.
    std::vector<compile_graph_ptr_t> calls;
    for (draft_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        if (!draft.alive(id)) {
            continue;
        }
        const node_handle_t node = draft.ref(id);
        if (nodeIsKind(node, runtime::GCNodeKind::Func)) {
            if (const compile_graph_ptr_t callee = graph->funcTarget(node); callee != nullptr) {
                calls.push_back(callee);
            }
            continue;
        }
        if (isEffectNode(graph.get(), node)) {
            inside = graph;
            return node;
        }
    }
    for (const compile_graph_ptr_t &callee : calls) {
        if (const node_handle_t node = effectOfGraph(callee, visited, inside); node != nullptr) {
            return node;
        }
    }
    return nullptr;
}

void Builder::reportEffectfulConditions() {
    // A rule of a module's top level is a value of the root graph, and one written inside a
    // function is in that function's graph; the bodies of the module are still here (the dropped
    // effects of the same step clear them). The root graph holds the top levels of every module,
    // so a rule reported from it is the one `unit_` wrote (`rootNodeOwners_`).
    std::vector<compile_graph_ptr_t> roots = unit_->builtBodies;
    roots.push_back(rootGraph_);
    auto sourceContext = context_ ? context_->sourceContext() : nullptr;
    std::unordered_set<const void *> searched;
    // The name the rule is written with: a condition is `<rule>__when`, possibly qualified by the
    // module it is written in.
    const auto ruleNameOf = [](const std::string &conditionName) {
        std::string rule = conditionName;
        if (const size_t scope = rule.rfind("::"); scope != std::string::npos) {
            rule = rule.substr(scope + 2);
        }
        if (const size_t side = rule.rfind("__when"); side != std::string::npos) {
            rule.resize(side);
        }
        return rule;
    };
    const auto report = [&](const std::string &subject,
                            const std::string &rule,
                            camel::source::origin_id_t origin,
                            camel::source::origin_id_t where) {
        auto diagnostic = diags_->of(SemanticDiag::EffectInRuleCondition).atOrigin(origin);
        if (where != camel::source::kInvalidOriginId) {
            // Where the rule is written: the effect is what the report is about, and the rule is
            // what makes it one, so an editor offers both places.
            diagnostic.relatedOrigin(where, std::format("the rule '{}' is declared here", rule));
        }
        diagnostic.commit(subject, rule);
    };
    for (const compile_graph_ptr_t &graph : roots) {
        std::vector<RuleCondition> conditions =
            ruleConditionsIn(graph, searched, graph == rootGraph_);
        for (const RuleCondition &condition : conditions) {
            if (condition.entry != nullptr) {
                // A rule whose head declares type parameters: what its condition writes where the
                // rule is written is read from its template. What only the type arguments reveal
                // is asked of the instance (
                // `reportEffectInRuleInstance`).
                const GenericRuleEntry &entry = *condition.entry;
                if (templateConditionsReported_.contains(entry.when.get())) {
                    continue;
                }
                const TemplateEffect effect = effectOfConditionTemplate(entry);
                if (!effect.found) {
                    continue;
                }
                templateConditionsReported_.insert(entry.when.get());
                report(effect.subject, entry.name, effect.origin, condition.where);
                continue;
            }
            const compile_graph_ptr_t &conditionGraph = condition.graph;
            if (conditionGraph == nullptr ||
                !conditionsReported_.insert(conditionGraph.get()).second) {
                continue;
            }
            std::unordered_set<const void *> walked;
            compile_graph_ptr_t inside;
            const node_handle_t effect = effectOfGraph(conditionGraph, walked, inside);
            if (effect == nullptr) {
                continue;
            }
            camel::source::origin_id_t origin = nodeDraftOf(effect)->nodeOrigin(nodeIdOf(effect));
            if (origin == camel::source::kInvalidOriginId && sourceContext) {
                origin = sourceContext->debugMap().graphOrigin(inside->graphId());
            }
            report(
                describeEffect(inside, effect, inside != conditionGraph),
                ruleNameOf(conditionGraph->name()),
                origin,
                condition.where);
        }
    }
}

void Builder::reportEffectInRuleInstance(
    const GenericRuleEntry &entry, const camel::core::module::module_ptr_t &module,
    const compile_graph_ptr_t &condition) {
    if (condition == nullptr) {
        return;
    }
    // A rule whose condition writes an effect the type arguments revealed: which candidate a call
    // takes is decided by the tuple, so only the instance can see it. The report is about the
    // rule, not about the attempt at one tuple: it goes to the sink the user reads, whatever a
    // speculative scope is holding, and it is made once for the rule (`why_not` and the engine
    // keep reading the attempt as one that did not come about).
    if (templateConditionsReported_.contains(entry.when.get())) {
        return;
    }
    std::unordered_set<const void *> walked;
    compile_graph_ptr_t inside;
    const node_handle_t effect = effectOfGraph(condition, walked, inside);
    if (effect == nullptr) {
        return;
    }
    auto sourceContext                = context_ ? context_->sourceContext() : nullptr;
    camel::source::origin_id_t origin = nodeDraftOf(effect)->nodeOrigin(nodeIdOf(effect));
    if (origin == camel::source::kInvalidOriginId && sourceContext) {
        origin = sourceContext->debugMap().graphOrigin(inside->graphId());
    }
    templateConditionsReported_.insert(entry.when.get());
    camel::core::error::Diagnostics *sink = userSink();
    if (sink == nullptr) {
        // Nothing holds reports for this declaration: a rule is written in a module, and a module
        // holds its own, so this is a declaration no user wrote and no user reads a verdict about.
        return;
    }
    auto diagnostic = sink->of(SemanticDiag::EffectInRuleCondition).atOrigin(origin);
    if (entry.where != camel::source::kInvalidOriginId) {
        diagnostic.relatedOrigin(
            entry.where,
            std::format("the rule '{}' is declared here", entry.name));
    }
    Diagnostic reported =
        diagnostic.commit(describeEffect(inside, effect, inside != condition), entry.name);
    if (module != nullptr) {
        // A report made where the build is over goes to the run's channel, which is created
        // without a module: name the module that declares the rule here, as the run does for a
        // verdict about its own configuration.
        reported.moduleName = module->name();
        reported.modulePath = module->path();
    }
}

camel::core::error::Diagnostics *Builder::userSink() {
    if (speculative_ != nullptr) {
        if (camel::core::error::Diagnostics *held = speculative_->sinkForTheProgram();
            held != nullptr) {
            return held;
        }
    }
    if (diags_ != nullptr) {
        return diags_.get();
    }
    // The build that wrote the module is over and keeps no sink of its own. What the module said
    // is the module's, and it stays there: a library that does not compile is read by whoever
    // asked for it, and its errors are not the run's. A rule is instantiated later than that --
    // the engine makes instances while the program runs -- so a verdict about the rule is one the
    // run reached, and it goes to the channel the run reports through.
    return context_ != nullptr ? context_->rtmDiags().get() : nullptr;
}

void Builder::reportUnusedFallibleValues() {
    // Design principles 3.9: a `T!` that is evaluated and that nothing takes is a warning,
    // because the error it may hold is lost without a trace. `evaluated` is the statement the value
    // is: in a sync block the statements run, and outside one a statement whose value nothing takes
    // does not run (design principles 3.1) -- and the error it would have given is lost either way,
    // which is what this warning is about. Nothing else reports that statement: a statement whose
    // value nobody takes is one diagnostic, this one.
    auto sourceContext = context_ ? context_->sourceContext() : nullptr;
    const auto warn =
        [&](const compile_graph_ptr_t &graph, node_handle_t node, Type *type, const char *remark) {
            camel::source::origin_id_t origin = nodeDraftOf(node)->nodeOrigin(nodeIdOf(node));
            if (origin == camel::source::kInvalidOriginId && sourceContext) {
                origin = sourceContext->debugMap().graphOrigin(graph->graphId());
            }
            diags_->of(SemanticDiag::UnusedFallibleValue)
                .atOrigin(origin)
                .commit(type->toString(), remark);
        };
    // The `T!` a node gives, if it gives one. A call was typed when it was built, which for
    // a callee whose return type is taken from its body may be before that body was
    // complete (a recursive callee): the callee is asked.
    const auto fallibleTypeOf = [](const compile_graph_ptr_t &graph, node_handle_t node) {
        Type *type = nodeTypeOf(node);
        if (nodeIsKind(node, runtime::GCNodeKind::Func)) {
            if (const auto callee = graph->funcTarget(node)) {
                type = callee->funcType()->exitType();
            }
        }
        if (valueTypeOf(type) != type) {
            // A cell keeps the value it was given: a `T!` stored in one is not a `T!` that
            // nothing takes, and the statement that makes or fills the cell does not lose it.
            return static_cast<TupleType *>(nullptr);
        }
        return asFallible(valueTypeOf(type));
    };
    // The entry function, when it declares no return type: what its body ends with is not
    // the result of the program (design principles 9.8), so nothing takes that either.
    DraftGraphBuilder *undeclaredMain = nullptr;
    if (context_ && module_ && context_->mainModule() == module_) {
        if (auto mains = graphsAt("main");
            mains.has_value() && !mains.value()->empty() &&
            inferredExitGraphs_.contains(
                camel::runtime::toValue(mains.value()->front()->graphId()))) {
            undeclaredMain = mains.value()->front().get();
        }
    }

    for (const compile_graph_ptr_t &graph : unit_->builtBodies) {
        auto &draft = graph->transaction().edit();
        if (draft.exitNode() == runtime::kInvalidNodeRef) {
            continue; // the body did not build (an error was reported)
        }
        const auto isExit = [&](draft_node_ref_t id) {
            return id == draft.exitNode() || id == draft.outputNode() || id == draft.returnNode();
        };
        if (graph.get() == undeclaredMain && draft.returnNode() != runtime::kInvalidNodeRef) {
            node_handle_t value = draft.ref(draft.returnNode());
            if (TupleType *type = fallibleTypeOf(graph, value);
                type != nullptr && !discardedValues_.contains(value)) {
                warn(
                    graph,
                    value,
                    type,
                    " ('main' declares no return type, so what its body ends with is not the "
                    "result of the program)");
            }
        }
        for (draft_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
            if (!draft.alive(id)) {
                continue;
            }
            node_handle_t node = draft.ref(id);
            switch (nodeKindOf(node)) {
            case runtime::GCNodeKind::Oper:
            case runtime::GCNodeKind::Func:
            case runtime::GCNodeKind::Call:
            case runtime::GCNodeKind::Join:
                break;
            default:
                continue; // not the result of an evaluation
            }
            // "Ordered" here means a statement of a `sync` block: the edge that says so is a
            // sync edge, and the old control edge still carries it where one is left.
            const bool ordered = !draft.ctrlUsersOf(id).empty() || !draft.syncUsersOf(id).empty();
            // The arms of a JOIN that joins nothing that actually give a value: the written arm
            // of an `if` without `else` and the `null` the empty arm hands over are two WITH
            // inputs, so the number of inputs says nothing -- the `null` placeholder is a void
            // value no arm could have written and no error can be lost in it.
            const auto givesAValue = [&](draft_node_ref_t armId) {
                node_handle_t armValue = draft.ref(armId);
                if (nodeIsKind(armValue, runtime::GCNodeKind::Gate) &&
                    !normInputsOf(armValue).empty()) {
                    armValue = draft.ref(normInputsOf(armValue).front());
                }
                return !isVoidGraphExitType(nodeTypeOf(armValue));
            };
            if (nodeIsKind(node, runtime::GCNodeKind::Join) &&
                isVoidGraphExitType(nodeTypeOf(node)) && (ordered || isExit(id)) &&
                std::ranges::count_if(draft.withInputsOf(id), givesAValue) == 1) {
                // An `if` without `else`: the join takes nothing, so what an arm gives is
                // dropped where the arm ends. A branch with two arms is answered where its arms
                // end instead (`ExitInUnusedBranch`, `unused_arm_exit`): both arms hand a value to
                // a JOIN that joins nothing, and the JOIN is a statement of the region when a
                // write reaches the exit, so `ordered` alone does not tell the two shapes apart.
                for (draft_node_ref_t armId : draft.withInputsOf(id)) {
                    node_handle_t value = draft.ref(armId);
                    if (nodeIsKind(value, runtime::GCNodeKind::Gate) &&
                        !normInputsOf(value).empty()) {
                        value = draft.ref(normInputsOf(value).front());
                    }
                    if (TupleType *type = fallibleTypeOf(graph, value);
                        type != nullptr && !discardedValues_.contains(value)) {
                        warn(graph, value, type, "");
                    }
                }
                continue;
            }
            if (isExit(id) || !draft.normUsersOf(id).empty() || !draft.withUsersOf(id).empty() ||
                discardedValues_.contains(node) ||
                (!ordered && nodeIsKind(node, runtime::GCNodeKind::Join))) {
                // A branch whose value nothing takes is answered where its arms end -- their
                // exits are reported, and an `if` without `else` is answered above -- so what is
                // reported here is the result of an evaluation: a call, an operator.
                continue;
            }
            if (TupleType *type = fallibleTypeOf(graph, node)) {
                // A statement something is ordered after runs, in a sync block as anywhere else;
                // one nothing depends on does not, and then the warning says both that the value
                // is lost and that the statement that would have given it never runs.
                warn(
                    graph,
                    node,
                    type,
                    ordered ? "" : " (nothing depends on it, so it does not run)");
            }
        }
    }
}

void Builder::rejectNameWithoutValue(
    node_handle_t node, const std::string &name, const GCT::node_ptr_t &origin) {
    if (!nodeIsKind(node, runtime::GCNodeKind::Dref)) {
        return;
    }
    rejectUnappliedBinding(node, origin);
    if (genericDrefs_.contains(node)) {
        (void)genericValueFor(node, nullptr, origin); // nothing here fixes its types
    }
    const auto &target = nodeGraphOf(node)->drefTarget(node);
    std::string what   = "an operator with several signatures";
    if (std::holds_alternative<graph_vec_ptr_t>(target)) {
        auto graphs = asCompileGraphVec(target);
        what        = std::format("{} functions of that name", graphs ? graphs->size() : 0);
        if (graphs && !graphs->empty()) {
            what += " [" +
                    strutil::join(
                        *graphs,
                        "; ",
                        [](const graph_ptr_t &g) { return g->funcType()->toString(); }) +
                    "]";
        }
    } else if (std::holds_alternative<oper_group_ptr_t>(target)) {
        // The candidates, so that the reader can write the wrapper the message asks for.
        const auto &ops        = std::get<oper_group_ptr_t>(target);
        const std::string sigs = strutil::join(
            ops->resolvers(),
            "; ",
            [](const std::pair<std::string, resolver_ptr_t> &p) { return p.second->signature(); });
        if (ops->resolvers().size() == 1) {
            const bool fixed = ops->resolvers().front().second->fixedSignature() != nullptr;
            what = fixed ? std::format("an operator that changes its argument in place [{}]", sigs)
                         : std::format(
                               "an operator whose signature depends on its arguments [{}]",
                               sigs);
        } else {
            what =
                std::format("an operator with {} signatures [{}]", ops->resolvers().size(), sigs);
        }
    }
    diags_->of(SemanticDiag::FunctionNameAsValue).atOrigin(origin->origin()).commit(name, what);
    throw BuildAbortException();
}

void Builder::markNonRetainingReads() {
    // A read `r` of a variable becomes `:var/peek`, which leaves the proof that the variable's
    // value has no other observer standing, when the array it reads can be referred to by
    // nothing but the variable once the users of `r` have run, and they all run before the
    // variable can be written again. Then the next `acc = acc->append<v>` may still change the
    // array in place: nobody can see the difference. The conditions, every one required:
    //
    //  1. `r` was made in a sync block, so the sync chain orders it (syncedReads_).
    //  2. `r` is not the value of its graph (returned, the exit): that value leaves.
    //  3. Every user of `r` is an operator node in the sync chain, declared pure (no state, so
    //     the result is all it leaves behind), whose result type holds no reference to an
    //     array: a number, a bool, a string or nothing (`len`, `contains`, `==`, `<`, `[]`
    //     of an array of numbers, `format`). A call, a branch, a closure, a literal holding
    //     the value, a `let` used later by such a node: any of them keeps `r` a `:var/get`.
    //     Two things are read through rather than counted, and only where `r` can hand an array
    //     out: a pure computation that stands off the chain (5.3 leaves it off, so its place in
    //     the chain says nothing about when it runs -- its own readers are the ones that count),
    //     and a GATE, which forwards a value into the chain without observing it and is built at
    //     the chain's end. A value copied by value has no array to hand out, so for it every user
    //     is counted as before.
    //  4. No node made between `r` and its last user may write the variable: no operator
    //     that is neither pure nor state-only (the assignments, `:op/assn_owned` among them,
    //     effects), no call and no branch (their bodies may write it through a capture). The
    //     chain runs the nodes in the order they were made, so the users of `r` run before any
    //     later writer.
    //  5. The graph has no writer outside the chain (made in a nested block that is not a
    //     sync block), which could run between `r` and its users.
    //
    // Anything not covered keeps the read a `:var/get`, which clears the mark: at worst the
    // next append copies, as it would without this. The decision is made here, once the
    // graph is complete; the passes treat both reads alike (oper::isVariableRead).
    auto &table              = camel::core::OperatorTable::instance();
    const auto referenceFree = [](Type *type) {
        return type != nullptr && (type->code() == TypeCode::String || !isGCTraced(type->code()));
    };
    const auto mayWrite = [&](node_handle_t node) {
        switch (nodeKindOf(node)) {
        case runtime::GCNodeKind::Oper: {
            const auto op     = nodeGraphOf(node)->operTarget(node);
            const auto traits = table.find(op->uri());
            return !traits.has_value() || (!traits->pure && !traits->stateOnly);
        }
        case runtime::GCNodeKind::Call:
        case runtime::GCNodeKind::Func:
        case runtime::GCNodeKind::Brch:
            return true;
        default:
            return false;
        }
    };
    // Condition 5, per graph.
    std::unordered_map<runtime::GraphTransaction *, bool> unchainedWriter;
    const auto hasUnchainedWriter = [&](runtime::GraphTransaction *transaction) {
        if (auto it = unchainedWriter.find(transaction); it != unchainedWriter.end()) {
            return it->second;
        }
        const auto &draft = transaction->graph();
        bool found        = false;
        for (draft_node_ref_t id = 0; !found && id < draft.nodeSlotCount(); ++id) {
            if (draft.alive(id)) {
                node_handle_t node = draft.ref(id);
                found              = mayWrite(node) && !chainedNodes_.contains(node);
            }
        }
        return unchainedWriter[transaction] = found;
    };
    const oper_group_ptr_t peekGroup = internalOperatorGroup(camel::core::oper::VarPeek);
    const std::vector<StaticArg> statics(1);

    for (node_handle_t read : syncedReads_) {
        // A synced read names the graph it was made in, and that graph is handed over when its body
        // is complete and released with its builder: `nodeTransactionOf` would then assert that the
        // read has an owner (builder_support.h, `nodeGraphOf`). Nothing is left to mark in a graph
        // that is gone, so such a read is skipped instead -- and it is skipped for the graph the
        // read was made in, not for whichever graph the allocator put at its address since.
        auto *owner = DraftGraphBuilder::fromRef(read);
        if (owner == nullptr) {
            continue;
        }
        runtime::GraphTransaction *transaction = &owner->transaction();
        const auto &draft                      = transaction->graph();
        const draft_node_ref_t id              = nodeIdOf(read);
        if (!draft.alive(id) || id == draft.exitNode() || id == draft.outputNode() ||
            id == draft.returnNode()) {
            continue; // condition 2
        }
        draft_node_ref_t lastUser = id;
        bool keeps                = false;
        size_t users              = 0;
        // The nodes that decide the read: the nodes reading it, and -- through a pure computation
        // that reads it -- the nodes reading that computation, because such a node runs when its
        // own readers need it and its result is consumed where they are (design principles 3.1;
        // edges.cn.md 5.3 keeps a pure infallible computation off the chain, so standing off the
        // chain says nothing about when it runs). Both are walked so that condition 4 counts the
        // last reader of the *result*, not the last node that happens to name the read: a `len`
        // read by a `println` written after the next append is consumed after it.
        //
        // What a node reached this way can do is order, not observe: a reader of a computation
        // that hands out no reference cannot hold the array, so it decides nothing here, and a
        // GATE forwards a value into the block's chain without observing it -- `closeChainOver`
        // builds it at the chain's end, so counting it would place the last reader after every
        // writer in the block. A node that reads the array itself is the one that decides it, and
        // only a pure computation is not an observer.
        //
        // All of this is about a value that can hand an array out. A value copied by value has
        // nothing to hand out, and there the read keeps the form it always had.
        const bool handsOutReference = !referenceFree(nodeTypeOf(read));
        std::vector<std::pair<draft_node_ref_t, bool>> pending;
        for (auto inputs : {normUsersOf(read), withUsersOf(read)}) {
            for (draft_node_ref_t userId : inputs) {
                pending.emplace_back(userId, false);
            }
        }
        std::vector<draft_node_ref_t> seen;
        while (!pending.empty() && !keeps) {
            const auto [userId, reached] = pending.back();
            pending.pop_back();
            if (std::ranges::find(seen, userId) != seen.end()) {
                continue;
            }
            seen.push_back(userId);
            node_handle_t user = draft.ref(userId);
            if (handsOutReference && nodeIsKind(user, runtime::GCNodeKind::Gate)) {
                for (auto inputs : {normUsersOf(user), withUsersOf(user)}) {
                    for (draft_node_ref_t next : inputs) {
                        pending.emplace_back(next, true);
                    }
                }
                continue;
            }
            ++users;
            lastUser               = std::max(lastUser, userId);
            const bool computation = nodeIsKind(user, runtime::GCNodeKind::Oper) &&
                                     table.isPure(nodeGraphOf(user)->operTarget(user)->uri()) &&
                                     referenceFree(nodeTypeOf(user));
            if (!computation) {
                keeps = keeps || !reached; // condition 3: a node reading the array itself
                continue;
            }
            if (chainedNodes_.contains(user)) {
                continue; // a pure computation the chain holds: it runs where it stands
            }
            if (!handsOutReference) {
                keeps = true; // condition 3, as it always was for a value copied by value
                continue;
            }
            for (auto inputs : {normUsersOf(user), withUsersOf(user)}) {
                for (draft_node_ref_t next : inputs) {
                    pending.emplace_back(next, true);
                }
            }
        }
        if (keeps || users == 0) {
            continue;
        }
        for (draft_node_ref_t between = id + 1; !keeps && between < lastUser; ++between) {
            keeps = draft.alive(between) && mayWrite(draft.ref(between)); // condition 4
        }
        if (keeps || hasUnchainedWriter(transaction)) {
            continue;
        }
        node_handle_t cell = draft.ref(normInputsOf(read).front());
        const auto peek    = peekGroup->resolve({}, {nodeTypeOf(cell)}, statics);
        GUARD(peek.has_value(), "__var_peek__ rejected a variable.");
        nodeGraphOf(read)->retargetOperNode(read, *peek);
    }
    syncedReads_.clear();
}

void Builder::rejectDiscardedValue(BodyKey body) {
    if (auto it = bodyValueStatement_.find(body); it != bodyValueStatement_.end()) {
        rejectUnusedPureResult(it->second.first, it->second.second);
    }
}

Builder::BodyKey Builder::bodyKeyOf(const DraftGraphBuilder *graph) {
    return graph != nullptr ? BodyKey{false, camel::runtime::toValue(graph->graphId())} : BodyKey{};
}

Builder::BodyKey Builder::currentBodyKey() const {
    // The arm that is being built is the body; a body of no arm is the graph of the function it
    // is written in.
    const ArmBuild *arm = activeArm();
    return arm != nullptr ? BodyKey{true, arm->bodyId} : bodyKeyOf(currGraph_.get());
}

void Builder::rejectUnusedPureResult(const GCT::node_ptr_t &stmt, node_handle_t node) {
    if (node == nullptr || !nodeIsKind(node, runtime::GCNodeKind::Oper) ||
        discardedValues_.contains(node)) {
        return; // not an operator, or dropped on purpose (`let _`), or reported already
    }
    if (valueTypeOf(nodeTypeOf(node))->code() == TypeCode::Never) {
        // A statement of the bottom type `never` is not a value somebody forgot to bind (design
        // principles 3.9, a `fail` written as a statement of
        // its own): it gives no value at all, so there is nothing to use and no "it changes
        // nothing" to say either. Whether it is executed at all is the effect rule's question,
        // and a `fail` nobody needs has a warning of its own (FailNotExecuted).
        return;
    }
    const auto op = nodeGraphOf(node)->operTarget(node);
    if (op == nullptr || !camel::core::OperatorTable::instance().isPure(op->uri())) {
        return;
    }
    GCT::node_ptr_t call = stmt;
    if (call->type() != GCT::NodeKind::LINK || call->size() < 1) {
        return;
    }
    // The name the program wrote: the operator's symbol, or the function's name.
    GCT::node_ptr_t callee = call->at(0);
    while (callee->type() == GCT::NodeKind::WITH && callee->size() > 0) {
        callee = callee->at(0);
    }
    std::string written = callee->type() == GCT::NodeKind::DREF ? callee->name() : op->name();
    if (const char *symbol = operatorSymbolOf(written)) {
        written = symbol;
    }
    // When the operation is applied to a variable, the program most likely meant to change
    // it, as `xs->append<5>` did before 11.3: the fix is to bind the variable to the result.
    std::string fix;
    const std::string text = sourceTextOf(call);
    // The name a closure captured is a PORT and not a variable once the capture is by value
    // (design principles 3.5, 197), but it is still the name of a variable of the body the closure
    // stands in: the sentence below is about what the program meant to do, and the capture rule is
    // the one that answers whether the write is allowed. Asking only for a variable made the report
    // fall back to the generic sentence, which names nothing the reader can act on.
    node_handle_t target = nullptr;
    if (call->size() >= 2 && call->at(1)->type() == GCT::NodeKind::DREF &&
        nodeAt(call->at(1)->name()).has_value()) {
        target = resolveNodeByRef(call->at(1)->name());
    }
    // A name built as a value is assignable just as the cell was, so the advice to
    // write the result to it still holds: the same reading, with the name standing for its value.
    if (target != nullptr &&
        (isVariable(target) || capturePortOfAVariable(target) || nameIsSsa(call->at(1)->name()))) {
        const std::string variable = call->at(1)->name();
        fix = text.empty() ? std::format(
                                 "; to change the variable '{0}', bind it to the result: "
                                 "'{0} = ...'",
                                 variable)
                           : std::format(
                                 "; to change the variable '{0}', bind it to the result: "
                                 "'{0} = {1}'",
                                 variable,
                                 text);
    } else {
        fix = "; use the result ('let ys = ...') or drop it on purpose with 'let _ = ...'";
    }
    diags_->of(SemanticDiag::UnusedPureResult).atOrigin(stmt->origin()).commit(written, fix);
    // Reported here; it is not also an unused `T!` (reportUnusedFallibleValues).
    discardedValues_.insert(node);
}

namespace {

namespace rtdata = camel::core::rtdata;
using rtdata::Float32;
using rtdata::Float64;
using rtdata::Int32;
using rtdata::Int64;

/// Whether the conversion function of `to` applied to the literal `value` of `from` has no
/// value, by the rule of the conversion functions themselves (camel/core/numeric_convert.h,
/// the operators in builtin/operators/cast.cpp): a float to an integer truncates and faults
/// when the result does not fit, an integer to i32 faults when it does not fit, an integer to
/// a float rounds (an f32 overflow is an infinity, not a fault), and a real to a complex
/// number is always representable. This is not the `as` conversion: a CAST node is built only
/// where castSafetyFrom says Safe, and Type::castSlotFrom asserts exactly that. `int(x)` and
/// `x as int` share their arithmetic but not their legality (design principles 5.1), so a
/// literal argument is checked here by the conversion function's rule.
void checkLiteralNumericConversion(TypeCode from, TypeCode to, slot_t value) {
    // A bf16 or f16 source is the f32 it widens to, exactly, so `bf16(6.0)` as an int is checked
    // as `f32(6.0)` is (design principles 7.3). A target of either type is not checked here: the
    // conversion into it is the written one, and its rule (`halfFromText`,
    // camel/core/numeric_convert_half.h) is the one the call reports at run time.
    const bool fromHalf = from == TypeCode::BFloat16 || from == TypeCode::Float16;
    const float fromValue =
        !fromHalf ? 0.0f
                  : (from == TypeCode::BFloat16 ? rtdata::halfToFloat<rtdata::BFloat16>(value)
                                                : rtdata::halfToFloat<rtdata::Float16>(value));
    switch (to) {
    case TypeCode::Int32:
        if (from == TypeCode::Int64) {
            (void)camel::core::checkedNarrow<Int32>(rtdata::fromSlot<Int64>(value), "i32");
        } else if (fromHalf) {
            (void)camel::core::checkedTruncate<Int32>(fromValue, "i32");
        } else if (from == TypeCode::Float32) {
            (void)camel::core::checkedTruncate<Int32>(rtdata::fromSlot<Float32>(value), "i32");
        } else if (from == TypeCode::Float64) {
            (void)camel::core::checkedTruncate<Int32>(rtdata::fromSlot<Float64>(value), "i32");
        }
        break;
    case TypeCode::Int64:
        if (fromHalf) {
            (void)camel::core::checkedTruncate<Int64>(fromValue, "int");
        } else if (from == TypeCode::Float32) {
            (void)camel::core::checkedTruncate<Int64>(rtdata::fromSlot<Float32>(value), "int");
        } else if (from == TypeCode::Float64) {
            (void)camel::core::checkedTruncate<Int64>(rtdata::fromSlot<Float64>(value), "int");
        }
        break;
    default:
        break;
    }
}

} // namespace

void Builder::rejectLiteralConversion(
    const std::string &group, FunctionType *funcType, const std::vector<Argument> &operands,
    const GCT::node_ptr_t &call) {
    static const std::unordered_set<std::string> kConversions =
        {"int", "i64", "i32", "float", "f64", "f32", "c64", "c128"};
    if (!kConversions.contains(group) || operands.size() != 1 || funcType == nullptr ||
        !funcType->hasExitType() || operands[0].expr->type() != GCT::NodeKind::DATA) {
        return;
    }
    // `int(1.0e30)`, `i32(5000000000)`, `int('12abc')`: the argument is written in the
    // program and the conversion is known not to fit, so it is refused here as a literal
    // given a type it cannot have is (`let b: i32 = 3000000000`). A computed argument stays
    // a fault when the program runs.
    const data_ptr_t data = operands[0].expr->data();
    Type *target          = funcType->exitType();
    if (data == nullptr) {
        return;
    }
    const bool text = data->type()->code() == TypeCode::String;
    if (!text && !data->type()->isPrimitive()) {
        return;
    }
    try {
        if (text) {
            // The rule of the conversion itself (camel/core/numeric_convert.h).
            const std::string &written = std::dynamic_pointer_cast<StringData>(data)->data();
            switch (target->code()) {
            case TypeCode::Int32:
                (void)camel::core::integerFromText<int32_t>(written, "i32");
                break;
            case TypeCode::Int64:
                (void)camel::core::integerFromText<int64_t>(written, "int");
                break;
            case TypeCode::Float32:
                (void)camel::core::floatFromText<float>(written, "f32");
                break;
            case TypeCode::Float64:
                (void)camel::core::floatFromText<double>(written, "float");
                break;
            default:
                break;
            }
        } else if (const auto value = staticArgOf(operands[0].node).value) {
            checkLiteralNumericConversion(data->type()->code(), target->code(), *value);
        }
        return;
    } catch (const camel::core::error::RuntimeFault &fault) {
        // Why the conversion has no value, in the words the conversion uses: the text is not a
        // number, or it is a number the type cannot hold (which names the range, as an
        // out-of-range literal does).
        const std::string reason =
            text ? (fault.diag() == camel::core::error::RuntimeDiag::ConversionOutOfRange
                        ? "out of range (" + literal::rangeText(target) + ")"
                        : std::string("the text is not a number"))
                 : std::string();
        rejectLiteralData(
            data,
            target,
            call->origin(),
            operands.empty() ? nullptr : operands[0].expr,
            camel::source::kInvalidOriginId,
            reason);
    }
}

void Builder::rejectAmbiguousCall(
    const std::string &name, const std::string &argumentTypes,
    const std::vector<std::string> &candidates, const GCT::node_ptr_t &origin) {
    rejectAmbiguousCallAt(name, argumentTypes, candidates, origin->origin());
}

void Builder::rejectAmbiguousCallAt(
    const std::string &name, const std::string &argumentTypes,
    const std::vector<std::string> &candidates, camel::source::origin_id_t origin) {
    // The one report for several candidates in the pass a call is making (design principles
    //  5.1): an ordinary call, an operator call and a partial application used as a
    // value all come here. Listing the candidates in declaration order lets a reader see
    // both the one written here and the one that makes the choice ambiguous; the help says
    // how to choose (annotate the literal, add a cast).
    GUARD(!candidates.empty(), "An ambiguity has candidates.");
    diags_->of(SemanticDiag::AmbiguousCall)
        .atOrigin(origin)
        .commit(name, argumentTypes, strutil::join(candidates, "; ", [](const std::string &s) {
                    return s;
                }));
    throw BuildAbortException();
}

void Builder::rejectAmbiguousCall(
    const std::string &name, const std::string &argumentTypes,
    const std::vector<FunctionType *> &candidates, const GCT::node_ptr_t &origin) {
    std::vector<std::string> signatures;
    signatures.reserve(candidates.size());
    for (FunctionType *candidate : candidates) {
        GUARD(candidate != nullptr, "An ambiguity lists function types.");
        signatures.push_back(candidate->toString());
    }
    rejectAmbiguousCall(name, argumentTypes, signatures, origin);
}

void Builder::rejectUnappliedBinding(node_handle_t node, const GCT::node_ptr_t &origin) {
    const auto boundIt = boundWithArgs_.find(node);
    if (boundIt == boundWithArgs_.end()) {
        return;
    }
    const std::string given = strutil::join(boundIt->second, ", ", [&](const Argument &arg) {
        return valueTypeOf(nodeTypeOf(arg.node))->toString();
    });
    std::string name        = "the function";
    if (const auto callee = boundWithCallee_.find(node);
        callee != boundWithCallee_.end() && callee->second->type() == GCT::NodeKind::DREF) {
        name = callee->second->name();
    }
    // Where the whole binding is: the `f<w>` expression (boundWithOrigin_), otherwise the position
    // the caller gives. The expression is the union of the node's own span with its operands
    // (design principles 5.1), as for a call; the destructive pass below runs after it is made.
    const auto originIt         = boundWithOrigin_.find(node);
    const GCT::node_ptr_t where = originIt != boundWithOrigin_.end() ? originIt->second : origin;
    const auto exprSpan         = callExpressionSpan(context_, where);
    const auto exprOrigin       = deriveGirOrigin(
        context_,
        where,
        camel::source::OriginKind::GirNode,
        "gir.with.expr",
        {},
        exprSpan);
    const camel::source::origin_id_t at =
        exprOrigin != camel::source::kInvalidOriginId ? exprOrigin : where->origin();
    // Several candidates in the pass the binding could use: the same ambiguity an ordinary
    // call reports, with the same candidate list. No position chose one (`expected`),
    // which is why the binding is being refused now.
    if (const auto ambiguous = ambiguousBindings_.find(node);
        ambiguous != ambiguousBindings_.end()) {
        std::vector<FunctionType *> candidates = std::move(ambiguous->second);
        ambiguousBindings_.erase(ambiguous);
        boundWithArgs_.erase(node);
        boundWithCallee_.erase(node);
        std::vector<std::string> signatures;
        signatures.reserve(candidates.size());
        for (FunctionType *candidate : candidates) {
            signatures.push_back(candidate->toString());
        }
        rejectAmbiguousCallAt(name, given, signatures, at);
    }
    std::string why;
    if (genericDrefs_.contains(node)) {
        why = "it is a generic function, whose type variables are fixed by a call";
    } else if (nodeIsKind(node, runtime::GCNodeKind::Dref)) {
        const auto &target = nodeGraphOf(node)->drefTarget(node);
        if (std::holds_alternative<graph_vec_ptr_t>(target)) {
            auto graphs = asCompileGraphVec(target);
            why         = std::format(
                "{} function(s) of that name, and not exactly one takes them [{}]; where "
                "several do, a declared type or a parameter must say which is meant",
                graphs ? graphs->size() : 0,
                graphs ? strutil::join(
                             *graphs,
                             "; ",
                             [](const graph_ptr_t &g) { return g->funcType()->toString(); })
                       : std::string());
        } else if (std::holds_alternative<oper_group_ptr_t>(target)) {
            const auto &ops = std::get<oper_group_ptr_t>(target);
            why             = std::format(
                "it is an operator whose signature depends on its arguments [{}], so it has "
                "no parameter types before it is called; write the call in a function, e.g. "
                "'(x: int) => {}<...>(x)'",
                strutil::join(
                    ops->resolvers(),
                    "; ",
                    [](const std::pair<std::string, resolver_ptr_t> &p) {
                        return p.second->signature();
                    }),
                name);
        } else {
            why = "it is a decorated function";
        }
    } else {
        why = "its type is '" + valueTypeOf(nodeTypeOf(node))->toString() + "'";
    }
    boundWithArgs_.erase(node);
    boundWithCallee_.erase(node);
    boundWithOrigin_.erase(node);
    diags_->of(SemanticDiag::PartialApplicationRefused).atOrigin(at).commit(given, name, why);
    throw BuildAbortException();
}

std::string Builder::describeTypeOfPlaceholders(const Type *type, const GCT::Tree *written) const {
    if (type == nullptr) {
        return {};
    }
    const std::string text = type->toString();
    const std::string mark = "typeof#";
    std::string described;
    described.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        if (text.compare(i, mark.size(), mark) != 0) {
            described += text[i++];
            continue;
        }
        size_t end = i + mark.size();
        while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end])) != 0) {
            ++end;
        }
        if (end == i + mark.size()) {
            described += text[i++];
            continue;
        }
        const std::string digits = text.substr(i + mark.size(), end - (i + mark.size()));
        const GCT::node_ptr_t expression =
            written != nullptr ? written->typeOfOperandOf(mark + digits) : nullptr;
        if (expression == nullptr) {
            described += "typeof(#" + digits + ")";
        } else {
            described += "typeof(" + sourceTextOf(expression) + ")";
        }
        i = end;
    }
    return described;
}

std::string Builder::sourceTextOf(const GCT::node_ptr_t &expr) const {
    const auto *sourceContext = context_ ? context_->sourceContext().get() : nullptr;
    if (sourceContext == nullptr || expr == nullptr) {
        return {};
    }
    // The span of a node covers what the node itself was made from (the argument list of a
    // call, the operator of an expression): the expression is the union with its operands.
    camel::source::source_file_id_t fileId = camel::source::kInvalidSourceFileId;
    size_t start = 0, end = 0;
    bool consistent                                         = true;
    std::function<void(const GCT::node_ptr_t &, int)> cover = [&](const GCT::node_ptr_t &node,
                                                                  int depth) {
        if (node == nullptr || depth > 32) {
            return;
        }
        const auto *record = sourceContext->origin(node->origin());
        const auto *span   = record ? sourceContext->span(record->primarySpan) : nullptr;
        if (span != nullptr && span->valid() && span->endOffset > span->startOffset) {
            if (fileId == camel::source::kInvalidSourceFileId) {
                fileId = span->fileId;
                start  = span->startOffset;
                end    = span->endOffset;
            } else if (fileId != span->fileId) {
                consistent = false;
            } else {
                start = std::min(start, span->startOffset);
                end   = std::max(end, span->endOffset);
            }
        }
        for (size_t i = 0; i < node->size(); ++i) {
            cover(node->at(i), depth + 1);
        }
    };
    cover(expr, 0);
    const auto *file =
        fileId != camel::source::kInvalidSourceFileId ? sourceContext->sourceFile(fileId) : nullptr;
    if (!consistent || file == nullptr || end > file->content.size() || end - start > 60) {
        return {};
    }
    const std::string text = file->content.substr(start, end - start);
    // Quoted only when it is certainly one expression on one line.
    int depth = 0;
    for (const char c : text) {
        if (c == '\r' || c == '\n') {
            return {};
        }
        depth += (c == '(' || c == '[' || c == '{')   ? 1
                 : (c == ')' || c == ']' || c == '}') ? -1
                                                      : 0;
        if (depth < 0) {
            return {};
        }
    }
    return depth == 0 ? text : std::string();
}

camel::source::origin_id_t Builder::expressionPositionOf(const GCT::node_ptr_t &expr) const {
    if (expr == nullptr) {
        return camel::source::kInvalidOriginId;
    }
    // A folded expression's own origin covers only its last operand: the position of a value it
    // computes to is the whole expression (design principles 5.1).
    const camel::source::span_id_t span = expressionSpan(context_, expr);
    if (span != camel::source::kInvalidSpanId) {
        const camel::source::origin_id_t where = deriveGirOrigin(
            context_,
            expr,
            camel::source::OriginKind::GirNode,
            "gir.expr.span",
            {},
            span);
        if (where != camel::source::kInvalidOriginId) {
            return where;
        }
    }
    return expr->origin();
}

namespace {

bool isIntegerType(Type *type) {
    return type != nullptr && type->isPrimitive() &&
           (type->code() == TypeCode::Int32 || type->code() == TypeCode::Int64);
}

/// The source text the origin covers (`1e40`), when it is short and on one line; empty
/// otherwise. An element of a composite literal is quoted as it was written, not as the value
/// it was lowered to (design principles 5.1).
std::string textAtOrigin(
    const camel::source::source_context_ptr_t &sourceContext, camel::source::origin_id_t origin) {
    const auto *record = sourceContext != nullptr && origin != camel::source::kInvalidOriginId
                             ? sourceContext->origin(origin)
                             : nullptr;
    const auto *span   = record != nullptr ? sourceContext->span(record->primarySpan) : nullptr;
    const auto *file =
        span != nullptr && span->valid() ? sourceContext->sourceFile(span->fileId) : nullptr;
    if (file == nullptr || span->endOffset < span->startOffset ||
        span->endOffset > file->content.size()) {
        return {};
    }
    std::string text = file->content.substr(span->startOffset, span->endOffset - span->startOffset);
    if (text.empty() || text.size() > 60 || text.find('\n') != std::string::npos ||
        text.find('\r') != std::string::npos) {
        return {};
    }
    return text;
}

/// Whether a numeric literal datum is a whole number (`1.0` is, `1.9` is not): put into an
/// integer type it either fits or is out of range; a fraction would be truncated.
bool isWholeNumber(const data_ptr_t &data) {
    if (std::dynamic_pointer_cast<LongData>(data) != nullptr ||
        std::dynamic_pointer_cast<IntData>(data) != nullptr) {
        return true;
    }
    double value = 0.0;
    if (const auto real = std::dynamic_pointer_cast<DoubleData>(data); real != nullptr) {
        value = real->data();
    } else if (const auto real32 = std::dynamic_pointer_cast<FloatData>(data); real32 != nullptr) {
        value = static_cast<double>(real32->data());
    } else {
        return false;
    }
    return std::isfinite(value) && std::trunc(value) == value;
}

} // namespace

void Builder::rejectLiteralValue(
    const literal::Value &value, Type *type, const GCT::node_ptr_t &expr) {
    const bool folded   = expr != nullptr && expr->type() != GCT::NodeKind::DATA;
    std::string written = sourceTextOf(expr);
    if (written.empty()) {
        written = value.toString();
    }
    const std::string range = literal::rangeText(type);
    std::string reason;
    if (value.underflows(type)) {
        // A nonzero value whose nearest value of the type is zero: not a range above its
        // largest, and not rounded away.
        reason = folded ? "the value " + value.toString() +
                              " is too small to be represented (the nearest value is zero)"
                        : "too small to be represented (the nearest value is zero)";
    } else if (!literal::kindEnters(value, type)) {
        // A value of a kind the type has no form of: a fractional value in an integer type, an
        // imaginary part in a real one. Said before the range, which would answer a question the
        // value does not raise (`0.5` is not outside the range of an integer, it is not an
        // integer). The words match the ones a decimal literal in an integer type already gets
        // (rejectLiteralData, `int(1.9)`).
        reason = isIntegerType(type) ? "the value would be truncated"
                                     : "the value cannot become that type";
    } else if (!range.empty()) {
        // A folded expression is quoted as written, so the value it came to is also said.
        reason = folded ? "the value " + value.toString() + " is out of range (" + range + ")"
                        : "out of range (" + range + ")";
    } else {
        reason = "the value does not fit the type";
    }
    diags_->of(SemanticDiag::LiteralNotRepresentable)
        .atOrigin(expressionPositionOf(expr))
        .commit(written, type->toString(), reason);
    throw BuildAbortException();
}

namespace {

/// Why an array literal cannot be an array type that states one dimension: the length it writes.
/// This decides nothing -- `arrayLiteralFitsShape` already decided, and the elements are each
/// reported by whoever builds the value -- it says which side is what, because a rank-one length
/// that does not match would otherwise get the fallback sentence "the value cannot become that
/// type", which names neither the length nor the side that wrote it, for a literal whose elements
/// all fit.
std::string arrayLengthReason(const data_ptr_t &data, Type *type) {
    const auto *stated = type != nullptr ? tt::as_ptr<ArrayType>(type) : nullptr;
    const auto written = data != nullptr ? std::dynamic_pointer_cast<ArrayData>(data) : nullptr;
    if (stated == nullptr || written == nullptr) {
        return {};
    }
    const std::span<const ExtentForm> extents = stated->extents();
    if (extents.size() != 1 || !extents.front().isConstant()) {
        return {};
    }
    const int64_t count = static_cast<int64_t>(written->raw().size());
    if (count == extents.front().constant()) {
        return {};
    }
    return "the literal writes " + std::to_string(count) + " element(s) and the type states " +
           std::to_string(extents.front().constant());
}

} // namespace

void Builder::rejectLiteralData(
    const data_ptr_t &data, Type *type, camel::source::origin_id_t where,
    const GCT::node_ptr_t &written, camel::source::origin_id_t writtenAt,
    const std::string &reasonGiven) {
    std::string reason;
    if (!reasonGiven.empty()) {
        // A refusal the conversion already explained (`int('99999999999999999999')` is out of
        // range, not text that is not a number).
        reason = reasonGiven;
    } else if (const std::string notFit = literalNotFitReason(data, type); !notFit.empty()) {
        reason = notFit;
    } else if (
        const std::string lengthMismatch = arrayLengthReason(data, type); !lengthMismatch.empty()) {
        reason = lengthMismatch;
    } else if (data->type()->isPrimitive() && isIntegerType(type)) {
        // A decimal literal in an integer type: `int(1.9)` loses the fraction, `int(1.0e30)`
        // is out of the range.
        reason = isWholeNumber(data) ? "out of range (" + literal::rangeText(type) + ")"
                                     : "the value would be truncated";
    } else if (data->type()->code() == TypeCode::String || type->code() == TypeCode::String) {
        // A text where another type is expected, or another type where a text is expected, is
        // a type mismatch, not a text that is not a number: there is no implicit conversion
        // between a value and a text either way (design principles 5.1), and `int(s)` /
        // `parse_int(s)` / `string(n)` are the explicit ones. The same position reports a
        // variable of type `string` with this diagnostic.
        diags_->of(SemanticDiag::DeclaredTypeMismatch)
            .atOrigin(where)
            .commit(data->type()->toString(), type->toString());
        throw BuildAbortException();
    } else {
        reason = "the value cannot become that type";
    }
    // The literal as it is written, when the expression it came from is at hand; an element
    // of a composite literal is quoted from its own position.
    std::string text = sourceTextOf(written);
    if (text.empty() && writtenAt != camel::source::kInvalidOriginId) {
        text = textAtOrigin(context_ != nullptr ? context_->sourceContext() : nullptr, writtenAt);
    }
    if (text.empty()) {
        text = literalValueText(data);
    }
    diags_->of(SemanticDiag::LiteralNotRepresentable)
        .atOrigin(where)
        .commit(text, describeTypeOfPlaceholders(type, tree_), reason);
    throw BuildAbortException();
}

std::string Builder::sourceLocationOf(uint64_t origin) const {
    const auto *sourceContext = context_ ? context_->sourceContext().get() : nullptr;
    const auto *record        = sourceContext ? sourceContext->origin(origin) : nullptr;
    const auto *span          = record ? sourceContext->span(record->primarySpan) : nullptr;
    const auto *file = span && span->valid() ? sourceContext->sourceFile(span->fileId) : nullptr;
    if (file == nullptr || span->startOffset > file->content.size()) {
        return {};
    }
    size_t line = 1, lineStart = 0;
    for (size_t i = 0; i < span->startOffset; ++i) {
        if (file->content[i] == '\n') {
            ++line;
            lineStart = i + 1;
        }
    }
    // The file name only: an error value must not differ with where the program is stored.
    const size_t slash = file->path.find_last_of("/\\");
    return std::format(
        "{}:{}:{}",
        slash == std::string::npos ? file->path : file->path.substr(slash + 1),
        line,
        span->startOffset - lineStart + 1);
}

void Builder::reportConversionGaps(
    const std::vector<std::pair<std::string, FunctionType *>> &candidates,
    const std::vector<Argument> &withInputs, const std::vector<Argument> &normInputs,
    const type_vec_t &withTypes, const type_vec_t &normTypes, const GCT::node_ptr_t &call) {
    // Reported only when exactly one overload is a conversion away: with several, which one
    // the program means is not the compiler's to choose.
    const std::pair<std::string, FunctionType *> *target = nullptr;
    std::string what, fixes;
    for (const auto &candidate : candidates) {
        FunctionType *funcType = candidate.second;
        if (funcType->withTypesCount() != withTypes.size() ||
            funcType->normTypesCount() != normTypes.size()) {
            continue;
        }
        bool fits = true;
        std::string graphWhat, graphFixes;
        const auto check = [&](const std::vector<Argument> &inputs,
                               const type_vec_t &types,
                               bool isWith) {
            for (size_t i = 0; i < types.size() && fits; ++i) {
                Type *want       = isWith ? funcType->withTypeAt(i) : funcType->normTypeAt(i);
                const bool isVar = isWith ? funcType->withIsVarAt(i) : funcType->normIsVarAt(i);
                std::vector<ConversionGap> gaps;
                if (!collectConversionGaps(want, valueTypeOf(types[i]), "", gaps)) {
                    fits = false;
                    return;
                }
                if (gaps.empty()) {
                    continue;
                }
                const size_t nameIndex = (isWith ? 0 : funcType->withTypesCount()) + i;
                const std::string name = nameIndex < funcType->argNamesCount()
                                             ? std::string(funcType->argNameAt(nameIndex))
                                             : std::string();
                const std::string text = sourceTextOf(inputs[i].expr);
                std::string subject =
                    std::string(isWith ? "with-argument " : "argument ") + std::to_string(i + 1);
                if (!text.empty() || !name.empty()) {
                    subject += " (" + (text.empty() ? "" : "'" + text + "'") +
                               (text.empty() || name.empty() ? "" : ", ") +
                               (name.empty() ? "" : "for parameter '" + name + "'") + ")";
                }
                graphWhat += (graphWhat.empty() ? "" : "; ") + describeGaps(subject, gaps);
                if (isVar) {
                    // A conversion yields a value; a var parameter takes the variable itself.
                    graphFixes += std::string(graphFixes.empty() ? "" : " ") +
                                  "No conversion applies to argument " + std::to_string(i + 1) +
                                  ": a var parameter takes the variable itself, and a variable " +
                                  "has the widened type of its first value. Pass the value to a " +
                                  "parameter that is not var, or declare the var parameter with " +
                                  "the variable's type and convert the value where it is read.";
                    continue;
                }
                std::string conversions;
                for (const ConversionGap &gap : gaps) {
                    conversions += (conversions.empty() ? "" : ", ") +
                                   conversionTextOf(text.empty() ? "<argument>" : text, gap);
                }
                graphFixes +=
                    std::string(graphFixes.empty() ? "" : " ") +
                    "Where the value is known to fit, write the conversion: " + conversions + ".";
            }
        };
        check(withInputs, withTypes, true);
        check(normInputs, normTypes, false);
        if (!fits || graphWhat.empty()) {
            continue;
        }
        if (target) {
            return;
        }
        target = &candidate;
        what   = std::move(graphWhat);
        fixes  = std::move(graphFixes);
    }
    if (!target) {
        return;
    }
    diags_->of(SemanticDiag::ArgumentNeedsConversion)
        .atOrigin(call->origin())
        .commit(
            (target->first.empty() ? "" : target->first + ": ") + target->second->toString(),
            what,
            fixes);
    throw BuildAbortException();
}

void Builder::checkDeclaredReturn(
    const compile_graph_ptr_t &graph, Type *resultType, uint64_t origin,
    const GCT::node_ptr_t &value) {
    // Only a DECLARED type is checked: an undeclared one is set from the first value returned,
    // after which hasExitType() is true as well, so the check runs before that happens.
    if (graph == nullptr || graph->view().exitNode() != runtime::kInvalidNodeRef) {
        return;
    }
    FunctionType *funcType = graph->funcType();
    if (funcType == nullptr || !funcType->hasExitType() || resultType == nullptr) {
        return;
    }
    Type *declared = funcType->exitType();
    // Not judged here: a body without a value (its last statement decides nothing about the
    // type), and types that are not resolved yet.
    if (declared == nullptr || declared == resultType || resultType->code() == TypeCode::Void ||
        declared->code() == TypeCode::Ref || resultType->code() == TypeCode::Ref) {
        return;
    }
    if (acceptsAsResult(declared, resultType)) {
        return;
    }
    std::vector<ConversionGap> gaps;
    const bool convertible = collectConversionGaps(declared, resultType, "", gaps) && !gaps.empty();
    auto diag              = diags_->of(
        convertible ? SemanticDiag::ReturnNeedsConversion : SemanticDiag::ReturnTypeMismatch);
    if (origin != 0) {
        diag.atOrigin(origin);
    } else if (auto sourceContext = context_ ? context_->sourceContext() : nullptr) {
        diag.atOrigin(sourceContext->debugMap().graphOrigin(graph->graphId()));
    }
    if (convertible) {
        const std::string text = sourceTextOf(value);
        std::string fixes;
        for (const ConversionGap &gap : gaps) {
            fixes += (fixes.empty() ? "" : ", ") +
                     conversionTextOf(text.empty() ? "<returned expression>" : text, gap);
        }
        diag.commit(
            declared->toString(),
            describeGaps(text.empty() ? "the value" : "the value ('" + text + "')", gaps),
            fixes,
            funcType->toString());
        throw BuildAbortException();
    }
    diag.commit(resultType->toString(), declared->toString(), funcType->toString());
    throw BuildAbortException();
}

// ---- A body that does not compile for the types it was tried with ----

Builder::SpeculativeInstantiation::SpeculativeInstantiation(Builder &builder)
    : self_(builder), previous_(builder.speculative_), outer_(builder.diags_) {
    // The sink of the bodies answers for the module the search happens in: the same name and the
    // same file, so a diagnostic kept from it reads as one the builder would have made. The sink is
    // not swapped in here: only a body build writes to it (`BodyDiagnostics`), so a verdict the
    // attempt makes on its way to the body is reported where it always was.
    //
    // It is made without the outer sink's configuration, and so without its limit. A limit says how
    // much the user is shown before the compiler gives up on them; a diagnostic kept here is shown
    // to nobody -- it is the reason one attempt at one type tuple did not come about, read by
    // `reason` and otherwise dropped -- so a limit on it reports nothing about what the user may
    // see and everything about the pass: a body that says why the law is not written for these
    // types would end the whole pass instead, and a program with no error in it would fail.
    //
    // Nothing grows without bound for the missing limit. The sink lives exactly as long as one
    // attempt at one type tuple, and a body build records at most one error per statement of the
    // body it is building (a statement that fails is skipped so the rest is still checked), so what
    // it holds is bounded by the body, not by the program the rule is applied to; the only reader
    // takes the first error and looks no further.
    inner_ = std::make_shared<Diagnostics>(
        outer_->moduleName(),
        outer_->modulePath(),
        outer_->sourceContext());
    self_.speculative_ = this;
}

Builder::SpeculativeInstantiation::~SpeculativeInstantiation() {
    settled_ = true;
    if (self_.speculative_ == this) {
        self_.speculative_ = previous_;
    }
    if (self_.diags_ == inner_) {
        // A body that is still open when the scope ends: nothing kept it, so nothing is reported.
        self_.diags_ = outer_;
    }
}

void Builder::SpeculativeInstantiation::close() {
    settled_ = true;
    if (self_.speculative_ == this) {
        self_.speculative_ = previous_;
    }
    if (self_.diags_ == inner_) {
        self_.diags_ = outer_;
    }
}

std::string Builder::SpeculativeInstantiation::reason() const {
    // The bodies hold their errors in the order they made them, and the first one is the verdict
    // the attempt died of: a body that does not compile stops there, so nothing after it is about
    // this attempt. A body that said nothing leaves the caller its own reason.
    for (const Diagnostic *error : inner_->errors()) {
        if (error != nullptr && !error->message.empty()) {
            return error->message;
        }
    }
    return {};
}

camel::core::error::Diagnostics *Builder::SpeculativeInstantiation::sinkForTheProgram() const {
    camel::core::error::Diagnostics *sink = outer_.get();
    for (const SpeculativeInstantiation *scope = previous_; scope != nullptr;
         scope                                 = scope->previous_) {
        if (scope->outer_ != nullptr) {
            sink = scope->outer_.get();
        }
    }
    return sink;
}

void Builder::SpeculativeInstantiation::attach() {
    if (settled_) {
        return;
    }
    // The errors carry what the body said about them (the instance and the call that asked for it,
    // from addRelatedToErrorsFrom); they are added to the sink in place, in order, and then the
    // scope ends, so the refusal the caller makes next is reported with them.
    for (const Diagnostic *error : inner_->errors()) {
        outer_->add(Diagnostic(*error));
    }
    close();
}

Builder::BodyDiagnostics::BodyDiagnostics(Builder &builder)
    : self_(builder), scope_(builder.speculative_) {
    if (scope_ != nullptr) {
        outer_       = self_.diags_;
        self_.diags_ = scope_->inner_;
    }
}

Builder::BodyDiagnostics::~BodyDiagnostics() {
    if (scope_ != nullptr && self_.diags_ == scope_->inner_) {
        self_.diags_ = outer_;
    }
}

} // namespace camel::compile::gir
