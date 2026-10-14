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

#include "program_emitter.h"

#include "camel/core/module/module.h"
#include "camel/core/rtdata/func.h"
#include "camel/execute/graph_runtime_support.h"
#include "camel/execute/limits.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace camel::passes::cpp {

using camel::core::type::TypeCode;
using camel::runtime::GCFillBody;
using camel::runtime::GCFillKind;
using camel::runtime::GCNodeKind;
using camel::runtime::kInvalidNodeRef;

namespace {

bool isAnonymous(const std::string &name) {
    return name.size() >= 4 && name.starts_with("__") && name.ends_with("__");
}

/// The graph of a function constant (`Function(graph=main, ...)`) in a DATA node of `graph`.
GCGraph *functionConstant(const GCGraph *graph, gc_node_ref_t ref) {
    const auto *node = graph->node(ref);
    if (node == nullptr || node->kind != GCNodeKind::Data) {
        return nullptr;
    }
    auto *function = camel::core::rtdata::fromSlot<::Function *>(
        graph->staticArea()->get<slot_t>(-node->dataIndex));
    return function != nullptr ? function->graph() : nullptr;
}

const char *kindName(GCNodeKind kind) {
    switch (kind) {
    case GCNodeKind::Data:
        return "DATA";
    case GCNodeKind::Port:
        return "PORT";
    case GCNodeKind::Cast:
        return "CAST";
    case GCNodeKind::Copy:
        return "COPY";
    case GCNodeKind::Fill:
        return "FILL";
    case GCNodeKind::Accs:
        return "ACCS";
    case GCNodeKind::Brch:
        return "BRCH";
    case GCNodeKind::Join:
        return "JOIN";
    case GCNodeKind::Call:
        return "CALL";
    case GCNodeKind::Bind:
        return "BIND";
    case GCNodeKind::Func:
        return "FUNC";
    case GCNodeKind::Oper:
        return "OPER";
    case GCNodeKind::Sync:
        return "SYNC";
    case GCNodeKind::Gate:
        return "GATE";
    case GCNodeKind::Dref:
        return "DREF";
    }
    return "?";
}

} // namespace

ProgramEmitter::Entry ProgramEmitter::findEntry(GCGraph *root) const {
    // The root graph ends with the call of `main`: through a closure (FILL) when main captures
    // values of the top level, else through the function constant itself.
    Entry entry;
    const gc_node_ref_t call = root->returnNodeRef();
    const auto *callNode     = root->node(call);
    if (callNode == nullptr || callNode->kind != GCNodeKind::Call || root->withInputsOf(call).empty()) {
        throw Refusal{root, call, "the program has no call of 'main' std::cpp can find", false};
    }
    const gc_node_ref_t callee = root->withInputsOf(call).front();
    const auto *calleeNode     = root->node(callee);
    if (calleeNode->kind == GCNodeKind::Fill &&
        root->nodeBodyAs<GCFillBody>(callee)->fillKind == GCFillKind::FunctionClosure &&
        !root->normInputsOf(callee).empty()) {
        entry.main = functionConstant(root, root->normInputsOf(callee).front());
        for (gc_node_ref_t arg : root->withInputsOf(callee)) {
            entry.args.push_back(arg);
        }
    } else {
        entry.main = functionConstant(root, callee);
    }
    if (entry.main == nullptr) {
        throw Refusal{root, call, "the program has no call of 'main' std::cpp can find", false};
    }
    return entry;
}

std::vector<GCGraph *> ProgramEmitter::collectFunctions(const std::vector<GCGraph *> &roots) {
    std::vector<GCGraph *> functions;
    std::unordered_set<const GCGraph *> seen;
    std::vector<GCGraph *> work(roots.rbegin(), roots.rend());

    // The graphs a graph calls directly, looking through the arms lowered in place.
    std::function<void(GCGraph *)> scan = [&](GCGraph *graph) {
        for (gc_node_ref_t ref : camel::execute::buildReachableExecutionTopoIndices(graph)) {
            const auto *node = graph->node(ref);
            if (node->kind != GCNodeKind::Func) {
                continue;
            }
            GCGraph *callee = graph->directCalleeGraphOf(ref);
            if (callee == nullptr) {
                continue;
            }
            if (isArmCall(graph, ref)) {
                scan(callee);
            } else if (!seen.contains(callee)) {
                work.push_back(callee);
            }
        }
    };

    while (!work.empty()) {
        GCGraph *graph = work.back();
        work.pop_back();
        if (!seen.insert(graph).second) {
            continue;
        }
        functions.push_back(graph);
        const std::string &name = graph->name();
        std::string base        = isAnonymous(name) ? "function" : name.substr(0, name.find('$'));
        parts_.functionNames[graph] = globals_.unique(cppIdentifier(base));
        scan(graph);
    }
    return functions;
}

std::vector<std::vector<GCGraph *>>
ProgramEmitter::tailCallGroups(const std::vector<GCGraph *> &functions) {
    // Which functions each one calls in tail position: written once to find out (what it
    // writes goes to the shared tables, which the real writing then finds already there).
    std::unordered_map<const GCGraph *, std::vector<GCGraph *>> calls;
    std::unordered_map<const GCGraph *, std::string> result;
    // The signatures first: a function whose parameters or result have no translation is
    // refused at its definition before anything in a body is looked at.
    std::vector<std::unique_ptr<FunctionEmitter>> probes;
    for (GCGraph *graph : functions) {
        probes.push_back(std::make_unique<FunctionEmitter>(parts_, std::vector<GCGraph *>{graph}));
    }
    for (size_t i = 0; i < functions.size(); ++i) {
        GCGraph *graph = functions[i];
        probes[i]->definition();
        for (const GCGraph *callee : probes[i]->tailCallees()) {
            calls[graph].push_back(const_cast<GCGraph *>(callee));
        }
        result[graph] = cppTypeOf(graph->funcType()->exitType()).value_or("");
    }

    // The strongly connected components of "calls in tail position" (Tarjan).
    std::unordered_map<const GCGraph *, size_t> index, low;
    std::unordered_set<const GCGraph *> onStack;
    std::vector<GCGraph *> stack;
    std::vector<std::vector<GCGraph *>> components;
    size_t next = 0;
    std::function<void(GCGraph *)> visit = [&](GCGraph *g) {
        index[g] = low[g] = next++;
        stack.push_back(g);
        onStack.insert(g);
        for (GCGraph *callee : calls[g]) {
            if (!index.contains(callee)) {
                visit(callee);
                low[g] = std::min(low[g], low[callee]);
            } else if (onStack.contains(callee)) {
                low[g] = std::min(low[g], index[callee]);
            }
        }
        if (low[g] == index[g]) {
            std::vector<GCGraph *> component;
            GCGraph *member = nullptr;
            do {
                member = stack.back();
                stack.pop_back();
                onStack.erase(member);
                component.push_back(member);
            } while (member != g);
            components.push_back(std::move(component));
        }
    };
    for (GCGraph *graph : functions) {
        if (!index.contains(graph)) {
            visit(graph);
        }
    }

    // A group is written where its first function was found; its functions keep that order.
    std::unordered_map<const GCGraph *, size_t> position;
    for (size_t i = 0; i < functions.size(); ++i) {
        position[functions[i]] = i;
    }
    std::vector<std::vector<GCGraph *>> groups;
    for (auto &component : components) {
        std::sort(component.begin(), component.end(), [&](GCGraph *a, GCGraph *b) {
            return position[a] < position[b];
        });
        // Functions with different result types cannot share one loop: they stay apart (and
        // their tail calls of each other are ordinary calls).
        const bool sameResult =
            std::all_of(component.begin(), component.end(), [&](GCGraph *g) {
                return result[g] == result[component.front()];
            });
        if (component.size() > 1 && sameResult) {
            groups.push_back(component);
        } else {
            for (GCGraph *g : component) {
                groups.push_back({g});
            }
        }
    }
    std::sort(groups.begin(), groups.end(), [&](const auto &a, const auto &b) {
        return position[a.front()] < position[b.front()];
    });
    return groups;
}

std::string ProgramEmitter::entryFunction(GCGraph *root, const Entry &entry) {
    std::string call = std::format("{}(0", parts_.functionNames.at(entry.main));
    for (gc_node_ref_t arg : entry.args) {
        const auto *node = root->node(arg);
        std::optional<std::string> value;
        if (node->kind == GCNodeKind::Data) {
            value = cppConstant(
                root->staticArea()->get<slot_t>(-node->dataIndex),
                node->dataType,
                parts_.strings);
        }
        if (!value) {
            throw Refusal{
                root,
                arg,
                "the entry function uses a value of the top level of the module that is "
                "computed, and std::cpp translates only literal top-level values yet (compute "
                "the value inside a function instead)",
                false};
        }
        call += ", " + *value;
    }
    call += ")";

    // Only a `main` that declares its return type gives the program a result (design
    // principles 2.10): an integer or a bool is the exit code, any other value is printed.
    const Type *type    = entry.main->funcType()->exitType();
    const bool declared = context_->entryResultDeclared();
    std::string body;
    if (!declared || type == nullptr || isVoid(type)) {
        body = std::format("    {};\n    return 0;\n", call);
    } else if (type->code() == TypeCode::Int32 || type->code() == TypeCode::Int64) {
        body = std::format("    return static_cast<int>({});\n", call);
    } else if (type->code() == TypeCode::Bool) {
        body = std::format("    return {} ? 1 : 0;\n", call);
    } else {
        body = std::format("    rt::println({});\n    return 0;\n", call);
    }
    return "/// Runs `main` and gives the exit code of the program.\nint entry() {\n" + body + "}\n";
}

std::string ProgramEmitter::inspectComment(GCGraph *graph) const {
    std::string out = std::format("// graph '{}':\n", graph->name());
    for (gc_node_ref_t ref : camel::execute::buildReachableExecutionTopoIndices(graph)) {
        const auto *node = graph->node(ref);
        out += std::format(
            "//   [{}] {} : {}\n",
            ref,
            kindName(node->kind),
            node->dataType ? node->dataType->toString() : "-");
    }
    return out;
}

std::string ProgramEmitter::emit(GCGraph *root) {
    const bool program = mode_ == CppEmitMode::Plain || mode_ == CppEmitMode::Bench;
    for (const char *reserved : {"entry", "rt", "std", "main", "depth"}) {
        globals_.reserve(reserved);
    }

    std::vector<GCGraph *> roots;
    Entry entry;
    if (program) {
        entry = findEntry(root);
        roots.push_back(entry.main);
    } else {
        for (GCGraph *graph : root->subGraphs()) {
            if (graph != nullptr && !isAnonymous(graph->name())) {
                roots.push_back(graph);
            }
        }
        std::sort(roots.begin(), roots.end(), [](const GCGraph *a, const GCGraph *b) {
            return a->name() < b->name();
        });
    }
    const std::vector<GCGraph *> functions = collectFunctions(roots);

    std::vector<std::string> declarations, definitions;
    for (const std::vector<GCGraph *> &group : tailCallGroups(functions)) {
        std::string name;
        if (group.size() > 1) {
            for (GCGraph *graph : group) {
                name += parts_.functionNames.at(graph) + "_";
            }
            name = globals_.unique(name + "loop");
        }
        FunctionEmitter emitter(parts_, group, name);
        std::string text = emitter.definition();
        if (mode_ == CppEmitMode::Inspect) {
            for (auto it = group.rbegin(); it != group.rend(); ++it) {
                text = inspectComment(*it) + text;
            }
        }
        for (std::string &d : emitter.declarations()) {
            declarations.push_back(std::move(d));
        }
        definitions.push_back(std::move(text));
    }
    const std::string entryText = program ? entryFunction(root, entry) : std::string();

    // The file.
    std::string source;
    std::string stem = "program";
    std::string ns   = "program";
    if (auto module = context_->mainModule()) {
        source = module->path();
        stem   = std::filesystem::path(source).stem().string();
        ns     = cppIdentifier(stem);
    }
    const size_t limit = camel::execute::recursionLimit(*context_).frames;

    const bool bridged = !parts_.bridgedOperators.empty();
    std::string out;
    out += std::format("// Generated by `camel {} std::cpp`.\n", source.empty() ? "<file>" : source);
    if (!bridged) {
        out += "// A standalone program: it needs a C++20 compiler and the headers of Camel, not\n";
        out += "// the runtime. Build it with\n";
        out += std::format(
            "//   c++ -std=c++20 -O2 -I\"$CAMEL_HOME/include\" {}.cpp -o {}\n\n",
            stem,
            stem);
    } else {
        // Sorted, so that the text does not depend on the order of a hash table.
        std::vector<std::string> uris;
        for (const auto &[uri, name] : parts_.bridgedOperators) {
            uris.push_back(uri);
        }
        std::sort(uris.begin(), uris.end());
        std::string list;
        for (const auto &uri : uris) {
            list += (list.empty() ? "" : ", ") + uri;
        }
        out += "// The program uses operators of runtime modules that std::cpp has no standalone\n";
        out += std::format("// translation for ({}), so it links the Camel runtime:\n", list);
        out += std::format(
            "//   c++ -std=c++20 -O2 -I\"$CAMEL_HOME/include\" {}.cpp -o {} \\n"
            "//       -L\"$CAMEL_HOME/libs\" -lcamel -Wl,-rpath,\"$CAMEL_HOME/libs\"\n"
            "// and it finds the modules through CAMEL_HOME when it runs.\n\n",
            stem,
            stem);
    }
    out += "#include \"camel/standalone/runtime.h\"\n";
    out += bridged ? "#include \"camel/standalone/bridge.h\"\n\n" : "\n";
    out += std::format("namespace {} {{\n\nnamespace rt = camel::standalone;\n\n", ns);
    if (!parts_.strings.empty()) {
        out += "// ---- String constants ----\n\n" + parts_.strings.definitions() + "\n";
    }
    if (bridged) {
        std::vector<std::pair<std::string, std::string>> operators(
            parts_.bridgedOperators.begin(),
            parts_.bridgedOperators.end());
        std::sort(operators.begin(), operators.end());
        out += "// ---- Operators of runtime modules ----\n\n";
        for (const auto &[uri, name] : operators) {
            out += std::format("const rt::bridge::Operator {}{{{}}};\n", name, cppStringLiteral(uri));
        }
        out += "\n";
    }
    if (!parts_.sites.empty()) {
        out += "// ---- Places where a fault can happen, and what a fault there reports ----\n\n";
        out += parts_.sites.definitions() + "\n";
    }
    out += "// ---- Functions ----\n\n";
    for (const std::string &d : declarations) {
        out += d + "\n";
    }
    for (const std::string &d : definitions) {
        out += "\n" + d;
    }
    if (program) {
        out += "\n" + entryText;
    }
    out += std::format("\n}} // namespace {}\n", ns);
    if (program) {
        out += std::format(
            "\nint main() {{ return camel::standalone::run({}, {}::entry); }}\n",
            limit,
            ns);
    }
    return out;
}

} // namespace camel::passes::cpp
