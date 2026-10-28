// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "array_convert.h"

#include "camel/core/array_convert.h"
#include "camel/core/context/context.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/resolver.h"
#include "camel/utils/type.h"

#include <format>

using namespace camel::core::type;
using namespace camel::core::rtdata;
using camel::core::context::Context;

namespace {

template <TypeCode Target> slot_t convert(ArgsView &, ArgsView &norm, Context &) {
    const Array *input = norm.get<Array *>(0);
    return toSlot(camel::core::convertArrayElements(input, Target, camel::core::mm::autoSpace()));
}

} // namespace

slot_t __atoi__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return convert<TypeCode::Int32>(with, norm, ctx);
}
slot_t __atol__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return convert<TypeCode::Int64>(with, norm, ctx);
}
slot_t __atof__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return convert<TypeCode::Float32>(with, norm, ctx);
}
slot_t __atod__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return convert<TypeCode::Float64>(with, norm, ctx);
}
slot_t __atobf__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return convert<TypeCode::BFloat16>(with, norm, ctx);
}
slot_t __atof6__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return convert<TypeCode::Float16>(with, norm, ctx);
}

const char *arrayConversionUri(TypeCode target) {
    switch (target) {
    case TypeCode::Int32:
        return ":op/atoi";
    case TypeCode::Int64:
        return ":op/atol";
    case TypeCode::Float32:
        return ":op/atof";
    case TypeCode::Float64:
        return ":op/atod";
    case TypeCode::BFloat16:
        return ":op/atobf";
    case TypeCode::Float16:
        return ":op/atof6";
    default:
        return "";
    }
}
