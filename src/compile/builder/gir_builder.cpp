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
 * Updated: Dec. 17, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "gir_builder.h"

#include "utils/log.h"
#include "utils/scope.h"
#include "utils/str.h"
#include "utils/type.h"

#define DEBUG_LEVEL -1

using namespace std;

namespace GraphIR {

inline void tryRemoveCtrlLink(const node_ptr_t &from, const node_ptr_t &to) {
    // if from has already linked to to by a ctrl link, remove it first
    // sometimes we may need to change a ctrl link (linked before) to a data link
    // because data link has higher priority than ctrl link
    // and we don't want to have duplicate links
    auto &fromCtrlOutputs = from->ctrlOutputs();
    if (std::find(fromCtrlOutputs.begin(), fromCtrlOutputs.end(), to) != fromCtrlOutputs.end()) {
        fromCtrlOutputs.erase(
            std::remove(fromCtrlOutputs.begin(), fromCtrlOutputs.end(), to),
            fromCtrlOutputs.end());

        auto &toCtrlInputs = to->ctrlInputs();
        toCtrlInputs.erase(
            std::remove(toCtrlInputs.begin(), toCtrlInputs.end(), from),
            toCtrlInputs.end());
    }
}

inline bool linkCheek(const node_ptr_t &from, const node_ptr_t &to) {
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
        EXEC_WHEN_DEBUG(l.in("GIR").warn(
            "Prevent linking deeply linked nodes: {} -> {}",
            from->toString(),
            to->toString()));
        return false;
    }
    return true;
}

graph_ptr_t Builder::build(GCT::node_ptr_t &gct, diagnostics_ptr_t diags) {
    waited_ = false;
    synced_ = false;
    varied_ = false;
    diags_  = diags;
    usedGraphs_.clear();

    nodeScope_  = node_scope_t::create();
    graphScope_ = graph_scope_t::create();
    rootGraph_  = Graph::create(std::make_shared<FunctionType>(), nullptr, "__root__");
    currGraph_  = rootGraph_;

    try {
        visit(gct);

        auto optMainGraph = currGraph_->getSubGraphsByName("main");
        if (optMainGraph.has_value()) {
            auto mainGraphSet = optMainGraph.value();
            ASSERT(!mainGraphSet.empty(), "Main graph set is empty.");
            ASSERT(mainGraphSet.size() == 1, "Multiple main graphs found.");
            auto mainGraph = *mainGraphSet.begin();
            auto funcNode  = createFuncDataNode(mainGraph, false, false);
            currGraph_->setOutput(funcNode);
        }

        rootGraph_->rearrange();
    } catch (Diagnostic &d) {
        diags_->add(std::move(d));
        rootGraph_ = nullptr;
    }

    return rootGraph_;
}

graph_ptr_t Builder::enterScope(const func_type_ptr_t &funcType, const std::string &name) {
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
    nodeScope_  = nodeScope_->enter(name);
    graphScope_ = graphScope_->enter(name);
    return currGraph_;
}

void Builder::leaveScope() {
    nodeScope_  = nodeScope_->leave();
    graphScope_ = graphScope_->leave();
    currGraph_->rearrange();
    currGraph_ = currGraph_->outer();
}

bool Builder::insertNode(const std::string &name, const node_ptr_t &node) {
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

node_ptr_t Builder::resolveCrossGraphRef(const node_ptr_t &node, const std::string &name) {
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
        const auto &port = PortNode::create(*curr, node->dataType(), name, false);
        curr->addClosure(port);
        scope->insert(name, port);

        // 向外层图和作用域继续遍历
        curr  = curr->outer().get();
        scope = scope->outer();
    }

    // 再次获取新的节点引用
    return *nodeScope_->get(name);
}

node_ptr_t Builder::resolveNodeByRef(const std::string &name) {
    auto optSrcNode = nodeAt(name);
    if (!optSrcNode.has_value()) {
        diags_->of(SemanticDiag::UnresolvedReference).commit(name);
        throw BuildAbortException();
    }
    node_ptr_t node = optSrcNode.value();

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
    type_ptr_t type          = typeNode->loadAs<GCT::TypeLoad>()->dataType();
    func_type_ptr_t funcType = tt::as_shared<FunctionType>(type);

    graph_ptr_t graph = enterScope(funcType, declLoad->ref().ident());
    leaveScope();

    LEAVE("DECL");
    return nullptr;
}

graph_ptr_t Builder::visitFuncNode(const GCT::node_ptr_t &gct) {
    ENTER("FUNC");
    // type_ptr_t type = visitTypeNode(gct->atAs<GCT::TypeLoad>(0));
    std::string name         = gct->loadAs<GCT::FuncLoad>()->name();
    GCT::node_ptr_t typeLoad = gct->atAs<GCT::TypeLoad>(0);
    type_ptr_t type          = typeLoad->loadAs<GCT::TypeLoad>()->dataType();
    func_type_ptr_t funcType = tt::as_shared<FunctionType>(type);
    graph_ptr_t graph        = enterScope(funcType, name);
    for (const auto &port : graph->withPorts()) {
        const auto &portNode = tt::as_shared<PortNode>(port);
        insertNode(portNode->name(), port);
    }
    for (const auto &port : graph->normPorts()) {
        const auto &portNode = tt::as_shared<PortNode>(port);
        insertNode(portNode->name(), port);
    }
    node_ptr_t res = visitExecNode(gct->atAs<GCT::ExecLoad>(1));
    if (!graph->hasOutput()) {
        if (res) {
            graph->setOutput(res);
        } else {
            // function with no return value, setting null by default
            node_ptr_t resNode = DataNode::create(*graph, Data::null());
            graph->setOutput(resNode);
        }
    }
    leaveScope();
    LEAVE("FUNC");
    return graph;
}

node_ptr_t Builder::visitDataNode(const GCT::node_ptr_t &gct) {
    ENTER("DATA");
    const auto &dataLoad   = gct->loadAs<GCT::DataLoad>();
    const data_ptr_t &data = dataLoad->data();

    node_ptr_t node   = nullptr;
    TypeCode dataType = data->type()->code();
    if (isComposite(dataType)) {
        auto composedData = tt::as_shared<CompositeData>(data);
        if (!composedData->resolved()) {
            node_ptr_t srcNode   = DataNode::create(*currGraph_, data);
            const auto &dataType = tt::as_shared<CompositeType>(data->type());
            type_vec_t refTypes;
            node_vec_t refNodes;
            for (const auto &ref : composedData->refs()) {
                const auto &refNode = resolveNodeByRef(ref);
                refTypes.push_back(refNode->dataType());
                refNodes.push_back(refNode);
            }
            auto fillType   = tt::as_shared<CompositeType>(dataType->clone());
            auto filledType = fillType->resolve(refTypes);
            node            = FillNode::create(*currGraph_, filledType);
            Node::link(LinkType::Norm, srcNode, node);
            for (const auto &refNode : refNodes) {
                Node::link(LinkType::With, refNode, node);
            }

            LEAVE("DATA");
            return node;
        }
    }

    node = DataNode::create(*currGraph_, data);
    if (varied_ && currGraph_->outer() != nullptr) {
        // If it is a global variable, no longer maintain a copy
        // For local variables, still need to create a new copy for each call
        // The mechanism of local shared variables is yet to be designed
        node_ptr_t copyNode = CopyNode::create(*currGraph_, data->type());
        Node::link(LinkType::Norm, node, copyNode);
        node = copyNode;
    }

    LEAVE("DATA");
    return node;
}

type_ptr_t Builder::visitTypeNode(const GCT::node_ptr_t &gct) {
    ENTER("TYPE");
    type_ptr_t type = gct->loadAs<GCT::TypeLoad>()->dataType();
    LEAVE("TYPE");
    return type;
}

node_ptr_t Builder::visitNRefNode(const GCT::node_ptr_t &gct) {
    ENTER("NREF");
    const string &ident = gct->loadAs<GCT::NRefLoad>()->ref();
    const auto &res     = visit(gct->at(0));
    ASSERT(
        res.type() == typeid(node_ptr_t),
        "Unexpected result type from Enter the child of NREF node.");
    node_ptr_t node = any_cast<node_ptr_t>(res);
    bool success    = insertNode(ident, node);
    if (!success) {
        diags_->of(SemanticDiag::Redeclaration).commit(ident);
        throw BuildAbortException();
    }
    LEAVE("NREF");
    return node;
}

node_ptr_t Builder::visitDRefNode(const GCT::node_ptr_t &gct) {
    ENTER("DREF");
    const string &name = gct->loadAs<GCT::DRefLoad>()->ref();
    auto optNode       = nodeAt(name);
    if (optNode.has_value()) {
        node_ptr_t node = optNode.value();
        if (node->graph() != *currGraph_) {
            node = resolveCrossGraphRef(node, name);
            ASSERT(node->graph() == *currGraph_, "Failed to resolve cross-graph reference.");
        }
        LEAVE("DREF");
        return node;
    }
    graph_ptr_t &graph = currGraph_;
    auto optGraphs     = graphsAt(name);
    if (optGraphs.has_value()) {
        auto graphs = optGraphs.value();
        if (!graphs->empty()) {
            node_ptr_t drefNode = DrefNode::create(*graph, graphs);
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
            diags_->of(SemanticDiag::ImportNameNotExported).commit(name);
        }
        const auto &e = opt.value();
        if (std::holds_alternative<node_ptr_t>(e)) {
            ASSERT(false, "Cannot import a data node directly.");
            const auto &node = std::get<node_ptr_t>(e);
            LEAVE("DREF");
            return node;
        } else if (std::holds_alternative<graph_vec_ptr_t>(e)) {
            auto graphs         = std::get<graph_vec_ptr_t>(e);
            node_ptr_t drefNode = DrefNode::create(*graph, graphs);
            LEAVE("DREF");
            return drefNode;
        } else if (std::holds_alternative<oper_group_ptr_t>(e)) {
            auto ops            = std::get<oper_group_ptr_t>(e);
            node_ptr_t drefNode = DrefNode::create(*graph, ops);
            LEAVE("DREF");
            return drefNode;
        }
    }
    diags_->of(SemanticDiag::UnresolvedReference).commit(name);
    throw BuildAbortException();
}

node_ptr_t Builder::visitCastNode(const GCT::node_ptr_t &gct) {
    ENTER("CAST");
    node_ptr_t res = nullptr;
    LEAVE("CAST");
    return res;
}

node_ptr_t Builder::visitVariNode(const GCT::node_ptr_t &gct) {
    ENTER("VARI");
    bool old        = varied_;
    varied_         = true;
    const auto &res = visit(gct->at(0));
    ASSERT(
        res.type() == typeid(node_ptr_t),
        "Unexpected result type from Enter the child of VARI node.");
    node_ptr_t node = any_cast<node_ptr_t>(res);
    varied_         = old;
    LEAVE("VARI");
    return node;
}

node_ptr_t Builder::visitWaitNode(const GCT::node_ptr_t &gct) {
    ENTER("WAIT");
    bool old        = waited_;
    waited_         = true;
    const auto &res = visit(gct->at(0));
    ASSERT(
        res.type() == typeid(node_ptr_t),
        "Unexpected result type from Enter the child of WAIT node.");
    node_ptr_t node = any_cast<node_ptr_t>(res);
    waited_         = old;
    LEAVE("WAIT");
    return node;
}

node_ptr_t Builder::createFuncDataNode(
    const graph_ptr_t &graph, bool callableAsResult, bool allowParameterization) {
    ASSERT(
        !(callableAsResult && allowParameterization),
        "Cannot enable both callableAsResult and allowParameterization options.");

    bool graphUsedBefore = usedGraphs_.find(graph.get()) != usedGraphs_.end();

    auto funcData         = FunctionData::create(*graph);
    node_ptr_t resultNode = nullptr;

    if (allowParameterization && !callableAsResult && !graphUsedBefore) {
        if (funcData->resolved()) {
            resultNode = FuncNode::create(*currGraph_, funcData);
        } else {
            auto funcNode = FuncNode::create(*currGraph_, funcData);
            for (const auto &ref : funcData->refs()) {
                const auto &refNode = resolveNodeByRef(ref);
                Node::link(LinkType::With, refNode, funcNode);
            }
            funcData->graph().parametrizeClosure();
            resultNode = funcNode;
        }

        usedGraphs_.insert(graph.get());
        return resultNode;
    }

    // allowParameterization = false
    if (funcData->resolved()) {
        if (callableAsResult) {
            resultNode = DataNode::create(*currGraph_, funcData);
        } else {
            auto funcNode = FuncNode::create(*currGraph_, funcData);
            if (graph->parameterized()) {
                for (const auto &ref : graph->funcType()->closureRefs()) {
                    const auto &refNode = resolveNodeByRef(ref);
                    Node::link(LinkType::With, refNode, funcNode);
                }
            }
            resultNode = funcNode;
        }

        usedGraphs_.insert(graph.get());
        return resultNode;
    }

    // funcData not resolved, while parameterization not allowed
    auto dataNode = DataNode::create(*currGraph_, funcData);
    node_vec_t refNodes;
    for (const auto &ref : funcData->refs()) {
        const auto &refNode = resolveNodeByRef(ref);
        refNodes.push_back(refNode);
    }

    auto fillNode = FillNode::create(*currGraph_, funcData->funcType());
    Node::link(LinkType::Norm, dataNode, fillNode);
    for (const auto &refNode : refNodes) {
        Node::link(LinkType::With, refNode, fillNode);
    }

    if (callableAsResult) {
        resultNode = fillNode;
    } else {
        auto callNode = CallNode::create(*currGraph_, graph->funcType()->exitType());
        Node::link(LinkType::With, fillNode, callNode);
        resultNode = callNode;
    }

    // 保证在最后再更新 usedGraphs_
    // 因为在构造过程中可能会更新图的闭包捕获
    // 而在更新闭包捕获前会检查被更新的图是否已经被使用过
    // 所以要等所有的更新完成再将此图设置为被使用过
    usedGraphs_.insert(graph.get());
    return resultNode;
}

node_ptr_t Builder::visitLinkNode(const GCT::node_ptr_t &gct) {
    ENTER("LINK");
    any targetNodeRes = visit(gct->at(0));
    ASSERT(
        targetNodeRes.type() == typeid(node_ptr_t),
        "Unexpected result type from Enter the child of LINK node.");
    node_ptr_t targetNode = any_cast<node_ptr_t>(targetNodeRes);

    graph_ptr_t targetGraph        = nullptr;
    oper_idx_ptr_t targetOperator  = nullptr;
    func_type_ptr_t targetFuncType = nullptr;
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
        } else if (dataRes.type() == typeid(node_ptr_t)) {
            node_ptr_t inputNode = any_cast<node_ptr_t>(dataRes);
            normInputNodes.push_back(inputNode);
            if (inputNode->type() == NodeType::FUNC) {
                const auto &funcNode = tt::as_shared<FuncNode>(inputNode);
                const auto &fType    = funcNode->funcType();
                normInputTypes.push_back(fType->exitType());
            } else if (inputNode->type() == NodeType::OPER) {
                const auto &operNode = tt::as_shared<OperNode>(inputNode);
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
        const auto &drefNode = tt::as_shared<DrefNode>(targetNode);
        if (std::holds_alternative<graph_vec_ptr_t>(drefNode->target())) {
            auto graphs = std::get<graph_vec_ptr_t>(drefNode->target());
            for (const auto &g : *graphs) {
                const auto &funcType = g->funcType();
                if (!funcType->hasExitType()) {
                    diags_->of(SemanticDiag::CallingIncompleteFunction)
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
                    strutil::join(
                        withInputTypes,
                        ", ",
                        [](const type_ptr_t &t) { return t->toString(); }),
                    strutil::join(normInputTypes, ", ", [](const type_ptr_t &t) {
                        return t->toString();
                    }));
                std::string overloadsStr =
                    "\n    " +
                    strutil::join(
                        *graphs,
                        "\n    ",
                        [](const graph_ptr_t &g) {
                            return g->name() + ": " + g->funcType()->toString();
                        }) +
                    "\n";
                diags_->of(SemanticDiag::NoMatchingFunction).commit(argTypesStr, overloadsStr);
                throw BuildAbortException();
            }
            currGraph_->addDependency(targetGraph);
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
                    strutil::join(
                        withInputTypes,
                        ", ",
                        [](const type_ptr_t &t) { return t->toString(); }),
                    strutil::join(normInputTypes, ", ", [](const type_ptr_t &t) {
                        return t->toString();
                    }));
                std::string overloadsStr =
                    "\n    " +
                    strutil::join(
                        ops->resolvers(),
                        "\n    ",
                        [](const std::pair<std::string, resolver_ptr_t> &p) {
                            return "<" + p.first + ">: " + p.second->signature();
                        }) +
                    "\n";
                diags_->of(SemanticDiag::NoMatchingFunction).commit(argTypesStr, overloadsStr);
                throw BuildAbortException();
            }
            targetOperator      = *res;
            node_ptr_t operNode = OperNode::create(*currGraph_, targetOperator);
            targetNode          = operNode;
            targetFuncType      = targetOperator->funcType();
        } else {
            ASSERT(false, "DrefNode must refer to a graph or an operator group.");
        }
        drefNode->detach();
    } else {
        const auto &dataType = targetNode->dataType();
        ASSERT(
            dataType->code() == TypeCode::Function,
            "Target node of LINK must be a function or operator node.");
        const auto &funcType = tt::as_shared<FunctionType>(dataType);
        StaticFuncTypeResolver resolver(funcType);
        const auto &res = resolver.resolve(withInputTypes, normInputTypes, Modifier::None);
        if (!res.has_value()) {
            std::string argTypesStr = std::format(
                "<{}> ({})",
                strutil::join(
                    withInputTypes,
                    ", ",
                    [](const type_ptr_t &t) { return t->toString(); }),
                strutil::join(normInputTypes, ", ", [](const type_ptr_t &t) {
                    return t->toString();
                }));
            diags_->of(SemanticDiag::ArgumentsMismatch).commit(funcType->toString(), argTypesStr);
            throw BuildAbortException();
        }
        node_ptr_t invokeNode = CallNode::create(*currGraph_, funcType->exitType());
        Node::link(LinkType::With, targetNode, invokeNode);
        targetNode     = invokeNode;
        targetFuncType = funcType;
    }

    ASSERT(targetFuncType != nullptr, "Target function type must be resolved.");

    std::string targetName = targetGraph
                                 ? targetGraph->name()
                                 : (targetOperator ? "<" + targetOperator->name() + ">" : "");

    for (size_t i = 0; i < withInputNodes.size(); i++) {
        const node_ptr_t &inputNode = withInputNodes[i];
        bool isVar                  = false;
        const auto &withTypes       = targetFuncType->withTypes();
        if (i < withTypes.size()) {
            isVar = withTypes[i].second;
        }
        tryRemoveCtrlLink(inputNode, targetNode);
        Node::link(LinkType::With, inputNode, targetNode);
        if (nodeModifierMap_.count(inputNode.get())) {
            node_ptr_t modifierNode = nodeModifierMap_[inputNode.get()].lock();
            if (modifierNode && linkCheek(modifierNode, targetNode)) {
                Node::link(LinkType::Ctrl, modifierNode, targetNode);
            }
        }
        if (isVar) {
            if (!waited_ && !synced_) {
                diags_->of(SemanticDiag::IgnoredSideEffect)
                    .commit(targetName + ": " + targetFuncType->toString());
            }
            // Mark this node as a modifier for the input node
            nodeModifierMap_[inputNode.get()] = targetNode;
        }
    }

    for (size_t i = 0; i < normInputNodes.size(); i++) {
        const node_ptr_t &inputNode = normInputNodes[i];
        bool isVar                  = false;
        const auto &normTypes       = targetFuncType->normTypes();
        if (i < normTypes.size()) {
            isVar = normTypes[i].second;
        }
        tryRemoveCtrlLink(inputNode, targetNode);
        Node::link(LinkType::Norm, inputNode, targetNode);
        if (nodeModifierMap_.count(inputNode.get())) {
            node_ptr_t modifierNode = nodeModifierMap_[inputNode.get()].lock();
            if (modifierNode && linkCheek(modifierNode, targetNode)) {
                Node::link(LinkType::Ctrl, modifierNode, targetNode);
            }
        }
        if (isVar) {
            if (!waited_ && !synced_) {
                diags_->of(SemanticDiag::IgnoredSideEffect)
                    .commit(targetName + ": " + targetFuncType->toString());
            }
            // Mark this node as a modifier for the input node
            nodeModifierMap_[inputNode.get()] = targetNode;
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

node_ptr_t Builder::visitWithNode(const GCT::node_ptr_t &gct) {
    ENTER("WITH");
    any targetNodeRes = visit(gct->at(0));
    ASSERT(
        targetNodeRes.type() == typeid(node_ptr_t),
        "Unexpected result type from Enter the child of WITH node.");
    node_ptr_t targetNode = any_cast<node_ptr_t>(targetNodeRes);
    vector<node_ptr_t> inputs;
    for (size_t i = 1; i < gct->size(); i++) {
        any dataRes = visit(gct->at(i));
        if (dataRes.type() == typeid(graph_ptr_t)) {
            // The subtree returned a subgraph,
            // which means that a lambda function is passed as a parameter
            graph_ptr_t subGraph = any_cast<graph_ptr_t>(dataRes);
            currGraph_->addDependency(subGraph);
            auto inputNode = createFuncDataNode(subGraph, true, false);
            inputs.push_back(inputNode);
        } else if (dataRes.type() == typeid(node_ptr_t)) {
            node_ptr_t inputNode = any_cast<node_ptr_t>(dataRes);
            inputs.push_back(inputNode);
        } else {
            ASSERT(false, std::format("Unexpected result type from the {} child of WITH node", i));
        }
    }
    for (size_t i = 0; i < inputs.size(); i++) {
        const node_ptr_t &inputNode = inputs[i];
        tryRemoveCtrlLink(inputNode, targetNode);
        Node::link(LinkType::With, inputNode, targetNode);
    }
    // with 操作不属于调用，所以这里不处理synced_和lastCalledFuncNode_
    LEAVE("WITH");
    return targetNode;
}

node_ptr_t Builder::visitAccsNode(const GCT::node_ptr_t &gct) {
    ENTER("ACCS");
    any res = visit(gct->at(0));
    ASSERT(
        res.type() == typeid(node_ptr_t),
        "Unexpected result type from Enter the child of ACCS node.");
    node_ptr_t tgtNode = any_cast<node_ptr_t>(res);
    ASSERT(tgtNode != nullptr, "Access node target is null.");
    if (!tgtNode->dataType()->isComposite()) {
        diags_->of(SemanticDiag::TypeNotIndexable).commit(tgtNode->dataType()->toString());
        throw BuildAbortException();
    }
    const auto tgtType   = tt::as_shared<CompositeType>(tgtNode->dataType());
    const auto &accsLoad = gct->loadAs<GCT::AccsLoad>();
    graph_ptr_t &graph   = currGraph_;
    // TODO: here may need inplace access to the data
    ASSERT(accsLoad->isNum(), "Access index must be a number");
    const auto &optEleType = tgtType->typeAt(accsLoad->index<size_t>());
    if (!optEleType.has_value()) {
        diags_->of(SemanticDiag::InvalidAccessIndex)
            .commit(
                accsLoad->isNum() ? std::to_string(accsLoad->index<size_t>())
                                  : accsLoad->index<std::string>());
        throw BuildAbortException();
    }
    node_ptr_t accsNode = AccsNode::create(*graph, *optEleType, accsLoad->index());
    Node::link(LinkType::Norm, tgtNode, accsNode);
    LEAVE("ACCS");
    return accsNode;
}

node_ptr_t Builder::visitBrchNode(const GCT::node_ptr_t &gct) {
    ENTER("BRCH");
    const auto &res = visit(gct->at(0));
    ASSERT(
        res.type() == typeid(node_ptr_t),
        "Unexpected result type from Enter the child of BRCH node.");
    node_ptr_t condNode = any_cast<node_ptr_t>(res);
    node_ptr_t brchNode = BrchNode::create(*currGraph_, Type::Int());
    node_ptr_t joinNode = JoinNode::create(*currGraph_, nullptr);

    type_ptr_t joinType = nullptr;

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
                res.type() == typeid(node_ptr_t),
                "Unexpected result type from visiting the case node.");
            node_ptr_t valueNode = any_cast<node_ptr_t>(res);
            Node::link(LinkType::With, valueNode, brchNode);
            caseExecNode = caseNode->atAs<GCT::ExecLoad>(1);
            break;
        }
        default:
            ASSERT(false, "Unknown case type in BRCH node.");
        }

        graph_ptr_t subGraph = enterScope(std::make_shared<FunctionType>());
        node_ptr_t resNode   = visitExecNode(caseExecNode);
        if (!subGraph->hasOutput()) {
            if (resNode) {
                subGraph->setOutput(resNode);
            } else {
                // function with no return value, setting null by default
                node_ptr_t nullNode = DataNode::create(*subGraph, Data::null());
                subGraph->setOutput(nullNode);
            }
        }
        leaveScope();

        currGraph_->addDependency(subGraph);
        type_ptr_t exitType = subGraph->funcType()->exitType();

        // 保证所有捕获的变量都在 BRCH 节点执行之前准备好
        // 这样的好处是，保证 BRCH 节点到 JOIN 节点之间的各条分支路径上
        // 所有的节点在拓扑排序时会被紧密地排列在一起
        // 而不会有其他外部节点插入到中间
        // 进而便于图调度算法的实现（可以直接跳转而不需要频繁判断）
        for (const auto &port : subGraph->closure()) {
            const auto &portNode = tt::as_shared<PortNode>(port);
            const auto &refNode  = resolveNodeByRef(portNode->name());
            if (linkCheek(refNode, brchNode)) {
                Node::link(LinkType::Ctrl, refNode, brchNode);
            }
        }

        // 在分支中，默认允许闭包参数化
        auto funcNode = createFuncDataNode(subGraph, false, true);

        if (joinType == nullptr) {
            joinType = exitType;
            tt::as_shared<JoinNode>(joinNode)->setDataType(joinType);
        } else {
            if (!exitType->equals(joinType)) {
                diags_->of(SemanticDiag::BranchReturnTypeMismatch)
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

node_ptr_t Builder::visitAnnoNode(const GCT::node_ptr_t &gct) {
    ENTER("ANNO");
    ASSERT(false, "Annotation nodes are not supported in the current version of the compiler.");
    LEAVE("ANNO");
    return nullptr;
}

node_ptr_t Builder::visitExitNode(const GCT::node_ptr_t &gct) {
    ENTER("EXIT");
    auto res = visit(gct->at(0));
    ASSERT(
        res.type() == typeid(node_ptr_t),
        "Unexpected result type from Enter child of EXIT node.");
    node_ptr_t resNode = any_cast<node_ptr_t>(res);
    currGraph_->setOutput(resNode);

    if (nodeModifierMap_.count(resNode.get())) {
        node_ptr_t modifier   = nodeModifierMap_[resNode.get()].lock();
        node_ptr_t returnNode = currGraph_->exitNode();
        if (modifier && linkCheek(modifier, returnNode)) {
            Node::link(LinkType::Ctrl, modifier, returnNode);
        }
    }

    node_ptr_t exitNode = currGraph_->exitNode();
    if (synced_ && lastSyncedNode_ && linkCheek(lastSyncedNode_, exitNode)) {
        Node::link(LinkType::Ctrl, lastSyncedNode_, exitNode);
    }

    LEAVE("EXIT");
    return resNode;
}

node_ptr_t Builder::visitExecNode(const GCT::node_ptr_t &gct) {
    ENTER("EXEC");
    const auto &execLoad   = gct->loadAs<GCT::ExecLoad>();
    bool old               = synced_;
    node_ptr_t oldFuncNode = lastSyncedNode_;
    synced_                = execLoad->synced();
    lastSyncedNode_        = nullptr;
    node_ptr_t res         = nullptr;
    for (size_t i = 0; i < gct->size(); i++) {
        try {
            any result = visit(gct->at(i));
            if (result.has_value() && result.type() == typeid(node_ptr_t)) {
                res = any_cast<node_ptr_t>(result);
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
        diags_->of(SemanticDiag::UnresolvedReference).commit(ref.toString());
        throw BuildAbortException();
    }
    LEAVE("EXPT");
    return nullptr;
}

} // namespace GraphIR
