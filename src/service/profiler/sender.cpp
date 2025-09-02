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
 * Created: Aug. 20, 2025
 * Updated: Aug. 20, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "sender.h"

#include <chrono>
#include <fstream>
#include <thread>

TraceSender::TraceSender(TraceBuffer &buffer) : buffer_(buffer), running_(false) {}

TraceSender::~TraceSender() { stop(); }

void TraceSender::start() {
    running_ = true;
    thread_ = std::thread(&TraceSender::run, this);
}

void TraceSender::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void TraceSender::run() {
    std::ofstream out("trace_output.json");
    out << "{\"traceEvents\":[\n";

    bool first = true;

    while (running_) {
        auto events = buffer_.pop_all();
        if (!events.empty()) {
            for (const auto &e : events) {
                if (!first)
                    out << ",\n";
                out << e.to_json();
                first = false;
            }
            out.flush();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    out << "\n]}\n";
    out.close();
}
