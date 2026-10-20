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
 * Created: Oct. 20, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The graph manager: owns the Programs of a context and decides which graphs stay alive.
 *
 * The program installed as the context's program owns its graphs; a rewrite installs a new
 * Program and the graphs of the old one that the new one does not hold are freed. A back end's
 * private rewrite (a detached commit) leaves the installed program alone and is kept, with its
 * graphs, until releaseDetached(); the program it is a copy of stays too. Programs of the
 * definition graphs of operators are pinned for the manager's lifetime. Every kept graph is a
 * root of the collector.
 *
 * The Program objects themselves are only freed with the manager: a function value made in a
 * program that was replaced still names its Program, and reading the table of a replaced
 * Program is harmless where freeing it would not be.
 */

#include "camel/runtime/program.h"
#include "camel/runtime/graph.h"

#include "camel/core/context/context.h"
#include "camel/core/mm.h"

#include <algorithm>
#include <unordered_set>

namespace camel::runtime {

namespace mm = camel::core::mm;

void GCGraphManager::freeGraph(GCGraph *graph) {
    delete graph->debug_;
    graph->releaseNodeChunks();
    graph->~GCGraph();
    mm::graphSpace().free(graph);
}

const Program *makeProgram(
    camel::core::context::Context &context, const Program *base, std::span<GCGraph *const> graphs) {
    Program *program = context.reserveProgram();
    if (base != nullptr) {
        *program = *base;
    }
    for (GCGraph *graph : graphs) {
        program->bind(graph->id(), graph);
    }
    if (!graphs.empty()) {
        program->setRoot(graphs.front());
    }
    context.retainRuntimeGraphs(graphs);
    return program;
}

void GCGraphManager::retain(std::span<GCGraph *const> graphs) {
    mm::NoCollectionScope noCollection;
    std::lock_guard lock(mutex_);
    for (GCGraph *graph : graphs) {
        retained_.push_back(graph);
        gcRoots_.push_back(graph);
    }
}

const Program *GCGraphManager::programOf(const GCGraph *graph) const {
    std::lock_guard lock(mutex_);
    for (auto program = programs_.rbegin(); program != programs_.rend(); ++program) {
        if ((*program)->contains(graph)) {
            return program->get();
        }
    }
    return nullptr;
}

Program *GCGraphManager::reserve() {
    std::lock_guard lock(mutex_);
    programs_.push_back(std::make_unique<Program>());
    return programs_.back().get();
}

void GCGraphManager::replaceRoot(const Program *program) {
    std::lock_guard lock(mutex_);
    std::vector<GCGraph *> newGraphs = program ? program->graphs() : std::vector<GCGraph *>{};
    std::unordered_set<GCGraph *> keep(newGraphs.begin(), newGraphs.end());
    // A private copy still tracked (a back end's lowered program) names the program it was
    // copied from (Program::original): that program's graphs stay, owned as part of the
    // program, until a later replacement finds no copy pointing at them.
    for (const Program *copy : detached_) {
        if (const Program *original = copy->original()) {
            for (GCGraph *graph : original->graphs()) {
                if (keep.insert(graph).second) {
                    newGraphs.push_back(graph);
                }
            }
        }
    }

    for (GCGraph *graph : owned_) {
        if (graph && !keep.contains(graph)) {
            freeGraph(graph);
        }
    }

    current_ = program;
    owned_   = std::move(newGraphs);
    rebuildGcRoots();
}

void GCGraphManager::adoptRoot(const Program *program) {
    std::lock_guard lock(mutex_);
    if (current_ || !owned_.empty()) {
        replaceRoot(program);
        return;
    }
    current_ = program;
    if (!current_) {
        return;
    }
    owned_ = current_->graphs();
    rebuildGcRoots();
}

const Program *GCGraphManager::root() const {
    std::lock_guard lock(mutex_);
    return current_;
}

void GCGraphManager::clear() {
    std::lock_guard lock(mutex_);
    for (GCGraph *graph : owned_) {
        if (graph) {
            freeGraph(graph);
        }
    }
    owned_.clear();
    current_ = nullptr;
    rebuildGcRoots();
}

void GCGraphManager::trackDetached(const Program *program) {
    mm::NoCollectionScope noCollection;
    std::lock_guard lock(mutex_);
    if (program && std::find(detached_.begin(), detached_.end(), program) == detached_.end()) {
        detached_.push_back(program);
        for (GCGraph *graph : program->graphs()) {
            gcRoots_.push_back(graph);
        }
    }
}

void GCGraphManager::releaseDetached() {
    std::lock_guard lock(mutex_);
    const std::unordered_set<GCGraph *> owned(owned_.begin(), owned_.end());
    for (const Program *program : detached_) {
        for (GCGraph *graph : program->graphs()) {
            if (!owned.contains(graph)) { // else installed as part of the program since
                freeGraph(graph);
            }
        }
    }
    detached_.clear();
    rebuildGcRoots();
}

void GCGraphManager::pin(const Program *program) {
    mm::NoCollectionScope noCollection;
    std::lock_guard lock(mutex_);
    if (program && std::find(pinned_.begin(), pinned_.end(), program) == pinned_.end()) {
        pinned_.push_back(program);
        for (GCGraph *graph : program->graphs()) {
            gcRoots_.push_back(graph);
        }
    }
}

void GCGraphManager::rebuildGcRoots() {
    gcRoots_.clear();
    gcRoots_.reserve(owned_.size());
    for (GCGraph *graph : owned_) {
        if (graph) {
            gcRoots_.push_back(graph);
        }
    }
    for (const Program *program : detached_) {
        for (GCGraph *graph : program->graphs()) {
            gcRoots_.push_back(graph);
        }
    }
    for (const Program *program : pinned_) {
        for (GCGraph *graph : program->graphs()) {
            gcRoots_.push_back(graph);
        }
    }
    gcRoots_.insert(gcRoots_.end(), retained_.begin(), retained_.end());
}

GCGraphManager::~GCGraphManager() {
    std::lock_guard lock(mutex_);
    releaseDetached();
    clear();
    for (const Program *program : pinned_) {
        for (GCGraph *graph : program->graphs()) {
            freeGraph(graph);
        }
    }
    pinned_.clear();
    for (GCGraph *graph : retained_) {
        freeGraph(graph);
    }
    retained_.clear();
    gcRoots_.clear();
}

} // namespace camel::runtime
