// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Backend-native pending results. These never cross a Camel operator boundary.

#pragma once
#include "tiles/device.h"

#include <memory>
#include <vector>

namespace camel::tiles {
struct SubmittedProgram {
    std::vector<std::shared_ptr<DeviceValue>> outputs;
    // Required even when outputs is empty: faults are an execution obligation.
    std::shared_ptr<DeviceCompletion> completion;
};
SubmittedProgram submitProgram(std::shared_ptr<const Program> program);
} // namespace camel::tiles
