// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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

#include <cstdint>
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
    /// The value of a `var`'s cell: the C++ local it is, or an rt::Cell (design note 5.3).
    enum class Cell : uint8_t { None, Local, Shared } cell = Cell::None;
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
    /// The Place of this call: what a fault of the operator itself reports (bridge::Operator).
    virtual std::string place() = 0;
    /// The accessor of this operator in the generated file, `op_<identifier>`: a rule that calls
    /// the runtime's own implementation (bridge.h) names the operator with it, which is also what
    /// makes the program one that includes bridge.h and links the runtime.
    virtual std::string bridgeOperator() = 0;
    /// The C++ type of `type` ("void" when it has none).
    virtual std::string cppType(const Type *type) const = 0;
    /// The C++ member name of the field the argument `argIndex` of a store names
    /// (`:op/set_field`): a field index is one the compiler wrote, not a value of the program, so
    /// it is not among the arguments' text. Empty when that argument is not such a constant.
    virtual std::string fieldName(size_t argIndex) = 0;

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
