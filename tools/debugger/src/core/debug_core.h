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
#pragma once

/**
 * @file debug_core.h
 * @brief The one place that controls a program being debugged
 *        (docs/technical/34_debugger.md, section 7).
 *
 * DebugCore lives in the process that runs the program (a camel-db worker). It is the
 * scheduler's ExecutionObserver: it keeps the stack of frames, decides at every node whether
 * to stop (breakpoints, steps, pause), stops by blocking the program's thread in the hook, and
 * answers questions about the stopped program on that thread. It knows no transport: the HTTP
 * API of the worker and, through it, the debug adapter are shells over command() and events().
 *
 * Threads: the program's thread calls the run life cycle and the observer hooks; any other
 * thread calls command() and events(). Commands that read the program (stack, variables,
 * graph) are executed by the program's thread while it is stopped, so nothing runs
 * concurrently with them and no collection can start under them.
 */

// First: ANTLR, which the compiler headers pull in, undefines EOF.
#include "nlohmann/json.hpp"

#include "program_index.h"
#include "values.h"

#include "camel/execute/observer.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace debugger::core {

/// Thrown out of a hook to end the run when the debugger was asked to (terminate, restart).
/// Deliberately not std::exception: the pass driver reports those as failures of a pass.
struct RunAborted {
    bool restart = false;
};

class DebugCore final : public camel::execute::ExecutionObserver {
  public:
    using json = nlohmann::json;

    static DebugCore &instance();

    // ---- life cycle of a run; called on the thread that compiles and runs the program ----

    /// Before the compilation: attaches the recorder to `ctx`.
    void beginCompile(const camel::core::context::context_ptr_t &ctx);
    /// After it. `root` is the graph before any pass; null when the program did not compile.
    void endCompile(camel::runtime::GCGraph *root);
    /// Before the passes run. `notice`: what the user is told about this run (a back end
    /// replaced by a debuggable one); empty when there is nothing to say.
    void beginRun(const std::string &notice);
    /// After the run: `outcome` is "completed", "failed" or "terminated".
    void endRun(const std::string &outcome, std::optional<int> exitCode);

    // ---- control; any thread ----

    /// Executes one command (see debug_core.cpp for the list). Never throws: an error is
    /// `{"ok": false, "error": "..."}`.
    json command(const std::string &name, const json &args);
    /// The events after sequence number `after`, waiting up to `timeoutMs` for the first.
    json events(uint64_t after, int timeoutMs);

    /// Called with the stop description when the program stops and with null when it
    /// resumes (the worker mirrors it into the state the Web UI polls).
    void setStopListener(std::function<void(const json *stop)> listener);

    // ---- ExecutionObserver ----
    void activationEntered() override;
    void activationLeft() override;
    void beforeNode(
        camel::runtime::GCGraph *graph, camel::core::context::Frame *frame,
        camel::runtime::gc_node_ref_t node) override;
    void tailCall() override;
    void beforeInstruction(const camel::execute::InstructionSite &site) override;
    void faultRaised(const camel::core::error::RuntimeFault &fault, bool isolated) override;

  private:
    DebugCore() = default;

    enum class State { Idle, Loaded, Running, Stopped, Finished };
    enum class StepKind { None, Node, Line, Out };
    enum class Pending { None, Enter, Tail };

    struct ShadowFrame {
        uint64_t id                         = 0;
        camel::runtime::GCGraph *graph      = nullptr;
        camel::core::context::Frame *frame  = nullptr;
        GraphInfo *info                     = nullptr;
        const NodeInfo *node                = nullptr; // about to run, running, or waited in
        origin_t origin                     = 0;       // of what is about to run
        SourceLoc loc;                                 // of that origin
        int lastFile                        = -1;      // the line the frame is on (2.1)
        int lastLine                        = 0;
        int lastFunction                    = -1;
        int pendingNode                     = -1; // index of the node whose result is due
        std::vector<bool> done;                   // by node index: has run in this activation
        uint32_t tailReplaced               = 0;
        camel::execute::InstructionEffect lastEffect = camel::execute::InstructionEffect::None;
    };

    struct Step {
        StepKind kind = StepKind::None;
        bool into     = false;
        size_t depth  = 0;
        uint64_t frame = 0;
        int file      = -1;
        int line      = 0;
        int function  = -1;
        bool foreign  = false; // started in code inlined from another function
        std::string graph;     // node steps of the HTTP API: only nodes of this graph
    };

    struct LineBreakpoint {
        int id = 0;
        std::string path;
        int line      = 0;
        bool verified = false;
        std::string message;
        std::vector<origin_t> origins;
    };

    /// What is about to run, for both back ends.
    struct Arrival {
        const NodeInfo *node = nullptr;
        origin_t origin      = 0;
        const SourceLoc *loc = nullptr;
        int function         = -1;
        bool executes        = false;
    };

    struct Handle {
        enum class Kind { Locals, Captured, Inlined, Nodes, Value } kind = Kind::Value;
        uint64_t frame = 0;
        ValueView view;
    };

    struct Variable {
        std::string name;
        bool evaluated = true;
        std::string note; // shown instead of a value when not evaluated
        ValueView view;
    };

    // program thread
    ShadowFrame &pushFrame(camel::runtime::GCGraph *graph, camel::core::context::Frame *frame);
    void firstNodeOfRun(camel::runtime::GCGraph *root);
    void arrive(ShadowFrame &frame, const Arrival &arrival);
    bool stepReached(const ShadowFrame &frame, const Arrival &arrival) const;
    void refreshConfig();
    void stop(const std::string &reason, json details);
    json describeStop(const std::string &reason, json details);
    json frameJson(const ShadowFrame &frame, bool top);
    json stackJson();
    ShadowFrame *frameById(uint64_t id);
    std::vector<Variable> localsOf(ShadowFrame &frame);
    std::vector<Variable> capturedOf(ShadowFrame &frame);
    std::vector<Variable> inlinedOf(ShadowFrame &frame);
    std::vector<Variable> nodesOf(ShadowFrame &frame);
    std::optional<Variable> nodeVariable(ShadowFrame &frame, const NodeInfo &node, std::string name);
    json variablesJson(std::vector<Variable> variables, uint64_t frame);
    json variableJson(const Variable &variable, uint64_t frame);
    json scopesJson(uint64_t frameId);
    json handleJson(int reference);
    json evaluateJson(uint64_t frameId, const std::string &expression);
    json graphJson(uint64_t frameId);
    int newHandle(Handle handle);

    // any thread
    json runWhileStopped(std::function<json()> fn);
    json setLineBreakpoints(const json &args);
    json breakpointJson(const LineBreakpoint &bp) const;
    void verify(LineBreakpoint &bp);
    void reverifyAll();
    void pushEvent(json event);
    json resume(Step step, bool stepping);

    // ---- state shared between threads, guarded by mutex_ ----
    std::mutex mutex_;
    std::condition_variable cond_;       // wakes the stopped program
    std::condition_variable eventCond_;  // wakes event polls
    State state_ = State::Idle;
    std::deque<json> events_;
    uint64_t nextEventSeq_ = 1;
    std::vector<LineBreakpoint> lineBreakpoints_;
    std::vector<origin_t> nodeBreakpoints_;
    bool stopOnUncaught_ = true;
    bool stopOnIsolated_ = false;
    bool stopOnEntry_    = false;
    bool stopOnEntryOnce_ = false;
    int nextBreakpointId_ = 1;
    json lastStop_;
    json lastException_;
    bool resumePending_ = false;
    Step resumeStep_;
    std::deque<std::pair<std::function<json()>, std::promise<json> *>> requests_;
    std::function<void(const json *)> stopListener_;

    std::atomic<uint64_t> configVersion_{1};
    std::atomic<bool> pauseRequested_{false};
    std::atomic<int> abort_{0}; // 0 none, 1 terminate, 2 restart

    // ---- the index; guarded by indexMutex_ (never held while waiting) ----
    std::mutex indexMutex_;
    ProgramIndex index_;

    // ---- owned by the program's thread ----
    std::vector<ShadowFrame> stack_;
    std::vector<size_t> activationBase_;
    Pending pending_       = Pending::None;
    uint64_t nextFrameId_  = 1;
    bool runIndexed_       = false;
    bool entryStopPending_ = false;
    Step step_;
    uint64_t seenConfig_ = 0;
    std::unordered_map<origin_t, std::vector<int>> lineBreakpointByOrigin_;
    std::unordered_set<origin_t> nodeBreakpointSet_;
    bool filterUncaught_ = true;
    bool filterIsolated_ = false;
    std::vector<Handle> handles_;
    std::unordered_map<size_t, Arrival> instructionCache_; // fvm: by pc
};

} // namespace debugger::core
