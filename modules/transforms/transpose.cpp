// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "transpose.h"

#include "camel/core/forward.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/tangent_space.h"
#include "camel/core/type/tree.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/draft_session.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace camel::transforms {

namespace mm = camel::core::mm;
namespace rt = camel::runtime;
using camel::core::type::FunctionType;
using camel::core::type::Type;
using camel::runtime::gc_node_ref_t;
using camel::runtime::GraphId;

FunctionType *transposeSignature(FunctionType *function) {
    if (function == nullptr) {
        return nullptr;
    }
    if (function->withTypesCount() != 0 || function->normTypesCount() == 0) {
        return nullptr;
    }
    if (function->exitType() != Type::Float64()) {
        return nullptr;
    }
    // `Transpose<F> = (..ParamsOf<F>, ct: Tangent<ResultOf<F>>) => (..Tangent<ParamsOf<F>>)`: the
    // parameters of `f` come first, because a pullback may need the value it is transposing at -- a
    // branch's condition is a value, and the cotangent of an arm is only meaningful for the arm
    // that ran. The cotangent comes last, which is what makes `transpose(f)(x, 1.0)` read as "what
    // does 1.0 pull back to at x".
    // The residuals split hands the backward half: one tuple parameter holding the values the
    // walk cannot carry (a x*x needs the x), and the tangents after it. Those values are the
    // known values this signature's first parameter names -- the ruling of 2026-11-01 06:50 kept the
    // shape and changed where the known values come from.
    const size_t count = function->normTypesCount();
    if (count >= 2) {
        const auto *bundle = dynamic_cast<const camel::core::type::TupleType *>(function->normTypeAt(0));
        if (bundle == nullptr) {
            // A function of several floats and nothing else: a rule's tangent function reaches a
            // transposed half this way (`mul_tangent(x, w, dx, dw)` is what a lifted half calls
            // since `reinterp` expands a rule at the match site). Every parameter is a value the
            // transposed graph reads and every parameter is also where a cotangent goes, so the
            // transpose takes them all in the order they stand, then the cotangent, and answers one
            // cotangent per parameter, in that same order.
            camel::core::type::param_vec_t params;
            std::vector<Type *> cotangentsOfAll;
            for (size_t i = 0; i < count; ++i) {
                if (function->normTypeAt(i) != Type::Float64()) {
                    return nullptr;
                }
                params.emplace_back(Type::Float64(), false);
                cotangentsOfAll.push_back(Type::Float64());
            }
            params.emplace_back(Type::Float64(), false);
            return FunctionType::create(
                camel::core::type::param_vec_t{}, params,
                camel::core::type::TupleType::create(cotangentsOfAll));
        }
        // A residual's cotangent space may be empty: a branch's condition crosses the cut as a
        // residual (so the backward half picks the arm the forward half picked, design 4.2) and a
        // predicate is not a value anything is differentiable with respect to. Such a member is
        // accepted here and contributes nothing at the other end -- the statement is made once, in the
        // cotangent space (`addCotangent` drops a target whose type has no cotangent), rather than as a
        // per-type test at every use.
        for (size_t i = 1; i < count; ++i) {
            if (function->normTypeAt(i) != Type::Float64()) {
                return nullptr;
            }
        }
        camel::core::type::param_vec_t params;
        params.emplace_back(function->normTypeAt(0), false);
        params.emplace_back(Type::Float64(), false);
        std::vector<Type *> tangents;
        for (size_t i = 1; i < count; ++i) {
            tangents.push_back(Type::Float64());
        }
        return FunctionType::create(
            camel::core::type::param_vec_t{}, params,
            camel::core::type::TupleType::create(tangents));
    }
    if (count != 1 || function->normTypeAt(0) != Type::Float64()) {
        return nullptr;
    }
    camel::core::type::param_vec_t params;
    params.emplace_back(Type::Float64(), false);
    params.emplace_back(Type::Float64(), false);
    return FunctionType::create(camel::core::type::param_vec_t{}, params, Type::Float64());
}

/// Whether a loop whose body let a cotangent escape carries that cotangent as a state of its own.
/// On, the loop is built with a second state -- the running sum of what escaped -- and answers the
/// escaped value with the state's own cotangent plus that sum. Off, the loop refuses exactly as it
/// did before the channel existed, which is what the A/B of the reading below flips: the same case,
/// the same line, `skip=state` instead of `carried=1`.
constexpr bool kCarryEscapedCotangents = true;

/// Whether a tuple value has a cotangent slot by slot. On, reading one component of a tuple fills
/// that slot and leaves the rest zero, so a value built as a tuple can be differentiated through and a
/// loop that carries several states can be transposed at all. Off, a component read drops its
/// cotangent as a read of a known value -- which is what the transform did before this channel, and
/// what the A/B of the cases below flips back to: everything tuple-shaped refuses with `skip=output`.
constexpr bool kTupleCotangents = true;

/// Whether a value whose type has no tangent space is a zero of the derivative rather than a reason to
/// refuse. On, a cotangent that lands on such a value is dropped, and the reading says which value it
/// was and why -- an integer is a step function, constant between its jumps, so `float(int(3x))`
/// contributes nothing to the derivative of what it is added to, and `x + float(int(3x))` answers 1.
/// The type answers this (`camel/core/tangent_space.h`), not the node kind: the same rule covers the
/// booleans, the strings and a function value. Off, an integer in a loop body stops the body's walk
/// where it stands -- which is what the transform did before this rule, and what the A/B of the case
/// below flips back to: `skip=state`, with no answer at all.
constexpr bool kZeroTangentIntegers = true;

/// Whether a callee that arrives as a *value* is named. `calleeIdOf` answers for a FUNC node -- a call
/// written by name; a function value put into a `let` and called is a DATA node holding a static
/// function, and `GCGraph::staticFunctionGraphOf` is the one entry that turns it into a graph id (or
/// says which check refused: not a data node, no static slot, not a function type, no function value,
/// no graph, not a static reference of this graph). Off, that shape refuses with
/// `skip=call callee=none`, which is what the transform did before this entry existed -- the A/B of
/// `transpose_function_value_no_capture.cml` flips exactly this. On, a value with no capture names its
/// graph and transposes; a closure names its graph too, and then fails later, on the capture.
constexpr bool kNameFunctionValues = true;

/// What the graphs of one transpose share: one group (a callee's transpose is another member of
/// it, exactly as `jvp` lifts one graph per callee) and the callees already transposed, so that
/// two calls of one function call one graph.
struct TransposeState {
    camel::runtime::GraphDraftGroup &group;
    const camel::runtime::Program *program;
    std::unordered_map<uint32_t, GraphId> callees;
};

bool buildTransposeMember(
    TransposeState &state, camel::runtime::GCGraph *function, const GraphId member,
    std::ostream &os) {
    camel::runtime::GraphDraftGroup &group = state.group;
    if (state.program == nullptr || function == nullptr) {
        return false;
    }
    FunctionType *signature = transposeSignature(function->funcType());
    if (signature == nullptr) {
        os << "TRANSPOSE function=" << function->name() << " skip=signature\n";
        return false;
    }

    const size_t nodeCount         = function->nodeCount();
    const size_t paramCount        = function->funcType()->normTypesCount();
    std::vector<std::string> usedRules;
    std::vector<uint32_t> armRegionsBuilt;
    gc_node_ref_t result = rt::kInvalidNodeRef;
    std::function<gc_node_ref_t(gc_node_ref_t)> knownValue;
    {
        auto &draft = group.transaction(member).edit();
        draft.setFuncType(signature);
        // Three shapes reach this, and the type says which one it is: a function of one float, a
        // split half (the residuals as one tuple, the tangents after it), and a plain function of
        // several floats. The first is the single-value pullback; the second and third differ in
        // where the values the graph reads come from -- a residual tuple or the parameters
        // themselves.
        const bool bundlesResiduals =
            paramCount > 1 && dynamic_cast<const camel::core::type::TupleType *>(
                                  function->funcType()->normTypeAt(0)) != nullptr;
        const bool plainParameters = paramCount > 1 && !bundlesResiduals;
        std::vector<gc_node_ref_t> paramPorts;
        {
            // The residual form takes the known values as one tuple; the others take one port per
            // parameter, as it stands.
            const size_t portCount = bundlesResiduals ? 1 : (plainParameters ? paramCount : 1);
            for (size_t i = 0; i < portCount; ++i) {
                Type *const portType = bundlesResiduals ? function->funcType()->normTypeAt(0)
                                                        : Type::Float64();
                const gc_node_ref_t port =
                    draft.addPortNode(portType, draft.allocateRuntimeSlot(portType));
                draft.appendNormPort(port);
                paramPorts.push_back(port);
            }
        }
        const gc_node_ref_t cotangentIn =
            draft.addPortNode(Type::Float64(), draft.allocateRuntimeSlot(Type::Float64()));
        draft.appendNormPort(cotangentIn);

        const auto primitive = [&draft](const char *uri, std::vector<gc_node_ref_t> inputs) {
            const gc_node_ref_t made = draft.addOperNode(Type::Float64(), nullptr, uri);
            draft.setNormInputs(made, std::span<const gc_node_ref_t>(inputs));
            return made;
        };
        const auto zero = [&]() {
            return draft.materializeStaticValue(
                camel::core::rtdata::toSlot<double>(0.0), Type::Float64(),
                rt::kGCNodeFlagConstant);
        };
        // A tuple of these values, as a node of the new graph, and the type that names it: the Fill
        // the builder writes for a state tuple (`builder_control.cpp`, `gir.loop.split`), so a loop
        // that carries more than one value is the shape every consumer of a loop already reads.
        const auto tupleOf = [&](const std::vector<gc_node_ref_t> &parts) {
            std::vector<Type *> partTypes(parts.size(), Type::Float64());
            Type *const tupleType = camel::core::type::TupleType::create(partTypes);
            ::Tuple *emptyTuple   = ::Tuple::create(parts.size(), mm::autoSpace());
            const gc_node_ref_t source = draft.materializeStaticValue(
                camel::core::rtdata::toSlot<camel::core::rtdata::Object *>(emptyTuple), tupleType);
            std::vector<size_t> slots;
            for (size_t i = 0; i < parts.size(); ++i) {
                slots.push_back(i);
            }
            const gc_node_ref_t tuple =
                draft.addFillNode(tupleType, rt::makeFillPayload(rt::GCFillKind::Tuple, slots));
            draft.setNormInputs(tuple, std::span<const gc_node_ref_t>(&source, 1));
            draft.setWithInputs(tuple, std::span<const gc_node_ref_t>(parts));
            return std::pair<gc_node_ref_t, Type *>{tuple, tupleType};
        };

        // The cotangent of a value in the original graph, as a node of the new one: `k summed
        // cotangents` when the value was used k times, which is the transpose of using it k times.
        std::vector<gc_node_ref_t> cotangent(nodeCount, rt::kInvalidNodeRef);
        std::vector<bool> hasCotangent(nodeCount, false);
        // A constant operand carries no cotangent, but its *value* is what a rule needs (`ct * c`).
        std::vector<gc_node_ref_t> constantOf(nodeCount, rt::kInvalidNodeRef);
        // A value the cotangent path does not carry but the transposed graph must reproduce: a
        // branch's condition is computed from the parameters, and the arm that ran is decided by
        // it. These nodes are copied forward, not transposed.
        std::vector<gc_node_ref_t> forwardOf(nodeCount, rt::kInvalidNodeRef);

        const auto isConstant = [function](gc_node_ref_t ref) {
            const camel::runtime::GCNode *node = function->node(ref);
            return node != nullptr && node->kind == rt::GCNodeKind::Data;
        };
        const auto constantValue = [&](gc_node_ref_t ref) -> gc_node_ref_t {
            if (constantOf[ref] != rt::kInvalidNodeRef) {
                return constantOf[ref];
            }
            const camel::runtime::GCNode *node    = function->node(ref);
            const std::span<const slot_t> slots = function->staticSlots();
            const std::ptrdiff_t index          = -static_cast<std::ptrdiff_t>(node->dataIndex);
            if (node->dataIndex < 0 && index >= 0 && static_cast<size_t>(index) < slots.size()) {
                constantOf[ref] = draft.materializeStaticValue(
                    slots[static_cast<size_t>(index)], node->dataType(), rt::kGCNodeFlagConstant);
            }
            return constantOf[ref];
        };
        // A round's cotangent can land on a value the round only read: that cotangent has to
        // leave the loop, and this is where it is caught. `allowEscapes` is on while a LOOP's
        // body runs -- a branch arm has nowhere to put a capture, so there an outside target is
        // still a refusal.
        bool allowEscapes = false;
        // One escaped cotangent: the product it is (`from`, the node whose rule handed it out) and
        // the value it landed on. Both are named because the refusal below has to say which node
        // escaped -- a value the round only read is the only thing that may stand for it, and a
        // reader that has the count alone cannot tell that from a port that merely stands nearby.
        struct Escape {
            gc_node_ref_t cotangent = rt::kInvalidNodeRef;
            gc_node_ref_t from      = rt::kInvalidNodeRef;
        };
        std::unordered_map<gc_node_ref_t, Escape> escapes;
        // A tuple value's cotangent, slot by slot. A component read puts its cotangent in its own
        // slot and leaves the rest empty; the value that holds the tuple turns the empty slots into
        // zeros when the walk reaches it. Keyed by the value, not by the read, because two reads of
        // one tuple are two slots of one cotangent, not two cotangents.
        std::unordered_map<gc_node_ref_t, std::vector<gc_node_ref_t>> tupleCotangents;
        // How many slots a tuple value has: a Fill knows its components, anything else has to be
        // asked its type.
        const auto tupleSlots = [&](gc_node_ref_t ref) -> size_t {
            const camel::runtime::GCNode *node = function->node(ref);
            if (node == nullptr) {
                return 0;
            }
            if (node->kind == rt::GCNodeKind::Fill) {
                return function->withInputsOf(ref).size();
            }
            const Type *type = node->dataType();
            return type != nullptr && type->code() == camel::core::type::TypeCode::Tuple
                       ? camel::core::type::tree::arity(type)
                       : 0;
        };
        // Whether a value can hold a cotangent at all. The type answers, through the same query `jvp`
        // asks of a parameter: a float (and a tuple or array of them) is its own tangent space, and
        // an integer, a boolean, a `string` or a function has none. A node with no type yet is given
        // the benefit of the doubt -- guessing "no space" would silently stop a chain that has one.
        const auto hasTangentSpace = [&](gc_node_ref_t ref) {
            const camel::runtime::GCNode *node = function->node(ref);
            if (node == nullptr) {
                return true;
            }
            const Type *type = node->dataType();
            return type == nullptr || camel::core::tangentSpaceOf(type).hasSpace();
        };
        // One line per value whose cotangent is zero because the value cannot hold one. Deduplicated:
        // a value read in three rounds is still one value contributing one zero, and a reading that
        // printed it three times would look like three different findings.
        std::vector<gc_node_ref_t> zeroedValues;
        const auto noteZero = [&](gc_node_ref_t target, gc_node_ref_t from) {
            if (std::find(zeroedValues.begin(), zeroedValues.end(), target) !=
                zeroedValues.end()) {
                return;
            }
            zeroedValues.push_back(target);
            const camel::runtime::GCNode *node = function->node(target);
            const Type *type                  = node != nullptr ? node->dataType() : nullptr;
            os << "TRANSPOSE function=" << function->name() << " contribution=0 node=" << target
               << " type=" << (type != nullptr ? type->toString() : std::string("?"))
               << " space=none from=" << from << '\n';
        };
        const auto addCotangent = [&](gc_node_ref_t ref, gc_node_ref_t node) {
            // A value with no tangent space holds no cotangent: the type says so, and the reading
            // names the value rather than leaving the zero unexplained. This is the same question
            // `jvp` asks of a parameter, asked of a value in the middle of a graph.
            if (kZeroTangentIntegers && !hasTangentSpace(ref)) {
                noteZero(ref, ref);
                return;
            }
            if (!hasCotangent[ref]) {
                cotangent[ref]    = node;
                hasCotangent[ref] = true;
                return;
            }
            if (cotangent[ref] == node) {
                cotangent[ref] = primitive(":op/add_d", {node, node});
                return;
            }
            cotangent[ref] = primitive(":op/add_d", {cotangent[ref], node});
        };
        const auto negate = [&](gc_node_ref_t node) {
            return primitive(":op/sub_d", {zero(), node});
        };
        const auto noteRule = [&usedRules](const std::string &name) {
            if (std::find(usedRules.begin(), usedRules.end(), name) == usedRules.end()) {
                usedRules.push_back(name);
            }
        };
        // The condition of a branch, copied: the transposed graph decides its arms with the same
        // predicate the forward graph did, at the same values -- which is why the parameters of `f`
        // are parameters of the transposed function too.
        bool complete = true;
        std::function<gc_node_ref_t(gc_node_ref_t)> forward;
        forward = [&](gc_node_ref_t ref) -> gc_node_ref_t {
            if (ref >= nodeCount) {
                return rt::kInvalidNodeRef;
            }
            if (forwardOf[ref] != rt::kInvalidNodeRef) {
                return forwardOf[ref];
            }
            const camel::runtime::GCNode *node = function->node(ref);
            if (node == nullptr) {
                return rt::kInvalidNodeRef;
            }
            if (node->kind == rt::GCNodeKind::Data) {
                forwardOf[ref] = constantValue(ref);
                return forwardOf[ref];
            }
            if (node->kind == rt::GCNodeKind::Port) {
                const auto &ports = function->normPorts();
                for (size_t i = 0; i < ports.size(); ++i) {
                    if (ports[i] != ref) {
                        continue;
                    }
                    if (!bundlesResiduals) {
                        // A parameter of a plain function stands as itself, and its value is the
                        // port the caller fills.
                        const size_t at = plainParameters && i < paramPorts.size() ? i : 0;
                        forwardOf[ref] = paramPorts[at];
                        return forwardOf[ref];
                    }
                    if (i == 0) {
                        // The residuals themselves: a value the backward half is handed.
                        forwardOf[ref] = paramPorts.front();
                        return forwardOf[ref];
                    }
                    // A tangent's own value is the (i-1)-th residual: that is the whole point of
                    // the residuals -- the value the pullback needs, carried to where it is needed.
                    const auto *bundle = static_cast<const camel::core::type::TupleType *>(
                        function->funcType()->normTypeAt(0));
                    const gc_node_ref_t part = draft.addAccsNode(
                        bundle->typeAt(i - 1), static_cast<uint32_t>(i - 1));
                    draft.setNormInputs(part, std::span<const gc_node_ref_t>(&paramPorts.front(), 1));
                    forwardOf[ref] = part;
                    return forwardOf[ref];
                }
                complete = false;
                return rt::kInvalidNodeRef;
            }
            if (node->kind == rt::GCNodeKind::Accs) {
                const auto *accs = function->nodeBodyAs<rt::GCAccsBody>(ref);
                const auto norm  = function->normInputsOf(ref);
                if (accs == nullptr || norm.empty()) {
                    complete = false;
                    return rt::kInvalidNodeRef;
                }
                const gc_node_ref_t tuple = forward(norm.front());
                if (tuple == rt::kInvalidNodeRef) {
                    return rt::kInvalidNodeRef;
                }
                const gc_node_ref_t part = draft.addAccsNode(node->dataType(), accs->value);
                draft.setNormInputs(part, std::span<const gc_node_ref_t>(&tuple, 1));
                forwardOf[ref] = part;
                return part;
            }
            if (node->kind == rt::GCNodeKind::Fill) {
                const auto *fill = function->nodeBodyAs<rt::GCFillBody>(ref);
                // The slots of a Fill live after its header, in the payload: the payload is what
                // has to be written, not the header alone, or the copy has no components.
                const std::vector<std::byte> fillPayload =
                    rt::makeFillPayload(fill->fillKind, std::vector<size_t>(fill->slots().begin(), fill->slots().end()));
                // A Fill is a value, so it needs a slot of its own: without one the loop reads its
                // bounds from whatever slot 0 happens to hold.
                const gc_node_ref_t made = draft.addFillNode(
                    node->dataType(), std::span<const std::byte>(fillPayload),
                    draft.allocateRuntimeSlot(node->dataType()));
                // A Fill's Norm input is the static template of the pair it builds: without it the
                // node has no components, and a loop reading its bounds finds none.
                std::vector<gc_node_ref_t> fillNorm;
                for (gc_node_ref_t input : function->normInputsOf(ref)) {
                    const gc_node_ref_t copied = forward(input);
                    if (copied == rt::kInvalidNodeRef) {
                        return rt::kInvalidNodeRef;
                    }
                    fillNorm.push_back(copied);
                }
                if (!fillNorm.empty()) {
                    draft.setNormInputs(made, std::span<const gc_node_ref_t>(fillNorm));
                }
                std::vector<gc_node_ref_t> with;
                for (gc_node_ref_t input : function->withInputsOf(ref)) {
                    const gc_node_ref_t copied = forward(input);
                    if (copied == rt::kInvalidNodeRef) {
                        return rt::kInvalidNodeRef;
                    }
                    with.push_back(copied);
                }
                if (!with.empty()) {
                    draft.setWithInputs(made, std::span<const gc_node_ref_t>(with));
                }
                forwardOf[ref] = made;
                return made;
            }
            if (node->kind == rt::GCNodeKind::Func) {
                // A call the cotangent path also needs as a *value* -- a tangent feeds the next
                // tangent, so the second rule needs the first rule's number -- is computed forward
                // as it stands: the same callee graph, its arguments computed forward. It is
                // transposed as well, where its own cotangent arrives; this is only the number.
                const GraphId calleeId = function->calleeIdOf(ref);
                if (!rt::isValid(calleeId)) {
                    complete = false;
                    return rt::kInvalidNodeRef;
                }
                std::vector<gc_node_ref_t> copied;
                for (gc_node_ref_t input : function->normInputsOf(ref)) {
                    const gc_node_ref_t argument = forward(input);
                    if (argument == rt::kInvalidNodeRef) {
                        return rt::kInvalidNodeRef;
                    }
                    copied.push_back(argument);
                }
                const gc_node_ref_t made = draft.addFuncNode(calleeId, node->dataType());
                if (!copied.empty()) {
                    draft.setNormInputs(made, std::span<const gc_node_ref_t>(copied));
                }
                draft.addDependency(calleeId);
                forwardOf[ref] = made;
                return made;
            }
            if (node->kind != rt::GCNodeKind::Oper) {
                complete = false;
                return rt::kInvalidNodeRef;
            }
            const auto *body = function->nodeBodyAs<camel::runtime::GCOperBody>(ref);
            std::vector<gc_node_ref_t> inputs;
            for (gc_node_ref_t input : function->normInputsOf(ref)) {
                const gc_node_ref_t copied = forward(input);
                if (copied == rt::kInvalidNodeRef) {
                    return rt::kInvalidNodeRef;
                }
                inputs.push_back(copied);
            }
            const gc_node_ref_t made = draft.addOperNode(node->dataType(), nullptr, body->uri());
            if (!inputs.empty()) {
                draft.setNormInputs(made, std::span<const gc_node_ref_t>(inputs));
            }
            forwardOf[ref] = made;
            return made;
        };

        // The value a rule needs beside the cotangent: a constant read straight, anything else
        // through the residuals -- that is what the residuals were handed over for.
        knownValue = [&](gc_node_ref_t ref) -> gc_node_ref_t {
            if (ref >= nodeCount) {
                return rt::kInvalidNodeRef;
            }
            if (isConstant(ref)) {
                return constantValue(ref);
            }
            return forward(ref);
        };
        // The exit's cotangent is the caller's: `transpose(f)(x, 1.0)` asks what `1.0` pulls back
        // to. It is seeded before the walk, because the walk starts there and goes backwards.
        const gc_node_ref_t oldExit = function->exitNodeRef();
        if (oldExit >= nodeCount) {
            os << "TRANSPOSE function=" << function->name() << " skip=exit\n";
            return false;
        }
        addCotangent(oldExit, cotangentIn);

        // One arm of a branch, or one round of a loop body: the region, the values inside it whose
        // cotangents are already known, and the ports where the chain stops -- the parameter for an
        // arm, the loop's own state ports for a round. Returns the cotangent each stop received.
        // A single seed and a single stop is the branch's case; a loop body that carries several
        // states hands out one cotangent per state and collects one per state, so the shape has to
        // be a list on both ends.
        bool armsOk = true;
        struct ArmSeed {
            gc_node_ref_t at        = rt::kInvalidNodeRef;
            gc_node_ref_t cotangent = rt::kInvalidNodeRef;
        };
        const auto runSeeded = [&](uint32_t oldRegion, const std::vector<ArmSeed> &seeds,
                                   const std::vector<gc_node_ref_t> &stops)
            -> std::vector<gc_node_ref_t> {
            std::vector<gc_node_ref_t> local(nodeCount, rt::kInvalidNodeRef);
            std::vector<bool> hasLocal(nodeCount, false);
            std::vector<gc_node_ref_t> portCotangents(stops.size(), rt::kInvalidNodeRef);
            const auto addLocal = [&](gc_node_ref_t ref, gc_node_ref_t node) {
                if (!hasLocal[ref]) {
                    local[ref]    = node;
                    hasLocal[ref] = true;
                    return;
                }
                local[ref] = primitive(":op/add_d", {local[ref], node});
            };
            // The arm's chain ends at the parameter, which lives outside the arm's region: the
            // cotangent of that parameter for this arm is what the chain hands it. Anything else
            // from outside the region is a residual this step does not have.
            const auto deliver = [&](gc_node_ref_t target, gc_node_ref_t node, gc_node_ref_t from) {
                // A value the round read that cannot hold a cotangent contributes zero: the round's
                // answer does not depend on it in a differentiable way, so there is nothing to carry
                // out of the loop and no accumulator to grow. Dropped here rather than at the escape
                // collection, which would otherwise build a float sum for an integer.
                if (kZeroTangentIntegers && !hasTangentSpace(target)) {
                    noteZero(target, from);
                    return;
                }
                if (function->nodeRegion(target) == oldRegion) {
                    addLocal(target, node);
                    return;
                }
                for (size_t i = 0; i < stops.size(); ++i) {
                    if (stops[i] != target) {
                        continue;
                    }
                    portCotangents[i] = portCotangents[i] == rt::kInvalidNodeRef
                                            ? node
                                            : primitive(":op/add_d", {portCotangents[i], node});
                    return;
                }
                if (allowEscapes) {
                    // The round read this value from outside: its cotangent leaves the loop
                    // through the capture channel, accumulated over the rounds.
                    auto &slot = escapes[target];
                    if (slot.cotangent == rt::kInvalidNodeRef) {
                        slot = Escape{node, from};
                    } else {
                        slot.cotangent = primitive(":op/add_d", {slot.cotangent, node});
                    }
                    return;
                }
                armsOk = false;
            };
            for (const ArmSeed &seed : seeds) {
                if (seed.at != rt::kInvalidNodeRef) {
                    addLocal(seed.at, seed.cotangent);
                }
            }
            for (gc_node_ref_t ref = static_cast<gc_node_ref_t>(nodeCount); ref-- > 0;) {
                if (function->nodeRegion(ref) != oldRegion || !hasLocal[ref]) {
                    continue;
                }
                const camel::runtime::GCNode *node = function->node(ref);
                if (node == nullptr) {
                    continue;
                }
                if (node->kind == rt::GCNodeKind::Port) {
                    for (size_t i = 0; i < stops.size(); ++i) {
                        if (stops[i] == ref) {
                            portCotangents[i] = local[ref];
                            break;
                        }
                    }
                    continue;
                }
                if (node->kind == rt::GCNodeKind::Cast || node->kind == rt::GCNodeKind::Copy) {
                    // A conversion passes a cotangent along when both sides can hold one. A cast
                    // *to* an integer (`int(3x)`) ends the chain by its own type: the value it
                    // produces is a step function, and the cotangent of a step function's output is
                    // zero with respect to its input. A cast *from* one (`float(n)`) hands its
                    // cotangent to the integer, and the delivery above drops it.
                    const auto castInputs = function->normInputsOf(ref);
                    if (kZeroTangentIntegers && !hasTangentSpace(ref)) {
                        noteZero(ref, ref);
                        continue;
                    }
                    if (!castInputs.empty()) {
                        deliver(castInputs.front(), local[ref], ref);
                    }
                    continue;
                }
                if (node->kind != rt::GCNodeKind::Oper) {
                    armsOk = false;
                    return {};
                }
                // The same rule as the caller's walk: a cotangent with nowhere to go stops at the
                // type. Inside a loop body the shape it stops is `s + float(n)` -- `float(n)` has one
                // operand and it is an integer, so the round's cotangent does not reach it, and the
                // loop needs no accumulator for it.
                if (kZeroTangentIntegers) {
                    if (!hasTangentSpace(ref)) {
                        noteZero(ref, ref);
                        continue;
                    }
                    bool anyReceiver = false;
                    for (const gc_node_ref_t operand : function->normInputsOf(ref)) {
                        if (!isConstant(operand) && hasTangentSpace(operand)) {
                            anyReceiver = true;
                            break;
                        }
                    }
                    if (!anyReceiver) {
                        for (const gc_node_ref_t operand : function->normInputsOf(ref)) {
                            if (!isConstant(operand)) {
                                noteZero(operand, ref);
                            }
                        }
                        continue;
                    }
                }
                const auto inputs = function->normInputsOf(ref);
                const auto *body = function->nodeBodyAs<camel::runtime::GCOperBody>(ref);
                const auto rule  = camel::core::transposeRuleOf(std::string(body->uri()));
                if (!rule.has_value() || rule->status != camel::parse::ast::RuleLookup::Found) {
                    armsOk = false;
                    return {};
                }
                noteRule(rule->rule);
                if (rule->rule == "add_t") {
                    for (size_t i = 0; i < inputs.size(); ++i) {
                        if (!isConstant(inputs[i])) {
                            deliver(inputs[i], local[ref], ref);
                        }
                    }
                    continue;
                }
                if (rule->rule == "sub_t" || rule->rule == "neg_t") {
                    bool first = true;
                    for (size_t i = 0; i < inputs.size(); ++i) {
                        if (isConstant(inputs[i])) {
                            continue;
                        }
                        deliver(inputs[i], first ? local[ref] : negate(local[ref]), ref);
                        first = false;
                    }
                    continue;
                }
                if (rule->rule == "mul_t" || rule->rule == "div_t") {
                    for (size_t i = 0; i < inputs.size(); ++i) {
                        if (isConstant(inputs[i])) {
                            continue;
                        }
                        const size_t other = inputs.size() == 2 ? 1 - i : 0;
                        const gc_node_ref_t c = knownValue(inputs[other]);
                        if (c == rt::kInvalidNodeRef) {
                            armsOk = false;
                            return {};
                        }
                        if (rule->rule == "mul_t") {
                            deliver(inputs[i], primitive(":op/mul_d", {local[ref], c}), ref);
                        } else if (i == 0) {
                            deliver(inputs[i], primitive(":op/div_d", {local[ref], c}), ref);
                        } else {
                            const gc_node_ref_t a = constantValue(inputs[0]);
                            const gc_node_ref_t top = primitive(":op/mul_d", {local[ref], a});
                            const gc_node_ref_t sq  = primitive(":op/mul_d", {c, c});
                            deliver(inputs[i], negate(primitive(":op/div_d", {top, sq})), ref);
                        }
                    }
                    continue;
                }
                armsOk = false;
                return {};
            }
            return portCotangents;
        };
        // A single seed and a single stop: the branch's case, which is what every arm of a branch
        // is. A loop body is the same walk with one seed and one stop per state it carries.
        const auto runArm = [&](uint32_t oldRegion, gc_node_ref_t armResult,
                                gc_node_ref_t armCotangent,
                                gc_node_ref_t stop) -> gc_node_ref_t {
            const std::vector<gc_node_ref_t> back =
                runSeeded(oldRegion, {ArmSeed{armResult, armCotangent}}, {stop});
            return back.empty() ? rt::kInvalidNodeRef : back.front();
        };

        // The walk runs the graph backwards: node ids are creation order, so a node only reads ids
        // below its own, and the body's nodes are walked in reverse. A branch's arms are regions,
        // not ids, so they are walked by `runArm` when the branch is reached.
        for (gc_node_ref_t ref = static_cast<gc_node_ref_t>(nodeCount); ref-- > 0;) {
            const camel::runtime::GCNode *node = function->node(ref);
            if (node == nullptr || !hasCotangent[ref]) {
                continue;
            }
            if (function->nodeRegion(ref) != 0) {
                // An arm's nodes are the arm's business; walking them here would run both arms.
                continue;
            }
            switch (node->kind) {
            case rt::GCNodeKind::Port:
                if (!bundlesResiduals && !plainParameters && !function->normPorts().empty() &&
                    function->normPorts().front() == ref) {
                    result = cotangent[ref];
                }
                break;
            case rt::GCNodeKind::Data:
                break;
            case rt::GCNodeKind::Accs: {
                // Reading one component of a tuple. The cotangent of the tuple is a value of the
                // same shape: this slot is what arrived here, and the other slots are zero. Filling
                // them is the whole point -- this case used to drop the cotangent, on the reading
                // that a component of the residuals is a known value, and that reading is why a
                // tuple had no cotangent at all and every function returning one refused.
                const auto norm  = function->normInputsOf(ref);
                const auto *body = function->nodeBodyAs<rt::GCAccsBody>(ref);
                if (!kTupleCotangents || body == nullptr || norm.empty() ||
                    function->nodeRegion(norm.front()) != 0) {
                    break;
                }
                const gc_node_ref_t holder = norm.front();
                // A tuple inside a tuple: the read this node takes a component of is itself read a
                // component at a time, so its cotangent is a tuple, not a number, and it belongs to
                // the value the slot holds rather than to a slot of the holder's own cotangent. A
                // tuple put where a component cotangent belongs is not a cotangent at all, and the
                // walk cannot see it: it answered a silent zero. So the sub-tuple's slots are handed
                // over to the component the slot names -- `t.0.1` gives the second slot of `t.0`'s
                // cotangent to `t.0`'s own value, which is a tuple written out in the body.
                const auto nested = tupleCotangents.find(ref);
                if (nested != tupleCotangents.end()) {
                    const auto *held  = function->node(holder);
                    const auto parts2 = function->withInputsOf(holder);
                    if (held == nullptr || held->kind != rt::GCNodeKind::Fill ||
                        body->value >= parts2.size()) {
                        os << "TRANSPOSE function=" << function->name()
                           << " skip=component node=" << ref << " holder=" << holder << '\n';
                        return false;
                    }
                    std::vector<gc_node_ref_t> &into = tupleCotangents[parts2[body->value]];
                    for (size_t j = 0; j < nested->second.size(); ++j) {
                        const gc_node_ref_t part = nested->second[j];
                        if (part == rt::kInvalidNodeRef) {
                            continue;
                        }
                        if (into.size() <= j) {
                            into.resize(j + 1, rt::kInvalidNodeRef);
                        }
                        into[j] = into[j] == rt::kInvalidNodeRef
                                      ? part
                                      : primitive(":op/add_d", {into[j], part});
                    }
                    hasCotangent[parts2[body->value]] = true;
                    break;
                }
                std::vector<gc_node_ref_t> &slots = tupleCotangents[holder];
                const size_t count                = tupleSlots(holder);
                if (slots.size() < count) {
                    slots.resize(count, rt::kInvalidNodeRef);
                }
                if (body->value >= slots.size()) {
                    break;
                }
                gc_node_ref_t &slot = slots[body->value];
                slot = slot == rt::kInvalidNodeRef
                           ? cotangent[ref]
                           : primitive(":op/add_d", {slot, cotangent[ref]});
                hasCotangent[holder] = true;
                break;
            }
            case rt::GCNodeKind::Fill: {
                // The value that holds a tuple hands each slot to the component it belongs to. The
                // slots nobody read are zeros, not holes: a component that is not asked for pulls
                // back nothing, and saying so with a zero is the same statement a parameter no
                // cotangent reached makes below.
                const auto found = tupleCotangents.find(ref);
                if (!kTupleCotangents || found == tupleCotangents.end()) {
                    break;
                }
                const auto parts = function->withInputsOf(ref);
                for (size_t i = 0; i < parts.size(); ++i) {
                    if (isConstant(parts[i])) {
                        continue;
                    }
                    const gc_node_ref_t part =
                        i < found->second.size() ? found->second[i] : rt::kInvalidNodeRef;
                    addCotangent(parts[i], part == rt::kInvalidNodeRef ? zero() : part);
                }
                break;
            }
            case rt::GCNodeKind::Cast:
            case rt::GCNodeKind::Copy: {
                const auto norm = function->normInputsOf(ref);
                if (!norm.empty()) {
                    addCotangent(norm.front(), cotangent[ref]);
                }
                break;
            }
            case rt::GCNodeKind::Gate: {
                const auto norm = function->normInputsOf(ref);
                if (!norm.empty()) {
                    addCotangent(norm.back(), cotangent[ref]);
                }
                break;
            }
            case rt::GCNodeKind::Join: {
                // The cotangent of a join is the cotangent of the chain downstream of it; which arm
                // produced the value decides which arm's chain receives it. Both arms are built,
                // each in its own region, and the join picks one -- so the two arms' cotangents are
                // joined, not added: only one of them ran.
                const gc_node_ref_t ctJoin             = cotangent[ref];
                const auto arms                        = function->withInputsOf(ref);
                const auto normBranch                  = function->normInputsOf(ref);
                if (arms.size() < 2 || normBranch.empty()) {
                    os << "TRANSPOSE function=" << function->name() << " skip=join arms="
                       << arms.size() << '\n';
                    return false;
                }
                const gc_node_ref_t join = draft.addJoinNode(
                    Type::Float64(), rt::kInvalidNodeRef, static_cast<rt::gc_cnt_t>(arms.size()));
                const gc_node_ref_t brch = draft.addBrchNode(
                    Type::Int64(), join, static_cast<rt::gc_cnt_t>(arms.size()));
                // The join's norm input is the branch; the branch's own norm input is the
                // condition this arm was chosen by.
                const auto conditionInputs = function->normInputsOf(normBranch.front());
                if (conditionInputs.empty()) {
                    os << "TRANSPOSE function=" << function->name() << " skip=condition\n";
                    return false;
                }
                const gc_node_ref_t condition = forward(conditionInputs.front());
                if (!complete || condition == rt::kInvalidNodeRef) {
                    os << "TRANSPOSE function=" << function->name() << " skip=condition\n";
                    return false;
                }
                // A condition is a choice, not a magnitude: the branch that ran is decided by it, and
                // the transposed graph copies it forward rather than differentiating it. So the
                // cotangent never reaches it, and the reading would say nothing about a value that is
                // in the middle of the graph unless it is said here -- a bool contributes zero because
                // it has no tangent space, and that is a fact about the derivative, not an omission.
                if (kZeroTangentIntegers && !hasTangentSpace(conditionInputs.front())) {
                    noteZero(conditionInputs.front(), normBranch.front());
                }
                draft.setNormInputs(brch, std::span<const gc_node_ref_t>(&condition, 1));
                std::vector<gc_node_ref_t> armCotangents;
                for (size_t k = 0; k < arms.size(); ++k) {
                    const uint32_t oldRegion = function->nodeRegion(arms[k]);
                    const uint32_t newRegion =
                        draft.addRegion(rt::GCRegionKind::Arm, 0, brch, static_cast<uint32_t>(k));
                    draft.setInsertRegion(newRegion);
                    const gc_node_ref_t armCotangent =
                        runArm(oldRegion, arms[k], ctJoin, function->normPorts().front());
                    if (!armsOk || armCotangent == rt::kInvalidNodeRef) {
                        os << "TRANSPOSE function=" << function->name() << " skip=arm arm=" << k
                           << '\n';
                        return false;
                    }
                    draft.setRegionResult(newRegion, armCotangent);
                    draft.setInsertRegion(0);
                    armCotangents.push_back(armCotangent);
                    armRegionsBuilt.push_back(newRegion);
                }
                // The join names the branch and the branch names the join: the join was made first
                // and its payload is written now that the branch and the arms exist.
                camel::runtime::GCJoinBody joinBody{};
                joinBody.brch     = brch;
                joinBody.armCount = static_cast<rt::gc_cnt_t>(arms.size());
                std::vector<std::byte> bytes(sizeof(joinBody));
                std::memcpy(bytes.data(), &joinBody, sizeof(joinBody));
                const camel::runtime::DraftNodeHeader *header = draft.header(join);
                camel::runtime::DraftNodeInit init;
                init.dataIndex    = header->dataIndex;
                init.dataType     = header->dataType;
                init.kind         = header->kind;
                init.runtimeFlags = header->runtimeFlags;
                init.origin       = header->origin;
                init.permissions  = header->permissions;
                init.attrs        = header->attrs;
                init.region       = header->region;
                init.name         = header->name;
                init.payload      = std::span<const std::byte>(bytes);
                const gc_node_ref_t joinBranch = brch;
                init.normInputs   = std::span<const gc_node_ref_t>(&joinBranch, 1);
                init.withInputs   = armCotangents;
                draft.rewriteNode(join, init);
                result = join;
                break;
            }
            case rt::GCNodeKind::Oper: {
                // Where this rule's cotangent could go. A value whose type has no tangent space
                // cannot receive one, and an output with none has no cotangent to hand out -- so a
                // conversion whose only operand is an integer (`:op/ltod`, `float(n)`) stops here.
                // A rule for such an operator could only be a derivative invented for a value that
                // has none, so the cotangent is dropped and the value that stopped it is named, the
                // way an unread parameter answers zero instead of a refusal.
                if (kZeroTangentIntegers) {
                    if (!hasTangentSpace(ref)) {
                        noteZero(ref, ref);
                        break;
                    }
                    bool anyReceiver = false;
                    for (const gc_node_ref_t operand : function->normInputsOf(ref)) {
                        if (!isConstant(operand) && hasTangentSpace(operand)) {
                            anyReceiver = true;
                            break;
                        }
                    }
                    if (!anyReceiver) {
                        for (const gc_node_ref_t operand : function->normInputsOf(ref)) {
                            if (!isConstant(operand)) {
                                noteZero(operand, ref);
                            }
                        }
                        break;
                    }
                }
                const auto *body           = function->nodeBodyAs<camel::runtime::GCOperBody>(ref);
                const std::string_view uri = body->uri();
                const auto rule            = camel::core::transposeRuleOf(std::string(uri));
                if (!rule.has_value() || rule->status != camel::parse::ast::RuleLookup::Found) {
                    const char *reason =
                        !rule.has_value() ? "unbound"
                        : rule->status == camel::parse::ast::RuleLookup::NoSuchFamily
                            ? "no-such-family"
                            : "no-rule";
                    os << "TRANSPOSE function=" << function->name() << " skip=rule uri=" << uri
                       << " reason=" << reason << '\n';
                    return false;
                }
                const auto inputs = function->normInputsOf(ref);
                if (inputs.size() != 2 && inputs.size() != 1) {
                    os << "TRANSPOSE function=" << function->name() << " skip=arity uri=" << uri
                       << " operands=" << inputs.size() << '\n';
                    return false;
                }
                const gc_node_ref_t ct = cotangent[ref];
                noteRule(rule->rule);
                const std::string &law = rule->rule;
                if (law == "add_t") {
                    for (size_t i = 0; i < inputs.size(); ++i) {
                        if (!isConstant(inputs[i])) {
                            addCotangent(inputs[i], ct);
                        }
                    }
                } else if (law == "sub_t" || law == "neg_t") {
                    bool first = true;
                    for (size_t i = 0; i < inputs.size(); ++i) {
                        if (isConstant(inputs[i])) {
                            continue;
                        }
                        addCotangent(inputs[i], first ? ct : negate(ct));
                        first = false;
                    }
                } else if (law == "mul_t" || law == "div_t") {
                    for (size_t i = 0; i < inputs.size(); ++i) {
                        if (isConstant(inputs[i])) {
                            continue;
                        }
                        const size_t other = inputs.size() == 2 ? 1 - i : 0;
                        const gc_node_ref_t c = knownValue(inputs[other]);
                        if (c == rt::kInvalidNodeRef) {
                            os << "TRANSPOSE function=" << function->name()
                               << " skip=residual uri=" << uri << " operand=" << inputs[other]
                               << '\n';
                            return false;
                        }
                        if (law == "mul_t") {
                            addCotangent(inputs[i], primitive(":op/mul_d", {ct, c}));
                            continue;
                        }
                        if (i == 0) {
                            addCotangent(inputs[i], primitive(":op/div_d", {ct, c}));
                        } else {
                            const gc_node_ref_t a   = constantValue(inputs[0]);
                            const gc_node_ref_t top = primitive(":op/mul_d", {ct, a});
                            const gc_node_ref_t sq  = primitive(":op/mul_d", {c, c});
                            addCotangent(inputs[i], negate(primitive(":op/div_d", {top, sq})));
                        }
                    }
                } else {
                    os << "TRANSPOSE function=" << function->name() << " skip=law rule=" << law
                       << " uri=" << uri << '\n';
                    return false;
                }
                break;
            }
            case rt::GCNodeKind::Loop: {
                // A loop is rounds of a region, and the state it carries is the port the loop
                // hands its own body -- so the k-th round's value has no name in the graph, and
                // the round count is a value (the loop's first norm input), not a constant. The
                // transpose therefore runs a loop of the same rounds: what the reverse needs is
                // the same chain, applied from the far end. That is enough when the body's rules
                // only use constants -- the product of the round's multipliers does not depend on
                // the order -- and it is not enough when a rule needs the round's own state, which
                // is refused by name rather than answered with a cotangent from nowhere.
                const gc_node_ref_t ctLoop = cotangent[ref];
                // The origin a refusal names is the region's: it is where the round was written.
                const uint32_t regionOrigin = static_cast<uint32_t>(function->region(function->nodeRegion(ref))->origin);
                const gc_node_ref_t bodyRegionOld = [&]() -> uint32_t {
                    for (uint32_t r = 1; r < function->regionCount(); ++r) {
                        const rt::GCRegion *part = function->region(r);
                        if (part != nullptr && part->owner == ref &&
                            part->kind == rt::GCRegionKind::LoopBody) {
                            return r;
                        }
                    }
                    return 0;
                }();
                const auto regionPorts = bodyRegionOld != 0
                    ? function->regionPorts(bodyRegionOld)
                    : std::span<const gc_node_ref_t>{};
                if (bodyRegionOld == 0 || regionPorts.empty()) {
                    os << "TRANSPOSE function=" << function->name() << " skip=state node=" << ref
                       << " origin=" << regionOrigin << '\n';
                    return false;
                }
                const gc_node_ref_t oldBodyResult = function->region(bodyRegionOld)->result;
                // How many states the loop already carried: the body's ports are the element
                // followed by one per state, and the loop's value is the tuple of last states. When
                // there is one, its cotangent is a float; when there are more, it is a tuple and the
                // cotangent arrives slot by slot from the component reads.
                const size_t stateCount = regionPorts.size() - 1;
                // The state's initial values are the loop's norm inputs after the range; the
                // escapes are compared with them below, so they are read once here.
                const auto oldNorm = function->normInputsOf(ref);
                const uint32_t newRegion =
                    draft.addRegion(rt::GCRegionKind::LoopBody, 0);
                draft.setInsertRegion(newRegion);
                // The body takes the iterated element first and the states after it. The element is
                // not differentiated (it comes from the range), so it keeps its type and goes
                // unused; each state becomes one cotangent, so the region grows one port per state
                // and, later, one per escaped value.
                std::vector<gc_node_ref_t> newPorts;
                const gc_node_ref_t elementPort = draft.addPortNode(
                    function->node(regionPorts.front())->dataType(),
                    draft.allocateRuntimeSlot(function->node(regionPorts.front())->dataType()));
                newPorts.push_back(elementPort);
                std::vector<gc_node_ref_t> ctPorts;
                for (size_t i = 0; i < stateCount; ++i) {
                    ctPorts.push_back(
                        draft.addPortNode(Type::Float64(), draft.allocateRuntimeSlot(Type::Float64())));
                    newPorts.push_back(ctPorts.back());
                }
                draft.setRegionPorts(newRegion, std::span<const gc_node_ref_t>(newPorts));
                escapes.clear();
                allowEscapes = true;
                // One seed per state: the cotangent of that state's next value. For one state that
                // is the body's whole result; for several it is the component of the result that
                // the state is, which is why the components are read out here rather than the
                // tuple node being walked as one value.
                std::vector<ArmSeed> seeds;
                if (stateCount == 1) {
                    seeds.push_back(ArmSeed{oldBodyResult, ctPorts.front()});
                } else {
                    const auto results = function->withInputsOf(oldBodyResult);
                    for (size_t i = 0; i < stateCount; ++i) {
                        seeds.push_back(ArmSeed{
                            i < results.size() ? results[i] : rt::kInvalidNodeRef, ctPorts[i]});
                    }
                }
                std::vector<gc_node_ref_t> statePorts(
                    regionPorts.begin() + 1, regionPorts.end());
                const std::vector<gc_node_ref_t> back =
                    runSeeded(bodyRegionOld, seeds, statePorts);
                const gc_node_ref_t bodyCotangent =
                    back.empty() ? rt::kInvalidNodeRef : back.front();
                allowEscapes = false;
                // The escapes in target order: the accumulators, the components and the reading all
                // follow the graph rather than the map's iteration order.
                std::vector<std::pair<gc_node_ref_t, Escape>> caught(escapes.begin(), escapes.end());
                std::sort(
                    caught.begin(), caught.end(),
                    [](const auto &left, const auto &right) { return left.first < right.first; });
                // The value each state starts from: its cotangent is the state's own, and it is also a
                // value the body may have read -- an escape can land on one of them.
                std::vector<gc_node_ref_t> stateInitials;
                for (size_t i = 0; i < stateCount; ++i) {
                    const size_t at = 1 + i;
                    stateInitials.push_back(at < oldNorm.size() ? oldNorm[at] : rt::kInvalidNodeRef);
                }
                const auto isStateInitial = [&](gc_node_ref_t what) {
                    return std::find(stateInitials.begin(), stateInitials.end(), what) !=
                           stateInitials.end();
                };
                // One accumulator per escaped value, beside one cotangent per state: the loop stays one
                // loop -- not one loop per escape -- and grows a state for each value that escaped,
                // because each of them has a cotangent of its own to hand out and all of them
                // advance by the same rounds.
                const bool carries = kCarryEscapedCotangents && !caught.empty();
                // Two different readings hide behind a count. One value read by several products is
                // one sum -- `answer=summed` -- and a single escape onto a value the loop's state
                // starts from is the same thing, since that state's cotangent and the sum are
                // cotangents of one value then. Several escaped values are `answer=per-target`: one
                // cotangent each, and only the ones that are a state's own value also take that
                // state's cotangent.
                const bool answerSummed =
                    caught.size() == 1 && isStateInitial(caught.front().first);
                if (armsOk && bodyCotangent != rt::kInvalidNodeRef && !caught.empty()) {
                    // What escaped, read the same way whether or not it can be handed back: the
                    // count, and one line per escape -- which value the round only read (`target`),
                    // which of the body's products handed the cotangent out (`from`, the node the
                    // rule ran on), and the loop's own two input lists, norm and with, which are
                    // what the accumulators are added to.
                    // `skip=state` said the loop refused because its one state had nowhere to put
                    // what escaped. When the channel is built nothing is skipped, so the same line
                    // says how many escapes the loop now carries and keeps every other field: the
                    // reading survives the fix, which is what makes a refusal and an answer
                    // comparable. The `norm` and `with` lists in the ESCAPE lines stay the loop's
                    // own -- they are what the states are added to, not what the escape was.
                    if (carries) {
                        os << "TRANSPOSE function=" << function->name() << " carried="
                           << caught.size() << " node=" << ref << " origin=" << regionOrigin
                           << " captures=" << caught.size()
                           << " answer=" << (answerSummed ? "summed" : "per-target") << '\n';
                    } else {
                        // The refusal's own line, character for character: the A/B of this change
                        // reads it as it stood before the accumulators existed.
                        os << "TRANSPOSE function=" << function->name() << " skip=state node=" << ref
                           << " origin=" << regionOrigin << " captures=" << caught.size() << '\n';
                    }
                    for (const auto &entry : caught) {
                        const camel::runtime::GCNode *carrier = function->node(entry.first);
                        const camel::runtime::GCNode *source  = function->node(entry.second.from);
                        os << "ESCAPE target=" << entry.first << " from=" << entry.second.from
                           << " kindOfFrom="
                           << (source != nullptr ? static_cast<int>(source->kind) : -1)
                           << " kindOfTarget="
                           << (carrier != nullptr ? static_cast<int>(carrier->kind) : -1)
                           << " typeOfTarget="
                           << (carrier != nullptr && carrier->dataType() != nullptr
                                   ? carrier->dataType()->toString()
                                   : std::string("-"))
                           << " norm=[";
                        for (const gc_node_ref_t input : function->normInputsOf(ref)) {
                            os << input << ',';
                        }
                        os << "] with=[";
                        for (const gc_node_ref_t input : function->withInputsOf(ref)) {
                            os << input << ',';
                        }
                        os << "]\n";
                    }
                    if (!carries) {
                        return false;
                    }
                }
                if (!armsOk || bodyCotangent == rt::kInvalidNodeRef) {
                    os << "TRANSPOSE function=" << function->name() << " skip=state node=" << ref
                       << " origin=" << regionOrigin << '\n';
                    return false;
                }
                // The region hands back one cotangent per state, then one sum per escaped value. With one
                // state and no escape that is a single value, which is what the region returned
                // before there were accumulators at all; with more, it is the tuple the loop carries.
                // The port list is written again rather than kept from above, because how many
                // states there are is known only after the body ran -- the same way a branch's arm
                // grows its ports when its own walk learns what it reads.
                std::vector<gc_node_ref_t> regionParts;
                for (size_t i = 0; i < stateCount; ++i) {
                    // A state no rule reached pulls nothing back: the zero says so, rather than a
                    // hole in the tuple.
                    const gc_node_ref_t part =
                        i < back.size() ? back[i] : rt::kInvalidNodeRef;
                    regionParts.push_back(part == rt::kInvalidNodeRef ? zero() : part);
                }
                for (const auto &entry : caught) {
                    const gc_node_ref_t accPort = draft.addPortNode(
                        Type::Float64(), draft.allocateRuntimeSlot(Type::Float64()));
                    newPorts.push_back(accPort);
                    // The round adds what escaped to the sum the round before handed on. One
                    // accumulator per value, not per product: the products that call one value
                    // add into the same sum when they are delivered (the collection above).
                    regionParts.push_back(primitive(":op/add_d", {accPort, entry.second.cotangent}));
                }
                gc_node_ref_t regionResult = regionParts.front();
                Type *loopType             = Type::Float64();
                if (regionParts.size() > 1) {
                    draft.setRegionPorts(newRegion, std::span<const gc_node_ref_t>(newPorts));
                    const auto pair = tupleOf(regionParts);
                    regionResult    = pair.first;
                    loopType        = pair.second;
                }
                draft.setRegionResult(newRegion, regionResult);
                draft.setInsertRegion(0);
                const gc_node_ref_t loopNode = draft.addLoopNode(
                    rt::loopBodyOf(*function, ref), loopType);
                draft.setRegionOwner(newRegion, loopNode, function->region(bodyRegionOld)->index);
                draft.setNodeOrigin(loopNode, static_cast<uint32_t>(function->region(bodyRegionOld)->origin));
                std::vector<gc_node_ref_t> loopNorm;
                // The range, and everything before the states in the old norm list: those are
                // copied as they stand, because the reverse loop iterates over the same values.
                // What follows them is the states, and those become cotangents.
                for (size_t i = 0; i + stateCount < oldNorm.size(); ++i) {
                    const gc_node_ref_t copied = forward(oldNorm[i]);
                    if (!complete || copied == rt::kInvalidNodeRef) {
                        os << "TRANSPOSE function=" << function->name() << " skip=range node="
                           << ref << '\n';
                        return false;
                    }
                    loopNorm.push_back(copied);
                }
                // One seed per state: the cotangent the loop's value carried for that state. With
                // one state that is the whole cotangent of the loop's value; with several it is the
                // slot of the tuple that the component reads filled in.
                for (size_t i = 0; i < stateCount; ++i) {
                    if (stateCount == 1) {
                        loopNorm.push_back(ctLoop);
                        continue;
                    }
                    const auto slots   = tupleCotangents.find(ref);
                    const gc_node_ref_t slot = slots != tupleCotangents.end() && i < slots->second.size()
                                                   ? slots->second[i]
                                                   : rt::kInvalidNodeRef;
                    // A slot nobody read is zero: that state's value pulls nothing back into it.
                    loopNorm.push_back(slot == rt::kInvalidNodeRef ? zero() : slot);
                }
                for (size_t i = 0; i < caught.size(); ++i) {
                    // Every sum starts at zero: a value a round never reaches adds nothing.
                    loopNorm.push_back(zero());
                }
                draft.setNormInputs(loopNode, std::span<const gc_node_ref_t>(loopNorm));
                // The With inputs of a loop are the results of its parts and its round: the round is
                // a value of its own, and the body's state port is what the previous round left.
                const gc_node_ref_t roundValue = draft.materializeStaticValue(
                    camel::core::rtdata::toSlot<camel::core::rtdata::Int64>(0), Type::Int64(),
                    rt::kGCNodeFlagConstant);
                const std::array<gc_node_ref_t, 2> loopWith{regionResult, roundValue};
                draft.setWithInputs(loopNode, std::span<const gc_node_ref_t>(loopWith));
                if (carries || stateCount > 1) {
                    // The loop's value is one cotangent per state followed by one sum per escaped
                    // value, so each component goes where its own value's cotangent goes: a state's
                    // cotangent to the value that state starts from, a sum to the value that
                    // escaped. When those are the same value the two add, because both are
                    // cotangents of it -- that is `answer=summed`. Different values get different
                    // slots -- `answer=per-target` -- and nothing is added across values.
                    const auto component = [&](uint32_t index) {
                        const gc_node_ref_t part = draft.addAccsNode(Type::Float64(), index);
                        draft.setNormInputs(part, std::span<const gc_node_ref_t>(&loopNode, 1));
                        return part;
                    };
                    for (size_t i = 0; i < stateCount; ++i) {
                        if (stateInitials[i] != rt::kInvalidNodeRef) {
                            addCotangent(stateInitials[i], component(static_cast<uint32_t>(i)));
                        }
                    }
                    for (size_t i = 0; i < caught.size(); ++i) {
                        addCotangent(
                            caught[i].first, component(static_cast<uint32_t>(stateCount + i)));
                    }
                } else {
                    result = loopNode;
                }
                break;
            }
            case rt::GCNodeKind::Call:
            case rt::GCNodeKind::Func: {
                // A call reaches its callee one of two ways in this graph model: a CALL node whose
                // first input is the callee's value, or a FUNC node that is the call itself, with
                // the arguments as its norm inputs. Both are one fact -- a graph is called here --
                // and both are transposed the same way: the callee's transpose is a graph of its
                // own, built beside this one, once per callee (the rule `jvp` reads for a lifted
                // function, so two calls of one function call one graph). The call site passes
                // what the transposed callee takes, its own parameters as they stand -- they are
                // the values the call already has -- and then the cotangent.
                const bool isCall = node->kind == rt::GCNodeKind::Call;
                const auto inputs = function->normInputsOf(ref);
                // Where the callee arrives from. A function value written in place is the call's
                // first normal input; a value kept in a `let` is a DATA node naming its graph and
                // the call takes it as a `with` input, with only the arguments as norm inputs; a
                // FUNC node is the call itself. All three name a graph, so all three transpose the
                // same way.
                GraphId calleeId  = function->calleeIdOf(inputs.empty() ? ref : inputs.front());
                size_t argumentAt = (!inputs.empty() && rt::isValid(calleeId)) ? 1 : 0;
                // What the one entry that names a *value* answered, when it was asked. Empty means
                // `calleeIdOf` answered and no value had to be named.
                std::string_view namedReason;
                if (!rt::isValid(calleeId)) {
                    const auto captured = function->withInputsOf(ref);
                    if (!captured.empty()) {
                        calleeId   = function->calleeIdOf(captured.front());
                        argumentAt = 0;
                        if (!rt::isValid(calleeId)) {
                            // `calleeIdOf` answers for a FUNC node -- a call written by name. A
                            // function value kept in a `let` is a DATA node holding a static
                            // function, and `staticFunctionGraphOf` is the one entry that turns that
                            // value into a graph id, or says which check refused.
                            if (kNameFunctionValues) {
                                const auto named =
                                    function->staticFunctionGraphOf(captured.front());
                                calleeId    = named.graph;
                                namedReason = named.reason;
                            }
                        }
                    }
                }
                if (!rt::isValid(calleeId) && !isCall) {
                    calleeId   = function->calleeIdOf(ref);
                    argumentAt = 0;
                }
                if (inputs.size() <= argumentAt && !rt::isValid(calleeId)) {
                    os << "TRANSPOSE function=" << function->name() << " skip=kind node=" << ref
                       << " kind=" << static_cast<int>(node->kind) << '\n';
                    return false;
                }

                camel::runtime::GCGraph *callee = state.program->graph(calleeId);
                if (callee == nullptr) {
                    // The callee is the value the first input carries, and a value that is
                    // itself computed -- a call's result, a returned closure -- names no graph
                    // here. The refusal names the node that carries that value and its kind, so a
                    // reader can see what stands in the way without a dump.
                    const auto describe = [&](gc_node_ref_t value) {
                        const camel::runtime::GCNode *carrier = function->node(value);
                        return std::to_string(value) + ':' +
                               std::to_string(carrier != nullptr
                                                  ? static_cast<int>(carrier->kind)
                                                  : -1);
                    };
                    os << "TRANSPOSE function=" << function->name() << " skip=call node=" << ref
                       << " callee=none value=" << describe(inputs.front());
                    const auto captured = function->withInputsOf(ref);
                    if (!captured.empty()) {
                        os << " with=" << describe(captured.front());
                    }
                    if (!namedReason.empty()) {
                        os << " reason=" << namedReason;
                    }
                    os << '\n';
                    return false;
                }
                GraphId transposed = rt::kInvalidGraphId;
                const auto known   = state.callees.find(rt::toValue(calleeId));
                if (known == state.callees.end()) {
                    transposed = group.reserve(
                        "transpose_of_" + std::string(callee->name()),
                        "transpose_of_" + std::string(callee->name()));
                    state.callees.emplace(rt::toValue(calleeId), transposed);
                    if (!buildTransposeMember(state, callee, transposed, os)) {
                        os << "TRANSPOSE function=" << function->name() << " skip=call node=" << ref
                           << " callee=" << callee->name() << '\n';
                        return false;
                    }
                } else {
                    transposed = known->second;
                }
                // A callee of one parameter answers one cotangent; a callee of several answers one for
                // each, in the order its parameters stand. Both are one call: the parameters as they
                // stand at the call site, then the cotangent.
                const size_t calleeParams = callee->funcType()->normTypesCount();
                if (calleeParams == 0 || inputs.size() != argumentAt + calleeParams) {
                    os << "TRANSPOSE function=" << function->name() << " skip=call node=" << ref
                       << " callee=" << callee->name() << " params=" << calleeParams << '\n';
                    return false;
                }
                std::vector<gc_node_ref_t> arguments;
                for (size_t i = 0; i < calleeParams; ++i) {
                    const gc_node_ref_t argument = forward(inputs[argumentAt + i]);
                    if (argument == rt::kInvalidNodeRef) {
                        os << "TRANSPOSE function=" << function->name() << " skip=call node=" << ref
                           << " arg=" << inputs[argumentAt + i] << '\n';
                        return false;
                    }
                    arguments.push_back(argument);
                }
                FunctionType *calleeType = transposeSignature(callee->funcType());
                if (calleeType == nullptr) {
                    os << "TRANSPOSE function=" << function->name() << " skip=call node=" << ref
                       << " callee=" << callee->name() << " signature\n";
                    return false;
                }
                // The callee's transpose stands where the call stood: it takes the arguments as the
                // call has them and then the cotangent, and answers one cotangent for each argument
                // -- a number for one parameter, one tuple for several.
                Type *const answerType =
                    calleeParams == 1
                        ? Type::Float64()
                        : camel::core::type::TupleType::create(
                              std::vector<Type *>(calleeParams, Type::Float64()));
                std::vector<gc_node_ref_t> callArgs;
                // Where the callee stood at the call site, it stands in the transposed call: as the
                // first normal input when the original wrote it in place, as a `with` input when the
                // original took it as a value (`let h = double; h(x)`). A call whose callee value is
                // put back in the wrong place is a CALL node without a callee.
                gc_node_ref_t calleeValue = rt::kInvalidNodeRef;
                if (isCall) {
                    if (argumentAt == 1) {
                        // Written in place: a FUNC node, whose type is the call's value.
                        calleeValue = draft.addFuncNode(transposed, Type::Float64());
                        callArgs.push_back(calleeValue);
                    } else {
                        // Taken as a value: it has to be a function value, a static function of the
                        // transposed graph with an empty closure (nothing captured). A call whose
                        // callee *captures* cannot be transposed here -- the transposed callee would
                        // have to take the captured values as parameters of its own -- and refusing
                        // that is the next question this shape asks, not one this step answers.
                        auto *closureType = camel::core::type::TupleType::create(
                            std::vector<Type *>{});
                        auto *function = ::Function::create(
                            state.program, transposed, closureType,
                            camel::core::mm::autoSpace());
                        calleeValue = draft.materializeStaticValue(
                            toSlot<::Function *>(function), calleeType);
                    }
                }
                callArgs.insert(callArgs.end(), arguments.begin(), arguments.end());
                callArgs.push_back(cotangent[ref]);
                const gc_node_ref_t made =
                    isCall ? draft.addCallNode(answerType, rt::GCCallBody{})
                           : draft.addFuncNode(transposed, answerType);
                draft.setNormInputs(made, std::span<const gc_node_ref_t>(callArgs));
                if (calleeValue != rt::kInvalidNodeRef && argumentAt == 0) {
                    draft.setWithInputs(
                        made, std::span<const gc_node_ref_t>(&calleeValue, size_t(1)));
                }
                for (size_t i = 0; i < calleeParams; ++i) {
                    if (calleeParams == 1) {
                        addCotangent(inputs[argumentAt], made);
                        break;
                    }
                    const gc_node_ref_t part = draft.addAccsNode(Type::Float64(), static_cast<uint32_t>(i));
                    draft.setNormInputs(part, std::span<const gc_node_ref_t>(&made, 1));
                    addCotangent(inputs[argumentAt + i], part);
                }
                break;
            }
            default:
                os << "TRANSPOSE function=" << function->name() << " skip=kind node=" << ref
                   << '\n';
                return false;
            }
        }
        if (result == rt::kInvalidNodeRef && (bundlesResiduals || plainParameters)) {
            // The answer of the residual form is one cotangent per tangent, in the order the
            // tangents stand: the residuals came in as one tuple, the cotangents go out as one. The
            // answer of a plain function is one cotangent per parameter, in the order they stand.
            //
            // A parameter no cotangent reached is answered with a zero. A parameter the graph never
            // reads pulls nothing back -- the `x` and `w` of `add_tangent(x, w, dx, dw) = dx + dw`
            // are in the signature and nowhere in the body -- and refusing the whole function
            // because one of its parameters has no derivative would refuse a function that is
            // written correctly. The zero is the same statement the walk makes when it drops the
            // cotangent of a residual: that component is read, not differentiated.
            const auto &ports      = function->normPorts();
            const size_t firstPort = bundlesResiduals ? 1 : 0;
            std::vector<gc_node_ref_t> parts;
            for (size_t i = firstPort; i < ports.size(); ++i) {
                parts.push_back(hasCotangent[ports[i]] ? cotangent[ports[i]] : zero());
            }
            if (!parts.empty()) {
                std::vector<Type *> partTypes;
                std::vector<size_t> slots;
                for (size_t i = 0; i < parts.size(); ++i) {
                    partTypes.push_back(Type::Float64());
                    slots.push_back(i);
                }
                Type *const tupleType = camel::core::type::TupleType::create(partTypes);
                ::Tuple *emptyTuple    = ::Tuple::create(parts.size(), mm::autoSpace());
                const gc_node_ref_t source = draft.materializeStaticValue(
                    camel::core::rtdata::toSlot<camel::core::rtdata::Object *>(emptyTuple),
                    tupleType);
                const gc_node_ref_t tuple = draft.addFillNode(
                    tupleType, rt::makeFillPayload(rt::GCFillKind::Tuple, slots));
                draft.setNormInputs(tuple, std::span<const gc_node_ref_t>(&source, 1));
                draft.setWithInputs(tuple, std::span<const gc_node_ref_t>(parts));
                result = tuple;
            }
        }
        if (result == rt::kInvalidNodeRef) {
            os << "TRANSPOSE function=" << function->name() << " skip=output\n";
            return false;
        }
        draft.setExitNode(result);
        draft.setOutputNode(result);
        draft.setReturnNode(result, rt::GCReturnKind::Self);
    }
    std::string rules;
    for (const std::string &rule : usedRules) {
        rules += (rules.empty() ? "" : ",") + rule;
    }
    os << "TRANSPOSE function=" << function->name() << " graph=" << rt::toValue(member)
       << " type=" << signature->toString() << " nodes=" << nodeCount
       << " arms=" << armRegionsBuilt.size() << " rules=" << (rules.empty() ? "-" : rules)
       << '\n';
    return true;
}

TransposeGraph buildTransposeGraph(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    camel::runtime::GCGraph *function, std::ostream &os) {
    if (program == nullptr || function == nullptr) {
        return TransposeGraph{};
    }
    camel::runtime::GraphDraftGroup group(context, program);
    const GraphId member = group.reserve(
        "transpose_of_" + std::string(function->name()),
        "transpose_of_" + std::string(function->name()));
    TransposeState state{group, program, {}};
    if (!buildTransposeMember(state, function, member, os)) {
        return TransposeGraph{};
    }
    group.encode();
    const camel::runtime::GCGraph *made = group.encoded(member);
    return TransposeGraph{group.program(), const_cast<camel::runtime::GCGraph *>(made)};
}

} // namespace camel::transforms
