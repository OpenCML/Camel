/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 *
 * Author: Camel Contributors
 * Created: Sep. 28, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Generic graph optimizations. Algebra recognizes core scalar arithmetic by OperId;
 * the other passes consult declared properties, never module-specific knowledge:
 *
 *   std::opt::fold  evaluates pure operators whose inputs are all static and
 *                   replaces them by static values, and replaces an element
 *                   projected out of a tuple built in the same graph by the
 *                   value filled there;
 *   std::opt::cse   merges pure operator nodes with identical operator and
 *                   inputs;
 *   std::opt::algebra combines local scalar arithmetic, with optional safe_fp mode;
 *   std::opt::dce   removes pure operator (and value-only) nodes whose results
 *                   are unused, SYNC control joins, and GATEs used only for control;
 *                   also bypasses non-anchor GATEs by transferring waits to mandatory users,
 *                   within an edge-growth budget;
 *                   their ordering is transferred to their control users.
 *   std::opt::ctrl  removes control edges already implied by mandatory data/control paths,
 *                   within a work budget and without changing branch boundaries or arm anchors.
 *
 * std::opt runs call rewriting, fold, cse, algebra and dce to a fixpoint, then ctrl once.
 *
 * Together with call rewriting (devirtualization, which also turns calls of closures
 * built in the caller into direct calls of a lambda-lifted graph, and
 * inlining), they collapse the pullback closures of autodiff into straight-line
 * code when the program is static.
 *
 * These passes keep side-effect ordering intact: when a node on a control chain
 * (every call in `sync` code is on one) is replaced, all of its users inherit
 * its control predecessors, and nodes that anchor the graph (exit, output,
 * return, branch-arm heads and tails) keep their identities. Algebra may rewrite an
 * anchor's arithmetic in place while retaining its dependencies.
 */

#pragma once

#include "camel/execute/pass/opt.h"

class ConstantFoldPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

class CommonSubexpressionPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

class AlgebraPass : public RuntimeGraphRewritePass {
  public:
    explicit AlgebraPass(
        const camel::core::context::context_ptr_t &ctx, bool safeFp = false,
        bool neutralOnly = false)
        : RuntimeGraphRewritePass(ctx), safeFp_(safeFp), neutralOnly_(neutralOnly) {}
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;

  private:
    bool safeFp_;
    bool neutralOnly_;
};

/// std::opt::extract: one writing for every value that has several (runtime/writings.h).
class ExtractPass : public RuntimeGraphRewritePass {
  public:
    ExtractPass(const camel::core::context::context_ptr_t &ctx, std::string cost)
        : RuntimeGraphRewritePass(ctx), cost_(std::move(cost)) {}
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
    bool acceptsWritings() const override { return true; }

  private:
    std::string cost_;
};

class DeadCodePass : public RuntimeGraphRewritePass {
  public:
    explicit DeadCodePass(const camel::core::context::context_ptr_t &ctx, bool valuesOnly = false)
        : RuntimeGraphRewritePass(ctx), valuesOnly_(valuesOnly) {}
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;

  private:
    bool valuesOnly_;
};

class ControlReductionPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

/**
 * std::opt: call rewriting, fold, cse, algebra and dce until the program stops
 * changing. The passes feed each other (specializing a call makes a condition constant, folding
 * prunes the branch, which exposes the next call to specialize and inline), so a recursion of
 * static depth unrolls and a static program reduces to straight-line code. Rounds are bounded:
 * a recursion whose depth is not static would otherwise specialize one level deeper each round.
 * Growth is bounded as well (OptimizeRewriteConfig::growthBudget, one budget for all
 * rounds): a recursion with constant arguments that branches (`fib(15)`) would otherwise unroll
 * into its whole call tree; past the budget the remaining calls stay calls.
 */
struct OptimizeRewriteConfig;

/**
 * The std::opt fixpoint on `program`, with `config` for its call-rewrite rounds. `program` may
 * be detached from the running program (a function being exported); then the program is left as
 * it is. Stops after `maxRounds` rounds (each unrolls about one more level of a recursion of
 * static depth). Returns the rewritten program.
 */
const camel::runtime::Program *simplifyGraph(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    const OptimizeRewriteConfig &config, std::ostream &os, size_t maxRounds = 16);
