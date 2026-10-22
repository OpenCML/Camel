// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/operator.h"

#include <string>
#include <unordered_map>
#include <vector>

/// The implementations of numeric.cpp, merged into the module by operators.cpp. The signatures
/// are the declarations of `stdlib/math.cml` (the shifts included: both integer types for the
/// count are written out; handover H202), not a second table here.
std::unordered_map<std::string, operator_t> getMathNumericOpsMap();
