// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "builder_helpers.h"
#include "camel/core/data/composite/struct.h"
#include "camel/core/data/primary.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/error/diagnostics/messages/semantic.h"
#include "camel/core/tags.h"
#include "camel/utils/number_text.h"

namespace camel::compile::gir {

using namespace camel::core::error;

namespace {

/// The kind a tag parameter declares, as far as a tag can carry it. A tag argument is a
/// compile-time constant: an integer, a real, a boolean or a string, or, for a parameter
/// declared as a function (a concrete function type, or a type parameter the condition selects
/// with `is_function::<F>()`), a function of the program (carried as its graph reference).
enum class ArgKind { Int, Float, Bool, String, Func, Other };

/// The type a tag parameter is declared with, as it is compared: letters lowercased and spaces
/// dropped, so `Int` and `int ` are the same declaration.
std::string foldedType(std::string_view type) {
    std::string t;
    for (const char c : type) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return t;
}

/// Whether the declared type is written out as a function type (`(x: int) => int`). The arrow
/// is what tells it from every other type a parameter could declare, and it is the parameter
/// that fixes the signature: the tag machinery checks the function's own signature against it.
bool isFunctionTypeText(std::string_view type) {
    return type.find("=>") != std::string_view::npos;
}

/// Whether the declared type is one of the declaration's type parameters (`::<F>`).
bool namesTypeParameter(const camel::core::tag::Decl &decl, std::string_view type) {
    const std::string t = foldedType(type);
    for (const std::string &param : decl.typeParams) {
        if (foldedType(param) == t) {
            return true;
        }
    }
    return false;
}

/// Whether the declaration's condition says the parameter's type is a function
/// (`when is_function::<F>()`). The condition is one text and the name is matched in it with the
/// spaces dropped, exactly as a pass declaration's condition is (Builder::isPassDeclaration): a
/// tag carries no instance of its own for the type parameter to be bound in, so there is no
/// other reading of it. A condition that also says something else still selects every type
/// parameter it names this way.
bool conditionSelectsFunction(const camel::core::tag::Decl &decl, std::string_view type) {
    return foldedType(decl.when).find("is_function::<" + foldedType(type) + ">()") !=
           std::string::npos;
}

ArgKind kindOfDeclaredType(const camel::core::tag::Decl &decl, std::string_view type) {
    const std::string t = foldedType(type);
    if (t == "int" || t == "int8" || t == "int16" || t == "int32" || t == "int64" || t == "i8" ||
        t == "i16" || t == "i32" || t == "i64" || t == "u8" || t == "u16" || t == "u32" ||
        t == "u64" || t == "uint" || t == "integer") {
        return ArgKind::Int;
    }
    if (t == "float" || t == "float32" || t == "float64" || t == "f32" || t == "f64" ||
        t == "double" || t == "real" || t == "bf16" || t == "f16") {
        return ArgKind::Float;
    }
    if (t == "bool" || t == "boolean") {
        return ArgKind::Bool;
    }
    if (t == "string" || t == "str" || t == "text") {
        return ArgKind::String;
    }
    if (isFunctionTypeText(type)) {
        return ArgKind::Func;
    }
    if (namesTypeParameter(decl, type) && conditionSelectsFunction(decl, type)) {
        return ArgKind::Func;
    }
    // No other type a tag parameter may declare: a parameter of any other type is reported
    // where the tag is written (TagParameterType).
    return ArgKind::Other;
}

/// The comparable form of a function type: lowercased, without spaces and without the parameter
/// names, so the type a declaration writes (`(x: int) => int`) and the type a function of the
/// program has (`(n: i64) => i64`) compare as the signature they are.
std::string foldedSignature(std::string_view text) {
    const std::string folded = foldedType(text);
    std::string out;
    size_t i = 0;
    while (i < folded.size()) {
        const char c = folded[i];
        const bool word = (c >= 'a' && c <= 'z') || c == '_';
        if (word) {
            size_t end = i;
            while (end < folded.size() &&
                   ((folded[end] >= 'a' && folded[end] <= 'z') ||
                    (folded[end] >= '0' && folded[end] <= '9') || folded[end] == '_')) {
                ++end;
            }
            // `name:` is a parameter's name, not part of its type; `::` never is.
            if (end < folded.size() && folded[end] == ':' &&
                (end + 1 >= folded.size() || folded[end + 1] != ':')) {
                i = end + 1;
                continue;
            }
            out += folded.substr(i, end - i);
            i = end;
            continue;
        }
        out += c;
        ++i;
    }
    return out;
}

/// The compile-time kind a tag parameter restores to, read from the type name the declaration
/// writes. The language's own names are `int` (i64) and `float` (f64), and their explicit
/// spellings name the same objects, so a parameter restores as the type it names and not as the
/// kind the fact happens to be stored in (`int8`, `bf16` and the like are `Other`: the
/// compile-time value layer has no value of those types to make, so those fall back to the stored
/// kind rather than inventing one).
enum class RestoredKind { Int64, Int32, Float64, Float32, Bool, String, Func, Other };

RestoredKind restoredKindOf(const camel::core::tag::Decl &decl, std::string_view type) {
    const std::string t = foldedType(type);
    if (t == "int" || t == "int64" || t == "i64" || t == "integer") {
        return RestoredKind::Int64;
    }
    if (t == "int32" || t == "i32") {
        return RestoredKind::Int32;
    }
    if (t == "float" || t == "float64" || t == "f64" || t == "double" || t == "real") {
        return RestoredKind::Float64;
    }
    if (t == "float32" || t == "f32") {
        return RestoredKind::Float32;
    }
    if (t == "bool" || t == "boolean") {
        return RestoredKind::Bool;
    }
    if (t == "string" || t == "str" || t == "text") {
        return RestoredKind::String;
    }
    if (kindOfDeclaredType(decl, type) == ArgKind::Func) {
        return RestoredKind::Func;
    }
    return RestoredKind::Other;
}

/// The empty record: one object per build, interned by its (empty) field list, so every `{}` a
/// tag with no arguments gives is the same type.
data_ptr_t emptyRecord() {
    camel::core::data::StructDataFactory empty;
    return empty.build();
}

ArgKind kindOfValue(camel::core::type::TypeCode code) {
    using camel::core::type::TypeCode;
    switch (code) {
    case TypeCode::Int32:
    case TypeCode::Int64:
        return ArgKind::Int;
    case TypeCode::Float32:
    case TypeCode::Float64:
    case TypeCode::BFloat16:
    case TypeCode::Float16:
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
    case ArgKind::Func:
        return "a function";
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
          camel::core::tag::Module,
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
        if (auto core = camel::core::tag::find(std::string_view{}, written); core.has_value()) {
            return core;
        }
    }
    // A tag is a name of the entity namespace: an imported one resolves exactly as an imported
    // function does, by name or through the module that exports it.
    const Reference ref(written);
    if (auto entity = importedEntity(ref); entity.has_value() && entity->isTag()) {
        return entity->tagId();
    }
    return std::nullopt;
}

camel::core::attr::AttrValueId
Builder::tagValueOf(const camel::core::tag::Decl &decl, const GCT::TagUse &use) {
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
    // The value of one constant argument, by the kind of data it is.
    const auto valueOf =
        [&](const camel::core::data::data_ptr_t &data) -> camel::core::attr::AttrValueId {
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
        case TypeCode::BFloat16:
        case TypeCode::Float16:
            // A bf16 or f16 constant is a real to the pass that reads it: the attribute carries
            // the value, so the f32 it widens to is what it means (design principles 7.3).
            return camel::core::attr::real(
                std::static_pointer_cast<camel::core::data::HalfData>(data)->toFloat());
        case TypeCode::Bool:
            return camel::core::attr::integer(
                std::static_pointer_cast<camel::core::data::BoolData>(data)->data() ? 1 : 0);
        case TypeCode::String:
            return camel::core::attr::symbol(
                std::static_pointer_cast<camel::core::data::StringData>(data)->data());
        default:
            // Every kind this switch does not handle is refused by the parameter's declared
            // kind before the value is read (TagParameterType / TagArgumentType), so no
            // argument of such a type reaches this point.
            throw BuildAbortException();
        }
    };
    // The written form of a constant, not the data's six-decimal rendering.
    const auto writtenText = [](const camel::core::data::data_ptr_t &data) {
        std::string written = data->toString();
        if (data->type()->code() == camel::core::type::TypeCode::Float32) {
            written = camel::utils::floatText(
                std::static_pointer_cast<camel::core::data::FloatData>(data)->data());
        } else if (data->type()->code() == camel::core::type::TypeCode::Float64) {
            written = camel::utils::floatText(
                std::static_pointer_cast<camel::core::data::DoubleData>(data)->data());
        }
        return written;
    };
    bool takesFunction = false;
    for (const camel::core::tag::TagParam &param : decl.params) {
        takesFunction = takesFunction || kindOfDeclaredType(decl, param.type) == ArgKind::Func;
    }
    if (takesFunction) {
        // A parameter declared as a function names a function of the program (design principles
        // 6.2: `#vjp(cube_rule)`). Its fact is the graph reference of the function
        // (attr::graphRef), not a data value: the graph is added to the reference table of the
        // declaring graph by attachRuleGraphs, so the tag's reader finds it in the frozen graph
        // instead of resolving the name again.
        std::vector<camel::core::attr::AttrValueId> values;
        values.reserve(use.args.size());
        for (size_t i = 0; i < use.args.size(); ++i) {
            const camel::core::tag::TagParam &param = decl.params[i];
            const ArgKind declared                  = kindOfDeclaredType(decl, param.type);
            const GCT::node_ptr_t &arg              = use.args[i];
            if (declared == ArgKind::Func) {
                std::optional<compile_graph_ptr_t> graph;
                if (arg != nullptr && arg->type() == GCT::NodeKind::DREF) {
                    graph = functionGraphNamed(arg->name());
                }
                if (!graph.has_value()) {
                    diags_->of(SemanticDiag::TagArgumentType)
                        .atOrigin(use.origin)
                        .commit(
                            param.name,
                            use.name,
                            param.type,
                            arg == nullptr ? std::string{} : arg->name(),
                            "not a function of the program");
                    throw BuildAbortException();
                }
                // A parameter whose type is written out as a function type fixes the signature
                // the argument must have: the check is made here, where the tag is pasted. A
                // parameter declared as a type parameter (`rule: F`) names no signature for
                // this machinery to compare, so what its condition asks for is the reader's
                // question, answered at the tag's position when it reads the version it asks
                // the function to build.
                const camel::core::type::FunctionType *type = graph.value()->funcType();
                const std::string actual =
                    type == nullptr
                        ? std::string{}
                        : camel::core::type::sourceTypeSpelling(type->toString());
                if (isFunctionTypeText(param.type) && type != nullptr &&
                    foldedSignature(param.type) != foldedSignature(actual)) {
                    diags_->of(SemanticDiag::TagArgumentType)
                        .atOrigin(use.origin)
                        .commit(param.name, use.name, param.type, arg->name(), actual);
                    throw BuildAbortException();
                }
                const camel::runtime::GraphId id                = graph.value()->graphId();
                tagFunctionGraphs_[camel::runtime::toValue(id)] = graph.value();
                values.push_back(camel::core::attr::graphRef(id));
                continue;
            }
            if (arg == nullptr || arg->type() != GCT::NodeKind::DATA) {
                diags_->of(SemanticDiag::TagArgumentNotConstant)
                    .atOrigin(use.origin)
                    .commit(arg == nullptr ? std::string{} : arg->name(), use.name);
                throw BuildAbortException();
            }
            if (declared == ArgKind::Other) {
                diags_->of(SemanticDiag::TagParameterType)
                    .atOrigin(use.origin)
                    .commit(param.name, use.name, param.type);
                throw BuildAbortException();
            }
            const ArgKind actual = kindOfValue(arg->data()->type()->code());
            if (actual != declared) {
                diags_->of(SemanticDiag::TagArgumentType)
                    .atOrigin(use.origin)
                    .commit(
                        param.name,
                        use.name,
                        param.type,
                        writtenText(arg->data()),
                        kindName(actual));
                throw BuildAbortException();
            }
            values.push_back(valueOf(arg->data()));
        }
        if (values.empty()) {
            return camel::core::attr::unit();
        }
        if (values.size() == 1) {
            return values.front();
        }
        return camel::core::attr::tuple(values);
    }
    // A parameter of a kind a tag cannot carry is refused by name whatever argument is written
    // (TagParameterType): the declared kind is the same rule in this branch as in the one above,
    // and a function name written for a parameter declared with a kind the language does not have
    // would otherwise be reported as an argument that is not a compile-time value -- telling the
    // writer to write what they wrote.
    for (const camel::core::tag::TagParam &param : decl.params) {
        if (kindOfDeclaredType(decl, param.type) == ArgKind::Other) {
            diags_->of(SemanticDiag::TagParameterType)
                .atOrigin(use.origin)
                .commit(param.name, use.name, param.type);
            throw BuildAbortException();
        }
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
        // The arguments are names, not values (`#reads(reassociate, contract)`): the fact is
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
            const auto name = std::static_pointer_cast<camel::core::data::StringData>(arg)->data();
            // A reader names tags, so a name that is not a declared one is a typo, not a fact.
            if (decl.namesAreTags && !tagNamed(name).has_value()) {
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
    // `string` a string. A parameter of a kind a tag cannot carry was refused above, not carried
    // as something else.
    for (size_t i = 0; i < args.size(); ++i) {
        const camel::core::tag::TagParam &param = decl.params[i];
        const ArgKind declared                  = kindOfDeclaredType(decl, param.type);
        const ArgKind actual = kindOfValue(args[i]->type()->code());
        if (actual != declared) {
            diags_->of(SemanticDiag::TagArgumentType)
                .atOrigin(use.origin)
                .commit(param.name, use.name, param.type, writtenText(args[i]), kindName(actual));
            throw BuildAbortException();
        }
    }
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

camel::core::data::data_ptr_t Builder::tagArgumentValue(
    const camel::core::tag::Decl *decl, const std::string &qualified, size_t index,
    camel::core::attr::AttrValueId id, const GCT::node_ptr_t &head) {
    using camel::core::attr::ValueKind;
    const camel::core::attr::Value value = camel::core::attr::value(id);
    const RestoredKind declared =
        decl != nullptr && index < decl->params.size()
            ? restoredKindOf(*decl, decl->params[index].type)
            : RestoredKind::Other;
    switch (declared) {
    case RestoredKind::Bool:
        // The fact of a boolean argument is the 0 or 1 an integer holds (attr keeps no boolean
        // kind): the parameter's declared type is what says the program reads it back as a bool.
        return std::make_shared<camel::core::data::BoolData>(value.integer != 0);
    case RestoredKind::Int64:
        return std::make_shared<camel::core::data::LongData>(value.integer);
    case RestoredKind::Int32:
        return std::make_shared<camel::core::data::IntData>(static_cast<int32_t>(value.integer));
    case RestoredKind::Float64:
        return std::make_shared<camel::core::data::DoubleData>(value.real);
    case RestoredKind::Float32:
        return std::make_shared<camel::core::data::FloatData>(static_cast<float>(value.real));
    case RestoredKind::String:
        return std::make_shared<camel::core::data::StringData>(value.text);
    case RestoredKind::Func:
    case RestoredKind::Other:
        break;
    }
    if (declared == RestoredKind::Func || value.kind == ValueKind::Graph) {
        // The argument is the graph reference of a function of the program: an internal number
        // the language has no value for. Until the reading can give that function itself, the
        // reading is refused here rather than given the decimal text of the id.
        diags_->of(SemanticDiag::TagFunctionArgumentNotReadable)
            .atOrigin(head->origin())
            .commit(
                qualified,
                decl != nullptr && index < decl->params.size() ? decl->params[index].name
                                                               : std::string("?"));
        throw BuildAbortException();
    }
    // A tag this does not know, or a parameter type the compile-time value layer has no kind
    // for: the stored kind is all there is to go by.
    switch (value.kind) {
    case ValueKind::Int:
        return std::make_shared<camel::core::data::LongData>(value.integer);
    case ValueKind::Float:
        return std::make_shared<camel::core::data::DoubleData>(value.real);
    case ValueKind::Symbol:
        return std::make_shared<camel::core::data::StringData>(value.text);
    default:
        return emptyRecord();
    }
}

std::optional<compile_graph_ptr_t> Builder::functionGraphNamed(const std::string &written) {
    if (auto graphs = graphsAt(written); graphs.has_value() && graphs.value()->size() == 1) {
        return graphs.value()->front();
    }
    if (auto decorated = decoratedGraphAt(written);
        decorated.has_value() && overloadsOfDecorated(written, *decorated) == nullptr) {
        return decorated.value();
    }
    if (auto entity = importedEntity(Reference(written)); entity.has_value()) {
        using camel::core::module::detail::EntityAccess;
        if (EntityAccess::isGraphSet(*entity)) {
            auto set = asCompileGraphSet(EntityAccess::graphSet(*entity));
            if (set != nullptr && set->size() == 1) {
                return set->front();
            }
        } else if (EntityAccess::isDecoratedGraph(*entity)) {
            // A name that is a decorated function and other functions of the same name stands
            // for all of them, and the one function a tag reaches is what the decorators made
            // (overloadsOfDecorated refuses a decorated function that has overloads).
            return EntityAccess::decoratedGraph(*entity);
        }
    }
    return std::nullopt;
}

void Builder::attachRuleGraphs(const compile_graph_ptr_t &graph, camel::core::attr::AttrSetId set) {
    if (!graph || set == camel::core::attr::kNoAttrs) {
        return;
    }
    const std::function<void(camel::core::attr::AttrValueId)> attach =
        [&](camel::core::attr::AttrValueId id) {
            const camel::core::attr::Value value = camel::core::attr::value(id);
            if (value.kind == camel::core::attr::ValueKind::Graph) {
                const uint32_t key = static_cast<uint32_t>(std::stoul(value.text));
                if (const auto it = tagFunctionGraphs_.find(key); it != tagFunctionGraphs_.end()) {
                    graph->addRuleGraphRef(it->second);
                }
            } else if (value.kind == camel::core::attr::ValueKind::Tuple) {
                for (camel::core::attr::AttrValueId item : value.items) {
                    attach(item);
                }
            }
        };
    for (const camel::core::attr::Attr &attr : camel::core::attr::entries(set)) {
        attach(attr.value);
    }
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
    // Before `module` (`#gpu module m`, design principles 13.4) the tag is the fact of every
    // top-level function of that module, so it is read exactly as a function's own tag is; the
    // propagation itself is in tagFactsOf.
    const bool asDeclaration = at == camel::core::tag::Func || at == camel::core::tag::Type ||
                               at == camel::core::tag::Field ||
                               at == camel::core::tag::Module;
    const bool asNodes       = at == camel::core::tag::Nodes;
    const bool declaration   = allows && asDeclaration;
    const bool nodes         = ((decl->on & camel::core::tag::Nodes) != 0) &&
                       (asNodes || at == camel::core::tag::Func ||
                        at == camel::core::tag::Module);
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
        facts.permissions =
            static_cast<camel::core::sem::PermissionMask>(facts.permissions | decl->permission);
        if (declaration) {
            facts.declaration = camel::core::attr::unite(
                facts.declaration,
                camel::core::attr::single(decl->key, value));
        }
        if (context_ != nullptr && !decl->readBy.empty()) {
            context_->noteTagUse({decl->name, decl->readBy, use.origin});
        }
        return;
    }
    const auto mergeFact = [&](camel::core::attr::AttrSetId &set) {
        const camel::core::attr::AttrValueId current = camel::core::attr::valueOf(set, decl->key);
        if (current != camel::core::attr::kNoValue && current != value) {
            diags_->of(SemanticDiag::TagValueConflict)
                .atOrigin(use.origin)
                .commit(
                    use.name,
                    std::string(camel::core::attr::keyName(decl->key)),
                    camel::core::attr::valueText(value),
                    camel::core::attr::valueText(current));
            throw BuildAbortException();
        }
        set = camel::core::attr::unite(set, camel::core::attr::single(decl->key, value));
    };
    if (declaration) {
        mergeFact(facts.declaration);
    }
    if (nodes) {
        mergeFact(facts.nodes);
    }
    // The pipeline can tell the programmer when nothing reads the tag: the declaration names
    // the passes, and a Camel pass names the tags it reads with `#reads(...)`. The use is where
    // the tag was written.
    if (context_ != nullptr && !decl->readBy.empty()) {
        context_->noteTagUse({decl->name, decl->readBy, use.origin});
    }
}

Builder::TagFacts
Builder::tagFactsOfList(const std::vector<GCT::TagUse> &uses, camel::core::tag::Position at) {
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
    // A tag written before `module` (`#gpu module m`, design principles 13.4) is the fact of
    // every top-level function of that module: read first, so a fact the function writes itself
    // is united over it and the inner value wins (the key's own rule). Only a function declared
    // at the top level of the module is reached: a function written inside a body has the module
    // as its scope anyway, and the module's tags are not its declaration's facts.
    if (at == camel::core::tag::Func) {
        const TagFacts outer = moduleTagFactsOf(gct);
        if (outer.declaration != camel::core::attr::kNoAttrs ||
            outer.nodes != camel::core::attr::kNoAttrs || outer.permissions != 0) {
            facts.declaration = outer.declaration;
            facts.nodes       = outer.nodes;
            facts.permissions = outer.permissions;
        }
    }
    if (const auto *uses = GCT::Tree::of(gct)->tagUsesOf(gct); uses != nullptr) {
        const TagFacts own = tagFactsOfList(*uses, at);
        // The function's own fact is the inner one for every key it writes.
        facts.declaration = camel::core::attr::unite(facts.declaration, own.declaration);
        facts.nodes       = camel::core::attr::unite(facts.nodes, own.nodes);
        facts.permissions =
            static_cast<camel::core::sem::PermissionMask>(facts.permissions | own.permissions);
    }
    tagFacts_[gct] = facts;
    return facts;
}

/// The facts a tag written before `module` gives a function declared at the top level of that
/// module (design principles 13.4: a tag of a function, a block or a module). Empty for anything
/// else: a nested function, a function of another module, or a module that carries no tags.
Builder::TagFacts Builder::moduleTagFactsOf(const GCT::node_ptr_t &gct) {
    const auto *tree = GCT::Tree::of(gct);
    if (tree == nullptr) {
        return {};
    }
    const GCT::node_ptr_t root = tree->root();
    if (root == nullptr) {
        return {};
    }
    bool topLevel = false;
    for (size_t i = 0; i < root->size() && !topLevel; ++i) {
        topLevel = root->at(i) == gct;
    }
    if (!topLevel) {
        return {};
    }
    const auto *uses = tree->tagUsesOf(root);
    if (uses == nullptr) {
        return {};
    }
    return tagFactsOfList(*uses, camel::core::tag::Module);
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

std::optional<Builder::ImplTag> Builder::implTagOf(const GCT::node_ptr_t &gct) {
    if (gct == nullptr) {
        return std::nullopt;
    }
    // `impl` is a tag of the core, so a program writes it without importing anything. The
    // standard declaration is the one this reads; a module that declares a tag of its own with
    // that name is a different tag, and a use of it is not this one (design principles 10.2).
    const auto implTag = camel::core::tag::find(std::string_view{}, "impl");
    if (!implTag.has_value()) {
        return std::nullopt;
    }
    const camel::core::tag::Decl *decl = camel::core::tag::get(*implTag);
    const auto *uses                   = GCT::Tree::of(gct)->tagUsesOf(gct);
    if (decl == nullptr || uses == nullptr) {
        return std::nullopt;
    }
    for (const GCT::TagUse &use : *uses) {
        const auto named = tagNamed(use.name);
        if (!named.has_value() || *named != *implTag) {
            continue;
        }
        // The argument is one name, carried as a one-item tuple of symbols (`namesAreTags`
        // false: the name is a declaration, not a tag). A wrong number or kind is reported by
        // tagValueOf here, at the tag, like every other argument mistake.
        const camel::core::attr::Value value = camel::core::attr::value(tagValueOf(*decl, use));
        if (value.kind != camel::core::attr::ValueKind::Tuple || value.items.size() != 1) {
            continue;
        }
        const camel::core::attr::Value kernel = camel::core::attr::value(value.items.front());
        if (kernel.kind != camel::core::attr::ValueKind::Symbol || kernel.text.empty()) {
            continue;
        }
        return ImplTag{std::move(kernel.text), use.origin};
    }
    return std::nullopt;
}

} // namespace camel::compile::gir
