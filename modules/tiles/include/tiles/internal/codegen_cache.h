/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Process-local source templates. No input data, GC pointers or device owners.
#pragma once
#include "tiles/program.h"

namespace camel::tiles::kernel {
void emitCUDA(Program &program);
struct GenerationCacheStats {
    size_t hits, misses, entries, retainedBytes;
};
GenerationCacheStats generationCacheStats();
} // namespace camel::tiles::kernel
