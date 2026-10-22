// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/operator.h"

#include <string>
#include <unordered_map>
#include <vector>

/// The implementations of sequence.cpp, merged into the module by operators.cpp. The operators
/// are declared in `stdlib/algo.cml` (`argsort`, `flatten`, `unique`; handover H202).
std::unordered_map<std::string, operator_t> getAlgoSequenceOpsMap();
