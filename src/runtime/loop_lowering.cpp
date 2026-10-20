/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The private lowering of LOOP nodes to tail recursion (camel/runtime/loop_lowering.h).
 *
 * Every loop gets three new graphs, made in one GraphDraftGroup (rec and next call each other):
 *   rec    the loop itself: computes whether to go on and branches;
 *   next   the arm that goes on: one round of the body, then the tail call of rec;
 *   done   the arm that stops: the state.
 * The graph that held the LOOP node is then edited in a draft session over the program's root:
 * the node becomes a FUNC node calling rec, with the same users, control inputs and origin, so
 * a fault in a lowered loop is reported where the loop is written. The session is committed
 * detached: the program the context holds keeps its LOOP nodes.
 */

#include "camel/runtime/loop_lowering.h"

#include "camel/core/mm.h"
#include "camel/core/operator_table.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/loop.h"
#include "camel/runtime/reachable.h"
#include "camel/utils/assert.h"

#include <array>
#include <cstring>
#include <format>
#include <memory>
#include <string>
#include <vector>

namespace camel::runtime {

using namespace camel::core::type;
using camel::core::loopHasCondition;
using camel::core::loopHasIterable;
using camel::core::rtdata::toSlot;

namespace {

/// What the three graphs of one lowered loop need to know about it.
struct LoopShape {
    GCGraph *owner         = nullptr; // the graph the LOOP node is in
    gc_node_ref_t node     = kInvalidNodeRef;
    LoopKind kind          = LoopKind::While;
    GCGraph *cond          = nullptr;
    GCGraph *body          = nullptr;
    size_t stateCount      = 0;
    size_t collectionCount = 0;              // s: values collected into arrays each round
    Type *stateType        = nullptr;        // the LOOP node's value: the state (or state+tape)
    Type *bodyType         = nullptr;        // the body's exit (LoopView::bodyResultType)
    std::vector<Type *> stateTypes;          // the k values of the state
    std::vector<Type *> collectionTypes;     // Array<T>, one per collection slot
    std::vector<Type *> collectionElemTypes; // T, one per collection slot
    std::vector<Type *> captureTypes;        // the types of the body's capture ports
    std::vector<std::string> captureNames;   // what they capture, for dumps and std::cpp
    Type *arrayType = nullptr;               // For, ForWhile
    uint64_t origin = 0;                     // where the loop is written

    bool iterates() const { return loopHasIterable(kind); }
    /// True when the state travels in a tuple: k > 1, or the loop collects (then the tuple is
    /// (state…, tape…), the LOOP node's value).
    bool wrapsState() const { return stateCount > 1 || collectionCount != 0; }
};

/// The names the three graphs give their own parameters: of the compiler's reserved form
/// (`__x__`), so that no captured name of the program equals one of them.
constexpr const char *kStateName = "__state__";
constexpr const char *kRoundName = "__round__";
constexpr const char *kItemsName = "__items__";

/// A parameter of a lowered graph: its name and type.
using NamedParam = std::pair<std::string, Type *>;

/// The interned signature of a lowered graph, with the names of its parameters (std::cpp and
/// the dumps name ports by them).
FunctionType *
signatureOf(const std::vector<NamedParam> &with, const std::vector<NamedParam> &norm, Type *exit) {
    FunctionTypeFactory factory;
    for (const auto &[name, type] : with) {
        factory.addWithArg(name, type, false);
    }
    for (const auto &[name, type] : norm) {
        factory.addNormArg(name, type, false);
    }
    factory.setExitType(exit);
    return factory.build();
}

/// The captures of a loop as parameters of a lowered graph.
std::vector<NamedParam> captureParams(const LoopShape &loop) {
    std::vector<NamedParam> params;
    for (size_t c = 0; c < loop.captureTypes.size(); ++c) {
        params.emplace_back(loop.captureNames[c], loop.captureTypes[c]);
    }
    return params;
}

/// A port of type `type` appended to the Norm (`with` false) or With ports of `draft`.
gc_node_ref_t addPort(GraphDraft &draft, Type *type, bool with) {
    const gc_node_ref_t port = draft.addPortNode(type, draft.allocateRuntimeSlot(type));
    if (with) {
        draft.appendWithPort(port);
    } else {
        draft.appendNormPort(port);
    }
    return port;
}

gc_node_ref_t addOper(
    GraphDraft &draft, Type *type, std::string_view uri, std::span<const gc_node_ref_t> inputs) {
    const gc_node_ref_t node = draft.addOperNode(type, nullptr, uri);
    draft.setNormInputs(node, inputs);
    return node;
}

gc_node_ref_t addCall(
    GraphDraft &draft, GCGraph *callee, Type *type, std::span<const gc_node_ref_t> norm,
    std::span<const gc_node_ref_t> with) {
    const gc_node_ref_t node = draft.addFuncNode(callee, type);
    draft.setNormInputs(node, norm);
    draft.setWithInputs(node, with);
    draft.addDependency(callee);
    return node;
}

gc_node_ref_t addInt(GraphDraft &draft, int64_t value) {
    return draft.materializeStaticValue(toSlot<int64_t>(value), Type::Int64());
}

/// The values of the state a graph's condition or body call takes: S itself when it is not
/// wrapped, its first k elements (ACCS) when it is (k > 1, or the loop collects).
std::vector<gc_node_ref_t> unpackState(GraphDraft &draft, const LoopShape &loop, gc_node_ref_t s) {
    if (!loop.wrapsState()) {
        return {s};
    }
    std::vector<gc_node_ref_t> values;
    for (size_t j = 0; j < loop.stateCount; ++j) {
        const gc_node_ref_t element =
            draft.addAccsNode(loop.stateTypes[j], static_cast<uint32_t>(j));
        draft.setNormInputs(element, std::span<const gc_node_ref_t>(&s, 1));
        values.push_back(element);
    }
    return values;
}

/// Gives every node of `draft` the origin of the loop (the condition and body graphs keep their
/// own): what goes wrong in the machinery of a lowered loop is reported at the loop.
void stampOrigin(GraphDraft &draft, uint64_t origin) {
    for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        if (draft.alive(id) && draft.nodeOrigin(id) == 0) {
            draft.setNodeOrigin(id, origin);
        }
    }
}

void setResult(GraphDraft &draft, gc_node_ref_t result) {
    draft.setOutputNode(result);
    draft.setExitNode(result);
    draft.setReturnNode(result, GCReturnKind::Self);
}

/// `done(S) = S`.
std::unique_ptr<GraphDraft> buildDone(const LoopShape &loop) {
    auto draft = std::make_unique<GraphDraft>();
    draft->setFuncType(signatureOf({{kStateName, loop.stateType}}, {}, loop.stateType));
    const gc_node_ref_t s = addPort(*draft, loop.stateType, true);
    setResult(*draft, s);
    return draft;
}

/// An ACCS node reading component `index` of `tuple`.
gc_node_ref_t addAccs(GraphDraft &draft, gc_node_ref_t tuple, Type *type, uint32_t index) {
    const gc_node_ref_t node = draft.addAccsNode(type, index);
    draft.setNormInputs(node, std::span<const gc_node_ref_t>(&tuple, 1));
    return node;
}

/// `next(S; c, i, xs) = rec(i + 1, body(xs[i], S; c); c, xs)`; a collecting loop's body also
/// produces the values of the slots, which are appended to the tapes the state carries (one
/// `:loop/collect` per slot), and the next state is the (state…, tape…) tuple.
std::unique_ptr<GraphDraft> buildNext(const LoopShape &loop, GCGraph *rec) {
    auto draft = std::make_unique<GraphDraft>();
    std::vector<NamedParam> withParams{{kStateName, loop.stateType}};
    for (auto &param : captureParams(loop)) {
        withParams.push_back(std::move(param));
    }
    if (loop.iterates()) {
        withParams.emplace_back(kRoundName, Type::Int64());
        withParams.emplace_back(kItemsName, loop.arrayType);
    }
    draft->setFuncType(signatureOf(withParams, {}, loop.stateType));

    const gc_node_ref_t s = addPort(*draft, loop.stateType, true);
    std::vector<gc_node_ref_t> captures;
    for (Type *type : loop.captureTypes) {
        captures.push_back(addPort(*draft, type, true));
    }
    gc_node_ref_t index = kInvalidNodeRef, array = kInvalidNodeRef;
    if (loop.iterates()) {
        index = addPort(*draft, Type::Int64(), true);
        array = addPort(*draft, loop.arrayType, true);
    }

    std::vector<gc_node_ref_t> bodyArgs;
    if (loop.iterates()) {
        const std::array<gc_node_ref_t, 2> at{array, index};
        bodyArgs.push_back(addOper(
            *draft,
            static_cast<ArrayType *>(loop.arrayType)->elemType(),
            ":op/idx_arr",
            at));
    }
    for (gc_node_ref_t value : unpackState(*draft, loop, s)) {
        bodyArgs.push_back(value);
    }
    Type *producedType           = loop.collectionCount != 0 ? loop.bodyType : loop.stateType;
    const gc_node_ref_t produced = addCall(*draft, loop.body, producedType, bodyArgs, captures);

    gc_node_ref_t nextState = produced;
    if (loop.collectionCount != 0) {
        std::vector<size_t> slots;
        std::vector<gc_node_ref_t> values;
        for (size_t j = 0; j < loop.stateCount; ++j) {
            values.push_back(
                addAccs(*draft, produced, loop.stateTypes[j], static_cast<uint32_t>(j)));
            slots.push_back(j);
        }
        for (size_t c = 0; c < loop.collectionCount; ++c) {
            const uint32_t at         = static_cast<uint32_t>(loop.stateCount + c);
            const gc_node_ref_t value = addAccs(*draft, produced, loop.collectionElemTypes[c], at);
            const gc_node_ref_t tape  = addAccs(*draft, s, loop.collectionTypes[c], at);
            const std::array<gc_node_ref_t, 2> operands{tape, value};
            const gc_node_ref_t collect =
                addOper(*draft, loop.collectionTypes[c], ":loop/collect", operands);
            // Growing the tape can exhaust memory: the failure is the loop's allocation.
            draft->setNodeOrigin(collect, loop.origin);
            values.push_back(collect);
            slots.push_back(loop.stateCount + c);
        }
        auto *empty =
            ::Tuple::create(loop.stateCount + loop.collectionCount, camel::core::mm::autoSpace());
        const gc_node_ref_t source = draft->materializeStaticValue(
            toSlot<camel::core::rtdata::Object *>(empty),
            loop.stateType);
        nextState = draft->addFillNode(loop.stateType, makeFillPayload(GCFillKind::Tuple, slots));
        draft->setNormInputs(nextState, std::span<const gc_node_ref_t>(&source, 1));
        draft->setWithInputs(nextState, values);
        draft->setNodeOrigin(nextState, loop.origin);
    }

    std::vector<gc_node_ref_t> recNorm, recWith = captures;
    if (loop.iterates()) {
        const std::array<gc_node_ref_t, 2> step{index, addInt(*draft, 1)};
        recNorm.push_back(addOper(*draft, Type::Int64(), ":op/add_l", step));
        recWith.push_back(array);
    }
    recNorm.push_back(nextState);
    setResult(*draft, addCall(*draft, rec, loop.stateType, recNorm, recWith));
    stampOrigin(*draft, loop.origin);
    return draft;
}

/// `rec(i, S; c, xs) = if go then next(...) else done(S)`, the arms as graphs under a BRCH.
std::unique_ptr<GraphDraft> buildRec(const LoopShape &loop, GCGraph *next, GCGraph *done) {
    auto draft                         = std::make_unique<GraphDraft>();
    std::vector<NamedParam> withParams = captureParams(loop), normParams;
    if (loop.iterates()) {
        withParams.emplace_back(kItemsName, loop.arrayType);
        normParams.emplace_back(kRoundName, Type::Int64());
    }
    normParams.emplace_back(kStateName, loop.stateType);
    draft->setFuncType(signatureOf(withParams, normParams, loop.stateType));

    gc_node_ref_t index = kInvalidNodeRef, array = kInvalidNodeRef;
    if (loop.iterates()) {
        index = addPort(*draft, Type::Int64(), false);
    }
    const gc_node_ref_t s = addPort(*draft, loop.stateType, false);
    std::vector<gc_node_ref_t> captures;
    for (Type *type : loop.captureTypes) {
        captures.push_back(addPort(*draft, type, true));
    }
    if (loop.iterates()) {
        array = addPort(*draft, loop.arrayType, true);
    }

    // Whether to go on: the condition first (it sees the state), then an element left.
    gc_node_ref_t go = kInvalidNodeRef;
    if (loopHasCondition(loop.kind)) {
        go = addCall(*draft, loop.cond, Type::Bool(), unpackState(*draft, loop, s), captures);
    }
    if (loop.iterates()) {
        const gc_node_ref_t length = addOper(
            *draft,
            Type::Int64(),
            ":struct/len_arr",
            std::span<const gc_node_ref_t>(&array, 1));
        const std::array<gc_node_ref_t, 2> bound{index, length};
        const gc_node_ref_t left = addOper(*draft, Type::Bool(), ":op/lt_l", bound);
        if (go == kInvalidNodeRef) {
            go = left;
        } else {
            const std::array<gc_node_ref_t, 2> both{go, left};
            go = addOper(*draft, Type::Bool(), ":op/and", both);
        }
    }

    // The branch, as the front end builds an `if`: each arm a FUNC under the BRCH, every value
    // an arm takes ready before the BRCH, the JOIN the graph's exit.
    std::vector<gc_node_ref_t> nextWith{s};
    nextWith.insert(nextWith.end(), captures.begin(), captures.end());
    if (loop.iterates()) {
        nextWith.push_back(index);
        nextWith.push_back(array);
    }
    const gc_node_ref_t join = draft->addJoinNode(loop.stateType, kInvalidNodeRef, 2);
    const gc_node_ref_t goOn = draft->addFuncNode(next, loop.stateType);
    const gc_node_ref_t stop = draft->addFuncNode(done, loop.stateType);
    draft->addDependency(next);
    draft->addDependency(done);
    draft->addSubGraph(next);
    draft->addSubGraph(done);
    const std::array<GCBranchArm, 2> arms{
        GCBranchArm{.head = goOn, .tail = goOn},
        GCBranchArm{.head = stop, .tail = stop},
    };
    const gc_node_ref_t brch = draft->addBrchNode(Type::Int64(), join, arms);
    draft->setNormInputs(brch, std::span<const gc_node_ref_t>(&go, 1));
    draft->setCtrlInputs(brch, nextWith);
    reinterpret_cast<GCJoinBody *>(draft->mutablePayloadOf(join).data())->brch = brch;
    draft->setCtrlInputs(goOn, std::span<const gc_node_ref_t>(&brch, 1));
    draft->setWithInputs(goOn, nextWith);
    draft->setCtrlInputs(stop, std::span<const gc_node_ref_t>(&brch, 1));
    draft->setWithInputs(stop, std::span<const gc_node_ref_t>(&s, 1));
    draft->setNormInputs(join, std::span<const gc_node_ref_t>(&brch, 1));
    const std::array<gc_node_ref_t, 2> armValues{goOn, stop};
    draft->setWithInputs(join, armValues);
    setResult(*draft, join);
    stampOrigin(*draft, loop.origin);
    return draft;
}

LoopShape shapeOf(GCGraph *owner, gc_node_ref_t node) {
    const LoopView view(*owner, node);
    LoopShape loop;
    loop.owner           = owner;
    loop.node            = node;
    loop.kind            = view.kind();
    loop.cond            = view.condGraph();
    loop.body            = view.bodyGraph();
    loop.stateCount      = view.stateCount();
    loop.collectionCount = view.collectionCount();
    loop.stateType       = view.valueType();
    loop.bodyType        = view.bodyResultType();
    loop.origin          = owner->nodeOrigin(node);
    for (size_t j = 0; j < loop.stateCount; ++j) {
        loop.stateTypes.push_back(view.stateType(j));
    }
    for (size_t c = 0; c < loop.collectionCount; ++c) {
        loop.collectionTypes.push_back(view.collectionArrayType(c));
        loop.collectionElemTypes.push_back(view.collectionElemType(c));
    }
    const auto names = loop.body->captureNames();
    for (size_t c = 0; c < view.captures().size(); ++c) {
        loop.captureTypes.push_back(loop.body->node(view.bodyCapturePort(c))->dataType());
        loop.captureNames.push_back(
            c < names.size() ? std::string(camel::core::symbolText(names[c]))
                             : std::format("__capture{}__", c));
    }
    if (const auto iterable = view.iterable()) {
        loop.arrayType = owner->node(*iterable)->dataType();
    }
    return loop;
}

/// Replaces the LOOP node of `loop` in `draft` (the draft of loop.owner) by a call of `rec`.
void replaceLoop(GraphDraft &draft, const LoopShape &loop, GCGraph *rec) {
    const gc_node_ref_t id = draft.draftIdOfSourceRef(loop.node);
    ASSERT(id != kInvalidNodeRef, "A lowered loop's node is not in its graph's draft.");
    const auto norm = draft.normInputsOf(id);
    const size_t at = loop.iterates() ? 1 : 0;
    std::vector<gc_node_ref_t> initial(norm.begin() + static_cast<std::ptrdiff_t>(at), norm.end());
    const gc_node_ref_t array = loop.iterates() ? norm.front() : kInvalidNodeRef;

    // The first state: the one first value, or the tuple of them; a collecting loop also starts
    // with one empty tape per slot.
    gc_node_ref_t first = initial.front();
    if (loop.wrapsState()) {
        std::vector<gc_node_ref_t> values = initial;
        std::vector<size_t> slots(initial.size());
        for (size_t j = 0; j < slots.size(); ++j) {
            slots[j] = j;
        }
        for (size_t c = 0; c < loop.collectionCount; ++c) {
            const gc_node_ref_t tape = addOper(
                draft,
                loop.collectionTypes[c],
                ":loop/collector",
                std::span<const gc_node_ref_t>());
            draft.setNodeOrigin(tape, loop.origin);
            values.push_back(tape);
            slots.push_back(loop.stateCount + c);
        }
        auto *empty                = ::Tuple::create(values.size(), camel::core::mm::autoSpace());
        const gc_node_ref_t source = draft.materializeStaticValue(
            toSlot<camel::core::rtdata::Object *>(empty),
            loop.stateType);
        first = draft.addFillNode(loop.stateType, makeFillPayload(GCFillKind::Tuple, slots));
        draft.setNormInputs(first, std::span<const gc_node_ref_t>(&source, 1));
        draft.setWithInputs(first, values);
        draft.setNodeOrigin(first, loop.origin);
    }
    std::vector<gc_node_ref_t> recNorm;
    if (loop.iterates()) {
        const gc_node_ref_t zero = addInt(draft, 0);
        draft.setNodeOrigin(zero, loop.origin);
        recNorm.push_back(zero);
    }
    recNorm.push_back(first);
    std::vector<gc_node_ref_t> recWith(
        draft.withInputsOf(id).begin(),
        draft.withInputsOf(id).end());
    if (loop.iterates()) {
        recWith.push_back(array);
    }

    const DraftNodeHeader *header = draft.header(id);
    const GCFuncRef body{.ref = draft.payloadGraphIndexOf(rec)};
    std::vector<std::byte> payload(sizeof(body));
    std::memcpy(payload.data(), &body, sizeof(body));
    const std::vector<gc_node_ref_t> ctrl(
        draft.ctrlInputsOf(id).begin(),
        draft.ctrlInputsOf(id).end());
    const std::vector<gc_node_ref_t> normUsers(
        draft.normUsersOf(id).begin(),
        draft.normUsersOf(id).end());
    const std::vector<gc_node_ref_t> withUsers(
        draft.withUsersOf(id).begin(),
        draft.withUsersOf(id).end());
    const std::vector<gc_node_ref_t> ctrlUsers(
        draft.ctrlUsersOf(id).begin(),
        draft.ctrlUsersOf(id).end());
    draft.rewriteNode(
        id,
        DraftNodeInit{
            .dataIndex    = header->dataIndex,
            .dataType     = header->dataType,
            .kind         = GCNodeKind::Func,
            .runtimeFlags = header->runtimeFlags,
            .payload      = payload,
            .normInputs   = recNorm,
            .withInputs   = recWith,
            .ctrlInputs   = ctrl,
            .normUsers    = normUsers,
            .withUsers    = withUsers,
            .ctrlUsers    = ctrlUsers,
        });
    draft.addDependency(rec);
    draft.addSubGraph(rec);
}

/// In `draft`, the inputs `inputs` with every one of function type passed through
/// `:graph/original` (one new node per input, at `origin`).
std::vector<gc_node_ref_t> throughOriginals(
    GraphDraft &draft, std::span<const gc_node_ref_t> inputs, uint64_t origin, bool &changed) {
    std::vector<gc_node_ref_t> result(inputs.begin(), inputs.end());
    for (gc_node_ref_t &input : result) {
        Type *type = draft.header(input)->dataType;
        if (type != nullptr && type->code() == TypeCode::Function) {
            const gc_node_ref_t value = input;
            input =
                addOper(draft, type, kGraphOriginalUri, std::span<const gc_node_ref_t>(&value, 1));
            draft.setNodeOrigin(input, origin);
            changed = true;
        }
    }
    return result;
}

/**
 * The operators of the copy that read the graphs of the function values they receive
 * (OperatorTraits::inspectsGraphs: Tiles compiling a function, an export) must see the program
 * as it was written, not this private lowering (op-semantics 11.4). Every operand of function
 * type of such an operator is passed through `:graph/original`, which takes a function value
 * of the copy back to the program's graph it was copied from (Program::original; the
 * commit records it). The new node takes the operator's origin.
 */
void handOriginalsToGraphReaders(RuntimeGraphDraftSession &session) {
    const auto &table = camel::core::OperatorTable::instance();
    for (GCGraph *graph : session.collectReachableRuntimeGraphs()) {
        std::vector<gc_node_ref_t> readers;
        if (const GraphDraft *draft = session.tryDraft(graph)) {
            for (gc_node_ref_t id = 0; id < draft->nodeSlotCount(); ++id) {
                const DraftNodeHeader *header = draft->alive(id) ? draft->header(id) : nullptr;
                if (header && header->kind == GCNodeKind::Oper &&
                    table.inspectsGraphs(
                        reinterpret_cast<const GCOperBody *>(draft->payloadOf(id).data())->oper)) {
                    readers.push_back(id);
                }
            }
        } else {
            for (gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
                if (graph->node(ref)->kind == GCNodeKind::Oper &&
                    table.inspectsGraphs(graph->nodeBodyAs<GCOperBody>(ref)->oper)) {
                    readers.push_back(ref); // a draft decoded from it numbers its nodes alike
                }
            }
        }
        if (readers.empty()) {
            continue;
        }
        GraphDraft &draft = session.edit(graph);
        for (gc_node_ref_t id : readers) {
            const std::vector<gc_node_ref_t> norm(
                draft.normInputsOf(id).begin(),
                draft.normInputsOf(id).end());
            const std::vector<gc_node_ref_t> with(
                draft.withInputsOf(id).begin(),
                draft.withInputsOf(id).end());
            const uint64_t origin = draft.nodeOrigin(id);
            bool changed          = false;
            const std::vector<gc_node_ref_t> newNorm =
                throughOriginals(draft, norm, origin, changed);
            const std::vector<gc_node_ref_t> newWith =
                throughOriginals(draft, with, origin, changed);
            if (changed) {
                draft.setNormInputs(id, newNorm);
                draft.setWithInputs(id, newWith);
            }
        }
    }
}

} // namespace

bool programHasLoops(GCGraph *root) {
    bool found = false;
    forEachReachableGraph(root, [&](GCGraph *graph) {
        for (gc_node_ref_t ref = 0; !found && ref < graph->nodeCount(); ++ref) {
            found = graph->node(ref)->kind == GCNodeKind::Loop;
        }
    });
    return found;
}

const Program *lowerLoopsToTailRecursion(
    const camel::core::context::context_ptr_t &context, const Program *program) {
    return lowerLoopsToTailRecursion(context, program, program ? program->root() : nullptr);
}

const Program *lowerLoopsToTailRecursion(
    const camel::core::context::context_ptr_t &context, const Program *program, GCGraph *root) {
    if (program == nullptr || root == nullptr || !programHasLoops(root)) {
        return program;
    }
    RuntimeGraphDraftSession session(context, program, root);
    std::vector<LoopShape> loops;
    for (GCGraph *graph : session.collectReachableRuntimeGraphs()) {
        for (gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
            if (graph->node(ref)->kind == GCNodeKind::Loop) {
                loops.push_back(shapeOf(graph, ref));
            }
        }
    }

    // The new graphs of every loop, in one group: rec and next refer to each other.
    GraphDraftGroup group(context);
    struct Members {
        GCGraph *rec, *next, *done;
    };
    std::vector<Members> members;
    for (const LoopShape &loop : loops) {
        // `f.loop1.body` gives `f.loop1.rec`, the name the lowered loop runs under.
        std::string base = loop.body->name();
        if (base.ends_with(".body")) {
            base.resize(base.size() - 5);
        }
        Members m{
            .rec  = group.reserve(base + ".rec", base + ".rec"),
            .next = group.reserve(base + ".next", base + ".next"),
            .done = group.reserve(base + ".done", base + ".done"),
        };
        group.define(m.rec, buildRec(loop, m.next, m.done));
        group.define(m.next, buildNext(loop, m.rec));
        group.define(m.done, buildDone(loop));
        members.push_back(m);
    }
    group.encode();

    for (size_t i = 0; i < loops.size(); ++i) {
        replaceLoop(session.edit(loops[i].owner), loops[i], group.encoded(members[i].rec));
    }
    handOriginalsToGraphReaders(session);
    return session.commit(/*detached=*/true);
}

} // namespace camel::runtime
