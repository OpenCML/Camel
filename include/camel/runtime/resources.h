// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The resources of the nodes of a frozen graph (docs/design/op-semantics, section 8): what each
 * node reads and writes besides its operands' values, and the questions a scheduler asks about
 * two nodes. The declarations are the operators' (camel/core/resource_semantics.h); this file
 * resolves them in a graph.
 *
 *   resourcesOf(program, graph, node)     the accesses of one node, each with the node of the same
 * graph whose value names the resource. An OPER: its operator's declaration, or the conservative
 * reading. A FUNC, a LOOP, a CALL whose callee is known: what the graphs it runs do to resources
 * they receive (their footprint), mapped onto the node's inputs; a CALL through an unknown value:
 * conservative. Other nodes touch nothing. footprintOf(program, graph)           what running a
 * graph once does to resources it does not make itself: through its ports, its captures, ambient
 * state. originOf / mayAlias          where a resource value comes from within one activation of
 * its graph (a fresh allocation of a node, a port, or unknown), and whether two values may be one
 * resource. resourceConflict(a, b)       whether two nodes touch overlapping parts of one resource
 * with at least one write: they must then run in the order the program gives them. A conflict is
 * judged by resource alone (design principles 3.3): two effects on different resources are not a
 * pair, and the order between effects on one resource is the program's to write (a `sync` block);
 * an effect outside the program that names no resource is the "global" resource and meets every
 * effect. dependsOn(later, earlier)    computation dependence: the data edges (Norm,
 * With) only. orderedBefore(a, b)          any path, control edges included (the order the front
 * end gave conflicting effects, design principles 3.3). requiredOrderings(graph)     every pair of
 * nodes of a graph that conflict, with whether and how the graph orders them.
 *
 * Computation dependence, resource conflict and asynchronous completion are three different
 * things (design 8.2): the first is the graph's data edges, the second is answered here, the
 * third is not the core's (a node's result is there once it has run; a device's own
 * asynchrony is its back end's). Nothing here changes a graph or what any back end does.
 *
 * Conservative throughout: an operator without a declaration reads and writes every operand
 * and may have an external effect; a value whose origin cannot be told may be any resource of
 * its class; a region that cannot be computed is the whole resource.
 */

#pragma once

#include "camel/core/index_semantics.h"
#include "camel/core/resource_semantics.h"
#include "camel/runtime/graph.h"

#include <iosfwd>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace camel::runtime {

namespace sem = camel::core::sem;

/// One access of a node to a resource, resolved in the node's graph.
struct NodeAccess {
    sem::Resource resource;
    sem::AccessMode mode = sem::AccessMode::ReadModifyWrite;
    sem::ResourceRegion region;
    /// The node of the same graph whose value names the resource (the input the declaration's
    /// binding selects); kInvalidNodeRef for an ambient resource and for `anyValue`.
    gc_node_ref_t value = kInvalidNodeRef;
    /// The resource cannot be told: any resource of its class the node can reach (an
    /// undeclared operator's operand of unknown origin, an element of a loop's array, a call
    /// through a value).
    bool anyValue = false;

    bool operator==(const NodeAccess &) const = default;
};

/// What one node does to resources.
struct NodeResources {
    std::vector<NodeAccess> accesses;
    sem::ResultAlias result;
    std::optional<sem::Resource> allocates;
    bool external = false;
    /// The accesses come from declarations (and the footprints of graphs built from them); false
    /// when any part is the conservative reading of something undeclared or unknown.
    bool declared = true;

    /// Touches no resource: no access, nothing allocated, no external effect (how the result
    /// relates to the operands' resources is not a touch).
    bool none() const { return accesses.empty() && !allocates && !external; }
    bool operator==(const NodeResources &) const = default;
};

/// The resources of node `node` of `graph` (see the file comment).
NodeResources resourcesOf(const Program &program, const GCGraph &graph, gc_node_ref_t node);

/// What running `graph` once does to resources it receives (bindings Norm, With and Capture are
/// its norm ports, with ports and closure ports), to ambient state, and outside the program.
/// Accesses to resources the graph makes itself are not part of it; regions are whole.
struct GraphFootprint {
    std::vector<sem::ResourceAccess> accesses;
    /// It touches resources it cannot name (an undeclared operator on a value of unknown origin,
    /// a call through a value): any resource of the classes listed may be read and written.
    std::vector<sem::ResourceClass> anyOf;
    /// Where the graph's return value comes from (design effects plan 3.3): Fresh when it makes
    /// one, Same when it returns one of its norm ports (directly, through a GATE or through a
    /// call that returns the parameter), Unknown otherwise. A `Same` names the norm port.
    sem::ResultAlias result;
    bool external = false;
    bool declared = true;

    bool operator==(const GraphFootprint &) const = default;
};

GraphFootprint footprintOf(const Program &program, const GCGraph *graph);

/// Where a value comes from within one activation of its graph, seen through GATEs.
struct ValueOrigin {
    enum class Kind : uint8_t {
        Fresh,   // made by this node in this activation (an operator whose result is fresh, a COPY)
        Port,    // received: a norm or with port, a closure port
        Unknown, // anything else (a call's result, a projection, a value read from a variable)
    };
    Kind kind          = Kind::Unknown;
    gc_node_ref_t node = kInvalidNodeRef;

    bool operator==(const ValueOrigin &) const = default;
};

/// `program` is needed to resolve a call's result: a FUNC or CALL node's value may be its
/// callee's result, which is a fact about the callee's graph (or its function type).
ValueOrigin originOf(const Program &program, const GCGraph &graph, gc_node_ref_t value);

/// Whether the values of `a` and `b` (nodes of `graph`) may be the same resource: the same
/// origin; never two different fresh allocations, nor a fresh allocation and a received value
/// (what a caller passes exists before the activation that allocates); otherwise yes (two ports
/// may receive one cell).
bool mayAlias(const Program &program, const GCGraph &graph, gc_node_ref_t a, gc_node_ref_t b);

/// The bounding box of the elements a mapped region reaches: one inclusive [lo, hi] per axis
/// of the resource, or `empty` when the instance's domain has no point.
struct RegionBounds {
    bool empty = false;
    std::vector<std::pair<int64_t, int64_t>> axes;
};

/// The bounds of `region` (ResourceRegion::Kind::Mapped) in `instance`, the index instance of
/// the node that accesses it: the range of each coordinate of the map it names over the domain
/// (the guard ignored: a superset). nullopt when it cannot be told (not Mapped, an indirect or
/// non-affine map, an unknown parameter): read as the whole resource.
std::optional<RegionBounds>
regionBounds(const camel::core::sem::IndexedInstance &instance, const sem::ResourceRegion &region);

/// Whether two regions of one resource may overlap; unknown bounds (nullopt) overlap anything
/// that is not empty.
bool regionsMayOverlap(const std::optional<RegionBounds> &a, const std::optional<RegionBounds> &b);

/// Why two nodes must keep their relative order.
enum class ConflictKind : uint8_t {
    None,
    Resource, // they touch overlapping parts of one resource and at least one writes
};

std::string_view conflictKindName(ConflictKind kind);

struct ResourceConflict {
    ConflictKind kind = ConflictKind::None;
    /// Resource: the accesses of the two nodes (indexes into their NodeResources::accesses).
    size_t accessA = 0, accessB = 0;
    /// One line, for diagnostics and tests.
    std::string why;

    explicit operator bool() const { return kind != ConflictKind::None; }
};

/// Whether nodes `a` and `b` of `graph` conflict (see ConflictKind). Symmetric.
ResourceConflict
resourceConflict(const Program &program, const GCGraph &graph, gc_node_ref_t a, gc_node_ref_t b);

/// Computation dependence: `later` uses the value of `earlier`, directly or not, through Norm
/// and With edges only.
bool dependsOn(const GCGraph &graph, gc_node_ref_t later, gc_node_ref_t earlier);

/// Any path from `a` to `b`, control edges included.
bool orderedBefore(const GCGraph &graph, gc_node_ref_t a, gc_node_ref_t b);

/// A pair of nodes of one graph that conflict, and how the graph orders them.
struct RequiredOrdering {
    gc_node_ref_t first  = kInvalidNodeRef; // the one the graph runs first, when it orders them
    gc_node_ref_t second = kInvalidNodeRef;
    ConflictKind why     = ConflictKind::None;
    /// The graph orders them (a path from first to second).
    bool ordered = false;
    /// The path is a data path (dependsOn): their order needs no control edge.
    bool byData = false;
};

/// Every conflicting pair of nodes of `graph`, by node order, except pairs in different arms of
/// one branch (they never both run). A pair the graph does not order (ordered = false) is two
/// effects on one resource the program left unordered (design principles 15, undecided: writes
/// through references outside a sync block without `wait`).
std::vector<RequiredOrdering> requiredOrderings(const Program &program, const GCGraph &graph);

/// The text forms (core.resources, test output).
std::string toString(const NodeAccess &access);
std::string toString(const NodeResources &resources);
std::string toString(const GraphFootprint &footprint);

} // namespace camel::runtime
