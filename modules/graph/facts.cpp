// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The facts of the graph module: what the core knows of a graph or a node without the pass
 * computing it again (design 10). Reads go through the context's fact store, so a fact asked
 * for twice is computed once and dropped when a rewrite changes what it read.
 */

#include "facts.h"

#include "handle.h"

#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/core_facts.h"
#include "camel/runtime/effects.h"
#include "camel/runtime/program.h"

using namespace camel::core::context;
using namespace camel::core::type;
using namespace camel::core::rtdata;
using camel::runtime::fact::Anchor;
using camel::runtime::EffectSummary;
using camel::runtime::GCGraph;
using camel::runtime::kInvalidNodeRef;

namespace camel::graphmod {

namespace {

namespace mm   = camel::core::mm;
namespace err  = camel::core::error;
namespace fact = camel::runtime::fact;

Anchor anchorOf(const Handle *handle, const char *site, bool node) {
    const GCGraph *graph =
        handle->program() != nullptr ? handle->program()->graph(handle->graph()) : nullptr;
    if (graph == nullptr ||
        (node && (!graph->containsNodeRef(handle->index()) ||
                  graph->node(handle->index())->kind == camel::runtime::GCNodeKind::Dead))) {
        err::throwRuntimeFault(err::RuntimeDiag::InvalidArgument, site, "the handle names nothing");
    }
    return Anchor{handle->program(), graph, node ? handle->index() : kInvalidNodeRef};
}

/// What running the graph once does besides computing its value: an effect outside the
/// program's variables, a write to a variable, a possible fault, a possible non-termination.
slot_t effectsKernel(ArgsView &, ArgsView &norm, Context &ctx) {
    const Anchor anchor = anchorOf(norm.get<Handle *>(0), "graph.effects", false);
    const Anchor anchors[] = {anchor};
    const auto summary = ctx.facts().obtain<EffectSummary>(fact::coreKinds().effects, anchors);
    if (!summary) {
        err::throwRuntimeFault(err::RuntimeDiag::InvalidArgument, "graph.effects", "no summary");
    }
    StructType *type = effectsType();
    ::Struct *out    = ::Struct::create(type->size(), mm::autoSpace());
    out->set<bool>(0, summary->external, type);
    out->set<bool>(1, summary->writesVariables, type);
    out->set<bool>(2, summary->mayFault, type);
    out->set<bool>(3, summary->mayNotTerminate, type);
    return toSlot(out);
}

/// The tensor shape of the node's value as text (`f32[2, 3]`), null when its value is no tensor.
slot_t shapeKernel(ArgsView &, ArgsView &norm, Context &ctx) {
    const Anchor anchors[] = {anchorOf(norm.get<Handle *>(0), "graph.shape", true)};
    const auto shape = ctx.facts().obtain<fact::TensorShape>(fact::coreKinds().shape, anchors);
    if (!shape || !shape->dtype) {
        return NullSlot;
    }
    return makeSome(
        toSlot(::String::from(fact::toString(*shape), mm::autoSpace())),
        TupleType::nullable(Type::String()));
}

/// The numeric permissions the program granted the node, as a bit set.
slot_t permissionsKernel(ArgsView &, ArgsView &norm, Context &ctx) {
    const Anchor anchors[] = {anchorOf(norm.get<Handle *>(0), "graph.permissions", true)};
    const auto granted = ctx.facts().obtain<int>(fact::coreKinds().permissions, anchors);
    return toSlot<Int64>(granted ? *granted : 0);
}

struct FactOp {
    const char *name;
    operator_t kernel;
};

const std::vector<FactOp> &factOps() {
    static const std::vector<FactOp> ops = {
        {"effects", effectsKernel},
        {"shape", shapeKernel},
        {"permissions", permissionsKernel},
    };
    return ops;
}

} // namespace

StructType *effectsType() {
    static StructType *type = [] {
        StructTypeFactory factory;
        factory.add("external", Type::Bool());
        factory.add("writes_variables", Type::Bool());
        factory.add("may_fault", Type::Bool());
        factory.add("may_not_terminate", Type::Bool());
        return factory.build();
    }();
    return type;
}

const std::unordered_map<std::string, operator_t> &factKernels() {
    static const std::unordered_map<std::string, operator_t> kernels = [] {
        std::unordered_map<std::string, operator_t> out;
        for (const FactOp &op : factOps()) {
            out.emplace(op.name, op.kernel);
        }
        return out;
    }();
    return kernels;
}

} // namespace camel::graphmod
