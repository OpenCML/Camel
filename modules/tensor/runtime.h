#pragma once

#include "camel/core/mm/alloc/allocator.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/type/base.h"
#include "dlpack.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace camel::tensor {

namespace mm     = camel::core::mm;
namespace rtdata = camel::core::rtdata;
namespace type   = camel::core::type;

enum class BinaryOp {
    Add,
    Subtract,
    Multiply,
    Divide,
    Power,
};

enum class CompareOp {
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
};

class TensorObject : public rtdata::Object {
  public:
    TensorObject(const TensorObject &)            = delete;
    TensorObject &operator=(const TensorObject &) = delete;

    static TensorObject *create(
        type::TypeCode dtype, std::span<const int64_t> shape, mm::IAllocator &allocator,
        bool zeroInit = false);

    size_t rank() const { return rank_; }
    uint64_t numel() const { return numel_; }
    type::TypeCode dtype() const { return dtype_; }
    size_t byteSize() const { return byteSize_; }
    const int64_t *shape() const { return shape_; }
    int64_t dim(size_t index) const;
    const std::byte *rawData() const { return data_; }
    std::byte *rawData() { return data_; }

    template <typename T> T *dataAs() { return reinterpret_cast<T *>(data_); }
    template <typename T> const T *dataAs() const { return reinterpret_cast<const T *>(data_); }

    bool sameShape(const TensorObject *other) const;
    double getAsDouble(uint64_t flatIndex) const;
    int64_t getAsInt64(uint64_t flatIndex) const;
    bool getAsBool(uint64_t flatIndex) const;
    void setFromDouble(uint64_t flatIndex, double value);
    void setFromInt64(uint64_t flatIndex, int64_t value);
    void setFromBool(uint64_t flatIndex, bool value);

    bool
    equals(const rtdata::Object *other, const type::Type *type, bool deep = false) const override;
    rtdata::Object *
    clone(mm::IAllocator &allocator, const type::Type *type, bool deep = false) const override;
    void print(std::ostream &os, const type::Type *type) const override;
    void onMoved() override;
    void updateRefs(
        const std::function<rtdata::Object *(rtdata::Object *)> &relocate,
        const type::Type *type) override;

  private:
    TensorObject(type::TypeCode dtype, uint32_t rank, uint64_t numel, uint64_t byteSize);
    void refreshPointers();
    void printRecursive(std::ostream &os, size_t dimIndex, uint64_t &flatIndex) const;

    type::TypeCode dtype_;
    uint32_t rank_;
    uint64_t numel_;
    uint64_t byteSize_;
    int64_t *shape_;
    std::byte *data_;
    std::byte storage_[];
};

size_t elementSize(type::TypeCode dtype);
bool isSupportedTensorScalar(type::TypeCode code);
bool isFloatingTensorType(type::TypeCode code);
type::TypeCode normalizeTensorDType(type::TypeCode code);
type::TypeCode tensorDTypeFromValueType(type::TypeCode code);
type::TypeCode promoteTensorTypes(type::TypeCode lhs, type::TypeCode rhs);

std::vector<int64_t> parseShapeArray(const ::Array *shapeArray, const type::Type *shapeType);
::Array *makeShapeArray(const TensorObject *tensor, mm::IAllocator &allocator);

TensorObject *
tensorEmpty(type::TypeCode dtype, std::span<const int64_t> shape, mm::IAllocator &allocator);
TensorObject *
tensorZeros(type::TypeCode dtype, std::span<const int64_t> shape, mm::IAllocator &allocator);
TensorObject *
tensorOnes(type::TypeCode dtype, std::span<const int64_t> shape, mm::IAllocator &allocator);
TensorObject *tensorFull(
    type::TypeCode dtype, std::span<const int64_t> shape, double value, mm::IAllocator &allocator);
TensorObject *
tensorRandom(std::span<const int64_t> shape, double low, double high, mm::IAllocator &allocator);
TensorObject *tensorArange(int64_t start, int64_t stop, int64_t step, mm::IAllocator &allocator);
TensorObject *tensorEye(int64_t size, mm::IAllocator &allocator);
TensorObject *tensorReshape(
    const TensorObject *tensor, std::span<const int64_t> shape, mm::IAllocator &allocator);
TensorObject *tensorTranspose2D(const TensorObject *tensor, mm::IAllocator &allocator);
TensorObject *tensorConcat(
    const TensorObject *lhs, const TensorObject *rhs, int64_t axis, mm::IAllocator &allocator);
TensorObject *tensorBinary(
    const TensorObject *lhs, const TensorObject *rhs, BinaryOp op, mm::IAllocator &allocator);
TensorObject *tensorBinaryScalarRight(
    const TensorObject *lhs, type::TypeCode rhsType, slot_t rhs, BinaryOp op,
    mm::IAllocator &allocator);
TensorObject *tensorBinaryScalarLeft(
    type::TypeCode lhsType, slot_t lhs, const TensorObject *rhs, BinaryOp op,
    mm::IAllocator &allocator);
TensorObject *tensorCompareScalarRight(
    const TensorObject *lhs, type::TypeCode rhsType, slot_t rhs, CompareOp op,
    mm::IAllocator &allocator);
TensorObject *tensorCompareScalarLeft(
    type::TypeCode lhsType, slot_t lhs, const TensorObject *rhs, CompareOp op,
    mm::IAllocator &allocator);
TensorObject *tensorCompare(
    const TensorObject *lhs, const TensorObject *rhs, CompareOp op, mm::IAllocator &allocator);

double tensorSum(const TensorObject *tensor);
double tensorIndex1D(const TensorObject *tensor, int64_t index);
double tensorIndex2D(const TensorObject *tensor, int64_t row, int64_t col);

TensorObject *tensorFromArray(slot_t slot, type::Type *valueType, mm::IAllocator &allocator);
DLManagedTensor *tensorToDLPackCopy(const TensorObject *tensor);
TensorObject *tensorFromDLPackCopy(const DLManagedTensor *managed, mm::IAllocator &allocator);

} // namespace camel::tensor
