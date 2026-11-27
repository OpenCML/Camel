// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * What a transposition asks of a law before it builds anything: which law answers a step, the type
 * tuple that law is instantiated at, whether its ports take the operands the step really has, and
 * how the two spellings of a shape disagreement are compared.
 *
 * Nothing here reads a graph's structure: every question is asked of types and of the rule index,
 * so a caller hands in the operands' types and the node's uri and nothing else.
 */

#pragma once

#include "rule_expand.h"

#include "camel/core/context/context.h"
#include "camel/core/type/base.h"
#include "camel/runtime/graph.h"
#include "graph/graph_view.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace camel::transforms {

/// The type a law's right side takes its cotangent at, or nullptr when the law has no instance at
/// `instanceTypes` or its right side is not a function of the tangents.
camel::core::type::Type *cotangentPortOf(
    camel::core::context::Context &context, const std::string &rule,
    const std::vector<camel::core::type::Type *> &instanceTypes, size_t position);

/// The transpose law that answers an operator here, chosen by the operands' own types and by the
/// `<>` value the node carries at each of its context ports, or the refusal when the declaration
/// does not choose.
///
/// `valueOf` is what makes a law that writes a `<>` value of its own answerable at all
/// (`rule_expand.h#chooseRuleAmong`'s `portValues`): a law whose left side writes `log_softmax<0>`
/// is worth asking only of a node that carries 0 there. A caller that cannot answer passes null,
/// and then such a law is refused rather than guessed -- which is what this family did by not
/// asking the question at all: every law that spells its configuration on its own left side was
/// refused, the node's own configuration never read.
RuleChoice chooseTransposedRule(
    camel::core::context::Context &context, std::string_view uri,
    std::span<const camel::runtime::gc_node_ref_t> inputs, size_t dataOperandCount,
    const std::function<const camel::core::type::Type *(camel::runtime::gc_node_ref_t)> &typeOf,
    const camel::core::type::Type *cotangentType, const std::vector<bool> *answered,
    const std::vector<camel::core::type::Type *> *offeredOperands                      = nullptr,
    bool *lawFits                                                                      = nullptr,
    const std::function<std::optional<slot_t>(camel::runtime::gc_node_ref_t)> *valueOf = nullptr);

/// The type list as the index's reports write it: `(float[..], float)`, `()` when empty.
std::string typeTextOf(const std::vector<camel::core::type::Type *> &types);

/// The ranks of the operands a cotangent was asked for, in the law's own order: `(3, 2)`. Each rank
/// is read off the operand's type, which is a query of the view and not of a representation.
std::string operandRanksText(
    const camel::graph::GraphView *function,
    std::span<const camel::runtime::gc_node_ref_t> operands);

/// Whether a law's answer and the operand it answers for state shapes that cannot both be right,
/// and -- when they cannot -- the sentence that names both spellings.
std::optional<std::string> statedShapeDisagreement(
    const camel::core::type::Type *answer, const camel::core::type::Type *operand);

/// The type tuple a law has an instance at, from the types its ports are handed.
std::vector<camel::core::type::Type *> lawInstanceTypes(
    camel::core::context::Context &context, const std::string &ruleName,
    const std::vector<camel::core::type::Type *> &portTypes);

/// Whether the rule named `ruleName` describes a node whose ports are `operandTypes`.
bool ruleTakesOperands(
    camel::core::context::Context &context, const std::string &ruleName,
    const std::vector<camel::core::type::Type *> &operandTypes, const std::vector<bool> &answered,
    std::string &why);

} // namespace camel::transforms