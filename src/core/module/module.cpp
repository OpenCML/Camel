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
 * Created: Jul. 29, 2025
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/module/module.h"
#include "camel/core/operator.h"
#include "camel/core/type/resolver.h"
#include "core/module/entity_internal.h"

#include <algorithm>

namespace camel::core::module {

using resolver_ptr_t = camel::core::type::resolver_ptr_t;

namespace {

/// Merge identically named entities exported by multiple modules so function
/// and operator overloading work across module boundaries.
/// - graph_vec_ptr_t: merge all graphs into one vector
/// - oper_group_ptr_t: merge all operator resolvers by name
/// - node_ptr_t: keep the first one only
std::optional<entity> mergeImportedEntities(const std::vector<entity> &entities) {
    if (entities.empty()) {
        return std::nullopt;
    }
    const entity &first = entities.front();
    if (detail::EntityAccess::isGraphSet(first)) {
        auto merged = std::make_shared<detail::EntityAccess::graph_set_t>();
        for (const auto &ent : entities) {
            if (detail::EntityAccess::isGraphSet(ent)) {
                const auto &graphs = detail::EntityAccess::graphSet(ent);
                if (graphs) {
                    for (const auto &g : *graphs) {
                        // One module's function reaches here twice when a name is imported
                        // both by name and with the module's default names: it is one
                        // function. Counted twice, the name had "2 functions" and no
                        // function value (`xs->map<twice>` with an imported `twice`).
                        if (std::ranges::find(*merged, g) == merged->end()) {
                            merged->push_back(g);
                        }
                    }
                }
            }
        }
        return detail::EntityAccess::makeGraphSet(std::move(merged));
    }
    if (first.isOperGroup()) {
        std::string name;
        std::vector<std::pair<std::string, resolver_ptr_t>> allResolvers;
        for (const auto &ent : entities) {
            if (ent.isOperGroup()) {
                const auto &group = ent.operGroup();
                if (group) {
                    if (name.empty()) {
                        name = group->name();
                    }
                    for (const auto &r : group->resolvers()) {
                        // One module's group reaches here twice when a name is imported
                        // both by name and with the module's default names: the same
                        // overload is one overload.
                        const bool known = std::ranges::any_of(allResolvers, [&](const auto &have) {
                            return have.first == r.first && have.second == r.second;
                        });
                        if (!known) {
                            allResolvers.push_back(r);
                        }
                    }
                }
            }
        }
        if (name.empty()) {
            return std::nullopt;
        }
        return OperatorGroup::merged(name, std::move(allResolvers));
    }
    if (detail::EntityAccess::isDecoratedGraph(first)) {
        for (const auto &ent : entities) {
            if (detail::EntityAccess::isDecoratedGraph(ent) &&
                detail::EntityAccess::decoratedGraph(ent)) {
                return detail::EntityAccess::makeDecoratedGraph(
                    detail::EntityAccess::decoratedGraph(ent));
            }
        }
        return std::nullopt;
    }
    // node_ptr_t or anything else: return the first one.
    return first;
}

std::vector<std::string> refParts(const Reference &ref) {
    std::vector<std::string> parts = ref.paths();
    parts.push_back(ref.ident());
    return parts;
}

bool startsWith(const std::vector<std::string> &parts, const std::vector<std::string> &prefix) {
    if (prefix.size() > parts.size()) {
        return false;
    }
    return std::equal(prefix.begin(), prefix.end(), parts.begin());
}

std::optional<Reference> refFromParts(const std::vector<std::string> &parts) {
    if (parts.empty()) {
        return std::nullopt;
    }
    return Reference(parts);
}

} // namespace

Module::Module(const std::string &name, const std::string &path, context_ptr_t ctx)
    : loaded_(false), name_(name), path_(path), context_(ctx),
      exportedTypeNS_(std::make_shared<Namespace<std::string, Type *>>()),
      exportedEntityNS_(std::make_shared<Namespace<std::string, entity>>()), defaultImportedRefs_(),
      importedRefMap_(), importedNamespaceBindings_(), importedEntityCache_() {}

void Module::importDefaultRefsFromMod(const module_ptr_t &mod) {
    // Default names are what a module of operators brings along; a module written in Camel
    // has none, and is not compiled here (it may be importing this module).
    mod->declareDefaultRefs();
    for (const auto &ref : mod->defaultImportedRefs()) {
        markImportedRefFromMod(ref, ref, mod, true);
    }
}

void Module::clearImportedEntityCache() {
    for (auto &cache : importedEntityCache_) {
        cache.clear();
    }
}

void Module::importAllRefsFromMod(const module_ptr_t &mod) {
    if (!mod->loaded()) {
        mod->load();
    }
    // Everything of the module, brought along: the built-in module (the last layer of names).
    auto appendModForRef = [this, &mod](const Reference &ref) {
        markImportedRefFromMod(ref, ref, mod, true);
    };
    auto typeNS = mod->exportedTypeNS();
    typeNS->forEach([&](const Reference &ref, Type *) { appendModForRef(ref); });
    auto entNS = mod->exportedEntityNS();
    entNS->forEach([&](const Reference &ref, const entity &) { appendModForRef(ref); });
    clearImportedEntityCache();
}

bool Module::imports(const module_ptr_t &mod) const {
    for (const auto &[ref, bindings] : importedRefMap_) {
        auto it =
            std::find_if(bindings.begin(), bindings.end(), [&](const ImportedRefBinding &binding) {
                return binding.mod == mod;
            });
        if (it != bindings.end()) {
            return true;
        }
    }
    auto nsIt = std::find_if(
        importedNamespaceBindings_.begin(),
        importedNamespaceBindings_.end(),
        [&](const ImportedNamespaceBinding &binding) { return binding.mod == mod; });
    if (nsIt != importedNamespaceBindings_.end()) {
        return true;
    }
    return false;
}

void Module::markImportedRefFromMod(const Reference &ref, const module_ptr_t &mod) {
    markImportedRefFromMod(ref, ref, mod);
}

void Module::markImportedRefFromMod(
    const Reference &localRef, const Reference &remoteRef, const module_ptr_t &mod, bool ambient) {
    auto &vec = importedRefMap_[localRef];
    auto it   = std::find_if(vec.begin(), vec.end(), [&](const ImportedRefBinding &binding) {
        return binding.mod == mod && binding.remoteRef == remoteRef;
    });
    if (it == vec.end()) {
        vec.push_back(ImportedRefBinding{mod, remoteRef, ambient});
    } else {
        // Asked for by name as well: it is a named import.
        it->ambient = it->ambient && ambient;
    }
    for (auto &cache : importedEntityCache_) {
        cache.erase(localRef);
    }
}

void Module::markImportedNamespaceFromMod(const Reference &localPrefix, const module_ptr_t &mod) {
    ImportedNamespaceBinding binding{
        .localPrefixParts  = refParts(localPrefix),
        .remotePrefixParts = {},
        .mod               = mod,
    };
    auto it = std::find_if(
        importedNamespaceBindings_.begin(),
        importedNamespaceBindings_.end(),
        [&](const ImportedNamespaceBinding &other) {
            return other.mod == binding.mod && other.localPrefixParts == binding.localPrefixParts &&
                   other.remotePrefixParts == binding.remotePrefixParts;
        });
    if (it == importedNamespaceBindings_.end()) {
        importedNamespaceBindings_.push_back(std::move(binding));
    }
    clearImportedEntityCache();
}

void Module::markImportedNamespaceFromMod(
    const Reference &localPrefix, const Reference &remotePrefix, const module_ptr_t &mod) {
    ImportedNamespaceBinding binding{
        .localPrefixParts  = refParts(localPrefix),
        .remotePrefixParts = refParts(remotePrefix),
        .mod               = mod,
    };
    auto it = std::find_if(
        importedNamespaceBindings_.begin(),
        importedNamespaceBindings_.end(),
        [&](const ImportedNamespaceBinding &other) {
            return other.mod == binding.mod && other.localPrefixParts == binding.localPrefixParts &&
                   other.remotePrefixParts == binding.remotePrefixParts;
        });
    if (it == importedNamespaceBindings_.end()) {
        importedNamespaceBindings_.push_back(std::move(binding));
    }
    clearImportedEntityCache();
}

std::vector<Module::ImportedRefBinding>
Module::importedBindingsForRef(const Reference &ref, ImportLayer layer) const {
    std::vector<ImportedRefBinding> result;
    auto exact = importedRefMap_.find(ref);
    if (exact != importedRefMap_.end()) {
        for (const ImportedRefBinding &binding : exact->second) {
            if (layer == ImportLayer::All || binding.ambient == (layer == ImportLayer::Ambient)) {
                result.push_back(binding);
            }
        }
    }
    if (layer == ImportLayer::Ambient) {
        return result; // a prefix is always written in an import
    }

    const auto parts = refParts(ref);
    for (const auto &binding : importedNamespaceBindings_) {
        if (!startsWith(parts, binding.localPrefixParts)) {
            continue;
        }
        std::vector<std::string> remoteParts = binding.remotePrefixParts;
        remoteParts.insert(
            remoteParts.end(),
            parts.begin() + static_cast<std::ptrdiff_t>(binding.localPrefixParts.size()),
            parts.end());
        auto remoteRef = refFromParts(remoteParts);
        if (!remoteRef.has_value()) {
            continue;
        }
        result.push_back(ImportedRefBinding{binding.mod, *remoteRef});
    }
    return result;
}

bool Module::importAllRefsFromImportedNamespace(const Reference &localPrefix) {
    bool importedAny      = false;
    const auto localParts = refParts(localPrefix);

    for (const auto &binding : importedNamespaceBindings_) {
        if (!startsWith(localParts, binding.localPrefixParts)) {
            continue;
        }

        std::vector<std::string> remotePrefix = binding.remotePrefixParts;
        remotePrefix.insert(
            remotePrefix.end(),
            localParts.begin() + static_cast<std::ptrdiff_t>(binding.localPrefixParts.size()),
            localParts.end());

        auto importRef = [&](const Reference &remoteRef) {
            const auto remoteParts = refParts(remoteRef);
            if (!startsWith(remoteParts, remotePrefix)) {
                return;
            }
            std::vector<std::string> localRefParts(
                remoteParts.begin() + static_cast<std::ptrdiff_t>(remotePrefix.size()),
                remoteParts.end());
            auto localRef = refFromParts(localRefParts);
            if (!localRef.has_value()) {
                return;
            }
            markImportedRefFromMod(*localRef, remoteRef, binding.mod);
            importedAny = true;
        };

        binding.mod->declareEntities();
        binding.mod->exportedTypeNS()->forEach(
            [&](const Reference &remoteRef, Type *) { importRef(remoteRef); });
        binding.mod->exportedEntityNS()->forEach(
            [&](const Reference &remoteRef, const entity &) { importRef(remoteRef); });
    }
    return importedAny;
}

void Module::noteImport(
    const module_ptr_t &mod, camel::source::origin_id_t origin, size_t tokenStart,
    size_t tokenEnd) {
    importSites_.push_back(ImportSite{mod, origin, tokenStart, tokenEnd});
}

void Module::noteNamedImport(
    const Reference &ref, const Reference &remote, const module_ptr_t &mod,
    camel::source::origin_id_t origin, size_t tokenStart, size_t tokenEnd) {
    namedImports_.emplace_back(ref, NamedImport{mod, remote, origin, tokenStart, tokenEnd});
}

std::vector<std::pair<Reference, std::vector<Module::NamedImport>>>
Module::namesImportedMoreThanOnce() const {
    std::vector<std::pair<Reference, std::vector<NamedImport>>> result;
    for (const auto &[ref, site] : namedImports_) {
        auto it = std::find_if(result.begin(), result.end(), [&](const auto &entry) {
            return entry.first == ref;
        });
        if (it == result.end()) {
            result.emplace_back(ref, std::vector<NamedImport>{site});
        } else if (std::none_of(it->second.begin(), it->second.end(), [&](const auto &have) {
                       return have.mod == site.mod;
                   })) {
            it->second.push_back(site);
        }
    }
    std::erase_if(result, [](const auto &entry) { return entry.second.size() < 2; });
    return result;
}

bool Module::hasImportedRef(const Reference &ref, ImportLayer layer) const {
    return !importedBindingsForRef(ref, layer).empty();
}

bool Module::hasImportedPrefix(const Reference &prefix) const {
    const auto parts = refParts(prefix);
    for (const auto &binding : importedNamespaceBindings_) {
        if (startsWith(parts, binding.localPrefixParts) ||
            startsWith(binding.localPrefixParts, parts)) {
            return true;
        }
    }
    return false;
}

bool Module::exportDefaultImportRef(const Reference &ref) {
    if (std::find(defaultImportedRefs_.begin(), defaultImportedRefs_.end(), ref) !=
        defaultImportedRefs_.end()) {
        return false;
    }
    defaultImportedRefs_.push_back(ref);
    return true;
}

bool Module::exportType(const Reference &ref, Type *type) {
    return exportedTypeNS_->insert(ref, type);
}

bool Module::exportEntity(const Reference &ref, const entity &ent) {
    return exportedEntityNS_->insert(ref, ent);
}

void Module::clearExportedNamespaces() {
    exportedTypeNS_   = std::make_shared<Namespace<std::string, Type *>>();
    exportedEntityNS_ = std::make_shared<Namespace<std::string, entity>>();
}

std::optional<Type *> Module::getImportedType(const Reference &ref, ImportLayer layer) const {
    auto bindings = importedBindingsForRef(ref, layer);
    if (bindings.empty()) {
        return std::nullopt;
    }
    // Types are not overloaded; the first module providing this ref wins.
    for (auto &binding : bindings) {
        auto &mod = binding.mod;
        mod->declareTypes();
        auto type = mod->getExportedType(binding.remoteRef);
        if (type.has_value()) {
            return type;
        }
    }
    return std::nullopt;
}

std::string Module::qualifiedName(const Reference &ref) const {
    if (declaredName_.empty() && context_ && context_->mainModule().get() == this) {
        return ref.toString();
    }
    return (declaredName_.empty() ? name_ : declaredName_) + "::" + ref.toString();
}

std::optional<std::pair<std::shared_ptr<Module>, Reference>>
Module::importedSource(const Reference &ref, ImportLayer layer) const {
    const auto bindings = importedBindingsForRef(ref, layer);
    if (bindings.empty()) {
        return std::nullopt;
    }
    return std::make_pair(bindings.front().mod, bindings.front().remoteRef);
}

std::optional<entity> Module::getImportedEntity(const Reference &ref, ImportLayer layer) const {
    auto bindings = importedBindingsForRef(ref, layer);
    if (bindings.empty()) {
        return std::nullopt;
    }
    // Return the cached result directly to avoid repeated merges.
    auto &cache  = importedEntityCache_[static_cast<size_t>(layer)];
    auto cacheIt = cache.find(ref);
    if (cacheIt != cache.end()) {
        return cacheIt->second;
    }
    std::vector<entity> collected;
    for (const auto &binding : bindings) {
        const auto &mod = binding.mod;
        mod->declareEntities();
        auto opt = mod->getExportedEntity(binding.remoteRef);
        if (opt) {
            collected.push_back(std::move(*opt));
        }
    }
    auto merged = mergeImportedEntities(collected);
    if (merged) {
        cache[ref] = *merged;
    }
    return merged;
}

std::vector<Module::ImportedOperatorOverloads>
Module::importedOperatorOverloads(const std::string &group) const {
    std::vector<ImportedOperatorOverloads> result;
    // The operator groups of the built-in module and of the modules of operators: brought
    // along by every import (the ambient layer), merged into one group.
    if (auto ops = getImportedEntity(Reference(group)); ops.has_value() && ops->isOperGroup()) {
        result.push_back({nullptr, Reference(group), std::move(*ops)});
    }
    // The overloads a module written in Camel exports (`export func operator +`): every
    // import of the module brings them, whatever it names (design principles 10.2), so they
    // are looked up in the modules imported, not among the names imported. A generic overload
    // is exported under `group#k` (the GIR builder's exportOperatorOverloads).
    std::vector<std::shared_ptr<Module>> modules;
    for (const ImportSite &site : importSites_) {
        if (site.mod && std::ranges::find(modules, site.mod) == modules.end()) {
            modules.push_back(site.mod);
        }
    }
    std::ranges::sort(modules, [](const auto &a, const auto &b) { return a->name() < b->name(); });
    for (const auto &mod : modules) {
        for (auto &overload : mod->exportedOperatorOverloads(group)) {
            result.push_back(std::move(overload));
        }
    }
    return result;
}

std::vector<Module::ImportedOperatorOverloads>
Module::exportedOperatorOverloads(const std::string &group) {
    // The declarations of the module are complete before any function body of the program is
    // built (design principles 10.1), so this is a no-op by the time an operator is resolved.
    declareEntities();
    std::vector<ImportedOperatorOverloads> result;
    const auto isOverloadOf = [&](const Reference &ref) {
        const std::string &ident = ref.ident();
        return ref.plain() &&
               (ident == group || (ident.size() > group.size() && ident.starts_with(group) &&
                                   ident[group.size()] == '#'));
    };
    std::vector<std::pair<Reference, entity>> found;
    exportedEntityNS()->forEach([&](const Reference &ref, const entity &ent) {
        if (isOverloadOf(ref) && !ent.isOperGroup()) {
            found.emplace_back(ref, ent);
        }
    });
    std::ranges::sort(found, [](const auto &a, const auto &b) {
        return a.first.toString() < b.first.toString();
    });
    const auto self = shared_from_this();
    for (auto &[ref, ent] : found) {
        result.push_back({self, ref, std::move(ent)});
    }
    return result;
}

// The export tables of a module written in Camel fill while it is declared (types while its
// construction tree is built, the rest when the program's graph builder declares it); what is
// asked for before that is not there yet.
std::optional<Type *> Module::getExportedType(const Reference &ref) const {
    return exportedTypeNS_->get(ref);
};

std::optional<entity> Module::getExportedEntity(const Reference &ref) const {
    return exportedEntityNS_->get(ref);
};

std::vector<Reference> Module::importedRefs() const {
    std::vector<Reference> refs;
    refs.reserve(importedRefMap_.size());
    for (const auto &[ref, bindings] : importedRefMap_) {
        refs.push_back(ref);
    }
    return refs;
}

std::vector<std::pair<std::string, std::shared_ptr<Module>>> Module::importedNamespaces() const {
    std::vector<std::pair<std::string, std::shared_ptr<Module>>> out;
    for (const auto &binding : importedNamespaceBindings_) {
        std::string prefix;
        for (const auto &part : binding.localPrefixParts) {
            if (!prefix.empty()) {
                prefix += "::";
            }
            prefix += part;
        }
        out.emplace_back(std::move(prefix), binding.mod);
    }
    return out;
}

const std::vector<Reference> &Module::defaultImportedRefs() const { return defaultImportedRefs_; }

type_ns_ptr_t Module::exportedTypeNS() const { return exportedTypeNS_; }

entity_ns_ptr_t Module::exportedEntityNS() const { return exportedEntityNS_; }

// ---- Tags (design principles 6.2; camel/core/tags.h) -------------------------------------

std::optional<uint32_t> Module::declaredTag(const std::string &ident) const {
    const auto it = declaredTags_.find(ident);
    if (it == declaredTags_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void Module::addDeclaredTag(const std::string &ident, uint32_t tag) {
    declaredTags_[ident] = tag;
}

camel::core::attr::AttrSetId Module::declarationTags(const Reference &name) const {
    const auto it = declarationTags_.find(name.toString());
    return it == declarationTags_.end() ? camel::core::attr::kNoAttrs : it->second;
}

void Module::setDeclarationTags(
    const Reference &name, camel::core::attr::AttrSetId tags) {
    const std::string key = name.toString();
    const auto it         = declarationTags_.find(key);
    declarationTags_[key] = it == declarationTags_.end()
                                ? tags
                                : camel::core::attr::unite(it->second, tags);
}

camel::core::attr::AttrSetId Module::fieldTags(const Reference &typeName, size_t index) const {
    const auto it = fieldTags_.find({typeName.toString(), index});
    return it == fieldTags_.end() ? camel::core::attr::kNoAttrs : it->second;
}

void Module::setFieldTags(
    const Reference &typeName, size_t index, camel::core::attr::AttrSetId tags) {
    const auto key = std::make_pair(typeName.toString(), index);
    const auto it  = fieldTags_.find(key);
    fieldTags_[key] =
        it == fieldTags_.end() ? tags : camel::core::attr::unite(it->second, tags);
}

} // namespace camel::core::module
