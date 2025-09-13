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
 * See the the MIT license for more details
 *
 * Author: Zhenjie Wei
 * Created: Jul. 03, 2025
 * Updated: Jul. 03, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <ranges>
#include <string>
#include <string_view>

namespace strutil {

constexpr std::string_view trim(std::string_view str, std::string_view whitespace = " \t\n\r") {
    const auto start = str.find_first_not_of(whitespace);
    if (start == std::string_view::npos)
        return {};
    const auto end = str.find_last_not_of(whitespace);
    return str.substr(start, end - start + 1);
}

constexpr std::string_view
trim_left(std::string_view str, std::string_view whitespace = " \t\n\r") {
    const auto start = str.find_first_not_of(whitespace);
    return (start == std::string_view::npos) ? std::string_view{} : str.substr(start);
}

constexpr std::string_view
trim_right(std::string_view str, std::string_view whitespace = " \t\n\r") {
    const auto end = str.find_last_not_of(whitespace);
    return (end == std::string_view::npos) ? std::string_view{} : str.substr(0, end + 1);
}

inline std::vector<std::string_view>
split(std::string_view str, char delim, bool skip_empty = true) {
    std::vector<std::string_view> result;
    size_t start = 0;
    while (start < str.size()) {
        size_t end = str.find(delim, start);
        if (end == std::string_view::npos)
            end = str.size();
        auto part = str.substr(start, end - start);
        if (!skip_empty || !part.empty())
            result.push_back(part);
        start = end + 1;
    }
    return result;
}

template <std::ranges::input_range Range, typename Formatter = std::identity, typename CharT = char>
    requires requires(Formatter f, std::ranges::range_value_t<Range> v) {
        { f(v) } -> std::convertible_to<std::basic_string_view<CharT>>;
    }
std::basic_string<CharT>
join(const Range &range, std::basic_string_view<CharT> delimiter, Formatter formatter = {}) {
    std::basic_string<CharT> result;
    auto it = std::ranges::begin(range);
    auto end = std::ranges::end(range);
    if (it == end)
        return result;

    result += formatter(*it++);
    while (it != end) {
        result += delimiter;
        result += formatter(*it++);
    }
    return result;
}

template <std::ranges::input_range Range, typename Formatter = std::identity, typename CharT = char>
    requires requires(Formatter f, std::ranges::range_value_t<Range> v) {
        { f(v) } -> std::convertible_to<std::basic_string_view<CharT>>;
    }
std::basic_string<CharT>
join(const Range &range, const CharT *delimiter, Formatter formatter = {}) {
    return join<Range, Formatter, CharT>(
        range,
        std::basic_string_view<CharT>(delimiter),
        formatter);
}

template <std::ranges::input_range Range, typename Formatter = std::identity, typename CharT = char>
    requires requires(Formatter f, std::ranges::range_value_t<Range> v) {
        { f(v) } -> std::convertible_to<std::basic_string_view<CharT>>;
    }
void join(
    std::basic_ostream<CharT> &os, const Range &range, std::basic_string_view<CharT> delimiter,
    Formatter formatter = {}) {
    auto it = std::ranges::begin(range);
    auto end = std::ranges::end(range);
    if (it == end)
        return;

    os << formatter(*it++);
    while (it != end) {
        os << delimiter;
        os << formatter(*it++);
    }
}

template <std::ranges::input_range Range, typename Formatter = std::identity, typename CharT = char>
    requires requires(Formatter f, std::ranges::range_value_t<Range> v) {
        { f(v) } -> std::convertible_to<std::basic_string_view<CharT>>;
    }
void join(
    std::basic_ostream<CharT> &os, const Range &range, const CharT *delimiter,
    Formatter formatter = {}) {
    join<Range, Formatter, CharT>(os, range, std::basic_string_view<CharT>(delimiter), formatter);
}

} // namespace strutil