// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// `std::cml`: print the runtime program as Camel source. S1 covered the straight-line subset:
// module values, functions, literals, accesses, casts, operators, direct calls, `let`/`var`,
// assignment, `return` and straight-line `sync`. S2 adds branches (`if`, `match`, the join that
// gives their value), loops (`for`, `while`, `for ... while`, their state and their regions),
// closures (a lambda as a value, its captures, a body that is a sync block), and `with` and
// `var` parameters. A construct outside that subset is refused by name instead of being printed
// wrongly.

#include "camel/passes/trans/cml/cml.h"

#include "camel/core/error/diagnostics/messages/semantic.h"
#include "camel/core/loop_kind.h"
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
#include "camel/runtime/control_regions.h"
#include "camel/runtime/effects.h"
#include "camel/runtime/exec_plan.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/loop.h"
#include "camel/runtime/reachable.h"
#include "camel/utils/number_text.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <format>
#include <memory>
#include <queue>
#include <sstream>
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

/// The compiler names a lambda's graph `__g_<graph id>__`, which is no source name: the printer
/// writes the lambda where its value is used instead of declaring a function.
bool isLambdaName(std::string_view name) {
    constexpr std::string_view prefix = "__g_";
    constexpr std::string_view suffix = "__";
    if (!name.starts_with(prefix) || !name.ends_with(suffix) ||
        name.size() <= prefix.size() + suffix.size()) {
        return false;
    }
    const std::string_view id = name.substr(prefix.size(), name.size() - prefix.size() - 2);
    return std::all_of(id.begin(), id.end(), [](char c) { return c >= '0' && c <= '9'; });
}

std::string_view regionKindText(camel::runtime::GCRegionKind kind) {
    switch (kind) {
    case camel::runtime::GCRegionKind::Root:
        return "root";
    case camel::runtime::GCRegionKind::Arm:
        return "arm";
    case camel::runtime::GCRegionKind::LoopCond:
        return "loop condition";
    case camel::runtime::GCRegionKind::LoopBody:
        return "loop body";
    case camel::runtime::GCRegionKind::Inline:
        return "inline";
    case camel::runtime::GCRegionKind::Block:
        return "block";
    case camel::runtime::GCRegionKind::Dead:
        return "dead";
    }
    return "unknown";
}

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

/// The precedence of the interval `..` (design principles 4.2): tighter than every operator the
/// printer writes except the postfix forms, so an operand that is an operator, a prefix or an
/// `as` is parenthesised and a name, a literal or a postfix chain is not. Only the interval
/// operator and the sequence of a fused loop are written with it.
constexpr int kIntervalPrecedence = 85;

std::string parenthesize(const Expr &value, int minimum) {
    return value.precedence < minimum ? "(" + value.text + ")" : value.text;
}

/// A bound of the interval `a..b`: `parenthesize`, and a value whose text begins with a minus sign
/// is parenthesised too. A negative constant is an atom to `parenthesize`, but the unary minus
/// binds *looser* than `..` (design principles 4.2, the language reference's precedence table), so
/// `-2..3` reads as `-(2..3)`: the compiler says so, `'-'` having no `int[]` reading. The interval
/// is the one form the printer writes as the operand of an operator that binds tighter than a
/// unary.
std::string intervalOperand(const Expr &value, int minimum) {
    const std::string text = parenthesize(value, minimum);
    if (!text.empty() && text.front() == '-') {
        return "(" + text + ")";
    }
    return text;
}

/// A bound of an interval the graph holds as a constant: the integer itself, parenthesised when it
/// is negative for the reason `intervalOperand` gives.
std::string intervalConstantOperand(int64_t value) {
    const std::string text = std::to_string(value);
    return value < 0 ? "(" + text + ")" : text;
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
        classifyLambdas();
        collectTopLevelNames(root);
        findEntry(root);
        for (GCGraph *graph : graphs_) {
            validateGraph(graph, graph == root);
        }
        scanBigConstants();
        if (!bigConstants_.empty()) {
            return;
        }

        graph_    = root;
        isRoot_   = true;
        indent_   = "";
        skip_     = entrySkip_;
        prepareGraph(root);
        if (!topo_.empty()) {
            printStatements();
            if (!lines_.empty()) {
                appendLines();
            }
        }

        std::vector<GCGraph *> functions;
        for (GCGraph *graph : graphs_) {
            if (graph != root && !lambdaGraphs_.contains(graph)) {
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

    // A lambda is printed where its value is made, not declared: its graph, the single function
    // constant that names it, and how many closures are built around that constant.
    std::unordered_set<const GCGraph *> lambdaGraphs_;
    std::unordered_map<const GCGraph *, gc_node_ref_t> lambdaSite_;
    std::unordered_map<const GCGraph *, size_t> lambdaDataCount_;
    std::unordered_map<const GCGraph *, size_t> lambdaClosureCount_;

    GCGraph *graph_ = nullptr;
    bool isRoot_    = false;
    std::unordered_set<gc_node_ref_t> skip_;
    std::vector<gc_node_ref_t> topo_;
    std::unordered_map<gc_node_ref_t, std::string> params_;
    std::unordered_map<gc_node_ref_t, std::string> declared_;
    std::unordered_map<gc_node_ref_t, std::string> cells_;
    std::unordered_set<std::string> usedNames_;
    std::vector<std::string> lines_;
    size_t generated_          = 0;
    std::string indent_        = "    ";
    bool branchAsStatement_    = false;
    std::unique_ptr<camel::runtime::ControlRegions> control_;

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

    /// Whether a function constant refers to a lambda graph, and how many such constants and
    /// closures exist. One lambda value is printed where it is made; a second one (two function
    /// constants, or two closures around one) would duplicate a source function, so it is refused.
    Function *functionConstantOf(GCGraph *graph, gc_node_ref_t ref) const {
        const camel::runtime::GCNode *node = graph->node(ref);
        if (node == nullptr || node->kind != camel::runtime::GCNodeKind::Data ||
            node->dataIndex >= 0 || node->dataType() == nullptr ||
            node->dataType()->code() != TypeCode::Function) {
            return nullptr;
        }
        return fromSlot<Function *>(graph->staticArea()->get<slot_t>(-node->dataIndex));
    }

    void classifyLambdas() {
        for (GCGraph *graph : graphs_) {
            if (isLambdaName(graph->name())) {
                lambdaGraphs_.insert(graph);
            }
        }
        for (GCGraph *graph : graphs_) {
            for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
                const camel::runtime::GCNode *node = *it;
                if (node->kind == camel::runtime::GCNodeKind::Data) {
                    Function *function = functionConstantOf(graph, it.ref());
                    if (function != nullptr && function->graph() != nullptr &&
                        lambdaGraphs_.contains(function->graph())) {
                        ++lambdaDataCount_[function->graph()];
                        lambdaSite_.try_emplace(function->graph(), it.ref());
                    }
                } else if (node->kind == camel::runtime::GCNodeKind::Fill) {
                    const auto *body = graph->nodeBodyAs<camel::runtime::GCFillBody>(it.ref());
                    if (body->fillKind != camel::runtime::GCFillKind::FunctionClosure) {
                        continue;
                    }
                    const auto inputs = graph->normInputsOf(it.ref());
                    if (inputs.empty()) {
                        continue;
                    }
                    Function *function = functionConstantOf(graph, inputs.front());
                    if (function != nullptr && function->graph() != nullptr &&
                        lambdaGraphs_.contains(function->graph())) {
                        ++lambdaClosureCount_[function->graph()];
                        lambdaSite_.try_emplace(function->graph(), it.ref());
                    }
                }
            }
        }
    }

    void validateGraph(GCGraph *graph, bool root) {
        graph_  = graph;
        isRoot_ = root;
        const bool lambda = lambdaGraphs_.contains(graph);
        if (!graph->executable()) {
            fail(camel::runtime::kInvalidNodeRef, "it has writings (it is not executable)");
        }
        if ((graph->flags() & camel::runtime::kGraphFlagMacro) != 0) {
            fail(camel::runtime::kInvalidNodeRef, "it is a macro graph");
        }
        for (uint32_t r = 1; r < graph->regionCount(); ++r) {
            const camel::runtime::GCRegion *region = graph->region(r);
            if (region == nullptr) {
                continue;
            }
            switch (region->kind) {
            case camel::runtime::GCRegionKind::Arm:
            case camel::runtime::GCRegionKind::LoopCond:
            case camel::runtime::GCRegionKind::LoopBody:
                break;
            default:
                fail(
                    camel::runtime::kInvalidNodeRef,
                    std::format(
                        "a '{}' region, which is not a branch or loop region",
                        regionKindText(region->kind)));
            }
        }
        if (!root) {
            if (!lambda) {
                const std::string &name = graph->name();
                if (!isIdentifier(name)) {
                    fail(
                        camel::runtime::kInvalidNodeRef,
                        std::format("the graph name '{}' is not a source identifier", name));
                }
            }
            const FunctionType *type = graph->funcType();
            if (type == nullptr) {
                fail(camel::runtime::kInvalidNodeRef, "it has no function type");
            }
            if (type->hasRest()) {
                fail(camel::runtime::kInvalidNodeRef, "a variadic parameter");
            }
            if (type->hasConstas()) {
                fail(camel::runtime::kInvalidNodeRef, "a 'constas' parameter");
            }
            if (lambda && type->withTypesCount() != 0) {
                fail(camel::runtime::kInvalidNodeRef, "a lambda with 'with' parameters");
            }
            // A `with` parameter is a source parameter: it is named by the function type, and a
            // direct call gives it in `<...>`. The rest of the context inputs are module values
            // the function reads, which a source call does not write.
            const size_t withCount = type->withTypesCount();
            const auto withPorts   = graph->withPorts();
            for (size_t i = 0; i < withPorts.size(); ++i) {
                if (i < withCount) {
                    continue;
                }
                const camel::passes::gir::PortInfo info =
                    camel::passes::gir::portInfo(graph, withPorts[i]);
                if (!topLevelNames_.contains(info.name) && !lambda) {
                    fail(
                        withPorts[i],
                        std::format(
                            "it reads '{}', which is not a top-level name", info.name));
                }
            }
            checkSignatureType(type);
            for (const SymId capture : graph->captureNames()) {
                const std::string name = std::string(camel::core::symbolText(capture));
                if (lambda) {
                    if (!isIdentifier(name)) {
                        fail(
                            camel::runtime::kInvalidNodeRef,
                            std::format("a closure capture '{}' without a source name", name));
                    }
                } else if (!topLevelNames_.contains(name)) {
                    fail(
                        camel::runtime::kInvalidNodeRef,
                        std::format(
                            "it captures '{}' from an enclosing function (a closure)", name));
                }
            }
            // A lambda is printed inside its consumer: one function constant, at most one closure.
            if (lambda) {
                const auto siteIt = lambdaSite_.find(graph);
                const gc_node_ref_t site =
                    siteIt != lambdaSite_.end() ? siteIt->second
                                                : camel::runtime::kInvalidNodeRef;
                if (lambdaDataCount_[graph] > 1) {
                    fail(site, "a lambda used as more than one function value");
                }
                if (lambdaClosureCount_[graph] > 1) {
                    fail(site, "a lambda wrapped in more than one closure (a shared lambda)");
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
            switch (node->kind) {
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
                std::format("{} has the type '{}', which std::cml does not print", where, type->toString()));
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
        control_ = std::make_unique<camel::runtime::ControlRegions>(*graph);

        const FunctionType *type = graph->funcType();
        if (type != nullptr) {
            const size_t withCount = type->withTypesCount();
            const auto withPorts   = graph->withPorts();
            for (size_t i = 0; i < withCount && i < withPorts.size(); ++i) {
                std::string name(type->argNameAt(i));
                if (!isIdentifier(name)) {
                    name = std::format("w{}", i);
                }
                if (!usedNames_.insert(name).second) {
                    name = std::format("w{}", i);
                    usedNames_.insert(name);
                }
                params_[withPorts[i]] = name;
            }
            for (size_t i = 0; i < type->normTypesCount(); ++i) {
                std::string name(type->argNameAt(withCount + i));
                if (!isIdentifier(name)) {
                    name = std::format("p{}", i);
                }
                if (!usedNames_.insert(name).second) {
                    name = std::format("p{}", i);
                    usedNames_.insert(name);
                }
                const gc_node_ref_t port = graph->normPorts()[i];
                params_[port]            = name;
                if (type->normIsVarAt(i)) {
                    cells_[port] = name;
                }
            }
            // The rest of the context inputs are module values the function reads; their source
            // names are the top-level names the printer already knows.
            for (size_t i = withCount; i < withPorts.size(); ++i) {
                const camel::passes::gir::PortInfo info =
                    camel::passes::gir::portInfo(graph, withPorts[i]);
                if (!isIdentifier(info.name)) {
                    fail(withPorts[i], "a captured module value without a name");
                }
                params_[withPorts[i]] = info.name;
            }
        }
        const auto captures = graph->captureNames();
        const auto closure  = graph->closureNodes();
        for (size_t i = 0; i < captures.size() && i < closure.size(); ++i) {
            const std::string name(camel::core::symbolText(captures[i]));
            if (!isIdentifier(name)) {
                fail(closure[i], "a closure capture without a source name");
            }
            params_[closure[i]] = name;
            usedNames_.insert(name);
            const Type *portType = graph->node(closure[i])->dataType();
            if (portType != nullptr && portType->code() == TypeCode::Tuple &&
                static_cast<const TupleType *>(portType)->isCell()) {
                cells_[closure[i]] = name;
            }
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
        case camel::runtime::GCNodeKind::Join: {
            const auto *body = graph_->nodeBodyAs<camel::runtime::GCJoinBody>(ref);
            for (const uint32_t arm : control_->armsOf(body->brch)) {
                if (arm != 0 &&
                    !camel::runtime::effectSummaryOfRegion(program_, graph_, arm).pure()) {
                    return true;
                }
            }
            return false;
        }
        case camel::runtime::GCNodeKind::Loop: {
            const camel::runtime::LoopView view(program_, *graph_, ref);
            return !view.bodyEffects().pure() || !view.condEffects().pure();
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

    /// The value of a node that is a constant int, when it is one: a subscript's axis is written
    /// as a subscript on that dimension, which is a number the printer needs.
    std::optional<int64_t> constantIntOf(gc_node_ref_t ref) const {
        const camel::runtime::GCNode *node = graph_->node(ref);
        if (node == nullptr || node->kind != camel::runtime::GCNodeKind::Data ||
            node->dataIndex >= 0 || node->dataType() == nullptr ||
            node->dataType()->code() != TypeCode::Int64) {
            return std::nullopt;
        }
        return fromSlot<int64_t>(graph_->staticArea()->get<slot_t>(-node->dataIndex));
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

    // ---- scopes --------------------------------------------------------------------------

    /// A region's statements and the value it yields. `lines` already carry their indentation;
    /// `closeIndent` lines the closing brace up with the construct that opened the region.
    struct Body {
        std::vector<std::string> lines;
        Expr tail;
        bool hasTail     = false;
        bool sync        = false;
        std::string closeIndent;
        std::string tailIndent;
    };

    void printStatements() {
        gc_node_ref_t tail = camel::execute::resolveRuntimeTailValueRef(graph_);
        bool tailPrintedAsStatement = false;

        // A block with no chain of its own runs the write it yields only because the value is
        // taken from that write: the source ends such a body with the write, and `return v` after
        // it would hand the same value on while leaving the write behind a value that reaches
        // nothing, so the write would not run at all (design principles 3.4). The write the exit's
        // gate waits on is then the block's last statement.
        const gc_node_ref_t valueWrite = blockValueWrite(tail);

        // A local copy: printing a lambda or a region switches the printer to another graph and
        // back, and that replaces `topo_` while this loop walks it.
        const std::vector<gc_node_ref_t> order = topo_;
        for (const gc_node_ref_t ref : order) {
            const camel::runtime::GCNode *node = graph_->node(ref);
            if (node == nullptr) {
                continue;
            }
            // A node in an arm or a loop is printed where its region is opened, not here.
            if (control_->controlOf(ref) != 0) {
                continue;
            }
            processNode(ref, tail, true, tailPrintedAsStatement);
        }
        if (valueWrite != camel::runtime::kInvalidNodeRef) {
            tailPrintedAsStatement = true;
        }

        if (!isRoot_ && tail != camel::runtime::kInvalidNodeRef && !tailPrintedAsStatement) {
            const Type *type = graph_->node(tail)->dataType();
            if (type != nullptr && type->code() != TypeCode::Void) {
                if (declared_.contains(tail)) {
                    emit(std::format("{}return {}", indent_, declared_[tail]));
                } else {
                    emit(std::format("{}return {}", indent_, expression(tail).text));
                }
            }
        }
    }

    /// Prints the statements of one region (`0` is the graph body). `outerIndent` is the
    /// indentation of the construct that opens the region; its statements are one level deeper.
    Body buildBody(
        uint32_t region, gc_node_ref_t tail, std::string outerIndent, bool functionBody) {
        auto savedParams   = params_;
        auto savedDeclared = declared_;
        auto savedCells    = cells_;
        auto savedLines    = std::move(lines_);
        const std::string savedIndent = indent_;
        lines_.clear();
        indent_ = outerIndent + "    ";

        if (region != 0) {
            bindRegionPorts(region);
        }

        bool tailPrintedAsStatement = false;
        // Same reason as in `printStatements`: a nested lambda replaces `topo_` while this loop
        // walks it.
        const std::vector<gc_node_ref_t> order = topo_;
        for (const gc_node_ref_t ref : order) {
            const camel::runtime::GCNode *node = graph_->node(ref);
            if (node == nullptr || control_->controlOf(ref) != region) {
                continue;
            }
            processNode(ref, tail, functionBody, tailPrintedAsStatement);
        }

        Body body;
        body.lines       = std::move(lines_);
        body.closeIndent = outerIndent;
        body.tailIndent  = indent_;
        body.sync        = regionNeedsSync(region);
        // A region whose value is the value its last write wrote, with no chain of its own to run
        // that write: the write is the region's last statement, as `printStatements` prints it for
        // a body. `return <value>` there would hand the value on behind no wait at all and leave
        // the write behind a value that reaches nothing (see `regionValueWrite`).
        const gc_node_ref_t valueWrite =
            region == 0 ? blockValueWrite(tail) : regionValueWrite(region, tail);
        if (valueWrite != camel::runtime::kInvalidNodeRef) {
            tailPrintedAsStatement = true;
        }
        if (!functionBody && regionReturnsValue(region) && !tailPrintedAsStatement) {
            // A region whose value is a gate with ordering in front of it was written as an
            // explicit `return`; a bare last expression yields the value without that gate.
            Expr value;
            if (tail != camel::runtime::kInvalidNodeRef && graph_->node(tail) != nullptr) {
                value = declared_.contains(tail) ? Expr{declared_[tail], 100}
                                                 : expression(tail);
            }
            body.lines.push_back(value.text.empty() ? indent_ + "return"
                                                    : indent_ + "return " + value.text);
        } else if (tail != camel::runtime::kInvalidNodeRef && graph_->node(tail) != nullptr &&
                   !tailPrintedAsStatement) {
            body.hasTail = true;
            body.tail    = declared_.contains(tail) ? Expr{declared_[tail], 100}
                                                    : expression(tail);
        }

        lines_     = std::move(savedLines);
        params_    = std::move(savedParams);
        declared_  = std::move(savedDeclared);
        cells_     = std::move(savedCells);
        indent_    = savedIndent;
        return body;
    }

    /// A block for a region body, inline when it has no statements and no ordering to keep.
    std::string blockText(const Body &body, bool forceBraces) {
        if (!forceBraces && body.lines.empty() && !body.sync) {
            return body.hasTail ? body.tail.text : "()";
        }
        std::string text = body.sync ? "sync {\n" : "{\n";
        for (const std::string &line : body.lines) {
            text += line;
            text += '\n';
        }
        if (body.hasTail) {
            text += body.tailIndent + body.tail.text + "\n";
        }
        text += body.closeIndent + "}";
        return text;
    }

    /// Whether the source ended this region with an explicit `return`: the region yields a gate
    /// that orders the returned value after the statements before it.
    bool regionReturnsValue(uint32_t region) const {
        std::vector<gc_node_ref_t> results;
        if (region == 0) {
            // The body region yields the graph's exit, not a region result; the value may be
            // wrapped in the gate of an explicit `return`.
            results.push_back(graph_->exitNodeRef());
            results.push_back(graph_->returnNodeRef());
        } else {
            const camel::runtime::GCRegion *info = graph_->region(region);
            if (info == nullptr) {
                return false;
            }
            results.push_back(info->result);
        }
        for (const gc_node_ref_t result : results) {
            const camel::runtime::GCNode *node = graph_->node(result);
            if (node == nullptr || node->kind != camel::runtime::GCNodeKind::Gate) {
                continue;
            }
            for (const gc_node_ref_t source : graph_->ctrlInputsOf(result)) {
                if (graph_->node(source) != nullptr && control_->controlOf(source) == region) {
                    return true;
                }
            }
        }
        return false;
    }

    /// The write whose value the block yields, when the block has no chain to run it: the gate at
    /// the exit hands the block's value on and waits on that write. `kInvalidNodeRef` otherwise --
    /// a block with a chain runs its statements anyway, and a value that is no write's value has
    /// nothing behind it.
    gc_node_ref_t blockValueWrite(gc_node_ref_t tail) const {
        if (tail == camel::runtime::kInvalidNodeRef || regionNeedsSync(0)) {
            return camel::runtime::kInvalidNodeRef;
        }
        const gc_node_ref_t exitRef = graph_->exitNodeRef();
        if (exitRef == camel::runtime::kInvalidNodeRef || graph_->node(exitRef) == nullptr ||
            graph_->node(exitRef)->kind != camel::runtime::GCNodeKind::Gate) {
            return camel::runtime::kInvalidNodeRef;
        }
        bool handsTail = false;
        for (const gc_node_ref_t input : graph_->normInputsOf(exitRef)) {
            handsTail = handsTail || input == tail;
        }
        if (!handsTail) {
            return camel::runtime::kInvalidNodeRef;
        }
        return assignmentHandingOn(exitRef, 0, tail);
    }

    /// The assignment a block hands a value on behind: one of `gate`'s control inputs is a write
    /// of `region` whose value is the one `gate` carries. That is the GATE the builder puts in
    /// front of the value an assignment stands for, so that the write runs where nothing else
    /// reads the version it made (design principles 3.5, 3.4): the source's block ended with the
    /// write and the block's value is what that write wrote. `kInvalidNodeRef` otherwise.
    gc_node_ref_t assignmentHandingOn(
        gc_node_ref_t gate, uint32_t region, gc_node_ref_t value) const {
        for (const gc_node_ref_t input : graph_->ctrlInputsOf(gate)) {
            const camel::runtime::GCNode *node = graph_->node(input);
            if (node == nullptr || node->kind != camel::runtime::GCNodeKind::Oper ||
                control_->controlOf(input) != region) {
                continue;
            }
            if (!graph_->nodeBodyAs<camel::runtime::GCOperBody>(input)->uri().starts_with(
                    ":op/assn")) {
                continue;
            }
            const auto written = graph_->normInputsOf(input);
            if (written.size() == 2 && written[1] == value) {
                return input;
            }
        }
        return camel::runtime::kInvalidNodeRef;
    }

    /// The write whose value a region hands on, when the region has no chain of its own: the
    /// region's result is that same GATE, and the source's region ended with the write. Printing
    /// the value back as `return <value>` would hand it on behind no wait at all, leaving the
    /// write behind a value that reaches nothing, so the write would not run (design principles
    /// 3.4); the write is the region's last statement instead, as `blockValueWrite` prints it for
    /// a block with no chain.
    gc_node_ref_t regionValueWrite(uint32_t region, gc_node_ref_t tail) const {
        if (tail == camel::runtime::kInvalidNodeRef || regionNeedsSync(region)) {
            return camel::runtime::kInvalidNodeRef;
        }
        const camel::runtime::GCNode *gate = graph_->node(tail);
        if (gate == nullptr || gate->kind != camel::runtime::GCNodeKind::Gate) {
            return camel::runtime::kInvalidNodeRef;
        }
        const auto values = graph_->normInputsOf(tail);
        if (values.size() != 1) {
            return camel::runtime::kInvalidNodeRef;
        }
        return assignmentHandingOn(tail, region, values.front());
    }

    /// Whether the source wrote a `sync` block around this region's statements: the answer is
    /// decided by the sync edge. A `sync` block orders one statement after another, which is the
    /// sync edge the builder writes between the two (`syncPredsOf`, docs/design/effects/edges.cn.md
    /// 1.5); a branch or loop links its region to the owner with a control edge, so that edge says
    /// nothing here. A value handed on behind a gate to make a write run waits through the control
    /// kind alone: that is no block's chain, and printing it as one would make the block's reads
    /// settle as the reads of a chain (Builder::markNonRetainingReads) and print a graph the
    /// source did not write.
    bool regionNeedsSync(uint32_t region) const {
        for (const gc_node_ref_t ref : topo_) {
            if (graph_->node(ref) == nullptr || control_->controlOf(ref) != region) {
                continue;
            }
            for (const camel::runtime::GCGraphSyncEdge &edge : graph_->syncPredsOf(ref)) {
                const gc_node_ref_t source = edge.pred;
                if (graph_->node(source) != nullptr && control_->controlOf(source) == region) {
                    return true;
                }
            }
        }
        return false;
    }

    /// The name of a region port: the source's name when it is one, a fresh name otherwise (a
    /// loop's state port can carry a path such as `p.x`).
    std::string nameOfPort(gc_node_ref_t port) {
        auto found = params_.find(port);
        if (found != params_.end()) {
            return found->second;
        }
        const std::string name = freshName();
        params_[port]          = name;
        return name;
    }

    void bindRegionPorts(uint32_t region) {
        const camel::runtime::GCRegion *info = graph_->region(region);
        if (info == nullptr) {
            return;
        }
        const auto ports = graph_->regionPorts(region);
        for (size_t i = 0; i < ports.size(); ++i) {
            // A loop's condition ports are bound to the body's state names before the condition
            // is built; keep those.
            if (params_.contains(ports[i])) {
                continue;
            }
            std::string name;
            if (info->params != nullptr && i < info->params->normTypesCount()) {
                name = std::string(info->params->argNameAt(i));
            }
            if (!isIdentifier(name)) {
                name = freshName();
            } else {
                usedNames_.insert(name);
            }
            params_[ports[i]] = name;
            const Type *type  = graph_->node(ports[i])->dataType();
            if (type != nullptr && type->code() == TypeCode::Tuple &&
                static_cast<const TupleType *>(type)->isCell()) {
                cells_[ports[i]] = name;
            }
        }
    }

    /// Binds the loop condition's ports to the names the body gave the same state values.
    void bindLoopCond(uint32_t region, const std::vector<std::string> &stateNames) {
        const auto ports = graph_->regionPorts(region);
        for (size_t i = 0; i < ports.size() && i < stateNames.size(); ++i) {
            params_[ports[i]] = stateNames[i];
            const Type *type  = graph_->node(ports[i])->dataType();
            if (type != nullptr && type->code() == TypeCode::Tuple &&
                static_cast<const TupleType *>(type)->isCell()) {
                cells_[ports[i]] = stateNames[i];
            }
        }
    }

    void processNode(
        gc_node_ref_t ref, gc_node_ref_t tail, bool functionBody,
        bool &tailPrintedAsStatement) {
        const camel::runtime::GCNode *node = graph_->node(ref);
        if (node == nullptr) {
            return;
        }
        switch (node->kind) {
        case camel::runtime::GCNodeKind::Port:
        case camel::runtime::GCNodeKind::Gate:
        case camel::runtime::GCNodeKind::Copy:
        case camel::runtime::GCNodeKind::Brch:
            return;
        case camel::runtime::GCNodeKind::Oper: {
            const std::string_view uri =
                graph_->nodeBodyAs<camel::runtime::GCOperBody>(ref)->uri();
            if (uri == ":var/new") {
                emitVariable(ref);
                if (ref == tail) {
                    tailPrintedAsStatement = true;
                }
                return;
            }
            if (uri.starts_with(":var/")) {
                return;
            }
            if (uri.starts_with(":op/assn")) {
                emitAssignment(ref, uri);
                if (ref == tail) {
                    tailPrintedAsStatement = true;
                }
                return;
            }
            break;
        }
        default:
            break;
        }

        const bool named   = !sourceName(ref).empty();
        const bool effect  = nodeEffect(ref);
        const size_t users = valueUsers(ref);

        // The initializer is read before the name is recorded: `expression(ref)` must not
        // answer with the name the declaration is about to give the node.
        if (named) {
            const std::string name = sourceName(ref);
            const Expr value       = expression(ref);
            declared_[ref]         = name;
            emit(std::format(
                "{}let {}: {} = {}", indent_, name, typeText(node->dataType()), value.text));
        } else if (effect) {
            if (users == 0) {
                if (ref == tail && !functionBody) {
                    // The region's value: `buildBody` writes it as the block's tail.
                    return;
                }
                branchAsStatement_ = true;
                const std::string text = expression(ref).text;
                branchAsStatement_     = false;
                emit(indent_ + text);
                if (ref == tail) {
                    tailPrintedAsStatement = true;
                }
            } else if (users > 1) {
                const Expr value       = expression(ref);
                const std::string name = freshName();
                declared_[ref]         = name;
                emit(std::format(
                    "{}let {}: {} = {}", indent_, name, typeText(node->dataType()), value.text));
            }
            // A single use keeps the call where the source had it: lifting it into a
            // `let` would move the effect ahead of the expression it belongs to.
        } else if (users > 1 || needsAnnotation(ref)) {
            const Expr value       = expression(ref);
            const std::string name = freshName();
            declared_[ref]         = name;
            emit(std::format(
                "{}let {}: {} = {}", indent_, name, typeText(node->dataType()), value.text));
        } else if (users == 0) {
            // A value nothing reads is not part of the printed program, unless it is the tail
            // of a region, which `buildBody` still writes as the block's value.
            return;
        }
    }

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
            "{}var {}: {} = {}", indent_, name, typeText(valueType), initial.text));
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
        emit(indent_ +
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

    /// The cell a read of `ref` reads. A write's product is the version of the variable it wrote
    /// (`:op/assn*` returns this version's cell; design principles 2.1):
    /// the cell a write stores into is that same variable, so a read whose data port is a write
    /// reads the variable itself and is written with the variable's own name. Every other data port
    /// is the cell it names, and one that has no name yet is named by `cellName` as before.
    gc_node_ref_t cellReadBy(gc_node_ref_t ref) const {
        const camel::runtime::GCNode *node = graph_->node(ref);
        if (node == nullptr || node->kind != camel::runtime::GCNodeKind::Oper) {
            return ref;
        }
        const std::string_view uri = graph_->nodeBodyAs<camel::runtime::GCOperBody>(ref)->uri();
        if (!uri.starts_with(":op/assn")) {
            return ref;
        }
        const auto inputs = graph_->normInputsOf(ref);
        return inputs.empty() ? ref : inputs.front();
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
        case camel::runtime::GCNodeKind::Gate: {
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
            fail(ref, "a branch without its join");
        case camel::runtime::GCNodeKind::Join:
            return branchExpression(ref);
        case camel::runtime::GCNodeKind::Loop:
            return loopExpression(ref);
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

    // ---- branches ------------------------------------------------------------------------

    Expr branchExpression(gc_node_ref_t joinRef) {
        const auto *body = graph_->nodeBodyAs<camel::runtime::GCJoinBody>(joinRef);
        if (body->brch == camel::runtime::kInvalidNodeRef) {
            fail(joinRef, "a join without the branch it joins");
        }
        const auto arms = control_->armsOf(body->brch);
        if (arms.size() < 2) {
            fail(joinRef, "a branch with fewer than two arms");
        }
        const auto selector = graph_->normInputsOf(body->brch);
        if (selector.empty()) {
            fail(joinRef, "a branch without a selector");
        }
        const auto patterns = graph_->withInputsOf(body->brch);
        const Type *joinType = graph_->node(joinRef)->dataType();
        const bool isIf       = patterns.empty();
        const bool asStatement = branchAsStatement_;

        const auto armText = [&](size_t k) {
            const camel::runtime::GCRegion *region = graph_->region(arms[k]);
            if (region == nullptr) {
                return std::string("()");
            }
            Body arm = buildBody(arms[k], region->result, indent_, false);
            const bool braces =
                asStatement ||
                (joinType != nullptr && joinType->code() == TypeCode::Void);
            std::string text = blockText(arm, braces);
            // An arm that is itself an `if` or `match` needs its parentheses: `then if a then
            // b else c else d` would otherwise bind the second `else` to the inner `if`.
            if (!braces && arm.lines.empty() && !arm.sync && arm.hasTail &&
                arm.tail.precedence <= 10) {
                return "(" + text + ")";
            }
            return text;
        };

        // `if c then a else b`; the compiler adds an empty second arm for `if` without `else`,
        // whose arm yields void, and that arm is not written.
        if (isIf) {
            if (arms.size() != 2) {
                fail(joinRef, "an `if` that does not have two arms");
            }
            const camel::runtime::GCRegion *second = graph_->region(arms[1]);
            const bool emptyElse =
                joinType != nullptr && joinType->code() == TypeCode::Void && second != nullptr &&
                (second->result == camel::runtime::kInvalidNodeRef ||
                 (graph_->node(second->result) != nullptr &&
                  graph_->node(second->result)->kind == camel::runtime::GCNodeKind::Data &&
                  graph_->node(second->result)->dataType() != nullptr &&
                  graph_->node(second->result)->dataType()->code() == TypeCode::Void));
            const Expr condition = expression(selector.front());
            std::string text = std::format("if {} then {}", parenthesize(condition, 30), armText(0));
            if (!emptyElse) {
                text += " else " + armText(1);
            }
            return {text, 10};
        }

        const bool hasDefault = arms.size() == patterns.size() + 1;
        if (arms.size() != patterns.size() && !hasDefault) {
            fail(joinRef, "a `match` whose arm count does not match its patterns");
        }
        const std::string caseIndent = indent_ + "    ";
        std::string text = "match " + parenthesize(expression(selector.front()), 30) + " {\n";
        for (size_t k = 0; k < arms.size(); ++k) {
            text += caseIndent + "case ";
            if (k < patterns.size()) {
                const camel::runtime::GCNode *pattern = graph_->node(patterns[k]);
                if (pattern == nullptr || pattern->kind != camel::runtime::GCNodeKind::Data ||
                    pattern->dataIndex >= 0) {
                    fail(
                        patterns[k],
                        "a match pattern that is not a constant (an enum, tag or type pattern)");
                }
                text += expression(patterns[k]).text;
            } else if (hasDefault && k + 1 == arms.size()) {
                text += "_";
            } else {
                fail(joinRef, "a match arm without a pattern");
            }
            const auto *region = graph_->region(arms[k]);
            std::string arm    = region != nullptr
                                     ? blockText(
                                           buildBody(arms[k], region->result, caseIndent, false),
                                           false)
                                     : std::string("()");
            text += " => " + arm + "\n";
        }
        text += indent_ + "}";
        return {text, 10};
    }

    // ---- loops ---------------------------------------------------------------------------

    Expr loopExpression(gc_node_ref_t ref) {
        const camel::runtime::LoopView view(program_, *graph_, ref);
        if (view.collectionCount() != 0) {
            fail(ref, "a loop that collects values into arrays");
        }
        if (view.stateCount() == 0) {
            // The source spelling of a loop declares at least one state name; a loop without
            // one has no source form.
            fail(ref, "a loop without state");
        }
        const size_t states = view.stateCount();
        const bool iterates = view.iterable().has_value();

        // The initial values are written in the enclosing scope, before the loop's names exist.
        std::vector<std::string> initials;
        initials.reserve(states);
        for (size_t j = 0; j < states; ++j) {
            initials.push_back(expression(view.initialState(j)).text);
        }
        std::string iterable;
        if (iterates) {
            const gc_node_ref_t sequence = *view.iterable();
            // A loop whose sequence is the interval `a..b` walks the interval's two bounds and no
            // array exists (design principles 4.2: a `for` over an interval is fused): the source
            // form is the interval itself, not the pair of bounds, so the printed program compiles
            // back to the same graph.
            const bool interval =
                view.graph().loopIterableFormOf(view.node()) ==
                camel::core::LoopIterableForm::Interval;
            const auto bounds = interval ? view.graph().intervalBoundsOf(sequence) : std::nullopt;
            if (bounds.has_value()) {
                iterable = std::format(
                    "{}..{}", intervalOperand(expression(bounds->first), kIntervalPrecedence),
                    intervalOperand(expression(bounds->second), kIntervalPrecedence + 1));
            } else if (interval) {
                // The pair of bounds is a value: a pass may have written it as the constant
                // holding the same two integers, and the source form is still the interval
                // (GCGraph::intervalPairValuesOf).
                const auto values = view.graph().intervalPairValuesOf(sequence);
                iterable          = values.has_value()
                                        ? std::format(
                                              "{}..{}", intervalConstantOperand(values->first),
                                              intervalConstantOperand(values->second))
                                        : expression(sequence).text;
            } else {
                iterable = expression(sequence).text;
            }
        }

        auto savedParams   = params_;
        auto savedDeclared = declared_;
        auto savedCells    = cells_;
        const std::string savedIndent = indent_;
        std::string elementName;
        std::vector<std::string> stateNames;
        if (iterates) {
            bindRegionPorts(view.bodyRegion());
            elementName = nameOfPort(*view.bodyElementParam());
        } else {
            bindRegionPorts(view.bodyRegion());
        }
        for (size_t j = 0; j < states; ++j) {
            stateNames.push_back(nameOfPort(view.bodyStateParam(j)));
        }

        std::string text = states == 0 ? std::string("loop") : std::string("loop ");
        for (size_t j = 0; j < states; ++j) {
            text += (j ? ", " : "") + stateNames[j] + " = " + initials[j];
        }
        if (iterates) {
            text += " for " + elementName + " in " + iterable;
        }
        if (view.condRegion() != 0) {
            bindLoopCond(view.condRegion(), stateNames);
            Body cond = buildBody(view.condRegion(), view.condResult(), indent_, false);
            if (!cond.lines.empty() || !cond.hasTail || cond.sync) {
                fail(ref, "a loop condition with statements or a sync block");
            }
            text += " while " + parenthesize(cond.tail, 30);
        }

        Body body = buildBody(view.bodyRegion(), view.bodyResult(), indent_, false);
        text += " do " + blockText(body, false);

        params_   = std::move(savedParams);
        declared_ = std::move(savedDeclared);
        cells_    = std::move(savedCells);
        indent_   = savedIndent;
        return {text, 10};
    }

    // ---- closures ------------------------------------------------------------------------

    /// A lambda is written where its value is made: `(x: int) => x + offset`. The captures are
    /// the values the closure closes over, in capture-port order; their source names are kept so
    /// the recompiled lambda closes over the same names.
    Expr lambdaExpression(
        GCGraph *lambda, const std::vector<std::string> &captureTexts,
        std::string outerIndent) {
        const FunctionType *type = lambda->funcType();
        if (type == nullptr) {
            fail(camel::runtime::kInvalidNodeRef, "a lambda without a function type");
        }
        if (type->withTypesCount() != 0) {
            fail(camel::runtime::kInvalidNodeRef, "a lambda with 'with' parameters");
        }
        if (type->hasRest() || type->hasConstas()) {
            fail(camel::runtime::kInvalidNodeRef, "a lambda with a variadic parameter");
        }

        auto savedParams   = params_;
        auto savedDeclared = declared_;
        auto savedCells    = cells_;
        auto savedSkip     = skip_;
        auto savedTopo     = topo_;
        auto savedUsed     = usedNames_;
        auto savedLines    = std::move(lines_);
        auto savedControl  = std::move(control_);
        GCGraph *savedGraph             = graph_;
        const bool savedRoot            = isRoot_;
        const size_t savedGenerated     = generated_;
        const std::string savedIndent   = indent_;
        const bool savedBranchStatement = branchAsStatement_;

        graph_             = lambda;
        isRoot_            = false;
        skip_.clear();
        lines_.clear();
        generated_         = 0;
        indent_            = outerIndent;
        branchAsStatement_ = false;
        prepareGraph(lambda);

        const auto ports = lambda->closureNodes();
        const auto caps  = lambda->captureNames();
        if (ports.size() != caps.size() || captureTexts.size() != caps.size()) {
            fail(
                camel::runtime::kInvalidNodeRef,
                "a closure whose capture ports and values do not match");
        }
        for (size_t i = 0; i < caps.size(); ++i) {
            const std::string name(camel::core::symbolText(caps[i]));
            if (!isIdentifier(name)) {
                fail(ports[i], "a closure capture without a source name");
            }
            params_[ports[i]] = name;
            usedNames_.insert(name);
            const Type *portType = lambda->node(ports[i])->dataType();
            if (portType != nullptr && portType->code() == TypeCode::Tuple &&
                static_cast<const TupleType *>(portType)->isCell()) {
                cells_[ports[i]] = name;
            }
        }

        const gc_node_ref_t tail = camel::execute::resolveRuntimeTailValueRef(lambda);
        const Body body          = buildBody(0, tail, outerIndent, false);

        std::string text;
        text += "(";
        for (size_t i = 0; i < type->normTypesCount(); ++i) {
            std::string name(type->argNameAt(i));
            if (!isIdentifier(name)) {
                name = std::format("p{}", i);
            }
            if (type->normIsVarAt(i)) {
                text += "var ";
            }
            text += (i ? ", " : "") + name + ": " + typeText(type->normTypeAt(i));
        }
        text += ")";
        if (type->hasExitType()) {
            text += ": " + typeText(type->exitType());
        }
        text += " => " + blockText(body, body.sync);

        lines_             = std::move(savedLines);
        params_            = std::move(savedParams);
        declared_          = std::move(savedDeclared);
        cells_             = std::move(savedCells);
        skip_              = std::move(savedSkip);
        topo_              = std::move(savedTopo);
        usedNames_         = std::move(savedUsed);
        control_           = std::move(savedControl);
        graph_             = savedGraph;
        isRoot_            = savedRoot;
        generated_         = savedGenerated;
        indent_            = savedIndent;
        branchAsStatement_ = savedBranchStatement;
        return {text, 5};
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
            if (function->graph() != nullptr && lambdaGraphs_.contains(function->graph())) {
                return lambdaExpression(function->graph(), {}, indent_);
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
        std::string baseText = parenthesize(base, 90);
        // A struct literal starts with `{`: an access on it needs the parentheses the source
        // wrote (`({ x: 0 }).x`), because the postfix would otherwise bind inside the braces.
        if (!baseText.empty() && baseText.front() == '{') {
            baseText = "(" + baseText + ")";
        }
        const std::string member =
            body->accsKind == camel::runtime::GCAccsKind::StructKey
                ? std::string(body->key())
                : std::to_string(body->value);
        if (member.empty()) {
            fail(ref, "an access without a member");
        }
        return {std::format("{}.{}", baseText, member), 90};
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
            if (function != nullptr && function->graph() != nullptr &&
                lambdaGraphs_.contains(function->graph())) {
                std::vector<std::string> captureTexts;
                captureTexts.reserve(with.size());
                for (const gc_node_ref_t capture : with) {
                    captureTexts.push_back(expression(capture).text);
                }
                return lambdaExpression(function->graph(), captureTexts, indent_);
            }
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
            return {cellName(cellReadBy(inputs.front())), 100};
        }
        if (uri.starts_with(":var/")) {
            fail(ref, std::format("the operator '{}'", uri));
        }
        if (uri == ":op/idx_arr" || uri == ":op/idx_str" || uri == ":op/idx") {
            if (inputs.size() != 2 && inputs.size() != 3) {
                fail(ref, "an index without a value and an index");
            }
            Expr base  = expression(inputs[0]);
            Expr index = expression(inputs[1]);
            // The position's own axis is the third word, and the dimensions before it are kept
            // whole (design principles 5.4): the subscript writes them as `:` and the index on the
            // axis it names, so `m[:, 0]` is the position 0 on axis 1 of `m`. A node that writes
            // no axis is the first dimension's, which is the plain `t[i]`.
            std::string axes;
            if (inputs.size() == 3) {
                const std::optional<int64_t> axis = constantIntOf(inputs[2]);
                if (!axis || *axis < 0) {
                    fail(ref, "an index on a dimension that is not a constant");
                }
                for (int64_t k = 0; k < *axis; ++k) {
                    axes += ":, ";
                }
            }
            return {
                std::format("{}[{}{}]", parenthesize(base, 90), axes, index.text),
                90};
        }
        if (uri == ":struct/range") {
            // The interval `a..b` (design principles 4.2) is the one spelling of the integers from
            // `a` to `b - 1`, and it lowers to this operator's call with its step written out,
            // `range(a, b, 1)`: the rule takes exactly three arguments and the two-argument
            // spelling is gone. A step of one is that interval and is written back as it, which is
            // what keeps the round trip a round trip; any other step stays the call it is.
            if (inputs.size() == 3) {
                const Expr step = expression(inputs[2]);
                if (step.text == "1") {
                    return {
                        std::format(
                            "{}..{}",
                            intervalOperand(expression(inputs[0]), kIntervalPrecedence),
                            intervalOperand(expression(inputs[1]), kIntervalPrecedence + 1)),
                        kIntervalPrecedence};
                }
                // The stepped range `range(a, b, step)`: written back as the call it is, so the
                // program the printer writes reads the way the program it prints did (design
                // principles 4.2).
                return {
                    std::format(
                        "range({}, {}, {})",
                        expression(inputs[0]).text,
                        expression(inputs[1]).text,
                        step.text),
                    100};
            }
            fail(ref, "a range without its bounds");
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
            return callText(ref, function, graph_->normInputsOf(ref), {});
        }
        if (callee == nullptr) {
            fail(ref, "a call without a callee");
        }
        // A direct call of a function that reads module values passes those values as `with`
        // inputs. The source omits them: the callee names the top-level values itself. The
        // function's own `with` parameters come first and are written `<...>` at the call.
        const std::string name = functionNameOf(callee);
        if (name.empty()) {
            fail(ref, "a call of a function whose graph has no source name (a lambda)");
        }
        const FunctionType *type = callee->funcType();
        const size_t explicitWith = type != nullptr ? type->withTypesCount() : 0;
        const auto with           = graph_->withInputsOf(ref);
        if (with.size() < explicitWith) {
            fail(ref, "a call of a configured function without its configuration");
        }
        return callText(
            ref, {name, 100}, graph_->normInputsOf(ref),
            with.subspan(0, explicitWith));
    }

    Expr callText(
        gc_node_ref_t ref, const Expr &callee, std::span<const gc_node_ref_t> inputs,
        std::span<const gc_node_ref_t> withArgs) {
        std::string text = parenthesize(callee, 90);
        if (!withArgs.empty()) {
            text += "<";
            for (size_t i = 0; i < withArgs.size(); ++i) {
                text += (i ? ", " : "") + expression(withArgs[i]).text;
            }
            text += ">";
        }
        text += "(";
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
        indent_ = "    ";
        skip_.clear();
        prepareGraph(graph);

        const FunctionType *type = graph->funcType();
        const std::string name   = graph->name();
        std::string signature;
        if (type->withTypesCount() > 0) {
            std::string clause = "with <";
            for (size_t i = 0; i < type->withTypesCount(); ++i) {
                std::string withName(type->argNameAt(i));
                if (!isIdentifier(withName)) {
                    withName = std::format("w{}", i);
                }
                clause += (i ? ", " : "") + withName + ": " + typeText(type->withTypeAt(i));
            }
            clause += ">";
            emit(clause);
        }
        signature += "func " + name + "(";
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
        // A function graph whose body has a control edge between its own nodes was written with
        // a body that is a sync block: the modifier says so, and it is the only mark of it.
        if (regionNeedsSync(0)) {
            signature += " sync";
        }
        emit(signature + " {");
        isRoot_ = false;
        printStatements();
        emit("}");
        appendLines();
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

PassMaker cmlPass() {
    return [](const camel::core::context::context_ptr_t &ctx, const PassArgs &args) {
        CmlOptions options;
        const int64_t limit     = args.getInt("max_literal_bytes");
        options.maxLiteralBytes = limit > 0 ? static_cast<size_t>(limit) : 0;
        return std::make_unique<CmlDumpPass>(ctx, options);
    };
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

