// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * TEST-ONLY: the declaration evaluator (see index_eval.h).
 *
 * The scalar functions of the elementwise operators and the folding of the reduction laws are
 * written here independently of the kernels (no call into minmax.h or the kernels' loops): an
 * evaluator that shared the kernels' code would agree with them by construction.
 */

#include "index_eval.h"

#include "../interop.h"
#include "../ops/indexing.h"
#include "../ops/registry.h"
#include "../ops/support.h"
#include "../type.h"
#include "camel/core/mm.h"
#include "camel/core/operator_table.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/type/composite/array.h"

#include <algorithm>
#include <bit>
#include <cfloat>
#include <cmath>
#include <format>
#include <functional>
#include <map>
#include <ostream>
#include <string>
#include <vector>

namespace camel::tensor::passes {

namespace {

namespace sem = camel::core::sem;
using camel::core::type::Type;
using camel::core::type::TypeCode;
using ops::OpDef;
using ops::OpRegistry;

// ---- Values ---------------------------------------------------------------------------------

/// One element of any dtype the evaluator meets (f32, f64, int64, bool).
struct Num {
    TypeCode type = TypeCode::Float32;
    float f       = 0;
    double d      = 0;
    int64_t i     = 0; // int64; bool as 0 or 1
};

Num f32(float v) { return {TypeCode::Float32, v, 0, 0}; }
Num f64(double v) { return {TypeCode::Float64, 0, v, 0}; }
Num i64(int64_t v) { return {TypeCode::Int64, 0, 0, v}; }
Num boolean(bool v) { return {TypeCode::Bool, 0, 0, v ? 1 : 0}; }

bool isFloat(TypeCode t) { return t == TypeCode::Float32 || t == TypeCode::Float64; }

Num fromBits(uint64_t bits, TypeCode t) {
    switch (t) {
    case TypeCode::Float32:
        return f32(std::bit_cast<float>(static_cast<uint32_t>(bits)));
    case TypeCode::Float64:
        return f64(std::bit_cast<double>(bits));
    case TypeCode::Bool:
        return boolean(bits != 0);
    default:
        return i64(static_cast<int64_t>(bits));
    }
}

uint64_t bitsOf(const Num &n) {
    switch (n.type) {
    case TypeCode::Float32:
        return std::bit_cast<uint32_t>(n.f);
    case TypeCode::Float64:
        return std::bit_cast<uint64_t>(n.d);
    default:
        return static_cast<uint64_t>(n.i);
    }
}

bool isNan(const Num &n) {
    return (n.type == TypeCode::Float32 && std::isnan(n.f)) ||
           (n.type == TypeCode::Float64 && std::isnan(n.d));
}

/// The value converted as C++ converts it (the kernels' static_cast): rounding to nearest.
Num convert(const Num &n, TypeCode to) {
    if (n.type == to) {
        return n;
    }
    switch (to) {
    case TypeCode::Float32:
        return f32(n.type == TypeCode::Float64 ? static_cast<float>(n.d) : static_cast<float>(n.i));
    case TypeCode::Float64:
        return f64(n.type == TypeCode::Float32 ? static_cast<double>(n.f) : static_cast<double>(n.i));
    case TypeCode::Int64:
        return i64(n.type == TypeCode::Float32   ? static_cast<int64_t>(n.f)
                   : n.type == TypeCode::Float64 ? static_cast<int64_t>(n.d)
                                                 : n.i);
    case TypeCode::Bool:
        return boolean(isFloat(n.type) ? (n.type == TypeCode::Float32 ? n.f != 0 : n.d != 0) : n.i != 0);
    default:
        return n;
    }
}

std::string show(const Num &n) {
    switch (n.type) {
    case TypeCode::Float32:
        return std::format("{}f(0x{:08x})", n.f, bitsOf(n));
    case TypeCode::Float64:
        return std::format("{}(0x{:016x})", n.d, bitsOf(n));
    case TypeCode::Bool:
        return n.i ? "true" : "false";
    default:
        return std::to_string(n.i);
    }
}

/// IEEE 754-2019 maximum and minimum, written from the standard: a NaN on either side gives
/// the canonical quiet NaN, -0 < +0.
template <typename F> F ieeeMaximum(F a, F b) {
    if (std::isnan(a) || std::isnan(b)) {
        return std::numeric_limits<F>::quiet_NaN();
    }
    if (a == 0 && b == 0) {
        return std::signbit(a) ? b : a; // +0 when either is +0
    }
    return a > b ? a : b;
}
template <typename F> F ieeeMinimum(F a, F b) {
    if (std::isnan(a) || std::isnan(b)) {
        return std::numeric_limits<F>::quiet_NaN();
    }
    if (a == 0 && b == 0) {
        return std::signbit(a) ? a : b; // -0 when either is -0
    }
    return a < b ? a : b;
}

Num maximumOf(const Num &a, const Num &b, bool max) {
    switch (a.type) {
    case TypeCode::Float32:
        return f32(max ? ieeeMaximum(a.f, b.f) : ieeeMinimum(a.f, b.f));
    case TypeCode::Float64:
        return f64(max ? ieeeMaximum(a.d, b.d) : ieeeMinimum(a.d, b.d));
    case TypeCode::Bool:
        return boolean(max ? (a.i | b.i) : (a.i & b.i));
    default:
        return i64(max ? std::max(a.i, b.i) : std::min(a.i, b.i));
    }
}

int64_t wrapAdd(int64_t a, int64_t b) {
    return static_cast<int64_t>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
}
int64_t wrapMul(int64_t a, int64_t b) {
    return static_cast<int64_t>(static_cast<uint64_t>(a) * static_cast<uint64_t>(b));
}

// ---- Tensors as data ----------------------------------------------------------------------

/// A tensor (or a number operand, rank 0) as values in row-major order.
struct Data {
    TypeCode dtype = TypeCode::Float32;
    StaticShape shape;
    std::vector<Num> values;

    size_t numel() const { return values.size(); }
};

int64_t numelOf(const StaticShape &shape) {
    int64_t n = 1;
    for (const std::optional<int64_t> &e : shape) {
        // The evaluator's own shapes are read off the arrays it holds: every extent is a value.
        n *= *e;
    }
    return n;
}

/// The row-major index of `coords` in `shape`, or -1 when a coordinate is out of range.
int64_t linearIndex(const StaticShape &shape, std::span<const int64_t> coords) {
    if (coords.size() != shape.size()) {
        return -1;
    }
    int64_t index = 0;
    for (size_t a = 0; a < shape.size(); ++a) {
        if (coords[a] < 0 || coords[a] >= *shape[a]) {
            return -1;
        }
        index = index * *shape[a] + coords[a];
    }
    return index;
}

Data dataOf(const Array *t) {
    Data d{t->dtype(), knownShape(t->shapeVector()), {}};
    d.values.reserve(t->numel());
    for (uint64_t k = 0; k < t->numel(); ++k) {
        switch (t->dtype()) {
        case TypeCode::Float32:
            d.values.push_back(f32(t->dataAs<float>()[k]));
            break;
        case TypeCode::Int64:
            d.values.push_back(i64(t->dataAs<int64_t>()[k]));
            break;
        default:
            d.values.push_back(boolean(t->dataAs<bool_t>()[k] != 0));
            break;
        }
    }
    return d;
}

Array *tensorOf(const Data &d) {
    Array *t = createTensor(d.dtype, extentsOf(d.shape), mm::autoSpace());
    for (size_t k = 0; k < d.values.size(); ++k) {
        switch (d.dtype) {
        case TypeCode::Float32:
            t->dataAs<float>()[k] = d.values[k].f;
            break;
        case TypeCode::Int64:
            t->dataAs<int64_t>()[k] = d.values[k].i;
            break;
        default:
            t->dataAs<bool_t>()[k] = d.values[k].i ? 1 : 0;
            break;
        }
    }
    return t;
}

// ---- Pseudo-random cases --------------------------------------------------------------------

/// splitmix64: deterministic, so the plan's output is the same on every run.
class Rng {
  public:
    explicit Rng(uint64_t seed) : state_(seed) {}
    uint64_t next() {
        uint64_t z = (state_ += 0x9e3779b97f4a7c15u);
        z          = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9u;
        z          = (z ^ (z >> 27)) * 0x94d049bb133111ebu;
        return z ^ (z >> 31);
    }
    /// Uniform in [lo, hi].
    int64_t range(int64_t lo, int64_t hi) {
        return lo + static_cast<int64_t>(next() % static_cast<uint64_t>(hi - lo + 1));
    }
    bool chance(int percent) { return range(0, 99) < percent; }

  private:
    uint64_t state_;
};

/// A float: often an ordinary value with a short mantissa or a full one, sometimes a special.
float randomFloat(Rng &rng) {
    if (rng.chance(15)) {
        static const float specials[] = {
            std::numeric_limits<float>::quiet_NaN(),
            -std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity(),
            0.0f,
            -0.0f,
            1e-40f,  // subnormal
            -3e-42f, // subnormal
            FLT_MAX,
            -FLT_MIN,
        };
        return specials[rng.range(0, std::size(specials) - 1)];
    }
    // Full 24-bit mantissas and more, so that sums and products round.
    const double mantissa = static_cast<double>(rng.range(-(int64_t{1} << 30), int64_t{1} << 30)) / (1 << 28);
    return static_cast<float>(rng.chance(30) ? mantissa * std::pow(2.0, rng.range(-30, 30)) : mantissa);
}

Data randomData(Rng &rng, TypeCode dtype, const StaticShape &shape, int64_t intRange = 1 << 20) {
    Data d{dtype, shape, {}};
    for (int64_t k = 0; k < numelOf(shape); ++k) {
        switch (dtype) {
        case TypeCode::Float32:
            d.values.push_back(f32(randomFloat(rng)));
            break;
        case TypeCode::Int64:
            d.values.push_back(i64(rng.range(-intRange, intRange)));
            break;
        default:
            d.values.push_back(boolean(rng.chance(50)));
            break;
        }
    }
    return d;
}

StaticShape randomShape(Rng &rng, size_t rank, int64_t maxExtent = 4) {
    StaticShape s;
    for (size_t a = 0; a < rank; ++a) {
        // Mostly small; extent 1 and the empty extent 0 now and then.
        s.push_back(rng.chance(8) ? 0 : rng.chance(20) ? 1 : rng.range(2, maxExtent));
    }
    return s;
}

/// A shape that broadcasts to `out`: some leading axes dropped, some axes set to 1.
StaticShape broadcastFrom(Rng &rng, const StaticShape &out) {
    const size_t drop = out.empty() ? 0 : static_cast<size_t>(rng.range(0, static_cast<int64_t>(out.size())));
    StaticShape s(out.begin() + static_cast<int64_t>(rng.chance(50) ? drop : 0), out.end());
    for (std::optional<int64_t> &e : s) {
        if (rng.chance(30)) {
            e = 1;
        }
    }
    return s;
}

/// One argument of a call.
struct Arg {
    enum class Kind { Tensor, Number, Int, Bool, Ints } kind = Kind::Tensor;
    Data data;              // Tensor
    double number = 0;      // Number (a Camel float)
    int64_t integer = 0;    // Int
    bool flag       = false;
    std::vector<int64_t> ints;

    static Arg tensor(Data d) { return Arg{.kind = Kind::Tensor, .data = std::move(d)}; }
    static Arg num(double v) { return Arg{.kind = Kind::Number, .number = v}; }
    static Arg integerArg(int64_t v) { return Arg{.kind = Kind::Int, .integer = v}; }
    static Arg boolArg(bool v) { return Arg{.kind = Kind::Bool, .flag = v}; }
    static Arg intsArg(std::vector<int64_t> v) { return Arg{.kind = Kind::Ints, .ints = std::move(v)}; }

    Type *type() const {
        switch (kind) {
        case Kind::Tensor:
            return ops::tensorOf(data.dtype, data.shape);
        case Kind::Number:
            // A Camel float literal takes the element type of what it meets (design principles
            // 5.1), and a tensor operator never sees one of another type: the tool hands the
            // kernel and the declaration the same argument type, f32, so neither side reads the
            // literal at a width the other does not have.
            return Type::Float32();
        case Kind::Int:
            return Type::Int64();
        case Kind::Bool:
            return Type::Bool();
        case Kind::Ints:
            return camel::core::type::ArrayType::create(Type::Int64());
        }
        return nullptr;
    }

    slot_t slot() const {
        switch (kind) {
        case Kind::Tensor:
            // The value is read by the type this same entry reports in `type()`: a rank-0 tensor
            // argument is the element, and the kernel boxes it back (support.cpp's tensorArg).
            return ops::wrap(type(), tensorOf(data));
        case Kind::Number:
            // One float width in the whole tool: the slot is an f32 value because `type()` says
            // the argument is f32, and the kernel reads the slot by the type it is given.
            return camel::core::rtdata::toSlot(static_cast<float>(number));
        case Kind::Int:
            return camel::core::rtdata::toSlot(integer);
        case Kind::Bool:
            return camel::core::rtdata::toSlot(flag);
        case Kind::Ints: {
            ::Array *array = ::Array::create(
                mm::autoSpace(),
                camel::core::type::ArrayType::create(camel::core::type::Type::Int64()),
                ints.size());
            for (size_t k = 0; k < ints.size(); ++k) {
                array->set(k, static_cast<camel::core::rtdata::Int64>(ints[k]));
            }
            return camel::core::rtdata::toSlot(array);
        }
        }
        return NullSlot;
    }

    /// The operand as the evaluator reads it: a tensor, or a number as the kernels take it (a
    /// rank-0 f32 operand, ScalarOperand); nullopt for a constant argument.
    std::optional<Data> operand() const {
        if (kind == Kind::Tensor) {
            return data;
        }
        if (kind == Kind::Number) {
            return Data{TypeCode::Float32, {}, {f32(static_cast<float>(number))}};
        }
        return std::nullopt;
    }

    std::string text() const {
        switch (kind) {
        case Kind::Tensor:
            return std::format("{}{}", data.dtype == TypeCode::Float32 ? "f32" : data.dtype == TypeCode::Int64 ? "i64" : "bool",
                               ops::formatShape(data.shape));
        case Kind::Number:
            return std::format("{}", number);
        case Kind::Int:
            return std::to_string(integer);
        case Kind::Bool:
            return flag ? "true" : "false";
        case Kind::Ints:
            return ops::formatShape(ints);
        }
        return "?";
    }
};

/// The arguments of a call in the operand positions the kernel reads them from.
class ArgList final : public ArgsView {
  public:
    explicit ArgList(const std::vector<Arg> &args) {
        for (const Arg &a : args) {
            types_.push_back(a.type());
            slots_.push_back(a.slot());
        }
    }
    size_t size() const override { return slots_.size(); }
    slot_t slot(size_t i) const override { return slots_[i]; }
    void setSlot(size_t i, slot_t v) override { slots_[i] = v; }
    TypeCode code(size_t i) const override { return types_[i]->code(); }
    Type *type(size_t i) const override { return types_[i]; }
    const std::vector<Type *> &types() const { return types_; }

  private:
    std::vector<Type *> types_;
    std::vector<slot_t> slots_;
};

// ---- The scalar functions of the elementwise operators -------------------------------------

/// The value of an elementwise operator at one point, from its numeric declaration's
/// algorithm (semantics.cpp). exp and log are bounded: their reference is the f64 function,
/// returned as an f64 value that the comparison holds to the declared ulp bound.
std::optional<Num> pointwise(std::string_view op, const std::vector<Num> &v) {
    const Num &a = v[0];
    if (v.size() == 2) {
        const Num &b = v[1];
        if (a.type == TypeCode::Float32) {
            if (op == "add") return f32(a.f + b.f);
            if (op == "subtract") return f32(a.f - b.f);
            if (op == "multiply") return f32(a.f * b.f);
            if (op == "divide") return f32(a.f / b.f);
            if (op == "maximum") return f32(ieeeMaximum(a.f, b.f));
            if (op == "minimum") return f32(ieeeMinimum(a.f, b.f));
        } else if (a.type == TypeCode::Int64) {
            // Division produces f32: both operands converted first.
            if (op == "divide") return f32(convert(a, TypeCode::Float32).f / convert(b, TypeCode::Float32).f);
            if (op == "add") return i64(wrapAdd(a.i, b.i));
            if (op == "subtract") return i64(wrapAdd(a.i, wrapMul(-1, b.i)));
            if (op == "multiply") return i64(wrapMul(a.i, b.i));
            if (op == "maximum") return i64(std::max(a.i, b.i));
            if (op == "minimum") return i64(std::min(a.i, b.i));
        }
        return std::nullopt;
    }
    if (a.type == TypeCode::Float32) {
        if (op == "neg") return f32(-a.f);
        if (op == "abs") return f32(std::bit_cast<float>(std::bit_cast<uint32_t>(a.f) & 0x7fffffffu));
        if (op == "relu") return f32(ieeeMaximum(a.f, 0.0f));
        if (op == "sqrt") return f32(std::sqrt(a.f));
        if (op == "rsqrt") return f32(1.0f / std::sqrt(a.f));
        if (op == "exp") return f64(std::exp(static_cast<double>(a.f)));
        if (op == "log") return f64(std::log(static_cast<double>(a.f)));
    } else if (a.type == TypeCode::Int64) {
        if (op == "neg") return i64(wrapMul(-1, a.i));
        if (op == "abs") return i64(a.i < 0 ? -a.i : a.i);
        if (op == "relu") return i64(std::max<int64_t>(a.i, 0));
    }
    return std::nullopt;
}

/// The operators whose Unique write copies the one value it reads.
bool isMove(std::string_view op) {
    for (std::string_view m : {"transpose", "permute", "reshape", "flatten", "unsqueeze",
                               "broadcast_like", "expand_axis", "sum_to"}) {
        if (op == m) {
            return true;
        }
    }
    return false;
}

// ---- Reduction laws -----------------------------------------------------------------------

/// acc combined with one contribution (its values converted to the accumulator type first).
Num combine(const sem::ReductionLaw &law, const Num &acc, const std::vector<Num> &c) {
    const TypeCode t = law.accumulator;
    const Num x      = convert(c[0], t);
    switch (law.kind) {
    case sem::LawKind::Add:
        if (t == TypeCode::Float32) return f32(acc.f + x.f);
        if (t == TypeCode::Float64) return f64(acc.d + x.d);
        return i64(wrapAdd(acc.i, x.i));
    case sem::LawKind::Mul:
        if (t == TypeCode::Float32) return f32(acc.f * x.f);
        if (t == TypeCode::Float64) return f64(acc.d * x.d);
        return i64(wrapMul(acc.i, x.i));
    case sem::LawKind::MulAdd: {
        const Num y = convert(c[1], t);
        if (t == TypeCode::Float32) return f32(acc.f + x.f * y.f);
        if (t == TypeCode::Float64) return f64(acc.d + x.d * y.d);
        return i64(wrapAdd(acc.i, wrapMul(x.i, y.i)));
    }
    case sem::LawKind::FmaChain: {
        const Num y = convert(c[1], t);
        if (t == TypeCode::Float32) return f32(std::fmaf(x.f, y.f, acc.f));
        return f64(std::fma(x.d, y.d, acc.d));
    }
    case sem::LawKind::Max:
    case sem::LawKind::Min: {
        Num r  = maximumOf(acc, x, law.kind == sem::LawKind::Max);
        r.type = t;
        return r;
    }
    default:
        return acc; // not declared by any operator evaluated here
    }
}

/// The folded value of the contributions to one result element, nullopt for a fault.
std::optional<Num> fold(const sem::ReductionLaw &law, const std::vector<std::vector<Num>> &contributions) {
    if (contributions.empty() && law.empty == sem::LawEmpty::Fault) {
        return std::nullopt;
    }
    if (law.kind == sem::LawKind::ArgMax || law.kind == sem::LawKind::ArgMin) {
        // The position, in canonical order, of the first contribution equal (bit for bit, a
        // NaN as NaN) to the IEEE maximum (minimum) of them all, written from the standard's
        // operation rather than from the kernel's comparison (design principles 7.7).
        const bool max = law.kind == sem::LawKind::ArgMax;
        Num extreme    = convert(contributions[0][0], law.accumulator);
        for (size_t k = 1; k < contributions.size(); ++k) {
            extreme = maximumOf(extreme, convert(contributions[k][0], law.accumulator), max);
        }
        for (size_t k = 0; k < contributions.size(); ++k) {
            const Num v = convert(contributions[k][0], law.accumulator);
            if (isNan(extreme) ? isNan(v) : bitsOf(v) == bitsOf(extreme)) {
                return i64(static_cast<int64_t>(k));
            }
        }
        return std::nullopt; // unreachable: the extreme is one of the contributions
    }
    Num acc;
    size_t first = 0;
    if (law.init == sem::LawInit::Constant) {
        acc = fromBits(law.initBits, law.accumulator);
    } else {
        acc   = convert(contributions[0][0], law.accumulator);
        first = 1;
    }
    for (size_t k = first; k < contributions.size(); ++k) {
        acc = combine(law, acc, contributions[k]);
    }
    if (law.finalize == sem::LawFinalize::DivideByCount) {
        acc = f64(convert(acc, TypeCode::Float64).d / static_cast<double>(contributions.size()));
    }
    return convert(acc, law.result);
}

// ---- Evaluating an instance ----------------------------------------------------------------

struct Evaluation {
    bool fault = false;
    Data result;
    size_t points = 0;
    std::vector<std::string> unsound; // soundness violations, in words
};

/**
 * Executes `inst` on the operands `operands` (nullopt for a constant argument). `outShape` is
 * the result's shape (the type rule's), `moveDtype` the result dtype of a move (the kernel's).
 */
Evaluation evaluate(
    std::string_view op, const sem::IndexedInstance &inst, const sem::OperatorSemantics &s,
    const std::vector<std::optional<Data>> &operands, const StaticShape &outShape, TypeCode moveDtype) {
    Evaluation ev;
    const auto extents = inst.extents();
    const auto params  = inst.paramValues();
    if (!extents) {
        ev.unsound.push_back("the instance has extents that are not known");
        return ev;
    }
    const sem::ReductionLaw *law = inst.law == sem::kNoLaw ? nullptr : &s.laws[inst.law];
    // An empty reduction faults even when the result has no elements (LawEmpty::Fault).
    if (law && law->empty == sem::LawEmpty::Fault) {
        int64_t reduced = 1;
        for (size_t d = 0; d < extents->size(); ++d) {
            if (inst.domain.dims[d].role == sem::DimRole::Reduction) {
                reduced *= (*extents)[d];
            }
        }
        if (reduced == 0) {
            ev.fault = true;
            return ev;
        }
    }
    const int64_t outCount = numelOf(outShape);
    std::vector<std::optional<Num>> written(static_cast<size_t>(outCount));
    std::vector<std::vector<std::vector<Num>>> contributions(static_cast<size_t>(outCount));
    std::vector<int64_t> point(extents->size(), 0);
    const bool empty = std::ranges::any_of(*extents, [](int64_t e) { return e <= 0; });
    for (bool more = !empty; more;) {
        ++ev.points;
        if (sem::holds(inst, inst.domain.guard, point)) {
            std::vector<Num> values;
            bool excluded = false;
            for (size_t i = 0; i < inst.norm.size(); ++i) {
                const sem::Access &a = inst.norm[i];
                if (a.kind == sem::AccessKind::None) {
                    continue;
                }
                if (a.kind != sem::AccessKind::Affine || !operands[i]) {
                    ev.unsound.push_back(std::format("operand {} is read but cannot be evaluated", i));
                    return ev;
                }
                std::vector<int64_t> coords;
                for (sem::ExprId e : a.coords) {
                    coords.push_back(inst.exprs.evaluate(e, point, params));
                }
                const bool valid = sem::holds(inst, a.valid, point);
                const int64_t at = linearIndex(operands[i]->shape, coords);
                if (!valid) {
                    switch (a.onInvalid) {
                    case sem::OnInvalid::Unreachable:
                        ev.unsound.push_back(std::format("operand {}: a point declared unreachable is reached", i));
                        return ev;
                    case sem::OnInvalid::Fault:
                        ev.fault = true;
                        return ev;
                    case sem::OnInvalid::Excluded:
                        excluded = true;
                        continue;
                    case sem::OnInvalid::Padding:
                        values.push_back(fromBits(a.padding, operands[i]->dtype));
                        continue;
                    }
                }
                if (at < 0) {
                    ev.unsound.push_back(std::format("operand {}: coordinates out of range", i));
                    return ev;
                }
                values.push_back(operands[i]->values[static_cast<size_t>(at)]);
            }
            if (sem::holds(inst, inst.output.valid, point)) {
                std::vector<int64_t> out;
                for (sem::ExprId e : inst.output.coords) {
                    out.push_back(inst.exprs.evaluate(e, point, params));
                }
                const int64_t at = linearIndex(outShape, out);
                if (at < 0) {
                    ev.unsound.push_back("a write lands outside the result");
                    return ev;
                }
                if (inst.output.mode == sem::WriteMode::Unique) {
                    if (written[static_cast<size_t>(at)]) {
                        ev.unsound.push_back("a unique write is written twice");
                        return ev;
                    }
                    std::optional<Num> v =
                        isMove(op) ? std::optional(convert(values.at(0), moveDtype)) : pointwise(op, values);
                    if (!v) {
                        ev.unsound.push_back("no scalar function for this operator and dtype");
                        return ev;
                    }
                    written[static_cast<size_t>(at)] = v;
                } else if (!excluded) {
                    contributions[static_cast<size_t>(at)].push_back(std::move(values));
                }
            }
        }
        // The next point in canonical (lexicographic) order.
        more = false;
        for (size_t d = point.size(); d-- > 0;) {
            if (++point[d] < (*extents)[d]) {
                more = true;
                break;
            }
            point[d] = 0;
        }
    }
    ev.result.shape = outShape;
    for (int64_t k = 0; k < outCount; ++k) {
        if (inst.output.mode == sem::WriteMode::Unique) {
            if (!written[static_cast<size_t>(k)]) {
                ev.unsound.push_back("an element of the result is never written");
                return ev;
            }
            ev.result.values.push_back(*written[static_cast<size_t>(k)]);
        } else {
            const auto v = fold(*law, contributions[static_cast<size_t>(k)]);
            if (!v) {
                ev.fault = true;
                return ev;
            }
            ev.result.values.push_back(*v);
        }
    }
    ev.result.dtype = ev.result.values.empty()
                          ? (law ? law->result : moveDtype)
                          : ev.result.values.front().type;
    return ev;
}

/// The distance of a float from the f64 reference in units of the last place of the reference
/// rounded to f32; specials must be exact. `flushes`: results below the normal range may be +0.
bool withinUlps(float got, double ref, float bound, bool flushes) {
    const float r = static_cast<float>(ref);
    if (std::isnan(r)) {
        return std::isnan(got);
    }
    if (std::isinf(r) || r == 0) {
        return std::bit_cast<uint32_t>(got) == std::bit_cast<uint32_t>(r) ||
               (flushes && got == 0 && std::fabs(ref) < FLT_MIN);
    }
    if (flushes && std::fabs(r) < FLT_MIN && got == 0) {
        return true;
    }
    // One ulp of the f32 result: the gap above its magnitude.
    const double ulp =
        static_cast<double>(std::nextafter(std::fabs(r), INFINITY)) - std::fabs(static_cast<double>(r));
    return std::fabs(static_cast<double>(got) - ref) <= bound * ulp;
}

/// Why the kernel's result differs from the evaluation, empty when they agree.
std::string compare(const Data &kernel, const Data &eval, const sem::OperatorSemantics &s) {
    if (kernel.shape != eval.shape) {
        return std::format("shape {} vs {}", ops::formatShape(kernel.shape), ops::formatShape(eval.shape));
    }
    const bool bounded = s.numeric.cls == sem::NumericClass::Bounded;
    if (!bounded && !eval.values.empty() && kernel.dtype != eval.dtype) {
        // Name both element types: which side reads a scalar operand at which type is the whole
        // question when this fires, and the line is the only place it is reported.
        const auto name = [](TypeCode t) {
            switch (t) {
            case TypeCode::Float32: return "f32";
            case TypeCode::Float64: return "f64";
            case TypeCode::Int64: return "i64";
            case TypeCode::Bool: return "bool";
            default: return "other";
            }
        };
        return std::format("dtype differs (kernel {}, declaration {})", name(kernel.dtype), name(eval.dtype));
    }
    for (size_t k = 0; k < kernel.values.size(); ++k) {
        const Num &a = kernel.values[k], &b = eval.values[k];
        if (bounded) {
            if (!withinUlps(a.f, b.d, s.numeric.ulpBound, !s.numeric.specials.subnormals)) {
                return std::format("element {}: {} vs reference {}", k, show(a), show(b));
            }
            continue;
        }
        if (bitsOf(a) == bitsOf(b)) {
            continue;
        }
        // Two NaNs agree unless the operator is exact, whose NaN is the canonical one.
        if (isNan(a) && isNan(b) && s.numeric.cls != sem::NumericClass::Exact) {
            continue;
        }
        return std::format("element {}: kernel {} vs declaration {}", k, show(a), show(b));
    }
    return {};
}

// ---- Cases ----------------------------------------------------------------------------------

using Case = std::vector<Arg>;
using Generator = std::function<Case(Rng &)>;

TypeCode pickDtype(Rng &rng, bool withBool = true) {
    const int64_t k = rng.range(0, withBool ? 4 : 3);
    return k <= 1 ? TypeCode::Float32 : k <= 3 ? TypeCode::Int64 : TypeCode::Bool;
}

Generator binaryCases() {
    return [](Rng &rng) {
        const TypeCode t     = rng.chance(75) ? TypeCode::Float32 : TypeCode::Int64;
        const StaticShape out = randomShape(rng, static_cast<size_t>(rng.range(0, 4)));
        Case c;
        for (int side = 0; side < 2; ++side) {
            if (t == TypeCode::Float32 && rng.chance(10)) {
                c.push_back(Arg::num(static_cast<double>(randomFloat(rng))));
            } else {
                c.push_back(Arg::tensor(randomData(rng, t, rng.chance(50) ? out : broadcastFrom(rng, out))));
            }
        }
        if (c[0].kind != Arg::Kind::Tensor && c[1].kind != Arg::Kind::Tensor) {
            c[0] = Arg::tensor(randomData(rng, t, out));
        }
        return c;
    };
}

Generator unaryCases(bool integers) {
    return [integers](Rng &rng) {
        const TypeCode t = integers && rng.chance(30) ? TypeCode::Int64 : TypeCode::Float32;
        return Case{Arg::tensor(randomData(rng, t, randomShape(rng, static_cast<size_t>(rng.range(0, 4)))))};
    };
}

/// exp: the unary cases, and now and then inputs whose results are subnormal or round to +0
/// (x in [-105, -86]: e^x from 2^-151 to 2^-124), which must be within the bound too.
Generator expCases() {
    return [](Rng &rng) {
        if (!rng.chance(40)) {
            return unaryCases(false)(rng);
        }
        Data d{TypeCode::Float32, randomShape(rng, static_cast<size_t>(rng.range(1, 2)), 9), {}};
        for (int64_t k = 0; k < numelOf(d.shape); ++k) {
            d.values.push_back(f32(static_cast<float>(-86.0 - static_cast<double>(rng.range(0, 1 << 20)) * 19.0 / (1 << 20))));
        }
        return Case{Arg::tensor(std::move(d))};
    };
}

Generator transposeCases() {
    return [](Rng &rng) {
        return Case{Arg::tensor(randomData(rng, pickDtype(rng), randomShape(rng, static_cast<size_t>(rng.range(2, 4)))))};
    };
}

Generator permuteCases() {
    return [](Rng &rng) {
        const size_t rank = static_cast<size_t>(rng.range(0, 4));
        std::vector<int64_t> perm(rank);
        for (size_t a = 0; a < rank; ++a) {
            perm[a] = static_cast<int64_t>(a);
        }
        for (size_t a = rank; a > 1; --a) {
            std::swap(perm[a - 1], perm[static_cast<size_t>(rng.range(0, static_cast<int64_t>(a) - 1))]);
        }
        for (int64_t &p : perm) {
            if (rng.chance(30)) {
                p -= static_cast<int64_t>(rank); // the same axis, counted from the end
            }
        }
        return Case{Arg::tensor(randomData(rng, pickDtype(rng), randomShape(rng, rank))), Arg::intsArg(perm)};
    };
}

/// A reshape target for `s`: the element count split into factors, one of them maybe -1.
std::vector<int64_t> reshapeTarget(Rng &rng, const StaticShape &s) {
    int64_t n = numelOf(s);
    std::vector<int64_t> target;
    if (n == 0) {
        target = {0, rng.range(1, 3)};
    } else {
        while (n > 1 && target.size() < 3) {
            int64_t f = rng.range(1, std::min<int64_t>(n, 4));
            while (n % f != 0) {
                --f;
            }
            if (f > 1) {
                target.push_back(f);
                n /= f;
            } else if (rng.chance(50)) {
                break;
            }
            if (rng.chance(20)) {
                target.push_back(1);
            }
        }
        target.push_back(n);
    }
    if (rng.chance(30) && numelOf(s) > 0) {
        target[static_cast<size_t>(rng.range(0, static_cast<int64_t>(target.size()) - 1))] = -1;
    }
    return target;
}

Generator reshapeCases() {
    return [](Rng &rng) {
        const StaticShape s = randomShape(rng, static_cast<size_t>(rng.range(0, 4)));
        return Case{Arg::tensor(randomData(rng, pickDtype(rng), s)), Arg::intsArg(reshapeTarget(rng, s))};
    };
}

Generator flattenCases() {
    return [](Rng &rng) {
        const size_t rank = static_cast<size_t>(rng.range(1, 4));
        Case c{Arg::tensor(randomData(rng, pickDtype(rng), randomShape(rng, rank)))};
        if (rng.chance(70)) {
            c.push_back(Arg::integerArg(rng.range(-static_cast<int64_t>(rank), static_cast<int64_t>(rank))));
        }
        return c;
    };
}

Generator unsqueezeCases() {
    return [](Rng &rng) {
        const size_t rank = static_cast<size_t>(rng.range(0, 3));
        return Case{Arg::tensor(randomData(rng, pickDtype(rng), randomShape(rng, rank))),
                    Arg::integerArg(rng.range(-static_cast<int64_t>(rank) - 1, static_cast<int64_t>(rank)))};
    };
}

Generator broadcastLikeCases() {
    return [](Rng &rng) {
        const StaticShape like = randomShape(rng, static_cast<size_t>(rng.range(0, 4)));
        // `broadcast_like` is ::<T>(g: T[..], like: T[..]) (gradient_ops.cpp), and its first
        // operand may also be a number, which the kernel reads as the f32 scalar operand of the
        // tensor it is broadcast to: the generator writes one element type for the whole call,
        // since two of them is the mixed caller to fix, not a call the operator accepts.
        const TypeCode t = rng.chance(15) ? TypeCode::Float32 : pickDtype(rng, false);
        Case c;
        if (t == TypeCode::Float32 && rng.chance(20)) {
            c.push_back(Arg::num(static_cast<double>(randomFloat(rng))));
        } else {
            c.push_back(Arg::tensor(randomData(rng, t, broadcastFrom(rng, like))));
        }
        c.push_back(Arg::tensor(randomData(rng, t, like)));
        return c;
    };
}

Generator expandAxisCases() {
    return [](Rng &rng) {
        const size_t rank      = static_cast<size_t>(rng.range(1, 4));
        const StaticShape like = randomShape(rng, rank);
        const int64_t axis     = rng.range(-static_cast<int64_t>(rank), static_cast<int64_t>(rank) - 1);
        const size_t a         = static_cast<size_t>(axis < 0 ? axis + static_cast<int64_t>(rank) : axis);
        const bool keep        = rng.chance(50);
        StaticShape g;
        for (size_t k = 0; k < rank; ++k) {
            if (k == a) {
                if (keep) {
                    g.push_back(1);
                }
            } else {
                g.push_back(rng.chance(20) ? 1 : like[k]);
            }
        }
        Case c{Arg::tensor(randomData(rng, TypeCode::Float32, g)), Arg::tensor(randomData(rng, TypeCode::Float32, like)),
               Arg::integerArg(axis)};
        if (keep || rng.chance(50)) {
            c.push_back(Arg::boolArg(keep));
        }
        return c;
    };
}

Generator reduceAllCases() {
    return [](Rng &rng) {
        const TypeCode t = pickDtype(rng);
        // Large integers now and then: their f64 sum rounds.
        const int64_t range = rng.chance(30) ? int64_t{1} << 60 : int64_t{1} << 20;
        return Case{Arg::tensor(randomData(rng, t, randomShape(rng, static_cast<size_t>(rng.range(0, 4))), range))};
    };
}

Generator reduceAxisCases() {
    return [](Rng &rng) {
        const size_t rank  = static_cast<size_t>(rng.range(1, 4));
        const int64_t axis = rng.range(-static_cast<int64_t>(rank), static_cast<int64_t>(rank) - 1);
        Case c{Arg::tensor(randomData(rng, pickDtype(rng), randomShape(rng, rank))), Arg::integerArg(axis)};
        if (rng.chance(60)) {
            c.push_back(Arg::boolArg(rng.chance(50)));
        }
        return c;
    };
}

Generator sumToCases() {
    return [](Rng &rng) {
        const StaticShape g = randomShape(rng, static_cast<size_t>(rng.range(0, 4)));
        const StaticShape like = rng.chance(20) ? g : broadcastFrom(rng, g);
        // `sum_to` (and `broadcast_like`) are ::<T>(g: T[..], like: T[..]) (gradient_ops.cpp):
        // the two operands are one element type, and a generator that writes two is the mixed
        // caller the rule says to fix rather than a call the operator accepts. `like`'s
        // elements are not read, so only its element type has to agree.
        const TypeCode t = pickDtype(rng);
        return Case{Arg::tensor(randomData(rng, t, g)), Arg::tensor(randomData(rng, t, like))};
    };
}

Generator matmulCases() {
    return [](Rng &rng) {
        const TypeCode t = rng.chance(75) ? TypeCode::Float32 : TypeCode::Int64;
        const size_t ra = static_cast<size_t>(rng.range(1, 4)), rb = static_cast<size_t>(rng.range(1, 4));
        const int64_t M = rng.range(1, 5), N = rng.range(1, 5);
        const int64_t K = rng.chance(10) ? 0 : rng.range(1, 9);
        const StaticShape batch = randomShape(rng, std::max(ra, rb) > 2 ? std::max(ra, rb) - 2 : 0, 3);
        StaticShape a, b;
        if (ra == 1) {
            a = {K};
        } else {
            a = ra > 2 ? broadcastFrom(rng, batch) : StaticShape{};
            a.resize(std::min(a.size(), ra - 2));
            a.push_back(M);
            a.push_back(K);
        }
        if (rb == 1) {
            b = {K};
        } else {
            b = rb > 2 ? broadcastFrom(rng, batch) : StaticShape{};
            b.resize(std::min(b.size(), rb - 2));
            b.push_back(K);
            b.push_back(N);
        }
        return Case{Arg::tensor(randomData(rng, t, a)), Arg::tensor(randomData(rng, t, b))};
    };
}

Generator poolCases() {
    return [](Rng &rng) {
        const int64_t kernel = rng.range(1, 3);
        const int64_t pad    = rng.range(0, kernel);
        StaticShape s = {rng.range(1, 2), rng.range(1, 2), rng.range(1, 6), rng.range(1, 6)};
        s[2]          = std::max(*s[2], kernel - 2 * pad);
        s[3]          = std::max(*s[3], kernel - 2 * pad);
        Case c{Arg::tensor(randomData(rng, TypeCode::Float32, s)), Arg::integerArg(kernel)};
        if (rng.chance(70)) {
            c.push_back(Arg::integerArg(rng.range(1, 3)));
            if (rng.chance(70)) {
                c.push_back(Arg::integerArg(pad));
            }
        }
        return c;
    };
}

// ---- Running ------------------------------------------------------------------------------

struct Totals {
    size_t cases = 0, agree = 0, faults = 0, points = 0, unsound = 0, mismatches = 0, missing = 0;
};

/// Runs `count` cases of the operator `uri` and prints its line.
void runOperator(
    std::ostream &os, camel::core::context::Context &ctx, std::string_view uri, const Generator &gen,
    size_t count, uint64_t seed, Totals &total) {
    const OpDef *def = OpRegistry::instance().find(uri);
    auto &table      = camel::core::OperatorTable::instance();
    const auto id    = table.lookup(uri);
    const sem::OperatorSemantics *s = id ? table.semantics(*id) : nullptr;
    if (!def || !s || !s->indexing) {
        os << "EVAL op=" << uri << " undeclared\n";
        ++total.missing;
        return;
    }
    const std::string_view op = uri.substr(uri.find(':') + 1);
    Rng rng(seed);
    Totals t;
    size_t reported = 0;
    const sem::IndexedInstance *lastInstance = nullptr; // printed with the first mismatch
    auto mismatch   = [&](const Case &c, const std::string &why) {
        ++t.mismatches;
        if (reported++ < 4) {
            std::string args;
            for (const Arg &a : c) {
                args += (args.empty() ? "" : ", ") + a.text();
            }
            os << "EVAL_MISMATCH op=" << uri << " (" << args << "): " << why << "\n";
            if (reported == 1 && lastInstance) {
                os << sem::toString(*lastInstance, s);
            }
        }
    };
    for (size_t n = 0; n < count; ++n) {
        const Case c = gen(rng);
        mm::NoCollectionScope noCollection; // the arguments are raw pointers for this case
        ArgList norm(c);
        const std::vector<Arg> noArgs;
        ArgList with(noArgs);
        std::vector<camel::core::type::StaticArg> statics;
        std::vector<std::optional<Data>> operands;
        for (size_t i = 0; i < c.size(); ++i) {
            const bool constant = c[i].kind != Arg::Kind::Tensor;
            statics.push_back(constant ? camel::core::type::StaticArg(norm.slot(i))
                                       : camel::core::type::StaticArg());
            operands.push_back(c[i].operand());
        }
        ++t.cases;
        // The kernel, as the program runs it.
        // The result type the type rule gives (it may reject the call: nullptr).
        Type *resultType = nullptr;
        try {
            const ops::StaticFacts facts(norm.types(), statics);
            if (auto inferred = def->infer(ops::InferContext(norm.types(), facts.constants, facts.partials))) {
                resultType = *inferred;
            }
        } catch (const std::exception &) {
        }
        // The view carries the call's static result type, which is what every scheduler hands a
        // kernel (`node->dataType()`, `header->dataType`, the frame's `typeAt(bc.result)`): this is
        // the type `ops::wrap` asks to decide between the element and the array box, so a kernel run
        // without it answers a rank-zero result as a box, and the reading below -- which follows the
        // type rule and takes the element -- would take the box's pointer for the element. A real
        // call therefore never disagreed with itself here; only this tool did, by not asking.
        norm.setResultType(resultType);
        std::optional<Data> kernelResult;
        bool kernelFault = false;
        try {
            const slot_t r = def->kernel(with, norm, ctx);
            // A number comes back in the slot and a tensor comes back as a pointer to an array. An
            // array states its elements' code, so the code alone puts an f32 array and an f32
            // scalar together; the shape separates them, and a rank-zero result states an empty
            // one. The whole-tensor `sum` and `mean` answer in their elements' own type (design
            // principles 7.3) and are such results; the test used to be `code() == Float64`, which
            // read them as a pointer and ended the process.
            bool numberResult = false;
            if (resultType != nullptr) {
                const TensorFacts facts = tensorFactsOf(resultType);
                numberResult = facts.isStaticShape() && facts.shape->empty() &&
                               (resultType->code() == TypeCode::Float32 ||
                                resultType->code() == TypeCode::Float64 ||
                                resultType->code() == TypeCode::Int64 ||
                                resultType->code() == TypeCode::Bool);
            }
            if (numberResult && resultType->code() == TypeCode::Float32) {
                kernelResult = Data{TypeCode::Float32, {}, {f32(camel::core::rtdata::fromSlot<float>(r))}};
            } else if (numberResult && resultType->code() == TypeCode::Float64) {
                kernelResult = Data{TypeCode::Float64, {}, {f64(camel::core::rtdata::fromSlot<double>(r))}};
            } else if (numberResult && resultType->code() == TypeCode::Int64) {
                kernelResult = Data{TypeCode::Int64, {}, {i64(camel::core::rtdata::fromSlot<int64_t>(r))}};
            } else if (numberResult && resultType->code() == TypeCode::Bool) {
                kernelResult = Data{TypeCode::Bool, {}, {boolean(camel::core::rtdata::fromSlot<bool>(r))}};
            } else {
                kernelResult = dataOf(camel::core::rtdata::fromSlot<Array *>(r));
            }
        } catch (...) {
            kernelFault = true;
        }
        // The declaration.
        const sem::InstanceContext cx{.norm = norm.types(), .statics = statics};
        const auto inst = sem::instantiate(*id, cx);
        lastInstance    = inst ? &*inst : nullptr;
        {
            // The same question asked as a back end holding the call's InferContext asks it.
            const ops::StaticFacts facts(norm.types(), statics);
            const auto viaDef =
                ops::instantiate(*def, ops::InferContext(norm.types(), facts.constants, facts.partials));
            if (inst.has_value() != viaDef.has_value() ||
                (inst && sem::toString(*inst) != sem::toString(*viaDef))) {
                mismatch(c, "ops::instantiate and sem::instantiate differ");
                continue;
            }
        }
        if (!inst) {
            if (kernelFault) {
                ++t.faults; // the call is rejected: no instance is the right answer
                ++t.agree;
            } else {
                mismatch(c, "no instance for a call the kernel accepts");
            }
            continue;
        }
        // The result's shape: the kernel's, or when it faults the type rule's (the declaration
        // must then fault too). A result of rank zero states its shape like any other: its type
        // is the element type, so asking a type predicate for the array spelling is what left
        // the declaration unasked. The facts answer for a rank-0 result as for a rank-2 one, and
        // a scalar argument that is not such a result (sum and mean to a number) has the same
        // empty shape it would fall back to.
        StaticShape outShape;
        if (kernelResult) {
            outShape = kernelResult->shape;
        } else if (const TensorFacts facts = tensorFactsOf(resultType); facts.isStaticShape()) {
            outShape = *facts.shape;
        } else if (!resultType || resultType->code() != TypeCode::Float64) {
            mismatch(c, "the kernel faults, the declaration gives an instance");
            continue;
        }
        const TypeCode moveDtype   = kernelResult ? kernelResult->dtype : TypeCode::Float32;
        Evaluation ev;
        try {
            ev = evaluate(op, *inst, *s, operands, outShape, moveDtype);
        } catch (const std::exception &e) {
            ev.unsound.push_back(std::string("evaluation failed: ") + e.what());
        }
        t.points += ev.points;
        if (!ev.unsound.empty()) {
            ++t.unsound;
            mismatch(c, "unsound: " + ev.unsound.front());
            continue;
        }
        if (kernelFault || ev.fault) {
            if (kernelFault && ev.fault) {
                ++t.faults;
                ++t.agree;
            } else {
                mismatch(c, kernelFault ? "the kernel faults, the declaration does not" : "the declaration faults, the kernel does not");
            }
            continue;
        }
        if (const std::string why = compare(*kernelResult, ev.result, *s); !why.empty()) {
            mismatch(c, why);
            continue;
        }
        ++t.agree;
    }
    os << std::format(
        "EVAL op={} cases={} agree={} faults={} points={} unsound={}\n",
        uri, t.cases, t.agree, t.faults, t.points, t.unsound);
    total.cases += t.cases;
    total.agree += t.agree;
    total.faults += t.faults;
    total.points += t.points;
    total.unsound += t.unsound;
    total.mismatches += t.mismatches;
}

// ---- Facts that give no instance ----------------------------------------------------------

/// Prints whether `uri` has an instance for operands of these types and constants.
void query(
    std::ostream &os, std::string_view check, std::string_view uri, const std::vector<Type *> &types,
    const std::vector<std::optional<Arg>> &constants) {
    auto &table   = camel::core::OperatorTable::instance();
    const auto id = table.lookup(uri);
    mm::NoCollectionScope noCollection;
    std::vector<camel::core::type::StaticArg> statics;
    for (const auto &c : constants) {
        statics.push_back(c ? camel::core::type::StaticArg(c->slot()) : camel::core::type::StaticArg());
    }
    statics.resize(types.size());
    const auto inst = id ? sem::instantiate(*id, sem::InstanceContext{.norm = types, .statics = statics}) : std::nullopt;
    os << "QUERY " << check << " " << (inst ? "instance" : "none") << "\n";
    if (inst && check.starts_with("show_")) {
        os << sem::toString(*inst, table.semantics(*id));
    }
}

void queries(std::ostream &os) {
    const auto T = [](std::optional<StaticShape> shape, TypeCode t = TypeCode::Float32) -> Type * {
        return ops::tensorOf(t, std::move(shape));
    };
    Type *const integer = Type::Int64();
    Type *const ints    = camel::core::type::ArrayType::create(Type::Int64());
    // Q9: an extent that may or may not be 1 against another: no instance until specialized.
    query(os, "broadcast_unknown_both", "tensor:add", {T(StaticShape{std::nullopt, 3}), T(StaticShape{std::nullopt, 3})}, {});
    query(os, "broadcast_unknown_vs_known", "tensor:add", {T(StaticShape{std::nullopt, 3}), T(StaticShape{4, 3})}, {});
    query(os, "show_broadcast_unknown_alone", "tensor:add", {T(StaticShape{std::nullopt, 3}), T(StaticShape{3})}, {});
    query(os, "show_broadcast_unknown_vs_one", "tensor:add", {T(StaticShape{std::nullopt, 3}), T(StaticShape{1, 3})}, {});
    query(os, "unknown_rank", "tensor:add", {T(std::nullopt), T(StaticShape{3})}, {});
    query(os, "show_relu_unknown_extents", "tensor:relu", {T(StaticShape{std::nullopt, std::nullopt})}, {});
    query(os, "sum_axis_axis_not_constant", "tensor:sum_axis", {T(StaticShape{2, 3}), integer}, {});
    query(os, "show_sum_axis_unknown_extents", "tensor:sum_axis", {T(StaticShape{std::nullopt, std::nullopt, 4}), integer},
          {std::nullopt, Arg::integerArg(-2)});
    query(os, "permute_not_constant", "tensor:permute", {T(StaticShape{2, 3}), ints}, {});
    // The core's own slice node, `:op/slice_sub`, is the one node `t[a:b:s]` and
    // `slice(t, axis, a, b, s)` both build, and the tensor module publishes its declaration
    // (ops/indexing.cpp). A bound is a position from 0 to the axis' length, so a negative one is
    // a fault whatever the extent is, and it is the declaration that has to say so.
    query(os, "slice_unknown_extent", ":op/slice_sub", {T(StaticShape{std::nullopt, 3}), integer, integer, integer},
          {std::nullopt, Arg::integerArg(0), Arg::integerArg(0), Arg::integerArg(2)});
    query(os, "slice_unknown_extent_negative", ":op/slice_sub",
          {T(StaticShape{std::nullopt, 3}), integer, integer, integer},
          {std::nullopt, Arg::integerArg(0), Arg::integerArg(-1), Arg::integerArg(2)});
    query(os, "show_slice", ":op/slice_sub",
          {T(StaticShape{std::nullopt, 7}), integer, integer, integer, integer},
          {std::nullopt, Arg::integerArg(1), Arg::integerArg(1), Arg::integerArg(-1), Arg::integerArg(2)});
    query(os, "show_slice_written", ":op/slice_sub",
          {T(StaticShape{std::nullopt, 7}), integer, integer, integer, integer},
          {std::nullopt, Arg::integerArg(1), Arg::integerArg(1), Arg::integerArg(5), Arg::integerArg(2)});
    query(os, "slice_out_of_range", ":op/slice_sub", {T(StaticShape{5}), integer, integer, integer},
          {std::nullopt, Arg::integerArg(0), Arg::integerArg(2), Arg::integerArg(6)});
    query(os, "slice_backward", ":op/slice_sub",
          {T(StaticShape{5}), integer, integer, integer, integer},
          {std::nullopt, Arg::integerArg(0), Arg::integerArg(4), Arg::integerArg(0), Arg::integerArg(-2)});
    query(os, "slice_step_zero", ":op/slice_sub", {T(StaticShape{5}), integer, integer, integer, integer},
          {std::nullopt, Arg::integerArg(0), Arg::integerArg(0), Arg::integerArg(5), Arg::integerArg(0)});
    query(os, "reshape_unknown_input", "tensor:reshape", {T(StaticShape{std::nullopt, 4}), ints},
          {std::nullopt, Arg::intsArg({4, -1})});
    // The target is written out, so the result's shape is fixed while the input's first extent is
    // open: the instance is the target's, and the element count the operator compares is stated as
    // a read that is valid only where the input holds it, a fault elsewhere.
    query(os, "show_reshape_open_first_extent", "tensor:reshape", {T(StaticShape{std::nullopt}), ints},
          {std::nullopt, Arg::intsArg({1})});
    query(os, "show_reshape", "tensor:reshape", {T(StaticShape{2, 6}), ints}, {std::nullopt, Arg::intsArg({3, -1})});
    query(os, "show_reshape_unit_axes", "tensor:reshape", {T(StaticShape{2, 1, 6}), ints},
          {std::nullopt, Arg::intsArg({1, 2, 6})});
    query(os, "show_matmul_batched", "tensor:matmul", {T(StaticShape{std::nullopt, 2, 3, 4}), T(StaticShape{4, std::nullopt})}, {});
    query(os, "matmul_batch_q9", "tensor:matmul", {T(StaticShape{std::nullopt, 3, 4}), T(StaticShape{std::nullopt, 4, 5})}, {});
    query(os, "matmul_mixed_dtypes", "tensor:matmul", {T(StaticShape{3, 4}), T(StaticShape{4, 5}, TypeCode::Int64)}, {});
    query(os, "show_transpose", "tensor:transpose", {T(StaticShape{std::nullopt, 3, 5})}, {});
    query(os, "show_sum_to", "tensor:sum_to", {T(StaticShape{4, 2, 3}), T(StaticShape{2, 1})}, {});
    query(os, "show_max_pool2d", "nn:max_pool2d", {T(StaticShape{1, 2, 5, 5}), integer, integer, integer},
          {std::nullopt, Arg::integerArg(3), Arg::integerArg(2), Arg::integerArg(1)});
    query(os, "show_avg_pool2d_unknown_extents", "nn:avg_pool2d", {T(StaticShape{1, 2, std::nullopt, std::nullopt}), integer},
          {std::nullopt, Arg::integerArg(2)});
}

} // namespace

const camel::runtime::Program *
IndexEvalTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    queries(os);
    struct Entry {
        std::string_view uri;
        Generator gen;
        size_t count;
    };
    // No slice entry: the module's slice kernel is gone with its operator, and the core's own
    // slice node (the one `t[a:b:s]` and `slice(t, axis, a, b, s)` both build) has no module
    // definition to compare a declaration with. The queries below and the core's own cases cover
    // that declaration instead.
    const std::vector<Entry> entries = {
        {"tensor:add", binaryCases(), 120},
        {"tensor:subtract", binaryCases(), 80},
        {"tensor:multiply", binaryCases(), 80},
        {"tensor:divide", binaryCases(), 80},
        {"tensor:maximum", binaryCases(), 80},
        {"tensor:minimum", binaryCases(), 80},
        {"tensor:neg", unaryCases(true), 40},
        {"tensor:abs", unaryCases(true), 40},
        {"tensor:relu", unaryCases(true), 40},
        {"tensor:sqrt", unaryCases(false), 40},
        {"tensor:rsqrt", unaryCases(false), 40},
        {"tensor:exp", expCases(), 60},
        {"tensor:log", unaryCases(false), 40},
        {"tensor:transpose", transposeCases(), 60},
        {"tensor:permute", permuteCases(), 80},
        {"tensor:reshape", reshapeCases(), 100},
        {"tensor:flatten", flattenCases(), 60},
        {"tensor:unsqueeze", unsqueezeCases(), 60},
        {"tensor:broadcast_like", broadcastLikeCases(), 80},
        {"tensor:expand_axis", expandAxisCases(), 80},
        {"tensor:sum", reduceAllCases(), 80},
        {"tensor:mean", reduceAllCases(), 80},
        {"tensor:sum_axis", reduceAxisCases(), 120},
        {"tensor:mean_axis", reduceAxisCases(), 120},
        {"tensor:max_axis", reduceAxisCases(), 120},
        {"tensor:min_axis", reduceAxisCases(), 120},
        {"tensor:argmax_axis", reduceAxisCases(), 120},
        {"tensor:argmin_axis", reduceAxisCases(), 120},
        {"tensor:sum_to", sumToCases(), 100},
        {"tensor:matmul", matmulCases(), 150},
        {"nn:max_pool2d", poolCases(), 80},
        {"nn:avg_pool2d", poolCases(), 80},
    };
    Totals total;
    uint64_t seed = 0x5eed;
    for (const Entry &e : entries) {
        runOperator(os, *context_, e.uri, e.gen, e.count, seed++, total);
    }
    os << std::format(
        "EVAL total operators={} cases={} agree={} faults={} points={} unsound={} mismatches={} "
        "undeclared={}\n",
        entries.size(), total.cases, total.agree, total.faults, total.points, total.unsound,
        total.mismatches, total.missing);
    return program;
}

} // namespace camel::tensor::passes
