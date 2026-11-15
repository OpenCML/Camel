// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * A type variable's occurrences and the resources of the values given for them.
 *
 * `T` is one type, and the resource component of a function type is part of it (design principles
 * 3.3: a function's signature says what it touches, and two instances whose values touch different
 * resources are different types). So two values given for one variable that are function types of
 * one shape must write the same resources; a use that takes one value's set for the other's would
 * let the place a value is used decide what a function reaches.
 *
 * Whether they do is not known while the bodies are being built: a declaration that writes no
 * clause has its resources measured from its own body, and a body may be written after the place
 * it is used in. The type layer therefore does not judge the pair where it meets it. It asks the
 * compiler in hand to record it (this sink), and the answer is given once every body has been
 * measured (the ruling of 2026-11-15, design principles 3.3).
 *
 * The sink answers whether it recorded the pair: false leaves the type layer to decide the pair
 * itself, which is what a build without a compiler in hand (`typeEquals` on its own) needs.
 */

#pragma once

#include <functional>

namespace camel::core::type {

class Type;

using ResourceConflictSink = std::function<bool(const Type &left, const Type &right)>;

/// The compiler in hand, for the length of a build; empty when there is none.
ResourceConflictSink &resourceConflictSink();

} // namespace camel::core::type