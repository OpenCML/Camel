// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * TEST-ONLY: `test::resources` exercises the resource declarations of operators
 * (camel/core/resource_semantics.h, OperatorTable::declareResources) and their resolution in a
 * program (camel/runtime/resources.h). It can only be named when the environment variable
 * they are named by a test pipeline (fault.h). One line per check, `RESOURCES <check> <result>`:
 *
 *   - registration: one declaration per rule of check 5 that must be rejected (on operators of
 *     the protocol `semtest`, which no module has), one that is accepted, the minor version a
 *     declaration of resources adds to an existing record;
 *   - regions: the bounds of mapped regions of hand-made instances, and which overlap;
 *   - the program: for every graph reachable from the root (by name), every node that touches
 *     a resource (`RESOURCES node <graph> <node> <label>: <resources>`), the footprint of the
 *     graph (`RESOURCES footprint <graph> <footprint>`), and every pair of nodes that must keep
 *     their order (`RESOURCES order <graph> <first> <second> <why> ordered|unordered by
 *     data|control`).
 *
 * The program's graph is returned unchanged.
 */

#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kResourcesTestPass = "test::resources";

class ResourcesTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
