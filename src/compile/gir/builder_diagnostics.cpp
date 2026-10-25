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
#include "camel/core/numeric_convert.h"
#include "camel/core/operator_names.h"
#include "camel/core/rtdata/string.h"
#include "camel/execute/entry_args.h"
#include "camel/runtime/control.h"
#include "camel/runtime/graph.h"
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
        // The callee's variable, as the current graph sees it (see captureInto).
        node_handle_t variable = captureInto(currGraph_, origin, captureNameOf(origin, ""));
        uses.push_back({variable, origin, written.contains(origin)});
    }
    return uses;
}

void Builder::reportIgnoredCapturedWrites() {
    for (const PendingIgnoredWrite &pending : unit_->pendingIgnoredWrites) {
        std::set<node_handle_t> written = pending.written;
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
    const auto declaresEffects = [](Type *type) {
        if (type == nullptr || type->code() != TypeCode::Function) {
            return false;
        }
        const auto *func = tt::as_ptr<FunctionType>(type);
        if (func->modifiers().sync()) {
            return true;
        }
        for (size_t i = 0; i < func->normTypesCount(); ++i) {
            if (func->normIsVarAt(i)) {
                return true;
            }
        }
        for (size_t i = 0; i < func->withTypesCount(); ++i) {
            if (func->withIsVarAt(i)) {
                return true;
            }
        }
        return false;
    };
    const auto calleeOf = [&](node_handle_t func) { return graph->funcTarget(func); };
    switch (nodeKindOf(node)) {
    case runtime::GCNodeKind::Oper: {
        const auto &op = graph->operTarget(node);
        return op != nullptr && camel::core::OperatorTable::instance().hasExternalEffect(op->uri());
    }
    case runtime::GCNodeKind::Func: {
        const auto callee = calleeOf(node);
        if (callee == nullptr) {
            return false;
        }
        return declaresEffects(callee->funcType()) || effectfulGraphs_.contains(callee.get());
    }
    case runtime::GCNodeKind::Call: {
        const auto callee = withInputsOf(node);
        return !callee.empty() && declaresEffects(nodeTypeOf(nodeDraftOf(node)->ref(callee[0])));
    }
    case runtime::GCNodeKind::Loop: {
        // Each round runs the condition and the body, regions of the graph.
        const runtime::GraphDraft &draft = graph->draft();
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
        const runtime::GraphDraft &draft = graph->draft();
        const draft_node_ref_t join      = nodeIdOf(node);
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

void Builder::reportDroppedEffectsOfBodies() {
    for (const compile_graph_ptr_t &graph : unit_->builtBodies) {
        reportDroppedEffects(graph, true);
    }
    unit_->builtBodies.clear();
}

void Builder::reportDroppedEffects(const compile_graph_ptr_t &graph, bool report) {
    auto &draft = graph->draft();
    // What the exit depends on, through data, control and sync edges.
    std::unordered_set<draft_node_ref_t> needed;
    std::vector<draft_node_ref_t> work;
    for (draft_node_ref_t root : {draft.outputNode(), draft.exitNode(), draft.returnNode()}) {
        if (root != runtime::kInvalidNodeRef && needed.insert(root).second) {
            work.push_back(root);
        }
    }
    if (work.empty()) {
        return; // the body did not build (an error was reported)
    }
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
    std::vector<draft_node_ref_t> dropped;
    for (draft_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        if (!draft.alive(id) || !isEffectNode(graph.get(), draft.ref(id))) {
            continue;
        }
        if (needed.contains(id)) {
            effectfulGraphs_.insert(graph.get());
        } else if (
            graph != rootGraph_ || (id < rootNodeOwners_.size() && rootNodeOwners_[id] == unit_)) {
            // On the root graph, the top level of the module being reported only: another
            // module's top-level value that this program does not use is not a mistake.
            dropped.push_back(id);
        }
    }
    if (!report) {
        return;
    }
    // One warning per dropped chain: an effect whose result feeds another dropped effect
    // (`println(log(1))`, the statements of a dropped sync block) is reported through the
    // last one.
    const auto feedsDroppedEffect = [&](draft_node_ref_t start) {
        std::unordered_set<draft_node_ref_t> seen{start};
        std::vector<draft_node_ref_t> stack{start};
        while (!stack.empty()) {
            const draft_node_ref_t id = stack.back();
            stack.pop_back();
            for (auto users :
                 {draft.normUsersOf(id), draft.withUsersOf(id), draft.ctrlUsersOf(id),
                  draft.syncUsersOf(id)}) {
                for (draft_node_ref_t user : users) {
                    if (!seen.insert(user).second) {
                        continue;
                    }
                    if (std::ranges::find(dropped, user) != dropped.end()) {
                        return true;
                    }
                    stack.push_back(user);
                }
            }
        }
        return false;
    };
    auto sourceContext = context_ ? context_->sourceContext() : nullptr;
    for (draft_node_ref_t id : dropped) {
        if (feedsDroppedEffect(id)) {
            continue;
        }
        node_handle_t node = draft.ref(id);
        std::string subject;
        switch (nodeKindOf(node)) {
        case runtime::GCNodeKind::Oper: {
            const std::string &name = graph->operTarget(node)->name();
            subject = name.starts_with("__assn") ? std::string("The assignment")
                                                 : std::format("The call of '{}'", name);
        } break;
        case runtime::GCNodeKind::Func:
            subject = std::format("The call of '{}'", graph->funcTarget(node)->name());
            break;
        case runtime::GCNodeKind::Join:
            subject = "The branch";
            break;
        case runtime::GCNodeKind::Loop:
            subject = "The loop";
            break;
        default:
            subject = "The call";
            break;
        }
        camel::source::origin_id_t origin = nodeDraftOf(node)->nodeOrigin(nodeIdOf(node));
        if (origin == camel::source::kInvalidOriginId && sourceContext) {
            origin = sourceContext->debugMap().graphOrigin(graph->graphId());
        }
        diags_->of(SemanticDiag::EffectNotExecuted).atOrigin(origin).commit(subject);
    }
}

void Builder::reportUnusedFallibleValues() {
    // Design principles 3.9: a `T!` that is evaluated and that nothing takes is a warning,
    // because the error it may hold is lost without a trace. On the graph "evaluated and
    // not taken" is a node without a data user that something is ordered after: a statement
    // of a sync block. (Outside a sync block a value nothing takes is not evaluated at all:
    // design principles 3.1, which has its own warning, EffectNotExecuted.)
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
        return asFallible(valueTypeOf(type));
    };
    // The entry function, when it declares no return type: what its body ends with is not
    // the result of the program (design principles 9.8), so nothing takes that either.
    DraftGraphBuilder *undeclaredMain = nullptr;
    if (context_ && module_ && context_->mainModule() == module_) {
        if (auto mains = graphsAt("main");
            mains.has_value() && !mains.value()->empty() &&
            inferredExitGraphs_.contains(mains.value()->front().get())) {
            undeclaredMain = mains.value()->front().get();
        }
    }

    for (const compile_graph_ptr_t &graph : unit_->builtBodies) {
        auto &draft = graph->draft();
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
            const bool ordered =
                !draft.ctrlUsersOf(id).empty() || !draft.syncUsersOf(id).empty();
            if (nodeIsKind(node, runtime::GCNodeKind::Join) &&
                isVoidGraphExitType(nodeTypeOf(node)) && (ordered || isExit(id))) {
                // An `if` without `else`: the join takes nothing, so what an arm gives is
                // dropped where the arm ends.
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
            if (!ordered || isExit(id) || !draft.normUsersOf(id).empty() ||
                !draft.withUsersOf(id).empty() || discardedValues_.contains(node)) {
                continue;
            }
            if (TupleType *type = fallibleTypeOf(graph, node)) {
                warn(graph, node, type, "");
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
    std::unordered_map<runtime::GraphDraft *, bool> unchainedWriter;
    const auto hasUnchainedWriter = [&](runtime::GraphDraft *draft) {
        if (auto it = unchainedWriter.find(draft); it != unchainedWriter.end()) {
            return it->second;
        }
        bool found = false;
        for (draft_node_ref_t id = 0; !found && id < draft->nodeSlotCount(); ++id) {
            if (draft->alive(id)) {
                node_handle_t node = draft->ref(id);
                found              = mayWrite(node) && !chainedNodes_.contains(node);
            }
        }
        return unchainedWriter[draft] = found;
    };
    const oper_group_ptr_t peekGroup = internalOperatorGroup(camel::core::oper::VarPeek);
    const std::vector<StaticArg> statics(1);

    for (node_handle_t read : syncedReads_) {
        runtime::GraphDraft *draft = nodeDraftOf(read);
        const draft_node_ref_t id  = nodeIdOf(read);
        if (!draft->alive(id) || id == draft->exitNode() || id == draft->outputNode() ||
            id == draft->returnNode()) {
            continue; // condition 2
        }
        draft_node_ref_t lastUser = id;
        bool keeps                = false;
        size_t users              = 0;
        for (auto inputs : {normUsersOf(read), withUsersOf(read)}) {
            for (draft_node_ref_t userId : inputs) {
                node_handle_t user = draft->ref(userId);
                ++users;
                lastUser = std::max(lastUser, userId);
                if (!nodeIsKind(user, runtime::GCNodeKind::Oper) || !chainedNodes_.contains(user) ||
                    !table.isPure(nodeGraphOf(user)->operTarget(user)->uri()) ||
                    !referenceFree(nodeTypeOf(user))) {
                    keeps = true; // condition 3
                }
            }
        }
        if (keeps || users == 0) {
            continue;
        }
        for (draft_node_ref_t between = id + 1; !keeps && between < lastUser; ++between) {
            keeps = draft->alive(between) && mayWrite(draft->ref(between)); // condition 4
        }
        if (keeps || hasUnchainedWriter(draft)) {
            continue;
        }
        node_handle_t cell = draft->ref(normInputsOf(read).front());
        const auto peek    = peekGroup->resolve({}, {nodeTypeOf(cell)}, statics, Modifier::None);
        ASSERT(peek.has_value(), "__var_peek__ rejected a variable.");
        nodeGraphOf(read)->retargetOperNode(read, *peek);
    }
    syncedReads_.clear();
}

void Builder::rejectDiscardedValue(const void *body) {
    if (auto it = bodyValueStatement_.find(body); it != bodyValueStatement_.end()) {
        rejectUnusedPureResult(it->second.first, it->second.second);
    }
}

void Builder::rejectUnusedPureResult(const GCT::node_ptr_t &stmt, node_handle_t node) {
    if (node == nullptr || !nodeIsKind(node, runtime::GCNodeKind::Oper) ||
        discardedValues_.contains(node)) {
        return; // not an operator, or dropped on purpose (`let _`), or reported already
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
    if (call->size() >= 2 && call->at(1)->type() == GCT::NodeKind::DREF &&
        nodeAt(call->at(1)->name()).has_value() &&
        isVariable(resolveNodeByRef(call->at(1)->name()))) {
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
    ASSERT(!candidates.empty(), "An ambiguity has candidates.");
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
        ASSERT(candidate != nullptr, "An ambiguity lists function types.");
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
        .commit(text, type->toString(), reason);
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
    if (graph == nullptr || graph->draft().exitNode() != runtime::kInvalidNodeRef) {
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
    if (declared->assignableFrom(resultType)) {
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
        outer_->moduleName(), outer_->modulePath(), outer_->sourceContext());
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
        outer_        = self_.diags_;
        self_.diags_  = scope_->inner_;
    }
}

Builder::BodyDiagnostics::~BodyDiagnostics() {
    if (scope_ != nullptr && self_.diags_ == scope_->inner_) {
        self_.diags_ = outer_;
    }
}

} // namespace camel::compile::gir
