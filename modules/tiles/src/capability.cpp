/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

// The device `--devices` selected. Explicit target configuration is a requirement, never a
// capability miss: only a selection that was not given may fall back to the host.
#include "tiles/internal/capability.h"
#include "camel/execute/devices.h"
namespace camel::tiles {
std::optional<int> configuredCudaOrdinal() {
    const auto &selection = camel::execute::selectedDevices();
    if (selection.given) {
        if (selection.cpu || selection.cuda.empty())
            return std::nullopt;
        return selection.cuda.front();
    }
    // No --devices: the run prefers device 0 and the host runs the program when there is none.
    return 0;
}
} // namespace camel::tiles