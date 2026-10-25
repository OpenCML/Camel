// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * What a tensor is, now that `Tensor` is an alias of the native array type
 * (docs/design/arrays-memory/plan.cn.md section 5).
 *
 * `Tensor` is exported as the open-rank array whose element type is not stated
 * (`ArrayType::elementTypeOpen()`), so that a program which still writes the name is told to write
 * the element type; `Tensor<f32>` is `f32[..]`, and a numeric array a program writes
 * (`f32[2, 3]`) is the same kind of type: one representation, two spellings. There is no second
 * tensor type and no second type code; what a tensor's type says is read through one set of facts.
 *
 * A shape may name its extents (design principles 5.4): `Tensor<f32, [n, 3]>` keeps the linear
 * form of each named extent (`core/type/extent.h`) in the array type, so the caller can bind `n`
 * and hold every occurrence of it to one value. The forms take part in interning (the signature
 * keeps the names) but not in `equals` / assignability / cast safety.
 */

#pragma once

#include "camel/core/type/composite/array.h"

#include <optional>
#include <span>
#include <vector>

namespace camel::tensor {

namespace type = camel::core::type;

/// An extent a program left open (`?` in the plan's spelling): the facts carry it as an absent
/// value, and the type itself keeps the unknown as a form (`ExtentForm`). The transitional `-1`
/// sentinel is gone (docs/design/arrays-memory/plan.cn.md section 5.3's migration table:
/// `kUnknownDim = -1` is `std::nullopt`).
using StaticShape = std::vector<std::optional<int64_t>>;

/// The plain extents of a shape whose every extent is known, for a caller that needs numbers with
/// no way to say unknown (an array is created from them, a program's shape is written into). The
/// callers check the shape first.
std::vector<int64_t> extentsOf(const StaticShape &shape);

/// The same extents in the kernel shape helpers' own spelling (modules/tensor/kernels/layout.h):
/// an extent nobody stated is `-1` there, which is how a kernel carries an unknown extent through
/// a permutation or a reshape.
std::vector<int64_t> kernelExtents(const StaticShape &shape);

/// The facts' shape of a kernel shape helper's result: a negative extent is that helper saying the
/// extent is unknown, so the entry is absent here.
StaticShape knownShape(std::vector<int64_t> extents);

/// True for an array whose element is a numeric scalar, and for an array whose element type
/// nobody stated (`Void`, the empty array literal's element). A nested array (`f32[][]`) is not
/// one of these: it is an array of
/// arrays, the shape a tensor-like parameter also accepts.
bool isTensorType(const type::Type *type);

/// What is statically known about a tensor-valued argument: the element type when one is fixed,
/// and the shape when the rank is (an extent that is not known is an absent entry).
struct TensorFacts {
    std::optional<type::TypeCode> dtype;
    std::optional<StaticShape> shape;

    /// True when the rank and every extent are known.
    bool isStaticShape() const {
        if (!shape) {
            return false;
        }
        for (const std::optional<int64_t> &extent : *shape) {
            if (!extent) {
                return false;
            }
        }
        return true;
    }
};

/// The facts of `type` as an operator sees them (ops/op_def.cpp#InferContext::facts): a numeric
/// array (the element type is the dtype and the stated extents are the shape), a nested numeric
/// array (its nesting depth is the rank, its extents are not known), or a numeric scalar
/// (rank 0). A type this module does not take gives the empty facts.
TensorFacts tensorFactsOf(const type::Type *type);

} // namespace camel::tensor