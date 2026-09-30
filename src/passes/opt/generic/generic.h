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
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Generic, module-agnostic graph optimizations. They consult only the core
 * OperatorTraitsRegistry, never module knowledge:
 *
 *   std::opt::fold  evaluates pure operators whose inputs are all static and
 *                   replaces them by static values, and replaces an element
 *                   projected out of a tuple built in the same graph by the
 *                   value filled there;
 *   std::opt::cse   merges pure operator nodes with identical operator and
 *                   inputs;
 *   std::opt::dce   removes pure operator (and value-only) nodes whose results
 *                   are unused, and SYNC control joins.
 *
 * std::opt::simplify runs std::opt, fold and dce to a fixpoint.
 *
 * Together with std::opt (devirtualization, which also turns calls of closures
 * built in the caller into direct calls of a lambda-lifted graph, and
 * inlining), they collapse the pullback closures of autodiff into straight-line
 * code when the program is static.
 *
 * All three keep side-effect ordering intact: when a node on a control chain
 * (every call in `sync` code is on one) is replaced, all of its users inherit
 * its control predecessors, and nodes that anchor the graph (exit, output,
 * return, branch-arm heads and tails) are never rewritten.
 */

#pragma once

#include "camel/execute/pass/opt.h"

class ConstantFoldPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    camel::runtime::GCGraph *apply(camel::runtime::GCGraph *graph, std::ostream &os) override;
};

class CommonSubexpressionPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    camel::runtime::GCGraph *apply(camel::runtime::GCGraph *graph, std::ostream &os) override;
};

class DeadCodePass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    camel::runtime::GCGraph *apply(camel::runtime::GCGraph *graph, std::ostream &os) override;
};

/**
 * std::opt::simplify: std::opt, std::opt::fold and std::opt::dce repeated until the program stops
 * changing. The passes feed each other (specializing a call makes a condition constant, folding
 * prunes the branch, which exposes the next call to specialize and inline), so a recursion of
 * static depth unrolls and a static program reduces to straight-line code. Rounds are bounded:
 * a recursion whose depth is not static would otherwise specialize one level deeper each round.
 */
class SimplifyPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    camel::runtime::GCGraph *apply(camel::runtime::GCGraph *graph, std::ostream &os) override;
};
