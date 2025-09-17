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
 * Updated: Mar. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "gir_builder.h"

#include "utils/log.h"
#include "utils/scope.h"
#include "utils/type.h"

#define DEBUG_LEVEL -1

using namespace std;

namespace GraphIntermediateRepresentation {

inline bool linkCheek(const node_ptr_t &from, const node_ptr_t &to) {
    // prevent linking a node to itself
    if (from.get() == to.get()) {
        return false;
    }
    // prevent linking nodes that are already linked
    if (from->hasLinkedTo(to)) {
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
    opScope_ = opScope_->enter(name);
    return currGraph_;
}

void Builder::leaveScope() {
    nodeScope_ = nodeScope_->leave();
    graphScope_ = graphScope_->leave();
    opScope_ = opScope_->leave();
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

bool Builder::insertOperator(const std::string &name, const oper_idx_ptr_t &op) {
    if (opScope_->has(name, false)) {
        auto ops = opScope_->get(name).value();
        // TODO: check if the operator is already in the list
        ops->push_back(op);
    }
    opScope_->insert(name, std::make_shared<oper_idx_vec_t>(1, op));
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
    case GCT::LoadType::BIND:
        return visitBindNode(node);
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
    const auto &withParamsType = tt::as_shared<ParamsType>(funcType->withParamsType());
    const auto &normParamsType = tt::as_shared<ParamsType>(funcType->normParamsType());

    graph_ptr_t graph = enterScope(declLoad->ref().ident());
    graph->setFuncType(funcType);
    arena_ptr_t arena = graph->arena();
    for (const auto &[name, type, data] : withParamsType->elements()) {
        // TODO: ignored type and default data here
        if (data != nullptr) {
            diags_->of(SemanticDiag::FeatureNotSupported)
                .commit("Default data in function parameters");
        }
        insertNode(name, graph->addPort(true));
    }
    for (const auto &[name, type, data] : normParamsType->elements()) {
        // TODO: ignored type and default data here
        if (data != nullptr) {
            diags_->of(SemanticDiag::FeatureNotSupported)
                .commit("Default data in function parameters");
        }
        insertNode(name, graph->addPort(false));
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
    }
    ASSERT(graph->hasFuncType(), "Function graph must have a function type.");
    node_ptr_t res = visitExecNode(gct->atAs<GCT::ExecLoad>(1));
    if (!graph->hasOutput() && res != nullptr) {
        graph->setOutput(res);
    }
    leaveScope();
    LEAVE("FUNC");
    return graph;
}

node_ptr_t Builder::visitDataNode(const GCT::node_ptr_t &gct) {
    ENTER("DATA");
    const auto &dataLoad = gct->loadAs<GCT::DataLoad>();
    const data_ptr_t &data = dataLoad->data();
    graph_ptr_t &graph = currGraph_;
    node_ptr_t node = nullptr;
    if (data->resolved()) {
        DataIndex index = graph->addSharedConstant(data);
        node = SourceNode::create(currGraph_, index);
    } else {
        DataIndex index = graph->addRuntimeConstant(data);
        node = StructNode::create(currGraph_, index, data->type());
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
        LEAVE("DREF");
        return optNode.value();
    }
    graph_ptr_t &graph = currGraph_;
    auto optGraph = graphAt(ident);
    if (optGraph.has_value()) {
        auto graphs = optGraph.value();
        if (!graphs->empty()) {
            // TODO: generate data as the return value of a function
            DataIndex index = graph->addRuntimeConstant(nullptr);
            graph_ptr_t &tgtGraph = graphs->front();
            currGraph_->addDependency(tgtGraph);
            func_ptr_t funcData = FunctionData::create(tgtGraph);
            node_ptr_t funcNode = FunctionNode::create(graph, index, funcData);
            LEAVE("DREF");
            return funcNode;
        }
    }
    auto optOp = operatorAt(ident);
    if (optOp.has_value()) {
        auto ops = optOp.value();
        if (!ops->empty()) {
            DataIndex index = graph->addRuntimeConstant(nullptr);
            oper_idx_ptr_t &op = ops->front();
            node_ptr_t opNode = OperatorNode::create(graph, index, op);
            LEAVE("DREF");
            return opNode;
        }
    }
    if (module_->hasImportedRef(ident)) {
        const auto &e = module_->getImportedEntity(ident);
        if (std::holds_alternative<GIR::node_ptr_t>(e)) {
            LEAVE("DREF");
            return std::get<GIR::node_ptr_t>(e);
        } else if (std::holds_alternative<GIR::graph_vec_ptr_t>(e)) {
            auto graphs = std::get<graph_vec_ptr_t>(e);
            ASSERT(!graphs->empty(), "Imported graph list is empty.");
            auto tgtGraph = graphs->front();
            currGraph_->addDependency(tgtGraph);
            DataIndex index = graph->addRuntimeConstant(nullptr);
            func_ptr_t funcData = FunctionData::create(tgtGraph);
            node_ptr_t funcNode = FunctionNode::create(graph, index, funcData);
            LEAVE("DREF");
            return funcNode;
        } else if (std::holds_alternative<oper_idx_vec_ptr_t>(e)) {
            auto ops = std::get<oper_idx_vec_ptr_t>(e);
            ASSERT(!ops->empty(), "Imported operator list is empty.");
            oper_idx_ptr_t op = ops->front();
            DataIndex index = graph->addRuntimeConstant(nullptr);
            node_ptr_t opNode = OperatorNode::create(graph, index, op);
            LEAVE("DREF");
            return opNode;
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
    any funcNodeRes = visit(gct->at(0));
    ASSERT(
        funcNodeRes.type() == typeid(node_ptr_t),
        "Unexpected result type from Enter the child of LINK node.");
    node_ptr_t funcNode = any_cast<node_ptr_t>(funcNodeRes);
    std::vector<std::tuple<std::string, type_ptr_t, bool>> params;
    if (funcNode->type() == NodeType::Function) {
        func_type_ptr_t funcType = tt::as_shared<FunctionNode>(funcNode)->funcType();
        params = funcType->normParams();
    } else if (funcNode->type() == NodeType::Operator) {
        func_type_ptr_t funcType = tt::as_shared<OperatorNode>(funcNode)->funcType();
        params = funcType->normParams();
    } else {
        ASSERT(false, "LINK node must be a function or operator node.");
    }
    // TODO: check if the number of parameters matches the number of inputs
    vector<node_ptr_t> inputs;
    for (size_t i = 1; i < gct->size(); i++) {
        any dataRes = visit(gct->at(i));
        ASSERT(
            dataRes.type() == typeid(node_ptr_t),
            "Unexpected result type from Enter the child of LINK node.");
        node_ptr_t inputNode = any_cast<node_ptr_t>(dataRes);
        inputs.push_back(inputNode);
    }
    for (size_t i = 0; i < inputs.size(); i++) {
        const node_ptr_t &inputNode = inputs[i];
        bool isVar = false;
        if (i < params.size()) {
            isVar = std::get<2>(params[i]);
        }
        Node::link(LinkType::Norm, inputNode, funcNode);
        if (nodeModifierMap_.count(inputNode.get())) {
            node_ptr_t modifierNode = nodeModifierMap_[inputNode.get()].lock();
            if (modifierNode) {
                Node::link(LinkType::Ctrl, modifierNode, funcNode);
            }
        }
        if (isVar) {
            if (!waited_) {
                diags_->of(SemanticDiag::IgnoredSideEffect).commit();
            }
            nodeModifierMap_[inputNode.get()] =
                funcNode; // Mark this node as a modifier for the input node
        }
        if (synced_) {
            if (lastCalledFuncNode_ && linkCheek(lastCalledFuncNode_, funcNode)) {
                Node::link(LinkType::Ctrl, lastCalledFuncNode_, funcNode);
            }
            lastCalledFuncNode_ = funcNode;
        }
    }
    if (inputs.empty() && synced_) {
        if (lastCalledFuncNode_ && linkCheek(lastCalledFuncNode_, funcNode)) {
            Node::link(LinkType::Ctrl, lastCalledFuncNode_, funcNode);
        }
        lastCalledFuncNode_ = funcNode;
    }
    LEAVE("LINK");
    return funcNode;
}

node_ptr_t Builder::visitWithNode(const GCT::node_ptr_t &gct) {
    ENTER("WITH");
    any funcNodeRes = visit(gct->at(0));
    ASSERT(
        funcNodeRes.type() == typeid(node_ptr_t),
        "Unexpected result type from Enter the child of WITH node.");
    node_ptr_t funcNode = any_cast<node_ptr_t>(funcNodeRes);
    std::vector<std::tuple<std::string, type_ptr_t, bool>> params;
    if (funcNode->type() == NodeType::Function) {
        func_type_ptr_t funcType = tt::as_shared<FunctionNode>(funcNode)->funcType();
        params = funcType->withParams();
    } else if (funcNode->type() == NodeType::Operator) {
        func_type_ptr_t funcType = tt::as_shared<OperatorNode>(funcNode)->funcType();
        params = funcType->withParams();
    } else {
        ASSERT(false, "LINK node must be a function or operator node.");
    }
    // TODO: check if the number of parameters matches the number of inputs
    vector<node_ptr_t> inputs;
    for (size_t i = 1; i < gct->size(); i++) {
        any dataRes = visit(gct->at(i));
        ASSERT(
            dataRes.type() == typeid(node_ptr_t),
            "Unexpected result type from Enter the child of LINK node.");
        node_ptr_t inputNode = any_cast<node_ptr_t>(dataRes);
        inputs.push_back(inputNode);
    }
    for (size_t i = 0; i < inputs.size(); i++) {
        const node_ptr_t &inputNode = inputs[i];
        bool isVar = false;
        if (i < params.size()) {
            isVar = std::get<2>(params[i]);
        }
        Node::link(LinkType::With, inputNode, funcNode);
        if (nodeModifierMap_.count(inputNode.get())) {
            node_ptr_t modifierNode = nodeModifierMap_[inputNode.get()].lock();
            if (modifierNode) {
                Node::link(LinkType::Ctrl, modifierNode, funcNode);
            }
        }
        if (isVar) {
            if (!waited_) {
                diags_->of(SemanticDiag::IgnoredSideEffect).commit();
            }
            nodeModifierMap_[inputNode.get()] =
                funcNode; // Mark this node as a modifier for the input node
        }
        if (synced_) {
            if (lastCalledFuncNode_ && linkCheek(lastCalledFuncNode_, funcNode)) {
                Node::link(LinkType::Ctrl, lastCalledFuncNode_, funcNode);
            }
            lastCalledFuncNode_ = funcNode;
        }
    }
    if (inputs.empty() && synced_) {
        if (lastCalledFuncNode_ && linkCheek(lastCalledFuncNode_, funcNode)) {
            Node::link(LinkType::Ctrl, lastCalledFuncNode_, funcNode);
        }
        lastCalledFuncNode_ = funcNode;
    }
    LEAVE("WITH");
    return funcNode;
}

node_ptr_t Builder::visitBindNode(const GCT::node_ptr_t &gct) {
    ENTER("BIND");
    ASSERT(false, "Bind nodes are not supported in the current version of the compiler.");
    LEAVE("BIND");
    return nullptr;
}

node_ptr_t Builder::visitAccsNode(const GCT::node_ptr_t &gct) {
    ENTER("ACCS");
    any res = visit(gct->at(0));
    ASSERT(
        res.type() == typeid(node_ptr_t),
        "Unexpected result type from Enter the child of ACCS node.");
    node_ptr_t tgtNode = any_cast<node_ptr_t>(res);
    ASSERT(tgtNode != nullptr, "Access node target is null.");
    const auto &accsLoad = gct->loadAs<GCT::AccsLoad>();
    graph_ptr_t &graph = currGraph_;
    DataIndex index = graph->addRuntimeConstant(nullptr);
    // TODO: here may need inplace access to the data
    node_ptr_t accsNode = AccessNode::create(graph, index, accsLoad->index());
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
    node_ptr_t brchNode =
        SelectNode::create(currGraph_, condNode->index(), SelectNode::SelectType::Branch);
    Node::link(LinkType::Norm, condNode, brchNode);

    graph_ptr_t tGraph = enterScope();
    tGraph->setFuncType(std::make_shared<FunctionType>()); // TODO: set the function type properly
    node_ptr_t tNode = visitExecNode(gct->atAs<GCT::ExecLoad>(1));
    if (!tGraph->hasOutput() && tNode != nullptr) {
        tGraph->setOutput(tNode);
    }
    leaveScope();
    currGraph_->addDependency(tGraph);
    func_ptr_t tData = FunctionData::create(tGraph);
    node_ptr_t tFunc = FunctionNode::create(graph, graph->addRuntimeConstant(nullptr), tData);
    Node::link(LinkType::Ctrl, brchNode, tFunc);

    graph_ptr_t fGraph = enterScope();
    fGraph->setFuncType(std::make_shared<FunctionType>());
    node_ptr_t fNode = visitExecNode(gct->atAs<GCT::ExecLoad>(2));
    if (!fGraph->hasOutput() && fNode != nullptr) {
        fGraph->setOutput(fNode);
    }
    leaveScope();
    currGraph_->addDependency(fGraph);
    func_ptr_t fData = FunctionData::create(fGraph);
    node_ptr_t fFunc = FunctionNode::create(graph, graph->addRuntimeConstant(nullptr), fData);
    Node::link(LinkType::Ctrl, brchNode, fFunc);

    DataIndex index = graph->addRuntimeConstant(nullptr);
    node_ptr_t joinNode = SelectNode::create(graph, index, SelectNode::SelectType::Join);
    Node::link(LinkType::Ctrl, tFunc, joinNode);
    Node::link(LinkType::Ctrl, fFunc, joinNode);

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
    node_ptr_t node = any_cast<node_ptr_t>(res);
    node_ptr_t exitNode = node;
    if (nodeModifierMap_.count(node.get())) {
        exitNode = nodeModifierMap_[node.get()].lock();
    }
    if (synced_ && lastCalledFuncNode_ && linkCheek(lastCalledFuncNode_, exitNode)) {
        Node::link(LinkType::Ctrl, lastCalledFuncNode_, exitNode);
    }
    currGraph_->setOutput(exitNode);
    LEAVE("EXIT");
    return exitNode;
}

node_ptr_t Builder::visitExecNode(const GCT::node_ptr_t &gct) {
    ENTER("EXEC");
    const auto &execLoad = gct->loadAs<GCT::ExecLoad>();
    bool old = synced_;
    synced_ = execLoad->synced();
    if (synced_ && !old) {
        lastCalledFuncNode_ = nullptr;
    }
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
        auto optGraph = graphAt(ref);
        if (optGraph.has_value()) {
            module_->exportEntity(ref, optGraph.value());
            continue;
        }
        auto optOp = operatorAt(ref);
        if (optOp.has_value()) {
            module_->exportEntity(ref, optOp.value());
            continue;
        }
        diags_->of(SemanticDiag::UnresolvedReference).commit(ref.toString());
        throw BuildAbortException();
    }
    LEAVE("EXPT");
    return nullptr;
}

} // namespace GraphIntermediateRepresentation