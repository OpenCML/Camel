// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/data/base.h"
#include "camel/core/data/null.h"

using namespace std;
using namespace camel::core::type;

namespace camel::core::data {

Data::Data() {};

Data::Data(type::Type *type) : type_(type) {}

type::Type *Data::type() const { return type_; }

data_ptr_t Data::null() {
    static data_ptr_t nullData = nullptr;
    if (nullData == nullptr) {
        nullData = make_shared<NullData>();
    }
    return nullData;
}

data_ptr_t Data::null(type::Type *type) {
    // Only a `T?` has a `null`: a type with no nullable tag has no null value, and asking for one
    // is a value of no nullable type in particular rather than a null of that type.
    if (type == nullptr || type->code() == TypeCode::Void || asTagged(type) == nullptr ||
        !asTagged(type)->isNullable()) {
        return null();
    }
    return make_shared<NullData>(type);
}

} // namespace camel::core::data
