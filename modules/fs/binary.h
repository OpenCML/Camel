/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 18, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Binary file kernels and crash-safe replacement, separate from text IO.
#pragma once
#include "camel/execute/executor.h"
slot_t fsReadBytes(ArgsView &, ArgsView &, camel::core::context::Context &);
slot_t fsWriteBytes(ArgsView &, ArgsView &, camel::core::context::Context &);
slot_t fsReplaceBytes(ArgsView &, ArgsView &, camel::core::context::Context &);
