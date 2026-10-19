/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 18, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Type-directed transfers preserve struct declarations and tuple nesting.
#pragma once
#include "camel/execute/executor.h"

namespace camel::tiles {
core::type::Type *transferTreeType(core::type::Type *type, bool upload);
slot_t transferTree(ArgsView &norm, bool upload);
} // namespace camel::tiles
