// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `T[]` in the programs std::cpp writes (docs/design/cpp-standalone/README.cn.md, 4 and 5.2).
 *
 * An Array<T> is one pointer to a block {references, size, capacity, elements}; the empty
 * array is the null pointer. The elements are the C++ values of T, so an `int[]` is a run of
 * int64_t. An array is a value (design principles 11.3: `append` returns a new array): two
 * holders of one block never see a change, because a block is changed only while it has one
 * holder. The operations that give a new array take their first argument by value; when the
 * generated code passes its last use (std::move) and nothing else holds the block, they change
 * it in place, so `acc = acc->append<x>` in a loop is linear.
 *
 * The operations follow the runtime's (src/builtin/operators/struct.cpp, ops.cpp), faults
 * included.
 */

#pragma once

#include "camel/standalone/core.h"
#include "camel/standalone/str.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <new>
#include <string>
#include <type_traits>
#include <utility>

namespace camel::standalone {

template <typename T> class Array {
  public:
    using value_type = T;

    Array() noexcept = default;
    Array(std::initializer_list<T> items) {
        reserve(items.size());
        for (const T &item : items) {
            push(item);
        }
    }

    Array(const Array &other) noexcept : rep_(other.rep_) {
        if (rep_ != nullptr) {
            ++rep_->references;
        }
    }
    Array(Array &&other) noexcept : rep_(std::exchange(other.rep_, nullptr)) {}
    Array &operator=(Array other) noexcept {
        std::swap(rep_, other.rep_);
        return *this;
    }
    ~Array() { release(rep_); }

    std::size_t size() const noexcept { return rep_ != nullptr ? rep_->size : 0; }
    bool empty() const noexcept { return size() == 0; }
    const T &operator[](std::size_t i) const noexcept { return rep_->items()[i]; }
    const T *begin() const noexcept { return rep_ != nullptr ? rep_->items() : nullptr; }
    const T *end() const noexcept { return begin() + size(); }

    /// Makes the block this array's alone (copying it when it is shared), with room for
    /// `capacity` elements at least.
    void reserve(std::size_t capacity) {
        if (rep_ != nullptr && rep_->references == 1 && rep_->capacity >= capacity) {
            return;
        }
        // Growing a block of its own doubles it, so that adding one element at a time is
        // linear; a shared block is copied at the size asked for.
        const bool growing = rep_ != nullptr && rep_->references == 1;
        Rep *fresh         = allocate(std::max(
            {capacity, size(), growing ? rep_->capacity * 2 : std::size_t{0}, std::size_t{4}}));
        for (std::size_t i = 0; i < size(); ++i) {
            if (rep_->references == 1) {
                new (fresh->items() + i) T(std::move(rep_->items()[i]));
            } else {
                new (fresh->items() + i) T(rep_->items()[i]);
            }
            ++fresh->size;
        }
        release(std::exchange(rep_, fresh));
    }

    /// Adds `item` at the end, in place when the block is this array's alone.
    void push(T item) {
        if (rep_ == nullptr || rep_->references != 1 || rep_->size == rep_->capacity) {
            reserve(std::max(size() + 1, size() * 2));
        }
        new (rep_->items() + rep_->size) T(std::move(item));
        ++rep_->size;
    }

    friend bool operator==(const Array &a, const Array &b) {
        // By content, element by element (design principles 4.2); not short-cut by identity:
        // an array that holds a NaN does not equal itself.
        if (a.size() != b.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (!(a[i] == b[i])) {
                return false;
            }
        }
        return true;
    }

  private:
    struct Rep {
        std::size_t references;
        std::size_t size;
        std::size_t capacity;
        T *items() noexcept { return reinterpret_cast<T *>(this + 1); }
    };
    static_assert(alignof(T) <= alignof(Rep), "elements must not need more alignment");

    static Rep *allocate(std::size_t capacity) {
        auto *rep       = static_cast<Rep *>(::operator new(sizeof(Rep) + capacity * sizeof(T)));
        rep->references = 1;
        rep->size       = 0;
        rep->capacity   = capacity;
        return rep;
    }

    static void release(Rep *rep) noexcept {
        if (rep != nullptr && --rep->references == 0) {
            for (std::size_t i = 0; i < rep->size; ++i) {
                rep->items()[i].~T();
            }
            ::operator delete(rep);
        }
    }

    Rep *rep_ = nullptr;
};

/// `len(xs)` (:struct/len_arr).
template <typename T> int64_t length(const Array<T> &xs) { return static_cast<int64_t>(xs.size()); }

/// `xs[i]` (:op/idx_arr): an index outside the array is IndexOutOfRange at `site` (values: the
/// index, "an array", the length).
template <typename T> T at(const Array<T> &xs, int64_t index, const Site &site) {
    if (index < 0 || static_cast<std::size_t>(index) >= xs.size()) [[unlikely]] {
        fault(site, {std::to_string(index), "an array", std::to_string(xs.size())});
    }
    return xs[static_cast<std::size_t>(index)];
}

/// A type name applied to an array (:op/atoi, :op/atol, :op/atof, :op/atod): every element converted
/// by the C++ conversion, which is what the runtime's does (core/array_convert.h).
template <typename To, typename From> Array<To> convertArray(const Array<From> &xs) {
    Array<To> out;
    out.reserve(xs.size());
    for (const From &x : xs) {
        out.push(static_cast<To>(x));
    }
    return out;
}

/// `xs->append<x>` (:struct/append_arr): a new array, `xs` followed by `x`.
template <typename T> Array<T> append(Array<T> xs, std::type_identity_t<T> item) {
    xs.push(std::move(item));
    return xs;
}

/// `xs->extend<ys>` (:struct/extend_arr), `concat(xs, ys)` (:struct/concat_arr).
template <typename T> Array<T> concat(Array<T> xs, const Array<T> &ys) {
    xs.reserve(xs.size() + ys.size());
    for (const T &y : ys) {
        xs.push(y);
    }
    return xs;
}

/// `xs->slice<start, end>` (:struct/slice_arr): a bound outside 0..len is SliceOutOfRange at
/// `site` (values: the start, the end, "an array", the length); an end before the start gives
/// the empty array (design principles 3.9).
template <typename T>
Array<T> slice(const Array<T> &xs, int64_t start, int64_t end, const Site &site) {
    const auto n = static_cast<int64_t>(xs.size());
    if (start < 0 || start > n || end < 0 || end > n) [[unlikely]] {
        fault(
            site,
            {std::to_string(start), std::to_string(end), "an array", std::to_string(xs.size())});
    }
    Array<T> out;
    if (end > start) {
        out.reserve(static_cast<std::size_t>(end - start));
        for (int64_t i = start; i < end; ++i) {
            out.push(xs[static_cast<std::size_t>(i)]);
        }
    }
    return out;
}

/// `xs[a:b]`, `xs[a:b:s]`, `xs[:b]`, `xs[a:]`, `xs[::-1]` and `slice(xs, 0, a, b, s)` (:op/slice_sub)
/// of a one-dimensional array, as struct.cpp's __slice_sub__ reads them: `written` says which
/// bounds the program wrote (bit 1 the end, bit 2 the start), a step of 0 or an axis other than the
/// first is InvalidArgument at `invalid` (values: "[]", what is wrong), and a bound that leaves the
/// array is SliceOutOfRange at `outOfRange` (values: the start, the end, "an array", the length).
template <typename T>
Array<T> sliceSub(
    const Array<T> &xs, int64_t axis, int64_t startRaw, int64_t endRaw, int64_t step,
    int64_t written, const Site &outOfRange, const Site &invalid) {
    if (axis != 0 && axis != -1) [[unlikely]] {
        fault(invalid, {"[]", "a slice axis must be between 0 and 0, not " + std::to_string(axis)});
    }
    if (step == 0) [[unlikely]] {
        fault(invalid, {"[]", "a slice step must not be 0"});
    }
    const auto extent     = static_cast<int64_t>(xs.size());
    const bool wroteEnd   = (written & 1) != 0;
    const bool wroteStart = (written & 2) != 0;
    int64_t start         = 0;
    int64_t count         = 0;
    if (step > 0) {
        start             = wroteStart ? startRaw : 0;
        const int64_t end = wroteEnd ? endRaw : extent;
        if (start < 0 || start > extent || end < 0 || end > extent) [[unlikely]] {
            fault(
                outOfRange,
                {std::to_string(start), std::to_string(end), "an array", std::to_string(extent)});
        }
        count = start < end ? (end - start + step - 1) / step : 0;
    } else {
        start             = wroteStart ? startRaw : extent - 1;
        const int64_t end = wroteEnd ? endRaw : -1;
        if ((wroteStart && (start < 0 || start >= extent)) ||
            (wroteEnd && (end < 0 || end > extent))) [[unlikely]] {
            fault(
                outOfRange,
                {std::to_string(start), std::to_string(end), "an array", std::to_string(extent)});
        }
        count = start > end ? (start - end + (-step) - 1) / (-step) : 0;
    }
    Array<T> out;
    out.reserve(static_cast<std::size_t>(count));
    for (int64_t i = 0; i < count; ++i) {
        out.push(xs[static_cast<std::size_t>(start + i * step)]);
    }
    return out;
}

/// `head(xs)` (:struct/head_arr): the first element; of the empty array IndexOutOfRange at
/// `site`, as `xs[0]`.
template <typename T> T head(const Array<T> &xs, const Site &site) { return at(xs, 0, site); }

/// `tail(xs)` (:struct/tail_arr): all but the first element; the empty array for one or none.
template <typename T> Array<T> tail(const Array<T> &xs) {
    Array<T> out;
    for (std::size_t i = 1; i < xs.size(); ++i) {
        out.push(xs[i]);
    }
    return out;
}

/// `xs->contains<x>` (:struct/contains_arr): by the `==` of the element type.
template <typename T> bool contains(const Array<T> &xs, const std::type_identity_t<T> &item) {
    for (const T &x : xs) {
        if (x == item) {
            return true;
        }
    }
    return false;
}

/// `xs->join<sep>` (:str/join, str.cpp __join__): the strings with `separator` between them.
inline Str join(const Array<Str> &xs, const Str &separator) {
    std::string out;
    for (std::size_t i = 0; i < xs.size(); ++i) {
        if (i > 0) {
            out += separator.view();
        }
        out += xs[i].view();
    }
    return Str(out);
}

/// `for c in s` (:struct/iter_str): the code points of `s`, each a one-character string, in
/// order — what `s[i]` gives for every `i` (design principles 11.1).
inline Array<Str> chars(const Str &s) {
    const std::string_view text = s.view();
    Array<Str> out;
    out.reserve(camel::utf8::count(text));
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t start = at;
        (void)camel::utf8::decodeOne(text, at);
        out.push(Str(text.substr(start, at - start)));
    }
    return out;
}

/// `range(start, stop[, step])` (:struct/range): a step of 0 is InvalidArgument at `site`
/// (values: "range", what is wrong).
inline Array<int64_t> range(int64_t start, int64_t stop, int64_t step, const Site &site) {
    if (step == 0) [[unlikely]] {
        fault(
            site,
            {"range",
             "the step is 0 (from " + std::to_string(start) + " to " + std::to_string(stop) +
                 "); a range needs a step that moves"});
    }
    Array<int64_t> out;
    std::size_t count = 0;
    if ((step > 0 && start < stop) || (step < 0 && start > stop)) {
        count = step > 0 ? static_cast<std::size_t>((stop - start + step - 1) / step)
                         : static_cast<std::size_t>((start - stop - 1) / (-step) + 1);
    }
    out.reserve(count);
    int64_t value = start;
    for (std::size_t i = 0; i < count; ++i, value += step) {
        out.push(value);
    }
    return out;
}

/// `zip(xs, ys, ...)` (:struct/zip): the array of tuples `R::value_type{xs[i], ys[i], ...}`;
/// arrays of different lengths are InvalidArgument at `site` (values: "zip", what is wrong,
/// worded as struct.cpp's __zip__ words it).
template <typename R, typename First, typename... Rest>
R zip(const Site &site, const Array<First> &first, const Array<Rest> &...rest) {
    const std::size_t n = first.size();
    std::size_t index   = 1;
    auto check          = [&](std::size_t size) {
        ++index;
        if (size == n) {
            return;
        }
        fault(
            site,
            {"zip",
             sizeof...(Rest) == 1
                 ? "the two sequences have " + std::to_string(n) + " and " +
                       std::to_string(size) + " elements, and they must have the same number"
                 : "sequences 1 and " + std::to_string(index) + " have " + std::to_string(n) +
                       " and " + std::to_string(size) +
                       " elements, and all must have the same number"});
    };
    (check(rest.size()), ...);
    R out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        out.push(typename R::value_type{first[i], rest[i]...});
    }
    return out;
}

} // namespace camel::standalone
