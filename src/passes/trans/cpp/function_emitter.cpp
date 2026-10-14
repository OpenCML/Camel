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

#include "function_emitter.h"

#include "camel/core/type/composite/func.h"
#include "camel/execute/graph_runtime_support.h"

#include <algorithm>
#include <format>

namespace camel::passes::cpp {

using camel::core::error::RuntimeDiag;
using camel::runtime::GCBrchBody;
using camel::runtime::GCNodeKind;
using camel::runtime::GCOperBody;
using camel::runtime::kInvalidNodeRef;

namespace {

bool isAnonymousGraphName(const std::string &name) {
    return name.size() >= 4 && name.starts_with("__") && name.ends_with("__");
}

std::string typeName(const Type *type) { return type ? type->toString() : "an unknown type"; }

/// The value a graph gives: its output node, seen through the GATEs that only order it.
gc_node_ref_t resultOf(const GCGraph *graph) {
    gc_node_ref_t ref = graph->outputNodeRef();
    while (ref != kInvalidNodeRef) {
        const auto *node = graph->node(ref);
        if (node == nullptr || node->kind != GCNodeKind::Gate || graph->normInputsOf(ref).empty()) {
            break;
        }
        ref = graph->normInputsOf(ref).front();
    }
    return ref;
}

/// `text` without parentheses that enclose all of it: "(a + b)" gives "a + b", "(a) + (b)"
/// stays as it is.
std::string withoutParentheses(const std::string &text) {
    if (text.size() < 2 || text.front() != '(' || text.back() != ')') {
        return text;
    }
    int depth = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        depth += text[i] == '(' ? 1 : text[i] == ')' ? -1 : 0;
        if (depth == 0 && i + 1 < text.size()) {
            return text;
        }
    }
    return text.substr(1, text.size() - 2);
}

/// `ref` seen through the GATEs that only order it.
gc_node_ref_t resultThrough(const GCGraph *graph, gc_node_ref_t ref) {
    while (ref != kInvalidNodeRef) {
        const auto *node = graph->node(ref);
        if (node == nullptr || node->kind != GCNodeKind::Gate || graph->normInputsOf(ref).empty()) {
            break;
        }
        ref = graph->normInputsOf(ref).front();
    }
    return ref;
}

/// The last part of a URI as a name hint: ":op/sub_l" gives "sub".
std::string operatorHint(std::string_view uri) {
    std::string_view tail = uri.substr(uri.find_last_of(":/") + 1);
    if (const size_t cut = tail.rfind('_'); cut != std::string_view::npos && cut + 2 >= tail.size()) {
        tail = tail.substr(0, cut); // add_l -> add
    }
    return std::string(tail);
}

} // namespace

std::string describeFunction(const GCGraph *graph) {
    if (graph == nullptr || graph->name().empty() || isAnonymousGraphName(graph->name())) {
        return "an anonymous function";
    }
    return std::format("function '{}'", graph->name().substr(0, graph->name().find('$')));
}

bool isArmCall(const GCGraph *graph, gc_node_ref_t ref) {
    const auto *node = graph->node(ref);
    if (node == nullptr || node->kind != GCNodeKind::Func) {
        return false;
    }
    const GCGraph *callee = graph->directCalleeGraphOf(ref);
    if (callee == nullptr || !isAnonymousGraphName(callee->name())) {
        return false;
    }
    for (gc_node_ref_t input : graph->ctrlInputsOf(ref)) {
        if (const auto *in = graph->node(input); in != nullptr && in->kind == GCNodeKind::Brch) {
            return true;
        }
    }
    return false;
}

std::string operatorUri(const GCGraph *graph, gc_node_ref_t ref) {
    return std::string(graph->nodeBodyAs<GCOperBody>(ref)->uri());
}

// ---- Construction and the signature ----

FunctionEmitter::FunctionEmitter(
    ProgramParts &program, std::vector<GCGraph *> members, std::string groupName)
    : program_(program), group_(std::move(groupName)) {
    for (const char *reserved : {"depth", "rt", "std", "which"}) {
        locals_.reserve(reserved);
    }
    for (const auto &[g, fname] : program_.functionNames) {
        locals_.reserve(fname);
    }
    if (!group_.empty()) {
        locals_.reserve(group_);
    }
    for (size_t i = 0; i < members.size(); ++i) {
        Member m;
        m.graph = members[i];
        m.name  = program_.functionNames.at(members[i]);
        m.index = i;
        members_.push_back(std::move(m));
    }
    for (Member &m : members_) {
        collectParameters(m);
    }
}

void FunctionEmitter::collectParameters(Member &member) {
    GCGraph *graph     = member.graph;
    const Type *result = graph->funcType()->exitType();
    if (!cppTypeOf(result)) {
        throw Refusal{
            graph,
            graph->returnNodeRef(),
            std::format(
                "{} returns a value of type '{}'; {}",
                describeFunction(graph),
                typeName(result),
                kTranslatedTypes),
            true};
    }
    member.result = *cppTypeOf(result);
    auto add      = [&](gc_node_ref_t port, const char *what) {
        const Type *type = graph->node(port)->dataType;
        if (!cppTypeOf(type) || isVoid(type)) {
            refuse(
                graph,
                port,
                std::format(
                    "{} of {} has type '{}'; {}",
                    what,
                    describeFunction(graph),
                    typeName(type),
                    kTranslatedTypes));
        }
        // In a group the parameters of every member are locals of one function: each is
        // named after its function.
        const std::string base = parameterName(graph, port);
        member.params.push_back(Parameter{
            port,
            *cppTypeOf(type),
            base,
            group_.empty() ? locals_.unique(base) : locals_.unique(member.name + "_" + base)});
    };
    // The order of the arguments of a direct call: the normal ones, then the `with` ones
    // (declared, then captures), as a FUNC node lists its inputs.
    for (gc_node_ref_t port : graph->normPorts()) {
        add(port, "a parameter");
    }
    for (gc_node_ref_t port : graph->withPorts()) {
        add(port, "a 'with' parameter");
    }
    for (gc_node_ref_t port : graph->closureNodes()) {
        add(port, "a captured value");
    }
}

std::string FunctionEmitter::parameterName(const GCGraph *graph, gc_node_ref_t port) {
    // The names are those of the declared parameters: the `with` ones, then the others; ports
    // after the declared `with` ones are captures, named by what they capture (as
    // src/passes/trans/dot/graphviz.cpp names them).
    const auto *type = graph->funcType();
    if (type == nullptr) {
        return "arg";
    }
    const auto captured    = type->closureRefs();
    const size_t withPorts = graph->withPorts().size();
    const size_t withCount = graph->closureNodes().empty() && captured.size() <= withPorts
                                 ? withPorts - captured.size()
                                 : withPorts;
    std::optional<size_t> argIndex;
    for (size_t i = 0; i < withPorts; ++i) {
        if (graph->withPorts()[i] == port) {
            if (i >= withCount) {
                return cppIdentifier(captured[i - withCount]);
            }
            argIndex = i;
        }
    }
    for (size_t i = 0; !argIndex && i < graph->normPorts().size(); ++i) {
        if (graph->normPorts()[i] == port) {
            argIndex = withCount + i;
        }
    }
    if (argIndex && *argIndex < type->argNamesCount() && !type->argNameAt(*argIndex).empty()) {
        return cppIdentifier(type->argNameAt(*argIndex));
    }
    return "arg";
}

std::string FunctionEmitter::cppType(const GCGraph *graph, gc_node_ref_t ref, const Type *type) {
    if (auto t = cppTypeOf(type)) {
        return *t;
    }
    refuse(
        graph,
        ref,
        std::format(
            "{} uses a value of type '{}'; {}",
            describeFunction(graph),
            typeName(type),
            kTranslatedTypes));
}

std::string FunctionEmitter::signatureOf(const Member &m) const {
    std::string out = std::format("{} {}([[maybe_unused]] rt::Depth depth", m.result, m.name);
    for (const Parameter &p : m.params) {
        out += std::format(", {} {}", p.type, group_.empty() ? p.local : p.name);
    }
    return out + ")";
}

std::vector<std::string> FunctionEmitter::declarations() const {
    std::vector<std::string> out;
    for (const Member &m : members_) {
        out.push_back(signatureOf(m) + ";");
    }
    if (!group_.empty()) {
        out.push_back(groupSignature() + ";");
    }
    return out;
}

std::string FunctionEmitter::groupSignature() const {
    std::string out = std::format(
        "{} {}([[maybe_unused]] rt::Depth depth, int which",
        members_.front().result,
        group_);
    for (const Member &m : members_) {
        for (const Parameter &p : m.params) {
            out += std::format(", {} {}", p.type, p.local);
        }
    }
    return out + ")";
}

[[noreturn]] void
FunctionEmitter::refuse(const GCGraph *graph, gc_node_ref_t ref, std::string reason) const {
    throw Refusal{graph, ref, std::move(reason), false};
}

// ---- The body ----

Block FunctionEmitter::lowerMember(const Member &member) {
    current_ = &member;
    Frame top{member.graph, {}, {}, {}};
    for (const Parameter &p : member.params) {
        top.values[p.port] = CppValue{p.local, false};
    }
    Block body = lowerSequence(
        top,
        camel::execute::buildReachableExecutionTopoIndices(member.graph),
        resultOf(member.graph),
        Tail{Tail::Kind::Return, {}});
    current_ = nullptr;
    return body;
}

std::string FunctionEmitter::definition() {
    std::string out;
    if (group_.empty()) {
        const Member &m = members_.front();
        Block body      = lowerMember(m);
        out             = signatureOf(m) + " {\n";
        if (loops_) {
            // A call of the function itself in tail position jumps here (design principles
            // 2.9: a tail call does not make the stack grow).
            out += "    for (;;) {\n";
            printBlock(out, body, 2);
            out += "    }\n";
        } else {
            printBlock(out, body, 1);
        }
        return out + "}\n";
    }

    // The functions of the group call each other in tail position: one loop runs them, `which`
    // saying whose body is running, and a tail call sets the callee's parameters and `which`.
    std::string names;
    for (const Member &m : members_) {
        names += (names.empty() ? "" : ", ") + m.name;
    }
    out += std::format(
        "// {} call each other in tail position: they run in one loop, so that those calls do\n"
        "// not make the stack grow (design principles 2.9).\n",
        names);
    out += groupSignature() + " {\n    for (;;) {\n        switch (which) {\n";
    for (const Member &m : members_) {
        Block body = lowerMember(m);
        out += std::format("        case {}: {{ // {}\n", m.index, m.name);
        printBlock(out, body, 3);
        out += "        }\n";
    }
    out += "        }\n    }\n}\n";
    for (const Member &m : members_) {
        std::string args = "depth, " + std::to_string(m.index);
        for (const Member &other : members_) {
            for (const Parameter &p : other.params) {
                args += ", " + (&other == &m ? p.name : std::string("{}"));
            }
        }
        out += std::format("\n{} {{\n    return {}({});\n}}\n", signatureOf(m), group_, args);
    }
    return out;
}

size_t FunctionEmitter::liveUses(const GCGraph *graph, gc_node_ref_t ref) {
    auto &table = uses_[graph];
    if (table.empty()) {
        auto &order = orders_[graph];
        if (order.empty()) {
            order = camel::execute::buildReachableExecutionTopoIndices(const_cast<GCGraph *>(graph));
        }
        std::unordered_set<gc_node_ref_t> live(order.begin(), order.end());
        for (gc_node_ref_t n : order) {
            size_t count = 0;
            for (const auto users : {graph->normOutputsOf(n), graph->withOutputsOf(n)}) {
                for (gc_node_ref_t user : users) {
                    count += live.contains(user) ? 1 : 0;
                }
            }
            table[n] = count;
        }
        table.emplace(kInvalidNodeRef, 0); // so that an empty graph is not computed twice
    }
    auto it = table.find(ref);
    return it == table.end() ? 0 : it->second;
}

bool FunctionEmitter::nothingAfter(
    const Frame &frame, const std::vector<gc_node_ref_t> &order, size_t at) const {
    for (size_t i = at + 1; i < order.size(); ++i) {
        if (frame.consumed.contains(order[i])) {
            continue;
        }
        switch (frame.graph->node(order[i])->kind) {
        case GCNodeKind::Data:
        case GCNodeKind::Port:
        case GCNodeKind::Gate:
        case GCNodeKind::Sync:
            continue;
        default:
            return false;
        }
    }
    return true;
}

Block FunctionEmitter::lowerSequence(
    Frame &frame, const std::vector<gc_node_ref_t> &order, gc_node_ref_t result, Tail tail) {
    frame.results.insert(result);
    Block out = lowerSequenceBody(frame, order, result, tail);
    frame.results.erase(frame.results.find(result));
    return out;
}

Block FunctionEmitter::lowerSequenceBody(
    Frame &frame, const std::vector<gc_node_ref_t> &order, gc_node_ref_t result, Tail tail) {
    Block out;
    bool done = false;
    for (size_t i = 0; i < order.size() && !done; ++i) {
        const gc_node_ref_t ref = order[i];
        if (frame.consumed.contains(ref)) {
            continue;
        }
        const auto *node = frame.graph->node(ref);
        if (node->kind == GCNodeKind::Brch) {
            // The branch is the sequence's last word when its JOIN gives the result and only
            // order follows it: its arms then end with what the sequence would end with.
            const gc_node_ref_t join = frame.graph->nodeBodyAs<GCBrchBody>(ref)->join;
            const auto at            = std::find(order.begin(), order.end(), join);
            const bool joinIsTail    = join == result && tail.kind != Tail::Kind::Discard &&
                                    at != order.end() &&
                                    nothingAfter(frame, order, static_cast<size_t>(at - order.begin()));
            done = lowerBranch(frame, ref, order, out, joinIsTail, tail);
            continue;
        }
        const bool isTailResult =
            ref == result && tail.kind != Tail::Kind::Discard && nothingAfter(frame, order, i);
        done = lowerNode(frame, ref, out, isTailResult, tail);
    }
    if (done) {
        return out;
    }

    const bool hasValue = result != kInvalidNodeRef && !isVoid(frame.graph->node(result)->dataType);
    switch (tail.kind) {
    case Tail::Kind::Return:
        // A function declared to return nothing ends with `return;` whatever its last value
        // (`return println(...)` in a `void` function gives the string println gives).
        if (isVoid(current_->graph->funcType()->exitType())) {
            out.push_back(Stmt::line("return;"));
            break;
        }
        out.push_back(Stmt::line(
            hasValue ? std::format("return {};", withoutParentheses(valueOf(frame, result).text))
                     : std::string("return;")));
        break;
    case Tail::Kind::Assign:
        if (hasValue) {
            out.push_back(Stmt::line(std::format(
                "{} = {};",
                tail.variable,
                withoutParentheses(valueOf(frame, result).text))));
        }
        break;
    case Tail::Kind::Discard:
        break;
    }
    return out;
}

const CppValue &FunctionEmitter::valueOf(Frame &frame, gc_node_ref_t ref) {
    auto it = frame.values.find(ref);
    if (it == frame.values.end()) {
        refuse(
            frame.graph,
            ref,
            std::format(
                "{} uses a value std::cpp has not computed at that point (node {} of graph "
                "'{}'); this is an error of the translation",
                describeFunction(frame.graph),
                ref,
                frame.graph->name()));
    }
    return it->second;
}

void FunctionEmitter::bind(
    Frame &frame, gc_node_ref_t ref, std::string expr, bool inlinable, Block &out,
    const std::string &hint) {
    const Type *type  = frame.graph->node(ref)->dataType;
    const size_t uses = liveUses(frame.graph, ref) + frame.results.count(ref);
    if (isVoid(type) || uses == 0) {
        // Only its effect matters. A pure expression nothing uses has none: it is left out.
        if (!inlinable) {
            out.push_back(Stmt::line(expr + ";"));
        }
        frame.values[ref] = CppValue{"", false};
        return;
    }
    if (inlinable && uses == 1) {
        frame.values[ref] = CppValue{std::move(expr), false};
        return;
    }
    const std::string name = locals_.unique(std::format("v{}_{}", nextValue_++, cppIdentifier(hint)));
    out.push_back(Stmt::line(std::format(
        "const {} {} = {};",
        cppType(frame.graph, ref, type),
        name,
        withoutParentheses(expr))));
    frame.values[ref] = CppValue{name, false};
}

bool FunctionEmitter::lowerNode(
    Frame &frame, gc_node_ref_t ref, Block &out, bool isTailResult, const Tail &tail) {
    const GCGraph *g = frame.graph;
    const auto *node = g->node(ref);
    switch (node->kind) {
    case GCNodeKind::Data: {
        const Type *type = node->dataType;
        auto text =
            cppConstant(g->staticArea()->get<slot_t>(-node->dataIndex), type, program_.strings);
        if (!text) {
            // A function used as a value is reported where it is used (its constant carries
            // the position of the function's definition).
            gc_node_ref_t at = ref;
            if (type->code() == camel::core::type::TypeCode::Function) {
                for (const auto users : {g->withOutputsOf(ref), g->normOutputsOf(ref)}) {
                    if (!users.empty()) {
                        at = users.front();
                        break;
                    }
                }
            }
            refuse(
                g,
                at,
                std::format(
                    "{} uses a constant of type '{}', which has no C++ literal; {}",
                    describeFunction(g),
                    typeName(type),
                    kTranslatedTypes));
        }
        const bool numeric = type->code() == camel::core::type::TypeCode::Int64 ||
                             type->code() == camel::core::type::TypeCode::Int32;
        frame.values[ref] = CppValue{*text, numeric && text->find_first_not_of("-0123456789") ==
                                                           std::string::npos};
        return false;
    }
    case GCNodeKind::Port:
        if (!frame.values.contains(ref)) {
            refuse(g, ref, std::format("a port of {} has no value", describeFunction(g)));
        }
        return false;
    case GCNodeKind::Gate:
    case GCNodeKind::Copy: {
        // A GATE passes its value on once what it waits for has run; a COPY of a value that is
        // not an object (or of a string, which never changes) is that value.
        const auto inputs = g->normInputsOf(ref);
        if (!inputs.empty()) {
            if (node->kind == GCNodeKind::Copy) {
                cppType(g, ref, node->dataType);
            }
            frame.values[ref] = valueOf(frame, inputs.front());
        }
        return false;
    }
    case GCNodeKind::Sync:
        return false; // only order: the statements are already in it
    case GCNodeKind::Cast: {
        const gc_node_ref_t input = g->normInputsOf(ref).front();
        const std::string to      = cppType(g, ref, node->dataType);
        const std::string from    = cppType(g, input, g->node(input)->dataType);
        const CppValue &value     = valueOf(frame, input);
        frame.values[ref] =
            to == from ? value : CppValue{std::format("static_cast<{}>({})", to, value.text), false};
        return false;
    }
    case GCNodeKind::Oper:
        lowerOperator(frame, ref, out);
        return false;
    case GCNodeKind::Func: {
        bool handled = false;
        lowerCall(frame, ref, out, isTailResult, tail, handled);
        return handled;
    }
    case GCNodeKind::Brch:
        return lowerBranch(frame, ref, {}, out, false, tail);
    case GCNodeKind::Join:
        refuse(g, ref, std::format("a JOIN of {} is not reached from its BRCH", describeFunction(g)));
    case GCNodeKind::Call:
    case GCNodeKind::Fill:
    case GCNodeKind::Accs:
    case GCNodeKind::Bind:
    case GCNodeKind::Dref:
    default: {
        const char *construct =
            node->kind == GCNodeKind::Call   ? "a call of a function value"
            : node->kind == GCNodeKind::Fill ? "the construction of a struct, tuple, array or "
                                               "closure value"
            : node->kind == GCNodeKind::Accs ? "an access to an element or a field"
            : node->kind == GCNodeKind::Bind ? "a function bound to its 'with' arguments"
                                             : "a read through a reference";
        refuse(
            g,
            ref,
            std::format(
                "{} contains {}{}, which std::cpp does not translate yet",
                describeFunction(g),
                construct,
                node->dataType ? std::format(" (of type '{}')", typeName(node->dataType))
                               : std::string()));
    }
    }
}

void FunctionEmitter::lowerCall(
    Frame &frame, gc_node_ref_t ref, Block &out, bool isTailResult, const Tail &tail,
    bool &handled) {
    const GCGraph *g = frame.graph;
    GCGraph *callee  = g->directCalleeGraphOf(ref);
    if (callee == nullptr) {
        refuse(g, ref, std::format("a call in {} has no callee", describeFunction(g)));
    }
    if (!callee->closureNodes().empty()) {
        refuse(
            g,
            ref,
            std::format(
                "{} calls {}, which captures variables of the function around it; std::cpp "
                "does not translate closures yet",
                describeFunction(g),
                describeFunction(callee)));
    }
    auto name = program_.functionNames.find(callee);
    if (name == program_.functionNames.end()) {
        refuse(
            g,
            ref,
            std::format(
                "{} calls {}, which std::cpp did not translate",
                describeFunction(g),
                describeFunction(callee)));
    }
    std::vector<std::string> args;
    for (const auto inputs : {g->normInputsOf(ref), g->withInputsOf(ref)}) {
        for (gc_node_ref_t input : inputs) {
            args.push_back(valueOf(frame, input).text);
        }
    }

    const Member *target = nullptr;
    for (const Member &m : members_) {
        target = m.graph == callee ? &m : target;
    }
    if (isTailResult && tail.kind == Tail::Kind::Return && target != nullptr) {
        // A tail call within this function (or its group): the arguments become the callee's
        // parameters and its body starts again. Each new value is computed before any
        // parameter changes.
        std::vector<std::pair<std::string, std::string>> changes; // parameter, new value
        std::vector<std::string> types;
        for (size_t i = 0; i < target->params.size() && i < args.size(); ++i) {
            const Parameter &p = target->params[i];
            if (args[i] != p.local) {
                changes.emplace_back(p.local, args[i]);
                types.push_back(p.type);
            }
        }
        if (changes.size() == 1) {
            out.push_back(Stmt::line(std::format("{} = {};", changes[0].first, changes[0].second)));
        } else if (!changes.empty()) {
            Block assign;
            for (size_t i = 0; i < changes.size(); ++i) {
                assign.push_back(Stmt::line(std::format(
                    "{} next_{} = {};", types[i], changes[i].first, changes[i].second)));
            }
            for (const auto &[param, value] : changes) {
                assign.push_back(Stmt::line(std::format("{} = std::move(next_{});", param, param)));
            }
            out.push_back(Stmt::scope(std::move(assign)));
        }
        if (!group_.empty() && target != current_) {
            out.push_back(Stmt::line(std::format("which = {};", target->index)));
        }
        out.push_back(Stmt::line("continue;"));
        loops_  = true;
        handled = true;
        return;
    }
    if (isTailResult && tail.kind == Tail::Kind::Return) {
        tailCallees_.insert(callee);
    }

    // The count of open calls goes one up, checked against the limit at this call; the
    // report names the graph the call is written in (camel/execute/limits.h).
    const std::string site = program_.sites.siteFor(
        g,
        ref,
        RuntimeDiag::MaxRecursionDepthExceeded,
        {g->name(), SiteTable::valueMark(0)});
    std::string expr = std::format("{}(rt::call(depth, {})", name->second, site);
    for (const std::string &a : args) {
        expr += ", " + a;
    }
    expr += ")";
    bind(frame, ref, std::move(expr), false, out, callee->name().substr(0, callee->name().find('$')));
}

namespace {

/// An OPER node of a frame, as the operator rules see it.
class NodeCall final : public OperatorCall {
  public:
    NodeCall(
        std::string uri, std::vector<CppValue> norm, std::vector<CppValue> with,
        std::vector<const Type *> normTypes, std::vector<const Type *> withTypes,
        const Type *result, std::function<std::string(RuntimeDiag, const std::vector<std::string> &)> site)
        : uri_(std::move(uri)), norm_(std::move(norm)), with_(std::move(with)),
          normTypes_(std::move(normTypes)), withTypes_(std::move(withTypes)), result_(result),
          site_(std::move(site)) {}

    std::string_view uri() const override { return uri_; }
    size_t normCount() const override { return norm_.size(); }
    size_t withCount() const override { return with_.size(); }
    const CppValue &norm(size_t i) const override { return norm_[i]; }
    const CppValue &with(size_t i) const override { return with_[i]; }
    const Type *normType(size_t i) const override { return normTypes_[i]; }
    const Type *withType(size_t i) const override { return withTypes_[i]; }
    const Type *resultType() const override { return result_; }
    std::string site(RuntimeDiag kind, const std::vector<std::string> &args) override {
        return site_(kind, args);
    }

  private:
    std::string uri_;
    std::vector<CppValue> norm_, with_;
    std::vector<const Type *> normTypes_, withTypes_;
    const Type *result_;
    std::function<std::string(RuntimeDiag, const std::vector<std::string> &)> site_;
};

} // namespace

void FunctionEmitter::lowerOperator(Frame &frame, gc_node_ref_t ref, Block &out) {
    const GCGraph *g      = frame.graph;
    const auto *node      = g->node(ref);
    const std::string uri = operatorUri(g, ref);
    if (!cppTypeOf(node->dataType)) {
        refuse(
            g,
            ref,
            std::format(
                "the result of operator '{}' in {} has type '{}'; {}",
                uri,
                describeFunction(g),
                typeName(node->dataType),
                kTranslatedTypes));
    }
    std::vector<CppValue> norm, with;
    std::vector<const Type *> normTypes, withTypes;
    auto collect = [&](std::span<const gc_node_ref_t> inputs, std::vector<CppValue> &values,
                       std::vector<const Type *> &types) {
        for (gc_node_ref_t input : inputs) {
            const Type *type = g->node(input)->dataType;
            if (!cppTypeOf(type)) {
                refuse(
                    g,
                    ref,
                    std::format(
                        "an argument of operator '{}' in {} has type '{}'; {}",
                        uri,
                        describeFunction(g),
                        typeName(type),
                        kTranslatedTypes));
            }
            values.push_back(valueOf(frame, input));
            types.push_back(type);
        }
    };
    collect(g->normInputsOf(ref), norm, normTypes);
    collect(g->withInputsOf(ref), with, withTypes);

    const OperatorRule *rule = findOperatorRule(uri);
    if (rule == nullptr) {
        // No standalone translation: the operator of the runtime module runs through the
        // bridge (camel/standalone/bridge.h), and the program links libcamel.
        auto [it, added] = program_.bridgedOperators.try_emplace(uri, std::string());
        if (added) {
            it->second = std::format("op_{}", cppIdentifier(uri));
        }
        auto values = [&](const std::vector<CppValue> &args, const std::vector<const Type *> &types) {
            std::string out;
            for (size_t i = 0; i < args.size(); ++i) {
                const bool wide = args[i].literal && types[i]->code() == camel::core::type::TypeCode::Int64;
                out += std::format(
                    "{}rt::bridge::arg({})",
                    i ? ", " : "",
                    wide ? std::format("int64_t{{{}}}", args[i].text) : args[i].text);
            }
            return "{" + out + "}";
        };
        std::string expr = std::format(
            "{}.call<{}>({}, {}, {})",
            it->second,
            *cppTypeOf(node->dataType),
            program_.sites.placeFor(g, ref),
            values(norm, normTypes),
            values(with, withTypes));
        bind(frame, ref, std::move(expr), false, out, operatorHint(uri));
        return;
    }
    NodeCall call(
        uri,
        std::move(norm),
        std::move(with),
        std::move(normTypes),
        std::move(withTypes),
        node->dataType,
        [&](RuntimeDiag kind, const std::vector<std::string> &args) {
            return program_.sites.siteFor(g, ref, kind, args);
        });
    auto expr = rule->emit(call);
    if (!expr) {
        refuse(
            g,
            ref,
            std::format(
                "{} uses the operator '{}' on values std::cpp does not translate it for yet",
                describeFunction(g),
                uri));
    }
    bind(frame, ref, std::move(*expr), rule->inlinable, out, operatorHint(uri));
}

bool FunctionEmitter::lowerBranch(
    Frame &frame, gc_node_ref_t brch, const std::vector<gc_node_ref_t> &order, Block &out,
    bool joinIsTail, const Tail &tail) {
    GCGraph *g                 = frame.graph;
    const gc_node_ref_t join   = g->nodeBodyAs<GCBrchBody>(brch)->join;
    const auto *joinNode       = join != kInvalidNodeRef ? g->node(join) : nullptr;
    const auto arms            = g->branchArmsOf(brch);
    const auto cases           = g->withInputsOf(brch);
    const auto selectorInputs  = g->normInputsOf(brch);
    if (joinNode == nullptr || joinNode->kind != GCNodeKind::Join || selectorInputs.empty() ||
        arms.size() < 2) {
        refuse(g, brch, std::format("a branch of {} is malformed", describeFunction(g)));
    }
    const std::string selector = valueOf(frame, selectorInputs.front()).text;

    // Where the arms put the value of the branch.
    Tail armTail{Tail::Kind::Discard, {}};
    if (joinIsTail) {
        armTail = tail;
    } else if (!isVoid(joinNode->dataType) && liveUses(g, join) > 0) {
        const std::string type = cppType(g, join, joinNode->dataType);
        const std::string name = locals_.unique(std::format("v{}_branch", nextValue_++));
        out.push_back(Stmt::line(std::format("{} {};", type, name)));
        armTail           = Tail{Tail::Kind::Assign, name};
        frame.values[join] = CppValue{name, false};
    } else {
        frame.values[join] = CppValue{"", false};
    }

    // The form std::inline leaves: an arm's nodes lie between the BRCH and the JOIN in the
    // execution order, each arm as one piece starting at its entry (the node std::fvm jumps
    // to, resolveRuntimeBranchArmEntry) and running to the next arm's entry or the JOIN.
    const auto brchAt = std::find(order.begin(), order.end(), brch);
    const auto joinAt = std::find(order.begin(), order.end(), join);
    std::vector<size_t> entries(arms.size(), 0);
    if (brchAt != order.end() && joinAt != order.end()) {
        const size_t joinPos = static_cast<size_t>(joinAt - order.begin());
        for (size_t k = 0; k < arms.size(); ++k) {
            const gc_node_ref_t entry =
                camel::execute::resolveRuntimeBranchArmEntry(g, brch, k, order);
            const auto at = std::find(brchAt, joinAt, entry);
            entries[k]    = at == joinAt ? joinPos : static_cast<size_t>(at - order.begin());
        }
        // What lies before the first entry holds no statement (constants, ports, gates): it
        // gives values to the arms and is lowered here, before the branch.
        const size_t first = *std::min_element(entries.begin(), entries.end());
        for (size_t i = static_cast<size_t>(brchAt - order.begin()) + 1; i < first; ++i) {
            if (!frame.consumed.contains(order[i])) {
                lowerNode(frame, order[i], out, false, Tail{});
                frame.consumed.insert(order[i]);
            }
        }
    }
    auto regionOf = [&](size_t k) {
        std::vector<gc_node_ref_t> nodes;
        if (brchAt == order.end() || joinAt == order.end()) {
            return nodes;
        }
        size_t end = static_cast<size_t>(joinAt - order.begin());
        for (size_t e : entries) {
            if (e > entries[k] && e < end) {
                end = e;
            }
        }
        for (size_t i = entries[k]; i < end; ++i) {
            nodes.push_back(order[i]);
        }
        return nodes;
    };

    Stmt branch;
    branch.kind = Stmt::Kind::Branch;
    for (size_t k = 0; k < arms.size(); ++k) {
        Block body;
        if (arms[k].head == arms[k].tail && isArmCall(g, arms[k].head)) {
            // The front end's form: the arm is a graph of its own, lowered here.
            body = lowerArmGraph(frame, arms[k].head, armTail);
            frame.consumed.insert(arms[k].head);
        } else {
            const std::vector<gc_node_ref_t> region = regionOf(k);
            body = lowerSequence(frame, region, resultThrough(g, arms[k].tail), armTail);
            for (gc_node_ref_t n : region) {
                frame.consumed.insert(n);
            }
        }
        std::string condition;
        if (k + 1 < arms.size()) {
            if (cases.empty()) {
                // `if`: the first arm when the condition holds; a branch on a number picks the
                // arm of that index.
                condition = arms.size() == 2 ? withoutParentheses(selector)
                                             : std::format("{} == {}", selector, k);
            } else {
                condition = std::format("{} == {}", selector, valueOf(frame, cases[k]).text);
            }
        }
        branch.arms.emplace_back(std::move(condition), std::move(body));
    }
    frame.consumed.insert(join);
    out.push_back(std::move(branch));
    return joinIsTail;
}

Block FunctionEmitter::lowerArmGraph(Frame &frame, gc_node_ref_t call, Tail tail) {
    const GCGraph *g = frame.graph;
    GCGraph *arm     = g->directCalleeGraphOf(call);
    if (!arm->closureNodes().empty()) {
        refuse(g, call, std::format("an arm of a branch in {} captures a closure", describeFunction(g)));
    }
    Frame inner{arm, {}, {}};
    Block prefix;
    // The ports take the values the call passes. A value written as an expression that the
    // arm uses more than once is computed once, at the start of the arm.
    auto bindPorts = [&](std::span<const gc_node_ref_t> ports, std::span<const gc_node_ref_t> args) {
        for (size_t i = 0; i < ports.size() && i < args.size(); ++i) {
            CppValue value = valueOf(frame, args[i]);
            const bool simple =
                value.literal || value.text.find_first_of("()+-*/<>=!&|~ ,") == std::string::npos;
            if (!simple && liveUses(arm, ports[i]) > 1) {
                const std::string name =
                    locals_.unique(std::format("v{}_{}", nextValue_++, parameterName(arm, ports[i])));
                prefix.push_back(Stmt::line(std::format(
                    "const {} {} = {};",
                    cppType(arm, ports[i], arm->node(ports[i])->dataType),
                    name,
                    value.text)));
                value = CppValue{name, false};
            }
            inner.values[ports[i]] = value;
        }
    };
    bindPorts(arm->normPorts(), g->normInputsOf(call));
    bindPorts(arm->withPorts(), g->withInputsOf(call));

    Block body = lowerSequence(
        inner,
        camel::execute::buildReachableExecutionTopoIndices(arm),
        resultOf(arm),
        tail);
    prefix.insert(prefix.end(), std::make_move_iterator(body.begin()), std::make_move_iterator(body.end()));
    return prefix;
}

} // namespace camel::passes::cpp
