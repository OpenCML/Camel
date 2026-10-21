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
 * Resolving the `#` tags of a program (design principles 6.2).
 *
 * The GCT builder keeps the uses written on a declaration beside its node (GCT::Tree). Here,
 * where imports and this module are known, each use is resolved once: which tag it names,
 * whether the tag may be written there, what value its arguments give, and where the facts go.
 *
 *   - `on nodes`: the facts become node attributes of everything written below the use (`TagFacts
 *     .nodes`), which the callers pass into the attribute scope of a body or a block. The
 *     attribute key's combine rule accumulates the facts of the enclosing regions, and an inner
 *     same-name tag with a parameter wins (attr::rules::inner) -- that is the whole mechanism
 *     (camel/core/node_attrs.h).
 *   - `on func`, `on type`, `on field`: the facts are attached to the declaration, in the module
 *     that declares it (tag::attachToDeclaration / attachToField), and read by a C++ module with
 *     tag::ofDeclaration / ofField.
 *
 * A tag is a name of the module's entity namespace, so `#tag` resolves like a function call:
 * declared here, imported by name, or qualified through an imported module.
 */

#include "builder.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <string>
#include <vector>

#include "camel/core/data/primary.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/error/diagnostics/messages/semantic.h"
#include "camel/core/tags.h"
#include "camel/utils/number_text.h"

namespace camel::compile::gir {

using namespace camel::core::error;

namespace {

/// The kind a tag parameter declares, as far as a tag can carry it. A tag argument is a
/// compile-time constant: an integer, a real, a boolean or a string.
enum class ArgKind { Int, Float, Bool, String, Other };

ArgKind kindOfDeclaredType(std::string_view type) {
    std::string t;
    for (const char c : type) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    if (t == "int" || t == "int8" || t == "int16" || t == "int32" || t == "int64" ||
        t == "i8" || t == "i16" || t == "i32" || t == "i64" || t == "u8" || t == "u16" ||
        t == "u32" || t == "u64" || t == "uint" || t == "integer") {
        return ArgKind::Int;
    }
    if (t == "float" || t == "float32" || t == "float64" || t == "f32" || t == "f64" ||
        t == "double" || t == "real") {
        return ArgKind::Float;
    }
    if (t == "bool" || t == "boolean") {
        return ArgKind::Bool;
    }
    if (t == "string" || t == "str" || t == "text") {
        return ArgKind::String;
    }
    return ArgKind::Other;
}

ArgKind kindOfValue(camel::core::type::TypeCode code) {
    using camel::core::type::TypeCode;
    switch (code) {
    case TypeCode::Int32:
    case TypeCode::Int64:
        return ArgKind::Int;
    case TypeCode::Float32:
    case TypeCode::Float64:
        return ArgKind::Float;
    case TypeCode::Bool:
        return ArgKind::Bool;
    case TypeCode::String:
        return ArgKind::String;
    default:
        return ArgKind::Other;
    }
}

const char *kindName(ArgKind kind) {
    switch (kind) {
    case ArgKind::Int:
        return "int";
    case ArgKind::Float:
        return "float";
    case ArgKind::Bool:
        return "bool";
    case ArgKind::String:
        return "string";
    default:
        return "other";
    }
}

/// The positions of a declaration as the source writes them (`func, nodes`).
std::string positionsText(camel::core::tag::Positions positions) {
    std::string text;
    for (camel::core::tag::Position position :
         {camel::core::tag::Func,
          camel::core::tag::Type,
          camel::core::tag::Field,
          camel::core::tag::Nodes,
          camel::core::tag::Param}) {
        if ((positions & position) == 0) {
            continue;
        }
        text += (text.empty() ? "" : ", ") + std::string(camel::core::tag::positionName(position));
    }
    return text;
}

} // namespace

std::optional<camel::core::tag::TagId> Builder::tagNamed(const std::string &written) {
    if (auto local = module_->declaredTag(written); local.has_value()) {
        return local;
    }
    // The language's own tags (`reassociate`, `finite_math`, ...) are names of the core: a
    // program writes them without importing anything, as it writes `#macro`.
    if (written.find("::") == std::string::npos) {
        if (auto core = camel::core::tag::find(std::string_view{}, written);
            core.has_value()) {
            return core;
        }
    }
    // A tag is a name of the entity namespace: an imported one resolves exactly as an imported
    // function does, by name or through the module that exports it.
    const Reference ref(written);
    if (auto entity = module_->getImportedEntity(ref);
        entity.has_value() && entity->isTag()) {
        return entity->tagId();
    }
    return std::nullopt;
}

camel::core::attr::AttrValueId Builder::tagValueOf(
    const camel::core::tag::Decl &decl, const GCT::TagUse &use) {
    if (decl.variadic) {
        if (use.args.empty()) {
            diags_->of(SemanticDiag::TagArgumentCount)
                .atOrigin(use.origin)
                .commit(use.name, "1 or more", "0");
            throw BuildAbortException();
        }
    } else if (static_cast<size_t>(decl.params.size()) != use.args.size()) {
        diags_->of(SemanticDiag::TagArgumentCount)
            .atOrigin(use.origin)
            .commit(use.name, std::to_string(decl.params.size()), std::to_string(use.args.size()));
        throw BuildAbortException();
    }
    std::vector<camel::core::data::data_ptr_t> args;
    args.reserve(use.args.size());
    for (const auto &arg : use.args) {
        if (arg == nullptr || arg->type() != GCT::NodeKind::DATA) {
            diags_->of(SemanticDiag::TagArgumentNotConstant)
                .atOrigin(use.origin)
                .commit(arg == nullptr ? std::string{} : arg->name(), use.name);
            throw BuildAbortException();
        }
        args.push_back(arg->data());
    }
    if (decl.names) {
        // The arguments are names, not values (`#reads<reassociate, contract>`): the fact is
        // the tuple of them, one item per argument, always a tuple however many there are.
        std::vector<camel::core::attr::AttrValueId> items;
        items.reserve(args.size());
        for (const auto &arg : args) {
            if (arg->type()->code() != camel::core::type::TypeCode::String) {
                diags_->of(SemanticDiag::TagArgumentType)
                    .atOrigin(use.origin)
                    .commit("", use.name, "string", arg->toString(), "another kind");
                throw BuildAbortException();
            }
            const auto name =
                std::static_pointer_cast<camel::core::data::StringData>(arg)->data();
            // A reader names tags, so a name that is not a declared one is a typo, not a fact.
            if (!tagNamed(name).has_value()) {
                diags_->of(SemanticDiag::TagUndeclared).atOrigin(use.origin).commit(name);
                throw BuildAbortException();
            }
            items.push_back(camel::core::attr::symbol(name));
        }
        return camel::core::attr::tuple(items);
    }
    if (decl.value) {
        if (auto value = decl.value(args); value.has_value()) {
            return *value;
        }
        diags_->of(SemanticDiag::TagArgumentNotConstant)
            .atOrigin(use.origin)
            .commit(use.name + " arguments", use.name);
        throw BuildAbortException();
    }
    // Each argument must be a constant of the kind its parameter declares (design principles
    // 6.2): an `int` parameter takes an integer, a `float` one a real, `bool` a boolean and
    // `string` a string. A parameter of another declared kind is a located diagnostic here,
    // not a value silently carried as something else.
    for (size_t i = 0; i < args.size(); ++i) {
        const camel::core::tag::TagParam &param = decl.params[i];
        const ArgKind declared                  = kindOfDeclaredType(param.type);
        if (declared == ArgKind::Other) {
            diags_->of(SemanticDiag::TagParameterType)
                .atOrigin(use.origin)
                .commit(param.name, use.name, param.type);
            throw BuildAbortException();
        }
        const ArgKind actual = kindOfValue(args[i]->type()->code());
        if (actual != declared) {
            // The written form of the argument, not the data's six-decimal rendering.
            std::string written = args[i]->toString();
            if (args[i]->type()->code() == camel::core::type::TypeCode::Float32) {
                written = camel::utils::floatText(
                    std::static_pointer_cast<camel::core::data::FloatData>(args[i])->data());
            } else if (args[i]->type()->code() == camel::core::type::TypeCode::Float64) {
                written = camel::utils::floatText(
                    std::static_pointer_cast<camel::core::data::DoubleData>(args[i])->data());
            }
            diags_->of(SemanticDiag::TagArgumentType)
                .atOrigin(use.origin)
                .commit(param.name, use.name, param.type, written, kindName(actual));
            throw BuildAbortException();
        }
    }
    auto valueOf = [&](const camel::core::data::data_ptr_t &data) {
        using camel::core::type::TypeCode;
        switch (data->type()->code()) {
        case TypeCode::Int32:
            return camel::core::attr::integer(
                std::static_pointer_cast<camel::core::data::IntData>(data)->data());
        case TypeCode::Int64:
            return camel::core::attr::integer(
                std::static_pointer_cast<camel::core::data::LongData>(data)->data());
        case TypeCode::Float32:
            return camel::core::attr::real(
                std::static_pointer_cast<camel::core::data::FloatData>(data)->data());
        case TypeCode::Float64:
            return camel::core::attr::real(
                std::static_pointer_cast<camel::core::data::DoubleData>(data)->data());
        case TypeCode::Bool:
            return camel::core::attr::integer(
                std::static_pointer_cast<camel::core::data::BoolData>(data)->data() ? 1 : 0);
        case TypeCode::String:
            return camel::core::attr::symbol(
                std::static_pointer_cast<camel::core::data::StringData>(data)->data());
        default:
            diags_->of(SemanticDiag::TagArgumentUnsupported)
                .atOrigin(use.origin)
                .commit(data->toString(), use.name);
            throw BuildAbortException();
        }
    };
    if (args.empty()) {
        return camel::core::attr::unit();
    }
    if (args.size() == 1) {
        return valueOf(args.front());
    }
    std::vector<camel::core::attr::AttrValueId> items;
    items.reserve(args.size());
    for (const auto &arg : args) {
        items.push_back(valueOf(arg));
    }
    return camel::core::attr::tuple(items);
}

void Builder::addTagFacts(
    const GCT::TagUse &use, camel::core::tag::Position at, TagFacts &facts,
    std::unordered_set<camel::core::tag::TagId> &seen) {
    const auto id = tagNamed(use.name);
    if (!id.has_value()) {
        diags_->of(SemanticDiag::TagUndeclared).atOrigin(use.origin).commit(use.name);
        throw BuildAbortException();
    }
    const camel::core::tag::Decl *decl = camel::core::tag::get(*id);
    if (decl == nullptr) {
        return;
    }
    if (decl->isGroup()) {
        // A group stands for its members: each is used, and each must allow the position.
        if (!seen.insert(*id).second) {
            return; // a group that (through another) mentions itself
        }
        for (const Reference &member : decl->aliases) {
            GCT::TagUse memberUse = use;
            memberUse.name        = member.toString();
            addTagFacts(memberUse, at, facts, seen);
        }
        return;
    }
    const bool allows = (decl->on & at) != 0;
    // A tag written before a function may be a tag of the function (its declaration) or of the
    // nodes of its body: the declaration lists which. Anywhere else only the written position.
    const bool asDeclaration =
        at == camel::core::tag::Func || at == camel::core::tag::Type ||
        at == camel::core::tag::Field;
    const bool asNodes = at == camel::core::tag::Nodes;
    const bool declaration = allows && asDeclaration;
    const bool nodes       = ((decl->on & camel::core::tag::Nodes) != 0) &&
                       (asNodes || (at == camel::core::tag::Func));
    if (!declaration && !nodes) {
        diags_->of(SemanticDiag::TagNotAtPosition)
            .atOrigin(use.origin)
            .commit(
                use.name,
                std::string(camel::core::tag::positionName(at)),
                positionsText(decl->on));
        throw BuildAbortException();
    }
    const auto value = tagValueOf(*decl, use);
    if (decl->permission != 0) {
        // A numeric permission is a tag (design principles 6.6, 7.2) but its fact is the
        // permission column, not an attribute: the nodes get it through the permission scope
        // of the body or block (see visitExecNode and buildFuncBody). On a declaration the tag
        // is recorded too, so the facts query reports what the declaration grants.
        facts.permissions = static_cast<camel::core::sem::PermissionMask>(facts.permissions | decl->permission);
        if (declaration) {
            facts.declaration = camel::core::attr::unite(
                facts.declaration, camel::core::attr::single(decl->key, value));
        }
        if (context_ != nullptr && !decl->readBy.empty()) {
            context_->noteTagUse({decl->name, decl->readBy, use.origin});
        }
        return;
    }
    if (declaration) {
        facts.declaration = camel::core::attr::unite(
            facts.declaration, camel::core::attr::single(decl->key, value));
    }
    if (nodes) {
        facts.nodes =
            camel::core::attr::unite(facts.nodes, camel::core::attr::single(decl->key, value));
    }
    // The pipeline can tell the programmer when nothing reads the tag: the declaration names
    // the passes, and a Camel pass names the tags it reads with `#reads<...>`. The use is where
    // the tag was written.
    if (context_ != nullptr && !decl->readBy.empty()) {
        context_->noteTagUse({decl->name, decl->readBy, use.origin});
    }
}

Builder::TagFacts Builder::tagFactsOfList(
    const std::vector<GCT::TagUse> &uses, camel::core::tag::Position at) {
    TagFacts facts;
    std::unordered_set<camel::core::tag::TagId> seen;
    for (const auto &use : uses) {
        addTagFacts(use, at, facts, seen);
        seen.clear(); // a group may be used twice; only a cycle within one use is a cycle
    }
    return facts;
}

Builder::TagFacts Builder::tagFactsOf(const GCT::node_ptr_t &gct, camel::core::tag::Position at) {
    if (gct == nullptr) {
        return {};
    }
    if (const auto it = tagFacts_.find(gct); it != tagFacts_.end()) {
        return it->second;
    }
    TagFacts facts;
    if (const auto *uses = GCT::Tree::of(gct)->tagUsesOf(gct); uses != nullptr) {
        facts = tagFactsOfList(*uses, at);
    }
    tagFacts_[gct] = facts;
    return facts;
}

void Builder::recordFieldTags(const GCT::node_ptr_t &decl) {
    const auto *fields = GCT::Tree::of(decl)->fieldTagUsesOf(decl);
    if (fields == nullptr) {
        return;
    }
    const Reference typeName(decl->name());
    for (const auto &[index, uses] : *fields) {
        const TagFacts facts = tagFactsOfList(uses, camel::core::tag::Field);
        camel::core::tag::attachToField(*module_, typeName, index, facts.declaration);
    }
}

} // namespace camel::compile::gir
