// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Which kind a node is and which node a reference names: the vocabulary the questions `graph.h` and
 * `graph_placement.h` are asked in. `GCNodeKind` states the graph model's kinds (docs/design/
 * graph-model, section 3.5) and a `gc_node_ref_t` names one node of a frozen graph by its index.
 *
 * These declarations live here rather than in `camel/runtime/graph.h` so a header that only names a
 * kind or a node id -- a table of node ids, a function that hands one on, a question asked about a
 * kind -- does not have to hold the frozen graph's layout. `graph.h` includes this header, so a
 * reader of the graph still gets the kinds; what it does not get is the reverse: this header knows
 * nothing of `GCGraph`.
 */

#pragma once

#include <cstdint>

namespace camel::runtime {

using gc_node_ref_t = uint32_t;
using gc_slot_idx_t = int32_t;
using gc_data_idx_t = gc_slot_idx_t;
using gc_off_t      = uint32_t;
using gc_cnt_t      = uint32_t;

constexpr gc_node_ref_t kInvalidNodeRef = static_cast<gc_node_ref_t>(-1);
constexpr gc_off_t kInvalidOffset       = static_cast<gc_off_t>(-1);

// The kinds of node. The numeric values are part of the graph model (docs/design/graph-model,
// section 3.5) and must not be renumbered.
//
// Two values are reserved and never appear in a frozen graph (GCGraph):
//  - Bind: nothing creates it; partial application is a closure (a FILL of a function value).
//  - Dref: the front end's placeholder for a name whose target is not settled yet. It lives in
//    build-time drafts only; encoding a graph that still holds one is an error
//    (graph_encode.cpp, validateRuntimeGraphPayload).
// Consumers of frozen graphs therefore do not handle either.
enum class GCNodeKind : uint8_t {
    Data = 0,
    Port = 1,
    // 2 was the CAST node and 3 the COPY node, deleted: a conversion is a call of `:op/cast` and a
    // copy a call of `:op/copy`, the operators `lib/std/prelude.cml` declares for them (plan 2.3).
    // The numbers are not reused -- an encoded kind is a byte of the graph's format -- and a graph
    // that still holds them is the graph an older build wrote.
    Fill = 4,
    Accs = 5,
    Brch = 6,
    Join = 7,
    Call = 8,
    Bind = 9, // reserved, see above
    Func = 10,
    Oper = 11,
    // 12 was the SYNC node, deleted: a statement's place in its `sync` block is a sync edge, and
    // the node it would need is not part of the model (docs/design/effects/edges.cn.md 5.3).
    Gate = 13,
    Dref = 14, // build-time only, see above
    // A loop (design principles 3.10; docs/design/graph-model 3.5, the LOOP card):
    // Norm inputs the array (For, ForWhile) then the k first values of the state; With inputs
    // the results of its two regions and the round port; Ctrl inputs ordering. Its condition
    // and body are regions of this graph (GCRegionKind::LoopCond and LoopBody) whose ports are
    // the state (see camel/runtime/loop.h). Its value is the
    // last state: the value when k = 1, the k-tuple when k > 1. A producer may also give it s
    // collection slots (payload collectionCount): the body then returns (state…, v…) each round
    // and the value is (state…, array…), the arrays holding the v of every round (see
    // camel/runtime/loop.h). Consumers read it through camel/runtime/loop.h (LoopView).
    Loop = 15,
    // The place of a node a rewrite removed. A graph keeps the NodeIds of the version it came
    // from, so a removed node leaves a record with this kind and no inputs, which every
    // consumer that walks the nodes skips (GCGraph::nodes() does); ids are not reused. A
    // graph without removed nodes has none.
    Dead = 255,
};

} // namespace camel::runtime