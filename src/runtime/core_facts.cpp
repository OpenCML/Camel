// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The core's kinds of graph fact (camel/runtime/core_facts.h): their registration, computation,
 * text and encoded forms.
 *
 * Encoded forms (persist.h) are short and process-independent: no OperId, TypeId or pointer.
 * A type code is its name (typeCodeToString), read back by comparing with every code's name.
 */

#include "camel/runtime/core_facts.h"

#include "camel/core/operator_semantics.h"
#include "camel/core/operator_table.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/tags.h"
#include "camel/runtime/operator_instance.h"

#include <charconv>
#include <format>
#include <mutex>
#include <shared_mutex>

namespace camel::runtime::fact {

namespace sem = camel::core::sem;
using camel::core::type::TypeCode;

namespace {

// ---- Shape readers -------------------------------------------------------------------------

struct ShapeReaders {
    std::shared_mutex mutex;
    std::vector<ShapeReader> readers;
};

ShapeReaders &shapeReaders() {
    static ShapeReaders instance;
    return instance;
}

// ---- Small text helpers --------------------------------------------------------------------

std::string flags(std::initializer_list<bool> bits) {
    std::string out;
    for (bool bit : bits) {
        out += bit ? '1' : '0';
    }
    return out;
}

std::optional<std::vector<bool>> unflags(std::string_view text, size_t count) {
    if (text.size() != count) {
        return std::nullopt;
    }
    std::vector<bool> bits;
    for (char c : text) {
        if (c != '0' && c != '1') {
            return std::nullopt;
        }
        bits.push_back(c == '1');
    }
    return bits;
}

std::optional<int64_t> integer(std::string_view text) {
    int64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

/// The type code named `name` (typeCodeToString), among the codes a tensor's elements have.
std::optional<TypeCode> typeCodeNamed(std::string_view name) {
    for (TypeCode code : {TypeCode::Int32, TypeCode::Int64, TypeCode::Float32, TypeCode::Float64,
                          TypeCode::Bool}) {
        if (camel::core::type::typeCodeToString(code) == name) {
            return code;
        }
    }
    return std::nullopt;
}

// ---- core.shape ------------------------------------------------------------------------------

/// "float32|2,3", "-|2,-1" (unknown element type; an extent nobody stated is -1, the spelling this
/// form has always used), "float32|?" (unknown rank).
std::string encodeShape(const TensorShape &shape) {
    std::string out = shape.dtype ? camel::core::type::typeCodeToString(*shape.dtype) : "-";
    out += '|';
    if (!shape.dims) {
        return out + "?";
    }
    for (size_t i = 0; i < shape.dims->size(); ++i) {
        out += std::format("{}{}", i ? "," : "", (*shape.dims)[i].value_or(-1));
    }
    return out;
}

std::optional<TensorShape> decodeShape(std::string_view text) {
    const size_t bar = text.find('|');
    if (bar == std::string_view::npos) {
        return std::nullopt;
    }
    TensorShape shape;
    if (const std::string_view code = text.substr(0, bar); code != "-") {
        shape.dtype = typeCodeNamed(code);
        if (!shape.dtype) {
            return std::nullopt;
        }
    }
    std::string_view dims = text.substr(bar + 1);
    if (dims == "?") {
        return shape;
    }
    shape.dims.emplace();
    while (!dims.empty()) {
        const size_t comma = dims.find(',');
        const auto extent  = integer(dims.substr(0, comma));
        if (!extent) {
            return std::nullopt;
        }
        // -1 is the spelling this form gives an extent nobody stated; a real extent is never
        // negative, so the two cannot be confused.
        shape.dims->push_back(*extent < 0 ? std::nullopt : std::optional<int64_t>(*extent));
        dims = comma == std::string_view::npos ? std::string_view{} : dims.substr(comma + 1);
    }
    return shape;
}

std::optional<TensorShape> computeShape(std::span<const Anchor> anchors) {
    return shapeOfType(anchors[0].graph->node(anchors[0].node)->dataType());
}

// ---- core.instance, core.reorder -------------------------------------------------------------

/// The OPER node's operator declares index semantics.
bool declaresIndexing(const GCGraph &graph, gc_node_ref_t node) {
    if (graph.node(node)->kind != GCNodeKind::Oper) {
        return false;
    }
    const sem::OperatorSemantics *s = camel::core::OperatorTable::instance().semantics(
        graph.nodeBodyAs<GCOperBody>(node)->oper);
    return s != nullptr && s->indexing != nullptr;
}

std::optional<NodeInstance> computeInstance(std::span<const Anchor> anchors) {
    const GCGraph &graph = *anchors[0].graph;
    if (!declaresIndexing(graph, anchors[0].node)) {
        return std::nullopt;
    }
    NodeInstance value;
    value.instance = instanceOf(graph, anchors[0].node);
    if (value.instance) {
        value.text = sem::toString(
            *value.instance,
            camel::core::OperatorTable::instance().semantics(
                graph.nodeBodyAs<GCOperBody>(anchors[0].node)->oper));
    }
    return value;
}

std::string printInstance(const NodeInstance &value) {
    if (!value.instance) {
        return "none";
    }
    std::string out = value.text;
    for (char &c : out) {
        c = c == '\n' ? ';' : c;
    }
    while (!out.empty() && out.back() == ';') {
        out.pop_back();
    }
    return out;
}

std::optional<Reorder> computeReorder(std::span<const Anchor> anchors) {
    const auto freedom = reorderFreedomOf(*anchors[0].graph, anchors[0].node);
    if (!freedom) {
        return std::nullopt;
    }
    return Reorder{
        .regroup         = freedom->regroup,
        .permute         = freedom->permute,
        .blockIdentity   = freedom->blockIdentity,
        .contract        = freedom->contract,
        .changePrecision = freedom->changePrecision,
    };
}

std::string encodeReorder(const Reorder &r) {
    return flags({r.regroup, r.permute, r.blockIdentity, r.contract, r.changePrecision});
}

std::optional<Reorder> decodeReorder(std::string_view text) {
    const auto bits = unflags(text, 5);
    if (!bits) {
        return std::nullopt;
    }
    return Reorder{(*bits)[0], (*bits)[1], (*bits)[2], (*bits)[3], (*bits)[4]};
}

// ---- core.effects ----------------------------------------------------------------------------

std::string encodeEffects(const EffectSummary &e) {
    return flags({e.external, e.writesVariables, e.mayFault, e.mayNotTerminate});
}

std::optional<EffectSummary> decodeEffects(std::string_view text) {
    const auto bits = unflags(text, 4);
    if (!bits) {
        return std::nullopt;
    }
    return EffectSummary{
        .external        = (*bits)[0],
        .writesVariables = (*bits)[1],
        .mayFault        = (*bits)[2],
        .mayNotTerminate = (*bits)[3],
    };
}

// ---- core.loopDomain -------------------------------------------------------------------------

/// The number of elements a `for` takes along the first dimension of the array `node` holds as a
/// constant: `shape[0]`, which is the element count of a rank-1 array and the row count of a
/// higher-rank one (design principles 5.4: `t[i]` takes the i-th along the first dimension). The
/// author's ruling of 2026-10-24 (docs/handover/reviews/questions-2026-10-24-evening.md section
/// loop-domain-through-the-tensor-iterator) states it once: "`core.loopDomain`'s `extent` is
/// `shape[0]`, the rows of a constant array and the element count at rank 1, and every reader
/// shares that one fact". A numeric array is the tensor kind and the `for` takes its elements
/// itself, so the iterable of a loop over an array is the array, and its length is read where the
/// value is written.
std::optional<int64_t> constantLength(const GCGraph &graph, gc_node_ref_t node) {
    const GCNode *n = graph.node(node);
    if (n == nullptr) {
        return std::nullopt;
    }
    if (n->kind != GCNodeKind::Data || n->dataIndex >= 0 || graph.staticArea() == nullptr ||
        n->dataType() == nullptr || n->dataType()->code() != TypeCode::Array) {
        return std::nullopt;
    }
    const auto *array = camel::core::rtdata::fromSlot<::Array *>(
        graph.staticArea()->get<slot_t>(size_t(-n->dataIndex)));
    // `size()` is the first dimension (`extents_[0]`), the number of rows the loop walks, for an
    // array of any rank (runtime/rtdata/array.h).
    return array ? std::optional<int64_t>(static_cast<int64_t>(array->size())) : std::nullopt;
}

std::optional<LoopDomain> computeLoopDomain(std::span<const Anchor> anchors) {
    const GCGraph &graph = *anchors[0].graph;
    if (anchors.size() != 2 || graph.node(anchors[0].node)->kind != GCNodeKind::Loop) {
        return std::nullopt;
    }
    const LoopView loop(*anchors[0].program, graph, anchors[0].node);
    if (anchors[1].graph != &graph) {
        return std::nullopt;
    }
    LoopDomain domain{.kind = loop.kind()};
    if (const auto iterable = loop.iterable()) {
        domain.iterable    = *iterable;
        domain.extent      = constantLength(graph, *iterable);
        domain.elementPort = loop.bodyElementParam();
    }
    return domain;
}

std::vector<std::vector<Anchor>> loopAnchors(const Program &program, const GCGraph &graph) {
    std::vector<std::vector<Anchor>> all;
    for (gc_node_ref_t ref = 0; ref < graph.nodeCount(); ++ref) {
        if (graph.node(ref)->kind != GCNodeKind::Loop) {
            continue;
        }
        const LoopView loop(program, graph, ref);
        all.push_back(
            {Anchor{&program, &graph, ref},
             Anchor{&program, &graph, loop.bodyElementParam().value_or(kInvalidNodeRef)}});
    }
    return all;
}

// ---- core.permissions ------------------------------------------------------------------------

std::optional<int> computePermissions(std::span<const Anchor> anchors) {
    const camel::core::sem::PermissionMask mask =
        camel::core::tag::permissionsOf(*anchors[0].graph, anchors[0].node);
    return mask != 0 ? std::optional<int>(mask) : std::nullopt;
}

CoreKinds registerCoreKinds() {
    CoreKinds k{};
    k.shape = registerKind(KindSpec<TensorShape>{
        .key               = "core.shape",
        .owner             = "core",
        // compute reads the node's record and nothing else (its type): the shape holds while the
        // node does, though another node of its graph changed (facts.h, Dependency::Node).
        .dependency        = Dependency::Node,
        .readsDeclarations = false,
        .compute           = computeShape,
        .print             = [](const TensorShape &s) { return toString(s); },
        .encode            = encodeShape,
        .decode            = decodeShape,
    });
    k.instance = registerKind(KindSpec<NodeInstance>{
        .key     = "core.instance",
        .owner   = "core",
        .compute = computeInstance,
        .print   = printInstance,
    });
    k.reorder = registerKind(KindSpec<Reorder>{
        .key     = "core.reorder",
        .owner   = "core",
        .compute = computeReorder,
        .print   = [](const Reorder &r) { return toString(r); },
        .encode  = encodeReorder,
        .decode  = decodeReorder,
    });
    k.effects     = registerKind(KindSpec<EffectSummary>{
            .key     = "core.effects",
            .owner   = "core",
            .scope   = Scope::Graph,
            .compute = [](std::span<const Anchor> anchors) -> std::optional<EffectSummary> {
            return effectSummaryOf(*anchors[0].program, anchors[0].graph);
        },
            .print  = [](const EffectSummary &e) { return toString(e); },
            .encode = encodeEffects,
            .decode = decodeEffects,
    });
    k.loopDomain = registerKind(KindSpec<LoopDomain>{
        .key       = "core.loopDomain",
        .owner     = "core",
        .scope     = Scope::CrossGraph,
        .readsDeclarations = false,
        .compute   = computeLoopDomain,
        .print     = [](const LoopDomain &d) { return toString(d); },
        .anchorsIn = loopAnchors,
    });
    k.permissions = registerKind(KindSpec<int>{
        .key               = "core.permissions",
        .owner             = "core",
        .cls               = FactClass::GraphContent,
        .readsDeclarations = false,
        .compute           = computePermissions,
        .print = [](const int &mask) { return sem::permissionsText(static_cast<camel::core::sem::PermissionMask>(mask)); },
        .encode = [](const int &mask) { return std::to_string(mask); },
        .decode = [](std::string_view text) -> std::optional<int> {
            const auto value = integer(text);
            return value && *value > 0 && *value < 256 ? std::optional<int>(int(*value)) : std::nullopt;
        },
    });
    k.resources   = registerKind(KindSpec<NodeResources>{
          .key     = "core.resources",
          .owner   = "core",
          .compute = [](std::span<const Anchor> anchors) -> std::optional<NodeResources> {
            NodeResources r = resourcesOf(*anchors[0].program, *anchors[0].graph, anchors[0].node);
            return r.none() ? std::nullopt : std::optional<NodeResources>(std::move(r));
        },
          .print = [](const NodeResources &r) { return toString(r); },
          .anchorsIn =
            [](const Program &program, const GCGraph &graph) {
                std::vector<std::vector<Anchor>> all;
                for (gc_node_ref_t ref = 0; ref < graph.nodeCount(); ++ref) {
                    if (!resourcesOf(program, graph, ref).none()) {
                        all.push_back({Anchor{&program, &graph, ref}});
                    }
                }
                return all;
            },
    });
    return k;
}

} // namespace

void registerShapeReader(ShapeReader reader) {
    ShapeReaders &r = shapeReaders();
    std::unique_lock lock(r.mutex);
    r.readers.push_back(std::move(reader));
}

std::optional<TensorShape> shapeOfType(const camel::core::type::Type *type) {
    if (type == nullptr) {
        return std::nullopt;
    }
    ShapeReaders &r = shapeReaders();
    std::shared_lock lock(r.mutex);
    for (const ShapeReader &reader : r.readers) {
        if (auto shape = reader(type)) {
            return shape;
        }
    }
    return std::nullopt;
}

const CoreKinds &coreKinds() {
    static const CoreKinds kinds = registerCoreKinds();
    return kinds;
}

std::string toString(const TensorShape &shape) {
    std::string out = shape.dtype ? camel::core::type::typeCodeToString(*shape.dtype) : "?";
    if (!shape.dims) {
        return out + " [rank ?]";
    }
    out += " [";
    for (size_t i = 0; i < shape.dims->size(); ++i) {
        const std::optional<int64_t> extent = (*shape.dims)[i];
        out += (i ? ", " : "") + (extent ? std::to_string(*extent) : std::string("?"));
    }
    return out + "]";
}

std::string toString(const Reorder &r) {
    std::string out;
    for (const auto &[on, name] : {std::pair{r.regroup, "regroup"},
                                   std::pair{r.permute, "permute"},
                                   std::pair{r.blockIdentity, "block_identity"},
                                   std::pair{r.contract, "contract"},
                                   std::pair{r.changePrecision, "change_precision"}}) {
        if (on) {
            out += (out.empty() ? "" : ", ") + std::string(name);
        }
    }
    return out.empty() ? "in order" : out;
}

std::string toString(const EffectSummary &e) {
    std::string out;
    for (const auto &[on, name] : {std::pair{e.external, "external"},
                                   std::pair{e.writesVariables, "writes variables"},
                                   std::pair{e.mayFault, "may fault"},
                                   std::pair{e.mayNotTerminate, "may not terminate"}}) {
        if (on) {
            out += (out.empty() ? "" : ", ") + std::string(name);
        }
    }
    return out.empty() ? "none" : out;
}

std::string toString(const LoopDomain &d) {
    std::string out = camel::core::loopKindName(d.kind);
    if (!d.iterable) {
        return out + ": rounds unbounded (no sequence)";
    }
    out += std::format(": t sequential, t < len(node {})", *d.iterable);
    if (d.extent) {
        out += std::format(" = {}", *d.extent);
    }
    if (d.elementPort) {
        out += std::format("; element port {} reads xs(t)", *d.elementPort);
    }
    return out;
}

} // namespace camel::runtime::fact

