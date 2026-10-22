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

#include "camel/core/data/primary.h"
#include "camel/core/type/text.h"
#include "camel/core/type/tree.h"
#include "camel/core/type/typevar.h"
#include "core/module/entity_internal.h"

#include <algorithm>
#include <format>
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

/// The condition is answered in the declaration's module, not in the module that selects it:
/// the names it may call are the ones that module can see. The builder's "where it is" is
/// saved and put back, so a condition evaluated while another module calls the declaration
/// still reads the declaration's own names, imports and `using namespace`.
class Builder::ConditionContext {
  public:
    ConditionContext(
        Builder &builder, const template_ptr_t &declaring, const ModuleUnit *contextUnit)
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
    const auto templateOf = [](const camel::core::module::entity &ent) -> template_ptr_t {
        if (!EntityAccess::isGenericTemplate(ent)) {
            return nullptr;
        }
        return std::static_pointer_cast<GenericTemplate>(EntityAccess::genericTemplate(ent));
    };
    for (const auto &[ref, imports] : module_->namesImportedMoreThanOnce()) {
        if (ref.toString() != written) {
            continue;
        }
        std::vector<template_ptr_t> predicates;
        std::vector<std::string> modules;
        bool ordinary = false;
        for (const auto &import : imports) {
            const auto entity = exports_->exportedEntity(*import.mod, import.remote);
            if (!entity.has_value()) {
                continue;
            }
            if (const template_ptr_t tmpl = templateOf(*entity); tmpl != nullptr) {
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
    -> std::optional<template_ptr_t> {
    using camel::core::module::detail::EntityAccess;
    const auto templateOf = [](const camel::core::module::entity &ent) -> template_ptr_t {
        if (!EntityAccess::isGenericTemplate(ent)) {
            return nullptr;
        }
        return std::static_pointer_cast<GenericTemplate>(EntityAccess::genericTemplate(ent));
    };
    // 1. The declaration's own module, from its innermost namespace out, its own name allowed
    // as a prefix (design principles 10.2, layers 1 and 2).
    const std::string own = moduleLevelName(written);
    if (const auto tmpl = templateScope_->get(own); tmpl.has_value()) {
        return *tmpl;
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
        if (const template_ptr_t tmpl = templateOf(*entity); tmpl != nullptr) {
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
                return *tmpl;
            }
            problem.outcome = WhenOutcome::NotCompileTime;
            problem.subject = written;
            problem.detail  = "it is an ordinary function of this module";
            return std::nullopt;
        }
        if (!match->entity.has_value()) {
            return std::nullopt;
        }
        if (const template_ptr_t tmpl = templateOf(*match->entity); tmpl != nullptr) {
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
            if (const template_ptr_t tmpl = templateOf(*entity); tmpl != nullptr) {
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
    const template_ptr_t &declaring, const ModuleUnit *contextUnit) {
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
                const std::optional<template_ptr_t> resolved =
                    whenPredicateNamed(written, callee->at(0), problem);
                if (!resolved.has_value() && problem.outcome != WhenOutcome::NotEvaluable) {
                    return problem;
                }
                const template_ptr_t tmpl = resolved.value_or(nullptr);
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
                    const std::optional<template_ptr_t> resolved =
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