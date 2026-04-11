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
 * Updated: Apr. 11, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "builder.h"

#include "camel/compile/gir/static_function.h"
#include "camel/utils/log.h"
#include "camel/utils/scope.h"
#include "camel/utils/str.h"
#include "camel/utils/type.h"

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

camel::core::mm::IAllocator &staticFunctionAllocator(Graph &ownerGraph) {
    return ownerGraph.arena()->allocator();
}

TupleType *currentClosureTupleType(const graph_ptr_t &graph) {
    type_vec_t closureTypes;
    closureTypes.reserve(graph->closure().size());
    for (Node *node : graph->closure()) {
        closureTypes.push_back(node->dataType());
    }
    return TupleType::create(std::move(closureTypes));
}

StaticFunction *createStaticFunction(Graph &ownerGraph, const graph_ptr_t &targetGraph) {
    ASSERT(
        targetGraph != nullptr,
        "Target graph is null when materializing compile-time function.");
    return StaticFunction::create(
        targetGraph.get(),
        currentClosureTupleType(targetGraph),
        staticFunctionAllocator(ownerGraph));
}
} // namespace

inline void tryRemoveCtrlLink(Node *from, Node *to) {
    // if from has already linked to to by a ctrl link, remove it first
    // sometimes we may need to change a ctrl link (linked before) to a data link
    // because data link has higher priority than ctrl link
    // and we don't want to have duplicate links
    (void)detail::NodeMutation::unlinkCtrl(from, to);
}

inline bool linkCheek(Node *from, Node *to) {
    // prevent linking a node to itself
    if (from == to) {
        return false;
    }
    // prevent linking nodes that are already linked
    if (from->hasLinkedTo(to)) {
        return false;
    }
    // prevent linking nodes that are already deeply linked reversely
    // which may cause cycles in the graph
    // note: this check can be expensive
    if (to->hasDeepLinkedTo(from)) {
        CAMEL_LOG_WARN_S(
            "GIR",
            "Prevent linking deeply linked nodes: {} -> {}",
            from->toString(),
            to->toString());
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
    const context_ptr_t &context, const graph_ptr_t &graph, const GCT::node_ptr_t &gct,
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
    const context_ptr_t &context, Node *node, const GCT::node_ptr_t &gct,
    const std::string &label = "gir.node", std::vector<camel::source::SemanticPart> extraParts = {},
    std::vector<camel::source::origin_id_t> mergedInputs = {}, bool synthetic = false,
    const std::string &syntheticReason = "") {
    if (!context || !node) {
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
            node,
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
    camel::source::origin_id_t graphOrigin, const graph_ptr_t &graph, Node *node) {
    camel::source::SourceContext *sc = sourceContext.get();
    if (!sc || graphOrigin == camel::source::kInvalidOriginId || !node) {
        return;
    }
    if (const auto *graphSemantic = sc->girGraphSemantic(graph->stableId())) {
        sc->bindGirNodeDraftDebug(node, graphOrigin, *graphSemantic);
    } else {
        camel::source::SemanticBundle bundle;
        bundle.mainOrigin = graphOrigin;
        sc->bindGirNodeDraftDebug(node, graphOrigin, std::move(bundle));
    }
}

// Build GIR graphs from GCT. Compilation now keeps Graph mutable all the way
// until runtime materialization. The compiler only constructs the graph tree
// here; derived layout metadata is refreshed later when the runtime graph is
// encoded.
graph_ptr_t Builder::build(GCT::node_ptr_t &gct, diagnostics_ptr_t diags) {
    waited_ = false;
    synced_ = false;
    varied_ = false;
    diags_  = diags;
    usedGraphs_.clear();
    syntheticRefIndex_ = 0;

    nodeScope_      = node_scope_t::create();
    graphScope_     = graph_scope_t::create();
    decoratedScope_ = decorated_scope_t::create();
    rootGraph_      = Graph::create(FunctionType::create(), nullptr, "__root__");
    if (auto sourceContext = context_ ? context_->sourceContext() : nullptr) {
        rootGraph_->setExtra<camel::source::SourceContext, kSourceContextExtraIndex>(
            sourceContext.get());
    }
    currGraph_ = rootGraph_;

    try {
        visit(gct);

        auto optMainGraph      = rootGraph_->getSubGraphsByName("main");
        graph_ptr_t entryGraph = nullptr;
        if (optMainGraph.has_value()) {
            auto mainGraphSet = optMainGraph.value();
            ASSERT(!mainGraphSet.empty(), "Main graph set is empty.");
            ASSERT(mainGraphSet.size() == 1, "Multiple main graphs found.");
            entryGraph = *mainGraphSet.begin();
        } else if (auto decoratedMain = decoratedGraphAt("main")) {
            entryGraph = *decoratedMain;
        } else if (auto gv = graphsAt("main"); gv.has_value() && !gv.value()->empty()) {
            entryGraph = gv.value()->front();
        }
        const bool entryModule =
            context_ && module_ && context_->mainModule() && context_->mainModule() == module_;
        if (entryGraph) {
            auto funcNode = createFuncDataNode(entryGraph, false, false);
            rootGraph_->setOutput(funcNode);
        } else if (entryModule) {
            diags_->of(SemanticDiag::EntryModuleMissingMain).commit(module_->name());
            throw BuildAbortException();
        } else {
            // Non-entry library modules may omit main: keep a placeholder output
            // so the graph remains encodable.
            auto zero               = std::make_shared<LongData>(static_cast<int64_t>(0));
            Node *const placeholder = DataNode::create(*rootGraph_, zero);
            rootGraph_->setOutput(placeholder);
        }
    } catch (Diagnostic &d) {
        diags_->add(std::move(d));
        rootGraph_ = nullptr;
    } catch (const BuildAbortException &) {
        rootGraph_ = nullptr;
    }

    return rootGraph_;
}

graph_ptr_t Builder::enterScope(FunctionType *funcType, const std::string &name) {
    if (name.empty()) {
        currGraph_ = Graph::create(funcType, currGraph_);
    } else {
        auto graphs = graphScope_->get(name);
        if (graphs.has_value() && !graphs.value()->empty()) {
            currGraph_ = graphs.value()->front();
        } else {
            currGraph_ = Graph::create(funcType, currGraph_, name);
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
    currGraph_      = currGraph_->outer();
}

bool Builder::insertNode(const std::string &name, Node *node) {
    if (nodeScope_->has(name, false)) {
        return false;
    }
    nodeScope_->insert(name, node);
    return true;
}

bool Builder::insertGraph(const std::string &name, const graph_ptr_t &graph) {
    if (graphScope_->has(name, false)) {
        auto graphs = graphScope_->get(name).value();
        // TODO: check if the graph is already in the list
        graphs->push_back(graph);
    }
    graphScope_->insert(name, std::make_shared<graph_vec_t>(1, graph));
    return true;
}

bool Builder::insertDecoratedGraph(const std::string &name, const graph_ptr_t &graph) {
    if (!decoratedScope_) {
        return false;
    }
    decoratedScope_->insert(name, graph);
    return true;
}

// Cross-graph reference resolution: when the current function graph references
// nodes from an outer graph, insert PortNode hop by hop along the outer graph
// chain as closure capture ports.
// This mutates the closure sets on the path during initial compilation while
// Graph is still the mutable compile-time carrier.
Node *Builder::resolveCrossGraphRef(Node *node, const std::string &name) {
    Graph *curr            = currGraph_.get();
    node_scope_ptr_t scope = nodeScope_;

    while (curr != &node->graph()) {
        // Add Port nodes to every intermediate graph on the path.
        if (usedGraphs_.find(curr) != usedGraphs_.end()) {
            // Do not add closure captures to a graph that has already been used.
            diags_->of(SemanticDiag::ClosureCaptureAfterSelfCall).commit(name, curr->name());
            throw BuildAbortException();
        }

        // Insert a Port node.
        Node *port = PortNode::create(*curr, node->dataType(), name, false);
        curr->addClosure(port);
        scope->insert(name, port);

        // Continue walking toward the outer graph and scope.
        curr  = curr->outer().get();
        scope = scope->outer();
    }

    // Reacquire the updated node reference.
    return *nodeScope_->get(name);
}

Node *Builder::resolveNodeByRef(const std::string &name) {
    auto optSrcNode = nodeAt(name);
    if (!optSrcNode.has_value()) {
        EXEC_WHEN_DEBUG({ nodeScope_->dump(std::cerr, 0); });
        auto sourceContext = context_ ? context_->sourceContext() : nullptr;
        auto origin        = (sourceContext && currGraph_)
                                 ? sourceContext->debugMap().graphOrigin(currGraph_->stableId())
                                 : camel::source::kInvalidOriginId;
        diags_->of(SemanticDiag::UnresolvedReference).atOrigin(origin).commit(name);
        throw BuildAbortException();
    }
    Node *node = optSrcNode.value();

    // Handle closure capture for cross-graph references.
    if (node->graph() != *currGraph_) {
        node = resolveCrossGraphRef(node, name);
        ASSERT(node->graph() == *currGraph_, "Failed to resolve cross-graph reference.");
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

    graph_ptr_t graph = enterScope(funcType, declLoad->ref().ident());
    leaveScope();

    LEAVE("DECL");
    return nullptr;
}

graph_ptr_t Builder::visitFuncNode(const GCT::node_ptr_t &gct) {
    ENTER("FUNC");
    std::string name         = gct->loadAs<GCT::FuncLoad>()->name();
    GCT::node_ptr_t typeLoad = gct->atAs<GCT::TypeLoad>(0);
    Type *type               = typeLoad->loadAs<GCT::TypeLoad>()->dataType();
    FunctionType *funcType   = tt::as_ptr<FunctionType>(type);
    graph_ptr_t graph        = enterScope(funcType, name);
    // Module-level visitExecNode swallows BuildAbortException and continues.
    // If the function body throws before leaveScope, currGraph_ stays stuck on a
    // subgraph and later functions (such as main) get attached to the wrong
    // outer graph, leaving root without "main" and __root__ without exit. Pair
    // leaveScope on every exception path.
    try {
        registerGraphOrigin(context_, graph, gct, "gir.func.graph");
        for (const auto &port : graph->withPorts()) {
            const auto &portNode = tt::as_ptr<PortNode>(port);
            insertNode(portNode->name(), port);
        }
        for (const auto &port : graph->normPorts()) {
            const auto &portNode = tt::as_ptr<PortNode>(port);
            insertNode(portNode->name(), port);
        }
        Node *res = visitExecNode(gct->atAs<GCT::ExecLoad>(1));
        if (graph->exitNode_ == nullptr) {
            if (res) {
                graph->setOutput(res);
            } else {
                // function with no return value, setting null by default
                Node *resNode = DataNode::create(*graph, Data::null());
                graph->setOutput(resNode);
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

Node *Builder::visitDataNode(const GCT::node_ptr_t &gct) {
    ENTER("DATA");
    const auto &dataLoad   = gct->loadAs<GCT::DataLoad>();
    const data_ptr_t &data = dataLoad->data();

    Node *node        = nullptr;
    TypeCode dataType = data->type()->code();
    if (isComposite(dataType)) {
        auto composedData = tt::as_shared<CompositeData>(data);
        if (!composedData->resolved()) {
            Node *srcNode        = DataNode::create(*currGraph_, data);
            const auto &dataType = tt::as_ptr<CompositeType>(data->type());
            type_vec_t refTypes;
            node_vec_t refNodes;
            for (const auto &ref : composedData->refs()) {
                const auto &refNode = resolveNodeByRef(std::string(ref));
                refTypes.push_back(refNode->dataType());
                refNodes.push_back(refNode);
            }
            auto fillType   = tt::as_ptr<CompositeType>(dataType->clone());
            auto filledType = fillType->resolve(refTypes);
            node            = FillNode::create(*currGraph_, filledType);
            Node::link(LinkType::Norm, srcNode, node);
            std::vector<camel::source::origin_id_t> mergedInputs;
            for (const auto &refNode : refNodes) {
                Node::link(LinkType::With, refNode, node);
                if (auto *sourceContext = context_ ? context_->sourceContext().get() : nullptr) {
                    auto origin = sourceContext->resolveGirNodeOrigin(refNode);
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
                        context_->sourceContext()
                            ? context_->sourceContext()->resolveGirNodeOrigin(srcNode)
                            : camel::source::kInvalidOriginId,
                        -1,
                        "base"),
                },
                std::move(mergedInputs));

            LEAVE("DATA");
            return node;
        }
    }

    node = DataNode::create(*currGraph_, data);
    if (varied_ && currGraph_->outer() != nullptr) {
        // Global variables stop using copy maintenance here.
        // Local variables still need a fresh copy for each call.
        // The mechanism for locally shared variables is not designed yet.
        Node *copyNode = CopyNode::create(*currGraph_, data->type());
        Node::link(LinkType::Norm, node, copyNode);
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

Node *Builder::visitNRefNode(const GCT::node_ptr_t &gct) {
    ENTER("NREF");
    const string &ident = gct->loadAs<GCT::NRefLoad>()->ref();
    const auto &res     = visit(gct->at(0));
    ASSERT(
        res.type() == typeid(Node *),
        "Unexpected result type from Enter the child of NREF node.");
    Node *node   = any_cast<Node *>(res);
    bool success = insertNode(ident, node);
    if (!success) {
        diags_->of(SemanticDiag::Redeclaration).atOrigin(gct->load()->origin()).commit(ident);
        throw BuildAbortException();
    }
    LEAVE("NREF");
    return node;
}

Node *Builder::visitDRefNode(const GCT::node_ptr_t &gct) {
    ENTER("DREF");
    const string &name = gct->loadAs<GCT::DRefLoad>()->ref();
    auto optNode       = nodeAt(name);
    if (optNode.has_value()) {
        Node *node = optNode.value();
        if (node->graph() != *currGraph_) {
            node = resolveCrossGraphRef(node, name);
            ASSERT(node->graph() == *currGraph_, "Failed to resolve cross-graph reference.");
        }
        LEAVE("DREF");
        return node;
    }
    graph_ptr_t &graph = currGraph_;
    auto optDecorated  = decoratedGraphAt(name);
    if (optDecorated.has_value()) {
        Node *drefNode = DrefNode::create(*graph, optDecorated.value());
        registerNodeOrigin(context_, drefNode, gct, "gir.dref.decorated");
        LEAVE("DREF");
        return drefNode;
    }
    auto optGraphs = graphsAt(name);
    if (optGraphs.has_value()) {
        auto graphs = optGraphs.value();
        if (!graphs->empty()) {
            Node *drefNode = DrefNode::create(*graph, graphs);
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
        if (std::holds_alternative<Node *>(e)) {
            ASSERT(false, "Cannot import a data node directly.");
            const auto &node = std::get<Node *>(e);
            LEAVE("DREF");
            return node;
        } else if (std::holds_alternative<graph_vec_ptr_t>(e)) {
            auto graphs    = std::get<graph_vec_ptr_t>(e);
            Node *drefNode = DrefNode::create(*graph, graphs);
            registerNodeOrigin(context_, drefNode, gct, "gir.dref");
            LEAVE("DREF");
            return drefNode;
        } else if (std::holds_alternative<oper_group_ptr_t>(e)) {
            auto ops       = std::get<oper_group_ptr_t>(e);
            Node *drefNode = DrefNode::create(*graph, ops);
            registerNodeOrigin(context_, drefNode, gct, "gir.dref");
            LEAVE("DREF");
            return drefNode;
        } else if (std::holds_alternative<graph_ptr_t>(e)) {
            auto decorated = std::get<graph_ptr_t>(e);
            Node *drefNode = DrefNode::create(*graph, decorated);
            registerNodeOrigin(context_, drefNode, gct, "gir.dref.decorated");
            LEAVE("DREF");
            return drefNode;
        }
    }
    diags_->of(SemanticDiag::UnresolvedReference).atOrigin(gct->load()->origin()).commit(name);
    throw BuildAbortException();
}

Node *Builder::visitCastNode(const GCT::node_ptr_t &gct) {
    ENTER("CAST");
    const auto &res = visit(gct->at(0));
    ASSERT(res.type() == typeid(Node *), "Unexpected result type from child of CAST node.");
    Node *valueNode = any_cast<Node *>(res);
    ASSERT(valueNode != nullptr, "Cast node value is null.");
    const auto &castLoad = gct->loadAs<GCT::CastLoad>();
    Type *targetType     = castLoad->targetType();
    Type *sourceType     = valueNode->dataType();
    if (targetType->castSafetyFrom(sourceType) != CastSafety::Safe) {
        diags_->of(SemanticDiag::DynamicCastForbidden)
            .atOrigin(gct->load()->origin())
            .commit(sourceType->toString(), targetType->toString());
        throw BuildAbortException();
    }
    Node *castNode = CastNode::create(*currGraph_, targetType);
    Node::link(LinkType::Norm, valueNode, castNode);
    registerNodeOrigin(context_, castNode, gct, "gir.cast");
    LEAVE("CAST");
    return castNode;
}

Node *Builder::visitVariNode(const GCT::node_ptr_t &gct) {
    ENTER("VARI");
    bool old        = varied_;
    varied_         = true;
    const auto &res = visit(gct->at(0));
    ASSERT(
        res.type() == typeid(Node *),
        "Unexpected result type from Enter the child of VARI node.");
    Node *node = any_cast<Node *>(res);
    varied_    = old;
    LEAVE("VARI");
    return node;
}

Node *Builder::visitWaitNode(const GCT::node_ptr_t &gct) {
    ENTER("WAIT");
    bool old        = waited_;
    waited_         = true;
    const auto &res = visit(gct->at(0));
    ASSERT(
        res.type() == typeid(Node *),
        "Unexpected result type from Enter the child of WAIT node.");
    Node *node = any_cast<Node *>(res);
    waited_    = old;
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
Node *Builder::createFuncDataNode(
    const graph_ptr_t &graph, bool callableAsResult, bool allowParameterization) {
    ASSERT(
        !(callableAsResult && allowParameterization),
        "Cannot enable both callableAsResult and allowParameterization options.");
    ASSERT(currGraph_ != nullptr, "Current owner graph is null when creating a function node.");
    ASSERT(graph != nullptr, "Target graph is null when creating a function node.");

    // Centralize dependency bookkeeping here so every FUNC node and static Function value obeys
    // the same graph-reference invariant. Self-dependency is intentional: recursive graphs are
    // represented by the owner's `looped` flag.
    currGraph_->addDependency(graph);

    bool graphUsedBefore = usedGraphs_.find(graph.get()) != usedGraphs_.end();
    bool resolved        = graph->closure().empty();
    auto *staticFunc     = createStaticFunction(*currGraph_, graph);

    Node *resultNode   = nullptr;
    auto sourceContext = context_ ? context_->sourceContext() : nullptr;
    auto graphOrigin   = sourceContext ? sourceContext->debugMap().graphOrigin(graph->stableId())
                                       : camel::source::kInvalidOriginId;

    auto markMacroNode = [&](Node *node) {
        if (node && graph->isMacro()) {
            detail::NodeMutation::setMacro(node, true);
        }
    };

    if (allowParameterization && !callableAsResult && !graphUsedBefore) {
        if (resolved) {
            resultNode = FuncNode::create(*currGraph_, graph);
            markMacroNode(resultNode);
            bindGraphScopedFuncNodeDebug(sourceContext, graphOrigin, graph, resultNode);
        } else {
            std::vector<std::string> closureRefs;
            closureRefs.reserve(graph->closure().size());
            for (Node *closureNode : graph->closure()) {
                closureRefs.push_back(tt::as_ptr<PortNode>(closureNode)->name());
            }
            graph->parametrizeClosure();
            auto funcNode = FuncNode::create(*currGraph_, graph);
            markMacroNode(funcNode);
            for (const auto &ref : closureRefs) {
                const auto &refNode = resolveNodeByRef(ref);
                Node::link(LinkType::With, refNode, funcNode);
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
            resultNode = DataNode::createStaticSlot(
                *currGraph_,
                graph->funcType(),
                camel::core::rtdata::toSlot<StaticFunction *>(staticFunc));
            markMacroNode(resultNode);
        } else {
            auto funcNode = FuncNode::create(*currGraph_, graph);
            markMacroNode(funcNode);
            if (graph->parameterized()) {
                for (const auto &ref : graph->funcType()->closureRefs()) {
                    const auto &refNode = resolveNodeByRef(std::string(ref));
                    Node::link(LinkType::With, refNode, funcNode);
                }
            }
            resultNode = funcNode;
        }
        bindGraphScopedFuncNodeDebug(sourceContext, graphOrigin, graph, resultNode);

        usedGraphs_.insert(graph.get());
        return resultNode;
    }

    // graph still carries unresolved closure captures while parameterization is disabled
    auto dataNode = DataNode::createStaticSlot(
        *currGraph_,
        graph->funcType(),
        camel::core::rtdata::toSlot<StaticFunction *>(staticFunc));
    markMacroNode(dataNode);
    node_vec_t refNodes;
    for (Node *closureNode : graph->closure()) {
        const auto &refNode = resolveNodeByRef(tt::as_ptr<PortNode>(closureNode)->name());
        refNodes.push_back(refNode);
    }

    auto fillNode = FillNode::create(*currGraph_, graph->funcType());
    markMacroNode(fillNode);
    Node::link(LinkType::Norm, dataNode, fillNode);
    for (const auto &refNode : refNodes) {
        Node::link(LinkType::With, refNode, fillNode);
    }

    if (callableAsResult) {
        resultNode = fillNode;
    } else {
        auto callNode = CallNode::create(*currGraph_, graph->funcType()->exitType());
        markMacroNode(callNode);
        Node::link(LinkType::With, fillNode, callNode);
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

Node *Builder::applyDecoratorAnno(const GCT::node_ptr_t &annoNode, Node *funcValueNode) {
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

    Node *decoratedValue = visitLinkNode(linkNode);
    nodeScope_           = nodeScope_->leave();
    return decoratedValue;
}

graph_ptr_t Builder::buildDecoratedGraph(
    const std::string &funcName, const graph_ptr_t &rawGraph,
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
    Node *decoratedValue = createFuncDataNode(rawGraph, true, false);
    for (auto it = annoNodes.rbegin(); it != annoNodes.rend(); ++it) {
        decoratedValue = applyDecoratorAnno(*it, decoratedValue);
    }

    if (decoratedValue->dataType()->code() != TypeCode::Function) {
        diags_->of(SemanticDiag::ArgumentsMismatch)
            .atOrigin(annoNodes.front()->load()->origin())
            .commit("decorator must return function", decoratedValue->dataType()->toString());
        throw BuildAbortException();
    }

    decoratedGraph->setOutput(decoratedValue);
    leaveScope();
    return decoratedGraph;
}

Node *Builder::visitLinkNode(const GCT::node_ptr_t &gct) {
    ENTER("LINK");
    any targetNodeRes = visit(gct->at(0));
    ASSERT(
        targetNodeRes.type() == typeid(Node *),
        "Unexpected result type from Enter the child of LINK node.");
    Node *targetNode = any_cast<Node *>(targetNodeRes);

    graph_ptr_t targetGraph       = nullptr;
    oper_idx_ptr_t targetOperator = nullptr;
    FunctionType *targetFuncType  = nullptr;
    node_vec_t withInputNodes, normInputNodes;
    type_vec_t withInputTypes, normInputTypes;

    for (const auto &withInputNode : targetNode->withInputs()) {
        withInputNodes.push_back(withInputNode);
        withInputTypes.push_back(withInputNode->dataType());
    }

    for (size_t i = 1; i < gct->size(); i++) {
        any dataRes = visit(gct->at(i));
        if (dataRes.type() == typeid(graph_ptr_t)) {
            // The subtree returned a subgraph,
            // which means that a lambda function is passed as a parameter
            graph_ptr_t inputGraph = any_cast<graph_ptr_t>(dataRes);
            currGraph_->addDependency(inputGraph);
            auto inputNode = createFuncDataNode(inputGraph, true, false);
            normInputNodes.push_back(inputNode);
            normInputTypes.push_back(inputNode->dataType());
        } else if (dataRes.type() == typeid(Node *)) {
            Node *inputNode = any_cast<Node *>(dataRes);
            if (inputNode->type() == NodeType::DREF) {
                auto *argDref = tt::as_ptr<DrefNode>(inputNode);
                if (std::holds_alternative<graph_ptr_t>(argDref->target())) {
                    auto decoratedGraph = std::get<graph_ptr_t>(argDref->target());
                    currGraph_->addDependency(decoratedGraph);
                    if (!decoratedGraph->funcType()->hasExitType() ||
                        decoratedGraph->funcType()->exitType()->code() != TypeCode::Function) {
                        diags_->of(SemanticDiag::ArgumentsMismatch)
                            .atOrigin(gct->load()->origin())
                            .commit(
                                "decorated graph should return function",
                                decoratedGraph->funcType()->toString());
                        throw BuildAbortException();
                    }
                    Node *decoratorFactoryValue = createFuncDataNode(decoratedGraph, true, false);
                    auto *decoratorInvoke =
                        CallNode::create(*currGraph_, decoratedGraph->funcType()->exitType());
                    Node::link(LinkType::With, decoratorFactoryValue, decoratorInvoke);
                    argDref->detach();
                    inputNode = decoratorInvoke;
                }
            }
            normInputNodes.push_back(inputNode);
            if (inputNode->type() == NodeType::FUNC) {
                const auto &funcNode = tt::as_ptr<FuncNode>(inputNode);
                const auto &fType    = funcNode->funcType();
                normInputTypes.push_back(fType->exitType());
            } else if (inputNode->type() == NodeType::OPER) {
                const auto &operNode = tt::as_ptr<OperNode>(inputNode);
                const auto &fType    = operNode->funcType();
                normInputTypes.push_back(fType->exitType());
            } else {
                normInputTypes.push_back(inputNode->dataType());
            }
        } else {
            ASSERT(false, std::format("Unexpected result type from the {} child of LINK node", i));
        }
    }

    if (targetNode->type() == NodeType::DREF) {
        const auto &drefNode = tt::as_ptr<DrefNode>(targetNode);
        if (std::holds_alternative<graph_vec_ptr_t>(drefNode->target())) {
            auto graphs = std::get<graph_vec_ptr_t>(drefNode->target());
            for (const auto &g : *graphs) {
                const auto &funcType = g->funcType();
                if (!funcType->hasExitType()) {
                    diags_->of(SemanticDiag::CallingIncompleteFunction)
                        .atOrigin(gct->load()->origin())
                        .commit(g->name(), funcType->toString());
                    throw BuildAbortException();
                }
                StaticFuncTypeResolver resolver(funcType);
                const auto &res = resolver.resolve(withInputTypes, normInputTypes, Modifier::None);
                if (res.has_value()) {
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
            currGraph_->addDependency(targetGraph);
            // If the target graph is a subgraph of the current graph, it was
            // defined in the current graph scope.
            // That means every closure capture can be resolved in the current
            // graph, so we can optimize the capture into parameter passing and
            // reduce runtime maintenance overhead.
            targetNode     = createFuncDataNode(targetGraph, false, true);
            targetFuncType = targetGraph->funcType();
        } else if (std::holds_alternative<oper_group_ptr_t>(drefNode->target())) {
            auto ops        = std::get<oper_group_ptr_t>(drefNode->target());
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
            Node *operNode = OperNode::create(*currGraph_, targetOperator);
            registerNodeOrigin(context_, operNode, gct->at(0), "gir.link.oper");
            targetNode     = operNode;
            targetFuncType = targetOperator->funcType();
        } else if (std::holds_alternative<graph_ptr_t>(drefNode->target())) {
            auto decoratedGraph = std::get<graph_ptr_t>(drefNode->target());
            currGraph_->addDependency(decoratedGraph);
            if (!decoratedGraph->funcType()->hasExitType() ||
                decoratedGraph->funcType()->exitType()->code() != TypeCode::Function) {
                diags_->of(SemanticDiag::ArgumentsMismatch)
                    .atOrigin(gct->load()->origin())
                    .commit(
                        "decorated graph should return function",
                        decoratedGraph->funcType()->toString());
                throw BuildAbortException();
            }

            // 1) evaluate decorated graph itself (it should yield a function value)
            Node *decoratorFactoryValue = createFuncDataNode(decoratedGraph, true, false);
            auto *decoratorInvoke =
                CallNode::create(*currGraph_, decoratedGraph->funcType()->exitType());
            Node::link(LinkType::With, decoratorFactoryValue, decoratorInvoke);

            FunctionType *factoryType = tt::as_ptr<FunctionType>(decoratorInvoke->dataType());

            // 2) call returned callable with user's args.
            // The with-input is the invoked decorator result.
            targetNode = CallNode::create(*currGraph_, factoryType->exitType());
            Node::link(LinkType::With, decoratorInvoke, targetNode);
            targetFuncType = factoryType;
        } else {
            ASSERT(false, "DrefNode must refer to graph(s), operator group, or inlined graph.");
        }
        drefNode->detach();
    } else {
        const auto &dataType = targetNode->dataType();
        ASSERT(
            dataType->code() == TypeCode::Function,
            "Target node of LINK must be a function or operator node.");
        const auto &funcType = tt::as_ptr<FunctionType>(dataType);
        StaticFuncTypeResolver resolver(funcType);
        const auto &res = resolver.resolve(withInputTypes, normInputTypes, Modifier::None);
        if (!res.has_value()) {
            std::string argTypesStr = std::format(
                "<{}> ({})",
                strutil::join(withInputTypes, ", ", [](Type *t) { return t->toString(); }),
                strutil::join(normInputTypes, ", ", [](Type *t) { return t->toString(); }));
            diags_->of(SemanticDiag::ArgumentsMismatch)
                .atOrigin(gct->load()->origin())
                .commit(funcType->toString(), argTypesStr);
            throw BuildAbortException();
        }
        Node *invokeNode = CallNode::create(*currGraph_, funcType->exitType());
        Node::link(LinkType::With, targetNode, invokeNode);
        std::vector<camel::source::origin_id_t> callInputs;
        if (auto *sourceContext = context_ ? context_->sourceContext().get() : nullptr) {
            auto calleeOrigin = sourceContext->resolveGirNodeOrigin(targetNode);
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
        Node *inputNode = withInputNodes[i];
        bool isVar      = (i < targetFuncType->withTypesCount()) &&
                          targetFuncType->withIsVarAt(static_cast<size_t>(i));
        tryRemoveCtrlLink(inputNode, targetNode);
        Node::link(LinkType::With, inputNode, targetNode);
        if (nodeModifierMap_.count(inputNode)) {
            Node *modifierNode = nodeModifierMap_[inputNode];
            if (modifierNode && linkCheek(modifierNode, targetNode)) {
                Node::link(LinkType::Ctrl, modifierNode, targetNode);
            }
        }
        if (isVar) {
            if (!waited_ && !synced_) {
                diags_->of(SemanticDiag::IgnoredSideEffect)
                    .atOrigin(gct->load()->origin())
                    .commit(targetName + ": " + targetFuncType->toString());
            }
            nodeModifierMap_[inputNode] = targetNode;
        }
    }

    for (size_t i = 0; i < normInputNodes.size(); i++) {
        Node *inputNode = normInputNodes[i];
        bool isVar      = false;
        if (i < targetFuncType->normTypesCount()) {
            isVar = targetFuncType->normIsVarAt(i);
        }
        tryRemoveCtrlLink(inputNode, targetNode);
        Node::link(LinkType::Norm, inputNode, targetNode);
        if (nodeModifierMap_.count(inputNode)) {
            Node *modifierNode = nodeModifierMap_[inputNode];
            if (modifierNode && linkCheek(modifierNode, targetNode)) {
                Node::link(LinkType::Ctrl, modifierNode, targetNode);
            }
        }
        if (isVar) {
            if (!waited_ && !synced_) {
                diags_->of(SemanticDiag::IgnoredSideEffect)
                    .atOrigin(gct->load()->origin())
                    .commit(targetName + ": " + targetFuncType->toString());
            }
            nodeModifierMap_[inputNode] = targetNode;
        }
    }

    if (synced_) {
        if (lastSyncedNode_ && linkCheek(lastSyncedNode_, targetNode)) {
            Node::link(LinkType::Ctrl, lastSyncedNode_, targetNode);
        }
        lastSyncedNode_ = targetNode;
    }
    LEAVE("LINK");
    return targetNode;
}

Node *Builder::visitWithNode(const GCT::node_ptr_t &gct) {
    ENTER("WITH");
    any targetNodeRes = visit(gct->at(0));
    ASSERT(
        targetNodeRes.type() == typeid(Node *),
        "Unexpected result type from Enter the child of WITH node.");
    Node *targetNode = any_cast<Node *>(targetNodeRes);
    vector<Node *> inputs;
    for (size_t i = 1; i < gct->size(); i++) {
        any dataRes = visit(gct->at(i));
        if (dataRes.type() == typeid(graph_ptr_t)) {
            // The subtree returned a subgraph,
            // which means that a lambda function is passed as a parameter
            graph_ptr_t subGraph = any_cast<graph_ptr_t>(dataRes);
            currGraph_->addDependency(subGraph);
            auto inputNode = createFuncDataNode(subGraph, true, false);
            inputs.push_back(inputNode);
        } else if (dataRes.type() == typeid(Node *)) {
            Node *inputNode = any_cast<Node *>(dataRes);
            inputs.push_back(inputNode);
        } else {
            ASSERT(false, std::format("Unexpected result type from the {} child of WITH node", i));
        }
    }
    for (size_t i = 0; i < inputs.size(); i++) {
        Node *inputNode = inputs[i];
        tryRemoveCtrlLink(inputNode, targetNode);
        Node::link(LinkType::With, inputNode, targetNode);
    }
    // With is not a call, so synced_ and lastCalledFuncNode_ are left alone.
    LEAVE("WITH");
    return targetNode;
}

Node *Builder::visitAccsNode(const GCT::node_ptr_t &gct) {
    ENTER("ACCS");
    any res = visit(gct->at(0));
    ASSERT(
        res.type() == typeid(Node *),
        "Unexpected result type from Enter the child of ACCS node.");
    Node *tgtNode = any_cast<Node *>(res);
    ASSERT(tgtNode != nullptr, "Access node target is null.");
    if (!tgtNode->dataType()->isComposite()) {
        diags_->of(SemanticDiag::TypeNotIndexable)
            .atOrigin(gct->load()->origin())
            .commit(tgtNode->dataType()->toString());
        throw BuildAbortException();
    }

    const auto tgtType   = tgtNode->dataType();
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

    graph_ptr_t &graph = currGraph_;
    Node *accsNode     = AccsNode::create(*graph, elemType, accsLoad->index());
    Node::link(LinkType::Norm, tgtNode, accsNode);
    registerNodeOrigin(context_, accsNode, gct, "gir.accs");
    LEAVE("ACCS");
    return accsNode;
}

Node *Builder::visitBrchNode(const GCT::node_ptr_t &gct) {
    ENTER("BRCH");
    const auto &res = visit(gct->at(0));
    ASSERT(
        res.type() == typeid(Node *),
        "Unexpected result type from Enter the child of BRCH node.");
    Node *condNode = any_cast<Node *>(res);
    Node *brchNode = BrchNode::create(*currGraph_, Type::Int64());
    Node *joinNode = JoinNode::create(*currGraph_, nullptr);
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

    Node::link(LinkType::Norm, condNode, brchNode);

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
                res.type() == typeid(Node *),
                "Unexpected result type from visiting the case node.");
            Node *valueNode = any_cast<Node *>(res);
            Node::link(LinkType::With, valueNode, brchNode);
            caseExecNode = caseNode->atAs<GCT::ExecLoad>(1);
            break;
        }
        default:
            ASSERT(false, "Unknown case type in BRCH node.");
        }

        graph_ptr_t subGraph = enterScope(FunctionType::create());
        registerGraphOrigin(context_, subGraph, caseExecNode, "gir.brch.case.graph");
        Node *resNode = visitExecNode(caseExecNode);
        if (subGraph->exitNode_ == nullptr) {
            if (resNode) {
                subGraph->setOutput(resNode);
            } else {
                // function with no return value, setting null by default
                Node *nullNode = DataNode::create(*subGraph, Data::null());
                subGraph->setOutput(nullNode);
            }
        }
        leaveScope();

        currGraph_->addDependency(subGraph);
        Type *exitType = subGraph->funcType()->exitType();

        // Ensure all captured variables are ready before the BRCH node runs.
        // This keeps the nodes on every branch path between BRCH and JOIN
        // tightly grouped in topological order, with no unrelated external
        // nodes inserted in between.
        // That makes the graph scheduler easier to implement because it can
        // jump directly without frequent checks.
        for (const auto &port : subGraph->closure()) {
            const auto &portNode = tt::as_ptr<PortNode>(port);
            const auto &refNode  = resolveNodeByRef(portNode->name());
            if (linkCheek(refNode, brchNode)) {
                Node::link(LinkType::Ctrl, refNode, brchNode);
            }
        }

        // Closure parameterization is allowed by default in branches.
        auto funcNode = createFuncDataNode(subGraph, false, true);

        if (joinType == nullptr) {
            joinType = exitType;
            detail::NodeMutation::setDataType(joinNode, joinType);
        } else {
            if (!exitType->equals(joinType)) {
                diags_->of(SemanticDiag::BranchReturnTypeMismatch)
                    .atOrigin(gct->load()->origin())
                    .commit(
                        currGraph_->location() + ": " + currGraph_->funcType()->toString(),
                        joinType->toString(),
                        exitType->toString());
                throw BuildAbortException();
            }
        }

        Node::link(LinkType::Ctrl, brchNode, funcNode);
        Node::link(LinkType::With, funcNode, joinNode);
    }

    Node::link(LinkType::Norm, brchNode, joinNode);

    if (synced_) {
        if (lastSyncedNode_ && linkCheek(lastSyncedNode_, brchNode)) {
            Node::link(LinkType::Ctrl, lastSyncedNode_, brchNode);
        }
        lastSyncedNode_ = joinNode;
    }

    LEAVE("BRCH");
    return joinNode;
}

Node *Builder::visitAnnoNode(const GCT::node_ptr_t &gct) {
    ENTER("ANNO");
    ASSERT(gct->size() == 1, "ANNO node should have exactly one child.");
    const auto &res = visit(gct->at(0));
    if (res.type() == typeid(Node *)) {
        Node *node = any_cast<Node *>(res);
        LEAVE("ANNO");
        return node;
    }
    if (res.type() == typeid(graph_ptr_t)) {
        graph_ptr_t graph = any_cast<graph_ptr_t>(res);
        currGraph_->addDependency(graph);
        Node *node = createFuncDataNode(graph, true, false);
        LEAVE("ANNO");
        return node;
    }
    ASSERT(false, "Unexpected child result type in ANNO node.");
    LEAVE("ANNO");
    return nullptr;
}

Node *Builder::visitExitNode(const GCT::node_ptr_t &gct) {
    ENTER("EXIT");
    auto res      = visit(gct->at(0));
    Node *resNode = nullptr;
    if (res.type() == typeid(Node *)) {
        resNode = any_cast<Node *>(res);
    } else if (res.type() == typeid(graph_ptr_t)) {
        graph_ptr_t subGraph = any_cast<graph_ptr_t>(res);
        currGraph_->addDependency(subGraph);
        // Returning a function value should lower to a DATA(Function) static slot rather than an
        // eager call.
        resNode = createFuncDataNode(subGraph, true, false);
    } else {
        ASSERT(false, "Unexpected result type from Enter child of EXIT node.");
    }
    Node *outputAnchor = resNode;
    node_vec_t pendingCtrlInputs;
    if (nodeModifierMap_.count(resNode)) {
        Node *modifier = nodeModifierMap_[resNode];
        if (modifier) {
            pendingCtrlInputs.push_back(modifier);
        }
    }
    if (synced_ && lastSyncedNode_) {
        pendingCtrlInputs.push_back(lastSyncedNode_);
    }

    // Decide output anchor first, then validate Ctrl links against that anchor.
    // This avoids false negatives such as `tail -> JOIN` (cycle-prone) where
    // the correct lowering should be `tail -> GATE`, with EXIT anchored on GATE.
    if (!pendingCtrlInputs.empty() && resNode->type() != NodeType::GATE) {
        auto *gatedValue = GateNode::create(*currGraph_);
        detail::NodeMutation::setDataType(gatedValue, resNode->dataType());
        Node::link(LinkType::Norm, resNode, gatedValue);
        outputAnchor = gatedValue;
    }
    for (Node *ctrlInput : pendingCtrlInputs) {
        if (ctrlInput && linkCheek(ctrlInput, outputAnchor)) {
            Node::link(LinkType::Ctrl, ctrlInput, outputAnchor);
        }
    }
    currGraph_->setOutput(outputAnchor);

    LEAVE("EXIT");
    return resNode;
}

Node *Builder::visitExecNode(const GCT::node_ptr_t &gct) {
    ENTER("EXEC");
    const auto &execLoad = gct->loadAs<GCT::ExecLoad>();
    bool old             = synced_;
    Node *oldFuncNode    = lastSyncedNode_;
    synced_              = execLoad->synced();
    lastSyncedNode_      = nullptr;
    Node *res            = nullptr;
    for (size_t i = 0; i < gct->size(); i++) {
        try {
            any result = visit(gct->at(i));
            if (result.has_value() && result.type() == typeid(Node *)) {
                res = any_cast<Node *>(result);
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
            module_->exportEntity(ref, optDecorated.value());
            continue;
        }
        auto optNode = nodeAt(ref);
        if (optNode.has_value()) {
            module_->exportEntity(ref, optNode.value());
            continue;
        }
        auto optGraph = graphsAt(ref);
        if (optGraph.has_value()) {
            module_->exportEntity(ref, optGraph.value());
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
