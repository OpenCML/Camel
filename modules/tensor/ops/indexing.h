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
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The index semantics of the tensor operators (camel/core/index_semantics.h,
 * docs/design/op-semantics sections 5 and 7): their instantiation functions, written beside
 * shape inference and against the same facts (InferContext), and their reduction laws.
 *
 * An instantiation function sees what inference sees and must work for every rank the
 * operator accepts. It returns no instance whenever the facts do not fix the maps: an unknown
 * rank, an axis or a constant argument that is not constant, a broadcast where an extent may
 * or may not be 1 (design Q9), a call the kernel would reject. Extents that are not known stay
 * parameters without a value; a back end that specializes asks again with the shapes.
 *
 * IndexBuilder is the shared vocabulary: parameters for operand extents and constants, dims,
 * accesses, the broadcast rule. Other modules (nn's pooling) use it for their own operators.
 */

#pragma once

#include "op_def.h"

#include "camel/core/index_semantics.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace camel::tensor::ops {

/// Builds one IndexedInstance.
class IndexBuilder {
  public:
    using ExprId = core::sem::ExprId;

    core::sem::IndexedInstance instance;
    core::sem::ExprPool &x() { return instance.exprs; }

    /// The parameter shape(operand)[axis], named s<operand>_<axis>, with its value when `known`
    /// is an extent (>= 0). Asking twice gives the same parameter.
    ExprId extent(size_t operand, size_t axis, int64_t known);
    /// A constant argument: the scalar operand `operand`, or element `element` of an int array.
    ExprId constant(
        std::string name, size_t operand, int64_t value, std::optional<size_t> element = {});
    /// A scalar argument known only when the program runs (ParamSource::Runtime): its value is
    /// left unknown; a check on it states the legal interval.
    ExprId runtime(std::string name, size_t operand);
    /// Records a check on the value of `operand`, held by parameter `param`: it must lie in
    /// [lower, upper], or the operator faults (IndexedInstance::checks).
    void check(size_t operand, ExprId param, ExprId lower, ExprId upper);
    /// A parameter computed from the others, with its value when known.
    ExprId derived(std::string name, std::optional<int64_t> value, ExprId definition);

    /// Appends a dim d<k> of the given extent and role; returns its expression.
    ExprId dim(ExprId extent, core::sem::DimRole role = core::sem::DimRole::Parallel);

    /// Requires `lhs rel rhs` of the parameters.
    void require(ExprId lhs, core::sem::Constraint::Rel rel, ExprId rhs);

    /// An affine access with these coordinates.
    static core::sem::Access affine(std::vector<ExprId> coords);
    /// An access that reads no element (a constant argument, an operand only its shape is used of).
    static core::sem::Access none();

    /**
     * The broadcast of operands of the given shapes to the shape of the result (NumPy rules,
     * aligned on the last axis). Appends one Parallel dim per result axis and returns, per
     * operand, its coordinates: the result dim, or 0 on an axis where the operand's extent is 1.
     * nullopt when the shapes do not fix it: an axis where two operands may differ and one may
     * be 1 (design Q9: no instance until the shape is known), or extents that conflict.
     * `operands[i]` is the index of the operand whose shape is `shapes[i]`, for the parameters.
     */
    std::optional<std::vector<std::vector<ExprId>>>
    broadcast(std::span<const StaticShape> shapes, std::span<const size_t> operands);

    /// The finished instance, writing `coords` with `mode` (and `law` when it combines).
    core::sem::IndexedInstance finish(
        std::vector<ExprId> coords, core::sem::WriteMode mode = core::sem::WriteMode::Unique,
        uint8_t law = core::sem::kNoLaw);
};

/// Gives a tensor operator that has index semantics its IndexFn (OpDef::index) and its
/// reduction laws (in def.semantics), and raises the minor version of its key (the facts are
/// added to version 1.0; design 4.1). Called by attachSemantics (semantics.cpp).
void attachIndexing(OpDef &def);

/// The InstantiateFn of an IndexFn: the call's types and constant arguments become the
/// InferContext inference sees. The registry declares OpDef::index through it.
core::sem::InstantiateFn indexingFrom(IndexFn fn);

/**
 * The instance of `def` for what `ctx` knows, validated against its semantics (its laws), as
 * sem::instantiate gives it: for a back end that has the InferContext of the call already
 * (it called def.infer) and not the slots of its constant arguments. nullopt when `def`
 * declares no index semantics or the facts do not fix them.
 */
std::optional<core::sem::IndexedInstance> instantiate(const OpDef &def, const InferContext &ctx);

} // namespace camel::tensor::ops
