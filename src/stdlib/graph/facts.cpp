// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The facts of the graph module: what the core knows of a graph or a node without the pass
 * computing it again (design 10). Reads go through the context's fact store, so a fact asked
 * for twice is computed once and dropped when a rewrite changes what it read.
 *
 * A fact about a *draft* is the exception, and it is why every operator here starts at `viewOf`
 * (resolve.h) and not at the store. The store's anchor holds a published `GCGraph`
 * (camel/runtime/facts.h, Anchor) and a draft is not one, and the store decides staleness from
 * `(GraphId, revision)` -- a draft has no revision column to read. So a fact asked about a draft
 * node is answered by the draft at the operator that asks, and not kept: a cache of a draft's
 * facts would need a second answer to "when is this stale", which is the one thing this module
 * must not have two of. A published graph and a published node keep going to the store exactly as
 * before.
 *
 * `effects` is the one query with no draft answer at all: `effectwalk` walks a published GCGraph
 * (camel/runtime/effect_walk.h), so it is refused on a draft rather than answered about the graph
 * the draft was opened from (see effectsKernel).
 */

#include "facts.h"

#include "handle.h"
#include "resolve.h"

#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/tags.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/core_facts.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/effects.h"
#include "camel/runtime/program.h"

using namespace camel::core::context;
using namespace camel::core::type;
using namespace camel::core::rtdata;
using camel::runtime::fact::Anchor;
using camel::runtime::EffectSummary;
using camel::runtime::kInvalidNodeRef;

namespace camel::graphmod {

namespace {

namespace mm   = camel::core::mm;
namespace err  = camel::core::error;
namespace fact = camel::runtime::fact;

/// The anchor of a published graph or node: what the store is keyed on. A draft has none (the
/// file comment), so a caller reaches this only once `viewOf` said the handle names no draft.
Anchor publishedAnchor(const Handle *handle, const char *site, bool node) {
    const camel::runtime::GCGraph &graph = graphOf(handle, site);
    if (node && (!graph.containsNodeRef(handle->index()) ||
                 graph.node(handle->index())->kind == camel::runtime::GCNodeKind::Dead)) {
        err::throwRuntimeFault(err::RuntimeDiag::InvalidArgument, site, "the handle names nothing");
    }
    return Anchor{handle->program(), &graph, node ? handle->index() : kInvalidNodeRef};
}

/// What running the graph once does besides computing its value: an effect outside the
/// program's variables, a write to a variable, a possible fault, a possible non-termination.
slot_t effectsKernel(ArgsView &, ArgsView &norm, Context &ctx) {
    const Handle *handle = norm.get<Handle *>(0);
    // A draft's effects are refused rather than answered. `effectwalk` walks a published GCGraph
    // and a draft is not one, and resolving the handle the old way would take the draft's GraphId
    // to the program it was opened on: the summary printed would be the one of the graph *before*
    // the edits, with no diagnostic and exit 0 -- a silently wrong answer, which is worse than a
    // refusal. A walk that reads the draft is a change of its own, not smaller than this one.
    if (handle->transaction() != nullptr) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            "graph.effects",
            "the effects of a draft are not computed: a draft has no published graph to walk");
    }
    const Anchor anchor    = publishedAnchor(handle, "graph.effects", false);
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
    const Handle *handle = norm.get<Handle *>(0);
    const NodeView node  = viewOf(handle, "graph.shape");
    std::optional<fact::TensorShape> shape;
    if (node.isDraft()) {
        // The draft's node carries the type the published node had and the edits that changed it
        // (NodeView::dataType), and the shape of a type is one reading for both representations
        // (core_facts.h, shapeOfType) -- so the draft answers without the store, and the text is
        // the text the published node prints.
        shape = fact::shapeOfType(node.dataType());
    } else {
        const Anchor anchors[] = {publishedAnchor(handle, "graph.shape", true)};
        const auto stored = ctx.facts().obtain<fact::TensorShape>(fact::coreKinds().shape, anchors);
        if (stored) {
            shape = *stored;
        }
    }
    if (!shape || !shape->dtype) {
        return NullSlot;
    }
    return makeSome(
        toSlot(::String::from(fact::toString(*shape), mm::autoSpace())),
        TupleType::nullable(Type::String()));
}

/// The numeric permissions the program granted the node, as a bit set.
slot_t permissionsKernel(ArgsView &, ArgsView &norm, Context &ctx) {
    const Handle *handle = norm.get<Handle *>(0);
    const NodeView node  = viewOf(handle, "graph.permissions");
    if (node.isDraft()) {
        // `tag::permissionsOf` is the one place a node's permission tags become bits (tags.h), and
        // it states both representations; the draft keeps the column and the writes that changed
        // it, so this is the published reading taken where the draft keeps it -- not a second one.
        return toSlot<Int64>(camel::core::tag::permissionsOf(node.draft(), node.ref));
    }
    const Anchor anchors[] = {publishedAnchor(handle, "graph.permissions", true)};
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
        {"shape_text", shapeKernel},
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
