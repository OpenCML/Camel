// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/data/null.h"

using namespace camel::core::type;

namespace camel::core::data {

NullData::NullData(type::Type *type) : Data(type != nullptr ? type : Type::Void()) {}

bool NullData::equals(const data_ptr_t &other) const { return true; }

data_ptr_t NullData::clone(bool deep) const { return Data::null(type_); }

const std::string NullData::toString() const { return "null"; }

data_ptr_t NullData::convertTo(type::Type *type) {
    if (type->equals(type_)) {
        return Data::null(type_);
    }
    return nullptr;
}

} // namespace camel::core::data
