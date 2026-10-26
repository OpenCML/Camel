// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Modules and imports: the conflicts an import introduces and the declarations a module
 * exports.
 */

#include "builder.h"
#include "builder_support.h"
#include "builder_helpers.h"

#include "camel/compile/gir/static_function.h"
#include "camel/core/numeric_convert.h"
#include "camel/core/operator_names.h"
#include "camel/core/rtdata/string.h"
#include "camel/execute/entry_args.h"
#include "camel/runtime/control.h"
#include "camel/runtime/graph.h"
#include "camel/utils/log.h"
#include "camel/utils/scope.h"
#include "camel/utils/str.h"
#include "camel/utils/type.h"
#include "core/module/entity_internal.h"

#include "camel/core/operator_table.h"
#include "camel/core/operator_traits.h"
#include "camel/core/resource_semantics.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/other.h"
#include "camel/execute/executor.h"

#include <atomic>
#include <cctype>
#include <format>
#include <set>
#include <unordered_set>
#define DEBUG_LEVEL -1

using namespace std;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::data;
using namespace camel::core::type;

namespace camel::compile::gir {

namespace {

/// Whether one and the same argument can have both types, for some instantiation of the type
/// variables of two declarations: `T[]` and `int[]` can (`T` is `int`), `T[]` and `string`
/// cannot, and `T` can be whatever the other side is. A value written at a `T?` or `T!`
/// position enters it, so `T?` and `int` can be the type of one argument, and so can `T?` and
/// `U!` (a plain value enters either). `left` and `right` remember what each side's variables
/// stand for, so that `(T, T)` and `(int, string)` are told apart. Two types with no variable
/// are compared exactly.
///
/// The answer is used to refuse two generic declarations of one name that one call could enter
/// (design principles 4.3, 5.1). Where this does not look into a type
/// (an application of a type function, for one) it says they can agree: refusing a pair that
/// cannot overlap is a diagnostic to remove, while letting one through is a call whose meaning
/// the declaration order decides.
bool typesCanAgree(
    Type *left, Type *right, std::unordered_map<std::string, Type *> &leftBindings,
    std::unordered_map<std::string, Type *> &rightBindings) {
    if (left == nullptr || right == nullptr) {
        return true;
    }
    if (left->code() == TypeCode::TypeVar) {
        const auto *var = static_cast<const TypeVarType *>(left);
        if (const auto bound = leftBindings.find(var->name()); bound != leftBindings.end()) {
            if (bound->second == left) {
                // The variable is bound to itself: two signatures that write the same variable
                // name in two parameters of one kind (`k::<T>(a: T, b: T)` twice) equate it with
                // itself here, and following that binding again asks the same question forever
                // (such a pair used to run the compiler out of stack). The binding already
                // answers this question, and the answer is yes.
                return true;
            }
            return typesCanAgree(bound->second, right, leftBindings, rightBindings);
        }
        leftBindings.emplace(var->name(), right);
        return true;
    }
    if (right->code() == TypeCode::TypeVar) {
        const auto *var = static_cast<const TypeVarType *>(right);
        if (const auto bound = rightBindings.find(var->name()); bound != rightBindings.end()) {
            if (bound->second == right) {
                return true; // the mirror of the case above
            }
            return typesCanAgree(left, bound->second, leftBindings, rightBindings);
        }
        rightBindings.emplace(var->name(), left);
        return true;
    }
    if (TupleType *tagged = asTagged(left)) {
        TupleType *other = asTagged(right);
        return typesCanAgree(
            tagged->payloadType(), other != nullptr ? other->payloadType() : right, leftBindings,
            rightBindings);
    }
    if (TupleType *tagged = asTagged(right)) {
        return typesCanAgree(left, tagged->payloadType(), leftBindings, rightBindings);
    }
    if (!containsTypeVar(left) && !containsTypeVar(right)) {
        return left->equals(right);
    }
    if (left->code() != right->code()) {
        return false;
    }
    switch (left->code()) {
    case TypeCode::Array:
        return typesCanAgree(
            static_cast<ArrayType *>(left)->elemType(),
            static_cast<ArrayType *>(right)->elemType(), leftBindings, rightBindings);
    case TypeCode::Tuple: {
        auto *want = static_cast<TupleType *>(left);
        auto *have = static_cast<TupleType *>(right);
        if (want->size() != have->size() || want->isCell() != have->isCell() ||
            want->tag() != have->tag()) {
            return false;
        }
        for (size_t i = 0; i < want->size(); ++i) {
            if (!typesCanAgree(
                    want->typeAt(i), have->typeAt(i), leftBindings, rightBindings)) {
                return false;
            }
        }
        return true;
    }
    case TypeCode::Struct: {
        auto *want = static_cast<StructType *>(left);
        auto *have = static_cast<StructType *>(right);
        if (want->named() || have->named()) {
            if (!want->named() || !have->named() || want->decl() != have->decl() ||
                want->typeArgCount() != have->typeArgCount()) {
                return false;
            }
            for (size_t i = 0; i < want->typeArgCount(); ++i) {
                if (!typesCanAgree(
                        want->typeArgAt(i), have->typeArgAt(i), leftBindings, rightBindings)) {
                    return false;
                }
            }
            return true;
        }
        if (want->size() != have->size()) {
            return false;
        }
        for (size_t i = 0; i < want->size(); ++i) {
            const auto field = have->findField(want->fieldName(i));
            if (!field.has_value() ||
                !typesCanAgree(
                    want->typeAt(i), have->typeAt(*field), leftBindings, rightBindings)) {
                return false;
            }
        }
        return true;
    }
    case TypeCode::Function: {
        auto *want = static_cast<FunctionType *>(left);
        auto *have = static_cast<FunctionType *>(right);
        if (want->withTypesCount() != have->withTypesCount() ||
            want->normTypesCount() != have->normTypesCount()) {
            return false;
        }
        for (size_t i = 0; i < want->withTypesCount(); ++i) {
            if (!typesCanAgree(
                    want->withTypeAt(i), have->withTypeAt(i), leftBindings, rightBindings)) {
                return false;
            }
        }
        for (size_t i = 0; i < want->normTypesCount(); ++i) {
            if (!typesCanAgree(
                    want->normTypeAt(i), have->normTypeAt(i), leftBindings, rightBindings)) {
                return false;
            }
        }
        if (want->hasExitType() && have->hasExitType()) {
            return typesCanAgree(
                want->exitType(), have->exitType(), leftBindings, rightBindings);
        }
        return true;
    }
    default:
        break;
    }
    return true;
}

/// Whether one call could be an instance of both signatures: the same number of parameters of
/// both kinds, and one argument list that both can take (`typesCanAgree` above).
bool signaturesCanAgree(FunctionType *left, FunctionType *right) {
    if (left == nullptr || right == nullptr ||
        left->withTypesCount() != right->withTypesCount() ||
        left->normTypesCount() != right->normTypesCount()) {
        return false;
    }
    std::unordered_map<std::string, Type *> leftBindings;
    std::unordered_map<std::string, Type *> rightBindings;
    for (size_t i = 0; i < left->withTypesCount(); ++i) {
        if (!typesCanAgree(
                left->withTypeAt(i), right->withTypeAt(i), leftBindings, rightBindings)) {
            return false;
        }
    }
    for (size_t i = 0; i < left->normTypesCount(); ++i) {
        if (!typesCanAgree(
                left->normTypeAt(i), right->normTypeAt(i), leftBindings, rightBindings)) {
            return false;
        }
    }
    return true;
}

} // namespace

void Builder::reportImportNameConflicts() {
    // Two imports by name that bring functions of the same name and the same parameter types
    // (design principles 10.2): the call used to take the function of the import written
    // first, silently. Functions of different parameter types form a set of overloads, as
    // before; whether that stays is undecided.
    if (!module_) {
        return;
    }
    const auto sameSignature = [](FunctionType *a, FunctionType *b) {
        if (!a || !b || a->withTypesCount() != b->withTypesCount() ||
            a->normTypesCount() != b->normTypesCount()) {
            return false;
        }
        for (size_t i = 0; i < a->withTypesCount(); ++i) {
            if (!a->withTypeAt(i)->equals(b->withTypeAt(i))) {
                return false;
            }
        }
        for (size_t i = 0; i < a->normTypesCount(); ++i) {
            if (!a->normTypeAt(i)->equals(b->normTypeAt(i))) {
                return false;
            }
        }
        return true;
    };
    const auto signatureOf = [](FunctionType *type) {
        std::string text;
        if (type->withTypesCount() > 0) {
            text += "<";
            for (size_t i = 0; i < type->withTypesCount(); ++i) {
                text += (i ? ", " : "") + type->withTypeAt(i)->toString();
            }
            text += "> ";
        }
        text += "(";
        for (size_t i = 0; i < type->normTypesCount(); ++i) {
            text += (i ? ", " : "") + type->normTypeAt(i)->toString();
        }
        return text + ")";
    };
    bool found = false;
    for (const auto &[ref, imports] : module_->namesImportedMoreThanOnce()) {
        for (size_t later = 1; later < imports.size(); ++later) {
            bool reported = false;
            for (size_t earlier = 0; earlier < later && !reported; ++earlier) {
                const auto first =
                    exports_->exportedEntity(*imports[earlier].mod, imports[earlier].remote);
                const auto second =
                    exports_->exportedEntity(*imports[later].mod, imports[later].remote);
                if (!first || !second) {
                    continue;
                }
                using camel::core::module::detail::EntityAccess;
                // What the two imports bring that cannot stand side by side: two functions of
                // the same parameter types, or a value and anything else of that name (a value
                // is not overloaded).
                std::optional<std::string> clash;
                // A value: exported by a module written in Camel (a node of the root graph) or
                // a constant of a C++ module (design principles 10.1).
                const auto isValue = [](const camel::core::module::entity &e) {
                    return EntityAccess::isNode(e) || e.isValue();
                };
                if (isValue(*first) || isValue(*second)) {
                    const bool same =
                        (EntityAccess::isNode(*first) && EntityAccess::isNode(*second) &&
                         EntityAccess::node(*first) == EntityAccess::node(*second)) ||
                        (first->isValue() && second->isValue() &&
                         first->value() == second->value());
                    if (!same) {
                        clash = isValue(*first) && isValue(*second)
                                    ? std::string("a value in both")
                                    : std::string("a value in one of them");
                    }
                } else if (EntityAccess::isGraphSet(*first) && EntityAccess::isGraphSet(*second)) {
                    const auto firstSet  = asCompileGraphSet(EntityAccess::graphSet(*first));
                    const auto secondSet = asCompileGraphSet(EntityAccess::graphSet(*second));
                    for (const auto &f : *firstSet) {
                        for (const auto &g : *secondSet) {
                            if (!clash && f != g && sameSignature(f->funcType(), g->funcType())) {
                                clash = "the same signature " + signatureOf(f->funcType());
                            }
                        }
                    }
                }
                if (clash) {
                    auto diag         = diags_->of(SemanticDiag::ImportNameConflict);
                    const auto &here  = imports[later];
                    const auto &there = imports[earlier];
                    if (here.origin != camel::source::kInvalidOriginId) {
                        diag.atOrigin(here.origin);
                    } else {
                        diag.at(TokenRange{here.tokenStart, here.tokenEnd});
                    }
                    if (there.origin != camel::source::kInvalidOriginId) {
                        diag.relatedOrigin(there.origin, "also imported here");
                    } else {
                        diag.related(
                            TokenRange{there.tokenStart, there.tokenEnd},
                            "also imported here");
                    }
                    diag.commit(
                        ref.toString(),
                        imports[earlier].mod->name(),
                        imports[later].mod->name(),
                        *clash);
                    reported = true;
                    found    = true;
                }
            }
        }
    }
    if (found) {
        throw BuildAbortException();
    }
}

std::optional<std::string> Builder::nativeOperatorUri(const GCT::node_ptr_t &gct) {
    // `#op('module:function')` names the operator when the name of the declaration is not the
    // name of the function: an operator of another module under a name of this one, or a
    // function whose name is not a Camel identifier (design principles 6.2, 6.4).
    const TagFacts tags = tagFactsOf(gct, camel::core::tag::Func);
    if (tags.declaration == camel::core::attr::kNoAttrs) {
        return std::nullopt;
    }
    const auto value =
        camel::core::attr::valueOf(tags.declaration, camel::core::attr::key("op"));
    if (value == camel::core::attr::kNoValue) {
        return std::nullopt;
    }
    const camel::core::attr::Value uri = camel::core::attr::value(value);
    if (uri.kind != camel::core::attr::ValueKind::Symbol) {
        return std::nullopt;
    }
    return uri.text;
}

namespace {
/// The identity of a resource, as `module:name`: the module that declares it together with its
/// name (design principles 10.2). A clause names a resource of its own module bare (`r`), one of
/// another module through that module (`m::r`), and the C++ module declares it with the module
/// separator written as one colon (`m:r`); the three spellings name one resource, so the identity
/// is read from whichever of them is written, and a name that leaves the module out is taken to
/// be one of `declaring`, the module whose declaration it is compared with.
std::string resourceIdentity(std::string_view name, std::string_view declaring) {
    const size_t separator = name.find("::");
    if (separator != std::string_view::npos) {
        return std::string(name.substr(0, separator)) + ":" + std::string(name.substr(separator + 2));
    }
    if (const size_t colon = name.find(':'); colon != std::string_view::npos) {
        return std::string(name.substr(0, colon)) + ":" + std::string(name.substr(colon + 1));
    }
    return std::string(declaring) + ":" + std::string(name);
}

/// The accesses a clause writes, as the identity of the resource: the operand an access is
/// bound to (`#i`), or the name of an ambient resource. The clause says which resources a call
/// touches, not how (its modes are the upper bound a call uses), so the modes and the class of
/// a parameter are not compared: a handle a declaration names and the `Foreign` resource the
/// operator's module wraps are the same operand.
std::set<std::string> resourceIdentities(
    const camel::core::sem::ResourceSignature &signature, std::string_view declaring) {
    std::set<std::string> identities;
    for (const auto &use : signature.params) {
        identities.insert(std::format("#{}", use.index));
    }
    for (const auto &use : signature.ambient) {
        identities.insert(resourceIdentity(use.name, declaring));
    }
    return identities;
}

/// The text of a signature for a diagnostic: its parameter names where the type has them,
/// `#index` otherwise.
std::string describeResources(
    const camel::core::sem::ResourceSignature &signature, const FunctionType *type) {
    if (signature.mode == camel::core::sem::ResourceSignature::Mode::Generic) {
        return "nothing about its resources";
    }
    if (signature.pure()) {
        return "pure";
    }
    std::string out     = "uses ";
    bool first          = true;
    const auto add      = [&](const std::string &name) {
        out += (first ? "" : ", ") + name;
        first = false;
    };
    for (const auto &use : signature.params) {
        if (type != nullptr && use.index < type->argNamesCount()) {
            add(std::string(type->argNameAt(use.index)));
        } else {
            add(std::format("#{}", use.index));
        }
    }
    for (const auto &use : signature.ambient) {
        add(use.name);
    }
    for (camel::core::sem::ResourceClass cls : signature.anyOf) {
        add(std::format("any {}", camel::core::sem::resourceClassName(cls)));
    }
    if (signature.external) {
        add("external");
    }
    return out;
}
} // namespace

void Builder::declareNativeOperator(const GCT::node_ptr_t &gct) {
    const std::string name = gct->name();
    FunctionType *funcType = tt::as_ptr<FunctionType>(gct->at(0)->dataType());
    // The URI's prefix is the name the module declares (`module bytes`), not the name the
    // runtime gave the file: the entry module is `main` to the runtime, but a declaration in it
    // stands for an operator of the module it writes itself to be (design principles 10.1;
    // rule 4).
    const std::string &declared = unit_->module->declaredName();
    const std::string where     = declared.empty() ? unit_->module->name() : declared;
    std::string uri             = where + ":" + name;
    if (auto tagged = nativeOperatorUri(gct); tagged.has_value()) {
        uri = std::move(*tagged);
    }
    if (uri.find(':') == std::string::npos) {
        diags_->of(SemanticDiag::InvalidOperatorURI).atOrigin(gct->origin()).commit(uri);
        throw BuildAbortException();
    }
    // The declaration is the signature of an operator of a loaded module: an operator no executor
    // answers cannot be called, and a mistake of the declaration (a wrong module, a typo) must be
    // reported where it is written rather than when the call runs (step 1).
    if (!context_->execMgr().findWithoutImport(uri).has_value()) {
        diags_->of(SemanticDiag::NativeDeclarationNotLoaded)
            .atOrigin(gct->origin())
            .commit(name, uri);
        throw BuildAbortException();
    }
    // The operator's purity is the C++ module's, not the declaration's; a declaration of an
    // operator that declares nothing would run with effects a pass cannot see.
    if (!camel::core::OperatorTable::instance().find(uri).has_value()) {
        diags_->of(SemanticDiag::NativeDeclarationNotLoaded)
            .atOrigin(gct->origin())
            .commit(name, uri);
        throw BuildAbortException();
    }
    // The declared type publishes what the operator's own module says it touches (design
    // effects plan 2.2): until a body is inferred, the C++ declaration is the only source of a
    // Concrete signature. The parameter classes and modes stay those of the declaration, so a
    // call derived from the type touches what the operator itself would.
    const camel::core::sem::ResourceSignature written = funcType->resources();
    const auto *declaredFacts = [&]() -> const camel::core::sem::OperatorSemantics * {
        if (const auto id = camel::core::OperatorTable::instance().lookup(uri); id.has_value()) {
            return camel::core::OperatorTable::instance().semantics(*id);
        }
        return nullptr;
    }();
    if (written.mode == camel::core::sem::ResourceSignature::Mode::Concrete) {
        // A clause on a body-less declaration is not a promise to be checked later: it is the
        // signature the module publishes, and it must be the one the operator declares (design
        // effects plan 3.2). The clause says which resources a call touches; the result, the
        // allocation and the external effect stay the operator's, so only the accesses are
        // compared.
        std::optional<camel::core::sem::ResourceSignature> declared;
        if (declaredFacts != nullptr && declaredFacts->resources) {
            declared = camel::core::sem::signatureOf(
                *declaredFacts->resources, funcType->withTypesCount());
        }
        const bool same = declared.has_value() &&
                          resourceIdentities(written, module_->name()) ==
                              resourceIdentities(*declared, module_->name()) &&
                          (written.pure() ==
                           (declared->params.empty() && declared->ambient.empty() &&
                            declared->anyOf.empty()));
        if (!same) {
            diags_->of(SemanticDiag::NativeResourceMismatch)
                .atOrigin(gct->origin())
                .commit(
                    name,
                    describeResources(written, funcType),
                    uri,
                    declared ? describeResources(*declared, funcType)
                             : std::string("nothing about its resources"));
            throw BuildAbortException();
        }
    }
    if (declaredFacts != nullptr && declaredFacts->resources) {
        funcType = funcType->withResources(camel::core::sem::signatureOf(
            *declaredFacts->resources, funcType->withTypesCount()));
    }
    // A declaration may give its parameters defaults, exactly as a function with a body does
    // (design principles 4.4): the declaration is where they are written, and the
    // rule the call resolves to is what names them. Evaluated here, once, so that a call that
    // leaves a parameter out takes the one value. The owner is the declared signature: a call to
    // a native operator recovers it from the operator group, which is all a `#op` line leaves.
    if (GCT::Tree::of(gct)->parameterDefaultsOf(gct) != nullptr) {
        evaluateDefaults(gct, funcType, funcType, false);
        if (const auto found = defaults_.find(funcType); found != defaults_.end()) {
            // The declaration keeps its defaults under its signature; a call to a native operator
            // reaches them through the group entry's URI, which is the one name the call and the
            // declaration share.
            nativeDefaults_[uri] = found->second;
        }
    }
    auto resolver = camel::core::type::DeclaredFuncTypeResolver::create(funcType);
    // A declaration stands for an operator of this module (design principles 10.1): the prefix
    // of its URI is the name the module declares, or one of the
    // protocols the module answers on (a module whose operators are not named after it, like
    // `random`, answers on `rand:`), or empty for an operator the compiler itself names
    // (`:op/...`). The protocols are the module's own statement, not the groups it exported: a
    // module that declares every operator has no group left to read them from.
    // Naming another module's operator is refused here: that operator's own module is where its
    // signature belongs, and replacing its resolver from here would be one module writing
    // another's signature.
    const std::string protocol = uri.substr(0, uri.find(':'));
    if (!protocol.empty() && protocol != where && protocol != unit_->module->name()) {
        const auto protocols = unit_->module->operatorProtocols();
        if (std::find(protocols.begin(), protocols.end(), protocol) == protocols.end()) {
            diags_->of(SemanticDiag::NativeDeclarationForeignOperator)
                .atOrigin(gct->origin())
                .commit(name, uri);
            throw BuildAbortException();
        }
    }
    const auto sameParameters = [](FunctionType *a, FunctionType *b) {
        if (a == nullptr || b == nullptr || a->withTypesCount() != b->withTypesCount() ||
            a->normTypesCount() != b->normTypesCount()) {
            return false;
        }
        for (size_t i = 0; i < a->withTypesCount(); ++i) {
            if (!a->withTypeAt(i)->equals(b->withTypeAt(i))) {
                return false;
            }
        }
        for (size_t i = 0; i < a->normTypesCount(); ++i) {
            if (!a->normTypeAt(i)->equals(b->normTypeAt(i))) {
                return false;
            }
        }
        return true;
    };
    // Two rules of a name that accept the same parameters cannot both be meant (the operator
    // group refuses them the same way); nor may a declaration repeat a function of the module.
    // Two *generic* declarations are checked alike, by what a call of the name could enter: a
    // call both accept would be decided by the order the declarations are written in, which no
    // rule of the language allows (design principles 4.3, 5.1). A fixed
    // declaration beside a generic one is no such pair: the fixed one is the more specific, and
    // a call it accepts is its own.
    //
    // Two generic declarations whose `when` conditions exclude each other are the exception
    // (design principles 4.4): "同名声明按 `when` 互斥分臂就是重载，
    // `signaturesCanAgree` 忽略 `when` 是缺陷" -- `when` is the applicability condition of an
    // overload (design principles 4.4), so each declaration covers one family of types and the
    // two may stand side by side. The conditions are asked about the types the compiler
    // registers (`separateWhenConditions`): a type that satisfies both is reported with the two
    // declarations, and a condition the compiler cannot answer leaves the pair refused as before.
    const bool generic = resolver->fixedSignature() == nullptr;
    const auto *ownWhen = GCT::Tree::of(gct)->whenOf(gct);
    const auto ask = [this](const GCT::node_ptr_t &condition, FunctionType *signature,
                            const ModuleUnit *unit,
                            const camel::core::type::type_bindings_t &bindings) {
        // The condition belongs to the declaration that wrote it and is answered in that
        // declaration's module, wherever the pair is being checked (evaluateWhenCondition).
        return evaluateWhenCondition(condition, signature, bindings, {}, nullptr, unit);
    };
    for (auto &group : unit_->nativeOperators) {
        if (group.name != name) {
            continue;
        }
        for (const auto &[otherUri, other] : group.rules) {
            if (sameParameters(other->fixedSignature(), resolver->fixedSignature())) {
                diags_->of(SemanticDiag::Redeclaration).atOrigin(gct->origin()).commit(name);
                throw BuildAbortException();
            }
            const auto *otherDeclared = dynamic_cast<const DeclaredFuncTypeResolver *>(other.get());
            if (!generic || otherDeclared == nullptr ||
                otherDeclared->fixedSignature() != nullptr) {
                continue;
            }
            if (!signaturesCanAgree(otherDeclared->funcType(), funcType)) {
                continue;
            }
            const auto *otherWhen = [&]() -> const GCT::Tree::WhenClause * {
                const auto found = nativeRuleConditions_.find(other.get());
                return found == nativeRuleConditions_.end() ? nullptr : &found->second.clause;
            }();
            const ModuleUnit *otherUnit = [&]() -> const ModuleUnit * {
                const auto found = nativeRuleConditions_.find(other.get());
                return found == nativeRuleConditions_.end() ? unit_ : found->second.unit;
            }();
            if (ownWhen != nullptr && otherWhen != nullptr) {
                SpeculativeInstantiation quiet(*this);
                std::string overlappingType;
                const ConditionSeparation separation = separateWhenConditions(
                    ownWhen->condition,
                    otherWhen->condition,
                    [&](const camel::core::type::type_bindings_t &bindings) {
                        return ask(ownWhen->condition, funcType, unit_, bindings);
                    },
                    [&](const camel::core::type::type_bindings_t &bindings) {
                        return ask(
                            otherWhen->condition, otherDeclared->funcType(), otherUnit, bindings);
                    },
                    overlappingType);
                if (separation == ConditionSeparation::Disjoint) {
                    // One family of element types each: this rule is the executor of the
                    // instances its own condition admits, and of no others (declaredRuleApplies).
                    continue;
                }
                if (separation == ConditionSeparation::Overlapping) {
                    // A rule of one name and one signature is told from its sibling by its
                    // condition alone, so the report names each declaration by both.
                    diags_->of(SemanticDiag::WhenDeclarationOverlap)
                        .atOrigin(gct->origin())
                        .commit(
                            name,
                            overlappingType,
                            otherDeclared->funcType()->toString() + " — when " + otherWhen->text,
                            funcType->toString() + " — when " + ownWhen->text);
                    throw BuildAbortException();
                }
            }
            diags_->of(SemanticDiag::NativeDeclarationOverlap)
                .atOrigin(gct->origin())
                .commit(name, otherDeclared->funcType()->toString(), funcType->toString());
            throw BuildAbortException();
        }
    }
    if (auto graphs = graphScope_->get(name, false); graphs.has_value()) {
        for (const auto &graph : **graphs) {
            if (sameParameters(graph->funcType(), resolver->fixedSignature())) {
                diags_->of(SemanticDiag::Redeclaration).atOrigin(gct->origin()).commit(name);
                throw BuildAbortException();
            }
        }
    }
    if (const auto *when = GCT::Tree::of(gct)->whenOf(gct)) {
        // The condition this rule was written with (design principles 4.4): `when` says where a
        // rule applies, and the `#impl` machinery reads it as the arms of a kernel -- the
        // instances of the function this rule is the executor of (declaredRuleApplies). A pass
        // declaration is not an operator and is read before this point, so it never reaches here.
        nativeRuleConditions_[resolver.get()] = NativeRuleCondition{*when, unit_};
    }
    auto found = std::find_if(
        unit_->nativeOperators.begin(),
        unit_->nativeOperators.end(),
        [&](const ModuleUnit::NativeOperators &group) { return group.name == name; });
    if (found == unit_->nativeOperators.end()) {
        unit_->nativeOperators.push_back({name, gct->origin(), {}});
        found = unit_->nativeOperators.end() - 1;
    }
    found->rules.emplace_back(uri, std::move(resolver));
}

void Builder::exportNativeOperators(const GCT::node_ptr_t &exportList) {
    std::unordered_set<std::string> exported;
    if (exportList != nullptr) {
        for (size_t i = 0; i < exportList->exportCount(); ++i) {
            exported.insert(Reference(exportList->exportName(i)).toString());
        }
    }
    for (auto &group : unit_->nativeOperators) {
        if (group.rules.empty()) {
            continue;
        }
        const Reference ref(group.name);
        // A declaration the module does not export is a private operator of the module: its own
        // bodies call it, an importer cannot. Either way it is the
        // module's own group, kept here and not in the program's export table. It is still an
        // operator of the module, so it is registered as a private entity: `inspect --ops` lists
        // it marked private and the reference checker verifies its signature and its purity
        // against this registration, which is what a kernel nobody exported used to escape.
        const bool isExported = exported.contains(group.name);
        std::optional<camel::core::module::entity> existing;
        if (isExported) {
            existing = unit_->module->getNativeEntity(ref);
            if (existing.has_value() && !existing->isOperGroup()) {
                // The C++ module exports something else under this name (a constant, a tag):
                // the declaration cannot also be that name.
                diags_->of(SemanticDiag::Redeclaration)
                    .atOrigin(group.origin)
                    .commit(group.name);
                throw BuildAbortException();
            }
        }
        // A fixed declaration is the more specific rule: a call that both it and a generic
        // declaration accept is its own (design principles 4.3). The
        // group answers in the order of its rules, so the fixed ones come first.
        std::stable_sort(
            group.rules.begin(), group.rules.end(), [](const auto &a, const auto &b) {
                return a.second->fixedSignature() != nullptr &&
                       b.second->fixedSignature() == nullptr;
            });
        const oper_group_ptr_t ops =
            OperatorGroup::create(group.name, std::move(group.rules));
        unit_->nativeOperatorGroups[group.name] = ops;
        if (!isExported) {
            unit_->module->declarePrivateEntity(ref, ops);
            group.rules.clear();
            continue;
        }
        if (existing.has_value()) {
            // The C++ module exports an operator group of this name too: the declaration is the
            // module's signature for it, so it replaces the group instead of being a second
            // entity beside it (design principles 6.4).
            unit_->module->replaceEntity(ref, ops);
        } else {
            unit_->module->exportEntity(ref, ops);
        }
        group.rules.clear();
    }
}

std::optional<Builder::ImplKernel> Builder::implKernelOf(const graph_set_ptr_t &graphs) const {
    if (graphs == nullptr || graphs->size() != 1) {
        return std::nullopt;
    }
    const auto found = implKernels_.find(graphs->front().get());
    if (found == implKernels_.end()) {
        return std::nullopt;
    }
    return found->second;
}

std::optional<Builder::ImplKernel> Builder::implKernelOfGraph(
    const compile_graph_ptr_t &graph) const {
    if (graph == nullptr) {
        return std::nullopt;
    }
    const auto found = implKernels_.find(graph.get());
    if (found == implKernels_.end()) {
        return std::nullopt;
    }
    return found->second;
}

// The kernel group of an `#impl` function holds exactly the one rule a direct call of the
// function resolves in, and its key is the kernel operator's URI: the declared name is what the
// group says about the call, and the kernel's own name stays where it is written.
std::string Builder::declaredNameOfKernelGroup(const oper_group_ptr_t &ops) const {
    if (ops == nullptr) {
        return {};
    }
    for (const auto &[uri, resolver] : ops->resolvers()) {
        if (const auto found = implDeclaredNames_.find(uri); found != implDeclaredNames_.end()) {
            return found->second;
        }
    }
    return {};
}

bool Builder::sameImplSignature(FunctionType *function, FunctionType *kernel) {
    if (function == nullptr || kernel == nullptr ||
        function->withTypesCount() != kernel->withTypesCount() ||
        function->normTypesCount() != kernel->normTypesCount() ||
        function->hasConstas() != kernel->hasConstas() ||
        function->hasExitType() != kernel->hasExitType()) {
        return false;
    }
    for (size_t i = 0; i < function->withTypesCount(); ++i) {
        if (function->withIsVarAt(i) != kernel->withIsVarAt(i) ||
            !function->withTypeAt(i)->equals(kernel->withTypeAt(i))) {
            return false;
        }
    }
    for (size_t i = 0; i < function->normTypesCount(); ++i) {
        if (function->normIsVarAt(i) != kernel->normIsVarAt(i) ||
            !function->normTypeAt(i)->equals(kernel->normTypeAt(i))) {
            return false;
        }
    }
    return !function->hasExitType() || function->exitType()->equals(kernel->exitType());
}

Builder::RuleVerdict Builder::ruleVerdictForCall(
    const camel::core::type::resolver_ptr_t &rule, const std::string &name,
    const type_vec_t &withTypes, const type_vec_t &normTypes, std::string *conditionText) {
    const auto found = nativeRuleConditions_.find(rule.get());
    if (found == nativeRuleConditions_.end()) {
        // No condition: the rule applies wherever its signature does.
        return RuleVerdict::Applies;
    }
    if (conditionText != nullptr) {
        *conditionText = found->second.clause.text;
    }
    // The call's argument types are what the declaration's type parameters are here, and they
    // are what its condition asks about (`is_arm_i32::<T>()`): the same bindings the rule's own
    // resolution makes, from the declaration's closed signature. A declaration whose parameters
    // do not take these arguments was never a candidate for the call, and its condition is not
    // asked about it.
    FunctionType *declared = nullptr;
    camel::core::type::type_bindings_t bindings;
    if (const auto *generic = dynamic_cast<const DeclaredFuncTypeResolver *>(rule.get());
        generic != nullptr) {
        declared = generic->funcType();
        if (declared->withTypesCount() != withTypes.size() ||
            declared->normTypesCount() != normTypes.size()) {
            return RuleVerdict::NotThisRule;
        }
        for (size_t i = 0; i < declared->withTypesCount(); ++i) {
            if (!unifyTypeVars(declared->withTypeAt(i), valueTypeOf(withTypes[i]), bindings)) {
                return RuleVerdict::NotThisRule;
            }
        }
        for (size_t i = 0; i < declared->normTypesCount(); ++i) {
            if (!unifyTypeVars(declared->normTypeAt(i), valueTypeOf(normTypes[i]), bindings)) {
                return RuleVerdict::NotThisRule;
            }
        }
    }
    // A rule has no `with` arguments of its own where a call selects it, so a condition that
    // reads one of the declaration's cannot be answered here; whatever the outcome, a condition
    // that is neither true nor false is the declaration's defect and is reported where it is
    // written, never taken for a candidate that applies.
    const WhenResult verdict = evaluateWhenCondition(
        found->second.clause.condition, declared, bindings, {}, nullptr, found->second.unit);
    if (verdict.holds()) {
        return RuleVerdict::Applies;
    }
    if (verdict.fails()) {
        return RuleVerdict::ConditionUnmet;
    }
    reportWhenProblem(verdict, name, found->second.clause.condition);
}

bool Builder::declaredRuleApplies(
    const camel::core::type::resolver_ptr_t &rule, const std::string &name,
    const type_vec_t &withTypes, const type_vec_t &normTypes) {
    // The same question a call asks, with one answer instead of three. An `#impl` instance asks
    // it after checking that the rule's signature names the instance (sameImplSignature, or the
    // rule's own resolve), so the rule's parameters always take the instance's types and
    // NotThisRule cannot arise here.
    return ruleVerdictForCall(rule, name, withTypes, normTypes) == RuleVerdict::Applies;
}

void Builder::adoptImplKernel(
    const generic_template_ptr_t &tmpl, const compile_graph_ptr_t &graph, FunctionType *funcType) {
    if (tmpl == nullptr || graph == nullptr || funcType == nullptr) {
        return;
    }
    const auto declared = implTemplateKernels_.find(tmpl.get());
    if (declared == implTemplateKernels_.end()) {
        return;
    }
    const ImplTemplateKernel &kernel = declared->second;
    // Which rule of the declaration is this instance's executor (: one operator, per-type
    // executors). A rule with a fixed signature answers for the signature it names; a rule
    // declared with the function's own type parameters is asked with the instance's parameter
    // types, and its own result type is what a call of the instance gets. A rule only applies
    // where its own condition says it does (design principles 4.4): a rule whose condition is
    // false for these types does not apply, and the next candidate is tried -- which is what
    // makes the arms of a generic kernel sayable at all, since its single rule otherwise accepts
    // every element type the function admits.
    const type_vec_t withTypes(funcType->withTypesSpan().begin(), funcType->withTypesSpan().end());
    const type_vec_t normTypes(funcType->normTypesSpan().begin(), funcType->normTypesSpan().end());
    std::string uri;
    camel::core::type::resolver_ptr_t executor;
    for (const auto &[ruleUri, resolver] : kernel.group->resolvers()) {
        if (FunctionType *fixed = resolver->fixedSignature(); fixed != nullptr) {
            if (sameImplSignature(funcType, fixed) &&
                declaredRuleApplies(resolver, kernel.name, withTypes, normTypes)) {
                uri      = ruleUri;
                executor = resolver;
                break;
            }
            continue;
        }
        if (executor != nullptr) {
            continue;
        }
        if (resolver->resolve(withTypes, normTypes).has_value() &&
            declaredRuleApplies(resolver, kernel.name, withTypes, normTypes)) {
            uri      = ruleUri;
            executor = resolver;
        }
    }
    // The instance is a semantic body of its `#impl` function whether or not an executor was
    // found for it: the body it holds is the semantics, and a consumer that asks to look inside
    // (autodiff, fusion, export, a rule) reads it rather than the kernel's own rule.
    graph->draft().setFlags(
        static_cast<uint8_t>(graph->draft().flags() | camel::runtime::kGraphFlagImplBody));
    graph->draft().setImplKernelUri(std::string(uri.empty() ? kernel.uri : uri));
    if (executor == nullptr) {
        // No executor for these types: this instance runs the body, which is the semantics and
        // gives the same result, and no direct call of it is a call of a kernel. `std::profile`
        // then shows the body's own operators and none of the kernel's.
        return;
    }
    ImplKernel adopted;
    adopted.name  = kernel.name;
    adopted.uri   = uri;
    adopted.group = OperatorGroup::create(
        kernel.group->name(),
        std::vector<std::pair<std::string, camel::core::type::resolver_ptr_t>>{
            {uri, executor}});
    // A call of the instance resolves in this group, and what it says is the function's name.
    implDeclaredNames_[uri] = tmpl->name;
    implKernels_[graph.get()] = std::move(adopted);
}

bool Builder::insideImplBody() const {
    for (DraftGraphBuilder *graph = currGraph_.get(); graph != nullptr;) {
        if (implKernels_.find(graph) != implKernels_.end()) {
            return true;
        }
        const graph_ptr_t &outer = graph->outerGraph();
        graph                    = outer ? outer.get() : nullptr;
    }
    return false;
}

void Builder::resolveImplDeclarations(const GCT::node_ptr_t &root) {
    if (root == nullptr) {
        return;
    }
    // The kernel must be another implementation of the very same function (design principles
    // 10.4): same with and normal parameters, types and `var` marks alike, and the same result
    // type. A `constas` parameter has no counterpart in a body-less declaration (design
    // principles 5.4), so a function that takes one cannot have a kernel.
    const auto sameSignature = [](FunctionType *function, FunctionType *kernel) {
        return sameImplSignature(function, kernel);
    };
    // The name of the kernel as a name of this module: a program may write it qualified with
    // the module it is declared in (`impl_demo::add_kernel`), which means the same name here.
    const auto localName = [&](const std::string &written) {
        for (const std::string &prefix :
             {unit_->module->name(), unit_->module->declaredName()}) {
            if (prefix.empty()) {
                continue;
            }
            const std::string scope = prefix + "::";
            if (written.starts_with(scope)) {
                return written.substr(scope.size());
            }
        }
        return written;
    };
    for (size_t i = 0; i < root->size(); ++i) {
        const GCT::node_ptr_t &child = root->at(i);
        if (child->type() != GCT::NodeKind::FUNC) {
            continue;
        }
        // A wrong argument count or kind is reported at the tag; the rest of the module is
        // still declared and built.
        std::optional<ImplTag> tag;
        try {
            tag = implTagOf(child);
        } catch (const BuildAbortException &) {
            continue;
        }
        if (!tag.has_value()) {
            continue;
        }
        try {
            const std::string functionName = child->name();
            if (child->nativeDeclaration()) {
                // A body-less declaration is the kernel itself: there is no body to keep.
                diags_->of(SemanticDiag::ImplOnBodylessDeclaration)
                    .atOrigin(tag->origin)
                    .commit(functionName, tag->kernel);
                throw BuildAbortException();
            }
            FunctionType *funcType = tt::as_ptr<FunctionType>(child->at(0)->dataType());
            // A generic function may carry `#impl` (rule impl-kernel-site): its kernel is a
            // body-less declaration of this module whose type parameters are its own, and which
            // rule of it runs an instance is decided at instantiation, when that instance's
            // types are known (adoptImplKernel; one operator, per-type executors). An
            // instance no rule accepts has no executor and runs the body, which is the
            // semantics, so the result is the same.
            const bool generic = isGenericHere(funcType);
            generic_ref_t genericTemplate;
            if (generic) {
                if (templateScope_ == nullptr) {
                    continue;
                }
                std::optional<generic_ref_t> declaredTemplate =
                    templateScope_->get(functionName);
                if (!declaredTemplate.has_value()) {
                    // The function of a namespace: its template is registered under the name
                    // the reference ends with (registerGenericTemplates).
                    const std::string tail = Reference(functionName).ident();
                    if (tail != functionName) {
                        declaredTemplate = templateScope_->get(tail);
                    }
                }
                if (!declaredTemplate.has_value()) {
                    continue; // its declaration was refused, and that error is reported
                }
                genericTemplate = declaredTemplate.value();
            }
            const std::string kernelName = localName(tag->kernel);
            const auto declared          = unit_->nativeOperatorGroups.find(kernelName);
            if (declared == unit_->nativeOperatorGroups.end()) {
                // A name of this module that has a body is not a kernel; a name an import
                // brings is another module's kernel, which this module cannot use.
                bool hasBody = false;
                if (const auto graphs = graphScope_->get(kernelName, false);
                    graphs.has_value()) {
                    hasBody = !graphs.value()->empty();
                }
                if (hasBody) {
                    diags_->of(SemanticDiag::ImplKernelNotBodyless)
                        .atOrigin(tag->origin)
                        .commit(functionName, tag->kernel);
                    throw BuildAbortException();
                }
                if (importedEntity(Reference(tag->kernel)).has_value()) {
                    diags_->of(SemanticDiag::ImplKernelForeign)
                        .atOrigin(tag->origin)
                        .commit(functionName, tag->kernel);
                    throw BuildAbortException();
                }
                diags_->of(SemanticDiag::ImplKernelUnknown)
                    .atOrigin(tag->origin)
                    .commit(functionName, tag->kernel);
                throw BuildAbortException();
            }
            const oper_group_ptr_t &group = declared->second;
            // The one rule of the group that implements this signature. A generic rule (one
            // with no fixed signature) is the kernel when no fixed rule matches; its call is
            // checked where the operator is resolved, by the argument types.
            std::string uri;
            camel::core::type::resolver_ptr_t kernel;
            FunctionType *shown = nullptr;
            for (const auto &[ruleUri, resolver] : group->resolvers()) {
                FunctionType *fixed = resolver->fixedSignature();
                if (fixed != nullptr && shown == nullptr) {
                    shown = fixed;
                }
                if (fixed != nullptr && sameSignature(funcType, fixed)) {
                    uri    = ruleUri;
                    kernel = resolver;
                    break;
                }
                if (fixed == nullptr && kernel == nullptr) {
                    uri    = ruleUri;
                    kernel = resolver;
                }
            }
            if (generic) {
                // Which rule runs an instance is decided at instantiation, not here: the
                // declaration is remembered whole, and every rule it has may be the executor of
                // some instance. A fixed rule is an executor for the types it names; a rule
                // with no fixed signature (the kernel declared with the function's own type
                // parameters) is asked with the instance's types there.
                if (group->resolvers().empty()) {
                    continue; // an operator with no rule can implement nothing
                }
                ImplTemplateKernel adopted;
                adopted.name  = tag->kernel;
                adopted.uri   = group->resolvers().front().first;
                adopted.group = group;
                implTemplateKernels_[genericTemplate.one().get()] = std::move(adopted);
                continue;
            }
            if (funcType->hasConstas()) {
                // A `constas` parameter is bound at the call, and a body-less declaration
                // cannot carry one (design principles 5.4): no kernel can implement it.
                diags_->of(SemanticDiag::ImplSignatureMismatch)
                    .atOrigin(tag->origin)
                    .commit(
                        tag->kernel,
                        functionName,
                        shown != nullptr ? shown->toString() : funcType->toString(),
                        funcType->toString());
                throw BuildAbortException();
            }
            if (kernel == nullptr) {
                diags_->of(SemanticDiag::ImplSignatureMismatch)
                    .atOrigin(tag->origin)
                    .commit(
                        tag->kernel,
                        functionName,
                        shown != nullptr ? shown->toString() : funcType->toString(),
                        funcType->toString());
                throw BuildAbortException();
            }
            // The rule the declaration names is this instance's executor only where the
            // declaration's own condition says it is (design principles 4.4): a non-generic
            // function's types are known here, and a rule narrowed to other types leaves this
            // instance without an executor. A signature the declaration does not name at all is
            // the misuse reported above, not a narrowing.
            camel::core::type::resolver_ptr_t executor;
            const type_vec_t withTypes(
                funcType->withTypesSpan().begin(), funcType->withTypesSpan().end());
            const type_vec_t normTypes(
                funcType->normTypesSpan().begin(), funcType->normTypesSpan().end());
            if (declaredRuleApplies(kernel, tag->kernel, withTypes, normTypes)) {
                executor = kernel;
            }
            // The graph the declaration made, by the declared type object (overloads share a
            // name and differ by it; builder.cpp, enterScope).
            compile_graph_ptr_t body;
            if (const auto graphs = graphScope_->get(functionName, false);
                graphs.has_value()) {
                for (const auto &graph : **graphs) {
                    if (graph != nullptr && graph->funcType() == funcType) {
                        body = graph;
                        break;
                    }
                }
            }
            if (body == nullptr) {
                continue; // its declaration was refused, and that error is reported
            }
            if (executor == nullptr) {
                // No executor for these types: this instance runs the body, which is the
                // semantics and gives the same result, and a direct call of the function is a
                // call of that graph rather than of a kernel (adoptImplKernel's no-executor
                // path, for the instance of a generic function).
                body->draft().setFlags(
                    static_cast<uint8_t>(body->draft().flags() | camel::runtime::kGraphFlagImplBody));
                body->draft().setImplKernelUri(std::string(uri));
                continue;
            }
            // A direct call resolves in the kernel's own one-rule group: the operator path
            // then picks the type, the conversions and the execution the kernel declares.
            ImplKernel record;
            record.name  = tag->kernel;
            record.uri   = uri;
            record.group = OperatorGroup::create(
                group->name(),
                std::vector<std::pair<std::string, camel::core::type::resolver_ptr_t>>{
                    {uri, executor}});
            // A call of the function is a call of this kernel, so a message about the call says
            // the name the function is declared under, never the kernel's own (design principles
            // 10.4: the kernel is private to the module that writes it).
            implDeclaredNames_[uri] = functionName;
            implKernels_[body.get()] = std::move(record);
            // The graph stays the semantic body: expanding the function means reading this
            // graph, and a consumer that asks for it (autodiff, fusion, export, a rule) gets
            // exactly the graph the compiler built for the Camel body.
            body->draft().setFlags(
                static_cast<uint8_t>(body->draft().flags() | camel::runtime::kGraphFlagImplBody));
            // The body names the kernel it implements. A direct call of the function is one OPER
            // of that kernel, so this is the only link back from such a call site to the
            // semantics (design principles 10.4): a consumer that must see inside the function
            // follows it, rather than reading the kernel's own derivative or lowering rule.
            body->draft().setImplKernelUri(std::string(uri));
        } catch (const BuildAbortException &) {
            // Reported at the tag; the other declarations are still read.
        } catch (Diagnostic &d) {
            diags_->add(std::move(d));
        }
    }
}

std::optional<camel::core::module::entity> Builder::ownNativeEntity(const Reference &ref) {
    // The module whose code is being built, which is not always the unit a caller is in: an
    // instance of a generic function is built in the module that declares it, and reaches that
    // module's private operators from there (instantiateGeneric, rule 3).
    if (module_ == nullptr) {
        return std::nullopt;
    }
    if (const auto unit = units_.find(module_.get()); unit != units_.end()) {
        if (const auto group = unit->second->nativeOperatorGroups.find(ref.toString());
            group != unit->second->nativeOperatorGroups.end()) {
            return camel::core::module::entity(group->second);
        }
    }
    return module_->getNativeEntity(ref);
}

void Builder::exportDeclarations(const GCT::node_ptr_t &gct, bool decorated) {
    // Whether a top-level function of this name carries decorators: its exported form is the
    // decorated function, which exists once its body is built (the second call).
    const auto isDecorated = [&](const std::string &name) {
        for (const auto &declaration : unit_->declarations) {
            if (declaration->graph == nullptr || declaration->names.front() != name) {
                continue;
            }
            if (!appliedDecoratorsOf(declaration->gct).empty()) {
                return true;
            }
        }
        return false;
    };
    bool failed = false;
    for (size_t i = 0; i < gct->exportCount(); ++i) {
        const Reference ref(gct->exportName(i));
        const std::string name = ref.toString();
        if (unit_->passDeclarations.contains(name)) {
            // A pass (design principles 9.3): its declaration is read by the pipeline, which
            // resolves the kernel by module and name. It is not an entity the module exports.
            continue;
        }
        if (isDecorated(name) != decorated) {
            continue;
        }
        if (ref.plain() && camel::core::isOverloadableOperatorGroup(ref.ident())) {
            // `export func operator +`: every module that imports this one receives its
            // overloads (Module::importedOperatorOverloads).
            exportOperatorOverloads(ref.ident(), gct);
            continue;
        }
        if (decorated) {
            auto optDecorated = decoratedGraphAt(name);
            if (!optDecorated.has_value()) {
                continue; // its body is in error, which was reported
            }
            if (const graph_set_ptr_t overloads = overloadsOfDecorated(name, *optDecorated);
                overloads != nullptr) {
                // The name stands for the decorated function and for the other functions of it
                // at once -- a call chooses among them by the parameters of what the decorators
                // returned (design principles 4.4) -- so both are exported as one entity.
                // Exporting the decorated one alone dropped the others without a word.
                exports_->exportEntity(
                    *module_,
                    ref,
                    camel::core::module::detail::EntityAccess::makeDecoratedOverloads(
                        eraseCompileGraph(optDecorated.value()), eraseCompileGraphSet(overloads)));
                continue;
            }
            exports_->exportEntity(
                *module_,
                ref,
                camel::core::module::detail::EntityAccess::makeDecoratedGraph(
                    eraseCompileGraph(optDecorated.value())));
            continue;
        }
        if (auto declared = module_->declaredTag(name); declared.has_value()) {
            // A tag is a name the module exports like any other (design principles 6.2): a module
            // that imports it gets the tag itself, so a `#` use of it resolves there.
            exports_->exportEntity(
                *module_,
                ref,
                camel::core::module::detail::EntityAccess::makeTag(*declared));
            continue;
        }
        if (auto optType = unit_->typeDecls.find(ref); optType != unit_->typeDecls.end()) {
            // A type declaration is the module's own and was exported when its construction tree
            // was built (GCT, Module::exportType); it means the same in every program, so the
            // program's table does not name it again.
            continue;
        }
        if (module_->getNativeEntity(ref).has_value()) {
            // A body-less declaration: its operator group is the module's own entity, exported
            // when the declarations of the module were read (step 1).
            continue;
        }
        if (auto value = unit_->values.find(name); value != unit_->values.end()) {
            TopLevelDeclaration &declaration = *value->second;
            if (declaration.isVar) {
                // Design principles 10.1: a `var` of the top level is written by the module's
                // own functions only, which are exported instead.
                camel::source::origin_id_t origin = gct->origin();
                for (const auto &child : *declaration.gct) {
                    if (child->type() == GCT::NodeKind::NREF && child->name() == name) {
                        origin = child->origin();
                    }
                }
                if (declaration.gct->type() == GCT::NodeKind::NREF) {
                    origin = declaration.gct->origin();
                }
                diags_->of(SemanticDiag::ExportedVariable).atOrigin(origin).commit(name);
                failed = true;
                continue;
            }
            // A value of the top level is a node of the root graph, which every module of the
            // program builds into: the importing module captures that very node, made when
            // a module first uses it (exportedValueNode).
            auto exported         = std::make_unique<ExportedValue>();
            exported->name        = name;
            exported->declaration = &declaration;
            exports_->exportEntity(
                *module_,
                ref,
                camel::core::module::detail::EntityAccess::makeNode(exported.get()));
            unit_->exportedValues.push_back(std::move(exported));
            continue;
        }
        if (auto optGraph = graphsAt(name); optGraph.has_value()) {
            exports_->exportEntity(
                *module_,
                ref,
                camel::core::module::detail::EntityAccess::makeGraphSet(
                    eraseCompileGraphSet(optGraph.value())));
            continue;
        }
        if (auto optTemplate = templateScope_->get(name); optTemplate.has_value()) {
            // A generic function has no graph to export: the templates are exported, and there
            // is one instance per set of types in the program (instantiateGeneric). A name
            // whose generic functions are several exports them all, so a call in the importing
            // module takes the overload its arguments fit (design principles 4.4, 5.5).
            exports_->exportEntity(
                *module_,
                ref,
                camel::core::module::detail::EntityAccess::makeGenericTemplates(
                    genericTemplatePayloadOf(optTemplate.value())));
            continue;
        }
        if (module_->resource(ref) != nullptr) {
            // A resource the module declares is exported like a name of another kind (design
            // principles 10.2): a module that imports it writes it bare in a clause after
            // `import { r } from m`, or through the module (`m::r`). The clause resolution reads
            // the resource and its export flag, which the declaration of the module already set,
            // so nothing of it is written into the program's table.
            continue;
        }
        if (isPoisonedName(name)) {
            failed = true;
            continue;
        }
        diags_->of(SemanticDiag::UnresolvedReference).atOrigin(gct->origin()).commit(name);
        failed = true;
    }
    if (failed) {
        throw BuildAbortException();
    }
}

} // namespace camel::compile::gir



