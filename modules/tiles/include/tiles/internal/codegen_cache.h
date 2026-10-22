// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
