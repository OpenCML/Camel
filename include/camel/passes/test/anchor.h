// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* The nodes the edge tests write where a program has a statement, or one they only need to be
 * there. Neither computes anything observable; what tells them apart is what the model lets a pass
 * do with them. */

#pragma once
#include "camel/core/rtdata/base.h"
#include "camel/core/type/base.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/graph_txn.h"

#include <span>

namespace camel::passes::test {

/// A statement of no value that no pass may arrange freely: the conversion of a constant. The seal
/// lets an order edge end on a conversion (`EdgeChecker::isFreelySchedulable` keeps a Cast ordered:
/// whether one can fail depends on the types, and a pass that refines them may make it one that
/// cannot, docs/design/effects/edges.cn.md 4.6), and a `sync` block's chain may run through it, so
/// it stands where a statement of the source would. It is not a GATE, so a chain that forks on it is
/// a fork, and it runs as the conversion it is. The tests use it where they need a statement the
/// source did not write; the SYNC node that used to stand there is gone (edges.cn.md 5.3).
inline camel::runtime::gc_node_ref_t anchorNode(camel::runtime::GraphTransaction &transaction) {
    auto &draft = transaction.edit();
    const camel::runtime::gc_node_ref_t value = draft.materializeStaticValue(
        camel::core::rtdata::toSlot<float>(1.0f), camel::core::type::Type::Float32());
    const camel::runtime::gc_node_ref_t cast =
        draft.addCastNode(camel::core::type::Type::Float32());
    draft.setNormInputs(cast, std::span<const camel::runtime::gc_node_ref_t>(&value, 1));
    return cast;
}

/// A node nothing reads and nothing executes: the null constant (slot 0, no type). It is what a
/// test adds when it only needs the graph to change, or a node to hang an edge on that no rule about
/// purity, chains or layout has anything to say about -- it is pure, so no order edge may end on it.
inline camel::runtime::gc_node_ref_t blankNode(camel::runtime::GraphTransaction &transaction) {
    auto &draft = transaction.edit();
    return draft.addDataNode(nullptr, 0);
}

} // namespace camel::passes::test