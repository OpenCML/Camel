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
 *
 * The table itself, which is what makes those slots possible, is checked on three more points:
 *
 *   - the table a compiled graph carries is the facts' size, not a constant: it is smaller than
 *     the fixed thirty-two (`table_smaller_than_the_fixed_thirty_two`), registering more kinds
 *     does not enlarge it (`table_not_grown_by_registration`), and a kind nobody has written has
 *     no slot, which is the read that asks the store (`kind_without_a_fact_has_no_slot`). The
 *     state the design calls "no table at all" cannot be built from a pass: the freeze itself
 *     writes one fact into every graph it encodes (`core.controlRegions`, through
 *     `validateRuntimeGraphPayload` in graph_encode.cpp), so every graph of a compiled program has
 *     a table by the time this pass sees it, and what is pinned here is its size;
 *   - a kind registered past thirty-two -- where a fixed table stopped -- still lands in a slot
 *     (`slot_past_thirty_two`, `table_covers_registered_kinds`), so the growth is measured, not
 *     assumed;
 *   - a reader that took the table before a growth still reads it afterwards
 *     (`table_grown_for_later_kind`, `old_table_read_after_growth`). The interleaving is built by
 *     hand rather than raced: the pass holds the table, registers one more kind, writes it, and
 *     then reads the first kind back through the table it was holding. It can do that because the
 *     graph keeps a replaced table until it ends, which is the property under test -- the check
 *     reads the data, and the reason the read is safe is that the table was retired rather than
 *     freed.
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