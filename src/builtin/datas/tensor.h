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
 * Updated: Dec. 23, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

// #include "core/data/other/other.h"
// #include "core/type/other.h"
#include "core/rtdata/array.h"

// #include <memory>
// #include <vector>

#include <Eigen/Dense>

#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <new>
#include <ostream>
#include <stdexcept>
#include <vector>

// 设备类型枚举
enum class DeviceType : uint8_t {
    CPU = 0,
    GPU = 1
    // 可以扩展 TPU, NPU, FPGA ...
};

// class Tensor : public Object {
//   public:
//     using DimType = uint16_t;

//     struct DimInfo {
//         DimType size;
//         DimType stride;
//     };

//     // ====== 禁止直接构造 ======
//     Tensor(const Tensor &)            = delete;
//     Tensor &operator=(const Tensor &) = delete;

//     // ====== 静态创建方法 ======
//     static Tensor *create(
//         TypeCode dtype, DeviceType deviceType, int32_t deviceId, const std::vector<size_t>
//         &shape, IAllocator &allocator) { size_t ndim     = shape.size(); size_t elemSize =
//         typeSize(dtype);

//         // 计算 total 元素数
//         size_t totalElems = 1;
//         for (auto s : shape)
//             totalElems *= s;

//         // 对象头部 + 数据
//         size_t headerSize = sizeof(Tensor) + ndim * sizeof(DimInfo);
//         size_t dataSize   = totalElems * elemSize;
//         size_t totalSize  = headerSize + dataSize;

//         void *mem = allocator.alloc(totalSize, alignof(Tensor));
//         if (!mem)
//             throw std::bad_alloc();

//         Tensor *t = new (mem) Tensor(dtype, deviceType, deviceId, ndim, totalElems);

//         // 设置 dims
//         DimInfo *dimArr = t->dims();
//         for (size_t i = 0; i < ndim; ++i) {
//             dimArr[i].size = static_cast<DimType>(shape[i]);
//         }
//         computeStrides(dimArr, ndim);

//         return t;
//     }

//     // ====== 基本属性访问 ======
//     size_t ndim() const { return ndim_; }
//     size_t size() const { return size_; }
//     TypeCode dtype() const { return dtype_; }
//     DeviceType deviceType() const { return deviceType_; }
//     int32_t deviceId() const { return deviceId_; }
//     const DimInfo *dims() const { return reinterpret_cast<const DimInfo *>(this + 1); }
//     DimInfo *dims() { return reinterpret_cast<DimInfo *>(this + 1); }

//     void *data() { return reinterpret_cast<std::byte *>(dims()) + ndim_ * sizeof(DimInfo); }
//     const void *data() const {
//         return reinterpret_cast<const std::byte *>(dims()) + ndim_ * sizeof(DimInfo);
//     }

//     // ====== Object 接口实现 ======
//     virtual bool equals(const Object *other, bool deep = false) const override {
//         if (this == other)
//             return true;
//         if (!isOfSameCls(this, other))
//             return false;

//         const Tensor *o = reinterpret_cast<const Tensor *>(other);
//         if (dtype_ != o->dtype_ || deviceType_ != o->deviceType_ || deviceId_ != o->deviceId_ ||
//             ndim_ != o->ndim_ || size_ != o->size_)
//             return false;

//         // shape/stride
//         if (std::memcmp(dims(), o->dims(), ndim_ * sizeof(DimInfo)) != 0)
//             return false;

//         // 比较数据
//         if (isGCTraced(dtype_)) {
//             const Object *const *arrA = reinterpret_cast<const Object *const *>(data());
//             const Object *const *arrB = reinterpret_cast<const Object *const *>(o->data());
//             if (deep) {
//                 for (size_t i = 0; i < size_; ++i) {
//                     const Object *a = arrA[i];
//                     const Object *b = arrB[i];
//                     if (a == b)
//                         continue;
//                     if (!a->equals(b, true))
//                         return false;
//                 }
//                 return true;
//             } else {
//                 return std::memcmp(arrA, arrB, size_ * sizeof(Object *)) == 0;
//             }
//         } else {
//             return std::memcmp(data(), o->data(), size_ * typeSize(dtype_)) == 0;
//         }
//     }

//     virtual Object *clone(IAllocator &allocator, bool deep = false) const override {
//         std::vector<size_t> shape;
//         const DimInfo *d = dims();
//         for (size_t i = 0; i < ndim_; ++i)
//             shape.push_back(d[i].size);

//         Tensor *t = Tensor::create(dtype_, deviceType_, deviceId_, shape, allocator);

//         if (isGCTraced(dtype_)) {
//             const Object *const *src = reinterpret_cast<const Object *const *>(data());
//             Object **dst             = reinterpret_cast<Object **>(t->data());
//             for (size_t i = 0; i < size_; ++i) {
//                 const Object *orig = src[i];
//                 if (orig) {
//                     dst[i] = deep ? orig->clone(allocator, true) : const_cast<Object *>(orig);
//                 } else {
//                     dst[i] = NullRef;
//                 }
//             }
//         } else {
//             std::memcpy(t->data(), data(), size_ * typeSize(dtype_));
//         }
//         return reinterpret_cast<Object *>(t);
//     }

//     virtual void print(std::ostream &os) const override {
//         os << "Tensor(" << ndim_ << "D, dtype=" << int(dtype_) << ", device=" << int(deviceType_)
//            << ", shape=[";
//         for (size_t i = 0; i < ndim_; ++i) {
//             os << dims()[i].size;
//             if (i + 1 < ndim_)
//                 os << ",";
//         }
//         os << "], strides=[";
//         for (size_t i = 0; i < ndim_; ++i) {
//             os << dims()[i].stride;
//             if (i + 1 < ndim_)
//                 os << ",";
//         }
//         os << "])";
//     }

//     virtual void onMoved() override {
//         // 因为 dims 和 data 都是 FAM 内部，内存相对对象本身是固定偏移，所以不需要更新
//     }

//     virtual void updateRefs(const std::function<Object *(Object *)> &relocate) override {
//         if (!isGCTraced(dtype_))
//             return;
//         Object **ptr = reinterpret_cast<Object **>(data());
//         for (size_t i = 0; i < size_; ++i) {
//             if (ptr[i]) {
//                 ptr[i] = relocate(ptr[i]);
//             }
//         }
//     }

//   private:
//     Tensor(TypeCode dtype, DeviceType deviceType, int32_t deviceId, size_t ndim, size_t size)
//         : dtype_(dtype), deviceType_(deviceType), deviceId_(deviceId),
//           ndim_(static_cast<uint8_t>(ndim)), size_(size) {}

//     static void computeStrides(DimInfo *dims, size_t ndim) {
//         size_t stride = 1;
//         for (size_t i = ndim; i-- > 0;) {
//             dims[i].stride = static_cast<DimType>(stride);
//             stride *= dims[i].size;
//         }
//     }

//     TypeCode dtype_;
//     DeviceType deviceType_;
//     int32_t deviceId_;
//     uint8_t ndim_;
//     size_t size_;
//     // 之后紧跟 DimInfo dims_[ndim_] + 数据 data_[]
// };

// class TensorData : public OtherData {
//   private:
//     std::vector<data_ptr_t> refs_;
//     std::vector<size_t> shape_;
//     Eigen::VectorXd data_;
//     size_t calculate_linear_index(const std::vector<size_t> &index) const;

//     static size_t
//     calculate_linear_index(const std::vector<size_t> &indices, const std::vector<size_t> &shape);
//     static bool
//     is_broadcastable(const std::vector<size_t> &shape1, const std::vector<size_t> &shape2);
//     static std::vector<size_t>
//     broadcast_shape(const std::vector<size_t> &shape1, const std::vector<size_t> &shape2);
//     static void calculate_tensor_contraction_full(
//         const TensorData &tensor1, const TensorData &tensor2, TensorData &result,
//         size_t contraction_dims, std::vector<size_t> &index1, std::vector<size_t> &index2,
//         std::vector<size_t> &index_result, size_t current_dim);
//     static void calculate_contraction_sum(
//         const TensorData &tensor1, const TensorData &tensor2,
//         std::vector<size_t> &contraction_index, std::vector<size_t> &index1,
//         std::vector<size_t> &index2, size_t current_contraction_dim, double &sum);
//     static void calculate_broadcast_addition(
//         const TensorData &tensor1, const TensorData &tensor2, TensorData &result,
//         std::vector<size_t> &index_result, size_t current_dim);
//     static void calculate_broadcast_subtraction(
//         const TensorData &tensor1, const TensorData &tensor2, TensorData &result,
//         std::vector<size_t> &index_result, size_t current_dim);
//     static void calculate_broadcast_multiplication(
//         const TensorData &tensor1, const TensorData &tensor2, TensorData &result,
//         std::vector<size_t> &index1, std::vector<size_t> &index2, std::vector<size_t>
//         &index_result, size_t current_dim);
//     static void calculate_broadcast_division(
//         const TensorData &tensor1, const TensorData &tensor2, TensorData &result,
//         std::vector<size_t> &index_result, size_t current_dim);
//     static void calculate_broadcast_power(
//         const TensorData &tensor1, const TensorData &tensor2, TensorData &result,
//         std::vector<size_t> &index_result, size_t current_dim);
//     static std::string shape_to_string(const std::vector<size_t> &shape);

//   public:
//     TensorData(const type_ptr_t &elementType, const std::vector<size_t> &shape);
//     ~TensorData();
//     TensorData(TensorData &&other) noexcept;
//     TensorData &operator=(TensorData &&other) noexcept;

//     TensorData(const TensorData &) = delete;
//     TensorData &operator=(const TensorData &) = delete;

//     // Basic Access and Operators
//     data_ptr_t at(const std::vector<size_t> &index) const;

//     double &operator()(const std::vector<size_t> &index);
//     const double &operator()(const std::vector<size_t> &index) const;

//     std::vector<size_t> shape() const;
//     size_t dimension() const;
//     size_t size() const;
//     double *data_ptr();
//     const double *data_ptr() const;

//     // Virtual Functions from StructData
//     virtual std::vector<std::string> refs() const override;
//     virtual bool resolved() const override { return refs_.empty(); }
//     virtual void resolve(const data_vec_t &dataList) override;

//     virtual bool equals(const data_ptr_t &other) const override;
//     virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override;
//     virtual data_ptr_t clone(bool deep = false) const override;
//     virtual const std::string toString() const override;
//     const std::string toFormattedString(size_t maxItemsPerDim = 6) const;
//     virtual void print(std::ostream &os) const override;

//     // Basic Arithmetic Operations
//     data_ptr_t add(const data_ptr_t &other) const;
//     data_ptr_t subtract(const data_ptr_t &other) const;
//     data_ptr_t multiply(const data_ptr_t &other) const;
//     data_ptr_t matmul(const data_ptr_t &other) const;
//     data_ptr_t divide(const data_ptr_t &other) const;

//     // Shape Transformation Operations
//     data_ptr_t reshape(const std::vector<size_t> &shape) const;
//     data_ptr_t transpose() const;
//     data_ptr_t flatten() const;
//     data_ptr_t expand_dims(size_t axis) const;
//     data_ptr_t squeeze() const;

//     // Tensor Combination Operations
//     data_ptr_t concat(const data_ptr_t &other, size_t axis = 0) const;
//     data_ptr_t stack(const data_ptr_t &other, size_t axis = 0) const;

//     // Statistical and Aggregation Operations
//     data_ptr_t sum() const;
//     data_ptr_t mean() const;
//     data_ptr_t min() const;
//     data_ptr_t max() const;
//     data_ptr_t argmin() const;
//     data_ptr_t argmax() const;
//     data_ptr_t var() const;
//     data_ptr_t std() const;

//     // Norm Calculations
//     data_ptr_t norm_l1() const;
//     data_ptr_t norm_l2() const;
//     data_ptr_t norm_squared_l2() const;

//     // Basic Mathematical Functions
//     data_ptr_t sin() const;
//     data_ptr_t cos() const;
//     data_ptr_t exp() const;
//     data_ptr_t log() const;
//     data_ptr_t sqrt() const;

//     // Hyperbolic Functions
//     data_ptr_t sinh() const;
//     data_ptr_t cosh() const;
//     data_ptr_t tanh() const;

//     // Power Functions
//     data_ptr_t pow(double exponent) const;
//     data_ptr_t pow(const data_ptr_t &exponent) const; // Element-wise power with tensor exponent
//     data_ptr_t matpow(size_t exponent) const;         // Matrix power

//     data_ptr_t square() const;
//     data_ptr_t cube() const;

//     // Other Mathematical Functions
//     data_ptr_t abs() const;
//     data_ptr_t ceil() const;
//     data_ptr_t floor() const;
//     data_ptr_t round() const;

//     // Indexing and Slicing Operations
//     data_ptr_t take(const std::vector<size_t> &indices, size_t axis) const;
//     data_ptr_t put(const std::vector<size_t> &indices, const data_ptr_t &values) const;
//     data_ptr_t slice(size_t start, size_t end, size_t row) const;

//     // Comparison Operations
//     data_ptr_t equal(const data_ptr_t &other) const;
//     data_ptr_t not_equal(const data_ptr_t &other) const;
//     data_ptr_t greater(const data_ptr_t &other) const;
//     data_ptr_t less(const data_ptr_t &other) const;
//     data_ptr_t greater_equal(const data_ptr_t &other) const;
//     data_ptr_t less_equal(const data_ptr_t &other) const;

//     // Logical Operations
//     data_ptr_t logical_and(const data_ptr_t &other) const;
//     data_ptr_t logical_or(const data_ptr_t &other) const;
//     data_ptr_t logical_not() const;

//     // Conditional Operations
//     data_ptr_t where(const data_ptr_t &condition, const data_ptr_t &other) const;

//     // Flip and Mirror Operations
//     data_ptr_t flip(size_t axis) const;
//     data_ptr_t flipud() const;
//     data_ptr_t fliplr() const;

//     // Repetition and Padding Operations
//     data_ptr_t repeat(size_t repeats, size_t axis) const;
//     data_ptr_t tile(const std::vector<size_t> &reps) const;
//     data_ptr_t pad(size_t pad_width, double constant_value = 0.0) const;

//     void assign(const data_ptr_t &other);

//     // Static Factory Methods
//     static data_ptr_t eye(size_t n);
//     static data_ptr_t diag(const data_ptr_t &v);
//     static data_ptr_t linspace(double start, double stop, size_t num);
//     static data_ptr_t arange(double start, double stop, double step = 1.0);
//     static data_ptr_t zeros(const std::vector<size_t> &shape);
//     static data_ptr_t ones(const std::vector<size_t> &shape);
//     static data_ptr_t
//     random(const std::vector<size_t> &shape, double lower = 0.0, double upper = 1.0);
//     static data_ptr_t
//     randn(const std::vector<size_t> &shape, double mean = 0.0, double stddev = 1.0);
// };
