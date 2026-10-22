/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/operator.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace camel::graphmod {

/// The operators of a transaction: `open` one on a Program, edit it, `commit` it into a new
/// Program or `abort` it. Their kernels, and the groups whose signature C++ computes
/// (`open_function`, `to_function`, `program_of`, `graph_of`, `add_constant`; the rest are
/// declared in stdlib/graph.cml). None is pure: a transaction is a state that edits change.
const std::vector<oper_group_ptr_t> &txnGroups();
const std::unordered_map<std::string, operator_t> &txnKernels();

} // namespace camel::graphmod
