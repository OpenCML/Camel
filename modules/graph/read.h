// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/operator.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace camel::graphmod {

/// Whether a constant of this type is one the graph module reads and writes: a number, a bool
/// or a string. These are the types whose slot is the value itself (for a string, a reference
/// to an object), so a transaction can make one without a type of its own.
bool constantType(const camel::core::type::Type *type);

/// The kernels of the operators that read a Program. Every one is pure and every signature is a
/// declaration of `stdlib/graph.cml` (`constant` per sample type; handover H202).
const std::unordered_map<std::string, operator_t> &readKernels();

} // namespace camel::graphmod
