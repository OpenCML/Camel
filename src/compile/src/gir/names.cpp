// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * What a name written in a function body or an initializer stands for (design principles
 * 10.2, "模块与命名空间"; the graph model, section 8). Namespaces are not objects: a name of
 * a module is a path (`Tools::inc`), and these rules only decide which path a written name
 * means.
 *
 * There is one rule of precedence -- the first row that has the name wins -- and its rows are
 * listed in ONE place: `name_layers.h`. This file asks that order for a value name once, in
 * `valueNameByRow`. The type names are the chain that still carries its own copy of the rows
 * (`lookupType`, `builder_type.cpp`); it is joined to this table in the step after this one.
 *
 * Within a row, functions of one name form a set of overloads; across rows nothing is merged,
 * except the operators (`__add__` and the like), whose overloads come from everywhere.
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
    if (const std::string &self = module_->name(); !self.empty() && own.starts_with(self + "::")) {
        own = own.substr(self.size() + 2);
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
                if (!have.entity ||
                    EntityAccess::isNode(*have.entity) != EntityAccess::isNode(*match.entity)) {
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
    auto merged    = std::make_shared<EntityAccess::graph_set_t>();
    bool mergeable = true;
    for (const UsingMatch &match : matches) {
        std::shared_ptr<EntityAccess::graph_set_t> graphs;
        if (!match.ownName.empty()) {
            if (auto own = unit->graphScope->get(match.ownName, false);
                own.has_value() && !unit->decoratedScope->has(match.ownName, false)) {
                graphs =
                    std::make_shared<EntityAccess::graph_set_t>((*own)->begin(), (*own)->end());
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

std::optional<camel::core::module::entity> Builder::preludeEntity(const std::string &name) {
    if (module_ == nullptr || exports_ == nullptr || context_ == nullptr) {
        return std::nullopt;
    }
    const module_ptr_t prelude = context_->preludeModule();
    if (prelude == nullptr || prelude.get() == module_.get()) {
        // Inside the prelude: its names are its own, layer 2, and a call there is resolved by the
        // module's own declarations alone.
        return std::nullopt;
    }
    auto found = exports_->exportedEntity(*prelude, Reference(name));
    if (!found.has_value() || found->isTag()) {
        return std::nullopt;
    }
    return found;
}

auto Builder::valueNameByRow(
    const std::string &written, const std::string &preludeName, const GCT::node_ptr_t &at)
    -> RowAnswer<ValueName> {
    // The rows of `name_layers.h`, in that table's order, each read from the import bindings of
    // this module and the `using` frames in force. Which row wins is the table's answer: a row
    // that has the name stops the walk, and no layer is named in this function's order.
    return firstRowHolding<ValueName>(
        rowsFrom(NameRow::ImportedByName),
        [&](NameRow row) -> RowAnswer<ValueName> {
            switch (row) {
            case NameRow::Local:
            case NameRow::Module:
                // The scopes and the module's own names: answered before a name reaches its
                // module's imports (visitDRefNode reads them from the graph, template and decorated
                // scopes).
                return RowAnswer<ValueName>::absent();
            case NameRow::ImportedByName: {
                // A name brought by name is the name the program means, whatever it is: the
                // reference written here with an import of it in force has that import's entity and
                // no other row's (`import { range } from tensor` is tensor's `range`; the built-in
                // of the same spelling is the prelude, the last row). A name the imported module
                // does not export leaves the name to the rows below, and is reported only when none
                // has it.
                if (module_->hasImportedRef(written, ImportLayer::Named)) {
                    if (auto named = importedEntity(written, ImportLayer::Named);
                        named.has_value()) {
                        return RowAnswer<ValueName>::has(ValueName{std::move(named), {}});
                    }
                }
                return RowAnswer<ValueName>::absent();
            }
            case NameRow::UsingNamespace: {
                // A `using namespace` applies to the block it is written in; two that bring things
                // that cannot be told apart are AmbiguousName here (throughUsing reports it).
                auto match = throughUsing(written, at);
                if (!match.has_value()) {
                    return RowAnswer<ValueName>::absent();
                }
                if (!match->ownName.empty()) {
                    // A name of this module, opened by a `using namespace` of one of its
                    // namespaces: the answer is the module's own name, read again from its head.
                    return RowAnswer<ValueName>::has(ValueName{std::nullopt, match->ownName});
                }
                if (!match->entity.has_value()) {
                    return RowAnswer<ValueName>::nothing();
                }
                return RowAnswer<ValueName>::has(ValueName{match->entity, {}});
            }
            case NameRow::EveryImport: {
                // An operator takes its overloads from every layer (design principles 10.2; the
                // graph model, N3), and a name a prefix of an imported module or namespace reaches
                // (`m::f`) is read from every binding of it at once as well: one answer of the
                // import bindings. A name no import brings and no spelling makes an operator is not
                // this row's.
                if (!isOperatorName(written) && !module_->hasImportedRef(written)) {
                    return RowAnswer<ValueName>::absent();
                }
                if (auto merged = importedEntity(written); merged.has_value()) {
                    return RowAnswer<ValueName>::has(ValueName{std::move(merged), {}});
                }
                // Written as an operator's name, or imported, and nothing to give: this row has the
                // name, and an import of a name without an entity is what the caller reports.
                return RowAnswer<ValueName>::nothing();
            }
            case NameRow::AmbientImport: {
                // What the imports bring unasked, with no name written for them.
                if (!module_->hasImportedRef(written, ImportLayer::Ambient)) {
                    return RowAnswer<ValueName>::absent();
                }
                if (auto ambient = importedEntity(written, ImportLayer::Ambient);
                    ambient.has_value()) {
                    return RowAnswer<ValueName>::has(ValueName{std::move(ambient), {}});
                }
                return RowAnswer<ValueName>::nothing();
            }
            case NameRow::Prelude: {
                // The prelude is what the built-in module brings, and what it exports comes along
                // unasked: a name of it needs no import. The reading is the export table's own
                // (`ProgramExports::exportedEntity`: what this program declared for the module,
                // merged with the module's own entity), so the prelude's declarations reach a name
                // without a second mechanism beside that one. A spelling a row above already
                // answered -- the built-in module's operator group for `map`, say -- is that row's
                // answer and is not read here.
                if (auto fromPrelude = preludeEntity(preludeName); fromPrelude.has_value()) {
                    return RowAnswer<ValueName>::has(ValueName{std::move(fromPrelude), {}});
                }
                return RowAnswer<ValueName>::absent();
            }
            }
            return RowAnswer<ValueName>::absent();
        });
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
