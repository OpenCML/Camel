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
 * Updated: Oct. 15, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "tensor.h"
#include "utils/log.h"

#include "core/data/struct/tensor.h"
#include "core/type/struct/tensor.h"

// Tensor static factory methods
OperatorReturnCode __eye__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.eye"));

    auto args = self->dataInputs();
    if (args.size() != 1) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.eye expects 1 argument, got {}", args.size()));
    }

    auto n = frame.get(args[0]);
    if (!n->type()->equals(Type::Int32())) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.eye expects Int32 argument"));
    }

    auto intData = std::dynamic_pointer_cast<Int32Data>(n);
    auto tensor = TensorData::eye(intData->data());
    frame.set(self, tensor);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __diag__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.diag"));

    auto args = self->dataInputs();
    if (args.size() != 1) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.diag expects 1 argument, got {}", args.size()));
    }

    auto v = frame.get(args[0]);
    auto tensor = TensorData::diag(v);
    frame.set(self, tensor);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __zeros__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.zeros"));

    auto args = self->dataInputs();
    if (args.size() != 1) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.zeros expects 1 argument, got {}", args.size()));
    }

    auto shapeArg = frame.get(args[0]);
    if (shapeArg->type()->code() != TypeCode::List) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.zeros expects List argument"));
    }

    auto listData = std::dynamic_pointer_cast<ListData>(shapeArg);
    std::vector<size_t> shape;

    for (size_t i = 0; i < listData->size(); ++i) {
        auto element = listData->get(i);
        if (!element->type()->equals(Type::Int32())) {
            throw CamelRuntimeException(
                RuntimeExceptionCode::UnknownError,
                std::format("tensor.zeros expects List of Int32 arguments"));
        }
        auto intData = std::dynamic_pointer_cast<Int32Data>(element);
        shape.push_back(static_cast<size_t>(intData->data()));
    }

    auto tensor = TensorData::zeros(shape);
    frame.set(self, tensor);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __ones__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.ones"));

    auto args = self->dataInputs();
    if (args.size() != 1) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.ones expects 1 argument, got {}", args.size()));
    }

    auto shapeArg = frame.get(args[0]);
    if (shapeArg->type()->code() != TypeCode::List) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.ones expects List argument"));
    }

    auto listData = std::dynamic_pointer_cast<ListData>(shapeArg);
    std::vector<size_t> shape;

    for (size_t i = 0; i < listData->size(); ++i) {
        auto element = listData->get(i);
        if (!element->type()->equals(Type::Int32())) {
            throw CamelRuntimeException(
                RuntimeExceptionCode::UnknownError,
                std::format("tensor.ones expects List of Int32 arguments"));
        }
        auto intData = std::dynamic_pointer_cast<Int32Data>(element);
        shape.push_back(static_cast<size_t>(intData->data()));
    }

    auto tensor = TensorData::ones(shape);
    frame.set(self, tensor);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __linspace__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.linspace"));

    auto args = self->dataInputs();
    if (args.size() != 3) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.linspace expects 3 arguments, got {}", args.size()));
    }

    auto start = frame.get(args[0]);
    auto stop = frame.get(args[1]);
    auto num = frame.get(args[2]);

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
    return OperatorReturnCode::OK;
}

OperatorReturnCode __arange__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.arange"));

    auto args = self->dataInputs();
    if (args.size() < 2 || args.size() > 3) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.arange expects 2 or 3 arguments, got {}", args.size()));
    }

    auto start = frame.get(args[0]);
    auto stop = frame.get(args[1]);

    if (!start->type()->equals(Type::Float()) || !stop->type()->equals(Type::Float())) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.arange expects Float arguments"));
    }

    data_ptr_t tensor;
    if (args.size() == 2) {
        auto startData = std::dynamic_pointer_cast<FloatData>(start);
        auto stopData = std::dynamic_pointer_cast<FloatData>(stop);
        tensor = TensorData::arange(startData->data(), stopData->data());
    } else {
        auto step = frame.get(args[2]);
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
    return OperatorReturnCode::OK;
}

// Tensor arithmetic operations
OperatorReturnCode __tensor_add__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(auto l = Logger(); l.in("TensorOps").debug("Calling tensor.add"));

    auto args = self->dataInputs();
    EXEC_WHEN_DEBUG(auto l = Logger(); l.in("TensorOps").debug("tensor.add called with {} arguments", args.size()));

    if (args.size() != 2) {
        EXEC_WHEN_DEBUG(auto l = Logger(); l.in("TensorOps").debug("Argument count check failed"));
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.add expects 2 arguments, got {}", args.size()));
    }

    EXEC_WHEN_DEBUG(auto l = Logger(); l.in("TensorOps").debug("Getting first argument"));
    auto tensor1 = frame.get(args[0]);
    EXEC_WHEN_DEBUG(auto l = Logger(); l.in("TensorOps").debug("Getting second argument"));
    auto tensor2 = frame.get(args[1]);

    EXEC_WHEN_DEBUG(auto l = Logger(); l.in("TensorOps").debug("tensor1 type: {}, tensor2 type: {}", 
        tensor1->type()->toString(), tensor2->type()->toString()));

    // 检查类型是否为Tensor类型
    if (tensor1->type()->code() != TypeCode::Tensor) {
        EXEC_WHEN_DEBUG(auto l = Logger(); l.in("TensorOps").error("First argument is not a tensor, type: {}", tensor1->type()->toString()));
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.add expects first argument to be Tensor, got {}", 
                tensor1->type()->toString()));
    }

    if (tensor2->type()->code() != TypeCode::Tensor) {
        EXEC_WHEN_DEBUG(auto l = Logger(); l.in("TensorOps").error("Second argument is not a tensor, type: {}", tensor2->type()->toString()));
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.add expects second argument to be Tensor, got {}", 
                tensor2->type()->toString()));
    }

    EXEC_WHEN_DEBUG(auto l = Logger(); l.in("TensorOps").debug("Both arguments are Tensor types, performing addition"));

    try {
        EXEC_WHEN_DEBUG(auto l = Logger(); l.in("TensorOps").debug("Calling TensorData::add method"));
        auto result = std::dynamic_pointer_cast<TensorData>(tensor1)->add(tensor2);
        EXEC_WHEN_DEBUG(auto l = Logger(); l.in("TensorOps").debug("Addition completed successfully"));
        frame.set(self, result);
    } catch (const std::exception& e) {
        EXEC_WHEN_DEBUG(auto l = Logger(); l.in("TensorOps").error("Exception during tensor addition: {}", e.what()));
        throw;
    }
    
    EXEC_WHEN_DEBUG(auto l = Logger(); l.in("TensorOps").debug("Returning from tensor.add"));
    return OperatorReturnCode::OK;
}
OperatorReturnCode __tensor_subtract__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.subtract"));

    auto args = self->dataInputs();
    if (args.size() != 2) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.subtract expects 2 arguments, got {}", args.size()));
    }

    auto tensor1 = frame.get(args[0]);
    auto tensor2 = frame.get(args[1]);

    if (tensor1->type()->code() != TypeCode::Tensor ||
        tensor2->type()->code() != TypeCode::Tensor) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.subtract expects Tensor arguments"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor1)->subtract(tensor2);
    frame.set(self, result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __tensor_multiply__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.multiply"));

    auto args = self->dataInputs();
    if (args.size() != 2) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.multiply expects 2 arguments, got {}", args.size()));
    }

    auto tensor1 = frame.get(args[0]);
    auto tensor2 = frame.get(args[1]);

    // 检查类型是否为Tensor类型
    if (tensor1->type()->code() != TypeCode::Tensor ||
        tensor2->type()->code() != TypeCode::Tensor) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.multiply expects Tensor arguments"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor1)->multiply(tensor2);
    frame.set(self, result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __tensor_divide__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.divide"));

    auto args = self->dataInputs();
    if (args.size() != 2) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.divide expects 2 arguments, got {}", args.size()));
    }

    auto tensor1 = frame.get(args[0]);
    auto tensor2 = frame.get(args[1]);

    // 检查类型是否为Tensor类型
    if (tensor1->type()->code() != TypeCode::Tensor ||
        tensor2->type()->code() != TypeCode::Tensor) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.divide expects Tensor arguments"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor1)->divide(tensor2);
    frame.set(self, result);
    return OperatorReturnCode::OK;
}

// Tensor shape transformation operations
OperatorReturnCode __tensor_reshape__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.reshape"));

    auto args = self->dataInputs();
    if (args.size() != 2) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.reshape expects 2 arguments, got {}", args.size()));
    }

    auto tensor = frame.get(args[0]);
    auto shape = frame.get(args[1]);

    if (tensor->type()->code() != TypeCode::Tensor) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.reshape expects Tensor argument"));
    }

    // TODO: Implement shape argument parsing
    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->reshape({});
    frame.set(self, result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __tensor_transpose__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.transpose"));

    auto args = self->dataInputs();
    if (args.size() != 1) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.transpose expects 1 argument, got {}", args.size()));
    }

    auto tensor = frame.get(args[0]);

    if (tensor->type()->code() != TypeCode::Tensor) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.transpose expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->transpose();
    frame.set(self, result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __tensor_flatten__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.flatten"));

    auto args = self->dataInputs();
    if (args.size() != 1) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.flatten expects 1 argument, got {}", args.size()));
    }

    auto tensor = frame.get(args[0]);

    if (tensor->type()->code() != TypeCode::Tensor) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.flatten expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->flatten();
    frame.set(self, result);
    return OperatorReturnCode::OK;
}

// Tensor combination operations
OperatorReturnCode __tensor_concat__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.concat"));

    auto args = self->dataInputs();
    if (args.size() != 2) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.concat expects 2 arguments, got {}", args.size()));
    }

    auto tensor1 = frame.get(args[0]);
    auto tensor2 = frame.get(args[1]);

    if (tensor1->type()->code() != TypeCode::Tensor ||
        tensor2->type()->code() != TypeCode::Tensor) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.concat expects Tensor arguments"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor1)->concat(tensor2);
    frame.set(self, result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __tensor_stack__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.stack"));

    auto args = self->dataInputs();
    if (args.size() != 2) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.stack expects 2 arguments, got {}", args.size()));
    }

    auto tensor1 = frame.get(args[0]);
    auto tensor2 = frame.get(args[1]);

    if (tensor1->type()->code() != TypeCode::Tensor ||
        tensor2->type()->code() != TypeCode::Tensor) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.stack expects Tensor arguments"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor1)->stack(tensor2);
    frame.set(self, result);
    return OperatorReturnCode::OK;
}

// Tensor statistical operations
OperatorReturnCode __tensor_sum__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.sum"));

    auto args = self->dataInputs();
    if (args.size() != 1) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.sum expects 1 argument, got {}", args.size()));
    }

    auto tensor = frame.get(args[0]);

    if (tensor->type()->code() != TypeCode::Tensor) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.sum expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->sum();
    frame.set(self, result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __tensor_mean__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.mean"));

    auto args = self->dataInputs();
    if (args.size() != 1) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.mean expects 1 argument, got {}", args.size()));
    }

    auto tensor = frame.get(args[0]);

    if (tensor->type()->code() != TypeCode::Tensor) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.mean expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->mean();
    frame.set(self, result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __tensor_min__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.min"));

    auto args = self->dataInputs();
    if (args.size() != 1) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.min expects 1 argument, got {}", args.size()));
    }

    auto tensor = frame.get(args[0]);

    if (tensor->type()->code() != TypeCode::Tensor) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.min expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->min();
    frame.set(self, result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __tensor_max__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.max"));

    auto args = self->dataInputs();
    if (args.size() != 1) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.max expects 1 argument, got {}", args.size()));
    }

    auto tensor = frame.get(args[0]);

    if (tensor->type()->code() != TypeCode::Tensor) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.max expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->max();
    frame.set(self, result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __tensor_argmin__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.argmin"));

    auto args = self->dataInputs();
    if (args.size() != 1) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.argmin expects 1 argument, got {}", args.size()));
    }

    auto tensor = frame.get(args[0]);

    if (tensor->type()->code() != TypeCode::Tensor) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.argmin expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->argmin();
    frame.set(self, result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __tensor_argmax__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.argmax"));

    auto args = self->dataInputs();
    if (args.size() != 1) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.argmax expects 1 argument, got {}", args.size()));
    }

    auto tensor = frame.get(args[0]);

    if (tensor->type()->code() != TypeCode::Tensor) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.argmax expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->argmax();
    frame.set(self, result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __tensor_std__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.std"));

    auto args = self->dataInputs();
    if (args.size() != 1) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.std expects 1 argument, got {}", args.size()));
    }

    auto tensor = frame.get(args[0]);

    if (tensor->type()->code() != TypeCode::Tensor) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.std expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->std();
    frame.set(self, result);
    return OperatorReturnCode::OK;
}

// Tensor norm calculations
OperatorReturnCode __tensor_norm_l1__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.norm_l1"));

    auto args = self->dataInputs();
    if (args.size() != 1) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.norm_l1 expects 1 argument, got {}", args.size()));
    }

    auto tensor = frame.get(args[0]);

    if (tensor->type()->code() != TypeCode::Tensor) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.norm_l1 expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->norm_l1();
    frame.set(self, result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __tensor_norm_l2__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.norm_l2"));

    auto args = self->dataInputs();
    if (args.size() != 1) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.norm_l2 expects 1 argument, got {}", args.size()));
    }

    auto tensor = frame.get(args[0]);

    if (tensor->type()->code() != TypeCode::Tensor) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.norm_l2 expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->norm_l2();
    frame.set(self, result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode
__tensor_norm_squared_l2__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.norm_squared_l2"));

    auto args = self->dataInputs();
    if (args.size() != 1) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.norm_squared_l2 expects 1 argument, got {}", args.size()));
    }

    auto tensor = frame.get(args[0]);

    if (tensor->type()->code() != TypeCode::Tensor) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.norm_squared_l2 expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->norm_squared_l2();
    frame.set(self, result);
    return OperatorReturnCode::OK;
}

// Tensor mathematical functions
OperatorReturnCode __tensor_sin__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.sin"));

    auto args = self->dataInputs();
    if (args.size() != 1) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.sin expects 1 argument, got {}", args.size()));
    }

    auto tensor = frame.get(args[0]);

    if (tensor->type()->code() != TypeCode::Tensor) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.sin expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->sin();
    frame.set(self, result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __tensor_cos__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.cos"));

    auto args = self->dataInputs();
    if (args.size() != 1) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.cos expects 1 argument, got {}", args.size()));
    }

    auto tensor = frame.get(args[0]);

    if (tensor->type()->code() != TypeCode::Tensor) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.cos expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->cos();
    frame.set(self, result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __tensor_exp__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.exp"));

    auto args = self->dataInputs();
    if (args.size() != 1) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.exp expects 1 argument, got {}", args.size()));
    }

    auto tensor = frame.get(args[0]);

    if (tensor->type()->code() != TypeCode::Tensor) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.exp expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->exp();
    frame.set(self, result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __tensor_log__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.log"));

    auto args = self->dataInputs();
    if (args.size() != 1) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.log expects 1 argument, got {}", args.size()));
    }

    auto tensor = frame.get(args[0]);

    if (tensor->type()->code() != TypeCode::Tensor) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.log expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->log();
    frame.set(self, result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __tensor_sqrt__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.sqrt"));

    auto args = self->dataInputs();
    if (args.size() != 1) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.sqrt expects 1 argument, got {}", args.size()));
    }

    auto tensor = frame.get(args[0]);

    if (tensor->type()->code() != TypeCode::Tensor) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.sqrt expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->sqrt();
    frame.set(self, result);
    return OperatorReturnCode::OK;
}

// Tensor hyperbolic functions
OperatorReturnCode __tensor_sinh__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.sinh"));

    auto args = self->dataInputs();
    if (args.size() != 1) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.sinh expects 1 argument, got {}", args.size()));
    }

    auto tensor = frame.get(args[0]);

    if (tensor->type()->code() != TypeCode::Tensor) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.sinh expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->sinh();
    frame.set(self, result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __tensor_cosh__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.cosh"));

    auto args = self->dataInputs();
    if (args.size() != 1) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.cosh expects 1 argument, got {}", args.size()));
    }

    auto tensor = frame.get(args[0]);

    if (tensor->type()->code() != TypeCode::Tensor) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.cosh expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->cosh();
    frame.set(self, result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __tensor_tanh__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.tanh"));

    auto args = self->dataInputs();
    if (args.size() != 1) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.tanh expects 1 argument, got {}", args.size()));
    }

    auto tensor = frame.get(args[0]);

    if (tensor->type()->code() != TypeCode::Tensor) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::UnknownError,
            std::format("tensor.tanh expects Tensor argument"));
    }

    auto result = std::dynamic_pointer_cast<TensorData>(tensor)->tanh();
    frame.set(self, result);
    return OperatorReturnCode::OK;
}

// Tensor shape operation
OperatorReturnCode __shape__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    EXEC_WHEN_DEBUG(l.in("TensorOps").debug("Calling tensor.shape"));

    auto args = self->dataInputs();
    if (args.size() != 1) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            std::format("tensor.shape expects 1 argument, got {}", args.size()));
    }

    auto tensor = frame.get(args[0]);

    if (tensor->type()->code() != TypeCode::Tensor) {
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
    return OperatorReturnCode::OK;
}