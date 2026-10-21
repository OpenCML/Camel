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
 * Author: Camel Contributors
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

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

#include <deque>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>

#include "camel/core/module/module.h"
#include "camel/core/operator_table.h"

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

} // namespace

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

attr::AttrSetId ofField(const module::Module &module, const Reference &typeName, size_t index) {
    return module.fieldTags(typeName, index);
}

void attachToDeclaration(module::Module &module, const Reference &name, attr::AttrSetId tags) {
    if (tags == attr::kNoAttrs) {
        return;
    }
    module.setDeclarationTags(name, tags);
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
    for (const auto &[name, facts] : module.declarationTagSets()) {
        os << "  " << name << " : " << attr::text(facts) << "\n";
    }
    for (const auto &[field, facts] : module.fieldTagSets()) {
        os << "  " << field.first << "." << field.second << " : " << attr::text(facts) << "\n";
    }
}

} // namespace camel::core::tag