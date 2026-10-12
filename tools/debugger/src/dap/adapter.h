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
 * Created: Oct. 12, 2026
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */
#pragma once

/**
 * @file adapter.h
 * @brief `camel-db --dap`: the Debug Adapter Protocol on standard input and output
 *        (docs/technical/34_debugger.md, 7.3).
 *
 * The adapter is a translator and holds no debugging logic: it starts one worker process
 * (`camel-db --run-worker`) for the program, forwards the worker's standard output and error
 * as `output` events, turns DAP requests into commands of the worker's debug core
 * (POST /api/debug/<command>) and the core's events into DAP events.
 */

#include <string>

namespace debugger::dap {

/// Serves one debug session on stdin/stdout and returns the exit code of the adapter process.
/// `exePath` is this executable, which is started again as the worker.
int runAdapter(const std::string &exePath);

} // namespace debugger::dap
