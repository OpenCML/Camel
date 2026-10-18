/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Checked host evaluation and CUDA printing of predicated physical accesses.
#include "tiles/internal/access.h"
#include "tiles/program.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

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
namespace {
// The core expressions denote mathematical signed integers. This backend emits
// unsigned addresses only after proving every live intermediate is nonnegative
// and fits the core's signed range, so FloorDiv and Mod keep their meaning.
struct BoundedIndex {
    IndexExpr value;
    uint64_t lower, upper;
};
class ViewExpressions {
    const sem::IndexedInstance &instance;
    const std::vector<int64_t> &extents;
    const std::vector<IndexExpr> &point;
    std::unordered_map<sem::ExprId, BoundedIndex> lowered;

  public:
    ViewExpressions(
        const sem::IndexedInstance &i, const std::vector<int64_t> &e,
        const std::vector<IndexExpr> &p)
        : instance(i), extents(e), point(p) {}
    BoundedIndex expression(sem::ExprId id) {
        if (auto found = lowered.find(id); found != lowered.end())
            return found->second;
        const auto &node = instance.exprs[id];
        BoundedIndex result{IndexExpr(0), 0, 0};
        if (node.op == sem::ExprOp::Dim) {
            requireAccess(size_t(node.value) < point.size(), "invalid view dimension");
            result = {point[node.value], 0, static_cast<uint64_t>(extents[node.value] - 1)};
        } else if (node.op == sem::ExprOp::Const || node.op == sem::ExprOp::Param) {
            auto value = node.op == sem::ExprOp::Const ? std::optional<int64_t>(node.value)
                                                       : instance.paramValues()[node.value];
            requireAccess(value && *value >= 0, "view expression is negative or unresolved");
            result = {
                IndexExpr(static_cast<uint64_t>(*value)),
                static_cast<uint64_t>(*value),
                static_cast<uint64_t>(*value)};
        } else {
            auto a = expression(node.lhs), b = expression(node.rhs);
            if (node.op == sem::ExprOp::Add) {
                requireAccess(
                    a.upper <= uint64_t(INT64_MAX) - b.upper,
                    "view expression addition overflow");
                result = {a.value + b.value, a.lower + b.lower, a.upper + b.upper};
            } else if (node.op == sem::ExprOp::Mul) {
                requireAccess(
                    !b.upper || a.upper <= uint64_t(INT64_MAX) / b.upper,
                    "view expression multiplication overflow");
                result = {a.value * b.value, a.lower * b.lower, a.upper * b.upper};
            } else {
                requireAccess(
                    b.lower == b.upper && b.lower > 0,
                    "view divisor is not a positive known value");
                requireAccess(
                    node.op == sem::ExprOp::FloorDiv || node.op == sem::ExprOp::Mod,
                    "unsupported core view expression");
                if (node.op == sem::ExprOp::FloorDiv)
                    result = {a.value / b.value, a.lower / b.lower, a.upper / b.upper};
                else if (a.lower / b.lower == a.upper / b.lower)
                    result = {a.value % b.value, a.lower % b.lower, a.upper % b.upper};
                else
                    result = {a.value % b.value, 0, std::min(a.upper, b.upper - 1)};
            }
        }
        lowered.emplace(id, result);
        return result;
    }
};
} // namespace
IndexExpr coreViewAddress(const Program &program, const Node &node, const IndexExpr &linear) {
    requireAccess(
        node.op == "reindex" && node.indexing && node.inputs.size() == 1,
        "view requires a core instance and one data operand");
    requireAccess(
        node.offset == 0 && node.strides.empty(),
        "core view also carries private strides");
    const auto &instance = *node.indexing;
    const auto problem   = sem::validate(instance);
    requireAccess(!problem, "invalid core view instance");
    const auto extents = instance.extents();
    requireAccess(
        extents && *extents == node.shape && instance.domain.guard.empty() && instance.with.empty(),
        "view domain does not match the output");
    requireAccess(
        instance.output.mode == sem::WriteMode::Unique && !instance.output.indirect &&
            instance.output.valid.empty() && instance.output.coords.size() == node.shape.size() &&
            instance.law == sem::kNoLaw,
        "view output is not a unique pointwise map");
    for (size_t d = 0; d < node.shape.size(); ++d) {
        const auto &coordinate = instance.exprs[instance.output.coords[d]];
        requireAccess(
            instance.domain.dims[d].role == sem::DimRole::Parallel &&
                coordinate.op == sem::ExprOp::Dim && coordinate.value == int64_t(d),
            "view output is not the identity domain");
    }
    for (const auto &constraint : instance.domain.constraints)
        requireAccess(
            !instance.exprs.mentionsDim(constraint.lhs) &&
                !instance.exprs.mentionsDim(constraint.rhs),
            "dimension-dependent view constraint");
    requireAccess(
        sem::holds(instance, instance.domain.constraints, std::vector<int64_t>(node.shape.size())),
        "unsatisfied core view constraints");
    requireAccess(!instance.norm.empty(), "view has no source access");
    for (size_t i = 1; i < instance.norm.size(); ++i)
        requireAccess(
            instance.norm[i].kind == sem::AccessKind::None,
            "view reads an unbound operand");
    const auto &access = instance.norm[0];
    const auto &shape  = program.nodes.at(node.inputs[0]).shape;
    (void)elements(shape);
    requireAccess(
        access.kind == sem::AccessKind::Affine && access.valid.empty() && !access.indirect &&
            access.coords.size() == shape.size(),
        "view needs an unconditional core source map");
    // An empty domain reads no address. In particular, never form a modulo by
    // a zero extent or inspect unused coordinate arithmetic on this path.
    if (!elements(node.shape))
        return IndexExpr(0);
    std::vector<IndexExpr> point(node.shape.size(), IndexExpr(0));
    size_t stride = 1;
    for (size_t d = node.shape.size(); d-- > 0;) {
        point[d] = (linear / IndexExpr(stride)) % IndexExpr(node.shape[d]);
        stride *= node.shape[d];
    }
    ViewExpressions expressions(instance, *extents, point);
    IndexExpr address(0);
    stride = 1;
    for (size_t d = shape.size(); d-- > 0;) {
        const auto coordinate = expressions.expression(access.coords[d]);
        requireAccess(
            coordinate.upper < uint64_t(shape[d]),
            "core view access outside operand domain");
        address = address + coordinate.value * IndexExpr(stride);
        stride *= shape[d];
    }
    // Retain compact physical strides whenever the core can prove them. The
    // general expression above is the fallback, not a second broadcast rule.
    if (const auto view = sem::rowMajorView(instance, access, shape);
        view && view->offset >= 0 &&
        std::all_of(view->strides.begin(), view->strides.end(), [](auto value) {
            return value >= 0;
        })) {
        address = IndexExpr(view->offset);
        for (size_t d = 0; d < point.size(); ++d)
            if (view->strides[d])
                address = address + point[d] * IndexExpr(view->strides[d]);
    }
    return address;
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
