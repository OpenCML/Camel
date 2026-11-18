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

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace camel::core::context {
class Context;
} // namespace camel::core::context

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
    /// The declaration of the operator names this resource `unordered` (design principles 3.3), so
    /// how two accesses to it are written keeps no order and the declaring library promises the
    /// order does not matter. Carried here because the order check reads a node's accesses and not
    /// the operator's spec, and the promise is the operator's.
    bool unordered = false;

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

// ---- Which port of a graph an access came through -------------------------------------------------
//
// A resource use that reaches its graph through a port is written down by the port's number in the
// graph's own numbering (sem::ResourceSignature: `with` parameters first, then ordinary ones, and a
// capture by its position). Two readers need that number, and they must not spell it themselves:
// this analysis binds an access to the port it came through, and the GIR printer says which port a
// drawn node is. The last group is the one that bit -- a graph that a direct call already runs
// holds the values it captured as further `with` ports after the ones it declares
// (`Builder::captureInto`), and numbering those by their `with` index names a slot no clause ever
// wrote. `Builder::numberBodyPorts` numbers them by their capture position, so this does too.

/// Which of a graph's three port groups a port belongs to.
enum class PortGroup : uint8_t {
    None = 0,
    Norm,
    With,
    Capture,
};

/// A port, in the group whose numbering names it.
struct PortPlacement {
    PortGroup group = PortGroup::None;
    /// The index the group's own numbering gives this port: the norm port's position, the declared
    /// `with` port's position, or the capture's position in the graph's capture table.
    size_t index = 0;
};

/// How many `with` ports `graph` declares, as opposed to the ones that carry its captures. A
/// capture is appended after every declared `with` port (`Builder::captureInto`), so the declared
/// ones are the list's length less the capture table's -- and the whole list when the graph holds
/// its captures as closure ports instead, or when the table is longer than the list (which no
/// built graph is, and which must not underflow a count either).
size_t declaredWithPorts(const GCGraph &graph);

/// Which group `ref` is a port of, and where it stands there. A node of no group is
/// `PortGroup::None` -- no built graph has one, and a reader that meets one has no number to give.
PortPlacement portPlacement(const GCGraph &graph, gc_node_ref_t ref);

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
    size_t anyObject  = 0;
    size_t anyHandle  = 0;
    /// A cell of a variable: not something a `uses` clause can spell (design effects plan 5.1).
    size_t variableCell = 0;
    /// A call site the check could not read at all, counted rather than passed: no function value
    /// at its first `with` input, or one whose body the graph does not name -- a `Fill` closure
    /// value, a parameter of function type, a value an operator made. It is a gap in the analysis
    /// and not a limit of the language: there is a body behind the value, the checker just cannot
    /// find it from the graph. It has its own count for that reason, and it is a to-do, not a
    /// silence.
    size_t unresolved = 0;
    /// Readings declined for a reason the counts above do not name.
    size_t other = 0;
    /// The two numbers answer different questions and do not add up to one another: `checked`
    /// counts call sites, `declined` counts readings, and one site can contribute several readings.
    /// The summary line says which unit is which, so that no one reads "19 checked plus 3 declined"
    /// as 22 call sites.
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
sem::ResourceSignature
widened(const sem::ResourceSignature &declared, const sem::ResourceSignature &body);

/// The text forms (core.resources, test output).
std::string toString(const NodeAccess &access);
std::string toString(const NodeResources &resources);
std::string toString(const GraphFootprint &footprint);

// ---- The commit checks of the effect analysis, asked of one graph as one question ----------------
//
// docs/design/effects/analysis.cn.md 4.1 names the two things a commit owes the program it
// publishes. They are one question here because both commit paths (a pass, and the graph module's
// `commit`) have to ask both, and a second copy of either reading would be a second answer:
//
//   Effect  the footprint the graph's body reaches (resolved bottom-up from the nodes' operator
//           declarations, principle 3.3) must stay inside the clause the graph's own type
//           declares. What the type cannot spell -- a cell of a variable, a class the analysis
//           cannot name -- is not judged: a possibility is not an access (the same line the
//           call-site check draws).
//   Order   within one region, any two accesses to one resource of which at least one writes must
//           have a path between them in the graph (a data, sync or order edge, or a region's
//           `GATE` input). A region is the body, an arm, a loop's condition or a loop's body; an
//           owner node (the BRCH or LOOP) stands in the region it sits in for every region under
//           it, so two accesses in different arms of one branch are never asked to be ordered --
//           they never both run -- while an access inside a loop and one outside are ordered
//           through the loop node. A resource a declaration names `unordered` carries no promise
//           and is passed over (principle 3.3).
//
// The reading is per graph because a commit edits graphs; which of them it must answer for, and for
// which nodes, is the caller's question (`commitViolation`'s `made` and `askOrder`).

enum class CommitCheck : uint8_t {
    Effect = 0, // the body reaches past the type
    Order  = 1, // two conflicting accesses of one region no path orders
};

std::string_view commitCheckName(CommitCheck check);

/// A commit that must be refused: which of the two checks, the graph, the nodes the reading names,
/// and the one line the diagnostic carries. `origin` is the source position to report against (0
/// when the program does not name one), so that a caller with a diagnostic can attach it.
struct CommitViolation {
    CommitCheck check     = CommitCheck::Effect;
    const GCGraph *graph  = nullptr;
    gc_node_ref_t first   = kInvalidNodeRef;
    gc_node_ref_t second  = kInvalidNodeRef;
    uint64_t origin       = 0;
    std::string text;
};

/// The pair of questions analysis 4.1 asks of a commit, answered for one graph: does its body stay
/// inside the effect its own type declares, and do two conflicting accesses in one region have a
/// path that orders them. Both read the accesses their nodes' operator declarations give them
/// (principle 3.3), bottom-up, and neither reads an operator's name.
///
/// `made` is what the commit made here, and it scopes the order question only: a pair is judged when
/// at least one of its two nodes is one this commit made, because an ordering the program already
/// lacked before the commit is not this commit's to answer for. `askOrder` says whether the question
/// is asked at all -- a commit that made no node cannot have added a write and cannot have dropped an
/// order edge (the public edits of the graph module keep both promises), so it has nothing to answer
/// for in ordering. Containment is a property of the graph as a whole -- the body either fits the
/// type it publishes or it does not -- so it is asked whatever `made` is.
std::optional<CommitViolation> commitViolation(
    const Program &program, const GCGraph &graph, std::span<const gc_node_ref_t> made = {},
    bool askOrder = true);

/// A commit that would publish a graph breaking one of the two promises above: the body stays
/// inside the effect its own type declares, and two conflicting accesses in one region have a path
/// ordering them. The refusal is carried whole so that a caller which catches it reports the same
/// diagnostic the reading named, with the same graph, nodes and source position; `what()` is that
/// line. It is thrown where the graphs of a commit are read (draft_session.cpp), which is the one
/// place a graph becomes a graph either commit path can publish.
class GraphCommitRefused : public std::runtime_error {
  public:
    explicit GraphCommitRefused(CommitViolation violation)
        : std::runtime_error(violation.text), violation_(std::move(violation)) {}

    const CommitViolation &violation() const { return violation_; }

  private:
    CommitViolation violation_;
};

/// Records a refusal as the diagnostic both commit paths carry, and says who asked: `site` is the
/// pass's name, or `graph::commit` for the graph module. The diagnostic is the report; the caller
/// still has to stop, which is why this answers nothing.
void reportCommitViolation(
    const std::shared_ptr<camel::core::context::Context> &ctx, const std::string &site,
    const CommitViolation &violation);

/// Whether a commit asks the two checks above. The ruling of 2026-11-15 stopped asking them of a
/// rewrite whose only interface is the graph module, because such a rewrite cannot make a node that
/// carries an access and cannot drop an edge that orders one; the ruling of 2026-11-16 keeps that
/// saving only where it is paid for -- a shipped build (`NDEBUG`) does not ask, a debug build asks,
/// and a test asks through `setCommitChecksEnabled`. Asking is a build's answer, so it is one cell
/// and not a setting of each call site: `camel::execute::camelPassCommitChecksEnabled` reads this
/// same cell (src/execute/pass.cpp), and the two paths cannot drift into two answers.
bool commitChecksEnabled();
void setCommitChecksEnabled(bool enabled);

} // namespace camel::runtime
