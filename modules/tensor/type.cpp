#include "type.h"

#include "camel/core/mm.h"
#include "camel/utils/assert.h"

namespace mm = camel::core::mm;

namespace camel::tensor {

using namespace camel::core::type;

TensorType::TensorType(const std::vector<size_t> &shape, Type *elementType)
    : OtherType(typeCode()), shape_(shape),
      elementType_(elementType ? elementType : Type::Float32()) {}

TensorType::TensorType(TypeCode code, size_t paramCount, Type **params)
    : OtherType(code, paramCount, params), shape_(), elementType_(Type::Float32()) {}

TypeCode TensorType::typeCode() {
    static TypeCode code = registerOtherType("Tensor", TypeFlag::Composite | TypeFlag::GC_Traced);
    return code;
}

TensorType *TensorType::create(Type *elementType, const std::vector<size_t> &shape) {
    void *mem = mm::autoSpace().alloc(sizeof(TensorType), alignof(TensorType));
    ASSERT(mem != nullptr, "Failed to allocate TensorType from autoSpace");
    return new (mem) TensorType(shape, elementType);
}

TensorType *TensorType::Dynamic(Type *elementType) {
    static TensorType *floatDynamic = TensorType::create(Type::Float32(), {});
    if (!elementType || elementType->equals(Type::Float32())) {
        return floatDynamic;
    }
    return TensorType::create(elementType, {});
}

Type *TensorType::Default() { return Dynamic(Type::Float32()); }

std::vector<size_t> TensorType::shape() const { return shape_; }

Type *TensorType::dType() const { return elementType_ ? elementType_ : Type::Float32(); }

std::string TensorType::toString() const {
    if (shape_.empty()) {
        return dType()->equals(Type::Float32()) ? "Tensor" : "Tensor<" + dType()->toString() + ">";
    }
    std::string result = "Tensor<[";
    for (size_t i = 0; i < shape_.size(); ++i) {
        if (i > 0) {
            result += ", ";
        }
        result += std::to_string(shape_[i]);
    }
    result += "]";
    if (!dType()->equals(Type::Float32())) {
        result += ", " + dType()->toString();
    }
    result += ">";
    return result;
}

std::string TensorType::mangle() const {
    std::string result = "T";
    if (shape_.empty()) {
        result += "_";
    } else {
        for (size_t i = 0; i < shape_.size(); ++i) {
            if (i > 0) {
                result += ",";
            }
            result += std::to_string(shape_[i]);
        }
    }
    result += ";";
    result += dType()->mangle();
    return result;
}

Type *TensorType::clone(bool deep) const {
    Type *dtype = deep ? dType()->clone(true) : dType();
    return TensorType::create(dtype, shape_);
}

bool TensorType::equals(Type *other) const {
    if (this == other) {
        return true;
    }
    auto *rhs = dynamic_cast<TensorType *>(other);
    if (!rhs) {
        return false;
    }
    return shape_ == rhs->shape() && dType()->equals(rhs->dType());
}

CastSafety TensorType::castSafetyFrom(Type *sourceType) const {
    if (auto r = Type::checkCastSafetyWithAny(code(), sourceType)) {
        return *r;
    }
    return assignableFrom(sourceType) ? CastSafety::Safe : CastSafety::Forbidden;
}

bool TensorType::assignableFrom(Type *sourceType) const {
    auto *rhs = dynamic_cast<TensorType *>(sourceType);
    if (!rhs) {
        return false;
    }
    bool dtypeCompat = dType()->equals(Type::Float32()) || dType()->equals(rhs->dType());
    bool shapeCompat = shape_.empty() || rhs->shape().empty() || shape_ == rhs->shape();
    return dtypeCompat && shapeCompat;
}

OtherType *TensorType::cloneWithParams(std::span<Type *const> params) const {
    if (params.empty()) {
        return TensorType::Dynamic();
    }
    if (params.size() == 1) {
        return TensorType::Dynamic(params[0]);
    }
    Type **p  = OtherType::copyParams(params);
    void *mem = mm::autoSpace().alloc(sizeof(TensorType), alignof(TensorType));
    ASSERT(mem != nullptr, "Failed to allocate TensorType from autoSpace");
    return new (mem) TensorType(typeCode(), params.size(), p);
}

TensorType *getTensorType(Type *dtype) { return TensorType::Dynamic(dtype); }

} // namespace camel::tensor
