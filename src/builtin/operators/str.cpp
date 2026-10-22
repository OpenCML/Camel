// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "str.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/complex.h"
#include "camel/core/type/composite/array.h"
#include "camel/utils/type.h"

#include "complex_format.h"
#include "float_format.h"
#include "fmt/args.h"
#include "fmt/core.h"
#include "text_format.h"

#include <sstream>

namespace mm = camel::core::mm;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::type;
using namespace camel::core::rtdata;

std::optional<std::string> checkFormatPattern(
    std::string_view pattern, std::span<camel::core::type::Type *const> argumentTypes) {
    fmt::dynamic_format_arg_store<fmt::format_context> store;
    for (const camel::core::type::Type *type : argumentTypes) {
        switch (type->code()) {
        case TypeCode::Int32:
            store.push_back(Int32{0});
            break;
        case TypeCode::Int64:
            store.push_back(Int64{0});
            break;
        case TypeCode::Float32:
            store.push_back(camel::builtin::LanguageFloat<Float32>{0});
            break;
        case TypeCode::Float64:
            store.push_back(camel::builtin::LanguageFloat<Float64>{0});
            break;
        case TypeCode::Complex64:
            store.push_back(camel::builtin::LanguageComplex<Float32>{0, 0});
            break;
        case TypeCode::Complex128:
            store.push_back(camel::builtin::LanguageComplex<Float64>{0, 0});
            break;
        default:
            store.push_back(camel::builtin::CodePointText{""});
            break;
        }
    }
    try {
        (void)fmt::vformat(std::string(pattern), store);
    } catch (const fmt::format_error &e) {
        return std::string(e.what());
    }
    return std::nullopt;
}

slot_t __format__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *fmtStrObj = norm.get<String *>(0);

    std::string fmtStr = fmtStrObj->toString();

    fmt::dynamic_format_arg_store<fmt::format_context> store;

    for (size_t i = 0; i < with.size(); ++i) {
        TypeCode type = with.code(i);

        switch (type) {
        case TypeCode::Int32:
            store.push_back(with.get<Int32>(i));
            break;
        case TypeCode::Int64:
            store.push_back(with.get<Int64>(i));
            break;
        case TypeCode::Float32:
            store.push_back(camel::builtin::LanguageFloat<Float32>{with.get<Float32>(i)});
            break;
        case TypeCode::Float64:
            store.push_back(camel::builtin::LanguageFloat<Float64>{with.get<Float64>(i)});
            break;
        case TypeCode::Complex64: {
            const Complex *value = with.get<Complex *>(i);
            store.push_back(camel::builtin::LanguageComplex<Float32>{
                value != nullptr ? value->realF() : Float32{0},
                value != nullptr ? value->imaginaryF() : Float32{0}});
            break;
        }
        case TypeCode::Complex128: {
            const Complex *value = with.get<Complex *>(i);
            store.push_back(camel::builtin::LanguageComplex<Float64>{
                value != nullptr ? value->real() : Float64{0},
                value != nullptr ? value->imaginary() : Float64{0}});
            break;
        }
        default:
            // fallback to string
            std::ostringstream oss;
            printSlot(oss, with.slot(i), with.type(i));
            store.push_back(camel::builtin::CodePointText{oss.str()});
            break;
        }
    }

    try {
        std::string resultStr = fmt::vformat(fmtStr, store);
        String *resultObj     = String::from(resultStr, mm::autoSpace());
        return rtdata::toSlot(resultObj);
    } catch (const fmt::format_error &e) {
        throwRuntimeFault(RuntimeDiag::FormatFailed, std::string(fmtStr), std::string(e.what()));
    }
}

slot_t __join__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *sepObj = with.get<String *>(0);
    if (!sepObj) {
        throwRuntimeFault(RuntimeDiag::InvalidArgument, "join", "the separator is null");
    }
    std::string separator = sepObj->toString();

    Array *arrObj = norm.get<Array *>(0);
    const camel::core::type::ArrayType *arrType =
        tt::as_ptr<camel::core::type::ArrayType>(norm.type(0));
    camel::core::type::Type *elemType = arrType->elemType();

    std::ostringstream joined;
    size_t len = arrObj->size();

    for (size_t i = 0; i < len; ++i) {
        if (i > 0)
            joined << separator;
        slot_t slot = arrObj->get<slot_t>(i);
        printSlot(joined, slot, elemType);
    }

    String *resultObj = String::from(joined.str(), mm::autoSpace());
    return rtdata::toSlot(resultObj);
}
