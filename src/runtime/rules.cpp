// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/runtime/rules.h"

#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"
#include "camel/execute/executor.h"
#include "camel/execute/graph_runtime_support.h"

#include <algorithm>
#include <format>
#include <set>

namespace camel::runtime {

namespace {

using camel::core::type::Type;

/// Operator arguments backed by constants.
class StaticArgs final : public ArgsView {
  public:
    StaticArgs(std::vector<slot_t> slots, std::vector<Type *> types)
        : slots_(std::move(slots)), types_(std::move(types)) {}
    size_t size() const override { return slots_.size(); }
    slot_t slot(size_t index) const override { return slots_[index]; }
    void setSlot(size_t index, slot_t value) override { slots_[index] = value; }
    camel::core::type::TypeCode code(size_t index) const override {
        return types_[index]->code();
    }
    Type *type(size_t index) const override { return types_[index]; }

  private:
    std::vector<slot_t> slots_;
    std::vector<Type *> types_;
};

/// One side of a rule, read as a Pattern.
class SideReader {
  public:
    SideReader(
        camel::core::context::Context &context, const RuleSource &source, const GCGraph &graph,
        bool right)
        : context_(context), source_(source), graph_(graph), right_(right) {
        const auto ports = graph.normPorts();
        ports_.assign(ports.begin(), ports.end());
    }

    Pattern read() {
        const gc_node_ref_t result =
            camel::execute::resolveRuntimeTailValueRef(const_cast<GCGraph *>(&graph_));
        if (result == kInvalidNodeRef) {
            fail("has no result");
        }
        Pattern pattern = term(result);
        if (!right_ && pattern.kind != Pattern::Kind::Op) {
            fail("is not an operator applied to something, so it would match every value");
        }
        return pattern;
    }

    const std::set<uint32_t> &variables() const { return variables_; }

  private:
    [[noreturn]] void fail(const std::string &what) const {
        throw RuleCompileError(
            std::format(
                "Rule '{}': the {} side {}.", source_.name, right_ ? "right" : "left", what));
    }

    bool isConstParam(uint32_t index) const {
        return index < source_.constParams.size() && source_.constParams[index] != 0;
    }

    std::optional<uint32_t> portIndex(gc_node_ref_t ref) const {
        const auto found = std::find(ports_.begin(), ports_.end(), ref);
        if (found == ports_.end()) {
            return std::nullopt;
        }
        return static_cast<uint32_t>(found - ports_.begin());
    }

    /// Only constants and `const` parameters below it (and at least one operator or constant).
    bool constantOnly(gc_node_ref_t ref) const {
        const GCNode *node = graph_.node(ref);
        switch (node->kind) {
        case GCNodeKind::Data:
            return node->dataIndex < 0;
        case GCNodeKind::Port: {
            const auto index = portIndex(ref);
            return index && isConstParam(*index);
        }
        case GCNodeKind::Oper:
            for (gc_node_ref_t in : graph_.normInputsOf(ref)) {
                if (!constantOnly(in)) {
                    return false;
                }
            }
            return graph_.withInputsOf(ref).empty() && graph_.ctrlInputsOf(ref).empty();
        default:
            return false;
        }
    }

    Pattern term(gc_node_ref_t ref) {
        const GCNode *node = graph_.node(ref);
        switch (node->kind) {
        case GCNodeKind::Port: {
            const auto index = portIndex(ref);
            if (!index) {
                fail("reads a port that is not a parameter");
            }
            variables_.insert(*index);
            // A `const` parameter is matched as a constant writing of the type it was declared
            // with (rewrite design 2 (second version) 1): the type check is the matcher's, so no
            // rule guards `isI32(c)` itself.
            return !right_ && isConstParam(*index) ? patConst(*index, node->dataType())
                                                   : patVar(*index, node->dataType());
        }
        case GCNodeKind::Data: {
            if (node->dataIndex >= 0) {
                fail("reads a value that is not a constant");
            }
            const auto index = static_cast<size_t>(-node->dataIndex);
            if (index >= graph_.staticSlots().size()) {
                fail("names a constant the graph does not have");
            }
            return patLiteral(graph_.staticSlots()[index], node->dataType());
        }
        case GCNodeKind::Oper: {
            if (!graph_.withInputsOf(ref).empty() || !graph_.ctrlInputsOf(ref).empty()) {
                fail("has an operator with a context argument or an order dependency");
            }
            const auto *body = graph_.nodeBodyAs<GCOperBody>(ref);
            std::vector<Pattern> args;
            for (gc_node_ref_t in : graph_.normInputsOf(ref)) {
                args.push_back(term(in));
            }
            if (right_ && constantOnly(ref)) {
                return computed(ref, *body, std::move(args));
            }
            return patOp(body->oper, std::move(args), node->dataType());
        }
        default:
            fail("has a term that is not an operator, a constant or a parameter");
        }
    }

    /// A term of constants: evaluated by the operator when the rule is applied.
    Pattern computed(gc_node_ref_t ref, const GCOperBody &body, std::vector<Pattern> args) {
        const auto op = context_.execMgr().find(std::string(body.uri()));
        if (!op) {
            fail(std::format("uses the operator '{}', which has no implementation", body.uri()));
        }
        std::vector<Type *> types;
        for (gc_node_ref_t in : graph_.normInputsOf(ref)) {
            types.push_back(graph_.node(in)->dataType());
        }
        Type *resultType = graph_.node(ref)->dataType();
        auto *context    = &context_;
        const auto fn    = *op;
        return patCompute(
            resultType,
            [context, fn, types, resultType](std::span<const slot_t> values) -> slot_t {
                StaticArgs with({}, {});
                StaticArgs norm(std::vector<slot_t>(values.begin(), values.end()), types);
                with.setResultType(resultType);
                norm.setResultType(resultType);
                try {
                    return (*fn)(with, norm, *context);
                } catch (const camel::core::error::RuntimeFault &) {
                    throw RuleSkipped("a constant of the rule faults");
                }
            },
            std::move(args));
    }

    camel::core::context::Context &context_;
    const RuleSource &source_;
    const GCGraph &graph_;
    bool right_;
    std::vector<gc_node_ref_t> ports_;
    std::set<uint32_t> variables_;
};

Rule compileDirection(
    camel::core::context::Context &context, const RuleSource &source, const GCGraph &pattern,
    const GCGraph &replacement, bool reversed) {
    SideReader left(context, source, pattern, false);
    SideReader right(context, source, replacement, true);
    Rule rule;
    rule.name  = reversed ? source.name + "_reversed" : source.name;
    rule.id    = source.id;
    rule.needs = source.needs;
    rule.lhs   = left.read();
    rule.rhs   = right.read();
    rule.when  = source.when;
    rule.twoWay = source.bidirectional;
    for (uint32_t var : right.variables()) {
        if (!left.variables().contains(var)) {
            throw RuleCompileError(
                std::format(
                    "Rule '{}': the right side uses the parameter {}, which the left side does "
                    "not.",
                    source.name,
                    var));
        }
    }
    return rule;
}

} // namespace

std::vector<Rule> compileRule(camel::core::context::Context &context, const RuleSource &source) {
    const GCGraph *left  = source.program != nullptr ? source.program->graph(source.lhs) : nullptr;
    const GCGraph *right = source.program != nullptr ? source.program->graph(source.rhs) : nullptr;
    if (left == nullptr || right == nullptr) {
        throw RuleCompileError(std::format("Rule '{}': its two sides are not in the program.", source.name));
    }
    if (left->normPorts().size() != right->normPorts().size()) {
        throw RuleCompileError(
            std::format("Rule '{}': the two sides do not have the same parameters.", source.name));
    }
    if (source.when) {
        const size_t own = left->normPorts().size();
        size_t consts    = 0;
        for (size_t i = 0; i < own; ++i) {
            consts += source.constParams.size() > i && source.constParams[i] != 0 ? 1 : 0;
        }
        if (source.whenNodesStart != own || source.whenArity != own + consts) {
            throw RuleCompileError(
                std::format(
                    "Rule '{}': its condition takes {} parameters, its sides {} ({} of them "
                    "`const`, which the condition reaches through `node_of`).",
                    source.name,
                    source.whenArity,
                    own,
                    consts));
        }
    }
    std::vector<Rule> rules;
    rules.push_back(compileDirection(context, source, *left, *right, false));
    if (source.bidirectional) {
        rules.push_back(compileDirection(context, source, *right, *left, true));
    }
    return rules;
}

} // namespace camel::runtime
