// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The operators that call function values (`map`, `filter`, `reduce`, `foreach` and the
 * fault boundary `attempt`) in the programs std::cpp writes, as the schedulers carry them out
 * (src/passes/sched/nodevm/nodevm.cpp, evalMarkedOperator_*).
 *
 * Each call of the element function is a call like any other: it counts as one more open call,
 * checked at the operator's site (core.h, call()). `map` and `filter` run the elements in order;
 * the language promises no order between them (design principles 3.6), so this is one of the
 * orders it allows, and a fault in an element is reported for the first element that faults,
 * as 3.6 asks of a parallel `map`.
 */

#pragma once

#include "camel/standalone/array.h"
#include "camel/standalone/core.h"
#include "camel/standalone/opt.h"

#include <cstdio>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace camel::standalone {

/// `xs->map<f>` (:mark/map_arr).
template <typename T, typename F>
auto map(Depth depth, const Site &site, const Array<T> &xs, const F &f) {
    using R = std::remove_cvref_t<decltype(f(depth, xs[0]))>;
    Array<R> out;
    out.reserve(xs.size());
    for (const T &x : xs) {
        out.push(f(call(depth, site), x));
    }
    return out;
}

/// `xs->filter<f>` (:mark/filter_arr): the elements for which `f` gives true, in order.
template <typename T, typename F>
Array<T> filter(Depth depth, const Site &site, const Array<T> &xs, const F &f) {
    Array<T> out;
    for (const T &x : xs) {
        if (f(call(depth, site), x)) {
            out.push(x);
        }
    }
    return out;
}

/// `xs->reduce<f, init>` (:mark/reduce_arr): `f(f(f(init, x0), x1), ...)`. The generated code
/// names A, the type of the result (`init` may be a literal of another C++ type).
template <typename A, typename T, typename F>
A reduce(Depth depth, const Site &site, const Array<T> &xs, const F &f,
         std::type_identity_t<A> init) {
    for (const T &x : xs) {
        init = f(call(depth, site), std::move(init), x);
    }
    return init;
}

/// `xs->foreach<f>` (:mark/foreach_arr): `f` on each element, in order, for its effects.
template <typename T, typename F>
void foreach(Depth depth, const Site &site, const Array<T> &xs, const F &f) {
    for (const T &x : xs) {
        f(call(depth, site), x);
    }
}

/// `attempt(f, args...)` (:mark/isolate, design principles 3.9): the call of `f` in which a
/// fault becomes the failed value of the result, with the fault's kind, message and location
/// (src/execute/fault_isolation.cpp). The call counts like any other, checked outside the
/// boundary: a recursion limit reached by the call itself is not isolated. `exit` passes
/// through. Output written before the fault stays written.
template <typename R, typename F, typename... A>
Fallible<R> attempt(Depth depth, const Site &site, const F &f, A &&...args) {
    const Depth inner = call(depth, site);
    try {
        using Result = std::remove_cvref_t<decltype(f(inner, std::forward<A>(args)...))>;
        if constexpr (std::is_void_v<Result>) {
            f(inner, std::forward<A>(args)...);
            return Fallible<R>(R{});
        } else if constexpr (std::is_same_v<Result, Fallible<R>>) {
            // A function that gives a `T!` itself: its value is the result as it is.
            return f(inner, std::forward<A>(args)...);
        } else {
            return Fallible<R>(f(inner, std::forward<A>(args)...));
        }
    } catch (const Fault &fault) {
        return Fallible<R>::failure(Error{fault.kind(), fault.message(), fault.location()});
    }
}

/// An error value nobody handled reached the exit of `main` (design principles 3.9): the
/// report of `site` (rendered without a position) with the error's kind and message, and the
/// position the error records; on standard error, after the output. Gives the exit code 1.
inline int unhandled(const Site &site, const Error &error) {
    std::string message(error.message.view());
    while (!message.empty() && message.back() == '.') {
        message.pop_back();
    }
    const std::vector<std::string> values{error.kind.str(), message};
    std::string text = withValues(site.text, values);
    if (!error.location.empty()) {
        const std::size_t firstLine = text.find('\n');
        text.insert(firstLine == std::string::npos ? text.size() : firstLine,
                    "\n  --> " + error.location.str());
    }
    std::fflush(stdout);
    std::fputs(text.c_str(), stderr);
    std::fputs("\n\n", stderr);
    return 1;
}

} // namespace camel::standalone
