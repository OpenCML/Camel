// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "operators.h"
#include "case_tables.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/utils/type.h"
#include "camel/utils/utf8.h"

#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mm = camel::core::mm;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::type;
using namespace camel::core::rtdata;

namespace {

// A String holds its length in 32 bits.
constexpr size_t kMaxStringBytes = std::numeric_limits<uint32_t>::max();

slot_t makeString(const std::string &text) { return toSlot(String::from(text, mm::autoSpace())); }

slot_t makeIntArray(const std::vector<Int64> &values) {
    Array *res = Array::create(mm::autoSpace(), values.size());
    for (size_t i = 0; i < values.size(); ++i)
        res->set<Int64>(i, values[i]);
    return toSlot(res);
}

// The text of a string argument, copied: an operator allocates its result, and an allocation
// may move the argument.
std::string textOf(ArgsView &args, size_t index) {
    return std::string(args.get<String *>(index)->view());
}

} // namespace

// ---------------------------------------------------------------------------------------------
// A string is valid UTF-8 and its indices and lengths count code points (design principles
// 11.1). Text that is only ASCII, the common case, is indexed directly.
// ---------------------------------------------------------------------------------------------

slot_t __str_split__(ArgsView &with, ArgsView &norm, Context &ctx) {
    static const ArrayType *resultType = ArrayType::create(Type::String());
    const std::string s                = textOf(norm, 0);
    const std::string sep              = textOf(with, 0);
    std::vector<std::string> parts;
    if (sep.empty()) {
        // An empty separator cuts the string into its code points (stdlib/str.cml `split`).
        for (size_t at = 0; at < s.size();) {
            const size_t len = camel::utf8::sequenceLength(static_cast<unsigned char>(s[at]));
            parts.push_back(s.substr(at, len));
            at += len;
        }
        Array *res = Array::create(mm::autoSpace(), parts.size());
        for (size_t i = 0; i < parts.size(); ++i)
            res->set<slot_t>(i, makeString(parts[i]), resultType);
        return toSlot(res);
    }
    size_t from = 0;
    while (true) {
        const size_t at = s.find(sep, from);
        if (at == std::string::npos) {
            parts.push_back(s.substr(from));
            break;
        }
        parts.push_back(s.substr(from, at - from));
        from = at + sep.size();
    }
    Array *res = Array::create(mm::autoSpace(), parts.size());
    for (size_t i = 0; i < parts.size(); ++i)
        res->set<slot_t>(i, makeString(parts[i]), resultType);
    return toSlot(res);
}

slot_t __str_to_upper__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return makeString(camel::str::toUpperCase(norm.get<String *>(0)->view()));
}

slot_t __str_to_lower__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return makeString(camel::str::toLowerCase(norm.get<String *>(0)->view()));
}

slot_t __str_find__(ArgsView &with, ArgsView &norm, Context &ctx) {
    static const TupleType *resultType = TupleType::nullable(Type::Int64());
    const std::string_view s           = norm.get<String *>(0)->view();
    const std::string_view sub         = with.get<String *>(0)->view();
    const size_t at                    = s.find(sub);
    if (at == std::string_view::npos)
        return NullSlot;
    // A valid needle in a valid text starts on a character boundary; the result counts
    // characters.
    const size_t index = camel::utf8::indexOf(s, at);
    return makeSome(toSlot(static_cast<Int64>(index)), resultType);
}

// ---------------------------------------------------------------------------------------------
// Code points as numbers.
// ---------------------------------------------------------------------------------------------

slot_t __str_codepoints__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const std::string text = textOf(norm, 0);
    std::vector<Int64> values;
    values.reserve(text.size());
    size_t pos = 0;
    while (pos < text.size())
        values.push_back(camel::utf8::decodeOne(text, pos));
    return makeIntArray(values);
}

slot_t __str_from_codepoints__(ArgsView &with, ArgsView &norm, Context &ctx) {
    static const TupleType *resultType = TupleType::fallible(Type::String());
    Array *arr                         = norm.get<Array *>(0);
    std::string out;
    out.reserve(arr->size());
    for (size_t i = 0; i < arr->size(); ++i) {
        const Int64 cp = arr->get<Int64>(i);
        if (!camel::utf8::isScalarValue(cp)) {
            return makeError(
                "EncodingError",
                std::format("element {} is {}, not a Unicode code point", i, cp),
                "<str::from_codepoints>");
        }
        camel::utf8::encodeOne(static_cast<uint32_t>(cp), out);
    }
    if (out.size() > kMaxStringBytes) {
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument,
            "str.from_codepoints",
            "the result is longer than a string can be");
    }
    return makeOk(makeString(out), resultType);
}

// The kernel of `repeat` (stdlib/str.cml): the same string as the Camel body, built at once. A
// result longer than a string can hold is a fault here, before anything is allocated.
slot_t __str_repeat__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const std::string s = textOf(norm, 0);
    const Int64 count   = with.get<Int64>(0);
    if (count <= 0 || s.empty()) {
        return makeString("");
    }
    if (static_cast<uint64_t>(count) > kMaxStringBytes / s.size()) {
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument,
            "str.repeat",
            std::format("{} times {} bytes is longer than a string can be", count, s.size()));
    }
    std::string out;
    out.reserve(s.size() * static_cast<size_t>(count));
    for (Int64 i = 0; i < count; ++i)
        out.append(s);
    return makeString(out);
}

std::unordered_map<std::string, operator_t> getStrOpsMap() {
    return {
        {"split", __str_split__},
        {"to_upper", __str_to_upper__},
        {"to_lower", __str_to_lower__},
        {"find", __str_find__},
        {"repeat", __str_repeat__},
        {"codepoints", __str_codepoints__},
        {"from_codepoints", __str_from_codepoints__},
    };
}
