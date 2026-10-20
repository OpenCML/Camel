/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Oct. 12, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "operators.h"
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

// ASCII only: a byte above 0x7F is part of a multi-byte character and is left alone.
bool isAsciiSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

std::string_view trimStart(std::string_view s) {
    size_t i = 0;
    while (i < s.size() && isAsciiSpace(s[i]))
        ++i;
    return s.substr(i);
}

std::string_view trimEnd(std::string_view s) {
    size_t n = s.size();
    while (n > 0 && isAsciiSpace(s[n - 1]))
        --n;
    return s.substr(0, n);
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
    if (sep.empty()) {
        throwRuntimeFault(RuntimeDiag::InvalidArgument, "str.split", "the separator is empty");
    }
    std::vector<std::string> parts;
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

slot_t __str_trim__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const std::string s = textOf(norm, 0);
    return makeString(std::string(trimEnd(trimStart(s))));
}

slot_t __str_trim_start__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const std::string s = textOf(norm, 0);
    return makeString(std::string(trimStart(s)));
}

slot_t __str_trim_end__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const std::string s = textOf(norm, 0);
    return makeString(std::string(trimEnd(s)));
}

slot_t __str_to_upper__(ArgsView &with, ArgsView &norm, Context &ctx) {
    std::string text = textOf(norm, 0);
    for (char &c : text) {
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    }
    return makeString(text);
}

slot_t __str_to_lower__(ArgsView &with, ArgsView &norm, Context &ctx) {
    std::string text = textOf(norm, 0);
    for (char &c : text) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return makeString(text);
}

slot_t __str_replace__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const std::string s    = textOf(norm, 0);
    const std::string from = textOf(with, 0);
    const std::string to   = textOf(with, 1);
    if (from.empty()) {
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument,
            "str.replace",
            "the text to replace is empty");
    }
    std::string out;
    out.reserve(s.size());
    size_t pos = 0;
    while (true) {
        const size_t at = s.find(from, pos);
        if (at == std::string::npos) {
            out.append(s, pos, std::string::npos);
            break;
        }
        out.append(s, pos, at - pos);
        out.append(to);
        pos = at + from.size();
        if (out.size() > kMaxStringBytes) {
            throwRuntimeFault(
                RuntimeDiag::InvalidArgument,
                "str.replace",
                "the result is longer than a string can be");
        }
    }
    return makeString(out);
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

// A range that does not lie inside the string is a fault, as an index out of range is and as
// the built-in `slice` on arrays does (design principles 3.9).
slot_t __str_substr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const std::string s = textOf(norm, 0);
    const Int64 start   = with.get<Int64>(0);
    const Int64 length  = with.get<Int64>(1);
    const bool ascii    = camel::utf8::isAscii(s);
    const Int64 size    = static_cast<Int64>(ascii ? s.size() : camel::utf8::count(s));
    if (start < 0 || start > size) {
        throwRuntimeFault(RuntimeDiag::IndexOutOfRange, start, "a string", size);
    }
    if (length < 0) {
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument,
            "str.substr",
            std::format("the length is negative ({})", length));
    }
    if (length > size - start) {
        throwRuntimeFault(RuntimeDiag::IndexOutOfRange, start + length, "a string", size);
    }
    if (ascii) {
        return makeString(s.substr(static_cast<size_t>(start), static_cast<size_t>(length)));
    }
    const size_t from = camel::utf8::offsetOf(s, static_cast<size_t>(start));
    const size_t to =
        from + camel::utf8::offsetOf(std::string_view(s).substr(from), static_cast<size_t>(length));
    return makeString(s.substr(from, to - from));
}

slot_t __str_starts_with__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot(
        static_cast<Bool>(
            norm.get<String *>(0)->view().starts_with(with.get<String *>(0)->view())));
}

slot_t __str_ends_with__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot(
        static_cast<Bool>(norm.get<String *>(0)->view().ends_with(with.get<String *>(0)->view())));
}

slot_t __str_repeat__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const std::string s = textOf(norm, 0);
    const Int64 count   = with.get<Int64>(0);
    if (count < 0) {
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument,
            "str.repeat",
            std::format("the count is negative ({})", count));
    }
    if (!s.empty() && static_cast<uint64_t>(count) > kMaxStringBytes / s.size()) {
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

slot_t __str_bytes__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const std::string s = textOf(norm, 0);
    std::vector<Int64> values(s.size());
    for (size_t i = 0; i < s.size(); ++i)
        values[i] = static_cast<uint8_t>(s[i]);
    return makeIntArray(values);
}

slot_t __str_from_bytes__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *arr = norm.get<Array *>(0);
    std::string out(arr->size(), '\0');
    for (size_t i = 0; i < arr->size(); ++i) {
        const Int64 value = arr->get<Int64>(i);
        if (value < 0 || value > 255) {
            throwRuntimeFault(
                RuntimeDiag::InvalidArgument,
                "str.from_bytes",
                std::format("element {} is {}, not a byte (0 to 255)", i, value));
        }
        out[i] = static_cast<char>(static_cast<uint8_t>(value));
    }
    // A string is always valid UTF-8 (design principles 11.1): the bytes must be.
    if (const size_t bad = camel::utf8::invalidOffset(out); bad != camel::utf8::npos) {
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument,
            "str.from_bytes",
            std::format("the bytes are not valid UTF-8 (byte {} starts an invalid sequence)", bad));
    }
    return makeString(out);
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

std::unordered_map<std::string, operator_t> getStrOpsMap() {
    return {
        {"split", __str_split__},
        {"trim", __str_trim__},
        {"trim_start", __str_trim_start__},
        {"trim_end", __str_trim_end__},
        {"to_upper", __str_to_upper__},
        {"to_lower", __str_to_lower__},
        {"replace", __str_replace__},
        {"find", __str_find__},
        {"substr", __str_substr__},
        {"starts_with", __str_starts_with__},
        {"ends_with", __str_ends_with__},
        {"repeat", __str_repeat__},
        {"bytes", __str_bytes__},
        {"from_bytes", __str_from_bytes__},
        {"codepoints", __str_codepoints__},
        {"from_codepoints", __str_from_codepoints__},
    };
}
