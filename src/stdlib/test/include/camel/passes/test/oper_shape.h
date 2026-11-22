// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * TEST-ONLY: `test::oper_shape` prints the shape every conversion, copy, construction and member
 * read of the program answers -- `[OPER_SHAPE] <form> shape=<shape>`, once per form -- where the
 * form is the operator a node calls (`oper=:op/fill`) or the kind a node was built as
 * (`kind=FILL`). The two forms of one operation must answer the same shape, because the question is
 * the operator's (`camel/execute/shaped_operators.h`, `shapeOfOperation`), not the
 * representation's.
 *
 * The program is returned unchanged.
 */

#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kOperShapeTestPass = "test::oper_shape";

class OperShapeTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
