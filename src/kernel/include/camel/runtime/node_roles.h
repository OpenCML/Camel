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
 * A role is a reading of the graph, so it is read through the `GraphView`
 * (kernel/graph/graph_view.h) and asks the view's questions -- the kind, the type, the inputs, the
 * operator a node names, the regions it owns. A role that speaks about a fact does not work it out:
 * it asks the fact layer (`kernel/facts`, camel/runtime/core_facts.h) for the answer about the node
 * it is given, because a fact is computed by one set of rules and read everywhere else
 * (architecture 2.3). That is what the site below carries beside the view: the graph as a value, the
 * program that holds it and the context's fact store, which is what a fact is anchored on and
 * answered by.
 */

#pragma once

#include "graph/graph_view.h"

#include <functional>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

namespace camel::runtime {

class Program;
class GCGraph;
namespace fact {
class FactStore;
} // namespace fact

struct NodeRoleSpec {
    std::string name;  // stable identifier, e.g. "scalar-boundary"
    std::string title; // legend text
    std::string color; // Graphviz fill color
};

/// What a role is asked about: the graph, read through the view, plus -- for a role whose answer is
/// a fact -- the graph as a value, the program that holds it, and the fact store of the context,
/// because a fact is anchored on the graph and answered by the store (`camel/runtime/core_facts.h`).
/// The graph is the runtime's own (`GCGraph`) and not the identity the view hides: a fact's anchor
/// names it, so a role that reads a fact must be handed the value the facts were stored against.
/// A role that reads no fact leaves the three null, and one that does must be given all three: a
/// graph no store holds (a draft) has no fact to read, and such a role answers nothing there rather
/// than working the answer out a second time.
struct NodeRoleSite {
    const camel::graph::GraphView &view;
    const camel::runtime::GCGraph *graph   = nullptr;
    const camel::runtime::Program *program = nullptr;
    camel::runtime::fact::FactStore *facts = nullptr;
};

/// The role's detail for node `ref` of the site's graph when the node has the role, else nullopt.
using NodeRoleClassifier = std::function<std::optional<std::string>(
    const NodeRoleSite &site,
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

    /// The roles node `ref` of `site`'s graph has, in registration order.
    std::vector<Match> classify(const NodeRoleSite &site, camel::graph::NodeRef ref) const;

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