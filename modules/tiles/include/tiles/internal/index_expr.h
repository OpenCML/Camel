/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Structured unsigned address expressions in the physical kernel lowering.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace camel::tiles::kernel {
class IndexExpr {
    struct Node;
    std::shared_ptr<const Node> node;
    explicit IndexExpr(std::shared_ptr<const Node> node);
    static IndexExpr binary(char operation, IndexExpr lhs, IndexExpr rhs);

  public:
    explicit IndexExpr(uint64_t value);
    static IndexExpr variable(std::string name);
    uint64_t evaluate(const std::unordered_map<std::string, uint64_t> &bindings) const;
    IndexExpr substitute(const std::string &name, const IndexExpr &replacement) const;
    std::unordered_set<std::string> symbols() const;
    std::string cuda() const;
    friend IndexExpr operator+(IndexExpr lhs, IndexExpr rhs);
    friend IndexExpr operator*(IndexExpr lhs, IndexExpr rhs);
    friend IndexExpr operator/(IndexExpr lhs, IndexExpr rhs);
    friend IndexExpr operator%(IndexExpr lhs, IndexExpr rhs);
};
} // namespace camel::tiles::kernel
