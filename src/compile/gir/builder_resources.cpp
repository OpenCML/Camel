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

std::optional<camel::core::sem::ResourceSignature>
Builder::footprintOf(DraftGraphBuilder *graph) const {
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
    const auto addParam = [&](size_t index, sem::ResourceClass cls, const std::string &name,
                              sem::AccessMode mode) {
        if (index > 0xFF) {
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
    for (draft_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        if (!draft.alive(id)) {
            continue;
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
            const auto measured = inferredResources_.find(callee.get());
            if (measured == inferredResources_.end()) {
                return std::nullopt; // the callee is not measured yet
            }
            const FunctionType *calleeType = callee->funcType();
            if (calleeType == nullptr) {
                return std::nullopt;
            }
            if (!readSignature(
                    measured->second, calleeType->withTypesCount(), false, id)) {
                return std::nullopt;
            }
            break;
        }
        case runtime::GCNodeKind::Call: {
            // A call through a function value: what it runs is the value's signature, which is
            // the first `with` input of the call (isEffectNode reads it the same way). When that
            // signature promises nothing, the body is not measured.
            const auto callee = draft.withInputsOf(id);
            if (callee.empty()) {
                return std::nullopt;
            }
            Type *calleeType = nodeTypeOf(draft.ref(callee[0]));
            if (calleeType == nullptr || calleeType->code() != TypeCode::Function) {
                return std::nullopt;
            }
            const auto *calleeFunc = tt::as_ptr<FunctionType>(calleeType);
            if (!readSignature(calleeFunc->resources(), calleeFunc->withTypesCount(), true, id)) {
                return std::nullopt;
            }
            break;
        }
        default:
            break;
        }
    }
    // The declaration's own `var` parameters: a `var` parameter is a resource by itself, whether
    // or not the body reaches it (design principles 3.3).
    {
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
    // another module, so the measurements settle against each other first. A body whose walk met
    // something it cannot place is absent from the map, and stays absent.
    for (bool changed = true; changed;) {
        changed = false;
        for (const ModuleUnit *unit : unitOrder_) {
            for (const compile_graph_ptr_t &graph : unit->builtBodies) {
                const auto measured = footprintOf(graph.get());
                if (!measured.has_value()) {
                    continue;
                }
                const auto known = inferredResources_.find(graph.get());
                if (known == inferredResources_.end() || !(known->second == *measured)) {
                    inferredResources_[graph.get()] = *measured;
                    changed                        = true;
                }
            }
        }
    }
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
        reportResourceViolation(graph, promised, measured->second);
    }
}
} // namespace camel::compile::gir