// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The applicability condition of a declaration (design principles 5.13; handover H234).
 *
 * `func f(...): T when p { ... }` is selected only where `p` holds. The condition is a
 * compile-time predicate over the declaration's type parameters and its `with` parameters; it
 * is evaluated when the declaration is instantiated or selected, never when it is parsed, and
 * a false condition makes the declaration inapplicable rather than erroneous. A condition the
 * compiler cannot answer is reported, never taken for `true`.
 *
 * The predicates are the compiler's own type queries (`is_numeric::<T>()` and the others),
 * not functions of the library: the standard library is compiled by the same compiler, and a
 * query has to be answered before a body can be built for the type it asks about.
 */

#include "builder.h"
#include "builder_support.h"

#include "camel/core/data/primary.h"
#include "camel/core/type/typevar.h"

#include <optional>
#include <string_view>

#define DEBUG_LEVEL -1

using namespace std;
using namespace camel::core::data;
using namespace camel::core::error;
using namespace camel::core::type;

namespace camel::compile::gir {

namespace {

/// `$builtin::is_numeric` and `is_numeric` are the same query; a name a module qualified
/// (`m::is_numeric`) is that module's own function, not a query.
std::string queryName(const std::string &written) {
    constexpr std::string_view builtin = "$builtin::";
    if (written.rfind(builtin, 0) == 0) {
        return written.substr(builtin.size());
    }
    return written.find("::") == std::string::npos ? written : std::string{};
}

bool answersTypeQuery(const std::string &name, Type *type) {
    switch (type->code()) {
    case TypeCode::Int32:
    case TypeCode::Int64:
        return name == "is_integer" || name == "is_numeric" || name == "is_ordered";
    case TypeCode::Float32:
    case TypeCode::Float64:
        return name == "is_float" || name == "is_numeric" || name == "is_ordered";
    case TypeCode::Complex64:
    case TypeCode::Complex128:
        return name == "is_numeric";
    case TypeCode::String:
        return name == "is_string" || name == "is_ordered";
    case TypeCode::Bool:
        return name == "is_bool" || name == "is_ordered";
    case TypeCode::Function:
        return name == "is_function";
    default:
        return false;
    }
}

bool isTypeQuery(const std::string &name) {
    return name == "is_numeric" || name == "is_integer" || name == "is_float" ||
           name == "is_ordered" || name == "is_function" || name == "is_string" ||
           name == "is_bool";
}

} // namespace

WhenResult Builder::evaluateWhenCondition(
    const GCT::node_ptr_t &condition, FunctionType *signature,
    const type_bindings_t &bindings,
    const std::unordered_map<std::string, data_ptr_t> &withValues) {
    WhenResult result;
    if (condition == nullptr) {
        result.outcome = WhenOutcome::NotEvaluable;
        result.detail  = "the declaration has no condition";
        return result;
    }

    // The condition's arms are `EXEC` blocks; a compile-time expression has exactly one value.
    const auto soleValue = [](const GCT::node_ptr_t &node) -> GCT::node_ptr_t {
        if (node == nullptr) {
            return nullptr;
        }
        if (node->type() == GCT::NodeKind::EXEC) {
            return node->size() == 1 ? node->at(0) : nullptr;
        }
        return node;
    };

    const auto boolOf = [](const data_ptr_t &data, std::optional<bool> &value) {
        if (auto flag = std::dynamic_pointer_cast<PrimaryData<bool>>(data)) {
            value = flag->data();
            return true;
        }
        return false;
    };

    std::function<WhenResult(const GCT::node_ptr_t &)> eval =
        [&](const GCT::node_ptr_t &node) -> WhenResult {
        WhenResult local;
        if (node == nullptr) {
            local.outcome = WhenOutcome::NotEvaluable;
            local.detail  = "the condition has no value here";
            return local;
        }
        switch (node->type()) {
        case GCT::NodeKind::DATA: {
            std::optional<bool> value;
            const data_ptr_t &data = node->data();
            if (data != nullptr && boolOf(data, value)) {
                local.outcome = *value ? WhenOutcome::Holds : WhenOutcome::Fails;
                return local;
            }
            local.outcome = WhenOutcome::NotBool;
            local.subject = data != nullptr && data->type() != nullptr
                                ? data->type()->toString()
                                : std::string("a value without a type");
            local.detail = "the condition is not a bool";
            return local;
        }
        case GCT::NodeKind::EXEC:
            if (node->size() == 1) {
                return eval(node->at(0));
            }
            local.outcome = WhenOutcome::NotEvaluable;
            local.detail  = "the condition is more than one expression";
            return local;
        case GCT::NodeKind::NREF:
            return node->size() == 1 ? eval(node->at(0)) : local;
        case GCT::NodeKind::CAST:
            return eval(node->at(0));
        case GCT::NodeKind::BRCH: {
            const GCT::node_ptr_t &test = node->at(0);
            const GCT::BranchKind kind   = node->branchKind();
            if (kind == GCT::BranchKind::LogicalAnd ||
                kind == GCT::BranchKind::LogicalOr) {
                WhenResult left = eval(test);
                if (!left.holds() && !left.fails()) {
                    return left;
                }
                const bool decides = kind == GCT::BranchKind::LogicalOr ? left.holds()
                                                                        : left.fails();
                if (decides) {
                    return left;
                }
                for (size_t i = 1; i < node->size(); ++i) {
                    const GCT::node_ptr_t &arm = node->at(i);
                    if (arm->type() != GCT::NodeKind::CASE) {
                        continue;
                    }
                    const bool wanted = kind == GCT::BranchKind::LogicalAnd
                                            ? arm->caseType() == GCT::CaseType::True
                                            : arm->caseType() == GCT::CaseType::Else;
                    if (wanted && arm->size() == 1) {
                        return eval(soleValue(arm->at(0)));
                    }
                }
                local.outcome = WhenOutcome::NotEvaluable;
                local.detail  = "the condition's second operand is not one expression";
                return local;
            }
            if (kind == GCT::BranchKind::Cond) {
                WhenResult testResult = eval(test);
                if (!testResult.holds() && !testResult.fails()) {
                    return testResult;
                }
                for (size_t i = 1; i < node->size(); ++i) {
                    const GCT::node_ptr_t &arm = node->at(i);
                    if (arm->type() != GCT::NodeKind::CASE) {
                        continue;
                    }
                    const bool wanted = testResult.holds()
                                            ? arm->caseType() == GCT::CaseType::True
                                            : arm->caseType() == GCT::CaseType::Else;
                    if (wanted && arm->size() == 1) {
                        return eval(soleValue(arm->at(0)));
                    }
                }
            }
            local.outcome = WhenOutcome::NotEvaluable;
            local.detail  = "the condition is not a compile-time bool expression";
            return local;
        }
        case GCT::NodeKind::LINK: {
            const GCT::node_ptr_t &callee = node->at(0);
            // `is_numeric::<T>()`: the type application is the callee; there are no runtime
            // arguments.
            if (callee->type() == GCT::NodeKind::TAPP) {
                if (node->size() != 1) {
                    local.outcome = WhenOutcome::NotEvaluable;
                    local.detail  = "a type query takes no value arguments";
                    return local;
                }
                if (callee->size() != 2 || callee->at(0)->type() != GCT::NodeKind::DREF ||
                    callee->at(1)->type() != GCT::NodeKind::TYPE) {
                    local.outcome = WhenOutcome::NotEvaluable;
                    local.detail  = "a type query takes one type argument";
                    return local;
                }
                const std::string name = queryName(callee->at(0)->name());
                if (!isTypeQuery(name)) {
                    local.outcome = WhenOutcome::NotEvaluable;
                    local.subject = callee->at(0)->name();
                    local.detail  = "only the compiler's type queries can be evaluated here";
                    return local;
                }
                Type *asked = substituteTypeVars(callee->at(1)->dataType(), bindings);
                if (asked == nullptr || containsTypeVar(asked)) {
                    local.outcome = WhenOutcome::TypeNotConcrete;
                    local.subject = name;
                    local.detail  = asked != nullptr ? asked->toString() : std::string("?");
                    return local;
                }
                local.outcome = answersTypeQuery(name, asked) ? WhenOutcome::Holds
                                                              : WhenOutcome::Fails;
                return local;
            }
            if (callee->type() == GCT::NodeKind::DREF) {
                const std::string op = queryName(callee->name());
                const auto argument = [&](size_t index) -> GCT::node_ptr_t {
                    return node->size() > index + 1 ? soleValue(node->at(index + 1)) : nullptr;
                };
                if (op == "__not__" && node->size() == 2) {
                    WhenResult inner = eval(argument(0));
                    if (!inner.holds() && !inner.fails()) {
                        return inner;
                    }
                    inner.outcome = inner.holds() ? WhenOutcome::Fails : WhenOutcome::Holds;
                    return inner;
                }
                if ((op == "__and__" || op == "__or__") && node->size() == 3) {
                    WhenResult left  = eval(argument(0));
                    WhenResult right = eval(argument(1));
                    if (!left.holds() && !left.fails()) {
                        return left;
                    }
                    if (!right.holds() && !right.fails()) {
                        return right;
                    }
                    const bool value = op == "__and__" ? left.holds() && right.holds()
                                                       : left.holds() || right.holds();
                    local.outcome = value ? WhenOutcome::Holds : WhenOutcome::Fails;
                    return local;
                }
                local.outcome = WhenOutcome::NotEvaluable;
                local.subject = callee->name();
                local.detail  = "only '!', '&&' and '||' can combine compile-time values";
                return local;
            }
            local.outcome = WhenOutcome::NotEvaluable;
            local.detail  = "the condition calls something that is not a compile-time query";
            return local;
        }
        case GCT::NodeKind::DREF: {
            const std::string plain = queryName(node->name());
            if (!isTypeQuery(plain) && node->name().find("::") == std::string::npos) {
                // A `with` parameter is in scope; its value is a compile-time constant only
                // when the call wrote one.
                if (signature != nullptr) {
                    const size_t withCount = signature->withTypesCount();
                    for (size_t i = 0; i < withCount; ++i) {
                        if (plain != signature->argNameAt(i)) {
                            continue;
                        }
                        const auto found = withValues.find(plain);
                        if (found == withValues.end()) {
                            local.outcome = WhenOutcome::NeedsWithValue;
                            local.subject = plain;
                            local.detail  =
                                "the call did not give a compile-time value for this "
                                "'with' parameter";
                            return local;
                        }
                        std::optional<bool> value;
                        if (found->second != nullptr && boolOf(found->second, value)) {
                            local.outcome =
                                *value ? WhenOutcome::Holds : WhenOutcome::Fails;
                            return local;
                        }
                        local.outcome = WhenOutcome::NotBool;
                        local.subject = plain;
                        local.detail  = "the 'with' parameter is not a bool";
                        return local;
                    }
                }
            }
            local.outcome = WhenOutcome::UnknownName;
            local.subject = node->name();
            local.detail =
                "a condition sees the declaration's type parameters and 'with' parameters, "
                "not its ordinary parameters or body";
            return local;
        }
        default:
            local.outcome = WhenOutcome::NotEvaluable;
            local.detail  = "the condition is not computed at compile time";
            return local;
        }
    };

    return eval(soleValue(condition));
}

void Builder::reportWhenProblem(
    const WhenResult &result, const std::string &name, const GCT::node_ptr_t &origin) {
    const auto at = origin != nullptr ? origin->origin() : camel::source::kInvalidOriginId;
    switch (result.outcome) {
    case WhenOutcome::NotBool:
        diags_->of(SemanticDiag::WhenConditionNotBool)
            .atOrigin(at)
            .commit(name, result.subject);
        break;
    case WhenOutcome::UnknownName:
        diags_->of(SemanticDiag::WhenUnknownName)
            .atOrigin(at)
            .commit(result.subject, name);
        break;
    case WhenOutcome::TypeNotConcrete:
        diags_->of(SemanticDiag::WhenTypeQueryNotConcrete)
            .atOrigin(at)
            .commit(result.subject, result.detail, name);
        break;
    case WhenOutcome::NeedsWithValue:
    case WhenOutcome::NotEvaluable:
        diags_->of(SemanticDiag::WhenConditionNotConstant)
            .atOrigin(at)
            .commit(name, result.detail);
        break;
    case WhenOutcome::Fails:
    case WhenOutcome::Holds:
        ASSERT(false, "reportWhenProblem is for a condition that does not hold");
        break;
    }
    throw BuildAbortException();
}

} // namespace camel::compile::gir