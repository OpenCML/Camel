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
 * Supported by: National Key Research and Development Program of China
 */

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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace camel::runtime {

using namespace camel::core::type;

namespace {

class EdgeChecker {
  public:
    EdgeChecker(const Program &program, const GCGraph &graph, bool requireAcyclic)
        : program_(program), graph_(graph), requireAcyclic_(requireAcyclic) {}

    std::vector<std::string> run() {
        if (requireAcyclic_) {
            findCycle();
        }
        for (auto it = graph_.nodes().begin(); it != graph_.nodes().end(); ++it) {
            checkNode(it.ref(), **it);
        }
        return std::move(violations_);
    }

  private:
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

    /// Depth-first over the inputs with an explicit stack; a node reached while it is open is
    /// on a cycle.
    void findCycle() {
        const size_t count = graph_.nodeCount();
        std::vector<uint8_t> state(count, 0); // 0 new, 1 open, 2 done
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
                while (top.group < 3) {
                    inputs = top.group == 0   ? graph_.normInputsOf(top.node)
                             : top.group == 1 ? graph_.withInputsOf(top.node)
                                              : graph_.ctrlInputsOf(top.node);
                    if (top.next < inputs.size()) {
                        break;
                    }
                    ++top.group;
                    top.next = 0;
                }
                if (top.group == 3) {
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
    const bool requireAcyclic_;
    std::vector<std::string> violations_;
};

/// CAMEL_GRAPH_EDGES: "record" prints the violations and goes on, "off" skips the checks;
/// anything else (the default) fails the commit.
const char *mode() {
    static const char *value = std::getenv("CAMEL_GRAPH_EDGES");
    return value ? value : "";
}

} // namespace

void validateRuntimeGraphEdges(const Program &program, const GCGraph *graph, bool requireAcyclic) {
    if (graph == nullptr || !graph->hasNodePayload() || std::strcmp(mode(), "off") == 0) {
        return;
    }
    const std::vector<std::string> violations = EdgeChecker(program, *graph, requireAcyclic).run();
    if (violations.empty()) {
        return;
    }
    if (std::strcmp(mode(), "record") == 0) {
        std::string text;
        for (const std::string &line : violations) {
            text += "GRAPH-EDGE " + line + '\n';
        }
        if (const char *log = std::getenv("CAMEL_GRAPH_EDGES_LOG")) {
            if (std::FILE *file = std::fopen(log, "a")) {
                std::fputs(text.c_str(), file);
                std::fclose(file);
                return;
            }
        }
        std::cerr << text;
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
