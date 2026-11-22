// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "operators.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"

#include "camel/utils/utf8.h"
#include <memory>
#include <mutex>
#include <regex>
#include <unordered_map>
#include <vector>
#include <string>

using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::rtdata;
using namespace camel::core::type;
namespace mm = camel::core::mm;

namespace {

// What is wrong with a pattern, in the same words on every platform: the text of
// std::regex_error::what() is the standard library's (libstdc++, libc++ and the MSVC library
// each have their own), and the diagnostic is compared with recorded output.
std::string regexErrorReason(const std::regex_error &e) {
    namespace rc = std::regex_constants;
    switch (e.code()) {
    case rc::error_collate:
        return "Invalid collating element in regular expression";
    case rc::error_ctype:
        return "Invalid character class in regular expression";
    case rc::error_escape:
        return "Invalid escape in regular expression";
    case rc::error_backref:
        return "Invalid back reference in regular expression";
    case rc::error_brack:
        return "Mismatched '[' and ']' in regular expression";
    case rc::error_paren:
        return "Mismatched '(' and ')' in regular expression";
    case rc::error_brace:
        return "Mismatched '{' and '}' in regular expression";
    case rc::error_badbrace:
        return "Invalid range in '{}' in regular expression";
    case rc::error_range:
        return "Invalid character range in regular expression";
    case rc::error_space:
        return "Not enough memory to compile the regular expression";
    case rc::error_badrepeat:
        return "A repetition ('*', '?', '+', '{') with nothing to repeat in regular expression";
    case rc::error_complexity:
        return "The regular expression is too complex";
    case rc::error_stack:
        return "Not enough stack to match the regular expression";
    default:
        return "Invalid regular expression";
    }
}

// Text with characters above ASCII is matched as wide characters, so that `.` and a class
// stand for one code point and not for one byte (a string counts code points; design
// principles 11.1). Where wchar_t is 16 bits (Windows) a code point above U+FFFF is two units.
std::wstring widen(std::string_view text) {
    std::wstring out;
    size_t pos = 0;
    while (pos < text.size()) {
        const uint32_t cp = camel::utf8::decodeOne(text, pos);
        if constexpr (sizeof(wchar_t) == 2) {
            if (cp > 0xFFFF) {
                out.push_back(static_cast<wchar_t>(0xD800 + ((cp - 0x10000) >> 10)));
                out.push_back(static_cast<wchar_t>(0xDC00 + ((cp - 0x10000) & 0x3FF)));
                continue;
            }
        }
        out.push_back(static_cast<wchar_t>(cp));
    }
    return out;
}

// ---- the compiled patterns ----
//
// A pattern is compiled once per run: find_all, replace and split ask for the same pattern at
// every round. A compiled regex is read-only, so a match runs without the lock.

template <typename Regex> struct Compiled {
    std::shared_ptr<const Regex> regex;
    std::string error; // empty when the pattern compiled
};

constexpr size_t kCacheLimit = 256;

template <typename Regex> Compiled<Regex> compiled(std::string_view pattern) {
    static std::mutex mutex;
    static std::unordered_map<std::string, Compiled<Regex>> cache;
    std::lock_guard<std::mutex> lock(mutex);
    const std::string key(pattern);
    if (const auto found = cache.find(key); found != cache.end()) {
        return found->second;
    }
    if (cache.size() >= kCacheLimit) {
        cache.clear();
    }
    Compiled<Regex> entry;
    try {
        if constexpr (std::is_same_v<Regex, std::wregex>) {
            entry.regex = std::make_shared<const Regex>(widen(pattern));
        } else {
            entry.regex = std::make_shared<const Regex>(key);
        }
    } catch (const std::regex_error &e) {
        entry.error = regexErrorReason(e);
    }
    return cache.emplace(key, entry).first->second;
}

// ---- one search ----

// The spans of the first match at or after code point `start`: begin and end (in code points)
// of the whole match and of every group, (-1, -1) for a group that took no part. Empty when
// there is no match. A pattern that is not a regular expression is the fault RegexInvalid.
std::vector<int64_t>
search(std::string_view pattern, std::string_view text, size_t start, const char *who) {
    const auto flags =
        start > 0 ? std::regex_constants::match_prev_avail : std::regex_constants::match_default;
    const auto invalid = [&](const std::string &reason) {
        throwRuntimeFault(RuntimeDiag::RegexInvalid, who, std::string(pattern), reason);
    };
    std::vector<int64_t> spans;
    if (camel::utf8::isAscii(pattern) && camel::utf8::isAscii(text)) {
        const auto re = compiled<std::regex>(pattern);
        if (!re.regex) {
            invalid(re.error);
        }
        const char *begin = text.data();
        std::cmatch match;
        if (!std::regex_search(begin + start, begin + text.size(), match, *re.regex, flags)) {
            return spans;
        }
        for (const auto &group : match) {
            spans.push_back(group.matched ? group.first - begin : -1);
            spans.push_back(group.matched ? group.second - begin : -1);
        }
        return spans;
    }
    const auto re = compiled<std::wregex>(pattern);
    if (!re.regex) {
        invalid(re.error);
    }
    const std::wstring units = widen(text);
    // Where wchar_t has 16 bits a code point above U+FFFF is two units: map between the two.
    std::vector<size_t> unitOfCp;
    std::vector<size_t> cpOfUnit;
    if constexpr (sizeof(wchar_t) == 2) {
        size_t cp = 0;
        cpOfUnit.resize(units.size() + 1);
        for (size_t u = 0; u < units.size(); ++u) {
            const bool low = units[u] >= 0xDC00 && units[u] <= 0xDFFF;
            if (!low) {
                unitOfCp.push_back(u);
                ++cp;
            }
            cpOfUnit[u] = cp - 1;
        }
        unitOfCp.push_back(units.size());
        cpOfUnit[units.size()] = cp;
    }
    const auto toUnit = [&](size_t cp) {
        if constexpr (sizeof(wchar_t) == 2) {
            return unitOfCp[cp];
        } else {
            return cp;
        }
    };
    const auto toCp = [&](size_t unit) -> int64_t {
        if constexpr (sizeof(wchar_t) == 2) {
            return static_cast<int64_t>(cpOfUnit[unit]);
        } else {
            return static_cast<int64_t>(unit);
        }
    };
    const wchar_t *begin = units.c_str();
    std::wcmatch match;
    if (!std::regex_search(
            begin + toUnit(start), begin + units.size(), match, *re.regex, flags)) {
        return spans;
    }
    for (const auto &group : match) {
        spans.push_back(group.matched ? toCp(group.first - begin) : -1);
        spans.push_back(group.matched ? toCp(group.second - begin) : -1);
    }
    return spans;
}

// The text between two code point indices.
std::string cut(std::string_view text, int64_t from, int64_t to) {
    const size_t a = camel::utf8::offsetOf(text, static_cast<size_t>(from));
    const size_t b = camel::utf8::offsetOf(text, static_cast<size_t>(to));
    return std::string(text.substr(a, b - a));
}

slot_t stringSlot(const std::string &text) { return toSlot(String::from(text, mm::autoSpace())); }

} // namespace

slot_t __re_test__(ArgsView &with, ArgsView &norm, Context &) {
    const std::string_view pattern = with.get<String *>(0)->view();
    const std::string_view text    = norm.get<String *>(0)->view();
    return toSlot(!search(pattern, text, 0, "re.test").empty());
}

slot_t __re_find__(ArgsView &with, ArgsView &norm, Context &) {
    const std::string_view pattern = with.get<String *>(0)->view();
    const std::string_view text    = norm.get<String *>(0)->view();
    const int64_t length           = static_cast<int64_t>(camel::utf8::count(text));
    const int64_t start            = with.get<int64_t>(1);
    if (start < 0 || start > length) {
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument,
            "re.find",
            "start " + std::to_string(start) + " is outside 0.." + std::to_string(length));
    }
    const std::vector<int64_t> spans = search(pattern, text, static_cast<size_t>(start), "re.find");
    if (spans.empty()) {
        return NullSlot;
    }
    auto *nullable   = static_cast<TupleType *>(norm.resultType());
    auto *matchType  = static_cast<StructType *>(nullable->payloadType());
    auto *groupsType = static_cast<ArrayType *>(matchType->typeAt(3));
    auto *elemType   = static_cast<TupleType *>(groupsType->elemType());
    const size_t groupCount = spans.size() / 2 - 1;

    Array *groups = Array::create(mm::autoSpace(), groupCount);
    for (size_t i = 0; i < groupCount; ++i) {
        const int64_t from = spans[2 * (i + 1)];
        const int64_t to   = spans[2 * (i + 1) + 1];
        slot_t element     = NullSlot;
        if (from >= 0) {
            element = makeSome(stringSlot(cut(text, from, to)), elemType);
        }
        groups->set<slot_t>(i, element, groupsType);
    }
    Struct *match = Struct::create(4, mm::autoSpace());
    match->set<int64_t>(0, spans[0], matchType);
    match->set<int64_t>(1, spans[1], matchType);
    match->set<slot_t>(2, stringSlot(cut(text, spans[0], spans[1])), matchType);
    match->set<Array *>(3, groups, matchType);
    return makeSome(toSlot(match), nullable);
}

slot_t __re_validate__(ArgsView &, ArgsView &norm, Context &) {
    String *pattern = norm.get<String *>(0);
    const std::string_view view = pattern->view();
    const std::string reason =
        camel::utf8::isAscii(view) ? compiled<std::regex>(view).error
                                   : compiled<std::wregex>(view).error;
    if (!reason.empty()) {
        return makeError(
            "RegexInvalid",
            "'" + std::string(view) + "' is not a valid regular expression: " + reason,
            "<re::validate>");
    }
    return makeOk(toSlot(pattern), TupleType::fallible(Type::String()));
}

std::unordered_map<std::string, operator_t> getReOpsMap() {
    return {{"test", __re_test__}, {"find", __re_find__}, {"validate", __re_validate__}};
}
