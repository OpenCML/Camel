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
 * Function values and `var` cells in the programs std::cpp writes
 * (docs/design/cpp-standalone/README.cn.md, 4 and 5.3).
 *
 * A function value is "a graph and the values it captured" (design principles 2.1).
 * Fn<R(A...)> is one pointer to a counted block that holds the code to run and the captured
 * values; calling it passes the count of open calls first, as every generated function takes
 * it. A function constant without captures is a block too (made once where it is used).
 *
 * A Cell<T> is a `var` variable that leaves the function that declares it (a closure captures
 * it, or it is passed as a `var` argument): every holder reads and writes the same value
 * (design principles 3.5: a capture of a variable is a reference). A variable that does not
 * leave its function is a plain C++ local and needs none of this. A cell that holds a closure
 * that holds the cell is a cycle the count never frees (design principles 10.5 allows a back
 * end not to collect it).
 */

#pragma once

#include "camel/standalone/core.h"

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace camel::standalone {

template <typename Signature> class Fn;

template <typename R, typename... A> class Fn<R(A...)> {
  public:
    Fn() noexcept = default;

    /// A function value running `code(depth, args...)`; `text` is what printing it writes
    /// (the runtime's `Function(graph=..., tupleSlots=...)`).
    template <typename F> Fn(const char *text, F code) {
        auto *holder       = new Holder<F>{{1, &invoke<F>, &destroy<F>, text}, std::move(code)};
        rep_               = holder;
    }

    Fn(const Fn &other) noexcept : rep_(other.rep_) {
        if (rep_ != nullptr) {
            ++rep_->references;
        }
    }
    Fn(Fn &&other) noexcept : rep_(std::exchange(other.rep_, nullptr)) {}
    Fn &operator=(Fn other) noexcept {
        std::swap(rep_, other.rep_);
        return *this;
    }
    ~Fn() {
        if (rep_ != nullptr && --rep_->references == 0) {
            rep_->destroy(rep_);
        }
    }

    /// The call: `depth` is the count of open calls the callee runs at.
    R operator()(Depth depth, A... args) const {
        return rep_->invoke(rep_, depth, std::move(args)...);
    }

    const char *text() const noexcept { return rep_ != nullptr ? rep_->text : "null"; }
    bool isNull() const noexcept { return rep_ == nullptr; }

  private:
    struct Rep {
        std::size_t references;
        R (*invoke)(Rep *, Depth, A...);
        void (*destroy)(Rep *);
        const char *text;
    };
    template <typename F> struct Holder : Rep {
        F code;
    };
    template <typename F> static R invoke(Rep *rep, Depth depth, A... args) {
        return static_cast<Holder<F> *>(rep)->code(depth, std::move(args)...);
    }
    template <typename F> static void destroy(Rep *rep) { delete static_cast<Holder<F> *>(rep); }

    Rep *rep_ = nullptr;
};

template <typename T> class Cell {
  public:
    Cell() = default; // only to be assigned
    explicit Cell(T value) : rep_(new Rep{1, std::move(value)}) {}
    Cell(const Cell &other) noexcept : rep_(other.rep_) {
        if (rep_ != nullptr) {
            ++rep_->references;
        }
    }
    Cell(Cell &&other) noexcept : rep_(std::exchange(other.rep_, nullptr)) {}
    Cell &operator=(Cell other) noexcept {
        std::swap(rep_, other.rep_);
        return *this;
    }
    ~Cell() {
        if (rep_ != nullptr && --rep_->references == 0) {
            delete rep_;
        }
    }

    /// `:var/get`.
    const T &get() const noexcept { return rep_->value; }
    /// An assignment (`:op/assn`): stores `value` and gives it.
    const T &set(T value) const {
        rep_->value = std::move(value);
        return rep_->value;
    }
    /// The value, moved out for an assignment that computes the new value from it and then
    /// stores it with set() (`acc = acc->append<v>`, the in-place form of `append`): an array
    /// the cell held alone is then grown in place. Nothing reads the cell between take() and
    /// set().
    T take() const noexcept { return std::move(rep_->value); }

  private:
    struct Rep {
        std::size_t references;
        T value;
    };
    Rep *rep_ = nullptr;
};

} // namespace camel::standalone
