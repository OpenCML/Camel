// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Coverage statistics of the bytecode VM and its JIT, off unless the environment variable
// std::fvm(stats="file") names a file. One JSON object is appended to the file for each run:
//
//   {"jit": true, "graphs": [{"name": ..., "pc": ..., "ops": ..., "executed": ...,
//     "status": "compiled" | "compilable" | "rejected" | "interpreter",
//     "mode": "frameless" | "framed" | "",
//     "reason": ..., "refSlots": ..., "blockers": [{"kind": ..., "detail": ..., "count": ...}],
//     "runtime": [{"kind": ..., "detail": ..., "count": ...}]}]}
//
// - `ops` is the number of bytecodes of the graph; `executed` how many bytecodes of it the
//   interpreter ran. Bytecodes run by compiled code are not counted, nor are the calls its
//   driver makes for it: the share of a program that ran outside the interpreter is
//   1 - executed(std::jit) / executed(std::fvm) for the same pipeline.
// - `status`: under std::jit, `compiled` (compiled and entered), `compilable` (the back end
//   compiles it, but it never became hot) or `rejected` with the reason of the compile log and
//   every construct that blocks it; under std::fvm always `interpreter`.
// - `mode`: which of its two kinds of code the back end makes of the graph (x64::JitExitKind);
//   `runtime` lists the constructs that need the runtime, i.e. why it is framed code. Before
//   framed code existed these were the constructs that kept a graph from being compiled.
// - `refSlots`: how many slots of the graph's frame hold references the collector traces.
//
// test/tools/jitcov/jitcov.py runs programs with it and tabulates the result.

#include "fastvm.h"

#include "camel/execute/executor.h"
#include "camel/utils/path.h"

#include <cstdlib>
#include <fstream>
#include <map>

using namespace camel::core::context;

struct FastVMSchedPass::Stats {
    struct Blocker {
        std::string kind;
        std::string detail;
        size_t count = 0;
    };
    struct GraphRecord {
        std::string name;
        size_t pc       = 0;
        size_t length   = 0;
        size_t ops      = 0;
        size_t refSlots = 0;
        bool compilable = false;
        bool compiled   = false;
        std::string reason;
        std::vector<Blocker> blockers;
        std::vector<Blocker> runtime; // constructs that make it framed code
    };
    std::string path;
    // Indexed by bytecode pc; sized to the capacity of the bytecode vector, which never moves.
    std::vector<uint64_t> counts;
    std::vector<GraphRecord> graphs;
};

namespace {

std::string jsonString(const std::string &text) {
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
                out += ' ';
            else
                out += c;
        }
    }
    return out + "\"";
}

} // namespace

void FastVMSchedPass::statsBegin() {
    if (statsPath_.empty()) {
        return;
    }
    stats_       = std::make_shared<Stats>();
    stats_->path = statsPath_;
    stats_->counts.assign(bytecodes_.capacity(), 0);
    opCounts_ = stats_->counts.data();
    for (const auto &[graph, pc] : offsetMap_) {
        if (graph) {
            statsNoteGraph(graph, pc);
        }
    }
}

void FastVMSchedPass::statsNoteGraph(camel::runtime::GCGraph *graph, size_t pc) {
    if (!stats_) {
        return;
    }
    Stats::GraphRecord record;
    record.name   = graph->name();
    record.pc     = pc;
    record.length = graphLengthOf(graph);
    for (size_t p = pc; p < pc + record.length && p < bytecodes_.size();
         p += bytecodes_[p].opsize) {
        ++record.ops;
    }
    if (graph->hasFrameLayout()) {
        record.refSlots = graph->runtimeDataType()->refCount();
    }
#if ENABLE_FASTVM_JIT
    if (jitBackend_) {
        const jit::CompilationUnit unit = jitUnitFor(graph, pc, nullptr);
        auto grouped = [&](const std::vector<jit::JitBlocker> &list, std::string *firstText) {
            std::map<std::pair<std::string, std::string>, size_t> byKind;
            for (const jit::JitBlocker &blocker : list) {
                std::string detail;
                if (!blocker.detail.empty()) {
                    // The construct itself says how it was compiled (a loop's rounds: native
                    // code, or the helper and why).
                    detail = blocker.detail;
                } else if (blocker.kind == "OPER") {
                    detail = context_->execMgr().getNameOfAnOperator(
                        bytecodes_[blocker.pc].extra()->func);
                } else if (blocker.kind == "SCHD") {
                    detail = to_string(bytecodes_[blocker.pc].extra()->mark);
                }
                if (firstText != nullptr && firstText->empty()) {
                    *firstText = blocker.text;
                }
                ++byKind[{blocker.kind, detail}];
            }
            std::vector<Stats::Blocker> out;
            for (const auto &[key, count] : byKind) {
                out.push_back({key.first, key.second, count});
            }
            return out;
        };
        record.blockers = grouped(jitBackend_->blockers(unit), &record.reason);
        record.runtime  = grouped(jitBackend_->runtimeConstructs(unit), nullptr);
        if (record.blockers.empty()) {
            // Nothing the back end lists: what the compiler itself says is the answer.
            std::string failure;
            record.compilable = jitBackend_->compile(unit, &failure) != nullptr;
            if (!record.compilable) {
                record.reason = failure;
                record.blockers.push_back({"other", failure, 1});
            }
        }
    }
#endif
    stats_->graphs.push_back(std::move(record));
}

void FastVMSchedPass::statsNoteCompiled(size_t pc) {
    if (!stats_) {
        return;
    }
    for (auto &record : stats_->graphs) {
        if (record.pc == pc) {
            record.compiled = true;
        }
    }
}

void FastVMSchedPass::statsWrite() {
    if (!stats_) {
        return;
    }
    bool jit = false;
#if ENABLE_FASTVM_JIT
    jit = jitBackend_ != nullptr;
#endif
    std::string out = std::string("{\"jit\": ") + (jit ? "true" : "false") + ", \"graphs\": [";
    bool first      = true;
    for (const auto &record : stats_->graphs) {
        uint64_t executed = 0;
        for (size_t p = record.pc; p < record.pc + record.length && p < stats_->counts.size();
             ++p) {
            executed += stats_->counts[p];
        }
        const char *status = !jit                ? "interpreter"
                             : record.compiled   ? "compiled"
                             : record.compilable ? "compilable"
                                                 : "rejected";
        out += first ? "" : ", ";
        first            = false;
        const char *mode = !jit || !(record.compiled || record.compilable) ? ""
                           : record.runtime.empty()                        ? "frameless"
                                                                           : "framed";
        out += std::format(
            "{{\"name\": {}, \"pc\": {}, \"ops\": {}, \"executed\": {}, \"status\": \"{}\", "
            "\"mode\": \"{}\", \"refSlots\": {}, \"reason\": {}, ",
            jsonString(record.name),
            record.pc,
            record.ops,
            executed,
            status,
            mode,
            record.refSlots,
            jsonString(record.reason));
        auto listOf = [&](const char *key, const std::vector<Stats::Blocker> &list) {
            out += std::format("\"{}\": [", key);
            for (size_t i = 0; i < list.size(); ++i) {
                out += std::format(
                    "{}{{\"kind\": {}, \"detail\": {}, \"count\": {}}}",
                    i == 0 ? "" : ", ",
                    jsonString(list[i].kind),
                    jsonString(list[i].detail),
                    list[i].count);
            }
            out += "]";
        };
        listOf("blockers", record.blockers);
        out += ", ";
        listOf("runtime", record.runtime);
        out += "}";
    }
    out += "]}\n";
    std::ofstream file(camel::utils::pathFromUtf8(stats_->path), std::ios::app);
    file << out;
    opCounts_ = nullptr;
    stats_.reset();
}
