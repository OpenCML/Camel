// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Timing thunks for operator calls (`std::profile`, camel/profile/profile.h).
 *
 * Every operator has one signature, `slot_t (ArgsView &with, ArgsView &norm, Context &)`, so a
 * scheduler can be handed a small function of its own in place of the operator it resolved:
 * one thunk per operator, which times the call and records it under the operator's URI. The
 * scheduler writes
 *
 *     opFunc = camel::profile::profiledOperator(uri, *opFunc);
 *
 * where it resolves the operator (once per node for the node VM, once per graph for the
 * bytecode), and asks `unwrapThunk` where it needs the operator itself (a name lookup, a
 * comparison). Without the session the call returns its argument and nothing else changes.
 */

#pragma once

#include "camel/core/operator.h"

#include <cstddef>

namespace camel::profile {

/// How many distinct operators can be timed at once. A program that calls more has the
/// operators past the limit run untimed, which the report counts and says.
constexpr size_t kMaxTimedOperators = 1024;

/// `op`, or a thunk that times it when the session is recording; `nullptr` stays `nullptr`.
/// `uri` must outlive the run (it is the graph's own operator string, or an interned one).
operator_t profiledOperator(std::string_view uri, operator_t op);

/// True when `op` is a timing thunk of this file.
bool isThunk(operator_t op) noexcept;
/// The operator behind a timing thunk; `op` itself when it is not one.
operator_t unwrapThunk(operator_t op) noexcept;

/// The URI of the operator behind a thunk, or an empty view.
std::string_view thunkUri(operator_t op) noexcept;

/// Number of operator calls the session had to leave untimed (past kMaxTimedOperators).
size_t untimedOperatorCalls() noexcept;

} // namespace camel::profile