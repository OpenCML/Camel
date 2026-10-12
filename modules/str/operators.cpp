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
 * Updated: Oct. 12, 2026
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

// ---- UTF-8 ----
//
// Strict decoding: an overlong form, a surrogate (U+D800..U+DFFF), a value above U+10FFFF, a
// stray continuation byte and a sequence cut short are all invalid.

/// Decodes the code point that starts at `pos` and advances `pos` past it; nullopt when the
/// bytes there are not valid UTF-8 (`pos` is then left at the offending sequence).
std::optional<uint32_t> decodeOne(std::string_view s, size_t &pos) {
    const auto byte  = [&](size_t i) { return static_cast<uint8_t>(s[i]); };
    const uint8_t b0 = byte(pos);
    if (b0 < 0x80) {
        ++pos;
        return b0;
    }
    size_t length  = 0;
    uint32_t value = 0;
    uint32_t least = 0;
    if ((b0 & 0xE0) == 0xC0) {
        length = 2, value = b0 & 0x1F, least = 0x80;
    } else if ((b0 & 0xF0) == 0xE0) {
        length = 3, value = b0 & 0x0F, least = 0x800;
    } else if ((b0 & 0xF8) == 0xF0) {
        length = 4, value = b0 & 0x07, least = 0x10000;
    } else {
        return std::nullopt;
    }
    if (pos + length > s.size())
        return std::nullopt;
    for (size_t k = 1; k < length; ++k) {
        const uint8_t b = byte(pos + k);
        if ((b & 0xC0) != 0x80)
            return std::nullopt;
        value = (value << 6) | (b & 0x3F);
    }
    if (value < least || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF))
        return std::nullopt;
    pos += length;
    return value;
}

bool isScalarValue(Int64 cp) {
    return cp >= 0 && cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF);
}

void encodeOne(uint32_t cp, std::string &out) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

/// The byte offset at which each code point of `s` starts, followed by s.size(); nullopt and
/// the offset of the first invalid byte when `s` is not valid UTF-8.
std::optional<std::vector<size_t>> charStarts(std::string_view s, size_t &badAt) {
    std::vector<size_t> starts;
    starts.reserve(s.size() + 1);
    size_t pos = 0;
    while (pos < s.size()) {
        starts.push_back(pos);
        if (!decodeOne(s, pos)) {
            badAt = pos;
            return std::nullopt;
        }
    }
    starts.push_back(s.size());
    return starts;
}

slot_t invalidUtf8(const char *op, size_t badAt) {
    return makeError(
        "EncodingError",
        std::format("the string is not valid UTF-8 (byte {})", badAt),
        std::format("<str::{}>", op));
}

// The text of a string argument, copied: an operator allocates its result, and an allocation
// may move the argument.
std::string textOf(ArgsView &args, size_t index) {
    return std::string(args.get<String *>(index)->view());
}

} // namespace

// ---------------------------------------------------------------------------------------------
// By byte
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
    return makeSome(toSlot(static_cast<Int64>(at)), resultType);
}

// DESIGN-QUESTION(slice-bounds): a range that does not lie inside the string is a fault, as an
// index out of range is. The built-in `slice` on arrays clamps instead; which of the two a
// range operation does is not ruled on.
slot_t __str_substr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const std::string s = textOf(norm, 0);
    const Int64 start   = with.get<Int64>(0);
    const Int64 length  = with.get<Int64>(1);
    const Int64 size    = static_cast<Int64>(s.size());
    if (start < 0 || start > size) {
        throwRuntimeFault(RuntimeDiag::IndexOutOfRange, start, "a string", s.size());
    }
    if (length < 0) {
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument,
            "str.substr",
            std::format("the length is negative ({})", length));
    }
    if (length > size - start) {
        throwRuntimeFault(RuntimeDiag::IndexOutOfRange, start + length, "a string", s.size());
    }
    return makeString(s.substr(static_cast<size_t>(start), static_cast<size_t>(length)));
}

slot_t __str_starts_with__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot(
        static_cast<Bool>(norm.get<String *>(0)->view().starts_with(with.get<String *>(0)->view())));
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
    return makeString(out);
}

// ---------------------------------------------------------------------------------------------
// By code point. A string is bytes and need not be valid UTF-8, so an operator that decodes
// can fail: it returns `T!` with an `EncodingError`. An index out of range is a fault, as
// everywhere.
// ---------------------------------------------------------------------------------------------

slot_t __str_is_valid_utf8__(ArgsView &with, ArgsView &norm, Context &ctx) {
    size_t badAt = 0;
    return toSlot(static_cast<Bool>(charStarts(norm.get<String *>(0)->view(), badAt).has_value()));
}

slot_t __str_codepoints__(ArgsView &with, ArgsView &norm, Context &ctx) {
    static const TupleType *resultType = TupleType::fallible(ArrayType::create(Type::Int64()));
    const std::string text             = textOf(norm, 0);
    std::vector<Int64> values;
    values.reserve(text.size());
    size_t pos = 0;
    while (pos < text.size()) {
        const auto cp = decodeOne(text, pos);
        if (!cp)
            return invalidUtf8("codepoints", pos);
        values.push_back(*cp);
    }
    return makeOk(makeIntArray(values), resultType);
}

slot_t __str_from_codepoints__(ArgsView &with, ArgsView &norm, Context &ctx) {
    static const TupleType *resultType = TupleType::fallible(Type::String());
    Array *arr                         = norm.get<Array *>(0);
    std::string out;
    out.reserve(arr->size());
    for (size_t i = 0; i < arr->size(); ++i) {
        const Int64 cp = arr->get<Int64>(i);
        if (!isScalarValue(cp)) {
            return makeError(
                "EncodingError",
                std::format("element {} is {}, not a Unicode code point", i, cp),
                "<str::from_codepoints>");
        }
        encodeOne(static_cast<uint32_t>(cp), out);
    }
    if (out.size() > kMaxStringBytes) {
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument,
            "str.from_codepoints",
            "the result is longer than a string can be");
    }
    return makeOk(makeString(out), resultType);
}

slot_t __str_char_count__(ArgsView &with, ArgsView &norm, Context &ctx) {
    static const TupleType *resultType = TupleType::fallible(Type::Int64());
    size_t badAt                       = 0;
    const auto starts                  = charStarts(norm.get<String *>(0)->view(), badAt);
    if (!starts)
        return invalidUtf8("char_count", badAt);
    return makeOk(toSlot(static_cast<Int64>(starts->size() - 1)), resultType);
}

slot_t __str_char_at__(ArgsView &with, ArgsView &norm, Context &ctx) {
    static const TupleType *resultType = TupleType::fallible(Type::String());
    const std::string text             = textOf(norm, 0);
    const Int64 index                  = with.get<Int64>(0);
    size_t badAt                       = 0;
    const auto starts                  = charStarts(text, badAt);
    if (!starts)
        return invalidUtf8("char_at", badAt);
    const size_t count = starts->size() - 1;
    if (index < 0 || static_cast<size_t>(index) >= count) {
        throwRuntimeFault(RuntimeDiag::IndexOutOfRange, index, "a string of characters", count);
    }
    const size_t from = (*starts)[index], to = (*starts)[index + 1];
    return makeOk(makeString(text.substr(from, to - from)), resultType);
}

// DESIGN-QUESTION(slice-bounds): as for substr, a range outside the string is a fault.
slot_t __str_char_substr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    static const TupleType *resultType = TupleType::fallible(Type::String());
    const std::string text             = textOf(norm, 0);
    const Int64 start                  = with.get<Int64>(0);
    const Int64 n                      = with.get<Int64>(1);
    size_t badAt                       = 0;
    const auto starts                  = charStarts(text, badAt);
    if (!starts)
        return invalidUtf8("char_substr", badAt);
    const Int64 count = static_cast<Int64>(starts->size() - 1);
    if (start < 0 || start > count) {
        throwRuntimeFault(
            RuntimeDiag::IndexOutOfRange,
            start,
            "a string of characters",
            static_cast<size_t>(count));
    }
    if (n < 0) {
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument,
            "str.char_substr",
            std::format("the count is negative ({})", n));
    }
    if (n > count - start) {
        throwRuntimeFault(
            RuntimeDiag::IndexOutOfRange,
            start + n,
            "a string of characters",
            static_cast<size_t>(count));
    }
    const size_t from = (*starts)[start], to = (*starts)[start + n];
    return makeOk(makeString(text.substr(from, to - from)), resultType);
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
        {"is_valid_utf8", __str_is_valid_utf8__},
        {"codepoints", __str_codepoints__},
        {"from_codepoints", __str_from_codepoints__},
        {"char_count", __str_char_count__},
        {"char_at", __str_char_at__},
        {"char_substr", __str_char_substr__},
    };
}
