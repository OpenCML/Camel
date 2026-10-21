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
 * Created: Oct. 20, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
#include "camel/core/type/collection.h"
#include "camel/core/type/other.h"
#include "camel/execute/executor.h"

#include <atomic>
#include <cctype>
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
/// (design principles 4.3, 5.1; handover H94, ruling 2). Where this does not look into a type
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
            return typesCanAgree(bound->second, right, leftBindings, rightBindings);
        }
        leftBindings.emplace(var->name(), right);
        return true;
    }
    if (right->code() == TypeCode::TypeVar) {
        const auto *var = static_cast<const TypeVarType *>(right);
        if (const auto bound = rightBindings.find(var->name()); bound != rightBindings.end()) {
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
    // `#op<'module:function'>` names the operator when the name of the declaration is not the
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

void Builder::declareNativeOperator(const GCT::node_ptr_t &gct) {
    const std::string name = gct->name();
    FunctionType *funcType = tt::as_ptr<FunctionType>(gct->at(0)->dataType());
    const std::string where  = unit_->module->name();
    std::string uri          = where + ":" + name;
    if (auto tagged = nativeOperatorUri(gct); tagged.has_value()) {
        uri = std::move(*tagged);
    }
    if (uri.find(':') == std::string::npos) {
        diags_->of(SemanticDiag::InvalidOperatorURI).atOrigin(gct->origin()).commit(uri);
        throw BuildAbortException();
    }
    // The declaration is the signature of an operator of a loaded module: an operator no executor
    // answers cannot be called, and a mistake of the declaration (a wrong module, a typo) must be
    // reported where it is written rather than when the call runs (handover H19, step 1).
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
    auto resolver = camel::core::type::DeclaredFuncTypeResolver::create(funcType);
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
    // rule of the language allows (design principles 4.3, 5.1; handover H94, ruling 2). A fixed
    // declaration beside a generic one is no such pair: the fixed one is the more specific, and
    // a call it accepts is its own.
    const bool generic = resolver->fixedSignature() == nullptr;
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
            if (signaturesCanAgree(otherDeclared->funcType(), funcType)) {
                diags_->of(SemanticDiag::NativeDeclarationOverlap)
                    .atOrigin(gct->origin())
                    .commit(
                        name, otherDeclared->funcType()->toString(), funcType->toString());
                throw BuildAbortException();
            }
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
        // bodies call it, an importer cannot (handover H94, ruling 3). Either way it is the
        // module's own group, kept here and not in the program's export table.
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
        // declaration accept is its own (design principles 4.3; handover H94, ruling 2). The
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
            group.rules.clear();
            continue;
        }
        if (existing.has_value()) {
            // The C++ module exports an operator group of this name too: the declaration is the
            // module's signature for it, so it replaces the group instead of being a second
            // entity beside it (design principles 6.4; handover H94, ruling 1).
            unit_->module->replaceEntity(ref, ops);
        } else {
            unit_->module->exportEntity(ref, ops);
        }
        group.rules.clear();
    }
}

std::optional<camel::core::module::entity> Builder::ownNativeEntity(const Reference &ref) {
    // The module whose code is being built, which is not always the unit a caller is in: an
    // instance of a generic function is built in the module that declares it, and reaches that
    // module's private operators from there (instantiateGeneric; handover H94, ruling 3).
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
            if (overloadsOfDecorated(name, *optDecorated) != nullptr) {
                // An exported name is one decorated function or a set of plain ones; the set
                // of both has no exported form yet. Exporting the decorated one alone dropped
                // the others without a word.
                diags_->of(SemanticDiag::ConstructNotSupported)
                    .atOrigin(gct->origin())
                    .commit(name, "a decorated function that has overloads cannot be exported yet");
                failed = true;
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
            // when the declarations of the module were read (handover H19, step 1).
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
            // A generic function has no graph to export: the template is exported, and there
            // is one instance per set of types in the program (instantiateGeneric).
            exports_->exportEntity(
                *module_,
                ref,
                camel::core::module::detail::EntityAccess::makeGenericTemplate(
                    std::static_pointer_cast<void>(optTemplate.value())));
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
