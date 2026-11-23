// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * A body's variables: the cell a `var` is held in (visitVariNode), the operator calls a read and a
 * write of it make (internalOperatorGroup, internalOperatorDref, variableOperator,
 * readVariableThrough), the version a name has after a call (variableAfterCall), and the two walks
 * that drop what nothing uses (collapseNonEscapingVariableReads, dropCarriedVariableUnits).
 */

#include "builder.h"
#include "builder_helpers.h"
#include "builder_support.h"
#include "camel/core/trace.h"
#include <format>

namespace {
// The `[DBG-W]` and `[DBG-FOLD]` lines of the fold are one reading of the run's tracing channel,
// `--trace fold` (`include/camel/core/trace.h`). The environment is not where a run's settings
// come from (design principles 9.3), and one kind of reading has one way to ask for it.
namespace folddebug {
inline std::ostream &out() { return camel::core::trace(camel::core::TraceArea::Fold); }
} // namespace folddebug
} // namespace

#include "camel/compile/gir/static_function.h"
#include "camel/core/data/composite/struct.h"
#include "camel/core/numeric_convert.h"
#include "camel/core/operator_names.h"
#include "camel/core/rtdata/string.h"
#include "camel/execute/entry_args.h"
#include "camel/runtime/control.h"
#include "camel/runtime/graph.h"
#include "camel/utils/log.h"
#include "camel/utils/scope.h"
#include "camel/utils/str.h"
#include "camel/utils/type.h"
#include "core/module/entity_internal.h"

#include "camel/core/operator_traits.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/other.h"
#include "camel/core/type/ref.h"

#include <atomic>
#include <cctype>
#include <stdexcept>
#define DEBUG_LEVEL -1

using namespace std;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::data;
using namespace camel::core::type;

namespace camel::compile::gir {
node_handle_t Builder::visitVariNode(const GCT::node_ptr_t &gct) {
    ENTER("VARI");
    bool old           = varied_;
    bool oldDeclares   = declaresVariable_;
    varied_            = true;
    declaresVariable_  = true;
    node_handle_t node = valueNodeOf(visit(gct->at(0)));
    varied_            = old;
    declaresVariable_  = oldDeclares;
    LEAVE("VARI");
    return node;
}

oper_group_ptr_t Builder::internalOperatorGroup(camel::core::OperId oper) {
    const auto group = context_->importModule("")->nativeOperatorGroup(oper);
    GUARD(
        group != nullptr,
        "The built-in module did not register the operator the compiler emits: " +
            std::string(camel::core::OperatorTable::instance().uri(oper)));
    return group;
}

GCT::node_ptr_t Builder::internalOperatorDref(camel::core::OperId oper) {
    // The id is the reference; the entity name is only the node's label (a GCT dump, a
    // diagnostic), as it was when the DREF was made by name.
    return tree_->makeDRef(oper, internalOperatorGroup(oper)->name());
}

node_handle_t Builder::variableOperator(camel::core::OperId oper, node_handle_t input) {
    const auto group = internalOperatorGroup(oper);
    const std::vector<StaticArg> statics(1);
    const auto op = group->resolve({}, {nodeTypeOf(input)}, statics);
    if (!op.has_value()) {
        // ASSERT is an empty statement under NDEBUG, so the line below dereferenced an empty
        // optional and this was a segfault. The reading says which operand and what
        // node it is: a variable operator needs a version that carries a value, and `void` is not
        // one.
        throw std::runtime_error(
            std::format(
                "a variable operator got an operand with no value type: `:var/get` needs a version "
                "that carries a value, and the node it was given is a node of kind {} with none",
                static_cast<int>(nodeKindOf(input))));
    }
    node_handle_t node = currGraph_->addOperNode(*op, 0);
    stampPermissions(node);
    linkNodes(LinkType::Norm, input, node);
    return node;
}

node_handle_t Builder::secondNameOf(node_handle_t value) {
    // A name bound to a value, where there is no cell to read it out of: a `var x = <the state>`
    // hands the state to a new cell and `:var/new` counts what it is given, but a `let` has no cell,
    // and the name it binds has to be a read of its own -- a node of the value at this statement,
    // not the state it was read from (design principles 10.5). Otherwise every use of the name
    // would take the state where the use stood, and `let kept = xs` followed by `xs[i] = i + 1`
    // would read the write (`Builder::valueOf`, the round's value at the place of the read).
    Type *type = nodeTypeOf(value) != nullptr ? valueTypeOf(nodeTypeOf(value)) : nullptr;
    if (type == nullptr) {
        return value;
    }
    if (type->code() == TypeCode::Array) {
        // An array carries storage a write further down the round reaches in place, and there is no
        // count on this path to stop it: the name leaves with its own storage instead
        // (`:struct/copy_arr`, the operator the loop rewrite gives a carried array, and the same
        // answer `shareForSecondName` gives for an array the reads do not share).
        return variableOperator(camel::core::oper::CopyArray, value);
    }
    // Every other value has no storage a write could reach, so the value itself is what the name
    // holds, and the keeper gives the value a place of its own to be read from: the GATE of
    // docs/design/effects/edges.cn.md 5.3 and operator_table.h#kWellKnownCount, a region of one data
    // port reading the value, passed through. No sync predecessor: what the name needs is a place
    // for the value, not an order for it. It carries no count, as a value with no storage needs none.
    node_handle_t keeper = currGraph_->addGateNode(nodeTypeOf(value));
    linkNodes(LinkType::Norm, value, keeper);
    return keeper;
}

node_handle_t
Builder::secondNameOfCarriedName(const GCT::node_ptr_t &initializer, node_handle_t value) {
    // A `var x = <the state>` hands the state to a new cell, and `:var/new` counts what it is
    // given; a `let` has no cell, so the read itself is where the count has to be made.
    if (initializer == nullptr || initializer->type() != GCT::NodeKind::DREF ||
        initializer->size() != 0) {
        return value;
    }
    // A carried state reaches the body as one of two things: a port that stands for a cell
    // (`loopStatePorts_`, the cell form) or a name the body's scope declares as the port itself
    // (`SsaEnv::declarePort`, the value form).
    const auto bound = nodeAt(moduleLevelName(initializer->name()));
    const bool carried = (bound.has_value() && loopStatePorts_.contains(bound.value())) ||
                         ssa_.portOf(nodeScope_, initializer->name()) != nullptr;
    if (!carried) {
        return value;
    }
    return secondNameOf(value);
}

node_handle_t Builder::variableAfterCall(node_handle_t variable, node_handle_t call) {
    // The variable as it stands after `call`: `:var/after(cell, call)`. Its second argument is the
    // call, whose value may be `void` and is not read -- the edge is what makes the call run when
    // the read takes this version (docs/design-principles.cn.md 3.1, 3.5) -- and its result is the
    // cell itself, so the read above it is a read of the variable after that write.
    //
    // Its first argument is the version the variable had before that call when there was one: the
    // write written before a call through a function value must keep running, because the call is
    // only a possible writer of the variable (the conservative writer set), and the read takes what
    // the last real write left. The cell itself when there is none.
    node_handle_t before = variable;
    if (const auto byCall = writeBeforeCall_.find(call); byCall != writeBeforeCall_.end()) {
        if (const auto found = byCall->second.find(variable); found != byCall->second.end()) {
            before = found->second;
        }
    }
    const auto group = internalOperatorGroup(camel::core::oper::VarAfter);
    const std::vector<StaticArg> statics(1);
    const auto op = group->resolve({}, {nodeTypeOf(before), nodeTypeOf(call)}, statics);
    if (!op.has_value()) {
        throw std::runtime_error(
            std::format(
                "a variable after a call rejected its operands: the variable is {} and the call is "
                "{}",
                nodeTypeOf(before) != nullptr ? nodeTypeOf(before)->toString() : "<no type>",
                nodeTypeOf(call) != nullptr ? nodeTypeOf(call)->toString() : "<no type>"));
    }
    node_handle_t node = currGraph_->addOperNode(*op, 0);
    stampPermissions(node);
    linkNodes(LinkType::Norm, before, node);
    linkNodes(LinkType::Norm, call, node);
    return node;
}

node_handle_t Builder::newVariable(node_handle_t value) {
    // The variable takes the widened type of its initializer (:var/new), so later assignments
    // may store values with a different refinement (e.g. shape).
    node_handle_t variable = variableOperator(camel::core::oper::VarNew, value);
    variables_.insert(variable);
    return variable;
}

node_handle_t Builder::newVariableOfType(Type *type) {
    // The type the cell holds is the operator's one `with` argument: no operand, no value, and the
    // read of the cell has a type from the moment the cell is made.
    const auto group = internalOperatorGroup(camel::core::oper::VarNewEmpty);
    const std::vector<StaticArg> statics(0);
    const auto op = group->resolve({type}, {}, statics);
    GUARD(op.has_value(), "The empty cell operator rejected the type it was given.");
    node_handle_t variable = currGraph_->addOperNode(*op, 0);
    stampPermissions(variable);
    variables_.insert(variable);
    return variable;
}

node_handle_t Builder::readVariable(node_handle_t variable) {
    return readVariableThrough(variable, camel::core::oper::VarGet);
}

node_handle_t Builder::readVariableThrough(node_handle_t variable, camel::core::OperId oper) {
    // The version of the variable the last write made: that write's own value. Every write
    // produces one (valueVersionOf): an assignment gives the cell it wrote into, and a call that
    // wrote a captured variable gives the access node of the part of its value that is that
    // variable's new version. The read takes it as its data port, and that edge is the read's
    // whole tie to the write -- it orders the read after it and it is what makes the write
    // execute (docs/design/effects/edges.cn.md 1, 5.6; docs/design-principles.cn.md 3.5). A
    // writer whose value holds no version of this variable -- a branch whose writes meet at a
    // JOIN, an indirect call -- gives the cell and no version of its own: the read keeps the
    // cell as its data port and the write orders it, which a control input stands in for
    // (Builder::orderAfterLastWriter). Before any write the version is the cell :var/new made.
    node_handle_t version = valueVersionOf(variable);
    if (version != nullptr) {
        // A version that carries the variable's value rather than its cell -- a branch whose arms
        // wrote it, so the JOIN is the write (design principles 3.5) --
        // is the read itself: there is nothing to take out of a cell, and `:var/get` would be an
        // operator on a node that holds no cell. The data edge from the write is the whole tie the
        // read needs, so it is the node the read stands for and the write's place orders whoever
        // reads it.
        Type *versionType = nodeTypeOf(version);
        if (versionType != nullptr && valueTypeOf(versionType) == versionType) {
            // The version is a node of the region it was built in, and a read stands in one:
            // `recordWrite` keeps a read the write cannot be ordered against pending for the
            // region (`readRegion != exitRegion`, builder_expr.cpp), and a read cannot take a
            // value out of a region it is not in for the same reason, one side later. The JOIN of
            // a statement branch written in a loop body is a node of that arm, and a read after the
            // loop is not in it: an edge from there to here is the branch-arm invariant
            // (src/runtime/branch_arms.cpp), which the graph is refused for at encode. The read
            // takes the cell instead -- the shape a read after a loop of a `var` has when no
            // branch carried it -- and the region it stands in orders it.
            auto &readDraft              = currGraph_->transaction().edit();
            const uint32_t versionRegion = readDraft.nodeRegion(nodeIdOf(version));
            const uint32_t readRegion    = readDraft.insertRegion();
            if (readDraft.regionWithin(readRegion, versionRegion)) {
                return version;
            }
            version = nullptr;
        }
    }
    if (version == nullptr) {
        // The conservative writer set may have counted a call of this graph as a possible writer of
        // the variable before this graph had captured it (a call through a function value writes a
        // variable this body had not touched yet): the read written after that call takes the
        // variable as it stands after it. Program order is what says "after": the builder writes
        // the body in the order it is written, so a claim met here was made by a call written
        // before this read.
        const auto origin           = captureOriginOf_.find(variable);
        const node_handle_t claimed = claimedWriterOf(
            currGraph_.get(),
            origin == captureOriginOf_.end() ? variable : origin->second);
        if (claimed != nullptr && sameGraph(claimed, currGraph_)) {
            version = variableAfterCall(variable, claimed);
        }
    }
    if (version == nullptr) {
        version = variable;
    }
    node_handle_t value = variableOperator(oper, version);
    pendingUsesOf(variable).push_back({value, true});
    if (version == variable) {
        if (auto modifier = modifierOf(variable); modifier.has_value()) {
            if (sameGraph(*modifier, currGraph_) && linkCheek(*modifier, value)) {
                writeControlLink(*modifier, value);
            }
        }
    }
    // SSA (collapseNonEscapingVariableReads): this read may later take its value from the write
    // that made it instead of from the cell, so the writer it belongs to is recorded here, while
    // the builder still knows it. The read node counts as one of the cell's known users.
    if (sameGraph(variable, currGraph_)) {
        variableReadSource_[value] = VariableRead{version, variable};
        knownCellUsers_.insert(value);
    }
    // A read is not a statement of a sync block: it does not join the chain. What orders it is the
    // version it takes as its data port and the writes recorded after it (design principles 3.5,
    // docs/design/effects/edges.cn.md 5.5). A `var` parameter may alias the caller's other
    // argument, so its read also waits on the preceding statement without taking over that
    // statement's place.
    if (synced_ && lastIndirectCall_ != nullptr && sameGraph(lastIndirectCall_, currGraph_) &&
        linkCheek(lastIndirectCall_, value) && nodeTakesAnOrder(value) &&
        nodeTakesAnOrder(lastIndirectCall_) && !hasDirectDataDependency(lastIndirectCall_, value)) {
        linkNodes(LinkType::Order, lastIndirectCall_, value);
    }
    if (synced_) {
        // A read is not a statement of the sync block's chain: it
        // takes its place from the version it reads, so the link the chain would write here is not
        // written at all.
        syncedReads_.push_back(value);
        const bool hasCaptureVersion = version != variable && captureOriginOf_.contains(variable);
        if (nodeKindOf(variable) == camel::runtime::GCNodeKind::Port && !hasCaptureVersion) {
            if (lastSyncedNode_ != nullptr && shouldAddSyncCtrlLink(lastSyncedNode_, value)) {
                writeControlLink(lastSyncedNode_, value);
            }
        }
    }
    return value;
}

node_handle_t Builder::peekVariable(node_handle_t variable) {
    // The value of the variable without the store's right to its array: a path assignment
    // reading the value it writes must not take the array away from the set that follows.
    // It is a read all the same: ordered after the variable's last writer and, in a sync
    // block, in the block's chain.
    return readVariableThrough(variable, camel::core::oper::VarPeek);
}

void Builder::collapseNonEscapingVariableReads() {
    if (variableReadSource_.empty() || currGraph_ == nullptr) {
        return;
    }
    // A read nothing uses is no statement and runs for nobody (`let after = c` that is never read):
    // it is not on the chain any more, so nothing would reach it, and the writer's order edge to it
    // would name a node the graph does not keep. It goes before the reads that are folded, with the
    // order edge that kept the next write after it.
    {
        auto &graphDraft = currGraph_->transaction().edit();
        std::vector<node_handle_t> unused;
        for (const auto &entry : variableReadSource_) {
            const node_handle_t read = entry.first;
            // A read is recorded while this builder is in the graph that made it, and the table
            // outlives that graph: the graph is handed over and released, and the builder with it.
            // Asking `sameGraph` about such a read asserts that it has an owner (builder_support.h,
            // `nodeGraphOf`), which is exactly what is gone here, so the read asks the draft
            // whether it names it at all -- by address and by epoch, which is what a handle names
            // a draft by. A read of another graph is not this graph's read; it is skipped here and
            // dropped at the border by `forgetNodesOfOtherGraphs`, because an outer graph's reads
            // are still needed by the outer graph's own turn.
            if (!graphDraft.isNamedBy(read)) {
                continue;
            }
            if (graphDraft.header(nodeIdOf(read)) == nullptr || !graphDraft.alive(nodeIdOf(read))) {
                continue;
            }
            const draft_node_ref_t readId = nodeIdOf(read);
            if (readId == graphDraft.exitNode() || readId == graphDraft.outputNode() ||
                readId == graphDraft.returnNode()) {
                continue;
            }
            bool used = false;
            for (const auto &users :
                 {graphDraft.normUsersOf(readId),
                  graphDraft.withUsersOf(readId),
                  graphDraft.ctrlUsersOf(readId),
                  graphDraft.syncUsersOf(readId)}) {
                used = used || !users.empty();
            }
            if (!used) {
                unused.push_back(read);
            }
        }
        // The deletion goes through the edit interface: `eraseUnreadNodes` asks the permission and
        // hands the place the node held in its block's chain to the statement written after it
        // (`closeChainOver`, `spliceOrderEdgesOver`), so this builder erases by no second path.
        // Which reads may go is the membership test above, and it stays here: a read with no user
        // of any kind, sequencing edges included. A node the graph names (`namesNode`) keeps
        // itself, and then the read keeps its record too.
        for (const node_handle_t read : unused) {
            const draft_node_ref_t only[1] = {nodeIdOf(read)};
            if (!graphDraft.eraseUnreadNodes(only)) {
                continue;
            }
            variableReadSource_.erase(read);
        }
    }
    // The reads recorded for this graph, grouped by the cell they read. A cell of an outer graph
    // belongs to that graph's own turn, so it is skipped here.
    std::unordered_map<node_handle_t, std::vector<std::pair<node_handle_t, node_handle_t>>> byCell;
    auto &draft = currGraph_->transaction().edit();
    for (const auto &[read, info] : variableReadSource_) {
        // Same reason as above: the draft is asked whether it is the one the cell names, because a
        // cell of a graph that has been released has no builder left to answer `sameGraph`.
        if (!draft.isNamedBy(info.cell)) {
            continue;
        }
        byCell[info.cell].push_back({read, info.source});
    }
    // `variableReadSource_` is hashed on the handle's address, so each cell's reads arrive here in
    // the allocator's order, and the fold below hands them to the graph in it. Sorted by the name
    // the program gives each read (its graph, then its id -- `walksBefore`, builder_helpers.h), the
    // stretches are the program's; the cell's own order is what the walk outside asks about.
    for (auto &[cell, reads] : byCell) {
        std::sort(reads.begin(), reads.end(), [](const auto &left, const auto &right) {
            return walksBefore(left.first, right.first);
        });
    }
    // Every exit of this fold is counted, because three times now a condition was written,
    // compiled,
    // and had no effect -- each time found by guessing at readings. The counts go to stderr when
    // CAMEL_LOG_DEBUG is set: initial (the value `:var/new` made, only for a variable never
    // written), loop (carried state, not folded), join (a branch's value merge point), write (the
    // value a write wrote), nothing (a source the fold does not stand for, or the read kept for
    // another reason).
    static int foldTookInitial_ = 0;
    static int foldSawLoop_     = 0;
    static int foldTookJoin_    = 0;
    static int foldTookWrite_   = 0;
    static int foldTookNothing_ = 0;
    // The reads that took the value a straight-line write wrote, found by writesOf_ and not by the
    // version record (the branch that a read of a still-cell source takes).
    static int foldTookFold_ = 0;
    struct FoldCountsReport {
        ~FoldCountsReport() {
            folddebug::out() << std::format(
                "[DBG-FOLD] initial={} loop={} join={} write={} nothing={} fold={}\n",
                foldTookInitial_,
                foldSawLoop_,
                foldTookJoin_,
                foldTookWrite_,
                foldTookNothing_,
                foldTookFold_);
        }
    };
    static FoldCountsReport foldCountsReport_;
    // The cells by the name the program gives them, not the order their hashes fall in: the fold
    // builds one node after another and the graph numbers its nodes in creation order.
    std::vector<node_handle_t> folded;
    folded.reserve(byCell.size());
    for (const auto &entry : byCell) {
        folded.push_back(entry.first);
    }
    std::sort(folded.begin(), folded.end(), walksBefore);
    for (const node_handle_t cell : folded) {
        std::vector<std::pair<node_handle_t, node_handle_t>> &reads = byCell.at(cell);
        // A `var` parameter of this graph is a resource shared with the caller, and a captured cell
        // is shared with a closure: both keep their cell. A `var` that only a write put in
        // `escapingVariables_` is not the same thing: the assignment is the write, it does not
        // exist after the call, so being written is not a way of leaving the body. It is given to
        // the fold here, and what keeps it a resource then has to be a real escape (written into
        // `escapingVariables_` by a capture or a user function's `var` parameter) or a write the
        // fold cannot stand for -- a read whose version is a carried state (the LOOP) is not
        // folded below, so one such read is enough to keep the cell:
        //   straight-line (foldable): every read's version is a write of the same body, and no
        //   read's version is a LOOP. Anything else -- a loop's carried state, a read whose version
        //   is a merge point the fold does not accept, an arm's region -- is not straight-line.
        if (nodeKindOf(cell) == camel::runtime::GCNodeKind::Port) {
            continue;
        }
        if (escapingVariables_.contains(cell) && !writeOnlyEscaping_.contains(cell)) {
            continue;
        }
        const camel::runtime::gc_node_ref_t cellId = nodeIdOf(cell);
        if (draft.header(cellId) == nullptr) {
            continue;
        }
        // Every user of the cell must be one this builder recorded: one of its reads, or one of its
        // stores. Any other user -- a `:var/take`, a call the `var` was handed to, a capture port
        // -- is a use this conversion does not model, so the cell keeps its cell.
        bool known = true;
        for (auto users : {normUsersOf(cell), withUsersOf(cell), ctrlUsersOf(cell)}) {
            for (draft_node_ref_t user : users) {
                if (!knownCellUsers_.contains(nodeDraftOf(cell)->ref(user))) {
                    known = false;
                    break;
                }
            }
            if (!known) {
                break;
            }
        }
        if (!known) {
            continue;
        }
        // The value of a cell nothing has written yet is the one it was made with (:var/new's
        // input).
        const auto cellInputs = draft.normInputsOf(cellId);
        for (auto &[read, source] : reads) {
            const camel::runtime::gc_node_ref_t readId = nodeIdOf(read);
            if (draft.header(readId) == nullptr) {
                continue;
            }
            // A read a capture site handed to a direct call -- the call's `with` input -- is a read
            // like any other: it is an edge of the graph from the moment the call was built
            // (addCaptureSite), so the fold below rewrites the call's input with the rest of the
            // read's users and the cell's unit goes with it (dropCarriedVariableUnits).
            // A read of a value that can be *kept* stays a read. `markNonRetainingReads` turns a
            // read nobody keeps into `:var/peek` and lets the next append change the array in
            // place; a read that can be kept stays `:var/get` and the memory plan copies instead.
            // That decision is made once the whole program is built, and this fold runs at the end
            // of one graph (`leaveScope`), so it cannot ask it: it must leave every reference-
            // holding value inside its read. Handing the reader the array the variable still owns
            // makes a later in-place append show through it -- `new_values/peek.cml`'s
            // `let s = acc` kept `[1, 2]` before, and folding its read made it follow `acc` to
            // `[1, 2, 3]`. A number, a bool or a string has no place to keep, and folds as before.
            const Type *readType = nodeTypeOf(read);
            if (readType != nullptr && readType->code() != TypeCode::String &&
                isGCTraced(readType->code())) {
                ++foldTookNothing_;
                folddebug::out() << std::format(
                    "[DBG-W] read={} writes=- candidate=none kind=- region=- took=kept\n",
                    static_cast<unsigned>(readId));
                continue;
            }
            // A write gives the cell of this version; what a read wants is the value written, and
            // a write holds it as its second operand. A writer that is not one of those -- the JOIN
            // a branch's writes meet at, say -- gives a cell and no value of its own, so that read
            // keeps its cell: the writer it takes its version from is still the one the design
            // names, and reading it is what the design says a read does.
            camel::runtime::gc_node_ref_t version = readId;
            if (source == cell) {
                // A read wants the value written last before it. No write before it means the
                // variable has not been written yet, and then it is the value `:var/new` made.
                node_handle_t candidate = nullptr;
                size_t writesSeen       = 0;
                if (const auto it = writesOf_.find(cell); it != writesOf_.end()) {
                    writesSeen = it->second.size();
                    // A read inside an arm, a loop body or a loop condition of a cell that is
                    // written somewhere does not see `:var/new`'s value. In a loop it sees the
                    // value the previous turn carried, which the LOOP hands on and no write of the
                    // body stands for: `write_in_loop.cml` (`x = x + 1`) read the value `:var/new`
                    // made on every turn, so the turns wrote `1` and the read after the loop
                    // answered `1` instead of `3`. In an arm it may see a value another arm wrote,
                    // or the one a write before the branch produced: `warnings_visible.cml` (`var c
                    // = 0; inc(c); if c > 0 then { c += 10 }`) read the initial `0` inside the arm
                    // and answered `1` where `11` is what the program says. So such a read keeps
                    // its `:var/get`, and that is decided before the initial value is handed out.
                    if (writesSeen > 0) {
                        const auto readRegionId = draft.nodeRegion(readId);
                        const auto readKind     = draft.region(readRegionId).kind;
                        if (readKind == camel::runtime::GCRegionKind::Arm ||
                            readKind == camel::runtime::GCRegionKind::LoopBody ||
                            readKind == camel::runtime::GCRegionKind::LoopCond) {
                            ++foldTookNothing_;
                            folddebug::out() << std::format(
                                "[DBG-W] read={} writes={} readregion={} candidate=- kind=- "
                                "region=- took=region-read\n",
                                static_cast<unsigned>(readId),
                                writesSeen,
                                static_cast<int>(readRegionId));
                            continue;
                        }
                    }
                    // A write this fold cannot name: a call that wrote the cell through a `var`
                    // argument leaves its value in no node's input, so a read after it can take
                    // neither that value nor the initial one -- the call has already run. Whether a
                    // write came before the read and whether that write can be named are two
                    // questions: only "no write before it" licenses `:var/new`'s value.
                    bool aCallWroteBeforeTheRead = false;
                    for (const node_handle_t exit : it->second) {
                        if (nodeIdOf(exit) >= readId) {
                            continue;
                        }
                        if (nodeKindOf(exit) != camel::runtime::GCNodeKind::Oper) {
                            aCallWroteBeforeTheRead = true;
                            continue;
                        }
                        const auto ins = draft.normInputsOf(nodeIdOf(exit));
                        if (ins.empty() || ins[0] != cellId) {
                            continue;
                        }
                        candidate = exit;
                    }
                    if (aCallWroteBeforeTheRead) {
                        ++foldTookNothing_;
                        folddebug::out() << std::format(
                            "[DBG-W] read={} writes={} candidate=none kind=- region=- "
                            "took=call-write\n",
                            static_cast<unsigned>(readId),
                            writesSeen);
                        continue;
                    }
                }
                if (candidate == nullptr) {
                    if (cellInputs.empty()) {
                        ++foldTookNothing_;
                        continue;
                    }
                    version = cellInputs[0];
                    ++foldTookInitial_;
                    folddebug::out() << std::format(
                        "[DBG-W] read={} writes={} candidate=none kind=- region=- took=initial\n",
                        static_cast<unsigned>(readId),
                        writesSeen);
                } else {
                    // The candidate's region, by id: `region()` takes a region id, and a node's is
                    // `nodeRegion`. An arm's or a loop's write is a write of that region, and the
                    // fold does not stand for it here (a region is laid out as one piece).
                    const auto regionId = draft.nodeRegion(nodeIdOf(candidate));
                    const auto kind     = draft.region(regionId).kind;
                    const bool refused  = kind == camel::runtime::GCRegionKind::Arm ||
                                          kind == camel::runtime::GCRegionKind::LoopCond ||
                                          kind == camel::runtime::GCRegionKind::LoopBody ||
                                          kind == camel::runtime::GCRegionKind::Dead;
                    folddebug::out() << std::format(
                        "[DBG-W] read={} writes={} candidate={} kind={} region={} took={}\n",
                        static_cast<unsigned>(readId),
                        writesSeen,
                        static_cast<unsigned>(nodeIdOf(candidate)),
                        static_cast<int>(nodeKindOf(candidate)),
                        static_cast<int>(regionId),
                        refused ? "nothing" : "fold");
                    if (refused) {
                        ++foldTookNothing_;
                        continue;
                    }
                    const auto w = draft.normInputsOf(nodeIdOf(candidate));
                    version      = w[1];
                    ++foldTookFold_;
                }
            } else if (nodeKindOf(source) == camel::runtime::GCNodeKind::Loop) {
                // A write inside a loop is carried state: the version a read after the loop takes
                // is the loop, which is not a value the fold can stand in for (schedule item 3).
                // Folding it would answer with the value from before the loop.
                ++foldSawLoop_;
                continue;
            } else {
                if (nodeKindOf(source) == camel::runtime::GCNodeKind::Join) {
                    // A branch's JOIN selects between the values its arms give. When it carries a
                    // value -- its type is the value's and not a cell's -- the read takes the join
                    // itself: that is the branch's result, which is what a write inside a branch is
                    // (`if c then { x = 1 }` merges 1 and the value from before the branch).
                    Type *sourceType = nodeTypeOf(source);
                    if (sourceType == nullptr || valueTypeOf(sourceType) != sourceType) {
                        ++foldTookNothing_;
                        continue;
                    }
                    version = nodeIdOf(source);
                    ++foldTookJoin_;
                } else {
                    if (nodeKindOf(source) != camel::runtime::GCNodeKind::Oper) {
                        ++foldTookNothing_;
                        continue;
                    }
                    const auto written = draft.normInputsOf(nodeIdOf(source));
                    if (written.size() < 2) {
                        ++foldTookNothing_;
                        continue;
                    }
                    version = written[1];
                    ++foldTookWrite_;
                }
            }
            if (version == cellId || draft.header(version) == nullptr) {
                continue;
            }
            // A GATE that waits on this read keeps that wait: the read is the statement the block's
            // value is handed out after, so folding it away would take the wait with it and leave
            // the GATE waiting for nothing, which the commit refuses (src/runtime/graph.cpp, the
            // GATE check; docs/design/effects/edges.cn.md 5.3 gives the GATE the statement before
            // it as its wait). The read stays, and it reads what it read before.
            bool waitedOnByGate = false;
            for (const auto &users : {draft.ctrlUsersOf(readId), draft.syncUsersOf(readId)}) {
                for (camel::runtime::gc_node_ref_t user : users) {
                    const camel::runtime::DraftNodeHeader *userHeader = draft.header(user);
                    if (userHeader != nullptr &&
                        userHeader->kind == camel::runtime::GCNodeKind::Gate) {
                        waitedOnByGate = true;
                        break;
                    }
                }
                if (waitedOnByGate) {
                    break;
                }
            }
            if (waitedOnByGate) {
                continue;
            }
            // The read stood in a chain, and its place in it is the version's now: what ordered the
            // read orders the version, and what the read ordered is ordered by the version
            // (docs/design/effects/edges.cn.md 4). `eraseNode` splices a chain *over* the node it
            // removes (A -> B -> C becomes A -> C), which keeps the ordering only when the read had
            // a predecessor; a read at the head of a chain -- the one a BRCH keeps to say "read
            // this before the branch" -- has none, so its edge would go with it and the branch
            // would lose the order a later pass reads as its execution order
            // (order_edges_regions.gir).
            draft.replaceOrderEdgesAt(readId, version);
            // The read's users take the version's value; the read node is not needed any more.
            draft.replaceAllValueUses(readId, version);
            draft.eraseNode(readId);
        }
    }
}

void Builder::dropCarriedVariableUnits() {
    if (currGraph_ == nullptr) {
        return;
    }
    auto &draft = currGraph_->transaction().edit();
    // The users of a node, of every kind. They are read from the draft and not from a record this
    // builder keeps: a capture, a `var` argument, a region result and a chain all name a node
    // through the draft, and an edge is the only thing an erasure can take away.
    const auto usersOf = [&](camel::runtime::gc_node_ref_t id,
                             std::vector<camel::runtime::gc_node_ref_t> *out) {
        bool any = false;
        for (auto users :
             {draft.normUsersOf(id),
              draft.withUsersOf(id),
              draft.ctrlUsersOf(id),
              draft.syncUsersOf(id),
              draft.orderUsersOf(id)}) {
            any = any || !users.empty();
            if (out != nullptr) {
                out->insert(out->end(), users.begin(), users.end());
            }
        }
        return any;
    };
    const auto isRoot = [&](camel::runtime::gc_node_ref_t id) {
        return id == draft.exitNode() || id == draft.outputNode() || id == draft.returnNode();
    };
    // A node a region hands out is a result of that region: an arm's value or a body's value is
    // named there, and erasing it would leave the region naming a node the graph does not hold.
    const auto isRegionResult = [&](camel::runtime::gc_node_ref_t id) {
        for (size_t r = 0; r < draft.regionCount(); ++r) {
            if (draft.region(static_cast<uint32_t>(r)).result == id) {
                return true;
            }
        }
        return false;
    };
    // The cells whose unit may be left unread: the ones a branch or a loop carried, and the ones a
    // closure captured by value. The second is the same question with one more answer: every read
    // of such a cell took the value (the capture is the read, and the fold gave it the value the
    // write wrote), so what is left is the unit -- `:var/new`, its store and the GATEs that handed
    // the statement's value out -- and nobody uses it. Design principles 3.5 (197): the closure
    // holds the value, so the cell is not a resource of the body and there is nothing left for it
    // to be. The `let` writing of the same program has none of those nodes, which is what the pair
    // of writings is compared on.
    std::vector<node_handle_t> cells;
    for (const auto &[cell, carried] : carriedVersions_) {
        if (draft.isNamedBy(cell) && draft.isNamedBy(carried)) {
            cells.push_back(cell);
        }
    }
    // Both tables below are hashed on the handle's ADDRESS -- `std::hash<DraftNodeRef>` is
    // `hash<const void*>(draft) * 31 + id` (`runtime/node_ref.h`) -- so walking them walks the
    // allocator's order, and the units dropped further down are created in that order. Two runs of
// Both tables below are hashed on the handle's ADDRESS -- `std::hash<DraftNodeRef>` is
    // `hash<const void*>(draft) * 31 + id` (`runtime/node_ref.h`) -- so walking them walks the
    // allocator's order, and the units dropped further down are created in that order. Two runs of
    // one program then number the nodes of this body differently. A graph numbers its nodes in
    // creation order, so the table is sorted by a name the program gives the node: the graph it
    // belongs to, then the id that graph gave it (`walksBefore`, builder_helpers.h). The id alone
    // does not answer it -- it names a node inside one graph, and these tables hold handles of more
    // than one, so two cells with the same id would then keep the order their hashes fell in.
    std::sort(cells.begin(), cells.end(), walksBefore);
    const size_t carriedCount = cells.size();
    for (const node_handle_t cell : valueCapturedCells_) {
        if (draft.isNamedBy(cell)) {
            cells.push_back(cell);
        }
    }
    // `valueCapturedCells_` is the same kind of table. Its cells follow the carried ones -- the two
    // questions are asked in that order and the order is kept -- and each stretch is sorted inside
    // itself.
    std::sort(cells.begin() + static_cast<std::ptrdiff_t>(carriedCount), cells.end(), walksBefore);
    for (const node_handle_t cell : cells) {
        // A cell this graph no longer holds has no unit left to drop, and asking it for its kind
        // is a read of a header the erasure took away: `carriedVersions_` and the capture table
        // are tables of handles the builder keeps across bodies, and a rewrite that dissolves the
        // place a cell was written in -- the branch or the loop body it was carried out of -- takes
        // the cell's node with it and leaves the handle. So the liveness question comes first, as
        // it does for every node the stores below walk.
        const camel::runtime::gc_node_ref_t cellId = nodeIdOf(cell);
        if (draft.header(cellId) == nullptr || !draft.alive(cellId)) {
            continue;
        }
        // The cell is a cell of this graph that is not a resource any more. A captured cell, a
        // `var` parameter and one handed to a call as a `var` argument are `escapingVariables_`;
        // a read still recorded for it is the fold's business and is not folded here, so such a
        // cell keeps its unit.
        if (nodeKindOf(cell) != camel::runtime::GCNodeKind::Oper ||
            escapingVariables_.contains(cell) || captureOriginOf_.contains(cell)) {
            continue;
        }
        // Its stores are the only users it has: a read, a capture, a `var` argument or anything
        // else the builder did not record there keeps the cell (and its unit) as it is.
        const auto writes = writesOf_.find(cell);
        std::unordered_set<camel::runtime::gc_node_ref_t> stores;
        bool onlyStores = true;
        if (writes == writesOf_.end() || writes->second.empty()) {
            // A write the lowering never made: a write inside a loop body is the loop's carried
            // state (visitLoopNode), so the cell has no store. What is left of such a cell is its
            // own unit and the value the cell was made with, and both go -- the `let` writing has
            // neither. A cell anything at all still uses keeps them, as any other cell does.
            if (usersOf(cellId, nullptr) || isRoot(cellId) || isRegionResult(cellId)) {
                continue;
            }
        } else {
            for (node_handle_t write : writes->second) {
                if (!draft.isNamedBy(write)) {
                    onlyStores = false;
                    break;
                }
                stores.insert(nodeIdOf(write));
            }
            if (onlyStores) {
                std::vector<camel::runtime::gc_node_ref_t> users;
                usersOf(cellId, &users);
                for (camel::runtime::gc_node_ref_t user : users) {
                    if (!stores.contains(user)) {
                        onlyStores = false;
                    }
                }
            }
            if (!onlyStores) {
                continue;
            }
        }
        // The block's own GATEs go with the store. The arm of a `sync` block hands its value out
        // through GATEs that wait for the statement before them, and now that the branch takes the
        // value the write wrote, nothing reads those GATEs. They are collected as a set: a GATE
        // that only waits on what is already in the set, or on nothing at all, is part of the unit,
        // and a GATE may wait on the store while the store waits on it.Only a GATE or a COPY in the
        // region of a store is taken this way: an operator or a call stands for a statement the
        // program wrote, and a node a region hands out is that region's result and is not a unit's
        // part.
        std::unordered_set<camel::runtime::gc_node_ref_t> doomed = stores;
        for (bool changed = true; changed;) {
            changed = false;
            for (camel::runtime::gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
                if (!draft.alive(id) || doomed.contains(id) || isRoot(id) || isRegionResult(id)) {
                    continue;
                }
                const camel::runtime::DraftNodeHeader *header = draft.header(id);
                if (header == nullptr) {
                    continue;
                }
                // A copy of either shape: the block's GATEs and the copies the unit made go with
                // the store, and the flip turns the copies into `:op/copy` calls.
                if (header->kind != camel::runtime::GCNodeKind::Gate &&
                    !isCopyNode(draft.ref(id))) {
                    continue;
                }
                bool inStoreRegion = false;
                for (camel::runtime::gc_node_ref_t store : stores) {
                    inStoreRegion =
                        inStoreRegion || draft.nodeRegion(store) == draft.nodeRegion(id);
                }
                if (!inStoreRegion) {
                    continue;
                }
                std::vector<camel::runtime::gc_node_ref_t> users;
                usersOf(id, &users);
                bool onlyDoomed = true;
                for (camel::runtime::gc_node_ref_t user : users) {
                    if (doomed.contains(user)) {
                        continue;
                    }
                    const camel::runtime::DraftNodeHeader *userHeader = draft.header(user);
                    if (userHeader == nullptr || !draft.alive(user)) {
                        continue;
                    }
                    onlyDoomed = false;
                }
                if (onlyDoomed) {
                    doomed.insert(id);
                    changed = true;
                }
            }
        }
        // What is left of the unit is a set nothing outside it reads: the stores, the GATEs that
        // handed the statement's value out, and (below) the cell and the value it was made with.
        // The fold gave every reader of the cell the value the write wrote, so what a store wrote
        // is never read out of the cell; a `var` write is a value, not an effect (design principles
        // 3.5, 3.1), so the store goes with the unit. What a store may still have is a *sequencing*
        // user -- the chain of the block it stood in, the GATE that handed the statement's value
        // out, the JOIN or the LOOP that carried it -- and those are not readers: the erase hands
        // the place on and splices the orderings over the node (docs/design/effects/edges.cn.md
        // 1.5, 5.3). The permission belongs to the draft interface and not to this builder
        // (`GraphDraft::eraseUnreadNodes`): a node outside the set that still reads one of these,
        // or a node the graph names, keeps the whole unit as it was, and the call says so.
        std::vector<camel::runtime::gc_node_ref_t> going(doomed.begin(), doomed.end());
        std::sort(going.begin(), going.end());
        std::vector<node_handle_t> chained;
        chained.reserve(going.size());
        for (const camel::runtime::gc_node_ref_t node : going) {
            chained.push_back(nodeDraftOf(cell)->ref(node));
        }
        if (!draft.eraseUnreadNodes(going)) {
            continue;
        }
        // The nodes went through the interface, so this builder's record of which of them stood in
        // a chain is the only thing left to let go of.
        for (const node_handle_t &node : chained) {
            chainedNodes_.erase(node);
        }
        writesOf_.erase(cell);
        knownCellUsers_.erase(cell);
        // The cell is not a variable of this body any more: what is left of it is erased below,
        // and a registry that still names it would ask the draft about a node it no longer holds
        // (cellsOfBody walks `variables_` and asks each one's kind).
        variables_.erase(cell);
        if (const auto modifiers = nodeModifierMaps_.find(currGraph_.get());
            modifiers != nodeModifierMaps_.end()) {
            modifiers->second.erase(nodeIdOf(cell));
        }
        // What is left of the unit loses its last user one node at a time: the cell, then the value
        // it was made with, then that value's own source. Only a value of this unit is taken -- a
        // data node or a copy -- so nothing that can do something on its own is dropped here.
        for (camel::runtime::gc_node_ref_t node = cellId;;) {
            const camel::runtime::DraftNodeHeader *header = draft.header(node);
            if (header == nullptr || !draft.alive(node)) {
                break;
            }
            camel::runtime::gc_node_ref_t below = 0;
            const auto inputs                   = draft.normInputsOf(node);
            if (inputs.size() == 1) {
                below = inputs[0];
            }
            // A copy in either shape, so the walk still reaches the value a copy sits on.
            if (node != cellId && header->kind != camel::runtime::GCNodeKind::Data &&
                !isCopyNode(draft.ref(node))) {
                break;
            }
            // The same permission asked of one node, and the same erase: the cell goes first, then
            // the value it was made with, then that value's own source. A node anything still
            // reads keeps itself and the value below it -- that value is shared with something the
            // program wrote, and the unit is what carries it.
            const camel::runtime::gc_node_ref_t only[1] = {node};
            if (!draft.eraseUnreadNodes(only)) {
                break;
            }
            if (below == 0) {
                break;
            }
            node = below;
        }
    }
}

} // namespace camel::compile::gir
