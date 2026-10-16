/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Type checking, reference evaluation and CUDA rendering of scalar expression DAGs.
#include "tiles/internal/scalar_expr.h"
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace camel::tiles::kernel {
namespace {
void checkType(DType type) {
    if (type != DType::F32 && type != DType::F64)
        throw std::invalid_argument("invalid scalar type");
}
size_t arity(ScalarOp op) {
    switch (op) {
    case ScalarOp::Neg:
    case ScalarOp::Abs:
    case ScalarOp::Exp:
    case ScalarOp::Log:
    case ScalarOp::Sqrt:
        return 1;
    case ScalarOp::Add:
    case ScalarOp::Subtract:
    case ScalarOp::Multiply:
    case ScalarOp::Divide:
    case ScalarOp::Maximum:
    case ScalarOp::Minimum:
        return 2;
    case ScalarOp::Fma:
        return 3;
    default:
        throw std::invalid_argument("invalid scalar operation; use cast for conversions");
    }
}
template <class T, class U> T extrema(T a, T b, bool maximum) {
    if (std::isnan(a) || std::isnan(b))
        return std::bit_cast<T>(U(sizeof(T) == 4 ? 0x7fc00000ULL : 0x7ff8000000000000ULL));
    if (a == b)
        return std::bit_cast<T>(
            maximum ? std::bit_cast<U>(a) & std::bit_cast<U>(b)
                    : std::bit_cast<U>(a) | std::bit_cast<U>(b));
    return maximum ? (a < b ? b : a) : (b < a ? b : a);
}
template <class T> T compute(ScalarOp op, const std::vector<T> &x) {
    switch (op) {
    case ScalarOp::Neg:
        return -x[0];
    case ScalarOp::Abs:
        return std::fabs(x[0]);
    case ScalarOp::Exp:
        return std::exp(x[0]);
    case ScalarOp::Log:
        return std::log(x[0]);
    case ScalarOp::Sqrt:
        return std::sqrt(x[0]);
    case ScalarOp::Add:
        return x[0] + x[1];
    case ScalarOp::Subtract:
        return x[0] - x[1];
    case ScalarOp::Multiply:
        return x[0] * x[1];
    case ScalarOp::Divide:
        return x[0] / x[1]; // Physical IEEE instruction; graph faults are separate.
    case ScalarOp::Fma:
        return std::fma(x[0], x[1], x[2]);
    case ScalarOp::Maximum:
    case ScalarOp::Minimum:
        if constexpr (sizeof(T) == 4)
            return extrema<T, uint32_t>(x[0], x[1], op == ScalarOp::Maximum);
        else
            return extrema<T, uint64_t>(x[0], x[1], op == ScalarOp::Maximum);
    default:
        throw std::invalid_argument("invalid scalar evaluation operation");
    }
}
} // namespace
ScalarValue ScalarValue::f32(float value) { return {DType::F32, std::bit_cast<uint32_t>(value)}; }
ScalarValue ScalarValue::f64(double value) { return {DType::F64, std::bit_cast<uint64_t>(value)}; }
float ScalarValue::asF32() const {
    if (type != DType::F32 || bits > UINT32_MAX)
        throw std::invalid_argument("expected scalar f32 bits");
    return std::bit_cast<float>(static_cast<uint32_t>(bits));
}
double ScalarValue::asF64() const {
    if (type != DType::F64)
        throw std::invalid_argument("expected scalar f64 bits");
    return std::bit_cast<double>(bits);
}
struct ScalarExpr::Node {
    enum class Kind { Literal, Symbol, Operation } kind;
    DType type;
    ScalarValue value{DType::F32, 0};
    std::string name;
    ScalarOp op = ScalarOp::Neg;
    std::vector<ScalarExpr> inputs;
};
ScalarExpr::ScalarExpr(std::shared_ptr<const Node> n) : node(std::move(n)) {}
ScalarExpr ScalarExpr::literal(ScalarValue value) {
    checkType(value.type);
    if (value.type == DType::F32)
        (void)value.asF32();
    return ScalarExpr(
        std::make_shared<Node>(
            Node{.kind = Node::Kind::Literal, .type = value.type, .value = value}));
}
ScalarExpr ScalarExpr::symbol(std::string name, DType type) {
    checkType(type);
    auto initial = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    };
    if (name.empty() || !initial(name[0]))
        throw std::invalid_argument("invalid scalar symbol");
    for (char c : name)
        if (!initial(c) && !(c >= '0' && c <= '9'))
            throw std::invalid_argument("invalid scalar symbol");
    return ScalarExpr(
        std::make_shared<Node>(
            Node{.kind = Node::Kind::Symbol, .type = type, .name = std::move(name)}));
}
ScalarExpr ScalarExpr::apply(ScalarOp op, std::vector<ScalarExpr> inputs) {
    if (inputs.size() != arity(op))
        throw std::invalid_argument("scalar operation arity mismatch");
    const auto type = inputs[0].type();
    for (const auto &input : inputs)
        if (input.type() != type)
            throw std::invalid_argument("mixed scalar types require explicit casts");
    return ScalarExpr(
        std::make_shared<Node>(Node{
            .kind   = Node::Kind::Operation,
            .type   = type,
            .op     = op,
            .inputs = std::move(inputs)}));
}
ScalarExpr ScalarExpr::cast(DType type, ScalarExpr input) {
    checkType(type);
    if (input.type() == type)
        return input;
    return ScalarExpr(
        std::make_shared<Node>(Node{
            .kind   = Node::Kind::Operation,
            .type   = type,
            .op     = ScalarOp::Cast,
            .inputs = {std::move(input)}}));
}
DType ScalarExpr::type() const { return node->type; }
ScalarValue
ScalarExpr::evaluate(const std::unordered_map<std::string, ScalarValue> &bindings) const {
    if (std::fegetround() != FE_TONEAREST)
        throw std::runtime_error("scalar reference requires round-to-nearest mode");
    if (node->kind == Node::Kind::Literal)
        return node->value;
    if (node->kind == Node::Kind::Symbol) {
        const auto found = bindings.find(node->name);
        if (found == bindings.end())
            throw std::runtime_error("unbound scalar symbol " + node->name);
        if (found->second.type != type())
            throw std::runtime_error("scalar binding type mismatch");
        if (type() == DType::F32)
            (void)found->second.asF32();
        return found->second;
    }
    if (node->op == ScalarOp::Cast) {
        const auto value = node->inputs[0].evaluate(bindings);
        return type() == DType::F32 ? ScalarValue::f32(static_cast<float>(value.asF64()))
                                    : ScalarValue::f64(static_cast<double>(value.asF32()));
    }
    // Every recursive result crosses an explicit IEEE bit representation; a
    // multiply/add pair cannot accidentally become an FMA in the host compiler.
    if (type() == DType::F32) {
        std::vector<float> inputs;
        for (const auto &input : node->inputs)
            inputs.push_back(input.evaluate(bindings).asF32());
        return ScalarValue::f32(compute(node->op, inputs));
    }
    std::vector<double> inputs;
    for (const auto &input : node->inputs)
        inputs.push_back(input.evaluate(bindings).asF64());
    return ScalarValue::f64(compute(node->op, inputs));
}
ScalarExpr ScalarExpr::substitute(const std::string &name, const ScalarExpr &replacement) const {
    if (node->kind == Node::Kind::Symbol && node->name == name) {
        if (type() != replacement.type())
            throw std::invalid_argument("scalar substitution type mismatch");
        return replacement;
    }
    if (node->kind != Node::Kind::Operation)
        return *this;
    auto copy = std::make_shared<Node>(*node);
    for (auto &input : copy->inputs)
        input = input.substitute(name, replacement);
    return ScalarExpr(std::move(copy));
}
std::unordered_map<std::string, DType> ScalarExpr::symbols() const {
    if (node->kind == Node::Kind::Symbol)
        return {{node->name, type()}};
    std::unordered_map<std::string, DType> result;
    for (const auto &input : node->inputs)
        for (const auto &[name, dtype] : input.symbols()) {
            auto [it, inserted] = result.emplace(name, dtype);
            if (!inserted && it->second != dtype)
                throw std::invalid_argument("inconsistent scalar symbol type");
        }
    return result;
}
std::string ScalarExpr::cuda() const {
    const bool f32 = type() == DType::F32;
    if (node->kind == Node::Kind::Literal)
        return f32 ? "__uint_as_float(" + std::to_string(node->value.bits) + "U)"
                   : "__longlong_as_double(" + std::to_string(node->value.bits) + "ULL)";
    if (node->kind == Node::Kind::Symbol)
        return node->name;
    std::vector<std::string> x;
    for (const auto &input : node->inputs)
        x.push_back(input.cuda());
    auto call = [&](const std::string &name) {
        std::string result = name + '(';
        for (size_t i = 0; i < x.size(); ++i) {
            if (i)
                result += ',';
            result += x[i];
        }
        return result + ')';
    };
    const std::string prefix = f32 ? "__f" : "__d";
    switch (node->op) {
    case ScalarOp::Add:
        return call(prefix + "add_rn");
    case ScalarOp::Subtract:
        return call(prefix + "sub_rn");
    case ScalarOp::Multiply:
        return call(prefix + "mul_rn");
    case ScalarOp::Divide:
        return call(prefix + "div_rn");
    case ScalarOp::Sqrt:
        return call(prefix + "sqrt_rn");
    case ScalarOp::Neg:
        return "(-(" + x[0] + "))";
    case ScalarOp::Abs:
        return call(f32 ? "fabsf" : "fabs");
    case ScalarOp::Exp:
        return call(f32 ? "expf" : "exp");
    case ScalarOp::Log:
        return call(f32 ? "logf" : "log");
    case ScalarOp::Fma:
        return call(f32 ? "fmaf" : "fma");
    case ScalarOp::Maximum:
        return call(f32 ? "tiles_maximum" : "tiles_maximum64");
    case ScalarOp::Minimum:
        return call(f32 ? "tiles_minimum" : "tiles_minimum64");
    case ScalarOp::Cast:
        return f32 ? call("__double2float_rn") : "((double)(" + x[0] + "))";
    }
    throw std::runtime_error("invalid scalar CUDA operation");
}
namespace {
const char *comparison(ScalarCompare op) {
    switch (op) {
    case ScalarCompare::Equal:
        return "==";
    case ScalarCompare::NotEqual:
        return "!=";
    case ScalarCompare::Less:
        return "<";
    case ScalarCompare::LessEqual:
        return "<=";
    }
    throw std::invalid_argument("invalid scalar comparison");
}
} // namespace
bool ScalarPredicate::evaluate(const std::unordered_map<std::string, ScalarValue> &bindings) const {
    if (lhs.type() != rhs.type())
        throw std::invalid_argument("scalar comparison type mismatch");
    const auto a = lhs.evaluate(bindings), b = rhs.evaluate(bindings);
    const double x = a.type == DType::F32 ? a.asF32() : a.asF64();
    const double y = b.type == DType::F32 ? b.asF32() : b.asF64();
    switch (compare) {
    case ScalarCompare::Equal:
        return x == y;
    case ScalarCompare::NotEqual:
        return x != y;
    case ScalarCompare::Less:
        return x < y;
    case ScalarCompare::LessEqual:
        return x <= y;
    }
    throw std::invalid_argument("invalid scalar comparison");
}
std::string ScalarPredicate::cuda() const {
    if (lhs.type() != rhs.type())
        throw std::invalid_argument("scalar comparison type mismatch");
    return "(" + lhs.cuda() + comparison(compare) + rhs.cuda() + ")";
}
ScalarExpr elementwiseExpression(std::string_view op, DType type) {
    const auto a = ScalarExpr::symbol("a", type), b = ScalarExpr::symbol("b", type);
    if (op == "cast_f32")
        return ScalarExpr::cast(DType::F32, ScalarExpr::symbol("a", DType::F64));
    if (op == "reshape" || op == "reindex")
        return a;
    for (const auto &[name, operation] : std::vector<std::pair<std::string_view, ScalarOp>>{
             {"add", ScalarOp::Add},
             {"subtract", ScalarOp::Subtract},
             {"multiply", ScalarOp::Multiply},
             {"divide", ScalarOp::Divide},
             {"maximum", ScalarOp::Maximum},
             {"minimum", ScalarOp::Minimum}})
        if (op == name)
            return ScalarExpr::apply(operation, {a, b});
    for (const auto &[name, operation] : std::vector<std::pair<std::string_view, ScalarOp>>{
             {"neg", ScalarOp::Neg},
             {"abs", ScalarOp::Abs},
             {"exp", ScalarOp::Exp},
             {"log", ScalarOp::Log},
             {"sqrt", ScalarOp::Sqrt}})
        if (op == name)
            return ScalarExpr::apply(operation, {a});
    if (op == "relu")
        return ScalarExpr::apply(
            ScalarOp::Maximum,
            {a,
             ScalarExpr::literal(type == DType::F32 ? ScalarValue::f32(0) : ScalarValue::f64(0))});
    if (op == "rsqrt")
        return ScalarExpr::apply(
            ScalarOp::Divide,
            {ScalarExpr::literal(type == DType::F32 ? ScalarValue::f32(1) : ScalarValue::f64(1)),
             ScalarExpr::apply(ScalarOp::Sqrt, {a})});
    throw std::invalid_argument("unknown scalar lowering " + std::string(op));
}
std::string scalarCudaPreamble() {
    return R"CUDA(
__device__ __forceinline__ float tiles_maximum(float a, float b) {
    if (a != a || b != b) return __uint_as_float(0x7fc00000U);
    if (a == b) return __uint_as_float(__float_as_uint(a) & __float_as_uint(b));
    return a < b ? b : a;
}
__device__ __forceinline__ float tiles_minimum(float a, float b) {
    if (a != a || b != b) return __uint_as_float(0x7fc00000U);
    if (a == b) return __uint_as_float(__float_as_uint(a) | __float_as_uint(b));
    return b < a ? b : a;
}
__device__ __forceinline__ double tiles_maximum64(double a, double b) {
    if (a != a || b != b) return __longlong_as_double(0x7ff8000000000000ULL);
    if (a == b) return __longlong_as_double(__double_as_longlong(a) & __double_as_longlong(b));
    return a < b ? b : a;
}
__device__ __forceinline__ double tiles_minimum64(double a, double b) {
    if (a != a || b != b) return __longlong_as_double(0x7ff8000000000000ULL);
    if (a == b) return __longlong_as_double(__double_as_longlong(a) | __double_as_longlong(b));
    return b < a ? b : a;
}
)CUDA";
}
} // namespace camel::tiles::kernel
