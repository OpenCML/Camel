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
 * Updated: May. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "builder.h"

#include "camel/compile/gir/static_function.h"
#include "camel/runtime/graph.h"
#include "camel/utils/log.h"
#include "camel/utils/scope.h"
#include "camel/utils/str.h"
#include "camel/utils/type.h"
#include "core/module/entity_internal.h"

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
    return std::format("cgraph:{}:{}", name.empty() ? "anonymous" : name, seq++);
}

compile_graph_ptr_t createCompileGraph(
    FunctionType *funcType, const compile_graph_ptr_t &outer, std::string name = "") {
    auto graph = std::make_shared<DraftGraphBuilder>(funcType ? funcType : FunctionType::create());
    if (name.empty()) {
        name = std::format("__graph_{}", makeCompileGraphStableId("anon"));
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
                                             : std::format("__with{}", i);
            graph->addPortNode(funcType->withTypeAt(i), portName, true, funcType->withIsVarAt(i));
        }
        for (size_t i = 0; i < funcType->normTypesCount(); ++i) {
            const size_t argIndex      = funcType->withTypesCount() + i;
            const std::string portName = argIndex < funcType->argNamesCount()
                                             ? std::string(funcType->argNameAt(argIndex))
                                             : std::format("__arg{}", i);
            graph->addPortNode(funcType->normTypeAt(i), portName, false, funcType->normIsVarAt(i));
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

        compile_graph_ptr_t entryGraph = nullptr;
        if (auto decoratedMain = decoratedGraphAt("main")) {
            entryGraph = *decoratedMain;
        } else if (auto gv = graphsAt("main"); gv.has_value() && !gv.value()->empty()) {
            entryGraph = gv.value()->front();
        }
        const bool entryModule =
            context_ && module_ && context_->mainModule() && context_->mainModule() == module_;
        if (entryGraph) {
            auto entryFuncValue = createFuncDataNode(entryGraph, true, false);
            auto entryCall      = rootGraph_->addCallNode(entryGraph->funcType()->exitType());
            linkNodes(LinkType::With, entryFuncValue, entryCall);
            setGraphReturnFromResult(
                rootGraph_,
                nodeIdOf(entryCall),
                nodeIdOf(entryCall),
                entryGraph->funcType()->exitType());
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

        auto *portNode = curr->addPortNode(nodeTypeOf(node), name, false, false);
        curr->draft().removeNormPort(nodeIdOf(portNode));
        curr->draft().appendClosureNode(nodeIdOf(portNode));
        const auto closureRefs = curr->funcType()->closureRefs();
        if (std::find(closureRefs.begin(), closureRefs.end(), name) == closureRefs.end()) {
            curr->funcType()->addClosureRef(name);
        }
        scope->insert(name, portNode);

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
    const auto &declLoad = gct->loadAs<GCT::DeclLoad>();
    if (!declLoad->isFunc()) {
        LEAVE("DECL");
        return nullptr;
    }

    GCT::node_ptr_t typeNode = gct->atAs<GCT::TypeLoad>(0);
    Type *type               = typeNode->loadAs<GCT::TypeLoad>()->dataType();
    FunctionType *funcType   = tt::as_ptr<FunctionType>(type);

    compile_graph_ptr_t graph = enterScope(funcType, declLoad->ref().ident());
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
        for (draft_node_ref_t port : graph->draft().withPorts()) {
            insertNode(graph->nodePortName(graph->draft().node(port)), graph->draft().node(port));
        }
        for (draft_node_ref_t port : graph->draft().normPorts()) {
            insertNode(graph->nodePortName(graph->draft().node(port)), graph->draft().node(port));
        }
        node_handle_t res = visitExecNode(gct->atAs<GCT::ExecLoad>(1));
        if (graph->draft().exitNode() == runtime::kInvalidNodeRef) {
            if (res != nullptr) {
                setGraphReturnFromResult(graph, nodeIdOf(res), nodeIdOf(res), nodeTypeOf(res));
            } else {
                auto *resNode = graph->addStaticDataNode(Data::null());
                setGraphReturnFromResult(graph, nodeIdOf(resNode), nodeIdOf(resNode), Type::Void());
            }
        }
    } catch (...) {
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
                const auto &refNode = resolveNodeByRef(std::string(ref));
                refTypes.push_back(nodeTypeOf(refNode));
                refNodes.push_back(refNode);
            }
            ASSERT(
                holes.size() == refNodes.size(),
                "Composite holes must match unresolved reference count.");
            auto filledType = dataType->resolved()
                                  ? dataType
                                  : tt::as_ptr<CompositeType>(dataType->clone())->resolve(refTypes);
            auto payload    = runtime::makeFillPayload(fillKindForType(filledType), holes);
            node_handle_t srcNode = currGraph_->addStaticDataNode(data, filledType);
            node                  = currGraph_->addFillNode(filledType, payload);
            linkNodes(LinkType::Norm, srcNode, node);
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
    const auto &res     = visit(gct->at(0));
    ASSERT(
        res.type() == typeid(node_handle_t),
        "Unexpected result type from Enter the child of NREF node.");
    node_handle_t node = any_cast<node_handle_t>(res);
    bool success       = insertNode(ident, node);
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
    auto *inputGraph    = nodeGraphOf(input);
    auto *modifierGraph = nodeGraphOf(modifier);
    ASSERT(
        inputGraph == modifierGraph,
        "Modifier tracking must stay within a single compile graph.");
    nodeModifierMaps_[inputGraph][nodeIdOf(input)] = nodeIdOf(modifier);
}

node_handle_t Builder::visitDRefNode(const GCT::node_ptr_t &gct) {
    ENTER("DREF");
    const string &name = gct->loadAs<GCT::DRefLoad>()->ref();
    auto optNode       = nodeAt(name);
    if (optNode.has_value()) {
        node_handle_t node = optNode.value();
        if (!sameGraph(node, currGraph_)) {
            node = resolveCrossGraphRef(node, name);
            ASSERT(sameGraph(node, currGraph_), "Failed to resolve cross-graph reference.");
        }
        LEAVE("DREF");
        return node;
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
    if (targetType->castSafetyFrom(sourceType) != CastSafety::Safe) {
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
    bool old        = varied_;
    varied_         = true;
    const auto &res = visit(gct->at(0));
    ASSERT(
        res.type() == typeid(node_handle_t),
        "Unexpected result type from Enter the child of VARI node.");
    node_handle_t node = any_cast<node_handle_t>(res);
    varied_            = old;
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

    const auto &withNode = annoNode->atAs<GCT::WithLoad>(0);

    const std::string tmpScopeName = "__decorator_tmp_scope_" + std::to_string(syntheticRefIndex_);
    const std::string tmpRefName   = "__decorator_tmp_ref_" + std::to_string(syntheticRefIndex_++);
    nodeScope_                     = nodeScope_->enter(tmpScopeName);
    nodeScope_->insert(tmpRefName, funcValueNode);

    GCT::node_ptr_t linkNode = std::make_shared<GCT::Node>(
        std::dynamic_pointer_cast<GCT::Load>(std::make_shared<GCT::LinkLoad>(1)));
    *linkNode << withNode->clone();
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

    graph_ptr_t decoratedGraph = enterScope(
        FunctionType::create(),
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
    node_vec_t withInputNodes, normInputNodes;
    type_vec_t withInputTypes, normInputTypes;

    for (draft_node_ref_t inputId : withInputsOf(targetNode)) {
        node_handle_t inputNode = nodeDraftOf(targetNode)->node(inputId);
        withInputNodes.push_back(inputNode);
        withInputTypes.push_back(nodeTypeOf(inputNode));
    }

    auto callableValueType = [&](node_handle_t inputNode) -> Type * {
        if (nodeIsKind(inputNode, runtime::GCNodeKind::Func)) {
            auto callee = nodeGraphOf(inputNode)->funcTarget(inputNode);
            ASSERT(callee != nullptr, "Compile FUNC node target is null.");
            return callee->funcType()->exitType();
        }
        if (nodeIsKind(inputNode, runtime::GCNodeKind::Oper)) {
            return nodeGraphOf(inputNode)->operTarget(inputNode)->funcType()->exitType();
        }
        return nodeTypeOf(inputNode);
    };

    auto lowerDecoratedGraphValue =
        [&](const compile_graph_ptr_t &decoratedGraph) -> node_handle_t {
        currGraph_->addDependencyGraph(decoratedGraph);
        if (!decoratedGraph->funcType()->hasExitType() ||
            decoratedGraph->funcType()->exitType()->code() != TypeCode::Function) {
            diags_->of(SemanticDiag::ArgumentsMismatch)
                .atOrigin(gct->load()->origin())
                .commit(
                    "decorated graph should return function",
                    decoratedGraph->funcType()->toString());
            throw BuildAbortException();
        }
        node_handle_t decoratorFactoryValue = createFuncDataNode(decoratedGraph, true, false);
        node_handle_t decoratorInvoke =
            currGraph_->addCallNode(decoratedGraph->funcType()->exitType());
        linkNodes(LinkType::With, decoratorFactoryValue, decoratorInvoke);
        return decoratorInvoke;
    };

    for (size_t i = 1; i < gct->size(); i++) {
        any dataRes = visit(gct->at(i));
        if (dataRes.type() == typeid(graph_ptr_t)) {
            graph_ptr_t inputGraph = any_cast<graph_ptr_t>(dataRes);
            currGraph_->addDependencyGraph(inputGraph);
            auto inputNode = createFuncDataNode(inputGraph, true, false);
            normInputNodes.push_back(inputNode);
            normInputTypes.push_back(nodeTypeOf(inputNode));
        } else if (dataRes.type() == typeid(node_handle_t)) {
            node_handle_t inputNode = any_cast<node_handle_t>(dataRes);
            if (nodeIsKind(inputNode, runtime::GCNodeKind::Dref)) {
                const auto &target = nodeGraphOf(inputNode)->drefTarget(inputNode);
                if (std::holds_alternative<graph_ptr_t>(target)) {
                    inputNode = lowerDecoratedGraphValue(std::get<graph_ptr_t>(target));
                }
            }
            normInputNodes.push_back(inputNode);
            normInputTypes.push_back(callableValueType(inputNode));
        } else {
            ASSERT(false, std::format("Unexpected result type from the {} child of LINK node", i));
        }
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
            const auto &res = ops->resolve(withInputTypes, normInputTypes, Modifier::None);
            if (!res.has_value()) {
                std::string argTypesStr = std::format(
                    "<{}> ({})",
                    strutil::join(withInputTypes, ", ", [](Type *t) { return t->toString(); }),
                    strutil::join(normInputTypes, ", ", [](Type *t) { return t->toString(); }));
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
            targetNode     = currGraph_->addOperNode(targetOperator, 0);
            registerNodeOrigin(context_, targetNode, gct->at(0), "gir.link.oper");
            targetFuncType = targetOperator->funcType();
        } else if (std::holds_alternative<graph_ptr_t>(drefTarget)) {
            auto decoratedGraph           = std::get<graph_ptr_t>(drefTarget);
            node_handle_t decoratorInvoke = lowerDecoratedGraphValue(decoratedGraph);
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

    std::string targetName = targetGraph
                                 ? targetGraph->name()
                                 : (targetOperator ? "<" + targetOperator->name() + ">" : "");

    for (size_t i = 0; i < withInputNodes.size(); i++) {
        node_handle_t inputNode = withInputNodes[i];
        bool isVar              = (i < targetFuncType->withTypesCount()) &&
                                  targetFuncType->withIsVarAt(static_cast<size_t>(i));
        tryRemoveCtrlLink(inputNode, targetNode);
        linkNodes(LinkType::With, inputNode, targetNode);
        if (auto modifierNode = modifierOf(inputNode); modifierNode.has_value()) {
            if (sameGraph(*modifierNode, currGraph_) && linkCheek(*modifierNode, targetNode)) {
                linkNodes(LinkType::Ctrl, *modifierNode, targetNode);
            }
        }
        if (isVar) {
            if (!waited_ && !synced_) {
                diags_->of(SemanticDiag::IgnoredSideEffect)
                    .atOrigin(gct->load()->origin())
                    .commit(targetName + ": " + targetFuncType->toString());
            }
            setModifier(inputNode, targetNode);
        }
    }

    for (size_t i = 0; i < normInputNodes.size(); i++) {
        node_handle_t inputNode = normInputNodes[i];
        bool isVar = i < targetFuncType->normTypesCount() && targetFuncType->normIsVarAt(i);
        tryRemoveCtrlLink(inputNode, targetNode);
        linkNodes(LinkType::Norm, inputNode, targetNode);
        if (auto modifierNode = modifierOf(inputNode); modifierNode.has_value()) {
            if (sameGraph(*modifierNode, currGraph_) && linkCheek(*modifierNode, targetNode)) {
                linkNodes(LinkType::Ctrl, *modifierNode, targetNode);
            }
        }
        if (isVar) {
            if (!waited_ && !synced_) {
                diags_->of(SemanticDiag::IgnoredSideEffect)
                    .atOrigin(gct->load()->origin())
                    .commit(targetName + ": " + targetFuncType->toString());
            }
            setModifier(inputNode, targetNode);
        }
    }

    if (synced_) {
        if (lastSyncedNode_ != nullptr && linkCheek(lastSyncedNode_, targetNode)) {
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
    vector<node_handle_t> inputs;
    for (size_t i = 1; i < gct->size(); i++) {
        any dataRes = visit(gct->at(i));
        if (dataRes.type() == typeid(graph_ptr_t)) {
            graph_ptr_t subGraph = any_cast<graph_ptr_t>(dataRes);
            currGraph_->addDependencyGraph(subGraph);
            auto inputNode = createFuncDataNode(subGraph, true, false);
            inputs.push_back(inputNode);
        } else if (dataRes.type() == typeid(node_handle_t)) {
            inputs.push_back(any_cast<node_handle_t>(dataRes));
        } else {
            ASSERT(false, std::format("Unexpected result type from the {} child of WITH node", i));
        }
    }
    for (node_handle_t inputNode : inputs) {
        tryRemoveCtrlLink(inputNode, targetNode);
        linkNodes(LinkType::With, inputNode, targetNode);
    }
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
        elemType              = tupleType->typeAt(accsLoad->index<size_t>());
        break;
    }
    case TypeCode::Array: {
        const auto *arrTy = tt::as_ptr<camel::core::type::ArrayType>(tgtType);
        elemType          = arrTy->elemType();
        break;
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
        node_handle_t resNode = visitExecNode(caseExecNode);
        if (subGraph->draft().exitNode() == runtime::kInvalidNodeRef) {
            if (resNode != nullptr) {
                setGraphReturnFromResult(
                    subGraph,
                    nodeIdOf(resNode),
                    nodeIdOf(resNode),
                    nodeTypeOf(resNode));
            } else {
                node_handle_t nullNode = subGraph->addStaticDataNode(Data::null());
                setGraphReturnFromResult(
                    subGraph,
                    nodeIdOf(nullNode),
                    nodeIdOf(nullNode),
                    Type::Void());
            }
        }
        leaveScope();

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
        branchArms.push_back(
            runtime::GCBranchArm{
                .head = nodeIdOf(funcNode),
                .tail = nodeIdOf(funcNode),
            });

        if (joinType == nullptr) {
            joinType = exitType;
            setNodeType(joinNode, joinType);
        } else {
            if (!exitType->equals(joinType)) {
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

    if (synced_) {
        if (lastSyncedNode_ != nullptr && linkCheek(lastSyncedNode_, brchNode)) {
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
    const auto &res = visit(gct->at(0));
    if (res.type() == typeid(node_handle_t)) {
        LEAVE("ANNO");
        return any_cast<node_handle_t>(res);
    }
    if (res.type() == typeid(graph_ptr_t)) {
        graph_ptr_t graph = any_cast<graph_ptr_t>(res);
        currGraph_->addDependencyGraph(graph);
        LEAVE("ANNO");
        return createFuncDataNode(graph, true, false);
    }
    ASSERT(false, "Unexpected child result type in ANNO node.");
    LEAVE("ANNO");
    return {};
}

node_handle_t Builder::visitExitNode(const GCT::node_ptr_t &gct) {
    ENTER("EXIT");
    auto res = visit(gct->at(0));
    node_handle_t resNode{};
    if (res.type() == typeid(node_handle_t)) {
        resNode = any_cast<node_handle_t>(res);
    } else if (res.type() == typeid(graph_ptr_t)) {
        graph_ptr_t subGraph = any_cast<graph_ptr_t>(res);
        currGraph_->addDependencyGraph(subGraph);
        resNode = createFuncDataNode(subGraph, true, false);
    } else {
        ASSERT(false, "Unexpected result type from Enter child of EXIT node.");
    }
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
    setGraphReturnFromResult(
        currGraph_,
        nodeIdOf(outputAnchor),
        nodeIdOf(resNode),
        nodeTypeOf(resNode));

    LEAVE("EXIT");
    return resNode;
}

node_handle_t Builder::visitExecNode(const GCT::node_ptr_t &gct) {
    ENTER("EXEC");
    const auto &execLoad      = gct->loadAs<GCT::ExecLoad>();
    bool old                  = synced_;
    node_handle_t oldFuncNode = lastSyncedNode_;
    synced_                   = execLoad->synced();
    lastSyncedNode_           = {};
    node_handle_t res{};
    for (size_t i = 0; i < gct->size(); i++) {
        try {
            any result = visit(gct->at(i));
            if (result.has_value() && result.type() == typeid(node_handle_t)) {
                res = any_cast<node_handle_t>(result);
            }
        } catch (const BuildAbortException &e) {
            continue;
        }
    }
    synced_         = old;
    lastSyncedNode_ = oldFuncNode;
    LEAVE("EXEC");
    return res;
}

void_ptr_t Builder::visitExptNode(const GCT::node_ptr_t &gct) {
    ENTER("EXPT");
    const auto &exptLoad = gct->loadAs<GCT::ExptLoad>();
    const auto &exports  = exptLoad->exports();
    for (const Reference &ref : exports) {
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
