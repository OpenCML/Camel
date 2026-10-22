// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * A value exported by a module written in Camel, as the module's export table holds it
 * (design principles 10.1; docs/design/graph-model/README.cn.md 3.9.1). The value is a node of
 * the program's root graph; the export is made when the module is declared, before the node
 * exists, so the entity names the value and the graph builder makes the node when a module
 * first uses it. Owned by the program's graph builder, which outlives the export tables.
 */

#pragma once

#include "camel/runtime/node_ref.h"

#include <string>



namespace camel::compile::gir {

struct ExportedValue {
    /// The module's name for the value (with its namespaces).
    std::string name;
    /// Its node on the root graph, once built; null before, and when its initializer failed.
    camel::runtime::DraftNodeRef node;
    /// The top-level declaration that binds it (opaque outside the graph builder).
    void *declaration = nullptr;
};

} // namespace camel::compile::gir
