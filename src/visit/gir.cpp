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

inline ast_ptr_t ast_ptr_cast(const tree_node_ptr_t<ast_load_ptr_t> &ptr) {
    return std::dynamic_pointer_cast<ASTNode>(ptr);
}

std::any GraphIRConstructor::visitASTNode(const ast_ptr_t &ast) {
    // TODO: implement
    switch (ast->type()) {
    case ASTNodeType::DATA:
        return visitDataNode(ast);
    case ASTNodeType::VARI:
        return visitVariNode(ast);
    case ASTNodeType::TYPE:
        return visitTypeNode(ast);
    case ASTNodeType::FUNC:
        return visitFuncNode(ast);
    case ASTNodeType::NREF:
        return visitNRefNode(ast);
    case ASTNodeType::DREF:
        return visitDRefNode(ast);
    case ASTNodeType::WAIT:
        return visitWaitNode(ast);
    case ASTNodeType::ANNO:
        return visitAnnoNode(ast);
    case ASTNodeType::LINK:
        return visitLinkNode(ast);
    case ASTNodeType::WITH:
        return visitWithNode(ast);
    case ASTNodeType::RETN:
        return visitRetnNode(ast);
    case ASTNodeType::EXEC:
        return visitExecNode(ast);
    case ASTNodeType::FROM:
        return visitFromNode(ast);
    default:
        throw std::runtime_error("Unknown ASTNodeType");
    }
}

node_ptr_t GraphIRConstructor::visitDataNode(const ast_ptr_t &ast) {
    const auto &dataNode = std::dynamic_pointer_cast<DataASTLoad>(ast);
    const data_ptr_t &data = dataNode->data();
    node_ptr_t node = std::make_shared<DataGraphNode>(data);
    if (dataNode->resolved()) {
        return node;
    } else {
        for (const auto &e : dataNode->getUnrefData()) {
            const std::string &ref = std::dynamic_pointer_cast<RefData>(e)->ref();
            auto optSrcNode = nodeScope_->at(ref);
            if (optSrcNode.has_value()) {
                GraphNode::link(optSrcNode.value(), node);
            } else {
                throw std::runtime_error("Unresolved reference: " + ref);
            }
        }
        return node;
    }
}

node_ptr_t GraphIRConstructor::visitVariNode(const ast_ptr_t &ast) { return nullptr; }

type_ptr_t GraphIRConstructor::visitTypeNode(const ast_ptr_t &ast) { return nullptr; }

func_ptr_t GraphIRConstructor::visitFuncNode(const ast_ptr_t &ast) {}

void_ptr_t GraphIRConstructor::visitNRefNode(const ast_ptr_t &ast) {
    const std::string &ident = nref_ast_load_ptr_cast(ast->load())->ident();
    const ast_ptr_t &child = ast_ptr_cast(ast->childAt(0));
    const ast_load_ptr_t &target = child->load();
    if (target->type() == ASTNodeType::FUNC) {
        func_ptr_t func = visitFuncNode(child);
        funcScope_->insert(ident, func);
    } else {
        auto result = visitASTNode(child);
        if (result.type() == typeid(node_ptr_t)) {
            node_ptr_t node = std::any_cast<node_ptr_t>(result);
            nodeScope_->insert(ident, node);
        } else {
            throw std::runtime_error("Unexpected result type from visiting the child of NREF node");
        }
    }
    return nullptr;
}

node_ptr_t GraphIRConstructor::visitDRefNode(const ast_ptr_t &ast) {
    const std::string &ident = dref_ast_load_ptr_cast(ast->load())->ident();
    auto optNode = nodeScope_->at(ident);
    if (optNode.has_value()) {
        return optNode.value();
    }
    auto optEntity = entityScope_->at(ident);
    if (optEntity.has_value()) {
        entity_ptr_t &entity = optEntity.value();
        if (entity->isFunc()) {
            const functor_ptr_t functor = std::dynamic_pointer_cast<Functor>(entity);
            auto funcNode = std::make_shared<FuncGraphNode>(functor->func());
            return funcNode;
        } else {
            return std::make_shared<DataGraphNode>(entity->data());
        }
    }
    auto optFunc = funcScope_->at(ident);
    if (optFunc.has_value()) {
        return std::make_shared<FuncGraphNode>(optFunc.value());
    }
    throw std::runtime_error("Unresolved reference: " + ident);
}

node_ptr_t GraphIRConstructor::visitWaitNode(const ast_ptr_t &ast) { throw std::runtime_error("Not implemented"); }

node_ptr_t GraphIRConstructor::visitAnnoNode(const ast_ptr_t &ast) { throw std::runtime_error("Not implemented"); }

node_ptr_t GraphIRConstructor::visitLinkNode(const ast_ptr_t &ast) {
    auto srcResult = visitASTNode(ast_ptr_cast(ast->at(0)));
    auto tgtResult = visitASTNode(ast_ptr_cast(ast->at(1)));
    if (srcResult.type() == typeid(node_ptr_t) && tgtResult.type() == typeid(node_ptr_t)) {
        node_ptr_t srcNode = std::any_cast<node_ptr_t>(srcResult);
        node_ptr_t tgtNode = std::any_cast<node_ptr_t>(tgtResult);
        if (tgtNode->type() == NodeType::FUNCTOR) {
            auto funcNode = func_graph_node_ptr_cast(tgtNode);
            funcNode->setParams(srcNode);
        } else {
            throw std::runtime_error("Non-functor entities cannot be set with parameters");
        }
        return tgtNode;
    } else {
        throw std::runtime_error("Unexpected result type from visiting children of LINK node");
    }
}

node_ptr_t GraphIRConstructor::visitWithNode(const ast_ptr_t &ast) {
    auto withResult = visitASTNode(ast_ptr_cast(ast->at(0)));
    if (withResult.type() == typeid(node_ptr_t)) {
        node_ptr_t withNode = std::any_cast<node_ptr_t>(withResult);
        auto funcResult = visitASTNode(ast_ptr_cast(ast->at(1)));
        if (funcResult.type() == typeid(node_ptr_t)) {
            node_ptr_t resNode = std::any_cast<node_ptr_t>(funcResult);
            if (resNode->type() == NodeType::FUNCTOR) {
                auto funcNode = func_graph_node_ptr_cast(resNode);
                funcNode->setSuperParams(withNode);
                return resNode;
            } else {
                throw std::runtime_error("Non-functor entities cannot be set with super parameters");
            }
        } else {
            throw std::runtime_error("Unexpected result type from visiting FUNC node");
        }
    } else {
        throw std::runtime_error("Unexpected result type from visiting WITH node");
    }
}

node_ptr_t GraphIRConstructor::visitRetnNode(const ast_ptr_t &ast) {
    auto result = visitASTNode(ast_ptr_cast(ast->at(0)));
    if (result.type() == typeid(node_ptr_t)) {
        return std::any_cast<node_ptr_t>(result);
    } else {
        throw std::runtime_error("Unexpected result type from visiting child of RETN node");
    }
}

node_ptr_t GraphIRConstructor::visitExecNode(const ast_ptr_t &ast) { throw std::runtime_error("Not implemented"); }

node_ptr_t GraphIRConstructor::visitFromNode(const ast_ptr_t &ast) { throw std::runtime_error("Not implemented"); }