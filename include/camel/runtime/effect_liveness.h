// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * What keeps a node whose running does something alive (docs/design-principles.cn.md 3.1): the two
 * things an effect can stand on, and the one shape where neither has an answer yet.
 *
 * The question is asked by every pass that drops, folds or keeps a node because of what running it
 * does (the reduction of waits, a gate's bypass test, the fusion group's consumer test, the
 * liveness walk that reports a dropped effect, the splice that moves a statement into a chain).
 * Asking it in one place is what keeps those answers from disagreeing (design principles 3.1: a
 * write runs when something reads what it wrote, or when it is a statement of a `sync` block).
 */

#pragma once

#include <cstdint>

namespace camel::runtime {

/// How a node that has an effect stands (docs/design-principles.cn.md 3.1).
enum class EffectStanding : uint8_t {
    /// Something keeps it: a value of it is read (data use), or it is a statement of a `sync`
    /// block's chain, or -- outside a block -- it is a writer that the reads written after it
    /// take (the writer table, `orderAfterLastWriter`).
    Alive,
    /// Nothing keeps it: no read of what it produced, no chain, no later read of what it wrote.
    /// Running it is not part of the program (design principles 3.4, 3.5).
    HasNoCredential,
};

/// The facts the question is answered from. A caller reads them where it stands -- a pass
/// reads its own draft, the builder reads its own bookkeeping -- and the answer is one function so
/// that no two callers invent different rules.
struct EffectLivenessFacts {
    /// A value of the node is read by something (design principles 3.1): a normal, `with` or
    /// version input of another node.
    bool hasDataUser = false;
    /// The node is a statement of a `sync` block: it has a sync predecessor or successor, or the
    /// builder marked it as one (`kGCNodeFlagSyncStatement`).
    bool isSyncStatement = false;
    /// Outside a block: the node is registered as a writer of a variable, and a read written after
    /// it takes that variable (the writer table). No caller fills it today: the writer table's
    /// answer is not measured here yet (3.1 keeps it as the third credential).
    bool isWriterWithLaterRead = false;
    /// The node carries a wait: an edge that exists only to order it (today the control edge,
    /// docs/design/effects/plan.cn.md 5.5). A GATE carries no effect of its own -- it carries a
    /// wait -- and what a pass may do with it depends on whether that wait is a chain's
    /// (`isSyncStatement`) or an edge of its own.
    ///
    /// Its only source today is a control input. Once the control edge stops being written this is
    /// false everywhere, and `bypassValueGate` (src/passes/opt/generic/gates.cpp) becomes dead code:
    /// its premise is gone, not its correctness. Read this line before deleting either.
    bool hasWaitEdge = false;
    /// Something outside takes this node as a wait: it has control users (the same kind, the other
    /// direction). An effect something waits on is observed from outside, so a fusion group may not
    /// take it in as private: the wait would move with it.
    bool isWaitedOn = false;
};

/// The answer. There are two of them: an effect is kept by a data use, a chain, or a later read of
/// what it wrote, and a call through a function value is no third case -- the call is itself the
/// new version of what it wrote, and the read takes a data edge from it (`Builder::valueVersionOf`).
/// A write the writer table does not know is registered by the conservative writer set (all
/// functions in the program that write the variable, scanned before building; see the 22:50
/// ruling). The sync blocks and the resource order are only an extra guarantee now, not the
/// only one.
EffectStanding effectStandingOf(const EffectLivenessFacts &facts);

/// Whether running the node is part of the program, as far as the liveness question goes.
bool keepsEffectAlive(EffectStanding standing);

} // namespace camel::runtime