// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Explicit scalar types select concrete binary-decoding operators at compile time.

#pragma once
#include "op_def.h"
namespace camel::tensor::ops {
std::vector<OpDef> binaryOps();
oper_group_ptr_t fromBytesOperatorGroup();
} // namespace camel::tensor::ops
