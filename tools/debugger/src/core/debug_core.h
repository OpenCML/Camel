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
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */
#pragma once

/**
 * @file debug_core.h
 * @brief The one place that controls a program being debugged
 *        (docs/technical/35_debugger.md, section 7).
 *
 * DebugCore lives in the process that runs the program (a camel-db worker). It is the
 * scheduler's ExecutionObserver: it keeps the stack of frames, decides at every node whether
 * to stop (breakpoints, steps, pause), stops by blocking the program's thread in the hook, and
 * answers questions about the stopped program on that thread. It knows no transport: the HTTP
 * API of the worker and, through it, the debug adapter are shells over command() and events().
 *
 * Two layers of frames (35_debugger.md, 5.4). The core tracks one shadow frame per activation
 * of a graph, as the schedulers run them. What it shows is, by default, one frame per
 * activation of a *function*: the frames of the subgraphs a function is made of (the arms of
 * its branches) are folded into it, and the line steps and line breakpoints treat them as one.
 * The setting `frames` ("functions", the default, or "subgraphs") switches the presentation
 * to the shadow frames themselves; node steps and the graph view always see the real graphs.
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
#include <set>
#include <string>
#include <thread>
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

    /// A materialized graph boundary, called synchronously on the program thread.
    /// Index 0 precedes all passes; later indices identify a returned graph, not a name.
    void pipelineBoundary(size_t index, const std::string &pass, camel::runtime::GCGraph *graph);

    /// Debug allocation hooks: true when handled by the native core (including filtered events).
    bool allocationEvent(size_t size, const char *space, const void *ptr);

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
    void passStarting(bool schedules, camel::runtime::GCGraph *root) override;

  private:
    DebugCore() = default;

    enum class State { Idle, Loaded, Running, Stopped, Finished };
    enum class StepKind { None, Node, Line, Out, Pass };
    enum class Pending { None, Enter, Tail };

    /// Where a frame is in the text: the line it is on (2.1) and the source function of it.
    struct LinePosition {
        int file     = -1;
        int line     = 0;
        int function = -1;
    };

    struct ShadowFrame {
        uint64_t id                        = 0;
        camel::runtime::GCGraph *graph     = nullptr;
        camel::core::context::Frame *frame = nullptr;
        GraphInfo *info                    = nullptr;
        const NodeInfo *node               = nullptr; // about to run, running, or waited in
        origin_t origin                    = 0;       // of what is about to run
        SourceLoc loc;                                // of that origin
        int lastFile     = -1;                        // the line the frame is on (2.1)
        int lastLine     = 0;
        int lastFunction = -1;
        int pendingNode  = -1;  // index of the node whose result is due
        std::vector<bool> done; // by node index: has run in this activation
        uint32_t tailReplaced                        = 0;
        camel::execute::InstructionEffect lastEffect = camel::execute::InstructionEffect::None;

        // ---- the function activation the frame is part of (5.4) ----
        /// Shared by the frame of a function and the frames of the subgraphs it calls that
        /// are part of it (GraphInfo::folds); every other frame starts an activation.
        uint64_t activation = 0;
        /// The number of activations from the bottom of the stack up to this one.
        size_t presentedDepth = 0;
        /// The graph of the activation's function and what is known of it.
        GraphInfo *function = nullptr;
        /// Activations of functions that tail calls replaced in this one's place.
        uint32_t functionsReplaced = 0;
        /// The line the activation is on: the line of its last located node, whichever of
        /// its frames ran it. Handed on to the frame below when a frame of it ends.
        LinePosition activationLine;
        /// The activation has not run a located node yet (function breakpoints, 3.3).
        bool awaitingEntry = false;
        bool atEntry = false; // the current instruction is the first located one of this activation
    };

    struct Step {
        StepKind kind = StepKind::None;
        bool into     = false;
        /// Steps by activation of a function (`frames` is "functions"), or by shadow frame.
        bool folded              = true;
        size_t depth             = 0; // of the frame or of the activation, as `folded` says
        uint64_t frame           = 0; // the frame or the activation the step started in
        uint64_t startActivation = 0; // the activation it started in, in both cases
        int file                 = -1;
        int line                 = 0;
        int function             = -1;
        bool foreign             = false; // started in code inlined from another function
        std::string graph;                // node steps of the HTTP API: only nodes of this graph
        /// Where the caller of the start frame was: a line step that returns there may
        /// have gone past lines of the caller as well (4.2).
        uint64_t caller = 0;
        LinePosition callerLine;
    };

    struct LineBreakpoint {
        int id = 0;
        std::string path;
        int line      = 0;
        bool verified = false;
        std::string message;
        std::vector<origin_t> origins;
        LineResolution resolution; // why it is not hit, said again at the end (4.2)
    };

    /// A breakpoint on the entry of a function, by the name the user wrote (3.3).
    struct FunctionBreakpoint {
        int id = 0;
        std::string name;     // as written
        std::string fullName; // as resolved
        bool verified = false;
        std::string message;
        std::vector<std::string> graphs; // graph ids
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
        uint64_t frame                                                   = 0;
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
    /// The frame of a call, above the caller's.
    ShadowFrame &pushCallee(camel::runtime::GCGraph *graph, camel::core::context::Frame *frame);
    /// The frame of a tail call, in the place of the top frame.
    ShadowFrame &replaceTop(camel::runtime::GCGraph *graph, camel::core::context::Frame *frame);
    void firstNodeOfRun(camel::runtime::GCGraph *root);
    void arrive(ShadowFrame &frame, const Arrival &arrival);
    bool stepReached(const ShadowFrame &frame, const Arrival &arrival) const;
    void refreshConfig();
    void stop(const std::string &reason, json details);
    json describeStop(const std::string &reason, json details);
    json stackJson();
    ShadowFrame *frameById(uint64_t id);

    // presentation of frames (frames.cpp)
    using Chain = std::vector<ShadowFrame *>; // the frames shown as one, innermost first
    /// Places the frame of a call, just pushed above `caller` (null for the first frame):
    /// part of the caller's activation when it is a subgraph of the caller's graph that folds
    /// (an arm of one of its branches), the start of an activation otherwise.
    void placeCallee(ShadowFrame &f, const ShadowFrame *caller);
    /// Places the frame of a tail call that replaced `old`; `below` is the frame under it.
    void placeReplacement(ShadowFrame &f, const ShadowFrame &old, const ShadowFrame *below);
    /// Before the top frame ends: the line of its activation goes on in the frame below.
    void handOnLine(size_t endingFrom);
    bool foldsFrames() const { return foldFrames_.load(std::memory_order_relaxed); }
    /// The frames shown as the frame of `top` (only `top` when frames are not folded).
    Chain chainOf(ShadowFrame &top);
    json frameJson(const Chain &chain, bool top);
    /// The lines a line step went past that did not execute because nothing uses their
    /// result, and why (4.2). `f` is the frame stopped in.
    json skippedLines(const ShadowFrame &f, const Arrival &arrival);

    std::vector<Variable> localsOf(const Chain &chain);
    std::vector<Variable> capturedOf(const Chain &chain);
    std::vector<Variable> inlinedOf(const Chain &chain);
    std::vector<Variable> nodesOf(const Chain &chain);
    std::optional<Variable>
    nodeVariable(ShadowFrame &frame, const NodeInfo &node, std::string name);
    json variablesJson(std::vector<Variable> variables, uint64_t frame);
    json variableJson(const Variable &variable, uint64_t frame);
    json scopesJson(uint64_t frameId);
    json handleJson(int reference);
    json evaluateJson(uint64_t frameId, const std::string &expression);
    json graphJson(uint64_t frameId);
    json memoryCommand(const std::string &name, const json &args);
    int newHandle(Handle handle);

    // any thread
    json runWhileStopped(std::function<json()> fn);
    json setLineBreakpoints(const json &args);
    json setFunctionBreakpoints(const json &args);
    json breakpointJson(const LineBreakpoint &bp) const;
    json breakpointJson(const FunctionBreakpoint &bp) const;
    void verify(LineBreakpoint &bp);
    void verify(FunctionBreakpoint &bp);
    /// At the end of a run: what is known about the breakpoints that were never hit.
    void reportUnhitBreakpoints();
    void reverifyAll();
    void pushEvent(json event);
    json resume(Step step, bool stepping);

    // ---- state shared between threads, guarded by mutex_ ----
    std::mutex mutex_;
    std::condition_variable cond_;      // wakes the stopped program
    std::condition_variable eventCond_; // wakes event polls
    State state_ = State::Idle;
    std::deque<json> events_;
    uint64_t nextEventSeq_ = 1;
    std::vector<LineBreakpoint> lineBreakpoints_;
    std::vector<FunctionBreakpoint> functionBreakpoints_;
    std::vector<origin_t> nodeBreakpoints_;
    bool stopOnUncaught_    = true;
    bool stopOnIsolated_    = false;
    bool allocationEnabled_ = false;
    std::set<std::string> allocationSpaces_;
    std::thread::id programThread_;
    bool stopOnPassEntry_ = false;
    std::set<size_t> passBreakpoints_;
    bool stopOnEntry_     = false;
    bool stopOnEntryOnce_ = false;
    int nextBreakpointId_ = 1;
    json lastStop_;
    json lastException_;
    bool resumePending_ = false;
    Step resumeStep_;
    std::deque<std::pair<std::function<json()>, std::promise<json> *>> requests_;
    std::function<void(const json *)> stopListener_;

    std::atomic<uint64_t> configVersion_{1};
    /// `frames`: true for "functions" (the default, design principles 12.1), false for
    /// "subgraphs". Read by the program's thread when it presents a frame or starts a step.
    std::atomic<bool> foldFrames_{true};
    std::atomic<bool> pauseRequested_{false};
    std::atomic<int> abort_{0}; // 0 none, 1 terminate, 2 restart

    // ---- the index; guarded by indexMutex_ (never held while waiting) ----
    // Allocation callbacks can run inside an index update. Track occupancy instead
    // of try_lock on a mutex the same thread may already own (undefined behavior).
    class IndexMutex {
      public:
        void lock();
        void unlock();
        bool busy() const;

      private:
        std::mutex mutex_;
        std::atomic<bool> held_{false};
    } indexMutex_;
    ProgramIndex index_;

    // ---- owned by the program's thread ----
    std::vector<ShadowFrame> stack_;
    std::vector<size_t> activationBase_;
    Pending pending_      = Pending::None;
    uint64_t nextFrameId_ = 1;
    // Serialized only while stopped at a pipeline boundary; no retained GC pointers.
    std::string pipelineGir_;
    bool runIndexed_       = false;
    /// True only while a scheduling pass runs the program (see passStarting).
    bool observingRun_     = false;
    bool entryStopPending_ = false;
    Step step_;
    uint64_t seenConfig_ = 0;
    std::unordered_map<origin_t, std::vector<int>> lineBreakpointByOrigin_;
    std::unordered_map<std::string, std::vector<int>> functionBreakpointByGraph_; // graph id
    std::unordered_set<int> hitBreakpoints_; // ids of the breakpoints this run stopped at
    uint64_t nextActivation_ = 1;
    /// The dropped lines a step has already explained, by activation: said once (4.2).
    std::unordered_map<uint64_t, std::set<std::pair<int, int>>> explainedLines_;
    std::unordered_set<origin_t> nodeBreakpointSet_;
    bool filterUncaught_ = true;
    bool filterIsolated_ = false;
    std::vector<Handle> handles_;
    std::unordered_map<size_t, Arrival> instructionCache_; // fvm: by pc
};

} // namespace debugger::core
