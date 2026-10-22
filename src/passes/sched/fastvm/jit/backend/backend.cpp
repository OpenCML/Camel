// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "backend.h"
#include "../jit_config.h"
#include "fallback.h"

#if JIT_TARGET_X64
#include "x64/x64_backend.h"
#endif

namespace camel::jit {

std::unique_ptr<IJitBackend> createBackend() {
#if JIT_TARGET_X64
    return std::make_unique<X64Backend>();
#else
    return std::make_unique<FallbackBackend>();
#endif
}

} // namespace camel::jit
