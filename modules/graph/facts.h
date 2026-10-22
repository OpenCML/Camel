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

namespace camel::core::type {
class StructType;
}

namespace camel::graphmod {

/// The type of the value `effects` gives: a structural type (its fields are `external`,
/// `writes_variables`, `may_fault` and `may_not_terminate`). The module exports it under this
/// name, because the declaration of `effects` in stdlib/graph.cml names its result.
camel::core::type::StructType *effectsType();

/// The operators that read the facts the core keeps of a program (camel/runtime/core_facts.h):
/// their kernels; the declarations are in stdlib/graph.cml. A fact is computed when it is asked
/// for and kept for as long as the graphs it was computed from do not change; every operator is
/// pure.
const std::unordered_map<std::string, operator_t> &factKernels();

} // namespace camel::graphmod
