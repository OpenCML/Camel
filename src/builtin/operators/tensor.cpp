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
 * Updated: Feb. 06, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "tensor.h"
#include "utils/log.h"

#include "builtin/datas/tensor.h"
#include "builtin/types/tensor.h"

// void __tensor_eye__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto n = frame.get(nargs[0]);
//     auto intData = tt::as_shared<IntData>(n);
//     auto tensor = TensorData::eye(intData->data());
//     frame.set(self, tensor);
// }

// void __tensor_diag__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto v = frame.get(nargs[0]);
//     auto tensor = TensorData::diag(v);
//     frame.set(self, tensor);
// }

// void __tensor_zeros__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto shapeArg = frame.get(nargs[0]);
//     std::vector<size_t> shape;

//     auto arrayData = tt::as_shared<ArrayData>(shapeArg);
//     for (const auto &element : arrayData->raw()) {
//         auto intData = tt::as_shared<IntData>(element);
//         shape.push_back(static_cast<size_t>(intData->data()));
//     }

//     auto tensor = TensorData::zeros(shape);
//     frame.set(self, tensor);
// }

// void __tensor_ones__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto shapeArg = frame.get(nargs[0]);
//     std::vector<size_t> shape;

//     auto arrayData = tt::as_shared<ArrayData>(shapeArg);
//     for (const auto &element : arrayData->raw()) {
//         auto intData = tt::as_shared<IntData>(element);
//         shape.push_back(static_cast<size_t>(intData->data()));
//     }

//     auto tensor = TensorData::ones(shape);
//     frame.set(self, tensor);
// }

// void __tensor_linspace__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto start = frame.get(nargs[0]);
//     auto stop = frame.get(nargs[1]);
//     auto num = frame.get(nargs[2]);

//     auto startData = tt::as_shared<FloatData>(start);
//     auto stopData = tt::as_shared<FloatData>(stop);
//     auto numData = tt::as_shared<IntData>(num);

//     auto tensor = TensorData::linspace(startData->data(), stopData->data(), numData->data());
//     frame.set(self, tensor);
// }

// void __tensor_arange__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto start = frame.get(nargs[0]);
//     auto stop = frame.get(nargs[1]);

//     data_ptr_t tensor;
//     if (nargs.size == 2) {
//         auto startData = tt::as_shared<FloatData>(start);
//         auto stopData = tt::as_shared<FloatData>(stop);
//         tensor = TensorData::arange(startData->data(), stopData->data());
//     } else {
//         auto step = frame.get(nargs[2]);
//         auto startData = tt::as_shared<FloatData>(start);
//         auto stopData = tt::as_shared<FloatData>(stop);
//         auto stepData = tt::as_shared<FloatData>(step);
//         tensor = TensorData::arange(startData->data(), stopData->data(), stepData->data());
//     }

//     frame.set(self, tensor);
// }

// void __tensor_random__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto shapeArg = frame.get(nargs[0]);
//     std::vector<size_t> shape;

//     auto arrayData = tt::as_shared<ArrayData>(shapeArg);
//     for (const auto &element : arrayData->raw()) {
//         auto intData = tt::as_shared<IntData>(element);
//         shape.push_back(static_cast<size_t>(intData->data()));
//     }

//     double lower = 0.0;
//     double upper = 1.0;

//     if (nargs.size > 1) {
//         auto lowerArg = frame.get(nargs[1]);
//         lower = tt::as_shared<FloatData>(lowerArg)->data();
//     }

//     if (nargs.size > 2) {
//         auto upperArg = frame.get(nargs[2]);
//         upper = tt::as_shared<FloatData>(upperArg)->data();
//     }

//     auto tensor = TensorData::random(shape, lower, upper);
//     frame.set(self, tensor);
// }

// void __tensor_randn__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto shapeArg = frame.get(nargs[0]);
//     std::vector<size_t> shape;

//     auto arrayData = tt::as_shared<ArrayData>(shapeArg);
//     for (const auto &element : arrayData->raw()) {
//         auto intData = tt::as_shared<IntData>(element);
//         shape.push_back(static_cast<size_t>(intData->data()));
//     }

//     double mean = 0.0;
//     double stddev = 1.0;

//     if (nargs.size > 1) {
//         auto meanArg = frame.get(nargs[1]);
//         mean = tt::as_shared<FloatData>(meanArg)->data();
//     }

//     if (nargs.size > 2) {
//         auto stddevArg = frame.get(nargs[2]);
//         stddev = tt::as_shared<FloatData>(stddevArg)->data();
//     }

//     auto tensor = TensorData::randn(shape, mean, stddev);
//     frame.set(self, tensor);
// }

// // Tensor arithmetic operations
// void __tensor_add__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto tensor1 = frame.get(nargs[0]);
//     auto tensor2 = frame.get(nargs[1]);
//     auto result = tt::as_shared<TensorData>(tensor1)->add(tensor2);
//     frame.set(self, result);
// }

// void __tensor_subtract__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto tensor1 = frame.get(nargs[0]);
//     auto tensor2 = frame.get(nargs[1]);
//     auto result = tt::as_shared<TensorData>(tensor1)->subtract(tensor2);
//     frame.set(self, result);
// }

// void __tensor_multiply__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto lhs = frame.get(nargs[0]);
//     auto rhs = frame.get(nargs[1]);

//     // 如果第一个是 Tensor
//     if (lhs->type()->code() == TensorType::typeCode()) {
//         auto tensor_data = tt::as_shared<TensorData>(lhs);
//         auto result = tensor_data->multiply(rhs);
//         frame.set(self, result);
//         return;
//     }

//     // 如果第一个不是 Tensor，那么第二个必须是 Tensor
//     // 类型校验系统会保证这一点
//     auto tensor_data = tt::as_shared<TensorData>(rhs);
//     auto result = tensor_data->multiply(lhs); // Multiply is commutative for scalar-tensor
//     frame.set(self, result);
// }

// void __tensor_matmul__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto tensor1 = frame.get(nargs[0]);
//     auto tensor2 = frame.get(nargs[1]);

//     auto result = tt::as_shared<TensorData>(tensor1)->matmul(tensor2);
//     frame.set(self, result);
// }

// void __tensor_divide__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto tensor1 = frame.get(nargs[0]);
//     auto tensor2 = frame.get(nargs[1]);

//     auto result = tt::as_shared<TensorData>(tensor1)->divide(tensor2);
//     frame.set(self, result);
// }

// // Tensor shape transformation operations
// void __tensor_reshape__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto tensor = frame.get(nargs[0]);
//     auto shape = frame.get(nargs[1]);

//     // TODO: Implement shape argument parsing
//     auto result = tt::as_shared<TensorData>(tensor)->reshape({});
//     frame.set(self, result);
//     return;
// }

// void __tensor_transpose__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto tensor = frame.get(nargs[0]);
//     auto result = tt::as_shared<TensorData>(tensor)->transpose();
//     frame.set(self, result);
// }

// void __tensor_flatten__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto tensor_data = frame.get(nargs[0]);
//     auto result_tensor = tt::as_shared<TensorData>(tensor_data)->flatten();
//     frame.set(self, result_tensor);
// }

// // Tensor combination operations
// void __tensor_concat__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto tensor1 = frame.get(nargs[0]);
//     auto tensor2 = frame.get(nargs[1]);
//     auto axis_data = frame.get(nargs[2]);
//     auto axis = tt::as_shared<IntData>(axis_data)->data();
//     auto result = tt::as_shared<TensorData>(tensor1)->concat(tensor2, axis);
//     frame.set(self, result);
// }

// void __tensor_stack__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto tensor1 = frame.get(nargs[0]);
//     auto tensor2 = frame.get(nargs[1]);
//     auto result = tt::as_shared<TensorData>(tensor1)->stack(tensor2);
//     frame.set(self, result);
// }

// // Tensor statistical operations
// void __tensor_sum__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto tensor = frame.get(nargs[0]);
//     auto result = tt::as_shared<TensorData>(tensor)->sum();
//     frame.set(self, result);
// }

// void __tensor_mean__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto tensor = frame.get(nargs[0]);
//     auto result = tt::as_shared<TensorData>(tensor)->mean();
//     frame.set(self, result);
// }

// void __tensor_min__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto tensor = frame.get(nargs[0]);
//     auto result = tt::as_shared<TensorData>(tensor)->min();
//     frame.set(self, result);
// }

// void __tensor_max__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto tensor = frame.get(nargs[0]);
//     auto result = tt::as_shared<TensorData>(tensor)->max();
//     frame.set(self, result);
// }

// void __tensor_argmin__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto tensor = frame.get(nargs[0]);
//     auto result = tt::as_shared<TensorData>(tensor)->argmin();
//     frame.set(self, result);
// }

// void __tensor_argmax__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto tensor = frame.get(nargs[0]);
//     auto result = tt::as_shared<TensorData>(tensor)->argmax();
//     frame.set(self, result);
// }

// void __tensor_std__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto tensor = frame.get(nargs[0]);
//     auto result = tt::as_shared<TensorData>(tensor)->std();
//     frame.set(self, result);
// }

// // Tensor norm calculations
// void __tensor_norm_l1__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto tensor = frame.get(nargs[0]);
//     auto result = tt::as_shared<TensorData>(tensor)->norm_l1();
//     frame.set(self, result);
// }

// void __tensor_norm_l2__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto tensor = frame.get(nargs[0]);
//     auto result = tt::as_shared<TensorData>(tensor)->norm_l2();
//     frame.set(self, result);
// }

// void __tensor_norm_squared_l2__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto tensor = frame.get(nargs[0]);
//     auto result = tt::as_shared<TensorData>(tensor)->norm_squared_l2();
//     frame.set(self, result);
// }

// // Tensor mathematical functions
// void __tensor_sin__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto tensor = frame.get(nargs[0]);
//     auto result = tt::as_shared<TensorData>(tensor)->sin();
//     frame.set(self, result);
// }

// void __tensor_cos__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto tensor = frame.get(nargs[0]);
//     auto result = tt::as_shared<TensorData>(tensor)->cos();
//     frame.set(self, result);
// }

// void __tensor_exp__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto tensor = frame.get(nargs[0]);
//     auto result = tt::as_shared<TensorData>(tensor)->exp();
//     frame.set(self, result);
// }

// void __tensor_log__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto tensor = frame.get(nargs[0]);
//     auto result = tt::as_shared<TensorData>(tensor)->log();
//     frame.set(self, result);
// }

// void __tensor_sqrt__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto tensor = frame.get(nargs[0]);
//     auto result = tt::as_shared<TensorData>(tensor)->sqrt();
//     frame.set(self, result);
// }

// void __tensor_pow__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto self_data = frame.get(nargs[0]);
//     auto tensor = tt::as_shared<TensorData>(self_data);
//     auto exponent = frame.get(nargs[1]);
//     auto result = tensor->pow(exponent);
//     frame.set(self, result);
// }

// void __tensor_matpow__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto self_data = frame.get(nargs[0]);
//     auto tensor = tt::as_shared<TensorData>(self_data);
//     auto exponent_data = frame.get(nargs[1]);
//     auto intData = tt::as_shared<IntData>(exponent_data);
//     auto result = tensor->matpow(intData->data());
//     frame.set(self, result);
// }

// // Tensor hyperbolic functions
// void __tensor_sinh__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto tensor = frame.get(nargs[0]);
//     auto result = tt::as_shared<TensorData>(tensor)->sinh();
//     frame.set(self, result);
// }

// void __tensor_cosh__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto tensor = frame.get(nargs[0]);
//     auto result = tt::as_shared<TensorData>(tensor)->cosh();
//     frame.set(self, result);
// }

// void __tensor_tanh__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto tensor = frame.get(nargs[0]);
//     auto result = tt::as_shared<TensorData>(tensor)->tanh();
//     frame.set(self, result);
// }

// // Tensor shape operation
// void __tensor_shape__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto tensor = frame.get(nargs[0]);
//     auto tensor_data = tt::as_shared<TensorData>(tensor);
//     auto shape = tensor_data->shape();

//     data_vec_t shape_array;
//     for (auto dim : shape) {
//         shape_array.push_back(std::make_shared<IntData>(dim));
//     }

//     auto result = ArrayData::from(ArrayType::create(Type::Int()), std::move(shape_array));
//     frame.set(self, result);
// }

// void __tensor_idx__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto tensor = frame.get(nargs[0]);
//     auto index = frame.get(nargs[1]);

//     auto tensorData = tt::as_shared<TensorData>(tensor);
//     auto indexData = tt::as_shared<IntData>(index);

//     auto result = tensorData->at({static_cast<size_t>(indexData->data())});
//     frame.set(self, result);
// }

// void __tensor_idx2d__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto tensor = frame.get(nargs[0]);
//     auto tensorData = tt::as_shared<TensorData>(tensor);

//     if (nargs.size == 3) {
//         auto rowIndex = frame.get(nargs[1]);
//         auto colIndex = frame.get(nargs[2]);

//         auto rowIndexData = tt::as_shared<IntData>(rowIndex);
//         auto colIndexData = tt::as_shared<IntData>(colIndex);

//         auto result = tensorData->at(
//             {static_cast<size_t>(rowIndexData->data()),
//             static_cast<size_t>(colIndexData->data())});
//         frame.set(self, result);
//     } else if (nargs.size == 5) {
//         auto arg1 = frame.get(nargs[1]);
//         auto arg2 = frame.get(nargs[2]);
//         auto arg3 = frame.get(nargs[3]);
//         auto arg4 = frame.get(nargs[4]);

//         auto startData = tt::as_shared<IntData>(arg1);
//         auto endData = tt::as_shared<IntData>(arg2);
//         auto rowData = tt::as_shared<IntData>(arg3);

//         auto result = tensorData->slice(
//             static_cast<size_t>(startData->data()),
//             static_cast<size_t>(endData->data()),
//             static_cast<size_t>(rowData->data()));
//         frame.set(self, result);
//     }
// }

// void __tensor_show__(
//     ArgsView &with, ArgsView &norm, Context &ctx) {
//     auto tensor = frame.get(nargs[0]);
//     auto tensor_data = tt::as_shared<TensorData>(tensor);
//     std::cout << tensor_data->toFormattedString() << std::endl;
//     frame.set(self, Data::null());
// }

// Tensor static factory methods
slot_t __tensor_eye__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __tensor_diag__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __tensor_linspace__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __tensor_arange__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __tensor_zeros__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __tensor_ones__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __tensor_random__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __tensor_randn__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }

// Tensor arithmetic operations
slot_t __tensor_add__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __tensor_subtract__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __tensor_multiply__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __tensor_matmul__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __tensor_divide__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }

// Tensor shape transformation operations
slot_t __tensor_shape__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __tensor_reshape__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __tensor_transpose__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __tensor_flatten__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }

// Tensor combination operations
slot_t __tensor_concat__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __tensor_stack__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }

// Tensor statistical operations
slot_t __tensor_sum__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __tensor_mean__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __tensor_min__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __tensor_max__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __tensor_argmin__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __tensor_argmax__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __tensor_std__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }

// Tensor norm calculations
slot_t __tensor_norm_l1__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __tensor_norm_l2__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __tensor_norm_squared_l2__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }

// Tensor mathematical functions
slot_t __tensor_sin__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __tensor_cos__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __tensor_exp__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __tensor_log__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __tensor_sqrt__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __tensor_pow__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __tensor_matpow__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }

// Tensor hyperbolic functions
slot_t __tensor_sinh__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __tensor_cosh__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __tensor_tanh__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }

slot_t __tensor_show__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
slot_t __to_float__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
