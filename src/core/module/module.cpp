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
 * Updated: May. 05, 2026
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
                        merged->push_back(g);
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
                        allResolvers.push_back(r);
                    }
                }
            }
        }
        if (name.empty()) {
            return std::nullopt;
        }
        return OperatorGroup::create(name, std::move(allResolvers));
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
    if (!mod->loaded()) {
        mod->load();
    }
    for (const auto &ref : mod->defaultImportedRefs()) {
        markImportedRefFromMod(ref, mod);
    }
}

void Module::importAllRefsFromMod(const module_ptr_t &mod) {
    if (!mod->loaded()) {
        mod->load();
    }
    auto appendModForRef = [this, &mod](const Reference &ref) { markImportedRefFromMod(ref, mod); };
    auto typeNS          = mod->exportedTypeNS();
    typeNS->forEach([&](const Reference &ref, Type *) { appendModForRef(ref); });
    auto entNS = mod->exportedEntityNS();
    entNS->forEach([&](const Reference &ref, const entity &) { appendModForRef(ref); });
    importedEntityCache_.clear();
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
    const Reference &localRef, const Reference &remoteRef, const module_ptr_t &mod) {
    auto &vec = importedRefMap_[localRef];
    auto it   = std::find_if(vec.begin(), vec.end(), [&](const ImportedRefBinding &binding) {
        return binding.mod == mod && binding.remoteRef == remoteRef;
    });
    if (it == vec.end()) {
        vec.push_back(ImportedRefBinding{mod, remoteRef});
    }
    importedEntityCache_.erase(localRef);
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
    importedEntityCache_.clear();
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
    importedEntityCache_.clear();
}

std::vector<Module::ImportedRefBinding> Module::importedBindingsForRef(const Reference &ref) const {
    std::vector<ImportedRefBinding> result;
    auto exact = importedRefMap_.find(ref);
    if (exact != importedRefMap_.end()) {
        result.insert(result.end(), exact->second.begin(), exact->second.end());
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

        if (!binding.mod->loaded()) {
            binding.mod->load();
        }
        binding.mod->exportedTypeNS()->forEach(
            [&](const Reference &remoteRef, Type *) { importRef(remoteRef); });
        binding.mod->exportedEntityNS()->forEach(
            [&](const Reference &remoteRef, const entity &) { importRef(remoteRef); });
    }
    return importedAny;
}

bool Module::hasImportedRef(const Reference &ref) const {
    return !importedBindingsForRef(ref).empty();
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

std::optional<Type *> Module::getImportedType(const Reference &ref) const {
    auto bindings = importedBindingsForRef(ref);
    if (bindings.empty()) {
        return std::nullopt;
    }
    // Types are not overloaded; the first module providing this ref wins.
    for (auto &binding : bindings) {
        auto &mod = binding.mod;
        if (!mod->loaded()) {
            mod->load();
        }
        auto type = mod->getExportedType(binding.remoteRef);
        if (type.has_value()) {
            return type;
        }
    }
    return std::nullopt;
}

std::optional<entity> Module::getImportedEntity(const Reference &ref) const {
    auto bindings = importedBindingsForRef(ref);
    if (bindings.empty()) {
        return std::nullopt;
    }
    // Return the cached result directly to avoid repeated merges.
    auto cacheIt = importedEntityCache_.find(ref);
    if (cacheIt != importedEntityCache_.end()) {
        return cacheIt->second;
    }
    std::vector<entity> collected;
    for (const auto &binding : bindings) {
        const auto &mod = binding.mod;
        if (!mod->loaded()) {
            mod->load();
        }
        auto opt = mod->getExportedEntity(binding.remoteRef);
        if (opt) {
            collected.push_back(std::move(*opt));
        }
    }
    auto merged = mergeImportedEntities(collected);
    if (merged) {
        importedEntityCache_[ref] = *merged;
    }
    return merged;
}

std::optional<Type *> Module::getExportedType(const Reference &ref) const {
    ASSERT(loaded_, "Module not built: " + name_);
    return exportedTypeNS_->get(ref);
};

std::optional<entity> Module::getExportedEntity(const Reference &ref) const {
    ASSERT(loaded_, "Module not built: " + name_);
    return exportedEntityNS_->get(ref);
};

const std::vector<Reference> &Module::defaultImportedRefs() const { return defaultImportedRefs_; }

type_ns_ptr_t Module::exportedTypeNS() const {
    ASSERT(loaded_, "Module not built: " + name_);
    return exportedTypeNS_;
}

entity_ns_ptr_t Module::exportedEntityNS() const {
    ASSERT(loaded_, "Module not built: " + name_);
    return exportedEntityNS_;
}

} // namespace camel::core::module
