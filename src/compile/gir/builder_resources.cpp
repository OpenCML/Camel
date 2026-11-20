// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The resources a built body touches: the measurement each body publishes and the check that
 * holds a declaration to the clause it wrote. Nothing here is read from the running program: a
 * node publishes its own signature -- an operator's from the operator table, a callee's from its
 * own measurement, a call's from the value it goes through -- and what those signatures mean is
 * camel/core/resource_semantics.h. What a violation sounds like is builder_diagnostics.cpp's.
 */

#include "builder.h"
#include "builder_helpers.h"

#include "camel/compile/gir/resource_transition.h"
#include "camel/core/operator_names.h"
#include "camel/core/operator_table.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/text.h"
#include "camel/utils/type.h"

#include <algorithm>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std;
using namespace camel::core::type;

namespace sem = camel::core::sem;

namespace camel::compile::gir {

namespace {

using sem::AccessMode;
using sem::ResourceClass;
using sem::ResourceSignature;

// The bottom of a measurement: a body recorded as reaching nothing. Every body starts here, and no
// body is ever below it. What a call answers about its result stays the conservative one: the walk
// says which resources a body reaches, not what its result aliases.
ResourceSignature emptyReach() {
    ResourceSignature reach;
    reach.mode   = ResourceSignature::Mode::Concrete;
    reach.result = sem::ResultAlias{.kind = sem::ResultAlias::Kind::Unknown};
    return reach;
}

// What a body reaches when the walk cannot place one of its nodes. Reaching *something* unknown is
// the honest reading (a class the clause has no name for, an ambient resource that could be any),
// and recording it is what lets the clause a declaration wrote still be judged: skipping the body
// would leave the promise unchecked and unsaid (design principles 3.3).
ResourceSignature unplaceableReach() {
    ResourceSignature reach = emptyReach();
    for (ResourceClass cls :
         {ResourceClass::Variable,
          ResourceClass::Object,
          ResourceClass::Foreign,
          ResourceClass::Ambient,
          ResourceClass::Handle}) {
        reach.anyOf.push_back(cls);
    }
    reach.external = true;
    return reach;
}

// What two readings of one body amount to: a resource either of them reaches is reached. The mode
// widens the same way, and the parameter and ambient uses keep the wider access.
ResourceSignature uniteReach(const ResourceSignature &a, const ResourceSignature &b) {
    ResourceSignature united = a;
    for (const auto &use : b.params) {
        const auto found =
            std::find_if(united.params.begin(), united.params.end(), [&](const auto &other) {
                return other.index == use.index;
            });
        if (found == united.params.end()) {
            united.params.push_back(use);
            continue;
        }
        if (found->cls != use.cls) {
            found->cls = ResourceClass::Unknown;
        }
        if (found->name.empty()) {
            found->name = use.name;
        }
        found->mode = sem::uniteAccess(found->mode, use.mode);
    }
    for (const auto &use : b.ambient) {
        const auto found =
            std::find_if(united.ambient.begin(), united.ambient.end(), [&](const auto &other) {
                return other.name == use.name;
            });
        if (found == united.ambient.end()) {
            united.ambient.push_back(use);
            continue;
        }
        found->mode      = sem::uniteAccess(found->mode, use.mode);
        found->unordered = found->unordered || use.unordered;
    }
    united.captures.insert(united.captures.end(), b.captures.begin(), b.captures.end());
    united.anyOf.insert(united.anyOf.end(), b.anyOf.begin(), b.anyOf.end());
    united.result = b.result;
    if (!united.allocates && b.allocates.has_value()) {
        united.allocates = b.allocates;
    }
    united.external = united.external || b.external;
    // The capability is united the way `external` is: a reading of the body that reaches the
    // spelling makes the whole body reach it, whichever round found it (design principles 9.5).
    united.spelling = united.spelling || b.spelling;
    std::sort(united.params.begin(), united.params.end(), [](const auto &left, const auto &right) {
        return left.index < right.index;
    });
    std::sort(
        united.ambient.begin(),
        united.ambient.end(),
        [](const auto &left, const auto &right) { return left.name < right.name; });
    std::sort(united.captures.begin(), united.captures.end());
    united.captures.erase(
        std::unique(united.captures.begin(), united.captures.end()),
        united.captures.end());
    std::sort(united.anyOf.begin(), united.anyOf.end());
    united.anyOf.erase(std::unique(united.anyOf.begin(), united.anyOf.end()), united.anyOf.end());
    return united;
}

/// Whether a value of this type can hold a function value: `typeCarriesFunction` in
/// builder_helpers.h, which the state rule (`Builder::reportCellEscapes`) reads the same way -- a
/// body whose result can hold a function can hand a closure it made to its caller, and that is what
/// makes the cell the closure captured outlive the call.

/// The ordering key of an access through a handle the body received (design principles 3.3, handle
/// types): the handle's type when the type is a handle type, and the name the
/// body gave the value otherwise. A handle is a run-time value, so two names may be one handle, and
/// nothing in the body tells two same-typed names apart -- the accesses through them are one
/// resource's and keep the order they are written in. Two handle types are two handles, a
/// difference the compiler can prove, so they keep no order. A handle the body made itself is keyed
/// by its node instead (Builder::handleKeyOf): two `ref` calls are two boxes, which is another
/// difference the compiler can prove.
std::string handleOrderKeyOf(const Type *type, std::string otherwise) {
    if (!sem::isHandleType(type)) {
        return otherwise;
    }
    return "H" + camel::core::type::typeMangle(type);
}

} // namespace

void Builder::numberBodyPorts(
    BodyReach &walk, DraftGraphBuilder *graph, const runtime::GraphTransaction &transaction,
    const FunctionType &type) const {
    const auto &draft = transaction.graph();
    // Which parameter a port is, in the numbering a call uses: `with` parameters first, then the
    // ordinary ones (a `constas` parameter is not a runtime value and has no port).
    const size_t withCount = type.withTypesCount();
    const auto withPorts   = draft.withPorts();
    const auto normPorts   = draft.normPorts();
    size_t index = 0, withAt = 0, normAt = 0;
    for (size_t i = 0; i < withCount + type.normTypesCount(); ++i) {
        const bool isWith = i < withCount;
        if (isWith) {
            if (type.withIsConstasAt(i)) {
                continue; // not a runtime parameter
            }
            if (withAt < withPorts.size()) {
                walk.ports.emplace(withPorts[withAt++], index);
            }
        } else {
            if (type.normIsConstasAt(i - withCount)) {
                continue; // not a runtime parameter
            }
            if (normAt < normPorts.size()) {
                walk.ports.emplace(normPorts[normAt++], index);
            }
        }
        ++index;
    }
    // A `with` port the type does not count is a value the function captured from the one around
    // it; so is a closure node. A use through one is not a use of a parameter of this function: it
    // is that function's resource, or a cell of this body's own frame (placeCapturedUse). The
    // position is how this body's own signature names it, so that a caller holding its value can
    // read the capture back.
    for (; withAt < withPorts.size(); ++withAt) {
        walk.captured.insert(withPorts[withAt]);
        if (const auto position = capturePositionOf(graph, draft.ref(withPorts[withAt]));
            position.has_value()) {
            walk.capturePositions.emplace(withPorts[withAt], *position);
        }
    }
    for (draft_node_ref_t node : draft.closureNodes()) {
        walk.captured.insert(node);
        if (const auto position = capturePositionOf(graph, draft.ref(node)); position.has_value()) {
            walk.capturePositions.emplace(node, *position);
        }
    }
}

void Builder::addParamUse(
    BodyReach &walk, size_t index, sem::ResourceClass cls, const std::string &name,
    sem::AccessMode mode) const {
    if (index > 0xFF) {
        // A signature numbers its parameters in one byte, so a use through the 256th parameter has
        // no way to be written down. Dropping it would measure the body as reaching less than it
        // does, and a `pure` body would pass on a use nobody recorded. Nothing here is measured
        // instead.
        walk.unplaceable = true;
        return;
    }
    ResourceSignature &out = walk.reach;
    const auto found = std::find_if(out.params.begin(), out.params.end(), [&](const auto &use) {
        return use.index == static_cast<uint8_t>(index);
    });
    if (found == out.params.end()) {
        out.params.push_back(
            ResourceSignature::ParamUse{
                .index = static_cast<uint8_t>(index),
                .cls   = cls,
                .name  = name,
                .mode  = mode});
        return;
    }
    // One parameter, one resource: two uses of it are one use, with the wider mode.
    if (found->cls != cls) {
        found->cls = sem::ResourceClass::Unknown;
    }
    if (found->name.empty()) {
        found->name = name;
    }
    found->mode = sem::uniteAccess(found->mode, mode);
}

std::optional<std::string> Builder::moduleVariableOf(
    const runtime::GraphTransaction &transaction, draft_node_ref_t port) const {
    // A capture port stands for the node it captured, which may itself be a capture port of the
    // function around: follow the chain to the cell, and ask whether it is a module's variable.
    node_handle_t at = transaction.graph().ref(port);
    for (size_t hops = 0; hops < 64; ++hops) {
        if (const auto cell = moduleVariableResources_.find(at);
            cell != moduleVariableResources_.end()) {
            return cell->second;
        }
        const auto origin = captureOriginOf_.find(at);
        if (origin == captureOriginOf_.end()) {
            return std::nullopt;
        }
        at = origin->second;
    }
    return std::nullopt;
}

void Builder::addAmbientUse(
    BodyReach &walk, const std::string &name, sem::AccessMode mode, bool unordered) const {
    ResourceSignature &out = walk.reach;
    const auto found = std::find_if(out.ambient.begin(), out.ambient.end(), [&](const auto &use) {
        return use.name == name;
    });
    if (found == out.ambient.end()) {
        out.ambient.push_back(
            ResourceSignature::AmbientUse{.name = name, .mode = mode, .unordered = unordered});
        return;
    }
    found->mode      = sem::uniteAccess(found->mode, mode);
    found->unordered = found->unordered || unordered;
}

bool Builder::placeCapturedUse(
    BodyReach &walk, DraftGraphBuilder *graph, const runtime::GraphTransaction &transaction,
    draft_node_ref_t captured, sem::AccessMode mode) const {
    // Design principles 3.3, effect masking: a function promises none of its caller's resources,
    // and a variable the function makes itself is not one of them -- the cell dies with the call
    // and the caller cannot read it, so reading or writing it is no effect of this function, even
    // when a closure that stays inside this call is what writes it. What the resource is, three
    // answers in order:
    //   * a variable of a module's top level: the caller reads the same cell, so it is ambient;
    //   * a capture of this body: the value comes from the graph around it, and this body's own
    //     signature names it by position, so the function holding this value can read it back;
    //   * a cell of this body: nothing the caller can reach, so nothing to record -- unless this
    //     body can hand the closure out, because then the cell outlives the call.
    if (const auto variable = moduleVariableOf(transaction, captured); variable.has_value()) {
        addAmbientUse(walk, *variable, mode);
        return true;
    }
    if (const auto position = walk.capturePositions.find(captured);
        position != walk.capturePositions.end()) {
        walk.reach.captures.push_back(position->second);
        return true;
    }
    const node_handle_t node = transaction.graph().ref(captured);
    if (nodeGraphOf(node) != graph) {
        return false; // not a node of this body: which resource it is cannot be read from here
    }
    if (!isVariable(node)) {
        // A value this body computed that is not a cell cannot be written through: there is no
        // resource here to record, and no cell that could outlive the call.
        return true;
    }
    if (bodyResultCanHoldAFunction(*graph->funcType())) {
        // The body can hand a closure it made to its caller, so the cell such a closure captured
        // outlives the call and a write of it is an effect the caller can reach. A clause has no
        // name for a cell of the function's own making, so the reach records its class.
        walk.reach.anyOf.push_back(sem::ResourceClass::Variable);
    }
    return true;
}

bool Builder::bodyResultCanHoldAFunction(const camel::core::type::FunctionType &type) const {
    return type.hasExitType() && typeCarriesFunction(type.exitType());
}

bool Builder::placeCalleeCapture(
    BodyReach &walk, DraftGraphBuilder *graph, const runtime::GraphTransaction &transaction,
    const compile_graph_ptr_t &callee, size_t position, draft_node_ref_t value) const {
    // One capture of a callee, as this body sees it. A direct call hands its callee's captures in
    // after the declared `with` arguments (addCaptureSite links them, linkCaptureSites places them
    // there), so the caller's node is known; a call through a value does not list them among its
    // inputs, and the value was made in the graph that captured them, so the origin is read from
    // there. The origin then answers the same question a direct use of a capture answers
    // (placeCapturedUse), except that a cell of an enclosing graph is this body's capture of it.
    const auto classify = [&](node_handle_t at) -> bool {
        if (at == nullptr) {
            return false;
        }
        if (const auto cell = moduleVariableResources_.find(at);
            cell != moduleVariableResources_.end()) {
            addAmbientUse(walk, cell->second, sem::AccessMode::ReadModifyWrite);
            return true;
        }
        if (const auto port = capturePositionOf(graph, at); port.has_value()) {
            walk.reach.captures.push_back(*port);
            return true;
        }
        if (nodeGraphOf(at) == graph) {
            return placeCapturedUse(
                walk,
                graph,
                transaction,
                nodeIdOf(at),
                sem::AccessMode::ReadModifyWrite);
        }
        // A value of a graph around this one: this body holds it in a capture port of its own.
        const node_handle_t port = capturedCellIn(graph, at);
        if (port == nullptr) {
            return false;
        }
        const auto position = capturePositionOf(graph, port);
        if (!position.has_value()) {
            return false;
        }
        walk.reach.captures.push_back(*position);
        return true;
    };
    if (value != 0 && transaction.graph().alive(value)) {
        return classify(transaction.graph().ref(value));
    }
    const auto table = capturedValues_.find(callee.get());
    if (table == capturedValues_.end() || position >= table->second.size()) {
        return false;
    }
    return classify(table->second[position].origin);
}

bool Builder::readCalleeSignature(
    BodyReach &walk, DraftGraphBuilder *graph, const runtime::GraphTransaction &transaction,
    const ResourceSignature &sig, size_t calleeWithCount, bool isCall, draft_node_ref_t id,
    const compile_graph_ptr_t &calleeGraph) const {
    // What one node's signature amounts to for this body: its own parameters, in the numbering the
    // callee uses (with parameters first, then ordinary ones -- the same mapping as
    // runtime/resources.cpp fromSignature), read back onto the parameters of this body where the
    // operand is one of them. False when the signature says nothing (Generic), or says something
    // this measurement cannot place: the body is then not measured at all, rather than measured as
    // pure because the walk gave up.
    if (sig.mode != ResourceSignature::Mode::Concrete) {
        return false;
    }
    const auto &draft = transaction.graph();
    const auto norm   = draft.normInputsOf(id);
    const auto with   = draft.withInputsOf(id);
    for (const auto &use : sig.params) {
        draft_node_ref_t operand = 0;
        if (use.index < calleeWithCount) {
            // A call through a value keeps the value itself as its first `with` input.
            const size_t at = size_t(use.index) + (isCall ? 1 : 0);
            if (at >= with.size()) {
                return false;
            }
            operand = with[at];
        } else {
            const size_t at = size_t(use.index) - calleeWithCount;
            if (at >= norm.size()) {
                return false;
            }
            operand = norm[at];
        }
        if (use.cls == sem::ResourceClass::Handle) {
            // A handle access names a value, not a parameter of this body (design effects plan
            // 3.2): its key is read before the port test below, because a handle this body made
            // itself is no port of the caller and still has to meet the other operations on the
            // one box it names.
            std::string key;
            walk.valueKeys.push_back(
                handleKeyOf(walk, graph, transaction, operand, key) ? std::move(key)
                                                                    : std::string());
        }
        if (walk.captured.contains(operand)) {
            if (!placeCapturedUse(walk, graph, transaction, operand, use.mode)) {
                return false;
            }
            continue;
        }
        const auto port = walk.ports.find(operand);
        if (port == walk.ports.end()) {
            // The operand is a value the body computed, not one of its caller's: a use through it
            // is not a use of a resource the caller still holds.
            continue;
        }
        addParamUse(walk, port->second, use.cls, use.name, use.mode);
    }
    // What the callee captured. A closure that wrote no clause carries the captures its body was
    // measured to reach, and a call of it through its value is a use of those (design principles
    // 3.3): each is read onto this body, which may find it is a cell of its own -- in which case
    // nothing is reached, and the callee's write stays inside this call.
    for (uint8_t capture : sig.captures) {
        draft_node_ref_t value = 0;
        if (!isCall) {
            // A direct call hands the captures in after the declared `with` arguments.
            const size_t at = calleeWithCount + size_t(capture);
            if (at >= with.size()) {
                return false;
            }
            value = with[at];
        }
        if (!placeCalleeCapture(walk, graph, transaction, calleeGraph, size_t(capture), value)) {
            return false;
        }
    }
    for (const auto &use : sig.ambient) {
        addAmbientUse(walk, use.name, use.mode, use.unordered);
    }
    for (sem::ResourceClass cls : sig.anyOf) {
        walk.reach.anyOf.push_back(cls);
    }
    if (sig.allocates) {
        walk.reach.allocates = sig.allocates;
    }
    walk.reach.external = walk.reach.external || sig.external;
    // A call reads the spelling exactly when the callee may: the capability is the signature's, and
    // a caller that does not write it is refused (design principles 9.5).
    walk.reach.spelling = walk.reach.spelling || sig.spelling;
    return true;
}

Builder::OperandReach Builder::readFunctionOperandReach(
    BodyReach &walk, DraftGraphBuilder *graph, const runtime::GraphTransaction &transaction,
    draft_node_ref_t id, const sem::ResourceBinding &where, sem::ElementShape shape) const {
    if (where.kind != sem::ResourceBinding::Kind::With) {
        // An operator that takes the function as an ordinary operand (`:mark/isolate`) puts its
        // other operands after it, so the callback's parameters do not line up with the node's
        // operands here: left as the walk found it.
        return OperandReach::CannotPlace;
    }
    if (shape == sem::ElementShape::Fold) {
        // A fold's accumulator is the initial value -- the `with` operand after the function --
        // and not one of the callback's parameters, so reading the signature onto the operands
        // would place it on the array: left as the walk found it.
        return OperandReach::CannotPlace;
    }
    const auto &draft = transaction.graph();
    const auto with   = draft.withInputsOf(id);
    if (where.index >= with.size()) {
        return OperandReach::Unreadable;
    }
    Type *type = nodeTypeOf(draft.ref(with[where.index]));
    if (type == nullptr || type->code() != TypeCode::Function) {
        return OperandReach::Unreadable;
    }
    const auto *fn = tt::as_ptr<FunctionType>(type);
    if (fn->resources().mode != ResourceSignature::Mode::Concrete) {
        // A parameter of function type that writes no clause promises nothing (`Generic`).
        return OperandReach::Unreadable;
    }
    // The value stands where a call's callee stands and the operator's ordinary operands are the
    // call's arguments (`ElementCalls`, operator_semantics.h): this is `reachOfCall`'s reading.
    if (!readCalleeSignature(
            walk,
            graph,
            transaction,
            fn->resources(),
            fn->withTypesCount(),
            true,
            id,
            functionValueGraphOf(draft.ref(with[where.index])))) {
        return OperandReach::Unreadable;
    }
    return OperandReach::Placed;
}

void Builder::readUnknownValue(BodyReach &walk) const {
    // A signature that says nothing promises nothing, and what a call through it reaches is the
    // instantiation's: any instantiation may write a cell or print. Read conservatively
    // (resource_semantics.h, `Generic`) rather than as touching nothing -- a body is never measured
    // as pure because the walk found a signature it could not read. What has such a signature is a
    // parameter of function type that writes no clause of its own (resource-generic, design
    // principles 3.3): a function that wrote a clause carries the clause, and a function that wrote
    // none carries the set its own body was measured to reach, so those two are read above.
    for (sem::ResourceClass cls :
         {sem::ResourceClass::Variable,
          sem::ResourceClass::Object,
          sem::ResourceClass::Foreign,
          sem::ResourceClass::Ambient,
          sem::ResourceClass::Handle}) {
        walk.reach.anyOf.push_back(cls);
    }
    walk.reach.external = true;
    // The spelling is *not* read conservatively here, unlike the resources above. What a signature
    // nobody wrote may reach is unbounded, so a body has to be measured as reaching everything;
    // the spelling is not like that: the guarantee 9.5 makes is about the body's own text, and
    // `covers` refuses a value that may read spelling where a signature does not promise it -- so a
    // function handed in is caught where it is handed in, and not again here for every body that
    // calls it through a parameter.
}

std::optional<sem::ResultAlias> Builder::resultAliasOf(
    DraftGraphBuilder *graph, const runtime::GraphTransaction &transaction,
    draft_node_ref_t id) const {
    // How a node's result relates to its operands, read from what the node names: an operator's
    // declaration, a callee's signature (the clause it wrote, or what its body was measured to
    // reach), or the signature of the function value a call goes through. `nullopt` for a node
    // that runs nothing -- a port, a gate, a constant -- whose value is simply its own.
    const auto &draft = transaction.graph();
    const auto kind   = nodeKindOf(draft.ref(id));
    if (kind == runtime::GCNodeKind::Oper) {
        const auto &op = graph->operTarget(draft.ref(id));
        if (op == nullptr) {
            return std::nullopt;
        }
        const auto &table = camel::core::OperatorTable::instance();
        if (const auto found = table.lookup(std::string(op->uri())); found.has_value()) {
            if (const auto *facts = table.semantics(*found); facts != nullptr && facts->resources) {
                return facts->resources->result;
            }
        }
        // An operator that declares nothing may answer anything one of its operands holds.
        return sem::ResultAlias{.kind = sem::ResultAlias::Kind::Unknown};
    }
    if (kind == runtime::GCNodeKind::Func) {
        const compile_graph_ptr_t callee = graph->funcTarget(draft.ref(id));
        if (callee == nullptr || callee->funcType() == nullptr) {
            return std::nullopt;
        }
        if (inferredSignatures_.contains(callee.get())) {
            const auto inferred = inferredResources_.find(callee.get());
            if (inferred != inferredResources_.end()) {
                return inferred->second.result;
            }
        }
        const ResourceSignature &sig = callee->funcType()->resources();
        if (sig.mode != ResourceSignature::Mode::Concrete) {
            return sem::ResultAlias{.kind = sem::ResultAlias::Kind::Unknown};
        }
        return sig.result;
    }
    if (kind == runtime::GCNodeKind::Call) {
        const auto callee = draft.withInputsOf(id);
        if (callee.empty()) {
            return std::nullopt;
        }
        Type *calleeType = nodeTypeOf(draft.ref(callee[0]));
        if (calleeType == nullptr || calleeType->code() != TypeCode::Function) {
            return std::nullopt;
        }
        const auto *fnType = tt::as_ptr<FunctionType>(calleeType);
        if (fnType->resources().mode != ResourceSignature::Mode::Concrete) {
            return sem::ResultAlias{.kind = sem::ResultAlias::Kind::Unknown};
        }
        return fnType->resources().result;
    }
    return std::nullopt;
}

bool Builder::handleKeyOf(
    BodyReach &walk, DraftGraphBuilder *graph, const runtime::GraphTransaction &transaction,
    draft_node_ref_t value, std::string &key) const {
    // The resource a handle value names (design effects plan 3.2: the key is the handle's value).
    // The value is followed through the nodes that only name the same handle again -- a gate, a
    // copy, and a result the operator says is the operand it was handed -- until it lands on a
    // parameter of this body, on one of its captures, or on a node that made the handle itself.
    // Two uses with one key reach one box; two uses with two keys are two references, whose
    // operations keep no order between them. A key a body received is the handle's type rather
    // than its position (handleOrderKeyOf): two same-typed names may name one box, and the body
    // cannot prove they do not.
    const auto &draft = transaction.graph();
    std::unordered_set<draft_node_ref_t> seen;
    while (draft.alive(value) && seen.insert(value).second) {
        if (const auto port = walk.ports.find(value); port != walk.ports.end()) {
            key = handleOrderKeyOf(nodeTypeOf(draft.ref(value)), std::format("p{}", port->second));
            return true;
        }
        if (walk.captured.contains(value)) {
            const auto at = walk.capturePositions.find(value);
            if (at == walk.capturePositions.end()) {
                return false;
            }
            key = handleOrderKeyOf(
                nodeTypeOf(draft.ref(value)),
                std::format("c{}", static_cast<unsigned>(at->second)));
            return true;
        }
        const node_handle_t node = draft.ref(value);
        if (nodeGraphOf(node) != graph) {
            // A value of a graph around this one: this body holds it in a capture port of its own.
            const node_handle_t port = capturedCellIn(graph, node);
            const auto at =
                port == nullptr ? std::optional<uint8_t>() : capturePositionOf(graph, port);
            if (!at.has_value()) {
                return false;
            }
            key =
                handleOrderKeyOf(nodeTypeOf(port), std::format("c{}", static_cast<unsigned>(*at)));
            return true;
        }
        const auto kind = nodeKindOf(node);
        if (kind == runtime::GCNodeKind::Gate || kind == runtime::GCNodeKind::Copy) {
            const auto in = draft.normInputsOf(value);
            if (in.empty()) {
                return false;
            }
            value = in[0];
            continue;
        }
        const auto alias = resultAliasOf(graph, transaction, value);
        if (alias.has_value()) {
            if (alias->kind == sem::ResultAlias::Kind::Same ||
                alias->kind == sem::ResultAlias::Kind::View) {
                const auto in = draft.normInputsOf(value);
                if (alias->operand >= in.size()) {
                    return false;
                }
                value = in[alias->operand];
                continue;
            }
            if (alias->kind != sem::ResultAlias::Kind::Fresh) {
                // The result may be a value one of its operands holds, so this cannot say which
                // one handle it names.
                return false;
            }
        }
        key = std::format("h{}", static_cast<unsigned>(value));
        return true;
    }
    return false;
}

bool Builder::reachOfOper(
    DraftGraphBuilder *graph, const runtime::GraphTransaction &transaction, BodyReach &walk,
    draft_node_ref_t id) const {
    const auto &draft = transaction.graph();
    const auto &op    = graph->operTarget(draft.ref(id));
    if (op == nullptr) {
        return false;
    }
    // An operator's resources are in the operator table, not on its type
    // (runtime/resources.cpp oper reads them the same way): what the table declares, and otherwise
    // the conservative reading of its traits.
    const auto &table = camel::core::OperatorTable::instance();
    const std::string uri(op->uri());
    const auto traits = table.find(uri);
    sem::ResourceSpec spec;
    sem::ElementCalls elementCalls;
    bool declared = false;
    if (const auto found = table.lookup(uri); found.has_value()) {
        if (const auto *facts = table.semantics(*found); facts != nullptr && facts->resources) {
            spec         = *facts->resources;
            elementCalls = facts->elementCalls.value_or(sem::ElementCalls{});
            declared     = true;
        }
    }
    if (!declared) {
        spec = sem::conservativeResources(
            draft.normInputsOf(id).size(),
            draft.withInputsOf(id).size(),
            traits.has_value() && traits->pure,
            table.hasExternalEffect(uri));
    }
    if (spec.functionOperand) {
        // The accesses are the function operand's (design principles 3.3), so
        // they are read off that value's type -- the same reading `reachOfCall` makes for a call
        // through a value, with the value standing where the call's value does. A value that is
        // not there, is not a function, or whose type writes no clause promises nothing and is
        // read conservatively; a fold's accumulator is the initial value and not one of the
        // callback's parameters, so that shape keeps the reading this walk had before rather than
        // placing a use on the wrong operand.
        switch (readFunctionOperandReach(walk, graph, transaction, id, *spec.functionOperand,
                                         elementCalls.shape)) {
        case OperandReach::Placed:
            break;
        case OperandReach::Unreadable:
            readUnknownValue(walk);
            break;
        case OperandReach::CannotPlace:
            break;
        }
        return true;
    }
    for (const sem::ResourceAccess &access : spec.accesses) {
        if (access.binding.kind == sem::ResourceBinding::Kind::Ambient) {
            addAmbientUse(walk, access.resource.name, access.mode, access.unordered);
            continue;
        }
        if (access.binding.kind == sem::ResourceBinding::Kind::Capture) {
            // Which capture of this graph an operator's own footprint names is a position in that
            // footprint, not the position of one of this graph's capture ports: the two do not
            // correspond, so the use cannot be placed from here.
            return false;
        }
        const bool isWith = access.binding.kind == sem::ResourceBinding::Kind::With;
        const auto inputs = isWith ? draft.withInputsOf(id) : draft.normInputsOf(id);
        // A call through a value keeps the value itself as its first `with` input, so its `with`
        // operands start after it. An operator node has no callee among its inputs -- what it calls
        // is the operator the table holds -- and its `with` operands are the call's `with`
        // arguments as written. Reading the node's kind is what runtime/resources.cpp does for the
        // same mapping; counting the callee on an operator node costs the whole body its
        // measurement (the operand lands past the last `with` input and the walk gives up).
        const bool isCall = nodeKindOf(draft.ref(id)) == runtime::GCNodeKind::Call;
        const size_t at   = size_t(access.binding.index) + (isWith && isCall ? 1 : 0);
        if (at >= inputs.size()) {
            return false;
        }
        if (access.resource.cls == sem::ResourceClass::Handle) {
            // The same reading as in readCalleeSignature, for an operator's own declaration.
            std::string key;
            walk.valueKeys.push_back(
                handleKeyOf(walk, graph, transaction, inputs[at], key) ? std::move(key)
                                                                       : std::string());
        }
        if (walk.captured.contains(inputs[at])) {
            if (!placeCapturedUse(walk, graph, transaction, inputs[at], access.mode)) {
                return false;
            }
            continue;
        }
        const auto port = walk.ports.find(inputs[at]);
        if (port == walk.ports.end()) {
            continue; // a value the body computed, not one of its caller's
        }
        addParamUse(walk, port->second, access.resource.cls, access.resource.name, access.mode);
    }
    walk.reach.external = walk.reach.external || spec.externalEffect;
    // What the operator says about the spelling of the program is declared once, in its spec, the
    // way its accesses are (design principles 9.5): a body that calls one of these reads it.
    walk.reach.spelling = walk.reach.spelling || spec.spelling;
    return true;
}

bool Builder::reachOfFunc(
    DraftGraphBuilder *graph, const runtime::GraphTransaction &transaction, BodyReach &walk,
    draft_node_ref_t id) const {
    const auto &draft                = transaction.graph();
    const compile_graph_ptr_t callee = graph->funcTarget(draft.ref(id));
    if (callee == nullptr) {
        return false;
    }
    const FunctionType *calleeType = callee->funcType();
    if (calleeType == nullptr) {
        return false;
    }
    // A call reads the signature of the function it calls -- the clause it wrote, or, when it wrote
    // none, the set its body was measured to reach (design principles 3.3). A call through a value
    // reads the same signature below, so a function's resources have one reading and both paths
    // take it. A function that wrote no clause keeps its measurement in the fixpoint while that is
    // still running, which is what a caller has to read then; a signature that says nothing at all
    // is the one a function-type parameter has, and what the call reaches is the instantiation's,
    // so it is read conservatively rather than as reaching nothing.
    const auto inferred = inferredSignatures_.contains(callee.get())
                              ? inferredResources_.find(callee.get())
                              : inferredResources_.end();
    if (inferred != inferredResources_.end()) {
        return readCalleeSignature(
            walk,
            graph,
            transaction,
            inferred->second,
            calleeType->withTypesCount(),
            false,
            id,
            callee);
    }
    if (calleeType->resources().mode != ResourceSignature::Mode::Concrete) {
        // A signature that promises nothing about its resources still says whether it reads the
        // spelling: a declaration that writes the capability and nothing else (`f! spelling`) is
        // Generic and carries it, and a caller has to know (design principles 9.5).
        walk.reach.spelling = walk.reach.spelling || calleeType->resources().spelling;
        readUnknownValue(walk);
        return true;
    }
    return readCalleeSignature(
        walk,
        graph,
        transaction,
        calleeType->resources(),
        calleeType->withTypesCount(),
        false,
        id,
        callee);
}

bool Builder::reachOfCall(
    DraftGraphBuilder *graph, const runtime::GraphTransaction &transaction, BodyReach &walk,
    draft_node_ref_t id) const {
    // A call through a function value: what it runs is the value's signature, which is the first
    // `with` input of the call (isEffectNode reads it the same way). A value of a function that
    // wrote no clause carries the set its body was measured to reach; the only function type that
    // says nothing is a parameter of function type, whose clause the callee writes at each
    // instantiation -- so this is read conservatively.
    const auto callee = transaction.graph().withInputsOf(id);
    if (callee.empty()) {
        return false;
    }
    Type *calleeType = nodeTypeOf(transaction.graph().ref(callee[0]));
    if (calleeType == nullptr || calleeType->code() != TypeCode::Function) {
        return false;
    }
    const auto *calleeFunc = tt::as_ptr<FunctionType>(calleeType);
    if (calleeFunc->resources().mode != ResourceSignature::Mode::Concrete) {
        readUnknownValue(walk);
        return true;
    }
    return readCalleeSignature(
        walk,
        graph,
        transaction,
        calleeFunc->resources(),
        calleeFunc->withTypesCount(),
        true,
        id,
        functionValueGraphOf(transaction.graph().ref(callee[0])));
}

void Builder::addVarParameters(BodyReach &walk, const FunctionType &type) const {
    // The declaration's own `var` parameters: a `var` parameter is a resource by itself, whether or
    // not the body reaches it (design principles 3.3). A parameter is not a node, so it is part of
    // the body's reach and not of any one node's.
    const size_t withCount = type.withTypesCount();
    size_t index           = 0;
    for (size_t i = 0; i < withCount + type.normTypesCount(); ++i) {
        const bool isWith = i < withCount;
        if (isWith ? type.withIsConstasAt(i) : type.normIsConstasAt(i - withCount)) {
            continue; // not a runtime parameter
        }
        const bool isVar = isWith ? type.withIsVarAt(i) : type.normIsVarAt(i - withCount);
        if (isVar) {
            addParamUse(
                walk,
                index,
                sem::ResourceClass::Variable,
                "",
                sem::AccessMode::ReadModifyWrite);
        }
        ++index;
    }
}

std::optional<camel::core::sem::ResourceSignature> Builder::footprintOf(
    DraftGraphBuilder *graph, std::optional<draft_node_ref_t> only,
    std::vector<std::string> *valueKeys) const {
    FunctionType *type = graph->funcType();
    if (type == nullptr) {
        return std::nullopt; // the body did not build (an error was reported)
    }
    const runtime::GraphTransaction &transaction = graph->transaction();
    const auto &draft                            = transaction.graph();
    BodyReach walk;
    walk.reach.mode = ResourceSignature::Mode::Concrete;
    // The result part is the conservative answer, not the default a `Fresh` result would claim: the
    // runtime reads this on the type once the measurement is published there, and a body that
    // returns the caller's cell is not a body whose result nothing else refers to.
    walk.reach.result = sem::ResultAlias{.kind = sem::ResultAlias::Kind::Unknown};
    numberBodyPorts(walk, graph, transaction, *type);
    // One node's own reach, asked for by the report of a violation or by the ordering of the
    // effects on one resource, is one node's walk: nothing else of the body is looked at.
    const auto walkNode = [&](draft_node_ref_t id) {
        switch (nodeKindOf(draft.ref(id))) {
        case runtime::GCNodeKind::Oper:
            return reachOfOper(graph, transaction, walk, id);
        case runtime::GCNodeKind::Func:
            return reachOfFunc(graph, transaction, walk, id);
        case runtime::GCNodeKind::Call:
            return reachOfCall(graph, transaction, walk, id);
        default:
            return true;
        }
    };
    if (only.has_value()) {
        if (!draft.alive(*only) || !walkNode(*only)) {
            return std::nullopt;
        }
    } else {
        for (draft_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
            if (draft.alive(id) && !walkNode(id)) {
                return std::nullopt;
            }
        }
    }
    if (walk.unplaceable) {
        return std::nullopt; // see addParamUse: nothing here is measured, rather than too little
    }
    if (valueKeys != nullptr) {
        // The resources this walk named by value (design effects plan 3.2). They are not part of
        // the signature -- a signature names its resources by parameter -- and only the ordering of
        // the effects on one resource reads them.
        *valueKeys = std::move(walk.valueKeys);
    }
    if (!only.has_value() && bodyResultCanHoldAFunction(*type)) {
        // A closure made here that captures a cell of this body can leave through the result --
        // the tuple or struct the body returns holds it -- and then the cell outlives the call, so
        // writing it is an effect of this body after all (design principles 3.3, effect masking).
        // Which closure the result actually holds is not traced: a body whose result can hold a
        // function and that makes a closure over one of its cells is measured as reaching that
        // cell. Refusing a `pure` promise costs a program; accepting one the body broke costs
        // correctness. A cell of this body can only be captured by a closure written inside it,
        // so the captures of every body are asked, without walking a single node.
        for (const auto &[closure, summary] : captures_) {
            (void)closure;
            for (node_handle_t cell : summary.variables) {
                if (nodeGraphOf(cell) == graph && isVariable(cell)) {
                    walk.reach.anyOf.push_back(sem::ResourceClass::Variable);
                }
            }
        }
    }
    if (!only.has_value()) {
        addVarParameters(walk, *type);
    }
    std::sort(walk.reach.params.begin(), walk.reach.params.end(), [](const auto &a, const auto &b) {
        return a.index < b.index;
    });
    std::sort(
        walk.reach.ambient.begin(),
        walk.reach.ambient.end(),
        [](const auto &a, const auto &b) { return a.name < b.name; });
    std::sort(walk.reach.anyOf.begin(), walk.reach.anyOf.end());
    walk.reach.anyOf.erase(
        std::unique(walk.reach.anyOf.begin(), walk.reach.anyOf.end()),
        walk.reach.anyOf.end());
    std::sort(walk.reach.captures.begin(), walk.reach.captures.end());
    walk.reach.captures.erase(
        std::unique(walk.reach.captures.begin(), walk.reach.captures.end()),
        walk.reach.captures.end());
    return walk.reach;
}

namespace {

// The resources a type publishes, when the type is one function's signature: the two sides of a
// conflict are function types, recorded where two values had to have the same resources
// (resource_conflict.h).
const camel::core::sem::ResourceSignature *signatureOf(const camel::core::type::Type *type) {
    if (type == nullptr || type->code() != camel::core::type::TypeCode::Function) {
        return nullptr;
    }
    return &static_cast<const camel::core::type::FunctionType *>(type)->resources();
}

} // namespace

bool Builder::recordResourceConflict(
    const camel::core::type::Type &left, const camel::core::type::Type &right) {
    if (!declaredFuncTypes_.contains(&left) && !declaredFuncTypes_.contains(&right)) {
        // Neither side is a declaration of this build: no body here decides their resources, so
        // there is nothing that settles -- the type layer keeps the reading it had.
        return false;
    }
    for (const ResourceConflict &recorded : resourceConflicts_) {
        const bool sameOrder = recorded.left == &left && recorded.right == &right;
        const bool swapped   = recorded.left == &right && recorded.right == &left;
        if (sameOrder || swapped) {
            return true;
        }
    }
    const camel::core::sem::ResourceSignature *leftSignature  = signatureOf(&left);
    const camel::core::sem::ResourceSignature *rightSignature = signatureOf(&right);
    ResourceConflict conflict;
    conflict.left  = &left;
    conflict.right = &right;
    // Which side wrote no clause is read here, before the measurement is written onto both: after
    // that a settled set and a written clause print the same, and the repair is the side that wrote
    // none.
    conflict.leftUnwritten =
        leftSignature != nullptr &&
        leftSignature->mode == camel::core::sem::ResourceSignature::Mode::Generic;
    conflict.rightUnwritten =
        rightSignature != nullptr &&
        rightSignature->mode == camel::core::sem::ResourceSignature::Mode::Generic;
    resourceConflicts_.push_back(conflict);
    return true;
}

void Builder::measureInferredResources() {
    // What a body touches depends on its callees, which may be written after their caller and in
    // another module, so the measurements settle against each other first -- and a body may call
    // itself, so they settle from the bottom: every body is recorded as reaching nothing, and each
    // round recomputes every body from what its callees currently reach, taking the union with what
    // that body reached before. A recursive function therefore reads its own reach from the round
    // before, and the answer it settles at is the least one its body satisfies. The sets only grow,
    // so the rounds end.
    std::vector<compile_graph_ptr_t> bodies;
    // TEST-ONLY, the R3 transition: a GraphId belongs to one program, and a later compile in this
    // process may give the same id to another body, so this compile's measurements replace the
    // earlier ones (resource_transition.h).
    clearMeasuredReaches();
    for (const ModuleUnit *unit : unitOrder_) {
        for (const compile_graph_ptr_t &graph : unit->builtBodies) {
            bodies.push_back(graph);
            inferredResources_[graph.get()] = emptyReach();
        }
    }
    // The bodies whose walk reads another body's signature: a direct call or a call through a
    // function value. Everything else is settled by its first walk, so only these are walked again.
    std::vector<DraftGraphBuilder *> unsettled;
    for (const auto &graph : bodies) {
        bool readsAnother = false;
        for (draft_node_ref_t id = 0; id < graph->view().nodeSlotCount() && !readsAnother; ++id) {
            if (!graph->view().alive(id)) {
                continue;
            }
            const auto kind = nodeKindOf(graph->view().ref(id));
            readsAnother = kind == runtime::GCNodeKind::Func || kind == runtime::GCNodeKind::Call;
        }
        if (readsAnother) {
            unsettled.push_back(graph.get());
        }
    }
    const auto walk = [&](DraftGraphBuilder *body) {
        const auto measured = footprintOf(body);
        // A body the walk cannot place reaches something unknown rather than nothing, so a clause
        // it wrote is still judged against it instead of passing unchecked.
        const auto settled = measured.has_value() ? uniteReach(inferredResources_[body], *measured)
                                                  : unplaceableReach();
        const bool grew    = !(settled == inferredResources_[body]);
        inferredResources_[body] = settled;
        return grew;
    };
    for (const auto &graph : bodies) {
        walk(graph.get());
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (DraftGraphBuilder *body : unsettled) {
            changed = walk(body) || changed;
        }
    }
    // The settled measurement is published on the signature of the bodies that carry an inferred
    // set rather than a clause, which keeps the signature the one place a function's resources are
    // read from: a caller that calls it directly reads the same thing a caller taking one of its
    // values reads (design principles 3.3, effects plan 5). A function that wrote a clause keeps
    // the clause: that is its contract with its callers. A set that cannot be written as a clause
    // (`anyOf`: a class the clause has no name for) leaves the signature as it is rather than
    // promising too little; a call through it is read conservatively.
    for (const auto &graph : bodies) {
        const auto &settled = inferredResources_[graph.get()];
        // TEST-ONLY, the R3 transition: the settled reach of every body, including a set the clause
        // language cannot carry and a body whose type keeps the clause it wrote. A type is the
        // place a caller reads a function's resources from; it is not the whole of what this walk
        // measured, and the case that asks both copies of the masking rule the same question reads
        // the measurement here (resource_transition.h).
        publishMeasuredReach(graph->graphId(), settled);
        if (!inferredSignatures_.contains(graph.get())) {
            continue;
        }
        if (settled.anyOf.empty()) {
            // The capability is not published: it is not one of the resources a caller has to be
            // told about -- a caller reads it from this measurement (see `readCalleeSignature`)
            // or from the clause the declaration wrote -- and a signature that showed `spelling`
            // for a body that wrote no clause would be a type claiming a promise its source never
            // made (design principles 9.5). It stays in `inferredResources_`, which is what a
            // caller and the check read.
            ResourceSignature published = settled;
            published.spelling          = false;
            graph->setResources(published);
        }
    }
}

namespace {

// One resource a node reaches, named the way the body it is written in names it: a parameter of
// the body, or an ambient resource (`stdout`, `fs`) by the name the declaration gave it. Two
// effects reach the same resource exactly when their keys are equal (design principles 3.3);
// a node whose reach the walk cannot place is `unnamed` and stands for every resource at once.
// `unordered` ambient resources (`resource net unordered`) are left out: effects on one of them
// keep no written order, so no order edge joins them (design principles 3.3).
struct ResourceKeys {
    std::vector<std::string> keys;
    bool unnamed = false;
};

// What the positions of a `sync` block's chain would ask -- one statement follows the other -- is
// not repeated here: this only says which effects touch one resource, and the chain keeps its own
// statement order. An effect whose reach cannot be placed is ordered against everything, the way
// an undeclared operator is read as touching "全局" (design principles 3.3).
ResourceKeys resourceKeysOf(const camel::core::sem::ResourceSignature &reach) {
    ResourceKeys out;
    for (const auto &use : reach.params) {
        out.keys.push_back(std::format("p{}", static_cast<int>(use.index)));
    }
    for (const auto &use : reach.ambient) {
        if (use.unordered) {
            continue;
        }
        out.keys.push_back(std::format("a{}", use.name));
    }
    out.unnamed = !reach.anyOf.empty();
    return out;
}

// The last effect on each resource of one block, and the last effect whose resources could not be
// named (it stands for all of them).
struct BlockEffectOrder {
    std::map<std::string, draft_node_ref_t> lastOn;
    draft_node_ref_t lastUnnamed = 0;
};

} // namespace

void Builder::orderEffectsOnOneResource() {
    // Design principles 3.3: "同一资源自动有序" -- within one function body, effects that really
    // run and reach one resource run in the order they are written, and the compiler writes an
    // order edge between them. Without it, `let x = println(9); let y = println(0)` is left to the
    // schedule, and the node VM prints `0` before `9`.
    //
    // The edge is an order edge, not a version of the resource: the console is ambient state
    // (design principles 3.3), so there is no value that one write hands to the next one, and a
    // data edge would carry "used" with it -- it would make the earlier effect run merely because
    // a later one does, which is exactly what 3.1 refuses ("顺序不决定是否执行"). A `var` cell
    // does have a version: its writes and reads are data edges already (recordWrite,
    // collapseNonEscapingVariableReads), so a cell write or read reaching this walk is a pair that
    // is ordered by its value, and no edge is added beside it.
    //
    // It runs after measureInferredResources: a call's resources are the ones its callee settled
    // at, so the pairs can only be read once every body is built and the measurements agree.
    for (const ModuleUnit *unit : unitOrder_) {
        for (const compile_graph_ptr_t &graph : unit->builtBodies) {
            orderEffectsOnOneResourceIn(graph.get());
        }
    }
}

void Builder::orderEffectsOnOneResourceIn(DraftGraphBuilder *graph) {
    if (graph == nullptr || graph->funcType() == nullptr) {
        return;
    }
    const auto &draft = graph->transaction().graph();
    // The block an effect is written in decides which effects it is ordered with: an arm and a
    // part of a loop separate blocks, and the regions that only annotate where a node came from
    // are looked through (the notion the seal check and the layout use).
    std::map<uint32_t, BlockEffectOrder> blocks;
    // An effect is written in one block, and it also stands in every block further out, on the node
    // that owns the region between them: an owner runs as a whole with what is under it
    // (docs/design/effects/edges.cn.md 4.6, `nodeStandsForItsRegion`), and the owner is the node
    // the commit's order check pairs a neighbour with (`runtime::orderIncomplete`: "an owner node
    // answers for what is under it, and it is the owner that stands in the region around it").
    // Recording an arm's effect in the arm's block alone therefore left a statement written before
    // (or after) the branch and an effect inside it reaching one resource with no edge between
    // them -- the shape the front end published, and the shape an inline splice copies verbatim
    // (edges.cn.md 4.2, 5.7), so the first commit that saw the copy refused it (design principles
    // 3.3: effects on one resource run in the order they are written).
    // `orderedAlready` is asked only where the node standing for a region meets its own block: the
    // block may already order the two -- a statement of a `sync` block is chained, and the chain
    // says every statement runs after the one before it -- and a second edge beside that path would
    // say the same thing twice. The effect's own level keeps the edges it always wrote.
    //
    // The walk follows every kind of edge the check itself follows (`runtime::orderIncomplete`:
    // data, sync and order); a data path is the same question the caller asked above, and asking it
    // here as well is what keeps an effect inside a loop body from hanging an edge on the `LOOP`
    // when the loop's carried state already ties it to what was written before.
    const auto orderedAlready = [&](draft_node_ref_t from, draft_node_ref_t to) {
        std::vector<draft_node_ref_t> pending{to};
        std::unordered_set<draft_node_ref_t> seen{to};
        const auto push = [&](std::span<const runtime::gc_node_ref_t> inputs) {
            for (const runtime::gc_node_ref_t input : inputs) {
                if (seen.insert(input).second) {
                    pending.push_back(input);
                }
            }
        };
        while (!pending.empty()) {
            const draft_node_ref_t here = pending.back();
            pending.pop_back();
            if (here == from) {
                return true;
            }
            push(draft.normInputsOf(here));
            push(draft.withInputsOf(here));
            push(draft.ctrlInputsOf(here));
            push(draft.syncInputsOf(here));
            push(draft.orderInputsOf(here));
        }
        return false;
    };
    const auto record = [&](uint32_t blockRegion,
                            draft_node_ref_t stands,
                            const ResourceKeys &named,
                            bool orderedAtItsRegion) {
        BlockEffectOrder &block = blocks[blockRegion];
        std::vector<draft_node_ref_t> before;
        const auto addBefore = [&](draft_node_ref_t from) {
            if (from != 0 && from != stands && std::ranges::find(before, from) == before.end()) {
                before.push_back(from);
            }
        };
        if (named.unnamed) {
            // Everything written before it in the block is before it, whatever it touched.
            for (const auto &entry : block.lastOn) {
                addBefore(entry.second);
            }
            addBefore(block.lastUnnamed);
        } else {
            for (const std::string &key : named.keys) {
                const auto found = block.lastOn.find(key);
                if (found != block.lastOn.end()) {
                    addBefore(found->second);
                }
            }
            addBefore(block.lastUnnamed);
        }
        const node_handle_t target = draft.ref(stands);
        for (draft_node_ref_t from : before) {
            const node_handle_t source = draft.ref(from);
            if (!nodeTakesAnOrder(source) || !linkCheek(source, target)) {
                continue;
            }
            // An edge the data path already carries says nothing, and one against it would be
            // a cycle (design principles 2.1: the read and the write of `x = x + 1` are
            // ordered by the value, not by a second edge).
            if (hasDirectDataDependency(source, target) || dataPathReaches(source, target) ||
                dataPathReaches(target, source)) {
                continue;
            }
            // A path from that node to this one already exists: the graph says the order
            // twice over, and the commit's check asks for a path, not for an edge.
            if (orderedAtItsRegion && orderedAlready(from, stands)) {
                continue;
            }
            linkNodes(LinkType::Order, source, target);
        }
        if (named.unnamed) {
            // What it was ordered after is ordered after this one now: keeping both would say
            // the same thing twice, and this node stands for every resource.
            block.lastOn.clear();
            block.lastUnnamed = stands;
        } else {
            for (const std::string &key : named.keys) {
                block.lastOn[key] = stands;
            }
        }
    };
    for (draft_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        if (!draft.alive(id)) {
            continue;
        }
        const node_handle_t node = draft.ref(id);
        // Only an effect carries the ordering ("两端都是效应节点或可能出故障的节点", 4.6), and a
        // node the model lets a pass arrange freely carries none.
        if (!isEffectNode(graph, node) || !nodeTakesAnOrder(node)) {
            continue;
        }
        std::vector<std::string> valueKeys;
        const auto reach = footprintOf(graph, id, &valueKeys);
        ResourceKeys named;
        if (reach.has_value()) {
            named = resourceKeysOf(*reach);
            for (std::string &key : valueKeys) {
                if (key.empty()) {
                    // A handle access the walk cannot pin to one box reaches any resource of its
                    // class, the way an undeclared operator does (design principles 3.3).
                    named.unnamed = true;
                    continue;
                }
                named.keys.push_back(std::move(key));
            }
            // One key per resource, however many of a node's accesses name it.
            std::sort(named.keys.begin(), named.keys.end());
            named.keys.erase(std::unique(named.keys.begin(), named.keys.end()), named.keys.end());
        } else {
            named.unnamed = true;
        }
        if (!named.unnamed && named.keys.empty()) {
            // An effect the walk places on no resource orders with nothing -- and an effect whose
            // only resource is `unordered` is one of those: the resource itself says the order the
            // program wrote does not matter (design principles 3.3), so the plan is free to run
            // two of them at the same time and `std::nvm` runs them one after the other.
            continue;
        }
        // The effect is written in one block, and it stands in every block around that one too:
        // here, and then on the node owning each region it lies in, in the block that region sits
        // in (the same two depths `runtime::orderIncomplete` pairs).
        uint32_t block = runtime::controlRegionOf(draft, id);
        record(block, id, named, false);
        for (size_t steps = 0; block != 0 && steps <= draft.regionCount(); ++steps) {
            const auto &region = draft.region(block);
            if (region.owner == 0) {
                break; // a region that only annotates: `controlRegionOf` already looked through it
            }
            const uint32_t outer = runtime::controlRegionOf(draft, region.owner);
            if (outer == block) {
                break;
            }
            record(outer, region.owner, named, true);
            block = outer;
        }
    }
}

camel::source::origin_id_t Builder::firstUnpromisedEffectOrigin(
    const compile_graph_ptr_t &graph, const camel::core::sem::ResourceSignature &promised) const {
    // The first use the clause does not name is what the writer has to look at: the declaration
    // says what a caller may assume, and this is where the body stopped matching it. A use the
    // clause does name is not that place, so it is skipped rather than reported.
    for (draft_node_ref_t id = 0; id < graph->view().nodeSlotCount(); ++id) {
        if (!graph->view().alive(id)) {
            continue;
        }
        const node_handle_t node = graph->view().ref(id);
        if (!isEffectNode(graph.get(), node)) {
            continue;
        }
        const auto reach = footprintOf(graph.get(), id);
        if (!reach.has_value() || sem::covers(promised, *reach)) {
            continue; // promised, or not placeable: not what the clause left out
        }
        return nodeDraftOf(node)->nodeOrigin(nodeIdOf(node));
    }
    return 0;
}

void Builder::checkInferredResources() {
    using sem::ResourceSignature;
    // A lambda written in place has no clause of its own to write: its signature is the one the
    // position it stands in expects (design principles 5.9), so the capability it reads is the
    // enclosing body's, and that body is the one this check asks -- a lambda's reach joins it
    // through the argument it is handed as. The bodies written in place are the values
    // `lambdaGraphs_` holds (they are the ones the program wrote `(x) => ...` for).
    std::unordered_set<const DraftGraphBuilder *> writtenInPlace;
    writtenInPlace.reserve(lambdaGraphs_.size());
    for (const auto &[node, body] : lambdaGraphs_) {
        (void)node;
        if (body != nullptr) {
            writtenInPlace.insert(body);
        }
    }
    for (const compile_graph_ptr_t &graph : unit_->builtBodies) {
        const auto measured = inferredResources_.find(graph.get());
        if (measured == inferredResources_.end()) {
            continue;
        }
        FunctionType *type = graph->funcType();
        if (type == nullptr) {
            continue;
        }
        const ResourceSignature &promised = type->resources();
        // The spelling is checked whatever the clause says, and before the clause's mode is read: a
        // declaration that writes no clause promises nothing about resources, but it does promise
        // its body reads no spelling -- that is the guarantee design principles 9.5 makes. A body
        // that reaches the spelling is refused here whether or not it also overruns its resource
        // promise.
        //
        // What is read is the clause, and only the clause: the published signature of a body that
        // wrote none deliberately carries no `spelling` (see measureInferredResources), so this
        // asks the declaration the writer made and not the body's own reach.
        const bool writtenHere = writtenInPlace.contains(graph.get());
        if (measured->second.spelling && !writtenHere && !promised.spelling) {
            reportSpellingNotDeclared(graph);
            continue;
        }
        if (promised.mode != ResourceSignature::Mode::Concrete) {
            continue; // the declaration wrote no clause: it promised nothing to break
        }
        if (sem::covers(promised, measured->second)) {
            continue;
        }
        reportResourceViolation(
            graph,
            promised,
            measured->second,
            firstUnpromisedEffectOrigin(graph, promised));
    }
    // A place where one type variable was asked for two function types that write different
    // resources (recordResourceConflict). Every body has settled by now, so the question has one
    // answer, whichever order the two bodies were built in -- and it is not the place that decides:
    // the two sets are what the two bodies were measured to reach, and a position that asks for one
    // type does not take one of them for the other's.
    const auto ownerOf = [&](const camel::core::type::Type *type) -> DraftGraphBuilder * {
        const auto found = declaredFuncTypes_.find(type);
        return found == declaredFuncTypes_.end() ? nullptr : found->second;
    };
    const auto isHere = [&](DraftGraphBuilder *graph) {
        return graph != nullptr &&
               std::any_of(
                   unit_->builtBodies.begin(),
                   unit_->builtBodies.end(),
                   [&](const compile_graph_ptr_t &body) { return body.get() == graph; });
    };
    for (size_t index = 0; index < resourceConflicts_.size(); ++index) {
        if (reportedResourceConflicts_.contains(index)) {
            continue;
        }
        const ResourceConflict &conflict = resourceConflicts_[index];
        DraftGraphBuilder *leftOwner     = ownerOf(conflict.left);
        DraftGraphBuilder *rightOwner    = ownerOf(conflict.right);
        if (!isHere(leftOwner) && !isHere(rightOwner)) {
            continue; // reported with the module of the declaration it is about
        }
        reportedResourceConflicts_.insert(index);
        const auto *leftSignature  = signatureOf(conflict.left);
        const auto *rightSignature = signatureOf(conflict.right);
        if (leftSignature == nullptr || rightSignature == nullptr) {
            continue;
        }
        // A set that could not be written as a clause (`anyOf`), and a declaration whose body was
        // never built, are not this build's to judge: a call through them is read conservatively.
        if (leftSignature->mode != ResourceSignature::Mode::Concrete ||
            rightSignature->mode != ResourceSignature::Mode::Concrete) {
            continue;
        }
        if (*leftSignature == *rightSignature) {
            continue; // the two agree: the variable is one type after all
        }
        reportResourceConflict(conflict, leftOwner, rightOwner);
    }
}

// ---- The transition bridge (TEST-ONLY; see resource_transition.h) -------------------------------

namespace {

// The reach of every body of the compile in hand, by the GraphId the frozen graph keeps. One table
// for the process: a compile replaces its entries (measureInferredResources clears them first), and
// the test that reads them does so in the process that wrote them.
std::unordered_map<runtime::GraphId, ResourceSignature> &measuredReaches() {
    static std::unordered_map<runtime::GraphId, ResourceSignature> table;
    return table;
}

} // namespace

void publishMeasuredReach(runtime::GraphId graph, const ResourceSignature &reach) {
    measuredReaches().insert_or_assign(graph, reach);
}

const ResourceSignature *measuredReachOf(runtime::GraphId graph) {
    const auto &table = measuredReaches();
    const auto found  = table.find(graph);
    return found == table.end() ? nullptr : &found->second;
}

void clearMeasuredReaches() { measuredReaches().clear(); }

} // namespace camel::compile::gir
