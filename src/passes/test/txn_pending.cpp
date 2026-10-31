// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The figures a pending transaction leaves behind: see txn_pending.h.
 *
 * The pair this prints is the acceptance of the lead's ruling: a transaction that owns a graph
 * before the graph has a name owns everything the graph makes, so dropping it without assigning an
 * id returns the collector to the reading it had before. Both readings are printed -- one before
 * the transaction is opened and one after it is dropped -- because "the same" is a relation and a
 * reader cannot check a relation against nothing.
 */

#include "camel/passes/test/txn_pending.h"

#include "camel/core/mm/gen.h"
#include "camel/runtime/graph_txn.h"

#include <format>

namespace {

/// The two figures that move when a graph is made: what the collector handed out, and what the
/// spaces hold after a sweep. Both are needed: the first says work happened, the second says the
/// work came back.
struct Figures {
    uint64_t allocations = 0;
    size_t liveAfterSweep = 0;
};

Figures read() {
    auto &space  = camel::core::mm::autoSpace();
    const auto s = space.stats();
    return Figures{
        .allocations   = s.allocations,
        .liveAfterSweep = s.elderLiveBytes + s.largeLiveBytes};
}

} // namespace

const camel::runtime::Program *
TxnPendingTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;

    const Figures before = read();
    {
        auto transaction = GraphTransaction::openPending();
        auto &draft      = transaction->edit();
        auto *f64        = camel::core::type::Type::Float64();
        for (int i = 0; i < 8; ++i) {
            const auto port = draft.addPortNode(f64, draft.allocateRuntimeSlot(f64));
            draft.appendNormPort(port);
        }
        draft.materializeStaticValue(camel::core::rtdata::toSlot(3.5), f64);
        const Figures during = read();
        os << std::format(
            "TXN_PENDING during allocations={} live={}\n", during.allocations, during.liveAfterSweep);
    }
    camel::core::mm::autoSpace().collectGraphSpace();
    const Figures after = read();
    os << std::format(
        "TXN_PENDING before allocations={} live={}\n", before.allocations, before.liveAfterSweep);
    os << std::format(
        "TXN_PENDING after allocations={} live={}\n", after.allocations, after.liveAfterSweep);
    os << std::format(
        "TXN_PENDING live_equal={}\n", after.liveAfterSweep == before.liveAfterSweep ? 1 : 0);
    return program;
}