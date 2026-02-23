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
 * Updated: Feb. 24, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "debugger_server.h"
#include "camel/core/debug_breakpoint.h"
#include "camel/core/mm/profiler.h"
#include "camel/utils/assert.h"
#include "camel/utils/log.h"
#include "nlohmann/json.hpp"

#include <algorithm>
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

constexpr size_t kLogSinkMaxLines = 2000;

class LogSinkBuf : public std::streambuf {
  public:
    LogSinkBuf(std::mutex &m, std::deque<std::string> &q) : mutex_(m), queue_(q) {
        setp(buf_, buf_ + sizeof(buf_));
    }

  protected:
    int overflow(int c) override {
        sync();
        if (c != EOF) {
            *pptr() = static_cast<char>(c);
            pbump(1);
        }
        return c;
    }
    int sync() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pptr() > pbase()) {
            queue_.emplace_back(pbase(), pptr());
            while (queue_.size() > kLogSinkMaxLines)
                queue_.pop_front();
            setp(pbase(), epptr());
        }
        return 0;
    }

  private:
    std::mutex &mutex_;
    std::deque<std::string> &queue_;
    char buf_[256];
};

} // namespace

class DebuggerServer::LogSink {
  public:
    LogSink() : buf_(mutex_, lines_), stream_(&buf_) {}
    std::ostream *getStream() { return &stream_; }
    void getLines(size_t offset, std::vector<std::string> &out, size_t &nextOffset) {
        std::lock_guard<std::mutex> lock(mutex_);
        nextOffset = startIndex_ + lines_.size();
        if (offset < startIndex_)
            offset = startIndex_;
        for (size_t i = offset - startIndex_; i < lines_.size(); ++i)
            out.push_back(lines_[i]);
    }

  private:
    std::mutex mutex_;
    size_t startIndex_ = 0;
    std::deque<std::string> lines_;
    LogSinkBuf buf_;
    std::ostream stream_;
};

DebuggerServer::DebuggerServer() : logSink_(std::make_unique<LogSink>()) {}

DebuggerServer::~DebuggerServer() { stop(); }

void DebuggerServer::setDebuggerCallbacks(
    GetStateFn getState, LoadFileFn loadFile, RunWithOptsFn runWithOpts,
    SetSettingsFn setSettings) {
    getState_    = std::move(getState);
    loadFile_    = std::move(loadFile);
    runWithOpts_ = std::move(runWithOpts);
    setSettings_ = std::move(setSettings);
}

void DebuggerServer::start(int port) {
    if (running_)
        return;
    port_       = port;
    running_    = true;
    httpStop_   = false;
    scanStop_   = true;
    httpThread_ = std::thread(&DebuggerServer::httpServerLoop, this);

    std::string msg = "API http://127.0.0.1:" + std::to_string(port_) +
                      " | Web UI: python tools/debugger/serve_ui.py";
    std::cout << msg << std::endl;
    Logger::WriteToAllStreams(msg);
#ifndef NDEBUG
    if (logSink_ && logStreamHandle_ == 0)
        logStreamHandle_ = Logger::AddOutputStream(logSink_->getStream());
#endif
}

void DebuggerServer::startMemoryScan() {
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
    if (!allocStepEnabled_ || !running_)
        return;
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
            return continueRequested_.load() || restartRequested_.load() || !running_ ||
                   !allocStepEnabled_;
        });
    }
    paused_.store(false);
    if (restartRequested_.exchange(false))
        throw RestartRequestedException();
}

void DebuggerServer::requestRestart() {
    {
        std::lock_guard<std::mutex> lock(jsonMutex_);
        lastAllocJson_.clear();
        latestJson_.clear();
    }
    paused_.store(false);
    restartRequested_.store(true);
    continueCond_.notify_one();
}

void DebuggerServer::stop() {
    if (!running_)
        return;
    running_           = false;
    scanStop_          = true;
    httpStop_          = true;
    memoryScanRunning_ = false;
    allocStepEnabled_  = false;
    continueRequested_ = true;
    continueCond_.notify_all();
#ifndef NDEBUG
    if (logStreamHandle_ != 0) {
        Logger::RemoveOutputStream(logStreamHandle_);
        logStreamHandle_ = 0;
    }
#endif

    auto *svr = reinterpret_cast<httplib::Server *>(serverPtr_.load());
    if (svr)
        svr->stop();

    if (scanThread_.joinable())
        scanThread_.join();
    if (httpThread_.joinable())
        httpThread_.join();
}

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

void DebuggerServer::httpServerLoop() {
    httplib::Server svr;
    serverPtr_.store(&svr);

    svr.Get("/api/state", [this](const httplib::Request &, httplib::Response &res) {
        if (getState_) {
            json j = json::parse(getState_());
            {
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
    svr.Post("/api/file", [this](const httplib::Request &req, httplib::Response &res) {
        if (!loadFile_) {
            res.set_content("{\"ok\":false,\"error\":\"not configured\"}", "application/json");
            return;
        }
        try {
            json body        = json::parse(req.body.empty() ? "{}" : req.body);
            std::string path = body.value("path", "");
            bool ok          = loadFile_(path);
            res.set_content(
                ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"load failed\"}",
                "application/json");
        } catch (const std::exception &e) {
            res.set_content(
                "{\"ok\":false,\"error\":\"" + std::string(e.what()) + "\"}",
                "application/json");
        }
    });
    svr.Post("/api/run", [this](const httplib::Request &req, httplib::Response &res) {
        if (!runWithOpts_) {
            res.set_content("{\"ok\":false,\"error\":\"not configured\"}", "application/json");
            return;
        }
        try {
            json body          = json::parse(req.body.empty() ? "{}" : req.body);
            bool memoryMonitor = body.value("memoryMonitor", true);
            bool allocStep     = body.value("allocStep", false);
            // 在后台线程执行 run，避免阻塞 HTTP 线程；否则 alloc-step 暂停时无法处理 /api/continue
            {
                std::lock_guard<std::mutex> lock(lastRunErrorMutex_);
                lastRunError_.clear();
                lastRunErrorExpression_.clear();
                lastRunErrorFile_.clear();
                lastRunErrorLine_ = 0;
            }
            std::thread([this, memoryMonitor, allocStep]() {
                try {
                    runWithOpts_(memoryMonitor, allocStep);
                } catch (const RestartRequestedException &) {
                    // Restart requested by Web UI; do not store as run error (overlay should hide).
                } catch (const cml::AssertionFailure &e) {
                    std::lock_guard<std::mutex> lock(lastRunErrorMutex_);
                    lastRunError_           = e.what();
                    lastRunErrorExpression_ = e.expression();
                    lastRunErrorFile_       = e.location().file_name();
                    lastRunErrorLine_       = static_cast<int>(e.location().line());
                    Logger::WriteToAllStreams(std::string("[assertion] ") + e.what());
                } catch (const std::exception &e) {
                    std::string msg(e.what());
                    if (msg != "restart requested") {
                        std::lock_guard<std::mutex> lock(lastRunErrorMutex_);
                        lastRunError_ = std::move(msg);
                        lastRunErrorExpression_.clear();
                        lastRunErrorFile_.clear();
                        lastRunErrorLine_ = 0;
                        std::cerr << "[debugger] Run thread: " << e.what() << std::endl;
                    }
                }
            }).detach();
            res.set_content("{\"ok\":true}", "application/json");
        } catch (const std::exception &e) {
            res.set_content(
                "{\"ok\":false,\"error\":\"" + std::string(e.what()) + "\"}",
                "application/json");
        }
    });

    svr.Get("/api/snapshot", [this](const httplib::Request &, httplib::Response &res) {
        std::lock_guard<std::mutex> lock(jsonMutex_);
        if (latestJson_.empty()) {
            res.set_content("{}", "application/json");
        } else {
            res.set_content(latestJson_, "application/json");
        }
    });

    svr.Get("/api/step-paused", [this](const httplib::Request &, httplib::Response &res) {
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

    svr.Get("/api/last-alloc", [this](const httplib::Request &, httplib::Response &res) {
        std::lock_guard<std::mutex> lock(jsonMutex_);
        res.set_content(lastAllocJson_.empty() ? "{}" : lastAllocJson_, "application/json");
    });

    svr.Get("/api/log", [this](const httplib::Request &req, httplib::Response &res) {
        size_t offset = 0;
        if (auto v = req.get_param_value("offset"); !v.empty())
            offset = std::strtoull(v.c_str(), nullptr, 0);
        std::vector<std::string> lines;
        size_t nextOffset = 0;
        if (logSink_)
            logSink_->getLines(offset, lines, nextOffset);
        json j;
        j["lines"]      = lines;
        j["nextOffset"] = nextOffset;
        res.set_content(j.dump(), "application/json");
    });

    svr.Post("/api/continue", [this](const httplib::Request &, httplib::Response &res) {
        std::cout << "Continue." << std::endl;
        Logger::WriteToAllStreams("Continue.");
        continueRequested_.store(true);
        continueCond_.notify_one();
        res.set_content("{\"ok\":true}", "application/json");
    });

    svr.Post("/api/restart", [this](const httplib::Request &, httplib::Response &res) {
        {
            std::lock_guard<std::mutex> lock(lastRunErrorMutex_);
            lastRunError_.clear();
            lastRunErrorExpression_.clear();
            lastRunErrorFile_.clear();
            lastRunErrorLine_ = 0;
        }
        std::cout << "Restart." << std::endl;
        Logger::WriteToAllStreams("Restart.");
        requestRestart();
        res.set_content("{\"ok\":true}", "application/json");
    });

    svr.Post("/api/settings", [this](const httplib::Request &req, httplib::Response &res) {
        if (!setSettings_) {
            res.set_content("{\"ok\":false,\"error\":\"not configured\"}", "application/json");
            return;
        }
        try {
            json body           = json::parse(req.body.empty() ? "{}" : req.body);
            bool verbose        = body.value("verbose", false);
            std::string logFile = body.value("logFile", "");
            setSettings_(verbose, logFile);
            res.set_content("{\"ok\":true}", "application/json");
        } catch (const std::exception &e) {
            res.set_content(
                "{\"ok\":false,\"error\":\"" + std::string(e.what()) + "\"}",
                "application/json");
        }
    });

    svr.Get("/api/breakpoint-types", [](const httplib::Request &, httplib::Response &res) {
        try {
            auto known   = camel::DebugBreakpoint::GetKnownTypes();
            auto enabled = camel::DebugBreakpoint::GetEnabledTypes();
            std::vector<std::string> knownFiltered;
            for (const auto &t : known) {
                if (t != "alloc_before")
                    knownFiltered.push_back(t);
            }
            json j;
            j["known"]   = knownFiltered;
            j["enabled"] = enabled;
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception &e) {
            res.set_content("{\"error\":\"" + std::string(e.what()) + "\"}", "application/json");
        }
    });

    svr.Post("/api/breakpoint-types", [](const httplib::Request &req, httplib::Response &res) {
        try {
            json body    = json::parse(req.body.empty() ? "{}" : req.body);
            auto enabled = body.value("enabled", std::vector<std::string>{});
            auto known   = camel::DebugBreakpoint::GetKnownTypes();
            std::unordered_set<std::string> toEnable(enabled.begin(), enabled.end());
            for (const auto &t : known) {
                if (t == "alloc_before")
                    continue;
                if (toEnable.count(t)) {
                    camel::DebugBreakpoint::EnableType(t.c_str());
                    if (t == "alloc")
                        camel::DebugBreakpoint::EnableType("alloc_before");
                } else {
                    camel::DebugBreakpoint::DisableType(t.c_str());
                    if (t == "alloc")
                        camel::DebugBreakpoint::DisableType("alloc_before");
                }
            }
            for (const auto &t : enabled) {
                if (std::find(known.begin(), known.end(), t) == known.end() && t != "alloc_before")
                    camel::DebugBreakpoint::EnableType(t.c_str());
            }
            res.set_content("{\"ok\":true}", "application/json");
        } catch (const std::exception &e) {
            res.set_content(
                "{\"ok\":false,\"error\":\"" + std::string(e.what()) + "\"}",
                "application/json");
        }
    });

    svr.Get("/api/region/:name/memory", [](const httplib::Request &req, httplib::Response &res) {
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
            res.set_content("{\"error\":\"" + std::string(e.what()) + "\"}", "application/json");
        }
    });
    svr.Get("/api/region/:name/objects", [](const httplib::Request &req, httplib::Response &res) {
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
            std::string json = mm::profiler::regionObjectsToJson(it->second.c_str(), offset, limit);
            res.set_content(json, "application/json");
        } catch (const std::exception &e) {
            res.set_content("{\"error\":\"" + std::string(e.what()) + "\"}", "application/json");
        }
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
