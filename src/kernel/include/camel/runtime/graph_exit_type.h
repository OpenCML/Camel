// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/source/ids.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace camel::runtime {

/// A frozen graph whose exit hands on a value that is not of the type the graph's own signature
/// declares: `func target(n: float): float` handing on a `float[]`, or an `int` function handing on
/// a `float`.
///
/// It is the program's error and not the compiler's -- a `return` is a position with an expected
/// type (design principles 5.1), and a graph whose exit value is of another type was written that
/// way by the program, by a rewrite rule whose right side is not of the type of its left side -- so
/// it travels as its own exception and is reported as its own diagnostic
/// (`RuntimeDiag::ExitTypeMismatch`) rather than as `PassInternalError`, which is what a pass that
/// broke the graph it was given answers. The facts the report needs travel with it: the graph, the
/// value (its node and, when it has one, its source name), both types, and the value's source
/// origin when the node records one -- a node a rewrite built records none, which is why this
/// report can stand without a position.
///
/// Thrown by `validateRuntimeGraphEdges` (src/runtime/graph_edges.cpp#, `EdgeChecker`), where the
/// graph is frozen, so no back end is ever handed such a graph; caught by the pass boundary
/// (src/execute/pass.cpp#, `reportPassFailure`).
class GraphExitTypeMismatch : public std::runtime_error {
  public:
    GraphExitTypeMismatch(
        std::string graphName, std::string whereInGraph, std::string valueType,
        std::string declaredType, camel::source::origin_id_t origin)
        : std::runtime_error(
              "the value the graph '" + graphName + "' hands on has type '" + valueType +
              "', and the graph declares '" + declaredType + "'"),
          graphName_(std::move(graphName)), whereInGraph_(std::move(whereInGraph)),
          valueType_(std::move(valueType)), declaredType_(std::move(declaredType)), origin_(origin) {}

    /// The graph's name, which is the function the writer declared.
    const std::string &graphName() const { return graphName_; }
    /// The value inside the graph: `node 3` or `node 3, the value 's'`.
    const std::string &whereInGraph() const { return whereInGraph_; }
    /// The type of the value the graph hands on.
    const std::string &valueType() const { return valueType_; }
    /// The type the graph's signature declares.
    const std::string &declaredType() const { return declaredType_; }
    /// The value's source origin, or `camel::source::kInvalidOriginId` when the node records none.
    camel::source::origin_id_t origin() const { return origin_; }

  private:
    std::string graphName_;
    std::string whereInGraph_;
    std::string valueType_;
    std::string declaredType_;
    camel::source::origin_id_t origin_;
};

} // namespace camel::runtime