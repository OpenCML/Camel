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
 * Updated: Oct. 31, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "tensor.h"
#include "builtin/types/tensor.h"
#include "core/data/primary.h"
#include "error/diagnostics/diagnostics.h"
#include "list.h"
#include "utils/log.h"
#include "vector.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <vector>

using namespace std;

TensorData::TensorData(const type_ptr_t &elementType, const std::vector<size_t> &shape)
    : OtherData(std::static_pointer_cast<Type>(std::make_shared<TensorType>(shape))),
      shape_(shape) {
    size_t total_size = 1;
    for (size_t dim : shape) {
        total_size *= dim;
    }
    data_.resize(total_size);
    data_.setZero();
}

TensorData::~TensorData() = default;

data_ptr_t TensorData::at(const std::vector<size_t> &index) const {
    if (index.size() != shape_.size()) {
        throw std::runtime_error("Index dimension mismatch");
    }

    size_t linear_index = 0;
    size_t multiplier = 1;

    for (size_t i = index.size(); i > 0; --i) {
        size_t dim_index = i - 1;
        if (index[dim_index] >= shape_[dim_index]) {
            throw std::runtime_error("Index out of bounds");
        }
        linear_index += index[dim_index] * multiplier;
        multiplier *= shape_[dim_index];
    }

    return std::make_shared<FloatData>(static_cast<float>(data_(linear_index)));
}
std::vector<size_t> TensorData::shape() const { return shape_; }

size_t TensorData::size() const { return data_.size(); }
std::vector<std::string> TensorData::refs() const {
    std::vector<std::string> result;
    for (const auto &ref : refs_) {
        if (ref) {
            result.push_back(ref->toString());
        }
    }
    return result;
}

data_ptr_t TensorData::clone(bool deep) const {
    auto result = make_shared<TensorData>(type_, shape_);
    result->data_ = data_;

    if (deep) {
        result->refs_.clear();
        for (const auto &ref : refs_) {
            if (ref) {
                result->refs_.push_back(ref->clone(true));
            } else {
                result->refs_.push_back(nullptr);
            }
        }
    } else {
        result->refs_ = refs_;
    }

    return result;
}

const std::string TensorData::toString() const {
    std::stringstream ss;
    ss << "Tensor(shape=" << shape_to_string(shape_) << ", data=[";

    size_t display_count = std::min(size_t(10), static_cast<size_t>(data_.size()));
    for (size_t i = 0; i < display_count; ++i) {
        ss << data_(i);
        if (i < static_cast<size_t>(data_.size()) - 1) {
            ss << ", ";
        }
    }

    if (data_.size() > 10) {
        ss << ", ...";
    }
    ss << "])";

    return ss.str();
}

const std::string TensorData::toFormattedString(size_t /*maxItemsPerDim*/) const {
    std::stringstream ss;

    if (shape_.empty()) {
        ss << "Tensor([])";
        return ss.str();
    }

    // 数值格式化
    auto formatValue = [](double val) -> std::string {
        std::ostringstream oss;
        double absVal = std::fabs(val);
        if (absVal == 0.0) {
            oss << "0.0";
        } else if (absVal >= 1e5 || absVal < 1e-4) {
            oss << std::scientific << std::setprecision(4) << val;
        } else if (std::floor(val) == val) {
            oss << std::fixed << std::setprecision(1) << val;
        } else {
            oss << std::fixed << std::setprecision(6) << val;
        }
        return oss.str();
    };

    // 递归打印 + 缩进
    std::function<void(std::stringstream &, size_t, size_t, size_t)> printDim;
    printDim = [&](std::stringstream &out, size_t dim, size_t offset, size_t indent) {
        size_t len = shape_[dim];
        bool isLastDim = (dim == shape_.size() - 1);

        if (isLastDim) {
            out << "[";
            for (size_t i = 0; i < len; ++i) {
                out << formatValue(data_(offset + i));
                if (i != len - 1)
                    out << ", ";
            }
            out << "]";
        } else {
            out << "[";
            size_t stride = 1;
            for (size_t k = dim + 1; k < shape_.size(); ++k)
                stride *= shape_[k];

            for (size_t i = 0; i < len; ++i) {
                if (i > 0) {
                    out << ",\n" << std::string(indent + 1, ' ');
                }
                printDim(out, dim + 1, offset + i * stride, indent + 1);
            }
            out << "]";
        }
    };

    // 顶层调用，初始缩进量 = "Tensor(" 的长度
    ss << "Tensor(";
    printDim(ss, 0, 0, std::string("Tensor(").size());
    const auto &dtype = tt::as_shared<TensorType>(type_)->dType();
    ss << ", dtype=" << dtype->toString() << ")";
    return ss.str();
}

void TensorData::print(std::ostream &os) const { os << toFormattedString(); }

void TensorData::resolve(const data_vec_t &dataList) {
    if (refs_.empty()) {
        return;
    }

    if (dataList.empty()) {
        throw runtime_error("Cannot resolve tensor references: empty data list provided");
    }

    if (refs_.size() != static_cast<size_t>(data_.size())) {
        throw runtime_error(
            "Reference count mismatch: expected " + std::to_string(data_.size()) +
            " references, got " + std::to_string(refs_.size()));
    }

    for (size_t i = 0; i < refs_.size(); ++i) {
        const auto &refData = refs_[i];

        bool found = false;
        for (const auto &data : dataList) {
            if (!data)
                continue;
            if (refData && refData->toString() == data->toString()) {
                try {
                    auto doubleData = tt::as_shared<DoubleData>(data);
                    if (doubleData) {
                        data_(i) = doubleData->data();
                        found = true;
                        break;
                    }

                    auto int32Data = tt::as_shared<IntData>(data);
                    if (int32Data) {
                        data_(i) = static_cast<double>(int32Data->data());
                        found = true;
                        break;
                    }

                    auto int64Data = tt::as_shared<LongData>(data);
                    if (int64Data) {
                        data_(i) = static_cast<double>(int64Data->data());
                        found = true;
                        break;
                    }

                    auto floatData = tt::as_shared<FloatData>(data);
                    if (floatData) {
                        data_(i) = static_cast<double>(floatData->data());
                        found = true;
                        break;
                    }

                    auto boolData = tt::as_shared<BoolData>(data);
                    if (boolData) {
                        data_(i) = boolData->data() ? 1.0 : 0.0;
                        found = true;
                        break;
                    }

                    auto charData = tt::as_shared<CharData>(data);
                    if (charData) {
                        data_(i) = static_cast<double>(charData->data());
                        found = true;
                        break;
                    }

                    auto stringData = tt::as_shared<StringData>(data);
                    if (stringData) {
                        try {
                            data_(i) = std::stod(stringData->data());
                            found = true;
                            break;
                        } catch (const std::exception &) {
                            continue;
                        }
                    }

                    auto tensorData = tt::as_shared<TensorData>(data);
                    if (tensorData && tensorData->size() == 1) {
                        data_(i) = tensorData->data_(0);
                        found = true;
                        break;
                    }

                    auto vectorData = tt::as_shared<VectorData>(data);
                    if (vectorData && vectorData->size() == 1) {
                        auto element = vectorData->get(0);
                        auto elementDouble = tt::as_shared<DoubleData>(element);
                        if (elementDouble) {
                            data_(i) = elementDouble->data();
                            found = true;
                            break;
                        }
                    }

                    auto listData = tt::as_shared<ListData>(data);
                    if (listData && listData->size() == 1) {
                        auto element = listData->get(0);
                        auto elementDouble = tt::as_shared<DoubleData>(element);
                        if (elementDouble) {
                            data_(i) = elementDouble->data();
                            found = true;
                            break;
                        }
                    }

                } catch (const exception &e) {
                    std::cerr << "Warning: Error resolving reference " << i << ": " << e.what()
                              << std::endl;
                    continue;
                }
            }
        }

        if (!found) {
            throw runtime_error(
                "Cannot resolve reference: " + refData->toString() + " at position " +
                std::to_string(i));
        }
    }

    refs_.clear();

    for (Eigen::Index i = 0; i < data_.size(); ++i) {
        if (std::isnan(data_(i)) || std::isinf(data_(i))) {
            throw runtime_error(
                "Invalid data value at position " + std::to_string(i) +
                " after reference resolution");
        }
    }
}
bool TensorData::equals(const data_ptr_t &other) const { return true; }

data_ptr_t TensorData::convert(type_ptr_t target, bool inplace) {
    if (target == type_ || (type_ && type_->equals(target))) {
        return shared_from_this();
    }

    if (inplace && target->code() == TensorType::typeCode()) {
        type_ = target;
        return shared_from_this();
    }

    switch (target->code()) {
    case TypeCode::Array: {
        auto array_type = tt::as_shared<ArrayType>(target);
        if (array_type) {
            vector<data_ptr_t> elements;
            elements.reserve(static_cast<size_t>(data_.size()));
            for (Eigen::Index i = 0; i < data_.size(); ++i) {
                elements.push_back(make_shared<DoubleData>(data_(i)));
            }
            auto result = make_shared<ArrayData>(array_type, std::move(elements));
            return result;
        }
        break;
    }

    case TypeCode::Double: {
        if (data_.size() == 1) {
            auto result = make_shared<DoubleData>(data_(0));
            return result;
        }
        break;
    }

    default:
        if (target->code() == TensorType::typeCode()) {
            auto tensor_type = tt::as_shared<TensorType>(target);
            if (tensor_type) {
                if (type_ && !type_->equals(tensor_type->dType())) {
                    auto result = make_shared<TensorData>(target, tensor_type->shape());

                    // Eigen VectorXd already contains double values
                    result->data_ = data_;

                    return result;
                }

                if (tensor_type->shape() != shape()) {
                    try {
                        return reshape(tensor_type->shape());
                    } catch (...) {
                        auto result = make_shared<TensorData>(target, tensor_type->shape());
                        Eigen::Index min_size = std::min(
                            static_cast<Eigen::Index>(data_.size()),
                            static_cast<Eigen::Index>(result->data_.size()));
                        if (min_size > 0) {
                            result->data_.head(min_size) = data_.head(min_size);
                        }
                        return result;
                    }
                }

                auto result = make_shared<TensorData>(target, shape());
                result->data_ = data_;
                return result;
            }
        }
    }

    throw DataConvError("Cannot convert TensorData to " + target->toString());
}

data_ptr_t TensorData::reshape(const std::vector<size_t> &shape) const {
    size_t new_size = 1;
    for (size_t dim : shape) {
        new_size *= dim;
    }

    if (static_cast<Eigen::Index>(new_size) != data_.size()) {
        throw invalid_argument("Cannot reshape tensor: total size must remain constant");
    }

    auto result = make_shared<TensorData>(nullptr, shape);
    result->data_ = data_;
    return result;
}

data_ptr_t TensorData::expand_dims(size_t axis) const {
    vector<size_t> new_shape = shape();

    if (axis <= new_shape.size()) {
        new_shape.insert(new_shape.begin() + axis, 1);
    } else {
        throw invalid_argument("Axis out of bounds");
    }

    auto result = make_shared<TensorData>(nullptr, new_shape);

    result->data_ = data_;

    return result;
}

data_ptr_t TensorData::squeeze() const {
    vector<size_t> old_shape = shape();
    vector<size_t> new_shape;

    for (size_t dim : old_shape) {
        if (dim != 1) {
            new_shape.push_back(dim);
        }
    }

    if (new_shape.size() == old_shape.size()) {
        return clone(true);
    }

    if (new_shape.empty()) {
        new_shape.push_back(1);
        new_shape.push_back(1);
    } else if (new_shape.size() == 1) {
        new_shape.push_back(1);
    }

    auto result = make_shared<TensorData>(nullptr, new_shape);
    result->data_ = data_;
    return result;
}

data_ptr_t TensorData::concat(const data_ptr_t &other, size_t axis) const {
    auto other_tensor = tt::as_shared<TensorData>(other);
    if (!other_tensor) {
        throw invalid_argument("Can only concatenate with another TensorData");
    }

    if (axis > 1) {
        throw invalid_argument("Axis out of bounds for 2D tensor");
    }

    vector<size_t> new_shape = shape();

    if (axis == 0) {
        if (shape().size() != 2 || other_tensor->shape().size() != 2) {
            throw invalid_argument("Concatenation currently only supports 2D tensors");
        }

        if (shape()[1] != other_tensor->shape()[1]) {
            throw runtime_error(
                "Tensors must have the same number of columns for axis=0 concatenation");
        }

        new_shape[0] = shape()[0] + other_tensor->shape()[0];
        auto result = make_shared<TensorData>(nullptr, new_shape);
        result->data_.resize(new_shape[0] * new_shape[1]);

        // Copy data from first tensor using Eigen's block operations
        size_t rows1 = shape()[0];
        size_t cols = shape()[1];
        size_t rows2 = other_tensor->shape()[0];

        Eigen::Map<Eigen::MatrixXd> result_map(result->data_.data(), cols, rows1 + rows2);
        Eigen::Map<const Eigen::MatrixXd> this_map(data_.data(), cols, rows1);
        Eigen::Map<const Eigen::MatrixXd> other_map(other_tensor->data_.data(), cols, rows2);

        result_map.block(0, 0, cols, rows1) = this_map;
        result_map.block(0, rows1, cols, rows2) = other_map;

        return result;
    } else {
        // axis == 1
        if (shape().size() != 2 || other_tensor->shape().size() != 2) {
            throw invalid_argument("Concatenation currently only supports 2D tensors");
        }

        if (shape()[0] != other_tensor->shape()[0]) {
            throw invalid_argument(
                "Tensors must have the same number of rows for axis=1 concatenation");
        }

        new_shape[1] = shape()[1] + other_tensor->shape()[1];
        auto result = make_shared<TensorData>(nullptr, new_shape);
        result->data_.resize(new_shape[0] * new_shape[1]);

        // Copy data from first tensor and second tensor using Eigen operations
        size_t rows = shape()[0];
        size_t cols1 = shape()[1];
        size_t cols2 = other_tensor->shape()[1];

        Eigen::Map<Eigen::MatrixXd> result_map(result->data_.data(), cols1 + cols2, rows);
        Eigen::Map<const Eigen::MatrixXd> this_map(data_.data(), cols1, rows);
        Eigen::Map<const Eigen::MatrixXd> other_map(other_tensor->data_.data(), cols2, rows);

        result_map.block(0, 0, cols1, rows) = this_map;
        result_map.block(cols1, 0, cols2, rows) = other_map;

        return result;
    }
}

data_ptr_t TensorData::stack(const data_ptr_t &other, size_t axis) const {
    auto other_tensor = tt::as_shared<TensorData>(other);
    if (!other_tensor) {
        throw invalid_argument("Can only stack with another TensorData");
    }

    if (axis > 2) {
        throw invalid_argument("Axis out of bounds for 2D tensor stacking");
    }

    if (shape() != other_tensor->shape()) {
        throw invalid_argument("Tensors must have the same shape for stacking");
    }

    if (shape().size() != 2) {
        throw invalid_argument("Stacking currently only supports 2D tensors");
    }

    if (axis == 0) {
        vector<size_t> new_shape = {2 * shape()[0], shape()[1]};
        auto result = make_shared<TensorData>(nullptr, new_shape);
        result->data_.resize(new_shape[0] * new_shape[1]);

        size_t rows1 = shape()[0];
        size_t cols = shape()[1];
        size_t rows2 = other_tensor->shape()[0];

        Eigen::Map<Eigen::MatrixXd> result_map(result->data_.data(), cols, rows1 + rows2);
        Eigen::Map<const Eigen::MatrixXd> this_map(data_.data(), cols, rows1);
        Eigen::Map<const Eigen::MatrixXd> other_map(other_tensor->data_.data(), cols, rows2);

        result_map.block(0, 0, cols, rows1) = this_map;
        result_map.block(0, rows1, cols, rows2) = other_map;

        return result;
    } else if (axis == 1) {
        vector<size_t> new_shape = {shape()[0], 2 * shape()[1]};
        auto result = make_shared<TensorData>(nullptr, new_shape);
        result->data_.resize(new_shape[0] * new_shape[1]);

        size_t rows = shape()[0];
        size_t cols1 = shape()[1];
        size_t cols2 = other_tensor->shape()[1];

        Eigen::Map<Eigen::MatrixXd> result_map(result->data_.data(), cols1 + cols2, rows);
        Eigen::Map<const Eigen::MatrixXd> this_map(data_.data(), cols1, rows);
        Eigen::Map<const Eigen::MatrixXd> other_map(other_tensor->data_.data(), cols2, rows);

        result_map.block(0, 0, cols1, rows) = this_map;
        result_map.block(cols1, 0, cols2, rows) = other_map;

        return result;
    } else {
        // axis == 2 (stack along depth)
        // For 2D tensors, this creates a 3D tensor
        vector<size_t> new_shape = {shape()[0], shape()[1], 2};
        auto result = make_shared<TensorData>(nullptr, new_shape);
        result->data_.resize(shape()[0] * shape()[1] * 2);

        size_t rows = shape()[0];
        size_t cols = shape()[1];
        size_t total_elements = rows * cols;

        result->data_.head(total_elements) = data_;

        result->data_.segment(total_elements, total_elements) = other_tensor->data_;

        return result;
    }
}

data_ptr_t TensorData::argmin() const {
    if (data_.size() == 0) {
        throw invalid_argument("Cannot find argmin of empty tensor");
    }

    Eigen::Index min_index;
    data_.minCoeff(&min_index);

    size_t cols = shape().size() > 1 ? shape()[1] : data_.size();
    size_t min_row = min_index / cols;
    size_t min_col = min_index % cols;

    auto result = make_shared<TensorData>(nullptr, vector<size_t>{1, 2});
    result->data_(0) = static_cast<double>(min_row);
    result->data_(1) = static_cast<double>(min_col);
    return result;
}

data_ptr_t TensorData::argmax() const {
    if (data_.size() == 0) {
        throw invalid_argument("Cannot find argmax of empty tensor");
    }

    Eigen::Index max_index;
    data_.maxCoeff(&max_index);

    size_t cols = shape().size() > 1 ? shape()[1] : data_.size();
    size_t max_row = max_index / cols;
    size_t max_col = max_index % cols;

    auto result = make_shared<TensorData>(nullptr, vector<size_t>{1, 2});
    result->data_(0) = static_cast<double>(max_row);
    result->data_(1) = static_cast<double>(max_col);
    return result;
}

data_ptr_t TensorData::var() const {
    if (data_.size() == 0) {
        throw invalid_argument("Cannot calculate variance of empty tensor");
    }

    double mean_val = data_.mean();
    double variance = (data_.array() - mean_val).square().mean();

    auto result = make_shared<FloatData>(static_cast<float>(variance));
    return result;
}

data_ptr_t TensorData::std() const {
    if (data_.size() == 0) {
        throw invalid_argument("Cannot calculate standard deviation of empty tensor");
    }

    double mean_val = data_.mean();
    double variance = (data_.array() - mean_val).square().mean();
    double std_dev = std::sqrt(variance);

    auto result = make_shared<FloatData>(static_cast<float>(std_dev));
    return result;
}

data_ptr_t TensorData::sinh() const {
    auto result = make_shared<TensorData>(nullptr, shape_);
    result->data_ = data_.array().sinh();
    return result;
}

data_ptr_t TensorData::cosh() const {
    auto result = make_shared<TensorData>(nullptr, shape_);
    result->data_ = data_.array().cosh();
    return result;
}

data_ptr_t TensorData::tanh() const {
    auto result = make_shared<TensorData>(nullptr, shape_);
    result->data_ = data_.array().tanh();
    return result;
}

data_ptr_t TensorData::pow(double exponent) const {
    auto result = make_shared<TensorData>(nullptr, shape_);
    result->data_ = data_.array().pow(exponent);
    return result;
}

data_ptr_t TensorData::pow(const data_ptr_t &exponent) const {
    if (auto tensor = tt::to_shared<TensorData>(exponent)) {
        if (!is_broadcastable(shape_, tensor->shape_)) {
            throw std::invalid_argument(
                "Tensor shapes are not broadcastable for power operation: " +
                shape_to_string(shape_) + " and " + shape_to_string(tensor->shape_));
        }

        std::vector<size_t> result_shape = broadcast_shape(shape_, tensor->shape_);

        auto result = make_shared<TensorData>(nullptr, result_shape);
        result->data_.resize(result->data_.size());

        std::vector<size_t> index_result(result_shape.size(), 0);
        calculate_broadcast_power(*this, *tensor, *result, index_result, 0);

        return result;
    }

    if (auto intData = tt::to_shared<IntData>(exponent)) {
        auto result = make_shared<TensorData>(nullptr, shape_);
        result->data_ = data_.array().pow(static_cast<double>(intData->data()));
        return result;
    }
    if (auto longData = tt::to_shared<LongData>(exponent)) {
        auto result = make_shared<TensorData>(nullptr, shape_);
        result->data_ = data_.array().pow(static_cast<double>(longData->data()));
        return result;
    }
    if (auto floatData = tt::to_shared<FloatData>(exponent)) {
        auto result = make_shared<TensorData>(nullptr, shape_);
        result->data_ = data_.array().pow(static_cast<double>(floatData->data()));
        return result;
    }
    if (auto doubleData = tt::to_shared<DoubleData>(exponent)) {
        auto result = make_shared<TensorData>(nullptr, shape_);
        result->data_ = data_.array().pow(doubleData->data());
        return result;
    }

    throw runtime_error("Tensor power operation expects either Tensor or scalar operand");
}

void TensorData::calculate_broadcast_power(
    const TensorData &tensor1, const TensorData &tensor2, TensorData &result,
    std::vector<size_t> &index_result, size_t current_dim) {
    size_t dim_result = result.shape_.size();

    if (current_dim == dim_result) {
        std::vector<size_t> actual_index1(tensor1.shape_.size(), 0);
        std::vector<size_t> actual_index2(tensor2.shape_.size(), 0);

        size_t offset1 = dim_result - tensor1.shape_.size();
        for (size_t i = 0; i < tensor1.shape_.size(); ++i) {
            size_t result_dim = offset1 + i;
            if (tensor1.shape_[i] == 1) {
                actual_index1[i] = 0; // Broadcast dimension
            } else {
                actual_index1[i] = index_result[result_dim];
            }
        }

        size_t offset2 = dim_result - tensor2.shape_.size();
        for (size_t i = 0; i < tensor2.shape_.size(); ++i) {
            size_t result_dim = offset2 + i;
            if (tensor2.shape_[i] == 1) {
                actual_index2[i] = 0; // Broadcast dimension
            } else {
                actual_index2[i] = index_result[result_dim];
            }
        }

        size_t linear_index1 = calculate_linear_index(actual_index1, tensor1.shape_);
        size_t linear_index2 = calculate_linear_index(actual_index2, tensor2.shape_);
        size_t linear_index_result = calculate_linear_index(index_result, result.shape_);

        result.data_(linear_index_result) =
            std::pow(tensor1.data_(linear_index1), tensor2.data_(linear_index2));
        return;
    }

    size_t result_dim_size = result.shape_[current_dim];
    for (size_t i = 0; i < result_dim_size; ++i) {
        index_result[current_dim] = i;
        calculate_broadcast_power(tensor1, tensor2, result, index_result, current_dim + 1);
    }
}

data_ptr_t TensorData::matpow(size_t exponent) const {
    if (shape_.size() != 2) {
        throw std::invalid_argument("Matrix power only supports 2D tensors");
    }

    if (shape_[0] != shape_[1]) {
        throw std::invalid_argument("Matrix power only supports square matrices");
    }

    if (exponent == 0) {
        return eye(shape_[0]);
    }

    if (exponent == 1) {
        return clone(true);
    }

    auto result = tt::as_shared<TensorData>(clone(true));

    auto base = tt::as_shared<TensorData>(clone(true));

    while (exponent > 1) {
        if (exponent % 2 == 1) {
            result = tt::as_shared<TensorData>(result->matmul(base));
        }
        if (exponent > 1) {
            base = tt::as_shared<TensorData>(base->matmul(base));
        }
        exponent /= 2;
    }

    return result;
}

data_ptr_t TensorData::cube() const {
    auto result = make_shared<TensorData>(nullptr, shape_);
    result->data_ = data_.array().cube();
    return result;
}

data_ptr_t TensorData::ceil() const {
    auto result = make_shared<TensorData>(nullptr, shape_);
    result->data_ = data_.array().ceil();
    return result;
}

data_ptr_t TensorData::floor() const {
    auto result = make_shared<TensorData>(nullptr, shape_);
    result->data_ = data_.array().floor();
    return result;
}

data_ptr_t TensorData::round() const {
    auto result = make_shared<TensorData>(nullptr, shape_);
    result->data_ = data_.array().round();
    return result;
}

data_ptr_t TensorData::take(const std::vector<size_t> &indices, size_t axis) const {
    if (axis > 1) {
        throw invalid_argument("Axis out of bounds for 2D tensor");
    }

    if (indices.empty()) {
        throw invalid_argument("Indices cannot be empty");
    }

    const auto &shape_vec = shape();
    if (shape_vec.size() != 2) {
        throw invalid_argument("Take operation currently only supports 2D tensors");
    }

    size_t rows = shape_vec[0];
    size_t cols = shape_vec[1];

    if (axis == 0) {
        auto result = make_shared<TensorData>(nullptr, vector<size_t>{indices.size(), cols});
        result->data_.resize(indices.size() * cols);

        for (size_t i = 0; i < indices.size(); ++i) {
            if (indices[i] >= rows) {
                throw out_of_range("Index out of bounds");
            }

            size_t src_start = indices[i] * cols;
            size_t dest_start = i * cols;
            result->data_.segment(dest_start, cols) = data_.segment(src_start, cols);
        }

        return result;
    } else {
        auto result = make_shared<TensorData>(nullptr, vector<size_t>{rows, indices.size()});
        result->data_.resize(rows * indices.size());

        for (size_t i = 0; i < indices.size(); ++i) {
            if (indices[i] >= cols) {
                throw out_of_range("Index out of bounds");
            }

            for (size_t j = 0; j < rows; ++j) {
                size_t src_index = j * cols + indices[i];
                size_t dest_index = j * indices.size() + i;
                result->data_(dest_index) = data_(src_index);
            }
        }

        return result;
    }
}

data_ptr_t TensorData::put(const std::vector<size_t> &indices, const data_ptr_t &values) const {
    auto result = make_shared<TensorData>(nullptr, shape_);
    result->data_ = data_;

    auto values_tensor = tt::as_shared<TensorData>(values);
    if (!values_tensor) {
        throw invalid_argument("Values must be a TensorData");
    }

    if (indices.size() != static_cast<size_t>(values_tensor->data_.size())) {
        throw invalid_argument("Number of indices must match number of values");
    }

    const auto &shape_vec = shape();
    if (shape_vec.size() != 2) {
        throw invalid_argument("Put operation currently only supports 2D tensors");
    }

    for (Eigen::Index i = 0; i < static_cast<Eigen::Index>(indices.size()); ++i) {
        if (indices[i] >= static_cast<size_t>(data_.size())) {
            throw out_of_range("Index out of bounds");
        }

        if (i < values_tensor->data_.size()) {
            result->data_(indices[i]) = values_tensor->data_(i);
        }
    }

    return result;
}

data_ptr_t TensorData::slice(size_t start, size_t end, size_t row) const {
    // For 2D tensor, extract a slice from a specific row
    if (shape_.size() != 2) {
        throw std::runtime_error("Slice operation only supported for 2D tensors");
    }

    if (row >= shape_[0]) {
        throw std::runtime_error("Row index out of bounds");
    }

    if (start >= shape_[1] || end > shape_[1] || start >= end) {
        throw std::runtime_error("Invalid slice range");
    }

    // Create a new tensor for the slice
    std::vector<size_t> new_shape = {1, end - start};
    auto result_tensor = std::make_shared<TensorData>(type_, new_shape);

    // Copy the data
    for (size_t col = start; col < end; ++col) {
        size_t src_index = row * shape_[1] + col;
        size_t dst_index = col - start;
        result_tensor->data_(dst_index) = data_(src_index);
    }

    return result_tensor;
}

data_ptr_t TensorData::add(const data_ptr_t &other) const {
    // Element-wise addition with broadcasting
    if (auto tensor = tt::as_shared<TensorData>(other)) {
        if (!is_broadcastable(shape_, tensor->shape_)) {
            throw std::invalid_argument(
                "Tensor shapes are not broadcastable for addition: " + shape_to_string(shape_) +
                " and " + shape_to_string(tensor->shape_));
        }

        std::vector<size_t> result_shape = broadcast_shape(shape_, tensor->shape_);

        auto result = make_shared<TensorData>(nullptr, result_shape);
        result->data_.resize(result->data_.size());

        std::vector<size_t> index_result(result_shape.size(), 0);
        calculate_broadcast_addition(*this, *tensor, *result, index_result, 0);

        return result;
    }

    if (auto intData = tt::as_shared<IntData>(other)) {
        auto result = make_shared<TensorData>(nullptr, shape_);
        result->data_ = data_.array() + static_cast<double>(intData->data());
        return result;
    }
    if (auto longData = tt::as_shared<LongData>(other)) {
        auto result = make_shared<TensorData>(nullptr, shape_);
        result->data_ = data_.array() + static_cast<double>(longData->data());
        return result;
    }
    if (auto floatData = tt::as_shared<FloatData>(other)) {
        auto result = make_shared<TensorData>(nullptr, shape_);
        result->data_ = data_.array() + static_cast<double>(floatData->data());
        return result;
    }
    if (auto doubleData = tt::as_shared<DoubleData>(other)) {
        auto result = make_shared<TensorData>(nullptr, shape_);
        result->data_ = data_.array() + doubleData->data();
        return result;
    }

    throw runtime_error("Tensor addition expects either Tensor or scalar operand");
}

void TensorData::calculate_broadcast_addition(
    const TensorData &tensor1, const TensorData &tensor2, TensorData &result,
    std::vector<size_t> &index_result, size_t current_dim) {
    size_t dim_result = result.shape_.size();

    if (current_dim == dim_result) {
        std::vector<size_t> actual_index1(tensor1.shape_.size(), 0);
        std::vector<size_t> actual_index2(tensor2.shape_.size(), 0);

        for (size_t i = 0; i < tensor1.shape_.size(); ++i) {
            size_t result_dim = dim_result - tensor1.shape_.size() + i;
            if (tensor1.shape_[i] == 1) {
                actual_index1[i] = 0; // Broadcast dimension
            } else {
                actual_index1[i] = index_result[result_dim];
            }
        }

        for (size_t i = 0; i < tensor2.shape_.size(); ++i) {
            size_t result_dim = dim_result - tensor2.shape_.size() + i;
            if (tensor2.shape_[i] == 1) {
                actual_index2[i] = 0; // Broadcast dimension
            } else {
                actual_index2[i] = index_result[result_dim];
            }
        }

        size_t linear_index1 = calculate_linear_index(actual_index1, tensor1.shape_);
        size_t linear_index2 = calculate_linear_index(actual_index2, tensor2.shape_);
        size_t linear_index_result = calculate_linear_index(index_result, result.shape_);

        result.data_(linear_index_result) =
            tensor1.data_(linear_index1) + tensor2.data_(linear_index2);
        return;
    }

    size_t result_dim_size = result.shape_[current_dim];
    for (size_t i = 0; i < result_dim_size; ++i) {
        index_result[current_dim] = i;
        calculate_broadcast_addition(tensor1, tensor2, result, index_result, current_dim + 1);
    }
}

data_ptr_t TensorData::subtract(const data_ptr_t &other) const {
    if (auto tensor = tt::as_shared<TensorData>(other)) {
        if (!is_broadcastable(shape_, tensor->shape_)) {
            throw std::invalid_argument(
                "Tensor shapes are not broadcastable for subtraction: " + shape_to_string(shape_) +
                " and " + shape_to_string(tensor->shape_));
        }

        std::vector<size_t> result_shape = broadcast_shape(shape_, tensor->shape_);

        auto result = make_shared<TensorData>(nullptr, result_shape);
        result->data_.resize(result->data_.size());

        std::vector<size_t> index_result(result_shape.size(), 0);
        calculate_broadcast_subtraction(*this, *tensor, *result, index_result, 0);

        return result;
    }

    if (auto intData = tt::as_shared<IntData>(other)) {
        auto result = make_shared<TensorData>(nullptr, shape_);
        result->data_ = data_.array() - static_cast<double>(intData->data());
        return result;
    }
    if (auto longData = tt::as_shared<LongData>(other)) {
        auto result = make_shared<TensorData>(nullptr, shape_);
        result->data_ = data_.array() - static_cast<double>(longData->data());
        return result;
    }
    if (auto floatData = tt::as_shared<FloatData>(other)) {
        auto result = make_shared<TensorData>(nullptr, shape_);
        result->data_ = data_.array() - static_cast<double>(floatData->data());
        return result;
    }
    if (auto doubleData = tt::as_shared<DoubleData>(other)) {
        auto result = make_shared<TensorData>(nullptr, shape_);
        result->data_ = data_.array() - doubleData->data();
        return result;
    }

    throw runtime_error("Tensor subtraction expects either Tensor or scalar operand");
}

void TensorData::calculate_broadcast_subtraction(
    const TensorData &tensor1, const TensorData &tensor2, TensorData &result,
    std::vector<size_t> &index_result, size_t current_dim) {
    size_t dim_result = result.shape_.size();

    if (current_dim == dim_result) {
        std::vector<size_t> actual_index1(tensor1.shape_.size(), 0);
        std::vector<size_t> actual_index2(tensor2.shape_.size(), 0);

        for (size_t i = 0; i < tensor1.shape_.size(); ++i) {
            size_t result_dim = dim_result - tensor1.shape_.size() + i;
            if (tensor1.shape_[i] == 1) {
                actual_index1[i] = 0; // Broadcast dimension
            } else {
                actual_index1[i] = index_result[result_dim];
            }
        }

        for (size_t i = 0; i < tensor2.shape_.size(); ++i) {
            size_t result_dim = dim_result - tensor2.shape_.size() + i;
            if (tensor2.shape_[i] == 1) {
                actual_index2[i] = 0; // Broadcast dimension
            } else {
                actual_index2[i] = index_result[result_dim];
            }
        }

        size_t linear_index1 = calculate_linear_index(actual_index1, tensor1.shape_);
        size_t linear_index2 = calculate_linear_index(actual_index2, tensor2.shape_);
        size_t linear_index_result = calculate_linear_index(index_result, result.shape_);

        result.data_(linear_index_result) =
            tensor1.data_(linear_index1) - tensor2.data_(linear_index2);
        return;
    }

    size_t result_dim_size = result.shape_[current_dim];
    for (size_t i = 0; i < result_dim_size; ++i) {
        index_result[current_dim] = i;
        calculate_broadcast_subtraction(tensor1, tensor2, result, index_result, current_dim + 1);
    }
}

data_ptr_t TensorData::multiply(const data_ptr_t &other) const {
    // Element-wise multiplication with broadcasting
    if (this == other.get()) {
        auto result = make_shared<TensorData>(nullptr, shape_);
        result->data_ = data_.array().square();
        return result;
    }
    if (auto tensor = tt::to_shared<TensorData>(other)) {
        if (!is_broadcastable(shape_, tensor->shape_)) {
            throw std::invalid_argument(
                "Tensor shapes are not broadcastable for multiplication: " +
                shape_to_string(shape_) + " and " + shape_to_string(tensor->shape_));
        }

        std::vector<size_t> result_shape = broadcast_shape(shape_, tensor->shape_);

        auto result = make_shared<TensorData>(nullptr, result_shape);
        result->data_.resize(result->data_.size());

        std::vector<size_t> index_result(result_shape.size(), 0);
        std::vector<size_t> index1(shape_.size(), 0);
        std::vector<size_t> index2(tensor->shape_.size(), 0);
        calculate_broadcast_multiplication(
            *this,
            *tensor,
            *result,
            index1,
            index2,
            index_result,
            0);

        return result;
    }

    if (auto intData = tt::to_shared<IntData>(other)) {
        auto result = make_shared<TensorData>(nullptr, shape_);
        result->data_ = data_.array() * static_cast<double>(intData->data());
        return result;
    }
    if (auto longData = tt::to_shared<LongData>(other)) {
        auto result = make_shared<TensorData>(nullptr, shape_);
        result->data_ = data_.array() * static_cast<double>(longData->data());
        return result;
    }
    if (auto floatData = tt::to_shared<FloatData>(other)) {
        auto result = make_shared<TensorData>(nullptr, shape_);
        result->data_ = data_.array() * static_cast<double>(floatData->data());
        return result;
    }
    if (auto doubleData = tt::to_shared<DoubleData>(other)) {
        auto result = make_shared<TensorData>(nullptr, shape_);
        result->data_ = data_.array() * doubleData->data();
        return result;
    }

    throw runtime_error("Tensor multiplication expects either Tensor or scalar operand");
}

data_ptr_t TensorData::matmul(const data_ptr_t &other) const {
    auto other_tensor = tt::as_shared<TensorData>(other);

    size_t dim_this = shape_.size();
    size_t dim_other = other_tensor->shape_.size();

    if ((dim_this != 1 && dim_this != 2) || (dim_other != 1 && dim_other != 2)) {
        throw std::invalid_argument("Matrix multiplication supports 1D and 2D tensors only");
    }

    std::vector<size_t> shape1 = shape_;
    std::vector<size_t> shape2 = other_tensor->shape_;

    if (dim_this == 1) {
        shape1.insert(shape1.begin(), 1);
    }
    if (dim_other == 1) {
        shape2.push_back(1);
    }

    if (shape1[shape1.size() - 1] != shape2[shape2.size() - 2]) {
        throw DiagnosticBuilder::of(RuntimeDiag::TensorDimensionMismatch)
            .commit(shape_to_string(shape1), shape_to_string(shape2));
    }

    std::vector<size_t> result_shape;
    if (dim_this == 1 && dim_other == 1) {
        auto result = std::make_shared<DoubleData>(0.0);

        double dot_product = 0.0;
        for (Eigen::Index i = 0; i < data_.size(); ++i) {
            dot_product += data_(i) * other_tensor->data_(i);
        }
        result->data() = dot_product;
        return result;
    } else if (dim_this == 1) {
        // 1D * 2D -> 1D
        result_shape = {shape2[1]};
    } else if (dim_other == 1) {
        // 2D * 1D -> 1D
        result_shape = {shape1[0]};
    } else {
        // 2D * 2D -> 2D
        result_shape = {shape1[0], shape2[1]};
    }

    auto result = std::make_shared<TensorData>(nullptr, result_shape);
    result->data_.resize(result_shape[0] * result_shape[1]);

    if (dim_this == 1 && dim_other == 1) {
        Eigen::Map<const Eigen::VectorXd> this_vector(data_.data(), shape1[0]);
        Eigen::Map<const Eigen::VectorXd> other_vector(other_tensor->data_.data(), shape2[0]);

        double dot_product = this_vector.dot(other_vector);
        auto result_scalar = std::make_shared<DoubleData>(dot_product);
        return result_scalar;
    } else if (dim_this == 1) {
        // 1D * 2D
        Eigen::Map<const Eigen::Matrix<double, 1, Eigen::Dynamic, Eigen::RowMajor>> this_matrix(
            data_.data(),
            1,
            shape1[0]);
        Eigen::Map<const Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>
            other_matrix(other_tensor->data_.data(), shape2[0], shape2[1]);
        Eigen::Map<Eigen::Matrix<double, 1, Eigen::Dynamic, Eigen::RowMajor>> result_matrix(
            result->data_.data(),
            1,
            result_shape[0]);

        result_matrix = this_matrix * other_matrix;
    } else if (dim_other == 1) {
        // 2D * 1D
        Eigen::Map<const Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>
            this_matrix(data_.data(), shape1[0], shape1[1]);
        Eigen::Map<const Eigen::Matrix<double, Eigen::Dynamic, 1, Eigen::ColMajor>> other_matrix(
            other_tensor->data_.data(),
            shape2[0],
            1);
        Eigen::Map<Eigen::Matrix<double, Eigen::Dynamic, 1, Eigen::ColMajor>> result_matrix(
            result->data_.data(),
            result_shape[0],
            1);

        result_matrix = this_matrix * other_matrix;
    } else {
        // 2D * 2D
        Eigen::Map<const Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>
            this_matrix(data_.data(), shape1[0], shape1[1]);
        Eigen::Map<const Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>
            other_matrix(other_tensor->data_.data(), shape2[0], shape2[1]);
        Eigen::Map<Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>
            result_matrix(result->data_.data(), result_shape[0], result_shape[1]);

        result_matrix = this_matrix * other_matrix;
    }

    return result;
}
data_ptr_t TensorData::divide(const data_ptr_t &other) const {
    if (auto tensor = tt::as_shared<TensorData>(other)) {
        if (!is_broadcastable(shape_, tensor->shape_)) {
            throw std::invalid_argument(
                "Tensor shapes are not broadcastable for division: " + shape_to_string(shape_) +
                " and " + shape_to_string(tensor->shape_));
        }

        std::vector<size_t> result_shape = broadcast_shape(shape_, tensor->shape_);

        auto result = make_shared<TensorData>(nullptr, result_shape);
        result->data_.resize(result->data_.size());

        std::vector<size_t> index_result(result_shape.size(), 0);
        calculate_broadcast_division(*this, *tensor, *result, index_result, 0);

        return result;
    }

    if (auto intData = tt::as_shared<IntData>(other)) {
        if (intData->data() == 0) {
            throw std::invalid_argument("Division by zero");
        }
        auto result = make_shared<TensorData>(nullptr, shape_);
        result->data_ = data_.array() / static_cast<double>(intData->data());
        return result;
    }
    if (auto longData = tt::as_shared<LongData>(other)) {
        if (longData->data() == 0) {
            throw std::invalid_argument("Division by zero");
        }
        auto result = make_shared<TensorData>(nullptr, shape_);
        result->data_ = data_.array() / static_cast<double>(longData->data());
        return result;
    }
    if (auto floatData = tt::as_shared<FloatData>(other)) {
        if (floatData->data() == 0.0f) {
            throw std::invalid_argument("Division by zero");
        }
        auto result = make_shared<TensorData>(nullptr, shape_);
        result->data_ = data_.array() / static_cast<double>(floatData->data());
        return result;
    }
    if (auto doubleData = tt::as_shared<DoubleData>(other)) {
        if (doubleData->data() == 0.0) {
            throw std::invalid_argument("Division by zero");
        }
        auto result = make_shared<TensorData>(nullptr, shape_);
        result->data_ = data_.array() / doubleData->data();
        return result;
    }

    throw runtime_error("Tensor division expects either Tensor or scalar operand");
}

void TensorData::calculate_broadcast_division(
    const TensorData &tensor1, const TensorData &tensor2, TensorData &result,
    std::vector<size_t> &index_result, size_t current_dim) {
    size_t dim_result = result.shape_.size();

    if (current_dim == dim_result) {
        std::vector<size_t> actual_index1(tensor1.shape_.size(), 0);
        std::vector<size_t> actual_index2(tensor2.shape_.size(), 0);

        for (size_t i = 0; i < tensor1.shape_.size(); ++i) {
            size_t result_dim = dim_result - tensor1.shape_.size() + i;
            if (tensor1.shape_[i] == 1) {
                actual_index1[i] = 0; // Broadcast dimension
            } else {
                actual_index1[i] = index_result[result_dim];
            }
        }

        for (size_t i = 0; i < tensor2.shape_.size(); ++i) {
            size_t result_dim = dim_result - tensor2.shape_.size() + i;
            if (tensor2.shape_[i] == 1) {
                actual_index2[i] = 0; // Broadcast dimension
            } else {
                actual_index2[i] = index_result[result_dim];
            }
        }

        size_t linear_index1 = calculate_linear_index(actual_index1, tensor1.shape_);
        size_t linear_index2 = calculate_linear_index(actual_index2, tensor2.shape_);
        size_t linear_index_result = calculate_linear_index(index_result, result.shape_);

        result.data_(linear_index_result) =
            tensor1.data_(linear_index1) / tensor2.data_(linear_index2);
        return;
    }

    size_t result_dim_size = result.shape_[current_dim];
    for (size_t i = 0; i < result_dim_size; ++i) {
        index_result[current_dim] = i;
        calculate_broadcast_division(tensor1, tensor2, result, index_result, current_dim + 1);
    }
}
data_ptr_t TensorData::equal(const data_ptr_t &other) const {
    auto other_tensor = tt::as_shared<TensorData>(other);
    if (!other_tensor) {
        throw invalid_argument("Can only compare with another TensorData");
    }

    if (data_.size() != other_tensor->data_.size()) {
        throw invalid_argument("Tensor dimensions must match for comparison");
    }

    auto result = make_shared<TensorData>(nullptr, shape());
    result->data_ = (data_.array() == other_tensor->data_.array()).cast<double>();
    return result;
}

data_ptr_t TensorData::not_equal(const data_ptr_t &other) const {
    auto other_tensor = tt::as_shared<TensorData>(other);
    if (!other_tensor) {
        throw invalid_argument("Can only compare with another TensorData");
    }

    if (data_.size() != other_tensor->data_.size()) {
        throw invalid_argument("Tensor dimensions must match for comparison");
    }

    auto result = make_shared<TensorData>(nullptr, shape());
    result->data_ = (data_.array() != other_tensor->data_.array()).cast<double>();
    return result;
}

data_ptr_t TensorData::greater(const data_ptr_t &other) const {
    auto other_tensor = tt::as_shared<TensorData>(other);
    if (!other_tensor) {
        throw invalid_argument("Can only compare with another TensorData");
    }

    if (data_.size() != other_tensor->data_.size()) {
        throw invalid_argument("Tensor dimensions must match for comparison");
    }

    auto result = make_shared<TensorData>(nullptr, shape());
    result->data_ = (data_.array() > other_tensor->data_.array()).cast<double>();
    return result;
}

data_ptr_t TensorData::less(const data_ptr_t &other) const {
    auto other_tensor = tt::as_shared<TensorData>(other);
    if (!other_tensor) {
        throw invalid_argument("Can only compare with another TensorData");
    }

    if (data_.size() != other_tensor->data_.size()) {
        throw invalid_argument("Tensor dimensions must match for comparison");
    }

    auto result = make_shared<TensorData>(nullptr, shape());
    result->data_ = (data_.array() < other_tensor->data_.array()).cast<double>();
    return result;
}

data_ptr_t TensorData::greater_equal(const data_ptr_t &other) const {
    auto other_tensor = tt::as_shared<TensorData>(other);
    if (!other_tensor) {
        throw invalid_argument("Can only compare with another TensorData");
    }

    if (data_.size() != other_tensor->data_.size()) {
        throw invalid_argument("Tensor dimensions must match for comparison");
    }

    auto result = make_shared<TensorData>(nullptr, shape());
    result->data_ = (data_.array() >= other_tensor->data_.array()).cast<double>();
    return result;
}

data_ptr_t TensorData::less_equal(const data_ptr_t &other) const {
    auto other_tensor = tt::as_shared<TensorData>(other);
    if (!other_tensor) {
        throw invalid_argument("Can only compare with another TensorData");
    }

    if (data_.size() != other_tensor->data_.size()) {
        throw invalid_argument("Tensor dimensions must match for comparison");
    }

    auto result = make_shared<TensorData>(nullptr, shape());
    result->data_ = (data_.array() <= other_tensor->data_.array()).cast<double>();
    return result;
}

data_ptr_t TensorData::logical_and(const data_ptr_t &other) const {
    auto other_tensor = tt::as_shared<TensorData>(other);
    if (!other_tensor) {
        throw invalid_argument("Can only perform logical operation with another TensorData");
    }

    if (data_.size() != other_tensor->data_.size()) {
        throw invalid_argument("Tensor dimensions must match for logical operation");
    }

    auto result = make_shared<TensorData>(nullptr, shape());
    result->data_ = (data_.array() != 0.0 && other_tensor->data_.array() != 0.0).cast<double>();
    return result;
}

data_ptr_t TensorData::logical_or(const data_ptr_t &other) const {
    auto other_tensor = tt::as_shared<TensorData>(other);
    if (!other_tensor) {
        throw invalid_argument("Can only perform logical operation with another TensorData");
    }

    if (data_.size() != other_tensor->data_.size()) {
        throw invalid_argument("Tensor dimensions must match for logical operation");
    }

    auto result = make_shared<TensorData>(nullptr, shape());
    result->data_ = (data_.array() != 0.0 || other_tensor->data_.array() != 0.0).cast<double>();
    return result;
}

data_ptr_t TensorData::logical_not() const {
    auto result = make_shared<TensorData>(nullptr, shape());
    result->data_ = (data_.array() == 0.0).cast<double>();
    return result;
}

data_ptr_t TensorData::where(const data_ptr_t &condition, const data_ptr_t &other) const {
    auto condition_tensor = tt::as_shared<TensorData>(condition);
    auto other_tensor = tt::as_shared<TensorData>(other);

    if (!condition_tensor || !other_tensor) {
        throw invalid_argument("Condition and other must be TensorData");
    }

    if (data_.size() != condition_tensor->data_.size() ||
        data_.size() != other_tensor->data_.size()) {
        throw invalid_argument("All tensors must have the same dimensions");
    }

    auto result = make_shared<TensorData>(nullptr, shape());

    result->data_ = (condition_tensor->data_.array() != 0.0)
                        .select(data_.array(), other_tensor->data_.array())
                        .matrix();

    return result;
}

data_ptr_t TensorData::flip(size_t axis) const {
    if (axis > 1) {
        throw invalid_argument("Axis out of bounds for 2D tensor");
    }

    const auto &shape_vec = shape();
    if (shape_vec.size() != 2) {
        throw invalid_argument("Flip operation currently only supports 2D tensors");
    }

    size_t rows = shape_vec[0];
    size_t cols = shape_vec[1];

    auto result = make_shared<TensorData>(nullptr, shape());

    if (axis == 0) {
        Eigen::Map<const Eigen::MatrixXd> src_map(data_.data(), cols, rows);
        Eigen::Map<Eigen::MatrixXd> dest_map(result->data_.data(), cols, rows);
        dest_map = src_map.colwise().reverse();
    } else {
        Eigen::Map<const Eigen::MatrixXd> src_map(data_.data(), cols, rows);
        Eigen::Map<Eigen::MatrixXd> dest_map(result->data_.data(), cols, rows);
        dest_map = src_map.rowwise().reverse();
    }

    return result;
}

data_ptr_t TensorData::flipud() const { return flip(0); }

data_ptr_t TensorData::fliplr() const { return flip(1); }

data_ptr_t TensorData::repeat(size_t repeats, size_t axis) const {
    if (repeats == 0) {
        throw invalid_argument("Repeats must be positive");
    }

    if (axis > 1) {
        throw invalid_argument("Axis out of bounds for 2D tensor");
    }

    const auto &shape_vec = shape();
    if (shape_vec.size() != 2) {
        throw invalid_argument("Repeat operation currently only supports 2D tensors");
    }

    size_t rows = shape_vec[0];
    size_t cols = shape_vec[1];

    vector<size_t> new_shape = shape();

    if (axis == 0) {
        new_shape[0] *= repeats;
        auto result = make_shared<TensorData>(nullptr, new_shape);
        result->data_.resize(new_shape[0] * new_shape[1]);

        Eigen::Map<const Eigen::MatrixXd> src_map(data_.data(), cols, rows);
        Eigen::Map<Eigen::MatrixXd> dest_map(result->data_.data(), cols, new_shape[0]);
        dest_map = src_map.replicate(1, repeats);

        return result;
    } else {
        new_shape[1] *= repeats;
        auto result = make_shared<TensorData>(nullptr, new_shape);
        result->data_.resize(new_shape[0] * new_shape[1]);

        Eigen::Map<const Eigen::MatrixXd> src_map(data_.data(), cols, rows);
        Eigen::Map<Eigen::MatrixXd> dest_map(result->data_.data(), new_shape[1], rows);
        dest_map = src_map.replicate(repeats, 1);

        return result;
    }
}

data_ptr_t TensorData::tile(const std::vector<size_t> &reps) const {
    if (reps.size() != 2) {
        throw invalid_argument("Reps must specify repetition counts for both dimensions");
    }

    size_t row_reps = reps[0];
    size_t col_reps = reps[1];

    if (row_reps == 0 || col_reps == 0) {
        throw invalid_argument("Repetition counts must be positive");
    }

    const auto &shape_vec = shape();
    if (shape_vec.size() != 2) {
        throw invalid_argument("Tile operation currently only supports 2D tensors");
    }

    size_t rows = shape_vec[0];
    size_t cols = shape_vec[1];

    vector<size_t> new_shape = {rows * row_reps, cols * col_reps};

    auto result = make_shared<TensorData>(nullptr, new_shape);
    result->data_.resize(new_shape[0] * new_shape[1]);

    Eigen::Map<const Eigen::MatrixXd> src_map(data_.data(), cols, rows);
    Eigen::Map<Eigen::MatrixXd> dest_map(result->data_.data(), new_shape[1], new_shape[0]);
    dest_map = src_map.replicate(col_reps, row_reps);

    return result;
}

data_ptr_t TensorData::pad(size_t pad_width, double constant_value) const {
    if (pad_width == 0) {
        return clone(true);
    }

    const auto &shape_vec = shape();
    if (shape_vec.size() != 2) {
        throw invalid_argument("Pad operation currently only supports 2D tensors");
    }

    size_t rows = shape_vec[0];
    size_t cols = shape_vec[1];

    vector<size_t> new_shape = {rows + 2 * pad_width, cols + 2 * pad_width};

    auto result = make_shared<TensorData>(nullptr, new_shape);
    result->data_.resize(new_shape[0] * new_shape[1]);

    Eigen::Map<Eigen::MatrixXd> dest_map(result->data_.data(), new_shape[1], new_shape[0]);
    dest_map.setConstant(constant_value);

    Eigen::Map<const Eigen::MatrixXd> src_map(data_.data(), cols, rows);
    dest_map.block(pad_width, pad_width, cols, rows) = src_map;

    return result;
}

data_ptr_t TensorData::diag(const data_ptr_t &v) {
    auto v_tensor = tt::as_shared<TensorData>(v);
    if (!v_tensor) {
        throw invalid_argument("Input must be a TensorData");
    }

    const auto &shape_vec = v_tensor->shape();
    if (shape_vec.size() != 2) {
        throw invalid_argument("diag operation currently only supports 2D tensors");
    }

    size_t rows = shape_vec[0];
    size_t cols = shape_vec[1];

    if (rows == 1 || cols == 1) {
        size_t size = std::max(rows, cols);
        auto result = make_shared<TensorData>(nullptr, vector<size_t>{size, size});
        result->data_.resize(size * size);
        result->data_.setZero();

        if (rows == 1) {
            // Row vector
            Eigen::Map<Eigen::MatrixXd> dest_map(result->data_.data(), size, size);
            dest_map = v_tensor->data_.transpose().asDiagonal();
        } else {
            // Column vector
            Eigen::Map<Eigen::MatrixXd> dest_map(result->data_.data(), size, size);
            dest_map = v_tensor->data_.asDiagonal();
        }

        return result;
    } else {
        size_t diag_size = std::min(rows, cols);
        auto result = make_shared<TensorData>(nullptr, vector<size_t>{diag_size, 1});
        result->data_.resize(diag_size);

        Eigen::Map<const Eigen::MatrixXd> src_map(v_tensor->data_.data(), cols, rows);
        result->data_ = src_map.diagonal().transpose();

        return result;
    }
}

void TensorData::assign(const data_ptr_t &other) {
    auto other_tensor = tt::as_shared<TensorData>(other);
    if (!other_tensor) {
        throw invalid_argument("Can only assign from another TensorData");
    }

    if (shape_ != other_tensor->shape_) {
        throw invalid_argument("Tensor shapes must match for assignment");
    }

    data_ = other_tensor->data_;
}

size_t
TensorData::calculate_linear_index(const vector<size_t> &indices, const vector<size_t> &shape) {
    if (indices.size() != shape.size()) {
        throw invalid_argument("Indices size must match shape size");
    }

    size_t index = 0;
    size_t stride = 1;

    for (size_t i = indices.size(); i > 0; --i) {
        if (indices[i - 1] >= shape[i - 1]) {
            throw invalid_argument("Index out of bounds");
        }
        index += indices[i - 1] * stride;
        stride *= shape[i - 1];
    }

    return index;
}

bool TensorData::is_broadcastable(const vector<size_t> &shape1, const vector<size_t> &shape2) {
    size_t dim1 = shape1.size();
    size_t dim2 = shape2.size();
    size_t max_dim = std::max(dim1, dim2);

    for (size_t i = 0; i < max_dim; ++i) {
        size_t size1 = (i < dim1) ? shape1[dim1 - 1 - i] : 1;
        size_t size2 = (i < dim2) ? shape2[dim2 - 1 - i] : 1;

        if (size1 != size2 && size1 != 1 && size2 != 1) {
            return false;
        }
    }

    return true;
}

vector<size_t>
TensorData::broadcast_shape(const vector<size_t> &shape1, const vector<size_t> &shape2) {
    size_t dim1 = shape1.size();
    size_t dim2 = shape2.size();
    size_t max_dim = std::max(dim1, dim2);
    vector<size_t> result_shape(max_dim);

    for (size_t i = 0; i < max_dim; ++i) {
        size_t size1 = (i < dim1) ? shape1[dim1 - 1 - i] : 1;
        size_t size2 = (i < dim2) ? shape2[dim2 - 1 - i] : 1;

        if (size1 == size2) {
            result_shape[max_dim - 1 - i] = size1;
        } else if (size1 == 1) {
            result_shape[max_dim - 1 - i] = size2;
        } else if (size2 == 1) {
            result_shape[max_dim - 1 - i] = size1;
        } else {
            throw invalid_argument("Shapes are not broadcastable");
        }
    }

    return result_shape;
}

void TensorData::calculate_tensor_contraction_full(
    const TensorData &tensor1, const TensorData &tensor2, TensorData &result,
    size_t contraction_dims, vector<size_t> &index1, vector<size_t> &index2,
    vector<size_t> &index_result, size_t current_dim) {
    size_t dim1 = tensor1.shape_.size();

    if (current_dim == contraction_dims) {
        double sum = 0.0;
        vector<size_t> contraction_index(contraction_dims, 0);

        calculate_contraction_sum(tensor1, tensor2, contraction_index, index1, index2, 0, sum);

        size_t linear_index = calculate_linear_index(index_result, result.shape_);
        result.data_(linear_index) = sum;
        return;
    }

    size_t result_dim_size = result.shape_[current_dim];
    for (size_t i = 0; i < result_dim_size; ++i) {
        index_result[current_dim] = i;

        if (current_dim < dim1 - contraction_dims) {
            index1[current_dim] = i;
        }
        if (current_dim >= dim1 - contraction_dims) {
            size_t tensor2_dim = current_dim - (dim1 - contraction_dims);
            index2[contraction_dims + tensor2_dim] = i;
        }

        calculate_tensor_contraction_full(
            tensor1,
            tensor2,
            result,
            contraction_dims,
            index1,
            index2,
            index_result,
            current_dim + 1);
    }
}

void TensorData::calculate_contraction_sum(
    const TensorData &tensor1, const TensorData &tensor2, vector<size_t> &contraction_index,
    vector<size_t> &index1, vector<size_t> &index2, size_t current_contraction_dim, double &sum) {
    if (current_contraction_dim == contraction_index.size()) {
        size_t linear_index1 = calculate_linear_index(index1, tensor1.shape_);
        size_t linear_index2 = calculate_linear_index(index2, tensor2.shape_);
        sum += tensor1.data_(linear_index1) * tensor2.data_(linear_index2);
        return;
    }

    size_t contraction_size =
        tensor1.shape_[tensor1.shape_.size() - contraction_index.size() + current_contraction_dim];
    for (size_t i = 0; i < contraction_size; ++i) {
        contraction_index[current_contraction_dim] = i;

        index1[tensor1.shape_.size() - contraction_index.size() + current_contraction_dim] = i;
        index2[current_contraction_dim] = i;

        calculate_contraction_sum(
            tensor1,
            tensor2,
            contraction_index,
            index1,
            index2,
            current_contraction_dim + 1,
            sum);
    }
}

void TensorData::calculate_broadcast_multiplication(
    const TensorData &tensor1, const TensorData &tensor2, TensorData &result,
    vector<size_t> &index1, vector<size_t> &index2, vector<size_t> &index_result,
    size_t current_dim) {
    size_t dim_result = result.shape_.size();

    if (current_dim == dim_result) {
        vector<size_t> actual_index1(tensor1.shape_.size(), 0);
        vector<size_t> actual_index2(tensor2.shape_.size(), 0);

        for (size_t i = 0; i < tensor1.shape_.size(); ++i) {
            size_t result_dim = dim_result - tensor1.shape_.size() + i;
            if (tensor1.shape_[i] == 1) {
                actual_index1[i] = 0;
            } else {
                actual_index1[i] = index_result[result_dim];
            }
        }

        for (size_t i = 0; i < tensor2.shape_.size(); ++i) {
            size_t result_dim = dim_result - tensor2.shape_.size() + i;
            if (tensor2.shape_[i] == 1) {
                actual_index2[i] = 0;
            } else {
                actual_index2[i] = index_result[result_dim];
            }
        }

        size_t linear_index1 = calculate_linear_index(actual_index1, tensor1.shape_);
        size_t linear_index2 = calculate_linear_index(actual_index2, tensor2.shape_);
        size_t linear_index_result = calculate_linear_index(index_result, result.shape_);

        result.data_(linear_index_result) =
            tensor1.data_(linear_index1) * tensor2.data_(linear_index2);
        return;
    }

    size_t result_dim_size = result.shape_[current_dim];
    for (size_t i = 0; i < result_dim_size; ++i) {
        index_result[current_dim] = i;
        calculate_broadcast_multiplication(
            tensor1,
            tensor2,
            result,
            index1,
            index2,
            index_result,
            current_dim + 1);
    }
}

std::string TensorData::shape_to_string(const vector<size_t> &shape) {
    if (shape.empty()) {
        return "[]";
    }

    string result = "[";
    for (size_t i = 0; i < shape.size(); ++i) {
        result += to_string(shape[i]);
        if (i < shape.size() - 1) {
            result += ", ";
        }
    }
    result += "]";
    return result;
}

data_ptr_t TensorData::eye(size_t n) {
    auto result = make_shared<TensorData>(nullptr, std::vector<size_t>{n, n});
    result->data_.resize(n * n);
    result->data_.setZero();

    for (size_t i = 0; i < n; ++i) {
        result->data_(i * n + i) = 1.0;
    }

    return result;
}

data_ptr_t TensorData::zeros(const std::vector<size_t> &shape) {
    size_t total_size = 1;
    for (size_t dim : shape) {
        total_size *= dim;
    }

    auto result = make_shared<TensorData>(nullptr, shape);
    result->data_.resize(total_size);
    result->data_.setZero();

    return result;
}

data_ptr_t TensorData::ones(const std::vector<size_t> &shape) {
    size_t total_size = 1;
    for (size_t dim : shape) {
        total_size *= dim;
    }

    auto result = make_shared<TensorData>(nullptr, shape);
    result->data_.resize(total_size);
    result->data_.setConstant(1.0);

    return result;
}

data_ptr_t TensorData::linspace(double start, double stop, size_t num) {
    if (num == 0) {
        return make_shared<TensorData>(nullptr, std::vector<size_t>{0});
    }

    auto result = make_shared<TensorData>(nullptr, std::vector<size_t>{num});
    result->data_.resize(num);

    if (num == 1) {
        result->data_(0) = start;
    } else {
        double step = (stop - start) / (num - 1);
        for (size_t i = 0; i < num; ++i) {
            result->data_(i) = start + i * step;
        }
    }

    return result;
}

data_ptr_t TensorData::arange(double start, double stop, double step) {
    if (step == 0) {
        throw std::invalid_argument("Step must be non-zero");
    }

    size_t num;
    if ((step > 0 && start >= stop) || (step < 0 && start <= stop)) {
        num = 0;
    } else {
        num = static_cast<size_t>(std::ceil(std::abs((stop - start) / step)));
    }

    auto result = make_shared<TensorData>(nullptr, std::vector<size_t>{num});
    result->data_.resize(num);

    for (size_t i = 0; i < num; ++i) {
        result->data_(i) = start + i * step;
    }

    return result;
}

data_ptr_t TensorData::random(const std::vector<size_t> &shape, double lower, double upper) {
    size_t total_size = 1;
    for (size_t dim : shape) {
        total_size *= dim;
    }

    auto result = make_shared<TensorData>(nullptr, shape);
    result->data_.resize(total_size);

    if (lower == 0.0 && upper == 1.0) {
        result->data_.setRandom();
        result->data_ = (result->data_.array() + 1.0) / 2.0;
    } else {
        result->data_.setRandom();
        result->data_ = (result->data_.array() + 1.0) / 2.0;
        result->data_ = result->data_.array() * (upper - lower) + lower;
    }

    return result;
}

data_ptr_t TensorData::randn(const std::vector<size_t> &shape, double mean, double stddev) {
    size_t total_size = 1;
    for (size_t dim : shape) {
        total_size *= dim;
    }

    auto result = make_shared<TensorData>(nullptr, shape);
    result->data_.resize(total_size);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<double> dis(mean, stddev);

    result->data_ = Eigen::VectorXd::NullaryExpr(total_size, [&]() { return dis(gen); });

    return result;
}

data_ptr_t TensorData::transpose() const {
    if (shape_.size() != 2) {
        throw std::invalid_argument("Transpose currently only supports 2D tensors");
    }

    size_t rows = shape_[0];
    size_t cols = shape_[1];
    auto result = make_shared<TensorData>(nullptr, std::vector<size_t>{cols, rows});
    result->data_.resize(rows * cols);

    Eigen::Map<const Eigen::MatrixXd> src_map(data_.data(), rows, cols);
    Eigen::Map<Eigen::MatrixXd> dest_map(result->data_.data(), cols, rows);
    dest_map = src_map.transpose();

    return result;
}
data_ptr_t TensorData::flatten() const {
    size_t total_size = 1;
    for (size_t dim : shape_) {
        total_size *= dim;
    }

    auto result = make_shared<TensorData>(nullptr, std::vector<size_t>{total_size});
    result->data_ = data_;

    return result;
}

data_ptr_t TensorData::sum() const {
    auto result = make_shared<FloatData>(static_cast<float>(data_.sum()));
    return result;
}

data_ptr_t TensorData::mean() const {
    if (data_.size() == 0) {
        throw std::runtime_error("Cannot calculate mean of empty tensor");
    }
    auto result = make_shared<FloatData>(static_cast<float>(data_.mean()));
    return result;
}

data_ptr_t TensorData::min() const {
    if (data_.size() == 0) {
        throw std::runtime_error("Cannot find minimum of empty tensor");
    }
    auto result = make_shared<FloatData>(static_cast<float>(data_.minCoeff()));
    return result;
}

data_ptr_t TensorData::max() const {
    if (data_.size() == 0) {
        throw std::runtime_error("Cannot find maximum of empty tensor");
    }
    auto result = make_shared<FloatData>(static_cast<float>(data_.maxCoeff()));
    return result;
}

data_ptr_t TensorData::norm_l1() const {
    auto result = make_shared<FloatData>(static_cast<float>(data_.array().abs().sum()));
    return result;
}

data_ptr_t TensorData::norm_l2() const {
    auto result =
        make_shared<FloatData>(static_cast<float>(std::sqrt(data_.array().square().sum())));
    return result;
}

data_ptr_t TensorData::norm_squared_l2() const {
    auto result = make_shared<FloatData>(static_cast<float>(data_.array().square().sum()));
    return result;
}
data_ptr_t TensorData::sin() const {
    auto result = make_shared<TensorData>(nullptr, shape_);
    result->data_ = data_.array().sin();
    return result;
}

data_ptr_t TensorData::cos() const {
    auto result = make_shared<TensorData>(nullptr, shape_);
    result->data_ = data_.array().cos();
    return result;
}

data_ptr_t TensorData::exp() const {
    auto result = make_shared<TensorData>(nullptr, shape_);
    result->data_ = data_.array().exp();
    return result;
}

data_ptr_t TensorData::log() const {
    auto result = make_shared<TensorData>(nullptr, shape_);
    result->data_ = data_.array().log();
    return result;
}

data_ptr_t TensorData::sqrt() const {
    auto result = make_shared<TensorData>(nullptr, shape_);
    result->data_ = data_.array().sqrt();
    return result;
}

data_ptr_t TensorData::abs() const {
    auto result = make_shared<TensorData>(nullptr, shape_);
    result->data_ = data_.array().abs();
    return result;
}

data_ptr_t TensorData::square() const {
    auto result = make_shared<TensorData>(nullptr, shape_);
    result->data_ = data_.array().square();
    return result;
}
