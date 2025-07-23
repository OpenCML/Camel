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

namespace GraphIntermediateRepresentation {

using void_ptr_t = void *;

class Constructor {
  public:
    Constructor(context_ptr_t &context) : context_(context) {}

    graph_ptr_t construct(GCT::node_ptr_t &gct, diagnostics_ptr_t diagnostics) {
        waited_ = false;
        synced_ = false;
        varied_ = false;
        diagnostics_ = diagnostics;
        visit(gct);
        return context_->rootGraph();
    }

  private:
    context_ptr_t context_;
    diagnostics_ptr_t diagnostics_;

    bool waited_;
    bool synced_;
    bool varied_;

    void reportDiagnostic(Diagnostic::Severity sev, const std::string &msg, std::pair<size_t, size_t> tokenRange = {0, 0}) {
        diagnostics_->emplace(sev, msg, tokenRange.first, tokenRange.second);
    }

    std::any visit(const GCT::node_ptr_t &gct);

    void_ptr_t visitDeclNode(const GCT::node_ptr_t &gct);
    graph_ptr_t visitFuncNode(const GCT::node_ptr_t &gct);
    node_ptr_t visitDataNode(const GCT::node_ptr_t &gct);
    type_ptr_t visitTypeNode(const GCT::node_ptr_t &gct);
    void_ptr_t visitNRefNode(const GCT::node_ptr_t &gct);
    node_ptr_t visitDRefNode(const GCT::node_ptr_t &gct);
    node_ptr_t visitVariNode(const GCT::node_ptr_t &gct);
    node_ptr_t visitWaitNode(const GCT::node_ptr_t &gct);
    node_ptr_t visitLinkNode(const GCT::node_ptr_t &gct);
    node_ptr_t visitWithNode(const GCT::node_ptr_t &gct);
    node_ptr_t visitBindNode(const GCT::node_ptr_t &gct);
    node_ptr_t visitAccsNode(const GCT::node_ptr_t &gct);
    node_ptr_t visitBrchNode(const GCT::node_ptr_t &gct);
    node_ptr_t visitAnnoNode(const GCT::node_ptr_t &gct);
    void_ptr_t visitExitNode(const GCT::node_ptr_t &gct);
    node_ptr_t visitExecNode(const GCT::node_ptr_t &gct);
};
} // namespace GraphIntermediateRepresentation
