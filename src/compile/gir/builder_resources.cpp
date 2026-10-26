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
// body is ever below it.
ResourceSignature emptyReach() {
    ResourceSignature reach;
    reach.mode = ResourceSignature::Mode::Concrete;
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
    const auto uniteMode      = [](AccessMode left, AccessMode right) {
        return sem::writes(left) || sem::writes(right) ? AccessMode::ReadModifyWrite
                                                       : AccessMode::Read;
    };
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
        found->mode = uniteMode(found->mode, use.mode);
    }
    for (const auto &use : b.ambient) {
        const auto found = std::find_if(united.ambient.begin(), united.ambient.end(),
                                        [&](const auto &other) { return other.name == use.name; });
        if (found == united.ambient.end()) {
            united.ambient.push_back(use);
            continue;
        }
        found->mode = uniteMode(found->mode, use.mode);
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

std::optional<camel::core::sem::ResourceSignature>
Builder::footprintOf(DraftGraphBuilder *graph, std::optional<draft_node_ref_t> only) const {
    using sem::ResourceSignature;
    const runtime::GraphDraft &draft = graph->draft();
    FunctionType *type               = graph->funcType();
    if (type == nullptr) {
        return std::nullopt; // the body did not build (an error was reported)
    }
    ResourceSignature out;
    out.mode = ResourceSignature::Mode::Concrete;
    const auto uniteMode = [](sem::AccessMode a, sem::AccessMode b) {
        return sem::writes(a) || sem::writes(b) ? sem::AccessMode::ReadModifyWrite
                                                : sem::AccessMode::Read;
    };
    bool unplaceable = false;
    const auto addParam = [&](size_t index, sem::ResourceClass cls, const std::string &name,
                              sem::AccessMode mode) {
        if (index > 0xFF) {
            // A signature numbers its parameters in one byte, so a use through the 256th parameter
            // has no way to be written down. Dropping it would measure the body as reaching less
            // than it does, and a `pure` body would pass on a use nobody recorded. Nothing here is
            // measured instead.
            unplaceable = true;
            return;
        }
        const auto found = std::find_if(out.params.begin(), out.params.end(), [&](const auto &use) {
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
        found->mode = uniteMode(found->mode, mode);
    };
    const auto addAmbient = [&](const std::string &name, sem::AccessMode mode) {
        const auto found =
            std::find_if(out.ambient.begin(), out.ambient.end(), [&](const auto &use) {
                return use.name == name;
            });
        if (found == out.ambient.end()) {
            out.ambient.push_back(ResourceSignature::AmbientUse{.name = name, .mode = mode});
            return;
        }
        found->mode = uniteMode(found->mode, mode);
    };
    // Which parameter a port is, in the numbering a call uses: `with` parameters first, then the
    // ordinary ones (a `constas` parameter is not a runtime value and has no port).
    const size_t withCount = type->withTypesCount();
    std::unordered_map<draft_node_ref_t, size_t> portIndex;
    {
        const auto withPorts = draft.withPorts();
        const auto normPorts = draft.normPorts();
        size_t index = 0, withAt = 0, normAt = 0;
        for (size_t i = 0; i < withCount + type->normTypesCount(); ++i) {
            const bool isWith = i < withCount;
            if (isWith) {
                if (withAt < withPorts.size()) {
                    portIndex.emplace(withPorts[withAt++], index);
                }
            } else {
                if (type->normIsConstasAt(i - withCount)) {
                    continue; // not a runtime parameter
                }
                if (normAt < normPorts.size()) {
                    portIndex.emplace(normPorts[normAt++], index);
                }
            }
            ++index;
        }
    }
    // What one node's signature amounts to for this body: its own parameters, in the numbering
    // the callee uses (with parameters first, then ordinary ones -- the same mapping as
    // runtime/resources.cpp fromSignature), read back onto the parameters of this body where the
    // operand is one of them. False when the signature says nothing (Generic), or says something
    // this measurement cannot place: the body is then not measured at all, rather than measured
    // as pure because the walk gave up.
    const auto readSignature = [&](const ResourceSignature &sig, size_t calleeWithCount,
                                   bool isCall, draft_node_ref_t id) -> bool {
        if (sig.mode != ResourceSignature::Mode::Concrete) {
            return false;
        }
        if (!sig.captures.empty()) {
            // A capture is a resource of the closure this body belongs to, and which capture of
            // the enclosing function it is cannot be read from here.
            return false;
        }
        const auto norm = draft.normInputsOf(id);
        const auto with = draft.withInputsOf(id);
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
            const auto at = portIndex.find(operand);
            if (at == portIndex.end()) {
                // The operand is a value the body computed, not one of its caller's: a use
                // through it is not a use of a resource the caller still holds.
                continue;
            }
            addParam(at->second, use.cls, use.name, use.mode);
        }
        for (const auto &use : sig.ambient) {
            addAmbient(use.name, use.mode);
        }
        for (sem::ResourceClass cls : sig.anyOf) {
            out.anyOf.push_back(cls);
        }
        if (sig.allocates) {
            out.allocates = sig.allocates;
        }
        out.external = out.external || sig.external;
        return true;
    };
    // A signature that says nothing promises nothing, and what a call through it reaches is the
    // instantiation's: any instantiation may write a cell or print. Read conservatively
    // (resource_semantics.h, `Generic`) rather than as touching nothing -- a body is never measured
    // as pure because the walk found a signature it could not read. What has such a signature is a
    // parameter of function type that writes no clause of its own (resource-generic, design
    // principles 3.3): a function that wrote a clause carries the clause, and a function that wrote
    // none carries the set its own body was measured to reach, so those two are read above.
    const auto readUnknownValue = [&]() {
        for (sem::ResourceClass cls : {sem::ResourceClass::Variable,
                                       sem::ResourceClass::Object,
                                       sem::ResourceClass::Foreign,
                                       sem::ResourceClass::Ambient,
                                       sem::ResourceClass::Handle}) {
            out.anyOf.push_back(cls);
        }
        out.external = true;
    };
    for (draft_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        if (!draft.alive(id)) {
            continue;
        }
        if (only.has_value() && id != *only) {
            continue; // one node's own reach, asked for by the report of a violation
        }
        const node_handle_t node = draft.ref(id);
        switch (nodeKindOf(node)) {
        case runtime::GCNodeKind::Oper: {
            const auto &op = graph->operTarget(node);
            if (op == nullptr) {
                return std::nullopt;
            }
            // An operator's resources are in the operator table, not on its type
            // (runtime/resources.cpp oper reads them the same way): what the table declares, and
            // otherwise the conservative reading of its traits.
            const auto &table = camel::core::OperatorTable::instance();
            const std::string uri(op->uri());
            const auto traits = table.find(uri);
            sem::ResourceSpec spec;
            bool declared = false;
            if (const auto found = table.lookup(uri); found.has_value()) {
                if (const auto *facts = table.semantics(*found);
                    facts != nullptr && facts->resources) {
                    spec     = *facts->resources;
                    declared = true;
                }
            }
            if (!declared) {
                spec = sem::conservativeResources(
                    draft.normInputsOf(id).size(),
                    draft.withInputsOf(id).size(),
                    traits.has_value() && traits->pure,
                    table.hasExternalEffect(uri));
            }
            for (const sem::ResourceAccess &access : spec.accesses) {
                if (access.binding.kind == sem::ResourceBinding::Kind::Ambient) {
                    addAmbient(access.resource.name, access.mode);
                    continue;
                }
                if (access.binding.kind == sem::ResourceBinding::Kind::Capture) {
                    return std::nullopt; // which capture of this closure it is, is not readable
                }
                const bool isWith = access.binding.kind == sem::ResourceBinding::Kind::With;
                const auto inputs = isWith ? draft.withInputsOf(id) : draft.normInputsOf(id);
                // A call through a value keeps the value itself as its first `with` input, so an
                // operator value's `with` operands start after it.
                const size_t at = size_t(access.binding.index) + (isWith ? 1 : 0);
                if (at >= inputs.size()) {
                    return std::nullopt;
                }
                const auto port = portIndex.find(inputs[at]);
                if (port == portIndex.end()) {
                    continue; // a value the body computed, not one of its caller's
                }
                addParam(port->second, access.resource.cls, access.resource.name, access.mode);
            }
            out.external = out.external || spec.externalEffect;
            break;
        }
        case runtime::GCNodeKind::Func: {
            const compile_graph_ptr_t callee = graph->funcTarget(node);
            if (callee == nullptr) {
                return std::nullopt;
            }
            const FunctionType *calleeType = callee->funcType();
            if (calleeType == nullptr) {
                return std::nullopt;
            }
            // A call reads the signature of the function it calls -- the clause it wrote, or, when
            // it wrote none, the set its body was measured to reach (design principles 3.3). A
            // call through a value reads the same signature below, so a function's resources have
            // one reading and both paths take it. A function that wrote no clause keeps its
            // measurement in the fixpoint while that is still running, which is what a caller has
            // to read then; a signature that says nothing at all is the one a function-type
            // parameter has, and what the call reaches is the instantiation's, so it is read
            // conservatively rather than as reaching nothing.
            const auto inferred =
                inferredSignatures_.contains(callee.get()) ? inferredResources_.find(callee.get())
                                                           : inferredResources_.end();
            if (inferred != inferredResources_.end()) {
                if (!readSignature(inferred->second, calleeType->withTypesCount(), false, id)) {
                    return std::nullopt;
                }
                break;
            }
            if (calleeType->resources().mode != sem::ResourceSignature::Mode::Concrete) {
                readUnknownValue();
                break;
            }
            if (!readSignature(calleeType->resources(), calleeType->withTypesCount(), false, id)) {
                return std::nullopt;
            }
            break;
        }
        case runtime::GCNodeKind::Call: {
            // A call through a function value: what it runs is the value's signature, which is
            // the first `with` input of the call (isEffectNode reads it the same way). A value of
            // a function that wrote no clause carries the set its body was measured to reach; the
            // only function type that says nothing is a parameter of function type, whose clause
            // the callee writes at each instantiation -- so this is read conservatively.
            const auto callee = draft.withInputsOf(id);
            if (callee.empty()) {
                return std::nullopt;
            }
            Type *calleeType = nodeTypeOf(draft.ref(callee[0]));
            if (calleeType == nullptr || calleeType->code() != TypeCode::Function) {
                return std::nullopt;
            }
            const auto *calleeFunc = tt::as_ptr<FunctionType>(calleeType);
            if (calleeFunc->resources().mode != sem::ResourceSignature::Mode::Concrete) {
                readUnknownValue();
                break;
            }
            if (!readSignature(calleeFunc->resources(), calleeFunc->withTypesCount(), true, id)) {
                return std::nullopt;
            }
            break;
        }
        default:
            break;
        }
    }
    if (unplaceable) {
        return std::nullopt; // see addParam: nothing here is measured, rather than too little
    }
    // The declaration's own `var` parameters: a `var` parameter is a resource by itself, whether
    // or not the body reaches it (design principles 3.3). A parameter is not a node, so it is part
    // of the body's reach and not of any one node's.
    if (!only.has_value()) {
        const size_t withCount = type->withTypesCount();
        size_t index           = 0;
        for (size_t i = 0; i < withCount + type->normTypesCount(); ++i) {
            const bool isWith = i < withCount;
            if (!isWith && type->normIsConstasAt(i - withCount)) {
                continue; // not a runtime parameter
            }
            const bool isVar = isWith ? type->withIsVarAt(i) : type->normIsVarAt(i - withCount);
            if (isVar) {
                addParam(index, sem::ResourceClass::Variable, "", sem::AccessMode::ReadModifyWrite);
            }
            ++index;
        }
    }
    std::sort(out.params.begin(), out.params.end(), [](const auto &a, const auto &b) {
        return a.index < b.index;
    });
    std::sort(out.ambient.begin(), out.ambient.end(), [](const auto &a, const auto &b) {
        return a.name < b.name;
    });
    std::sort(out.anyOf.begin(), out.anyOf.end());
    out.anyOf.erase(std::unique(out.anyOf.begin(), out.anyOf.end()), out.anyOf.end());
    return out;
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