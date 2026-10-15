/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Checked host evaluation and CUDA printing of predicated physical accesses.
#include "tiles/internal/access.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace camel::tiles::kernel {
struct Predicate::Node {
    char operation;
    IndexExpr lhs{0}, rhs{0};
    std::shared_ptr<const Node> a, b;
};
Predicate::Predicate(std::shared_ptr<const Node> value) : node(std::move(value)) {}
Predicate::Predicate(bool value) : node(std::make_shared<const Node>(Node{value ? 't' : 'f'})) {}
Predicate Predicate::less(IndexExpr lhs, IndexExpr rhs) {
    return Predicate(std::make_shared<const Node>(Node{'<', std::move(lhs), std::move(rhs)}));
}
Predicate Predicate::equal(IndexExpr lhs, IndexExpr rhs) {
    return Predicate(std::make_shared<const Node>(Node{'=', std::move(lhs), std::move(rhs)}));
}
Predicate operator&&(Predicate a, Predicate b) {
    return Predicate(
        std::make_shared<const Predicate::Node>(
            Predicate::Node{.operation = '&', .a = std::move(a.node), .b = std::move(b.node)}));
}
Predicate operator||(Predicate a, Predicate b) {
    return Predicate(
        std::make_shared<const Predicate::Node>(
            Predicate::Node{.operation = '|', .a = std::move(a.node), .b = std::move(b.node)}));
}
Predicate operator!(Predicate value) {
    return Predicate(
        std::make_shared<const Predicate::Node>(
            Predicate::Node{.operation = '!', .a = std::move(value.node)}));
}
bool Predicate::evaluate(const std::unordered_map<std::string, uint64_t> &bindings) const {
    switch (node->operation) {
    case 't':
        return true;
    case 'f':
        return false;
    case '<':
        return node->lhs.evaluate(bindings) < node->rhs.evaluate(bindings);
    case '=':
        return node->lhs.evaluate(bindings) == node->rhs.evaluate(bindings);
    case '&':
        return Predicate(node->a).evaluate(bindings) && Predicate(node->b).evaluate(bindings);
    case '|':
        return Predicate(node->a).evaluate(bindings) || Predicate(node->b).evaluate(bindings);
    default:
        return !Predicate(node->a).evaluate(bindings);
    }
}
Predicate Predicate::substitute(const std::string &name, const IndexExpr &replacement) const {
    switch (node->operation) {
    case 't':
    case 'f':
        return *this;
    case '<':
        return less(
            node->lhs.substitute(name, replacement),
            node->rhs.substitute(name, replacement));
    case '=':
        return equal(
            node->lhs.substitute(name, replacement),
            node->rhs.substitute(name, replacement));
    case '&':
        return Predicate(node->a).substitute(name, replacement) &&
               Predicate(node->b).substitute(name, replacement);
    case '|':
        return Predicate(node->a).substitute(name, replacement) ||
               Predicate(node->b).substitute(name, replacement);
    default:
        return !Predicate(node->a).substitute(name, replacement);
    }
}
std::string Predicate::cuda() const {
    switch (node->operation) {
    case 't':
        return "true";
    case 'f':
        return "false";
    case '<':
        return "(" + node->lhs.cuda() + "<" + node->rhs.cuda() + ")";
    case '=':
        return "(" + node->lhs.cuda() + "==" + node->rhs.cuda() + ")";
    case '&':
        return "(" + Predicate(node->a).cuda() + "&&" + Predicate(node->b).cuda() + ")";
    case '|':
        return "(" + Predicate(node->a).cuda() + "||" + Predicate(node->b).cuda() + ")";
    default:
        return "(!" + Predicate(node->a).cuda() + ")";
    }
}
std::optional<uint64_t>
Access::evaluate(const std::unordered_map<std::string, uint64_t> &bindings) const {
    if (!mask.evaluate(bindings))
        return std::nullopt;
    return address.evaluate(bindings);
}
Access Access::substitute(const std::string &name, const IndexExpr &replacement) const {
    return {buffer, address.substitute(name, replacement), mask.substitute(name, replacement)};
}
std::string Access::loadF32(uint32_t fallbackBits) const {
    return "(" + mask.cuda() + "?v[" + std::to_string(buffer) + "][" + address.cuda() +
           "]:__uint_as_float(" + std::to_string(fallbackBits) + "U))";
}
} // namespace camel::tiles::kernel
