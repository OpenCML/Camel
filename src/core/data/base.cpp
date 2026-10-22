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

} // namespace camel::core::data
