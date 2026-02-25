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

/**
 * @file spawn.h
 * @brief 子进程创建与回收：Windows 下 CreateProcess + Job
 * Object，保证主进程退出时子进程被系统回收。
 *
 * 设计要点：
 * - 子进程通过环境变量 CAMEL_DB_WORKER_PORT 获知自己的 HTTP 端口，父进程不传 run
 * 参数（memoryMonitor/allocStep 等）经环境变量，改为 spawn 后通过 forwardPostToChild("/api/run",
 * body) 下发，便于动态修改。
 * - 子进程 stdout/stderr 重定向到管道，父进程 pipeReaderThread 读到后写入
 * Logger，使脚本输出出现在父进程侧（并可由 Web UI 展示）。
 */

#include <string>
#include <unordered_set>
#include <utility>

namespace debugger {

/// 启动一个 worker 子进程（camel-db --run-worker <path>），返回 (成功?, 子进程 HTTP 端口)。仅
/// Windows 实现。
std::pair<bool, int> spawnWorker(
    const std::string &path, bool memoryMonitor, bool allocStep,
    const std::unordered_set<std::string> &breakSpaces);

/// 终止所有已 spawn 的子进程；关闭 Job 后系统会回收 Job 内进程，避免孤儿进程。
void terminateAllWorkers();

/// 终止指定端口的单个 worker；用于“重启”时先杀再 spawn。句柄由等待线程在进程退出后关闭并 erase。
bool terminateWorker(int port);

} // namespace debugger
