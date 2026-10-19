/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Jul. 29, 2025
 * Updated: Oct. 13, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "operators.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"

#include "camel/utils/utf8.h"
#include <regex>
#include <string>

using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::rtdata;

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

} // namespace

slot_t __re_test__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *pattern = norm.get<String *>(0);
    String *text    = norm.get<String *>(1);
    try {
        if (!camel::utf8::isAscii(pattern->view()) || !camel::utf8::isAscii(text->view())) {
            std::wregex re(widen(pattern->view()));
            return toSlot(std::regex_search(widen(text->view()), re));
        }
        std::regex re(pattern->c_str());
        bool match = std::regex_search(text->c_str(), re);
        return toSlot(match);
    } catch (const std::regex_error &e) {
        throwRuntimeFault(
            RuntimeDiag::RegexInvalid,
            "re.test",
            std::string(pattern->c_str()),
            regexErrorReason(e));
    }
}

std::unordered_map<std::string, operator_t> getReOpsMap() { return {{"test", __re_test__}}; }
