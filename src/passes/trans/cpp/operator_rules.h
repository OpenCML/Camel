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
 * How each operator is written in the generated C++ (docs/design/cpp-standalone/README.cn.md,
 * section 8): an inline C++ expression, a function of the standalone runtime, or (outside this
 * table) a call through the runtime module that implements it.
 *
 * Every rule does what the runtime's operator of the same URI does, including where that is a
 * fault; the comment of each group names the runtime function it follows.
 */

#pragma once

#include "camel/core/error/diagnostics/messages/runtime.h"
#include "camel/core/type/base.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace camel::passes::cpp {

using camel::core::type::Type;

/// A value of the generated code: its C++ expression, and whether the expression is a bare
/// numeric literal (whose C++ type is `int`, not the type of the Camel value).
struct CppValue {
    std::string text;
    bool literal = false;
};

/// One application of an operator, as a rule sees it.
class OperatorCall {
  public:
    virtual ~OperatorCall() = default;

    virtual std::string_view uri() const                = 0;
    virtual size_t normCount() const                    = 0;
    virtual size_t withCount() const                    = 0;
    virtual const CppValue &norm(size_t i) const        = 0;
    virtual const CppValue &with(size_t i) const        = 0;
    virtual const Type *normType(size_t i) const        = 0;
    virtual const Type *withType(size_t i) const        = 0;
    virtual const Type *resultType() const              = 0;
    /// The name of the Site for a fault of `kind` at this operation; `args` as for
    /// SiteTable::siteFor.
    virtual std::string site(
        camel::core::error::RuntimeDiag kind, const std::vector<std::string> &args) = 0;

    /// The C++ text of argument `i` to pass to an overloaded runtime function: a bare literal
    /// is written with its type when every argument is one (camel/standalone/num.h).
    std::string operand(size_t i) const;
};

struct OperatorRule {
    /// The C++ expression of the call, or nothing when the argument or result types are not
    /// translated by this rule.
    std::function<std::optional<std::string>(OperatorCall &)> emit;
    /// Its result depends on its arguments only, it has no effect and cannot fault: the
    /// expression may be moved to its one use (and need not be a statement of its own).
    bool inlinable = false;
};

/// The rule for the operator `uri`, or null when it has no standalone translation.
const OperatorRule *findOperatorRule(std::string_view uri);

} // namespace camel::passes::cpp
