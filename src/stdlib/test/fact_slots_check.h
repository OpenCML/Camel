// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * TEST-ONLY: `test::fact_slots` is the check on the slots a frozen graph keeps for the facts about
 * it (`camel/runtime/fact_slots.h`, `camel/facts/facts.h`).
 *
 * The slots are the one place a read can be wrong without asking the store: a slot naming a value
 * the store no longer owns is a read of freed memory, and it cannot fail visibly -- it either
 * returns something plausible or crashes somewhere else. So this pass pins the two moments a slot
 * must go back to null, on a live graph, for both shapes of slot:
 *
 *   - a Graph fact's slot holds the value; a Node fact's slot holds a dense array indexed by node
 *     id. Both are published when the store computes (`graph_slot_published`,
 *     `node_slot_published`) and read back through the same accessor a reader uses.
 *   - `Store::invalidate` of one fact clears its slot before it releases the value
 *     (`graph_slot_cleared_on_invalidate`, `node_slot_cleared_on_invalidate`) and the store then
 *     answers absent.
 *   - `Store::invalidateGraph` -- the call `~GCGraph` makes through `facts::forgetGraph`, and the
 *     reason a graph's end leaves no slot behind -- clears every slot anchored on the graph
 *     (`*_cleared_on_invalidateGraph`), and every fact about it is absent.
 *
 * Each of the two is followed by asking for the fact again and finding the slot published once
 * more: a slot that clears is a slot that works, not a slot that is never written. The pass leaves
 * the store holding what it found, so the pipeline after it reads facts as it would have.
 */

#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kFactSlotsTestPass = "test::fact_slots";

class FactSlotsTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};