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

#include "camel/core/operator_names.h"
#include "camel/core/operator_table.h"
#include "camel/utils/type.h"

#include <algorithm>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
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
    for (ResourceClass cls : {ResourceClass::Variable, ResourceClass::Object, ResourceClass::Foreign,
                              ResourceClass::Ambient, ResourceClass::Handle}) {
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
        const auto found = std::find_if(united.params.begin(), united.params.end(),
                                        [&](const auto &other) { return other.index == use.index; });
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
        const auto found = std::find_if(united.ambient.begin(), united.ambient.end(),
                                        [&](const auto &other) { return other.name == use.name; });
        if (found == united.ambient.end()) {
            united.ambient.push_back(use);
            continue;
        }
        found->mode = sem::uniteAccess(found->mode, use.mode);
    }
    united.captures.insert(united.captures.end(), b.captures.begin(), b.captures.end());
    united.anyOf.insert(united.anyOf.end(), b.anyOf.begin(), b.anyOf.end());
    united.result = b.result;
    if (!united.allocates && b.allocates.has_value()) {
        united.allocates = b.allocates;
    }
    united.external = united.external || b.external;
    std::sort(united.params.begin(), united.params.end(),
              [](const auto &left, const auto &right) { return left.index < right.index; });
    std::sort(united.ambient.begin(), united.ambient.end(),
              [](const auto &left, const auto &right) { return left.name < right.name; });
    std::sort(united.captures.begin(), united.captures.end());
    united.captures.erase(std::unique(united.captures.begin(), united.captures.end()),
                          united.captures.end());
    std::sort(united.anyOf.begin(), united.anyOf.end());
    united.anyOf.erase(std::unique(united.anyOf.begin(), united.anyOf.end()), united.anyOf.end());
    return united;
}

} // namespace

void Builder::numberBodyPorts(BodyReach &walk, const runtime::GraphTransaction &transaction,
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
    // it; so is a closure node. A use through one is not a use of a parameter of this function, and
    // which resource of the enclosing function it is cannot be read from here.
    for (; withAt < withPorts.size(); ++withAt) {
        walk.captured.insert(withPorts[withAt]);
    }
    for (draft_node_ref_t node : draft.closureNodes()) {
        walk.captured.insert(node);
    }
}

void Builder::addParamUse(BodyReach &walk, size_t index, sem::ResourceClass cls,
                          const std::string &name, sem::AccessMode mode) const {
    if (index > 0xFF) {
        // A signature numbers its parameters in one byte, so a use through the 256th parameter has
        // no way to be written down. Dropping it would measure the body as reaching less than it
        // does, and a `pure` body would pass on a use nobody recorded. Nothing here is measured
        // instead.
        walk.unplaceable = true;
        return;
    }
    ResourceSignature &out = walk.reach;
    const auto found       = std::find_if(out.params.begin(), out.params.end(), [&](const auto &use) {
        return use.index == static_cast<uint8_t>(index);
    });
    if (found == out.params.end()) {
        out.params.push_back(ResourceSignature::ParamUse{
            .index = static_cast<uint8_t>(index), .cls = cls, .name = name, .mode = mode});
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

std::optional<std::string> Builder::moduleVariableOf(const runtime::GraphTransaction &transaction,
                                                    draft_node_ref_t port) const {
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

void Builder::addAmbientUse(BodyReach &walk, const std::string &name, sem::AccessMode mode) const {
    ResourceSignature &out = walk.reach;
    const auto found =
        std::find_if(out.ambient.begin(), out.ambient.end(), [&](const auto &use) {
            return use.name == name;
        });
    if (found == out.ambient.end()) {
        out.ambient.push_back(ResourceSignature::AmbientUse{.name = name, .mode = mode});
        return;
    }
    found->mode = sem::uniteAccess(found->mode, mode);
}

bool Builder::readCalleeSignature(BodyReach &walk, const runtime::GraphTransaction &transaction,
                                  const ResourceSignature &sig, size_t calleeWithCount, bool isCall,
                                  draft_node_ref_t id) const {
    // What one node's signature amounts to for this body: its own parameters, in the numbering the
    // callee uses (with parameters first, then ordinary ones -- the same mapping as
    // runtime/resources.cpp fromSignature), read back onto the parameters of this body where the
    // operand is one of them. False when the signature says nothing (Generic), or says something
    // this measurement cannot place: the body is then not measured at all, rather than measured as
    // pure because the walk gave up.
    if (sig.mode != ResourceSignature::Mode::Concrete) {
        return false;
    }
    if (!sig.captures.empty()) {
        // A capture is a resource of the closure this body belongs to, and which capture of the
        // enclosing function it is cannot be read from here.
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
        if (walk.captured.contains(operand)) {
            if (const auto variable = moduleVariableOf(transaction, operand);
                variable.has_value()) {
                addAmbientUse(walk, *variable, use.mode); // a variable of a module's top level
                continue;
            }
            return false; // a resource of the function around this one
        }
        const auto port = walk.ports.find(operand);
        if (port == walk.ports.end()) {
            // The operand is a value the body computed, not one of its caller's: a use through it
            // is not a use of a resource the caller still holds.
            continue;
        }
        addParamUse(walk, port->second, use.cls, use.name, use.mode);
    }
    for (const auto &use : sig.ambient) {
        addAmbientUse(walk, use.name, use.mode);
    }
    for (sem::ResourceClass cls : sig.anyOf) {
        walk.reach.anyOf.push_back(cls);
    }
    if (sig.allocates) {
        walk.reach.allocates = sig.allocates;
    }
    walk.reach.external = walk.reach.external || sig.external;
    return true;
}

void Builder::readUnknownValue(BodyReach &walk) const {
    // A signature that says nothing promises nothing, and what a call through it reaches is the
    // instantiation's: any instantiation may write a cell or print. Read conservatively
    // (resource_semantics.h, `Generic`) rather than as touching nothing -- a body is never measured
    // as pure because the walk found a signature it could not read. What has such a signature is a
    // parameter of function type that writes no clause of its own (resource-generic, design
    // principles 3.3): a function that wrote a clause carries the clause, and a function that wrote
    // none carries the set its own body was measured to reach, so those two are read above.
    for (sem::ResourceClass cls : {sem::ResourceClass::Variable, sem::ResourceClass::Object,
                                   sem::ResourceClass::Foreign, sem::ResourceClass::Ambient,
                                   sem::ResourceClass::Handle}) {
        walk.reach.anyOf.push_back(cls);
    }
    walk.reach.external = true;
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
    bool declared = false;
    if (const auto found = table.lookup(uri); found.has_value()) {
        if (const auto *facts = table.semantics(*found); facts != nullptr && facts->resources) {
            spec     = *facts->resources;
            declared = true;
        }
    }
    if (!declared) {
        spec = sem::conservativeResources(draft.normInputsOf(id).size(),
                                          draft.withInputsOf(id).size(),
                                          traits.has_value() && traits->pure,
                                          table.hasExternalEffect(uri));
    }
    for (const sem::ResourceAccess &access : spec.accesses) {
        if (access.binding.kind == sem::ResourceBinding::Kind::Ambient) {
            addAmbientUse(walk, access.resource.name, access.mode);
            continue;
        }
        if (access.binding.kind == sem::ResourceBinding::Kind::Capture) {
            return false; // which capture of this closure it is, is not readable
        }
        const bool isWith = access.binding.kind == sem::ResourceBinding::Kind::With;
        const auto inputs = isWith ? draft.withInputsOf(id) : draft.normInputsOf(id);
        // A call through a value keeps the value itself as its first `with` input, so its `with`
        // operands start after it. An operator node has no callee among its inputs -- what it calls
        // is the operator the table holds -- and its `with` operands are the call's `with` arguments
        // as written. Reading the node's kind is what runtime/resources.cpp does for the same
        // mapping; counting the callee on an operator node costs the whole body its measurement
        // (the operand lands past the last `with` input and the walk gives up).
        const bool isCall = nodeKindOf(draft.ref(id)) == runtime::GCNodeKind::Call;
        const size_t at = size_t(access.binding.index) + (isWith && isCall ? 1 : 0);
        if (at >= inputs.size()) {
            return false;
        }
        if (walk.captured.contains(inputs[at])) {
            if (const auto variable = moduleVariableOf(transaction, inputs[at]);
                variable.has_value()) {
                addAmbientUse(walk, *variable, access.mode); // a variable of a module's top level
                continue;
            }
            return false; // a resource of the function around this one
        }
        const auto port = walk.ports.find(inputs[at]);
        if (port == walk.ports.end()) {
            continue; // a value the body computed, not one of its caller's
        }
        addParamUse(walk, port->second, access.resource.cls, access.resource.name, access.mode);
    }
    walk.reach.external = walk.reach.external || spec.externalEffect;
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
        return readCalleeSignature(walk, transaction, inferred->second,
                                   calleeType->withTypesCount(), false, id);
    }
    if (calleeType->resources().mode != ResourceSignature::Mode::Concrete) {
        readUnknownValue(walk);
        return true;
    }
    return readCalleeSignature(walk, transaction, calleeType->resources(),
                               calleeType->withTypesCount(), false, id);
}

bool Builder::reachOfCall(
    const runtime::GraphTransaction &transaction, BodyReach &walk, draft_node_ref_t id) const {
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
    return readCalleeSignature(walk, transaction, calleeFunc->resources(),
                               calleeFunc->withTypesCount(), true, id);
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
            addParamUse(walk, index, sem::ResourceClass::Variable, "",
                        sem::AccessMode::ReadModifyWrite);
        }
        ++index;
    }
}

std::optional<camel::core::sem::ResourceSignature>
Builder::footprintOf(DraftGraphBuilder *graph, std::optional<draft_node_ref_t> only) const {
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
    numberBodyPorts(walk, transaction, *type);
    // One node's own reach, asked for by the report of a violation or by the ordering of the
    // effects on one resource, is one node's walk: nothing else of the body is looked at.
    const auto walkNode = [&](draft_node_ref_t id) {
        switch (nodeKindOf(draft.ref(id))) {
        case runtime::GCNodeKind::Oper:
            return reachOfOper(graph, transaction, walk, id);
        case runtime::GCNodeKind::Func:
            return reachOfFunc(graph, transaction, walk, id);
        case runtime::GCNodeKind::Call:
            return reachOfCall(transaction, walk, id);
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
    if (!only.has_value()) {
        addVarParameters(walk, *type);
    }
    std::sort(walk.reach.params.begin(), walk.reach.params.end(),
              [](const auto &a, const auto &b) { return a.index < b.index; });
    std::sort(walk.reach.ambient.begin(), walk.reach.ambient.end(),
              [](const auto &a, const auto &b) { return a.name < b.name; });
    std::sort(walk.reach.anyOf.begin(), walk.reach.anyOf.end());
    walk.reach.anyOf.erase(std::unique(walk.reach.anyOf.begin(), walk.reach.anyOf.end()),
                           walk.reach.anyOf.end());
    return walk.reach;
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
        for (draft_node_ref_t id = 0; id < graph->draft().nodeSlotCount() && !readsAnother; ++id) {
            if (!graph->draft().alive(id)) {
                continue;
            }
            const auto kind = nodeKindOf(graph->draft().ref(id));
            readsAnother    = kind == runtime::GCNodeKind::Func || kind == runtime::GCNodeKind::Call;
        }
        if (readsAnother) {
            unsettled.push_back(graph.get());
        }
    }
    const auto walk = [&](DraftGraphBuilder *body) {
        const auto measured = footprintOf(body);
        // A body the walk cannot place reaches something unknown rather than nothing, so a clause
        // it wrote is still judged against it instead of passing unchecked.
        const auto settled =
            measured.has_value() ? uniteReach(inferredResources_[body], *measured)
                                 : unplaceableReach();
        const bool grew = !(settled == inferredResources_[body]);
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
        if (!inferredSignatures_.contains(graph.get())) {
            continue;
        }
        const auto &settled = inferredResources_[graph.get()];
        if (settled.anyOf.empty()) {
            graph->setResources(settled);
        }
    }
}

namespace {

// One resource a node reaches, named the way the body it is written in names it: a parameter of
// the body, or an ambient resource (`stdout`, `fs`) by the name the declaration gave it. Two
// effects reach the same resource exactly when their keys are equal (design principles 3.3);
// a node whose reach the walk cannot place is `unnamed` and stands for every resource at once.
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
        const auto reach = footprintOf(graph, id);
        ResourceKeys named;
        if (reach.has_value()) {
            named = resourceKeysOf(*reach);
        } else {
            named.unnamed = true;
        }
        if (!named.unnamed && named.keys.empty()) {
            continue; // an effect the walk places on no resource orders with nothing
        }
        BlockEffectOrder &block = blocks[runtime::controlRegionOf(draft, id)];
        std::vector<draft_node_ref_t> before;
        const auto addBefore = [&](draft_node_ref_t from) {
            if (from != 0 && from != id && std::ranges::find(before, from) == before.end()) {
                before.push_back(from);
            }
        };
        if (named.unnamed) {
            // Everything written before it in the block is before it, whatever it touched.
            for (const auto &[key, from] : block.lastOn) {
                addBefore(from);
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
        for (draft_node_ref_t from : before) {
            const node_handle_t source = draft.ref(from);
            if (!nodeTakesAnOrder(source) || !linkCheek(source, node)) {
                continue;
            }
            // An edge the data path already carries says nothing, and one against it would be a
            // cycle (design principles 2.1: the read and the write of `x = x + 1` are ordered by
            // the value, not by a second edge).
            if (hasDirectDataDependency(source, node) || dataPathReaches(source, node) ||
                dataPathReaches(node, source)) {
                continue;
            }
            linkNodes(LinkType::Order, source, node);
        }
        if (named.unnamed) {
            // What it was ordered after is ordered after this one now: keeping both would say the
            // same thing twice, and this node stands for every resource.
            block.lastOn.clear();
            block.lastUnnamed = id;
        } else {
            for (const std::string &key : named.keys) {
                block.lastOn[key] = id;
            }
        }
    }
}

camel::source::origin_id_t Builder::firstUnpromisedEffectOrigin(
    const compile_graph_ptr_t &graph, const camel::core::sem::ResourceSignature &promised) const {
    // The first use the clause does not name is what the writer has to look at: the declaration
    // says what a caller may assume, and this is where the body stopped matching it. A use the
    // clause does name is not that place, so it is skipped rather than reported.
    for (draft_node_ref_t id = 0; id < graph->draft().nodeSlotCount(); ++id) {
        if (!graph->draft().alive(id)) {
            continue;
        }
        const node_handle_t node = graph->draft().ref(id);
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
        if (promised.mode != ResourceSignature::Mode::Concrete) {
            continue; // the declaration wrote no clause: it promised nothing to break
        }
        if (sem::covers(promised, measured->second)) {
            continue;
        }
        reportResourceViolation(
            graph, promised, measured->second,
            firstUnpromisedEffectOrigin(graph, promised));
    }
}
} // namespace camel::compile::gir