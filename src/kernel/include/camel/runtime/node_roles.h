// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Boundary roles of graph nodes, for annotated graph dumps.
 *
 * Some nodes mark a boundary a reader of the graph cares about: where a value
 * leaves the tensor world for a host scalar, where branch arms must agree on a
 * type, where the export checks a runtime's capabilities, where an effect
 * happens. Each role is a named classifier registered here by whoever knows
 * the rule (the runtime for effects and branch contracts, the tensor module
 * for scalar boundaries, the onnx module for export capabilities); dump passes
 * only ask the registry, so a new role needs no change to them.
 *
 * A classifier reads a `GraphView` (kernel/graph/graph_view.h), not a frozen graph: a role is a
 * reading of the graph, so it must be askable of both representations, and it asks the view's
 * questions -- the kind, the type, the inputs, the operator a node names, the regions it owns.
 */

#pragma once

#include "graph/graph_view.h"

#include <functional>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

namespace camel::runtime {

struct NodeRoleSpec {
    std::string name;  // stable identifier, e.g. "scalar-boundary"
    std::string title; // legend text
    std::string color; // Graphviz fill color
};

/// The role's detail for node `ref` of `graph` when the node has the role, else nullopt.
using NodeRoleClassifier = std::function<std::optional<std::string>(
    const camel::graph::GraphView &graph,
    camel::graph::NodeRef ref)>;

class NodeRoleRegistry {
  public:
    struct Match {
        NodeRoleSpec spec;
        std::string detail;
    };

    static NodeRoleRegistry &instance();

    /// Registers (or replaces, by name) a role.
    void add(NodeRoleSpec spec, NodeRoleClassifier classify);

    /// The roles node `ref` of `graph` has, in registration order.
    std::vector<Match>
    classify(const camel::graph::GraphView &graph, camel::graph::NodeRef ref) const;

  private:
    NodeRoleRegistry();

    struct Entry {
        NodeRoleSpec spec;
        NodeRoleClassifier classify;
    };
    mutable std::shared_mutex mutex_;
    std::vector<Entry> entries_;
};

} // namespace camel::runtime