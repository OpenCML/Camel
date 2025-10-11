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
 * Updated: Oct. 11, 2025
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

graph_ptr_t Builder::enterScope(const std::string &name) {
    if (name.empty()) {
        currGraph_ = Graph::create(currGraph_);
    } else {
        auto graphs = graphScope_->get(name);
        if (graphs.has_value() && !graphs.value()->empty()) {
            currGraph_ = graphs.value()->front();
        } else {
            currGraph_ = Graph::create(currGraph_, name);
            insertGraph(name, currGraph_);
        }
    }
    nodeScope_ = nodeScope_->enter(name);
    graphScope_ = graphScope_->enter(name);
    return currGraph_;
}

void Builder::leaveScope() {
    nodeScope_ = nodeScope_->leave();
    graphScope_ = graphScope_->leave();
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

node_ptr_t Builder::resolveNodeByRef(const std::string &name) {
    auto optSrcNode = nodeAt(name);
    if (!optSrcNode.has_value()) {
        diags_->of(SemanticDiag::UnresolvedReference).commit(name);
        throw BuildAbortException();
    }
    return optSrcNode.value();
}

void Builder::setGraphOutputAndExitType(const graph_ptr_t &graph, const node_ptr_t &node) {
    type_ptr_t actualExitType = node->dataType();
    func_type_ptr_t funcType = graph->funcType();
    if (funcType->hasExitType()) {
        type_ptr_t declaredExitType = graph->funcType()->exitType();
        if (!actualExitType->assignable(declaredExitType)) {
            diags_->of(SemanticDiag::ReturnTypeMismatch)
                .commit(
                    actualExitType->toString(),
                    declaredExitType->toString(),
                    graph->name() + ": " + graph->funcType()->toString());
            throw BuildAbortException();
        }
    } else {
        // If the function has no declared return type, set it to the actual return type
        graph->funcType()->setExitType(actualExitType);
    }
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
    type_ptr_t type = typeNode->loadAs<GCT::TypeLoad>()->dataType();
    func_type_ptr_t funcType = tt::as_shared<FunctionType>(type);

    graph_ptr_t graph = enterScope(declLoad->ref().ident());
    graph->setFuncType(funcType);
    for (const auto &[name, type, isVar] : funcType->withArgsInfo()) {
        node_ptr_t portNode = PortNode::create(*graph, type, name, isVar);
        graph->addPort(portNode, true);
        insertNode(name, portNode);
    }
    for (const auto &[name, type, isVar] : funcType->normArgsInfo()) {
        node_ptr_t portNode = PortNode::create(*graph, type, name, isVar);
        graph->addPort(portNode, false);
        insertNode(name, portNode);
    }
    leaveScope();

    LEAVE("DECL");
    return nullptr;
}

graph_ptr_t Builder::visitFuncNode(const GCT::node_ptr_t &gct) {
    ENTER("FUNC");
    // type_ptr_t type = visitTypeNode(gct->atAs<GCT::TypeLoad>(0));
    std::string name = gct->loadAs<GCT::FuncLoad>()->name();
    GCT::node_ptr_t typeLoad = gct->atAs<GCT::TypeLoad>(0);
    graph_ptr_t graph = enterScope(name);
    if (!graph->hasFuncType()) {
        // TODO: 非全局函数不会生成前置DECL节点，因此不会预设函数类型信息，需要从FUNC节点中获取
        type_ptr_t type = typeLoad->loadAs<GCT::TypeLoad>()->dataType();
        func_type_ptr_t funcType = tt::as_shared<FunctionType>(type);
        graph->setFuncType(funcType);
        for (const auto &[name, type, isVar] : funcType->withArgsInfo()) {
            node_ptr_t portNode = PortNode::create(*graph, type, name, isVar);
            graph->addPort(portNode, true);
            insertNode(name, portNode);
        }
        for (const auto &[name, type, isVar] : funcType->normArgsInfo()) {
            node_ptr_t portNode = PortNode::create(*graph, type, name, isVar);
            graph->addPort(portNode, false);
            insertNode(name, portNode);
        }
    }
    ASSERT(graph->hasFuncType(), "Function graph must have a function type.");
    node_ptr_t res = visitExecNode(gct->atAs<GCT::ExecLoad>(1));
    if (!graph->hasOutput()) {
        if (res) {
            setGraphOutputAndExitType(graph, res);
        } else {
            // function with no return value, setting null by default
            node_ptr_t resNode = DataNode::create(*graph, Data::null());
            setGraphOutputAndExitType(graph, resNode);
        }
    }
    leaveScope();
    LEAVE("FUNC");
    return graph;
}

node_ptr_t Builder::visitDataNode(const GCT::node_ptr_t &gct) {
    ENTER("DATA");
    const auto &dataLoad = gct->loadAs<GCT::DataLoad>();
    const data_ptr_t &data = dataLoad->data();
    node_ptr_t node = nullptr;
    if (data->resolved()) {
        node = DataNode::create(*currGraph_, data);
        if (varied_ && currGraph_->outer() != nullptr) {
            // If it is a global variable, no longer maintain a copy
            // For local variables, still need to create a new copy for each call
            // The mechanism of local shared variables is yet to be designed
            node_ptr_t copyNode = CopyNode::create(*currGraph_, data->type());
            Node::link(LinkType::With, node, copyNode);
            node = copyNode;
        }
    } else {
        node_ptr_t srcNode = DataNode::create(*currGraph_, data);
        // Here, there is no need to use a copy node to handle the varied case,
        // because the Fill node itself can be modified during runtime.
        node = FillNode::create(*currGraph_, data->type());
        Node::link(LinkType::With, srcNode, node);
        for (const string &ref : data->refs()) {
            Node::link(LinkType::Norm, resolveNodeByRef(ref), node);
        }
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
    const auto &res = visit(gct->at(0));
    ASSERT(
        res.type() == typeid(node_ptr_t),
        "Unexpected result type from Enter the child of NREF node.");
    node_ptr_t node = any_cast<node_ptr_t>(res);
    bool success = insertNode(ident, node);
    if (!success) {
        diags_->of(SemanticDiag::Redeclaration).commit(ident);
        throw BuildAbortException();
    }
    LEAVE("NREF");
    return node;
}

node_ptr_t Builder::visitDRefNode(const GCT::node_ptr_t &gct) {
    ENTER("DREF");
    const string &ident = gct->loadAs<GCT::DRefLoad>()->ref();
    auto optNode = nodeAt(ident);
    if (optNode.has_value()) {
        const auto &node = optNode.value();
        LEAVE("DREF");
        return node;
    }
    graph_ptr_t &graph = currGraph_;
    auto optGraphs = graphsAt(ident);
    if (optGraphs.has_value()) {
        auto graphs = optGraphs.value();
        if (!graphs->empty()) {
            node_ptr_t drefNode = DrefNode::create(*graph, graphs);
            LEAVE("DREF");
            return drefNode;
        }
    }
    if (module_->hasImportedRef(ident)) {
        const auto &e = module_->getImportedEntity(ident);
        if (std::holds_alternative<node_ptr_t>(e)) {
            const auto &node = std::get<node_ptr_t>(e);
            LEAVE("DREF");
            return node;
        } else if (std::holds_alternative<graph_vec_ptr_t>(e)) {
            auto graphs = std::get<graph_vec_ptr_t>(e);
            node_ptr_t drefNode = DrefNode::create(*graph, graphs);
            LEAVE("DREF");
            return drefNode;
        } else if (std::holds_alternative<oper_group_ptr_t>(e)) {
            auto ops = std::get<oper_group_ptr_t>(e);
            node_ptr_t drefNode = DrefNode::create(*graph, ops);
            LEAVE("DREF");
            return drefNode;
        }
    }
    diags_->of(SemanticDiag::UnresolvedReference).commit(ident);
    throw BuildAbortException();
}

node_ptr_t Builder::visitVariNode(const GCT::node_ptr_t &gct) {
    ENTER("VARI");
    bool old = varied_;
    varied_ = true;
    const auto &res = visit(gct->at(0));
    ASSERT(
        res.type() == typeid(node_ptr_t),
        "Unexpected result type from Enter the child of VARI node.");
    node_ptr_t node = any_cast<node_ptr_t>(res);
    varied_ = old;
    LEAVE("VARI");
    return node;
}

node_ptr_t Builder::visitWaitNode(const GCT::node_ptr_t &gct) {
    ENTER("WAIT");
    bool old = waited_;
    waited_ = true;
    const auto &res = visit(gct->at(0));
    ASSERT(
        res.type() == typeid(node_ptr_t),
        "Unexpected result type from Enter the child of WAIT node.");
    node_ptr_t node = any_cast<node_ptr_t>(res);
    waited_ = old;
    LEAVE("WAIT");
    return node;
}

node_ptr_t Builder::visitLinkNode(const GCT::node_ptr_t &gct) {
    ENTER("LINK");
    any targetNodeRes = visit(gct->at(0));
    ASSERT(
        targetNodeRes.type() == typeid(node_ptr_t),
        "Unexpected result type from Enter the child of LINK node.");
    node_ptr_t targetNode = any_cast<node_ptr_t>(targetNodeRes);

    graph_ptr_t targetGraph = nullptr;
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
            auto funcData = FunctionData::create(*inputGraph);
            node_ptr_t inputNode = DataNode::create(*currGraph_, funcData);
            for (const auto &inputCaptureNode : inputGraph->capture()) {
                if (inputCaptureNode->type() != NodeType::DATA &&
                    inputCaptureNode->type() != NodeType::PORT) {
                    Node::link(LinkType::Ctrl, inputCaptureNode, inputNode);
                }
            }
            normInputNodes.push_back(inputNode);
            normInputTypes.push_back(funcData->funcType());
        } else if (dataRes.type() == typeid(node_ptr_t)) {
            node_ptr_t inputNode = any_cast<node_ptr_t>(dataRes);
            normInputNodes.push_back(inputNode);
            if (inputNode->type() == NodeType::FUNC) {
                const auto &funcNode = tt::as_shared<FuncNode>(inputNode);
                const auto &fType = funcNode->funcType();
                normInputTypes.push_back(fType->exitType());
            } else if (inputNode->type() == NodeType::OPER) {
                const auto &operNode = tt::as_shared<OperNode>(inputNode);
                const auto &fType = operNode->funcType();
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
                StaticFuncTypeResolver resolver(g->funcType());
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
                std::string overloadsStr = strutil::join(*graphs, "; ", [](const graph_ptr_t &g) {
                    return g->name() + ": " + g->funcType()->toString();
                });
                diags_->of(SemanticDiag::NoMatchingFunction).commit(argTypesStr, overloadsStr);
                throw BuildAbortException();
            }
            currGraph_->addDependency(targetGraph);
            func_ptr_t funcData = FunctionData::create(*targetGraph);
            node_ptr_t funcNode = FuncNode::create(*currGraph_, funcData);
            targetNode = funcNode;
            targetFuncType = targetGraph->funcType();
        } else if (std::holds_alternative<oper_group_ptr_t>(drefNode->target())) {
            auto ops = std::get<oper_group_ptr_t>(drefNode->target());
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
                std::string overloadsStr = strutil::join(
                    ops->resolvers(),
                    "; ",
                    [](const std::pair<std::string, resolver_ptr_t> &p) {
                        return "<" + p.first + ">: " + p.second->signature();
                    });
                diags_->of(SemanticDiag::NoMatchingFunction).commit(argTypesStr, overloadsStr);
                throw BuildAbortException();
            }
            oper_idx_ptr_t oper = *res;
            node_ptr_t operNode = OperNode::create(*currGraph_, oper);
            targetNode = operNode;
            targetFuncType = oper->funcType();
        } else {
            ASSERT(false, "DrefNode must refer to a graph or an operator group.");
        }
        drefNode->detach();
    } else {
        const auto &dataType = targetNode->dataType();
        ASSERT(
            dataType->code() == TypeCode::Func,
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
        targetNode = invokeNode;
        targetFuncType = funcType;
    }

    ASSERT(targetFuncType != nullptr, "Target function type must be resolved.");

    if (targetGraph) {
        for (const auto &capNode : targetGraph->capture()) {
            if (capNode->type() != NodeType::DATA && capNode->type() != NodeType::PORT) {
                Node::link(LinkType::Ctrl, capNode, targetNode);
            }
        }
    }

    for (size_t i = 0; i < withInputNodes.size(); i++) {
        const node_ptr_t &inputNode = withInputNodes[i];
        bool isVar = false;
        const auto &withTypes = targetFuncType->withTypes();
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
            if (!waited_) {
                diags_->of(SemanticDiag::IgnoredSideEffect).commit();
            }
            // Mark this node as a modifier for the input node
            nodeModifierMap_[inputNode.get()] = targetNode;
        }
    }

    for (size_t i = 0; i < normInputNodes.size(); i++) {
        const node_ptr_t &inputNode = normInputNodes[i];
        bool isVar = false;
        const auto &normTypes = targetFuncType->normTypes();
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
            if (!waited_) {
                diags_->of(SemanticDiag::IgnoredSideEffect).commit();
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
            auto funcData = FunctionData::create(*subGraph);
            node_ptr_t inputNode =
                DataNode::create(*currGraph_, std::static_pointer_cast<Data>(funcData));
            for (const auto &capNode : subGraph->capture()) {
                if (capNode->type() != NodeType::DATA && capNode->type() != NodeType::PORT) {
                    Node::link(LinkType::Ctrl, capNode, inputNode);
                }
            }
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
    if (!tgtNode->dataType()->structured()) {
        diags_->of(SemanticDiag::TypeNotIndexable).commit(tgtNode->dataType()->toString());
        throw BuildAbortException();
    }
    const auto tgtType = tt::as_shared<StructType>(tgtNode->dataType());
    const auto &accsLoad = gct->loadAs<GCT::AccsLoad>();
    graph_ptr_t &graph = currGraph_;
    // TODO: here may need inplace access to the data
    const auto &optEleType = tgtType->typeAt(accsLoad->index());
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
    graph_ptr_t graph = currGraph_;
    const auto &res = visit(gct->at(0));
    ASSERT(
        res.type() == typeid(node_ptr_t),
        "Unexpected result type from Enter the child of BRCH node.");
    node_ptr_t condNode = any_cast<node_ptr_t>(res);
    node_ptr_t brchNode = BrchNode::create(*currGraph_, Type::Int32());
    node_ptr_t joinNode = JoinNode::create(*graph, nullptr);

    type_ptr_t joinType = nullptr;

    Node::link(LinkType::With, condNode, brchNode);

    for (size_t i = 1; i < gct->size(); i++) {
        const auto &caseNode = gct->atAs<GCT::CaseLoad>(i);
        const auto &caseLoad = caseNode->loadAs<GCT::CaseLoad>();
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
            Node::link(LinkType::Norm, valueNode, brchNode);
            caseExecNode = caseNode->atAs<GCT::ExecLoad>(1);
            break;
        }
        default:
            ASSERT(false, "Unknown case type in BRCH node.");
        }

        graph_ptr_t subGraph = enterScope();
        subGraph->setFuncType(
            std::make_shared<FunctionType>(param_init_list_t{}, param_init_list_t{}, nullptr));
        node_ptr_t res = visitExecNode(caseExecNode);
        if (!subGraph->hasOutput()) {
            if (res) {
                setGraphOutputAndExitType(subGraph, res);
            } else {
                // function with no return value, setting null by default
                node_ptr_t resNode = DataNode::create(*subGraph, Data::null());
                setGraphOutputAndExitType(subGraph, resNode);
            }
        }
        leaveScope();

        currGraph_->addDependency(subGraph);
        func_ptr_t funcData = FunctionData::create(*subGraph);
        type_ptr_t exitType = funcData->funcType()->exitType();
        node_ptr_t funcNode = FuncNode::create(*graph, funcData);

        if (joinType == nullptr) {
            joinType = exitType;
            tt::as_shared<JoinNode>(joinNode)->setDataType(joinType);
        } else {
            if (!exitType->equals(joinType)) {
                diags_->of(SemanticDiag::BranchReturnTypeMismatch)
                    .commit(joinType->toString(), exitType->toString());
                throw BuildAbortException();
            }
        }

        for (const auto &capNode : subGraph->capture()) {
            if (capNode->type() != NodeType::DATA && capNode->type() != NodeType::PORT) {
                Node::link(LinkType::Ctrl, capNode, funcNode);
            }
        }

        Node::link(LinkType::Ctrl, brchNode, funcNode);
        Node::link(LinkType::Ctrl, funcNode, joinNode);
    }

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
    setGraphOutputAndExitType(currGraph_, resNode);

    if (nodeModifierMap_.count(resNode.get())) {
        node_ptr_t modifier = nodeModifierMap_[resNode.get()].lock();
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
    const auto &execLoad = gct->loadAs<GCT::ExecLoad>();
    bool old = synced_;
    node_ptr_t oldFuncNode = lastSyncedNode_;
    synced_ = execLoad->synced();
    lastSyncedNode_ = nullptr;
    node_ptr_t res = nullptr;
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
    synced_ = old;
    lastSyncedNode_ = oldFuncNode;
    LEAVE("EXEC");
    return res;
}

void_ptr_t Builder::visitExptNode(const GCT::node_ptr_t &gct) {
    ENTER("EXPT");
    const auto &exptLoad = gct->loadAs<GCT::ExptLoad>();
    const auto &exports = exptLoad->exports();
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
