// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * What the bytecode engine records about the code it made for one graph version.
 *
 * The plan of a version is a fact: one immutable value, computed once and kept by the store
 * (camel/runtime/exec_plan.h). What FastVM writes while it runs is not part of that value -- where
 * its bytecode starts, how long it is, the JIT entry it made, the flags it set, how often an
 * indirect call left it, the bytecode range the version holds in the interpreter's code space, and
 * the pc table the --gc-check anchor reads. Those are the engine's own state about a version, so
 * they live here, in an object keyed by the plan: the plan is what a version is, and the store
 * frees it when the graph goes.
 *
 * One table for the process, one mutex, and an entry that keeps a weak reference to its plan. The
 * address of a plan the store freed can be handed to the next plan, so an entry found under that
 * address but holding another plan is dropped rather than answered. An entry whose plan is gone is
 * dropped when a version next asks for its own, which is also when the bytecode range the version
 * held goes back to the code space (CodeLease::onRelease).
 */

#pragma once

#include "camel/runtime/exec_plan.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace camel::runtime {

/// What the bytecode engine records about one graph version. Only this engine reads and writes it.
struct FastVmVersionState {
    /// Where the version's bytecode starts and how long it is, each stored plus one (0 is "not
    /// compiled"), the JIT entry, the flags, and how often an indirect call from it was made.
    uintptr_t entryPc           = 0;
    uintptr_t graphLength       = 0;
    uintptr_t jitEntry          = 0;
    uintptr_t flags             = 0;
    uintptr_t indirectCallCount = 0;
    /// Keeps the bytecode range of the version in the interpreter's code space, and gives it back
    /// when this state goes.
    std::shared_ptr<void> codeLease;

    /// The anchor's side table, kept for the reason the fields above are: the compiler knows what
    /// the run needs later, and the run reads it while it runs. The anchor's pc form -- a back end
    /// that runs bytecode holds a pc where the node form holds a node -- asks which node an
    /// instruction belongs to at the moment a counted box is made, which is after the compile that
    /// knew the answer is over.
    ///
    /// Empty unless `--gc-check` asked for the counted blocks: a compile that is not asked fills
    /// nothing, allocates nothing for it, and every reader of it finds an empty table.
    ///
    /// `nodeAtPc` is keyed by the graph's own pc numbering as the compiler emitted it, and the
    /// optimizer moves those keys with the instructions, in the same call that moves the origin
    /// table (`BytecodeOptimizer::optimize`), so a pc an interpreter holds in hand finds the row of
    /// the instruction it is pointing at and never the row of one that was there before the move.
    /// `countedEntryPc` is where that numbering starts in the image the run executes, so an image
    /// pc can be brought back to a key of `nodeAtPc`.
    std::unordered_map<size_t, gc_node_ref_t> nodeAtPc;
    size_t countedEntryPc = 0;
};

/// The state of `graph`'s version: made on the first ask and kept while the version lives.
FastVmVersionState &fastVmStateOf(const GCGraph &graph);

} // namespace camel::runtime