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
 * Updated: Oct. 03, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "perfetto_integration.h"
#include "../core/trace.h"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

#ifndef NDEBUG

#ifdef _WIN32
#include <shellapi.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace profiler {

PerfettoIntegration &PerfettoIntegration::getInstance() {
    static PerfettoIntegration instance;
    return instance;
}

void PerfettoIntegration::startTracing(const std::string &output_file) {
    tracing_enabled_ = true;
    output_file_ = output_file;
    events_.clear();

    recordEventInstant("perfetto_tracing_started", "perfetto");
}

void PerfettoIntegration::stopAndOpenPerfetto() {
    if (!tracing_enabled_) {
        std::cout << "[PROFILER PERFETTO] Tracing not started" << std::endl;
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

    tracing_enabled_ = false;
}

void PerfettoIntegration::recordEventBegin(const std::string &name, const std::string &category) {
    if (!tracing_enabled_)
        return;

    PerfettoTraceEvent event;
    event.name = name;
    event.category = category;
    event.phase = 'B';
    event.timestamp = getCurrentTimestamp();
    event.thread_id = getCurrentThreadId();

    events_.push_back(event);
}

void PerfettoIntegration::recordEventEnd(const std::string &name, const std::string &category) {
    if (!tracing_enabled_)
        return;

    PerfettoTraceEvent event;
    event.name = name;
    event.category = category;
    event.phase = 'E';
    event.timestamp = getCurrentTimestamp();
    event.thread_id = getCurrentThreadId();

    events_.push_back(event);
}

void PerfettoIntegration::recordEventInstant(const std::string &name, const std::string &category) {
    if (!tracing_enabled_)
        return;

    PerfettoTraceEvent event;
    event.name = name;
    event.category = category;
    event.phase = 'i';
    event.timestamp = getCurrentTimestamp();
    event.thread_id = getCurrentThreadId();

    events_.push_back(event);
}

uint64_t PerfettoIntegration::getCurrentTimestamp() {
    auto now = std::chrono::high_resolution_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    return ns;
}

uint32_t PerfettoIntegration::getCurrentThreadId() {
#ifdef _WIN32
    return static_cast<uint32_t>(GetCurrentThreadId());
#else
    return static_cast<uint32_t>(pthread_self());
#endif
}

void PerfettoIntegration::generatePerfettoFile() {
    std::filesystem::path filepath(output_file_);
    if (filepath.has_parent_path()) {
        std::filesystem::create_directories(filepath.parent_path());
    }

    std::ofstream file(output_file_);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file " << output_file_ << " for writing" << std::endl;
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
        file << "      \"pid\": " << event.process_id << ",\n";
        file << "      \"tid\": " << event.thread_id << "\n";
        file << "    }";
    }

    file << "\n  ]\n";
    file << "}\n";
    file.close();

    // std::cout << "[PROFILER PERFETTO] Trace file generated: " << output_file_ << std::endl;
}

void PerfettoIntegration::openPerfettoInBrowser(bool auto_open) {
#ifdef _WIN32
    if (auto_open) {
        std::string url = "https://ui.perfetto.dev/";
        ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
        std::cout << "[PROFILER PERFETTO] Opening Perfetto UI in browser..." << std::endl;
    } else {
        std::cout << "[PROFILER PERFETTO] To view trace results, manually open "
                     "https://ui.perfetto.dev/ and load the trace file."
                  << std::endl;
    }
#else
    if (auto_open) {
        std::string command = "xdg-open https://ui.perfetto.dev/ &";
        int result = system(command.c_str());
        if (result != 0) {
            std::cout << "[PROFILER PERFETTO] Failed to open browser. To view trace results, "
                         "manually open https://ui.perfetto.dev/ and load the trace file."
                      << std::endl;
        } else {
            std::cout << "[PROFILER PERFETTO] Opening Perfetto UI in browser..." << std::endl;
        }
    } else {
        std::cout << "[PROFILER PERFETTO] To view trace results, manually open "
                     "https://ui.perfetto.dev/ and load the trace file."
                  << std::endl;
    }
#endif
}

} // namespace profiler

#endif // NDEBUG
