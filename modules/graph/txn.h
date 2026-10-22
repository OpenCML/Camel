// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/operator.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace camel::graphmod {

/// The operators of a transaction: `open` one on a Program, edit it, `commit` it into a new
/// Program or `abort` it. Their kernels, and the groups whose signature C++ computes
/// (`open_function`, `to_function`, `program_of`, `graph_of`, `add_constant`; the rest are
/// declared in stdlib/graph.cml). None is pure: a transaction is a state that edits change.
const std::vector<oper_group_ptr_t> &txnGroups();
const std::unordered_map<std::string, operator_t> &txnKernels();

} // namespace camel::graphmod
