/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 19, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Explicit scalar types select concrete binary-decoding operators at compile time.
#pragma once
#include "op_def.h"
namespace camel::tensor::ops {
std::vector<OpDef> binaryOps();
oper_group_ptr_t fromBytesOperatorGroup();
} // namespace camel::tensor::ops
