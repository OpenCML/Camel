/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Created: Sep. 28, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The single definition of a tensor-level operator.
 *
 * An OpDef bundles everything the system needs to know about one operator:
 * its URI, the names it is exported under, its parameter signature, its
 * static type inference, its runtime kernel, and its semantic traits. The
 * registry derives operator groups (type resolution), the executor's kernel
 * map, and trait queries from these definitions, so no operator is declared
 * in more than one place.
 *
 * Type inference is written once against InferContext and is used in two
 * settings: during compilation (argument types only) and during export or
 * graph rewriting (argument types plus the values of constant arguments, such
 * as a literal reshape target). Inference returns nullopt when the arguments
 * do not fit the operator (overload rejection) and throws ShapeError when they
 * fit but their known shapes conflict.
 */

#pragma once

#include "../type.h"
#include "camel/core/operator.h"
#include "camel/core/operator_semantics.h"
#include "vjp.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace camel::tensor::ops {

/// Accepted argument category for one parameter.
enum class ParamKind {
    Tensor,             // a tensor
    TensorLike,         // a tensor or a (nested) numeric array
    TensorOrScalar,     // a tensor or a numeric scalar
    TensorLikeOrScalar, // a tensor, a numeric array, or a numeric scalar
    Int,                // int / long
    Number,             // any numeric scalar
    Bool,
    String,
    Bytes,
    IntArray, // int[]
};

struct ParamSpec {
    std::string_view name;
    ParamKind kind;
    bool optional = false;
};

/// Value of a constant argument, when known (export time, graph rewrites).
using ConstArg = std::variant<int64_t, double, bool, std::string, std::vector<int64_t>>;

/// An int array of known length whose elements are known only in part: `[n, 3]` is
/// {nullopt, 3}. What inference sees of an array written in place with computed elements.
using PartialInts = std::vector<std::optional<int64_t>>;

/// What is statically known about a tensor-valued argument.
struct TensorFacts {
    std::optional<type::TypeCode> dtype;
    std::optional<StaticShape> shape;
};

class InferContext {
  public:
    InferContext(
        std::span<type::Type *const> types, std::span<const std::optional<ConstArg>> constants = {},
        std::span<const std::optional<PartialInts>> partials = {})
        : types_(types), constants_(constants), partials_(partials) {}

    size_t size() const { return types_.size(); }
    type::Type *type(size_t index) const { return types_[index]; }
    bool has(size_t index) const { return index < types_.size(); }

    /// Tensor facts of argument `index`: tensors report their type, numeric
    /// arrays report their element dtype (shape unknown), scalars are rank 0.
    TensorFacts facts(size_t index) const;

    std::optional<int64_t> constInt(size_t index) const;
    std::optional<double> constNumber(size_t index) const;
    std::optional<bool> constBool(size_t index) const;
    std::optional<std::string> constString(size_t index) const;
    std::optional<std::vector<int64_t>> constInts(size_t index) const;
    /// An int-array argument whose length is known: a constant array (every element known) or
    /// an array written in place (`[n, 3]`), whose computed elements are nullopt.
    std::optional<PartialInts> partialInts(size_t index) const;

  private:
    const ConstArg *constant(size_t index) const;

    std::span<type::Type *const> types_;
    std::span<const std::optional<ConstArg>> constants_;
    std::span<const std::optional<PartialInts>> partials_;
};

using InferFn = std::function<std::optional<type::Type *>(const InferContext &)>;

/// The index semantics of one call (camel/core/index_semantics.h): its iteration domain,
/// access maps and write map, for the facts inference sees; nullopt when they do not fix it.
/// Written beside the operator's inference (ops/indexing.cpp) and declared to the core
/// OperatorTable with the operator's semantics (OperatorSemantics::indexing).
using IndexFn = std::function<std::optional<core::sem::IndexedInstance>(const InferContext &)>;

/// The result as a constant when the argument types (and constant arguments) fix it, e.g. the
/// shape of a tensor whose type carries its shape; nullopt otherwise. Mirrored into the core
/// OperatorTable (setTypeFolder), which std::opt::fold consults.
using FoldFn = std::function<std::optional<slot_t>(const InferContext &, core::mm::IAllocator &)>;

/// Semantic properties used by exporters and, mirrored into the core OperatorTable on
/// registration, by generic graph passes. Operators are pure unless marked otherwise.
struct OpTraits {
    bool pure        = true;  // no side effects; result depends only on arguments
    bool elementwise = false; // output element i depends only on input elements i (after broadcast)
};

struct OpDef {
    std::string_view name;                 // URI suffix, e.g. "matmul" -> "tensor:matmul"
    std::vector<std::string_view> exports; // module-level names (overloads share a name)
    std::vector<ParamSpec> params;
    std::string_view resultDoc; // result text for signatures, e.g. "Tensor"
    InferFn infer;
    operator_t kernel;
    OpTraits traits;
    VjpFn vjp = nullptr; // reverse-mode rule; nullptr when the operator is not differentiable
    FoldFn foldFromTypes = nullptr; // constant result from argument types, when they fix it
    /// What the operator computes (camel/core/operator_semantics.h), declared to the core
    /// OperatorTable on registration; nullopt for an operator without a declaration (read as
    /// opaque). The key may be left empty: the registry fills it in from the URI, keeping the
    /// version. Filled in from one table by attachSemantics (semantics.cpp).
    std::optional<core::sem::OperatorSemantics> semantics;
    /// The index semantics (IndexFn), nullptr when not declared. The registry declares it as
    /// semantics->indexing; a back end that already holds an InferContext (Tiles) can call it
    /// through ops::instantiate (indexing.h). Its reduction laws are semantics->laws.
    IndexFn index = nullptr;

    /// Human-readable signature, e.g. "(lhs: Tensor | number, rhs: Tensor | number) => Tensor".
    std::string signature() const;
};

} // namespace camel::tensor::ops
