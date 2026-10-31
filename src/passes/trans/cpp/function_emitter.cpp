// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "function_emitter.h"

#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/symbol.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/execute/graph_runtime_support.h"

#include <algorithm>
#include <cctype>
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

/// Whether every user of `ref` takes it as the template of a construction (FILL, Norm 0).
bool onlyTemplate(const GCGraph *graph, gc_node_ref_t ref) {
    if (!graph->withOutputsOf(ref).empty() || graph->normOutputsOf(ref).empty()) {
        return false;
    }
    for (gc_node_ref_t user : graph->normOutputsOf(ref)) {
        const auto *node = graph->node(user);
        if (node == nullptr || node->kind != GCNodeKind::Fill ||
            graph->normInputsOf(user).front() != ref) {
            return false;
        }
    }
    return true;
}

/// `text` with its one occurrence of the identifier `name` as `std::move(name)`; unchanged
/// when it occurs other than once.
std::string movedOnce(const std::string &text, const std::string &name) {
    auto isIdent = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    size_t found = std::string::npos, count = 0;
    for (size_t at = text.find(name); at != std::string::npos; at = text.find(name, at + 1)) {
        const bool before =
            at > 0 && (isIdent(text[at - 1]) || text[at - 1] == ':' || text[at - 1] == '.');
        const bool after = at + name.size() < text.size() && isIdent(text[at + name.size()]);
        if (!before && !after) {
            found = at;
            ++count;
        }
    }
    if (count != 1 || text == name) {
        return text == name ? "std::move(" + name + ")" : text;
    }
    return text.substr(0, found) + "std::move(" + name + ")" + text.substr(found + name.size());
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
    if (const size_t cut = tail.rfind('_');
        cut != std::string_view::npos && cut + 2 >= tail.size()) {
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

std::string operatorUri(const GCGraph *graph, gc_node_ref_t ref) {
    return std::string(graph->nodeBodyAs<GCOperBody>(ref)->uri());
}

camel::core::OperId operatorId(const GCGraph *graph, gc_node_ref_t ref) {
    return graph->nodeBodyAs<GCOperBody>(ref)->oper;
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
    if (!program_.values.type(result)) {
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
    member.result = *program_.values.type(result);
    auto add      = [&](gc_node_ref_t port, const char *what) {
        const Type *type = graph->node(port)->dataType();
        if (!program_.values.type(type) || isVoid(type)) {
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
        member.params.push_back(
            Parameter{
                port,
                *program_.values.type(type),
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
    const auto captured    = graph->captureNames();
    const size_t withPorts = graph->withPorts().size();
    const size_t withCount = graph->closureNodes().empty() && captured.size() <= withPorts
                                 ? withPorts - captured.size()
                                 : withPorts;
    std::optional<size_t> argIndex;
    for (size_t i = 0; i < withPorts; ++i) {
        if (graph->withPorts()[i] == port) {
            if (i >= withCount) {
                return cppIdentifier(camel::core::symbolText(captured[i - withCount]));
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
    // A captured value of a function value: named by what it captures.
    for (size_t i = 0; i < graph->closureNodes().size(); ++i) {
        if (graph->closureNodes()[i] == port && i < captured.size()) {
            return cppIdentifier(camel::core::symbolText(captured[i]));
        }
    }
    return "arg";
}

std::string FunctionEmitter::cppType(const GCGraph *graph, gc_node_ref_t ref, const Type *type) {
    if (auto t = program_.values.type(type)) {
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
        // A `var` parameter or a captured variable is the variable's cell.
        const bool cell = p.type.starts_with("rt::Cell<");
        top.values[p.port] =
            CppValue{p.local, false, cell ? CppValue::Cell::Shared : CppValue::Cell::None};
    }
    Block body = lowerSequence(
        top,
        member.graph->execPlan().topo,
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
            // 3.9: a tail call does not make the stack grow).
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
        "// not make the stack grow (design principles 3.9).\n",
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
            order =
                graph->execPlan().topo;
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

const camel::runtime::ControlRegions &FunctionEmitter::controlRegionsOf(const GCGraph *graph) {
    auto &held = regions_[graph];
    if (!held) {
        held = std::make_unique<camel::runtime::ControlRegions>(*graph);
    }
    return *held;
}

Block FunctionEmitter::lowerSequenceBody(
    Frame &frame, const std::vector<gc_node_ref_t> &order, gc_node_ref_t result, Tail tail) {
    Block out;
    bool done = false;
    // What a loop of this sequence owns is lowered with the loop: the nodes of its regions, the
    // ports of the regions and the port that counts its rounds.
    std::unordered_set<gc_node_ref_t> ownedByLoops;
    for (gc_node_ref_t loop : order) {
        if (frame.graph->node(loop)->kind != GCNodeKind::Loop) {
            continue;
        }
        const auto &regions = controlRegionsOf(frame.graph);
        const auto parts    = regions.loopPartsOf(loop);
        for (gc_node_ref_t n : order) {
            for (uint32_t part : parts) {
                if (part != 0 && regions.within(regions.controlOf(n), part)) {
                    ownedByLoops.insert(n);
                }
            }
        }
        for (uint32_t part : parts) {
            if (part != 0) {
                for (gc_node_ref_t port : frame.graph->regionPorts(part)) {
                    ownedByLoops.insert(port);
                }
            }
        }
        ownedByLoops.insert(frame.graph->withInputsOf(loop).back());
        if (frame.graph->loopIterableFormOf(loop) == camel::core::LoopIterableForm::Interval) {
            // The pair of the interval's two bounds is the loop's sequence (design principles
            // 4.2, LoopIterableForm::Interval): it is written out with the loop, as the array of
            // the interval, not where it stands.
            ownedByLoops.insert(frame.graph->normInputsOf(loop).front());
        }
    }
    for (size_t i = 0; i < order.size(); ++i) {
        const gc_node_ref_t ref = order[i];
        if (frame.consumed.contains(ref) || ownedByLoops.contains(ref)) {
            continue;
        }
        const auto *node = frame.graph->node(ref);
        if (done) {
            // The sequence has ended (a tail call, a branch that returns), and only nodes that
            // write nothing can follow (nothingAfter). They still give values: an arm laid out
            // after another one reads the gates the order put at the end of the other. A gate
            // of the value that ended the sequence (a tail call has none) gives nothing.
            const bool forwards  = node->kind == GCNodeKind::Gate || node->kind == GCNodeKind::Copy;
            const bool valueOnly = forwards || node->kind == GCNodeKind::Data ||
                                   node->kind == GCNodeKind::Port;
            const auto inputs    = frame.graph->normInputsOf(ref);
            if (valueOnly &&
                (!forwards || (!inputs.empty() && frame.values.contains(inputs.front())))) {
                lowerNode(frame, ref, out, false, Tail{});
            }
            continue;
        }
        if (node->kind == GCNodeKind::Brch) {
            // The branch is the sequence's last word when its JOIN gives the result and only
            // order follows it: its arms then end with what the sequence would end with.
            const gc_node_ref_t join = frame.graph->nodeBodyAs<GCBrchBody>(ref)->join;
            const auto at            = std::find(order.begin(), order.end(), join);
            const bool joinIsTail =
                join == result && tail.kind != Tail::Kind::Discard && at != order.end() &&
                nothingAfter(frame, order, static_cast<size_t>(at - order.begin()));
            done = lowerBranch(frame, ref, order, out, joinIsTail, tail);
            continue;
        }
        if (node->kind == GCNodeKind::Loop) {
            lowerLoop(frame, ref, order, out);
            continue;
        }
        const bool isTailResult =
            ref == result && tail.kind != Tail::Kind::Discard && nothingAfter(frame, order, i);
        done = lowerNode(frame, ref, out, isTailResult, tail);
    }
    if (done) {
        return out;
    }

    const bool hasValue =
        result != kInvalidNodeRef && !isVoid(frame.graph->node(result)->dataType());
    switch (tail.kind) {
    case Tail::Kind::Return:
        // A function declared to return nothing ends with `return;` whatever its last value
        // (`return println(...)` in a `void` function gives the string println gives).
        if (isVoid(current_->graph->funcType()->exitType())) {
            out.push_back(Stmt::line("return;"));
            break;
        }
        out.push_back(
            Stmt::line(
                hasValue
                    ? std::format("return {};", withoutParentheses(valueOf(frame, result).text))
                    : std::string("return;")));
        break;
    case Tail::Kind::Assign:
        if (hasValue) {
            out.push_back(
                Stmt::line(
                    std::format(
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
                "'{}', of kind {}); this is an error of the translation",
                describeFunction(frame.graph),
                ref,
                frame.graph->name(),
                static_cast<int>(frame.graph->node(ref)->kind)));
    }
    return it->second;
}

void FunctionEmitter::bind(
    Frame &frame, gc_node_ref_t ref, std::string expr, bool inlinable, Block &out,
    const std::string &hint) {
    const Type *type  = frame.graph->node(ref)->dataType();
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
    const std::string name =
        locals_.unique(std::format("v{}_{}", nextValue_++, cppIdentifier(hint)));
    out.push_back(
        Stmt::line(
            std::format(
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
        const Type *type = node->dataType();
        std::optional<std::string> text;
        if (GCGraph *function = functionConstantOf(g, ref)) {
            text = functionValue(function, {});
        } else if (hasSeveralDimensions(type)) {
            // The constant of such a type is its first extent's worth of elements only, so writing
            // one would print three numbers where the interpreter prints three rows -- a program
            // that exits 0 and is wrong. The refusal says what the target does not have, because
            // that is what is missing: an array here is a flat run of elements, with no shape.
            refuse(
                g,
                ref,
                std::format(
                    "{} uses a value of type '{}'; an array in std::cpp is a flat run of elements "
                    "and carries no shape, so a value of several dimensions has no form there",
                    describeFunction(g),
                    typeName(type)));
        } else {
            text = program_.values.constant(g->staticArea()->get<slot_t>(-node->dataIndex), type);
        }
        if (!text && onlyTemplate(g, ref)) {
            // The template of constructions (FILL): they read its constant elements themselves.
            frame.values[ref] = CppValue{"", false};
            return false;
        }
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
        frame.values[ref] =
            CppValue{*text, numeric && text->find_first_not_of("-0123456789") == std::string::npos};
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
                // A copy of a value is that value: values have no identity (design principles
                // 10.5), and every holder of a shared array sees it unchanged (array.h).
                cppType(g, ref, node->dataType());
            }
            frame.values[ref] = valueOf(frame, inputs.front());
        }
        return false;
    }
    case GCNodeKind::Cast:
        lowerCast(frame, ref);
        return false;
    case GCNodeKind::Fill:
        lowerFill(frame, ref, out);
        return false;
    case GCNodeKind::Accs:
        lowerAccess(frame, ref);
        return false;
    case GCNodeKind::Call:
        lowerCallValue(frame, ref, out);
        return false;
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
        refuse(
            g,
            ref,
            std::format("a JOIN of {} is not reached from its BRCH", describeFunction(g)));
    default: {
        const char *construct = node->kind == GCNodeKind::Bind
                                    ? "a function bound to its 'with' arguments"
                                    : "a read through a reference";
        refuse(
            g,
            ref,
            std::format(
                "{} contains {}{}, which std::cpp does not translate yet",
                describeFunction(g),
                construct,
                node->dataType() ? std::format(" (of type '{}')", typeName(node->dataType()))
                                 : std::string()));
    }
    }
}

void FunctionEmitter::lowerCall(
    Frame &frame, gc_node_ref_t ref, Block &out, bool isTailResult, const Tail &tail,
    bool &handled) {
    const GCGraph *g = frame.graph;
    GCGraph *callee  = program_.program->calleeOf(*g, ref);
    if (callee == nullptr) {
        refuse(g, ref, std::format("a call in {} has no callee", describeFunction(g)));
    }
    if (camel::runtime::funcWriteMaskOf(*g, ref) != 0) {
        // The call hands on the tuple of the callee's result and the new versions of the captured
        // variables it writes, and its ACCS nodes read its parts; this emitter translates the
        // callee's result alone.
        refuse(
            g,
            ref,
            std::format(
                "{} calls {}, which writes captured variables, and std::cpp does not translate "
                "their new versions yet",
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
        // A parameter that gets a new value is dead once the new values are computed: the
        // one place they read it moves it, so an operator that gives a new array or string
        // (`acc->append<x>`, `acc + s`) finds it unshared and works in place (design note 5.2).
        // A parameter read in more than one of them, or one that keeps its value, is not moved.
        for (auto &[param, value] : changes) {
            std::string all;
            for (const auto &[other, expr] : changes) {
                all += expr + " ";
            }
            if (movedOnce(all, param) != all) {
                for (auto &[other, expr] : changes) {
                    expr = movedOnce(expr, param);
                }
            }
        }
        if (changes.size() == 1) {
            out.push_back(Stmt::line(std::format("{} = {};", changes[0].first, changes[0].second)));
        } else if (!changes.empty()) {
            Block assign;
            for (size_t i = 0; i < changes.size(); ++i) {
                assign.push_back(
                    Stmt::line(
                        std::format(
                            "{} next_{} = {};",
                            types[i],
                            changes[i].first,
                            changes[i].second)));
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

    std::string expr = std::format("{}({}", name->second, callDepth(g, ref));
    for (const std::string &a : args) {
        expr += ", " + a;
    }
    expr += ")";
    bind(
        frame,
        ref,
        std::move(expr),
        false,
        out,
        callee->name().substr(0, callee->name().find('$')));
}

namespace {

/// An OPER node of a frame, as the operator rules see it.
class NodeCall final : public OperatorCall {
  public:
    NodeCall(
        std::string uri, std::vector<CppValue> norm, std::vector<CppValue> with,
        std::vector<const Type *> normTypes, std::vector<const Type *> withTypes,
        const Type *result,
        std::function<std::string(RuntimeDiag, const std::vector<std::string> &)> site,
        std::function<std::string(const Type *)> cppType,
        std::function<std::string(size_t)> fieldName)
        : uri_(std::move(uri)), norm_(std::move(norm)), with_(std::move(with)),
          normTypes_(std::move(normTypes)), withTypes_(std::move(withTypes)), result_(result),
          site_(std::move(site)), cppType_(std::move(cppType)), fieldName_(std::move(fieldName)) {}

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
    std::string cppType(const Type *type) const override { return cppType_(type); }
    std::string fieldName(size_t argIndex) override { return fieldName_(argIndex); }

  private:
    std::string uri_;
    std::vector<CppValue> norm_, with_;
    std::vector<const Type *> normTypes_, withTypes_;
    const Type *result_;
    std::function<std::string(RuntimeDiag, const std::vector<std::string> &)> site_;
    std::function<std::string(const Type *)> cppType_;
    std::function<std::string(size_t)> fieldName_;
};

} // namespace

void FunctionEmitter::lowerOperator(Frame &frame, gc_node_ref_t ref, Block &out) {
    const GCGraph *g      = frame.graph;
    const auto *node      = g->node(ref);
    const std::string uri = operatorUri(g, ref);
    if (!program_.values.type(node->dataType())) {
        refuse(
            g,
            ref,
            std::format(
                "the result of operator '{}' in {} has type '{}'; {}",
                uri,
                describeFunction(g),
                typeName(node->dataType()),
                kTranslatedTypes));
    }
    std::vector<CppValue> norm, with;
    std::vector<const Type *> normTypes, withTypes;
    auto collect = [&](std::span<const gc_node_ref_t> inputs,
                       std::vector<CppValue> &values,
                       std::vector<const Type *> &types) {
        for (gc_node_ref_t input : inputs) {
            const Type *type = g->node(input)->dataType();
            if (!program_.values.type(type)) {
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
    // The arguments' nodes and the first argument's type, kept before they are moved into the
    // call a rule sees: a rule asks for a field name after that, and the field index is a node of
    // the graph (a constant), not a value of the arguments.
    const std::vector<gc_node_ref_t> normInputs(
        g->normInputsOf(ref).begin(), g->normInputsOf(ref).end());
    const Type *firstNormType = normTypes.empty() ? nullptr : normTypes.front();

    if (lowerVariable(frame, ref, uri, out) || lowerHigherOrder(frame, ref, uri, out) ||
        lowerFail(frame, ref, uri, out)) {
        return;
    }
    const OperatorRule *rule = findOperatorRule(uri);
    if (rule == nullptr) {
        for (const auto types : {&normTypes, &withTypes}) {
            for (const Type *t : *types) {
                if (!bridgeCarries(t)) {
                    refuse(
                        g,
                        ref,
                        std::format(
                            "{} uses the operator '{}' on a value of type '{}', which has no "
                            "translation in std::cpp yet",
                            describeFunction(g),
                            uri,
                            typeName(t)));
                }
            }
        }
        if (!bridgeCarries(node->dataType())) {
            refuse(
                g,
                ref,
                std::format(
                    "{} uses the operator '{}', whose result of type '{}' has no translation in "
                    "std::cpp yet",
                    describeFunction(g),
                    uri,
                    typeName(node->dataType())));
        }
        // No standalone translation: the operator of the runtime module runs through the
        // bridge (camel/standalone/bridge.h), and the program links libcamel.
        auto [it, added] = program_.bridgedOperators.try_emplace(uri, std::string());
        if (added) {
            it->second = std::format("op_{}", cppIdentifier(uri));
        }
        if (program_.firstBridgedGraph == nullptr) {
            program_.firstBridgedGraph = g;
            program_.firstBridgedNode  = ref;
        }
        auto values = [&](const std::vector<CppValue> &args,
                          const std::vector<const Type *> &types) {
            std::string out;
            for (size_t i = 0; i < args.size(); ++i) {
                const bool wide =
                    args[i].literal && types[i]->code() == camel::core::type::TypeCode::Int64;
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
            *program_.values.type(node->dataType()),
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
        node->dataType(),
        [&](RuntimeDiag kind, const std::vector<std::string> &args) {
            return program_.sites.siteFor(g, ref, kind, args);
        },
        [&](const Type *t) { return program_.values.type(t).value_or("void"); },
        [&](size_t argIndex) {
            // The field a store writes is an index the compiler put in a constant
            // (`:op/set_field`): the name is the struct type's, not the argument's text.
            if (argIndex >= normInputs.size() || firstNormType == nullptr) {
                return std::string();
            }
            const auto *index = g->node(normInputs[argIndex]);
            if (index->kind != GCNodeKind::Data || index->dataType() == nullptr ||
                index->dataType()->code() != camel::core::type::TypeCode::Int64) {
                return std::string();
            }
            const int64_t field = camel::core::rtdata::fromSlot<int64_t>(
                g->staticArea()->get<slot_t>(-index->dataIndex));
            if (field < 0) {
                return std::string();
            }
            return program_.values.fieldName(firstNormType, static_cast<size_t>(field));
        });
    auto expr = rule->emit(call);
    if (!expr) {
        // The rule said no: name the operator *and* the values it was given, because what the
        // rule turns on is their types. A refusal that only says "on values" leaves the reader to
        // guess which shape the writer wanted -- the one thing the census sorts by.
        std::string types;
        std::string last;
        for (size_t i = 0; i < call.normCount(); ++i) {
            const Type *type = call.normType(i);
            if (type == nullptr) {
                continue;
            }
            std::string name = typeName(type);
            if (name == last) {
                continue; // the same type twice running: the shape is what the reader wants once
            }
            types += types.empty() ? "'" : " and '";
            types += name;
            types += "'";
            last = std::move(name);
        }
        refuse(
            g,
            ref,
            std::format(
                "{} uses the operator '{}' on values{} std::cpp does not translate it for yet",
                describeFunction(g),
                uri,
                types.empty() ? std::string() : " of type " + types));
    }
    bind(frame, ref, std::move(*expr), rule->inlinable, out, operatorHint(uri));
}

void FunctionEmitter::lowerLoop(
    Frame &frame, gc_node_ref_t loop, const std::vector<gc_node_ref_t> &order, Block &out) {
    GCGraph *g = frame.graph;
    if (g->loopCollectionCountOf(loop) != 0) {
        refuse(g, loop, std::format("{} has a loop that collects values", describeFunction(g)));
    }
    const bool iterates = camel::core::loopHasIterable(g->loopKindOf(loop));
    const auto norm     = g->normInputsOf(loop);
    const size_t k      = norm.size() - (iterates ? 1 : 0);
    const auto &regions = controlRegionsOf(g);
    const auto parts    = regions.loopPartsOf(loop);
    const auto results  = g->withInputsOf(loop);
    const bool hasCond  = parts[0] != 0;
    const gc_node_ref_t bodyResult = results[0];
    const gc_node_ref_t condResult = hasCond ? results[1] : kInvalidNodeRef;

    // The state is k variables the rounds assign; the ports of both regions are those variables.
    std::vector<std::string> state;
    for (size_t j = 0; j < k; ++j) {
        const gc_node_ref_t port = g->regionPorts(parts[1])[(iterates ? 1 : 0) + j];
        const std::string name   = locals_.unique(std::format("v{}_state", nextValue_++));
        out.push_back(
            Stmt::line(
                std::format(
                    "{} {} = {};",
                    cppType(g, port, g->node(port)->dataType()),
                    name,
                    withoutParentheses(valueOf(frame, norm[(iterates ? 1 : 0) + j]).text))));
        state.push_back(name);
        frame.values[port] = CppValue{name, false};
        if (hasCond) {
            frame.values[g->regionPorts(parts[0])[j]] = CppValue{name, false};
        }
    }
    std::string array, index;
    if (iterates) {
        const gc_node_ref_t iterable = norm.front();
        array = locals_.unique(std::format("v{}_items", nextValue_++));
        index = locals_.unique(std::format("v{}_round", nextValue_++));
        if (g->loopIterableFormOf(loop) == camel::core::LoopIterableForm::Interval) {
            // `for i in a..b`: the graph's sequence is the pair of the interval's two bounds
            // (design principles 4.2, LoopIterableForm::Interval). The C++ this exporter writes
            // has no loop driver of its own — it writes the loop out — so the interval becomes
            // the array of its integers here, with the operation the interval is
            // (`:struct/range`, rt::range). What the program computes is the same.
            const auto bounds = g->withInputsOf(iterable);
            if (bounds.size() != 2) {
                refuse(
                    g,
                    loop,
                    std::format("{} has an interval without its two bounds", describeFunction(g)));
            }
            const std::vector<std::string> marks{
                SiteTable::valueMark(0), SiteTable::valueMark(1)};
            out.push_back(
                Stmt::line(
                    std::format(
                        "const {} {} = rt::range({}, {}, 1, {});",
                        cppType(g, iterable, g->node(iterable)->dataType()),
                        array,
                        withoutParentheses(valueOf(frame, bounds[0]).text),
                        withoutParentheses(valueOf(frame, bounds[1]).text),
                        program_.sites.siteFor(g, iterable, RuntimeDiag::InvalidArgument, marks))));
        } else {
            out.push_back(
                Stmt::line(
                    std::format(
                        "const {} {} = {};",
                        cppType(g, iterable, g->node(iterable)->dataType()),
                        array,
                        withoutParentheses(valueOf(frame, iterable).text))));
        }
        out.push_back(Stmt::line(std::format("std::size_t {} = 0;", index)));
    }

    const auto nodesIn = [&](uint32_t part) {
        std::vector<gc_node_ref_t> nodes;
        for (gc_node_ref_t n : order) {
            if (part != 0 && !frame.consumed.contains(n) &&
                regions.within(regions.controlOf(n), part)) {
                nodes.push_back(n);
            }
        }
        return nodes;
    };
    const std::vector<gc_node_ref_t> condNodes = nodesIn(parts[0]);
    const std::vector<gc_node_ref_t> bodyNodes = nodesIn(parts[1]);

    Block round;
    if (hasCond) {
        Block cond = lowerSequence(frame, condNodes, condResult, Tail{});
        round.insert(round.end(), cond.begin(), cond.end());
        round.push_back(
            Stmt::line(std::format("if (!({})) {{ break; }}", valueOf(frame, condResult).text)));
        for (gc_node_ref_t n : condNodes) {
            frame.consumed.insert(n);
        }
    }
    if (iterates) {
        const gc_node_ref_t element = g->regionPorts(parts[1]).front();
        round.push_back(Stmt::line(std::format("if ({} >= {}.size()) {{ break; }}", index, array)));
        const std::string name = locals_.unique(std::format("v{}_item", nextValue_++));
        round.push_back(
            Stmt::line(
                std::format(
                    "const {} {} = {}[{}];",
                    cppType(g, element, g->node(element)->dataType()),
                    name,
                    array,
                    index)));
        round.push_back(Stmt::line(std::format("++{};", index)));
        frame.values[element] = CppValue{name, false};
    }
    Block body = lowerSequence(frame, bodyNodes, bodyResult, Tail{});
    round.insert(round.end(), body.begin(), body.end());
    for (gc_node_ref_t n : bodyNodes) {
        frame.consumed.insert(n);
    }
    // The next state. The components are all computed before any variable is assigned.
    const std::string next = valueOf(frame, bodyResult).text;
    if (k == 1) {
        round.push_back(Stmt::line(std::format("{} = {};", state[0], withoutParentheses(next))));
    } else {
        const Type *type  = g->node(bodyResult)->dataType();
        cppType(g, bodyResult, type);
        const std::string tuple = locals_.unique(std::format("v{}_next", nextValue_++));
        round.push_back(Stmt::line(std::format("const auto {} = {};", tuple, withoutParentheses(next))));
        for (size_t j = 0; j < k; ++j) {
            round.push_back(
                Stmt::line(
                    std::format("{} = {}.{};", state[j], tuple, program_.values.fieldName(type, j))));
        }
    }
    out.push_back(Stmt::loop(std::move(round)));

    // The value of the loop is its state.
    if (k == 1) {
        frame.values[loop] = CppValue{state[0], false};
    } else {
        const Type *type = g->node(loop)->dataType();
        std::string list;
        for (size_t j = 0; j < k; ++j) {
            list += (j ? ", " : "") + state[j];
        }
        const std::string name = locals_.unique(std::format("v{}_loop", nextValue_++));
        out.push_back(
            Stmt::line(std::format("const {} {} = {}{{{}}};", cppType(g, loop, type), name, cppType(g, loop, type), list)));
        frame.values[loop] = CppValue{name, false};
    }
    frame.consumed.insert(loop);
}

bool FunctionEmitter::lowerBranch(
    Frame &frame, gc_node_ref_t brch, const std::vector<gc_node_ref_t> &order, Block &out,
    bool joinIsTail, const Tail &tail) {
    GCGraph *g                = frame.graph;
    const gc_node_ref_t join  = g->nodeBodyAs<GCBrchBody>(brch)->join;
    const auto *joinNode      = join != kInvalidNodeRef ? g->node(join) : nullptr;
    const size_t armCount     = g->armCountOf(brch);
    const auto cases          = g->withInputsOf(brch);
    const auto selectorInputs = g->normInputsOf(brch);
    if (joinNode == nullptr || joinNode->kind != GCNodeKind::Join || selectorInputs.empty() ||
        armCount < 2 || g->withInputsOf(join).size() != armCount) {
        refuse(g, brch, std::format("a branch of {} is malformed", describeFunction(g)));
    }
    const std::string selector = valueOf(frame, selectorInputs.front()).text;

    // Where the arms put the value of the branch.
    Tail armTail{Tail::Kind::Discard, {}};
    if (joinIsTail) {
        armTail            = tail;
        frame.values[join] = CppValue{"", false}; // the arms end the sequence themselves
    } else if (!isVoid(joinNode->dataType()) && liveUses(g, join) > 0) {
        const std::string type = cppType(g, join, joinNode->dataType());
        const std::string name = locals_.unique(std::format("v{}_branch", nextValue_++));
        out.push_back(Stmt::line(std::format("{} {};", type, name)));
        armTail            = Tail{Tail::Kind::Assign, name};
        frame.values[join] = CppValue{name, false};
    } else {
        frame.values[join] = CppValue{"", false};
    }

    // The form std::opt::inline leaves: an arm's nodes lie between the BRCH and the JOIN in the
    // execution order, each arm as one piece starting at its entry (the node std::fvm jumps
    // to, resolveRuntimeBranchArmEntry) and running to the next arm's entry or the JOIN.
    const auto brchAt = std::find(order.begin(), order.end(), brch);
    const auto joinAt = std::find(order.begin(), order.end(), join);
    std::vector<size_t> entries(armCount, 0);
    if (brchAt != order.end() && joinAt != order.end()) {
        const size_t joinPos = static_cast<size_t>(joinAt - order.begin());
        for (size_t k = 0; k < armCount; ++k) {
            const gc_node_ref_t entry = g->execPlan().branchArmEntriesOfNode(brch)[k];
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
    for (size_t k = 0; k < armCount; ++k) {
        Block body;
        const gc_node_ref_t result = g->withInputsOf(join)[k];
        const std::vector<gc_node_ref_t> region = regionOf(k);
        body = lowerSequence(frame, region, resultThrough(g, result), armTail);
        for (gc_node_ref_t n : region) {
            frame.consumed.insert(n);
        }
        std::string condition;
        if (k + 1 < armCount) {
            if (cases.empty()) {
                // `if`: the first arm when the condition holds; a branch on a number picks the
                // arm of that index.
                condition = armCount == 2 ? withoutParentheses(selector)
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

std::string FunctionEmitter::callDepth(const GCGraph *graph, gc_node_ref_t ref) {
    if (graph == program_.root) {
        return "depth"; // a call the entry makes is not counted (kEntryCalls)
    }
    // The count of open calls goes one up, checked against the limit at this call; the
    // report names the graph the call is written in (camel/execute/limits.h).
    return std::format(
        "rt::call(depth, {})",
        program_.sites.siteFor(
            graph,
            ref,
            RuntimeDiag::MaxRecursionDepthExceeded,
            {graph->name(), std::to_string(program_.recursionLimit)},
            std::to_string(program_.recursionLimit)));
}

// ---- Composite values (design note, section 4) ----

/// The bridge carries numbers, booleans and strings (bridge.h).
bool FunctionEmitter::bridgeCarries(const Type *type) {
    using camel::core::type::TypeCode;
    switch (type != nullptr ? type->code() : TypeCode::Void) {
    case TypeCode::Int32:
    case TypeCode::Int64:
    case TypeCode::Float32:
    case TypeCode::Float64:
    case TypeCode::Bool:
    case TypeCode::String:
    case TypeCode::Void:
        return true;
    default:
        return false;
    }
}

GCGraph *FunctionEmitter::functionConstantOf(const GCGraph *graph, gc_node_ref_t ref) {
    const auto *node = graph->node(ref);
    if (node == nullptr || node->kind != GCNodeKind::Data || node->dataType() == nullptr ||
        node->dataType()->code() != camel::core::type::TypeCode::Function) {
        return nullptr;
    }
    auto *function = camel::core::rtdata::fromSlot<::Function *>(
        graph->staticArea()->get<slot_t>(-node->dataIndex));
    return function != nullptr ? function->graph() : nullptr;
}

std::optional<std::string>
FunctionEmitter::functionValue(GCGraph *function, const std::vector<std::string> &captures) {
    // A function value is the graph and the values it captured (design principles, section
    // 1): a lambda that calls the graph's function with the arguments, then the captures.
    auto name = program_.functionNames.find(function);
    auto type = program_.values.type(function->funcType());
    if (name == program_.functionNames.end() || !type) {
        return std::nullopt;
    }
    std::string params, args;
    size_t i = 0;
    for (const auto ports : {function->normPorts(), function->withPorts()}) {
        for (gc_node_ref_t port : ports) {
            auto t = program_.values.type(function->node(port)->dataType());
            if (!t) {
                return std::nullopt;
            }
            params += std::format(", {} a{}", *t, i);
            args += std::format(", a{}", i);
            ++i;
        }
    }
    std::string capture, captured;
    for (size_t k = 0; k < captures.size(); ++k) {
        capture += std::format("{}c{} = {}", k ? ", " : "", k, captures[k]);
        captured += std::format(", c{}", k);
    }
    const std::string text = std::format(
        "Function(graph={}, tupleSlots={})",
        function->name(),
        function->closureNodes().size());
    return std::format(
        "{}({}, [{}](rt::Depth depth{}) {{ return {}(depth{}{}); }})",
        *type,
        cppStringLiteral(text),
        capture,
        params,
        name->second,
        args,
        captured);
}

void FunctionEmitter::lowerFill(Frame &frame, gc_node_ref_t ref, Block &out) {
    const GCGraph *g = frame.graph;
    const auto *node = g->node(ref);
    const auto *body = g->nodeBodyAs<camel::runtime::GCFillBody>(ref);
    const auto norm  = g->normInputsOf(ref);
    const auto with  = g->withInputsOf(ref);
    if (norm.empty()) {
        refuse(g, ref, std::format("a construction in {} has no template", describeFunction(g)));
    }
    const gc_node_ref_t templ = norm.front();
    const auto slots          = body->slots();

    if (body->fillKind == camel::runtime::GCFillKind::FunctionClosure) {
        // A closure: the function constant and the values it captures, slot i of the closure
        // being the i-th captured port of the function's graph.
        GCGraph *function = functionConstantOf(g, templ);
        std::vector<std::string> captures(
            function != nullptr ? function->closureNodes().size() : 0);
        for (size_t i = 0; i < with.size() && i < slots.size(); ++i) {
            const auto at = static_cast<size_t>(slots[i]);
            if (at < captures.size()) {
                captures[at] = valueOf(frame, with[i]).text;
            }
        }
        auto value = function != nullptr ? functionValue(function, captures) : std::nullopt;
        if (!value) {
            refuse(
                g,
                ref,
                std::format(
                    "{} makes a function value of type '{}' std::cpp does not translate yet",
                    describeFunction(g),
                    typeName(node->dataType())));
        }
        bind(frame, ref, std::move(*value), true, out, "closure");
        return;
    }

    // A tuple, struct or array: the template's constant elements, with the computed ones in
    // their places.
    const Type *type          = node->dataType();
    const std::string cppName = cppType(g, ref, type);
    const slot_t constant     = g->staticArea()->get<slot_t>(-g->node(templ)->dataIndex);
    std::vector<std::string> items;
    std::vector<const Type *> itemTypes;
    using camel::core::type::TypeCode;
    if (type->code() == TypeCode::Array) {
        const auto *array = camel::core::rtdata::fromSlot<::Array *>(constant);
        const Type *elem  = static_cast<const camel::core::type::ArrayType *>(type)->elemType();
        for (size_t i = 0; array != nullptr && i < array->size(); ++i) {
            itemTypes.push_back(elem);
            items.push_back("");
        }
    } else if (type->code() == TypeCode::Tuple) {
        const auto *tuple = static_cast<const camel::core::type::TupleType *>(type);
        for (size_t i = 0; i < tuple->size(); ++i) {
            itemTypes.push_back(tuple->typeAt(i));
            items.push_back("");
        }
    } else if (type->code() == TypeCode::Struct) {
        const auto *record = static_cast<const camel::core::type::StructType *>(type);
        for (size_t i = 0; i < record->size(); ++i) {
            itemTypes.push_back(record->typeAt(i));
            items.push_back("");
        }
    }
    for (size_t i = 0; i < with.size() && i < slots.size(); ++i) {
        const auto at = static_cast<size_t>(slots[i]);
        if (at < items.size()) {
            items[at] = valueOf(frame, with[i]).text;
        }
    }
    for (size_t i = 0; i < items.size(); ++i) {
        if (!items[i].empty()) {
            continue;
        }
        // An element the template holds: the constant's own.
        slot_t element = 0;
        if (type->code() == TypeCode::Array) {
            element = camel::core::rtdata::fromSlot<::Array *>(constant)->get<slot_t>(i);
        } else if (type->code() == TypeCode::Tuple) {
            element = camel::core::rtdata::fromSlot<::Tuple *>(constant)->get<slot_t>(i);
        } else {
            element = camel::core::rtdata::fromSlot<::Struct *>(constant)->get<slot_t>(i);
        }
        auto text = program_.values.constant(element, itemTypes[i]);
        if (!text) {
            refuse(
                g,
                ref,
                std::format(
                    "{} builds a value of type '{}', and std::cpp cannot write one of its "
                    "elements",
                    describeFunction(g),
                    typeName(type)));
        }
        items[i] = *text;
    }
    std::string list;
    for (size_t i = 0; i < items.size(); ++i) {
        list += (i ? ", " : "") + items[i];
    }
    bind(frame, ref, std::format("{}{{{}}}", cppName, list), true, out, "value");
}

void FunctionEmitter::lowerAccess(Frame &frame, gc_node_ref_t ref) {
    const GCGraph *g          = frame.graph;
    const gc_node_ref_t input = g->normInputsOf(ref).front();
    const Type *type          = g->node(input)->dataType();
    const auto *body          = g->nodeBodyAs<camel::runtime::GCAccsBody>(ref);
    cppType(g, input, type);
    size_t index = body->value;
    if (body->accsKind == camel::runtime::GCAccsKind::StructKey) {
        const auto *record = static_cast<const camel::core::type::StructType *>(type);
        const auto found   = record->findField(body->key());
        if (!found) {
            refuse(g, ref, std::format("a field of {} is not in its struct", describeFunction(g)));
        }
        index = *found;
    }
    frame.values[ref] = CppValue{
        std::format("{}.{}", valueOf(frame, input).text, program_.values.fieldName(type, index)),
        false};
}

void FunctionEmitter::lowerCallValue(Frame &frame, gc_node_ref_t ref, Block &out) {
    // A call of a function value (design principles 2.1): with input 0 is the value,
    // the other `with` inputs and the normal ones its arguments.
    const GCGraph *g = frame.graph;
    const auto with  = g->withInputsOf(ref);
    if (with.empty()) {
        refuse(g, ref, std::format("a call in {} has no function", describeFunction(g)));
    }
    if (g == program_.root && ref == program_.entryCall) {
        // The root's call of `main`: a direct call, with the values main captures from the
        // top level (the closure the root builds of them, a FILL).
        const gc_node_ref_t closure = with.front();
        std::string expr = std::format("{}(depth", program_.functionNames.at(program_.entryMain));
        for (gc_node_ref_t arg : g->normInputsOf(ref)) {
            expr += ", " + valueOf(frame, arg).text;
        }
        if (g->node(closure)->kind == GCNodeKind::Fill) {
            for (gc_node_ref_t captured : g->withInputsOf(closure)) {
                expr += ", " + valueOf(frame, captured).text;
            }
        }
        bind(frame, ref, expr + ")", false, out, "main");
        return;
    }
    cppType(g, with.front(), g->node(with.front())->dataType());
    std::string expr = std::format("{}({}", valueOf(frame, with.front()).text, callDepth(g, ref));
    for (gc_node_ref_t arg : g->normInputsOf(ref)) {
        expr += ", " + valueOf(frame, arg).text;
    }
    for (size_t i = 1; i < with.size(); ++i) {
        expr += ", " + valueOf(frame, with[i]).text;
    }
    bind(frame, ref, expr + ")", false, out, "call");
}

void FunctionEmitter::lowerCast(Frame &frame, gc_node_ref_t ref) {
    // A value entering `T?` or `T!` (design principles 3.8), `null` entering `T?`, a failed
    // value passed on with another payload (`->`), a conversion between numbers.
    const GCGraph *g          = frame.graph;
    const gc_node_ref_t input = g->normInputsOf(ref).front();
    const Type *toType        = g->node(ref)->dataType();
    const Type *fromType      = g->node(input)->dataType();
    const std::string to      = cppType(g, ref, toType);
    const CppValue &value     = valueOf(frame, input);
    const std::string from    = isVoid(fromType) ? "void" : cppType(g, input, fromType);
    using camel::core::type::TupleType;
    using camel::core::type::TypeCode;
    const auto *target =
        toType->code() == TypeCode::Tuple ? static_cast<const TupleType *>(toType) : nullptr;
    const auto *source = fromType != nullptr && fromType->code() == TypeCode::Tuple
                             ? static_cast<const TupleType *>(fromType)
                             : nullptr;
    std::string text;
    // A CAST between two struct types of one layout moves nothing (design principles 5.1 and
    // 5.2): an anonymous value enters a named type, and an `as` between named types with the
    // fields in the same order changes the type only. Both are one C++ struct, so there is
    // nothing to write; a conversion that reorders the fields is a FILL, not a CAST.
    const bool sameStruct =
        fromType != nullptr && toType != nullptr && fromType->code() == TypeCode::Struct &&
        toType->code() == TypeCode::Struct && program_.values.sameRepresentation(fromType, toType);
    if (to == from || sameStruct) {
        frame.values[ref] = value;
        return;
    } else if (
        target != nullptr && target->isNullable() &&
        (isVoid(fromType) || value.text == "rt::Unit{}")) {
        text = std::format("{}{{}}", to); // null
    } else if (
        target != nullptr && target->isFallible() && source != nullptr && source->isFallible()) {
        text = std::format("rt::retag<{}>({})", to, value.text);
    } else if (target != nullptr && (target->isNullable() || target->isFallible())) {
        text = std::format("{}({})", to, value.text);
    } else if (
        fromType != nullptr && fromType->code() <= TypeCode::Bool &&
        toType->code() <= TypeCode::Bool) {
        text = std::format("static_cast<{}>({})", to, value.text);
    } else {
        refuse(
            g,
            ref,
            std::format(
                "{} converts a value of type '{}' to '{}', which std::cpp does not translate yet",
                describeFunction(g),
                typeName(fromType),
                typeName(toType)));
    }
    frame.values[ref] = CppValue{text, false};
}

// ---- `var` variables (design note 5.3) ----

namespace {

/// Whether the operator `id` reads or writes a variable it is given first: :var/get,
/// :var/peek and the assignments (:op/assn_owned among them).
bool isVariableAccess(camel::core::OperId id) {
    return camel::core::oper::isVariableRead(id) ||
           camel::core::OperatorTable::instance().isAssignment(id);
}

/// Whether `text` is one C++ identifier (a name the emitter bound), not an expression.
bool isIdentifier(std::string_view text) {
    return !text.empty() && !std::isdigit(static_cast<unsigned char>(text.front())) &&
           std::all_of(text.begin(), text.end(), [](char c) {
               return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
           });
}

/// The operator an assignment `x op= v` computes `x op v` with (":op/assn_add_l" gives
/// ":op/add_l"); a plain assignment has none.
std::string assignedOperator(std::string_view uri) {
    const std::string_view rest = uri.substr(std::string_view(":op/assn").size());
    if (rest.empty() || rest == "_i" || rest == "_l" || rest == "_f" || rest == "_d" ||
        rest == "_s" || rest == "_b") {
        return "";
    }
    if (rest == "_and") {
        return ":op/and";
    }
    if (rest == "_or") {
        return ":op/or";
    }
    return std::string(":op/") + std::string(rest.substr(1)); // _add_l -> add_l
}

} // namespace

bool FunctionEmitter::cellEscapes(const GCGraph *graph, gc_node_ref_t cell) {
    // A variable stays a C++ local while everything that has its cell reads or writes it, in
    // this function or an arm lowered into it.
    for (const auto users : {graph->normOutputsOf(cell), graph->withOutputsOf(cell)}) {
        for (gc_node_ref_t user : users) {
            const auto *node = graph->node(user);
            if (node == nullptr) {
                continue;
            }
            if (node->kind == GCNodeKind::Oper &&
                (isVariableAccess(operatorId(graph, user)) ||
                 operatorId(graph, user) == camel::core::oper::VarTake) &&
                graph->normInputsOf(user).front() == cell) {
                continue;
            }
            if (node->kind == GCNodeKind::Gate) {
                if (cellEscapes(graph, user)) {
                    return true;
                }
                continue;
            }
            return true;
        }
    }
    return false;
}

bool FunctionEmitter::lowerVariable(
    Frame &frame, gc_node_ref_t ref, const std::string &uri, Block &out) {
    const GCGraph *g             = frame.graph;
    const camel::core::OperId id = operatorId(g, ref);
    if (id == camel::core::oper::VarNew) {
        const gc_node_ref_t init = g->normInputsOf(ref).front();
        const Type *valueType    = g->node(init)->dataType();
        const std::string type   = cppType(g, init, valueType);
        const std::string name   = locals_.unique(std::format("var{}", nextValue_++));
        if (cellEscapes(g, ref)) {
            out.push_back(
                Stmt::line(
                    std::format(
                        "const rt::Cell<{}> {}{{{}}};",
                        type,
                        name,
                        withoutParentheses(valueOf(frame, init).text))));
            frame.values[ref] = CppValue{name, false, CppValue::Cell::Shared};
        } else {
            out.push_back(
                Stmt::line(
                    std::format(
                        "{} {} = {};",
                        type,
                        name,
                        withoutParentheses(valueOf(frame, init).text))));
            frame.values[ref] = CppValue{name, false, CppValue::Cell::Local};
        }
        return true;
    }
    const auto norm = g->normInputsOf(ref);
    if (id == camel::core::oper::VarTake) {
        // `:var/take` hands the variable's own array to an in-place form. The generated
        // program moves it there (`std::move(cell)` in the form's lowering), so the take is
        // the cell's value, not a copy of it: the node is the variable's cell, and a later
        // read of it dereferences a shared cell the same way a read of the variable does.
        const CppValue taken = valueOf(frame, norm.front());
        frame.values[ref]    = CppValue{taken.text, false, taken.cell};
        return true;
    }
    if (id == camel::core::oper::VarAfter) {
        // `:var/after(cell, call)`: the variable as it stands after the call that wrote it. The
        // call is a statement of its own here -- its effects run in the order the graph gives
        // them -- so the version is the cell, exactly as a write leaves it; the second argument
        // is not read (it may be `void`). The read above it dereferences the cell as it always
        // does, after that statement.
        const CppValue &cell = valueOf(frame, norm.front());
        frame.values[ref]    = CppValue{cell.text, false, cell.cell};
        return true;
    }
    if (uri == ":struct/append_arr_in_place") {
        return lowerAppendInPlace(frame, ref, out);
    }
    if (uri == ":op/set_index_in_place") {
        return lowerSetIndexInPlace(frame, ref, out);
    }
    if (uri == ":op/assn_owned") {
        // `xs = xs->append<v>` (the in-place form of `append`) is written
        // :op/assn_owned(xs, :struct/append_arr_in_place(v, :var/take(xs))), and `xs[i] = v` is
        // :op/assn_owned(xs, :op/set_index_in_place(:var/take(xs), i, v)). The variable gives its
        // array up for the call, so when nothing else holds it the form grows or changes it in
        // place (design note 5.2), which is what the interpreter's sole-reference mark does.
        // The other arguments are computed first: an expression that read the variable must see
        // the array before it is given up. A form may already have been lowered as a step of the
        // sequence before the assignment that stores it, so it is lowered once (consumed).
        if (norm.size() == 2 && g->node(norm[1])->kind == GCNodeKind::Oper) {
            const std::string form = operatorUri(g, norm[1]);
            const bool lowered =
                (form == ":struct/append_arr_in_place" && lowerAppendInPlace(frame, norm[1], out)) ||
                (form == ":op/set_index_in_place" && lowerSetIndexInPlace(frame, norm[1], out));
            if (lowered) {
                // The assignment stands for the variable's version, so it carries what the form
                // left: the variable's cell with its marking, which is how a write leaves the
                // variable too (`lowerVariable`'s assignment path).
                const CppValue stored = valueOf(frame, norm[1]);
                frame.values[ref]     = CppValue{stored.text, false, stored.cell};
                return true;
            }
        }
        refuse(
            g,
            ref,
            std::format(
                "{} binds a variable to an in-place form that std::cpp does not translate yet",
                describeFunction(g)));
    }
    if (!isVariableAccess(id)) {
        return false;
    }
    const CppValue &cell   = valueOf(frame, norm.front());
    const bool local       = cell.cell == CppValue::Cell::Local;
    const std::string read = local ? cell.text : cell.text + ".get()";
    // A write gives the cell of this version, so the node's declared type is the cell; the value
    // it holds is what the compound operator (if one is left) and the stored value have.
    const Type *type = camel::core::type::valueTypeOf(g->node(ref)->dataType());
    if (camel::core::oper::isVariableRead(id)) {
        // Read now: a later assignment must not change what this read gave. :var/peek is the
        // same read; what it tells the interpreter (the array may still be grown in place)
        // the reference count tells the generated program.
        bind(frame, ref, read, false, out, "read");
        return true;
    }
    // An assignment: `x = v`, or `x = x op v` computed as the operator `op` is.
    std::string value = valueOf(frame, norm[1]).text;
    if (const std::string op = assignedOperator(uri); !op.empty()) {
        const OperatorRule *rule = findOperatorRule(op);
        if (rule == nullptr) {
            refuse(g, ref, std::format("{} uses the assignment '{}'", describeFunction(g), uri));
        }
        // `x op= v` on a local: x is overwritten by the result, so the operator may take it
        // (a string or array it holds alone is extended in place, design note 5.2).
        NodeCall call(
            op,
            {CppValue{
                 local && cppType(g, ref, type).starts_with("rt::") ? "std::move(" + cell.text + ")"
                                                                    : read,
                 false},
             valueOf(frame, norm[1])},
            {},
            {type, g->node(norm[1])->dataType()},
            {},
            type,
            [&](RuntimeDiag kind, const std::vector<std::string> &args) {
                return program_.sites.siteFor(g, ref, kind, args);
            },
            [&](const Type *t) { return program_.values.type(t).value_or("void"); },
            // A compound assignment writes no field of a record: its operator takes a value and
            // the variable's own, and the field name is not among them.
            [&](size_t) { return std::string(); });
        value = rule->emit(call).value_or(value);
    }
    if (local) {
        out.push_back(Stmt::line(std::format("{} = {};", cell.text, withoutParentheses(value))));
    } else {
        out.push_back(Stmt::line(std::format("{}.set({});", cell.text, withoutParentheses(value))));
    }
    // What the assignment gives is the version of the variable: the cell it wrote into. The value
    // it wrote is its second operand, and the graph takes it from there where an assignment stands
    // where a value is wanted (Builder::visitLinkNode); a later read of the variable reads this
    // node, so it takes the cell.
    const std::string written = cell.text;
    const CppValue::Cell kind = cell.cell;
    frame.values[ref]         = CppValue{written, false, kind};
    return true;
}

bool FunctionEmitter::lowerAppendInPlace(Frame &frame, gc_node_ref_t ref, Block &out) {
    if (frame.consumed.contains(ref)) {
        return true;
    }
    const GCGraph *g = frame.graph;
    const auto taken = g->normInputsOf(ref);
    const auto items = g->withInputsOf(ref);
    if (taken.size() != 1 || items.size() != 1 ||
        g->node(taken.front())->kind != GCNodeKind::Oper ||
        operatorUri(g, taken.front()) != ":var/take") {
        return false;
    }
    const auto cellInput = g->normInputsOf(taken.front());
    if (cellInput.empty()) {
        return false;
    }
    const CppValue &cell = valueOf(frame, cellInput.front());
    const bool local     = cell.cell == CppValue::Cell::Local;
    std::string item     = valueOf(frame, items.front()).text;
    if (!isIdentifier(item)) {
        const std::string name = locals_.unique(std::format("item{}", nextValue_++));
        out.push_back(Stmt::line(std::format(
            "const {} {} = {};",
            cppType(g, items.front(), g->node(items.front())->dataType()),
            name,
            withoutParentheses(item))));
        item = name;
    }
    out.push_back(Stmt::line(
        local ? std::format("{0} = rt::append(std::move({0}), {1});", cell.text, item)
              : std::format("{0}.set(rt::append({0}.take(), {1}));", cell.text, item)));
    frame.consumed.insert(ref);
    // This node stands for the variable's own value, so it is the variable's cell with its
    // marking, exactly as a write leaves the variable (`lowerVariable`'s assignment path): a
    // non-escaping variable is a plain local and a later read of this node is that local, while
    // an escaping one is an rt::Cell, and a read of this node dereferences it once.
    frame.values[ref] = CppValue{cell.text, false, cell.cell};
    return true;
}

bool FunctionEmitter::lowerSetIndexInPlace(Frame &frame, gc_node_ref_t ref, Block &out) {
    if (frame.consumed.contains(ref)) {
        return true;
    }
    const GCGraph *g = frame.graph;
    const auto args  = g->normInputsOf(ref);
    if (args.size() != 3 || g->node(args.front())->kind != GCNodeKind::Oper ||
        operatorUri(g, args.front()) != ":var/take") {
        return false;
    }
    const auto cellInput = g->normInputsOf(args.front());
    if (cellInput.empty()) {
        return false;
    }
    const CppValue &cell = valueOf(frame, cellInput.front());
    const bool local     = cell.cell == CppValue::Cell::Local;
    // The index and the element are computed before the array is given up, as in the append
    // form: an expression that reads the variable must see the array as it was.
    std::vector<std::string> operand;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const gc_node_ref_t arg = args[i];
        std::string text        = valueOf(frame, arg).text;
        if (!isIdentifier(text)) {
            const std::string name = locals_.unique(std::format("item{}", nextValue_++));
            out.push_back(Stmt::line(std::format(
                "const {} {} = {};",
                cppType(g, arg, g->node(arg)->dataType()),
                name,
                withoutParentheses(text))));
            text = name;
        }
        operand.push_back(text);
    }
    const std::vector<std::string> marks{
        SiteTable::valueMark(0), SiteTable::valueMark(1), SiteTable::valueMark(2)};
    const std::string site =
        program_.sites.siteFor(g, ref, RuntimeDiag::IndexOutOfRange, marks);
    out.push_back(Stmt::line(
        local ? std::format(
                    "{0} = rt::setAt(std::move({0}), {1}, {2}, {3});",
                    cell.text,
                    operand[0],
                    operand[1],
                    site)
              : std::format(
                    "{0}.set(rt::setAt({0}.take(), {1}, {2}, {3}));",
                    cell.text,
                    operand[0],
                    operand[1],
                    site)));
    frame.consumed.insert(ref);
    // As the append form: the node stands for the variable's own value, the cell with its
    // marking, and a read of it dereferences the cell once.
    frame.values[ref] = CppValue{cell.text, false, cell.cell};
    return true;
}

// ---- The bottom type ----

bool FunctionEmitter::lowerFail(
    Frame &frame, gc_node_ref_t ref, const std::string &uri, Block &out) {
    if (uri != ":op/fail") {
        return false;
    }
    const GCGraph *g = frame.graph;
    // `fail<'Kind'>(message)`: the kind is a constant of the graph -- the tag the program wrote,
    // which is what the fault's report is named after (design principles 3.9) -- and the message
    // is the call's argument. Both are wanted, and neither is a value of the program's own words.
    const auto with   = g->withInputsOf(ref);
    const auto norm   = g->normInputsOf(ref);
    std::string kind;
    if (!with.empty()) {
        const auto *node = g->node(with[0]);
        if (node->kind == GCNodeKind::Data && node->dataType() != nullptr &&
            node->dataType()->code() == camel::core::type::TypeCode::String) {
            if (const auto *text = camel::core::rtdata::fromSlot<::String *>(
                    g->staticArea()->get<slot_t>(-node->dataIndex));
                text != nullptr) {
                kind = text->toString();
            }
        }
    }
    if (kind.empty()) {
        refuse(
            g,
            ref,
            std::format(
                "{} fails with a kind that is not a constant, and std::cpp names the report of "
                "the fault after the kind it was written with",
                describeFunction(g)));
    }
    if (norm.size() != 1) {
        refuse(
            g,
            ref,
            std::format(
                "{} fails with {} arguments, and std::cpp translates the message of a fail",
                describeFunction(g),
                norm.size()));
    }
    const std::string site = program_.sites.siteForProgramFault(g, ref, kind);
    bind(
        frame,
        ref,
        std::format("rt::fail({}, {})", site, valueOf(frame, norm[0]).text),
        false,
        out,
        "fail");
    return true;
}

// ---- Operators that call function values (hof.h) ----

bool FunctionEmitter::lowerHigherOrder(
    Frame &frame, gc_node_ref_t ref, const std::string &uri, Block &out) {
    // The generated program's own runtime (hof.h) has one entry per shape of call; which one an
    // operator uses is its declaration's (OperatorSemantics::elementCalls).
    using camel::core::sem::ElementShape;
    const GCGraph *g  = frame.graph;
    const auto *calls = camel::core::OperatorTable::instance().elementCalls(operatorId(g, ref));
    if (calls == nullptr) {
        return false;
    }
    const std::pair<camel::core::OperId, std::string> entry{
        operatorId(g, ref),
        calls->shape == ElementShape::Map       ? "map"
        : calls->shape == ElementShape::Filter  ? "filter"
        : calls->shape == ElementShape::Fold    ? "reduce"
        : calls->shape == ElementShape::Foreach ? "foreach"
                                                : "attempt"};
    const auto *it = &entry;
    // Each call of the element function counts as an open call made here.
    const std::string site = program_.sites.siteFor(
        g,
        ref,
        RuntimeDiag::MaxRecursionDepthExceeded,
        {g->name(), std::to_string(program_.recursionLimit)},
        std::to_string(program_.recursionLimit));
    const auto norm = g->normInputsOf(ref);
    const auto with = g->withInputsOf(ref);
    std::string expr;
    if (it->second == "attempt") {
        // The marked call of the value `attempt(f)` (f, args...): the result's payload type is the
        // C++ type of the `T!`'s T.
        const Type *result            = g->node(ref)->dataType();
        const auto *tagged            = static_cast<const camel::core::type::TupleType *>(result);
        const Type *payload           = tagged->payloadType();
        const std::string payloadType = isVoid(payload) ? "rt::Unit" : cppType(g, ref, payload);
        expr                          = std::format("rt::attempt<{}>(depth, {}", payloadType, site);
        for (gc_node_ref_t input : norm) {
            expr += ", " + valueOf(frame, input).text;
        }
        expr += ")";
    } else {
        expr = std::format(
            "rt::{}{}(depth, {}, {}, {}",
            it->second,
            it->second == "reduce" ? std::format("<{}>", cppType(g, ref, g->node(ref)->dataType()))
                                   : std::string(),
            site,
            valueOf(frame, norm.front()).text,
            valueOf(frame, with.front()).text);
        for (size_t i = 1; i < with.size(); ++i) {
            expr += ", " + valueOf(frame, with[i]).text; // the initial value of reduce
        }
        expr += ")";
    }
    bind(frame, ref, std::move(expr), false, out, it->second);
    return true;
}

} // namespace camel::passes::cpp
