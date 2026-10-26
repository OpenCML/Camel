// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "stream.h"
#include "camel/core/context/context.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/type/base.h"
#include "camel/core/type/composite/stream.h"
#include "camel/profile/profile.h"
#include "camel/utils/type.h"

#include <cstdint>
#include <string>

using namespace camel::core::context;
using namespace camel::core::rtdata;
using namespace camel::core::type;

namespace {

/// What one element takes in bytes, 0 when its values are not a fixed number of them (a string, an
/// array, a tuple, a stream): the report counts elements, and this is what turns a count into
/// bytes where there is one answer.
uint64_t bytesOfElement(Type *element) {
    if (element == nullptr) {
        return 0;
    }
    switch (element->code()) {
        case TypeCode::Bool:
            return 1;
        case TypeCode::Float16:
        case TypeCode::BFloat16:
            return 2;
        case TypeCode::Int32:
        case TypeCode::Float32:
            return 4;
        case TypeCode::Int64:
        case TypeCode::Float64:
        case TypeCode::Complex64:
            return 8;
        case TypeCode::Complex128:
            return 16;
        default:
            return 0;
    }
}

} // namespace

slot_t __stream_tee_note__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    // A tee is pulled in the program's own loop, once per element, so the hook is behind the one
    // flag every other recorded event is behind: a run that does not ask for the report pays one
    // load and returns, and records nothing for the report to print.
    if (!camel::profile::enabledInline()) {
        return NullSlot;
    }
    camel::profile::StreamTee tee;
    tee.hint     = static_cast<uint64_t>(norm.get<Int64>(0));
    tee.cap      = static_cast<uint64_t>(norm.get<Int64>(1));
    tee.elements = static_cast<uint64_t>(norm.get<Int64>(2));
    tee.peak     = static_cast<uint64_t>(norm.get<Int64>(3));
    // The last parameter is the stream the tee reads: its value is what tells this tee from
    // another, and the same value arrives on every pull, so it is the record's key. Only its type
    // is read besides, and a stream value is a function value, so reading its type does not pull
    // it.
    if (Type *stream = norm.type(4); stream != nullptr && stream->code() == TypeCode::Stream) {
        Type *element = tt::as_ptr<StreamType>(stream)->elemType();
        tee.elem      = element != nullptr ? element->toString() : std::string();
        tee.elemBytes = bytesOfElement(element);
    }
    camel::profile::streamTee(reinterpret_cast<const void *>(norm.slot(4)), tee);
    return NullSlot;
}