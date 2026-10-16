/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Physical access masks and addresses, independent of CUDA statement strings.
#pragma once
#include "tiles/internal/index_expr.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace camel::tiles::kernel {
class Predicate {
    struct Node;
    std::shared_ptr<const Node> node;
    explicit Predicate(std::shared_ptr<const Node> node);

  public:
    explicit Predicate(bool value);
    static Predicate less(IndexExpr lhs, IndexExpr rhs);
    static Predicate equal(IndexExpr lhs, IndexExpr rhs);
    bool evaluate(const std::unordered_map<std::string, uint64_t> &bindings) const;
    Predicate substitute(const std::string &name, const IndexExpr &replacement) const;
    std::unordered_set<std::string> symbols() const;
    std::string cuda() const;
    friend Predicate operator&&(Predicate lhs, Predicate rhs);
    friend Predicate operator||(Predicate lhs, Predicate rhs);
    friend Predicate operator!(Predicate value);
};

struct Access {
    size_t buffer;
    IndexExpr address;
    Predicate mask;
    // No address evaluation takes place when the access is masked out.
    std::optional<uint64_t>
    evaluate(const std::unordered_map<std::string, uint64_t> &bindings) const;
    Access substitute(const std::string &name, const IndexExpr &replacement) const;
};
} // namespace camel::tiles::kernel
