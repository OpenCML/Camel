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
 * Created: Aug. 17, 2024
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "builder.h"

#include "camel/compile/gir/static_function.h"
#include "camel/execute/entry_args.h"
#include "camel/runtime/graph.h"
#include "camel/utils/log.h"
#include "camel/utils/scope.h"
#include "camel/utils/str.h"
#include "camel/utils/type.h"
#include "core/module/entity_internal.h"

#include "camel/core/operator_traits.h"
#include "camel/core/type/other.h"

#include <atomic>
#define DEBUG_LEVEL -1

using namespace std;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::data;
using namespace camel::core::type;

namespace camel::compile::gir {

namespace {
constexpr std::size_t kSourceContextExtraIndex = 3;

bool isVoidGraphExitType(Type *type) {
    return type == nullptr || type == Type::Void() || type->code() == TypeCode::Void;
}

runtime::GCFillKind fillKindForType(Type *type) {
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

void setGraphReturnFromResult(
    const compile_graph_ptr_t &graph, draft_node_ref_t outputNode, draft_node_ref_t valueNode,
    Type *resultType) {
    ASSERT(graph != nullptr, "Graph return setup requires a valid graph.");
    graph->draft().setOutputNode(outputNode);
    graph->draft().setExitNode(outputNode);
    if (isVoidGraphExitType(resultType)) {
        graph->draft().setReturnNode(runtime::kInvalidNodeRef, runtime::GCReturnKind::None);
        if (!graph->funcType()->hasExitType()) {
            graph->funcType()->setExitType(Type::Void());
        }
        return;
    }

    graph->draft().setReturnNode(valueNode, runtime::GCReturnKind::Self);
    if (!graph->funcType()->hasExitType()) {
        graph->funcType()->setExitType(resultType);
    }
}

camel::source::gir_draft_node_key_t draftDebugKey(node_handle_t node) {
    return node ? static_cast<camel::source::gir_draft_node_key_t>(node->header.selfId) : 0;
}

node_handle_t asCompileNode(camel::core::module::detail::EntityAccess::node_handle_t node) {
    return static_cast<node_handle_t>(node);
}

compile_graph_ptr_t
asCompileGraph(const camel::core::module::detail::EntityAccess::graph_handle_t &graph) {
    return graph;
}

graph_set_ptr_t
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

graph_vec_ptr_t asCompileGraphVec(const DraftGraphBuilder::DrefTarget &target) {
    return std::get<graph_vec_ptr_t>(target);
}

camel::core::module::detail::EntityAccess::graph_handle_t
eraseCompileGraph(const compile_graph_ptr_t &graph) {
    return graph;
}

camel::core::module::detail::EntityAccess::graph_set_ptr_t
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

camel::core::mm::IAllocator &staticFunctionAllocator(const compile_graph_ptr_t &ownerGraph) {
    return ownerGraph->arena()->allocator();
}

TupleType *currentClosureTupleType(const compile_graph_ptr_t &graph) {
    type_vec_t closureTypes;
    auto &draft = graph->draft();
    closureTypes.reserve(draft.closureNodes().size());
    for (draft_node_ref_t nodeId : draft.closureNodes()) {
        const auto *header = draft.header(nodeId);
        closureTypes.push_back(header ? header->dataType : nullptr);
    }
    return TupleType::create(std::move(closureTypes));
}

StaticFunction *createStaticFunction(
    const compile_graph_ptr_t &ownerGraph, const compile_graph_ptr_t &targetGraph) {
    ASSERT(
        targetGraph != nullptr,
        "Target graph is null when materializing compile-time function.");
    return StaticFunction::create(
        targetGraph,
        currentClosureTupleType(targetGraph),
        staticFunctionAllocator(ownerGraph));
}

runtime::DraftEdgeKind toDraftEdgeKind(LinkType type) {
    switch (type) {
    case LinkType::Norm:
        return runtime::DraftEdgeKind::Norm;
    case LinkType::With:
        return runtime::DraftEdgeKind::With;
    case LinkType::Ctrl:
        return runtime::DraftEdgeKind::Ctrl;
    }
    ASSERT(false, "Unknown link type.");
    return runtime::DraftEdgeKind::Norm;
}

runtime::GCNodeKind nodeKindOf(node_handle_t node) {
    ASSERT(node != nullptr, "Draft node is null.");
    return node->header.kind;
}

Type *nodeTypeOf(node_handle_t node) {
    ASSERT(node != nullptr, "Draft node is null.");
    return node->header.dataType;
}

const runtime::DraftNodeHeader *nodeHeaderOf(node_handle_t node) {
    ASSERT(node != nullptr, "Draft node is null.");
    return &node->header;
}

runtime::GraphDraft *nodeDraftOf(node_handle_t node) {
    ASSERT(node != nullptr, "Draft node is null.");
    return node->header.owner;
}

DraftGraphBuilder *nodeGraphOf(node_handle_t node) {
    auto *builder = DraftGraphBuilder::fromDraft(nodeDraftOf(node));
    ASSERT(builder != nullptr, "Draft node is not owned by a compile graph builder.");
    return builder;
}

draft_node_ref_t nodeIdOf(node_handle_t node) {
    ASSERT(node != nullptr, "Draft node is null.");
    return node->header.selfId;
}

std::span<const draft_node_ref_t> normInputsOf(node_handle_t node) {
    return nodeDraftOf(node)->normInputsOf(node);
}

std::span<const draft_node_ref_t> withInputsOf(node_handle_t node) {
    return nodeDraftOf(node)->withInputsOf(node);
}

std::span<const draft_node_ref_t> ctrlInputsOf(node_handle_t node) {
    return nodeDraftOf(node)->ctrlInputsOf(node);
}

std::span<const draft_node_ref_t> normUsersOf(node_handle_t node) {
    return nodeDraftOf(node)->normUsersOf(node);
}

std::span<const draft_node_ref_t> withUsersOf(node_handle_t node) {
    return nodeDraftOf(node)->withUsersOf(node);
}

std::span<const draft_node_ref_t> ctrlUsersOf(node_handle_t node) {
    return nodeDraftOf(node)->ctrlUsersOf(node);
}

std::string_view nodeDebugEntityIdOf(node_handle_t node) {
    return node ? nodeGraphOf(node)->nodeDebugEntityId(node) : std::string_view{};
}

bool nodeIsKind(node_handle_t node, runtime::GCNodeKind kind) {
    return node != nullptr && nodeKindOf(node) == kind;
}

/// Compile-time value of a node that is static data (possibly behind a COPY), else nullopt.
std::optional<slot_t> staticValueOf(node_handle_t node) {
    if (nodeIsKind(node, runtime::GCNodeKind::Copy)) {
        const auto inputs = normInputsOf(node);
        if (inputs.size() != 1) {
            return std::nullopt;
        }
        node = nodeDraftOf(node)->node(inputs.front());
    }
    if (!nodeIsKind(node, runtime::GCNodeKind::Data) || node->header.dataIndex >= 0) {
        return std::nullopt;
    }
    const auto slots = nodeDraftOf(node)->staticSlots();
    const auto index = static_cast<size_t>(-node->header.dataIndex);
    return index < slots.size() ? std::optional<slot_t>(slots[index]) : std::nullopt;
}

/// The node behind a COPY of it (the argument of a call is copied when it is a constant).
node_handle_t throughCopy(node_handle_t node) {
    if (nodeIsKind(node, runtime::GCNodeKind::Copy)) {
        const auto inputs = normInputsOf(node);
        if (inputs.size() == 1) {
            return nodeDraftOf(node)->node(inputs.front());
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
bool collectConversionGaps(
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
std::string spellingOf(Type *type) {
    return type->isOtherType() ? static_cast<OtherType *>(type)->spelling() : type->toString();
}

/// The elements of `text` when it is a parenthesised list written on the spot, `(a, b)`.
std::vector<std::string> tupleElementTexts(const std::string &text) {
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
std::string conversionTextOf(const std::string &text, const ConversionGap &gap) {
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
std::string describeGaps(const std::string &subject, const std::vector<ConversionGap> &gaps) {
    std::string out;
    for (const ConversionGap &gap : gaps) {
        out += out.empty() ? "" : "; ";
        out += (gap.path.empty() ? subject : "element " + gap.path + " of " + subject) +
               " has type '" + gap.have->toString() + "', which is less exact than '" +
               gap.want->toString() + "'";
    }
    return out;
}

void setNodeType(node_handle_t node, Type *type) {
    ASSERT(node != nullptr, "Cannot update type of null draft node.");
    nodeDraftOf(node)->setNodeDataType(nodeIdOf(node), type);
}

void setNodeMacro(node_handle_t node, bool enabled) {
    ASSERT(node != nullptr, "Cannot update runtime flags of null draft node.");
    uint8_t flags = nodeHeaderOf(node)->runtimeFlags;
    if (enabled) {
        flags = static_cast<uint8_t>(flags | runtime::kGCNodeFlagMacro);
    } else {
        flags = static_cast<uint8_t>(flags & ~runtime::kGCNodeFlagMacro);
    }
    nodeDraftOf(node)->setNodeRuntimeFlags(nodeIdOf(node), flags);
}

bool isMacroGraph(const compile_graph_ptr_t &graph) {
    return graph && graph->funcType() && graph->funcType()->modifiers().macro();
}

std::vector<std::string> closureRefNames(const compile_graph_ptr_t &graph) {
    std::vector<std::string> refs;
    if (!graph) {
        return refs;
    }
    refs.reserve(graph->draft().closureNodes().size());
    for (draft_node_ref_t portId : graph->draft().closureNodes()) {
        refs.push_back(graph->nodePortName(graph->draft().node(portId)));
    }
    return refs;
}

bool sameGraph(node_handle_t lhs, const compile_graph_ptr_t &graph) {
    return lhs != nullptr && nodeGraphOf(lhs) == graph.get();
}

void linkNodes(LinkType type, node_handle_t from, node_handle_t to) {
    ASSERT(from != nullptr && to != nullptr, "Cannot link null draft nodes.");
    ASSERT(nodeGraphOf(from) == nodeGraphOf(to), "Compile-time link must stay within one graph.");
    nodeGraphOf(from)->link(toDraftEdgeKind(type), from, to);
}

std::string makeCompileGraphStableId(const std::string &name) {
    static std::atomic<uint64_t> seq = 1;
    return std::format("{}_{}", name.empty() ? "g" : name, seq++);
}

compile_graph_ptr_t createCompileGraph(
    FunctionType *funcType, const compile_graph_ptr_t &outer, std::string name = "") {
    auto graph = std::make_shared<DraftGraphBuilder>(funcType ? funcType : FunctionType::create());
    if (name.empty()) {
        name = std::format("__{}__", makeCompileGraphStableId(""));
    }
    graph->setName(std::move(name));
    graph->setStableId(makeCompileGraphStableId(graph->name()));
    if (outer) {
        graph->setOuterGraph(outer);
        outer->addSubGraph(graph);
    }
    if (funcType) {
        for (size_t i = 0; i < funcType->withTypesCount(); ++i) {
            const std::string portName = i < funcType->argNamesCount()
                                             ? std::string(funcType->argNameAt(i))
                                             : std::format("__w{}", i);
            const bool isVar = funcType->withIsVarAt(i);
            Type *type       = funcType->withTypeAt(i);
            graph->addPortNode(isVar ? TupleType::cell(type) : type, portName, true, isVar);
        }
        for (size_t i = 0; i < funcType->normTypesCount(); ++i) {
            const size_t argIndex      = funcType->withTypesCount() + i;
            const std::string portName = argIndex < funcType->argNamesCount()
                                             ? std::string(funcType->argNameAt(argIndex))
                                             : std::format("__n{}", i);
            const bool isVar = funcType->normIsVarAt(i);
            Type *type       = funcType->normTypeAt(i);
            graph->addPortNode(isVar ? TupleType::cell(type) : type, portName, false, isVar);
        }
    }
    return graph;
}
} // namespace

inline void tryRemoveCtrlLink(node_handle_t from, node_handle_t to) {
    // if from has already linked to to by a ctrl link, remove it first
    // sometimes we may need to change a ctrl link (linked before) to a data link
    // because data link has higher priority than ctrl link
    // and we don't want to have duplicate links
    if (!from || !to || nodeGraphOf(from) != nodeGraphOf(to)) {
        return;
    }
    (void)nodeGraphOf(from)->unlink(runtime::DraftEdgeKind::Ctrl, from, to);
}

inline bool linkCheek(node_handle_t from, node_handle_t to) {
    // prevent linking a node to itself
    if (!from || !to || from == to) {
        return false;
    }
    return true;
}

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
    return linkCheek(from, to) && !hasDirectDataDependency(from, to);
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
    return sourceContext ? sourceContext->gctSemantic(gct->load()->origin()) : nullptr;
}

inline camel::source::origin_id_t deriveGirOrigin(
    const context_ptr_t &context, const GCT::node_ptr_t &gct, camel::source::OriginKind kind,
    const std::string &label, std::vector<camel::source::origin_id_t> inputs = {}) {
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
    auto origin = gct->load()->origin();
    if (origin == camel::source::kInvalidOriginId) {
        return camel::source::kInvalidOriginId;
    }
    return sourceContext->deriveOrigin(
        origin,
        camel::source::OriginStage::GIR,
        kind,
        label,
        camel::source::kInvalidSpanId,
        false,
        std::move(inputs));
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
    const std::string &syntheticReason = "") {
    if (!context || !graph) {
        return;
    }
    auto sourceContext = context->sourceContext();
    if (!sourceContext) {
        return;
    }
    auto origin =
        deriveGirOrigin(context, gct, camel::source::OriginKind::Graph, label, mergedInputs);
    if (origin != camel::source::kInvalidOriginId) {
        sourceContext->debugMap().registerGraphOrigin(graph->stableId(), origin);
        sourceContext->registerGirGraphSemantic(
            graph->stableId(),
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
    const std::string &syntheticReason = "") {
    if (!context || node == nullptr) {
        return;
    }
    auto sourceContext = context->sourceContext();
    if (!sourceContext) {
        return;
    }
    auto origin =
        deriveGirOrigin(context, gct, camel::source::OriginKind::GirNode, label, mergedInputs);
    if (origin != camel::source::kInvalidOriginId) {
        // The node carries its origin through encoding and runtime rewrites.
        node->header.origin = origin;
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

inline void bindGraphScopedFuncNodeDebug(
    const camel::source::source_context_ptr_t &sourceContext,
    camel::source::origin_id_t graphOrigin, const compile_graph_ptr_t &graph, node_handle_t node) {
    camel::source::SourceContext *sc = sourceContext.get();
    if (!sc || graphOrigin == camel::source::kInvalidOriginId || node == nullptr) {
        return;
    }
    if (node->header.origin == 0) {
        node->header.origin = graphOrigin;
    }
    if (const auto *graphSemantic = sc->girGraphSemantic(graph->stableId())) {
        sc->bindGirNodeDraftDebug(draftDebugKey(node), graphOrigin, *graphSemantic);
    } else {
        camel::source::SemanticBundle bundle;
        bundle.mainOrigin = graphOrigin;
        sc->bindGirNodeDraftDebug(draftDebugKey(node), graphOrigin, std::move(bundle));
    }
}

compile_graph_ptr_t Builder::build(GCT::node_ptr_t &gct, diagnostics_ptr_t diags) {
    waited_ = false;
    synced_ = false;
    varied_ = false;
    diags_  = diags;
    usedGraphs_.clear();
    variables_.clear();
    pendingUses_.clear();
    captures_.clear();
    bodyStates_.clear();
    capturePortNames_.clear();
    pendingIgnoredWrites_.clear();
    builtFunctions_.clear();
    effectfulGraphs_.clear();
    armGraphs_.clear();
    builtBodies_.clear();
    typeDecls_.clear();
    syntheticRefIndex_ = 0;

    nodeScope_      = node_scope_t::create();
    graphScope_     = graph_scope_t::create();
    decoratedScope_ = decorated_scope_t::create();
    rootGraph_      = createCompileGraph(FunctionType::create(), nullptr, "__root__");
    if (auto sourceContext = context_ ? context_->sourceContext() : nullptr) {
        rootGraph_->setExtra<camel::source::SourceContext, kSourceContextExtraIndex>(
            sourceContext.get());
    }
    currGraph_ = rootGraph_;

    try {
        visit(gct);
        reportIgnoredCapturedWrites();
        reportUndeclaredCapturedWrites();
        reportDroppedEffectsOfBodies();

        compile_graph_ptr_t entryGraph = nullptr;
        if (auto decoratedMain = decoratedGraphAt("main")) {
            entryGraph = *decoratedMain;
        } else if (auto gv = graphsAt("main"); gv.has_value() && !gv.value()->empty()) {
            entryGraph = gv.value()->front();
        }
        const bool entryModule =
            context_ && module_ && context_->mainModule() && context_->mainModule() == module_;
        if (entryGraph) {
            // The entry graph stands for the entry function where a diagnostic is about the
            // program as a whole (a translation pass that finds nothing to translate): it
            // shares the origin of `main`, also after passes have inlined `main` into it.
            if (auto sourceContext = context_ ? context_->sourceContext() : nullptr) {
                const auto mainOrigin =
                    sourceContext->debugMap().graphOrigin(entryGraph->stableId());
                if (mainOrigin != camel::source::kInvalidOriginId) {
                    sourceContext->debugMap().registerGraphOrigin(
                        rootGraph_->stableId(),
                        mainOrigin);
                    // Also under a fixed key: the entry graph is rewritten (and renamed) by
                    // passes, the question "where is main" outlives that.
                    sourceContext->debugMap().registerGraphOrigin(
                        camel::execute::kEntryFunctionOriginKey,
                        mainOrigin);
                }
            }
            // `main` is a function like any other: its parameters are the inputs of the
            // program. The entry graph takes the same parameters (`with` ones first) and
            // passes them on, so it never calls `main` with fewer arguments than it takes.
            // Who supplies them is not decided here: a run binds values given from outside
            // (see camel/execute/entry_args.h), a translation takes them as the inputs of
            // what it produces.
            FunctionType *entryType = entryGraph->funcType();
            std::vector<node_handle_t> entryWithArgs, entryNormArgs;
            if (entryType->withTypesCount() + entryType->normTypesCount() > 0) {
                FunctionTypeFactory rootTypeFactory;
                const auto nameAt = [&](size_t argIndex, const char *prefix, size_t i) {
                    return argIndex < entryType->argNamesCount()
                               ? std::string(entryType->argNameAt(argIndex))
                               : std::format("{}{}", prefix, i);
                };
                for (size_t i = 0; i < entryType->withTypesCount(); ++i) {
                    const std::string name = nameAt(i, "__w", i);
                    const bool isVar       = entryType->withIsVarAt(i);
                    Type *type             = entryType->withTypeAt(i);
                    rootTypeFactory.addWithArg(name, type, isVar);
                    entryWithArgs.push_back(
                        rootGraph_
                            ->addPortNode(isVar ? TupleType::cell(type) : type, name, true, isVar));
                }
                for (size_t i = 0; i < entryType->normTypesCount(); ++i) {
                    const std::string name = nameAt(entryType->withTypesCount() + i, "__n", i);
                    const bool isVar       = entryType->normIsVarAt(i);
                    Type *type             = entryType->normTypeAt(i);
                    rootTypeFactory.addNormArg(name, type, isVar);
                    entryNormArgs.push_back(
                        rootGraph_
                            ->addPortNode(isVar ? TupleType::cell(type) : type, name, false, isVar));
                }
                rootGraph_->setFuncType(rootTypeFactory.build());
            }
            auto entryFuncValue = createFuncDataNode(entryGraph, true, false);
            auto entryCall      = rootGraph_->addCallNode(entryGraph->funcType()->exitType());
            linkNodes(LinkType::With, entryFuncValue, entryCall);
            for (node_handle_t arg : entryWithArgs) {
                linkNodes(LinkType::With, arg, entryCall);
            }
            for (node_handle_t arg : entryNormArgs) {
                linkNodes(LinkType::Norm, arg, entryCall);
            }
            setGraphReturnFromResult(
                rootGraph_,
                nodeIdOf(entryCall),
                nodeIdOf(entryCall),
                entryGraph->funcType()->exitType());
            // Module-level statements run only as far as the call of `main` depends on them.
            reportDroppedEffects(rootGraph_);
        } else if (entryModule) {
            diags_->of(SemanticDiag::EntryModuleMissingMain).commit(module_->name());
            throw BuildAbortException();
        } else {
            auto *placeholder =
                rootGraph_->addStaticDataNode(std::make_shared<LongData>(static_cast<int64_t>(0)));
            setGraphReturnFromResult(
                rootGraph_,
                nodeIdOf(placeholder),
                nodeIdOf(placeholder),
                Type::Int64());
        }
    } catch (Diagnostic &d) {
        diags_->add(std::move(d));
        rootGraph_ = nullptr;
    } catch (const BuildAbortException &) {
        rootGraph_ = nullptr;
    }

    return rootGraph_;
}

compile_graph_ptr_t Builder::enterScope(FunctionType *funcType, const std::string &name) {
    if (name.empty()) {
        currGraph_ = createCompileGraph(funcType, currGraph_);
    } else {
        auto graphs = graphScope_->get(name);
        if (graphs.has_value() && !graphs.value()->empty()) {
            currGraph_ = graphs.value()->front();
        } else {
            currGraph_ = createCompileGraph(funcType, currGraph_, name);
            insertGraph(name, currGraph_);
        }
    }
    if (auto sourceContext = context_ ? context_->sourceContext() : nullptr) {
        currGraph_->setExtra<camel::source::SourceContext, kSourceContextExtraIndex>(
            sourceContext.get());
    }
    nodeScope_      = nodeScope_->enter(name);
    graphScope_     = graphScope_->enter(name);
    decoratedScope_ = decoratedScope_->enter(name);
    return currGraph_;
}

void Builder::leaveScope() {
    nodeScope_      = nodeScope_->leave();
    graphScope_     = graphScope_->leave();
    decoratedScope_ = decoratedScope_->leave();
    currGraph_      = currGraph_ ? currGraph_->outerGraph() : nullptr;
}

bool Builder::insertNode(const std::string &name, node_handle_t node) {
    if (nodeScope_->has(name, false)) {
        return false;
    }
    nodeScope_->insert(name, node);
    return true;
}

bool Builder::insertGraph(const std::string &name, const compile_graph_ptr_t &graph) {
    if (graphScope_->has(name, false)) {
        auto graphs = graphScope_->get(name).value();
        graphs->push_back(graph);
    }
    graphScope_->insert(name, std::make_shared<graph_set_t>(1, graph));
    return true;
}

bool Builder::insertDecoratedGraph(const std::string &name, const compile_graph_ptr_t &graph) {
    if (!decoratedScope_) {
        return false;
    }
    decoratedScope_->insert(name, graph);
    return true;
}

node_handle_t Builder::resolveCrossGraphRef(node_handle_t node, const std::string &name) {
    compile_graph_ptr_t curr = currGraph_;
    node_scope_ptr_t scope   = nodeScope_;

    while (curr && node && curr.get() != nodeGraphOf(node)) {
        if (usedGraphs_.find(curr.get()) != usedGraphs_.end()) {
            diags_->of(SemanticDiag::ClosureCaptureAfterSelfCall).commit(name, curr->name());
            throw BuildAbortException();
        }

        // A captured variable is captured by reference: the port holds the same cell.
        auto *portNode = curr->addPortNode(nodeTypeOf(node), name, false, isVariable(node));
        curr->draft().removeNormPort(nodeIdOf(portNode));
        curr->draft().appendClosureNode(nodeIdOf(portNode));
        const auto closureRefs = curr->funcType()->closureRefs();
        if (std::find(closureRefs.begin(), closureRefs.end(), name) == closureRefs.end()) {
            curr->funcType()->addClosureRef(name);
        }
        scope->insert(name, portNode);
        if (isVariable(node)) {
            variables_.insert(portNode);
            capturePortNames_.emplace(portNode, name);
            captures_[curr.get()].variables.push_back(name);
        }

        curr  = curr->outerGraph();
        scope = scope->outer();
    }

    return *nodeScope_->get(name);
}

node_handle_t Builder::resolveNodeByRef(const std::string &name) {
    auto optSrcNode = nodeAt(name);
    if (!optSrcNode.has_value()) {
        EXEC_WHEN_DEBUG({
            nodeScope_->dump(
                std::cerr,
                [](std::ostream &os, const std::string &key, const node_handle_t &value) {
                    os << "[" << key << "] ";
                    if (value == nullptr) {
                        os << "<invalid>";
                        return;
                    }
                    auto *graph = nodeGraphOf(value);
                    os << graph->name() << "#" << nodeIdOf(value);
                },
                0);
        });
        auto sourceContext = context_ ? context_->sourceContext() : nullptr;
        auto origin        = (sourceContext && currGraph_)
                                 ? sourceContext->debugMap().graphOrigin(currGraph_->stableId())
                                 : camel::source::kInvalidOriginId;
        diags_->of(SemanticDiag::UnresolvedReference).atOrigin(origin).commit(name);
        throw BuildAbortException();
    }
    node_handle_t node = optSrcNode.value();

    if (!sameGraph(node, currGraph_)) {
        node = resolveCrossGraphRef(node, name);
        ASSERT(sameGraph(node, currGraph_), "Failed to resolve cross-graph reference.");
    }

    return node;
}

any Builder::visit(const GCT::node_ptr_t &node) {
    switch (node->type()) {
    case GCT::LoadType::DECL:
        return visitDeclNode(node);
    case GCT::LoadType::FUNC:
        return visitFuncNode(node);
    case GCT::LoadType::DATA:
        return visitDataNode(node);
    case GCT::LoadType::TYPE:
        return visitTypeNode(node);
    case GCT::LoadType::NREF:
        return visitNRefNode(node);
    case GCT::LoadType::DREF:
        return visitDRefNode(node);
    case GCT::LoadType::VARI:
        return visitVariNode(node);
    case GCT::LoadType::WAIT:
        return visitWaitNode(node);
    case GCT::LoadType::LINK:
        return visitLinkNode(node);
    case GCT::LoadType::WITH:
        return visitWithNode(node);
    case GCT::LoadType::ACCS:
        return visitAccsNode(node);
    case GCT::LoadType::BRCH:
        return visitBrchNode(node);
    case GCT::LoadType::ANNO:
        return visitAnnoNode(node);
    case GCT::LoadType::EXIT:
        return visitExitNode(node);
    case GCT::LoadType::EXEC:
        return visitExecNode(node);
    case GCT::LoadType::EXPT:
        return visitExptNode(node);
    case GCT::LoadType::CAST:
        return visitCastNode(node);
    default:
        ASSERT(false, "Unknown GCT NodeType");
    }
    return nullptr;
}

void_ptr_t Builder::visitDeclNode(const GCT::node_ptr_t &gct) {
    ENTER("DECL");
    const auto &declLoad     = gct->loadAs<GCT::DeclLoad>();
    GCT::node_ptr_t typeNode = gct->atAs<GCT::TypeLoad>(0);
    Type *type               = typeNode->loadAs<GCT::TypeLoad>()->dataType();
    if (!declLoad->isFunc()) {
        typeDecls_[declLoad->ref()] = type;
        LEAVE("DECL");
        return nullptr;
    }

    FunctionType *funcType    = tt::as_ptr<FunctionType>(type);
    compile_graph_ptr_t graph = enterScope(funcType, declLoad->ref().toString());
    leaveScope();

    LEAVE("DECL");
    return nullptr;
}

compile_graph_ptr_t Builder::visitFuncNode(const GCT::node_ptr_t &gct) {
    ENTER("FUNC");
    std::string name          = gct->loadAs<GCT::FuncLoad>()->name();
    GCT::node_ptr_t typeLoad  = gct->atAs<GCT::TypeLoad>(0);
    Type *type                = typeLoad->loadAs<GCT::TypeLoad>()->dataType();
    FunctionType *funcType    = tt::as_ptr<FunctionType>(type);
    compile_graph_ptr_t graph = enterScope(funcType, name);
    try {
        registerGraphOrigin(context_, graph, gct, "gir.func.graph");
        const auto withPorts = graph->draft().withPorts();
        for (size_t i = 0; i < withPorts.size(); ++i) {
            node_handle_t port = graph->draft().node(withPorts[i]);
            insertNode(graph->nodePortName(port), port);
            if (i < funcType->withTypesCount() && funcType->withIsVarAt(i)) {
                variables_.insert(port);
            }
        }
        const auto normPorts = graph->draft().normPorts();
        for (size_t i = 0; i < normPorts.size(); ++i) {
            node_handle_t port = graph->draft().node(normPorts[i]);
            insertNode(graph->nodePortName(port), port);
            if (i < funcType->normTypesCount() && funcType->normIsVarAt(i)) {
                variables_.insert(port);
            }
        }
        bodyStates_[graph.get()] = BodyState::Open;
        setBodyResult(graph, visitBodyNode(gct->atAs<GCT::ExecLoad>(1)));
        bodyStates_[graph.get()] = BodyState::Built;
        builtFunctions_.push_back({graph.get(), gct->load()->origin(), name});
        builtBodies_.push_back(graph);
    } catch (...) {
        bodyStates_[graph.get()] = BodyState::Built;
        leaveScope();
        throw;
    }
    leaveScope();

    std::vector<GCT::node_ptr_t> annoNodes;
    for (size_t i = 2; i < gct->size(); ++i) {
        const auto &annoNode = gct->at(i);
        if (annoNode && annoNode->type() == GCT::LoadType::ANNO) {
            annoNodes.push_back(annoNode);
        }
    }
    if (!annoNodes.empty()) {
        graph_ptr_t decorated = buildDecoratedGraph(name, graph, annoNodes);
        insertDecoratedGraph(name, decorated);
    }

    LEAVE("FUNC");
    return graph;
}

node_handle_t Builder::visitDataNode(const GCT::node_ptr_t &gct) {
    ENTER("DATA");
    const auto &dataLoad   = gct->loadAs<GCT::DataLoad>();
    const data_ptr_t &data = dataLoad->data();

    node_handle_t node{};
    TypeCode dataType = data->type()->code();
    if (isComposite(dataType)) {
        auto composedData = tt::as_shared<CompositeData>(data);
        if (!composedData->resolved()) {
            const auto &dataType = tt::as_ptr<CompositeType>(data->type());
            auto holes           = composedData->holes();
            type_vec_t refTypes;
            std::vector<node_handle_t> refNodes;
            for (const auto &ref : composedData->refs()) {
                const auto &refNode = valueOf(resolveNodeByRef(std::string(ref)));
                refTypes.push_back(nodeTypeOf(refNode));
                refNodes.push_back(refNode);
            }
            ASSERT(
                holes.size() == refNodes.size(),
                "Composite holes must match unresolved reference count.");
            auto filledType       = dataType->resolved()
                                        ? dataType
                                        : tt::as_ptr<CompositeType>(dataType->clone())->resolve(refTypes);
            auto payload          = runtime::makeFillPayload(fillKindForType(filledType), holes);
            node_handle_t srcNode = currGraph_->addStaticDataNode(data, filledType);
            node                  = currGraph_->addFillNode(filledType, payload);
            linkNodes(LinkType::Norm, srcNode, node);
            // An array written in place with some elements computed: the constant elements
            // stay known to the operators the array is passed to (StaticArg::elements).
            if (auto array = std::dynamic_pointer_cast<ArrayData>(data);
                array && filledType->code() == TypeCode::Array) {
                Type *elemType = tt::as_ptr<ArrayType>(filledType)->elemType();
                std::vector<std::optional<slot_t>> elements(array->raw().size());
                for (size_t i = 0; i < elements.size(); ++i) {
                    const auto &element = array->raw()[i];
                    if (!element || element->type() != elemType) {
                        continue;
                    }
                    if (auto l = std::dynamic_pointer_cast<LongData>(element)) {
                        elements[i] = camel::core::rtdata::toSlot(l->data());
                    } else if (auto n = std::dynamic_pointer_cast<IntData>(element)) {
                        elements[i] = camel::core::rtdata::toSlot(n->data());
                    } else if (auto d = std::dynamic_pointer_cast<DoubleData>(element)) {
                        elements[i] = camel::core::rtdata::toSlot(d->data());
                    } else if (auto f = std::dynamic_pointer_cast<FloatData>(element)) {
                        elements[i] = camel::core::rtdata::toSlot(f->data());
                    } else if (auto b = std::dynamic_pointer_cast<BoolData>(element)) {
                        elements[i] = camel::core::rtdata::toSlot(b->data());
                    }
                }
                for (size_t h = 0; h < holes.size(); ++h) {
                    if (holes[h] < elements.size()) {
                        elements[holes[h]] = nodeTypeOf(refNodes[h]) == elemType
                                                 ? staticArgOf(refNodes[h]).value
                                                 : std::nullopt;
                    }
                }
                arrayElementStatics_[node] = std::move(elements);
            }
            std::vector<camel::source::origin_id_t> mergedInputs;
            for (const auto &refNode : refNodes) {
                linkNodes(LinkType::With, refNode, node);
                if (auto *sourceContext = context_ ? context_->sourceContext().get() : nullptr) {
                    auto origin = sourceContext->resolveGirNodeOrigin(draftDebugKey(refNode), "");
                    if (origin != camel::source::kInvalidOriginId) {
                        mergedInputs.push_back(origin);
                    }
                }
            }
            registerNodeOrigin(
                context_,
                node,
                gct,
                "gir.data.fill",
                {
                    semanticPart(
                        camel::source::SemanticRole::ValueProducer,
                        context_->sourceContext() ? context_->sourceContext()->resolveGirNodeOrigin(
                                                        draftDebugKey(srcNode),
                                                        "")
                                                  : camel::source::kInvalidOriginId,
                        -1,
                        "base"),
                },
                std::move(mergedInputs));

            LEAVE("DATA");
            return node;
        }
    }

    node = currGraph_->addStaticDataNode(data);
    if (varied_ && currGraph_->outerGraph().get() != nullptr) {
        node_handle_t copyNode = currGraph_->addCopyNode(data->type());
        linkNodes(LinkType::Norm, node, copyNode);
        node = copyNode;
    }

    registerNodeOrigin(context_, node, gct, "gir.data");

    LEAVE("DATA");
    return node;
}

Type *Builder::visitTypeNode(const GCT::node_ptr_t &gct) {
    ENTER("TYPE");
    Type *type = gct->loadAs<GCT::TypeLoad>()->dataType();
    LEAVE("TYPE");
    return type;
}

node_handle_t Builder::visitNRefNode(const GCT::node_ptr_t &gct) {
    ENTER("NREF");
    const string &ident = gct->loadAs<GCT::NRefLoad>()->ref();
    // Names declared inside the initializer are not part of this declaration.
    const bool isVar   = std::exchange(declaresVariable_, false);
    node_handle_t node = valueNodeOf(visit(gct->at(0)));
    declaresVariable_  = isVar;
    // `var x = e` binds x to a new variable holding the value of e; `let x = e` binds it to
    // that value.
    bool success = insertNode(ident, isVar ? newVariable(node) : node);
    if (!success) {
        diags_->of(SemanticDiag::Redeclaration).atOrigin(gct->load()->origin()).commit(ident);
        throw BuildAbortException();
    }
    LEAVE("NREF");
    return node;
}

std::optional<node_handle_t> Builder::modifierOf(node_handle_t node) const {
    if (node == nullptr) {
        return std::nullopt;
    }
    auto *graph  = nodeGraphOf(node);
    auto graphIt = nodeModifierMaps_.find(graph);
    if (graphIt == nodeModifierMaps_.end()) {
        return std::nullopt;
    }
    auto it = graphIt->second.find(nodeIdOf(node));
    if (it == graphIt->second.end()) {
        return std::nullopt;
    }
    return graph->draft().node(it->second);
}

void Builder::setModifier(node_handle_t input, node_handle_t modifier) {
    ASSERT(input != nullptr && modifier != nullptr, "Cannot record modifier for a null node.");
    auto *inputGraph = nodeGraphOf(input);
    ASSERT(
        inputGraph == nodeGraphOf(modifier),
        "Modifier tracking must stay within a single compile graph.");
    nodeModifierMaps_[inputGraph][nodeIdOf(input)] = nodeIdOf(modifier);
}

void Builder::recordWrite(
    node_handle_t variable, node_handle_t entry, node_handle_t exit, bool throughCallee) {
    // `A(i); wait modify(i); B(i)` gives A -> modify -> B (design principles 2.6). The second
    // half follows from setModifier. The first: the waited writer is ordered after every
    // operation that directly used the variable's value before it: the operations that took a
    // value read from it (the read itself when nothing took it), and the calls and branches
    // that used the variable through a capture. `wait` orders by this use of the variable, not
    // by position: in `A(i + 1); wait modify(i)` the direct user is `+`, not A. In a sync block
    // the block's chain already orders them. A writer that is neither waited nor in a sync
    // block is not ordered after earlier users; they stay pending for the next waited writer.
    auto &uses = pendingUses_[variable];
    if (waited_ && !synced_) {
        std::unordered_set<node_handle_t> before;
        for (const VariableUse &use : uses) {
            bool taken = false;
            if (use.isRead) {
                for (auto users : {normUsersOf(use.node), withUsersOf(use.node)}) {
                    for (draft_node_ref_t user : users) {
                        before.insert(nodeDraftOf(use.node)->node(user));
                        taken = true;
                    }
                }
            }
            if (!taken) {
                before.insert(use.node);
            }
        }
        for (node_handle_t node : before) {
            if (node != exit && shouldAddSyncCtrlLink(node, entry)) {
                linkNodes(LinkType::Ctrl, node, entry);
            }
        }
    }
    if (waited_ || synced_) {
        uses.clear();
    }
    setModifier(variable, exit);
    // A write of a captured variable is what callers of this graph have to see.
    if (!throughCallee) {
        if (auto captured = capturePortNames_.find(variable);
            captured != capturePortNames_.end()) {
            captures_[nodeGraphOf(variable)].written.insert(captured->second);
        }
    }
}

void Builder::orderAfterLastWriter(node_handle_t variable, node_handle_t node) {
    if (auto modifier = modifierOf(variable); modifier.has_value()) {
        if (sameGraph(*modifier, currGraph_) && shouldAddSyncCtrlLink(*modifier, node)) {
            linkNodes(LinkType::Ctrl, *modifier, node);
        }
    }
}

std::set<std::string> Builder::capturedWritesOf(DraftGraphBuilder *root) const {
    const auto isOpen = [&](DraftGraphBuilder *graph) {
        auto state = bodyStates_.find(graph);
        return state != bodyStates_.end() && state->second == BodyState::Open;
    };
    const auto summaryOf = [&](DraftGraphBuilder *graph) -> const CaptureSummary * {
        auto summary = captures_.find(graph);
        return summary == captures_.end() ? nullptr : &summary->second;
    };
    // The graphs reachable through direct calls and arms. An open body is not looked into:
    // what it will write is not known yet, so all it captured stands for it.
    std::vector<DraftGraphBuilder *> reached{root};
    std::unordered_map<DraftGraphBuilder *, std::set<std::string>> writes;
    for (size_t i = 0; i < reached.size(); ++i) {
        DraftGraphBuilder *graph = reached[i];
        const auto *summary      = summaryOf(graph);
        if (summary == nullptr) {
            writes[graph];
            continue;
        }
        if (isOpen(graph)) {
            writes[graph].insert(summary->variables.begin(), summary->variables.end());
            continue;
        }
        writes[graph] = summary->written;
        for (DraftGraphBuilder *callee : summary->callees) {
            if (std::ranges::find(reached, callee) == reached.end()) {
                reached.push_back(callee);
            }
        }
    }
    // Least fixpoint of: a graph writes what its callees write, as far as it captured the
    // variable itself (a callee's write of the graph's own local variable stops there). A
    // recursive call adds nothing to the function's own set, so recursion needs no special
    // case here.
    for (bool changed = true; changed;) {
        changed = false;
        for (DraftGraphBuilder *graph : reached) {
            const auto *summary = summaryOf(graph);
            if (summary == nullptr || isOpen(graph)) {
                continue;
            }
            auto &own = writes[graph];
            for (DraftGraphBuilder *callee : summary->callees) {
                for (const std::string &name : writes[callee]) {
                    if (std::ranges::find(summary->variables, name) != summary->variables.end() &&
                        own.insert(name).second) {
                        changed = true;
                    }
                }
            }
        }
    }
    return writes[root];
}

std::vector<Builder::CapturedUse> Builder::capturedUsesOf(const compile_graph_ptr_t &callee) {
    auto &callees = captures_[currGraph_.get()].callees;
    if (std::ranges::find(callees, callee.get()) == callees.end()) {
        callees.push_back(callee.get());
    }
    auto summary = captures_.find(callee.get());
    if (summary == captures_.end() || summary->second.variables.empty()) {
        return {};
    }
    // Resolving a name may add a capture port to the current graph and its summary.
    const std::vector<std::string> names = summary->second.variables;
    const std::set<std::string> written  = capturedWritesOf(callee.get());
    std::vector<CapturedUse> uses;
    for (const std::string &name : names) {
        // The callee's capture is bound by name where it is called, as its with-inputs are.
        node_handle_t variable = resolveNodeByRef(name);
        if (isVariable(variable)) {
            uses.push_back({variable, name, written.contains(name)});
        }
    }
    return uses;
}

void Builder::reportIgnoredCapturedWrites() {
    for (const PendingIgnoredWrite &pending : pendingIgnoredWrites_) {
        std::set<std::string> written;
        for (DraftGraphBuilder *graph : pending.graphs) {
            written.merge(capturedWritesOf(graph));
        }
        std::vector<std::string> names;
        for (const std::string &name : pending.names) {
            if (written.contains(name)) {
                names.push_back("'" + name + "'");
            }
        }
        if (names.empty()) {
            continue;
        }
        diags_->of(SemanticDiag::IgnoredSideEffect)
            .atOrigin(pending.origin)
            .commit(
                pending.subject,
                std::format(
                    "{} the captured variable{} {}",
                    pending.verb,
                    names.size() > 1 ? "s" : "",
                    strutil::join(names, ", ", [](const std::string &name) { return name; })));
    }
    pendingIgnoredWrites_.clear();
}

void Builder::reportUndeclaredCapturedWrites() {
    // The prefix `sync` of a function declares that it has side effects (design principles
    // 2.3); a function with a `var` parameter is already checked for it
    // (VarParamInAsyncFunction, in the GCT builder). A function that writes a variable it
    // captured, itself or through the functions it calls, has side effects as well. This is a
    // declaration check only: the modifier changes nothing in how calls are compiled.
    for (const BuiltFunction &function : builtFunctions_) {
        if (function.graph->funcType()->modifiers().sync()) {
            continue;
        }
        std::vector<std::string> names;
        for (const std::string &name : capturedWritesOf(function.graph)) {
            names.push_back("'" + name + "'");
        }
        if (names.empty()) {
            continue;
        }
        diags_->of(SemanticDiag::CapturedWriteInAsyncFunction)
            .atOrigin(function.origin)
            .commit(
                function.name.empty() ? std::string("The lambda")
                                      : std::format("Function '{}'", function.name),
                names.size() > 1 ? "s" : "",
                strutil::join(names, ", ", [](const std::string &name) { return name; }));
    }
    builtFunctions_.clear();
}

bool Builder::isEffectNode(DraftGraphBuilder *graph, node_handle_t node) const {
    const auto declaresEffects = [](Type *type) {
        if (type == nullptr || type->code() != TypeCode::Function) {
            return false;
        }
        const auto *func = tt::as_ptr<FunctionType>(type);
        if (func->modifiers().sync()) {
            return true;
        }
        for (size_t i = 0; i < func->normTypesCount(); ++i) {
            if (func->normIsVarAt(i)) {
                return true;
            }
        }
        for (size_t i = 0; i < func->withTypesCount(); ++i) {
            if (func->withIsVarAt(i)) {
                return true;
            }
        }
        return false;
    };
    const auto calleeOf = [&](node_handle_t func) { return graph->funcTarget(func); };
    switch (nodeKindOf(node)) {
    case runtime::GCNodeKind::Oper: {
        const auto &op = graph->operTarget(node);
        return op != nullptr &&
               camel::core::OperatorTraitsRegistry::instance().hasExternalEffect(op->uri());
    }
    case runtime::GCNodeKind::Func: {
        const auto callee = calleeOf(node);
        // An arm is entered by its branch, which is what gets reported.
        if (callee == nullptr || armGraphs_.contains(callee.get())) {
            return false;
        }
        return declaresEffects(callee->funcType()) || effectfulGraphs_.contains(callee.get());
    }
    case runtime::GCNodeKind::Call: {
        const auto callee = withInputsOf(node);
        return !callee.empty() && declaresEffects(nodeTypeOf(nodeDraftOf(node)->node(callee[0])));
    }
    case runtime::GCNodeKind::Join: {
        for (draft_node_ref_t arm : withInputsOf(node)) {
            node_handle_t armNode = nodeDraftOf(node)->node(arm);
            if (nodeIsKind(armNode, runtime::GCNodeKind::Func)) {
                const auto callee = calleeOf(armNode);
                if (callee != nullptr && effectfulGraphs_.contains(callee.get())) {
                    return true;
                }
            }
        }
        return false;
    }
    default:
        return false;
    }
}

void Builder::reportDroppedEffectsOfBodies() {
    // Whether a call is an effect depends on its callee's body, which may be written later in
    // the file (or be the caller itself): the set of effectful graphs is the least fixpoint
    // over all bodies, and only then are the warnings issued, in the order the bodies were
    // built.
    for (size_t known = effectfulGraphs_.size() + 1; known != effectfulGraphs_.size();) {
        known = effectfulGraphs_.size();
        for (const compile_graph_ptr_t &graph : builtBodies_) {
            reportDroppedEffects(graph, false);
        }
    }
    for (const compile_graph_ptr_t &graph : builtBodies_) {
        reportDroppedEffects(graph, true);
    }
    builtBodies_.clear();
}

void Builder::reportDroppedEffects(const compile_graph_ptr_t &graph, bool report) {
    auto &draft = graph->draft();
    // What the exit depends on, through data and control edges.
    std::unordered_set<draft_node_ref_t> needed;
    std::vector<draft_node_ref_t> work;
    for (draft_node_ref_t root : {draft.outputNode(), draft.exitNode(), draft.returnNode()}) {
        if (root != runtime::kInvalidNodeRef && needed.insert(root).second) {
            work.push_back(root);
        }
    }
    if (work.empty()) {
        return; // the body did not build (an error was reported)
    }
    while (!work.empty()) {
        const draft_node_ref_t id = work.back();
        work.pop_back();
        for (auto inputs : {draft.normInputsOf(id), draft.withInputsOf(id), draft.ctrlInputsOf(id)}) {
            for (draft_node_ref_t input : inputs) {
                if (needed.insert(input).second) {
                    work.push_back(input);
                }
            }
        }
    }
    std::vector<draft_node_ref_t> dropped;
    for (draft_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        if (!draft.alive(id) || !isEffectNode(graph.get(), draft.node(id))) {
            continue;
        }
        if (needed.contains(id)) {
            effectfulGraphs_.insert(graph.get());
        } else {
            dropped.push_back(id);
        }
    }
    if (!report) {
        return;
    }
    // One warning per dropped chain: an effect whose result feeds another dropped effect
    // (`println(log(1))`, the statements of a dropped sync block) is reported through the
    // last one.
    const auto feedsDroppedEffect = [&](draft_node_ref_t start) {
        std::unordered_set<draft_node_ref_t> seen{start};
        std::vector<draft_node_ref_t> stack{start};
        while (!stack.empty()) {
            const draft_node_ref_t id = stack.back();
            stack.pop_back();
            for (auto users : {draft.normUsersOf(id), draft.withUsersOf(id), draft.ctrlUsersOf(id)}) {
                for (draft_node_ref_t user : users) {
                    if (!seen.insert(user).second) {
                        continue;
                    }
                    if (std::ranges::find(dropped, user) != dropped.end()) {
                        return true;
                    }
                    stack.push_back(user);
                }
            }
        }
        return false;
    };
    auto sourceContext = context_ ? context_->sourceContext() : nullptr;
    for (draft_node_ref_t id : dropped) {
        if (feedsDroppedEffect(id)) {
            continue;
        }
        node_handle_t node = draft.node(id);
        std::string subject;
        switch (nodeKindOf(node)) {
        case runtime::GCNodeKind::Oper: {
            const std::string &name = graph->operTarget(node)->name();
            subject = name.starts_with("__assn") ? std::string("The assignment")
                                                 : std::format("The call of '{}'", name);
        } break;
        case runtime::GCNodeKind::Func:
            subject = std::format("The call of '{}'", graph->funcTarget(node)->name());
            break;
        case runtime::GCNodeKind::Join:
            subject = "The branch";
            break;
        default:
            subject = "The call";
            break;
        }
        camel::source::origin_id_t origin = node->header.origin;
        if (origin == camel::source::kInvalidOriginId && sourceContext) {
            origin = sourceContext->debugMap().graphOrigin(graph->stableId());
        }
        diags_->of(SemanticDiag::EffectNotExecuted).atOrigin(origin).commit(subject);
    }
}

node_handle_t Builder::visitDRefNode(const GCT::node_ptr_t &gct) {
    ENTER("DREF");
    const string name = gct->loadAs<GCT::DRefLoad>()->ref().toString();
    auto optNode      = nodeAt(name);
    if (optNode.has_value()) {
        node_handle_t node = optNode.value();
        if (!sameGraph(node, currGraph_)) {
            node = resolveCrossGraphRef(node, name);
            ASSERT(sameGraph(node, currGraph_), "Failed to resolve cross-graph reference.");
        }
        LEAVE("DREF");
        return valueOf(node);
    }
    auto &graph       = currGraph_;
    auto optDecorated = decoratedGraphAt(name);
    if (optDecorated.has_value()) {
        node_handle_t drefNode = graph->addDrefNode(
            std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>{optDecorated.value()});
        registerNodeOrigin(context_, drefNode, gct, "gir.dref.decorated");
        LEAVE("DREF");
        return drefNode;
    }
    auto optGraphs = graphsAt(name);
    if (optGraphs.has_value()) {
        auto graphs = optGraphs.value();
        if (!graphs->empty()) {
            node_handle_t drefNode{graph->addDrefNode(
                std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>{graphs})};
            registerNodeOrigin(context_, drefNode, gct, "gir.dref");
            LEAVE("DREF");
            return drefNode;
        }
    }
    if (module_->hasImportedRef(name)) {
        // hasImportedRef only means the user declared an import for this name.
        // It does not imply the imported module exported that name, so we still
        // need to check here.
        const auto &opt = module_->getImportedEntity(name);
        if (!opt.has_value()) {
            diags_->of(SemanticDiag::ImportNameNotExported)
                .atOrigin(gct->load()->origin())
                .commit(name);
            throw BuildAbortException();
        }
        const auto &e = opt.value();
        if (camel::core::module::detail::EntityAccess::isNode(e)) {
            ASSERT(false, "Cannot import a data node directly.");
            const auto node = asCompileNode(camel::core::module::detail::EntityAccess::node(e));
            LEAVE("DREF");
            return node;
        } else if (camel::core::module::detail::EntityAccess::isGraphSet(e)) {
            auto graphs = asCompileGraphSet(camel::core::module::detail::EntityAccess::graphSet(e));
            node_handle_t drefNode{graph->addDrefNode(
                std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>{graphs})};
            registerNodeOrigin(context_, drefNode, gct, "gir.dref");
            LEAVE("DREF");
            return drefNode;
        } else if (e.isOperGroup()) {
            auto ops = e.operGroup();
            node_handle_t drefNode{graph->addDrefNode(
                std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>{ops})};
            registerNodeOrigin(context_, drefNode, gct, "gir.dref");
            LEAVE("DREF");
            return drefNode;
        } else if (camel::core::module::detail::EntityAccess::isDecoratedGraph(e)) {
            auto decorated = camel::core::module::detail::EntityAccess::decoratedGraph(e);
            node_handle_t drefNode{graph->addDrefNode(
                std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>{decorated})};
            registerNodeOrigin(context_, drefNode, gct, "gir.dref.decorated");
            LEAVE("DREF");
            return drefNode;
        }
    }
    diags_->of(SemanticDiag::UnresolvedReference).atOrigin(gct->load()->origin()).commit(name);
    throw BuildAbortException();
}

node_handle_t Builder::visitCastNode(const GCT::node_ptr_t &gct) {
    ENTER("CAST");
    const auto &res = visit(gct->at(0));
    ASSERT(res.type() == typeid(node_handle_t), "Unexpected result type from child of CAST node.");
    node_handle_t valueNode = any_cast<node_handle_t>(res);
    ASSERT(valueNode != nullptr, "Cast node value is null.");
    const auto &castLoad = gct->loadAs<GCT::CastLoad>();
    Type *targetType     = castLoad->targetType();
    Type *sourceType     = nodeTypeOf(valueNode);
    // Safe conversions always; an Unsafe one only into a module type, which then checks the
    // value when the conversion runs (Type::castSlotFrom of that type). Primitive conversions
    // that may lose information stay refused.
    const CastSafety safety = targetType->castSafetyFrom(sourceType);
    if (safety != CastSafety::Safe &&
        !(safety == CastSafety::Unsafe && targetType->isOtherType())) {
        diags_->of(SemanticDiag::DynamicCastForbidden)
            .atOrigin(gct->load()->origin())
            .commit(sourceType->toString(), targetType->toString());
        throw BuildAbortException();
    }
    node_handle_t castNode = currGraph_->addCastNode(targetType);
    linkNodes(LinkType::Norm, valueNode, castNode);
    registerNodeOrigin(context_, castNode, gct, "gir.cast");
    LEAVE("CAST");
    return castNode;
}

node_handle_t Builder::visitVariNode(const GCT::node_ptr_t &gct) {
    ENTER("VARI");
    bool old          = varied_;
    bool oldDeclares  = declaresVariable_;
    varied_           = true;
    declaresVariable_ = true;
    const auto &res   = visit(gct->at(0));
    ASSERT(
        res.type() == typeid(node_handle_t),
        "Unexpected result type from Enter the child of VARI node.");
    node_handle_t node = any_cast<node_handle_t>(res);
    varied_            = old;
    declaresVariable_  = oldDeclares;
    LEAVE("VARI");
    return node;
}

node_handle_t Builder::visitWaitNode(const GCT::node_ptr_t &gct) {
    ENTER("WAIT");
    bool old        = waited_;
    waited_         = true;
    const auto &res = visit(gct->at(0));
    ASSERT(
        res.type() == typeid(node_handle_t),
        "Unexpected result type from Enter the child of WAIT node.");
    node_handle_t node = any_cast<node_handle_t>(res);
    waited_            = old;
    LEAVE("WAIT");
    return node;
}

// Create a function-value node for a subgraph. When allowParameterization is
// true, parametrizeClosure() may convert the subgraph's closure captures into
// with parameters, which directly changes the subgraph's port structure. This
// only happens during initial compilation while Graph is still the mutable
// compile-time carrier.
// Create a function-producing node for `graph` in the current owner graph.
//
// This helper is the canonical point where compile-time graph references become IR-level
// dependencies. Every emitted FUNC node or static Function object must be backed by an explicit
// dependency edge, except the self-recursive case which is encoded by the owner graph's
// `looped` bit.
//
// When `allowParameterization=true`, unresolved closure captures may be rewritten into explicit
// with-ports on the target graph. That mutation is only valid during the build phase.
node_handle_t Builder::valueNodeOf(const std::any &res) {
    if (res.type() == typeid(graph_ptr_t)) {
        return createFuncDataNode(any_cast<graph_ptr_t>(res), true, false);
    }
    ASSERT(res.type() == typeid(node_handle_t), "Expression did not produce a value node.");
    return any_cast<node_handle_t>(res);
}

node_handle_t Builder::createFuncDataNode(
    const graph_ptr_t &graph, bool callableAsResult, bool allowParameterization) {
    ASSERT(
        !(callableAsResult && allowParameterization),
        "Cannot enable both callableAsResult and allowParameterization options.");
    ASSERT(currGraph_ != nullptr, "Current owner graph is null when creating a function node.");
    ASSERT(graph != nullptr, "Target graph is null when creating a function node.");

    // Centralize dependency bookkeeping here so every FUNC node and static Function value obeys
    // the same graph-reference invariant. Self-dependency is intentional: recursive graphs are
    // represented by the owner's `looped` flag.
    currGraph_->addDependencyGraph(graph);

    bool graphUsedBefore = usedGraphs_.find(graph.get()) != usedGraphs_.end();
    bool resolved        = graph->draft().closureNodes().empty();
    auto *staticFunc     = createStaticFunction(currGraph_, graph);

    node_handle_t resultNode{};
    auto sourceContext = context_ ? context_->sourceContext() : nullptr;
    auto graphOrigin   = sourceContext ? sourceContext->debugMap().graphOrigin(graph->stableId())
                                       : camel::source::kInvalidOriginId;

    auto markMacroNode = [&](node_handle_t node) {
        if (node != nullptr && isMacroGraph(graph)) {
            setNodeMacro(node, true);
        }
    };

    if (allowParameterization && !callableAsResult && !graphUsedBefore) {
        if (resolved) {
            resultNode = currGraph_->addFuncNode(graph, 0);
            markMacroNode(resultNode);
            bindGraphScopedFuncNodeDebug(sourceContext, graphOrigin, graph, resultNode);
        } else {
            std::vector<draft_node_ref_t> closureNodes(
                graph->draft().closureNodes().begin(),
                graph->draft().closureNodes().end());
            std::vector<std::string> closureRefs = closureRefNames(graph);
            for (draft_node_ref_t closureId : closureNodes) {
                graph->draft().removeNormPort(closureId);
                graph->draft().appendWithPort(closureId);
                graph->draft().removeClosureNode(closureId);
                ASSERT(
                    std::find(
                        graph->draft().normPorts().begin(),
                        graph->draft().normPorts().end(),
                        closureId) == graph->draft().normPorts().end(),
                    "Parameterized closure port must be removed from norm ports.");
            }
            graph->setParameterized(true);
            auto funcNode = currGraph_->addFuncNode(graph, 0);
            markMacroNode(funcNode);
            for (const auto &ref : closureRefs) {
                const auto &refNode = resolveNodeByRef(ref);
                linkNodes(LinkType::With, refNode, funcNode);
            }
            resultNode = funcNode;
            bindGraphScopedFuncNodeDebug(sourceContext, graphOrigin, graph, resultNode);
        }

        usedGraphs_.insert(graph.get());
        return resultNode;
    }

    // allowParameterization = false
    if (resolved) {
        if (callableAsResult) {
            const auto slotIndex = currGraph_->addStaticSlot(
                camel::core::rtdata::toSlot<StaticFunction *>(staticFunc),
                graph->funcType());
            resultNode = currGraph_->draft().node(currGraph_->draft().addDataNode(
                graph->funcType(),
                slotIndex,
                static_cast<uint8_t>(runtime::kGCNodeFlagConstant)));
            markMacroNode(resultNode);
        } else {
            auto funcNode = currGraph_->addFuncNode(graph, 0);
            markMacroNode(funcNode);
            if (graph->parameterized()) {
                for (const auto &ref : graph->funcType()->closureRefs()) {
                    const auto &refNode = resolveNodeByRef(std::string(ref));
                    linkNodes(LinkType::With, refNode, funcNode);
                }
            }
            resultNode = funcNode;
        }
        bindGraphScopedFuncNodeDebug(sourceContext, graphOrigin, graph, resultNode);

        usedGraphs_.insert(graph.get());
        return resultNode;
    }

    // graph still carries unresolved closure captures while parameterization is disabled
    const auto slotIndex = currGraph_->addStaticSlot(
        camel::core::rtdata::toSlot<StaticFunction *>(staticFunc),
        graph->funcType());
    auto dataNode = currGraph_->draft().node(currGraph_->draft().addDataNode(
        graph->funcType(),
        slotIndex,
        static_cast<uint8_t>(runtime::kGCNodeFlagConstant)));
    markMacroNode(dataNode);
    node_vec_t refNodes;
    for (draft_node_ref_t closureId : graph->draft().closureNodes()) {
        const auto &refNode = resolveNodeByRef(graph->nodePortName(graph->draft().node(closureId)));
        refNodes.push_back(refNode);
    }

    std::vector<size_t> closureSlots;
    closureSlots.reserve(refNodes.size());
    for (size_t i = 0; i < refNodes.size(); ++i) {
        closureSlots.push_back(i);
    }
    auto fillPayload = runtime::makeFillPayload(runtime::GCFillKind::FunctionClosure, closureSlots);
    auto fillNode    = currGraph_->addFillNode(graph->funcType(), fillPayload);
    markMacroNode(fillNode);
    linkNodes(LinkType::Norm, dataNode, fillNode);
    for (const auto &refNode : refNodes) {
        linkNodes(LinkType::With, refNode, fillNode);
    }

    if (callableAsResult) {
        resultNode = fillNode;
    } else {
        auto callNode = currGraph_->addCallNode(graph->funcType()->exitType());
        markMacroNode(callNode);
        linkNodes(LinkType::With, fillNode, callNode);
        resultNode = callNode;
    }
    bindGraphScopedFuncNodeDebug(sourceContext, graphOrigin, graph, resultNode);

    // Update usedGraphs_ last.
    // Closure captures may be updated during construction, and capture updates
    // check whether the target graph has already been used. Mark the graph as
    // used only after all updates finish.
    usedGraphs_.insert(graph.get());
    return resultNode;
}

node_handle_t
Builder::applyDecoratorAnno(const GCT::node_ptr_t &annoNode, node_handle_t funcValueNode) {
    ASSERT(annoNode && annoNode->type() == GCT::LoadType::ANNO, "Expected ANNO node.");
    ASSERT(annoNode->size() == 1, "ANNO node should have exactly one lowered expression child.");
    ASSERT(funcValueNode != nullptr, "Decorator input function value is null.");

    // `@name` or `@name<args>`: either way the expression is the decorator to call.
    const auto &decorator = annoNode->at(0);

    const std::string tmpScopeName = "__decorator_tmp_scope_" + std::to_string(syntheticRefIndex_);
    const std::string tmpRefName   = "__decorator_tmp_ref_" + std::to_string(syntheticRefIndex_++);
    nodeScope_                     = nodeScope_->enter(tmpScopeName);
    nodeScope_->insert(tmpRefName, funcValueNode);

    GCT::node_ptr_t linkNode = std::make_shared<GCT::Node>(
        std::dynamic_pointer_cast<GCT::Load>(std::make_shared<GCT::LinkLoad>(1)));
    *linkNode << decorator->clone();
    *linkNode << std::make_shared<GCT::Node>(std::dynamic_pointer_cast<GCT::Load>(
        std::make_shared<GCT::DRefLoad>(Reference(tmpRefName))));

    node_handle_t decoratedValue = visitLinkNode(linkNode);
    nodeScope_                   = nodeScope_->leave();
    return decoratedValue;
}

compile_graph_ptr_t Builder::buildDecoratedGraph(
    const std::string &funcName, const compile_graph_ptr_t &rawGraph,
    const std::vector<GCT::node_ptr_t> &annoNodes) {
    ASSERT(rawGraph != nullptr, "Raw function graph is null.");
    if (annoNodes.empty()) {
        return rawGraph;
    }

    // Decorators run at macro time: the graph applying them is a macro that yields the decorated
    // function once its inputs are static.
    graph_ptr_t decoratedGraph = enterScope(
        FunctionType::create(param_vec_t{}, param_vec_t{}, nullptr, Modifier::Macro),
        "__decorated__" + funcName + "_" + std::to_string(syntheticRefIndex_++));
    registerGraphOrigin(
        context_,
        decoratedGraph,
        annoNodes.back(),
        "gir.func.decorated.graph",
        {},
        {},
        true);

    // Seed value is the original function object.
    node_handle_t decoratedValue = createFuncDataNode(rawGraph, true, false);
    for (auto it = annoNodes.rbegin(); it != annoNodes.rend(); ++it) {
        decoratedValue = applyDecoratorAnno(*it, decoratedValue);
    }

    if (nodeTypeOf(decoratedValue)->code() != TypeCode::Function) {
        diags_->of(SemanticDiag::ArgumentsMismatch)
            .atOrigin(annoNodes.front()->load()->origin())
            .commit("decorator must return function", nodeTypeOf(decoratedValue)->toString());
        throw BuildAbortException();
    }

    setGraphReturnFromResult(
        decoratedGraph,
        nodeIdOf(decoratedValue),
        nodeIdOf(decoratedValue),
        nodeTypeOf(decoratedValue));
    leaveScope();
    return decoratedGraph;
}

node_handle_t Builder::lowerDecoratedGraphValue(
    const compile_graph_ptr_t &decoratedGraph, const GCT::node_ptr_t &origin) {
    currGraph_->addDependencyGraph(decoratedGraph);
    if (!decoratedGraph->funcType()->hasExitType() ||
        decoratedGraph->funcType()->exitType()->code() != TypeCode::Function) {
        diags_->of(SemanticDiag::ArgumentsMismatch)
            .atOrigin(origin->load()->origin())
            .commit(
                "decorated graph should return function",
                decoratedGraph->funcType()->toString());
        throw BuildAbortException();
    }
    node_handle_t decoratorFactoryValue = createFuncDataNode(decoratedGraph, true, false);
    node_handle_t decoratorInvoke = currGraph_->addCallNode(decoratedGraph->funcType()->exitType());
    linkNodes(LinkType::With, decoratorFactoryValue, decoratorInvoke);
    return decoratorInvoke;
}

node_handle_t Builder::argumentNodeOf(const std::any &res, const GCT::node_ptr_t &origin) {
    if (res.type() == typeid(graph_ptr_t)) {
        return createFuncDataNode(any_cast<graph_ptr_t>(res), true, false);
    }
    ASSERT(res.type() == typeid(node_handle_t), "Argument did not produce a value node.");
    node_handle_t node = any_cast<node_handle_t>(res);
    if (!nodeIsKind(node, runtime::GCNodeKind::Dref)) {
        return node;
    }
    // A function named as a value: a decorated function yields what its decorators return, a
    // plain one its function value.
    const auto &target = nodeGraphOf(node)->drefTarget(node);
    if (std::holds_alternative<graph_ptr_t>(target)) {
        return lowerDecoratedGraphValue(std::get<graph_ptr_t>(target), origin);
    }
    if (std::holds_alternative<graph_vec_ptr_t>(target)) {
        auto graphs = asCompileGraphVec(target);
        if (graphs && graphs->size() == 1) {
            return createFuncDataNode(graphs->front(), true, false);
        }
    }
    return node;
}

StaticArg Builder::staticArgOf(node_handle_t node) const {
    StaticArg arg(staticValueOf(node));
    node = throughCopy(node);
    if (!arg.value) {
        if (auto it = typeFoldedValues_.find(node); it != typeFoldedValues_.end()) {
            arg.value = it->second;
        }
    }
    if (auto it = arrayElementStatics_.find(node); it != arrayElementStatics_.end()) {
        arg.elements = it->second;
    }
    return arg;
}

std::string Builder::sourceTextOf(const GCT::node_ptr_t &expr) const {
    const auto *sourceContext = context_ ? context_->sourceContext().get() : nullptr;
    if (sourceContext == nullptr || expr == nullptr) {
        return {};
    }
    // The span of a node covers what the node itself was made from (the argument list of a
    // call, the operator of an expression): the expression is the union with its operands.
    camel::source::source_file_id_t fileId = camel::source::kInvalidSourceFileId;
    size_t start = 0, end = 0;
    bool consistent                                         = true;
    std::function<void(const GCT::node_ptr_t &, int)> cover = [&](const GCT::node_ptr_t &node,
                                                                  int depth) {
        if (node == nullptr || depth > 32) {
            return;
        }
        const auto *record = sourceContext->origin(node->load()->origin());
        const auto *span   = record ? sourceContext->span(record->primarySpan) : nullptr;
        if (span != nullptr && span->valid() && span->endOffset > span->startOffset) {
            if (fileId == camel::source::kInvalidSourceFileId) {
                fileId = span->fileId;
                start  = span->startOffset;
                end    = span->endOffset;
            } else if (fileId != span->fileId) {
                consistent = false;
            } else {
                start = std::min(start, span->startOffset);
                end   = std::max(end, span->endOffset);
            }
        }
        for (size_t i = 0; i < node->size(); ++i) {
            cover(node->at(i), depth + 1);
        }
    };
    cover(expr, 0);
    const auto *file =
        fileId != camel::source::kInvalidSourceFileId ? sourceContext->sourceFile(fileId) : nullptr;
    if (!consistent || file == nullptr || end > file->content.size() || end - start > 60) {
        return {};
    }
    const std::string text = file->content.substr(start, end - start);
    // Quoted only when it is certainly one expression on one line.
    int depth = 0;
    for (const char c : text) {
        if (c == '\r' || c == '\n') {
            return {};
        }
        depth += (c == '(' || c == '[' || c == '{')   ? 1
                 : (c == ')' || c == ']' || c == '}') ? -1
                                                      : 0;
        if (depth < 0) {
            return {};
        }
    }
    return depth == 0 ? text : std::string();
}

node_handle_t Builder::variableOperator(const std::string &group, node_handle_t input) {
    const auto entity = module_->getImportedEntity(group);
    ASSERT(entity.has_value() && entity->isOperGroup(), "Variable operators are not available.");
    const std::vector<StaticArg> statics(1);
    const auto op = entity->operGroup()->resolve({}, {nodeTypeOf(input)}, statics, Modifier::None);
    ASSERT(op.has_value(), "Variable operator rejected its operand.");
    node_handle_t node = currGraph_->addOperNode(*op, 0);
    linkNodes(LinkType::Norm, input, node);
    return node;
}

node_handle_t Builder::newVariable(node_handle_t value) {
    // The variable takes the widened type of its initializer (:var/new), so later assignments
    // may store values with a different refinement (e.g. shape).
    node_handle_t variable = variableOperator("__var_new__", value);
    variables_.insert(variable);
    return variable;
}

node_handle_t Builder::readVariable(node_handle_t variable) {
    node_handle_t value = variableOperator("__var_get__", variable);
    pendingUses_[variable].push_back({value, true});
    if (auto modifier = modifierOf(variable); modifier.has_value()) {
        if (sameGraph(*modifier, currGraph_) && linkCheek(*modifier, value)) {
            linkNodes(LinkType::Ctrl, *modifier, value);
        }
    }
    // In a sync block a read happens where it is written, like a call.
    if (synced_) {
        if (lastSyncedNode_ != nullptr && shouldAddSyncCtrlLink(lastSyncedNode_, value)) {
            linkNodes(LinkType::Ctrl, lastSyncedNode_, value);
        }
        lastSyncedNode_ = value;
    }
    return value;
}

node_handle_t Builder::visitOperand(const GCT::node_ptr_t &expr, const GCT::node_ptr_t &call) {
    if (expr->type() == GCT::LoadType::DREF) {
        const std::string name = expr->loadAs<GCT::DRefLoad>()->ref().toString();
        if (nodeAt(name).has_value()) {
            if (node_handle_t node = resolveNodeByRef(name); isVariable(node)) {
                return node;
            }
        }
    }
    return argumentNodeOf(visit(expr), call);
}

std::optional<std::string> Builder::immutableNameOf(const GCT::node_ptr_t &expr) {
    GCT::node_ptr_t root = expr;
    while (root && root->type() == GCT::LoadType::ACCS) {
        root = root->at(0);
    }
    if (!root || root->type() != GCT::LoadType::DREF) {
        return std::nullopt;
    }
    const std::string name = root->loadAs<GCT::DRefLoad>()->ref().toString();
    // A name without a node is a function or an operator.
    const auto node = nodeAt(name);
    if (!node.has_value() || isVariable(*node)) {
        return std::nullopt;
    }
    return name;
}

void Builder::requireMutableArgument(const Argument &arg, const std::string &callee) {
    if (auto name = immutableNameOf(arg.expr)) {
        diags_->of(SemanticDiag::ImmutablePassedToVarParam)
            .atOrigin(arg.expr->load()->origin())
            .commit(*name, callee);
        throw BuildAbortException();
    }
}

node_handle_t Builder::visitLinkNode(const GCT::node_ptr_t &gct) {
    ENTER("LINK");
    any targetNodeRes = visit(gct->at(0));
    ASSERT(
        targetNodeRes.type() == typeid(node_handle_t),
        "Unexpected result type from Enter the child of LINK node.");
    node_handle_t targetNode = any_cast<node_handle_t>(targetNodeRes);

    compile_graph_ptr_t targetGraph = nullptr;
    oper_idx_ptr_t targetOperator   = nullptr;
    FunctionType *targetFuncType    = nullptr;
    std::vector<Argument> withInputs, normInputs;
    type_vec_t withInputTypes, normInputTypes;

    if (auto bound = boundWithArgs_.find(targetNode); bound != boundWithArgs_.end()) {
        withInputs = bound->second;
    }
    for (const Argument &input : withInputs) {
        withInputTypes.push_back(valueTypeOf(nodeTypeOf(input.node)));
    }

    // Call results take the callee's current exit type (a recursive callee's
    // exit type is only final after its body is built), unless the node was
    // deliberately widened as a `var` binding (see visitVariNode).
    auto callResultType = [&](node_handle_t inputNode, Type *exitType) -> Type * {
        Type *recorded = nodeTypeOf(inputNode);
        if (exitType != nullptr && recorded != exitType && recorded == exitType->widened()) {
            return recorded;
        }
        return exitType;
    };
    auto callableValueType = [&](node_handle_t inputNode) -> Type * {
        if (nodeIsKind(inputNode, runtime::GCNodeKind::Func)) {
            auto callee = nodeGraphOf(inputNode)->funcTarget(inputNode);
            ASSERT(callee != nullptr, "Compile FUNC node target is null.");
            return callResultType(inputNode, callee->funcType()->exitType());
        }
        if (nodeIsKind(inputNode, runtime::GCNodeKind::Oper)) {
            return callResultType(
                inputNode,
                nodeGraphOf(inputNode)->operTarget(inputNode)->funcType()->exitType());
        }
        return nodeTypeOf(inputNode);
    };

    for (size_t i = 1; i < gct->size(); i++) {
        node_handle_t inputNode = visitOperand(gct->at(i), gct);
        normInputs.push_back({inputNode, gct->at(i)});
        normInputTypes.push_back(valueTypeOf(callableValueType(inputNode)));
    }

    if (nodeIsKind(targetNode, runtime::GCNodeKind::Dref)) {
        const auto &drefTarget = nodeGraphOf(targetNode)->drefTarget(targetNode);
        if (std::holds_alternative<graph_vec_ptr_t>(drefTarget)) {
            auto graphs = asCompileGraphVec(drefTarget);
            for (const auto &g : *graphs) {
                const auto &funcType = g->funcType();
                if (!funcType->hasExitType()) {
                    diags_->of(SemanticDiag::CallingIncompleteFunction)
                        .atOrigin(gct->load()->origin())
                        .commit(g->name(), funcType->toString());
                    throw BuildAbortException();
                }
                StaticFuncTypeResolver resolver(funcType);
                if (resolver.resolve(withInputTypes, normInputTypes, Modifier::None).has_value()) {
                    targetGraph = g;
                    break;
                }
            }
            if (!targetGraph) {
                std::vector<std::pair<std::string, FunctionType *>> candidates;
                for (const auto &g : *graphs) {
                    candidates.emplace_back(g->name(), g->funcType());
                }
                reportConversionGaps(
                    candidates,
                    withInputs,
                    normInputs,
                    withInputTypes,
                    normInputTypes,
                    gct);
                std::string argTypesStr = std::format(
                    "<{}> ({})",
                    strutil::join(withInputTypes, ", ", [](Type *t) { return t->toString(); }),
                    strutil::join(normInputTypes, ", ", [](Type *t) { return t->toString(); }));
                std::string overloadsStr =
                    "\n    " +
                    strutil::join(
                        *graphs,
                        "\n    ",
                        [](const graph_ptr_t &g) {
                            return g->name() + ": " + g->funcType()->toString();
                        }) +
                    "\n";
                diags_->of(SemanticDiag::NoMatchingFunction)
                    .atOrigin(gct->load()->origin())
                    .commit(argTypesStr, overloadsStr);
                throw BuildAbortException();
            }
            currGraph_->addDependencyGraph(targetGraph);
            targetNode     = createFuncDataNode(targetGraph, false, true);
            targetFuncType = targetGraph->funcType();
        } else if (std::holds_alternative<oper_group_ptr_t>(drefTarget)) {
            auto ops        = std::get<oper_group_ptr_t>(drefTarget);
            // Constant arguments (shape literals, axes) let operators refine their result type.
            std::vector<StaticArg> normStatics;
            normStatics.reserve(normInputs.size());
            for (const Argument &input : normInputs) {
                normStatics.push_back(staticArgOf(input.node));
            }
            const auto &res =
                ops->resolve(withInputTypes, normInputTypes, normStatics, Modifier::None);
            if (!res.has_value()) {
                std::string argTypesStr = std::format(
                    "<{}> ({})",
                    strutil::join(withInputTypes, ", ", [](Type *t) { return t->toString(); }),
                    strutil::join(normInputTypes, ", ", [](Type *t) { return t->toString(); }));
                // An overload whose parameter kinds fit may know the precise reason (e.g. a
                // static tensor shape conflict); report that instead of "no match".
                std::vector<std::string> reasons;
                for (const auto &[uri, resolver] : ops->resolvers()) {
                    if (auto reason = resolver->explainRejection(
                            withInputTypes,
                            normInputTypes,
                            normStatics,
                            Modifier::None)) {
                        if (std::ranges::find(reasons, *reason) == reasons.end()) {
                            reasons.push_back(std::move(*reason));
                        }
                    }
                }
                if (!reasons.empty()) {
                    diags_->of(SemanticDiag::ArgumentsRejected)
                        .atOrigin(gct->load()->origin())
                        .commit(ops->name(), argTypesStr, strutil::join(reasons, "; "));
                    throw BuildAbortException();
                }
                std::string overloadsStr =
                    "\n    " +
                    strutil::join(
                        ops->resolvers(),
                        "\n    ",
                        [](const std::pair<std::string, resolver_ptr_t> &p) {
                            return "<" + p.first + ">: " + p.second->signature();
                        }) +
                    "\n";
                diags_->of(SemanticDiag::NoMatchingFunction)
                    .atOrigin(gct->load()->origin())
                    .commit(argTypesStr, overloadsStr);
                throw BuildAbortException();
            }
            targetOperator = *res;
            // An operator calls a function argument with values. A function with a `var`
            // parameter needs a variable there, which an operator does not have. A macro
            // operator (grad, value_and_grad, ...) does not call its argument: it rewrites the
            // function's graph into another function, whose `var` parameters are passed by
            // the caller of that function as usual.
            const auto takesVariable = [](Type *type) {
                if (type == nullptr || type->code() != TypeCode::Function) {
                    return false;
                }
                const auto *func = tt::as_ptr<FunctionType>(type);
                for (size_t i = 0; i < func->normTypesCount(); ++i) {
                    if (func->normIsVarAt(i)) {
                        return true;
                    }
                }
                for (size_t i = 0; i < func->withTypesCount(); ++i) {
                    if (func->withIsVarAt(i)) {
                        return true;
                    }
                }
                return false;
            };
            const bool callsArguments = !targetOperator->funcType()->modifiers().macro();
            for (const auto *inputs : {&withInputs, &normInputs}) {
                for (const Argument &input : *inputs) {
                    Type *type = valueTypeOf(nodeTypeOf(input.node));
                    if (callsArguments && takesVariable(type)) {
                        diags_->of(SemanticDiag::ArgumentsRejected)
                            .atOrigin(input.expr->load()->origin())
                            .commit(
                                ops->name(),
                                type->toString(),
                                "an operator calls a function argument with values, and a "
                                "function with a var parameter must be given a variable");
                        throw BuildAbortException();
                    }
                }
            }
            targetNode     = currGraph_->addOperNode(targetOperator, 0);
            // An operator whose resolved type is macro runs in std::macro once its inputs are
            // static, like a call of a macro function.
            if (targetOperator->funcType()->modifiers().macro()) {
                setNodeMacro(targetNode, true);
            }
            registerNodeOrigin(context_, targetNode, gct->at(0), "gir.link.oper");
            targetFuncType = targetOperator->funcType();
            // A result that the argument types fix (the shape of a tensor whose type carries
            // it) is a constant for the operators it is passed to, as a literal would be.
            if (const auto *folder =
                    camel::core::OperatorTypeFolderRegistry::instance().find(targetOperator->uri());
                folder != nullptr && withInputs.empty()) {
                Type *resultType = targetFuncType->exitType();
                std::optional<slot_t> value;
                try {
                    value = (*folder)(normInputTypes, normStatics, camel::core::mm::autoSpace());
                } catch (const std::exception &) {
                    value = std::nullopt;
                }
                if (value && resultType != nullptr) {
                    if (isGCTraced(resultType->code()) && *value != NullSlot) {
                        typeFoldedRoots_.emplace_back(
                            camel::core::mm::autoSpace(),
                            camel::core::rtdata::fromSlot<camel::core::rtdata::Object *>(*value),
                            resultType,
                            "gir.build.typefold");
                    }
                    typeFoldedValues_[targetNode] = *value;
                }
            }
        } else if (std::holds_alternative<graph_ptr_t>(drefTarget)) {
            auto decoratedGraph           = std::get<graph_ptr_t>(drefTarget);
            node_handle_t decoratorInvoke = lowerDecoratedGraphValue(decoratedGraph, gct);
            FunctionType *factoryType     = tt::as_ptr<FunctionType>(nodeTypeOf(decoratorInvoke));
            targetNode                    = currGraph_->addCallNode(factoryType->exitType());
            linkNodes(LinkType::With, decoratorInvoke, targetNode);
            targetFuncType = factoryType;
        } else {
            ASSERT(false, "Dref target must be graph set, operator group, or decorated graph.");
        }
    } else {
        Type *dataType = nodeTypeOf(targetNode);
        ASSERT(
            dataType->code() == TypeCode::Function,
            "Target node of LINK must be a function or operator node.");
        auto *funcType = tt::as_ptr<FunctionType>(dataType);
        StaticFuncTypeResolver resolver(funcType);
        if (!resolver.resolve(withInputTypes, normInputTypes, Modifier::None).has_value()) {
            reportConversionGaps(
                {{std::string(), funcType}},
                withInputs,
                normInputs,
                withInputTypes,
                normInputTypes,
                gct);
            std::string argTypesStr = std::format(
                "<{}> ({})",
                strutil::join(withInputTypes, ", ", [](Type *t) { return t->toString(); }),
                strutil::join(normInputTypes, ", ", [](Type *t) { return t->toString(); }));
            diags_->of(SemanticDiag::ArgumentsMismatch)
                .atOrigin(gct->load()->origin())
                .commit(funcType->toString(), argTypesStr);
            throw BuildAbortException();
        }
        node_handle_t invokeNode = currGraph_->addCallNode(funcType->exitType());
        linkNodes(LinkType::With, targetNode, invokeNode);
        std::vector<camel::source::origin_id_t> callInputs;
        if (auto *sourceContext = context_ ? context_->sourceContext().get() : nullptr) {
            auto calleeOrigin = sourceContext->resolveGirNodeOrigin(
                draftDebugKey(targetNode),
                std::string(nodeDebugEntityIdOf(targetNode)));
            if (calleeOrigin != camel::source::kInvalidOriginId) {
                callInputs.push_back(calleeOrigin);
            }
        }
        registerNodeOrigin(context_, invokeNode, gct, "gir.link.call", {}, std::move(callInputs));
        targetNode     = invokeNode;
        targetFuncType = funcType;
    }

    ASSERT(targetFuncType != nullptr, "Target function type must be resolved.");
    // A direct call is located at the call expression, not at the callee's definition.
    if (targetGraph && targetNode && targetNode->header.kind == camel::runtime::GCNodeKind::Func) {
        registerNodeOrigin(context_, targetNode, gct, "gir.link.func");
    }

    std::string targetName = targetGraph
                                 ? targetGraph->name()
                                 : (targetOperator ? "<" + targetOperator->name() + ">" : "");
    // A function value has no name of its own; its type stands for it in diagnostics.
    const std::string calleeLabel = targetName.empty() ? targetFuncType->toString() : targetName;

    // How an argument is passed. A function (a graph or a function value) and assignment take
    // a `var` argument as the variable itself. Any other operator with a `var` parameter mutates
    // an object in place and takes the object; the call still counts as a write of the variable
    // the object was read from. Every other parameter takes the value.
    const bool takesVariables =
        targetOperator == nullptr || targetOperator->name().starts_with("__assn");
    // The node to link and, at a `var` position, the variable the callee writes (if any).
    const auto passArgument = [&](const Argument &arg, bool isVar) {
        if (!isVar) {
            return std::pair{valueOf(arg.node), node_handle_t{}};
        }
        requireMutableArgument(arg, calleeLabel);
        const bool named = isVariable(arg.node);
        if (takesVariables) {
            // `s.x = e`, `arr[0] = e`, `f() = e`: the target is a temporary, and the store
            // would be lost without a trace.
            if (!named && targetOperator != nullptr) {
                diags_->of(SemanticDiag::AssignmentTargetNotVariable)
                    .atOrigin(arg.expr->load()->origin())
                    .commit();
                throw BuildAbortException();
            }
            // An expression that is not a name, passed to a function, is a fresh object
            // nobody else refers to.
            node_handle_t variable = named ? arg.node : newVariable(arg.node);
            return std::pair{variable, variable};
        }
        return std::pair{valueOf(arg.node), named ? arg.node : node_handle_t{}};
    };

    // All arguments are read before the call is recorded as a writer: a variable passed both
    // by value and to a `var` parameter (`f(c, c)`) is read before the call, not after it.
    struct Passed {
        node_handle_t node;
        node_handle_t written;
        LinkType link;
    };
    std::vector<Passed> passed;
    bool hasVarArgument = false;
    for (size_t i = 0; i < withInputs.size(); i++) {
        bool isVar = i < targetFuncType->withTypesCount() && targetFuncType->withIsVarAt(i);
        const auto [node, written] = passArgument(withInputs[i], isVar);
        passed.push_back({node, written, LinkType::With});
        hasVarArgument |= isVar;
    }
    for (size_t i = 0; i < normInputs.size(); i++) {
        bool isVar = i < targetFuncType->normTypesCount() && targetFuncType->normIsVarAt(i);
        const auto [node, written] = passArgument(normInputs[i], isVar);
        passed.push_back({node, written, LinkType::Norm});
        hasVarArgument |= isVar;
    }
    for (const Passed &arg : passed) {
        tryRemoveCtrlLink(arg.node, targetNode);
        linkNodes(arg.link, arg.node, targetNode);
        if (auto modifierNode = modifierOf(arg.node); modifierNode.has_value()) {
            if (sameGraph(*modifierNode, currGraph_) &&
                shouldAddSyncCtrlLink(*modifierNode, targetNode)) {
                linkNodes(LinkType::Ctrl, *modifierNode, targetNode);
            }
        }
    }
    // A function called directly brings its captured variables with it: the call uses each of
    // them, and writes those the function writes, exactly as if they were `var` arguments.
    std::vector<CapturedUse> captured;
    if (targetGraph) {
        captured = capturedUsesOf(targetGraph);
    }
    std::vector<std::string> capturedWrites;
    for (const CapturedUse &use : captured) {
        orderAfterLastWriter(use.variable, targetNode);
        if (use.written) {
            capturedWrites.push_back(use.name);
        }
    }
    if (!waited_ && !synced_) {
        const std::string subject =
            std::format("The call of '{}: {}'", targetName, targetFuncType->toString());
        if (hasVarArgument) {
            diags_->of(SemanticDiag::IgnoredSideEffect)
                .atOrigin(gct->load()->origin())
                .commit(subject, "it takes a var argument");
        } else if (!capturedWrites.empty()) {
            pendingIgnoredWrites_.push_back(
                {{targetGraph.get()}, capturedWrites, gct->load()->origin(), subject, "it writes"});
        }
    }
    for (const Passed &arg : passed) {
        if (arg.written != nullptr) {
            recordWrite(arg.written, targetNode);
        }
    }
    for (const CapturedUse &use : captured) {
        if (use.written) {
            recordWrite(use.variable, targetNode, targetNode, true);
        } else {
            pendingUses_[use.variable].push_back({targetNode, false});
        }
    }

    if (synced_) {
        if (lastSyncedNode_ != nullptr && shouldAddSyncCtrlLink(lastSyncedNode_, targetNode)) {
            linkNodes(LinkType::Ctrl, lastSyncedNode_, targetNode);
        }
        lastSyncedNode_ = targetNode;
    }
    LEAVE("LINK");
    return targetNode;
}

node_handle_t Builder::visitWithNode(const GCT::node_ptr_t &gct) {
    ENTER("WITH");
    any targetNodeRes = visit(gct->at(0));
    ASSERT(
        targetNodeRes.type() == typeid(node_handle_t),
        "Unexpected result type from Enter the child of WITH node.");
    node_handle_t targetNode = any_cast<node_handle_t>(targetNodeRes);
    vector<Argument> inputs;
    for (size_t i = 1; i < gct->size(); i++) {
        inputs.push_back({visitOperand(gct->at(i), gct), gct->at(i)});
    }
    auto &bound = boundWithArgs_[targetNode];
    bound.insert(bound.end(), inputs.begin(), inputs.end());
    LEAVE("WITH");
    return targetNode;
}

node_handle_t Builder::visitAccsNode(const GCT::node_ptr_t &gct) {
    ENTER("ACCS");
    any res = visit(gct->at(0));
    ASSERT(
        res.type() == typeid(node_handle_t),
        "Unexpected result type from Enter the child of ACCS node.");
    node_handle_t tgtNode = any_cast<node_handle_t>(res);
    ASSERT(tgtNode != nullptr, "Access node target is null.");
    if (!nodeTypeOf(tgtNode)->isComposite()) {
        diags_->of(SemanticDiag::TypeNotIndexable)
            .atOrigin(gct->load()->origin())
            .commit(nodeTypeOf(tgtNode)->toString());
        throw BuildAbortException();
    }

    const auto tgtType   = nodeTypeOf(tgtNode);
    const auto &accsLoad = gct->loadAs<GCT::AccsLoad>();
    Type *elemType       = nullptr;

    switch (tgtType->code()) {
    case TypeCode::Tuple: {
        const auto &tupleType = tt::as_ptr<TupleType>(tgtType);
        // A tuple has a fixed length: an element past its end is a compile-time error (it was
        // read unchecked, which crashed the compiler in release builds).
        if (accsLoad->index<size_t>() >= tupleType->size()) {
            diags_->of(SemanticDiag::InvalidAccessIndex)
                .atOrigin(gct->load()->origin())
                .commit(std::to_string(accsLoad->index<size_t>()));
            throw BuildAbortException();
        }
        elemType = tupleType->typeAt(accsLoad->index<size_t>());
        break;
    }
    case TypeCode::Array: {
        // `.N` reads a tuple element and `.name` a struct field. On an array no back end
        // implements either: the array was read as if it were a tuple, without a bounds
        // check, and the program printed whatever was there. An element is read with `[N]`.
        diags_->of(SemanticDiag::TypeNotIndexable)
            .atOrigin(gct->load()->origin())
            .commit(tgtType->toString());
        throw BuildAbortException();
    }
    case TypeCode::Struct: {
        const auto *strTy    = tt::as_ptr<camel::core::type::StructType>(tgtType);
        const auto &optIndex = strTy->findField(accsLoad->index<std::string>());
        if (!optIndex.has_value()) {
            diags_->of(SemanticDiag::InvalidAccessIndex)
                .atOrigin(gct->load()->origin())
                .commit(accsLoad->index<std::string>());
            throw BuildAbortException();
        }
        elemType = strTy->typeAt(optIndex.value());
        break;
    }
    default:
        ASSERT(false, "Unexpected target type in ACCS node.");
    }

    node_handle_t accsNode{};
    if (std::holds_alternative<size_t>(accsLoad->index())) {
        accsNode =
            currGraph_->addAccsNode(elemType, static_cast<uint32_t>(accsLoad->index<size_t>()));
    } else {
        accsNode = currGraph_->addAccsNode(elemType, accsLoad->index<std::string>());
    }
    linkNodes(LinkType::Norm, tgtNode, accsNode);
    registerNodeOrigin(context_, accsNode, gct, "gir.accs");
    LEAVE("ACCS");
    return accsNode;
}

node_handle_t Builder::visitBrchNode(const GCT::node_ptr_t &gct) {
    ENTER("BRCH");
    const auto &res = visit(gct->at(0));
    ASSERT(
        res.type() == typeid(node_handle_t),
        "Unexpected result type from Enter the child of BRCH node.");
    node_handle_t condNode = any_cast<node_handle_t>(res);
    node_handle_t joinNode = currGraph_->addJoinNode(nullptr, nullptr, 0);
    node_handle_t brchNode = currGraph_->addBrchNode(Type::Int64(), joinNode, {}, nullptr);
    registerNodeOrigin(context_, brchNode, gct, "gir.brch");
    registerNodeOrigin(
        context_,
        joinNode,
        gct,
        "gir.join",
        {semanticPart(
            camel::source::SemanticRole::ValueProducer,
            camel::source::kInvalidOriginId,
            -1,
            "join")},
        {});

    Type *joinType = nullptr;

    linkNodes(LinkType::Norm, condNode, brchNode);
    std::vector<runtime::GCBranchArm> branchArms;
    std::vector<node_handle_t> branchFuncs;
    // The variables the arms captured, and whether some arm writes them.
    std::vector<CapturedUse> captured;
    std::vector<DraftGraphBuilder *> armGraphs;

    for (size_t i = 1; i < gct->size(); i++) {
        const auto &caseNode         = gct->atAs<GCT::CaseLoad>(i);
        const auto &caseLoad         = caseNode->loadAs<GCT::CaseLoad>();
        GCT::node_ptr_t caseExecNode = nullptr;

        switch (caseLoad->caseType()) {
        case GCT::CaseLoad::CaseType::True:
            ASSERT(caseNode->size() == 1, "True case should have only one child.");
            caseExecNode = caseNode->atAs<GCT::ExecLoad>(0);
            break;
        case GCT::CaseLoad::CaseType::Else:
            ASSERT(i == gct->size() - 1, "Else case should be the last case.");
            ASSERT(caseNode->size() == 1, "Else case should have only one child.");
            caseExecNode = caseNode->atAs<GCT::ExecLoad>(0);
            break;
        case GCT::CaseLoad::CaseType::Value: {
            ASSERT(caseNode->size() == 2, "Value case should have two children.");
            any res = visit(caseNode->at(0));
            ASSERT(
                res.type() == typeid(node_handle_t),
                "Unexpected result type from visiting the case node.");
            node_handle_t valueNode = any_cast<node_handle_t>(res);
            linkNodes(LinkType::With, valueNode, brchNode);
            caseExecNode = caseNode->atAs<GCT::ExecLoad>(1);
            break;
        }
        default:
            ASSERT(false, "Unknown case type in BRCH node.");
        }

        graph_ptr_t subGraph = enterScope(FunctionType::create());
        registerGraphOrigin(context_, subGraph, caseExecNode, "gir.brch.case.graph");
        bodyStates_[subGraph.get()] = BodyState::Open;
        try {
            setBodyResult(subGraph, visitBodyNode(caseExecNode));
        } catch (...) {
            bodyStates_[subGraph.get()] = BodyState::Built;
            leaveScope();
            throw;
        }
        bodyStates_[subGraph.get()] = BodyState::Built;
        armGraphs_.insert(subGraph.get());
        builtBodies_.push_back(subGraph);
        leaveScope();
        armGraphs.push_back(subGraph.get());
        for (const CapturedUse &use : capturedUsesOf(subGraph)) {
            auto known = std::ranges::find(captured, use.variable, &CapturedUse::variable);
            if (known == captured.end()) {
                captured.push_back(use);
            } else {
                known->written |= use.written;
            }
        }

        currGraph_->addDependencyGraph(subGraph);
        Type *exitType = subGraph->funcType()->exitType();

        // Ensure all captured variables are ready before the BRCH node runs.
        // This keeps the nodes on every branch path between BRCH and JOIN
        // tightly grouped in topological order, with no unrelated external
        // nodes inserted in between.
        // That makes the graph scheduler easier to implement because it can
        // jump directly without frequent checks.
        for (draft_node_ref_t portId : subGraph->draft().closureNodes()) {
            const auto &refNode =
                resolveNodeByRef(subGraph->nodePortName(subGraph->draft().node(portId)));
            if (linkCheek(refNode, brchNode)) {
                linkNodes(LinkType::Ctrl, refNode, brchNode);
            }
        }

        auto funcNode = createFuncDataNode(subGraph, false, true);
        branchFuncs.push_back(funcNode);
        branchArms.push_back(runtime::GCBranchArm{
            .head = nodeIdOf(funcNode),
            .tail = nodeIdOf(funcNode),
        });

        if (joinType == nullptr) {
            joinType = exitType;
            setNodeType(joinNode, joinType);
        } else {
            // Arms must agree up to refinement: unify widens (for example two
            // tensors whose shapes differ in one extent) or fails.
            Type *unified = joinType->unify(exitType);
            if (unified != nullptr && unified != joinType) {
                joinType = unified;
                setNodeType(joinNode, joinType);
            }
            if (unified == nullptr) {
                diags_->of(SemanticDiag::BranchReturnTypeMismatch)
                    .atOrigin(gct->load()->origin())
                    .commit(
                        currGraph_->name() + ": " + currGraph_->funcType()->toString(),
                        joinType->toString(),
                        exitType->toString());
                throw BuildAbortException();
            }
        }

        linkNodes(LinkType::Ctrl, brchNode, funcNode);
        linkNodes(LinkType::With, funcNode, joinNode);
    }

    runtime::DraftBrchPayload payload{
        .join       = nodeIdOf(joinNode),
        .armCount   = static_cast<runtime::gc_cnt_t>(branchArms.size()),
        .defaultArm = runtime::kInvalidNodeRef};
    std::vector<std::byte> brchBytes(
        sizeof(runtime::DraftBrchPayload) + sizeof(runtime::GCBranchArm) * branchArms.size());
    std::memcpy(brchBytes.data(), &payload, sizeof(payload));
    if (!branchArms.empty()) {
        std::memcpy(
            brchBytes.data() + sizeof(payload),
            branchArms.data(),
            sizeof(runtime::GCBranchArm) * branchArms.size());
    }
    std::vector<draft_node_ref_t> brchNormInputs(
        normInputsOf(brchNode).begin(),
        normInputsOf(brchNode).end());
    std::vector<draft_node_ref_t> brchWithInputs(
        withInputsOf(brchNode).begin(),
        withInputsOf(brchNode).end());
    std::vector<draft_node_ref_t> brchCtrlInputs(
        ctrlInputsOf(brchNode).begin(),
        ctrlInputsOf(brchNode).end());
    std::vector<draft_node_ref_t> brchNormUsers(
        normUsersOf(brchNode).begin(),
        normUsersOf(brchNode).end());
    std::vector<draft_node_ref_t> brchWithUsers(
        withUsersOf(brchNode).begin(),
        withUsersOf(brchNode).end());
    std::vector<draft_node_ref_t> brchCtrlUsers(
        ctrlUsersOf(brchNode).begin(),
        ctrlUsersOf(brchNode).end());
    const auto *brchHeader   = nodeHeaderOf(brchNode);
    const auto brchDataIndex = brchHeader->dataIndex != 0
                                   ? brchHeader->dataIndex
                                   : currGraph_->addRuntimeSlot(Type::Int64());
    currGraph_->draft().rewriteNode(
        nodeIdOf(brchNode),
        runtime::DraftNodeInit{
            .dataIndex    = brchDataIndex,
            .dataType     = Type::Int64(),
            .runtimeFlags = brchHeader->runtimeFlags,
            .kind         = runtime::GCNodeKind::Brch,
            .payload      = std::span<const std::byte>(brchBytes.data(), brchBytes.size()),
            .normInputs   = brchNormInputs,
            .withInputs   = brchWithInputs,
            .ctrlInputs   = brchCtrlInputs,
            .normUsers    = brchNormUsers,
            .withUsers    = brchWithUsers,
            .ctrlUsers    = brchCtrlUsers});
    runtime::GCJoinBody joinBody{
        .brch     = nodeIdOf(brchNode),
        .armCount = static_cast<runtime::gc_cnt_t>(branchArms.size())};
    std::vector<draft_node_ref_t> joinNormInputs(
        normInputsOf(joinNode).begin(),
        normInputsOf(joinNode).end());
    std::vector<draft_node_ref_t> joinWithInputs(
        withInputsOf(joinNode).begin(),
        withInputsOf(joinNode).end());
    std::vector<draft_node_ref_t> joinCtrlInputs(
        ctrlInputsOf(joinNode).begin(),
        ctrlInputsOf(joinNode).end());
    std::vector<draft_node_ref_t> joinNormUsers(
        normUsersOf(joinNode).begin(),
        normUsersOf(joinNode).end());
    std::vector<draft_node_ref_t> joinWithUsers(
        withUsersOf(joinNode).begin(),
        withUsersOf(joinNode).end());
    std::vector<draft_node_ref_t> joinCtrlUsers(
        ctrlUsersOf(joinNode).begin(),
        ctrlUsersOf(joinNode).end());
    const auto *joinHeader = nodeHeaderOf(joinNode);
    const auto joinDataIndex =
        joinHeader->dataIndex != 0 ? joinHeader->dataIndex : currGraph_->addRuntimeSlot(joinType);
    currGraph_->draft().rewriteNode(
        nodeIdOf(joinNode),
        runtime::DraftNodeInit{
            .dataIndex    = joinDataIndex,
            .dataType     = joinType,
            .runtimeFlags = joinHeader->runtimeFlags,
            .kind         = runtime::GCNodeKind::Join,
            .payload      = std::as_bytes(std::span{&joinBody, 1}),
            .normInputs   = joinNormInputs,
            .withInputs   = joinWithInputs,
            .ctrlInputs   = joinCtrlInputs,
            .normUsers    = joinNormUsers,
            .withUsers    = joinWithUsers,
            .ctrlUsers    = joinCtrlUsers});

    linkNodes(LinkType::Norm, brchNode, joinNode);

    // Entering an arm is like calling it: the branch uses every variable an arm captured and
    // writes those an arm writes. It begins at BRCH and is complete at JOIN.
    std::vector<std::string> capturedWrites;
    for (const CapturedUse &use : captured) {
        orderAfterLastWriter(use.variable, brchNode);
        if (use.written) {
            capturedWrites.push_back(use.name);
        }
    }
    if (!capturedWrites.empty() && !waited_ && !synced_) {
        pendingIgnoredWrites_.push_back(
            {armGraphs, capturedWrites, gct->load()->origin(), "The branch", "an arm writes"});
    }
    for (const CapturedUse &use : captured) {
        if (use.written) {
            recordWrite(use.variable, brchNode, joinNode, true);
        } else {
            pendingUses_[use.variable].push_back({joinNode, false});
        }
    }

    if (synced_) {
        if (lastSyncedNode_ != nullptr && shouldAddSyncCtrlLink(lastSyncedNode_, brchNode)) {
            linkNodes(LinkType::Ctrl, lastSyncedNode_, brchNode);
        }
        lastSyncedNode_ = joinNode;
    }

    LEAVE("BRCH");
    return joinNode;
}

node_handle_t Builder::visitAnnoNode(const GCT::node_ptr_t &gct) {
    ENTER("ANNO");
    ASSERT(gct->size() == 1, "ANNO node should have exactly one child.");
    node_handle_t node = valueNodeOf(visit(gct->at(0)));
    LEAVE("ANNO");
    return node;
}

node_handle_t Builder::visitExitNode(const GCT::node_ptr_t &gct) {
    ENTER("EXIT");
    node_handle_t resNode      = valueNodeOf(visit(gct->at(0)));
    node_handle_t outputAnchor = resNode;
    node_vec_t pendingCtrlInputs;
    if (auto modifier = modifierOf(resNode); modifier.has_value()) {
        pendingCtrlInputs.push_back(*modifier);
    }
    if (synced_ && lastSyncedNode_ != nullptr) {
        pendingCtrlInputs.push_back(lastSyncedNode_);
    }

    if (!pendingCtrlInputs.empty() && !nodeIsKind(resNode, runtime::GCNodeKind::Gate)) {
        node_handle_t gatedValue = currGraph_->addGateNode(nodeTypeOf(resNode));
        linkNodes(LinkType::Norm, resNode, gatedValue);
        outputAnchor = gatedValue;
    }
    for (node_handle_t ctrlInput : pendingCtrlInputs) {
        if (ctrlInput != nullptr && linkCheek(ctrlInput, outputAnchor)) {
            linkNodes(LinkType::Ctrl, ctrlInput, outputAnchor);
        }
    }
    checkDeclaredReturn(currGraph_, nodeTypeOf(resNode), gct->load()->origin(), gct->at(0));
    setGraphReturnFromResult(
        currGraph_,
        nodeIdOf(outputAnchor),
        nodeIdOf(resNode),
        nodeTypeOf(resNode));

    LEAVE("EXIT");
    return resNode;
}

node_handle_t Builder::visitBodyNode(const GCT::node_ptr_t &gct) {
    const bool outerSynced        = std::exchange(synced_, false);
    const node_handle_t outerLast = std::exchange(lastSyncedNode_, {});
    node_handle_t result{};
    try {
        result = visitExecNode(gct);
    } catch (...) {
        synced_         = outerSynced;
        lastSyncedNode_ = outerLast;
        throw;
    }
    synced_         = outerSynced;
    lastSyncedNode_ = outerLast;
    return result;
}

void Builder::setBodyResult(const compile_graph_ptr_t &graph, node_handle_t result) {
    if (graph->draft().exitNode() != runtime::kInvalidNodeRef) {
        return;
    }
    if (result == nullptr) {
        result = graph->addStaticDataNode(Data::null());
        setGraphReturnFromResult(graph, nodeIdOf(result), nodeIdOf(result), Type::Void());
        return;
    }
    // A sync block that ends in a value hands it back behind a GATE (see visitExecNode): the
    // GATE is the output anchor, the value it forwards is what the graph returns.
    node_handle_t value = result;
    if (nodeIsKind(result, runtime::GCNodeKind::Gate) && !normInputsOf(result).empty()) {
        value = nodeDraftOf(result)->node(normInputsOf(result).front());
    }
    checkDeclaredReturn(graph, nodeTypeOf(value), 0);
    setGraphReturnFromResult(graph, nodeIdOf(result), nodeIdOf(value), nodeTypeOf(value));
}

void Builder::reportConversionGaps(
    const std::vector<std::pair<std::string, FunctionType *>> &candidates,
    const std::vector<Argument> &withInputs, const std::vector<Argument> &normInputs,
    const type_vec_t &withTypes, const type_vec_t &normTypes, const GCT::node_ptr_t &call) {
    // Reported only when exactly one overload is a conversion away: with several, which one
    // the program means is not the compiler's to choose.
    const std::pair<std::string, FunctionType *> *target = nullptr;
    std::string what, fixes;
    for (const auto &candidate : candidates) {
        FunctionType *funcType = candidate.second;
        if (funcType->withTypesCount() != withTypes.size() ||
            funcType->normTypesCount() != normTypes.size()) {
            continue;
        }
        bool fits = true;
        std::string graphWhat, graphFixes;
        const auto check = [&](const std::vector<Argument> &inputs,
                               const type_vec_t &types,
                               bool isWith) {
            for (size_t i = 0; i < types.size() && fits; ++i) {
                Type *want       = isWith ? funcType->withTypeAt(i) : funcType->normTypeAt(i);
                const bool isVar = isWith ? funcType->withIsVarAt(i) : funcType->normIsVarAt(i);
                std::vector<ConversionGap> gaps;
                if (!collectConversionGaps(want, valueTypeOf(types[i]), "", gaps)) {
                    fits = false;
                    return;
                }
                if (gaps.empty()) {
                    continue;
                }
                const size_t nameIndex = (isWith ? 0 : funcType->withTypesCount()) + i;
                const std::string name = nameIndex < funcType->argNamesCount()
                                             ? std::string(funcType->argNameAt(nameIndex))
                                             : std::string();
                const std::string text = sourceTextOf(inputs[i].expr);
                std::string subject =
                    std::string(isWith ? "with-argument " : "argument ") + std::to_string(i + 1);
                if (!text.empty() || !name.empty()) {
                    subject += " (" + (text.empty() ? "" : "'" + text + "'") +
                               (text.empty() || name.empty() ? "" : ", ") +
                               (name.empty() ? "" : "for parameter '" + name + "'") + ")";
                }
                graphWhat += (graphWhat.empty() ? "" : "; ") + describeGaps(subject, gaps);
                if (isVar) {
                    // A conversion yields a value; a var parameter takes the variable itself.
                    graphFixes += std::string(graphFixes.empty() ? "" : " ") +
                                  "No conversion applies to argument " + std::to_string(i + 1) +
                                  ": a var parameter takes the variable itself, and a variable " +
                                  "has the widened type of its first value. Pass the value to a " +
                                  "parameter that is not var, or declare the var parameter with " +
                                  "the variable's type and convert the value where it is read.";
                    continue;
                }
                std::string conversions;
                for (const ConversionGap &gap : gaps) {
                    conversions += (conversions.empty() ? "" : ", ") +
                                   conversionTextOf(text.empty() ? "<argument>" : text, gap);
                }
                graphFixes +=
                    std::string(graphFixes.empty() ? "" : " ") +
                    "Where the value is known to fit, write the conversion: " + conversions + ".";
            }
        };
        check(withInputs, withTypes, true);
        check(normInputs, normTypes, false);
        if (!fits || graphWhat.empty()) {
            continue;
        }
        if (target) {
            return;
        }
        target = &candidate;
        what   = std::move(graphWhat);
        fixes  = std::move(graphFixes);
    }
    if (!target) {
        return;
    }
    diags_->of(SemanticDiag::ArgumentNeedsConversion)
        .atOrigin(call->load()->origin())
        .commit(
            (target->first.empty() ? "" : target->first + ": ") + target->second->toString(),
            what,
            fixes);
    throw BuildAbortException();
}

void Builder::checkDeclaredReturn(
    const compile_graph_ptr_t &graph, Type *resultType, uint64_t origin,
    const GCT::node_ptr_t &value) {
    // Only a DECLARED type is checked: an undeclared one is set from the first value returned,
    // after which hasExitType() is true as well, so the check runs before that happens.
    if (graph == nullptr || graph->draft().exitNode() != runtime::kInvalidNodeRef) {
        return;
    }
    FunctionType *funcType = graph->funcType();
    if (funcType == nullptr || !funcType->hasExitType() || resultType == nullptr) {
        return;
    }
    Type *declared = funcType->exitType();
    // Not judged here: a body without a value (its last statement decides nothing about the
    // type), and types that are not resolved yet.
    if (declared == nullptr || declared == resultType || resultType->code() == TypeCode::Void ||
        declared->code() == TypeCode::Ref || resultType->code() == TypeCode::Ref) {
        return;
    }
    if (declared->assignableFrom(resultType)) {
        return;
    }
    std::vector<ConversionGap> gaps;
    const bool convertible = collectConversionGaps(declared, resultType, "", gaps) && !gaps.empty();
    auto diag              = diags_->of(
        convertible ? SemanticDiag::ReturnNeedsConversion : SemanticDiag::ReturnTypeMismatch);
    if (origin != 0) {
        diag.atOrigin(origin);
    } else if (auto sourceContext = context_ ? context_->sourceContext() : nullptr) {
        diag.atOrigin(sourceContext->debugMap().graphOrigin(graph->stableId()));
    }
    if (convertible) {
        const std::string text = sourceTextOf(value);
        std::string fixes;
        for (const ConversionGap &gap : gaps) {
            fixes += (fixes.empty() ? "" : ", ") +
                     conversionTextOf(text.empty() ? "<returned expression>" : text, gap);
        }
        diag.commit(
            declared->toString(),
            describeGaps(text.empty() ? "the value" : "the value ('" + text + "')", gaps),
            fixes,
            funcType->toString());
        throw BuildAbortException();
    }
    diag.commit(resultType->toString(), declared->toString(), funcType->toString());
    throw BuildAbortException();
}

node_handle_t Builder::visitExecNode(const GCT::node_ptr_t &gct) {
    ENTER("EXEC");
    const auto &execLoad      = gct->loadAs<GCT::ExecLoad>();
    bool old                  = synced_;
    node_handle_t oldFuncNode = lastSyncedNode_;
    // A scope the lowering introduced (the operands of a composite literal, the names of a
    // declaration) is not a block: in a sync block it continues the chain, in written order,
    // like the arguments of a call statement. So does a sync block written inside a sync
    // block: its expressions are expressions of the enclosing block, in the same order.
    const bool operands = execLoad->operands() || (synced_ && execLoad->synced());
    if (!operands) {
        synced_         = execLoad->synced();
        lastSyncedNode_ = {};
    }
    node_handle_t res{};
    for (size_t i = 0; i < gct->size(); i++) {
        try {
            const auto &child = gct->at(i);
            any result        = visit(child);
            if (result.has_value() && result.type() == typeid(node_handle_t)) {
                res = any_cast<node_handle_t>(result);
            } else if (
                result.type() == typeid(graph_ptr_t) && child->type() == GCT::LoadType::FUNC &&
                child->loadAs<GCT::FuncLoad>()->name().empty()) {
                // A function literal is a value like any other expression, e.g. a branch arm.
                res = valueNodeOf(result);
            }
        } catch (const BuildAbortException &e) {
            continue;
        }
    }
    if (!operands) {
        // A sync block that is not part of an enclosing chain: whoever takes its value takes
        // it after the block's last expression (a `return` has already done this for the
        // graph's exit). Without this the chain of a block ending in a plain value was not
        // reachable from anything and the block's effects were dropped.
        const bool returned =
            gct->size() > 0 && gct->at(gct->size() - 1)->type() == GCT::LoadType::EXIT;
        if (synced_ && !returned && res != nullptr && lastSyncedNode_ != nullptr &&
            lastSyncedNode_ != res && sameGraph(res, currGraph_)) {
            node_handle_t gate = currGraph_->addGateNode(nodeTypeOf(res));
            linkNodes(LinkType::Norm, res, gate);
            linkNodes(LinkType::Ctrl, lastSyncedNode_, gate);
            res = gate;
        }
        synced_         = old;
        lastSyncedNode_ = oldFuncNode;
    }
    LEAVE("EXEC");
    return res;
}

void_ptr_t Builder::visitExptNode(const GCT::node_ptr_t &gct) {
    ENTER("EXPT");
    const auto &exptLoad = gct->loadAs<GCT::ExptLoad>();
    const auto &exports  = exptLoad->exports();
    for (const Reference &ref : exports) {
        auto optType = typeDecls_.find(ref);
        if (optType != typeDecls_.end()) {
            module_->exportType(ref, optType->second);
            continue;
        }
        auto optDecorated = decoratedGraphAt(ref.toString());
        if (optDecorated.has_value()) {
            module_->exportEntity(
                ref,
                camel::core::module::detail::EntityAccess::makeDecoratedGraph(
                    eraseCompileGraph(optDecorated.value())));
            continue;
        }
        auto optNode = nodeAt(ref);
        if (optNode.has_value()) {
            auto *exportedNode = new node_handle_t(optNode.value());
            module_->exportEntity(
                ref,
                camel::core::module::detail::EntityAccess::makeNode(
                    static_cast<camel::core::module::detail::EntityAccess::node_handle_t>(
                        exportedNode)));
            continue;
        }
        auto optGraph = graphsAt(ref);
        if (optGraph.has_value()) {
            module_->exportEntity(
                ref,
                camel::core::module::detail::EntityAccess::makeGraphSet(
                    eraseCompileGraphSet(optGraph.value())));
            continue;
        }
        diags_->of(SemanticDiag::UnresolvedReference)
            .atOrigin(gct->load()->origin())
            .commit(ref.toString());
        throw BuildAbortException();
    }
    LEAVE("EXPT");
    return nullptr;
}

} // namespace camel::compile::gir
