// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The free functions the GIR builder is written in terms of: the helper functions that were
 * local to builder.cpp, shared by the source files the builder is now split into. They are
 * inline because every one of those files may use them; nothing outside src/compile/gir
 * includes this header.
 *
 * Include it after builder.h: the signatures name the aliases that header declares
 * (graph_set_ptr_t and the rest).
 */

#pragma once

#include "builder_support.h"

#include "camel/compile/gct.h"
#include "camel/compile/gir/static_function.h"
#include "camel/core/numeric_convert.h"
#include "camel/core/operator_names.h"
#include "camel/core/operator_traits.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/other.h"
#include "camel/core/type/typevar.h"
#include "camel/execute/entry_args.h"
#include "camel/runtime/control.h"
#include "camel/runtime/graph.h"
#include "camel/utils/log.h"
#include "camel/utils/scope.h"
#include "camel/utils/str.h"
#include "camel/utils/type.h"
#include "core/module/entity_internal.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace camel::compile::gir {

// These helpers were written against the namespaces builder.cpp brought in at global scope;
// they are named here so that the header stands on its own inside the namespace.
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::data;
using namespace camel::core::type;

constexpr std::size_t kSourceContextExtraIndex = 3;

inline bool isVoidGraphExitType(Type *type) {
    return type == nullptr || type == Type::Void() || type->code() == TypeCode::Void;
}

inline runtime::GCFillKind fillKindForType(Type *type) {
    ASSERT(type != nullptr, "FILL type cannot be null.");
    switch (type->code()) {
    case TypeCode::Tuple:
        return runtime::GCFillKind::Tuple;
    case TypeCode::Array:
        return runtime::GCFillKind::Array;
    case TypeCode::Struct:
        return runtime::GCFillKind::Struct;
    case TypeCode::Function:
        return runtime::GCFillKind::FunctionClosure;
    default:
        ASSERT(false, std::format("Unsupported FILL type '{}'.", type->toString()));
        return runtime::GCFillKind::Tuple;
    }
}

inline void setGraphReturnFromResult(
    const compile_graph_ptr_t &graph, draft_node_ref_t outputNode, draft_node_ref_t valueNode,
    Type *resultType) {
    ASSERT(graph != nullptr, "Graph return setup requires a valid graph.");
    graph->draft().setOutputNode(outputNode);
    graph->draft().setExitNode(outputNode);
    if (isVoidGraphExitType(resultType)) {
        graph->draft().setReturnNode(runtime::kInvalidNodeRef, runtime::GCReturnKind::None);
        if (!graph->funcType()->hasExitType()) {
            graph->setExitType(Type::Void());
        }
        return;
    }

    graph->draft().setReturnNode(valueNode, runtime::GCReturnKind::Self);
    if (!graph->funcType()->hasExitType()) {
        graph->setExitType(resultType);
    }
}

inline camel::source::gir_draft_node_key_t draftDebugKey(node_handle_t node) {
    return node ? static_cast<camel::source::gir_draft_node_key_t>(node.id) : 0;
}

inline compile_graph_ptr_t
asCompileGraph(const camel::core::module::detail::EntityAccess::graph_handle_t &graph) {
    return graph;
}

inline graph_set_ptr_t
asCompileGraphSet(const camel::core::module::detail::EntityAccess::graph_set_ptr_t &graphs) {
    auto result = std::make_shared<graph_set_t>();
    if (!graphs) {
        return result;
    }
    result->reserve(graphs->size());
    for (const auto &graph : *graphs) {
        auto typed = asCompileGraph(graph);
        if (typed) {
            result->push_back(typed);
        }
    }
    return result;
}

inline graph_vec_ptr_t asCompileGraphVec(const DraftGraphBuilder::DrefTarget &target) {
    return std::get<graph_vec_ptr_t>(target);
}

inline camel::core::module::detail::EntityAccess::graph_handle_t
eraseCompileGraph(const compile_graph_ptr_t &graph) {
    return graph;
}

inline camel::core::module::detail::EntityAccess::graph_set_ptr_t
eraseCompileGraphSet(const graph_set_ptr_t &graphs) {
    auto erased = std::make_shared<camel::core::module::detail::EntityAccess::graph_set_t>();
    if (!graphs) {
        return erased;
    }
    erased->reserve(graphs->size());
    for (const auto &graph : *graphs) {
        erased->push_back(eraseCompileGraph(graph));
    }
    return erased;
}

inline camel::core::mm::IAllocator &staticFunctionAllocator(const compile_graph_ptr_t &ownerGraph) {
    return ownerGraph->arena()->allocator();
}

inline TupleType *currentClosureTupleType(const compile_graph_ptr_t &graph) {
    type_vec_t closureTypes;
    auto &draft = graph->draft();
    closureTypes.reserve(draft.closureNodes().size());
    for (draft_node_ref_t nodeId : draft.closureNodes()) {
        const auto *header = draft.header(nodeId);
        closureTypes.push_back(header ? header->dataType : nullptr);
    }
    return TupleType::create(std::move(closureTypes));
}

inline StaticFunction *createStaticFunction(
    const compile_graph_ptr_t &ownerGraph, const compile_graph_ptr_t &targetGraph) {
    ASSERT(
        targetGraph != nullptr,
        "Target graph is null when materializing compile-time function.");
    return StaticFunction::create(
        targetGraph,
        currentClosureTupleType(targetGraph),
        staticFunctionAllocator(ownerGraph));
}

/// Compile-time value of a node that is static data (possibly behind a COPY), else nullopt.
inline std::optional<slot_t> staticValueOf(node_handle_t node) {
    if (nodeIsKind(node, runtime::GCNodeKind::Copy)) {
        const auto inputs = normInputsOf(node);
        if (inputs.size() != 1) {
            return std::nullopt;
        }
        node = nodeDraftOf(node)->ref(inputs.front());
    }
    if (!nodeIsKind(node, runtime::GCNodeKind::Data) || nodeHeaderOf(node)->dataIndex >= 0) {
        return std::nullopt;
    }
    const auto slots = nodeDraftOf(node)->staticSlots();
    const auto index = static_cast<size_t>(-nodeHeaderOf(node)->dataIndex);
    return index < slots.size() ? std::optional<slot_t>(slots[index]) : std::nullopt;
}

/// The node behind a COPY of it (the argument of a call is copied when it is a constant).
inline node_handle_t throughCopy(node_handle_t node) {
    if (nodeIsKind(node, runtime::GCNodeKind::Copy)) {
        const auto inputs = normInputsOf(node);
        if (inputs.size() == 1) {
            return nodeDraftOf(node)->ref(inputs.front());
        }
    }
    return node;
}

/// A place where a value of type `have` goes where `want` is declared and only a checked
/// conversion (`as`) leads from one to the other: `have` leaves open what `want` fixes.
struct ConversionGap {
    std::string path; // "" for the value itself, ".0" / ".name" for an element of it
    Type *have;
    Type *want;
};

/// Collects the conversion gaps between `have` and `want`, through tuples and structs. False
/// when the types differ in any other way (then no conversion written with `as` helps). The
/// rule is the one of a CAST node: an Unsafe conversion into a module type is checked when it
/// runs (Type::castSlotFrom), every other conversion that is not Safe is refused.
inline bool collectConversionGaps(
    Type *want, Type *have, const std::string &path, std::vector<ConversionGap> &gaps) {
    if (want == nullptr || have == nullptr) {
        return false;
    }
    if (want == have || want->assignableFrom(have)) {
        return true;
    }
    if (want->isOtherType()) {
        if (want->castSafetyFrom(have) != CastSafety::Unsafe) {
            return false;
        }
        gaps.push_back({path, have, want});
        return true;
    }
    if (want->code() == TypeCode::Array && have->code() == TypeCode::Array) {
        // A tensor is an array (P4), so the conversion that refines one is an array's and not a
        // module type's: the relation lives in ArrayType::castSafetyFrom, which is Unsafe exactly
        // when the source leaves open what the target fixes (an unknown rank, extent or element
        // type) and the program writes `as` to have it checked when the conversion runs
        // (design principles 5.4: an array whose shape is unknown entering a position that
        // declares one is converted explicitly; camel/core/type/composite/array.cpp). The gap is
        // the whole value: one `as` reaches it, whatever is left open inside it. Without this
        // branch a call like `f(t)` with `t: Tensor` and `b: Tensor<[3]>` was refused as
        // "no matching function" with the overload list, which says less than the conversion to
        // write (ruling 2026-10-24 evening, h450 item 2).
        if (want->castSafetyFrom(have) != CastSafety::Unsafe) {
            return false;
        }
        gaps.push_back({path, have, want});
        return true;
    }
    if (want->code() == TypeCode::Tuple && have->code() == TypeCode::Tuple) {
        auto *w = tt::as_ptr<TupleType>(want);
        auto *h = tt::as_ptr<TupleType>(have);
        if (w->isCell() || h->isCell() || w->size() != h->size()) {
            return false;
        }
        for (size_t i = 0; i < w->size(); ++i) {
            if (!collectConversionGaps(
                    w->typeAt(i),
                    h->typeAt(i),
                    path + "." + std::to_string(i),
                    gaps)) {
                return false;
            }
        }
        return true;
    }
    if (want->code() == TypeCode::Struct && have->code() == TypeCode::Struct) {
        auto *w = tt::as_ptr<StructType>(want);
        auto *h = tt::as_ptr<StructType>(have);
        if (w->size() != h->size()) {
            return false;
        }
        for (size_t i = 0; i < w->size(); ++i) {
            const auto field = h->findField(w->fieldName(i));
            if (!field || !collectConversionGaps(
                              w->typeAt(i),
                              h->typeAt(*field),
                              path + "." + std::string(w->fieldName(i)),
                              gaps)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

/// How a program writes a type in a conversion.
inline std::string spellingOf(Type *type) {
    return type->isOtherType() ? static_cast<OtherType *>(type)->spelling() : type->toString();
}

/// The elements of `text` when it is a parenthesised list written on the spot, `(a, b)`.
inline std::vector<std::string> tupleElementTexts(const std::string &text) {
    std::vector<std::string> parts;
    if (text.size() < 2 || text.front() != '(' || text.back() != ')') {
        return parts;
    }
    int depth = 0;
    std::string current;
    for (size_t i = 1; i + 1 < text.size(); ++i) {
        const char c = text[i];
        if (c == '(' || c == '[' || c == '{' || c == '<') {
            ++depth;
        } else if (c == ')' || c == ']' || c == '}' || c == '>') {
            if (--depth < 0) {
                return {}; // "(a) + (b)": not one list
            }
        }
        if (c == ',' && depth == 0) {
            parts.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    parts.push_back(current);
    for (std::string &part : parts) {
        const auto first = part.find_first_not_of(' ');
        const auto last  = part.find_last_not_of(' ');
        part = first == std::string::npos ? std::string() : part.substr(first, last - first + 1);
        if (part.empty()) {
            return {};
        }
    }
    return parts;
}

/// The conversion a program would write for `gap` on the expression whose text is `text`.
inline std::string conversionTextOf(const std::string &text, const ConversionGap &gap) {
    std::string operand = text;
    if (!gap.path.empty()) {
        // An element of a tuple written on the spot is converted where it stands.
        const auto parts = tupleElementTexts(text);
        const bool index = gap.path.find_first_not_of("0123456789", 1) == std::string::npos;
        if (index && !parts.empty() && std::stoul(gap.path.substr(1)) < parts.size()) {
            operand = parts[std::stoul(gap.path.substr(1))];
        } else {
            operand = text + gap.path;
        }
    }
    // Only a name, an access or a call binds tighter than `as` for certain (a placeholder
    // such as <argument> stands for whatever the program wrote).
    bool simple = !operand.empty();
    if (!operand.empty() && operand.front() == '<' && operand.find('>') != std::string::npos) {
        return "'" + operand + " as " + spellingOf(gap.want) + "'";
    }
    int depth = 0;
    for (const char c : operand) {
        if (c == '(' || c == '[') {
            ++depth;
        } else if (c == ')' || c == ']') {
            --depth;
        } else if (
            depth == 0 && !(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.')) {
            simple = false;
        }
    }
    return "'" + (simple ? operand : "(" + operand + ")") + " as " + spellingOf(gap.want) + "'";
}

/// "it has type 'A', less exact than 'B'" for each gap, the subject naming the element.
inline std::string describeGaps(const std::string &subject, const std::vector<ConversionGap> &gaps) {
    std::string out;
    for (const ConversionGap &gap : gaps) {
        out += out.empty() ? "" : "; ";
        out += (gap.path.empty() ? subject : "element " + gap.path + " of " + subject) +
               " has type '" + gap.have->toString() + "', which is less exact than '" +
               gap.want->toString() + "'";
    }
    return out;
}

inline void setNodeType(node_handle_t node, Type *type) {
    ASSERT(node != nullptr, "Cannot update type of null draft node.");
    nodeDraftOf(node)->setNodeDataType(nodeIdOf(node), type);
}

inline void setNodeMacro(node_handle_t node, bool enabled) {
    ASSERT(node != nullptr, "Cannot update runtime flags of null draft node.");
    uint8_t flags = nodeHeaderOf(node)->runtimeFlags;
    if (enabled) {
        flags = static_cast<uint8_t>(flags | runtime::kGCNodeFlagMacro);
    } else {
        flags = static_cast<uint8_t>(flags & ~runtime::kGCNodeFlagMacro);
    }
    nodeDraftOf(node)->setNodeRuntimeFlags(nodeIdOf(node), flags);
}

inline compile_graph_ptr_t createCompileGraph(
    runtime::GraphId id, FunctionType *funcType, const compile_graph_ptr_t &outer,
    std::string name = "") {
    auto graph = std::make_shared<DraftGraphBuilder>(funcType ? funcType : FunctionType::create());
    graph->draft().setId(id);
    if (name.empty()) {
        name = std::format("__g_{}__", runtime::toValue(id));
    }
    graph->setName(std::move(name));
    if (outer) {
        graph->setOuterGraph(outer);
        outer->addSubGraph(graph);
    }
    if (funcType) {
        for (size_t i = 0; i < funcType->withTypesCount(); ++i) {
            const std::string portName = i < funcType->argNamesCount()
                                             ? std::string(funcType->argNameAt(i))
                                             : std::format("__w{}", i);
            const bool isVar           = funcType->withIsVarAt(i);
            Type *type                 = funcType->withTypeAt(i);
            graph->addPortNode(isVar ? TupleType::cell(type) : type, portName, true);
        }
        for (size_t i = 0; i < funcType->normTypesCount(); ++i) {
            const size_t argIndex      = funcType->withTypesCount() + i;
            const std::string portName = argIndex < funcType->argNamesCount()
                                             ? std::string(funcType->argNameAt(argIndex))
                                             : std::format("__n{}", i);
            const bool isVar           = funcType->normIsVarAt(i);
            Type *type                 = funcType->normTypeAt(i);
            graph->addPortNode(isVar ? TupleType::cell(type) : type, portName, false);
        }
    }
    return graph;
}

// How a program writes the operator behind an operator group (`__assn_add__` is `+=`), the
// table of the operators that are calls of a function.
using camel::core::displayNameOf;
using camel::core::operatorSymbolOf;

/// The prefix of a name the compiler writes for a built-in operator it means itself (the
/// `filter` and `map` of a list comprehension). No program can write it, and no declaration
/// of the program is looked at when it is resolved.
constexpr std::string_view kBuiltinPrefix = "$builtin::";

inline bool hasDirectDataDependency(node_handle_t lhs, node_handle_t rhs) {
    if (!lhs || !rhs || nodeGraphOf(lhs) != nodeGraphOf(rhs)) {
        return false;
    }
    const draft_node_ref_t lhsId = nodeIdOf(lhs);
    const draft_node_ref_t rhsId = nodeIdOf(rhs);
    return std::ranges::find(normInputsOf(rhs), lhsId) != normInputsOf(rhs).end() ||
           std::ranges::find(withInputsOf(rhs), lhsId) != withInputsOf(rhs).end() ||
           std::ranges::find(normInputsOf(lhs), rhsId) != normInputsOf(lhs).end() ||
           std::ranges::find(withInputsOf(lhs), rhsId) != withInputsOf(lhs).end();
}

inline bool shouldAddSyncCtrlLink(node_handle_t from, node_handle_t to) {
    // Only the exact, local skip: the predecessor is already a direct data input of the target,
    // so the edge would say nothing new. This is not a count of control users and cannot be one.
    // Whether a value is shared -- whether `:var/take` copies -- is answered at run time by the
    // cell's own `holdsUnsharedArray` mark (`:op/assn_owned` sets it, `:var/get` and `:op/assn`
    // clear it, `:var/peek` leaves it), and a control edge carries order, not a use (design
    // principles 3.3, 3.4). The transitive reduction over such edges lives in `std::opt::order`;
    // handover H95 measured that adding or removing one changes no copy.
    return linkCheek(from, to) && !hasDirectDataDependency(from, to);
}

/// A statement one `sync` block writes directly: the edge that joins it to the statement written
/// before it is a sync edge (docs/design/effects/edges.cn.md 1.5), and the builder records that fact
/// on both ends so the seal check can read the graph back (src/runtime/graph_edges.cpp,
/// `checkSyncEdges`).
inline void markSyncStatement(node_handle_t node) {
    if (node == nullptr) {
        return;
    }
    auto *draft               = nodeDraftOf(node);
    const draft_node_ref_t id = nodeIdOf(node);
    draft->setNodeRuntimeFlags(
        id, static_cast<uint8_t>(draft->header(id)->runtimeFlags | runtime::kGCNodeFlagSyncStatement));
}

inline camel::source::SemanticPart semanticPart(
    camel::source::SemanticRole role, camel::source::origin_id_t origin, int32_t slot = -1,
    const std::string &label = "") {
    return camel::source::SemanticPart{
        .role   = role,
        .origin = origin,
        .slot   = slot,
        .label  = label};
}

inline const camel::source::SemanticBundle *
gctSemantic(const context_ptr_t &context, const GCT::node_ptr_t &gct) {
    if (!context || !gct) {
        return nullptr;
    }
    auto sourceContext = context->sourceContext();
    return sourceContext ? sourceContext->gctSemantic(gct->origin()) : nullptr;
}

inline camel::source::origin_id_t deriveGirOrigin(
    const context_ptr_t &context, const GCT::node_ptr_t &gct, camel::source::OriginKind kind,
    const std::string &label, std::vector<camel::source::origin_id_t> inputs = {},
    camel::source::span_id_t primarySpan = camel::source::kInvalidSpanId) {
    // GIR is a more execution-oriented lowering than GCT.
    // We do not rebuild fresh source spans for every GIR node; instead, we keep
    // deriving from the GCT primary origin, so multiple GIR nodes expanded from
    // one GCT node usually share the same primarySpan.
    if (!context || !gct) {
        return camel::source::kInvalidOriginId;
    }
    auto sourceContext = context->sourceContext();
    if (!sourceContext) {
        return camel::source::kInvalidOriginId;
    }
    auto origin = gct->origin();
    if (origin == camel::source::kInvalidOriginId) {
        return camel::source::kInvalidOriginId;
    }
    return sourceContext->deriveOrigin(
        origin,
        camel::source::OriginStage::GIR,
        kind,
        label,
        primarySpan,
        false,
        std::move(inputs));
}

/// The span of the whole call expression a GCT call node stands for.
///
/// A node's own span covers what the node was made from -- a call's is its argument list
/// (`(x * 0.5)`), an operator's is the operator token -- and the expression is the union with
/// its operands (see `sourceTextOf`). A node that stands for the whole call, rather than for
/// the operation inside it, therefore covers `f(x * 0.5)`: the callee and the argument list.
/// Spans of another file are ignored: such a span has no union, and the node's own span is a
/// correct (if shorter) answer.
inline camel::source::span_id_t
callExpressionSpan(const context_ptr_t &context, const GCT::node_ptr_t &gct) {
    auto *sourceContext = context ? context->sourceContext().get() : nullptr;
    if (sourceContext == nullptr || gct == nullptr) {
        return camel::source::kInvalidSpanId;
    }
    camel::source::source_file_id_t fileId = camel::source::kInvalidSourceFileId;
    size_t start = 0, end = 0;
    const auto cover = [&](const GCT::node_ptr_t &node) {
        if (node == nullptr) {
            return;
        }
        const auto *record = sourceContext->origin(node->origin());
        const auto *span   = record ? sourceContext->span(record->primarySpan) : nullptr;
        if (span == nullptr || !span->valid() || span->endOffset <= span->startOffset) {
            return;
        }
        if (fileId == camel::source::kInvalidSourceFileId) {
            fileId = span->fileId;
            start  = span->startOffset;
            end    = span->endOffset;
        } else if (fileId == span->fileId) {
            start = std::min(start, span->startOffset);
            end   = std::max(end, span->endOffset);
        }
    };
    cover(gct);
    for (size_t i = 0; i < gct->size(); ++i) {
        cover(gct->at(i));
    }
    if (fileId == camel::source::kInvalidSourceFileId) {
        return camel::source::kInvalidSpanId;
    }
    return sourceContext->createSpan(fileId, start, end);
}

/// The span of the whole expression a GCT node stands for: the union of the node's span and
/// those of everything below it. A node's own span covers only part of what it stands for (an
/// operator's is the operator token, a binary expression's is its right side), and a folded
/// expression is nested (`(1 + 2) * 3`), so the whole tree is covered. Spans of another file
/// are ignored. Invalid when no node has a span.
inline camel::source::span_id_t
expressionSpan(const context_ptr_t &context, const GCT::node_ptr_t &gct) {
    auto *sourceContext = context ? context->sourceContext().get() : nullptr;
    if (sourceContext == nullptr || gct == nullptr) {
        return camel::source::kInvalidSpanId;
    }
    camel::source::source_file_id_t fileId = camel::source::kInvalidSourceFileId;
    size_t start = 0, end = 0;
    std::function<void(const GCT::node_ptr_t &, int)> cover = [&](const GCT::node_ptr_t &node,
                                                                 int depth) {
        if (node == nullptr || depth > 32) {
            return;
        }
        const auto *record = sourceContext->origin(node->origin());
        const auto *span   = record ? sourceContext->span(record->primarySpan) : nullptr;
        if (span != nullptr && span->valid() && span->endOffset > span->startOffset) {
            if (fileId == camel::source::kInvalidSourceFileId) {
                fileId = span->fileId;
                start  = span->startOffset;
                end    = span->endOffset;
            } else if (fileId == span->fileId) {
                start = std::min(start, span->startOffset);
                end   = std::max(end, span->endOffset);
            }
        }
        for (size_t i = 0; i < node->size(); ++i) {
            cover(node->at(i), depth + 1);
        }
    };
    cover(gct, 0);
    if (fileId == camel::source::kInvalidSourceFileId) {
        return camel::source::kInvalidSpanId;
    }
    return sourceContext->createSpan(fileId, start, end);
}

inline camel::source::SemanticBundle makeGirSemanticBundle(
    camel::source::origin_id_t mainOrigin, const context_ptr_t &context, const GCT::node_ptr_t &gct,
    std::vector<camel::source::SemanticPart> extraParts  = {},
    std::vector<camel::source::origin_id_t> mergedInputs = {}, bool synthetic = false,
    const std::string &syntheticReason = "") {
    camel::source::SemanticBundle bundle;
    bundle.mainOrigin      = mainOrigin;
    bundle.synthetic       = synthetic;
    bundle.syntheticReason = syntheticReason;
    if (const auto *parentBundle = gctSemantic(context, gct)) {
        bundle.parts = parentBundle->parts;
        if (mergedInputs.empty()) {
            mergedInputs = parentBundle->mergedInputs;
        }
    }
    bundle.parts.insert(bundle.parts.end(), extraParts.begin(), extraParts.end());
    bundle.mergedInputs = std::move(mergedInputs);
    return bundle;
}

inline void registerGraphOrigin(
    const context_ptr_t &context, const compile_graph_ptr_t &graph, const GCT::node_ptr_t &gct,
    const std::string &label                             = "gir.graph",
    std::vector<camel::source::SemanticPart> extraParts  = {},
    std::vector<camel::source::origin_id_t> mergedInputs = {}, bool synthetic = false,
    const std::string &syntheticReason                   = "",
    camel::source::span_id_t primarySpan = camel::source::kInvalidSpanId) {
    if (!context || !graph) {
        return;
    }
    auto sourceContext = context->sourceContext();
    if (!sourceContext) {
        return;
    }
    auto origin = deriveGirOrigin(
        context, gct, camel::source::OriginKind::Graph, label, mergedInputs, primarySpan);
    if (origin != camel::source::kInvalidOriginId) {
        sourceContext->debugMap().registerGraphOrigin(graph->graphId(), origin);
        sourceContext->registerGirGraphSemantic(
            graph->graphId(),
            makeGirSemanticBundle(
                origin,
                context,
                gct,
                std::move(extraParts),
                std::move(mergedInputs),
                synthetic,
                syntheticReason));
    }
}

inline void registerNodeOrigin(
    const context_ptr_t &context, node_handle_t node, const GCT::node_ptr_t &gct,
    const std::string &label = "gir.node", std::vector<camel::source::SemanticPart> extraParts = {},
    std::vector<camel::source::origin_id_t> mergedInputs = {}, bool synthetic = false,
    const std::string &syntheticReason   = "",
    camel::source::span_id_t primarySpan = camel::source::kInvalidSpanId) {
    if (!context || node == nullptr) {
        return;
    }
    auto sourceContext = context->sourceContext();
    if (!sourceContext) {
        return;
    }
    auto origin = deriveGirOrigin(
        context,
        gct,
        camel::source::OriginKind::GirNode,
        label,
        mergedInputs,
        primarySpan);
    if (origin != camel::source::kInvalidOriginId) {
        // The node carries its origin through encoding and runtime rewrites.
        nodeDraftOf(node)->setNodeOrigin(nodeIdOf(node), origin);
        sourceContext->bindGirNodeDraftDebug(
            draftDebugKey(node),
            origin,
            makeGirSemanticBundle(
                origin,
                context,
                gct,
                std::move(extraParts),
                std::move(mergedInputs),
                synthetic,
                syntheticReason));
    }
}

/// The origin of the node that stands for a call expression: `f(x)`, `x->f`, `g(x)` where
/// `g` is a function value. Its primary span is the whole call, while its semantic bundle is
/// the one of the GCT call node, as for any other GIR node.
inline void registerCallExpressionOrigin(
    const context_ptr_t &context, node_handle_t node, const GCT::node_ptr_t &gct,
    const std::string &label, std::vector<camel::source::SemanticPart> extraParts = {},
    std::vector<camel::source::origin_id_t> mergedInputs = {}) {
    registerNodeOrigin(
        context,
        node,
        gct,
        label,
        std::move(extraParts),
        std::move(mergedInputs),
        false,
        "",
        callExpressionSpan(context, gct));
}

inline void bindGraphScopedFuncNodeDebug(
    const camel::source::source_context_ptr_t &sourceContext,
    camel::source::origin_id_t graphOrigin, const compile_graph_ptr_t &graph, node_handle_t node) {
    camel::source::SourceContext *sc = sourceContext.get();
    if (!sc || graphOrigin == camel::source::kInvalidOriginId || node == nullptr) {
        return;
    }
    if (nodeDraftOf(node)->nodeOrigin(nodeIdOf(node)) == 0) {
        nodeDraftOf(node)->setNodeOrigin(nodeIdOf(node), graphOrigin);
    }
    if (const auto *graphSemantic = sc->girGraphSemantic(graph->graphId())) {
        sc->bindGirNodeDraftDebug(draftDebugKey(node), graphOrigin, *graphSemantic);
    } else {
        camel::source::SemanticBundle bundle;
        bundle.mainOrigin = graphOrigin;
        sc->bindGirNodeDraftDebug(draftDebugKey(node), graphOrigin, std::move(bundle));
    }
}

/// The placeholders of a format string in the order written (`{`, `{:spec}`), each as the
/// text `format` is asked about. A doubled brace is a literal brace, so it is skipped. This is
/// only used on the pattern of a formatted string, which is built with placeholders in the
/// order of the interpolations and no explicit argument index.
inline std::vector<std::string> formatPlaceholders(std::string_view pattern) {
    std::vector<std::string> out;
    for (size_t i = 0; i < pattern.size(); ++i) {
        const char c = pattern[i];
        if (c == '{' && i + 1 < pattern.size() && pattern[i + 1] == '{') {
            ++i;
            continue;
        }
        if (c == '}' && i + 1 < pattern.size() && pattern[i + 1] == '}') {
            ++i;
            continue;
        }
        if (c != '{') {
            continue;
        }
        const size_t close = pattern.find('}', i + 1);
        if (close == std::string_view::npos) {
            break;
        }
        out.emplace_back(pattern.substr(i, close - i + 1));
        i = close;
    }
    return out;
}

// ---- Whether two same-named declarations of one signature are told apart by their conditions ----

/// How the applicability conditions of two same-named declarations relate (design principles
/// 4.4; the language author's ruling of 2026-10-24 on `impl-declaration-arms`,
/// `docs/handover/reviews/questions-2026-10-23.md`): "同名声明按 `when` 互斥分臂就是重载"
/// -- declarations split by mutually exclusive conditions ARE overloads, so a same-named set of
/// them may stand side by side, one family of types each.
enum class ConditionSeparation {
    /// No type the compiler registers satisfies both: the two declarations cover families that
    /// exclude each other, and a call of either is decided by its argument types.
    Disjoint,
    /// A registered type satisfies both (`overlappingType` names it): a call with arguments of
    /// that type would be decided by the order the declarations are written in, which no rule of
    /// the language allows (design principles 4.3, 5.1).
    Overlapping,
    /// A condition could not be answered for a registered type, or one of the two declarations
    /// writes no condition at all: nothing is known about the pair, and it is refused exactly as
    /// it was before this check (the ruling: "求不出 ... 按今天的规则拒绝").
    NotComputable,
};

/// The types a condition is asked about: the types the compiler registers as identities -- the
/// primitive types `stdlib/types.cml` names (`int`, `i32`, `float`, `f32`, `bf16`, `f16`, `c64`,
/// `c128`, `bool`, `string`, `void`) -- and one sample of each shape a composite value has, so
/// that a condition that tells shapes apart (`is_function::<T>()`) is asked about those too. A
/// type a program declares (a struct, an enum, a module's own type) is not among them: the
/// ruling's check is per registered type, and a pair whose conditions overlap only on such a type
/// is not caught here.
///
/// `int` comes first: a pair whose conditions do not depend on the type at all (two ordinary
/// functions of one signature) satisfies every type, and the diagnostic then names the type the
/// signatures themselves are about.
inline const std::vector<Type *> &registeredWhenTypes() {
    static const std::vector<Type *> types = [] {
        std::vector<Type *> list{
            Type::Int(),        Type::Int32(),     Type::Float(),     Type::Float32(),
            Type::BFloat16(),   Type::Float16(),   Type::Complex64(), Type::Complex128(),
            Type::Bool(),       Type::String(),    Type::Void()};
        // One of each composite shape: an array, a tuple, a function, and an empty struct.
        list.push_back(ArrayType::create(Type::Int()));
        list.push_back(TupleType::create({Type::Int(), Type::Int()}));
        camel::core::type::FunctionTypeFactory factory;
        factory.addNormArg("x", Type::Int(), false);
        factory.setExitType(Type::Int());
        list.push_back(factory.build());
        list.push_back(StructType::create());
        return list;
    }();
    return types;
}

/// The names of the type variables a condition writes, in the order it writes them: a condition
/// is asked about one type by binding each of them to it (`is_integer::<T>()` asks about `T`,
/// which the declaration's own signature is written over).
inline void collectConditionTypeVars(
    const GCT::node_ptr_t &condition, std::vector<std::string> &names) {
    if (condition == nullptr) {
        return;
    }
    if (condition->type() == GCT::NodeKind::TYPE) {
        std::vector<std::string> written;
        camel::core::type::collectTypeVars(condition->dataType(), written);
        for (const std::string &name : written) {
            if (std::find(names.begin(), names.end(), name) == names.end()) {
                names.push_back(name);
            }
        }
    }
    for (size_t i = 0; i < condition->size(); ++i) {
        collectConditionTypeVars(condition->at(i), names);
    }
}

/// Answers one declaration's condition with one type bound to every type variable it writes. It
/// is built where the declaration is written, so that the condition's predicate is looked up
/// there (Builder::evaluateWhenCondition); a condition the asker cannot answer throws, and the
/// pair is then refused as it was before this check.
using WhenConditionAsker =
    std::function<WhenResult(const camel::core::type::type_bindings_t &bindings)>;

/// Whether the conditions `left` and `right` exclude each other over `registeredWhenTypes()`
/// (design principles 4.4, 5.1; the ruling of 2026-10-24 quoted above). `overlappingType` is set
/// to the type that satisfies both when the answer is `Overlapping`.
///
/// The conditions are asked about every registered type, with the type bound to the type
/// variables each one writes: a type both conditions hold for is the overlap the ruling reports.
/// An answer that is neither true nor false (`a predicate with a part that has no compile-time
/// value`, a name the declaration does not make visible, ...) is not an answer, so the pair is
/// refused by the caller as it was before.
inline ConditionSeparation separateWhenConditions(
    const GCT::node_ptr_t &left, const GCT::node_ptr_t &right, const WhenConditionAsker &askLeft,
    const WhenConditionAsker &askRight, std::string &overlappingType) {
    if (left == nullptr || right == nullptr) {
        return ConditionSeparation::NotComputable; // one of them writes no condition
    }
    std::vector<std::string> leftVars;
    std::vector<std::string> rightVars;
    collectConditionTypeVars(left, leftVars);
    collectConditionTypeVars(right, rightVars);
    bool unanswerable = false;
    for (Type *type : registeredWhenTypes()) {
        if (type == nullptr) {
            continue;
        }
        camel::core::type::type_bindings_t leftBindings;
        camel::core::type::type_bindings_t rightBindings;
        for (const std::string &name : leftVars) {
            leftBindings.emplace_back(name, type);
        }
        for (const std::string &name : rightVars) {
            rightBindings.emplace_back(name, type);
        }
        WhenResult leftVerdict;
        WhenResult rightVerdict;
        try {
            leftVerdict  = askLeft(leftBindings);
            rightVerdict = askRight(rightBindings);
        } catch (...) {
            // The condition has no answer for this type: it is not an overlap, and not evidence
            // that the pair is separable either.
            unanswerable = true;
            continue;
        }
        if (leftVerdict.holds() && rightVerdict.holds()) {
            overlappingType = type->toString();
            return ConditionSeparation::Overlapping;
        }
        if (!leftVerdict.holds() && !leftVerdict.fails()) {
            unanswerable = true;
        }
        if (!rightVerdict.holds() && !rightVerdict.fails()) {
            unanswerable = true;
        }
    }
    return unanswerable ? ConditionSeparation::NotComputable : ConditionSeparation::Disjoint;
}

} // namespace camel::compile::gir
