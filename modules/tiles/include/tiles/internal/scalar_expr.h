// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Typed immutable scalar arithmetic; explicit rounding boundaries and host evaluation.

#pragma once
#include "tiles/dtype.h"
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace camel::tiles::kernel {
// Physical register/shared-memory types; tensor plans retain their own DType.
struct ScalarType {
    enum Kind { F32, F64, I32, U32, I64 } kind;
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
    int32_t asI32() const;
    uint32_t asU32() const;
    int64_t asI64() const;
    float asF32() const;
    double asF64() const;
};
class ScalarExpr {
    struct Node;
    std::shared_ptr<const Node> node;
    explicit ScalarExpr(std::shared_ptr<const Node> node);

  public:
    static ScalarExpr literal(ScalarValue value);
    static ScalarExpr symbol(std::string name, ScalarType type);
    static ScalarExpr apply(ScalarOp op, std::vector<ScalarExpr> inputs);
    static ScalarExpr cast(ScalarType type, ScalarExpr input);
    static ScalarExpr bitcast(ScalarType type, ScalarExpr input);
    ScalarType type() const;
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
