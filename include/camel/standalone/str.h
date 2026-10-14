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
 * Author: Zhenjie Wei
 * Created: Oct. 14, 2026
 * Updated: Oct. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * `string` in the programs std::cpp writes (docs/design/cpp-standalone/README.cn.md, 4 and 5).
 *
 * A Str is one pointer to a block {references, size, capacity, bytes}; the empty string is the
 * null pointer. The bytes never change while more than one Str refers to them: a string is a
 * value, and two holders of the same block cannot tell it from two copies (design principles
 * 2.11). Copying a Str counts one more reference, destroying it one less; the last one frees
 * the block. The count is not atomic: a generated program runs on one thread.
 *
 * Concatenation takes its left operand by value. When the generated code passes the operand's
 * last use (std::move) and nothing else refers to the block, the bytes are appended in place,
 * so building a string piece by piece in a loop is linear.
 *
 * Strings are bytes and their lengths and indices count bytes (design principles 2.9).
 */

#pragma once

#include "camel/standalone/core.h"

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <string_view>
#include <utility>

namespace camel::standalone {

class Str {
  public:
    Str() noexcept = default;
    Str(std::string_view text) {
        if (!text.empty()) {
            rep_ = allocate(text.size());
            std::memcpy(rep_->bytes(), text.data(), text.size());
            rep_->size = text.size();
        }
    }
    Str(const char *text) : Str(std::string_view(text)) {}
    Str(const std::string &text) : Str(std::string_view(text)) {}

    Str(const Str &other) noexcept : rep_(other.rep_) {
        if (rep_ != nullptr) {
            ++rep_->references;
        }
    }
    Str(Str &&other) noexcept : rep_(std::exchange(other.rep_, nullptr)) {}
    Str &operator=(Str other) noexcept {
        std::swap(rep_, other.rep_);
        return *this;
    }
    ~Str() { release(rep_); }

    std::size_t size() const noexcept { return rep_ != nullptr ? rep_->size : 0; }
    bool empty() const noexcept { return size() == 0; }
    const char *data() const noexcept { return rep_ != nullptr ? rep_->bytes() : ""; }
    std::string_view view() const noexcept { return {data(), size()}; }
    operator std::string_view() const noexcept { return view(); }
    std::string str() const { return std::string(view()); }

    /// `a + b`: `a` followed by `b`, in `a`'s block when `a` is its only holder and has room.
    friend Str operator+(Str a, const Str &b) {
        if (b.empty()) {
            return a;
        }
        if (a.empty()) {
            return b;
        }
        const std::size_t total = a.size() + b.size();
        if (a.rep_->references != 1 || a.rep_->capacity < total) {
            // Room for the result and as much again: a loop that appends grows it in place.
            Rep *grown = allocate(std::max(total, a.size() * 2));
            std::memcpy(grown->bytes(), a.data(), a.size());
            grown->size = a.size();
            release(std::exchange(a.rep_, grown));
        }
        std::memcpy(a.rep_->bytes() + a.rep_->size, b.data(), b.size());
        a.rep_->size = total;
        return a;
    }

    /// `==`, `!=`, `<` and the rest: by the bytes; a proper prefix comes first (as the runtime's
    /// String::compare).
    friend bool operator==(const Str &a, const Str &b) noexcept { return a.view() == b.view(); }
    friend std::strong_ordering operator<=>(const Str &a, const Str &b) noexcept {
        return a.view() <=> b.view();
    }

  private:
    struct Rep {
        std::size_t references;
        std::size_t size;
        std::size_t capacity;
        char *bytes() noexcept { return reinterpret_cast<char *>(this + 1); }
    };

    static Rep *allocate(std::size_t capacity) {
        auto *rep       = static_cast<Rep *>(::operator new(sizeof(Rep) + capacity));
        rep->references = 1;
        rep->size       = 0;
        rep->capacity   = capacity;
        return rep;
    }

    static void release(Rep *rep) noexcept {
        if (rep != nullptr && --rep->references == 0) {
            ::operator delete(rep);
        }
    }

    Rep *rep_ = nullptr;
};

/// `len(s)` (:struct/len_str): the number of bytes.
inline int64_t length(const Str &s) { return static_cast<int64_t>(s.size()); }

/// `s[i]` (:op/idx_str): the string of the one byte at `i` (design principles 2.10). An index
/// outside the string is IndexOutOfRange at `site` (values: the index, "a string", the length).
inline Str byteAt(const Str &s, int64_t index, const Site &site) {
    if (index < 0 || static_cast<std::size_t>(index) >= s.size()) [[unlikely]] {
        fault(site, {std::to_string(index), "a string", std::to_string(s.size())});
    }
    return Str(std::string_view(s.data() + index, 1));
}

/// `s->contains<sub>` (:struct/contains_str): whether the bytes of `sub` occur in `s`.
inline bool contains(const Str &s, const Str &sub) {
    return s.view().find(sub.view()) != std::string_view::npos;
}

} // namespace camel::standalone
