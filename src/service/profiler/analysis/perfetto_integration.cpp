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
 * Created: Sep. 27, 2025
 * Updated: Oct. 04, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "perfetto_integration.h"
#include "../core/trace.h"
#include "utils/log.h"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

#ifndef NDEBUG

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// windows.h should be included before shellapi.h
#include <shellapi.h>
#else
#include <unistd.h>
#endif

namespace profiler {

PerfettoIntegration &PerfettoIntegration::getInstance() {
    static PerfettoIntegration instance;
    return instance;
}

void PerfettoIntegration::startTracing(const std::string &output_file) {
    tracingEnabled_ = true;
    outputFile_ = output_file;
    events_.clear();

    recordEventInstant("perfetto_tracing_started", "perfetto");
}

void PerfettoIntegration::stopAndOpenPerfetto() {
    if (!tracingEnabled_) {
        l.in("Profiler").warn("Tracing was not started, nothing to stop.");
        return;
    }

    generatePerfettoFile();

#ifdef _WIN32
    char *profile_mode = nullptr;
    size_t len = 0;
    errno_t err = _dupenv_s(&profile_mode, &len, "CAMEL_PROFILE_MODE");
    bool auto_open = (err == 0 && profile_mode != nullptr && std::string(profile_mode) == "FULL");
    if (profile_mode) {
        free(profile_mode);
    }
#else
    const char *profile_mode = std::getenv("CAMEL_PROFILE_MODE");
    bool auto_open = (profile_mode && std::string(profile_mode) == "FULL");
#endif

    if (auto_open) {
        std::thread([this]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            openPerfettoInBrowser();
        }).detach();
    }

    tracingEnabled_ = false;
}

void PerfettoIntegration::recordEventBegin(const std::string &name, const std::string &category) {
    if (!tracingEnabled_)
        return;

    PerfettoTraceEvent event;
    event.name = name;
    event.category = category;
    event.phase = 'B';
    event.timestamp = getCurrentTimestamp();
    event.threadId = getCurrentThreadId();

    events_.push_back(event);
}

void PerfettoIntegration::recordEventEnd(const std::string &name, const std::string &category) {
    if (!tracingEnabled_)
        return;

    PerfettoTraceEvent event;
    event.name = name;
    event.category = category;
    event.phase = 'E';
    event.timestamp = getCurrentTimestamp();
    event.threadId = getCurrentThreadId();

    events_.push_back(event);
}

void PerfettoIntegration::recordEventInstant(const std::string &name, const std::string &category) {
    if (!tracingEnabled_)
        return;

    PerfettoTraceEvent event;
    event.name = name;
    event.category = category;
    event.phase = 'i';
    event.timestamp = getCurrentTimestamp();
    event.threadId = getCurrentThreadId();

    events_.push_back(event);
}

uint64_t PerfettoIntegration::getCurrentTimestamp() {
    auto now = std::chrono::high_resolution_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    return us;
}

uint32_t PerfettoIntegration::getCurrentThreadId() {
#ifdef _WIN32
    return static_cast<uint32_t>(GetCurrentThreadId());
#else
    return static_cast<uint32_t>(pthread_self());
#endif
}

void PerfettoIntegration::generatePerfettoFile() {
    std::filesystem::path filepath(outputFile_);
    if (filepath.has_parent_path()) {
        std::filesystem::create_directories(filepath.parent_path());
    }

    std::ofstream file(outputFile_);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file " << outputFile_ << " for writing" << std::endl;
        return;
    }

    file << "{\n";
    file << "  \"traceEvents\": [\n";

    bool first = true;
    for (const auto &event : events_) {
        if (!first) {
            file << ",\n";
        }
        first = false;
        std::string name = event.name;
        std::string category = event.category;

        for (auto it = name.begin(); it != name.end(); ++it) {
            if (*it == '"' || *it == '\\') {
                it = name.insert(it, '\\') + 1;
            }
        }

        for (auto it = category.begin(); it != category.end(); ++it) {
            if (*it == '"' || *it == '\\') {
                it = category.insert(it, '\\') + 1;
            }
        }

        file << "    {\n";
        file << "      \"name\": \"" << name << "\",\n";
        file << "      \"cat\": \"" << category << "\",\n";
        file << "      \"ph\": \"" << event.phase << "\",\n";
        file << "      \"ts\": " << event.timestamp << ",\n";
        file << "      \"pid\": " << event.processId << ",\n";
        file << "      \"tid\": " << event.threadId << "\n";
        file << "    }";
    }

    file << "\n  ]\n";
    file << "}\n";
    file.close();
}

void PerfettoIntegration::openPerfettoInBrowser(bool auto_open) {
    l.in("Profiler")
        .info("To view trace results, manually open https://ui.perfetto.dev/ and load the trace "
              "file.");
}

} // namespace profiler

#endif // NDEBUG
