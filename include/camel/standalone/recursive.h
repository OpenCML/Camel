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
 * Created: Oct. 20, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Helpers for a recursive enum in the C++ a Camel program is translated into
 * (docs/design/cpp-standalone). A field whose type contains the enum itself is held through
 * `std::shared_ptr`; three things then need an explicit stack instead of the native one:
 *
 *  - freeing: a chain of a million nodes would free its tail recursively. The generated
 *    destructor moves every boxed child onto `destroyStack()` and the outermost destructor
 *    drains it, so each `~T` returns before the next one starts.
 *  - equality: the generated `operator==` keeps a work list of node pairs (in the generated
 *    code, where the fields are known) and compares boxed children through `boxedEqual`.
 *  - printing: the generated `appendText` keeps a work list of actions (likewise generated).
 *
 * `boxedEqual` compares what the pointers point at, never the pointers themselves: two values
 * built separately must compare equal when their contents do.
 */

#pragma once

#include <functional>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

namespace camel::standalone {

/// The boxed children of the value being destroyed, deepest last.
inline std::vector<std::shared_ptr<void>> &destroyStack() {
    static thread_local std::vector<std::shared_ptr<void>> stack;
    return stack;
}

inline int &destroyDepth() {
    static thread_local int depth = 0;
    return depth;
}

/// True for the outermost generated destructor of a nested destruction: only it drains.
inline bool beginDestroy() { return destroyDepth()++ == 0; }
inline void endDestroy() { --destroyDepth(); }

inline void drainDestroyed() {
    auto &stack = destroyStack();
    while (!stack.empty()) {
        // Moving the pointer out first means the child's destructor can push its own children.
        std::shared_ptr<void> child = std::move(stack.back());
        stack.pop_back();
        child.reset();
    }
}

/// Hands a boxed child to the pending list, leaving the member empty.
template <typename T> inline void boxToStack(std::shared_ptr<T> &value) {
    if (value) {
        destroyStack().push_back(std::shared_ptr<void>(std::move(value)));
    }
}

/// Equality of two boxed values: both absent, or both present and equal in content.
template <typename T>
inline bool boxedEqual(const std::shared_ptr<T> &a, const std::shared_ptr<T> &b) {
    if (!a || !b) {
        return a == b;
    }
    return *a == *b;
}

/// Boxes a field value of a recursive enum. The call is made from a `make` branch chosen by the
/// variant tag, so the pointee may itself be a type that holds the enum by value (`T?`, `T[]`)
/// and is only complete after the enum's definition.
template <typename T, typename V> inline std::shared_ptr<T> box(V &&value) {
    return std::make_shared<T>(std::forward<V>(value));
}

} // namespace camel::standalone