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
 * Updated: Mar. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "operators.h"

#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/type/composite/array.h"
#include "runtime.h"
#include "type.h"

#include <iostream>
#include <optional>

namespace mm = camel::core::mm;
using namespace camel::core::error;
using namespace camel::core::type;
using namespace camel::core::rtdata;
using camel::tensor::TensorObject;
using camel::tensor::TensorType;

namespace {

Type *tensorType(Type *dtype = nullptr) {
    return TensorType::Dynamic(dtype ? dtype : Type::Float32());
}

ArrayType *intArrayType() {
    static ArrayType *instance = ArrayType::create(Type::Int64());
    return instance;
}

bool isTensorType(Type *type) { return type && type->code() == TensorType::typeCode(); }

bool isNumericScalar(Type *type) {
    return type && camel::tensor::isSupportedTensorScalar(type->code());
}

double scalarAsDouble(TypeCode code, ArgsView &norm, size_t index) {
    switch (code) {
    case TypeCode::Int32:
        return static_cast<double>(norm.get<Int32>(index));
    case TypeCode::Int64:
        return static_cast<double>(norm.get<Int64>(index));
    case TypeCode::Float32:
        return static_cast<double>(norm.get<Float32>(index));
    case TypeCode::Float64:
        return norm.get<Float64>(index);
    case TypeCode::Bool:
        return norm.get<Bool>(index) ? 1.0 : 0.0;
    default:
        throwRuntimeFault(RuntimeDiag::RuntimeError, "Expected numeric scalar");
    }
}

TensorObject *requireTensor(ArgsView &norm, size_t index) {
    if (!isTensorType(norm.type(index))) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "Expected Tensor argument");
    }
    TensorObject *tensor = norm.get<TensorObject *>(index);
    if (!tensor) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "Tensor argument cannot be null");
    }
    return tensor;
}

TensorObject *requireTensorOrConvertArray(ArgsView &norm, size_t index) {
    if (isTensorType(norm.type(index))) {
        return requireTensor(norm, index);
    }
    if (norm.type(index)->code() == TypeCode::Array) {
        try {
            return camel::tensor::tensorFromArray(
                norm.slot(index),
                norm.type(index),
                mm::autoSpace());
        } catch (const std::exception &e) {
            throwRuntimeFault(RuntimeDiag::RuntimeError, e.what());
        }
    }
    throwRuntimeFault(RuntimeDiag::RuntimeError, "Expected Tensor or numeric array");
}

slot_t wrapTensor(TensorObject *tensor) { return toSlot(static_cast<Object *>(tensor)); }

template <typename Fn> slot_t withTensorErrors(Fn &&fn) {
    try {
        return fn();
    } catch (const std::exception &e) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, e.what());
    }
}

std::optional<Type *> resolveTensorBinaryNamed(const type_vec_t &norm) {
    if (norm.size() != 2) {
        return std::nullopt;
    }
    bool lhsTensor = isTensorType(norm[0]);
    bool rhsTensor = isTensorType(norm[1]);
    if (lhsTensor && rhsTensor) {
        return tensorType();
    }
    if ((lhsTensor && isNumericScalar(norm[1])) || (rhsTensor && isNumericScalar(norm[0]))) {
        return tensorType();
    }
    return std::nullopt;
}

std::optional<Type *> resolveTensorCompare(const type_vec_t &norm) {
    if (norm.size() != 2) {
        return std::nullopt;
    }
    bool lhsTensor = isTensorType(norm[0]);
    bool rhsTensor = isTensorType(norm[1]);
    bool lhsArray  = norm[0]->code() == TypeCode::Array;
    bool rhsArray  = norm[1]->code() == TypeCode::Array;
    if ((lhsTensor && rhsTensor) || (lhsTensor && isNumericScalar(norm[1])) ||
        (rhsTensor && isNumericScalar(norm[0])) || (lhsArray && isNumericScalar(norm[1])) ||
        (isNumericScalar(norm[0]) && rhsArray)) {
        return tensorType(Type::Bool());
    }
    return std::nullopt;
}

std::optional<Type *> resolveShapeCtor(const type_vec_t &norm) {
    if (norm.size() != 1 || norm[0]->code() != TypeCode::Array) {
        return std::nullopt;
    }
    return tensorType();
}

std::optional<Type *> resolveTensorArrayConcat(const type_vec_t &norm) {
    if (norm.size() != 3 || norm[2]->code() != TypeCode::Int64) {
        return std::nullopt;
    }
    bool lhsOk = isTensorType(norm[0]) || norm[0]->code() == TypeCode::Array;
    bool rhsOk = isTensorType(norm[1]) || norm[1]->code() == TypeCode::Array;
    return (lhsOk && rhsOk) ? std::optional<Type *>{tensorType()} : std::nullopt;
}

} // namespace

std::unordered_map<std::string, operator_t> getTensorOpsMap() {
    return {
        {"add", __tensor_add__},
        {"subtract", __tensor_subtract__},
        {"multiply", __tensor_multiply__},
        {"divide", __tensor_divide__},
        {"pow", __tensor_pow__},
        {"matmul", __tensor_matmul__},
        {"idx", __tensor_idx__},
        {"idx2d", __tensor_idx2d__},
        {"lt", __tensor_lt__},
        {"le", __tensor_le__},
        {"gt", __tensor_gt__},
        {"ge", __tensor_ge__},
        {"eq", __tensor_eq__},
        {"empty", __tensor_empty__},
        {"zeros", __tensor_zeros__},
        {"ones", __tensor_ones__},
        {"full", __tensor_full__},
        {"random", __tensor_random__},
        {"range", __tensor_range__},
        {"eye", __tensor_eye__},
        {"shape", __tensor_shape__},
        {"sum", __tensor_sum__},
        {"sum_axis", __tensor_sum_axis__},
        {"max_axis", __tensor_max_axis__},
        {"argmax_axis", __tensor_argmax_axis__},
        {"exp", __tensor_exp__},
        {"log", __tensor_log__},
        {"transpose", __tensor_transpose__},
        {"concat", __tensor_concat__},
        {"reshape", __tensor_reshape__},
        {"show", __tensor_show__},
    };
}

const std::vector<oper_group_ptr_t> &getTensorOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "__add__",
            {{"tensor:add",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {2, {false, false}}},
                  "(lhs: Tensor | number, rhs: Tensor | number) => Tensor",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> { return resolveTensorBinaryNamed(norm); })}}),
        OperatorGroup::create(
            "__sub__",
            {{"tensor:subtract",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {2, {false, false}}},
                  "(lhs: Tensor | number, rhs: Tensor | number) => Tensor",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> { return resolveTensorBinaryNamed(norm); })}}),
        OperatorGroup::create(
            "__mul__",
            {{"tensor:multiply",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {2, {false, false}}},
                  "(lhs: Tensor | number, rhs: Tensor | number) => Tensor",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> { return resolveTensorBinaryNamed(norm); })}}),
        OperatorGroup::create(
            "__div__",
            {{"tensor:divide",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {2, {false, false}}},
                  "(lhs: Tensor | number, rhs: Tensor | number) => Tensor",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> { return resolveTensorBinaryNamed(norm); })}}),
        OperatorGroup::create(
            "__pow__",
            {{"tensor:pow",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {2, {false, false}}},
                  "(base: Tensor, exponent: Tensor | number) => Tensor",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      if (!isTensorType(norm[0])) {
                          return std::nullopt;
                      }
                      return isTensorType(norm[1]) || isNumericScalar(norm[1])
                                 ? std::optional<Type *>{tensorType()}
                                 : std::nullopt;
                  })}}),
        OperatorGroup::create(
            "__mat__",
            {{"tensor:matmul",
              StaticFuncTypeResolver::create(
                  {},
                  {{tensorType(), false}, {tensorType(), false}},
                  tensorType())}}),
        OperatorGroup::create(
            "__idx__",
            {
                {"tensor:idx",
                 DynamicFuncTypeResolver::create(
                     {{0, {}}, {2, {false, false}}},
                     "(t: Tensor, i: int) => float",
                     [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                         -> std::optional<Type *> {
                         return isTensorType(norm[0]) && norm[1]->code() == TypeCode::Int64
                                    ? std::optional<Type *>{Type::Float64()}
                                    : std::nullopt;
                     })},
                {"tensor:idx2d",
                 DynamicFuncTypeResolver::create(
                     {{0, {}}, {3, {false, false, false}}},
                     "(t: Tensor, i: int, j: int) => float",
                     [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                         -> std::optional<Type *> {
                         return isTensorType(norm[0]) && norm[1]->code() == TypeCode::Int64 &&
                                        norm[2]->code() == TypeCode::Int64
                                    ? std::optional<Type *>{Type::Float64()}
                                    : std::nullopt;
                     })},
            }),
        OperatorGroup::create(
            "__lt__",
            {{"tensor:lt",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {2, {false, false}}},
                  "(lhs: Tensor | number, rhs: Tensor | number) => Tensor<bool>",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> { return resolveTensorCompare(norm); })}}),
        OperatorGroup::create(
            "__le__",
            {{"tensor:le",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {2, {false, false}}},
                  "(lhs: Tensor | number, rhs: Tensor | number) => Tensor<bool>",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> { return resolveTensorCompare(norm); })}}),
        OperatorGroup::create(
            "__gt__",
            {{"tensor:gt",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {2, {false, false}}},
                  "(lhs: Tensor | number, rhs: Tensor | number) => Tensor<bool>",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> { return resolveTensorCompare(norm); })}}),
        OperatorGroup::create(
            "__ge__",
            {{"tensor:ge",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {2, {false, false}}},
                  "(lhs: Tensor | number, rhs: Tensor | number) => Tensor<bool>",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> { return resolveTensorCompare(norm); })}}),
        OperatorGroup::create(
            "__eq__",
            {{"tensor:eq",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {2, {false, false}}},
                  "(lhs: Tensor | number, rhs: Tensor | number) => Tensor<bool>",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> { return resolveTensorCompare(norm); })}}),
        OperatorGroup::create(
            "empty",
            {{"tensor:empty",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {1, {false}}},
                  "(shape: int[]) => Tensor",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> { return resolveShapeCtor(norm); })}}),
        OperatorGroup::create(
            "zeros",
            {{"tensor:zeros",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {1, {false}}},
                  "(shape: int[]) => Tensor",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> { return resolveShapeCtor(norm); })}}),
        OperatorGroup::create(
            "ones",
            {{"tensor:ones",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {1, {false}}},
                  "(shape: int[]) => Tensor",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> { return resolveShapeCtor(norm); })}}),
        OperatorGroup::create(
            "full",
            {{"tensor:full",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {2, {false, false}}},
                  "(shape: int[], value: number) => Tensor",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      return norm[0]->code() == TypeCode::Array && isNumericScalar(norm[1])
                                 ? std::optional<Type *>{tensorType()}
                                 : std::nullopt;
                  })}}),
        OperatorGroup::create(
            "random",
            {{"tensor:random",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {3, {false, false, false}}},
                  "(shape: int[], low: number, high: number) => Tensor",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      return norm[0]->code() == TypeCode::Array && isNumericScalar(norm[1]) &&
                                     isNumericScalar(norm[2])
                                 ? std::optional<Type *>{tensorType()}
                                 : std::nullopt;
                  })}}),
        OperatorGroup::create(
            "range",
            {{"tensor:range",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {-1, {}}},
                  "(start: int, stop?: int, step?: int) => Tensor",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      if (norm.empty() || norm.size() > 3) {
                          return std::nullopt;
                      }
                      for (Type *t : norm) {
                          if (t->code() != TypeCode::Int64 && t->code() != TypeCode::Int32) {
                              return std::nullopt;
                          }
                      }
                      return tensorType(Type::Int64());
                  })}}),
        OperatorGroup::create(
            "eye",
            {{"tensor:eye",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {1, {false}}},
                  "(n: int) => Tensor",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      return (norm[0]->code() == TypeCode::Int32 ||
                              norm[0]->code() == TypeCode::Int64)
                                 ? std::optional<Type *>{tensorType()}
                                 : std::nullopt;
                  })}}),
        OperatorGroup::create(
            "shape",
            {{"tensor:shape",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {1, {false}}},
                  "(t: Tensor) => int[]",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      return isTensorType(norm[0]) ? std::optional<Type *>{intArrayType()}
                                                   : std::nullopt;
                  })}}),
        OperatorGroup::create(
            "sum",
            {
                {"tensor:sum",
                 DynamicFuncTypeResolver::create(
                     {{0, {}}, {1, {false}}},
                     "(t: Tensor) => float",
                     [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                         -> std::optional<Type *> {
                         return isTensorType(norm[0]) ? std::optional<Type *>{Type::Float64()}
                                                      : std::nullopt;
                     })},
                {"tensor:sum_axis",
                 DynamicFuncTypeResolver::create(
                     {{0, {}}, {2, {false, false}}},
                     "(t: Tensor, axis: int) => Tensor",
                     [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                         -> std::optional<Type *> {
                         return (norm.size() == 2 && isTensorType(norm[0]) &&
                                 (norm[1]->code() == TypeCode::Int32 ||
                                  norm[1]->code() == TypeCode::Int64))
                                    ? std::optional<Type *>{tensorType()}
                                    : std::nullopt;
                     })},
            }),
        OperatorGroup::create(
            "max",
            {{"tensor:max_axis",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {2, {false, false}}},
                  "(t: Tensor, axis: int) => Tensor",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      return (norm.size() == 2 && isTensorType(norm[0]) &&
                              (norm[1]->code() == TypeCode::Int32 ||
                               norm[1]->code() == TypeCode::Int64))
                                 ? std::optional<Type *>{tensorType()}
                                 : std::nullopt;
                  })}}),
        OperatorGroup::create(
            "argmax",
            {{"tensor:argmax_axis",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {2, {false, false}}},
                  "(t: Tensor, axis: int) => Tensor",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      return (norm.size() == 2 && isTensorType(norm[0]) &&
                              (norm[1]->code() == TypeCode::Int32 ||
                               norm[1]->code() == TypeCode::Int64))
                                 ? std::optional<Type *>{tensorType(Type::Int64())}
                                 : std::nullopt;
                  })}}),
        OperatorGroup::create(
            "exp",
            {{"tensor:exp",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {1, {false}}},
                  "(t: Tensor) => Tensor",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      return isTensorType(norm[0]) ? std::optional<Type *>{tensorType()}
                                                   : std::nullopt;
                  })}}),
        OperatorGroup::create(
            "log",
            {{"tensor:log",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {1, {false}}},
                  "(t: Tensor) => Tensor",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      return isTensorType(norm[0]) ? std::optional<Type *>{tensorType()}
                                                   : std::nullopt;
                  })}}),
        OperatorGroup::create(
            "transpose",
            {{"tensor:transpose",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {1, {false}}},
                  "(t: Tensor) => Tensor",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      return isTensorType(norm[0]) ? std::optional<Type *>{tensorType()}
                                                   : std::nullopt;
                  })}}),
        OperatorGroup::create(
            "concat",
            {{"tensor:concat",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {3, {false, false, false}}},
                  "(a: Tensor | number[], b: Tensor | number[], axis: int) => Tensor",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> { return resolveTensorArrayConcat(norm); })}}),
        OperatorGroup::create(
            "reshape",
            {{"tensor:reshape",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {2, {false, false}}},
                  "(t: Tensor | number[], shape: int[]) => Tensor",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      return (isTensorType(norm[0]) || norm[0]->code() == TypeCode::Array) &&
                                     norm[1]->code() == TypeCode::Array
                                 ? std::optional<Type *>{tensorType()}
                                 : std::nullopt;
                  })}}),
        OperatorGroup::create(
            "show",
            {{"tensor:show",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {1, {false}}},
                  "(t: Tensor | number[]) => void",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      return (isTensorType(norm[0]) || norm[0]->code() == TypeCode::Array)
                                 ? std::optional<Type *>{Type::Void()}
                                 : std::nullopt;
                  })}}),
        OperatorGroup::create(
            "add",
            {{"tensor:add",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {2, {false, false}}},
                  "(lhs: Tensor | number, rhs: Tensor | number) => Tensor",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> { return resolveTensorBinaryNamed(norm); })}}),
        OperatorGroup::create(
            "subtract",
            {{"tensor:subtract",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {2, {false, false}}},
                  "(lhs: Tensor | number, rhs: Tensor | number) => Tensor",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> { return resolveTensorBinaryNamed(norm); })}}),
        OperatorGroup::create(
            "multiply",
            {{"tensor:multiply",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {2, {false, false}}},
                  "(lhs: Tensor | number, rhs: Tensor | number) => Tensor",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> { return resolveTensorBinaryNamed(norm); })}}),
        OperatorGroup::create(
            "divide",
            {{"tensor:divide",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {2, {false, false}}},
                  "(lhs: Tensor | number, rhs: Tensor | number) => Tensor",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> { return resolveTensorBinaryNamed(norm); })}}),
        OperatorGroup::create(
            "pow",
            {{"tensor:pow",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {2, {false, false}}},
                  "(base: Tensor, exponent: Tensor | number) => Tensor",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      if (!isTensorType(norm[0])) {
                          return std::nullopt;
                      }
                      return isTensorType(norm[1]) || isNumericScalar(norm[1])
                                 ? std::optional<Type *>{tensorType()}
                                 : std::nullopt;
                  })}}),
        OperatorGroup::create(
            "matmul",
            {{"tensor:matmul",
              StaticFuncTypeResolver::create(
                  {},
                  {{tensorType(), false}, {tensorType(), false}},
                  tensorType())}}),
    };
    return groups;
}

slot_t __tensor_add__(ArgsView &with, ArgsView &norm, ctx::Context &ctx) {
    (void)with;
    (void)ctx;
    return withTensorErrors([&]() -> slot_t {
        bool lhsTensor = isTensorType(norm.type(0));
        bool rhsTensor = isTensorType(norm.type(1));
        if (lhsTensor && rhsTensor) {
            return wrapTensor(
                camel::tensor::tensorBinary(
                    requireTensor(norm, 0),
                    requireTensor(norm, 1),
                    camel::tensor::BinaryOp::Add,
                    mm::autoSpace()));
        }
        if (lhsTensor) {
            return wrapTensor(
                camel::tensor::tensorBinaryScalarRight(
                    requireTensor(norm, 0),
                    norm.type(1)->code(),
                    norm.slot(1),
                    camel::tensor::BinaryOp::Add,
                    mm::autoSpace()));
        }
        return wrapTensor(
            camel::tensor::tensorBinaryScalarLeft(
                norm.type(0)->code(),
                norm.slot(0),
                requireTensor(norm, 1),
                camel::tensor::BinaryOp::Add,
                mm::autoSpace()));
    });
}

slot_t __tensor_subtract__(ArgsView &with, ArgsView &norm, ctx::Context &ctx) {
    (void)with;
    (void)ctx;
    return withTensorErrors([&]() -> slot_t {
        bool lhsTensor = isTensorType(norm.type(0));
        bool rhsTensor = isTensorType(norm.type(1));
        if (lhsTensor && rhsTensor) {
            return wrapTensor(
                camel::tensor::tensorBinary(
                    requireTensor(norm, 0),
                    requireTensor(norm, 1),
                    camel::tensor::BinaryOp::Subtract,
                    mm::autoSpace()));
        }
        if (lhsTensor) {
            return wrapTensor(
                camel::tensor::tensorBinaryScalarRight(
                    requireTensor(norm, 0),
                    norm.type(1)->code(),
                    norm.slot(1),
                    camel::tensor::BinaryOp::Subtract,
                    mm::autoSpace()));
        }
        return wrapTensor(
            camel::tensor::tensorBinaryScalarLeft(
                norm.type(0)->code(),
                norm.slot(0),
                requireTensor(norm, 1),
                camel::tensor::BinaryOp::Subtract,
                mm::autoSpace()));
    });
}

slot_t __tensor_multiply__(ArgsView &with, ArgsView &norm, ctx::Context &ctx) {
    (void)with;
    (void)ctx;
    return withTensorErrors([&]() -> slot_t {
        bool lhsTensor = isTensorType(norm.type(0));
        bool rhsTensor = isTensorType(norm.type(1));
        if (lhsTensor && rhsTensor) {
            return wrapTensor(
                camel::tensor::tensorBinary(
                    requireTensor(norm, 0),
                    requireTensor(norm, 1),
                    camel::tensor::BinaryOp::Multiply,
                    mm::autoSpace()));
        }
        if (lhsTensor) {
            return wrapTensor(
                camel::tensor::tensorBinaryScalarRight(
                    requireTensor(norm, 0),
                    norm.type(1)->code(),
                    norm.slot(1),
                    camel::tensor::BinaryOp::Multiply,
                    mm::autoSpace()));
        }
        return wrapTensor(
            camel::tensor::tensorBinaryScalarLeft(
                norm.type(0)->code(),
                norm.slot(0),
                requireTensor(norm, 1),
                camel::tensor::BinaryOp::Multiply,
                mm::autoSpace()));
    });
}

slot_t __tensor_divide__(ArgsView &with, ArgsView &norm, ctx::Context &ctx) {
    (void)with;
    (void)ctx;
    return withTensorErrors([&]() -> slot_t {
        bool lhsTensor = isTensorType(norm.type(0));
        bool rhsTensor = isTensorType(norm.type(1));
        if (lhsTensor && rhsTensor) {
            return wrapTensor(
                camel::tensor::tensorBinary(
                    requireTensor(norm, 0),
                    requireTensor(norm, 1),
                    camel::tensor::BinaryOp::Divide,
                    mm::autoSpace()));
        }
        if (lhsTensor) {
            return wrapTensor(
                camel::tensor::tensorBinaryScalarRight(
                    requireTensor(norm, 0),
                    norm.type(1)->code(),
                    norm.slot(1),
                    camel::tensor::BinaryOp::Divide,
                    mm::autoSpace()));
        }
        return wrapTensor(
            camel::tensor::tensorBinaryScalarLeft(
                norm.type(0)->code(),
                norm.slot(0),
                requireTensor(norm, 1),
                camel::tensor::BinaryOp::Divide,
                mm::autoSpace()));
    });
}

slot_t __tensor_pow__(ArgsView &with, ArgsView &norm, ctx::Context &ctx) {
    (void)with;
    (void)ctx;
    return withTensorErrors([&]() -> slot_t {
        bool rhsTensor = isTensorType(norm.type(1));
        if (rhsTensor) {
            return wrapTensor(
                camel::tensor::tensorBinary(
                    requireTensor(norm, 0),
                    requireTensor(norm, 1),
                    camel::tensor::BinaryOp::Power,
                    mm::autoSpace()));
        }
        return wrapTensor(
            camel::tensor::tensorBinaryScalarRight(
                requireTensor(norm, 0),
                norm.type(1)->code(),
                norm.slot(1),
                camel::tensor::BinaryOp::Power,
                mm::autoSpace()));
    });
}

slot_t __tensor_matmul__(ArgsView &with, ArgsView &norm, ctx::Context &ctx) {
    (void)with;
    (void)ctx;
    return withTensorErrors([&]() -> slot_t {
        TensorObject *lhs = requireTensor(norm, 0);
        TensorObject *rhs = requireTensor(norm, 1);
        if (lhs->rank() != 2 || rhs->rank() != 2) {
            throw std::invalid_argument("matmul currently only supports rank-2 tensors");
        }
        if (lhs->dim(1) != rhs->dim(0)) {
            throw std::invalid_argument("matmul dimension mismatch");
        }
        int64_t outShape[] = {lhs->dim(0), rhs->dim(1)};
        TensorObject *out  = camel::tensor::tensorZeros(
            camel::tensor::promoteTensorTypes(lhs->dtype(), rhs->dtype()),
            outShape,
            mm::autoSpace());
        for (int64_t i = 0; i < lhs->dim(0); ++i) {
            for (int64_t j = 0; j < rhs->dim(1); ++j) {
                double acc = 0.0;
                for (int64_t k = 0; k < lhs->dim(1); ++k) {
                    acc += lhs->getAsDouble(static_cast<uint64_t>(i * lhs->dim(1) + k)) *
                           rhs->getAsDouble(static_cast<uint64_t>(k * rhs->dim(1) + j));
                }
                out->setFromDouble(static_cast<uint64_t>(i * rhs->dim(1) + j), acc);
            }
        }
        return wrapTensor(out);
    });
}

slot_t __tensor_idx__(ArgsView &with, ArgsView &norm, ctx::Context &ctx) {
    (void)with;
    (void)ctx;
    return withTensorErrors([&]() {
        return toSlot(camel::tensor::tensorIndex1D(requireTensor(norm, 0), norm.get<Int64>(1)));
    });
}

slot_t __tensor_idx2d__(ArgsView &with, ArgsView &norm, ctx::Context &ctx) {
    (void)with;
    (void)ctx;
    return withTensorErrors([&]() {
        return toSlot(
            camel::tensor::tensorIndex2D(
                requireTensor(norm, 0),
                norm.get<Int64>(1),
                norm.get<Int64>(2)));
    });
}

slot_t __tensor_lt__(ArgsView &with, ArgsView &norm, ctx::Context &ctx) {
    (void)with;
    (void)ctx;
    return withTensorErrors([&]() -> slot_t {
        bool lhsTensor = isTensorType(norm.type(0));
        bool rhsTensor = isTensorType(norm.type(1));
        if (lhsTensor && rhsTensor) {
            return wrapTensor(
                camel::tensor::tensorCompare(
                    requireTensor(norm, 0),
                    requireTensor(norm, 1),
                    camel::tensor::CompareOp::Less,
                    mm::autoSpace()));
        }
        if (lhsTensor) {
            return wrapTensor(
                camel::tensor::tensorCompareScalarRight(
                    requireTensor(norm, 0),
                    norm.type(1)->code(),
                    norm.slot(1),
                    camel::tensor::CompareOp::Less,
                    mm::autoSpace()));
        }
        return wrapTensor(
            camel::tensor::tensorCompareScalarLeft(
                norm.type(0)->code(),
                norm.slot(0),
                requireTensor(norm, 1),
                camel::tensor::CompareOp::Less,
                mm::autoSpace()));
    });
}

slot_t __tensor_le__(ArgsView &with, ArgsView &norm, ctx::Context &ctx) {
    (void)with;
    (void)ctx;
    return withTensorErrors([&]() -> slot_t {
        bool lhsTensor = isTensorType(norm.type(0));
        bool rhsTensor = isTensorType(norm.type(1));
        if (lhsTensor && rhsTensor) {
            return wrapTensor(
                camel::tensor::tensorCompare(
                    requireTensor(norm, 0),
                    requireTensor(norm, 1),
                    camel::tensor::CompareOp::LessEqual,
                    mm::autoSpace()));
        }
        if (lhsTensor) {
            return wrapTensor(
                camel::tensor::tensorCompareScalarRight(
                    requireTensor(norm, 0),
                    norm.type(1)->code(),
                    norm.slot(1),
                    camel::tensor::CompareOp::LessEqual,
                    mm::autoSpace()));
        }
        return wrapTensor(
            camel::tensor::tensorCompareScalarLeft(
                norm.type(0)->code(),
                norm.slot(0),
                requireTensor(norm, 1),
                camel::tensor::CompareOp::LessEqual,
                mm::autoSpace()));
    });
}

slot_t __tensor_gt__(ArgsView &with, ArgsView &norm, ctx::Context &ctx) {
    (void)with;
    (void)ctx;
    return withTensorErrors([&]() -> slot_t {
        bool lhsTensor = isTensorType(norm.type(0));
        bool rhsTensor = isTensorType(norm.type(1));
        if (lhsTensor && rhsTensor) {
            return wrapTensor(
                camel::tensor::tensorCompare(
                    requireTensor(norm, 0),
                    requireTensor(norm, 1),
                    camel::tensor::CompareOp::Greater,
                    mm::autoSpace()));
        }
        if (lhsTensor) {
            return wrapTensor(
                camel::tensor::tensorCompareScalarRight(
                    requireTensor(norm, 0),
                    norm.type(1)->code(),
                    norm.slot(1),
                    camel::tensor::CompareOp::Greater,
                    mm::autoSpace()));
        }
        return wrapTensor(
            camel::tensor::tensorCompareScalarLeft(
                norm.type(0)->code(),
                norm.slot(0),
                requireTensor(norm, 1),
                camel::tensor::CompareOp::Greater,
                mm::autoSpace()));
    });
}

slot_t __tensor_ge__(ArgsView &with, ArgsView &norm, ctx::Context &ctx) {
    (void)with;
    (void)ctx;
    return withTensorErrors([&]() -> slot_t {
        bool lhsTensor = isTensorType(norm.type(0));
        bool rhsTensor = isTensorType(norm.type(1));
        if (lhsTensor && rhsTensor) {
            return wrapTensor(
                camel::tensor::tensorCompare(
                    requireTensor(norm, 0),
                    requireTensor(norm, 1),
                    camel::tensor::CompareOp::GreaterEqual,
                    mm::autoSpace()));
        }
        if (lhsTensor) {
            return wrapTensor(
                camel::tensor::tensorCompareScalarRight(
                    requireTensor(norm, 0),
                    norm.type(1)->code(),
                    norm.slot(1),
                    camel::tensor::CompareOp::GreaterEqual,
                    mm::autoSpace()));
        }
        return wrapTensor(
            camel::tensor::tensorCompareScalarLeft(
                norm.type(0)->code(),
                norm.slot(0),
                requireTensor(norm, 1),
                camel::tensor::CompareOp::GreaterEqual,
                mm::autoSpace()));
    });
}

slot_t __tensor_eq__(ArgsView &with, ArgsView &norm, ctx::Context &ctx) {
    (void)with;
    (void)ctx;
    return withTensorErrors([&]() -> slot_t {
        bool lhsTensorOrArray =
            isTensorType(norm.type(0)) || norm.type(0)->code() == TypeCode::Array;
        bool rhsTensorOrArray =
            isTensorType(norm.type(1)) || norm.type(1)->code() == TypeCode::Array;
        if (lhsTensorOrArray && rhsTensorOrArray) {
            return wrapTensor(
                camel::tensor::tensorCompare(
                    requireTensorOrConvertArray(norm, 0),
                    requireTensorOrConvertArray(norm, 1),
                    camel::tensor::CompareOp::Equal,
                    mm::autoSpace()));
        }
        if (lhsTensorOrArray) {
            return wrapTensor(
                camel::tensor::tensorCompareScalarRight(
                    requireTensorOrConvertArray(norm, 0),
                    norm.type(1)->code(),
                    norm.slot(1),
                    camel::tensor::CompareOp::Equal,
                    mm::autoSpace()));
        }
        return wrapTensor(
            camel::tensor::tensorCompareScalarLeft(
                norm.type(0)->code(),
                norm.slot(0),
                requireTensorOrConvertArray(norm, 1),
                camel::tensor::CompareOp::Equal,
                mm::autoSpace()));
    });
}

slot_t __tensor_empty__(ArgsView &with, ArgsView &norm, ctx::Context &ctx) {
    (void)with;
    (void)ctx;
    return withTensorErrors([&]() -> slot_t {
        auto shape = camel::tensor::parseShapeArray(norm.get<Array *>(0), norm.type(0));
        return wrapTensor(
            camel::tensor::tensorEmpty(Type::Float32()->code(), shape, mm::autoSpace()));
    });
}

slot_t __tensor_zeros__(ArgsView &with, ArgsView &norm, ctx::Context &ctx) {
    (void)with;
    (void)ctx;
    return withTensorErrors([&]() -> slot_t {
        auto shape = camel::tensor::parseShapeArray(norm.get<Array *>(0), norm.type(0));
        return wrapTensor(
            camel::tensor::tensorZeros(Type::Float32()->code(), shape, mm::autoSpace()));
    });
}

slot_t __tensor_ones__(ArgsView &with, ArgsView &norm, ctx::Context &ctx) {
    (void)with;
    (void)ctx;
    return withTensorErrors([&]() -> slot_t {
        auto shape = camel::tensor::parseShapeArray(norm.get<Array *>(0), norm.type(0));
        return wrapTensor(
            camel::tensor::tensorOnes(Type::Float32()->code(), shape, mm::autoSpace()));
    });
}

slot_t __tensor_full__(ArgsView &with, ArgsView &norm, ctx::Context &ctx) {
    (void)with;
    (void)ctx;
    return withTensorErrors([&]() -> slot_t {
        auto shape = camel::tensor::parseShapeArray(norm.get<Array *>(0), norm.type(0));
        return wrapTensor(
            camel::tensor::tensorFull(
                camel::tensor::tensorDTypeFromValueType(norm.type(1)->code()),
                shape,
                scalarAsDouble(norm.type(1)->code(), norm, 1),
                mm::autoSpace()));
    });
}

slot_t __tensor_random__(ArgsView &with, ArgsView &norm, ctx::Context &ctx) {
    (void)with;
    (void)ctx;
    return withTensorErrors([&]() -> slot_t {
        auto shape = camel::tensor::parseShapeArray(norm.get<Array *>(0), norm.type(0));
        return wrapTensor(
            camel::tensor::tensorRandom(
                shape,
                scalarAsDouble(norm.type(1)->code(), norm, 1),
                scalarAsDouble(norm.type(2)->code(), norm, 2),
                mm::autoSpace()));
    });
}

slot_t __tensor_range__(ArgsView &with, ArgsView &norm, ctx::Context &ctx) {
    (void)with;
    (void)ctx;
    return withTensorErrors([&]() -> slot_t {
        int64_t start = 0;
        int64_t stop  = 0;
        int64_t step  = 1;
        if (norm.size() == 1) {
            stop = norm.type(0)->code() == TypeCode::Int32
                       ? static_cast<int64_t>(norm.get<Int32>(0))
                       : norm.get<Int64>(0);
        } else {
            start = norm.type(0)->code() == TypeCode::Int32
                        ? static_cast<int64_t>(norm.get<Int32>(0))
                        : norm.get<Int64>(0);
            stop  = norm.type(1)->code() == TypeCode::Int32
                        ? static_cast<int64_t>(norm.get<Int32>(1))
                        : norm.get<Int64>(1);
            if (norm.size() == 3) {
                step = norm.type(2)->code() == TypeCode::Int32
                           ? static_cast<int64_t>(norm.get<Int32>(2))
                           : norm.get<Int64>(2);
            }
        }
        return wrapTensor(camel::tensor::tensorArange(start, stop, step, mm::autoSpace()));
    });
}

slot_t __tensor_eye__(ArgsView &with, ArgsView &norm, ctx::Context &ctx) {
    (void)with;
    (void)ctx;
    return withTensorErrors([&]() -> slot_t {
        int64_t size = norm.type(0)->code() == TypeCode::Int32
                           ? static_cast<int64_t>(norm.get<Int32>(0))
                           : norm.get<Int64>(0);
        return wrapTensor(camel::tensor::tensorEye(size, mm::autoSpace()));
    });
}

slot_t __tensor_shape__(ArgsView &with, ArgsView &norm, ctx::Context &ctx) {
    (void)with;
    (void)ctx;
    return withTensorErrors([&]() -> slot_t {
        return toSlot(camel::tensor::makeShapeArray(requireTensor(norm, 0), mm::autoSpace()));
    });
}

slot_t __tensor_sum__(ArgsView &with, ArgsView &norm, ctx::Context &ctx) {
    (void)with;
    (void)ctx;
    return withTensorErrors(
        [&]() -> slot_t { return toSlot(camel::tensor::tensorSum(requireTensor(norm, 0))); });
}

slot_t __tensor_sum_axis__(ArgsView &with, ArgsView &norm, ctx::Context &ctx) {
    (void)with;
    (void)ctx;
    return withTensorErrors([&]() -> slot_t {
        int64_t axis = norm.type(1)->code() == TypeCode::Int32
                           ? static_cast<int64_t>(norm.get<Int32>(1))
                           : norm.get<Int64>(1);
        return wrapTensor(
            camel::tensor::tensorSumAxis(requireTensor(norm, 0), axis, mm::autoSpace()));
    });
}

slot_t __tensor_max_axis__(ArgsView &with, ArgsView &norm, ctx::Context &ctx) {
    (void)with;
    (void)ctx;
    return withTensorErrors([&]() -> slot_t {
        int64_t axis = norm.type(1)->code() == TypeCode::Int32
                           ? static_cast<int64_t>(norm.get<Int32>(1))
                           : norm.get<Int64>(1);
        return wrapTensor(
            camel::tensor::tensorMaxAxis(requireTensor(norm, 0), axis, mm::autoSpace()));
    });
}

slot_t __tensor_argmax_axis__(ArgsView &with, ArgsView &norm, ctx::Context &ctx) {
    (void)with;
    (void)ctx;
    return withTensorErrors([&]() -> slot_t {
        int64_t axis = norm.type(1)->code() == TypeCode::Int32
                           ? static_cast<int64_t>(norm.get<Int32>(1))
                           : norm.get<Int64>(1);
        return wrapTensor(
            camel::tensor::tensorArgmaxAxis(requireTensor(norm, 0), axis, mm::autoSpace()));
    });
}

slot_t __tensor_exp__(ArgsView &with, ArgsView &norm, ctx::Context &ctx) {
    (void)with;
    (void)ctx;
    return withTensorErrors([&]() -> slot_t {
        return wrapTensor(camel::tensor::tensorExp(requireTensor(norm, 0), mm::autoSpace()));
    });
}

slot_t __tensor_log__(ArgsView &with, ArgsView &norm, ctx::Context &ctx) {
    (void)with;
    (void)ctx;
    return withTensorErrors([&]() -> slot_t {
        return wrapTensor(camel::tensor::tensorLog(requireTensor(norm, 0), mm::autoSpace()));
    });
}

slot_t __tensor_transpose__(ArgsView &with, ArgsView &norm, ctx::Context &ctx) {
    (void)with;
    (void)ctx;
    return withTensorErrors([&]() -> slot_t {
        return wrapTensor(
            camel::tensor::tensorTranspose2D(requireTensor(norm, 0), mm::autoSpace()));
    });
}

slot_t __tensor_concat__(ArgsView &with, ArgsView &norm, ctx::Context &ctx) {
    (void)with;
    (void)ctx;
    return withTensorErrors([&]() -> slot_t {
        TensorObject *lhs = requireTensorOrConvertArray(norm, 0);
        TensorObject *rhs = requireTensorOrConvertArray(norm, 1);
        return wrapTensor(
            camel::tensor::tensorConcat(lhs, rhs, norm.get<Int64>(2), mm::autoSpace()));
    });
}

slot_t __tensor_reshape__(ArgsView &with, ArgsView &norm, ctx::Context &ctx) {
    (void)with;
    (void)ctx;
    return withTensorErrors([&]() -> slot_t {
        auto shape = camel::tensor::parseShapeArray(norm.get<Array *>(1), norm.type(1));
        return wrapTensor(
            camel::tensor::tensorReshape(
                requireTensorOrConvertArray(norm, 0),
                shape,
                mm::autoSpace()));
    });
}

slot_t __tensor_show__(ArgsView &with, ArgsView &norm, ctx::Context &ctx) {
    (void)with;
    (void)ctx;
    return withTensorErrors([&]() -> slot_t {
        if (isTensorType(norm.type(0))) {
            requireTensor(norm, 0)->print(std::cout, norm.type(0));
            std::cout << std::endl;
            return NullSlot;
        }
        TensorObject *tensor = requireTensorOrConvertArray(norm, 0);
        tensor->print(std::cout, tensorType());
        std::cout << std::endl;
        return NullSlot;
    });
}
