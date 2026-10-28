// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Atomic, typed snapshots of ordinary Camel training-state trees.

#pragma once
#include "camel/core/operator.h"
namespace camel::ckptlib {
std::unordered_map<std::string, operator_t> checkpointKernels();
void declareCheckpointResources();
} // namespace camel::ckptlib
