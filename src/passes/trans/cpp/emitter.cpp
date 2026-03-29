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
 * Created: Mar. 11, 2026
 * Updated: Mar. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "emitter.h"

#include "bridge.h"

#include "camel/utils/type.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

using namespace GIR;
using namespace camel::core::type;

namespace {

graph_ptr_t findNamedSubGraph(const graph_ptr_t &root, const std::string &name) {
    auto optGraphs = root->getSubGraphsByName(name);
    if (!optGraphs.has_value() || optGraphs->empty()) {
        return nullptr;
    }
    if (optGraphs->size() != 1) {
        throw std::runtime_error(std::format("multiple '{}' graphs found", name));
    }
    return *optGraphs->begin();
}

void collectDepsDepthFirst(
    Graph *graph, std::vector<Graph *> &ordered, std::unordered_set<Graph *> &visited) {
    if (!visited.insert(graph).second) {
        return;
    }
    for (const auto &dep : graph->dependencies()) {
        collectDepsDepthFirst(dep.get(), ordered, visited);
    }
    ordered.push_back(graph);
}

std::string joinReasons(const std::vector<LoweringIssue> &issues) {
    std::string joined;
    for (size_t i = 0; i < issues.size(); ++i) {
        if (i != 0) {
            joined += "; ";
        }
        joined += std::format("{}: {}", issues[i].node->toString(), issues[i].reason);
    }
    return joined;
}

bool isAnonymousGraphName(const std::string &name) {
    return name.size() >= 4 && name.starts_with("__") && name.ends_with("__");
}

std::string sanitizeHint(std::string text) {
    for (char &ch : text) {
        if (!std::isalnum(static_cast<unsigned char>(ch))) {
            ch = '_';
        } else {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
    }
    std::string out;
    out.reserve(text.size());
    bool lastUnderscore = false;
    for (char ch : text) {
        if (ch == '_') {
            if (!lastUnderscore) {
                out.push_back('_');
                lastUnderscore = true;
            }
        } else {
            out.push_back(ch);
            lastUnderscore = false;
        }
    }
    while (!out.empty() && out.front() == '_') {
        out.erase(out.begin());
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    return out.empty() ? "value" : out;
}

std::string tailAfterColon(const std::string &text) {
    const size_t pos = text.rfind(':');
    if (pos == std::string::npos || pos + 1 >= text.size()) {
        return text;
    }
    return text.substr(pos + 1);
}

std::vector<Node *> collectStatementNodes(const GraphLoweringPlan &plan) {
    std::vector<Node *> ordered;
    ordered.reserve(plan.topoNodes.size());

    std::unordered_map<Node *, size_t> originalOrder;
    std::unordered_map<Node *, size_t> indegree;
    std::unordered_map<Node *, std::vector<Node *>> successors;
    for (size_t i = 0; i < plan.topoNodes.size(); ++i) {
        Node *node          = plan.topoNodes[i];
        originalOrder[node] = i;
        indegree[node]      = 0;
    }

    auto addEdge = [&](Node *from, Node *to) {
        if (!from || !to || !indegree.contains(from) || !indegree.contains(to)) {
            return;
        }
        successors[from].push_back(to);
        ++indegree[to];
    };

    for (Node *node : plan.topoNodes) {
        for (auto *input : node->dataInputs()) {
            if (&input->graph() == &node->graph()) {
                addEdge(input, node);
            }
        }
        for (auto *input : node->ctrlInputs()) {
            if (&input->graph() == &node->graph()) {
                addEdge(input, node);
            }
        }
    }

    std::vector<Node *> ready;
    for (Node *node : plan.topoNodes) {
        if (indegree[node] == 0) {
            ready.push_back(node);
        }
    }

    while (!ready.empty()) {
        auto it    = std::min_element(ready.begin(), ready.end(), [&](Node *lhs, Node *rhs) {
            return originalOrder[lhs] < originalOrder[rhs];
        });
        Node *node = *it;
        ready.erase(it);
        ordered.push_back(node);

        auto succIt = successors.find(node);
        if (succIt == successors.end()) {
            continue;
        }
        for (Node *succ : succIt->second) {
            auto indegreeIt = indegree.find(succ);
            if (indegreeIt == indegree.end()) {
                continue;
            }
            if (--indegreeIt->second == 0) {
                ready.push_back(succ);
            }
        }
    }

    if (ordered.size() == plan.topoNodes.size()) {
        return ordered;
    }
    return plan.topoNodes;
}

/// 当表达式作为独立 RHS（赋值、return）时，去掉冗余的外层括号
std::string stripRedundantOuterParens(std::string_view s) {
    if (s.size() < 2 || s.front() != '(' || s.back() != ')')
        return std::string(s);
    int depth = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '(')
            ++depth;
        else if (s[i] == ')') {
            --depth;
            if (depth == 0)
                return (i == s.size() - 1) ? std::string(s.substr(1, s.size() - 2))
                                           : std::string(s);
        }
    }
    return std::string(s);
}

} // namespace

std::string CppEmitter::emit(const graph_ptr_t &root) {
    auto target = resolveTargetGraph(root);
    if (!target) {
        throw std::runtime_error("no suitable target graph found for C++ emission");
    }

    std::vector<GraphLoweringPlan> plans;
    for (Graph *graph : collectEmissionOrder(collectEmissionRoots(target.get()))) {
        GraphLoweringPlan plan = analyzeGraphForCpp(graph);
        if (!plan.directCallable) {
            throw std::runtime_error(
                std::format(
                    "graph '{}' is not supported by the direct C++ path: {}",
                    graph->name(),
                    joinReasons(plan.issues)));
        }
        plans.push_back(std::move(plan));
    }

    switch (mode_) {
    case CppEmitMode::Plain:
        return emitPlainUnit(target.get(), plans);
    case CppEmitMode::Module:
    case CppEmitMode::Inspect:
        return emitModuleUnit(target.get(), plans);
    default:
        throw std::runtime_error("unknown C++ emission mode");
    }
}

graph_ptr_t CppEmitter::resolveTargetGraph(const graph_ptr_t &root) const {
    if (isExecutableMode()) {
        if (auto main = findNamedSubGraph(root, "main")) {
            return main;
        }
        throw std::runtime_error(
            "std::cpp requires a 'main' graph; use std::cpp::module to dump a module without an "
            "entry point");
    }
    if (mode_ == CppEmitMode::Bench) {
        if (auto fib = findNamedSubGraph(root, "fib")) {
            return fib;
        }
        if (auto main = findNamedSubGraph(root, "main")) {
            return main;
        }
    }
    return root;
}

std::vector<Graph *> CppEmitter::collectEmissionRoots(Graph *entry) const {
    if (isExecutableMode()) {
        return {entry};
    }

    std::vector<Graph *> roots;
    for (const auto &[_, graphs] : entry->subGraphs()) {
        for (const auto &graph : graphs) {
            if (!graph || graph->outer().get() != entry) {
                continue;
            }
            if (isAnonymousGraphName(graph->name())) {
                continue;
            }
            roots.push_back(graph.get());
        }
    }

    if (roots.empty() && entry->hasOutput()) {
        roots.push_back(entry);
    }

    std::sort(roots.begin(), roots.end(), [](const Graph *lhs, const Graph *rhs) {
        return lhs->name() < rhs->name();
    });
    roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
    return roots;
}

std::vector<Graph *> CppEmitter::collectEmissionOrder(const std::vector<Graph *> &entries) const {
    std::vector<Graph *> ordered;
    std::unordered_set<Graph *> visited;
    for (Graph *entry : entries) {
        collectDepsDepthFirst(entry, ordered, visited);
    }
    return ordered;
}

bool CppEmitter::isExecutableMode() const { return mode_ == CppEmitMode::Plain; }

bool CppEmitter::isInspectMode() const { return mode_ == CppEmitMode::Inspect; }

bool CppEmitter::shouldEmitStatementBody(const GraphLoweringPlan &plan) const {
    for (auto *node : plan.topoNodes) {
        switch (node->type()) {
        case NodeType::DATA:
        case NodeType::PORT:
        case NodeType::EXIT:
        case NodeType::BRCH:
            continue;
        default:
            return true;
        }
    }
    return false;
}

std::string CppEmitter::emitForwardDeclarations(const std::vector<GraphLoweringPlan> &plans) {
    std::ostringstream oss;
    for (const auto &plan : plans) {
        oss << emitFunctionSignature(plan.graph, true) << ";\n";
    }
    return oss.str();
}

std::string CppEmitter::emitFunctionSignature(Graph *graph, bool forceInline) {
    std::ostringstream oss;
    if (forceInline) {
        oss << "static inline ";
    } else {
        oss << "static ";
    }
    oss << cppTypeFor(graph->funcType()->exitType()) << " " << namer_.graphSymbol(graph) << "(";

    bool first = true;
    for (const auto &port : graph->normPorts()) {
        auto *portNode = tt::as_ptr<PortNode>(port);
        if (!first) {
            oss << ", ";
        }
        first = false;
        oss << cppTypeFor(portNode->dataType()) << " "
            << namer_.valueSymbol(portNode, portNode->name());
    }
    for (const auto &port : graph->withPorts()) {
        auto *portNode = tt::as_ptr<PortNode>(port);
        if (!first) {
            oss << ", ";
        }
        first = false;
        oss << cppTypeFor(portNode->dataType()) << " "
            << namer_.valueSymbol(portNode, portNode->name());
    }

    oss << ")";
    return oss.str();
}

std::string CppEmitter::emitBindingSymbol(Node *node, std::optional<size_t> stmtIndex) {
    auto hintFor = [](const std::string &hint) -> std::string { return sanitizeHint(hint); };
    if (stmtIndex.has_value()) {
        switch (node->type()) {
        case NodeType::OPER: {
            auto *opNode = tt::as_ptr<OperNode>(node);
            return namer_.valueSymbolStmt(
                node,
                hintFor(tailAfterColon(opNode->oper()->name())),
                *stmtIndex);
        }
        case NodeType::FUNC: {
            auto *funcNode = tt::as_ptr<FuncNode>(node);
            return namer_.valueSymbolStmt(node, hintFor(funcNode->bodyGraph()->name()), *stmtIndex);
        }
        case NodeType::CAST:
            return namer_.valueSymbolStmt(node, "cast", *stmtIndex);
        case NodeType::COPY:
            return namer_.valueSymbolStmt(node, "copy", *stmtIndex);
        case NodeType::JOIN:
            return namer_.valueSymbolStmt(node, "join", *stmtIndex);
        default:
            return namer_.valueSymbolStmt(node, "v", *stmtIndex);
        }
    }
    switch (node->type()) {
    case NodeType::OPER: {
        auto *opNode = tt::as_ptr<OperNode>(node);
        return namer_.valueSymbol(
            node,
            hintFor(tailAfterColon(opNode->oper()->name()) + "_" + std::to_string(node->index())));
    }
    case NodeType::FUNC: {
        auto *funcNode = tt::as_ptr<FuncNode>(node);
        return namer_.valueSymbol(
            node,
            hintFor(funcNode->bodyGraph()->name() + "_" + std::to_string(node->index())));
    }
    case NodeType::CAST:
        return namer_.valueSymbol(node, "cast_" + std::to_string(node->index()));
    case NodeType::COPY:
        return namer_.valueSymbol(node, "copy_" + std::to_string(node->index()));
    case NodeType::JOIN:
        return namer_.valueSymbol(node, "join_" + std::to_string(node->index()));
    default:
        return namer_.valueSymbol(node, "value_" + std::to_string(node->index()));
    }
}

std::string CppEmitter::emitOperatorExpr(OperNode *node, expr_cache_t &cache) {
    if (const auto *spec = findCppOperatorSpec(node)) {
        const auto &args = node->normInputs();
        switch (spec->emitKind) {
        case CppOperatorEmitKind::InlineUnary:
            return std::format("({}{})", spec->symbol, emitExpr(args.front(), cache));

        case CppOperatorEmitKind::InlineBinary:
            return std::format(
                "({} {} {})",
                emitExpr(args.front(), cache),
                spec->symbol,
                emitExpr(args.back(), cache));

        case CppOperatorEmitKind::NumericCast:
            return std::format(
                "static_cast<{}>({})",
                cppTypeFor(node->dataType()),
                emitExpr(args.front(), cache));

        case CppOperatorEmitKind::RuntimeFallback:
            break;
        }
    }

    return emitRuntimeFallbackExpr(node, cache);
}

std::string CppEmitter::emitRuntimeFallbackExpr(OperNode *node, expr_cache_t &cache) {
    if (!bridgePlan_) {
        throw std::runtime_error("bridge plan is not initialized for runtime fallback emission");
    }
    return emitCppRuntimeFallbackExpr(*bridgePlan_, node, [&](Node *arg) {
        return emitExpr(arg, cache);
    });
}

std::string CppEmitter::emitTypeAccessorRef(camel::core::type::Type *type) const {
    if (!bridgePlan_) {
        throw std::runtime_error("bridge plan is not initialized for type accessor emission");
    }
    return cppBridgeTypeAccessorName(*bridgePlan_, type) + "()";
}

std::string CppEmitter::emitExpr(Node *node, expr_cache_t &cache) {
    auto it = cache.find(node);
    if (it != cache.end()) {
        return it->second;
    }

    std::string result;
    switch (node->type()) {
    case NodeType::DATA: {
        auto *dataNode = tt::as_ptr<DataNode>(node);
        auto literal   = cppLiteralFor(dataNode->dataSlot(), dataNode->dataType());
        if (!literal.has_value()) {
            throw std::runtime_error(
                std::format("unsupported literal in node '{}'", node->toString()));
        }
        result = *literal;
    } break;

    case NodeType::PORT: {
        auto *portNode = tt::as_ptr<PortNode>(node);
        result         = namer_.valueSymbol(portNode, portNode->name());
    } break;

    case NodeType::OPER: {
        auto *opNode = tt::as_ptr<OperNode>(node);
        result       = emitOperatorExpr(opNode, cache);
    } break;

    case NodeType::FUNC: {
        auto *funcNode = tt::as_ptr<FuncNode>(node);
        Graph *callee  = funcNode->bodyGraph();
        std::ostringstream oss;
        oss << namer_.graphSymbol(callee) << "(";
        bool first = true;
        for (const auto &arg : node->normInputs()) {
            if (!first) {
                oss << ", ";
            }
            first = false;
            oss << emitExpr(arg, cache);
        }
        for (const auto &arg : node->withInputs()) {
            if (!first) {
                oss << ", ";
            }
            first = false;
            oss << emitExpr(arg, cache);
        }
        oss << ")";
        result = oss.str();
    } break;

    case NodeType::JOIN: {
        auto *joinNode = tt::as_ptr<JoinNode>(node);
        if (joinNode->armCount() != 2 || !joinNode->hasMatchedBranch()) {
            throw std::runtime_error("JOIN shape is not supported for direct C++ emission");
        }
        auto *brchNode = joinNode->matchedBranch();
        if (!brchNode->hasSelectorInput()) {
            throw std::runtime_error("JOIN is not driven by a supported BRCH node");
        }
        const auto condExpr = emitExpr(brchNode->selectorInput(), cache);
        const auto thenExpr = emitExpr(joinNode->armTail(0), cache);
        const auto elseExpr = emitExpr(joinNode->armTail(1), cache);
        result              = std::format("({} ? {} : {})", condExpr, thenExpr, elseExpr);
    } break;

    case NodeType::COPY:
        result = emitExpr(node->normInputs().front(), cache);
        break;

    case NodeType::CAST:
        result = std::format(
            "static_cast<{}>({})",
            cppTypeFor(node->dataType()),
            emitExpr(node->normInputs().front(), cache));
        break;

    default:
        throw std::runtime_error(
            std::format(
                "node '{}' is not supported by direct expression emission",
                node->toString()));
    }

    cache[node] = result;
    return result;
}

std::string CppEmitter::emitCommentary(const GraphLoweringPlan &plan) const {
    if (!isInspectMode()) {
        return "";
    }

    std::ostringstream oss;
    oss << "// graph: " << plan.graph->name() << "\n";
    oss << "// frame-elided: " << (plan.frameElidable ? "true" : "false") << "\n";
    oss << "// topo:";
    for (const auto &node : plan.topoNodes) {
        oss << "\n//   [" << node->index() << "] " << node->toString();
    }
    oss << "\n";
    return oss.str();
}

std::string CppEmitter::emitProgramMain(Graph *graph, const CppBridgePlan *bridgePlan) {
    if (!graph || graph->name() != "main") {
        throw std::runtime_error(
            "std::cpp executable emission expects the resolved entry graph to be 'main'");
    }
    if (!graph->normPorts().empty() || !graph->withPorts().empty()) {
        throw std::runtime_error(
            "Camel 'main' must not take parameters for std::cpp executable emission");
    }
    const auto retCode = graph->funcType()->exitType()->code();
    if (retCode != TypeCode::Int32 && retCode != TypeCode::Int64) {
        throw std::runtime_error(
            "Camel 'main' must return int-compatible type for std::cpp executable emission");
    }

    std::ostringstream oss;
    oss << "int main() {\n";
    if (bridgePlan && bridgePlan->usesRuntimeBridge) {
        oss << emitCppBridgeInitializeCall(*bridgePlan);
        oss << "    int ret = 0;\n";
        oss << "    try {\n";
        oss << "        ret = static_cast<int>(generated::" << namer_.graphSymbol(graph)
            << "());\n";
        oss << "    } catch (const std::exception &e) {\n";
        oss << "        std::cerr << \"Error: \" << e.what() << std::endl;\n";
        oss << "        ret = 1;\n";
        oss << "    } catch (...) {\n";
        oss << "        std::cerr << \"Unknown error\" << std::endl;\n";
        oss << "        ret = 1;\n";
        oss << "    }\n";
        oss << "    finalize();\n";
        oss << "    return ret;\n";
    } else {
        oss << "    int ret = 0;\n";
        oss << "    try {\n";
        oss << "        ret = static_cast<int>(generated::" << namer_.graphSymbol(graph)
            << "());\n";
        oss << "    } catch (const std::exception &e) {\n";
        oss << "        std::cerr << \"Error: \" << e.what() << std::endl;\n";
        oss << "        ret = 1;\n";
        oss << "    } catch (...) {\n";
        oss << "        std::cerr << \"Unknown error\" << std::endl;\n";
        oss << "        ret = 1;\n";
        oss << "    }\n";
        oss << "    return ret;\n";
    }
    oss << "}\n";
    return oss.str();
}

std::string CppEmitter::emitDirectFunction(Graph *graph, const GraphLoweringPlan &plan) {
    expr_cache_t cache;
    std::ostringstream oss;
    oss << emitCommentary(plan);
    oss << emitFunctionSignature(graph, true) << " {\n";
    if (shouldEmitStatementBody(plan)) {
        // 收集 JOIN 的 then/else 臂节点，不单独作为语句发射，由 JOIN 的 ternary 内联求值以实现短路
        std::unordered_set<Node *> joinArmNodes;
        for (Node *n : plan.topoNodes) {
            if (n->type() == NodeType::JOIN) {
                auto *joinNode = tt::as_ptr<JoinNode>(n);
                for (size_t i = 0; i < joinNode->armCount(); ++i) {
                    joinArmNodes.insert(joinNode->armTail(i));
                }
            }
        }

        size_t stmtIdx = 1;
        for (auto *node : collectStatementNodes(plan)) {
            switch (node->type()) {
            case NodeType::DATA:
            case NodeType::PORT:
            case NodeType::EXIT:
                continue;
            case NodeType::BRCH:
                continue;
            default:
                break;
            }
            if (joinArmNodes.count(node)) {
                continue; // 由 JOIN 的 ternary 内联求值，确保短路
            }
            const std::string expr = stripRedundantOuterParens(emitExpr(node, cache));
            const std::string name = emitBindingSymbol(node, stmtIdx++);
            oss << "    const " << cppTypeFor(node->dataType()) << " " << name << " = " << expr
                << ";\n";
            cache[node] = name;
        }
    }
    oss << "    return " << stripRedundantOuterParens(emitExpr(graph->outputNode(), cache))
        << ";\n";
    oss << "}\n";
    return oss.str();
}

std::string CppEmitter::emitPlainUnit(Graph *graph, const std::vector<GraphLoweringPlan> &plans) {
    const auto plan = collectCppBridgePlan(context_, plans);
    std::ostringstream oss;
    oss << emitCppBridgePreamble(plan, true);
    oss << "namespace generated {\n";
    oss << emitCppBridgeSupport(plan);
    oss << emitForwardDeclarations(plans) << "\n";
    bridgePlan_ = &plan;
    for (const auto &plan : plans) {
        oss << emitDirectFunction(plan.graph, plan) << "\n";
    }
    bridgePlan_ = nullptr;
    oss << "} // namespace generated\n\n";
    oss << emitProgramMain(graph, &plan);
    return oss.str();
}

std::string CppEmitter::emitModuleUnit(Graph *graph, const std::vector<GraphLoweringPlan> &plans) {
    (void)graph;
    const auto plan = collectCppBridgePlan(context_, plans);
    std::ostringstream oss;
    oss << emitCppBridgePreamble(plan, false);
    oss << "namespace generated {\n";
    oss << emitCppBridgeSupport(plan);
    oss << emitForwardDeclarations(plans) << "\n";
    bridgePlan_ = &plan;
    for (const auto &plan : plans) {
        oss << emitDirectFunction(plan.graph, plan) << "\n";
    }
    bridgePlan_ = nullptr;
    oss << "} // namespace generated\n";
    return oss.str();
}
