// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The id an operator has in the operator table (camel/core/operator_table.h), and the id that
// names no operator. A file that only names one of these includes this header, not the table's
// definition.

#pragma once

#include <cstdint>

namespace camel::core {

/// Index of an operator in the OperatorTable.
using OperId = uint32_t;

/// "No operator": the value of a zero-initialized OPER payload. Its URI is the empty string and
/// it is never declared, so every query treats it conservatively.
inline constexpr OperId kNoOper = 0;

} // namespace camel::core