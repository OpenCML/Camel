// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/data/ref.h"

using namespace camel::core::type;
namespace camel::core::data {

RefData::RefData(const std::string &ref) : Data(type::Type::Ref()), ref_(ref) {}

const std::string &RefData::ref() const { return ref_; }

bool RefData::equals(const data_ptr_t &other) const { return false; }

data_ptr_t RefData::clone(bool deep) const { return std::make_shared<RefData>(ref_); }

const std::string RefData::toString() const { return "DREF<" + ref_ + ">"; }

data_ptr_t RefData::convertTo(type::Type *type) {
    if (type->equals(type_)) {
        return std::make_shared<RefData>(ref_);
    }
    return nullptr;
}

} // namespace camel::core::data
