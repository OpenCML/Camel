/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Typed function wrappers for explicit GPU dispatch decorators.
#pragma once
#include "camel/core/operator.h"
class Function;
namespace camel::tiles {
Function *gpuFunction(Function *source, operator_t run);
}
