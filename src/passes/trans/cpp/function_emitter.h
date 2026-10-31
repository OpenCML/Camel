// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * One graph of the program as one C++ function (docs/design/cpp-standalone/README.cn.md,
 * section 6).
 *
 * The statements follow the execution order std::fvm compiles the graph in
 * (ExecPlan::topo): only live nodes, every edge (data, `with`, control)
 * respected, the arms of each branch laid out as contiguous ranges. A branch is an if /
 * else-if / else statement at the place of its BRCH, its arms the statements of their nodes:
 * in the form the front end gives (each arm a graph of its own, called by a FUNC node) the arm
 * graph is lowered into the function in place of the call; in the form std::opt::inline leaves (the
 * arm's nodes between the BRCH and the JOIN) the range of the arm is.
 *
 * A call of the function itself whose value is the function's value, with nothing after it, is
 * a jump back to the start: the body is a loop and the call assigns the parameters. Every
 * other call passes the count of open calls on (camel/standalone/core.h, rt::call), which
 * checks the recursion limit at the call.
 */

#pragma once

#include "camel/core/operator_table.h"
#include "camel/runtime/control_regions.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"

#include "code.h"
#include "cpp_types.h"
#include "fault_sites.h"
#include "operator_rules.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace camel::passes::cpp {

using camel::runtime::gc_node_ref_t;
using camel::runtime::GCGraph;

/// A construct of the program that std::cpp does not translate: the pass reports it as
/// TranslationRefused at the construct (or at the function, for its signature).
struct Refusal {
    const GCGraph *graph = nullptr;
    gc_node_ref_t node   = camel::runtime::kInvalidNodeRef;
    std::string reason;
    bool aboutFunction = false;
};

/// What every function of a program shares while it is written.
struct ProgramParts {
    /// The program the graphs are of: it names the graphs a graph calls.
    const camel::runtime::Program *program = nullptr;
    ProgramValues values;
    SiteTable sites;
    /// The C++ name of each graph that is a function of the generated program.
    std::unordered_map<const GCGraph *, std::string> functionNames;
    /// Operators that go through the runtime module that implements them, by URI: the name
    /// of the generated accessor. Empty while the program is standalone.
    std::unordered_map<std::string, std::string> bridgedOperators;
    /// The root graph when it is a function of the program (it computes values of the top
    /// level before it calls `main`), and its call of `main`. Neither that call nor a call
    /// that computes a top-level value counts towards the recursion limit
    /// (camel/execute/limits.h, kEntryCalls).
    const GCGraph *root      = nullptr;
    gc_node_ref_t entryCall  = camel::runtime::kInvalidNodeRef;
    const GCGraph *entryMain = nullptr;
    /// Where the first of them is used: the warning that the program links the runtime is
    /// located there.
    const GCGraph *firstBridgedGraph = nullptr;
    gc_node_ref_t firstBridgedNode   = camel::runtime::kInvalidNodeRef;
    /// The limit on open calls the program is translated with (camel/execute/limits.h): the
    /// reports of the calls that meet it name it, and the generated program's `run` gets it.
    size_t recursionLimit = 1000;

    explicit ProgramParts(camel::core::context::context_ptr_t context)
        : sites(std::move(context)) {}
};

/// "function 'f'" or "an anonymous function", as refusals name a graph.
std::string describeFunction(const GCGraph *graph);

/// The URI of the operator of the OPER node `ref`: what the rule table (operator_rules.h) and
/// the messages name it by.
std::string operatorUri(const GCGraph *graph, gc_node_ref_t ref);

/// The operator-table id of the OPER node `ref`: what the operators the translation treats
/// itself (variables, the `:mark/` operators) are recognised by.
camel::core::OperId operatorId(const GCGraph *graph, gc_node_ref_t ref);

class FunctionEmitter {
  public:
    /// One function (`members` has one graph), or a group of functions that call each other
    /// in tail position: their bodies run in one loop, named `groupName`, and each keeps a
    /// function of its own name that enters it (design note 6.3).
    FunctionEmitter(
        ProgramParts &program, std::vector<GCGraph *> members, std::string groupName = {});

    /// `int64_t fib(rt::Depth depth, int64_t n);` for each function (and the group's loop).
    std::vector<std::string> declarations() const;
    /// The definitions: the bodies, or the group's loop and the functions that enter it.
    std::string definition();
    /// The other functions called in tail position, known once definition() has run: the
    /// program groups functions that call each other so.
    const std::unordered_set<const GCGraph *> &tailCallees() const { return tailCallees_; }

    /// The C++ name of parameter `i` (the `with` ports, then the others, then the captures).
    static std::string parameterName(const GCGraph *graph, gc_node_ref_t port);

  private:
    struct Parameter {
        gc_node_ref_t port;
        std::string type;
        std::string name;  // as the function's own parameter
        std::string local; // in the body (in a group, prefixed with the function's name)
    };
    struct Member {
        GCGraph *graph = nullptr;
        std::string name;
        std::string result;
        std::vector<Parameter> params;
        size_t index = 0;
    };

    void collectParameters(Member &member);
    std::string signatureOf(const Member &member) const;
    std::string groupSignature() const;
    Block lowerMember(const Member &member);

    /// The values of one activation of a graph lowered into this function: the function's
    /// own graph.
    struct Frame {
        GCGraph *graph = nullptr;
        std::unordered_map<gc_node_ref_t, CppValue> values;
        /// Nodes already lowered as part of something else (the arms of a branch, its JOIN).
        std::unordered_set<gc_node_ref_t> consumed;
        /// The results of the sequences being lowered: each is used once more, by the end of
        /// its sequence (a return, or the assignment of a branch's value).
        std::unordered_multiset<gc_node_ref_t> results;
    };

    /// What becomes of the value of a sequence of statements at its end.
    struct Tail {
        enum class Kind { Return, Assign, Discard } kind = Kind::Discard;
        std::string variable; // Assign
    };

    Block lowerSequence(
        Frame &frame, const std::vector<gc_node_ref_t> &order, gc_node_ref_t result, Tail tail);
    Block lowerSequenceBody(
        Frame &frame, const std::vector<gc_node_ref_t> &order, gc_node_ref_t result, Tail tail);
    /// Lowers node `ref`; true when it was the sequence's result and its own lowering already
    /// did what `tail` asks (a branch whose arms end the function, a tail call).
    bool
    lowerNode(Frame &frame, gc_node_ref_t ref, Block &out, bool isTailResult, const Tail &tail);
    bool lowerBranch(
        Frame &frame, gc_node_ref_t brch, const std::vector<gc_node_ref_t> &order, Block &out,
        bool joinIsTail, const Tail &tail);
    /// A loop whose condition and body are regions of the graph: its state in variables, and its
    /// rounds in a `for (;;)`. The nodes of its regions are in `order`, before the LOOP node.
    void lowerLoop(
        Frame &frame, gc_node_ref_t loop, const std::vector<gc_node_ref_t> &order, Block &out);
    /// The control regions of `graph`, made once.
    const camel::runtime::ControlRegions &controlRegionsOf(const GCGraph *graph);
    void lowerCall(
        Frame &frame, gc_node_ref_t ref, Block &out, bool isTailResult, const Tail &tail,
        bool &handled);
    void lowerOperator(Frame &frame, gc_node_ref_t ref, Block &out);
    void lowerFill(Frame &frame, gc_node_ref_t ref, Block &out);
    void lowerAccess(Frame &frame, gc_node_ref_t ref);
    void lowerCallValue(Frame &frame, gc_node_ref_t ref, Block &out);
    void lowerCast(Frame &frame, gc_node_ref_t ref);
    /// `:var/new`, `:var/get` and the assignments; false for any other operator.
    bool lowerVariable(Frame &frame, gc_node_ref_t ref, const std::string &uri, Block &out);
    /// The in-place form of `append` over `:var/take(xs)` (design principles 11.3): grows the
    /// variable's array with `rt::append` once; false when the node is not that shape.
    bool lowerAppendInPlace(Frame &frame, gc_node_ref_t ref, Block &out);
    /// The in-place form of the element store over `:var/take(xs)` (design principles 11.3):
    /// `xs[i] = v` becomes `rt::setAt` on the variable's own array, the element checked as a
    /// read checks it; false when the node is not that shape.
    bool lowerSetIndexInPlace(Frame &frame, gc_node_ref_t ref, Block &out);
    /// `fail<'Kind'>(message)` (:op/fail): the value of type `never`, under the report the
    /// program's own kind names (design principles 3.9); false for any other operator.
    bool lowerFail(Frame &frame, gc_node_ref_t ref, const std::string &uri, Block &out);
    /// `map`, `filter`, `reduce`, `foreach`, `attempt`; false for any other operator.
    bool lowerHigherOrder(Frame &frame, gc_node_ref_t ref, const std::string &uri, Block &out);
    /// Whether the variable whose cell is `cell` is used outside this function (design note
    /// 5.3): then it is an rt::Cell, else a C++ local.
    bool cellEscapes(const GCGraph *graph, gc_node_ref_t cell);
    /// The function value of `function` with `captures`, as a C++ expression.
    std::optional<std::string>
    functionValue(GCGraph *function, const std::vector<std::string> &captures);
    static GCGraph *functionConstantOf(const GCGraph *graph, gc_node_ref_t ref);
    static bool bridgeCarries(const Type *type);
    /// The count of open calls a call at `ref` passes: `rt::call(depth, site)`, which checks
    /// the limit, or `depth` for a call of the entry.
    std::string callDepth(const GCGraph *graph, gc_node_ref_t ref);

    /// Names the value of `ref`: a statement `const T name = expr;` (or `expr;` when nothing
    /// uses it), or the expression itself when it may be moved to its one use. The C++ type is the
    /// one the node's Camel type names: a type has one representation, whichever operator built the
    /// value (README.cn.md 4).
    void bind(
        Frame &frame, gc_node_ref_t ref, std::string expr, bool inlinable, Block &out,
        const std::string &hint);
    const CppValue &valueOf(Frame &frame, gc_node_ref_t ref);
    std::string cppType(const GCGraph *graph, gc_node_ref_t ref, const Type *type);
    /// The accessor of the runtime operator `uri` in the generated file, and the registration that
    /// makes the program one including bridge.h and linking the runtime (the first bridged node is
    /// remembered for the warning that says so).
    std::string bridgeOperator(const GCGraph *graph, gc_node_ref_t ref, const std::string &uri);

    /// Whether only nodes that write nothing (constants, ports, gates, joins of order) come
    /// after position `at` of `order`.
    bool nothingAfter(const Frame &frame, const std::vector<gc_node_ref_t> &order, size_t at) const;
    size_t liveUses(const GCGraph *graph, gc_node_ref_t ref);

    [[noreturn]] void refuse(const GCGraph *graph, gc_node_ref_t ref, std::string reason) const;

    ProgramParts &program_;
    std::vector<Member> members_;
    std::string group_; // the name of the group's loop; empty for one function
    const Member *current_ = nullptr;
    std::unordered_set<const GCGraph *> tailCallees_;
    NameScope locals_;
    size_t nextValue_ = 1;
    bool loops_       = false; // a tail call jumps back: the body is a loop
    std::unordered_map<const GCGraph *, std::vector<gc_node_ref_t>> orders_;
    std::unordered_map<const GCGraph *, std::unique_ptr<camel::runtime::ControlRegions>> regions_;
    std::unordered_map<const GCGraph *, std::unordered_map<gc_node_ref_t, size_t>> uses_;
};

} // namespace camel::passes::cpp
