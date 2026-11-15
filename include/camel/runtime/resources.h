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
/// through references outside a sync block).
std::vector<RequiredOrdering> requiredOrderings(const Program &program, const GCGraph &graph);

// ---- Effect containment, the first commit check of the effect analysis --------------------------
//
// docs/design/effects/analysis.cn.md 4.1: "at each call site, the footprint of the function value
// called (by instantiation) must not exceed what the type at the call site declares; when it does,
// refuse, and the diagnostic names the call node and the accesses that are beyond". Read off the
// analysis above -- `footprintOf` for what the body does, the graph's function type for what it
// says -- and nothing else.
//
// It is a query rather than a check here because what to do with the answer -- which diagnostic,
// carrying which pass name -- belongs to the pass boundary that knows the pass (execute/pass.cpp).
// A pass that rewrites through the graph module alone cannot build an effectful node, so for it the
// answer is a theorem and it does not ask (the ruling of 2026-11-15, GraphIRPass::
// rewritesThroughTheGraphModuleOnly).

/// A call site of one graph that does more than its type says: the node, the type it declares as
/// the declaration spells it, and the accesses of the footprint the declaration does not cover.
struct EffectContainment {
    gc_node_ref_t call = kInvalidNodeRef;
    /// The declared clause, in the spelling a type uses (`pure`, `uses var stdout`).
    std::string declared;
    /// The accesses beyond it, one line each, sorted and without repeats.
    std::vector<std::string> beyond;
};

/// What the check looked at and what it declined to judge. A decline is not a verdict: it is the
/// analysis saying it cannot name the resource, and the count is here so that "I looked and it is
/// fine" and "I refused to look" cannot read the same in a log -- a checker that grows its declines
/// to stay green has to make a number grow first.
struct EffectContainmentReport {
    /// Call sites whose footprint was compared with the type at the site.
    size_t checked = 0;
    /// Readings of unknown class (GraphFootprint::anyOf, or an access of class Unknown): the
    /// analysis cannot name the resource, so a type that cannot spell it is not proved against it.
    size_t anyUnknown = 0;
    size_t anyObject = 0;
    size_t anyHandle = 0;
    /// A cell of a variable: not something a `uses` clause can spell (design effects plan 5.1).
    size_t variableCell = 0;
    /// A call site whose callee the graph does not resolve -- `__root__`'s indirect call of the
    /// entry, today. This one is a gap in the analysis rather than a limit of the language: the type
    /// at that call site is written, the checker just does not find the body behind the value. It
    /// has its own count for that reason, and it is a to-do, not a silence.
    size_t unresolved = 0;
    /// Readings declined for a reason the counts above do not name.
    size_t other = 0;
    size_t declined() const {
        return anyUnknown + anyObject + anyHandle + variableCell + unresolved + other;
    }
};

/// The first call site of `graph` -- FUNC and CALL nodes, by node order -- whose callee does more
/// than the type there says, or nullopt when every call site is covered. A type that carries no
/// clause (Generic) promises nothing and `covers` says it covers anything: it is not a violation.
/// So is a call through a value the graph does not resolve: there is no body to compare.
///
/// Only what a type can state is judged: an external resource by name, and a handle a parameter
/// names. The conservative half of a footprint -- the classes it "cannot name"
/// (GraphFootprint::anyOf), a cell of a variable (whose mutability is declared where the variable
/// is, design effects plan 5.1, not in a `uses` clause) -- is not an access the callee is known to
/// make, and a type that cannot spell it does not promise against it. Writing variables is what the
/// order check asks about. `report`, when given, counts what was judged and what was declined.
std::optional<EffectContainment> effectContainmentViolation(
    const Program &program, const GCGraph &graph, EffectContainmentReport *report = nullptr);

/// The clause a graph's body writes: `footprintOf` measured, said in the graph's own numbering --
/// the same conversion `effectContainmentViolation` compares a call site against, read the other
/// way. A pass that rewrote a body reads it to bring the type over that body up to date, instead of
/// leaving a caller to find out at the next boundary.
sem::ResourceSignature bodySignature(const Program &program, const GCGraph &graph);

/// `declared` widened to name everything `body` names and never narrower: what a type has to carry
/// once the body under it reaches more than the clause did. A clause the `body` cannot be written
/// as leaves `declared` as it is (widening never invents a promise the language cannot spell).
sem::ResourceSignature widened(
    const sem::ResourceSignature &declared, const sem::ResourceSignature &body);

/// The text forms (core.resources, test output).
std::string toString(const NodeAccess &access);
std::string toString(const NodeResources &resources);
std::string toString(const GraphFootprint &footprint);

} // namespace camel::runtime
