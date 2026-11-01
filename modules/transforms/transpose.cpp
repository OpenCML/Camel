// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "transpose.h"

#include "camel/core/forward.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/func.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/draft_session.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <span>
#include <string>
#include <string_view>
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
        for (size_t i = 0; i < bundle->size(); ++i) {
            if (bundle->typeAt(i) != Type::Float64()) {
                return nullptr;
            }
        }
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
        std::unordered_map<gc_node_ref_t, gc_node_ref_t> escapes;
        const auto addCotangent = [&](gc_node_ref_t ref, gc_node_ref_t node) {
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

        // One arm of a branch: the region the arm owns, the value it produced in the original
        // graph, and the cotangent arriving at that value. Returns the cotangent of the parameter
        // for that arm -- which is the arm's own chain, run backwards inside the arm's region.
        bool armsOk = true;
        const auto runArm = [&](uint32_t oldRegion, gc_node_ref_t armResult,
                                gc_node_ref_t armCotangent,
                                gc_node_ref_t stop) -> gc_node_ref_t {
            std::vector<gc_node_ref_t> local(nodeCount, rt::kInvalidNodeRef);
            std::vector<bool> hasLocal(nodeCount, false);
            gc_node_ref_t portCotangent = rt::kInvalidNodeRef;
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
            const auto deliver = [&](gc_node_ref_t target, gc_node_ref_t node) {
                if (function->nodeRegion(target) == oldRegion) {
                    addLocal(target, node);
                    return;
                }
                if (target == stop) {
                    if (portCotangent == rt::kInvalidNodeRef) {
                        portCotangent = node;
                    } else {
                        portCotangent = primitive(":op/add_d", {portCotangent, node});
                    }
                    return;
                }
                if (allowEscapes) {
                    // The round read this value from outside: its cotangent leaves the loop
                    // through the capture channel, accumulated over the rounds.
                    auto &slot = escapes[target];
                    slot       = slot == rt::kInvalidNodeRef
                                     ? node
                                     : primitive(":op/add_d", {slot, node});
                    return;
                }
                armsOk = false;
            };
            addLocal(armResult, armCotangent);
            for (gc_node_ref_t ref = static_cast<gc_node_ref_t>(nodeCount); ref-- > 0;) {
                if (function->nodeRegion(ref) != oldRegion || !hasLocal[ref]) {
                    continue;
                }
                const camel::runtime::GCNode *node = function->node(ref);
                if (node == nullptr) {
                    continue;
                }
                if (node->kind == rt::GCNodeKind::Port) {
                    portCotangent = local[ref];
                    continue;
                }
                if (node->kind != rt::GCNodeKind::Oper) {
                    armsOk = false;
                    return rt::kInvalidNodeRef;
                }
                const auto inputs = function->normInputsOf(ref);
                const auto *body = function->nodeBodyAs<camel::runtime::GCOperBody>(ref);
                const auto rule  = camel::core::transposeRuleOf(std::string(body->uri()));
                if (!rule.has_value() || rule->status != camel::parse::ast::RuleLookup::Found) {
                    armsOk = false;
                    return rt::kInvalidNodeRef;
                }
                noteRule(rule->rule);
                if (rule->rule == "add_t") {
                    for (size_t i = 0; i < inputs.size(); ++i) {
                        if (!isConstant(inputs[i])) {
                            deliver(inputs[i], local[ref]);
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
                        deliver(inputs[i], first ? local[ref] : negate(local[ref]));
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
                            return rt::kInvalidNodeRef;
                        }
                        if (rule->rule == "mul_t") {
                            deliver(inputs[i], primitive(":op/mul_d", {local[ref], c}));
                        } else if (i == 0) {
                            deliver(inputs[i], primitive(":op/div_d", {local[ref], c}));
                        } else {
                            const gc_node_ref_t a = constantValue(inputs[0]);
                            const gc_node_ref_t top = primitive(":op/mul_d", {local[ref], a});
                            const gc_node_ref_t sq  = primitive(":op/mul_d", {c, c});
                            deliver(inputs[i], negate(primitive(":op/div_d", {top, sq})));
                        }
                    }
                    continue;
                }
                armsOk = false;
                return rt::kInvalidNodeRef;
            }
            return portCotangent;
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
            case rt::GCNodeKind::Accs:
                // A component of the residuals is a known value: it is read, not differentiated.
                break;
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
                const uint32_t newRegion =
                    draft.addRegion(rt::GCRegionKind::LoopBody, 0);
                draft.setInsertRegion(newRegion);
                // The body takes the iterated element first and the carried state after it. The
                // element is not differentiated (it comes from the range), so it keeps its type
                // and goes unused; the carried state becomes the cotangent.
                std::vector<gc_node_ref_t> newPorts;
                const gc_node_ref_t elementPort = draft.addPortNode(
                    function->node(regionPorts.front())->dataType(),
                    draft.allocateRuntimeSlot(function->node(regionPorts.front())->dataType()));
                newPorts.push_back(elementPort);
                const gc_node_ref_t ctPort =
                    draft.addPortNode(Type::Float64(), draft.allocateRuntimeSlot(Type::Float64()));
                newPorts.push_back(ctPort);
                draft.setRegionPorts(newRegion, std::span<const gc_node_ref_t>(newPorts));
                escapes.clear();
                allowEscapes = true;
                const gc_node_ref_t bodyCotangent =
                    runArm(bodyRegionOld, oldBodyResult, ctPort, regionPorts.back());
                allowEscapes = false;
                if (armsOk && bodyCotangent != rt::kInvalidNodeRef && !escapes.empty()) {
                    // The body's cotangents have a channel now, but the loop still hands back
                    // one state: the refusal says how many are waiting, and for which values, so
                    // the step that adds the accumulators knows the count from a reading.
                    os << "TRANSPOSE function=" << function->name() << " skip=state node=" << ref
                       << " origin=" << regionOrigin << " captures=" << escapes.size() << " nodes=";
                    for (const auto &entry : escapes) {
                        const camel::runtime::GCNode *carrier = function->node(entry.first);
                        os << entry.first << '('
                           << (carrier != nullptr ? static_cast<int>(carrier->kind) : -1) << ','
                           << (carrier != nullptr && carrier->dataType() != nullptr
                                   ? carrier->dataType()->toString()
                                   : std::string("-"))
                           << "),";
                    }
                    os << '\n';
                    return false;
                }
                if (!armsOk || bodyCotangent == rt::kInvalidNodeRef) {
                    os << "TRANSPOSE function=" << function->name() << " skip=state node=" << ref
                       << " origin=" << regionOrigin << '\n';
                    return false;
                }
                draft.setRegionResult(newRegion, bodyCotangent);
                draft.setInsertRegion(0);
                const gc_node_ref_t loopNode = draft.addLoopNode(
                    rt::loopBodyOf(*function, ref), Type::Float64());
                draft.setRegionOwner(newRegion, loopNode, function->region(bodyRegionOld)->index);
                draft.setNodeOrigin(loopNode, static_cast<uint32_t>(function->region(bodyRegionOld)->origin));
                std::vector<gc_node_ref_t> loopNorm;
                const auto oldNorm = function->normInputsOf(ref);
                for (size_t i = 0; i + 1 < oldNorm.size(); ++i) {
                    const gc_node_ref_t copied = forward(oldNorm[i]);
                    if (!complete || copied == rt::kInvalidNodeRef) {
                        os << "TRANSPOSE function=" << function->name() << " skip=range node="
                           << ref << '\n';
                        return false;
                    }
                    loopNorm.push_back(copied);
                }
                loopNorm.push_back(ctLoop);
                draft.setNormInputs(loopNode, std::span<const gc_node_ref_t>(loopNorm));
                // The With inputs of a loop are the results of its parts and its round: the round is
                // a value of its own, and the body's state port is what the previous round left.
                const gc_node_ref_t roundValue = draft.materializeStaticValue(
                    camel::core::rtdata::toSlot<camel::core::rtdata::Int64>(0), Type::Int64(),
                    rt::kGCNodeFlagConstant);
                const std::array<gc_node_ref_t, 2> loopWith{bodyCotangent, roundValue};
                draft.setWithInputs(loopNode, std::span<const gc_node_ref_t>(loopWith));
                result = loopNode;
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
                if (!rt::isValid(calleeId)) {
                    const auto captured = function->withInputsOf(ref);
                    if (!captured.empty()) {
                        calleeId   = function->calleeIdOf(captured.front());
                        argumentAt = 0;
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
                if (isCall) {
                    // A call through a value takes the callee's transpose as its first input.
                    callArgs.push_back(draft.addFuncNode(transposed, Type::Float64()));
                }
                callArgs.insert(callArgs.end(), arguments.begin(), arguments.end());
                callArgs.push_back(cotangent[ref]);
                const gc_node_ref_t made =
                    isCall ? draft.addCallNode(answerType, rt::GCCallBody{})
                           : draft.addFuncNode(transposed, answerType);
                draft.setNormInputs(made, std::span<const gc_node_ref_t>(callArgs));
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
