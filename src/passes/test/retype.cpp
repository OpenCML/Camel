// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Test-only passes for the graph rebuild primitive (see camel/passes/test/retype.h).
 */

#include "camel/passes/test/retype.h"

#include "camel/core/mm/gen.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/stream.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/enum.h"
#include "camel/core/type/intern.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/retype.h"

#include <algorithm>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace {

namespace ty = camel::core::type;
using camel::runtime::GCGraph;
using camel::runtime::Program;
using ty::Type;
using ty::TypeCode;
using ty::TypeId;

/// What a graph looked like: its node count, its edge count and the type id of every record, in
/// node order. Two of these being equal is "the same topology with the same types".
struct GraphShape {
    size_t nodes = 0;
    size_t edges = 0;
    std::vector<TypeId> types;
};

/// Whether two shapes are the same graph: as many nodes, as many edges, the same type on every
/// record. A rebuild that only changes types has to keep every one of these equal to what it was.
bool sameShape(const GraphShape &left, const GraphShape &right) {
    return left.nodes == right.nodes && left.edges == right.edges && left.types == right.types;
}

GraphShape shapeOf(const GCGraph &graph) {
    GraphShape shape;
    shape.nodes = graph.nodeCount();
    shape.types.resize(graph.nodeCount());
    for (auto it = graph.nodes().begin(); it != graph.nodes().end(); ++it) {
        const camel::runtime::GCNode &node = **it;
        shape.types[it.ref()]                  = node.typeId;
        shape.edges += graph.normInputsOf(it.ref()).size() +
                       graph.withInputsOf(it.ref()).size() +
                       graph.ctrlInputsOf(it.ref()).size();
    }
    shape.edges += graph.orderEdges().size() + graph.syncEdges().size();
    return shape;
}

/// Whether `type` names `id` anywhere it can be reached: a component of a tuple, an array, a
/// stream or a function type. A named type's fields are not walked (they are its declaration's).
bool mentions(const Type *type, TypeId id, int depth = 0) {
    if (type == nullptr || depth > 24) {
        return false;
    }
    if (type->typeId() == id) {
        return true;
    }
    switch (type->code()) {
    case TypeCode::Tuple:
        for (Type *member : static_cast<const ty::TupleType *>(type)->types()) {
            if (mentions(member, id, depth + 1)) {
                return true;
            }
        }
        return false;
    case TypeCode::Array:
        return mentions(static_cast<const ty::ArrayType *>(type)->elemType(), id, depth + 1);
    case TypeCode::Stream:
        return mentions(static_cast<const ty::StreamType *>(type)->elemType(), id, depth + 1);
    case TypeCode::Function: {
        auto *func = static_cast<const ty::FunctionType *>(type);
        for (Type *param : func->withTypesSpan()) {
            if (mentions(param, id, depth + 1)) {
                return true;
            }
        }
        for (Type *param : func->normTypesSpan()) {
            if (mentions(param, id, depth + 1)) {
                return true;
            }
        }
        return func->hasExitType() && mentions(func->exitType(), id, depth + 1);
    }
    default:
        return false;
    }
}

/// Whether any type the graph names is `id` or carries it: every node, the frame layout, the
/// signature, the closure and the form ports of the regions -- and, unless `includeStatics` is
/// false, the static area's layout, which `leaveStaticObjects` deliberately leaves alone.
bool graphMentions(const GCGraph &graph, TypeId id, bool includeStatics = true) {
    if (mentions(graph.funcType(), id) ||
        mentions(graph.runtimeDataType(), id) ||
        (includeStatics && mentions(graph.staticDataType(), id)) ||
        mentions(graph.closureType(), id)) {
        return true;
    }
    for (auto it = graph.nodes().begin(); it != graph.nodes().end(); ++it) {
        if (mentions((*it)->dataType(), id)) {
            return true;
        }
    }
    for (camel::runtime::gc_cnt_t r = 0; r < graph.regionCount(); ++r) {
        const camel::runtime::GCRegion *region = graph.region(r);
        if (region != nullptr && mentions(region->params, id)) {
            return true;
        }
    }
    return false;
}

size_t countNodesTyped(const Program &program, TypeId id) {
    size_t count = 0;
    for (const GCGraph *graph : program.graphs()) {
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            if ((*it)->typeId == id) {
                ++count;
            }
        }
    }
    return count;
}

size_t graphCount(const Program &program) { return program.graphs().size(); }

/// The graphs of `program`, by id, with what they looked like.
std::map<uint64_t, GraphShape> shapesOf(const Program &program) {
    std::map<uint64_t, GraphShape> shapes;
    for (const GCGraph *graph : program.graphs()) {
        shapes.emplace(camel::runtime::toValue(graph->id()), shapeOf(*graph));
    }
    return shapes;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// test::retype_f64_i64
// ---------------------------------------------------------------------------------------------

const Program *RetypeTrivialTestPass::apply(const Program *program, std::ostream &os) {
    using namespace camel::runtime;

    const TypeId floatId = ty::Type::Float64()->typeId();
    const TypeId intId   = ty::Type::Int64()->typeId();

    // Everything this pass says about "before" is read now: after a commit the program it was
    // handed is no longer the one the context holds, and the versions it was holding are the
    // commit's to release (settle), not this pass's to keep.
    const auto before            = shapesOf(*program);
    const size_t graphsBefore    = graphCount(*program);
    const size_t f64Before       = countNodesTyped(*program, floatId);
    const GCGraph *sourceRoot    = program->root();
    const GraphShape sourceShape = shapeOf(*sourceRoot);

    // (1) A rebuild of nothing is the same graph: an empty substitution changes no type, the
    // commit recognizes the content it encoded as the content it had, and the version the program
    // held stays the version it holds. This is the assertion that a rebuild adds nothing of its
    // own -- no renumbered node, no reordered edge, no rewritten region -- which is what "only the
    // types change" has to mean.
    bool identitySameVersion = false;
    const Program *afterIdentity = nullptr;
    {
        RuntimeGraphDraftSession session(context_, program);
        ProgramRetypeReport identity;
        afterIdentity = retypeProgram(session, GraphTypeSubstitution{}, {}, &identity);
        identitySameVersion = afterIdentity != nullptr &&
                              afterIdentity->graph(sourceRoot->id()) == sourceRoot;
        os << "RETYPE identity_changed="
           << (identity.retypedNodes != 0 || identity.rebuilt != 0 ? 1 : 0)
           << " identity_same_version=" << (identitySameVersion ? 1 : 0) << "\n";
    }
    if (afterIdentity == nullptr) {
        os << "RETYPE commit_failed=1\n";
        return program;
    }
    // The source graph survived a whole decode/encode round trip as the same version, with the
    // records and the inputs it had.
    const GraphShape sourceAfterIdentity = shapeOf(*sourceRoot);
    const bool sourceSurvived = sameShape(sourceAfterIdentity, sourceShape);

    // (2) float -> int, on every graph the program reaches.
    GraphTypeSubstitution types;
    types.replace(ty::Type::Float64(), ty::Type::Int64());

    RuntimeGraphDraftSession session(context_, afterIdentity);
    ProgramRetypeReport report;
    const Program *next = retypeProgram(session, types, {}, &report);
    if (next == nullptr) {
        os << "RETYPE commit_failed=1\n";
        return afterIdentity;
    }

    // (3) The assertions the plan locks in: same graphs, same node counts, same edge counts.
    bool topologySame = graphCount(*next) == graphsBefore;
    size_t nodesNow = 0, edgesNow = 0;
    for (const GCGraph *graph : next->graphs()) {
        nodesNow += graph->nodeCount();
        const auto old = before.find(toValue(graph->id()));
        if (topologySame && old != before.end()) {
            const GraphShape now = shapeOf(*graph);
            edgesNow += now.edges;
            topologySame = now.nodes == old->second.nodes && now.edges == old->second.edges;
        }
    }
    bool f64After = false;
    for (const GCGraph *graph : next->graphs()) {
        f64After = f64After || graphMentions(*graph, floatId);
    }
    const size_t retypedNow = countNodesTyped(*next, intId);

    // The graph the program was built from is still exactly what it was: the rebuild made a
    // version of it, it did not edit this one. It is read here through the graph space, and it is
    // the same check the identity rebuild above made pass, once more after the real one.
    bool sourceUnchanged = false;
    if (camel::core::mm::graphSpace().isAllocated(const_cast<GCGraph *>(sourceRoot))) {
        sourceUnchanged = sameShape(shapeOf(*sourceRoot), sourceShape);
    }

    os << "RETYPE graphs=" << report.graphs << " rebuilt=" << report.rebuilt
       << " nodes=" << nodesNow << " edges=" << edgesNow << " retyped=" << report.retypedNodes
       << "\n";
    os << "RETYPE counts_same=" << (topologySame ? 1 : 0)
       << " f64_before=" << f64Before << " f64_after=" << (f64After ? 1 : 0)
       << " int_nodes=" << retypedNow << "\n";
    os << "RETYPE source_unchanged=" << (sourceSurvived && sourceUnchanged ? 1 : 0) << "\n";
    return next;
}

// ---------------------------------------------------------------------------------------------
// test::retype_enum
// ---------------------------------------------------------------------------------------------

namespace {

/// The pair type the probe replaces: a tuple whose components include a `float` and a function
/// type -- a value and the pullback that goes with it.
ty::TupleType *findPairType(const Program &program) {
    for (const GCGraph *graph : program.graphs()) {
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            Type *nodeType = (*it)->dataType();
            if (nodeType == nullptr || nodeType->code() != TypeCode::Tuple) {
                continue;
            }
            auto *tuple = static_cast<ty::TupleType *>(nodeType);
            if (tuple->size() < 2 || tuple->isCell() || tuple->isNullable() || tuple->isFallible()) {
                continue;
            }
            bool hasFloat = false, hasFunction = false;
            for (Type *member : tuple->types()) {
                hasFloat    = hasFloat || (member != nullptr && member->code() == TypeCode::Float64);
                hasFunction = hasFunction ||
                              (member != nullptr && member->code() == TypeCode::Function);
            }
            if (hasFloat && hasFunction) {
                return tuple;
            }
        }
    }
    return nullptr;
}

} // namespace

const Program *RetypeEnumTestPass::apply(const Program *program, std::ostream &os) {
    using namespace camel::runtime;

    ty::TupleType *pair = findPairType(*program);
    if (pair == nullptr) {
        os << "RETYPEENUM pair_found=0\n";
        return program;
    }
    const TypeId pairId = pair->typeId();

    // The enum the pair becomes: one variant carrying exactly that tuple. Its variant set is
    // decided here, once, before the rebuilt graphs are encoded -- the order the design page
    // requires ("先定变体集合"), in miniature.
    ty::EnumDecl *decl = ty::EnumDecl::create("PullbackValue", {}, "test::retype");
    ty::EnumVariant variant;
    variant.name       = "Pair";
    variant.shape      = ty::VariantShape::Tuple;
    variant.fieldTypes = {pair};
    variant.fieldOrigins.assign(1, 0);
    decl->define({variant});
    ty::EnumType *boxed = ty::EnumType::get(decl, {});
    ASSERT(boxed != nullptr, "The probe's enum type could not be interned.");

    GraphTypeSubstitution types;
    types.replace(pair, boxed);

    // The pair is built by a tuple literal, and the aggregates a program builds are kept as static
    // templates: the static area therefore holds entries of the pair's type. A rebuild may not put
    // a new type over an object that keeps its class, so those keep what they have while the nodes
    // that read them are retyped with everything else; reconciling the two is what the rewrite that
    // replaces the constructor does (GraphRetypeOptions::leaveStaticObjects).
    GraphRetypeOptions options;
    options.leaveStaticObjects = true;

    const GCGraph *sourceRoot   = program->root();
    const GraphShape sourceShape = shapeOf(*sourceRoot);
    const size_t pairBefore      = countNodesTyped(*program, pairId);

    RuntimeGraphDraftSession session(context_, program);
    ProgramRetypeReport report;
    const Program *next = retypeProgram(session, types, options, &report);
    if (next == nullptr) {
        os << "RETYPEENUM commit_failed=1\n";
        return program;
    }

    const size_t pairAfter = countNodesTyped(*next, pairId);
    bool pairLeft          = false;
    for (const GCGraph *graph : next->graphs()) {
        pairLeft = pairLeft || graphMentions(*graph, pairId, /*includeStatics=*/false);
    }
    const size_t enumNodes = countNodesTyped(*next, boxed->typeId());

    // The branch: every arm of a JOIN has the type the JOIN has. This is the disagreement the
    // background names -- two arms and the JOIN built for two types -- and the reason the swap
    // has to reach every place the type occurs at once.
    size_t joins = 0, armsChecked = 0;
    bool armsAgree = true;
    const auto &table = ty::TypeTable::instance();
    for (const GCGraph *graph : next->graphs()) {
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            if ((*it)->kind != GCNodeKind::Join) {
                continue;
            }
            ++joins;
            const TypeId joinType = table.canonicalIdOf((*it)->typeId);
            const auto inputs     = graph->normInputsOf(it.ref());
            if (inputs.empty()) {
                continue;
            }
            const gc_node_ref_t brch = inputs.front();
            for (gc_cnt_t r = 0; r < graph->regionCount(); ++r) {
                const camel::runtime::GCRegion *region = graph->region(r);
                if (region == nullptr || region->kind != GCRegionKind::Arm || region->owner != brch ||
                    region->result == kInvalidNodeRef) {
                    continue;
                }
                const GCNode *result = graph->node(region->result);
                if (result == nullptr || result->typeId == ty::kNoTypeId) {
                    continue;
                }
                ++armsChecked;
                armsAgree = armsAgree && table.canonicalIdOf(result->typeId) == joinType;
            }
        }
    }

    bool sourceUnchanged = false;
    if (camel::core::mm::graphSpace().isAllocated(const_cast<GCGraph *>(sourceRoot))) {
        sourceUnchanged = sameShape(shapeOf(*sourceRoot), sourceShape);
    }

    os << "RETYPEENUM pair='" << pair->toString() << "' graphs=" << report.graphs
       << " rebuilt=" << report.rebuilt << " nodes_pair_before=" << pairBefore
       << " statics=" << report.retypedStatic << " statics_left=" << report.leftStatic << "\n";
    os << "RETYPEENUM type='" << boxed->toString() << "' enum_nodes=" << enumNodes
       << " pair_after=" << (pairLeft ? 1 : 0) << " pair_nodes_after=" << pairAfter << "\n";
    os << "RETYPEENUM seal=ok joins=" << joins << " arms_checked=" << armsChecked
       << " arms_ok=" << (armsChecked != 0 && armsAgree ? 1 : 0) << "\n";
    os << "RETYPEENUM source_unchanged=" << (sourceUnchanged ? 1 : 0) << "\n";
    return next;
}