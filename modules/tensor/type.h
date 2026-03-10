#pragma once

#include "camel/core/type/other.h"

#include <span>
#include <unordered_map>
#include <vector>

namespace camel::tensor {

namespace type = camel::core::type;

class TensorType : public type::OtherType {
  public:
    TensorType(const std::vector<size_t> &shape, type::Type *elementType = nullptr);

  protected:
    TensorType(type::TypeCode code, size_t paramCount, type::Type **params);

  public:
    static type::TypeCode typeCode();
    static TensorType *create(type::Type *elementType, const std::vector<size_t> &shape);
    static TensorType *Dynamic(type::Type *elementType = nullptr);
    static type::Type *Default();

    std::vector<size_t> shape() const;
    type::Type *dType() const;

    std::string toString() const override;
    std::string mangle() const override;
    type::Type *clone(bool deep = false) const override;
    bool equals(type::Type *type) const override;
    type::CastSafety castSafetyFrom(type::Type *sourceType) const override;
    bool assignableFrom(type::Type *sourceType) const override;
    type::OtherType *cloneWithParams(std::span<type::Type *const> params) const override;

  private:
    std::vector<size_t> shape_;
    type::Type *elementType_;
};

TensorType *getTensorType(type::Type *dtype = nullptr);

} // namespace camel::tensor
