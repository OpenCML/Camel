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
 * Updated: Oct. 18, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "gir.h"
#include "common/error/build.h"
#include "utils/log.h"

#define DEBUG_LEVEL 1

using namespace std;
using namespace gir;

inline ast::node_ptr_t ast_ptr_cast(const tree_node_ptr_t<ast::load_ptr_t> &ptr) {
    return dynamic_pointer_cast<ast::Node>(ptr);
}

any Constructor::visit(const ast::node_ptr_t &node) {
    switch (node->type()) {
    case ast::NodeType::DATA:
        return visitDataNode(node);
    case ast::NodeType::VARI:
        return visitVariNode(node);
    case ast::NodeType::TYPE:
        return visitTypeNode(node);
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
    debug(0) << "Visiting DATA node" << endl;
    const auto &dataNode = ast::data_load_ptr_cast(ast->load());
    const data_ptr_t &data = dataNode->data();
    node_ptr_t node = DataNode::create(context_->graph(), data, false);
    if (!data->resolved()) {
        for (const string &ref : data->refs()) {
            auto optSrcNode = context_->nodeAt(ref);
            if (optSrcNode.has_value()) {
                Node::link(optSrcNode.value(), node);
            } else {
                throw runtime_error("Unresolved reference: " + ref);
            }
        }
    }
    return node;
}

node_ptr_t Constructor::visitVariNode(const ast::node_ptr_t &ast) {
    debug(0) << "Visiting VARI node" << endl;
    const auto &res = visit(ast_ptr_cast(ast->childAt(0)));
    if (res.type() != typeid(node_ptr_t)) {
        throw runtime_error("Unexpected result type from visiting the child of VARI node");
    }
    node_ptr_t node = any_cast<node_ptr_t>(res);
    node->makeVariable();
    return node;
}

type_ptr_t Constructor::visitTypeNode(const ast::node_ptr_t &ast) {
    debug(0) << "Visiting TYPE node" << endl;
    const type_ptr_t &type = ast::type_load_ptr_cast(ast->load())->type();
    return type;
}

node_ptr_t Constructor::visitFuncNode(const ast::node_ptr_t &ast) {
    debug(0) << "Visiting FUNC node" << endl;
    context_->pushScope();
    const auto &type = visitTypeNode(ast_ptr_cast(ast->childAt(0)));
    // TODO: addPort
    visitExecNode(ast_ptr_cast(ast->childAt(1)));
    func_ptr_t func = make_shared<FunctorData>(type, context_->graph());
    const auto &funcNode = gir::FunctorNode::create(context_->graph(), func);
    context_->popScope();
    return funcNode;
}

void_ptr_t Constructor::visitNRefNode(const ast::node_ptr_t &ast) {
    debug(0) << "Visiting NREF node" << endl;
    const string &ident = ast::nref_load_ptr_cast(ast->load())->ident();
    const auto &res = visit(ast_ptr_cast(ast->childAt(0)));
    if (res.type() != typeid(node_ptr_t)) {
        throw runtime_error("Unexpected result type from visiting the child of NREF node");
    }
    node_ptr_t node = any_cast<node_ptr_t>(res);
    if (node->type() == gir::NodeType::FUNCTOR) {
        if (!context_->insertFunc(ident, node)) {
            throw runtime_error("Redeclaration of functor: " + ident);
        }
    } else {
        if (!context_->insertData(ident, node)) {
            throw runtime_error("Redeclaration of entity: " + ident);
        }
    }
    return nullptr;
}

node_ptr_t Constructor::visitDRefNode(const ast::node_ptr_t &ast) {
    debug(0) << "Visiting DREF node" << endl;
    const string &ident = dref_load_ptr_cast(ast->load())->ident();
    auto optNode = context_->nodeAt(ident);
    if (optNode.has_value()) {
        return optNode.value();
    }
    throw runtime_error("Unresolved reference: " + ident);
    return nullptr;
}

node_ptr_t Constructor::visitWaitNode(const ast::node_ptr_t &ast) { throw runtime_error("Not implemented"); }

node_ptr_t Constructor::visitAnnoNode(const ast::node_ptr_t &ast) { throw runtime_error("Not implemented"); }

node_ptr_t Constructor::visitLinkNode(const ast::node_ptr_t &ast) {
    debug(0) << "Visiting LINK node" << endl;
    // TODO: consider functor and operator overriden
    // for now, we just ignore it
    // because we cannot get the exact type of unref elements of struct data yet
    any dataRes = visit(ast_ptr_cast(ast->at(0)));
    any funcRes = visit(ast_ptr_cast(ast->at(1)));
    if (dataRes.type() != typeid(node_ptr_t) || funcRes.type() != typeid(node_ptr_t)) {
        throw runtime_error("Unexpected result type from visiting children of LINK node");
    }
    node_ptr_t dataNode = any_cast<node_ptr_t>(dataRes);
    node_ptr_t linkNode = any_cast<node_ptr_t>(funcRes);
    node_ptr_t funcNode = nullptr;
    switch (linkNode->type()) {
    case NodeType::FUNCTOR:
        funcNode = linkNode;
        break;
    case NodeType::OPERATOR:
        funcNode = linkNode;
        break;
    case NodeType::SELECT:
        funcNode = select_node_ptr_cast(linkNode)->caseAt(0);
        break;

    default:
        throw runtime_error("Unexpected node type of LINK node");
    }
    Node::link(dataNode, funcNode, 1);
    return funcNode;
}

node_ptr_t Constructor::visitWithNode(const ast::node_ptr_t &ast) {
    debug(0) << "Visiting WITH node" << endl;
    any dataRes = visit(ast_ptr_cast(ast->at(0)));
    any funcRes = visit(ast_ptr_cast(ast->at(1)));
    if (dataRes.type() != typeid(node_ptr_t) || funcRes.type() != typeid(node_ptr_t)) {
        throw runtime_error("Unexpected result type from visiting children of LINK node");
    }
    node_ptr_t dataNode = any_cast<node_ptr_t>(dataRes);
    node_ptr_t withNode = any_cast<node_ptr_t>(funcRes);
    node_ptr_t funcNode = nullptr;
    switch (withNode->type()) {
    case NodeType::FUNCTOR:
        funcNode = withNode;
        break;
    case NodeType::OPERATOR:
        funcNode = withNode;
        break;
    case NodeType::SELECT:
        funcNode = select_node_ptr_cast(withNode)->caseAt(0);
        break;

    default:
        throw runtime_error("Unexpected node type of WITH node");
    }
    Node::link(dataNode, funcNode, 0);
    return funcNode;
}

void_ptr_t Constructor::visitRetnNode(const ast::node_ptr_t &ast) {
    debug(0) << "Visiting RETN node" << endl;
    auto res = visit(ast_ptr_cast(ast->at(0)));
    if (res.type() != typeid(node_ptr_t)) {
        throw runtime_error("Unexpected result type from visiting child of RETN node");
    }
    const auto &node = any_cast<node_ptr_t>(res);
    context_->graph()->setOutput(node);
    return nullptr;
}

node_ptr_t Constructor::visitExecNode(const ast::node_ptr_t &ast) {
    debug(0) << "Visiting EXEC node" << endl;
    node_ptr_t node;
    for (size_t i = 0; i < ast->size(); i++) {
        any result = visit(ast_ptr_cast(ast->at(i)));
        if (result.type() == typeid(node_ptr_t)) {
            node = any_cast<node_ptr_t>(result);
        }
    }
    return node;
}

void_ptr_t Constructor::visitFromNode(const ast::node_ptr_t &ast) { return nullptr; }
