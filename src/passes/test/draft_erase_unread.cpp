// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The witness of the permission `GraphDraft::eraseUnreadNodes` asks: a set that a node outside it
 * still reads is refused whole and the graph is left as it was found, while a set nothing outside
 * reads is accepted and gone.
 *
 * The two answers are made on graphs built here, not by a program: no program in the corpus builds
 * a unit whose nodes still have a reader (the caller's own "no reader" test is narrower than the
 * interface's), so a program cannot reach the refusal at all. What a program also cannot do is see
 * the erase: a node nobody reads and the graph does not name is outside the set every reader walks
 * from the exit, so `std::gir` and `std::stats` print the same document with and without it. Both
 * facts are why this case asks the interface directly and why its acceptance is the reverse
 * verification -- with the permission turned into "always accept", the first line below stops
 * matching.
 *
 * The accepted side comes second and in the same harness, so a refusal line cannot be a pass that
 * ships an interface which never erases anything.
 */

#include "camel/passes/test/draft_erase_unread.h"

#include "../../compile/gir/builder_support.h"

#include "camel/compile/gir/draft_graph_builder.h"
#include "camel/core/type/base.h"
#include "camel/core/type/composite/func.h"
#include "camel/runtime/graph_txn.h"

#include <format>
#include <memory>
#include <string_view>

using namespace camel::compile::gir;

namespace {

/// The reading of one step, printed so the plan can assert it: a line a person can read in the log
/// is worth more than a boolean nobody prints.
void say(std::ostream &os, std::string_view line) { os << "[ERASE_UNREAD] " << line << "\n"; }

/// The readers that keep a node: a normal or a `with` port of another node. A chain, a control edge
/// and an ordering are not readers (docs/design/effects/edges.cn.md 1.5, 5.3), which is what lets
/// the interface hand a removed statement's place on.
int readersOf(auto &draft, camel::runtime::gc_node_ref_t id) {
    return static_cast<int>(draft.normUsersOf(id).size() + draft.withUsersOf(id).size());
}

int aliveAs(auto &draft, camel::runtime::gc_node_ref_t id) {
    return draft.header(id) != nullptr && draft.alive(id) ? 1 : 0;
}

} // namespace

const camel::runtime::Program *
DraftEraseUnreadTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    const auto make = []() {
        return std::make_shared<DraftGraphBuilder>(camel::runtime::GraphTransaction::open(
            static_cast<camel::runtime::GraphId>(1), camel::core::type::FunctionType::create()));
    };

    compile_graph_ptr_t builder = make();
    auto &draft                 = builder->edit();

    // copy <- middle <- outside: the set {copy, middle} is read from outside it, by `outside`.
    const node_handle_t copyNode    = builder->addCopyNode(camel::core::type::Type::Float32());
    const node_handle_t middleNode  = builder->addCopyNode(camel::core::type::Type::Float32());
    const node_handle_t outsideNode = builder->addCopyNode(camel::core::type::Type::Float32());
    const camel::runtime::gc_node_ref_t copyId    = nodeIdOf(copyNode);
    const camel::runtime::gc_node_ref_t middleId  = nodeIdOf(middleNode);
    const camel::runtime::gc_node_ref_t outsideId = nodeIdOf(outsideNode);
    {
        const camel::runtime::gc_node_ref_t readsCopy[1] = {copyId};
        draft.setNormInputs(middleId, readsCopy);
        const camel::runtime::gc_node_ref_t readsMiddle[1] = {middleId};
        draft.setNormInputs(outsideId, readsMiddle);
    }

    // The case: one node of the set is still read from outside, so the whole set must be refused and
    // every node of it must survive, with its reader still recorded.
    const camel::runtime::gc_node_ref_t readFromOutside[2] = {copyId, middleId};
    const bool refused   = !draft.eraseUnreadNodes(readFromOutside);
    const int copyAlive  = aliveAs(draft, copyId);
    const int midAlive   = aliveAs(draft, middleId);
    const int midReaders = readersOf(draft, middleId);
    say(
        os,
        std::format(
            "refused={} alive_copy={} alive_middle={} readers_middle={}",
            refused ? 1 : 0,
            copyAlive,
            midAlive,
            midReaders));

    // The control, in the same harness: a node nothing reads is accepted and gone. Without it the
    // line above would also pass on an interface that refused everything.
    const camel::runtime::gc_node_ref_t alone[1] = {outsideId};
    const bool acceptedAlone                     = draft.eraseUnreadNodes(alone);
    const int outsideAlive                       = aliveAs(draft, outsideId);
    say(os, std::format("accepted_alone={} alive_outside={}", acceptedAlone ? 1 : 0, outsideAlive));

    // And the set form's own point: a node of the set that reads another node of it is not the
    // "reader outside" the permission refuses, so the pair goes together.
    const node_handle_t headNode = builder->addCopyNode(camel::core::type::Type::Float32());
    const node_handle_t tailNode = builder->addCopyNode(camel::core::type::Type::Float32());
    const camel::runtime::gc_node_ref_t headId = nodeIdOf(headNode);
    const camel::runtime::gc_node_ref_t tailId = nodeIdOf(tailNode);
    {
        const camel::runtime::gc_node_ref_t readsHead[1] = {headId};
        draft.setNormInputs(tailId, readsHead);
    }
    const camel::runtime::gc_node_ref_t closedSet[2] = {headId, tailId};
    const bool acceptedClosedSet                     = draft.eraseUnreadNodes(closedSet);
    say(
        os,
        std::format(
            "accepted_closed_set={} alive_head={} alive_tail={}",
            acceptedClosedSet ? 1 : 0,
            aliveAs(draft, headId),
            aliveAs(draft, tailId)));

    // A witness that could not run its own case must say so instead of reporting nothing wrong.
    if (!refused || copyAlive != 1 || midAlive != 1 || midReaders != 1) {
        say(os, "MISSING the refusal did not leave the set as it was found");
        return program;
    }
    if (!acceptedAlone || outsideAlive != 0 || !acceptedClosedSet) {
        say(os, "MISSING the accept side did not erase what nothing reads");
        return program;
    }
    say(os, "ok");
    return program;
}