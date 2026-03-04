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
 * Updated: Mar. 04, 2026
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

// nlohmann 必须在解析器相关头之前（否则 ANTLR 的 EOF 会遮蔽 C 的 EOF）
#include "nlohmann/json.hpp"
// state.h 必须在任何会拉入 Windows 的头之前，避免 minwindef/wingdi 的 TRUE/FALSE/ERROR/IN/CONST
// 与解析器冲突
#include "state.h"

#include "camel/core/debug_breakpoint.h"
#include "camel/core/mm/profiler.h"
#include "camel/utils/log.h"
#include "command/dispatcher.h"
#include "server.h"

#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
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

void DebuggerServer::setQueryCallbacks(GetStateFn getState, GetGirJsonFn getGirJson) {
    getState_   = std::move(getState);
    getGirJson_ = std::move(getGirJson);
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
    // 统一断点模型：仅当该类型断点条件（此处为分配空间集合）非空且命中时才暂停。
    {
        std::lock_guard<std::mutex> lock(allocBreakSpacesMutex_);
        if (allocBreakSpaces_.empty())
            return;
        std::string s(space ? space : "");
        if (allocBreakSpaces_.count(s) == 0)
            return;
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

std::unordered_set<std::string> DebuggerServer::getAllocBreakSpaces() {
    std::lock_guard<std::mutex> lock(allocBreakSpacesMutex_);
    return allocBreakSpaces_;
}

void DebuggerServer::pauseAndWaitForGirBreakpoint(
    const std::string &nodeId, const std::string &graphId) {
    assert(isWorkerProcess() && "pauseAndWaitForGirBreakpoint() called in non-worker process");
    if (!running_)
        return;
    try {
        json j;
        j["phase"]   = "gir_node";
        j["nodeId"]  = nodeId;
        j["graphId"] = graphId;
        {
            std::lock_guard<std::mutex> lock(jsonMutex_);
            lastAllocJson_ = j.dump();
        }
    } catch (const std::exception &e) {
        std::cerr << "[debugger] GIR pause exception: " << e.what() << std::endl;
    }
    paused_.store(true);
    continueRequested_.store(false);
    {
        std::unique_lock<std::mutex> lock(continueMutex_);
        continueCond_.wait(lock, [this] {
            return continueRequested_.load() || restartRequested_.load() ||
                   terminateRequested_.load() || !running_;
        });
    }
    paused_.store(false);
    if (restartRequested_.exchange(false))
        throw RestartRequestedException();
    if (terminateRequested_.exchange(false))
        throw TerminateRequestedException();
}

void DebuggerServer::pauseAndWaitForPipelineStage(const char *stageId) {
    assert(isWorkerProcess() && "pauseAndWaitForPipelineStage() called in non-worker process");
    if (!running_ || !stageId)
        return;
    try {
        json j;
        j["phase"]   = "pipeline";
        j["stageId"] = stageId;
        {
            std::lock_guard<std::mutex> lock(jsonMutex_);
            lastAllocJson_ = j.dump();
        }
    } catch (const std::exception &e) {
        std::cerr << "[debugger] Pipeline pause exception: " << e.what() << std::endl;
    }
    paused_.store(true);
    continueRequested_.store(false);
    {
        std::unique_lock<std::mutex> lock(continueMutex_);
        continueCond_.wait(lock, [this] {
            return continueRequested_.load() || restartRequested_.load() ||
                   terminateRequested_.load() || !running_;
        });
    }
    paused_.store(false);
    if (restartRequested_.exchange(false))
        throw RestartRequestedException();
    if (terminateRequested_.exchange(false))
        throw TerminateRequestedException();
}

void DebuggerServer::setGirBreakpointNodeIds(std::unordered_set<uintptr_t> ids) {
    std::lock_guard<std::mutex> lock(girBreakpointNodeIdsMutex_);
    girBreakpointNodeIds_ = std::move(ids);
    EXEC_WHEN_DEBUG({
        if (girBreakpointNodeIds_.empty())
            camel::DebugBreakpoint::DisableType("gir_node");
        else
            camel::DebugBreakpoint::EnableType("gir_node");
    });
}

bool DebuggerServer::isGirBreakpointNode(uintptr_t nodePtr) const {
    std::lock_guard<std::mutex> lock(girBreakpointNodeIdsMutex_);
    return girBreakpointNodeIds_.count(nodePtr) != 0;
}

std::unordered_set<uintptr_t> DebuggerServer::getGirBreakpointNodeIds() const {
    std::lock_guard<std::mutex> lock(girBreakpointNodeIdsMutex_);
    return girBreakpointNodeIds_;
}

void DebuggerServer::setEnabledBreakpointTypes(std::vector<std::string> types) {
    std::lock_guard<std::mutex> lock(enabledBreakpointTypesMutex_);
    enabledBreakpointTypes_ = std::move(types);
}

std::vector<std::string> DebuggerServer::getEnabledBreakpointTypes() const {
    std::lock_guard<std::mutex> lock(enabledBreakpointTypesMutex_);
    return enabledBreakpointTypes_;
}

void DebuggerServer::setLastRunBody(std::string body) {
    std::lock_guard<std::mutex> lock(lastRunBodyMutex_);
    lastRunBody_ = std::move(body);
}

std::string DebuggerServer::getLastRunBody() const {
    std::lock_guard<std::mutex> lock(lastRunBodyMutex_);
    return lastRunBody_;
}

bool DebuggerServer::forwardPostToChild(const std::string &apiPath, const std::string &body) {
    int port = childPort_.load();
    if (port <= 0)
        return false;
    bool ok = forwardPostToPort(port, apiPath, body);
    if (!ok) {
        clearChildPort();
        return false;
    }
    return true;
}

bool DebuggerServer::forwardPostToPort(
    int port, const std::string &apiPath, const std::string &body) {
    if (port <= 0)
        return false;
    httplib::Client cli("127.0.0.1", port);
    cli.set_connection_timeout(0, 300000);
    auto r = cli.Post(apiPath, body.empty() ? "{}" : body, "application/json");
    return static_cast<bool>(r);
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
    // GET /api/list-dir：仅父进程提供，供 Web UI 文件选择器浏览目录；返回子目录与 .cml 文件。
    if (!isWorkerProcess()) {
        svr.Get("/api/list-dir", [](const httplib::Request &req, httplib::Response &res) {
            namespace fs         = std::filesystem;
            std::string dirParam = req.get_param_value("dir");
            if (dirParam.empty())
                dirParam = ".";
            fs::path root(dirParam);
            json j;
            j["path"]  = root.lexically_normal().string();
            j["dirs"]  = json::array();
            j["files"] = json::array();
            try {
                if (!fs::exists(root) || !fs::is_directory(root)) {
                    j["error"] =
                        "Not a directory or does not exist: " + j["path"].get<std::string>();
                    res.set_content(j.dump(), "application/json");
                    return;
                }
                for (const auto &e : fs::directory_iterator(root)) {
                    try {
                        std::string name = e.path().filename().string();
                        if (name.empty() || name[0] == '.')
                            continue;
                        std::string fullPath = e.path().lexically_normal().string();
                        if (e.is_directory()) {
                            j["dirs"].push_back({{"name", name}, {"path", fullPath}});
                        } else if (e.is_regular_file()) {
                            std::string ext = e.path().extension().string();
                            if (ext == ".cml") {
                                j["files"].push_back({{"name", name}, {"path", fullPath}});
                            }
                        }
                    } catch (...) { /* skip inaccessible entries */
                    }
                }
            } catch (const std::exception &ex) {
                j["error"] = std::string(ex.what());
            }
            res.set_content(j.dump(), "application/json");
        });
    }

    // GET /api/state：仅全局状态。始终返回父进程 state（含 tasks 列表），不转发、不接受 target。
    // 父进程对每个非 exited 任务聚合 step-paused、last-alloc，并请求该任务 GET /api/state 合并
    // assertionError。
    svr.Get("/api/state", [this](const httplib::Request &, httplib::Response &res) {
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
            } else if (j.contains("tasks") && j["tasks"].is_array()) {
                // 父进程：对每个非 exited 任务请求 step-paused、last-alloc 并合并进 task
                try {
                    for (auto &task : j["tasks"]) {
                        if (!task.contains("taskState") || task["taskState"] == "exited")
                            continue;
                        if (!task.contains("port") || !task["port"].is_number_integer())
                            continue;
                        int port = task["port"].get<int>();
                        httplib::Client cli("127.0.0.1", port);
                        cli.set_connection_timeout(0, 200000);
                        auto rSp = cli.Get("/api/step-paused");
                        if (rSp && rSp->status == 200 && !rSp->body.empty()) {
                            try {
                                json sp = json::parse(rSp->body);
                                if (sp.contains("paused"))
                                    task["paused"] = sp["paused"];
                                if (sp.contains("phase") || sp.contains("size") ||
                                    sp.contains("space")) {
                                    json pr;
                                    if (sp.contains("phase"))
                                        pr["phase"] = sp["phase"];
                                    if (sp.contains("size"))
                                        pr["size"] = sp["size"];
                                    if (sp.contains("space"))
                                        pr["space"] = sp["space"];
                                    if (sp.contains("ptr"))
                                        pr["ptr"] = sp["ptr"];
                                    task["pauseReason"] = pr;
                                }
                            } catch (...) { /* ignore parse error */
                            }
                        }
                        auto rLa = cli.Get("/api/last-alloc");
                        if (rLa && rLa->status == 200 && !rLa->body.empty() && rLa->body != "{}") {
                            try {
                                task["lastAlloc"] = json::parse(rLa->body);
                            } catch (...) { /* ignore */
                            }
                        } else {
                            task["lastAlloc"] = nullptr;
                        }
                        auto rSt = cli.Get("/api/state");
                        if (rSt && rSt->status == 200 && !rSt->body.empty()) {
                            try {
                                json ws = json::parse(rSt->body);
                                if (ws.contains("assertionError") && !ws["assertionError"].empty())
                                    task["assertionError"] = ws["assertionError"];
                                if (ws.contains("assertionExpression"))
                                    task["assertionExpression"] = ws["assertionExpression"];
                                if (ws.contains("assertionFile"))
                                    task["assertionFile"] = ws["assertionFile"];
                                if (ws.contains("assertionLine"))
                                    task["assertionLine"] = ws["assertionLine"];
                            } catch (...) { /* ignore */
                            }
                        }
                    }
                } catch (...) {
                    /* 聚合异常时保持原 j 不变 */
                }
            }
            res.set_content(j.dump(), "application/json");
        } else {
            res.set_content("{}", "application/json");
        }
    });

    // GET /api/task-state：单个任务状态。必须带 ?target=<任务id>，转发到该任务并返回其 state。
    // 返回为 worker 的 GET /api/state（无 tasks 或 tasks 为空，含 assertionError 等）。
    svr.Get("/api/task-state", [this](const httplib::Request &req, httplib::Response &res) {
        if (isWorkerProcess()) {
            res.status = 400;
            res.set_content("{\"error\":\"task-state is only on parent.\"}", "application/json");
            return;
        }
        std::string target = req.get_param_value("target");
        if (target.empty()) {
            res.status = 400;
            res.set_content(
                "{\"error\":\"Missing target. Use ?target=<taskId>.\"}",
                "application/json");
            return;
        }
        int port = resolveTargetToPort(target);
        if (port <= 0) {
            res.status = 400;
            res.set_content("{\"error\":\"No such task or task exited.\"}", "application/json");
            return;
        }
        httplib::Client cli("127.0.0.1", port);
        cli.set_connection_timeout(0, 200000);
        auto r = cli.Get("/api/state");
        if (!r) {
            setTaskState(port, "exited");
            if (getChildPort() == port)
                clearChildPort();
            res.status = 502;
            res.set_content("{\"error\":\"Target unreachable.\"}", "application/json");
            return;
        }
        res.status = r->status;
        res.set_content(r->body, "application/json");
    });

    svr.Get(
        "/api/gir-json",
        [this, tryForwardToTarget, requireNoTasksForLocal, requireWorkerForLocal](
            const httplib::Request &req,
            httplib::Response &res) {
            if (tryForwardToTarget(req, res) != 0)
                return;
            if (requireNoTasksForLocal(res))
                return;
            if (requireWorkerForLocal(res))
                return;
            if (!getGirJson_) {
                res.set_content("{\"ok\":false,\"error\":\"not configured\"}", "application/json");
                return;
            }
            std::string path    = req.get_param_value("path");
            std::string graphId = req.get_param_value("graphId");
            try {
                auto [jsonStr, err] = getGirJson_(path, graphId);
                if (!err.empty()) {
                    json j;
                    j["ok"]    = false;
                    j["error"] = err;
                    res.set_content(j.dump(), "application/json");
                    return;
                }
                json j  = json::parse(jsonStr);
                j["ok"] = true;
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

    // step-paused / last-alloc：仅子进程（worker）注册；父进程不暴露，Web UI 通过 GET /api/state
    // 聚合得到 paused/pauseReason/lastAlloc。
    if (isWorkerProcess()) {
        svr.Get("/api/step-paused", [this](const httplib::Request &, httplib::Response &res) {
            if (!paused_.load()) {
                res.set_content("{\"paused\":false}", "application/json");
                return;
            }
            try {
                std::lock_guard<std::mutex> lock(jsonMutex_);
                if (lastAllocJson_.empty()) {
                    res.set_content("{\"paused\":true}", "application/json");
                    return;
                }
                nlohmann::json j = nlohmann::json::parse(lastAllocJson_);
                j["paused"]      = true;
                res.set_content(j.dump(), "application/json");
            } catch (...) {
                res.set_content("{\"paused\":true}", "application/json");
            }
        });
        svr.Get("/api/last-alloc", [this](const httplib::Request &, httplib::Response &res) {
            std::lock_guard<std::mutex> lock(jsonMutex_);
            res.set_content(lastAllocJson_.empty() ? "{}" : lastAllocJson_, "application/json");
        });
    }

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
            if (port <= 0)
                port = getTaskPortByTargetIncludingExited(targetParam);
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
            if (!getTasks().empty() && requireWorkerForLocal(res))
                return;
            std::lock_guard<std::mutex> lock(allocBreakSpacesMutex_);
            json j = json::array();
            for (const auto &s : allocBreakSpaces_)
                j.push_back(s);
            res.set_content("{\"breakSpaces\":" + j.dump() + "}", "application/json");
        });

    // 按类型返回断点状态，便于 Web UI 统一展示；支持 ?target= 转发到任务。
    svr.Get(
        "/api/breakpoints",
        [this, tryForwardToTarget, requireNoTasksForLocal, requireWorkerForLocal](
            const httplib::Request &req,
            httplib::Response &res) {
            if (tryForwardToTarget(req, res) != 0)
                return;
            if (requireNoTasksForLocal(res))
                return;
            if (requireWorkerForLocal(res))
                return;
            json out;
            out["alloc"]           = json::object();
            out["alloc"]["spaces"] = json::array();
            {
                std::lock_guard<std::mutex> lock(allocBreakSpacesMutex_);
                for (const auto &s : allocBreakSpaces_)
                    out["alloc"]["spaces"].push_back(s);
            }
            res.set_content(out.dump(), "application/json");
        });

    svr.Get(
        "/api/gir-breakpoints",
        [this, tryForwardToTarget, requireNoTasksForLocal, requireWorkerForLocal](
            const httplib::Request &req,
            httplib::Response &res) {
            if (tryForwardToTarget(req, res) != 0)
                return;
            if (requireNoTasksForLocal(res))
                return;
            if (requireWorkerForLocal(res))
                return;
            auto ids   = getGirBreakpointNodeIds();
            json j     = json::array();
            auto toHex = [](uintptr_t v) {
                std::ostringstream o;
                o << std::hex << v;
                return o.str();
            };
            for (uintptr_t p : ids)
                j.push_back("0x" + toHex(p));
            json out;
            out["nodeIds"] = j;
            res.set_content(out.dump(), "application/json");
        });

    // 统一断点模型：父进程收到任意断点设置请求时保留一份，Run/Restart 时推送给 worker，不区分类型。
    svr.Post(
        "/api/gir-breakpoints",
        [this, tryForwardToTarget, requireNoTasksForLocal, requireWorkerForLocal](
            const httplib::Request &req,
            httplib::Response &res) {
            std::unordered_set<uintptr_t> ids;
            try {
                json body = json::parse(req.body.empty() ? "{}" : req.body);
                if (body.contains("nodeIds") && body["nodeIds"].is_array()) {
                    for (const auto &v : body["nodeIds"]) {
                        std::string s = v.get<std::string>();
                        uintptr_t u   = 0;
                        if (s.size() > 2 && (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')))
                            u = static_cast<uintptr_t>(std::stoull(s, nullptr, 16));
                        else
                            u = static_cast<uintptr_t>(std::stoull(s, nullptr, 0));
                        ids.insert(u);
                    }
                }
                if (!isWorkerProcess())
                    setGirBreakpointNodeIds(ids);
            } catch (const std::exception &e) {
                json j;
                j["ok"]    = false;
                j["error"] = e.what();
                res.set_content(j.dump(), "application/json");
                return;
            }
            if (tryForwardToTarget(req, res) != 0)
                return;
            if (requireNoTasksForLocal(res))
                return;
            if (requireWorkerForLocal(res))
                return;
            try {
                setGirBreakpointNodeIds(std::move(ids));
                res.set_content("{\"ok\":true}", "application/json");
            } catch (const std::exception &e) {
                json j;
                j["ok"]    = false;
                j["error"] = e.what();
                res.set_content(j.dump(), "application/json");
            }
        });

    svr.Get(
        "/api/pipeline",
        [tryForwardToTarget,
         requireNoTasksForLocal,
         requireWorkerForLocal](const httplib::Request &req, httplib::Response &res) {
            if (tryForwardToTarget(req, res) != 0)
                return;
            if (requireNoTasksForLocal(res))
                return;
            if (requireWorkerForLocal(res))
                return;
            // Worker: return compile stages + runPasses (empty => std::fallback) + GIR-Z
            json j;
            json stages = json::array();
            for (const char *id : {"CTS", "CST", "AST", "GCT"})
                stages.push_back({{"id", id}, {"label", id}});
            const auto &passes = getState().runPasses;
            if (passes.empty()) {
                stages.push_back({{"id", "std::fallback"}, {"label", "fallback"}});
            } else {
                for (const auto &p : passes) {
                    std::string label = p;
                    size_t pos        = p.rfind("::");
                    if (pos != std::string::npos && pos + 2 < p.size())
                        label = p.substr(pos + 2);
                    stages.push_back({{"id", p}, {"label", label}});
                }
            }
            stages.push_back({{"id", "GIR-Z"}, {"label", "GIR-Z"}});
            j["stages"] = std::move(stages);
            res.set_content(j.dump(), "application/json");
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

    // 写操作一律经父进程命令执行并回显，命令内部再按 target 调用 forwardPostToPort 通知子进程。
    svr.Post("/api/continue", [](const httplib::Request &req, httplib::Response &res) {
        dispatchAndRespond("continue", req, res);
    });

    svr.Post("/api/restart", [](const httplib::Request &req, httplib::Response &res) {
        dispatchAndRespond("restart", req, res);
    });

    svr.Post("/api/terminate", [](const httplib::Request &req, httplib::Response &res) {
        dispatchAndRespond("terminate", req, res);
    });

    svr.Get(
        "/api/settings",
        [tryForwardToTarget,
         requireNoTasksForLocal](const httplib::Request &req, httplib::Response &res) {
            if (tryForwardToTarget(req, res) != 0)
                return;
            std::string target = req.get_param_value("target");
            if (!target.empty() && getTaskPortByTargetIncludingExited(target) > 0) {
                // 目标任务已退出，无法转发；返回父进程 settings，避免前端 400。
                json j;
                j["verbose"]              = getVerbose();
                j["logFile"]              = getLogFilePath();
                j["allocStepEnabled"]     = getServer().allocStepEnabled();
                j["memoryMonitorRunning"] = getServer().isMemoryScanRunning();
#ifndef NDEBUG
                j["allocBreakpointsAvailable"] = true;
#else
                j["allocBreakpointsAvailable"] = false;
#endif
                res.set_content(j.dump(), "application/json");
                return;
            }
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

    svr.Post("/api/settings", [](const httplib::Request &req, httplib::Response &res) {
        dispatchAndRespond("configure", req, res);
    });

    svr.Post("/api/breakpoint-spaces", [](const httplib::Request &req, httplib::Response &res) {
        dispatchAndRespond("setBreakpointFilter", req, res);
    });

    svr.Post("/api/breakpoint-types", [](const httplib::Request &req, httplib::Response &res) {
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
