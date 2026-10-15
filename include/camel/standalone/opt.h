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
 * `T?`, `T!` and the error value in the programs std::cpp writes (design principles 2.9:
 * two built-in forms, each with one bit of tag; an error value is a fixed structure of kind,
 * message and the position where it was made).
 *
 * They are C++ values, not objects: a value of `T?` is the tag and a T, of `T!` the tag, a T and
 * an Error. What `??`, `!!`, `?->` and the fixed patterns of `match` do with them is a branch
 * on the tag in the generated code; here are only the forms and their text.
 */

#pragma once

#include "camel/standalone/str.h"

#include <utility>

namespace camel::standalone {

/// The value of an expression of type `void` where one has to be kept (the payload of a `void!`).
struct Unit {
    bool operator==(const Unit &) const = default;
};

/// The built-in error structure (camel::core::type::errorType: kind, message, location).
struct Error {
    Str kind;
    Str message;
    Str location;
    bool operator==(const Error &) const = default;
};

/// `T?`: a T, or null.
template <typename T> class Opt {
  public:
    Opt() = default; // null
    Opt(T value) : has_(true), value_(std::move(value)) {}

    bool hasValue() const noexcept { return has_; }
    bool isNull() const noexcept { return !has_; }
    /// The payload; only read where the tag says there is one (as :op/tag_null_value).
    const T &value() const noexcept { return value_; }

    friend bool operator==(const Opt &a, const Opt &b) {
        return a.has_ == b.has_ && (!a.has_ || a.value_ == b.value_);
    }

  private:
    bool has_ = false;
    T value_{};
};

/// `T!`: a T, or an Error.
template <typename T> class Fallible {
  public:
    Fallible() = default;
    Fallible(T value) : value_(std::move(value)) {}

    /// The failed value carrying `error` (`throw error(...)`, a failed parse, an `attempt` that
    /// caught a fault).
    static Fallible failure(Error error) {
        Fallible f;
        f.failed_ = true;
        f.error_  = std::move(error);
        return f;
    }

    bool failed() const noexcept { return failed_; }
    const T &value() const noexcept { return value_; }
    const Error &error() const noexcept { return error_; }

    friend bool operator==(const Fallible &a, const Fallible &b) {
        if (a.failed_ != b.failed_) {
            return false;
        }
        return a.failed_ ? a.error_ == b.error_ : a.value_ == b.value_;
    }

  private:
    bool failed_ = false;
    T value_{};
    Error error_;
};

/// A failed value of one `T!` as a failed value of another (`x ?-> f` passes the error on,
/// design principles 2.9). Only applied to a failed value.
template <typename To, typename From> To retag(const From &from) {
    return To::failure(from.error());
}

/// `error(kind, message)` (:op/error_new); the compiler adds the position as the third argument.
inline Error makeError(Str kind, Str message, Str location) {
    return Error{std::move(kind), std::move(message), std::move(location)};
}

} // namespace camel::standalone
