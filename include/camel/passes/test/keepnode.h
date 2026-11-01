// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Test-only producer of `:op/keep` (core/operator_table.h, design principles 3.3): no source
 * syntax makes a pass remove a node whose only reader of an effect is that node, so the tests
 * build the shape at the graph level and observe what `replaceNode` does with it.
 *
 * The shape is the one `keepThroughValue` names (opt/generic/rewrite.cpp): a node that is about to
 * be replaced, reading a node that must keep running, which nothing else reads, and which is not a
 * statement of a sync block. `:op/div_i` is that node here: it may fault, it is not elementwise,
 * so it is not freely schedulable and dropping it would drop a fault.
 *
 *   test::keep_node   builds the shape in a graph the program already has and prints
 *                     KEEPNODE synthetic_keep=<n> real_calls=<m> real_keep=<n>
 *
 * The program itself is left as it was apart from that: the nodes added are the shape and the
 * `:op/keep` that replaces them, and the plan runs it afterwards to show the effect still runs.
 */

#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kKeepNodeTestPass = "test::keep_node";

class KeepNodeTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};