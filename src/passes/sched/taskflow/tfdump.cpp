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
 * Updated: Mar. 29, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/error/runtime.h"
#include "camel/core/module/module.h"
#include "taskflow.h"

#include <cctype>
#include <sstream>
#include <string>
#include <vector>

using namespace GIR;
using namespace camel::core::error;
using namespace camel::core::context;

namespace {

// 从 Taskflow dump 的字符串中提取 subgraph 块（从 "subgraph " 到匹配的 '}'）
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

// 将图名转为 DOT cluster id（只保留字母数字下划线）
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

void TaskflowExecSchedPass::buildAndDump(Graph *graph, std::ostream &os) {
    (void)graph->exitNode();
    buildGraphsInfo(graph);

    os << "digraph Taskflow {\n";
    std::vector<Frame *> framesToRelease;
    auto releaseAll = [&]() {
        for (Frame *f : framesToRelease)
            framePool_.release(f);
    };

    try {
        Graph *rootGraph     = graph;
        const auto &graphMap = globalBuildCtx_.graphInfoMap;

        // 先输出根图，再输出其余可达子图，使嵌套关系更清晰
        std::vector<Graph *> ordered;
        ordered.push_back(rootGraph);
        for (const auto &[g, _] : graphMap)
            if (g != rootGraph)
                ordered.push_back(g);

        for (Graph *g : ordered) {
            Frame *frame = framePool_.acquire(g);
            framesToRelease.push_back(frame);
            mainFlow_.clear();
            instantiate_graph_instance_generic(mainFlow_, g, frame);

            std::ostringstream ss;
            mainFlow_.dump(ss);
            std::string block = extractSubgraphBlock(ss.str());
            if (block.empty())
                continue;
            std::string clusterId   = sanitizeClusterId(g->name());
            std::string displayName = g->name();
            // 替换 cluster id 和 label，便于区分各子图
            size_t labelStart = block.find("label=");
            if (labelStart != std::string::npos) {
                size_t labelEnd = block.find(';', labelStart);
                if (labelEnd != std::string::npos)
                    block = block.substr(0, labelStart) + "label=\"" + displayName + "\";" +
                            block.substr(labelEnd + 1);
            }
            size_t clusterStart = block.find("cluster_");
            if (clusterStart != std::string::npos) {
                size_t clusterEnd = block.find_first_of(" \t{", clusterStart + 8);
                if (clusterEnd != std::string::npos)
                    block = block.substr(0, clusterStart) + "cluster_" + clusterId +
                            block.substr(clusterEnd);
            }
            os << "  " << block << "\n";
        }
        os << "}\n";
    } catch (...) {
        releaseAll();
        throw;
    }
    releaseAll();
}

GIR::graph_ptr_t TfDumpPass::apply(GIR::graph_ptr_t &graph, std::ostream &os) {
    TaskflowExecSchedPass tfPass(context_, 32);
    tfPass.buildAndDump(graph.get(), os);
    return GIR::Graph::null();
}
