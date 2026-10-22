// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/data/primary.h"
#include "camel/utils/escape.h"

using namespace camel::core::type;
namespace camel::core::data {

StringData::StringData(const std::string &data) : Data(type::Type::String()), data_(data) {}

const std::string &StringData::data() const { return data_; }

bool StringData::equals(const data_ptr_t &other) const {
    if (auto o = std::dynamic_pointer_cast<StringData>(other)) {
        return data_ == o->data_;
    }
    return false;
}

data_ptr_t StringData::clone(bool deep) const { return std::make_shared<StringData>(data_); }

const std::string StringData::toString() const { return "\"" + encodeEscapes(data_) + "\""; }

data_ptr_t StringData::convertTo(type::Type *type) {
    if (type->equals(type_)) {
        return shared_from_this();
    }
    return nullptr;
}

} // namespace camel::core::data
