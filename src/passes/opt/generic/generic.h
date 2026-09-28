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
 *                   replaces them by static values;
 *   std::opt::cse   merges pure operator nodes with identical operator and
 *                   inputs;
 *   std::opt::dce   removes pure operator (and value-only) nodes whose results
 *                   are unused.
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
