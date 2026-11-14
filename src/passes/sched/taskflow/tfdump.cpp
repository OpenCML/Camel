// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "taskflow.h"

#include "camel/runtime/reachable.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <utility>
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

namespace {

const char *predecessorKindName(camel::execute::ExecutionPredecessor::Kind kind) {
    switch (kind) {
    case camel::execute::ExecutionPredecessor::Kind::Control:
        return "control";
    case camel::execute::ExecutionPredecessor::Kind::Data:
        return "data";
    case camel::execute::ExecutionPredecessor::Kind::Sync:
        return "sync";
    case camel::execute::ExecutionPredecessor::Kind::Order:
        return "order";
    }
    return "unknown";
}

/// What every node that runs waits for, one line per node, read from the one function that says it
/// (camel::execute::executionPredecessorsOf): `# <node> <- [<kind>:<node>, ...]`. Written after the
/// digraph and prefixed with `#`, which is a comment to DOT, so the dump still parses as one.
///
/// Both halves of the line are sorted -- the nodes by number, the entries of one node by the
/// predecessor's number and then by kind -- so that the text is the answer itself and not also the
/// order the graph happens to store its edges in. A test can then hold this text to what it should
/// be, line for line: a predecessor that should not be there and one that is missing each move or
/// change exactly one line. std::tfdump declares no parameters (stdlib/std.cml gives it none, and
/// pass.cpp binds it without a config), so the section is always written rather than being asked
/// for; nothing else in the tree asserts this dump, whose digraph carries addresses.
void dumpPredecessors(
    const camel::execute::RunPrograms &programs, camel::runtime::GCGraph *root, std::ostream &os) {
    using camel::execute::ExecutionPredecessor;
    using camel::runtime::gc_node_ref_t;
    os << "# predecessors: what each node that runs waits for, and the edge that says so\n";
    for (camel::runtime::GCGraph *graph :
         camel::runtime::collectReachableGraphs(programs.of(root), root)) {
        std::vector<gc_node_ref_t> nodes(
            graph->execPlan().topo.begin(), graph->execPlan().topo.end());
        std::sort(nodes.begin(), nodes.end());
        os << "# graph " << graph->name() << "\n";
        for (gc_node_ref_t ref : nodes) {
            std::vector<std::pair<gc_node_ref_t, std::string>> entries;
            for (const ExecutionPredecessor &predecessor :
                 camel::execute::executionPredecessorsOf(*graph, ref)) {
                entries.emplace_back(predecessor.node, predecessorKindName(predecessor.kind));
            }
            std::sort(entries.begin(), entries.end());
            os << "# " << ref << " <- [";
            for (size_t i = 0; i < entries.size(); ++i) {
                if (i != 0) {
                    os << ", ";
                }
                os << entries[i].second << ":" << entries[i].first;
            }
            os << "]\n";
        }
    }
}

} // namespace

void TaskflowExecSchedPass::buildAndDump(
    const camel::runtime::Program *program, camel::runtime::GCGraph *graph, std::ostream &os) {
    GUARD(graph != nullptr, "Taskflow dump requires a non-null runtime root graph.");
    camel::execute::RunScope runScope(programs_, *context_, program);

    os << "digraph Taskflow {\n";
    for (auto *runtimeGraph : camel::runtime::collectReachableGraphs(programs_.of(graph), graph)) {
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
    dumpPredecessors(programs_, graph, os);
}

const camel::runtime::Program *
TfDumpPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    camel::runtime::GCGraph *graph = program->root();
    TaskflowExecSchedPass tfPass(context_, 32);
    tfPass.buildAndDump(program, graph, os);
    return nullptr;
}
