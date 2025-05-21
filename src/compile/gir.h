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
 * Created: May. 29, 2024
 * Updated: Mar. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include <fstream>
#include <iostream>

#include "common/context.h"
#include "common/gct.h"
#include "common/graph.h"
#include "gct.h"

namespace GraphIR {

using void_ptr_t = void *;

class Constructor {
  public:
    Constructor(context_ptr_t &context) : context_(context) {}

    graph_ptr_t construct(GCT::node_ptr_t &ast) {
        visit(ast);
        return context_->rootGraph();
    }

  private:
    context_ptr_t context_;
    std::unordered_map<func_type_ptr_t, func_ptr_t> funcCache_;

    void cacheFunc(func_type_ptr_t key, func_ptr_t node) { funcCache_[key] = node; }
    void delCachedFunc(func_type_ptr_t key) { funcCache_.erase(key); }
    func_ptr_t getCachedFunc(func_type_ptr_t key) { return funcCache_[key]; }

    std::any visit(const GCT::node_ptr_t &ast);

    node_ptr_t visitDataNode(const GCT::node_ptr_t &ast);
    node_ptr_t visitVariNode(const GCT::node_ptr_t &ast);
    type_ptr_t visitTypeNode(const GCT::node_ptr_t &ast);
    func_ptr_t visitDeclNode(const GCT::node_ptr_t &ast);
    node_ptr_t visitFuncNode(const GCT::node_ptr_t &ast);
    void_ptr_t visitNRefNode(const GCT::node_ptr_t &ast);
    node_ptr_t visitDRefNode(const GCT::node_ptr_t &ast);
    node_ptr_t visitWaitNode(const GCT::node_ptr_t &ast);
    node_ptr_t visitAnnoNode(const GCT::node_ptr_t &ast);
    node_ptr_t visitLinkNode(const GCT::node_ptr_t &ast);
    node_ptr_t visitWithNode(const GCT::node_ptr_t &ast);
    void_ptr_t visitRetnNode(const GCT::node_ptr_t &ast);
    node_ptr_t visitExecNode(const GCT::node_ptr_t &ast);
    void_ptr_t visitFromNode(const GCT::node_ptr_t &ast);
};
} // namespace GraphIR
