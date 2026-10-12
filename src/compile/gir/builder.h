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
 * Created: May. 29, 2024
 * Updated: May. 05, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include <deque>
#include <fstream>
#include <iostream>
#include <set>

#include "camel/common/scope.h"
#include "camel/core/context/context.h"
#include "camel/core/error/abort.h"
#include "camel/core/module/module.h"

#include "camel/compile/gct.h"
#include "camel/compile/gir/draft_graph_builder.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/mm.h"
#include "camel/core/mm/root_handle.h"
#include "camel/core/type/resolver.h"
#include "camel/core/type/typevar.h"

namespace camel::compile::gir {

using void_ptr_t   = void *;
using Type         = camel::core::type::Type;
using FunctionType = camel::core::type::FunctionType;

using compile_graph_ptr_t   = std::shared_ptr<DraftGraphBuilder>;
using node_handle_t         = draft_node_t *;
using node_scope_t          = Scope<std::string, node_handle_t>;
using node_scope_ptr_t      = std::shared_ptr<node_scope_t>;
using graph_set_t           = std::vector<compile_graph_ptr_t>;
using graph_set_ptr_t       = std::shared_ptr<graph_set_t>;
using graph_scope_t         = Scope<std::string, graph_set_ptr_t>;
using graph_scope_ptr_t     = std::shared_ptr<graph_scope_t>;
using decorated_scope_t     = Scope<std::string, compile_graph_ptr_t>;
using decorated_scope_ptr_t = std::shared_ptr<decorated_scope_t>;

class Builder {
  public:
    Builder(
        const camel::core::context::context_ptr_t &context,
        const camel::core::module::module_ptr_t &module)
        : context_(context), module_(module) {}

    compile_graph_ptr_t build(GCT::node_ptr_t &gct, camel::core::error::diagnostics_ptr_t diags);

    compile_graph_ptr_t rootGraph() const { return rootGraph_; }

  private:
    compile_graph_ptr_t rootGraph_;
    compile_graph_ptr_t currGraph_;

    node_scope_ptr_t nodeScope_;
    graph_scope_ptr_t graphScope_;
    decorated_scope_ptr_t decoratedScope_;
    std::unordered_map<Reference, Type *> typeDecls_;

    camel::core::context::context_ptr_t context_;
    camel::core::module::module_ptr_t module_;
    camel::core::error::diagnostics_ptr_t diags_;

    bool waited_;
    bool synced_;
    bool varied_;
    // Inside a `var` declaration: the names it binds are variables.
    bool declaresVariable_ = false;

    // ---- Reporting several errors without their consequences ----
    // A statement that fails is skipped and the next one is checked (visitExecNode). What the
    // failed statement would have declared is then missing, and every use of it would be
    // reported as an unresolved reference: one mistake, many errors. So the names a failed
    // declaration would have bound are remembered here, by the scope of the declaration, and
    // a reference to one of them fails without a report of its own: the statement using it
    // is skipped (and what *it* declares is poisoned in turn).
    std::map<node_scope_ptr_t, std::unordered_set<std::string>> poisonedNames_;
    bool isPoisonedName(const std::string &name) const;
    // Functions whose body is in error. One declared without a return type then never gets
    // one, and a call of it would be reported as a call of an incomplete function.
    std::unordered_set<const void *> failedBodies_;

    // An argument of a call: its value node and the expression it was written as.
    struct Argument {
        node_handle_t node;
        GCT::node_ptr_t expr;
    };

    // Track whether this Graph has already been used (via createFuncDataNode).
    // A used Graph can no longer accept additional closure captures.
    using node_modifier_map_t = std::unordered_map<draft_node_ref_t, draft_node_ref_t>;

    std::unordered_set<DraftGraphBuilder *> usedGraphs_;
    // The decorated form of each function declared with decorators, by its own graph.
    std::unordered_map<DraftGraphBuilder *, compile_graph_ptr_t> decoratedOf_;
    std::unordered_map<DraftGraphBuilder *, node_modifier_map_t> nodeModifierMaps_;
    // With-arguments bound to a callee by `f<a, b>`, consumed when the callee is invoked. Kept
    // beside the graph rather than as edges: the callee node is a value (a function literal, a
    // closure FILL whose with-inputs are its captures) that the binding must not alter.
    std::unordered_map<node_handle_t, std::vector<Argument>> boundWithArgs_;
    // Variables: the nodes bound by `var` (declarations, `var` parameters, the ports that capture
    // them). Such a node does not hold a value but the variable's cell (TupleType::cell), which
    // is what a `var` parameter or a capture receives; a value is read out of it by `:var/get`
    // and stored by the assignment operators. Every other named node is an immutable value
    // (`let`, a parameter without `var`).
    std::unordered_set<node_handle_t> variables_;
    // What the compiler knows about a node's value beyond static data, for operators whose
    // result type follows constant arguments (StaticArg). `typeFoldedValues_`: the value of an
    // operator result that its argument types fix (OperatorTypeFolderRegistry), e.g. the shape
    // of a tensor whose type carries it. `arrayElementStatics_`: for an array written in place
    // with some elements computed (`[n, 3]`), the elements that are constants. The graph is not
    // changed by either: the operator and the FILL stay, std::opt folds them later.
    std::unordered_map<node_handle_t, slot_t> typeFoldedValues_;
    std::unordered_map<node_handle_t, std::vector<std::optional<slot_t>>> arrayElementStatics_;
    std::deque<camel::core::mm::RootHandle> typeFoldedRoots_;
    camel::core::type::StaticArg staticArgOf(node_handle_t node) const;
    // The source text of the expression `expr` when it is short and on one line; empty
    // otherwise. For diagnostics that suggest source text.
    std::string sourceTextOf(const GCT::node_ptr_t &expr) const;
    // A use of a variable that no writer has been ordered after yet. A read (`:var/get`) hands
    // the value on, so the operations that take the read's result are the direct users of the
    // value; an operation that uses the variable itself (a call of a function that captured
    // it, a branch whose arm did) is the direct user.
    struct VariableUse {
        node_handle_t node;
        bool isRead;
    };
    std::unordered_map<node_handle_t, std::vector<VariableUse>> pendingUses_;

    // Captured variables. A graph (function, lambda, branch arm) that names a variable of an
    // enclosing graph, a module-level one included, holds the variable's cell in a capture
    // port. What the graph does with it is not in its type, so the builder keeps a summary per
    // graph and consults it wherever the graph is called directly or entered as a branch arm:
    // the call or branch is then a use of each captured variable, and a write of those the
    // graph writes (design principles 2.6: tracking of modifications is complete).
    struct CaptureSummary {
        // Names of the captured variables, in capture order.
        std::vector<std::string> variables;
        // Those the body itself stores into or hands to a `var` parameter. What its callees
        // and arms write is derived from `callees` (capturedWritesOf), not recorded here: at
        // a recursive call it is only an upper bound.
        std::set<std::string> written;
        // The graphs the body calls directly or enters as arms.
        std::vector<DraftGraphBuilder *> callees;
    };
    enum class BodyState { Open, Built };
    std::unordered_map<DraftGraphBuilder *, CaptureSummary> captures_;
    std::unordered_map<DraftGraphBuilder *, BodyState> bodyStates_;
    std::unordered_map<node_handle_t, std::string> capturePortNames_;
    // A captured variable as the current graph sees it at a call or branch.
    struct CapturedUse {
        node_handle_t variable;
        std::string name;
        bool written;
    };
    // An unwaited write of captured variables outside a sync block, reported once every body
    // is built: while a recursive function's body is open its writes are only an upper bound.
    struct PendingIgnoredWrite {
        std::vector<DraftGraphBuilder *> graphs;
        std::vector<std::string> names;
        camel::source::origin_id_t origin;
        std::string subject;
        std::string verb;
    };
    std::vector<PendingIgnoredWrite> pendingIgnoredWrites_;
    // The functions and lambdas of the module, in source order, for the check that a function
    // with side effects declares them (the `sync` function modifier).
    struct BuiltFunction {
        DraftGraphBuilder *graph;
        camel::source::origin_id_t origin;
        std::string name;
    };
    std::vector<BuiltFunction> builtFunctions_;
    // Dropped effects (design principles 2.1): outside a sync block an effectful expression
    // whose result nothing uses is not connected to the graph's exit and does not run. That
    // is legal; the builder warns about it. Graphs whose exit depends on an effect, and the
    // graphs that are branch arms (reported through their branch, not as calls).
    std::unordered_set<DraftGraphBuilder *> effectfulGraphs_;
    std::unordered_set<DraftGraphBuilder *> armGraphs_;
    // The function bodies and arms of the module, in the order they were built.
    std::vector<compile_graph_ptr_t> builtBodies_;
    node_handle_t lastSyncedNode_{};
    size_t syntheticRefIndex_ = 0;

    // ---- Generic functions (design principles 2.9) ----
    //
    // A function whose signature binds type variables (`typeas T`) is a subgraph template:
    // no graph is built for its declaration. Each direct call binds the variables from the
    // argument types and asks for the instance of those types, which is built once from the
    // function's GCT with the variables replaced (typeBindings_) wherever the body names a
    // type, and then called like any other function. So the body is typed and its overloads
    // are chosen at instantiation, and only concrete types reach a graph.
    struct GenericInstance {
        camel::core::type::type_bindings_t bindings;
        compile_graph_ptr_t graph;
        // An error was found in the body: later calls with the same types fail quietly.
        bool failed = false;
    };
    struct GenericTemplate {
        std::string name;
        GCT::node_ptr_t func;
        // The declared type with the variables of enclosing instances replaced; the function's
        // own variables remain.
        FunctionType *funcType = nullptr;
        // The bindings in force where the template was declared (a generic function declared
        // inside an instance of another one).
        camel::core::type::type_bindings_t outerBindings;
        // Where the function was declared: instances are built there, not at the call.
        compile_graph_ptr_t ownerGraph;
        node_scope_ptr_t nodeScope;
        graph_scope_ptr_t graphScope;
        decorated_scope_ptr_t decoratedScope;
        std::shared_ptr<Scope<std::string, std::shared_ptr<GenericTemplate>>> templateScope;
        std::vector<GenericInstance> instances;
        // Instances whose bodies are being built right now, one inside another.
        size_t openInstances = 0;
    };
    using template_ptr_t       = std::shared_ptr<GenericTemplate>;
    using template_scope_t     = Scope<std::string, template_ptr_t>;
    using template_scope_ptr_t = std::shared_ptr<template_scope_t>;
    // Bounds on instantiation. A generic function that calls itself with ever larger types
    // (`f(x) = f([x])`) would never stop building instances, so the instances of ONE function
    // being built inside one another are limited; the number is small because such types
    // may double at every step (`f((x, x))`). A chain of different generic functions calling
    // each other is ordinary code and only meets the far larger overall bound.
    static constexpr size_t kMaxOpenInstancesOfOne = 8;
    static constexpr size_t kMaxInstantiationDepth = 128;

    template_scope_ptr_t templateScope_;
    // The type variables of the instance being built and their types; empty outside one.
    camel::core::type::type_bindings_t typeBindings_;
    size_t instantiationDepth_ = 0;
    // Set while the callee of a call is visited: the one place the name of a generic function
    // may stand. The DREF nodes that name one, and the template each names.
    bool genericCalleeAllowed_ = false;
    std::unordered_map<node_handle_t, template_ptr_t> genericDrefs_;

    // A type read from the GCT, with the type variables of the current instance replaced.
    Type *boundType(Type *type) const {
        return camel::core::type::substituteTypeVars(type, typeBindings_);
    }
    // Whether a declared function type is still generic under the current bindings.
    bool isGenericHere(Type *type) const {
        return camel::core::type::containsTypeVar(type) &&
               camel::core::type::containsTypeVar(boundType(type));
    }
    // Registers the generic functions declared directly in the block `exec`, before anything
    // in the block is built: a call may precede the declaration.
    void registerGenericTemplates(const GCT::node_ptr_t &exec);
    // The instance of `tmpl` that the arguments of `call` ask for: binds the type variables,
    // checks every argument against its parameter, and builds the instance unless it exists.
    compile_graph_ptr_t resolveGenericCall(
        const template_ptr_t &tmpl, const camel::core::type::type_vec_t &withTypes,
        const camel::core::type::type_vec_t &normTypes, const GCT::node_ptr_t &call);
    compile_graph_ptr_t instantiateGeneric(
        const template_ptr_t &tmpl, const camel::core::type::type_bindings_t &bindings,
        const GCT::node_ptr_t &call);
    // Enters the scope of `graph`, an existing graph, as enterScope does for one it finds.
    void enterGraphScope(const compile_graph_ptr_t &graph, const std::string &name);
    // Builds the body of the function `gct` into `graph`, whose scope has been entered.
    void buildFuncBody(
        const GCT::node_ptr_t &gct, const compile_graph_ptr_t &graph, FunctionType *funcType,
        const std::string &name);

    std::optional<node_handle_t> nodeAt(const std::string &name) {
        EXEC_WHEN_DEBUG({
            std::stringstream ss;
            nodeScope_->dump(
                ss,
                [](std::ostream &os, const std::string &key, const node_handle_t &value) {
                    os << "[" << key << "] ";
                    if (value == nullptr) {
                        os << "<invalid>";
                        return;
                    }
                    auto *graph = DraftGraphBuilder::fromDraft(value->header.owner);
                    os << (graph ? graph->name() : std::string{"<detached>"}) << "#"
                       << value->header.selfId;
                });
            CAMEL_LOG_DEBUG_S("GIR Builder", "Accessing node '{}' from scope {}", name, ss.str());
        });
        return nodeScope_->get(name);
    }
    std::optional<graph_set_ptr_t> graphsAt(const std::string &name) {
        EXEC_WHEN_DEBUG({
            std::stringstream ss;
            graphScope_->dump(
                ss,
                [&](std::ostream &os,
                    const std::string &key,
                    const std::shared_ptr<graph_vec_t> &value) {
                    os << "[" << key << "]";
                    for (const auto &graph : *value) {
                        os << " " << (graph ? graph->name() : std::string{"<null>"});
                    }
                });
            CAMEL_LOG_DEBUG_S("GIR Builder", "Accessing graph '{}' from scope {}", name, ss.str());
        });
        return graphScope_->get(name);
    }
    std::optional<compile_graph_ptr_t> decoratedGraphAt(const std::string &name) {
        return decoratedScope_ ? decoratedScope_->get(name) : std::nullopt;
    }

    bool insertNode(const std::string &name, node_handle_t node);
    bool insertGraph(const std::string &name, const compile_graph_ptr_t &graph);
    bool insertDecoratedGraph(const std::string &name, const compile_graph_ptr_t &graph);

    // Enters the scope of the function `name` whose declared type is the object `funcType`,
    // creating its graph the first time. Functions of one name in one block are overloads;
    // the declared type object identifies which one is meant.
    compile_graph_ptr_t enterScope(FunctionType *funcType, const std::string &name = "");
    // A second function of this name with the same parameter types in the same block:
    // reports Redeclaration at `gct` and abandons the declaration.
    void rejectRedeclaredFunction(
        const std::string &name, FunctionType *funcType, const GCT::node_ptr_t &gct);
    // The declared types of the functions rejected so (the forward declaration reports, the
    // definition that follows is skipped quietly).
    std::unordered_set<FunctionType *> redeclaredFuncTypes_;
    void leaveScope();

    node_handle_t createFuncDataNode(
        const compile_graph_ptr_t &graph, bool getCallableNode, bool allowParameterization);
    // The value node of a visited expression: a node as is, or a function literal (a graph) as
    // its callable function value.
    node_handle_t valueNodeOf(const std::any &res);
    // The value node of a call or with-argument: also lowers functions named as values.
    node_handle_t argumentNodeOf(const std::any &res, const GCT::node_ptr_t &origin);
    // `node` when it is a value; when it is the name of exactly one function (a DREF node),
    // that function's value. A name that stands for an operator or for several overloads has
    // no single value and is returned as it is.
    node_handle_t functionValueOf(node_handle_t node, const GCT::node_ptr_t &origin);
    // The value of the element `name` of the composite literal `literal`: a bound value, or
    // the value of the function of that name.
    node_handle_t elementNodeOf(const std::string &name, const GCT::node_ptr_t &literal);
    // functionValueOf, and FunctionNameAsValue when `name` has no single function value.
    node_handle_t
    namedValueOf(node_handle_t node, const std::string &name, const GCT::node_ptr_t &origin);
    // Reports FunctionNameAsValue at `origin` when `node` is still such a name (`name`) in
    // a place that needs a value.
    void rejectNameWithoutValue(
        node_handle_t node, const std::string &name, const GCT::node_ptr_t &origin);
    // A function with the declared parameters of `target` that captures the names `target`
    // captured and calls it with them: what a function value of `target` refers to once the
    // captures of `target` have become with-parameters (createFuncDataNode).
    compile_graph_ptr_t forwardingGraphOf(const compile_graph_ptr_t &target);
    // The functions named `name` when `decorated` is one of several of that name (the set a
    // call chooses from); null when the decorated function is the only one.
    graph_set_ptr_t
    overloadsOfDecorated(const std::string &name, const compile_graph_ptr_t &decorated);
    // The function a decorated graph produces when its decorators run.
    node_handle_t lowerDecoratedGraphValue(
        const compile_graph_ptr_t &decoratedGraph, const GCT::node_ptr_t &origin);
    std::optional<node_handle_t> modifierOf(node_handle_t node) const;
    void setModifier(node_handle_t input, node_handle_t modifier);
    // An operation writes `variable`: later reads and writers are ordered after it, and a
    // waited one is ordered after the operations before it that used the variable's value. The
    // operation starts at `entry` and is complete at `exit` (one node for a call; BRCH and
    // JOIN for a branch). `throughCallee`: the write is one a called function or an arm makes
    // to a variable it captured, which the summaries already account for.
    void recordWrite(
        node_handle_t variable, node_handle_t entry, node_handle_t exit,
        bool throughCallee = false);
    void recordWrite(node_handle_t variable, node_handle_t writer) {
        recordWrite(variable, writer, writer);
    }
    // Orders `node` after the last writer of `variable` in the current graph.
    void orderAfterLastWriter(node_handle_t variable, node_handle_t node);
    // The captured variables `graph` writes: by its own body, by its arms and by the functions
    // it calls directly, transitively. Exact once every graph reached is built; for a graph
    // whose body is still open (a recursive call) every variable it captured counts as written.
    std::set<std::string> capturedWritesOf(DraftGraphBuilder *graph) const;
    // The captured variables of `callee` as nodes of the current graph, which becomes a
    // caller of `callee`.
    std::vector<CapturedUse> capturedUsesOf(const compile_graph_ptr_t &callee);
    // Reports the unwaited writes of captured variables that turned out to be real.
    void reportIgnoredCapturedWrites();
    // Reports the functions that write a captured variable without the `sync` modifier.
    void reportUndeclaredCapturedWrites();
    // Whether running `node` of `graph` is an effect: an operator that is neither pure nor
    // state-only, a call of a function that declares effects (`sync` modifier, `var`
    // parameter) or whose exit depends on one, a branch with such an arm.
    bool isEffectNode(DraftGraphBuilder *graph, node_handle_t node) const;
    // Once the body of `graph` is built: records whether its exit depends on an effect, and
    // warns (EffectNotExecuted) about the effects that the exit does not depend on.
    // With `report` false it only records whether the exit depends on an effect.
    void reportDroppedEffects(const compile_graph_ptr_t &graph, bool report = true);
    // Once every body is built: settles which graphs are effectful (a callee may be written
    // after its caller) and then warns for each body.
    void reportDroppedEffectsOfBodies();
    compile_graph_ptr_t buildDecoratedGraph(
        const std::string &funcName, const compile_graph_ptr_t &rawGraph,
        const std::vector<GCT::node_ptr_t> &annoNodes);
    node_handle_t applyDecoratorAnno(const GCT::node_ptr_t &annoNode, node_handle_t funcValueNode);
    node_handle_t resolveCrossGraphRef(node_handle_t node, const std::string &name);
    node_handle_t resolveNodeByRef(const std::string &name);
    bool isVariable(node_handle_t node) const { return variables_.contains(node); }
    // A new variable holding the value of `value` (`:var/new`).
    node_handle_t newVariable(node_handle_t value);
    // The value `variable` holds when the returned node runs (`:var/get`), ordered after the
    // variable's last writer in this graph and, in a sync block, in the block's chain.
    node_handle_t readVariable(node_handle_t variable);
    // `node` as a value: read when it is a variable, itself otherwise.
    node_handle_t valueOf(node_handle_t node) {
        return isVariable(node) ? readVariable(node) : node;
    }
    // An operand of a call. A variable named directly stays a variable: whether the callee
    // takes the variable or its value is only known once the callee is resolved.
    node_handle_t visitOperand(const GCT::node_ptr_t &expr, const GCT::node_ptr_t &call);
    // The node of `:var/new` or `:var/get` applied to `input`.
    node_handle_t variableOperator(const std::string &group, node_handle_t input);
    // The immutable name an argument expression denotes: the name itself or a field of it
    // (`s`, `s.x`). Empty for a variable and for an expression that is not a name.
    std::optional<std::string> immutableNameOf(const GCT::node_ptr_t &expr);
    // Reports an immutable name passed where the callee has a `var` parameter.
    void requireMutableArgument(const Argument &arg, const std::string &callee);

    std::any visit(const GCT::node_ptr_t &gct);

    void_ptr_t visitDeclNode(const GCT::node_ptr_t &gct);
    compile_graph_ptr_t visitFuncNode(const GCT::node_ptr_t &gct);
    node_handle_t visitDataNode(const GCT::node_ptr_t &gct);
    Type *visitTypeNode(const GCT::node_ptr_t &gct);
    node_handle_t visitNRefNode(const GCT::node_ptr_t &gct);
    node_handle_t visitDRefNode(const GCT::node_ptr_t &gct);
    node_handle_t visitCastNode(const GCT::node_ptr_t &gct);
    node_handle_t visitVariNode(const GCT::node_ptr_t &gct);
    node_handle_t visitWaitNode(const GCT::node_ptr_t &gct);
    node_handle_t visitLinkNode(const GCT::node_ptr_t &gct);
    node_handle_t visitWithNode(const GCT::node_ptr_t &gct);
    node_handle_t visitAccsNode(const GCT::node_ptr_t &gct);
    node_handle_t visitBrchNode(const GCT::node_ptr_t &gct);
    /// The BRCH/JOIN pair of a branch on `condNode`. The arms come from the CASE children of
    /// `gct`, or, when `arms` is given, are those graphs in order (built by buildArmGraph).
    /// With `faultArm`, one more arm follows them, taken when no case value equals the
    /// scrutinee: it stops the program (RuntimeDiag::MatchNotExhaustive).
    node_handle_t buildBranch(
        const GCT::node_ptr_t &gct, node_handle_t condNode,
        const std::vector<compile_graph_ptr_t> *arms = nullptr, bool faultArm = false);
    /// Whether the `match` `gct` can meet a value none of its cases matches (it has no
    /// `case _` and does not list both values of a bool); reports MatchWithoutDefault then.
    bool matchNeedsFaultArm(const GCT::node_ptr_t &gct, node_handle_t scrutinee);
    /// The subgraph of one branch arm. Its body is the block `exec`, or what `body` builds
    /// (called inside the arm's scope) when given; `exec` then only locates the arm.
    compile_graph_ptr_t buildArmGraph(
        const GCT::node_ptr_t &exec, const std::function<node_handle_t()> &body = {});

    // ---- The tagged forms `T?` and `T!` (design principles 2.9;
    // docs/technical/29_nullable_and_fallible_values.md). Each construct below is a branch on
    // the one-bit tag of a value; nothing here adds a node kind. ----
    /// `a ?? b`, `a !! b`, `x ?-> f`.
    node_handle_t visitTaggedBrchNode(const GCT::node_ptr_t &gct);
    /// `match x { case null => ..  case v => .. }` and `case error e` over a tagged `x`.
    node_handle_t visitTaggedMatch(
        const GCT::node_ptr_t &gct, node_handle_t scrutinee, camel::core::type::TupleType *type);
    /// Binds `node` to a fresh name no program can write, so that arms can capture it.
    std::string bindTemporary(node_handle_t node);
    /// The call `op(<name>)` of one of the built-in operators over tagged values.
    node_handle_t
    applyTagOperator(const char *op, const std::string &name, const GCT::node_ptr_t &origin);
    /// A CAST of `value` to `target`, made by the compiler (no conversion rule is consulted).
    node_handle_t castNodeTo(
        node_handle_t value, camel::core::type::Type *target, const GCT::node_ptr_t &origin);
    /// The return type the function around the current position declares (looking out of
    /// branch arms), or nullptr when it declares none.
    camel::core::type::Type *declaredExitTypeHere() const;
    /// What `return value` / `throw value` hands back: the value put into the declared
    /// `T?` / `T!`, or the failed form of the declared `T!`.
    node_handle_t adaptExitValue(const GCT::node_ptr_t &gct, node_handle_t value);
    /// `file:line:column` of an origin, for the `location` of an error; empty when unknown.
    std::string sourceLocationOf(uint64_t origin) const;
    /// Branch arms whose body is being built.
    std::unordered_set<DraftGraphBuilder *> openArmGraphs_;
    size_t tagTemporaries_ = 0;
    node_handle_t visitAnnoNode(const GCT::node_ptr_t &gct);
    node_handle_t visitExitNode(const GCT::node_ptr_t &gct);
    node_handle_t visitExecNode(const GCT::node_ptr_t &gct);
    // The body of a graph (function, lambda, branch arm): a block of its own, whatever block
    // the function or arm is written in.
    node_handle_t visitBodyNode(const GCT::node_ptr_t &gct);
    // Makes `result` the result of `graph` when its body set none with `return`.
    void setBodyResult(const compile_graph_ptr_t &graph, node_handle_t result);
    /// Reports SemanticDiag::ArgumentNeedsConversion (and aborts) when the arguments of `call`
    /// fit exactly one of `candidates` but for types that leave open what a parameter fixes,
    /// i.e. when writing `as` at those arguments makes the call compile. Returns otherwise.
    void reportConversionGaps(
        const std::vector<std::pair<std::string, camel::core::type::FunctionType *>> &candidates,
        const std::vector<Argument> &withInputs, const std::vector<Argument> &normInputs,
        const camel::core::type::type_vec_t &withTypes,
        const camel::core::type::type_vec_t &normTypes, const GCT::node_ptr_t &call);
    /// Reports SemanticDiag::ReturnTypeMismatch when `graph` declares a return type that the
    /// value it returns cannot be assigned to (the rule arguments are held to:
    /// Type::assignableFrom, no numeric conversion), or SemanticDiag::ReturnNeedsConversion when a
    /// conversion written with `as` leads there. `origin` locates the returned expression, or 0 for
    /// the function; `value` is the returned expression when it is known (its text is quoted in the
    /// suggested conversion).
    void checkDeclaredReturn(
        const compile_graph_ptr_t &graph, camel::core::type::Type *resultType, uint64_t origin,
        const GCT::node_ptr_t &value = nullptr);
    void_ptr_t visitExptNode(const GCT::node_ptr_t &gct);
};

} // namespace camel::compile::gir
