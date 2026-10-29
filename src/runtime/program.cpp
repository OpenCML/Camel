// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The graph manager: the Programs of a context and the graph versions they hold, both objects
 * of the graph space, freed when nothing reaches them.
 *
 * Roots: the installed program, the detached programs a back end runs, the pinned programs of
 * the operator definitions, the programs the pipeline made since the last settle(), and the
 * programs running calls were entered through. A Program traces its graphs and the program it
 * copies; a graph traces its static values; a function value traces its Program. A graph
 * version is freed when no live Program holds it, a Program when nothing reaches it.
 *
 * Two collections run the same sweep over different reachability. collect() runs between
 * passes, from the roots, over the Programs and the static values of the graphs it reaches;
 * the function values of the heap are not looked at, which is right while no program runs (a
 * pass does not keep a function value of a replaced program). While a program runs the
 * collector of run-time values marks, through the heap, what it reaches (the Programs and
 * graphs among it), and sweepMarked() frees the rest.
 */

#include "camel/runtime/program.h"
#include "camel/runtime/graph.h"

#include "camel/core/context/context.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/func.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <unordered_set>

namespace camel::runtime {

namespace mm = camel::core::mm;
using camel::core::rtdata::Object;
using camel::core::rtdata::RefTraceInfo;

camel::core::rtdata::Object *Program::clone(
    camel::core::mm::IAllocator &allocator, const camel::core::type::Type *type, bool deep) const {
    (void)allocator;
    (void)type;
    (void)deep;
    throw std::logic_error("A Program is not cloned.");
}

void Program::print(std::ostream &os, const camel::core::type::Type *type) const {
    (void)type;
    os << "Program(graphs=" << count_ << ")";
}

void Program::updateRefs(
    const camel::core::rtdata::Object::RefRelocator &relocate,
    const camel::core::type::Type *type) {
    for (GCGraph *&graph : byId_) {
        if (graph != nullptr) {
            relocate(
                static_cast<Object *>(graph),
                nullptr,
                RefTraceInfo{
                    .owner     = this,
                    .ownerType = type,
                    .slotType  = nullptr,
                    .ownerKind = "Program",
                    .slotName  = "graph",
                });
        }
    }
}

void Program::note(GCGraph *graph) { manager_->noteGraph(graph); }

void Program::noteAll() {
    for (GCGraph *graph : byId_) {
        if (graph != nullptr) {
            manager_->noteGraph(graph);
        }
    }
}

void GCGraphManager::freeGraph(GCGraph *graph) {
    // A graph-space block can be in the manager's books before it is a graph: an encoder binds
    // the memory a graph is built in, then builds it (graph_encode.cpp, a draft session), and a
    // run that ends while it builds -- the graph or the block space at its limit -- leaves the
    // blocks it had bound but not reached as memory with nothing built in it. The constructor is
    // what writes a graph's id (GCGraph::GCGraph), and no graph carries GraphId::Invalid, so a
    // block that names no graph is memory to give back, not an object to destruct: destructing it
    // dispatches through a vtable that was never written (a segmentation fault after the
    // out-of-memory diagnostic, which is the error the run was reporting).
    if (!isValid(graph->id())) {
        mm::graphSpace().free(graph);
        return;
    }
    delete graph->debug_;
    graph->releaseNodeChunks();
    graph->~GCGraph();
    mm::graphSpace().free(graph);
}

void GCGraphManager::freeProgram(Program *program) {
    program->~Program();
    mm::graphSpace().free(program);
}

const Program *makeProgram(
    camel::core::context::Context &context, const Program *base, std::span<GCGraph *const> graphs) {
    Program *program = context.reserveProgram();
    if (base != nullptr) {
        *program = *base;
    }
    program->makeSatellite();
    for (GCGraph *graph : graphs) {
        program->bindOwn(graph->id(), graph);
    }
    if (!graphs.empty()) {
        program->setRoot(graphs.front());
    }
    return program;
}

void GCGraphManager::noteGraph(GCGraph *graph) {
    std::lock_guard lock(mutex_);
    graphs_.insert(graph);
    unbound_.erase(graph);
}

void GCGraphManager::forget(GCGraph *graph) {
    std::lock_guard lock(mutex_);
    graphs_.erase(graph);
    unbound_.erase(graph);
}

void Program::abandonConstruction(std::span<GCGraph *const> unconstructed) {
    if (manager_ != nullptr) {
        for (GCGraph *graph : unconstructed) {
            manager_->forget(graph);
        }
    }
    byId_.clear();
    own_.clear();
    count_ = 0;
    root_  = nullptr;
    ++version_;
}

void GCGraphManager::noteUnbound(GCGraph *graph) {
    std::lock_guard lock(mutex_);
    graphs_.insert(graph);
    unbound_.insert(graph);
}

void GCGraphManager::release(GCGraph *graph) {
    std::lock_guard lock(mutex_);
    graphs_.erase(graph);
    unbound_.erase(graph);
    freeGraph(graph);
}

const Program *GCGraphManager::programOf(const GCGraph *graph) const {
    std::lock_guard lock(mutex_);
    for (auto program = programs_.rbegin(); program != programs_.rend(); ++program) {
        if ((*program)->contains(graph)) {
            return *program;
        }
    }
    return nullptr;
}

Program *GCGraphManager::reserve() {
    mm::NoCollectionScope noCollection;
    Program *program = nullptr;
    {
        std::lock_guard lock(mutex_);
        void *mem = mm::graphSpace().alloc(sizeof(Program), alignof(Program));
        if (mem == nullptr) {
            throw std::bad_alloc();
        }
        program           = new (mem) Program();
        program->manager_ = this;
        programs_.push_back(program);
        // A program made while a program runs is reached by the function value made for it; one
        // made by the pipeline is held until the pass is over (settle).
        if (running_ == 0) {
            provisional_.push_back(program);
            gcRoots_.push_back(program);
        }
    }
    // Outside the lock: the collector takes its own lock first and ours in the sweep.
    noteGrowth();
    return program;
}

namespace {
// --graph-gc-mb: read by every manager made after the option is applied (default 64 MB).
std::atomic<size_t> graphCollectionFloorBytes{size_t{64} << 20};
} // namespace

void setGraphCollectionFloorBytes(size_t bytes) {
    graphCollectionFloorBytes.store(bytes, std::memory_order_release);
}

GCGraphManager::GCGraphManager() {
    // How much the graph space may hold before the first collection asks for itself
    // (afterwards: as much again as it held after the last one).
    collectionFloor_ = graphCollectionFloorBytes.load(std::memory_order_acquire);
}

void GCGraphManager::collectIfGrown() {
    // The graph space holds what the last collection left and what was made since: collecting
    // costs a walk over the static values of every live graph, so it waits until the space has
    // grown by a quarter (and a MB).
    const size_t used = mm::graphSpace().usage().usedBytes;
    if (used > liveBytes_ + std::max(liveBytes_ / 4, size_t{1} << 20)) {
        collect();
    }
}

void GCGraphManager::noteGrowth() {
    if (mm::graphSpace().usage().usedBytes > liveBytes_ + std::max(liveBytes_, collectionFloor_)) {
        mm::autoSpace().requestMajorCollection("graph space budget");
    }
}

void GCGraphManager::noteSwept() { liveBytes_ = mm::graphSpace().usage().usedBytes; }

void GCGraphManager::replaceRoot(const Program *program) {
    {
        mm::NoCollectionScope noCollection;
        std::lock_guard lock(mutex_);
        current_ = program;
        rebuildGcRoots();
    }
    collectIfGrown();
}

bool GCGraphManager::replaceRootIfCurrent(const Program *expected, const Program *program) {
    bool replaced = false;
    {
        mm::NoCollectionScope noCollection;
        std::lock_guard lock(mutex_);
        if (current_ == expected) {
            current_ = program;
            rebuildGcRoots();
            replaced = true;
        }
    }
    if (replaced) {
        collectIfGrown();
    }
    return replaced;
}

void GCGraphManager::adoptRoot(const Program *program) {
    {
        mm::NoCollectionScope noCollection;
        std::lock_guard lock(mutex_);
        current_ = program;
        rebuildGcRoots();
    }
    collectIfGrown();
}

const Program *GCGraphManager::root() const {
    std::lock_guard lock(mutex_);
    return current_;
}

void GCGraphManager::clear() {
    {
        mm::NoCollectionScope noCollection;
        std::lock_guard lock(mutex_);
        current_ = nullptr;
        unbound_.clear();
        rebuildGcRoots();
    }
    collect();
}

void GCGraphManager::trackDetached(const Program *program) {
    mm::NoCollectionScope noCollection;
    std::lock_guard lock(mutex_);
    if (program && std::find(detached_.begin(), detached_.end(), program) == detached_.end()) {
        detached_.push_back(program);
        rebuildGcRoots();
    }
}

void GCGraphManager::releaseDetached() {
    {
        mm::NoCollectionScope noCollection;
        std::lock_guard lock(mutex_);
        detached_.clear();
        rebuildGcRoots();
    }
    collectIfGrown();
}

void GCGraphManager::pin(const Program *program) {
    mm::NoCollectionScope noCollection;
    std::lock_guard lock(mutex_);
    if (program && std::find(pinned_.begin(), pinned_.end(), program) == pinned_.end()) {
        pinned_.push_back(program);
        rebuildGcRoots();
    }
}

void GCGraphManager::settle(const Program *keep) {
    {
        mm::NoCollectionScope noCollection;
        std::lock_guard lock(mutex_);
        if (provisional_.empty() && keep == nullptr && unbound_.empty()) {
            return;
        }
        // `keep`, the program the pass handed on, is the one thing that must stay.
        provisional_.clear();
        if (keep != nullptr && keep != current_) {
            provisional_.push_back(keep);
        }
        // The graphs a rewrite encoded and its commit did not bind belong to the pass that made
        // them; from here they live only if a program holds them.
        unbound_.clear();
        rebuildGcRoots();
    }
    collectIfGrown();
}

void GCGraphManager::rootForRun(const Program *program) {
    mm::NoCollectionScope noCollection;
    std::lock_guard lock(mutex_);
    runRoots_.push_back(program);
    rebuildGcRoots();
}

void GCGraphManager::unrootForRun(const Program *program) {
    mm::NoCollectionScope noCollection;
    std::lock_guard lock(mutex_);
    if (auto it = std::find(runRoots_.begin(), runRoots_.end(), program); it != runRoots_.end()) {
        runRoots_.erase(it);
        rebuildGcRoots();
    }
}

void GCGraphManager::rebuildGcRoots() {
    gcRoots_.clear();
    const auto add = [&](const Program *program) {
        if (program != nullptr) {
            gcRoots_.push_back(const_cast<Program *>(program));
        }
    };
    add(current_);
    for (const auto *list : {&detached_, &pinned_, &provisional_, &runRoots_}) {
        for (const Program *program : *list) {
            add(program);
        }
    }
}

namespace {

/// The function values among the static values of `graph`, with the Programs they name.
void functionsInStatics(GCGraph *graph, std::vector<::Function *> &found) {
    std::unordered_set<Object *> visited;
    std::vector<std::pair<Object *, const camel::core::type::Type *>> stack;
    const Object::RefRelocator relocate = [&](Object *ref,
                                              const camel::core::type::Type *refType,
                                              const RefTraceInfo &info) -> Object * {
        if (ref == nullptr) {
            return ref;
        }
        if (info.slotName == "program") {
            found.push_back(const_cast<::Function *>(static_cast<const ::Function *>(info.owner)));
            return ref;
        }
        if (info.slotName == "graph" || mm::graphSpace().contains(ref) ||
            mm::permSpace().contains(ref)) {
            return ref;
        }
        if (visited.insert(ref).second) {
            stack.emplace_back(ref, refType);
        }
        return ref;
    };
    graph->updateRefs(relocate, nullptr);
    while (!stack.empty()) {
        const auto [object, type] = stack.back();
        stack.pop_back();
        object->updateRefs(relocate, type);
    }
}

} // namespace

void retargetFunctions(GCGraph *graph, const Program &to) {
    std::vector<::Function *> functions;
    functionsInStatics(graph, functions);
    for (::Function *function : functions) {
        if (function->program() != &to && to.graph(function->graphId()) != nullptr &&
            to.graph(function->graphId()) == function->graph()) {
            function->rebind(&to, function->graphId());
        }
    }
}

void GCGraphManager::collect() {
    {
        std::lock_guard lock(mutex_);
        if (running_ > 0) {
            return;
        }
    }
    // The collector marks through the heap too: a function value of a program keeps it.
    mm::autoSpace().collectGraphSpace();
}

void GCGraphManager::sweepMarked() {
    std::lock_guard lock(mutex_);
    const auto marked = [](const void *object) {
        return reinterpret_cast<const mm::ObjectHeader *>(object)[-1].marked();
    };
    struct Swept {
        GCGraphManager &manager;
        ~Swept() { manager.noteSwept(); }
    } swept{*this};
    std::vector<Program *> deadPrograms;
    for (Program *program : programs_) {
        if (!marked(program)) {
            deadPrograms.push_back(program);
        }
    }
    std::vector<GCGraph *> deadGraphs;
    for (GCGraph *graph : graphs_) {
        // A graph a rewrite encoded and no program bound yet is only reachable from the session
        // that encoded it: it is freed once that pass is over (settle).
        if (unbound_.contains(graph)) {
            continue;
        }
        if (!marked(graph)) {
            deadGraphs.push_back(graph);
        }
    }
    freeDead(deadPrograms, deadGraphs);
}

void GCGraphManager::freeDead(
    const std::vector<Program *> &deadPrograms, const std::vector<GCGraph *> &deadGraphs) {
    for (GCGraph *graph : deadGraphs) {
        graphs_.erase(graph);
        freeGraph(graph);
    }
    if (!deadPrograms.empty()) {
        const std::unordered_set<Program *> dead(deadPrograms.begin(), deadPrograms.end());
        std::erase_if(programs_, [&](Program *program) { return dead.contains(program); });
        for (Program *program : deadPrograms) {
            freeProgram(program);
        }
    }
}

GCGraphManager::~GCGraphManager() {
    std::lock_guard lock(mutex_);
    gcRoots_.clear();
    for (GCGraph *graph : graphs_) {
        freeGraph(graph);
    }
    graphs_.clear();
    for (Program *program : programs_) {
        freeProgram(program);
    }
    programs_.clear();
}

} // namespace camel::runtime
