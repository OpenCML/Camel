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
 * Author: Zhenjie Wei
 * Created: Jul. 29, 2025
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The static Tensor type.
 *
 * A TensorType refines "some tensor" with what is known at compile time:
 *   - dtype: a storage dtype (float32 / int64 / bool), or unknown;
 *   - shape: unknown rank (absent), or a list of extents where each extent is
 *     known (>= 0) or unknown (kUnknownDim).
 * The plain `Tensor` annotation is the fully unknown type and accepts every
 * tensor. Types are interned, so equal types share one instance.
 *
 * A shape written in a signature may name its extents (design principles 5.4):
 * `Tensor<f32, const [constas n, 3]>` keeps the linear form of each named extent
 * (`core/type/extent.h`), so the caller can bind `n` and hold every occurrence of
 * it to one value. The forms take part in interning (the signature keeps the
 * names) but not in `equals` / assignability / cast safety: two shapes with the
 * same extents are the same type for every check, as two function types that
 * differ only in parameter names are.
 *
 * Refinement order: a type with less information is assignable from one with
 * more. `unify` computes the least common refinement (used at branch joins),
 * keeping only the facts both sides agree on.
 */

#pragma once

#include "camel/core/type/other.h"

#include <optional>
#include <span>
#include <vector>

namespace camel::tensor {

namespace type = camel::core::type;

/// Marker for an extent that is not known statically.
inline constexpr int64_t kUnknownDim = -1;

using StaticShape = std::vector<int64_t>;

class TensorType : public type::OtherType {
  public:
    static type::TypeCode typeCode();

    /**
     * Interned constructor. `dtype` is any numeric scalar code (normalized to
     * storage) or nullopt for unknown; `shape` is nullopt for unknown rank.
     */
    static TensorType *
    get(std::optional<type::TypeCode> dtype, std::optional<StaticShape> shape = std::nullopt);
    /**
     * The same, with named extents (design principles 5.4): `forms` holds one extent per
     * dimension, and the dimensions that are not constant are unknown in `shape`. Names are
     * part of the interned type (two signatures that name their dimensions differently are
     * different types) but not of equality or assignability: `Tensor<[n, 3]>` is the same
     * tensor type as `Tensor<[?, 3]>` for every check that is not about names.
     */
    static TensorType *
    get(std::optional<type::TypeCode> dtype, std::optional<StaticShape> shape,
        std::vector<type::ExtentForm> forms);

    /// The fully unknown tensor type (`Tensor`).
    static TensorType *Default();

    std::optional<type::TypeCode> dtype() const { return dtype_; }
    const std::optional<StaticShape> &shape() const { return shape_; }
    /// The named extents, one per dimension, when the type states any; empty otherwise.
    const std::vector<type::ExtentForm> &forms() const { return forms_; }
    std::optional<size_t> rank() const {
        return shape_ ? std::optional<size_t>(shape_->size()) : std::nullopt;
    }
    /// True when rank and every extent are known.
    bool isStaticShape() const;

    std::string toString() const override;
    /// As a program writes the type: `Tensor<f32, const [2, -1]>` for what toString() prints as
    /// `Tensor<float32, [2, ?]>`.
    std::string spelling() const override;
    std::string mangle() const override;
    type::Type *clone(bool deep = false) const override;
    bool equals(type::Type *type) const override;
    type::Type *unify(type::Type *other) const override;
    /// Mutable bindings hold any tensor: widening drops dtype and shape facts.
    type::Type *widened() const override;
    type::CastSafety castSafetyFrom(type::Type *sourceType) const override;
    /// `t as Tensor<...>`: the tensor itself, after checking its element type and shape against
    /// this type (what the static types left open is checked here, when the conversion runs).
    slot_t castSlotFrom(slot_t value, type::Type *sourceType) const override;
    bool assignableFrom(type::Type *sourceType) const override;
    type::OtherType *cloneWithParams(std::span<type::Type *const> params) const override;
    /// `Tensor<dtype>`, `Tensor<const [d0, d1, ...]>`, `Tensor<dtype, const [...]>`: the element
    /// type and the shape. An extent written as a negative integer (conventionally -1) is
    /// unknown; `const []` is a scalar tensor.
    type::OtherType *cloneWithArguments(
        std::span<const type::TypeArgument> arguments, std::string &error) const override;
    bool extentForms(std::vector<type::ExtentForm> &out) const override;
    type::OtherType *withExtentValues(const type::ExtentValues &values) const override;

  private:
    TensorType(
        std::optional<type::TypeCode> dtype, std::optional<StaticShape> shape,
        std::vector<type::ExtentForm> forms = {});

    std::optional<type::TypeCode> dtype_;
    std::optional<StaticShape> shape_;
    std::vector<type::ExtentForm> forms_;
};

/// The TensorType of `type`, or nullptr when `type` is not a tensor.
const TensorType *asTensorType(const type::Type *type);

} // namespace camel::tensor
