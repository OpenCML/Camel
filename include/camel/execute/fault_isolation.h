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
 * Created: Oct. 12, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The fault-isolation operator (design principles 2.9; docs/technical/33_fault_isolation.md).
 *
 * On the graph it is one OPER node with the URI kFaultIsolationUri whose first input is a
 * function value and whose other inputs are the arguments: a marked call of the subgraph of
 * that function. A fault raised inside the call becomes, at the node, the failed form of the
 * node's `T!`. Every scheduler runs the node itself (it declares how it calls its function:
 * ElementShape::Isolate); this header holds what they share: the names, the type rule, and the
 * conversion of a fault that reached the boundary into an error value.
 */

#pragma once

#include "camel/core/error/diagnostics.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/type.h"

#include <optional>
#include <string_view>

namespace camel::execute {

/// The name the operator has in a program: `attempt`, settled by design principles 2.10. This
/// is the only place it is written: everything else uses the URI below, which does not
/// contain it.
inline constexpr const char *kFaultIsolationName = "attempt";

/// The URI of the operator. Schedulers recognise it by its declaration, not its name: it
/// declares ElementShape::Isolate (camel/core/operator_semantics.h).
inline constexpr std::string_view kFaultIsolationUri = ":mark/isolate";

/// The result type of the operator applied to arguments of the types `norm` (the function value
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
