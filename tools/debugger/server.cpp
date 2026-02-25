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
 * Created: Feb. 22, 2026
 * Updated: Feb. 26, 2026
 * Supported by: National Key Research and Development Program of China
 */

/**
 * @file server.cpp
 * @brief DebuggerServer 实现：HTTP 路由、按 target 转发、LogSink、内存扫描与分配断点等待。
 *
 * 设计要点：
 * - 查询类 API（state/snapshot/step-paused 等）先 tryForwardToTarget：有 target
 * 则转发到子进程，否则在父进程用 requireNoTasksForLocal
 * 判断是否允许用本地数据应答，避免把父进程数据误当任务数据返回。
 * - 写类 API（run/continue/settings 等）同样按 target 转发或本地
 * dispatch，保证“当前选中的任务”收到操作。
 */

#include "server.h"
#include "camel/core/debug_breakpoint.h"
#include "camel/core/mm/profiler.h"
#include "camel/utils/log.h"
#include "command/dispatcher.h"
#include "nlohmann/json.hpp"
#include "state.h"

#include <chrono>
#include <cstdlib>
#include <deque>
#include <httplib.h>
#include <iostream>
#include <mutex>
#include <sstream>
#include <streambuf>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace debugger {

using json = nlohmann::json;

namespace {

// -----------------------------------------------------------------------------
// LogSink：将 Logger 输出按行缓冲，供 GET /api/log 按 offset 分页拉取，避免一次性加载全部。
// 使用 streambuf + 队列，写入时加锁，保证多线程下 Logger 写入与 HTTP 读取安全。
// -----------------------------------------------------------------------------
constexpr size_t kLogSinkMaxLines = 2000;

class LogSinkBuf : public std::streambuf {
  public:
    explicit LogSinkBuf(size_t bufSize = 256) : buf_(bufSize + 1) {
        char *base = buf_.data();
        setp(base, base + bufSize);
    }

  protected:
    int overflow(int c) override {
        sync();
        if (c != std::char_traits<char>::eof()) {
            *pptr() = static_cast<char>(c);
            pbump(1);
        }
        return c;
    }
    int sync() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pptr() > pbase()) {
            queue_.emplace_back(pbase(), pptr());
            if (queue_.size() > kLogSinkMaxLines)
                queue_.pop_front();
            setp(buf_.data(), buf_.data() + buf_.size() - 1);
        }
        return 0;
    }

  public:
    void getLines(size_t offset, std::vector<std::string> &out, size_t &nextOffset) {
        std::lock_guard<std::mutex> lock(mutex_);
        out.clear();
        if (offset >= queue_.size()) {
            nextOffset = queue_.size();
            return;
        }
        for (size_t i = offset; i < queue_.size(); ++i)
            out.push_back(queue_[i]);
        nextOffset = queue_.size();
    }

  private:
    std::mutex mutex_;
    std::deque<std::string> queue_;
    std::vector<char> buf_;
};

} // namespace

class DebuggerServer::LogSink {
  public:
    LogSink() : buf_(), stream_(&buf_) {}
    std::ostream &getStream() { return stream_; }
    void getLines(size_t offset, std::vector<std::string> &out, size_t &nextOffset) {
        buf_.getLines(offset, out, nextOffset);
    }

  private:
    LogSinkBuf buf_;
    std::ostream stream_;
};

DebuggerServer::DebuggerServer() : logSink_(std::make_unique<LogSink>()) {}

DebuggerServer::~DebuggerServer() { stop(); }

void DebuggerServer::setQueryCallbacks(GetStateFn getState, GetGirDotFn getGirDot) {
    getState_  = std::move(getState);
    getGirDot_ = std::move(getGirDot);
}

void DebuggerServer::setWorkerRunHandler(WorkerRunHandlerFn fn) {
    workerRunHandler_ = std::move(fn);
}

void DebuggerServer::setChildPort(int port) { childPort_.store(port); }

void DebuggerServer::clearChildPort() { childPort_.store(0); }

void DebuggerServer::start(int port, bool silent) {
    if (running_)
        return;
    port_       = port;
    running_    = true;
    httpStop_   = false;
    scanStop_   = true;
    httpThread_ = std::thread(&DebuggerServer::httpServerLoop, this);

    if (!silent) {
        std::string msg = "API http://127.0.0.1:" + std::to_string(port_) +
                          " | Web UI: python tools/debugger/serve_ui.py";
        std::cout << msg << std::endl;
        Logger::WriteToAllStreams(msg);
    }
    if (logSink_ && logStreamHandle_ == 0)
        logStreamHandle_ = Logger::AddOutputStream(&logSink_->getStream());
}

void DebuggerServer::startMemoryScan() {
    assert(isWorkerProcess() && "startMemoryScan() called in non-worker process");
    if (memoryScanRunning_)
        return;
    if (!running_)
        return;
    scanStop_          = false;
    memoryScanRunning_ = true;
    scanThread_        = std::thread(&DebuggerServer::scanThreadLoop, this);
}

void DebuggerServer::stopMemoryScan() {
    if (!memoryScanRunning_)
        return;
    scanStop_          = true;
    memoryScanRunning_ = false;
    continueRequested_ = true;
    continueCond_.notify_all();
    if (scanThread_.joinable())
        scanThread_.join();
}

void DebuggerServer::pauseAndWaitForContinue(const void *ptr, size_t size, const char *space) {
    assert(isWorkerProcess() && "pauseAndWaitForContinue() called in non-worker process");
    if (!allocStepEnabled_ || !running_)
        return;
    // 若配置了 breakSpaces 过滤，仅在被列出的 space 上暂停，避免无关分配打断用户。
    {
        std::lock_guard<std::mutex> lock(allocBreakSpacesMutex_);
        if (!allocBreakSpaces_.empty()) {
            std::string s(space ? space : "");
            if (allocBreakSpaces_.count(s) == 0)
                return;
        }
    }
    try {
        std::ostringstream oss;
        const char *phase = (ptr == nullptr) ? "before" : "after";
        oss << "{\"phase\":\"" << phase << "\",\"size\":" << size << ",\"space\":\""
            << (space ? space : "") << "\"";
        if (ptr != nullptr)
            oss << ",\"ptr\":" << reinterpret_cast<uintptr_t>(ptr);
        oss << "}";
        {
            std::lock_guard<std::mutex> lock(jsonMutex_);
            lastAllocJson_ = oss.str();
        }
        std::string json = mm::profiler::snapshotToJson();
        {
            std::lock_guard<std::mutex> lock(jsonMutex_);
            latestJson_ = std::move(json);
        }
    } catch (const std::exception &e) {
        std::cerr << "[debugger] Snapshot exception: " << e.what() << std::endl;
    }
    paused_.store(true);
    continueRequested_.store(false);
    {
        std::unique_lock<std::mutex> lock(continueMutex_);
        continueCond_.wait(lock, [this] {
            return continueRequested_.load() || restartRequested_.load() ||
                   terminateRequested_.load() || !running_ || !allocStepEnabled_;
        });
    }
    paused_.store(false);
    if (restartRequested_.exchange(false))
        throw RestartRequestedException();
    if (terminateRequested_.exchange(false))
        throw TerminateRequestedException();
}

void DebuggerServer::requestContinue() {
    continueRequested_.store(true);
    continueCond_.notify_one();
}

void DebuggerServer::requestRestart() {
    paused_.store(false);
    restartRequested_.store(true);
    continueCond_.notify_one();
}

void DebuggerServer::requestTerminate() {
    paused_.store(false);
    terminateRequested_.store(true);
    continueCond_.notify_one();
}

void DebuggerServer::clearLastRunError() {
    std::lock_guard<std::mutex> lock(lastRunErrorMutex_);
    lastRunError_.clear();
    lastRunErrorExpression_.clear();
    lastRunErrorFile_.clear();
    lastRunErrorLine_ = 0;
}

void DebuggerServer::setAllocBreakSpaces(std::unordered_set<std::string> spaces) {
    std::lock_guard<std::mutex> lock(allocBreakSpacesMutex_);
    allocBreakSpaces_ = std::move(spaces);
}

bool DebuggerServer::forwardPostToChild(const std::string &apiPath, const std::string &body) {
    int port = childPort_.load();
    if (port <= 0)
        return false;
    httplib::Client cli("127.0.0.1", port);
    cli.set_connection_timeout(0, 300000);
    auto r = cli.Post(apiPath, body, "application/json");
    if (!r) {
        clearChildPort();
        return false;
    }
    return true;
}

void DebuggerServer::stop() {
    if (!running_)
        return;
    running_            = false;
    scanStop_           = true;
    httpStop_           = true;
    memoryScanRunning_  = false;
    allocStepEnabled_   = false;
    continueRequested_  = true;
    terminateRequested_ = false;
    continueCond_.notify_all();
    if (logStreamHandle_ != 0) {
        Logger::RemoveOutputStream(logStreamHandle_);
        logStreamHandle_ = 0;
    }

    auto *svr = reinterpret_cast<httplib::Server *>(serverPtr_.load());
    if (svr)
        svr->stop();

    if (scanThread_.joinable())
        scanThread_.join();
    if (httpThread_.joinable())
        httpThread_.join();
}

/// 后台周期拉取 GC 快照并缓存到 latestJson_，供 snapshot 类 API 使用；子进程内由本进程的 profiler
/// 提供数据。
void DebuggerServer::scanThreadLoop() {
    while (!scanStop_) {
        try {
            std::string json = mm::profiler::snapshotToJson();
            {
                std::lock_guard<std::mutex> lock(jsonMutex_);
                latestJson_ = std::move(json);
            }
        } catch (const std::exception &e) {
            std::cerr << "[debugger] Scan exception: " << e.what() << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

namespace {
/// 转发请求时需保留原始 path + query，子进程收到的 URL 与前端一致，便于子进程路由。
std::string
buildPathWithQuery(const std::string &path, const std::multimap<std::string, std::string> &params) {
    if (params.empty())
        return path;
    std::string q = path + "?";
    bool first    = true;
    for (const auto &p : params) {
        if (!first)
            q += "&";
        first = false;
        q += p.first + "=" + p.second;
    }
    return q;
}

/// 写操作统一经 Command 派发，保证与 REPL 行为一致并可回显；异常时返回 JSON error 而非断开连接。
void dispatchAndRespond(
    const std::string &cmdName, const httplib::Request &req, httplib::Response &res) {
    try {
        auto result = getDispatcher().dispatch(cmdName, req.body.empty() ? "{}" : req.body);
        if (!result.body.empty()) {
            res.set_content(result.body, "application/json");
        } else {
            json j;
            j["ok"] = result.success;
            if (!result.success)
                j["error"] = result.message;
            res.set_content(j.dump(), "application/json");
        }
    } catch (const std::exception &e) {
        json j;
        j["ok"]    = false;
        j["error"] = e.what();
        res.set_content(j.dump(), "application/json");
    }
}
} // namespace

void DebuggerServer::httpServerLoop() {
    httplib::Server svr;
    serverPtr_.store(&svr); // stop() 时通过 serverPtr_ 调用 svr->stop() 结束 accept 循环

    // 从请求中取出 target（任务 id）：GET 用 query，POST 用 body JSON，与前端约定一致。
    auto extractTarget = [](const httplib::Request &req) -> std::string {
        if (req.method == "GET")
            return req.get_param_value("target");
        if (!req.body.empty()) {
            try {
                json j = json::parse(req.body, nullptr, false);
                return j.value("target", "");
            } catch (...) {
            }
        }
        return "";
    };

    // 将当前请求原样转发到指定端口的子进程；失败时从任务表移除该端口，避免反复请求已退出的子进程。
    auto forwardToPort =
        [this](int port, const httplib::Request &req, httplib::Response &res) -> bool {
        if (port <= 0)
            return false;
        std::string path = buildPathWithQuery(req.path, req.params);
        httplib::Client cli("127.0.0.1", port);
        cli.set_connection_timeout(0, 300000);
        if (req.method == "GET") {
            auto r = cli.Get(path);
            if (!r) {
                setTaskState(port, "exited");
                if (getChildPort() == port)
                    clearChildPort();
                return false;
            }
            res.status = r->status;
            res.set_content(r->body, "application/json");
            return true;
        }
        if (req.method == "POST") {
            auto r = cli.Post(path, req.body, "application/json");
            if (!r) {
                setTaskState(port, "exited");
                if (getChildPort() == port)
                    clearChildPort();
                return false;
            }
            res.status = r->status;
            res.set_content(r->body, "application/json");
            return true;
        }
        return false;
    };

    // 返回值：1=已转发并设置 res；0=无 target，调用方可用本地逻辑应答；2=有 target 但转发失败，已写
    // 502，禁止再用本地数据应答（避免把父进程数据当任务数据返回）。
    auto tryForwardToTarget =
        [extractTarget, forwardToPort](const httplib::Request &req, httplib::Response &res) -> int {
        std::string target = extractTarget(req);
        int port           = resolveTargetToPort(target);
        if (port <= 0)
            return 0;
        if (forwardToPort(port, req, res))
            return 1;
        json j;
        j["error"] = "Target unreachable (connection failed or timeout).";
        res.status = 502;
        res.set_content(j.dump(), "application/json");
        return 2;
    };

    // 父进程已有任务时，按任务维度的接口不允许用父进程本地数据应答，否则会混淆“当前任务”与父进程；返回
    // 400 提示前端选择任务。
    auto requireNoTasksForLocal = [](httplib::Response &res) -> bool {
        if (getTasks().empty())
            return false;
        json j;
        j["error"] = "No target specified. Select a task in the sidebar.";
        res.status = 400;
        res.set_content(j.dump(), "application/json");
        return true;
    };

    // 父进程绝不使用本地“任务运行时”数据（profiler、pause、alloc）应答；这些数据只存在于跑脚本的子进程。未转发时必须
    // 400，避免返回空的父进程数据导致前端显示错误。
    auto requireWorkerForLocal = [this](httplib::Response &res) -> bool {
        if (isWorkerProcess())
            return false;
        json j;
        j["error"] = "No task selected or target required. Run a script and select a task in the "
                     "sidebar, or add ?target=<taskId>.";
        res.status = 400;
        res.set_content(j.dump(), "application/json");
        return true;
    };

    // -----------------------------------------------------------------------
    // Query handlers (direct, not via Command dispatcher)
    // -----------------------------------------------------------------------
    // GET /api/state is global: parent returns task list + global info.
    // assertionError 仅在 worker 进程（无 tasks）时输出，父进程不合并 lastRunError_（它始终为空）；
    // 前端通过 GET /api/state?target=<taskId> 获取子进程的 assertion error。
    svr.Get(
        "/api/state",
        [this, tryForwardToTarget](const httplib::Request &req, httplib::Response &res) {
            // 如果有 target，转发到子进程获取该任务的完整状态（含 assertionError）。
            if (tryForwardToTarget(req, res) != 0)
                return;
            if (getState_) {
                json j = json::parse(getState_());
                // worker 进程（无 tasks）: 附加 assertion error（来自本进程跑脚本产生的 assert）
                if (isWorkerProcess()) {
                    std::lock_guard<std::mutex> lock(lastRunErrorMutex_);
                    if (!lastRunError_.empty() && lastRunError_ != "restart requested") {
                        j["assertionError"] = lastRunError_;
                        if (!lastRunErrorExpression_.empty())
                            j["assertionExpression"] = lastRunErrorExpression_;
                        if (!lastRunErrorFile_.empty())
                            j["assertionFile"] = lastRunErrorFile_;
                        if (lastRunErrorLine_ > 0)
                            j["assertionLine"] = lastRunErrorLine_;
                    }
                }
                res.set_content(j.dump(), "application/json");
            } else {
                res.set_content("{}", "application/json");
            }
        });

    svr.Get(
        "/api/gir-dot",
        [this, tryForwardToTarget, requireNoTasksForLocal, requireWorkerForLocal](
            const httplib::Request &req,
            httplib::Response &res) {
            if (tryForwardToTarget(req, res) != 0)
                return;
            if (requireNoTasksForLocal(res))
                return;
            if (requireWorkerForLocal(res))
                return;
            if (!getGirDot_) {
                res.set_content("{\"ok\":false,\"error\":\"not configured\"}", "application/json");
                return;
            }
            std::string path = req.get_param_value("path");
            try {
                auto [dot, err] = getGirDot_(path);
                if (!err.empty()) {
                    json j;
                    j["ok"]    = false;
                    j["error"] = err;
                    res.set_content(j.dump(), "application/json");
                    return;
                }
                json j;
                j["ok"]  = true;
                j["dot"] = dot;
                res.set_content(j.dump(), "application/json");
            } catch (const std::exception &e) {
                json j;
                j["ok"]    = false;
                j["error"] = e.what();
                res.set_content(j.dump(), "application/json");
            }
        });

    svr.Get(
        "/api/snapshot",
        [this, tryForwardToTarget, requireNoTasksForLocal, requireWorkerForLocal](
            const httplib::Request &req,
            httplib::Response &res) {
            if (tryForwardToTarget(req, res) != 0)
                return;
            if (requireNoTasksForLocal(res))
                return;
            if (requireWorkerForLocal(res))
                return;
            try {
                std::string raw = mm::profiler::snapshotToJson();
                if (raw.empty()) {
                    res.set_content("{}", "application/json");
                    return;
                }
                json j      = json::parse(raw);
                j["source"] = isWorkerProcess() ? "worker" : "parent";
                res.set_content(j.dump(2), "application/json");
            } catch (const std::exception &e) {
                res.set_content(
                    "{\"error\":\"" + std::string(e.what()) + "\"}",
                    "application/json");
            }
        });

    svr.Get(
        "/api/step-paused",
        [this, tryForwardToTarget, requireNoTasksForLocal, requireWorkerForLocal](
            const httplib::Request &req,
            httplib::Response &res) {
            if (tryForwardToTarget(req, res) != 0)
                return;
            if (requireNoTasksForLocal(res))
                return;
            if (requireWorkerForLocal(res))
                return;
            if (!paused_.load()) {
                res.set_content("{\"paused\":false}", "application/json");
                return;
            }
            std::lock_guard<std::mutex> lock(jsonMutex_);
            if (lastAllocJson_.empty()) {
                res.set_content("{\"paused\":true}", "application/json");
                return;
            }
            nlohmann::json j = nlohmann::json::parse(lastAllocJson_);
            j["paused"]      = true;
            res.set_content(j.dump(), "application/json");
        });

    svr.Get(
        "/api/last-alloc",
        [this, tryForwardToTarget, requireNoTasksForLocal, requireWorkerForLocal](
            const httplib::Request &req,
            httplib::Response &res) {
            if (tryForwardToTarget(req, res) != 0)
                return;
            if (requireNoTasksForLocal(res))
                return;
            if (requireWorkerForLocal(res))
                return;
            std::lock_guard<std::mutex> lock(jsonMutex_);
            res.set_content(lastAllocJson_.empty() ? "{}" : lastAllocJson_, "application/json");
        });

    // GET /api/log：无 target 时仅用本进程 LogSink（不转发），Debugger tab；有 target
    // 时父进程从任务日志缓冲返回，Task tab。 这样避免“无 target
    // 被解析为唯一任务”导致误转发，且父进程 Logger 不再混入子进程管道输出。
    svr.Get("/api/log", [this](const httplib::Request &req, httplib::Response &res) {
        std::string targetParam = req.get_param_value("target");
        size_t offset           = 0;
        if (auto v = req.get_param_value("offset"); !v.empty())
            offset = std::strtoull(v.c_str(), nullptr, 0);

        if (!targetParam.empty()) {
            int port = resolveTargetToPort(targetParam);
            if (!isWorkerProcess() && port > 0) {
                std::vector<std::string> lines;
                size_t nextOffset = 0;
                getTaskLogLines(port, offset, lines, nextOffset);
                json j;
                j["lines"]      = lines;
                j["nextOffset"] = nextOffset;
                res.set_content(j.dump(), "application/json");
                return;
            }
            if (!isWorkerProcess() && port <= 0) {
                json j;
                j["lines"]      = json::array();
                j["nextOffset"] = 0;
                j["error"]      = "No such task or task exited.";
                res.status      = 400;
                res.set_content(j.dump(), "application/json");
                return;
            }
        }

        try {
            std::vector<std::string> lines;
            size_t nextOffset = 0;
            if (logSink_)
                logSink_->getLines(offset, lines, nextOffset);
            json j;
            j["lines"]      = lines;
            j["nextOffset"] = nextOffset;
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception &e) {
            json j;
            j["lines"]      = json::array();
            j["nextOffset"] = 0;
            j["error"]      = e.what();
            res.set_content(j.dump(), "application/json");
        }
    });

    svr.Get(
        "/api/breakpoint-spaces",
        [this, tryForwardToTarget, requireNoTasksForLocal, requireWorkerForLocal](
            const httplib::Request &req,
            httplib::Response &res) {
            if (tryForwardToTarget(req, res) != 0)
                return;
            if (requireNoTasksForLocal(res))
                return;
            if (requireWorkerForLocal(res))
                return;
            std::lock_guard<std::mutex> lock(allocBreakSpacesMutex_);
            json j = json::array();
            for (const auto &s : allocBreakSpaces_)
                j.push_back(s);
            res.set_content("{\"breakSpaces\":" + j.dump() + "}", "application/json");
        });

    svr.Get(
        "/api/breakpoint-types",
        [tryForwardToTarget,
         requireNoTasksForLocal,
         requireWorkerForLocal](const httplib::Request &req, httplib::Response &res) {
            if (tryForwardToTarget(req, res) != 0)
                return;
            if (requireNoTasksForLocal(res))
                return;
            if (requireWorkerForLocal(res))
                return;
            try {
                auto known   = camel::DebugBreakpoint::GetKnownTypes();
                auto enabled = camel::DebugBreakpoint::GetEnabledTypes();
                std::vector<std::string> knownFiltered;
                for (const auto &t : known)
                    if (t != "alloc_before")
                        knownFiltered.push_back(t);
                json j;
                j["known"]   = knownFiltered;
                j["enabled"] = enabled;
                res.set_content(j.dump(), "application/json");
            } catch (const std::exception &e) {
                res.set_content(
                    "{\"error\":\"" + std::string(e.what()) + "\"}",
                    "application/json");
            }
        });

    svr.Get(
        "/api/region/:name/memory",
        [tryForwardToTarget,
         requireNoTasksForLocal,
         requireWorkerForLocal](const httplib::Request &req, httplib::Response &res) {
            if (tryForwardToTarget(req, res) != 0)
                return;
            if (requireNoTasksForLocal(res))
                return;
            if (requireWorkerForLocal(res))
                return;
            auto it = req.path_params.find("name");
            if (it == req.path_params.end()) {
                res.set_content("{\"error\":\"missing region name\"}", "application/json");
                return;
            }
            size_t offset = 0, limit = 512;
            if (auto v = req.get_param_value("offset"); !v.empty())
                offset = std::strtoull(v.c_str(), nullptr, 0);
            if (auto v = req.get_param_value("limit"); !v.empty())
                limit = std::strtoull(v.c_str(), nullptr, 0);
            try {
                std::string json =
                    mm::profiler::regionMemoryRawToJson(it->second.c_str(), offset, limit);
                res.set_content(json, "application/json");
            } catch (const std::exception &e) {
                res.set_content(
                    "{\"error\":\"" + std::string(e.what()) + "\"}",
                    "application/json");
            }
        });

    svr.Get(
        "/api/region/:name/objects",
        [tryForwardToTarget,
         requireNoTasksForLocal,
         requireWorkerForLocal](const httplib::Request &req, httplib::Response &res) {
            if (tryForwardToTarget(req, res) != 0)
                return;
            if (requireNoTasksForLocal(res))
                return;
            if (requireWorkerForLocal(res))
                return;
            auto it = req.path_params.find("name");
            if (it == req.path_params.end()) {
                res.set_content("{\"error\":\"missing region name\"}", "application/json");
                return;
            }
            size_t offset = 0, limit = 50;
            if (auto v = req.get_param_value("offset"); !v.empty())
                offset = std::strtoull(v.c_str(), nullptr, 0);
            if (auto v = req.get_param_value("limit"); !v.empty())
                limit = std::strtoull(v.c_str(), nullptr, 0);
            try {
                std::string json =
                    mm::profiler::regionObjectsToJson(it->second.c_str(), offset, limit);
                res.set_content(json, "application/json");
            } catch (const std::exception &e) {
                res.set_content(
                    "{\"error\":\"" + std::string(e.what()) + "\"}",
                    "application/json");
            }
        });

    // -----------------------------------------------------------------------
    // Mutation handlers (thin dispatch layer via Command dispatcher)
    // -----------------------------------------------------------------------

    svr.Post("/api/file", [](const httplib::Request &req, httplib::Response &res) {
        dispatchAndRespond("loadSource", req, res);
    });

    svr.Post("/api/run", [this](const httplib::Request &req, httplib::Response &res) {
        if (workerRunHandler_) {
            std::string body;
            workerRunHandler_(req.body.empty() ? "{}" : req.body, body);
            res.set_content(body.empty() ? "{}" : body, "application/json");
        } else {
            dispatchAndRespond("launch", req, res);
        }
    });

    // 父进程有 target 时转发；子进程无 tasks，tryForwardToTarget 返回 0，走 dispatchAndRespond
    // 在本地执行 requestContinue/Restart/Terminate。
    svr.Post(
        "/api/continue",
        [tryForwardToTarget](const httplib::Request &req, httplib::Response &res) {
            if (tryForwardToTarget(req, res) != 0)
                return;
            dispatchAndRespond("continue", req, res);
        });

    svr.Post(
        "/api/restart",
        [tryForwardToTarget](const httplib::Request &req, httplib::Response &res) {
            if (tryForwardToTarget(req, res) != 0)
                return;
            dispatchAndRespond("restart", req, res);
        });

    svr.Post(
        "/api/terminate",
        [tryForwardToTarget](const httplib::Request &req, httplib::Response &res) {
            if (tryForwardToTarget(req, res) != 0)
                return;
            dispatchAndRespond("terminate", req, res);
        });

    svr.Get(
        "/api/settings",
        [tryForwardToTarget,
         requireNoTasksForLocal](const httplib::Request &req, httplib::Response &res) {
            if (tryForwardToTarget(req, res) != 0)
                return;
            if (requireNoTasksForLocal(res))
                return;
            json j;
            j["verbose"]          = getVerbose();
            j["logFile"]          = getLogFilePath();
            j["allocStepEnabled"] = getServer().allocStepEnabled();
            j["memoryMonitorRunning"] =
                getServer()
                    .isMemoryScanRunning(); // 任务级：仅 worker 为 true，供前端“Memory scan on”展示
#ifndef NDEBUG
            j["allocBreakpointsAvailable"] = true;
#else
            j["allocBreakpointsAvailable"] = false;
#endif
            res.set_content(j.dump(), "application/json");
        });

    svr.Post(
        "/api/settings",
        [tryForwardToTarget](const httplib::Request &req, httplib::Response &res) {
            if (tryForwardToTarget(req, res) != 0)
                return;
            dispatchAndRespond("configure", req, res);
        });

    svr.Post(
        "/api/breakpoint-spaces",
        [tryForwardToTarget](const httplib::Request &req, httplib::Response &res) {
            if (tryForwardToTarget(req, res) != 0)
                return;
            dispatchAndRespond("setBreakpointFilter", req, res);
        });

    svr.Post(
        "/api/breakpoint-types",
        [tryForwardToTarget](const httplib::Request &req, httplib::Response &res) {
            if (tryForwardToTarget(req, res) != 0)
                return;
            dispatchAndRespond("setBreakpointTypes", req, res);
        });

    svr.set_payload_max_length(1024);
    svr.set_keep_alive_timeout(1);

    if (!svr.listen("127.0.0.1", port_)) {
        std::cerr << "[debugger] HTTP server failed to start, port " << port_ << " may be in use"
                  << std::endl;
    }
    serverPtr_.store(nullptr);
}

} // namespace debugger
