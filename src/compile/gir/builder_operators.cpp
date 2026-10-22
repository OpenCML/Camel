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
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Operators: the operator overloads a program declares and the graph a LINK node becomes
 * (visitLinkNode), rebinding and in-place assignment.
 */

#include "builder.h"
#include "builder_helpers.h"
#include "builder_support.h"

#include "camel/compile/gir/static_function.h"
#include "camel/core/data/primary.h"
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

#include "camel/core/operator_traits.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/other.h"

#include <algorithm>
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

// ---- Operator overloads of the program (design principles 10.2) --------------------------
//
// Where this meets the resolution of names: an operator is the one kind of name that does not
// take the first layer having it (visitDRefNode, "local > this module > imported > using >
// built-in"); operatorOverloadsOf gathers every layer instead. The layers it reads are the
// graph scopes of this module (local functions and the module's own), the module's generic
// operator templates, and ProgramExports::importedOperatorOverloads (every import of a
// module brings its operator overloads, as for a C++ module's operators). Nothing else of the
// name lookup is involved, so the lookup can change around it as long as those three stay
// reachable.

Builder::OperatorOverloads Builder::operatorOverloadsOf(const std::string &group) {
    OperatorOverloads set;
    const auto addGraph = [&](const compile_graph_ptr_t &graph, const std::string &module) {
        if (graph && std::ranges::find(set.graphs, graph) == set.graphs.end()) {
            set.graphs.push_back(graph);
            if (!module.empty()) {
                set.moduleOf[graph.get()] = module;
            }
        }
    };
    // The program's own, from the innermost scope out: every layer, not the first one that has
    // the name.
    for (graph_scope_ptr_t scope = graphScope_; scope; scope = scope->outer()) {
        if (auto graphs = scope->get(group, false); graphs.has_value() && graphs.value()) {
            for (const auto &graph : *graphs.value()) {
                addGraph(graph, {});
            }
        }
    }
    if (auto it = operatorTemplates_.find(group); it != operatorTemplates_.end()) {
        for (const auto &[declaredIn, tmpl] : it->second) {
            for (template_scope_ptr_t scope = templateScope_; scope; scope = scope->outer()) {
                if (scope == declaredIn) {
                    set.templates.push_back(tmpl);
                    break;
                }
            }
        }
    }
    // Imported (the same at every use: the imports of a module are complete before its graphs
    // are built).
    const OperatorOverloads &imported = importedOperatorOverloadsOf(group);
    for (const auto &graph : imported.graphs) {
        addGraph(graph, imported.moduleOf.at(graph.get()));
    }
    for (const auto &tmpl : imported.templates) {
        if (std::ranges::find(set.templates, tmpl) == set.templates.end()) {
            set.templates.push_back(tmpl);
            set.moduleOf[tmpl.get()] = imported.moduleOf.at(tmpl.get());
        }
    }
    set.ops = imported.ops;
    // The rules of this module's own body-less declarations are its operator overloads too
    // (design principles 10.2; handover H94, ruling 2): a call written here reaches them, and
    // the comparison of the candidates counts them. They come before what an import brings, and
    // a fixed rule before a generic one (a call both accept is the more specific rule's).
    if (module_ != nullptr) {
        if (const auto own = ownNativeEntity(Reference(group));
            own.has_value() && own->isOperGroup() && own->operGroup() != nullptr) {
            std::vector<std::pair<std::string, resolver_ptr_t>> resolvers;
            for (const auto &overload : own->operGroup()->resolvers()) {
                resolvers.push_back(overload);
            }
            for (const auto &overload : imported.ops->resolvers()) {
                const bool known = std::ranges::any_of(resolvers, [&](const auto &have) {
                    return have.first == overload.first && have.second == overload.second;
                });
                if (!known) {
                    resolvers.push_back(overload);
                }
            }
            std::stable_sort(resolvers.begin(), resolvers.end(), [](const auto &a, const auto &b) {
                return a.second->fixedSignature() != nullptr &&
                       b.second->fixedSignature() == nullptr;
            });
            set.ops = OperatorGroup::merged(group, std::move(resolvers));
        }
    }
    return set;
}

const Builder::OperatorOverloads &Builder::importedOperatorOverloadsOf(const std::string &group) {
    const auto key = std::make_pair(static_cast<const void *>(module_.get()), group);
    if (auto it = importedOperators_.find(key); it != importedOperators_.end()) {
        return it->second;
    }
    // The operator groups of the built-in module and of the C++ modules merge into one group;
    // the overloads of modules written in Camel are functions and templates.
    OperatorOverloads set;
    std::vector<std::pair<std::string, resolver_ptr_t>> resolvers;
    for (const auto &imported : exports_->importedOperatorOverloads(*module_, group)) {
        const auto &ent = imported.overloads;
        if (ent.isOperGroup()) {
            if (const auto &ops = ent.operGroup()) {
                for (const auto &overload : ops->resolvers()) {
                    const bool known = std::ranges::any_of(resolvers, [&](const auto &have) {
                        return have.first == overload.first && have.second == overload.second;
                    });
                    if (!known) {
                        resolvers.push_back(overload);
                    }
                }
            }
        } else if (camel::core::module::detail::EntityAccess::isGraphSet(ent)) {
            for (const auto &graph :
                 *asCompileGraphSet(camel::core::module::detail::EntityAccess::graphSet(ent))) {
                if (std::ranges::find(set.graphs, graph) == set.graphs.end()) {
                    set.graphs.push_back(graph);
                    set.moduleOf[graph.get()] = imported.mod->name();
                }
            }
        } else if (camel::core::module::detail::EntityAccess::isGenericTemplate(ent)) {
            auto tmpl = std::static_pointer_cast<GenericTemplate>(
                camel::core::module::detail::EntityAccess::genericTemplate(ent));
            if (std::ranges::find(set.templates, tmpl) == set.templates.end()) {
                set.templates.push_back(tmpl);
                set.moduleOf[tmpl.get()] = imported.mod->name();
            }
        }
    }
    set.ops = OperatorGroup::merged(group, std::move(resolvers));
    return importedOperators_.emplace(key, std::move(set)).first->second;
}

// ---- An operator of the module that declares the operand's type (design principles 5.7) ----
//
// Names are looked up lexically where the expression is written. A value of a dependent type
// variable is also operated on as its type's declaration means, so an instance looks for the
// operator in the module that declares the operand's named type as well, not only in the module
// the generic function is written in; what the caller imported is not looked at. The types are
// concrete by the time an instance is built, so the declaring modules come from the operand
// types. Anonymous types (no declaration), builtin types and aliases of them (no declaration of
// their own) have no declaring module and are left to the lexical scope and the built-ins.

std::vector<std::pair<std::string, std::string>> Builder::declaringModulesOf(
    const type_vec_t &withTypes, const type_vec_t &normTypes) {
    std::vector<std::pair<std::string, std::string>> found;
    const auto add = [&](Type *type) {
        type = valueTypeOf(type);
        if (type == nullptr) {
            return;
        }
        std::string module;
        std::string name;
        if (type->code() == TypeCode::Struct) {
            const auto *structType = tt::as_ptr<StructType>(type);
            if (structType->named()) {
                module = structType->decl()->moduleName();
                name   = structType->decl()->name();
            }
        } else if (const auto *enumType = asEnumType(type); enumType != nullptr) {
            module = enumType->decl()->moduleName();
            name   = enumType->decl()->name();
        }
        // The module this unit builds is already in the lexical scope of the expression.
        if (module.empty() || (module_ && module == module_->name())) {
            return;
        }
        if (std::ranges::find(found, std::make_pair(module, name)) == found.end()) {
            found.emplace_back(std::move(module), std::move(name));
        }
    };
    for (Type *type : withTypes) {
        add(type);
    }
    for (Type *type : normTypes) {
        add(type);
    }
    return found;
}

module_ptr_t Builder::moduleNamed(const std::string &name) {
    if (module_ && module_->name() == name) {
        return module_;
    }
    if (context_) {
        for (const module_ptr_t &candidate : context_->allUserModules()) {
            if (candidate && candidate->name() == name) {
                return candidate;
            }
        }
    }
    // The type came from the module, so it is loaded: this lookup never loads one.
    return nullptr;
}

std::string Builder::moduleDisplayName(const std::string &name) {
    if (const module_ptr_t module = moduleNamed(name); module) {
        return module->declaredName().empty() ? module->name() : module->declaredName();
    }
    return name;
}

bool Builder::addDeclaringModuleOverloads(
    node_handle_t dref, const std::string &group, const type_vec_t &withTypes,
    const type_vec_t &normTypes) {
    if (instantiationDepth_ == 0 || dref == nullptr) {
        // No dependent type variable: the operator is looked up where it is written, as before.
        return false;
    }
    const auto declaring = declaringModulesOf(withTypes, normTypes);
    if (declaring.empty()) {
        return false;
    }
    // `!=` has no overloads of its own: an overload of `==` gives it (design principles 4.3),
    // exactly as for the overloads the program declares (operatorDrefOf).
    const bool negated       = group == "__neq__";
    const std::string wanted = negated ? std::string("__eq__") : group;
    auto known               = operatorOverloads_.find(dref);
    if (known == operatorOverloads_.end()) {
        known = operatorOverloads_.emplace(dref, operatorOverloadsOf(group)).first;
    }
    OperatorOverloads &set = known->second;
    bool added             = false;
    for (const auto &[moduleName, typeName] : declaring) {
        const module_ptr_t module = moduleNamed(moduleName);
        if (!module) {
            continue;
        }
        for (const auto &overload : exports_->exportedOperatorOverloads(*module, wanted)) {
            const entity &exported = overload.overloads;
            if (camel::core::module::detail::EntityAccess::isGraphSet(exported)) {
                for (const auto &graph : *asCompileGraphSet(
                         camel::core::module::detail::EntityAccess::graphSet(exported))) {
                    if (std::ranges::find(set.graphs, graph) == set.graphs.end()) {
                        set.graphs.push_back(graph);
                        set.moduleOf[graph.get()] = moduleName;
                        added                     = true;
                    }
                }
            } else if (camel::core::module::detail::EntityAccess::isGenericTemplate(exported)) {
                auto generic = std::static_pointer_cast<GenericTemplate>(
                    camel::core::module::detail::EntityAccess::genericTemplate(exported));
                if (std::ranges::find(set.templates, generic) == set.templates.end()) {
                    set.templates.push_back(generic);
                    set.moduleOf[generic.get()] = moduleName;
                    added                       = true;
                }
            }
        }
    }
    if (negated && added) {
        negatedEqualities_.insert(dref);
    }
    return added;
}

bool Builder::hasDeclaringModuleOperator(
    const std::string &group, const type_vec_t &withTypes, const type_vec_t &normTypes) {
    if (instantiationDepth_ == 0) {
        return false;
    }
    for (const auto &[moduleName, typeName] : declaringModulesOf(withTypes, normTypes)) {
        if (const module_ptr_t module = moduleNamed(moduleName);
            module && !exports_->exportedOperatorOverloads(*module, group).empty()) {
            return true;
        }
    }
    return false;
}

std::string Builder::operatorSearchScopeText(
    const type_vec_t &withTypes, const type_vec_t &normTypes) {
    if (instantiationDepth_ == 0) {
        return {};
    }
    const auto declaring = declaringModulesOf(withTypes, normTypes);
    if (declaring.empty()) {
        // Nothing outside the lexical scope was searched: the usual message says it all.
        return {};
    }
    std::string where = "this module";
    if (module_ && context_ && context_->mainModule() != module_ && !module_->name().empty()) {
        where = "module '" + moduleDisplayName(module_->name()) + "'";
    }
    std::string text =
        "; searched the operators visible in " + where + " (where the expression is)";
    text += declaring.size() == 1 ? " and those of module " : " and those of modules ";
    for (size_t i = 0; i < declaring.size(); ++i) {
        if (i > 0) {
            text += i + 1 == declaring.size() ? " and " : ", ";
        }
        text += "'" + moduleDisplayName(declaring[i].first) + "' (which declares '" +
                declaring[i].second + "')";
    }
    return text;
}

bool Builder::hasProgramOperatorOverloads(const std::string &group) {
    if (!camel::core::isOverloadableOperatorGroup(group)) {
        return false;
    }
    for (graph_scope_ptr_t scope = graphScope_; scope; scope = scope->outer()) {
        if (auto graphs = scope->get(group, false);
            graphs.has_value() && graphs.value() && !graphs.value()->empty()) {
            return true;
        }
    }
    if (auto it = operatorTemplates_.find(group);
        it != operatorTemplates_.end() && !it->second.empty()) {
        return true;
    }
    const OperatorOverloads &imported = importedOperatorOverloadsOf(group);
    return !imported.graphs.empty() || !imported.templates.empty();
}

node_handle_t Builder::operatorDrefOf(const std::string &group, const GCT::node_ptr_t &gct) {
    OperatorOverloads set = operatorOverloadsOf(group);
    // `!=` is not overloaded itself: an overload of `==` gives it (design principles 4.3).
    // Where one applies, `a != b` is the negation of the call of that overload; where none
    // does, the built-in `!=` (the group of the DREF, which set.ops is not) is used as before.
    bool negated = false;
    if (group == "__neq__") {
        if (OperatorOverloads equality = operatorOverloadsOf("__eq__");
            !equality.graphs.empty() || !equality.templates.empty()) {
            set.graphs    = std::move(equality.graphs);
            set.templates = std::move(equality.templates);
            set.moduleOf  = std::move(equality.moduleOf);
            negated       = true;
        }
    }
    ASSERT(set.ops != nullptr, "An operator has no group.");
    node_handle_t drefNode{currGraph_->addDrefNode(
        std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>{set.ops})};
    registerNodeOrigin(context_, drefNode, gct, "gir.dref");
    if (recorder_) {
        std::vector<std::string> signatures;
        for (const auto &[uri, resolver] : set.ops->resolvers()) {
            signatures.push_back(resolver->signature());
        }
        for (const auto &graph : set.graphs) {
            signatures.push_back(graph->funcType()->toString());
        }
        recorder_->operatorReferenced(gct->origin(), group, std::move(signatures));
    }
    if (!set.graphs.empty() || !set.templates.empty()) {
        operatorOverloads_[drefNode] = std::move(set);
        if (negated) {
            negatedEqualities_.insert(drefNode);
        }
    }
    return drefNode;
}

bool Builder::genericOperatorAccepts(
    const template_ptr_t &tmpl, const std::vector<Argument> &normInputs,
    const type_vec_t &normTypes) {
    FunctionType *signature = tmpl->funcType;
    if (signature->withTypesCount() != 0 || signature->normTypesCount() != normTypes.size()) {
        return false;
    }
    // As resolveGenericCall: the binding occurrences, then the others, then each argument
    // against its parameter with the variables replaced.
    type_bindings_t bindings;
    for (size_t i = 0; i < normTypes.size(); ++i) {
        if (!bindTypeVars(signature->normTypeAt(i), valueTypeOf(normTypes[i]), bindings)) {
            return false;
        }
    }
    for (size_t i = 0; i < normTypes.size(); ++i) {
        unifyTypeVars(signature->normTypeAt(i), valueTypeOf(normTypes[i]), bindings);
    }
    for (size_t i = 0; i < normTypes.size(); ++i) {
        Type *wanted = substituteTypeVars(signature->normTypeAt(i), bindings);
        if (containsTypeVar(wanted)) {
            return false;
        }
        if (entersImplicitly(wanted, valueTypeOf(normTypes[i]))) {
            continue;
        }
        // An argument that is an array or struct literal enters the parameter's type (design
        // principles 5.1); resolveGenericCall makes the conversion once this template is the
        // one the call means.
        if (i < normInputs.size() &&
            compositeLiteralEntersIn(normInputs[i].node, wanted, normInputs[i].expr)) {
            continue;
        }
        return false;
    }
    return true;
}

compile_graph_ptr_t Builder::chooseOperatorOverload(
    const OperatorOverloads &set, std::vector<Argument> &withInputs,
    std::vector<Argument> &normInputs, type_vec_t &withTypes, type_vec_t &normTypes,
    const GCT::node_ptr_t &call, const std::function<bool(FunctionType *, bool)> &accepts) {
    if (!withTypes.empty()) {
        return nullptr; // an operator is never given with-arguments; the group reports it
    }
    const char *symbol = operatorSymbolOf(set.ops->name());
    std::vector<StaticArg> statics;
    for (const Argument &input : normInputs) {
        statics.push_back(staticArgOf(input.node));
    }
    const bool builtinAccepts =
        set.ops->resolve(withTypes, normTypes, statics, Modifier::None).has_value();
    // Design principles 4.3: an overload of the program is preferred where it is more
    // specific than the others that take the arguments. "More specific" is exact: its
    // parameter types are the argument types themselves (as written, without a value entering
    // `T?` / `T!`), while the others take them only as members of a family of types: a
    // built-in or C++ module operator whose type rule accepts several types (the comparison of
    // arrays, tuples and structs by content, an arithmetic over tensors), or a generic
    // overload. A built-in overload declared for exactly these types is as specific, and
    // the two stay a conflict (rejectOperatorConflict) or an ambiguity.
    const auto exactFor = [&](FunctionType *type) {
        if (type == nullptr || type->withTypesCount() != 0 ||
            type->normTypesCount() != normTypes.size()) {
            return false;
        }
        for (size_t i = 0; i < normTypes.size(); ++i) {
            if (!type->normTypeAt(i)->equals(valueTypeOf(normTypes[i]))) {
                return false;
            }
        }
        return true;
    };
    const bool builtinExact = std::ranges::any_of(set.ops->resolvers(), [&](const auto &entry) {
        FunctionType *fixed = entry.second->fixedSignature();
        return fixed != nullptr && exactFor(fixed) &&
               entry.second->resolve(withTypes, normTypes, Modifier::None).has_value();
    });
    const auto where        = [&](const void *overload) {
        const auto it = set.moduleOf.find(overload);
        return it == set.moduleOf.end() ? std::string(" (this module)")
                                        : std::format(" (module '{}')", it->second);
    };
    // Exactly as given first, then with the arguments entering `T?` / `T!` parameters (as for
    // a call of a function, visitLinkNode). The built-in overloads take only the first.
    for (const bool entering : {false, true}) {
        std::vector<compile_graph_ptr_t> graphs;
        std::vector<template_ptr_t> templates;
        for (const auto &graph : set.graphs) {
            if (accepts(graph->funcType(), entering)) {
                graphs.push_back(graph);
            }
        }
        if (!entering) {
            for (const auto &tmpl : set.templates) {
                if (genericOperatorAccepts(tmpl, normInputs, normTypes)) {
                    templates.push_back(tmpl);
                }
            }
        }
        bool useBuiltin = builtinAccepts && !entering;
        size_t fits     = graphs.size() + templates.size() + (useBuiltin ? 1 : 0);
        if (fits == 0) {
            continue;
        }
        if (fits > 1 && !entering && !builtinExact) {
            // One exact overload of the program among several that fit: it wins (above).
            std::vector<compile_graph_ptr_t> exact;
            for (const auto &graph : graphs) {
                if (exactFor(graph->funcType())) {
                    exact.push_back(graph);
                }
            }
            if (exact.size() == 1) {
                graphs     = std::move(exact);
                templates  = {};
                useBuiltin = false;
                fits       = 1;
            }
        }
        if (fits > 1) {
            // One line, as every diagnostic message is: the overloads separated by `;`.
            std::vector<std::string> overloads;
            for (const auto &graph : graphs) {
                overloads.push_back(
                    std::format(
                        "operator {}: {}{}",
                        symbol,
                        graph->funcType()->toString(),
                        where(graph.get())));
            }
            for (const auto &tmpl : templates) {
                overloads.push_back(
                    std::format(
                        "operator {}: {}{}",
                        symbol,
                        tmpl->funcType->toString(),
                        where(tmpl.get())));
            }
            if (builtinAccepts && !entering) {
                for (const auto &[uri, resolver] : set.ops->resolvers()) {
                    if (resolver->resolve(withTypes, normTypes, Modifier::None).has_value()) {
                        overloads.push_back(
                            std::format(
                                "the built-in or module operator <{}>: {}",
                                uri,
                                resolver->signature()));
                    }
                }
            }
            // At the operator: the origin of a binary call is its right operand.
            rejectAmbiguousCall(
                symbol,
                strutil::join(normTypes, ", ", [](Type *t) { return t->toString(); }),
                overloads,
                call->at(0));
        }
        if (useBuiltin) {
            return nullptr;
        }
        if (!templates.empty()) {
            return resolveGenericCall(
                templates.front(),
                withInputs,
                normInputs,
                withTypes,
                normTypes,
                call);
        }
        const compile_graph_ptr_t &chosen = graphs.front();
        if (!chosen->funcType()->hasExitType()) {
            if (failedBodies_.contains(chosen.get())) {
                throw BuildAbortException(); // its body is in error, reported there
            }
            diags_->of(SemanticDiag::CallingIncompleteFunction)
                .atOrigin(call->origin())
                .commit(displayNameOf(chosen->name()), chosen->funcType()->toString());
            throw BuildAbortException();
        }
        return chosen;
    }
    return nullptr;
}

void Builder::rejectOperatorConflict(
    const std::string &group, FunctionType *funcType, const GCT::node_ptr_t &gct) {
    if (!camel::core::isOverloadableOperatorGroup(group) ||
        !checkedOperatorSignatures_.insert(funcType).second) {
        return;
    }
    type_vec_t params;
    for (size_t i = 0; i < funcType->normTypesCount(); ++i) {
        params.push_back(funcType->normTypeAt(i));
        if (containsTypeVar(funcType->normTypeAt(i))) {
            // A generic overload: which arguments it takes is known at a call
            // (chooseOperatorOverload reports when it and another one both take them).
            return;
        }
    }
    const std::string symbol = operatorSymbolOf(group);
    const std::string types  = strutil::join(params, ", ", [](Type *t) { return t->toString(); });
    const OperatorOverloads set = operatorOverloadsOf(group);
    // A built-in (or C++ module) overload declared for exactly these parameter types is as
    // specific as this one: the two cannot both be meant. One that takes them as members of a
    // family of types (by content, a type rule over several types) is less specific, and this
    // one is preferred where both apply (design principles 4.3; chooseOperatorOverload).
    const bool sameAsBuiltin = std::ranges::any_of(set.ops->resolvers(), [&](const auto &entry) {
        const FunctionType *fixed = entry.second->fixedSignature();
        if (fixed == nullptr || fixed->withTypesCount() != 0 ||
            fixed->normTypesCount() != params.size()) {
            return false;
        }
        for (size_t i = 0; i < params.size(); ++i) {
            if (!fixed->normTypeAt(i)->equals(params[i])) {
                return false;
            }
        }
        return true;
    });
    if (sameAsBuiltin) {
        diags_->of(SemanticDiag::OperatorOverloadConflict)
            .atOrigin(gct->origin())
            .commit(symbol, types, " by the built-in operator or by an operator of a C++ module");
        throw BuildAbortException();
    }
    // An overload of another module with the same parameters (those of this module are a
    // Redeclaration, reported by rejectRedeclaredFunction).
    for (const auto &graph : set.graphs) {
        const auto module   = set.moduleOf.find(graph.get());
        FunctionType *other = graph->funcType();
        if (module == set.moduleOf.end() || other->normTypesCount() != params.size() ||
            other->withTypesCount() != 0) {
            continue;
        }
        bool same = true;
        for (size_t i = 0; same && i < params.size(); ++i) {
            same = other->normTypeAt(i)->equals(params[i]);
        }
        if (same) {
            diags_->of(SemanticDiag::OperatorOverloadConflict)
                .atOrigin(gct->origin())
                .commit(symbol, types, std::format(" by module '{}'", module->second));
            throw BuildAbortException();
        }
    }
}

void Builder::exportOperatorOverloads(const std::string &group, const GCT::node_ptr_t &gct) {
    bool exported = false;
    if (auto graphs = graphScope_->get(group, false);
        graphs.has_value() && graphs.value() && !graphs.value()->empty()) {
        exports_->exportEntity(
            *module_,
            Reference(group),
            camel::core::module::detail::EntityAccess::makeGraphSet(
                eraseCompileGraphSet(graphs.value())));
        exported = true;
    }
    // Each generic overload under a name of its own (`__add__#0`): an exported name holds one
    // entity, and a template is not a graph a set could hold.
    if (auto it = operatorTemplates_.find(group); it != operatorTemplates_.end()) {
        size_t index = 0;
        for (const auto &[declaredIn, tmpl] : it->second) {
            if (declaredIn != templateScope_) {
                continue; // declared inside a function: not the module's
            }
            const Reference ref(std::format("{}#{}", group, index++));
            exports_->exportEntity(
                *module_,
                ref,
                camel::core::module::detail::EntityAccess::makeGenericTemplate(
                    std::static_pointer_cast<void>(tmpl)));
            exported = true;
        }
    }
    if (!exported) {
        // A body-less declaration of an operator group (`export func operator ==(…): bool` in a
        // C++ module's declaration file) has no graph and no template. Its group was made from
        // the declaration and is already the module's own entity (`exportNativeOperators`), so
        // there is nothing missing here (handover H19, H141).
        if (const auto existing = module_->getNativeEntity(Reference(group));
            existing.has_value() && existing->isOperGroup()) {
            return;
        }
        diags_->of(SemanticDiag::UnresolvedReference)
            .atOrigin(gct->origin())
            .commit(displayNameOf(group));
        throw BuildAbortException();
    }
}

compile_graph_ptr_t
Builder::operatorValueGraphOf(const oper_idx_ptr_t &op, const GCT::node_ptr_t &origin) {
    FunctionType *opType = op->funcType();
    if (opType == nullptr || !opType->hasExitType()) {
        return nullptr;
    }
    for (size_t i = 0; i < opType->withTypesCount(); ++i) {
        if (opType->withIsVarAt(i)) {
            return nullptr;
        }
    }
    for (size_t i = 0; i < opType->normTypesCount(); ++i) {
        if (opType->normIsVarAt(i)) {
            return nullptr;
        }
    }
    // One per place that names the operator: the function is built where it is used.
    const std::string key = std::format(
        "{}|{}|{}",
        static_cast<const void *>(currGraph_.get()),
        op->uri(),
        opType->toString());
    if (auto known = operatorValueGraphs_.find(key); known != operatorValueGraphs_.end()) {
        return known->second;
    }
    // Its own type object (a graph completes its function type in place).
    FunctionTypeFactory factory;
    factory.setModifiers(opType->modifiers());
    factory.setExitType(opType->exitType());
    const auto nameAt = [&](size_t argIndex, const char *prefix, size_t i) {
        return argIndex < opType->argNamesCount() && !opType->argNameAt(argIndex).empty()
                   ? std::string(opType->argNameAt(argIndex))
                   : std::format("{}{}", prefix, i);
    };
    for (size_t i = 0; i < opType->withTypesCount(); ++i) {
        factory.addWithArg(nameAt(i, "__w", i), opType->withTypeAt(i), false);
    }
    for (size_t i = 0; i < opType->normTypesCount(); ++i) {
        factory.addNormArg(
            nameAt(opType->withTypesCount() + i, "__n", i),
            opType->normTypeAt(i),
            false);
    }
    FunctionType *type = factory.build();

    compile_graph_ptr_t graph = enterScope(type);
    const bool macroOperator =
        camel::core::OperatorTable::instance().isMacro(op->uri());
    if (macroOperator) {
        graph->draft().setFlags(static_cast<uint8_t>(
            graph->draft().flags() | camel::runtime::kGraphFlagMacro));
    }
    try {
        bodyStates_[graph.get()] = BodyState::Open;
        node_handle_t call       = graph->addOperNode(op, 0);
        stampPermissions(call);
        if (macroOperator) {
            setNodeMacro(call, true);
        }
        registerNodeOrigin(context_, call, origin, "gir.link.oper");
        for (draft_node_ref_t port : graph->draft().withPorts()) {
            linkNodes(LinkType::With, graph->draft().ref(port), call);
        }
        for (draft_node_ref_t port : graph->draft().normPorts()) {
            linkNodes(LinkType::Norm, graph->draft().ref(port), call);
        }
        setGraphReturnFromResult(graph, nodeIdOf(call), nodeIdOf(call), opType->exitType());
        bodyStates_[graph.get()] = BodyState::Built;
        unit_->builtBodies.push_back(graph);
    } catch (...) {
        bodyStates_[graph.get()] = BodyState::Built;
        leaveScope();
        throw;
    }
    leaveScope();
    operatorValueGraphs_[key] = graph;
    return graph;
}

node_handle_t Builder::overloadValueFor(
    node_handle_t node, FunctionType *expected, const GCT::node_ptr_t &origin) {
    if (!nodeIsKind(node, runtime::GCNodeKind::Dref) || expected == nullptr ||
        !expected->hasExitType()) {
        return nullptr;
    }
    const type_vec_t withTypes(expected->withTypesSpan().begin(), expected->withTypesSpan().end());
    const type_vec_t normTypes(expected->normTypesSpan().begin(), expected->normTypesSpan().end());
    const auto &target = nodeGraphOf(node)->drefTarget(node);
    if (std::holds_alternative<graph_vec_ptr_t>(target)) {
        auto graphs = asCompileGraphVec(target);
        compile_graph_ptr_t chosen;
        if (!graphs) {
            return nullptr;
        }
        for (const auto &g : *graphs) {
            FunctionType *type = g->funcType();
            if (decoratedOf_.contains(g.get()) || !type->hasExitType() ||
                type->withTypesCount() != withTypes.size() ||
                type->normTypesCount() != normTypes.size() ||
                !type->exitType()->equals(expected->exitType())) {
                continue;
            }
            bool same = true;
            for (size_t i = 0; same && i < withTypes.size(); ++i) {
                same = type->withTypeAt(i)->equals(withTypes[i]) && !type->withIsVarAt(i);
            }
            for (size_t i = 0; same && i < normTypes.size(); ++i) {
                same = type->normTypeAt(i)->equals(normTypes[i]) &&
                       type->normIsVarAt(i) == expected->normIsVarAt(i);
            }
            if (!same) {
                continue;
            }
            if (chosen) {
                return nullptr;
            }
            chosen = g;
        }
        return chosen ? createFuncDataNode(chosen, true, false) : nullptr;
    }
    if (std::holds_alternative<oper_group_ptr_t>(target)) {
        const auto &ops = std::get<oper_group_ptr_t>(target);
        oper_idx_ptr_t chosen;
        const auto typed = explicitOperatorTypes_.find(node);
        const type_vec_t explicitTypes =
            typed == explicitOperatorTypes_.end() ? type_vec_t{} : typed->second;
        const std::vector<StaticArg> statics(normTypes.size());
        for (const auto &[uri, resolver] : ops->resolvers()) {
            auto type = resolver->resolveForCall(
                explicitTypes,
                expected->exitType(),
                withTypes,
                normTypes,
                statics,
                Modifier::None);
            if (!type || !(*type)->hasExitType() ||
                !(*type)->exitType()->equals(expected->exitType())) {
                continue;
            }
            if (chosen) {
                return nullptr;
            }
            chosen = std::make_shared<OperatorIndex>(ops->name(), *type, uri);
        }
        if (chosen) {
            explicitOperatorTypes_.erase(node);
            if (compile_graph_ptr_t graph = operatorValueGraphOf(chosen, origin)) {
                return createFuncDataNode(graph, true, false);
            }
        }
    }
    return nullptr;
}

node_handle_t Builder::compoundAssignmentByRebinding(
    const std::string &group, const GCT::node_ptr_t &gct, const std::vector<Argument> &operands) {
    // The plain operator a compound assignment follows: the table of operator_names.cpp.
    const auto plainGroup = camel::core::plainGroupOfCompoundAssignment(group);
    if (!plainGroup || operands.size() != 2) {
        return nullptr;
    }
    const std::string plainName(*plainGroup);
    // Only for a type a module defines. The built-in types have their own operators, and
    // where one is missing (`%=` on a float, `+=` on an array) that stays an error.
    // Or for any type when the program overloads the plain operator: `m &= n` on a type of
    // the program is `m = m & n` through its `operator &` (design principles 4.2, 4.3). In a
    // generic instance the plain operator may come from the module that declares the operand's
    // type (design principles 5.7), and the rebinding follows it there too.
    Type *target = valueTypeOf(nodeTypeOf(operands[0].node));
    if (target == nullptr) {
        return nullptr;
    }
    bool plainOverloaded = hasProgramOperatorOverloads(plainName);
    if (!plainOverloaded && instantiationDepth_ > 0) {
        // Design principles 5.7: the module that declares the operand's type offers the
        // operator with it, so `x += y` follows that `+` as it follows one visible here.
        type_vec_t operandTypes;
        operandTypes.reserve(operands.size());
        for (const Argument &operand : operands) {
            operandTypes.push_back(nodeTypeOf(operand.node));
        }
        plainOverloaded = hasDeclaringModuleOperator(plainName, {}, operandTypes);
    }
    if (!target->isOtherType() && !plainOverloaded) {
        return nullptr;
    }
    const auto make = [&](GCT::node_ptr_t node) {
        node->setOrigin(gct->origin());
        return node;
    };
    // The right operand is evaluated once: it was built already, and is named here.
    const std::string right = bindTemporary(valueOf(operands[1].node));
    GCT::node_ptr_t value   = make(tree_->makeLink(2));
    *value << make(tree_->makeDRef(plainName)) << tree_->clone(operands[0].expr)
           << make(tree_->makeDRef(right));
    GCT::node_ptr_t assign = make(tree_->makeLink(2));
    *assign << make(tree_->makeDRef("__assn__")) << tree_->clone(operands[0].expr) << value;
    return visitLinkNode(assign);
}

oper_group_ptr_t Builder::importedOperatorGroupNamed(const std::string &name) {
    if (nodeAt(name).has_value() || templateScope_->get(name).has_value() ||
        decoratedGraphAt(name).has_value()) {
        return nullptr;
    }
    if (auto graphs = graphsAt(name); graphs.has_value() && !graphs.value()->empty()) {
        return nullptr;
    }
    const auto entity = importedEntity(name);
    return entity.has_value() && entity->isOperGroup() ? entity->operGroup() : nullptr;
}

node_handle_t Builder::inPlaceRebindingOf(const GCT::node_ptr_t &gct) {
    // The lowered form of `xs = xs->op<w...>` (and of `xs = op<w...>(xs)`, `xs = xs->op`):
    //   LINK(DREF __assn__, DREF xs, LINK(WITH(DREF op, w...) | DREF op, DREF xs))
    // In a sync block only: the rewritten statement is three operations on the variable,
    // which must run together and in this order.
    const auto isDref = [](const GCT::node_ptr_t &node, std::string_view name = {}) {
        return node != nullptr && node->type() == GCT::NodeKind::DREF &&
               (name.empty() || node->name() == name);
    };
    if (!synced_ || gct->size() != 3 || !isDref(gct->at(0), "__assn__") || !isDref(gct->at(1))) {
        return nullptr;
    }
    const GCT::node_ptr_t target = gct->at(1);
    const GCT::node_ptr_t call   = gct->at(2);
    if (call->type() != GCT::NodeKind::LINK || call->size() != 2 ||
        !isDref(call->at(1), target->name())) {
        return nullptr;
    }
    const GCT::node_ptr_t callee = call->at(0);
    const bool withArgs          = callee->type() == GCT::NodeKind::WITH;
    const GCT::node_ptr_t opName = withArgs ? callee->at(0) : callee;
    if (!isDref(opName)) {
        return nullptr;
    }
    // `xs` is a variable (of this function or a captured one) holding an array.
    const std::string name = target->name();
    if (!nodeAt(name).has_value()) {
        return nullptr;
    }
    const node_handle_t variable = resolveNodeByRef(name);
    Type *arrayType              = valueTypeOf(nodeTypeOf(variable));
    if (!isVariable(variable) || arrayType == nullptr || arrayType->code() != TypeCode::Array) {
        return nullptr;
    }
    // `op` is an operator of a module (nothing of the program is named so), and one of its
    // overloads declares an in-place form: anything else is an ordinary call.
    const oper_group_ptr_t group = importedOperatorGroupNamed(opName->name());
    auto &table                  = camel::core::OperatorTable::instance();
    if (group == nullptr || std::ranges::none_of(group->resolvers(), [&](const auto &overload) {
            return table.inPlaceForm(overload.first).has_value();
        })) {
        return nullptr;
    }
    // The with-arguments are plain values, built first as in the call as written and named
    // so that the call below refers to them; a name of a function, a binding or a lambda is
    // settled by the call it is passed to, and such a call is built as written.
    for (size_t i = 1; withArgs && i < callee->size(); ++i) {
        const GCT::node_ptr_t arg = callee->at(i);
        if ((isDref(arg) && !nodeAt(arg->name()).has_value()) ||
            arg->type() == GCT::NodeKind::WITH || arg->type() == GCT::NodeKind::FUNC) {
            return nullptr;
        }
    }
    std::vector<std::string> temporaries;
    type_vec_t withTypes;
    for (size_t i = 1; withArgs && i < callee->size(); ++i) {
        node_handle_t value = valueOf(visitOperand(callee->at(i), callee));
        if (nodeIsKind(value, runtime::GCNodeKind::Dref) || boundWithArgs_.contains(value)) {
            rejectNameWithoutValue(value, "the argument", callee->at(i));
        }
        withTypes.push_back(valueTypeOf(nodeTypeOf(value)));
        temporaries.push_back(bindTemporary(value));
    }
    const auto make = [&](GCT::node_ptr_t node, const GCT::node_ptr_t &at) {
        node->setOrigin(at->origin());
        return node;
    };
    // The callee as written, with the with-arguments replaced by their names.
    GCT::node_ptr_t calleeCopy = tree_->clone(opName);
    if (withArgs) {
        GCT::node_ptr_t with = make(tree_->makeWith(temporaries.size()), callee);
        *with << calleeCopy;
        for (size_t i = 0; i < temporaries.size(); ++i) {
            *with << make(tree_->makeDRef(temporaries[i]), callee->at(i + 1));
        }
        calleeCopy = with;
    }
    // The overload the call resolves to, as visitLinkNode will resolve it: rewritten only when
    // it has an in-place form and gives an array of the variable's own type.
    const std::vector<StaticArg> statics(1);
    const auto resolved       = group->resolve(withTypes, {arrayType}, statics, Modifier::None);
    const bool inPlace        = resolved.has_value() &&
                                table.inPlaceForm((*resolved)->uri()).has_value() &&
                                (*resolved)->funcType()->exitType() != nullptr &&
                                (*resolved)->funcType()->exitType()->equals(arrayType);
    GCT::node_ptr_t rebinding = make(tree_->makeLink(2), gct);
    GCT::node_ptr_t applied   = make(tree_->makeLink(1), call);
    if (!inPlace) {
        // As written (with the arguments already built): `op` reports what does not fit.
        *applied << calleeCopy << tree_->clone(call->at(1));
        *rebinding << tree_->clone(gct->at(0)) << tree_->clone(target) << applied;
        return visitLinkNode(rebinding);
    }
    GCT::node_ptr_t take = make(tree_->makeLink(1), call->at(1));
    *take << make(internalOperatorDref(camel::core::oper::VarTake), call->at(1))
          << tree_->clone(target);
    *applied << calleeCopy << take;
    inPlaceCalls_.insert(applied);
    *rebinding << make(internalOperatorDref(camel::core::oper::AssnOwned), gct->at(0))
               << tree_->clone(target) << applied;
    return visitLinkNode(rebinding);
}

namespace {

/// One place a path assignment writes: a field (`v.f`, `v.0`) or an element (`v[i]`).
struct PathStep {
    GCT::node_ptr_t node;   // the ACCS or __idx__ node, as written
    bool isIndex = false;   // a field of a struct or tuple, or an element of an array
    size_t field = 0;       // the position of the field
    std::string selector;   // the name of the index value of an element
    std::string base;       // the name of the value the step is applied to
    std::string value;      // the name of the value the step reads
};

} // namespace

node_handle_t Builder::pathAssignmentOf(const GCT::node_ptr_t &gct) {
    // `v.f = e`, `v[i] = e`, `v[i].f += e` (design principles 3.5): the whole value of the
    // variable is rebound to a `set` chain, so everything downstream sees an ordinary
    // assignment of `v`:
    //   LINK(DREF __assn__, <path>, e)  ->
    //   LINK(DREF __assn__, DREF v,
    //        LINK(DREF __set_index__, DREF v, i, LINK(DREF __set_field__, DREF v[i], f, e)))
    // Each step of the path is read once, and each prefix of it is named, so that the chain
    // refers to the values the reads gave and to the places they came from.
    const auto isDref = [](const GCT::node_ptr_t &node, std::string_view name = {}) {
        return node != nullptr && node->type() == GCT::NodeKind::DREF &&
               (name.empty() || node->name() == name);
    };
    if (gct->size() != 3 || !isDref(gct->at(0))) {
        return nullptr;
    }
    // `=` or a compound assignment (`+=`), which is the plain operator over the place's value.
    const std::string group = gct->at(0)->name();
    std::optional<std::string_view> plainGroup;
    if (group != "__assn__") {
        plainGroup = camel::core::plainGroupOfCompoundAssignment(group);
        if (!plainGroup.has_value()) {
            return nullptr;
        }
    }
    // The path, read from its leaf back to the variable it starts at, then in the order it is
    // written. A place is a field of a struct or tuple or an element of an array; anything else
    // (`s.0` on a string, `f().x`) is left to the call as written.
    std::vector<GCT::node_ptr_t> chain;
    GCT::node_ptr_t root = gct->at(1);
    while (!isDref(root)) {
        const bool element = root->type() == GCT::NodeKind::LINK && root->size() == 3 &&
                             isDref(root->at(0), "__idx__");
        if (root->type() != GCT::NodeKind::ACCS && !element) {
            return nullptr;
        }
        chain.push_back(root);
        root = element ? root->at(1) : root->at(0);
    }
    if (chain.empty()) {
        return nullptr; // `v = e`: the whole variable, which is the assignment as written
    }
    std::reverse(chain.begin(), chain.end());
    // The path starts at a variable: its whole value can be stored back into it.
    const std::string name = moduleLevelName(root->name());
    if (!nodeAt(name).has_value()) {
        return nullptr;
    }
    const node_handle_t variable = resolveNodeByRef(name);
    if (!isVariable(variable)) {
        return nullptr;
    }
    Type *rootType = valueTypeOf(nodeTypeOf(variable));
    if (rootType == nullptr) {
        return nullptr;
    }
    // The types along the path and what each step does.
    std::vector<PathStep> steps(chain.size());
    Type *type = rootType;
    for (size_t i = 0; i < chain.size(); ++i) {
        PathStep &built = steps[i];
        built.node      = chain[i];
        if (chain[i]->type() == GCT::NodeKind::ACCS) {
            if (type->code() == TypeCode::Struct) {
                const auto *structType = tt::as_ptr<StructType>(type);
                const auto index       = structType->findField(chain[i]->accessKey());
                if (!index.has_value()) {
                    return nullptr; // the read reports the field that does not exist
                }
                built.field = *index;
                type        = structType->typeAt(*index);
            } else if (type->code() == TypeCode::Tuple) {
                const auto *tupleType = tt::as_ptr<TupleType>(type);
                if (chain[i]->accessIndex() >= tupleType->size()) {
                    return nullptr; // the read reports the element past the end
                }
                built.field = chain[i]->accessIndex();
                type        = tupleType->typeAt(built.field);
            } else {
                return nullptr;
            }
        } else {
            if (type->code() != TypeCode::Array) {
                return nullptr; // an element of a tuple or struct by a runtime index
            }
            built.isIndex = true;
            type          = tt::as_ptr<ArrayType>(type)->elemType();
        }
    }
    // An array variable is written where it is when the statement is part of a sync block: the
    // outermost element set takes the variable's own array (`:var/take`) and the store keeps it
    // as the variable's, so nothing downstream copies. The read must not take the array away.
    //
    // Whether the value is shared is not asked here, and no count of edges answers it: the copy
    // is decided at run time by the cell's own mark (`:op/assn_owned` sets it, `:var/get` and
    // `:op/assn` clear it, `:var/peek` leaves it). A control edge only orders two nodes and
    // reads no value (design principles 3.3, 3.4), so adding one cannot make this statement
    // copy; see handover H95 for the measurement that shows both.
    const bool inPlace     = synced_ && rootType->code() == TypeCode::Array;
    node_handle_t rootRead = inPlace ? peekVariable(variable) : readVariable(variable);
    const auto make        = [&](GCT::node_ptr_t node, const GCT::node_ptr_t &at) {
        node->setOrigin(at->origin());
        return node;
    };
    steps.front().base = bindTemporary(rootRead);
    for (size_t i = 0; i < steps.size(); ++i) {
        PathStep &step     = steps[i];
        node_handle_t read = nullptr;
        if (step.isIndex) {
            // The index is a value of the program: evaluated once, and named for the set below.
            node_handle_t indexValue = valueOf(visitOperand(step.node->at(2), step.node));
            if (nodeIsKind(indexValue, runtime::GCNodeKind::Dref) ||
                boundWithArgs_.contains(indexValue)) {
                rejectNameWithoutValue(indexValue, "the index", step.node->at(2));
            }
            step.selector        = bindTemporary(indexValue);
            GCT::node_ptr_t call = make(tree_->makeLink(2), step.node);
            *call << make(tree_->makeDRef("__idx__"), step.node->at(0))
                  << make(tree_->makeDRef(step.base), step.node->at(1))
                  << make(tree_->makeDRef(step.selector), step.node->at(2));
            read = visitLinkNode(call);
        } else {
            GCT::node_ptr_t accs = step.node->accessByIndex()
                                       ? make(tree_->makeAccs(step.node->accessIndex()), step.node)
                                       : make(tree_->makeAccs(step.node->accessKey()), step.node);
            *accs << make(tree_->makeDRef(step.base), step.node->at(0));
            read = visitAccsNode(accs);
        }
        step.value = bindTemporary(read);
        if (i + 1 < steps.size()) {
            steps[i + 1].base = step.value;
        }
    }
    // The value written: the right-hand side for `=`, and for a compound assignment the plain
    // operator over the place's value and the right-hand side (which is therefore read once,
    // design principles 6.11). The right-hand side takes the type of the place, as it takes the
    // type of the variable in `v = e`.
    std::string assigned;
    {
        ExpectedValueScope valueScope(expectedValueType_, type);
        node_handle_t right = valueOf(visitOperand(gct->at(2), gct));
        if (nodeIsKind(right, runtime::GCNodeKind::Dref) || boundWithArgs_.contains(right)) {
            rejectNameWithoutValue(right, "the value", gct->at(2));
        }
        if (plainGroup.has_value()) {
            const std::string rightName = bindTemporary(right);
            GCT::node_ptr_t plain       = make(tree_->makeLink(2), gct);
            *plain << make(tree_->makeDRef(std::string(*plainGroup)), gct->at(0))
                   << make(tree_->makeDRef(steps.back().value), chain.back())
                   << make(tree_->makeDRef(rightName), gct->at(2));
            right = visitLinkNode(plain);
        }
        assigned = bindTemporary(right);
    }
    // The set chain, from the deepest place to the variable.
    GCT::node_ptr_t value = make(tree_->makeDRef(assigned), gct->at(2));
    for (size_t i = steps.size(); i-- > 0;) {
        const PathStep &step = steps[i];
        const bool outermost = i == 0;
        GCT::node_ptr_t call = make(tree_->makeLink(3), step.node);
        if (step.isIndex) {
            GCT::node_ptr_t applied = nullptr;
            if (outermost && inPlace) {
                applied = make(tree_->makeLink(1), step.node->at(1));
                *applied << make(
                                internalOperatorDref(camel::core::oper::VarTake),
                                step.node->at(1))
                         << tree_->clone(root);
                inPlaceCalls_.insert(call);
            } else {
                applied = make(tree_->makeDRef(step.base), step.node->at(1));
            }
            *call << make(internalOperatorDref(camel::core::oper::SetIndex), step.node->at(0))
                  << applied << make(tree_->makeDRef(step.selector), step.node->at(2)) << value;
        } else {
            GCT::node_ptr_t field = make(
                tree_->makeData(std::make_shared<LongData>(static_cast<int64_t>(step.field))),
                step.node);
            *call << make(internalOperatorDref(camel::core::oper::SetField), step.node->at(0))
                  << make(tree_->makeDRef(step.base), step.node->at(0)) << field << value;
        }
        value = call;
    }
    // The store: the variable keeps the new value, and the array as its own when it was written
    // in place (so that the next statement may write it in place again).
    GCT::node_ptr_t rebinding = make(tree_->makeLink(2), gct);
    // `__assn__` is a group of one overload per type, chosen by the operand types (an
    // assignment the program may also write as `=`): it has no single id and stays by name.
    *rebinding << make(
                   inPlace ? internalOperatorDref(camel::core::oper::AssnOwned)
                           : tree_->makeDRef(std::string(kBuiltinPrefix) + "__assn__"),
                   gct->at(0))
               << tree_->clone(root) << value;
    return visitLinkNode(rebinding);
}

node_handle_t Builder::visitLinkNode(const GCT::node_ptr_t &gct) {
    ENTER("LINK");
    Type *expectedResult = expectedValueType_;
    ExpectedValueScope callScope(expectedValueType_, nullptr);
    // Whether this call is a statement whose value nothing takes; the calls inside it are not.
    const bool unusedStatement = gct == unusedStatement_;
    unusedStatement_           = nullptr;
    if (gct->at(0)->type() == GCT::NodeKind::FUNC && gct->at(0)->decoratedBlock()) {
        node_handle_t result = visitDecoratedBlock(gct, unusedStatement);
        LEAVE("LINK");
        return result;
    }
    // `v.f = e`, `v[i] = e`: the path is lowered to a rebinding of the whole variable, whether
    // the assignment's value is taken or not.
    if (node_handle_t rebound = pathAssignmentOf(gct)) {
        LEAVE("LINK");
        return rebound;
    }
    if (unusedStatement) {
        if (node_handle_t rebound = inPlaceRebindingOf(gct)) {
            LEAVE("LINK");
            return rebound;
        }
    }
    if (gct->size() >= 1 && gct->at(0)->type() == GCT::NodeKind::DREF &&
        gct->at(0)->name().find("::") != std::string::npos) {
        // `Shape::Circle(1.0)`: the variant is built from its fields, not called (design
        // principles 5.3). A qualified name that is no variant is resolved as it was.
        EnumType *enumType = nullptr;
        size_t tag         = 0;
        if (enumVariantOfName(gct->at(0)->name(), gct->at(0), enumType, tag)) {
            node_handle_t built = visitEnumConstruction(gct, enumType, tag);
            LEAVE("LINK");
            return built;
        }
    }
    // An expression of literals only is computed now, exactly (design principles 5.1):
    // `1 + 2`, `2 ^ 10`, `0.1 + 0.2 == 0.3`. Its value takes the type this position expects,
    // and the default type of its kind when nothing does.
    if (node_handle_t folded = foldLiteralCall(gct, expectedResult)) {
        LEAVE("LINK");
        return folded;
    }
    genericCalleeAllowed_ = gct->at(0)->type() == GCT::NodeKind::DREF ||
                            gct->at(0)->type() == GCT::NodeKind::WITH ||
                            gct->at(0)->type() == GCT::NodeKind::TAPP;
    any targetNodeRes     = visit(gct->at(0));
    genericCalleeAllowed_ = false;
    // The callee may be a lambda written in place (`((x: int): int => x + 1)(3)`,
    // `v ?-> (x: int): int => x * 2`): visiting it gives its graph, and what is called is the
    // function value of that graph. (The graph was cast to a node: `bad any_cast`, bare.)
    node_handle_t targetNode = valueNodeOf(targetNodeRes);

    compile_graph_ptr_t targetGraph = nullptr;
    oper_idx_ptr_t targetOperator   = nullptr;
    FunctionType *targetFuncType    = nullptr;
    std::vector<Argument> withInputs, normInputs;
    type_vec_t withInputTypes, normInputTypes;

    if (auto bound = boundWithArgs_.find(targetNode); bound != boundWithArgs_.end()) {
        // Consumed by this call: a callee that is a value (`g<2>(x)` with `g` a variable) is
        // the same node at its next use, which must not find these arguments again.
        withInputs = std::move(bound->second);
        boundWithArgs_.erase(bound);
        boundWithCallee_.erase(targetNode);
    }
    for (const Argument &input : withInputs) {
        withInputTypes.push_back(valueTypeOf(nodeTypeOf(input.node)));
    }

    // Call results take the callee's current exit type (a recursive callee's
    // exit type is only final after its body is built), unless the node was
    // deliberately widened as a `var` binding (see visitVariNode).
    auto callResultType = [&](node_handle_t inputNode, Type *exitType) -> Type * {
        Type *recorded = nodeTypeOf(inputNode);
        if (exitType != nullptr && recorded != exitType && recorded == exitType->widened()) {
            return recorded;
        }
        return exitType;
    };
    auto callableValueType = [&](node_handle_t inputNode) -> Type * {
        if (nodeIsKind(inputNode, runtime::GCNodeKind::Func)) {
            auto callee = nodeGraphOf(inputNode)->funcTarget(inputNode);
            ASSERT(callee != nullptr, "Compile FUNC node target is null.");
            return callResultType(inputNode, callee->funcType()->exitType());
        }
        if (nodeIsKind(inputNode, runtime::GCNodeKind::Oper)) {
            return callResultType(
                inputNode,
                nodeGraphOf(inputNode)->operTarget(inputNode)->funcType()->exitType());
        }
        return nodeTypeOf(inputNode);
    };

    FunctionType *argumentSignature = signatureForArgumentContext(targetNode);
    // `v = e`: the value takes the type of the place it is written to, as inPlaceRebindingOf
    // already does for the forms it rewrites. The assignment's own signature follows the
    // place's type, so the call gives the value no expected type of its own; here only a
    // function type is handed on, for a lambda's unwritten parameter types (design principles
    // 5.9). Any other type leaves the value as it was.
    const bool isAssignment = gct->size() == 3 && gct->at(0)->type() == GCT::NodeKind::DREF &&
                              gct->at(0)->name() == "__assn__";
    Type *assignedType = nullptr;
    // The declaration of a rest function, when this call names one: its arguments are expanded
    // into a body in place of the call, and no instance is made (inlineRestCall).
    template_ptr_t restTemplate;
    if (auto generic = genericDrefs_.find(targetNode); generic != genericDrefs_.end() &&
                                                       generic->second->funcType->hasRest()) {
        restTemplate = generic->second;
    }
    const size_t restFixedCount =
        restTemplate != nullptr ? restTemplate->funcType->normTypesCount() - 1 : 0;
    for (size_t i = 1; i < gct->size(); i++) {
        Type *wanted = argumentSignature && i <= argumentSignature->normTypesCount()
                           ? argumentSignature->normTypeAt(i - 1)
                           : nullptr;
        if (restTemplate != nullptr && i - 1 >= restFixedCount) {
            wanted = nullptr; // the rest parameter takes every remaining argument
        }
        if (isAssignment && i == 2 && assignedType != nullptr) {
            wanted = assignedType;
        }
        ExpectedValueScope argumentScope(
            expectedValueType_,
            wanted && !containsTypeVar(wanted) ? wanted : nullptr);
        if (gct->at(i)->type() == GCT::NodeKind::SPRD) {
            // `..e`: the elements are arguments of their own. The marker is last, so nothing
            // follows that could be mistaken for an element.
            if (i + 1 != gct->size()) {
                const GCT::node_ptr_t operand = gct->at(i)->at(0);
                diags_->of(SemanticDiag::RestArgumentNotLast)
                    .atOrigin(operand->origin() != 0 ? operand->origin() : gct->origin())
                    .commit(
                        operand->type() == GCT::NodeKind::DREF ? operand->name()
                                                               : std::string("expression"));
                throw BuildAbortException();
            }
            const GCT::node_ptr_t operand = gct->at(i)->at(0);
            std::vector<node_handle_t> elements = expandRestArgument(
                operand,
                operand->type() == GCT::NodeKind::DREF ? operand->name()
                                                       : std::string("expression"),
                gct);
            for (const node_handle_t &element : elements) {
                normInputs.push_back({element, operand});
                normInputTypes.push_back(valueTypeOf(callableValueType(element)));
            }
            continue;
        }
        node_handle_t inputNode = visitOperand(gct->at(i), gct);
        normInputs.push_back({inputNode, gct->at(i)});
        normInputTypes.push_back(valueTypeOf(callableValueType(inputNode)));
        if (isAssignment && i == 1) {
            Type *place = valueTypeOf(nodeTypeOf(inputNode));
            if (place != nullptr && place->code() == TypeCode::Function) {
                assignedType = place;
            }
        }
    }
    if (nodeIsKind(targetNode, runtime::GCNodeKind::Dref)) {
        // An operator that declares it takes the position of its call at this many arguments
        // (CallSpec::callSiteAtArity; `error(kind, message)`: an error value records where it
        // was made, design principles 3.9) gets that position as one more, constant argument.
        const auto &drefTarget   = nodeGraphOf(targetNode)->drefTarget(targetNode);
        const auto takesCallSite = [&] {
            if (!std::holds_alternative<oper_group_ptr_t>(drefTarget)) {
                return false;
            }
            for (const auto &[uri, resolver] :
                 std::get<oper_group_ptr_t>(drefTarget)->resolvers()) {
                const auto *spec = camel::core::OperatorTable::instance().callSpec(uri);
                if (spec != nullptr && spec->callSiteAtArity >= 0 &&
                    static_cast<size_t>(spec->callSiteAtArity) + 1 == gct->size()) {
                    return true;
                }
            }
            return false;
        };
        if (takesCallSite()) {
            // The position the value records is the whole call expression: `error(k, m)`, and
            // `x->error(m)` written as a pipeline, which is the same call (design principles
            // 4.4, 3.9). The call node's own span is its argument list, so the callee and the
            // arguments together are the expression.
            const camel::source::origin_id_t where = expressionPositionOf(gct);
            node_handle_t location                 = currGraph_->addStaticDataNode(
                tt::as_shared<Data>(std::make_shared<StringData>(sourceLocationOf(where))));
            registerNodeOrigin(context_, location, gct, "gir.data");
            normInputs.push_back({location, gct});
            normInputTypes.push_back(Type::String());
        }
    }

    // An argument that is still a name without a single function value (several overloads, or
    // an operator whose signature is not one function type). Where the callee has one known
    // signature and its parameter there is a function type, the overload with exactly that
    // type is the argument. Anywhere else the name has no value: it used to be passed on as
    // a value of type void, which ended in "No matching function ... <void>" or, when the
    // callee takes anything (`println(println)`), in an internal error about a live DREF.
    {
        FunctionType *knownCalleeType = nullptr;
        if (!genericDrefs_.contains(targetNode)) {
            if (nodeIsKind(targetNode, runtime::GCNodeKind::Dref)) {
                const auto &drefTarget = nodeGraphOf(targetNode)->drefTarget(targetNode);
                if (std::holds_alternative<graph_vec_ptr_t>(drefTarget)) {
                    auto graphs = asCompileGraphVec(drefTarget);
                    if (graphs && graphs->size() == 1 &&
                        !decoratedOf_.contains(graphs->front().get())) {
                        knownCalleeType = graphs->front()->funcType();
                    }
                }
            } else if (
                Type *type = nodeTypeOf(targetNode);
                type != nullptr && type->code() == TypeCode::Function) {
                knownCalleeType = tt::as_ptr<FunctionType>(type);
            }
        }
        const auto nameOf = [](const GCT::node_ptr_t &expr) -> std::string {
            GCT::node_ptr_t at = expr;
            while (at && at->type() == GCT::NodeKind::WITH && at->size() > 0) {
                at = at->at(0);
            }
            return at && at->type() == GCT::NodeKind::DREF ? at->name() : std::string("the name");
        };
        const auto settle = [&](std::vector<Argument> &inputs, type_vec_t &types, bool with) {
            for (size_t i = 0; i < inputs.size(); ++i) {
                node_handle_t node = inputs[i].node;
                if (node == nullptr || !nodeIsKind(node, runtime::GCNodeKind::Dref)) {
                    continue;
                }
                if (genericDrefs_.contains(node)) {
                    // The name of a generic function (design principles 5.5). A callee
                    // with one known signature says which function it expects; a generic
                    // callee solves for it with its other arguments (resolveGenericCall);
                    // an operator tries the instances its other arguments suggest
                    // (settleGenericOperatorArguments). Any other callee says nothing.
                    const bool later = genericDrefs_.contains(targetNode) ||
                                       (nodeIsKind(targetNode, runtime::GCNodeKind::Dref) &&
                                        std::holds_alternative<oper_group_ptr_t>(
                                            nodeGraphOf(targetNode)->drefTarget(targetNode)));
                    if (later) {
                        continue;
                    }
                    Type *wanted = nullptr;
                    if (knownCalleeType != nullptr) {
                        const size_t count = with ? knownCalleeType->withTypesCount()
                                                  : knownCalleeType->normTypesCount();
                        if (i < count) {
                            wanted = with ? knownCalleeType->withTypeAt(i)
                                          : knownCalleeType->normTypeAt(i);
                        }
                    }
                    inputs[i].node = genericValueFor(node, wanted, inputs[i].expr);
                    types[i]       = valueTypeOf(nodeTypeOf(inputs[i].node));
                    continue;
                }
                Type *expected = nullptr;
                if (knownCalleeType != nullptr) {
                    const size_t count = with ? knownCalleeType->withTypesCount()
                                              : knownCalleeType->normTypesCount();
                    if (i < count) {
                        expected =
                            with ? knownCalleeType->withTypeAt(i) : knownCalleeType->normTypeAt(i);
                    }
                }
                node_handle_t chosen{};
                if (boundWithArgs_.contains(node)) {
                    // `apply(scale<2>, x)`: the partial application the parameter asks for.
                    chosen = partialApplicationOf(
                        node,
                        inputs[i].expr,
                        expected != nullptr && expected->code() == TypeCode::Function
                            ? tt::as_ptr<FunctionType>(expected)
                            : nullptr);
                }
                if (chosen == nullptr && expected != nullptr &&
                    expected->code() == TypeCode::Function) {
                    chosen =
                        overloadValueFor(node, tt::as_ptr<FunctionType>(expected), inputs[i].expr);
                }
                if (chosen == nullptr && !boundWithArgs_.contains(node) &&
                    nodeIsKind(targetNode, runtime::GCNodeKind::Dref) &&
                    !genericDrefs_.contains(targetNode) &&
                    std::holds_alternative<oper_group_ptr_t>(
                        nodeGraphOf(targetNode)->drefTarget(targetNode))) {
                    // A built-in operator cannot say which function it expects: it is asked
                    // about each function the name stands for
                    // (settleGenericOperatorArguments).
                    continue;
                }
                if (chosen == nullptr) {
                    rejectNameWithoutValue(node, nameOf(inputs[i].expr), inputs[i].expr);
                }
                inputs[i].node = chosen;
                types[i]       = valueTypeOf(nodeTypeOf(chosen));
            }
        };
        settle(withInputs, withInputTypes, true);
        settle(normInputs, normInputTypes, false);
    }

    // Design principles 3.8: an argument enters a `T?` / `T!` parameter by itself, `null`
    // too, and `[]` takes the element type of its parameter. A callee is first tried with the
    // argument types as they are, so that among overloads an exact match wins (`f(5)` with
    // `f(x: int)` and `f(x: int?)`); then with every argument that enters its parameter
    // counted as having the parameter's type. The CASTs are added once the callee is known
    // (enterParameters below).
    // DESIGN-QUESTION(entering-operator-args): operators are not tried this way. Their
    // parameter types are computed from the arguments by C++ resolvers, so there is no
    // target type before a resolver has accepted the arguments (`append(xs, 5)` with
    // `xs: (int?)[]` still needs `5 as int?`, and `len([])` keeps `[]` without an element
    // type). The alternative is to let a resolver name the type it would accept.
    const auto enteringTypes = [&](FunctionType *funcType,
                                   const std::vector<Argument> &inputs,
                                   const type_vec_t &types,
                                   bool with) {
        type_vec_t entered = types;
        const size_t count = with ? funcType->withTypesCount() : funcType->normTypesCount();
        for (size_t i = 0; i < entered.size() && i < count; ++i) {
            Type *param      = with ? funcType->withTypeAt(i) : funcType->normTypeAt(i);
            const bool isVar = with ? funcType->withIsVarAt(i) : funcType->normIsVarAt(i);
            if (isVar || param == nullptr || param->assignableFrom(entered[i])) {
                continue;
            }
            if (entersImplicitly(param, entered[i])) {
                entered[i] = param;
                continue;
            }
            // A literal takes the parameter's type when its value can be one of it (design
            // principles 5.1); whether the value fits is checked when the CAST is made.
            std::optional<literal::Value> value;
            if (i < inputs.size()) {
                value = literalValueOfExpr(inputs[i].expr);
            }
            if (value.has_value() && literal::kindEnters(*value, param)) {
                entered[i] = param;
                continue;
            }
            // An array or struct literal is a position with an expected type too (design
            // principles 5.1): `pick([1.0])` with `xs: f32[]`, `norm({ x: 1.0 })` with a
            // struct parameter. Its elements are converted once, from the values they were
            // written as, when the callee is known (enterParameters, coerceTo); a literal that
            // does not fit is refused there, and one that does not fit at all stays a name
            // without a matching function.
            if (i < inputs.size() &&
                compositeLiteralEntersIn(inputs[i].node, param, inputs[i].expr)) {
                entered[i] = param;
            }
        }
        return entered;
    };
    const auto accepts = [&](FunctionType *funcType, bool entering) {
        StaticFuncTypeResolver resolver(funcType);
        if (!entering) {
            return resolver.resolve(withInputTypes, normInputTypes, Modifier::None).has_value();
        }
        return resolver
            .resolve(
                enteringTypes(funcType, withInputs, withInputTypes, true),
                enteringTypes(funcType, normInputs, normInputTypes, false),
                Modifier::None)
            .has_value();
    };
    // The dimension names a call binds (design principles 5.4), remembered for the checked
    // conversion of the returned value below.
    ExtentValues dimValues;
    ExtentPacks dimPacks;
    const auto enterParameters = [&](FunctionType *funcType) {
        // The parameter types may write their extents over names the signature binds, e.g.
        // `func f(x: Tensor<f32, const [constas n]>, y: Tensor<f32, const [n]>)`. Bind each name
        // from every argument whose extent is statically known; what the arguments leave open is
        // checked when the call runs, by passing the argument through the checked conversion
        // (`as`) to the refined parameter type. Nothing here depends on the values at runtime,
        // and no second instance of the function is built.
        struct NamedExtentArg {
            Argument *input;
            type_vec_t *types;
            size_t index;
            Type *param;
            Type *actual;
        };
        std::vector<NamedExtentArg> namedArgs;
        const auto bindNamedExtents =
            [&](std::vector<Argument> &inputs, type_vec_t &types, bool with) {
                const size_t count = with ? funcType->withTypesCount() : funcType->normTypesCount();
                for (size_t i = 0; i < inputs.size() && i < count; ++i) {
                    const bool isVar = with ? funcType->withIsVarAt(i) : funcType->normIsVarAt(i);
                    Type *param      = with ? funcType->withTypeAt(i) : funcType->normTypeAt(i);
                    if (isVar || param == nullptr || types[i] == nullptr) {
                        continue;
                    }
                    // Whether the parameter states dimension names (or a pack name) is the
                    // type's answer, not a module type's: the core array type and a module
                    // type are both bound the same way here.
                    std::vector<ExtentForm> forms;
                    if (!param->extentForms(forms)) {
                        continue;
                    }
                    bool hasNames = !param->packName().empty();
                    for (const ExtentForm &form : forms) {
                        hasNames |= form.hasNames();
                    }
                    if (!hasNames) {
                        continue;
                    }
                    const size_t nameIndex  = (with ? 0 : funcType->withTypesCount()) + i;
                    const std::string label = nameIndex < funcType->argNamesCount()
                                                  ? std::string(funcType->argNameAt(nameIndex))
                                                  : std::to_string(i + 1);
                    std::string mismatch;
                    const ExtentBinding binding =
                        bindExtents(param, types[i], dimValues, dimPacks, mismatch);
                    if (binding == ExtentBinding::NotDimensioned) {
                        continue;
                    }
                    if (binding == ExtentBinding::Mismatch) {
                        diags_->of(SemanticDiag::DimensionMismatch)
                            .atOrigin(inputs[i].expr->origin())
                            .commit(label, mismatch);
                        throw BuildAbortException();
                    }
                    namedArgs.push_back({&inputs[i], &types, i, param, types[i]});
                }
            };
        bindNamedExtents(withInputs, withInputTypes, true);
        bindNamedExtents(normInputs, normInputTypes, false);
        for (const NamedExtentArg &arg : namedArgs) {
            Type *refined = const_cast<Type *>(arg.param->withExtentValues(dimValues, dimPacks));
            if (refined == nullptr || refined == arg.param || refined->equals(arg.param)) {
                continue;
            }
            if (refined->assignableFrom(arg.actual)) {
                (*arg.types)[arg.index] = refined;
                continue;
            }
            if (refined->castSafetyFrom(arg.actual) == CastSafety::Forbidden) {
                continue;
            }
            arg.input->node = castNodeTo(valueOf(arg.input->node), refined, arg.input->expr);
            (*arg.types)[arg.index] = refined;
        }
        const auto enter = [&](std::vector<Argument> &inputs, type_vec_t &types, bool with) {
            const size_t count = with ? funcType->withTypesCount() : funcType->normTypesCount();
            for (size_t i = 0; i < inputs.size() && i < count; ++i) {
                Type *param      = with ? funcType->withTypeAt(i) : funcType->normTypeAt(i);
                const bool isVar = with ? funcType->withIsVarAt(i) : funcType->normIsVarAt(i);
                if (isVar || param == nullptr || containsTypeVar(param) ||
                    param->assignableFrom(types[i])) {
                    continue;
                }
                if (node_handle_t entered = checkValue(inputs[i].node, param, inputs[i].expr)) {
                    inputs[i].node = entered;
                    types[i]       = param;
                }
            }
        };
        enter(withInputs, withInputTypes, true);
        enter(normInputs, normInputTypes, false);
    };

    // A call of a decorated function calls the function its decorators returned, so the
    // arguments are checked against that function's parameters (they were not checked at
    // all: a float passed where the decorated function takes an int was read as an int).
    const auto callDecorated = [&](const compile_graph_ptr_t &decoratedGraph) {
        node_handle_t decoratorInvoke = lowerDecoratedGraphValue(decoratedGraph, gct);
        FunctionType *factoryType     = tt::as_ptr<FunctionType>(nodeTypeOf(decoratorInvoke));
        if (!accepts(factoryType, false) && !accepts(factoryType, true)) {
            reportConversionGaps(
                {{std::string(), factoryType}},
                withInputs,
                normInputs,
                withInputTypes,
                normInputTypes,
                gct);
            diags_->of(SemanticDiag::ArgumentsMismatch)
                .atOrigin(gct->origin())
                .commit(
                    factoryType->toString(),
                    std::format(
                        "<{}> ({})",
                        strutil::join(withInputTypes, ", ", [](Type *t) { return t->toString(); }),
                        strutil::join(normInputTypes, ", ", [](Type *t) {
                            return t->toString();
                        })));
            throw BuildAbortException();
        }
        targetNode = currGraph_->addCallNode(factoryType->exitType());
        linkNodes(LinkType::With, decoratorInvoke, targetNode);
        // The call is written in the program: it is located where it is written, not where
        // the function's decorators left it.
        registerCallExpressionOrigin(context_, targetNode, gct, "gir.link.call");
        targetFuncType = factoryType;
    };

    // Design principles 5.7: an instance also uses the operators of the module that declares
    // the operand's type, so a module can offer its type and its operators together. The types
    // of the instance are concrete now, and the set is built from them, so an overload found
    // both there and through an import of this module stays one candidate.
    if (nodeIsKind(targetNode, runtime::GCNodeKind::Dref)) {
        const auto &drefTarget = nodeGraphOf(targetNode)->drefTarget(targetNode);
        if (std::holds_alternative<oper_group_ptr_t>(drefTarget)) {
            const auto &ops = std::get<oper_group_ptr_t>(drefTarget);
            addDeclaringModuleOverloads(
                targetNode, ops->name(), withInputTypes, normInputTypes);
        }
    }

    // An operator with overloads of the program: one of them, or the built-in group below.
    // `a != b` by an overload of `==` is the negation of that call (`negateResult`).
    compile_graph_ptr_t operatorOverload = nullptr;
    bool negateResult                    = false;
    if (auto overloads = operatorOverloads_.find(targetNode);
        overloads != operatorOverloads_.end()) {
        operatorOverload = chooseOperatorOverload(
            overloads->second,
            withInputs,
            normInputs,
            withInputTypes,
            normInputTypes,
            gct,
            accepts);
    }
    if (operatorOverload != nullptr) {
        negateResult = negatedEqualities_.contains(targetNode);
        targetGraph  = operatorOverload;
        currGraph_->addDependencyGraph(targetGraph);
        targetNode     = createFuncDataNode(targetGraph, false, true);
        targetFuncType = targetGraph->funcType();
    } else if (restTemplate != nullptr) {
        // A rest function is expanded here, into the caller's graph: its fixed parameters take
        // the arguments and the rest parameter takes the nodes left over. No call node is
        // made and the function is never an instance.
        return inlineRestCall(restTemplate, withInputs, normInputs, gct, expectedResult);
    } else if (auto generic = genericDrefs_.find(targetNode); generic != genericDrefs_.end()) {
        // A call of a generic function: the argument types select (and, the first time,
        // build) the instance, which is then called like any other function.
        targetGraph = resolveGenericCall(
            generic->second,
            withInputs,
            normInputs,
            withInputTypes,
            normInputTypes,
            gct,
            expectedResult);
        if (!targetGraph->funcType()->hasExitType()) {
            diags_->of(SemanticDiag::CallingIncompleteFunction)
                .atOrigin(gct->origin())
                .commit(targetGraph->name(), targetGraph->funcType()->toString());
            throw BuildAbortException();
        }
        currGraph_->addDependencyGraph(targetGraph);
        targetNode     = createFuncDataNode(targetGraph, false, true);
        targetFuncType = targetGraph->funcType();
    } else if (nodeIsKind(targetNode, runtime::GCNodeKind::Dref)) {
        const auto &drefTarget = nodeGraphOf(targetNode)->drefTarget(targetNode);
        if (std::holds_alternative<graph_vec_ptr_t>(drefTarget)) {
            auto graphs = asCompileGraphVec(drefTarget);
            // Overloads are tried in two passes (design principles 5.1): first with the
            // argument types as they are -- a literal has not entered any parameter type yet
            // -- and then with every literal entering the type of the parameter it is given
            // to. Several candidates in the pass that is used is an ambiguity, never settled
            // by declaration order (H27e); the same report is made for `f<w>` used as a value
            // (partialApplicationOf).
            // Only the chosen one has to be complete: an overload the call does not mean may
            // still be waiting for its return type.
            // A decorated overload is what its decorators return: it is chosen by that
            // function's parameters and called through it.
            compile_graph_ptr_t chosenDecorated = nullptr;
            const auto signatureOf = [&](const compile_graph_ptr_t &g) -> FunctionType * {
                if (auto it = decoratedOf_.find(g.get()); it != decoratedOf_.end()) {
                    return tt::as_ptr<FunctionType>(it->second->funcType()->exitType());
                }
                return g->funcType();
            };
            // The arguments as written, for the ambiguity report: the context arguments
            // first, then the ordinary ones.
            std::vector<std::string> argumentTypeTexts;
            for (Type *type : withInputTypes) {
                argumentTypeTexts.push_back(type->toString());
            }
            for (Type *type : normInputTypes) {
                argumentTypeTexts.push_back(type->toString());
            }
            const std::string argumentTypes =
                strutil::join(argumentTypeTexts, ", ", [](const std::string &s) { return s; });
            for (const bool entering : {false, true}) {
                if (targetGraph || chosenDecorated) {
                    break;
                }
                std::vector<compile_graph_ptr_t> fitted;
                for (const auto &g : *graphs) {
                    FunctionType *funcType = signatureOf(g);
                    if (!accepts(funcType, entering)) {
                        continue;
                    }
                    // A function of the top level not built yet is built now: its return type
                    // may come from its body, its decorated form exists once it is built (step
                    // two of loading builds on demand).
                    ensureFunctionBuilt(g);
                    fitted.push_back(g);
                }
                if (fitted.empty()) {
                    continue;
                }
                if (fitted.size() > 1) {
                    // Both (or more) take the arguments in this pass: no declaration order
                    // chooses between them. The signatures shown are the ones the call would
                    // be made with, decorated forms included.
                    std::vector<FunctionType *> candidates;
                    candidates.reserve(fitted.size());
                    for (const auto &g : fitted) {
                        candidates.push_back(signatureOf(g));
                    }
                    rejectAmbiguousCall(
                        graphs->front()->name(), argumentTypes, candidates, gct);
                }
                const compile_graph_ptr_t &g = fitted.front();
                FunctionType *funcType       = signatureOf(g);
                if (auto it = decoratedOf_.find(g.get()); it != decoratedOf_.end()) {
                    chosenDecorated = it->second;
                    break;
                }
                if (!funcType->hasExitType()) {
                    if (failedBodies_.contains(g.get())) {
                        // Its body is in error, which was reported there.
                        throw BuildAbortException();
                    }
                    diags_->of(SemanticDiag::CallingIncompleteFunction)
                        .atOrigin(gct->origin())
                        .commit(g->name(), funcType->toString());
                    throw BuildAbortException();
                }
                targetGraph = g;
            }
            if (!targetGraph && !chosenDecorated) {
                std::vector<std::pair<std::string, FunctionType *>> candidates;
                for (const auto &g : *graphs) {
                    candidates.emplace_back(g->name(), signatureOf(g));
                }
                reportConversionGaps(
                    candidates,
                    withInputs,
                    normInputs,
                    withInputTypes,
                    normInputTypes,
                    gct);
                std::string argTypesStr = std::format(
                    "<{}> ({})",
                    strutil::join(withInputTypes, ", ", [](Type *t) { return t->toString(); }),
                    strutil::join(normInputTypes, ", ", [](Type *t) { return t->toString(); }));
                // Every argument written in the parentheses of a function that takes some of
                // them in angle brackets: say which, and how the call is written.
                if (withInputTypes.empty()) {
                    for (const auto &g : *graphs) {
                        FunctionType *type     = signatureOf(g);
                        const size_t normCount = type->normTypesCount();
                        const size_t withCount = type->withTypesCount();
                        bool fits              = withCount > 0 && normCount > 0 &&
                                                 withCount + normCount == normInputTypes.size();
                        for (size_t i = 0; fits && i < normInputTypes.size(); ++i) {
                            Type *param = i < normCount ? type->normTypeAt(i)
                                                        : type->withTypeAt(i - normCount);
                            fits = containsTypeVar(param) || param->equals(normInputTypes[i]) ||
                                   param->assignableFrom(normInputTypes[i]);
                        }
                        if (fits) {
                            argTypesStr += withArgumentsHint(g->name(), normInputs, normCount);
                            break;
                        }
                    }
                }
                std::string overloadsStr =
                    "\n    " +
                    strutil::join(
                        *graphs,
                        "\n    ",
                        [&](const graph_ptr_t &g) {
                            return g->name() + ": " + signatureOf(g)->toString();
                        }) +
                    "\n";
                diags_->of(SemanticDiag::NoMatchingFunction)
                    .atOrigin(gct->origin())
                    .commit(argTypesStr, overloadsStr);
                throw BuildAbortException();
            }
            if (chosenDecorated) {
                callDecorated(chosenDecorated);
            } else {
                currGraph_->addDependencyGraph(targetGraph);
                targetNode     = createFuncDataNode(targetGraph, false, true);
                targetFuncType = targetGraph->funcType();
            }
        } else if (std::holds_alternative<oper_group_ptr_t>(drefTarget)) {
            auto ops = std::get<oper_group_ptr_t>(drefTarget);
            if (auto expanded = explicitOperatorTypes_.contains(targetNode)
                                    ? nullptr
                                    : expandTreeCall(ops, withInputs, normInputs, gct)) {
                LEAVE("LINK");
                return expanded;
            }
            settleGenericOperatorArguments(
                ops,
                withInputs,
                normInputs,
                withInputTypes,
                normInputTypes,
                gct);
            // A name the operator did not settle has no value.
            for (auto *inputs : {&withInputs, &normInputs}) {
                for (Argument &input : *inputs) {
                    if (nodeIsKind(input.node, runtime::GCNodeKind::Dref)) {
                        rejectNameWithoutValue(
                            input.node,
                            input.expr->type() == GCT::NodeKind::DREF ? input.expr->name()
                                                                      : std::string("the name"),
                            input.expr);
                    }
                }
            }
            // Design principles 5.1: a literal operand takes the type of the other operand
            // (`x * 2 + 1` with `x: f32`). Done before the resolvers see the arguments,
            // because a resolver matches types exactly. Only a binary arithmetic or
            // comparison operator relates its operands so: the operands of a tuple or an
            // array are independent, and a with-argument has a parameter of its own.
            if (withInputs.empty() && normInputs.size() == 2) {
                adaptLiteralOperands(ops->name(), normInputs, normInputTypes, gct);
            }
            // Constant arguments (shape literals, axes) let operators refine their result type.
            std::vector<StaticArg> normStatics;
            normStatics.reserve(normInputs.size());
            for (const Argument &input : normInputs) {
                normStatics.push_back(staticArgOf(input.node));
            }
            type_vec_t explicitTypes;
            if (auto typed = explicitOperatorTypes_.find(targetNode);
                typed != explicitOperatorTypes_.end()) {
                explicitTypes = std::move(typed->second);
                explicitOperatorTypes_.erase(typed);
            }
            const auto &res = ops->resolveForCall(
                explicitTypes,
                expectedResult,
                withInputTypes,
                normInputTypes,
                normStatics,
                Modifier::None);
            if (!res.has_value() && withInputs.empty()) {
                // `x == null` / `x != null` over a `T?`: the tag of `x`, not a comparison of
                // two values (design principles 3.8).
                if (node_handle_t test =
                        nullComparison(ops->name(), gct, normInputs, normInputTypes)) {
                    LEAVE("LINK");
                    return test;
                }
            }
            if (!res.has_value() && withInputs.empty()) {
                // `t += u` on a tensor: no operator changes the tensor in place; the new
                // tensor is computed and `t` bound to it, as for a number.
                if (node_handle_t rebound =
                        compoundAssignmentByRebinding(ops->name(), gct, normInputs)) {
                    LEAVE("LINK");
                    return rebound;
                }
            }
            if (!res.has_value()) {
                std::string argTypesStr = std::format(
                    "<{}> ({})",
                    strutil::join(withInputTypes, ", ", [](Type *t) { return t->toString(); }),
                    strutil::join(normInputTypes, ", ", [](Type *t) { return t->toString(); }));
                // An overload whose parameter kinds fit may know the precise reason (e.g. a
                // static tensor shape conflict); report that instead of "no match". A rule may
                // also name its own diagnostic for the rejection, when the generic one would not
                // say the right thing (grad's NotDifferentiable, 9.9).
                std::vector<std::string> reasons;
                std::optional<SemanticDiag> dedicated;
                bool mixedDiagnostics = false;
                for (const auto &[uri, resolver] : ops->resolvers()) {
                    if (auto reason = resolver->explainCallRejection(
                            explicitTypes,
                            expectedResult,
                            withInputTypes,
                            normInputTypes,
                            normStatics,
                            Modifier::None)) {
                        if (std::ranges::find(reasons, *reason) == reasons.end()) {
                            if (auto diag = resolver->rejectionDiag()) {
                                if (dedicated.has_value() && *dedicated != *diag) {
                                    mixedDiagnostics = true;
                                } else {
                                    dedicated = diag;
                                }
                            } else {
                                mixedDiagnostics = true;
                            }
                            reasons.push_back(std::move(*reason));
                        }
                    }
                }
                if (!reasons.empty()) {
                    if (dedicated.has_value() && !mixedDiagnostics) {
                        diags_->of(*dedicated)
                            .atOrigin(gct->origin())
                            .commit(strutil::join(reasons, "; "));
                    } else {
                        diags_->of(SemanticDiag::ArgumentsRejected)
                            .atOrigin(gct->origin())
                            .commit(ops->name(), argTypesStr, strutil::join(reasons, "; "));
                    }
                    throw BuildAbortException();
                }
                // An operator written in the program: say which, the group name is not what
                // the program wrote.
                if (const char *symbol = operatorSymbolOf(ops->name())) {
                    argTypesStr += std::format(
                        "; operator '{}' is not defined for these operand types",
                        symbol);
                }
                // Where it was looked for, when that went beyond the lexical scope: an
                // operator of the type's declaring module was searched for and not found
                // (design principles 5.7), so the message names the modules involved.
                argTypesStr += operatorSearchScopeText(withInputTypes, normInputTypes);
                // Every argument written in the parentheses of an operator that takes some of
                // them in angle brackets (`split('a,b', ',')`, `put(m, k, v)`): if the call
                // resolves with the trailing arguments moved there, say so.
                if (withInputTypes.empty() && operatorSymbolOf(ops->name()) == nullptr) {
                    for (size_t normCount = 1; normCount < normInputTypes.size(); ++normCount) {
                        const type_vec_t asNorm(
                            normInputTypes.begin(),
                            normInputTypes.begin() + static_cast<std::ptrdiff_t>(normCount));
                        const type_vec_t asWith(
                            normInputTypes.begin() + static_cast<std::ptrdiff_t>(normCount),
                            normInputTypes.end());
                        const std::vector<StaticArg> statics(
                            normStatics.begin(),
                            normStatics.begin() + static_cast<std::ptrdiff_t>(normCount));
                        if (ops->resolve(asWith, asNorm, statics, Modifier::None).has_value()) {
                            argTypesStr += withArgumentsHint(ops->name(), normInputs, normCount);
                            break;
                        }
                    }
                }
                std::string overloadsStr =
                    "\n    " +
                    strutil::join(
                        ops->resolvers(),
                        "\n    ",
                        [](const std::pair<std::string, resolver_ptr_t> &p) {
                            return "<" + p.first + ">: " + p.second->signature();
                        }) +
                    "\n";
                // The overloads of the program are part of the set (design principles 10.2).
                if (auto overloads = operatorOverloads_.find(targetNode);
                    overloads != operatorOverloads_.end()) {
                    for (const auto &graph : overloads->second.graphs) {
                        overloadsStr += "    " + displayNameOf(graph->name()) + ": " +
                                        graph->funcType()->toString() + "\n";
                    }
                    for (const auto &tmpl : overloads->second.templates) {
                        overloadsStr += "    " + displayNameOf(tmpl->name) + ": " +
                                        tmpl->funcType->toString() + "\n";
                    }
                }
                diags_->of(SemanticDiag::NoMatchingFunction)
                    .atOrigin(gct->origin())
                    .commit(argTypesStr, overloadsStr);
                throw BuildAbortException();
            }
            targetOperator = *res;
            if (inPlaceCalls_.contains(gct)) {
                // Written by inPlaceRebindingOf, which checked that this overload has an
                // in-place form: the call works on the array :var/take hands it.
                const auto form =
                    camel::core::OperatorTable::instance().inPlaceForm(targetOperator->uri());
                ASSERT(form.has_value(), "An in-place rebinding lost its in-place form.");
                targetOperator = std::make_shared<OperatorIndex>(
                    targetOperator->name(),
                    targetOperator->funcType(),
                    *form);
            }
            rejectLiteralConversion(ops->name(), targetOperator->funcType(), normInputs, gct);
            // `foreach` drops what its element function gives: the value of the body of a
            // function written in place here is a value nothing takes (design principles
            // 11.3). A function named here may be used elsewhere, where its value counts.
            // The operator says so itself (CallSpec::discardsCallbackResult).
            const auto *callSpec =
                camel::core::OperatorTable::instance().callSpec(targetOperator->uri());
            if (callSpec != nullptr && callSpec->discardsCallbackResult) {
                for (const Argument &input : withInputs) {
                    if (input.expr == nullptr) {
                        continue;
                    }
                    if (auto element = lambdaGraphs_.find(input.expr);
                        element != lambdaGraphs_.end()) {
                        rejectDiscardedValue(element->second);
                    }
                }
            }
            // A constant format string is checked against the arguments it formats now, not
            // when the program runs (CallSpec::checkFormat; design principles 5.4).
            if (callSpec != nullptr && callSpec->checkFormat) {
                const bool patternInWith = callSpec->formatWithArg >= 0;
                const auto &patterns     = patternInWith ? withInputs : normInputs;
                const size_t at          = static_cast<size_t>(
                    patternInWith ? callSpec->formatWithArg : callSpec->formatNormArg);
                if (at < patterns.size()) {
                    const StaticArg pattern = staticArgOf(patterns[at].node);
                    if (pattern.value.has_value() && *pattern.value != NullSlot &&
                        valueTypeOf(nodeTypeOf(patterns[at].node))->code() == TypeCode::String) {
                        const std::string_view text =
                            rtdata::fromSlot<const ::String *>(*pattern.value)->view();
                        const type_vec_t &formatted =
                            patternInWith ? normInputTypes : withInputTypes;
                        if (const auto problem = callSpec->checkFormat(text, formatted)) {
                            // A formatted string was lowered to this call: its pattern has one
                            // placeholder per interpolation, so the specification that does
                            // not fit is reported at the expression it was written for.
                            const auto &arguments = patternInWith ? normInputs : withInputs;
                            if (patterns[at].expr != nullptr &&
                                patterns[at].expr->type() == GCT::NodeKind::DATA &&
                                patterns[at].expr->fromFormattedString()) {
                                const auto placeholders = formatPlaceholders(text);
                                const size_t count =
                                    std::min(placeholders.size(), arguments.size());
                                for (size_t i = 0; i < count; ++i) {
                                    if (arguments[i].expr == nullptr) {
                                        continue;
                                    }
                                    const auto part = callSpec->checkFormat(
                                        placeholders[i],
                                        type_vec_t{formatted[i]});
                                    if (!part) {
                                        continue;
                                    }
                                    auto rejection = diags_->of(SemanticDiag::ArgumentsRejected)
                                                         .atOrigin(arguments[i].expr->origin());
                                    // The first ':' outside every bracket ends the expression,
                                    // so the ':' of a lambda's return type can be read as the
                                    // start of the specification. The fix is parentheses around
                                    // the lambda (H20 item 1; the parse-side twin is in
                                    // ast/builder.cpp, for the case where that cut leaves the
                                    // expression incomplete).
                                    if (placeholders[i].find("=>") != std::string::npos) {
                                        rejection.note(
                                            "a lambda with a return type needs its own "
                                            "parentheses at the top level of an interpolation: "
                                            "the ':' of the return type is read as the start of "
                                            "the format specification. Write it as "
                                            "${((x: int): int => x)}");
                                    }
                                    rejection.commit(
                                        ops->name(),
                                        "format specifier '" + placeholders[i] +
                                            "' of an interpolation",
                                        *part);
                                    throw BuildAbortException();
                                }
                            }
                            diags_->of(SemanticDiag::ArgumentsRejected)
                                .atOrigin(patterns[at].expr->origin())
                                .commit(
                                    ops->name(),
                                    "format string '" + std::string(text) + "'",
                                    *problem);
                            throw BuildAbortException();
                        }
                    }
                }
            }
            // An operator calls a function argument with values. A function with a `var`
            // parameter needs a variable there, which an operator does not have. A macro
            // operator (grad, with_grad, ...) does not call its argument: it rewrites the
            // function's graph into another function, whose `var` parameters are passed by
            // the caller of that function as usual.
            const auto takesVariable = [](Type *type) {
                if (type == nullptr || type->code() != TypeCode::Function) {
                    return false;
                }
                const auto *func = tt::as_ptr<FunctionType>(type);
                for (size_t i = 0; i < func->normTypesCount(); ++i) {
                    if (func->normIsVarAt(i)) {
                        return true;
                    }
                }
                for (size_t i = 0; i < func->withTypesCount(); ++i) {
                    if (func->withIsVarAt(i)) {
                        return true;
                    }
                }
                return false;
            };
            const bool targetIsMacro =
                camel::core::OperatorTable::instance().isMacro(targetOperator->uri());
            const bool callsArguments = !targetIsMacro;
            for (const auto *inputs : {&withInputs, &normInputs}) {
                for (const Argument &input : *inputs) {
                    Type *type = valueTypeOf(nodeTypeOf(input.node));
                    if (callsArguments && takesVariable(type)) {
                        diags_->of(SemanticDiag::ArgumentsRejected)
                            .atOrigin(input.expr->origin())
                            .commit(
                                ops->name(),
                                type->toString(),
                                "an operator calls a function argument with values, and a "
                                "function with a var parameter must be given a variable");
                        throw BuildAbortException();
                    }
                }
            }
            targetNode = currGraph_->addOperNode(targetOperator, 0);
            stampPermissions(targetNode);
            // An operator declared macro runs in std::macro once its inputs are static, like a call
            // of a macro function.
            if (targetIsMacro) {
                setNodeMacro(targetNode, true);
            }
            registerNodeOrigin(context_, targetNode, gct->at(0), "gir.link.oper");
            targetFuncType = targetOperator->funcType();
            // A result that the argument types fix (the shape of a tensor whose type carries
            // it) is a constant for the operators it is passed to, as a literal would be.
            if (const auto *folder =
                    camel::core::OperatorTable::instance().typeFolder(targetOperator->uri());
                folder != nullptr && withInputs.empty()) {
                Type *resultType = targetFuncType->exitType();
                std::optional<slot_t> value;
                try {
                    value = (*folder)(normInputTypes, normStatics, camel::core::mm::autoSpace());
                } catch (const std::exception &) {
                    value = std::nullopt;
                }
                if (value && resultType != nullptr) {
                    if (isGCTraced(resultType->code()) && *value != NullSlot) {
                        typeFoldedRoots_.emplace_back(
                            camel::core::mm::autoSpace(),
                            camel::core::rtdata::fromSlot<camel::core::rtdata::Object *>(*value),
                            resultType,
                            "gir.build.typefold");
                    }
                    typeFoldedValues_[targetNode] = *value;
                }
            }
        } else if (std::holds_alternative<graph_ptr_t>(drefTarget)) {
            callDecorated(std::get<graph_ptr_t>(drefTarget));
        } else {
            ASSERT(false, "Dref target must be graph set, operator group, or decorated graph.");
        }
    } else {
        Type *dataType = nodeTypeOf(targetNode);
        ASSERT(
            dataType->code() == TypeCode::Function,
            "Target node of LINK must be a function or operator node.");
        auto *funcType = tt::as_ptr<FunctionType>(dataType);
        if (!accepts(funcType, false) && !accepts(funcType, true)) {
            reportConversionGaps(
                {{std::string(), funcType}},
                withInputs,
                normInputs,
                withInputTypes,
                normInputTypes,
                gct);
            std::string argTypesStr = std::format(
                "<{}> ({})",
                strutil::join(withInputTypes, ", ", [](Type *t) { return t->toString(); }),
                strutil::join(normInputTypes, ", ", [](Type *t) { return t->toString(); }));
            diags_->of(SemanticDiag::ArgumentsMismatch)
                .atOrigin(gct->origin())
                .commit(funcType->toString(), argTypesStr);
            throw BuildAbortException();
        }
        node_handle_t invokeNode = currGraph_->addCallNode(funcType->exitType());
        linkNodes(LinkType::With, targetNode, invokeNode);
        std::vector<camel::source::origin_id_t> callInputs;
        if (auto *sourceContext = context_ ? context_->sourceContext().get() : nullptr) {
            auto calleeOrigin = sourceContext->resolveGirNodeOrigin(draftDebugKey(targetNode));
            if (calleeOrigin != camel::source::kInvalidOriginId) {
                callInputs.push_back(calleeOrigin);
            }
        }
        registerCallExpressionOrigin(
            context_,
            invokeNode,
            gct,
            "gir.link.call",
            {},
            std::move(callInputs));
        targetNode     = invokeNode;
        targetFuncType = funcType;
    }

    // A built-in operator reads the argument types its signature names (the arguments are not
    // converted on the way in). A struct value that enters a struct parameter is the one
    // conversion made here (design principles 5.2): an unnamed value is copied into the named
    // type, field by field and by name, so the operator reads the layout it was given.
    const auto enterStructParameters = [&](FunctionType *funcType) {
        const auto enter = [&](std::vector<Argument> &inputs, type_vec_t &types, bool with) {
            const size_t count = with ? funcType->withTypesCount() : funcType->normTypesCount();
            for (size_t i = 0; i < inputs.size() && i < count; ++i) {
                Type *param = with ? funcType->withTypeAt(i) : funcType->normTypeAt(i);
                if (param == nullptr || param->code() != TypeCode::Struct || types[i] == nullptr ||
                    types[i]->code() != TypeCode::Struct || param->equals(types[i])) {
                    continue;
                }
                if (node_handle_t entered = checkValue(inputs[i].node, param, inputs[i].expr)) {
                    inputs[i].node = entered;
                    types[i]       = param;
                }
            }
        };
        enter(withInputs, withInputTypes, true);
        enter(normInputs, normInputTypes, false);
    };

    ASSERT(targetFuncType != nullptr, "Target function type must be resolved.");
    if (targetOperator == nullptr) {
        enterParameters(targetFuncType);
    } else {
        enterStructParameters(targetFuncType);
    }
    if (recorder_) {
        const bool macroCallee =
            targetGraph != nullptr
                ? isMacroGraph(targetGraph)
                : (targetOperator != nullptr &&
                   camel::core::OperatorTable::instance().isMacro(targetOperator->uri()));
        recorder_->callResolved(
            gct->origin(),
            gct->at(0)->origin(),
            targetGraph ? targetGraph->graphId() : runtime::kInvalidGraphId,
            targetOperator ? targetOperator->uri() : std::string(),
            targetFuncType,
            macroCallee);
    }
    // A call is located at the call expression -- the callee and its argument list together --
    // not at the callee's definition (a FUNC node is made with its graph's origin) and not at
    // the argument list alone (the GCT call node's own span). A call through a function value
    // made earlier is a CALL node made by createFuncDataNode; either stands for this call.
    if (targetGraph && targetNode &&
        (nodeKindOf(targetNode) == camel::runtime::GCNodeKind::Func ||
         nodeKindOf(targetNode) == camel::runtime::GCNodeKind::Call)) {
        registerCallExpressionOrigin(context_, targetNode, gct, "gir.link.func");
    }

    std::string targetName = targetGraph
                                 ? targetGraph->name()
                                 : (targetOperator ? "<" + targetOperator->name() + ">" : "");
    // A function value has no name of its own; its type stands for it in diagnostics.
    const std::string calleeLabel = targetName.empty() ? targetFuncType->toString() : targetName;

    // How an argument is passed. A function (a graph or a function value) and assignment take
    // a `var` argument as the variable itself. Any other operator with a `var` parameter mutates
    // an object in place and takes the object; the call still counts as a write of the variable
    // the object was read from. Every other parameter takes the value.
    // A function, an assignment and a variable operator (`:var/take`, inPlaceRebindingOf) are
    // given the variable itself at a `var` position; any other operator its value.
    const bool takesVariables = targetOperator == nullptr ||
                                targetOperator->name().starts_with("__assn") ||
                                targetOperator->name().starts_with("__var_");
    // The node to link and, at a `var` position, the variable the callee writes (if any).
    const auto passArgument = [&](const Argument &arg, bool isVar) {
        if (!isVar) {
            return std::pair{valueOf(arg.node), node_handle_t{}};
        }
        requireMutableArgument(arg, calleeLabel);
        const bool named = isVariable(arg.node);
        if (takesVariables) {
            // `s.x = e`, `arr[0] = e`, `f() = e`: the target is a temporary, and the store
            // would be lost without a trace.
            if (!named && targetOperator != nullptr) {
                diags_->of(SemanticDiag::AssignmentTargetNotVariable)
                    .atOrigin(arg.expr->origin())
                    .commit();
                throw BuildAbortException();
            }
            // An expression that is not a name, passed to a function, is a fresh object
            // nobody else refers to.
            node_handle_t variable = named ? arg.node : newVariable(arg.node);
            return std::pair{variable, variable};
        }
        return std::pair{valueOf(arg.node), named ? arg.node : node_handle_t{}};
    };

    // All arguments are read before the call is recorded as a writer: a variable passed both
    // by value and to a `var` parameter (`f(c, c)`) is read before the call, not after it.
    struct Passed {
        node_handle_t node;
        node_handle_t written;
        LinkType link;
    };
    std::vector<Passed> passed;
    bool hasVarArgument = false;
    for (size_t i = 0; i < withInputs.size(); i++) {
        bool isVar = i < targetFuncType->withTypesCount() && targetFuncType->withIsVarAt(i);
        const auto [node, written] = passArgument(withInputs[i], isVar);
        passed.push_back({node, written, LinkType::With});
        hasVarArgument |= isVar;
    }
    for (size_t i = 0; i < normInputs.size(); i++) {
        bool isVar = i < targetFuncType->normTypesCount() && targetFuncType->normIsVarAt(i);
        const auto [node, written] = passArgument(normInputs[i], isVar);
        passed.push_back({node, written, LinkType::Norm});
        hasVarArgument |= isVar;
    }
    for (const Passed &arg : passed) {
        tryRemoveCtrlLink(arg.node, targetNode);
        linkNodes(arg.link, arg.node, targetNode);
        if (auto modifierNode = modifierOf(arg.node); modifierNode.has_value()) {
            if (sameGraph(*modifierNode, currGraph_) &&
                shouldAddSyncCtrlLink(*modifierNode, targetNode)) {
                linkNodes(LinkType::Ctrl, *modifierNode, targetNode);
            }
        }
    }
    // A function called directly brings its captured variables with it: the call uses each of
    // them, and writes those the function writes, exactly as if they were `var` arguments.
    std::vector<CapturedUse> captured;
    if (targetGraph) {
        captured = capturedUsesOf(targetGraph);
    }
    std::vector<node_handle_t> capturedWrites;
    for (const CapturedUse &use : captured) {
        orderAfterLastWriter(use.variable, targetNode);
        if (use.written) {
            capturedWrites.push_back(use.origin);
        }
    }
    if (!waited_ && !synced_) {
        const std::string subject =
            std::format("The call of '{}: {}'", targetName, targetFuncType->toString());
        if (hasVarArgument) {
            diags_->of(SemanticDiag::IgnoredSideEffect)
                .atOrigin(gct->origin())
                .commit(subject, "it takes a var argument");
        } else if (!capturedWrites.empty()) {
            unit_->pendingIgnoredWrites.push_back(
                {{targetGraph.get()}, capturedWrites, {}, gct->origin(), subject, "it writes"});
        }
    }
    for (const Passed &arg : passed) {
        if (arg.written != nullptr) {
            recordWrite(arg.written, targetNode);
        }
    }
    for (const CapturedUse &use : captured) {
        if (use.written) {
            recordWrite(use.variable, targetNode, targetNode, true);
        } else {
            pendingUsesOf(use.variable).push_back({targetNode, false});
        }
    }

    if (synced_) {
        if (lastSyncedNode_ != nullptr && shouldAddSyncCtrlLink(lastSyncedNode_, targetNode)) {
            linkNodes(LinkType::Ctrl, lastSyncedNode_, targetNode);
        }
        lastSyncedNode_ = targetNode;
        chainedNodes_.insert(targetNode);
    }
    if (targetOperator == nullptr && (!dimValues.empty() || !dimPacks.empty()) &&
        targetFuncType != nullptr && targetFuncType->hasExitType()) {
        // The return type may speak about the names the parameters bound (`: Tensor<[n, k]>`).
        // The function still has one graph: what varies with the call is only the static type,
        // and the value is passed through the checked conversion to it (the same check `as`
        // makes), so a body that breaks the relation is caught where the call runs.
        Type *namedExit = targetFuncType->exitType();
        std::vector<ExtentForm> forms;
        if (namedExit != nullptr && namedExit->extentForms(forms)) {
            bool hasNames = !namedExit->packName().empty();
            for (const ExtentForm &form : forms) {
                hasNames |= form.hasNames();
            }
            Type *actualExit = valueTypeOf(nodeTypeOf(targetNode));
            Type *refined    = hasNames
                                   ? const_cast<Type *>(
                                         namedExit->withExtentValues(dimValues, dimPacks))
                                   : nullptr;
            if (refined != nullptr && refined != namedExit && actualExit != nullptr &&
                !refined->equals(actualExit) &&
                refined->castSafetyFrom(actualExit) != CastSafety::Forbidden) {
                targetNode = castNodeTo(targetNode, refined, gct);
            }
        }
    }
    if (negateResult) {
        // `a != b` through an overload of `==`: `!(a == b)`, which needs a bool from it.
        if (Type *equal = valueTypeOf(nodeTypeOf(targetNode));
            equal == nullptr || equal->code() != TypeCode::Bool) {
            diags_->of(SemanticDiag::OperatorOverloadInvalid)
                .atOrigin(gct->origin())
                .commit(
                    "==",
                    std::format(
                        "'!=' is the negation of '==', and the overload of '==' for these "
                        "operands returns '{}', not 'bool'",
                        equal != nullptr ? equal->toString() : std::string("void")));
            throw BuildAbortException();
        }
        node_handle_t negation = applyTagOperator("__not__", bindTemporary(targetNode), gct);
        LEAVE("LINK");
        return negation;
    }
    LEAVE("LINK");
    return targetNode;
}

} // namespace camel::compile::gir
