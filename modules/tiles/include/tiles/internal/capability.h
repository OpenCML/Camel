/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Capability misses are dispatch decisions; compiler/runtime faults are not.
#pragma once
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace camel::tiles {
class BackendUnavailable : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};
class UnsupportedComputation : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};
/// A device the run named with `--devices` that is not there. It is not a capability miss: the
/// run named the device, so it is RuntimeDiag::DeviceUnavailable naming it (with the ordinal the
/// report shows) and never a fallback to the host or to another device.
class DeviceUnavailable : public std::runtime_error {
  public:
    DeviceUnavailable(int ordinal, std::string why)
        : std::runtime_error(std::move(why)), ordinal_(ordinal) {}
    int ordinal() const noexcept { return ordinal_; }

  private:
    int ordinal_;
};
/// The CUDA ordinal the run selected; nullopt when the run asked for the host only
/// (`--devices cpu`). Without `--devices` it is device 0, which is preferred when there is one
/// and ignored (the host runs the program) when there is not.
std::optional<int> configuredCudaOrdinal();
} // namespace camel::tiles