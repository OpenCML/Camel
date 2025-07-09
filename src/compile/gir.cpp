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

#include "gir.h"
#include "common/error/build.h"
#include "utils/log.h"

#define DEBUG_LEVEL 0

using namespace std;
using namespace GIR;

inline GCT::node_ptr_t ast_ptr_cast(const tree_node_ptr_t<GCT::load_ptr_t> &ptr) {
    return dynamic_pointer_cast<GCT::Node>(ptr);
}

inline node_ptr_t selectNode(node_ptr_t selNode, graph_ptr_t tgtGraph) {
    node_ptr_t res = selNode;
    if (selNode->type() == NodeType::SELECT) {
        res = select_node_ptr_cast(selNode)->select(0);
        if (res->type() == NodeType::FUNCTOR) {
            res = dynamic_pointer_cast<FunctorNode>(res);
        }
    }
    return res;
}

any Constructor::visit(const GCT::node_ptr_t &node) {
    switch (node->type()) {
    case GCT::NodeType::DATA:
        return visitDataNode(node);
    case GCT::NodeType::VARI:
        return visitVariNode(node);
    case GCT::NodeType::TYPE:
        return visitTypeNode(node);
    case GCT::NodeType::DECL:
        return visitDeclNode(node);
    case GCT::NodeType::FUNC:
        return visitFuncNode(node);
    case GCT::NodeType::NREF:
        return visitNRefNode(node);
    case GCT::NodeType::DREF:
        return visitDRefNode(node);
    case GCT::NodeType::WAIT:
        return visitWaitNode(node);
    case GCT::NodeType::ANNO:
        return visitAnnoNode(node);
    case GCT::NodeType::LINK:
        return visitLinkNode(node);
    case GCT::NodeType::WITH:
        return visitWithNode(node);
    case GCT::NodeType::EXIT:
        return visitRetnNode(node);
    case GCT::NodeType::EXEC:
        return visitExecNode(node);
    case GCT::NodeType::FROM:
        return visitFromNode(node);
    default:
        throw runtime_error("Unknown gct::ASTNodeType");
    }
}

node_ptr_t Constructor::visitDataNode(const GCT::node_ptr_t &gct) {
    enter("DATA");
    const auto &dataNode = GCT::data_load_ptr_cast(gct->load());
    const data_ptr_t &data = dataNode->data();
    node_ptr_t node = DataNode::create(context_->currGraph(), data, false);
    if (!data->resolved()) {
        for (const string &ref : data->refs()) {
            auto optSrcNode = context_->nodeAt(ref);
            if (optSrcNode.has_value()) {
                node_ptr_t srcNode = selectNode(optSrcNode.value(), context_->currGraph());
                Node::link(srcNode, node);
            } else {
                throw runtime_error("Unresolved reference: " + ref);
            }
        }
    }
    leave("DATA");
    return node;
}

node_ptr_t Constructor::visitVariNode(const GCT::node_ptr_t &gct) {
    enter("VARI");
    const auto &res = visit(ast_ptr_cast(gct->childAt(0)));
    if (res.type() != typeid(node_ptr_t)) {
        throw runtime_error("Unexpected result type from Enter the child of VARI node");
    }
    node_ptr_t node = any_cast<node_ptr_t>(res);
    node->makeVariable();
    leave("VARI");
    return node;
}

type_ptr_t Constructor::visitTypeNode(const GCT::node_ptr_t &gct) {
    enter("TYPE");
    const type_ptr_t &type = GCT::type_load_ptr_cast(gct->load())->dataType();
    leave("TYPE");
    return type;
}

func_ptr_t Constructor::visitDeclNode(const GCT::node_ptr_t &gct) {
    enter("DECL");
    func_type_ptr_t funcType = GCT::decl_load_ptr_cast(gct->load())->funcType();
    if (context_->cached(funcType)) {
        leave("DECL");
        return getCachedFunc(funcType);
    }
    auto functorType = dynamic_pointer_cast<FunctorType>(funcType);
    const auto &varMap = functorType->variableMap();
    const auto &withType = dynamic_pointer_cast<ParamsType>(functorType->withType());
    const auto &linkType = dynamic_pointer_cast<ParamsType>(functorType->linkType());

    context_->pushScope(funcType);
    graph_ptr_t &graph = context_->currGraph();
    for (const auto &[name, type, data] : withType->elements()) {
        node_ptr_t node = graph->addPort(varMap.at(name));
        context_->insertNode(name, node);
    }
    for (const auto &[name, type, data] : linkType->elements()) {
        node_ptr_t node = graph->addPort(varMap.at(name));
        context_->insertNode(name, node);
    }
    func_ptr_t func = make_shared<FunctorData>(funcType, graph);
    graph->setFuncType(funcType);
    context_->popScope();

    if (!functorType->name().empty()) {
        // lambda functors my not have a name
        context_->insertFunc(funcType->name(), func);
    }
    cacheFunc(funcType, func);
    leave("DECL");
    return func;
}

node_ptr_t Constructor::visitFuncNode(const GCT::node_ptr_t &gct) {
    enter("FUNC");
    func_ptr_t func = visitDeclNode(ast_ptr_cast(gct->childAt(0)));
    func_type_ptr_t funcType = func->funcType();
    context_->pushScope(funcType);
    visitExecNode(ast_ptr_cast(gct->childAt(1)));
    context_->popScope(funcType);
    delCachedFunc(funcType);
    leave("FUNC");
    return SelectNode::create(context_->currGraph(), func_vec_t{func});
}

inline bool validateIdent(const std::string &str) {
    if (str.length() < 4) {
        return true;
    }
    return !(str.substr(0, 2) == "__" && str.substr(str.length() - 2) == "__");
}

void_ptr_t Constructor::visitNRefNode(const GCT::node_ptr_t &gct) {
    enter("NREF");
    const string &ident = GCT::nref_load_ptr_cast(gct->load())->ident();
    if (!validateIdent(ident)) {
        throw runtime_error("Identifiers starting and ending with '__' are reserved for internal use.");
    }
    const auto &res = visit(ast_ptr_cast(gct->childAt(0)));
    if (res.type() != typeid(node_ptr_t)) {
        throw runtime_error("Unexpected result type from Enter the child of NREF node.");
    }
    node_ptr_t node = any_cast<node_ptr_t>(res);
    if (!context_->insertNode(ident, node)) {
        throw runtime_error("Redeclaration of entity: " + ident);
    }
    leave("NREF");
    return nullptr;
}

node_ptr_t Constructor::visitDRefNode(const GCT::node_ptr_t &gct) {
    enter("DREF");
    const string &ident = dref_load_ptr_cast(gct->load())->ident();
    auto optNode = context_->nodeAt(ident);
    if (!optNode.has_value()) {
        throw runtime_error("Unresolved reference: " + ident);
    }
    leave("DREF");
    node_ptr_t res = optNode.value();
    res->ref();
    return res;
}

node_ptr_t Constructor::visitWaitNode(const GCT::node_ptr_t &gct) { throw runtime_error("Not implemented"); }

node_ptr_t Constructor::visitAnnoNode(const GCT::node_ptr_t &gct) { throw runtime_error("Not implemented"); }

node_ptr_t Constructor::visitLinkNode(const GCT::node_ptr_t &gct) {
    enter("LINK");
    // TODO: consider functor and operator overriden
    // for now, we just ignore it
    // because we cannot get the exact type of unref elements of struct data yet
    any dataRes = visit(ast_ptr_cast(gct->at(0)));
    any funcRes = visit(ast_ptr_cast(gct->at(1)));
    if (dataRes.type() != typeid(node_ptr_t) || funcRes.type() != typeid(node_ptr_t)) {
        throw runtime_error("Unexpected result type from Enter children of LINK node");
    }
    node_ptr_t dataNode = selectNode(any_cast<node_ptr_t>(dataRes), context_->currGraph());
    node_ptr_t linkNode = selectNode(any_cast<node_ptr_t>(funcRes), context_->currGraph());
    Node::link(dataNode, linkNode, 1);
    leave("LINK");
    return linkNode;
}

node_ptr_t Constructor::visitWithNode(const GCT::node_ptr_t &gct) {
    enter("WITH");
    any dataRes = visit(ast_ptr_cast(gct->at(0)));
    any funcRes = visit(ast_ptr_cast(gct->at(1)));
    if (dataRes.type() != typeid(node_ptr_t) || funcRes.type() != typeid(node_ptr_t)) {
        throw runtime_error("Unexpected result type from Enter children of LINK node");
    }
    node_ptr_t dataNode = selectNode(any_cast<node_ptr_t>(dataRes), context_->currGraph());
    node_ptr_t withNode = selectNode(any_cast<node_ptr_t>(funcRes), context_->currGraph());
    Node::link(dataNode, withNode, 0);
    leave("WITH");
    return withNode;
}

void_ptr_t Constructor::visitRetnNode(const GCT::node_ptr_t &gct) {
    enter("RETN");
    auto res = visit(ast_ptr_cast(gct->at(0)));
    if (res.type() != typeid(node_ptr_t)) {
        throw runtime_error("Unexpected result type from Enter child of RETN node");
    }
    const auto &node = any_cast<node_ptr_t>(res);
    context_->currGraph()->setOutput(node);
    leave("RETN");
    return nullptr;
}

node_ptr_t Constructor::visitExecNode(const GCT::node_ptr_t &gct) {
    enter("EXEC");
    node_ptr_t node;
    for (size_t i = 0; i < gct->size(); i++) {
        any result = visit(ast_ptr_cast(gct->at(i)));
        if (result.type() == typeid(node_ptr_t)) {
            node = any_cast<node_ptr_t>(result);
        }
    }
    leave("EXEC");
    return node;
}

void_ptr_t Constructor::visitFromNode(const GCT::node_ptr_t &gct) { return nullptr; }
