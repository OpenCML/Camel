// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
/// Compiles the script without running it, so that breakpoints can be answered before the
/// first node runs; the next runScriptOnce runs this compilation. False when it has errors
/// (reported on standard output).
bool compileScript(const std::string &targetFile);
/// 执行一次脚本。内存扫描始终开启；断点按统一模型由当前配置决定，Run/Restart 时由父进程同步。
RunOutcome runScriptOnce(const std::string &targetFile);

} // namespace debugger
