// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The Camel enum `NodeKind` and its correspondence to GCNodeKind. The enum lists the kinds of
 * node a program can show, in the order of the table in nodekind.cpp. Bind and Dref exist only
 * while a graph is built, and Dead marks the place of a removed node: none of them is a node of
 * a graph a program reads, so none has a variant.
 */

#pragma once

#include "camel/core/type/enum.h"
#include "camel/runtime/graph.h"

#include <optional>

namespace camel::graphmod {

/// The enum type `NodeKind`.
camel::core::type::EnumType *nodeKindType();

/// The position of `kind` in `NodeKind`; nothing for a kind a program does not show. The
/// function names every GCNodeKind in a switch, so a kind added to the graph model stops the
/// build here until it is placed in the enum or left out on purpose.
std::optional<uint32_t> nodeKindTag(camel::runtime::GCNodeKind kind);

/// The name of the variant at `tag`.
const char *nodeKindName(uint32_t tag);

} // namespace camel::graphmod
