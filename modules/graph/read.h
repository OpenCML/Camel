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
#include <utility>
#include <vector>

namespace camel::graphmod {

/// Whether a constant of this type is one the graph module reads and writes: a number, a bool
/// or a string. These are the types whose slot is the value itself (for a string, a reference
/// to an object), so a transaction can make one without a type of its own.
bool constantType(const camel::core::type::Type *type);

/// The operators that read a Program: their groups (exported by the module) and kernels. Every
/// one is pure.
const std::vector<oper_group_ptr_t> &readGroups();
const std::unordered_map<std::string, operator_t> &readKernels();

} // namespace camel::graphmod
