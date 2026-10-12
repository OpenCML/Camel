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
 * Commands (DebugCore::command), all answered with {"ok": true, ...} or
 * {"ok": false, "error": "..."}:
 *
 *   status                                   state, the last stop
 *   setBreakpoints {path, lines}             replaces the line breakpoints of a file
 *   setNodeBreakpoints {origins}             replaces the node breakpoints
 *   setExceptionFilters {filters}            "uncaught", "isolated"
 *   configure {stopOnEntry}
 *   continue | pause
 *   step {unit: "line"|"node", mode: "over"|"in"|"out", graph?}
 *   terminate | restart                      ends the run from inside (RunAborted)
 * and, only while the program is stopped (executed on its thread):
 *   stackTrace | scopes {frame} | variables {reference} | evaluate {frame, expression}
 *   graph {frame} | exceptionInfo
 *
 * Events (DebugCore::events): loaded, started, stopped, continued, breakpoint, finished.
 */

#include "debug_core.h"

#include "camel/core/context/frame.h"
#include "camel/core/error/runtime.h"

#include <algorithm>
#include <chrono>
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

const char *lineStateMessage(LineState state) {
    switch (state) {
    case LineState::Executable:
        return "";
    case LineState::NoCode:
        return "No code on this line.";
    case LineState::NotDepended:
        return "The code on this line is not executed: nothing depends on its result. "
               "Evaluation is driven by dependencies; to run it, use its result or put it in "
               "a sync block.";
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
    const auto ready = [&] { return !events_.empty() && events_.back()["seq"].get<uint64_t>() > after; };
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
        std::lock_guard<std::mutex> lock(indexMutex_);
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
        std::lock_guard<std::mutex> lock(indexMutex_);
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
    pending_          = Pending::None;
    runIndexed_       = false;
    step_             = Step{};
    seenConfig_       = 0;
    abort_.store(0);
    pauseRequested_.store(false);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_            = State::Running;
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

void DebugCore::verify(LineBreakpoint &bp) {
    // mutex_ is held by the caller; the index has its own lock.
    bp.origins.clear();
    std::lock_guard<std::mutex> lock(indexMutex_);
    if (!index_.isCompiled()) {
        bp.verified = false;
        bp.message  = "The program has not been compiled yet.";
        return;
    }
    LineResolution r = index_.resolveLine(bp.path, bp.line);
    bp.verified      = r.state == LineState::Executable;
    bp.message       = lineStateMessage(r.state);
    bp.origins       = std::move(r.origins);
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
    nodeBreakpointSet_.clear();
    nodeBreakpointSet_.insert(nodeBreakpoints_.begin(), nodeBreakpoints_.end());
    filterUncaught_ = stopOnUncaught_;
    filterIsolated_ = stopOnIsolated_;
}

// ---------------------------------------------------------------------------------------
// The stack, fed by the scheduler
// ---------------------------------------------------------------------------------------

void DebugCore::activationEntered() {
    activationBase_.push_back(stack_.size());
    pending_ = Pending::Enter;
}

void DebugCore::activationLeft() {
    if (activationBase_.empty()) {
        return;
    }
    const size_t base = activationBase_.back();
    activationBase_.pop_back();
    if (stack_.size() > base) {
        stack_.resize(base);
    }
    pending_ = Pending::None;
}

void DebugCore::tailCall() { pending_ = Pending::Tail; }

void DebugCore::firstNodeOfRun(GCGraph *root) {
    runIndexed_ = true;
    {
        std::lock_guard<std::mutex> lock(indexMutex_);
        index_.running(root);
    }
    // The graph the scheduler was given is known now: breakpoints are answered again.
    reverifyAll();
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
        std::lock_guard<std::mutex> lock(indexMutex_);
        f.info = &index_.graph(graph);
    }
    f.lastFunction = f.info->function;
    f.done.assign(f.info->nodes.size(), false);
    stack_.push_back(std::move(f));
    return stack_.back();
}

void DebugCore::beforeNode(GCGraph *graph, Frame *frame, gc_node_ref_t node) {
    if (pending_ == Pending::Enter || stack_.empty()) {
        pushFrame(graph, frame);
    } else if (pending_ == Pending::Tail || stack_.back().frame != frame ||
               stack_.back().graph != graph) {
        // A tail call: the callee's frame takes the caller's place (5.2).
        const uint32_t replaced = stack_.back().tailReplaced + 1;
        stack_.pop_back();
        pushFrame(graph, frame).tailReplaced = replaced;
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
    arrive(f, arrival);
}

void DebugCore::beforeInstruction(const camel::execute::InstructionSite &site) {
    GCGraph *graph = site.frame ? site.frame->runtimeGraph() : nullptr;
    if (graph == nullptr) {
        return;
    }
    if (pending_ == Pending::Enter || stack_.empty()) {
        pushFrame(graph, site.frame);
    } else {
        // What the instruction executed last in this activation did to the call stack.
        switch (stack_.back().lastEffect) {
        case InstructionEffect::Call:
            stack_.back().lastEffect = InstructionEffect::None;
            pushFrame(graph, site.frame);
            break;
        case InstructionEffect::Return:
            stack_.pop_back();
            if (stack_.empty()) {
                pushFrame(graph, site.frame);
            }
            stack_.back().lastEffect = InstructionEffect::None;
            break;
        case InstructionEffect::TailCall: {
            const uint32_t replaced = stack_.back().tailReplaced + 1;
            stack_.pop_back();
            pushFrame(graph, site.frame).tailReplaced = replaced;
        } break;
        default:
            break;
        }
    }
    pending_       = Pending::None;
    ShadowFrame &f = stack_.back();
    if (f.frame != site.frame || f.graph != graph) {
        // Not expected; follow the interpreter rather than show a frame it is not in.
        const uint32_t replaced = f.tailReplaced;
        stack_.pop_back();
        pushFrame(graph, site.frame).tailReplaced = replaced;
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
            std::lock_guard<std::mutex> lock(indexMutex_);
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
    const size_t depth = stack_.size();
    const bool located = a.loc != nullptr && a.loc->valid();
    switch (step_.kind) {
    case StepKind::None:
        return false;
    case StepKind::Node:
        if (!step_.graph.empty()) {
            return f.info->stableId == step_.graph;
        }
        return step_.into || depth <= step_.depth;
    case StepKind::Line:
        // DESIGN-QUESTION(line-step-order): the next line in execution order, not in the text.
        if (!located) {
            return false;
        }
        if (!step_.into && depth > step_.depth) {
            return false;
        }
        if (f.id != step_.frame) {
            // The caller after a return, a callee (step in), or the frame that replaced this
            // one by a tail call. DESIGN-QUESTION(step-over-tail-call)
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
        return f.id == step_.frame && step_.foreign && a.function != step_.function;
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
    // once per visit, not once per node of the line.
    const bool entering = located && !(f.lastFile == f.loc.file && f.lastLine == f.loc.line);
    std::string reason;
    json details = json::object();
    if (entering) {
        if (auto it = lineBreakpointByOrigin_.find(a.origin); it != lineBreakpointByOrigin_.end()) {
            reason                   = "breakpoint";
            details["breakpointIds"] = it->second;
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
    if (located) {
        f.lastFile     = f.loc.file;
        f.lastLine     = f.loc.line;
        f.lastFunction = a.function;
    }
    if (!reason.empty()) {
        stop(reason, std::move(details));
    }
}

void DebugCore::faultRaised(const camel::core::error::RuntimeFault &fault, bool isolated) {
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

json DebugCore::frameJson(const ShadowFrame &f, bool top) {
    json j{{"id", f.id}, {"graph", f.info->name}, {"graphId", f.info->stableId}};
    std::string name = f.info->name;
    if (f.tailReplaced > 0) {
        // DESIGN-QUESTION(tail-call-stack): the callers a tail call replaced are gone; say so
        // instead of inventing them.
        name += " (tail call, " + std::to_string(f.tailReplaced) +
                (f.tailReplaced == 1 ? " frame replaced)" : " frames replaced)");
        j["tailReplaced"] = f.tailReplaced;
    }
    if (f.loc.valid()) {
        int function;
        std::string path;
        {
            std::lock_guard<std::mutex> lock(indexMutex_);
            function = index_.functionOf(f.loc);
            path     = index_.filePath(f.loc.file);
            if (function != f.info->function && function >= 0 && f.info->function >= 0) {
                const std::string &from = index_.function(function)->name;
                name += " [inlined code of " + from + "]";
                j["inlinedFrom"] = from;
            }
        }
        j["path"]      = path;
        j["line"]      = f.loc.line;
        j["column"]    = f.loc.column;
        j["endLine"]   = f.loc.endLine;
        j["endColumn"] = f.loc.endColumn;
    }
    j["name"] = name;
    j["top"]  = top;
    if (f.node != nullptr) {
        j["node"] = json{
            {"ref", f.node->ref},
            {"kind", kindName(f.node->kind)},
            {"label", f.node->label},
            {"origin", f.node->origin}};
    }
    return j;
}

json DebugCore::stackJson() {
    json frames = json::array();
    for (size_t i = stack_.size(); i-- > 0;) {
        frames.push_back(frameJson(stack_[i], i + 1 == stack_.size()));
    }
    return frames;
}

json DebugCore::describeStop(const std::string &reason, json details) {
    json j       = std::move(details);
    j["reason"]  = reason;
    j["depth"]   = stack_.size();
    if (!stack_.empty()) {
        j["frame"] = frameJson(stack_.back(), true);
    }
    return j;
}

void DebugCore::stop(const std::string &reason, json details) {
    entryStopPending_ = false;
    step_             = Step{};
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
    json event  = description;
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
                request     = std::move(requests_.front());
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
            if (stack_.empty()) {
                return fail("no frame");
            }
            const ShadowFrame &f = stack_.back();
            step.depth           = stack_.size();
            step.frame           = f.id;
            step.file            = f.lastFile;
            step.line            = f.lastLine;
            step.function        = f.lastFunction;
            step.foreign         = f.lastFunction != f.info->function;
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
    if (node == nullptr || node->dataType == nullptr) {
        return std::nullopt;
    }
    Variable v;
    v.name      = std::move(name);
    v.view.type = node->dataType;
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

std::vector<DebugCore::Variable> DebugCore::localsOf(ShadowFrame &f) {
    std::vector<Variable> out;
    const auto addPort = [&](gc_node_ref_t ref) {
        if (const NodeInfo *n = f.info->node(ref)) {
            if (auto v = nodeVariable(f, *n, n->label)) {
                out.push_back(std::move(*v));
            }
        }
    };
    for (gc_node_ref_t ref : f.graph->withPorts()) {
        addPort(ref);
    }
    for (gc_node_ref_t ref : f.graph->normPorts()) {
        addPort(ref);
    }
    // The names declared in the function this graph was built from. A generic function has
    // one binding per instance for each declaration: the one whose node is in this graph is
    // this instance's.
    std::vector<const Binding *> bindings;
    {
        std::lock_guard<std::mutex> lock(indexMutex_);
        bindings = index_.bindingsOfFunction(f.info->function);
    }
    std::unordered_map<origin_t, size_t> byDeclaration; // decl -> index in out
    for (const Binding *b : bindings) {
        const NodeInfo *found = nullptr;
        auto [first, last]    = f.info->byOrigin.equal_range(b->node);
        for (auto it = first; it != last; ++it) {
            found = &f.info->nodes[it->second];
            break;
        }
        auto known = byDeclaration.find(b->decl);
        if (found == nullptr) {
            if (known == byDeclaration.end()) {
                Variable v;
                v.name      = b->name;
                v.evaluated = false;
                v.note      = "<not in the graph: nothing depends on it, or a pass removed it>";
                v.view.type = b->type;
                byDeclaration.emplace(b->decl, out.size());
                out.push_back(std::move(v));
            }
            continue;
        }
        auto v = nodeVariable(f, *found, b->name);
        if (!v) {
            continue;
        }
        if (known != byDeclaration.end()) {
            out[known->second] = std::move(*v); // replaces "not in the graph" of another instance
        } else {
            byDeclaration.emplace(b->decl, out.size());
            out.push_back(std::move(*v));
        }
    }
    return out;
}

std::vector<DebugCore::Variable> DebugCore::capturedOf(ShadowFrame &f) {
    std::vector<Variable> out;
    for (gc_node_ref_t ref : f.graph->closureNodes()) {
        if (const NodeInfo *n = f.info->node(ref)) {
            if (auto v = nodeVariable(f, *n, n->label)) {
                out.push_back(std::move(*v));
            }
        }
    }
    return out;
}

std::vector<DebugCore::Variable> DebugCore::inlinedOf(ShadowFrame &f) {
    std::vector<Variable> out;
    for (const NodeInfo &n : f.info->nodes) {
        if (n.origin == 0) {
            continue;
        }
        std::vector<const Binding *> bindings;
        std::string owner;
        {
            std::lock_guard<std::mutex> lock(indexMutex_);
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
            if (auto v = nodeVariable(f, n, owner + "." + b->name)) {
                out.push_back(std::move(*v));
            }
        }
    }
    return out;
}

std::vector<DebugCore::Variable> DebugCore::nodesOf(ShadowFrame &f) {
    std::vector<Variable> out;
    for (const NodeInfo &n : f.info->nodes) {
        std::string name =
            "%" + std::to_string(n.ref) + " " + kindName(n.kind) + " " + n.label;
        if (auto v = nodeVariable(f, n, std::move(name))) {
            out.push_back(std::move(*v));
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
    json scopes = json::array();
    const auto add = [&](const char *name, Handle::Kind kind, size_t count, bool expensive) {
        scopes.push_back(json{
            {"name", name},
            {"reference", newHandle({kind, frameId, {}})},
            {"count", count},
            {"expensive", expensive}});
    };
    add("Locals", Handle::Kind::Locals, localsOf(*f).size(), false);
    if (const size_t n = capturedOf(*f).size(); n > 0) {
        add("Captured", Handle::Kind::Captured, n, false);
    }
    if (const size_t n = inlinedOf(*f).size(); n > 0) {
        add("Inlined", Handle::Kind::Inlined, n, false);
    }
    add("Nodes", Handle::Kind::Nodes, nodesOf(*f).size(), true);
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
        variables = variablesJson(localsOf(*f), handle.frame);
        break;
    case Handle::Kind::Captured:
        variables = variablesJson(capturedOf(*f), handle.frame);
        break;
    case Handle::Kind::Inlined:
        variables = variablesJson(inlinedOf(*f), handle.frame);
        break;
    case Handle::Kind::Nodes:
        variables = variablesJson(nodesOf(*f), handle.frame);
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
            variables.push_back(json{
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

json DebugCore::evaluateJson(uint64_t frameId, const std::string &expression) {
    ShadowFrame *f = frameId == 0 && !stack_.empty() ? &stack_.back() : frameById(frameId);
    if (f == nullptr) {
        return fail("no such frame");
    }
    // name ( '.' member | '[' index ']' )*
    const auto isNameChar = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    };
    std::string text;
    for (char c : expression) {
        if (c != ' ' && c != '\t') {
            text += c;
        }
    }
    size_t pos = 0;
    while (pos < text.size() && isNameChar(text[pos])) {
        ++pos;
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
        return fail(
            "Only a name, optionally followed by member accesses (p.x, t.0, xs[2]), can be "
            "evaluated.");
    }
    // The name: a local or parameter, then a captured name, then an inlined one. Of several
    // declarations of the name, one that has a value is preferred, the latest of them.
    std::optional<Variable> found;
    const auto consider = [&](std::vector<Variable> candidates, bool qualified) {
        for (auto &v : candidates) {
            std::string_view shown = v.name;
            if (qualified) {
                if (auto dot = shown.rfind('.'); dot != std::string_view::npos) {
                    shown = shown.substr(dot + 1);
                }
            }
            if (shown != name) {
                continue;
            }
            if (!found || v.evaluated || !found->evaluated) {
                found = std::move(v);
            }
        }
    };
    consider(localsOf(*f), false);
    if (!found) {
        consider(capturedOf(*f), false);
    }
    if (!found) {
        consider(inlinedOf(*f), true);
    }
    if (!found) {
        return fail("'" + name + "' is not a name of this frame.");
    }
    if (!found->evaluated) {
        if (!path.empty()) {
            return fail("'" + name + "' has no value yet: " + found->note);
        }
        return json{
            {"ok", true},
            {"result", found->note},
            {"type", typeText(found->view.type)},
            {"reference", 0},
            {"evaluated", false}};
    }
    ValueView view = found->view;
    std::string walked = name;
    for (const auto &step : path) {
        auto next = member(view, step);
        if (!next) {
            return fail("'" + walked + "' has no member '" + step + "'.");
        }
        view = *next;
        walked += "." + step;
    }
    Rendered r = render(view);
    return json{
        {"ok", true},
        {"result", r.value},
        {"type", r.type},
        {"reference", r.expandable ? newHandle({Handle::Kind::Value, f->id, view}) : 0},
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
    dot << "digraph \"" << f->info->name << "\" {\n  rankdir=TB;\n  node [shape=box, style=filled, "
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
            std::lock_guard<std::mutex> lock(indexMutex_);
            j["path"] = index_.filePath(n.loc.file);
            j["line"] = n.loc.line;
        }
        json names = json::array();
        {
            std::lock_guard<std::mutex> lock(indexMutex_);
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
        {"graphId", f->info->stableId},
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
                {"lastEvent", nextEventSeq_ - 1}};
        }
        if (name == "setBreakpoints") {
            return setLineBreakpoints(args);
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
        if (name == "configure") {
            std::lock_guard<std::mutex> lock(mutex_);
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
