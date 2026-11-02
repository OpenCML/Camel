// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * What keeps a node whose running does something alive (docs/design-principles.cn.md 3.1): the two
 * things an effect can stand on, and the one shape where neither has an answer yet.
 *
 * The question is asked where a pass decides whether running a node is part of the program, and
 * today that is two places: the fusion group's consumer test (`FusionGroupView::isPrivateTo`) and
 * the pass that prints the answer for a plan to pin (`KeepInspectTestPass`).
 *
 * The gate's bypass test, the splice that moves a statement into a chain and the walk that reports a
 * dropped effect are **not** among them, although this header used to say so: each of them asks
 * whether a node carries a **wait**, which is a structural fact and never a credential -- a GATE
 * with a wait is `HasNoCredential` here, and that is the right answer to a different question.
 * Three callers read the wait from three different places (control inputs; control or sync inputs;
 * a control frontier), so the field they filled from this header was three readings wearing one
 * name. It is gone; each caller names its own reading on the line where it reads it.
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
///
/// Three fields, and each is a fact the answer reads. Nothing about a **wait** lives here any more:
/// `hasWaitEdge` was filled by four call sites in three spellings (`!ctrlInputs`, `!ctrl ∨ !sync`, a
/// control frontier) and `isWaitedOn` by one, and `effectStandingOf` read neither -- so the header's
/// "asked in one place" was true of the declaration and false of the four answers, which is what a
/// pseudo-single-point is. Deleting the two fields is the ruling from
/// `docs/handover/reports/code-review-2026-11-01.md` item 4: a field that cannot be routed through
/// the answer is deleted rather than left looking like part of it.
struct EffectLivenessFacts {
    /// A value of the node is read by something (design principles 3.1): a normal, `with` or
    /// version input of another node.
    bool hasDataUser = false;
    /// The node is a statement of a `sync` block: it has a sync predecessor or successor, or the
    /// builder marked it as one (`kGCNodeFlagSyncStatement`).
    bool isSyncStatement = false;
    /// Outside a block: the node is registered as a writer of a variable, and a read written after
    /// it takes that variable (the writer table). No caller fills it today: the writer table's
    /// answer is not measured here yet (3.1 keeps it as the third credential). It stays because this
    /// field is the only place in the tree that states that credential; deleting it would delete a
    /// sentence about 3.1, not a piece of dead code.
    bool isWriterWithLaterRead = false;
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