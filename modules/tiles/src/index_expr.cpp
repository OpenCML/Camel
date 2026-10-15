/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Address composition, checked host evaluation and final CUDA serialization.
#include "tiles/internal/index_expr.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace camel::tiles::kernel {
struct IndexExpr::Node {
    char operation = 'c';
    uint64_t value = 0;
    std::string name;
    std::shared_ptr<const Node> lhs, rhs;
};
namespace {
uint64_t calculate(char operation, uint64_t a, uint64_t b) {
    switch (operation) {
    case '+':
        if (a > std::numeric_limits<uint64_t>::max() - b)
            throw std::overflow_error("index addition overflow");
        return a + b;
    case '*':
        if (b && a > std::numeric_limits<uint64_t>::max() / b)
            throw std::overflow_error("index multiplication overflow");
        return a * b;
    case '/':
    case '%':
        if (!b)
            throw std::invalid_argument("zero index divisor");
        return operation == '/' ? a / b : a % b;
    default:
        throw std::invalid_argument("unknown index expression operation");
    }
}
bool initial(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
} // namespace
IndexExpr::IndexExpr(std::shared_ptr<const Node> value) : node(std::move(value)) {}
IndexExpr::IndexExpr(uint64_t value) : node(std::make_shared<const Node>(Node{.value = value})) {}
IndexExpr IndexExpr::variable(std::string name) {
    if (name.empty() || !initial(name.front()) ||
        !std::all_of(name.begin(), name.end(), [](char c) {
            return initial(c) || (c >= '0' && c <= '9');
        }))
        throw std::invalid_argument("invalid index variable name");
    return IndexExpr(std::make_shared<const Node>(Node{.operation = 'v', .name = std::move(name)}));
}
IndexExpr IndexExpr::binary(char op, IndexExpr a, IndexExpr b) {
    if ((op == '/' || op == '%') && b.node->operation == 'c' && !b.node->value)
        throw std::invalid_argument("zero index divisor");
    if (a.node->operation == 'c' && b.node->operation == 'c')
        return IndexExpr(calculate(op, a.node->value, b.node->value));
    // Do not erase potentially invalid subexpressions with algebraic shortcuts.
    return IndexExpr(
        std::make_shared<const Node>(
            Node{.operation = op, .lhs = std::move(a.node), .rhs = std::move(b.node)}));
}
IndexExpr operator+(IndexExpr a, IndexExpr b) {
    return IndexExpr::binary('+', std::move(a), std::move(b));
}
IndexExpr operator*(IndexExpr a, IndexExpr b) {
    return IndexExpr::binary('*', std::move(a), std::move(b));
}
IndexExpr operator/(IndexExpr a, IndexExpr b) {
    return IndexExpr::binary('/', std::move(a), std::move(b));
}
IndexExpr operator%(IndexExpr a, IndexExpr b) {
    return IndexExpr::binary('%', std::move(a), std::move(b));
}
uint64_t IndexExpr::evaluate(const std::unordered_map<std::string, uint64_t> &bindings) const {
    if (node->operation == 'c')
        return node->value;
    if (node->operation == 'v') {
        auto found = bindings.find(node->name);
        if (found == bindings.end())
            throw std::invalid_argument("unbound index variable: " + node->name);
        return found->second;
    }
    return calculate(
        node->operation,
        IndexExpr(node->lhs).evaluate(bindings),
        IndexExpr(node->rhs).evaluate(bindings));
}
IndexExpr IndexExpr::substitute(const std::string &name, const IndexExpr &replacement) const {
    if (node->operation == 'c')
        return *this;
    if (node->operation == 'v')
        return node->name == name ? replacement : *this;
    return binary(
        node->operation,
        IndexExpr(node->lhs).substitute(name, replacement),
        IndexExpr(node->rhs).substitute(name, replacement));
}
std::string IndexExpr::cuda() const {
    if (node->operation == 'c')
        return std::to_string(node->value) + "ULL";
    if (node->operation == 'v')
        return node->name;
    return "(" + IndexExpr(node->lhs).cuda() + node->operation + IndexExpr(node->rhs).cuda() + ")";
}
} // namespace camel::tiles::kernel
