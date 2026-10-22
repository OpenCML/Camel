// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "key.h"

#include "camel/core/type/composite/struct.h"

namespace camel::random {

camel::core::type::Type *keyType() {
    static camel::core::type::Type *keyStructType = [] {
        camel::core::type::StructTypeFactory factory;
        factory.add("hi", camel::core::type::Type::Int64());
        factory.add("lo", camel::core::type::Type::Int64());
        return factory.build();
    }();
    return keyStructType;
}

} // namespace camel::random