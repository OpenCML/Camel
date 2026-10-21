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
 * Created: Oct. 14, 2026
 * Updated: Oct. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Capture ports and their binding (docs/design/graph-model/README.cn.md 3.7, rule C1).
 *
 * A graph that uses a value of an enclosing graph holds it in a capture port; the port is
 * bound to the node it stands for (its origin). A value of the top level of a module is a
 * node of the root graph, so every function of every module can capture it. Where a graph is
 * called directly (a FUNC node), the caller passes each capture of the callee as a With
 * input; where it becomes a function value, the captures are filled into the value. In both
 * cases the caller supplies the value of the same origin as the caller sees it, which may
 * give the caller a capture port of its own: captures travel up the call chain to the graph
 * the value is computed in, the root graph for a top-level value.
 */

#include "builder.h"
#include "builder_support.h"

#include "camel/utils/assert.h"

#include <format>

using namespace camel::core::error;

namespace camel::compile::gir {

std::string Builder::captureNameOf(node_handle_t origin, const std::string &fallback) const {
    const auto it = topLevelValues_.find(origin);
    if (it == topLevelValues_.end()) {
        return fallback;
    }
    // A value of the current module is shown as the module writes it; one of another module
    // with its module's name, the way it is named from outside (design principles 10.2).
    const TopLevelValue &value = it->second;
    if (module_ && value.module == module_.get()) {
        return value.name;
    }
    return std::format("{}::{}", value.module->name(), value.name);
}

node_handle_t Builder::captureInto(
    const compile_graph_ptr_t &graph, node_handle_t origin, const std::string &name) {
    ASSERT(graph != nullptr && origin != nullptr, "A capture needs a graph and a value.");
    if (nodeGraphOf(origin) == graph.get()) {
        return origin;
    }
    if (!name.empty()) {
        originNames_.try_emplace(origin, name);
    }
    if (auto known = capturedValues_.find(graph.get()); known != capturedValues_.end()) {
        for (const CapturedValue &captured : known->second) {
            if (captured.origin == origin) {
                return captured.port;
            }
        }
    }
    // The origin is a node of a graph that encloses this one (the root graph for a value of
    // the top level of a module): the enclosing graphs between them capture it as well, so
    // that whoever calls or makes the value of this graph can pass it on.
    ASSERT(
        graph->outerGraph() != nullptr,
        "A captured value must belong to a graph that encloses the capturing one.");
    (void)captureInto(graph->outerGraph(), origin, name);

    const bool isVar = isVariable(origin);
    // A function value holds the captures its graph had when it was made; a direct call is
    // given a capture made later at the link step (linkCaptureSites), but not one of a
    // variable: the call has been ordered among the uses of the variables it was known to use
    // (recordWrite), and that cannot be redone once the caller is built. Function bodies are
    // built before they are used (ensureFunctionBuilt), so this is only met in recursion.
    if (valueUsedGraphs_.contains(graph.get()) || (graph->parameterized() && isVar)) {
        // Once per function and value: the next use of the value fails the same way.
        if (!lateCaptures_.insert({graph.get(), origin}).second) {
            throw BuildAbortException();
        }
        const std::string shownName =
            captureNameOf(origin, name.empty() ? originNames_[origin] : name);
        diags_->of(SemanticDiag::CaptureAfterRecursiveCall)
            .atOrigin(refOrigin_)
            .commit(shownName, graph->name());
        throw BuildAbortException();
    }

    const std::string shown = captureNameOf(origin, name.empty() ? originNames_[origin] : name);
    // A captured variable is captured by reference: the port holds the same cell.
    node_handle_t port = graph->addPortNode(nodeTypeOf(origin), shown, false);
    graph->draft().removeNormPort(nodeIdOf(port));
    if (graph->parameterized()) {
        // Called directly already: its captures are with-ports, after the declared ones.
        graph->draft().appendWithPort(nodeIdOf(port));
    } else {
        graph->draft().appendClosureNode(nodeIdOf(port));
    }
    graph->addCaptureName(shown);
    capturedValues_[graph.get()].push_back({origin, port, shown});
    captureOriginOf_[port] = origin;
    if (isVar) {
        variables_.insert(port);
        captures_[graph.get()].variables.push_back(origin);
    }
    return port;
}

void Builder::addCaptureSite(node_handle_t call, const compile_graph_ptr_t &callee) {
    CaptureSite site;
    site.caller = currGraph_;
    site.call   = call;
    site.callee = callee;
    // A copy: capturing into the caller may add to the table.
    const std::vector<CapturedValue> captured = capturedValues_[callee.get()];
    site.values.reserve(captured.size());
    for (const CapturedValue &value : captured) {
        node_handle_t here = captureInto(currGraph_, value.origin, value.name);
        site.values.push_back(here);
    }
    captureSites_.push_back(std::move(site));
}

void Builder::linkCaptureSites() {
    // Least fixpoint: passing a capture the callee made late may give the caller a capture
    // port of its own, which the calls of the caller then pass in turn.
    for (bool changed = true; changed;) {
        changed = false;
        for (size_t i = 0; i < captureSites_.size(); ++i) {
            const std::vector<CapturedValue> captured =
                capturedValues_[captureSites_[i].callee.get()];
            for (size_t k = captureSites_[i].linked; k < captured.size(); ++k) {
                CaptureSite &site = captureSites_[i];
                node_handle_t value = nullptr;
                if (k < site.values.size()) {
                    value = site.values[k];
                } else {
                    const size_t before = capturedValues_[site.caller.get()].size();
                    value = captureInto(site.caller, captured[k].origin, captured[k].name);
                    if (capturedValues_[site.caller.get()].size() != before) {
                        changed = true;
                    }
                }
                // The With inputs of a FUNC node are its callee's with-ports in order: the
                // declared `with` arguments, linked where the call was built, then the
                // captures, linked here.
                tryRemoveCtrlLink(value, site.call);
                linkNodes(LinkType::With, value, site.call);
                site.linked = k + 1;
            }
        }
    }
}

} // namespace camel::compile::gir
