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
 * Created: Feb. 22, 2026
 * Updated: Feb. 26, 2026
 * Supported by: National Key Research and Development Program of China
 */
#pragma once

#include <string>

namespace debugger {

enum class RunOutcome {
    Completed,
    Terminated,
    Failed,
    RestartRequested,
};

void clearRunState();
RunOutcome
runScriptOnce(const std::string &targetFile, bool enableMemoryMonitor, bool enableAllocStep);

} // namespace debugger
