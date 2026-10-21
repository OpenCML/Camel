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
#define DEBUG_LEVEL -1

using namespace std;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::data;
using namespace camel::core::type;

namespace camel::compile::gir {

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
    const std::string name   = gct->name();
    FunctionType *funcType   = tt::as_ptr<FunctionType>(gct->at(0)->dataType());
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
    for (auto &group : unit_->nativeOperators) {
        if (group.name != name) {
            continue;
        }
        for (const auto &[otherUri, other] : group.rules) {
            if (sameParameters(other->fixedSignature(), resolver->fixedSignature())) {
                diags_->of(SemanticDiag::Redeclaration).atOrigin(gct->origin()).commit(name);
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

void Builder::exportNativeOperators() {
    for (auto &group : unit_->nativeOperators) {
        if (group.rules.empty()) {
            continue;
        }
        const Reference ref(group.name);
        const auto existing = unit_->module->getNativeEntity(ref);
        if (existing.has_value() && !existing->isOperGroup()) {
            // The C++ module exports something else under this name (a constant, a tag): the
            // declaration cannot also be that name.
            diags_->of(SemanticDiag::Redeclaration).atOrigin(group.origin).commit(group.name);
            throw BuildAbortException();
        }
        const oper_group_ptr_t ops = OperatorGroup::create(group.name, std::move(group.rules));
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
