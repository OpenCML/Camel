// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The ONNX backend's operator capability registry.
 *
 * Maps Camel operator URIs ("tensor:matmul", "nn:conv2d", ...) to lowering
 * functions that emit equivalent ONNX nodes. The table belongs to the
 * backend, not to the operator definitions: frontend operators stay
 * independent of any export target, and the same table answers the
 * capability question "can this operator run on this backend at this
 * opset?" before any model is written.
 *
 * A lowering receives the call's arguments as exporter values (constants or
 * symbolic values) together with the statically inferred result facts and
 * the Camel result type, and returns the result value: usually a symbolic
 * value naming the emitted node's output, but a lowering may also answer
 * with a constant (for example `shape` of a tensor whose shape is static).
 *
 * Besides tensor and nn operators, the table covers the builtin scalar
 * operators (int/float arithmetic, comparisons, conversions) and array
 * indexing, so shape arithmetic on a dynamic batch dimension and branch
 * conditions computed from the input can be exported.
 */

#pragma once

#include "emitter.h"

#include "camel/core/index_semantics.h"

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace camel::onnx {

/// Arguments and helpers for one operator lowering.
class LowerContext {
  public:
    LowerContext(
        Emitter &emitter, std::string_view uri, std::span<const Value> args,
        tensor::ops::TensorFacts result, type::Type *resultType,
        std::optional<core::sem::IndexedInstance> instance = std::nullopt)
        : emitter_(emitter), uri_(uri), args_(args), result_(std::move(result)),
          resultType_(resultType), instance_(std::move(instance)) {}

    Emitter &emitter() const { return emitter_; }
    int64_t opset() const { return emitter_.opset(); }
    std::string_view uri() const { return uri_; }

    size_t size() const { return args_.size(); }
    bool has(size_t index) const { return index < args_.size(); }
    const Value &arg(size_t index) const { return args_[index]; }
    tensor::ops::TensorFacts facts(size_t index) const { return factsOf(args_[index]); }
    /// Rank of argument `index`; throws ExportError when unknown.
    size_t rank(size_t index) const;
    /// The call's index semantics instance (camel/core/index_semantics.h), from the operator's
    /// declaration (tensor::ops::instantiate); nullopt when it declares none or the facts do
    /// not fix it. A lowering reads the rules that belong to the operator from here (slice
    /// bounds, entry checks) instead of restating them.
    const std::optional<core::sem::IndexedInstance> &instance() const { return instance_; }

    /// Inferred facts of the result.
    const tensor::ops::TensorFacts &result() const { return result_; }
    /// Camel static type of the call's result (e.g. int for :op/add_l).
    type::Type *resultType() const { return resultType_; }

    /// The element type a tensor operator's arithmetic runs in: the result's when it is known,
    /// else the first operand's, else f32 — the float element type the operators declare they
    /// compute in, and the element type a bare `Tensor` model input is exported as. A scalar
    /// operand takes this type (the runtime kernel's rule: a scalar meeting a tensor is computed
    /// in the tensor's element type), so a graph never mixes a double scalar into float32 data.
    std::optional<type::TypeCode> elementDtype() const;

    /// Operand name of argument `index`, converted to `dtype` when given.
    std::string input(size_t index, std::optional<type::TypeCode> dtype = std::nullopt) const;

    /// Constant arguments; throw ExportError when the argument is not a constant of that kind.
    int64_t constInt(size_t index) const;
    int64_t constInt(size_t index, int64_t fallback) const;
    double constNumber(size_t index, double fallback) const;
    bool constBool(size_t index, bool fallback) const;
    std::string constString(size_t index) const;
    std::vector<int64_t> constInts(size_t index) const;

    /// Emits a single-output node and returns it as a value carrying the result facts.
    Value emit(
        std::string opType, std::vector<std::string> inputs,
        std::vector<Attribute> attributes = {}) const;
    /// Wraps an already emitted output name with the result facts.
    Value result(std::string name) const;
    /// Emits a single-output node producing the call's Camel scalar result.
    Value emitScalar(
        std::string opType, std::vector<std::string> inputs,
        std::vector<Attribute> attributes = {}) const;

  private:
    std::optional<tensor::ops::ConstArg> constArg(size_t index) const;
    [[noreturn]] void fail(size_t index, std::string_view expected) const;

    Emitter &emitter_;
    std::string_view uri_;
    std::span<const Value> args_;
    tensor::ops::TensorFacts result_;
    type::Type *resultType_;
    std::optional<core::sem::IndexedInstance> instance_;
};

/// Lowers one operator call.
using LowerFn = std::function<Value(const LowerContext &ctx)>;

struct Lowering {
    LowerFn lower;
    int64_t minOpset = 1; // oldest default-domain opset the lowering is valid for
};

class LoweringRegistry {
  public:
    static const LoweringRegistry &instance();

    /// Lowering for `uri` at `opset`, or nullptr when the backend cannot express it.
    const Lowering *find(std::string_view uri, int64_t opset) const;

    /// Every URI the backend can lower at `opset` (sorted), for capability reports.
    std::vector<std::string> supported(int64_t opset) const;

  private:
    LoweringRegistry();
    std::vector<std::pair<std::string, Lowering>> entries_;
};

/// Registers the lowerings of the gradient operators and optimizers (lowering_grad.cpp).
void addGradientLowerings(const std::function<void(std::string, LowerFn, int64_t)> &add);

} // namespace camel::onnx
