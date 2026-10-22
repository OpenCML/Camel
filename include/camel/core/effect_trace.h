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
 * Author: Camel Contributors
 * Created: Oct. 04, 2026
 * Updated: Oct. 13, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Effect trace: an ordered log of the effectful operators a process executed.
 *
 * When a file is named with `std::profile(effect_trace="<file>")`, every operator that is not
 * registered as pure in OperatorTable appends one JSON line when it is called:
 *
 *   {"seq":0,"uri":":io/println","stage":"run",
 *    "origin":{"file":"a.cml","line":12,"col":5},"payload_sha1":"...",
 *    "payload":"string=hello|||"}
 *
 * - `seq` counts effect calls of the process, in call order.
 * - `stage` is "macro" while `std::macro` evaluates a call ahead of time, "run" otherwise.
 * - `origin` is the source position (1-based) of the operator node, or null when the executing
 *   back end does not report it (JIT-compiled code) or the node was synthesized by a pass.
 * - `payload_sha1` hashes the printed argument values, taken before the operator runs.
 * - `payload` is the hashed text itself: each argument as `type=value` followed by `|`, the
 *   normal arguments first, then `||`, then the `with` arguments (the hashed bytes use 0x1f and
 *   0x1e for the two separators). It is cut to 2048 bytes, in which case the line also carries
 *   `"payload_truncated":true`; the hash always covers the full text.
 *
 * The hook sits where operator URIs are resolved to functions (ExecutorManager::find): an
 * effectful operator is handed out behind a trampoline, so every back end, including
 * JIT-compiled code, is covered without a per-back-end call hook. Interpreters additionally
 * publish the source origin of the operator they are about to call. With no file set nothing is
 * wrapped and the only cost is one cached flag test per operator resolution.
 */

#pragma once

#include "camel/core/operator.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace camel::core::effect_trace {

/// Names the file the trace is appended to (`std::profile(effect_trace="<file>")`), before the
/// program runs. An empty path turns tracing off.
void setTraceFile(std::string path);

/// True when a trace file is set.
bool enabled();

/// The function to call for operator `uri`: `op` itself when tracing is off or the operator is
/// pure, otherwise a trampoline that logs the call and forwards to `op`.
operator_t wrap(std::string_view uri, operator_t op);

/// The operator behind a trampoline returned by wrap(); `op` itself for anything else.
operator_t unwrap(operator_t op);

/// Publishes the source origin (0 = unknown) of the operator node the calling thread is about
/// to execute. Interpreters call it right before an operator call when enabled().
void setSite(uint64_t origin);

/// Marks the calling thread as evaluating a macro call ahead of time for its lifetime.
class MacroStageScope {
  public:
    MacroStageScope();
    ~MacroStageScope();
    MacroStageScope(const MacroStageScope &)            = delete;
    MacroStageScope &operator=(const MacroStageScope &) = delete;
};

} // namespace camel::core::effect_trace
