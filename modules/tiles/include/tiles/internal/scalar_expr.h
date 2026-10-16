/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
    Cast
};
struct ScalarValue {
    DType type;
    uint64_t bits;
    static ScalarValue f32(float value);
    static ScalarValue f64(double value);
    float asF32() const;
    double asF64() const;
};
class ScalarExpr {
    struct Node;
    std::shared_ptr<const Node> node;
    explicit ScalarExpr(std::shared_ptr<const Node> node);

  public:
    static ScalarExpr literal(ScalarValue value);
    static ScalarExpr symbol(std::string name, DType type);
    static ScalarExpr apply(ScalarOp op, std::vector<ScalarExpr> inputs);
    static ScalarExpr cast(DType type, ScalarExpr input);
    DType type() const;
    ScalarValue evaluate(const std::unordered_map<std::string, ScalarValue> &bindings) const;
    ScalarExpr substitute(const std::string &name, const ScalarExpr &replacement) const;
    std::unordered_map<std::string, DType> symbols() const;
    std::string cuda() const;
};
// Build the arithmetic body of a lowered elementwise instruction, using symbols a/b.
ScalarExpr elementwiseExpression(std::string_view op, DType type = DType::F32);
std::string scalarCudaPreamble();
} // namespace camel::tiles::kernel
