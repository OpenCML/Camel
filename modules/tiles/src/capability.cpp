/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Explicit target configuration is an error, never a capability miss.
#include "tiles/internal/capability.h"
#include <cstdlib>
namespace camel::tiles {
int configuredCudaOrdinal() {
    const char *value = std::getenv("CAMEL_TILES_DEVICE");
    if (!value)
        return 0;
    char *end           = nullptr;
    const long selected = std::strtol(value, &end, 10);
    if (!*value || *end || selected < 0 || selected > 65535)
        throw std::runtime_error("CAMEL_TILES_DEVICE must be a nonnegative device ordinal");
    return static_cast<int>(selected);
}
} // namespace camel::tiles
