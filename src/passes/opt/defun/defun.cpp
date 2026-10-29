// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * std::defunctionalize (design principles 9.1, 9.2).
 *
 * A function value in this graph model is already a tag with an environment: a FUNC node names
 * the function (the tag) and a FILL of kind FunctionClosure adds the values it captures (the
 * environment); a CALL node is the dispatch. What is left is to make the dispatch static wherever
 * the tag is known, so that the call through a value becomes a direct call of the function the
 * value is:
 *
 *  - a call through a parameter whose call site passes a function the program can name: the
 *    parameter is bound to that function in a copy of the callee (specialization), and the copy's
 *    call of it is a direct call;
 *  - a call of a closure built in the same graph: the callee's graph is lifted so the captures
 *    become trailing arguments, and the call becomes a direct call of the lifted graph.
 *
 * The rewriting is the call-rewrite engine's, with only the two stages that make a known tag
 * direct: specialization of function-valued arguments and devirtualization of the calls that
 * follow from it. Constant specialization and inlining stay off (this pass is not an optimizer:
 * it changes which calls are known, not how much code there is), and there is no growth budget:
 * one copy is made per (callee, function) pair, the table answers a second need with the same
 * copy, and nothing else grows.
 *
 * A value this pass cannot see through is left as it is, and the graph that still calls it is
 * named by a warning: a function value that is still a parameter at a call site the program fills
 * with more than one function (or with a closure), a closure whose captured values are not known
 * where it is called, or a function value that comes from outside the program (a container, an
 * argument of the entry function). Guessing which function such a call reaches would change which
 * effects run.
 */

#include "defun.h"

#include "../inline/engine.h"
#include "camel/core/rtdata/func.h"
#include "camel/execute/pass/warnings.h"
#include "camel/runtime/reachable.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <limits>
#include <map>
#include <ostream>
#include <set>
#include <span>
#include <string>
#include <vector>

using camel::runtime::GCFillBody;
using camel::runtime::GCFillKind;
using camel::runtime::GCGraph;
using camel::runtime::GCNodeKind;
using camel::runtime::Program;

namespace {

/// Why one graph still holds a dispatch this pass could not make static, gathered once per graph:
/// the warning names the graph, not every node in it.
struct Unresolved {
    size_t indirectCalls = 0;
    size_t closures      = 0;
    /// The distinct explanations, in the order they were first met (stable output).
    std::vector<std::string> reasons;

    void note(const std::string &reason) {
        if (std::ranges::find(reasons, reason) == reasons.end()) {
            reasons.push_back(reason);
        }
    }
};

/// The name of the function a value stands for, when the value is a static `Function`, and "" when
/// it is not one this pass can name.
std::string functionNameOf(const GCGraph &graph, camel::runtime::gc_node_ref_t ref) {
    const auto *header = graph.node(ref);
    if (header == nullptr || header->kind != GCNodeKind::Data || header->dataIndex >= 0) {
        return {};
    }
    const auto *statics = graph.staticDataType();
    if (statics == nullptr || graph.staticArea() == nullptr) {
        return {};
    }
    const size_t index = static_cast<size_t>(-header->dataIndex);
    if (index >= statics->size() || statics->typeAt(index) == nullptr ||
        statics->typeAt(index)->code() != camel::core::type::TypeCode::Function) {
        return {};
    }
    const auto *function =
        camel::core::rtdata::fromSlot<::Function *>(graph.staticArea()->get<slot_t>(index));
    const GCGraph *target = function != nullptr ? function->graph() : nullptr;
    return target != nullptr ? target->name() : std::string("<a function>");
}

/// What one value at a call site is, in the words a user can act on.
std::string describeValue(const GCGraph &graph, camel::runtime::gc_node_ref_t ref) {
    const auto *header = graph.node(ref);
    if (header == nullptr) {
        return "a value the graph does not hold";
    }
    if (header->kind == GCNodeKind::Fill) {
        const auto *fill = graph.nodeBodyAs<GCFillBody>(ref);
        if (fill != nullptr && fill->fillKind == GCFillKind::FunctionClosure) {
            // A closure's norm input is the function it is a closure of; its with inputs are the
            // values it captures.
            const auto inputs = graph.normInputsOf(ref);
            const std::string name =
                inputs.empty() ? std::string() : functionNameOf(graph, inputs.front());
            return std::format(
                "a closure over {} captured value(s){}",
                fill->slots().size(),
                name.empty() ? "" : " of '" + name + "'");
        }
    }
    if (header->kind == GCNodeKind::Port) {
        return "another parameter";
    }
    const std::string name = functionNameOf(graph, ref);
    if (!name.empty()) {
        return "a function value ('" + name + "')";
    }
    return "a value only known when the program runs";
}

/// Every graph of the program a FUNC node calls, with the function values its call sites pass.
std::vector<std::string> callSiteValues(
    const Program &program, const std::vector<GCGraph *> &reachable, const GCGraph &callee,
    size_t formalIndex) {
    std::vector<std::string> values;
    const auto note = [&](const std::string &value) {
        if (std::ranges::find(values, value) == values.end()) {
            values.push_back(value);
        }
    };
    for (GCGraph *caller : reachable) {
        if (caller == nullptr) {
            continue;
        }
        for (auto it = caller->nodes().begin(); it != caller->nodes().end(); ++it) {
            if (program.calleeOf(*caller, it.ref()) != &callee) {
                continue;
            }
            // A call's actual arguments are its norm inputs then its with inputs, in the order of
            // the callee's norm then with ports (the order every consumer of a call reads).
            std::vector<camel::runtime::gc_node_ref_t> actuals(
                caller->normInputsOf(it.ref()).begin(), caller->normInputsOf(it.ref()).end());
            const auto with = caller->withInputsOf(it.ref());
            actuals.insert(actuals.end(), with.begin(), with.end());
            note(formalIndex < actuals.size() ? describeValue(*caller, actuals[formalIndex])
                                              : "no argument at all");
        }
    }
    return values;
}

/// The index of `port` among the formals of `graph`: norm ports first, then with ports.
size_t formalIndexOf(const GCGraph &graph, camel::runtime::gc_node_ref_t port) {
    const auto norm = graph.normPorts();
    for (size_t i = 0; i < norm.size(); ++i) {
        if (norm[i] == port) {
            return i;
        }
    }
    const auto with = graph.withPorts();
    for (size_t i = 0; i < with.size(); ++i) {
        if (with[i] == port) {
            return norm.size() + i;
        }
    }
    return norm.size() + with.size();
}

/// What the callee of `call` is, in the words a user can act on.
std::string describeCallee(
    const Program &program, const std::vector<GCGraph *> &reachable, const GCGraph &graph,
    camel::runtime::gc_node_ref_t call) {
    const auto callee = graph.withInputsOf(call);
    if (callee.empty()) {
        return "the call names no function value";
    }
    const auto *header = graph.node(callee.front());
    if (header == nullptr) {
        return "the call names no function value";
    }
    if (header->kind == GCNodeKind::Port) {
        // The tag is a parameter here, and what it can be is what the program passes at that
        // parameter: say so, rather than that this pass could not think of a function.
        const size_t formal = formalIndexOf(graph, callee.front());
        std::string passed;
        for (const std::string &value : callSiteValues(program, reachable, graph, formal)) {
            passed += passed.empty() ? value : ", " + value;
        }
        size_t callers = 0;
        for (GCGraph *caller : reachable) {
            if (caller == nullptr) {
                continue;
            }
            for (auto it = caller->nodes().begin(); it != caller->nodes().end(); ++it) {
                callers += program.calleeOf(*caller, it.ref()) == &graph ? 1 : 0;
            }
        }
        return std::format(
            "the callee is parameter {} of '{}', filled at {} call site(s) with: {}",
            formal,
            graph.name(),
            callers,
            passed.empty() ? "nothing this pass can see" : passed);
    }
    if (header->kind == GCNodeKind::Func) {
        return {}; // A FUNC node is a direct call; nothing to explain.
    }
    return "the callee is " + describeValue(graph, callee.front());
}

/// The graphs a call reaches when the program runs: the entry and, for each of them, the graphs
/// its FUNC nodes call and the two parts of its loops. A graph the program only holds as a static
/// function value, or only writes inside another graph (a lambda nothing calls any more), is not
/// one of them: a call left in it is not a call this lowering failed to make direct.
std::set<const GCGraph *> runningGraphs(const Program &program) {
    std::set<const GCGraph *> running;
    std::vector<const GCGraph *> work{program.root()};
    while (!work.empty()) {
        const GCGraph *graph = work.back();
        work.pop_back();
        if (graph == nullptr || !running.insert(graph).second) {
            continue;
        }
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            const auto *node = *it;
            if (node == nullptr) {
                continue;
            }
            if (node->kind == GCNodeKind::Func) {
                work.push_back(program.calleeOf(*graph, it.ref()));
            } else if (node->kind == GCNodeKind::Loop) {
                work.push_back(program.loopCondOf(*graph, it.ref()));
                work.push_back(program.loopBodyOf(*graph, it.ref()));
            }
        }
    }
    return running;
}

/// Every graph that runs and still holds a dispatch, with what is left in it.
std::map<std::string, Unresolved> unresolvedByGraph(const Program &program) {
    std::map<std::string, Unresolved> result;
    const std::set<const GCGraph *> running          = runningGraphs(program);
    const std::vector<GCGraph *> reachable           = camel::runtime::collectReachableGraphs(program);
    for (GCGraph *graph : reachable) {
        if (graph == nullptr || !running.contains(graph)) {
            continue;
        }
        Unresolved unresolved;
        // A closure that feeds a call is worth reporting; one that nothing reads any more is not.
        // The pass turns the call through a closure built here into a direct call and leaves the
        // node that built it for the dead-code pass, so counting every closure node would report a
        // dispatch that no longer exists.
        std::set<camel::runtime::gc_node_ref_t> feeding;
        const auto noteFeeding = [&](std::span<const camel::runtime::gc_node_ref_t> inputs) {
            for (const auto ref : inputs) {
                const auto *header = graph->node(ref);
                if (header == nullptr || header->kind != GCNodeKind::Fill) {
                    continue;
                }
                const auto *fill = graph->nodeBodyAs<GCFillBody>(ref);
                if (fill != nullptr && fill->fillKind == GCFillKind::FunctionClosure) {
                    feeding.insert(ref);
                }
            }
        };
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            const auto *node = *it;
            if (node == nullptr) {
                continue;
            }
            // A function value a call or an operator reads is one that will be called, here or
            // inside the operator (a higher-order operator such as `map` calls what it is given).
            switch (node->kind) {
            case GCNodeKind::Call:
                noteFeeding(graph->withInputsOf(it.ref()));
                break;
            case GCNodeKind::Func:
            case GCNodeKind::Oper:
                noteFeeding(graph->normInputsOf(it.ref()));
                noteFeeding(graph->withInputsOf(it.ref()));
                break;
            default:
                break;
            }
        }
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            const auto ref   = it.ref();
            const auto *node = *it;
            if (node == nullptr) {
                continue;
            }
            if (node->kind == GCNodeKind::Call) {
                ++unresolved.indirectCalls;
                const std::string reason = describeCallee(program, reachable, *graph, ref);
                if (!reason.empty()) {
                    unresolved.note(reason);
                }
            } else if (node->kind == GCNodeKind::Fill && feeding.contains(ref)) {
                const auto *fill = graph->nodeBodyAs<GCFillBody>(ref);
                ++unresolved.closures;
                const auto inputs = graph->normInputsOf(ref);
                const std::string name =
                    inputs.empty() ? std::string() : functionNameOf(*graph, inputs.front());
                unresolved.note(std::format(
                    "a closure written here over {} captured value(s){}",
                    fill->slots().size(),
                    name.empty() ? "" : " of '" + name + "'"));
            }
        }
        if (unresolved.indirectCalls != 0 || unresolved.closures != 0) {
            result[graph->name()] = std::move(unresolved);
        }
    }
    return result;
}

} // namespace

const Program *DefunctionalizeRewritePass::apply(const Program *program, std::ostream &os) {
    if (program == nullptr) {
        return nullptr;
    }

    OptimizeRewriteConfig config{};
    config.enableDevirtualization     = true;
    config.enableSpecialization       = true;
    config.specializeOnFunctionValues = true;
    // Not an optimizer: it does not copy a callee into its caller, and it does not unroll a
    // recursion on constant arguments. Only the (callee, function) copies specialization makes
    // for a bound function value grow the program, one per pair.
    config.enableInlining = false;
    config.growthBudget.nodes =
        std::numeric_limits<size_t>::max(); // no budget: this lowering always completes
    config.growthBudget.passName = "std::defunctionalize";

    const Program *lowered = applyOptimizeRewritePass(context_, program, os, config);
    if (lowered == nullptr) {
        return nullptr;
    }

    // Everything above leaves an unsupported form as written. Say which graph still holds one and
    // why: a pass that silently gives up looks exactly like one that found nothing to do.
    if (context_) {
        for (const auto &[name, unresolved] : unresolvedByGraph(*lowered)) {
            std::string reasons;
            for (const std::string &reason : unresolved.reasons) {
                reasons += reasons.empty() ? reason : "; " + reason;
            }
            raisePassWarning(
                *context_,
                camel::core::error::RuntimeDiag::DefunctionalizationIncomplete,
                camel::source::kInvalidOriginId,
                std::format(
                    "'{}' still holds {} indirect call(s) and {} closure(s): {}",
                    name,
                    unresolved.indirectCalls,
                    unresolved.closures,
                    reasons.empty() ? "the form is not lowered" : reasons));
        }
    }
    return lowered;
}
