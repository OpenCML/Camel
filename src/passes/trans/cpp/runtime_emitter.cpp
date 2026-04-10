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
 * Created: Apr. 10, 2026
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "runtime_emitter.h"

#include "camel/core/module/module.h"
#include "camel/core/rtdata/func.h"
#include "camel/execute/graph_runtime_support.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

using namespace camel::runtime;
using namespace camel::core::type;

namespace {

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

std::string collapseUnderscores(std::string text) {
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
    return out;
}

std::string sanitizeGraphSymbol(const std::string &name) {
    static const std::regex anonymousBranchPattern("^__([0-9]+)__$");
    std::smatch match;
    if (std::regex_match(name, match, anonymousBranchPattern)) {
        return "l_" + match[1].str();
    }

    std::string sanitized = collapseUnderscores(sanitizeHint(name.empty() ? "graph" : name));
    if (sanitized.empty()) {
        sanitized = "graph";
    }
    if (std::isdigit(static_cast<unsigned char>(sanitized.front()))) {
        sanitized.insert(sanitized.begin(), '_');
    }
    return "f_" + sanitized;
}

template <typename T>
void collectDepsDepthFirst(T *graph, std::vector<T *> &ordered, std::unordered_set<T *> &visited) {
    if (!visited.insert(graph).second) {
        return;
    }
    for (T *dep : graph->dependencies()) {
        if (dep) {
            collectDepsDepthFirst(dep, ordered, visited);
        }
    }
    ordered.push_back(graph);
}

const GCNode *requireNode(GCGraph *graph, gc_node_ref_t nodeRef) {
    const auto *node = graph ? graph->node(nodeRef) : nullptr;
    if (!node) {
        throw std::runtime_error("runtime cpp emitter resolved to a null node");
    }
    return node;
}

uintptr_t nodeKey(GCGraph *graph, gc_node_ref_t nodeRef) {
    return (static_cast<uintptr_t>(reinterpret_cast<uintptr_t>(graph)) << 16) |
           static_cast<uintptr_t>(nodeRef);
}

std::string joinBranchExprs(
    const std::vector<std::pair<std::string, std::string>> &cases, const std::string &fallback) {
    std::string expr = fallback;
    for (auto it = cases.rbegin(); it != cases.rend(); ++it) {
        expr = std::format("(({}) ? ({}) : ({}))", it->first, it->second, expr);
    }
    return expr;
}

std::string stripRedundantOuterParens(std::string_view s) {
    if (s.size() < 2 || s.front() != '(' || s.back() != ')') {
        return std::string(s);
    }
    int depth = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '(') {
            ++depth;
        } else if (s[i] == ')') {
            --depth;
            if (depth == 0) {
                return (i == s.size() - 1) ? std::string(s.substr(1, s.size() - 2))
                                           : std::string(s);
            }
        }
    }
    return std::string(s);
}

} // namespace

std::string RuntimeCppEmitter::emit(GCGraph *root) {
    auto *target = resolveTargetGraph(root);
    if (!target) {
        throw std::runtime_error("no suitable target graph found for runtime C++ emission");
    }

    const auto emissionOrder = collectEmissionOrder(collectEmissionRoots(target));
    assignGraphSymbols(emissionOrder);

    std::vector<RuntimeCppGraphPlan> plans;
    for (GCGraph *graph : emissionOrder) {
        RuntimeCppGraphPlan plan = analyzeGraph(graph);
        if (!plan.directCallable) {
            throw std::runtime_error(
                std::format(
                    "runtime graph '{}' is not supported by the direct C++ path: {}",
                    graph->name(),
                    joinReasons(plan.issues)));
        }
        plans.push_back(std::move(plan));
    }

    switch (mode_) {
    case CppEmitMode::Plain:
        return emitPlainUnit(root, target, plans);
    case CppEmitMode::Module:
    case CppEmitMode::Inspect:
        return emitModuleUnit(target, plans);
    case CppEmitMode::Bench:
        return emitPlainUnit(root, target, plans);
    default:
        throw std::runtime_error("unknown runtime C++ emission mode");
    }
}

void RuntimeCppEmitter::assignGraphSymbols(const std::vector<GCGraph *> &graphs) {
    for (GCGraph *graph : graphs) {
        if (!graph || graphNames_.contains(graph)) {
            continue;
        }
        std::string base = sanitizeGraphSymbol(graph->name());
        std::string name = base;
        size_t suffix    = 0;
        while (!usedNames_.insert(name).second) {
            name = std::format("{}_{}", base, ++suffix);
        }
        graphNames_[graph] = name;
    }
}

GCGraph *RuntimeCppEmitter::resolveTargetGraph(GCGraph *root) const {
    if (!root) {
        return nullptr;
    }

    auto reachable        = camel::runtime::collectReachableGraphs(root);
    auto findUniqueByName = [&](const std::string &name) -> GCGraph * {
        GCGraph *match = nullptr;
        for (auto *graph : reachable) {
            if (!graph || graph->name() != name) {
                continue;
            }
            if (match != nullptr) {
                throw std::runtime_error(std::format("multiple '{}' graphs found", name));
            }
            match = graph;
        }
        return match;
    };

    if (isExecutableMode()) {
        if (auto *main = findUniqueByName("main")) {
            return main;
        }
        throw std::runtime_error(
            "std::cpp requires a 'main' graph; use std::cpp::module to dump a module without an "
            "entry point");
    }
    if (mode_ == CppEmitMode::Bench) {
        if (auto *fib = findUniqueByName("fib")) {
            return fib;
        }
        if (auto *main = findUniqueByName("main")) {
            return main;
        }
    }
    return root;
}

std::vector<GCGraph *> RuntimeCppEmitter::collectEmissionRoots(GCGraph *entry) const {
    if (isExecutableMode()) {
        return {entry};
    }

    std::vector<GCGraph *> roots;
    for (GCGraph *graph : entry->subGraphs()) {
        if (!graph || isAnonymousGraphName(graph->name())) {
            continue;
        }
        roots.push_back(graph);
    }

    if (roots.empty()) {
        roots.push_back(entry);
    }

    std::sort(roots.begin(), roots.end(), [](const GCGraph *lhs, const GCGraph *rhs) {
        return lhs->name() < rhs->name();
    });
    roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
    return roots;
}

std::vector<GCGraph *>
RuntimeCppEmitter::collectEmissionOrder(const std::vector<GCGraph *> &entries) const {
    std::vector<GCGraph *> ordered;
    std::unordered_set<GCGraph *> visited;
    for (GCGraph *entry : entries) {
        collectDepsDepthFirst(entry, ordered, visited);
    }
    return ordered;
}

RuntimeCppGraphPlan RuntimeCppEmitter::analyzeGraph(GCGraph *graph) const {
    RuntimeCppGraphPlan plan;
    plan.graph     = graph;
    plan.topoNodes = camel::execute::buildReachableExecutionTopoIndices(graph);

    auto addIssue = [&](gc_node_ref_t nodeRef, std::string reason) {
        plan.issues.push_back({
            .graph   = graph,
            .nodeRef = nodeRef,
            .reason  = std::move(reason),
        });
    };

    for (gc_node_ref_t portRef : graph->normPorts()) {
        const auto *port = requireNode(graph, portRef);
        if (!isBridgeableTypeForCpp(port->dataType)) {
            addIssue(portRef, "non-bridgeable norm port type");
        }
    }
    for (gc_node_ref_t portRef : graph->withPorts()) {
        const auto *port = requireNode(graph, portRef);
        if (!isBridgeableTypeForCpp(port->dataType)) {
            addIssue(portRef, "non-bridgeable with port type");
        }
    }
    if (auto *ret = const_cast<Type *>(graph->funcType()->exitType());
        !isBridgeableTypeForCpp(ret)) {
        addIssue(graph->returnNodeRef(), "non-bridgeable return type");
    }

    for (gc_node_ref_t nodeRef : plan.topoNodes) {
        const auto *node = requireNode(graph, nodeRef);
        switch (node->kind) {
        case GCNodeKind::Data: {
            auto literal =
                cppLiteralFor(graph->staticArea()->get<slot_t>(-node->dataIndex), node->dataType);
            if (!literal.has_value()) {
                addIssue(nodeRef, "data literal cannot be emitted as a C++ literal");
            }
        } break;
        case GCNodeKind::Port:
            break;
        case GCNodeKind::Oper: {
            if (!isBridgeableTypeForCpp(node->dataType)) {
                addIssue(nodeRef, "operator result type is not bridgeable");
                break;
            }
            bool bridgeable = true;
            for (gc_node_ref_t argRef : graph->normInputsOf(nodeRef)) {
                if (!isBridgeableTypeForCpp(requireNode(graph, argRef)->dataType)) {
                    bridgeable = false;
                    break;
                }
            }
            for (gc_node_ref_t argRef : graph->withInputsOf(nodeRef)) {
                if (!isBridgeableTypeForCpp(requireNode(graph, argRef)->dataType)) {
                    bridgeable = false;
                    break;
                }
            }
            if (!bridgeable) {
                addIssue(nodeRef, "operator arguments are not bridgeable");
            }
        } break;
        case GCNodeKind::Func: {
            auto *callee = graph->directCalleeGraphOf(nodeRef);
            if (!callee) {
                addIssue(nodeRef, "direct callee graph is missing");
                break;
            }
            if (!callee->closureNodes().empty()) {
                addIssue(nodeRef, "direct callee closure is not supported");
            }
            if (!isBridgeableTypeForCpp(const_cast<Type *>(callee->funcType()->exitType()))) {
                addIssue(nodeRef, "callee return type is not bridgeable");
            }
        } break;
        case GCNodeKind::Brch: {
            const auto arms = graph->ctrlOutputsOf(nodeRef);
            if (arms.size() < 2) {
                addIssue(nodeRef, "branch must have at least two control arms");
            }
            const auto *body      = graph->nodeBodyAs<GCBrchBody>(nodeRef);
            gc_node_ref_t joinRef = body->join;
            if (joinRef == kInvalidNodeRef) {
                addIssue(nodeRef, "branch must feed a JOIN");
                break;
            }
            const auto *joinNode = graph->node(joinRef);
            if (!joinNode || joinNode->kind != GCNodeKind::Join) {
                addIssue(nodeRef, "branch join reference is invalid");
                break;
            }
            const auto joinInputs = graph->withInputsOf(joinRef);
            if (joinInputs.size() != arms.size()) {
                addIssue(nodeRef, "JOIN arm count mismatch");
                break;
            }
            for (size_t i = 0; i < arms.size(); ++i) {
                auto arm = camel::execute::collectRuntimeBranchArmRegion(graph, nodeRef, i);
                for (gc_node_ref_t inlineNodeRef : arm.topoIndices) {
                    plan.inlineOnlyNodes.insert(inlineNodeRef);
                }
                plan.inlineOnlyNodes.insert(joinRef);
            }
        } break;
        case GCNodeKind::Join:
            break;
        case GCNodeKind::Cast:
        case GCNodeKind::Copy:
        case GCNodeKind::Gate:
            if (!isBridgeableTypeForCpp(node->dataType)) {
                addIssue(nodeRef, "node result type is not bridgeable");
            }
            break;
        case GCNodeKind::Call:
        case GCNodeKind::Fill:
        case GCNodeKind::Accs:
        case GCNodeKind::Bind:
        case GCNodeKind::Sync:
        case GCNodeKind::Dref:
            addIssue(nodeRef, "node type is not supported by runtime C++ lowering yet");
            break;
        default:
            addIssue(nodeRef, "unknown runtime node type");
            break;
        }
    }

    plan.directCallable = plan.issues.empty();
    return plan;
}

CppBridgePlan
RuntimeCppEmitter::collectBridgePlan(const std::vector<RuntimeCppGraphPlan> &plans) const {
    CppBridgePlan plan;
    std::set<std::string> importedModules;
    std::set<std::string> runtimeOperatorUris;
    std::unordered_set<Type *> usedTypes;

    auto recordTypeUsage = [&](Type *type) {
        if (!type) {
            return;
        }
        plan.usesRtdataNamespace = true;
        usedTypes.insert(type);
        if (type->code() == TypeCode::String) {
            plan.usesStringHeader = true;
            plan.usesMmNamespace  = true;
        }
    };

    auto operatorProtocol = [](const std::string &uri) {
        const size_t pos = uri.find(':');
        return pos == std::string::npos ? std::string() : uri.substr(0, pos);
    };

    for (const auto &graphPlan : plans) {
        if (graphPlan.graph) {
            recordTypeUsage(const_cast<Type *>(graphPlan.graph->funcType()->exitType()));
            for (gc_node_ref_t portRef : graphPlan.graph->normPorts()) {
                recordTypeUsage(requireNode(graphPlan.graph, portRef)->dataType);
            }
            for (gc_node_ref_t portRef : graphPlan.graph->withPorts()) {
                recordTypeUsage(requireNode(graphPlan.graph, portRef)->dataType);
            }
            for (gc_node_ref_t closureRef : graphPlan.graph->closureNodes()) {
                recordTypeUsage(requireNode(graphPlan.graph, closureRef)->dataType);
            }
        }

        for (gc_node_ref_t nodeRef : graphPlan.topoNodes) {
            const auto *node = requireNode(graphPlan.graph, nodeRef);
            recordTypeUsage(node->dataType);
            if (node->kind == GCNodeKind::Data) {
                auto literal = cppLiteralFor(
                    graphPlan.graph->staticArea()->get<slot_t>(-node->dataIndex),
                    node->dataType);
                if (literal.has_value() && node->dataType->code() == TypeCode::String) {
                    plan.usesRtdataNamespace = true;
                    plan.usesStringHeader    = true;
                    plan.usesMmNamespace     = true;
                }
            }
            if (node->kind != GCNodeKind::Oper) {
                continue;
            }
            plan.usesRuntimeBridge = true;
            runtimeOperatorUris.insert(
                std::string(graphPlan.graph->nodeBodyAs<GCOperBody>(nodeRef)->uri()));
        }
    }

    for (const auto &uri : runtimeOperatorUris) {
        plan.runtimeOperators.push_back({
            .uri  = uri,
            .name = cppBridgeAccessorNameForUri(uri),
        });
        const std::string protocol = operatorProtocol(uri);
        if (!protocol.empty() && context_) {
            auto module = context_->importModule(protocol);
            importedModules.insert(module->name());
        }
    }
    plan.importedModules.assign(importedModules.begin(), importedModules.end());
    return plan;
}

bool RuntimeCppEmitter::isExecutableMode() const {
    return mode_ == CppEmitMode::Plain || mode_ == CppEmitMode::Bench;
}

bool RuntimeCppEmitter::isInspectMode() const { return mode_ == CppEmitMode::Inspect; }

bool RuntimeCppEmitter::shouldEmitStatementBody(const RuntimeCppGraphPlan &plan) const {
    for (gc_node_ref_t nodeRef : plan.topoNodes) {
        const auto *node = requireNode(plan.graph, nodeRef);
        switch (node->kind) {
        case GCNodeKind::Data:
        case GCNodeKind::Port:
        case GCNodeKind::Brch:
            continue;
        default:
            return true;
        }
    }
    return false;
}

std::vector<gc_node_ref_t>
RuntimeCppEmitter::collectStatementNodes(const RuntimeCppGraphPlan &plan) const {
    std::vector<gc_node_ref_t> ordered;
    ordered.reserve(plan.topoNodes.size());

    std::unordered_map<gc_node_ref_t, size_t> originalOrder;
    std::unordered_map<gc_node_ref_t, size_t> indegree;
    std::unordered_map<gc_node_ref_t, std::vector<gc_node_ref_t>> successors;
    for (size_t i = 0; i < plan.topoNodes.size(); ++i) {
        const gc_node_ref_t nodeRef = plan.topoNodes[i];
        originalOrder[nodeRef]      = i;
        indegree[nodeRef]           = 0;
    }

    auto addEdge = [&](gc_node_ref_t from, gc_node_ref_t to) {
        if (!indegree.contains(from) || !indegree.contains(to)) {
            return;
        }
        successors[from].push_back(to);
        ++indegree[to];
    };

    for (gc_node_ref_t nodeRef : plan.topoNodes) {
        for (gc_node_ref_t input : plan.graph->normInputsOf(nodeRef)) {
            addEdge(input, nodeRef);
        }
        for (gc_node_ref_t input : plan.graph->withInputsOf(nodeRef)) {
            addEdge(input, nodeRef);
        }
        for (gc_node_ref_t input : plan.graph->ctrlInputsOf(nodeRef)) {
            addEdge(input, nodeRef);
        }
    }

    std::vector<gc_node_ref_t> ready;
    for (gc_node_ref_t nodeRef : plan.topoNodes) {
        if (indegree[nodeRef] == 0) {
            ready.push_back(nodeRef);
        }
    }

    while (!ready.empty()) {
        auto it =
            std::min_element(ready.begin(), ready.end(), [&](gc_node_ref_t lhs, gc_node_ref_t rhs) {
                return originalOrder[lhs] < originalOrder[rhs];
            });
        const gc_node_ref_t nodeRef = *it;
        ready.erase(it);
        ordered.push_back(nodeRef);

        auto succIt = successors.find(nodeRef);
        if (succIt == successors.end()) {
            continue;
        }
        for (gc_node_ref_t succ : succIt->second) {
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

std::string RuntimeCppEmitter::graphSymbol(GCGraph *graph) const {
    auto it = graphNames_.find(graph);
    ASSERT(it != graphNames_.end(), "Runtime C++ graph symbol is not assigned.");
    return it->second;
}

std::string
RuntimeCppEmitter::emitForwardDeclarations(const std::vector<RuntimeCppGraphPlan> &plans) {
    std::ostringstream oss;
    for (const auto &plan : plans) {
        oss << emitFunctionSignature(plan.graph, true) << ";\n";
    }
    return oss.str();
}

std::string RuntimeCppEmitter::emitFunctionSignature(GCGraph *graph, bool forceInline) {
    std::ostringstream oss;
    oss << (forceInline ? "static inline " : "static ");
    oss << cppTypeFor(const_cast<Type *>(graph->funcType()->exitType())) << " "
        << graphSymbol(graph) << "(";

    bool first = true;
    for (gc_node_ref_t portRef : graph->normPorts()) {
        const auto *port = requireNode(graph, portRef);
        if (!first) {
            oss << ", ";
        }
        first = false;
        oss << cppTypeFor(port->dataType) << " " << emitBindingSymbol(graph, portRef, std::nullopt);
    }
    for (gc_node_ref_t portRef : graph->withPorts()) {
        const auto *port = requireNode(graph, portRef);
        if (!first) {
            oss << ", ";
        }
        first = false;
        oss << cppTypeFor(port->dataType) << " " << emitBindingSymbol(graph, portRef, std::nullopt);
    }
    for (gc_node_ref_t closureRef : graph->closureNodes()) {
        const auto *closureNode = requireNode(graph, closureRef);
        if (!first) {
            oss << ", ";
        }
        first = false;
        oss << cppTypeFor(closureNode->dataType) << " "
            << emitBindingSymbol(graph, closureRef, std::nullopt);
    }
    oss << ")";
    return oss.str();
}

std::string RuntimeCppEmitter::emitBindingSymbol(
    GCGraph *graph, gc_node_ref_t nodeRef, std::optional<size_t> stmtIndex) {
    const uintptr_t key = nodeKey(graph, nodeRef);
    auto it             = valueNames_.find(key);
    if (it != valueNames_.end()) {
        return it->second;
    }

    const auto *node = requireNode(graph, nodeRef);
    std::string hint = "value";
    switch (node->kind) {
    case GCNodeKind::Port:
        hint = "arg";
        for (gc_node_ref_t closureRef : graph->closureNodes()) {
            if (closureRef == nodeRef) {
                hint = "closure";
                break;
            }
        }
        break;
    case GCNodeKind::Oper:
        hint = tailAfterColon(std::string(graph->nodeBodyAs<GCOperBody>(nodeRef)->uri()));
        break;
    case GCNodeKind::Func: {
        auto *callee = graph->directCalleeGraphOf(nodeRef);
        hint         = callee ? callee->name() : "func";
    } break;
    case GCNodeKind::Join:
        hint = "join";
        break;
    case GCNodeKind::Cast:
        hint = "cast";
        break;
    case GCNodeKind::Copy:
        hint = "copy";
        break;
    default:
        break;
    }

    std::string base = sanitizeHint(hint);
    std::string name;
    if (stmtIndex.has_value()) {
        name = std::format("v{}_{}", *stmtIndex, base);
    } else {
        name = std::format("v_{}_{}", base, static_cast<unsigned>(nodeRef));
    }
    while (!usedNames_.insert(name).second) {
        name.push_back('_');
    }
    valueNames_[key] = name;
    return name;
}

std::string RuntimeCppEmitter::emitOperatorExpr(
    GCGraph *graph, gc_node_ref_t nodeRef, expr_cache_t &cache, const RuntimeCppGraphPlan &plan) {
    return emitRuntimeFallbackExpr(graph, nodeRef, cache, plan);
}

std::string RuntimeCppEmitter::emitRuntimeFallbackExpr(
    GCGraph *graph, gc_node_ref_t nodeRef, expr_cache_t &cache, const RuntimeCppGraphPlan &plan) {
    (void)plan;
    if (!bridgePlan_) {
        throw std::runtime_error("bridge plan is not initialized for runtime fallback emission");
    }

    const auto *node = requireNode(graph, nodeRef);
    std::ostringstream oss;
    const std::string uri = std::string(graph->nodeBodyAs<GCOperBody>(nodeRef)->uri());
    oss << cppBridgeAccessorNameForUri(uri) << "().call<" << cppTypeFor(node->dataType) << ">(";

    auto emitArgs = [&](std::span<const gc_node_ref_t> refs) {
        std::ostringstream args;
        bool first = true;
        for (gc_node_ref_t argRef : refs) {
            if (!first) {
                args << ", ";
            }
            first               = false;
            const auto *argNode = requireNode(graph, argRef);
            args << "val(" << cppBridgeTypeExprFor(*bridgePlan_, argNode->dataType) << ", "
                 << emitExpr(graph, argRef, cache, plan) << ")";
        }
        return args.str();
    };

    const auto normInputs = graph->normInputsOf(nodeRef);
    const auto withInputs = graph->withInputsOf(nodeRef);
    if (!normInputs.empty() || !withInputs.empty()) {
        oss << "{" << emitArgs(normInputs) << "}";
        if (!withInputs.empty()) {
            oss << ", {" << emitArgs(withInputs) << "}";
        }
    }
    oss << ")";
    return oss.str();
}

std::string RuntimeCppEmitter::emitExpr(
    GCGraph *graph, gc_node_ref_t nodeRef, expr_cache_t &cache, const RuntimeCppGraphPlan &plan) {
    auto it = cache.find(nodeRef);
    if (it != cache.end()) {
        return it->second;
    }

    const auto *node = requireNode(graph, nodeRef);
    std::string result;
    switch (node->kind) {
    case GCNodeKind::Data: {
        auto literal =
            cppLiteralFor(graph->staticArea()->get<slot_t>(-node->dataIndex), node->dataType);
        if (!literal.has_value()) {
            throw std::runtime_error(
                std::format("unsupported runtime literal in graph '{}'", graph->name()));
        }
        result = *literal;
    } break;
    case GCNodeKind::Port:
        result = emitBindingSymbol(graph, nodeRef, std::nullopt);
        break;
    case GCNodeKind::Oper:
        result = emitOperatorExpr(graph, nodeRef, cache, plan);
        break;
    case GCNodeKind::Func: {
        auto *callee = graph->directCalleeGraphOf(nodeRef);
        if (!callee) {
            throw std::runtime_error("runtime direct callee graph is missing");
        }
        std::ostringstream oss;
        oss << graphSymbol(callee) << "(";
        bool first = true;
        for (gc_node_ref_t argRef : graph->normInputsOf(nodeRef)) {
            if (!first) {
                oss << ", ";
            }
            first = false;
            oss << emitExpr(graph, argRef, cache, plan);
        }
        for (gc_node_ref_t argRef : graph->withInputsOf(nodeRef)) {
            if (!first) {
                oss << ", ";
            }
            first = false;
            oss << emitExpr(graph, argRef, cache, plan);
        }
        oss << ")";
        result = oss.str();
    } break;
    case GCNodeKind::Join: {
        const auto *body            = graph->nodeBodyAs<GCJoinBody>(nodeRef);
        const gc_node_ref_t brchRef = body->brch;
        if (brchRef == kInvalidNodeRef) {
            throw std::runtime_error("runtime JOIN has no matching BRCH");
        }
        const auto selectorInputs = graph->normInputsOf(brchRef);
        if (selectorInputs.empty()) {
            throw std::runtime_error("runtime BRCH has no selector input");
        }
        const std::string selectorExpr = emitExpr(graph, selectorInputs.front(), cache, plan);
        const auto caseInputs          = graph->withInputsOf(brchRef);
        const auto joinInputs          = graph->withInputsOf(nodeRef);
        if (joinInputs.empty()) {
            throw std::runtime_error("runtime JOIN has no arm values");
        }

        if (caseInputs.empty()) {
            if (joinInputs.size() != 2) {
                std::vector<std::pair<std::string, std::string>> cases;
                cases.reserve(joinInputs.size() - 1);
                for (size_t i = 0; i + 1 < joinInputs.size(); ++i) {
                    cases.push_back({
                        std::format("({} == {})", selectorExpr, i),
                        emitExpr(graph, joinInputs[i], cache, plan),
                    });
                }
                result = joinBranchExprs(cases, emitExpr(graph, joinInputs.back(), cache, plan));
                break;
            }
            result = std::format(
                "(({}) ? ({}) : ({}))",
                selectorExpr,
                emitExpr(graph, joinInputs[0], cache, plan),
                emitExpr(graph, joinInputs[1], cache, plan));
            break;
        }

        if (joinInputs.size() != caseInputs.size() + 1) {
            throw std::runtime_error("case-branch JOIN arm count mismatch");
        }
        std::vector<std::pair<std::string, std::string>> cases;
        cases.reserve(caseInputs.size());
        for (size_t i = 0; i < caseInputs.size(); ++i) {
            cases.push_back({
                std::format(
                    "({} == {})",
                    selectorExpr,
                    emitExpr(graph, caseInputs[i], cache, plan)),
                emitExpr(graph, joinInputs[i], cache, plan),
            });
        }
        result = joinBranchExprs(cases, emitExpr(graph, joinInputs.back(), cache, plan));
    } break;
    case GCNodeKind::Copy:
        result = emitExpr(graph, graph->normInputsOf(nodeRef).front(), cache, plan);
        break;
    case GCNodeKind::Cast:
        result = std::format(
            "static_cast<{}>({})",
            cppTypeFor(node->dataType),
            emitExpr(graph, graph->normInputsOf(nodeRef).front(), cache, plan));
        break;
    case GCNodeKind::Gate: {
        const auto normInputs = graph->normInputsOf(nodeRef);
        if (normInputs.empty()) {
            throw std::runtime_error("runtime GATE has no Norm input");
        }
        result = emitExpr(graph, normInputs.front(), cache, plan);
    } break;
    default:
        throw std::runtime_error(
            std::format(
                "runtime node kind {} is not supported by direct expression emission",
                static_cast<int>(node->kind)));
    }

    cache[nodeRef] = result;
    return result;
}

std::string RuntimeCppEmitter::emitCommentary(const RuntimeCppGraphPlan &plan) const {
    if (!isInspectMode()) {
        return "";
    }
    std::ostringstream oss;
    oss << "// graph: " << plan.graph->name() << "\n";
    oss << "// direct-callable: " << (plan.directCallable ? "true" : "false") << "\n";
    oss << "// topo:";
    for (gc_node_ref_t nodeRef : plan.topoNodes) {
        const auto *node = requireNode(plan.graph, nodeRef);
        oss << "\n//   [" << nodeRef << "] kind=" << static_cast<int>(node->kind)
            << " slot=" << node->dataIndex;
    }
    oss << "\n";
    return oss.str();
}

std::string RuntimeCppEmitter::emitExecutableEntryCall(GCGraph *root, GCGraph *graph) {
    if (!graph) {
        throw std::runtime_error("runtime executable entry graph is null");
    }

    std::ostringstream args;
    bool first     = true;
    auto appendArg = [&](const std::string &expr) {
        if (!first) {
            args << ", ";
        }
        first = false;
        args << expr;
    };

    if (graph->normPorts().empty() && graph->withPorts().empty() && graph->closureNodes().empty()) {
        return std::format("generated::{}()", graphSymbol(graph));
    }

    ASSERT(root != nullptr, "runtime executable bootstrap requires a non-null root graph");
    const gc_node_ref_t callRef = root->returnNodeRef();
    const auto *callNode        = root->node(callRef);
    if (!callNode || callNode->kind != GCNodeKind::Call) {
        throw std::runtime_error(
            "std::cpp executable bootstrap requires __root__ to end with an indirect CALL");
    }

    const auto callWithInputs = root->withInputsOf(callRef);
    if (callWithInputs.empty()) {
        throw std::runtime_error("runtime executable bootstrap CALL has no callee input");
    }
    const gc_node_ref_t fillRef = callWithInputs.front();
    const auto *fillNode        = root->node(fillRef);
    if (!fillNode || fillNode->kind != GCNodeKind::Fill) {
        throw std::runtime_error(
            "std::cpp executable bootstrap requires the CALL callee to be a FILL closure");
    }
    const auto *fillBody = root->nodeBodyAs<GCFillBody>(fillRef);
    if (fillBody->fillKind != GCFillKind::FunctionClosure) {
        throw std::runtime_error("std::cpp executable bootstrap requires a FunctionClosure fill");
    }

    const auto fillNormInputs = root->normInputsOf(fillRef);
    if (fillNormInputs.empty()) {
        throw std::runtime_error("runtime executable bootstrap FILL has no function source input");
    }
    const auto *sourceNode = root->node(fillNormInputs.front());
    if (!sourceNode || sourceNode->kind != GCNodeKind::Data) {
        throw std::runtime_error(
            "std::cpp executable bootstrap requires a static function source literal");
    }
    auto *funcObj = camel::core::rtdata::fromSlot<Function *>(
        root->staticArea()->get<slot_t>(-sourceNode->dataIndex));
    if (!funcObj || funcObj->graph() != graph) {
        throw std::runtime_error(
            "std::cpp executable bootstrap could not resolve the entry graph function literal");
    }

    expr_cache_t cache;
    RuntimeCppGraphPlan bootstrapPlan;
    bootstrapPlan.graph = root;
    for (gc_node_ref_t argRef : root->normInputsOf(callRef)) {
        appendArg(stripRedundantOuterParens(emitExpr(root, argRef, cache, bootstrapPlan)));
    }
    for (gc_node_ref_t argRef : root->withInputsOf(fillRef)) {
        appendArg(stripRedundantOuterParens(emitExpr(root, argRef, cache, bootstrapPlan)));
    }

    return std::format("generated::{}({})", graphSymbol(graph), args.str());
}

std::string
RuntimeCppEmitter::emitProgramMain(GCGraph *root, GCGraph *graph, const CppBridgePlan *bridgePlan) {
    if (!graph || graph->name() != "main") {
        throw std::runtime_error(
            "std::cpp executable emission expects the resolved entry graph to be 'main'");
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
    }
    oss << "    int ret = 0;\n";
    oss << "    try {\n";
    oss << "        ret = static_cast<int>(" << emitExecutableEntryCall(root, graph) << ");\n";
    oss << "    } catch (const std::exception &e) {\n";
    oss << "        std::cerr << \"Error: \" << e.what() << std::endl;\n";
    oss << "        ret = 1;\n";
    oss << "    } catch (...) {\n";
    oss << "        std::cerr << \"Unknown error\" << std::endl;\n";
    oss << "        ret = 1;\n";
    oss << "    }\n";
    if (bridgePlan && bridgePlan->usesRuntimeBridge) {
        oss << "    finalize();\n";
    }
    oss << "    return ret;\n";
    oss << "}\n";
    return oss.str();
}

std::string RuntimeCppEmitter::emitDirectFunction(GCGraph *graph, const RuntimeCppGraphPlan &plan) {
    expr_cache_t cache;
    std::ostringstream oss;
    oss << emitCommentary(plan);
    oss << emitFunctionSignature(graph, true) << " {\n";
    if (shouldEmitStatementBody(plan)) {
        std::unordered_set<gc_node_ref_t> joinArmNodes;
        for (gc_node_ref_t nodeRef : plan.topoNodes) {
            if (requireNode(graph, nodeRef)->kind != GCNodeKind::Join) {
                continue;
            }
            for (gc_node_ref_t armRef : graph->withInputsOf(nodeRef)) {
                joinArmNodes.insert(armRef);
            }
        }

        size_t stmtIdx = 1;
        for (gc_node_ref_t nodeRef : collectStatementNodes(plan)) {
            const auto *node = requireNode(graph, nodeRef);
            switch (node->kind) {
            case GCNodeKind::Data:
            case GCNodeKind::Port:
            case GCNodeKind::Brch:
                continue;
            default:
                break;
            }
            if (plan.inlineOnlyNodes.contains(nodeRef)) {
                continue;
            }
            if (joinArmNodes.contains(nodeRef)) {
                continue;
            }
            const std::string expr =
                stripRedundantOuterParens(emitExpr(graph, nodeRef, cache, plan));
            const std::string name = emitBindingSymbol(graph, nodeRef, stmtIdx++);
            oss << "    const " << cppTypeFor(node->dataType) << " " << name << " = " << expr
                << ";\n";
            cache[nodeRef] = name;
        }
    }
    oss << "    return "
        << stripRedundantOuterParens(emitExpr(graph, graph->outputNodeRef(), cache, plan)) << ";\n";
    oss << "}\n";
    return oss.str();
}

std::string RuntimeCppEmitter::emitPlainUnit(
    GCGraph *root, GCGraph *graph, const std::vector<RuntimeCppGraphPlan> &plans) {
    const auto plan = collectBridgePlan(plans);
    std::ostringstream oss;
    oss << emitCppBridgePreamble(plan, true);
    oss << "namespace generated {\n";
    oss << emitCppBridgeSupport(plan);
    oss << emitForwardDeclarations(plans) << "\n";
    bridgePlan_ = &plan;
    for (const auto &graphPlan : plans) {
        oss << emitDirectFunction(graphPlan.graph, graphPlan) << "\n";
    }
    bridgePlan_ = nullptr;
    oss << "} // namespace generated\n\n";
    oss << emitProgramMain(root, graph, &plan);
    return oss.str();
}

std::string
RuntimeCppEmitter::emitModuleUnit(GCGraph *graph, const std::vector<RuntimeCppGraphPlan> &plans) {
    (void)graph;
    const auto plan = collectBridgePlan(plans);
    std::ostringstream oss;
    oss << emitCppBridgePreamble(plan, false);
    oss << "namespace generated {\n";
    oss << emitCppBridgeSupport(plan);
    oss << emitForwardDeclarations(plans) << "\n";
    bridgePlan_ = &plan;
    for (const auto &graphPlan : plans) {
        oss << emitDirectFunction(graphPlan.graph, graphPlan) << "\n";
    }
    bridgePlan_ = nullptr;
    oss << "} // namespace generated\n";
    return oss.str();
}

std::string
RuntimeCppEmitter::joinReasons(const std::vector<RuntimeCppLoweringIssue> &issues) const {
    std::string joined;
    for (size_t i = 0; i < issues.size(); ++i) {
        if (i != 0) {
            joined += "; ";
        }
        joined += std::format("node#{}: {}", issues[i].nodeRef, issues[i].reason);
    }
    return joined;
}
