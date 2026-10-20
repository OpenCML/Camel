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
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "builder.h"
#include "builder_support.h"

#include "camel/compile/gir/static_function.h"
#include "camel/core/numeric_convert.h"
#include "camel/core/operator_names.h"
#include "camel/core/rtdata/string.h"
#include "camel/execute/entry_args.h"
#include "camel/runtime/control.h"
#include "camel/runtime/graph.h"
#include "camel/utils/log.h"
#include "camel/utils/scope.h"
#include "camel/utils/str.h"
#include "camel/utils/type.h"
#include "core/module/entity_internal.h"

#include "camel/core/operator_traits.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/other.h"

#include <atomic>
#include <cctype>
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
            graph->setExitType(Type::Void());
        }
        return;
    }

    graph->draft().setReturnNode(valueNode, runtime::GCReturnKind::Self);
    if (!graph->funcType()->hasExitType()) {
        graph->setExitType(resultType);
    }
}

camel::source::gir_draft_node_key_t draftDebugKey(node_handle_t node) {
    return node ? static_cast<camel::source::gir_draft_node_key_t>(node->header.selfId) : 0;
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
            const bool isVar           = funcType->withIsVarAt(i);
            Type *type                 = funcType->withTypeAt(i);
            graph->addPortNode(isVar ? TupleType::cell(type) : type, portName, true, isVar);
        }
        for (size_t i = 0; i < funcType->normTypesCount(); ++i) {
            const size_t argIndex      = funcType->withTypesCount() + i;
            const std::string portName = argIndex < funcType->argNamesCount()
                                             ? std::string(funcType->argNameAt(argIndex))
                                             : std::format("__n{}", i);
            const bool isVar           = funcType->normIsVarAt(i);
            Type *type                 = funcType->normTypeAt(i);
            graph->addPortNode(isVar ? TupleType::cell(type) : type, portName, false, isVar);
        }
    }
    return graph;
}

// How a program writes the operator behind an operator group (`__assn_add__` is `+=`), the
// table of the operators that are calls of a function.
using camel::core::displayNameOf;
using camel::core::operatorSymbolOf;
} // namespace

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
    return linkCheek(from, to) && !hasDirectDataDependency(from, to) &&
           !runtime::hasControlDependency(*nodeDraftOf(to), nodeIdOf(from), nodeIdOf(to));
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
    auto origin = gct->origin();
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

void Builder::ensureRootGraph() {
    if (rootGraph_) {
        return;
    }
    rootGraph_ = createCompileGraph(FunctionType::create(), nullptr, "__root__");
    if (auto sourceContext = context_ ? context_->sourceContext() : nullptr) {
        rootGraph_->setExtra<camel::source::SourceContext, kSourceContextExtraIndex>(
            sourceContext.get());
    }
}

void Builder::buildEntryCall() {
    compile_graph_ptr_t entryGraph = nullptr;
    if (auto decoratedMain = decoratedGraphAt("main")) {
        entryGraph = *decoratedMain;
    } else if (auto gv = graphsAt("main"); gv.has_value() && !gv.value()->empty()) {
        entryGraph = gv.value()->front();
    }
    if (entryGraph) {
        // The entry graph stands for the entry function where a diagnostic is about the
        // program as a whole (a translation pass that finds nothing to translate): it
        // shares the origin of `main`, also after passes have inlined `main` into it.
        if (auto sourceContext = context_ ? context_->sourceContext() : nullptr) {
            const auto mainOrigin = sourceContext->debugMap().graphOrigin(entryGraph->stableId());
            if (mainOrigin != camel::source::kInvalidOriginId) {
                sourceContext->debugMap().registerGraphOrigin(rootGraph_->stableId(), mainOrigin);
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
    } else if (context_->requireEntry()) {
        diags_->of(SemanticDiag::EntryModuleMissingMain).commit(module_->name());
        throw BuildAbortException();
    } else {
        // A library module (no `main`) has no entry call. The root graph still needs a
        // well-formed exit: the graph dumps (std::gir, std::jgir, `inspect -g`) and every
        // reachability walk end at it, and an invalid exit is not a graph one can encode.
        // Nothing executes this root, so the exit is an empty (void) return.
        auto empty = rootGraph_->addStaticDataNode(Data::null());
        setGraphReturnFromResult(rootGraph_, nodeIdOf(empty), nodeIdOf(empty), Type::Void());
    }
}

compile_graph_ptr_t Builder::enterScope(FunctionType *funcType, const std::string &name) {
    std::string scopeName = name;
    if (name.empty()) {
        const bool atRoot = currGraph_ == rootGraph_;
        currGraph_        = createCompileGraph(funcType, currGraph_);
        if (atRoot) {
            rootSubGraphOwners_[currGraph_.get()] = unit_;
        }
    } else {
        // The functions of one name declared in one block are overloads, one graph each. A
        // graph is found again (the forward declaration of a module-level function, then its
        // definition) by the function type object the declaration carries, never by the name
        // alone: by name the second overload was built into the graph of the first.
        compile_graph_ptr_t found;
        size_t index = 0;
        if (auto graphs = graphScope_->get(name, false); graphs.has_value()) {
            for (; index < graphs.value()->size(); ++index) {
                if (graphs.value()->at(index)->funcType() == funcType) {
                    found = graphs.value()->at(index);
                    break;
                }
            }
        }
        if (!found) {
            found = createCompileGraph(funcType, currGraph_, name);
            insertGraph(name, found);
            if (currGraph_ == rootGraph_) {
                rootSubGraphOwners_[found.get()] = unit_;
            }
        }
        currGraph_ = found;
        // Each overload has its own scope: they may name their parameters alike.
        if (index > 0) {
            scopeName = std::format("{}#{}", name, index);
        }
    }
    enterGraphScope(currGraph_, scopeName);
    return currGraph_;
}

void Builder::rejectRedeclaredFunction(
    const std::string &name, FunctionType *funcType, const GCT::node_ptr_t &gct) {
    if (name.empty()) {
        return;
    }
    if (redeclaredFuncTypes_.contains(funcType)) {
        // Reported at the forward declaration of this function.
        throw BuildAbortException();
    }
    auto graphs = graphScope_->get(name, false);
    if (!graphs.has_value()) {
        return;
    }
    for (const auto &graph : *graphs.value()) {
        if (graph->funcType() == funcType) {
            return; // this very function, declared ahead
        }
    }
    // Overloads are told apart by their parameter types alone: a call has nothing else to
    // choose by.
    const auto sameParameters = [](FunctionType *a, FunctionType *b) {
        if (a->withTypesCount() != b->withTypesCount() ||
            a->normTypesCount() != b->normTypesCount()) {
            return false;
        }
        for (size_t i = 0; i < a->withTypesCount(); ++i) {
            if (!a->withTypeAt(i)->equals(b->withTypeAt(i))) {
                return false;
            }
        }
        for (size_t i = 0; i < a->normTypesCount(); ++i) {
            if (!a->normTypeAt(i)->equals(b->normTypeAt(i))) {
                return false;
            }
        }
        return true;
    };
    for (const auto &graph : *graphs.value()) {
        if (sameParameters(graph->funcType(), funcType)) {
            redeclaredFuncTypes_.insert(funcType);
            diags_->of(SemanticDiag::Redeclaration).atOrigin(gct->origin()).commit(name);
            throw BuildAbortException();
        }
    }
}

void Builder::enterGraphScope(const compile_graph_ptr_t &graph, const std::string &name) {
    currGraph_ = graph;
    if (auto sourceContext = context_ ? context_->sourceContext() : nullptr) {
        currGraph_->setExtra<camel::source::SourceContext, kSourceContextExtraIndex>(
            sourceContext.get());
    }
    nodeScope_      = nodeScope_->enter(name);
    graphScope_     = graphScope_->enter(name);
    decoratedScope_ = decoratedScope_->enter(name);
    templateScope_  = templateScope_->enter(name);
}

void Builder::leaveScope() {
    nodeScope_      = nodeScope_->leave();
    graphScope_     = graphScope_->leave();
    decoratedScope_ = decoratedScope_->leave();
    templateScope_  = templateScope_->leave();
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
    if (auto graphs = graphScope_->get(name, false); graphs.has_value()) {
        graphs.value()->push_back(graph);
        return true;
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
        if (isPoisonedName(name)) {
            throw BuildAbortException();
        }
        auto sourceContext = context_ ? context_->sourceContext() : nullptr;
        auto origin        = (sourceContext && currGraph_)
                                 ? sourceContext->debugMap().graphOrigin(currGraph_->stableId())
                                 : camel::source::kInvalidOriginId;
        diags_->of(SemanticDiag::UnresolvedReference).atOrigin(origin).commit(name);
        throw BuildAbortException();
    }
    node_handle_t node = optSrcNode.value();

    // A value of an enclosing graph (a module's top level included) is captured.
    return captureInto(currGraph_, node, name);
}

bool Builder::isPoisonedName(const std::string &name) const {
    if (poisonedNames_.empty()) {
        return false;
    }
    for (node_scope_ptr_t scope = nodeScope_; scope; scope = scope->outer()) {
        auto it = poisonedNames_.find(scope);
        if (it != poisonedNames_.end() && it->second.contains(name)) {
            return true;
        }
    }
    return false;
}

namespace {
// Reports that the name at `gct` stands for the functions `graphs`.
void reportFunctionReference(
    camel::compile::SemanticRecorder *recorder, const GCT::node_ptr_t &gct, const std::string &name,
    const std::optional<graph_set_ptr_t> &graphs) {
    std::vector<std::string> ids;
    if (graphs.has_value() && *graphs) {
        for (const auto &g : **graphs) {
            if (g) {
                ids.push_back(g->stableId());
            }
        }
    }
    recorder->functionReferenced(gct->origin(), name, std::move(ids));
}
} // namespace

void Builder::recordDeclaration(
    const std::string &name, node_handle_t node, const GCT::node_ptr_t &decl) {
    const auto origin                    = decl->origin();
    declOrigins_[nodeScope_.get()][name] = origin;
    recorder_->valueDeclared(origin, name, valueTypeOf(nodeTypeOf(node)), isVariable(node));
    recorder_->valueBound(
        origin,
        name,
        currGraph_ ? currGraph_->stableId() : std::string(),
        node->header.origin,
        nodeTypeOf(node),
        isVariable(node));
}

camel::source::origin_id_t Builder::declarationOf(const std::string &name) const {
    // A capture port is entered into the scopes between the reference and the declaration
    // without a record: the first scope that has one is the scope of the declaration.
    for (node_scope_ptr_t scope = nodeScope_; scope; scope = scope->outer()) {
        if (!scope->has(name, false)) {
            continue;
        }
        if (auto it = declOrigins_.find(scope.get()); it != declOrigins_.end()) {
            if (auto found = it->second.find(name); found != it->second.end()) {
                return found->second;
            }
        }
    }
    return camel::source::kInvalidOriginId;
}

void Builder::recordResult(const GCT::node_ptr_t &gct, const std::any &res) {
    if (res.type() != typeid(node_handle_t)) {
        return;
    }
    node_handle_t node = any_cast<node_handle_t>(res);
    if (node == nullptr || nodeIsKind(node, runtime::GCNodeKind::Dref)) {
        return;
    }
    recorder_->expressionTyped(gct->origin(), valueTypeOf(nodeTypeOf(node)));
}

any Builder::visit(const GCT::node_ptr_t &node) {
    if (recorder_ == nullptr) [[likely]] {
        return dispatch(node);
    }
    any res = dispatch(node);
    recordResult(node, res);
    return res;
}

any Builder::dispatch(const GCT::node_ptr_t &node) {
    switch (node->type()) {
    case GCT::NodeKind::DECL:
        return visitDeclNode(node);
    case GCT::NodeKind::FUNC:
        if (node->name().empty() && isGenericHere(node->at(0)->dataType())) {
            return anonymousTemplateOf(node);
        }
        return visitFuncNode(node);
    case GCT::NodeKind::DATA:
        return visitDataNode(node);
    case GCT::NodeKind::TYPE:
        return visitTypeNode(node);
    case GCT::NodeKind::NREF:
        return visitNRefNode(node);
    case GCT::NodeKind::DREF:
        return visitDRefNode(node);
    case GCT::NodeKind::VARI:
        return visitVariNode(node);
    case GCT::NodeKind::WAIT:
        return visitWaitNode(node);
    case GCT::NodeKind::LINK:
        return visitLinkNode(node);
    case GCT::NodeKind::WITH:
        return visitWithNode(node);
    case GCT::NodeKind::TAPP:
        return visitTypeApplication(node);
    case GCT::NodeKind::ACCS:
        return visitAccsNode(node);
    case GCT::NodeKind::BRCH:
        return visitBrchNode(node);
    case GCT::NodeKind::ANNO:
        return visitAnnoNode(node);
    case GCT::NodeKind::EXIT:
        return visitExitNode(node);
    case GCT::NodeKind::EXEC:
        return visitExecNode(node);
    case GCT::NodeKind::EXPT:
        // The export list is read when the module is declared (exportDeclarations).
        return nullptr;
    case GCT::NodeKind::CAST:
        return visitCastNode(node);
    case GCT::NodeKind::LOOP:
        return visitLoopNode(node);
    default:
        ASSERT(false, "Unknown GCT NodeType");
    }
    return nullptr;
}

void_ptr_t Builder::visitDeclNode(const GCT::node_ptr_t &gct) {
    ENTER("DECL");
    GCT::node_ptr_t typeNode = gct->at(0);
    Type *type               = typeNode->dataType();
    if (!gct->isFunc()) {
        unit_->typeDecls[Reference(gct->name())] = type;
        LEAVE("DECL");
        return nullptr;
    }

    if (isGenericHere(type)) {
        // A generic function is a template: its graphs are its instances.
        LEAVE("DECL");
        return nullptr;
    }
    FunctionType *funcType = tt::as_ptr<FunctionType>(type);
    rejectRedeclaredFunction(gct->name(), funcType, gct);
    rejectOperatorConflict(gct->name(), funcType, gct);
    compile_graph_ptr_t graph = enterScope(funcType, gct->name());
    leaveScope();

    LEAVE("DECL");
    return nullptr;
}

compile_graph_ptr_t Builder::visitFuncNode(const GCT::node_ptr_t &gct) {
    ENTER("FUNC");
    ExpectedValueScope functionScope(expectedValueType_, nullptr);
    std::string name         = gct->name();
    GCT::node_ptr_t typeLoad = gct->at(0);
    Type *type               = typeLoad->dataType();
    FunctionType *funcType   = tt::as_ptr<FunctionType>(type);
    if (isGenericHere(type)) {
        // A generic function: registered as a template when its block was entered
        // (registerGenericTemplates), built when a call asks for an instance.
        LEAVE("FUNC");
        return nullptr;
    }
    if (instantiationDepth_ > 0) {
        // Inside an instance every function and lambda of the body is built once per
        // instance, and a graph completes its function type in place: each needs its own,
        // with the type variables replaced.
        funcType = instantiateFunctionType(funcType, typeBindings_);
    }
    rejectRedeclaredFunction(name, funcType, gct);
    rejectOperatorConflict(name, funcType, gct);
    staticGrantsOf(gct); // its decorators are resolved here, where they are written
    compile_graph_ptr_t graph = enterScope(funcType, name);
    buildFuncBody(gct, graph, funcType, name);
    if (name.empty()) {
        lambdaGraphs_[gct] = graph.get();
    }
    leaveScope();

    // The decorators that grant attributes as the program is built were applied to the body
    // (buildFuncBody); the others make the decorated function. A decorated block applies its
    // own where it is called (visitDecoratedBlock).
    const std::vector<GCT::node_ptr_t> annoNodes =
        gct->decoratedBlock() ? std::vector<GCT::node_ptr_t>{} : appliedDecoratorsOf(gct);
    if (!annoNodes.empty()) {
        graph_ptr_t decorated = buildDecoratedGraph(name, graph, annoNodes);
        insertDecoratedGraph(name, decorated);
        decoratedOf_[graph.get()] = decorated;
    }

    LEAVE("FUNC");
    return graph;
}

void Builder::stampPermissions(node_handle_t operNode) const {
    if (operNode != nullptr) {
        operNode->header.permissions = numericPermissions_;
        operNode->header.attrs       = currentAttrs_;
    }
}

void Builder::buildFuncBody(
    const GCT::node_ptr_t &gct, const compile_graph_ptr_t &graph, FunctionType *funcType,
    const std::string &name) {
    // The function's graph has the permissions of its own body (which include those of the
    // code it is written in), not those of the place that asked for it to be built (a call
    // that instantiates a generic function, a block that declares it).
    const GCT::node_ptr_t body = gct->size() > 1 ? gct->at(1) : nullptr;
    NumericPermissionScope permissions(
        numericPermissions_,
        body && body->type() == GCT::NodeKind::EXEC ? body->numericPermissions() : 0);
    // The attributes its decorators grant join those in force where it is written (each key's
    // combine rule, camel/core/node_attrs.h), for everything written in it.
    AttrScope attrs(
        currentAttrs_,
        camel::core::attr::unite(currentAttrs_, staticGrantsOf(gct).first));
    try {
        registerGraphOrigin(context_, graph, gct, "gir.func.graph");
        const auto withPorts = graph->draft().withPorts();
        for (size_t i = 0; i < withPorts.size(); ++i) {
            node_handle_t port = graph->draft().node(withPorts[i]);
            insertNode(graph->nodePortName(port), port);
            if (i < funcType->withTypesCount() && funcType->withIsVarAt(i)) {
                variables_.insert(port);
            }
            if (recorder_) {
                recordDeclaration(graph->nodePortName(port), port, gct);
            }
        }
        const auto normPorts = graph->draft().normPorts();
        for (size_t i = 0; i < normPorts.size(); ++i) {
            node_handle_t port = graph->draft().node(normPorts[i]);
            insertNode(graph->nodePortName(port), port);
            if (i < funcType->normTypesCount() && funcType->normIsVarAt(i)) {
                variables_.insert(port);
            }
            if (recorder_) {
                recordDeclaration(graph->nodePortName(port), port, gct);
            }
        }
        bodyStates_[graph.get()] = BodyState::Open;
        if (!funcType->hasExitType()) {
            inferredExitGraphs_.insert(graph.get()); // the body sets it from now on
        }
        // A decorated block whose value nothing takes is a function whose value is dropped:
        // it returns nothing, and a `return` in it gives no value (visitExitNode). Not a sync
        // block: its statements run in order whatever is done with its value, as they do
        // when the block is written without decorators.
        const bool dropped = droppedBodies_.contains(gct->at(1)) && !gct->at(1)->synced();
        if (dropped) {
            droppedGraphs_.insert(graph.get());
        }
        node_handle_t bodyResult = visitBodyNode(gct->at(1));
        setBodyResult(graph, dropped ? nullptr : bodyResult);
        // A body declared `: void` drops the value of its last statement.
        if (!inferredExitGraphs_.contains(graph.get()) &&
            isVoidGraphExitType(funcType->exitType())) {
            rejectDiscardedValue(graph.get());
        }
        bodyStates_[graph.get()] = BodyState::Built;
        unit_->builtFunctions.push_back({graph.get(), gct->origin(), name});
        unit_->builtBodies.push_back(graph);
        if (recorder_) {
            recorder_->functionBuilt(gct->origin(), name, graph->stableId(), funcType);
        }
    } catch (const camel::core::mm::MemoryExhausted &e) {
        // Say which function was being compiled; the allocator does not know.
        bodyStates_[graph.get()] = BodyState::Built;
        leaveScope();
        throw e.during(
            std::format("compiling function '{}'", name.empty() ? graph->name() : name),
            gct->origin());
    } catch (...) {
        // The caller entered the scope of `graph`; an error leaves it on the way out.
        bodyStates_[graph.get()] = BodyState::Built;
        failedBodies_.insert(graph.get());
        leaveScope();
        throw;
    }
}

void Builder::registerGenericTemplates(const GCT::node_ptr_t &exec) {
    std::unordered_set<std::string> plainNames;
    for (size_t i = 0; i < exec->size(); ++i) {
        const auto &child = exec->at(i);
        if (child->type() != GCT::NodeKind::FUNC) {
            continue;
        }
        const std::string name = child->name();
        Type *type             = child->at(0)->dataType();
        if (!name.empty() && !isGenericHere(type)) {
            plainNames.insert(name);
        }
    }
    for (size_t i = 0; i < exec->size(); ++i) {
        const auto &child = exec->at(i);
        if (child->type() != GCT::NodeKind::FUNC) {
            continue;
        }
        const std::string name = child->name();
        Type *type             = child->at(0)->dataType();
        if (name.empty() || !isGenericHere(type)) {
            continue;
        }
        // One name is either a generic function or a set of ordinary ones: which of two
        // generic functions, or of a generic and an ordinary one, a call means is not defined.
        // An operator is the exception: its overloads, generic or not, are one set, and a call
        // that more than one of them accepts is an error (chooseOperatorOverload).
        const bool isOperator = camel::core::isOverloadableOperatorGroup(name);
        if (!isOperator && (templateScope_->has(name, false) || plainNames.contains(name))) {
            diags_->of(SemanticDiag::Redeclaration).atOrigin(child->origin()).commit(name);
            continue;
        }
        if (!appliedDecoratorsOf(child).empty()) {
            diags_->of(SemanticDiag::GenericFunctionUnsupported)
                .atOrigin(child->origin())
                .commit(name, "decorated: a decorator takes a function value");
        }
        auto tmpl = std::make_shared<GenericTemplate>();
        // An operator's template is named as the program writes it (`operator +`), in its
        // diagnostics and in the names of its instances.
        tmpl->name           = isOperator ? displayNameOf(name) : name;
        tmpl->func           = child;
        tmpl->funcType       = tt::as_ptr<FunctionType>(boundType(type));
        tmpl->outerBindings  = typeBindings_;
        tmpl->ownerGraph     = currGraph_;
        tmpl->module         = module_;
        tmpl->nodeScope      = nodeScope_;
        tmpl->graphScope     = graphScope_;
        tmpl->decoratedScope = decoratedScope_;
        tmpl->templateScope  = templateScope_;
        tmpl->attrs          = currentAttrs_;
        // A generic function of a namespace (registered at the top level) is in that
        // namespace; one written in a body is where the body is.
        tmpl->namespacePath = currGraph_ == rootGraph_ ? Reference(name).paths() : namespacePath_;
        tmpl->usingFrames   = usingFrames_;
        if (const auto *usings = GCT::Tree::of(child)->usingsOf(child)) {
            tmpl->usingFrames.push_back(usings);
        }
        if (isOperator) {
            operatorTemplates_[name].push_back({templateScope_, tmpl});
        } else {
            templateScope_->insert(name, tmpl);
        }
    }
}

node_handle_t Builder::anonymousTemplateOf(const GCT::node_ptr_t &func) {
    Type *type             = func->at(0)->dataType();
    auto tmpl              = std::make_shared<GenericTemplate>();
    tmpl->func             = func;
    tmpl->funcType         = tt::as_ptr<FunctionType>(boundType(type));
    tmpl->outerBindings    = typeBindings_;
    tmpl->ownerGraph       = currGraph_;
    tmpl->module           = module_;
    tmpl->nodeScope        = nodeScope_;
    tmpl->graphScope       = graphScope_;
    tmpl->decoratedScope   = decoratedScope_;
    tmpl->templateScope    = templateScope_;
    tmpl->attrs            = currentAttrs_;
    tmpl->namespacePath    = namespacePath_;
    tmpl->usingFrames      = usingFrames_;
    node_handle_t drefNode = currGraph_->addDrefNode(
        std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>{
            std::make_shared<graph_set_t>()});
    genericDrefs_[drefNode] = tmpl;
    anonymousTemplates_.push_back(tmpl);
    registerNodeOrigin(context_, drefNode, func, "gir.dref.generic");
    return drefNode;
}

void Builder::rejectGenericValue(
    const template_ptr_t &tmpl, const GCT::node_ptr_t &origin, const std::string &why) {
    diags_->of(SemanticDiag::GenericFunctionAsValue)
        .atOrigin(origin->origin())
        .commit(tmpl->name, why, tmpl->name + ": " + tmpl->funcType->toString());
    throw BuildAbortException();
}

compile_graph_ptr_t Builder::instantiateGenericAs(
    const template_ptr_t &tmpl, FunctionType *expected, const GCT::node_ptr_t &origin,
    std::string &why) {
    FunctionType *signature = tmpl->funcType;
    const size_t withCount  = signature->withTypesCount();
    const size_t normCount  = signature->normTypesCount();
    if (withCount != expected->withTypesCount() || normCount != expected->normTypesCount()) {
        why = std::format(
            "it takes {} parameter(s) and {} context parameter(s), and a function of type '{}' "
            "is expected",
            normCount,
            withCount,
            expected->toString());
        return nullptr;
    }
    const auto patternAt = [&](size_t i) {
        return i < withCount ? signature->withTypeAt(i) : signature->normTypeAt(i - withCount);
    };
    const auto expectedAt = [&](size_t i) {
        return i < withCount ? expected->withTypeAt(i) : expected->normTypeAt(i - withCount);
    };
    type_bindings_t bindings;
    for (size_t i = 0; i < withCount + normCount; ++i) {
        if (!unifyTypeVars(patternAt(i), expectedAt(i), bindings)) {
            why = std::format(
                "its parameter {}, '{}', does not have the form of '{}' in the function "
                "expected here, '{}'",
                i + 1,
                patternAt(i)->toString(),
                expectedAt(i)->toString(),
                expected->toString());
            return nullptr;
        }
    }
    if (signature->hasExitType() && expected->hasExitType()) {
        unifyTypeVars(signature->exitType(), expected->exitType(), bindings);
    }
    std::vector<std::string> names, open;
    collectTypeVars(signature, names);
    for (const std::string &name : names) {
        if (findTypeBinding(bindings, name) == nullptr) {
            open.push_back(name);
        }
    }
    if (!open.empty()) {
        why = std::format(
            "its type variable(s) {} cannot be determined from the function expected here, '{}'",
            strutil::join(open, ", ", [](const std::string &n) { return n; }),
            expected->toString());
        return nullptr;
    }
    for (size_t i = 0; i < withCount + normCount; ++i) {
        Type *param = substituteTypeVars(patternAt(i), bindings);
        if (containsTypeVar(expectedAt(i))) {
            why = std::format(
                "the function expected here, '{}', does not have all its types yet",
                expected->toString());
            return nullptr;
        }
        if (!param->equals(expectedAt(i))) {
            why = std::format(
                "with {} its parameter {} is '{}', and the function expected here is '{}'",
                typeBindingsToString(bindings),
                i + 1,
                param->toString(),
                expected->toString());
            return nullptr;
        }
    }
    compile_graph_ptr_t graph = instantiateGeneric(tmpl, bindings, origin);
    if (!graph->funcType()->hasExitType()) {
        diags_->of(SemanticDiag::CallingIncompleteFunction)
            .atOrigin(origin->origin())
            .commit(graph->name(), graph->funcType()->toString());
        throw BuildAbortException();
    }
    if (expected->hasExitType() && !containsTypeVar(expected->exitType()) &&
        !expected->exitType()->equals(graph->funcType()->exitType())) {
        why = std::format(
            "with {} it returns '{}', and the function expected here is '{}'",
            typeBindingsToString(bindings),
            graph->funcType()->exitType()->toString(),
            expected->toString());
        return nullptr;
    }
    return graph;
}

node_handle_t
Builder::genericValueFor(node_handle_t node, Type *expected, const GCT::node_ptr_t &origin) {
    const auto generic = genericDrefs_.find(node);
    ASSERT(generic != genericDrefs_.end(), "Not the name of a generic function.");
    const template_ptr_t tmpl = generic->second;
    if (expected == nullptr) {
        std::vector<std::string> names;
        collectTypeVars(tmpl->funcType, names);
        rejectGenericValue(
            tmpl,
            origin,
            std::format(
                "nothing here says which function is wanted, so its type variable(s) {} cannot "
                "be determined; write the type, e.g. 'let f: (x: int) => int = {}'",
                strutil::join(names, ", ", [](const std::string &n) { return n; }),
                tmpl->name));
    }
    if (expected->code() != TypeCode::Function) {
        rejectGenericValue(
            tmpl,
            origin,
            "a value of type '" + expected->toString() + "' is expected here, not a function");
    }
    std::string why;
    compile_graph_ptr_t graph =
        instantiateGenericAs(tmpl, tt::as_ptr<FunctionType>(expected), origin, why);
    if (graph == nullptr) {
        rejectGenericValue(tmpl, origin, why);
    }
    return createFuncDataNode(graph, true, false);
}

compile_graph_ptr_t Builder::resolveGenericCall(
    const template_ptr_t &tmpl, std::vector<Argument> &withInputs,
    std::vector<Argument> &normInputs, type_vec_t &withTypes, type_vec_t &normTypes,
    const GCT::node_ptr_t &call, Type *expectedResult) {
    FunctionType *signature = tmpl->funcType;
    const size_t withCount  = signature->withTypesCount();
    const size_t normCount  = signature->normTypesCount();
    const auto inputAt      = [&](size_t i) -> Argument      &{
        return i < withCount ? withInputs[i] : normInputs[i - withCount];
    };
    const auto typeAt = [&](size_t i) -> Type *& {
        return i < withCount ? withTypes[i] : normTypes[i - withCount];
    };
    // The arguments that name a generic function themselves: they have no type yet.
    std::vector<template_ptr_t> waiting(withCount + normCount);
    const auto typesText = [&](const type_vec_t &types, size_t offset) {
        std::string text;
        for (size_t i = 0; i < types.size(); ++i) {
            const template_ptr_t &generic =
                offset + i < waiting.size() ? waiting[offset + i] : nullptr;
            text += (i == 0 ? "" : ", ") +
                    (generic ? "<generic " + generic->name + ">" : types[i]->toString());
        }
        return text;
    };
    const auto argTypesStr = [&] {
        return withTypes.empty() ? std::format("({})", typesText(normTypes, withCount))
                                 : std::format(
                                       "<{}> ({})",
                                       typesText(withTypes, 0),
                                       typesText(normTypes, withCount));
    };
    const auto reject = [&](const std::string &why) {
        diags_->of(SemanticDiag::GenericArgumentsMismatch)
            .atOrigin(call->origin())
            .commit(tmpl->name, argTypesStr(), why, tmpl->name + ": " + signature->toString());
        throw BuildAbortException();
    };
    if (withCount != withTypes.size()) {
        // Every argument written in the parentheses (`take(xs, 2)`): say which parameters
        // are with-parameters and how the call is written.
        if (withTypes.empty() && normCount > 0 && withCount + normCount == normTypes.size() &&
            signature->argNamesCount() >= withCount + normCount) {
            const auto names = [&](size_t from, size_t count) {
                std::string out;
                for (size_t i = 0; i < count; ++i) {
                    out += (i == 0 ? "" : ", ") + std::string(signature->argNameAt(from + i));
                }
                return out;
            };
            const std::string withs = names(0, withCount), norms = names(withCount, normCount);
            reject(
                std::format(
                    "it takes '{}' in angle brackets, not in the parentheses: write {}<{}>({}){}",
                    withs,
                    tmpl->name,
                    withs,
                    norms,
                    normCount == 1 ? std::format(" or {}->{}<{}>", norms, tmpl->name, withs)
                                   : std::string()));
        }
        reject(
            std::format(
                "it takes {} with-argument(s) and {} were given",
                withCount,
                withTypes.size()));
    }
    if (normCount != normTypes.size()) {
        reject(
            std::format("it takes {} argument(s) and {} were given", normCount, normTypes.size()));
    }
    for (size_t i = 0; i < withCount + normCount; ++i) {
        if (auto generic = genericDrefs_.find(inputAt(i).node); generic != genericDrefs_.end()) {
            waiting[i] = generic->second;
        }
    }
    // A parameter by its position among all parameters (`with` ones first).
    const auto patternAt = [&](size_t i) {
        return i < withCount ? signature->withTypeAt(i) : signature->normTypeAt(i - withCount);
    };
    const auto actualAt = [&](size_t i) { return valueTypeOf(typeAt(i)); };
    const auto describe = [&](size_t i) {
        const std::string paramName =
            i < signature->argNamesCount() ? std::string(signature->argNameAt(i)) : "?";
        return std::format(
            "{} {} {}, but its parameter '{}: {}'",
            i < withCount ? "with-argument" : "argument",
            (i < withCount ? i : i - withCount) + 1,
            waiting[i] ? "is the generic function '" + waiting[i]->name + "'"
                       : "has type '" + actualAt(i)->toString() + "'",
            paramName,
            patternAt(i)->toString());
    };
    // 1. Each variable takes the type found at its `typeas` position. An argument that is
    // not of the form of a `T?` / `T!` parameter enters it (design principles 3.8), so it
    // binds the variables of the payload: `or_zero(5, 0)` with `x: (typeas T)?` gives
    // `T = int`. `null` binds nothing.
    type_bindings_t bindings;
    const std::function<bool(Type *, Type *)> bindEntering = [&](Type *pattern, Type *actual) {
        type_bindings_t attempt = bindings;
        if (bindTypeVars(pattern, actual, attempt)) {
            bindings = std::move(attempt);
            return true;
        }
        TupleType *tagged = asTagged(pattern);
        if (tagged == nullptr) {
            return false;
        }
        if (tagged->isNullable() && actual->code() == TypeCode::Void) {
            return true;
        }
        return bindEntering(tagged->payloadType(), actual);
    };
    for (size_t i = 0; i < withCount + normCount; ++i) {
        if (!waiting[i] && !bindEntering(patternAt(i), actualAt(i))) {
            reject(describe(i) + " needs a type of that form to bind its type variables");
        }
    }
    // 2. A variable that its `typeas` position left open (the argument there is `null`, or a
    // generic function that is still waiting) takes the type found at any other occurrence:
    // the constraints of all arguments are collected before any is held to its parameter.
    for (size_t i = 0; i < withCount + normCount; ++i) {
        if (!waiting[i]) {
            unifyTypeVars(patternAt(i), actualAt(i), bindings);
        }
    }
    if (expectedResult && signature->hasExitType()) {
        unifyTypeVars(signature->exitType(), expectedResult, bindings);
    }
    // 3. The waiting arguments, as long as one of them can be given its types: the parameter
    // it is passed to, with what is known so far, is the function it has to be; its instance
    // then says what is still open in that parameter (its return type).
    std::vector<std::string> reasons(waiting.size());
    for (bool progress = true; progress;) {
        progress = false;
        for (size_t i = 0; i < waiting.size(); ++i) {
            if (!waiting[i]) {
                continue;
            }
            Type *wanted = substituteTypeVars(patternAt(i), bindings);
            if (wanted->code() != TypeCode::Function) {
                reasons[i] = wanted->code() == TypeCode::TypeVar
                                 ? "its parameter has no type of its own for it to take"
                                 : "its parameter is not a function";
                continue;
            }
            compile_graph_ptr_t instance = instantiateGenericAs(
                waiting[i],
                tt::as_ptr<FunctionType>(wanted),
                inputAt(i).expr,
                reasons[i]);
            if (instance == nullptr) {
                continue;
            }
            inputAt(i).node = createFuncDataNode(instance, true, false);
            typeAt(i)       = valueTypeOf(nodeTypeOf(inputAt(i).node));
            waiting[i]      = nullptr;
            unifyTypeVars(patternAt(i), actualAt(i), bindings);
            progress = true;
        }
    }
    std::vector<std::string> names, open;
    collectTypeVars(signature, names);
    for (const std::string &name : names) {
        if (findTypeBinding(bindings, name) == nullptr) {
            open.push_back(name);
        }
    }
    const std::string openText = strutil::join(open, ", ", [](const std::string &n) { return n; });
    for (size_t i = 0; i < waiting.size(); ++i) {
        if (waiting[i]) {
            reject(
                std::format(
                    "{} cannot be given its types{}: {}",
                    describe(i),
                    open.empty() ? std::string()
                                 : " while the type variable(s) " + openText + " of '" +
                                       tmpl->name + "' are not determined by the other arguments",
                    reasons[i]));
        }
    }
    if (!open.empty()) {
        reject(
            "the type variable(s) " + openText +
            " cannot be determined from the arguments or expected result; give an expected type "
            "or explicit " +
            tmpl->name + "::<...> type arguments");
    }
    // ...and then every argument is held to its parameter exactly as for an ordinary function.
    for (size_t i = 0; i < withCount + normCount; ++i) {
        Type *wanted = substituteTypeVars(patternAt(i), bindings);
        if (containsTypeVar(wanted)) {
            reject(describe(i) + " leaves a type variable without a type");
        }
        if (!entersImplicitly(wanted, actualAt(i))) {
            reject(
                std::format(
                    "{} is '{}' here ({})",
                    describe(i),
                    wanted->toString(),
                    typeBindingsToString(bindings)));
        }
    }
    return instantiateGeneric(tmpl, bindings, call);
}

void Builder::settleGenericOperatorArguments(
    const oper_group_ptr_t &ops, std::vector<Argument> &withInputs,
    std::vector<Argument> &normInputs, type_vec_t &withTypes, type_vec_t &normTypes,
    const GCT::node_ptr_t &call) {
    // A name that still has no function value: a generic function, or a name with several
    // functions (overloads of the program, an operator with several signatures).
    const auto isOverloaded = [&](const Argument &arg) {
        if (arg.node == nullptr || !nodeIsKind(arg.node, runtime::GCNodeKind::Dref) ||
            genericDrefs_.contains(arg.node) || boundWithArgs_.contains(arg.node)) {
            return false;
        }
        const auto &target = nodeGraphOf(arg.node)->drefTarget(arg.node);
        return std::holds_alternative<graph_vec_ptr_t>(target) ||
               std::holds_alternative<oper_group_ptr_t>(target);
    };
    const auto isWaiting = [&](const Argument &arg) {
        return genericDrefs_.contains(arg.node) || isOverloaded(arg);
    };
    // The types to try for a parameter of the function: what the other arguments are made
    // of (the element type of an array, of a set, the key and value types of a map and their
    // pair), then those arguments' own types.
    type_vec_t candidates;
    const auto add = [&](Type *type) {
        if (type == nullptr || type->code() == TypeCode::Void || containsTypeVar(type)) {
            return;
        }
        for (Type *known : candidates) {
            if (known->equals(type)) {
                return;
            }
        }
        candidates.push_back(type);
    };
    const auto addParts = [&](Type *type) {
        if (type == nullptr) {
            return;
        }
        if (type->code() == TypeCode::Array) {
            add(tt::as_ptr<ArrayType>(type)->elemType());
        } else if (type->isOtherType()) {
            const auto params = static_cast<OtherType *>(type)->paramsSpan();
            for (Type *param : params) {
                add(param);
            }
            if (params.size() == 2) {
                add(TupleType::create(std::vector<Type *>{params[0], params[1]}));
            }
        }
    };
    for (const auto *list : {&withInputs, &normInputs}) {
        const type_vec_t &types = list == &withInputs ? withTypes : normTypes;
        for (size_t i = 0; i < list->size(); ++i) {
            if (!isWaiting((*list)[i])) {
                addParts(valueTypeOf(types[i]));
            }
        }
    }
    for (const auto *list : {&withInputs, &normInputs}) {
        const type_vec_t &types = list == &withInputs ? withTypes : normTypes;
        for (size_t i = 0; i < list->size(); ++i) {
            if (!isWaiting((*list)[i])) {
                add(valueTypeOf(types[i]));
            }
        }
    }
    const auto accepted = [&] {
        std::vector<StaticArg> statics;
        statics.reserve(normInputs.size());
        for (const Argument &input : normInputs) {
            statics.push_back(isWaiting(input) ? StaticArg{} : staticArgOf(input.node));
        }
        return ops->resolve(withTypes, normTypes, statics, Modifier::None).has_value();
    };
    // A name with several functions: the one the operator accepts, when it is exactly one
    // (`xs->map<abs>`, `xs->reduce<max, 0>`). The operator is asked about each candidate's
    // type; none or several accepted leave the name without a value, and the diagnostic of
    // that lists the candidates.
    const auto settleOverloaded = [&](Argument &arg, Type *&type) {
        Type *saved        = type;
        const auto &target = nodeGraphOf(arg.node)->drefTarget(arg.node);
        std::vector<compile_graph_ptr_t> graphs;
        std::vector<oper_idx_ptr_t> operators;
        const auto accepts = [&](FunctionType *candidate) {
            if (candidate == nullptr || !candidate->hasExitType()) {
                return false;
            }
            type          = candidate;
            const bool ok = accepted();
            type          = saved;
            return ok;
        };
        if (std::holds_alternative<graph_vec_ptr_t>(target)) {
            if (auto all = asCompileGraphVec(target)) {
                for (const auto &g : *all) {
                    if (!decoratedOf_.contains(g.get()) && accepts(g->funcType())) {
                        graphs.push_back(g);
                    }
                }
            }
        } else {
            const auto &group = std::get<oper_group_ptr_t>(target);
            const auto add    = [&](const std::string &uri, FunctionType *candidate) {
                for (const auto &known : operators) {
                    if (known->uri() == uri && known->funcType()->equals(candidate)) {
                        return;
                    }
                }
                if (accepts(candidate)) {
                    operators.push_back(
                        std::make_shared<OperatorIndex>(group->name(), candidate, uri));
                }
            };
            for (const auto &[uri, resolver] : group->resolvers()) {
                if (FunctionType *fixed = resolver->fixedSignature()) {
                    add(uri, fixed);
                    continue;
                }
                // A signature computed from the argument types: asked for the candidate
                // types, one and two parameters.
                for (Type *first : candidates) {
                    if (auto one = resolver->resolve({}, {first}, Modifier::None)) {
                        add(uri, *one);
                    }
                    for (Type *second : candidates) {
                        if (auto two = resolver->resolve({}, {first, second}, Modifier::None)) {
                            add(uri, *two);
                        }
                    }
                }
            }
        }
        if (graphs.size() + operators.size() != 1) {
            return; // stays a name: rejectNameWithoutValue lists what it stands for
        }
        compile_graph_ptr_t chosen =
            !graphs.empty() ? graphs.front() : operatorValueGraphOf(operators.front(), arg.expr);
        if (chosen != nullptr) {
            arg.node = createFuncDataNode(chosen, true, false);
            type     = valueTypeOf(nodeTypeOf(arg.node));
        }
    };
    const auto settleOne = [&](Argument &arg, Type *&type) {
        if (isOverloaded(arg)) {
            settleOverloaded(arg, type);
            return;
        }
        const template_ptr_t tmpl = genericDrefs_.find(arg.node)->second;
        FunctionType *signature   = tmpl->funcType;
        const size_t params       = signature->normTypesCount();
        std::vector<std::string> names;
        collectTypeVars(signature, names);
        std::string tried;
        // An instance that was built and that the operator then refused (its result type
        // is not what the operator needs): the first one stands for the name, so that the
        // operator's own diagnostic says what does not fit.
        node_handle_t refused{};
        Type *refusedType = nullptr;
        if (signature->withTypesCount() == 0 && params > 0 && !candidates.empty()) {
            // Every choice of candidate types for the parameters, the first candidates first.
            const size_t limit = 256;
            std::vector<size_t> pick(params, 0);
            for (size_t round = 0; round < limit; ++round) {
                type_bindings_t bindings;
                bool fits = true;
                for (size_t p = 0; fits && p < params; ++p) {
                    fits = unifyTypeVars(signature->normTypeAt(p), candidates[pick[p]], bindings);
                }
                for (size_t n = 0; fits && n < names.size(); ++n) {
                    fits = findTypeBinding(bindings, names[n]) != nullptr;
                }
                for (size_t p = 0; fits && p < params; ++p) {
                    fits = substituteTypeVars(signature->normTypeAt(p), bindings)
                               ->equals(candidates[pick[p]]);
                }
                if (fits) {
                    FunctionType *asType = instantiateFunctionType(signature, bindings);
                    Type *saved          = type;
                    // The operator is asked before the instance is built, so that the body
                    // of an instance it would refuse is never built (its errors would be
                    // reported for types the program does not mean). With a declared return
                    // type the question is exact; without one it is asked for the return
                    // types an element function usually has.
                    bool wanted = false;
                    if (asType->hasExitType()) {
                        type   = asType;
                        wanted = accepted();
                    } else {
                        type_vec_t results = candidates;
                        for (Type *usual :
                             {Type::Bool(), Type::Int64(), Type::Float64(), Type::String()}) {
                            results.push_back(usual);
                        }
                        for (size_t r = 0; !wanted && r < results.size(); ++r) {
                            OpenFunctionType *probe = instantiateFunctionType(signature, bindings);
                            probe->setExitType(results[r]);
                            type   = probe;
                            wanted = accepted();
                        }
                    }
                    type = saved;
                    if (wanted) {
                        std::string why;
                        compile_graph_ptr_t instance =
                            instantiateGenericAs(tmpl, asType, arg.expr, why);
                        if (instance != nullptr) {
                            node_handle_t value = createFuncDataNode(instance, true, false);
                            type                = valueTypeOf(nodeTypeOf(value));
                            if (accepted()) {
                                arg.node = value;
                                return;
                            }
                            if (refused == nullptr) {
                                refused     = value;
                                refusedType = type;
                            }
                            type = saved;
                        }
                    }
                    if (!tried.empty()) {
                        tried += "; ";
                    }
                    tried += typeBindingsToString(bindings);
                }
                // The next choice.
                size_t p = 0;
                while (p < params && ++pick[p] == candidates.size()) {
                    pick[p++] = 0;
                }
                if (p == params) {
                    break;
                }
            }
        }
        if (refused != nullptr) {
            arg.node = refused;
            type     = refusedType;
            return;
        }
        if (tmpl->name.empty()) {
            // The element function of a list comprehension: nothing to take `x` from.
            Type *source = !normTypes.empty() ? valueTypeOf(normTypes.front()) : nullptr;
            diags_->of(SemanticDiag::ComprehensionSourceRefused)
                .atOrigin(call->origin())
                .commit(source != nullptr ? source->toString() : std::string("void"));
            throw BuildAbortException();
        }
        rejectGenericValue(
            tmpl,
            arg.expr,
            std::format(
                "the operator '{}' does not say which function it expects, and no instance "
                "suggested by its other arguments is one it accepts{}; its type variable(s) {} "
                "stay undetermined. Write the call in a function whose parameter types are "
                "written out, e.g. '(x: int) => {}(x)'",
                ops->name(),
                tried.empty() ? std::string() : " (tried: " + tried + ")",
                strutil::join(names, ", ", [](const std::string &n) { return n; }),
                tmpl->name));
    };
    for (size_t i = 0; i < withInputs.size(); ++i) {
        if (isWaiting(withInputs[i])) {
            settleOne(withInputs[i], withTypes[i]);
        }
    }
    for (size_t i = 0; i < normInputs.size(); ++i) {
        if (isWaiting(normInputs[i])) {
            settleOne(normInputs[i], normTypes[i]);
        }
    }
}

void Builder::rejectInvalidCollectionKey(Type *type, const GCT::node_ptr_t &at) {
    OtherType *invalid = findInvalidCollectionKey(type);
    if (invalid == nullptr) {
        return;
    }
    Type *keyType = invalid->params()[0];
    diags_->of(SemanticDiag::CollectionKeyNotAllowed)
        .atOrigin(at->origin())
        .commit(keyType->toString(), invalid->toString(), collectionKeyProblem(keyType));
    throw BuildAbortException();
}

compile_graph_ptr_t Builder::instantiateGeneric(
    const template_ptr_t &tmpl, const type_bindings_t &bindings, const GCT::node_ptr_t &call) {
    ExpectedValueScope instanceScope(expectedValueType_, nullptr);
    // All routes (inferred calls, function values and ::<T>) enforce the same type rules.
    Type *concrete = substituteTypeVars(tmpl->funcType, bindings);
    if (TupleType *repeated = findRepeatedMark(concrete)) {
        diags_->of(SemanticDiag::TaggedTypeNesting)
            .atOrigin(call->origin())
            .commit(
                repeated->toString(),
                asTagged(repeated->payloadType())->payloadType()->toString());
        throw BuildAbortException();
    }
    rejectInvalidCollectionKey(concrete, call);
    const auto sameBindings = [&](const type_bindings_t &other) {
        if (other.size() != bindings.size()) {
            return false;
        }
        for (const auto &[name, type] : bindings) {
            Type *bound = findTypeBinding(other, name);
            if (bound == nullptr || !bound->equals(type)) {
                return false;
            }
        }
        return true;
    };
    // One instance per set of types in the whole program, whichever module calls it: the
    // instance is a graph under the graph the function was declared in (the root graph for a
    // function of a module's top level), and its body resolves its names, and captures the
    // values of the top level, of the module that declares it. An instance whose body is
    // still being built is found here too: that is a recursive call.
    const bool foreign = tmpl->module != module_;
    for (const GenericInstance &instance : tmpl->instances) {
        if (sameBindings(instance.bindings)) {
            if (instance.failed) {
                throw BuildAbortException();
            }
            return instance.graph;
        }
    }
    const std::string instanceName = std::format(
        "{}<{}>",
        tmpl->name,
        strutil::join(bindings, ", ", [](const std::pair<std::string, Type *> &b) {
            return b.second->toString();
        }));
    if (tmpl->openInstances >= kMaxOpenInstancesOfOne ||
        instantiationDepth_ >= kMaxInstantiationDepth) {
        diags_->of(SemanticDiag::GenericInstantiationTooDeep)
            .atOrigin(call->origin())
            .commit(
                tmpl->name,
                typeBindingsToString(bindings),
                tmpl->openInstances >= kMaxOpenInstancesOfOne
                    ? std::format(
                          "{} instances of it are already being built inside one another",
                          tmpl->openInstances)
                    : std::format(
                          "{} instantiations are already nested inside one another",
                          instantiationDepth_));
        throw BuildAbortException();
    }

    // Where an error in the body is reported it also says which call asked for the instance:
    // a secondary location at the call. The one-line format prints it after the message as
    // " [in the instance ..., instantiated by the call at line:col]".
    const DiagLabel instanceLabel{
        SourceRange{call->origin()},
        tmpl->name.empty()
            ? std::format(
                  "in a list comprehension whose elements have type '{}'",
                  bindings.empty() ? std::string("?") : bindings.front().second->toString())
            : std::format(
                  "in the instance '{}' of the generic function '{}' with {}, instantiated by "
                  "the call",
                  instanceName,
                  tmpl->name,
                  typeBindingsToString(bindings)),
        ""};

    // The instance is built where the function was declared, not where it is called.
    struct BuildState {
        Builder &self;
        compile_graph_ptr_t graph;
        node_scope_ptr_t nodeScope;
        graph_scope_ptr_t graphScope;
        decorated_scope_ptr_t decoratedScope;
        template_scope_ptr_t templateScope;
        type_bindings_t bindings;
        bool waited, synced, varied, declaresVariable, calleeAllowed;
        node_handle_t lastSynced;
        camel::core::module::module_ptr_t module;
        std::vector<std::string> namespacePath;
        std::vector<const std::vector<Reference> *> usingFrames;
        GenericTemplate &opened;
        BuildState(Builder &b, GenericTemplate &t)
            : self(b), graph(b.currGraph_), nodeScope(b.nodeScope_), graphScope(b.graphScope_),
              decoratedScope(b.decoratedScope_), templateScope(b.templateScope_),
              bindings(b.typeBindings_), waited(b.waited_), synced(b.synced_), varied(b.varied_),
              declaresVariable(b.declaresVariable_), calleeAllowed(b.genericCalleeAllowed_),
              lastSynced(b.lastSyncedNode_), module(b.module_), namespacePath(b.namespacePath_),
              usingFrames(b.usingFrames_), opened(t) {
            ++self.instantiationDepth_;
            ++opened.openInstances;
        }
        ~BuildState() {
            --opened.openInstances;
            self.module_               = module;
            self.namespacePath_        = std::move(namespacePath);
            self.usingFrames_          = std::move(usingFrames);
            self.currGraph_            = graph;
            self.nodeScope_            = nodeScope;
            self.graphScope_           = graphScope;
            self.decoratedScope_       = decoratedScope;
            self.templateScope_        = templateScope;
            self.typeBindings_         = std::move(bindings);
            self.waited_               = waited;
            self.synced_               = synced;
            self.varied_               = varied;
            self.declaresVariable_     = declaresVariable;
            self.genericCalleeAllowed_ = calleeAllowed;
            self.lastSyncedNode_       = lastSynced;
            --self.instantiationDepth_;
        }
    };

    const size_t instanceIndex = tmpl->instances.size();
    const size_t diagsBefore   = diags_->count();
    const size_t errorsBefore  = diags_->count(Severity::Error);
    compile_graph_ptr_t graph;
    try {
        BuildState saved(*this, *tmpl);
        AttrScope attrs(currentAttrs_, tmpl->attrs);
        currGraph_      = tmpl->ownerGraph;
        module_         = tmpl->module;
        namespacePath_  = tmpl->namespacePath;
        usingFrames_    = tmpl->usingFrames;
        nodeScope_      = tmpl->nodeScope;
        graphScope_     = tmpl->graphScope;
        decoratedScope_ = tmpl->decoratedScope;
        templateScope_  = tmpl->templateScope;
        typeBindings_   = tmpl->outerBindings;
        typeBindings_.insert(typeBindings_.end(), bindings.begin(), bindings.end());
        waited_               = false;
        synced_               = false;
        varied_               = false;
        declaresVariable_     = false;
        genericCalleeAllowed_ = false;
        lastSyncedNode_       = {};

        FunctionType *funcType = instantiateFunctionType(tmpl->funcType, bindings);
        graph = tmpl->name.empty() ? createCompileGraph(funcType, currGraph_)
                                   : createCompileGraph(funcType, currGraph_, instanceName);
        if (const auto declaring = units_.find(tmpl->module.get());
            declaring != units_.end() && !tmpl->name.empty()) {
            // Shown with the module that declares it (design principles 10.2).
            graph->setName(qualifiedName(*declaring->second, instanceName));
        }
        if (currGraph_ == rootGraph_) {
            // The instance of a top-level generic function belongs to the module declaring it.
            const auto declaring = units_.find(tmpl->module.get());
            rootSubGraphOwners_[graph.get()] =
                declaring != units_.end() ? declaring->second.get() : unit_;
        }
        tmpl->instances.push_back({bindings, graph, false});
        // Scopes are kept by name under the scope they were entered from, here the scope the
        // function was declared in: two functions written in place, which have no name to
        // tell them apart, need two scopes; so do two generic overloads of one operator,
        // which share their name.
        const bool sharedName =
            tmpl->name.empty() || camel::core::isOverloadableOperatorGroup(tmpl->func->name());
        enterGraphScope(
            graph,
            sharedName ? std::format("{}@{:p}", instanceName, static_cast<void *>(tmpl.get()))
                       : instanceName);
        buildFuncBody(tmpl->func, graph, funcType, instanceName);
        leaveScope();
    } catch (...) {
        if (instanceIndex < tmpl->instances.size()) {
            tmpl->instances[instanceIndex].failed = true;
        }
        if (foreign) {
            diags_->locateByOriginFrom(diagsBefore);
        }
        diags_->addRelatedToErrorsFrom(diagsBefore, instanceLabel, 3);
        throw;
    }
    // A statement of the body that fails is skipped so that the rest is still checked; the
    // instance is unusable all the same.
    if (diags_->count(Severity::Error) > errorsBefore) {
        tmpl->instances[instanceIndex].failed = true;
        if (foreign) {
            diags_->locateByOriginFrom(diagsBefore);
        }
        diags_->addRelatedToErrorsFrom(diagsBefore, instanceLabel, 3);
        throw BuildAbortException();
    }
    return graph;
}

node_handle_t Builder::visitDataNode(const GCT::node_ptr_t &gct) {
    ENTER("DATA");
    const data_ptr_t &data = gct->data();

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
                const auto &refNode = elementNodeOf(std::string(ref), gct);
                refTypes.push_back(nodeTypeOf(refNode));
                refNodes.push_back(refNode);
            }
            ASSERT(
                holes.size() == refNodes.size(),
                "Composite holes must match unresolved reference count.");
            if (dataType->code() == TypeCode::Array && !dataType->resolved() && !refTypes.empty()) {
                // An array whose elements are all computed (`[x, null]`, `[a, b]` with an
                // `int` and an `int?`): its element type is what the elements join to, and
                // an element that is not of that type enters it (design principles 3.8).
                Type *joined = valueTypeOf(refTypes.front());
                for (size_t i = 1; i < refTypes.size() && joined != nullptr; ++i) {
                    joined = joinTypes(joined, valueTypeOf(refTypes[i]));
                }
                for (size_t i = 0; i < refTypes.size() && joined != nullptr; ++i) {
                    if (joined->assignableFrom(valueTypeOf(refTypes[i]))) {
                        continue;
                    }
                    if (node_handle_t entered = coerceTo(refNodes[i], joined, gct)) {
                        refNodes[i] = entered;
                        refTypes[i] = joined;
                    }
                }
            } else if (
                dataType->code() == TypeCode::Array && dataType->resolved() &&
                tt::as_ptr<ArrayType>(dataType)->elemType()->code() != TypeCode::Void) {
                // The literal elements fixed the element type (`[x, 5]` is an `int[]`): each
                // computed element must be of it or enter it. Unchecked, `x: string` (or an
                // `any`) was stored as an `int` and its address read as a number
                // (docs/design/any-typing, defect D2).
                Type *elemType = tt::as_ptr<ArrayType>(dataType)->elemType();
                for (size_t i = 0; i < refTypes.size(); ++i) {
                    Type *have = valueTypeOf(refTypes[i]);
                    if (elemType->assignableFrom(have)) {
                        continue;
                    }
                    if (node_handle_t entered = coerceTo(refNodes[i], elemType, gct)) {
                        refNodes[i] = entered;
                        refTypes[i] = elemType;
                        continue;
                    }
                    // Reported at the element (the literal itself carries no position).
                    const camel::source::origin_id_t at = refNodes[i]->header.origin != 0
                                                              ? refNodes[i]->header.origin
                                                              : gct->origin();
                    diags_->of(SemanticDiag::ElementTypeMismatch)
                        .atOrigin(at)
                        .commit("array", have->toString(), elemType->toString());
                    throw BuildAbortException();
                }
            }
            auto filledType = dataType->resolved()
                                  ? dataType
                                  : tt::as_ptr<CompositeType>(dataType->clone())->resolve(refTypes);
            auto payload    = runtime::makeFillPayload(fillKindForType(filledType), holes);
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
    Type *type = boundType(gct->dataType());
    LEAVE("TYPE");
    return type;
}

node_handle_t Builder::visitNRefNode(const GCT::node_ptr_t &gct) {
    ENTER("NREF");
    const string &ident = gct->name();
    // Names declared inside the initializer are not part of this declaration.
    const bool isVar = std::exchange(declaresVariable_, false);
    // A function named as the initializer is bound as its function value (`let g = f`), so
    // that the name can be captured and returned like any other value. A name without a
    // single function value (an operator, a set of overloads) stays bound to what it names
    // and can be called where it was bound.
    const node_scope_ptr_t declScope = nodeScope_;
    node_handle_t node{};
    try {
        node = functionValueOf(valueNodeOf(visit(gct->at(0))), gct);
    } catch (const BuildAbortException &) {
        // The initializer is in error, so the name is not declared. Uses of it are
        // consequences of that error, not errors of their own (see poisonedNames_).
        declaresVariable_ = isVar;
        if (!declScope->has(ident, false)) {
            poisonedNames_[declScope].insert(ident);
        }
        throw;
    }
    declaresVariable_ = isVar;
    // `let g = f<w>` where `f<w>` is not one function: the name would be bound to `f` and the
    // arguments lost.
    try {
        rejectUnappliedBinding(node, gct->at(0));
        if (genericDrefs_.contains(node)) {
            // `let g = id`: no context to take the types from.
            (void)genericValueFor(node, nullptr, gct->at(0));
        }
    } catch (const BuildAbortException &) {
        if (!declScope->has(ident, false)) {
            poisonedNames_[declScope].insert(ident);
        }
        throw;
    }
    // Design principles 3.8: the empty literal `[]` takes its element type from where it
    // stands (an annotation, a parameter, the other arm of a branch, `as`). A name bound to
    // it without one has no element type; say so here, where the type can be written. (A
    // name the compiler made for an element of a literal starts with a digit.)
    if (Type *type = node != nullptr ? valueTypeOf(nodeTypeOf(node)) : nullptr;
        type != nullptr && type->code() == TypeCode::Array &&
        tt::as_ptr<ArrayType>(type)->elemType()->code() == TypeCode::Void && !ident.empty() &&
        !std::isdigit(static_cast<unsigned char>(ident.front())) && ident.front() != '$') {
        diags_->of(SemanticDiag::EmptyArrayWithoutType)
            .atOrigin(gct->at(0)->origin())
            .commit(ident);
        if (!declScope->has(ident, false)) {
            poisonedNames_[declScope].insert(ident);
        }
        throw BuildAbortException();
    }
    if (ident == GCT::kDiscardName) {
        // `let _ = e` (design principles 3.9): the value is evaluated where the statement
        // stands and dropped on purpose. No name is bound, so the statement can be repeated.
        if (node != nullptr) {
            discardedValues_.insert(node);
        }
        LEAVE("NREF");
        return node;
    }
    // `var x = e` binds x to a new variable holding the value of e; `let x = e` binds it to
    // that value.
    node_handle_t bound = isVar ? newVariable(node) : node;
    if (isVar) {
        // The cell is created by its declaration: that is its place in the source.
        registerNodeOrigin(context_, bound, gct, "gir.var");
    }
    bool success = insertNode(ident, bound);
    if (!success) {
        diags_->of(SemanticDiag::Redeclaration).atOrigin(gct->origin()).commit(ident);
        throw BuildAbortException();
    }
    if (currGraph_ == rootGraph_ && !ident.empty() &&
        !std::isdigit(static_cast<unsigned char>(ident.front())) && ident.front() != '$') {
        // A value of the top level of the module: what the functions that use it capture.
        topLevelValues_[bound] = {module_.get(), ident};
    }
    if (recorder_ && !nodeIsKind(bound, runtime::GCNodeKind::Dref)) {
        recordDeclaration(ident, bound, gct);
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
    // `A(i); wait modify(i); B(i)` gives A -> modify -> B (design principles log 2.6). The second
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
        if (auto captured = captureOriginOf_.find(variable); captured != captureOriginOf_.end()) {
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

std::set<node_handle_t> Builder::capturedWritesOf(DraftGraphBuilder *root) const {
    const auto isOpen = [&](DraftGraphBuilder *graph) {
        auto state = bodyStates_.find(graph);
        return state != bodyStates_.end() && state->second == BodyState::Open;
    };
    const auto summaryOf = [&](DraftGraphBuilder *graph) -> const CaptureSummary * {
        auto summary = captures_.find(graph);
        return summary == captures_.end() ? nullptr : &summary->second;
    };
    // A graph writes only variables it captured, and a write reaches `root` only through
    // graphs that all captured the variable: only the callees that captured one of root's
    // variables matter. (With all modules on one root, the calls reach across the whole
    // program; walking every callee made this quadratic in the length of a call chain.)
    const auto *rootSummary = summaryOf(root);
    if (rootSummary == nullptr || rootSummary->variables.empty()) {
        return {};
    }
    const std::unordered_set<node_handle_t> rootVariables(
        rootSummary->variables.begin(),
        rootSummary->variables.end());
    const auto sharesVariable = [&](const CaptureSummary &summary) {
        return std::ranges::any_of(summary.variables, [&](node_handle_t variable) {
            return rootVariables.contains(variable);
        });
    };
    // The graphs reachable through direct calls and arms. An open body is not looked into:
    // what it will write is not known yet, so all it captured stands for it.
    std::vector<DraftGraphBuilder *> reached{root};
    std::unordered_set<DraftGraphBuilder *> seen{root};
    std::unordered_map<DraftGraphBuilder *, std::set<node_handle_t>> writes;
    for (size_t i = 0; i < reached.size(); ++i) {
        DraftGraphBuilder *graph = reached[i];
        const auto *summary      = summaryOf(graph);
        if (isOpen(graph)) {
            writes[graph].insert(summary->variables.begin(), summary->variables.end());
            continue;
        }
        writes[graph] = summary->written;
        for (DraftGraphBuilder *callee : summary->callees) {
            const auto *calleeSummary = summaryOf(callee);
            if (calleeSummary != nullptr && sharesVariable(*calleeSummary) &&
                seen.insert(callee).second) {
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
            if (isOpen(graph)) {
                continue;
            }
            auto &own = writes[graph];
            for (DraftGraphBuilder *callee : summary->callees) {
                auto calleeWrites = writes.find(callee);
                if (calleeWrites == writes.end()) {
                    continue; // not reached: it captured none of root's variables
                }
                for (node_handle_t variable : calleeWrites->second) {
                    if (std::ranges::find(summary->variables, variable) !=
                            summary->variables.end() &&
                        own.insert(variable).second) {
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
    // Capturing into the current graph may add to its summary: copies.
    const std::vector<node_handle_t> variables = summary->second.variables;
    const std::set<node_handle_t> written      = capturedWritesOf(callee.get());
    std::vector<CapturedUse> uses;
    for (node_handle_t origin : variables) {
        // The callee's variable, as the current graph sees it (see captureInto).
        node_handle_t variable = captureInto(currGraph_, origin, captureNameOf(origin, ""));
        uses.push_back({variable, origin, written.contains(origin)});
    }
    return uses;
}

void Builder::reportIgnoredCapturedWrites() {
    for (const PendingIgnoredWrite &pending : unit_->pendingIgnoredWrites) {
        std::set<node_handle_t> written;
        for (DraftGraphBuilder *graph : pending.graphs) {
            written.merge(capturedWritesOf(graph));
        }
        std::vector<std::string> names;
        for (node_handle_t variable : pending.variables) {
            if (written.contains(variable)) {
                names.push_back("'" + captureNameOf(variable, originNames_[variable]) + "'");
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
    unit_->pendingIgnoredWrites.clear();
}

void Builder::reportUndeclaredCapturedWrites() {
    // The prefix `sync` of a function declares that it has side effects (design principles
    // 2.3); a function with a `var` parameter is already checked for it
    // (VarParamInAsyncFunction, in the GCT builder). A function that writes a variable it
    // captured, itself or through the functions it calls, has side effects as well. This is a
    // declaration check only: the modifier changes nothing in how calls are compiled.
    for (const BuiltFunction &function : unit_->builtFunctions) {
        if (function.graph->funcType()->modifiers().sync() ||
            blockGraphs_.contains(function.graph)) {
            continue; // declared so, or a decorated block (no declaration to write it on)
        }
        std::vector<std::string> names;
        for (node_handle_t variable : capturedWritesOf(function.graph)) {
            names.push_back("'" + captureNameOf(variable, originNames_[variable]) + "'");
        }
        if (names.empty()) {
            continue;
        }
        // In the order of the names, not of the nodes.
        std::ranges::sort(names);
        diags_->of(SemanticDiag::CapturedWriteInAsyncFunction)
            .atOrigin(function.origin)
            .commit(
                function.name.empty() ? std::string("The lambda")
                                      : std::format("Function '{}'", function.name),
                names.size() > 1 ? "s" : "",
                strutil::join(names, ", ", [](const std::string &name) { return name; }));
    }
    unit_->builtFunctions.clear();
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
        return op != nullptr && camel::core::OperatorTable::instance().hasExternalEffect(op->uri());
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
    case runtime::GCNodeKind::Loop: {
        // Each round calls the condition and the body graph.
        const auto &target = graph->loopTarget(node);
        return (target.cond && effectfulGraphs_.contains(target.cond.get())) ||
               effectfulGraphs_.contains(target.body.get());
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

void Builder::settleEffectfulGraphs() {
    // Whether a call is an effect depends on its callee's body, which may be written later,
    // in another module, or be the caller itself: the set of effectful graphs is the least
    // fixpoint over the bodies of the whole program (a part of the link step), and only then
    // are the warnings issued, per module, in the order its bodies were built.
    for (size_t known = effectfulGraphs_.size() + 1; known != effectfulGraphs_.size();) {
        known = effectfulGraphs_.size();
        for (const ModuleUnit *unit : unitOrder_) {
            for (const compile_graph_ptr_t &graph : unit->builtBodies) {
                reportDroppedEffects(graph, false);
            }
        }
    }
}

void Builder::reportDroppedEffectsOfBodies() {
    for (const compile_graph_ptr_t &graph : unit_->builtBodies) {
        reportDroppedEffects(graph, true);
    }
    unit_->builtBodies.clear();
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
        for (auto inputs :
             {draft.normInputsOf(id), draft.withInputsOf(id), draft.ctrlInputsOf(id)}) {
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
        } else if (
            graph != rootGraph_ || (id < rootNodeOwners_.size() && rootNodeOwners_[id] == unit_)) {
            // On the root graph, the top level of the module being reported only: another
            // module's top-level value that this program does not use is not a mistake.
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
            for (auto users :
                 {draft.normUsersOf(id), draft.withUsersOf(id), draft.ctrlUsersOf(id)}) {
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
        case runtime::GCNodeKind::Loop:
            subject = "The loop";
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

void Builder::reportUnusedFallibleValues() {
    // Design principles 3.9: a `T!` that is evaluated and that nothing takes is a warning,
    // because the error it may hold is lost without a trace. On the graph "evaluated and
    // not taken" is a node without a data user that something is ordered after: a statement
    // of a sync block. (Outside a sync block a value nothing takes is not evaluated at all:
    // design principles 3.1, which has its own warning, EffectNotExecuted.)
    auto sourceContext = context_ ? context_->sourceContext() : nullptr;
    const auto warn =
        [&](const compile_graph_ptr_t &graph, node_handle_t node, Type *type, const char *remark) {
            camel::source::origin_id_t origin = node->header.origin;
            if (origin == camel::source::kInvalidOriginId && sourceContext) {
                origin = sourceContext->debugMap().graphOrigin(graph->stableId());
            }
            diags_->of(SemanticDiag::UnusedFallibleValue)
                .atOrigin(origin)
                .commit(type->toString(), remark);
        };
    // The `T!` a node gives, if it gives one. A call was typed when it was built, which for
    // a callee whose return type is taken from its body may be before that body was
    // complete (a recursive callee): the callee is asked.
    const auto fallibleTypeOf = [](const compile_graph_ptr_t &graph, node_handle_t node) {
        Type *type = nodeTypeOf(node);
        if (nodeIsKind(node, runtime::GCNodeKind::Func)) {
            if (const auto callee = graph->funcTarget(node)) {
                type = callee->funcType()->exitType();
            }
        }
        return asFallible(valueTypeOf(type));
    };
    // The entry function, when it declares no return type: what its body ends with is not
    // the result of the program (design principles 9.8), so nothing takes that either.
    DraftGraphBuilder *undeclaredMain = nullptr;
    if (context_ && module_ && context_->mainModule() == module_) {
        if (auto mains = graphsAt("main");
            mains.has_value() && !mains.value()->empty() &&
            inferredExitGraphs_.contains(mains.value()->front().get())) {
            undeclaredMain = mains.value()->front().get();
        }
    }

    for (const compile_graph_ptr_t &graph : unit_->builtBodies) {
        auto &draft = graph->draft();
        if (draft.exitNode() == runtime::kInvalidNodeRef) {
            continue; // the body did not build (an error was reported)
        }
        const auto isExit = [&](draft_node_ref_t id) {
            return id == draft.exitNode() || id == draft.outputNode() || id == draft.returnNode();
        };
        if (graph.get() == undeclaredMain && draft.returnNode() != runtime::kInvalidNodeRef) {
            node_handle_t value = draft.node(draft.returnNode());
            if (TupleType *type = fallibleTypeOf(graph, value);
                type != nullptr && !discardedValues_.contains(value)) {
                warn(
                    graph,
                    value,
                    type,
                    " ('main' declares no return type, so what its body ends with is not the "
                    "result of the program)");
            }
        }
        for (draft_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
            if (!draft.alive(id)) {
                continue;
            }
            node_handle_t node = draft.node(id);
            switch (nodeKindOf(node)) {
            case runtime::GCNodeKind::Oper:
            case runtime::GCNodeKind::Func:
            case runtime::GCNodeKind::Call:
            case runtime::GCNodeKind::Join:
                break;
            default:
                continue; // not the result of an evaluation
            }
            const bool ordered = !draft.ctrlUsersOf(id).empty();
            if (nodeIsKind(node, runtime::GCNodeKind::Join) &&
                isVoidGraphExitType(nodeTypeOf(node)) && (ordered || isExit(id))) {
                // An `if` without `else`: the join takes nothing, so what an arm gives is
                // dropped where the arm ends.
                for (draft_node_ref_t armId : draft.withInputsOf(id)) {
                    node_handle_t arm   = draft.node(armId);
                    const auto armGraph = nodeIsKind(arm, runtime::GCNodeKind::Func)
                                              ? graph->funcTarget(arm)
                                              : nullptr;
                    if (armGraph == nullptr ||
                        armGraph->draft().returnNode() == runtime::kInvalidNodeRef) {
                        continue;
                    }
                    node_handle_t value = armGraph->draft().node(armGraph->draft().returnNode());
                    if (TupleType *type = fallibleTypeOf(armGraph, value);
                        type != nullptr && !discardedValues_.contains(value)) {
                        warn(armGraph, value, type, "");
                    }
                }
                continue;
            }
            if (!ordered || isExit(id) || !draft.normUsersOf(id).empty() ||
                !draft.withUsersOf(id).empty() || discardedValues_.contains(node)) {
                continue;
            }
            if (TupleType *type = fallibleTypeOf(graph, node)) {
                warn(graph, node, type, "");
            }
        }
    }
}

namespace {
/// The prefix of a name the compiler writes for a built-in operator it means itself (the
/// `filter` and `map` of a list comprehension). No program can write it, and no declaration
/// of the program is looked at when it is resolved.
constexpr std::string_view kBuiltinPrefix = "$builtin::";
} // namespace

node_handle_t Builder::visitDRefNode(const GCT::node_ptr_t &gct) {
    if (const std::string written = gct->name(); written.starts_with(kBuiltinPrefix)) {
        ENTER("DREF");
        genericCalleeAllowed_   = false;
        const std::string plain = written.substr(kBuiltinPrefix.size());
        const auto entity       = context_->importModule("")->getExportedEntity(Reference(plain));
        ASSERT(
            entity.has_value() && entity->isOperGroup(),
            "A built-in operator the compiler names does not exist: " + plain);
        node_handle_t drefNode{currGraph_->addDrefNode(
            std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>{entity->operGroup()})};
        registerNodeOrigin(context_, drefNode, gct, "gir.dref");
        LEAVE("DREF");
        return drefNode;
    }
    ENTER("DREF");
    // The name as written, and the name of the module it stands for when it is one (from the
    // innermost namespace out; design principles 10.2, layers 1 and 2).
    const string written = gct->name();
    if (written.find("::") != std::string::npos) {
        // `Shape::Empty`, a variant without fields: the value itself (design principles 5.3).
        EnumType *enumType = nullptr;
        size_t tag         = 0;
        if (enumVariantOfName(written, gct, enumType, tag)) {
            refOrigin_          = gct->origin();
            node_handle_t built = visitEnumConstruction(gct, enumType, tag);
            LEAVE("DREF");
            return built;
        }
    }
    const string name     = moduleLevelName(written);
    genericCalleeAllowed_ = false;
    refOrigin_            = gct->origin();
    if (camel::core::isOverloadableOperatorGroup(written)) {
        // An operator (`a + b` is `__add__(a, b)`): its overloads from every layer are one
        // set (design principles 10.2), not those of the first layer that has the name.
        node_handle_t drefNode = operatorDrefOf(written, gct);
        LEAVE("DREF");
        return drefNode;
    }
    auto optNode = nodeAt(name);
    if (optNode.has_value()) {
        node_handle_t node = optNode.value();
        if (!sameGraph(node, currGraph_)) {
            // A name bound to an operator or to a set of overloads has no value that a
            // capture could hold (it was captured as a port of type void, and the call of
            // it then failed with "Function of type 'void' cannot be called").
            rejectNameWithoutValue(node, name, gct);
            node = captureInto(currGraph_, node, name);
        }
        if (recorder_ && !nodeIsKind(node, runtime::GCNodeKind::Dref)) {
            recorder_->valueReferenced(
                gct->origin(),
                name,
                declarationOf(name),
                valueTypeOf(nodeTypeOf(node)));
        }
        LEAVE("DREF");
        return valueOf(node);
    }
    auto &graph = currGraph_;
    if (auto optTemplate = templateScope_->get(name); optTemplate.has_value()) {
        // The name of a generic function stands for a template, which has no graph. Called,
        // the arguments choose the instance; anywhere else a function is expected the
        // position does (design principles 5.5; genericValueFor), and until then the name
        // is this marker node.
        node_handle_t drefNode = graph->addDrefNode(
            std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>{
                std::make_shared<graph_set_t>()});
        genericDrefs_[drefNode] = optTemplate.value();
        registerNodeOrigin(context_, drefNode, gct, "gir.dref.generic");
        if (recorder_) {
            recorder_->genericReferenced(gct->origin(), name, optTemplate.value()->func->origin());
        }
        LEAVE("DREF");
        return drefNode;
    }
    if (auto named = graphsAt(name); named.has_value() && !decoratedGraphAt(name).has_value()) {
        // A function with decorators of the top level is what its decorators make of it,
        // which exists once its body is built: built now when it is not.
        for (const auto &candidate : **named) {
            auto known = topLevelFunctions_.find(candidate.get());
            if (known == topLevelFunctions_.end()) {
                continue;
            }
            if (!appliedDecoratorsOf(known->second->gct).empty()) {
                ensureFunctionBuilt(candidate);
            }
        }
    }
    auto optDecorated = decoratedGraphAt(name);
    if (optDecorated.has_value() && overloadsOfDecorated(name, *optDecorated) != nullptr) {
        // A decorated function that shares its name with other functions: the name stands
        // for the set, and a call chooses among them (visitLinkNode).
        optDecorated.reset();
    }
    if (optDecorated.has_value()) {
        node_handle_t drefNode = graph->addDrefNode(
            std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>{optDecorated.value()});
        registerNodeOrigin(context_, drefNode, gct, "gir.dref.decorated");
        if (recorder_) {
            reportFunctionReference(recorder_, gct, name, graphsAt(name));
        }
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
            if (recorder_) {
                reportFunctionReference(recorder_, gct, name, graphs);
            }
            LEAVE("DREF");
            return drefNode;
        }
    }
    // Layers 3 to 5 (design principles 10.2): imported by name (or through a module or
    // namespace prefix), then through a `using namespace`, then what comes along unasked.
    // Operators take their overloads from every layer.
    std::optional<camel::core::module::entity> opt;
    bool importedName = false;
    // A layer has the name when it brings something of it: an import by name of a name the
    // module does not export leaves the name to the next layers (and is reported only when
    // none has it).
    std::optional<camel::core::module::entity> named;
    if (!isOperatorName(written) && module_->hasImportedRef(written, ImportLayer::Named)) {
        importedName = true;
        named        = module_->getImportedEntity(written, ImportLayer::Named);
    }
    if (isOperatorName(written) || (named.has_value() && named->isOperGroup())) {
        // Operator groups (`+`, and the operators a module of operators brings) take their
        // overloads from every layer (design principles 10.2; the graph model, N3).
        importedName = importedName || module_->hasImportedRef(written);
        if (importedName) {
            opt = module_->getImportedEntity(written);
        }
    } else if (named.has_value()) {
        opt = named;
    } else if (auto match = throughUsing(written, gct)) {
        if (!match->ownName.empty()) {
            // A name of this module, opened by a `using namespace` of one of its namespaces.
            GCT::node_ptr_t resolved = tree_->makeDRef(match->ownName);
            resolved->setOrigin(gct->origin());
            LEAVE("DREF");
            return visitDRefNode(resolved);
        }
        importedName = true;
        opt          = match->entity;
    } else if (module_->hasImportedRef(written, ImportLayer::Ambient)) {
        importedName = true;
        opt          = module_->getImportedEntity(written, ImportLayer::Ambient);
    }
    if (importedName) {
        // An import of this name does not imply that the imported module exported it.
        if (!opt.has_value()) {
            diags_->of(SemanticDiag::ImportNameNotExported).atOrigin(gct->origin()).commit(name);
            throw BuildAbortException();
        }
        const auto &e = opt.value();
        if (e.isValue()) {
            // A constant a C++ module exports (`math::pi`; design principles 10.1): the
            // constant itself, here, as the same value written as a literal would be. It has
            // no node on the root graph and needs no capture.
            const GCT::node_ptr_t literal = tree_->makeData(e.value());
            literal->setOrigin(gct->origin());
            node_handle_t node = visitDataNode(literal);
            if (recorder_) {
                recorder_->valueReferenced(
                    gct->origin(),
                    name,
                    camel::source::kInvalidOriginId,
                    valueTypeOf(nodeTypeOf(node)));
            }
            LEAVE("DREF");
            return node;
        }
        if (camel::core::module::detail::EntityAccess::isNode(e)) {
            // A value exported by another module: a node of the root graph (design principles
            // 10.1), used like a value of this module's top level: on the root graph itself,
            // through a capture port in a function.
            node_handle_t node = exportedValueNode(
                *static_cast<ExportedValue *>(camel::core::module::detail::EntityAccess::node(e)));
            if (nodeIsKind(node, runtime::GCNodeKind::Dref)) {
                // A name bound to a set of overloads or to an operator has no value of its own.
                rejectNameWithoutValue(node, name, gct);
            }
            node = captureInto(currGraph_, node, name);
            if (recorder_) {
                recorder_->valueReferenced(
                    gct->origin(),
                    name,
                    camel::source::kInvalidOriginId,
                    valueTypeOf(nodeTypeOf(node)));
            }
            LEAVE("DREF");
            return node;
        } else if (camel::core::module::detail::EntityAccess::isGenericTemplate(e)) {
            // A generic function of another module: like a local one it is a marker until a
            // call or the position it stands in chooses the instance, which is built in this
            // module (instantiateGeneric).
            node_handle_t drefNode = graph->addDrefNode(
                std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>{
                    std::make_shared<graph_set_t>()});
            genericDrefs_[drefNode] = std::static_pointer_cast<GenericTemplate>(
                camel::core::module::detail::EntityAccess::genericTemplate(e));
            registerNodeOrigin(context_, drefNode, gct, "gir.dref.generic");
            LEAVE("DREF");
            return drefNode;
        } else if (camel::core::module::detail::EntityAccess::isGraphSet(e)) {
            auto graphs = asCompileGraphSet(camel::core::module::detail::EntityAccess::graphSet(e));
            node_handle_t drefNode{graph->addDrefNode(
                std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>{graphs})};
            registerNodeOrigin(context_, drefNode, gct, "gir.dref");
            if (recorder_) {
                reportFunctionReference(recorder_, gct, name, graphs);
            }
            LEAVE("DREF");
            return drefNode;
        } else if (e.isOperGroup()) {
            auto ops = e.operGroup();
            node_handle_t drefNode{graph->addDrefNode(
                std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>{ops})};
            registerNodeOrigin(context_, drefNode, gct, "gir.dref");
            if (recorder_) {
                std::vector<std::string> signatures;
                for (const auto &[uri, resolver] : ops->resolvers()) {
                    signatures.push_back(resolver->signature());
                }
                recorder_->operatorReferenced(gct->origin(), name, std::move(signatures));
            }
            LEAVE("DREF");
            return drefNode;
        } else if (camel::core::module::detail::EntityAccess::isDecoratedGraph(e)) {
            auto decorated = camel::core::module::detail::EntityAccess::decoratedGraph(e);
            node_handle_t drefNode{graph->addDrefNode(
                std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>{decorated})};
            registerNodeOrigin(context_, drefNode, gct, "gir.dref.decorated");
            if (recorder_) {
                recorder_->functionReferenced(gct->origin(), name, {});
            }
            LEAVE("DREF");
            return drefNode;
        }
    }
    if (isPoisonedName(name)) {
        throw BuildAbortException();
    }
    diags_->of(SemanticDiag::UnresolvedReference).atOrigin(gct->origin()).commit(name);
    throw BuildAbortException();
}

// ---- Operator overloads of the program (design principles 10.2) --------------------------
//
// Where this meets the resolution of names: an operator is the one kind of name that does not
// take the first layer having it (visitDRefNode, "local > this module > imported > using >
// built-in"); operatorOverloadsOf gathers every layer instead. The layers it reads are the
// graph scopes of this module (local functions and the module's own), the module's generic
// operator templates, and Module::importedOperatorOverloads (every import of a module brings
// its operator overloads, as for a C++ module's operators). Nothing else of the name lookup
// is involved, so the lookup can change around it as long as those three stay reachable.

Builder::OperatorOverloads Builder::operatorOverloadsOf(const std::string &group) {
    OperatorOverloads set;
    const auto addGraph = [&](const compile_graph_ptr_t &graph, const std::string &module) {
        if (graph && std::ranges::find(set.graphs, graph) == set.graphs.end()) {
            set.graphs.push_back(graph);
            if (!module.empty()) {
                set.moduleOf[graph.get()] = module;
            }
        }
    };
    // The program's own, from the innermost scope out: every layer, not the first one that has
    // the name.
    for (graph_scope_ptr_t scope = graphScope_; scope; scope = scope->outer()) {
        if (auto graphs = scope->get(group, false); graphs.has_value() && graphs.value()) {
            for (const auto &graph : *graphs.value()) {
                addGraph(graph, {});
            }
        }
    }
    if (auto it = operatorTemplates_.find(group); it != operatorTemplates_.end()) {
        for (const auto &[declaredIn, tmpl] : it->second) {
            for (template_scope_ptr_t scope = templateScope_; scope; scope = scope->outer()) {
                if (scope == declaredIn) {
                    set.templates.push_back(tmpl);
                    break;
                }
            }
        }
    }
    // Imported (the same at every use: the imports of a module are complete before its graphs
    // are built).
    const OperatorOverloads &imported = importedOperatorOverloadsOf(group);
    for (const auto &graph : imported.graphs) {
        addGraph(graph, imported.moduleOf.at(graph.get()));
    }
    for (const auto &tmpl : imported.templates) {
        if (std::ranges::find(set.templates, tmpl) == set.templates.end()) {
            set.templates.push_back(tmpl);
            set.moduleOf[tmpl.get()] = imported.moduleOf.at(tmpl.get());
        }
    }
    set.ops = imported.ops;
    return set;
}

const Builder::OperatorOverloads &Builder::importedOperatorOverloadsOf(const std::string &group) {
    const auto key = std::make_pair(static_cast<const void *>(module_.get()), group);
    if (auto it = importedOperators_.find(key); it != importedOperators_.end()) {
        return it->second;
    }
    // The operator groups of the built-in module and of the C++ modules merge into one group;
    // the overloads of modules written in Camel are functions and templates.
    OperatorOverloads set;
    std::vector<std::pair<std::string, resolver_ptr_t>> resolvers;
    for (const auto &imported : module_->importedOperatorOverloads(group)) {
        const auto &ent = imported.overloads;
        if (ent.isOperGroup()) {
            if (const auto &ops = ent.operGroup()) {
                for (const auto &overload : ops->resolvers()) {
                    const bool known = std::ranges::any_of(resolvers, [&](const auto &have) {
                        return have.first == overload.first && have.second == overload.second;
                    });
                    if (!known) {
                        resolvers.push_back(overload);
                    }
                }
            }
        } else if (camel::core::module::detail::EntityAccess::isGraphSet(ent)) {
            for (const auto &graph :
                 *asCompileGraphSet(camel::core::module::detail::EntityAccess::graphSet(ent))) {
                if (std::ranges::find(set.graphs, graph) == set.graphs.end()) {
                    set.graphs.push_back(graph);
                    set.moduleOf[graph.get()] = imported.mod->name();
                }
            }
        } else if (camel::core::module::detail::EntityAccess::isGenericTemplate(ent)) {
            auto tmpl = std::static_pointer_cast<GenericTemplate>(
                camel::core::module::detail::EntityAccess::genericTemplate(ent));
            if (std::ranges::find(set.templates, tmpl) == set.templates.end()) {
                set.templates.push_back(tmpl);
                set.moduleOf[tmpl.get()] = imported.mod->name();
            }
        }
    }
    set.ops = OperatorGroup::merged(group, std::move(resolvers));
    return importedOperators_.emplace(key, std::move(set)).first->second;
}

bool Builder::hasProgramOperatorOverloads(const std::string &group) {
    if (!camel::core::isOverloadableOperatorGroup(group)) {
        return false;
    }
    for (graph_scope_ptr_t scope = graphScope_; scope; scope = scope->outer()) {
        if (auto graphs = scope->get(group, false);
            graphs.has_value() && graphs.value() && !graphs.value()->empty()) {
            return true;
        }
    }
    if (auto it = operatorTemplates_.find(group);
        it != operatorTemplates_.end() && !it->second.empty()) {
        return true;
    }
    const OperatorOverloads &imported = importedOperatorOverloadsOf(group);
    return !imported.graphs.empty() || !imported.templates.empty();
}

node_handle_t Builder::operatorDrefOf(const std::string &group, const GCT::node_ptr_t &gct) {
    OperatorOverloads set = operatorOverloadsOf(group);
    // `!=` is not overloaded itself: an overload of `==` gives it (design principles 4.3).
    // Where one applies, `a != b` is the negation of the call of that overload; where none
    // does, the built-in `!=` (the group of the DREF, which set.ops is not) is used as before.
    bool negated = false;
    if (group == "__neq__") {
        if (OperatorOverloads equality = operatorOverloadsOf("__eq__");
            !equality.graphs.empty() || !equality.templates.empty()) {
            set.graphs    = std::move(equality.graphs);
            set.templates = std::move(equality.templates);
            set.moduleOf  = std::move(equality.moduleOf);
            negated       = true;
        }
    }
    ASSERT(set.ops != nullptr, "An operator has no group.");
    node_handle_t drefNode{currGraph_->addDrefNode(
        std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>{set.ops})};
    registerNodeOrigin(context_, drefNode, gct, "gir.dref");
    if (recorder_) {
        std::vector<std::string> signatures;
        for (const auto &[uri, resolver] : set.ops->resolvers()) {
            signatures.push_back(resolver->signature());
        }
        for (const auto &graph : set.graphs) {
            signatures.push_back(graph->funcType()->toString());
        }
        recorder_->operatorReferenced(gct->origin(), group, std::move(signatures));
    }
    if (!set.graphs.empty() || !set.templates.empty()) {
        operatorOverloads_[drefNode] = std::move(set);
        if (negated) {
            negatedEqualities_.insert(drefNode);
        }
    }
    return drefNode;
}

bool Builder::genericOperatorAccepts(const template_ptr_t &tmpl, const type_vec_t &normTypes) {
    FunctionType *signature = tmpl->funcType;
    if (signature->withTypesCount() != 0 || signature->normTypesCount() != normTypes.size()) {
        return false;
    }
    // As resolveGenericCall: the binding occurrences, then the others, then each argument
    // against its parameter with the variables replaced.
    type_bindings_t bindings;
    for (size_t i = 0; i < normTypes.size(); ++i) {
        if (!bindTypeVars(signature->normTypeAt(i), valueTypeOf(normTypes[i]), bindings)) {
            return false;
        }
    }
    for (size_t i = 0; i < normTypes.size(); ++i) {
        unifyTypeVars(signature->normTypeAt(i), valueTypeOf(normTypes[i]), bindings);
    }
    for (size_t i = 0; i < normTypes.size(); ++i) {
        Type *wanted = substituteTypeVars(signature->normTypeAt(i), bindings);
        if (containsTypeVar(wanted) || !entersImplicitly(wanted, valueTypeOf(normTypes[i]))) {
            return false;
        }
    }
    return true;
}

compile_graph_ptr_t Builder::chooseOperatorOverload(
    const OperatorOverloads &set, std::vector<Argument> &withInputs,
    std::vector<Argument> &normInputs, type_vec_t &withTypes, type_vec_t &normTypes,
    const GCT::node_ptr_t &call, const std::function<bool(FunctionType *, bool)> &accepts) {
    if (!withTypes.empty()) {
        return nullptr; // an operator is never given with-arguments; the group reports it
    }
    const char *symbol = operatorSymbolOf(set.ops->name());
    std::vector<StaticArg> statics;
    for (const Argument &input : normInputs) {
        statics.push_back(staticArgOf(input.node));
    }
    const bool builtinAccepts =
        set.ops->resolve(withTypes, normTypes, statics, Modifier::None).has_value();
    // Design principles 4.3: an overload of the program is preferred where it is more
    // specific than the others that take the arguments. "More specific" is exact: its
    // parameter types are the argument types themselves (as written, without a value entering
    // `T?` / `T!`), while the others take them only as members of a family of types: a
    // built-in or C++ module operator whose type rule accepts several types (the comparison of
    // arrays, tuples and structs by content, an arithmetic over tensors), or a generic
    // overload. A built-in overload declared for exactly these types is as specific, and
    // the two stay a conflict (rejectOperatorConflict) or an ambiguity.
    const auto exactFor = [&](FunctionType *type) {
        if (type == nullptr || type->withTypesCount() != 0 ||
            type->normTypesCount() != normTypes.size()) {
            return false;
        }
        for (size_t i = 0; i < normTypes.size(); ++i) {
            if (!type->normTypeAt(i)->equals(valueTypeOf(normTypes[i]))) {
                return false;
            }
        }
        return true;
    };
    const bool builtinExact = std::ranges::any_of(set.ops->resolvers(), [&](const auto &entry) {
        FunctionType *fixed = entry.second->fixedSignature();
        return fixed != nullptr && exactFor(fixed) &&
               entry.second->resolve(withTypes, normTypes, Modifier::None).has_value();
    });
    const auto where        = [&](const void *overload) {
        const auto it = set.moduleOf.find(overload);
        return it == set.moduleOf.end() ? std::string(" (this module)")
                                        : std::format(" (module '{}')", it->second);
    };
    // Exactly as given first, then with the arguments entering `T?` / `T!` parameters (as for
    // a call of a function, visitLinkNode). The built-in overloads take only the first.
    for (const bool entering : {false, true}) {
        std::vector<compile_graph_ptr_t> graphs;
        std::vector<template_ptr_t> templates;
        for (const auto &graph : set.graphs) {
            if (accepts(graph->funcType(), entering)) {
                graphs.push_back(graph);
            }
        }
        if (!entering) {
            for (const auto &tmpl : set.templates) {
                if (genericOperatorAccepts(tmpl, normTypes)) {
                    templates.push_back(tmpl);
                }
            }
        }
        bool useBuiltin = builtinAccepts && !entering;
        size_t fits     = graphs.size() + templates.size() + (useBuiltin ? 1 : 0);
        if (fits == 0) {
            continue;
        }
        if (fits > 1 && !entering && !builtinExact) {
            // One exact overload of the program among several that fit: it wins (above).
            std::vector<compile_graph_ptr_t> exact;
            for (const auto &graph : graphs) {
                if (exactFor(graph->funcType())) {
                    exact.push_back(graph);
                }
            }
            if (exact.size() == 1) {
                graphs     = std::move(exact);
                templates  = {};
                useBuiltin = false;
                fits       = 1;
            }
        }
        if (fits > 1) {
            // One line, as every diagnostic message is: the overloads separated by `;`.
            std::vector<std::string> overloads;
            for (const auto &graph : graphs) {
                overloads.push_back(
                    std::format(
                        "operator {}: {}{}",
                        symbol,
                        graph->funcType()->toString(),
                        where(graph.get())));
            }
            for (const auto &tmpl : templates) {
                overloads.push_back(
                    std::format(
                        "operator {}: {}{}",
                        symbol,
                        tmpl->funcType->toString(),
                        where(tmpl.get())));
            }
            if (builtinAccepts && !entering) {
                for (const auto &[uri, resolver] : set.ops->resolvers()) {
                    if (resolver->resolve(withTypes, normTypes, Modifier::None).has_value()) {
                        overloads.push_back(
                            std::format(
                                "the built-in or module operator <{}>: {}",
                                uri,
                                resolver->signature()));
                    }
                }
            }
            // At the operator: the origin of a binary call is its right operand.
            diags_->of(SemanticDiag::AmbiguousOperatorCall)
                .atOrigin(call->at(0)->origin())
                .commit(
                    symbol,
                    strutil::join(normTypes, ", ", [](Type *t) { return t->toString(); }),
                    strutil::join(overloads, "; ", [](const std::string &o) { return o; }));
            throw BuildAbortException();
        }
        if (useBuiltin) {
            return nullptr;
        }
        if (!templates.empty()) {
            return resolveGenericCall(
                templates.front(),
                withInputs,
                normInputs,
                withTypes,
                normTypes,
                call);
        }
        const compile_graph_ptr_t &chosen = graphs.front();
        if (!chosen->funcType()->hasExitType()) {
            if (failedBodies_.contains(chosen.get())) {
                throw BuildAbortException(); // its body is in error, reported there
            }
            diags_->of(SemanticDiag::CallingIncompleteFunction)
                .atOrigin(call->origin())
                .commit(displayNameOf(chosen->name()), chosen->funcType()->toString());
            throw BuildAbortException();
        }
        return chosen;
    }
    return nullptr;
}

void Builder::rejectOperatorConflict(
    const std::string &group, FunctionType *funcType, const GCT::node_ptr_t &gct) {
    if (!camel::core::isOverloadableOperatorGroup(group) ||
        !checkedOperatorSignatures_.insert(funcType).second) {
        return;
    }
    type_vec_t params;
    for (size_t i = 0; i < funcType->normTypesCount(); ++i) {
        params.push_back(funcType->normTypeAt(i));
        if (containsTypeVar(funcType->normTypeAt(i))) {
            // A generic overload: which arguments it takes is known at a call
            // (chooseOperatorOverload reports when it and another one both take them).
            return;
        }
    }
    const std::string symbol = operatorSymbolOf(group);
    const std::string types  = strutil::join(params, ", ", [](Type *t) { return t->toString(); });
    const OperatorOverloads set = operatorOverloadsOf(group);
    // A built-in (or C++ module) overload declared for exactly these parameter types is as
    // specific as this one: the two cannot both be meant. One that takes them as members of a
    // family of types (by content, a type rule over several types) is less specific, and this
    // one is preferred where both apply (design principles 4.3; chooseOperatorOverload).
    const bool sameAsBuiltin = std::ranges::any_of(set.ops->resolvers(), [&](const auto &entry) {
        const FunctionType *fixed = entry.second->fixedSignature();
        if (fixed == nullptr || fixed->withTypesCount() != 0 ||
            fixed->normTypesCount() != params.size()) {
            return false;
        }
        for (size_t i = 0; i < params.size(); ++i) {
            if (!fixed->normTypeAt(i)->equals(params[i])) {
                return false;
            }
        }
        return true;
    });
    if (sameAsBuiltin) {
        diags_->of(SemanticDiag::OperatorOverloadConflict)
            .atOrigin(gct->origin())
            .commit(symbol, types, " by the built-in operator or by an operator of a C++ module");
        throw BuildAbortException();
    }
    // An overload of another module with the same parameters (those of this module are a
    // Redeclaration, reported by rejectRedeclaredFunction).
    for (const auto &graph : set.graphs) {
        const auto module   = set.moduleOf.find(graph.get());
        FunctionType *other = graph->funcType();
        if (module == set.moduleOf.end() || other->normTypesCount() != params.size() ||
            other->withTypesCount() != 0) {
            continue;
        }
        bool same = true;
        for (size_t i = 0; same && i < params.size(); ++i) {
            same = other->normTypeAt(i)->equals(params[i]);
        }
        if (same) {
            diags_->of(SemanticDiag::OperatorOverloadConflict)
                .atOrigin(gct->origin())
                .commit(symbol, types, std::format(" by module '{}'", module->second));
            throw BuildAbortException();
        }
    }
}

void Builder::exportOperatorOverloads(const std::string &group, const GCT::node_ptr_t &gct) {
    bool exported = false;
    if (auto graphs = graphScope_->get(group, false);
        graphs.has_value() && graphs.value() && !graphs.value()->empty()) {
        module_->exportEntity(
            Reference(group),
            camel::core::module::detail::EntityAccess::makeGraphSet(
                eraseCompileGraphSet(graphs.value())));
        exported = true;
    }
    // Each generic overload under a name of its own (`__add__#0`): an exported name holds one
    // entity, and a template is not a graph a set could hold.
    if (auto it = operatorTemplates_.find(group); it != operatorTemplates_.end()) {
        size_t index = 0;
        for (const auto &[declaredIn, tmpl] : it->second) {
            if (declaredIn != templateScope_) {
                continue; // declared inside a function: not the module's
            }
            const Reference ref(std::format("{}#{}", group, index++));
            module_->exportEntity(
                ref,
                camel::core::module::detail::EntityAccess::makeGenericTemplate(
                    std::static_pointer_cast<void>(tmpl)));
            exported = true;
        }
    }
    if (!exported) {
        diags_->of(SemanticDiag::UnresolvedReference)
            .atOrigin(gct->origin())
            .commit(displayNameOf(group));
        throw BuildAbortException();
    }
}

node_handle_t Builder::visitCastNode(const GCT::node_ptr_t &gct) {
    ENTER("CAST");
    ExpectedValueScope expectedScope(expectedValueType_, boundType(gct->targetType()));
    if (const auto &operand = gct->at(0);
        operand->type() == GCT::NodeKind::DATA && containsTypeVar(gct->targetType()) &&
        asTagged(gct->targetType()) == nullptr &&
        asCollectionTypeOrPrototype(gct->targetType()) == nullptr) {
        // A literal converted to a type written with a type variable (`let zero: T = 0`,
        // `0 as T`). The GCT converts a literal to a concrete type itself; here the type is
        // only known now, so the same conversion is applied now.
        const data_ptr_t &data = operand->data();
        Type *targetType       = boundType(gct->targetType());
        const bool literal =
            !isComposite(data->type()->code()) || tt::as_shared<CompositeData>(data)->resolved();
        if (literal) {
            data_ptr_t converted;
            if (gct->annotation() || targetType->castSafetyFrom(data->type()) == CastSafety::Safe) {
                converted = data->convertTo(targetType);
            }
            if (!converted) {
                if (const std::string limits = literalRangeViolation(data, targetType);
                    !limits.empty()) {
                    diags_->of(SemanticDiag::LiteralOutOfRange)
                        .atOrigin(gct->origin())
                        .commit(literalValueText(data), targetType->toString(), limits);
                    throw BuildAbortException();
                }
                diags_->of(SemanticDiag::LiteralStaticCastFailed)
                    .atOrigin(gct->origin())
                    .commit(data->toString(), targetType->toString());
                throw BuildAbortException();
            }
            node_handle_t node = currGraph_->addStaticDataNode(converted);
            if (varied_ && currGraph_->outerGraph().get() != nullptr) {
                node_handle_t copyNode = currGraph_->addCopyNode(converted->type());
                linkNodes(LinkType::Norm, node, copyNode);
                node = copyNode;
            }
            registerNodeOrigin(context_, node, gct, "gir.data");
            LEAVE("CAST");
            return node;
        }
    }
    Type *targetType = boundType(gct->targetType());
    if (targetType != nullptr && targetType->code() == TypeCode::Function) {
        // A function type written for a function: it says which function a name stands for
        // (the instance of a generic function, one of several overloads, a partial
        // application), and it must be the type of any other value. Nothing is converted.
        auto *wanted       = tt::as_ptr<FunctionType>(targetType);
        node_handle_t node = valueNodeOf(visit(gct->at(0)));
        if (genericDrefs_.contains(node)) {
            node = genericValueFor(node, wanted, gct->at(0));
        } else if (nodeIsKind(node, runtime::GCNodeKind::Dref)) {
            node_handle_t chosen{};
            if (boundWithArgs_.contains(node)) {
                chosen = partialApplicationOf(node, gct->at(0), wanted);
            }
            if (chosen == nullptr) {
                chosen = overloadValueFor(node, wanted, gct->at(0));
            }
            node = chosen != nullptr ? chosen : functionValueOf(node, gct->at(0));
            rejectNameWithoutValue(node, "the name", gct->at(0));
        } else {
            node = functionValueOf(node, gct->at(0));
        }
        Type *actual = valueTypeOf(nodeTypeOf(node));
        if (!targetType->assignableFrom(actual)) {
            diags_->of(SemanticDiag::DynamicCastForbidden)
                .atOrigin(gct->origin())
                .commit(actual->toString(), targetType->toString());
            throw BuildAbortException();
        }
        LEAVE("CAST");
        return node;
    }
    // A collection type written with a type variable is only checked now that it is replaced.
    rejectInvalidCollectionKey(targetType, gct);
    node_handle_t valueNode{};
    if (TupleType *tagged = asTagged(targetType);
        tagged != nullptr && gct->at(0)->type() == GCT::NodeKind::DATA) {
        // A literal put into `T?` / `T!`: it is first the literal of the payload type, by the
        // rule for any annotated literal (`let x: float? = 1`); `null` stays `null`.
        data_ptr_t data = gct->at(0)->data();
        const bool literal =
            !isComposite(data->type()->code()) || tt::as_shared<CompositeData>(data)->resolved();
        Type *payload = tagged->payloadType();
        if (literal && data->type()->code() != TypeCode::Void && !payload->equals(data->type()) &&
            (gct->annotation() || payload->castSafetyFrom(data->type()) == CastSafety::Safe)) {
            if (data_ptr_t converted = data->convertTo(payload)) {
                valueNode = currGraph_->addStaticDataNode(converted);
                registerNodeOrigin(context_, valueNode, gct->at(0), "gir.data");
            } else if (
                const std::string limits = literalRangeViolation(data, payload); !limits.empty()) {
                diags_->of(SemanticDiag::LiteralOutOfRange)
                    .atOrigin(gct->origin())
                    .commit(literalValueText(data), payload->toString(), limits);
                throw BuildAbortException();
            }
        }
    }
    if (valueNode == nullptr) {
        const auto &res = visit(gct->at(0));
        ASSERT(
            res.type() == typeid(node_handle_t),
            "Unexpected result type from child of CAST node.");
        valueNode = any_cast<node_handle_t>(res);
    }
    ASSERT(valueNode != nullptr, "Cast node value is null.");
    Type *sourceType = nodeTypeOf(valueNode);
    if (gct->annotation() && asTagged(targetType) == nullptr &&
        gct->at(0)->type() != GCT::NodeKind::DATA) {
        // The type a declaration is written with (`let x: T = e`; GCT Builder::declaredValue):
        // `e` must already have it. A value of that very type passes as it is (converting a
        // value to its own type does nothing, design principles 5.1); one of a type the
        // declared type accepts is cast to it, so that the name has the declared type; any
        // other is refused, as numbers are not converted implicitly (design principles 5.1).
        Type *have = valueTypeOf(sourceType);
        if (targetType->equals(have)) {
            LEAVE("CAST");
            return valueNode;
        }
        if (!targetType->assignableFrom(have)) {
            diags_->of(SemanticDiag::DeclaredTypeMismatch)
                .atOrigin(gct->origin())
                .commit(have->toString(), targetType->toString());
            throw BuildAbortException();
        }
    }
    if (asTagged(targetType) != nullptr &&
        targetType->castSafetyFrom(sourceType) != CastSafety::Safe) {
        // Several levels at once (`let x: int?! = 5`): one CAST per level.
        if (node_handle_t entered = coerceTo(valueNode, targetType, gct)) {
            LEAVE("CAST");
            return entered;
        }
    }
    // Safe conversions always; an Unsafe one only into a module type, which then checks the
    // value when the conversion runs (Type::castSlotFrom of that type). Primitive conversions
    // that may lose information stay refused.
    const CastSafety safety = targetType->castSafetyFrom(sourceType);
    if (safety != CastSafety::Safe &&
        !(safety == CastSafety::Unsafe && targetType->isOtherType())) {
        diags_->of(SemanticDiag::DynamicCastForbidden)
            .atOrigin(gct->origin())
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
// dependency edge; a self-recursive graph lists itself as a dependency.
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

node_handle_t Builder::elementNodeOf(const std::string &name, const GCT::node_ptr_t &literal) {
    node_handle_t node{};
    refOrigin_ = literal->origin();
    if (const std::string own = moduleLevelName(name); nodeAt(own).has_value()) {
        node = valueOf(resolveNodeByRef(own));
    } else {
        // Not a value that was bound: the name of a function (`[inc, dec]`), looked up as
        // the name is anywhere else. An unknown name is reported at the literal.
        const GCT::node_ptr_t dref = tree_->makeDRef(name);
        dref->setOrigin(literal->origin());
        node = visitDRefNode(dref);
    }
    return namedValueOf(node, name, literal);
}

node_handle_t
Builder::namedValueOf(node_handle_t node, const std::string &name, const GCT::node_ptr_t &origin) {
    node = functionValueOf(node, origin);
    rejectNameWithoutValue(node, name, origin);
    return node;
}

node_handle_t Builder::functionValueOf(node_handle_t node, const GCT::node_ptr_t &origin) {
    if (boundWithArgs_.contains(node)) {
        // `f<w>` as a value is a partial application (design principles 4.4). The binding
        // used to be dropped here, without a word.
        if (node_handle_t applied = partialApplicationOf(node, origin)) {
            return applied;
        }
        // Not one function for these arguments. A name may still be settled by the position
        // it stands in (a parameter of function type); a value cannot.
        if (!nodeIsKind(node, runtime::GCNodeKind::Dref)) {
            rejectUnappliedBinding(node, origin);
        }
        return node;
    }
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
    if (std::holds_alternative<oper_group_ptr_t>(target)) {
        // A library operator with one signature that does not depend on its arguments
        // (`trim: (s: string) => string`) is one function, like a user function of that
        // name: its value is a function that calls it.
        const auto &ops = std::get<oper_group_ptr_t>(target);
        if (ops->resolvers().size() == 1) {
            const auto &[uri, resolver] = ops->resolvers().front();
            if (FunctionType *fixed = resolver->fixedSignature()) {
                auto op = std::make_shared<OperatorIndex>(ops->name(), fixed, uri);
                if (compile_graph_ptr_t graph = operatorValueGraphOf(op, origin)) {
                    return createFuncDataNode(graph, true, false);
                }
            }
        }
    }
    return node;
}

compile_graph_ptr_t
Builder::operatorValueGraphOf(const oper_idx_ptr_t &op, const GCT::node_ptr_t &origin) {
    FunctionType *opType = op->funcType();
    if (opType == nullptr || !opType->hasExitType()) {
        return nullptr;
    }
    for (size_t i = 0; i < opType->withTypesCount(); ++i) {
        if (opType->withIsVarAt(i)) {
            return nullptr;
        }
    }
    for (size_t i = 0; i < opType->normTypesCount(); ++i) {
        if (opType->normIsVarAt(i)) {
            return nullptr;
        }
    }
    // One per place that names the operator: the function is built where it is used.
    const std::string key = std::format(
        "{}|{}|{}",
        static_cast<const void *>(currGraph_.get()),
        op->uri(),
        opType->toString());
    if (auto known = operatorValueGraphs_.find(key); known != operatorValueGraphs_.end()) {
        return known->second;
    }
    // Its own type object (a graph completes its function type in place).
    FunctionTypeFactory factory;
    factory.setModifiers(opType->modifiers());
    factory.setExitType(opType->exitType());
    const auto nameAt = [&](size_t argIndex, const char *prefix, size_t i) {
        return argIndex < opType->argNamesCount() && !opType->argNameAt(argIndex).empty()
                   ? std::string(opType->argNameAt(argIndex))
                   : std::format("{}{}", prefix, i);
    };
    for (size_t i = 0; i < opType->withTypesCount(); ++i) {
        factory.addWithArg(nameAt(i, "__w", i), opType->withTypeAt(i), false);
    }
    for (size_t i = 0; i < opType->normTypesCount(); ++i) {
        factory.addNormArg(
            nameAt(opType->withTypesCount() + i, "__n", i),
            opType->normTypeAt(i),
            false);
    }
    FunctionType *type = factory.build();

    compile_graph_ptr_t graph = enterScope(type);
    try {
        bodyStates_[graph.get()] = BodyState::Open;
        node_handle_t call       = graph->addOperNode(op, 0);
        stampPermissions(call);
        if (opType->modifiers().macro()) {
            setNodeMacro(call, true);
        }
        registerNodeOrigin(context_, call, origin, "gir.link.oper");
        for (draft_node_ref_t port : graph->draft().withPorts()) {
            linkNodes(LinkType::With, graph->draft().node(port), call);
        }
        for (draft_node_ref_t port : graph->draft().normPorts()) {
            linkNodes(LinkType::Norm, graph->draft().node(port), call);
        }
        setGraphReturnFromResult(graph, nodeIdOf(call), nodeIdOf(call), opType->exitType());
        bodyStates_[graph.get()] = BodyState::Built;
        unit_->builtBodies.push_back(graph);
    } catch (...) {
        bodyStates_[graph.get()] = BodyState::Built;
        leaveScope();
        throw;
    }
    leaveScope();
    operatorValueGraphs_[key] = graph;
    return graph;
}

node_handle_t Builder::overloadValueFor(
    node_handle_t node, FunctionType *expected, const GCT::node_ptr_t &origin) {
    if (!nodeIsKind(node, runtime::GCNodeKind::Dref) || expected == nullptr ||
        !expected->hasExitType()) {
        return nullptr;
    }
    const type_vec_t withTypes(expected->withTypesSpan().begin(), expected->withTypesSpan().end());
    const type_vec_t normTypes(expected->normTypesSpan().begin(), expected->normTypesSpan().end());
    const auto &target = nodeGraphOf(node)->drefTarget(node);
    if (std::holds_alternative<graph_vec_ptr_t>(target)) {
        auto graphs = asCompileGraphVec(target);
        compile_graph_ptr_t chosen;
        if (!graphs) {
            return nullptr;
        }
        for (const auto &g : *graphs) {
            FunctionType *type = g->funcType();
            if (decoratedOf_.contains(g.get()) || !type->hasExitType() ||
                type->withTypesCount() != withTypes.size() ||
                type->normTypesCount() != normTypes.size() ||
                !type->exitType()->equals(expected->exitType())) {
                continue;
            }
            bool same = true;
            for (size_t i = 0; same && i < withTypes.size(); ++i) {
                same = type->withTypeAt(i)->equals(withTypes[i]) && !type->withIsVarAt(i);
            }
            for (size_t i = 0; same && i < normTypes.size(); ++i) {
                same = type->normTypeAt(i)->equals(normTypes[i]) &&
                       type->normIsVarAt(i) == expected->normIsVarAt(i);
            }
            if (!same) {
                continue;
            }
            if (chosen) {
                return nullptr;
            }
            chosen = g;
        }
        return chosen ? createFuncDataNode(chosen, true, false) : nullptr;
    }
    if (std::holds_alternative<oper_group_ptr_t>(target)) {
        const auto &ops = std::get<oper_group_ptr_t>(target);
        oper_idx_ptr_t chosen;
        const auto typed = explicitOperatorTypes_.find(node);
        const type_vec_t explicitTypes =
            typed == explicitOperatorTypes_.end() ? type_vec_t{} : typed->second;
        const std::vector<StaticArg> statics(normTypes.size());
        for (const auto &[uri, resolver] : ops->resolvers()) {
            auto type = resolver->resolveForCall(
                explicitTypes,
                expected->exitType(),
                withTypes,
                normTypes,
                statics,
                Modifier::None);
            if (!type || !(*type)->hasExitType() ||
                !(*type)->exitType()->equals(expected->exitType())) {
                continue;
            }
            if (chosen) {
                return nullptr;
            }
            chosen = std::make_shared<OperatorIndex>(ops->name(), *type, uri);
        }
        if (chosen) {
            explicitOperatorTypes_.erase(node);
            if (compile_graph_ptr_t graph = operatorValueGraphOf(chosen, origin)) {
                return createFuncDataNode(graph, true, false);
            }
        }
    }
    return nullptr;
}

void Builder::rejectNameWithoutValue(
    node_handle_t node, const std::string &name, const GCT::node_ptr_t &origin) {
    if (!nodeIsKind(node, runtime::GCNodeKind::Dref)) {
        return;
    }
    rejectUnappliedBinding(node, origin);
    if (genericDrefs_.contains(node)) {
        (void)genericValueFor(node, nullptr, origin); // nothing here fixes its types
    }
    const auto &target = nodeGraphOf(node)->drefTarget(node);
    std::string what   = "an operator with several signatures";
    if (std::holds_alternative<graph_vec_ptr_t>(target)) {
        auto graphs = asCompileGraphVec(target);
        what        = std::format("{} functions of that name", graphs ? graphs->size() : 0);
        if (graphs && !graphs->empty()) {
            what += " [" +
                    strutil::join(
                        *graphs,
                        "; ",
                        [](const graph_ptr_t &g) { return g->funcType()->toString(); }) +
                    "]";
        }
    } else if (std::holds_alternative<oper_group_ptr_t>(target)) {
        // The candidates, so that the reader can write the wrapper the message asks for.
        const auto &ops        = std::get<oper_group_ptr_t>(target);
        const std::string sigs = strutil::join(
            ops->resolvers(),
            "; ",
            [](const std::pair<std::string, resolver_ptr_t> &p) { return p.second->signature(); });
        if (ops->resolvers().size() == 1) {
            const bool fixed = ops->resolvers().front().second->fixedSignature() != nullptr;
            what = fixed ? std::format("an operator that changes its argument in place [{}]", sigs)
                         : std::format(
                               "an operator whose signature depends on its arguments [{}]",
                               sigs);
        } else {
            what =
                std::format("an operator with {} signatures [{}]", ops->resolvers().size(), sigs);
        }
    }
    diags_->of(SemanticDiag::FunctionNameAsValue).atOrigin(origin->origin()).commit(name, what);
    throw BuildAbortException();
}

void Builder::reportImportNameConflicts() {
    // Two imports by name that bring functions of the same name and the same parameter types
    // (design principles 10.2): the call used to take the function of the import written
    // first, silently. Functions of different parameter types form a set of overloads, as
    // before; whether that stays is undecided.
    if (!module_) {
        return;
    }
    const auto sameSignature = [](FunctionType *a, FunctionType *b) {
        if (!a || !b || a->withTypesCount() != b->withTypesCount() ||
            a->normTypesCount() != b->normTypesCount()) {
            return false;
        }
        for (size_t i = 0; i < a->withTypesCount(); ++i) {
            if (!a->withTypeAt(i)->equals(b->withTypeAt(i))) {
                return false;
            }
        }
        for (size_t i = 0; i < a->normTypesCount(); ++i) {
            if (!a->normTypeAt(i)->equals(b->normTypeAt(i))) {
                return false;
            }
        }
        return true;
    };
    const auto signatureOf = [](FunctionType *type) {
        std::string text;
        if (type->withTypesCount() > 0) {
            text += "<";
            for (size_t i = 0; i < type->withTypesCount(); ++i) {
                text += (i ? ", " : "") + type->withTypeAt(i)->toString();
            }
            text += "> ";
        }
        text += "(";
        for (size_t i = 0; i < type->normTypesCount(); ++i) {
            text += (i ? ", " : "") + type->normTypeAt(i)->toString();
        }
        return text + ")";
    };
    bool found = false;
    for (const auto &[ref, imports] : module_->namesImportedMoreThanOnce()) {
        for (size_t later = 1; later < imports.size(); ++later) {
            bool reported = false;
            for (size_t earlier = 0; earlier < later && !reported; ++earlier) {
                const auto first = imports[earlier].mod->getExportedEntity(imports[earlier].remote);
                const auto second = imports[later].mod->getExportedEntity(imports[later].remote);
                if (!first || !second) {
                    continue;
                }
                using camel::core::module::detail::EntityAccess;
                // What the two imports bring that cannot stand side by side: two functions of
                // the same parameter types, or a value and anything else of that name (a value
                // is not overloaded).
                std::optional<std::string> clash;
                // A value: exported by a module written in Camel (a node of the root graph) or
                // a constant of a C++ module (design principles 10.1).
                const auto isValue = [](const camel::core::module::entity &e) {
                    return EntityAccess::isNode(e) || e.isValue();
                };
                if (isValue(*first) || isValue(*second)) {
                    const bool same =
                        (EntityAccess::isNode(*first) && EntityAccess::isNode(*second) &&
                         EntityAccess::node(*first) == EntityAccess::node(*second)) ||
                        (first->isValue() && second->isValue() &&
                         first->value() == second->value());
                    if (!same) {
                        clash = isValue(*first) && isValue(*second)
                                    ? std::string("a value in both")
                                    : std::string("a value in one of them");
                    }
                } else if (EntityAccess::isGraphSet(*first) && EntityAccess::isGraphSet(*second)) {
                    const auto firstSet  = asCompileGraphSet(EntityAccess::graphSet(*first));
                    const auto secondSet = asCompileGraphSet(EntityAccess::graphSet(*second));
                    for (const auto &f : *firstSet) {
                        for (const auto &g : *secondSet) {
                            if (!clash && f != g && sameSignature(f->funcType(), g->funcType())) {
                                clash = "the same signature " + signatureOf(f->funcType());
                            }
                        }
                    }
                }
                if (clash) {
                    auto diag         = diags_->of(SemanticDiag::ImportNameConflict);
                    const auto &here  = imports[later];
                    const auto &there = imports[earlier];
                    if (here.origin != camel::source::kInvalidOriginId) {
                        diag.atOrigin(here.origin);
                    } else {
                        diag.at(TokenRange{here.tokenStart, here.tokenEnd});
                    }
                    if (there.origin != camel::source::kInvalidOriginId) {
                        diag.relatedOrigin(there.origin, "also imported here");
                    } else {
                        diag.related(
                            TokenRange{there.tokenStart, there.tokenEnd},
                            "also imported here");
                    }
                    diag.commit(
                        ref.toString(),
                        imports[earlier].mod->name(),
                        imports[later].mod->name(),
                        *clash);
                    reported = true;
                    found    = true;
                }
            }
        }
    }
    if (found) {
        throw BuildAbortException();
    }
}

node_handle_t Builder::createFuncDataNode(
    const graph_ptr_t &graph, bool callableAsResult, bool allowParameterization,
    node_handle_t branch) {
    ASSERT(
        !(callableAsResult && allowParameterization),
        "Cannot enable both callableAsResult and allowParameterization options.");
    ASSERT(currGraph_ != nullptr, "Current owner graph is null when creating a function node.");
    ASSERT(graph != nullptr, "Target graph is null when creating a function node.");
    // Its captures are known once its body is built: a function of the top level is built
    // here unless it is (or it is being built: a recursive use).
    ensureFunctionBuilt(graph);

    // Centralize dependency bookkeeping here so every FUNC node and static Function value obeys
    // the same graph-reference invariant. Self-dependency is intentional: a recursive graph lists
    // itself as a dependency.
    currGraph_->addDependencyGraph(graph);

    auto sourceContext = context_ ? context_->sourceContext() : nullptr;
    auto graphOrigin   = sourceContext ? sourceContext->debugMap().graphOrigin(graph->stableId())
                                       : camel::source::kInvalidOriginId;
    auto markMacroNode = [&](node_handle_t node) {
        if (node != nullptr && isMacroGraph(graph)) {
            setNodeMacro(node, true);
        }
    };
    const bool hasClosure = !graph->draft().closureNodes().empty();
    const bool valueMade  = valueUsedGraphs_.contains(graph.get());

    if (allowParameterization && (graph->parameterized() || !hasClosure || !valueMade)) {
        // A direct call: a FUNC node. At the first one the callee's capture ports become
        // with-ports after its declared ones (it is "parameterized"); every direct call then
        // passes its captures as With inputs after the declared `with` arguments, linked by
        // the link step (addCaptureSite, linkCaptureSites).
        if (!graph->parameterized()) {
            const std::vector<draft_node_ref_t> closureNodes(
                graph->draft().closureNodes().begin(),
                graph->draft().closureNodes().end());
            for (draft_node_ref_t closureId : closureNodes) {
                graph->draft().removeNormPort(closureId);
                graph->draft().appendWithPort(closureId);
                graph->draft().removeClosureNode(closureId);
            }
            graph->setParameterized(true);
        }
        node_handle_t funcNode = currGraph_->addFuncNode(graph, 0);
        markMacroNode(funcNode);
        addCaptureSite(funcNode, graph, branch);
        bindGraphScopedFuncNodeDebug(sourceContext, graphOrigin, graph, funcNode);
        return funcNode;
    }

    if (callableAsResult && graph->parameterized() && graph->funcType()->hasExitType() &&
        !capturedValues_[graph.get()].empty()) {
        // A direct call has turned the captures of `graph` into with-parameters, which
        // each direct call passes. A function value has no place for them: called through
        // the value, the function read parameters nobody had set. The value is therefore
        // that of a function which captures the same values and passes them on.
        return createFuncDataNode(forwardingGraphOf(graph), true, false);
    }

    // A function value. It holds the captures the graph has now (filled into it), so the
    // graph can take no more (see captureInto).
    valueUsedGraphs_.insert(graph.get());
    auto *staticFunc     = createStaticFunction(currGraph_, graph);
    const auto slotIndex = currGraph_->addStaticSlot(
        camel::core::rtdata::toSlot<StaticFunction *>(staticFunc),
        graph->funcType());
    auto dataNode = currGraph_->draft().node(currGraph_->draft().addDataNode(
        graph->funcType(),
        slotIndex,
        static_cast<uint8_t>(runtime::kGCNodeFlagConstant)));
    markMacroNode(dataNode);
    node_handle_t resultNode = dataNode;
    if (hasClosure) {
        // The closure: the captured values, as the current graph sees them, in the order of
        // the closure ports.
        node_vec_t refNodes;
        const std::vector<CapturedValue> captured = capturedValues_[graph.get()];
        for (const CapturedValue &value : captured) {
            refNodes.push_back(captureInto(currGraph_, value.origin, value.name));
        }
        std::vector<size_t> closureSlots;
        closureSlots.reserve(refNodes.size());
        for (size_t i = 0; i < refNodes.size(); ++i) {
            closureSlots.push_back(i);
        }
        auto fillPayload =
            runtime::makeFillPayload(runtime::GCFillKind::FunctionClosure, closureSlots);
        auto fillNode = currGraph_->addFillNode(graph->funcType(), fillPayload);
        markMacroNode(fillNode);
        linkNodes(LinkType::Norm, dataNode, fillNode);
        for (const auto &refNode : refNodes) {
            linkNodes(LinkType::With, refNode, fillNode);
        }
        resultNode = fillNode;
    }
    if (!callableAsResult) {
        // A direct call of a graph whose value was made before: called through a value.
        auto callNode = currGraph_->addCallNode(graph->funcType()->exitType());
        markMacroNode(callNode);
        linkNodes(LinkType::With, resultNode, callNode);
        resultNode = callNode;
    }
    bindGraphScopedFuncNodeDebug(sourceContext, graphOrigin, graph, resultNode);
    return resultNode;
}

node_handle_t Builder::partialApplicationOf(
    node_handle_t target, const GCT::node_ptr_t &origin, FunctionType *expected) {
    const auto boundIt = boundWithArgs_.find(target);
    if (boundIt == boundWithArgs_.end() || genericDrefs_.contains(target)) {
        return nullptr;
    }
    std::vector<Argument> bound = boundIt->second;
    type_vec_t boundTypes;
    for (Argument &arg : bound) {
        arg.node = functionValueOf(arg.node, arg.expr);
        if (nodeIsKind(arg.node, runtime::GCNodeKind::Dref)) {
            return nullptr; // a name without a function value among the arguments
        }
        boundTypes.push_back(valueTypeOf(nodeTypeOf(arg.node)));
    }
    // The one function the callee stands for with these `with` arguments (and, when the
    // position says which function it wants, with the parameters that are left).
    const auto fits = [&](FunctionType *type) {
        if (type == nullptr || !type->hasExitType() || type->withTypesCount() < bound.size()) {
            return false;
        }
        for (size_t i = 0; i < bound.size(); ++i) {
            if (!entersImplicitly(type->withTypeAt(i), boundTypes[i])) {
                return false;
            }
        }
        if (expected == nullptr) {
            return true;
        }
        if (type->withTypesCount() - bound.size() != expected->withTypesCount() ||
            type->normTypesCount() != expected->normTypesCount()) {
            return false;
        }
        for (size_t i = 0; i < expected->withTypesCount(); ++i) {
            if (!type->withTypeAt(bound.size() + i)->equals(expected->withTypeAt(i))) {
                return false;
            }
        }
        for (size_t i = 0; i < expected->normTypesCount(); ++i) {
            if (!type->normTypeAt(i)->equals(expected->normTypeAt(i))) {
                return false;
            }
        }
        return true;
    };
    FunctionType *calleeType = nullptr;
    if (nodeIsKind(target, runtime::GCNodeKind::Dref)) {
        const auto &drefTarget = nodeGraphOf(target)->drefTarget(target);
        size_t candidates      = 0;
        if (std::holds_alternative<graph_vec_ptr_t>(drefTarget)) {
            if (auto graphs = asCompileGraphVec(drefTarget)) {
                for (const auto &g : *graphs) {
                    if (!decoratedOf_.contains(g.get()) && fits(g->funcType())) {
                        calleeType = g->funcType();
                        ++candidates;
                    }
                }
            }
        } else if (std::holds_alternative<oper_group_ptr_t>(drefTarget)) {
            // An operator whose signature does not depend on its arguments. One whose type
            // is computed from the argument types has no parameter types before it is
            // called, so its binding cannot stand alone.
            for (const auto &[uri, resolver] :
                 std::get<oper_group_ptr_t>(drefTarget)->resolvers()) {
                FunctionType *fixed = resolver->fixedSignature();
                if (fixed && fits(fixed)) {
                    calleeType = fixed;
                    ++candidates;
                }
            }
        }
        if (candidates != 1) {
            return nullptr;
        }
    } else if (
        Type *type = valueTypeOf(nodeTypeOf(target)); type != nullptr &&
                                                      type->code() == TypeCode::Function &&
                                                      fits(tt::as_ptr<FunctionType>(type))) {
        calleeType = tt::as_ptr<FunctionType>(type);
    } else {
        return nullptr;
    }
    for (size_t i = 0; i < calleeType->withTypesCount(); ++i) {
        if (calleeType->withIsVarAt(i)) {
            // DESIGN-QUESTION(partial-application-var): a `with` parameter declared `var`
            // is a variable the function writes. Binding one ahead of the call would make
            // the function value a writer of a captured variable; whether that is meant has
            // not been decided, so it is refused rather than guessed.
            diags_->of(SemanticDiag::ConstructNotSupported)
                .atOrigin(origin->origin())
                .commit(
                    "f<w> as a value",
                    "a partial application is implemented for functions without 'var' "
                    "context parameters; this one has '" +
                        calleeType->toString() + "'. Call it with its arguments in place");
            throw BuildAbortException();
        }
    }

    // The bound values and, unless it is named, the callee: bound to names no program can
    // write, in the scope the expression stands in, so that the new graph captures them the
    // way any closure captures a name. Nothing refers to a node of another graph.
    std::vector<std::string> boundNames;
    for (const Argument &arg : bound) {
        boundNames.push_back(bindTemporary(valueOf(arg.node)));
    }
    const auto make = [&](GCT::node_ptr_t node) {
        node->setOrigin(origin->origin());
        return node;
    };
    const auto nameNode    = [&](const std::string &name) { return make(tree_->makeDRef(name)); };
    GCT::node_ptr_t callee = nullptr;
    if (nodeIsKind(target, runtime::GCNodeKind::Dref)) {
        const auto known = boundWithCallee_.find(target);
        ASSERT(known != boundWithCallee_.end(), "A bound callee has its expression recorded.");
        callee = tree_->clone(known->second);
    } else {
        callee = nameNode(bindTemporary(target));
    }
    boundWithArgs_.erase(target);
    boundWithCallee_.erase(target);

    // The function: the `with` parameters that are left, then the ordinary parameters.
    FunctionTypeFactory factory;
    factory.setModifiers(calleeType->modifiers());
    factory.setExitType(calleeType->exitType());
    const size_t withCount = calleeType->withTypesCount();
    const auto nameAt      = [&](size_t argIndex, const char *prefix, size_t i) {
        return argIndex < calleeType->argNamesCount() && !calleeType->argNameAt(argIndex).empty()
                   ? std::string(calleeType->argNameAt(argIndex))
                   : std::format("{}{}", prefix, i);
    };
    for (size_t i = bound.size(); i < withCount; ++i) {
        factory.addWithArg(nameAt(i, "__w", i), calleeType->withTypeAt(i), false);
    }
    for (size_t i = 0; i < calleeType->normTypesCount(); ++i) {
        factory.addNormArg(
            nameAt(withCount + i, "__n", i),
            calleeType->normTypeAt(i),
            calleeType->normIsVarAt(i));
    }
    FunctionType *type = factory.build();

    compile_graph_ptr_t graph     = enterScope(type);
    const bool outerSynced        = synced_;
    const node_handle_t outerLast = lastSyncedNode_;
    const auto restore            = [&] {
        synced_                  = outerSynced;
        lastSyncedNode_          = outerLast;
        bodyStates_[graph.get()] = BodyState::Built;
    };
    try {
        bodyStates_[graph.get()] = BodyState::Open;
        // Its body is the one call `f<bound..., rest...>(args...)`, built as any call is.
        GCT::node_ptr_t with = make(tree_->makeWith(withCount));
        *with << callee;
        for (const std::string &name : boundNames) {
            *with << nameNode(name);
        }
        size_t index = 0;
        for (draft_node_ref_t port : graph->draft().withPorts()) {
            const std::string name = "$with" + std::to_string(index++);
            nodeScope_->insert(name, graph->draft().node(port));
            *with << nameNode(name);
        }
        GCT::node_ptr_t call = make(tree_->makeLink(calleeType->normTypesCount()));
        *call << with;
        index = 0;
        for (draft_node_ref_t port : graph->draft().normPorts()) {
            const std::string name = "$norm" + std::to_string(index++);
            nodeScope_->insert(name, graph->draft().node(port));
            *call << nameNode(name);
        }
        // The call is the value of the function, so it runs whenever the function is called.
        synced_             = true;
        lastSyncedNode_     = {};
        node_handle_t value = visitLinkNode(call);
        setGraphReturnFromResult(graph, nodeIdOf(value), nodeIdOf(value), nodeTypeOf(value));
        restore();
        unit_->builtBodies.push_back(graph);
    } catch (...) {
        restore();
        leaveScope();
        throw;
    }
    leaveScope();
    return createFuncDataNode(graph, true, false);
}

node_handle_t Builder::compoundAssignmentByRebinding(
    const std::string &group, const GCT::node_ptr_t &gct, const std::vector<Argument> &operands) {
    // The plain operator a compound assignment follows: the table of operator_names.cpp.
    const auto plainGroup = camel::core::plainGroupOfCompoundAssignment(group);
    if (!plainGroup || operands.size() != 2) {
        return nullptr;
    }
    const std::string plainName(*plainGroup);
    // Only for a type a module defines. The built-in types have their own operators, and
    // where one is missing (`%=` on a float, `+=` on an array) that stays an error.
    // Or for any type when the program overloads the plain operator: `m &= n` on a type of
    // the program is `m = m & n` through its `operator &` (design principles 4.2, 4.3).
    Type *target = valueTypeOf(nodeTypeOf(operands[0].node));
    if (target == nullptr || (!target->isOtherType() && !hasProgramOperatorOverloads(plainName))) {
        return nullptr;
    }
    const auto make = [&](GCT::node_ptr_t node) {
        node->setOrigin(gct->origin());
        return node;
    };
    // The right operand is evaluated once: it was built already, and is named here.
    const std::string right = bindTemporary(valueOf(operands[1].node));
    GCT::node_ptr_t value   = make(tree_->makeLink(2));
    *value << make(tree_->makeDRef(plainName)) << tree_->clone(operands[0].expr)
           << make(tree_->makeDRef(right));
    GCT::node_ptr_t assign = make(tree_->makeLink(2));
    *assign << make(tree_->makeDRef("__assn__")) << tree_->clone(operands[0].expr) << value;
    return visitLinkNode(assign);
}

node_handle_t Builder::rebindingAppendOf(const GCT::node_ptr_t &gct) {
    // The lowered form of `acc = acc->append<v>` (and of `acc = append<v>(acc)`):
    //   LINK(DREF __assn__, DREF acc, LINK(WITH(DREF append, v), DREF acc))
    const auto isDref = [](const GCT::node_ptr_t &node, std::string_view name = {}) {
        return node != nullptr && node->type() == GCT::NodeKind::DREF &&
               (name.empty() || node->name() == name);
    };
    if (gct->size() != 3 || !isDref(gct->at(0), "__assn__") || !isDref(gct->at(1))) {
        return nullptr;
    }
    const GCT::node_ptr_t target = gct->at(1);
    const GCT::node_ptr_t call   = gct->at(2);
    if (call->type() != GCT::NodeKind::LINK || call->size() != 2 ||
        !isDref(call->at(1), target->name())) {
        return nullptr;
    }
    const GCT::node_ptr_t binding = call->at(0);
    if (binding->type() != GCT::NodeKind::WITH || binding->size() != 2 ||
        !isDref(binding->at(0), "append")) {
        return nullptr;
    }
    // `acc` is a variable (one of this function or a captured one).
    const std::string name = target->name();
    if (!nodeAt(name).has_value() || !isVariable(resolveNodeByRef(name))) {
        return nullptr;
    }
    // `append` is the built-in one: no declaration of the program, and no other module,
    // gives the name another meaning here (the lookup order of visitDRefNode).
    const oper_group_ptr_t appendGroup = importedOperatorGroupNamed("append");
    if (appendGroup == nullptr ||
        std::ranges::none_of(appendGroup->resolvers(), [](const auto &overload) {
            return overload.first == ":struct/append_arr";
        })) {
        return nullptr;
    }
    // A value that is still a name of a function, a binding or a lambda is settled by the
    // call it is passed to (visitLinkNode); it is left to the call as written.
    const GCT::node_ptr_t valueExpr = binding->at(1);
    if ((isDref(valueExpr) && !nodeAt(valueExpr->name()).has_value()) ||
        valueExpr->type() == GCT::NodeKind::WITH || valueExpr->type() == GCT::NodeKind::FUNC) {
        return nullptr;
    }

    // The value first, as in the call as written (the with-argument is built before the
    // operand); it is named so that the call below refers to it.
    node_handle_t value = valueOf(visitOperand(valueExpr, binding));
    if (nodeIsKind(value, runtime::GCNodeKind::Dref) || boundWithArgs_.contains(value)) {
        rejectNameWithoutValue(value, "the value", valueExpr);
    }
    const std::string temporary = bindTemporary(value);
    const auto make             = [&](GCT::node_ptr_t node, const GCT::node_ptr_t &at) {
        node->setOrigin(at->origin());
        return node;
    };
    if (recorder_) {
        std::vector<std::string> signatures;
        for (const auto &[uri, resolver] : appendGroup->resolvers()) {
            signatures.push_back(resolver->signature());
        }
        recorder_->operatorReferenced(binding->at(0)->origin(), "append", std::move(signatures));
    }

    // The operator that grows the array in place takes exactly the types `append` takes
    // for an array variable; any other combination is built as written, so that `append`
    // reports what does not fit.
    const auto group = context_->importModule("")->getExportedEntity(Reference("__assn_append__"));
    ASSERT(group.has_value() && group->isOperGroup(), "The built-in __assn_append__ is missing.");
    const std::vector<StaticArg> statics(2);
    const bool fits =
        group->operGroup()
            ->resolve(
                {},
                {valueTypeOf(nodeTypeOf(resolveNodeByRef(name))), valueTypeOf(nodeTypeOf(value))},
                statics,
                Modifier::None)
            .has_value();
    GCT::node_ptr_t rebinding = make(tree_->makeLink(2), gct);
    if (fits) {
        *rebinding << make(
                          tree_->makeDRef(std::string(kBuiltinPrefix) + "__assn_append__"),
                          gct->at(0))
                   << tree_->clone(target) << make(tree_->makeDRef(temporary), valueExpr);
    } else {
        GCT::node_ptr_t with = make(tree_->makeWith(1), binding);
        *with << tree_->clone(binding->at(0)) << make(tree_->makeDRef(temporary), valueExpr);
        GCT::node_ptr_t appended = make(tree_->makeLink(1), call);
        *appended << with << tree_->clone(call->at(1));
        *rebinding << tree_->clone(gct->at(0)) << tree_->clone(target) << appended;
    }
    return visitLinkNode(rebinding);
}

oper_group_ptr_t Builder::importedOperatorGroupNamed(const std::string &name) {
    if (nodeAt(name).has_value() || templateScope_->get(name).has_value() ||
        decoratedGraphAt(name).has_value()) {
        return nullptr;
    }
    if (auto graphs = graphsAt(name); graphs.has_value() && !graphs.value()->empty()) {
        return nullptr;
    }
    const auto entity = module_->getImportedEntity(name);
    return entity.has_value() && entity->isOperGroup() ? entity->operGroup() : nullptr;
}

node_handle_t Builder::inPlaceRebindingOf(const GCT::node_ptr_t &gct) {
    // The lowered form of `xs = xs->op<w...>` (and of `xs = op<w...>(xs)`, `xs = xs->op`):
    //   LINK(DREF __assn__, DREF xs, LINK(WITH(DREF op, w...) | DREF op, DREF xs))
    // In a sync block only: the rewritten statement is three operations on the variable,
    // which must run together and in this order.
    const auto isDref = [](const GCT::node_ptr_t &node, std::string_view name = {}) {
        return node != nullptr && node->type() == GCT::NodeKind::DREF &&
               (name.empty() || node->name() == name);
    };
    if (!synced_ || gct->size() != 3 || !isDref(gct->at(0), "__assn__") || !isDref(gct->at(1))) {
        return nullptr;
    }
    const GCT::node_ptr_t target = gct->at(1);
    const GCT::node_ptr_t call   = gct->at(2);
    if (call->type() != GCT::NodeKind::LINK || call->size() != 2 ||
        !isDref(call->at(1), target->name())) {
        return nullptr;
    }
    const GCT::node_ptr_t callee = call->at(0);
    const bool withArgs          = callee->type() == GCT::NodeKind::WITH;
    const GCT::node_ptr_t opName = withArgs ? callee->at(0) : callee;
    if (!isDref(opName)) {
        return nullptr;
    }
    // `xs` is a variable (of this function or a captured one) holding an array.
    const std::string name = target->name();
    if (!nodeAt(name).has_value()) {
        return nullptr;
    }
    const node_handle_t variable = resolveNodeByRef(name);
    Type *arrayType              = valueTypeOf(nodeTypeOf(variable));
    if (!isVariable(variable) || arrayType == nullptr || arrayType->code() != TypeCode::Array) {
        return nullptr;
    }
    // `op` is an operator of a module (nothing of the program is named so), and one of its
    // overloads declares an in-place form: anything else is an ordinary call.
    const oper_group_ptr_t group = importedOperatorGroupNamed(opName->name());
    auto &table                  = camel::core::OperatorTable::instance();
    if (group == nullptr || std::ranges::none_of(group->resolvers(), [&](const auto &overload) {
            return table.inPlaceForm(overload.first).has_value();
        })) {
        return nullptr;
    }
    // The with-arguments are plain values, built first as in the call as written and named
    // so that the call below refers to them; a name of a function, a binding or a lambda is
    // settled by the call it is passed to, and such a call is built as written.
    for (size_t i = 1; withArgs && i < callee->size(); ++i) {
        const GCT::node_ptr_t arg = callee->at(i);
        if ((isDref(arg) && !nodeAt(arg->name()).has_value()) ||
            arg->type() == GCT::NodeKind::WITH || arg->type() == GCT::NodeKind::FUNC) {
            return nullptr;
        }
    }
    std::vector<std::string> temporaries;
    type_vec_t withTypes;
    for (size_t i = 1; withArgs && i < callee->size(); ++i) {
        node_handle_t value = valueOf(visitOperand(callee->at(i), callee));
        if (nodeIsKind(value, runtime::GCNodeKind::Dref) || boundWithArgs_.contains(value)) {
            rejectNameWithoutValue(value, "the argument", callee->at(i));
        }
        withTypes.push_back(valueTypeOf(nodeTypeOf(value)));
        temporaries.push_back(bindTemporary(value));
    }
    const auto make = [&](GCT::node_ptr_t node, const GCT::node_ptr_t &at) {
        node->setOrigin(at->origin());
        return node;
    };
    // The callee as written, with the with-arguments replaced by their names.
    GCT::node_ptr_t calleeCopy = tree_->clone(opName);
    if (withArgs) {
        GCT::node_ptr_t with = make(tree_->makeWith(temporaries.size()), callee);
        *with << calleeCopy;
        for (size_t i = 0; i < temporaries.size(); ++i) {
            *with << make(tree_->makeDRef(temporaries[i]), callee->at(i + 1));
        }
        calleeCopy = with;
    }
    // The overload the call resolves to, as visitLinkNode will resolve it: rewritten only when
    // it has an in-place form and gives an array of the variable's own type.
    const std::vector<StaticArg> statics(1);
    const auto resolved       = group->resolve(withTypes, {arrayType}, statics, Modifier::None);
    const bool inPlace        = resolved.has_value() &&
                                table.inPlaceForm((*resolved)->uri()).has_value() &&
                                (*resolved)->funcType()->exitType() != nullptr &&
                                (*resolved)->funcType()->exitType()->equals(arrayType);
    GCT::node_ptr_t rebinding = make(tree_->makeLink(2), gct);
    GCT::node_ptr_t applied   = make(tree_->makeLink(1), call);
    if (!inPlace) {
        // As written (with the arguments already built): `op` reports what does not fit.
        *applied << calleeCopy << tree_->clone(call->at(1));
        *rebinding << tree_->clone(gct->at(0)) << tree_->clone(target) << applied;
        return visitLinkNode(rebinding);
    }
    GCT::node_ptr_t take = make(tree_->makeLink(1), call->at(1));
    *take << make(tree_->makeDRef(std::string(kBuiltinPrefix) + "__var_take__"), call->at(1))
          << tree_->clone(target);
    *applied << calleeCopy << take;
    inPlaceCalls_.insert(applied);
    *rebinding << make(tree_->makeDRef(std::string(kBuiltinPrefix) + "__assn_owned__"), gct->at(0))
               << tree_->clone(target) << applied;
    return visitLinkNode(rebinding);
}

void Builder::markNonRetainingReads() {
    // A read `r` of a variable becomes `:var/peek`, which leaves the variable's sole-reference
    // mark set (Tuple::holdsUnsharedArray), when the array it reads can be referred to by
    // nothing but the variable once the users of `r` have run, and they all run before the
    // variable can be written again. Then the next `acc = acc->append<v>` may still change the
    // array in place: nobody can see the difference. The conditions, every one required:
    //
    //  1. `r` was made in a sync block, so the sync chain orders it (syncedReads_).
    //  2. `r` is not the value of its graph (returned, the exit): that value leaves.
    //  3. Every user of `r` is an operator node in the sync chain, declared pure (no state, so
    //     the result is all it leaves behind), whose result type holds no reference to an
    //     array: a number, a bool, a string or nothing (`len`, `contains`, `==`, `<`, `[]`
    //     of an array of numbers, `format`). A call, a branch, a closure, a literal holding
    //     the value, a `let` used later by such a node: any of them keeps `r` a `:var/get`.
    //  4. No node made between `r` and its last user may write the variable: no operator
    //     that is neither pure nor state-only (assignments, `:op/assn_append`, effects), no
    //     call and no branch (their bodies may write it through a capture). The chain runs
    //     the nodes in the order they were made, so the users of `r` run before any later
    //     writer.
    //  5. The graph has no writer outside the chain (made in a nested block that is not a
    //     sync block), which could run between `r` and its users.
    //
    // Anything not covered keeps the read a `:var/get`, which clears the mark: at worst the
    // next append copies, as it would without this. The decision is made here, once the
    // graph is complete; the passes treat both reads alike (oper::isVariableRead).
    auto &table              = camel::core::OperatorTable::instance();
    const auto referenceFree = [](Type *type) {
        return type != nullptr && (type->code() == TypeCode::String || !isGCTraced(type->code()));
    };
    const auto mayWrite = [&](node_handle_t node) {
        switch (nodeKindOf(node)) {
        case runtime::GCNodeKind::Oper: {
            const auto op     = nodeGraphOf(node)->operTarget(node);
            const auto traits = table.find(op->uri());
            return !traits.has_value() || (!traits->pure && !traits->stateOnly);
        }
        case runtime::GCNodeKind::Call:
        case runtime::GCNodeKind::Func:
        case runtime::GCNodeKind::Brch:
            return true;
        default:
            return false;
        }
    };
    // Condition 5, per graph.
    std::unordered_map<runtime::GraphDraft *, bool> unchainedWriter;
    const auto hasUnchainedWriter = [&](runtime::GraphDraft *draft) {
        if (auto it = unchainedWriter.find(draft); it != unchainedWriter.end()) {
            return it->second;
        }
        bool found = false;
        for (draft_node_ref_t id = 0; !found && id < draft->nodeSlotCount(); ++id) {
            if (draft->alive(id)) {
                node_handle_t node = draft->node(id);
                found              = mayWrite(node) && !chainedNodes_.contains(node);
            }
        }
        return unchainedWriter[draft] = found;
    };
    const auto peekGroup = context_->importModule("")->getExportedEntity(Reference("__var_peek__"));
    ASSERT(
        peekGroup.has_value() && peekGroup->isOperGroup(),
        "The built-in __var_peek__ is missing.");
    const std::vector<StaticArg> statics(1);

    for (node_handle_t read : syncedReads_) {
        runtime::GraphDraft *draft = nodeDraftOf(read);
        const draft_node_ref_t id  = nodeIdOf(read);
        if (!draft->alive(id) || id == draft->exitNode() || id == draft->outputNode() ||
            id == draft->returnNode()) {
            continue; // condition 2
        }
        draft_node_ref_t lastUser = id;
        bool keeps                = false;
        size_t users              = 0;
        for (auto inputs : {normUsersOf(read), withUsersOf(read)}) {
            for (draft_node_ref_t userId : inputs) {
                node_handle_t user = draft->node(userId);
                ++users;
                lastUser = std::max(lastUser, userId);
                if (!nodeIsKind(user, runtime::GCNodeKind::Oper) || !chainedNodes_.contains(user) ||
                    !table.isPure(nodeGraphOf(user)->operTarget(user)->uri()) ||
                    !referenceFree(nodeTypeOf(user))) {
                    keeps = true; // condition 3
                }
            }
        }
        if (keeps || users == 0) {
            continue;
        }
        for (draft_node_ref_t between = id + 1; !keeps && between < lastUser; ++between) {
            keeps = draft->alive(between) && mayWrite(draft->node(between)); // condition 4
        }
        if (keeps || hasUnchainedWriter(draft)) {
            continue;
        }
        node_handle_t cell = draft->node(normInputsOf(read).front());
        const auto peek =
            peekGroup->operGroup()->resolve({}, {nodeTypeOf(cell)}, statics, Modifier::None);
        ASSERT(peek.has_value(), "__var_peek__ rejected a variable.");
        nodeGraphOf(read)->retargetOperNode(read, *peek);
    }
    syncedReads_.clear();
}

void Builder::rejectDiscardedValue(const DraftGraphBuilder *graph) {
    if (auto it = bodyValueStatement_.find(graph); it != bodyValueStatement_.end()) {
        rejectUnusedPureResult(it->second.first, it->second.second);
    }
}

void Builder::rejectUnusedPureResult(const GCT::node_ptr_t &stmt, node_handle_t node) {
    if (node == nullptr || !nodeIsKind(node, runtime::GCNodeKind::Oper) ||
        discardedValues_.contains(node)) {
        return; // not an operator, or dropped on purpose (`let _`), or reported already
    }
    const auto op = nodeGraphOf(node)->operTarget(node);
    if (op == nullptr || !camel::core::OperatorTable::instance().isPure(op->uri())) {
        return;
    }
    // The call as written: `wait` around it changes nothing about its value.
    GCT::node_ptr_t call = stmt;
    if (call->type() == GCT::NodeKind::WAIT && call->size() == 1) {
        call = call->at(0);
    }
    if (call->type() != GCT::NodeKind::LINK || call->size() < 1) {
        return;
    }
    // The name the program wrote: the operator's symbol, or the function's name.
    GCT::node_ptr_t callee = call->at(0);
    while (callee->type() == GCT::NodeKind::WITH && callee->size() > 0) {
        callee = callee->at(0);
    }
    std::string written = callee->type() == GCT::NodeKind::DREF ? callee->name() : op->name();
    if (const char *symbol = operatorSymbolOf(written)) {
        written = symbol;
    }
    // When the operation is applied to a variable, the program most likely meant to change
    // it, as `xs->append<5>` did before 11.3: the fix is to bind the variable to the result.
    std::string fix;
    const std::string text = sourceTextOf(call);
    if (call->size() >= 2 && call->at(1)->type() == GCT::NodeKind::DREF &&
        nodeAt(call->at(1)->name()).has_value() &&
        isVariable(resolveNodeByRef(call->at(1)->name()))) {
        const std::string variable = call->at(1)->name();
        fix = text.empty() ? std::format(
                                 "; to change the variable '{0}', bind it to the result: "
                                 "'{0} = ...'",
                                 variable)
                           : std::format(
                                 "; to change the variable '{0}', bind it to the result: "
                                 "'{0} = {1}'",
                                 variable,
                                 text);
    } else {
        fix = "; use the result ('let ys = ...') or drop it on purpose with 'let _ = ...'";
    }
    diags_->of(SemanticDiag::UnusedPureResult).atOrigin(stmt->origin()).commit(written, fix);
    // Reported here; it is not also an unused `T!` (reportUnusedFallibleValues).
    discardedValues_.insert(node);
}

void Builder::rejectLiteralConversion(
    const std::string &group, FunctionType *funcType, const std::vector<Argument> &operands,
    const GCT::node_ptr_t &call) {
    static const std::unordered_set<std::string> kConversions =
        {"int", "i64", "i32", "float", "f64", "f32"};
    if (!kConversions.contains(group) || operands.size() != 1 || funcType == nullptr ||
        !funcType->hasExitType() || operands[0].expr->type() != GCT::NodeKind::DATA) {
        return;
    }
    // `int(1.0e30)`, `i32(5000000000)`, `int('12abc')`: the argument is written in the
    // program and the conversion is known not to fit, so it is refused here as a literal
    // given a type it cannot have is (`let b: i32 = 3000000000`). A computed argument stays
    // a fault when the program runs.
    const data_ptr_t data = operands[0].expr->data();
    Type *target          = funcType->exitType();
    if (data == nullptr) {
        return;
    }
    const bool text = data->type()->code() == TypeCode::String;
    if (!text && !data->type()->isPrimitive()) {
        return;
    }
    try {
        if (text) {
            // The rule of the conversion itself (camel/core/numeric_convert.h).
            const std::string &written = std::dynamic_pointer_cast<StringData>(data)->data();
            switch (target->code()) {
            case TypeCode::Int32:
                (void)camel::core::integerFromText<int32_t>(written, "i32");
                break;
            case TypeCode::Int64:
                (void)camel::core::integerFromText<int64_t>(written, "int");
                break;
            case TypeCode::Float32:
                (void)camel::core::floatFromText<float>(written, "f32");
                break;
            case TypeCode::Float64:
                (void)camel::core::floatFromText<double>(written, "float");
                break;
            default:
                break;
            }
        } else if (const auto value = staticArgOf(operands[0].node).value) {
            (void)target->castSlotFrom(*value, data->type());
        }
        return;
    } catch (const camel::core::error::RuntimeFault &) {
    }
    if (text) {
        diags_->of(SemanticDiag::LiteralStaticCastFailed)
            .atOrigin(call->origin())
            .commit(data->toString(), target->toString());
    } else {
        std::string limits = literalRangeViolation(data, target);
        if (limits.empty()) {
            limits = target->code() == TypeCode::Int32   ? "-2147483648 to 2147483647"
                     : target->code() == TypeCode::Int64 ? "-9223372036854775808 to "
                                                           "9223372036854775807"
                                                         : "the value does not fit the type";
        }
        diags_->of(SemanticDiag::LiteralOutOfRange)
            .atOrigin(call->origin())
            .commit(literalValueText(data), target->toString(), limits);
    }
    throw BuildAbortException();
}

void Builder::rejectUnappliedBinding(node_handle_t node, const GCT::node_ptr_t &origin) {
    const auto boundIt = boundWithArgs_.find(node);
    if (boundIt == boundWithArgs_.end()) {
        return;
    }
    const std::string given = strutil::join(boundIt->second, ", ", [&](const Argument &arg) {
        return valueTypeOf(nodeTypeOf(arg.node))->toString();
    });
    std::string name        = "the function";
    if (const auto callee = boundWithCallee_.find(node);
        callee != boundWithCallee_.end() && callee->second->type() == GCT::NodeKind::DREF) {
        name = callee->second->name();
    }
    std::string why;
    if (genericDrefs_.contains(node)) {
        why = "it is a generic function, whose type variables are fixed by a call";
    } else if (nodeIsKind(node, runtime::GCNodeKind::Dref)) {
        const auto &target = nodeGraphOf(node)->drefTarget(node);
        if (std::holds_alternative<graph_vec_ptr_t>(target)) {
            auto graphs = asCompileGraphVec(target);
            why         = std::format(
                "{} function(s) of that name, and not exactly one takes them [{}]; where "
                "several do, a declared type or a parameter must say which is meant",
                graphs ? graphs->size() : 0,
                graphs ? strutil::join(
                             *graphs,
                             "; ",
                             [](const graph_ptr_t &g) { return g->funcType()->toString(); })
                       : std::string());
        } else if (std::holds_alternative<oper_group_ptr_t>(target)) {
            const auto &ops = std::get<oper_group_ptr_t>(target);
            why             = std::format(
                "it is an operator whose signature depends on its arguments [{}], so it has "
                "no parameter types before it is called; write the call in a function, e.g. "
                "'(x: int) => {}<...>(x)'",
                strutil::join(
                    ops->resolvers(),
                    "; ",
                    [](const std::pair<std::string, resolver_ptr_t> &p) {
                        return p.second->signature();
                    }),
                name);
        } else {
            why = "it is a decorated function";
        }
    } else {
        why = "its type is '" + valueTypeOf(nodeTypeOf(node))->toString() + "'";
    }
    boundWithArgs_.erase(node);
    boundWithCallee_.erase(node);
    diags_->of(SemanticDiag::PartialApplicationRefused)
        .atOrigin(origin->origin())
        .commit(given, name, why);
    throw BuildAbortException();
}

compile_graph_ptr_t Builder::forwardingGraphOf(const compile_graph_ptr_t &target) {
    FunctionType *targetType = target->funcType();
    // Its own type object (a graph completes its function type in place): the declared
    // parameters and result of the target, and no captures yet.
    FunctionTypeFactory factory;
    factory.setModifiers(targetType->modifiers());
    factory.setExitType(targetType->exitType());
    const auto nameAt = [&](size_t argIndex, const char *prefix, size_t i) {
        return argIndex < targetType->argNamesCount() ? std::string(targetType->argNameAt(argIndex))
                                                      : std::format("{}{}", prefix, i);
    };
    for (size_t i = 0; i < targetType->withTypesCount(); ++i) {
        factory.addWithArg(
            nameAt(i, "__w", i),
            targetType->withTypeAt(i),
            targetType->withIsVarAt(i));
    }
    for (size_t i = 0; i < targetType->normTypesCount(); ++i) {
        factory.addNormArg(
            nameAt(targetType->withTypesCount() + i, "__n", i),
            targetType->normTypeAt(i),
            targetType->normIsVarAt(i));
    }
    FunctionType *type = factory.build();

    compile_graph_ptr_t graph = enterScope(type);
    try {
        auto sourceContext = context_ ? context_->sourceContext() : nullptr;
        auto targetOrigin  = sourceContext
                                 ? sourceContext->debugMap().graphOrigin(target->stableId())
                                 : camel::source::kInvalidOriginId;
        if (sourceContext && targetOrigin != camel::source::kInvalidOriginId) {
            sourceContext->debugMap().registerGraphOrigin(graph->stableId(), targetOrigin);
        }
        bodyStates_[graph.get()] = BodyState::Open;
        graph->addDependencyGraph(target);
        // The call the target's direct callers make: the arguments, then the target's
        // captures, which become captures of this graph (passed by the link step).
        node_handle_t call = graph->addFuncNode(target, 0);
        if (isMacroGraph(target)) {
            setNodeMacro(call, true);
        }
        for (draft_node_ref_t port : graph->draft().withPorts()) {
            linkNodes(LinkType::With, graph->draft().node(port), call);
        }
        for (draft_node_ref_t port : graph->draft().normPorts()) {
            linkNodes(LinkType::Norm, graph->draft().node(port), call);
        }
        addCaptureSite(call, target);
        bindGraphScopedFuncNodeDebug(sourceContext, targetOrigin, target, call);
        // The summaries see the target as a function this graph calls: what the target
        // writes of the captured variables, this graph writes.
        (void)capturedUsesOf(target);
        setGraphReturnFromResult(graph, nodeIdOf(call), nodeIdOf(call), targetType->exitType());
        bodyStates_[graph.get()] = BodyState::Built;
        unit_->builtBodies.push_back(graph);
    } catch (...) {
        bodyStates_[graph.get()] = BodyState::Built;
        leaveScope();
        throw;
    }
    leaveScope();
    return graph;
}

node_handle_t
Builder::applyDecoratorAnno(const GCT::node_ptr_t &annoNode, node_handle_t funcValueNode) {
    ASSERT(annoNode && annoNode->type() == GCT::NodeKind::ANNO, "Expected ANNO node.");
    ASSERT(annoNode->size() == 1, "ANNO node should have exactly one lowered expression child.");
    ASSERT(funcValueNode != nullptr, "Decorator input function value is null.");

    // `@name` or `@name<args>`: either way the expression is the decorator to call.
    const auto &decorator = annoNode->at(0);

    const std::string tmpScopeName = "__decorator_tmp_scope_" + std::to_string(syntheticRefIndex_);
    const std::string tmpRefName   = "__decorator_tmp_ref_" + std::to_string(syntheticRefIndex_++);
    nodeScope_                     = nodeScope_->enter(tmpScopeName);
    nodeScope_->insert(tmpRefName, funcValueNode);

    GCT::node_ptr_t linkNode = tree_->makeLink(1);
    *linkNode << tree_->clone(decorator);
    *linkNode << tree_->makeDRef(tmpRefName);
    // A call of the decorator is reported at the decorator.
    linkNode->setOrigin(decorator->origin());
    linkNode->at(1)->setOrigin(decorator->origin());

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
    // The factory is a separate graph, not a statement in the enclosing sync chain.
    // Otherwise applying a local decorator overwrites lastSyncedNode_ with a node
    // belonging to this factory, and the next enclosing call links across graphs.
    struct RestoreSync {
        bool &synced;
        node_handle_t &last;
        bool outerSynced;
        node_handle_t outerLast;
        ~RestoreSync() {
            synced = outerSynced;
            last   = outerLast;
        }
    } restoreSync{synced_, lastSyncedNode_, synced_, lastSyncedNode_};
    synced_                    = false;
    lastSyncedNode_            = {};
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
            .atOrigin(annoNodes.front()->origin())
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

// ---- Decorators that grant attributes, decorated blocks (language reference 7.6) ------------

std::optional<camel::core::attr::AttrSetId> Builder::staticGrantOf(const GCT::node_ptr_t &anno) {
    if (auto it = staticGrants_.find(anno); it != staticGrants_.end()) {
        return it->second;
    }
    // Asked first where the decorator is written (visitFuncNode before it enters the
    // function, registerGenericTemplates): the names of the decorator and of its arguments are
    // resolved there.
    const auto compute = [&]() -> std::optional<camel::core::attr::AttrSetId> {
        const GCT::node_ptr_t expr = anno->at(0);
        GCT::node_ptr_t callee     = expr;
        std::vector<GCT::node_ptr_t> args;
        if (expr->type() == GCT::NodeKind::WITH) {
            callee = expr->at(0);
            for (size_t i = 1; i < expr->size(); ++i) {
                args.push_back(expr->at(i));
            }
        }
        // A name the program binds itself (a value, a function, a generic function) is not
        // the operator: the decorator is applied as any other.
        const auto programName = [&](const std::string &written) {
            const std::string name = moduleLevelName(written);
            return nodeAt(name).has_value() || templateScope_->get(name).has_value() ||
                   graphsAt(name).has_value() || decoratedGraphAt(name).has_value() ||
                   (unit_ != nullptr && unit_->values.contains(name));
        };
        // The entity an imported name stands for (design principles 10.2, layers 3 to 5).
        const auto imported =
            [&](const GCT::node_ptr_t &ref) -> std::optional<camel::core::module::entity> {
            const std::string written = ref->name();
            if (programName(written) || isOperatorName(written)) {
                return std::nullopt;
            }
            if (module_->hasImportedRef(written, ImportLayer::Named)) {
                return module_->getImportedEntity(written, ImportLayer::Named);
            }
            if (auto match = throughUsing(written, ref)) {
                return match->ownName.empty() ? match->entity : std::nullopt;
            }
            if (module_->hasImportedRef(written, ImportLayer::Ambient)) {
                return module_->getImportedEntity(written, ImportLayer::Ambient);
            }
            return std::nullopt;
        };
        if (callee->type() != GCT::NodeKind::DREF) {
            return std::nullopt;
        }
        const auto entity = imported(callee);
        if (!entity.has_value() || !entity->isOperGroup() ||
            entity->operGroup()->resolvers().size() != 1) {
            return std::nullopt;
        }
        const auto grant = camel::core::OperatorTable::instance().grantOf(
            entity->operGroup()->resolvers().front().first);
        if (grant == nullptr) {
            return std::nullopt;
        }
        // The with-arguments must be constants: literals, or constants a module exports.
        std::vector<data_ptr_t> values;
        for (const GCT::node_ptr_t &arg : args) {
            if (arg->type() == GCT::NodeKind::DATA) {
                values.push_back(arg->data());
                continue;
            }
            if (arg->type() != GCT::NodeKind::DREF) {
                return std::nullopt;
            }
            const auto value = imported(arg);
            if (!value.has_value() || !value->isValue()) {
                return std::nullopt;
            }
            values.push_back(value->value());
        }
        const std::optional<camel::core::attr::AttrValueId> granted =
            grant->value ? grant->value(values)
                         : std::optional<camel::core::attr::AttrValueId>(camel::core::attr::unit());
        if (!granted.has_value()) {
            return std::nullopt;
        }
        return camel::core::attr::single(camel::core::attr::key(grant->key), *granted);
    };
    std::optional<camel::core::attr::AttrSetId> result;
    try {
        result = compute();
    } catch (const BuildAbortException &) {
        result = std::nullopt; // reported, if at all, when the decorator is applied
    }
    staticGrants_.emplace(anno, result);
    return result;
}

std::pair<camel::core::attr::AttrSetId, size_t>
Builder::staticGrantsOf(const GCT::node_ptr_t &func) {
    std::vector<GCT::node_ptr_t> annos;
    for (size_t i = 2; i < func->size(); ++i) {
        if (func->at(i) && func->at(i)->type() == GCT::NodeKind::ANNO) {
            annos.push_back(func->at(i));
        }
    }
    camel::core::attr::AttrSetId granted = camel::core::attr::kNoAttrs;
    size_t count                         = 0;
    // The decorator written last applies first: a run of granting ones from there is applied
    // to the function as written, before any other decorator sees it.
    for (auto it = annos.rbegin(); it != annos.rend(); ++it) {
        const auto grant = staticGrantOf(*it);
        if (!grant.has_value()) {
            break;
        }
        granted = camel::core::attr::unite(granted, *grant);
        ++count;
    }
    return {granted, count};
}

std::vector<GCT::node_ptr_t> Builder::appliedDecoratorsOf(const GCT::node_ptr_t &func) {
    std::vector<GCT::node_ptr_t> annos;
    for (size_t i = 2; i < func->size(); ++i) {
        if (func->at(i) && func->at(i)->type() == GCT::NodeKind::ANNO) {
            annos.push_back(func->at(i));
        }
    }
    annos.resize(annos.size() - staticGrantsOf(func).second);
    return annos;
}

node_handle_t Builder::visitDecoratedBlock(const GCT::node_ptr_t &link, bool unused) {
    ENTER("DECORATED_BLOCK");
    // `@d { ... }` is the block as a function of no parameters, passed to the decorators and
    // called right where it is written (language reference 7.6). The block is lowered as a
    // plain block would be (its scope, its exits, its value); the function's value is the
    // block's, and is dropped when nothing takes the block's value.
    const GCT::node_ptr_t func = link->at(0);
    const GCT::node_ptr_t body = func->at(1);
    staticGrantsOf(func); // resolved here, where the decorators are written
    const std::vector<GCT::node_ptr_t> decorators = appliedDecoratorsOf(func);
    if (decorators.empty()) {
        // Every decorator grants attributes as the program is built: what they make is the
        // block's own function with the attributes, called here, which is the block itself
        // with the attributes. Built in place, as the same block without decorators is.
        AttrScope attrs(
            currentAttrs_,
            camel::core::attr::unite(currentAttrs_, staticGrantsOf(func).first));
        unusedStatement_     = unused ? body : nullptr;
        node_handle_t result = visitExecNode(body);
        LEAVE("DECORATED_BLOCK");
        return result;
    }
    if (unused) {
        droppedBodies_.insert(body);
    }
    compile_graph_ptr_t graph = visitFuncNode(func);
    blockGraphs_.insert(graph.get());
    node_handle_t value = createFuncDataNode(graph, true, false);
    for (auto it = decorators.rbegin(); it != decorators.rend(); ++it) {
        value = applyDecoratorAnno(*it, value);
    }
    // The call, as if written here: `(decorated)()`.
    const std::string scopeName = "__block_tmp_scope_" + std::to_string(syntheticRefIndex_);
    const std::string refName   = "__block_tmp_ref_" + std::to_string(syntheticRefIndex_++);
    nodeScope_                  = nodeScope_->enter(scopeName);
    nodeScope_->insert(refName, value);
    GCT::node_ptr_t call = tree_->makeLink(0);
    GCT::node_ptr_t ref  = tree_->makeDRef(refName);
    ref->setOrigin(link->origin());
    call->setOrigin(link->origin());
    *call << ref;
    node_handle_t result{};
    try {
        result = visitLinkNode(call);
    } catch (...) {
        nodeScope_ = nodeScope_->leave();
        throw;
    }
    nodeScope_ = nodeScope_->leave();
    LEAVE("DECORATED_BLOCK");
    return result;
}

graph_set_ptr_t
Builder::overloadsOfDecorated(const std::string &name, const compile_graph_ptr_t &decorated) {
    auto graphs = graphsAt(name);
    if (!graphs.has_value() || graphs.value()->size() < 2) {
        return nullptr;
    }
    for (const auto &g : *graphs.value()) {
        auto it = decoratedOf_.find(g.get());
        if (it != decoratedOf_.end() && it->second == decorated) {
            return graphs.value();
        }
    }
    return nullptr;
}

node_handle_t Builder::lowerDecoratedGraphValue(
    const compile_graph_ptr_t &decoratedGraph, const GCT::node_ptr_t &origin) {
    currGraph_->addDependencyGraph(decoratedGraph);
    if (!decoratedGraph->funcType()->hasExitType() ||
        decoratedGraph->funcType()->exitType()->code() != TypeCode::Function) {
        diags_->of(SemanticDiag::ArgumentsMismatch)
            .atOrigin(origin->origin())
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
    return functionValueOf(any_cast<node_handle_t>(res), origin);
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

// The part of a "no matching function" message for a call that wrote every argument in the
// parentheses although `name` takes all but the first `normCount` in angle brackets: the
// standard library's convention is that the value worked on is an ordinary argument and what
// configures the operation a with-argument (design principles 11.4). Names the misplaced
// arguments and gives the call as it should be written.
std::string Builder::withArgumentsHint(
    const std::string &name, const std::vector<Argument> &arguments, size_t normCount) const {
    std::vector<std::string> texts;
    texts.reserve(arguments.size());
    for (size_t i = 0; i < arguments.size(); ++i) {
        std::string text = sourceTextOf(arguments[i].expr);
        texts.push_back(text.empty() ? std::format("arg{}", i + 1) : std::move(text));
    }
    const auto joined = [&](size_t from, size_t to) {
        std::string out;
        for (size_t i = from; i < to; ++i) {
            out += (i == from ? "" : ", ") + texts[i];
        }
        return out;
    };
    const std::string withs = joined(normCount, texts.size());
    const std::string norms = joined(0, normCount);
    std::string hint        = std::format(
        "; '{}' takes {} in angle brackets, not in the parentheses: write {}<{}>({})",
        name,
        "'" + withs + "'",
        name,
        withs,
        norms);
    if (normCount == 1) {
        hint += std::format(" or {}->{}<{}>", norms, name, withs);
    }
    return hint;
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
        const auto *record = sourceContext->origin(node->origin());
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
    stampPermissions(node);
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
        syncedReads_.push_back(value);
        chainedNodes_.insert(value);
    }
    return value;
}

node_handle_t Builder::visitOperand(const GCT::node_ptr_t &expr, const GCT::node_ptr_t &call) {
    if (expr->type() == GCT::NodeKind::DREF) {
        const std::string name = moduleLevelName(expr->name());
        refOrigin_             = expr->origin();
        if (nodeAt(name).has_value()) {
            if (node_handle_t node = resolveNodeByRef(name); isVariable(node)) {
                if (recorder_) {
                    recorder_->valueReferenced(
                        expr->origin(),
                        name,
                        declarationOf(name),
                        valueTypeOf(nodeTypeOf(node)));
                }
                return node;
            }
        }
    }
    return argumentNodeOf(visit(expr), call);
}

std::optional<std::string> Builder::immutableNameOf(const GCT::node_ptr_t &expr) {
    GCT::node_ptr_t root = expr;
    while (root && root->type() == GCT::NodeKind::ACCS) {
        root = root->at(0);
    }
    if (!root || root->type() != GCT::NodeKind::DREF) {
        return std::nullopt;
    }
    const std::string name = moduleLevelName(root->name());
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
            .atOrigin(arg.expr->origin())
            .commit(*name, callee);
        throw BuildAbortException();
    }
}

node_handle_t Builder::visitLinkNode(const GCT::node_ptr_t &gct) {
    ENTER("LINK");
    Type *expectedResult = expectedValueType_;
    ExpectedValueScope callScope(expectedValueType_, nullptr);
    // Whether this call is a statement whose value nothing takes; the calls inside it are not.
    const bool unusedStatement = gct == unusedStatement_;
    unusedStatement_           = nullptr;
    if (gct->at(0)->type() == GCT::NodeKind::FUNC && gct->at(0)->decoratedBlock()) {
        node_handle_t result = visitDecoratedBlock(gct, unusedStatement);
        LEAVE("LINK");
        return result;
    }
    if (unusedStatement) {
        if (node_handle_t rebound = rebindingAppendOf(gct)) {
            LEAVE("LINK");
            return rebound;
        }
        if (node_handle_t rebound = inPlaceRebindingOf(gct)) {
            LEAVE("LINK");
            return rebound;
        }
    }
    if (gct->size() >= 1 && gct->at(0)->type() == GCT::NodeKind::DREF &&
        gct->at(0)->name().find("::") != std::string::npos) {
        // `Shape::Circle(1.0)`: the variant is built from its fields, not called (design
        // principles 5.3). A qualified name that is no variant is resolved as it was.
        EnumType *enumType = nullptr;
        size_t tag         = 0;
        if (enumVariantOfName(gct->at(0)->name(), gct->at(0), enumType, tag)) {
            node_handle_t built = visitEnumConstruction(gct, enumType, tag);
            LEAVE("LINK");
            return built;
        }
    }
    genericCalleeAllowed_ = gct->at(0)->type() == GCT::NodeKind::DREF ||
                            gct->at(0)->type() == GCT::NodeKind::WITH ||
                            gct->at(0)->type() == GCT::NodeKind::TAPP;
    any targetNodeRes     = visit(gct->at(0));
    genericCalleeAllowed_ = false;
    // The callee may be a lambda written in place (`((x: int): int => x + 1)(3)`,
    // `v ?-> (x: int): int => x * 2`): visiting it gives its graph, and what is called is the
    // function value of that graph. (The graph was cast to a node: `bad any_cast`, bare.)
    node_handle_t targetNode = valueNodeOf(targetNodeRes);

    compile_graph_ptr_t targetGraph = nullptr;
    oper_idx_ptr_t targetOperator   = nullptr;
    FunctionType *targetFuncType    = nullptr;
    std::vector<Argument> withInputs, normInputs;
    type_vec_t withInputTypes, normInputTypes;

    if (auto bound = boundWithArgs_.find(targetNode); bound != boundWithArgs_.end()) {
        // Consumed by this call: a callee that is a value (`g<2>(x)` with `g` a variable) is
        // the same node at its next use, which must not find these arguments again.
        withInputs = std::move(bound->second);
        boundWithArgs_.erase(bound);
        boundWithCallee_.erase(targetNode);
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

    FunctionType *argumentSignature = signatureForArgumentContext(targetNode);
    for (size_t i = 1; i < gct->size(); i++) {
        Type *wanted = argumentSignature && i <= argumentSignature->normTypesCount()
                           ? argumentSignature->normTypeAt(i - 1)
                           : nullptr;
        ExpectedValueScope argumentScope(
            expectedValueType_,
            wanted && !containsTypeVar(wanted) ? wanted : nullptr);
        node_handle_t inputNode = visitOperand(gct->at(i), gct);
        normInputs.push_back({inputNode, gct->at(i)});
        normInputTypes.push_back(valueTypeOf(callableValueType(inputNode)));
    }
    if (nodeIsKind(targetNode, runtime::GCNodeKind::Dref)) {
        // An operator that declares it takes the position of its call at this many arguments
        // (CallSpec::callSiteAtArity; `error(kind, message)`: an error value records where it
        // was made, design principles 3.9) gets that position as one more, constant argument.
        const auto &drefTarget   = nodeGraphOf(targetNode)->drefTarget(targetNode);
        const auto takesCallSite = [&] {
            if (!std::holds_alternative<oper_group_ptr_t>(drefTarget)) {
                return false;
            }
            for (const auto &[uri, resolver] :
                 std::get<oper_group_ptr_t>(drefTarget)->resolvers()) {
                const auto *spec = camel::core::OperatorTable::instance().callSpec(uri);
                if (spec != nullptr && spec->callSiteAtArity >= 0 &&
                    static_cast<size_t>(spec->callSiteAtArity) + 1 == gct->size()) {
                    return true;
                }
            }
            return false;
        };
        if (takesCallSite()) {
            node_handle_t location = currGraph_->addStaticDataNode(
                tt::as_shared<Data>(
                    std::make_shared<StringData>(sourceLocationOf(gct->at(0)->origin()))));
            registerNodeOrigin(context_, location, gct, "gir.data");
            normInputs.push_back({location, gct});
            normInputTypes.push_back(Type::String());
        }
    }

    // An argument that is still a name without a single function value (several overloads, or
    // an operator whose signature is not one function type). Where the callee has one known
    // signature and its parameter there is a function type, the overload with exactly that
    // type is the argument. Anywhere else the name has no value: it used to be passed on as
    // a value of type void, which ended in "No matching function ... <void>" or, when the
    // callee takes anything (`println(println)`), in an internal error about a live DREF.
    {
        FunctionType *knownCalleeType = nullptr;
        if (!genericDrefs_.contains(targetNode)) {
            if (nodeIsKind(targetNode, runtime::GCNodeKind::Dref)) {
                const auto &drefTarget = nodeGraphOf(targetNode)->drefTarget(targetNode);
                if (std::holds_alternative<graph_vec_ptr_t>(drefTarget)) {
                    auto graphs = asCompileGraphVec(drefTarget);
                    if (graphs && graphs->size() == 1 &&
                        !decoratedOf_.contains(graphs->front().get())) {
                        knownCalleeType = graphs->front()->funcType();
                    }
                }
            } else if (
                Type *type = nodeTypeOf(targetNode);
                type != nullptr && type->code() == TypeCode::Function) {
                knownCalleeType = tt::as_ptr<FunctionType>(type);
            }
        }
        const auto nameOf = [](const GCT::node_ptr_t &expr) -> std::string {
            GCT::node_ptr_t at = expr;
            while (at && at->type() == GCT::NodeKind::WITH && at->size() > 0) {
                at = at->at(0);
            }
            return at && at->type() == GCT::NodeKind::DREF ? at->name() : std::string("the name");
        };
        const auto settle = [&](std::vector<Argument> &inputs, type_vec_t &types, bool with) {
            for (size_t i = 0; i < inputs.size(); ++i) {
                node_handle_t node = inputs[i].node;
                if (node == nullptr || !nodeIsKind(node, runtime::GCNodeKind::Dref)) {
                    continue;
                }
                if (genericDrefs_.contains(node)) {
                    // The name of a generic function (design principles 5.5). A callee
                    // with one known signature says which function it expects; a generic
                    // callee solves for it with its other arguments (resolveGenericCall);
                    // an operator tries the instances its other arguments suggest
                    // (settleGenericOperatorArguments). Any other callee says nothing.
                    const bool later = genericDrefs_.contains(targetNode) ||
                                       (nodeIsKind(targetNode, runtime::GCNodeKind::Dref) &&
                                        std::holds_alternative<oper_group_ptr_t>(
                                            nodeGraphOf(targetNode)->drefTarget(targetNode)));
                    if (later) {
                        continue;
                    }
                    Type *wanted = nullptr;
                    if (knownCalleeType != nullptr) {
                        const size_t count = with ? knownCalleeType->withTypesCount()
                                                  : knownCalleeType->normTypesCount();
                        if (i < count) {
                            wanted = with ? knownCalleeType->withTypeAt(i)
                                          : knownCalleeType->normTypeAt(i);
                        }
                    }
                    inputs[i].node = genericValueFor(node, wanted, inputs[i].expr);
                    types[i]       = valueTypeOf(nodeTypeOf(inputs[i].node));
                    continue;
                }
                Type *expected = nullptr;
                if (knownCalleeType != nullptr) {
                    const size_t count = with ? knownCalleeType->withTypesCount()
                                              : knownCalleeType->normTypesCount();
                    if (i < count) {
                        expected =
                            with ? knownCalleeType->withTypeAt(i) : knownCalleeType->normTypeAt(i);
                    }
                }
                node_handle_t chosen{};
                if (boundWithArgs_.contains(node)) {
                    // `apply(scale<2>, x)`: the partial application the parameter asks for.
                    chosen = partialApplicationOf(
                        node,
                        inputs[i].expr,
                        expected != nullptr && expected->code() == TypeCode::Function
                            ? tt::as_ptr<FunctionType>(expected)
                            : nullptr);
                }
                if (chosen == nullptr && expected != nullptr &&
                    expected->code() == TypeCode::Function) {
                    chosen =
                        overloadValueFor(node, tt::as_ptr<FunctionType>(expected), inputs[i].expr);
                }
                if (chosen == nullptr && !boundWithArgs_.contains(node) &&
                    nodeIsKind(targetNode, runtime::GCNodeKind::Dref) &&
                    !genericDrefs_.contains(targetNode) &&
                    std::holds_alternative<oper_group_ptr_t>(
                        nodeGraphOf(targetNode)->drefTarget(targetNode))) {
                    // A built-in operator cannot say which function it expects: it is asked
                    // about each function the name stands for
                    // (settleGenericOperatorArguments).
                    continue;
                }
                if (chosen == nullptr) {
                    rejectNameWithoutValue(node, nameOf(inputs[i].expr), inputs[i].expr);
                }
                inputs[i].node = chosen;
                types[i]       = valueTypeOf(nodeTypeOf(chosen));
            }
        };
        settle(withInputs, withInputTypes, true);
        settle(normInputs, normInputTypes, false);
    }

    // Design principles 3.8: an argument enters a `T?` / `T!` parameter by itself, `null`
    // too, and `[]` takes the element type of its parameter. A callee is first tried with the
    // argument types as they are, so that among overloads an exact match wins (`f(5)` with
    // `f(x: int)` and `f(x: int?)`); then with every argument that enters its parameter
    // counted as having the parameter's type. The CASTs are added once the callee is known
    // (enterParameters below).
    // DESIGN-QUESTION(entering-operator-args): operators are not tried this way. Their
    // parameter types are computed from the arguments by C++ resolvers, so there is no
    // target type before a resolver has accepted the arguments (`append(xs, 5)` with
    // `xs: (int?)[]` still needs `5 as int?`, and `len([])` keeps `[]` without an element
    // type). The alternative is to let a resolver name the type it would accept.
    const auto enteringTypes = [](FunctionType *funcType, const type_vec_t &types, bool with) {
        type_vec_t entered = types;
        const size_t count = with ? funcType->withTypesCount() : funcType->normTypesCount();
        for (size_t i = 0; i < entered.size() && i < count; ++i) {
            Type *param      = with ? funcType->withTypeAt(i) : funcType->normTypeAt(i);
            const bool isVar = with ? funcType->withIsVarAt(i) : funcType->normIsVarAt(i);
            if (!isVar && param != nullptr && !param->assignableFrom(entered[i]) &&
                entersImplicitly(param, entered[i])) {
                entered[i] = param;
            }
        }
        return entered;
    };
    const auto accepts = [&](FunctionType *funcType, bool entering) {
        StaticFuncTypeResolver resolver(funcType);
        if (!entering) {
            return resolver.resolve(withInputTypes, normInputTypes, Modifier::None).has_value();
        }
        return resolver
            .resolve(
                enteringTypes(funcType, withInputTypes, true),
                enteringTypes(funcType, normInputTypes, false),
                Modifier::None)
            .has_value();
    };
    const auto enterParameters = [&](FunctionType *funcType) {
        const auto enter = [&](std::vector<Argument> &inputs, type_vec_t &types, bool with) {
            const size_t count = with ? funcType->withTypesCount() : funcType->normTypesCount();
            for (size_t i = 0; i < inputs.size() && i < count; ++i) {
                Type *param      = with ? funcType->withTypeAt(i) : funcType->normTypeAt(i);
                const bool isVar = with ? funcType->withIsVarAt(i) : funcType->normIsVarAt(i);
                if (isVar || param == nullptr || containsTypeVar(param) ||
                    param->assignableFrom(types[i])) {
                    continue;
                }
                if (node_handle_t entered = coerceTo(inputs[i].node, param, inputs[i].expr)) {
                    inputs[i].node = entered;
                    types[i]       = param;
                }
            }
        };
        enter(withInputs, withInputTypes, true);
        enter(normInputs, normInputTypes, false);
    };

    // A call of a decorated function calls the function its decorators returned, so the
    // arguments are checked against that function's parameters (they were not checked at
    // all: a float passed where the decorated function takes an int was read as an int).
    const auto callDecorated = [&](const compile_graph_ptr_t &decoratedGraph) {
        node_handle_t decoratorInvoke = lowerDecoratedGraphValue(decoratedGraph, gct);
        FunctionType *factoryType     = tt::as_ptr<FunctionType>(nodeTypeOf(decoratorInvoke));
        if (!accepts(factoryType, false) && !accepts(factoryType, true)) {
            reportConversionGaps(
                {{std::string(), factoryType}},
                withInputs,
                normInputs,
                withInputTypes,
                normInputTypes,
                gct);
            diags_->of(SemanticDiag::ArgumentsMismatch)
                .atOrigin(gct->origin())
                .commit(
                    factoryType->toString(),
                    std::format(
                        "<{}> ({})",
                        strutil::join(withInputTypes, ", ", [](Type *t) { return t->toString(); }),
                        strutil::join(normInputTypes, ", ", [](Type *t) {
                            return t->toString();
                        })));
            throw BuildAbortException();
        }
        targetNode = currGraph_->addCallNode(factoryType->exitType());
        linkNodes(LinkType::With, decoratorInvoke, targetNode);
        targetFuncType = factoryType;
    };

    // An operator with overloads of the program: one of them, or the built-in group below.
    // `a != b` by an overload of `==` is the negation of that call (`negateResult`).
    compile_graph_ptr_t operatorOverload = nullptr;
    bool negateResult                    = false;
    if (auto overloads = operatorOverloads_.find(targetNode);
        overloads != operatorOverloads_.end()) {
        operatorOverload = chooseOperatorOverload(
            overloads->second,
            withInputs,
            normInputs,
            withInputTypes,
            normInputTypes,
            gct,
            accepts);
    }
    if (operatorOverload != nullptr) {
        negateResult = negatedEqualities_.contains(targetNode);
        targetGraph  = operatorOverload;
        currGraph_->addDependencyGraph(targetGraph);
        targetNode     = createFuncDataNode(targetGraph, false, true);
        targetFuncType = targetGraph->funcType();
    } else if (auto generic = genericDrefs_.find(targetNode); generic != genericDrefs_.end()) {
        // A call of a generic function: the argument types select (and, the first time,
        // build) the instance, which is then called like any other function.
        targetGraph = resolveGenericCall(
            generic->second,
            withInputs,
            normInputs,
            withInputTypes,
            normInputTypes,
            gct,
            expectedResult);
        if (!targetGraph->funcType()->hasExitType()) {
            diags_->of(SemanticDiag::CallingIncompleteFunction)
                .atOrigin(gct->origin())
                .commit(targetGraph->name(), targetGraph->funcType()->toString());
            throw BuildAbortException();
        }
        currGraph_->addDependencyGraph(targetGraph);
        targetNode     = createFuncDataNode(targetGraph, false, true);
        targetFuncType = targetGraph->funcType();
    } else if (nodeIsKind(targetNode, runtime::GCNodeKind::Dref)) {
        const auto &drefTarget = nodeGraphOf(targetNode)->drefTarget(targetNode);
        if (std::holds_alternative<graph_vec_ptr_t>(drefTarget)) {
            auto graphs = asCompileGraphVec(drefTarget);
            // The first overload, in declaration order, whose parameters take the arguments.
            // Only the chosen one has to be complete: an overload the call does not mean may
            // still be waiting for its return type.
            // A decorated overload is what its decorators return: it is chosen by that
            // function's parameters and called through it.
            compile_graph_ptr_t chosenDecorated = nullptr;
            const auto signatureOf = [&](const compile_graph_ptr_t &g) -> FunctionType * {
                if (auto it = decoratedOf_.find(g.get()); it != decoratedOf_.end()) {
                    return tt::as_ptr<FunctionType>(it->second->funcType()->exitType());
                }
                return g->funcType();
            };
            for (const bool entering : {false, true}) {
                if (targetGraph || chosenDecorated) {
                    break;
                }
                for (const auto &g : *graphs) {
                    FunctionType *funcType = signatureOf(g);
                    if (!accepts(funcType, entering)) {
                        continue;
                    }
                    // A function of the top level not built yet is built now: its return type
                    // may come from its body, its decorated form exists once it is built (step
                    // two of loading builds on demand).
                    ensureFunctionBuilt(g);
                    if (auto it = decoratedOf_.find(g.get()); it != decoratedOf_.end()) {
                        chosenDecorated = it->second;
                        break;
                    }
                    if (!funcType->hasExitType()) {
                        if (failedBodies_.contains(g.get())) {
                            // Its body is in error, which was reported there.
                            throw BuildAbortException();
                        }
                        diags_->of(SemanticDiag::CallingIncompleteFunction)
                            .atOrigin(gct->origin())
                            .commit(g->name(), funcType->toString());
                        throw BuildAbortException();
                    }
                    targetGraph = g;
                    break;
                }
            }
            if (!targetGraph && !chosenDecorated) {
                std::vector<std::pair<std::string, FunctionType *>> candidates;
                for (const auto &g : *graphs) {
                    candidates.emplace_back(g->name(), signatureOf(g));
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
                // Every argument written in the parentheses of a function that takes some of
                // them in angle brackets: say which, and how the call is written.
                if (withInputTypes.empty()) {
                    for (const auto &g : *graphs) {
                        FunctionType *type     = signatureOf(g);
                        const size_t normCount = type->normTypesCount();
                        const size_t withCount = type->withTypesCount();
                        bool fits              = withCount > 0 && normCount > 0 &&
                                                 withCount + normCount == normInputTypes.size();
                        for (size_t i = 0; fits && i < normInputTypes.size(); ++i) {
                            Type *param = i < normCount ? type->normTypeAt(i)
                                                        : type->withTypeAt(i - normCount);
                            fits = containsTypeVar(param) || param->equals(normInputTypes[i]) ||
                                   param->assignableFrom(normInputTypes[i]);
                        }
                        if (fits) {
                            argTypesStr += withArgumentsHint(g->name(), normInputs, normCount);
                            break;
                        }
                    }
                }
                std::string overloadsStr =
                    "\n    " +
                    strutil::join(
                        *graphs,
                        "\n    ",
                        [&](const graph_ptr_t &g) {
                            return g->name() + ": " + signatureOf(g)->toString();
                        }) +
                    "\n";
                diags_->of(SemanticDiag::NoMatchingFunction)
                    .atOrigin(gct->origin())
                    .commit(argTypesStr, overloadsStr);
                throw BuildAbortException();
            }
            if (chosenDecorated) {
                callDecorated(chosenDecorated);
            } else {
                currGraph_->addDependencyGraph(targetGraph);
                targetNode     = createFuncDataNode(targetGraph, false, true);
                targetFuncType = targetGraph->funcType();
            }
        } else if (std::holds_alternative<oper_group_ptr_t>(drefTarget)) {
            auto ops = std::get<oper_group_ptr_t>(drefTarget);
            if (auto expanded = explicitOperatorTypes_.contains(targetNode)
                                    ? nullptr
                                    : expandTreeCall(ops, withInputs, normInputs, gct)) {
                LEAVE("LINK");
                return expanded;
            }
            settleGenericOperatorArguments(
                ops,
                withInputs,
                normInputs,
                withInputTypes,
                normInputTypes,
                gct);
            // A name the operator did not settle has no value.
            for (auto *inputs : {&withInputs, &normInputs}) {
                for (Argument &input : *inputs) {
                    if (nodeIsKind(input.node, runtime::GCNodeKind::Dref)) {
                        rejectNameWithoutValue(
                            input.node,
                            input.expr->type() == GCT::NodeKind::DREF ? input.expr->name()
                                                                      : std::string("the name"),
                            input.expr);
                    }
                }
            }
            // Constant arguments (shape literals, axes) let operators refine their result type.
            std::vector<StaticArg> normStatics;
            normStatics.reserve(normInputs.size());
            for (const Argument &input : normInputs) {
                normStatics.push_back(staticArgOf(input.node));
            }
            type_vec_t explicitTypes;
            if (auto typed = explicitOperatorTypes_.find(targetNode);
                typed != explicitOperatorTypes_.end()) {
                explicitTypes = std::move(typed->second);
                explicitOperatorTypes_.erase(typed);
            }
            const auto &res = ops->resolveForCall(
                explicitTypes,
                expectedResult,
                withInputTypes,
                normInputTypes,
                normStatics,
                Modifier::None);
            if (!res.has_value() && withInputs.empty()) {
                // `x == null` / `x != null` over a `T?`: the tag of `x`, not a comparison of
                // two values (design principles 3.8).
                if (node_handle_t test =
                        nullComparison(ops->name(), gct, normInputs, normInputTypes)) {
                    LEAVE("LINK");
                    return test;
                }
            }
            if (!res.has_value() && withInputs.empty()) {
                // `t += u` on a tensor: no operator changes the tensor in place; the new
                // tensor is computed and `t` bound to it, as for a number.
                if (node_handle_t rebound =
                        compoundAssignmentByRebinding(ops->name(), gct, normInputs)) {
                    LEAVE("LINK");
                    return rebound;
                }
            }
            if (!res.has_value()) {
                std::string argTypesStr = std::format(
                    "<{}> ({})",
                    strutil::join(withInputTypes, ", ", [](Type *t) { return t->toString(); }),
                    strutil::join(normInputTypes, ", ", [](Type *t) { return t->toString(); }));
                // An overload whose parameter kinds fit may know the precise reason (e.g. a
                // static tensor shape conflict); report that instead of "no match".
                std::vector<std::string> reasons;
                for (const auto &[uri, resolver] : ops->resolvers()) {
                    if (auto reason = resolver->explainCallRejection(
                            explicitTypes,
                            expectedResult,
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
                        .atOrigin(gct->origin())
                        .commit(ops->name(), argTypesStr, strutil::join(reasons, "; "));
                    throw BuildAbortException();
                }
                // An operator written in the program: say which, the group name is not what
                // the program wrote.
                if (const char *symbol = operatorSymbolOf(ops->name())) {
                    argTypesStr += std::format(
                        "; operator '{}' is not defined for these operand types",
                        symbol);
                }
                // Every argument written in the parentheses of an operator that takes some of
                // them in angle brackets (`split('a,b', ',')`, `put(m, k, v)`): if the call
                // resolves with the trailing arguments moved there, say so.
                if (withInputTypes.empty() && operatorSymbolOf(ops->name()) == nullptr) {
                    for (size_t normCount = 1; normCount < normInputTypes.size(); ++normCount) {
                        const type_vec_t asNorm(
                            normInputTypes.begin(),
                            normInputTypes.begin() + static_cast<std::ptrdiff_t>(normCount));
                        const type_vec_t asWith(
                            normInputTypes.begin() + static_cast<std::ptrdiff_t>(normCount),
                            normInputTypes.end());
                        const std::vector<StaticArg> statics(
                            normStatics.begin(),
                            normStatics.begin() + static_cast<std::ptrdiff_t>(normCount));
                        if (ops->resolve(asWith, asNorm, statics, Modifier::None).has_value()) {
                            argTypesStr += withArgumentsHint(ops->name(), normInputs, normCount);
                            break;
                        }
                    }
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
                // The overloads of the program are part of the set (design principles 10.2).
                if (auto overloads = operatorOverloads_.find(targetNode);
                    overloads != operatorOverloads_.end()) {
                    for (const auto &graph : overloads->second.graphs) {
                        overloadsStr += "    " + displayNameOf(graph->name()) + ": " +
                                        graph->funcType()->toString() + "\n";
                    }
                    for (const auto &tmpl : overloads->second.templates) {
                        overloadsStr += "    " + displayNameOf(tmpl->name) + ": " +
                                        tmpl->funcType->toString() + "\n";
                    }
                }
                diags_->of(SemanticDiag::NoMatchingFunction)
                    .atOrigin(gct->origin())
                    .commit(argTypesStr, overloadsStr);
                throw BuildAbortException();
            }
            targetOperator = *res;
            if (inPlaceCalls_.contains(gct)) {
                // Written by inPlaceRebindingOf, which checked that this overload has an
                // in-place form: the call works on the array :var/take hands it.
                const auto form =
                    camel::core::OperatorTable::instance().inPlaceForm(targetOperator->uri());
                ASSERT(form.has_value(), "An in-place rebinding lost its in-place form.");
                targetOperator = std::make_shared<OperatorIndex>(
                    targetOperator->name(),
                    targetOperator->funcType(),
                    *form);
            }
            rejectLiteralConversion(ops->name(), targetOperator->funcType(), normInputs, gct);
            // `foreach` drops what its element function gives: the value of the body of a
            // function written in place here is a value nothing takes (design principles
            // 11.3). A function named here may be used elsewhere, where its value counts.
            // The operator says so itself (CallSpec::discardsCallbackResult).
            const auto *callSpec =
                camel::core::OperatorTable::instance().callSpec(targetOperator->uri());
            if (callSpec != nullptr && callSpec->discardsCallbackResult) {
                for (const Argument &input : withInputs) {
                    if (input.expr == nullptr) {
                        continue;
                    }
                    if (auto element = lambdaGraphs_.find(input.expr);
                        element != lambdaGraphs_.end()) {
                        rejectDiscardedValue(element->second);
                    }
                }
            }
            // A constant format string is checked against the arguments it formats now, not
            // when the program runs (CallSpec::checkFormat; design principles 5.4).
            if (callSpec != nullptr && callSpec->checkFormat) {
                const bool patternInWith = callSpec->formatWithArg >= 0;
                const auto &patterns     = patternInWith ? withInputs : normInputs;
                const size_t at          = static_cast<size_t>(
                    patternInWith ? callSpec->formatWithArg : callSpec->formatNormArg);
                if (at < patterns.size()) {
                    const StaticArg pattern = staticArgOf(patterns[at].node);
                    if (pattern.value.has_value() && *pattern.value != NullSlot &&
                        valueTypeOf(nodeTypeOf(patterns[at].node))->code() == TypeCode::String) {
                        const std::string_view text =
                            rtdata::fromSlot<const ::String *>(*pattern.value)->view();
                        const type_vec_t &formatted =
                            patternInWith ? normInputTypes : withInputTypes;
                        if (const auto problem = callSpec->checkFormat(text, formatted)) {
                            diags_->of(SemanticDiag::ArgumentsRejected)
                                .atOrigin(patterns[at].expr->origin())
                                .commit(
                                    ops->name(),
                                    "format string '" + std::string(text) + "'",
                                    *problem);
                            throw BuildAbortException();
                        }
                    }
                }
            }
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
                            .atOrigin(input.expr->origin())
                            .commit(
                                ops->name(),
                                type->toString(),
                                "an operator calls a function argument with values, and a "
                                "function with a var parameter must be given a variable");
                        throw BuildAbortException();
                    }
                }
            }
            targetNode = currGraph_->addOperNode(targetOperator, 0);
            stampPermissions(targetNode);
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
                    camel::core::OperatorTable::instance().typeFolder(targetOperator->uri());
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
            callDecorated(std::get<graph_ptr_t>(drefTarget));
        } else {
            ASSERT(false, "Dref target must be graph set, operator group, or decorated graph.");
        }
    } else {
        Type *dataType = nodeTypeOf(targetNode);
        ASSERT(
            dataType->code() == TypeCode::Function,
            "Target node of LINK must be a function or operator node.");
        auto *funcType = tt::as_ptr<FunctionType>(dataType);
        if (!accepts(funcType, false) && !accepts(funcType, true)) {
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
                .atOrigin(gct->origin())
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
    if (targetOperator == nullptr) {
        enterParameters(targetFuncType);
    }
    if (recorder_) {
        recorder_->callResolved(
            gct->origin(),
            gct->at(0)->origin(),
            targetGraph ? targetGraph->stableId() : std::string(),
            targetOperator ? targetOperator->uri() : std::string(),
            targetFuncType);
    }
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
    // A function, an assignment and a variable operator (`:var/take`, inPlaceRebindingOf) are
    // given the variable itself at a `var` position; any other operator its value.
    const bool takesVariables = targetOperator == nullptr ||
                                targetOperator->name().starts_with("__assn") ||
                                targetOperator->name().starts_with("__var_");
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
                    .atOrigin(arg.expr->origin())
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
    std::vector<node_handle_t> capturedWrites;
    for (const CapturedUse &use : captured) {
        orderAfterLastWriter(use.variable, targetNode);
        if (use.written) {
            capturedWrites.push_back(use.origin);
        }
    }
    if (!waited_ && !synced_) {
        const std::string subject =
            std::format("The call of '{}: {}'", targetName, targetFuncType->toString());
        if (hasVarArgument) {
            diags_->of(SemanticDiag::IgnoredSideEffect)
                .atOrigin(gct->origin())
                .commit(subject, "it takes a var argument");
        } else if (!capturedWrites.empty()) {
            unit_->pendingIgnoredWrites.push_back(
                {{targetGraph.get()}, capturedWrites, gct->origin(), subject, "it writes"});
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
        chainedNodes_.insert(targetNode);
    }
    if (negateResult) {
        // `a != b` through an overload of `==`: `!(a == b)`, which needs a bool from it.
        if (Type *equal = valueTypeOf(nodeTypeOf(targetNode));
            equal == nullptr || equal->code() != TypeCode::Bool) {
            diags_->of(SemanticDiag::OperatorOverloadInvalid)
                .atOrigin(gct->origin())
                .commit(
                    "==",
                    std::format(
                        "'!=' is the negation of '==', and the overload of '==' for these "
                        "operands returns '{}', not 'bool'",
                        equal != nullptr ? equal->toString() : std::string("void")));
            throw BuildAbortException();
        }
        node_handle_t negation = applyTagOperator("__not__", bindTemporary(targetNode), gct);
        LEAVE("LINK");
        return negation;
    }
    LEAVE("LINK");
    return targetNode;
}

node_handle_t Builder::visitWithNode(const GCT::node_ptr_t &gct) {
    ENTER("WITH");
    // `f<w>(x)`: the callee of the call is the target of the binding.
    genericCalleeAllowed_ =
        std::exchange(genericCalleeAllowed_, false) &&
        (gct->at(0)->type() == GCT::NodeKind::DREF || gct->at(0)->type() == GCT::NodeKind::TAPP);
    any targetNodeRes     = visit(gct->at(0));
    genericCalleeAllowed_ = false;
    // As in visitLinkNode: a lambda written in place gives its graph.
    node_handle_t targetNode = valueNodeOf(targetNodeRes);
    vector<Argument> inputs;
    FunctionType *argumentSignature = signatureForArgumentContext(targetNode);
    for (size_t i = 1; i < gct->size(); i++) {
        Type *wanted = argumentSignature && i <= argumentSignature->withTypesCount()
                           ? argumentSignature->withTypeAt(i - 1)
                           : nullptr;
        ExpectedValueScope argumentScope(
            expectedValueType_,
            wanted && !containsTypeVar(wanted) ? wanted : nullptr);
        inputs.push_back({visitOperand(gct->at(i), gct), gct->at(i)});
    }
    auto &bound = boundWithArgs_[targetNode];
    bound.insert(bound.end(), inputs.begin(), inputs.end());
    boundWithCallee_.try_emplace(targetNode, gct->at(0));
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
    if (EnumType *enumType = asEnumType(nodeTypeOf(tgtNode))) {
        // A value of an enum has the fields of the variant it is, and which one that is the
        // compiler does not know (design principles 3.8, 5.3): they are taken out by matching.
        std::string example;
        for (const EnumVariant &variant : enumType->variants()) {
            if (variant.fieldCount() > 0) {
                example = enumType->decl()->shortName() + "::" + variant.name;
                example += variant.shape == VariantShape::Struct
                               ? " { " + variant.fieldNames.front() + ", ... }"
                               : "(...)";
                break;
            }
        }
        diags_->of(SemanticDiag::EnumFieldAccess)
            .atOrigin(gct->origin())
            .commit(
                gct->accessByIndex() ? std::to_string(gct->accessIndex()) : gct->accessKey(),
                enumType->toString(),
                example);
        throw BuildAbortException();
    }
    if (!nodeTypeOf(tgtNode)->isComposite()) {
        diags_->of(SemanticDiag::TypeNotIndexable)
            .atOrigin(gct->origin())
            .commit(nodeTypeOf(tgtNode)->toString());
        throw BuildAbortException();
    }

    const auto tgtType = nodeTypeOf(tgtNode);
    Type *elemType     = nullptr;
    if (asTagged(tgtType) != nullptr) {
        // A `T?` / `T!` has the layout of a tuple and is not one: its parts are reached by
        // `??`, `!!`, `?->` and `match` only (design principles 3.9).
        diags_->of(SemanticDiag::TaggedValueNotOpened)
            .atOrigin(gct->origin())
            .commit(tgtType->toString(), "a member cannot be read from it");
        throw BuildAbortException();
    }

    switch (tgtType->code()) {
    case TypeCode::Tuple: {
        const auto &tupleType = tt::as_ptr<TupleType>(tgtType);
        // A tuple has a fixed length: an element past its end is a compile-time error (it was
        // read unchecked, which crashed the compiler in release builds).
        if (gct->accessIndex() >= tupleType->size()) {
            diags_->of(SemanticDiag::InvalidAccessIndex)
                .atOrigin(gct->origin())
                .commit(std::to_string(gct->accessIndex()));
            throw BuildAbortException();
        }
        elemType = tupleType->typeAt(gct->accessIndex());
        break;
    }
    case TypeCode::Array: {
        // `.N` reads a tuple element and `.name` a struct field. On an array no back end
        // implements either: the array was read as if it were a tuple, without a bounds
        // check, and the program printed whatever was there. An element is read with `[N]`.
        diags_->of(SemanticDiag::TypeNotIndexable)
            .atOrigin(gct->origin())
            .commit(tgtType->toString());
        throw BuildAbortException();
    }
    case TypeCode::Struct: {
        const auto *strTy    = tt::as_ptr<camel::core::type::StructType>(tgtType);
        const auto &optIndex = strTy->findField(gct->accessKey());
        if (!optIndex.has_value()) {
            diags_->of(SemanticDiag::InvalidAccessIndex)
                .atOrigin(gct->origin())
                .commit(gct->accessKey());
            throw BuildAbortException();
        }
        elemType = strTy->typeAt(optIndex.value());
        break;
    }
    default:
        ASSERT(false, "Unexpected target type in ACCS node.");
    }

    node_handle_t accsNode{};
    if (gct->accessByIndex()) {
        accsNode = currGraph_->addAccsNode(elemType, static_cast<uint32_t>(gct->accessIndex()));
    } else {
        accsNode = currGraph_->addAccsNode(elemType, gct->accessKey());
    }
    linkNodes(LinkType::Norm, tgtNode, accsNode);
    registerNodeOrigin(context_, accsNode, gct, "gir.accs");
    LEAVE("ACCS");
    return accsNode;
}

std::string Builder::sourceLocationOf(uint64_t origin) const {
    const auto *sourceContext = context_ ? context_->sourceContext().get() : nullptr;
    const auto *record        = sourceContext ? sourceContext->origin(origin) : nullptr;
    const auto *span          = record ? sourceContext->span(record->primarySpan) : nullptr;
    const auto *file = span && span->valid() ? sourceContext->sourceFile(span->fileId) : nullptr;
    if (file == nullptr || span->startOffset > file->content.size()) {
        return {};
    }
    size_t line = 1, lineStart = 0;
    for (size_t i = 0; i < span->startOffset; ++i) {
        if (file->content[i] == '\n') {
            ++line;
            lineStart = i + 1;
        }
    }
    // The file name only: an error value must not differ with where the program is stored.
    const size_t slash = file->path.find_last_of("/\\");
    return std::format(
        "{}:{}:{}",
        slash == std::string::npos ? file->path : file->path.substr(slash + 1),
        line,
        span->startOffset - lineStart + 1);
}

std::string Builder::bindTemporary(node_handle_t node) {
    std::string name = "$tagged" + std::to_string(tagTemporaries_++);
    insertNode(name, node);
    return name;
}

void Builder::registerTreeOrigin(
    node_handle_t node, const GCT::node_ptr_t &origin, const char *kind) {
    registerNodeOrigin(context_, node, origin, kind);
}

node_handle_t
Builder::applyTagOperator(const char *op, const std::string &name, const GCT::node_ptr_t &origin) {
    // An ordinary call, built from a synthesized GCT so that capturing the operand from an
    // enclosing graph, overload resolution and origins are what they are for any call.
    const auto make = [&](GCT::node_ptr_t node) {
        node->setOrigin(origin->origin());
        return node;
    };
    GCT::node_ptr_t call = make(tree_->makeLink(1));
    *call << make(tree_->makeDRef(op)) << make(tree_->makeDRef(name));
    return visitLinkNode(call);
}

node_handle_t
Builder::castNodeTo(node_handle_t value, Type *target, const GCT::node_ptr_t &origin) {
    node_handle_t castNode = currGraph_->addCastNode(target);
    linkNodes(LinkType::Norm, value, castNode);
    registerNodeOrigin(context_, castNode, origin, "gir.cast");
    return castNode;
}

node_handle_t Builder::coerceTo(node_handle_t node, Type *target, const GCT::node_ptr_t &origin) {
    Type *source = valueTypeOf(nodeTypeOf(node));
    if (target->assignableFrom(source)) {
        return node;
    }
    if (!entersImplicitly(target, source)) {
        return nullptr;
    }
    node = valueOf(node); // what a variable holds, not the variable
    if (TupleType *tagged = asTagged(target)) {
        // `null` is the null slot of every `T?`; any other value enters the payload first.
        if (tagged->isNullable() && source->code() == TypeCode::Void) {
            return castNodeTo(node, tagged, origin);
        }
        node_handle_t inner = coerceTo(node, tagged->payloadType(), origin);
        return inner != nullptr ? castNodeTo(inner, tagged, origin) : nullptr;
    }
    // The empty literal `[]`: the same literal with the element type its position gives it.
    ASSERT(target->code() == TypeCode::Array, "Only an empty array literal is left here.");
    node_handle_t typed = currGraph_->addStaticDataNode(
        tt::as_shared<Data>(ArrayData::create(tt::as_ptr<ArrayType>(target)->elemType())));
    registerNodeOrigin(context_, typed, origin, "gir.data");
    return typed;
}

void Builder::coerceArmExit(
    const compile_graph_ptr_t &arm, Type *target, const GCT::node_ptr_t &origin) {
    Type *exitType = arm->funcType()->exitType();
    if (exitType != nullptr && target->assignableFrom(exitType)) {
        return;
    }
    // The nodes are added to the arm's own graph; no name is looked up, so the scopes of the
    // position the branch stands at do no harm.
    const compile_graph_ptr_t outer = std::exchange(currGraph_, arm);
    node_handle_t output            = arm->draft().node(arm->draft().exitNode());
    node_handle_t coerced           = coerceTo(output, target, origin);
    ASSERT(coerced != nullptr, "A branch arm is joined only with a type its value enters.");
    if (normInputsOf(coerced).empty() && !ctrlInputsOf(output).empty()) {
        // A literal took the place of the output (`[]`): what the output waited for (the
        // statements of a sync arm) is still waited for.
        node_handle_t gate = currGraph_->addGateNode(target);
        linkNodes(LinkType::Norm, coerced, gate);
        linkNodes(LinkType::Ctrl, output, gate);
        coerced = gate;
    }
    arm->setExitType(target);
    setGraphReturnFromResult(arm, nodeIdOf(coerced), nodeIdOf(coerced), target);
    currGraph_ = outer;
}

Type *Builder::declaredExitTypeHere() const {
    compile_graph_ptr_t graph = currGraph_;
    while (graph != nullptr && openArmGraphs_.contains(graph.get())) {
        graph = graph->outerGraph();
    }
    FunctionType *funcType = graph != nullptr ? graph->funcType() : nullptr;
    // An undeclared return type is set from the first value returned (see
    // checkDeclaredReturn): before that, a type that is present was declared.
    if (funcType == nullptr || !funcType->hasExitType() ||
        graph->draft().exitNode() != runtime::kInvalidNodeRef) {
        return nullptr;
    }
    return funcType->exitType();
}

node_handle_t Builder::adaptExitValue(const GCT::node_ptr_t &gct, node_handle_t value) {
    Type *declared   = declaredExitTypeHere();
    Type *valueType  = valueTypeOf(nodeTypeOf(value));
    const auto where = gct->origin() != 0 ? gct->origin() : gct->at(0)->origin();
    if (gct->exitType() == ExitType::Throw) {
        // `throw e`: the exit `return` is, with the failed form of the declared `T!` as its
        // value (design principles 3.9). OPER __throw__ makes the failed value, the CAST
        // gives it the declared type; both are the same object.
        TupleType *fallible = asFallible(declared);
        if (fallible == nullptr) {
            diags_->of(SemanticDiag::ThrowOutsideFallible)
                .atOrigin(where)
                .commit(
                    declared == nullptr
                        ? "the function around it declares no return type"
                        : "the function around it returns '" + declared->toString() + "'");
            throw BuildAbortException();
        }
        // Design principles 3.9: `throw` takes an error value (made by `error(kind,
        // message)` or bound by `case error e`), nothing else.
        if (!valueType->equals(errorType())) {
            diags_->of(SemanticDiag::ThrowNotAnError).atOrigin(where).commit(valueType->toString());
            throw BuildAbortException();
        }
        node_handle_t thrown = applyTagOperator("__throw__", bindTemporary(value), gct->at(0));
        return fallible->equals(nodeTypeOf(thrown)) ? thrown
                                                    : castNodeTo(thrown, fallible, gct->at(0));
    }
    // `return v` under a declared `T?` / `T!` puts `v` into that form: the counterpart of
    // `throw`, and one of the positions where a value enters by itself (design principles
    // 3.8; coerceTo). A value that already has the declared type, and one that fits neither
    // way (reported by checkDeclaredReturn), is left alone.
    if (declared != nullptr && !declared->assignableFrom(valueType)) {
        if (node_handle_t entered = coerceTo(value, declared, gct->at(0))) {
            return entered;
        }
    }
    return value;
}

compile_graph_ptr_t
Builder::buildArmGraph(const GCT::node_ptr_t &exec, const std::function<node_handle_t()> &body) {
    graph_ptr_t subGraph = enterScope(FunctionType::create());
    registerGraphOrigin(context_, subGraph, exec, "gir.brch.case.graph");
    bodyStates_[subGraph.get()] = BodyState::Open;
    openArmGraphs_.insert(subGraph.get());
    // A body given by the caller is a body of its own like any other (see visitBodyNode).
    const bool outerSynced        = synced_;
    const node_handle_t outerLast = lastSyncedNode_;
    const auto restore            = [&] {
        synced_         = outerSynced;
        lastSyncedNode_ = outerLast;
        openArmGraphs_.erase(subGraph.get());
        bodyStates_[subGraph.get()] = BodyState::Built;
    };
    try {
        if (body) {
            synced_         = false;
            lastSyncedNode_ = {};
            setBodyResult(subGraph, body());
        } else {
            setBodyResult(subGraph, visitBodyNode(exec));
        }
    } catch (...) {
        restore();
        leaveScope();
        throw;
    }
    restore();
    armGraphs_.insert(subGraph.get());
    unit_->builtBodies.push_back(subGraph);
    leaveScope();
    return subGraph;
}

node_handle_t Builder::visitTaggedBrchNode(const GCT::node_ptr_t &gct) {
    using Kind         = GCT::BranchKind;
    const Kind kind    = gct->branchKind();
    const bool onNull  = kind == Kind::NullThen;
    const char *symbol = onNull ? "??" : kind == Kind::ErrorThen ? "!!" : "?->";

    node_handle_t subject = valueNodeOf(visit(gct->at(0)));
    if (onNull && asTagged(valueTypeOf(nodeTypeOf(subject))) == nullptr &&
        isNarrowedName(gct->at(0))) {
        // `x ?? d` where an `if` around it already narrowed `x` to a `T` (design principles
        // 3.8): allowed, with a warning.
        subject = reopenNarrowedName(gct->at(0), subject, gct, "??");
    }
    Type *subjectType = valueTypeOf(nodeTypeOf(subject));
    TupleType *tagged = asTagged(subjectType);
    if (tagged == nullptr || tagged->isNullable() != onNull) {
        // DESIGN-QUESTION(nullable-error) Q6: `?->` is defined for errors only.
        diags_->of(SemanticDiag::TaggedOperandMismatch)
            .atOrigin(gct->origin())
            .commit(
                symbol,
                "its left side has type '" + subjectType->toString() + "', which is not a '" +
                    (onNull ? "T?" : "T!") + "'");
        throw BuildAbortException();
    }
    Type *payload          = tagged->payloadType();
    const std::string name = bindTemporary(subject);
    node_handle_t tagNode  = applyTagOperator(onNull ? "__is_null__" : "__is_error__", name, gct);
    const char *valueOp    = onNull ? "__null_value__" : "__error_value__";
    const GCT::node_ptr_t exec = gct->at(1)->at(0);
    std::vector<compile_graph_ptr_t> arms(2);

    if (kind != Kind::ErrorPass) {
        // Arm 0 is the right-hand side, a subgraph of its own: it is entered, and so
        // evaluated, only when the tag selects it. Arm 1 takes the value out.
        arms[0] = buildArmGraph(exec);
        // Design principles 3.8: the right-hand side is a `T`, and the result is a `T`; or
        // it is itself a `T?` (`T!` for `!!`), and the result is that: `a ?? b ?? 9` tries
        // `a`, then `b`, then gives the default. Arm 1 then hands on `a` as it is instead of
        // taking the value out. Either way it is one branch.
        Type *fallback   = arms[0]->funcType()->exitType();
        const bool plain = fallback != nullptr && (payload->unify(fallback) != nullptr ||
                                                   entersImplicitly(payload, fallback));
        const bool chained =
            !plain && fallback != nullptr && entersImplicitly(subjectType, fallback);
        if (plain) {
            if (payload->unify(fallback) == nullptr) {
                coerceArmExit(arms[0], payload, gct);
            }
            arms[1] = buildArmGraph(exec, [&] { return applyTagOperator(valueOp, name, gct); });
        } else if (chained) {
            coerceArmExit(arms[0], subjectType, gct);
            arms[1] = buildArmGraph(exec, [&] {
                const GCT::node_ptr_t dref = tree_->makeDRef(name);
                dref->setOrigin(gct->origin());
                return visitDRefNode(dref);
            });
        } else {
            diags_->of(SemanticDiag::TaggedOperandMismatch)
                .atOrigin(gct->origin())
                .commit(
                    symbol,
                    "its right side has type '" +
                        (fallback ? fallback->toString() : std::string("void")) +
                        "', but the left side holds a '" + payload->toString() + "'");
            throw BuildAbortException();
        }
        return buildBranch(gct, tagNode, &arms);
    }

    // `x ?-> f`: arm 1 calls `f` with the value and yields a `U!` (put into that form when
    // `f` returns a plain `U`); arm 0 hands the failed `x` on as that `U!`. Arm 1 is built
    // first because it is what determines `U`.
    Type *resultType = nullptr;
    arms[1]          = buildArmGraph(exec, [&] {
        insertNode(GCT::kPassedValueName, applyTagOperator(valueOp, name, gct));
        node_handle_t result = visitBodyNode(exec);
        ASSERT(result != nullptr, "The call of '?->' produced no node.");
        if (nodeIsKind(result, runtime::GCNodeKind::Gate) && !normInputsOf(result).empty()) {
            result = nodeDraftOf(result)->node(normInputsOf(result).front());
        }
        Type *type = valueTypeOf(nodeTypeOf(result));
        if (asFallible(type) != nullptr) {
            resultType = type;
            return result;
        }
        resultType = TupleType::fallible(type);
        return castNodeTo(result, resultType, gct);
    });
    arms[0]          = buildArmGraph(exec, [&] {
        const GCT::node_ptr_t dref = tree_->makeDRef(name);
        dref->setOrigin(gct->origin());
        node_handle_t failed = visitDRefNode(dref);
        return resultType->equals(subjectType) ? failed : castNodeTo(failed, resultType, gct);
    });
    return buildBranch(gct, tagNode, &arms);
}

node_handle_t
Builder::visitTaggedMatch(const GCT::node_ptr_t &gct, node_handle_t scrutinee, TupleType *type) {
    using CaseType       = GCT::CaseType;
    const auto malformed = [&](const std::string &why) {
        diags_->of(SemanticDiag::TaggedMatchCases)
            .atOrigin(gct->origin())
            .commit(type->toString(), why);
        throw BuildAbortException();
    };
    // DESIGN-QUESTION(nullable-error) Q4: exactly the tagged case and one other.
    GCT::node_ptr_t taggedCase = nullptr, restCase = nullptr;
    for (size_t i = 1; i < gct->size(); ++i) {
        const auto &caseNode = gct->at(i);
        const auto caseType  = caseNode->caseType();
        const bool isNull    = caseType == CaseType::Value &&
                               caseNode->at(0)->type() == GCT::NodeKind::DATA &&
                               caseNode->at(0)->data()->type()->code() == TypeCode::Void;
        if (isNull || caseType == CaseType::Error) {
            if (isNull != type->isNullable()) {
                malformed(
                    isNull ? "'case null' needs a value that may be null"
                           : "'case error' needs a value that may have failed");
            }
            if (taggedCase != nullptr) {
                malformed(
                    std::string("it has more than one '") + (isNull ? "case null" : "case error") +
                    "'");
            }
            taggedCase = caseNode;
        } else if (caseType == CaseType::Bind || caseType == CaseType::Else) {
            if (restCase != nullptr) {
                malformed("it has more than one case for the value");
            }
            restCase = caseNode;
        } else {
            malformed("a literal case cannot be compared with a value that is not opened yet");
        }
    }
    if (taggedCase == nullptr) {
        malformed(type->isNullable() ? "'case null' is missing" : "'case error e' is missing");
    }
    if (restCase == nullptr) {
        malformed("the case for the value is missing ('case v' or 'case _')");
    }

    const std::string name = bindTemporary(scrutinee);
    node_handle_t tagNode =
        applyTagOperator(type->isNullable() ? "__is_null__" : "__is_error__", name, gct);
    const GCT::node_ptr_t taggedExec = taggedCase->at(taggedCase->size() - 1);
    const GCT::node_ptr_t restExec   = restCase->at(0);
    std::vector<compile_graph_ptr_t> arms(2);
    arms[0] = buildArmGraph(taggedExec, [&] {
        if (type->isFallible()) {
            insertNode(taggedCase->binding(), applyTagOperator("__error_info__", name, taggedCase));
        }
        return visitBodyNode(taggedExec);
    });
    arms[1] = buildArmGraph(restExec, [&] {
        if (restCase->caseType() == CaseType::Bind) {
            insertNode(
                restCase->binding(),
                applyTagOperator(
                    type->isNullable() ? "__null_value__" : "__error_value__",
                    name,
                    restCase));
        }
        return visitBodyNode(restExec);
    });
    return buildBranch(gct, tagNode, &arms);
}

// ---- The enum (design principles 5.3) ----

namespace {

/// `Shape::Rect` gives ("Shape", "Rect"). A name that is not qualified gives an empty prefix.
std::pair<std::string, std::string> splitQualifiedName(const std::string &written) {
    const size_t last = written.rfind("::");
    if (last == std::string::npos) {
        return {{}, written};
    }
    return {written.substr(0, last), written.substr(last + 2)};
}

std::string lastSegmentOf(const std::string &path) {
    const size_t last = path.rfind("::");
    return last == std::string::npos ? path : path.substr(last + 2);
}

} // namespace

EnumType *Builder::enumTypeOfName(const Reference &name) {
    Type *resolved = nullptr;
    if (const auto declared = unit_->typeDecls.find(name); declared != unit_->typeDecls.end()) {
        resolved = declared->second;
    } else if (const auto imported = module_->getImportedType(name); imported.has_value()) {
        resolved = imported.value();
    }
    if (resolved == nullptr) {
        return nullptr;
    }
    if (EnumType *instance = asEnumType(resolved)) {
        return instance;
    }
    if (resolved->code() == TypeCode::TypeFunc) {
        // A generic enum written without its arguments (`Option`): the declaration's own
        // instance, whose arguments are its parameters; a value of it is made by inference.
        return asEnumType(static_cast<TypeFunctionType *>(resolved)->body());
    }
    return nullptr;
}

bool Builder::enumVariantOfName(
    const std::string &written, const GCT::node_ptr_t &origin, EnumType *&type, size_t &tag) {
    const auto [prefix, name] = splitQualifiedName(written);
    if (prefix.empty()) {
        return false;
    }
    EnumType *found = enumTypeOfName(Reference(prefix));
    if (found == nullptr) {
        return false; // a qualified name of something else (`io::println`)
    }
    const auto variant = found->decl()->findVariant(name);
    if (!variant.has_value()) {
        diags_->of(SemanticDiag::EnumPatternMismatch)
            .atOrigin(origin->origin())
            .commit(
                written,
                found->toString(),
                "'" + name + "' is not one of its variants (" + found->decl()->name() +
                    " declares " +
                    [&] {
                        std::string list;
                        for (const EnumVariant &each : found->decl()->variants()) {
                            if (!list.empty()) {
                                list += ", ";
                            }
                            list += each.name;
                        }
                        return list;
                    }() +
                    ")");
        throw BuildAbortException();
    }
    type = found;
    tag  = *variant;
    return true;
}

node_handle_t Builder::applyInternalOperator(
    const char *op, Type *typeArgument, const std::vector<GCT::node_ptr_t> &operands,
    const GCT::node_ptr_t &origin) {
    GCT::node_ptr_t callee = tree_->makeDRef(op);
    if (typeArgument != nullptr) {
        // The instance the call makes, as `f::<T>` gives one: the operator's result type is
        // that instance, and its slot is of it (a value of an enum type holds references).
        GCT::node_ptr_t application = tree_->makeTypeApplication();
        *application << callee << tree_->makeType(typeArgument, ImplMark::Graph);
        callee = application;
    }
    GCT::node_ptr_t call = tree_->makeLink(static_cast<uint32_t>(operands.size()));
    *call << callee;
    for (const auto &operand : operands) {
        *call << operand;
    }
    call->setOrigin(origin->origin());
    return visitLinkNode(call);
}

node_handle_t
Builder::visitEnumConstruction(const GCT::node_ptr_t &gct, EnumType *type, size_t tag) {
    const EnumDecl *decl       = type->decl();
    const EnumVariant &variant = decl->variants()[tag];
    const bool called          = gct->type() == GCT::NodeKind::LINK;
    const size_t argumentCount = called ? gct->size() - 1 : 0;

    std::vector<node_handle_t> fields;
    std::vector<Type *> fieldTypes; // what the arguments are, before the field types are known
    if (variant.shape == VariantShape::Struct) {
        if (argumentCount != 1) {
            diags_->of(SemanticDiag::EnumConstructionMismatch)
                .atOrigin(gct->origin())
                .commit(
                    decl->shortName() + "::" + variant.name,
                    decl->name(),
                    "its fields are given by name: '" + decl->shortName() + "::" + variant.name +
                        " { " +
                        [&] {
                            std::string list;
                            for (const std::string &field : variant.fieldNames) {
                                if (!list.empty()) {
                                    list += ", ";
                                }
                                list += field + ": ...";
                            }
                            return list;
                        }() +
                        " }'");
            throw BuildAbortException();
        }
        node_handle_t literal = visitOperand(gct->at(1), gct);
        Type *literalType     = valueTypeOf(nodeTypeOf(literal));
        if (literalType->code() != TypeCode::Struct) {
            diags_->of(SemanticDiag::EnumConstructionMismatch)
                .atOrigin(gct->at(1)->origin())
                .commit(
                    decl->shortName() + "::" + variant.name,
                    decl->name(),
                    "the value has type '" + literalType->toString() +
                        "', and the fields of a struct variant are given by name");
            throw BuildAbortException();
        }
        StructType *literalStruct = tt::as_ptr<StructType>(literalType);
        for (size_t i = 0; i < variant.fieldCount(); ++i) {
            const std::string &fieldName = variant.fieldNames[i];
            const auto position          = literalStruct->findField(fieldName);
            if (!position.has_value()) {
                diags_->of(SemanticDiag::EnumConstructionMismatch)
                    .atOrigin(gct->at(1)->origin())
                    .commit(
                        decl->shortName() + "::" + variant.name,
                        decl->name(),
                        "the value has no field '" + fieldName + "'");
                throw BuildAbortException();
            }
            Type *fieldType         = literalStruct->typeAt(*position);
            node_handle_t fieldNode = currGraph_->addAccsNode(fieldType, fieldName);
            linkNodes(LinkType::Norm, literal, fieldNode);
            registerNodeOrigin(context_, fieldNode, gct->at(1), "gir.accs");
            fields.push_back(fieldNode);
            fieldTypes.push_back(fieldType);
        }
        if (literalStruct->size() != variant.fieldCount()) {
            diags_->of(SemanticDiag::EnumConstructionMismatch)
                .atOrigin(gct->at(1)->origin())
                .commit(
                    decl->shortName() + "::" + variant.name,
                    decl->name(),
                    "the value has fields the variant does not: a struct variant takes exactly "
                    "the fields its declaration writes");
            throw BuildAbortException();
        }
    } else {
        if (argumentCount != variant.fieldCount()) {
            diags_->of(SemanticDiag::EnumVariantFieldCount)
                .atOrigin(gct->origin())
                .commit(
                    variant.name,
                    decl->name(),
                    std::to_string(variant.fieldCount()),
                    std::to_string(argumentCount));
            throw BuildAbortException();
        }
        for (size_t i = 0; i < argumentCount; ++i) {
            node_handle_t argument = visitOperand(gct->at(i + 1), gct);
            fields.push_back(argument);
            fieldTypes.push_back(valueTypeOf(nodeTypeOf(argument)));
        }
    }

    // The instance: the declaration's own when it is not generic, otherwise the arguments the
    // fields give (a field left open takes what the position expects, as `let x: Option<int>
    // = Option::None` does).
    EnumType *instance = type;
    if (decl->generic()) {
        type_bindings_t bindings;
        for (size_t i = 0; i < fields.size(); ++i) {
            unifyTypeVars(variant.fieldTypes[i], fieldTypes[i], bindings);
        }
        std::vector<Type *> arguments;
        arguments.reserve(decl->params().size());
        EnumType *expected = asEnumType(expectedValueType_);
        for (size_t i = 0; i < decl->params().size(); ++i) {
            Type *bound = findTypeBinding(bindings, decl->params()[i]);
            if (bound == nullptr && expected != nullptr && expected->decl() == decl &&
                expected->complete()) {
                bound = expected->params()[i];
            }
            if (bound == nullptr) {
                diags_->of(SemanticDiag::EnumTypeArgumentNotInferred)
                    .atOrigin(gct->origin())
                    .commit(decl->params()[i], decl->name(), variant.name);
                throw BuildAbortException();
            }
            arguments.push_back(bound);
        }
        instance = EnumType::get(const_cast<EnumDecl *>(decl), arguments);
    }

    // Each field as the variant of this instance has it.
    std::vector<GCT::node_ptr_t> operands;
    operands.push_back(tree_->makeData(
        tt::as_shared<Data>(std::make_shared<PrimaryData<int64_t>>(static_cast<int64_t>(tag)))));
    for (size_t i = 0; i < fields.size(); ++i) {
        Type *wanted          = instance->fieldType(tag, i);
        node_handle_t coerced = coerceTo(fields[i], wanted, gct);
        if (coerced == nullptr) {
            diags_->of(SemanticDiag::EnumFieldTypeMismatch)
                .atOrigin(gct->origin())
                .commit(
                    variant.shape == VariantShape::Struct ? "'" + variant.fieldNames[i] + "'"
                                                          : std::to_string(i),
                    decl->shortName() + "::" + variant.name,
                    wanted->toString(),
                    fieldTypes[i]->toString());
            throw BuildAbortException();
        }
        operands.push_back(tree_->makeDRef(bindTemporary(coerced)));
    }
    return applyInternalOperator("__enum_make__", instance, operands, gct);
}

node_handle_t
Builder::visitEnumMatch(const GCT::node_ptr_t &gct, node_handle_t scrutinee, EnumType *type) {
    using CaseType = GCT::CaseType;
    struct Arm {
        const GCT::node_ptr_t caseNode;
        size_t tag = 0;
        // The position of the field in the variant, and the name the arm binds it to.
        std::vector<std::pair<size_t, std::string>> bindings;
    };

    const std::string name      = bindTemporary(scrutinee);
    const node_handle_t tagNode = applyTagOperator("__enum_tag__", name, gct);
    const std::string tagName   = bindTemporary(tagNode);

    std::vector<Arm> arms;
    const GCT::node_ptr_t wildcard = [&] {
        for (size_t i = 1; i < gct->size(); ++i) {
            if (gct->at(i)->caseType() == CaseType::Else) {
                return gct->at(i);
            }
        }
        return GCT::node_ptr_t(nullptr);
    }();
    std::vector<bool> covered(type->variantCount(), false);
    for (size_t i = 1; i < gct->size(); ++i) {
        const GCT::node_ptr_t &caseNode = gct->at(i);
        if (caseNode->caseType() == CaseType::Else) {
            break; // the last case
        }
        if (caseNode->caseType() != CaseType::Enum) {
            // A bare name is a binding pattern everywhere else (`case v` over `T?` / `T!`), so
            // `case Empty` over an enum is read as a binding, not as the unit variant: say what
            // the name refers to and how the variant is written, instead of only that a case
            // must name a variant.
            std::string written = "case";
            std::string reason =
                "every case of a match over an enum names a variant, and the value a case "
                "does not name falls to 'case _'";
            if (caseNode->caseType() == CaseType::Bind) {
                written        = caseNode->binding();
                const auto tag = type->decl()->findVariant(written);
                if (tag.has_value()) {
                    const EnumVariant &variant = type->decl()->variants()[*tag];
                    const std::string how = variant.shape == VariantShape::Unit    ? "'"
                                            : variant.shape == VariantShape::Tuple ? "(...)'"
                                                                                   : " { ... }'";
                    reason = "'" + written +
                             "' is a variant of this enum, and a bare name in a pattern is a "
                             "binding; write the variant qualified: 'case " +
                             type->decl()->shortName() + "::" + written + how +
                             ", or 'case _' for every other value";
                } else {
                    reason = "'" + written +
                             "' is a bare name, and a bare name in a pattern is a binding; a "
                             "match over an enum names a variant: write 'case " +
                             type->decl()->shortName() + "::<variant>', or 'case _'";
                }
            }
            diags_->of(SemanticDiag::EnumPatternMismatch)
                .atOrigin(caseNode->origin())
                .commit(written, type->toString(), reason);
            throw BuildAbortException();
        }
        const GCT::Node::EnumCase &pattern = caseNode->enumCase();
        const auto [prefix, variantName]   = splitQualifiedName(*pattern.label);
        if (!prefix.empty() && lastSegmentOf(prefix) != lastSegmentOf(type->decl()->name())) {
            diags_->of(SemanticDiag::EnumPatternMismatch)
                .atOrigin(caseNode->origin())
                .commit(*pattern.label, type->toString(), "it is a variant of another enum");
            throw BuildAbortException();
        }
        const auto tag = type->decl()->findVariant(variantName);
        if (!tag.has_value()) {
            diags_->of(SemanticDiag::EnumPatternMismatch)
                .atOrigin(caseNode->origin())
                .commit(
                    *pattern.label,
                    type->toString(),
                    "'" + variantName + "' is not one of its variants");
            throw BuildAbortException();
        }
        const EnumVariant &variant = type->decl()->variants()[*tag];
        const bool structPattern   = caseNode->enumStructPattern();
        if (structPattern != (variant.shape == VariantShape::Struct)) {
            diags_->of(SemanticDiag::EnumPatternMismatch)
                .atOrigin(caseNode->origin())
                .commit(
                    *pattern.label,
                    type->toString(),
                    variant.shape == VariantShape::Struct
                        ? "this variant's fields are named: write 'case " + variantName + " { " +
                              [&] {
                                  std::string list;
                                  for (const std::string &field : variant.fieldNames) {
                                      if (!list.empty()) {
                                          list += ", ";
                                      }
                                      list += field;
                                  }
                                  return list;
                              }() +
                              " }'"
                        : "this variant's fields are in order: write 'case " + variantName + "(" +
                              [&] {
                                  std::string list;
                                  for (size_t f = 0; f < variant.fieldCount(); ++f) {
                                      if (!list.empty()) {
                                          list += ", ";
                                      }
                                      list += "_";
                                  }
                                  return list;
                              }() +
                              ")'");
            throw BuildAbortException();
        }
        Arm arm{caseNode, *tag, {}};
        if (structPattern) {
            for (size_t f = 0; f < pattern.count; ++f) {
                const std::string &fieldName = *pattern.fields[f];
                const auto position          = variant.findField(fieldName);
                if (!position.has_value()) {
                    diags_->of(SemanticDiag::EnumPatternMismatch)
                        .atOrigin(caseNode->origin())
                        .commit(
                            *pattern.label,
                            type->toString(),
                            "the variant has no field '" + fieldName + "'");
                    throw BuildAbortException();
                }
                arm.bindings.emplace_back(*position, *pattern.bindings[f]);
            }
            // A field written twice binds one position twice: the later binding was read
            // first, silently.
            for (size_t a = 0; a < arm.bindings.size(); ++a) {
                for (size_t b = a + 1; b < arm.bindings.size(); ++b) {
                    if (arm.bindings[a].first == arm.bindings[b].first) {
                        diags_->of(SemanticDiag::EnumPatternMismatch)
                            .atOrigin(caseNode->origin())
                            .commit(
                                *pattern.label,
                                type->toString(),
                                "the field '" + variant.fieldNames[arm.bindings[a].first] +
                                    "' is written twice");
                        throw BuildAbortException();
                    }
                }
            }
        } else {
            if (pattern.count != variant.fieldCount()) {
                diags_->of(SemanticDiag::EnumPatternMismatch)
                    .atOrigin(caseNode->origin())
                    .commit(
                        *pattern.label,
                        type->toString(),
                        "the variant has " + std::to_string(variant.fieldCount()) +
                            " field(s) and the pattern gives " + std::to_string(pattern.count) +
                            "; write each position, '_' for the one not bound");
                throw BuildAbortException();
            }
            for (size_t f = 0; f < pattern.count; ++f) {
                arm.bindings.emplace_back(f, *pattern.bindings[f]);
            }
        }
        if (covered[*tag]) {
            diags_->of(SemanticDiag::EnumVariantCaseRepeated)
                .atOrigin(caseNode->origin())
                .commit(variantName);
        }
        covered[*tag] = true;
        arms.push_back(std::move(arm));
    }
    if (wildcard == nullptr) {
        std::string missing;
        for (size_t tag = 0; tag < type->variantCount(); ++tag) {
            if (!covered[tag]) {
                if (!missing.empty()) {
                    missing += ", ";
                }
                missing += "'" + type->decl()->variants()[tag].name + "'";
            }
        }
        if (!missing.empty()) {
            diags_->of(SemanticDiag::EnumMatchNotExhaustive)
                .atOrigin(gct->origin())
                .commit(type->toString(), missing);
            throw BuildAbortException();
        }
    } else {
        // Naming every variant already covers every value: the wildcard is a case that can
        // never be taken. It stays in the graph (the request asked for it), but say so.
        bool everyVariantCovered = true;
        for (bool each : covered) {
            if (!each) {
                everyVariantCovered = false;
                break;
            }
        }
        if (everyVariantCovered) {
            diags_->of(SemanticDiag::MatchWildcardRedundant)
                .atOrigin(wildcard->origin())
                .commit(type->toString());
        }
    }

    const auto indexData = [&](size_t index) {
        return tree_->makeData(
            tt::as_shared<Data>(
                std::make_shared<PrimaryData<int64_t>>(static_cast<int64_t>(index))));
    };
    // The fields the case binds, each taken out of the value by the position of the variant the
    // case names: the variant is known here, so the type of each field is known too.
    const auto bindFields = [&](const Arm &arm) {
        for (const auto &[index, binding] : arm.bindings) {
            if (binding.empty()) {
                continue;
            }
            node_handle_t fieldNode = applyInternalOperator(
                "__enum_field__",
                nullptr,
                {indexData(arm.tag), indexData(index), tree_->makeDRef(name)},
                arm.caseNode);
            insertNode(binding, fieldNode);
        }
    };
    // The arm that runs when no case names the value: the wildcard the program wrote, or,
    // when it wrote every variant, an arm that cannot be reached. The second is needed all
    // the same: the branch it ends offers the join one value of the arms' type.
    std::function<compile_graph_ptr_t()> defaultArm = [&]() -> compile_graph_ptr_t {
        if (wildcard != nullptr) {
            return buildArmGraph(wildcard->at(0), [&] { return visitBodyNode(wildcard->at(0)); });
        }
        const Arm &last = arms.back();
        return buildArmGraph(last.caseNode, [&] {
            bindFields(last);
            return visitBodyNode(last.caseNode);
        });
    };

    // Every case of the value is one test of the tag: `tag == k`, an ordinary comparison,
    // whose true arm is the body of that case and whose false arm is the test of the next.
    // A branch on a boolean has no With inputs, so the case values of the GCT (variant
    // names) never reach a node.
    std::function<node_handle_t(size_t)> branchAt = [&](size_t k) -> node_handle_t {
        const Arm &arm          = arms[k];
        node_handle_t condition = applyInternalOperator(
            "__eq__",
            nullptr,
            {tree_->makeDRef(tagName), indexData(arm.tag)},
            arm.caseNode);
        std::vector<compile_graph_ptr_t> two(2);
        two[0] = buildArmGraph(arm.caseNode, [&] {
            bindFields(arm);
            return visitBodyNode(arm.caseNode);
        });
        if (k + 1 < arms.size()) {
            const GCT::node_ptr_t next = arms[k + 1].caseNode;
            two[1]                     = buildArmGraph(next, [&] { return branchAt(k + 1); });
        } else {
            two[1] = defaultArm();
        }
        return buildBranch(gct, condition, &two);
    };
    if (arms.empty()) {
        // `match e { case _ => ... }`: nothing is tested; the only arm is the value. The
        // branch has one arm and no case value, so the arm the runtime chooses is that one.
        node_handle_t always = currGraph_->addStaticDataNode(
            tt::as_shared<Data>(std::make_shared<PrimaryData<bool>>(true)));
        registerNodeOrigin(context_, always, gct, "gir.data");
        std::vector<compile_graph_ptr_t> one(1);
        one[0] = buildArmGraph(wildcard->at(0), [&] { return visitBodyNode(wildcard->at(0)); });
        return buildBranch(gct, always, &one);
    }
    return branchAt(0);
}

namespace {

bool isNullLiteral(const GCT::node_ptr_t &expr) {
    return expr->type() == GCT::NodeKind::DATA && expr->data()->type()->code() == TypeCode::Void;
}

bool isLogicalBranch(const GCT::node_ptr_t &expr, GCT::BranchKind kind) {
    return expr->type() == GCT::NodeKind::BRCH && expr->branchKind() == kind;
}

/// The right side of the `&&` / `||` branch `brch` (see GCT::BranchKind for its layout).
GCT::node_ptr_t rightSideOf(const GCT::node_ptr_t &brch) {
    const bool isAnd = brch->branchKind() == GCT::BranchKind::LogicalAnd;
    return brch->at(isAnd ? 1 : 2)->at(0)->at(0);
}

/// One comparison of a name with `null` inside a condition.
struct NullTest {
    std::string name;
    GCT::node_ptr_t expr;    // the comparison
    GCT::node_ptr_t subject; // the name in it
    /// The name is not null where the condition the test is part of holds (`x != null`), as
    /// opposed to where it does not (`x == null`).
    bool nonNullWhenTrue;
    /// Not under a `!` (`!(x == null)` is not one of the forms narrowing follows).
    bool direct;
};

/// `expr` as the comparison of a name with `null`, in either order.
std::optional<NullTest> nullTestOf(const GCT::node_ptr_t &expr) {
    if (expr->type() != GCT::NodeKind::LINK || expr->size() != 3 ||
        expr->at(0)->type() != GCT::NodeKind::DREF) {
        return std::nullopt;
    }
    const std::string op = expr->at(0)->name();
    if (op != "__eq__" && op != "__neq__") {
        return std::nullopt;
    }
    const bool nullFirst     = isNullLiteral(expr->at(1));
    const auto &subject      = expr->at(nullFirst ? 2 : 1);
    const auto &comparedWith = expr->at(nullFirst ? 1 : 2);
    if (!isNullLiteral(comparedWith) || subject->type() != GCT::NodeKind::DREF) {
        return std::nullopt;
    }
    return NullTest{subject->name(), expr, subject, op == "__neq__", true};
}

/// Every comparison of a name with `null` the condition `expr` is made of, looking through
/// `&&`, `||` and `!`. This is wider than what narrowing follows (see conjunctsOf): the
/// tests found here and not followed are the ones a diagnostic explains.
void collectNullTests(const GCT::node_ptr_t &expr, bool negated, std::vector<NullTest> &tests) {
    if (auto test = nullTestOf(expr)) {
        test->nonNullWhenTrue = test->nonNullWhenTrue != negated;
        test->direct          = test->direct && !negated;
        tests.push_back(std::move(*test));
    } else if (
        expr->type() == GCT::NodeKind::LINK && expr->size() == 2 &&
        expr->at(0)->type() == GCT::NodeKind::DREF && expr->at(0)->name() == "__not__") {
        collectNullTests(expr->at(1), !negated, tests);
    } else if (
        isLogicalBranch(expr, GCT::BranchKind::LogicalAnd) ||
        isLogicalBranch(expr, GCT::BranchKind::LogicalOr)) {
        collectNullTests(expr->at(0), negated, tests);
        collectNullTests(rightSideOf(expr), negated, tests);
    }
}

/// The operands that `&&` joins in `expr` (`expr` itself when it is not an `&&`). Each of
/// them holds where `expr` does: a test against null among them narrows the then arm
/// (design principles 3.8).
void conjunctsOf(const GCT::node_ptr_t &expr, std::vector<const GCT::Node *> &conjuncts) {
    if (isLogicalBranch(expr, GCT::BranchKind::LogicalAnd)) {
        conjunctsOf(expr->at(0), conjuncts);
        conjunctsOf(rightSideOf(expr), conjuncts);
    } else {
        conjuncts.push_back(expr);
    }
}

/// Whether the name is written as a DREF anywhere in `node`. A use of another declaration of
/// the same name counts too, and a name kept in a constant (an element of an array literal)
/// is not seen: the answer only decides whether a diagnostic gets a note.
bool mentionsName(const GCT::node_ptr_t &node, const std::string &name) {
    if (node == nullptr) {
        return false;
    }
    if (node->type() == GCT::NodeKind::DREF && node->name() == name) {
        return true;
    }
    for (size_t i = 0; i < node->size(); ++i) {
        if (mentionsName(node->at(i), name)) {
            return true;
        }
    }
    return false;
}

} // namespace

node_handle_t Builder::nullComparison(
    const std::string &group, const GCT::node_ptr_t &gct, const std::vector<Argument> &operands,
    const type_vec_t &types) {
    if ((group != "__eq__" && group != "__neq__") || operands.size() != 2) {
        return nullptr;
    }
    const bool nullFirst = isNullLiteral(operands[0].expr);
    if (nullFirst == isNullLiteral(operands[1].expr)) {
        return nullptr; // no `null` written, or two
    }
    // Only a `T?` has the tag asked about. Two `T?` values are not compared (no `null` is
    // written then).
    const size_t subject = nullFirst ? 1 : 0;
    Type *subjectType    = valueTypeOf(types[subject]);
    if (asNullable(subjectType) == nullptr) {
        // A value whose type is never null (`5`, an `int`, a name already narrowed to a `T`):
        // the test is allowed and its result is fixed, which is worth a warning (design
        // principles 3.8). The value itself is not needed, so nothing evaluates it. `null`
        // itself (type `void`) is left to the comparison of two `null`s, which is refused.
        if (subjectType == nullptr || subjectType->code() == TypeCode::Void) {
            return nullptr;
        }
        const bool fixed      = group == "__neq__";
        const auto &written   = operands[subject].expr;
        const std::string who = written->type() == GCT::NodeKind::DREF
                                    ? std::format("'{}'", written->name())
                                    : std::string("This value");
        diags_->of(SemanticDiag::NullTestNeverNull)
            .atOrigin(gct->origin())
            .commit(who, subjectType->toString(), fixed ? "true" : "false");
        const GCT::node_ptr_t literal =
            tree_->makeData(tt::as_shared<Data>(std::make_shared<BoolData>(fixed)));
        literal->setOrigin(gct->origin());
        return visitDataNode(literal);
    }
    node_handle_t isNull =
        applyTagOperator("__is_null__", bindTemporary(valueOf(operands[subject].node)), gct);
    return group == "__eq__" ? isNull : applyTagOperator("__not__", bindTemporary(isNull), gct);
}

bool Builder::isNarrowedName(const GCT::node_ptr_t &expr) {
    if (expr->type() != GCT::NodeKind::DREF) {
        return false;
    }
    // The scope maps the name to the value the narrowing `if` bound in its arm, also from an
    // arm or a function nested in that arm (which reaches it through a capture).
    const auto bound = nodeScope_->get(expr->name());
    return bound.has_value() && narrowedValues_.contains(*bound);
}

node_handle_t Builder::reopenNarrowedName(
    const GCT::node_ptr_t &expr, node_handle_t subject, const GCT::node_ptr_t &at,
    const char *construct) {
    // Design principles 3.8: the construct is still allowed on the name; it is applied to the
    // value put back into `T?`, so that it is checked and built exactly as on the name before
    // narrowing (the right side of `??` still has to fit, the cases of the `match` still have
    // to be its fixed patterns), and only its null arm can never be taken.
    Type *type = valueTypeOf(nodeTypeOf(subject));
    diags_->of(SemanticDiag::KnownNotNull)
        .atOrigin(at->origin())
        .commit(expr->name(), type->toString(), construct);
    node_handle_t reopened = coerceTo(subject, TupleType::nullable(type), at);
    ASSERT(reopened != nullptr, "A value always enters the nullable form of its own type.");
    return reopened;
}

node_handle_t Builder::visitConditionBrchNode(const GCT::node_ptr_t &gct) {
    using Kind         = GCT::BranchKind;
    using CaseType     = GCT::CaseType;
    const Kind kind    = gct->branchKind();
    const bool logical = kind == Kind::LogicalAnd || kind == Kind::LogicalOr;
    if (gct->size() != 3 || gct->at(1)->caseType() != CaseType::True ||
        gct->at(2)->caseType() != CaseType::Else) {
        ASSERT(!logical, "'&&' and '||' are lowered with a true arm and an else arm.");
        return nullptr; // a `match`
    }
    const GCT::node_ptr_t &cond = gct->at(0);
    std::vector<NullTest> tests;
    collectNullTests(cond, false, tests);
    if (!logical && tests.empty()) {
        return nullptr; // an `if` that tests no name against null
    }

    // Narrowing (design principles 3.8). In the arm of `if x != null then A else B`
    // that excludes null, `x` is a `T`: the arm starts by taking the value out of `x`, and
    // the name `x` stands for that value inside the arm, as `v` does in
    // `match x { case null => B  case v => A }`. The value is taken out inside the arm
    // because doing so has a meaning only where the arm is selected.
    //
    // It applies to a name bound by `let` or a parameter (a `var` may be rewritten between
    // the test and the use), and to the direct forms only: `x != null` or `null != x` that
    // is the whole condition or one of the operands `&&` joins narrows the then arm
    // (`if x != null && ready then ...`); `x == null` or `null == x` that is the whole
    // condition narrows the else arm. Nothing under `!` or `||` narrows the arms of the `if`,
    // and the else arm of a conjunction learns nothing. A test that is found but not
    // followed is kept as the explanation of an error in the arm it would have narrowed.
    struct Opened {
        std::string name;
        GCT::node_ptr_t subject; // the name as the condition wrote it
        std::string temporary;   // what the arm captures (bindTemporary)
    };
    struct Refused {
        std::string name;
        std::string type;
        DiagLabel label;
    };
    struct Arm {
        GCT::node_ptr_t exec;
        std::vector<Opened> opened;
        std::vector<Refused> refused;
    };
    Arm thenArm{gct->at(1)->at(0), {}, {}}, elseArm{gct->at(2)->at(0), {}, {}};
    const std::optional<NullTest> whole = nullTestOf(cond);
    std::vector<const GCT::Node *> conjuncts;
    conjunctsOf(cond, conjuncts);
    for (const NullTest &test : tests) {
        const auto bound    = nodeAt(test.name);
        TupleType *nullable = bound.has_value() && *bound != nullptr
                                  ? asNullable(valueTypeOf(nodeTypeOf(*bound)))
                                  : nullptr;
        if (nullable == nullptr) {
            continue; // not a `T?`: nothing to narrow (and the comparison itself is an error)
        }
        Arm &arm            = test.nonNullWhenTrue ? thenArm : elseArm;
        const bool conjunct = std::ranges::find(conjuncts, test.expr) != conjuncts.end();
        const bool follows =
            test.direct && conjunct && (test.nonNullWhenTrue || conjuncts.size() == 1);
        const bool mutableName = isVariable(*bound);
        if (follows && !mutableName) {
            if (std::ranges::find(arm.opened, test.name, &Opened::name) == arm.opened.end()) {
                arm.opened.push_back({test.name, test.subject, {}});
            }
            continue;
        }
        const char *why =
            mutableName ? "a 'var' may be rewritten between the test and the use (bind its value "
                          "with 'let' and test that)"
            : test.direct && conjunct
                ? "where the operands of '&&' do not all hold, nothing is known about "
                  "any one of them"
                : "narrowing follows a test that is the whole condition or an operand "
                  "of '&&', not one under '||' or '!'";
        arm.refused.push_back(
            {test.name,
             nullable->toString(),
             DiagLabel{
                 SourceRange{test.subject->origin()},
                 std::format(
                     "'{}' still has the type '{}' in this arm; the test of it against null "
                     "does not narrow it: {}",
                     test.name,
                     nullable->toString(),
                     why),
                 ""}});
    }

    // The condition. One followed test that is the whole condition selects the arm by the
    // tag itself, which is the branch of the `match` above: arm 0 is the null arm. Any other
    // condition is a bool as in every `if`: arm 0 is the then arm.
    const bool byTag = whole.has_value() && thenArm.opened.size() + elseArm.opened.size() == 1;
    for (Arm *arm : {&thenArm, &elseArm}) {
        for (Opened &opened : arm->opened) {
            opened.temporary = bindTemporary(valueNodeOf(visit(opened.subject)));
        }
    }
    node_handle_t condNode =
        byTag ? applyTagOperator(
                    "__is_null__",
                    (thenArm.opened.empty() ? elseArm : thenArm).opened.front().temporary,
                    cond)
              : valueNodeOf(visit(cond));

    const char *symbol = kind == Kind::LogicalAnd ? "&&" : "||";
    // Reported at the operand that is not a bool.
    const auto requireBool = [&](Type *type, const char *side, const GCT::node_ptr_t &operand) {
        type = valueTypeOf(type);
        if (type == nullptr || type->code() != TypeCode::Bool) {
            const auto origin = operand->origin();
            diags_->of(SemanticDiag::LogicalOperandNotBool)
                .atOrigin(origin != 0 ? origin : gct->origin())
                .commit(symbol, side, type != nullptr ? type->toString() : std::string("void"));
            throw BuildAbortException();
        }
    };
    if (logical) {
        requireBool(nodeTypeOf(condNode), "left", cond);
    }

    const auto buildArm = [&](const Arm &arm) {
        const size_t errorsBefore = diags_->count(Severity::Error);
        const size_t diagsBefore  = diags_->count();
        try {
            if (arm.opened.empty()) {
                return buildArmGraph(arm.exec);
            }
            return buildArmGraph(arm.exec, [&] {
                for (const Opened &opened : arm.opened) {
                    // Opened whether or not the arm seems to use the name: a name can be
                    // written where no DREF shows it (the elements of an array literal are
                    // names kept in the constant), and an unused node costs nothing at run
                    // time, as nothing depends on it.
                    node_handle_t value =
                        applyTagOperator("__null_value__", opened.temporary, opened.subject);
                    [[maybe_unused]] const bool bound = insertNode(opened.name, value);
                    ASSERT(bound, "A narrowed name is the first name of its arm.");
                    narrowedValues_.insert(value);
                }
                return visitBodyNode(arm.exec);
            });
        } catch (const BuildAbortException &) {
            // An error in an arm that uses a name the condition tested without narrowing it,
            // and that speaks of the name's type: most likely the name was used as a `T`.
            const auto errors = diags_->errors();
            const auto first =
                errors.begin() + static_cast<std::ptrdiff_t>(std::min(errorsBefore, errors.size()));
            for (const Refused &refused : arm.refused) {
                if (mentionsName(arm.exec, refused.name) &&
                    std::any_of(first, errors.end(), [&](const Diagnostic *d) {
                        return d->message.find(refused.type) != std::string::npos;
                    })) {
                    diags_->addRelatedToErrorsFrom(diagsBefore, refused.label, 3);
                }
            }
            throw;
        }
    };
    // Arm 0 is the one the condition selects first: the null arm by tag, the then arm else.
    const bool thenFirst = !byTag || thenArm.opened.empty();
    std::vector<compile_graph_ptr_t> arms(2);
    arms[thenFirst ? 0 : 1] = buildArm(thenArm);
    arms[thenFirst ? 1 : 0] = buildArm(elseArm);
    if (logical) {
        // The right side is the then arm of `&&` and the else arm of `||`.
        const bool rightIsThen = kind == Kind::LogicalAnd;
        requireBool(
            arms[rightIsThen == thenFirst ? 0 : 1]->funcType()->exitType(),
            "right",
            rightSideOf(gct));
    }
    // An `if` without `else` has no value (design principles 3.7; see buildBranch).
    const bool valueless = !logical && elseArm.exec->size() == 0;
    return buildBranch(gct, condNode, &arms, valueless);
}

node_handle_t Builder::visitBrchNode(const GCT::node_ptr_t &gct) {
    ENTER("BRCH");
    const auto kind = gct->branchKind();
    if (kind == GCT::BranchKind::NullThen || kind == GCT::BranchKind::ErrorThen ||
        kind == GCT::BranchKind::ErrorPass) {
        node_handle_t node = visitTaggedBrchNode(gct);
        LEAVE("BRCH");
        return node;
    }
    // `&&`, `||`, and an `if` whose condition compares names with `null` (design principles
    // 3.8). Any other `if`, and every `match`, is built below.
    if (node_handle_t node = visitConditionBrchNode(gct)) {
        LEAVE("BRCH");
        return node;
    }
    const auto &res = visit(gct->at(0));
    ASSERT(
        res.type() == typeid(node_handle_t),
        "Unexpected result type from Enter the child of BRCH node.");
    node_handle_t condNode = any_cast<node_handle_t>(res);
    node_handle_t node{};
    if (asTagged(valueTypeOf(nodeTypeOf(condNode))) == nullptr && isNarrowedName(gct->at(0))) {
        // `match x { case null => ...  case v => ... }` where an `if` around it already
        // narrowed `x` to a `T` (design principles 3.8): allowed, with a warning. A match
        // without `case null` is an ordinary match on the `T`.
        const bool nullCase = std::any_of(gct->begin() + 1, gct->end(), [](const auto &c) {
            return c->caseType() == GCT::CaseType::Value && isNullLiteral(c->at(0));
        });
        if (nullCase) {
            condNode = reopenNarrowedName(gct->at(0), condNode, gct, "match");
        }
    }
    if (TupleType *tagged = asTagged(valueTypeOf(nodeTypeOf(condNode)))) {
        node = visitTaggedMatch(gct, condNode, tagged);
    } else if (EnumType *enumType = asEnumType(valueTypeOf(nodeTypeOf(condNode)))) {
        node = visitEnumMatch(gct, condNode, enumType);
    } else {
        for (size_t i = 1; i < gct->size(); ++i) {
            const GCT::node_ptr_t &boundCase = gct->at(i);
            if (boundCase->caseType() == GCT::CaseType::Bind ||
                boundCase->caseType() == GCT::CaseType::Error) {
                // The two patterns that bind are defined over `T?` / `T!` only. What an
                // identifier pattern means over any other type has not been decided.
                const bool isError = boundCase->caseType() == GCT::CaseType::Error;
                diags_->of(SemanticDiag::ConstructNotSupported)
                    .atOrigin(boundCase->origin())
                    .commit(
                        (isError ? "error " : "") + boundCase->binding(),
                        std::string(isError ? "the pattern 'error e'" : "an identifier pattern") +
                            " is implemented only for a value of type 'T?' or 'T!'; over '" +
                            nodeTypeOf(condNode)->toString() +
                            "' only literal patterns and '_' are");
                throw BuildAbortException();
            }
        }
        requireMatchDefault(gct, condNode);
        node = buildBranch(gct, condNode);
    }
    LEAVE("BRCH");
    return node;
}

void Builder::requireMatchDefault(const GCT::node_ptr_t &gct, node_handle_t scrutinee) {
    using CaseType    = GCT::CaseType;
    bool hasValueCase = false, hasTrue = false, hasFalse = false;
    for (size_t i = 1; i < gct->size(); ++i) {
        const auto &caseNode = gct->at(i);
        const auto caseType  = caseNode->caseType();
        if (caseType == CaseType::Else) {
            return;
        }
        if (caseType != CaseType::Value) {
            continue;
        }
        hasValueCase = true;
        if (caseNode->at(0)->type() == GCT::NodeKind::DATA) {
            const auto literal = std::dynamic_pointer_cast<BoolData>(caseNode->at(0)->data());
            if (literal) {
                (literal->data() ? hasTrue : hasFalse) = true;
            }
        }
    }
    if (!hasValueCase) {
        return; // an `if`
    }
    Type *type = valueTypeOf(nodeTypeOf(scrutinee));
    if (type == Type::Bool() && hasTrue && hasFalse) {
        return; // every value of the type has a case
    }
    // Design principles 3.7: a `match` must have a default case, and the compiler adds no
    // arm the program does not show. A `match` without `case _` is therefore refused here
    // (the fixed patterns over `T?` / `T!` never reach this function).
    diags_->of(SemanticDiag::MatchWithoutDefault).atOrigin(gct->origin()).commit(type->toString());
    throw BuildAbortException();
}

node_handle_t Builder::buildBranch(
    const GCT::node_ptr_t &gct, node_handle_t condNode,
    const std::vector<compile_graph_ptr_t> *arms, bool valueless) {
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
    // The arm being added is the one an `if` without `else` has in its place. With arms the
    // caller built, the caller says whether the branch is such an `if` (`valueless`): the
    // join then takes nothing from any of them.
    bool absentElse = valueless;
    bool ifArmSeen  = false;
    // Whether the branch is an `if` without `else`, known before its arms are added: its arms
    // give values nothing takes (rejectDiscardedValue in addArm).
    bool dropsArmValues = valueless;
    if (arms == nullptr && gct->size() >= 3) {
        const auto &first = gct->at(1);
        const auto &last  = gct->at(gct->size() - 1);
        dropsArmValues    = first->caseType() == GCT::CaseType::True &&
                            last->caseType() == GCT::CaseType::Else && last->size() == 1 &&
                            last->at(0)->size() == 0;
    }

    linkNodes(LinkType::Norm, condNode, brchNode);
    std::vector<runtime::GCBranchArm> branchArms;
    std::vector<node_handle_t> branchFuncs;
    // The variables the arms captured, and whether some arm writes them.
    std::vector<CapturedUse> captured;
    std::vector<DraftGraphBuilder *> armGraphs;
    std::vector<graph_ptr_t> armList;

    // Makes `subGraph` the next arm of the branch.
    const auto addArm = [&](const graph_ptr_t &subGraph) {
        if (dropsArmValues) {
            // The arm of an `if` without `else` gives a value nothing takes.
            rejectDiscardedValue(subGraph.get());
        }
        armGraphs.push_back(subGraph.get());
        armList.push_back(subGraph);
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

        // The arm is called by a FUNC node like a function; every value it captures is made
        // ready before the BRCH node runs (addCaptureSite). This keeps the nodes on every
        // branch path between BRCH and JOIN tightly grouped in topological order, with no
        // unrelated external nodes inserted in between, so that the graph scheduler can jump
        // directly without frequent checks.
        auto funcNode = createFuncDataNode(subGraph, false, true, brchNode);
        branchFuncs.push_back(funcNode);
        branchArms.push_back(
            runtime::GCBranchArm{
                .head = nodeIdOf(funcNode),
                .tail = nodeIdOf(funcNode),
            });

        if (absentElse) {
            // Design principles 3.7: an `if` without `else` has no value. Its arm may be of
            // any type and what the arm gives is dropped: the join takes nothing. The arm
            // written is evaluated as any arm is; the one not written is an empty arm.
            joinType = Type::Void();
            setNodeType(joinNode, joinType);
        } else if (joinType == nullptr) {
            joinType = exitType;
            setNodeType(joinNode, joinType);
        } else {
            // Arms must agree up to refinement: unify widens (for example two
            // tensors whose shapes differ in one extent) or fails. Failing that, one arm may
            // enter the form of the other (design principles 3.8): `if c then 1 else null`
            // is an `int?`, `if c then v else failed` a `T!`. The arm that is not of the
            // joined type gets the CAST at its exit once the type is known (below).
            Type *unified = joinTypes(joinType, exitType);
            if (unified != nullptr && unified != joinType) {
                joinType = unified;
                setNodeType(joinNode, joinType);
            }
            if (unified == nullptr) {
                diags_->of(SemanticDiag::BranchReturnTypeMismatch)
                    .atOrigin(gct->origin())
                    .commit(
                        currGraph_->name() + ": " + currGraph_->funcType()->toString(),
                        joinType->toString(),
                        exitType->toString());
                throw BuildAbortException();
            }
        }

        linkNodes(LinkType::Ctrl, brchNode, funcNode);
        linkNodes(LinkType::With, funcNode, joinNode);
    };

    const size_t armEnd = arms != nullptr ? arms->size() + 1 : gct->size();
    for (size_t i = 1; i < armEnd; i++) {
        graph_ptr_t subGraph;
        if (arms != nullptr) {
            // Arms the caller built (the tagged forms): a boolean condition and two arms.
            subGraph = (*arms)[i - 1];
        } else {
            const auto &caseNode         = gct->at(i);
            GCT::node_ptr_t caseExecNode = nullptr;

            switch (caseNode->caseType()) {
            case GCT::CaseType::True: {
                ASSERT(caseNode->size() == 1, "True case should have only one child.");
                // An `if`: the condition is a bool (language reference 6.8.1: no truth-value
                // conversion). Anything else was compared bit for bit with `true`.
                Type *condType = valueTypeOf(nodeTypeOf(condNode));
                if (condType->code() != TypeCode::Bool) {
                    diags_->of(SemanticDiag::ConditionNotBool)
                        .atOrigin(gct->at(0)->origin())
                        .commit(condType->toString());
                    throw BuildAbortException();
                }
                caseExecNode = caseNode->at(0);
                ifArmSeen    = true;
                break;
            }
            case GCT::CaseType::Else:
                ASSERT(i == gct->size() - 1, "Else case should be the last case.");
                ASSERT(caseNode->size() == 1, "Else case should have only one child.");
                caseExecNode = caseNode->at(0);
                absentElse   = ifArmSeen && caseExecNode->size() == 0;
                break;
            case GCT::CaseType::Value: {
                ASSERT(caseNode->size() == 2, "Value case should have two children.");
                any res = visit(caseNode->at(0));
                ASSERT(
                    res.type() == typeid(node_handle_t),
                    "Unexpected result type from visiting the case node.");
                node_handle_t valueNode = any_cast<node_handle_t>(res);
                // A case is compared with the value matched, by the comparison of that type:
                // a case of another type was compared bit for bit (`case 1` on a string read
                // the integer as a string, and crashed).
                Type *subjectType = valueTypeOf(nodeTypeOf(condNode));
                Type *caseType    = valueTypeOf(nodeTypeOf(valueNode));
                if (!subjectType->assignableFrom(caseType)) {
                    diags_->of(SemanticDiag::MatchCaseTypeMismatch)
                        .atOrigin(caseNode->at(0)->origin())
                        .commit(subjectType->toString(), caseType->toString());
                    throw BuildAbortException();
                }
                linkNodes(LinkType::With, valueNode, brchNode);
                caseExecNode = caseNode->at(1);
                break;
            }
            default:
                ASSERT(false, "Unknown case type in BRCH node.");
            }

            subGraph = buildArmGraph(caseExecNode);
        }
        addArm(subGraph);
    }
    if (joinType != nullptr && !absentElse) {
        for (size_t i = 0; i < armList.size(); ++i) {
            Type *exitType = armList[i]->funcType()->exitType();
            if (exitType != nullptr && !joinType->assignableFrom(exitType) &&
                entersImplicitly(joinType, exitType)) {
                coerceArmExit(armList[i], joinType, gct);
                // The node that stands for the arm was typed when the arm was added, with
                // what the arm gave then. A pass that folds the branch to this one arm
                // reads the value through that type (std::opt::fold read a `float?` box as
                // a float).
                // The slot the arm's value is kept in has a type of its own, given when the
                // node was made: a new slot of the joined type (a `float?` box kept in a
                // slot typed `float` is not traced by the collector either).
                setNodeType(branchFuncs[i], joinType);
                nodeDraftOf(branchFuncs[i])
                    ->setNodeDataIndex(
                        nodeIdOf(branchFuncs[i]),
                        currGraph_->addRuntimeSlot(joinType));
            }
        }
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
    std::vector<node_handle_t> capturedWrites;
    for (const CapturedUse &use : captured) {
        orderAfterLastWriter(use.variable, brchNode);
        if (use.written) {
            capturedWrites.push_back(use.origin);
        }
    }
    if (!capturedWrites.empty() && !waited_ && !synced_) {
        unit_->pendingIgnoredWrites.push_back(
            {armGraphs, capturedWrites, gct->origin(), "The branch", "an arm writes"});
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
        chainedNodes_.insert(brchNode);
        chainedNodes_.insert(joinNode);
        // The arms run between the BRCH and the JOIN, so in the chain too.
        chainedNodes_.insert(branchFuncs.begin(), branchFuncs.end());
    }

    return joinNode;
}

void Builder::buildLoopGraph(const compile_graph_ptr_t &graph, const GCT::node_ptr_t &exec) {
    // The graph is written inside the current one, like an arm: its scope is entered from
    // here, so the names of the enclosing code are visible in it (and captured).
    const bool atRoot = currGraph_ == rootGraph_;
    enterGraphScope(graph, "");
    if (atRoot) {
        rootSubGraphOwners_[graph.get()] = unit_;
    }
    registerGraphOrigin(context_, graph, exec, "gir.loop.graph");
    bodyStates_[graph.get()] = BodyState::Open;
    try {
        for (draft_node_ref_t portId : graph->draft().normPorts()) {
            node_handle_t port = graph->draft().node(portId);
            insertNode(graph->nodePortName(port), port);
            if (recorder_) {
                recordDeclaration(graph->nodePortName(port), port, exec);
            }
        }
        setBodyResult(graph, visitBodyNode(exec));
    } catch (...) {
        bodyStates_[graph.get()] = BodyState::Built;
        failedBodies_.insert(graph.get());
        leaveScope();
        throw;
    }
    bodyStates_[graph.get()] = BodyState::Built;
    unit_->builtBodies.push_back(graph);
    leaveScope();
}

node_handle_t Builder::visitLoopNode(const GCT::node_ptr_t &gct) {
    ENTER("LOOP");
    using camel::core::LoopKind;
    const LoopKind kind    = gct->loopKind();
    const size_t k         = gct->loopStateCount();
    const bool hasIterable = camel::core::loopHasIterable(kind);

    // The names of the head are declared once, together (design principles 3.10).
    {
        std::unordered_set<std::string> names;
        for (size_t j = 0; j < k + (hasIterable ? 1 : 0); ++j) {
            const std::string &name = j < k ? gct->loopStateName(j) : gct->loopElementName();
            if (!names.insert(name).second) {
                diags_->of(SemanticDiag::LoopNameRepeated).atOrigin(gct->origin()).commit(name);
                throw BuildAbortException();
            }
        }
    }

    // The first values and the array: evaluated once, where the loop is (they do not see the
    // names of the head).
    std::vector<node_handle_t> initial;
    std::vector<Type *> stateTypes;
    for (size_t j = 0; j < k; ++j) {
        const auto &expr   = gct->loopInitial(j);
        node_handle_t node = valueOf(functionValueOf(valueNodeOf(visit(expr)), expr));
        Type *type         = valueTypeOf(nodeTypeOf(node));
        const bool untyped = type == nullptr || type->code() == TypeCode::Void ||
                             (type->code() == TypeCode::Array &&
                              tt::as_ptr<ArrayType>(type)->elemType()->code() == TypeCode::Void);
        if (untyped) {
            diags_->of(SemanticDiag::LoopStateWithoutType)
                .atOrigin(expr->origin())
                .commit(gct->loopStateName(j), type ? type->toString() : std::string("void"));
            throw BuildAbortException();
        }
        initial.push_back(node);
        stateTypes.push_back(type);
    }
    node_handle_t sequence = nullptr;
    Type *elementType      = nullptr;
    if (hasIterable) {
        const auto &expr = gct->loopSequence();
        sequence         = valueOf(functionValueOf(valueNodeOf(visit(expr)), expr));
        Type *type       = valueTypeOf(nodeTypeOf(sequence));
        if (type == nullptr || type->code() != TypeCode::Array) {
            diags_->of(SemanticDiag::LoopSequenceNotArray)
                .atOrigin(expr->origin())
                .commit(sourceTextOf(expr), type ? type->toString() : std::string("void"));
            throw BuildAbortException();
        }
        elementType = tt::as_ptr<ArrayType>(type)->elemType();
    }
    Type *stateType =
        k == 1 ? stateTypes.front() : static_cast<Type *>(TupleType::create(stateTypes));

    // The two graphs, made before either is built so that every capture is made in both in
    // the same order (captureTwins_). They take the state (the body the element first) as
    // ordinary parameters; their captures become with-ports once they are built.
    const size_t ordinal        = ++loopCounts_[currGraph_.get()];
    const std::string graphBase = std::format("{}.loop{}", currGraph_->name(), ordinal);
    const auto makeLoopGraph    = [&](bool withElement, const char *role) {
        FunctionTypeFactory factory;
        if (withElement) {
            factory.addNormArg(gct->loopElementName(), elementType, false);
        }
        for (size_t j = 0; j < k; ++j) {
            factory.addNormArg(gct->loopStateName(j), stateTypes[j], false);
        }
        return createCompileGraph(
            factory.build(),
            currGraph_,
            std::format("{}.{}", graphBase, role));
    };
    compile_graph_ptr_t condGraph =
        camel::core::loopHasCondition(kind) ? makeLoopGraph(false, "cond") : nullptr;
    compile_graph_ptr_t bodyGraph = makeLoopGraph(hasIterable, "body");
    if (condGraph) {
        captureTwins_[condGraph.get()] = bodyGraph;
        captureTwins_[bodyGraph.get()] = condGraph;
    }

    if (condGraph) {
        buildLoopGraph(condGraph, gct->loopCondition());
        Type *condType = condGraph->funcType()->exitType();
        if (condType == nullptr || condType->code() != TypeCode::Bool) {
            diags_->of(SemanticDiag::LoopConditionNotBool)
                .atOrigin(gct->loopCondition()->origin())
                .commit(condType ? condType->toString() : std::string("void"));
            throw BuildAbortException();
        }
    }
    buildLoopGraph(bodyGraph, gct->loopBody());
    // The body gives the next state: of the state's type, or a value that enters it (an
    // `int` where the state is an `int?`, design principles 3.8).
    Type *bodyType = bodyGraph->funcType()->exitType();
    if (bodyType == nullptr || !stateType->assignableFrom(bodyType)) {
        if (bodyType != nullptr && entersImplicitly(stateType, bodyType)) {
            coerceArmExit(bodyGraph, stateType, gct->loopBody());
        } else {
            diags_->of(SemanticDiag::LoopBodyTypeMismatch)
                .atOrigin(gct->loopBody()->origin())
                .commit(
                    bodyType ? bodyType->toString() : std::string("void"),
                    stateType->toString());
            throw BuildAbortException();
        }
    }

    // Called by the LOOP node: the captures are with-ports, in the order both graphs share.
    for (const compile_graph_ptr_t &graph : {condGraph, bodyGraph}) {
        if (!graph) {
            continue;
        }
        const std::vector<draft_node_ref_t> closureNodes(
            graph->draft().closureNodes().begin(),
            graph->draft().closureNodes().end());
        for (draft_node_ref_t closureId : closureNodes) {
            graph->draft().removeNormPort(closureId);
            graph->draft().appendWithPort(closureId);
            graph->draft().removeClosureNode(closureId);
        }
        graph->setParameterized(true);
    }

    node_handle_t loopNode = currGraph_->addLoopNode(
        kind,
        DraftGraphBuilder::LoopTarget{condGraph, bodyGraph},
        stateType);
    registerNodeOrigin(context_, loopNode, gct, "gir.loop");
    stampPermissions(loopNode);
    if (sequence != nullptr) {
        linkNodes(LinkType::Norm, sequence, loopNode);
    }
    for (node_handle_t node : initial) {
        linkNodes(LinkType::Norm, node, loopNode);
    }
    // The captured values are the With inputs, linked by the link step in the graphs' order
    // (the condition graph has the same captures as the body graph).
    addCaptureSite(loopNode, bodyGraph, nullptr);

    // Running the loop is like calling its graphs (and entering an arm): it uses every
    // variable they captured and writes those they write.
    std::vector<CapturedUse> captured;
    for (const compile_graph_ptr_t &graph : {condGraph, bodyGraph}) {
        if (!graph) {
            continue;
        }
        for (const CapturedUse &use : capturedUsesOf(graph)) {
            auto known = std::ranges::find(captured, use.variable, &CapturedUse::variable);
            if (known == captured.end()) {
                captured.push_back(use);
            } else {
                known->written |= use.written;
            }
        }
    }
    std::vector<node_handle_t> capturedWrites;
    for (const CapturedUse &use : captured) {
        orderAfterLastWriter(use.variable, loopNode);
        if (use.written) {
            capturedWrites.push_back(use.origin);
        }
    }
    if (!capturedWrites.empty() && !waited_ && !synced_) {
        std::vector<DraftGraphBuilder *> graphs{bodyGraph.get()};
        if (condGraph) {
            graphs.push_back(condGraph.get());
        }
        unit_->pendingIgnoredWrites.push_back(
            {graphs, capturedWrites, gct->origin(), "The loop", "its body writes"});
    }
    for (const CapturedUse &use : captured) {
        if (use.written) {
            recordWrite(use.variable, loopNode, loopNode, true);
        } else {
            pendingUses_[use.variable].push_back({loopNode, false});
        }
    }

    if (synced_) {
        if (lastSyncedNode_ != nullptr && shouldAddSyncCtrlLink(lastSyncedNode_, loopNode)) {
            linkNodes(LinkType::Ctrl, lastSyncedNode_, loopNode);
        }
        lastSyncedNode_ = loopNode;
        chainedNodes_.insert(loopNode);
    }
    LEAVE("LOOP");
    return loopNode;
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
    ExpectedValueScope expectedScope(expectedValueType_, declaredExitTypeHere());
    // `return f`: the function value of a function named here. The name itself is not a
    // value, and left in place it reached the end of compilation as a DREF node.
    node_handle_t resNode = functionValueOf(valueNodeOf(visit(gct->at(0))), gct);
    if (genericDrefs_.contains(resNode)) {
        // `return id` under a declared function type: that type is the function wanted.
        resNode = genericValueFor(resNode, declaredExitTypeHere(), gct->at(0));
    }
    if (nodeIsKind(resNode, runtime::GCNodeKind::Dref)) {
        const auto &returned = gct->at(0);
        rejectNameWithoutValue(
            resNode,
            returned->type() == GCT::NodeKind::DREF ? returned->name()
                                                    : std::string("the returned name"),
            gct);
    }
    // A function that declares T? or T! returns its plain value boxed (see adaptExitValue).
    resNode                    = adaptExitValue(gct, resNode);
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
        // A return GATE must retain a CTRL input even when its value already depends on it.
        if (ctrlInput != nullptr && linkCheek(ctrlInput, outputAnchor) &&
            !runtime::hasControlDependency(
                *nodeDraftOf(outputAnchor),
                nodeIdOf(ctrlInput),
                nodeIdOf(outputAnchor))) {
            linkNodes(LinkType::Ctrl, ctrlInput, outputAnchor);
        }
    }
    checkDeclaredReturn(currGraph_, nodeTypeOf(resNode), gct->origin(), gct->at(0));
    if (!droppedGraphs_.contains(currGraph_.get())) {
        setGraphReturnFromResult(
            currGraph_,
            nodeIdOf(outputAnchor),
            nodeIdOf(resNode),
            nodeTypeOf(resNode));
    }

    LEAVE("EXIT");
    return resNode;
}

node_handle_t Builder::visitBodyNode(const GCT::node_ptr_t &gct) {
    const bool outerSynced        = std::exchange(synced_, false);
    const node_handle_t outerLast = std::exchange(lastSyncedNode_, {});
    node_handle_t result{};
    try {
        result = visitExecNode(gct);
        if (lastBlockStatement_.first != nullptr) {
            bodyValueStatement_[currGraph_.get()] = lastBlockStatement_;
        }
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
        .atOrigin(call->origin())
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
    // The permissions of this scope (they include those of the enclosing ones) are in force
    // for everything built in it, arms and lambdas included.
    NumericPermissionScope permissions(numericPermissions_, gct->numericPermissions());
    // The `using namespace` written in this block are in force in it (design principles 10.2).
    UsingScope usings(*this, GCT::Tree::of(gct)->usingsOf(gct));
    bool old                  = synced_;
    node_handle_t oldFuncNode = lastSyncedNode_;
    // A scope the lowering introduced (the operands of a composite literal, the names of a
    // declaration) is not a block: in a sync block it continues the chain, in written order,
    // like the arguments of a call statement. So does a sync block written inside a sync
    // block: its expressions are expressions of the enclosing block, in the same order.
    const bool operands = gct->operands() || (synced_ && gct->synced());
    if (!operands) {
        synced_         = gct->synced();
        lastSyncedNode_ = {};
    }
    registerGenericTemplates(gct);
    node_handle_t res{};
    // A statement in error is skipped and the following ones are still checked, so that one
    // compilation reports every independent error of the block. The builder is put back as
    // it was before the statement (an error may leave from inside a nested scope), and when
    // the block is done it fails as a whole: a block with a missing statement has no value
    // an enclosing expression could use, and a function with a missing statement must not
    // be analysed further (its dropped effects, its result).
    struct StatementState {
        Builder &self;
        compile_graph_ptr_t graph;
        node_scope_ptr_t nodeScope;
        graph_scope_ptr_t graphScope;
        decorated_scope_ptr_t decoratedScope;
        template_scope_ptr_t templateScope;
        type_bindings_t bindings;
        size_t depth;
        bool waited, synced, varied, declaresVariable, calleeAllowed;
        node_handle_t lastSynced;
        explicit StatementState(Builder &b)
            : self(b), graph(b.currGraph_), nodeScope(b.nodeScope_), graphScope(b.graphScope_),
              decoratedScope(b.decoratedScope_), templateScope(b.templateScope_),
              bindings(b.typeBindings_), depth(b.instantiationDepth_), waited(b.waited_),
              synced(b.synced_), varied(b.varied_), declaresVariable(b.declaresVariable_),
              calleeAllowed(b.genericCalleeAllowed_), lastSynced(b.lastSyncedNode_) {}
        void restore() {
            self.currGraph_            = graph;
            self.nodeScope_            = nodeScope;
            self.graphScope_           = graphScope;
            self.decoratedScope_       = decoratedScope;
            self.templateScope_        = templateScope;
            self.typeBindings_         = std::move(bindings);
            self.instantiationDepth_   = depth;
            self.waited_               = waited;
            self.synced_               = synced;
            self.varied_               = varied;
            self.declaresVariable_     = declaresVariable;
            self.genericCalleeAllowed_ = calleeAllowed;
            self.lastSyncedNode_       = lastSynced;
        }
    };
    bool failed                     = false;
    const GCT::Node *outerStatement = unusedStatement_;
    // A block that is itself a statement whose value nothing takes drops the value of its last
    // statement too.
    const bool valueDropped = gct == unusedStatement_ || droppedBodies_.contains(gct);
    std::pair<GCT::node_ptr_t, node_handle_t> lastStatement{};
    for (size_t i = 0; i < gct->size(); i++) {
        StatementState before(*this);
        try {
            const auto &child = gct->at(i);
            // Every statement but the last is one whose value nothing takes, and the last one
            // too when the block's value is dropped. The scopes the lowering introduced
            // (operands of a literal, the names of a declaration) have no statements; a sync
            // block inside a sync block continues the chain but has statements of its own.
            const bool unused = !gct->operands() && (i + 1 < gct->size() || valueDropped);
            unusedStatement_  = unused ? child : nullptr;
            any result        = visit(child);
            unusedStatement_  = nullptr;
            if (unused && result.has_value() && result.type() == typeid(node_handle_t)) {
                rejectUnusedPureResult(child, any_cast<node_handle_t>(result));
            }
            if (result.has_value() && result.type() == typeid(node_handle_t)) {
                res = any_cast<node_handle_t>(result);
                if (!gct->operands() && i + 1 == gct->size()) {
                    lastStatement = {child, res};
                }
            } else if (
                result.type() == typeid(graph_ptr_t) && child->type() == GCT::NodeKind::FUNC &&
                child->name().empty()) {
                // A function literal is a value like any other expression, e.g. a branch arm.
                res = valueNodeOf(result);
            }
        } catch (const BuildAbortException &e) {
            before.restore();
            unusedStatement_ = nullptr;
            failed           = true;
            continue;
        }
    }
    unusedStatement_    = outerStatement;
    lastBlockStatement_ = lastStatement;
    if (failed) {
        throw BuildAbortException();
    }
    if (gct->size() > 0 && res != nullptr && nodeIsKind(res, runtime::GCNodeKind::Dref) &&
        gct->at(gct->size() - 1)->type() == GCT::NodeKind::DREF && sameGraph(res, currGraph_)) {
        // A block that ends in the name of a function has that function's value: the value
        // of a branch arm (`if c then inc else dec`), of a block, of a function body. Left a
        // name, it gave the block the type void.
        const auto &last = gct->at(gct->size() - 1);
        res              = namedValueOf(res, last->name(), last);
    }
    if (!operands) {
        // A sync block that is not part of an enclosing chain: whoever takes its value takes
        // it after the block's last expression (a `return` has already done this for the
        // graph's exit). Without this the chain of a block ending in a plain value was not
        // reachable from anything and the block's effects were dropped.
        const bool returned =
            gct->size() > 0 && gct->at(gct->size() - 1)->type() == GCT::NodeKind::EXIT;
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

void Builder::exportDeclarations(const GCT::node_ptr_t &gct, bool decorated) {
    // Whether a top-level function of this name carries decorators: its exported form is the
    // decorated function, which exists once its body is built (the second call).
    const auto isDecorated = [&](const std::string &name) {
        for (const auto &declaration : unit_->declarations) {
            if (declaration->graph == nullptr || declaration->names.front() != name) {
                continue;
            }
            if (!appliedDecoratorsOf(declaration->gct).empty()) {
                return true;
            }
        }
        return false;
    };
    bool failed = false;
    for (size_t i = 0; i < gct->exportCount(); ++i) {
        const Reference ref(gct->exportName(i));
        const std::string name = ref.toString();
        if (isDecorated(name) != decorated) {
            continue;
        }
        if (ref.plain() && camel::core::isOverloadableOperatorGroup(ref.ident())) {
            // `export func operator +`: every module that imports this one receives its
            // overloads (Module::importedOperatorOverloads).
            exportOperatorOverloads(ref.ident(), gct);
            continue;
        }
        if (decorated) {
            auto optDecorated = decoratedGraphAt(name);
            if (!optDecorated.has_value()) {
                continue; // its body is in error, which was reported
            }
            if (overloadsOfDecorated(name, *optDecorated) != nullptr) {
                // An exported name is one decorated function or a set of plain ones; the set
                // of both has no exported form yet. Exporting the decorated one alone dropped
                // the others without a word.
                diags_->of(SemanticDiag::ConstructNotSupported)
                    .atOrigin(gct->origin())
                    .commit(name, "a decorated function that has overloads cannot be exported yet");
                failed = true;
                continue;
            }
            module_->exportEntity(
                ref,
                camel::core::module::detail::EntityAccess::makeDecoratedGraph(
                    eraseCompileGraph(optDecorated.value())));
            continue;
        }
        if (auto optType = unit_->typeDecls.find(ref); optType != unit_->typeDecls.end()) {
            module_->exportType(ref, optType->second);
            continue;
        }
        if (auto value = unit_->values.find(name); value != unit_->values.end()) {
            TopLevelDeclaration &declaration = *value->second;
            if (declaration.isVar) {
                // Design principles 10.1: a `var` of the top level is written by the module's
                // own functions only, which are exported instead.
                camel::source::origin_id_t origin = gct->origin();
                for (const auto &child : *declaration.gct) {
                    if (child->type() == GCT::NodeKind::NREF && child->name() == name) {
                        origin = child->origin();
                    }
                }
                if (declaration.gct->type() == GCT::NodeKind::NREF) {
                    origin = declaration.gct->origin();
                }
                diags_->of(SemanticDiag::ExportedVariable).atOrigin(origin).commit(name);
                failed = true;
                continue;
            }
            // A value of the top level is a node of the root graph, which every module of the
            // program builds into: the importing module captures that very node, made when
            // a module first uses it (exportedValueNode).
            auto exported         = std::make_unique<ExportedValue>();
            exported->name        = name;
            exported->declaration = &declaration;
            module_->exportEntity(
                ref,
                camel::core::module::detail::EntityAccess::makeNode(exported.get()));
            unit_->exportedValues.push_back(std::move(exported));
            continue;
        }
        if (auto optGraph = graphsAt(name); optGraph.has_value()) {
            module_->exportEntity(
                ref,
                camel::core::module::detail::EntityAccess::makeGraphSet(
                    eraseCompileGraphSet(optGraph.value())));
            continue;
        }
        if (auto optTemplate = templateScope_->get(name); optTemplate.has_value()) {
            // A generic function has no graph to export: the template is exported, and there
            // is one instance per set of types in the program (instantiateGeneric).
            module_->exportEntity(
                ref,
                camel::core::module::detail::EntityAccess::makeGenericTemplate(
                    std::static_pointer_cast<void>(optTemplate.value())));
            continue;
        }
        if (isPoisonedName(name)) {
            failed = true;
            continue;
        }
        diags_->of(SemanticDiag::UnresolvedReference).atOrigin(gct->origin()).commit(name);
        failed = true;
    }
    if (failed) {
        throw BuildAbortException();
    }
}

} // namespace camel::compile::gir
