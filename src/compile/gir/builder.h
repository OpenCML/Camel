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
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include <any>
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>

#include "camel/common/scope.h"
#include "camel/core/context/context.h"
#include "camel/core/error/abort.h"
#include "camel/core/module/module.h"

#include "camel/compile/gct.h"
#include "camel/compile/gir/draft_graph_builder.h"
#include "camel/compile/gir/value_entity.h"
#include "camel/compile/semantic_recorder.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/mm.h"
#include "camel/core/mm/root_handle.h"
#include "camel/core/type/enum.h"
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

/// The graph builder of a program (design principles 2.11, "模块"; the graph model,
/// docs/design/graph-model/README.cn.md 3.9). One builder serves every module of the program:
/// the functions of all modules are graphs of their own, and the values of the top level of
/// every module are nodes of ONE root graph, `__root__`, whose exit is the call of the entry
/// module's `main`. A function reaches a value of the top level of any module through a
/// capture port bound to that value's node (captureInto), never by looking up its name where
/// the function is called, so that a function keeps its meaning wherever it is used.
class Builder {
  public:
    explicit Builder(const camel::core::context::context_ptr_t &context) : context_(context) {}

    /// The builder of the program compiled on `context`, created on first use.
    static Builder &of(const camel::core::context::context_ptr_t &context);

    // Loading is three steps (the graph model, 3.9.4): declare every module, build, link.

    /// Step one for the module `module`, whose construction tree is `tree`: makes the graphs
    /// of its functions (without bodies), registers its generic functions, its top-level
    /// values and its types, and fills its export table. Nothing of another module is looked
    /// at, so modules may import each other. Diagnostics go to `diags`. The tree must outlive
    /// the program's graphs: function bodies and generic instances are lowered from it.
    void declareModule(
        const camel::core::module::module_ptr_t &module, GCT::Tree &tree,
        camel::core::error::diagnostics_ptr_t diags);

    /// Steps two and three for the program whose entry is Context::mainModule (every module
    /// it imports, directly or not, declared): builds the function bodies and the top-level
    /// values of every module, in the order of the imports, each one when it is first needed
    /// (so the top level of a module does not depend on the order it is written in); then
    /// links (captures, the call of the entry module's `main`, the summaries of effects and
    /// the warnings that need them). False when a module is in error.
    bool buildProgram();

    /// Steps two and three for a library: the module `module` (declared with declareModule on
    /// this builder) and what it imports, built as buildProgram builds a program, without the
    /// call of an entry function. The definition libraries of operators are built so, by a
    /// builder of their own (UserDefinedModule::compileAsLibrary). False when a module is in
    /// error.
    bool buildLibrary(const camel::core::module::module_ptr_t &module);

    compile_graph_ptr_t rootGraph() const { return rootGraph_; }

  private:
    // ---- The modules of the program ----
    // What the builder keeps of one module: its tree, its diagnostics and the scopes of its
    // top level (the names it declares; its functions are graphs whose scopes are entered
    // from these). A function body of the module is built with these in force, whichever
    // module asks for it.
    struct GenericTemplate;
    struct BuiltFunction;
    struct PendingIgnoredWrite;
    struct ModuleUnit;
    // A top-level declaration of a module that is built when first needed (step two): a value
    // declaration (`let`/`var`, one or several names) or a function.
    enum class BuildState { Declared, Building, Built, Failed };
    struct TopLevelDeclaration {
        GCT::node_ptr_t gct;
        ModuleUnit *unit = nullptr;
        BuildState state = BuildState::Declared;
        // The names a value declaration binds; the function's name for a function.
        std::vector<std::string> names;
        bool isVar = false;
        // The graph of a function (made when the module was declared).
        compile_graph_ptr_t graph;
    };
    struct ModuleUnit {
        camel::core::module::module_ptr_t module;
        GCT::Tree *tree = nullptr;
        camel::core::error::diagnostics_ptr_t diags;
        node_scope_ptr_t nodeScope;
        graph_scope_ptr_t graphScope;
        decorated_scope_ptr_t decoratedScope;
        std::shared_ptr<Scope<std::string, std::shared_ptr<GenericTemplate>>> templateScope;
        // The types the module declares, for its exports.
        std::unordered_map<Reference, Type *> typeDecls;
        // Whether the module is the entry of the program.
        bool entry = false;
        // Its top-level declarations to build, in the order they are written, and the value
        // declaration of each name of its top level.
        std::vector<std::unique_ptr<TopLevelDeclaration>> declarations;
        std::unordered_map<std::string, TopLevelDeclaration *> values;
        // The values it exports (their entities point here), and its export list.
        std::vector<std::unique_ptr<ExportedValue>> exportedValues;
        GCT::node_ptr_t exportList = nullptr;
        // Whether step two found it in error.
        bool failed = false;
        // What the link step reports per module, collected while its bodies are built.
        std::vector<BuiltFunction> builtFunctions;
        std::vector<compile_graph_ptr_t> builtBodies;
        std::vector<PendingIgnoredWrite> pendingIgnoredWrites;
    };
    std::unordered_map<const camel::core::module::Module *, std::unique_ptr<ModuleUnit>> units_;
    // The module being built.
    ModuleUnit *unit_ = nullptr;
    // The module whose top level put each node on the root graph, by node id: the warnings
    // about the top level of a module (EffectNotExecuted) are about its own nodes only.
    std::vector<const ModuleUnit *> rootNodeOwners_;
    // The modules in the order they were declared.
    std::vector<ModuleUnit *> unitOrder_;
    // The full name of `name` of `unit`: `module::path::name`, without the module for the
    // entry module (design principles 2.12: the module is left out where it is not needed).
    static std::string qualifiedName(const ModuleUnit &unit, const std::string &name);
    // The top-level functions of every module by their graphs.
    std::unordered_map<DraftGraphBuilder *, TopLevelDeclaration *> topLevelFunctions_;
    // The module of each graph made directly under the root graph (a top-level function, a
    // generic instance of one, a lambda at the top level): the root graph lists as its own
    // functions only the entry module's, so that what the program does not use of the
    // modules it imports is not part of it.
    std::unordered_map<DraftGraphBuilder *, const ModuleUnit *> rootSubGraphOwners_;
    // What is being built on demand, outermost first: a top-level value or function. A value
    // asked for while it is in here depends on itself (ModuleValueCycle).
    std::vector<TopLevelDeclaration *> buildStack_;
    // Where the name being resolved is written: a diagnostic about what resolving it found
    // (a cycle, a late capture) is reported there.
    camel::source::origin_id_t refOrigin_ = camel::source::kInvalidOriginId;
    // ---- Names (names.cpp; design principles 2.12) ----
    // The namespace path of the top-level declaration being built (`A::B` for `A::B::f`; for
    // a generic instance, its template's): a name written in it is looked up there first.
    std::vector<std::string> namespacePath_;
    // The `using namespace` in force, one frame per block (the module's, the namespace blocks
    // of the declaration, the blocks of the body down to the current one).
    std::vector<const std::vector<Reference> *> usingFrames_;
    // What a name a `using namespace` brings stands for: a name of the module itself, or an
    // entity of an imported module; `shown` is the qualified name for diagnostics.
    struct UsingMatch {
        std::string ownName;
        std::optional<camel::core::module::entity> entity;
        std::string shown;
    };
    static bool isOperatorName(const std::string &name);
    static bool sameParameterTypes(FunctionType *a, FunctionType *b);
    // The unit of the module whose names are being resolved (module_).
    ModuleUnit *currentModuleUnit() const;
    // Whether `name` is declared in a scope below the module's top level (layer 1).
    bool declaredLocally(const std::string &name) const;
    // Whether `name` (with its namespaces) is declared at the top level of `unit`.
    bool declaredAtTopLevel(const ModuleUnit &unit, const std::string &name) const;
    // The name to look up in the scopes for `written` (layers 1 and 2): `written` itself when
    // it is local or not the module's; else the module's name it stands for, from the
    // innermost namespace out, the module's own name stripped from it (N1, N7).
    std::string moduleLevelName(const std::string &written) const;
    // Layer 4: what the `using namespace` in force bring under the name `written`; null when
    // none does. Reports AmbiguousName at `at` when two bring things that cannot be told apart.
    std::optional<UsingMatch> throughUsing(const std::string &written, const GCT::node_ptr_t &at);
    // Enters the frame of `using namespace` of `node` (a block, a top-level declaration) for as
    // long as it lives.
    struct UsingScope {
        Builder &self;
        bool pushed = false;
        UsingScope(Builder &b, const std::vector<Reference> *usings) : self(b) {
            if (usings != nullptr) {
                self.usingFrames_.push_back(usings);
                pushed = true;
            }
        }
        ~UsingScope() {
            if (pushed) {
                self.usingFrames_.pop_back();
            }
        }
        UsingScope(const UsingScope &)            = delete;
        UsingScope &operator=(const UsingScope &) = delete;
    };
    // Switches the builder to the top level of a module for as long as it lives, and back.
    class UnitContext;
    friend class UnitContext;
    // Steps two and three from `entry` (buildProgram, buildLibrary): every module it imports,
    // directly or not, then it; the call of `main` only when `callEntry`.
    bool buildFrom(ModuleUnit &entry, bool callEntry);
    // Step two for one module: its top-level declarations in the order they are written.
    void buildModuleBody(ModuleUnit &unit);
    // Builds the top-level value declaration `declaration` unless it is built: in its own
    // module's top level, wherever the builder is. A value already being built depends on
    // itself: ModuleValueCycle.
    void buildTopLevelValue(TopLevelDeclaration &declaration);
    // Builds the body of the top-level function `graph` unless it is built or being built (a
    // recursive call). An error in it is reported and leaves the caller's build going.
    void ensureFunctionBuilt(const compile_graph_ptr_t &graph);
    // When `name` names a top-level value of the current module not built yet: builds it and
    // returns true (nodeAt then finds it).
    bool buildTopLevelValueNamed(const std::string &name);
    // The node of a value another module exports, built if it is not.
    node_handle_t exportedValueNode(ExportedValue &value);
    // Reports ModuleValueCycle: `declaration` is asked for while it is being built.
    [[noreturn]] void reportValueCycle(const TopLevelDeclaration &declaration);
    // The namespaces a top-level declaration is built in: those of its name, or for an
    // operator overload those of the block it is written in (GCT::Tree::namespaceOf).
    std::vector<std::string> namespacesOf(const TopLevelDeclaration &declaration) const;
    // The name a top-level declaration is shown by in a diagnostic of the current module.
    std::string shownName(const TopLevelDeclaration &declaration) const;
    // Fills the export table of the current module from its export list `expt`: at step one
    // everything but the functions with decorators, which exist once their bodies are built
    // (`decorated`).
    void exportDeclarations(const GCT::node_ptr_t &expt, bool decorated);
    // Notes the nodes of the root graph added since `firstNode` as nodes of the current module.
    void claimRootNodes(size_t firstNode);
    // Makes the root graph the first time a module is declared.
    void ensureRootGraph();
    // The exit of the root graph: the call of the entry module's `main` (EntryModuleMissingMain
    // when it has none), which takes the parameters `main` takes.
    void buildEntryCall();
    // The value of the top level of a module that a node of the root graph is: the module that
    // declares it and its name there (with the namespaces it is declared in). A capture of
    // such a node is a capture of that value; diagnostics name it so.
    struct TopLevelValue {
        const camel::core::module::Module *module;
        std::string name;
    };
    std::unordered_map<node_handle_t, TopLevelValue> topLevelValues_;

    // The tree of the module being built. Nodes the builder writes itself (a compound
    // assignment spelled out, the tag operators of `??`) are made by it.
    GCT::Tree *tree_ = nullptr;
    compile_graph_ptr_t rootGraph_;
    compile_graph_ptr_t currGraph_;

    node_scope_ptr_t nodeScope_;
    graph_scope_ptr_t graphScope_;
    decorated_scope_ptr_t decoratedScope_;

    camel::core::context::context_ptr_t context_;
    camel::core::module::module_ptr_t module_;
    camel::core::error::diagnostics_ptr_t diags_;

    bool waited_;
    bool synced_;
    bool varied_;
    // The numeric permissions in force where the builder is (design principles 2.13): those of
    // the nearest GCT EXEC, which already include every enclosing block and function
    // (GCT::Node::numericPermissions). Every OPER node made while they are in force gets them
    // (stampPermissions); a function's graph starts from those of its own body, whatever was
    // in force where it is built from.
    uint8_t numericPermissions_ = 0;
    void stampPermissions(node_handle_t operNode) const;
    /// Sets numericPermissions_ for a scope and puts the previous value back at its end.
    struct NumericPermissionScope {
        uint8_t &current;
        uint8_t outer;
        NumericPermissionScope(uint8_t &c, uint8_t permissions) : current(c), outer(c) {
            current = permissions;
        }
        ~NumericPermissionScope() { current = outer; }
    };
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

    // ---- Facts for tools (camel/compile/semantic_recorder.h) ----
    // Null unless the context has a recorder; every report is guarded by it. The origin of
    // the declaration that bound each name, by the scope it was bound in, so that a reference
    // can report which declaration it resolved to. Filled only while recording.
    camel::compile::SemanticRecorder *recorder_ = nullptr;
    std::unordered_map<
        const node_scope_t *, std::unordered_map<std::string, camel::source::origin_id_t>>
        declOrigins_;
    // Reports that `decl` bound `name` (to `node`) in the current scope.
    void
    recordDeclaration(const std::string &name, node_handle_t node, const GCT::node_ptr_t &decl);
    // The declaration a reference to `name` from the current scope resolved to.
    camel::source::origin_id_t declarationOf(const std::string &name) const;
    // Reports the type of the value `res` that visiting `gct` produced.
    void recordResult(const GCT::node_ptr_t &gct, const std::any &res);
    // visit() without the report.
    std::any dispatch(const GCT::node_ptr_t &gct);
    // Functions whose body is in error. One declared without a return type then never gets
    // one, and a call of it would be reported as a call of an incomplete function.
    std::unordered_set<const void *> failedBodies_;

    // An argument of a call: its value node and the expression it was written as.
    struct Argument {
        node_handle_t node;
        GCT::node_ptr_t expr = nullptr;
    };

    using node_modifier_map_t = std::unordered_map<draft_node_ref_t, draft_node_ref_t>;

    // ---- Captures (the graph model, 3.7: a capture port is bound to what it captures) ----
    // A graph (function, lambda, branch arm, ...) that uses a value of an enclosing graph
    // holds it in a capture port. The port is bound to the node it stands for, its origin:
    // a node of the enclosing graph the value is computed in, or, for a value of the top
    // level of a module, its node on the root graph. Wherever the graph is called or made a
    // function value, the caller supplies the value of that same origin, as the caller sees
    // it (captureInto): the origin itself when it is in the caller's graph, the caller's own
    // capture port of it otherwise. A name is never looked up again where a function is
    // used: before, `k` was resolved where a function capturing `k` was called, and a local
    // `k` of the caller, or another module's `k`, silently took the place of the right one.
    struct CapturedValue {
        node_handle_t origin;
        node_handle_t port;
        // The name the graph uses it by, for dumps and diagnostics.
        std::string name;
    };
    // The capture ports of each graph, in the order they were made: the order of the closure
    // ports, or, once the graph is called directly, of the with-ports after its declared ones.
    std::unordered_map<DraftGraphBuilder *, std::vector<CapturedValue>> capturedValues_;
    // The origin of each capture port, and the name each origin was first captured by (a
    // variable is named by it in diagnostics).
    std::unordered_map<node_handle_t, node_handle_t> captureOriginOf_;
    std::unordered_map<node_handle_t, std::string> originNames_;
    // Graphs whose function value has been made (a DATA node of the function, or a FILL of it
    // with its captures). The value holds the captures the graph had then, so the graph
    // cannot take one more.
    std::unordered_set<DraftGraphBuilder *> valueUsedGraphs_;
    // The captures refused so (CaptureAfterRecursiveCall), each reported once.
    std::set<std::pair<DraftGraphBuilder *, node_handle_t>> lateCaptures_;
    // A direct call of a graph (a FUNC node): its captures are passed as With inputs after
    // the declared `with` arguments, in the order of the callee's capture ports, by the link
    // step (linkCaptureSites) once the module is built. `values` are the callee's captures as
    // the caller saw them when the call was built; a capture the callee makes later (a
    // recursive function that calls itself before it uses a value) is looked up then.
    struct CaptureSite {
        compile_graph_ptr_t caller;
        node_handle_t call;
        compile_graph_ptr_t callee;
        std::vector<node_handle_t> values;
        // When the call is a branch arm: the BRCH, which every captured value must precede.
        node_handle_t branch = nullptr;
        // How many of the callee's captures are linked already.
        size_t linked = 0;
    };
    std::vector<CaptureSite> captureSites_;
    /// The value of `origin` as `graph` sees it: `origin` when it is a node of `graph`;
    /// otherwise the capture port of `graph` bound to it, made (with the ports of the graphs
    /// between `graph` and the origin's graph) when there is none yet. `name` is the name
    /// the new ports are shown with.
    node_handle_t
    captureInto(const compile_graph_ptr_t &graph, node_handle_t origin, const std::string &name);
    /// The name a capture of `origin` is shown with: the qualified name of a top-level value
    /// (with its module when it is not the current module's), `fallback` otherwise.
    std::string captureNameOf(node_handle_t origin, const std::string &fallback) const;
    /// Registers the direct call `call` of `callee` in the current graph: its captures, as
    /// the current graph sees them now, to be linked by linkCaptureSites.
    void addCaptureSite(
        node_handle_t call, const compile_graph_ptr_t &callee, node_handle_t branch = nullptr);
    /// The link step for captures: passes to every direct call the captures of its callee,
    /// including the ones the callee made after the call was built, until nothing changes.
    void linkCaptureSites();
    // The decorated form of each function declared with decorators, by its own graph.
    std::unordered_map<DraftGraphBuilder *, compile_graph_ptr_t> decoratedOf_;
    // The functions that stand for library operators named as values (operatorValueGraphOf),
    // by operator URI and signature.
    std::unordered_map<std::string, compile_graph_ptr_t> operatorValueGraphs_;
    std::unordered_map<DraftGraphBuilder *, node_modifier_map_t> nodeModifierMaps_;
    // With-arguments bound to a callee by `f<a, b>`, consumed when the callee is invoked. Kept
    // beside the graph rather than as edges: the callee node is a value (a function literal, a
    // closure FILL whose with-inputs are its captures) that the binding must not alter.
    std::unordered_map<node_handle_t, std::vector<Argument>> boundWithArgs_;
    // The callee expression of such a binding, for the binding used as a value
    // (partialApplicationOf builds the call from it).
    std::unordered_map<node_handle_t, GCT::node_ptr_t> boundWithCallee_;
    // Variables: the nodes bound by `var` (declarations, `var` parameters, the ports that capture
    // them). Such a node does not hold a value but the variable's cell (TupleType::cell), which
    // is what a `var` parameter or a capture receives; a value is read out of it by `:var/get`
    // and stored by the assignment operators. Every other named node is an immutable value
    // (`let`, a parameter without `var`).
    std::unordered_set<node_handle_t> variables_;
    // What the compiler knows about a node's value beyond static data, for operators whose
    // result type follows constant arguments (StaticArg). `typeFoldedValues_`: the value of an
    // operator result that its argument types fix (its type folder), e.g. the shape
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
    // See the definition: the hint for a call that put with-arguments in the parentheses.
    std::string withArgumentsHint(
        const std::string &name, const std::vector<Argument> &arguments, size_t normCount) const;
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
        // The captured variables (the origins of the capture ports that hold a cell), in
        // capture order.
        std::vector<node_handle_t> variables;
        // Those the body itself stores into or hands to a `var` parameter. What its callees
        // and arms write is derived from `callees` (capturedWritesOf), not recorded here: at
        // a recursive call it is only an upper bound.
        std::set<node_handle_t> written;
        // The graphs the body calls directly or enters as arms.
        std::vector<DraftGraphBuilder *> callees;
    };
    enum class BodyState { Open, Built };
    std::unordered_map<DraftGraphBuilder *, CaptureSummary> captures_;
    std::unordered_map<DraftGraphBuilder *, BodyState> bodyStates_;
    // A captured variable as the current graph sees it at a call or branch: the node of the
    // current graph that holds it, and the variable itself (its origin).
    struct CapturedUse {
        node_handle_t variable;
        node_handle_t origin;
        bool written;
    };
    // An unwaited write of captured variables outside a sync block, reported once every body
    // is built: while a recursive function's body is open its writes are only an upper bound.
    struct PendingIgnoredWrite {
        std::vector<DraftGraphBuilder *> graphs;
        std::vector<node_handle_t> variables;
        camel::source::origin_id_t origin;
        std::string subject;
        std::string verb;
    };
    // (ModuleUnit::pendingIgnoredWrites.) The functions and lambdas of the module, in source
    // order, for the check that a function with side effects declares them (the `sync`
    // function modifier): ModuleUnit::builtFunctions.
    struct BuiltFunction {
        DraftGraphBuilder *graph;
        camel::source::origin_id_t origin;
        std::string name;
    };
    // Dropped effects (design principles 2.1): outside a sync block an effectful expression
    // whose result nothing uses is not connected to the graph's exit and does not run. That
    // is legal; the builder warns about it. Graphs whose exit depends on an effect, and the
    // graphs that are branch arms (reported through their branch, not as calls).
    std::unordered_set<DraftGraphBuilder *> effectfulGraphs_;
    std::unordered_set<DraftGraphBuilder *> armGraphs_;
    // The function bodies and arms of a module, in the order they were built, are in
    // ModuleUnit::builtBodies.
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
        GCT::node_ptr_t func = nullptr;
        // The declared type with the variables of enclosing instances replaced; the function's
        // own variables remain.
        FunctionType *funcType = nullptr;
        // The bindings in force where the template was declared (a generic function declared
        // inside an instance of another one).
        camel::core::type::type_bindings_t outerBindings;
        // Where the function was declared: instances are built there, not at the call. There is
        // one instance per set of types in the whole program, whichever module calls it.
        compile_graph_ptr_t ownerGraph;
        // The module that declares the function: the names of the body are resolved there.
        camel::core::module::module_ptr_t module;
        node_scope_ptr_t nodeScope;
        graph_scope_ptr_t graphScope;
        decorated_scope_ptr_t decoratedScope;
        std::shared_ptr<Scope<std::string, std::shared_ptr<GenericTemplate>>> templateScope;
        // The namespace and the `using namespace` in force where it was declared: its body
        // resolves its names as if it were built there.
        std::vector<std::string> namespacePath;
        std::vector<const std::vector<Reference> *> usingFrames;
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
    Type *expectedValueType_   = nullptr;
    struct ExpectedValueScope {
        Type *&current;
        Type *outer;
        ExpectedValueScope(Type *&slot, Type *expected)
            : current(slot), outer(std::exchange(slot, expected)) {}
        ~ExpectedValueScope() { current = outer; }
    };
    std::unordered_map<node_handle_t, camel::core::type::type_vec_t> explicitOperatorTypes_;
    std::unordered_map<node_handle_t, template_ptr_t> genericDrefs_;
    // The templates of functions written in place (anonymousTemplateOf): nothing else owns
    // them.
    std::vector<template_ptr_t> anonymousTemplates_;

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
    /// An error at `at` when `type` holds a Map or a Set whose key type is not allowed, which
    /// replacing a type variable can produce.
    void rejectInvalidCollectionKey(Type *type, const GCT::node_ptr_t &at);
    // An argument that names a generic function waits for the others: once they give the
    // parameter it is passed to a type, it is instantiated for that type and takes part like
    // any other argument (its node and type replace the name in `withInputs` / `normInputs`
    // and the type lists). Everything happens here, at the call; the graph only ever holds
    // the instances.
    compile_graph_ptr_t resolveGenericCall(
        const template_ptr_t &tmpl, std::vector<Argument> &withInputs,
        std::vector<Argument> &normInputs, camel::core::type::type_vec_t &withTypes,
        camel::core::type::type_vec_t &normTypes, const GCT::node_ptr_t &call,
        Type *expectedResult = nullptr);
    /// A function written in place whose type still holds a type variable (the element
    /// functions of a list comprehension): a template without a name, registered where it
    /// stands. Its marker node is what the name of a generic function gives.
    node_handle_t anonymousTemplateOf(const GCT::node_ptr_t &func);
    // ---- A generic function where a function is expected (design principles 2.10) ----
    /// The instance of `tmpl` that is a function of type `expected` (whose return type may
    /// still be open), or nullptr with the reason in `why`: the parameter types of `expected`
    /// fix the type variables, and the instance must then have exactly those parameters.
    compile_graph_ptr_t instantiateGenericAs(
        const template_ptr_t &tmpl, FunctionType *expected, const GCT::node_ptr_t &origin,
        std::string &why);
    /// The function value of the generic function `node` names, instantiated for the type
    /// the position expects; an error (GenericFunctionAsValue) that says which variables
    /// cannot be determined when `expected` is not a function type that fixes them.
    node_handle_t genericValueFor(
        node_handle_t node, camel::core::type::Type *expected, const GCT::node_ptr_t &origin);
    [[noreturn]] void rejectGenericValue(
        const template_ptr_t &tmpl, const GCT::node_ptr_t &origin, const std::string &why);
    /// An argument of a built-in operator that names a generic function. An operator's type
    /// rule is C++ code that needs the argument types, so it cannot say which function it
    /// expects; the instances the types of the other arguments suggest are tried (their
    /// element types first) and the one the operator accepts is taken. Replaces the name in
    /// `inputs` / `types`; an error when no instance is accepted.
    void settleGenericOperatorArguments(
        const oper_group_ptr_t &ops, std::vector<Argument> &withInputs,
        std::vector<Argument> &normInputs, camel::core::type::type_vec_t &withTypes,
        camel::core::type::type_vec_t &normTypes, const GCT::node_ptr_t &call);
    // Type-directed tree intrinsics expand to ordinary accesses, calls and fills.
    node_handle_t expandTreeCall(
        const oper_group_ptr_t &ops, const std::vector<Argument> &with,
        const std::vector<Argument> &norm, const GCT::node_ptr_t &call);
    void registerTreeOrigin(node_handle_t node, const GCT::node_ptr_t &origin, const char *kind);
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
        auto found = nodeScope_->get(name);
        // A value of the top level written further down, or in a module not yet built: built
        // now (the top level does not depend on the order it is written in).
        if (!found.has_value() && buildTopLevelValueNamed(name)) {
            found = nodeScope_->get(name);
        }
        return found;
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

    // Reports a name imported from two modules with the same signature (ImportNameConflict).
    void reportImportNameConflicts();
    // A use of the function `graph` in the current graph: its function value
    // (`callableAsResult`), or a direct call of it (`allowParameterization`; a FUNC node, or a
    // CALL of its value when its value was made before). `branch` is the BRCH node when the
    // call is a branch arm.
    node_handle_t createFuncDataNode(
        const compile_graph_ptr_t &graph, bool callableAsResult, bool allowParameterization,
        node_handle_t branch = nullptr);
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
    // A function with the parameters of the operator `op` that calls it: what a library
    // operator named as a value (`xs->map<trim>`) stands for. Null when the operator has no
    // such function (it changes an argument in place).
    compile_graph_ptr_t
    operatorValueGraphOf(const oper_idx_ptr_t &op, const GCT::node_ptr_t &origin);
    // The one function of the name `node` (a DREF to several overloads or to an operator
    // group) that has exactly the parameters and the result of `expected`, as a value; null
    // when none or several do.
    node_handle_t
    overloadValueFor(node_handle_t node, FunctionType *expected, const GCT::node_ptr_t &origin);
    // A function with the declared parameters of `target` that captures the names `target`
    // captured and calls it with them: what a function value of `target` refers to once the
    // captures of `target` have become with-parameters (createFuncDataNode).
    compile_graph_ptr_t forwardingGraphOf(const compile_graph_ptr_t &target);
    /// `f<w>` used as a value (design principles 2.10): the function that calls `f` with the
    /// `with` arguments bound to `target` and takes the rest, as its function value. It is a
    /// closure: a graph of its own that captures the bound values, which enter it through
    /// ports like any captured name; no node kind is added. `expected`, when given, is the
    /// function type the position asks for and selects among overloads. Returns nullptr,
    /// leaving the binding in place, when `target` does not name one function for these
    /// arguments.
    node_handle_t partialApplicationOf(
        node_handle_t target, const GCT::node_ptr_t &origin,
        camel::core::type::FunctionType *expected = nullptr);
    /// `t += u` for a `t` of a module type without a compound-assignment operator of its
    /// own (a tensor): the new value is computed and the variable bound to it, `t = t + u`
    /// (design principles 2.10). `group` is the operator group of the compound assignment
    /// and `operands` its two operands, already built. Returns nullptr when the form does
    /// not apply.
    node_handle_t compoundAssignmentByRebinding(
        const std::string &group, const GCT::node_ptr_t &gct,
        const std::vector<Argument> &operands);
    /// An error at `call` when the conversion `group` (`int`, `i32`, `float`, ...) is given
    /// a literal that does not fit the type (LiteralOutOfRange, LiteralStaticCastFailed).
    void rejectLiteralConversion(
        const std::string &group, FunctionType *funcType, const std::vector<Argument> &operands,
        const GCT::node_ptr_t &call);
    /// An error at `origin` when `node` still carries `with` arguments no partial
    /// application could be made of (PartialApplicationRefused): the binding is never
    /// dropped without a word.
    void rejectUnappliedBinding(node_handle_t node, const GCT::node_ptr_t &origin);
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
    std::set<node_handle_t> capturedWritesOf(DraftGraphBuilder *graph) const;
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
    // Once every body of the program is built: settles which graphs are effectful (a callee
    // may be written after its caller, in another module).
    void settleEffectfulGraphs();
    // Then warns for each body of the current module.
    void reportDroppedEffectsOfBodies();
    compile_graph_ptr_t buildDecoratedGraph(
        const std::string &funcName, const compile_graph_ptr_t &rawGraph,
        const std::vector<GCT::node_ptr_t> &annoNodes);
    node_handle_t applyDecoratorAnno(const GCT::node_ptr_t &annoNode, node_handle_t funcValueNode);
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
    node_handle_t visitTypeApplication(const GCT::node_ptr_t &gct);
    FunctionType *signatureForArgumentContext(node_handle_t target) const;
    node_handle_t visitAccsNode(const GCT::node_ptr_t &gct);
    node_handle_t visitBrchNode(const GCT::node_ptr_t &gct);
    /// A loop (design principles 2.10, 2.12, 2.13; camel/runtime/loop.h): the first values and
    /// the array where the loop is, a graph for the condition and one for the body, and one
    /// LOOP node calling them. The two graphs share one capture table (captureTwins_).
    node_handle_t visitLoopNode(const GCT::node_ptr_t &gct);
    /// Builds `graph`, a loop's condition or body graph made with its ports, from `exec`: the
    /// names of its Norm ports are declared in it, and its result is the value of the block.
    void buildLoopGraph(const compile_graph_ptr_t &graph, const GCT::node_ptr_t &exec);
    /// The condition and the body graph of a loop capture the same values in the same order
    /// (design principles 2.12: one capture table): a capture made into one is made into the
    /// other (captureInto).
    std::unordered_map<DraftGraphBuilder *, compile_graph_ptr_t> captureTwins_;
    /// How many loops each graph has, for the names of their graphs (`f.loop2.body`).
    std::unordered_map<DraftGraphBuilder *, size_t> loopCounts_;
    /// The BRCH/JOIN pair of a branch on `condNode`. The arms come from the CASE children of
    /// `gct`, or, when `arms` is given, are those graphs in order (built by buildArmGraph).
    /// No arm is added to those the program wrote (design principles 2.10).
    /// `valueless` (with `arms`): the branch is an `if` without `else`, whose join takes no
    /// value from its arms.
    node_handle_t buildBranch(
        const GCT::node_ptr_t &gct, node_handle_t condNode,
        const std::vector<compile_graph_ptr_t> *arms = nullptr, bool valueless = false);
    /// Refuses the `match` `gct` when it can meet a value none of its cases matches: it has
    /// no `case _` and does not list both values of a bool (MatchWithoutDefault, an error).
    void requireMatchDefault(const GCT::node_ptr_t &gct, node_handle_t scrutinee);
    /// The subgraph of one branch arm. Its body is the block `exec`, or what `body` builds
    /// (called inside the arm's scope) when given; `exec` then only locates the arm.
    compile_graph_ptr_t
    buildArmGraph(const GCT::node_ptr_t &exec, const std::function<node_handle_t()> &body = {});

    // ---- The tagged forms `T?` and `T!` (design principles 2.9;
    // docs/technical/29_nullable_and_fallible_values.md). Each construct below is a branch on
    // the one-bit tag of a value; nothing here adds a node kind. ----
    /// `a ?? b`, `a !! b`, `x ?-> f`.
    node_handle_t visitTaggedBrchNode(const GCT::node_ptr_t &gct);
    /// `match x { case null => ..  case v => .. }` and `case error e` over a tagged `x`.
    node_handle_t visitTaggedMatch(
        const GCT::node_ptr_t &gct, node_handle_t scrutinee, camel::core::type::TupleType *type);
    // ---- The enum (design principles 2.13). A value is a tag and the data of its variant; a
    // `match` reads the tag and takes the fields out in the arm of their variant. Nothing here
    // adds a node kind: the builder inserts the ordinary operators of operators/enum.h. ----
    /// The enum type a type name denotes: the declaration's own instance when the name is
    /// written without arguments. nullptr when the name denotes no enum.
    camel::core::type::EnumType *enumTypeOfName(const Reference &name);
    /// The declaration, variant and tag that the written variant `Shape::Rect` names. False
    /// when the name is not a qualified name at all; an enum name whose variant does not
    /// exist is reported here.
    bool enumVariantOfName(
        const std::string &written, const GCT::node_ptr_t &origin,
        camel::core::type::EnumType *&type, size_t &tag);
    /// `Shape::Circle(1.0)`, `Shape::Rect { w: 1.0, h: 2.0 }`, `Shape::Empty`.
    node_handle_t visitEnumConstruction(
        const GCT::node_ptr_t &gct, camel::core::type::EnumType *type, size_t tag);
    /// `match e { case Shape::Circle(r) => ... }`.
    node_handle_t visitEnumMatch(
        const GCT::node_ptr_t &gct, node_handle_t scrutinee, camel::core::type::EnumType *type);
    /// A call of the compiler's own operator `op` over the given GCT operands (a DREF of the
    /// operator and the operands), built like any call.
    /// A call the graph makes itself (`__enum_make__` and its like): `typeArgument` is given as
    /// `f::<T>`, for an operator whose result type is decided by a type the program does not
    /// write. The operands are GCT nodes; the call is lowered as a written one would be.
    node_handle_t applyInternalOperator(
        const char *op, camel::core::type::Type *typeArgument,
        const std::vector<GCT::node_ptr_t> &operands, const GCT::node_ptr_t &origin);
    /// The type of a node whose value is of enum type: its result slot is of that type too
    /// (the operator's placeholder type is not what the value is kept as; a slot of a type
    /// the collector does not trace would hide the value's references from it).

    // ---- Conditions (design principles 2.11, 2.12). `&&` and `||` are branches; a `T?` can
    // be compared with `null`; and where such a comparison is the condition (or an operand
    // `&&` joins in it), the name is a `T` in the arm that excludes null. Each is the
    // BRCH/JOIN pair the equivalent `if` or `match` gives: nothing here adds a node kind. ----
    /// The branch of `a && b`, `a || b`, or of an `if` whose condition compares names with
    /// `null`. nullptr (nothing built) for any other branch, which visitBrchNode builds.
    node_handle_t visitConditionBrchNode(const GCT::node_ptr_t &gct);
    /// `x == null` / `x != null` (either order) where `x` is a `T?`: the test of its tag, a
    /// `bool`. Where `x` has a type that is never null, the constant result of the test, with
    /// a warning (NullTestNeverNull; design principles 2.13). nullptr when the call `gct` of
    /// the operator group `group` is not that.
    node_handle_t nullComparison(
        const std::string &group, const GCT::node_ptr_t &gct, const std::vector<Argument> &operands,
        const camel::core::type::type_vec_t &types);
    /// The values a narrowing `if` binds its names to in the arm that excludes null: the name
    /// `x` in the then arm of `if x != null then ...` stands for one of these (design
    /// principles 2.11, 2.12). A lookup that finds one of these nodes found a narrowed name;
    /// a declaration that shadows the name finds another node.
    std::unordered_set<node_handle_t> narrowedValues_;
    /// Whether `expr` is a name narrowed to a `T` by an `if` around it (see narrowedValues_).
    bool isNarrowedName(const GCT::node_ptr_t &expr);
    /// `subject`, the value of the narrowed name `expr`, put back into `T?` so that a
    /// construct written for a `T?` (`??`, a `match` with `case null`) applies to it, with a
    /// warning that the name is known not to be null (KnownNotNull; design principles 2.13).
    /// `construct` names the construct in the warning.
    node_handle_t reopenNarrowedName(
        const GCT::node_ptr_t &expr, node_handle_t subject, const GCT::node_ptr_t &at,
        const char *construct);
    /// Warns about every `T!` that is evaluated in a body and that nothing takes
    /// (UnusedFallibleValue): a statement of a sync block, the arm of an `if` without
    /// `else`, and what a `main` without a declared return type ends with.
    void reportUnusedFallibleValues();
    /// Values dropped on purpose by `let _ = e`.
    std::unordered_set<node_handle_t> discardedValues_;

    // ---- Operator overloads of the program (design principles 2.12) ----------------------
    //
    // `func operator +(a: V, b: V): V` declares a function under the operator's reserved name
    // (`__add__`). Unlike every other name, an operator does not take the first layer that has
    // it (local, this module, imported, built-in): its overloads from every layer are one set,
    // and a call takes the one overload of the set that accepts its arguments; two that do are
    // an error, as is an overload whose parameters a built-in or imported overload takes.

    /// The overloads of one operator visible at a use of it.
    struct OperatorOverloads {
        /// The program's own, from every enclosing scope and every module that exports some.
        std::vector<compile_graph_ptr_t> graphs;
        /// The generic ones (`typeas` in a parameter), in this module and imported.
        std::vector<template_ptr_t> templates;
        /// Those of the built-in module and of the C++ modules imported, one group.
        oper_group_ptr_t ops;
        /// The module each imported overload comes from, for diagnostics (absent: this one).
        std::unordered_map<const void *, std::string> moduleOf;
    };
    /// The DREF of an operator (`__add__`) whose set has overloads of the program, with that
    /// set. A DREF of an operator without any is an ordinary DREF to the operator group.
    std::unordered_map<node_handle_t, OperatorOverloads> operatorOverloads_;
    /// The `!=` operators whose overloads (in operatorOverloads_) are those of `==`: an
    /// overload of `==` gives `!=`, the negation of its result (design principles 2.13).
    std::unordered_set<node_handle_t> negatedEqualities_;
    /// The generic overloads of an operator declared in this module, each with the scope it
    /// was declared in (visible from that scope and the scopes inside it).
    std::unordered_map<std::string, std::vector<std::pair<template_scope_ptr_t, template_ptr_t>>>
        operatorTemplates_;
    /// Signatures already checked by rejectOperatorConflict (a function is declared ahead and
    /// then defined).
    std::unordered_set<FunctionType *> checkedOperatorSignatures_;

    /// Collects the overloads of the operator `group` visible here.
    OperatorOverloads operatorOverloadsOf(const std::string &group);
    /// The overloads of `group` the current module imported (Module::importedOperatorOverloads),
    /// the operator groups merged into one; computed once per module and operator (the module
    /// changes while an instance of an imported generic function is built).
    const OperatorOverloads &importedOperatorOverloadsOf(const std::string &group);
    std::map<std::pair<const void *, std::string>, OperatorOverloads> importedOperators_;
    /// The DREF for a use of the operator `group` (visitDRefNode).
    node_handle_t operatorDrefOf(const std::string &group, const GCT::node_ptr_t &gct);
    /// The program's overload a call of an operator takes (an instance for a generic one), or
    /// nullptr when the call is the built-in group's to resolve (no overload of the program
    /// accepts the arguments; then the group reports what does not fit). Reports
    /// AmbiguousOperatorCall when more than one overload, built-in ones included, accepts them.
    compile_graph_ptr_t chooseOperatorOverload(
        const OperatorOverloads &set, std::vector<Argument> &withInputs,
        std::vector<Argument> &normInputs, camel::core::type::type_vec_t &withTypes,
        camel::core::type::type_vec_t &normTypes, const GCT::node_ptr_t &call,
        const std::function<bool(FunctionType *, bool)> &accepts);
    /// Whether the generic overload `tmpl` accepts arguments of the types `normTypes`: its type
    /// variables bind and every argument then fits its parameter (resolveGenericCall without
    /// the diagnostics).
    bool genericOperatorAccepts(
        const template_ptr_t &tmpl, const camel::core::type::type_vec_t &normTypes);
    /// OperatorOverloadConflict when the overload `funcType` of the operator `group` takes
    /// parameter types a built-in, C++ module or imported overload of it takes.
    void rejectOperatorConflict(
        const std::string &group, FunctionType *funcType, const GCT::node_ptr_t &gct);
    /// `export func operator ...`: exports the module's overloads of `group` (a generic one
    /// under `group#k`). Every module that imports this one, in any form, finds them there
    /// (Module::importedOperatorOverloads); they are not names an import lists.
    void exportOperatorOverloads(const std::string &group, const GCT::node_ptr_t &gct);
    /// True when the program has an overload of the operator `group` visible here.
    bool hasProgramOperatorOverloads(const std::string &group);

    // ---- Collections are not changed in place (design principles 2.12) --------------------

    /// The statement of the block being built whose value nothing takes: every statement but
    /// the last (which is the block's value), while it is visited. A call that is this
    /// statement may be the rebinding `acc = acc->append<v>` (rebindingAppendOf) or a pure
    /// operation whose result is lost (rejectUnusedPureResult).
    const GCT::Node *unusedStatement_ = nullptr;
    /// `acc = acc->append<v>` as the statement `gct`, `acc` a variable and `append` the
    /// built-in one: the same rebinding, written with the operator __assn_append__, which
    /// grows the array in place while only the variable refers to it. Returns nullptr when
    /// `gct` is not that form; the call is then built as written.
    node_handle_t rebindingAppendOf(const GCT::node_ptr_t &gct);
    /// `xs = xs->op<w...>` (or `xs = op<w...>(xs)`) as the statement `gct` of a sync block,
    /// `xs` a variable holding an array and `op` an operator of a module that declares an
    /// in-place form for the overload the call resolves to (OperatorTable::setInPlaceForm;
    /// design principles 2.13: `xs = xs->sort`). The same rebinding, written
    /// `:op/assn_owned(xs, <in-place form><w...>(:var/take(xs)))`, which works on the
    /// variable's own array when nothing else refers to it and on a copy otherwise
    /// (__builtin__var_take__). Returns nullptr when `gct` is not that form; the statement
    /// is then built as written.
    node_handle_t inPlaceRebindingOf(const GCT::node_ptr_t &gct);
    /// The calls inPlaceRebindingOf wrote: visitLinkNode calls the in-place form of the
    /// operator they resolve to.
    std::unordered_set<const GCT::Node *> inPlaceCalls_;
    /// The operator group the name `name` stands for here when it is an operator a module
    /// brings (the built-in module or an imported one) that nothing of the program shadows (the
    /// lookup order of visitDRefNode); nullptr otherwise.
    oper_group_ptr_t importedOperatorGroupNamed(const std::string &name);
    /// UnusedPureResult at the statement `stmt` when it is a call of an operator declared
    /// pure (`xs->append<5>` on its own line): the operation returns a new value and changes
    /// nothing, so the statement does nothing. `node` is what the statement built.
    void rejectUnusedPureResult(const GCT::node_ptr_t &stmt, node_handle_t node);
    /// The reads of variables (`:var/get`) made in a sync block, and the nodes the sync chain
    /// orders (reads, calls, operators and branches made in a sync block, in the order they
    /// were made): what markNonRetainingReads needs to know about a read and its users.
    /// The last statement of the body of each graph and the node it made: the value of the
    /// body. Where that value is dropped (a body declared `: void`, the element function of
    /// `foreach`, the arm of an `if` without `else`), rejectDiscardedValue checks it as a
    /// statement whose value nothing takes.
    std::unordered_map<const DraftGraphBuilder *, std::pair<GCT::node_ptr_t, node_handle_t>>
        bodyValueStatement_;
    /// The last statement of the block visitExecNode finished most recently, and its node.
    std::pair<GCT::node_ptr_t, node_handle_t> lastBlockStatement_{};
    /// UnusedPureResult at the last statement of the body of `graph` when it is a call of a
    /// pure operator: the body's value is dropped where `graph` is used (see above).
    void rejectDiscardedValue(const DraftGraphBuilder *graph);
    /// The graph of each function written in place (a lambda), by its construction node.
    std::unordered_map<const GCT::Node *, const DraftGraphBuilder *> lambdaGraphs_;
    std::vector<node_handle_t> syncedReads_;
    std::unordered_set<node_handle_t> chainedNodes_;
    /// Once every body is built: turns into `:var/peek` each read whose value cannot be kept by
    /// anything but the variable, so that the read leaves the variable's sole-reference mark
    /// set and the next `acc = acc->append<v>` still appends in place
    /// (`out = out->append<i>; total += len(out)` in a loop stays linear). See the definition
    /// for the conditions; a read that does not meet every one of them stays `:var/get`.
    void markNonRetainingReads();
    /// Function graphs whose return type is not declared but taken from their body.
    std::unordered_set<DraftGraphBuilder *> inferredExitGraphs_;

    /// Binds `node` to a fresh name no program can write, so that arms can capture it.
    std::string bindTemporary(node_handle_t node);
    /// The call `op(<name>)` of one of the built-in operators over tagged values.
    node_handle_t
    applyTagOperator(const char *op, const std::string &name, const GCT::node_ptr_t &origin);
    /// A CAST of `value` to `target`, made by the compiler (no conversion rule is consulted).
    node_handle_t
    castNodeTo(node_handle_t value, camel::core::type::Type *target, const GCT::node_ptr_t &origin);
    /// `node` where a value of type `target` is expected (design principles 2.10): `node`
    /// itself when its type is assignable; otherwise the value put into `T?` / `T!` (one
    /// CAST per level), `null` as a `T?`, or the empty literal `[]` given the element type of
    /// `target`. nullptr when the value does not enter (camel::core::type::entersImplicitly).
    node_handle_t
    coerceTo(node_handle_t node, camel::core::type::Type *target, const GCT::node_ptr_t &origin);
    /// The same at the exit of a branch arm already built: what `arm` gives enters `target`.
    void coerceArmExit(
        const compile_graph_ptr_t &arm, camel::core::type::Type *target,
        const GCT::node_ptr_t &origin);
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
};

} // namespace camel::compile::gir
