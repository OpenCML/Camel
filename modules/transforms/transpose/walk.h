// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * What one transposition's walk keeps between its steps: the state a build shares with the callees
 * it has already transposed, the description of one call of a callee that answers the pair
 * (value, tangent), and the member builder that `transpose/entry.cpp` starts from.
 *
 * This is the transform's own plumbing, not its contract: the entry point and `TransposeGraph` are
 * declared in `transpose.h`.
 */

#pragma once

#include "transpose.h"

#include "camel/core/context/context.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph.h"

#include <cstddef>
#include <optional>
#include <iosfwd>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace camel::transforms {

using camel::runtime::gc_node_ref_t;
using camel::runtime::GraphId;

namespace rt = camel::runtime;

/// What the graphs of one transpose share: one group (a callee's transpose is another member of
/// it, exactly as `jvp` lifts one graph per callee) and the callees already transposed, so that
/// two calls of one function call one graph.
struct TransposeState {
    camel::runtime::GraphDraftGroup &group;
    // program-holder-exempt: borrow-or-foreign-type -- the state borrows the program its pass
    // was given; the pass keeps it for the build.
    const camel::runtime::Program *program;
    std::unordered_map<uint32_t, GraphId> callees;
    /// The context whose rule table the transpose reads. `TransposeState` is where it belongs: it
    /// is what the build is handed (`buildTransposeGraph`), it is not a field of any value the
    /// language names, and the rule's right side is looked up by name in the table the context
    /// keeps.
    const camel::core::context::context_ptr_t &context;
    /// True inside a *probe*: the scratch build that discovers what a member needs, whose answer is
    /// thrown away with its draft (see `buildTransposeMember`). A probe never probes.
    bool inProbe = false;
    /// What each member needs, keyed by the graph whose member it is. A call site reads this before
    /// it computes the type it calls with: the capture ports are part of that type, so the set has
    /// to be known before the signature is written, not after the build that uses it.
    std::unordered_map<const camel::runtime::GCGraph *, std::vector<size_t>> calleeNeeds;
    /// Why the walk stopped, recorded at the site that stopped it. A probe carries a state of its
    /// own, so a probe's refusal never becomes the answer of the build that made it.
    TransposeRefusal refusal;
    /// The parameter whose *shape* the walk had to ask for and whose type does not state it: the
    /// cause the build refuses on when the member comes out with a port the operator's type has no
    /// room for. It is a fact and not a refusal: a call site inside a body carries the value (the
    /// capture is one of the member's ports, and the caller's type is written with it in hand), so
    /// asking is legitimate there and the walk must keep asking -- refusing at the step would break
    /// every `index_into` over an array whose extent only the caller knows. Whether the ask is
    /// fatal is a fact about the whole member, and `buildTransposeGraph` decides it once the ports
    /// are known: the shape has to be in the type or derivable from `ct`
    /// and known values -- and a value the caller carries *is* one.
    bool shapeWanted = false;
    /// The parameter the ask was about, and the name and type its declaration writes, for the
    /// sentence: the repair is that spelling. Recorded where the ask is made, because that is the
    /// one place that has the parameter and its declared type together.
    size_t shapeParam = 0;
    std::string shapeName;
    std::string shapeType;
};

/// One call of a callee that answers the pair (value, tangent), as a call site describes it to
/// `transposePairCall` below: the graph that is called, the call's own arguments, and the cotangent
/// of the pair's **tangent** component -- which is the only component a linear half has.
struct PairCall {
    camel::runtime::GCGraph *callee = nullptr;
    /// The source graph's call node: the place a refusal names.
    gc_node_ref_t ref = rt::kInvalidNodeRef;
    /// The call's ordinary arguments in the source graph, in the callee's parameter order.
    std::vector<gc_node_ref_t> arguments;
    /// The call's context (`with`) arguments in the source graph, in the callee's `with` order.
    std::vector<gc_node_ref_t> withArguments;
    /// This call's cotangent of the answer's tangent component.
    gc_node_ref_t cotangent = rt::kInvalidNodeRef;
};

/// What one node family is handed besides the node: the walk's state, the graph it is reading, and
/// the stream a refusal is written to. One struct for every family, because they differ in what
/// they do and not in what they are handed.
struct WalkContext {
    TransposeState &state;
    camel::runtime::GCGraph *function = nullptr;
    std::ostream &os;
};

/// Records the cause and writes the line for it, in the one place both are written: the cause on the
/// state (so the caller reads a value, not a sentence) and the sentence on the log. A site adds the
/// *where* -- the node, the uri, the callee -- because that is not the cause.
void refuseMember(WalkContext &ctx, TransposeRefusalCause cause,
                  std::optional<size_t> param = std::nullopt);

/// Records the parameter whose shape the walk had to ask for, and the name and type its declaration
/// writes: the parameter's place, its declared name, and the type whose extents are missing.
void noteUnstatedParameterOf(WalkContext &ctx, size_t param);
/// Builds the transposed member of one function into state's group and answers whether it did.
bool buildTransposeMember(
    TransposeState &state, camel::runtime::GCGraph *function, const GraphId member,
    std::ostream &os, const std::vector<bool> &participates = {},
    std::vector<size_t> *needs = nullptr);

} // namespace camel::transforms
