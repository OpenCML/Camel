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
    /// A call through a function value writes captures that the type does not name: which ones is
    /// known to the run time (`GCFuncRef::reserved`) and not here, so the version a later read
    /// should take cannot be handed back statically. Today such a call waits through a control
    /// edge (`builder_expr.cpp`, `orderAfterLastWriter`, "the data edge it is owed"), which is the
    /// one thing the edge model has no replacement for.
    /// docs/handover/questions/questions-2026-10-31-how-an-indirect-call-hands-back-a-version.md
    PendingIndirectCallVersion,
};

/// The four facts the question is answered from. A caller reads them where it stands -- a pass
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
    /// it takes that variable (the writer table).
    bool isWriterWithLaterRead = false;
    /// The node is a call through a function value that may write captures (`EffectSummary`:
    /// a call whose callee is not known).
    bool isCallThroughValue = false;
};

/// The answer. A call through a function value that has no data user and is on no chain is
/// `PendingIndirectCallVersion` and not `HasNoCredential`: the question is open, and a caller that
/// treats it as dead would drop a write that may be read (see the page named above).
EffectStanding effectStandingOf(const EffectLivenessFacts &facts);

/// Whether running the node is part of the program, as far as the liveness question goes: `Alive`,
/// or the open case, which no caller may treat as dead until the author has ruled.
bool keepsEffectAlive(EffectStanding standing);

} // namespace camel::runtime