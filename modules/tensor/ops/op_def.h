// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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

/// The facts of a tensor-valued argument: `camel::tensor::TensorFacts` (modules/tensor/type.h),
/// where one definition serves the operators and the callers outside this module that used to
/// import it from here.
using TensorFacts = ::camel::tensor::TensorFacts;

class InferContext {
  public:
    /// `sourceLevel` says the inference resolves a call a program wrote, where design principles
    /// 5.1's literal rule has already run and a scalar of another type than the tensor's element
    /// type is a value to report. A pass that reads a graph again (the exporter, the tiles
    /// compiler) infers with the default: it must keep the tolerance the program was compiled
    /// under, because a generic function's body was inferred with open element types and a
    /// literal there could take no element type.
    InferContext(
        std::span<type::Type *const> types, std::span<const std::optional<ConstArg>> constants = {},
        std::span<const std::optional<PartialInts>> partials = {}, bool sourceLevel = false)
        : types_(types), constants_(constants), partials_(partials), sourceLevel_(sourceLevel) {}

    /// The element type the call states (`zeros::<f32>(shape)`) or takes from the position it
    /// stands in (`let x: f32[2, 3] = zeros([2, 3])`), for an operator whose element type is not
    /// in its arguments (`OpDef::elementFromCall`).
    std::optional<type::TypeCode> callElement() const { return callElement_; }
    InferContext &withCallElement(std::optional<type::TypeCode> element) {
        callElement_ = element;
        return *this;
    }

    size_t size() const { return types_.size(); }
    type::Type *type(size_t index) const { return types_[index]; }
    bool has(size_t index) const { return index < types_.size(); }
    bool sourceLevel() const { return sourceLevel_; }

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
    bool sourceLevel_ = false;
    std::optional<type::TypeCode> callElement_;
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
    std::string_view resultDoc; // result text for signatures, e.g. "float[..]"
    InferFn infer;
    operator_t kernel;
    OpTraits traits;
    /// The kernel takes the with-arguments of a call after the ordinary ones, as one list
    /// (`reshape<shape>(x)` is the kernel's `(x, shape)`): the operator is registered with that
    /// fact (OperatorTraits::withAfterNorm), the resolver and the index semantics read the joined
    /// list, and the kernel is wrapped by `positionally` (support.h). The graph keeps the
    /// with-argument on its own edge.
    bool withAfterNorm = false;
    /// With `withAfterNorm`: how many of the parameters are ordinary arguments of a call the
    /// program writes; the others are written in angle brackets. A call that writes them in
    /// parentheses is refused (the passes and the derivative rules build nodes with them in the
    /// ordinary list, and those are not calls).
    size_t ordinaryParams = 0;
    /// The element type of the result is not in the arguments: the call writes it
    /// (`zeros::<f32>(shape)`) or the position gives it (`let x: f32[2, 3] = zeros(...)`), and a
    /// call that does neither is refused (design principles 5.1: no default element type).
    bool elementFromCall = false;
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
