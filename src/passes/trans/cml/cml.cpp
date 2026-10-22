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
 * Created: Oct. 22, 2026
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

// `std::cml`: print the runtime program as Camel source. Step S1 covers the straight-line
// subset: module values, functions, literals, accesses, casts, operators, direct calls,
// `let`/`var`, assignment, `return` and straight-line `sync`. A construct outside that subset is
// refused by name instead of being printed wrongly.
#include "camel/passes/trans/cml/cml.h"

#include "camel/core/error/diagnostics/messages/semantic.h"
#include "camel/core/operator_table.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/complex.h"
#include "camel/core/rtdata/conv.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/symbol.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/intern.h"
#include "camel/execute/graph_runtime_support.h"
#include "camel/passes/trans/gir_metadata.h"
#include "camel/runtime/exec_plan.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/reachable.h"
#include "camel/utils/number_text.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <queue>
#include <sstream>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

using namespace camel::core::rtdata;
using camel::core::SymId;
using camel::core::type::ArrayType;
using camel::core::type::FunctionType;
using camel::core::type::StructType;
using camel::core::type::TupleType;
using camel::core::type::Type;
using camel::core::type::TypeCode;
using camel::runtime::GCGraph;
using camel::runtime::gc_node_ref_t;
using camel::runtime::Program;

/// Why the printer cannot write a value. It carries the node so `apply` can point at it.
struct Refusal : std::exception {
    gc_node_ref_t ref;
    uint64_t origin;
    std::string graph;
    std::string reason;
    Refusal(gc_node_ref_t r, uint64_t o, std::string g, std::string why)
        : ref(r), origin(o), graph(std::move(g)), reason(std::move(why)) {}
    const char *what() const noexcept override { return reason.c_str(); }
};

/// A constant over `max_literal_bytes` the printer left out; every one is reported.
struct BigConstant {
    uint64_t origin;
    std::string name;
    std::string type;
    std::string graph;
    size_t bytes;
};

bool isIdentifier(std::string_view text) {
    if (text.empty()) {
        return false;
    }
    const auto word = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_';
    };
    if (!(text.front() == '_' || (text.front() >= 'a' && text.front() <= 'z') ||
          (text.front() >= 'A' && text.front() <= 'Z'))) {
        return false;
    }
    return std::all_of(text.begin(), text.end(), word);
}

/// True when the text is valid UTF-8, so it can be written raw in a UTF-8 source file.
bool isUtf8(std::string_view text) {
    size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        size_t extra   = 0;
        if (lead < 0x80) {
            ++i;
            continue;
        } else if ((lead & 0xE0) == 0xC0) {
            extra = 1;
        } else if ((lead & 0xF0) == 0xE0) {
            extra = 2;
        } else if ((lead & 0xF8) == 0xF0) {
            extra = 3;
        } else {
            return false;
        }
        if (i + extra >= text.size()) {
            return false;
        }
        for (size_t k = 1; k <= extra; ++k) {
            if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) {
                return false;
            }
        }
        i += extra + 1;
    }
    return true;
}

std::string escapeString(std::string_view text, bool towardUtf8) {
    std::string out = "'";
    for (const char raw : text) {
        const auto c = static_cast<unsigned char>(raw);
        switch (raw) {
        case '\n':
            out += "\\n";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\'':
            out += "\\'";
            break;
        case '\0':
            out += "\\x00";
            break;
        default:
            if (c < 0x20 || c == 0x7F) {
                out += std::format("\\x{:02X}", c);
            } else if (c < 0x80 || towardUtf8) {
                out.push_back(raw);
            } else {
                out += std::format("\\x{:02X}", c);
            }
            break;
        }
    }
    out += '\'';
    return out;
}

struct Expr {
    std::string text;
    int precedence = 100;
};

std::string parenthesize(const Expr &value, int minimum) {
    return value.precedence < minimum ? "(" + value.text + ")" : value.text;
}

std::string functionNameOf(const GCGraph *graph) {
    if (graph == nullptr) {
        return {};
    }
    const std::string &name = graph->name();
    return isIdentifier(name) ? name : std::string{};
}

} // namespace

namespace {

class CmlPrinter {
  public:
    CmlPrinter(
        const Program &program, std::ostream &out, size_t maxLiteralBytes,
        std::vector<BigConstant> &bigConstants)
        : program_(program), out_(out), maxLiteralBytes_(maxLiteralBytes),
          bigConstants_(bigConstants) {}

    void run() {
        GCGraph *root = program_.root();
        if (root == nullptr) {
            return;
        }
        graphs_ = camel::runtime::collectReachableGraphs(program_, root);
        collectTopLevelNames(root);
        findEntry(root);
        for (GCGraph *graph : graphs_) {
            validateGraph(graph, graph == root);
        }
        scanBigConstants();
        if (!bigConstants_.empty()) {
            return;
        }

        graph_   = root;
        isRoot_  = true;
        skip_    = entrySkip_;
        prepareGraph(root);
        if (!topo_.empty()) {
            printStatements();
            if (!lines_.empty()) {
                appendLines();
            }
        }

        std::vector<GCGraph *> functions;
        for (GCGraph *graph : graphs_) {
            if (graph != root) {
                functions.push_back(graph);
            }
        }
        std::stable_sort(
            functions.begin(), functions.end(), [](const GCGraph *a, const GCGraph *b) {
                // Source order is the graph order the compiler gave them; only `main` moves,
                // to the end, where a reader expects the entry point.
                const bool aMain = a->name() == "main";
                const bool bMain = b->name() == "main";
                if (aMain != bMain) {
                    return !aMain;
                }
                return false;
            });
        for (GCGraph *graph : functions) {
            printFunction(graph);
        }
    }

  private:
    const Program &program_;
    std::ostream &out_;
    size_t maxLiteralBytes_;
    std::vector<BigConstant> &bigConstants_;

    std::vector<GCGraph *> graphs_;
    std::unordered_map<const GCGraph *, bool> effectful_;
    std::unordered_set<std::string> topLevelNames_;
    std::unordered_set<gc_node_ref_t> entrySkip_;
    GCGraph *entryGraph_ = nullptr;

    GCGraph *graph_ = nullptr;
    bool isRoot_    = false;
    std::unordered_set<gc_node_ref_t> skip_;
    std::vector<gc_node_ref_t> topo_;
    std::unordered_map<gc_node_ref_t, std::string> params_;
    std::unordered_map<gc_node_ref_t, std::string> declared_;
    std::unordered_map<gc_node_ref_t, std::string> cells_;
    std::unordered_set<std::string> usedNames_;
    std::vector<std::string> lines_;
    size_t generated_ = 0;

    // ---- names ---------------------------------------------------------------------------

    std::string sourceName(gc_node_ref_t ref) const {
        const std::string_view name = graph_->nodeName(ref);
        return isIdentifier(name) ? std::string(name) : std::string{};
    }

    std::string freshName() {
        for (;;) {
            ++generated_;
            std::string candidate = std::format("cml{}", generated_);
            if (!usedNames_.contains(candidate)) {
                usedNames_.insert(candidate);
                return candidate;
            }
        }
    }

    // ---- graph setup ---------------------------------------------------------------------

    void collectTopLevelNames(GCGraph *root) {
        for (auto it = root->nodes().begin(); it != root->nodes().end(); ++it) {
            const std::string name = sourceNameIt(root, it.ref());
            if (!name.empty()) {
                topLevelNames_.insert(name);
            }
        }
    }

    std::string sourceNameIt(const GCGraph *graph, gc_node_ref_t ref) const {
        const std::string_view name = graph->nodeName(ref);
        return isIdentifier(name) ? std::string(name) : std::string{};
    }

    /// The entry is the call the root's value comes from. Its callee is a normal function; the
    /// function value and, for a module with captured top-level values, the closure fill around
    /// it are the only nodes the printer hides from the top-level statements.
    void findEntry(GCGraph *root) {
        gc_node_ref_t ref = camel::execute::resolveRuntimeTailValueRef(root);
        while (ref != camel::runtime::kInvalidNodeRef) {
            const camel::runtime::GCNode *node = root->node(ref);
            if (node == nullptr) {
                return;
            }
            if (node->kind == camel::runtime::GCNodeKind::Gate ||
                node->kind == camel::runtime::GCNodeKind::Sync ||
                node->kind == camel::runtime::GCNodeKind::Copy) {
                const auto inputs = root->normInputsOf(ref);
                if (inputs.empty()) {
                    return;
                }
                ref = inputs.back();
                continue;
            }
            break;
        }
        const camel::runtime::GCNode *call = root->node(ref);
        if (call == nullptr ||
            (call->kind != camel::runtime::GCNodeKind::Call &&
             call->kind != camel::runtime::GCNodeKind::Func)) {
            return;
        }
        entrySkip_.insert(ref);
        GCGraph *callee = program_.calleeOf(*root, ref);
        if (callee == nullptr) {
            return;
        }
        entryGraph_ = callee;

        const auto with = root->withInputsOf(ref);
        if (!with.empty()) {
            const camel::runtime::GCNode *value = root->node(with.front());
            if (value != nullptr &&
                (value->kind == camel::runtime::GCNodeKind::Fill ||
                 value->kind == camel::runtime::GCNodeKind::Data)) {
                entrySkip_.insert(with.front());
                if (value->kind == camel::runtime::GCNodeKind::Fill) {
                    const auto inputs = root->normInputsOf(with.front());
                    if (!inputs.empty()) {
                        entrySkip_.insert(inputs.front());
                    }
                }
            }
        }
    }

    void validateGraph(GCGraph *graph, bool root) {
        graph_  = graph;
        isRoot_ = root;
        if (!graph->executable()) {
            fail(camel::runtime::kInvalidNodeRef, "it has writings (it is not executable)");
        }
        if ((graph->flags() & camel::runtime::kGraphFlagMacro) != 0) {
            fail(camel::runtime::kInvalidNodeRef, "it is a macro graph");
        }
        if (graph->regionCount() > 1) {
            fail(camel::runtime::kInvalidNodeRef, "it has a control region");
        }
        if (!root) {
            const std::string &name = graph->name();
            if (!isIdentifier(name)) {
                fail(
                    camel::runtime::kInvalidNodeRef,
                    std::format("the graph name '{}' is not a source identifier", name));
            }
            const FunctionType *type = graph->funcType();
            if (type == nullptr) {
                fail(camel::runtime::kInvalidNodeRef, "it has no function type");
            }
            if (type->withTypesCount() != 0) {
                fail(camel::runtime::kInvalidNodeRef, "a partial application ('with' parameters)");
            }
            if (type->hasRest()) {
                fail(camel::runtime::kInvalidNodeRef, "a variadic parameter");
            }
            if (type->hasConstas()) {
                fail(camel::runtime::kInvalidNodeRef, "a 'constas' parameter");
            }
            // A function that reads a module value takes it as a `with` parameter. That is how
            // the source's top-level `let` reaches it, so those parameters are allowed; a
            // `with` parameter that is not a top-level name is a closure capture (S2).
            for (const gc_node_ref_t port : graph->withPorts()) {
                const camel::passes::gir::PortInfo info = camel::passes::gir::portInfo(graph, port);
                if (!topLevelNames_.contains(info.name)) {
                    fail(
                        port,
                        std::format(
                            "it captures '{}' from an enclosing function (a closure)", info.name));
                }
            }
            checkSignatureType(type);
            for (const SymId capture : graph->captureNames()) {
                const std::string name = std::string(camel::core::symbolText(capture));
                if (!topLevelNames_.contains(name)) {
                    fail(
                        camel::runtime::kInvalidNodeRef,
                        std::format(
                            "it captures '{}' from an enclosing function (a closure)", name));
                }
            }
        }
        if (graph->hasNodeAttrs() || graph->hasNodePermissions()) {
            fail(camel::runtime::kInvalidNodeRef, "it has node attributes or tags");
        }
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            const gc_node_ref_t ref           = it.ref();
            const camel::runtime::GCNode *node = *it;
            if (node->kind == camel::runtime::GCNodeKind::Dead) {
                continue;
            }
            if (graph->nodeRegion(ref) != 0) {
                fail(ref, "a node in a control region");
            }
            switch (node->kind) {
            case camel::runtime::GCNodeKind::Brch:
                fail(ref, "a branch");
            case camel::runtime::GCNodeKind::Join:
                fail(ref, "a branch join");
            case camel::runtime::GCNodeKind::Loop:
                fail(ref, "a loop");
            case camel::runtime::GCNodeKind::Bind:
                fail(ref, "a function bound to its 'with' arguments");
            case camel::runtime::GCNodeKind::Dref:
                fail(ref, "a read through a reference");
            default:
                break;
            }
        }
    }

    void checkSignatureType(const FunctionType *type) {
        for (size_t i = 0; i < type->withTypesCount(); ++i) {
            checkType(type->withTypeAt(i), "a 'with' parameter");
        }
        for (size_t i = 0; i < type->normTypesCount(); ++i) {
            checkType(type->normTypeAt(i), "a parameter");
        }
        if (type->hasExitType()) {
            checkType(type->exitType(), "the return type");
        }
    }

    void checkType(const Type *type, std::string_view where) {
        if (type == nullptr) {
            fail(camel::runtime::kInvalidNodeRef, std::format("{} has no type", where));
        }
        switch (type->code()) {
        case TypeCode::Int32:
        case TypeCode::Int64:
        case TypeCode::Float32:
        case TypeCode::Float64:
        case TypeCode::Bool:
        case TypeCode::Void:
        case TypeCode::String:
        case TypeCode::Complex64:
        case TypeCode::Complex128:
            return;
        case TypeCode::Array: {
            const auto *array = static_cast<const ArrayType *>(type);
            checkType(array->elemType(), "an array element");
            return;
        }
        case TypeCode::Tuple: {
            const auto *tuple = static_cast<const TupleType *>(type);
            if (tuple->isTagged() || tuple->isNullable() || tuple->isCell()) {
                fail(
                    camel::runtime::kInvalidNodeRef,
                    std::format("{} is a tagged, nullable or cell tuple", where));
            }
            for (size_t i = 0; i < tuple->size(); ++i) {
                checkType(tuple->typeAt(i), "a tuple element");
            }
            return;
        }
        case TypeCode::Struct: {
            const auto *record = static_cast<const StructType *>(type);
            if (record->named() || record->typeArgCount() != 0) {
                fail(
                    camel::runtime::kInvalidNodeRef,
                    std::format("{} is the named type '{}'", where, type->toString()));
            }
            for (size_t i = 0; i < record->size(); ++i) {
                checkType(record->typeAt(i), "a struct field");
            }
            return;
        }
        case TypeCode::Function: {
            const auto *function = static_cast<const FunctionType *>(type);
            if (function->withTypesCount() != 0 || function->hasRest() ||
                function->hasConstas()) {
                fail(
                    camel::runtime::kInvalidNodeRef,
                    std::format("{} is a partial or variadic function type", where));
            }
            for (size_t i = 0; i < function->normTypesCount(); ++i) {
                checkType(function->normTypeAt(i), "a function parameter");
            }
            if (function->hasExitType()) {
                checkType(function->exitType(), "a function return type");
            }
            return;
        }
        default:
            fail(
                camel::runtime::kInvalidNodeRef,
                std::format("{} has the type '{}', which S1 does not print", where, type->toString()));
        }
    }

    std::string typeText(const Type *type) {
        checkType(type, "a value");
        return type->toString();
    }

    void prepareGraph(GCGraph *graph) {
        params_.clear();
        declared_.clear();
        cells_.clear();
        usedNames_.clear();
        lines_.clear();
        generated_ = 0;
        topo_.clear();

        const FunctionType *type = graph->funcType();
        if (type != nullptr) {
            const size_t withCount = type->withTypesCount();
            for (size_t i = 0; i < type->normTypesCount(); ++i) {
                std::string name(type->argNameAt(withCount + i));
                if (!isIdentifier(name)) {
                    name = std::format("p{}", i);
                }
                if (!usedNames_.insert(name).second) {
                    name = std::format("p{}", i);
                    usedNames_.insert(name);
                }
                params_[graph->normPorts()[i]] = name;
            }
            const auto withPorts = graph->withPorts();
            for (const gc_node_ref_t port : withPorts) {
                const camel::passes::gir::PortInfo info = camel::passes::gir::portInfo(graph, port);
                if (!isIdentifier(info.name)) {
                    fail(port, "a captured module value without a name");
                }
                params_[port] = info.name;
            }
        }
        const auto captures = graph->captureNames();
        const auto closure = graph->closureNodes();
        for (size_t i = 0; i < captures.size() && i < closure.size(); ++i) {
            params_[closure[i]] = std::string(camel::core::symbolText(captures[i]));
        }

        const auto &planTopo = graph->execPlan().topo;
        buildTopo(planTopo);
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            if ((*it)->kind == camel::runtime::GCNodeKind::Dead) {
                continue;
            }
            const std::string name = sourceName(it.ref());
            if (!name.empty()) {
                usedNames_.insert(name);
            }
        }
    }

    // ---- order ---------------------------------------------------------------------------

    // The execution plan is a valid topological order, but it is free to put a pure node after
    // a later effect. The printer inlines some effectful calls into the expression that uses
    // them, so it needs the source's order among independent nodes. Node creation order is that
    // order: a stable topological sort, using it as the tie-break, keeps effects where the source
    // wrote them without breaking a dependency.
    void buildTopo(const std::vector<gc_node_ref_t> &plan) {
        std::vector<gc_node_ref_t> nodes;
        nodes.reserve(plan.size());
        for (const gc_node_ref_t ref : plan) {
            if (!skip_.contains(ref)) {
                nodes.push_back(ref);
            }
        }
        std::unordered_map<gc_node_ref_t, size_t> order;
        size_t ordinal = 0;
        for (auto it = graph_->nodes().begin(); it != graph_->nodes().end(); ++it) {
            order[it.ref()] = ordinal++;
        }
        const std::unordered_set<gc_node_ref_t> present(nodes.begin(), nodes.end());
        std::unordered_map<gc_node_ref_t, size_t> indegree;
        std::unordered_map<gc_node_ref_t, std::vector<gc_node_ref_t>> next;
        for (const gc_node_ref_t ref : nodes) {
            indegree[ref] = 0;
        }
        for (const gc_node_ref_t ref : nodes) {
            auto depend = [&](gc_node_ref_t input) {
                if (!present.contains(input)) {
                    return;
                }
                ++indegree[ref];
                next[input].push_back(ref);
            };
            for (const gc_node_ref_t input : graph_->normInputsOf(ref)) {
                depend(input);
            }
            for (const gc_node_ref_t input : graph_->withInputsOf(ref)) {
                depend(input);
            }
            for (const gc_node_ref_t input : graph_->ctrlInputsOf(ref)) {
                depend(input);
            }
        }
        auto later = [&](gc_node_ref_t a, gc_node_ref_t b) {
            return order.contains(a) && order.contains(b) ? order[a] > order[b]
                                                          : a > b;
        };
        std::priority_queue<
            gc_node_ref_t, std::vector<gc_node_ref_t>, decltype(later)>
            ready(later);
        for (const gc_node_ref_t ref : nodes) {
            if (indegree[ref] == 0) {
                ready.push(ref);
            }
        }
        topo_.clear();
        while (!ready.empty()) {
            const gc_node_ref_t ref = ready.top();
            ready.pop();
            topo_.push_back(ref);
            for (const gc_node_ref_t user : next[ref]) {
                if (--indegree[user] == 0) {
                    ready.push(user);
                }
            }
        }
        if (topo_.size() != nodes.size()) {
            // A cycle would be a broken graph; fall back to the plan order rather than dropping
            // nodes from the printed program.
            topo_ = std::move(nodes);
        }
    }

    // ---- needs ---------------------------------------------------------------------------

    bool isEffectful(const GCGraph *graph) {
        auto found = effectful_.find(graph);
        if (found != effectful_.end()) {
            return found->second;
        }
        // Effects can go through cycles (recursion), so the first answer is refined until the
        // set stops growing.
        bool changed = true;
        while (changed) {
            changed = false;
            for (GCGraph *candidate : graphs_) {
                if (effectful_[candidate]) {
                    continue;
                }
                bool effect = false;
                const FunctionType *type = candidate->funcType();
                if (type != nullptr) {
                    for (size_t i = 0; i < type->normTypesCount(); ++i) {
                        if (type->normIsVarAt(i)) {
                            effect = true;
                        }
                    }
                }
                if (!effect) {
                    for (const gc_node_ref_t ref : candidate->execPlan().topo) {
                        const camel::runtime::GCNode *node = candidate->node(ref);
                        if (node == nullptr) {
                            continue;
                        }
                        if (!candidate->ctrlInputsOf(ref).empty()) {
                            effect = true;
                            break;
                        }
                        if (node->kind == camel::runtime::GCNodeKind::Sync) {
                            effect = true;
                            break;
                        }
                        if (node->kind == camel::runtime::GCNodeKind::Oper) {
                            const std::string_view uri =
                                candidate->nodeBodyAs<camel::runtime::GCOperBody>(ref)->uri();
                            if (!isPureOper(uri)) {
                                effect = true;
                                break;
                            }
                        } else if (
                            node->kind == camel::runtime::GCNodeKind::Func ||
                            node->kind == camel::runtime::GCNodeKind::Call) {
                            GCGraph *callee = program_.calleeOf(*candidate, ref);
                            if (callee != nullptr && effectful_[callee]) {
                                effect = true;
                                break;
                            }
                        }
                    }
                }
                if (effect) {
                    effectful_[candidate] = true;
                    changed               = true;
                }
            }
        }
        return effectful_[graph];
    }

    static bool isPureOper(std::string_view uri) {
        if (uri.starts_with(":var/") || uri.starts_with(":op/assn")) {
            return false;
        }
        return camel::core::OperatorTable::instance().isPure(uri);
    }

    bool nodeEffect(gc_node_ref_t ref) {
        const camel::runtime::GCNode *node = graph_->node(ref);
        switch (node->kind) {
        case camel::runtime::GCNodeKind::Oper: {
            const std::string_view uri = graph_->nodeBodyAs<camel::runtime::GCOperBody>(ref)->uri();
            return !isPureOper(uri);
        }
        case camel::runtime::GCNodeKind::Func:
        case camel::runtime::GCNodeKind::Call: {
            GCGraph *callee = program_.calleeOf(*graph_, ref);
            return callee != nullptr && isEffectful(callee);
        }
        default:
            return false;
        }
    }

    size_t valueUsers(gc_node_ref_t ref) const {
        size_t count = 0;
        for (const gc_node_ref_t user : graph_->normOutputsOf(ref)) {
            if (!skip_.contains(user)) {
                ++count;
            }
        }
        for (const gc_node_ref_t user : graph_->withOutputsOf(ref)) {
            if (!skip_.contains(user)) {
                ++count;
            }
        }
        return count;
    }

    bool literalUsesDefault(const Type *type) const {
        switch (type->code()) {
        case TypeCode::Int32:
        case TypeCode::Float32:
        case TypeCode::Complex64:
        case TypeCode::Complex128:
            return false;
        case TypeCode::Array:
            return literalUsesDefault(static_cast<const ArrayType *>(type)->elemType());
        case TypeCode::Tuple: {
            const auto *tuple = static_cast<const TupleType *>(type);
            for (size_t i = 0; i < tuple->size(); ++i) {
                if (!literalUsesDefault(tuple->typeAt(i))) {
                    return false;
                }
            }
            return true;
        }
        case TypeCode::Struct: {
            const auto *record = static_cast<const StructType *>(type);
            for (size_t i = 0; i < record->size(); ++i) {
                if (!literalUsesDefault(record->typeAt(i))) {
                    return false;
                }
            }
            return true;
        }
        default:
            return true;
        }
    }

    bool needsAnnotation(gc_node_ref_t ref) const {
        const camel::runtime::GCNode *node = graph_->node(ref);
        if (node->kind != camel::runtime::GCNodeKind::Data &&
            node->kind != camel::runtime::GCNodeKind::Fill) {
            return false;
        }
        const Type *type = node->dataType();
        if (type == nullptr) {
            return false;
        }
        if (type->code() == TypeCode::Array && isConstant(ref)) {
            const auto *array = static_cast<const ArrayType *>(type);
            const size_t index = static_cast<size_t>(-node->dataIndex);
            const auto *value   = fromSlot<Array *>(graph_->staticArea()->get<slot_t>(index));
            if (value != nullptr && value->size() == 0) {
                return true;
            }
            (void)array;
        }
        return !literalUsesDefault(type);
    }

    bool isConstant(gc_node_ref_t ref) const {
        const camel::runtime::GCNode *node = graph_->node(ref);
        return node->kind == camel::runtime::GCNodeKind::Data && node->dataIndex < 0;
    }

    // ---- printing ------------------------------------------------------------------------

    void emit(std::string line) {
        lines_.push_back(std::move(line));
    }

    void appendLines() {
        for (const std::string &line : lines_) {
            out_ << line << '\n';
        }
        out_ << '\n';
        lines_.clear();
    }

    void printStatements() {
        gc_node_ref_t tail = camel::execute::resolveRuntimeTailValueRef(graph_);
        bool tailPrintedAsStatement = false;

        for (const gc_node_ref_t ref : topo_) {
            const camel::runtime::GCNode *node = graph_->node(ref);
            if (node == nullptr) {
                continue;
            }
            switch (node->kind) {
            case camel::runtime::GCNodeKind::Port:
            case camel::runtime::GCNodeKind::Gate:
            case camel::runtime::GCNodeKind::Sync:
            case camel::runtime::GCNodeKind::Copy:
                continue;
            case camel::runtime::GCNodeKind::Oper: {
                const std::string_view uri =
                    graph_->nodeBodyAs<camel::runtime::GCOperBody>(ref)->uri();
                if (uri == ":var/new") {
                    emitVariable(ref);
                    continue;
                }
                if (uri.starts_with(":var/")) {
                    continue;
                }
                if (uri.starts_with(":op/assn")) {
                    emitAssignment(ref, uri);
                    continue;
                }
                break;
            }
            default:
                break;
            }

            const bool named  = !sourceName(ref).empty();
            const bool effect = nodeEffect(ref);
            const size_t users = valueUsers(ref);

            // The initializer is read before the name is recorded: `expression(ref)` must not
            // answer with the name the declaration is about to give the node.
            if (named) {
                const std::string name = sourceName(ref);
                const Expr value       = expression(ref);
                declared_[ref]         = name;
                emit(std::format(
                    "{}let {}: {} = {}", indentation(), name, typeText(node->dataType()),
                    value.text));
            } else if (effect) {
                if (users == 0) {
                    emit(indentation() + expression(ref).text);
                    if (ref == tail) {
                        tailPrintedAsStatement = true;
                    }
                } else if (users > 1) {
                    const Expr value       = expression(ref);
                    const std::string name = freshName();
                    declared_[ref]         = name;
                    emit(std::format(
                        "{}let {}: {} = {}", indentation(), name, typeText(node->dataType()),
                        value.text));
                }
                // A single use keeps the call where the source had it: lifting it into a
                // `let` would move the effect ahead of the expression it belongs to.
            } else if (users > 1 || needsAnnotation(ref)) {
                const Expr value       = expression(ref);
                const std::string name = freshName();
                declared_[ref]         = name;
                emit(std::format(
                    "{}let {}: {} = {}", indentation(), name, typeText(node->dataType()),
                    value.text));
            } else if (users == 0) {
                // A value nothing reads is not part of the printed program.
                continue;
            }
        }

        if (!isRoot_ && tail != camel::runtime::kInvalidNodeRef && !tailPrintedAsStatement) {
            const Type *type = graph_->node(tail)->dataType();
            if (type != nullptr && type->code() != TypeCode::Void) {
                if (declared_.contains(tail)) {
                    emit(std::format("{}return {}", indentation(), declared_[tail]));
                } else {
                    emit(std::format("{}return {}", indentation(), expression(tail).text));
                }
            }
        }
    }

    std::string indentation() const { return isRoot_ ? "" : "    "; }

    void emitVariable(gc_node_ref_t ref) {
        if (isRoot_) {
            fail(ref, "a top-level 'var'");
        }
        const auto inputs = graph_->normInputsOf(ref);
        if (inputs.empty()) {
            fail(ref, "'var' without an initial value");
        }
        const camel::runtime::GCNode *node = graph_->node(ref);
        const Type *type                   = node->dataType();
        if (type == nullptr || type->code() != TypeCode::Tuple ||
            !static_cast<const TupleType *>(type)->isCell()) {
            fail(ref, "'var' without a cell type");
        }
        const Type *valueType = static_cast<const TupleType *>(type)->typeAt(0);
        if (valueType == nullptr) {
            fail(ref, "'var' cell without a value type");
        }
        std::string name = sourceName(ref);
        if (name.empty()) {
            name = freshName();
        }
        const Expr initial = expression(inputs.front());
        cells_[ref]        = name;
        declared_[ref]     = name;
        emit(std::format(
            "{}var {}: {} = {}", indentation(), name, typeText(valueType), initial.text));
    }

    void emitAssignment(gc_node_ref_t ref, std::string_view uri) {
        if (isRoot_) {
            fail(ref, "a top-level assignment");
        }
        const auto inputs = graph_->normInputsOf(ref);
        if (inputs.size() != 2) {
            fail(ref, "an assignment without a cell and a value");
        }
        auto cell = cells_.find(inputs.front());
        if (cell == cells_.end()) {
            if (params_.contains(inputs.front())) {
                fail(ref, "an assignment to a `var` parameter");
            }
            fail(ref, "an assignment to a cell the printer has not seen");
        }
        emit(indentation() +
             std::format(
                 "{} {} {}", cell->second, assignmentOperator(uri),
                 expression(inputs.back()).text));
    }

    // The assignment operator is a node of its own: `a += 5` and `a = 5` have the same shape
    // but different payloads, so the spelling must carry the operation.
    static std::string_view assignmentOperator(std::string_view uri) {
        constexpr std::string_view prefix = ":op/assn";
        if (!uri.starts_with(prefix)) {
            return "=";
        }
        std::string_view suffix = uri.substr(prefix.size());
        const size_t version     = suffix.find('@');
        if (version != std::string_view::npos) {
            suffix = suffix.substr(0, version);
        }
        if (suffix.empty() || suffix == "_l" || suffix == "_d" || suffix == "_i" ||
            suffix == "_f" || suffix == "_b") {
            return "=";
        }
        if (!suffix.starts_with('_')) {
            return "=";
        }
        suffix.remove_prefix(1);
        const size_t typeSuffix = suffix.rfind('_');
        if (typeSuffix != std::string_view::npos) {
            suffix = suffix.substr(0, typeSuffix);
        }
        if (suffix == "add") {
            return "+=";
        }
        if (suffix == "sub") {
            return "-=";
        }
        if (suffix == "mul") {
            return "*=";
        }
        if (suffix == "div") {
            return "/=";
        }
        if (suffix == "mod") {
            return "%=";
        }
        if (suffix == "pow") {
            return "^=";
        }
        if (suffix == "and") {
            return "&=";
        }
        if (suffix == "or") {
            return "|=";
        }
        return "=";
    }

    std::string cellName(gc_node_ref_t ref) {
        auto found = cells_.find(ref);
        if (found != cells_.end()) {
            return found->second;
        }
        const std::string name = freshName();
        cells_[ref]            = name;
        return name;
    }

    // ---- expressions ---------------------------------------------------------------------

    Expr expression(gc_node_ref_t ref) {
        auto declared = declared_.find(ref);
        if (declared != declared_.end()) {
            return {declared->second, 100};
        }
        const camel::runtime::GCNode *node = graph_->node(ref);
        if (node == nullptr) {
            fail(ref, "a missing node");
        }
        switch (node->kind) {
        case camel::runtime::GCNodeKind::Data:
            return dataExpression(ref);
        case camel::runtime::GCNodeKind::Port: {
            auto found = params_.find(ref);
            if (found == params_.end()) {
                fail(ref, "a port without a source name");
            }
            return {found->second, 100};
        }
        case camel::runtime::GCNodeKind::Copy:
        case camel::runtime::GCNodeKind::Gate:
        case camel::runtime::GCNodeKind::Sync: {
            const auto inputs = graph_->normInputsOf(ref);
            if (inputs.empty()) {
                return {"()", 100};
            }
            return expression(inputs.back());
        }
        case camel::runtime::GCNodeKind::Cast: {
            const auto inputs = graph_->normInputsOf(ref);
            if (inputs.size() != 1 || node->dataType() == nullptr) {
                fail(ref, "a cast without an input or a type");
            }
            Expr inner = expression(inputs.front());
            return {
                std::format("({} as {})", parenthesize(inner, 80), typeText(node->dataType())),
                80};
        }
        case camel::runtime::GCNodeKind::Accs:
            return accessExpression(ref);
        case camel::runtime::GCNodeKind::Fill:
            return fillExpression(ref);
        case camel::runtime::GCNodeKind::Oper:
            return operatorExpression(ref);
        case camel::runtime::GCNodeKind::Func:
        case camel::runtime::GCNodeKind::Call:
            return callExpression(ref);
        case camel::runtime::GCNodeKind::Brch:
            fail(ref, "a branch");
        case camel::runtime::GCNodeKind::Join:
            fail(ref, "a branch join");
        case camel::runtime::GCNodeKind::Loop:
            fail(ref, "a loop");
        case camel::runtime::GCNodeKind::Bind:
            fail(ref, "a function bound to its 'with' arguments");
        case camel::runtime::GCNodeKind::Dref:
            fail(ref, "a read through a reference");
        case camel::runtime::GCNodeKind::Dead:
            fail(ref, "a dead node");
        }
        fail(ref, "an unknown node kind");
        return {};
    }

    Expr dataExpression(gc_node_ref_t ref) {
        const camel::runtime::GCNode *node = graph_->node(ref);
        if (node->dataIndex >= 0) {
            fail(ref, "a value read from the frame");
        }
        const Type *type = node->dataType();
        if (type == nullptr) {
            fail(ref, "a constant without a type");
        }
        const size_t index = static_cast<size_t>(-node->dataIndex);
        if (index >= graph_->staticArea()->size()) {
            fail(ref, "a constant outside the static area");
        }
        if (type->code() == TypeCode::Function) {
            const auto *function = fromSlot<Function *>(graph_->staticArea()->get<slot_t>(index));
            if (function == nullptr) {
                fail(ref, "a function constant without a function");
            }
            const std::string name = functionNameOf(function->graph());
            if (name.empty()) {
                fail(ref, "a function value whose graph has no source name (a lambda)");
            }
            return {name, 100};
        }
        const slot_t slot = graph_->staticArea()->get<slot_t>(index);
        return {constantText(slot, type), 100};
    }

    Expr accessExpression(gc_node_ref_t ref) {
        const auto *body = graph_->nodeBodyAs<camel::runtime::GCAccsBody>(ref);
        const auto inputs = graph_->normInputsOf(ref);
        if (inputs.size() != 1) {
            fail(ref, "an access without one value");
        }
        Expr base = expression(inputs.front());
        const std::string member =
            body->accsKind == camel::runtime::GCAccsKind::StructKey
                ? std::string(body->key())
                : std::to_string(body->value);
        if (member.empty()) {
            fail(ref, "an access without a member");
        }
        return {std::format("{}.{}", parenthesize(base, 90), member), 90};
    }

    Expr fillExpression(gc_node_ref_t ref) {
        const auto *body   = graph_->nodeBodyAs<camel::runtime::GCFillBody>(ref);
        const auto inputs  = graph_->normInputsOf(ref);
        const auto with    = graph_->withInputsOf(ref);
        const Type *type   = graph_->node(ref)->dataType();
        if (inputs.empty() || type == nullptr) {
            fail(ref, "a construction without a template");
        }
        if (body->fillKind == camel::runtime::GCFillKind::FunctionClosure) {
            const camel::runtime::GCNode *templ = graph_->node(inputs.front());
            if (templ == nullptr || templ->kind != camel::runtime::GCNodeKind::Data ||
                templ->dataIndex >= 0 || templ->dataType() == nullptr ||
                templ->dataType()->code() != TypeCode::Function) {
                fail(ref, "a closure whose function is not a constant");
            }
            const auto *function =
                fromSlot<Function *>(graph_->staticArea()->get<slot_t>(-templ->dataIndex));
            const std::string name =
                function != nullptr ? functionNameOf(function->graph()) : std::string{};
            if (name.empty()) {
                fail(ref, "a closure of an anonymous function (a lambda)");
            }
            return {name, 100};
        }

        std::vector<std::string> items;
        const camel::runtime::GCNode *templ = graph_->node(inputs.front());
        if (templ == nullptr || templ->dataIndex >= 0) {
            fail(ref, "a construction whose template is not a constant");
        }
        const slot_t slot = graph_->staticArea()->get<slot_t>(-templ->dataIndex);
        const Type *elementType = nullptr;
        size_t count            = 0;
        if (type->code() == TypeCode::Array) {
            const auto *arrayType = static_cast<const ArrayType *>(type);
            elementType           = arrayType->elemType();
            const auto *value     = fromSlot<Array *>(slot);
            if (value == nullptr) {
                fail(ref, "an array construction without a template value");
            }
            count = value->size();
        } else if (type->code() == TypeCode::Tuple) {
            const auto *tupleType = static_cast<const TupleType *>(type);
            count                 = tupleType->size();
        } else if (type->code() == TypeCode::Struct) {
            const auto *record = static_cast<const StructType *>(type);
            count              = record->size();
        } else {
            fail(ref, "a construction of a type S1 does not build");
        }
        items.resize(count);
        std::vector<bool> filled(count, false);
        const auto slots = body->slots();
        for (size_t i = 0; i < with.size() && i < slots.size(); ++i) {
            const size_t at = static_cast<size_t>(slots[i]);
            if (at >= count) {
                fail(ref, "a construction slot outside its type");
            }
            items[at]  = expression(with[i]).text;
            filled[at] = true;
        }
        for (size_t i = 0; i < count; ++i) {
            if (filled[i]) {
                continue;
            }
            slot_t element = 0;
            if (type->code() == TypeCode::Array) {
                element = fromSlot<Array *>(slot)->get<slot_t>(i);
            } else if (type->code() == TypeCode::Tuple) {
                element = fromSlot<Tuple *>(slot)->get<slot_t>(i);
            } else {
                element = fromSlot<Struct *>(slot)->get<slot_t>(i);
            }
            const Type *fieldType = type->code() == TypeCode::Array
                                        ? elementType
                                        : type->code() == TypeCode::Tuple
                                              ? static_cast<const TupleType *>(type)->typeAt(i)
                                              : static_cast<const StructType *>(type)->typeAt(i);
            items[i] = constantText(element, fieldType);
        }
        if (type->code() == TypeCode::Array) {
            std::string text = "[";
            for (size_t i = 0; i < items.size(); ++i) {
                text += (i ? ", " : "") + items[i];
            }
            text += "]";
            return {text, 100};
        }
        if (type->code() == TypeCode::Tuple) {
            std::string text = "(";
            for (size_t i = 0; i < items.size(); ++i) {
                text += (i ? ", " : "") + items[i];
            }
            if (items.size() == 1) {
                text += ",";
            }
            text += ")";
            return {text, 100};
        }
        const auto *record = static_cast<const StructType *>(type);
        std::string text   = "{ ";
        for (size_t i = 0; i < items.size(); ++i) {
            text += (i ? ", " : "") + std::string(record->fieldName(i)) + ": " + items[i];
        }
        text += " }";
        return {text, 100};
    }

    Expr operatorExpression(gc_node_ref_t ref) {
        const auto *body   = graph_->nodeBodyAs<camel::runtime::GCOperBody>(ref);
        const std::string_view uri = body->uri();
        const auto inputs  = graph_->normInputsOf(ref);
        if (uri == ":var/get" || uri == ":var/peek") {
            if (inputs.size() != 1) {
                fail(ref, "a variable read without a cell");
            }
            return {cellName(inputs.front()), 100};
        }
        if (uri.starts_with(":var/")) {
            fail(ref, std::format("the operator '{}'", uri));
        }
        if (uri == ":op/idx_arr" || uri == ":op/idx_str" || uri == ":op/idx") {
            if (inputs.size() != 2) {
                fail(ref, "an index without a value and an index");
            }
            Expr base  = expression(inputs[0]);
            Expr index = expression(inputs[1]);
            return {
                std::format("{}[{}]", parenthesize(base, 90), index.text),
                90};
        }

        std::string name;
        if (uri.starts_with(":io/")) {
            name = std::string(uri.substr(4));
        } else if (uri.starts_with(":op/")) {
            const std::string_view op = uri.substr(4);
            const auto isSide = [](char c) {
                return c == 'i' || c == 'l' || c == 'f' || c == 'd' || c == 'c' || c == 'z' ||
                       c == 's';
            };
            if (op.size() == 4 && op[1] == 't' && op[2] == 'o' && isSide(op[0]) &&
                isSide(op[3])) {
                switch (op[3]) {
                case 'i':
                    name = "i32";
                    break;
                case 'l':
                    name = "int";
                    break;
                case 'f':
                    name = "f32";
                    break;
                case 'd':
                    name = "float";
                    break;
                case 'c':
                    name = "c64";
                    break;
                case 'z':
                    name = "c128";
                    break;
                case 's':
                    name = "string";
                    break;
                default:
                    break;
                }
            } else {
                std::string_view base = op;
                if (base.size() > 2 && base[base.size() - 2] == '_' &&
                    isSide(base.back())) {
                    base.remove_suffix(2);
                }
                if (base == "add") {
                    return infix(ref, inputs, "+", 40);
                }
                if (base == "sub") {
                    return infix(ref, inputs, "-", 40);
                }
                if (base == "mul") {
                    return infix(ref, inputs, "*", 50);
                }
                if (base == "div") {
                    return infix(ref, inputs, "/", 50);
                }
                if (base == "mod") {
                    return infix(ref, inputs, "%", 50);
                }
                if (base == "pow") {
                    return infix(ref, inputs, "^", 60);
                }
                if (base == "eq") {
                    return infix(ref, inputs, "==", 30);
                }
                if (base == "ne") {
                    return infix(ref, inputs, "!=", 30);
                }
                if (base == "lt") {
                    return infix(ref, inputs, "<", 30);
                }
                if (base == "le") {
                    return infix(ref, inputs, "<=", 30);
                }
                if (base == "gt") {
                    return infix(ref, inputs, ">", 30);
                }
                if (base == "ge") {
                    return infix(ref, inputs, ">=", 30);
                }
                if (base == "and") {
                    return infix(ref, inputs, "&", 20);
                }
                if (base == "or") {
                    return infix(ref, inputs, "|", 19);
                }
                if (base == "neg") {
                    return prefix(ref, inputs, "-");
                }
                if (base == "inv") {
                    return prefix(ref, inputs, "~");
                }
                if (base == "not") {
                    return prefix(ref, inputs, "!");
                }
                fail(ref, std::format("the operator '{}'", uri));
            }
        } else {
            fail(ref, std::format("the operator '{}'", uri));
        }

        std::string text = name + "(";
        for (size_t i = 0; i < inputs.size(); ++i) {
            text += (i ? ", " : "") + expression(inputs[i]).text;
        }
        text += ")";
        return {text, 90};
    }

    Expr infix(
        gc_node_ref_t ref, std::span<const gc_node_ref_t> inputs, std::string_view op,
        int precedence) {
        if (inputs.size() != 2) {
            fail(ref, std::format("the operator '{}' without two values", op));
        }
        Expr left  = expression(inputs[0]);
        Expr right = expression(inputs[1]);
        return {
            std::format(
                "{} {} {}", parenthesize(left, precedence), op,
                parenthesize(right, precedence + 1)),
            precedence};
    }

    Expr prefix(gc_node_ref_t ref, std::span<const gc_node_ref_t> inputs, std::string_view op) {
        if (inputs.size() != 1) {
            fail(ref, std::format("the operator '{}' without one value", op));
        }
        Expr inner = expression(inputs[0]);
        return {std::format("{}{}", op, parenthesize(inner, 70)), 70};
    }

    Expr callExpression(gc_node_ref_t ref) {
        GCGraph *callee = program_.calleeOf(*graph_, ref);
        const camel::runtime::GCNode *node = graph_->node(ref);
        if (node->kind == camel::runtime::GCNodeKind::Call) {
            const auto with = graph_->withInputsOf(ref);
            if (with.size() != 1) {
                fail(ref, "an indirect call without one function value");
            }
            Expr function = expression(with.front());
            return callText(ref, function, graph_->normInputsOf(ref));
        }
        if (callee == nullptr) {
            fail(ref, "a call without a callee");
        }
        // A direct call of a function that reads module values passes those values as `with`
        // inputs. The source omits them: the callee names the top-level values itself.
        const std::string name = functionNameOf(callee);
        if (name.empty()) {
            fail(ref, "a call of a function whose graph has no source name (a lambda)");
        }
        return callText(ref, {name, 100}, graph_->normInputsOf(ref));
    }

    Expr callText(
        gc_node_ref_t ref, const Expr &callee, std::span<const gc_node_ref_t> inputs) {
        std::string text = parenthesize(callee, 90) + "(";
        for (size_t i = 0; i < inputs.size(); ++i) {
            text += (i ? ", " : "") + expression(inputs[i]).text;
        }
        text += ")";
        (void)ref;
        return {text, 90};
    }

    // ---- constants -----------------------------------------------------------------------

    /// Every static constant the printed program would write is measured first: a program with
    /// one over the limit is refused as a whole and every oversized constant is listed, not just
    /// the first the printer would meet.
    void scanBigConstants() {
        for (GCGraph *graph : graphs_) {
            for (const gc_node_ref_t ref : graph->execPlan().topo) {
                const camel::runtime::GCNode *node = graph->node(ref);
                if (node == nullptr || node->kind != camel::runtime::GCNodeKind::Data ||
                    node->dataIndex >= 0 || node->dataType() == nullptr ||
                    node->dataType()->code() == TypeCode::Function) {
                    continue;
                }
                const size_t index = static_cast<size_t>(-node->dataIndex);
                const slot_t slot  = graph->staticArea()->get<slot_t>(index);
                const size_t bytes = constantBytes(slot, node->dataType());
                if (bytes <= maxLiteralBytes_) {
                    continue;
                }
                std::string name = sourceNameIt(graph, ref);
                if (name.empty()) {
                    name = "<unnamed>";
                }
                bigConstants_.push_back(
                    {graph->nodeOrigin(ref), name, node->dataType()->toString(), graph->name(),
                     bytes});
            }
        }
    }

    size_t constantBytes(slot_t slot, const Type *type) {
        switch (type->code()) {
        case TypeCode::String: {
            const auto *text = fromSlot<String *>(slot);
            return text != nullptr ? text->view().size() : 0;
        }
        case TypeCode::Array: {
            const auto *value = fromSlot<Array *>(slot);
            if (value == nullptr) {
                return 0;
            }
            size_t bytes = value->size() * sizeof(slot_t);
            const Type *element = static_cast<const ArrayType *>(type)->elemType();
            for (size_t i = 0; i < value->size(); ++i) {
                bytes += constantBytes(value->get<slot_t>(i), element);
            }
            return bytes;
        }
        case TypeCode::Tuple: {
            const auto *value    = fromSlot<Tuple *>(slot);
            const auto *tupleType = static_cast<const TupleType *>(type);
            if (value == nullptr) {
                return 0;
            }
            size_t bytes = tupleType->size() * sizeof(slot_t);
            for (size_t i = 0; i < tupleType->size(); ++i) {
                bytes += constantBytes(value->get<slot_t>(i), tupleType->typeAt(i));
            }
            return bytes;
        }
        case TypeCode::Struct: {
            const auto *value = fromSlot<Struct *>(slot);
            const auto *record = static_cast<const StructType *>(type);
            if (value == nullptr) {
                return 0;
            }
            size_t bytes = record->size() * sizeof(slot_t);
            for (size_t i = 0; i < record->size(); ++i) {
                bytes += constantBytes(value->get<slot_t>(i), record->typeAt(i));
            }
            return bytes;
        }
        case TypeCode::Complex64:
        case TypeCode::Complex128:
            return 16;
        default:
            return 8;
        }
    }

    std::string constantText(slot_t slot, const Type *type) {
        switch (type->code()) {
        case TypeCode::Int32:
            return std::to_string(fromSlot<int32_t>(slot));
        case TypeCode::Int64:
            return std::to_string(fromSlot<int64_t>(slot));
        case TypeCode::Float32: {
            const float value = fromSlot<float>(slot);
            if (!std::isfinite(value)) {
                fail(camel::runtime::kInvalidNodeRef, "an infinite or NaN constant");
            }
            return camel::utils::floatText(value);
        }
        case TypeCode::Float64: {
            const double value = fromSlot<double>(slot);
            if (!std::isfinite(value)) {
                fail(camel::runtime::kInvalidNodeRef, "an infinite or NaN constant");
            }
            return camel::utils::floatText(value);
        }
        case TypeCode::Bool:
            return fromSlot<bool>(slot) ? "true" : "false";
        case TypeCode::Void:
            return "()";
        case TypeCode::String: {
            const auto *text = fromSlot<String *>(slot);
            if (text == nullptr) {
                fail(camel::runtime::kInvalidNodeRef, "a null string constant");
            }
            return escapeString(text->view(), isUtf8(text->view()));
        }
        case TypeCode::Complex64: {
            const auto *value = fromSlot<camel::core::rtdata::Complex *>(slot);
            return complexText(value != nullptr ? value->realF() : 0.0F,
                               value != nullptr ? value->imaginaryF() : 0.0F);
        }
        case TypeCode::Complex128: {
            const auto *value = fromSlot<camel::core::rtdata::Complex *>(slot);
            return complexText(
                value != nullptr ? value->real() : 0.0,
                value != nullptr ? value->imaginary() : 0.0);
        }
        case TypeCode::Array: {
            const auto *value = fromSlot<Array *>(slot);
            const auto *arrayType = static_cast<const ArrayType *>(type);
            std::string text = "[";
            for (size_t i = 0; value != nullptr && i < value->size(); ++i) {
                text += (i ? ", " : "") + constantText(value->get<slot_t>(i), arrayType->elemType());
            }
            text += "]";
            return text;
        }
        case TypeCode::Tuple: {
            const auto *value = fromSlot<Tuple *>(slot);
            const auto *tupleType = static_cast<const TupleType *>(type);
            std::string text = "(";
            for (size_t i = 0; value != nullptr && i < tupleType->size(); ++i) {
                text += (i ? ", " : "") + constantText(value->get<slot_t>(i), tupleType->typeAt(i));
            }
            if (tupleType->size() == 1) {
                text += ",";
            }
            text += ")";
            return text;
        }
        case TypeCode::Struct: {
            const auto *value  = fromSlot<Struct *>(slot);
            const auto *record = static_cast<const StructType *>(type);
            std::string text   = "{ ";
            for (size_t i = 0; value != nullptr && i < record->size(); ++i) {
                text += (i ? ", " : "") + std::string(record->fieldName(i)) + ": " +
                        constantText(value->get<slot_t>(i), record->typeAt(i));
            }
            text += " }";
            return text;
        }
        case TypeCode::Function:
            fail(camel::runtime::kInvalidNodeRef, "a function constant in a nested position");
        default:
            fail(
                camel::runtime::kInvalidNodeRef,
                std::format("a constant of type '{}'", type->toString()));
        }
    }

    std::string complexText(float real, float imaginary) {
        if (!std::isfinite(real) || !std::isfinite(imaginary)) {
            fail(camel::runtime::kInvalidNodeRef, "an infinite or NaN complex constant");
        }
        if (real == 0.0F) {
            return camel::utils::floatText(imaginary) + "i";
        }
        return camel::utils::complexText(real, imaginary);
    }

    std::string complexText(double real, double imaginary) {
        if (!std::isfinite(real) || !std::isfinite(imaginary)) {
            fail(camel::runtime::kInvalidNodeRef, "an infinite or NaN complex constant");
        }
        if (real == 0.0) {
            return camel::utils::floatText(imaginary) + "i";
        }
        return camel::utils::complexText(real, imaginary);
    }

    // ---- functions -----------------------------------------------------------------------

    void printFunction(GCGraph *graph) {
        graph_  = graph;
        isRoot_ = false;
        skip_.clear();
        prepareGraph(graph);

        const FunctionType *type = graph->funcType();
        const std::string name   = graph->name();
        const bool syncType      = type->hasModifier(Modifier::Sync);
        std::string signature    = syncType ? "sync func " + name + "(" : "func " + name + "(";
        for (size_t i = 0; i < type->normTypesCount(); ++i) {
            signature += i ? ", " : "";
            if (type->normIsVarAt(i)) {
                signature += "var ";
            }
            std::string param(type->argNameAt(type->withTypesCount() + i));
            if (!isIdentifier(param)) {
                param = std::format("p{}", i);
            }
            signature += param + ": " + typeText(type->normTypeAt(i));
        }
        signature += ")";
        if (type->hasExitType()) {
            signature += ": " + typeText(type->exitType());
        }
        // `sync` is a modifier of the function type: a function declared `sync func` keeps it even
        // when its body has no control edge. Only a body that is itself a sync block (type without
        // the modifier) takes the suffix spelling.
        if (!syncType && hasControl(graph)) {
            signature += " sync";
        }
        emit(signature + " {");
        isRoot_ = false;
        printStatements();
        emit("}");
        appendLines();
    }

    static bool hasControl(const GCGraph *graph) {
        for (const gc_node_ref_t ref : graph->execPlan().topo) {
            if (!graph->ctrlInputsOf(ref).empty()) {
                return true;
            }
        }
        return false;
    }

    // ---- errors --------------------------------------------------------------------------

    [[noreturn]] void fail(gc_node_ref_t ref, std::string reason) {
        const uint64_t origin =
            ref != camel::runtime::kInvalidNodeRef ? graph_->nodeOrigin(ref) : 0;
        throw Refusal(
            ref, origin, graph_ != nullptr ? graph_->name() : std::string("<program>"),
            std::move(reason));
    }
};

} // namespace

PassDescriptor cmlDescriptor() {
    PassDescriptor descriptor;
    descriptor.doc = "print the program as Camel source (S1: straight-line code)";
    descriptor.params.push_back(
        PassParam{
            "max_literal_bytes",
            PassParamType::Int,
            int64_t(64 * 1024),
            "the largest constant the printer writes as a literal",
        });
    descriptor.make = [](const camel::core::context::context_ptr_t &ctx, const PassArgs &args) {
        CmlOptions options;
        const int64_t limit      = args.getInt("max_literal_bytes");
        options.maxLiteralBytes  = limit > 0 ? static_cast<size_t>(limit) : 0;
        return std::make_unique<CmlDumpPass>(ctx, options);
    };
    return descriptor;
}

const camel::runtime::Program *CmlDumpPass::apply(
    const camel::runtime::Program *program, std::ostream &out) {
    if (program == nullptr) {
        return nullptr;
    }
    std::vector<BigConstant> bigConstants;
    std::ostringstream rendered;
    try {
        CmlPrinter printer(*program, rendered, options_.maxLiteralBytes, bigConstants);
        printer.run();
    } catch (const Refusal &refusal) {
        auto diagnostic = context_->rtmDiags()->of(
            camel::core::error::SemanticDiag::CmlUnsupportedConstruct);
        if (refusal.origin != 0) {
            diagnostic.atOrigin(refusal.origin);
        }
        diagnostic.commit(refusal.graph, refusal.reason);
        return nullptr;
    }
    if (!bigConstants.empty()) {
        for (const BigConstant &constant : bigConstants) {
            auto diagnostic =
                context_->rtmDiags()->of(camel::core::error::SemanticDiag::CmlBigConstant);
            if (constant.origin != 0) {
                diagnostic.atOrigin(constant.origin);
            }
            diagnostic.commit(
                constant.name, constant.type, constant.graph, constant.bytes,
                options_.maxLiteralBytes);
        }
        return nullptr;
    }
    // Nothing is written before the whole program has been printed: a refusal must not leave
    // half a program on the stream.
    out << rendered.str();
    return nullptr;
}