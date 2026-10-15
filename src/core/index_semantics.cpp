/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The index semantics of operators (see index_semantics.h): the quasi-affine expression pool,
 * the structural validation of an instance, the query through the operator table, the text
 * form, and the two transformations a back end needs first (splitting dims for tiles, the
 * strided view of a row-major operand).
 */

#include "camel/core/index_semantics.h"

#include "camel/core/operator_table.h"

#include <format>
#include <stdexcept>

namespace camel::core::sem {

namespace {

int64_t checkedAdd(int64_t a, int64_t b) {
    int64_t r = 0;
    if (__builtin_add_overflow(a, b, &r)) {
        throw std::overflow_error("index arithmetic overflows 64 bits");
    }
    return r;
}

int64_t checkedMul(int64_t a, int64_t b) {
    int64_t r = 0;
    if (__builtin_mul_overflow(a, b, &r)) {
        throw std::overflow_error("index arithmetic overflows 64 bits");
    }
    return r;
}

/// Mathematical floor division and modulo (the divisor is positive).
int64_t floorDivide(int64_t a, int64_t b) {
    const int64_t q = a / b;
    return (a % b != 0 && a < 0) ? q - 1 : q;
}
int64_t floorModulo(int64_t a, int64_t b) {
    const int64_t r = a % b;
    return r < 0 ? r + b : r;
}

} // namespace

// ---- ExprPool -------------------------------------------------------------------------------

size_t ExprPool::ExprHash::operator()(const Expr &e) const {
    size_t h = static_cast<size_t>(e.op);
    h        = h * 1000003u ^ std::hash<int64_t>{}(e.value);
    h        = h * 1000003u ^ e.lhs;
    return h * 1000003u ^ e.rhs;
}

ExprId ExprPool::intern(Expr expr) {
    if (auto it = interned_.find(expr); it != interned_.end()) {
        return it->second;
    }
    const auto id = static_cast<ExprId>(exprs_.size());
    exprs_.push_back(expr);
    interned_.emplace(expr, id);
    return id;
}

ExprId ExprPool::constant(int64_t value) { return intern({ExprOp::Const, value}); }
ExprId ExprPool::dim(uint32_t position) { return intern({ExprOp::Dim, position}); }
ExprId ExprPool::param(uint32_t position) { return intern({ExprOp::Param, position}); }

std::optional<int64_t> ExprPool::constantValue(ExprId id) const {
    return exprs_[id].op == ExprOp::Const ? std::optional(exprs_[id].value) : std::nullopt;
}

ExprId ExprPool::add(ExprId lhs, ExprId rhs) {
    const auto a = constantValue(lhs), b = constantValue(rhs);
    if (a && b) {
        return constant(checkedAdd(*a, *b));
    }
    if (a && *a == 0) {
        return rhs;
    }
    if (b && *b == 0) {
        return lhs;
    }
    if (a) {
        std::swap(lhs, rhs); // the constant term goes to the right
    }
    // (x + c1) + c2 = x + (c1 + c2)
    if (const auto c2 = constantValue(rhs); c2 && exprs_[lhs].op == ExprOp::Add) {
        if (const auto c1 = constantValue(exprs_[lhs].rhs)) {
            return add(exprs_[lhs].lhs, constant(checkedAdd(*c1, *c2)));
        }
    }
    return intern({ExprOp::Add, 0, lhs, rhs});
}

ExprId ExprPool::scale(int64_t c, ExprId e) { return mul(constant(c), e); }

ExprId ExprPool::sub(ExprId lhs, ExprId rhs) { return add(lhs, scale(-1, rhs)); }

ExprId ExprPool::mul(ExprId factor, ExprId e) {
    const ExprOp fop = exprs_[factor].op;
    if (fop != ExprOp::Const && fop != ExprOp::Param) {
        // A product of two expressions is quasi-affine only when one is a constant or a
        // parameter: let the other one be the factor.
        if (exprs_[e].op == ExprOp::Const || exprs_[e].op == ExprOp::Param) {
            return mul(e, factor);
        }
        throw std::invalid_argument(
            "a product of two expressions that are not constants or parameters is not "
            "quasi-affine");
    }
    const auto c = constantValue(factor), v = constantValue(e);
    if (c && v) {
        return constant(checkedMul(*c, *v));
    }
    if (c && *c == 0) {
        return constant(0);
    }
    if (c && *c == 1) {
        return e;
    }
    if (v && *v == 0) {
        return constant(0);
    }
    if (v && fop == ExprOp::Param) {
        return mul(e, factor); // the constant is the factor: 2 * p, not p * 2
    }
    if (c) {
        // constant() below may grow exprs_; keep a value across that mutation.
        const Expr inner = exprs_[e];
        // c1 * (c2 * x) = (c1 * c2) * x; c * (x + y) is kept as written (distributing would
        // grow the expression without making it more analysable).
        if (inner.op == ExprOp::Mul) {
            if (const auto c2 = constantValue(inner.lhs)) {
                return mul(constant(checkedMul(*c, *c2)), inner.rhs);
            }
        }
    }
    return intern({ExprOp::Mul, 0, factor, e});
}

void ExprPool::requireDivisor(ExprId divisor) const {
    const Expr &d = exprs_[divisor];
    if (d.op == ExprOp::Const ? d.value <= 0 : d.op != ExprOp::Param) {
        throw std::invalid_argument(
            "a floordiv or mod divides by a positive constant or by a parameter");
    }
}

ExprId ExprPool::floorDiv(ExprId lhs, ExprId divisor) {
    requireDivisor(divisor);
    const auto a = constantValue(lhs), d = constantValue(divisor);
    if (d && *d == 1) {
        return lhs;
    }
    if (a && d) {
        return constant(floorDivide(*a, *d));
    }
    return intern({ExprOp::FloorDiv, 0, lhs, divisor});
}

ExprId ExprPool::mod(ExprId lhs, ExprId divisor) {
    requireDivisor(divisor);
    const auto a = constantValue(lhs), d = constantValue(divisor);
    if (d && *d == 1) {
        return constant(0);
    }
    if (a && d) {
        return constant(floorModulo(*a, *d));
    }
    return intern({ExprOp::Mod, 0, lhs, divisor});
}

int64_t ExprPool::evaluate(ExprId id, std::span<const int64_t> dims, ParamValues params) const {
    const Expr &e = exprs_[id];
    switch (e.op) {
    case ExprOp::Const:
        return e.value;
    case ExprOp::Dim:
        return dims[static_cast<size_t>(e.value)];
    case ExprOp::Param: {
        const auto &value = params[static_cast<size_t>(e.value)];
        if (!value) {
            throw std::invalid_argument(std::format("parameter {} has no known value", e.value));
        }
        return *value;
    }
    case ExprOp::Add:
        return checkedAdd(evaluate(e.lhs, dims, params), evaluate(e.rhs, dims, params));
    case ExprOp::Mul:
        return checkedMul(evaluate(e.lhs, dims, params), evaluate(e.rhs, dims, params));
    case ExprOp::FloorDiv:
    case ExprOp::Mod: {
        const int64_t a = evaluate(e.lhs, dims, params), d = evaluate(e.rhs, dims, params);
        if (d <= 0) {
            throw std::invalid_argument("a floordiv or mod by a parameter that is not positive");
        }
        return e.op == ExprOp::FloorDiv ? floorDivide(a, d) : floorModulo(a, d);
    }
    }
    return 0;
}

bool ExprPool::mentionsDim(ExprId id) const {
    const Expr &e = exprs_[id];
    switch (e.op) {
    case ExprOp::Dim:
        return true;
    case ExprOp::Const:
    case ExprOp::Param:
        return false;
    default:
        return mentionsDim(e.lhs) || mentionsDim(e.rhs);
    }
}

std::optional<AffineForm> ExprPool::affine(ExprId id, size_t dimCount, ParamValues params) const {
    const Expr &e = exprs_[id];
    AffineForm form;
    form.coeffs.assign(dimCount, 0);
    switch (e.op) {
    case ExprOp::Const:
        form.constant = e.value;
        return form;
    case ExprOp::Dim:
        form.coeffs[static_cast<size_t>(e.value)] = 1;
        return form;
    case ExprOp::Param: {
        const auto &value = params[static_cast<size_t>(e.value)];
        if (!value) {
            return std::nullopt;
        }
        form.constant = *value;
        return form;
    }
    case ExprOp::Add: {
        auto a = affine(e.lhs, dimCount, params), b = affine(e.rhs, dimCount, params);
        if (!a || !b) {
            return std::nullopt;
        }
        a->constant = checkedAdd(a->constant, b->constant);
        for (size_t d = 0; d < dimCount; ++d) {
            a->coeffs[d] = checkedAdd(a->coeffs[d], b->coeffs[d]);
        }
        return a;
    }
    case ExprOp::Mul: {
        const auto factor = affine(e.lhs, dimCount, params); // a constant once params are known
        auto b            = affine(e.rhs, dimCount, params);
        if (!factor || !b) {
            return std::nullopt;
        }
        b->constant = checkedMul(factor->constant, b->constant);
        for (int64_t &coeff : b->coeffs) {
            coeff = checkedMul(factor->constant, coeff);
        }
        return b;
    }
    case ExprOp::FloorDiv:
    case ExprOp::Mod: {
        // Affine only when nothing under it depends on a dim: then it is a constant.
        if (mentionsDim(e.lhs)) {
            return std::nullopt;
        }
        try {
            form.constant = evaluate(id, {}, params);
        } catch (const std::invalid_argument &) {
            return std::nullopt;
        }
        return form;
    }
    }
    return std::nullopt;
}

std::string ExprPool::text(
    ExprId id, std::span<const std::string> dimNames,
    std::span<const std::string> paramNames) const {
    const Expr &e = exprs_[id];
    // An operand of a product, floordiv or mod is parenthesized when it is a sum, a floordiv
    // or a mod: "(d0 + d1) floordiv 4", "(d0 floordiv 4) mod 3".
    auto operand = [&](ExprId sub) {
        const std::string t = text(sub, dimNames, paramNames);
        const ExprOp op     = exprs_[sub].op;
        return op == ExprOp::Add || op == ExprOp::FloorDiv || op == ExprOp::Mod ? "(" + t + ")" : t;
    };
    switch (e.op) {
    case ExprOp::Const:
        return std::to_string(e.value);
    case ExprOp::Dim:
        return static_cast<size_t>(e.value) < dimNames.size()
                   ? dimNames[static_cast<size_t>(e.value)]
                   : std::format("d{}", e.value);
    case ExprOp::Param:
        return static_cast<size_t>(e.value) < paramNames.size()
                   ? paramNames[static_cast<size_t>(e.value)]
                   : std::format("p{}", e.value);
    case ExprOp::Add: {
        const std::string lhs = text(e.lhs, dimNames, paramNames);
        const Expr &r         = exprs_[e.rhs];
        // x + (-c) and x + (-c) * y read as subtractions.
        if (r.op == ExprOp::Const && r.value < 0 && r.value != INT64_MIN) {
            return std::format("{} - {}", lhs, -r.value);
        }
        if (r.op == ExprOp::Mul) {
            if (const auto c = constantValue(r.lhs); c && *c < 0 && *c != INT64_MIN) {
                const std::string rest = operand(r.rhs);
                return *c == -1 ? std::format("{} - {}", lhs, rest)
                                : std::format("{} - {}*{}", lhs, -*c, rest);
            }
        }
        return std::format("{} + {}", lhs, text(e.rhs, dimNames, paramNames));
    }
    case ExprOp::Mul:
        if (const auto c = constantValue(e.lhs); c && *c == -1) {
            return "-" + operand(e.rhs);
        }
        return std::format("{}*{}", operand(e.lhs), operand(e.rhs));
    case ExprOp::FloorDiv:
        return std::format("{} floordiv {}", operand(e.lhs), operand(e.rhs));
    case ExprOp::Mod:
        return std::format("{} mod {}", operand(e.lhs), operand(e.rhs));
    }
    return "?";
}

// ---- Names ----------------------------------------------------------------------------------

std::string_view dimRoleName(DimRole role) {
    switch (role) {
    case DimRole::Parallel:
        return "parallel";
    case DimRole::Reduction:
        return "reduction";
    case DimRole::Sequential:
        return "sequential";
    }
    return "?";
}

std::string_view paramSourceName(ParamSource source) {
    switch (source) {
    case ParamSource::Extent:
        return "extent";
    case ParamSource::Static:
        return "static";
    case ParamSource::Runtime:
        return "runtime";
    case ParamSource::Derived:
        return "derived";
    }
    return "?";
}

std::string_view onInvalidName(OnInvalid policy) {
    switch (policy) {
    case OnInvalid::Unreachable:
        return "unreachable";
    case OnInvalid::Fault:
        return "fault";
    case OnInvalid::Excluded:
        return "excluded";
    case OnInvalid::Padding:
        return "padding";
    }
    return "?";
}

std::string_view accessKindName(AccessKind kind) {
    switch (kind) {
    case AccessKind::Affine:
        return "affine";
    case AccessKind::Indirect:
        return "indirect";
    case AccessKind::Whole:
        return "whole";
    case AccessKind::None:
        return "none";
    }
    return "?";
}

std::string_view writeModeName(WriteMode mode) {
    switch (mode) {
    case WriteMode::Unique:
        return "unique";
    case WriteMode::Combine:
        return "combine";
    case WriteMode::OrderedOverwrite:
        return "ordered_overwrite";
    }
    return "?";
}

// ---- Instances ------------------------------------------------------------------------------

std::vector<std::optional<int64_t>> Domain::paramValues() const {
    std::vector<std::optional<int64_t>> values;
    values.reserve(params.size());
    for (const Param &p : params) {
        values.push_back(p.value);
    }
    return values;
}

std::optional<std::vector<int64_t>> IndexedInstance::extents() const {
    const auto values = paramValues();
    std::vector<int64_t> out;
    for (const Dim &d : domain.dims) {
        try {
            out.push_back(exprs.evaluate(d.extent, {}, values));
        } catch (const std::invalid_argument &) {
            return std::nullopt;
        }
    }
    return out;
}

bool holds(
    const IndexedInstance &instance, const Predicate &predicate, std::span<const int64_t> point) {
    const auto values = instance.paramValues();
    for (const Constraint &c : predicate) {
        const int64_t a = instance.exprs.evaluate(c.lhs, point, values);
        const int64_t b = instance.exprs.evaluate(c.rhs, point, values);
        const bool ok   = c.rel == Constraint::Rel::Eq   ? a == b
                          : c.rel == Constraint::Rel::Le ? a <= b
                                                         : a < b;
        if (!ok) {
            return false;
        }
    }
    return true;
}

namespace {

/// Checks of validate() on one expression tree: ids in range, positive divisors.
class Validator {
  public:
    explicit Validator(const IndexedInstance &inst) : inst_(inst) {}

    std::optional<std::string> expr(ExprId id) const {
        if (id >= inst_.exprs.size()) {
            return std::format("expression {} is not in the instance", id);
        }
        const Expr &e = inst_.exprs[id];
        switch (e.op) {
        case ExprOp::Const:
            return std::nullopt;
        case ExprOp::Dim:
            if (static_cast<size_t>(e.value) >= inst_.domain.dims.size()) {
                return std::format("dim {} is not in the domain", e.value);
            }
            return std::nullopt;
        case ExprOp::Param:
            if (static_cast<size_t>(e.value) >= inst_.domain.params.size()) {
                return std::format("parameter {} is not in the domain", e.value);
            }
            return std::nullopt;
        case ExprOp::FloorDiv:
        case ExprOp::Mod:
            if (auto problem = divisor(e.rhs)) {
                return problem;
            }
            [[fallthrough]];
        case ExprOp::Add:
        case ExprOp::Mul:
            if (auto problem = expr(e.lhs)) {
                return problem;
            }
            return expr(e.rhs);
        }
        return std::nullopt;
    }

    std::optional<std::string> predicate(const Predicate &p) const {
        for (const Constraint &c : p) {
            if (auto problem = expr(c.lhs)) {
                return problem;
            }
            if (auto problem = expr(c.rhs)) {
                return problem;
            }
        }
        return std::nullopt;
    }

    std::optional<std::string> coords(const std::vector<ExprId> &list, bool allowEmptySlot) const {
        for (ExprId id : list) {
            if (id == kNoExpr && allowEmptySlot) {
                continue;
            }
            if (auto problem = expr(id)) {
                return problem;
            }
        }
        return std::nullopt;
    }

  private:
    /// A divisor is a positive constant or a parameter known (or required) to be positive.
    std::optional<std::string> divisor(ExprId id) const {
        if (id >= inst_.exprs.size()) {
            return std::format("expression {} is not in the instance", id);
        }
        const Expr &d = inst_.exprs[id];
        if (d.op == ExprOp::Const && d.value > 0) {
            return std::nullopt;
        }
        if (d.op != ExprOp::Param || static_cast<size_t>(d.value) >= inst_.domain.params.size()) {
            return "a floordiv or mod divides by something other than a constant or a parameter";
        }
        const Param &p = inst_.domain.params[static_cast<size_t>(d.value)];
        if (p.value) {
            return *p.value > 0
                       ? std::nullopt
                       : std::optional<std::string>(
                             std::format("a floordiv or mod divides by {} = {}", p.name, *p.value));
        }
        // An unknown parameter: some constraint must require p >= c with c >= 1 (written
        // c <= p or c < p).
        for (const Constraint &c : inst_.domain.constraints) {
            if (c.rhs != id || c.lhs >= inst_.exprs.size()) {
                continue;
            }
            const auto bound = inst_.exprs.constantValue(c.lhs);
            if (bound && ((c.rel == Constraint::Rel::Le && *bound >= 1) ||
                          (c.rel == Constraint::Rel::Lt && *bound >= 0))) {
                return std::nullopt;
            }
        }
        return std::format(
            "a floordiv or mod divides by {}, which the constraints do not require to be positive",
            p.name);
    }

    const IndexedInstance &inst_;
};

} // namespace

std::optional<std::string>
validate(const IndexedInstance &instance, const OperatorSemantics *semantics) {
    const Validator v(instance);
    for (const Param &p : instance.domain.params) {
        if (p.definition != kNoExpr) {
            if (auto problem = v.expr(p.definition)) {
                return problem;
            }
            if (instance.exprs.mentionsDim(p.definition)) {
                return std::format("parameter {} is defined in terms of a dim", p.name);
            }
        }
    }
    bool reduces = false;
    for (const Dim &d : instance.domain.dims) {
        if (auto problem = v.expr(d.extent)) {
            return problem;
        }
        if (instance.exprs.mentionsDim(d.extent)) {
            return std::format("the extent of {} depends on a dim", d.name);
        }
        reduces |= d.role == DimRole::Reduction;
    }
    if (auto problem = v.predicate(instance.domain.constraints)) {
        return problem;
    }
    for (const Constraint &c : instance.domain.constraints) {
        if (instance.exprs.mentionsDim(c.lhs) || instance.exprs.mentionsDim(c.rhs)) {
            return "a constraint of the domain depends on a dim (it constrains parameters)";
        }
    }
    if (auto problem = v.predicate(instance.domain.guard)) {
        return problem;
    }
    for (const auto *list : {&instance.norm, &instance.with}) {
        for (const Access &a : *list) {
            if (a.kind == AccessKind::Indirect && !a.indirect) {
                return "an indirect access does not say where its index is read";
            }
            if (auto problem = v.coords(a.coords, a.kind == AccessKind::Indirect)) {
                return problem;
            }
            if (a.indirect) {
                if (auto problem = v.coords(a.indirect->indexCoords, false)) {
                    return problem;
                }
            }
            if (auto problem = v.predicate(a.valid)) {
                return problem;
            }
        }
    }
    if (auto problem = v.coords(instance.output.coords, instance.output.indirect.has_value())) {
        return problem;
    }
    if (auto problem = v.predicate(instance.output.valid)) {
        return problem;
    }
    if (reduces && instance.output.mode == WriteMode::Unique) {
        return "a domain with a reduction dim writes each result element more than once, not "
               "uniquely";
    }
    if (instance.output.mode == WriteMode::Combine) {
        if (instance.law == kNoLaw) {
            return "a combining write names no reduction law";
        }
        (void)semantics; // the laws are checked against the record from phase 2's laws on
    }
    return std::nullopt;
}

std::optional<IndexedInstance> instantiate(OperId id, const InstanceContext &cx) {
    const OperatorSemantics *s = OperatorTable::instance().semantics(id);
    if (s == nullptr || !s->indexing) {
        return std::nullopt;
    }
    std::optional<IndexedInstance> instance;
    try {
        instance = s->indexing(cx);
    } catch (const std::exception &) {
        return std::nullopt; // a rejected argument, or a defect: no instance either way
    }
    if (instance && validate(*instance, s)) {
        return std::nullopt;
    }
    return instance;
}

// ---- Text form ------------------------------------------------------------------------------

namespace {

/// Names and printing helpers for one instance.
class Printer {
  public:
    explicit Printer(const IndexedInstance &inst) : inst_(inst) {
        for (const Dim &d : inst.domain.dims) {
            dims_.push_back(d.name);
        }
        for (const Param &p : inst.domain.params) {
            params_.push_back(p.name);
        }
    }

    std::string expr(ExprId id) const {
        return id == kNoExpr ? "_" : inst_.exprs.text(id, dims_, params_);
    }

    std::string tuple(
        const std::vector<ExprId> &coords, const std::optional<Indirect> &indirect,
        std::string_view operandWord) const {
        std::string out = "(";
        for (size_t i = 0; i < coords.size(); ++i) {
            out += i ? ", " : "";
            if (indirect && indirect->axis == i) {
                out += std::format(
                    "{} {}[{}]",
                    operandWord,
                    indirect->indexOperand,
                    list(indirect->indexCoords));
            } else {
                out += expr(coords[i]);
            }
        }
        return out + ")";
    }

    std::string list(const std::vector<ExprId> &coords) const {
        std::string out;
        for (size_t i = 0; i < coords.size(); ++i) {
            out += (i ? ", " : "") + expr(coords[i]);
        }
        return out;
    }

    std::string predicate(const Predicate &p) const {
        std::string out;
        for (const Constraint &c : p) {
            const char *rel = c.rel == Constraint::Rel::Eq   ? "="
                              : c.rel == Constraint::Rel::Le ? "<="
                                                             : "<";
            out +=
                std::format("{}{} {} {}", out.empty() ? "" : ", ", expr(c.lhs), rel, expr(c.rhs));
        }
        return out;
    }

  private:
    const IndexedInstance &inst_;
    std::vector<std::string> dims_, params_;
};

std::string paramText(const Printer &pr, const Param &p) {
    std::string out = p.name + " = " + std::string(paramSourceName(p.source));
    switch (p.source) {
    case ParamSource::Extent:
        out += std::format("({}, {})", p.operand, p.axis);
        break;
    case ParamSource::Static:
        out += p.axis == 0xFF ? std::format("({})", p.operand)
                              : std::format("({}[{}])", p.operand, p.axis);
        break;
    case ParamSource::Runtime:
        out += std::format("({})", p.operand);
        break;
    case ParamSource::Derived:
        if (p.definition != kNoExpr) {
            out += "(" + pr.expr(p.definition) + ")";
        }
        break;
    }
    if (p.value) {
        out += std::format(" = {}", *p.value);
    }
    return out;
}

std::string accessText(const Printer &pr, const Access &a) {
    std::string out;
    switch (a.kind) {
    case AccessKind::Whole:
        return "whole";
    case AccessKind::None:
        return "none";
    case AccessKind::Affine:
    case AccessKind::Indirect:
        out = pr.tuple(a.coords, a.indirect, "norm");
        break;
    }
    if (!a.valid.empty()) {
        out += " valid [" + pr.predicate(a.valid) + "] else " +
               std::string(onInvalidName(a.onInvalid));
        if (a.onInvalid == OnInvalid::Padding) {
            out += std::format("(0x{:x})", a.padding);
        }
    }
    return out;
}

} // namespace

std::string toString(const IndexedInstance &instance, const OperatorSemantics *semantics) {
    (void)semantics;
    const Printer pr(instance);
    std::string out;
    std::string params;
    for (const Param &p : instance.domain.params) {
        params += (params.empty() ? "" : "; ") + paramText(pr, p);
    }
    out += "params   " + params + "\n";
    std::string dims;
    for (const Dim &d : instance.domain.dims) {
        dims += std::format(
            "{}{} < {} {}",
            dims.empty() ? "" : "; ",
            d.name,
            pr.expr(d.extent),
            dimRoleName(d.role));
    }
    out += "domain   " + dims + "\n";
    if (!instance.domain.constraints.empty()) {
        out += "where    " + pr.predicate(instance.domain.constraints) + "\n";
    }
    if (!instance.domain.guard.empty()) {
        out += "guard    " + pr.predicate(instance.domain.guard) + "\n";
    }
    for (size_t i = 0; i < instance.norm.size(); ++i) {
        out += std::format("norm {}   {}\n", i, accessText(pr, instance.norm[i]));
    }
    for (size_t i = 0; i < instance.with.size(); ++i) {
        out += std::format("with {}   {}\n", i, accessText(pr, instance.with[i]));
    }
    const Write &w = instance.output;
    out += "write    " + pr.tuple(w.coords, w.indirect, "norm") + " " +
           std::string(writeModeName(w.mode));
    if (instance.law != kNoLaw) {
        out += std::format(" law {}", instance.law);
    }
    if (!w.valid.empty()) {
        out += " valid [" + pr.predicate(w.valid) + "]";
    }
    return out + "\n";
}

// ---- Transformations ------------------------------------------------------------------------

namespace {

/// Rebuilds the expressions of `from` in `to`, with every dim replaced by the expression
/// `dims[d]` of `to` and every parameter by `to`'s parameter of the same position.
class Rewriter {
  public:
    Rewriter(const ExprPool &from, ExprPool &to, std::vector<ExprId> dims)
        : from_(from), to_(to), dims_(std::move(dims)), memo_(from.size(), kNoExpr) {}

    ExprId operator()(ExprId id) {
        if (id == kNoExpr) {
            return kNoExpr;
        }
        if (memo_[id] != kNoExpr) {
            return memo_[id];
        }
        const Expr &e = from_[id];
        ExprId out    = kNoExpr;
        switch (e.op) {
        case ExprOp::Const:
            out = to_.constant(e.value);
            break;
        case ExprOp::Dim:
            out = dims_[static_cast<size_t>(e.value)];
            break;
        case ExprOp::Param:
            out = to_.param(static_cast<uint32_t>(e.value));
            break;
        case ExprOp::Add:
            out = to_.add((*this)(e.lhs), (*this)(e.rhs));
            break;
        case ExprOp::Mul:
            out = to_.mul((*this)(e.lhs), (*this)(e.rhs));
            break;
        case ExprOp::FloorDiv:
            out = to_.floorDiv((*this)(e.lhs), (*this)(e.rhs));
            break;
        case ExprOp::Mod:
            out = to_.mod((*this)(e.lhs), (*this)(e.rhs));
            break;
        }
        return memo_[id] = out;
    }

    std::vector<ExprId> list(const std::vector<ExprId> &ids) {
        std::vector<ExprId> out;
        for (ExprId id : ids) {
            out.push_back((*this)(id));
        }
        return out;
    }

    Predicate predicate(const Predicate &p) {
        Predicate out;
        for (const Constraint &c : p) {
            out.push_back({(*this)(c.lhs), c.rel, (*this)(c.rhs)});
        }
        return out;
    }

  private:
    const ExprPool &from_;
    ExprPool &to_;
    std::vector<ExprId> dims_;
    std::vector<ExprId> memo_;
};

} // namespace

IndexedInstance splitDims(const IndexedInstance &instance, std::span<const DimSplit> splits) {
    const auto &oldDims = instance.domain.dims;
    std::vector<int64_t> factor(oldDims.size(), 0);
    for (const DimSplit &s : splits) {
        if (s.dim >= oldDims.size() || s.factor < 1) {
            throw std::invalid_argument("splitDims: a dim out of range or a factor below 1");
        }
        factor[s.dim] = s.factor;
    }
    IndexedInstance out;
    out.domain.params = instance.domain.params;
    out.law           = instance.law;
    // The new dims, in place of the old ones: an old dim d becomes f * outer + inner.
    std::vector<ExprId> image(oldDims.size(), kNoExpr);
    std::vector<std::pair<size_t, int64_t>> bounds; // (old dim, f) whose original bound is kept
    uint32_t next = 0;
    for (size_t d = 0; d < oldDims.size(); ++d) {
        if (factor[d] == 0) {
            image[d] = out.exprs.dim(next++);
            continue;
        }
        const ExprId outer = out.exprs.dim(next++);
        const ExprId inner = out.exprs.dim(next++);
        image[d]           = out.exprs.add(out.exprs.scale(factor[d], outer), inner);
        bounds.emplace_back(d, factor[d]);
    }
    Rewriter rw(instance.exprs, out.exprs, image);
    for (size_t d = 0; d < oldDims.size(); ++d) {
        const Dim &old      = oldDims[d];
        const ExprId extent = rw(old.extent);
        if (factor[d] == 0) {
            out.domain.dims.push_back({old.name, extent, old.role});
            continue;
        }
        const ExprId f = out.exprs.constant(factor[d]);
        out.domain.dims.push_back(
            {old.name + "_o",
             out.exprs.floorDiv(out.exprs.add(extent, out.exprs.constant(factor[d] - 1)), f),
             old.role});
        out.domain.dims.push_back({old.name + "_i", f, old.role});
    }
    for (Param &p : out.domain.params) {
        p.definition = rw(p.definition);
    }
    out.domain.constraints = rw.predicate(instance.domain.constraints);
    out.domain.guard       = rw.predicate(instance.domain.guard);
    for (const auto &[d, f] : bounds) {
        out.domain.guard.push_back({image[d], Constraint::Rel::Lt, rw(oldDims[d].extent)});
    }
    auto access = [&](const Access &a) {
        Access b = a;
        b.coords = rw.list(a.coords);
        b.valid  = rw.predicate(a.valid);
        if (a.indirect) {
            b.indirect->indexCoords = rw.list(a.indirect->indexCoords);
        }
        return b;
    };
    for (const Access &a : instance.norm) {
        out.norm.push_back(access(a));
    }
    for (const Access &a : instance.with) {
        out.with.push_back(access(a));
    }
    out.output        = instance.output;
    out.output.coords = rw.list(instance.output.coords);
    out.output.valid  = rw.predicate(instance.output.valid);
    if (instance.output.indirect) {
        out.output.indirect->indexCoords = rw.list(instance.output.indirect->indexCoords);
    }
    return out;
}

std::optional<StridedView> rowMajorView(
    const IndexedInstance &instance, const Access &access, std::span<const int64_t> shape) {
    if (access.kind != AccessKind::Affine || access.coords.size() != shape.size()) {
        return std::nullopt;
    }
    const auto values = instance.paramValues();
    const size_t dims = instance.domain.dims.size();
    StridedView view;
    view.strides.assign(dims, 0);
    try {
        int64_t stride = 1; // row-major: the last axis is contiguous
        for (size_t axis = shape.size(); axis-- > 0;) {
            const auto form = instance.exprs.affine(access.coords[axis], dims, values);
            if (!form) {
                return std::nullopt;
            }
            view.offset = checkedAdd(view.offset, checkedMul(stride, form->constant));
            for (size_t d = 0; d < dims; ++d) {
                view.strides[d] = checkedAdd(view.strides[d], checkedMul(stride, form->coeffs[d]));
            }
            stride = checkedMul(stride, shape[axis]);
        }
    } catch (const std::overflow_error &) {
        return std::nullopt;
    }
    return view;
}

} // namespace camel::core::sem
