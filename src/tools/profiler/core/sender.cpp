// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "sender.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>

using json = nlohmann::json;

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
    std::filesystem::create_directories("profile_reports");
    std::ofstream out("profile_reports/trace_output.json");
    out << "{\"traceEvents\":[\n";

    bool first = true;

    while (running_) {
        auto events = buffer_.pop_all();
        if (!events.empty()) {
            for (const auto &e : events) {
                if (!first) {
                    out << ",\n";
                }
                first = false;

                nlohmann::json json_event;
                json_event["name"] = e.name;
                json_event["cat"] = e.cat;
                json_event["ph"] = e.ph;
                json_event["ts"] = e.ts;
                json_event["pid"] = e.pid;
                json_event["tid"] = e.tid;

                if (!e.args.empty()) {
                    try {
                        json_event["args"] = e.args;
                    } catch (...) {
                        json_event["args"] = e.args;
                    }
                }

                out << json_event.dump();
            }
            out.flush();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    out << "\n]}\n";
    out.close();
}
