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
 * Updated: Oct. 12, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/data/primary.h"
#include "struct.h"

#include <memory>
#include <vector>

#include <Eigen/Dense>

class TensorData : public StructData {
  private:
    std::vector<data_ptr_t> refs_;
    std::vector<size_t> shape_;
    Eigen::VectorXd data_;  // 使用Eigen向量替代std::vector<double>

    size_t calculate_linear_index(const std::vector<size_t> &index) const;

    // Helper functions for tensor multiplication
    static size_t
    calculate_linear_index(const std::vector<size_t> &indices, const std::vector<size_t> &shape);
    static bool
    is_broadcastable(const std::vector<size_t> &shape1, const std::vector<size_t> &shape2);
    static std::vector<size_t>
    broadcast_shape(const std::vector<size_t> &shape1, const std::vector<size_t> &shape2);
    static void calculate_tensor_contraction_full(
        const TensorData &tensor1, const TensorData &tensor2, TensorData &result,
        size_t contraction_dims, std::vector<size_t> &index1, std::vector<size_t> &index2,
        std::vector<size_t> &index_result, size_t current_dim);
    static void calculate_contraction_sum(
        const TensorData &tensor1, const TensorData &tensor2,
        std::vector<size_t> &contraction_index, std::vector<size_t> &index1,
        std::vector<size_t> &index2, size_t current_contraction_dim, double &sum);
    static void calculate_broadcast_multiplication(
        const TensorData &tensor1, const TensorData &tensor2, TensorData &result,
        std::vector<size_t> &index1, std::vector<size_t> &index2, std::vector<size_t> &index_result,
        size_t current_dim);
    static std::string shape_to_string(const std::vector<size_t> &shape);

  public:
    // Constructors and Destructors
    TensorData(const type_ptr_t &elementType, const std::vector<size_t> &shape);
    ~TensorData();

    TensorData(TensorData &&other) noexcept;
    TensorData &operator=(TensorData &&other) noexcept;

    TensorData(const TensorData &) = delete;
    TensorData &operator=(const TensorData &) = delete;

    // Basic Access and Operators
    data_ptr_t at(const std::vector<size_t> &index) const;

    double &operator()(const std::vector<size_t> &index);
    const double &operator()(const std::vector<size_t> &index) const;

    std::vector<size_t> shape() const;
    size_t dimension() const;
    size_t size() const;
    double *data_ptr();
    const double *data_ptr() const;

    // Virtual Functions from StructData
    virtual std::vector<std::string> refs() const override;
    virtual bool resolved() const override { return refs_.empty(); }
    virtual void resolve(const data_vec_t &dataList) override;

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
    virtual void print(std::ostream &os) const override;

    // Basic Arithmetic Operations
    data_ptr_t add(const data_ptr_t &other) const;
    data_ptr_t subtract(const data_ptr_t &other) const;
    data_ptr_t multiply(const data_ptr_t &other) const;
    data_ptr_t divide(const data_ptr_t &other) const;

    // Shape Transformation Operations
    data_ptr_t reshape(const std::vector<size_t> &shape) const;
    data_ptr_t transpose() const;
    data_ptr_t flatten() const;
    data_ptr_t expand_dims(size_t axis) const;
    data_ptr_t squeeze() const;

    // Tensor Combination Operations
    data_ptr_t concat(const data_ptr_t &other, size_t axis = 0) const;
    data_ptr_t stack(const data_ptr_t &other, size_t axis = 0) const;

    // Statistical and Aggregation Operations
    data_ptr_t sum() const;
    data_ptr_t mean() const;
    data_ptr_t min() const;
    data_ptr_t max() const;
    data_ptr_t argmin() const;
    data_ptr_t argmax() const;
    data_ptr_t var() const;
    data_ptr_t std() const;

    // Norm Calculations
    data_ptr_t norm_l1() const;
    data_ptr_t norm_l2() const;
    data_ptr_t norm_squared_l2() const;

    // Basic Mathematical Functions
    data_ptr_t sin() const;
    data_ptr_t cos() const;
    data_ptr_t exp() const;
    data_ptr_t log() const;
    data_ptr_t sqrt() const;

    // Hyperbolic Functions
    data_ptr_t sinh() const;
    data_ptr_t cosh() const;
    data_ptr_t tanh() const;

    // Power Functions
    data_ptr_t pow(double exponent) const;
    data_ptr_t square() const;
    data_ptr_t cube() const;

    // Other Mathematical Functions
    data_ptr_t abs() const;
    data_ptr_t ceil() const;
    data_ptr_t floor() const;
    data_ptr_t round() const;

    // Indexing and Slicing Operations
    data_ptr_t take(const std::vector<size_t> &indices, size_t axis) const;
    data_ptr_t put(const std::vector<size_t> &indices, const data_ptr_t &values) const;
    data_ptr_t slice(const std::vector<std::pair<size_t, size_t>> &slices) const;

    // Comparison Operations
    data_ptr_t equal(const data_ptr_t &other) const;
    data_ptr_t not_equal(const data_ptr_t &other) const;
    data_ptr_t greater(const data_ptr_t &other) const;
    data_ptr_t less(const data_ptr_t &other) const;
    data_ptr_t greater_equal(const data_ptr_t &other) const;
    data_ptr_t less_equal(const data_ptr_t &other) const;

    // Logical Operations
    data_ptr_t logical_and(const data_ptr_t &other) const;
    data_ptr_t logical_or(const data_ptr_t &other) const;
    data_ptr_t logical_not() const;

    // Conditional Operations
    data_ptr_t where(const data_ptr_t &condition, const data_ptr_t &other) const;

    // Flip and Mirror Operations
    data_ptr_t flip(size_t axis) const;
    data_ptr_t flipud() const;
    data_ptr_t fliplr() const;

    // Repetition and Padding Operations
    data_ptr_t repeat(size_t repeats, size_t axis) const;
    data_ptr_t tile(const std::vector<size_t> &reps) const;
    data_ptr_t pad(size_t pad_width, double constant_value = 0.0) const;

    // Static Factory Methods
    static data_ptr_t eye(size_t n);
    static data_ptr_t diag(const data_ptr_t &v);
    static data_ptr_t linspace(double start, double stop, size_t num);
    static data_ptr_t arange(double start, double stop, double step = 1.0);
    static data_ptr_t zeros(const std::vector<size_t> &shape);
    static data_ptr_t ones(const std::vector<size_t> &shape);
};