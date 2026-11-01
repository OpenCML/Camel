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
    camel::core::type::param_vec_t params;
    for (size_t i = 0; i < function->normTypesCount(); ++i) {
        if (function->normTypeAt(i) != Type::Float64()) {
            return nullptr;
        }
        params.emplace_back(Type::Float64(), false);
    }
    params.emplace_back(Type::Float64(), false);
    return FunctionType::create(camel::core::type::param_vec_t{}, params, Type::Float64());
}

TransposeGraph buildTransposeGraph(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    camel::runtime::GCGraph *function, std::ostream &os) {
    if (program == nullptr || function == nullptr) {
        return TransposeGraph{};
    }
    FunctionType *signature = transposeSignature(function->funcType());
    if (signature == nullptr) {
        os << "TRANSPOSE function=" << function->name() << " skip=signature\n";
        return TransposeGraph{};
    }

    const size_t nodeCount         = function->nodeCount();
    const size_t paramCount        = function->funcType()->normTypesCount();
    std::vector<std::string> usedRules;
    std::vector<uint32_t> armRegionsBuilt;
    camel::runtime::GraphDraftGroup group(context, program);
    const GraphId member = group.reserve(
        "transpose_of_" + std::string(function->name()),
        "transpose_of_" + std::string(function->name()));
    gc_node_ref_t result = rt::kInvalidNodeRef;
    {
        auto &draft = group.transaction(member).edit();
        draft.setFuncType(signature);
        std::vector<gc_node_ref_t> paramPorts;
        for (size_t i = 0; i < paramCount; ++i) {
            const gc_node_ref_t port =
                draft.addPortNode(Type::Float64(), draft.allocateRuntimeSlot(Type::Float64()));
            draft.appendNormPort(port);
            paramPorts.push_back(port);
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
                for (size_t i = 0; i < ports.size() && i < paramPorts.size(); ++i) {
                    if (ports[i] == ref) {
                        forwardOf[ref] = paramPorts[i];
                        return forwardOf[ref];
                    }
                }
                complete = false;
                return rt::kInvalidNodeRef;
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

        // The exit's cotangent is the caller's: `transpose(f)(x, 1.0)` asks what `1.0` pulls back
        // to. It is seeded before the walk, because the walk starts there and goes backwards.
        const gc_node_ref_t oldExit = function->exitNodeRef();
        if (oldExit >= nodeCount) {
            os << "TRANSPOSE function=" << function->name() << " skip=exit\n";
            return TransposeGraph{};
        }
        addCotangent(oldExit, cotangentIn);

        // One arm of a branch: the region the arm owns, the value it produced in the original
        // graph, and the cotangent arriving at that value. Returns the cotangent of the parameter
        // for that arm -- which is the arm's own chain, run backwards inside the arm's region.
        bool armsOk = true;
        const auto runArm = [&](uint32_t oldRegion, gc_node_ref_t armResult,
                                gc_node_ref_t armCotangent) -> gc_node_ref_t {
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
                if (!function->normPorts().empty() && function->normPorts().front() == target) {
                    if (portCotangent == rt::kInvalidNodeRef) {
                        portCotangent = node;
                    } else {
                        portCotangent = primitive(":op/add_d", {portCotangent, node});
                    }
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
                        if (!isConstant(inputs[other])) {
                            armsOk = false;
                            return rt::kInvalidNodeRef;
                        }
                        const gc_node_ref_t c = constantValue(inputs[other]);
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
                if (!function->normPorts().empty() && function->normPorts().front() == ref) {
                    result = cotangent[ref];
                }
                break;
            case rt::GCNodeKind::Data:
                break;
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
                    return TransposeGraph{};
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
                    return TransposeGraph{};
                }
                const gc_node_ref_t condition = forward(conditionInputs.front());
                if (!complete || condition == rt::kInvalidNodeRef) {
                    os << "TRANSPOSE function=" << function->name() << " skip=condition\n";
                    return TransposeGraph{};
                }
                draft.setNormInputs(brch, std::span<const gc_node_ref_t>(&condition, 1));
                std::vector<gc_node_ref_t> armCotangents;
                for (size_t k = 0; k < arms.size(); ++k) {
                    const uint32_t oldRegion = function->nodeRegion(arms[k]);
                    const uint32_t newRegion =
                        draft.addRegion(rt::GCRegionKind::Arm, 0, brch, static_cast<uint32_t>(k));
                    draft.setInsertRegion(newRegion);
                    const gc_node_ref_t armCotangent = runArm(oldRegion, arms[k], ctJoin);
                    if (!armsOk || armCotangent == rt::kInvalidNodeRef) {
                        os << "TRANSPOSE function=" << function->name() << " skip=arm arm=" << k
                           << '\n';
                        return TransposeGraph{};
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
                    return TransposeGraph{};
                }
                const auto inputs = function->normInputsOf(ref);
                if (inputs.size() != 2 && inputs.size() != 1) {
                    os << "TRANSPOSE function=" << function->name() << " skip=arity uri=" << uri
                       << " operands=" << inputs.size() << '\n';
                    return TransposeGraph{};
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
                        if (!isConstant(inputs[other])) {
                            os << "TRANSPOSE function=" << function->name()
                               << " skip=residual uri=" << uri << '\n';
                            return TransposeGraph{};
                        }
                        const gc_node_ref_t c = constantValue(inputs[other]);
                        if (c == rt::kInvalidNodeRef) {
                            os << "TRANSPOSE function=" << function->name()
                               << " skip=constant uri=" << uri << '\n';
                            return TransposeGraph{};
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
                    return TransposeGraph{};
                }
                break;
            }
            default:
                os << "TRANSPOSE function=" << function->name() << " skip=kind node=" << ref
                   << '\n';
                return TransposeGraph{};
            }
        }
        if (result == rt::kInvalidNodeRef) {
            os << "TRANSPOSE function=" << function->name() << " skip=output\n";
            return TransposeGraph{};
        }
        draft.setExitNode(result);
        draft.setOutputNode(result);
        draft.setReturnNode(result, rt::GCReturnKind::Self);
    }
    group.encode();
    const camel::runtime::GCGraph *made = group.encoded(member);
    const auto *madeType = made != nullptr ? made->funcType() : nullptr;
    std::string rules;
    for (const std::string &rule : usedRules) {
        rules += (rules.empty() ? "" : ",") + rule;
    }
    os << "TRANSPOSE function=" << function->name() << " graph=" << rt::toValue(member)
       << " type=" << (madeType != nullptr ? madeType->toString() : std::string("-"))
       << " nodes=" << nodeCount << " arms=" << armRegionsBuilt.size()
       << " rules=" << (rules.empty() ? "-" : rules) << '\n';
    return TransposeGraph{group.program(), const_cast<camel::runtime::GCGraph *>(made)};
}

} // namespace camel::transforms