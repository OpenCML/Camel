// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/module/module.h"
#include "camel/core/operator.h"
#include "camel/core/tags.h"
#include "camel/core/type/resolver.h"
#include "core/module/entity_internal.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

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
      nativeEntityNS_(std::make_shared<Namespace<std::string, entity>>()), defaultImportedRefs_(),
      importedRefMap_(), importedNamespaceBindings_() {}

void Module::importDefaultRefsFromMod(const module_ptr_t &mod) {
    // Default names are what a module of operators brings along; a module written in Camel
    // has none, and is not compiled here (it may be importing this module).
    mod->declareDefaultRefs();
    for (const auto &ref : mod->defaultImportedRefs()) {
        markImportedRefFromMod(ref, ref, mod, true);
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
    auto entNS = mod->nativeEntityNS();
    entNS->forEach([&](const Reference &ref, const entity &) { appendModForRef(ref); });
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
        // The prelude (the module with no name) is layer 5 on its own (`hasImportedRef` with
        // ImportLayer::Ambient). It brings every import along with an empty prefix and an empty
        // remote prefix, so a `using namespace m` would otherwise reach the built-in of a name
        // through layer 4 -- where the language's rule puts the prelude below every layer the
        // program wrote, and a `using namespace` brings the module's own names, not the prelude
        // again (design principles 6.x).
        if (binding.mod != nullptr && binding.mod->name().empty() &&
            binding.remotePrefixParts.empty()) {
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
    const bool inserted = nativeEntityNS_->insert(ref, ent);
    // An operator the compiler emits itself is referred to by the URI it was interned under
    // (oper::EnumMake and the like), not by this entity name: record the way from the id back
    // to the group, once, here at registration.
    if (ent.isOperGroup() && ent.operGroup() != nullptr) {
        auto &table = camel::core::OperatorTable::instance();
        for (const auto &[uri, resolver] : ent.operGroup()->resolvers()) {
            nativeOperatorGroups_[table.intern(uri)] = ent.operGroup();
        }
        // The same operator under a type-qualified name (`Tensor.shape` beside `shape`) is one
        // signature: a plain name this module writes takes its aliases with it.
        if (ref.plain()) {
            replaceTypeQualifiedAliases(ref.ident(), ent);
        }
    }
    return inserted;
}

bool Module::replaceEntity(const Reference &ref, const entity &ent) {
    const bool written = nativeEntityNS_->assign(ref, ent);
    if (ent.isOperGroup() && ent.operGroup() != nullptr) {
        auto &table = camel::core::OperatorTable::instance();
        for (const auto &[uri, resolver] : ent.operGroup()->resolvers()) {
            nativeOperatorGroups_[table.intern(uri)] = ent.operGroup();
        }
        if (ref.plain()) {
            replaceTypeQualifiedAliases(ref.ident(), ent);
        }
    }
    return written;
}

size_t Module::replaceTypeQualifiedAliases(const std::string &ident, const entity &ent) {
    if (!ent.isOperGroup() || ent.operGroup() == nullptr) {
        return 0;
    }
    // A qualified name of an unrelated operator that happens to share the identifier is left
    // alone: the two are one operator only when they answer for one URI.
    const auto &resolvers = ent.operGroup()->resolvers();
    std::vector<Reference> aliases;
    nativeEntityNS_->forEach([&](const Reference &ref, const entity &other) {
        if (ref.plain() || ref.ident() != ident || !other.isOperGroup() ||
            other.operGroup() == nullptr || other.operGroup() == ent.operGroup()) {
            return;
        }
        const auto &aliasResolvers = other.operGroup()->resolvers();
        const bool same             = std::any_of(
            resolvers.begin(),
            resolvers.end(),
            [&](const auto &resolver) {
                return std::any_of(
                    aliasResolvers.begin(),
                    aliasResolvers.end(),
                    [&](const auto &alias) { return alias.first == resolver.first; });
            });
        if (same) {
            aliases.push_back(ref);
        }
    });
    for (const Reference &ref : aliases) {
        nativeEntityNS_->assign(ref, ent);
    }
    return aliases.size();
}

void Module::inheritExportsFrom(const Module &other) {
    other.exportedTypeNS_->forEach([&](const Reference &ref, Type *type) {
        exportedTypeNS_->insert(ref, type);
    });
    other.nativeEntityNS_->forEach(
        [&](const Reference &ref, const entity &ent) { exportEntity(ref, ent); });
    for (const auto &[ident, tag] : other.declaredTags_) {
        addDeclaredTag(ident, tag);
    }
    for (const Reference &ref : other.defaultImportedRefs_) {
        exportDefaultImportRef(ref);
    }
    // A declaration this module holds is the signature of its operator: the implementation may
    // export the same operator under a type-qualified alias too (`Tensor.shape` beside
    // `shape`), and an alias that just arrived with those exports would otherwise be the last
    // write, pointing at the C++ group the declaration replaced. Whichever of the two names is
    // written last, the declaration is the one signature (design principles 6.4).
    std::vector<std::string> plainGroups;
    nativeEntityNS_->forEach([&](const Reference &ref, const entity &ent) {
        if (ref.plain() && ent.isOperGroup() && ent.operGroup() != nullptr) {
            plainGroups.push_back(ref.ident());
        }
    });
    for (const std::string &ident : plainGroups) {
        if (auto ent = nativeEntityNS_->get(Reference(ident)); ent.has_value()) {
            replaceTypeQualifiedAliases(ident, *ent);
        }
    }
}

bool Module::exportTag(const std::string &qualifiedName) {
    const auto id = camel::core::tag::find(qualifiedName);
    if (!id.has_value()) {
        return false;
    }
    const size_t sep         = qualifiedName.rfind("::");
    const std::string ident  = sep == std::string::npos ? qualifiedName
                                                       : qualifiedName.substr(sep + 2);
    addDeclaredTag(ident, *id);
    // Like an operator group, a tag is exported under its name: an import binds it by name,
    // and a module prefix resolves to it through the module it names.
    const bool exported = exportEntity(
        Reference(ident), detail::EntityAccess::makeTag(static_cast<uint32_t>(*id)));
    // A tag is written `#name`, not used as a value, so a program does not import it by name:
    // it comes with the module that declares it, as the module's operator groups do.
    exportDefaultImportRef(Reference(ident));
    return exported;
}

oper_group_ptr_t Module::nativeOperatorGroup(OperId oper) const {
    const auto it = nativeOperatorGroups_.find(oper);
    return it == nativeOperatorGroups_.end() ? nullptr : it->second;
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

// ---- ProgramExports (task) -------------------------------------------------------------
//
// An exported entity of a module written in Camel — a function's graph, a generic template, a
// top-level value's node, a tag — is made by the builder of one program. The module object is
// shared by every program that loads it, so these names live in the program's table; a module
// declares the same names into another program without touching the first.

bool ProgramExports::exportEntity(const Module &module, const Reference &ref, const entity &ent) {
    if (!exports_[&module].entities->insert(ref, ent)) {
        return false;
    }
    // A merged imported name may have been cached before this name existed (a module's exports
    // are written while the program is declared; a decorated form is added after its body is
    // built), so no cached merge survives a write.
    imported_.clear();
    return true;
}

std::optional<entity>
ProgramExports::exportedEntity(const Module &module, const Reference &ref) const {
    if (auto it = exports_.find(&module); it != exports_.end()) {
        if (auto found = it->second.entities->get(ref); found.has_value()) {
            return found;
        }
    }
    // A C++ module's own names (its operator groups, constants and tags) are not program
    // entities: it declares them once when it is loaded.
    return module.getNativeEntity(ref);
}

void ProgramExports::forEachExportedEntity(
    const Module &module,
    const std::function<void(const Reference &, const entity &)> &visit) const {
    std::unordered_set<std::string> declared;
    if (auto it = exports_.find(&module); it != exports_.end()) {
        std::as_const(*it->second.entities).forEach([&](const Reference &ref, const entity &ent) {
            declared.insert(ref.toString());
            visit(ref, ent);
        });
    }
    std::as_const(*module.nativeEntityNS()).forEach([&](const Reference &ref, const entity &ent) {
        if (!declared.contains(ref.toString())) {
            visit(ref, ent);
        }
    });
}

std::optional<entity>
ProgramExports::importedEntity(const Module &importer, const Reference &ref, ImportLayer layer) {
    auto bindings = importer.importedBindingsForRef(ref, layer);
    if (bindings.empty()) {
        return std::nullopt;
    }
    // Return the cached result directly to avoid repeated merges.
    auto &cache  = imported_[&importer].merged[static_cast<size_t>(layer)];
    auto cacheIt = cache.find(ref);
    if (cacheIt != cache.end()) {
        return cacheIt->second;
    }
    std::vector<entity> collected;
    for (const auto &binding : bindings) {
        binding.mod->declareEntities();
        auto found = exportedEntity(*binding.mod, binding.remoteRef);
        if (found) {
            collected.push_back(std::move(*found));
        }
    }
    auto merged = mergeImportedEntities(collected);
    if (merged) {
        // Not the reference taken above: declaring a module writes its exports, and every write
        // clears this cache (`exportEntity`), which would leave that reference dangling. The
        // write used to happen before any lookup was made, so it could not.
        imported_[&importer].merged[static_cast<size_t>(layer)][ref] = *merged;
    }
    return merged;
}

std::vector<Module::ImportedOperatorOverloads>
ProgramExports::importedOperatorOverloads(const Module &importer, const std::string &group) {
    std::vector<Module::ImportedOperatorOverloads> result;
    // The operator groups of the built-in module and of the modules of operators: brought
    // along by every import (the ambient layer), merged into one group.
    if (auto ops = importedEntity(importer, Reference(group));
        ops.has_value() && ops->isOperGroup()) {
        result.push_back({nullptr, Reference(group), std::move(*ops)});
    }
    // The overloads a module written in Camel exports (`export func operator +`): every
    // import of the module brings them, whatever it names (design principles 10.2), so they
    // are looked up in the modules imported, not among the names imported. A generic overload
    // is exported under `group#k` (the GIR builder's exportOperatorOverloads).
    std::vector<std::shared_ptr<Module>> modules;
    for (const Module::ImportSite &site : importer.importSites_) {
        if (site.mod && std::ranges::find(modules, site.mod) == modules.end()) {
            modules.push_back(site.mod);
        }
    }
    std::ranges::sort(modules, [](const auto &a, const auto &b) { return a->name() < b->name(); });
    for (const auto &mod : modules) {
        for (auto &overload : exportedOperatorOverloads(*mod, group)) {
            result.push_back(std::move(overload));
        }
    }
    return result;
}

std::vector<Module::ImportedOperatorOverloads>
ProgramExports::exportedOperatorOverloads(Module &module, const std::string &group) {
    // The declarations of the module are complete before any function body of the program is
    // built (design principles 10.1), so this is a no-op by the time an operator is resolved.
    module.declareEntities();
    std::vector<Module::ImportedOperatorOverloads> result;
    const auto isOverloadOf = [&](const Reference &ref) {
        const std::string &ident = ref.ident();
        return ref.plain() &&
               (ident == group || (ident.size() > group.size() && ident.starts_with(group) &&
                                   ident[group.size()] == '#'));
    };
    std::vector<std::pair<Reference, entity>> found;
    forEachExportedEntity(module, [&](const Reference &ref, const entity &ent) {
        if (isOverloadOf(ref) && !ent.isOperGroup()) {
            found.emplace_back(ref, ent);
        }
    });
    std::ranges::sort(found, [](const auto &a, const auto &b) {
        return a.first.toString() < b.first.toString();
    });
    const auto self = module.shared_from_this();
    for (auto &[ref, ent] : found) {
        result.push_back({self, ref, std::move(ent)});
    }
    return result;
}

// The module's types fill while its construction tree is built (they are the module's own and
// mean the same in every program); the program's entities fill when its graph builder declares
// the module. What is asked for before that is not there yet.
std::optional<Type *> Module::getExportedType(const Reference &ref) const {
    return exportedTypeNS_->get(ref);
};

std::optional<entity> Module::getNativeEntity(const Reference &ref) const {
    return nativeEntityNS_->get(ref);
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

entity_ns_ptr_t Module::nativeEntityNS() const { return nativeEntityNS_; }

// ---- Resources (design effects plan 3.2) --------------------------------------------------

void Module::declareResource(const Reference &name, ResourceDecl decl) {
    resources_[name.toString()] = std::move(decl);
}

void Module::setResourceExported(const Reference &name, bool exported) {
    const auto it = resources_.find(name.toString());
    if (it != resources_.end()) {
        it->second.exported = exported;
    }
}

const Module::ResourceDecl *Module::resource(const Reference &name) const {
    const auto it = resources_.find(name.toString());
    return it == resources_.end() ? nullptr : &it->second;
}

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
    if (it == declarationTags_.end()) {
        return camel::core::attr::kNoAttrs;
    }
    // The name-level query sees every overload of the name (an operator's, normally).
    camel::core::attr::AttrSetId all = camel::core::attr::kNoAttrs;
    for (const auto &[signature, tags] : it->second) {
        all = camel::core::attr::unite(all, tags);
    }
    return all;
}

camel::core::attr::AttrSetId
Module::declarationTags(const Reference &name, std::string_view signature) const {
    const auto outer = declarationTags_.find(name.toString());
    if (outer == declarationTags_.end()) {
        return camel::core::attr::kNoAttrs;
    }
    const auto inner = outer->second.find(std::string(signature));
    return inner == outer->second.end() ? camel::core::attr::kNoAttrs : inner->second;
}

void Module::setDeclarationTags(const Reference &name, camel::core::attr::AttrSetId tags) {
    setDeclarationTags(name, std::string_view{}, tags);
}

void Module::setDeclarationTags(
    const Reference &name, std::string_view signature, camel::core::attr::AttrSetId tags) {
    auto &bySignature = declarationTags_[name.toString()];
    const auto it     = bySignature.find(std::string(signature));
    bySignature[std::string(signature)] =
        it == bySignature.end() ? tags : camel::core::attr::unite(it->second, tags);
}

sem::PermissionMask Module::declarationPermissions(const Reference &name) const {
    const auto it = declarationPermissions_.find(name.toString());
    if (it == declarationPermissions_.end()) {
        return 0;
    }
    sem::PermissionMask all = 0;
    for (const auto &[signature, permissions] : it->second) {
        all = static_cast<sem::PermissionMask>(all | permissions);
    }
    return all;
}

sem::PermissionMask Module::declarationPermissions(const Reference &name, std::string_view signature) const {
    const auto outer = declarationPermissions_.find(name.toString());
    if (outer == declarationPermissions_.end()) {
        return 0;
    }
    const auto inner = outer->second.find(std::string(signature));
    return inner == outer->second.end() ? 0 : inner->second;
}

void Module::setDeclarationPermissions(
    const Reference &name, std::string_view signature, sem::PermissionMask permissions) {
    // The permissions of a declaration are the union of everything written on it; a second
    // record (an instance built after the template) must not lose the first.
    sem::PermissionMask &slot = declarationPermissions_[name.toString()][std::string(signature)];
    slot          = static_cast<sem::PermissionMask>(slot | permissions);
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



