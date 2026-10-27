// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
#include "camel/core/type/composite/stream.h"
#include "camel/execute/entry_args.h"
#include "camel/execute/fault_isolation.h"
#include "camel/runtime/control.h"
#include "camel/runtime/graph.h"
#include "camel/utils/log.h"
#include "camel/utils/scope.h"
#include "camel/utils/str.h"
#include "camel/utils/type.h"
#include "core/module/entity_internal.h"

#include "camel/core/operator_traits.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/composite/array.h"
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
    // (design principles 10.2): a call written here reaches them, and
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
            for (const generic_template_ptr_t &tmpl : genericTemplatesOfEntity(ent)) {
                if (std::ranges::find(set.templates, tmpl) == set.templates.end()) {
                    set.templates.push_back(tmpl);
                    set.moduleOf[tmpl.get()] = imported.mod->name();
                }
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

std::vector<std::pair<std::string, std::string>>
Builder::declaringModulesOf(const type_vec_t &withTypes, const type_vec_t &normTypes) {
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
                for (const generic_template_ptr_t &generic : genericTemplatesOfEntity(exported)) {
                    if (std::ranges::find(set.templates, generic) == set.templates.end()) {
                        set.templates.push_back(generic);
                        set.moduleOf[generic.get()] = moduleName;
                        added                       = true;
                    }
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

std::string
Builder::operatorSearchScopeText(const type_vec_t &withTypes, const type_vec_t &normTypes) {
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
    const generic_template_ptr_t &tmpl, const std::vector<Argument> &normInputs,
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
        set.ops->resolve(withTypes, normTypes, statics).has_value();
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
               entry.second->resolve(withTypes, normTypes).has_value();
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
        std::vector<generic_template_ptr_t> templates;
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
                    if (resolver->resolve(withTypes, normTypes).has_value()) {
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
        // there is nothing missing here.
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
    factory.setExitType(opType->exitType());
    // The function value promises what the operator's type promises (effects plan 2.3): a call
    // through this value takes its footprint from the type, not by walking the one-node body.
    factory.setResources(opType->resources());
    const auto nameAt = [&](size_t argIndex, const char *prefix, size_t i) {
        return argIndex < opType->argNamesCount() && !opType->argNameAt(argIndex).empty()
                   ? std::string(opType->argNameAt(argIndex))
                   : std::format("{}{}", prefix, i);
    };
    for (size_t i = 0; i < opType->withTypesCount(); ++i) {
        factory.addWithArg(nameAt(i, "__w", i), opType->withTypeAt(i), false, opType->withNeedsValueAt(i));
    }
    for (size_t i = 0; i < opType->normTypesCount(); ++i) {
        factory.addNormArg(
            nameAt(opType->withTypesCount() + i, "__n", i),
            opType->normTypeAt(i),
            false);
    }
    FunctionType *type = factory.build();

    compile_graph_ptr_t graph = enterScope(type);
    const bool macroOperator  = camel::core::OperatorTable::instance().isMacro(op->uri());
    if (macroOperator) {
        graph->draft().setFlags(
            static_cast<uint8_t>(graph->draft().flags() | camel::runtime::kGraphFlagMacro));
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
        registerBuiltBody(graph);
    } catch (...) {
        bodyStates_[graph.get()] = BodyState::Built;
        leaveScope();
        throw;
    }
    leaveScope();
    operatorValueGraphs_[key] = graph;
    return graph;
}

node_handle_t
Builder::attemptFunctionValue(const Argument &f, const GCT::node_ptr_t &origin) {
    // The argument is what the call would pass: a variable is read here, once, and the value
    // read is what the function captures.
    node_handle_t fValue = valueOf(f.node);
    Type *fType          = valueTypeOf(nodeTypeOf(fValue));
    ASSERT(
        fType != nullptr && fType->code() == TypeCode::Function,
        "The transform is given a function value.");
    FunctionType *type = camel::execute::attemptFunctionType(tt::as_ptr<FunctionType>(fType));
    ASSERT(type != nullptr, "The resolver accepted a function the transform cannot take.");

    // The captured function is bound to a name no program can write, in the scope the call
    // stands in, so that the new graph captures it the way any closure captures a name
    // (captureInto).
    const std::string fName = bindTemporary(fValue);

    compile_graph_ptr_t graph = enterScope(type);
    registerGraphOrigin(context_, graph, origin, "gir.attempt.graph");
    const bool outerSynced        = synced_;
    const node_handle_t outerLast = lastSyncedNode_;
    const auto restore            = [&] {
        synced_                  = outerSynced;
        lastSyncedNode_          = outerLast;
        bodyStates_[graph.get()] = BodyState::Built;
    };
    const auto make = [&](GCT::node_ptr_t node) {
        node->setOrigin(origin->origin());
        return node;
    };
    // The marked call stands where the program wrote the transform: a node of a call takes the
    // origin of the callee the program wrote (`gir.link.oper` reads gct->at(0)), so the call
    // inside the new graph is given the name's origin rather than the argument list's.
    const auto nameOrigin = [&] {
        return origin != nullptr && !origin->empty() ? origin->at(0)->origin() : origin->origin();
    }();
    try {
        bodyStates_[graph.get()] = BodyState::Open;
        // Its body is the one marked call `attempt's function(f, args...)`, built from
        // a synthesized GCT so that resolution, the argument checks and the origins are what
        // they are for any call.
        GCT::node_ptr_t callee =
            make(tree_->makeDRef(std::string(camel::execute::kFaultIsolationOperatorName)));
        callee->setOrigin(nameOrigin);
        GCT::node_ptr_t call = make(tree_->makeLink(1 + type->normTypesCount()));
        *call << callee << make(tree_->makeDRef(fName));
        size_t index = 0;
        for (draft_node_ref_t port : graph->draft().normPorts()) {
            const std::string name = "$attempt" + std::to_string(index++);
            nodeScope_->insert(name, graph->draft().ref(port));
            *call << make(tree_->makeDRef(name));
        }
        // The call is the value of the function, so it runs whenever the function is called.
        synced_             = true;
        lastSyncedNode_     = {};
        node_handle_t value = visitLinkNode(call);
        setGraphReturnFromResult(graph, nodeIdOf(value), nodeIdOf(value), nodeTypeOf(value));
        restore();
        registerBuiltBody(graph);
    } catch (...) {
        restore();
        leaveScope();
        throw;
    }
    leaveScope();
    return createFuncDataNode(graph, true, false);
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
                statics);
            if (!type || !(*type)->hasExitType() ||
                !(*type)->exitType()->equals(expected->exitType())) {
                continue;
            }
            // The rule is one of the call's overloads only where its own `when` says so
            // (design principles 4.4): a rule whose condition is false for these arguments
            // cannot be the one the value means, so it is not a second candidate and the
            // writing order of two declarations never decides (the same question
            // resolveGroupedCall answers for a call).
            if (ruleVerdictForCall(resolver, ops->name(), withTypes, normTypes) !=
                RuleVerdict::Applies) {
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

std::optional<oper_idx_ptr_t> Builder::resolveGroupedCall(
    const oper_group_ptr_t &ops, const type_vec_t &explicitTypes, Type *expectedResult,
    const type_vec_t &withTypes, const type_vec_t &normTypes, static_args_t normStatics,
    std::vector<std::pair<std::string, std::string>> &unmet, std::vector<std::string> *ambiguous,
    const std::vector<Argument> *withInputs) {
    std::optional<oper_idx_ptr_t> chosen;
    // The candidates that write `when` and whose condition holds for these arguments. One of them
    // is a call that has an answer; two or more are a call that has none, and the writing order
    // of the declarations is not what settles it (see the declaration of this function).
    std::vector<std::string> holding;
    for (const auto &[uri, resolver] : ops->resolvers()) {
        // A rule that is declared takes every count of `with` arguments from its required ones to
        // its full arity: the types of the defaults it registers stand for what the call left out.
        type_vec_t ruleWith = withTypes;
        if (FunctionType *declared = resolver->fixedSignature() != nullptr
                                         ? resolver->fixedSignature()
                                         : resolver->declaredSignature();
            declared != nullptr) {
            const auto completed = withTypesCompletedFor(uri, declared, withTypes, withInputs);
            if (!completed.has_value()) {
                continue;
            }
            ruleWith = *completed;
        }
        auto result = resolver->resolveForCall(
            explicitTypes, expectedResult, ruleWith, normTypes, normStatics);
        if (!result) {
            // The rule's parameters do not take these arguments: it was never a candidate for
            // this call, and its condition is not asked about a call it does not fit.
            continue;
        }
        // A body-less declaration is one of this call's overloads where its own `when` says so
        // (design principles 4.4): a rule whose condition is false for the arguments is not one,
        // so the order in which two declarations of the name are written cannot choose between
        // them. The condition is recorded, and the call reported as one whose conditions are
        // unmet, only when no rule at all takes it.
        std::string condition;
        const RuleVerdict verdict =
            ruleVerdictForCall(resolver, ops->name(), ruleWith, normTypes, &condition);
        if (verdict == RuleVerdict::ConditionUnmet) {
            unmet.emplace_back(resolver->signature(), std::move(condition));
            continue;
        }
        if (verdict == RuleVerdict::NotThisRule) {
            continue;
        }
        if (!chosen) {
            chosen = std::make_shared<OperatorIndex>(ops->name(), *result, uri);
        }
        if (ambiguous != nullptr && nativeRuleConditions_.contains(resolver.get())) {
            // Named by its signature and its condition together: the two arms of one name share
            // the signature and are told apart by the condition alone, the way
            // WhenDeclarationOverlap and WhenConditionUnmet name them.
            holding.push_back(resolver->signature() + " — when " + condition);
        }
    }
    if (ambiguous != nullptr && holding.size() > 1) {
        // Sorted, so that the report does not carry the order the declarations were written in:
        // that order is exactly what must not decide, and a report that listed the candidates in
        // it would differ between two files holding one program (design principles 4.4).
        std::ranges::sort(holding);
        holding.erase(std::ranges::unique(holding).begin(), holding.end());
        *ambiguous = std::move(holding);
    }
    return chosen;
}

std::optional<type_vec_t> Builder::withTypesCompletedFor(
    const std::string &uri, FunctionType *signature, const type_vec_t &withTypes,
    const std::vector<Argument> *withInputs) const {
    const size_t arity = signature->withTypesCount();
    if (withTypes.size() > arity) {
        return withTypes; // too many: the rule says so itself
    }
    const auto native = nativeDefaults_.find(uri);
    type_vec_t completed = withTypes;
    completed.resize(arity, nullptr);
    for (size_t i = 0; i < arity; ++i) {
        const bool written = i < withTypes.size() &&
                             !(withInputs != nullptr && i < withInputs->size() &&
                               (*withInputs)[i].left);
        if (written) {
            continue;
        }
        if (native == nativeDefaults_.end() || i >= native->second.with.size() ||
            !native->second.with[i].has_value()) {
            return std::nullopt;
        }
        completed[i] = native->second.with[i]->data->type();
    }
    return completed;
}

std::string Builder::unstatedBindingForCall(
    const oper_group_ptr_t &ops, const type_vec_t &explicitTypes, const type_vec_t &normTypes) {
    // A call that writes its type arguments has already fixed the variables they name, so an
    // empty literal among its arguments is an argument the call answered for itself: what it
    // said is what the argument takes, and the type it takes is not one the call infers.
    if (std::ranges::any_of(explicitTypes, [](Type *type) {
            return type != nullptr && type->code() != TypeCode::Void;
        })) {
        return {};
    }
    for (const auto &[uri, resolver] : ops->resolvers()) {
        (void)uri;
        // The signature the rule declares, with its type variables still in it: a rule that
        // computes its signature from the arguments writes no variable a call could leave open.
        FunctionType *declared = resolver->declaredSignature();
        if (declared == nullptr) {
            continue;
        }
        // The arguments are matched to the parameters the way the call wrote them: a `constas`
        // parameter is written at its own position and is not a runtime argument.
        size_t written = 0;
        for (size_t i = 0; i < declared->normTypesCount() && written < normTypes.size(); ++i) {
            if (declared->normIsConstasAt(i)) {
                continue;
            }
            const std::string name =
                unstatedBindingOf(declared->normTypeAt(i), valueTypeOf(normTypes[written]));
            ++written;
            if (!name.empty()) {
                return name;
            }
        }
    }
    return {};
}

bool Builder::plainOperatorAvailableFor(
    const std::string &group, const std::vector<Argument> &operands, const type_vec_t &normTypes) {
    if (normTypes.size() != operands.size()) {
        return false;
    }
    std::vector<StaticArg> statics;
    statics.reserve(operands.size());
    for (const Argument &operand : operands) {
        statics.push_back(staticArgOf(operand.node));
    }
    // A built-in operator, or one a module registers for the types it declares (the `+` the
    // tensor module declares for a numeric array): the group answers for the operand types
    // alone (design principles 4.3).
    if (operatorOverloadsOf(group)
            .ops->resolve({}, normTypes, statics)
            .has_value()) {
        return true;
    }
    // An `operator` the program writes, or one an import brings: its parameters are matched
    // where the ordinary call of the plain operator is built, and no type's origin enters it.
    if (hasProgramOperatorOverloads(group)) {
        return true;
    }
    // The module that declares the operand's type offers the operator with the type
    // (design principles 5.7): asked of the types, as the operator itself is.
    return hasDeclaringModuleOperator(group, {}, normTypes);
}

node_handle_t Builder::compoundAssignmentByRebinding(
    const std::string &group, const GCT::node_ptr_t &gct, const std::vector<Argument> &operands,
    const type_vec_t &normTypes) {
    // `a op= b` is `a = a op b` (design principles 4.2): one rule for every type, whoever
    // declared it — a number's built-in `+`, a `+` the tensor module declares for a numeric
    // array, an `operator +` of the program. What the compound assignment follows is that
    // operator, so what is asked of it is what design principles 4.2 says: that `a op b` be
    // defined for these operands, and that `a = a op b` be a legal assignment — the value the
    // operator gives fits `a`'s type where the assignment would put it (an addition broadcasts
    // `f32[]` and `f32[2]` to `f32[2]`, which a `var` holding `f32[]` takes). `int[]` has no
    // `+`, and a `+` whose value no assignment to `a` would accept is not a `+=` either; both
    // are refused here, and the call reports the compound assignment the program wrote.
    const auto plainGroup = camel::core::plainGroupOfCompoundAssignment(group);
    if (!plainGroup || operands.size() != 2 || normTypes.size() != operands.size()) {
        return nullptr;
    }
    const std::string plainName(*plainGroup);
    if (!plainOperatorAvailableFor(plainName, operands, normTypes)) {
        return nullptr;
    }
    // `a`'s type as the operator of `a op b` sees it: the two are compared where the operator
    // is resolved, so the comparison asks about one and the same reading of the type.
    Type *target = valueTypeOf(normTypes[0]);
    if (target == nullptr) {
        return nullptr;
    }
    const auto make = [&](GCT::node_ptr_t node) {
        node->setOrigin(gct->origin());
        return node;
    };
    // The value keeps the compound operator's own origin: what a fault of the operator is
    // reported at is the `%=` as written (a `%=` by zero is a DivisionByZero at the `%=`,
    // test/plans/feat/linear/operators.plan.toml). The write keeps the statement's, so the
    // statement is still the whole assignment where something asks for it (the LSP's
    // navigation, test/plans/feat/tools/lsp.plan.toml).
    const auto makeOperator = [&](GCT::node_ptr_t node) {
        node->setOrigin(gct->size() > 0 && gct->at(0) != nullptr ? gct->at(0)->origin()
                                                                : gct->origin());
        return node;
    };
    // The right operand is evaluated once: it was built already, and is named here.
    const std::string right = bindTemporary(valueOf(operands[1].node));
    // The place is read through `:var/peek`: what the plain operator reads is the value this same
    // assignment writes back, so the read must not take the place's array with it, and whether the
    // store updates in place is the memory planner's question (design principles 6.11). The read
    // is otherwise the read the place would get: it is the same `:var/peek` a place of an array is
    // read with when the assignment is in place, and the same one a read inside a sync block is
    // settled to when nothing after it needs the array. A place that is not a variable of the
    // program is left to be built as written, so its own report is the one given.
    node_handle_t placeCell = nullptr;
    if (operands[0].expr != nullptr && operands[0].expr->type() == GCT::NodeKind::DREF) {
        const std::string name = moduleLevelName(operands[0].expr->name());
        if (nodeAt(name).has_value()) {
            if (node_handle_t node = resolveNodeByRef(name); isVariable(node)) {
                placeCell = node;
            }
        }
    }
    const std::string place =
        placeCell != nullptr ? bindTemporary(peekVariable(placeCell)) : std::string();
    GCT::node_ptr_t value = makeOperator(tree_->makeLink(2));
    *value << makeOperator(tree_->makeDRef(plainName));
    if (place.empty()) {
        *value << tree_->clone(operands[0].expr);
    } else {
        *value << make(tree_->makeDRef(place));
    }
    *value << make(tree_->makeDRef(right));
    // The plain operator is resolved as one written by itself would be: this is where its
    // availability is settled for the arguments, and where its result type is known. A result
    // the assignment to `a` would not take is not this assignment's meaning, so the rebinding
    // does not apply and the compound assignment is reported as undefined, exactly as for a
    // type that has no plain operator at all.
    const node_handle_t plain = visitLinkNode(value);
    Type *result             = valueTypeOf(nodeTypeOf(plain));
    if (result == nullptr || !target->assignableFrom(result)) {
        return nullptr;
    }
    // The value the plain operator gave is named for the assignment, so that it is computed
    // once: the assignment stores what the operator answered.
    const std::string held = bindTemporary(valueOf(plain));
    GCT::node_ptr_t assign = make(tree_->makeLink(2));
    *assign << make(tree_->makeDRef("__assn__")) << tree_->clone(operands[0].expr)
            << make(tree_->makeDRef(held));
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
    oper_group_ptr_t group = importedOperatorGroupNamed(opName->name());
    if (group == nullptr) {
        // A generic function whose `#impl` kernel has an in-place form (`sort`): the rule of the
        // kernel that is the executor for this array is the operator the rewrite is about
        // (adoptImplKernel asks the same question of an instance).
        std::vector<generic_template_ptr_t> templates;
        if (const auto scoped = templateScope_->get(opName->name()); scoped.has_value()) {
            if (GenericGroup *templated = genericGroupOf(scoped.value()); templated != nullptr) {
                templates.assign(templated->begin(), templated->end());
            }
        } else if (const auto imported = importedEntity(opName->name()); imported.has_value()) {
            templates = genericTemplatesOfEntity(imported.value());
        }
        for (const generic_template_ptr_t &tmpl : templates) {
            const auto declared = implTemplateKernels_.find(tmpl.get());
            if (declared == implTemplateKernels_.end()) {
                continue;
            }
            for (const auto &[ruleUri, resolver] : declared->second.group->resolvers()) {
                if (resolver->resolve({}, {arrayType}).has_value() &&
                    declaredRuleApplies(resolver, declared->second.name, {}, {arrayType})) {
                    group = OperatorGroup::create(
                        declared->second.group->name(),
                        std::vector<std::pair<std::string, camel::core::type::resolver_ptr_t>>{
                            {ruleUri, resolver}});
                    break;
                }
            }
            if (group != nullptr) {
                break;
            }
        }
    }
    auto &table = camel::core::OperatorTable::instance();
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
    const auto resolved       = group->resolve(withTypes, {arrayType}, statics);
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

/// The reduced-precision element type (bf16, f16) an argument carries — as its own type, or as
/// the element type of an array — or nullopt when none does. A tensor argument is not one: its
/// type is not an `ArrayType`, and a bf16 or f16 tensor is computed by the tensor module, which
/// widens every element to f32 and rounds once when it stores the result (modules/tensor/dtype.h,
/// design principles 7.3). A bf16 or f16 scalar or array has a literal, the written
/// conversions and the scalar arithmetic, comparison and reduction of the language, but not every
/// operation: `%` is an integer operation, and a function that takes a fixed set of types has no
/// overload for the element type. Such a call is reported as asking for an operation the type does
/// not have, rather than as "no matching function" over overloads it could never match.
std::optional<TypeCode>
reducedPrecisionArgumentOf(const type_vec_t &withTypes, const type_vec_t &normTypes) {
    const auto reducedCode = [](const Type *type) -> std::optional<TypeCode> {
        if (type == nullptr) {
            return std::nullopt;
        }
        TypeCode code = type->code();
        if (code == TypeCode::Array) {
            const auto *arrayType = static_cast<const ArrayType *>(type);
            code                  = arrayType->elemTypeCode();
        }
        if (code == TypeCode::BFloat16 || code == TypeCode::Float16) {
            return code;
        }
        return std::nullopt;
    };
    for (const type_vec_t *types : {&withTypes, &normTypes}) {
        for (const Type *type : *types) {
            if (const auto code = reducedCode(type)) {
                return code;
            }
        }
    }
    return std::nullopt;
}

/// One place a path assignment writes: a field (`v.f`, `v.0`) or an element (`v[i]`).
struct PathStep {
    GCT::node_ptr_t node; // the ACCS or __idx__ node, as written
    bool isIndex = false; // a field of a struct or tuple, or an element of an array
    size_t field = 0;     // the position of the field
    std::string selector; // the name of the index value of an element
    std::string base;     // the name of the value the step is applied to
    std::string value;    // the name of the value the step reads
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
    // copy; see for the measurement that shows both.
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
        // The value enters the place's type as it enters a declaration's and a parameter's
        // (`acc.best = hi` with `best: float?`): one relation, entersImplicitly.
        Type *valueType = valueTypeOf(nodeTypeOf(right));
        if (type != nullptr && valueType != nullptr && !type->equals(valueType) &&
            entersImplicitly(type, valueType)) {
            if (node_handle_t entered = checkValue(right, type, gct->at(2))) {
                right = entered;
            }
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
                *applied << make(internalOperatorDref(camel::core::oper::VarTake), step.node->at(1))
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

namespace {
// The names the graph of a composition gives to its parameter and to the two functions it
// captures: no program writes them, and they are inserted in that graph's own scope.
constexpr std::string_view kCompParameter = "$comp_arg";
constexpr std::string_view kCompLhs       = "$comp_f";
constexpr std::string_view kCompRhs       = "$comp_g";
// The built-in `range(start, end)`, which is what the interval reading of `..` is: referred to
// by the name the compiler writes for a built-in operator (design principles 4.2).
constexpr std::string_view kRangeCall = "$builtin::range";
} // namespace

node_handle_t Builder::buildComposition(const GCT::node_ptr_t &gct, Type *expected) {
    // The composition is the lambda `(x) => g(f(x))` and nothing else (design principles 4.2), so
    // it is typed exactly as that lambda is typed: the function type the position expects gives
    // the composed function its input - the parameter of `f` - and its output - the result of
    // `g` - and it instantiates the operands where they stand, as the lambda's two calls are
    // instantiated (design principles 5.9, 5.5).
    FunctionType *expectedFunc = nullptr;
    if (expected != nullptr && expected->code() == TypeCode::Function) {
        auto *candidate = tt::as_ptr<FunctionType>(expected);
        // A composed function takes the one argument `f` takes. A position that expects some
        // other signature - two parameters, or a `with` parameter this value cannot carry - says
        // nothing about the operands, and the composition is left to the two of them.
        if (candidate->withTypesCount() == 0 && candidate->normTypesCount() == 1) {
            expectedFunc = candidate;
        }
    }
    // What `f` is expected to be: the one parameter the composed function takes. Its result is
    // deliberately not taken from the expectation: what `f` returns is what `g` takes, and only
    // `f` says what that is (the lambda reads it off the call `f(x)`, not off the declared type).
    FunctionType *lhsExpected = nullptr;
    if (expectedFunc != nullptr) {
        FunctionTypeFactory factory;
        factory.addNormArg(
            std::string(kCompParameter), expectedFunc->normTypeAt(0), expectedFunc->normIsVarAt(0));
        lhsExpected = factory.build();
    }
    // The two operands are values, and a function written by name is its value here as it is in
    // any argument position. The name of a generic function is instantiated by the type its
    // position gives it, exactly as an argument is (`checkValue`): with no such type the name
    // still stands for no instance, and the refusal below says so by name.
    const auto operandValue = [&](const GCT::node_ptr_t &expr, Type *wanted) {
        ExpectedValueScope scope(expectedValueType_, wanted);
        node_handle_t node = valueNodeOf(visit(expr));
        if (wanted != nullptr && genericDrefs_.contains(node)) {
            node = genericValueFor(node, wanted, expr);
        }
        return argumentNodeOf(node, expr);
    };
    node_handle_t lhs = operandValue(gct->at(1), lhsExpected);
    // What `g` is expected to be: it takes what `f` returns - known now - and gives what the
    // whole composition gives, the result the position expects.
    FunctionType *rhsExpected = nullptr;
    if (expectedFunc != nullptr) {
        Type *lhsBuiltType = nodeTypeOf(lhs);
        if (lhsBuiltType != nullptr && lhsBuiltType->code() == TypeCode::Function) {
            auto *lhsFunc = tt::as_ptr<FunctionType>(lhsBuiltType);
            if (lhsFunc->withTypesCount() == 0 && lhsFunc->normTypesCount() == 1 &&
                lhsFunc->hasExitType()) {
                FunctionTypeFactory factory;
                factory.addNormArg(std::string(kCompParameter), lhsFunc->exitType(), false);
                if (expectedFunc->hasExitType()) {
                    factory.setExitType(expectedFunc->exitType());
                }
                rhsExpected = factory.build();
            }
        }
    }
    node_handle_t rhs = operandValue(gct->at(2), rhsExpected);
    return buildCompositionWith(gct, lhs, rhs);
}

bool Builder::isIntervalExpr(const GCT::node_ptr_t &expr) {
    return expr != nullptr && expr->size() == 3 && expr->at(0)->type() == GCT::NodeKind::DREF &&
           expr->at(0)->name() == camel::core::kCompositionCall;
}

bool Builder::intervalBounds(node_handle_t from, node_handle_t to) const {
    // An interval is two integers and nothing else (design principles 4.2): `int` is the
    // language's integer type, as it is in `intervalValue`.
    Type *fromType = nodeTypeOf(from);
    Type *toType   = nodeTypeOf(to);
    return fromType != nullptr && toType != nullptr && fromType->code() == TypeCode::Int64 &&
           toType->code() == TypeCode::Int64;
}

node_handle_t
Builder::buildCompositionWith(const GCT::node_ptr_t &gct, node_handle_t lhs, node_handle_t rhs) {    Type *lhsType     = nodeTypeOf(lhs);
    Type *rhsType = nodeTypeOf(rhs);
    const bool bothFunctions = lhsType != nullptr && rhsType != nullptr &&
                               lhsType->code() == TypeCode::Function &&
                               rhsType->code() == TypeCode::Function;
    if (bothFunctions) {
        auto *lhsFunc = static_cast<FunctionType *>(lhsType);
        auto *rhsFunc = static_cast<FunctionType *>(rhsType);
        // `f` is unary: the composed function takes the one argument `f` takes (design
        // principles 4.2).
        if (lhsFunc->normTypesCount() != 1 || lhsFunc->withTypesCount() != 0 ||
            !lhsFunc->hasExitType()) {
            diags_->of(SemanticDiag::CompOperandNotUnary)
                .atOrigin(gct->origin())
                .commit(lhsFunc->toString(), std::to_string(lhsFunc->normTypesCount()));
            throw BuildAbortException();
        }
        return composeFunctionValue(lhs, rhs, lhsFunc, rhsFunc, gct);
    }
    // Two integers are the interval `[a, b)` (design principles 4.2). `int` is the language's
    // integer type (`Type::Int()` is `Int64`), and the interval is the array of them.
    if (lhsType != nullptr && rhsType != nullptr && lhsType->code() == TypeCode::Int64 &&
        rhsType->code() == TypeCode::Int64) {
        return intervalValue(lhs, rhs, gct);
    }
    // A name of a generic function that neither the position nor the other operand settles: the
    // composition is refused by that name, together with the two ways to settle it (
    // section generic-operands-of-composition). This is not CompOperandMismatch: the operand is
    // not a value of a type the composition cannot take, it is a name with no instance yet.
    for (const bool isLeft : {true, false}) {
        const node_handle_t operandNode = isLeft ? lhs : rhs;
        const auto found                = genericDrefs_.find(operandNode);
        if (found == genericDrefs_.end()) {
            continue;
        }
        const GCT::node_ptr_t &expr        = gct->at(isLeft ? 1 : 2);
        const generic_template_ptr_t &tmpl = found->second.one();
        std::string written                = sourceTextOf(expr);
        if (written.empty() && tmpl != nullptr) {
            written = tmpl->name;
        }
        diags_->of(SemanticDiag::CompOperandGenericWithoutType)
            .atOrigin(gct->origin())
            .commit(isLeft ? "left" : "right", written);
        throw BuildAbortException();
    }
    // The operand's type as the message names it. A value whose type is not settled is not a
    // type the program wrote, and the message says that rather than print `void` as one.
    const auto operandText = [](Type *type) -> std::string {
        if (type == nullptr) {
            return "nothing";
        }
        if (type->code() != TypeCode::Void) {
            return type->toString();
        }
        return "a value with no type yet";
    };
    diags_->of(SemanticDiag::CompOperandMismatch)
        .atOrigin(gct->origin())
        .commit(operandText(lhsType), operandText(rhsType));
    throw BuildAbortException();
}

node_handle_t Builder::composeFunctionValue(
    node_handle_t lhs, node_handle_t rhs, FunctionType *lhsType, FunctionType *rhsType,
    const GCT::node_ptr_t &gct) {
    // The composition is the lambda `(x) => g(f(x))` and nothing else (design principles 4.2):
    // it has the parameter
    // of `f`, the result of `g`, and the two functions as its captures. Its type, its generic
    // instantiation and its overloads are the ones that lambda has, because it is built as one.
    FunctionTypeFactory factory;
    factory.addNormArg(
        std::string(kCompParameter), lhsType->normTypeAt(0), lhsType->normIsVarAt(0));
    factory.setExitType(rhsType->exitType());
    FunctionType *type        = factory.build();
    compile_graph_ptr_t graph = enterScope(type);
    const bool outerSynced        = synced_;
    const node_handle_t outerLast = lastSyncedNode_;
    const auto restore            = [&] {
        synced_                  = outerSynced;
        lastSyncedNode_          = outerLast;
        bodyStates_[graph.get()] = BodyState::Built;
    };
    try {
        bodyStates_[graph.get()] = BodyState::Open;
        const auto make = [&](GCT::node_ptr_t node) {
            node->setOrigin(gct->origin());
            return node;
        };
        const auto nameNode = [&](std::string_view name) {
            return make(tree_->makeDRef(std::string(name)));
        };
        // The parameter's port, and the two functions: a name of an enclosing graph used in a
        // nested one is what a closure captures, and insertNode makes exactly that of them.
        size_t portCount = 0;
        for (draft_node_ref_t port : graph->draft().normPorts()) {
            insertNode(std::string(kCompParameter), graph->draft().ref(port));
            ++portCount;
        }
        ASSERT(portCount == 1, "A composition has exactly one parameter.");
        insertNode(std::string(kCompLhs), lhs);
        insertNode(std::string(kCompRhs), rhs);
        GCT::node_ptr_t inner = make(tree_->makeLink(1));
        *inner << nameNode(kCompLhs) << nameNode(kCompParameter);
        GCT::node_ptr_t outer = make(tree_->makeLink(1));
        *outer << nameNode(kCompRhs) << inner;
        // The call is the value of the function, so it runs whenever the function is called.
        synced_         = true;
        lastSyncedNode_ = {};
        node_handle_t value = visitLinkNode(outer);
        setGraphReturnFromResult(graph, nodeIdOf(value), nodeIdOf(value), nodeTypeOf(value));
        restore();
        registerBuiltBody(graph);
    } catch (...) {
        restore();
        leaveScope();
        throw;
    }
    leaveScope();
    return createFuncDataNode(graph, true, false);
}

node_handle_t Builder::intervalValue(
    node_handle_t lhs, node_handle_t rhs, const GCT::node_ptr_t &gct) {
    // `a..b` of two integers is the array of them from `a` to `b - 1`, left closed and right
    // open, as the slice `t[a:b]` is (design principles 4.2). It is the step-1 case of `range`,
    // written out as that call with the step, `range(a, b, 1)`: the group has one rule and it
    // takes three arguments, so the two-argument spelling is an ordinary no-match while the
    // interval keeps its own lowering -- the operation is retired nowhere.
    const auto make = [&](GCT::node_ptr_t node) {
        node->setOrigin(gct->origin());
        return node;
    };
    const auto nameNode = [&](const std::string &name) { return make(tree_->makeDRef(name)); };
    const std::string lhsName = bindTemporary(lhs);
    const std::string rhsName = bindTemporary(rhs);
    GCT::node_ptr_t call      = make(tree_->makeLink(3));
    *call << make(tree_->makeDRef(std::string(kRangeCall))) << nameNode(lhsName)
          << nameNode(rhsName)
          << make(tree_->makeData(
                 tt::as_shared<Data>(std::make_shared<PrimaryData<int64_t>>(int64_t{1}))));
    node_handle_t value = visitLinkNode(call);
    nodeScope_->erase(lhsName, false);
    nodeScope_->erase(rhsName, false);
    return value;
}

node_handle_t Builder::visitPipeLink(
    const GCT::node_ptr_t &call, const GCT::node_ptr_t &replacement) {
    const auto unused = unusedStatement_;
    PipeState detect;
    detect.call        = call;
    detect.replacement = replacement;
    detect.previous    = pipe_;
    pipe_              = &detect;
    try {
        node_handle_t built = visitLinkNode(call);
        pipe_               = detect.previous;
        return built;
    } catch (const PipeTagged &) {
        pipe_ = detect.previous;
    } catch (...) {
        pipe_ = detect.previous;
        throw;
    }
    // The receiver is a `T?` or a `T!`. Does an overload take it as it is?
    node_handle_t receiver = detect.receiver;
    bool direct            = true;
    {
        PipeState scratch;
        scratch.call     = call;
        scratch.receiver = receiver;
        scratch.mode     = PipeMode::Scratch;
        scratch.previous = pipe_;
        unusedStatement_                 = nullptr;
        runtime::GraphDraft &draft       = currGraph_->draft();
        const draft_node_ref_t firstNode = static_cast<draft_node_ref_t>(draft.nodeSlotCount());
        // Nothing the dropped arm built stays in the graph: a node of it that had an effect would
        // be reported as one that never runs, and one that did would run.
        const auto dropScratch = [&] {
            for (draft_node_ref_t id = static_cast<draft_node_ref_t>(draft.nodeSlotCount());
                 id > firstNode;) {
                --id;
                if (draft.alive(id)) {
                    draft.eraseNode(id);
                }
            }
        };
        try {
            pipe_ = &scratch;
            buildArm(call, [&] { return visitLinkNode(call); });
        } catch (const PipeNoMatch &) {
            direct = false;
        } catch (...) {
            pipe_ = scratch.previous;
            throw;
        }
        pipe_ = scratch.previous;
        dropScratch();
    }
    unusedStatement_ = unused;
    if (!direct) {
        return peelPipeReceiver(call, receiver);
    }
    PipeState real;
    real.call     = call;
    real.receiver = receiver;
    real.mode     = PipeMode::Direct;
    real.previous = pipe_;
    pipe_         = &real;
    try {
        node_handle_t built = visitLinkNode(call);
        pipe_               = real.previous;
        return built;
    } catch (...) {
        pipe_ = real.previous;
        throw;
    }
}

void Builder::yieldToPipe(const GCT::node_ptr_t &call) {
    if (pipe_ != nullptr && pipe_->call == call && pipe_->mode == PipeMode::Scratch) {
        throw PipeNoMatch();
    }
}

node_handle_t Builder::visitLinkNode(const GCT::node_ptr_t &gct) {
    if (GCT::Tree::of(gct)->isPipe(gct) && (pipe_ == nullptr || pipe_->call != gct)) {
        return visitPipeLink(gct, nullptr);
    }
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
    // `a..b` (gct.comp): one construct of two readings, told apart by the types of its two
    // operands, and they are known only here (design principles 4.2). The type this position
    // expects is what a generic operand is instantiated from (design principles 5.5).
    if (gct->size() == 3 && gct->at(0)->type() == GCT::NodeKind::DREF &&
        gct->at(0)->name() == camel::core::kCompositionCall) {
        node_handle_t composed = buildComposition(gct, expectedResult);
        LEAVE("LINK");
        return composed;
    }
    // An expression of literals only is computed now, exactly (design principles 5.1):
    // `1 + 2`, `2 ^ 10`, `0.1 + 0.2 == 0.3`. Its value takes the type this position expects,
    // and the default type of its kind when nothing does.
    if (node_handle_t folded = foldLiteralCall(gct, expectedResult)) {
        LEAVE("LINK");
        return folded;
    }
    genericCalleeAllowed_          = gct->at(0)->type() == GCT::NodeKind::DREF ||
                                     gct->at(0)->type() == GCT::NodeKind::WITH ||
                                     gct->at(0)->type() == GCT::NodeKind::TAPP;
    const bool outerWithArgsOfCall = withArgsOfCall_;
    withArgsOfCall_                = gct->at(0)->type() == GCT::NodeKind::WITH;
    // The callee of a call is a name: a rule's binding function called here is called, not made
    // the `Rule` its name gives (visitDRefNode).
    const bool outerRuleBindingCallee = std::exchange(ruleBindingCallee_, true);
    any targetNodeRes                 = visit(gct->at(0));
    ruleBindingCallee_                = outerRuleBindingCallee;
    withArgsOfCall_                = outerWithArgsOfCall;
    genericCalleeAllowed_          = false;
    // The callee may be a lambda written in place (`((x: int): int => x + 1)(3)`,
    // `v -> (x: int): int => x * 2`): visiting it gives its graph, and what is called is the
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

    FunctionType *argumentSignature = signatureForArgumentContext(
        targetNode, gct->at(0)->type() == GCT::NodeKind::WITH ? gct->at(0)->size() - 1 : 0);
    // The type of the position the call stands in, aligned with the callee's result type, solves
    // the type variables that result mentions (design principles 5.1, 5.5; the rule of
    // 2026-10-23 on `expected-type-through-literals-and-calls`). Every variable the result
    // mentions — not only one that appears in the result alone — is solved here, and a parameter
    // that mentions one of them is a position with an expected type like any other: the
    // arguments are built with the substituted parameter, so `keep([1.0, 2.0], 2)` in an `f32[]`
    // position builds the array as an `f32[]` and a literal takes the type of that position
    // while it is made, as it does in a declaration, a return or a field. The variables the
    // arguments themselves determine are solved after they are built, as they were; a parameter
    // this leaves open gives its argument no expected type.
    //
    // A `void` position is not a requirement but the absence of one: a value written where nothing
    // is wanted is discarded (`let x = <call>` binds the value to a name and checks it against
    // nothing), so it solves no variable — `sum([1, 2, 3])` there is a `sum` of an `int[]`, not of
    // a `void[]`. A result-only variable solved from a discarded position would otherwise take
    // `void` as its type.
    type_bindings_t positionBindings;
    if (argumentSignature != nullptr && expectedResult != nullptr &&
        expectedResult->code() != TypeCode::Void && argumentSignature->hasExitType() &&
        containsTypeVar(argumentSignature)) {
        unifyTypeVars(argumentSignature->exitType(), expectedResult, positionBindings);
    }
    const auto parameterInPosition = [&](Type *parameter) -> Type * {
        if (parameter == nullptr || positionBindings.empty()) {
            return parameter;
        }
        Type *solved = substituteTypeVars(parameter, positionBindings);
        return containsTypeVar(solved) ? nullptr : solved;
    };
    // `v = e`: the value takes the type of the place it is written to, as inPlaceRebindingOf
    // already does for the forms it rewrites. The assignment's own signature follows the
    // place's type, so the call gives the value no expected type of its own; here only a
    // function type is handed on, for a lambda's unwritten parameter types (design principles
    // 5.9). Any other type leaves the value as it was.
    const bool isAssignment = gct->size() == 3 && gct->at(0)->type() == GCT::NodeKind::DREF &&
                              gct->at(0)->name() == "__assn__";
    Type *assignedType      = nullptr;
    // The declaration of a rest function, when this call names one: its arguments are expanded
    // into a body in place of the call, and no instance is made (inlineRestCall).
    generic_template_ptr_t restTemplate;
    if (auto generic = genericDrefs_.find(targetNode);
        generic != genericDrefs_.end() && generic->second.one() != nullptr &&
        generic->second.one()->funcType->hasRest()) {
        restTemplate = generic->second.one();
    }
    const size_t restFixedCount =
        restTemplate != nullptr
            ? restTemplate->funcType->normTypesCount() - 1 - restTemplate->funcType->constasCount()
            : 0;
    const size_t restDeclaredCount =
        restTemplate != nullptr ? restTemplate->funcType->normTypesCount() : 0;
    // An ordinary argument may be written by name (`f(1, by: 2)`), and an ordinary parameter
    // may have a default (design principles 4.4). The name selects the parameter the value is
    // given to, so the expected type is that parameter's, not the position's: the written
    // order is only the order the call was written in.
    const auto *argumentNames = GCT::Tree::of(gct)->argumentNamesOf(gct);
    const auto normSlotOf     = [&](size_t written) -> size_t {
        if (argumentNames == nullptr || written >= argumentNames->size() ||
            !(*argumentNames)[written]) {
            return written;
        }
        if (argumentSignature != nullptr && argumentSignature->hasArgNames()) {
            const std::string &name = *(*argumentNames)[written];
            for (size_t i = 0; i < argumentSignature->normTypesCount(); ++i) {
                if (name == argumentSignature->argNameAt(argumentSignature->withTypesCount() + i)) {
                    return i;
                }
            }
        }
        return written;
    };
    for (size_t i = 1; i < gct->size(); i++) {
        const size_t slot = normSlotOf(i - 1);
        Type *wanted      = argumentSignature && slot < argumentSignature->normTypesCount()
                                ? argumentSignature->normTypeAt(slot)
                                : nullptr;
        if (restTemplate != nullptr) {
            // The declaration's own parameter, `constas` values included: a `constas` value is
            // written at its parameter's position like any other argument. The rest parameter
            // takes every argument after the fixed ones and gives them no expected type.
            const size_t at = i - 1;
            wanted = at < restDeclaredCount &&
                             (restTemplate->funcType->normIsConstasAt(at) || at < restFixedCount)
                         ? restTemplate->funcType->normTypeAt(at)
                         : nullptr;
        }
        if (isAssignment && i == 2 && assignedType != nullptr) {
            wanted = assignedType;
        }
        // The type variables the position solved are in the parameter now (design principles
        // 5.1, 5.5): a parameter that still mentions one gives its argument no expected type,
        // as it did.
        wanted = parameterInPosition(wanted);
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
            const GCT::node_ptr_t operand       = gct->at(i)->at(0);
            std::vector<node_handle_t> elements = expandRestArgument(
                operand,
                operand->type() == GCT::NodeKind::DREF ? operand->name()
                                                       : std::string("expression"),
                gct);
            for (const node_handle_t &element : elements) {
                normInputs.push_back({element, operand, std::nullopt});
            }
            continue;
        }
        // The receiver of a pipe whose value was taken apart is the value taken out.
        const bool pipeReceiver = i == 1 && pipe_ != nullptr && pipe_->call == gct;
        const GCT::node_ptr_t &argumentExpr =
            pipeReceiver && pipe_->replacement != nullptr ? pipe_->replacement : gct->at(i);
        node_handle_t inputNode = nullptr;
        if (pipeReceiver && pipe_->receiver != nullptr) {
            inputNode = pipe_->receiver; // built once, before the overloads were asked
        } else {
            inputNode = visitOperand(argumentExpr, gct);
        }
        if (pipeReceiver && pipe_->receiver == nullptr) {
            pipe_->receiver = inputNode;
            if (asTagged(valueTypeOf(nodeTypeOf(inputNode))) != nullptr) {
                throw PipeTagged();
            }
        }
        const std::optional<std::string> name =
            argumentNames != nullptr && i - 1 < argumentNames->size() ? (*argumentNames)[i - 1]
                                                                      : std::nullopt;
        normInputs.push_back({inputNode, argumentExpr, name});
        if (isAssignment && i == 1) {
            Type *place = valueTypeOf(nodeTypeOf(inputNode));
            if (place != nullptr && place->code() == TypeCode::Function) {
                assignedType = place;
            }
        }
    }
    // An ordinary argument may be written by name, and an ordinary parameter may have a
    // default (design principles 4.4): the same binding as the `with` arguments,
    // on the other parameter list. From here on an index into `normInputs` is an ordinary
    // parameter's index — the declaration's order, the one order everything below reads. A rest
    // call is expanded instead of bound: its parameters are read by position.
    if (restTemplate == nullptr) {
        bindArguments(normInputs, targetNode, gct, argumentSignature, false);
    } else if (argumentNames != nullptr) {
        const auto first =
            std::find_if(normInputs.begin(), normInputs.end(), [](const Argument &input) {
                return input.name.has_value();
            });
        if (first != normInputs.end()) {
            diags_->of(SemanticDiag::ParamArgumentCallee)
                .atOrigin(first->expr->origin())
                .commit(*first->name, gct->at(0)->toString());
            throw BuildAbortException();
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
            normInputs.push_back({location, gct, std::nullopt});
        }
    }
    // The with-form of the core slice (design principles 5.4): `xs->slice<axis, start, end>` and
    // `<axis, start, end, step>` are the slice node the subscript and the function form build,
    // whose words after the value are the axis, the two bounds, the step and the mask saying both
    // bounds were written. The with-arguments become those ordinary words, so there is one node
    // and one rule for every consumer of it (its derivative, its checks, its lowering).
    if (nodeIsKind(targetNode, runtime::GCNodeKind::Dref)) {
        const auto &sliceTarget = nodeGraphOf(targetNode)->drefTarget(targetNode);
        if (std::holds_alternative<oper_group_ptr_t>(sliceTarget) &&
            std::get<oper_group_ptr_t>(sliceTarget)->name() == "slice" && normInputs.size() == 1 &&
            (withInputs.size() == 3 || withInputs.size() == 4)) {
            const auto staticWord = [&](int64_t value) {
                node_handle_t word = currGraph_->addStaticDataNode(tt::as_shared<Data>(
                    std::make_shared<PrimaryData<int64_t>>(value)));
                registerNodeOrigin(context_, word, gct, "gir.data");
                return Argument{word, gct, std::nullopt};
            };
            for (const Argument &word : withInputs) {
                normInputs.push_back({word.node, word.expr, std::nullopt});
            }
            if (withInputs.size() == 3) {
                normInputs.push_back(staticWord(1));
            }
            normInputs.push_back(staticWord(3));
            withInputs.clear();
        }
    }
    // The one place the ordinary argument types are built: from the bound nodes, in the
    // declaration's order. A binding that fills a default puts the declaration's own static
    // node there, so the type is the parameter's, and nothing here reads the written order.
    {
        normInputTypes.clear();
        normInputTypes.reserve(normInputs.size());
        for (const Argument &input : normInputs) {
            normInputTypes.push_back(valueTypeOf(callableValueType(input.node)));
        }
    }
    // `v = e` where `v` is a `T?` or a `T!`: the value enters the place's type as it enters a
    // declaration's and a parameter's (design principles 3.8, one relation: entersImplicitly).
    if (isAssignment && normInputs.size() == 2 && normInputTypes.size() == 2) {
        Type *place = normInputTypes[0];
        if (asTagged(place) != nullptr && !place->equals(normInputTypes[1]) &&
            entersImplicitly(place, normInputTypes[1])) {
            if (node_handle_t entered = checkValue(normInputs[1].node, place, normInputs[1].expr)) {
                normInputs[1].node = entered;
                normInputTypes[1]  = place;
            }
        }
    }
    // A variable passed to a `var` parameter is the variable's cell, and the callee's port holds
    // `cell` of what the parameter declares. The cell is written through every reference to it
    // (TupleType::assignableFrom), so its content type is the parameter's: the argument's value
    // type is what the parameter has to take, and the ordinary report below refuses a call whose
    // cell does not fit. The parameter's type is a type of its own (an array with its element type
    // written, `T[..]` for a generic head), so there is no wider spelling of the same value to
    // hand the cell here.

    // A `with` argument may be written by name, and a `with` parameter may have a default
    // (design principles 4.4). The binding puts the arguments in the declaration's order and
    // fills the parameters the call leaves out, so that from here on an index into
    // `withInputs` is a `with` parameter's index. That is the one order everything below
    // reads: `parameterOf(i)` is the i-th `with` parameter, the deferred argument at `i` is
    // typed by that parameter, and `withInputTypes` is built from the same list once, at its
    // own place, after the deferred arguments have their nodes (the typing defers to the
    // call). A default is a static node the declaration already computed: it is in place
    // here and is never deferred.
    bindArguments(withInputs, targetNode, gct, argumentSignature, true);

    // A with-argument whose parameter still had type variables when the callee was visited was
    // left for this point (visitWithNode): the ordinary arguments are typed now, so the
    // signature's variables have their types. `reduce<(a, b) => a + b, 0>`: the element type
    // comes from the array and the accumulator type from the position the call stands at. A
    // variable that nothing binds stays open, and the argument is built without an expected
    // type, as it was before.

    bool hasDeferredWith = false;
    for (const Argument &input : withInputs) {
        hasDeferredWith = hasDeferredWith || input.node == nullptr;
    }
    if (hasDeferredWith) {
        type_bindings_t bindings;
        if (argumentSignature != nullptr) {
            for (size_t i = 0; i < normInputTypes.size() && i < argumentSignature->normTypesCount();
                 ++i) {
                unifyTypeVars(argumentSignature->normTypeAt(i), normInputTypes[i], bindings);
            }
            // The type of the position the call stands at is one more source for a variable the
            // arguments left open, but a position that takes the value by putting it into the
            // form does not say which of the two forms the value has (design principles 3.8,
            // 5.2, `determinedByPosition`). `let v: int? = [1, 2]->reduce<(a: int, b: int):
            // int => a + b, 0>` stands in an `int?` position and the reduction is an `int`, put
            // into the form there; reading the position as the result type made the deferred
            // initial value an `int?` too (`(a: int, b: int) => int, int?`), and the call was
            // refused as a whole with `SemanticDiag::NoMatchingFunction` even though every
            // argument the program wrote is an `int`. A signature whose own exit type is
            // already `T?` keeps the whole position: the form is the call's, not the position's.
            if (expectedResult != nullptr && argumentSignature->hasExitType() &&
                asTagged(argumentSignature->exitType()) == nullptr) {
                unifyTypeVars(
                    argumentSignature->exitType(), determinedByPosition(expectedResult), bindings);
            }
        }
        // `i` is a `with` parameter's index: the binding above put `withInputs` in the
        // declaration's order, so the argument at `i` is the value for parameter `i`.
        const auto parameterOf = [&](size_t i) -> Type * {
            return argumentSignature != nullptr && i < argumentSignature->withTypesCount()
                       ? argumentSignature->withTypeAt(i)
                       : nullptr;
        };
        // A function type is a usable expected type for a lambda when its parameters are
        // concrete; a type variable in its result says nothing and the body decides
        // (completeLambdaSignature). Any other type must be concrete.
        const auto usableExpected = [](Type *wanted) {
            if (wanted == nullptr) {
                return false;
            }
            if (wanted->code() != TypeCode::Function) {
                return !containsTypeVar(wanted);
            }
            const auto *funcType = tt::as_ptr<FunctionType>(wanted);
            for (size_t p = 0; p < funcType->withTypesCount(); ++p) {
                if (containsTypeVar(funcType->withTypeAt(p))) {
                    return false;
                }
            }
            for (size_t p = 0; p < funcType->normTypesCount(); ++p) {
                if (containsTypeVar(funcType->normTypeAt(p))) {
                    return false;
                }
            }
            return true;
        };
        // A value that is not a function (the initial value of a reduction, a shape literal)
        // can be built before the function is: its own type binds the variables the signature
        // still leaves open (`reduce<(a, b) => a + b, 0>` with nothing else fixing the
        // accumulator type takes it from the `0`). Then the functions follow, with their
        // parameter types known.
        for (const bool functionsPass : {false, true}) {
            for (size_t i = 0; i < withInputs.size(); ++i) {
                if (withInputs[i].node != nullptr) {
                    continue;
                }
                Type *pattern = parameterOf(i);
                Type *wanted = pattern != nullptr ? substituteTypeVars(pattern, bindings) : nullptr;
                const bool isFunction = wanted != nullptr && wanted->code() == TypeCode::Function;
                if (isFunction != functionsPass) {
                    continue;
                }
                const bool usable  = usableExpected(wanted);
                withInputs[i].node = visitInPosition(withInputs[i].expr, usable ? wanted : nullptr);
                if (!usable && pattern != nullptr) {
                    // The value's own type is what this position gives the variables: an `int`
                    // literal makes the accumulator an `int`, and the lambda takes that type.
                    unifyTypeVars(pattern, valueTypeOf(nodeTypeOf(withInputs[i].node)), bindings);
                }
            }
        }
    }
    // The one place the call's `with` types are built: from the nodes, in the declaration's
    // order the binding put them in, after the deferred arguments above have theirs.
    for (const Argument &input : withInputs) {
        withInputTypes.push_back(valueTypeOf(nodeTypeOf(input.node)));
    }
    // A `with` parameter declared `var` takes the caller's variable too (design principles 4.4,
    // 3.5), and the reading above applies here as well: the cell is written through every
    // reference to it, and the declared parameter type is the type of what the callee may store.
    // A cell whose content the parameter cannot take is left alone: the ordinary report refuses
    // the call.

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
        // A with-argument of a rest call that names a generic function or a set of overloads
        // has no single function value: the expansion resolves it per call in the body
        // (inlineRestCall; `tree_map<twice>`, `tree_map<bump>`). Let it stay a name.
        const auto callableName = [&](node_handle_t node) {
            if (boundWithArgs_.contains(node)) {
                return false;
            }
            if (!nodeIsKind(node, runtime::GCNodeKind::Dref)) {
                return false;
            }
            if (genericDrefs_.contains(node)) {
                return true;
            }
            const auto &target = nodeGraphOf(node)->drefTarget(node);
            if (std::holds_alternative<graph_vec_ptr_t>(target)) {
                auto graphs = asCompileGraphVec(target);
                return graphs && graphs->size() != 1;
            }
            if (std::holds_alternative<oper_group_ptr_t>(target)) {
                const auto &ops = std::get<oper_group_ptr_t>(target);
                return ops->resolvers().size() != 1 ||
                       ops->resolvers().front().second->fixedSignature() == nullptr;
            }
            return false;
        };
        const auto settle = [&](std::vector<Argument> &inputs, type_vec_t &types, bool with) {
            for (size_t i = 0; i < inputs.size(); ++i) {
                node_handle_t node = inputs[i].node;
                if (node == nullptr || !nodeIsKind(node, runtime::GCNodeKind::Dref)) {
                    continue;
                }
                if (with && restTemplate != nullptr && callableName(node)) {
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
            return resolver.resolve(withInputTypes, normInputTypes).has_value();
        }
        return resolver
            .resolve(
                enteringTypes(funcType, withInputs, withInputTypes, true),
                enteringTypes(funcType, normInputs, normInputTypes, false))
            .has_value();
    };
    // The dimension names a call binds (design principles 5.4), remembered for the checked
    // conversion of the returned value below.
    ExtentValues dimValues;
    ExtentPacks dimPacks;
    const auto enterParameters = [&](FunctionType *funcType) {
        // The parameter types may write their extents over names the signature binds, e.g.
        // `func f(x: f32[n], y: f32[n])`. Bind each name
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
                            .commit(
                                "Argument " + label, mismatch,
                                "the dimensions the earlier arguments bound do not allow it");
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
            yieldToPipe(gct);
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
            addDeclaringModuleOverloads(targetNode, ops->name(), withInputTypes, normInputTypes);
        }
    }

    // A call with an argument of type `never` never runs (design principles 3.9; the author's
    // ruling of 2026-10-26 on `never-argument-scope`): an argument is evaluated before the call
    // it is written in, and this one never gives a value, so the call itself is typed `never` and
    // no overload is chosen for it. The reading stands before the resolution -- a set of
    // overloads that fits is not the question for a call that never runs, and a call that never
    // runs says nothing about a signature either. A `with` argument is evaluated before the call
    // like an ordinary one, so this is one reading over every argument the call is written with:
    // the two lists are walked here, `with` arguments first, and not in two places. The call is
    // not built: what the expression gives is the argument's own node, which is the value that
    // ends the program. One warning per call, naming the argument that causes it.
    for (const bool inWith : {true, false}) {
        const std::vector<Argument> &inputs = inWith ? withInputs : normInputs;
        const type_vec_t &types            = inWith ? withInputTypes : normInputTypes;
        for (size_t i = 0; i < inputs.size() && i < types.size(); ++i) {
            if (types[i] == nullptr || inputs[i].node == nullptr ||
                valueTypeOf(types[i])->code() != TypeCode::Never) {
                continue;
            }
            // Which argument the program wrote: the name it gave the argument, or its position
            // among the arguments as written. An ordinary argument's position is looked up in the
            // call itself, because the binding put the arguments in the declaration's order; a
            // `with` argument is written in the callee's angle brackets, which the call node does
            // not carry, so it is named by its place among the `with` arguments.
            std::string written;
            if (inputs[i].name.has_value()) {
                written = std::format("the argument '{}'", *inputs[i].name);
            } else if (inWith) {
                written = std::format("the `with` argument {}", i + 1);
            } else {
                size_t position = i + 1;
                for (size_t j = 1; j < gct->size(); ++j) {
                    if (inputs[i].expr != nullptr && gct->at(j) == inputs[i].expr) {
                        position = j;
                        break;
                    }
                }
                written = std::format("argument {}", position);
            }
            diags_->of(SemanticDiag::NeverArgumentCall)
                .atOrigin(inputs[i].expr != nullptr ? inputs[i].expr->origin() : gct->origin())
                .commit(written);
            node_handle_t given = inputs[i].node;
            LEAVE("LINK");
            return given;
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
        // build) the instance, which is then called like any other function. A name with
        // several generic functions of it is a set of overloads like any other: the one whose
        // parameters take these arguments is the call (design principles 4.4).
        const generic_template_ptr_t chosenTemplate = chooseGenericOverload(
            genericOverloadsOf(targetNode),
            withInputs,
            normInputs,
            withInputTypes,
            normInputTypes,
            gct);
        if (chosenTemplate != nullptr &&
            (withInputs.size() < chosenTemplate->funcType->withTypesCount() ||
             std::any_of(withInputs.begin(), withInputs.end(), [](const Argument &a) {
                 return a.left;
             }))) {
            completeWithFor(
                chosenTemplate.get(), "", chosenTemplate->funcType, withInputs, withInputTypes);
        }
        targetGraph = resolveGenericCall(
            chosenTemplate,
            withInputs,
            normInputs,
            withInputTypes,
            normInputTypes,
            gct,
            expectedResult);
        if (targetGraph == nullptr) {
            // The instance does not compile for the types these arguments have, and a scope is
            // open that holds the attempt back (`SpeculativeInstantiation`): the call has no
            // function then, which is what a caller above the scope is deciding. The instance
            // that is tried *inside* the attempt makes this reachable -- a rule's `when` whose
            // predicate is generic over a type the program also uses -- so the failure has to be
            // an attempt that failed, not a null instance walked into (
            // on `generic-rule-instantiation-set-and-predicate`, §3).
            throw BuildAbortException();
        }
        if (!targetGraph->funcType()->hasExitType()) {
            diags_->of(SemanticDiag::CallingIncompleteFunction)
                .atOrigin(gct->origin())
                .commit(targetGraph->name(), targetGraph->funcType()->toString());
            throw BuildAbortException();
        }
        // An instance of a generic `#impl` function whose type an executor was found for: a
        // direct call is one OPER of the kernel, exactly as for a non-generic `#impl` function
        // (design principles 10.4). The instance's body stays the
        // semantics, and an instance whose type has no executor falls through to the call of
        // the body, which is what it runs.
        if (std::optional<ImplKernel> kernel = implKernelOfGraph(targetGraph);
            kernel.has_value() && !insideImplBody()) {
            std::vector<StaticArg> kernelStatics;
            kernelStatics.reserve(normInputs.size());
            for (const Argument &input : normInputs) {
                kernelStatics.push_back(staticArgOf(input.node));
            }
            if (const auto resolved = kernel->group->resolveForCall(
                    {},
                    expectedResult,
                    withInputTypes,
                    normInputTypes,
                    kernelStatics);
                resolved.has_value()) {
                targetOperator = *resolved;
                if (inPlaceCalls_.contains(gct)) {
                    // Written by inPlaceRebindingOf, which checked that this kernel has an
                    // in-place form: the call works on the array var/take hands it.
                    const auto form =
                        camel::core::OperatorTable::instance().inPlaceForm(targetOperator->uri());
                    ASSERT(form.has_value(), "An in-place rebinding lost its in-place form.");
                    targetOperator = std::make_shared<OperatorIndex>(
                        targetOperator->name(), targetOperator->funcType(), *form);
                }
                targetNode     = currGraph_->addOperNode(targetOperator, 0);
                stampPermissions(targetNode);
                registerNodeOrigin(context_, targetNode, gct->at(0), "gir.link.oper");
                targetFuncType = targetOperator->funcType();
                targetGraph    = nullptr;
            }
        }
        if (targetGraph != nullptr) {
            currGraph_->addDependencyGraph(targetGraph);
            targetNode     = createFuncDataNode(targetGraph, false, true);
            targetFuncType = targetGraph->funcType();
        }
    } else if (nodeIsKind(targetNode, runtime::GCNodeKind::Dref)) {
        const auto &drefTarget = nodeGraphOf(targetNode)->drefTarget(targetNode);
        if (std::holds_alternative<graph_vec_ptr_t>(drefTarget)) {
            auto graphs = asCompileGraphVec(drefTarget);
            // Overloads are tried in two passes (design principles 5.1): first with the
            // argument types as they are -- a literal has not entered any parameter type yet
            // -- and then with every literal entering the type of the parameter it is given
            // to. Several candidates in the pass that is used is an ambiguity, never settled
            // by declaration order; the same report is made for `f<w>` used as a value
            // (partialApplicationOf).
            // Only the chosen one has to be complete: an overload the call does not mean may
            // still be waiting for its return type.
            // A decorated overload is what its decorators return: it is chosen by that
            // function's parameters and called through it.
            compile_graph_ptr_t chosenDecorated = nullptr;
            // Candidates that took the arguments but whose `when` condition is false: if no
            // other candidate applies, the call is reported as one whose conditions are unmet
            // (design principles 4.4), not as an ordinary mismatch.
            std::vector<std::pair<std::string, std::string>> unmetConditions;
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
            // A declaration takes every count of `with` arguments from its required ones to its
            // full arity (design principles 4.4): the call is completed for each declaration with
            // the defaults that declaration registers, and the arguments of the one chosen are
            // the completed ones.
            const std::vector<Argument> writtenWith = withInputs;
            const type_vec_t writtenWithTypes       = withInputTypes;
            std::unordered_map<const void *, std::pair<std::vector<Argument>, type_vec_t>>
                completedWith;
            const auto leavesOut = [&](FunctionType *funcType) {
                return writtenWith.size() < funcType->withTypesCount() ||
                       std::any_of(writtenWith.begin(), writtenWith.end(), [](const Argument &a) {
                           return a.left;
                       });
            };
            for (const bool entering : {false, true}) {
                if (targetGraph || chosenDecorated) {
                    break;
                }
                std::vector<compile_graph_ptr_t> fitted;
                for (const auto &g : *graphs) {
                    FunctionType *funcType = signatureOf(g);
                    withInputs             = writtenWith;
                    withInputTypes         = writtenWithTypes;
                    if (leavesOut(funcType) &&
                        !completeWithFor(g.get(), "", funcType, withInputs, withInputTypes)) {
                        continue;
                    }
                    if (!accepts(funcType, entering)) {
                        continue;
                    }
                    completedWith[g.get()] = {withInputs, withInputTypes};
                    // A function of the top level not built yet is built now: its return type
                    // may come from its body, its decorated form exists once it is built (step
                    // two of loading builds on demand).
                    ensureFunctionBuilt(g);
                    if (auto when = whenClauses_.find(g.get()); when != whenClauses_.end()) {
                        // The condition is checked at selection, before the candidate is
                        // called: a false one makes it inapplicable, not erroneous.
                        std::unordered_map<std::string, data_ptr_t> withValues;
                        for (size_t i = 0;
                             i < g->funcType()->withTypesCount() && i < withInputs.size();
                             ++i) {
                            data_ptr_t value = constantOfNode(withInputs[i].node);
                            if (value != nullptr) {
                                withValues.emplace(std::string(g->funcType()->argNameAt(i)), value);
                            }
                        }
                        const WhenResult verdict = evaluateWhenCondition(
                            when->second.condition,
                            g->funcType(),
                            {},
                            withValues,
                            nullptr,
                            unitOfFunction(g));
                        if (verdict.fails()) {
                            unmetConditions.emplace_back(g->name(), when->second.text);
                            continue;
                        }
                        if (!verdict.holds()) {
                            reportWhenProblem(verdict, g->name(), when->second.condition);
                        }
                    }
                    fitted.push_back(g);
                }
                if (fitted.empty()) {
                    continue;
                }
                if (fitted.size() > 1) {
                    // Both (or more) take the arguments in this pass: no declaration order
                    // chooses between them. The signatures shown are the ones the call would
                    // be made with, decorated forms included.
                    // Overloads told apart by their conditions are listed each with the condition
                    // it wrote (design principles 4.4).
                    const bool allConditioned =
                        std::all_of(fitted.begin(), fitted.end(), [&](const auto &g) {
                            return whenClauses_.contains(g.get());
                        });
                    if (allConditioned) {
                        std::vector<std::string> listed;
                        for (const auto &g : fitted) {
                            listed.push_back(
                                signatureOf(g)->toString() + " — when " +
                                whenClauses_.at(g.get()).text);
                        }
                        std::ranges::sort(listed);
                        rejectAmbiguousCall(graphs->front()->name(), argumentTypes, listed, gct);
                    }
                    std::vector<FunctionType *> candidates;
                    candidates.reserve(fitted.size());
                    for (const auto &g : fitted) {
                        candidates.push_back(signatureOf(g));
                    }
                    rejectAmbiguousCall(graphs->front()->name(), argumentTypes, candidates, gct);
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
            withInputs     = writtenWith;
            withInputTypes = writtenWithTypes;
            if (targetGraph) {
                if (const auto done = completedWith.find(targetGraph.get());
                    done != completedWith.end()) {
                    withInputs     = done->second.first;
                    withInputTypes = done->second.second;
                }
            }
            if (!targetGraph && !chosenDecorated) {
                yieldToPipe(gct);
                if (!unmetConditions.empty()) {
                    // Every candidate that took the arguments was made inapplicable by its
                    // condition. The report names each candidate and the condition it wrote
                    // (design principles 4.4: the call-site error lists the unmet conditions).
                    std::map<std::string, std::string> listed;
                    for (const auto &[name, text] : unmetConditions) {
                        listed.emplace(name, text);
                    }
                    std::string conditions;
                    for (const auto &[name, text] : listed) {
                        conditions += (conditions.empty() ? "" : "; ") + name + " — when " + text;
                    }
                    diags_->of(SemanticDiag::WhenConditionUnmet)
                        .atOrigin(gct->origin())
                        .commit(graphs->front()->name(), argumentTypes, conditions);
                    throw BuildAbortException();
                }
                // A bf16 or f16 scalar or array among the arguments: no candidate can compute
                // with it, whatever its parameters say, because the language defines no
                // operation on a value of the element type (design principles 7.3). Say
                // that, rather than list overloads the call could never match.
                if (const auto reduced =
                        reducedPrecisionArgumentOf(withInputTypes, normInputTypes)) {
                    diags_->of(SemanticDiag::ReducedPrecisionUnsupported)
                        .atOrigin(gct->origin())
                        .commit(displayNameOf(graphs->front()->name()), typeCodeToString(*reduced));
                    throw BuildAbortException();
                }
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
            if (ops->name() == "__rule_generic") {
                // The maker of a rule whose head declares type parameters: its sides are generic
                // functions, which have no value, so it takes their templates and is called with
                // the id they are registered under (design principles 5.5; the rule of
                // 2026-10-23 on generic-rule-instantiation).
                settleGenericRuleMaker(normInputs, normInputTypes, gct);
            }
            if (ops->name() == "__fact") {
                // The maker of a fact whose head declares type parameters: the functions it is made
                // of are generic, and this call is the instance of its query, so the type arguments
                // they are made at are the ones that instance was resolved with (rewrite design
                // 7.5; design principles 5.9).
                settleGenericFactMaker(normInputs, normInputTypes, gct);
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
            // Candidates that took the arguments but whose own `when` condition is false: if no
            // other candidate applies, the call is reported as one whose conditions are unmet
            // (design principles 4.4), not as an ordinary mismatch and never by writing order.
            std::vector<std::pair<std::string, std::string>> unmetConditions;
            // The arguments as written, for the ambiguity report: the context arguments first,
            // then the ordinary ones (the same rendering an ordinary call's ambiguity uses).
            const auto argumentTypes = [&]() {
                std::vector<std::string> written;
                written.reserve(withInputTypes.size() + normInputTypes.size());
                for (Type *type : withInputTypes) {
                    written.push_back(type->toString());
                }
                for (Type *type : normInputTypes) {
                    written.push_back(type->toString());
                }
                return strutil::join(written, ", ", [](const std::string &s) { return s; });
            };
            // Candidates whose own `when` holds for these arguments: one of them is the call,
            // and more than one is a call with no answer (design principles 4.4:
            // "带 `when` 的候选都适用时，调用处报
            // 歧义，列出两条候选与各自成立的条件"). The declaration-time check asks about one type
            // at a time, so this is the first place the pair can be seen.
            std::vector<std::string> ambiguousConditions;
            // `a op= b` is `a = a op b`: the plain operator is the value node, and the write
            // stores it, so a compound assignment is rewritten before the group is asked for an
            // overload of its own. Whether the store may update the place in memory is the memory
            // planner's question, not a compound operator's (design principles 4.2). Rewriting
            // here, before the resolution, is what gives the compound assignment a value node of
            // the plain operator to hand out where a value is wanted; a compound the plain
            // operator does not define falls through and is reported by the group as written.
            if (withInputs.empty() &&
                camel::core::plainGroupOfCompoundAssignment(ops->name()).has_value()) {
                if (node_handle_t rebound =
                        compoundAssignmentByRebinding(ops->name(), gct, normInputs, normInputTypes)) {
                    LEAVE("LINK");
                    return rebound;
                }
            }
            auto res = resolveGroupedCall(
                ops,
                explicitTypes,
                expectedResult,
                withInputTypes,
                normInputTypes,
                normStatics,
                unmetConditions,
                &ambiguousConditions,
                &withInputs);
            if (res.has_value()) {
                // The with arguments the rule took are the ones it completes (design principles
                // 4.4: a parameter with a default may be left out).
                for (const auto &[ruleUri, rule] : ops->resolvers()) {
                    FunctionType *declared = rule->fixedSignature() != nullptr
                                                 ? rule->fixedSignature()
                                                 : rule->declaredSignature();
                    if (ruleUri == (*res)->uri() && declared != nullptr &&
                        (withInputs.size() < declared->withTypesCount() ||
                         std::any_of(withInputs.begin(), withInputs.end(), [](const Argument &a) {
                             return a.left;
                         }))) {
                        completeWithFor(nullptr, ruleUri, declared, withInputs, withInputTypes);
                    }
                }
            }
            if (!res.has_value()) {
                // A literal operand takes the other operand's type in a named operator call too:
                // `add(x, 2.0)` with an f32 tensor is the f32 sum, as `x * 2.0` already is
                // (design principles 5.1). Tried only where the first resolution failed, so an
                // operand of another meaning in the same position - an index, a shape - keeps its
                // type, and a predicate that still does not fit reports as it always did. The
                // adapted call is resolved again; when that fails too, the original report stands.
                std::vector<Argument> retriedInputs = normInputs;
                type_vec_t retriedTypes            = normInputTypes;
                adaptLiteralOperands(ops->name(), retriedInputs, retriedTypes, gct, true);
                if (retriedTypes != normInputTypes) {
                    std::vector<StaticArg> retriedStatics;
                    retriedStatics.reserve(retriedInputs.size());
                    for (const Argument &input : retriedInputs) {
                        retriedStatics.push_back(staticArgOf(input.node));
                    }
                    // The adapted call is tried with its own candidates and its own conditions;
                    // its answer is dropped when it fails, because the report the call gets is
                    // about the arguments the program wrote (the conditions below stand too).
                    // An ambiguity it finds is not such an answer and is reported below.
                    std::vector<std::pair<std::string, std::string>> retriedUnmet;
                    auto retried = resolveGroupedCall(
                        ops,
                        explicitTypes,
                        expectedResult,
                        withInputTypes,
                        retriedTypes,
                        retriedStatics,
                        retriedUnmet,
                        &ambiguousConditions);
                    if (retried.has_value()) {
                        normInputs     = std::move(retriedInputs);
                        normInputTypes = std::move(retriedTypes);
                        normStatics    = std::move(retriedStatics);
                        res            = retried;
                    }
                }
            }
            if (!ambiguousConditions.empty()) {
                // The candidates are listed each with the condition it wrote, sorted, so that
                // two files holding one program with the declarations in the two orders report
                // one and the same diagnostic.
                rejectAmbiguousCall(ops->name(), argumentTypes(), ambiguousConditions, gct);
            }
            if (!res.has_value() && withInputs.empty()) {
                // `x == null` / `x != null` over a `T?`: the tag of `x`, not a comparison of
                // two values (design principles 3.8).
                if (node_handle_t test =
                        nullComparison(ops->name(), gct, normInputs, normInputTypes)) {
                    LEAVE("LINK");
                    return test;
                }
            }
            if (!res.has_value()) {
                yieldToPipe(gct);
                if (!unmetConditions.empty()) {
                    // Every candidate whose parameters took the arguments was made inapplicable
                    // by its own `when` (design principles 4.4: a rule that does not apply is
                    // not the call's overload, and the call goes on to the other candidates).
                    // The report names each declaration and the condition it wrote, so that a
                    // call that reaches no rule at all says which conditions it failed rather
                    // than which declaration happened to be written first. A declaration is
                    // named by its signature and its condition together: the two arms of one
                    // name share the signature and are told apart by the condition alone, the
                    // way WhenDeclarationOverlap names them.
                    std::vector<std::pair<std::string, std::string>> listed;
                    std::string conditions;
                    for (const auto &[signature, text] : unmetConditions) {
                        if (std::ranges::find(listed, std::pair{signature, text}) != listed.end()) {
                            continue;
                        }
                        listed.emplace_back(signature, text);
                        conditions +=
                            (conditions.empty() ? "" : "; ") + signature + " — when " + text;
                    }
                    const std::string argTypes = std::format(
                        "<{}> ({})",
                        strutil::join(
                            withInputTypes, ", ", [](Type *t) { return t->toString(); }),
                        strutil::join(
                            normInputTypes, ", ", [](Type *t) { return t->toString(); }));
                    diags_->of(SemanticDiag::WhenConditionUnmet)
                        .atOrigin(gct->origin())
                        .commit(ops->name(), argTypes, conditions);
                    throw BuildAbortException();
                }
                // An argument that states no element type (`[]`, an array whose element type
                // nobody wrote) is not a mismatch of types: the type variable of the parameter it
                // stands at can be fixed by nothing, and the call is reported as the type argument
                // it cannot infer (design principles 5.1, 5.2). The candidate that would have
                // taken the argument names the variable.
                if (const std::string name =
                        unstatedBindingForCall(ops, explicitTypes, normInputTypes);
                    !name.empty()) {
                    diags_->of(SemanticDiag::FunctionTypeArgumentNotInferred)
                        .atOrigin(gct->origin())
                        .commit(name, ops->name());
                    throw BuildAbortException();
                }
                // A bf16 or f16 operand (or an array of one) that no overload can take: the
                // operation is one the element type does not have (`%` is an integer operation,
                // and a function with a fixed set of parameter types has no overload for it).
                // Say that, rather than print an overload set the call could never match
                // (design principles 7.3).
                if (const auto reduced =
                        reducedPrecisionArgumentOf(withInputTypes, normInputTypes)) {
                    diags_->of(SemanticDiag::ReducedPrecisionUnsupported)
                        .atOrigin(gct->origin())
                        .commit(displayNameOf(ops->name()), typeCodeToString(*reduced));
                    throw BuildAbortException();
                }
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
                            normStatics)) {
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
if (ops->resolve(asWith, asNorm, statics).has_value()) {
                            // A call of a function with an `#impl` kernel resolves in the
                            // kernel's group: the hint names the declaration, not the kernel.
                            const std::string declared = declaredNameOfKernelGroup(ops);
                            argTypesStr += withArgumentsHint(
                                declared.empty() ? ops->name() : declared, normInputs, normCount);
                            break;
                        }
                    }
                }
                std::string overloadsStr =
                    "\n    " +
                    strutil::join(
                        ops->resolvers(),
                        "\n    ",
                        [this](const std::pair<std::string, resolver_ptr_t> &p) {
                            return calleeNameForMessage(p.first) + ": " + p.second->signature();
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
            // `range(a, b, step)` is the integers `a`, `a + step`, ... below `b` (design
            // principles 4.2), so
            // a step of 0 names no sequence at all: a fault when the step is known only at the
            // run, and the refusal here when it is a constant, where the step is written. A
            // constant step that is not 0 is left to the call: the array it builds is the value
            // of the program, and building a constant one here would be folding, not checking.
            if (targetOperator->uri() == ":struct/range" && normInputs.size() == 3 &&
                withInputs.empty() && gct->size() >= 1 &&
                gct->at(0)->type() == GCT::NodeKind::DREF && gct->at(0)->name() == "range") {
                const auto stepIsZero = [](const data_ptr_t &data) {
                    if (const auto integer = std::dynamic_pointer_cast<LongData>(data);
                        integer != nullptr) {
                        return integer->data() == 0;
                    }
                    const auto integer = std::dynamic_pointer_cast<IntData>(data);
                    return integer != nullptr && integer->data() == 0;
                };
                if (stepIsZero(constantOfNode(normInputs[2].node))) {
                    // The caret is under the step, the argument that says why the call can never
                    // be right.
                    const GCT::node_ptr_t &stepNode = gct->at(gct->size() - 1);
                    const auto where =
                        stepNode->origin() != 0 ? stepNode->origin() : gct->origin();
                    diags_->of(SemanticDiag::RangeStepZero)
                        .atOrigin(where)
                        .commit(
                            sourceTextOf(normInputs[0].expr),
                            sourceTextOf(normInputs[1].expr));
                    throw BuildAbortException();
                }
            }
            // A slice with a constant step of 0 takes no element at all: the same rule as the step of a
            // range, refused where the step is written (design principles 5.4 for the slice, 4.2
            // for the range). The two spellings of the node are one node -- the subscript's
            // `__slice__` and the function form's `slice(t, axis, start, end, step, written)`, both
            // the core's `:op/slice_sub`, or `:op/slice_str` on a string -- so the check reads the node and not the spelling: one
            // fault has one answer, the catalog's `SliceStepZero`, with the caret under the step.
            // The step is the node's fifth ordinary argument (the value, the axis, the two bounds,
            // the step; the sixth is the mask that says which of the two bounds were written, and a
            // call that stops at five words wrote no mask, so it is read as "both bounds", 3). A
            // step that is not constant is a fault of the run instead (RuntimeDiag::InvalidArgument,
            // and a tensor's own slice checks its step with the shape facts it holds).
            if ((targetOperator->uri() == ":op/slice_sub" || targetOperator->uri() == ":op/slice_str" ||
                 targetOperator->uri() == ":op/slice_bytes") &&
                normInputs.size() >= 5 &&
                normInputs.size() <= 6 && withInputs.empty() && gct->size() >= 1 &&
                gct->at(0)->type() == GCT::NodeKind::DREF) {
                const auto stepIsZero = [](const data_ptr_t &data) {
                    if (const auto integer = std::dynamic_pointer_cast<LongData>(data);
                        integer != nullptr) {
                        return integer->data() == 0;
                    }
                    const auto integer = std::dynamic_pointer_cast<IntData>(data);
                    return integer != nullptr && integer->data() == 0;
                };
                if (stepIsZero(constantOfNode(normInputs[4].node))) {
                    // The caret is under the step, the argument that says why the slice can
                    // never be right.
                    const GCT::node_ptr_t &stepExpr = normInputs[4].expr;
                    const auto where = stepExpr != nullptr && stepExpr->origin() != 0
                                           ? stepExpr->origin()
                                           : gct->origin();
                    diags_->of(SemanticDiag::SliceStepZero).atOrigin(where).commit();
                    throw BuildAbortException();
                }
            }
            if (inPlaceCalls_.contains(gct)) {
                // Written by inPlaceRebindingOf, which checked that this overload has an
                // in-place form: the call works on the array:var/take hands it.
                const auto form =
                    camel::core::OperatorTable::instance().inPlaceForm(targetOperator->uri());
                ASSERT(form.has_value(), "An in-place rebinding lost its in-place form.");
                targetOperator = std::make_shared<OperatorIndex>(
                    targetOperator->name(),
                    targetOperator->funcType(),
                    *form);
            }
            rejectLiteralConversion(ops->name(), targetOperator->funcType(), normInputs, gct);
            // The empty literal `[]` stated no element type where it was written, and the type
            // argument the call wrote is what fixes it now (`show::<f32>([])` has an `f32[]`
            // argument, not the `void[]` it was built as): it takes the type of the parameter it
            // stands at, the way it takes the type of any other position it stands in (design
            // principles 3.8, 5.1). An argument already of the parameter's type is left as it is.
            if (!explicitTypes.empty()) {
                for (size_t i = 0; i < normInputs.size(); ++i) {
                    if (i >= targetOperator->funcType()->normTypesCount()) {
                        break;
                    }
                    Type *parameter = targetOperator->funcType()->normTypeAt(i);
                    if (Type *type = valueTypeOf(nodeTypeOf(normInputs[i].node));
                        type != nullptr && type->equals(parameter)) {
                        continue;
                    }
                    if (node_handle_t typed =
                            checkValue(normInputs[i].node, parameter, normInputs[i].expr);
                        typed != nullptr) {
                        normInputs[i].node = typed;
                        normInputTypes[i]  = parameter;
                    }
                }
            }
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
                                    // the lambda (item 1; the parse-side twin is in
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
            if (targetOperator->uri() == camel::execute::kAttemptTransformUri) {
                // `attempt(f)` (design principles 3.9) is a function transform and not a node:
                // the call gives a function value whose body is the marked call of `f`. What the
                // value captures is the argument, so no operand of this graph stands for it.
                node_handle_t transformed = attemptFunctionValue(normInputs[0], gct);
                LEAVE("LINK");
                return transformed;
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
        // A stream's value is its step function (StreamType::stepType), so a call of a stream is
        // one pull: the element with the rest of the stream, or `null` at the end. The call reads
        // the signature the stream type gives it, and nothing is converted -- the value already
        // is that function value -- so no arguments are taken and `s()` is the form.
        if (dataType != nullptr && dataType->code() == TypeCode::Stream) {
            // A pull is one consumer of the stream (design principles 5.4: a stream is read once on
            // each execution path), the other being a `for` over it; the check on the built graph
            // reads this pull from the data edge the call writes.
            dataType = tt::as_ptr<StreamType>(dataType)->stepType();
        }
        ASSERT(
            dataType->code() == TypeCode::Function,
            "Target node of LINK must be a function or operator node.");
        auto *funcType = tt::as_ptr<FunctionType>(dataType);
        if (!accepts(funcType, false) && !accepts(funcType, true)) {
            yieldToPipe(gct);
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

    // `tee(s, n)` reads one stream from two places: the two streams it gives share one buffer,
    // which holds the elements one side has read and the other has not (design principles 5.4).
    // `n` says how wide that buffer is expected to be -- a hint the executor may exceed by growing
    // the buffer -- but no buffer holds fewer than one element, the one being handed over, so a
    // constant width below 1 is refused where it is written, as a constant step of 0 is. A width
    // that is not constant is the executor's to take: it is a hint, and the executor clamps it and
    // grows the buffer, so it never faults. The callee is the library's `tee`, the only function of
    // that name whose stream it splits: a program's own function of that name is left alone.
    // The callee's name without the instantiation a generic function carries (`stream::tee<int>`),
    // so a check for a library function names it as it is written.
    const auto baseGraphName = [](const std::string &name) {
        const size_t generic = name.find('<');
        return generic == std::string::npos ? name : name.substr(0, generic);
    };
    if (targetGraph != nullptr && baseGraphName(targetGraph->name()) == "stream::tee" &&
        normInputs.size() == 2 && targetFuncType->normTypesCount() == 2 &&
        targetFuncType->normTypeAt(0) != nullptr &&
        targetFuncType->normTypeAt(0)->code() == TypeCode::Stream) {
        const auto widthBelowOne = [](const data_ptr_t &data) {
            if (const auto integer = std::dynamic_pointer_cast<LongData>(data); integer != nullptr) {
                return integer->data() < 1;
            }
            const auto integer = std::dynamic_pointer_cast<IntData>(data);
            return integer != nullptr && integer->data() < 1;
        };
        if (widthBelowOne(constantOfNode(normInputs[1].node))) {
            // The caret is under the width, the argument that says why the call can never be right.
            const GCT::node_ptr_t &widthExpr = normInputs[1].expr;
            const auto where                 = widthExpr != nullptr && widthExpr->origin() != 0
                                                   ? widthExpr->origin()
                                                   : gct->origin();
            diags_->of(SemanticDiag::TeeHintNotPositive)
                .atOrigin(where)
                .commit(sourceTextOf(normInputs[1].expr));
            throw BuildAbortException();
        }
    }

    // How an argument is passed. A function (a graph or a function value) and assignment take
    // a `var` argument as the variable itself. Any other operator with a `var` parameter mutates
    // an object in place and takes the object; the call still counts as a write of the variable
    // the object was read from. Every other parameter takes the value.
    // A function, an assignment and a variable operator (`:var/take`, inPlaceRebindingOf) are
    // given the variable itself at a `var` position; any other operator its value.
    const bool takesVariables = targetOperator == nullptr ||
                                targetOperator->name().starts_with("__assn") ||
                                targetOperator->name().starts_with("__var_");
    // Whether a node stands in the block the call is written in. A version of a variable that a
    // branch arm or another block wrote is not what this call reads: what the call reads there is
    // the join, which is not a cell.
    const auto inSameRegion = [&](node_handle_t a, node_handle_t b) {
        if (a == nullptr || b == nullptr || nodeGraphOf(a) != nodeGraphOf(b)) {
            return false;
        }
        runtime::GraphDraft *draft = nodeDraftOf(a);
        const draft_node_ref_t first  = nodeIdOf(a);
        const draft_node_ref_t second = nodeIdOf(b);
        if (!draft->alive(first) || !draft->alive(second)) {
            return false;
        }
        return draft->nodeRegion(first) == draft->nodeRegion(second);
    };
    // An argument as it is linked: the node the call reads, the variable the callee writes at a
    // `var` position (if any), and how it is passed.
    struct Passed {
        node_handle_t node;
        node_handle_t written;
        LinkType link;
        // The port carries the version the variable's nearest write made, so the call is ordered
        // after that write by the data edge itself and needs no control input for it.
        bool readsVersion = false;
    };
    // The node to link and, at a `var` position, the variable the callee writes (if any).
    const auto passArgument = [&](const Argument &arg, bool isVar) {
        if (!isVar) {
            return Passed{valueOf(arg.node), node_handle_t{}, LinkType::Norm};
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
            // A `var` handed to another graph is read before the callee writes it, and what that
            // read takes is the version the variable's nearest write made, not the cell the
            // first write made: the port carries that version as a data edge, and the control
            // input the writer used to stand in for it is gone with it (linkCaptureSites links a
            // capture the same way; Builder::valueVersionOf says what a version is). The version
            // is the cell the write stored into -- `:op/assn` gives that cell back as its own
            // value -- so the callee still writes the variable's one cell, and the write the
            // callee does is recorded where the call is built (recordWrite). The `var` position
            // of an assignment or of a variable operator is the write itself rather than a read
            // of the cell, so it keeps the cell (docs/design/effects/edges.cn.md 5.6).
            if (named && targetOperator == nullptr) {
                if (node_handle_t version = valueVersionOf(variable);
                    version != nullptr && inSameRegion(version, targetNode)) {
                    return Passed{version, variable, LinkType::Norm, true};
                }
            }
            return Passed{variable, variable, LinkType::Norm};
        }
        return Passed{valueOf(arg.node), named ? arg.node : node_handle_t{}, LinkType::Norm};
    };

    // All arguments are read before the call is recorded as a writer: a variable passed both
    // by value and to a `var` parameter (`f(c, c)`) is read before the call, not after it.
    std::vector<Passed> passed;
    bool hasVarArgument = false;
    for (size_t i = 0; i < withInputs.size(); i++) {
        bool isVar = i < targetFuncType->withTypesCount() && targetFuncType->withIsVarAt(i);
        Passed arg = passArgument(withInputs[i], isVar);
        arg.link   = LinkType::With;
        passed.push_back(arg);
        hasVarArgument |= isVar;
    }
    for (size_t i = 0; i < normInputs.size(); i++) {
        bool isVar = i < targetFuncType->normTypesCount() && targetFuncType->normIsVarAt(i);
        Passed arg = passArgument(normInputs[i], isVar);
        arg.link   = LinkType::Norm;
        passed.push_back(arg);
        hasVarArgument |= isVar;
    }
    for (const Passed &arg : passed) {
        tryRemoveCtrlLink(arg.node, targetNode);
        linkNodes(arg.link, arg.node, targetNode);
        if (arg.readsVersion) {
            // The port carries the nearest write's version, and that data edge is what runs the
            // write this read uses and orders the call after it (design principles 3.5, F3): the
            // control input the writer used to stand in for it is not written.
            continue;
        }
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
    // What the call hands on beside its return value: the new version of each captured variable
    // it writes, in the callee's capture order. The mask was decided when the node was made and
    // is in its payload; here it says which of the call's With inputs are those versions, and
    // each is recorded against the variable it is of, so a read of that variable after the call
    // takes the access node of the part (Builder::valueVersionOf) instead of the cell and a
    // control input.
    uint32_t writtenMask = 0;
    if (targetGraph) {
        if (auto mask = callWriteMasks_.find(targetNode); mask != callWriteMasks_.end()) {
            writtenMask = mask->second;
        }
    }
    const bool callReturnsValue =
        targetFuncType != nullptr && targetFuncType->hasExitType() &&
        !isVoidGraphExitType(targetFuncType->exitType());
    if (writtenMask != 0) {
        CallValue value;
        value.hasReturn = callReturnsValue;
        const auto table = capturedValues_.find(targetGraph.get());
        for (size_t k = 0; table != capturedValues_.end() && k < table->second.size(); ++k) {
            if ((writtenMask & (uint32_t{1} << k)) == 0) {
                continue;
            }
            value.variables.push_back(
                captureInto(currGraph_, table->second[k].origin, table->second[k].name));
        }
        callValues_[targetNode] = std::move(value);
    }
    std::vector<node_handle_t> capturedWrites;
    for (const CapturedUse &use : captured) {
        orderAfterLastWriter(use.variable, targetNode);
        if (use.written) {
            capturedWrites.push_back(use.origin);
        }
    }
    if (!synced_) {
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
            recordWrite(use.variable, targetNode, true);
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
        // The return type may speak about the names the parameters bound (`: f32[n, k]`).
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
            Type *refined =
                hasNames ? const_cast<Type *>(namedExit->withExtentValues(dimValues, dimPacks))
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
    // What an assignment gives where a value is wanted is the value it wrote: the write's second
    // operand, one data edge, and no `:var/get` (design principles 2.1: the value
    // and the version are two, and the write itself gives the version -- the cell it writes
    // into). Written as a statement, or where the position wants no value at all (the last
    // statement of a `: void` body), the write stands for it: the write is what the statement
    // does.
    const bool assignment =
        gct->size() == 3 && gct->at(0) != nullptr && gct->at(0)->type() == GCT::NodeKind::DREF &&
        gct->at(0)->name().starts_with("__assn");
    // Whether a value is wanted here: the statement's value is not dropped (`unusedStatement`),
    // and the position is not a `void` one (a body declared `: void` takes its last statement's
    // value in no position -- `visitExecNode` builds it and drops it). A position that declares no
    // type still wants the value: a body whose exit type is inferred is one, and that is the whole
    // reason its exit type can be inferred. `expectedResult` is the position's type as its caller
    // passed it; `expectedValueType_` is nil for the whole of a call (`callScope`).
    const bool valueWanted =
        !unusedStatement &&
        !(expectedResult != nullptr && isVoidGraphExitType(expectedResult));
    if (assignment && valueWanted && normInputs.size() == 2) {
        // What the position takes is the value the write wrote -- the write's second operand, one
        // data edge, no read of the cell and no new anchor. The write is an effect of its own and
        // the value is not it, and nothing else in the graph depends on the write here, so the
        // value is handed on behind a gate whose other input is the write: that is what makes the
        // write run where the write's version has no reader of its own.
        node_handle_t value = valueOf(normInputs[1].node);
        node_handle_t gate  = currGraph_->addGateNode(nodeTypeOf(value));
        linkNodes(LinkType::Norm, value, gate);
        linkNodes(LinkType::Ctrl, targetNode, gate);
        LEAVE("LINK");
        return gate;
    }
    LEAVE("LINK");
    if (writtenMask != 0 && callReturnsValue && valueWanted) {
        // The value the position takes is the call's return value, the first part of what the
        // call hands on. A call whose value nothing wants builds no access node for it.
        return callValuePartOf(targetNode, 0);
    }
    if (writtenMask != 0 && !valueWanted && !unusedStatement && expectedResult != nullptr &&
        isVoidGraphExitType(expectedResult)) {
        // A position that expects `void` takes no value, and a branch joins the value that
        // stands here with the values of its other arms: what the call hands on beside its
        // return value is the new version of each captured variable it writes, and a version is
        // a `var` cell -- a place, not a value a position takes. It is taken only where a
        // reader takes it, through the access node of the call (Builder::valueVersionOf). What
        // stands here is the callee's own value -- nothing -- after the call, so an arm joining
        // this call with a `void` one stays `void` and not a `var T?` (design principles 190,
        // 189: the join of such arms takes no value). The value is handed on behind a gate whose
        // other input is the call, which is what the position does (`setArmResult`).
        node_handle_t empty = currGraph_->addStaticDataNode(Data::null());
        node_handle_t gate  = currGraph_->addGateNode(nodeTypeOf(empty));
        linkNodes(LinkType::Norm, empty, gate);
        linkNodes(LinkType::Ctrl, targetNode, gate);
        LEAVE("LINK");
        return gate;
    }
    return targetNode;
}

} // namespace camel::compile::gir
