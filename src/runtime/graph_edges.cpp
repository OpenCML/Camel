// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The edge invariants of an encoded graph, checked at every commit (docs/design/graph-store 5.4).
 * The owner of these rules is the finished graph, executed by the seal check at commit, not the
 * draft's writers: a node still being built answers a different question.
 *
 *  - no cycle: following the inputs of a node (value, `with` and control alike) never comes back
 *    to it. A graph calls itself through other graphs, never through an edge;
 *  - the types agree along the edges: what a node produces has the type its readers need.
 *    The rules are the ones the compiler's own graphs satisfy, node kind by node kind (see
 *    checkNode); a rewrite that breaks one has built a graph that a back end would run on the
 *    wrong slot type;
 *  - a block's statements are one chain (docs/design/effects/edges.cn.md 5.3): each statement
 *    follows one other of its block and one statement follows it. A statement with two predecessors
 *    or with two statements after it is a chain that forks, which is no chain -- the layout walks
 *    one of them and the other is reached by nothing.
 *
 * Violations are collected; the commit fails with all of them, named by graph and node.
 */

#include "runtime/graph_build.h"

#include "camel/core/operator_table.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/cycle.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"
#include "camel/utils/assert.h"

#include <algorithm>
#include <format>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace camel::runtime {

using namespace camel::core::type;

namespace {

/// Reads a frozen graph for `camel::runtime::findInputCycle`: the data, `with`, control and sync
/// edges are runs of refs; an order edge is a record (`GCGraphOrderEdge`), so the adaptor collects
/// the predecessors it was asked for and hands out that scratch until the next call.
class FrozenCycleEdges {
  public:
    explicit FrozenCycleEdges(const GCGraph &graph) : graph_(graph) {}

    size_t slotCount() const { return graph_.nodeCount(); }
    bool present(gc_node_ref_t ref) const {
        return graph_.containsNodeRef(ref) && graph_.node(ref)->kind != GCNodeKind::Dead;
    }
    std::span<const gc_node_ref_t> inputsOf(gc_node_ref_t ref, CycleEdgeKind kind) {
        switch (kind) {
        case CycleEdgeKind::Norm:
            return graph_.normInputsOf(ref);
        case CycleEdgeKind::With:
            return graph_.withInputsOf(ref);
        case CycleEdgeKind::Ctrl:
            return graph_.ctrlInputsOf(ref);
        case CycleEdgeKind::Sync:
            sideEdges_.clear();
            for (const GCGraphSyncEdge &edge : graph_.syncPredsOf(ref)) {
                sideEdges_.push_back(edge.pred);
            }
            return std::span<const gc_node_ref_t>(sideEdges_);
        case CycleEdgeKind::Order:
            sideEdges_.clear();
            for (const GCGraphOrderEdge &edge : graph_.orderPredsOf(ref)) {
                sideEdges_.push_back(edge.pred);
            }
            return std::span<const gc_node_ref_t>(sideEdges_);
        }
        return {};
    }

  private:
    const GCGraph &graph_;
    /// The predecessors of the edge kind asked for last: sync and order edges are records, not
    /// runs of refs, so the adaptor hands out this scratch until the next call.
    std::vector<gc_node_ref_t> sideEdges_;
};

class EdgeChecker {
  public:
    EdgeChecker(const Program &program, const GCGraph &graph, GraphState state)
        : program_(program), graph_(graph), state_(state) {}

    std::vector<std::string> run() {
        checkNodeTypeIds();
        checkReturnValueType();
        checkWritings();
        checkOrderEdges();
        checkSyncEdges();
        checkSyncChainForks();
        findCycle();
        for (auto it = graph_.nodes().begin(); it != graph_.nodes().end(); ++it) {
            checkNode(it.ref(), **it);
        }
        return std::move(violations_);
    }

  private:
    /**
     * Every type id a frozen node names came from the type table's interning path, and the debug
     * assertion is this check: `TypeTable::adopt` -- the door an id could be handed to an object
     * beside the table, which is how a type's *object identity* got written into a graph -- is
     * gone, so an id the table does not hold is a type no path built. A build without assertions
     * still reports it here rather than reading a type out of the table's range.
     */
    void checkNodeTypeIds() {
        const auto &table = TypeTable::instance();
        for (auto it = graph_.nodes().begin(); it != graph_.nodes().end(); ++it) {
            const GCNode &node = **it;
            if (node.typeId != kNoTypeId && !table.holds(node.typeId)) {
                violations_.push_back(
                    std::format(
                        "graph '{}' node {}: names type id {} the type table does not hold",
                        graph_.name(),
                        it.ref(),
                        node.typeId));
            }
        }
        ASSERT(
            violations_.empty(),
            "A frozen graph names a type id that did not come from the type table's interning "
            "path (see typeIdOf in src/runtime/graph.cpp).");
    }

    /// The value a graph whose exit is a `T?` or a `T!` hands on is that tagged value. It is the
    /// shape the value is boxed into, and it is what a second writing of the exit leaves out: the
    /// second `return` writes its own value where the first wrote the boxed one, so the graph's
    /// signature and its exit no longer agree, and a back end that reads the exit as the declared
    /// type writes a slot of another shape -- the node VM crashed on exactly that. Refused here,
    /// where the graph is frozen, so no back end is ever handed one. Any other exit type is left
    /// to the builder: a function declared `void` hands on the value it computes, and the purity of
    /// a function type is not a difference the value's shape shows.
    void checkReturnValueType() {
        const gc_node_ref_t returned = graph_.returnNodeRef();
        if (returned == kInvalidNodeRef || graph_.returnKind() == GCReturnKind::None) {
            return;
        }
        const GCNode *node                        = graph_.returnNode();
        camel::core::type::FunctionType *funcType = graph_.funcType();
        if (node == nullptr || funcType == nullptr || !funcType->hasExitType()) {
            return;
        }
        camel::core::type::Type *declared = funcType->exitType();
        camel::core::type::Type *actual   = node->dataType();
        if (declared == nullptr || actual == nullptr || declared->equals(actual)) {
            return;
        }
        if (camel::core::type::asTagged(declared) == nullptr) {
            return;
        }
        violations_.push_back(
            std::format(
                "graph '{}': the value it hands on has type '{}', and the graph declares '{}'",
                graph_.name(),
                actual->toString(),
                declared->toString()));
    }

    /// The other half of the structural rule of 4.6: a sync edge joins two statements one `sync`
    /// block writes directly -- or the block's chain tail to the block's value -- and nothing else.
    /// The builder records that fact on each node it chains (kGCNodeFlagSyncStatement), so the
    /// check is: both ends are such a node, they are not the same node and not a node a rewrite
    /// removed, and one statement is written directly after one other (the tail that reaches the
    /// block's value is the one node that two sync edges leave). The other direction -- one
    /// statement two statements follow -- is `checkSyncChainForks` below.
    void checkSyncEdges() {
        if (!graph_.hasSyncEdges()) {
            return;
        }
        for (const GCGraphSyncEdge &edge : graph_.syncEdges()) {
            const auto bad = [&](const std::string &what) {
                violations_.push_back(
                    std::format(
                        "graph '{}': sync edge {} -> {} {}",
                        graph_.name(),
                        edge.pred,
                        edge.user,
                        what));
            };
            if (!graph_.containsNodeRef(edge.user) || !graph_.containsNodeRef(edge.pred)) {
                bad("names a node the graph does not have");
                continue;
            }
            const GCNode *user = graph_.node(edge.user);
            const GCNode *pred = graph_.node(edge.pred);
            if (user->kind == GCNodeKind::Dead || pred->kind == GCNodeKind::Dead) {
                bad("names a node a rewrite removed");
                continue;
            }
            if (edge.user == edge.pred) {
                bad("chains a node to itself");
                continue;
            }
            if ((user->flags & kGCNodeFlagSyncStatement) == 0) {
                bad(std::format(
                    "ends on a node no `sync` block wrote (kind {}, flags {}, data type '{}')",
                    static_cast<int>(user->kind),
                    user->flags,
                    user->dataType() != nullptr ? user->dataType()->toString() : "-"));
                continue;
            }
            if ((pred->flags & kGCNodeFlagSyncStatement) == 0) {
                bad(std::format(
                    "starts at a node no `sync` block wrote (kind {}, flags {}, data type '{}')",
                    static_cast<int>(pred->kind),
                    pred->flags,
                    pred->dataType() != nullptr ? pred->dataType()->toString() : "-"));
                continue;
            }
            if (graph_.syncPredsOf(edge.user).size() > 1) {
                bad("is the second predecessor of one statement");
            }
        }
    }

    /// The block a statement is written in: the innermost region that holds code of its own, with the
    /// region an inline splice gave the code it moved looked through -- that code is the code of the
    /// block it was moved into (docs/design/effects/edges.cn.md 5.7), which is exactly what
    /// `statementRegionOf` reads back on the draft (src/runtime/draft.cpp, beside
    /// `sameSyncBlock`). Nothing else is looked through: a region that holds code of its own -- an
    /// arm, a part of a loop, a block written as a statement -- separates blocks, and its chain is
    /// its own.
    uint32_t controlBlockOf(gc_node_ref_t ref) const {
        uint32_t region = graph_.nodeRegion(ref);
        while (region != 0) {
            const GCRegion *enclosing = graph_.region(region);
            if (enclosing == nullptr || enclosing->kind != GCRegionKind::Inline) {
                break;
            }
            region = enclosing->parent;
        }
        return region;
    }

    /// The other half of 5.3, and the one a splice can break: a block's statements are one chain,
    /// so one statement follows one other at most *and* one statement follows it. `checkSyncEdges`
    /// reads the first half back; this is the second. Two statements of one block following the
    /// same statement is a chain that forks, which is no chain: the layout takes one of the two and
    /// the other is reached by nothing, so its work does not happen and a program that prints twice
    /// prints once. The owner of this rule is the finished graph, executed by the seal check at
    /// commit, not the draft's writers.
    ///
    /// What counts is the chain 5.3 and 3.4 make. A node the model lets a pass arrange freely is not
    /// on the chain at all (3.4: a sync block orders its effects, and a node that is pure and cannot
    /// fault has no order to keep), so an edge forking there is nothing to report. A GATE is where a
    /// chain's tail hands its value on (5.3) -- an end of the chain, not a continuation of it --
    /// and a value that several nested blocks hand on is reached from the one tail by one GATE each.
    ///
    /// The splice resolves a statement's place again once the body's code is in
    /// (`resolvePlaceForks`, src/runtime/draft_inline.cpp) and moves a statement that finds itself
    /// following one a second statement follows to the end of the chain growing from it. The move is
    /// refused when that end waits on the statement being moved -- moving it would put the graph on
    /// a cycle of inputs -- and the fork then stands: the splice has no other place to put the
    /// statement, so the graph it would leave is one the model does not have. No such graph is
    /// frozen: this names the statement the two follow, which is where the defect is looked for,
    /// instead of leaving a chain the layout may walk one way and silently drop the other.
    void checkSyncChainForks() {
        if (!graph_.hasSyncEdges()) {
            return;
        }
        std::unordered_map<gc_node_ref_t, std::vector<gc_node_ref_t>> followers;
        for (const GCGraphSyncEdge &edge : graph_.syncEdges()) {
            if (!graph_.containsNodeRef(edge.user) || !graph_.containsNodeRef(edge.pred)) {
                continue; // reported by checkSyncEdges
            }
            const GCNode *user = graph_.node(edge.user);
            const GCNode *pred = graph_.node(edge.pred);
            if (user->kind == GCNodeKind::Dead || pred->kind == GCNodeKind::Dead) {
                continue; // reported by checkSyncEdges
            }
            if ((user->flags & kGCNodeFlagSyncStatement) == 0 ||
                (pred->flags & kGCNodeFlagSyncStatement) == 0) {
                continue; // reported by checkSyncEdges
            }
            if (controlBlockOf(edge.user) != controlBlockOf(edge.pred)) {
                continue; // not one block's chain, which checkSyncEdges does not allow either
            }
            if (user->kind == GCNodeKind::Gate) {
                continue; // the chain's end, not its continuation
            }
            if (isFreelySchedulable(edge.user)) {
                continue; // not a statement of a chain at all (3.4)
            }
            followers[edge.pred].push_back(edge.user);
        }
        for (const auto &[pred, users] : followers) {
            if (users.size() < 2) {
                continue;
            }
            std::string named;
            for (gc_node_ref_t user : users) {
                if (!named.empty()) {
                    named += ", ";
                }
                named += std::to_string(user);
            }
            violations_.push_back(
                std::format(
                    "graph '{}': sync chain forks at node {}: statements {} follow it, and a "
                    "block's statements are one chain (docs/design/effects/edges.cn.md 5.3)",
                    graph_.name(),
                    pred,
                    named));
        }
    }

    /// The rule the design has the seal check enforce (docs/design/effects/edges.cn.md 4.6): an
    /// order edge only orders, and only something that has an order can carry one.
    void checkOrderEdges() {
        if (!graph_.hasOrderEdges()) {
            return;
        }
        for (const GCGraphOrderEdge &edge : graph_.orderEdges()) {
            const auto bad = [&](const std::string &what) {
                violations_.push_back(
                    std::format(
                        "graph '{}': order edge {} -> {} {}",
                        graph_.name(),
                        edge.pred,
                        edge.user,
                        what));
            };
            if (!graph_.containsNodeRef(edge.user) || !graph_.containsNodeRef(edge.pred)) {
                bad("names a node the graph does not have");
                continue;
            }
            const GCNode *user = graph_.node(edge.user);
            const GCNode *pred = graph_.node(edge.pred);
            if (user->kind == GCNodeKind::Dead || pred->kind == GCNodeKind::Dead) {
                bad("names a node a rewrite removed");
                continue;
            }
            if (edge.user == edge.pred) {
                bad("orders a node after itself");
                continue;
            }
            if (isFreelySchedulable(edge.user) && !standsForItsRegion(edge.user)) {
                bad("is on a pure node, which nothing orders");
            }
            if (const uint32_t region = crossedControlRegion(edge.pred, edge.user); region != 0) {
                bad(std::format(
                    "enters region {}; an arm or a part of a loop runs as a whole, so the order it "
                    "has with the code around it is written on the node that owns the region "
                    "(docs/design/effects/edges.cn.md 4.6)",
                    region));
            }
        }
    }

    /// The control region an order edge would cross, 0 when it crosses none. The owner of this rule is
    /// the finished graph, executed by the seal check at commit, not the draft's writers. An order edge orders
    /// two nodes that both run, and an arm or a part of a loop runs as a whole with the node that
    /// owns it: the order such a region has with the code around it is an order on the owner (the
    /// `BRCH` or the `LOOP`), never one that reaches inside the region (docs/design/effects/
    /// edges.cn.md 4.6; the builder puts them there in Builder::rehomeOrderEdges). A region that only
    /// records where code came from -- an inlined body, a tagged block -- is looked through: it
    /// holds no execution of its own (section 5.8).
    uint32_t crossedControlRegion(gc_node_ref_t pred, gc_node_ref_t user) const {
        const std::vector<uint32_t> predChain = controlRegionChain(pred);
        const std::vector<uint32_t> userChain = controlRegionChain(user);
        size_t common                          = 0;
        while (common < predChain.size() && common < userChain.size() &&
               predChain[common] == userChain[common]) {
            ++common;
        }
        if (common == predChain.size() && common == userChain.size()) {
            return 0; // both ends are in the one control region that holds them
        }
        // A node inside a deeper region stands for the owner of the first region the two ends do
        // not share; the owner is where the edge belongs. Any other end is where it is.
        const auto movedInside = [&](gc_node_ref_t end, const std::vector<uint32_t> &chain) {
            if (common >= chain.size()) {
                return false;
            }
            const GCRegion *region = graph_.region(chain[common]);
            return region != nullptr && region->owner != end;
        };
        if (movedInside(pred, predChain)) {
            return predChain[common];
        }
        if (movedInside(user, userChain)) {
            return userChain[common];
        }
        return 0;
    }

    /// The control regions a node runs with, outermost first: the arms and the parts of a loop.
    std::vector<uint32_t> controlRegionChain(gc_node_ref_t ref) const {
        std::vector<uint32_t> chain;
        for (uint32_t region = graph_.nodeRegion(ref); region != 0;) {
            const GCRegion *record = graph_.region(region);
            if (record == nullptr) {
                break;
            }
            if (record->kind == GCRegionKind::Arm || record->kind == GCRegionKind::LoopCond ||
                record->kind == GCRegionKind::LoopBody) {
                chain.push_back(region);
            }
            region = record->parent;
        }
        std::reverse(chain.begin(), chain.end());
        return chain;
    }

    /// Whether a node owns a control region of this graph, and so stands on the outside for what
    /// runs inside it (docs/design/effects/edges.cn.md 4.6): an arm or a part of a loop runs as a
    /// whole with the node that owns it, so the order the region has with the code around it is an
    /// order on that node -- the `BRCH` or the `LOOP` -- and not one that reaches inside the region.
    /// Such a node computes nothing itself, and still carries an order edge, which is why the rule
    /// "a pure node carries none" reads it out here.
    bool standsForItsRegion(gc_node_ref_t ref) const {
        const GCNode *node = graph_.node(ref);
        if (node == nullptr) {
            return false;
        }
        if (node->kind != GCNodeKind::Brch && node->kind != GCNodeKind::Loop) {
            return false;
        }
        for (uint32_t region = 1; region < graph_.regionCount(); ++region) {
            const GCRegion *record = graph_.region(region);
            if (record != nullptr && record->owner == ref) {
                return true;
            }
        }
        return false;
    }

    /// The rule of 4.6's third clause, read off the shared classification so that the seal check
    /// and the passes that write order edges cannot drift: only a node the model lets a pass
    /// arrange freely -- pure *and* unable to fault (design principles 3.4) -- refuses one.
    bool isFreelySchedulable(gc_node_ref_t ref) const {
        // Whether a conversion can fail depends on the types it converts between, and a pass that
        // refines the types (specialization, inlining) can make a conversion that was ordered when
        // it was written one that cannot fail. The order it was given then stays: dropping it is
        // the passes' choice, and an edge on such a node is not a defect of the graph. The owner of
        // this rule is the finished graph, executed by the seal check at commit, not the draft's writers.
        const GCNode *node = graph_.node(ref);
        if (node != nullptr && node->kind == GCNodeKind::Cast) {
            return false;
        }
        return nodeIsFreelySchedulable(graph_, ref);
    }
    /// The equivalences between nodes (the Tabled state): a graph validated as another state has
    /// none; in this one each names two nodes that compute a value of the same type, in the same
    /// region, and the graph says so by its flag.
    void checkWritings() {
        const auto log = graph_.writings();
        if (log.empty() != graph_.executable()) {
            violations_.push_back(
                std::format(
                    "graph '{}': {} equivalences between nodes but {} the flag that says so",
                    graph_.name(),
                    log.empty() ? "has no" : "has",
                    log.empty() ? "has" : "lacks"));
        }
        if (log.empty()) {
            return;
        }
        if (state_ != GraphState::Tabled) {
            violations_.push_back(
                std::format(
                    "graph '{}': values have several writings, which this state does not allow",
                    graph_.name()));
            return;
        }
        for (size_t i = 0; i < log.size(); ++i) {
            const WritingUnion &entry = log[i];
            const auto bad            = [&](const char *what) {
                violations_.push_back(
                    std::format("graph '{}': equivalence {} {}", graph_.name(), i, what));
            };
            if (!graph_.containsNodeRef(entry.a) || !graph_.containsNodeRef(entry.b)) {
                bad("names a node the graph does not have");
                continue;
            }
            const GCNode *a = graph_.node(entry.a);
            const GCNode *b = graph_.node(entry.b);
            if (a->kind == GCNodeKind::Dead || b->kind == GCNodeKind::Dead) {
                bad("names a node a rewrite removed");
            } else if (
                camel::core::type::TypeTable::instance().canonicalIdOf(a->typeId) !=
                camel::core::type::TypeTable::instance().canonicalIdOf(b->typeId)) {
                // By the canonical id, which is the structural identity of the type: two nodes
                // whose types are one type under `equals` (the names in a key erased) have one
                // canonical id, so this is "the same type" and not "the same object".
                bad("is between nodes of different types");
            } else if (graph_.nodeRegion(entry.a) != graph_.nodeRegion(entry.b)) {
                bad("is between nodes of different regions");
            }
        }
    }

    Type *typeOf(gc_node_ref_t ref) const {
        const GCNode *node = graph_.node(ref);
        return node ? node->dataType() : nullptr;
    }

    static std::string show(const Type *type) { return type ? type->toString() : "<none>"; }

    /// Two types a value can pass between: equal, or one refines the other (a tensor of a known
    /// shape and the tensor of the same element type whose shape is not known), or either may
    /// be assigned to the other. Names of parameters and fields do not matter. A node typed `void`
    /// is a value nobody reads (the arms of a branch used as a statement, a discarded result),
    /// which takes whatever reaches it.
    static bool compatible(Type *a, Type *b) {
        if (a->code() == TypeCode::Void || b->code() == TypeCode::Void) {
            return true;
        }
        return a == b || a->equals(b) || a->unify(b) != nullptr || a->assignableFrom(b) ||
               b->assignableFrom(a);
    }

    void report(gc_node_ref_t ref, const std::string &what) {
        violations_.push_back(
            std::format(
                "graph '{}' node {} ({}): {}",
                graph_.name(),
                ref,
                static_cast<int>(graph_.node(ref)->kind),
                what));
    }

    /// The judgement the draft's check makes, on the frozen graph (camel/runtime/cycle.h): every
    /// edge kind takes part, the sync and order ones included. Each of them makes one node run after
    /// another -- a sync edge is a real ordering (docs/design/effects/edges.cn.md, section 1) -- so a
    /// cycle of them is a graph no back end can run, and it is refused here rather than handed on.
    void findCycle() {
        FrozenCycleEdges edges(graph_);
        std::vector<gc_node_ref_t> path;
        const gc_node_ref_t closes = findInputCycle(edges, &path);
        if (closes == kInvalidNodeRef) {
            return;
        }
        std::string chain;
        for (gc_node_ref_t node : path) {
            chain += std::format("{}({}) <- ", node, static_cast<int>(graph_.node(node)->kind));
        }
        report(closes, "lies on a cycle of inputs: " + chain + std::to_string(closes));
    }

    void checkNode(gc_node_ref_t ref, const GCNode &node) {
        const auto norm = graph_.normInputsOf(ref);
        const auto with = graph_.withInputsOf(ref);
        Type *type      = node.dataType();
        switch (node.kind) {
        case GCNodeKind::Copy:
            if (norm.size() == 1 && type && typeOf(norm[0]) && !compatible(typeOf(norm[0]), type)) {
                report(
                    ref,
                    std::format("copies a {} as a {}", show(typeOf(norm[0])), show(type)));
            }
            break;
        case GCNodeKind::Gate:
            if (!norm.empty() && type && typeOf(norm.back()) && !compatible(typeOf(norm.back()), type)) {
                report(
                    ref,
                    std::format(
                        "forwards a {} as a {}",
                        show(typeOf(norm.back())),
                        show(type)));
            }
            break;
        case GCNodeKind::Cast:
            if (norm.size() == 1 && type && typeOf(norm[0]) &&
                !compatible(typeOf(norm[0]), type) &&
                type->castSafetyFrom(typeOf(norm[0])) == CastSafety::Forbidden) {
                report(
                    ref,
                    std::format(
                        "converts a {} to a {}, which is forbidden",
                        show(typeOf(norm[0])),
                        show(type)));
            }
            break;
        case GCNodeKind::Join:
            if (type && type != Type::Void()) {
                for (size_t i = 0; i < with.size(); ++i) {
                    if (typeOf(with[i]) && !compatible(typeOf(with[i]), type)) {
                        report(
                            ref,
                            std::format(
                                "arm {} yields a {}, the JOIN is a {}",
                                i,
                                show(typeOf(with[i])),
                                show(type)));
                    }
                }
            }
            break;
        case GCNodeKind::Accs:
            checkAccess(ref, node, norm);
            break;
        case GCNodeKind::Func:
            checkCall(ref, node, norm, with);
            break;
        default:
            break;
        }
    }

    void checkAccess(gc_node_ref_t ref, const GCNode &node, std::span<const gc_node_ref_t> norm) {
        if (norm.size() != 1 || !node.hasPayload()) {
            return;
        }
        Type *source = typeOf(norm[0]);
        Type *type   = node.dataType();
        if (!source || !type) {
            return;
        }
        const auto *body = graph_.nodeBodyAs<GCAccsBody>(ref);
        if (body->accsKind == GCAccsKind::TupleIndex && source->code() == TypeCode::Tuple) {
            const auto *tuple = static_cast<const TupleType *>(source);
            if (body->value >= tuple->size()) {
                report(ref, std::format("reads element {} of a {}", body->value, show(source)));
            } else if (!compatible(tuple->typeAt(body->value), type)) {
                report(
                    ref,
                    std::format(
                        "element {} of a {} is a {}, the node is a {}",
                        body->value,
                        show(source),
                        show(tuple->typeAt(body->value)),
                        show(type)));
            }
        }
    }

    void checkCall(
        gc_node_ref_t ref, const GCNode &node, std::span<const gc_node_ref_t> norm,
        std::span<const gc_node_ref_t> with) {
        const GCGraph *callee = program_.calleeOf(graph_, ref);
        if (!callee) {
            return;
        }
        const auto normPorts = callee->normPorts();
        const auto withPorts = callee->withPorts();
        const auto compare   = [&](std::span<const gc_node_ref_t> args,
                                 std::span<const gc_node_ref_t> ports,
                                 const char *kind) {
            for (size_t i = 0; i < args.size() && i < ports.size(); ++i) {
                const GCNode *port = callee->node(ports[i]);
                Type *want         = port ? port->dataType() : nullptr;
                Type *have         = typeOf(args[i]);
                if (want && have && !compatible(want, have)) {
                    report(
                        ref,
                        std::format(
                            "passes a {} as {} argument {} of '{}', which takes a {}",
                            show(have),
                            kind,
                            i,
                            callee->name(),
                            show(want)));
                }
            }
        };
        compare(norm, normPorts, "ordinary");
        compare(with, withPorts, "'with'");
        // What the call is: the value it hands on. That is the callee's exit type alone, unless
        // the callee writes captured variables, and then it is the tuple of the exit value and
        // the new version of each of them, in the callee's capture order (callValueTypeOf).
        camel::core::type::Type *wanted = callValueTypeOf(*callee, funcWriteMaskOf(graph_, ref));
        if (wanted != nullptr && node.dataType() != nullptr &&
            !compatible(wanted, node.dataType())) {
            report(
                ref,
                std::format(
                    "calls '{}', whose value is a {}, and is a {}",
                    callee->name(),
                    show(wanted),
                    show(node.dataType())));
        }
    }

    const Program &program_;
    const GCGraph &graph_;
    const GraphState state_;
    std::vector<std::string> violations_;
};

} // namespace

void validateRuntimeGraphEdges(const Program &program, const GCGraph *graph, GraphState state) {
    if (graph == nullptr || !graph->hasNodePayload()) {
        return;
    }
    const std::vector<std::string> violations = EdgeChecker(program, *graph, state).run();
    if (violations.empty()) {
        return;
    }
    std::string text = std::format(
        "Runtime graph '{}' breaks its edge invariants ({} violation(s)):",
        graph->name(),
        violations.size());
    for (size_t i = 0; i < violations.size() && i < 8; ++i) {
        text += "\n  " + violations[i];
    }
    throw std::runtime_error(text);
}

} // namespace camel::runtime
