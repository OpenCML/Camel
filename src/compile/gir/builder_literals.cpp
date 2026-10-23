// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Literals take their type from their context, and what literals compute to is computed here
 * (design principles 5.1):
 *
 *   - a literal keeps the exact value it was written as until the context says which type it
 *     has (the construction tree holds it, camel/compile/gct/tree.h; the node's exact value
 *     lives beside the graph node here);
 *   - an expression of literals only (`1 + 2`, `0.1 + 0.2 == 0.3`, `2 ^ 10`) is folded to a
 *     constant, exactly: no intermediate overflow (`9223372036854775807 + 1` is the value
 *     9223372036854775808, which then does not fit `int`), no rounding before the target type
 *     is known (`let x: f32 = 1.0 / 3` rounds once, to f32);
 *   - a literal operand of an operator takes the type of the other operand (`x * 2 + 1` with
 *     `x: f32`: both literals are f32), and a literal where a value of a numeric type is
 *     expected takes the expected type (`let y: float = 1`, `f(2)` with `f(x: f32)`,
 *     `return 2` under `f32`). A value the type cannot hold is a compile-time error, never a
 *     value cut to size.
 *
 * What does NOT change: a value that already has a type never converts by itself. `n + 1.0`
 * with `n: int`, `let y: float = n`, and returning an `int` under `f32` stay errors. Only a
 * literal — a value the program wrote and whose type is not decided yet — takes a type from
 * its context.
 */

#include "builder.h"
#include "builder_helpers.h"

#include "camel/utils/log.h"

#define DEBUG_LEVEL -1

using namespace std;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::data;
using namespace camel::core::type;

namespace camel::compile::gir {

namespace {

/// The operators whose value over literals is computed here. `&&`, `||` and the branches are
/// not calls. `~` is the one bitwise operator of the language (`&` and `|` are the eager
/// logical operators, `^` is a power, and `bit_and`, `shl` and the rest are functions of the
/// `math` module): its value over an integer literal is `-x - 1` whatever the width is
/// (`~5` is `-6` in an `int` and in an `i32`), so it is exact and folds like `-`.
bool isFoldedOperator(const std::string &group) {
    static const std::unordered_set<std::string> kGroups = {
        "__add__",
        "__sub__",
        "__mul__",
        "__div__",
        "__mod__",
        "__pow__",
        "__neg__",
        "__inv__",
        "__eq__",
        "__neq__",
        "__lt__",
        "__le__",
        "__gt__",
        "__ge__"};
    return kGroups.contains(group);
}

bool isComparisonOperator(const std::string &group) {
    return group == "__eq__" || group == "__neq__" || group == "__lt__" || group == "__le__" ||
           group == "__gt__" || group == "__ge__";
}

/// The exact value of `group` over `values` (the operands, left to right); nothing when the
/// operator has no value here (integer division or a remainder by zero, a power that runs
/// away, a remainder with a float operand).
std::optional<literal::Value>
foldedValueOf(const std::string &group, const std::vector<literal::Value> &values) {
    if (values.size() == 1) {
        if (group == "__neg__") {
            return literal::Value::negate(values[0]);
        }
        if (group == "__inv__" && values[0].kind() == literal::Kind::Integer) {
            // `~x` is `-x - 1` in two's complement, at every width: exact, and independent of
            // the type the literal will take.
            std::optional<literal::Value> negated = literal::Value::negate(values[0]);
            if (!negated.has_value()) {
                return std::nullopt;
            }
            return literal::Value::sub(*negated, literal::Value::fromInt64(1));
        }
        return std::nullopt;
    }
    if (values.size() != 2) {
        return std::nullopt;
    }
    if (group == "__add__") {
        return literal::Value::add(values[0], values[1]);
    }
    if (group == "__sub__") {
        return literal::Value::sub(values[0], values[1]);
    }
    if (group == "__mul__") {
        return literal::Value::mul(values[0], values[1]);
    }
    if (group == "__div__") {
        return literal::Value::div(values[0], values[1]);
    }
    if (group == "__mod__") {
        return literal::Value::mod(values[0], values[1]);
    }
    if (group == "__pow__") {
        return literal::Value::pow(values[0], values[1]);
    }
    return std::nullopt;
}

/// The value a comparison of two exact values gives; nothing for any other group.
std::optional<bool> foldedComparison(
    const std::string &group, const literal::Value &left, const literal::Value &right) {
    // A complex number has no order: `<`, `<=`, `>` and `>=` over an imaginary value are
    // not folded (and the operator table has no overload for them either).
    if (left.hasImaginaryPart() || right.hasImaginaryPart()) {
        if (group != "__eq__" && group != "__neq__") {
            return std::nullopt;
        }
    }
    const int order = left.compare(right);
    if (group == "__eq__") {
        return order == 0;
    }
    if (group == "__neq__") {
        return order != 0;
    }
    if (group == "__lt__") {
        return order < 0;
    }
    if (group == "__le__") {
        return order <= 0;
    }
    if (group == "__gt__") {
        return order > 0;
    }
    if (group == "__ge__") {
        return order >= 0;
    }
    return std::nullopt;
}

} // namespace

const literal::Value *Builder::literalValueOfGct(const GCT::node_ptr_t &gct) {
    if (gct == nullptr || gct->type() != GCT::NodeKind::DATA) {
        return nullptr;
    }
    return GCT::Tree::of(gct)->literalValueOf(gct);
}

std::optional<literal::Value> Builder::literalValueOfExpr(const GCT::node_ptr_t &expr) {
    if (expr == nullptr) {
        return std::nullopt;
    }
    // A literal written there, exactly as written; or an expression of literals only, whose
    // exact value is what it computes to. A name — even one a `let` bound to a constant — is
    // a value of its type, not a literal.
    if (const literal::Value *written = literalValueOfGct(expr); written != nullptr) {
        return *written;
    }
    if (expr->type() == GCT::NodeKind::LINK) {
        return foldedLiteralValue(expr);
    }
    return std::nullopt;
}

// The default type of a literal of this kind: `int` for an integer, `float` for a decimal or
// exponent form (design principles 5.1, 02_lexical).
Type *Builder::defaultTypeOf(const literal::Value &value) {
    switch (value.kind()) {
    case literal::Kind::Integer:
        return Type::Int();
    case literal::Kind::Float:
        return Type::Float();
    case literal::Kind::Imaginary:
    case literal::Kind::Complex:
        // Design principles 5.1: an imaginary literal with no context is a c128.
        return Type::Complex128();
    }
    return Type::Float();
}

node_handle_t Builder::literalInType(
    const literal::Value &value, Type *type, const GCT::node_ptr_t &origin, bool report) {
    if (type == nullptr || !literal::kindEnters(value, type)) {
        return nullptr;
    }
    const auto data = value.toData(type);
    if (!data.has_value()) {
        if (!report) {
            return nullptr;
        }
        // The literal as it is written — or the whole expression it was folded from, with the
        // value it came to (design principles 5.1).
        rejectLiteralValue(value, type, origin);
    }
    node_handle_t node = currGraph_->addStaticDataNode(*data);
    rememberConstant(node, *data);
    registerNodeOrigin(context_, node, origin, "gir.data.literal");
    return node;
}

node_handle_t
Builder::makeLiteralNode(const literal::Value &value, Type *type, const GCT::node_ptr_t &origin) {
    node_handle_t node = literalInType(value, type, origin, true);
    ASSERT(node != nullptr, "A literal of a numeric type is a node.");
    return node;
}

std::optional<literal::Value> Builder::foldedLiteralValue(const GCT::node_ptr_t &gct) {
    if (gct == nullptr) {
        return std::nullopt;
    }
    // A literal written in the program, exactly as written.
    if (const literal::Value *written = literalValueOfGct(gct); written != nullptr) {
        return *written;
    }
    if (gct->type() != GCT::NodeKind::LINK || gct->size() < 2) {
        return std::nullopt;
    }
    const GCT::node_ptr_t &callee = gct->at(0);
    if (callee->type() != GCT::NodeKind::DREF) {
        return std::nullopt;
    }
    const std::string &group = callee->name();
    if (!isFoldedOperator(group) || hasProgramOperatorOverloads(group)) {
        return std::nullopt;
    }
    std::vector<literal::Value> values;
    for (size_t i = 1; i < gct->size(); ++i) {
        std::optional<literal::Value> operand = foldedLiteralValue(gct->at(i));
        if (!operand.has_value()) {
            return std::nullopt;
        }
        values.push_back(std::move(*operand));
    }
    // A comparison is a boolean, not a number; only its own call can fold it.
    if (isComparisonOperator(group)) {
        return std::nullopt;
    }
    return foldedValueOf(group, values);
}

node_handle_t Builder::foldLiteralCall(const GCT::node_ptr_t &gct, Type *expected) {
    if (gct == nullptr || gct->type() != GCT::NodeKind::LINK || gct->size() < 2) {
        return nullptr;
    }
    const GCT::node_ptr_t &callee = gct->at(0);
    if (callee->type() != GCT::NodeKind::DREF) {
        return nullptr;
    }
    const std::string &group = callee->name();
    if (!isFoldedOperator(group) || hasProgramOperatorOverloads(group)) {
        return nullptr;
    }
    std::vector<literal::Value> values;
    for (size_t i = 1; i < gct->size(); ++i) {
        std::optional<literal::Value> operand = foldedLiteralValue(gct->at(i));
        if (!operand.has_value()) {
            return nullptr;
        }
        values.push_back(std::move(*operand));
    }
    if (isComparisonOperator(group)) {
        if (values.size() != 2) {
            return nullptr;
        }
        const std::optional<bool> folded = foldedComparison(group, values[0], values[1]);
        if (!folded.has_value()) {
            return nullptr;
        }
        const GCT::node_ptr_t literal =
            tree_->makeData(tt::as_shared<Data>(std::make_shared<BoolData>(*folded)));
        literal->setOrigin(gct->origin());
        return visitDataNode(literal);
    }
    const std::optional<literal::Value> value = foldedValueOf(group, values);
    if (!value.has_value()) {
        return nullptr;
    }
    // The expected type decides, and only then the default type of the kind (design
    // principles 5.1). A value the expected type cannot hold is an error at the expression.
    Type *type = expected != nullptr && literal::kindEnters(*value, expected)
                     ? expected
                     : defaultTypeOf(*value);
    return makeLiteralNode(*value, type, gct);
}

void Builder::adaptLiteralOperands(
    const std::string &group, std::vector<Argument> &inputs, type_vec_t &types,
    const GCT::node_ptr_t &call, bool anyOperatorCall) {
    if (inputs.size() < 2 || (!anyOperatorCall && !isFoldedOperator(group))) {
        return;
    }
    // The value of the other operand decides the literal's type ("二元运算中另一个操作数的
    // 类型"). Two literals were folded before this, so every literal here has at least one
    // non-literal sibling; a literal with two numeric siblings of different types is left to
    // the operator, which has no overload for it either way.
    for (size_t i = 0; i < inputs.size(); ++i) {
        const std::optional<literal::Value> value = literalValueOfExpr(inputs[i].expr);
        if (!value.has_value() || i >= types.size() || types[i] == nullptr) {
            continue;
        }
        Type *other = nullptr;
        for (size_t j = 0; j < types.size() && other == nullptr; ++j) {
            if (j == i || types[j] == nullptr) {
                continue;
            }
            Type *candidate = valueTypeOf(types[j]);
            // The type of the other operand decides, and the operand's type says what it gives a
            // literal (design principles 5.1): a numeric scalar gives itself, a numeric array its
            // element type (a tensor's element type included — leaving element types out left
            // `bf16_v + 0.0` with no overload at all, while `f32_v + 0.0` is an f32 sum). One
            // answer for both, so the rule is stated once.
            if (candidate != nullptr) {
                other = candidate->literalOperandType();
            }
        }
        if (other == nullptr || other->equals(types[i])) {
            continue;
        }
        if (!literal::kindEnters(*value, other)) {
            // A float literal and an integer operand: the literal cannot become an integer,
            // and the integer operand keeps its type. No overload is the report.
            continue;
        }
        // A value that does not fit the other operand's type is a compile-time error.
        inputs[i].node = literalInType(*value, other, inputs[i].expr, true);
        types[i]       = other;
    }
}

} // namespace camel::compile::gir