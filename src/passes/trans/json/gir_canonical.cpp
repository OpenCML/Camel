// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The canonical JSON GIR document (version 2): the input of the equivalence checker
 * (test/tools/cml_roundtrip.py) and the ground every later step of the graph-to-source plan is
 * judged by (docs/design/graph-store/plan.cn.md, canon(P) in section 2.2).
 *
 * What it keeps is what equivalence is defined on, and nothing else:
 *
 *  - the graphs reachable from the root, and only their live nodes (a removed node's record is
 *    not a node);
 *  - regions by kind, parent, owner, index, form ports, params, result and attributes, with the
 *    annotation regions (Inline, Block) dissolved: their nodes belong to the nearest control
 *    ancestor, because an inlined body is not a different program (design principles 2.5);
 *  - node kinds, ordered inputs by kind and target slot, operator identity (the semantics key,
 *    not the URI spelling), payloads, and sorted tag sets;
 *  - types as a structural table, with every named declaration an atom of its own, so that two
 *    declarations with equal fields stay two types;
 *  - static values by value: a number by its bits, a string by its bytes, an array element by
 *    element, a function constant by the graph it names.
 *
 * What it drops is what a program does not have: graph and node names, node ids (the local
 * numbers below are this export's own), source origins and spans, parameter names, display
 * labels and mangled names.
 *
 * S0 does not normalise the entry yet: it marks the graph the root's entry call names and keeps
 * the root's entry chain, which is what the comparison of two dumps of one program needs. The
 * hoisting of an inlined entry (plan 2.2 step 3) belongs with the printer that makes it
 * necessary (S1).
 */

#include "camel/core/node_attrs.h"
#include "camel/core/operator_table.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/collection.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/rtdata/variant.h"
#include "camel/core/tags.h"
#include "camel/core/type/bytes.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/enum.h"
#include "camel/passes/trans/gir_metadata.h"
#include "camel/passes/trans/json/gir.h"
#include "camel/runtime/reachable.h"

#include <algorithm>
#include <bit>
#include <format>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_map>
#include <vector>

using nlohmann::json;
using namespace camel::runtime;
namespace type = camel::core::type;
namespace attr = camel::core::attr;

namespace {

constexpr int kMaxValueDepth = 64;

std::string hex64(uint64_t value) { return std::format("0x{:016x}", value); }

std::string regionKindName(GCRegionKind kind) {
    switch (kind) {
    case GCRegionKind::Root:
        return "body";
    case GCRegionKind::Arm:
        return "arm";
    case GCRegionKind::LoopCond:
        return "loop_cond";
    case GCRegionKind::LoopBody:
        return "loop_body";
    case GCRegionKind::Inline:
        return "inline";
    case GCRegionKind::Block:
        return "block";
    case GCRegionKind::Dead:
        return "dead";
    }
    return "dead";
}

std::string fillKindName(GCFillKind kind) {
    switch (kind) {
    case GCFillKind::Tuple:
        return "tuple";
    case GCFillKind::Array:
        return "array";
    case GCFillKind::Struct:
        return "struct";
    case GCFillKind::FunctionClosure:
        return "function_closure";
    }
    return "tuple";
}

/// A type's entry, without its table index: components are table indices filled in later.
class CanonicalDocument {
  public:
    CanonicalDocument(const Program &program, GCGraph *root)
        : program_(program), root_(root), graphs_(collectReachableGraphs(program, root)) {
        for (size_t i = 0; i < graphs_.size(); ++i) {
            const std::string id                       = std::format("g{}", i);
            graphIds_[graphs_[i]]                      = id;
            graphIdByValue_[toValue(graphs_[i]->id())] = id;
        }
        typeEntries_.push_back(json{{"k", "none"}});
        typeIndex_[nullptr] = 0;
        indexNodes();
        collectTags();
        collectTypes();
    }

    std::string dump() {
        // The graphs first: writing them is what settles the type and declaration tables, so
        // the tables that are copied into the document below are final.
        json graphs = json::array();
        for (GCGraph *graph : graphs_)
            graphs.push_back(graphJson(graph));
        json decls = json::array();
        for (const auto &decl : declEntries_)
            decls.push_back(decl);
        json tags = json::array();
        for (const auto &tag : tagNameToId_)
            tags.push_back(tag.second);
        json document{
            {"format", "camel.gir"},
            {"version", 2},
            {"canonical", true},
            {"root", graphIds_.at(root_)},
            {"entry", entryGraphJson()},
            {"decls", std::move(decls)},
            {"tags", std::move(tags)},
            {"types", typeEntries_},
            {"graphs", std::move(graphs)}};
        return document.dump(2);
    }

  private:
    const Program &program_;
    GCGraph *root_;
    std::vector<GCGraph *> graphs_;
    std::unordered_map<const GCGraph *, std::string> graphIds_;
    std::unordered_map<uint32_t, std::string> graphIdByValue_;

    std::vector<json> typeEntries_;
    std::unordered_map<const type::Type *, size_t> typeIndex_;
    /// Type objects with the same structure are one entry: parameter names are not part of a
    /// program (plan 2.2 step 4), but the front end interns a function type by its parameter
    /// names, so identity alone would make the table depend on them.
    std::unordered_map<std::string, size_t> typeIndexByKey_;
    std::vector<json> declEntries_;
    std::unordered_map<const void *, std::string> declIds_;

    /// Tag key names in the order they are first met, with the atom each one is written as.
    std::vector<std::pair<std::string, std::string>> tagNameToId_;
    std::unordered_map<std::string, std::string> tagIdOf_;

    std::unordered_map<const GCGraph *, std::unordered_map<gc_node_ref_t, std::string>> nodeIds_;
    std::unordered_map<const GCGraph *, std::unordered_map<uint32_t, uint32_t>> canonicalRegions_;

    // ---- nodes and regions ----------------------------------------------------------------

    void indexNodes() {
        for (GCGraph *graph : graphs_) {
            auto &ids   = nodeIds_[graph];
            size_t next = 0;
            for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it)
                ids[it.ref()] = std::format("n{}", next++);
        }
    }

    const std::string *nodeIdOf(const GCGraph *graph, gc_node_ref_t ref) const {
        auto at = nodeIds_.find(graph);
        if (at == nodeIds_.end())
            return nullptr;
        auto found = at->second.find(ref);
        return found == at->second.end() ? nullptr : &found->second;
    }

    /// The canonical region of an original region: annotation regions (Inline, Block) are not
    /// part of equivalence, so a node or region inside one belongs to the nearest ancestor that
    /// is. 0 is the body.
    uint32_t canonicalRegionOf(const GCGraph *graph, uint32_t region) const {
        auto at = canonicalRegions_.find(graph);
        if (at == canonicalRegions_.end())
            return 0;
        const auto &map    = at->second;
        uint32_t at_region = region;
        while (at_region != 0) {
            auto found = map.find(at_region);
            if (found != map.end())
                return found->second;
            const GCRegion *record = graph->region(at_region);
            if (record == nullptr)
                return 0;
            at_region = record->parent;
        }
        return 0;
    }

    void buildCanonicalRegions(GCGraph *graph) {
        auto &map     = canonicalRegions_[graph];
        uint32_t next = 1;
        for (uint32_t r = 1; r < graph->regionCount(); ++r) {
            const GCRegion *region = graph->region(r);
            if (region == nullptr || region->kind == GCRegionKind::Dead ||
                region->kind == GCRegionKind::Inline || region->kind == GCRegionKind::Block)
                continue;
            map[r] = next++;
        }
    }

    // ---- tags -----------------------------------------------------------------------------

    void noteTags(uint32_t attrs) {
        for (const auto &entry : attr::entries(attrs)) {
            const std::string name(attr::keyName(entry.key));
            if (tagIdOf_.contains(name))
                continue;
            const std::string id = std::format("t{}", tagNameToId_.size());
            tagIdOf_[name]       = id;
            tagNameToId_.emplace_back(name, id);
        }
    }

    void collectTags() {
        for (GCGraph *graph : graphs_) {
            for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it)
                noteTags(graph->nodeAttrs(it.ref()));
            for (uint32_t r = 1; r < graph->regionCount(); ++r) {
                const GCRegion *region = graph->region(r);
                if (region != nullptr && region->kind != GCRegionKind::Dead)
                    noteTags(region->attrs);
            }
        }
    }

    json tagValueJson(attr::AttrValueId id) {
        const attr::Value value = attr::value(id);
        if (value.kind == attr::ValueKind::Unit)
            return true;
        if (value.kind == attr::ValueKind::Graph) {
            // The value names a graph of the program; a reader must compare the graph, not the
            // process-local number it was stored as.
            try {
                const auto number = static_cast<uint32_t>(std::stoul(value.text));
                auto found        = graphIdByValue_.find(number);
                if (found != graphIdByValue_.end())
                    return json{{"graph", found->second}};
            } catch (...) {
            }
            return json{{"graph", nullptr}};
        }
        // valueText is process-independent by construction (names, never ids).
        return attr::valueText(id);
    }

    /// The attributes of a set as an object from tag atom to value (keys sort deterministically).
    json attrsJson(uint32_t attrs) {
        json out = json::object();
        for (const auto &entry : attr::entries(attrs)) {
            auto found = tagIdOf_.find(std::string(attr::keyName(entry.key)));
            if (found != tagIdOf_.end())
                out[found->second] = tagValueJson(entry.value);
        }
        return out;
    }

    // ---- types ----------------------------------------------------------------------------

    std::string declareType(const std::string &kind, uint32_t params) {
        const std::string id = std::format("d{}", declEntries_.size());
        declEntries_.push_back(json{{"id", id}, {"kind", kind}, {"params", params}});
        return id;
    }

    std::string structDeclId(type::StructDecl *decl) {
        auto found = declIds_.find(decl);
        if (found != declIds_.end())
            return found->second;
        const std::string id = declareType("struct", static_cast<uint32_t>(decl->params().size()));
        declIds_[decl]       = id;
        return id;
    }

    std::string enumDeclId(type::EnumDecl *decl) {
        auto found = declIds_.find(decl);
        if (found != declIds_.end())
            return found->second;
        const std::string id = declareType("enum", static_cast<uint32_t>(decl->params().size()));
        declIds_[decl]       = id;
        json variants        = json::array();
        for (const type::EnumVariant &variant : decl->variants()) {
            std::string shape = "unit";
            if (variant.shape == type::VariantShape::Tuple)
                shape = "tuple";
            else if (variant.shape == type::VariantShape::Struct)
                shape = "struct";
            variants.push_back(
                json{{"name", variant.name}, {"shape", shape}, {"fields", variant.fieldNames}});
        }
        declEntries_.back()["variants"] = std::move(variants);
        return id;
    }

    /// The structure of a type, without its table index: two types with the same key are one entry.
    /// A cycle back to an ancestor is `^depth`, so a recursive declaration has a finite key.
    std::string typeKey(type::Type *t, std::vector<type::Type *> &stack) {
        using type::TypeCode;
        if (t == nullptr)
            return "0";
        for (size_t i = 0; i < stack.size(); ++i) {
            if (stack[i] == t)
                return std::format("^{}", stack.size() - 1 - i);
        }
        stack.push_back(t);
        const auto sub = [&](type::Type *inner) { return typeKey(inner, stack); };
        std::string key;
        switch (t->code()) {
        case TypeCode::Int32:
        case TypeCode::Int64:
        case TypeCode::Float32:
        case TypeCode::Float64:
        case TypeCode::Bool:
        case TypeCode::Void:
        case TypeCode::String:
        case TypeCode::Complex64:
        case TypeCode::Complex128:
        case TypeCode::Ref:
            key = std::format("p:{}", type::typeCodeToString(t->code()));
            break;
        case TypeCode::Array:
            key = std::format("a:{}", sub(static_cast<type::ArrayType *>(t)->elemType()));
            break;
        case TypeCode::Tuple: {
            auto *tuple = static_cast<type::TupleType *>(t);
            key         = std::format(
                "t:{}",
                tuple->isCell()       ? "cell"
                : tuple->isNullable() ? "nullable"
                : tuple->isFallible() ? "fallible"
                                      : "plain");
            for (size_t i = 0; i < tuple->size(); ++i)
                key += std::format(",{}", sub(tuple->typeAt(i)));
            break;
        }
        case TypeCode::Struct: {
            auto *record = static_cast<type::StructType *>(t);
            key          = record->named() ? "N" + structDeclId(record->decl()) : std::string("S");
            for (type::Type *arg : record->typeArgs())
                key += std::format(",{}", sub(arg));
            for (size_t i = 0; i < record->size(); ++i)
                key += std::format(",{}={}", record->fieldName(i), sub(record->typeAt(i)));
            break;
        }
        case TypeCode::Function: {
            auto *function = static_cast<type::FunctionType *>(t);
            key            = std::format(
                "f:{}",
                function->hasRest());
            for (size_t i = 0; i < function->withTypesCount(); ++i)
                key +=
                    std::format(",w{}:{}", function->withIsVarAt(i), sub(function->withTypeAt(i)));
            for (size_t i = 0; i < function->normTypesCount(); ++i)
                key +=
                    std::format(",n{}:{}", function->normIsVarAt(i), sub(function->normTypeAt(i)));
            key +=
                std::format(",e:{}", sub(function->hasExitType() ? function->exitType() : nullptr));
            break;
        }
        default:
            break;
        }
        if (!key.empty()) {
            stack.pop_back();
            return key;
        }
        if (auto *enumType = type::asEnumType(t)) {
            key = "e" + enumDeclId(enumType->decl());
            for (type::Type *arg : enumType->paramsSpan())
                key += std::format(",{}", sub(arg));
            if (enumType->complete() && !enumType->pending()) {
                for (const type::EnumVariant &variant : enumType->variants()) {
                    key += "|";
                    for (type::Type *fieldType : variant.fieldTypes)
                        key += std::format(",{}", sub(fieldType));
                }
            }
            stack.pop_back();
            return key;
        }
        if (const auto *collection = type::asCollectionType(t)) {
            key = collection->isMap() ? std::format(
                                            "m:{},{}",
                                            sub(collection->keyType()),
                                            sub(collection->valueType()))
                                      : std::format("s:{}", sub(collection->keyType()));
            stack.pop_back();
            return key;
        }
        if (type::isBytesType(t)) {
            stack.pop_back();
            return "b";
        }
        key = std::format(
            "o:{}:{}",
            static_cast<uint32_t>(t->code()),
            type::sourceTypeSpelling(t->toString()));
        stack.pop_back();
        return key;
    }

    size_t addType(type::Type *t) {
        if (t == nullptr)
            return 0;
        auto found = typeIndex_.find(t);
        if (found != typeIndex_.end())
            return found->second;
        std::vector<type::Type *> stack;
        const std::string key = typeKey(t, stack);
        auto same             = typeIndexByKey_.find(key);
        if (same != typeIndexByKey_.end()) {
            typeIndex_[t] = same->second;
            return same->second;
        }
        const size_t index   = typeEntries_.size();
        typeIndex_[t]        = index;
        typeIndexByKey_[key] = index;
        typeEntries_.push_back(json::object());
        typeEntries_[index] = typeJson(t);
        return index;
    }

    json typeJson(type::Type *t) {
        using type::TypeCode;
        switch (t->code()) {
        case TypeCode::Int32:
        case TypeCode::Int64:
        case TypeCode::Float32:
        case TypeCode::Float64:
        case TypeCode::Bool:
        case TypeCode::Void:
        case TypeCode::String:
        case TypeCode::Complex64:
        case TypeCode::Complex128:
        case TypeCode::Ref:
            return json{{"k", type::typeCodeToString(t->code())}};
        case TypeCode::Array: {
            auto *array = static_cast<type::ArrayType *>(t);
            return json{{"k", "array"}, {"elem", addType(array->elemType())}};
        }
        case TypeCode::Tuple: {
            auto *tuple     = static_cast<type::TupleType *>(t);
            std::string tag = "plain";
            if (tuple->isCell())
                tag = "cell";
            else if (tuple->isNullable())
                tag = "nullable";
            else if (tuple->isFallible())
                tag = "fallible";
            json elems = json::array();
            for (size_t i = 0; i < tuple->size(); ++i)
                elems.push_back(addType(tuple->typeAt(i)));
            return json{{"k", "tuple"}, {"tag", tag}, {"elems", std::move(elems)}};
        }
        case TypeCode::Struct: {
            auto *record = static_cast<type::StructType *>(t);
            json fields  = json::array();
            for (size_t i = 0; i < record->size(); ++i) {
                fields.push_back(
                    json{
                        {"name", std::string(record->fieldName(i))},
                        {"type", addType(record->typeAt(i))}});
            }
            if (record->named()) {
                json args = json::array();
                for (type::Type *arg : record->typeArgs())
                    args.push_back(addType(arg));
                return json{
                    {"k", "struct"},
                    {"decl", structDeclId(record->decl())},
                    {"args", std::move(args)},
                    {"fields", std::move(fields)}};
            }
            return json{{"k", "struct"}, {"fields", std::move(fields)}};
        }
        case TypeCode::Function: {
            auto *function = static_cast<type::FunctionType *>(t);
            json with      = json::array();
            json norm      = json::array();
            for (size_t i = 0; i < function->withTypesCount(); ++i)
                with.push_back(
                    json{
                        {"type", addType(function->withTypeAt(i))},
                        {"var", function->withIsVarAt(i)}});
            for (size_t i = 0; i < function->normTypesCount(); ++i)
                norm.push_back(
                    json{
                        {"type", addType(function->normTypeAt(i))},
                        {"var", function->normIsVarAt(i)}});
            return json{
                {"k", "func"},
                {"rest", function->hasRest()},
                {"with", std::move(with)},
                {"norm", std::move(norm)},
                {"exit", addType(function->hasExitType() ? function->exitType() : nullptr)}};
        }
        default:
            break;
        }
        if (auto *enumType = type::asEnumType(t)) {
            json args = json::array();
            for (type::Type *arg : enumType->paramsSpan())
                args.push_back(addType(arg));
            json variants = json::array();
            if (enumType->complete() && !enumType->pending()) {
                for (const type::EnumVariant &variant : enumType->variants()) {
                    json fields = json::array();
                    for (type::Type *fieldType : variant.fieldTypes)
                        fields.push_back(addType(fieldType));
                    variants.push_back(std::move(fields));
                }
            }
            return json{
                {"k", "enum"},
                {"decl", enumDeclId(enumType->decl())},
                {"args", std::move(args)},
                {"variants", std::move(variants)}};
        }
        if (const auto *collection = type::asCollectionType(t)) {
            json entry{{"k", collection->isMap() ? "map" : "set"}};
            entry["key"] = addType(collection->keyType());
            if (collection->isMap())
                entry["value"] = addType(collection->valueType());
            return entry;
        }
        if (type::isBytesType(t))
            return json{{"k", "bytes"}};
        // A type the core does not know by structure (a module's): its spelling is the only
        // stable description; no named declaration of the program is spelled into it.
        return json{
            {"k", "other"},
            {"code", static_cast<uint32_t>(t->code())},
            {"spelling", type::sourceTypeSpelling(t->toString())}};
    }

    void collectTypes() {
        for (GCGraph *graph : graphs_) {
            addType(graph->funcType());
            for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it)
                addType(camel::passes::gir::nodeStaticSlotType(graph, **it));
            for (uint32_t r = 1; r < graph->regionCount(); ++r) {
                const GCRegion *region = graph->region(r);
                if (region == nullptr || region->kind == GCRegionKind::Dead ||
                    region->params == nullptr)
                    continue;
                for (size_t i = 0; i < region->params->withTypesCount(); ++i)
                    addType(region->params->withTypeAt(i));
                for (size_t i = 0; i < region->params->normTypesCount(); ++i)
                    addType(region->params->normTypeAt(i));
            }
        }
    }

    // ---- values ---------------------------------------------------------------------------

    /// A static value by value, not by spelling: numbers by their bits, strings by their bytes,
    /// aggregates element by element, a function constant by the graph it names.
    json valueJson(type::Type *t, slot_t slot, int depth) {
        if (t == nullptr)
            return json{{"k", "none"}};
        if (depth > kMaxValueDepth)
            return json{{"k", "deep"}};
        using type::TypeCode;
        if (!t->isGCTraced()) {
            switch (t->code()) {
            case TypeCode::Int32:
                return json{
                    {"k", "i32"},
                    {"v", static_cast<int64_t>(camel::core::rtdata::fromSlot<int32_t>(slot))}};
            case TypeCode::Int64:
                return json{
                    {"k", "i64"},
                    {"v", static_cast<int64_t>(camel::core::rtdata::fromSlot<int64_t>(slot))}};
            case TypeCode::Float32:
                return json{
                    {"k", "f32"},
                    {"bits",
                     hex64(std::bit_cast<uint32_t>(camel::core::rtdata::fromSlot<float>(slot)))}};
            case TypeCode::Float64:
                return json{
                    {"k", "f64"},
                    {"bits",
                     hex64(std::bit_cast<uint64_t>(camel::core::rtdata::fromSlot<double>(slot)))}};
            case TypeCode::Bool:
                return json{{"k", "bool"}, {"v", camel::core::rtdata::fromSlot<bool>(slot)}};
            case TypeCode::Void:
                return json{{"k", "void"}};
            default:
                return json{{"k", "scalar"}, {"bits", hex64(slot)}};
            }
        }
        if (slot == NullSlot)
            return json{{"k", "null"}};

        switch (t->code()) {
        case TypeCode::String: {
            const auto *text = camel::core::rtdata::fromSlot<const ::String *>(slot);
            if (text == nullptr)
                return json{{"k", "null"}};
            json bytes = json::array();
            for (char c : text->view())
                bytes.push_back(static_cast<unsigned char>(c));
            return json{{"k", "string"}, {"bytes", std::move(bytes)}};
        }
        case TypeCode::Array: {
            const auto *array = camel::core::rtdata::fromSlot<const ::Array *>(slot);
            auto *arrayType   = static_cast<type::ArrayType *>(t);
            if (array == nullptr)
                return json{{"k", "null"}};
            json elems = json::array();
            for (size_t i = 0; i < array->size(); ++i)
                elems.push_back(valueJson(arrayType->elemType(), array->getSlot(i), depth + 1));
            return json{{"k", "array"}, {"elems", std::move(elems)}};
        }
        case TypeCode::Tuple: {
            auto *tupleType = static_cast<type::TupleType *>(t);
            if (tupleType->isNullable()) {
                if (camel::core::rtdata::isNullValue(slot))
                    return json{{"k", "null"}};
                return json{
                    {"k", "nullable"},
                    {"value",
                     valueJson(
                         tupleType->payloadType(),
                         camel::core::rtdata::nullablePayload(slot),
                         depth + 1)}};
            }
            if (tupleType->isFallible()) {
                if (camel::core::rtdata::isErrorValue(slot)) {
                    return json{
                        {"k", "failed"},
                        {"error",
                         valueJson(
                             type::errorType(),
                             camel::core::rtdata::fallibleError(slot),
                             depth + 1)}};
                }
                return json{
                    {"k", "ok"},
                    {"value",
                     valueJson(
                         tupleType->payloadType(),
                         camel::core::rtdata::falliblePayload(slot),
                         depth + 1)}};
            }
            const auto *tuple = camel::core::rtdata::fromSlot<const ::Tuple *>(slot);
            if (tuple == nullptr)
                return json{{"k", "null"}};
            json elems = json::array();
            for (size_t i = 0; i < tupleType->size(); ++i)
                elems.push_back(valueJson(tupleType->typeAt(i), tuple->slotAt(i), depth + 1));
            return json{{"k", "tuple"}, {"elems", std::move(elems)}};
        }
        case TypeCode::Struct: {
            auto *structType   = static_cast<type::StructType *>(t);
            const auto *record = camel::core::rtdata::fromSlot<const ::Struct *>(slot);
            if (record == nullptr)
                return json{{"k", "null"}};
            json fields = json::array();
            for (size_t i = 0; i < structType->size(); ++i) {
                fields.push_back(
                    json{
                        {"name", std::string(structType->fieldName(i))},
                        {"value", valueJson(structType->typeAt(i), record->slotAt(i), depth + 1)}});
            }
            return json{{"k", "struct"}, {"fields", std::move(fields)}};
        }
        default:
            break;
        }

        if (const auto *enumType = type::asEnumType(t)) {
            const auto *variant =
                camel::core::rtdata::fromSlot<const camel::core::rtdata::Variant *>(slot);
            if (variant == nullptr)
                return json{{"k", "null"}};
            const size_t tag = variant->tag();
            json fields      = json::array();
            if (tag < enumType->variantCount()) {
                const type::EnumVariant &record = enumType->variant(tag);
                for (size_t i = 0; i < record.fieldTypes.size() && i < variant->fieldCount(); ++i)
                    fields.push_back(valueJson(record.fieldTypes[i], variant->get(i), depth + 1));
            }
            return json{{"k", "enum"}, {"tag", tag}, {"fields", std::move(fields)}};
        }

        if (const auto *collection = type::asCollectionType(t)) {
            const auto *table =
                camel::core::rtdata::fromSlot<const camel::core::rtdata::Table *>(slot);
            if (table == nullptr)
                return json{{"k", "null"}};
            // The shape of the tree is not observable (collection.h), so the entries are read
            // out and sorted: two dumps of one collection must not depend on it.
            std::vector<json> entries;
            std::vector<const camel::core::rtdata::Table *> work{table};
            while (!work.empty()) {
                const auto *leaf = work.back();
                work.pop_back();
                if (leaf == nullptr)
                    continue;
                if (!leaf->isLeaf()) {
                    for (size_t i = 0; i < leaf->count(); ++i)
                        work.push_back(leaf->children()[i]);
                    continue;
                }
                for (size_t i = 0; i < leaf->size(); ++i) {
                    json entry{
                        {"key", valueJson(collection->keyType(), leaf->keys()[i], depth + 1)}};
                    if (collection->isMap())
                        entry["value"] =
                            valueJson(collection->valueType(), leaf->values()[i], depth + 1);
                    entries.push_back(std::move(entry));
                }
            }
            std::sort(entries.begin(), entries.end(), [](const json &a, const json &b) {
                return a.dump() < b.dump();
            });
            return json{
                {"k", collection->isMap() ? "map" : "set"},
                {"entries", std::move(entries)}};
        }

        if (t->code() == type::TypeCode::Function) {
            const auto *function = camel::core::rtdata::fromSlot<const ::Function *>(slot);
            if (function == nullptr)
                return json{{"k", "null"}};
            const GCGraph *graph = function->graph();
            auto found           = graphIds_.find(graph);
            return json{
                {"k", "func"},
                {"graph", found == graphIds_.end() ? json(nullptr) : json(found->second)}};
        }

        // A value the core does not know by structure: its printed text is the stable part.
        std::ostringstream text;
        camel::core::rtdata::printSlot(text, slot, t);
        return json{{"k", "opaque"}, {"text", text.str()}};
    }

    // ---- nodes ----------------------------------------------------------------------------

    json inputsJson(GCGraph *graph, gc_node_ref_t ref) {
        json inputs    = json::array();
        const auto add = [&](const char *kind, auto values) {
            for (size_t i = 0; i < values.size(); ++i) {
                const std::string *source = nodeIdOf(graph, values[i]);
                inputs.push_back(
                    json{
                        {"kind", kind},
                        {"slot", i},
                        {"source", source == nullptr ? json(nullptr) : json(*source)}});
            }
        };
        add("normal", graph->normInputsOf(ref));
        add("context", graph->withInputsOf(ref));
        add("control", graph->ctrlInputsOf(ref));
        return inputs;
    }

    json payloadJson(GCGraph *graph, gc_node_ref_t ref, const GCNode &node) {
        switch (node.kind) {
        case GCNodeKind::Fill: {
            const auto *body = graph->nodeBodyAs<GCFillBody>(ref);
            if (body == nullptr)
                return nullptr;
            json slots = json::array();
            for (gc_slot_idx_t slot : body->slots())
                slots.push_back(slot);
            return json{{"fill", fillKindName(body->fillKind)}, {"slots", std::move(slots)}};
        }
        case GCNodeKind::Accs: {
            const auto *body = graph->nodeBodyAs<GCAccsBody>(ref);
            if (body == nullptr)
                return nullptr;
            if (body->accsKind == GCAccsKind::StructKey)
                return json{{"accs", "struct_key"}, {"key", std::string(body->key())}};
            return json{{"accs", "tuple_index"}, {"index", body->value}};
        }
        case GCNodeKind::Brch: {
            const auto *body = graph->nodeBodyAs<GCBrchBody>(ref);
            if (body == nullptr)
                return nullptr;
            const std::string *join = nodeIdOf(graph, body->join);
            return json{
                {"arms", body->armCount},
                {"pair", join == nullptr ? json(nullptr) : json(*join)}};
        }
        case GCNodeKind::Join: {
            const auto *body = graph->nodeBodyAs<GCJoinBody>(ref);
            if (body == nullptr)
                return nullptr;
            const std::string *brch = nodeIdOf(graph, body->brch);
            return json{
                {"arms", body->armCount},
                {"pair", brch == nullptr ? json(nullptr) : json(*brch)}};
        }
        case GCNodeKind::Call:
            return json{{"call", "indirect"}};
        case GCNodeKind::Oper: {
            const auto *body = graph->nodeBodyAs<GCOperBody>(ref);
            if (body == nullptr)
                return nullptr;
            const auto *semantics = camel::core::OperatorTable::instance().semantics(body->oper);
            return json{
                {"op",
                 semantics != nullptr ? semantics->key.toString() : std::string(body->uri())}};
        }
        default:
            return nullptr;
        }
    }

    json loopJson(GCGraph *graph, gc_node_ref_t ref) {
        json loop{
            {"kind", camel::core::loopKindName(graph->loopKindOf(ref))},
            {"collectionCount", graph->loopCollectionCountOf(ref)}};
        // Where the elements come from, when it is not an array: the interval of `for i in a..b`,
        // which the compiler fuses (design principles 4.2, LoopIterableForm::Interval). An array
        // is what every other loop iterates, and is left out as the other defaults are.
        if (graph->loopIterableFormOf(ref) == camel::core::LoopIterableForm::Interval) {
            loop["iterable"] = "interval";
        }
        const GCGraph *condition = program_.loopCondOf(*graph, ref);
        const GCGraph *body      = program_.loopBodyOf(*graph, ref);
        loop["condition"] = condition == nullptr ? json(nullptr) : json(graphIds_.at(condition));
        loop["body"]      = body == nullptr ? json(nullptr) : json(graphIds_.at(body));
        const uint32_t conditionRegion = graph->loopRegionOf(ref, GCRegionKind::LoopCond);
        const uint32_t bodyRegion      = graph->loopRegionOf(ref, GCRegionKind::LoopBody);
        if (conditionRegion != 0)
            loop["conditionRegion"] = canonicalRegionOf(graph, conditionRegion);
        if (bodyRegion != 0)
            loop["bodyRegion"] = canonicalRegionOf(graph, bodyRegion);
        return loop;
    }

    json nodeJson(GCGraph *graph, gc_node_ref_t ref, const GCNode &node) {
        json value{
            {"id", nodeIdOf(graph, ref) ? *nodeIdOf(graph, ref) : std::string()},
            {"kind", camel::passes::gir::recordType(node)},
            {"type", addType(camel::passes::gir::nodeStaticSlotType(graph, node))},
            {"inputs", inputsJson(graph, ref)}};
        const uint32_t region = canonicalRegionOf(graph, graph->nodeRegion(ref));
        if (region != 0)
            value["region"] = region;
        const auto facts = camel::core::tag::factsOf(*graph, ref);
        if (facts.tags != attr::kNoAttrs)
            value["attrs"] = attrsJson(facts.tags);
        if (facts.permissions != 0)
            value["perms"] = facts.permissions;
        switch (node.kind) {
        case GCNodeKind::Port: {
            const auto info = camel::passes::gir::portInfo(graph, ref);
            value["param"]  = json{{"kind", info.kind}, {"index", info.index}};
            break;
        }
        case GCNodeKind::Func: {
            const GCGraph *callee = program_.calleeOf(*graph, ref);
            value["callee"]       = callee == nullptr ? json(nullptr) : json(graphIds_.at(callee));
            break;
        }
        case GCNodeKind::Loop:
            value["loop"] = loopJson(graph, ref);
            break;
        case GCNodeKind::Data: {
            if (node.dataIndex < 0) {
                auto *type         = camel::passes::gir::nodeStaticSlotType(graph, node);
                const auto slots   = graph->staticSlots();
                const size_t index = static_cast<size_t>(-node.dataIndex);
                if (index < slots.size())
                    value["value"] = valueJson(type, slots[index], 0);
                if (type != nullptr && type->code() == type::TypeCode::Function) {
                    const auto *function =
                        camel::core::rtdata::fromSlot<const ::Function *>(slots[index]);
                    if (function != nullptr) {
                        const GCGraph *callee = function->graph();
                        auto found            = graphIds_.find(callee);
                        if (found != graphIds_.end())
                            value["callee"] = found->second;
                    }
                }
            }
            break;
        }
        default:
            break;
        }
        json payload = payloadJson(graph, ref, node);
        if (!payload.is_null())
            value["payload"] = std::move(payload);
        return value;
    }

    // ---- regions --------------------------------------------------------------------------

    json regionJson(GCGraph *graph, uint32_t id, uint32_t original, const GCRegion &region) {
        json value{{"id", id}, {"kind", regionKindName(region.kind)}, {"index", region.index}};
        value["parent"] = canonicalRegionOf(graph, region.parent);
        if (region.owner != kInvalidNodeRef) {
            if (const std::string *owner = nodeIdOf(graph, region.owner))
                value["owner"] = *owner;
        }
        if (region.result != kInvalidNodeRef) {
            if (const std::string *result = nodeIdOf(graph, region.result))
                value["result"] = *result;
        }
        json ports = json::array();
        for (gc_node_ref_t port : graph->regionPorts(original)) {
            const std::string *node = nodeIdOf(graph, port);
            ports.push_back(node == nullptr ? json(nullptr) : json(*node));
        }
        if (!ports.empty())
            value["ports"] = std::move(ports);
        if (region.params != nullptr) {
            json with = json::array();
            json norm = json::array();
            for (size_t i = 0; i < region.params->withTypesCount(); ++i)
                with.push_back(addType(region.params->withTypeAt(i)));
            for (size_t i = 0; i < region.params->normTypesCount(); ++i)
                norm.push_back(addType(region.params->normTypeAt(i)));
            value["params"] = json{{"with", std::move(with)}, {"norm", std::move(norm)}};
        }
        if (region.attrs != attr::kNoAttrs)
            value["attrs"] = attrsJson(region.attrs);
        return value;
    }

    // ---- graphs ---------------------------------------------------------------------------

    /// The graph the root's entry call names: a best-effort read of the exit chain (S0 keeps the
    /// entry chain instead of hoisting it; see the file comment).
    json entryGraphJson() {
        if (root_ == nullptr)
            return nullptr;
        const auto nodeId = root_->exitNodeRef();
        if (nodeId == kInvalidNodeRef)
            return nullptr;
        std::vector<gc_node_ref_t> work{nodeId};
        std::unordered_map<gc_node_ref_t, bool> seen;
        while (!work.empty()) {
            const gc_node_ref_t ref = work.back();
            work.pop_back();
            if (!seen.emplace(ref, true).second)
                continue;
            const GCNode *node = root_->node(ref);
            if (node == nullptr || node->kind == GCNodeKind::Dead)
                continue;
            if (node->kind == GCNodeKind::Call) {
                std::vector<gc_node_ref_t> callInputs;
                for (gc_node_ref_t input : root_->normInputsOf(ref))
                    callInputs.push_back(input);
                // A function value reaches a call as a context input when it is the callee of a
                // plain call (`main()` reads no capture, so it arrives as a with input).
                for (gc_node_ref_t input : root_->withInputsOf(ref))
                    callInputs.push_back(input);
                for (gc_node_ref_t input : callInputs) {
                    const GCNode *source = root_->node(input);
                    if (source == nullptr)
                        continue;
                    if (source->kind == GCNodeKind::Data && source->dataIndex < 0) {
                        // Only a slot typed as a function names a graph. The walk from the exit
                        // crosses every node that carries the entry chain, and the static area
                        // holds the ordinary constants as well (a call's literal argument is a
                        // Data node with a negative index), so the type has to be asked for the
                        // way version 1 asks: a slot that is not a function is not a callee.
                        const auto *slotType =
                            camel::passes::gir::nodeStaticSlotType(root_, *source);
                        if (slotType == nullptr || slotType->code() != type::TypeCode::Function)
                            continue;
                        const auto slots   = root_->staticSlots();
                        const size_t index = static_cast<size_t>(-source->dataIndex);
                        if (index >= slots.size())
                            continue;
                        const auto *function =
                            camel::core::rtdata::fromSlot<const ::Function *>(slots[index]);
                        if (function == nullptr)
                            continue;
                        auto found = graphIds_.find(function->graph());
                        if (found != graphIds_.end())
                            return found->second;
                    }
                }
            }
            for (gc_node_ref_t input : root_->normInputsOf(ref))
                work.push_back(input);
            for (gc_node_ref_t input : root_->withInputsOf(ref))
                work.push_back(input);
            for (gc_node_ref_t input : root_->ctrlInputsOf(ref))
                work.push_back(input);
        }
        return nullptr;
    }

    json graphJson(GCGraph *graph) {
        buildCanonicalRegions(graph);
        json value{
            {"id", graphIds_.at(graph)},
            {"flags", graph->flags()},
            {"signature", addType(graph->funcType())},
            {"nodes", json::array()},
            {"regions", json::array()},
            {"normalPorts", json::array()},
            {"contextPorts", json::array()},
            {"capturePorts", json::array()}};
        if (graph == root_)
            value["root"] = true;
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            const auto ref   = it.ref();
            const auto *node = *it;
            if (node == nullptr)
                continue;
            value["nodes"].push_back(nodeJson(graph, ref, *node));
        }
        for (uint32_t r = 1; r < graph->regionCount(); ++r) {
            const GCRegion *region = graph->region(r);
            if (region == nullptr || region->kind == GCRegionKind::Dead ||
                region->kind == GCRegionKind::Inline || region->kind == GCRegionKind::Block)
                continue;
            value["regions"].push_back(regionJson(graph, canonicalRegionOf(graph, r), r, *region));
        }
        const auto pushPorts = [&](const char *field, auto ports) {
            for (gc_node_ref_t port : ports) {
                const std::string *node = nodeIdOf(graph, port);
                value[field].push_back(node == nullptr ? json(nullptr) : json(*node));
            }
        };
        pushPorts("normalPorts", graph->normPorts());
        pushPorts("contextPorts", graph->withPorts());
        pushPorts("capturePorts", graph->closureNodes());
        const gc_node_ref_t exit = graph->exitNodeRef();
        if (const std::string *node = nodeIdOf(graph, exit))
            value["exit"] = *node;
        else
            value["exit"] = nullptr;
        return value;
    }
};

} // namespace

std::string dumpCanonicalRuntimeGraph(const Program &program, GCGraph *root) {
    if (!root || !root->hasNodePayload())
        return {};
    return CanonicalDocument(program, root).dump();
}