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
 * Created: Oct. 12, 2026
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The Debug Adapter Protocol front end of camel-db (docs/technical/35_debugger.md, 7.3).
 *
 * Threads:
 *  - the main thread reads requests from stdin and answers them, one at a time;
 *  - the event pump long-polls the worker's event list and sends DAP events;
 *  - the reader threads of the worker process (child_process.h) send `output` events and, when the
 *    worker has exited and its streams are at their end, `exited` and `terminated`.
 * Everything written to stdout goes through Adapter::send, which serialises the writers.
 *
 * The end of a session has one path: the worker process exits (because the adapter killed it
 * after the program finished, because a request asked for it, or on its own), its remaining
 * output is forwarded, then `exited` and `terminated` are sent. So the output of a program
 * always precedes the events that say it ended.
 */

#include "adapter.h"

#include "nlohmann/json.hpp"
#include "child_process.h"

#include <httplib.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <utility>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

namespace debugger::dap {

namespace fs = std::filesystem;
using json   = nlohmann::json;

namespace {

constexpr int kThreadId = 1;
/// Exit codes reported for a program that did not run to its end: a failed run exits as the
/// `camel` command does; one ended by the session, as a process ended by SIGTERM would.
constexpr int kExitFailed     = 1;
constexpr int kExitTerminated = 143;

/// Bytes of a stream as text for JSON: an incomplete UTF-8 sequence at the end of a chunk
/// waits for the next chunk.
class Utf8Stream {
  public:
    std::string feed(std::string_view chunk) {
        pending_.append(chunk);
        size_t cut = pending_.size();
        // Step back over at most three continuation bytes to the start of the last sequence.
        size_t i = cut;
        int back = 0;
        while (i > 0 && back < 4) {
            const unsigned char c = static_cast<unsigned char>(pending_[i - 1]);
            --i;
            ++back;
            if ((c & 0xC0) == 0x80) {
                continue;
            }
            const int need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
            if (need > back) {
                cut = i; // the sequence is not complete yet
            }
            break;
        }
        std::string out = pending_.substr(0, cut);
        pending_.erase(0, cut);
        return out;
    }
    std::string flush() { return std::exchange(pending_, std::string()); }

  private:
    std::string pending_;
};

class Adapter {
  public:
    explicit Adapter(std::string exePath) : exePath_(std::move(exePath)) {}

    int run() {
        for (;;) {
            std::optional<std::string> body = readMessage();
            if (!body) {
                break; // the client closed the connection
            }
            json message = json::parse(*body, nullptr, false);
            if (message.is_discarded() || !message.is_object() ||
                message.value("type", "") != "request" || !message.contains("command") ||
                !message["command"].is_string()) {
                // Not a request: there is nothing to answer by number, so say it by event.
                respond(
                    message.is_object() && message.contains("seq") &&
                            message["seq"].is_number_integer()
                        ? message["seq"].get<int64_t>()
                        : 0,
                    message.is_object() && message.contains("command") &&
                            message["command"].is_string()
                        ? message["command"].get<std::string>()
                        : std::string(),
                    false,
                    json::object(),
                    "Malformed message: a request is a JSON object with \"type\": \"request\" "
                    "and a \"command\".");
                continue;
            }
            const std::string command = message["command"].get<std::string>();
            const int64_t seq         = message.value("seq", int64_t{0});
            json args                 = json::object();
            if (auto it = message.find("arguments"); it != message.end() && it->is_object()) {
                args = *it;
            }
            bool quit = false;
            try {
                quit = handle(command, seq, args);
            } catch (const std::exception &e) {
                respond(seq, command, false, json::object(), e.what());
            }
            if (quit) {
                break;
            }
        }
        shutdown();
        return 0;
    }

  private:
    // ---- the wire -------------------------------------------------------------------------

    std::optional<std::string> readMessage() {
        for (;;) {
            size_t length   = 0;
            bool haveLength = false;
            bool sawHeader  = false;
            for (;;) {
                std::string line;
                int c;
                while ((c = std::fgetc(stdin)) != EOF && c != '\n') {
                    line += static_cast<char>(c);
                }
                if (c == EOF) {
                    return std::nullopt;
                }
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (line.empty()) {
                    if (sawHeader) {
                        break;
                    }
                    continue; // blank lines between messages
                }
                sawHeader = true;
                static const std::string key = "content-length:";
                std::string lower;
                for (char ch : line) {
                    lower += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                }
                if (lower.rfind(key, 0) == 0) {
                    length     = std::strtoull(line.c_str() + key.size(), nullptr, 10);
                    haveLength = true;
                }
            }
            if (!haveLength || length > (64u << 20)) {
                respond(0, "", false, json::object(), "Malformed header: no usable Content-Length.");
                continue;
            }
            std::string body(length, '\0');
            if (std::fread(body.data(), 1, length, stdin) != length) {
                return std::nullopt;
            }
            return body;
        }
    }

    void send(json message) {
        std::lock_guard<std::mutex> lock(writeMutex_);
        message["seq"] = nextSeq_++;
        // Text that is not UTF-8 (a program may print any bytes) is replaced, never thrown at.
        const std::string body = message.dump(-1, ' ', false, json::error_handler_t::replace);
        std::fprintf(stdout, "Content-Length: %zu\r\n\r\n", body.size());
        std::fwrite(body.data(), 1, body.size(), stdout);
        std::fflush(stdout);
    }

    void respond(
        int64_t requestSeq, const std::string &command, bool success, json body,
        const std::string &message = std::string()) {
        json r{
            {"type", "response"},
            {"request_seq", requestSeq},
            {"command", command},
            {"success", success}};
        if (!success) {
            r["message"] = message;
            r["body"]    = json{
                {"error", json{{"id", 1}, {"format", message}, {"showUser", true}}}};
        } else {
            r["body"] = std::move(body);
        }
        send(std::move(r));
    }

    void event(const std::string &name, json body = json::object()) {
        send(json{{"type", "event"}, {"event", name}, {"body", std::move(body)}});
    }

    void output(const std::string &category, const std::string &text) {
        if (!text.empty()) {
            event("output", json{{"category", category}, {"output", text}});
        }
    }

    // ---- the worker -----------------------------------------------------------------------

    /// One command of the worker's debug core. A worker that cannot be reached answers
    /// `{"ok": false}` like a command that failed.
    json core(const std::string &command, const json &args = json::object(), int timeoutSec = 30) {
        if (port_ == 0) {
            return json{{"ok", false}, {"error", "No program is being debugged: launch first."}};
        }
        httplib::Client client("127.0.0.1", port_);
        client.set_connection_timeout(5, 0);
        client.set_read_timeout(timeoutSec, 0);
        auto r = client.Post("/api/debug/" + command, args.dump(), "application/json");
        if (!r) {
            return json{{"ok", false}, {"error", "The program being debugged has ended."}};
        }
        json answer = json::parse(r->body, nullptr, false);
        if (!answer.is_object()) {
            return json{{"ok", false}, {"error", "The debuggee gave no usable answer."}};
        }
        return answer;
    }

    static std::string errorOf(const json &answer) {
        return answer.value("error", std::string("the request failed"));
    }

    /// Ends the worker; its exit callback reports the end of the session.
    void killWorker() {
        std::shared_ptr<ChildProcess> worker;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            worker = worker_;
        }
        if (worker) {
            worker->kill();
        }
    }

    void onWorkerExit(int exitCode, int signal) {
        output("stdout", stdoutText_.flush());
        output("stderr", stderrText_.flush());
        workerGone_.store(true);
        int code;
        bool expected;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            expected = programExit_.has_value();
            code     = programExit_.value_or(signal != 0 ? 128 + signal : exitCode);
        }
        if (quiet_.load()) {
            return; // the client asked to disconnect: it knows
        }
        if (!expected) {
            std::ostringstream text;
            text << "The debuggee process ended unexpectedly";
            if (signal != 0) {
                text << " (signal " << signal << ")";
            } else {
                text << " (exit code " << exitCode << ")";
            }
            text << ".\n";
            output("console", text.str());
        }
        event("exited", json{{"exitCode", code}});
        event("terminated");
    }

    // ---- events of the core ---------------------------------------------------------------

    void pump() {
        uint64_t seen = 0;
        while (!workerGone_.load()) {
            httplib::Client client("127.0.0.1", port_);
            client.set_connection_timeout(2, 0);
            client.set_read_timeout(10, 0);
            auto r = client.Get(
                ("/api/debug/events?timeout=4000&after=" + std::to_string(seen)).c_str());
            if (!r) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            json answer = json::parse(r->body, nullptr, false);
            if (!answer.is_object() || !answer.contains("events")) {
                continue;
            }
            for (const json &e : answer["events"]) {
                seen = std::max(seen, e.value("seq", uint64_t{0}));
                if (coreEvent(e)) {
                    return;
                }
            }
        }
    }

    /// Returns true when the run is over and the pump has nothing more to wait for.
    bool coreEvent(const json &e) {
        const std::string type = e.value("type", "");
        if (type == "stopped") {
            const std::string reason = e.value("reason", "");
            json body{
                {"reason", reason},
                {"threadId", kThreadId},
                {"allThreadsStopped", true}};
            if (auto ids = e.find("breakpointIds"); ids != e.end() && ids->is_array()) {
                body["hitBreakpointIds"] = *ids;
            }
            if (reason == "exception") {
                const json info     = e.value("exception", json::object());
                body["description"] = info.value("isolated", false)
                                          ? "Fault (isolated by attempt)"
                                          : "Fault";
                body["text"]        = info.value("message", "");
            } else if (e.contains("nodeBreakpoint")) {
                body["description"] = "Paused on a node breakpoint";
            }
            // The graph-node view of the stop, for clients that know Camel.
            json camel{{"reason", reason}, {"threadId", kThreadId}};
            if (auto f = e.find("frame"); f != e.end()) {
                camel["frame"] = *f;
            }
            event("camel/stopped", std::move(camel));
            event("stopped", std::move(body));
        } else if (type == "continued") {
            event("continued", json{{"threadId", kThreadId}, {"allThreadsContinued", true}});
        } else if (type == "breakpoint") {
            event(
                "breakpoint",
                json{
                    {"reason", "changed"},
                    {"breakpoint", breakpointOf(e.value("breakpoint", json::object()))}});
        } else if (type == "started") {
            const std::string notice = e.value("notice", "");
            if (!notice.empty()) {
                output("console", notice + "\n");
            }
        } else if (type == "finished") {
            const std::string outcome = e.value("outcome", "");
            int code                  = kExitFailed;
            if (outcome == "completed") {
                code = e.contains("exitCode") && e["exitCode"].is_number_integer()
                           ? e["exitCode"].get<int>()
                           : 0;
            } else if (outcome == "terminated") {
                code = kExitTerminated;
            }
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                programExit_ = code;
            }
            // The worker has flushed the program's output before it reported the end; what is
            // still in the pipes is forwarded before the exit callback runs.
            killWorker();
            return true;
        }
        return false;
    }

    json breakpointOf(const json &bp) const {
        json out{{"id", bp.value("id", 0)}, {"verified", bp.value("verified", false)}};
        if (bp.contains("line")) {
            out["line"] = bp.value("line", 0) - lineOffset_;
        }
        if (bp.contains("message")) {
            out["message"] = bp["message"];
            if (!bp.value("verified", false)) {
                out["reason"] = "failed";
            }
        }
        if (bp.contains("path")) {
            out["source"] = sourceOf(bp.value("path", ""));
        }
        return out;
    }

    static json sourceOf(const std::string &path) {
        return json{{"name", fs::path(path).filename().string()}, {"path", path}};
    }

    // ---- requests -------------------------------------------------------------------------

    /// Returns true when the session is over and the adapter exits.
    bool handle(const std::string &command, int64_t seq, const json &args) {
        const auto ok   = [&](json body = json::object()) { respond(seq, command, true, std::move(body)); };
        const auto fail = [&](const std::string &m) { respond(seq, command, false, json::object(), m); };
        /// The answer of a core command, or an error response and nullopt.
        const auto ask = [&](const std::string &name, const json &a) -> std::optional<json> {
            json answer = core(name, a);
            if (!answer.value("ok", false)) {
                fail(errorOf(answer));
                return std::nullopt;
            }
            return answer;
        };

        if (command == "initialize") {
            lineOffset_   = args.value("linesStartAt1", true) ? 0 : 1;
            columnOffset_ = args.value("columnsStartAt1", true) ? 0 : 1;
            ok(json{
                {"supportsConfigurationDoneRequest", true},
                {"supportsTerminateRequest", true},
                {"supportTerminateDebuggee", true},
                {"supportsSteppingGranularity", true},
                {"supportsExceptionInfoRequest", true},
                {"supportsEvaluateForHovers", true},
                {"exceptionBreakpointFilters",
                 json::array(
                     {json{
                          {"filter", "uncaught"},
                          {"label", "Faults that end the program"},
                          {"description",
                           "Stop where a fault (division by zero, index out of range, ...) is "
                           "raised that will end the program."},
                          {"default", true}},
                      json{
                          {"filter", "isolated"},
                          {"label", "Faults isolated by attempt"},
                          {"description",
                           "Stop where a fault is raised inside an attempt, which will become "
                           "an error value at its boundary."},
                          {"default", false}}})}});
            return false;
        }
        if (command == "launch") {
            launch(seq, args);
            return false;
        }
        if (command == "attach") {
            fail("attach is not supported: camel-db starts the program itself (use launch).");
            return false;
        }
        if (command == "disconnect") {
            quiet_.store(true);
            killWorker();
            ok();
            return true;
        }
        if (command == "threads") {
            ok(json{{"threads", json::array({json{{"id", kThreadId}, {"name", "main"}}})}});
            return false;
        }
        if (port_ == 0) {
            if (command == "setBreakpoints" || command == "setExceptionBreakpoints" ||
                command == "configurationDone") {
                fail(
                    command + " before launch: the adapter sends the initialized event when the "
                              "program is loaded and breakpoints can be set.");
            } else if (isKnown(command)) {
                fail("No program is being debugged: launch first.");
            } else {
                fail("Unsupported request: " + command);
            }
            return false;
        }

        if (command == "setBreakpoints") {
            const json source = args.value("source", json::object());
            if (!source.is_object() || !source.contains("path") || !source["path"].is_string()) {
                fail("setBreakpoints: source.path is required.");
                return false;
            }
            json lines = json::array();
            if (auto b = args.find("breakpoints"); b != args.end() && b->is_array()) {
                for (const json &bp : *b) {
                    if (bp.is_object() && bp.contains("line") && bp["line"].is_number_integer()) {
                        lines.push_back(bp["line"].get<int>() + lineOffset_);
                    }
                }
            } else if (auto l = args.find("lines"); l != args.end() && l->is_array()) {
                for (const json &line : *l) {
                    if (line.is_number_integer()) {
                        lines.push_back(line.get<int>() + lineOffset_);
                    }
                }
            }
            auto answer = ask("setBreakpoints", json{{"path", source["path"]}, {"lines", lines}});
            if (!answer) {
                return false;
            }
            json out = json::array();
            for (const json &bp : answer->value("breakpoints", json::array())) {
                out.push_back(breakpointOf(bp));
            }
            ok(json{{"breakpoints", std::move(out)}});
            return false;
        }
        if (command == "setExceptionBreakpoints") {
            if (ask("setExceptionFilters", json{{"filters", args.value("filters", json::array())}})) {
                ok();
            }
            return false;
        }
        if (command == "configurationDone") {
            bool already;
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                already  = started_;
                started_ = true;
            }
            if (already) {
                fail("configurationDone was already sent: the program is started once.");
            } else if (ask("start", json::object())) {
                ok();
            }
            return false;
        }
        if (command == "continue") {
            if (ask("continue", json::object())) {
                ok(json{{"allThreadsContinued", true}});
            }
            return false;
        }
        if (command == "next" || command == "stepIn" || command == "stepOut") {
            // Granularity "instruction" is a step by graph node; a statement or a line is a
            // step by source line (docs/technical/35_debugger.md, section 4). A session
            // launched with "stepBy": "node" steps by node whatever the client asks: editors
            // send the instruction granularity only from a disassembly view.
            const bool byNode =
                stepByNode_ || args.value("granularity", "statement") == "instruction";
            json step{
                {"unit", byNode ? "node" : "line"},
                {"mode", command == "next" ? "over" : command == "stepIn" ? "in" : "out"}};
            if (ask("step", step)) {
                ok();
            }
            return false;
        }
        if (command == "pause") {
            if (ask("pause", json::object())) {
                ok();
            }
            return false;
        }
        if (command == "stackTrace") {
            auto answer = ask("stackTrace", json::object());
            if (!answer) {
                return false;
            }
            const json all     = answer->value("frames", json::array());
            const size_t start = args.value("startFrame", size_t{0});
            size_t levels      = args.value("levels", size_t{0});
            if (levels == 0) {
                levels = all.size();
            }
            json frames = json::array();
            for (size_t i = start; i < all.size() && frames.size() < levels; ++i) {
                const json &f = all[i];
                json frame{{"id", f.value("id", uint64_t{0})}, {"name", f.value("name", "")}};
                if (f.contains("line")) {
                    frame["source"]    = sourceOf(f.value("path", ""));
                    frame["line"]      = f.value("line", 0) - lineOffset_;
                    frame["column"]    = f.value("column", 0) - columnOffset_;
                    frame["endLine"]   = f.value("endLine", 0) - lineOffset_;
                    frame["endColumn"] = f.value("endColumn", 0) - columnOffset_;
                } else {
                    // A frame without a source position: the entry graph, or a node no source
                    // text stands for.
                    frame["line"]             = 0;
                    frame["column"]           = 0;
                    frame["presentationHint"] = "subtle";
                }
                frames.push_back(std::move(frame));
            }
            ok(json{{"stackFrames", std::move(frames)}, {"totalFrames", all.size()}});
            return false;
        }
        if (command == "scopes") {
            auto answer = ask("scopes", json{{"frame", args.value("frameId", uint64_t{0})}});
            if (!answer) {
                return false;
            }
            json scopes = json::array();
            for (const json &s : answer->value("scopes", json::array())) {
                const std::string name = s.value("name", "");
                json scope{
                    {"name", name},
                    {"variablesReference", s.value("reference", 0)},
                    {"namedVariables", s.value("count", 0)},
                    {"expensive", s.value("expensive", false)}};
                if (name == "Locals") {
                    scope["presentationHint"] = "locals";
                }
                scopes.push_back(std::move(scope));
            }
            ok(json{{"scopes", std::move(scopes)}});
            return false;
        }
        if (command == "variables") {
            auto answer =
                ask("variables", json{{"reference", args.value("variablesReference", 0)}});
            if (!answer) {
                return false;
            }
            json variables = json::array();
            for (const json &v : answer->value("variables", json::array())) {
                json variable{
                    {"name", v.value("name", "")},
                    {"value", v.value("value", "")},
                    {"type", v.value("type", "")},
                    {"variablesReference", v.value("reference", 0)}};
                if (!v.value("evaluated", true)) {
                    // Shown, but there is no value to copy or to watch.
                    variable["presentationHint"] = json{{"attributes", json::array({"readOnly"})}};
                }
                variables.push_back(std::move(variable));
            }
            ok(json{{"variables", std::move(variables)}});
            return false;
        }
        if (command == "evaluate") {
            if (!args.contains("expression") || !args["expression"].is_string()) {
                fail("evaluate: expression is required.");
                return false;
            }
            auto answer = ask(
                "evaluate",
                json{
                    {"frame", args.value("frameId", uint64_t{0})},
                    {"expression", args["expression"]}});
            if (answer) {
                ok(json{
                    {"result", answer->value("result", "")},
                    {"type", answer->value("type", "")},
                    {"variablesReference", answer->value("reference", 0)}});
            }
            return false;
        }
        if (command == "exceptionInfo") {
            auto answer = ask("exceptionInfo", json::object());
            if (answer) {
                const json info = answer->value("exception", json::object());
                ok(json{
                    {"exceptionId", info.value("name", "Fault")},
                    {"description", info.value("message", "")},
                    {"breakMode", info.value("isolated", false) ? "always" : "unhandled"},
                    {"details",
                     json{
                         {"message", info.value("message", "")},
                         {"typeName", info.value("name", "Fault")},
                         {"stackTrace", info.value("text", "")}}}});
            }
            return false;
        }
        if (command == "terminate") {
            // The program ends at its next node; a program that reaches none (it waits in an
            // operator) is ended with its process after a grace period.
            core("terminate");
            ok();
            if (!terminator_.joinable()) {
                terminator_ = std::thread([this] {
                    for (int i = 0; i < 40 && !workerGone_.load(); ++i) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                    if (!workerGone_.load()) {
                        {
                            std::lock_guard<std::mutex> lock(stateMutex_);
                            if (!programExit_) {
                                programExit_ = kExitTerminated;
                            }
                        }
                        killWorker();
                    }
                });
            }
            return false;
        }
        if (command == "camel/graph") {
            auto answer = ask("graph", json{{"frame", args.value("frameId", uint64_t{0})}});
            if (answer) {
                answer->erase("ok");
                ok(std::move(*answer));
            }
            return false;
        }
        fail("Unsupported request: " + command);
        return false;
    }

    static bool isKnown(const std::string &command) {
        static const char *const known[] = {
            "continue",   "next",          "stepIn",      "stepOut",  "pause",       "stackTrace",
            "scopes",     "variables",     "evaluate",    "terminate", "exceptionInfo",
            "camel/graph"};
        for (const char *k : known) {
            if (command == k) {
                return true;
            }
        }
        return false;
    }

    void launch(int64_t seq, const json &args) {
        const auto fail = [&](const std::string &m) { respond(seq, "launch", false, json::object(), m); };
        if (port_ != 0) {
            fail("A program was already launched in this session.");
            return;
        }
        if (!args.contains("program") || !args["program"].is_string() ||
            args["program"].get<std::string>().empty()) {
            fail("launch: \"program\" (the path of a .cml file) is required.");
            return;
        }
        std::string cwd = args.value("cwd", "");
        std::error_code ec;
        fs::path program(args["program"].get<std::string>());
        if (program.is_relative()) {
            program = (cwd.empty() ? fs::current_path(ec) : fs::path(cwd)) / program;
        }
        program = program.lexically_normal();
        if (!fs::is_regular_file(program, ec)) {
            fail("launch: no such file: " + program.string());
            return;
        }

        // The pass sequence, as on the command line: an array of names, or one string.
        json passes = json::array();
        if (auto p = args.find("passes"); p != args.end()) {
            if (p->is_array()) {
                for (const json &name : *p) {
                    if (name.is_string() && !name.get<std::string>().empty()) {
                        passes.push_back(name);
                    }
                }
            } else if (p->is_string()) {
                std::istringstream words(p->get<std::string>());
                for (std::string word; words >> word;) {
                    passes.push_back(word);
                }
            } else {
                fail("launch: \"passes\" is an array of pass names or a string of them.");
                return;
            }
        }
        // Values for the parameters of main: ["name=value", ...] or {"name": value, ...}.
        json entryArgs = json::array();
        if (auto a = args.find("args"); a != args.end()) {
            if (a->is_array()) {
                for (const json &item : *a) {
                    if (item.is_string()) {
                        entryArgs.push_back(item);
                    }
                }
            } else if (a->is_object()) {
                for (auto it = a->begin(); it != a->end(); ++it) {
                    entryArgs.push_back(
                        it.key() + "=" + (it->is_string() ? it->get<std::string>() : it->dump()));
                }
            } else {
                fail("launch: \"args\" is an array of \"name=value\" or an object.");
                return;
            }
        }

        const std::string stepBy = args.value("stepBy", "line");
        if (stepBy != "line" && stepBy != "node") {
            fail("launch: \"stepBy\" is \"line\" or \"node\".");
            return;
        }
        stepByNode_ = stepBy == "node";

        ProcessSpec spec;
        spec.exe  = exePath_;
        spec.args = {"--run-worker", program.string()};
        spec.cwd  = cwd;
        const int port = pickFreeLocalPort();
        if (port == 0) {
            fail("launch: no free local port for the debuggee.");
            return;
        }
        spec.env.emplace_back("CAMEL_DB_WORKER_PORT", std::to_string(port));
        if (auto e = args.find("env"); e != args.end() && e->is_object()) {
            for (auto it = e->begin(); it != e->end(); ++it) {
                if (it->is_string()) {
                    spec.env.emplace_back(it.key(), it->get<std::string>());
                }
            }
        }
        ProcessCallbacks callbacks;
        callbacks.onStdout = [this](std::string_view chunk) { output("stdout", stdoutText_.feed(chunk)); };
        callbacks.onStderr = [this](std::string_view chunk) { output("stderr", stderrText_.feed(chunk)); };
        callbacks.onExit   = [this](int code, int signal) { onWorkerExit(code, signal); };
        std::string error;
        auto worker = ChildProcess::spawn(spec, std::move(callbacks), error);
        if (!worker) {
            fail("launch: the debuggee could not be started: " + error);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            worker_ = worker;
        }
        port_ = port;

        // The worker answers once its HTTP server listens.
        bool ready          = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (std::chrono::steady_clock::now() < deadline && !workerGone_.load()) {
            httplib::Client client("127.0.0.1", port_);
            client.set_connection_timeout(0, 200000);
            if (client.Get("/api/state")) {
                ready = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        const auto giveUp = [&](const std::string &m) {
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                if (!programExit_) {
                    programExit_ = kExitFailed;
                }
            }
            fail(m);
            killWorker();
        };
        if (!ready) {
            giveUp("launch: the debuggee did not start.");
            return;
        }
        // Compile first: breakpoints are answered against the compiled program before
        // anything runs.
        json answer = core(
            "load",
            json{
                {"passes", passes},
                {"args", entryArgs},
                {"quiet", true},
                {"stopOnEntry", args.value("stopOnEntry", false)}},
            320);
        if (!answer.value("ok", false)) {
            giveUp("launch: " + errorOf(answer));
            return;
        }
        if (!answer.value("compiled", false)) {
            giveUp("The program did not compile; its diagnostics are in the debug console.");
            return;
        }
        pump_ = std::thread([this] { pump(); });
        respond(seq, "launch", true, json::object());
        event("initialized");
    }

    void shutdown() {
        quiet_.store(true);
        std::shared_ptr<ChildProcess> worker;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            worker = worker_;
        }
        if (worker) {
            worker->kill();
            worker->waitExit(5000);
        }
        workerGone_.store(true);
        if (pump_.joinable()) {
            pump_.join();
        }
        if (terminator_.joinable()) {
            terminator_.join();
        }
        ChildProcess::terminateAll();
    }

    std::string exePath_;
    std::mutex writeMutex_;
    int64_t nextSeq_ = 1;
    int lineOffset_   = 0; // added to a client line to get a 1-based line
    int columnOffset_ = 0;
    bool stepByNode_  = false; // launch "stepBy": "node"

    std::mutex stateMutex_;
    std::shared_ptr<ChildProcess> worker_;
    std::optional<int> programExit_; // set when the end of the worker is expected
    bool started_ = false;
    int port_     = 0; // written by the main thread before the pump starts
    std::atomic<bool> workerGone_{false};
    std::atomic<bool> quiet_{false};
    std::thread pump_;
    std::thread terminator_; // ends a program that does not end by itself after terminate
    Utf8Stream stdoutText_;
    Utf8Stream stderrText_;
};

} // namespace

int runAdapter(const std::string &exePath) {
#if defined(_WIN32)
    // The protocol counts bytes: no newline translation on either stream.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    Adapter adapter(exePath);
    return adapter.run();
}

} // namespace debugger::dap
