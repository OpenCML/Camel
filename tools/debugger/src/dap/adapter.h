// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

/**
 * @file adapter.h
 * @brief `camel-db --dap`: the Debug Adapter Protocol on standard input and output
 *        (docs/technical/35_debugger.md, 7.3).
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
