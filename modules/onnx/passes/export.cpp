/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Created: Oct. 04, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The onnx::export translation pass (see export.h).
 */

#include "export.h"

#include "../../tensor/interop.h"
#include "../emitter.h"
#include "../exporter.h"

#include "camel/core/error/diagnostics.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/source/ids.h"
#include "camel/core/source/manager.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/execute/entry_args.h"
#include "camel/execute/graph_runtime_support.h"
#include "camel/execute/pass/params.h"
#include "camel/passes/opt/config.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/reachable.h"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace camel::onnx::passes {

using camel::core::error::RuntimeDiag;
using camel::core::type::TypeCode;
using camel::runtime::gc_node_ref_t;
using camel::runtime::GCGraph;
using camel::runtime::GCNodeKind;

namespace {

constexpr const char *kPassName   = "onnx::export";
constexpr const char *kRequestUri = "onnx:export_model";

/// Why the pass does not translate, and the node it is about. A refusal that is about no
/// particular node leaves `origin` invalid; apply() then reports it at the export_model call
/// being translated or, before any call is known, at the entry function (see locate()).
struct Refusal {
    uint64_t origin;
    std::string message;
};

/// One `onnx.export_model(...)` call of the entry graph, with its arguments read as constants.
struct Request {
    uint64_t origin = camel::source::kInvalidOriginId; // the call, for refusals about it
    ::Function *fn  = nullptr; // constant function value (the closure template when built by FILL)
    GCGraph *graph  = nullptr;
    std::vector<Example> captures;
    std::vector<Example> examples;
    std::string path;
    ExportOptions options;
};

/// The source origin of `ref`, or of the nearest input that has one (calls synthesized by the
/// compiler carry none; the function they call does).
uint64_t originNear(const GCGraph *g, gc_node_ref_t ref) {
    if (const uint64_t origin = g->nodeOrigin(ref); origin != camel::source::kInvalidOriginId) {
        return origin;
    }
    for (const auto inputs : {g->withInputsOf(ref), g->normInputsOf(ref)}) {
        for (gc_node_ref_t input : inputs) {
            if (const uint64_t origin = g->nodeOrigin(input);
                origin != camel::source::kInvalidOriginId) {
                return origin;
            }
        }
    }
    return camel::source::kInvalidOriginId;
}

/// The value of `ref` when it is a constant of the graph (a DATA node in the static area).
std::optional<Example> constantOf(const GCGraph *g, gc_node_ref_t ref) {
    const auto *n = g->node(ref);
    if (n->kind != GCNodeKind::Data || n->dataIndex >= 0) {
        return std::nullopt;
    }
    const auto index     = static_cast<size_t>(-n->dataIndex);
    core::type::Type *ty = g->staticDataType()->typeAt(index);
    return Example{g->staticArea()->get<slot_t>(index), ty ? ty : n->dataType()};
}

std::string describeNode(const GCGraph *g, gc_node_ref_t ref) {
    const auto *n = g->node(ref);
    if (n->kind == GCNodeKind::Oper) {
        return std::format(
            "a value computed by '{}'",
            g->nodeBodyAs<camel::runtime::GCOperBody>(ref)->uri());
    }
    if (n->kind == GCNodeKind::Func || n->kind == GCNodeKind::Call) {
        return "the result of a call";
    }
    return "a value computed at run time";
}

/// Where to report argument `arg` of the request `node`: the argument when it has a source
/// position, the call otherwise.
uint64_t argOrigin(const GCGraph *g, gc_node_ref_t node, gc_node_ref_t arg) {
    const uint64_t origin = originNear(g, arg);
    return origin != camel::source::kInvalidOriginId ? origin : originNear(g, node);
}

/// Reads argument `index` of the request `node` as a constant; refuses when the preceding
/// passes did not reduce it to one. The pass does not execute the program to obtain it.
Example constantArg(const GCGraph *g, gc_node_ref_t node, size_t index, std::string_view what) {
    const gc_node_ref_t arg = g->normInputsOf(node)[index];
    if (auto value = constantOf(g, arg)) {
        return *value;
    }
    throw Refusal{
        argOrigin(g, node, arg),
        std::format(
            "the {} of onnx.export_model is {}, not a constant of the graph; onnx::export "
            "does not run the program: run std::opt before it so that it folds, or "
            "build it from constants only",
            what,
            describeNode(g, arg))};
}

/// The exported function: a constant function value, or a closure built (FILL) from a
/// constant function and constant captures.
void readFunction(const GCGraph *g, gc_node_ref_t node, Request &request) {
    const gc_node_ref_t arg = g->normInputsOf(node)[0];
    const auto *n           = g->node(arg);
    if (n->kind == GCNodeKind::Fill && n->dataType() &&
        n->dataType()->code() == TypeCode::Function) {
        const auto templ = constantOf(g, g->normInputsOf(arg).front());
        if (!templ) {
            throw Refusal{
                argOrigin(g, node, arg),
                "the function argument of onnx.export_model is not a constant of the graph; "
                "run std::opt before onnx::export"};
        }
        auto *fn         = rtdata::fromSlot<::Function *>(templ->value);
        request.fn       = fn;
        request.graph    = fn->graph();
        const auto slots = g->nodeBodyAs<camel::runtime::GCFillBody>(arg)->slots();
        const auto refs  = g->withInputsOf(arg);
        request.captures.assign(
            request.graph ? request.graph->closureNodes().size() : 0,
            Example{NullSlot, nullptr});
        for (size_t k = 0; k < refs.size() && k < slots.size(); ++k) {
            const auto value = constantOf(g, refs[k]);
            const auto slot  = static_cast<size_t>(slots[k]);
            if (!value || slot >= request.captures.size()) {
                throw Refusal{
                    argOrigin(g, node, refs[k]),
                    std::format(
                        "the function passed to onnx.export_model captures {}, not a constant "
                        "of the graph; onnx::export does not run the "
                        "program: run std::opt before it so that it folds, or pass "
                        "the value as a function argument",
                        describeNode(g, refs[k]))};
            }
            request.captures[slot] = *value;
        }
        for (const Example &capture : request.captures) {
            if (!capture.type) {
                throw Refusal{
                    argOrigin(g, node, arg),
                    "the function passed to onnx.export_model has a capture that is not bound "
                    "in the graph"};
            }
        }
        return;
    }
    const Example value = constantArg(g, node, 0, "function argument");
    auto *fn            = rtdata::fromSlot<::Function *>(value.value);
    request.fn          = fn;
    request.graph       = fn ? fn->graph() : nullptr;
}

Request readRequest(const GCGraph *g, gc_node_ref_t node, const std::string &out) {
    Request request;
    request.origin  = originNear(g, node);
    const auto args = g->normInputsOf(node);
    readFunction(g, node, request);
    if (!request.graph) {
        throw Refusal{originNear(g, node), "onnx.export_model expects a function value"};
    }

    // `example` is the argument of a one-parameter function, or a tuple with one example per
    // parameter (as the export_model operator reads it).
    const Example example = constantArg(g, node, 1, "example argument");
    if (request.graph->normPorts().size() == 1 || example.type->code() != TypeCode::Tuple) {
        request.examples.push_back(example);
    } else {
        auto *tupleType = static_cast<core::type::TupleType *>(example.type);
        auto *tuple     = rtdata::fromSlot<::Tuple *>(example.value);
        for (size_t i = 0; i < tupleType->size(); ++i) {
            request.examples.push_back({tuple->get<slot_t>(i), tupleType->typeAt(i)});
        }
    }

    if (!out.empty()) {
        request.path = out;
    } else if (const auto path = constantOf(g, args[2])) {
        request.path = rtdata::fromSlot<::String *>(path->value)->toString();
    } else {
        throw Refusal{
            argOrigin(g, node, args[2]),
            std::format(
                "the output path of onnx.export_model is {}, not a constant of the graph; "
                "name the model file with the parameter out of the pass "
                "(onnx::export(out=\"model.onnx\")), or write the path as a string literal",
                describeNode(g, args[2]))};
    }
    if (args.size() > 3) {
        const Example axes = constantArg(g, node, 3, "dynamic_axes argument");
        request.options.dynamicAxes =
            tensor::parseIntArray(rtdata::fromSlot<::Array *>(axes.value), axes.type);
    }
    if (args.size() > 4) {
        const Example target   = constantArg(g, node, 4, "target argument");
        request.options.target = rtdata::fromSlot<::String *>(target.value)->toString();
    }
    return request;
}

/// The branch of `g` in whose arm `node` sits (the innermost one), when the condition of that
/// branch is not a constant of the graph: whether the arm runs is then decided at run time.
std::optional<gc_node_ref_t> runtimeBranchOver(GCGraph *g, gc_node_ref_t node) {
    std::optional<gc_node_ref_t> found;
    size_t foundSize = 0;
    for (auto it = g->nodes().begin(); it != g->nodes().end(); ++it) {
        const auto *n = *it;
        if (!n || n->kind != GCNodeKind::Brch) {
            continue;
        }
        const auto cond = g->normInputsOf(it.ref());
        if (!cond.empty() && constantOf(g, cond.front())) {
            continue;
        }
        const size_t arms = g->ctrlOutputsOf(it.ref()).size();
        for (size_t arm = 0; arm < arms; ++arm) {
            const auto region = camel::execute::collectRuntimeBranchArmRegion(g, it.ref(), arm);
            const auto &nodes = region.topoIndices;
            if (std::find(nodes.begin(), nodes.end(), node) != nodes.end() &&
                (!found || nodes.size() < foundSize)) {
                found     = it.ref();
                foundSize = nodes.size();
            }
        }
    }
    return found;
}

/// "file:line:column" of `origin`, or nothing when it has no source position.
std::string locationOf(const core::context::Context &ctx, uint64_t origin) {
    const auto source = ctx.sourceContext();
    if (!source || origin == camel::source::kInvalidOriginId) {
        return {};
    }
    const auto *file = source->fileForOrigin(origin);
    if (!file) {
        return {};
    }
    const auto range = source->resolveOrigin(origin);
    return std::format("{}:{}:{}", file->path, range.start.line + 1, range.start.character + 1);
}

/// Reads the request `node` of the entry graph. A request the pass cannot read because it is
/// made in the arm of a branch decided at run time is refused at that branch: what the arm
/// computes is not a constant of the graph (it waits for the branch), and that, not the
/// argument the reading stumbled on, is what the program has to change.
Request readEntryRequest(
    const core::context::Context &ctx, GCGraph *g, gc_node_ref_t node, const std::string &out) {
    // Whether the arm runs is decided at run time: the pass cannot tell whether the request is
    // made, whatever its arguments are. Refuse before reading the arguments, which may well be
    // constants of the arm and would otherwise look like a request made unconditionally.
    const auto brch = runtimeBranchOver(g, node);
    if (!brch) {
        return readRequest(g, node, out);
    }
    const auto cond            = g->normInputsOf(*brch);
    const std::string location = locationOf(ctx, originNear(g, node));
    uint64_t origin            = originNear(g, *brch);
    if (origin == camel::source::kInvalidOriginId) {
        origin = originNear(g, node);
    }
    throw Refusal{
        origin,
        std::format(
            "the onnx.export_model call{} is made in an arm of this branch, and the "
            "condition of the branch{} is not known at compile time; onnx::export does not "
            "run the program, so it cannot tell whether the request is made, and what the "
            "arm computes is not a constant of the graph; write the export_model call "
            "outside the branch, or make the condition a constant",
            location.empty() ? std::string() : " at " + location,
            cond.empty() ? std::string() : std::format(" ({})", describeNode(g, cond.front())))};
}

/// The function a call node of `g` calls, when it is statically known.
const GCGraph *calleeGraphOf(const GCGraph *g, gc_node_ref_t ref) {
    if (g->node(ref)->kind == GCNodeKind::Func) {
        return g->directCalleeGraphOf(ref);
    }
    if (const auto with = g->withInputsOf(ref); !with.empty()) {
        if (const auto fn = constantOf(g, with.front());
            fn && fn->type && fn->type->code() == TypeCode::Function) {
            return rtdata::fromSlot<::Function *>(fn->value)->graph();
        }
    }
    return nullptr;
}

/// A source-level name of the function a call node of `g` calls, when it is statically known.
std::string calleeNameOf(const GCGraph *g, gc_node_ref_t ref) {
    const GCGraph *callee = calleeGraphOf(g, ref);
    if (!callee || callee->name().empty() || callee->name().starts_with("__")) {
        return {};
    }
    return callee->name().substr(0, callee->name().find('$'));
}

/// The definition of the function `g` was compiled from, or the first of its nodes that has a
/// source position (a graph a pass cloned keeps its nodes' origins but not its own).
uint64_t graphOriginOf(core::context::Context &ctx, const GCGraph *g) {
    if (g == nullptr) {
        return camel::source::kInvalidOriginId;
    }
    if (const auto source = ctx.sourceContext()) {
        const auto origin = source->debugMap().graphOrigin(g->stableId());
        if (origin != camel::source::kInvalidOriginId) {
            return origin;
        }
    }
    for (auto it = g->nodes().begin(); it != g->nodes().end(); ++it) {
        if (*it == nullptr) {
            continue;
        }
        if (const uint64_t origin = g->nodeOrigin(it.ref());
            origin != camel::source::kInvalidOriginId) {
            return origin;
        }
    }
    return camel::source::kInvalidOriginId;
}

/// Where a refusal about the program as a whole is reported: the entry function, that is the
/// function the entry graph calls (`main`) while it is still a call, the entry graph itself
/// once `main` has been inlined into it.
uint64_t entryOriginOf(core::context::Context &ctx, const GCGraph *root) {
    for (auto it = root->nodes().begin(); it != root->nodes().end(); ++it) {
        const auto *n = *it;
        if (n == nullptr || (n->kind != GCNodeKind::Func && n->kind != GCNodeKind::Call)) {
            continue;
        }
        const uint64_t origin = graphOriginOf(ctx, calleeGraphOf(root, it.ref()));
        if (origin != camel::source::kInvalidOriginId) {
            return origin;
        }
    }
    return graphOriginOf(ctx, root);
}

/// Whether node `ref` of `g` is the export request (the operator kRequestUri).
bool isRequest(const GCGraph *g, gc_node_ref_t ref) {
    static const camel::core::OperId request =
        camel::core::OperatorTable::instance().intern(kRequestUri);
    const auto *n = g->node(ref);
    return n && n->kind == GCNodeKind::Oper &&
           g->nodeBodyAs<camel::runtime::GCOperBody>(ref)->oper == request;
}

bool holdsRequest(const GCGraph *g) {
    for (auto it = g->nodes().begin(); it != g->nodes().end(); ++it) {
        if (isRequest(g, it.ref())) {
            return true;
        }
    }
    return false;
}

/// Whether an export request is written somewhere in the program (in a function the entry
/// graph reaches), though none is visible in the entry graph itself.
bool requestIsReachable(GCGraph *root) {
    for (GCGraph *g : camel::runtime::collectReachableGraphs(root)) {
        if (g != nullptr && g != root && holdsRequest(g)) {
            return true;
        }
    }
    return false;
}

/// Whether `g` holds an export request or reaches, through calls of statically known
/// functions, a graph that does.
bool leadsToRequest(const GCGraph *g, std::unordered_set<const GCGraph *> &seen) {
    if (g == nullptr || !seen.insert(g).second) {
        return false;
    }
    if (holdsRequest(g)) {
        return true;
    }
    for (auto it = g->nodes().begin(); it != g->nodes().end(); ++it) {
        const auto *n = *it;
        if (n && (n->kind == GCNodeKind::Func || n->kind == GCNodeKind::Call) &&
            leadsToRequest(calleeGraphOf(g, it.ref()), seen)) {
            return true;
        }
    }
    return false;
}

/// The call of the entry graph behind which the export request sits, when the calls from
/// there to the request are all calls of statically known functions.
std::optional<gc_node_ref_t> callLeadingToRequest(const GCGraph *root) {
    for (auto it = root->nodes().begin(); it != root->nodes().end(); ++it) {
        const auto *n = *it;
        if (!n || (n->kind != GCNodeKind::Func && n->kind != GCNodeKind::Call)) {
            continue;
        }
        std::unordered_set<const GCGraph *> seen{root};
        if (leadsToRequest(calleeGraphOf(root, it.ref()), seen)) {
            return it.ref();
        }
    }
    return std::nullopt;
}

/// What to tell about a call of the entry graph that hides the export request: why the
/// inliner left it, when a pass that inlines ran (it records its decisions, see InlineReport),
/// and that no such pass ran otherwise.
std::string
explainCallLeft(const core::context::Context &ctx, const GCGraph *root, gc_node_ref_t call) {
    const auto report =
        std::static_pointer_cast<const InlineReport>(ctx.passFact(kInlineReportFactKey));
    if (!report) {
        return "run std::opt before onnx::export (after std::macro when the program "
               "uses macros such as grad)";
    }
    const GCGraph *callee    = calleeGraphOf(root, call);
    const std::string shown  = calleeNameOf(root, call);
    const std::string name   = shown.empty() ? "the called function" : "'" + shown + "'";
    const InlineDeclined *no = callee ? report->find(root->name(), callee->name()) : nullptr;
    const std::string budget = std::format(
        "{} nodes, {}",
        report->growthBudget,
        report->budgetFromEnvironment ? std::format("set by {}", kGrowthBudgetEnvVar)
                                      : std::string("the default"));
    const std::string raise = std::format(
        "raise the budget by setting the environment variable {} to a larger number of nodes",
        kGrowthBudgetEnvVar);
    if (no && no->why == InlineDecline::GrowthBudget) {
        return std::format(
            "the inliner ran before onnx::export and left this call because its growth budget "
            "was exhausted: the budget is {} nodes ({}), {} were spent, and inlining {} copies {} "
            "more; {}",
            report->growthBudget,
            report->budgetFromEnvironment ? std::format("set by {}", kGrowthBudgetEnvVar)
                                          : std::string("the default"),
            report->growthSpent,
            name,
            no->calleeNodes,
            raise);
    }
    std::string why;
    if (!no) {
        why = "the inliner ran before onnx::export and left this call";
    } else {
        switch (no->why) {
        case InlineDecline::OwnerSize:
            why = std::format(
                "the inliner ran before onnx::export and left this call because inlining {} "
                "({} nodes) would make the entry graph ({} nodes) larger than the {} nodes a "
                "graph may reach by inlining",
                name,
                no->calleeNodes,
                no->callerNodes,
                report->maxOwnerNodes);
            break;
        case InlineDecline::Recursive:
            why = std::format(
                "the inliner ran before onnx::export and left this call because {} is "
                "recursive, and a recursive function is not copied into its callers; write "
                "the export_model call outside the recursion",
                name);
            break;
        case InlineDecline::ReachesRecursion:
            why = std::format(
                "the inliner ran before onnx::export and left this call because {} calls, "
                "directly or through other functions, a recursive function (a loop), and "
                "such a function is inlined only while it is small (at most {} nodes besides "
                "constants and parameters; this one has {}); keep the function that makes "
                "the export_model call small by moving the rest of its work into a function "
                "it calls",
                name,
                report->smallNodes,
                no->calleeNodes);
            break;
        case InlineDecline::Duplication:
            why = std::format(
                "the inliner ran before onnx::export and left this call because {} is "
                "called from {} places and is too large ({} nodes) to be copied to each; "
                "write the export_model call in a function that is called once",
                name,
                no->callSites,
                no->calleeNodes);
            break;
        default:
            why = "the inliner ran before onnx::export and left this call";
            break;
        }
    }
    if (report->growthSpent >= report->growthBudget) {
        // Not the reason recorded for this call, but nothing is specialized or inlined once
        // the budget is spent, so it may be what kept the call from becoming inlinable.
        why += std::format(
            "; the growth budget of the inliner was also exhausted ({}, {} spent): {}",
            budget,
            report->growthSpent,
            raise);
    }
    return why;
}

/// The export requests of the entry graph. None, in a program that makes none anywhere, is
/// the other way to export: the entry graph itself is translated (see apply). A program that
/// does make a request, in a function its entry graph still calls (nothing was inlined), shows
/// none here: that is a missing simplification.
std::vector<Request>
collectRequests(const core::context::Context &ctx, GCGraph *root, const std::string &out) {
    std::vector<Request> requests;
    std::optional<gc_node_ref_t> firstCall;
    for (auto it = root->nodes().begin(); it != root->nodes().end(); ++it) {
        const auto *n = *it;
        if (!n) {
            continue;
        }
        if (isRequest(root, it.ref())) {
            requests.push_back(readEntryRequest(ctx, root, it.ref(), out));
        } else if ((n->kind == GCNodeKind::Func || n->kind == GCNodeKind::Call) && !firstCall) {
            firstCall = it.ref();
        }
    }
    if (!requests.empty()) {
        return requests;
    }
    if (firstCall && requestIsReachable(root)) {
        // The call behind which the request sits, when that is known; the first call else.
        const gc_node_ref_t call = callLeadingToRequest(root).value_or(*firstCall);
        const std::string callee = calleeNameOf(root, call);
        throw Refusal{
            originNear(root, call),
            std::format(
                "the entry graph still contains a call{} that was not inlined, so no "
                "onnx.export_model(fn, example, path) request is visible in it; {}",
                callee.empty() ? std::string() : std::format(" to '{}'", callee),
                explainCallLeft(ctx, root, call))};
    }
    return requests;
}

/// A macro operator (grad, value_and_grad, ...) left in the function means std::macro did
/// not run: its expansion is a graph rewrite, which this pass does not perform.
void refuseUnexpandedMacros(const GCGraph *g) {
    for (auto it = g->nodes().begin(); it != g->nodes().end(); ++it) {
        const auto *n = *it;
        if (n && n->kind == GCNodeKind::Oper && n->isMacro()) {
            throw Refusal{
                originNear(g, it.ref()),
                std::format(
                    "macro operator '{}' is not expanded; run std::macro (then "
                    "std::opt) before onnx::export",
                    g->nodeBodyAs<camel::runtime::GCOperBody>(it.ref())->uri())};
        }
    }
}

Model translate(core::context::Context &ctx, const Request &request) {
    refuseUnexpandedMacros(request.graph);
    try {
        return translateFunction(
            ctx,
            request.fn,
            request.captures,
            request.examples,
            request.options);
    } catch (const ExportError &e) {
        std::string message = e.what();
        if (e.kind() == ExportError::Kind::CallRemains) {
            message += "; onnx::export inlines what std::opt left once the shapes "
                       "of the examples are known, so this call is one no simplification "
                       "removes (run std::macro before the pass when the program uses macros "
                       "such as grad)";
        }
        // An error about the request as a whole (its examples, dynamic axes, target, the
        // function's arity or result) names no node: it is reported at the export_model call.
        throw Refusal{
            e.origin() != camel::source::kInvalidOriginId ? e.origin() : request.origin,
            std::move(message)};
    }
}

/// The export of a program that makes no request: the computation of `main` is the model.
void exportEntry(
    core::context::Context &ctx, GCGraph *root, std::ostream &os, const std::string &out) {
    if (out.empty()) {
        throw Refusal{
            camel::source::kInvalidOriginId,
            std::format(
                "the program contains no onnx.export_model(fn, example, path) call, so "
                "onnx::export translates 'main' itself, and the model file is not named: give "
                "it as the parameter out, onnx::export(out=\"model.onnx\")")};
    }
    for (GCGraph *g : camel::runtime::collectReachableGraphs(root)) {
        if (g != nullptr) {
            refuseUnexpandedMacros(g);
        }
    }
    Model model;
    try {
        model = translateEntryGraph(ctx, root);
    } catch (const ExportError &e) {
        std::string message = e.what();
        if (e.kind() == ExportError::Kind::CallRemains) {
            message += "; onnx::export inlines what std::opt left, so this call is "
                       "one no simplification removes (run std::macro before the pass when "
                       "the program uses macros such as grad)";
        }
        // The program makes no export request, so what is translated is `main` as a whole:
        // say so, a driver program is refused here for what it does besides computing.
        message += " (the program contains no onnx.export_model call, so onnx::export "
                   "translates the computation of 'main' itself: its parameters are the "
                   "model's inputs, its result the outputs; a program that does more than "
                   "compute exports one of its functions with onnx.export_model instead)";
        throw Refusal{e.origin(), std::move(message)};
    }
    try {
        writeModelFile(model, out);
    } catch (const std::runtime_error &e) {
        throw Refusal{camel::source::kInvalidOriginId, e.what()};
    }
    os << std::format("onnx::export: wrote {}\n", out);
}

} // namespace

GCGraph *OnnxExportPass::apply(GCGraph *graph, std::ostream &os) {
    try {
        // Every request is translated before any file is written: a refusal leaves no model.
        const std::vector<Request> requests = collectRequests(*context_, graph, out_);
        if (requests.empty()) {
            exportEntry(*context_, graph, os, out_);
            return nullptr;
        }
        if (requests.size() > 1 && !out_.empty()) {
            // Reported at the second call: the first one is what the file name stands for.
            throw Refusal{
                requests[1].origin,
                std::format(
                    "the parameter out names one model file but the entry graph contains {} "
                    "onnx.export_model calls",
                    requests.size())};
        }
        std::vector<Model> models;
        for (const Request &request : requests) {
            models.push_back(translate(*context_, request));
        }
        for (size_t i = 0; i < requests.size(); ++i) {
            try {
                writeModelFile(models[i], requests[i].path);
            } catch (const std::runtime_error &e) {
                throw Refusal{requests[i].origin, e.what()};
            }
            os << std::format("onnx::export: wrote {}\n", requests[i].path);
        }
    } catch (const Refusal &refusal) {
        // Every refusal is located: at the node it names, else at the export_model call it
        // concerns (set where it is raised), else at the entry function.
        uint64_t origin = refusal.origin;
        if (origin == camel::source::kInvalidOriginId) {
            origin = camel::execute::entryFunctionOrigin(*context_);
        }
        if (origin == camel::source::kInvalidOriginId) {
            origin = entryOriginOf(*context_, graph);
        }
        auto diag = context_->rtmDiags()->of(RuntimeDiag::TranslationRefused);
        if (origin != camel::source::kInvalidOriginId) {
            diag.atOrigin(origin);
        }
        diag.commit(kPassName, refusal.message);
    }
    // Translation consumes the graph: the program is not executed.
    return nullptr;
}

void registerOnnxPasses() {
    PassDescriptor descriptor;
    descriptor.doc    = "translate the program to an ONNX model";
    descriptor.params = {PassParam{
        "out",
        PassParamType::String,
        std::string(),
        "the model file; overrides the path argument of onnx.export_model (then exactly one "
        "call is allowed) and names the file when the program makes no request"}};
    descriptor.make   = [](const core::context::context_ptr_t &ctx, const PassArgs &args) {
        return std::make_unique<OnnxExportPass>(ctx, args.getString("out"));
    };
    registerModulePass(kPassName, std::move(descriptor));
}

} // namespace camel::onnx::passes
