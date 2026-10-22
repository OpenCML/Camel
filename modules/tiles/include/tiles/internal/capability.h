// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Capability misses are dispatch decisions; compiler/runtime faults are not.

#pragma once
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace camel::tiles {
class BackendUnavailable : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};
class UnsupportedComputation : public std::runtime_error {
  public:
    /// What the compiler refused. `Operator` is a lowering the back end does not have at any
    /// shape; `Shape` is a use whose types, shapes or element types it cannot take even though
    /// the operator itself has a lowering. A refusal that reaches the user says which one.
    enum class Reason { Operator, Shape };
    /// `oper` is the operator the compiler stopped at, when it knows one.
    explicit UnsupportedComputation(
        const std::string &what, std::string oper = {}, Reason reason = Reason::Operator)
        : std::runtime_error(what), oper_(std::move(oper)), reason_(reason) {}
    const std::string &oper() const noexcept { return oper_; }
    Reason reason() const noexcept { return reason_; }

  private:
    std::string oper_;
    Reason reason_;
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

/// What CUDA devices this process can see, asked before any graph runs. `count` is the number of
/// visible devices; `missing` is the subset of `named` that `cuDeviceGet` does not answer;
/// `reason` explains a count of zero (no device, no driver, or a build without CUDA support).
/// This is a device-count question, not the one probe that builds the back end: it opens no
/// context and compiles nothing.
struct CudaDeviceAvailability {
    int count = 0;
    std::vector<int> missing;
    std::string reason;
};
CudaDeviceAvailability cudaDeviceAvailability(const std::vector<int> &named);
/// Told when the process takes its one probe of the CUDA back end, before the driver is asked.
/// The device sources are also built into the module's own check executables, which link neither
/// libcamel nor its headers, so the probe cannot write a line through the core's logger itself;
/// the module registers a callback where it is loaded (module.cpp). Null when the device code is
/// used without the module, which records nothing.
inline void (*deviceProbeStarted)() = nullptr;
} // namespace camel::tiles
