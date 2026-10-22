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
 * The predicates are compile-time functions: any pure function of the library, answered when the
 * declaration is instantiated. A `match type` predicate (`is_numeric::<T>()`,
 * `has_field::<T>('w')`) is the common case: its body is Camel, and it is folded over the
 * constants the condition gives. The standard predicates are written in the standard library
 * this way, not answered by the compiler.
 */

#include "builder.h"
#include "builder_support.h"

#include "camel/core/data/primary.h"
#include "camel/core/type/text.h"
#include "camel/core/type/tree.h"
#include "camel/core/type/typevar.h"

#include <format>
#include <optional>
#include <string_view>
#include <unordered_map>

#define DEBUG_LEVEL -1

using namespace std;
using namespace camel::core::data;
using namespace camel::core::error;
using namespace camel::core::type;

namespace camel::compile::gir {

namespace {

/// `$builtin::not` and `not` name the same operator; a name a module qualified is that
/// module's own.
std::string plainName(const std::string &written) {
    constexpr std::string_view builtin = "$builtin::";
    if (written.rfind(builtin, 0) == 0) {
        return written.substr(builtin.size());
    }
    return written;
}

/// A condition may call a function of the library: a `match type` predicate is one, and its
/// body is ordinary Camel. The instance's graph answers it when the result is a constant of the
/// graph (the common case: every arm of `match type` ends in `true` or `false`). A body that
/// computes the answer from a value argument (`has_field::<T>('w')` folds the field names of T)
/// leaves a graph of operators instead; the same GCT the graph is built from is then walked
/// here, over the constants the call gives. Only pure forms are walked: data, names, blocks,
/// `match type`, `static for`/`static loop`, branches and the comparison operators.
struct EvalFailure {
    std::string why;
};

struct EarlyReturn {
    data_ptr_t value = nullptr;
};

data_ptr_t makeBool(bool value) {
    return std::make_shared<PrimaryData<bool>>(value);
}

bool asBool(const data_ptr_t &data, bool &value) {
    auto primary = std::dynamic_pointer_cast<PrimaryData<bool>>(data);
    if (primary == nullptr) {
        return false;
    }
    value = primary->data();
    return true;
}

class GctConstantEvaluator {
  public:
    GctConstantEvaluator(
        const GCT::node_ptr_t &func, camel::core::type::type_bindings_t vars,
        std::vector<data_ptr_t> args)
        : func_(func), vars_(std::move(vars)) {
        FunctionType *type = tt::as_ptr<FunctionType>(func->at(0)->dataType());
        const size_t with  = type->withTypesCount();
        for (size_t i = 0; i < type->normTypesCount() && i < args.size(); ++i) {
            env_[std::string(type->argNameAt(with + i))] = std::move(args[i]);
        }
    }

    data_ptr_t run() {
        try {
            eval(func_->at(1));
        } catch (const EarlyReturn &early) {
            return early.value;
        }
        return nullptr;
    }

  private:
    Type *bound(Type *type) const {
        return camel::core::type::substituteTypeVars(type, vars_);
    }

    data_ptr_t eval(const GCT::node_ptr_t &node) {
        if (node == nullptr) {
            return nullptr;
        }
        using Kind = GCT::NodeKind;
        switch (node->type()) {
        case Kind::DATA:
            return node->data();
        case Kind::TYPE:
            return nullptr;
        case Kind::DREF: {
            const auto found = env_.find(node->name());
            if (found == env_.end()) {
                throw EvalFailure{
                    "the predicate reads '" + node->name() +
                    "', which this compile-time evaluation does not have"};
            }
            return found->second;
        }
        case Kind::NREF:
        case Kind::VARI: {
            data_ptr_t value = eval(node->size() > 0 ? node->at(0) : nullptr);
            env_[node->name()] = value;
            return value;
        }
        case Kind::EXEC: {
            data_ptr_t last = nullptr;
            for (size_t i = 0; i < node->size(); ++i) {
                last = eval(node->at(i));
            }
            return last;
        }
        case Kind::EXIT:
            throw EarlyReturn{eval(node->size() > 0 ? node->at(0) : nullptr)};
        case Kind::CAST:
            return eval(node->size() > 0 ? node->at(0) : nullptr);
        case Kind::TMTCH:
            return evalTypeMatch(node);
        case Kind::SFOR:
            return evalStaticFor(node);
        case Kind::SLOOP:
            return evalStaticLoop(node);
        case Kind::BRCH:
            return evalBranch(node);
        case Kind::CASE:
            return eval(node->size() > 0 ? node->at(0) : nullptr);
        case Kind::LINK:
            return evalCall(node);
        case Kind::TAPP:
            throw EvalFailure{
                "the predicate calls another generic function; only a direct call is evaluated at "
                "compile time"};
        default:
            throw EvalFailure{
                "the predicate uses a construct this compile-time evaluation does not read (kind " +
                std::to_string(static_cast<int>(node->type())) + ")"};
        }
    }

    data_ptr_t evalTypeMatch(const GCT::node_ptr_t &node) {
        Type *subject = bound(node->at(0)->dataType());
        uint16_t shape = 0;
        if (subject->code() == TypeCode::Struct) {
            shape = GCT::typeCaseBit(GCT::TypeCaseKind::Struct);
        } else if (subject->code() == TypeCode::Tuple && tree::isInternal(subject)) {
            shape = GCT::typeCaseBit(GCT::TypeCaseKind::Tuple);
        } else {
            // Every other type, a function included, is a leaf: `#leaf` still selects it.
            shape = GCT::typeCaseBit(GCT::TypeCaseKind::Leaf);
        }
        const bool isFunc       = subject->code() == TypeCode::Function;
        const uint16_t wildcard = GCT::typeCaseBit(GCT::TypeCaseKind::Wildcard);
        for (size_t i = 1; i < node->size(); ++i) {
            const GCT::node_ptr_t &arm = node->at(i);
            bool selected              = (arm->typeCasePatterns() & (shape | wildcard)) != 0 ||
                            (isFunc && (arm->typeCasePatterns() &
                                        GCT::typeCaseBit(GCT::TypeCaseKind::Func)) != 0);
            for (size_t k = 0; !selected && k < arm->typeCaseTypeCount(); ++k) {
                Type *pattern = bound(arm->typeCaseTypeAt(k));
                selected      = pattern != nullptr && !containsTypeVar(pattern) &&
                           typeEquals(subject, pattern);
            }
            if (selected) {
                return eval(arm->size() > 0 ? arm->at(0) : nullptr);
            }
        }
        throw EvalFailure{"no case of the predicate's 'match type' matches " + subject->toString()};
    }

    data_ptr_t evalStaticFor(const GCT::node_ptr_t &node) {
        Type *subject = bound(node->staticForOperand()->dataType());
        if (!tree::isInternal(subject)) {
            throw EvalFailure{
                "the predicate walks the fields of a type that is not a struct or a tuple"};
        }
        const std::string valueName = node->staticForValueName();
        const std::string typeName  = node->staticForTypeName();
        const auto saved            = env_;
        for (size_t i = 0; i < tree::arity(subject); ++i) {
            env_ = saved;
            env_[valueName] = std::make_shared<StringData>(tree::childName(subject, i));
            if (!typeName.empty()) {
                typeEnv_[typeName] = tree::childType(subject, i);
            }
            eval(node->staticForBody());
        }
        env_ = saved;
        return nullptr;
    }

    data_ptr_t evalStaticLoop(const GCT::node_ptr_t &node) {
        Type *subject = bound(node->staticLoopOperand()->dataType());
        if (!tree::isInternal(subject)) {
            throw EvalFailure{
                "the predicate walks the fields of a type that is not a struct or a tuple"};
        }
        const size_t count = node->staticLoopStateCount();
        std::vector<std::string> names;
        std::vector<data_ptr_t> state;
        for (size_t j = 0; j < count; ++j) {
            names.push_back(node->staticLoopStateName(j));
            state.push_back(eval(node->staticLoopInitial(j)));
        }
        const std::string valueName = node->staticLoopValueName();
        const std::string typeName  = node->staticLoopTypeName();
        const auto saved            = env_;
        for (size_t i = 0; i < tree::arity(subject); ++i) {
            env_ = saved;
            for (size_t j = 0; j < count; ++j) {
                env_[names[j]] = state[j];
            }
            env_[valueName] = std::make_shared<StringData>(tree::childName(subject, i));
            if (!typeName.empty()) {
                typeEnv_[typeName] = tree::childType(subject, i);
            }
            data_ptr_t round = eval(node->staticLoopBody());
            if (count == 1) {
                state[0] = round;
            } else {
                throw EvalFailure{"a 'static loop' with several states is not evaluated here"};
            }
        }
        env_ = saved;
        return count == 0 ? nullptr : state[0];
    }

    data_ptr_t evalBranch(const GCT::node_ptr_t &node) {
        const GCT::BranchKind kind = node->branchKind();
        if (kind == GCT::BranchKind::LogicalAnd || kind == GCT::BranchKind::LogicalOr) {
            bool left = false;
            if (!asBool(eval(node->at(0)), left)) {
                throw EvalFailure{"the left side of a logical operator is not a bool"};
            }
            if (kind == GCT::BranchKind::LogicalAnd && !left) {
                return makeBool(false);
            }
            if (kind == GCT::BranchKind::LogicalOr && left) {
                return makeBool(true);
            }
            for (size_t i = 1; i < node->size(); ++i) {
                const GCT::node_ptr_t &arm = node->at(i);
                const bool wanted = kind == GCT::BranchKind::LogicalAnd
                                        ? arm->caseType() == GCT::CaseType::True
                                        : arm->caseType() == GCT::CaseType::Else;
                if (wanted) {
                    return eval(arm->size() > 0 ? arm->at(0) : nullptr);
                }
            }
            throw EvalFailure{"a logical branch has no arm for the side taken"};
        }
        if (kind == GCT::BranchKind::Cond) {
            bool test = false;
            if (!asBool(eval(node->at(0)), test)) {
                throw EvalFailure{"the condition of an 'if' is not a bool"};
            }
            for (size_t i = 1; i < node->size(); ++i) {
                const GCT::node_ptr_t &arm = node->at(i);
                const bool wanted = test ? arm->caseType() == GCT::CaseType::True
                                         : arm->caseType() == GCT::CaseType::Else;
                if (wanted) {
                    return eval(arm->size() > 0 ? arm->at(0) : nullptr);
                }
            }
            return nullptr;
        }
        throw EvalFailure{"the predicate branches on a value this compile-time evaluation cannot compare"};
    }

    data_ptr_t evalCall(const GCT::node_ptr_t &node) {
        const GCT::node_ptr_t &callee = node->at(0);
        const std::string name = callee->type() == GCT::NodeKind::DREF ? callee->name() : "";
        std::vector<data_ptr_t> args;
        for (size_t i = 1; i < node->size(); ++i) {
            args.push_back(eval(node->at(i)));
        }
        if (name == "__eq__" || name == "__ne__") {
            if (args.size() != 2) {
                throw EvalFailure{"a comparison does not have two sides"};
            }
            const bool equal = args[0] != nullptr && args[1] != nullptr &&
                               args[0]->equals(args[1]);
            return makeBool(name == "__eq__" ? equal : !equal);
        }
        if (name == "__not__") {
            bool value = false;
            if (args.size() != 1 || !asBool(args[0], value)) {
                throw EvalFailure{"'!' is applied to something that is not a bool"};
            }
            return makeBool(!value);
        }
        throw EvalFailure{
            "the predicate calls '" + (name.empty() ? std::string("an expression") : name) +
            "', which this compile-time evaluation cannot run"};
    }

    GCT::node_ptr_t func_;
    camel::core::type::type_bindings_t vars_;
    std::unordered_map<std::string, data_ptr_t> env_;
    std::unordered_map<std::string, Type *> typeEnv_;
};

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
                if (callee->size() < 2 || callee->at(0)->type() != GCT::NodeKind::DREF) {
                    local.outcome = WhenOutcome::NotEvaluable;
                    local.detail  = "a type application in a condition names no function";
                    return local;
                }
                const std::string name = callee->at(0)->name();
                // Every predicate is a function of the library now: the standard type
                // predicates are written in Camel with `match type`, so they arrive here like
                // any other generic function and are answered by their own body.
                const template_ptr_t tmpl = templateScope_ != nullptr
                                                ? templateScope_->get(name).value_or(nullptr)
                                                : nullptr;
                if (tmpl == nullptr) {
                    local.outcome = WhenOutcome::NotEvaluable;
                    local.subject = name;
                    local.detail =
                        "no compile-time function of that name is visible here; a condition "
                        "calls a generic function this module declares";
                    return local;
                }
                std::vector<std::string> parameters = typeParameterNames(tmpl);
                std::vector<Type *> arguments;
                for (size_t i = 1; i < callee->size(); ++i) {
                    if (callee->at(i)->type() != GCT::NodeKind::TYPE) {
                        local.outcome = WhenOutcome::NotEvaluable;
                        local.detail  = "a type argument here is not a type";
                        return local;
                    }
                    arguments.push_back(substituteTypeVars(callee->at(i)->dataType(), bindings));
                }
                if (arguments.size() != parameters.size()) {
                    local.outcome = WhenOutcome::NotEvaluable;
                    local.subject = name;
                    local.detail  = std::format(
                        "the condition gives {} type argument(s) and the predicate declares {}",
                        arguments.size(),
                        parameters.size());
                    return local;
                }
                for (Type *argument : arguments) {
                    if (argument == nullptr || containsTypeVar(argument)) {
                        local.outcome = WhenOutcome::TypeNotConcrete;
                        local.subject = name;
                        local.detail  = argument != nullptr ? argument->toString() : std::string("?");
                        return local;
                    }
                }
                // A value argument has to be a constant; the instance's body may read it, and
                // only a constant is available while the declaration is being selected.
                std::vector<data_ptr_t> valueArgs;
                for (size_t i = 1; i < node->size(); ++i) {
                    const GCT::node_ptr_t &argument = node->at(i);
                    if (argument->type() != GCT::NodeKind::DATA) {
                        local.outcome = WhenOutcome::NeedsWithValue;
                        local.subject = name;
                        local.detail =
                            "the predicate is called with a value that is not a compile-time "
                            "constant";
                        return local;
                    }
                    valueArgs.push_back(argument->data());
                }
                type_bindings_t callBindings = tmpl->outerBindings;
                for (size_t i = 0; i < parameters.size(); ++i) {
                    callBindings.emplace_back(parameters[i], arguments[i]);
                }
                compile_graph_ptr_t graph =
                    instantiateGeneric(tmpl, callBindings, {}, {}, callee);
                if (graph == nullptr || graph->draft().returnNode() ==
                                             camel::runtime::kInvalidNodeRef) {
                    local.outcome = WhenOutcome::NotEvaluable;
                    local.subject = name;
                    local.detail  = "the predicate's instance has no result";
                    return local;
                }
                node_handle_t returned = graph->draft().ref(graph->draft().returnNode());
                const data_ptr_t result = constantOfNode(returned);
                std::optional<bool> value;
                if (result != nullptr && boolOf(result, value)) {
                    local.outcome = *value ? WhenOutcome::Holds : WhenOutcome::Fails;
                    return local;
                }
                // The instance's result is computed rather than written (`has_field` folds the
                // field names of T against the argument). The same GCT the graph came from is
                // walked over the constants the call gave; a body this evaluation cannot read
                // says why, and the declaration stays inapplicable rather than becoming false.
                try {
                    GctConstantEvaluator evaluator(tmpl->func, callBindings, valueArgs);
                    data_ptr_t folded = evaluator.run();
                    std::optional<bool> foldedValue;
                    if (folded != nullptr && boolOf(folded, foldedValue)) {
                        local.outcome = *foldedValue ? WhenOutcome::Holds : WhenOutcome::Fails;
                        return local;
                    }
                } catch (const EvalFailure &failure) {
                    local.outcome = WhenOutcome::NotEvaluable;
                    local.subject = name;
                    local.detail  = failure.why;
                    return local;
                }
                local.outcome = WhenOutcome::NotEvaluable;
                local.subject = name;
                local.detail =
                    "the predicate's result is not a compile-time constant; a condition can "
                    "call a function only where its body reduces to one over the types";
                return local;
            }
            if (callee->type() == GCT::NodeKind::DREF) {
                const std::string op = plainName(callee->name());
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
            const std::string plain = plainName(node->name());
            if (node->name().find("::") == std::string::npos) {
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