// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Reverse-mode differentiation of runtime graphs by pullback closures.
 *
 * After macros have run, a program is a static graph, so derivatives are a
 * graph rewrite rather than a runtime tape. For a function graph g the
 * transform produces
 *
 *     g.fwd : (inputs) => (value, pullback)
 *     g.pb  : (dy) => tangents of g's inputs        (a closure over residuals)
 *
 * g.fwd is g with every differentiable call rewritten to call the callee's
 * .fwd graph; g.pb walks g backward from dy, applying the rules published in
 * the core DerivativeRegistry to operators and calling the pullbacks returned
 * by callees. The captured residuals and callee pullbacks are the tape, held
 * by ordinary closures.
 *
 * Control flow needs no special runtime support:
 *   - a branch's arms are subgraph calls; each arm is differentiated like a
 *     callee (after giving all arms the same inputs), and the JOIN merges
 *     their (value, pullback) pairs, so the pullback of the taken arm is the
 *     one called backward;
 *   - recursion is a call of a graph whose .fwd is being built: the pullback
 *     of one step captures the pullback of the next.
 * When shapes are static, inlining and simplification passes flatten the
 * closures back into straight-line code.
 *
 * grad(f) itself is built the same way, except that f's backward pass is
 * emitted into f's own forward graph instead of a separate pullback.
 */

#pragma once

#include "camel/core/context/context.h"
#include "camel/core/type/composite/func.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/graph_txn.h"

#include <optional>
#include <vector>

namespace camel::autodiff {

namespace type = camel::core::type;

/// What grad(f) differentiates and returns. The differential form differentiates f's **first
/// `with` parameter** (the model's weights, design principles 9.9); the other `with` parameters
/// are constants and the ordinary parameters are data. The gradient is shaped like that first
/// `with` parameter.
struct GradientSignature {
    type::Type *gradientType = nullptr;
    /// Type of grad(f), or of with_grad(f) whose result is (value, gradient).
    type::FunctionType *functionType = nullptr;
};

/// Why f cannot be differentiated: its result is not a float, it has no `with` parameter, its
/// first `with` parameter is a `var`, or that parameter's type has no tangent. Empty when it can
/// be differentiated. With `hasAux` the result must be a pair `(value, aux)` whose first element
/// is the float that is differentiated; the pair's second element is handed back unchanged, as
/// JAX's `has_aux` does.
std::string differentiationRefusal(type::FunctionType *function, bool hasAux = false);

/// The signature of grad(f) (or with_grad(f)), or nullopt when f cannot be differentiated
/// (see differentiationRefusal). With `hasAux` the transform gives `(gradient, aux)` where grad
/// gives the gradient, and `((value, aux), gradient)` where with_grad gives the pair, which is
/// JAX's order for both.
std::optional<GradientSignature>
gradientSignature(type::FunctionType *function, bool withValue, bool hasAux = false);

/// The graph of a gradient and the program of the graphs built with it.
struct GradientGraph {
    // program-holder-exempt: long-lived-cache -- tracking here would root every gradient ever
    // built (buildGradientGraph, test/plans/feat/mm/graph_space).
    const camel::runtime::Program *program = nullptr;
    camel::runtime::GCGraph *graph         = nullptr;
};

/// Where the number of rounds a reverse loop counts backwards from comes from. A reverse pass runs
/// the rounds the forward pass ran, from the last to the first, and the two writers of one differ
/// only in how they know that number: the pullback reads the tape the forward half recorded, and
/// the transpose of a loop recomputes it from the loop's own control (an interval's bounds, an
/// array's length, a condition re-run). One statement of "how many rounds were there", so the two
/// callers read the same answer through the same function instead of each holding its own count.
struct ReverseRoundCount {
    enum class Source : uint8_t {
        /// The residuals the forward pass recorded, one per round: how many there are is the count.
        TapeLength,
        /// A value the caller computed by re-running the loop's control (no recording involved).
        Recomputed,
        /// A counter carried beside a loop's own states while the loop's body is replayed: the
        /// condition reads a value the body produced, so the rounds are known only by running them
        /// (`transpose/walk.cpp`). The number is the counter's last value, not a length taken off
        /// an array.
        Replayed,
    };
    Source source = Source::TapeLength;
    /// The residual array, when the source is TapeLength.
    camel::runtime::gc_node_ref_t tape = camel::runtime::kInvalidNodeRef;
    /// The number itself, when the source is Recomputed.
    camel::runtime::gc_node_ref_t value = camel::runtime::kInvalidNodeRef;
};

/// The round a reverse counting loop starts at: `rounds - 1`, with the number taken from the
/// caller's source. The value is an `int` the condition region compares against zero.
camel::runtime::gc_node_ref_t
reverseFirstRound(camel::runtime::GraphTransaction &transaction, const ReverseRoundCount &rounds);
/// The condition region of a reverse counting loop: one more round while the round index -- the
/// first of the loop's states -- is not negative. A reverse pass never asks the source loop's own
/// condition; it counts the rounds the forward pass took, backwards, and this is the test that
/// says when the count is spent. Shared with the transpose of a loop, so the two writers of a
/// reverse loop state the same condition in the same words.
void buildReverseCondRegion(
    camel::runtime::GraphTransaction &transaction, uint32_t region,
    std::span<camel::core::type::Type *const> stateTypes);
/// Builds the graph of grad(f) (or with_grad(f)) for the graph `function` of `program`.
GradientGraph buildGradientGraph(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    camel::runtime::GCGraph *function, bool withValue, bool hasAux = false);

} // namespace camel::autodiff
