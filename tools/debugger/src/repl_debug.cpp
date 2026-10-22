// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * @file repl_debug.cpp
 * @brief Source-level debugging from the REPL of camel-db: breakpoints on lines and on
 *        functions, steps by line and by node, the stack, variables, and the `frames` switch
 *        (docs/technical/35_debugger.md, 7.4).
 *
 * Each command is a command of the debug core of the foreground task, sent through the
 * gateway's command layer (the "debug" command, which echoes it and forwards it to the task:
 * PRODUCT_PRINCIPLES.md 2.1). A command that lets the program run (run, continue, the steps)
 * then waits for the next stop or the end of the run, and prints on the way what the program
 * printed (the task's log), the notes of the core and the breakpoints whose answer changed.
 * Ctrl+C ends the wait, not the program.
 */

#include "camel/utils/windows_parser_guard.h"

// nlohmann MUST be before any header that pulls in ANTLR (EOF conflict)
#include "nlohmann/json.hpp"

#include "command/dispatcher.h"
#include "repl.h"
#include "state.h"

#include <httplib.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace debugger {

namespace {

using json   = nlohmann::json;
namespace fs = std::filesystem;

/// The breakpoints the user set in this REPL: the core replaces the breakpoints of a file, or
/// all function breakpoints, at once, so the REPL keeps the whole set.
struct ReplBreakpoints {
    std::map<std::string, std::set<int>> lines; // by path
    std::vector<std::string> functions;
};

ReplBreakpoints &breakpoints() {
    static ReplBreakpoints set;
    return set;
}

/// The frames of the last stack shown, innermost first: `locals <n>` names one by its number.
std::vector<json> &shownFrames() {
    static std::vector<json> frames;
    return frames;
}

std::string trim(const std::string &s) {
    const size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    return s.substr(start, s.find_last_not_of(" \t\r\n") - start + 1);
}

std::string fileName(const std::string &path) { return fs::path(path).filename().string(); }

/// One command of the foreground task's debug core. Prints why and returns nullopt when it
/// cannot be served.
std::optional<json> core(const std::string &command, json args = json::object()) {
    const std::string target = getForegroundTaskId();
    if (target.empty()) {
        std::cout << "No task: load a file first (file <path>)." << std::endl;
        return std::nullopt;
    }
    args["command"]       = command;
    args["target"]        = target;
    const CommandResult r = getDispatcher().dispatch("debug", args.dump());
    if (!r.success) {
        return std::nullopt; // the dispatcher's hook has printed the message
    }
    json answer = json::parse(r.body, nullptr, false);
    if (!answer.is_object()) {
        std::cout << "The task gave no usable answer." << std::endl;
        return std::nullopt;
    }
    if (!answer.value("ok", false)) {
        std::cout << answer.value("error", std::string("The command failed.")) << std::endl;
        return std::nullopt;
    }
    return answer;
}

std::string breakpointLine(const json &bp) {
    std::string out = "Breakpoint " + std::to_string(bp.value("id", 0)) + " at ";
    if (bp.contains("path")) {
        out += fileName(bp.value("path", "")) + ":" + std::to_string(bp.value("line", 0));
    } else {
        out += bp.value("function", bp.value("name", std::string()));
    }
    if (!bp.value("verified", false)) {
        out += " (not verified: " + bp.value("message", std::string("unknown")) + ")";
    }
    return out;
}

/// "<name>  <file>:<line>" of a frame of the stack.
std::string frameLine(const json &f) {
    std::string out = f.value("name", std::string());
    if (f.contains("line")) {
        out += "  " + fileName(f.value("path", "")) + ":" + std::to_string(f.value("line", 0)) +
               ":" + std::to_string(f.value("column", 0));
    }
    return out;
}

/// What the program printed since `offset` (its task log), advancing `offset`.
void printLog(int port, size_t &offset) {
    std::vector<std::string> lines;
    size_t next = offset;
    getTaskLogLines(port, offset, lines, next);
    for (const auto &line : lines) {
        std::cout << line << std::endl;
    }
    offset = next;
}

size_t logEnd(int port) {
    std::vector<std::string> lines;
    size_t next = 0;
    getTaskLogLines(port, 0, lines, next);
    return next;
}

void printStop(const json &stop) {
    const json frame = stop.value("frame", json::object());
    std::cout << "Stopped (" << stop.value("reason", std::string()) << ") in " << frameLine(frame)
              << std::endl;
    if (auto ex = stop.find("exception"); ex != stop.end()) {
        std::cout << ex->value("text", ex->value("message", std::string())) << std::endl;
    }
    // The lines the step went past that did not execute, and why (4.2).
    for (const json &line : stop.value("skipped", json::array())) {
        std::cout << "  " << line.value("text", std::string()) << std::endl;
    }
}

/// Waits for the next stop or the end of the run after event `after`.
void waitForStop(int port, uint64_t after, size_t logOffset) {
    for (;;) {
        if (isInterrupted()) {
            setInterrupted(false);
            std::cout << "(no longer waiting; the program goes on)" << std::endl;
            return;
        }
        httplib::Client client("127.0.0.1", port);
        client.set_connection_timeout(2, 0);
        client.set_read_timeout(5, 0);
        auto r = client.Get("/api/debug/events?timeout=300&after=" + std::to_string(after));
        printLog(port, logOffset);
        if (!r) {
            std::cout << "The program's process has ended." << std::endl;
            return;
        }
        const json answer = json::parse(r->body, nullptr, false);
        if (!answer.is_object()) {
            continue;
        }
        for (const json &e : answer.value("events", json::array())) {
            after                  = std::max(after, e.value("seq", uint64_t{0}));
            const std::string type = e.value("type", "");
            if (type == "breakpoint") {
                std::cout << breakpointLine(e.value("breakpoint", json::object())) << std::endl;
            } else if (type == "note") {
                std::cout << e.value("text", std::string()) << std::endl;
            } else if (type == "started") {
                if (const std::string notice = e.value("notice", ""); !notice.empty()) {
                    std::cout << notice << std::endl;
                }
            } else if (type == "stopped") {
                printStop(e);
                return;
            } else if (type == "finished") {
                // The output reaches the log through a pipe: give it a moment to arrive.
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                printLog(port, logOffset);
                std::cout << "Program finished: " << e.value("outcome", std::string());
                if (e.contains("exitCode") && e["exitCode"].is_number_integer()) {
                    std::cout << " (exit code " << e["exitCode"].get<int>() << ")";
                }
                std::cout << "." << std::endl;
                return;
            }
        }
    }
}

/// Runs `resume` (a command that lets the program go on), then waits for where it stops.
template <typename Resume> void resumeAndWait(Resume &&resume) {
    const int port = resolveTargetToPort(getForegroundTaskId());
    if (port <= 0) {
        std::cout << "No task: load a file first (file <path>)." << std::endl;
        return;
    }
    const auto status   = core("status");
    const uint64_t last = status ? status->value("lastEvent", uint64_t{0}) : 0;
    const size_t offset = logEnd(port);
    if (resume()) {
        waitForStop(port, last, offset);
    }
}

/// A program that has not been compiled cannot answer breakpoints: compile it (once).
void ensureCompiled() {
    if (auto status = core("status"); status && status->value("state", "") == "idle") {
        core("load", json{{"quiet", false}});
    }
}

void setBreakpoint(const std::string &arg) {
    if (arg.empty()) {
        std::cout << "Usage: break <file>:<line> | <line> | <function>" << std::endl;
        return;
    }
    ensureCompiled();
    // file:line, a line of the loaded file, or a function by its full name.
    const size_t colon = arg.rfind(':');
    const bool isLine  = std::all_of(arg.begin(), arg.end(), ::isdigit);
    const bool isFileLine =
        colon != std::string::npos && colon + 1 < arg.size() &&
        (colon == 0 || arg[colon - 1] != ':') &&
        std::all_of(arg.begin() + static_cast<long>(colon) + 1, arg.end(), ::isdigit);
    if (isLine || isFileLine) {
        std::string path = isLine ? getState().targetFile : arg.substr(0, colon);
        const int line   = std::stoi(isLine ? arg : arg.substr(colon + 1));
        std::error_code ec;
        if (fs::path(path).is_relative()) {
            // Relative to the loaded file first, then to the working directory.
            const fs::path beside = fs::path(getState().targetFile).parent_path() / path;
            path = fs::exists(beside, ec) ? beside.string() : fs::absolute(path, ec).string();
        }
        auto &lines = breakpoints().lines[path];
        lines.insert(line);
        json args{{"path", path}, {"lines", json(std::vector<int>(lines.begin(), lines.end()))}};
        if (auto answer = core("setBreakpoints", args)) {
            for (const json &bp : answer->value("breakpoints", json::array())) {
                if (bp.value("line", 0) == line) {
                    std::cout << breakpointLine(bp) << std::endl;
                }
            }
        }
        return;
    }
    auto &functions = breakpoints().functions;
    if (std::find(functions.begin(), functions.end(), arg) == functions.end()) {
        functions.push_back(arg);
    }
    if (auto answer = core("setFunctionBreakpoints", json{{"names", functions}})) {
        for (const json &bp : answer->value("breakpoints", json::array())) {
            if (bp.value("name", "") == arg) {
                std::cout << breakpointLine(bp) << std::endl;
            }
        }
    }
}

void deleteBreakpoints() {
    auto &set = breakpoints();
    for (const auto &[path, lines] : set.lines) {
        core("setBreakpoints", json{{"path", path}, {"lines", json::array()}});
    }
    if (!set.functions.empty()) {
        core("setFunctionBreakpoints", json{{"names", json::array()}});
    }
    set = ReplBreakpoints{};
    std::cout << "All breakpoints deleted." << std::endl;
}

void step(const char *unit, const char *mode) {
    resumeAndWait([&] { return core("step", json{{"unit", unit}, {"mode", mode}}).has_value(); });
}

void backtrace() {
    auto answer = core("stackTrace");
    if (!answer) {
        return;
    }
    shownFrames().clear();
    size_t i = 0;
    for (const json &f : answer->value("frames", json::array())) {
        std::cout << "#" << i++ << "  " << frameLine(f) << std::endl;
        shownFrames().push_back(f);
    }
}

void printVariables(int reference, const std::string &indent) {
    auto answer = core("variables", json{{"reference", reference}});
    if (!answer) {
        return;
    }
    for (const json &v : answer->value("variables", json::array())) {
        std::cout << indent << v.value("name", "") << ": " << v.value("type", "") << " = "
                  << v.value("value", "") << std::endl;
    }
}

void locals(const std::string &arg) {
    uint64_t frame = 0; // the top frame
    if (!arg.empty()) {
        if (shownFrames().empty()) {
            backtrace();
        }
        const size_t n = static_cast<size_t>(std::atoi(arg.c_str()));
        if (n >= shownFrames().size()) {
            std::cout << "No frame #" << arg << " (bt lists them)." << std::endl;
            return;
        }
        frame = shownFrames()[n].value("id", uint64_t{0});
    }
    auto scopes = core("scopes", json{{"frame", frame}});
    if (!scopes) {
        return;
    }
    for (const json &s : scopes->value("scopes", json::array())) {
        if (s.value("name", "") == "Nodes") {
            continue; // the graph view: `nodes` would list them
        }
        std::cout << "[" << s.value("name", "") << "]" << std::endl;
        printVariables(s.value("reference", 0), "  ");
    }
}

void print(const std::string &expression) {
    if (expression.empty()) {
        std::cout << "Usage: print <name>[.member|[index]...]" << std::endl;
        return;
    }
    if (auto answer = core("evaluate", json{{"frame", 0}, {"expression", expression}})) {
        std::cout << expression << ": " << answer->value("type", "") << " = "
                  << answer->value("result", "") << std::endl;
    }
}

void frames(const std::string &arg) {
    if (arg.empty()) {
        if (auto status = core("status")) {
            std::cout << "Frames: " << status->value("frames", "") << std::endl;
        }
        return;
    }
    if (arg != "functions" && arg != "subgraphs") {
        std::cout << "Usage: frames [functions|subgraphs]" << std::endl;
        return;
    }
    if (core("configure", json{{"frames", arg}})) {
        std::cout << (arg == "functions"
                          ? "Frames: functions (the arms of a branch are part of the frame of "
                            "their function)."
                          : "Frames: subgraphs (every subgraph that runs is a frame).")
                  << std::endl;
    }
}

} // namespace

void printDebugReplHelp(std::ostream &os) {
    os << R"(Source-level debugging (the foreground task; docs/debugging.en.md):
  break <file>:<line> | <line> | <function>  (b)  Set a breakpoint (a function by its full name,
                                                  or without the module when only one has it)
  delete            Delete all breakpoints
  run [pass ...]    (r)  Run the loaded file (with a pass sequence) and wait for a stop
  continue          (c)  Continue and wait for the next stop
  next / step / finish   (n)  Step over / into / out, by source line in execution order
  nexti / stepi     (ni, si)  Step over / into by graph node
  pause             Stop the running program at its next node
  bt                (backtrace, where)  The call stack
  locals [n]        The variables of frame n of bt (default: the top frame)
  print <name>      (p)  The value of a name, with member accesses (p.x, t.0, xs[2])
  frames [functions|subgraphs]  Show the arms of branches folded into the frame of their
                    function (default) or as the subgraphs they are
)";
}

bool handleDebugReplCommand(const std::string &cmd, const std::string &rawArg) {
    const std::string arg = trim(rawArg);
    if (cmd == "break" || cmd == "b") {
        setBreakpoint(arg);
    } else if (cmd == "delete") {
        deleteBreakpoints();
    } else if (cmd == "run" || cmd == "r") {
        // The run of the gateway (launch), with the passes given; then the first stop.
        json args = json::object();
        if (!arg.empty()) {
            json passes = json::array();
            for (size_t i = 0; i < arg.size();) {
                const size_t end = arg.find(' ', i);
                const std::string word =
                    arg.substr(i, end == std::string::npos ? std::string::npos : end - i);
                if (!word.empty()) {
                    passes.push_back(word);
                }
                if (end == std::string::npos) {
                    break;
                }
                i = end + 1;
            }
            args["passes"] = passes;
        }
        const std::string target = getForegroundTaskId();
        if (!target.empty()) {
            args["target"] = target;
        }
        resumeAndWait([&] { return getDispatcher().dispatch("launch", args.dump()).success; });
    } else if (cmd == "continue" || cmd == "c") {
        resumeAndWait([&] {
            const std::string target = getForegroundTaskId();
            return getDispatcher().dispatch("continue", json{{"target", target}}.dump()).success;
        });
    } else if (cmd == "next" || cmd == "n") {
        step("line", "over");
    } else if (cmd == "step") {
        step("line", "in");
    } else if (cmd == "finish" || cmd == "out") {
        step("line", "out");
    } else if (cmd == "nexti" || cmd == "ni") {
        step("node", "over");
    } else if (cmd == "stepi" || cmd == "si") {
        step("node", "in");
    } else if (cmd == "pause") {
        core("pause");
    } else if (cmd == "bt" || cmd == "backtrace" || cmd == "where") {
        backtrace();
    } else if (cmd == "locals") {
        locals(arg);
    } else if (cmd == "print" || cmd == "p") {
        print(arg);
    } else if (cmd == "frames") {
        frames(arg);
    } else {
        return false;
    }
    return true;
}

} // namespace debugger
