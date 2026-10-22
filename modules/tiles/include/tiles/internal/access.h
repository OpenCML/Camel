// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Physical access masks and addresses, independent of CUDA statement strings.

#pragma once
#include "camel/core/index_semantics.h"
#include "tiles/internal/index_expr.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace camel::tiles {
struct Program;
struct Node;
} // namespace camel::tiles

namespace camel::tiles::kernel {
// Compose a nonnegative affine core map with row-major storage, checking each
// operand coordinate's bounds over the whole rectangular iteration domain.
IndexExpr affineAddress(
    const core::sem::IndexedInstance &instance, const core::sem::Access &access,
    const std::vector<int64_t> &shape, const std::vector<IndexExpr> &point);
// Lower an owned core view map into row-major storage. The graph instance remains
// authoritative; strided views are only an optional physical simplification.
IndexExpr coreViewAddress(const Program &program, const Node &node, const IndexExpr &linear);

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
