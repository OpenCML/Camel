/**
 * Copyright (c) 2024 Beijing Jiaotong University
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
 * Updated: Oct. 22, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "gir.h"
#include "common/error/build.h"
#include "utils/log.h"

#define DEBUG_LEVEL 0

using namespace std;
using namespace gir;

inline ast::node_ptr_t ast_ptr_cast(const tree_node_ptr_t<ast::load_ptr_t> &ptr) {
    return dynamic_pointer_cast<ast::Node>(ptr);
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

any Constructor::visit(const ast::node_ptr_t &node) {
    switch (node->type()) {
    case ast::NodeType::DATA:
        return visitDataNode(node);
    case ast::NodeType::VARI:
        return visitVariNode(node);
    case ast::NodeType::TYPE:
        return visitTypeNode(node);
    case ast::NodeType::DECL:
        return visitDeclNode(node);
    case ast::NodeType::FUNC:
        return visitFuncNode(node);
    case ast::NodeType::NREF:
        return visitNRefNode(node);
    case ast::NodeType::DREF:
        return visitDRefNode(node);
    case ast::NodeType::WAIT:
        return visitWaitNode(node);
    case ast::NodeType::ANNO:
        return visitAnnoNode(node);
    case ast::NodeType::LINK:
        return visitLinkNode(node);
    case ast::NodeType::WITH:
        return visitWithNode(node);
    case ast::NodeType::RETN:
        return visitRetnNode(node);
    case ast::NodeType::EXEC:
        return visitExecNode(node);
    case ast::NodeType::FROM:
        return visitFromNode(node);
    default:
        throw runtime_error("Unknown ast::ASTNodeType");
    }
}

node_ptr_t Constructor::visitDataNode(const ast::node_ptr_t &ast) {
    enter("DATA");
    const auto &dataNode = ast::data_load_ptr_cast(ast->load());
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

node_ptr_t Constructor::visitVariNode(const ast::node_ptr_t &ast) {
    enter("VARI");
    const auto &res = visit(ast_ptr_cast(ast->childAt(0)));
    if (res.type() != typeid(node_ptr_t)) {
        throw runtime_error("Unexpected result type from Enter the child of VARI node");
    }
    node_ptr_t node = any_cast<node_ptr_t>(res);
    node->makeVariable();
    leave("VARI");
    return node;
}

type_ptr_t Constructor::visitTypeNode(const ast::node_ptr_t &ast) {
    enter("TYPE");
    const type_ptr_t &type = ast::type_load_ptr_cast(ast->load())->dataType();
    leave("TYPE");
    return type;
}

func_ptr_t Constructor::visitDeclNode(const ast::node_ptr_t &ast) {
    enter("DECL");
    func_type_ptr_t funcType = ast::decl_load_ptr_cast(ast->load())->funcType();
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

node_ptr_t Constructor::visitFuncNode(const ast::node_ptr_t &ast) {
    enter("FUNC");
    func_ptr_t func = visitDeclNode(ast_ptr_cast(ast->childAt(0)));
    func_type_ptr_t funcType = func->funcType();
    context_->pushScope(funcType);
    visitExecNode(ast_ptr_cast(ast->childAt(1)));
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

void_ptr_t Constructor::visitNRefNode(const ast::node_ptr_t &ast) {
    enter("NREF");
    const string &ident = ast::nref_load_ptr_cast(ast->load())->ident();
    if (!validateIdent(ident)) {
        throw runtime_error("Identifiers starting and ending with '__' are reserved for internal use.");
    }
    const auto &res = visit(ast_ptr_cast(ast->childAt(0)));
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

node_ptr_t Constructor::visitDRefNode(const ast::node_ptr_t &ast) {
    enter("DREF");
    const string &ident = dref_load_ptr_cast(ast->load())->ident();
    auto optNode = context_->nodeAt(ident);
    if (!optNode.has_value()) {
        throw runtime_error("Unresolved reference: " + ident);
    }
    leave("DREF");
    node_ptr_t res = optNode.value();
    res->ref();
    return res;
}

node_ptr_t Constructor::visitWaitNode(const ast::node_ptr_t &ast) { throw runtime_error("Not implemented"); }

node_ptr_t Constructor::visitAnnoNode(const ast::node_ptr_t &ast) { throw runtime_error("Not implemented"); }

node_ptr_t Constructor::visitLinkNode(const ast::node_ptr_t &ast) {
    enter("LINK");
    // TODO: consider functor and operator overriden
    // for now, we just ignore it
    // because we cannot get the exact type of unref elements of struct data yet
    any dataRes = visit(ast_ptr_cast(ast->at(0)));
    any funcRes = visit(ast_ptr_cast(ast->at(1)));
    if (dataRes.type() != typeid(node_ptr_t) || funcRes.type() != typeid(node_ptr_t)) {
        throw runtime_error("Unexpected result type from Enter children of LINK node");
    }
    node_ptr_t dataNode = selectNode(any_cast<node_ptr_t>(dataRes), context_->currGraph());
    node_ptr_t linkNode = selectNode(any_cast<node_ptr_t>(funcRes), context_->currGraph());
    Node::link(dataNode, linkNode, 1);
    leave("LINK");
    return linkNode;
}

node_ptr_t Constructor::visitWithNode(const ast::node_ptr_t &ast) {
    enter("WITH");
    any dataRes = visit(ast_ptr_cast(ast->at(0)));
    any funcRes = visit(ast_ptr_cast(ast->at(1)));
    if (dataRes.type() != typeid(node_ptr_t) || funcRes.type() != typeid(node_ptr_t)) {
        throw runtime_error("Unexpected result type from Enter children of LINK node");
    }
    node_ptr_t dataNode = selectNode(any_cast<node_ptr_t>(dataRes), context_->currGraph());
    node_ptr_t withNode = selectNode(any_cast<node_ptr_t>(funcRes), context_->currGraph());
    Node::link(dataNode, withNode, 0);
    leave("WITH");
    return withNode;
}

void_ptr_t Constructor::visitRetnNode(const ast::node_ptr_t &ast) {
    enter("RETN");
    auto res = visit(ast_ptr_cast(ast->at(0)));
    if (res.type() != typeid(node_ptr_t)) {
        throw runtime_error("Unexpected result type from Enter child of RETN node");
    }
    const auto &node = any_cast<node_ptr_t>(res);
    context_->currGraph()->setOutput(node);
    leave("RETN");
    return nullptr;
}

node_ptr_t Constructor::visitExecNode(const ast::node_ptr_t &ast) {
    enter("EXEC");
    node_ptr_t node;
    for (size_t i = 0; i < ast->size(); i++) {
        any result = visit(ast_ptr_cast(ast->at(i)));
        if (result.type() == typeid(node_ptr_t)) {
            node = any_cast<node_ptr_t>(result);
        }
    }
    leave("EXEC");
    return node;
}

void_ptr_t Constructor::visitFromNode(const ast::node_ptr_t &ast) { return nullptr; }
