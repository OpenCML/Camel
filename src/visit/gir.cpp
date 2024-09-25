/**
 * Copyright (c) 2022 Beijing Jiaotong University
 * PhotLab is licensed under [Open Source License].
 * You can use this software according to the terms and conditions of the [Open
 * Source License]. You may obtain a copy of [Open Source License] at:
 * [https://open.source.license/]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the [Open Source License] for more details.
 *
 * Author: Zhenjie Wei
 * Created: Aug. 17, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "gir.h"
#include "core/error/build.h"
#include "core/struct/functor.h"

using namespace std;
using namespace gir;

inline ast::node_ptr_t ast_ptr_cast(const tree_node_ptr_t<ast::load_ptr_t> &ptr) {
    return dynamic_pointer_cast<ast::Node>(ptr);
}

any Constructor::visit(const ast::node_ptr_t &node) {
    // TODO: implement
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
    const auto &dataNode = dynamic_pointer_cast<ast::DataLoad>(ast);
    const data_ptr_t &data = dataNode->data();
    node_ptr_t node = make_shared<DataNode>(data);
    if (dataNode->resolved()) {
        return node;
    } else {
        for (const auto &e : dataNode->getUnrefData()) {
            const string &ref = dynamic_pointer_cast<RefData>(e)->ref();
            auto optSrcNode = nodeScope_->at(ref);
            if (optSrcNode.has_value()) {
                Node::link(optSrcNode.value(), node);
            } else {
                throw runtime_error("Unresolved reference: " + ref);
            }
        }
        return node;
    }
}

node_ptr_t Constructor::visitVariNode(const ast::node_ptr_t &ast) { return nullptr; }

type_ptr_t Constructor::visitTypeNode(const ast::node_ptr_t &ast) { return nullptr; }

func_ptr_t Constructor::visitFuncNode(const ast::node_ptr_t &ast) {}

void_ptr_t Constructor::visitNRefNode(const ast::node_ptr_t &ast) {
    const string &ident = ast::nref_load_ptr_cast(ast->load())->ident();
    const ast::node_ptr_t &child = ast_ptr_cast(ast->childAt(0));
    const ast::load_ptr_t &target = child->load();
    if (target->type() == ast::NodeType::FUNC) {
        func_ptr_t func = visitFuncNode(child);
        funcScope_->insert(ident, func);
    } else {
        auto result = visit(child);
        if (result.type() == typeid(node_ptr_t)) {
            node_ptr_t node = any_cast<node_ptr_t>(result);
            nodeScope_->insert(ident, node);
        } else {
            throw runtime_error("Unexpected result type from visiting the child of NREF node");
        }
    }
    return nullptr;
}

node_ptr_t Constructor::visitDRefNode(const ast::node_ptr_t &ast) {
    const string &ident = dref_load_ptr_cast(ast->load())->ident();
    auto optNode = nodeScope_->at(ident);
    if (optNode.has_value()) {
        return optNode.value();
    }
    auto optEntity = entityScope_->at(ident);
    if (optEntity.has_value()) {
        entity_ptr_t &entity = optEntity.value();
        if (entity->isFunc()) {
            const functor_ptr_t functor = dynamic_pointer_cast<Functor>(entity);
            auto funcNode = make_shared<FunctorNode>(functor->func());
            return funcNode;
        } else {
            return make_shared<DataNode>(entity->data());
        }
    }
    auto optFunc = funcScope_->at(ident);
    if (optFunc.has_value()) {
        return make_shared<FunctorNode>(optFunc.value());
    }
    throw runtime_error("Unresolved reference: " + ident);
}

node_ptr_t Constructor::visitWaitNode(const ast::node_ptr_t &ast) { throw runtime_error("Not implemented"); }

node_ptr_t Constructor::visitAnnoNode(const ast::node_ptr_t &ast) { throw runtime_error("Not implemented"); }

node_ptr_t Constructor::visitLinkNode(const ast::node_ptr_t &ast) {
    auto srcResult = visit(ast_ptr_cast(ast->at(0)));
    auto tgtResult = visit(ast_ptr_cast(ast->at(1)));
    if (srcResult.type() == typeid(node_ptr_t) && tgtResult.type() == typeid(node_ptr_t)) {
        node_ptr_t srcNode = any_cast<node_ptr_t>(srcResult);
        node_ptr_t tgtNode = any_cast<node_ptr_t>(tgtResult);
        if (tgtNode->type() == NodeType::FUNCTOR) {
            auto funcNode = func_node_ptr_cast(tgtNode);
            funcNode->setParams(srcNode);
        } else {
            throw runtime_error("Non-functor entities cannot be set with parameters");
        }
        return tgtNode;
    } else {
        throw runtime_error("Unexpected result type from visiting children of LINK node");
    }
}

node_ptr_t Constructor::visitWithNode(const ast::node_ptr_t &ast) {
    auto withResult = visit(ast_ptr_cast(ast->at(0)));
    if (withResult.type() == typeid(node_ptr_t)) {
        node_ptr_t withNode = any_cast<node_ptr_t>(withResult);
        auto funcResult = visit(ast_ptr_cast(ast->at(1)));
        if (funcResult.type() == typeid(node_ptr_t)) {
            node_ptr_t resNode = any_cast<node_ptr_t>(funcResult);
            if (resNode->type() == NodeType::FUNCTOR) {
                auto funcNode = func_node_ptr_cast(resNode);
                funcNode->setSuperParams(withNode);
                return resNode;
            } else {
                throw runtime_error("Non-functor entities cannot be set with super parameters");
            }
        } else {
            throw runtime_error("Unexpected result type from visiting FUNC node");
        }
    } else {
        throw runtime_error("Unexpected result type from visiting WITH node");
    }
}

node_ptr_t Constructor::visitRetnNode(const ast::node_ptr_t &ast) {
    auto result = visit(ast_ptr_cast(ast->at(0)));
    if (result.type() == typeid(node_ptr_t)) {
        return any_cast<node_ptr_t>(result);
    } else {
        throw runtime_error("Unexpected result type from visiting child of RETN node");
    }
}

node_ptr_t Constructor::visitExecNode(const ast::node_ptr_t &ast) { throw runtime_error("Not implemented"); }

node_ptr_t Constructor::visitFromNode(const ast::node_ptr_t &ast) { throw runtime_error("Not implemented"); }