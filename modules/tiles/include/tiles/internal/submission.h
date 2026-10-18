/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 18, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Backend-native pending results. These never cross a Camel operator boundary.
#pragma once
#include "tiles/device.h"

namespace camel::tiles {
struct SubmittedProgram {
    std::vector<std::shared_ptr<DeviceValue>> outputs;
    // Required even when outputs is empty: faults are an execution obligation.
    std::shared_ptr<DeviceCompletion> completion;
};
SubmittedProgram submitProgram(std::shared_ptr<const Program> program);
} // namespace camel::tiles
