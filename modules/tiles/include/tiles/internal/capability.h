/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Capability misses are dispatch decisions; compiler/runtime faults are not.
#pragma once
#include <stdexcept>

namespace camel::tiles {
class BackendUnavailable : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};
class UnsupportedComputation : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};
// Validate explicit configuration even when another capability is unavailable.
int configuredCudaOrdinal();
} // namespace camel::tiles
