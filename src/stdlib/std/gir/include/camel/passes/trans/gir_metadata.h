// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Shared semantic labels and types for the DOT and JSON GIR exporters.

#pragma once
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"
#include <string>
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
std::string graphTooltip(camel::runtime::GCGraph *);
std::string recordTooltip(camel::runtime::GCGraph *, const camel::runtime::GCNode &);
std::string recordLabel(
    const camel::runtime::Program &, camel::runtime::GCGraph *, camel::runtime::gc_node_ref_t,
    const camel::runtime::GCNode &);
std::string recordType(const camel::runtime::GCNode &);
} // namespace camel::passes::gir
