// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The edge invariants of an encoded graph, checked at every commit (docs/design/graph-store 5.4):
 *
 *  - no cycle: following the inputs of a node (value, `with` and control alike) never comes back
 *    to it. A graph calls itself through other graphs, never through an edge;
 *  - the types agree along the edges: what a node produces has the type its readers need.
 *    The rules are the ones the compiler's own graphs satisfy, node kind by node kind (see
 *    checkNode); a rewrite that breaks one has built a graph that a back end would run on the
 *    wrong slot type.
 *
 * Violations are collected; the commit fails with all of them, named by graph and node.
 */

#include "runtime/graph_build.h"

#include "camel/core/operator_table.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"

#include <format>
#include <stdexcept>
#include <string>
#include <vector>

namespace camel::runtime {

using namespace camel::core::type;

namespace {

class EdgeChecker {
  public:
    EdgeChecker(const Program &program, const GCGraph &graph, GraphState state)
        : program_(program), graph_(graph), state_(state) {}

    std::vector<std::string> run() {
        checkWritings();
        checkOrderEdges();
        checkSyncEdges();
        findCycle();
        for (auto it = graph_.nodes().begin(); it != graph_.nodes().end(); ++it) {
            checkNode(it.ref(), **it);
        }
        return std::move(violations_);
    }

  private:
    /// The other half of the structural rule of 4.6: a sync edge joins two statements one `sync`
    /// block writes directly -- or the block's chain tail to the block's value -- and nothing else.
    /// The builder records that fact on each node it chains (kGCNodeFlagSyncStatement), so the
    /// check is: both ends are such a node, they are not the same node and not a node a rewrite
    /// removed, and one statement is written directly after one other (the tail that reaches the
    /// block's value is the one node that two sync edges leave).
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
                bad("ends on a node no `sync` block wrote");
                continue;
            }
            if ((pred->flags & kGCNodeFlagSyncStatement) == 0) {
                bad("starts at a node no `sync` block wrote");
                continue;
            }
            if (graph_.syncPredsOf(edge.user).size() > 1) {
                bad("is the second predecessor of one statement");
            }
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
            if (isPureNode(edge.user)) {
                bad("is on a pure node, which nothing orders");
            }
        }
    }

    /// A node whose value is entirely determined by its inputs: the kinds that carry no resource
    /// and no writing, and the operators declared `pure` (design principles 3.3). A node of a
    /// kind this does not name (a call, a function, a gate, a loop, a sync node) is an anchor
    /// that may be an effect, and nothing here refuses an order on it.
    bool isPureNode(gc_node_ref_t ref) const {
        const GCNode *node = graph_.node(ref);
        if (node == nullptr) {
            return false;
        }
        switch (node->kind) {
        case GCNodeKind::Data:
        case GCNodeKind::Port:
        case GCNodeKind::Cast:
        case GCNodeKind::Copy:
        case GCNodeKind::Fill:
        case GCNodeKind::Accs:
        case GCNodeKind::Brch:
        case GCNodeKind::Join:
            return true;
        case GCNodeKind::Oper:
            return graph_.nodeBodyAs<GCOperBody>(ref) != nullptr &&
                   camel::core::OperatorTable::instance().isPure(
                       graph_.nodeBodyAs<GCOperBody>(ref)->oper);
        default:
            return false;
        }
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
            } else if (a->typeId != b->typeId) {
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
    /// be assigned to the other. Names of parameters and fields do not matter.
    static bool compatible(Type *a, Type *b) {
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

    /// Depth-first over the edges with an explicit stack; a node reached while it is open is on a
    /// cycle. An order edge takes part: it constrains the order the back ends lay the graph out
    /// in, so a cycle of them is a graph no scheduler can run (docs/design/effects/edges.cn.md
    /// 1.5, 2).
    void findCycle() {
        const size_t count = graph_.nodeCount();
        std::vector<uint8_t> state(count, 0); // 0 new, 1 open, 2 done
        std::vector<gc_node_ref_t> orderPreds;
        struct Frame {
            gc_node_ref_t node;
            uint8_t group;
            size_t next;
        };
        for (auto it = graph_.nodes().begin(); it != graph_.nodes().end(); ++it) {
            const gc_node_ref_t start = it.ref();
            if (state[start] != 0) {
                continue;
            }
            std::vector<Frame> stack{{start, 0, 0}};
            state[start] = 1;
            while (!stack.empty()) {
                Frame &top = stack.back();
                std::span<const gc_node_ref_t> inputs;
                while (top.group < 4) {
                    if (top.group == 3) {
                        orderPreds.clear();
                        for (const GCGraphOrderEdge &edge : graph_.orderPredsOf(top.node)) {
                            orderPreds.push_back(edge.pred);
                        }
                        inputs = std::span<const gc_node_ref_t>(orderPreds);
                    } else {
                        inputs = top.group == 0   ? graph_.normInputsOf(top.node)
                                 : top.group == 1 ? graph_.withInputsOf(top.node)
                                                  : graph_.ctrlInputsOf(top.node);
                    }
                    if (top.next < inputs.size()) {
                        break;
                    }
                    ++top.group;
                    top.next = 0;
                }
                if (top.group == 4) {
                    state[top.node] = 2;
                    stack.pop_back();
                    continue;
                }
                const gc_node_ref_t input = inputs[top.next++];
                if (!graph_.containsNodeRef(input)) {
                    continue; // reported by the reference check
                }
                if (state[input] == 1) {
                    std::string path;
                    bool on = false;
                    for (const Frame &frame : stack) {
                        on = on || frame.node == input;
                        if (on) {
                            path += std::format(
                                "{}({}) <- ",
                                frame.node,
                                static_cast<int>(graph_.node(frame.node)->kind));
                        }
                    }
                    report(input, "lies on a cycle of inputs: " + path + std::to_string(input));
                    return;
                }
                if (state[input] == 0) {
                    state[input] = 1;
                    stack.push_back({input, 0, 0});
                }
            }
        }
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
        const auto *calleeType = callee->funcType();
        if (calleeType && calleeType->hasExitType() && node.dataType() &&
            !compatible(calleeType->exitType(), node.dataType())) {
            report(
                ref,
                std::format(
                    "calls '{}', which returns a {}, and is a {}",
                    callee->name(),
                    show(calleeType->exitType()),
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
