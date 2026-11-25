// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Shared semantic labels and types for the DOT and JSON GIR exporters.

#pragma once
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"
#include "graph/graph_view.h"
#include <string>
#include <string_view>
namespace camel::passes::gir {
struct PortInfo {
    std::string name;
    std::string kind;
    size_t index = 0;
};
const camel::runtime::GCNode *
lookupRecord(camel::runtime::GCGraph *, camel::runtime::gc_node_ref_t);
camel::core::type::Type *
nodeStaticSlotType(camel::runtime::GCGraph *, const camel::runtime::GCNode &);
PortInfo portInfo(camel::runtime::GCGraph *, camel::runtime::gc_node_ref_t);
/// The declared signature of the graph, which the face answers (`funcType`).
std::string graphTooltip(const camel::graph::GraphView &);
/// A node's tooltip: the name of the graph it is in -- a fact the face answers (`name`), handed in
/// here rather than asked through the graph -- and the place its value sits at, which is the
/// graph's numbering (`dataIndex`) and not a reading this face has.
std::string recordTooltip(std::string_view graphName, const camel::runtime::GCNode &);
std::string recordLabel(
    const camel::runtime::Program &, camel::runtime::GCGraph *, camel::runtime::gc_node_ref_t,
    const camel::runtime::GCNode &);
std::string recordType(camel::runtime::GCGraph &, camel::runtime::gc_node_ref_t);
} // namespace camel::passes::gir
