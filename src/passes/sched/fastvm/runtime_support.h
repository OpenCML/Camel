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
 * Author: OpenAI Codex
 * Created: Apr. 11, 2026
 * Updated: Apr. 11, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Shared FastVM interpreter helpers.
 *
 * This file holds bytecode-local execution semantics that are duplicated by
 * multiple interpreter backends. These helpers are intentionally scoped to the
 * FastVM bytecode layer rather than the runtime GCGraph layer.
 */

#pragma once

#include "bytecode.h"
#include "camel/core/context/frame.h"

namespace camel::passes::sched::fastvm {

size_t selectBranchArm(const Bytecode &bc, camel::core::context::Frame *frame);

} // namespace camel::passes::sched::fastvm
