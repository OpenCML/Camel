// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Values of the export-time partial evaluator.
 *
 * Every graph node evaluates to either
 *   - a constant: a concrete runtime value (slot + type), known at export
 *     time because it does not depend on the exported function's inputs
 *     (weights, hyper-parameters, shape arithmetic, closures), or
 *   - a symbolic value: an ONNX value name plus what is statically known
 *     about it. Its form says which Camel value it stands for:
 *       Tensor    a tensor (dtype and shape facts),
 *       Scalar    a Camel int/float/bool (a rank-0 ONNX tensor; `camelType`
 *                 is the Camel type, floats are carried as float32),
 *       IntArray  a Camel int[] such as shape(x) with a dynamic batch (a 1-D
 *                 int64 ONNX tensor; `elements` holds the entries known
 *                 statically, an absent entry for the others),
 *       Aggregate a Camel tuple or struct (`camelType`) with some symbolic
 *                 elements, e.g. a model's parameters or its gradients. It
 *                 has no ONNX value of its own: `fields` holds its elements.
 */

#pragma once

#include "../tensor/type.h"
#include "camel/core/rtdata/base.h"

#include <optional>
#include <string>
#include <vector>

namespace camel::onnx {

namespace type   = camel::core::type;
namespace rtdata = camel::core::rtdata;

struct Value {
    enum class Kind { Constant, Symbolic };
    enum class Form { Tensor, Scalar, IntArray, Aggregate, Closure };
    /// What a Closure value is (see the linear-recursion translation in exporter.cpp).
    enum class ClosureKind { Plain, Hole, Loop, Select };

    Kind kind = Kind::Constant;
    Form form = Form::Tensor; // symbolic values only
    // Constant
    slot_t slot    = NullSlot;
    type::Type *ty = nullptr;
    std::string label; // source name (e.g. a struct field), used to name initializers
    // Symbolic
    std::string name;
    std::optional<type::TypeCode> dtype;
    std::optional<tensor::StaticShape> shape;
    type::Type *camelType = nullptr; // Scalar: the Camel scalar type; Aggregate: tuple/struct type
    std::vector<std::optional<int64_t>> elements; // IntArray: known entries (absent when dynamic)
    std::vector<Value> fields;       // Aggregate: the elements, in the type's order
    // Closure: a function value that exists only at export time. Plain: `slot` is the function
    // and `fields` its captured values. Hole: the result of the recursive call being abstracted.
    // Loop: the function a linear recursion returns (`closureRef` names it). Select: one of the
    // two closures in `fields`, chosen by the bool value `name`.
    ClosureKind closureKind = ClosureKind::Plain;
    size_t closureRef       = 0;

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

    /// A symbolic Camel scalar of type `camelType` (int, float, or bool).
    static Value symbolicScalar(std::string name, type::Type *camelType);

    /// A symbolic int[] whose statically known entries are given (absent otherwise).
    static Value symbolicIntArray(std::string name, std::vector<std::optional<int64_t>> elements) {
        Value v;
        v.kind     = Kind::Symbolic;
        v.form     = Form::IntArray;
        v.name     = std::move(name);
        v.dtype    = type::TypeCode::Int64;
        v.shape    = tensor::StaticShape{static_cast<int64_t>(elements.size())};
        v.elements = std::move(elements);
        return v;
    }

    /// A tuple or struct of type `camelType` whose elements are `fields`.
    static Value aggregate(type::Type *camelType, std::vector<Value> fields) {
        Value v;
        v.kind      = Kind::Symbolic;
        v.form      = Form::Aggregate;
        v.camelType = camelType;
        v.fields    = std::move(fields);
        return v;
    }

    bool isConstant() const { return kind == Kind::Constant; }
    bool isAggregate() const { return kind == Kind::Symbolic && form == Form::Aggregate; }
    bool isClosure() const { return kind == Kind::Symbolic && form == Form::Closure; }

    static Value closure(ClosureKind closureKind, type::Type *functionType) {
        Value v;
        v.kind        = Kind::Symbolic;
        v.form        = Form::Closure;
        v.closureKind = closureKind;
        v.camelType   = functionType;
        return v;
    }
    bool isSymbolic() const { return kind == Kind::Symbolic; }
};

} // namespace camel::onnx
