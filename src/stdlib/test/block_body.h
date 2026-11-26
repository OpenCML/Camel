// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * TEST-ONLY: `test::block_body` compiles a body written as text into a program of its own, the way
 * a block's body is to be compiled (the block is an operator whose implementation is a program,
 * and no pass walks into it), and then runs it through the operator the block would be.
 *
 * The readings this pass prints are the ones the arrangement owes:
 *
 *   * the body compiled at all, and into a program with its own graphs (`BLOCK_BODY built ...`);
 *   * the entry program's graphs before and after that build are the same set
 *     (`BLOCK_BODY entry ...`), which is what "the body is not part of this program" means;
 *   * one line per graph of the body's program (`BLOCK_BODY graph ...`), so a reader can see the
 *     body's function graph is there and is not in the entry program;
 *   * two blocks, one kernel, and the value each one answers (`BLOCK_CALL ...`): the URI reaches
 *     the kernel through the executor manager (`block` protocol), the node's attributes say which
 *     program of the context's store to run, and the second call runs after the first is over.
 *
 * The body's program is pinned to the context before this pass returns: the module that built it
 * lives as long as this call, and the graphs it made would go with it. Nothing is inserted into
 * the program this pass was handed, and the program comes back unchanged.
 */

#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kBlockBodyTestPass = "test::block_body";

class BlockBodyTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;

  private:
    /// Compiles `text` as a module named `name` and answers the program of its `function`, pinned
    /// to the context; nullptr when it does not parse or does not compile.
    camel::runtime::Program *compileBody(
        const std::string &name, const char *text, const char *function, const std::string &what,
        std::ostream &os);
};