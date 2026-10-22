// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "backend/backend.h"
#include "jit_config.h"

#if JIT_TARGET_X64
#include "backend/x64/x64_backend.h"
#endif
