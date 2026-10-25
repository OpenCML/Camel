// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The tag registry and the storage of declaration tags (camel/core/tags.h).
 *
 * The registry interns one Decl per qualified name. Everything the compiler needs about a
 * tag is there before a `#` use is resolved: where it may be written, its parameters, whether
 * it is an alias group, which passes read it, and for `on nodes` the attribute key (and, for
 * a C++ module, the value builder of the decorator form).
 *
 * Declaration tags live on the module that declares the function, type or field. A module is
 * the identity of its declarations (design principles 6.2: "标签属于声明（名字）"), and it is
 * what survives compilation into the runtime program, so a pass reads them with
 * ofDeclaration/ofField without touching the graph layout.
 */

#include "camel/core/tags.h"

#include <algorithm>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <unordered_map>
#include <utility>

#include "camel/core/module/module.h"
#include "camel/core/operator_semantics.h"
#include "camel/core/operator_table.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/graph.h"

namespace camel::core::tag {

namespace {

struct Registry {
    std::shared_mutex mutex;
    // A deque: a Decl keeps its address when more are added, so get() may hand it out while
    // another thread declares.
    std::deque<Decl> decls{Decl{}};
    std::unordered_map<std::string, TagId> ids;
};

Registry &registry() {
    static Registry r;
    return r;
}

std::string qualified(std::string_view module, std::string_view ident) {
    if (module.empty()) {
        return std::string(ident);
    }
    return std::string(module) + "::" + std::string(ident);
}

/// The passes modules and the core registered as readers of tag facts, and, for a Camel pass
/// that says which tags it reads (`#reads<...>`), the passes of each tag.
struct Readers {
    std::shared_mutex mutex;
    std::set<std::string> passes;
    std::map<std::string, std::set<std::string>> ofTag;
};

Readers &readers() {
    static Readers r;
    return r;
}

/// The `read_by` list of each attribute set, computed once: the tags are interned, so the set
/// is a small key and a pass that reads every node's facts does not rebuild the list.
struct ReaderCache {
    std::shared_mutex mutex;
    std::unordered_map<attr::AttrSetId, std::vector<std::string>> bySet;
};

ReaderCache &readerCache() {
    static ReaderCache c;
    return c;
}

} // namespace

void declareStandardTags() {
    static std::once_flag once;
    std::call_once(once, [] {
        const auto add = [](const char *name, sem::PermissionMask permission, Positions on) {
            Decl decl;
            decl.name       = name;
            decl.ident      = name;
            decl.on         = on;
            decl.permission = permission;
            declare(std::move(decl));
        };
        const Positions on = Nodes;
        add("reassociate", sem::kReassociate, on);
        add("contract", sem::kContract, on);
        add("approx_math", sem::kApproxMath, on);
        add("change_precision", sem::kChangePrecision, on);
        add("no_nans", sem::kNoNans, on);
        add("no_infs", sem::kNoInfs, on);
        add("no_signed_zeros", sem::kNoSignedZeros, on);
        add("flush_subnormals", sem::kFlushSubnormals, on);
        add("nondeterministic", sem::kNonDeterministic, on);
        // `refine_accuracy` (rewrite 8.10): a rewrite may replace a form with one whose error is
        // not larger, even though the bits change. Not granted by default; `#strict` revokes it
        // with every other permission.
        add("refine_accuracy", sem::kRefineAccuracy, on);
        // `finite_math` is the group of the four assumptions (design principles 7.2), not a
        // permission of its own.
        Decl finite;
        finite.name    = "finite_math";
        finite.ident   = "finite_math";
        finite.on      = on;
        finite.aliases = {
            Reference("no_nans"),
            Reference("no_infs"),
            Reference("no_signed_zeros"),
            Reference("flush_subnormals")};
        declare(std::move(finite));
        // `strict` (design principles 7.4): the function or block it marks uses no permission at
        // all, the default grants of 7.3 included. It is a tag, not a permission: it grants no
        // bit, and it is the one tag whose meaning is that the effective set is empty (see
        // effectivePermissions).
        Decl strict;
        strict.name  = "strict";
        strict.ident = "strict";
        strict.on    = static_cast<Positions>(Func | Nodes);
        declare(std::move(strict));
        // `reads`, written on a Camel pass function: the tags the pass reads. Its arguments are
        // tag names, not values, and there may be any number of them (`#reads<reassociate,
        // contract>`); `registerCamelPass` records the pass as a reader of each.
        Decl reads;
        reads.name     = "reads";
        reads.ident    = "reads";
        reads.on       = Func;
        reads.names    = true;
        reads.variadic = true;
        declare(std::move(reads));
        // `op`, written before a body-less declaration: the URI of the operator the declaration
        // is, when the operator's name is not the declaration's. A name that follows the
        // convention (`bytes:size` for `size` of `bytes`) needs no tag (design principles 6.4;
        //  step 1).
        Decl op;
        op.name   = "op";
        op.ident  = "op";
        op.on     = Func;
        op.params = {{"uri", "string"}};
        declare(std::move(op));

        // `impl<f>` (design principles 10.4): the Camel body of the function is its semantics,
        // and the body-less declaration `f` of the same module is the operator that runs it.
        // f is a kernel, not a value: a body-less declaration has no function value, so the
        // argument is the name as written, not an expression. A standard tag, so no import.
        // No pass reads the tag: the graph builder resolves it (a call goes to the kernel, a
        // consumer that asks for the body gets the semantic graph).
        Decl impl;
        impl.name         = "impl";
        impl.ident        = "impl";
        impl.on           = Func;
        impl.names        = true;
        impl.namesAreTags = false;
        impl.params       = {{"kernel", "string"}};
        declare(std::move(impl));

        // `macro` (design principles 8): the function may be evaluated ahead of time by the
        // `std::macro` pass, which materialises the result as a constant of the graph. It is a
        // standard tag, so a program writes it without an import. The pass is optional: without
        // it the function is an ordinary function, so the tag names no reader.
        Decl macro;
        macro.name  = "macro";
        macro.ident = "macro";
        macro.on    = Func;
        declare(std::move(macro));

        // `#independent` on a function: its elements do not depend on each other (design
        // principles 3.6), so a scheduler may run its element calls at once. The fact is on
        // the declaration and, through markIndependentElements, on the graph's exit node.
        Decl independent;
        independent.name  = "independent";
        independent.ident = "independent";
        independent.on    = Func;
        declare(std::move(independent));

        // `#resource` on a type declaration (design effects plan 3.1): a value of the type is a
        // handle, one resource per value, named by the declaration. It is a standard tag, so a
        // program writes it without an import, and it is declaration-only: it says nothing about
        // the nodes of a body, so the placement facts of `#gpu`/`#cpu` are untouched. The fact
        // is a mark; the declaration's own flag (StructDecl::handle) is what the type predicate
        // reads, so the two labels cannot be combined into a second meaning.
        const attr::AttrKey resourceKey = attr::registerKey(
            "resource",
            attr::KeySpec{
                attr::ValueKind::Unit,
                attr::rules::inner(),
                attr::rules::equalOnly(),
            });
        Decl resource;
        resource.name  = "resource";
        resource.ident = "resource";
        resource.on    = Type;
        resource.key   = resourceKey;
        declare(std::move(resource));

        // The one placement fact (design principles 13.4, docs/design/gpu/plan.cn.md 2.1): a
        // Symbol, `gpu` or `cpu`. Two labels write it, so a consumer reads one key whichever
        // label the program wrote. The inner value wins where scopes nest; two different
        // values on one node are refused (builder_tags.cpp).
        const attr::AttrKey placement = attr::registerKey(
            "tiles.placement",
            attr::KeySpec{
                attr::ValueKind::Symbol,
                attr::rules::inner(),
                attr::rules::equalOnly(),
            });
        const auto placementValue =
            [](std::string_view text) {
                return [text](std::span<const data::data_ptr_t> args)
                           -> std::optional<attr::AttrValueId> {
                    return args.empty() ? std::optional<attr::AttrValueId>(attr::symbol(text))
                                        : std::nullopt;
                };
            };
        // `#gpu` (design principles 13.4): the function or block it marks is asked to run on
        // the device. It is a standard tag, so the placement label needs no import; the fact
        // is on the declaration and on every node written in the scope. The pass that reads
        // it is `tiles::compile`, which also registers itself as its reader; a pipeline
        // without that pass warns that the label has no reader (9.4).
        Decl gpu;
        gpu.name   = "gpu";
        gpu.ident  = "gpu";
        gpu.on     = static_cast<Positions>(Func | Nodes);
        gpu.readBy = {"tiles::compile"};
        gpu.key    = placement;
        gpu.value  = placementValue("gpu");
        declare(std::move(gpu));

        // `#cpu`: the same placement fact with the other value. It says where the code is to
        // run, not that it must not use a device; no pass decides by it yet. Written as a
        // nested block it overrides an enclosing `#gpu` (the inner value wins).
        Decl cpu;
        cpu.name   = "cpu";
        cpu.ident  = "cpu";
        cpu.on     = static_cast<Positions>(Func | Nodes);
        cpu.readBy = {"tiles::compile"};
        cpu.key    = placement;
        cpu.value  = placementValue("cpu");
        declare(std::move(cpu));
    });
}

TagId declare(Decl decl) {
    if (decl.name.empty()) {
        if (decl.ident.empty()) {
            return kNoTag;
        }
        decl.name = qualified(decl.module, decl.ident);
    }
    if (decl.ident.empty()) {
        const size_t sep = decl.name.rfind("::");
        decl.ident       = sep == std::string::npos ? decl.name : decl.name.substr(sep + 2);
    }
    // Every tag but a group has one attribute key, named as the tag is qualified. On nodes it
    // is where the facts are stored; on a declaration it names the fact in the set the module
    // keeps. A C++ module registers the key with its kind (declareNodeTag) before declaring
    // the tag, so the key it made is the one used.
    if (!decl.isGroup() && decl.key == attr::kNoKey) {
        decl.key = attr::key(decl.name);
    }
    Registry &r = registry();
    std::unique_lock lock(r.mutex);
    if (auto it = r.ids.find(decl.name); it != r.ids.end()) {
        r.decls[it->second] = std::move(decl);
        return it->second;
    }
    const auto id                     = static_cast<TagId>(r.decls.size());
    const std::string name            = decl.name;
    r.decls.push_back(std::move(decl));
    r.ids.emplace(name, id);
    return id;
}

const Decl *get(TagId id) {
    Registry &r = registry();
    std::shared_lock lock(r.mutex);
    if (id == kNoTag || id >= r.decls.size()) {
        return nullptr;
    }
    return &r.decls[id];
}

std::optional<TagId> find(std::string_view name) {
    declareStandardTags();
    Registry &r = registry();
    std::shared_lock lock(r.mutex);
    auto it = r.ids.find(std::string(name));
    if (it == r.ids.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<TagId> find(std::string_view module, std::string_view ident) {
    return find(qualified(module, ident));
}

sem::PermissionMask permissionOf(TagId id) {
    const Decl *decl = get(id);
    return decl == nullptr ? 0 : decl->permission;
}

sem::PermissionMask permissionOfName(std::string_view name) {
    const auto id = find(std::string_view{}, name);
    if (!id.has_value()) {
        return 0;
    }
    const Decl *decl = get(*id);
    if (decl == nullptr) {
        return 0;
    }
    if (decl->permission != 0) {
        return decl->permission;
    }
    // `finite_math` stands for its four assumptions (design principles 7.2): a group grants
    // what its members grant.
    sem::PermissionMask mask = 0;
    for (const Reference &member : decl->aliases) {
        mask |= permissionOfName(member.toString());
    }
    return mask;
}

std::vector<std::string> readersOf(attr::AttrSetId tags) {
    if (tags == attr::kNoAttrs) {
        return {};
    }
    {
        ReaderCache &cache = readerCache();
        std::shared_lock lock(cache.mutex);
        if (const auto it = cache.bySet.find(tags); it != cache.bySet.end()) {
            return it->second;
        }
    }
    std::set<std::string> readers;
    for (const attr::Attr &entry : attr::entries(tags)) {
        const auto id = find(std::string_view{}, attr::keyName(entry.key));
        if (!id.has_value()) {
            continue;
        }
        const Decl *decl = get(*id);
        if (decl == nullptr) {
            continue;
        }
        readers.insert(decl->readBy.begin(), decl->readBy.end());
    }
    std::vector<std::string> out(readers.begin(), readers.end());
    ReaderCache &cache = readerCache();
    std::unique_lock lock(cache.mutex);
    cache.bySet.emplace(tags, out);
    return out;
}

void declareReader(std::string_view pass) {
    Readers &r = readers();
    std::unique_lock lock(r.mutex);
    r.passes.emplace(pass);
}

bool isReader(std::string_view written) {
    Readers &r = readers();
    std::shared_lock lock(r.mutex);
    if (r.passes.contains(std::string(written))) {
        return true;
    }
    // A module name names every pass of the module (`tensor` for `tensor::fuse`).
    const std::string prefix = std::string(written) + "::";
    for (const std::string &pass : r.passes) {
        if (pass.starts_with(prefix)) {
            return true;
        }
    }
    return false;
}

void declareTagReader(std::string_view tag, std::string_view pass) {
    Readers &r = readers();
    std::unique_lock lock(r.mutex);
    r.ofTag[std::string(tag)].emplace(pass);
}

std::vector<std::string> readersOfTag(std::string_view tag) {
    Readers &r = readers();
    std::shared_lock lock(r.mutex);
    const auto it = r.ofTag.find(std::string(tag));
    if (it == r.ofTag.end()) {
        return {};
    }
    return std::vector<std::string>(it->second.begin(), it->second.end());
}

std::string_view positionName(Position position) {
    switch (position) {
    case Func:
        return "func";
    case Type:
        return "type";
    case Field:
        return "field";
    case Nodes:
        return "nodes";
    case Param:
        return "param";
    default:
        return {};
    }
}

std::optional<Position> positionFromName(std::string_view name) {
    if (name == "func") {
        return Func;
    }
    if (name == "type") {
        return Type;
    }
    if (name == "field") {
        return Field;
    }
    if (name == "nodes") {
        return Nodes;
    }
    if (name == "param") {
        return Param;
    }
    return std::nullopt;
}

TagId declareNodeTag(std::string_view qualifiedName, Positions positions, NodeTagSpec spec) {
    // The one call that replaces attr::registerKey and OperatorTable::declareGrant: the key
    // has the tag's name, and the decorator that grants it (when there is one) is declared
    // together with the tag.
    attr::registerKey(
        qualifiedName,
        {.kind = spec.kind, .combine = std::move(spec.combine), .merge = std::move(spec.merge)});
    if (!spec.decorator.empty()) {
        OperatorTable::instance().declareGrant(
            spec.decorator,
            {.key = std::string(qualifiedName), .value = std::move(spec.value)});
    }
    Decl decl;
    decl.name = std::string(qualifiedName);
    const size_t sep = decl.name.rfind("::");
    if (sep != std::string::npos) {
        decl.module = decl.name.substr(0, sep);
        decl.ident  = decl.name.substr(sep + 2);
    } else {
        decl.ident = decl.name;
    }
    decl.on = positions;
    return declare(std::move(decl));
}

attr::AttrSetId ofDeclaration(const module::Module &module, const Reference &name) {
    return module.declarationTags(name);
}

attr::AttrSetId ofDeclaration(
    const module::Module &module, const Reference &name, std::string_view signature) {
    return module.declarationTags(name, signature);
}

attr::AttrSetId ofField(const module::Module &module, const Reference &typeName, size_t index) {
    return module.fieldTags(typeName, index);
}

void attachToDeclaration(module::Module &module, const Reference &name, attr::AttrSetId tags) {
    if (tags == attr::kNoAttrs) {
        return;
    }
    module.setDeclarationTags(name, tags);
}

void attachToDeclaration(
    module::Module &module,
    const Reference &name,
    std::string_view signature,
    attr::AttrSetId tags) {
    if (tags == attr::kNoAttrs) {
        return;
    }
    module.setDeclarationTags(name, signature, tags);
}

void attachPermissionsToDeclaration(
    module::Module &module,
    const Reference &name,
    std::string_view signature,
    sem::PermissionMask permissions) {
    if (permissions == 0) {
        return;
    }
    module.setDeclarationPermissions(name, signature, permissions);
}

namespace {

/// The numeric permissions the tags of a declaration grant: each permission tag in the set.
sem::PermissionMask permissionsOfTags(attr::AttrSetId tags) {
    sem::PermissionMask mask = 0;
    for (const attr::Attr &entry : attr::entries(tags)) {
        mask = static_cast<sem::PermissionMask>(mask | permissionOfName(attr::keyName(entry.key)));
    }
    return mask;
}

/// The tag `strict` (design principles 7.4): a standard tag, declared once with the others.
TagId strictTag() {
    static const TagId id = [] {
        const auto found = find(std::string_view{}, "strict");
        return found.has_value() ? *found : kNoTag;
    }();
    return id;
}

bool hasStrictTag(attr::AttrSetId tags) {
    const TagId strict = strictTag();
    if (strict == kNoTag || tags == attr::kNoAttrs) {
        return false;
    }
    const Decl *decl = get(strict);
    if (decl == nullptr) {
        return false;
    }
    for (const attr::Attr &entry : attr::entries(tags)) {
        if (entry.key == decl->key) {
            return true;
        }
    }
    return false;
}

Facts factsOfSet(attr::AttrSetId tags, sem::PermissionMask permissions) {
    Facts facts;
    facts.tags        = tags;
    facts.permissions = static_cast<sem::PermissionMask>(permissions | permissionsOfTags(tags));
    facts.strict      = hasStrictTag(tags);
    facts.readers     = readersOf(tags);
    return facts;
}

} // namespace

Facts factsOf(const module::Module &module, const Reference &name) {
    return factsOfSet(module.declarationTags(name), module.declarationPermissions(name));
}

Facts factsOf(const module::Module &module, const Reference &name, std::string_view signature) {
    return factsOfSet(
        module.declarationTags(name, signature),
        module.declarationPermissions(name, signature));
}

Facts factsOfField(const module::Module &module, const Reference &typeName, size_t index) {
    return factsOfSet(module.fieldTags(typeName, index), 0);
}

Facts factsOf(const camel::runtime::GraphDraft &draft, camel::runtime::gc_node_ref_t node) {
    return factsOfSet(draft.nodeAttrs(node), draft.nodePermissions(node));
}

Facts factsOf(const camel::runtime::GCGraph &graph, camel::runtime::gc_node_ref_t node) {
    return factsOfSet(graph.nodeAttrs(node), graph.nodePermissions(node));
}

sem::PermissionMask
permissionsOf(const camel::runtime::GraphDraft &draft, camel::runtime::gc_node_ref_t node) {
    return factsOf(draft, node).permissions;
}

sem::PermissionMask
permissionsOf(const camel::runtime::GCGraph &graph, camel::runtime::gc_node_ref_t node) {
    return factsOf(graph, node).permissions;
}

sem::PermissionMask effectivePermissions(
    const camel::runtime::GraphDraft &draft, camel::runtime::gc_node_ref_t node,
    const sem::NumericPolicy &policy) {
    const Facts facts = factsOf(draft, node);
    return facts.strict ? sem::kNoPermissions : policy.effective(facts.permissions);
}

sem::PermissionMask effectivePermissions(
    const camel::runtime::GCGraph &graph, camel::runtime::gc_node_ref_t node,
    const sem::NumericPolicy &policy) {
    const Facts facts = factsOf(graph, node);
    return facts.strict ? sem::kNoPermissions : policy.effective(facts.permissions);
}

namespace {

// The standard tag (declareStandardTags) and the node attribute its fact is carried in.
constexpr std::string_view kIndependent = "independent";

attr::AttrKey independentKey() {
    const auto key = attr::findKey(kIndependent);
    return key ? *key : attr::kNoKey;
}

} // namespace

bool declaresIndependentElements(const camel::runtime::GraphDraft &draft) {
    const attr::AttrKey key = independentKey();
    const auto exit         = draft.exitNode();
    if (key == attr::kNoKey || exit == camel::runtime::kInvalidNodeRef) {
        return false;
    }
    return attr::valueOf(draft.nodeAttrs(exit), key) != attr::kNoValue;
}

bool declaresIndependentElements(const camel::runtime::GCGraph &graph) {
    const attr::AttrKey key = independentKey();
    const auto exit         = graph.exitNodeRef();
    if (key == attr::kNoKey || exit == camel::runtime::kInvalidNodeRef) {
        return false;
    }
    return attr::valueOf(graph.nodeAttrs(exit), key) != attr::kNoValue;
}

void markIndependentElements(camel::runtime::GraphDraft &draft, attr::AttrSetId declaration) {
    const attr::AttrKey key = independentKey();
    const auto exit         = draft.exitNode();
    if (key == attr::kNoKey || exit == camel::runtime::kInvalidNodeRef ||
        attr::valueOf(declaration, key) == attr::kNoValue) {
        return;
    }
    draft.setNodeAttrs(
        exit, attr::unite(draft.nodeAttrs(exit), attr::single(key, attr::unit())));
}

bool declaresMacro(attr::AttrSetId declaration) {
    if (declaration == attr::kNoAttrs) {
        return false;
    }
    const auto id = find(std::string_view{}, "macro");
    if (!id.has_value()) {
        return false;
    }
    const Decl *decl = get(*id);
    return decl != nullptr && attr::valueOf(declaration, decl->key) != attr::kNoValue;
}

void attachToField(
    module::Module &module, const Reference &typeName, size_t index, attr::AttrSetId tags) {
    if (tags == attr::kNoAttrs) {
        return;
    }
    module.setFieldTags(typeName, index, tags);
}

void dump(const module::Module &module, std::ostream &os) {
    if (module.declaredTags().empty()) {
        return;
    }
    os << "tags of " << module.name() << ":\n";
    for (const auto &[written, id] : module.declaredTags()) {
        const Decl *decl = get(static_cast<TagId>(id));
        if (decl == nullptr) {
            continue;
        }
        os << "  " << decl->name << " on ";
        bool first = true;
        for (const Position position :
             {Func, Type, Field, Nodes, Param}) {
            if ((decl->on & position) == 0) {
                continue;
            }
            os << (first ? "" : ", ") << positionName(position);
            first = false;
        }
        if (decl->isGroup()) {
            os << " = [";
            for (size_t i = 0; i < decl->aliases.size(); ++i) {
                os << (i == 0 ? "" : ", ") << decl->aliases[i].toString();
            }
            os << "]";
        }
        if (!decl->params.empty()) {
            os << "<";
            for (size_t i = 0; i < decl->params.size(); ++i) {
                os << (i == 0 ? "" : ", ") << decl->params[i].name << ": "
                   << decl->params[i].type;
            }
            os << ">";
        }
        if (!decl->readBy.empty()) {
            os << " read_by ";
            for (size_t i = 0; i < decl->readBy.size(); ++i) {
                os << (i == 0 ? "" : ", ") << decl->readBy[i];
            }
        }
        os << "\n";
    }
    for (const auto &[name, overloads] : module.declarationTagSets()) {
        for (const auto &[signature, facts] : overloads) {
            os << "  " << name;
            if (!signature.empty()) {
                os << "<" << signature << ">";
            }
            os << " : " << attr::text(facts) << "\n";
        }
    }
    for (const auto &[field, facts] : module.fieldTagSets()) {
        os << "  " << field.first << "." << field.second << " : " << attr::text(facts) << "\n";
    }
}

} // namespace camel::core::tag
