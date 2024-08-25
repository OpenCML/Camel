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

node_ptr_t GraphIRConstructor::visitDataNode(const ast_ptr_t &ast) {
    const auto &data = std::dynamic_pointer_cast<DataNode>(ast);
    const auto &value = data->value();
    node_ptr_t node = std::make_shared<GraphNode>();
    if (data->resolved()) {
        return node;
    } else {
        for (const auto &e : data->getUnrefVals()) {
            const auto &ref = std::dynamic_pointer_cast<DanglingValue>(e)->ref();
            auto &sourceNode = nodeScope_->at(ref);
            if (sourceNode.has_value()) {
                GraphNode::link(sourceNode.value(), node);
            } else {
                throw std::runtime_error("Unresolved reference: " + ref);
            }
        }
        return node;
    }
}

node_ptr_t GraphIRConstructor::visitFuncNode(const ast_ptr_t &ast) {}

node_ptr_t GraphIRConstructor::visitTypeNode(const ast_ptr_t &ast) {}

node_ptr_t GraphIRConstructor::visitNewRefNode(const ast_ptr_t &ast) {}

node_ptr_t GraphIRConstructor::visitDeRefNode(const ast_ptr_t &ast) {}

node_ptr_t GraphIRConstructor::visitAssignNode(const ast_ptr_t &ast) {}

node_ptr_t GraphIRConstructor::visitAnnoNode(const ast_ptr_t &ast) {}

node_ptr_t GraphIRConstructor::visitLinkNode(const ast_ptr_t &ast) {}

node_ptr_t GraphIRConstructor::visitWithNode(const ast_ptr_t &ast) {}

node_ptr_t GraphIRConstructor::visitReturnNode(const ast_ptr_t &ast) {}