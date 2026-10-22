// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/operator.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace camel::core::type {
class StructType;
}

namespace camel::graphmod {

/// The type of the value `effects` gives: a structural type (its fields are `external`,
/// `writes_variables`, `may_fault` and `may_not_terminate`). The module exports it under this
/// name, because the declaration of `effects` in stdlib/graph.cml names its result.
camel::core::type::StructType *effectsType();

/// The operators that read the facts the core keeps of a program (camel/runtime/core_facts.h):
/// their kernels; the declarations are in stdlib/graph.cml. A fact is computed when it is asked
/// for and kept for as long as the graphs it was computed from do not change; every operator is
/// pure.
const std::unordered_map<std::string, operator_t> &factKernels();

} // namespace camel::graphmod
