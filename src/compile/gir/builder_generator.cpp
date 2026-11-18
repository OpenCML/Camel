// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The generator's machine, built in the translation (author's ruling of 2026-11-18, `96714f465`:
 * "build the generator body in GIR, and the slot unwrap checks and reports an internal error").
 *
 * Why it moves out of the construction tree at all. A generator's body is not a block of statements
 * like another: what a pull hands to the next one is a *type* -- a slot's type -- and the tree has
 * no type inference (`gct::Builder::typeOfPlaceholder` leaves `typeof#n` behind: "Nothing of the
 * expression reaches the type"). So a machine built there has to bake a type it cannot know:
 *
 *   * every cell of a generator body is made with `var typeof#n`, and `zeroValueOf(TypeVar)` is
 *     null, so `CellDecl::slotType` is null for every local and **the state carries `pc` alone**
 *     (`builder.cpp` "The slots are empty today, because a program local of a generator body is a
 *     `var T` cell and a cell type has no zero value"). A local therefore travels in a cell the
 *     step closure *captures*, not in the state -- and a value with no cell form (a `state::Ref`,
 *     the `cell_holds_ref` probe) has nowhere to travel at all;
 *   * the ports, the runtime slots and the step's signature are all made from that baked type
 *     before a single statement of the body is built (`builder_helpers.h`, `createCompileGraph`),
 *     so a type learned later cannot be put in.
 *
 * Built here, the arms' statements are visited by the ordinary visitors, so a local is born with
 * the type its value already has, and the state's types are the ones the machine writes.
 *
 * What a `yield` is to this file. It is a node of the construction tree (`EXIT` with
 * `ExitType::Yield`, built by `gct::Builder::visitExitStmt` since 2026-11-18) and nothing else: the
 * GCT machine claims every `yield` today, so no body reaching this file holds one, and this file's
 * branch is a seam, not a behaviour.
 *
 * Why there is no half-landed version of this file. Until it builds a whole generator, the branch
 * below cannot be taken: a body built with `yield` nodes and no machine has its `yield`s refused at
 * `visitExitNode` -- that is the reading the GCT statement builder kept until 2026-11-18
 * ("a `yield` reached the statement builder, so the generator machine did not claim it"), moved one
 * layer down so that it is said where the machine that could claim it lives.
 *
 * The shape it must produce, read off the machine that runs today (`std::jgir` of
 * `test/cases/compile/stream/generators_cell_type.cml`, seven graphs, `__gen_step__` with 78 nodes
 * and 116 edges):
 *
 *   labels () => string..              the cell(s) of the body, captured by the stream value
 *   __gen_step__ (__gen_state__: (int,)) => (string, string..)?
 *       ACCS .0 of the state -> `:var/new` a `var int` pc cell
 *       LOOP while over the round state `((string, string..)?, int)`  -- answer, answered
 *       inside one round: the pc decides the arm (`eq_l` 0/1/2 -> BRCH + GATE + JOIN), each arm is
 *           the body's statements up to its `yield`, and ends by packing the next state
 *           (`FILL` the pair `(elem, rest)` -> `CAST` to `(elem, rest)?` -> `FILL` the round
 *           state -> `:op/assn_l`)
 *       after the loop: `ACCS .0/.1`, the answer is what the step returns
 *
 * Four facts measured against that machine (they are in the code below, and they are the shape of
 * "the state carries `pc` alone"):
 *
 *   * the pc cell is `:var/new` fed straight by the `ACCS .0` of the state, **not**
 *     `:var/new/empty`: the state's first part is taken out as a value and the cell is made from
 *     that value, so the two nodes are an operator with an operand and its input, not a type-only
 *     cell;
 *   * the pc is a *variable of the step's scope* (`newVariable` puts it in `variables_`), which is
 *     what lets the dispatch read it as a value of the round and an arm's `:op/assn_l` write it;
 *   * the state parameter is named `__gen_state__` **and that is why the port has a name at all**:
 *     `createCompileGraph` takes a port's name from the signature, and an unnamed parameter is port
 *     `__w0` with no name in scope (the same sentence the construction tree's machine keeps at
 *     `gct/builder.cpp:7063-7066`). The name is not decoration: it enters the scope, which is what
 *     makes the two machines give one observable shape;
 *   * the round state is `(answer, int)`, i.e. `((string, string..)?, int)` -- the `?` is on the
 *     **first** component and the pc is a bare `int`. A `?` on the pc, or its absence on the answer,
 *     would be a difference of structure and not of ids.
 *
 * Two readings taken from the construction tree's own machine today. They are the *transition* and
 * not the destination (`gct/builder.cpp:6277-6285`, `6315`, `7017-7030`):
 *
 *   * the loop's first round state is `(Data::null(), 0)` declared as `roundStateType`: an **untyped
 *     null data node cast to the answer's type**, and the pc's zero as an ordinary `int` literal --
 *     not a null of `T?`. That is what `stateZero()` gives while every slot is a cell, and it is
 *     exactly what goes away once a slot is a `T?`: the answer's starting value then comes from the
 *     `T?` itself and no null-and-cast pair stands there. **Do not read the pair as the shape this
 *     machine aims at**: it is the shape that can be built while the slots are still cells, and it
 *     is written down here so that the next reader knows which of the two they are looking at.
 *   * the loop's condition tests the round state's second part with a **call to `__eq__`**
 *     (`__eq__(state.1, 0)`), and so does an arm's dispatch (`equalTo` at `gct/builder.cpp:6072`
 *     is `makeLink(2) << makeDRef("__eq__") << readCell(name) << intLiteral(value)`). **Both are the
 *     same operator, seen at two layers**, and this was read off the published graph and not off the
 *     construction tree: `std::jgir` of the generator case holds **four** `:op/eq_l` nodes, three in
 *     the arm regions and one in the *loop condition's* region (`g4r1`), each fed by a `DATA` int
 *     literal. So `__eq__` on two `int`s **is** `:op/eq_l` once the group is resolved, and a machine
 *     that built one place as a group call and the other as a direct operator would publish the same
 *     graph while differing from itself at the construction layer -- which is a difference nobody can
 *     see and everybody would have to keep in step. Build both as the group call and let the
 *     resolution decide. (An earlier draft of this comment called them two operators; the target
 *     said otherwise, and the target is the one that is read.)
 *
 * Three conventions of a region, which the loop below is built by (`builder_control.cpp:2643-2690`,
 * `:3568-3599`, `:3730-3735`). Two of them look like details and one of them is a trap:
 *
 *   * a loop's regions are made **before** the loop node exists, so the owner is written after it:
 *     `draft.addRegion(kind, outerRegion)` then `setRegionOwner(region, nodeIdOf(loopNode), index)`,
 *     where **`index` is `0` for the condition and `1` for the body** (`graph.h:601` names the kinds,
 *     `Arm`, `LoopCond`, `LoopBody`). That `0`/`1` is **not** an order of construction and must not be
 *     read as one -- it is which of the loop's two parts the region is. **And the order of
 *     construction is visible in the graph anyway**: the published `std::jgir` numbers the regions in
 *     the order they were made. In the generator case, measured on the published `std::jgir` of
 *     `test/cases/compile/stream/generators_cell_type.cml`: the condition's region is `1`, the
 *     body's is `2`, and the **six** arm regions are `3` to `8` -- so the condition's region is
 *     made **first**, and this sentence's earlier draft named `g4r2`, `g4r4`, `g4r6` as the arm
 *     regions, which was a transcription and not a reading: the arms are six, and two of the
 *     three names were the body's region. The two
 *     sentences belong together: the index says *which part*, the numbering says *which was made
 *     first*, and a machine that read the first as the second would number the regions the other way
 *     round and change the graph.
 *   * a region's ports are set **twice**, and both are needed: `setRegionPorts(region, ids)` (the
 *     Port nodes, which must already have been put in that region with `setNodeRegion`) and
 *     `setRegionParams(region, params)` -- a `FunctionTypeFactory` of the same names and types with
 *     `setExitType(Type::Void())`. With only the first, the region's signature is empty.
 *   * a region port node is **explicitly put in its region** (`setNodeRegion(nodeIdOf(port),
 *     region)`), or it lands in the body region (0) and reads as a parameter of the graph.
 *
 * Where a `var` written inside an arm goes -- asked, read, answered (`builder_control.cpp:2134`):
 * `mergeArmVariables` is not a mechanism of regions. It walks the `ArmBuild` bookkeeping a *visitor*
 * keeps (`arm->touched`, `touchedOrder`, `recordWrite`, `pendingUsesOf`) and does two things: it
 * writes a control link from the arm's last writer to the join, and it records that a reader after
 * the join sees the written version. Neither is about a region's ports -- a region is not a scope of
 * its own for a `var`, the cell is one node of the graph's body wherever it is written -- and both
 * are about *the join being where a reader stands*. This machine has no such reader: the pc is read
 * at the top of the next round, and the round state is the loop's own value. So nothing here merges
 * anything back, and `ArmBuild` is not imported: the two machines keep the same shape because the
 * values that cross a round are region ports, not writes a later statement reads. If a future arm
 * ever wrote a scope `var` that a statement *after* the branch reads, this is the paragraph that
 * says why the bookkeeping would have to come with it.
 *
 * "By arm" therefore means "by the round's pc branch", not by a branch of the source: a call body
 * sits inside one arm, and the pc is what carries one pull across the pull before it.
 */

#include "builder.h"
#include "builder_helpers.h"
#include "builder_support.h"

#include "camel/core/data/composite/tuple.h"
#include "camel/core/data/primary.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type.h"
#include "camel/core/type/composite/stream.h"
#include "camel/runtime/graph.h"

#include <string>

namespace camel::compile::gir {

namespace {

/// Whether `node` writes a `yield` of its own. A function or a lambda written inside it is a body of
/// its own: what it yields belongs to its graph, and its `yield` node is the child of its own FUNC,
/// which this walk does not enter.
bool writesOwnYieldIn(const GCT::node_ptr_t &node) {
    if (node == nullptr) {
        return false;
    }
    switch (node->type()) {
    case GCT::NodeKind::FUNC:
        // The body of a nested function or lambda, and its type: neither is this body's.
        return false;
    case GCT::NodeKind::EXIT:
        return node->exitType() != ExitType::Return;
    default:
        break;
    }
    for (size_t i = 0; i < node->size(); ++i) {
        if (writesOwnYieldIn(node->at(i))) {
            return true;
        }
    }
    return false;
}

} // namespace

GCT::node_ptr_t Builder::ownYieldBodyOf(const GCT::node_ptr_t &funcGct) const {
    // Children of a FUNC: TYPE, then the body (a `gct/types.h` comment says so where the kind is
    // declared). A body that holds no `yield` is the case of every body today.
    if (funcGct == nullptr || funcGct->size() < 2) {
        return nullptr;
    }
    const GCT::node_ptr_t body = funcGct->at(1);
    return writesOwnYieldIn(body) ? body : nullptr;
}

void Builder::buildGeneratorBody(
    const GCT::node_ptr_t &gct, const compile_graph_ptr_t &graph, FunctionType *funcType) {
    // The head writes what one pull hands out, and that is where the machine's types come from: the
    // element (`string`), and the step's own signature (`() => (string, string..)?`).
    Type *const exit = funcType != nullptr ? funcType->exitType() : nullptr;
    if (exit == nullptr || exit->code() != TypeCode::Stream) {
        // A body that yields hands out a stream. Saying it here and not at the first `yield` is the
        // ruling the GCT machine keeps (`YieldRequiresStreamHead`): the head is what is wrong.
        diags_->of(SemanticDiag::YieldRequiresStreamHead)
            .atOrigin(gct != nullptr ? gct->at(0)->origin() : camel::source::kInvalidOriginId)
            .commit();
        throw BuildAbortException();
    }
    const auto *stream        = static_cast<const StreamType *>(exit);
    const Type *const element = stream->elemType();
    (void)graph;
    // The answer one pull gives: `(elem, rest)?` -- the element and the stream the next pull
    // continues from, or null where the body ended.
    Type *const answerType = TupleType::nullable(TupleType::create({stream->elemType(), exit}));
    // The state one pull hands to the next. Today it is `pc` alone, and that is the fact this
    // segment is built on (see the head of the file): every local of the body is a `var T` cell
    // whose type the construction tree could not know, so no local travels in the state yet. The
    // next segment is what puts them there, one `T?` slot each.
    Type *const pcType    = Type::Int64();
    Type *const stateType = TupleType::create({pcType});
    // What one *round* hands to the next: the answer a pull has built (or not yet), and the point
    // the next round starts from. The `?` is on the first component only: the pc of a round is
    // always a number, and the answer of a round is often not there yet.
    Type *const roundStateType = TupleType::create({answerType, pcType});
    // The loop is not built yet, and it cannot be built in pieces the way the state port and the
    // pc cell were: its node names its regions, each region names its ports and its result, and the
    // results are the arms. A loop node without them is not a smaller loop -- it is a draft that
    // would be refused where a graph is published, so it is exactly the shape that must land whole
    // (the arms included) or not at all. What stands here is the type it will be given.
    // The step's own signature: `(__gen_state__: (int,)) => (string, string..)?`. The parameter is
    // named, so the port `createCompileGraph` makes carries that name (an unnamed one is `__w0`
    // with no name in scope).
    FunctionTypeFactory stepSignature;
    stepSignature.addNormArg("__gen_state__", stateType, false);
    stepSignature.setExitType(answerType);
    FunctionType *const stepType = stepSignature.build();
    // The step is a graph of its own, built in the scope of its signature: `enterScope` makes the
    // graph, makes the state its port, and puts the parameter's name in scope.
    const compile_graph_ptr_t stepGraph = enterScope(stepType, "__gen_step__", {});
    {
        const auto normPorts = stepGraph->view().normPorts();
        GUARD(!normPorts.empty(), "The step graph has no state port.");
        const node_handle_t statePort = stepGraph->view().ref(normPorts.front());
        // The point of the body the pull starts from: the state's first part, taken out and put in
        // a cell of its own, so that an arm which moves the point on is read by the dispatch of the
        // same pull (a value would be read as it was before the round began).
        const node_handle_t pcMember = currGraph_->addAccsNode(pcType, 0);
        linkNodes(LinkType::Norm, statePort, pcMember);
        const node_handle_t pcCell = newVariable(pcMember);
        insertNode("__gen_pc__", pcCell);

        // ---- the round state, and the two regions that carry it ----------------
        //
        // Three conventions of a region decide the shape below, and one of them is a trap
        // (`builder_control.cpp#Builder::visitLoopNode`, `#Builder::buildStreamLoop`):
        //
        //   * a loop's regions are made **before** the loop node exists, so the owner is written
        //     after it, and `setRegionOwner(region, loop, 0|1)` says *which part* the region is --
        //     `0` the condition, `1` the body -- and **not** the order of construction;
        //   * the order of construction is visible in the published graph anyway, which numbers
        //     the regions in the order they were made: in the target `g4r1` is the condition's
        //     region and `g4r2` the body's, so **the condition is made first**. Reading the first
        //     sentence as the second numbers the regions the other way round and is a different
        //     graph;
        //   * a region port is **explicitly put in its region** (`setNodeRegion`); without that
        //     it lands in the graph's own region and reads as a parameter of the graph. And a
        //     region's ports are set **twice** -- `setRegionPorts` (the Port nodes) and
        //     `setRegionParams` (their names and types) -- because with only the first the
        //     region's signature is empty.
        //
        // The first round state. `(Data::null() cast to the answer, 0)` is a **transition shape**:
        // it is what can be built while a slot is still a cell, and it is written so that the next
        // reader knows which of the two shapes they are looking at. It is **not** the shape this
        // machine aims at -- the aim is a `T?` slot and no null-and-cast pair.
        const std::vector<size_t> answerSlot{0};
        node_handle_t initialAnswer =
            castNodeTo(currGraph_->addStaticDataNode(Data::null()), answerType, gct);
        node_handle_t initialRoundState = currGraph_->addFillNode(
            roundStateType, runtime::makeFillPayload(runtime::GCFillKind::Tuple, answerSlot));
        const slot_t emptyRoundState =
            camel::core::rtdata::toSlot(::Tuple::create(2, camel::core::mm::autoSpace()));
        linkNodes(
            LinkType::Norm,
            currGraph_->view().ref(
                currGraph_->edit().materializeStaticValue(emptyRoundState, roundStateType)),
            initialRoundState);
        linkNodes(LinkType::With, initialAnswer, initialRoundState);

        const std::string answerPortName   = "__gen_answer__";
        const std::string answeredPortName = "__gen_answered__";
        std::vector<node_handle_t> condPorts;
        std::vector<node_handle_t> bodyPorts;
        // The names and types a region declares for its ports, from the ports themselves: one
        // Port node per part of the round state, put in the region, and then the region's two
        // records of them.
        const auto declarePart = [&](ArmBuild &part, std::vector<node_handle_t> &ports) {
            FunctionTypeFactory params;
            std::vector<runtime::gc_node_ref_t> ids;
            for (const auto &[name, type] :
                 {std::pair{answerPortName, answerType}, std::pair{answeredPortName, pcType}}) {
                node_handle_t port = currGraph_->addRegionPortNode(type, name);
                part.graph->edit().setNodeRegion(nodeIdOf(port), part.region);
                insertNode(name, port);
                ports.push_back(port);
                ids.push_back(nodeIdOf(port));
                params.addNormArg(name, type, false);
            }
            params.setExitType(Type::Void());
            part.graph->edit().setRegionPorts(part.region, ids);
            part.graph->edit().setRegionParams(part.region, params.build());
        };

        // The condition: `__eq__(state.1, 0)` over the round state's second part, which says
        // whether the pull in hand has answered. Written as the **group** call `__eq__` -- the
        // same group an arm's `pc` dispatch writes -- and not as a direct operator, so that both
        // places move together if the resolution ever does: the target holds four `:op/eq_l` (one
        // here, three in the arm regions), and `__eq__` on two `int`s *is* `:op/eq_l` once the
        // group is resolved. A machine that wrote one place as a group call and the other as a
        // direct operator would publish the same graph while differing from itself at the
        // construction layer -- a difference nobody can see and everybody has to keep in step.
        const GCT::node_ptr_t bodyGct = gct->at(1);
        arm_ptr_t condPart            = buildArm(
            bodyGct,
            [&]() -> node_handle_t {
                GCT::node_ptr_t call = tree_->makeLink(2);
                *call << tree_->makeDRef("__eq__") << tree_->makeDRef(answeredPortName)
                      << tree_->makeData(tt::as_shared<Data>(std::make_shared<PrimaryData<int64_t>>(0)));
                return visitLinkNode(call);
            },
            runtime::GCRegionKind::LoopCond,
            [&](ArmBuild &part) { declarePart(part, condPorts); });
        // The body's region and its ports. Its result is the round state the next round starts
        // from, and it is the arms that build it: the ports are declared here, the rounds are not.
        arm_ptr_t bodyPart = buildArm(
            bodyGct,
            [&]() -> node_handle_t { return nullptr; },
            runtime::GCRegionKind::LoopBody,
            [&](ArmBuild &part) { declarePart(part, bodyPorts); });
        // The loop node names its regions, and each region names its result. Until the arms are
        // written there is no result to name, so the node is not made: a loop laid on a region
        // without a result is not a smaller loop, it is a draft that would be refused where a
        // graph is published.
        (void)condPart;
        (void)bodyPart;
        (void)initialRoundState;
    }
    leaveScope();
    // What is written is the seam: the pc dispatch, the arms, the pack and the return are the next
    // segments of this machine. Nothing reaches this line today -- the GCT machine claims every
    // `yield`, so no body here holds one -- and a body that did would be a `yield` nobody claimed,
    // which is what this says.
    diags_->of(InternalDiag::UnknownInternalError)
        .atOrigin(gct != nullptr ? gct->origin() : camel::source::kInvalidOriginId)
        .commit(
            "the generator body of `" +
            (element != nullptr ? element->toString() : std::string("?")) +
            "..` reached the translation, and the translation builds generator bodies in segments: "
            "the state is there, its `pc` cell is made, and the round state `" +
            roundStateType->toString() +
            "` has its first value, its two regions and their ports, and its condition; the arms "
            "of the pc dispatch, the pack and the loop node that names them are not written yet. "
            "Nothing in the program is wrong: the machine missed a shape it builds");
    throw BuildAbortException();
}

} // namespace camel::compile::gir