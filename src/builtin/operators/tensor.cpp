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
 * Author: Zhenjie Wei
 * Created: Oct. 06, 2024
 * Updated: Oct. 25, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "tensor.h"
#include "utils/log.h"

#include "builtin/datas/list.h"
#include "builtin/datas/tensor.h"
#include "builtin/types/tensor.h"

void __eye__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.eye"));

    auto n = frame.get(nargs[0]);
    if (!n->type()->equals(Type::Int32())) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.eye expects Int32 argument"));
    }

    auto intData = std::dynamic_pointer_cast<Int32Data>(n);
    auto tensor = TensorData::eye(intData->data());
    frame.set(self, tensor);
    return;
}

void __diag__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.diag"));

    auto v = frame.get(nargs[0]);
    auto tensor = TensorData::diag(v);
    frame.set(self, tensor);
    return;
}

void __zeros__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.zeros"));

    auto shapeArg = frame.get(nargs[0]);
    std::vector<size_t> shape;

    if (shapeArg->type()->code() == TypeCode::Array) {
        auto arrayData = std::dynamic_pointer_cast<ArrayData>(shapeArg);
        for (const auto &element : arrayData->raw()) {
            if (!element->type()->equals(Type::Int32())) {
                throw CamelRuntimeException(
                    RuntimeExceptionCode::UnknownError,
                    std::format("tensor.zeros expects Array of Int32 arguments"));
            }
            auto intData = std::dynamic_pointer_cast<Int32Data>(element);
            shape.push_back(static_cast<size_t>(intData->data()));
        }
    } else {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.zeros expects List or Array argument"));
    }

    auto tensor = TensorData::zeros(shape);
    frame.set(self, tensor);
    return;
}

void __ones__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.ones"));

    auto shapeArg = frame.get(nargs[0]);
    std::vector<size_t> shape;

    if (shapeArg->type()->code() == TypeCode::Array) {
        auto arrayData = std::dynamic_pointer_cast<ArrayData>(shapeArg);
        for (const auto &element : arrayData->raw()) {
            if (!element->type()->equals(Type::Int32())) {
                throw CamelRuntimeException(
                    RuntimeExceptionCode::UnknownError,
                    std::format("tensor.ones expects Array of Int32 arguments"));
            }
            auto intData = std::dynamic_pointer_cast<Int32Data>(element);
            shape.push_back(static_cast<size_t>(intData->data()));
        }
    } else {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.ones expects List or Array argument"));
    }

    auto tensor = TensorData::ones(shape);
    frame.set(self, tensor);
    return;
}

void __linspace__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.linspace"));

    auto start = frame.get(nargs[0]);
    auto stop = frame.get(nargs[1]);
    auto num = frame.get(nargs[2]);

    if (!start->type()->equals(Type::Float()) || !stop->type()->equals(Type::Float()) ||
        !num->type()->equals(Type::Int32())) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.linspace expects (Float, Float, Int32) arguments"));
    }

    auto startData = std::dynamic_pointer_cast<FloatData>(start);
    auto stopData = std::dynamic_pointer_cast<FloatData>(stop);
    auto numData = std::dynamic_pointer_cast<Int32Data>(num);
    auto tensor = TensorData::linspace(startData->data(), stopData->data(), numData->data());
    frame.set(self, tensor);
    return;
}

void __arange__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.arange"));

    auto start = frame.get(nargs[0]);
    auto stop = frame.get(nargs[1]);

    if (!start->type()->equals(Type::Float()) || !stop->type()->equals(Type::Float())) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.arange expects Float arguments"));
    }

    data_ptr_t tensor;
    if (nargs.size == 2) {
        auto startData = std::dynamic_pointer_cast<FloatData>(start);
        auto stopData = std::dynamic_pointer_cast<FloatData>(stop);
        tensor = TensorData::arange(startData->data(), stopData->data());
    } else {
        auto step = frame.get(nargs[2]);
        if (!step->type()->equals(Type::Float())) {
            throw CamelRuntimeException(
                RuntimeExceptionCode::UnknownError,
                std::format("tensor.arange step expects Float argument"));
        }
        auto startData = std::dynamic_pointer_cast<FloatData>(start);
        auto stopData = std::dynamic_pointer_cast<FloatData>(stop);
        auto stepData = std::dynamic_pointer_cast<FloatData>(step);
        tensor = TensorData::arange(startData->data(), stopData->data(), stepData->data());
    }

    frame.set(self, tensor);
    return;
}

// Tensor arithmetic operations
void __tensor_add__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(auto l = Logger(); l.in("TensorOps").debug("Calling tensor.add"));

    EXEC_WHEN_DEBUG(auto l = Logger();
                    l.in("TensorOps").debug("tensor.add called with {} arguments", nargs.size));

    if (nargs.size != 2) {
        EXEC_WHEN_DEBUG(auto l = Logger(); l.in("TensorOps").debug("Argument count check failed"));
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.add expects 2 arguments, got {}", nargs.size));
    }

    EXEC_WHEN_DEBUG(auto l = Logger(); l.in("TensorOps").debug("Getting first argument"));
    auto tensor1 = frame.get(nargs[0]);
    EXEC_WHEN_DEBUG(auto l = Logger(); l.in("TensorOps").debug("Getting second argument"));
    auto tensor2 = frame.get(nargs[1]);

    EXEC_WHEN_DEBUG(auto l = Logger(); l.in("TensorOps")
                                           .debug(
                                               "tensor1 type: {}, tensor2 type: {}",
                                               tensor1->type()->toString(),
                                               tensor2->type()->toString()));

    if (tensor1->type()->code() != TensorType::typeCode()) {
        EXEC_WHEN_DEBUG(
            auto l = Logger();
            l.in("TensorOps")
                .error("First argument is not a tensor, type: {}", tensor1->type()->toString()));
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format(
                "tensor.add expects first argument to be Tensor, got {}",
                tensor1->type()->toString()));
    }

    if (tensor2->type()->code() != TensorType::typeCode()) {
        EXEC_WHEN_DEBUG(
            auto l = Logger();
            l.in("TensorOps")
                .error("Second argument is not a tensor, type: {}", tensor2->type()->toString()));
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format(
                "tensor.add expects second argument to be Tensor, got {}",
                tensor2->type()->toString()));
    }

    EXEC_WHEN_DEBUG(
        auto l = Logger();
        l.in("TensorOps").debug("Both arguments are Tensor types, performing addition"));

    try {
        EXEC_WHEN_DEBUG(auto l = Logger();
                        l.in("TensorOps").debug("Calling TensorData::add method"));
        auto result = std::dynamic_pointer_cast<TensorData>(tensor1)->add(tensor2);
        EXEC_WHEN_DEBUG(auto l = Logger();
                        l.in("TensorOps").debug("Addition completed successfully"));
        frame.set(self, result);
    } catch (const std::exception &e) {
        EXEC_WHEN_DEBUG(auto l = Logger();
                        l.in("TensorOps").error("Exception during tensor addition: {}", e.what()));
        throw;
    }

    EXEC_WHEN_DEBUG(auto l = Logger(); l.in("TensorOps").debug("Returning from tensor.add"));
    return;
}
void __tensor_subtract__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.subtract"));

    auto tensor1 = frame.get(nargs[0]);
    auto tensor2 = frame.get(nargs[1]);

    if (tensor1->type()->code() != TensorType::typeCode() ||
        tensor2->type()->code() != TensorType::typeCode()) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.subtract expects Tensor arguments"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor1)->subtract(tensor2);
    frame.set(self, result);
    return;
}

void __tensor_multiply__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.multiply"));

    auto tensor1 = frame.get(nargs[0]);
    auto tensor2 = frame.get(nargs[1]);

    bool first_is_tensor = (tensor1->type()->code() == TensorType::typeCode());
    bool second_is_tensor = (tensor2->type()->code() == TensorType::typeCode());

    if (!first_is_tensor && !second_is_tensor) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.multiply expects at least one Tensor argument"));
    }

    if (first_is_tensor) {
        auto tensor_data = std::dynamic_pointer_cast<TensorData>(tensor1);
        auto result = tensor_data->multiply(tensor2);
        frame.set(self, result);
        return;
    }

    if (second_is_tensor) {
        auto tensor_data = std::dynamic_pointer_cast<TensorData>(tensor2);
        auto result = tensor_data->multiply(tensor1);
        frame.set(self, result);
        return;
    }

    throw CamelRuntimeException(
        RuntimeExceptionCode::UnknownError,
        std::format("tensor.multiply expects Tensor arguments"));
}

void __tensor_divide__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.divide"));

    auto tensor1 = frame.get(nargs[0]);
    auto tensor2 = frame.get(nargs[1]);

    if (tensor1->type()->code() != TensorType::typeCode() ||
        tensor2->type()->code() != TensorType::typeCode()) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.divide expects Tensor arguments"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor1)->divide(tensor2);
    frame.set(self, result);
    return;
}

// Tensor shape transformation operations
void __tensor_reshape__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.reshape"));

    auto tensor = frame.get(nargs[0]);
    auto shape = frame.get(nargs[1]);

    if (tensor->type()->code() != TensorType::typeCode()) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.reshape expects Tensor argument"));
    }

    // TODO: Implement shape argument parsing
    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->reshape({});
    frame.set(self, result);
    return;
}

void __tensor_transpose__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.transpose"));

    auto tensor = frame.get(nargs[0]);

    if (tensor->type()->code() != TensorType::typeCode()) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.transpose expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->transpose();
    frame.set(self, result);
    return;
}

void __tensor_flatten__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.flatten"));

    auto tensor_data = frame.get(nargs[0]);

    if (tensor_data->type()->code() != TensorType::typeCode()) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.flatten expects Tensor argument"));
    }

    auto tensor_ptr = std::dynamic_pointer_cast<TensorData>(tensor_data);
    if (!tensor_ptr) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("Failed to cast TensorData"));
    }

    auto result_tensor = tensor_ptr->flatten();

    frame.set(self, result_tensor);
    return;
}

// Tensor combination operations
void __tensor_concat__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.concat"));

    auto tensor1 = frame.get(nargs[0]);
    auto tensor2 = frame.get(nargs[1]);

    if (tensor1->type()->code() != TensorType::typeCode() ||
        tensor2->type()->code() != TensorType::typeCode()) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.concat expects Tensor arguments"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor1)->concat(tensor2);
    frame.set(self, result);
    return;
}

void __tensor_stack__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.stack"));

    auto tensor1 = frame.get(nargs[0]);
    auto tensor2 = frame.get(nargs[1]);

    if (tensor1->type()->code() != TensorType::typeCode() ||
        tensor2->type()->code() != TensorType::typeCode()) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.stack expects Tensor arguments"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor1)->stack(tensor2);
    frame.set(self, result);
    return;
}

// Tensor statistical operations
void __tensor_sum__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.sum"));

    auto tensor = frame.get(nargs[0]);

    if (tensor->type()->code() != TensorType::typeCode()) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.sum expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->sum();
    frame.set(self, result);
    return;
}

void __tensor_mean__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.mean"));

    auto tensor = frame.get(nargs[0]);

    if (tensor->type()->code() != TensorType::typeCode()) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.mean expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->mean();
    frame.set(self, result);
    return;
}

void __tensor_min__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.min"));

    auto tensor = frame.get(nargs[0]);

    if (tensor->type()->code() != TensorType::typeCode()) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.min expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->min();
    frame.set(self, result);
    return;
}

void __tensor_max__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.max"));

    auto tensor = frame.get(nargs[0]);

    if (tensor->type()->code() != TensorType::typeCode()) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.max expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->max();
    frame.set(self, result);
    return;
}

void __tensor_argmin__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.argmin"));

    auto tensor = frame.get(nargs[0]);

    if (tensor->type()->code() != TensorType::typeCode()) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.argmin expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->argmin();
    frame.set(self, result);
    return;
}

void __tensor_argmax__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.argmax"));

    auto tensor = frame.get(nargs[0]);

    if (tensor->type()->code() != TensorType::typeCode()) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.argmax expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->argmax();
    frame.set(self, result);
    return;
}

void __tensor_std__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.std"));

    auto tensor = frame.get(nargs[0]);

    if (tensor->type()->code() != TensorType::typeCode()) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.std expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->std();
    frame.set(self, result);
    return;
}

// Tensor norm calculations
void __tensor_norm_l1__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.norm_l1"));

    auto tensor = frame.get(nargs[0]);

    if (tensor->type()->code() != TensorType::typeCode()) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.norm_l1 expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->norm_l1();
    frame.set(self, result);
    return;
}

void __tensor_norm_l2__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.norm_l2"));

    auto tensor = frame.get(nargs[0]);

    if (tensor->type()->code() != TensorType::typeCode()) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.norm_l2 expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->norm_l2();
    frame.set(self, result);
    return;
}

void __tensor_norm_squared_l2__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.norm_squared_l2"));

    auto tensor = frame.get(nargs[0]);

    if (tensor->type()->code() != TensorType::typeCode()) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.norm_squared_l2 expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->norm_squared_l2();
    frame.set(self, result);
    return;
}

// Tensor mathematical functions
void __tensor_sin__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.sin"));

    auto tensor = frame.get(nargs[0]);

    if (tensor->type()->code() != TensorType::typeCode()) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.sin expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->sin();
    frame.set(self, result);
    return;
}

void __tensor_cos__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.cos"));

    auto tensor = frame.get(nargs[0]);

    if (tensor->type()->code() != TensorType::typeCode()) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.cos expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->cos();
    frame.set(self, result);
    return;
}

void __tensor_exp__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.exp"));

    auto tensor = frame.get(nargs[0]);

    if (tensor->type()->code() != TensorType::typeCode()) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.exp expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->exp();
    frame.set(self, result);
    return;
}

void __tensor_log__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.log"));

    auto tensor = frame.get(nargs[0]);

    if (tensor->type()->code() != TensorType::typeCode()) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.log expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->log();
    frame.set(self, result);
    return;
}

void __tensor_sqrt__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.sqrt"));

    auto tensor = frame.get(nargs[0]);

    if (tensor->type()->code() != TensorType::typeCode()) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.sqrt expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->sqrt();
    frame.set(self, result);
    return;
}

// Tensor hyperbolic functions
void __tensor_sinh__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.sinh"));

    auto tensor = frame.get(nargs[0]);

    if (tensor->type()->code() != TensorType::typeCode()) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.sinh expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->sinh();
    frame.set(self, result);
    return;
}

void __tensor_cosh__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.cosh"));

    auto tensor = frame.get(nargs[0]);

    if (tensor->type()->code() != TensorType::typeCode()) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.cosh expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->cosh();
    frame.set(self, result);
    return;
}

void __tensor_tanh__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.tanh"));

    auto tensor = frame.get(nargs[0]);

    if (tensor->type()->code() != TensorType::typeCode()) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.tanh expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->tanh();
    frame.set(self, result);
    return;
}

// Tensor shape operation
void __shape__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.shape"));

    auto tensor = frame.get(nargs[0]);

    if (tensor->type()->code() != TensorType::typeCode()) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.shape expects Tensor argument"));
    }

    auto tensor_data = std::dynamic_pointer_cast<TensorData>(tensor);
    auto shape = tensor_data->shape();

    // Convert shape to a list of integers
    data_vec_t shape_list;
    for (auto dim : shape) {
        shape_list.push_back(std::make_shared<Int32Data>(dim));
    }

    auto result = std::make_shared<ListData>(std::move(shape_list));
    frame.set(self, result);
    return;
}

void __tensor_idx__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor index"));

    auto tensor = frame.get(nargs[0]);
    auto index = frame.get(nargs[1]);

    if (tensor->type()->code() != TensorType::typeCode()) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format(
                "tensor index expects first argument to be Tensor, got {}",
                tensor->type()->toString()));
    }

    if (!index->type()->equals(Type::Int32())) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format(
                "tensor index expects second argument to be Int32, got {}",
                index->type()->toString()));
    }

    auto tensorData = std::dynamic_pointer_cast<TensorData>(tensor);
    auto indexData = std::dynamic_pointer_cast<Int32Data>(index);

    auto result = tensorData->at({static_cast<size_t>(indexData->data())});
    frame.set(self, result);
    return;
}

void __tensor_idx2d__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor 2d index"));

    auto tensor = frame.get(nargs[0]);
    auto rowIndex = frame.get(nargs[1]);
    auto colIndex = frame.get(nargs[2]);

    if (tensor->type()->code() != TensorType::typeCode()) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format(
                "tensor 2d index expects first argument to be Tensor, got {}",
                tensor->type()->toString()));
    }

    if (!rowIndex->type()->equals(Type::Int32()) || !colIndex->type()->equals(Type::Int32())) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format(
                "tensor 2d index expects second and third arguments to be Int32, got {} and {}",
                rowIndex->type()->toString(),
                colIndex->type()->toString()));
    }

    auto tensorData = std::dynamic_pointer_cast<TensorData>(tensor);
    auto rowIndexData = std::dynamic_pointer_cast<Int32Data>(rowIndex);
    auto colIndexData = std::dynamic_pointer_cast<Int32Data>(colIndex);

    auto result = tensorData->at(
        {static_cast<size_t>(rowIndexData->data()), static_cast<size_t>(colIndexData->data())});
    frame.set(self, result);
    return;
}
