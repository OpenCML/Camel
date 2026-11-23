// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Which operator a written call means: the overloads the program declares and the ones the
 * modules it imports declare (operatorOverloadsOf, addDeclaringModuleOverloads,
 * hasDeclaringModuleOperator), the group a call is resolved in and the graph the chosen answer
 * gives an operand or the result (chooseOperatorOverload, overloadValueFor, answerValueGraphOf,
 * settledAnswerOf).
 *
 * A write is in builder_operator_writes.cpp, the interval `a..b` and the composition `f <> g` in
 * builder_composition.cpp, and the graph a LINK node becomes in builder_call.cpp.
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
#include "camel/core/type/composite/tuple.h"
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
#include <set>
#include <stdexcept>
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

std::optional<Builder::OperatorOverloads> Builder::mergedOverloadsNamed(const std::string &name) {
    // A name a body-less declaration and a declaration with a body both write is one overload
    // set, at the one layer both are reachable from. `operatorOverloadsOf` already gathers the
    // rules of the name's operator group and the functions of the name this module and its
    // imports declare (design principles 10.2), so the set is what that returns. What it does
    // not ask is whether the name has a body at all, and a name with no body keeps the
    // resolution it had: this answers nullopt for it, and the lookup goes on as before.
    const ModuleUnit *unit = currentModuleUnit();
    // A name has a body at the module's top level when one of its declaration tables holds it
    // (`graphScope` for a function with a body, `templateScope` for a generic one).
    const bool declaredHere = unit != nullptr && declaredAtTopLevel(*unit, name);
    const bool importedHere = module_ != nullptr && importedEntity(Reference(name)).has_value();
    if (!declaredHere && !importedHere) {
        return std::nullopt;
    }
    OperatorOverloads set = operatorOverloadsOf(name);
    // A generic declaration with a body is a template, not a graph, and `operatorOverloadsOf`
    // collects templates only for the names a program can overload (`__add__`). A name whose
    // body is generic is one of these: the templates visible here are the functions of the
    // name too, and a call chooses among them and the rules alike.
    std::vector<generic_template_ptr_t> templates;
    for (template_scope_ptr_t scope = templateScope_; scope; scope = scope->outer()) {
        const auto found = scope->get(name, false);
        if (!found.has_value()) {
            continue;
        }
        if (GenericGroup *group = genericGroupOf(found.value()); group != nullptr) {
            for (const auto &tmpl : *group) {
                if (std::ranges::find(templates, tmpl) == templates.end()) {
                    templates.push_back(tmpl);
                }
            }
        }
    }
    for (const auto &tmpl : templates) {
        if (std::ranges::find(set.templates, tmpl) == set.templates.end()) {
            set.templates.push_back(tmpl);
        }
    }
    if (set.ops == nullptr || set.ops->resolvers().empty() ||
        (set.graphs.empty() && set.templates.empty())) {
        // No operator group of the name to merge with, or no body of the name to merge into it:
        // the name keeps the resolution it had.
        return std::nullopt;
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
//
// The module that is asked is the one the type's declaration records (5.2: a named type's
// identity is its name and the declaring module), not a name of it: the declaration is where the
// reference was made, and nothing about which modules a program happens to hold -- the unit being
// built, the user modules loaded, the prelude, the imports -- can decide whether the lookup
// reaches it.

std::vector<std::pair<Module *, std::string>>
Builder::declaringModulesOf(const type_vec_t &withTypes, const type_vec_t &normTypes) {
    std::vector<std::pair<Module *, std::string>> found;
    const auto add = [&](Type *type) {
        type = valueTypeOf(type);
        if (type == nullptr) {
            return;
        }
        Module *module = nullptr;
        std::string name;
        if (type->code() == TypeCode::Struct) {
            const auto *structType = tt::as_ptr<StructType>(type);
            if (structType->named()) {
                module = structType->decl()->module();
                name   = structType->decl()->name();
            }
        } else if (const auto *enumType = asEnumType(type); enumType != nullptr) {
            module = enumType->decl()->module();
            name   = enumType->decl()->name();
        }
        // The module this unit builds is already in the lexical scope of the expression.
        if (module == nullptr || module == module_.get()) {
            return;
        }
        if (std::ranges::find(found, std::make_pair(module, name)) == found.end()) {
            found.emplace_back(module, std::move(name));
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

bool Builder::addDeclaringModuleOverloads(
    node_handle_t dref, const std::string &group, const type_vec_t &withTypes,
    const type_vec_t &normTypes, const std::string &writtenGroup) {
    if (instantiationDepth_ == 0 || dref == nullptr) {
        // No dependent type variable: the operator is looked up where it is written, as before.
        return false;
    }
    const auto declaring = declaringModulesOf(withTypes, normTypes);
    if (declaring.empty()) {
        return false;
    }
    // `!=` and `!==` have no overloads of their own: an overload of `==` gives the first and an
    // overload of `===` the second (design principles 4.2, 4.3), exactly as for the overloads
    // the program declares (operatorDrefOf). What the program wrote says that, not the group
    // the call resolves in: for the built-in rule the DREF's group is already the comparison's.
    const char *negatedWhole = negatedWholeOf(writtenGroup);
    const bool negated       = negatedWhole != nullptr;
    const std::string wanted = negated ? std::string(negatedWhole) : group;
    auto known               = operatorOverloads_.find(dref);
    if (known == operatorOverloads_.end()) {
        known = operatorOverloads_.emplace(dref, operatorOverloadsOf(group)).first;
    }
    OperatorOverloads &set = known->second;
    bool added             = false;
    for (const auto &[module, typeName] : declaring) {
        for (const auto &overload : exports_->exportedOperatorOverloads(*module, wanted)) {
            const entity &exported = overload.overloads;
            if (camel::core::module::detail::EntityAccess::isGraphSet(exported)) {
                for (const auto &graph : *asCompileGraphSet(
                         camel::core::module::detail::EntityAccess::graphSet(exported))) {
                    if (std::ranges::find(set.graphs, graph) == set.graphs.end()) {
                        set.graphs.push_back(graph);
                        set.moduleOf[graph.get()] = module->name();
                        added                     = true;
                    }
                }
            } else if (camel::core::module::detail::EntityAccess::isGenericTemplate(exported)) {
                for (const generic_template_ptr_t &generic : genericTemplatesOfEntity(exported)) {
                    if (std::ranges::find(set.templates, generic) == set.templates.end()) {
                        set.templates.push_back(generic);
                        set.moduleOf[generic.get()] = module->name();
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
    for (const auto &[module, typeName] : declaringModulesOf(withTypes, normTypes)) {
        if (!exports_->exportedOperatorOverloads(*module, group).empty()) {
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
        where = "module '" + module_->name() + "'";
    }
    std::string text =
        "; searched the operators visible in " + where + " (where the expression is)";
    text += declaring.size() == 1 ? " and those of module " : " and those of modules ";
    for (size_t i = 0; i < declaring.size(); ++i) {
        if (i > 0) {
            text += i + 1 == declaring.size() ? " and " : ", ";
        }
        text +=
            "'" + declaring[i].first->name() + "' (which declares '" + declaring[i].second + "')";
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
    // `!=` is not overloaded itself: an overload of `==` gives it (design principles 4.2, 4.3).
    // Where one applies, the call is the negation of that overload's call; where none does, it is
    // the negation of the built-in `==` as well, and the DREF below names `==`'s group so that the
    // call itself resolves it and builds the negation over its one result (visitLinkNode). The
    // built-in group path does not read negatedEqualities_ -- its only reader is the branch
    // above, where a declared overload was chosen -- so nothing is recorded for it here.
    bool negated = false;
    if (const char *whole = negatedWholeOf(group)) {
        if (OperatorOverloads equality = operatorOverloadsOf(whole);
            !equality.graphs.empty() || !equality.templates.empty()) {
            set.graphs    = std::move(equality.graphs);
            set.templates = std::move(equality.templates);
            set.moduleOf  = std::move(equality.moduleOf);
            negated       = true;
        } else if (equality.ops != nullptr) {
            // The built-in `==` / `===` is a registry rule of the operator table (`:op/eq_*`,
            // `:op/eq`), not a declared overload, so the rule itself is the group the DREF
            // names. `ops` is a shared pointer into that table, which outlives this DREF: a
            // borrow, no copy.
            set.ops = equality.ops;
        }
    }
    GUARD(set.ops != nullptr, "An operator has no group.");
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
    // The name the reports use: `operator +` for a symbol, the name itself for a name a call
    // reaches without a symbol (`map` is an operator group the prelude declares, and a call of
    // it is written like any other call).
    const std::string display = camel::core::displayNameOf(set.ops->name());
    std::vector<StaticArg> statics;
    for (const Argument &input : normInputs) {
        statics.push_back(staticArgOf(input.node));
    }
    const bool builtinAccepts = set.ops->resolve(withTypes, normTypes, statics).has_value();
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
                        "{}: {}{}",
                        display,
                        graph->funcType()->toString(),
                        where(graph.get())));
            }
            for (const auto &tmpl : templates) {
                overloads.push_back(
                    std::format(
                        "{}: {}{}",
                        display,
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
                display,
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
        factory.addWithArg(
            nameAt(i, "__w", i),
            opType->withTypeAt(i),
            false,
            opType->withNeedsValueAt(i));
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
    try {
        bodyStates_[graph.get()] = BodyState::Open;
        node_handle_t call       = graph->addOperNode(op, 0);
        stampPermissions(call);
        if (macroOperator) {
            setNodeMacro(call, true);
        }
        registerNodeOrigin(context_, call, origin, "gir.link.oper");
        for (draft_node_ref_t port : graph->view().withPorts()) {
            linkNodes(LinkType::With, graph->view().ref(port), call);
        }
        for (draft_node_ref_t port : graph->view().normPorts()) {
            linkNodes(LinkType::Norm, graph->view().ref(port), call);
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

std::optional<Builder::SettledAnswer> Builder::settledAnswerOf(
    const oper_group_ptr_t &ops, const std::string &ruleUri,
    const camel::core::type::type_vec_t &types, Type *knownType) {
    if (types.empty()) {
        // No type argument was written, so no rule can be asked what it answers about one: a call
        // of it is a call, and a name of it is not yet a value of any particular type.
        return std::nullopt;
    }
    for (const auto &[uri, rule] : ops->resolvers()) {
        if (rule == nullptr || (!ruleUri.empty() && uri != ruleUri)) {
            continue;
        }
        const std::optional<slot_t> answer = rule->valueFromTypeArgs(types);
        if (!answer.has_value()) {
            // This rule reads something a run has: a call of it is a node, and a name of it is a
            // function that calls it. Not an answer, and not this rule's question.
            continue;
        }
        // The answer is the call's result, so its type is the one `resolveForCall` promises for the
        // same type arguments -- unless the call is already resolved and holds that type.
        Type *answerType = knownType;
        if (answerType == nullptr) {
            const std::optional<FunctionType *> callType =
                rule->resolveForCall(types, nullptr, {}, {}, static_args_t{});
            answerType =
                callType.has_value() && *callType != nullptr ? (*callType)->exitType() : nullptr;
        }
        if (answerType == nullptr) {
            return std::nullopt;
        }
        return SettledAnswer{*answer, answerType};
    }
    return std::nullopt;
}

compile_graph_ptr_t
Builder::answerValueGraphOf(Type *answerType, slot_t answer, const GCT::node_ptr_t &origin) {
    if (answerType == nullptr) {
        return nullptr;
    }
    // Its own type object (a graph completes its function type in place): no parameters, and the
    // answer's type as the result -- the call it stands for takes no argument either.
    FunctionTypeFactory factory;
    factory.setExitType(answerType);
    FunctionType *type        = factory.build();
    compile_graph_ptr_t graph = enterScope(type);
    registerGraphOrigin(context_, graph, origin, "gir.answer.graph");
    try {
        bodyStates_[graph.get()] = BodyState::Open;
        // The body is the value: a settled answer is a constant of the program, and no port of
        // this function is read (`FuncTypeResolver::valueFromTypeArgs`).
        auto &draft         = graph->transaction().edit();
        node_handle_t value = draft.ref(
            draft.materializeStaticValue(answer, answerType, runtime::kGCNodeFlagConstant));
        registerNodeOrigin(context_, value, origin, "gir.answer.graph");
        setGraphReturnFromResult(graph, nodeIdOf(value), nodeIdOf(value), answerType);
        bodyStates_[graph.get()] = BodyState::Built;
        registerBuiltBody(graph);
    } catch (...) {
        bodyStates_[graph.get()] = BodyState::Built;
        leaveScope();
        throw;
    }
    leaveScope();
    return graph;
}

node_handle_t Builder::attemptFunctionValue(const Argument &f, const GCT::node_ptr_t &origin) {
    // The argument is what the call would pass: a variable is read here, once, and the value
    // read is what the function captures.
    node_handle_t fValue = valueOf(f.node);
    Type *fType          = valueTypeOf(nodeTypeOf(fValue));
    GUARD(
        fType != nullptr && fType->code() == TypeCode::Function,
        "The transform is given a function value.");
    FunctionType *type = camel::execute::attemptFunctionType(tt::as_ptr<FunctionType>(fType));
    GUARD(type != nullptr, "The resolver accepted a function the transform cannot take.");

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
        for (draft_node_ref_t port : graph->view().normPorts()) {
            const std::string name = "$attempt" + std::to_string(index++);
            nodeScope_->insert(name, graph->view().ref(port));
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
            if (ruleVerdictForCall(
                    resolver,
                    ops->name(),
                    withTypes,
                    normTypes,
                    nullptr,
                    explicitTypes) != RuleVerdict::Applies) {
                continue;
            }
            if (chosen) {
                return nullptr;
            }
            chosen = std::make_shared<OperatorIndex>(ops->name(), *type, uri);
        }
        if (chosen) {
            explicitOperatorTypes_.erase(node);
            // A call that is already its own answer (`extent_of::<float[2, 3]>`) is a function of
            // no parameters that gives it, not a call of the operator: the answer is a fact of the
            // type arguments written with this name, and nothing in the graph or at run time reads
            // them back. `settledAnswerOf` is where the rule is asked; this position builds the
            // function that gives what it answers, as the two other positions that take such a
            // name as a value do.
            if (const auto settled = settledAnswerOf(
                    ops,
                    chosen->uri(),
                    explicitTypes,
                    chosen->funcType() != nullptr ? chosen->funcType()->exitType() : nullptr)) {
                if (compile_graph_ptr_t graph =
                        answerValueGraphOf(settled->type, settled->value, origin)) {
                    return createFuncDataNode(graph, true, false);
                }
            }
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
        auto result =
            resolver
                ->resolveForCall(explicitTypes, expectedResult, ruleWith, normTypes, normStatics);
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
        // A variable only the result mentions was fixed by the type the call is expected to
        // give: the condition is asked with it.
        type_vec_t verdictTypes = explicitTypes;
        if (explicitTypes.empty()) {
            if (const auto *declared =
                    dynamic_cast<const DeclaredFuncTypeResolver *>(resolver.get());
                declared != nullptr && !declared->resolve(ruleWith, normTypes).has_value()) {
                if (auto fromExpected = declared->typesFromExpected(expectedResult)) {
                    verdictTypes = std::move(*fromExpected);
                }
            }
        }
        const RuleVerdict verdict = ruleVerdictForCall(
            resolver,
            ops->name(),
            ruleWith,
            normTypes,
            &condition,
            verdictTypes);
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
    // A pack among the `with` parameters takes whatever is written beyond the fixed ones, and
    // is not a parameter a default could fill.
    const size_t arity = signature->withTypesCount() - (signature->hasWithRest() ? 1 : 0);
    if (withTypes.size() > arity) {
        return withTypes; // too many: the rule says so itself
    }
    const auto native    = nativeDefaults_.find(uri);
    type_vec_t completed = withTypes;
    completed.resize(arity, nullptr);
    for (size_t i = 0; i < arity; ++i) {
        const bool written =
            i < withTypes.size() &&
            !(withInputs != nullptr && i < withInputs->size() && (*withInputs)[i].left);
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
    if (operatorOverloadsOf(group).ops->resolve({}, normTypes, statics).has_value()) {
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

} // namespace camel::compile::gir
