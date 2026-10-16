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
#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace camel::tiles::kernel {
namespace sem = core::sem;
namespace {
void requireAccess(bool ok, const char *reason) {
    if (!ok)
        throw std::runtime_error(std::string("core access: ") + reason);
}
} // namespace
// Compose a core affine access with row-major storage and a physical iteration point.
// Signed, guarded and indirect maps need a different lowering; never silently ignore them.
IndexExpr affineAddress(
    const sem::IndexedInstance &inst, const sem::Access &access, const std::vector<int64_t> &shape,
    const std::vector<IndexExpr> &point) {
    requireAccess(
        access.kind == sem::AccessKind::Affine && access.valid.empty() && !access.indirect,
        "requires unconditional affine accesses");
    const auto extents = inst.extents();
    requireAccess(extents && point.size() == extents->size(), "unresolved iteration domain");
    const auto view = sem::rowMajorView(inst, access, shape);
    requireAccess(view && view->offset >= 0, "unrepresentable storage map");
    const bool empty = std::find(extents->begin(), extents->end(), 0) != extents->end();
    for (size_t axis = 0; axis < access.coords.size(); ++axis) {
        const auto affine =
            inst.exprs.affine(access.coords[axis], point.size(), inst.paramValues());
        requireAccess(affine && affine->constant >= 0, "negative or nonlinear coordinate");
        IndexExpr maximum(static_cast<uint64_t>(affine->constant));
        for (size_t d = 0; d < point.size(); ++d) {
            requireAccess(affine->coeffs[d] >= 0, "negative coordinate stride");
            if (!empty)
                maximum = maximum + IndexExpr(affine->coeffs[d]) * IndexExpr((*extents)[d] - 1);
        }
        requireAccess(
            empty || maximum.evaluate({}) < static_cast<uint64_t>(shape.at(axis)),
            "access outside operand domain");
    }
    IndexExpr result(view->offset);
    for (size_t d = 0; d < point.size(); ++d) {
        requireAccess(view->strides[d] >= 0, "negative physical stride");
        if (view->strides[d])
            result = result + point[d] * IndexExpr(view->strides[d]);
    }
    return result;
}
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
std::unordered_set<std::string> Predicate::symbols() const {
    if (node->operation == 't' || node->operation == 'f')
        return {};
    if (node->operation == '<' || node->operation == '=') {
        auto result = node->lhs.symbols();
        result.merge(node->rhs.symbols());
        return result;
    }
    auto result = Predicate(node->a).symbols();
    if (node->b)
        result.merge(Predicate(node->b).symbols());
    return result;
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
} // namespace camel::tiles::kernel
