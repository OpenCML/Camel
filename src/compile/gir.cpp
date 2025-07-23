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
#include "utils/log.h"
#include "utils/type.h"

#define DEBUG_LEVEL 0

using namespace std;

namespace GraphIntermediateRepresentation {

any Constructor::visit(const GCT::node_ptr_t &node) {
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
    default:
        ASSERT(false, "Unknown GCT NodeType");
    }
    return nullptr;
}

void_ptr_t Constructor::visitDeclNode(const GCT::node_ptr_t &gct) {
    enter("DECL");
    const auto &declLoad = gct->loadAs<GCT::DeclLoad>();
    if (!declLoad->isFunc()) {
        leave("DECL");
        return nullptr;
    }

    GCT::node_ptr_t typeNode = gct->atAs<GCT::TypeLoad>(0);
    type_ptr_t type = typeNode->loadAs<GCT::TypeLoad>()->dataType();
    func_type_ptr_t funcType = tt::as_shared<FunctionType>(type);
    const auto &withType = tt::as_shared<ParamsType>(funcType->withType());
    const auto &linkType = tt::as_shared<ParamsType>(funcType->linkType());

    graph_ptr_t graph = context_->pushScope(declLoad->ref().ident());
    graph->setFuncType(funcType);
    arena_ptr_t arena = graph->arena();
    for (const auto &[name, type, data] : withType->elements()) {
        // TODO: ignored type and default data here
        if (data != nullptr) {
            reportDiagnostic(Diagnostic::Severity::Warning,
                             "Default data is currently not supported in function parameters.");
        }
        context_->insertNode(name, graph->addPort());
    }
    for (const auto &[name, type, data] : linkType->elements()) {
        // TODO: ignored type and default data here
        if (data != nullptr) {
            reportDiagnostic(Diagnostic::Severity::Warning,
                             "Default data is currently not supported in function parameters.");
        }
        context_->insertNode(name, graph->addPort());
    }
    context_->popScope();

    leave("DECL");
    return nullptr;
}

node_ptr_t Constructor::visitFuncNode(const GCT::node_ptr_t &gct) {
    enter("FUNC");
    // func_ptr_t func = visitDeclNode(gct->at(0));
    // func_type_ptr_t funcType = func->funcType();
    // context_->pushScope(funcType);
    // visitExecNode(gct->at(1));
    // context_->popScope(funcType);
    // delCachedFunc(funcType);
    leave("FUNC");
    // return SelectNode::create(context_->currGraph(), func_vec_t{func});
    return nullptr;
}

node_ptr_t Constructor::visitDataNode(const GCT::node_ptr_t &gct) {
    enter("DATA");
    // const auto &dataLoad = gct->loadAs<GCT::DataLoad>();
    // const data_ptr_t &data = dataLoad->data();
    // node_ptr_t node = LiteralNode::create(context_->currGraph(), data, false);
    // if (!data->resolved()) {
    //     for (const string &ref : data->refs()) {
    //         auto optSrcNode = context_->nodeAt(ref);
    //         if (optSrcNode.has_value()) {
    //             node_ptr_t srcNode = selectNode(optSrcNode.value(), context_->currGraph());
    //             Node::link(srcNode, node);
    //         } else {
    //             throw runtime_error("Unresolved reference: " + ref);
    //         }
    //     }
    // }
    leave("DATA");
    return nullptr;
}

type_ptr_t Constructor::visitTypeNode(const GCT::node_ptr_t &gct) {
    enter("TYPE");
    const type_ptr_t &type = gct->loadAs<GCT::TypeLoad>()->dataType();
    leave("TYPE");
    return type;
}

void_ptr_t Constructor::visitNRefNode(const GCT::node_ptr_t &gct) {
    enter("NREF");
    const string &ident = gct->loadAs<GCT::NRefLoad>()->ref();
    const auto &res = visit(gct->at(0));
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
    // const string &ident = gct->loadAs<GCT::DRefLoad>()->ref();
    // auto optNode = context_->nodeAt(ident);
    // if (!optNode.has_value()) {
    //     throw runtime_error("Unresolved reference: " + ident);
    // }
    // leave("DREF");
    // node_ptr_t res = optNode.value();
    // res->ref();
    // return res;
    return nullptr;
}

node_ptr_t Constructor::visitVariNode(const GCT::node_ptr_t &gct) {
    enter("VARI");
    // const auto &res = visit(gct->at(0));
    // if (res.type() != typeid(node_ptr_t)) {
    //     throw runtime_error("Unexpected result type from Enter the child of VARI node");
    // }
    // node_ptr_t node = any_cast<node_ptr_t>(res);
    // node->makeVariable();
    leave("VARI");
    // return node;
    return nullptr;
}
node_ptr_t Constructor::visitWaitNode(const GCT::node_ptr_t &gct) { throw runtime_error("Not implemented"); }

node_ptr_t Constructor::visitLinkNode(const GCT::node_ptr_t &gct) {
    enter("LINK");
    // // TODO: consider functor and operator overriden
    // // for now, we just ignore it
    // // because we cannot get the exact type of unref elements of struct data yet
    // any dataRes = visit(gct->at(0));
    // any funcRes = visit(gct->at(1));
    // if (dataRes.type() != typeid(node_ptr_t) || funcRes.type() != typeid(node_ptr_t)) {
    //     throw runtime_error("Unexpected result type from Enter children of LINK node");
    // }
    // node_ptr_t dataNode = selectNode(any_cast<node_ptr_t>(dataRes), context_->currGraph());
    // node_ptr_t linkNode = selectNode(any_cast<node_ptr_t>(funcRes), context_->currGraph());
    // Node::link(dataNode, linkNode, 1);
    leave("LINK");
    return nullptr;
}

node_ptr_t Constructor::visitWithNode(const GCT::node_ptr_t &gct) {
    enter("WITH");
    // any dataRes = visit(gct->at(0));
    // any funcRes = visit(gct->at(1));
    // if (dataRes.type() != typeid(node_ptr_t) || funcRes.type() != typeid(node_ptr_t)) {
    //     throw runtime_error("Unexpected result type from Enter children of LINK node");
    // }
    // node_ptr_t dataNode = selectNode(any_cast<node_ptr_t>(dataRes), context_->currGraph());
    // node_ptr_t withNode = selectNode(any_cast<node_ptr_t>(funcRes), context_->currGraph());
    // Node::link(dataNode, withNode, 0);
    leave("WITH");
    // return withNode;
    return nullptr;
}

node_ptr_t Constructor::visitBindNode(const GCT::node_ptr_t &gct) {
    enter("BIND");
    leave("BIND");
    return nullptr;
}

node_ptr_t Constructor::visitAccsNode(const GCT::node_ptr_t &gct) {
    enter("ACCS");
    leave("ACCS");
    return nullptr;
}

node_ptr_t Constructor::visitBrchNode(const GCT::node_ptr_t &gct) {
    enter("BRCH");
    leave("BRCH");
    return nullptr;
}

node_ptr_t Constructor::visitAnnoNode(const GCT::node_ptr_t &gct) { throw runtime_error("Not implemented"); }

void_ptr_t Constructor::visitExitNode(const GCT::node_ptr_t &gct) {
    enter("RETN");
    auto res = visit(gct->at(0));
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
        any result = visit(gct->at(i));
        if (result.type() == typeid(node_ptr_t)) {
            node = any_cast<node_ptr_t>(result);
        }
    }
    leave("EXEC");
    return node;
}

} // namespace GraphIntermediateRepresentation