// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * TEST-ONLY: asks the builder's walk and the runtime's walk the same question about every body of
 * one program and prints both answers (see resource_transition.h).
 */

#include "resource_transition.h"

#include "camel/compile/gir/resource_transition.h"
#include "camel/core/type/composite/func.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/reachable.h"
#include "camel/runtime/resources.h"

#include <algorithm>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace camel::core::sem;
namespace sem = camel::core::sem;
namespace rt  = camel::runtime;
namespace gir = camel::compile::gir;
using camel::core::type::FunctionType;

/// The parts of one answer, sorted and joined: one part per access (in `sem::toString`'s text, the
/// form the runtime's footprint is written in), one per class it could not name, and `external`.
/// Sorted, because the two walks visit nodes in different orders and the claim is about the set.
std::string answerText(std::vector<std::string> parts) {
    std::sort(parts.begin(), parts.end());
    std::string out;
    for (const std::string &part : parts) {
        out += (out.empty() ? "" : "; ") + part;
    }
    return out.empty() ? "none" : out;
}

std::string anyPart(ResourceClass cls) {
    return std::format("rmw {} (any)", resourceClassName(cls));
}

/// One access, written the way the runtime writes its own: the resource it names, the binding it
/// arrives on, and the mode.
std::string accessPart(
    ResourceClass cls, const std::string &name, ResourceBinding binding, AccessMode mode) {
    return sem::toString(ResourceAccess{
        .resource = {.cls = cls, .name = name},
        .binding  = binding,
        .region   = {},
        .mode     = mode,
    });
}

/// The builder's measured reach of a body, in the runtime's language. A parameter's index is the
/// call's numbering -- with parameters first, the ordinary ones after them -- which the runtime
/// names by two bindings: `with` for the first `withCount`, `norm` for the rest (the mapping of
/// runtime/resources.cpp#fromSignature). A captured position carries no access mode in a signature;
/// it is read as read-modify-write, which is what the runtime says of a captured cell a body writes
/// and the widest thing the signature's word for a capture can mean.
std::string builderAnswer(const ResourceSignature &reach, size_t withCount) {
    std::vector<std::string> parts;
    for (const ResourceSignature::ParamUse &use : reach.params) {
        const bool isWith = use.index < withCount;
        const ResourceBinding binding{
            .kind  = isWith ? ResourceBinding::Kind::With : ResourceBinding::Kind::Norm,
            .index = static_cast<uint8_t>(isWith ? use.index : use.index - withCount)};
        parts.push_back(accessPart(use.cls, use.name, binding, use.mode));
    }
    for (const ResourceSignature::AmbientUse &use : reach.ambient) {
        parts.push_back(sem::toString(ambientAccess(use.name, use.mode)));
    }
    for (uint8_t capture : reach.captures) {
        const ResourceBinding binding{.kind = ResourceBinding::Kind::Capture, .index = capture};
        parts.push_back(
            accessPart(ResourceClass::Variable, {}, binding, AccessMode::ReadModifyWrite));
    }
    for (ResourceClass cls : reach.anyOf) {
        parts.push_back(anyPart(cls));
    }
    if (reach.external) {
        parts.push_back("external");
    }
    return answerText(std::move(parts));
}

/// The runtime's footprint of a graph, in the same language. A capture's mode is dropped on this
/// side too: a capture states a position and no mode (see builderAnswer), so the two answers are
/// then the same claim.
std::string runtimeAnswer(const rt::GraphFootprint &footprint) {
    std::vector<std::string> parts;
    for (const ResourceAccess &access : footprint.accesses) {
        if (access.binding.kind == ResourceBinding::Kind::Capture) {
            parts.push_back(accessPart(
                access.resource.cls, access.resource.name, access.binding,
                AccessMode::ReadModifyWrite));
            continue;
        }
        parts.push_back(sem::toString(access));
    }
    for (ResourceClass cls : footprint.anyOf) {
        parts.push_back(anyPart(cls));
    }
    if (footprint.external) {
        parts.push_back("external");
    }
    return answerText(std::move(parts));
}

/// The parameters a call passes, in the numbering a signature uses: a `constas` with parameter is a
/// compile-time value and has no port (FunctionType::withIsConstasAt), so it is not one of them.
size_t runtimeWithCount(const FunctionType &type) {
    size_t count = 0;
    for (size_t i = 0; i < type.withTypesCount(); ++i) {
        if (!type.withIsConstasAt(i)) {
            ++count;
        }
    }
    return count;
}

} // namespace

const camel::runtime::Program *
ResourceTransitionTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    std::vector<rt::GCGraph *> graphs = rt::collectReachableGraphs(*program, program->root());
    std::sort(graphs.begin(), graphs.end(), [](const rt::GCGraph *a, const rt::GCGraph *b) {
        return a->name() < b->name();
    });
    for (const rt::GCGraph *graph : graphs) {
        const ResourceSignature *measured = gir::measuredReachOf(graph->id());
        if (measured == nullptr) {
            continue; // not a body this compile measured
        }
        const FunctionType *type = graph->funcType();
        const std::string builder =
            builderAnswer(*measured, type != nullptr ? runtimeWithCount(*type) : 0);
        const std::string runtime = runtimeAnswer(rt::footprintOf(*program, graph));
        os << "MASK " << graph->name() << " builder=" << builder << " runtime=" << runtime
           << " agree=" << (builder == runtime ? "yes" : "no") << "\n";
    }
    return program;
}