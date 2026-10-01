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
 * Created: Sep. 29, 2026
 * Updated: Sep. 29, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Optimizer state: an opaque value holding an optimizer's per-parameter
 * trees (e.g. Adam's first and second moments, shaped like the gradient)
 * and its step count. Opaque so that training loops can name its type,
 * OptimizerState, whatever the model's shape.
 */

#pragma once

#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/other.h"

namespace camel::nn {

namespace mm     = camel::core::mm;
namespace rtdata = camel::core::rtdata;
namespace type   = camel::core::type;

class OptimizerStateType : public type::OtherType {
  public:
    OptimizerStateType();

    static type::TypeCode typeCode();
    static type::Type *Default();

    std::string toString() const override;
    std::string mangle() const override;
    type::Type *clone(bool deep = false) const override;
    bool equals(type::Type *type) const override;
    type::CastSafety castSafetyFrom(type::Type *sourceType) const override;
    bool assignableFrom(type::Type *sourceType) const override;
    type::OtherType *cloneWithParams(std::span<type::Type *const> params) const override;
};

class OptimizerStateObject : public rtdata::Object {
  public:
    OptimizerStateObject(const OptimizerStateObject &)            = delete;
    OptimizerStateObject &operator=(const OptimizerStateObject &) = delete;

    /// A state holding `trees` (of type `treesType`) after `step` updates.
    static OptimizerStateObject *
    create(::Tuple *trees, type::TupleType *treesType, int64_t step, mm::IAllocator &allocator);

    ::Tuple *trees() const { return trees_; }
    type::TupleType *treesType() const { return treesType_; }
    int64_t step() const { return step_; }

    bool
    equals(const rtdata::Object *other, const type::Type *type, bool deep = false) const override;
    rtdata::Object *
    clone(mm::IAllocator &allocator, const type::Type *type, bool deep = false) const override;
    void print(std::ostream &os, const type::Type *type) const override;
    void onMoved() override {}
    void updateRefs(const rtdata::Object::RefRelocator &relocate, const type::Type *type) override;

  private:
    OptimizerStateObject(::Tuple *trees, type::TupleType *treesType, int64_t step)
        : trees_(trees), treesType_(treesType), step_(step) {}

    ::Tuple *trees_;
    type::TupleType *treesType_;
    int64_t step_;
};

} // namespace camel::nn
