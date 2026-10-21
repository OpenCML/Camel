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
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "program_emitter.h"

#include "camel/core/module/module.h"
#include "camel/core/rtdata/func.h"
#include "camel/execute/entry_args.h"
#include "camel/execute/graph_runtime_support.h"
#include "camel/execute/limits.h"
#include "camel/execute/pass/warnings.h"
#include "camel/utils/path.h"

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
    if (node == nullptr || node->kind != GCNodeKind::Data || node->dataType() == nullptr ||
        node->dataType()->code() != TypeCode::Function) {
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
    case GCNodeKind::Loop:
        return "LOOP";
    }
    return "?";
}

} // namespace

ProgramEmitter::Entry ProgramEmitter::findEntry(GCGraph *root) const {
    // The root graph ends with the call of `main`: through a closure (FILL) when main captures
    // values of the top level, else through the function constant itself.
    Entry entry;
    // The call is the root's result; when main gives nothing, only its completion point.
    gc_node_ref_t call = root->returnNodeRef();
    for (gc_node_ref_t candidate :
         {root->returnNodeRef(), root->exitNodeRef(), root->outputNodeRef()}) {
        const auto *node = candidate != kInvalidNodeRef ? root->node(candidate) : nullptr;
        if (node != nullptr && node->kind == GCNodeKind::Call) {
            call = candidate;
            break;
        }
    }
    const auto *callNode = call != kInvalidNodeRef ? root->node(call) : nullptr;
    if (callNode == nullptr || callNode->kind != GCNodeKind::Call ||
        root->withInputsOf(call).empty()) {
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
    entry.call = call;
    return entry;
}

std::vector<GCGraph *> ProgramEmitter::collectFunctions(const std::vector<GCGraph *> &roots) {
    std::vector<GCGraph *> functions;
    std::unordered_set<const GCGraph *> seen;
    std::vector<GCGraph *> work(roots.rbegin(), roots.rend());

    // The graphs a graph calls directly or uses as values, looking through the arms lowered
    // in place.
    std::unordered_set<const GCGraph *> scanning;
    std::function<void(GCGraph *)> scan = [&](GCGraph *graph) {
        for (gc_node_ref_t ref : camel::execute::buildReachableExecutionTopoIndices(graph)) {
            const auto *node = graph->node(ref);
            if (node->kind == GCNodeKind::Data) {
                if (GCGraph *function = functionConstant(graph, ref);
                    function != nullptr && !seen.contains(function)) {
                    work.push_back(function);
                }
                continue;
            }
            if (node->kind != GCNodeKind::Func) {
                continue;
            }
            GCGraph *callee = parts_.program->calleeOf(*graph, ref);
            if (callee == nullptr) {
                continue;
            }
            // An arm is lowered in place, unless it is already being lowered further out (a
            // rewrite can leave an arm graph that reaches itself): then it is a function.
            if (isArmCall(*parts_.program, graph, ref) && !scanning.contains(callee)) {
                scanning.insert(callee);
                scan(callee);
                scanning.erase(callee);
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
        scanning = {graph};
        if (!parts_.functionNames.contains(graph)) {
            const std::string &name = graph->name();
            std::string base        = isAnonymous(name) ? "lambda" : name.substr(0, name.find('$'));
            parts_.functionNames[graph] = globals_.unique(cppIdentifier(base));
        }
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
        result[graph] = parts_.values.type(graph->funcType()->exitType()).value_or("");
    }

    // The strongly connected components of "calls in tail position" (Tarjan).
    std::unordered_map<const GCGraph *, size_t> index, low;
    std::unordered_set<const GCGraph *> onStack;
    std::vector<GCGraph *> stack;
    std::vector<std::vector<GCGraph *>> components;
    size_t next                          = 0;
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
        const bool sameResult = std::all_of(component.begin(), component.end(), [&](GCGraph *g) {
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
    // The parameters of `main` still open (no --arg bound them when the program was
    // translated) are the ports of the root graph: the program reads them from its command
    // line (design principles 10.6; camel/standalone/entry.h), in the order `camel` names them
    // (unboundEntryParameters: the `with` ones, then the others).
    const std::vector<std::string> names = camel::execute::unboundEntryParameters(root);
    const size_t withCount               = root->withPorts().size();
    const auto *rootType                 = root->funcType();
    std::unordered_map<gc_node_ref_t, std::string> parameterValue;
    std::string parameters;
    for (size_t i = 0; i < names.size(); ++i) {
        const bool isWith        = i < withCount;
        const size_t k           = isWith ? i : i - withCount;
        const gc_node_ref_t port = isWith ? root->withPorts()[k] : root->normPorts()[k];
        const bool declared =
            rootType != nullptr &&
            (isWith ? k < rootType->withTypesCount() : k < rootType->normTypesCount());
        const Type *type = declared ? (isWith ? rootType->withTypeAt(k) : rootType->normTypeAt(k))
                                    : root->node(port)->dataType();
        if (declared && (isWith ? rootType->withIsVarAt(k) : rootType->normIsVarAt(k))) {
            throw Refusal{
                root,
                port,
                std::format(
                    "the parameter '{}' of 'main' is a 'var' parameter, which the command line "
                    "cannot give",
                    names[i]),
                false};
        }
        const char *kind = nullptr;
        std::string value;
        switch (type != nullptr ? type->code() : TypeCode::Void) {
        case TypeCode::Int64:
            kind  = "Int";
            value = std::format("args.integer({})", i);
            break;
        case TypeCode::Int32:
            kind  = "Int";
            value = std::format("static_cast<int32_t>(args.integer({}))", i);
            break;
        case TypeCode::Float64:
            kind  = "Float";
            value = std::format("args.number({})", i);
            break;
        case TypeCode::Float32:
            kind  = "Float";
            value = std::format("static_cast<float>(args.number({}))", i);
            break;
        case TypeCode::Complex64:
            kind  = "ComplexFloat";
            value = std::format("args.complexFloat({})", i);
            break;
        case TypeCode::Complex128:
            kind  = "ComplexDouble";
            value = std::format("args.complexDouble({})", i);
            break;
        case TypeCode::Bool:
            kind  = "Bool";
            value = std::format("args.boolean({})", i);
            break;
        case TypeCode::String:
            kind  = "Text";
            value = std::format("args.text({})", i);
            break;
        default:
            throw Refusal{
                root,
                port,
                std::format(
                    "the parameter '{}' of 'main' has type '{}', and std::cpp reads from the "
                    "command line only the parameters of type int, i32, float, f32, c64, c128, "
                    "bool and string (a tensor or a struct is read from files by camel only)",
                    names[i],
                    type != nullptr ? type->toString() : std::string("void")),
                false};
        }
        parameters += std::format(
            "{}{{{}, rt::EntryKind::{}}}",
            parameters.empty() ? "" : ", ",
            cppStringLiteral(names[i]),
            kind);
        parameterValue[port] = std::move(value);
    }
    // The reports of a wrong or missing argument, at the entry function, as camel gives them.
    const uint64_t at  = camel::execute::entryFunctionOrigin(*context_);
    const std::string invalid = parts_.sites.siteAt(
        at,
        camel::core::error::RuntimeDiag::EntryArgumentInvalid,
        {SiteTable::valueMark(0), SiteTable::valueMark(1)});
    const std::string missing = parts_.sites.siteAt(
        at,
        camel::core::error::RuntimeDiag::EntryArgumentMissing,
        {SiteTable::valueMark(0), SiteTable::valueMark(1)});

    // What an argument of the call is: a parameter of `main`, or a literal value.
    const auto argumentText = [&](gc_node_ref_t arg) {
        if (auto it = parameterValue.find(arg); it != parameterValue.end()) {
            return it->second;
        }
        const auto *node = root->node(arg);
        std::optional<std::string> value;
        if (node->kind == GCNodeKind::Data) {
            value = parts_.values.constant(
                root->staticArea()->get<slot_t>(-node->dataIndex),
                node->dataType());
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
        return *value;
    };
    std::string call;
    if (parts_.root != nullptr) {
        // The root computes values of the top level and calls main with them; it takes the
        // parameters of `main` as its own, in the order of a direct call (the normal ones,
        // then the `with` ones).
        call = std::format("{}(0", parts_.functionNames.at(root));
        for (gc_node_ref_t port : root->normPorts()) {
            call += ", " + parameterValue.at(port);
        }
        for (gc_node_ref_t port : root->withPorts()) {
            call += ", " + parameterValue.at(port);
        }
    } else {
        // The arguments of main in the order of a direct call: the normal ones, the `with`
        // ones, then what it captures.
        call = std::format("{}(0", parts_.functionNames.at(entry.main));
        const auto withInputs = root->withInputsOf(entry.call);
        for (gc_node_ref_t arg : root->normInputsOf(entry.call)) {
            call += ", " + argumentText(arg);
        }
        for (size_t i = 1; i < withInputs.size(); ++i) {
            call += ", " + argumentText(withInputs[i]);
        }
        for (gc_node_ref_t arg : entry.args) {
            call += ", " + argumentText(arg);
        }
    }
    call += ")";

    // Only a `main` that declares its return type gives the program a result (design
    // principles 9.8): an integer or a bool is the exit code, any other value is printed. A
    // declared `T!` that failed is an error nobody handled: reported on standard error, exit
    // code 1 (src/execute/entry_args.cpp, writeEntryResultTo).
    const Type *type    = entry.main->funcType()->exitType();
    const bool declared = context_->entryResultDeclared();
    std::string body;
    std::string value = call;
    if (declared && type != nullptr && type->code() == TypeCode::Tuple &&
        static_cast<const camel::core::type::TupleType *>(type)->isFallible()) {
        const std::string unhandled = parts_.sites.unlocated(
            camel::core::error::RuntimeDiag::UnhandledError,
            {SiteTable::valueMark(0), SiteTable::valueMark(1)});
        body += std::format(
            "    const auto result = {};\n    if (result.failed()) {{\n"
            "        return rt::unhandled({}, result.error());\n    }}\n",
            call,
            unhandled);
        value = "result.value()";
        type  = static_cast<const camel::core::type::TupleType *>(type)->payloadType();
    }
    if (!declared || type == nullptr || isVoid(type)) {
        body += value == call ? std::format("    {};\n    return 0;\n", call) : "    return 0;\n";
    } else if (type->code() == TypeCode::Int32 || type->code() == TypeCode::Int64) {
        body += std::format("    return static_cast<int>({});\n", value);
    } else if (type->code() == TypeCode::Bool) {
        body += std::format("    return {} ? 1 : 0;\n", value);
    } else {
        body += std::format("    rt::println({});\n    return 0;\n", value);
    }
    return std::format(
        "/// Runs `main` with the arguments of the command line and gives the exit code of the\n"
        "/// program.\n"
        "int entry(int argc, char **argv) {{\n"
        "    const rt::EntryArguments args(argc, argv, {{{}}}, {}, {});\n"
        "{}}}\n",
        parameters,
        invalid,
        missing,
        body);
}

std::string ProgramEmitter::inspectComment(GCGraph *graph) const {
    std::string out = std::format("// graph '{}':\n", graph->name());
    for (gc_node_ref_t ref : camel::execute::buildReachableExecutionTopoIndices(graph)) {
        const auto *node = graph->node(ref);
        out += std::format(
            "//   [{}] {} : {}\n",
            ref,
            kindName(node->kind),
            node->dataType() ? node->dataType()->toString() : "-");
    }
    return out;
}

std::string ProgramEmitter::emit(const camel::runtime::Program &runtimeProgram, GCGraph *root) {
    parts_.program     = &runtimeProgram;
    const bool program = mode_ == CppEmitMode::Plain;
    for (const char *reserved : {"entry", "rt", "std", "main", "depth", "lit", "program"}) {
        globals_.reserve(reserved);
    }

    std::vector<GCGraph *> roots;
    Entry entry;
    if (program) {
        entry = findEntry(root);
        // Values of the top level that are computed (`let k = f()`, design principles 10.1:
        // before main starts): the root graph is a function of the program that computes
        // them and calls main.
        const bool computed =
            std::any_of(entry.args.begin(), entry.args.end(), [&](gc_node_ref_t arg) {
                return root->node(arg)->kind != GCNodeKind::Data;
            });
        if (computed) {
            parts_.root                = root;
            parts_.entryCall           = entry.call;
            parts_.entryMain           = entry.main;
            parts_.functionNames[root] = globals_.unique("top_level");
            roots.push_back(root);
        }
        roots.push_back(entry.main);
    } else {
        for (GCGraph *graph : runtimeProgram.subGraphs(*root)) {
            if (graph != nullptr && !isAnonymous(graph->name())) {
                roots.push_back(graph);
            }
        }
        std::sort(roots.begin(), roots.end(), [](const GCGraph *a, const GCGraph *b) {
            return a->name() < b->name();
        });
    }
    const std::vector<GCGraph *> functions = collectFunctions(roots);
    for (const auto &[graph, name] : parts_.functionNames) {
        parts_.values.reserve(name);
    }

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
        stem   = camel::utils::pathToUtf8(camel::utils::pathFromUtf8(source).stem());
    }
    const size_t limit = camel::execute::recursionLimit(*context_).frames;

    const bool bridged = !parts_.bridgedOperators.empty();
    std::string out;
    out +=
        std::format("// Generated by `camel {} std::cpp`.\n", source.empty() ? "<file>" : source);
    if (!bridged) {
        out += "// A standalone program: it needs a C++20 compiler and the headers of Camel, not\n";
        out += "// the runtime. Build it with (-ffp-contract=off: the arithmetic rounds as the\n";
        out += "// interpreter's, without fused multiply-adds)\n";
        out += std::format(
            "//   c++ -std=c++20 -O2 -ffp-contract=off -I\"$CAMEL_HOME/include\" {}.cpp -o {}\n\n",
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
        // Said where the first of them is used: the program is not standalone.
        std::string named;
        for (const auto &uri : uris) {
            named += (named.empty() ? "'" : ", '") + uri + "'";
        }
        ::raisePassWarning(
            *context_,
            camel::core::error::RuntimeDiag::TranslationLinksRuntime,
            nodeOriginNear(parts_.firstBridgedGraph, parts_.firstBridgedNode),
            (uris.size() == 1 ? "the operator " : "the operators ") + named);
        out += "// The program uses operators of runtime modules that std::cpp has no standalone\n";
        out += std::format(
            "// translation for ({}), so it links the Camel runtime (one command):\n",
            list);
        // No `\` at the end of a line: it would join the next comment line to this one.
        out += std::format(
            "//   c++ -std=c++20 -O2 -ffp-contract=off -I\"$CAMEL_HOME/include\" {}.cpp -o {}\n"
            "//       -L\"$CAMEL_HOME/libs\" -lcamel -Wl,-rpath,\"$CAMEL_HOME/libs\"\n"
            "// Windows: link $CAMEL_HOME/libs/libcamel.lib and add $CAMEL_HOME/libs to PATH.\n"
            "// and it finds the modules through CAMEL_HOME when it runs.\n\n",
            stem,
            stem);
    }
    out += "#include \"camel/standalone/runtime.h\"\n";
    out += bridged ? "#include \"camel/standalone/bridge.h\"\n\n" : "\n";
    out += std::format("namespace {} {{\n\nnamespace rt = camel::standalone;\n", ns);
    // The text of a value of a struct declared here calls appendText on its members: the
    // runtime's overloads are found next to the file's own.
    out += "using camel::standalone::appendText;\n\n";
    if (const std::string types = parts_.values.typeDeclarations(); !types.empty()) {
        out += "// ---- Tuples and structs ----\n\n" + types;
    }
    if (const std::string constants = parts_.values.constantDefinitions(); !constants.empty()) {
        out += "// ---- Constants ----\n\nnamespace lit {\n" + constants + "} // namespace lit\n\n";
    }
    if (bridged) {
        std::vector<std::pair<std::string, std::string>> operators(
            parts_.bridgedOperators.begin(),
            parts_.bridgedOperators.end());
        std::sort(operators.begin(), operators.end());
        out += "// ---- Operators of runtime modules ----\n\n";
        for (const auto &[uri, name] : operators) {
            out +=
                std::format("const rt::bridge::Operator {}{{{}}};\n", name, cppStringLiteral(uri));
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
        // The command line goes to the entry, which reads the parameters of `main` from it.
        out += std::format(
            "\nint main(int argc, char **argv) {{\n"
            "    return camel::standalone::run({}, [&] {{ return {}::entry(argc, argv); }});\n"
            "}}\n",
            limit,
            ns);
    }
    return out;
}

} // namespace camel::passes::cpp
