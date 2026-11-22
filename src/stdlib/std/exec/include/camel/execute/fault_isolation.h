// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Fault isolation (design principles 3.9; docs/technical/33_fault_isolation.md).
 *
 * In a program it is one operator of the name `attempt`, which is a function transform: applied
 * to a function `f` it gives the function `attempt(f)` whose parameters are `f`'s and whose
 * result is `f`'s result plus `!`. The value it gives holds one small graph of its own, and that
 * graph's body is one OPER node with the URI kFaultIsolationUri whose first input is the captured
 * `f` and whose other inputs are the arguments of the call: a marked call of the subgraph of
 * that function. A fault raised inside the call becomes, at the node, the failed form of the
 * node's `T!`. Every scheduler runs that node itself (it declares how it calls its function:
 * ElementShape::Isolate); this header holds what both halves share: the names, the two type
 * rules, and the conversion of a fault that reached the boundary into an error value.
 */

#pragma once

#include "camel/core/error/diagnostics.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/type.h"

#include <optional>
#include <string_view>

namespace camel::execute {

/// The name the operator has in a program: `attempt`, settled by design principles 3.9. This
/// is the only place it is written: everything else uses the URIs below, which do not
/// contain it.
inline constexpr const char *kFaultIsolationName = "attempt";

/// The URI of the transform `attempt(f)`. It is not a node: the builder reads the call and makes
/// the function value (Builder::attemptFunctionValue), so no scheduler ever sees this URI.
inline constexpr std::string_view kAttemptTransformUri = ":mark/attempt";

/// The URI of the marked call the value of `attempt(f)` holds. Schedulers recognise it by its
/// declaration, not its name: it declares ElementShape::Isolate (camel/core/operator_semantics.h).
inline constexpr std::string_view kFaultIsolationUri = ":mark/isolate";

/// The name the marked call's operator is registered under. No program writes it: the builder
/// builds that call (Builder::attemptFunctionValue), and what a program writes is `attempt`, the
/// transform above. A name no program writes keeps the two apart, and being internal it is the
/// one group no declaration writes either.
inline constexpr const char *kFaultIsolationOperatorName = "__isolate__";

/// The function `attempt(f)` gives: the parameters of `f`, and `f`'s result type plus `!`.
/// nullptr when the transform does not apply: `f` is null, takes a `with` parameter (the marked
/// call passes no `with` argument), takes a `var` parameter (the marked call is given values), or
/// declares no result type. The value carries no resource signature of `f`'s: it is built at the
/// call site out of a callee the type does not name, and a call through it reaches what the
/// callee of that call reaches (design effects plan 2.2).
core::type::FunctionType *attemptFunctionType(core::type::FunctionType *f);

/// The result type of the call `attempt(f)` -- the function above -- for the argument types
/// `norm` (the function value alone), or nullopt when it does not apply.
/// DESIGN-QUESTION(attempt-fallible-target)
std::optional<core::type::Type *>
resolveAttemptTransformType(const core::type::type_vec_t &with, const core::type::type_vec_t &norm);

/// The result type of the marked call for arguments of the types `norm` (the function value
/// first), or nullopt when it does not apply: `f: (A...) => T` gives `T!`; a function that
/// already returns `U!` gives `U!`.
/// DESIGN-QUESTION(attempt-fallible-target)
std::optional<core::type::Type *>
resolveFaultIsolationType(const core::type::type_vec_t &with, const core::type::type_vec_t &norm);

/// The value of the node after the callee returned `value` normally: `value` itself when the
/// callee's result type is already the node's type (`U!`), otherwise `value` put into the
/// node's `T!`. `calleeResultType` is the exit type of the callee graph.
slot_t wrapIsolatedResult(
    slot_t value, const core::type::Type *calleeResultType, const core::type::Type *nodeType);

/// The failed `T!` for a fault that reached the boundary (core::error::isIsolatedFault): kind
/// is the diagnostic's name without its type prefix, message its message, location
/// `file:line:column` of the place it points at (the format `error(kind, message)` records).
slot_t isolatedFaultToError(const core::error::Diagnostic &fault);

} // namespace camel::execute
