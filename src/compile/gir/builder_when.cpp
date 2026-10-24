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
 *
 * A condition is written in a declaration, and it resolves names where that declaration is
 * written, as a call in its body would (design principles 10.2): the module's own generic
 * functions first, then the ones its imports bring by name, selectively or as a module prefix,
 * then the prelude. A predicate in another module therefore works exactly as a local one, and
 * the module a call in another module selects it from does not change what its condition means.
 */

#include "builder.h"
#include "builder_support.h"

#include "camel/compile/literal.h"
#include "camel/core/data/composite/tuple.h"
#include "camel/core/data/primary.h"
#include "camel/core/type/text.h"
#include "camel/core/type/tree.h"
#include "camel/core/type/typevar.h"
#include "core/module/entity_internal.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <functional>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#define DEBUG_LEVEL -1

using namespace std;
using namespace camel::core::data;
using namespace camel::core::error;
using namespace camel::core::type;

namespace camel::compile::gir {

using camel::core::module::ImportLayer;

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

/// The name a module shows itself by (design principles 10.2: the name it declares, or else
/// the name it was loaded under).
std::string shownModuleName(const camel::core::module::Module &module) {
    return module.declaredName().empty() ? module.name() : module.declaredName();
}

/// A condition may call a function of the library: a `match type` predicate is one, and its
/// body is ordinary Camel. The instance's graph answers it when the result is a constant of the
/// graph (the common case: every arm of `match type` ends in `true` or `false`). A body that
/// computes the answer from a value argument (`has_field::<T>('w')` folds the field names of T)
/// leaves a graph of operators instead; the same GCT the graph is built from is then walked
/// here, over the constants the call gives. Only pure forms are walked: data, names, blocks,
/// `match type`, `static for`/`static loop`, branches, static access, the comparison and
/// arithmetic operators, and calls of other pure functions whose arguments are constants.

/// Why a call in a place that needs a compile-time constant did not become one. The evaluator
/// reports a refusal instead of guessing a value; the caller decides whether a refusal is a
/// diagnostic (a constant position) or a fallback to the ordinary build (a value position).
enum class EvalProblem {
    ArgumentNotConstant,
    Effects,
    TooDeep,
    ResultNotConstantKind,
    Construct,
};

struct EvalFailure {
    EvalProblem kind = EvalProblem::Construct;
    /// The call the refusal is about, when it is a nested one; empty for the call being folded.
    std::string call;
    std::string why;
};

struct EarlyReturn {
    data_ptr_t value = nullptr;
};

/// The field names and values of a comprehension built at build time, so that `.[key]` on it
/// can be answered. A comprehension itself is not a constant (a composite is not a kind a
/// constant value has), but the access that reads one element of it is.
struct EvalComposite {
    std::vector<std::string> names;
    std::vector<data_ptr_t> values;
};

using EvalDataResolver = std::function<data_ptr_t(const std::string &)>;
using EvalCallResolver  = std::function<data_ptr_t(
    const GCT::node_ptr_t &call, const GCT::node_ptr_t &callee,
    const std::vector<data_ptr_t> &arguments,
    const std::function<data_ptr_t(const GCT::node_ptr_t &)> &evalArgument,
    const camel::core::type::type_bindings_t &vars, size_t depth)>;
/// How the evaluator works out the sequence of a `static loop` whose head writes one out
/// (`for i in 0..5`, H462, the ruling of 2026-10-24). It is the same reading the graph builder
/// uses (`Builder::staticLoopIntSequenceOf`, the `int` half of `Builder::staticLoopSequenceOf`,
/// which has no room for a type or for another element type), passed in as a callback because
/// the evaluator is a helper that holds no builder: a `when` predicate and the expansion must
/// agree on the elements a head names, and a sequence the compiler cannot work out comes back
/// as nothing.
using EvalSequenceResolver =
    std::function<std::optional<std::vector<int64_t>>(const GCT::node_ptr_t &)>;

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

/// Whether a value of this type can be a compile-time constant at all: a number, a bool or a
/// string. A composite, a function or a handle cannot (nothing would hold its identity).
bool isConstantKind(Type *type) {
    if (type == nullptr) {
        return false;
    }
    switch (type->code()) {
    case TypeCode::Int32:
    case TypeCode::Int64:
    case TypeCode::Float32:
    case TypeCode::Float64:
    case TypeCode::Bool:
    case TypeCode::String:
        return true;
    default:
        return false;
    }
}

std::optional<literal::Value> literalOf(const data_ptr_t &data) {
    if (data == nullptr || data->type() == nullptr) {
        return std::nullopt;
    }
    switch (data->type()->code()) {
    case TypeCode::Int32:
        return literal::Value::fromInt64(
            std::dynamic_pointer_cast<PrimaryData<int32_t>>(data)->data());
    case TypeCode::Int64:
        return literal::Value::fromInt64(
            std::dynamic_pointer_cast<PrimaryData<int64_t>>(data)->data());
    case TypeCode::Float32:
        return literal::Value::fromDouble(
            static_cast<double>(std::dynamic_pointer_cast<PrimaryData<float>>(data)->data()));
    case TypeCode::Float64:
        return literal::Value::fromDouble(
            std::dynamic_pointer_cast<PrimaryData<double>>(data)->data());
    default:
        return std::nullopt;
    }
}

std::optional<std::string> stringOf(const data_ptr_t &data) {
    if (data == nullptr || data->type() == nullptr || data->type()->code() != TypeCode::String) {
        return std::nullopt;
    }
    return std::dynamic_pointer_cast<StringData>(data)->data();
}

bool isArithmeticOperator(const std::string &name) {
    return name == "__add__" || name == "__sub__" || name == "__mul__" || name == "__div__" ||
           name == "__mod__" || name == "__pow__" || name == "__neg__";
}

bool isOrderingOperator(const std::string &name) {
    return name == "__lt__" || name == "__le__" || name == "__gt__" || name == "__ge__";
}

/// The operators the evaluator computes itself: a comparison, a logical operator, arithmetic.
bool isBuiltinOperatorName(const std::string &name) {
    return name == "__eq__" || name == "__ne__" || name == "__neq__" || name == "__not__" ||
           name == "__and__" || name == "__or__" || isOrderingOperator(name) ||
           isArithmeticOperator(name);
}

class GctConstantEvaluator {
  public:
    GctConstantEvaluator(GCT::node_ptr_t expression, camel::core::type::type_bindings_t vars)
        : expression_(std::move(expression)), vars_(std::move(vars)) {}

    /// Evaluates the body of `func` with its parameters bound: `args` are the ordinary
    /// parameters (`constas` values included, in written order), `withArgs` the `with` ones.
    static data_ptr_t runFunction(
        const GCT::node_ptr_t &func, const camel::core::type::type_bindings_t &vars,
        const std::vector<data_ptr_t> &args, const std::vector<data_ptr_t> &withArgs,
        EvalDataResolver dataResolver, EvalCallResolver callResolver,
        EvalSequenceResolver sequenceResolver, size_t depth) {
        GctConstantEvaluator evaluator(func->at(1), vars);
        evaluator.dataResolver_     = std::move(dataResolver);
        evaluator.callResolver_     = std::move(callResolver);
        evaluator.sequenceResolver_ = std::move(sequenceResolver);
        evaluator.depth_            = depth;
        FunctionType *type       = tt::as_ptr<FunctionType>(func->at(0)->dataType());
        const size_t withCount   = type->withTypesCount();
        for (size_t i = 0; i < withCount && i < withArgs.size(); ++i) {
            if (withArgs[i] != nullptr) {
                evaluator.env_[std::string(type->argNameAt(i))] = withArgs[i];
            }
        }
        for (size_t i = 0; i < type->normTypesCount() && i < args.size(); ++i) {
            if (args[i] != nullptr) {
                evaluator.env_[std::string(type->argNameAt(withCount + i))] = args[i];
            }
        }
        return evaluator.run();
    }

    /// Evaluates one expression: the argument of a call being folded, or a `when` condition's
    /// value.
    static data_ptr_t runExpression(
        const GCT::node_ptr_t &expression, const camel::core::type::type_bindings_t &vars,
        EvalDataResolver dataResolver, EvalCallResolver callResolver,
        EvalSequenceResolver sequenceResolver, size_t depth) {
        GctConstantEvaluator evaluator(expression, vars);
        evaluator.dataResolver_     = std::move(dataResolver);
        evaluator.callResolver_     = std::move(callResolver);
        evaluator.sequenceResolver_ = std::move(sequenceResolver);
        evaluator.depth_            = depth;
        return evaluator.run();
    }

    /// The value of a built-in operator over constants (an arithmetic operator, a comparison, a
    /// logical operator): the same computation a body's operator expression gets.
    static data_ptr_t runBuiltinCall(
        const std::string &name, const std::vector<data_ptr_t> &arguments) {
        GctConstantEvaluator evaluator(nullptr, {});
        return evaluator.evalBuiltinCall(name, arguments);
    }

    data_ptr_t run() {
        data_ptr_t last = nullptr;
        try {
            last = eval(expression_);
        } catch (const EarlyReturn &early) {
            return early.value;
        }
        return last;
    }

  private:
    [[noreturn]] static void fail(EvalProblem kind, std::string call, std::string why) {
        throw EvalFailure{kind, std::move(call), std::move(why)};
    }

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
            if (found != env_.end()) {
                return found->second;
            }
            if (dataResolver_ != nullptr) {
                if (data_ptr_t data = dataResolver_(node->name())) {
                    return data;
                }
            }
            fail(
                EvalProblem::Effects, node->name(),
                "it reads '" + node->name() + "', which is not a compile-time constant");
        }
        case Kind::NREF: {
            data_ptr_t value = eval(node->size() > 0 ? node->at(0) : nullptr);
            env_[node->name()] = value;
            return value;
        }
        case Kind::VARI:
            // A `var` declaration is its initializer: the node carries no name (the name is a
            // mutable cell, not a value). A read of it is a DREF, which the case above refuses
            // as not a compile-time constant, so a body that reads one is not folded.
            return eval(node->size() > 0 ? node->at(0) : nullptr);
        case Kind::EXEC: {
            // A block (`{ ... }`) is an EXEC nested in a body: its `return` gives the block's
            // value and the statements after it still run. Only a `return` in the body's own
            // EXEC leaves the function.
            ++execDepth_;
            data_ptr_t last = nullptr;
            try {
                for (size_t i = 0; i < node->size(); ++i) {
                    last = eval(node->at(i));
                }
            } catch (const EarlyReturn &early) {
                --execDepth_;
                if (execDepth_ == 0) {
                    throw;
                }
                return early.value;
            }
            --execDepth_;
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
        case Kind::SACC:
            return evalStaticAccess(node);
        case Kind::SCOMP:
            return evalComprehension(node);
        case Kind::LINK:
            return evalCall(node);
        case Kind::TAPP:
            fail(
                EvalProblem::Construct, {},
                "it applies type arguments to a function outside a call");
        default:
            fail(
                EvalProblem::Construct, {},
                "it uses a construct this build-time evaluation does not read (kind " +
                    std::to_string(static_cast<int>(node->type())) + ")");
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
        fail(
            EvalProblem::Construct, {},
            "no case of its 'match type' matches " + subject->toString());
    }

    /// Visits each round of a `static for`/`static loop`: the field name and type for `fields T`,
    /// the position and the element's type for `indices ts`, and the element itself for a
    /// sequence the head writes out (`for i in 0..5`, H462). Does not touch the environment.
    /// `round` is called with `(name, index, type, element)`, where `element` carries a value
    /// only for a written sequence: it is the one round value that is not a position or a name.
    void walkRounds(
        const GCT::node_ptr_t &operand, bool byIndices, bool bySequence,
        const std::function<
            void(const std::string &name, size_t index, Type *type, const int64_t *element)>
            &round) {
        if (bySequence) {
            const std::optional<std::vector<int64_t>> elements =
                sequenceResolver_ ? sequenceResolver_(operand) : std::nullopt;
            if (!elements.has_value()) {
                fail(
                    EvalProblem::Construct, {},
                    "the sequence of its 'static loop' is not known when it is compiled");
            }
            for (size_t i = 0; i < elements->size(); ++i) {
                const int64_t element = (*elements)[i];
                round(std::to_string(element), i, Type::Int64(), &element);
            }
            return;
        }
        if (byIndices) {
            const data_ptr_t data = eval(operand);
            const auto tuple      = std::dynamic_pointer_cast<TupleData>(data);
            if (tuple == nullptr) {
                fail(
                    EvalProblem::Construct, {},
                    "it walks 'indices' of a value that is not a tuple");
            }
            for (size_t i = 0; i < tuple->size(); ++i) {
                const data_ptr_t element = tuple->get(i);
                round(
                    std::to_string(i), i, element != nullptr ? element->type() : nullptr, nullptr);
            }
            return;
        }
        Type *subject = bound(operand->dataType());
        if (!tree::isInternal(subject)) {
            fail(
                EvalProblem::Construct, {},
                "it walks the fields of a type that is not a struct or a tuple");
        }
        for (size_t i = 0; i < tree::arity(subject); ++i) {
            round(
                std::string(tree::childName(subject, i)),
                i,
                tree::childType(subject, i),
                nullptr);
        }
    }

    /// Binds the walked entry in the body: its index as an `int` for `indices`, its field name
    /// as a string for `fields`, and the element itself as an `int` for a written sequence.
    void bindRoundValue(
        const std::string &valueName, const std::string &name, size_t index, bool byIndices,
        const int64_t *element = nullptr) {
        if (element != nullptr) {
            env_[valueName] = data_ptr_t(std::make_shared<PrimaryData<int64_t>>(*element));
            return;
        }
        env_[valueName] = byIndices
                              ? data_ptr_t(std::make_shared<PrimaryData<int64_t>>(
                                    static_cast<int64_t>(index)))
                              : data_ptr_t(std::make_shared<StringData>(name));
    }

    data_ptr_t evalStaticFor(const GCT::node_ptr_t &node) {
        const std::string valueName = node->staticForValueName();
        const std::string typeName  = node->staticForTypeName();
        const bool byIndices        = node->staticForIndices();
        const auto saved            = env_;
        const auto savedTypes       = typeEnv_;
        walkRounds(
            node->staticForOperand(), byIndices, false,
            [&](const std::string &name, size_t index, Type *type, const int64_t *) {
                env_ = saved;
                bindRoundValue(valueName, name, index, byIndices);
                if (!typeName.empty() && type != nullptr) {
                    typeEnv_[typeName] = type;
                }
                eval(node->staticForBody());
            });
        env_     = saved;
        typeEnv_ = savedTypes;
        return nullptr;
    }

    data_ptr_t evalStaticLoop(const GCT::node_ptr_t &node) {
        if (node->staticLoopStateCount() != 1) {
            fail(
                EvalProblem::Construct, {},
                "a 'static loop' with several states is not evaluated here");
        }
        const std::string valueName = node->staticLoopValueName();
        const std::string typeName  = node->staticLoopTypeName();
        const bool byIndices        = node->staticLoopIndices();
        const bool bySequence       = node->staticLoopSequence();
        const auto saved            = env_;
        const auto savedTypes       = typeEnv_;
        data_ptr_t state            = eval(node->staticLoopInitial(0));
        walkRounds(
            node->staticLoopOperand(), byIndices, bySequence,
            [&](const std::string &name, size_t index, Type *type, const int64_t *element) {
                env_ = saved;
                env_[node->staticLoopStateName(0)] = state;
                bindRoundValue(valueName, name, index, byIndices, element);
                if (!typeName.empty() && type != nullptr) {
                    typeEnv_[typeName] = type;
                }
                state = eval(node->staticLoopBody());
            });
        env_     = saved;
        typeEnv_ = savedTypes;
        return state;
    }

    data_ptr_t evalBranch(const GCT::node_ptr_t &node) {
        const GCT::BranchKind kind = node->branchKind();
        if (kind == GCT::BranchKind::LogicalAnd || kind == GCT::BranchKind::LogicalOr) {
            bool left = false;
            if (!asBool(eval(node->at(0)), left)) {
                throw EvalFailure{
                    EvalProblem::Effects, {}, "the left side of a logical operator is not a bool"};
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
            throw EvalFailure{EvalProblem::Construct, {}, "a logical branch has no arm for the side taken"};
        }
        if (kind == GCT::BranchKind::Cond) {
            bool test = false;
            if (!asBool(eval(node->at(0)), test)) {
                fail(EvalProblem::Construct, {}, "the condition of an 'if' is not a bool");
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
        fail(
            EvalProblem::Construct, {},
            "it branches on a value this build-time evaluation cannot compare");
    }

    data_ptr_t evalBuiltinCall(const std::string &name, const std::vector<data_ptr_t> &args) {
        if (name == "__eq__" || name == "__ne__" || name == "__neq__") {
            if (args.size() != 2) {
                fail(EvalProblem::Construct, name, "a comparison does not have two sides");
            }
            const bool equal =
                args[0] != nullptr && args[1] != nullptr && args[0]->equals(args[1]);
            return makeBool(name == "__eq__" ? equal : !equal);
        }
        if (name == "__not__") {
            bool value = false;
            if (args.size() != 1 || !asBool(args[0], value)) {
                fail(
                    EvalProblem::Construct, name,
                    "'!' is applied to something that is not a bool");
            }
            return makeBool(!value);
        }
        if (name == "__and__" || name == "__or__") {
            bool left = false, right = false;
            if (args.size() != 2 || !asBool(args[0], left) || !asBool(args[1], right)) {
                fail(EvalProblem::Construct, name, "a logical operator is not applied to bools");
            }
            return makeBool(name == "__and__" ? left && right : left || right);
        }
        if (isOrderingOperator(name)) {
            if (args.size() != 2) {
                fail(EvalProblem::Construct, name, "a comparison does not have two sides");
            }
            int order = 0;
            const auto left  = literalOf(args[0]);
            const auto right = literalOf(args[1]);
            if (left.has_value() && right.has_value()) {
                order = left->compare(*right);
            } else {
                const auto leftText  = stringOf(args[0]);
                const auto rightText = stringOf(args[1]);
                if (!leftText.has_value() || !rightText.has_value()) {
                    fail(
                        EvalProblem::Construct, name,
                        "its operands are not values this evaluation can compare");
                }
                order = *leftText < *rightText ? -1 : (*leftText > *rightText ? 1 : 0);
            }
            const bool value = name == "__lt__" ? order < 0
                               : name == "__le__" ? order <= 0
                               : name == "__gt__" ? order > 0
                                                  : order >= 0;
            return makeBool(value);
        }
        if (name == "__add__") {
            const auto left  = stringOf(args[0]);
            const auto right = stringOf(args[1]);
            if (left.has_value() && right.has_value()) {
                return std::make_shared<StringData>(*left + *right);
            }
        }
        if (isArithmeticOperator(name)) {
            const size_t wanted = name == "__neg__" ? 1 : 2;
            if (args.size() != wanted) {
                fail(EvalProblem::Construct, name, "it does not have the operands it needs");
            }
            const auto left = literalOf(args[0]);
            if (!left.has_value()) {
                fail(EvalProblem::Construct, name, "its operand is not a number");
            }
            std::optional<literal::Value> value;
            if (name == "__neg__") {
                value = literal::Value::negate(*left);
            } else {
                const auto right = literalOf(args[1]);
                if (!right.has_value()) {
                    fail(EvalProblem::Construct, name, "its operand is not a number");
                }
                if (name == "__add__") {
                    value = literal::Value::add(*left, *right);
                } else if (name == "__sub__") {
                    value = literal::Value::sub(*left, *right);
                } else if (name == "__mul__") {
                    value = literal::Value::mul(*left, *right);
                } else if (name == "__div__") {
                    value = literal::Value::div(*left, *right);
                } else if (name == "__mod__") {
                    value = literal::Value::mod(*left, *right);
                } else {
                    value = literal::Value::pow(*left, *right);
                }
            }
            if (!value.has_value()) {
                fail(
                    EvalProblem::Construct, name,
                    "the operation has no value at build time (a division by zero, a result "
                    "that runs away)");
            }
            Type *type = args[0]->type();
            if (type == nullptr) {
                fail(EvalProblem::Construct, name, "its operand has no type");
            }
            const auto data = value->toData(type);
            if (!data.has_value()) {
                fail(
                    EvalProblem::Construct, name,
                    "the result does not fit '" + type->toString() + "'");
            }
            return *data;
        }
        fail(
            EvalProblem::Construct, name,
            "it calls an operator this build-time evaluation does not read");
    }

    data_ptr_t evalCall(const GCT::node_ptr_t &node) {
        const GCT::node_ptr_t &callee = node->at(0);
        const std::string name =
            callee->type() == GCT::NodeKind::DREF ? plainName(callee->name()) : std::string{};
        std::vector<data_ptr_t> args;
        for (size_t i = 1; i < node->size(); ++i) {
            args.push_back(eval(node->at(i)));
        }
        if (!name.empty() && (name == "__eq__" || name == "__ne__" || name == "__neq__" ||
                              name == "__not__" || name == "__and__" || name == "__or__" ||
                              isOrderingOperator(name) || isArithmeticOperator(name))) {
            return evalBuiltinCall(name, args);
        }
        if (callResolver_ != nullptr) {
            return callResolver_(
                node, callee, args,
                [this](const GCT::node_ptr_t &expression) { return eval(expression); }, vars_,
                depth_);
        }
        fail(
            EvalProblem::Construct, name,
            "it calls '" + (name.empty() ? std::string("an expression") : name) +
                "', which this build-time evaluation cannot read");
    }

    /// `.[key]` on a comprehension this evaluation built: the element at the field name (a
    /// struct) or the position (a tuple). The comprehension itself was not a constant; the
    /// element it selects is.
    data_ptr_t evalStaticAccess(const GCT::node_ptr_t &node) {
        const GCT::node_ptr_t &target = node->staticAccessTarget();
        const data_ptr_t key          = eval(node->staticAccessKey());
        const EvalComposite *built    = nullptr;
        data_ptr_t targetData         = nullptr;
        if (target != nullptr && target->type() == GCT::NodeKind::SCOMP) {
            eval(target);
            const auto found = composites_.find(target);
            if (found == composites_.end()) {
                fail(
                    EvalProblem::Construct, {},
                    "it reads an element of a comprehension that has no value here");
            }
            built = &found->second;
        } else {
            targetData = eval(target);
        }
        if (key == nullptr || key->type() == nullptr) {
            fail(EvalProblem::Construct, {}, "the key of the access is not a constant");
        }
        std::optional<size_t> index;
        if (key->type()->code() == TypeCode::String) {
            const std::string &text = std::dynamic_pointer_cast<StringData>(key)->data();
            if (built != nullptr) {
                for (size_t i = 0; i < built->names.size(); ++i) {
                    if (built->names[i] == text) {
                        index = i;
                        break;
                    }
                }
            }
            if (!index.has_value()) {
                uint32_t parsedIndex = 0;
                const char *begin    = text.data();
                const char *end      = begin + text.size();
                const auto parsed    = std::from_chars(begin, end, parsedIndex);
                if (parsed.ec == std::errc() && parsed.ptr == end) {
                    index = parsedIndex;
                }
            }
        } else if (key->type()->code() == TypeCode::Int64) {
            index = static_cast<size_t>(
                std::dynamic_pointer_cast<PrimaryData<int64_t>>(key)->data());
        } else if (key->type()->code() == TypeCode::Int32) {
            index = static_cast<size_t>(
                std::dynamic_pointer_cast<PrimaryData<int32_t>>(key)->data());
        }
        if (!index.has_value()) {
            fail(EvalProblem::Construct, {}, "the key of the access is not a name or an index");
        }
        if (built != nullptr) {
            if (*index >= built->values.size()) {
                fail(EvalProblem::Construct, {}, "the key of the access is out of range");
            }
            return built->values[*index];
        }
        const auto tuple = std::dynamic_pointer_cast<TupleData>(targetData);
        if (tuple == nullptr) {
            fail(
                EvalProblem::Construct, {},
                "it reads an element of a value of type '" +
                    (targetData != nullptr && targetData->type() != nullptr
                         ? targetData->type()->toString()
                         : std::string("unknown")) +
                    "', and this build-time evaluation does not read that");
        }
        if (*index >= tuple->size()) {
            fail(EvalProblem::Construct, {}, "the key of the access is out of range");
        }
        return tuple->get(*index);
    }

    /// A comprehension built here: a tuple of the values its rounds produced, with the field
    /// names of a struct comprehension. The value itself is not a constant.
    data_ptr_t evalComprehension(const GCT::node_ptr_t &node) {
        const GCT::node_ptr_t &generator = node->comprehensionGenerator();
        if (generator == nullptr || generator->type() != GCT::NodeKind::SFOR) {
            fail(
                EvalProblem::Construct, {},
                "it builds a comprehension this evaluation cannot fill");
        }
        EvalComposite built;
        const std::string valueName = generator->staticForValueName();
        const std::string typeName  = generator->staticForTypeName();
        const bool byIndices        = generator->staticForIndices();
        const auto saved            = env_;
        const auto savedTypes       = typeEnv_;
        walkRounds(
            generator->staticForOperand(), byIndices, false,
            [&](const std::string &name, size_t index, Type *type, const int64_t *) {
                env_ = saved;
                bindRoundValue(valueName, name, index, byIndices);
                if (!typeName.empty() && type != nullptr) {
                    typeEnv_[typeName] = type;
                }
                built.names.push_back(name);
                built.values.push_back(eval(generator->staticForBody()));
            });
        env_     = saved;
        typeEnv_ = savedTypes;
        composites_[node] = std::move(built);
        return nullptr;
    }

    GCT::node_ptr_t expression_;
    camel::core::type::type_bindings_t vars_;
    std::unordered_map<std::string, data_ptr_t> env_;
    std::unordered_map<std::string, Type *> typeEnv_;
    EvalDataResolver dataResolver_;
    EvalCallResolver callResolver_;
    /// How a `static loop`'s written sequence is read (`Builder::staticLoopIntSequenceOf`); null
    /// when the caller has no builder to ask, and then a sequence is not a sequence the compiler
    /// knows.
    EvalSequenceResolver sequenceResolver_;
    size_t depth_ = 0;
    /// How many EXECs enclose the expression being evaluated: 1 is the body itself, more is a
    /// block nested in it, whose `return` gives the block's value.
    size_t execDepth_ = 0;
    /// The comprehensions this evaluation built, by node: an element read from one is a value.
    std::unordered_map<const GCT::Node *, EvalComposite> composites_;
};

} // namespace

/// The condition is answered in the declaration's module, not in the module that selects it:
/// the names it may call are the ones that module can see. The builder's "where it is" is
/// saved and put back, so a condition evaluated while another module calls the declaration
/// still reads the declaration's own names, imports and `using namespace`.
class Builder::ConditionContext {
  public:
    ConditionContext(
        Builder &builder, const generic_template_ptr_t &declaring, const ModuleUnit *contextUnit)
        : self_(builder), module_(builder.module_), unit_(builder.unit_),
          nodeScope_(builder.nodeScope_), graphScope_(builder.graphScope_),
          decoratedScope_(builder.decoratedScope_), templateScope_(builder.templateScope_),
          namespacePath_(builder.namespacePath_), usingFrames_(builder.usingFrames_) {
        ModuleUnit *target = const_cast<ModuleUnit *>(contextUnit);
        if (target == nullptr && declaring != nullptr) {
            const auto found = builder.units_.find(declaring->module.get());
            if (found != builder.units_.end()) {
                target = found->second.get();
            }
        }
        // A non-generic declaration selected from its own module is evaluated where it already
        // is; nothing has to move.
        if (target == nullptr && declaring == nullptr) {
            return;
        }
        if (target == builder.unit_ && declaring == nullptr) {
            return;
        }
        if (target != nullptr) {
            self_.module_         = target->module;
            self_.unit_           = target;
            self_.nodeScope_      = target->nodeScope;
            self_.graphScope_     = target->graphScope;
            self_.decoratedScope_ = target->decoratedScope;
            self_.templateScope_  = target->templateScope;
        } else {
            // Known only through its template (a module this builder did not declare, if any).
            self_.module_        = declaring->module;
            self_.unit_          = nullptr;
            self_.templateScope_ = declaring->templateScope;
        }
        if (declaring != nullptr) {
            self_.namespacePath_ = declaring->namespacePath;
            self_.usingFrames_   = declaring->usingFrames;
        } else {
            self_.namespacePath_.clear();
            self_.usingFrames_.clear();
            if (target != nullptr && target->tree != nullptr &&
                target->tree->root() != nullptr) {
                if (const auto *usings = target->tree->usingsOf(target->tree->root())) {
                    self_.usingFrames_.push_back(usings);
                }
            }
        }
        active_ = true;
    }

    ~ConditionContext() {
        if (!active_) {
            return;
        }
        self_.module_         = module_;
        self_.unit_           = unit_;
        self_.nodeScope_      = nodeScope_;
        self_.graphScope_     = graphScope_;
        self_.decoratedScope_ = decoratedScope_;
        self_.templateScope_  = templateScope_;
        self_.namespacePath_  = std::move(namespacePath_);
        self_.usingFrames_    = std::move(usingFrames_);
    }

    ConditionContext(const ConditionContext &)            = delete;
    ConditionContext &operator=(const ConditionContext &) = delete;

  private:
    Builder &self_;
    camel::core::module::module_ptr_t module_;
    ModuleUnit *unit_;
    node_scope_ptr_t nodeScope_;
    graph_scope_ptr_t graphScope_;
    decorated_scope_ptr_t decoratedScope_;
    template_scope_ptr_t templateScope_;
    std::vector<std::string> namespacePath_;
    std::vector<const std::vector<Reference> *> usingFrames_;
    bool active_ = false;
};

auto Builder::unitOfFunction(const compile_graph_ptr_t &graph) const -> const ModuleUnit * {
    const auto owner = rootSubGraphOwners_.find(graph.get());
    return owner == rootSubGraphOwners_.end() ? nullptr : owner->second;
}

bool Builder::whenPredicateAmbiguous(const std::string &written, WhenResult &problem) {
    if (module_ == nullptr) {
        return false;
    }
    using camel::core::module::detail::EntityAccess;
    const auto templateOf = [](const camel::core::module::entity &ent) -> generic_template_ptr_t {
        if (!EntityAccess::isGenericTemplate(ent)) {
            return nullptr;
        }
        return std::static_pointer_cast<GenericTemplate>(EntityAccess::genericTemplate(ent));
    };
    for (const auto &[ref, imports] : module_->namesImportedMoreThanOnce()) {
        if (ref.toString() != written) {
            continue;
        }
        std::vector<generic_template_ptr_t> predicates;
        std::vector<std::string> modules;
        bool ordinary = false;
        for (const auto &import : imports) {
            const auto entity = exports_->exportedEntity(*import.mod, import.remote);
            if (!entity.has_value()) {
                continue;
            }
            if (const generic_template_ptr_t tmpl = templateOf(*entity); tmpl != nullptr) {
                if (std::ranges::find(predicates, tmpl) == predicates.end()) {
                    predicates.push_back(tmpl);
                    modules.push_back(shownModuleName(*import.mod));
                }
            } else {
                ordinary = true;
            }
        }
        if (predicates.size() < 2 && !(predicates.size() == 1 && ordinary)) {
            continue;
        }
        problem.outcome = WhenOutcome::PredicateAmbiguous;
        problem.subject = written;
        std::string brought;
        for (size_t i = 0; i < modules.size(); ++i) {
            brought += (i == 0 ? "" : ", ") + std::format("'{}'", modules[i]);
        }
        if (ordinary) {
            brought += brought.empty() ? "an ordinary name" : " and an ordinary name";
        }
        problem.detail = brought;
        return true;
    }
    return false;
}

auto Builder::whenPredicateNamed(
    const std::string &written, const GCT::node_ptr_t &at, WhenResult &problem)
    -> std::optional<generic_template_ptr_t> {
    using camel::core::module::detail::EntityAccess;
    const auto templateOf = [](const camel::core::module::entity &ent) -> generic_template_ptr_t {
        if (!EntityAccess::isGenericTemplate(ent)) {
            return nullptr;
        }
        return std::static_pointer_cast<GenericTemplate>(EntityAccess::genericTemplate(ent));
    };
    // 1. The declaration's own module, from its innermost namespace out, its own name allowed
    // as a prefix (design principles 10.2, layers 1 and 2).
    const std::string own = moduleLevelName(written);
    if (const auto tmpl = templateScope_->get(own); tmpl.has_value()) {
        return tmpl.value().one();
    }
    // The module declares the name itself, as something that is not a generic function: the
    // name is resolved, and the ordinary report says why a condition cannot call it.
    if (const ModuleUnit *unit = currentModuleUnit();
        unit != nullptr && declaredAtTopLevel(*unit, own)) {
        return std::nullopt;
    }
    // 2. What its imports bring by name, selectively (`import { p as q }`) or through a module
    // prefix (`import m`, `import m as x`; `m::p`, `x::p`).
    if (!isOperatorName(written) && module_->hasImportedRef(written, ImportLayer::Named)) {
        if (whenPredicateAmbiguous(written, problem)) {
            return std::nullopt;
        }
        const auto entity = importedEntity(written, ImportLayer::Named);
        if (!entity.has_value()) {
            problem.outcome = WhenOutcome::NotCompileTime;
            problem.subject = written;
            problem.detail  = "the import brings no name of that spelling";
            return std::nullopt;
        }
        if (const generic_template_ptr_t tmpl = templateOf(*entity); tmpl != nullptr) {
            return tmpl;
        }
        problem.outcome = WhenOutcome::NotCompileTime;
        problem.subject = written;
        if (const auto source = module_->importedSource(written, ImportLayer::Named)) {
            problem.detail = std::format(
                "module '{}' brings it, and it is not a generic function answered at compile "
                "time",
                shownModuleName(*source->first));
        } else {
            problem.detail = "it is not a generic function answered at compile time";
        }
        return std::nullopt;
    }
    // 3. What a `using namespace` in force where the declaration is written brings.
    if (const auto match = throughUsing(written, at); match.has_value()) {
        if (!match->ownName.empty()) {
            if (const auto tmpl = templateScope_->get(match->ownName); tmpl.has_value()) {
                return tmpl.value().one();
            }
            problem.outcome = WhenOutcome::NotCompileTime;
            problem.subject = written;
            problem.detail  = "it is an ordinary function of this module";
            return std::nullopt;
        }
        if (!match->entity.has_value()) {
            return std::nullopt;
        }
        if (const generic_template_ptr_t tmpl = templateOf(*match->entity); tmpl != nullptr) {
            return tmpl;
        }
        problem.outcome = WhenOutcome::NotCompileTime;
        problem.subject = written;
        problem.detail  = "the imported name is not a generic function answered at compile time";
        return std::nullopt;
    }
    // 4. The prelude: the names the built-in module brings unasked (design principles 10.2,
    // layer 5).
    if (module_->hasImportedRef(written, ImportLayer::Ambient)) {
        const auto entity = importedEntity(written, ImportLayer::Ambient);
        if (entity.has_value()) {
            if (const generic_template_ptr_t tmpl = templateOf(*entity); tmpl != nullptr) {
                return tmpl;
            }
            problem.outcome = WhenOutcome::NotCompileTime;
            problem.subject = written;
            problem.detail  = "the prelude brings it, and it is not a generic function answered "
                              "at compile time";
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::string Builder::whenPredicateElsewhere(const std::string &written) const {
    const camel::core::module::Module *here = module_ != nullptr ? module_.get() : nullptr;
    for (const auto &[module, unit] : units_) {
        if (module == here || unit == nullptr) {
            continue;
        }
        std::string candidate = written;
        if (const size_t separator = written.find("::"); separator != std::string::npos) {
            const std::string prefix = written.substr(0, separator);
            if (prefix != module->name() && prefix != module->declaredName()) {
                continue;
            }
            candidate = written.substr(separator + 2);
        }
        if (unit->templateScope->get(candidate).has_value()) {
            return shownModuleName(*module);
        }
    }
    return {};
}

WhenResult Builder::evaluateWhenCondition(
    const GCT::node_ptr_t &condition, FunctionType *signature,
    const type_bindings_t &bindings,
    const std::unordered_map<std::string, data_ptr_t> &withValues,
    const generic_template_ptr_t &declaring, const ModuleUnit *contextUnit) {
    WhenResult result;
    if (condition == nullptr) {
        result.outcome = WhenOutcome::NotEvaluable;
        result.detail  = "the declaration has no condition";
        return result;
    }
    // Where the declaration is written, not where the call that selects it is: the predicate
    // lookup below is the declaration's own.
    ConditionContext context(*this, declaring, contextUnit);

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
const std::string written = callee->at(0)->name();
                // Every predicate is a function of the library now, resolved where the
                // declaration is written: the module's own generic functions, then what its
                // imports bring by name (selectively, through a module prefix or an alias),
                // then the prelude.
                WhenResult problem;
                const std::optional<generic_template_ptr_t> resolved =
                    whenPredicateNamed(written, callee->at(0), problem);
                if (!resolved.has_value() && problem.outcome != WhenOutcome::NotEvaluable) {
                    return problem;
                }
                const generic_template_ptr_t tmpl = resolved.value_or(nullptr);
                if (tmpl == nullptr) {
                    // Not found by the declaration's own names: name the module that declares
                    // it without being imported, rather than call the name unknown.
                    const std::string elsewhere = whenPredicateElsewhere(written);
                    if (!elsewhere.empty()) {
                        local.outcome = WhenOutcome::NotImported;
                        local.subject = written;
                        local.detail  = elsewhere;
                        return local;
                    }
                    local.outcome = WhenOutcome::NotEvaluable;
                    local.subject = written;
                    local.detail =
                        "no compile-time function of that name is visible here; a condition "
                        "calls a generic function of this module or of one it imports";
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
                    local.subject = written;
                    local.detail  = std::format(
                        "the condition gives {} type argument(s) and the predicate declares {}",
                        arguments.size(),
                        parameters.size());
                    return local;
                }
                for (Type *argument : arguments) {
                    if (argument == nullptr || containsTypeVar(argument)) {
                        local.outcome = WhenOutcome::TypeNotConcrete;
                        local.subject = written;
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
                        local.subject = written;
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
                    local.subject = written;
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
                //
                // The body is read where it is written, not where the declaration was selected:
                // `is_numeric` is declared in `types` and asks `is_integer`, which `types` also
                // declares, while the consumer that selected it (a program's `classify`) need not
                // have imported `is_integer` at all. Folding the body with the consumer's
                // `templateScope_` looks the callee up among the names the consumer can see and
                // refuses it as "not a generic declaration" -- a scope, not a missing capability.
                // `ConditionContext` is exactly that scope switch, and it is all this needs.
                try {
                    ConditionContext body(*this, tmpl, nullptr);
                    data_ptr_t folded = GctConstantEvaluator::runFunction(
                        tmpl->func, callBindings, valueArgs, {}, nullptr,
                        [this](
                            const GCT::node_ptr_t &call, const GCT::node_ptr_t &callee,
                            const std::vector<data_ptr_t> &arguments,
                            const std::function<data_ptr_t(const GCT::node_ptr_t &)> &evalArg,
                            const type_bindings_t &vars, size_t depth) {
                            return evaluateCallInsideBody(
                                call, callee, arguments, evalArg, vars, depth);
                        },
                        [this](const GCT::node_ptr_t &subject) {
                            return staticLoopIntSequenceOf(subject);
                        },
                        0);
                    std::optional<bool> foldedValue;
                    if (folded != nullptr && boolOf(folded, foldedValue)) {
                        local.outcome = *foldedValue ? WhenOutcome::Holds : WhenOutcome::Fails;
                        return local;
                    }
                } catch (const EvalFailure &failure) {
                    local.outcome = WhenOutcome::NotEvaluable;
                    local.subject = written;
                    local.detail  = failure.why;
                    return local;
                }
                local.outcome = WhenOutcome::NotEvaluable;
                local.subject = written;
                local.detail =
                    "the predicate's result is not a compile-time constant; a condition can "
                    "call a function only where its body reduces to one over the types";
                return local;
            }
            if (callee->type() == GCT::NodeKind::DREF) {
                const std::string written = callee->name();
                const std::string op      = plainName(written);
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
                // A direct call of an imported ordinary function is a condition that cannot be
                // answered: say that, rather than that the name is not one of `!`, `&&`, `||`.
                // A generic predicate written without its type arguments is named as such.
                if (!isOperatorName(written)) {
                    WhenResult problem;
                    const std::optional<generic_template_ptr_t> resolved =
                        whenPredicateNamed(written, callee, problem);
                    if (resolved.has_value()) {
                        local.outcome = WhenOutcome::NotEvaluable;
                        local.subject = written;
                        local.detail  = "a predicate is called with its type arguments, as "
                                        "'" +
                                        written + "::<...>(...)'";
                        return local;
                    }
                    if (problem.outcome != WhenOutcome::NotEvaluable) {
                        return problem;
                    }
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

namespace {

/// Whether an expression holds a call or a comprehension that could be a constant. Used to
/// leave an operator expression whose operands are ordinary values alone without evaluating it:
/// a runtime `a + b` must not cost a build-time attempt at every operator.
bool holdsCallCandidate(const GCT::node_ptr_t &node, size_t depth = 0) {
    if (node == nullptr || depth > 64) {
        return false;
    }
    switch (node->type()) {
    case GCT::NodeKind::LINK:
    case GCT::NodeKind::SACC:
    case GCT::NodeKind::SCOMP:
        return true;
    default:
        break;
    }
    for (size_t i = 0; i < node->size(); ++i) {
        if (holdsCallCandidate(node->at(i), depth + 1)) {
            return true;
        }
    }
    return false;
}

} // namespace

Builder::FoldedCall Builder::foldConstantCall(const GCT::node_ptr_t &call) {
    FoldedCall result;
    if (call == nullptr || call->type() != GCT::NodeKind::LINK || call->size() == 0) {
        return result;
    }
    result.call = writtenCallName(call);
    // Only a callee that names a declaration is a candidate. A function value, an operator and a
    // name that resolves to several candidates keep the ordinary build, with its own checks.
    GCT::node_ptr_t head = call->at(0);
    while (head != nullptr && head->type() == GCT::NodeKind::WITH) {
        head = head->at(0);
    }
    if (head == nullptr ||
        (head->type() != GCT::NodeKind::DREF && head->type() != GCT::NodeKind::TAPP)) {
        return result;
    }
    if (head->type() == GCT::NodeKind::TAPP &&
        (head->size() < 2 || head->at(0)->type() != GCT::NodeKind::DREF)) {
        return result;
    }
    // Resolve the name cheaply before evaluating anything: a function value, an operator of a
    // module (`println`) or a name with no declaration is not a call this evaluation reads, and
    // its arguments must not be evaluated for nothing.
    const GCT::node_ptr_t &nameNode =
        head->type() == GCT::NodeKind::TAPP ? head->at(0) : head;
    const std::string name = nameNode->name();
    const std::string plain = plainName(name);
    const bool builtinName  = !plain.empty() && isBuiltinOperatorName(plain);
    if (builtinName) {
        bool foldable = false;
        for (size_t i = 1; i < call->size() && !foldable; ++i) {
            foldable = holdsCallCandidate(call->at(i));
        }
        if (!foldable) {
            return result;
        }
    } else if (!templateScope_->get(name).has_value()) {
        const auto graphs = graphsAt(name);
        if (!graphs.has_value() || (*graphs)->empty()) {
            return result;
        }
    }
    // The arguments have to be compile-time constants themselves; this is what a call with a
    // parameter for an argument is refused for.
    const auto evalArgument = [this](const GCT::node_ptr_t &expression) {
        return compileTimeExpression(expression);
    };
    std::vector<data_ptr_t> arguments;
    try {
        for (size_t i = 1; i < call->size(); ++i) {
            arguments.push_back(evalArgument(call->at(i)));
        }
        result =
            evaluateCallAtBuildTime(call, call->at(0), arguments, evalArgument, typeBindings_, 0,
                                    true);
    } catch (const EvalFailure &failure) {
        // The refusal names the call, not the name inside it: the reason says what stands in
        // the way ("it reads 'n', which is not a compile-time constant").
        result.refusal  = FoldRefusal::ArgumentNotConstant;
        result.reason   = failure.why;
        return result;
    }
    return result;
}

Builder::FoldedCall Builder::evaluateCallAtBuildTime(
    const GCT::node_ptr_t &call, GCT::node_ptr_t callee,
    const std::vector<data_ptr_t> &arguments,
    const std::function<data_ptr_t(const GCT::node_ptr_t &)> &evalArgument,
    const type_bindings_t &vars, size_t depth, bool buildFirst) {
    FoldedCall result;
    result.call = writtenCallName(call);
    if (depth >= kMaxFoldedCallDepth) {
        result.refusal = FoldRefusal::TooDeep;
        result.reason  = "the call nests more than " + std::to_string(kMaxFoldedCallDepth) +
                        " calls deep; a recursion that has no base case for these constants has "
                        "no value at build time";
        return result;
    }
    // A `with` application gives its own arguments in front of the call.
    std::vector<data_ptr_t> withArguments;
    while (callee != nullptr && callee->type() == GCT::NodeKind::WITH) {
        for (size_t i = 1; i < callee->size(); ++i) {
            withArguments.push_back(evalArgument(callee->at(i)));
        }
        callee = callee->at(0);
    }
    const bool typeApplied = callee != nullptr && callee->type() == GCT::NodeKind::TAPP;
    if (callee == nullptr ||
        (callee->type() != GCT::NodeKind::DREF && !typeApplied)) {
        result.refusal = FoldRefusal::NotAProgramCall;
        result.reason  = "the callee is an expression, not a name the compiler knows";
        return result;
    }
    const GCT::node_ptr_t &nameNode = typeApplied ? callee->at(0) : callee;
    if (nameNode == nullptr || nameNode->type() != GCT::NodeKind::DREF) {
        result.refusal = FoldRefusal::NotAProgramCall;
        result.reason  = "the callee names no function";
        return result;
    }
    const std::string name = nameNode->name();
    result.call            = name;

    // An operator over constants is computed by the evaluator itself; this is how an arithmetic
    // expression over folded calls becomes a constant too.
    const std::string operatorName = plainName(name);
    if (!operatorName.empty() && isBuiltinOperatorName(operatorName)) {
        try {
            result.data = GctConstantEvaluator::runBuiltinCall(operatorName, arguments);
        } catch (const EvalFailure &failure) {
            result.refusal = FoldRefusal::NotEvaluable;
            result.reason  = failure.why;
            return result;
        }
        if (result.data == nullptr || result.data->type() == nullptr ||
            !isConstantKind(result.data->type())) {
            result.refusal = FoldRefusal::ResultNotConstantKind;
            result.reason  = "its value is not a kind a compile-time constant has";
            return result;
        }
        result.refusal = FoldRefusal::None;
        return result;
    }

    type_bindings_t bindings;
    GCT::node_ptr_t func;
    FunctionType *signature = nullptr;
    std::vector<std::string> parameters;

    const auto templateFound = templateScope_->get(name);
    if (templateFound.has_value() && templateFound.value().one() != nullptr) {
        const generic_template_ptr_t tmpl = templateFound.value().one();
        parameters                 = typeParameterNames(tmpl);
        bindings                   = tmpl->outerBindings;
        if (tmpl->funcType == nullptr || tmpl->funcType->hasRest()) {
            result.refusal = FoldRefusal::NotAProgramCall;
            result.reason  = "its declaration has no fixed parameter list";
            return result;
        }
        if (typeApplied) {
            if (callee->size() - 1 != parameters.size()) {
                result.refusal = FoldRefusal::NotAProgramCall;
                result.reason  = "its type arguments do not match its type parameters";
                return result;
            }
            for (size_t i = 0; i < parameters.size(); ++i) {
                Type *type = substituteTypeVars(callee->at(i + 1)->dataType(), vars);
                if (type == nullptr || type->code() == TypeCode::TypeVar ||
                    containsTypeVar(type)) {
                    result.refusal = FoldRefusal::NotAProgramCall;
                    result.reason  = "a type argument is not concrete in this body";
                    return result;
                }
                bindings.emplace_back(parameters[i], type);
            }
        } else {
            if (tmpl->funcType->withTypesCount() != withArguments.size() ||
                tmpl->funcType->normTypesCount() != arguments.size()) {
                result.refusal = FoldRefusal::NotAProgramCall;
                result.reason  = "the call does not give its parameters";
                return result;
            }
            for (size_t i = 0; i < withArguments.size(); ++i) {
                Type *pattern =
                    substituteTypeVars(tmpl->funcType->withTypeAt(i), bindings);
                if (pattern == nullptr || withArguments[i] == nullptr ||
                    !unifyTypeVars(pattern, withArguments[i]->type(), bindings)) {
                    result.refusal = FoldRefusal::NotAProgramCall;
                    result.reason  = "its type parameters cannot be read from this call";
                    return result;
                }
            }
            for (size_t i = 0; i < arguments.size(); ++i) {
                Type *pattern = substituteTypeVars(tmpl->funcType->normTypeAt(i), bindings);
                if (pattern == nullptr || arguments[i] == nullptr ||
                    !unifyTypeVars(pattern, arguments[i]->type(), bindings)) {
                    result.refusal = FoldRefusal::NotAProgramCall;
                    result.reason  = "its type parameters cannot be read from this call";
                    return result;
                }
            }
        }
        for (const std::string &parameter : parameters) {
            if (findTypeBinding(bindings, parameter) == nullptr) {
                result.refusal = FoldRefusal::NotAProgramCall;
                result.reason  = "its type parameters cannot be read from this call";
                return result;
            }
        }
        func      = tmpl->func;
        signature = instantiateFunctionType(tmpl->funcType, bindings);
        if (signature->withTypesCount() != withArguments.size() ||
            signature->normTypesCount() != arguments.size()) {
            result.refusal = FoldRefusal::NotAProgramCall;
            result.reason  = "the call does not give its parameters";
            return result;
        }
        // Build the instance before reading its body: where a call becomes a constant, the
        // instance is still a graph of the program and its body keeps its own diagnostics.
        // A `constas` parameter is left to the ordinary build (the instance key carries the
        // constant's extent, which is not known here).
        bool hasConstas = false;
        for (size_t i = 0; i < signature->normTypesCount(); ++i) {
            hasConstas = hasConstas || signature->normIsConstasAt(i);
        }
        if (buildFirst && !hasConstas) {
            try {
                (void)instantiateGeneric(tmpl, bindings, {}, {}, callee);
            } catch (Diagnostic &diagnostic) {
                diags_->add(std::move(diagnostic));
            } catch (...) {
                // The instance does not build; the call keeps the ordinary build, which reports
                // what went wrong with it.
                result.refusal = FoldRefusal::NotAProgramCall;
                result.reason  = "its instance does not build";
                return result;
            }
        }
    } else {
        if (typeApplied) {
            result.refusal = FoldRefusal::NotAProgramCall;
            result.reason  = "the name is not a generic declaration";
            return result;
        }
        const auto graphs = graphsAt(name);
        if (!graphs.has_value() || (*graphs)->size() != 1) {
            result.refusal = FoldRefusal::NotAProgramCall;
            result.reason  = !graphs.has_value()
                                 ? "no function of that name is declared here"
                                 : "more than one declaration of that name can be called here";
            return result;
        }
        const compile_graph_ptr_t &graph = (*graphs)->front();
        if (decoratedOf_.contains(graph.get())) {
            result.refusal = FoldRefusal::NotAProgramCall;
            result.reason  = "it is decorated, and a decoration is code the call runs";
            return result;
        }
        const auto declaration = topLevelFunctions_.find(graph.get());
        if (declaration == topLevelFunctions_.end() || declaration->second == nullptr) {
            result.refusal = FoldRefusal::NotAProgramCall;
            result.reason  = "it has no body this evaluation can read";
            return result;
        }
        func = declaration->second->gct;
        if (func == nullptr || func->nativeDeclaration() || func->size() < 2) {
            result.refusal = FoldRefusal::NotAProgramCall;
            result.reason  = "it is a body-less declaration";
            return result;
        }
        signature = graph->funcType();
        if (signature == nullptr || !signature->hasExitType()) {
            result.refusal = FoldRefusal::NotAProgramCall;
            result.reason  = "its signature has no result type yet";
            return result;
        }
        if (signature->withTypesCount() != withArguments.size() ||
            signature->normTypesCount() != arguments.size()) {
            result.refusal = FoldRefusal::NotAProgramCall;
            result.reason  = "the call does not give its parameters";
            return result;
        }
        if (buildFirst) {
            // The body is built before it is read: a call that becomes a constant still leaves
            // the graph of the function it calls, and a body that does not build keeps its own
            // diagnostics instead of disappearing into a folded value.
            ensureFunctionBuilt(graph);
        }
    }

    // Purity: a `sync` function, a `var` parameter and a declared resource use are effects. A
    // declaration that says nothing (the default) is evaluated, and the evaluation refuses the
    // effectful constructs it meets (`println`, a `var`), so a body whose effects come from an
    // untaken branch is folded and one whose effects are on the path is not.
    if (signature->modifiers().sync()) {
        result.refusal = FoldRefusal::HasEffects;
        result.reason  = "it is a 'sync' function";
        return result;
    }
    const size_t withCount = signature->withTypesCount();
    for (size_t i = 0; i < withCount; ++i) {
        if (signature->withIsVarAt(i)) {
            result.refusal = FoldRefusal::HasEffects;
            result.reason  = "its 'with' parameter '" + std::string(signature->argNameAt(i)) +
                            "' is a 'var'";
            return result;
        }
    }
    for (size_t i = 0; i < signature->normTypesCount(); ++i) {
        if (signature->normIsVarAt(i)) {
            result.refusal = FoldRefusal::HasEffects;
            result.reason  = "its parameter '" + std::string(signature->argNameAt(withCount + i)) +
                            "' is a 'var'";
            return result;
        }
    }
    const auto &resources = signature->resources();
    if (resources.mode == camel::core::sem::ResourceSignature::Mode::Concrete &&
        !resources.pure()) {
        result.refusal = FoldRefusal::HasEffects;
        result.reason  = "it declares effects: " + camel::core::sem::toString(resources);
        return result;
    }

    std::unordered_map<std::string, data_ptr_t> withValues;
    for (size_t i = 0; i < withCount && i < withArguments.size(); ++i) {
        withValues[std::string(signature->argNameAt(i))] = withArguments[i];
    }
    if (func != nullptr) {
        if (const auto *when = GCT::Tree::of(func)->whenOf(func)) {
            const WhenResult condition =
                evaluateWhenCondition(when->condition, signature, bindings, withValues);
            if (!condition.holds()) {
                result.refusal = FoldRefusal::NotAProgramCall;
                result.reason  = "the declaration's 'when' condition is not shown to hold here";
                return result;
            }
        }
    }

    if (!signature->hasExitType() || signature->exitType() == nullptr) {
        result.refusal = FoldRefusal::ResultNotConstantKind;
        result.reason  = "it has no result type";
        return result;
    }
    Type *resultType = substituteTypeVars(signature->exitType(), bindings);
    if (resultType == nullptr || !isConstantKind(resultType)) {
        result.refusal = FoldRefusal::ResultNotConstantKind;
        result.reason  = "its result type is '" +
                        (resultType != nullptr ? resultType->toString() : std::string("unknown")) +
                        "', not a number, a bool or a string";
        return result;
    }

    // A name the body reads is not resolved: it would be looked up where the call is written,
    // not where the body is (a local of the caller can shadow a top-level value the callee
    // uses), so the value read could be the wrong one. A body that reads a name other than its
    // parameters is not folded.
    const EvalDataResolver dataResolver = nullptr;
    const auto callResolver = [this](
                                  const GCT::node_ptr_t &nestedCall,
                                  const GCT::node_ptr_t &nestedCallee,
                                  const std::vector<data_ptr_t> &nestedArguments,
                                  const std::function<data_ptr_t(const GCT::node_ptr_t &)> &eval,
                                  const type_bindings_t &nestedVars, size_t nestedDepth) {
        return evaluateCallInsideBody(
            nestedCall, nestedCallee, nestedArguments, eval, nestedVars, nestedDepth);
    };
    try {
        result.data = GctConstantEvaluator::runFunction(
            func, bindings, arguments, withArguments, dataResolver, callResolver,
            [this](const GCT::node_ptr_t &subject) { return staticLoopIntSequenceOf(subject); },
            depth + 1);
    } catch (const EvalFailure &failure) {
        switch (failure.kind) {
        case EvalProblem::ArgumentNotConstant:
            result.refusal = FoldRefusal::ArgumentNotConstant;
            break;
        case EvalProblem::Effects:
            result.refusal = FoldRefusal::HasEffects;
            break;
        case EvalProblem::TooDeep:
            result.refusal = FoldRefusal::TooDeep;
            break;
        case EvalProblem::ResultNotConstantKind:
            result.refusal = FoldRefusal::ResultNotConstantKind;
            break;
        case EvalProblem::Construct:
            result.refusal = FoldRefusal::NotEvaluable;
            break;
        }
        result.call   = failure.call.empty() ? name : failure.call;
        result.reason = failure.why;
        return result;
    }
    if (result.data == nullptr) {
        result.refusal = FoldRefusal::NotEvaluable;
        result.reason  = "its body produces no value";
        return result;
    }
    if (result.data->type() == nullptr || !isConstantKind(result.data->type())) {
        result.refusal = FoldRefusal::ResultNotConstantKind;
        result.reason  = "its value is not a kind a compile-time constant has";
        return result;
    }
    result.refusal = FoldRefusal::None;
    return result;
}

data_ptr_t Builder::evaluateCallInsideBody(
    const GCT::node_ptr_t &call, const GCT::node_ptr_t &callee,
    const std::vector<data_ptr_t> &arguments,
    const std::function<data_ptr_t(const GCT::node_ptr_t &)> &evalArgument,
    const type_bindings_t &vars, size_t depth) {
    const FoldedCall folded =
        evaluateCallAtBuildTime(call, callee, arguments, evalArgument, vars, depth, false);
    switch (folded.refusal) {
    case FoldRefusal::None:
        break;
    case FoldRefusal::NotAProgramCall:
        throw EvalFailure{
            EvalProblem::Construct, folded.call,
            "it calls '" + (folded.call.empty() ? std::string("an expression") : folded.call) +
                "', which this build-time evaluation cannot read" +
                (folded.reason.empty() ? std::string{} : ": " + folded.reason)};
    case FoldRefusal::ArgumentNotConstant:
        throw EvalFailure{EvalProblem::ArgumentNotConstant, folded.call, folded.reason};
    case FoldRefusal::HasEffects:
        throw EvalFailure{EvalProblem::Effects, folded.call, folded.reason};
    case FoldRefusal::TooDeep:
        throw EvalFailure{EvalProblem::TooDeep, folded.call, folded.reason};
    case FoldRefusal::ResultNotConstantKind:
        throw EvalFailure{EvalProblem::ResultNotConstantKind, folded.call, folded.reason};
    case FoldRefusal::NotEvaluable:
        throw EvalFailure{EvalProblem::Construct, folded.call, folded.reason};
    }
    if (folded.data == nullptr) {
        throw EvalFailure{
            EvalProblem::Construct, folded.call, "the call has no value at build time"};
    }
    return folded.data;
}

data_ptr_t Builder::constantValueOrNull(const GCT::node_ptr_t &expression) {
    // The same evaluation the folding entry points use, with a refusal answered as "no value":
    // a place that needs a constant decides for itself whether the refusal is worth a
    // diagnostic (the sequence of a `static loop` reports its own, by name).
    try {
        return compileTimeExpression(expression);
    } catch (const EvalFailure &) {
        return nullptr;
    }
}

data_ptr_t Builder::compileTimeExpression(const GCT::node_ptr_t &expression) {
    if (expression == nullptr) {
        return nullptr;
    }
    if (expression->type() == GCT::NodeKind::DATA) {
        return expression->data();
    }
    const auto callResolver = [this](
                                  const GCT::node_ptr_t &call,
                                  const GCT::node_ptr_t &callee,
                                  const std::vector<data_ptr_t> &arguments,
                                  const std::function<data_ptr_t(const GCT::node_ptr_t &)> &eval,
                                  const type_bindings_t &vars, size_t depth) {
        return evaluateCallInsideBody(call, callee, arguments, eval, vars, depth);
    };
    return GctConstantEvaluator::runExpression(
        expression, typeBindings_, nullptr, callResolver,
        [this](const GCT::node_ptr_t &subject) { return staticLoopIntSequenceOf(subject); }, 0);
}

[[noreturn]] void Builder::reportFoldRefusal(
    const FoldedCall &folded, const GCT::node_ptr_t &origin) {
    const auto at = origin != nullptr ? origin->origin() : camel::source::kInvalidOriginId;
    switch (folded.refusal) {
    case FoldRefusal::ArgumentNotConstant:
        diags_->of(SemanticDiag::FoldCallArgumentNotConstant)
            .atOrigin(at)
            .commit(folded.call, folded.reason);
        break;
    case FoldRefusal::HasEffects:
        diags_->of(SemanticDiag::FoldCallHasEffects)
            .atOrigin(at)
            .commit(folded.call, folded.reason);
        break;
    case FoldRefusal::TooDeep:
        diags_->of(SemanticDiag::FoldCallTooDeep)
            .atOrigin(at)
            .commit(folded.call, folded.reason);
        break;
    case FoldRefusal::ResultNotConstantKind:
        diags_->of(SemanticDiag::FoldCallResultNotConstant)
            .atOrigin(at)
            .commit(folded.call, folded.reason);
        break;
    case FoldRefusal::NotEvaluable:
        diags_->of(SemanticDiag::FoldCallNotEvaluable)
            .atOrigin(at)
            .commit(folded.call, folded.reason);
        break;
    case FoldRefusal::None:
    case FoldRefusal::NotAProgramCall:
        ASSERT(false, "reportFoldRefusal is for a refusal");
        break;
    }
    throw BuildAbortException();
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
    case WhenOutcome::NotImported:
        diags_->of(SemanticDiag::WhenPredicateNotImported)
            .atOrigin(at)
            .commit(name, result.subject, result.detail);
        break;
    case WhenOutcome::NotCompileTime:
        diags_->of(SemanticDiag::WhenPredicateNotCompileTime)
            .atOrigin(at)
            .commit(name, result.subject, result.detail);
        break;
    case WhenOutcome::PredicateAmbiguous:
        diags_->of(SemanticDiag::WhenPredicateAmbiguous)
            .atOrigin(at)
            .commit(name, result.subject, result.detail);
        break;
    case WhenOutcome::Fails:
    case WhenOutcome::Holds:
        ASSERT(false, "reportWhenProblem is for a condition that does not hold");
        break;
    }
    throw BuildAbortException();
}

} // namespace camel::compile::gir
