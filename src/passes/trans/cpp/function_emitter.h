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

/*
 * One graph of the program as one C++ function (docs/design/cpp-standalone/README.cn.md,
 * section 6).
 *
 * The statements follow the execution order std::fvm compiles the graph in
 * (buildReachableExecutionTopoIndices): only live nodes, every edge (data, `with`, control)
 * respected, the arms of each branch laid out as contiguous ranges. A branch is an if /
 * else-if / else statement at the place of its BRCH, its arms the statements of their nodes:
 * in the form the front end gives (each arm a graph of its own, called by a FUNC node) the arm
 * graph is lowered into the function in place of the call; in the form std::inline leaves (the
 * arm's nodes between the BRCH and the JOIN) the range of the arm is.
 *
 * A call of the function itself whose value is the function's value, with nothing after it, is
 * a jump back to the start: the body is a loop and the call assigns the parameters. Every
 * other call passes the count of open calls on (camel/standalone/core.h, rt::call), which
 * checks the recursion limit at the call.
 */

#pragma once

#include "camel/runtime/graph.h"

#include "code.h"
#include "cpp_types.h"
#include "fault_sites.h"
#include "operator_rules.h"

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
    StringPool strings;
    SiteTable sites;
    /// The C++ name of each graph that is a function of the generated program.
    std::unordered_map<const GCGraph *, std::string> functionNames;
    /// Operators that go through the runtime module that implements them, by URI: the name
    /// of the generated accessor. Empty while the program is standalone.
    std::unordered_map<std::string, std::string> bridgedOperators;

    explicit ProgramParts(camel::core::context::context_ptr_t context)
        : sites(std::move(context)) {}
};

/// "function 'f'" or "an anonymous function", as refusals name a graph.
std::string describeFunction(const GCGraph *graph);

/// Whether the FUNC node `ref` of `graph` is the head of a branch arm in the front end's form:
/// a call of an anonymous graph that the BRCH starts. Such a call is lowered in place.
bool isArmCall(const GCGraph *graph, gc_node_ref_t ref);

/// The URI of the operator of the OPER node `ref`. The one place the translation reads it:
/// when OPER nodes carry an operator-table id instead, only this changes.
std::string operatorUri(const GCGraph *graph, gc_node_ref_t ref);

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
    /// own graph, or an arm graph lowered in place of its call.
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
    bool lowerNode(
        Frame &frame, gc_node_ref_t ref, Block &out, bool isTailResult, const Tail &tail);
    bool lowerBranch(Frame &frame, gc_node_ref_t brch, Block &out, bool joinIsTail, const Tail &tail);
    void lowerCall(Frame &frame, gc_node_ref_t ref, Block &out, bool isTailResult, const Tail &tail,
                   bool &handled);
    void lowerOperator(Frame &frame, gc_node_ref_t ref, Block &out);
    /// The statements of an arm graph called by the FUNC node `call` of `frame`.
    Block lowerArmGraph(Frame &frame, gc_node_ref_t call, Tail tail);

    /// Names the value of `ref`: a statement `const T name = expr;` (or `expr;` when nothing
    /// uses it), or the expression itself when it may be moved to its one use.
    void bind(Frame &frame, gc_node_ref_t ref, std::string expr, bool inlinable, Block &out,
              const std::string &hint);
    const CppValue &valueOf(Frame &frame, gc_node_ref_t ref);
    std::string cppType(const GCGraph *graph, gc_node_ref_t ref, const Type *type);

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
    std::unordered_map<const GCGraph *, std::unordered_map<gc_node_ref_t, size_t>> uses_;
};

} // namespace camel::passes::cpp
