// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "cast.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/numeric_convert.h"
#include "camel/core/rtdata/complex.h"

#include <sstream>

namespace mm = camel::core::mm;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::rtdata;

slot_t __itoi__(ArgsView &with, ArgsView &norm, Context &ctx) { return norm.slot(0); }

slot_t __ltoi__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int64 v = norm.get<Int64>(0);
    return toSlot(camel::core::checkedNarrow<Int32>(v, "i32"));
}

slot_t __ftoi__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Float32 v = norm.get<Float32>(0);
    return toSlot(camel::core::checkedTruncate<Int32>(v, "i32"));
}

slot_t __dtoi__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Float64 v = norm.get<Float64>(0);
    return toSlot(camel::core::checkedTruncate<Int32>(v, "i32"));
}

slot_t __stoi__(ArgsView &with, ArgsView &norm, Context &ctx) {
    // The whole string must be one number (design principles 5.1).
    String *s = norm.get<String *>(0);
    return toSlot(camel::core::integerFromText<Int32>(s->view(), "i32"));
}

slot_t __itol__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int32 v = norm.get<Int32>(0);
    return toSlot(static_cast<Int64>(v));
}

slot_t __ltol__(ArgsView &with, ArgsView &norm, Context &ctx) { return norm.slot(0); }

slot_t __ftol__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Float32 v = norm.get<Float32>(0);
    return toSlot(camel::core::checkedTruncate<Int64>(v, "int"));
}

slot_t __dtol__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Float64 v = norm.get<Float64>(0);
    return toSlot(camel::core::checkedTruncate<Int64>(v, "int"));
}

slot_t __stol__(ArgsView &with, ArgsView &norm, Context &ctx) {
    // The whole string must be one number (design principles 5.1).
    String *s = norm.get<String *>(0);
    return toSlot(camel::core::integerFromText<Int64>(s->view(), "int"));
}

slot_t __itof__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int32 v = norm.get<Int32>(0);
    return toSlot(static_cast<Float32>(v));
}

slot_t __ltof__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int64 v = norm.get<Int64>(0);
    return toSlot(static_cast<Float32>(v));
}

slot_t __ftof__(ArgsView &with, ArgsView &norm, Context &ctx) { return norm.slot(0); }

slot_t __dtof__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Float64 v = norm.get<Float64>(0);
    return toSlot(static_cast<Float32>(v));
}

slot_t __stof__(ArgsView &with, ArgsView &norm, Context &ctx) {
    // The whole string must be one number (design principles 5.1).
    String *s = norm.get<String *>(0);
    return toSlot(camel::core::floatFromText<Float32>(s->view(), "f32"));
}

slot_t __itod__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int32 v = norm.get<Int32>(0);
    return toSlot(static_cast<Float64>(v));
}

slot_t __ltod__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int64 v = norm.get<Int64>(0);
    return toSlot(static_cast<Float64>(v));
}

slot_t __ftod__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Float32 v = norm.get<Float32>(0);
    return toSlot(static_cast<Float64>(v));
}

slot_t __dtod__(ArgsView &with, ArgsView &norm, Context &ctx) { return norm.slot(0); }

slot_t __stod__(ArgsView &with, ArgsView &norm, Context &ctx) {
    // The whole string must be one number (design principles 5.1).
    String *s = norm.get<String *>(0);
    return toSlot(camel::core::floatFromText<Float64>(s->view(), "float"));
}

slot_t __itos__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int32 v   = norm.get<Int32>(0);
    String *s = String::from(std::to_string(v), mm::autoSpace());
    return toSlot(s);
}

slot_t __ltos__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int64 v   = norm.get<Int64>(0);
    String *s = String::from(std::to_string(v), mm::autoSpace());
    return toSlot(s);
}

slot_t __ftos__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Float32 v = norm.get<Float32>(0);
    String *s = String::from(camel::utils::floatText(v), mm::autoSpace());
    return toSlot(s);
}

slot_t __dtos__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Float64 v = norm.get<Float64>(0);
    String *s = String::from(camel::utils::floatText(v), mm::autoSpace());
    return toSlot(s);
}

slot_t __stos__(ArgsView &with, ArgsView &norm, Context &ctx) { return norm.slot(0); }

// `string(x)` for a value of any type the overloads above do not take (bool, array, tuple,
// struct, function, ...): the text `println` writes for it. The slot used to be read as a
// string whatever its type, so `string(true)` dereferenced the bool.
slot_t __atos__(ArgsView &with, ArgsView &norm, Context &ctx) {
    std::ostringstream oss;
    printSlot(oss, norm.slot(0), norm.type(0));
    String *res = String::from(oss.str(), mm::autoSpace());
    return toSlot(res);
}

// ---- The complex conversions: c64(x), c128(x) ---------------------------------------------
//
// Every real type widens to a complex number (the imaginary part is zero), c64 widens to c128
// exactly, and c128 narrows to c64 by rounding each part to f32 (the same rule as f32 from a
// double: a part too large for an f32 becomes an infinity). A complex value does not become a
// real one: `re(z)` and `im(z)` are the explicit operations.

namespace {

template <typename T>
slot_t realToComplex(ArgsView &norm, camel::core::type::TypeCode code) {
    const T value = norm.get<T>(0);
    return toSlot(Complex::create(static_cast<double>(value), 0.0, code, mm::autoSpace()));
}

} // namespace

slot_t __itoc__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return realToComplex<Int32>(norm, camel::core::type::TypeCode::Complex64);
}
slot_t __ltoc__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return realToComplex<Int64>(norm, camel::core::type::TypeCode::Complex64);
}
slot_t __ftoc__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return realToComplex<Float32>(norm, camel::core::type::TypeCode::Complex64);
}
slot_t __dtoc__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return realToComplex<Float64>(norm, camel::core::type::TypeCode::Complex64);
}
slot_t __ctoc__(ArgsView &with, ArgsView &norm, Context &ctx) { return norm.slot(0); }

slot_t __ztoc__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const Complex *value = norm.get<Complex *>(0);
    return toSlot(Complex::create(
        value->real(), value->imaginary(), camel::core::type::TypeCode::Complex64, mm::autoSpace()));
}

slot_t __itoz__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return realToComplex<Int32>(norm, camel::core::type::TypeCode::Complex128);
}
slot_t __ltoz__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return realToComplex<Int64>(norm, camel::core::type::TypeCode::Complex128);
}
slot_t __ftoz__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return realToComplex<Float32>(norm, camel::core::type::TypeCode::Complex128);
}
slot_t __dtoz__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return realToComplex<Float64>(norm, camel::core::type::TypeCode::Complex128);
}
slot_t __ctoz__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const Complex *value = norm.get<Complex *>(0);
    return toSlot(Complex::create(
        value->real(), value->imaginary(), camel::core::type::TypeCode::Complex128, mm::autoSpace()));
}
slot_t __ztoz__(ArgsView &with, ArgsView &norm, Context &ctx) { return norm.slot(0); }
