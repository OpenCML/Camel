// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Rebuilding a graph under a type substitution.
 *
 * A frozen graph's node types are written into the record when the graph is encoded
 * (GCNode::typeId, from the draft's node type or from the slot the node reads; graph.cpp
 * typeIdOf / effectiveDraftNodeType), together with the frame layout, the function type and the
 * static area's layout. Nothing of that is a column a pass can rewrite afterwards, and every
 * other graph that calls this one, every reader that already holds the version, and the back end
 * that laid out its frame were all built for the type it has. Replacing a type therefore means
 * **building the graph again** -- the same nodes, the same edges, the same regions, and the new
 * types -- and publishing the result as a new version (design principles 2.1, 9.7). The old
 * version keeps serving whoever holds it, which is exactly why this must not be an edit in place.
 *
 * What the rebuild is:
 *   - `retypeDraft` rewrites one draft: every live node's type, the layout of the runtime slots,
 *     the layout of the static area (but for a slot whose value is an object of the replaced type:
 *     see `leaveStaticObjects`), the function type, the closure type and the form ports of the
 *     regions. Node kinds, node ids, the five input lists, the region table, the ports and the
 *     reference table are left exactly as they are.
 *   - `retypeGraph` decodes one frozen graph, does that, and commits: the graph it returns is the
 *     version the program now holds, and the one it was given is untouched.
 *   - `retypeProgram` does it for every graph the program reaches, in one commit. This is the form
 *     a whole-program type change needs: a call is checked against its callee's signature and a
 *     JOIN against the types of its arms (graph_edges.cpp), so retyping one graph alone leaves
 *     the two sides disagreeing and the commit refuses the result.
 *
 * What it is not:
 *   - It says nothing about values. A substitution is a statement about types; a node that
 *     computes a value of the old type still computes exactly what it computed (an OPER keeps the
 *     operator it resolved for the old types, unless the caller retargets it), and a static value
 *     stays the object it is. A caller that replaces a type with one of another representation
 *     also rewrites the nodes that build and consume it; that is the rewrite, this is its type
 *     half.
 *   - A static slot whose value is an object of the old type keeps its type and its value
 *     (`GraphRetypeOptions::leaveStaticObjects`), or the rebuild refuses the graph
 *     (std::runtime_error): the value keeps the class it was built with, and the collector is
 *     handed the declared type of the slot when it relocates what the value holds. The node that
 *     reads the value is retyped like every other node, so reconciling the two is the job of the
 *     rewrite that replaces the constructor.
 *   - A component of a *named* type (a declared struct, an enum instance, a module type) is not
 *     reached: a named type's identity is its declaration, so the table has to name that type
 *     itself. Components of tuples, arrays, streams and function types are reached.
 *   - A rebuilt graph is not a graph to *run* before the other half of the rewrite. Its values are
 *     the ones the old type built -- a tuple object where the graph now says enum -- and the
 *     runtime reads a value as the type its record declares (the return of a function, a match, a
 *     print). Measured on the probe case: the rebuilt program commits and dumps, and dies at
 *     run time. Rebuilding the types and rewriting the nodes that build and consume the value are
 *     one change, made in one pass; this is the type half of it, and it is useful on its own for
 *     reading, checking and measuring the type change before the values follow.
 */

#pragma once

#include "camel/core/type/base.h"
#include "camel/runtime/draft.h"

#include <functional>
#include <span>
#include <utility>
#include <vector>

namespace camel::runtime {

class RuntimeGraphDraftSession;
class GCGraph;
class Program;

/// A set of replacements of one type by another, by `TypeId` (the id a frozen node's record
/// stores). Applying it to a type gives the type the table names for it, or -- when a component is
/// named -- the same composite built with the components replaced.
class GraphTypeSubstitution {
  public:
    using Pair = std::pair<camel::core::type::TypeId, camel::core::type::TypeId>;

    GraphTypeSubstitution() = default;
    GraphTypeSubstitution(std::initializer_list<Pair> pairs);
    explicit GraphTypeSubstitution(std::span<const Pair> pairs);

    /// Records that every occurrence of `from` becomes `to`. One step only: an entry whose target is
    /// another entry's source is not followed (a chain is a mistake, not a shortcut).
    void replace(camel::core::type::TypeId from, camel::core::type::TypeId to);
    void replace(camel::core::type::Type *from, camel::core::type::Type *to);

    bool empty() const { return pairs_.empty(); }
    size_t size() const { return pairs_.size(); }
    const std::vector<Pair> &pairs() const { return pairs_; }

    /// The type this table gives for `type`: the entry naming it, else the same composite with
    /// `apply` run over its components, else `type` itself. Never null for a non-null argument.
    camel::core::type::Type *apply(camel::core::type::Type *type) const;

  private:
    std::vector<Pair> pairs_;
};

/// What a rebuild may change beyond the table.
struct GraphRetypeOptions {
    /// Called for every live node with the type the table gave for its old one, and may return the
    /// type that node is to have (nullptr: keep the table's answer). It exists for the rewrites
    /// that know one node's type better than the table does -- a constructor whose payload is one
    /// variant -- and it must not change what the node does: kinds, inputs and payloads are not its
    /// business.
    std::function<camel::core::type::Type *(
        const GraphDraft &draft, gc_node_ref_t node, GCNodeKind kind,
        camel::core::type::Type *oldType, camel::core::type::Type *mapped)>
        nodeType;

    /// Leave the declared type of a static slot whose *value* is an object of the replaced type.
    /// Off by default, and the rebuild refuses the case instead (`std::runtime_error`).
    ///
    /// A static slot is not a column a rebuild may rewrite over an object: the value keeps the
    /// class it was built with, and a reader -- the collector above all -- is handed the declared
    /// type when it relocates what the value holds. So the layout keeps the type of the value it
    /// describes, and the node that *reads* that value is retyped like every other node: the graph
    /// then says the value is the new type while the static area says what the value really is,
    /// which is the same stale-value situation as everywhere else in a rebuild and is reconciled
    /// by the rewrite that replaces the constructor.
    ///
    /// It exists because the case is not rare but *typical*: the aggregates a program builds are
    /// kept as static templates (a tuple literal of type `T` puts a `T` in the static area), so a
    /// whole-type substitution on a program that builds a value of the type meets this slot.
    bool leaveStaticObjects = false;
};

/// What one draft's rebuild did. Every count is of the items whose type actually changed, so an
/// all-zero report is a graph the table did not touch at all.
struct GraphRetypeReport {
    size_t nodes               = 0; // live nodes looked at
    size_t edges               = 0; // inputs of every kind, summed over the live nodes
    size_t retypedNodes        = 0;
    size_t retypedSlots        = 0; // runtime slots (the frame layout)
    size_t retypedStaticSlots  = 0; // static slots (the static area's layout)
    size_t leftStaticSlots     = 0; // static slots whose value is an object of the old type
    size_t retypedRegions      = 0; // region form ports
    bool signature             = false;
    bool closure               = false;

    bool changed() const {
        return retypedNodes != 0 || retypedSlots != 0 || retypedStaticSlots != 0 ||
               retypedRegions != 0 || signature || closure;
    }
};

/// Rewrites `draft` so that every type it names is the one the table gives for it. Kinds, ids,
/// edges, regions and references are untouched. Throws std::runtime_error when a static value
/// would have to be given a type its object does not have, unless `leaveStaticObjects` says to
/// leave it.
GraphRetypeReport retypeDraft(
    GraphDraft &draft, const GraphTypeSubstitution &types,
    const GraphRetypeOptions &options = {});

/// The rebuild of one frozen graph: decode `source` into the session, retype it, commit. Returns
/// the version of that graph in the program the commit made -- `source` itself when nothing
/// changed and the commit recognized the content (a rebuild of nothing is the same graph). The
/// commit is the session's, so it also publishes what else the caller edited in it.
///
/// `report` is filled with what the draft's rebuild changed (when it is not null).
GCGraph *retypeGraph(
    RuntimeGraphDraftSession &session, GCGraph *source, const GraphTypeSubstitution &types,
    const GraphRetypeOptions &options = {}, GraphRetypeReport *report = nullptr);

/// What a whole-program rebuild did.
struct ProgramRetypeReport {
    size_t graphs        = 0; // graphs the program reaches
    size_t rebuilt       = 0; // of them, the ones the table changed
    size_t retypedNodes  = 0;
    size_t retypedStatic = 0;
    size_t leftStatic    = 0; // static values that kept their type: see leaveStaticObjects
    const Program *program = nullptr; // the program the commit made
};

/// The whole-program form: retype every graph the program reaches and commit once. The graphs keep
/// their GraphIds (a rebuild is a new version of the same graph, not another graph), so functions,
/// references and specializations keep naming them.
const Program *retypeProgram(
    RuntimeGraphDraftSession &session, const GraphTypeSubstitution &types,
    const GraphRetypeOptions &options = {}, ProgramRetypeReport *report = nullptr);

} // namespace camel::runtime