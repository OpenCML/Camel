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
 * Created: Mar. 09, 2026
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "taskflow.h"

#include "camel/runtime/reachable.h"

#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string extractSubgraphBlock(const std::string &fullDump) {
    size_t sub = fullDump.find("subgraph ");
    if (sub == std::string::npos)
        return "";
    size_t open = fullDump.find('{', sub);
    if (open == std::string::npos)
        return "";
    int depth = 1;
    size_t i  = open + 1;
    while (depth > 0 && i < fullDump.size()) {
        if (fullDump[i] == '{')
            depth++;
        else if (fullDump[i] == '}')
            depth--;
        i++;
    }
    if (depth != 0)
        return "";
    return fullDump.substr(sub, (i - 1) - sub + 1);
}

std::string sanitizeClusterId(const std::string &name) {
    std::string out;
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
            out += c;
        else if (c == ':' || c == '/' || c == '.')
            out += '_';
    }
    return out.empty() ? "g" : out;
}

} // namespace

void TaskflowExecSchedPass::buildAndDump(camel::runtime::GCGraph *graph, std::ostream &os) {
    ASSERT(graph != nullptr, "Taskflow dump requires a non-null runtime root graph.");

    os << "digraph Taskflow {\n";
    for (auto *runtimeGraph : camel::runtime::collectReachableGraphs(graph)) {
        ctx::Frame *frame = framePool_.acquire(runtimeGraph);
        try {
            mainFlow_.clear();
            instantiate_graph_instance_generic(mainFlow_, runtimeGraph, frame);

            std::ostringstream ss;
            mainFlow_.dump(ss);
            std::string block = extractSubgraphBlock(ss.str());
            if (!block.empty()) {
                const std::string clusterId   = sanitizeClusterId(runtimeGraph->name());
                const std::string displayName = runtimeGraph->name();
                size_t labelStart             = block.find("label=");
                if (labelStart != std::string::npos) {
                    size_t labelEnd = block.find(';', labelStart);
                    if (labelEnd != std::string::npos) {
                        block = block.substr(0, labelStart) + "label=\"" + displayName + "\";" +
                                block.substr(labelEnd + 1);
                    }
                }
                size_t clusterStart = block.find("cluster_");
                if (clusterStart != std::string::npos) {
                    size_t clusterEnd = block.find_first_of(" \t{", clusterStart + 8);
                    if (clusterEnd != std::string::npos) {
                        block = block.substr(0, clusterStart) + "cluster_" + clusterId +
                                block.substr(clusterEnd);
                    }
                }
                os << "  " << block << "\n";
            }
            framePool_.release(frame);
        } catch (...) {
            framePool_.release(frame);
            throw;
        }
    }
    os << "}\n";
}

camel::runtime::GCGraph *TfDumpPass::apply(camel::runtime::GCGraph *graph, std::ostream &os) {
    TaskflowExecSchedPass tfPass(context_, 32);
    tfPass.buildAndDump(graph, os);
    return nullptr;
}
