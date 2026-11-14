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

#include "camel/core/context/context.h"
#include "camel/core/error/diagnostics/messages/semantic.h"
#include "camel/core/loop_kind.h"
#include "camel/core/module/module.h"
#include "camel/core/operator_names.h"
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
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <map>
#include <memory>
#include <queue>
#include <set>
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
using camel::core::type::StructDecl;
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

/// A graph name of a function a program declares inside a namespace: `Tools::inc`, `util::id`. Each
/// segment is a source identifier, and there is no generic argument list (a graph name that carries
/// `<...>` is an instance, not a declaration). Such a function is printed inside its `namespace`
/// block (`compile/namespace_rules/*`), which is what the source wrote.
bool isNamespaceName(std::string_view text) {
    if (text.find('<') != std::string_view::npos) {
        return false;
    }
    size_t start = 0;
    size_t segments = 0;
    for (;;) {
        const size_t at = text.find("::", start);
        const std::string_view segment =
            at == std::string_view::npos ? text.substr(start) : text.substr(start, at - start);
        if (!isIdentifier(segment)) {
            return false;
        }
        ++segments;
        if (at == std::string_view::npos) {
            break;
        }
        start = at + 2;
    }
    return segments >= 2;
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
    // A value with two or more backslashes and nothing else that an escape is for (no control
    // character, no byte outside printable ASCII) is printed as a raw string: a Windows path
    // stays `r"C:\dir\file"` instead of becoming `'C:\\dir\\file'`, and reading the printed
    // program costs what reading the original cost. The delimiters are `r"`, `r#"`, `r##"` ...
    // with as many `#` as it takes for the closing `"` + `#`s not to occur inside the value, so
    // the printed literal is always the value and nothing of it is decoded.
    const auto rawForm = [&]() -> std::string {
        size_t backslashes = 0;
        for (const char ch : text) {
            const auto c = static_cast<unsigned char>(ch);
            if (ch == '\\') {
                ++backslashes;
                continue;
            }
            if (c < 0x20 || c == 0x7F || c >= 0x80) {
                return {};
            }
        }
        if (backslashes < 2) {
            return {};
        }
        size_t hashes = 0;
        for (;; ++hashes) {
            const std::string closer = "\"" + std::string(hashes, '#');
            if (text.find(closer) == std::string_view::npos) {
                break;
            }
        }
        const std::string fence(hashes, '#');
        std::string raw = "r" + fence + "\"";
        raw += text;
        raw += "\"" + fence;
        return raw;
    };
    if (std::string raw = rawForm(); !raw.empty()) {
        return raw;
    }
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

/// How tightly a prefix operator (`-`, `!`, `~`) binds, which is the parser's own level and not
/// this printer's opinion (`src/parse/syntax/parser_expr.cpp`, `negExpr`): "looser than `->` and
/// than `^`, tighter than the other binary operators ... `-x ^ 2` is `-(x ^ 2)`, `-a * b` is
/// `(-a) * b`". So it sits between `*` and `^` of `precedenceOf`: a prefix as the base of a power
/// takes the parentheses (`(-x) ^ 2`), as the left operand of `*` it does not (`-a * b`). The
/// value 70 that used to stand here put the prefix above `^`, so `(-x) ^ 2` printed as `-x ^ 2`,
/// which parses as `-(x ^ 2)` (the A7 case `parse/rulings/power`: 9 printed as -9).
constexpr int kPrefixPrecedence = 55;

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
        const Program &program, const camel::core::context::Context *context, std::ostream &out,
        size_t maxLiteralBytes, std::vector<BigConstant> &bigConstants)
        : program_(program), context_(context), out_(out), maxLiteralBytes_(maxLiteralBytes),
          bigConstants_(bigConstants) {}

    void run() {
        GCGraph *root = program_.root();
        if (root == nullptr) {
            return;
        }
        graphs_ = camel::runtime::collectReachableGraphs(program_, root);
        classifyLambdas();
        assignFunctionNames();
        collectTopLevelNames(root);
        findEntry(root);
        collectOperatorModules();
        collectConstructionTemplates();
        collectImports();
        // The named struct types have to be known before a signature is validated: a signature
        // that names `V` is printable only because the printed program will declare `V`.
        collectNamedTypes();
        checkNamedTypeNames();
        orderNamedTypes();
        collectMatchPatterns();
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

        // The body is written first and the header last, because which `type` statements the
        // program needs is known only once its text exists: a named type is declared when the
        // program *writes* its name, and a type the graph carries in a place nothing prints needs
        // no declaration (writing one would add a declaration the source never had).
        orderNamedTypes();
        // Every import is whole (`import tensor`): a module operator is written `module::name`, and a
        // sign a program writes needs the module that declares its rule, so no name is imported.
        for (const std::string &module : moduleImports_) {
            out_ << "import " << module << '\n';
        }
        if (!moduleImports_.empty()) {
            out_ << '\n';
        }
        // One `type` statement per named struct type the program names, before the text that names
        // it: `func f(x: V): int` on its own is `UnresolvedTypeReference: 'V'`, and the declaration
        // is the half the printed program was missing (see `collectNamedTypes`).
        for (const StructType *record : namedTypeOrder_) {
            out_ << namedTypeText(record) << '\n';
        }
        if (!namedTypeOrder_.empty()) {
            out_ << '\n';
        }
        out_ << body_;
    }

  private:
    // program-holder-exempt: borrow-or-foreign-type -- the printer reads the program its pass
    // was given; the driver owns it for the translation.
    const Program &program_;
    /// The context the program was compiled on, for the one fact the printer cannot read off a
    /// graph: which module a name belongs to (see `collectOperatorModules`). Null when a caller
    /// builds a printer without one, and then a URI's own prefix is all there is.
    const camel::core::context::Context *context_ = nullptr;
    std::ostream &out_;
    size_t maxLiteralBytes_;
    std::vector<BigConstant> &bigConstants_;

    std::vector<GCGraph *> graphs_;
    std::unordered_map<const GCGraph *, bool> effectful_;
    std::unordered_set<std::string> topLevelNames_;
    /// The names of the functions the printed file declares (`func map(...)`): a module operator
    /// written by name has to mean the module's, and the program's own function of that name is
    /// what a bare `map<f>(xs)` would call. Collected from the graphs, because the root graph's
    /// `nodeName` is not always the source name of a top-level function (measured on
    /// `consistency/comprehension/shadowed.cml`: a guard keyed on `topLevelNames_` never fired).
    std::unordered_set<std::string> writtenFunctionNames_;
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
    /// The modules the printed program names (see `collectImports`): every operator the printer writes
    /// -- as a sign or as `module::name` -- is bound by a declaration of its module, and a module
    /// imported whole brings the declaration in.
    std::set<std::string> moduleImports_;
    /// The module each operator the text names belongs to, by the URI it was declared under (see
    /// `collectOperatorModules`), and the module a URI's prefix answers on. Both hold the name a
    /// program *imports*, which is not always the spelling the operator's URI uses.
    std::unordered_map<std::string, std::string> operatorModule_;
    std::unordered_map<std::string, std::string> protocolModule_;
    /// The template constants of constructions whose every slot is written by the construction
    /// (see `collectConstructionTemplates`): shapes, not values the program writes. The graph is
    /// part of the key: a node reference is only an index inside one graph, and a reference held
    /// for one graph named a node of another (a lambda body's template made the enclosing
    /// function's `let count` disappear, `trans/port_fill_metadata.cml`).
    std::unordered_map<const GCGraph *, std::unordered_set<gc_node_ref_t>> constructionTemplates_;
    /// The name each function graph is printed under, when it is not the graph's own name (see
    /// `assignFunctionNames`).
    std::unordered_map<const GCGraph *, std::string> functionNames_;
    /// The named struct types the printed program mentions, in discovery order, and the ones for
    /// which no `type` statement can be written, with the reason (see `collectNamedTypes`).
    std::vector<const StructType *> namedTypeRecords_;
    std::unordered_set<const StructType *> namedTypeSeen_;
    std::unordered_map<const StructType *, std::string> namedTypeWhy_;
    /// The same types in an order a `type` statement can be read in: a field type's declaration
    /// first (see `orderNamedTypes`).
    std::vector<const StructType *> namedTypeOrder_;
    /// The named types the printed text actually names: only these are declared, so a type the
    /// graph carries where nothing prints it does not add a declaration the source never had.
    std::unordered_set<const StructType *> namedTypeUsed_;
    /// The names the block being printed has bound. A second `let` of one name in one block is a
    /// `Redeclaration` in the program the text would be, so it is refused where it is found (a
    /// `static for` expands one source binding once per field, and the graph then holds two nodes
    /// with that name in one body: `parse/rulings/static_for_head_names.cml`).
    std::unordered_set<std::string> blockNames_;
    /// The constants the arms of a `match` test. The printed text has to write each as a literal: a
    /// name is not a pattern, and materialising one as a `let` wrote `case cml1`, which the front
    /// end refuses (`IdentifierPatternNeedsNullableOrErrorType`,
    /// `compile/literals/context_data_literal.cml`).
    std::unordered_set<gc_node_ref_t> patternRefs_;
    /// The program's text without its header. The header carries one `type` statement per named
    /// type the text names, and that is known only after the text is written.
    std::string body_;
    // Constants bound before the statement that uses them. `1 + 2` written out comes back folded to
    // `3`: the front end folds an operator whose operands are both literals, so a graph that holds
    // those three nodes would come back as one. A `let` of a literal is that literal's node and
    // nothing else, and a name is not a literal, so `let c0 = 1` then `c0 + 2` keeps the node.
    std::vector<std::string> held_;
    std::unordered_map<gc_node_ref_t, std::string> heldNames_;
    size_t heldSeq_ = 0;
    size_t generated_          = 0;
    std::string indent_        = "    ";
    /// The indent of the header lines of the function being printed: a function inside a
    /// `namespace` block sits one level in, and its body one deeper.
    std::string headerIndent_;
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
        // The names the file will declare: `func <name>`, in the spelling `printFunction` writes
        // (the part after the last `::`).
        for (const GCGraph *graph : graphs_) {
            const std::string name = printedNameOf(graph);
            if (name.empty()) {
                continue;
            }
            const size_t at = name.rfind("::");
            writtenFunctionNames_.insert(at == std::string::npos ? name : name.substr(at + 2));
        }
    }

    /// The name a function graph is printed under: its source name, unless another graph already
    /// carries that name. Two graphs can: a program with two `when`-overloaded declarations
    /// (`when fast` / `when wide`) compiles to two graphs of one source name, and the condition
    /// text that tells them apart is on the declaration and not on the graph
    /// (`compile/when_overloads/values.cml` printed `func pick` twice, which is `Redeclaration`).
    /// A name is for a reader -- the canonical document compares the graphs, and its graph label
    /// carries `flags`, `root` and `entry`, not the name -- so a second name the printer makes up
    /// is invisible to the checker, while every call still calls the graph it called (the calls
    /// are printed from the callee's graph, see `expression` of a function constant).
    void assignFunctionNames() {
        std::unordered_set<std::string> taken;
        for (GCGraph *graph : graphs_) {
            if (lambdaGraphs_.contains(graph) || graph == program_.root()) {
                continue;
            }
            // The graph's own name is what the declaration writes, namespaced functions included
            // (`Tools::inc` is printed inside a `namespace Tools` block, see `printFunction`).
            const std::string name = graph->name();
            if (name.empty()) {
                continue;
            }
            if (taken.insert(name).second) {
                continue;
            }
            // Keep the namespace the source wrote and make the local part new.
            const size_t at          = name.rfind("::");
            const std::string prefix = at == std::string::npos ? std::string{} : name.substr(0, at + 2);
            std::string candidate;
            do {
                candidate = prefix + freshName();
            } while (!taken.insert(candidate).second);
            functionNames_[graph] = candidate;
        }
    }

    /// The name a printed declaration of this graph writes (see `assignFunctionNames`): the graph's
    /// own name, or the one made up for it because another graph carries that name too.
    std::string printedNameOf(const GCGraph *graph) const {
        const auto found = functionNames_.find(graph);
        return found != functionNames_.end() ? found->second
                                             : (graph != nullptr ? graph->name() : std::string{});
    }

    /// The name a printed *call* of this graph writes: the same, but a graph with no source name
    /// (a lambda) is refused by the caller rather than named after its internal label.
    std::string callNameOf(const GCGraph *graph) const {
        const auto found = functionNames_.find(graph);
        return found != functionNames_.end() ? found->second : functionNameOf(graph);
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
                if (!isIdentifier(name) && !isNamespaceName(name)) {
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
            if (record->named()) {
                // The program writes the type's name, and `run` writes one `type` statement per
                // named type it mentions, so the name is declared. Its fields are the declaration
                // `collectNamedTypes` already settled; walking into them here would never end for
                // a type that mentions itself.
                if (namedTypePrintable(record)) {
                    return;
                }
                fail(
                    camel::runtime::kInvalidNodeRef,
                    std::format(
                        "{} is the named type '{}', whose declaration cannot be printed: {}",
                        where, type->toString(), namedTypeObstacleOf(record)));
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

    /// The `: T` a declaration writes after its name, or nothing when that position has no
    /// spelling for the type. A function type is the one: `let back: (float, float) => float = ...`
    /// is a syntax error -- an annotation is read as a type, and the arrow in it is not one, so the
    /// parser reports `Unexpected '=>'; expected '='` -- and a parenthesised one is no better
    /// (`MissingComma`). An unannotated `let` takes the type of its value, which is the type the
    /// node has, so the declaration says the same thing by saying nothing (the same reading
    /// `emitVariable` has for a `var`).
    /// Whether a type has a function anywhere in it: such a type has no spelling in the annotation
    /// position, and a tuple of function types fails the same way its members do.
    static bool hasFunction(const Type *type) {
        if (type == nullptr) {
            return false;
        }
        switch (type->code()) {
        case TypeCode::Function:
            return true;
        case TypeCode::Array:
            return hasFunction(static_cast<const ArrayType *>(type)->elemType());
        case TypeCode::Tuple: {
            const auto *tuple = static_cast<const TupleType *>(type);
            for (size_t i = 0; i < tuple->size(); ++i) {
                if (hasFunction(tuple->typeAt(i))) {
                    return true;
                }
            }
            return false;
        }
        case TypeCode::Struct: {
            const auto *record = static_cast<const StructType *>(type);
            for (size_t i = 0; i < record->size(); ++i) {
                if (hasFunction(record->typeAt(i))) {
                    return true;
                }
            }
            return false;
        }
        default:
            return false;
        }
    }

    std::string annotationOf(const Type *type) {
        if (hasFunction(type)) {
            return {};
        }
        // An array literal written with no element type at all is a value of the array whose
        // element type nobody wrote, and `[]` is that type's shape and not a type: the annotation
        // position reads a type, so `let cml1: [] = []` is `Unexpected '['; expected a type`
        // (`trans/cpp/empty_literal_takes_the_position.cml`). The unannotated `let` states the
        // same type by saying nothing -- it starts from the same literal, whose type is the one
        // the node has.
        if (camel::core::type::isUntypedArrayType(type)) {
            return {};
        }
        return ": " + writtenTypeText(type);
    }

    /// `resetNames` is false for a lambda's body, which is written inside the text of the body that
    /// makes it: names the inner body invented are in scope there, so the outer body must keep
    /// counting past them (`consistency/values/closure_var` printed `let cml2 = cml1 + 1` with
    /// `cml1` declared one scope down).
    void prepareGraph(GCGraph *graph, bool resetNames = true) {
        params_.clear();
        declared_.clear();
        cells_.clear();
        usedNames_.clear();
        lines_.clear();
        if (resetNames) {
            generated_ = 0;
        }
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
            for (const camel::runtime::GCGraphSyncEdge &edge : graph_->syncPredsOf(ref)) {
                depend(edge.pred);
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

    /// The cell a version node belongs to: the chain of writes and `:var/after` versions ends at
    /// the `:var/new` node that made the cell.
    gc_node_ref_t cellOf(gc_node_ref_t version) const {
        for (int guard = 0; guard < 64; ++guard) {
            const camel::runtime::GCNode *node = graph_->node(version);
            if (node == nullptr || node->kind != camel::runtime::GCNodeKind::Oper) {
                return version;
            }
            const std::string_view uri =
                graph_->nodeBodyAs<camel::runtime::GCOperBody>(version)->uri();
            if (!uri.starts_with(":op/assn") && uri != ":var/after") {
                return version;
            }
            const auto inputs = graph_->normInputsOf(version);
            if (inputs.empty()) {
                return version;
            }
            version = inputs.front();
        }
        return version;
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
                        if (!candidate->ctrlInputsOf(ref).empty() ||
                            !candidate->syncPredsOf(ref).empty()) {
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
                            // A call through a function value has no graph the printer can read,
                            // so nothing here can call it pure: it counts as an effect, because
                            // dropping it is a false print (A7's `effects/writer_table_through_callee`:
                            // `apply((x) => { counter = x }, 41)` writes the caller's `var` through
                            // the callee, and the printed program read `0` where the source read 41).
                            if (callee == nullptr || effectful_[callee]) {
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

    /// Whether the only thing that reads `ref` is version plumbing: a `GATE`/`COPY` chain that ends
    /// at `:var/peek`, `:var/after` or the cell slot of a write. Those nodes print a variable name,
    /// never the value they carry, so an effect that reaches one has no other place to be written.
    bool feedsVersionPlumbing(gc_node_ref_t ref) {
        gc_node_ref_t current = ref;
        for (int guard = 0; guard < 16; ++guard) {
            std::vector<gc_node_ref_t> users;
            for (const gc_node_ref_t user : graph_->normOutputsOf(current)) {
                if (!skip_.contains(user)) {
                    users.push_back(user);
                }
            }
            if (users.size() != 1) {
                return false;
            }
            const gc_node_ref_t user = users.front();
            const camel::runtime::GCNode *node = graph_->node(user);
            if (node == nullptr) {
                return false;
            }
            if (node->kind == camel::runtime::GCNodeKind::Gate ||
                node->kind == camel::runtime::GCNodeKind::Copy) {
                current = user;
                continue;
            }
            if (node->kind != camel::runtime::GCNodeKind::Oper) {
                return false;
            }
            const std::string_view uri = graph_->nodeBodyAs<camel::runtime::GCOperBody>(user)->uri();
            if (uri == ":var/peek" || uri == ":var/after") {
                return true;
            }
            if (uri.starts_with(":op/assn")) {
                const auto inputs = graph_->normInputsOf(user);
                return !inputs.empty() && inputs.front() == current;
            }
            return false;
        }
        return false;
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
            // A call through a function value: no graph to read, so it is an effect (see
            // `isEffectful` for why -- printing it away loses the write it makes).
            return callee == nullptr || isEffectful(callee);
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

    /// Whether the one place that reads this value writes its type itself. A cast states the type it
    /// produces, so a literal under a cast of the value's own type needs no `let` of its own: the
    /// `as` writes the type, and binding the literal first would add a node the graph does not
    /// have (a `let` is a store, the graph's copy of the value is not).
    bool typeWrittenByUse(gc_node_ref_t ref) {
        const camel::runtime::GCNode *self = graph_->node(ref);
        if (self == nullptr) {
            return false;
        }
        // The graph's own copy of the value is not a name it needs: look past it to the one place
        // the value is read.
        gc_node_ref_t user = ref;
        const camel::runtime::GCNode *node = nullptr;
        for (;;) {
            const auto users = graph_->normOutputsOf(user);
            if (users.size() != 1) {
                return false;
            }
            user = users.front();
            node = graph_->node(user);
            if (node == nullptr || node->kind != camel::runtime::GCNodeKind::Copy) {
                break;
            }
        }
        const Type *mine   = self->dataType();
        if (mine == nullptr || node->dataType() == nullptr) {
            return false;
        }
        if (node->kind == camel::runtime::GCNodeKind::Oper) {
            // A `var` declaration writes the type of the value it starts with (`var w: f32 = 1.0`),
            // so a literal written there needs no `let` of its own either.
            const auto *body = graph_->nodeBodyAs<camel::runtime::GCOperBody>(user);
            if (body == nullptr || body->uri() != ":var/new" ||
                node->dataType()->code() != TypeCode::Tuple) {
                return false;
            }
            const auto *cell = static_cast<const TupleType *>(node->dataType());
            if (!cell->isCell() || cell->size() != 1) {
                return false;
            }
            const Type *valueType = cell->typeAt(0);
            return valueType != nullptr && typeText(mine) == typeText(valueType);
        }
        if (node->kind != camel::runtime::GCNodeKind::Cast) {
            return false;
        }
        // The cast writes a type; the question is whether the value's own type says something the
        // cast does not. An array of one unknown extent (`f32[]`) under a cast to an open-rank
        // array (`f32[..]`) says nothing more, so the annotation is a `let` the graph does not
        // have. A value whose type has extents to lose (`f32[2, 2]` under `f32[..]`) keeps its
        // annotation: the cast cannot say the shape for it.
        const Type *theirs = node->dataType();
        if (theirs == nullptr || mine->code() != TypeCode::Array ||
            theirs->code() != TypeCode::Array) {
            return false;
        }
        const auto *value  = static_cast<const ArrayType *>(mine);
        const auto *casted = static_cast<const ArrayType *>(theirs);
        return value->rank() == 1 && value->extents()[0].isUnknown() && casted->rank() == 0;
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
            // An empty literal whose element type nobody wrote has no `let` of its own: the
            // annotation position reads a type and `[]` is a shape, while an unannotated `let` of
            // it is `EmptyArrayWithoutType` ("write the type: `let cml1: int[] = []`"). The type
            // such a value has is the one the *position* gives it, so the literal is written where
            // it stands and the position types it again -- which is what the source wrote
            // (`[]->append<1>`, `trans/cpp/empty_literal_takes_the_position.cml`).
            if (camel::core::type::isUntypedArrayType(type)) {
                return false;
            }
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

    /// Whether a printed expression is a literal the front end folds when both operands are one: a
/// number, a string, a boolean. `()` is not: it is the unit value, and binding it would change the
/// type it is written at.
    static bool isLiteralText(std::string_view text) {
        if (text.empty()) {
            return false;
        }
        if (text == "true" || text == "false") {
            return true;
        }
        if (text.front() == '\'' || text.front() == '"') {
            return true;
        }
        const size_t start = text.front() == '-' ? 1 : 0;
        return start < text.size() && std::isdigit(static_cast<unsigned char>(text[start])) != 0;
    }

    void emit(std::string line) {
        flushHeld(lines_);
        lines_.push_back(std::move(line));
    }

    /// Puts the bindings a statement needs in front of it.
    void flushHeld(std::vector<std::string> &into) {
        for (std::string &held : held_) {
            into.push_back(std::move(held));
        }
        held_.clear();
        // The bindings live in the statement they were written for: a later statement in another
        // function must not read them, and a name reused across statements would read a variable
        // that is out of scope, or the wrong one.
        heldNames_.clear();
    }

    /// The name a constant is bound to before it is used, binding it the first time: one `let` per
    /// constant node, so a constant read twice stays one node, as it is in the graph.
    std::string hoistConstant(gc_node_ref_t ref, const std::string &value) {
        const auto found = heldNames_.find(ref);
        if (found != heldNames_.end()) {
            return found->second;
        }
        std::string name;
        do {
            name = std::format("cml{}", heldSeq_++);
        } while (usedNames_.contains(name));
        usedNames_.insert(name);
        held_.push_back(std::format("{}let {} = {}", indent_, name, value));
        heldNames_.emplace(ref, name);
        return name;
    }

    void appendLines() {
        for (const std::string &line : lines_) {
            body_ += line;
            body_ += '\n';
        }
        body_ += '\n';
        lines_.clear();
    }

    // ---- scopes --------------------------------------------------------------------------
    //
    // Which module a printed name belongs to. An operator is printed as a call of the declaration
    // that made it an operator, and that call has to resolve in the printed file: a module
    // operator is written `module::name`, and the module is imported whole. That module is *not*
    // the operator URI's prefix, and two measurements say so:
    //
    //   * a module may answer on another prefix than its own name -- `random` registers its
    //     operators as `rand:...` (`modules/random/module.h`) -- so the printed `import rand` was
    //     `ModuleNotFound` (`resources/conflict_per_resource.cml`);
    //   * the core's namespace is where an *implementation* lives, not where the name does: a
    //     module's declaration tags its body `#op(':mark/unordered_reduce_arr')`
    //     (`stdlib/para.cml`), and the name a program writes is that module's. Printed bare, with
    //     no import, `unordered_reduce` was `UnresolvedReference`
    //     (`compile/native/para_declaration_import.cml`).
    //
    // So the module is asked for instead of being spelled out of the URI: the run's modules are
    // asked which of them exports the operator. The prelude and the entry module are not
    // candidates: their names are in scope without an import, so qualifying a call with one would
    // write a name the printed file does not have.
    void collectOperatorModules() {
        if (context_ == nullptr) {
            return;
        }
        const auto entry   = context_->mainModule();
        const auto prelude = context_->preludeModule();
        for (const auto &module : context_->allUserModules()) {
            if (module == nullptr || module == entry || module == prelude || module->name().empty()) {
                continue;
            }
            for (const std::string &protocol : module->operatorProtocols()) {
                if (!protocol.empty()) {
                    protocolModule_.try_emplace(protocol, module->name());
                }
            }
        }
    }

    /// The module a printed call of `uri` is qualified with and imported from, or empty when the
    /// name is in scope by itself (`println`, `+`, `map`) and no import is needed. A prefix no
    /// module claims is the answer, which keeps the reading a URI's own spelling gives wherever
    /// the two agree.
    std::string moduleNameOf(std::string_view uri) {
        const size_t colon = uri.find(':');
        if (colon == std::string_view::npos) {
            return {};
        }
        if (colon != 0) {
            const std::string prefix(uri.substr(0, colon));
            const auto known = protocolModule_.find(prefix);
            return known != protocolModule_.end() ? known->second : prefix;
        }
        const std::string key(uri);
        const auto cached = operatorModule_.find(key);
        if (cached != operatorModule_.end()) {
            return cached->second;
        }
        std::string owner;
        // A sign is the language's own spelling wherever the operator was declared: `:op/idx_arr`
        // is `[]` by the group's symbol, and the printed `[7, 8, 9][2]` needs no module at all --
        // qualifying it, or importing the module that declared the same URI, only adds a name the
        // program did not write (`compile/native/generic_qualified.cml`, whose `at` is a
        // declaration of `generic_module` over this URI and whose printed form is the subscript).
        const bool languageSymbol =
            camel::core::operatorSymbolOf(camel::core::OperatorTable::instance().groupOf(key)) !=
            nullptr;
        const auto oper = languageSymbol
                              ? std::optional<camel::core::OperId>{}
                              : camel::core::OperatorTable::instance().lookup(key);
        if (context_ != nullptr && oper.has_value()) {
            const auto entry   = context_->mainModule();
            const auto prelude = context_->preludeModule();
            for (const auto &module : context_->allUserModules()) {
                if (module == nullptr || module == entry || module == prelude ||
                    module->name().empty()) {
                    continue;
                }
                if (module->nativeOperatorGroup(*oper) != nullptr) {
                    owner = module->name();
                    break;
                }
            }
        }
        operatorModule_.emplace(key, owner);
        return owner;
    }

    /// A construction's template constant is not always a value the program writes: an array literal
    /// whose every slot is filled by the construction is written as that literal again, and the
    /// compiler builds the same template. Such a constant is skipped when all of this holds, and
    /// each part is a fact measured against a case that broke when it was left out:
    ///
    ///   * it is an `Array` construction (`append` grows its template rather than filling it, and
    ///     the loop that builds an array through `append` is refused by `loopExpression` for a
    ///     reason of its own -- `loops/edges.cml`);
    ///   * the constant is an array whose slots are all empty: the values of the slots a
    ///     construction does not fill come from the constant, so a constant that holds one is the
    ///     program's value;
    ///   * the constant has no source name (`let make = ...` binds a value the source named, and
    ///     the closure construction around it is not a literal, `trans/port_fill_metadata.cml`);
    ///   * nothing else reads it, and every slot is covered by a `with` input.
    ///
    /// What it fixes: `compile/array_shape/rank_zero_slot_array_element.cml` printed
    /// `let cml1: f32[...][2] = [[], []]` for the skeleton of `[one, two]`, and no spelling of an
    /// open-rank element is `[]` (`DimensionMismatch` at that position).
    void collectConstructionTemplates() {
        for (GCGraph *graph : graphs_) {
            for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
                const camel::runtime::GCNode *node = *it;
                if (node->kind != camel::runtime::GCNodeKind::Fill) {
                    continue;
                }
                const auto *body = graph->nodeBodyAs<camel::runtime::GCFillBody>(it.ref());
                if (body == nullptr || body->fillKind != camel::runtime::GCFillKind::Array) {
                    continue;
                }
                const auto inputs = graph->normInputsOf(it.ref());
                if (inputs.empty()) {
                    continue;
                }
                const gc_node_ref_t templ             = inputs.front();
                const camel::runtime::GCNode *constant = graph->node(templ);
                if (constant == nullptr || constant->kind != camel::runtime::GCNodeKind::Data ||
                    constant->dataIndex >= 0 || !sourceNameIt(graph, templ).empty()) {
                    continue;
                }
                if (constant->dataType() == nullptr ||
                    constant->dataType()->code() != TypeCode::Array) {
                    continue;
                }
                const auto *value =
                    fromSlot<Array *>(graph->staticArea()->get<slot_t>(-constant->dataIndex));
                if (value == nullptr || value->size() == 0 ||
                    body->slots().size() < value->size()) {
                    continue;
                }
                bool emptyShape = true;
                for (size_t i = 0; i < value->size(); ++i) {
                    if (value->get<slot_t>(i) != 0) {
                        emptyShape = false;
                        break;
                    }
                }
                if (!emptyShape) {
                    continue;
                }
                // A construction that is itself an input of another construction is part of that
                // one's shape, not a literal the program wrote: the array a closure captures is
                // the `with` input of the closure's own construction, and skipping its template
                // orphaned the values the closure body still names (`trans/port_fill_metadata.cml`
                // lost `let make` and left `count` unresolved).
                bool partOfAnotherConstruction = false;
                for (const gc_node_ref_t user : graph->normOutputsOf(it.ref())) {
                    const camel::runtime::GCNode *userNode = graph->node(user);
                    if (userNode != nullptr &&
                        userNode->kind == camel::runtime::GCNodeKind::Fill) {
                        partOfAnotherConstruction = true;
                        break;
                    }
                }
                if (!partOfAnotherConstruction) {
                    for (const gc_node_ref_t user : graph->normOutputsOf(it.ref())) {
                        for (const gc_node_ref_t with : graph->withInputsOf(user)) {
                            if (with == it.ref()) {
                                partOfAnotherConstruction = true;
                            }
                        }
                    }
                }
                if (partOfAnotherConstruction) {
                    continue;
                }
                // A template anything else reads is a value of the program, not only a shape.
                bool onlyTheConstruction = true;
                for (const gc_node_ref_t user : graph->normOutputsOf(templ)) {
                    if (user != it.ref()) {
                        onlyTheConstruction = false;
                        break;
                    }
                }
                for (const gc_node_ref_t user : graph->withOutputsOf(templ)) {
                    if (user != it.ref()) {
                        onlyTheConstruction = false;
                        break;
                    }
                }
                if (onlyTheConstruction) {
                    constructionTemplates_[graph].insert(templ);
                }
            }
        }
    }

    /// The names the printed program calls, by the module that declares them: a call by name needs
    /// the name in scope, and `sum` is a name of the module `tensor` (`import { sum } from
    /// tensor`). An operator of the core (`:op/add_l`, `:io/println`) has no module to import, and
    /// an operator written with a sign has no name to bring in -- but the *module* still has to be
    /// imported, because the sign is bound to its operator by the module's declaration: without
    /// `import tensor`, `[7, 8, -9] / [1, 0, 2]` is `NoMatchingFunction`, since `/` has no rule for
    /// `int[]` outside that module (design principles 5.1). So a module operator written with a
    /// sign imports the module, and one written as a call imports only the name.
    void collectImports() {
        for (const GCGraph *graph : graphs_) {
            for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
                if ((*it)->kind != camel::runtime::GCNodeKind::Oper) {
                    continue;
                }
                const auto *body = graph->nodeBodyAs<camel::runtime::GCOperBody>(it.ref());
                if (body == nullptr) {
                    continue;
                }
                const std::string_view uri = body->uri();
                const std::string module   = moduleNameOf(uri);
                if (module.empty()) {
                    continue;
                }
                // What the printed file writes for a module operator is `module::name`, the
                // qualified call (`tensor::sum([1.0])` compiles to `tensor:sum`, measured), and the
                // module is imported whole -- the one import that makes a qualified call resolve and
                // that a sign also needs (`/` for `int[]` is a rule of `math`).
                //
                // Importing the *name* instead was wrong in a way the corpus caught: a program that
                // uses both an operator the printer spells as a sign (`@`, which imports the module
                // whole) and one it spells by name pulled the module into `moduleImports_` and the
                // name into `imports_`, and the emission dropped the by-name clause for a module it
                // had already imported whole -- `import { tanh } from tensor` came out as
                // `import tensor` beside a bare `tanh(...)`, which the front end answered with
                // `UnresolvedReference: 'tanh'` (`compile/entry_args/struct_param`, measured). A
                // qualified name does not depend on which clauses were collected, so the variable is
                // gone rather than fixed.
                moduleImports_.insert(module);
            }
        }
    }

    // ---- named struct types ---------------------------------------------------------------
    //
    // A named struct type is printed by its name (`V`), and a program that writes the name has to
    // declare it: `func f(x: V): int` on its own does not compile (`UnresolvedTypeReference:
    // 'V'`), while the same function after `type V = { x: int }` does. Nothing in the graph gives
    // the second half: the field table is on the type (`StructType::size()` / `typeAt(i)` /
    // `fieldName(i)`), so the declaration can be written, but the printer had no `type` emitter
    // -- and importing the declaring module instead is not a general substitute, because an
    // import only brings a type in when the module is exported *and* resolvable from where the
    // printed file is compiled (the corpus compiles it in a temporary directory, where a
    // case-local module is on no search path).
    //
    // So: collect the named types the program mentions, decide for each whether a `type`
    // statement can be written, and refuse -- with the reason naming which half is missing -- the
    // ones that cannot.

    /// Whether a name can be written for this type at all, and if not, why not.
    std::string namedTypeObstacle(const StructType *record) const {
        const StructDecl *decl = record->decl();
        if (decl == nullptr) {
            return "the type has no declaration";
        }
        if (decl->generic() || record->typeArgCount() != 0) {
            return "a generic declaration, and the printer writes no type parameters";
        }
        if (!isIdentifier(decl->name())) {
            return std::format("a name that is not a source identifier ('{}')", decl->name());
        }
        if (decl->handle()) {
            return "a `#resource` declaration, and the tag is not readable from the type";
        }
        for (size_t i = 0; i < record->size(); ++i) {
            if (!isIdentifier(record->fieldName(i))) {
                return std::format(
                    "a field name that is not a source identifier ('{}')",
                    std::string(record->fieldName(i)));
            }
            if (record->typeAt(i) == nullptr) {
                return std::format("a field ('{}') with no type", std::string(record->fieldName(i)));
            }
        }
        return {};
    }

    /// Records every named struct type `type` mentions, and every named type their fields mention.
    void visitNamedTypes(const Type *type) {
        if (type == nullptr) {
            return;
        }
        switch (type->code()) {
        case TypeCode::Array:
            visitNamedTypes(static_cast<const ArrayType *>(type)->elemType());
            return;
        case TypeCode::Tuple: {
            const auto *tuple = static_cast<const TupleType *>(type);
            if (tuple->isTagged() || tuple->isNullable() || tuple->isCell()) {
                return; // refused where it is printed, and it declares nothing
            }
            for (size_t i = 0; i < tuple->size(); ++i) {
                visitNamedTypes(tuple->typeAt(i));
            }
            return;
        }
        case TypeCode::Struct: {
            const auto *record = static_cast<const StructType *>(type);
            if (!record->named()) {
                for (size_t i = 0; i < record->size(); ++i) {
                    visitNamedTypes(record->typeAt(i));
                }
                return;
            }
            noteNamedType(record);
            return;
        }
        case TypeCode::Function: {
            const auto *function = static_cast<const FunctionType *>(type);
            for (size_t i = 0; i < function->normTypesCount(); ++i) {
                visitNamedTypes(function->normTypeAt(i));
            }
            if (function->hasExitType()) {
                visitNamedTypes(function->exitType());
            }
            return;
        }
        default:
            return;
        }
    }

    void noteNamedType(const StructType *record) {
        if (!namedTypeSeen_.insert(record).second) {
            return;
        }
        namedTypeRecords_.push_back(record);
        const std::string obstacle = namedTypeObstacle(record);
        if (!obstacle.empty()) {
            namedTypeWhy_[record] = obstacle;
            return;
        }
        // The fields are what the declaration writes, so their own types are needed too. A type
        // that mentions itself ends here: it is already recorded.
        for (size_t i = 0; i < record->size(); ++i) {
            visitNamedTypes(record->typeAt(i));
        }
        // A field that names a type with no writable declaration makes this declaration
        // unwritable as well: writing `type A = { b: B }` where `B` cannot be declared would
        // name a type the printed program has no declaration for.
        for (size_t i = 0; i < record->size(); ++i) {
            const std::string nested = unwritableInside(record->typeAt(i));
            if (!nested.empty()) {
                namedTypeWhy_[record] = std::format(
                    "its field '{}' has a type whose declaration cannot be printed: {}",
                    std::string(record->fieldName(i)), nested);
                return;
            }
        }
    }

    /// The obstacle of the first named type `type` mentions whose declaration cannot be written,
    /// or nothing. A named type is looked up rather than walked into: its own fields were settled
    /// when it was recorded, and a type that mentions itself would never end.
    std::string unwritableInside(const Type *type) const {
        if (type == nullptr) {
            return {};
        }
        switch (type->code()) {
        case TypeCode::Array:
            return unwritableInside(static_cast<const ArrayType *>(type)->elemType());
        case TypeCode::Tuple: {
            const auto *tuple = static_cast<const TupleType *>(type);
            if (tuple->isTagged() || tuple->isNullable() || tuple->isCell()) {
                return {};
            }
            for (size_t i = 0; i < tuple->size(); ++i) {
                const std::string nested = unwritableInside(tuple->typeAt(i));
                if (!nested.empty()) {
                    return nested;
                }
            }
            return {};
        }
        case TypeCode::Struct: {
            const auto *record = static_cast<const StructType *>(type);
            if (record->named()) {
                const auto found = namedTypeWhy_.find(record);
                return found != namedTypeWhy_.end() ? found->second : std::string{};
            }
            for (size_t i = 0; i < record->size(); ++i) {
                const std::string nested = unwritableInside(record->typeAt(i));
                if (!nested.empty()) {
                    return nested;
                }
            }
            return {};
        }
        case TypeCode::Function: {
            const auto *function = static_cast<const FunctionType *>(type);
            for (size_t i = 0; i < function->normTypesCount(); ++i) {
                const std::string nested = unwritableInside(function->normTypeAt(i));
                if (!nested.empty()) {
                    return nested;
                }
            }
            return function->hasExitType() ? unwritableInside(function->exitType()) : std::string{};
        }
        default:
            return {};
        }
    }

    void collectNamedTypes() {
        for (const GCGraph *graph : graphs_) {
            const FunctionType *type = graph->funcType();
            if (type != nullptr) {
                for (size_t i = 0; i < type->withTypesCount(); ++i) {
                    visitNamedTypes(type->withTypeAt(i));
                }
                for (size_t i = 0; i < type->normTypesCount(); ++i) {
                    visitNamedTypes(type->normTypeAt(i));
                }
                if (type->hasExitType()) {
                    visitNamedTypes(type->exitType());
                }
            }
            for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
                visitNamedTypes((*it)->dataType());
            }
        }
    }

    /// The constants the arms of a `match` test, collected before anything is printed: no `let` may
    /// be written for one of them, because the pattern position has to be the literal itself.
    void collectMatchPatterns() {
        for (const GCGraph *graph : graphs_) {
            for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
                if ((*it)->kind != camel::runtime::GCNodeKind::Brch) {
                    continue;
                }
                for (const gc_node_ref_t pattern : graph->withInputsOf(it.ref())) {
                    patternRefs_.insert(pattern);
                }
            }
        }
    }

    /// A type statement has to be readable: its name is a name of the printed module, so it
    /// cannot be a name the module gives something else, and two different types cannot share it.
    void checkNamedTypeNames() {
        std::map<std::string, const StructType *> byName;
        for (const StructType *record : namedTypeRecords_) {
            if (namedTypeWhy_.contains(record)) {
                continue;
            }
            const std::string name = record->decl()->name();
            if (topLevelNames_.contains(name)) {
                namedTypeWhy_[record] =
                    std::format("its name '{}' is also a function the program declares", name);
                continue;
            }
            bool imported = false;
            for (const std::string &module : moduleImports_) {
                imported = imported || module == name;
            }
            if (imported) {
                namedTypeWhy_[record] = std::format(
                    "its name '{}' is also a name the program imports", name);
                continue;
            }
            const auto [found, inserted] = byName.emplace(name, record);
            if (!inserted) {
                namedTypeWhy_[found->second] = std::format(
                    "two different types the program uses are both named '{}'", name);
                namedTypeWhy_[record] = namedTypeWhy_[found->second];
            }
        }
    }

    /// Puts the declarations in an order a `type` statement can be read in: a field type's
    /// declaration first. A type that mentions itself is the one cycle a declaration can write
    /// (`type Node = { children: Node[] }`); a cycle between two types is refused, because
    /// neither can be written before the other.
    void orderNamedTypes() {
        namedTypeOrder_.clear();
        std::vector<const StructType *> stack;
        std::unordered_set<const StructType *> onStack;
        std::unordered_set<const StructType *> placed;
        for (const StructType *record : namedTypeRecords_) {
            placeNamedType(record, stack, onStack, placed);
        }
    }

    void placeNamedType(
        const StructType *record, std::vector<const StructType *> &stack,
        std::unordered_set<const StructType *> &onStack,
        std::unordered_set<const StructType *> &placed) {
        if (record == nullptr || namedTypeWhy_.contains(record) || placed.contains(record)) {
            return;
        }
        if (!namedTypeUsed_.contains(record)) {
            return; // the program never writes its name
        }
        if (onStack.contains(record)) {
            // The type is already being declared, so this closes a cycle. It is a legal one only
            // when the type names itself: the declaration's own name is in scope before its
            // fields are read. Any other cycle has no first declaration.
            if (stack.empty() || stack.back() != record) {
                for (const StructType *member : stack) {
                    if (!namedTypeWhy_.contains(member)) {
                        namedTypeWhy_[member] =
                            "it is one of a cycle of type declarations, which has no first";
                    }
                }
                namedTypeWhy_[record] =
                    "it is one of a cycle of type declarations, which has no first";
            }
            return;
        }
        stack.push_back(record);
        onStack.insert(record);
        for (size_t i = 0; i < record->size(); ++i) {
            // Only a named type needs a declaration of its own; an array, a tuple or an anonymous
            // struct is written inside this one and its members are placed here.
            placeInside(record->typeAt(i), stack, onStack, placed);
        }
        stack.pop_back();
        onStack.erase(record);
        placed.insert(record);
        if (!namedTypeWhy_.contains(record)) {
            namedTypeOrder_.push_back(record);
        }
    }

    /// Places the named types a non-named type mentions, in order.
    void placeInside(
        const Type *type, std::vector<const StructType *> &stack,
        std::unordered_set<const StructType *> &onStack,
        std::unordered_set<const StructType *> &placed) {
        if (type == nullptr) {
            return;
        }
        switch (type->code()) {
        case TypeCode::Array:
            placeInside(static_cast<const ArrayType *>(type)->elemType(), stack, onStack, placed);
            return;
        case TypeCode::Tuple: {
            const auto *tuple = static_cast<const TupleType *>(type);
            for (size_t i = 0; i < tuple->size(); ++i) {
                placeInside(tuple->typeAt(i), stack, onStack, placed);
            }
            return;
        }
        case TypeCode::Struct: {
            const auto *record = static_cast<const StructType *>(type);
            if (record->named()) {
                placeNamedType(record, stack, onStack, placed);
                return;
            }
            for (size_t i = 0; i < record->size(); ++i) {
                placeInside(record->typeAt(i), stack, onStack, placed);
            }
            return;
        }
        default:
            return;
        }
    }

    /// The `type` statement for a named struct type: the name it is printed by, and the field
    /// table the type itself carries.
    std::string namedTypeText(const StructType *record) {
        std::string text = "type " + record->decl()->name() + " = { ";
        for (size_t i = 0; i < record->size(); ++i) {
            text += (i == 0 ? "" : ", ");
            text += std::string(record->fieldName(i)) + ": " + writtenTypeText(record->typeAt(i));
        }
        text += " }";
        return text;
    }

    /// Whether the printed program can declare `record`: the test `checkType` uses, so a type is
    /// accepted exactly when its declaration is written.
    bool namedTypePrintable(const StructType *record) const {
        return namedTypeSeen_.contains(record) && !namedTypeWhy_.contains(record);
    }

    /// Records the named types `type` writes, and the ones their declarations need: a field whose
    /// type is named makes that type's own declaration part of this one.
    void markNamedTypesUsed(const Type *type) {
        if (type == nullptr) {
            return;
        }
        switch (type->code()) {
        case TypeCode::Array:
            markNamedTypesUsed(static_cast<const ArrayType *>(type)->elemType());
            return;
        case TypeCode::Tuple: {
            const auto *tuple = static_cast<const TupleType *>(type);
            for (size_t i = 0; i < tuple->size(); ++i) {
                markNamedTypesUsed(tuple->typeAt(i));
            }
            return;
        }
        case TypeCode::Struct: {
            const auto *record = static_cast<const StructType *>(type);
            if (!record->named()) {
                for (size_t i = 0; i < record->size(); ++i) {
                    markNamedTypesUsed(record->typeAt(i));
                }
                return;
            }
            if (!namedTypeUsed_.insert(record).second || namedTypeWhy_.contains(record)) {
                return;
            }
            for (size_t i = 0; i < record->size(); ++i) {
                markNamedTypesUsed(record->typeAt(i));
            }
            return;
        }
        case TypeCode::Function: {
            const auto *function = static_cast<const FunctionType *>(type);
            for (size_t i = 0; i < function->normTypesCount(); ++i) {
                markNamedTypesUsed(function->normTypeAt(i));
            }
            if (function->hasExitType()) {
                markNamedTypesUsed(function->exitType());
            }
            return;
        }
        default:
            return;
        }
    }

    /// `typeText` for a position the program writes: the named types it names are the ones the
    /// header declares.
    std::string writtenTypeText(const Type *type) {
        markNamedTypesUsed(type);
        return typeText(type);
    }

    /// Why `record` has no declaration in the printed program, for the refusal that keeps it.
    std::string namedTypeObstacleOf(const StructType *record) const {
        const auto found = namedTypeWhy_.find(record);
        if (found != namedTypeWhy_.end()) {
            return found->second;
        }
        return "the printer did not reach it";
    }

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
        // The names of one function's block are not the names of the next: two functions may both
        // bind `base` (the corpus' `linear.cml` does), and only one block's names are in scope.
        blockNames_.clear();

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
            } else if (ifWithoutElse(tail)) {
                // A body whose last statement is an `if` with no `else`: the branch has no value
                // (design principles 3.7), and printing nothing dropped the arm that runs —
                // `if debug then println('tail')` printed as an empty body
                // (`consistency/if_without_else/statement.cml`).
                emit(indent_ + expression(tail).text);
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
        auto savedBlockNames = std::move(blockNames_);
        blockNames_.clear();
        auto savedHeld     = std::move(held_);
        auto savedHeldNames = std::move(heldNames_);
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
            if (ifWithoutElse(tail)) {
                // An `if` with no `else` has no value: the branch stands as the statement it is,
                // and no `return` stands in front of it (`IfWithoutElseAsValue`).
                flushHeld(body.lines);
                body.lines.push_back(indent_ + expression(tail).text);
            } else {
                // A region whose value is a gate with ordering in front of it was written as an
                // explicit `return`; a bare last expression yields the value without that gate.
                Expr value;
                if (tail != camel::runtime::kInvalidNodeRef && graph_->node(tail) != nullptr) {
                    value = declared_.contains(tail) ? Expr{declared_[tail], 100}
                                                     : expression(tail);
                }
                flushHeld(body.lines);
                body.lines.push_back(value.text.empty() ? indent_ + "return"
                                                        : indent_ + "return " + value.text);
            }
        } else if (tail != camel::runtime::kInvalidNodeRef && graph_->node(tail) != nullptr &&
                   !tailPrintedAsStatement) {
            body.hasTail = true;
            body.tail    = declared_.contains(tail) ? Expr{declared_[tail], 100}
                                                    : expression(tail);
            flushHeld(body.lines);
        }

        lines_     = std::move(savedLines);
        params_    = std::move(savedParams);
        declared_  = std::move(savedDeclared);
        cells_     = std::move(savedCells);
        blockNames_ = std::move(savedBlockNames);
        held_      = std::move(savedHeld);
        heldNames_ = std::move(savedHeldNames);
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

    /// Whether the value this JOIN yields is an `if` with no `else`: an arm written and an empty
    /// second arm, over a `void` join. Such an `if` has no value (design principles 3.7) and can
    /// only stand as a statement, so a region that ends in one says `if ... then ...` and never
    /// `return if ... then ...`: the front end answers that with `IfWithoutElseAsValue`
    /// (`consistency/if_without_else/statement.cml`, whose lambda body is exactly this statement,
    /// printed as `return if x > 2 then { ... }`).
    bool ifWithoutElse(gc_node_ref_t joinRef) const {
        if (joinRef == camel::runtime::kInvalidNodeRef) {
            return false;
        }
        const camel::runtime::GCNode *node = graph_->node(joinRef);
        if (node == nullptr || node->kind != camel::runtime::GCNodeKind::Join) {
            return false;
        }
        const Type *joinType = node->dataType();
        if (joinType == nullptr || joinType->code() != TypeCode::Void) {
            return false;
        }
        const auto *body = graph_->nodeBodyAs<camel::runtime::GCJoinBody>(joinRef);
        if (body == nullptr || body->brch == camel::runtime::kInvalidNodeRef) {
            return false;
        }
        // A `match` writes patterns on its branch; an `if` has none.
        if (!graph_->withInputsOf(body->brch).empty()) {
            return false;
        }
        const auto arms = control_->armsOf(body->brch);
        if (arms.size() != 2) {
            return false;
        }
        const camel::runtime::GCRegion *second = graph_->region(arms[1]);
        return second != nullptr &&
               (second->result == camel::runtime::kInvalidNodeRef ||
                (graph_->node(second->result) != nullptr &&
                 graph_->node(second->result)->kind == camel::runtime::GCNodeKind::Data &&
                 graph_->node(second->result)->dataType() != nullptr &&
                 graph_->node(second->result)->dataType()->code() == TypeCode::Void));
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
            for (const gc_node_ref_t source : graph_->waitInputsOf(result)) {
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
        if (tail == camel::runtime::kInvalidNodeRef) {
            return camel::runtime::kInvalidNodeRef;
        }
        const gc_node_ref_t exitRef = graph_->exitNodeRef();
        if (exitRef == camel::runtime::kInvalidNodeRef || graph_->node(exitRef) == nullptr ||
            graph_->node(exitRef)->kind != camel::runtime::GCNodeKind::Gate) {
            return camel::runtime::kInvalidNodeRef;
        }
        // A `sync` block writes its value out through a gate that waits on the write: the exit chain
        // is `GATE(<value>, sync <- write)` and then `GATE(<that>, control <- write)`, so the tail the
        // printer resolves is the value node at the bottom of it, not the exit's own input. Walk the
        // chain down its normal inputs and accept the tail wherever it stands on that path; the value
        // handed on is the first node on it that is not a gate.
        gc_node_ref_t current = exitRef;
        gc_node_ref_t value   = camel::runtime::kInvalidNodeRef;
        bool handsTail        = false;
        for (int guard = 0; guard < 16; ++guard) {
            if (tail == current) {
                handsTail = true;
            }
            const auto below = graph_->normInputsOf(current);
            if (below.empty()) {
                break;
            }
            const gc_node_ref_t next = below.back();
            const camel::runtime::GCNode *nextNode = graph_->node(next);
            if (nextNode != nullptr &&
                (nextNode->kind == camel::runtime::GCNodeKind::Gate ||
                 nextNode->kind == camel::runtime::GCNodeKind::Copy)) {
                current = next;
                continue;
            }
            value = next;
            break;
        }
        if (tail == value) {
            handsTail = true;
        }
        if (!handsTail || value == camel::runtime::kInvalidNodeRef) {
            return camel::runtime::kInvalidNodeRef;
        }
        return assignmentHandingOn(exitRef, 0, value);
    }

    /// The assignment a block hands a value on behind: one of `gate`'s control inputs is a write
    /// of `region` whose value is the one `gate` carries. That is the GATE the builder puts in
    /// front of the value an assignment stands for, so that the write runs where nothing else
    /// reads the version it made (design principles 3.5, 3.4): the source's block ended with the
    /// write and the block's value is what that write wrote. `kInvalidNodeRef` otherwise.
    gc_node_ref_t assignmentHandingOn(
        gc_node_ref_t gate, uint32_t region, gc_node_ref_t value) const {
        for (const gc_node_ref_t input : graph_->waitInputsOf(gate)) {
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
        if (graph_ != nullptr && constructionTemplates_.contains(graph_) &&
            constructionTemplates_.at(graph_).contains(ref)) {
            // A construction's template is a shape, not a value the program writes: the literal
            // written back builds its own (see `collectConstructionTemplates`).
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
            if (uri == ":var/get" || uri == ":var/peek") {
                if (!materialiseRead(ref)) {
                    return;
                }
                // A read the source named and the text cannot name by the variable's name: the
                // generic statement logic below writes it as the `let` it was in the source.
                break;
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
            if (uri == camel::core::kFailOperatorUri) {
                // `fail<'Kind'>(message)` is a statement of the language (design principles 3.9),
                // and the fault it raises *is* its behaviour: a program printed without it goes on
                // past the point where the source stops. That was the first case of the A7 defect
                // (`println('before'); fail<'NoPremise'>('...'); println('after')` printed as
                // `before`, `after`, exit 0). The call is no effect, so the walk that prints
                // effects never meets it as one; it is written here, where the statement stands.
                emitFail(ref);
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
            // Two nodes of one block with one source name: the printed program would declare the
            // name twice in that block, which is a `Redeclaration`. A `static for` over the fields
            // of a type expands one source binding once per field and the graph keeps the name on
            // every one of them (`parse/rulings/static_for_head_names.cml` printed `let v` twice).
            if (!blockNames_.insert(name).second) {
                fail(
                    ref,
                    std::format(
                        "two bindings of the name '{}' in one block: one expansion of a `static "
                        "for` keeps the source name on every copy, and a block cannot declare one "
                        "name twice",
                        name));
            }
            const Expr value       = expression(ref);
            declared_[ref]         = name;
            emit(std::format(
                "{}let {}{} = {}", indent_, name, annotationOf(node->dataType()), value.text));
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
                    "{}let {}{} = {}", indent_, name, annotationOf(node->dataType()), value.text));
            }
            // A single use keeps the call where the source had it: lifting it into a `let` would
            // move the effect ahead of the expression it belongs to -- unless the one user is
            // version plumbing. A call whose result is used as the cell a later write stores
            // through (`GATE` -> `:var/peek` -> `:op/assn*`) is never written by that user: the
            // plumbing prints a variable name, and the call's text died with it. The call then is
            // a statement of its own, where it stands (`consistency/values/closure_var`: `twice(x)`
            // and `add(100)` vanished, and the printed program answered 1, 2, 4 where the source
            // answers 3, 24, 249).
            //
            // The result needs no name here: the write that follows names the variable, not the
            // call's version, so the statement form is the whole of it.
            else if (feedsVersionPlumbing(ref)) {
                branchAsStatement_ = true;
                const std::string text = expression(ref).text;
                branchAsStatement_     = false;
                emit(indent_ + text);
                if (ref == tail) {
                    tailPrintedAsStatement = true;
                }
            }
        } else if ((users > 1 || needsAnnotation(ref)) && !typeWrittenByUse(ref) &&
                   !(patternRefs_.contains(ref) && users <= 1)) {
            // An empty literal whose element type nobody wrote cannot be bound: `let x = []` is
            // `EmptyArrayWithoutType` and `let x: [] = []` is not a type. One use keeps the literal
            // in the position that types it; two uses have no spelling at all.
            if (node->kind == camel::runtime::GCNodeKind::Data && node->dataType() != nullptr &&
                camel::core::type::isUntypedArrayType(node->dataType())) {
                fail(
                    ref,
                    "an empty array literal whose element type nobody wrote, read in more than "
                    "one place: the position types one use, and the language has no spelling of "
                    "that type for a `let`");
            }
            const Expr value       = expression(ref);
            const std::string name = freshName();
            declared_[ref]         = name;
            emit(std::format(
                "{}let {}{} = {}", indent_, name, annotationOf(node->dataType()), value.text));
        } else if (users == 0) {
            // A value nothing reads is not part of the printed program -- but a region is not a
            // value. A `loop` or an `if` the source wrote is program structure, and a region whose
            // value nothing reads still runs its parts: dropping it changes no value that anything
            // answers, which is exactly why it is tempting, and it is still wrong, because the
            // printed text then is not the program. `linear/runtime/var_read_before_region.cml`'s
            // `nothing_written` lost a whole LOOP this way: 15 vertices and 2 regions on the left,
            // 5 vertices and 0 regions on the right, and the round trip stopped being an
            // isomorphism. The rule is the graph's own: a region in the graph is printed.
            const camel::runtime::GCNodeKind region = node->kind;
            if (region == camel::runtime::GCNodeKind::Loop ||
                region == camel::runtime::GCNodeKind::Brch) {
                // The statement form the source writes: `loop ... do ...` / `if ... then ...`. A
                // JOIN is not written here -- it is the branch's merge point and `expression` of
                // the BRCH already writes it.
                branchAsStatement_ = true;
                const std::string text = expression(ref).text;
                branchAsStatement_     = false;
                emit(indent_ + text);
                if (ref == tail) {
                    tailPrintedAsStatement = true;
                }
                return;
            }
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
        } else if (!blockNames_.insert(name).second) {
            // The same rule as a `let`: a block cannot declare one name twice.
            fail(
                ref,
                std::format(
                    "two bindings of the name '{}' in one block: a `var` and the bindings beside "
                    "it share the source's names, and a block declares one name once",
                    name));
        }
        const Expr initial = expression(inputs.front());
        cells_[ref]        = name;
        declared_[ref]     = name;
        emit(std::format(
            "{}var {}{} = {}", indent_, name, annotationOf(valueType), initial.text));
    }

    /// Gives a read of a `var` a name of its own when the variable's name would not say what it
    /// reads. The name in the text means "the version the text has written so far", so a read the
    /// builder ordered before a write (`recordWrite`: "the read has to see the value as it was
    /// before the write changes it") is a version the text has not reached yet at that place:
    /// `println(before, z)` after a loop that writes `z` printed `z` twice (the A7 cases
    /// `var_read_before_region`, `var_read_before_write`, `order_edges_regions`). The read is
    /// written as a `let` where it stands, and the name the source gave it comes back:
    ///
    ///     var z = 0
    ///     let cml1 = z        // the read, at the place it stands
    ///     loop ...
    ///     println(cml1, z)    // the old version, then the one the loop left
    ///
    /// No version number enters the text: the position of the `let` is what carries the order, and
    /// a printed program is a program -- it has to answer the same values, and it does.
    /// Answers true when the read should be written by the generic statement logic as a `let`: the
    /// source named it, and the variable's name would not say the version it reads.
    bool materialiseRead(gc_node_ref_t ref) {

        const auto inputs = graph_->normInputsOf(ref);
        if (inputs.size() != 1) {
            return false;
        }
        // The order edge to the write it must see past is what makes the read's version older than
        // the text at the place the read stands. A read whose value path already carries that order
        // (`n + 1` reading `n` before writing it) has no such edge, and the variable's name says it.
        //
        // An arm's or a loop body's nodes are in the block's own graph -- `controlOf` says which
        // region lays them out, not which graph records their versions -- so a read inside an arm
        // and the write it must see past are the same two nodes of the same graph as a read and a
        // write of the block (verified on `arm_inner`: `N0C -> N10 [ORDER]`, both `controlOf` the
        // BRCH), and the same `recordWrite` wrote the edge. That is why this rule reads
        // `orderUsersOf` and not "the read is in a region": the fact is one, and its spelling in
        // the text is one: the `let` goes inside the arm where the read stands.
        if (graph_->orderUsersOf(ref).empty()) {
            return false;
        }
        if (declared_.contains(ref)) {
            return false;
        }
        if (!sourceName(ref).empty()) {
            return true;
        }
        const camel::runtime::GCNode *node = graph_->node(ref);
        const std::string name             = freshName();
        declared_[ref]                     = name;
        emit(std::format(
            "{}let {}{} = {}", indent_, name, annotationOf(node->dataType()),
            cellName(cellOf(inputs.front()))));
        return false;
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
        // The cell's name is copied, not referred to. Reading the value's expression can restore a saved
        // copy of `cells_` (a nested attempt that does not print puts the table back), and putting it
        // back destroys every entry the map held -- so a reference taken before it points at freed
        // storage. That is what the crash was: the probe showed the name's size as garbage while the
        // value's text and the operator were intact.
        const std::string cellName  = cell->second;
        const std::string valueText = expression(inputs.back()).text;
        emit(indent_ + std::format("{} {} {}", cellName, assignmentOperator(uri), valueText));
    }

    /// `fail<'Kind'>(message)` as a statement. The kind is the constant string the call holds in
    /// its `with` slot and the message is its one norm input; a `fail` whose kind is not a constant
    /// has no spelling the printer can write -- `fail<'Kind'>` names the kind, and there is no
    /// other place to put it -- so that shape is refused by name rather than printed as a `fail`
    /// of some other kind (a printing that loses the kind is the false printing this cut is about).
    void emitFail(gc_node_ref_t ref) {
        const auto with = graph_->withInputsOf(ref);
        const auto norm = graph_->normInputsOf(ref);
        if (with.size() != 1 || norm.size() != 1) {
            fail(ref, "a 'fail' whose kind and message are not both written");
        }
        const camel::runtime::GCNode *kind = graph_->node(with.front());
        if (kind == nullptr || kind->kind != camel::runtime::GCNodeKind::Data ||
            kind->dataIndex >= 0 || kind->dataType() == nullptr ||
            kind->dataType()->code() != TypeCode::String) {
            fail(
                ref,
                "a 'fail' whose kind is not a constant string: the statement is written "
                "`fail<'Kind'>` and the kind is the value it holds, so a print that cannot read "
                "it cannot write the statement");
        }
        emit(indent_ + "fail<" + expression(with.front()).text + ">(" +
             expression(norm.front()).text + ")");
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
            // The entrance of an unnamed struct value into a named struct type is a conversion the
            // compiler inserts by itself wherever the type is known -- a return type, an annotated
            // `let`, a parameter, a field. Writing it is not the same graph: `return ({ x: 0 } as
            // V)` folds to a literal of `V`, while the source has the literal and the conversion,
            // so the printed program is one node short (`consistency/operator_overloads`
            // `plus_b.cml`). The cast is dropped and the position it stands in supplies the type;
            // a conversion between two named types (which the language only writes) stays.
            const camel::runtime::GCNode *source = graph_->node(inputs.front());
            const Type *from = source != nullptr ? source->dataType() : nullptr;
            // A `T` and a `T[..]` are the same value (design principles 5.4): the run keeps the
            // rank-zero array in the slot and the element in the type, and the conversion between
            // them is the compiler's, supplied by whatever position the value stands in. It is
            // not a program's `as` -- `as` between the two is `DynamicCastForbidden` -- so writing
            // one is not a stronger statement, it is a program that does not compile:
            // `compile/array_shape/rank_zero_parameter_slot` printed `(v as f32)` in the body and
            // `elem((cml1 as f32[...]))` at the call, and neither cast is in the source.
            if (from != nullptr &&
                camel::core::type::isRankZeroEdge(
                    const_cast<Type *>(node->dataType()), const_cast<Type *>(from))) {
                return inner;
            }
            if (from != nullptr && from->code() == TypeCode::Struct &&
                static_cast<const StructType *>(node->dataType())->named() &&
                camel::core::type::relateStructs(
                    static_cast<const StructType *>(node->dataType()),
                    static_cast<const StructType *>(from)) !=
                    camel::core::type::StructRelation::None) {
                // ... but only when the value already *is* the type, field for field: the position
                // supplies the conversion, and a position does not build the fields of a value
                // that has none. A struct with no fields converted to one with a field has no
                // spelling at all (`({  } as Inner)` is `LiteralNotRepresentable`,
                // `compile/array_shape/rank_zero_slot_nested_field.cml`), so it is refused rather
                // than written as the empty literal it is not.
                const auto *target = static_cast<const StructType *>(node->dataType());
                const auto *value  = static_cast<const StructType *>(from);
                bool sameFields    = target->size() == value->size();
                for (size_t i = 0; sameFields && i < target->size(); ++i) {
                    sameFields = std::string_view(target->fieldName(i)) == value->fieldName(i) &&
                                 target->typeAt(i) == value->typeAt(i);
                }
                if (sameFields) {
                    return inner;
                }
                fail(
                    ref,
                    std::format(
                        "a conversion of '{}' to the named type '{}': the two do not have the same "
                        "fields, and the language has no spelling for the conversion itself",
                        value->toString(),
                        target->toString()));
            }
            return {
                std::format("({} as {})", parenthesize(inner, 80), writtenTypeText(node->dataType())),
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
            const bool emptyElse = ifWithoutElse(joinRef);
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
        prepareGraph(lambda, false);

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
            text += (i ? ", " : "") + name + ": " + writtenTypeText(type->normTypeAt(i));
        }
        text += ")";
        if (type->hasExitType()) {
            text += ": " + writtenTypeText(type->exitType());
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
            const std::string name = callNameOf(function->graph());
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
                function != nullptr ? callNameOf(function->graph()) : std::string{};
            if (name.empty()) {
                fail(ref, "a closure of an anonymous function (a lambda)");
            }
            return {name, 100};
        }

        std::vector<std::string> items;
        // An array construction whose slots are all constants *written as literals* has no faithful
        // spelling: the front end folds such a literal into a *constant* array (one `DATA`), not
        // into a construction, so the program written back builds a different graph -- the same
        // value in another shape (measured: `[1, 2]`, `[[1.0, 2.0], [2.0, 2.0]]` and
        // `[-9223372036854775808]` all come back as one `DATA`, while `[1.0 + 1.0]` stays a `FILL`).
        // That is a fact about the language, not about this printer, and printing it anyway would
        // put a language fact into `not_equivalent`, which is where a printing this printer got
        // wrong belongs. A construction written back with a *name* in a slot keeps its shape (the
        // front end does not fold a variable), so the check is on the text of the items, not only
        // on their nodes: `[[1.0, x], [x, 2.0]]` is a construction and prints.
        const auto isLiteralText = [](const std::string &text) {
            if (text.empty()) {
                return false;
            }
            const unsigned char first = static_cast<unsigned char>(text.front());
            return std::isdigit(first) != 0 || text.front() == '-' || text.front() == '+' ||
                   text.front() == '\'' || text == "true" || text == "false";
        };
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
        // The value each slot holds, so a check on the *value* (a constant with no plain literal
        // spelling) does not have to read the written node again.
        std::vector<slot_t> values(count, 0);
        const auto slots = body->slots();
        for (size_t i = 0; i < with.size() && i < slots.size(); ++i) {
            const size_t at = static_cast<size_t>(slots[i]);
            if (at >= count) {
                fail(ref, "a construction slot outside its type");
            }
            items[at]  = expression(with[i]).text;
            filled[at] = true;
            // A field of a struct type written with a struct value of another shape: the position
            // converts a literal that *is* the field's type (`{ x: 1, y: 2 }` as a `Point`), and it
            // converts nothing else. A value with no fields where the field has one has no
            // spelling here at all and would be printed as the empty literal it is not
            // (`compile/array_shape/rank_zero_slot_nested_field.cml` printed `{ inner: {  } }` for
            // an `Inner`, and the front end answered `DeclaredTypeMismatch`).
            if (type->code() == TypeCode::Struct) {
                const auto *record    = static_cast<const StructType *>(type);
                const camel::runtime::GCNode *written = graph_->node(with[i]);
                const Type *writtenType = written != nullptr ? written->dataType() : nullptr;
                const Type *fieldType   = record->typeAt(at);
                if (writtenType != nullptr && fieldType != nullptr &&
                    writtenType->code() == TypeCode::Struct &&
                    fieldType->code() == TypeCode::Struct) {
                    const auto *writtenRecord = static_cast<const StructType *>(writtenType);
                    const auto *fieldRecord    = static_cast<const StructType *>(fieldType);
                    bool sameFields            = writtenRecord->size() == fieldRecord->size();
                    for (size_t k = 0; sameFields && k < fieldRecord->size(); ++k) {
                        sameFields =
                            std::string_view(writtenRecord->fieldName(k)) ==
                                fieldRecord->fieldName(k) &&
                            writtenRecord->typeAt(k) == fieldRecord->typeAt(k);
                    }
                    if (!sameFields) {
                        fail(
                            ref,
                            std::format(
                                "the field '{}' is written with a value of type '{}', which is not "
                                "'{}': a literal becomes the field's type by having its fields, and "
                                "this one has no spelling for that conversion",
                                record->fieldName(at),
                                writtenType->toString(),
                                fieldType->toString()));
                    }
                }
            }
            const camel::runtime::GCNode *written = graph_->node(with[i]);
            if (written != nullptr && written->kind == camel::runtime::GCNodeKind::Data &&
                written->dataIndex < 0) {
                values[at] = graph_->staticArea()->get<slot_t>(-written->dataIndex);
            }
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
            items[i]  = constantText(element, fieldType);
            values[i] = element;
        }
        if (type->code() == TypeCode::Array) {
            // The literal is folded into a constant array only when every item is a literal *and*
            // the elements are the plain scalars whose literals the folder reads: a construction
            // written back with a name, an operator or a complex literal in a slot keeps its shape.
            bool allLiteral = !with.empty();
            for (size_t i = 0; allLiteral && i < items.size(); ++i) {
                allLiteral = isLiteralText(items[i]);
            }
            // And it only matters when an element has no plain literal spelling: `-2^63` is the one
            // an int64 cannot write as a magnitude, so it is written as an expression and the front
            // end folds the whole literal into a constant array -- the construction comes back as a
            // different graph (the only case in the corpus is `[-9223372036854775807 - 1]`).
            bool noPlainLiteral = false;
            if (allLiteral) {
                for (size_t i = 0; i < values.size(); ++i) {
                    const slot_t value = values[i];
                    if (elementType != nullptr && elementType->code() == TypeCode::Int64 &&
                        fromSlot<int64_t>(value) == std::numeric_limits<int64_t>::min()) {
                        noPlainLiteral = true;
                    } else if (elementType != nullptr && elementType->code() == TypeCode::Int32 &&
                               fromSlot<int32_t>(value) == std::numeric_limits<int32_t>::min()) {
                        noPlainLiteral = true;
                    }
                }
            }
            if (allLiteral && noPlainLiteral) {
                fail(
                    ref,
                    "a construction whose slots are constants with no plain literal spelling has no "
                    "faithful spelling: the value is written as an expression, the front end folds "
                    "the literal into a constant array, and the program written back builds a "
                    "different graph");
            }
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
            // The cell the read reads, not the version node: a version is a write or a
            // `:var/after` (what a call through a `var` parameter or a captured variable left
            // behind), and both end at the `:var/new` that made the cell. Stopping at the write
            // left `:var/after` nodes unnamed, and `cellName` then invented a `cml` name for a
            // cell no declaration ever introduced (`linear/nvm/var_params` printed `println(cml6)`
            // with nothing named `cml6`).
            return {cellName(cellOf(inputs.front())), 100};
        }
        if (uri.starts_with(":var/")) {
            fail(ref, std::format("the operator '{}'", uri));
        }
        if (uri == ":op/idx_arr" || uri == ":op/idx_str" || uri == ":op/idx") {
            if (inputs.size() != 2 && inputs.size() != 3) {
                fail(ref, "an index without a value and an index");
            }
            // The language's `[]` takes an array or a string; a subscript on a struct exists only
            // because the program declared an overload with its own signature, and a body-less
            // declaration is not written by this printer. The printed program would resolve `[]`
            // to the built-in and refuse the call (`compile/native/idx_field_missing.cml`: the
            // source says so itself, and `s['y']` is `NoMatchingFunction` when recompiled).
            const camel::runtime::GCNode *indexed = graph_->node(inputs[0]);
            const Type *indexedType = indexed != nullptr ? indexed->dataType() : nullptr;
            if (indexedType != nullptr && indexedType->code() == TypeCode::Struct) {
                fail(
                    ref,
                    "a subscript on a struct: the '[]' of the language takes an array or a string, "
                    "so this one comes from a declaration in this program, and the printed program "
                    "would resolve '[]' to the built-in and refuse the call");
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

        // An operator whose declaration is a function is printed as a call of that function, and
        // the name and the argument count come from the declaration (`:io/println`, `tensor:sum`,
        // `jvp`). No name is derived from the shape of the URI: this table is the only source, so
        // `:io/println`, the conversions of `:op/*` and `:struct/range` are printed the way their
        // own declarations name them.
        std::string name;
        const auto callForm = camel::core::OperatorTable::instance().callFormOf(uri);
        // A name is written when the program can write it: an operator of the core (`:io/println`,
        // the conversions of `:op/*`) has a name every program has in scope, and an operator whose
        // own module declared its call shape has a name that module exports (this is the `jvp(f)`,
        // `transpose(f)` and `split(f)` of `transforms`). A shape read off a Camel declaration is
        // the callee's name and nothing more -- the declaration may be a kernel the module keeps
        // private (`#impl(sum_kernel)`) -- so such an operator of a module is still refused.
        // `:op/` is the language's own namespace for operators: the symbol of every group there is
        // fixed by the language, so a program writes `+` wherever it is -- not `__add__`, which is
        // the group's internal name. An operator of a module is spelled by the name that module
        // exports, so the declaration's name is what a program writes for those. The test is who
        // owns the name, not whether a symbol exists and not how the name was derived.
        const bool languageOwned =
            uri.starts_with(":op/") &&
            camel::core::operatorSymbolOf(
                camel::core::OperatorTable::instance().groupOf(uri)) != nullptr;
        const bool writable = !languageOwned && callForm.has_value() &&
                              (callForm->declaredByModule || callForm->exported ||
                               uri.starts_with(":")) &&
                              // The name has to mean the same thing in the printed file. A program
                              // that declares a function of that name shadows the module's, and a
                              // bare `map<f>(xs)` would then call the program's own `map`: measured
                              // on `consistency/comprehension/shadowed.cml`, whose `map` returns a
                              // string and where the printed call only failed because the argument
                              // types happened not to fit.
                              !writtenFunctionNames_.contains(callForm->name);
        if (writable) {
            // A module operator is written qualified (`tensor::sum`), which is the only spelling that
            // names one operator without depending on what else the printed file imports: a bare
            // `sum` is the language's own function as well, and a bare `tanh` is `math`'s. Which
            // module that is comes from `moduleNameOf`, not from the URI's prefix: `:mark/...` is
            // where `unordered_reduce` is implemented and `para` is where it is declared. An
            // operator of the core (`:io/println`, `:op/add_l`) has no module to qualify with and
            // keeps its plain name.
            const std::string owner = moduleNameOf(uri);
            name = owner.empty() ? callForm->name : owner + "::" + callForm->name;
        } else if (callForm.has_value() && callForm->kernelDeclaration && !languageOwned &&
                   !callForm->exported) {
            // There is a declaration and it has a name, but the module did not export it: a
            // body-less declaration the module keeps to itself is the kernel behind an `#impl`
            // function (`array:sum_l` is the `sum_kernel` behind `array::sum`), and a program of
            // the language cannot write that name. A name whose module *does* export it is a call
            // (`shl` is exported with no body, and `1->shl<4>` is what the source writes), so the
            // test is the export and not the body: "body-less" says where the implementation is,
            // "private" says whether a program may write the name, and they are different facts
            // (the author's ruling of 2026-11-03; design principles 6.4).
            fail(
                ref,
                std::format(
                    "the operator '{}' without a printable name: '{}' is a body-less kernel "
                    "declaration, and no program writes that name",
                    uri,
                    callForm->name));
        } else if (callForm.has_value() &&
                   (callForm->declaredByModule || callForm->exported) &&
                   writtenFunctionNames_.contains(callForm->name)) {
            fail(
                ref,
                std::format(
                    "the operator '{}' is written by the name '{}', and this program declares a "
                    "function of that name too: the printed file would call the program's one",
                    uri,
                    callForm->name));
        } else {
                // What an operator node is written as comes from where the operator was declared,
                // not from a table here: the group it was registered in (`__add__`, `__neg__`,
                // `__idx__`) is the name the language writes for it, and `operatorSymbolOf` turns
                // that name into the symbol a program writes (design principles 4.3). A group that
                // is not an operator of the language (`tensor:sum`, `:io/println`) has no symbol,
                // and an operator nobody declared is in no group at all: both are refused by name
                // rather than given a spelling this printer would have had to guess.
                const std::string group = camel::core::OperatorTable::instance().groupOf(uri);
                // A view, not the `const char *`: every comparison below is a comparison of the
                // text, and `symbol == "-"` on two pointers would be a comparison of addresses.
                const char *declared = camel::core::operatorSymbolOf(group);
                if (declared == nullptr) {
                    fail(ref, std::format("the operator '{}'", uri));
                }
                const std::string_view symbol = declared;
                if (inputs.size() == 2) {
                    const int precedence = precedenceOf(symbol);
                    if (precedence < 0) {
                        // A binary symbol this printer has no precedence for (`[]`, `@`, `=`) is
                        // written by a construct of its own or not at all: it is refused here
                        // rather than given parens this printer would have had to invent an
                        // association for.
                        fail(ref, std::format("the operator '{}'", uri));
                    }
                    return infix(ref, inputs, symbol, precedence);
                }
                if (inputs.size() == 1) {
                    const bool unary = symbol == "-" || symbol == "~" || symbol == "!";
                    if (!unary) {
                        fail(ref, std::format("the operator '{}'", uri));
                    }
                    return prefix(ref, inputs, symbol);
                }
                fail(ref, std::format("the operator '{}' without one or two values", symbol));
            }

        // A call with configuration writes it back in `<>`, in the position the source wrote it:
        // `print<'{}-{}'>(3, 4)`, `println<'{:.2f}'>(0.5)`. The format is a `with` input of the
        // call (`:io/print` and `:io/println` carry `<fmt: string>`), and a print that dropped it
        // wrote `3 4` where the source wrote `3-4` -- the A7 case `trans/cpp/exec/print_formats`.
        // A configuration the printer cannot write is refused by name through `expression`, never
        // written half-way.
        std::string text = name;
        const auto withArgs = graph_->withInputsOf(ref);
        if (!withArgs.empty()) {
            // The one configuration this printer used to write was the format of a print:
            // `print<'{}-{}'>`, `println<'{:.2f}'>` -- a string the call holds and the source writes
            // in `<>`. A declared operator that states a `with` parameter of its own (`shl` is
            // `with <count: int>`, written `1->shl<4>`) holds a value there, and a value the
            // printer can write is written: a literal or a string. Anything else (`:mark/map_arr`'s
            // function value, written `xs->map<f>` by the source) has no spelling here, and writing
            // the call without it would be a print that dropped the configuration: it is refused by
            // name instead.
            for (const gc_node_ref_t one : withArgs) {
                const camel::runtime::GCNode *argument = graph_->node(one);
                const Type *type = argument != nullptr ? argument->dataType() : nullptr;
                const bool literal =
                    argument != nullptr && argument->kind == camel::runtime::GCNodeKind::Data &&
                    argument->dataIndex < 0;
                if (type == nullptr || (type->code() != TypeCode::String && !literal)) {
                    fail(
                        ref,
                        "a call whose configuration is not a format string: the printer writes "
                        "`name<fmt>(...)` for the print family, and this operator's configuration "
                        "is not a string, so its call has no spelling here");
                }
            }
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
        return {text, 90};
    }

    /// How tightly a symbol binds, so that the parentheses this printer writes say what the graph
    /// says. The declaration says which symbol an operator is; how tightly it binds is the
    /// parser's business, and this is the one place the printed text needs it. A symbol this list
    /// does not know comes back -1 and is written fully parenthesized, which is never wrong.
    static int precedenceOf(std::string_view symbol) {
        if (symbol == "+" || symbol == "-") {
            return 40;
        }
        if (symbol == "*" || symbol == "/" || symbol == "%" || symbol == "@") {
            return 50;
        }
        if (symbol == "^") {
            return 60;
        }
        if (symbol == "==" || symbol == "!=" || symbol == "<" || symbol == "<=" || symbol == ">" ||
            symbol == ">=") {
            return 30;
        }
        if (symbol == "&") {
            return 20;
        }
        if (symbol == "|") {
            return 19;
        }
        return -1;
    }

    Expr infix(
        gc_node_ref_t ref, std::span<const gc_node_ref_t> inputs, std::string_view op,
        int precedence) {
        if (inputs.size() != 2) {
            fail(ref, std::format("the operator '{}' without two values", op));
        }
        Expr left;
        Expr right;
        // Two literals fold on the way back, and the folded node is not the nodes this graph holds:
        // each literal is bound first, so the operator survives with the operands it has. A name is
        // not a literal, and a `let` of a literal is that literal's node and nothing else, so the
        // count of nodes on both sides stays what it is.
        left  = expression(inputs[0]);
        right = expression(inputs[1]);
        if (isLiteralText(left.text) && isLiteralText(right.text)) {
            left  = {hoistConstant(inputs[0], left.text), 100};
            right = {hoistConstant(inputs[1], right.text), 100};
        }
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
        // The same fold, one operand wide: `-1` comes back as the constant, not as the node that
        // negated it.
        if (isLiteralText(inner.text) && inner.text.front() != '-') {
            inner = {hoistConstant(inputs[0], inner.text), 100};
        }
        std::string text = parenthesize(inner, 70);
        // `!!t` is not two negations to the lexer: `!!` is one token that fits nothing, so an
        // operand that opens with the operator's own character is parenthesized (`!(!t)`).
        if (text.starts_with(op)) {
            text = std::format("({})", text);
        }
        return {std::format("{}{}", op, text), kPrefixPrecedence};
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
        // function's own `with` parameters come first, and a call spells them inside `<...>`.
        const std::string name = callNameOf(callee);
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

    /// The text of an array constant: one bracket per extent of the value's own shape. `rank` is the
    /// number of extents `dim` still has to write, `dim` the one being written, and `next` the
    /// linear element the walk has reached -- the elements are contiguous in row-major order, so
    /// the recursion never has to multiply strides out.
    std::string arrayText(
        const Array *value, const ArrayType *arrayType, size_t rank, size_t dim, size_t &next) {
        if (rank == 0) {
            // A rank-zero array is the element itself (design principles 5.4).
            return constantText(value->getSlot(0), arrayType->elemType());
        }
        const size_t length = static_cast<size_t>(value->dim(dim));
        std::string text    = "[";
        for (size_t i = 0; i < length; ++i) {
            if (i != 0) {
                text += ", ";
            }
            if (dim + 1 == rank) {
                text += constantText(value->getSlot(next++), arrayType->elemType());
            } else {
                text += arrayText(value, arrayType, rank, dim + 1, next);
            }
        }
        text += "]";
        return text;
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
            if (value == nullptr) {
                return "[]";
            }
            const auto *arrayType = static_cast<const ArrayType *>(type);
            // One bracket per extent, not one bracket per slot: `size()` is the count of a single
            // row for a value of more than one extent, so a `f32[2, 2]` constant is `[[1.0, 2.0],
            // [3.0, 4.0]]` and not `[1.0, 2.0]`. The elements lie in row-major order, so the walk
            // is linear and only the brackets come from the shape.
            size_t next = 0;
            return arrayText(value, arrayType, static_cast<size_t>(value->rank()), 0, next);
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
            // A template value of a struct type that holds no struct: the fields the type has are
            // not in the value, so the literal written from it is `{  }` -- an empty struct where
            // the type has fields, which the front end refuses (`DeclaredTypeMismatch`,
            // `compile/array_shape/rank_zero_slot_nested_field.cml`). No spelling of the type is a
            // value that has none of its fields.
            if (value == nullptr && record->size() > 0) {
                fail(
                    camel::runtime::kInvalidNodeRef,
                    std::format(
                        "a template value of '{}' that holds no struct: the literal written from it "
                        "would be an empty struct where the type has {} field(s)",
                        record->toString(),
                        record->size()));
            }
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
        const std::string name   = printedNameOf(graph);
        // A function a program declared inside a namespace is printed inside that `namespace` block,
        // under its own name: the graph name is the qualified one (`Tools::inc`), and the source
        // wrote the block, not the qualified name (`compile/namespace_rules/lib2.cml`).
        std::vector<std::string> spaceParts;
        headerIndent_.clear();
        if (isNamespaceName(name)) {
            size_t start = 0;
            for (;;) {
                const size_t at = name.find("::", start);
                if (at == std::string::npos) {
                    break;
                }
                spaceParts.push_back(name.substr(start, at - start));
                start = at + 2;
            }
            for (size_t i = 0; i < spaceParts.size(); ++i) {
                emit(std::string(4 * i, ' ') + "namespace " + spaceParts[i] + " {");
            }
            indent_ = std::string(4 * (spaceParts.size() + 1), ' ');
            headerIndent_ = std::string(4 * spaceParts.size(), ' ');
        }
        const std::string localName =
            spaceParts.empty() ? name : name.substr(name.rfind("::") + 2);
        // A function a program declared as an operator is stored under the operator's reserved
        // name (`func operator +` builds the graph `__add__`), and that name is not source a
        // program can write back: `__add__` is a reserved identifier (`ReservedIdentifier`). The
        // symbol the declaration used is the name the program writes, so `operator +` is printed.
        std::string writtenName = localName;
        if (const char *symbol = camel::core::operatorSymbolOf(localName); symbol != nullptr) {
            writtenName = std::string("operator ") + symbol;
        }
        std::string signature;
        if (type->withTypesCount() > 0) {
            std::string clause = "with <";
            for (size_t i = 0; i < type->withTypesCount(); ++i) {
                std::string withName(type->argNameAt(i));
                if (!isIdentifier(withName)) {
                    withName = std::format("w{}", i);
                }
                clause += (i ? ", " : "") + withName + ": " + writtenTypeText(type->withTypeAt(i));
            }
            clause += ">";
            emit(headerIndent_ + clause);
        }
        signature += "func " + writtenName + "(";
        for (size_t i = 0; i < type->normTypesCount(); ++i) {
            signature += i ? ", " : "";
            if (type->normIsVarAt(i)) {
                signature += "var ";
            }
            std::string param(type->argNameAt(type->withTypesCount() + i));
            if (!isIdentifier(param)) {
                param = std::format("p{}", i);
            }
            signature += param + ": " + writtenTypeText(type->normTypeAt(i));
        }
        signature += ")";
        if (type->hasExitType()) {
            signature += ": " + writtenTypeText(type->exitType());
        }
        // A function graph whose body has a control edge between its own nodes was written with
        // a body that is a sync block: the modifier says so, and it is the only mark of it.
        if (regionNeedsSync(0)) {
            signature += " sync";
        }
        emit(headerIndent_ + signature + " {");
        isRoot_ = false;
        printStatements();
        emit(std::string(4 * spaceParts.size(), ' ') + "}");
        for (size_t i = spaceParts.size(); i > 0; --i) {
            emit(std::string(4 * (i - 1), ' ') + "}");
        }
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
        CmlPrinter printer(
            *program, context_.get(), rendered, options_.maxLiteralBytes, bigConstants);
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

