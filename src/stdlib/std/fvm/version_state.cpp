// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The bytecode engine's state about a graph version (version_state.h), and the pc form of the
 * --gc-check anchor, which reads it.
 */

#include "version_state.h"

#include <mutex>

namespace camel::runtime {

namespace {

struct VersionSlot {
    /// Weak on purpose: the store owns the plan, and a state entry must not keep a version alive
    /// after its graph is gone.
    std::weak_ptr<const ExecPlan> plan;
    FastVmVersionState state;
};

std::mutex &versionStatesMutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<const ExecPlan *, VersionSlot> &versionStates() {
    static std::unordered_map<const ExecPlan *, VersionSlot> states;
    return states;
}

} // namespace

FastVmVersionState &fastVmStateOf(const GCGraph &graph) {
    const std::shared_ptr<const ExecPlan> plan = execPlanOf(graph);
    std::lock_guard lock(versionStatesMutex());
    auto &states = versionStates();
    auto found   = states.find(plan.get());
    if (found != states.end() && found->second.plan.lock() != plan) {
        // The address of a plan the store freed, handed to this one. The entry belongs to the
        // version that used to be here; answering it would report code this version never made.
        states.erase(found);
        found = states.end();
    }
    if (found == states.end()) {
        // The versions whose plan is gone end here: their state, and the bytecode range it kept.
        std::erase_if(states, [](const auto &item) { return item.second.plan.expired(); });
        found = states.emplace(plan.get(), VersionSlot{plan, FastVmVersionState{}}).first;
    }
    return found->second.state;
}

void noteCountedCreationAt(const void *graph, size_t pc, uint64_t boxes) noexcept {
    if (graph == nullptr || boxes == 0) {
        return;
    }
    try {
        const FastVmVersionState &state = fastVmStateOf(*static_cast<const GCGraph *>(graph));
        gc_node_ref_t node              = kInvalidNodeRef;
        if (pc >= state.countedEntryPc) {
            const auto row = state.nodeAtPc.find(pc - state.countedEntryPc);
            if (row != state.nodeAtPc.end()) {
                node = row->second;
            }
        }
        noteCountedCreationForNode(graph, node, boxes);
    } catch (...) {
        // The anchor reports and never decides (camel/runtime/exec_plan.h): a version whose plan
        // cannot be found to name the node reports nothing about it and lets the run go on.
        return;
    }
}

} // namespace camel::runtime