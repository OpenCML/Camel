// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/source/ids.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <unordered_map>
#include <variant>
#include <vector>

#include "camel/common/ns.h"
#include "camel/core/context/context.h"
#include "camel/core/data/base.h"
#include "camel/core/node_attrs.h"
#include "camel/core/operator.h"
#include "camel/core/operator_semantics.h"

namespace camel::compile::gir {
class DraftGraphBuilder;
}

namespace camel::core::module {

namespace detail {
class EntityAccess;
}

class ProgramExports;

using Type = camel::core::type::Type;
class Entity {
  public:
    Entity() = default;
    Entity(oper_group_ptr_t ops) : storage_(std::move(ops)) {}
    /// A constant value a module exports under a name (`math::pi`): written without
    /// parentheses, and a constant of the graph where it is used (design principles 10.1).
    /// Its type is the type of the data.
    explicit Entity(camel::core::data::data_ptr_t value) : storage_(std::move(value)) {}
    bool isOperGroup() const { return std::holds_alternative<oper_group_ptr_t>(storage_); }
    const oper_group_ptr_t &operGroup() const { return std::get<oper_group_ptr_t>(storage_); }
    bool isValue() const {
        return std::holds_alternative<camel::core::data::data_ptr_t>(storage_);
    }
    const camel::core::data::data_ptr_t &value() const {
        return std::get<camel::core::data::data_ptr_t>(storage_);
    }
    /// A tag declared by a module (`tag kernel on nodes`, design principles 6.2): its id in
    /// camel/core/tags.h. A tag shares the namespace of functions and types, so it is
    /// imported and qualified like them.
    bool isTag() const { return std::holds_alternative<tag_payload_t>(storage_); }
    uint32_t tagId() const { return std::get<tag_payload_t>(storage_); }

  private:
    friend class detail::EntityAccess;

    /// A tag id: camel::core::tag::TagId, kept as a plain number so that this header does not
    /// depend on camel/core/tags.h (which depends on this one).
    using tag_payload_t       = uint32_t;
    using node_payload_t      = void *;
    using graph_payload_t     = std::shared_ptr<camel::compile::gir::DraftGraphBuilder>;
    using graph_set_payload_t = std::vector<graph_payload_t>;
    using graph_set_ptr_t     = std::shared_ptr<graph_set_payload_t>;
    /// The generic functions exported by a module written in Camel under one name: the list of the
    /// templates their instances are built from, opaque outside the GIR builder. A name declared
    /// once has a list of one template, a name with several generic functions (`func id::<T>` and
    /// `func id::<T, U>` are overloads, design principles 4.4) has all of them in declaration
    /// order, and the importing module builds the instances of the one a call selects
    /// (DESIGN-QUESTION(generic-export)).
    struct generic_payload_t {
        std::shared_ptr<void> tmpls;
    };
    /// The name of a decorated function that shares its name with other functions of the module:
    /// what the decorators made of the declaration, and the functions of the name it stands
    /// beside. An importer receives both -- a name stands for the decorated function and its
    /// overloads at once, and a call of it chooses among them (`docs/design-principles.cn.md`
    /// 4.4) -- so the two travel together.
    struct decorated_overloads_payload_t {
        graph_payload_t decorated;
        graph_set_ptr_t overloads;
    };
    using storage_t = std::variant<
        node_payload_t, graph_set_ptr_t, oper_group_ptr_t, graph_payload_t, generic_payload_t,
        camel::core::data::data_ptr_t, tag_payload_t, decorated_overloads_payload_t>;

    explicit Entity(node_payload_t node) : storage_(node) {}
    explicit Entity(tag_payload_t tag) : storage_(tag) {}
    explicit Entity(graph_set_ptr_t graphs) : storage_(std::move(graphs)) {}
    explicit Entity(graph_payload_t graph) : storage_(std::move(graph)) {}
    explicit Entity(generic_payload_t generic) : storage_(std::move(generic)) {}
    explicit Entity(decorated_overloads_payload_t overloads) : storage_(std::move(overloads)) {}

    storage_t storage_;
};
using entity          = Entity;
using entity_ns_ptr_t = std::shared_ptr<Namespace<std::string, entity>>;
using type_ns_ptr_t   = std::shared_ptr<Namespace<std::string, Type *>>;

using context_ptr_t = camel::core::context::context_ptr_t;

/// Which imports a lookup of an imported name looks at (design principles 10.2: one rule of
/// precedence, local > own module > imported by name > `using namespace` > built in, the first
/// layer that has the name). `Named`: the names an `import` of this module brings, by name or
/// through a module or namespace prefix (`m::f`); `Ambient`: what comes along without being
/// asked for (the built-in module, the operator groups a module of operators brings); `All`:
/// both, merged, as operators are (their overloads come from every layer).
enum class ImportLayer { All, Named, Ambient };

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
        /// The name in that module (with the namespaces the import takes it from).
        Reference remote;
        camel::source::origin_id_t origin;
        /// The tokens of the import, for an import without an origin.
        size_t tokenStart;
        size_t tokenEnd;
    };

    /// A resource this module declares (design effects plan 3.2): `resource NAME` (an ambient
    /// resource of its own), `resource NAME unordered` (the accesses of a call may be
    /// reordered) or `resource NAME = [a, b]` (a group naming resources and other groups). A
    /// group's `members` are names as written; expand them once all modules are read (the GCT
    /// builder's resourceSignatureOf). Design principles 3.3: a resource is state with an
    /// identity, and a name no value carries (`fs`, `rng`) is an ambient resource of that name.
    struct ResourceDecl {
        bool unordered = false;
        bool exported  = false;
        /// A `var` of the top level of the module is a resource of it, named by the variable
        /// (design principles 3.5): it is always shown with its module.
        bool variable = false;
        /// The members of a group, as written; empty for a single resource.
        std::vector<Reference> members;
        bool group() const { return !members.empty(); }
    };

  protected:
    /// The export tables of one program read a module's import bindings directly: which name
    /// comes from which module is a fact of the module (the source), not of the program.
    friend class ProgramExports;

    struct ImportedRefBinding {
        std::shared_ptr<Module> mod;
        Reference remoteRef;
        // Brought along by the built-in module or as a module's default names, not asked for.
        bool ambient = false;
    };

    struct ImportedNamespaceBinding {
        std::vector<std::string> localPrefixParts;
        std::vector<std::string> remotePrefixParts;
        std::shared_ptr<Module> mod;
    };

    bool loaded_;
    std::string name_;
    std::string declaredName_;
    std::string path_;
    context_ptr_t context_;
    /// The types this module declares and exports. A type declaration is the module's own: it
    /// is built once, with the module's construction tree, and means the same in every program
    /// (design principles 10.1, types). Entities are NOT kept here: an exported function or
    /// top-level value is an artifact of one program, and lives in that program's
    /// ProgramExports (see below) — the same module may be part of several programs at once.
    type_ns_ptr_t exportedTypeNS_;
    /// The entities the module itself brings (a C++ module's operators, constants and tags),
    /// filled when the module is loaded and never rewritten. A module written in Camel
    /// exports its entities into ProgramExports; this table stays empty for it.
    entity_ns_ptr_t nativeEntityNS_;
    /// The operator groups the module's own body-less declarations declare without exporting:
    /// the kernels an `#impl` function runs (design principles 10.4). The module's own bodies
    /// call them and an importer cannot, so they are not among the module's names; they are kept
    /// here so that `inspect --ops` lists them (marked private) and the reference checker
    /// verifies their signature and their purity like any other declaration's.
    entity_ns_ptr_t privateEntityNS_;
    /// The same entities, by the operator URI they export: an operator group of several
    /// overloads answers for each of its URIs. Filled by exportEntity, read by the compiler
    /// for the operators it emits itself (nativeOperatorGroup).
    std::unordered_map<OperId, oper_group_ptr_t> nativeOperatorGroups_;
    std::vector<Reference> defaultImportedRefs_;
    /// A ref may come from multiple modules (same-name function/operator overloads);
    /// group imported bindings by the local ref visible in this module.
    std::unordered_map<Reference, std::vector<ImportedRefBinding>> importedRefMap_;
    /// Namespace imports map a local prefix to an exported prefix in another module.
    std::vector<ImportedNamespaceBinding> importedNamespaceBindings_;
    /// The imports by name, in the order they were written (see noteNamedImport).
    std::vector<std::pair<Reference, NamedImport>> namedImports_;
    /// Every import of a module, in the order they were written (see noteImport).
    std::vector<ImportSite> importSites_;
    /// Where each type this module declares was declared: the reference it was declared under in
    /// this module (see visitTypeDecl) to the origin of the declaration's name. The compiler
    /// fills it as it declares the types; the language server reads it to answer where a type
    /// name's declaration is, including across modules (task).
    std::unordered_map<std::string, camel::source::origin_id_t> typeDeclarationOrigins_;

    // Tags (design principles 6.2): the declarations this module makes, and the facts written
    // on its declarations and on the fields of its type declarations.
    std::map<std::string, uint32_t> declaredTags_;
    // ---- Resources (design effects plan 3.2) -----------------------------------------------
    std::map<std::string, ResourceDecl> resources_;
    // The declaration's name, then the signature of one overload (empty when the name cannot be
    // overloaded), to the facts or the granted numeric permissions.
    std::map<std::string, std::map<std::string, camel::core::attr::AttrSetId>> declarationTags_;
    std::map<std::string, std::map<std::string, sem::PermissionMask>> declarationPermissions_;
    std::map<std::pair<std::string, size_t>, camel::core::attr::AttrSetId> fieldTags_;

    std::vector<ImportedRefBinding>
    importedBindingsForRef(const Reference &ref, ImportLayer layer = ImportLayer::All) const;

  public:
    Module(const std::string &name, const std::string &path, context_ptr_t ctx);
    virtual ~Module() = default;

    const std::string &name() const { return name_; }
    const std::string &path() const { return path_; }
    /// The name the module gives itself (`module shapes`), empty when it has no declaration.
    /// The module may qualify its own names with it, as with name() (design principles 10.2).
    const std::string &declaredName() const { return declaredName_; }
    void setDeclaredName(std::string name) { declaredName_ = std::move(name); }

    /// The module a name imported into this one comes from, and the name it has there: the
    /// first binding of `ref` in `layer` (the one getImportedType reads for a type). nullopt
    /// when the name is not imported.
    std::optional<std::pair<std::shared_ptr<Module>, Reference>>
    importedSource(const Reference &ref, ImportLayer layer = ImportLayer::All) const;

    /// The full name `module::path::name` of the name `ref` of this module, as diagnostics show
    /// it where names of several modules are listed (design principles 10.2). The module
    /// is named by the name it declares, or else the name it was loaded under; the entry
    /// module without a `module` declaration has no name to show, and its names are shown
    /// without one, as the graph names show them.
    std::string qualifiedName(const Reference &ref) const;

    virtual bool load() = 0;
    virtual bool loaded() const { return loaded_; }
    /// The URI protocols the operators of this module answer on, that is the part before the
    /// colon of their URIs: its own name, unless the module registers its operators under another
    /// name (`random` answers on `rand:`; design principles 6.4; rule 6). A
    /// declaration may name an operator of this module with a `#op` tag whose prefix is one of
    /// these, or empty for an operator the compiler itself names (`:op/...`); naming another
    /// module's operator is refused (NativeDeclarationForeignOperator).
    virtual std::vector<std::string> operatorProtocols() const { return {name_}; }
    /// Makes the types the module exports readable (getExportedType). A module written in
    /// Camel declares them while its construction tree is built, so that modules importing
    /// each other can use each other's types (design principles 10.1); a module of another
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
    /// Makes the module's own declarations readable (getNativeEntity, nativeEntityNS): for a
    /// module written in Camel, once its declarations are known, before any function body of
    /// the program is built. The entities a program declares for the module are that program's
    /// (ProgramExports), and are already there by the same point.
    virtual void declareEntities() {
        if (!loaded()) {
            load();
        }
    }

    void markImportedRefFromMod(const Reference &ref, const std::shared_ptr<Module> &mod);
    void markImportedRefFromMod(
        const Reference &localRef, const Reference &remoteRef, const std::shared_ptr<Module> &mod,
        bool ambient = false);
    void
    markImportedNamespaceFromMod(const Reference &localPrefix, const std::shared_ptr<Module> &mod);
    void markImportedNamespaceFromMod(
        const Reference &localPrefix, const Reference &remotePrefix,
        const std::shared_ptr<Module> &mod);
    void importDefaultRefsFromMod(const std::shared_ptr<Module> &mod);
    void importAllRefsFromMod(const std::shared_ptr<Module> &mod);
    bool hasImportedRef(const Reference &ref, ImportLayer layer = ImportLayer::All) const;

    /// Records that `ref` was imported by name from `mod` by the import at `origin`.
    void noteNamedImport(
        const Reference &ref, const Reference &remote, const std::shared_ptr<Module> &mod,
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
    /// Declares an entity the module itself brings (a C++ module's operator group, constant or
    /// tag; design principles 10.1). A module written in Camel does not call this: what it
    /// exports is the program's, and is recorded by that program's ProgramExports.
    bool exportEntity(const Reference &ref, const entity &ent);
    /// The same, for a name that already has an entity: the later write wins. A `.cml` beside a
    /// `.cmo` is the same module as that C++ module (design principles 6.4,
    /// rule 1), so a declaration of an operator the C++ module exported too writes the
    /// module's signature, not a second entity beside it.
    bool replaceEntity(const Reference &ref, const entity &ent);
    /// Declares an operator group the module declares without exporting it (the kernel of an
    /// `#impl` function; design principles 10.4). A private operator is not one of the module's
    /// names: an importer cannot reach it and it is not in the module's entity namespace. It is
    /// an operator all the same, so it is listed by `inspect --ops` marked private and checked
    /// against its declaration by the reference checker.
    bool declarePrivateEntity(const Reference &ref, const entity &ent);
    /// The same, for the aliases a module publishes under a type namespace: a module may export
    /// one operator under its own name and under a type's (`shape` and `Tensor.shape` in
    /// `modules/tensor`). A declaration can write only the plain function, so when a plain name
    /// becomes the module's signature for `ident`, every qualified entity whose identifier is
    /// `ident` and whose group answers for one of the same URIs resolves to that same entity: the
    /// alias is the declaration under another name, not a second entity beside it (design
    /// principles 6.4). Returns how many aliases were replaced.
    size_t replaceTypeQualifiedAliases(const std::string &ident, const entity &ent);
    /// Adds every name `other` exports to this module's own tables: its types, its entities
    /// (operator groups, constants, tags) and the names it brings along. The two objects are one
    /// module (design principles 6.4): the C++ module implements, the
    /// `.cml` declares, and both are read under the one name the import found.
    void inheritExportsFrom(const Module &other);
    /// Exports the constant `value` under `ref` (a C++ module's `math::pi`; design principles
    /// 10.1). A use of the name is that constant in the graph where it is used: no node of the
    /// root graph, no capture, nothing evaluated when the module is imported. Only data that
    /// can be a constant of a graph is accepted (scalars, strings, and composites of them).
    bool exportValue(const Reference &ref, camel::core::data::data_ptr_t value) {
        return exportEntity(ref, entity(std::move(value)));
    }
    /// Exports the tag `qualifiedName` (already declared with camel/core/tags.h) under its own
    /// name, so that a program imports it like a function or a constant (design principles
    /// 6.2, 10.1). A C++ module declares the tag and calls this; false when no tag of that
    /// name was declared.
    bool exportTag(const std::string &qualifiedName);

    /// Return true if this module imports the given module.
    bool imports(const std::shared_ptr<Module> &mod) const;

    std::optional<Type *>
    getImportedType(const Reference &ref, ImportLayer layer = ImportLayer::All) const;

    /// Records where the declaration of the type named `ref` in this module is (its name's
    /// origin); the compiler calls it while it declares the module's types.
    void noteTypeDeclarationOrigin(const Reference &ref, camel::source::origin_id_t origin) {
        typeDeclarationOrigins_[ref.toString()] = origin;
    }
    /// The origin of the declaration of the type named `ref` in this module, or 0 when the
    /// module does not declare it (the type is imported, built in or a parameter).
    camel::source::origin_id_t typeDeclarationOrigin(const Reference &ref) const {
        const auto it = typeDeclarationOrigins_.find(ref.toString());
        return it == typeDeclarationOrigins_.end() ? 0 : it->second;
    }
    /// Whether some import binds a module or namespace prefix that `prefix` names, or begins
    /// with (`using namespace lib::Tools` after `import lib`).
    bool hasImportedPrefix(const Reference &prefix) const;

    /// One overload set of an operator that this module imported: the module that exports it
    /// (null for the operator groups of the built-in module and of the modules of operators,
    /// merged into one) and the entity (that group, the functions of a module written in
    /// Camel, or one of its generic functions).
    struct ImportedOperatorOverloads {
        std::shared_ptr<Module> mod;
        Reference remoteRef;
        entity overloads;
    };

    /// The type `ref` names among the types this module exports, or nullopt.
    std::optional<Type *> getExportedType(const Reference &ref) const;
    /// The entity the module itself declares under `ref` (a C++ module's operator group or
    /// constant). A name a program exported for the module is not here: ask the program's
    /// ProgramExports (exportedEntity), which falls back to this table.
    std::optional<entity> getNativeEntity(const Reference &ref) const;
    /// The operator group this module exports for `oper`: the URI of one of the group's
    /// overloads, as interned when the module was loaded (exportEntity). This is how code
    /// refers to an operator the compiler emits itself (`oper::EnumMake` and the like) without
    /// naming its entity `__enum_make__` again on every compilation. nullptr when this module
    /// exports no operator with that URI.
    oper_group_ptr_t nativeOperatorGroup(OperId oper) const;

    const std::vector<Reference> &defaultImportedRefs() const;
    /// For tools that list what a module can name (camel/service/semantic): the local names
    /// it imported, and the namespaces it imported (`a::b`) with the module behind each.
    std::vector<Reference> importedRefs() const;
    std::vector<std::pair<std::string, std::shared_ptr<Module>>> importedNamespaces() const;
    type_ns_ptr_t exportedTypeNS() const;
    entity_ns_ptr_t nativeEntityNS() const;
    /// The operator groups this module declares without exporting (declarePrivateEntity): the
    /// kernels of its `#impl` functions. They are operators, so a tool that lists what a module
    /// registers (`inspect --ops`) reads them here, marked private; a compiler resolving a name
    /// the module exports does not.
    entity_ns_ptr_t privateEntityNS() const;

    // ---- Resources (design effects plan 3.2) -----------------------------------------------

    /// Declares `name` (namespace-qualified as written) as a resource. A second declaration of
    /// the same name replaces nothing: the caller reports it.
    void declareResource(const Reference &name, ResourceDecl decl);
    void setResourceExported(const Reference &name, bool exported = true);
    /// The resource this module declares under `name`; nullptr when it declares none.
    const ResourceDecl *resource(const Reference &name) const;
    /// Every resource the module declares, by its qualified name.
    const std::map<std::string, ResourceDecl> &resources() const { return resources_; }

    // ---- Tags (design principles 6.2; camel/core/tags.h) -----------------------------------

    /// The tags declared in this module under `ident` (locally), for resolving `#ident` while
    /// the module is compiled.
    std::optional<uint32_t> declaredTag(const std::string &ident) const;
    void addDeclaredTag(const std::string &ident, uint32_t tag);

    /// The facts written on the declaration `name` of this module (`on func`, `on type`), over
    /// every overload of that name.
    camel::core::attr::AttrSetId declarationTags(const Reference &name) const;
    /// The facts written on one overload: `signature` is the declaration's function type as
    /// written; empty for a declaration that cannot be overloaded. An operator's overloads have
    /// one entry each.
    camel::core::attr::AttrSetId
    declarationTags(const Reference &name, std::string_view signature) const;
    /// Attaches `tags` to the declaration (the name alone: one entry for every overload).
    void setDeclarationTags(const Reference &name, camel::core::attr::AttrSetId tags);
    /// Attaches `tags`, the inner facts winning as each key's combine rule says.
    void setDeclarationTags(
        const Reference &name, std::string_view signature, camel::core::attr::AttrSetId tags);
    /// The numeric permissions the declaration grants (its body's), over every overload.
    sem::PermissionMask declarationPermissions(const Reference &name) const;
    sem::PermissionMask declarationPermissions(const Reference &name, std::string_view signature) const;
    void setDeclarationPermissions(
        const Reference &name, std::string_view signature, sem::PermissionMask permissions);
    /// The facts written on field `index` of the type this module declares as `typeName`.
    camel::core::attr::AttrSetId fieldTags(const Reference &typeName, size_t index) const;
    void setFieldTags(
        const Reference &typeName, size_t index, camel::core::attr::AttrSetId tags);

    /// Every tag this module declares, by the name it writes (`N::t`) and its id; the facts
    /// written on its declarations and on the fields of its type declarations. For a dump of
    /// what the program declared (`camel inspect --tags`). A declaration appears once per
    /// overload; the inner key is its signature, empty when it cannot be overloaded.
    const std::map<std::string, uint32_t> &declaredTags() const { return declaredTags_; }
    const std::map<std::string, std::map<std::string, camel::core::attr::AttrSetId>> &
    declarationTagSets() const {
        return declarationTags_;
    }
    const std::map<std::pair<std::string, size_t>, camel::core::attr::AttrSetId> &
    fieldTagSets() const {
        return fieldTags_;
    }
};

/// The export tables of one program: which entity each module it declares exports in this
/// program. An exported function, generic template, top-level value or tag is an artifact of
/// one program (its builder made the graphs), so it belongs here, not to the Module object —
/// the same module may take part in several programs at once (design principles 10.1; a pass
/// written in Camel is compiled as a program of its own over the modules the program being
/// transformed loaded, task). Types are not here: a type declaration is the module's own
/// and is the same in every program (Module::exportedTypeNS).
///
/// One table per program also means one imported-name cache per program: what a name imported
/// into a module resolves to is the program's graphs.
class ProgramExports {
  public:
    ProgramExports()                                  = default;
    ProgramExports(const ProgramExports &)            = delete;
    ProgramExports &operator=(const ProgramExports &) = delete;

    /// Records that `module` exports `ref` as `ent` in this program. False when the name is
    /// already taken (a name is one entity per module).
    bool exportEntity(const Module &module, const Reference &ref, const entity &ent);

    /// The entity `module` exports under `ref` in this program; when the program declared none
    /// of that name, the module's own (native) entity of that name.
    std::optional<entity> exportedEntity(const Module &module, const Reference &ref) const;

    /// Calls `visit` for every name `module` brings to this program: the ones this program
    /// declared, then the module's own (native) names that no program name hides.
    void forEachExportedEntity(
        const Module &module,
        const std::function<void(const Reference &, const entity &)> &visit) const;

    /// The entity a name imported into `importer` stands for in this program: its import
    /// bindings (named, ambient or all, design principles 10.2), each module's export read from
    /// this table, then merged (same-name functions and operator overloads are one set). Cached
    /// per importing module, name and layer.
    std::optional<entity> importedEntity(
        const Module &importer, const Reference &ref, ImportLayer layer = ImportLayer::All);

    /// Every overload of the operator function `group` (`__add__`) `importer` imported (design
    /// principles 10.2: the overloads of an operator from every layer are one set): first the
    /// operator groups, merged; then what each module written in Camel that `importer` imports,
    /// in any form, exports under `group` or `group#k` (a generic overload), ordered by module
    /// name, then by name.
    std::vector<Module::ImportedOperatorOverloads>
    importedOperatorOverloads(const Module &importer, const std::string &group);

    /// Every overload of the operator function `group` (`__add__`) `module` itself exports in
    /// this program, in the order importedOperatorOverloads uses: what the module exports under
    /// `group` or `group#k` (a generic overload), by name. Design principles 5.7: the module that
    /// declares a type offers its operators with it, so a use of the type elsewhere reads this
    /// set whether or not it imported the module.
    std::vector<Module::ImportedOperatorOverloads>
    exportedOperatorOverloads(Module &module, const std::string &group);

    /// Forgets the merged imports of `importer`: its bindings changed after a name was read (an
    /// import made while the module is being built), and the next read merges them again.
    void forgetMergedImports(const Module &importer) { imported_.erase(&importer); }

  private:
    struct ModuleExports {
        entity_ns_ptr_t entities = std::make_shared<Namespace<std::string, entity>>();
    };
    struct ImportedEntities {
        std::unordered_map<Reference, entity> merged[3];
    };
    std::unordered_map<const Module *, ModuleExports> exports_;
    std::unordered_map<const Module *, ImportedEntities> imported_;
};

using module_ptr_t = std::shared_ptr<Module>;

} // namespace camel::core::module

