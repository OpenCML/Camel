// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Commands (DebugCore::command), all answered with {"ok": true, ...} or
 * {"ok": false, "error": "..."}:
 *
 *   status                                   state, the last stop
 *   setBreakpoints {path, lines}             replaces the line breakpoints of a file
 *   setFunctionBreakpoints {names}           replaces the function breakpoints
 *   setNodeBreakpoints {origins}             replaces the node breakpoints
 *   setExceptionFilters {filters}            "uncaught", "isolated"
 *   configure {stopOnEntry, frames}          frames: "functions" (default) or "subgraphs"
 *   continue | pause
 *   step {unit: "line"|"node", mode: "over"|"in"|"out", graph?}
 *   terminate | restart                      ends the run from inside (RunAborted)
 * and, only while the program is stopped (executed on its thread):
 *   stackTrace | scopes {frame} | variables {reference} | evaluate {frame, expression}
 *   graph {frame} | exceptionInfo
 *
 * Events (DebugCore::events): loaded, started, stopped, continued, breakpoint, note, finished.
 * A `note` is a sentence for the user (a breakpoint that was never hit, and why).
 */

#include "debug_core.h"

#include "camel/core/context/frame.h"
#include "camel/core/error/runtime.h"
#include "camel/passes/trans/json/gir.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <sstream>

namespace debugger::core {

using namespace camel::runtime;
using camel::core::context::Frame;
using camel::execute::InstructionEffect;
using json = nlohmann::json;

namespace {

constexpr size_t kMaxEvents   = 4096;
constexpr size_t kMaxChildren = 100;

json fail(const std::string &message) { return json{{"ok", false}, {"error", message}}; }

std::string lineStateMessage(const LineResolution &r) {
    switch (r.state) {
    case LineState::Executable:
        return "";
    case LineState::NoCode:
        return "No code on this line.";
    case LineState::NotDepended:
        return "Line " + std::to_string(r.line) +
               " is not executed: " + whyNotExecuted(r.state, r.names) +
               ". Evaluation is driven by dependencies; to run it, use its result or put it in "
               "a sync block.";
    case LineState::NotCalled:
        return "Line " + std::to_string(r.line) +
               " is not executed: " + whyNotExecuted(r.state, r.names) + ".";
    case LineState::Removed:
        return "The code on this line is not in the graph that runs: a pass removed or "
               "replaced it.";
    case LineState::ConstantOnly:
        return "This line only defines constants: there is nothing to execute.";
    case LineState::UnknownFile:
        return "This file is not part of the program being debugged.";
    }
    return "";
}

} // namespace

void DebugCore::IndexMutex::lock() {
    mutex_.lock();
    held_.store(true, std::memory_order_release);
}
void DebugCore::IndexMutex::unlock() {
    held_.store(false, std::memory_order_release);
    mutex_.unlock();
}
bool DebugCore::IndexMutex::busy() const { return held_.load(std::memory_order_acquire); }

DebugCore &DebugCore::instance() {
    static DebugCore core;
    return core;
}

void DebugCore::setStopListener(std::function<void(const json *)> listener) {
    std::lock_guard<std::mutex> lock(mutex_);
    stopListener_ = std::move(listener);
}

// ---------------------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------------------

void DebugCore::pushEvent(json event) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        event["seq"] = nextEventSeq_++;
        events_.push_back(std::move(event));
        while (events_.size() > kMaxEvents) {
            events_.pop_front();
        }
    }
    eventCond_.notify_all();
}

json DebugCore::events(uint64_t after, int timeoutMs) {
    std::unique_lock<std::mutex> lock(mutex_);
    const auto ready = [&] {
        return !events_.empty() && events_.back()["seq"].get<uint64_t>() > after;
    };
    if (timeoutMs > 0) {
        eventCond_.wait_for(lock, std::chrono::milliseconds(timeoutMs), ready);
    }
    json out = json::array();
    for (const auto &e : events_) {
        if (e["seq"].get<uint64_t>() > after) {
            out.push_back(e);
        }
    }
    return json{{"ok", true}, {"events", std::move(out)}, {"last", nextEventSeq_ - 1}};
}

// ---------------------------------------------------------------------------------------
// Run life cycle
// ---------------------------------------------------------------------------------------

void DebugCore::beginCompile(const camel::core::context::context_ptr_t &ctx) {
    {
        std::lock_guard lock(indexMutex_);
        index_.begin(ctx);
    }
    ctx->setSemanticRecorder(index_.recorder());
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = State::Idle;
}

void DebugCore::endCompile(GCGraph *root) {
    if (auto ctx = index_.context()) {
        ctx->setSemanticRecorder(nullptr);
    }
    {
        std::lock_guard lock(indexMutex_);
        index_.compiled(root);
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = root != nullptr ? State::Loaded : State::Idle;
    }
    reverifyAll();
    pushEvent(json{{"type", "loaded"}, {"compiled", root != nullptr}});
}

void DebugCore::beginRun(const std::string &notice) {
    stack_.clear();
    activationBase_.clear();
    handles_.clear();
    instructionCache_.clear();
    explainedLines_.clear();
    hitBreakpoints_.clear();
    pending_      = Pending::None;
    runIndexed_   = false;
    observingRun_ = false;
    step_       = Step{};
    seenConfig_ = 0;
    abort_.store(0);
    pauseRequested_.store(false);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_            = State::Running;
        programThread_    = std::this_thread::get_id();
        entryStopPending_ = stopOnEntry_ || stopOnEntryOnce_;
        stopOnEntryOnce_  = false;
        resumePending_    = false;
        lastStop_         = nullptr;
        lastException_    = nullptr;
    }
    camel::execute::setExecutionObserver(this);
    pushEvent(json{{"type", "started"}, {"notice", notice}});
}

void DebugCore::endRun(const std::string &outcome, std::optional<int> exitCode) {
    camel::execute::setExecutionObserver(nullptr);
    if (outcome != "terminated") {
        reportUnhitBreakpoints();
    }
    stack_.clear();
    activationBase_.clear();
    handles_.clear();
    std::function<void(const json *)> listener;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_   = State::Finished;
        listener = stopListener_;
        // Whoever still waits for the stopped program gets an answer.
        for (auto &[fn, promise] : requests_) {
            promise->set_value(fail("the program has ended"));
        }
        requests_.clear();
    }
    if (listener) {
        listener(nullptr);
    }
    json event{{"type", "finished"}, {"outcome", outcome}};
    event["exitCode"] = exitCode ? json(*exitCode) : json(nullptr);
    pushEvent(std::move(event));
}

// ---------------------------------------------------------------------------------------
// Breakpoints
// ---------------------------------------------------------------------------------------

json DebugCore::breakpointJson(const LineBreakpoint &bp) const {
    json j{{"id", bp.id}, {"verified", bp.verified}, {"line", bp.line}, {"path", bp.path}};
    if (!bp.message.empty()) {
        j["message"] = bp.message;
    }
    j["nodes"] = bp.origins.size();
    return j;
}

json DebugCore::breakpointJson(const FunctionBreakpoint &bp) const {
    json j{{"id", bp.id}, {"verified", bp.verified}, {"name", bp.name}};
    if (!bp.fullName.empty()) {
        j["function"] = bp.fullName;
    }
    if (!bp.message.empty()) {
        j["message"] = bp.message;
    }
    j["graphs"] = bp.graphs.size();
    return j;
}

void DebugCore::verify(LineBreakpoint &bp) {
    // mutex_ is held by the caller; the index has its own lock.
    bp.origins.clear();
    std::lock_guard lock(indexMutex_);
    if (!index_.isCompiled()) {
        bp.verified = false;
        bp.message  = "The program has not been compiled yet.";
        return;
    }
    LineResolution r = index_.resolveLine(bp.path, bp.line);
    bp.verified      = r.state == LineState::Executable;
    bp.message       = lineStateMessage(r);
    if (bp.verified && !r.called && !r.names.empty()) {
        // Verified all the same: the line is in the program and holds a breakpoint, which
        // will not be hit (said again at the end, reportUnhitBreakpoints).
        bp.message = "Nothing in the program calls `" + r.names.front() + "`.";
    }
    bp.origins    = std::move(r.origins);
    bp.resolution = std::move(r);
}

void DebugCore::verify(FunctionBreakpoint &bp) {
    // mutex_ is held by the caller; the index has its own lock.
    bp.graphs.clear();
    bp.fullName.clear();
    std::lock_guard lock(indexMutex_);
    if (!index_.isCompiled()) {
        bp.verified = false;
        bp.message  = "The program has not been compiled yet.";
        return;
    }
    FunctionResolution r = index_.resolveFunction(bp.name);
    bp.verified          = r.found;
    bp.message           = r.message;
    bp.fullName          = r.fullName;
    bp.graphs            = std::move(r.graphs);
}

void DebugCore::reportUnhitBreakpoints() {
    // A line breakpoint that was not hit because nothing uses what its line computes, or
    // nothing calls the function it is in, was never hit for a reason the graph shows: said
    // once more now that the program has ended (4.2). Of another breakpoint that was not hit
    // nothing is said: why a node was not reached (an arm not taken, ...) is not recorded.
    std::vector<std::string> notes;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto &bp : lineBreakpoints_) {
            const LineResolution &r = bp.resolution;
            LineState why           = r.state;
            if (r.state == LineState::Executable && !r.called) {
                why = LineState::NotCalled;
            }
            if (hitBreakpoints_.count(bp.id) != 0 ||
                (why != LineState::NotDepended && why != LineState::NotCalled)) {
                continue;
            }
            notes.push_back(
                "Breakpoint at " + std::filesystem::path(bp.path).filename().string() + ":" +
                std::to_string(bp.line) + " was never hit: line " + std::to_string(bp.line) +
                " was not executed: " + whyNotExecuted(why, r.names) + ".");
        }
    }
    for (auto &text : notes) {
        pushEvent(json{{"type", "note"}, {"text", std::move(text)}});
    }
}

void DebugCore::reverifyAll() {
    std::vector<json> changed;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &bp : lineBreakpoints_) {
            const bool was           = bp.verified;
            const std::string before = bp.message;
            const auto origins       = bp.origins;
            verify(bp);
            if (was != bp.verified || before != bp.message || origins != bp.origins) {
                changed.push_back(breakpointJson(bp));
            }
        }
        for (auto &bp : functionBreakpoints_) {
            const bool was           = bp.verified;
            const std::string before = bp.message;
            const auto graphs        = bp.graphs;
            verify(bp);
            if (was != bp.verified || before != bp.message || graphs != bp.graphs) {
                changed.push_back(breakpointJson(bp));
            }
        }
        configVersion_.fetch_add(1);
    }
    for (auto &bp : changed) {
        pushEvent(json{{"type", "breakpoint"}, {"breakpoint", std::move(bp)}});
    }
}

json DebugCore::setLineBreakpoints(const json &args) {
    if (!args.contains("path") || !args["path"].is_string()) {
        return fail("setBreakpoints needs a path");
    }
    const std::string path = args["path"].get<std::string>();
    std::vector<int> lines;
    if (args.contains("lines") && args["lines"].is_array()) {
        for (const auto &l : args["lines"]) {
            if (l.is_number_integer()) {
                lines.push_back(l.get<int>());
            }
        }
    }
    json out = json::array();
    std::lock_guard<std::mutex> lock(mutex_);
    std::erase_if(lineBreakpoints_, [&](const LineBreakpoint &bp) { return bp.path == path; });
    for (int line : lines) {
        LineBreakpoint bp;
        bp.id   = nextBreakpointId_++;
        bp.path = path;
        bp.line = line;
        verify(bp);
        out.push_back(breakpointJson(bp));
        lineBreakpoints_.push_back(std::move(bp));
    }
    configVersion_.fetch_add(1);
    return json{{"ok", true}, {"breakpoints", std::move(out)}};
}

json DebugCore::setFunctionBreakpoints(const json &args) {
    std::vector<std::string> names;
    if (args.contains("names") && args["names"].is_array()) {
        for (const auto &n : args["names"]) {
            if (n.is_string() && !n.get<std::string>().empty()) {
                names.push_back(n.get<std::string>());
            }
        }
    }
    json out = json::array();
    std::lock_guard<std::mutex> lock(mutex_);
    functionBreakpoints_.clear();
    for (const std::string &name : names) {
        FunctionBreakpoint bp;
        bp.id   = nextBreakpointId_++;
        bp.name = name;
        verify(bp);
        out.push_back(breakpointJson(bp));
        functionBreakpoints_.push_back(std::move(bp));
    }
    configVersion_.fetch_add(1);
    return json{{"ok", true}, {"breakpoints", std::move(out)}};
}

void DebugCore::refreshConfig() {
    std::lock_guard<std::mutex> lock(mutex_);
    seenConfig_ = configVersion_.load();
    lineBreakpointByOrigin_.clear();
    for (const auto &bp : lineBreakpoints_) {
        if (!bp.verified) {
            continue;
        }
        for (origin_t o : bp.origins) {
            lineBreakpointByOrigin_[o].push_back(bp.id);
        }
    }
    functionBreakpointByGraph_.clear();
    for (const auto &bp : functionBreakpoints_) {
        for (const std::string &graph : bp.graphs) {
            functionBreakpointByGraph_[graph].push_back(bp.id);
        }
    }
    nodeBreakpointSet_.clear();
    nodeBreakpointSet_.insert(nodeBreakpoints_.begin(), nodeBreakpoints_.end());
    filterUncaught_ = stopOnUncaught_;
    filterIsolated_ = stopOnIsolated_;
}

// ---------------------------------------------------------------------------------------
// The stack, fed by the scheduler
// ---------------------------------------------------------------------------------------

void DebugCore::activationEntered() {
    if (!observingRun_) {
        return;
    }
    activationBase_.push_back(stack_.size());
    pending_ = Pending::Enter;
}

void DebugCore::activationLeft() {
    if (!observingRun_) {
        return;
    }
    if (activationBase_.empty()) {
        return;
    }
    const size_t base = activationBase_.back();
    activationBase_.pop_back();
    if (stack_.size() > base) {
        handOnLine(base);
        stack_.resize(base);
    }
    pending_ = Pending::None;
}

void DebugCore::tailCall() {
    if (!observingRun_) {
        return;
    }
    pending_ = Pending::Tail;
}

void DebugCore::firstNodeOfRun(GCGraph *root) {
    runIndexed_ = true;
    {
        std::lock_guard lock(indexMutex_);
        index_.running(root);
    }
    // The graph the scheduler was given is known now: breakpoints are answered again.
    reverifyAll();
}

void DebugCore::passStarting(bool schedules, GCGraph *) {
    // A rewrite or translation pass may run graphs of its own for its work (the rules library
    // behind `std::opt`, the function of a macro). Those are the compiler's, not the program's
    // run: while one is in progress no frame is kept and the run is not indexed, so breakpoints
    // are still resolved against the program the pipeline is about to run (H150). A scheduling
    // pass is the run itself: the first frame it reports indexes the program it was given.
    observingRun_ = schedules;
    if (schedules) {
        stack_.clear();
        activationBase_.clear();
        runIndexed_ = false;
    }
}

DebugCore::ShadowFrame &DebugCore::pushFrame(GCGraph *graph, Frame *frame) {
    if (!runIndexed_) {
        firstNodeOfRun(graph);
    }
    ShadowFrame f;
    f.id    = nextFrameId_++;
    f.graph = graph;
    f.frame = frame;
    {
        std::lock_guard lock(indexMutex_);
        f.info = &index_.graph(graph);
    }
    f.lastFunction = f.info->function;
    f.done.assign(f.info->nodes.size(), false);
    stack_.push_back(std::move(f));
    return stack_.back();
}

DebugCore::ShadowFrame &DebugCore::pushCallee(GCGraph *graph, Frame *frame) {
    const bool called = !stack_.empty();
    ShadowFrame &f    = pushFrame(graph, frame);
    // After the push: pushing may have moved the frames.
    ShadowFrame *caller = called ? &stack_[stack_.size() - 2] : nullptr;
    placeCallee(f, caller);
    if (caller && f.info->folds()) {
        // The arm of a branch is code of the function that branches: its frame goes on with
        // the line that function is on, so a line the branch and its arm share is entered
        // once (2.1), not once in each.
        f.lastFile = caller->lastFile;
        f.lastLine = caller->lastLine;
    }
    return f;
}

DebugCore::ShadowFrame &DebugCore::replaceTop(GCGraph *graph, Frame *frame) {
    const ShadowFrame old = std::move(stack_.back());
    stack_.pop_back();
    ShadowFrame &f = pushFrame(graph, frame);
    f.tailReplaced = old.tailReplaced + 1;
    placeReplacement(f, old, stack_.size() > 1 ? &stack_[stack_.size() - 2] : nullptr);
    if (f.info->folds()) {
        // As in pushCallee; and a line step that started in the function goes on in its arm
        // (a step by activation goes on by itself: the arm is part of it).
        f.lastFile = old.lastFile;
        f.lastLine = old.lastLine;
        if (step_.frame == old.id && !step_.folded) {
            step_.frame = f.id;
        }
    }
    return f;
}

void DebugCore::beforeNode(
    GCGraph *graph, Frame *frame, gc_node_ref_t node,
    const camel::execute::FusionGroupSite *group) {
    // A pass may run graphs of its own for its work (`std::opt`'s rules library, the function of
    // a macro). The program being debugged runs only in a scheduling pass; indexing or keeping
    // a frame of that other execution would describe the session as that program and report
    // every breakpoint of the user's file as gone (H150).
    if (!observingRun_) {
        return;
    }
    if (pending_ == Pending::Enter || stack_.empty()) {
        pushCallee(graph, frame);
    } else if (
        pending_ == Pending::Tail || stack_.back().frame != frame || stack_.back().graph != graph) {
        // A tail call: the callee's frame takes the caller's place (5.2).
        replaceTop(graph, frame);
    }
    pending_       = Pending::None;
    ShadowFrame &f = stack_.back();
    if (f.pendingNode >= 0) {
        f.done[static_cast<size_t>(f.pendingNode)] = true;
    }
    Arrival arrival;
    if (auto it = f.info->byRef.find(node); it != f.info->byRef.end()) {
        const NodeInfo &n = f.info->nodes[it->second];
        arrival.node      = &n;
        arrival.origin    = n.origin;
        arrival.loc       = &n.loc;
        arrival.function  = n.function;
        arrival.executes  = n.executes;
        f.pendingNode     = static_cast<int>(it->second);
    } else {
        f.pendingNode = -1;
    }
    arrival.group = group;
    arrive(f, arrival);
}

void DebugCore::beforeInstruction(const camel::execute::InstructionSite &site) {
    GCGraph *graph = site.frame ? site.frame->runtimeGraph() : nullptr;
    if (graph == nullptr) {
        return;
    }
    // See beforeNode: a graph a pass runs for itself is not the program being debugged.
    if (!observingRun_) {
        return;
    }
    if (pending_ == Pending::Enter || stack_.empty()) {
        pushCallee(graph, site.frame);
    } else {
        // What the instruction executed last in this activation did to the call stack.
        switch (stack_.back().lastEffect) {
        case InstructionEffect::Call:
            stack_.back().lastEffect = InstructionEffect::None;
            pushCallee(graph, site.frame);
            break;
        case InstructionEffect::Return:
            handOnLine(stack_.size() - 1);
            stack_.pop_back();
            if (stack_.empty()) {
                placeCallee(pushFrame(graph, site.frame), nullptr);
            }
            stack_.back().lastEffect = InstructionEffect::None;
            break;
        case InstructionEffect::TailCall:
            replaceTop(graph, site.frame);
            break;
        default:
            break;
        }
    }
    pending_       = Pending::None;
    ShadowFrame &f = stack_.back();
    if (f.frame != site.frame || f.graph != graph) {
        // Not expected; follow the interpreter rather than show a frame it is not in.
        const ShadowFrame old = std::move(stack_.back());
        stack_.pop_back();
        ShadowFrame &now = pushFrame(graph, site.frame);
        now.tailReplaced = old.tailReplaced;
        placeReplacement(now, old, stack_.size() > 1 ? &stack_[stack_.size() - 2] : nullptr);
    }
    ShadowFrame &top = stack_.back();
    if (top.pendingNode >= 0) {
        top.done[static_cast<size_t>(top.pendingNode)] = true;
    }
    auto cached = instructionCache_.find(site.pc);
    if (cached == instructionCache_.end()) {
        Arrival arrival;
        arrival.origin = site.origin;
        {
            std::lock_guard lock(indexMutex_);
            arrival.loc      = &index_.loc(site.origin);
            arrival.function = index_.functionOf(*arrival.loc);
        }
        if (site.effect != InstructionEffect::Jump && site.effect != InstructionEffect::Return) {
            arrival.node     = top.info->nodeOfInstruction(site.origin, site.resultSlot);
            arrival.executes = arrival.node != nullptr || site.origin != 0;
        }
        cached = instructionCache_.emplace(site.pc, arrival).first;
    }
    const Arrival &arrival = cached->second;
    top.pendingNode =
        arrival.node != nullptr && site.resultSlot != 0 && arrival.node->slot == site.resultSlot
            ? static_cast<int>(arrival.node - top.info->nodes.data())
            : -1;
    top.lastEffect = site.effect;
    arrive(top, arrival);
}

// ---------------------------------------------------------------------------------------
// Deciding whether to stop
// ---------------------------------------------------------------------------------------

bool DebugCore::stepReached(const ShadowFrame &f, const Arrival &a) const {
    // A step started with frames folded counts activations of functions: the frames of the
    // arms of a function are the function's frame (5.4). Otherwise it counts shadow frames.
    const size_t depth   = step_.folded ? f.presentedDepth : stack_.size();
    const uint64_t frame = step_.folded ? f.activation : f.id;
    const bool located   = a.loc != nullptr && a.loc->valid();
    switch (step_.kind) {
    case StepKind::None:
        return false;
    case StepKind::Node:
        if (!step_.graph.empty()) {
            return f.info->graphId == step_.graph;
        }
        return step_.into || depth < step_.depth || (depth == step_.depth && frame == step_.frame);
    case StepKind::Line:
        // Design principles 12.1: the next line in execution order, not in the text.
        if (!located) {
            return false;
        }
        if (!step_.into &&
            (depth > step_.depth || (depth == step_.depth && frame != step_.frame))) {
            // A tail call replaces the activation at the same depth. F10 runs that
            // entire call to its real caller; only F11 enters the replacement.
            return false;
        }
        if (frame != step_.frame) {
            // The caller after a return, or a callee (including a tail call) on step in.
            return true;
        }
        if (a.function != step_.function) {
            // DESIGN-QUESTION(inlined-frames): code of another function of the source in the
            // same frame was inlined. Step in enters it; a line step started in the graph's
            // own function steps over it; coming back to the graph's own function is a stop.
            if (step_.into || a.function == f.info->function) {
                return true;
            }
            return step_.foreign;
        }
        return !(a.loc->file == step_.file && a.loc->line == step_.line);
    case StepKind::Out:
        if (!located) {
            return false;
        }
        if (depth < step_.depth) {
            return true;
        }
        return frame == step_.frame && step_.foreign && a.function != step_.function;
    }
    return false;
}

void DebugCore::arrive(ShadowFrame &f, const Arrival &a) {
    f.node   = a.node;
    f.origin = a.origin;
    f.loc    = a.loc ? *a.loc : SourceLoc{};
    if (const int abort = abort_.load(std::memory_order_relaxed); abort != 0) [[unlikely]] {
        throw RunAborted{abort == 2};
    }
    if (!a.executes) {
        return;
    }
    if (seenConfig_ != configVersion_.load(std::memory_order_relaxed)) [[unlikely]] {
        refreshConfig();
    }
    const bool located = f.loc.valid();
    // DESIGN-QUESTION(line-breakpoint-hits): a line is stopped at when the frame enters it,
    // once per visit, not once per node of the line. With frames folded the frame is the
    // activation of the function: a line it is on is not entered again when an arm of it
    // starts, and is when the activation comes back to it after the arm (5.4).
    const LinePosition on =
        foldsFrames() ? f.activationLine : LinePosition{f.lastFile, f.lastLine, f.lastFunction};
    const bool entering = located && !(on.file == f.loc.file && on.line == f.loc.line);
    std::string reason;
    json details = json::object();
    if (entering) {
        if (auto it = lineBreakpointByOrigin_.find(a.origin); it != lineBreakpointByOrigin_.end()) {
            reason                   = "breakpoint";
            details["breakpointIds"] = it->second;
        }
    }
    f.atEntry = f.awaitingEntry && located;
    if (f.atEntry) [[unlikely]] {
        // The first located node of an activation: the entry of its function (3.3).
        f.awaitingEntry           = false;
        const GraphInfo *function = f.function != nullptr ? f.function : f.info;
        if (auto it = functionBreakpointByGraph_.find(function->graphId);
            it != functionBreakpointByGraph_.end() && reason.empty()) {
            reason                   = "breakpoint";
            details["breakpointIds"] = it->second;
            details["function"]      = function->name;
        }
    }
    if (reason.empty() && a.origin != 0 && nodeBreakpointSet_.count(a.origin) != 0) {
        reason                    = "breakpoint";
        details["nodeBreakpoint"] = a.origin;
    }
    if (reason.empty() && entryStopPending_ && located) {
        reason = "entry";
    }
    if (reason.empty() && step_.kind != StepKind::None && stepReached(f, a)) {
        reason = "step";
    }
    if (pauseRequested_.load(std::memory_order_relaxed)) [[unlikely]] {
        pauseRequested_.store(false);
        if (reason.empty()) {
            reason = "pause";
        }
    }
    if ((reason == "step" || reason == "breakpoint") && step_.kind != StepKind::None) {
        // A line step that stops says which lines it went past that did not execute because
        // nothing uses their result (4.2), also when a breakpoint ends it.
        if (json skipped = skippedLines(f, a); !skipped.empty()) {
            details["skipped"] = std::move(skipped);
        }
    }
    if (located) {
        f.lastFile       = f.loc.file;
        f.lastLine       = f.loc.line;
        f.lastFunction   = a.function;
        f.activationLine = LinePosition{f.loc.file, f.loc.line, a.function};
    }
    if (!reason.empty()) {
        // A node that runs a fusion group is the group's tail: the members before it were
        // deferred and did not execute, so the stop says what the group stood for (2). The text
        // is also printed to the debug console by the adapter, which reads `message`.
        if (a.group != nullptr) {
            details["fusion"] = fusionDetails(f, *a.group);
        }
        if (auto ids = details.find("breakpointIds"); ids != details.end()) {
            for (const auto &id : *ids) {
                hitBreakpoints_.insert(id.get<int>());
            }
        }
        stop(reason, std::move(details));
    }
}

json DebugCore::fusionDetails(
    const ShadowFrame &f, const camel::execute::FusionGroupSite &site) {
    json members = json::array();
    json refs    = json::array();
    std::string list;
    for (const camel::runtime::gc_node_ref_t ref : site.members) {
        std::string label = "?";
        if (auto it = f.info->byRef.find(ref); it != f.info->byRef.end()) {
            label = f.info->nodes[it->second].label;
        }
        if (!list.empty()) {
            list += ", ";
        }
        list += label;
        members.push_back(label);
        refs.push_back(ref);
    }
    const std::string name(site.name);
    return json{
        {"name", name},
        {"members", std::move(members)},
        {"refs", std::move(refs)},
        {"message",
         "fusion group `" + name + "` runs here: it computes " + list +
             " in one pass; those nodes do not execute separately."}};
}

void DebugCore::faultRaised(const camel::core::error::RuntimeFault &fault, bool isolated) {
    // A condition `why_not` runs, or any other nested graph of an evaluation, is not the program:
    // its fault is reported by the call that made it, not as a stop of the session (H150).
    if (!observingRun_) {
        return;
    }
    if (stack_.empty()) {
        return;
    }
    if (seenConfig_ != configVersion_.load(std::memory_order_relaxed)) {
        refreshConfig();
    }
    if (isolated ? !filterIsolated_ : !filterUncaught_) {
        return;
    }
    ShadowFrame &f = stack_.back();
    json info{{"isolated", isolated}};
    std::string name    = fault.what();
    std::string message = fault.what();
    std::string text;
    // The diagnostic the scheduler is about to report, without recording it.
    try {
        auto ctx = index_.context();
        if (auto reporter = ctx ? ctx->runtimeErrorReporter() : nullptr) {
            auto site = camel::core::error::makeGraphExecutionSite(
                ctx->sourceContext(),
                f.graph,
                stack_.size());
            if (f.origin != 0) {
                site.cachedOrigin = f.origin;
            }
            auto diagnostic = reporter->describe(fault, site);
            name            = diagnostic.name;
            message         = diagnostic.message;
            text            = diagnostic.toText();
        }
    } catch (...) {
        // Keep the plain text of the fault.
    }
    info["name"]    = name;
    info["message"] = message;
    info["text"]    = text.empty() ? message : text;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lastException_ = info;
    }
    json details{{"exception", info}};
    stop("exception", std::move(details));
}

// ---------------------------------------------------------------------------------------
// Being stopped
// ---------------------------------------------------------------------------------------

json DebugCore::stackJson() {
    // Innermost first; with frames folded, the frames of one activation are shown as one.
    json frames = json::array();
    for (size_t i = stack_.size(); i-- > 0;) {
        const Chain chain = chainOf(stack_[i]);
        frames.push_back(frameJson(chain, i + 1 == stack_.size()));
        i -= chain.size() - 1;
    }
    return frames;
}

json DebugCore::describeStop(const std::string &reason, json details) {
    json j      = std::move(details);
    j["reason"] = reason;
    j["depth"]  = stack_.size();
    if (!stack_.empty()) {
        j["frame"] = frameJson(chainOf(stack_.back()), true);
    }
    return j;
}

void DebugCore::stop(const std::string &reason, json details) {
    if (!details.contains("pipeline"))
        entryStopPending_ = false;
    step_ = Step{};
    handles_.clear();
    json description = describeStop(reason, std::move(details));
    std::function<void(const json *)> listener;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_         = State::Stopped;
        lastStop_      = description;
        resumePending_ = false;
        listener       = stopListener_;
    }
    if (listener) {
        listener(&description);
    }
    json event    = description;
    event["type"] = "stopped";
    pushEvent(std::move(event));

    for (;;) {
        std::pair<std::function<json()>, std::promise<json> *> request;
        bool haveRequest = false;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cond_.wait(lock, [this] {
                return resumePending_ || !requests_.empty() || abort_.load() != 0;
            });
            if (!requests_.empty()) {
                request = std::move(requests_.front());
                requests_.pop_front();
                haveRequest = true;
            } else if (abort_.load() != 0) {
                state_ = State::Running;
            } else {
                step_          = resumeStep_;
                resumePending_ = false;
                state_         = State::Running;
            }
        }
        if (haveRequest) {
            json answer;
            try {
                answer = request.first();
            } catch (const std::exception &e) {
                answer = fail(e.what());
            } catch (...) {
                answer = fail("the request failed");
            }
            request.second->set_value(std::move(answer));
            continue;
        }
        break;
    }
    handles_.clear();
    if (listener) {
        listener(nullptr);
    }
    if (const int abort = abort_.load(); abort != 0) {
        throw RunAborted{abort == 2};
    }
    pushEvent(json{{"type", "continued"}});
}

json DebugCore::runWhileStopped(std::function<json()> fn) {
    std::promise<json> promise;
    auto future = promise.get_future();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != State::Stopped || resumePending_) {
            return fail("the program is not stopped");
        }
        requests_.emplace_back(std::move(fn), &promise);
    }
    cond_.notify_all();
    return future.get();
}

json DebugCore::resume(Step step, bool stepping) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != State::Stopped || resumePending_) {
            return fail("the program is not stopped");
        }
        if (!stepping) {
            resumeStep_    = Step{};
            resumePending_ = true;
        }
    }
    if (stepping) {
        // The step starts from where the program is: read that on its thread.
        json answer = runWhileStopped([this, step]() mutable {
            if (step.kind == StepKind::Pass) {
                if (pipelineGir_.empty())
                    return fail("nextPass: not stopped at a compiler boundary");
                // If no later pass returns a graph, stop at execution entry instead of
                // silently running the program to completion.
                entryStopPending_ = true;
                std::lock_guard<std::mutex> lock(mutex_);
                resumeStep_    = step;
                resumePending_ = true;
                return json{{"ok", true}};
            }
            if (!pipelineGir_.empty())
                return fail(
                    "Use nextPass at a compiler boundary; source stepping requires a runtime "
                    "frame.");
            if (stack_.empty()) {
                return fail("no frame");
            }
            ShadowFrame &f = stack_.back();
            // A step counts what the stack shows when it starts: activations of functions
            // with frames folded, shadow frames otherwise (5.4).
            step.folded       = foldsFrames();
            const auto lineOf = [&](const ShadowFrame &frame) {
                return step.folded
                           ? frame.activationLine
                           : LinePosition{frame.lastFile, frame.lastLine, frame.lastFunction};
            };
            const LinePosition at = lineOf(f);
            step.startActivation  = f.activation;
            step.depth            = step.folded ? f.presentedDepth : stack_.size();
            step.frame            = step.folded ? f.activation : f.id;
            step.file             = at.file;
            step.line             = at.line;
            step.function         = at.function;
            step.foreign          = at.function != f.info->function;
            // The frame shown below this one: the shadow frame under the frames shown as this
            // one, which is the innermost of its own activation.
            const size_t bottom = static_cast<size_t>(chainOf(f).back() - stack_.data());
            if (bottom > 0) {
                const ShadowFrame &caller = stack_[bottom - 1];
                step.caller               = step.folded ? caller.activation : caller.id;
                step.callerLine           = lineOf(caller);
            }
            std::lock_guard<std::mutex> lock(mutex_);
            resumeStep_    = step;
            resumePending_ = true;
            return json{{"ok", true}};
        });
        if (!answer.value("ok", false)) {
            return answer;
        }
    }
    cond_.notify_all();
    return json{{"ok", true}};
}

// ---------------------------------------------------------------------------------------
// Variables
// ---------------------------------------------------------------------------------------

DebugCore::ShadowFrame *DebugCore::frameById(uint64_t id) {
    for (auto &f : stack_) {
        if (f.id == id) {
            return &f;
        }
    }
    return nullptr;
}

int DebugCore::newHandle(Handle handle) {
    handles_.push_back(handle);
    return static_cast<int>(handles_.size());
}

std::optional<DebugCore::Variable>
DebugCore::nodeVariable(ShadowFrame &f, const NodeInfo &n, std::string name) {
    if (n.slot == 0) {
        return std::nullopt;
    }
    const GCNode *node = f.graph->node(n.ref);
    if (node == nullptr || node->dataType() == nullptr) {
        return std::nullopt;
    }
    Variable v;
    v.name             = std::move(name);
    v.view.type        = node->dataType();
    const size_t index = static_cast<size_t>(&n - f.info->nodes.data());
    // Constants and ports have their values from the start; everything else once it ran.
    const bool available = n.slot < 0 || n.kind == GCNodeKind::Port || n.kind == GCNodeKind::Data ||
                           (index < f.done.size() && f.done[index]);
    if (!available) {
        v.evaluated = false;
        v.note      = "<not evaluated>";
        return v;
    }
    v.view.slot = f.frame->get<slot_t>(static_cast<data_idx_t>(n.slot));
    return v;
}

std::vector<DebugCore::Variable> DebugCore::localsOf(const Chain &chain) {
    // `chain` is innermost first; its last frame is the function's own frame, unless an arm
    // replaced it by a tail call. With frames folded the names of the function and of the
    // arms it is in are one list; an arm's own names are there only while it runs (5.4).
    const bool folded         = foldsFrames();
    const ShadowFrame &inner  = *chain.front();
    const GraphInfo *function = folded && inner.function != nullptr ? inner.function : inner.info;
    std::vector<Variable> out;
    std::unordered_map<std::string, size_t> shown; // name -> index in out
    std::unordered_set<std::string> fromArmPort;   // names shown for a port of an arm

    // The values the function captured are in the Captured scope; an arm passed one of them
    // has a port of the same name.
    std::unordered_set<std::string> capturedNames;
    for (gc_node_ref_t ref : function->capturePorts) {
        if (const NodeInfo *n = function->node(ref)) {
            capturedNames.insert(n->label);
        }
    }

    // Ports, outermost frame first: the parameters, then what each arm was passed (named
    // after what its caller passed). The innermost frame's value is the one the running code
    // sees.
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        ShadowFrame &f     = **it;
        const bool arm     = f.info->folds();
        const auto addPort = [&](gc_node_ref_t ref) {
            if (!arm && f.info->capturePorts.count(ref) != 0) {
                return; // a captured value: the Captured scope
            }
            const NodeInfo *n = f.info->node(ref);
            if (n == nullptr) {
                return;
            }
            if (arm && folded && (n->label == "port" || capturedNames.count(n->label) != 0)) {
                return; // a value without a name in the source, or a captured one
            }
            auto v = nodeVariable(f, *n, n->label);
            if (!v) {
                return;
            }
            if (auto known = shown.find(n->label); folded && known != shown.end()) {
                out[known->second] = std::move(*v);
                return;
            }
            shown.emplace(n->label, out.size());
            if (arm) {
                fromArmPort.insert(n->label);
            }
            out.push_back(std::move(*v));
        };
        for (gc_node_ref_t ref : f.graph->withPorts()) {
            addPort(ref);
        }
        for (gc_node_ref_t ref : f.graph->normPorts()) {
            addPort(ref);
        }
        // The state and the element of a loop whose condition or body the code is in.
        const uint32_t at = f.node != nullptr ? f.node->controlRegion : 0;
        for (uint32_t r = 1; r < f.graph->regionCount(); ++r) {
            const GCRegion *region = f.graph->region(r);
            if ((region->kind == GCRegionKind::LoopCond || region->kind == GCRegionKind::LoopBody) &&
                f.info->visibleAt(r, at)) {
                for (gc_node_ref_t ref : f.graph->regionPorts(r)) {
                    addPort(ref);
                }
            }
        }
    }

    // The names declared in the function the frames were built from. A binding is shown from
    // the frame its node is in. One whose node is in none of them is left out when it is a
    // value of another frame of the function: of an arm that is not running, or, in the view
    // by subgraphs, of the function's frame below an arm. Otherwise the frame says why it has
    // no value. A generic function has one binding per instance for each declaration: the
    // one whose node is in this frame is this instance's.
    std::vector<const Binding *> bindings;
    std::unordered_set<std::string> chainGraphs;
    for (const ShadowFrame *f : chain) {
        chainGraphs.insert(f->info->graphId);
    }
    // An arm that replaced the function's frame by a tail call holds only what it uses.
    const bool functionReplaced = folded && chain.back()->info != function;
    std::unordered_map<std::string, bool> armGraphs; // by graph id: a running arm
    std::unordered_set<origin_t> built;              // nodes in the graph before the passes
    {
        std::lock_guard lock(indexMutex_);
        bindings = index_.bindingsOfFunction(folded ? function->function : inner.info->function);
        for (const Binding *b : bindings) {
            armGraphs.emplace(b->graph, index_.isArm(b->graph));
            if (index_.inInitialGraph(b->node)) {
                built.insert(b->node);
            }
        }
    }
    const bool functionInChain = std::any_of(chain.begin(), chain.end(), [&](const ShadowFrame *f) {
        return f->info == function;
    });
    std::unordered_map<origin_t, size_t> byDeclaration; // decl -> index in out
    for (const Binding *b : bindings) {
        ShadowFrame *home     = nullptr;
        const NodeInfo *found = nullptr;
        for (ShadowFrame *f : chain) {
            if (auto at = f->info->byOrigin.find(b->node); at != f->info->byOrigin.end()) {
                home  = f;
                found = &f->info->nodes[at->second];
                break;
            }
        }
        if (found != nullptr &&
            !home->info->visibleAt(
                found->controlRegion, home->node != nullptr ? home->node->controlRegion : 0)) {
            continue; // a name of an arm that does not run (or is over)
        }
        auto known = byDeclaration.find(b->decl);
        if (found == nullptr) {
            const bool ofThisChain = b->graph.empty() || chainGraphs.count(b->graph) != 0;
            if (!ofThisChain && armGraphs[b->graph]) {
                continue; // a name of an arm that is not running
            }
            if (!ofThisChain && !functionInChain && b->graph == function->graphId &&
                !functionReplaced) {
                continue; // in the view by subgraphs: a name of the function's own frame
            }
            std::string note;
            if (functionReplaced && b->graph == function->graphId) {
                note = "<not held: the arm that runs replaced the function's frame and does not "
                       "use it>";
            } else if (built.count(b->node) == 0) {
                // The builder left it out: what the source computes there is used by nothing.
                note = "<not in the graph: nothing uses its value>";
            } else {
                note = "<not in the graph: a pass removed it>";
            }
            if (known == byDeclaration.end() && shown.count(b->name) == 0) {
                Variable v;
                v.name      = b->name;
                v.evaluated = false;
                v.note      = std::move(note);
                v.view.type = b->type;
                byDeclaration.emplace(b->decl, out.size());
                out.push_back(std::move(v));
            }
            continue;
        }
        auto v = nodeVariable(*home, *found, b->name);
        if (!v) {
            continue;
        }
        if (known != byDeclaration.end()) {
            out[known->second] = std::move(*v); // replaces "not in the graph" of another instance
        } else if (
            auto port = shown.find(b->name);
            port != shown.end() && fromArmPort.count(b->name) != 0) {
            // The arm's port was this name, passed in: one entry, from the binding.
            byDeclaration.emplace(b->decl, port->second);
            out[port->second] = std::move(*v);
        } else {
            byDeclaration.emplace(b->decl, out.size());
            out.push_back(std::move(*v));
        }
    }
    return out;
}

std::vector<DebugCore::Variable> DebugCore::capturedOf(const Chain &chain) {
    const bool folded         = foldsFrames();
    const ShadowFrame &inner  = *chain.front();
    const GraphInfo *function = folded && inner.function != nullptr ? inner.function : inner.info;
    std::vector<Variable> out;
    std::unordered_map<std::string, size_t> shown;
    std::unordered_set<std::string> capturedNames;
    for (gc_node_ref_t ref : function->capturePorts) {
        if (const NodeInfo *n = function->node(ref)) {
            capturedNames.insert(n->label);
        }
    }
    const auto add = [&](ShadowFrame &f, gc_node_ref_t ref) {
        const NodeInfo *n = f.info->node(ref);
        if (n == nullptr) {
            return;
        }
        if (auto v = nodeVariable(f, *n, n->label)) {
            if (auto known = shown.find(n->label); known != shown.end()) {
                out[known->second] = std::move(*v); // the innermost frame's
            } else {
                shown.emplace(n->label, out.size());
                out.push_back(std::move(*v));
            }
        }
    };
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        ShadowFrame &f = **it;
        if (f.info->folds() && folded) {
            // What an arm was passed of the function's captured values.
            for (gc_node_ref_t ref : f.graph->withPorts()) {
                if (const NodeInfo *n = f.info->node(ref); n && capturedNames.count(n->label)) {
                    add(f, ref);
                }
            }
            continue;
        }
        // Closure nodes, then the captures a directly called function has as `with` ports.
        for (gc_node_ref_t ref : f.graph->closureNodes()) {
            add(f, ref);
        }
        for (gc_node_ref_t ref : f.graph->withPorts()) {
            if (f.info->capturePorts.count(ref) != 0) {
                add(f, ref);
            }
        }
    }
    return out;
}

std::vector<DebugCore::Variable> DebugCore::inlinedOf(const Chain &chain) {
    std::vector<Variable> out;
    std::unordered_set<std::string> shown;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        ShadowFrame &f = **it;
        for (const NodeInfo &n : f.info->nodes) {
            if (n.origin == 0) {
                continue;
            }
            std::vector<const Binding *> bindings;
            std::string owner;
            {
                std::lock_guard lock(indexMutex_);
                bindings = index_.bindingsOfNode(n.origin);
                for (const Binding *b : bindings) {
                    if (b->function != f.info->function && b->function >= 0) {
                        owner = index_.function(b->function)->name;
                    }
                }
            }
            for (const Binding *b : bindings) {
                if (b->function == f.info->function || b->function < 0) {
                    continue;
                }
                std::string name = owner + "." + b->name;
                if (shown.count(name) != 0) {
                    continue;
                }
                if (auto v = nodeVariable(f, n, name)) {
                    shown.insert(std::move(name));
                    out.push_back(std::move(*v));
                }
            }
        }
    }
    return out;
}

std::vector<DebugCore::Variable> DebugCore::nodesOf(const Chain &chain) {
    // The graph view's values; with frames folded, of every graph of the frame, each node
    // of a subgraph prefixed with what the subgraph is.
    std::vector<Variable> out;
    const ShadowFrame &inner  = *chain.front();
    const GraphInfo *function = inner.function != nullptr ? inner.function : inner.info;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        ShadowFrame &f = **it;
        const std::string prefix =
            chain.size() > 1 && f.info != function ? f.info->display + " " : std::string();
        for (const NodeInfo &n : f.info->nodes) {
            std::string name =
                prefix + "%" + std::to_string(n.ref) + " " + kindName(n.kind) + " " + n.label;
            if (auto v = nodeVariable(f, n, std::move(name))) {
                out.push_back(std::move(*v));
            }
        }
    }
    return out;
}

json DebugCore::variableJson(const Variable &v, uint64_t frame) {
    json j{{"name", v.name}, {"evaluated", v.evaluated}};
    if (!v.evaluated) {
        j["value"]     = v.note;
        j["type"]      = typeText(v.view.type);
        j["reference"] = 0;
        return j;
    }
    Rendered r     = render(v.view);
    j["value"]     = r.value;
    j["type"]      = r.type;
    j["reference"] = r.expandable ? newHandle({Handle::Kind::Value, frame, v.view}) : 0;
    return j;
}

json DebugCore::variablesJson(std::vector<Variable> variables, uint64_t frame) {
    // A name shadowed in the same function appears once per declaration: tell them apart.
    std::unordered_map<std::string, int> seen;
    json out = json::array();
    for (auto &v : variables) {
        const int n = ++seen[v.name];
        if (n > 1) {
            v.name += " (" + std::to_string(n) + ")";
        }
        out.push_back(variableJson(v, frame));
    }
    return out;
}

json DebugCore::scopesJson(uint64_t frameId) {
    ShadowFrame *f = frameById(frameId);
    if (f == nullptr) {
        return fail("no such frame");
    }
    json scopes    = json::array();
    const auto add = [&](const char *name, Handle::Kind kind, size_t count, bool expensive) {
        scopes.push_back(
            json{
                {"name", name},
                {"reference", newHandle({kind, frameId, {}})},
                {"count", count},
                {"expensive", expensive}});
    };
    const Chain chain = chainOf(*f);
    add("Locals", Handle::Kind::Locals, localsOf(chain).size(), false);
    if (const size_t n = capturedOf(chain).size(); n > 0) {
        add("Captured", Handle::Kind::Captured, n, false);
    }
    if (const size_t n = inlinedOf(chain).size(); n > 0) {
        add("Inlined", Handle::Kind::Inlined, n, false);
    }
    add("Nodes", Handle::Kind::Nodes, nodesOf(chain).size(), true);
    return json{{"ok", true}, {"scopes", std::move(scopes)}};
}

json DebugCore::handleJson(int reference) {
    if (reference <= 0 || static_cast<size_t>(reference) > handles_.size()) {
        return fail("no such variables reference (references end when the program resumes)");
    }
    const Handle handle = handles_[static_cast<size_t>(reference) - 1];
    ShadowFrame *f      = frameById(handle.frame);
    if (f == nullptr) {
        return fail("the frame is gone");
    }
    json variables;
    switch (handle.kind) {
    case Handle::Kind::Locals:
        variables = variablesJson(localsOf(chainOf(*f)), handle.frame);
        break;
    case Handle::Kind::Captured:
        variables = variablesJson(capturedOf(chainOf(*f)), handle.frame);
        break;
    case Handle::Kind::Inlined:
        variables = variablesJson(inlinedOf(chainOf(*f)), handle.frame);
        break;
    case Handle::Kind::Nodes:
        variables = variablesJson(nodesOf(chainOf(*f)), handle.frame);
        break;
    case Handle::Kind::Value: {
        size_t total = 0;
        std::vector<Variable> children;
        for (auto &m : members(handle.view, kMaxChildren, &total)) {
            Variable v;
            v.name = std::move(m.name);
            v.view = m.value;
            children.push_back(std::move(v));
        }
        variables = variablesJson(std::move(children), handle.frame);
        if (total > kMaxChildren) {
            variables.push_back(
                json{
                    {"name", "..."},
                    {"value", std::to_string(total - kMaxChildren) + " more not shown"},
                    {"type", ""},
                    {"reference", 0},
                    {"evaluated", true}});
        }
    } break;
    }
    return json{{"ok", true}, {"variables", std::move(variables)}};
}

bool DebugCore::resolveValueAt(
    ShadowFrame &f, const std::string &expression, ResolvedValue &out, std::string &why) {
    // name ( '::' name )* ( '.' member | '[' index ']' )*: a full name such as
    // `lib::Config::limit` names a value of a module's top level (design principles 10.2).
    const auto isNameChar = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_';
    };
    std::string text;
    for (char c : expression) {
        if (c != ' ' && c != '\t') {
            text += c;
        }
    }
    size_t pos = 0;
    for (;;) {
        while (pos < text.size() && isNameChar(text[pos])) {
            ++pos;
        }
        if (pos + 2 < text.size() && text[pos] == ':' && text[pos + 1] == ':' &&
            isNameChar(text[pos + 2])) {
            pos += 2;
            continue;
        }
        break;
    }
    const std::string name = text.substr(0, pos);
    std::vector<std::string> path;
    bool wellFormed = !name.empty() && !(name[0] >= '0' && name[0] <= '9');
    while (wellFormed && pos < text.size()) {
        const char open = text[pos++];
        size_t start    = pos;
        while (pos < text.size() && isNameChar(text[pos])) {
            ++pos;
        }
        if (pos == start) {
            wellFormed = false;
        } else if (open == '.') {
            path.push_back(text.substr(start, pos - start));
        } else if (open == '[' && pos < text.size() && text[pos] == ']') {
            path.push_back(text.substr(start, pos - start));
            ++pos;
        } else {
            wellFormed = false;
        }
    }
    if (!wellFormed) {
        why = "Only a name, optionally followed by member accesses (p.x, t.0, xs[2]), can be "
              "evaluated.";
        return false;
    }
    // The name: a local or parameter, then a captured name, then an inlined one. Of several
    // declarations of the name, one that has a value is preferred, the latest of them. A
    // captured value is shown by its full name and found by it, or by the end of it as the
    // source writes it where it is used (`limit`, `Config::limit`).
    enum class Match { Exact, Inlined, FullName };
    std::optional<Variable> found;
    const auto consider = [&](std::vector<Variable> candidates, Match match) {
        for (auto &v : candidates) {
            std::string_view shown = v.name;
            if (match == Match::Inlined) {
                if (auto dot = shown.rfind('.'); dot != std::string_view::npos) {
                    shown = shown.substr(dot + 1);
                }
            }
            const bool matches =
                shown == name || (match == Match::FullName && shown.size() > name.size() + 2 &&
                                  shown.ends_with(name) &&
                                  shown.substr(0, shown.size() - name.size()).ends_with("::"));
            if (!matches) {
                continue;
            }
            if (!found || v.evaluated || !found->evaluated) {
                found = std::move(v);
            }
        }
    };
    const Chain chain = chainOf(f);
    consider(localsOf(chain), Match::Exact);
    if (!found) {
        consider(capturedOf(chain), Match::FullName);
    }
    if (!found) {
        consider(inlinedOf(chain), Match::Inlined);
    }
    if (!found) {
        why = "'" + name + "' is not a name of this frame.";
        return false;
    }
    out.variable = std::move(*found);
    out.hasPath  = !path.empty();
    out.evaluated = out.variable.evaluated;
    if (!out.evaluated) {
        return true;
    }
    ValueView view = out.variable.view;
    std::string walked = name;
    for (const auto &step : path) {
        auto next = member(view, step);
        if (!next) {
            why = "'" + walked + "' has no member '" + step + "'.";
            return false;
        }
        view = *next;
        walked += "." + step;
    }
    out.view = view;
    return true;
}

json DebugCore::evaluateJson(uint64_t frameId, const std::string &expression) {
    ShadowFrame *f = frameId == 0 && !stack_.empty() ? &stack_.back() : frameById(frameId);
    if (f == nullptr) {
        return fail("no such frame");
    }
    ResolvedValue resolved;
    std::string why;
    if (!resolveValueAt(*f, expression, resolved, why)) {
        return fail(why);
    }
    const Variable &found = resolved.variable;
    if (!resolved.evaluated) {
        if (resolved.hasPath) {
            return fail("'" + found.name + "' has no value yet: " + found.note);
        }
        return json{
            {"ok", true},
            {"result", found.note},
            {"type", typeText(found.view.type)},
            {"reference", 0},
            {"evaluated", false}};
    }
    Rendered r = render(resolved.view);
    return json{
        {"ok", true},
        {"result", r.value},
        {"type", r.type},
        {"reference", r.expandable ? newHandle({Handle::Kind::Value, f->id, resolved.view}) : 0},
        {"evaluated", true}};
}

// ---------------------------------------------------------------------------------------
// The graph view
// ---------------------------------------------------------------------------------------

json DebugCore::graphJson(uint64_t frameId) {
    ShadowFrame *f = frameId == 0 && !stack_.empty() ? &stack_.back() : frameById(frameId);
    if (f == nullptr) {
        return fail("no such frame");
    }
    json nodes = json::array();
    json edges = json::array();
    std::ostringstream dot;
    dot << "digraph \"" << f->info->name
        << "\" {\n  rankdir=TB;\n  node [shape=box, style=filled, "
           "fontname=\"monospace\"];\n";
    for (size_t i = 0; i < f->info->nodes.size(); ++i) {
        const NodeInfo &n = f->info->nodes[i];
        const char *state = "pending";
        if (f->node == &n) {
            state = "current";
        } else if (n.kind == GCNodeKind::Data || n.kind == GCNodeKind::Port) {
            state = "static";
        } else if (f->done[i]) {
            state = "done";
        } else if (!n.executes) {
            state = "static";
        }
        json j{
            {"ref", n.ref},
            {"kind", kindName(n.kind)},
            {"label", n.label},
            {"origin", n.origin},
            {"slot", n.slot},
            {"state", state}};
        if (n.loc.valid()) {
            std::lock_guard lock(indexMutex_);
            j["path"] = index_.filePath(n.loc.file);
            j["line"] = n.loc.line;
        }
        json names = json::array();
        {
            std::lock_guard lock(indexMutex_);
            for (const Binding *b : index_.bindingsOfNode(n.origin)) {
                names.push_back(b->name);
            }
        }
        if (n.kind == GCNodeKind::Port) {
            names.push_back(n.label);
        }
        j["names"] = names;
        if (auto v = nodeVariable(*f, n, ""); v && v->evaluated && f->node != &n) {
            j["value"] = render(v->view, 60).value;
        }
        const char *color = std::string_view(state) == "current"  ? "gold"
                            : std::string_view(state) == "done"   ? "palegreen"
                            : std::string_view(state) == "static" ? "gray92"
                                                                  : "white";
        std::string label = std::string(kindName(n.kind)) + " " + n.label;
        if (n.loc.valid()) {
            label += "\\nline " + std::to_string(n.loc.line);
        }
        std::string escaped;
        for (char c : label) {
            if (c == '"') {
                escaped += "\\\"";
            } else {
                escaped += c;
            }
        }
        dot << "  n" << n.ref << " [label=\"" << escaped << "\", fillcolor=" << color
            << (std::string_view(state) == "current" ? ", penwidth=3" : "") << "];\n";
        const auto addEdges = [&](std::span<const gc_node_ref_t> inputs, const char *kind) {
            for (gc_node_ref_t from : inputs) {
                edges.push_back(json{{"from", from}, {"to", n.ref}, {"kind", kind}});
                dot << "  n" << from << " -> n" << n.ref
                    << (std::string_view(kind) == "ctrl"   ? " [style=dashed]"
                        : std::string_view(kind) == "with" ? " [style=dotted]"
                                                           : "")
                    << ";\n";
            }
        };
        addEdges(f->graph->normInputsOf(n.ref), "norm");
        addEdges(f->graph->withInputsOf(n.ref), "with");
        addEdges(f->graph->ctrlInputsOf(n.ref), "ctrl");
        nodes.push_back(std::move(j));
    }
    dot << "}\n";
    json out{
        {"ok", true},
        {"graph", f->info->name},
        {"graphId", f->info->graphId},
        {"frame", f->id},
        {"nodes", std::move(nodes)},
        {"edges", std::move(edges)},
        {"dot", dot.str()}};
    out["current"] = f->node != nullptr ? json(f->node->ref) : json(nullptr);
    return out;
}

// ---------------------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------------------

json DebugCore::command(const std::string &name, const json &args) {
    try {
        if (name == "status") {
            std::lock_guard<std::mutex> lock(mutex_);
            const char *state = state_ == State::Idle      ? "idle"
                                : state_ == State::Loaded  ? "loaded"
                                : state_ == State::Running ? "running"
                                : state_ == State::Stopped ? "stopped"
                                                           : "finished";
            return json{
                {"ok", true},
                {"state", state},
                {"stop", lastStop_},
                {"frames", foldsFrames() ? "functions" : "subgraphs"},
                {"lastEvent", nextEventSeq_ - 1}};
        }
        if (name == "setBreakpoints") {
            return setLineBreakpoints(args);
        }
        if (name == "setFunctionBreakpoints") {
            return setFunctionBreakpoints(args);
        }
        if (name == "setNodeBreakpoints") {
            std::vector<origin_t> origins;
            if (args.contains("origins") && args["origins"].is_array()) {
                for (const auto &o : args["origins"]) {
                    if (o.is_number_unsigned()) {
                        origins.push_back(o.get<origin_t>());
                    }
                }
            }
            std::lock_guard<std::mutex> lock(mutex_);
            nodeBreakpoints_ = std::move(origins);
            configVersion_.fetch_add(1);
            return json{{"ok", true}};
        }
        if (name == "setExceptionFilters") {
            bool uncaught = false, isolated = false;
            if (args.contains("filters") && args["filters"].is_array()) {
                for (const auto &f : args["filters"]) {
                    if (f == "uncaught") {
                        uncaught = true;
                    } else if (f == "isolated") {
                        isolated = true;
                    }
                }
            }
            std::lock_guard<std::mutex> lock(mutex_);
            stopOnUncaught_ = uncaught;
            stopOnIsolated_ = isolated;
            configVersion_.fetch_add(1);
            return json{{"ok", true}};
        }
        if (name == "setPassBreakpoints") {
            const auto values = args.find("indices");
            if (values == args.end() || !values->is_array())
                return fail("indices must be an array");
            std::set<size_t> indices;
            for (const auto &v : *values) {
                if (!v.is_number_integer() || v.get<int64_t>() < 0)
                    return fail("pass indices must be nonnegative integers");
                indices.insert(v.get<size_t>());
            }
            std::lock_guard<std::mutex> lock(mutex_);
            passBreakpoints_ = std::move(indices);
            return json{{"ok", true}};
        }
        if (name == "nextPass") {
            Step step;
            step.kind = StepKind::Pass;
            return resume(step, true);
        }
        if (name == "pipelineGraph") {
            return runWhileStopped([this] {
                if (pipelineGir_.empty())
                    return fail("not stopped at a compiler boundary");
                return json{
                    {"ok", true},
                    {"gir", pipelineGir_},
                    {"pipeline", lastStop_["pipeline"]}};
            });
        }
        if (name == "configure") {
            if (auto frames = args.find("frames"); frames != args.end()) {
                // How the stack shows the subgraphs of a function (5.4); takes effect at the
                // next question about the stack, also while the program is stopped.
                if (!frames->is_string() || (*frames != "functions" && *frames != "subgraphs")) {
                    return fail("configure: frames is \"functions\" or \"subgraphs\"");
                }
                foldFrames_.store(*frames == "functions");
            }
            std::lock_guard<std::mutex> lock(mutex_);
            if (auto it = args.find("stopOnPassEntry"); it != args.end()) {
                if (!it->is_boolean())
                    return fail("stopOnPassEntry must be boolean");
                stopOnPassEntry_ = it->get<bool>();
            }
            if (args.contains("stopOnEntry") && args["stopOnEntry"].is_boolean()) {
                stopOnEntry_ = args["stopOnEntry"].get<bool>();
            }
            // For the next run only (a node step asked for before the program runs).
            if (args.value("stopOnEntryOnce", false)) {
                stopOnEntryOnce_ = true;
            }
            return json{{"ok", true}};
        }
        if (name == "continue") {
            return resume(Step{}, false);
        }
        if (name == "pause") {
            std::lock_guard<std::mutex> lock(mutex_);
            if (state_ == State::Stopped) {
                return json{{"ok", true}, {"alreadyStopped", true}};
            }
            if (state_ != State::Running) {
                return fail("the program is not running");
            }
            pauseRequested_.store(true);
            return json{{"ok", true}};
        }
        if (name == "step") {
            const std::string unit = args.value("unit", "line");
            const std::string mode = args.value("mode", "over");
            Step step;
            if (mode == "out") {
                step.kind = StepKind::Out;
            } else if (unit == "node") {
                step.kind = StepKind::Node;
            } else if (unit == "line") {
                step.kind = StepKind::Line;
            } else {
                return fail("step: unit is 'line' or 'node'");
            }
            if (mode != "over" && mode != "in" && mode != "out") {
                return fail("step: mode is 'over', 'in' or 'out'");
            }
            step.into  = mode == "in";
            step.graph = args.value("graph", "");
            return resume(step, true);
        }
        if (name == "terminate" || name == "restart") {
            abort_.store(name == "restart" ? 2 : 1);
            cond_.notify_all();
            return json{{"ok", true}};
        }
        if (name == "stackTrace") {
            return runWhileStopped([this] { return json{{"ok", true}, {"frames", stackJson()}}; });
        }
        if (name == "scopes") {
            const uint64_t frame = args.value("frame", uint64_t{0});
            return runWhileStopped([this, frame] {
                return scopesJson(frame == 0 && !stack_.empty() ? stack_.back().id : frame);
            });
        }
        if (name == "variables") {
            const int reference = args.value("reference", 0);
            return runWhileStopped([this, reference] { return handleJson(reference); });
        }
        if (name == "evaluate") {
            const uint64_t frame         = args.value("frame", uint64_t{0});
            const std::string expression = args.value("expression", "");
            return runWhileStopped(
                [this, frame, expression] { return evaluateJson(frame, expression); });
        }
        if (name == "explain") {
            return runWhileStopped([this, args] { return explainJson(args); });
        }
        if (name == "why_not" || name == "whyNot") {
            return runWhileStopped([this, args] { return whyNotJson(args); });
        }
        if (name == "memorySnapshot" || name == "regionMemory" || name == "regionObjects" ||
            name == "allocationBreakpoints") {
            return memoryCommand(name, args);
        }
        if (name == "structuredGraph") {
            const uint64_t frame = args.value("frame", uint64_t{0});
            return runWhileStopped([this, frame] {
                if (!pipelineGir_.empty())
                    return json{
                        {"ok", true},
                        {"gir", pipelineGir_},
                        {"pipeline", lastStop_["pipeline"]}};
                auto *f = frame == 0 && !stack_.empty() ? &stack_.back() : frameById(frame);
                if (!f)
                    return fail("no runtime frame");
                return json{
                    {"ok", true},
                    {"gir", dumpJsonRuntimeGraph(f->graph, index_.context())}};
            });
        }
        if (name == "graph") {
            const uint64_t frame = args.value("frame", uint64_t{0});
            return runWhileStopped([this, frame] { return graphJson(frame); });
        }
        if (name == "exceptionInfo") {
            std::lock_guard<std::mutex> lock(mutex_);
            if (state_ != State::Stopped || !lastStop_.is_object() ||
                lastStop_.value("reason", "") != "exception") {
                return fail("the program is not stopped at a fault");
            }
            return json{{"ok", true}, {"exception", lastException_}};
        }
        return fail("unknown command: " + name);
    } catch (const json::exception &e) {
        return fail(std::string("malformed arguments: ") + e.what());
    } catch (const std::exception &e) {
        return fail(e.what());
    }
}

} // namespace debugger::core
