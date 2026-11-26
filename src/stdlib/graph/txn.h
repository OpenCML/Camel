// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/operator.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace camel::graphmod {

class Handle;

/// The operators of a draft: `open` one on a function, edit it, `commit` it into the changed
/// function or `abort` it (design principles 9.1). Their kernels; every signature is declared in
/// stdlib/graph.cml. None but `graph_of` is pure: a draft is a state that edits change.
const std::unordered_map<std::string, operator_t> &txnKernels();

/// The graph handles of the draft a handle names, each as the draft of that graph: the function's
/// own graph and every graph it calls, once each in the order of their ids. The session that holds
/// them is reached through the value `open` answered, so only this file can build them; `graphs`
/// (read.cpp) asks for them here.
std::vector<Handle *> draftGraphHandles(const Handle *draft, const char *site);

} // namespace camel::graphmod
