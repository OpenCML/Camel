// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Why a rule made no step: the message `explain` and a failed plan read.

#include "camel/runtime/writings.h"
#include "writings_internal.h"
#include "camel/runtime/operator_instance.h"
#include "camel/runtime/reachable.h"

#include "camel/core/operator_table.h"
#include "camel/core/tags.h"
#include "camel/core/type/intern.h"
#include "camel/core/type/text.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/draft_writes.h"
#include "camel/runtime/effect_liveness.h"
#include "camel/runtime/graph_txn.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace camel::runtime {

using namespace detail;

std::string DraftWritings::textOf(gc_node_ref_t node) { return nodeText(transaction(), node); }

namespace {
std::string_view operatorText(camel::core::OperId oper) {
    return camel::core::OperatorTable::instance().uri(oper);
}
} // namespace

RuleMiss DraftWritings::whyNot(const Rule &rule, gc_node_ref_t node) {
    RuleMiss miss;
    const DraftNodeHeader *rootHeader = draft_.header(node);
    if (rootHeader == nullptr || !draft_.alive(node)) {
        miss.kind   = RuleMiss::Kind::Match;
        miss.detail = "the value is no longer in the program";
        return miss;
    }
    if (!takesPart(node)) {
        miss.kind   = RuleMiss::Kind::NotPart;
        miss.detail = std::format(
            "{} is not a value a rewrite may read: control, branch, region and anchor nodes order "
            "the computation, they do not compute a value",
            nodeText(transaction(), node));
        return miss;
    }
    const camel::core::sem::PermissionMask context = contextOf(node);
    if ((rule.needs & ~context) != 0) {
        miss.kind   = RuleMiss::Kind::Needs;
        miss.detail = std::format(
            "the rule needs the permission {}, which the value's context does not grant (it grants "
            "{}), so the law is not allowed here",
            camel::core::sem::permissionsText(rule.needs & ~context),
            camel::core::sem::permissionsText(context));
        return miss;
    }
    if (rule.lhs.kind != Pattern::Kind::Op) {
        miss.kind   = RuleMiss::Kind::Match;
        miss.detail = "the rule's left side is not an operator pattern, so it matches no node";
        return miss;
    }
    // The engine matches the operands of a node in the root's permission class (rewrite design
    // 3): a rewrite may not cross a permission boundary, so neither may this answer.
    matchPerms_ = rootHeader->permissions;

    size_t varCount                                = 0;
    std::function<void(const Pattern &)> countVars = [&](const Pattern &p) {
        if (p.kind == Pattern::Kind::Var) {
            varCount = std::max(varCount, static_cast<size_t>(p.var) + 1);
        }
        for (const Pattern &arg : p.args) {
            countVars(arg);
        }
    };
    countVars(rule.lhs);
    // What the match found so far, kept together so a failed operand order can be undone without
    // disturbing the order that succeeded.
    struct Binds {
        std::vector<gc_node_ref_t> node;
        std::vector<uint8_t> bound;
    };
    Binds binds{
        std::vector<gc_node_ref_t>(varCount, kInvalidNodeRef),
        std::vector<uint8_t>(varCount, 0)};

    std::function<
        std::optional<std::string>(const Pattern &, gc_node_ref_t, const std::string &, Binds &)>
        walk;
    walk = [&](const Pattern &p,
               gc_node_ref_t at,
               const std::string &where,
               Binds &state) -> std::optional<std::string> {
        switch (p.kind) {
        case Pattern::Kind::Compute:
            return std::format(
                "{} is a computed constant, which only a right side may write",
                where);
        case Pattern::Kind::Var: {
            if (p.constOnly && !constantOf(at)) {
                return std::format(
                    "{} is {} where the pattern writes a constant",
                    where,
                    nodeText(transaction(), at));
            }
            const DraftNodeHeader *header = draft_.header(at);
            if (header == nullptr || !acceptsType(p.type, header->dataType)) {
                return std::format(
                    "{} is {} where the pattern expects a value of type {}",
                    where,
                    nodeText(transaction(), at),
                    p.type != nullptr ? p.type->toString() : std::string("any"));
            }
            if (state.bound[p.var]) {
                if (state.node[p.var] != at) {
                    return std::format(
                        "{} is {}, but the pattern's parameter is already {} (one parameter is one "
                        "value)",
                        where,
                        nodeText(transaction(), at),
                        nodeText(transaction(), state.node[p.var]));
                }
                return std::nullopt;
            }
            state.bound[p.var] = 1;
            state.node[p.var]  = at;
            return std::nullopt;
        }
        case Pattern::Kind::Constant: {
            const auto found = constantOf(at);
            if (!found || !sameConstantValue(found->first, found->second, p.value, p.type)) {
                return std::format(
                    "{} is {} where the pattern writes a constant",
                    where,
                    nodeText(transaction(), at));
            }
            return std::nullopt;
        }
        case Pattern::Kind::Op: {
            const DraftNodeHeader *header = draft_.header(at);
            if (header == nullptr || header->kind != GCNodeKind::Oper) {
                return std::format(
                    "{} is {} where the pattern writes the operator {}",
                    where,
                    nodeText(transaction(), at),
                    operatorText(p.oper));
            }
            if (header->permissions != matchPerms_) {
                return std::format(
                    "{} is {}, in a different permission class than the value the rule matched; a "
                    "rewrite may not cross one",
                    where,
                    nodeText(transaction(), at));
            }
            if (!acceptsType(p.type, header->dataType)) {
                return std::format(
                    "{} is {} where the pattern expects a value of type {}",
                    where,
                    nodeText(transaction(), at),
                    p.type != nullptr ? p.type->toString() : std::string("any"));
            }
            // The operator is the pattern's own: a different one accepts no operand (the engine
            // compares the operator indexes, not their shapes).
            if (reinterpret_cast<const GCOperBody *>(draft_.payloadOf(at).data())->oper != p.oper) {
                return std::format(
                    "{} is {} where the pattern writes the operator {}",
                    where,
                    nodeText(transaction(), at),
                    operatorText(p.oper));
            }
            if (!takesPart(at)) {
                return std::format(
                    "{} is {}, which no rewrite may read",
                    where,
                    nodeText(transaction(), at));
            }
            const KernelArguments arguments = kernelArgumentsOf(draft_, at);
            const auto &inputs              = arguments.norm;
            if (inputs.size() != p.args.size() || !arguments.with.empty()) {
                return std::format(
                    "{} is {}: it has {} value input(s){} where the pattern writes {} operand(s)",
                    where,
                    nodeText(transaction(), at),
                    inputs.size(),
                    arguments.with.empty() ? "" : " and with-inputs",
                    p.args.size());
            }
            const auto attempt = [&](bool swapped, Binds &trial) -> std::optional<std::string> {
                for (size_t i = 0; i < p.args.size(); ++i) {
                    const size_t input    = swapped ? (i == 0 ? 1 : 0) : i;
                    const std::string sub = std::format(
                        "{}{}{} of {}",
                        where.empty() ? "" : where + ", ",
                        "operand ",
                        i + 1,
                        operatorText(p.oper));
                    if (auto failure = walk(p.args[i], inputs[input], sub, trial)) {
                        return failure;
                    }
                }
                return std::nullopt;
            };
            Binds natural = binds;
            auto failure  = attempt(false, natural);
            if (!failure) {
                binds = std::move(natural);
                return std::nullopt;
            }
            // The engine reads the written order first and the exchanged order only when that
            // failed and the operator's semantics say the exchange keeps the value (rewrite
            // design 2.2). The answer tries the same two orders, so a rule that does fire
            // commuted is not reported as a mismatch.
            if (!ordered_ && inputs.size() == 2 && declaredCommutative(p.oper, header->dataType)) {
                Binds exchanged = binds;
                if (!attempt(true, exchanged)) {
                    binds = std::move(exchanged);
                    return std::nullopt;
                }
            }
            return failure;
        }
        }
        return std::nullopt;
    };

    if (auto failure = walk(rule.lhs, node, "the value", binds)) {
        miss.kind   = RuleMiss::Kind::Match;
        miss.detail = *failure;
        return miss;
    }
    if (rule.when) {
        try {
            const Match match{*this, node, binds.node};
            if (!rule.when(match)) {
                std::string values;
                for (gc_node_ref_t var : binds.node) {
                    if (var == kInvalidNodeRef) {
                        continue;
                    }
                    if (!values.empty()) {
                        values += ", ";
                    }
                    values += nodeText(transaction(), var);
                }
                miss.kind   = RuleMiss::Kind::Condition;
                miss.detail = std::format(
                    "the left side matches ({}), but the condition is false",
                    values.empty() ? std::string("no parameter") : values);
                return miss;
            }
        } catch (const std::exception &e) {
            miss.kind   = RuleMiss::Kind::Condition;
            miss.detail = std::format(
                "the left side matches, but the condition could not be evaluated: {}",
                e.what());
            return miss;
        }
    }
    miss.kind = RuleMiss::Kind::Blocked;
    if (dropsProtected(rule, binds.node, MatchRemainder{})) {
        miss.detail = std::format(
            "the discarded computation is an external access or a write that outlives the call, "
            "which no selected guard deletes, or the selected guards reject it "
            "(protect_order={}, protect_faults={})",
            protection_.order,
            protection_.faults);
        return miss;
    }
    miss.detail =
        "the left side matches and the condition holds, but the engine made no step here: it "
        "built the same node, or the rewrite would have moved the computation across a control "
        "edge";
    return miss;
}

} // namespace camel::runtime
