// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "transpose.h"

#include "camel/core/forward.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/func.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/draft_session.h"

#include <algorithm>
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
    if (function->withTypesCount() != 0 || function->normTypesCount() != 1) {
        return nullptr;
    }
    if (function->normTypeAt(0) != Type::Float64() || function->exitType() != Type::Float64()) {
        return nullptr;
    }
    // `Transpose<F> = (ct: Tangent<ResultOf<F>>) => (..Tangent<ParamsOf<F>>)`: one cotangent in,
    // one cotangent per parameter out. One parameter today, which is the shape the walk handles.
    camel::core::type::param_vec_t params;
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

    std::vector<std::string> usedRules;
    camel::runtime::GraphDraftGroup group(context, program);
    const GraphId member = group.reserve(
        "transpose_of_" + std::string(function->name()),
        "transpose_of_" + std::string(function->name()));
    gc_node_ref_t result = rt::kInvalidNodeRef;
    {
        auto &draft = group.transaction(member).edit();
        draft.setFuncType(signature);
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
        std::vector<gc_node_ref_t> cotangent(function->nodeCount(), rt::kInvalidNodeRef);
        std::vector<bool> hasCotangent(function->nodeCount(), false);
        // A constant operand is not a cotangent carrier, but its *value* is what a rule needs
        // (`ct * c`): materialized once per constant node.
        std::vector<gc_node_ref_t> constantOf(function->nodeCount(), rt::kInvalidNodeRef);

        const auto isConstant = [function](gc_node_ref_t ref) {
            const camel::runtime::GCNode *node = function->node(ref);
            return node != nullptr && node->kind == rt::GCNodeKind::Data;
        };
        const auto constantValue = [&](gc_node_ref_t ref) -> gc_node_ref_t {
            if (constantOf[ref] != rt::kInvalidNodeRef) {
                return constantOf[ref];
            }
            const camel::runtime::GCNode *node = function->node(ref);
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
            // Two uses of one value: the transpose has two cotangents for it, and they add.
            if (cotangent[ref] == node) {
                cotangent[ref] = primitive(":op/add_d", {node, node});
                return;
            }
            cotangent[ref] = primitive(":op/add_d", {cotangent[ref], node});
        };
        const auto negate = [&](gc_node_ref_t node) { return primitive(":op/sub_d", {zero(), node}); };
        const auto noteRule = [&usedRules](const std::string &name) {
            if (std::find(usedRules.begin(), usedRules.end(), name) == usedRules.end()) {
                usedRules.push_back(name);
            }
        };

        // The exit's cotangent is the caller's: `transpose(f)(1.0)` asks what `1.0` pulls back to.
        gc_node_ref_t oldValue = function->exitNodeRef();
        if (oldValue >= function->nodeCount()) {
            os << "TRANSPOSE function=" << function->name() << " skip=exit\n";
            return TransposeGraph{};
        }
        addCotangent(oldValue, cotangentIn);

        // The walk runs the graph backwards, which is what "the data flow reversed" means for
        // straight-line code: node ids are creation order, and a node only reads ids below its own.
        for (gc_node_ref_t ref = static_cast<gc_node_ref_t>(function->nodeCount()); ref-- > 0;) {
            const camel::runtime::GCNode *node = function->node(ref);
            if (node == nullptr || !hasCotangent[ref]) {
                continue;
            }
            switch (node->kind) {
            case rt::GCNodeKind::Port:
                // The parameter's cotangent is the answer.
                if (!function->normPorts().empty() && function->normPorts().front() == ref) {
                    result = cotangent[ref];
                }
                break;
            case rt::GCNodeKind::Data:
                // A constant carries no cotangent of its own: it is the `c` of a rule, not the `x`.
                break;
            case rt::GCNodeKind::Gate: {
                // A gate only forwards, so the cotangent it collected is what its input's is.
                const auto norm = function->normInputsOf(ref);
                if (!norm.empty()) {
                    addCotangent(norm.back(), cotangent[ref]);
                }
                break;
            }
            case rt::GCNodeKind::Oper: {
                const auto *body          = function->nodeBodyAs<camel::runtime::GCOperBody>(ref);
                const std::string_view uri = body->uri();
                // Which rule transposes this primitive: asked of the index, every time, so a rule
                // taken out of the ruleset stops answering here (camel/core/forward.h).
                const auto rule = camel::core::transposeRuleOf(std::string(uri));
                if (!rule.has_value() || rule->status != camel::parse::ast::RuleLookup::Found) {
                    // The two ways of not finding one are named apart, because the repairs differ:
                    // a family the program never imported, or a rule the family does not hold.
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
                // What the rule's name means in a graph. The rule itself is written in the ruleset
                // as a closure over the primitive's operands; here those operands are *nodes* of
                // the graph being built, so the transform writes the law once per family and the
                // index decides which law -- the same shape `jvp.cpp` writes for its four laws,
                // with the name taken from the table instead of matched against the uri.
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
                            // Both operands carry data: the cotangent of one is the cotangent of
                            // the result times the *value* of the other, and that value is a
                            // residual this step does not have. `x * x` is not linear; `2 * x` is.
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
                            // The denominator's cotangent: -(ct * a) / (b * b).
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
                // A branch, a loop or a call on the path: step 2 of 4.3, not this one.
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
       << " nodes=" << function->nodeCount() << " rules=" << (rules.empty() ? "-" : rules)
       << '\n';
    return TransposeGraph{group.program(), const_cast<camel::runtime::GCGraph *>(made)};
}

} // namespace camel::transforms