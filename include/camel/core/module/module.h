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

#pragma once

#include "camel/core/source/ids.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "camel/common/ns.h"
#include "camel/core/context/context.h"
#include "camel/core/operator.h"

namespace camel::compile::gir {
class DraftGraphBuilder;
}

namespace camel::core::module {

namespace detail {
class EntityAccess;
}

using Type = camel::core::type::Type;
class Entity {
  public:
    Entity() = default;
    Entity(oper_group_ptr_t ops) : storage_(std::move(ops)) {}
    bool isOperGroup() const { return std::holds_alternative<oper_group_ptr_t>(storage_); }
    const oper_group_ptr_t &operGroup() const { return std::get<oper_group_ptr_t>(storage_); }

  private:
    friend class detail::EntityAccess;

    using node_payload_t      = void *;
    using graph_payload_t     = std::shared_ptr<camel::compile::gir::DraftGraphBuilder>;
    using graph_set_payload_t = std::vector<graph_payload_t>;
    using graph_set_ptr_t     = std::shared_ptr<graph_set_payload_t>;
    /// A generic function exported by a module written in Camel: the template its instances
    /// are built from, opaque outside the GIR builder. The importing module builds the
    /// instances (DESIGN-QUESTION(generic-export)).
    struct generic_payload_t {
        std::shared_ptr<void> tmpl;
    };
    using storage_t = std::variant<
        node_payload_t, graph_set_ptr_t, oper_group_ptr_t, graph_payload_t, generic_payload_t>;

    explicit Entity(node_payload_t node) : storage_(node) {}
    explicit Entity(graph_set_ptr_t graphs) : storage_(std::move(graphs)) {}
    explicit Entity(graph_payload_t graph) : storage_(std::move(graph)) {}
    explicit Entity(generic_payload_t generic) : storage_(std::move(generic)) {}

    storage_t storage_;
};
using entity          = Entity;
using entity_ns_ptr_t = std::shared_ptr<Namespace<std::string, entity>>;
using type_ns_ptr_t   = std::shared_ptr<Namespace<std::string, Type *>>;

using context_ptr_t = camel::core::context::context_ptr_t;

class Module : public std::enable_shared_from_this<Module> {
  public:
    /// One `import` of a module: the module, and where the import stands (its origin, or its
    /// tokens for an import without an origin).
    struct ImportSite {
        std::shared_ptr<Module> mod;
        camel::source::origin_id_t origin;
        size_t tokenStart;
        size_t tokenEnd;
    };

    /// One `import { name } from mod`: the module, and where the import stands.
    struct NamedImport {
        std::shared_ptr<Module> mod;
        camel::source::origin_id_t origin;
        /// The tokens of the import, for an import without an origin.
        size_t tokenStart;
        size_t tokenEnd;
    };

  protected:
    struct ImportedRefBinding {
        std::shared_ptr<Module> mod;
        Reference remoteRef;
    };

    struct ImportedNamespaceBinding {
        std::vector<std::string> localPrefixParts;
        std::vector<std::string> remotePrefixParts;
        std::shared_ptr<Module> mod;
    };

    bool loaded_;
    std::string name_;
    std::string path_;
    context_ptr_t context_;
    type_ns_ptr_t exportedTypeNS_;
    entity_ns_ptr_t exportedEntityNS_;
    std::vector<Reference> defaultImportedRefs_;
    /// A ref may come from multiple modules (same-name function/operator overloads);
    /// group imported bindings by the local ref visible in this module.
    std::unordered_map<Reference, std::vector<ImportedRefBinding>> importedRefMap_;
    /// Namespace imports map a local prefix to an exported prefix in another module.
    std::vector<ImportedNamespaceBinding> importedNamespaceBindings_;
    /// Cache merged imported entities to avoid re-merging on every query.
    mutable std::unordered_map<Reference, entity> importedEntityCache_;
    /// The imports by name, in the order they were written (see noteNamedImport).
    std::vector<std::pair<Reference, NamedImport>> namedImports_;
    /// Every import of a module, in the order they were written (see noteImport).
    std::vector<ImportSite> importSites_;

    std::vector<ImportedRefBinding> importedBindingsForRef(const Reference &ref) const;

  public:
    Module(const std::string &name, const std::string &path, context_ptr_t ctx);
    virtual ~Module() = default;

    const std::string &name() const { return name_; }
    const std::string &path() const { return path_; }

    virtual bool load() = 0;
    virtual bool loaded() const { return loaded_; }
    /// Makes the types the module exports readable (getExportedType). A module written in
    /// Camel declares them while its construction tree is built, so that modules importing
    /// each other can use each other's types (design principles 2.11); a module of another
    /// kind is loaded.
    virtual void declareTypes() {
        if (!loaded()) {
            load();
        }
    }
    /// Makes the names the module wants imported along with it (defaultImportedRefs: the
    /// operator groups of a module of operators) readable.
    virtual void declareDefaultRefs() {
        if (!loaded()) {
            load();
        }
    }
    /// Makes the entities the module exports readable (getExportedEntity, exportedEntityNS):
    /// for a module written in Camel, once its declarations are known, before any function
    /// body of the program is built.
    virtual void declareEntities() {
        if (!loaded()) {
            load();
        }
    }

    void markImportedRefFromMod(const Reference &ref, const std::shared_ptr<Module> &mod);
    void markImportedRefFromMod(
        const Reference &localRef, const Reference &remoteRef, const std::shared_ptr<Module> &mod);
    void
    markImportedNamespaceFromMod(const Reference &localPrefix, const std::shared_ptr<Module> &mod);
    void markImportedNamespaceFromMod(
        const Reference &localPrefix, const Reference &remotePrefix,
        const std::shared_ptr<Module> &mod);
    bool importAllRefsFromImportedNamespace(const Reference &localPrefix);
    void importDefaultRefsFromMod(const std::shared_ptr<Module> &mod);
    void importAllRefsFromMod(const std::shared_ptr<Module> &mod);
    bool hasImportedRef(const Reference &ref) const;

    /// Records that `ref` was imported by name from `mod` by the import at `origin`.
    void noteNamedImport(
        const Reference &ref, const std::shared_ptr<Module> &mod,
        camel::source::origin_id_t origin, size_t tokenStart, size_t tokenEnd);
    /// Records an import of `mod` (any form) at `origin`.
    void noteImport(
        const std::shared_ptr<Module> &mod, camel::source::origin_id_t origin, size_t tokenStart,
        size_t tokenEnd);
    /// Every import of a module this module has, in the order they were written.
    const std::vector<ImportSite> &importSites() const { return importSites_; }
    /// The names imported by name from more than one module, each with its imports in the
    /// order they were written.
    std::vector<std::pair<Reference, std::vector<NamedImport>>> namesImportedMoreThanOnce() const;

    bool exportDefaultImportRef(const Reference &ref);
    bool exportType(const Reference &ref, Type *type);
    bool exportEntity(const Reference &ref, const entity &ent);

    /// Return true if this module imports the given module.
    bool imports(const std::shared_ptr<Module> &mod) const;

    std::optional<Type *> getImportedType(const Reference &ref) const;
    std::optional<entity> getImportedEntity(const Reference &ref) const;

    std::optional<Type *> getExportedType(const Reference &ref) const;
    std::optional<entity> getExportedEntity(const Reference &ref) const;

    const std::vector<Reference> &defaultImportedRefs() const;
    /// For tools that list what a module can name (camel/service/semantic): the local names
    /// it imported, and the namespaces it imported (`a::b`) with the module behind each.
    std::vector<Reference> importedRefs() const;
    std::vector<std::pair<std::string, std::shared_ptr<Module>>> importedNamespaces() const;
    type_ns_ptr_t exportedTypeNS() const;
    entity_ns_ptr_t exportedEntityNS() const;
};

using module_ptr_t = std::shared_ptr<Module>;

} // namespace camel::core::module
