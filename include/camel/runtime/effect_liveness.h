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
 * The gate's bypass test, the splice that moves a statement into a chain and the walk that reports
 * a dropped effect are **not** among them, although this header used to say so: each of them asks
 * whether a node carries a **wait**, which is a structural fact and never a credential -- a GATE
 * with a wait is `HasNoCredential` here, and that is the right answer to a different question.
 * Three callers read the wait from three different places (control inputs; control or sync inputs;
 * a control frontier), so the field they filled from this header was three readings wearing one
 * name. It is gone; each caller names its own reading on the line where it reads it.
 */

#pragma once

#include "camel/runtime/graph.h"

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
/// `hasWaitEdge` was filled by four call sites in three spellings (`!ctrlInputs`, `!ctrl ∨ !sync`,
/// a control frontier) and `isWaitedOn` by one, and `effectStandingOf` read neither -- so the
/// header's "asked in one place" was true of the declaration and false of the four answers, which
/// is what a pseudo-single-point is. Deleting the two fields is the ruling from
/// `docs/handover/reports/code-review-2026-11-01.md` item 4: a field that cannot be routed through
/// the answer is deleted rather than left looking like part of it.
struct EffectLivenessFacts {
    /// A value of the node is read by something (design principles 3.1): a normal, `with` or
    /// version input of another node.
    bool hasDataUser = false;
    /// The node has a sync predecessor or successor in its block's chain.
    bool isSyncStatement = false;
    /// Outside a block: the node is registered as a writer of a variable, and a read written after
    /// it takes that variable (the writer table). No caller fills it today: the writer table's
    /// answer is not measured here yet (3.1 keeps it as the third credential). It stays because
    /// this field is the only place in the tree that states that credential; deleting it would
    /// delete a sentence about 3.1, not a piece of dead code.
    bool isWriterWithLaterRead = false;
};

/// The answer. There are two of them: an effect is kept by a data use, a chain, or a later read of
/// what it wrote, and a call through a function value is no third case -- the call is itself the
/// new version of what it wrote, and the read takes a data edge from it
/// (`Builder::valueVersionOf`). A write the writer table does not know is registered by the
/// conservative writer set (all functions in the program that write the variable, scanned before
/// building; see the 22:50 ruling). The sync blocks and the resource order are only an extra
/// guarantee now, not the only one.
EffectStanding effectStandingOf(const EffectLivenessFacts &facts);

/// Whether running the node is part of the program, as far as the liveness question goes.
bool keepsEffectAlive(EffectStanding standing);

/// Whether a root of the graph holds the write `ref` makes: the value it leaves, or the place it
/// writes into. A place is rooted when the graph names it -- a `var` parameter, a capture, a result
/// -- which is what a write into a variable the program keeps elsewhere looks like, and a value is
/// rooted when it is one of the graph's own results.
bool writeIsRooted(const GCGraph &graph, gc_node_ref_t ref);

/// Whether a value of `ref` is read: a normal or a `with` input of another node, the two ways a
/// reader takes a value (design principles 3.1). A GATE reading it counts, and so does a port of a
/// graph the value is handed to; what they do with it is their business.
bool valueIsRead(const GCGraph &graph, gc_node_ref_t ref);

/// Whether the write `ref` makes is a **dead write**: no root of its graph holds it or the place it
/// writes into, and no port reads it. Each half is asked of the graph as it stands, which is what
/// makes this a judgement about a rewrite and not about the set of writes the program had. A
/// rewrite may leave a variable captured by no escaping closure (say `fst((1, h))` drops the
/// closure `h` that captured `c`); the write it held is then a write nobody can read, and it may
/// go. What is checked is that **a deleted write is no root in the result and no port reads it**,
/// not that the set of final writes is the same before and after (design principles 9.7).
///
/// Asked where a pass decides whether running a node is part of the program
/// (`src/execute/pass.cpp#nodeRunsForItsEffect`, the pass boundary's effect credential check), and
/// printed on its own for the tests by `test::deadwrite` (`src/passes/test/deadwrite.cpp`), so that
/// the judgement is one function and can be asked without a pipeline around it.
///
/// Conservative where the graph does not answer: a node that is not an operator, or whose
/// declaration the graph does not carry, is not a dead write, and nothing here says a write that is
/// a root or that is read may go -- it may not, and this function says so.
bool deadWrite(const GCGraph &graph, gc_node_ref_t ref);

} // namespace camel::runtime