// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The state rule: a closure may not carry a `var` capture out of the evaluation that declared it
 * (design principles 3.5, rule two; the author's ruling of 2026-11-14).
 *
 * Rule two says what a `var` is. A `var` local lives in the call that declares it, and a `var`
 * parameter is the caller's cell lent to the call that received it. So a closure may capture one by
 * reference exactly while it stays inside that evaluation; the rule names the three ways a closure
 * leaves --
 *
 *   1. as a result: the call hands it back, and its caller reads a cell whose owner has returned;
 *   2. stored where the call does not own the cell: a field or an element of a value the caller
 *      handed in, which outlives the call the cell lives in;
 *   3. handed to a function that keeps it, by returning it or by keeping it in a cell of its own.
 *
 * -- and breaking it is a compile error rather than a run-time fault, because a cell whose owner has
 * returned is a value nobody owns and nothing about the graph says so. The two rewrites are the ones
 * the author named, and the capture is never quietly made by value: that would answer a different
 * question than the one the program asks.
 *
 * Nothing here reads the running program, and nothing new is written in the language: the judgement
 * is the effect analysis this builder already has (builder_resources.cpp). What a value's TYPE says
 * about its captures is nothing (camel/core/type/composite/func.h: the captures of a function value
 * are a fact about its graph, not about its type), so a body is judged by the conservative criterion
 * the design draft accepts (its cost item 5): a closure of this body
 * captured a cell of this body, and the body can hand a function out -- as its result, into a place
 * it does not own, or through a call that may keep one. Which closure ends up in which value is not
 * traced: a body that can hand a function out at all is never reported, so the rule costs nothing to
 * a body that keeps its state to itself.
 */

#include "builder.h"
#include "builder_helpers.h"
#include "builder_support.h"

#include "camel/core/operator_table.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"

#include <algorithm>
#include <string>
#include <utility>

using namespace std;
using namespace camel::core::type;

namespace sem = camel::core::sem;

namespace camel::compile::gir {

namespace {

/// Whether a cell can hold a value that carries a closure: the cell of a function, of a tuple or a
/// struct with a function-valued part, or of an array of one.
bool cellCanCarryAClosure(node_handle_t cell) {
    return typeCarriesFunction(valueTypeOf(nodeTypeOf(cell)));
}

/// Whether an operand of a call can carry a closure through it.
bool operandCanCarryAClosure(node_handle_t operand) {
    return typeCarriesFunction(valueTypeOf(nodeTypeOf(operand)));
}

} // namespace

std::unordered_set<node_handle_t> Builder::cellsOfBody(DraftGraphBuilder *body) const {
    // The cells this body's evaluation owns: the mutable cells it made itself, and its `var`
    // parameters, which its caller made and lent to it for exactly this call. A capture port is
    // neither: the cell behind it belongs to a graph around this one, and a closure that holds it
    // leaves THAT evaluation, which is where it is reported.
    std::unordered_set<node_handle_t> cells;
    for (node_handle_t cell : variables_) {
        if (!nodeIsCurrent(cell)) {
            continue; // the body that made it has been let go: this one cannot be its cell
        }
        if (nodeGraphOf(cell) == body && nodeKindOf(cell) != runtime::GCNodeKind::Port &&
            !moduleVariableResources_.contains(cell) && !captureOriginOf_.contains(cell)) {
            cells.insert(cell);
        }
    }
    const FunctionType *type = body->funcType();
    if (type == nullptr) {
        return cells;
    }
    const auto &draft      = body->transaction().graph();
    const auto withPorts   = draft.withPorts();
    const auto normPorts   = draft.normPorts();
    const size_t withCount = type->withTypesCount();
    for (size_t i = 0; i < withCount + type->normTypesCount(); ++i) {
        const bool isWith = i < withCount;
        if (!(isWith ? type->withIsVarAt(i) : type->normIsVarAt(i - withCount))) {
            continue;
        }
        const auto &ports = isWith ? withPorts : normPorts;
        const size_t at   = isWith ? i : i - withCount;
        if (at < ports.size()) {
            cells.insert(draft.ref(ports[at]));
        }
    }
    return cells;
}

std::optional<std::pair<node_handle_t, std::string>> Builder::carryOutOf(
    DraftGraphBuilder *body, const std::unordered_set<node_handle_t> &cells) const {
    const FunctionType *type = body->funcType();
    if (type == nullptr) {
        return std::nullopt;
    }
    const runtime::GraphTransaction &transaction = body->transaction();
    const auto &draft                            = transaction.graph();
    // 1. As a result. This is the place a closure leaves from most often, and the effect analysis
    // already asks this exact question of a body (design principles 3.3, effect masking: a body
    // whose result can hold a function and that made a closure over one of its own cells is measured
    // as reaching that cell).
    if (bodyResultCanHoldAFunction(*type)) {
        return std::make_pair(
            node_handle_t(nullptr),
            std::string("it goes out as the result of the call, and the cell it reads is gone by "
                        "the time the caller reads the closure"));
    }
    // 2. Into a place this call does not own: the caller's cell, handed in as a `var` parameter or
    // captured from a graph around this one. Such a cell outlives this call by construction -- that
    // is what makes it not this call's -- so a closure stored in it outlives the cell it reads.
    if (const auto sites = storeSites_.find(body); sites != storeSites_.end()) {
        for (const auto &[cell, exit] : sites->second) {
            if (!nodeIsCurrent(cell) || !nodeIsCurrent(exit)) {
                continue; // a body that has been let go: its stores are read by no evaluation now
            }
            if (cells.contains(cell) || !cellCanCarryAClosure(cell)) {
                continue;
            }
            return std::make_pair(
                exit,
                std::string("it is stored into a cell this call does not own, and that cell "
                            "outlives the call that declared the one the closure reads"));
        }
    }
    // 3. Handed to a call that may carry it out: one whose signature says it keeps what it is given
    // -- its result can hold a function, or it writes a `var` parameter that the argument is handed
    // to. A callee whose signature says nothing is not a route: every higher-order call of the
    // standard library (`map`, `foreach`, `filter`) takes a closure and drops it when it returns, and
    // the state rule is about the cell outliving the call, not about a call taking a function at all.
    for (draft_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        if (!draft.alive(id)) {
            continue;
        }
        const auto kind = nodeKindOf(draft.ref(id));
        if (kind != runtime::GCNodeKind::Func && kind != runtime::GCNodeKind::Call) {
            continue;
        }
        if (const auto words = carriedOffAt(body, id); words.has_value()) {
            return std::make_pair(draft.ref(id), *words);
        }
    }
    return std::nullopt;
}

std::optional<std::string> Builder::carriedOffAt(DraftGraphBuilder *body, draft_node_ref_t id) const {
    const runtime::GraphTransaction &transaction = body->transaction();
    const auto &draft                            = transaction.graph();
    const auto kind                              = nodeKindOf(draft.ref(id));
    const bool isCall = kind == runtime::GCNodeKind::Call;
    const auto norm   = draft.normInputsOf(id);
    const auto with   = draft.withInputsOf(id);
    // Whether an argument can carry a closure at all. A call through a value keeps the value it goes
    // through as its first `with` input, and that value is the function, not an argument.
    bool argumentCarries = false;
    for (size_t i = 0; i < norm.size() && !argumentCarries; ++i) {
        argumentCarries = operandCanCarryAClosure(draft.ref(norm[i]));
    }
    for (size_t i = isCall ? 1 : 0; i < with.size() && !argumentCarries; ++i) {
        argumentCarries = operandCanCarryAClosure(draft.ref(with[i]));
    }
    if (!argumentCarries) {
        return std::nullopt;
    }
    // What it goes through: a direct call names its callee, a call through a value keeps it as the
    // first `with` input.
    compile_graph_ptr_t callee;
    const FunctionType *calleeType = nullptr;
    if (!isCall) {
        callee     = body->funcTarget(draft.ref(id));
        calleeType = callee == nullptr ? nullptr : callee->funcType();
    } else if (!with.empty()) {
        Type *valueType = nodeTypeOf(draft.ref(with[0]));
        if (valueType != nullptr && valueType->code() == TypeCode::Function) {
            calleeType = static_cast<const FunctionType *>(valueType);
        }
    }
    const std::string keeps =
        "it is handed to a call that may keep it, and that call's own evaluation outlives the one "
        "that declares the cell";
    // The callee may keep it when its result can hold a function: what it was handed comes back out
    // and the caller keeps it.
    if (typeCarriesFunction(valueTypeOf(nodeTypeOf(draft.ref(id))))) {
        return keeps;
    }
    if (calleeType == nullptr) {
        return std::nullopt; // nothing is known about what it does with what it is given
    }
    const sem::ResourceSignature *signature = &calleeType->resources();
    if (const auto measured = inferredResources_.find(callee.get());
        callee != nullptr && measured != inferredResources_.end() &&
        inferredSignatures_.contains(callee.get())) {
        signature = &measured->second;
    }
    if (signature->mode != sem::ResourceSignature::Mode::Concrete) {
        return std::nullopt; // a signature that says nothing promises nothing
    }
    // A `var` parameter it writes is a cell the caller handed in: a closure stored there stays in
    // the caller's cell after this call returns.
    const size_t calleeWithCount = calleeType->withTypesCount();
    for (const auto &use : signature->params) {
        if (!sem::writes(use.mode) || use.cls != sem::ResourceClass::Variable) {
            continue;
        }
        const bool fromWith = use.index < calleeWithCount;
        const auto &inputs  = fromWith ? with : norm;
        const size_t at     = fromWith ? size_t(use.index) + (isCall ? 1 : 0)
                                       : size_t(use.index) - calleeWithCount;
        if (at < inputs.size() && operandCanCarryAClosure(draft.ref(inputs[at]))) {
            return keeps;
        }
    }
    return std::nullopt;
}

void Builder::reportCellEscape(DraftGraphBuilder *body,
    node_handle_t cell, node_handle_t at, const std::string &where) {
    DiagnosticBuilder builder = diags_->of(SemanticDiag::ClosureCaptureEscapes);
    if (const auto origin = nodeDraftOf(cell)->nodeOrigin(nodeIdOf(cell));
        origin != camel::source::kInvalidOriginId) {
        builder.atOrigin(origin);
    }
    if (at != nullptr) {
        if (const auto note = nodeDraftOf(at)->nodeOrigin(nodeIdOf(at));
            note != camel::source::kInvalidOriginId) {
            builder.relatedOrigin(note, where);
        }
    }
    const auto named = originNames_.find(cell);
    builder.commit(
        captureNameOf(cell, named == originNames_.end() ? std::string("this variable") : named->second));
}

void Builder::checkCellEscapes() {
    for (const compile_graph_ptr_t &graph : unit_->builtBodies) {
        DraftGraphBuilder *body = graph.get();
        if (body->funcType() == nullptr) {
            continue; // the body did not build (an error was reported)
        }
        const auto cells = cellsOfBody(body);
        if (cells.empty()) {
            continue;
        }
        // The cells a closure holds by reference, in capture order. A closure that captured a
        // closure carries the cells of that one too -- the value it holds is the function -- and it
        // is listed under this body when this body made the cell.
        std::vector<node_handle_t> held;
        for (const auto &[closure, summary] : captures_) {
            (void)closure;
            for (node_handle_t cell : summary.variables) {
                if (cells.contains(cell) && std::find(held.begin(), held.end(), cell) == held.end()) {
                    held.push_back(cell);
                }
            }
        }
        if (held.empty()) {
            continue; // no closure holds a cell of this body: the rule has nothing to say
        }
        const auto carry = carryOutOf(body, cells);
        if (!carry.has_value()) {
            continue; // the closure stays inside the call: exactly what the rule allows
        }
        for (node_handle_t cell : held) {
            reportCellEscape(body, cell, carry->first, carry->second);
        }
    }
}

} // namespace camel::compile::gir
