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
 * Updated: Apr. 06, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "builder.h"

#include "camel/core/rtdata/func.h"
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

camel::core::mm::IAllocator &runtimeFunctionAllocator(Graph &ownerGraph) {
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

::Function *createRuntimeFunction(Graph &ownerGraph, const graph_ptr_t &targetGraph) {
    ASSERT(targetGraph != nullptr, "Target graph is null when materializing runtime Function.");
    return ::Function::create(
        targetGraph.get(),
        currentClosureTupleType(targetGraph),
        runtimeFunctionAllocator(ownerGraph));
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
    // GIR 是比 GCT 更“执行导向”的 lowering。
    // 当前策略不尝试为每个 GIR 节点重建全新的源码区间，而是沿 GCT 的主 origin 继续派生，
    // 因而一个 GCT 节点展开出的多个 GIR 节点通常共享同一个 primarySpan。
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

// 从 GCT 构建 GIR 图。全过程分为两个阶段：
// 1. 构造阶段：visit(gct) 遍历 GCT，通过 GraphBuilder 增量创建图和节点。
//    此阶段所有图均为 non-finalized，可自由编辑（含闭包捕获、参数化等结构变形）。
// 2. 封印阶段：sealGraphRecursively() 一次性为所有图计算 slot 编号、layout 与 finalized frame
// layout。
//    封印后所有图标记为 finalized，不再允许直接编辑。
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
    rootGraph_      = GraphBuilder::createGraph(FunctionType::create(), nullptr, "__root__");
    if (auto sourceContext = context_ ? context_->sourceContext() : nullptr) {
        rootGraph_->setExtra<camel::source::SourceContext, kSourceContextExtraIndex>(
            sourceContext.get());
    }
    currGraph_ = rootGraph_;

    try {
        visit(gct);

        auto optMainGraph = rootGraph_->getSubGraphsByName("main");
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
            GraphBuilder(rootGraph_).setOutput(funcNode);
        } else if (entryModule) {
            diags_->of(SemanticDiag::EntryModuleMissingMain).commit(module_->name());
            throw BuildAbortException();
        } else {
            // 非入口库模块可无 main：占位 output 以满足封印不变量。
            auto zero = std::make_shared<LongData>(static_cast<int64_t>(0));
            Node *const placeholder = DataNode::create(*rootGraph_, zero);
            GraphBuilder(rootGraph_).setOutput(placeholder);
        }

        GraphBuilder::sealGraphRecursively(rootGraph_);
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
        currGraph_ = GraphBuilder::createGraph(funcType, currGraph_);
    } else {
        auto graphs = graphScope_->get(name);
        if (graphs.has_value() && !graphs.value()->empty()) {
            currGraph_ = graphs.value()->front();
        } else {
            currGraph_ = GraphBuilder::createGraph(funcType, currGraph_, name);
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

// 跨图引用解析：当前函数图引用了外层图的节点时，
// 沿外层图链逐层插入 PortNode 作为闭包捕获端口。
// 这里直接修改沿途图的 closure 集合，但因为是在初始编译阶段（finalize 之前），
// 所有图尚未 finalized，这些修改通过 GraphBuilder::addClosure 受 assertBuildable 保护。
Node *Builder::resolveCrossGraphRef(Node *node, const std::string &name) {
    Graph *curr            = currGraph_.get();
    node_scope_ptr_t scope = nodeScope_;

    while (curr != &node->graph()) {
        // 在沿途经过的所有中间图添加 Port 节点
        if (usedGraphs_.find(curr) != usedGraphs_.end()) {
            // 不能对已经使用过的图添加闭包捕获
            diags_->of(SemanticDiag::ClosureCaptureAfterSelfCall).commit(name, curr->name());
            throw BuildAbortException();
        }

        // 插入一个 Port 节点
        Node *port = PortNode::create(*curr, node->dataType(), name, false);
        GraphBuilder(curr).addClosure(port);
        scope->insert(name, port);

        // 向外层图和作用域继续遍历
        curr  = curr->outer().get();
        scope = scope->outer();
    }

    // 再次获取新的节点引用
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

    // 如果是跨图节点引用，处理闭包捕获
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
    // 模块顶层 visitExecNode 会吞掉 BuildAbortException 并 continue；若函数体在 leaveScope
    // 之前抛错，currGraph_ 会卡在子图，后续函数（如 main）被挂到错误外层，root 上无 "main"、
    // __root__ 无 exit。必须在任何异常路径上配对 leaveScope。
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
                GraphBuilder(graph).setOutput(res);
            } else {
                // function with no return value, setting null by default
                Node *resNode = DataNode::create(*graph, Data::null());
                GraphBuilder(graph).setOutput(resNode);
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
        // If it is a global variable, no longer maintain a copy
        // For local variables, still need to create a new copy for each call
        // The mechanism of local shared variables is yet to be designed
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
        // hasImportedRef 为 true 只说明用户声明了导入这个名字
        // 但并不意味着被导入的模块中导出了这个名字
        // 所以这里仍然要做一次检测
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

// 为一个子图创建函数值节点。当 allowParameterization=true 时，
// 可能会调用 parametrizeClosure() 将子图的闭包捕获转为 with 参数，
// 这会直接修改子图的端口结构。此操作只在初始编译阶段（finalize 之前）发生，
// 所有图尚未 finalized，由 assertBuildable 保护。
Node *Builder::createFuncDataNode(
    const graph_ptr_t &graph, bool callableAsResult, bool allowParameterization) {
    ASSERT(
        !(callableAsResult && allowParameterization),
        "Cannot enable both callableAsResult and allowParameterization options.");

    bool graphUsedBefore = usedGraphs_.find(graph.get()) != usedGraphs_.end();
    bool resolved        = graph->closure().empty();
    auto *runtimeFunc    = createRuntimeFunction(*currGraph_, graph);

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
            GraphBuilder(graph).parametrizeClosure();
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
                camel::core::rtdata::toSlot<::Function *>(runtimeFunc));
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
        camel::core::rtdata::toSlot<::Function *>(runtimeFunc));
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

    // 保证在最后再更新 usedGraphs_
    // 因为在构造过程中可能会更新图的闭包捕获
    // 而在更新闭包捕获前会检查被更新的图是否已经被使用过
    // 所以要等所有的更新完成再将此图设置为被使用过
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

    GraphBuilder(decoratedGraph).setOutput(decoratedValue);
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
    std::vector<Node *> inlineLaterNodes;
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
            GraphBuilder(currGraph_).addDependency(inputGraph);
            auto inputNode = createFuncDataNode(inputGraph, true, false);
            normInputNodes.push_back(inputNode);
            normInputTypes.push_back(inputNode->dataType());
        } else if (dataRes.type() == typeid(Node *)) {
            Node *inputNode = any_cast<Node *>(dataRes);
            if (inputNode->type() == NodeType::DREF) {
                auto *argDref = tt::as_ptr<DrefNode>(inputNode);
                if (std::holds_alternative<graph_ptr_t>(argDref->target())) {
                    auto decoratedGraph = std::get<graph_ptr_t>(argDref->target());
                    GraphBuilder(currGraph_).addDependency(decoratedGraph);
                    if (!decoratedGraph->funcType()->hasExitType() ||
                        decoratedGraph->funcType()->exitType()->code() != TypeCode::Function) {
                        diags_->of(SemanticDiag::ArgumentsMismatch)
                            .atOrigin(gct->load()->origin())
                            .commit(
                                "decorated graph should return function",
                                decoratedGraph->funcType()->toString());
                        throw BuildAbortException();
                    }
                    Node *decoratorFactoryNode = createFuncDataNode(decoratedGraph, false, true);
                    ASSERT(
                        decoratorFactoryNode->type() == NodeType::FUNC,
                        "Decorated graph should lower to a FUNC node before inlining.");
                    FunctionType *factoryType =
                        tt::as_ptr<FunctionType>(decoratedGraph->funcType()->exitType());
                    (void)
                        factoryType; // type info is used through decoratorFactoryNode->dataType().
                    inlineLaterNodes.push_back(decoratorFactoryNode);
                    argDref->detach();
                    inputNode = decoratorFactoryNode;
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
            GraphBuilder(currGraph_).addDependency(targetGraph);
            // 如果目标图是当前图的子图，说明其是在当前图作用域中定义的
            // 这意味这它的闭包捕获在当前图中都能找到对应节点
            // 因而可以允许将闭包捕获优化成参数传递
            // 这样可以减少运行时对闭包捕获数据的维护开销
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
            GraphBuilder(currGraph_).addDependency(decoratedGraph);
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
            Node *decoratorFactoryNode = createFuncDataNode(decoratedGraph, false, true);
            ASSERT(
                decoratorFactoryNode->type() == NodeType::FUNC,
                "Decorated graph should lower to a FUNC node before inlining.");

            FunctionType *factoryType =
                tt::as_ptr<FunctionType>(decoratedGraph->funcType()->exitType());
            inlineLaterNodes.push_back(decoratorFactoryNode);

            // 2) call returned callable with user's args.
            // The with-input is the decorator graph output after inlining.
            targetNode = CallNode::create(*currGraph_, factoryType->exitType());
            Node::link(LinkType::With, decoratorFactoryNode, targetNode);
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

    // Inline decorated helper graphs after downstream links are fully established.
    // This rewires the decorated graph output directly into the target inputs.
    for (Node *inlineNode : inlineLaterNodes) {
        InlineResult inlineResult = GraphBuilder(currGraph_).inlineCallable(inlineNode);
        ASSERT(
            inlineResult.valueExit != nullptr,
            "Decorated helper inline must produce a value exit.");

        GraphBuilder(currGraph_).eraseNode(inlineNode);
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
            GraphBuilder(currGraph_).addDependency(subGraph);
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
    // with 操作不属于调用，所以这里不处理synced_和lastCalledFuncNode_
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
                GraphBuilder(subGraph).setOutput(resNode);
            } else {
                // function with no return value, setting null by default
                Node *nullNode = DataNode::create(*subGraph, Data::null());
                GraphBuilder(subGraph).setOutput(nullNode);
            }
        }
        leaveScope();

        GraphBuilder(currGraph_).addDependency(subGraph);
        Type *exitType = subGraph->funcType()->exitType();

        // 保证所有捕获的变量都在 BRCH 节点执行之前准备好
        // 这样的好处是，保证 BRCH 节点到 JOIN 节点之间的各条分支路径上
        // 所有的节点在拓扑排序时会被紧密地排列在一起
        // 而不会有其他外部节点插入到中间
        // 进而便于图调度算法的实现（可以直接跳转而不需要频繁判断）
        for (const auto &port : subGraph->closure()) {
            const auto &portNode = tt::as_ptr<PortNode>(port);
            const auto &refNode  = resolveNodeByRef(portNode->name());
            if (linkCheek(refNode, brchNode)) {
                Node::link(LinkType::Ctrl, refNode, brchNode);
            }
        }

        // 在分支中，默认允许闭包参数化
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
        GraphBuilder(currGraph_).addDependency(graph);
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
        GraphBuilder(currGraph_).addDependency(subGraph);
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
    GraphBuilder(currGraph_).setOutput(outputAnchor);

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
