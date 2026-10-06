/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Created: Oct. 06, 2026
 * Updated: Oct. 06, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once
#include "camel/core/operator.h"
#include "camel/core/type.h"
#include <string>
#include <unordered_map>

namespace ctx = camel::core::context;

// rendezvous(parties, timeout_ms): waits until `parties` calls are waiting at the same time
// and returns the size of the group they form, or returns after `timeout_ms` with the number
// of calls that were waiting together (at least 1, the caller). A result of `parties` or more
// therefore proves that many calls were in flight at once; it is how the test suite asserts
// that a scheduler runs independent calls concurrently, without timing anything.
slot_t __para_rendezvous__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);

std::unordered_map<std::string, operator_t> getParaOpsMap();
