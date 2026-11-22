// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/operator.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace camel::graphmod {

/// The operators of a transaction: `open` one on a function, edit it, `commit` it into the
/// changed function or `abort` it (design principles 9.1). Their kernels; every signature is
/// declared in stdlib/graph.cml. None but `graph_of` is pure: a transaction is a state that edits
/// change.
const std::unordered_map<std::string, operator_t> &txnKernels();

} // namespace camel::graphmod
