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
 * Updated: Feb. 28, 2026
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
/// 执行一次脚本。内存扫描始终开启；断点按统一模型由当前配置决定，Run/Restart 时由父进程同步。
RunOutcome runScriptOnce(const std::string &targetFile);

} // namespace debugger
