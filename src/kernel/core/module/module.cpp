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
///
/// A name is one overload set (design principles 6.4), so this gathers every form the name was
/// declared in -- the functions of each graph set, the templates of each generic function, the
/// decorated function if one of them has one, and the rules of each operator group -- and answers
/// the one form when there is one and one entity carrying them all when there is more
/// (Entity::overload_set_payload_t). The kind of the first entity used to decide, so a name a
/// module declared both as a body-less operator and with a body arrived as one of the two and the
/// other was dropped without a word.
std::optional<entity> mergeImportedEntities(const std::vector<entity> &entities) {
    if (entities.empty()) {
        return std::nullopt;
    }
    using detail::EntityAccess;
    oper_group_ptr_t group;
    std::string groupName;
    std::vector<std::pair<std::string, resolver_ptr_t>> rules;
    EntityAccess::graph_set_ptr_t graphs;
    EntityAccess::graph_handle_t decorated;
    EntityAccess::generic_template_list_t tmpls;
    // A name of another kind altogether -- a top-level value's node or a tag: one of those, and
    // the first one, as before.
    std::optional<entity> other;
    for (const auto &ent : entities) {
        if (ent.isOperGroup() && ent.operGroup() != nullptr) {
            if (group == nullptr) {
                group = ent.operGroup();
            }
            const auto &resolvers = ent.operGroup()->resolvers();
            if (groupName.empty() && !resolvers.empty()) {
                groupName = ent.operGroup()->name();
            }
            for (const auto &r : resolvers) {
                // One module's group reaches here twice when a name is imported both by name and
                // with the module's default names: the same overload is one overload.
                const bool known = std::ranges::any_of(rules, [&](const auto &have) {
                    return have.first == r.first && have.second == r.second;
                });
                if (!known) {
                    rules.push_back(r);
                }
            }
        }
        bool functionForm = false;
        if (EntityAccess::isGraphSet(ent)) {
            functionForm    = true;
            const auto &set = EntityAccess::graphSet(ent);
            if (set) {
                if (graphs == nullptr) {
                    graphs = std::make_shared<EntityAccess::graph_set_t>();
                }
                for (const auto &g : *set) {
                    // One module's function reaches here twice when a name is imported both by
                    // name and with the module's default names: it is one function. Counted
                    // twice, the name had "2 functions" and no function value (`xs->map<twice>`
                    // with an imported `twice`).
                    if (std::ranges::find(*graphs, g) == graphs->end()) {
                        graphs->push_back(g);
                    }
                }
            }
        }
        if (EntityAccess::isGenericTemplate(ent)) {
            functionForm = true;
            for (const auto &tmpl : EntityAccess::genericTemplates(ent)) {
                if (std::ranges::find(tmpls, tmpl) == tmpls.end()) {
                    tmpls.push_back(tmpl);
                }
            }
        }
        if (EntityAccess::isDecoratedGraph(ent)) {
            functionForm = true;
            if (decorated == nullptr) {
                decorated = EntityAccess::decoratedGraph(ent);
            }
        }
        if (!ent.isOperGroup() && !functionForm && !other.has_value()) {
            other = ent;
        }
    }
    const bool hasGroup   = group != nullptr && !groupName.empty();
    const int formsOfName = (hasGroup ? 1 : 0) + (graphs != nullptr ? 1 : 0) +
                            (decorated != nullptr ? 1 : 0) + (!tmpls.empty() ? 1 : 0);
    if (formsOfName == 0) {
        return other;
    }
    std::shared_ptr<void> tmplList;
    if (!tmpls.empty()) {
        tmplList = std::make_shared<EntityAccess::generic_template_list_t>(std::move(tmpls));
    }
    if (formsOfName > 1) {
        // The name's whole set travels as one entity.
        return EntityAccess::makeOverloadSet(group, graphs, decorated, tmplList);
    }
    if (hasGroup) {
        return OperatorGroup::merged(groupName, std::move(rules));
    }
    if (graphs != nullptr) {
        return EntityAccess::makeGraphSet(graphs);
    }
    if (decorated != nullptr) {
        return EntityAccess::makeDecoratedGraph(decorated);
    }
    return EntityAccess::makeGenericTemplates(
        *std::static_pointer_cast<EntityAccess::generic_template_list_t>(tmplList));
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
    : loaded_(false), name_(name), path_(path),
      // The context owns this module (Context::builtinModules_, loadedModules_), so the context a
      // module refers back to must not be an owning reference: a module that owned its context
      // would hold it through the very member the context owns, and a context its own members hold
      // reaches neither its destructor nor anything its destructor does (Context::handle()). The
      // conversion belongs in this constructor, which every module goes through, and not at the
      // places that build one: they hand on a `context_ptr_t` from `Context::create` or from
      // another module, and a site added later cannot forget a rule this constructor keeps. A
      // module of another language's `.cmo` gets it from here too, because BuiltinModule, which
      // such a module derives from, calls this constructor inside libcamel.
      context_(ctx ? ctx->handle() : context_ptr_t{}),
      exportedTypeNS_(std::make_shared<Namespace<std::string, Type *>>()),
      nativeEntityNS_(std::make_shared<Namespace<std::string, entity>>()),
      privateEntityNS_(std::make_shared<Namespace<std::string, entity>>()), defaultImportedRefs_(),
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

std::vector<std::shared_ptr<Module>> Module::namedImportSourcesOf(const Reference &ref) const {
    std::vector<std::shared_ptr<Module>> sources;
    for (const ImportedRefBinding &binding : importedBindingsForRef(ref, ImportLayer::Named)) {
        if (binding.mod != nullptr && std::ranges::find(sources, binding.mod) == sources.end()) {
            sources.push_back(binding.mod);
        }
    }
    return sources;
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
            // The name is exported, so a program may write it: a body-less declaration is a walk
            // to the implementation, not a statement that the name is private (see
            // `CallForm::exported`). `declarePrivateEntity` does not say this, which is what tells
            // the printer the two apart.
            table.markExported(uri);
        }
        // The same operator under a type-qualified name (`Tensor.shape` beside `shape`) is one
        // signature: a plain name this module writes takes its aliases with it.
        if (ref.plain()) {
            replaceTypeQualifiedAliases(ref.ident(), ent);
        }
    }
    return inserted;
}

bool Module::declarePrivateEntity(const Reference &ref, const entity &ent) {
    const bool inserted = privateEntityNS_->insert(ref, ent);
    // The URI the group answers for is how the compiler names it where it emits the operator
    // itself (the kernel of an `#impl` function), exactly as for an exported group.
    if (ent.isOperGroup() && ent.operGroup() != nullptr) {
        auto &table = camel::core::OperatorTable::instance();
        for (const auto &[uri, resolver] : ent.operGroup()->resolvers()) {
            nativeOperatorGroups_[table.intern(uri)] = ent.operGroup();
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
        const bool same =
            std::any_of(resolvers.begin(), resolvers.end(), [&](const auto &resolver) {
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
    other.exportedTypeNS_->forEach(
        [&](const Reference &ref, Type *type) { exportedTypeNS_->insert(ref, type); });
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
    const size_t sep = qualifiedName.rfind("::");
    const std::string ident =
        sep == std::string::npos ? qualifiedName : qualifiedName.substr(sep + 2);
    addDeclaredTag(ident, *id);
    // Like an operator group, a tag is exported under its name: an import binds it by name,
    // and a module prefix resolves to it through the module it names.
    const bool exported =
        exportEntity(Reference(ident), detail::EntityAccess::makeTag(static_cast<uint32_t>(*id)));
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
    // The main module's names are the program's own: they are written unqualified.
    if (context_ && context_->mainModule().get() == this) {
        return ref.toString();
    }
    return name_ + "::" + ref.toString();
}

std::optional<std::pair<std::shared_ptr<Module>, Reference>>
Module::importedSource(const Reference &ref, ImportLayer layer) const {
    const auto bindings = importedBindingsForRef(ref, layer);
    if (bindings.empty()) {
        return std::nullopt;
    }
    return std::make_pair(bindings.front().mod, bindings.front().remoteRef);
}

//  ---- ProgramExports (task) -------------------------------------------------------------
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
    // What this program declared for the module and what the module declares itself are one
    // name's overload set when both are there: a body-less declaration's operator group is the
    // module's own entity, and the functions of the same name are this program's (its builder
    // made their graphs). A name declared both ways answered with one of the two -- the program's
    // table first, the module's own only when that table had nothing -- so an importing module
    // saw half of it.
    const std::optional<entity> native = module.getNativeEntity(ref);
    if (auto it = exports_.find(&module); it != exports_.end()) {
        if (auto found = it->second.entities->get(ref); found.has_value()) {
            // Only the forms that are overloads of one name are one set. A constant, a tag or a
            // value node under the name is not an overload of anything: those keep the order this
            // table has always had, the program's entity first and the module's own as the
            // fallback.
            const auto isOverloadForm = [](const entity &ent) {
                return ent.isOperGroup() ||
                       camel::core::module::detail::EntityAccess::isGraphSet(ent) ||
                       camel::core::module::detail::EntityAccess::isGenericTemplate(ent) ||
                       camel::core::module::detail::EntityAccess::isDecoratedGraph(ent);
            };
            if (!native.has_value() || !isOverloadForm(*found) || !isOverloadForm(*native)) {
                return found;
            }
            return mergeImportedEntities({*found, *native});
        }
    }
    // A C++ module's own names (its operator groups, constants and tags) are not program
    // entities: it declares them once when it is loaded.
    return native;
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
        // clears this cache (`exportEntity`), which would leave that reference dangling.
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
        // A function of the name: a plain one, a generic one, a decorated one, or any of those
        // together with the name's operator group (a name declared both ways is one entity
        // carrying its whole set, so the group being there does not make it no function).
        const bool isFunction = detail::EntityAccess::isGraphSet(ent) ||
                                detail::EntityAccess::isGenericTemplate(ent) ||
                                detail::EntityAccess::isDecoratedGraph(ent);
        if (isOverloadOf(ref) && isFunction) {
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

entity_ns_ptr_t Module::privateEntityNS() const { return privateEntityNS_; }

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

void Module::addDeclaredTag(const std::string &ident, uint32_t tag) { declaredTags_[ident] = tag; }

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

sem::PermissionMask
Module::declarationPermissions(const Reference &name, std::string_view signature) const {
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
    slot                      = static_cast<sem::PermissionMask>(slot | permissions);
}

camel::core::attr::AttrSetId Module::fieldTags(const Reference &typeName, size_t index) const {
    const auto it = fieldTags_.find({typeName.toString(), index});
    return it == fieldTags_.end() ? camel::core::attr::kNoAttrs : it->second;
}

void Module::setFieldTags(
    const Reference &typeName, size_t index, camel::core::attr::AttrSetId tags) {
    const auto key  = std::make_pair(typeName.toString(), index);
    const auto it   = fieldTags_.find(key);
    fieldTags_[key] = it == fieldTags_.end() ? tags : camel::core::attr::unite(it->second, tags);
}

} // namespace camel::core::module
