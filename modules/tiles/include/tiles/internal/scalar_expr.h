// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Typed immutable scalar arithmetic; explicit rounding boundaries and host evaluation.

#pragma once
#include "tiles/dtype.h"
#include "tiles/internal/index_expr.h"
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace camel::tiles::kernel {
// Physical register/shared-memory types; tensor plans retain their own DType.
//
// A register holds a number of one of these widths. `F16` and `BF16` are the two narrow elements a
// plan can name: a register of one holds the element's own sixteen bits, and arithmetic is *not*
// done in it -- a narrow element is computed with in f32 and rounded once when it is stored
// (design principles 7.3), which is why `apply` refuses a narrow operand and the body spells the
// two conversions as explicit casts (`cast` widens exactly; a cast into a narrow type rounds once).
struct ScalarType {
    enum Kind { F32, F64, F16, BF16, I32, U32, I64 } kind;
    ScalarType(Kind kind);
    ScalarType(DType type);
    bool operator==(const ScalarType &) const = default;
};
enum class ScalarOp {
    Neg,
    Abs,
    Exp,
    Log,
    Sqrt,
    Add,
    Subtract,
    Multiply,
    Divide,
    Maximum,
    Minimum,
    Fma,
    Cast,
    Bitcast,
    BitAnd,
    BitOr,
    BitXor,
    LogicalShiftRight
};
struct ScalarValue {
    ScalarType type;
    uint64_t bits;
    static ScalarValue f32(float value);
    static ScalarValue f64(double value);
    static ScalarValue i32(int32_t value);
    static ScalarValue u32(uint32_t value);
    static ScalarValue i64(int64_t value);
    // A narrow value holds the element's sixteen bits: the f32 rounded once, ties to even.
    static ScalarValue f16(float value);
    static ScalarValue bf16(float value);
    int32_t asI32() const;
    uint32_t asU32() const;
    int64_t asI64() const;
    float asF32() const;
    double asF64() const;
    // The number widened into a register wide enough to compute with: an f32 or f64 as itself (an
    // f64 narrowed to f32 is the one rounding that conversion is), and a narrow element exactly,
    // because its bits are the top or the low half of that f32.
    float asWideF32() const;
    double asWideF64() const;
};
class ScalarExpr {
    struct Node;
    std::shared_ptr<const Node> node;
    explicit ScalarExpr(std::shared_ptr<const Node> node);

  public:
    static ScalarExpr literal(ScalarValue value);
    static ScalarExpr symbol(std::string name, ScalarType type);
    // A loop index read into an i64 register: the value of an index, not a number computed from
    // it. Only i64 can hold a full index, and the result takes no part in arithmetic (i64
    // arithmetic remains refused), so a position can be stored and nothing else can be derived.
    static ScalarExpr fromIndex(IndexExpr index, ScalarType type);
    static ScalarExpr apply(ScalarOp op, std::vector<ScalarExpr> inputs);
    static ScalarExpr cast(ScalarType type, ScalarExpr input);
    static ScalarExpr bitcast(ScalarType type, ScalarExpr input);
    ScalarType type() const;
    // The index this register reads, or nullptr for every other expression.
    const IndexExpr *index() const;
    ScalarValue evaluate(const std::unordered_map<std::string, ScalarValue> &bindings) const;
    ScalarExpr substitute(const std::string &name, const ScalarExpr &replacement) const;
    std::unordered_map<std::string, ScalarType> symbols() const;
    std::string cuda() const;
};
enum class ScalarCompare { Equal, NotEqual, Less, LessEqual };
// Same-type comparisons: integers remain exact; IEEE NaNs compare unequal and
// either floating signed zero equals zero. Mixed types require an explicit cast.
struct ScalarPredicate {
    ScalarCompare compare;
    ScalarExpr lhs, rhs;
    bool evaluate(const std::unordered_map<std::string, ScalarValue> &bindings) const;
    std::string cuda() const;
};
// Build the arithmetic body of a lowered elementwise instruction, using symbols a/b.
ScalarExpr elementwiseExpression(std::string_view op, DType type = DType::F32);
std::string scalarCudaPreamble();
} // namespace camel::tiles::kernel
