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
 * Created: Oct. 14, 2026
 * Updated: Oct. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * What a name written in a function body or an initializer stands for (design principles
 * 10.2, "模块与命名空间"; the graph model, section 8). Namespaces are not objects: a name of
 * a module is a path (`Tools::inc`), and these rules only decide which path a written name
 * means. There is one rule of precedence, the first layer that has the name wins:
 *
 *   1. local names (parameters, `let`s of the body, captured names of enclosing functions);
 *   2. the module's own names, from the innermost namespace the code is written in out to
 *      the top level (N1); the module's own name may qualify them (N7);
 *   3. names imported by name, or reached through an imported module or namespace (`m::f`);
 *   4. names a `using namespace` in force brings (N2: a `using` applies to the block it is
 *      written in; two that bring things that cannot be told apart are AmbiguousName where
 *      the name is used);
 *   5. what the built-in module and the modules of operators bring along.
 *
 * Within a layer, functions of one name form a set of overloads; across layers nothing is
 * merged, except the operators (`__add__` and the like), whose overloads come from everywhere.
 */

#include "builder.h"
#include "builder_support.h"

#include "core/module/entity_internal.h"

using namespace camel::core::error;
using namespace camel::core::module;

namespace camel::compile::gir {

namespace {

/// `a::b` joined from a path and a name.
std::string joined(const std::vector<std::string> &path, size_t depth, const std::string &name) {
    std::string out;
    for (size_t i = 0; i < depth; ++i) {
        out += path[i] + "::";
    }
    return out + name;
}

} // namespace

bool Builder::isOperatorName(const std::string &name) {
    return name.size() > 4 && name.starts_with("__") && name.ends_with("__");
}

Builder::ModuleUnit *Builder::currentModuleUnit() const {
    if (!module_) {
        return nullptr;
    }
    const auto it = units_.find(module_.get());
    return it == units_.end() ? nullptr : it->second.get();
}

bool Builder::declaredLocally(const std::string &name) const {
    const ModuleUnit *unit = currentModuleUnit();
    if (unit == nullptr) {
        return false;
    }
    // The scopes between the current one and the module's top level, which are not looked at.
    const auto below = [&](auto scope, const auto &top) {
        for (; scope && scope != top; scope = scope->outer()) {
            if (scope->has(name, false)) {
                return true;
            }
        }
        return false;
    };
    return below(nodeScope_, unit->nodeScope) || below(graphScope_, unit->graphScope) ||
           below(templateScope_, unit->templateScope) ||
           below(decoratedScope_, unit->decoratedScope);
}

bool Builder::declaredAtTopLevel(const ModuleUnit &unit, const std::string &name) const {
    return unit.nodeScope->has(name, false) || unit.values.contains(name) ||
           unit.graphScope->has(name, false) || unit.templateScope->has(name, false) ||
           unit.decoratedScope->has(name, false);
}

std::string Builder::moduleLevelName(const std::string &written) const {
    const ModuleUnit *unit = currentModuleUnit();
    if (unit == nullptr || declaredLocally(written)) {
        return written;
    }
    // `shapes::area` in the module `shapes` is `area` (N7).
    std::string own = written;
    for (const std::string &self : {module_->name(), module_->declaredName()}) {
        if (!self.empty() && own.starts_with(self + "::")) {
            own = own.substr(self.size() + 2);
            break;
        }
    }
    // From the innermost namespace out (N1).
    for (size_t depth = namespacePath_.size() + 1; depth-- > 0;) {
        const std::string candidate = joined(namespacePath_, depth, own);
        if (declaredAtTopLevel(*unit, candidate)) {
            return candidate;
        }
    }
    return written;
}

std::optional<Builder::UsingMatch>
Builder::throughUsing(const std::string &written, const GCT::node_ptr_t &at) {
    const ModuleUnit *unit = currentModuleUnit();
    if (unit == nullptr || usingFrames_.empty()) {
        return std::nullopt;
    }
    using camel::core::module::detail::EntityAccess;
    std::vector<UsingMatch> matches;
    for (const auto *frame : usingFrames_) {
        if (frame == nullptr) {
            continue;
        }
        for (const Reference &using_ : *frame) {
            const std::string prefixed = using_.toString() + "::" + written;
            UsingMatch match;
            match.shown = prefixed;
            // A namespace of the module itself, relative to where the code is written.
            for (size_t depth = namespacePath_.size() + 1; depth-- > 0;) {
                const std::string candidate = joined(namespacePath_, depth, prefixed);
                if (declaredAtTopLevel(*unit, candidate)) {
                    match.ownName = candidate;
                    match.shown   = module_->qualifiedName(Reference(candidate));
                    break;
                }
            }
            if (match.ownName.empty()) {
                if (!module_->hasImportedRef(prefixed, ImportLayer::Named)) {
                    continue;
                }
                match.entity = importedEntity(prefixed, ImportLayer::Named);
                if (!match.entity) {
                    continue;
                }
                // Shown by its full name in the module it comes from, whatever the import
                // and the `using` called the module and the namespace (an alias, a name
                // imported by itself).
                if (const auto source = module_->importedSource(prefixed, ImportLayer::Named)) {
                    match.shown = source->first->qualifiedName(source->second);
                }
            }
            const bool known = std::ranges::any_of(matches, [&](const UsingMatch &have) {
                if (!match.ownName.empty()) {
                    return have.ownName == match.ownName;
                }
                if (!have.entity || EntityAccess::isNode(*have.entity) !=
                                        EntityAccess::isNode(*match.entity)) {
                    return false;
                }
                if (EntityAccess::isNode(*match.entity)) {
                    return EntityAccess::node(*have.entity) == EntityAccess::node(*match.entity);
                }
                if (EntityAccess::isGraphSet(*match.entity) &&
                    EntityAccess::isGraphSet(*have.entity)) {
                    return *EntityAccess::graphSet(*have.entity) ==
                           *EntityAccess::graphSet(*match.entity);
                }
                return false;
            });
            if (!known) {
                matches.push_back(std::move(match));
            }
        }
    }
    if (matches.empty()) {
        return std::nullopt;
    }
    if (matches.size() == 1) {
        return matches.front();
    }
    // Several: functions of different parameter types from each form one set of overloads;
    // anything else (values, generic functions, operators, the same parameter types twice)
    // cannot be told apart.
    auto merged = std::make_shared<EntityAccess::graph_set_t>();
    bool mergeable = true;
    for (const UsingMatch &match : matches) {
        std::shared_ptr<EntityAccess::graph_set_t> graphs;
        if (!match.ownName.empty()) {
            if (auto own = unit->graphScope->get(match.ownName, false);
                own.has_value() && !unit->decoratedScope->has(match.ownName, false)) {
                graphs = std::make_shared<EntityAccess::graph_set_t>((*own)->begin(), (*own)->end());
            }
        } else if (EntityAccess::isGraphSet(*match.entity)) {
            graphs = EntityAccess::graphSet(*match.entity);
        }
        if (graphs == nullptr) {
            mergeable = false;
            break;
        }
        for (const auto &graph : *graphs) {
            for (const auto &have : *merged) {
                if (sameParameterTypes(have->funcType(), graph->funcType())) {
                    mergeable = false;
                }
            }
            merged->push_back(graph);
        }
    }
    if (!mergeable) {
        std::string sources;
        for (const UsingMatch &match : matches) {
            sources += (sources.empty() ? "" : ", ") + match.shown;
        }
        diags_->of(SemanticDiag::AmbiguousName).atOrigin(at->origin()).commit(written, sources);
        throw BuildAbortException();
    }
    UsingMatch overloads;
    overloads.shown  = written;
    overloads.entity = EntityAccess::makeGraphSet(std::move(merged));
    return overloads;
}

bool Builder::sameParameterTypes(FunctionType *a, FunctionType *b) {
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
}

} // namespace camel::compile::gir
