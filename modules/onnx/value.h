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
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Values of the export-time partial evaluator.
 *
 * Every graph node evaluates to either
 *   - a constant: a concrete runtime value (slot + type), known at export
 *     time because it does not depend on the exported function's inputs
 *     (weights, hyper-parameters, shape arithmetic, closures), or
 *   - a symbolic tensor: an ONNX value name plus the static facts (dtype and
 *     shape) known about it.
 */

#pragma once

#include "../tensor/type.h"
#include "camel/core/rtdata/base.h"

#include <optional>
#include <string>

namespace camel::onnx {

namespace type   = camel::core::type;
namespace rtdata = camel::core::rtdata;

struct Value {
    enum class Kind { Constant, Symbolic };

    Kind kind = Kind::Constant;
    // Constant
    slot_t slot    = NullSlot;
    type::Type *ty = nullptr;
    std::string label; // source name (e.g. a struct field), used to name initializers
    // Symbolic
    std::string name;
    std::optional<type::TypeCode> dtype;
    std::optional<tensor::StaticShape> shape;

    static Value constant(slot_t slot, type::Type *ty) {
        Value v;
        v.kind = Kind::Constant;
        v.slot = slot;
        v.ty   = ty;
        return v;
    }

    static Value symbolic(
        std::string name, std::optional<type::TypeCode> dtype,
        std::optional<tensor::StaticShape> shape) {
        Value v;
        v.kind  = Kind::Symbolic;
        v.name  = std::move(name);
        v.dtype = dtype;
        v.shape = std::move(shape);
        return v;
    }

    bool isConstant() const { return kind == Kind::Constant; }
    bool isSymbolic() const { return kind == Kind::Symbolic; }
};

} // namespace camel::onnx
