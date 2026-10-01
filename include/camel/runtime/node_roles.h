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
 * Author: Camel Contributors
 * Created: Sep. 30, 2026
 * Updated: Sep. 30, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
 */

#pragma once

#include "camel/runtime/graph.h"

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
using NodeRoleClassifier =
    std::function<std::optional<std::string>(const GCGraph &graph, gc_node_ref_t ref)>;

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
    std::vector<Match> classify(const GCGraph &graph, gc_node_ref_t ref) const;

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
