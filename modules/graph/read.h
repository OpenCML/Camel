// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/operator.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace camel::graphmod {

class Handle;

/// Whether a constant of this type is one the graph module reads and writes: a number, a bool
/// or a string. These are the types whose slot is the value itself (for a string, a reference
/// to an object), so a transaction can make one without a type of its own.
bool constantType(const camel::core::type::Type *type);

/// The canonical number of the node a Node handle names: its position in the order `nodes` gives,
/// which the graph's structure decides (design principles 9.5). The walk is the one
/// `src/runtime/canonical.cpp` already has -- the block naming and `same_graph` use it -- solved
/// once per Program from the Program's root, so every graph of one program is numbered by one
/// answer. A node of a draft has no canonical number yet and answers the place it holds in the
/// draft.
uint32_t nodeCanonicalNumber(const Handle *handle);

/// The nodes of one graph in canonical order, as `nodes` gives them: the same walk, same numbering.
std::vector<uint32_t> canonicalNodeRefs(const Handle *handle);

/// The kernels of the operators that read a Program. Every one is pure and every signature is a
/// declaration of `stdlib/graph.cml` (`constant` per sample type).
const std::unordered_map<std::string, operator_t> &readKernels();

} // namespace camel::graphmod
