// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include <any>
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>

#include "camel/common/scope.h"
#include "camel/core/context/context.h"
#include "camel/core/error/abort.h"
#include "camel/core/module/module.h"
// The payload of a module entity: a generic function of another module arrives as it was
// exported (genericTemplatesOfEntity).
#include "core/module/entity_internal.h"

#include "camel/compile/gct.h"
#include "camel/compile/gir/draft_graph_builder.h"
#include "camel/compile/gir/rule_templates.h"
#include "camel/compile/gir/value_entity.h"
#include "camel/compile/semantic_recorder.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/mm.h"
#include "camel/core/mm/root_handle.h"
#include "camel/core/node_attrs.h"
#include "camel/core/tags.h"
#include "camel/core/type/enum.h"
#include "camel/core/type/resolver.h"
#include "camel/core/type/typevar.h"

namespace camel::compile::gir {

using void_ptr_t   = void *;
using Type         = camel::core::type::Type;
using FunctionType = camel::core::type::FunctionType;
using data_ptr_t   = camel::core::data::data_ptr_t;

using compile_graph_ptr_t   = std::shared_ptr<DraftGraphBuilder>;
using node_handle_t         = camel::runtime::DraftNodeRef;
using node_scope_t          = Scope<std::string, node_handle_t>;
using node_scope_ptr_t      = std::shared_ptr<node_scope_t>;
using graph_set_t           = std::vector<compile_graph_ptr_t>;
using graph_set_ptr_t       = std::shared_ptr<graph_set_t>;
using graph_scope_t         = Scope<std::string, graph_set_ptr_t>;
using graph_scope_ptr_t     = std::shared_ptr<graph_scope_t>;
using decorated_scope_t     = Scope<std::string, compile_graph_ptr_t>;
using decorated_scope_ptr_t = std::shared_ptr<decorated_scope_t>;

class Builder;

struct GenericTemplate;
struct ConstasValue;

/// A `constas` parameter of a generic declaration and the compile-time value a call bound to
/// it (design principles 5.4): the value selects the instance and the body reads it as a static
/// value. It is not a runtime argument, so the instance has no port for it.
struct ConstasValue {
    Type *type = nullptr;      // the declared type of the parameter, the other extents substituted
    Type *valueType = nullptr; // the type of the constant the call wrote
    data_ptr_t data;           // the constant itself, the instance key's value
    node_handle_t node = {};   // the node the call gave it as
};
using ConstasValues = std::map<std::string, ConstasValue>;

/// One set of types a generic function was instantiated at, and the graph built for it.
struct GenericInstance {
    camel::core::type::type_bindings_t bindings;
    // The compile-time values bound to the `constas` parameters of the declaration, and the
    // integer ones among them as the extent bindings they stand for.
    ConstasValues constas;
    camel::core::type::ExtentValues extents;
    compile_graph_ptr_t graph;
    // An error was found in the body: later calls with the same types fail quietly.
    bool failed = false;
};

/// The generic functions of one name declared in one block. They are told apart by their
/// parameter types, exactly as ordinary overloads are (design principles 4.4): a call takes the
/// one whose parameters accept its arguments. One name is one template until a second generic
/// function is declared beside it, and then one group, whose first template is the one an
/// unqualified read of the name means.
using generic_template_ptr_t = std::shared_ptr<GenericTemplate>;
using GenericGroup        = std::vector<generic_template_ptr_t>;
using generic_group_ptr_t = std::shared_ptr<GenericGroup>;
/// What the scope of a name and a generic DREF holds: one template, or a group of overloads.
/// `->` answers the first of the group, which is the template a name with one overload is.
class generic_ref_t {
  public:
    generic_ref_t() = default;
    /// Defined where GenericTemplate is complete: it reads the template's name.
    generic_ref_t(generic_template_ptr_t one);
    explicit generic_ref_t(generic_group_ptr_t group) : group_(std::move(group)) {}
    explicit operator bool() const { return group_ != nullptr && !group_->empty(); }
    const generic_group_ptr_t &group() const { return group_; }
    /// The template a name with one overload stands for, and the first of a group.
    generic_template_ptr_t one() const {
        return group_ != nullptr && !group_->empty() ? group_->front() : nullptr;
    }
    generic_template_ptr_t operator->() const { return one(); }

  private:
    generic_group_ptr_t group_;
};
/// The names of the generic functions in force where a template was declared: one entry per
/// name, holding the one generic function of that name or the group of its overloads.
using template_scope_t     = Scope<std::string, generic_ref_t>;
using template_scope_ptr_t = std::shared_ptr<template_scope_t>;

/// A generic declaration: the function that still holds type variables, and everything an
/// instance of it needs. The instance key is the bindings with the `constas` values, and the
/// instance is built in the module that declared the function, not at the call.
struct GenericTemplate {
    std::string name;
    GCT::node_ptr_t func = nullptr;
    // The declared type with the variables of enclosing instances replaced; the function's
    // own variables remain.
    FunctionType *funcType = nullptr;
    // The type parameters the declaration put at its head, in order, whether or not the
    // signature writes them; an explicit `::<...>` binds them by position or by name.
    std::vector<std::string> headParams;
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
    template_scope_ptr_t templateScope;
    // The node attributes in force where it was declared (currentAttrs_).
    camel::core::attr::AttrSetId attrs = camel::core::attr::kNoAttrs;
    // The namespace and the `using namespace` in force where it was declared: its body
    // resolves its names as if it were built there.
    std::vector<std::string> namespacePath;
    std::vector<const std::vector<Reference> *> usingFrames;
    std::vector<GenericInstance> instances;
    // Instances whose bodies are being built right now, one inside another.
    size_t openInstances = 0;
};

inline generic_ref_t::generic_ref_t(generic_template_ptr_t one)
    : group_(std::make_shared<GenericGroup>()) {
    if (one != nullptr) {
        group_->push_back(std::move(one));
    }
}
/// The group a scope entry holds, in one expression: the entry is always a group.
inline GenericGroup *genericGroupOf(const generic_ref_t &ref) { return ref.group().get(); }

/// The generic functions an imported name brings: the payload of the module entity is the list of
/// the templates of that name, one of them for a name declared once and all of them when the name
/// is declared as several generic overloads (`makeGenericTemplates`). A call of the name picks
/// among them exactly as it does for a name of this module (design principles 4.4, 5.5).
inline std::vector<generic_template_ptr_t>
genericTemplatesOfEntity(const camel::core::module::Entity &entity) {
    if (!camel::core::module::detail::EntityAccess::isGenericTemplate(entity)) {
        return {};
    }
    const auto &tmpls = camel::core::module::detail::EntityAccess::genericTemplates(entity);
    std::vector<generic_template_ptr_t> templates;
    templates.reserve(tmpls.size());
    for (const std::shared_ptr<void> &one : tmpls) {
        templates.push_back(std::static_pointer_cast<GenericTemplate>(one));
    }
    return templates;
}

/// The payload a module exports for a name with generic functions: every template of the group,
/// so that an importer selects the overload a call takes.
inline std::vector<std::shared_ptr<void>> genericTemplatePayloadOf(const generic_ref_t &ref) {
    std::vector<std::shared_ptr<void>> payload;
    if (GenericGroup *group = genericGroupOf(ref); group != nullptr) {
        payload.reserve(group->size());
        for (const generic_template_ptr_t &tmpl : *group) {
            payload.push_back(std::static_pointer_cast<void>(tmpl));
        }
    }
    return payload;
}

/// What a declaration's `when` condition says at one instantiation (design principles 5.13).
/// The condition is a compile-time predicate over the declaration's type parameters and `with`
/// parameters; it is evaluated when the declaration is instantiated or selected, never when it
/// is parsed. A condition the compiler cannot answer is never taken for `true`.
enum class WhenOutcome {
    Holds,          // it is true
    Fails,          // it is false: the declaration is not applicable here
    NeedsWithValue, // it reads a `with` parameter whose value the call did not give
    NotBool,        // its value is not a bool
    NotEvaluable,   // it is not a compile-time predicate (a runtime value, a user call, ...)
    TypeNotConcrete,// a type query still sees a type variable
    UnknownName,    // a name outside the declaration (an ordinary parameter, a body name, ...)
    // A predicate call the condition cannot make, though the name exists.
    NotImported,    // declared by a module this one does not import
    NotCompileTime, // imported, but not a generic function answered at compile time
    PredicateAmbiguous, // two imports bring predicates of that name, and neither is chosen
};

struct WhenResult {
    WhenOutcome outcome = WhenOutcome::NotEvaluable;
    /// The name, type or expression the outcome is about, for the report.
    std::string subject;
    /// A sentence saying what is wrong or missing, for the report.
    std::string detail;
    bool holds() const { return outcome == WhenOutcome::Holds; }
    bool fails() const { return outcome == WhenOutcome::Fails; }
};

/// The value of a `constas` constant as the key it stands for when it names a field or a
/// position (`t.[n]`): the text of a string, the digits of an integer, `true` / `false`.
std::string constasKeyOf(const camel::core::data::data_ptr_t &data);
/// The value as an instance name shows it, strings quoted: `f(3)` is `f<3>`, `f("w")` is
/// `f<"w">`.
std::string constasTextOf(const camel::core::data::data_ptr_t &data);

/// The graph builder of a program (design principles 10.1, "模块"; the graph model,
/// docs/design/graph-model/README.cn.md 3.9). One builder serves every module of the program:
/// the functions of all modules are graphs of their own, and the values of the top level of
/// every module are nodes of ONE root graph, `__root__`, whose exit is the call of the entry
/// module's `main`. A function reaches a value of the top level of any module through a
/// capture port bound to that value's node (captureInto), never by looking up its name where
/// the function is called, so that a function keeps its meaning wherever it is used.
class Builder {
  public:
    /// A builder for a program of its own: it owns the program's export table (ProgramExports).
    explicit Builder(const camel::core::context::context_ptr_t &context)
        : context_(context), exports_(&ownExports_) {}

    /// A builder that declares its modules into `exports`: the program the table belongs to
    /// (the context's own program, whose table other readers use too).
    Builder(
        const camel::core::context::context_ptr_t &context,
        camel::core::module::ProgramExports &exports)
        : context_(context), exports_(&exports) {}

    /// The builder of the program compiled on `context`, created on first use.
    static Builder &of(const camel::core::context::context_ptr_t &context);

    /// The entities the modules of this program export. One table per program: declaring a
    /// module again for another program (a pass compiled as a program of its own) writes that
    ///  program's table, not this one (task).
    camel::core::module::ProgramExports &exports() { return *exports_; }
    const camel::core::module::ProgramExports &exports() const { return *exports_; }

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

    /// Steps two and three for a library used as a program of its own whose entry is the
    /// function `entryFunction`: the same build as buildProgram, with the call of that function
    /// as the exit of the root graph instead of the call of `main`. The root graph holds the
    /// top-level values of the module, so they are computed before the call, exactly as a
    /// program computes them before `main` (design principles 10.1); the function's captures
    /// are its value's closure, bound by that call. A pass written in Camel is run so
    /// (UserDefinedModule::compileAsProgram). False when the function has no graph or a module
    /// is in error.
    bool buildLibraryEntry(
        const camel::core::module::module_ptr_t &module, const std::string &entryFunction);

    compile_graph_ptr_t rootGraph() const { return rootGraph_; }

    /// The instance of one registered generic rule at `types`: its two sides and its condition,
    /// built here, in the builder the rule's templates belong to (where it was declared). Answers
    /// false with `why` when no instance can be made, and reports nothing on the way: a failed
    ///  instantiation is quiet (the rule on
    /// `generic-rule-instantiation-set-and-predicate`), and only `why_not` repeats `why`.
    ///
    /// Public because the context's table of generic rules calls it where a rule is applied
    /// (rule_templates.cpp): the library was built long before, and an instance belongs to the
    /// builder that declared the rule.
    bool instantiateRuleTemplates(
        const GenericRuleEntry &entry, const std::vector<Type *> &types, compile_graph_ptr_t &lhs,
        compile_graph_ptr_t &rhs, compile_graph_ptr_t &when, std::string &why);

    ///  What the condition of a generic rule says about one type tuple, at compile time (
    /// of 2026-10-24 on `generic-rule-check-surface-and-multi-parameter-instances`: `check`
    /// enumerates the instances of a generic rule over the registered types that its condition
    /// admits). The condition's type-level part alone is asked -- the type arguments of its
    /// predicate calls are bound to the tuple -- so nothing is instantiated and nothing is
    /// sampled: a conjunction answers as soon as one side settles it, and a condition that reads
    /// the values it matches is `ValueDependent`. Public for the same reason
    /// `instantiateRuleTemplates` is (rule_templates.cpp calls it where a rule is checked).
    RuleConditionAt
    ruleConditionVerdict(const GenericRuleEntry &entry, const std::vector<Type *> &types,
                         std::string &why);

    /// Runs `body` with the state of `module`'s unit in force: the same state a top-level
    /// declaration of that module is built in (its diagnostics, its construction tree, its module
    /// and its scopes, `UnitContext`). An instance of a generic rule of that module is built this
    /// way, long after the module was built, because a body build resolves names as the module
    /// does. Answers false when this builder declared no such module; the state is restored
    /// whatever `body` does, exception or not.
    bool inModule(
        const camel::core::module::module_ptr_t &module, const std::function<void()> &body);

  private:
    // ---- The modules of the program ----
    // What the builder keeps of one module: its tree, its diagnostics and the scopes of its
    // top level (the names it declares; its functions are graphs whose scopes are entered
    // from these). A function body of the module is built with these in force, whichever
    // module asks for it.
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
        template_scope_ptr_t templateScope;
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
        //  The body-less declarations of the module (step 1): per name, the
        // operator URI each rule declares and the rule itself. They are exported as one
        // operator group once every declaration of the module has been read.
        struct NativeOperators {
            std::string name;
            // Where the first declaration of the name is written, for a diagnostic about the
            // name itself (a declaration that collides with an entity the C++ module exports).
            camel::source::origin_id_t origin = camel::source::kInvalidOriginId;
            std::vector<std::pair<std::string, camel::core::type::resolver_ptr_t>> rules;
        };
        std::vector<NativeOperators> nativeOperators;
        // The groups those declarations make, by name: the ones the module exports and the
        //  private ones, which only its own bodies call. A module's
        // private operator is not an entity of the module.
        std::unordered_map<std::string, oper_group_ptr_t> nativeOperatorGroups;
        // The names of the body-less declarations of the module that declare a pass, not an
        // operator (design principles 9.3): `when is_function::<F>()` is the pass's
        // applicability. The pipeline reads the declaration from the module's `.cml` file and
        // resolves the kernel by module and name; no graph, no operator group and no export is
        // built for it.
        std::set<std::string> passDeclarations;
        // What the link step reports per module, collected while its bodies are built.
        std::vector<compile_graph_ptr_t> builtBodies;
        std::vector<PendingIgnoredWrite> pendingIgnoredWrites;
        // The names the module's source declares anywhere: a function or a type declared, and the
        // NREF that binds a name in any body (`let`, `var`, a parameter). Read once, on the first
        // name the declaration-site check cannot resolve (resolveGenericTemplateNames); the module
        // is one scope, so a name a later declaration gives is not a misspelling.
        std::unordered_set<std::string> sourceNames;
        bool sourceNamesRead = false;
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
    // entry module (design principles 10.2: the module is left out where it is not needed).
    static std::string qualifiedName(const ModuleUnit &unit, const std::string &name);
    // The top-level functions of every module by their graphs.
    std::unordered_map<DraftGraphBuilder *, TopLevelDeclaration *> topLevelFunctions_;
    // A body-less declaration that runs the semantics of an `#impl` function (design
    // principles 10.4): the operator's URI, and the group a direct call of the function
    // resolves in (one rule, the kernel's).
    struct ImplKernel {
        std::string name;
        std::string uri;
        oper_group_ptr_t group;
    };
    // The semantic bodies of the `#impl` functions of every module, by their graphs (design
    // principles 10.4): a direct call of the function is a call of the kernel instead; the
    // graph is kept for what asks to look inside (autodiff, fusion, export, a rule).
    std::unordered_map<DraftGraphBuilder *, ImplKernel> implKernels_;
    //  The kernel a generic `#impl` function declares, by its template (rule impl-kernel-site;
    //  design principles 10.4, one operator, per-type executors). A generic function's
    // instances have signatures of their own, so which rule of the kernel's declaration is the
    // executor is decided when the instance is built (adoptImplKernel); an instance whose types
    // no rule accepts keeps the body, which is the semantics.
    struct ImplTemplateKernel {
        std::string name;
        std::string uri;
        // The kernel's declaration as a whole: the rules an instance may adopt.
        oper_group_ptr_t group;
    };
    std::unordered_map<const GenericTemplate *, ImplTemplateKernel> implTemplateKernels_;
    // The applicability condition a rule of a body-less declaration was written with (design
    // principles 4.4: `when` says where a rule applies), by the rule's resolver. Its one reader
    // is the `#impl` machinery: for the kernel of an `#impl` function the condition is what says
    // which instances of that function the rule is the executor of, so an instance outside it
    // has no executor and runs the function's body (design principles 10.4). A rule without a
    // condition applies wherever its signature does.
    struct NativeRuleCondition {
        GCT::Tree::WhenClause clause;
        // The module that declares the rule: its condition's predicate is looked up there,
        // wherever the instance that selects the rule is built (evaluateWhenCondition).
        const ModuleUnit *unit = nullptr;
    };
    std::unordered_map<const camel::core::type::FuncTypeResolver *, NativeRuleCondition>
        nativeRuleConditions_;
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
    // ---- Names (names.cpp; design principles 10.2) ----
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
    //  A body-less declaration of the current module (design principles 6.4, step
    // 1): checked against the operator table and the loaded executors, then kept as a rule of
    // the operator group of its name. A body-less declaration with the pass applicability is
    // not an operator; see `isPassDeclaration`.
    void declareNativeOperator(const GCT::node_ptr_t &gct);
    // Whether a body-less declaration declares a pass (design principles 9.3): its applicability
    // condition is `is_function::<T>()`, the same condition the pipeline reads from the module's
    // `.cml` file. A pass is a function the pipeline applies to the program, not an operator of
    // the module; nothing of it is compiled or exported.
    static bool isPassDeclaration(const GCT::node_ptr_t &gct);
    // Exports the operator groups of the body-less declarations of the current module, one group
    // per declared name, after its declarations were read. A name the module's export list
    //  `exportList` does not have stays private to the module.
    void exportNativeOperators(const GCT::node_ptr_t &exportList);
    // The entity `ref` names in the module whose code is being built: one of its body-less
    // declarations (exported or private) or a name the C++ module it declares brings, before
    //  what an import brings under the same name (design principles 10.1, 10.2; handover).
    std::optional<camel::core::module::entity> ownNativeEntity(const Reference &ref);
    // The operator URI a body-less declaration names, from its `#op<'...'>` tag when it has one.
    std::optional<std::string> nativeOperatorUri(const GCT::node_ptr_t &gct);
    // Notes the nodes of the root graph added since `firstNode` as nodes of the current module.
    void claimRootNodes(size_t firstNode);
    // Makes the root graph the first time a module is declared.
    void ensureRootGraph();
    // The exit of the root graph: the call of the entry function — `main` for a program, the
    // function named by `libraryEntryFunction_` for a library built as a program of its own
    // (EntryModuleMissingMain when a program has none). It takes the parameters that function
    // takes, and the root graph's type is that function's when the build is a library's
    // (buildLibraryEntry), so that the program's root has the signature of its entry.
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
    // The function the root graph calls, when the build is a library used as a program of its
    // own (buildLibraryEntry); empty for a program, whose entry is `main`.
    std::string libraryEntryFunction_;

    node_scope_ptr_t nodeScope_;
    graph_scope_ptr_t graphScope_;
    decorated_scope_ptr_t decoratedScope_;

    camel::core::context::context_ptr_t context_;
    camel::core::module::module_ptr_t module_;
    camel::core::error::diagnostics_ptr_t diags_;
    /// The speculative scope in force, if any: the only thing it changes is what a *body build*
    /// reports (`BodyDiagnostics`) and how a body that does not compile is answered
    /// (`instantiateGeneric` returns nullptr instead of aborting). Every other diagnostic goes to
    /// `diags_` wherever it is made, scope or no scope.
    class SpeculativeInstantiation;
    SpeculativeInstantiation *speculative_ = nullptr;
    /// The program's export table (see the constructors): `exports_` points at `ownExports_`
    /// for a builder of a program of its own, or at the context's table for the context's
    /// program. Never at a module's table: that is the module's own (native) declarations.
    camel::core::module::ProgramExports ownExports_;
    camel::core::module::ProgramExports *exports_;

    bool synced_;
    bool varied_;
    // The numeric permissions in force where the builder is (design principles 7.5): those of
    // the nearest GCT EXEC, which already include every enclosing block and function
    // (GCT::Node::numericPermissions). Every OPER node made while they are in force gets them
    // (stampPermissions); a function's graph starts from those of its own body, whatever was
    // in force where it is built from.
    camel::core::sem::PermissionMask numericPermissions_ = 0;
    void stampPermissions(node_handle_t operNode) const;
    /// Sets numericPermissions_ for a scope and puts the previous value back at its end.
    struct NumericPermissionScope {
        camel::core::sem::PermissionMask &current;
        camel::core::sem::PermissionMask outer;
        NumericPermissionScope(
            camel::core::sem::PermissionMask &c, camel::core::sem::PermissionMask permissions)
            : current(c), outer(c) {
            current = permissions;
        }
        ~NumericPermissionScope() { current = outer; }
    };
    // The node attributes in force where the builder is (camel/core/node_attrs.h): those
    // decorators granted, as the program is built, to the functions
    // and decorated blocks the builder is in, lexically: a function built from elsewhere (a
    // top-level function referred to before its turn, an instance of a generic function)
    // starts from what was in force where it is written. Stamped with the permissions
    // (stampPermissions).
    camel::core::attr::AttrSetId currentAttrs_ = camel::core::attr::kNoAttrs;
    /// The EXEC that is the body of the function being built: its tags are the body region's
    /// (region 0), not a block region of their own.
    const GCT::Node *functionBody_ = nullptr;
    /// Sets currentAttrs_ for a scope and puts the previous value back at its end.
    struct AttrScope {
        camel::core::attr::AttrSetId &current;
        camel::core::attr::AttrSetId outer;
        AttrScope(camel::core::attr::AttrSetId &c, camel::core::attr::AttrSetId attrs)
            : current(c), outer(c) {
            current = attrs;
        }
        ~AttrScope() { current = outer; }
    };
    /// What the decorators of the function `func` (a FUNC with ANNO children) grant as the
    /// program is built, and how many of them do: the innermost ones (written last) that
    /// name an operator declared to grant an attribute, with constant with-arguments
    /// (OperatorTable::grantOf), up to the first that does not. The others are applied when
    /// the decorated function is made, as before.
    std::pair<camel::core::attr::AttrSetId, size_t> staticGrantsOf(const GCT::node_ptr_t &func);
    /// The attributes one decorator grants as the program is built; nullopt when it is not
    /// such a decorator (see staticGrantsOf). Remembered per ANNO node.
    std::optional<camel::core::attr::AttrSetId> staticGrantOf(const GCT::node_ptr_t &anno);
    std::unordered_map<const GCT::Node *, std::optional<camel::core::attr::AttrSetId>>
        staticGrants_;
    /// The decorators of `func` that are applied to its function value (the ones
    /// staticGrantsOf does not take), outermost first.
    std::vector<GCT::node_ptr_t> appliedDecoratorsOf(const GCT::node_ptr_t &func);
    /// A call of a decorated block (`@d { ... }`: a LINK of a FUNC with decoratedBlock()).
    node_handle_t visitDecoratedBlock(const GCT::node_ptr_t &link, bool unused);
    // The bodies whose value is dropped: a decorated block whose value nothing takes is a
    // function whose value is dropped, as a plain block drops the value of its last statement.
    std::unordered_set<const GCT::Node *> droppedBodies_;
    std::unordered_set<const DraftGraphBuilder *> droppedGraphs_; // their graphs
    // The graphs of decorated blocks: written as blocks, they are not functions a program
    // declares, and get no ignored-write check of their own.
    std::unordered_set<const DraftGraphBuilder *> blockGraphs_;
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

    // An argument of a call: its value node and the expression it was written as. An argument
    // may be written by name (`f(1, by: 2)`, `f<rounds: 8>`, design principles 4.4); the name
    // is resolved against the callee when the call is bound (bindArguments).
    struct Argument {
        node_handle_t node;
        GCT::node_ptr_t expr               = nullptr;
        std::optional<std::string> name;
    };

    using node_modifier_map_t = std::unordered_map<draft_node_ref_t, draft_node_ref_t>;

    // ---- Captures (the graph model, 3.7: a capture port is bound to what it captures) ----
    // A graph (function, lambda, loop part, ...) that uses a value of an enclosing graph
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
    void addCaptureSite(node_handle_t call, const compile_graph_ptr_t &callee);
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
    // The `f<w>` expression that made the binding. Everything the partial application makes is
    //  located there: the expression itself, not the statement it is written in (1.6).
    std::unordered_map<node_handle_t, GCT::node_ptr_t> boundWithOrigin_;
    // The candidates of such a binding when the pass being used has several and the position
    //  has not chosen one. The ambiguity is reported when the binding is refused for
    // good (rejectUnappliedBinding): visiting a value first must not preempt the position
    // that may still say which function is meant (`apply(scale<2>, 1.5)`).
    std::unordered_map<node_handle_t, std::vector<camel::core::type::FunctionType *>>
        ambiguousBindings_;
    //  The default of a parameter (design principles 4.4): the constant a call
    // fills in when it leaves the parameter out, and the expression it was written as (where a
    // diagnostic about it points). Evaluated once, where the declaration is, and the same value
    // goes into every call: a default is never evaluated at a call site, and it is not part of
    // the function's type, so a function value made from the declaration does not carry it.
    // Ordinary parameters and `with` parameters follow the same rule, each aligned with its own
    // parameter list.
    struct ParameterDefaultValue {
        data_ptr_t data;
        GCT::node_ptr_t expr;
    };
    struct DeclarationDefaults {
        std::vector<std::optional<ParameterDefaultValue>> with;
        std::vector<std::optional<ParameterDefaultValue>> norm;
    };
    // Keyed by the declaration's graph (an instance for a generic declaration) or by the
    // template of a generic declaration.
    std::unordered_map<const void *, DeclarationDefaults> defaults_;
    /// A native operator's declaration has no graph of its own: a call reaches it through the
    /// operator group, which carries the URI of each of its entries. The defaults of such a
    /// declaration (a `#op` line with `with <p: int = 8>`) are kept under that URI.
    std::unordered_map<std::string, DeclarationDefaults> nativeDefaults_;
    /// A function value made by explicit type arguments (`f::<int>`) is instantiated where it is
    /// written, so the call cannot reach the declaration's graph: the defaults are kept under
    /// the value here. A value that is then bound to a name carries no defaults, as any function
    /// value does (design principles 4.4).
    std::unordered_map<node_handle_t, DeclarationDefaults> defaultsOfValue_;
    /// The static node that carries a declaration's default into a graph, so that every call in
    /// that graph which leaves the parameter out reads the one node: the value is computed once,
    /// at the declaration, not once per call. Keyed by the graph, then by the default's
    /// expression (one per parameter of a declaration).
    std::unordered_map<const void *, std::unordered_map<const void *, node_handle_t>> defaultNodes_;
    /// Evaluates the defaults `decl` writes and keeps them under `owner` (a graph or a generic
    /// template). `parametersInScope` is false when the declaration's own parameters are not
    /// names of the scope (a declaration at a module's top level): a default that reads one is
    /// then reported as not constant, not as an unknown name.
    void evaluateDefaults(
        const GCT::node_ptr_t &decl, FunctionType *funcType, const void *owner,
        bool parametersInScope);
    /// The defaults of a callee, when the callee is one declaration: its graph, or the template
    /// of a generic declaration. Null for a function value, an operator, or an overload set.
    const std::vector<std::optional<ParameterDefaultValue>> *defaultsOfCallee(
        node_handle_t callee, bool isWith) const;
    /// One declaration a name stands for: the names of its parameters -- `with` ones first, then
    /// the ordinary ones -- and the owner its parameter defaults are kept under (a graph, a
    /// generic template, or the URI of a native rule).
    struct CalleeDeclaration {
        const void *owner = nullptr;
        /// A native rule's URI: a default of a body-less declaration is kept under it
        /// (`nativeDefaults_`), the one name the call and the declaration share.
        std::string uri;
        std::vector<std::string> withNames;
        std::vector<std::string> normNames;
        /// Whether the declaration gives every parameter a name: one that does not is one a named
        /// argument cannot reach, and the call is refused rather than bound to a guess.
        bool readable = false;
    };
    /// The declarations a callee stands for and the name they are written under: a generic
    /// function's templates, the graphs of a name with several functions, or the rules of an
    /// operator group. Null for a value and for a name with one Camel declaration (whose call
    /// reads its signature).
    struct CalleeSet {
        std::string label;
        std::vector<CalleeDeclaration> declarations;
    };
    std::optional<CalleeSet> declarationsOfCallee(node_handle_t callee) const;
    /// Whether one of the declarations gives the parameter at `position` of `isWith`'s list a
    /// default. A call whose callee is several declarations fills no default (one declaration
    /// writes it), so it reports the parameter instead.
    bool anyDeclarationDefaults(const CalleeSet &set, size_t position, bool isWith) const;
    /// Binds the arguments written at a call to the callee's parameters: a name to the parameter
    /// it names, an omitted parameter to its default (design principles 4.4). Reports an unknown
    /// name, a parameter given twice, and a parameter left without a value. `isWith` selects the
    /// `with` parameters or the ordinary ones; the rule is one rule.
    ///
    /// The list comes back in the declaration's order and as long as the declaration's parameter
    /// list, which is the one order the call reads by index: `parameterOf(i)` in the deferred
    /// typing, the edges, and the types the call builds from them. `signature` is the callee's
    /// signature where the call knows it (the same one the caller computed): the names are
    /// resolved against it, and its absence is the unknown-callee report. The types are not
    /// filled here: they follow the nodes, in that same order, where the call builds them once
    /// the deferred arguments have their nodes.
    void bindArguments(
        std::vector<Argument> &inputs, node_handle_t callee, const GCT::node_ptr_t &call,
        FunctionType *signature, bool isWith);
    /// Binds the named arguments of a call whose callee is several declarations, where no single
    /// signature answers for which parameter a name is (design principles 4.4). The parameter at a
    /// position carries the same name in every declaration of the set, so a name the call writes
    /// means one position there and the argument is placed at it; a name no declaration has, a
    /// name different declarations give to different positions, and a name at a position a
    /// declaration names differently are refused, as is a call that leaves a parameter of the set
    /// without a value (a default is written by one declaration, and the call does not say which
    /// one it means).
    void bindNamedArgumentsToDeclarations(
        std::vector<Argument> &inputs, const CalleeSet &set, const GCT::node_ptr_t &call,
        bool isWith);
    // Variables: the nodes bound by `var` (declarations, `var` parameters, the ports that capture
    // them). Such a node does not hold a value but the variable's cell (TupleType::cell), which
    // is what a `var` parameter or a capture receives; a value is read out of it by `:var/get`
    // and stored by the assignment operators. Every other named node is an immutable value
    // (`let`, a parameter without `var`).
    std::unordered_set<node_handle_t> variables_;

    /// A `var` that does not escape becomes SSA (docs/handover/reviews/questions-2026-10-24-evening.md,
    /// "var-version-node"): every read takes the value of the write that made it, and no read node is
    /// left reading the cell. A `var` that escapes -- captured by a closure, handed out as a `var`
    /// argument, or a `var` parameter of this graph -- is a resource and keeps its cell.
    /// `variableReadSource_` records, per read node, the writer it reads from (the cell itself when
    /// nothing has written yet) and the cell it belongs to; knownCellUsers_ holds the users of a
    /// cell this builder recorded (its reads and its stores), so a cell with any other user
    /// (:var/take, a call, a capture port) is left as a cell.
    struct VariableRead {
        node_handle_t source = nullptr;
        node_handle_t cell   = nullptr;
    };
    std::unordered_map<node_handle_t, VariableRead> variableReadSource_;
    std::unordered_set<node_handle_t> knownCellUsers_;
    std::unordered_set<node_handle_t> escapingVariables_;
    void collapseNonEscapingVariableReads();
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
    // The position of the whole expression `expr`: its own span and those of everything below
    // it. A folded expression's own origin covers only its last operand, so a value it computes
    // to that does not fit its type is reported at the whole expression (design principles
    // 5.1). `expr->origin()` when no span can be made.
    camel::source::origin_id_t expressionPositionOf(const GCT::node_ptr_t &expr) const;
    // Reports SemanticDiag::LiteralNotRepresentable at `expr` for a literal (or a folded
    // expression of literals) whose value `type` cannot hold, and aborts: the literal as it is
    // written, the exact value when it was folded, the type, and why.
    [[noreturn]] void rejectLiteralValue(
        const camel::compile::literal::Value &value, camel::core::type::Type *type,
        const GCT::node_ptr_t &expr);
    // The same, for a literal whose value is the data `data` (a written literal the GCT
    // converted or kept): reported at `where` and aborted. `written` is the expression the
    // literal was written as, when it is at hand (its source text is quoted); `writtenAt` is
    // the element's own position, for an element of a composite literal whose GCT entry kept
    // it (the text quoted is then the element as written).
    [[noreturn]] void rejectLiteralData(
        const camel::core::data::data_ptr_t &data, camel::core::type::Type *type,
        camel::source::origin_id_t where, const GCT::node_ptr_t &written = nullptr,
        camel::source::origin_id_t writtenAt = camel::source::kInvalidOriginId,
        const std::string &reason = {});

    // ---- The value of a body (design principles 5.1: a return position) ----
    // The last statement of a body is the value the body hands on, so it is a position with an
    // expected type: the body's declared type when it has one (`func f(): void! { if … }`), else
    // the type the place the body stands at asks for (a branch arm's `if`). A `return` written
    // in an arm of that statement takes the type from there, not from the function's declared
    //  return type.
    Type *ownDeclaredExitType() const;
    Type *tailPositionType() const;
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

    /// A branch arm: a region of the graph that has the branch (docs/design/graph-store 3.3),
    /// built where it is written. What the arm changes of the order of the variables (the last
    /// writer of each, the operations that used it since) is kept apart while the arm is built,
    /// and the branch takes it over once its JOIN is there (mergeArmVariables).
    struct ArmBuild {
        DraftGraphBuilder *graph = nullptr;
        uint32_t region          = 0;
        /// The first node the draft had room for when the arm began.
        draft_node_ref_t firstNode = 0;
        /// What the JOIN takes from the arm: its value, behind a GATE that waits for the
        /// arm's statements when it has any.
        node_handle_t output{};
        /// The value of the arm without that GATE.
        node_handle_t value{};
        /// The type of the value; void for an arm without one.
        camel::core::type::Type *type = nullptr;
        /// A `return` in the arm has given the value.
        bool returned = false;
        /// The variables the arm changed: the last writer and the uses they had when the arm
        /// began (the writer is empty for one without), and at the end of the arm what the
        /// arm made of them.
        struct Touched {
            std::optional<draft_node_ref_t> writerBefore;
            std::vector<VariableUse> usesBefore;
            std::optional<node_handle_t> writerAfter;
            std::vector<VariableUse> usesAfter;
        };
        std::unordered_map<node_handle_t, Touched> touched;
        std::vector<node_handle_t> touchedOrder; // in the order they were first changed
        /// The ports of the region (a loop's condition or body), in order: the element, then the
        /// values of the state.
        std::vector<node_handle_t> ports;
        /// The variables the arm stores into itself, and the functions it calls: what else it
        /// writes is what those write (capturedWritesOf), known once every body is built.
        std::set<node_handle_t> directWrites;
        std::vector<DraftGraphBuilder *> callees;
        camel::core::type::Type *exitType() const { return type; }
    };
    using arm_ptr_t = std::shared_ptr<ArmBuild>;
    /// The arms being built, outermost first.
    std::vector<ArmBuild *> armStack_;
    /// The `match type` arm being built: its cases are not regions (only one is selected),
    /// but a `return` written in the selected body still gives the value of the match, not of
    /// the function around it.
    struct StaticBodyScope {
        node_handle_t value{};
        bool returned = false;
    };
    StaticBodyScope *activeStaticBody_ = nullptr;
    /// The block a `static for` expands into. Its body is inline code at the `static for`
    /// position, so in a sync block it continues the chain instead of starting a plain block.
    const GCT::Node *syncedBlockOverride_ = nullptr;
    /// How many `match type` bodies are being built, to stop a pathological nesting early.
    size_t staticDepth_ = 0;
    /// The `n` of the `static for` bodies being built, innermost scope last. `n` is a
    /// compile-time string; it is not put in nodeScope_, so a body that only uses it as a key
    /// builds no node for it. For `indices`, the binding also carries the round's index as a
    /// value node: `j` is an ordinary `int`, so reading it as a value yields that node.
    struct StaticValueBinding {
        std::string name;
        std::string value;
        node_handle_t node{};
    };
    std::vector<std::vector<StaticValueBinding>> staticValueScopes_;
    /// The `Data` behind a static data node, when the builder made the node from one. A
    /// compile-time value handed on from node to node (a `constas` value passed to another
    /// `constas`, the `n` of a `static for`) has no `Data` of its own any more: it was encoded
    /// into a slot when its node was made, and the value is read back from here.
    std::unordered_map<node_handle_t, data_ptr_t> constantNodes_;
    void rememberConstant(node_handle_t node, const data_ptr_t &data) {
        if (node != nullptr && data != nullptr) {
            constantNodes_[node] = data;
        }
    }
    /// The constant a static data node (or a COPY of one) was made from, or null. A CAST that
    /// only retypes a constant -- `null` into a `T?` -- is the constant of the type it retypes it
    /// to, and carries it too.
    data_ptr_t constantOfNode(node_handle_t node) const;
    ///  The sequence a `static loop` walks when its head writes one out (`for i in <序列>`;
    ///  and the rule on which `in` forms count). One rule draws the line:
    /// the expression after `in` is a sequence the compiler works out when it is built only from
    /// compile-time constants -- literals, names bound to constants, the rounds of an enclosing
    /// `static` expansion -- joined by operations that are pure and cannot fail (the predicate
    /// design principles 3.4 names). Then it is evaluated and unrolled where it stands. The
    /// rounds are values (`a..b`, `range(a, b, step)`, an array literal or a constant array), or
    /// the elements of a compile-time tuple (`for x in ts`, where `ts` is the tuple of values the
    /// expansion was given, a rest parameter included): there each round binds the element
    /// `ts.[i]` itself, whose type is the type at that position -- which is exactly what tells a
    /// `static loop` from an ordinary `loop` (the ordinary one binds an element of one type every
    /// round).
    struct StaticSequence {
        /// The type every round's value has, when the rounds are compile-time constants.
        Type *elementType = nullptr;
        /// Each round's value, a compile-time constant. Empty when the rounds are elements of a
        /// tuple value.
        std::vector<data_ptr_t> values;
        /// Each round's element, for a compile-time tuple: the value at that position (`ts.[i]`),
        /// a node of the enclosing graph whose type is that position's type. Empty when the
        /// rounds are compile-time constants.
        std::vector<node_handle_t> elements;
        bool byElements() const { return !elements.empty(); }
        size_t rounds() const { return byElements() ? elements.size() : values.size(); }
    };
    /// The sequence of `for i in <序列>`, or nothing when the compiler cannot work it out. The
    /// operand is read, never built: a run-time expression leaves the number of rounds open, and
    /// the caller reports that (`SemanticDiag::StaticLoopSequenceNotKnown`). The two spellings
    /// the language refuses on their own -- the deleted two-argument `range(a, b)` and a `range`
    /// step of zero -- are reported here instead, where they are written.
    std::optional<StaticSequence> staticLoopSequenceOf(const GCT::node_ptr_t &subject);
    /// The same sequence where every round is an `int` value: what the build-time evaluator
    /// reads (`EvalSequenceResolver`), which has no room for a type or for another element type.
    std::optional<std::vector<int64_t>> staticLoopIntSequenceOf(const GCT::node_ptr_t &subject);
    /// The value of `expression` when the build-time evaluator works it out, else nothing: the
    /// evaluation the folding entry points use, with a refusal answered as "no value" rather than
    /// with a diagnostic, because the caller decides whether the place needs one.
    data_ptr_t constantValueOrNull(const GCT::node_ptr_t &expression);
    std::optional<std::string> staticValueNamed(const std::string &name) const {
        for (auto scope = staticValueScopes_.rbegin(); scope != staticValueScopes_.rend();
             ++scope) {
            for (auto binding = scope->rbegin(); binding != scope->rend(); ++binding) {
                if (binding->name == name) {
                    return binding->value;
                }
            }
        }
        return std::nullopt;
    }
    /// The node of the value bound to `name` by the nearest `indices` round, if that binding
    /// has one. Stops at the same binding as staticValueNamed, so a nearer `n` without a node
    /// hides an outer `j`.
    node_handle_t staticValueNodeNamed(const std::string &name) const {
        for (auto scope = staticValueScopes_.rbegin(); scope != staticValueScopes_.rend();
             ++scope) {
            for (auto binding = scope->rbegin(); binding != scope->rend(); ++binding) {
                if (binding->name == name) {
                    return binding->node;
                }
            }
        }
        return nullptr;
    }
    /// The arm whose code the builder is writing now, if it is in one: not a function or a
    /// lambda written inside it.
    ArmBuild *activeArm() const {
        return !armStack_.empty() && armStack_.back()->graph == currGraph_.get() ? armStack_.back()
                                                                              : nullptr;
    }
    /// Records the state of `variable` for the arm being built before it is changed.
    void noteVariable(node_handle_t variable);
    std::vector<VariableUse> &pendingUsesOf(node_handle_t variable) {
        noteVariable(variable);
        return pendingUses_[variable];
    }

    // Captured variables. A graph (function, lambda, loop part) that names a variable of an
    // enclosing graph, a module-level one included, holds the variable's cell in a capture
    // port. What the graph does with it is not in its type, so the builder keeps a summary per
    // graph and consults it wherever the graph is called directly: the call is then a use of
    // each captured variable, and a write of those the graph writes (design principles 3.5:
    // tracking of modifications is complete).
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
    // A write of captured variables outside a sync block, reported once every body
    // is built: while a recursive function's body is open its writes are only an upper bound.
    struct PendingIgnoredWrite {
        std::vector<DraftGraphBuilder *> graphs;
        std::vector<node_handle_t> variables;
        /// Variables known to be written (by the arms of a branch), besides what `graphs` write.
        std::set<node_handle_t> written;
        camel::source::origin_id_t origin;
        std::string subject;
        std::string verb;
    };
    // (ModuleUnit::pendingIgnoredWrites.)
    // Dropped effects (design principles 3.1): outside a sync block an effectful expression
    // whose result nothing uses is not connected to the graph's exit and does not run. That
    // is legal; the builder warns about it. Graphs whose exit depends on an effect, and the
    // graphs that are branch arms (reported through their branch, not as calls).
    std::unordered_set<DraftGraphBuilder *> effectfulGraphs_;
    // The function bodies and arms of a module, in the order they were built, are in
    // ModuleUnit::builtBodies.
    node_handle_t lastSyncedNode_{};
    size_t syntheticRefIndex_ = 0;

    // ---- Generic functions (design principles 5.5) ----
    //
    // A function whose signature declares its type parameters at its head is a subgraph template:
    // no graph is built for its declaration. Each direct call binds the variables from the
    // argument types and asks for the instance of those types, which is built once from the
    // function's GCT with the variables replaced (typeBindings_) wherever the body names a
    // type, and then called like any other function. So the body is typed and its overloads
    // are chosen at instantiation, and only concrete types reach a graph.
    //
    // A `constas` parameter binds a compile-time value the same way (design principles 5.4):
    // the value is given at the call, is part of the instance key, and is read by the body as
    // a static value. It is not a runtime argument, so the instance has no port for it.
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
    // The `constas` values of the instance being built; empty outside one.
    camel::core::type::ExtentValues extentBindings_;
    // The `constas` values of the instance being built, by parameter name; empty outside one.
    ConstasValues constasBindings_;
    size_t instantiationDepth_ = 0;
    // Set while the callee of a call is visited: the one place the name of a generic function
    // may stand. The DREF nodes that name one, and the template each names.
    bool genericCalleeAllowed_ = false;
    // Set while the callee of a call is visited, when that callee is a `WITH`: the arguments
    // bound there are arguments of this call, so one whose parameter type is only known after
    // the ordinary arguments are typed can be built at the call instead (design principles 5.9:
    // a lambda's parameter types come from the parameter it is passed to, and `reduce<f, 0>`
    // binds the element type from the array it reduces).
    bool withArgsOfCall_ = false;
    // The type of the position the expression being built stands at: the value the expression
    // produces must be of this type (design principles 5.1). Every syntax position that knows
    // the type passes it down with an ExpectedValueScope or visitInPosition; checkValue applies
    // it to the finished value.
    Type *expectedValueType_   = nullptr;
    struct ExpectedValueScope {
        Type *&current;
        Type *outer;
        ExpectedValueScope(Type *&slot, Type *expected)
            : current(slot), outer(std::exchange(slot, expected)) {}
        ~ExpectedValueScope() { current = outer; }
    };
    std::unordered_map<node_handle_t, camel::core::type::type_vec_t> explicitOperatorTypes_;
    std::unordered_map<node_handle_t, generic_ref_t> genericDrefs_;
    // The templates of functions written in place (anonymousTemplateOf): nothing else owns
    // them.
    std::vector<generic_template_ptr_t> anonymousTemplates_;

    // A type read from the GCT, with the type variables and the named extents of the current
    // instance replaced.
    Type *boundType(Type *type) const {
        Type *substituted = camel::core::type::substituteTypeVars(type, typeBindings_);
        return camel::core::type::substituteExtentValues(substituted, extentBindings_);
    }
    // Whether a declared function type is still generic under the current bindings.
    bool isGenericHere(Type *type) const {
        // A rest function is always a template: its arity is decided at each call, so no
        // graph is built for the declaration itself (design principles 5.5). A declaration
        // with a `constas` parameter is one too: its instance is fixed by the value at the
        // call, which is not a runtime argument (design principles 5.4).
        const bool rest =
            type != nullptr && type->code() == camel::core::type::TypeCode::Function &&
            static_cast<FunctionType *>(type)->hasRest();
        const bool constas =
            type != nullptr && type->code() == camel::core::type::TypeCode::Function &&
            static_cast<FunctionType *>(type)->hasConstas();
        return rest || constas || (camel::core::type::containsTypeVar(type) &&
                                   camel::core::type::containsTypeVar(boundType(type)));
    }
    // Whether the declaration `node` is a template: its head may declare type parameters that
    // its signature never writes (a predicate such as `is_plain::<T>()` matched in its body),
    // and those are bound by an explicit `::<...>` at the call all the same.
    bool isGenericHere(Type *type, const GCT::node_ptr_t &node) const {
        if (node != nullptr &&
            camel::compile::gct::Tree::of(node)->headTypeParamsOf(node) != nullptr) {
            return true;
        }
        return isGenericHere(type);
    }
    // The type parameters of a template in declaration order: the head names first, then the
    // variables its signature writes that the head did not declare.
    std::vector<std::string> typeParameterNames(const generic_template_ptr_t &tmpl) const;
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
        const generic_template_ptr_t &tmpl, std::vector<Argument> &withInputs,
        std::vector<Argument> &normInputs, camel::core::type::type_vec_t &withTypes,
        camel::core::type::type_vec_t &normTypes, const GCT::node_ptr_t &call,
        Type *expectedResult = nullptr);
    /// The instance of one chosen template; `resolveGenericCall` is this for the single template
    /// a name holds, and the call site is this for each candidate of a name with several.
    compile_graph_ptr_t instantiateForCall(
        const generic_template_ptr_t &tmpl, std::vector<Argument> &withInputs,
        std::vector<Argument> &normInputs, camel::core::type::type_vec_t &withTypes,
        camel::core::type::type_vec_t &normTypes, const GCT::node_ptr_t &call,
        Type *expectedResult = nullptr);
    /// Reads the values of the `constas` parameters of `signature` from the ordinary arguments
    /// of the call (each written at its parameter's position), removes them from `normInputs`
    /// (and `normTypes`, when given), and records the integer ones as the extent bindings they
    /// stand for. `normTypes` may be null (a rest call has only the argument nodes).
    void takeConstasValues(
        FunctionType *signature, std::vector<Argument> &normInputs,
        camel::core::type::type_vec_t *normTypes, const GCT::node_ptr_t &call,
        ConstasValues &constas, camel::core::type::ExtentValues &extents);
    /// A function written in place whose type still holds a type variable (the element
    /// functions of a list comprehension): a template without a name, registered where it
    /// stands. Its marker node is what the name of a generic function gives.
    node_handle_t anonymousTemplateOf(const GCT::node_ptr_t &func);
    // ---- A generic function where a function is expected (design principles 5.5) ----
    /// The instance of `tmpl` that is a function of type `expected` (whose return type may
    /// still be open), or nullptr with the reason in `why`: the parameter types of `expected`
    /// fix the type variables, and the instance must then have exactly those parameters.
    compile_graph_ptr_t instantiateGenericAs(
        const generic_template_ptr_t &tmpl, FunctionType *expected, const GCT::node_ptr_t &origin,
        std::string &why);
    // ---- A generic rule where a rule is expected (rewrite design 2 (second version) 8.5) ----
    /// `__rule_generic('name', two_way, needs, consts, <the sides>)`, the maker of a rule whose
    /// head declares type parameters: its sides are generic functions, and a generic function has
    /// no value to give. The maker takes their *templates* instead, which are registered under an
    /// id the rule value keeps (camel/compile/gir/rule_templates.h); the call becomes
    /// `__rule_generic('name', two_way, needs, consts, id)` and resolves as the `Rule` it is. Does
    /// nothing when the call is not that shape, and the maker's own resolution reports it.
    void settleGenericRuleMaker(
        std::vector<Argument> &normInputs, camel::core::type::type_vec_t &normTypes,
        const GCT::node_ptr_t &call);
    /// The instance of one registered generic rule at `types`: declared public (above), since the
    /// context's table of generic rules calls it.
    /// A body build inside a speculative scope: while this lives, what the body reports goes to the
    /// scope's sink. The body of a candidate is the only thing a search may hold back, and this is
    /// the only door into that sink.
    class BodyDiagnostics {
      public:
        explicit BodyDiagnostics(Builder &builder);
        ~BodyDiagnostics();

      private:
        Builder &self_;
        SpeculativeInstantiation *scope_;
        camel::core::error::diagnostics_ptr_t outer_;
    };
    /// The diagnostics of a body that does not compile for the types it was tried with, and the
    /// answer to them. A position that fixes the type variables of a generic name tries the
    /// candidates its name offers one after another, and a candidate whose *body* does not compile
    /// for those types is not the one the position wants: what its body said is about a function the
    /// program does not use, so it is held here instead of reported, and the attempt is answered
    /// with "no instance" (`instantiateGeneric` returns nullptr) rather than with an abort. The
    /// attempt that failed remembers the failure in its template (`GenericInstance::failed`,
    /// :77-78), so a second attempt at the same types makes no diagnostic and does not build the
    /// body again.
    ///
    /// Nothing else is held. The argument form of a call, a `constas` value that is not constant, a
    /// condition that cannot be answered, an incomplete return type and every other verdict of the
    /// program reach the sink in place the moment they are made, whether a scope is open or not: a
    /// search may skip a candidate, never a verdict. What is held is only ever the body of a
    /// candidate that may still turn out not to be the one wanted.
    ///
    /// A caller with no candidate left calls `attach()`, which adds those errors to the sink in
    /// place -- so the refusal that follows carries them, at the positions and with the labels the
    /// body gave them -- and ends the scope; a caller that found its instance lets the scope end,
    /// which drops them. The search that has no fallback does not use this at all: one candidate and
    /// no alternative is not speculative, and its diagnostics are the program's.
    class SpeculativeInstantiation {
      public:
        friend class BodyDiagnostics;
        explicit SpeculativeInstantiation(Builder &builder);
        ~SpeculativeInstantiation();
        /// Adds the errors of the bodies that did not compile to the sink in place, and ends the
        /// scope: from here the builder reports where it always did, so the diagnostic the caller
        /// is about to make is the program's, and it is preceded by what the bodies said.
        void attach();
        /// Ends the scope and reports nothing more: what the bodies said is dropped, for a caller
        /// that knows a candidate nobody chose is not the program's business (a rule's search for an
        /// instance for one type: the reason is remembered and reported only when the user asks).
        void close();
        /// What the bodies held back said, as one line: the first error's message, or empty when
        /// they said nothing (a search the caller skipped for its own reason, not the body's).
        /// Read before the scope ends, which is when the sink is dropped; a caller that remembers
        /// the attempt as one that did not come about keeps this as its reason, and `why_not` is
        /// where a reader gets it (nothing is reported where the body said it).
        std::string reason() const;

      private:
        Builder &self_;
        /// The scope this one replaced, so that scopes nest.
        SpeculativeInstantiation *previous_ = nullptr;
        camel::core::error::diagnostics_ptr_t outer_;
        camel::core::error::diagnostics_ptr_t inner_;
        bool settled_ = false;
    };
    /// The function value of the generic function `node` names, instantiated for the type
    /// the position expects; an error (GenericFunctionAsValue) that says which variables
    /// cannot be determined when `expected` is not a function type that fixes them.
    node_handle_t genericValueFor(
        node_handle_t node, camel::core::type::Type *expected, const GCT::node_ptr_t &origin);
    [[noreturn]] void rejectGenericValue(
        const generic_template_ptr_t &tmpl, const GCT::node_ptr_t &origin, const std::string &why);
    /// The generic functions the name of a generic DREF stands for: the single template, or the
    /// group of several overloads of one name.
    std::vector<generic_template_ptr_t> genericOverloadsOf(node_handle_t node) const;
    /// The one of them that a call with these arguments takes, or nullptr when none or several
    /// do. `normInputs` and `normTypes` are read only; the arguments are not changed.
    generic_template_ptr_t chooseGenericOverload(
        const std::vector<generic_template_ptr_t> &candidates, const std::vector<Argument> &withInputs,
        const std::vector<Argument> &normInputs, const camel::core::type::type_vec_t &withTypes,
        const camel::core::type::type_vec_t &normTypes, const GCT::node_ptr_t &call);
    /// An argument of a built-in operator that names a generic function. An operator's type
    /// rule is C++ code that needs the argument types, so it cannot say which function it
    /// expects; the instances the types of the other arguments suggest are tried (their
    /// element types first) and the one the operator accepts is taken. Replaces the name in
    /// `inputs` / `types`; an error when no instance is accepted.
    void settleGenericOperatorArguments(
        const oper_group_ptr_t &ops, std::vector<Argument> &withInputs,
        std::vector<Argument> &normInputs, camel::core::type::type_vec_t &withTypes,
        camel::core::type::type_vec_t &normTypes, const GCT::node_ptr_t &call);
    //  ---- Build-time tuples. ----
    // A tuple that is known while the graph is built: its type and its element nodes, without a
    // FILL node. A rest parameter's `ts` and a tuple comprehension are the two producers.
    // `indices` reads only the type, `ts.[j]` selects an element, and `..ts` splices the
    // elements; none of those builds a node for the tuple. A runtime use either materializes one
    // FILL (a comprehension is a value expression) or is refused (`ts` is not a value).
    struct StaticTuple {
        Type *type = nullptr; // TupleType of `elements`
        std::vector<node_handle_t> elements;
        /// A comprehension is a value and materializes when one is needed; a rest parameter's
        /// tuple is not a value and its escape is StaticTupleEscapes.
        bool materializable = true;
        /// The name the tuple was written with (`ts`), for the escape diagnostic.
        std::string name;
        /// Where the tuple was written, for the escape diagnostic and the materialized FILL.
        GCT::node_ptr_t origin;
    };
    //  ---- Rest parameters (design principles 5.5, step 6) ----
    // A rest function has no runtime arity, so it is never an instance: each direct call
    // expands its body into the caller's graph, with the fixed parameters bound to the argument
    // nodes and the rest parameter bound to the nodes the call's arguments leave over. The
    // expansion is built here, once per call; there is no call node and no tuple fill.
    struct RestBinding {
        std::string name;
        StaticTuple tuple; // the build-time tuple `ts`; not a value
    };
    struct RestBody {
        Type *exitType            = nullptr;
        node_handle_t value       = nullptr;
        bool returned             = false;
    };
    /// The rest parameters in force while an expanded body is built (innermost last).
    std::vector<RestBinding> restBindings_;
    /// The expanded body whose `return` is being built, if any.
    RestBody *activeRestBody_ = nullptr;
    /// The rest binding of `name` that no local of the same name shadows.
    const RestBinding *restBindingNamed(const std::string &name) const;
    /// The FILL of a build-time tuple, built only when the tuple is used as a runtime value.
    node_handle_t materializeStaticTuple(
        const StaticTuple &tuple, const GCT::node_ptr_t &at);
    /// The nodes `..operand` stands for: the elements of a compile-time tuple, or the
    /// elements of the rest parameter it names.
    std::vector<node_handle_t> expandRestArgument(
        const GCT::node_ptr_t &operand, const std::string &written,
        const GCT::node_ptr_t &call);
    /// Expands `tmpl`, a rest function, at `call`: binds its type variables and fixed
    /// parameters, checks the arguments, and builds its body in the caller's graph.
    node_handle_t inlineRestCall(
        const generic_template_ptr_t &tmpl, std::vector<Argument> &withInputs,
        std::vector<Argument> &normInputs, const GCT::node_ptr_t &call,
        Type *expectedResult);
    /// The leaves of a tree as one flat tuple: the one tree intrinsic still expanded here, because
    /// the result type is a concatenation of the leaves of every field (see tree.cpp).
    node_handle_t expandTreeCall(
        const oper_group_ptr_t &ops, const std::vector<Argument> &with,
        const std::vector<Argument> &norm, const GCT::node_ptr_t &call);
    compile_graph_ptr_t instantiateGeneric(
        const generic_template_ptr_t &tmpl, const camel::core::type::type_bindings_t &bindings,
        const camel::core::type::ExtentValues &extents, const ConstasValues &constas,
        const GCT::node_ptr_t &call);
    // Enters the scope of `graph`, an existing graph, as enterScope does for one it finds.
    void enterGraphScope(const compile_graph_ptr_t &graph, const std::string &name);
    // Builds the body of the function `gct` into `graph`, whose scope has been entered.
    // ---- Tags (design principles 6.2) -------------------------------------------------------
    // A `#` use written on a function, a type declaration, a field or a block adds facts
    // (camel/core/node_attrs.h): to the declaration it is written on (`on func`, `on type`,
    // `on field`) or to the nodes written below it (`on nodes`: the body of the function, the
    // statements of the block). The uses are on the GCT node (GCT::Tree::tagUsesOf), put there
    // by the GCT builder; they are resolved here, where imports and the module are known.
    struct TagFacts {
        camel::core::attr::AttrSetId declaration = camel::core::attr::kNoAttrs;
        camel::core::attr::AttrSetId nodes       = camel::core::attr::kNoAttrs;
        /// The numeric permissions the permission tags written here grant (design principles
        /// 7.2: a permission is a tag). They go to the node's permission column, never to the
        /// attribute set: one fact, one place.
        camel::core::sem::PermissionMask permissions = 0;
    };
    /// The facts of the tag uses written on `gct`, which is written at `at` (Func, Type or
    /// Nodes). Resolved once per node and remembered. Reports a use that is wrong and throws
    /// BuildAbortException.
    TagFacts tagFactsOf(const GCT::node_ptr_t &gct, camel::core::tag::Position at);
    /// The facts of a list of uses (the tags of a block, of a field), each written at `at`.
    TagFacts tagFactsOfList(
        const std::vector<GCT::TagUse> &uses, camel::core::tag::Position at);
    /// Adds the facts of one use, expanding an alias group to its members.
    void addTagFacts(
        const GCT::TagUse &use, camel::core::tag::Position at, TagFacts &facts,
        std::unordered_set<camel::core::tag::TagId> &seen);
    /// The tag the written name names here: declared by this module, then imported by name or
    /// through a module prefix. nullopt when there is none.
    std::optional<camel::core::tag::TagId> tagNamed(const std::string &written);
    /// The attribute value of the arguments of one use of `decl`; reports an argument that is
    /// not a compile-time value or that the tag cannot carry.
    camel::core::attr::AttrValueId tagValueOf(
        const camel::core::tag::Decl &decl, const GCT::TagUse &use);
    /// The `#impl<f>` use written on `gct`, when it has one: the kernel's name as written and
    /// where the tag is. The tag is a declaration fact (`on func`); a use anywhere else is
    /// reported by tagFactsOf like any other misplaced tag.
    struct ImplTag {
        std::string kernel;
        camel::source::origin_id_t origin = camel::source::kInvalidOriginId;
    };
    std::optional<ImplTag> implTagOf(const GCT::node_ptr_t &gct);
    /// Resolves the `#impl` tags of the module whose declarations were just read (step one):
    /// checks each kernel and records the body graph it stands for, so a direct call of the
    /// function is a call of the kernel (visitDRefNode) while the graph stays the semantic body
    /// (the expansion a consumer uses).
    void resolveImplDeclarations(const GCT::node_ptr_t &root);
    /// Whether the body-less declaration `kernel` is the same function as `function`, for the
    /// non-generic case where one signature answers for all calls (design principles 10.4).
    static bool sameImplSignature(FunctionType *function, FunctionType *kernel);
    /// Adopts the executor of the instance `graph` of the generic `#impl` function `tmpl`: the
    /// rule of the kernel's declaration that accepts `funcType`, if any. Chosen here, at
    ///  instantiation, because that is when the instance's types are known (rule
    ///  impl-kernel-site). An instance no rule accepts has no executor: it runs the body,
    /// which is the semantics, and gives the same result.
    void adoptImplKernel(
        const generic_template_ptr_t &tmpl, const compile_graph_ptr_t &graph, FunctionType *funcType);
    /// Whether the rule `rule` of a body-less declaration applies to an instance of these types:
    /// the condition the declaration wrote, answered for the instance's types (design principles
    /// 4.4), with the declaration's own type parameters bound by them. True when the rule has no
    /// condition. A condition that cannot be answered from the types alone is reported as the
    /// defect it is (reportWhenProblem).
    bool declaredRuleApplies(
        const camel::core::type::resolver_ptr_t &rule, const std::string &name,
        const camel::core::type::type_vec_t &withTypes,
        const camel::core::type::type_vec_t &normTypes);
    /// What a body-less declaration's `when` says about a call whose arguments have these types
    /// (design principles 4.4). `declaredRuleApplies` answers the same question about an `#impl`
    /// instance, where only "this rule is the executor" and "it is not" exist; selecting among a
    /// call's candidates needs the third answer, because a rule whose condition is false is not
    /// one of the call's overloads at all (the call goes on to the other candidates, and a call
    /// no candidate takes reports the conditions as unmet). A rule with no condition always
    /// applies, and `conditionText`, when asked for, receives the condition as it was written.
    enum class RuleVerdict {
        Applies,        // no condition, or the condition holds for these arguments
        ConditionUnmet, // the signature takes the arguments, the condition does not
        NotThisRule,    // the declaration's own type parameters do not bind to these arguments
    };
    RuleVerdict ruleVerdictForCall(
        const camel::core::type::resolver_ptr_t &rule, const std::string &name,
        const camel::core::type::type_vec_t &withTypes,
        const camel::core::type::type_vec_t &normTypes, std::string *conditionText = nullptr);
    /// The rule of the operator group `ops` a call means: a rule whose signature takes the
    /// arguments and whose own `when` holds for them (design principles 4.4). A rule that takes
    /// the arguments while its condition is false is not a candidate; it is recorded in `unmet`
    /// with the condition it wrote, so that a call no rule accepts reports those conditions
    /// (WhenConditionUnmet) rather than settling on one of them. `normStatics` are the
    /// compile-time values of constant arguments, as for `OperatorGroup::resolveForCall`.
    ///
    /// Two candidates that both write `when` and both hold for the arguments are an ambiguity,
    /// not a choice: the order two declarations of one name are written in decides nothing (the
    ///  language rule on `two-when-conditions-hold`,
    /// docs/handover/reviews/questions-2026-10-23.md), and the declaration-time check asks about
    /// one type at a time, so the call site is the first place that can see the pair. Every such
    /// candidate is recorded in `ambiguous` as `signature — when condition`, sorted and without
    /// repeats, so that the report is the same whichever order the declarations are written in;
    /// the caller reports it (AmbiguousCall, which names the call and lists them). A candidate
    /// that writes no condition is not part of that list: which overload without `when` the group
    /// answers with today is a separate question, left as it is.
    ///
    /// The returned index is the first candidate in the group's own order — the fixed signatures
    /// first, then the order the rules were written in. It is what a call with fewer than two
    /// candidates that write `when` means; a call with more is reported through `ambiguous` and
    /// the index is not used.
    std::optional<oper_idx_ptr_t> resolveGroupedCall(
        const oper_group_ptr_t &ops, const camel::core::type::type_vec_t &explicitTypes,
        Type *expectedResult, const camel::core::type::type_vec_t &withTypes,
        const camel::core::type::type_vec_t &normTypes,
        camel::core::type::static_args_t normStatics,
        std::vector<std::pair<std::string, std::string>> &unmet,
        std::vector<std::string> *ambiguous = nullptr);
    /// The kernel of `graphs` when they are exactly the one `#impl` body: what a call of the
    /// function is, instead of a call of the body (design principles 10.4).
    std::optional<ImplKernel> implKernelOf(const graph_set_ptr_t &graphs) const;
    /// The kernel of the one graph `graph`, when it is an `#impl` body an executor was found
    /// for (a generic function's instance, adoptImplKernel).
    std::optional<ImplKernel> implKernelOfGraph(const compile_graph_ptr_t &graph) const;
    /// Whether the graph being built is an `#impl` body or nested in one (a lambda in the
    /// body). A call written there stays a call of the function's semantic graph, so that the
    /// body the expansion reads is the body without the tag; the kernel is for callers.
    bool insideImplBody() const;
    /// The tags written on the fields of the type declaration `decl` (its GCT node), recorded
    /// in the module with the declaration.
    void recordFieldTags(const GCT::node_ptr_t &decl);
    std::unordered_map<const GCT::Node *, TagFacts> tagFacts_;
    /// The graph a `func`-valued tag argument names (design principles 6.2): a function of this
    /// block or module, a decorated function, or one an import brings. nullopt when the name
    /// does not name exactly one function (overloads, an operator group, a value, a name that is
    /// not declared).
    std::optional<compile_graph_ptr_t> functionGraphNamed(const std::string &written);
    /// Makes the graphs the declaration facts `set` name (a tag with a `func` parameter) rule
    /// references of `graph` (kGraphRefRule): the tag's fact is a graph reference, and the graph
    /// must be part of the program so that the tag's reader finds it in the frozen graph
    /// (GCGraph::ruleGraphs) without looking the name up again.
    void attachRuleGraphs(const compile_graph_ptr_t &graph, camel::core::attr::AttrSetId set);
    /// The graphs named by `func`-valued tag arguments, by GraphId: attachRuleGraphs needs the
    /// graph, and the attribute value only keeps the id.
    std::unordered_map<uint32_t, compile_graph_ptr_t> tagFunctionGraphs_;

    void buildFuncBody(
        const GCT::node_ptr_t &gct, const compile_graph_ptr_t &graph, FunctionType *funcType,
        const std::string &name, const TagFacts &bodyTags);

    /// Evaluates a declaration's `when` condition at one instantiation. `signature` is the
    /// declaration's signature with the instance's types bound, so the `with` parameter names
    /// are known; `bindings` substitutes the declaration's type variables in the condition's
    /// type arguments. `withValues` holds the call's `with` arguments that are compile-time
    /// constants, by parameter name. The condition is a bool expression over the compiler's
    /// type queries (`is_numeric`, `is_integer`, `is_float`, `is_ordered`, `is_function`,
    /// `is_string`, `is_bool`), `!`, `&&`, `||` and parentheses.
    /// A condition belongs to the declaration that writes it, and is answered in that module
    /// even when another one selects the declaration: `declaring` is the generic template, and
    /// `contextUnit` the module of a function that is not generic. Names are resolved there,
    /// as a call in the declaration's body would be.
    WhenResult evaluateWhenCondition(
        const GCT::node_ptr_t &condition, FunctionType *signature,
        const camel::core::type::type_bindings_t &bindings,
        const std::unordered_map<std::string, data_ptr_t> &withValues,
        const generic_template_ptr_t &declaring = nullptr, const ModuleUnit *contextUnit = nullptr);
    /// Switches the builder to the module a condition is written in for as long as it lives:
    /// the search for a predicate is the declaration's, wherever the selection happens.
    class ConditionContext;
    /// The template a condition's call names where the declaration is written: the module's
    /// own generic functions first, then those its imports bring by name or through a module
    /// prefix, then the prelude. `problem` is set when the name exists but cannot be called at
    /// compile time (imported and not a generic function, or two imports that both bring it).
    /// nullopt with `problem` still NotEvaluable when the name is not found by those layers.
    std::optional<generic_template_ptr_t> whenPredicateNamed(
        const std::string &written, const GCT::node_ptr_t &at, WhenResult &problem);
    /// Whether two selective imports bring a predicate of the name, in which case `problem`
    /// says so and neither is chosen.
    bool whenPredicateAmbiguous(const std::string &written, WhenResult &problem);
    /// The module that declares a predicate called `written` when this module does not import
    /// it, for the "not imported" report; empty when no loaded module declares it.
    std::string whenPredicateElsewhere(const std::string &written) const;
    /// The module of a top-level function or of an instance of one, by its graph.
    const ModuleUnit *unitOfFunction(const compile_graph_ptr_t &graph) const;
    /// Reports why a condition is not applicable at `origin`, for the declaration named `name`.
    /// Never returns: every outcome except Holds is a diagnostic.
    [[noreturn]] void reportWhenProblem(
        const WhenResult &result, const std::string &name, const GCT::node_ptr_t &origin);
    /// The graphs whose declaration has a `when` condition, with its source text.
    std::unordered_map<const DraftGraphBuilder *, GCT::Tree::WhenClause> whenClauses_;

    // ---- A call of a pure function with constant arguments is that constant ----
    // The same evaluator a `when` condition uses reads the call's body; a call in a place that
    // needs a compile-time constant is a diagnostic when it does not reduce to one.

    /// Why a call in a constant position did not become one. `NotAProgramCall` is not a
    /// refusal: the call is not one this evaluation knows (a function value, an operator, a
    /// name with several candidates), so the ordinary build decides and its diagnostics apply.
    enum class FoldRefusal {
        None,
        NotAProgramCall,
        ArgumentNotConstant,
        HasEffects,
        TooDeep,
        ResultNotConstantKind,
        NotEvaluable,
    };
    struct FoldedCall {
        FoldRefusal refusal = FoldRefusal::NotAProgramCall;
        data_ptr_t data;
        /// The call as written (`first_field`), for a diagnostic.
        std::string call;
        std::string reason;
    };
    /// How many nested calls a build-time evaluation follows before it says the call is not a
    /// constant here (`FoldCallTooDeep`). A recursion that reaches its base case in fewer steps
    /// folds; one that does not is reported where a constant is required.
    static constexpr size_t kMaxFoldedCallDepth = 64;
    /// Evaluates `call` as a constant when it is a call of a pure function whose arguments are
    /// constants.
    FoldedCall foldConstantCall(const GCT::node_ptr_t &call);
    /// The evaluation of one call: resolves the callee, checks its purity, and runs its body
    /// through the evaluator `when` conditions use. `arguments` are the ordinary arguments,
    /// already evaluated; `evalArgument` evaluates a `with` argument expression. `buildFirst`
    /// builds the callee's body (and the generic instance) before reading it, so a body that
    /// does not build keeps its own diagnostics instead of disappearing into a folded value.
    FoldedCall evaluateCallAtBuildTime(
        const GCT::node_ptr_t &call, GCT::node_ptr_t callee,
        const std::vector<data_ptr_t> &arguments,
        const std::function<data_ptr_t(const GCT::node_ptr_t &)> &evalArgument,
        const camel::core::type::type_bindings_t &vars, size_t depth, bool buildFirst);
    /// evaluateCallAtBuildTime for a call inside a body being evaluated: a refusal becomes an
    /// exception carrying the matching problem.
    data_ptr_t evaluateCallInsideBody(
        const GCT::node_ptr_t &call, const GCT::node_ptr_t &callee,
        const std::vector<data_ptr_t> &arguments,
        const std::function<data_ptr_t(const GCT::node_ptr_t &)> &evalArgument,
        const camel::core::type::type_bindings_t &vars, size_t depth);
    /// The value of an expression the compiler can compute at build time. Throws when it cannot
    /// (the evaluator's failure type is internal to builder_when.cpp).
    data_ptr_t compileTimeExpression(const GCT::node_ptr_t &expression);
    /// Reports a refusal at `origin`, naming `folded.call`. Never returns.
    [[noreturn]] void reportFoldRefusal(const FoldedCall &folded, const GCT::node_ptr_t &origin);

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
                    auto *graph = DraftGraphBuilder::fromDraft(value.draft);
                    os << (graph ? graph->name() : std::string{"<detached>"}) << "#" << value.id;
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
    // A second function of this name with the same parameter types in the same block, or an
    // ordinary declaration under a generic function of the name (`plain`): reports
    // Redeclaration at `gct` and abandons the declaration.
    void rejectRedeclaredFunction(
        const std::string &name, FunctionType *funcType, const GCT::node_ptr_t &gct,
        bool plain = false);
    // The declared types of the functions rejected so (the forward declaration reports, the
    // definition that follows is skipped quietly).
    std::unordered_set<FunctionType *> redeclaredFuncTypes_;
    void leaveScope();

    // Reports a name imported from two modules with the same signature (ImportNameConflict).
    void reportImportNameConflicts();
    // A use of the function `graph` in the current graph: its function value
    // (`callableAsResult`), or a direct call of it (`allowParameterization`; a FUNC node, or a
    // CALL of its value when its value was made before).
    node_handle_t createFuncDataNode(
        const compile_graph_ptr_t &graph, bool callableAsResult, bool allowParameterization);
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
    /// The tags written on field `index` of the struct type `subject`, as a compile-time
    /// `string[]`: the third thing the element of `fields T` gives, bound to the third name of
    /// `(n, F, tags)`. Empty for a field with no tags, for a type that is not a named struct and
    /// for a type declared by another module (the facts of a field live in the module that
    /// declares the type, and reading a tag's value from Camel code waits for tag reflection --
    ///  design principles 6.2, which puts `(n, F, tags)` with it).
    node_handle_t staticFieldTagsNode(
        Type *subject, size_t index, const GCT::node_ptr_t &where);
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
    /// `f<w>` used as a value (design principles 4.4): the function that calls `f` with the
    /// `with` arguments bound to `target` and takes the rest, as its function value. It is a
    /// closure: a graph of its own that captures the bound values, which enter it through
    /// ports like any captured name; no node kind is added. `expected`, when given, is the
    /// function type the position asks for and selects among overloads. Returns nullptr,
    /// leaving the binding in place, when `target` does not name one function for these
    /// arguments.
    node_handle_t partialApplicationOf(
        node_handle_t target, const GCT::node_ptr_t &origin,
        camel::core::type::FunctionType *expected = nullptr);
    /// `a op= b` is `a = a op b` (design principles 4.2): the new value is computed and the
    /// variable bound to it. One rule for every type, whoever declared it: `a op b` must be
    /// defined for these operands and must give `a`'s own type, and the rebinding applies only
    /// then. `group` is the operator group of the compound assignment and `operands` its two
    /// operands, already built; `normTypes` their types, the ones the plain operator is
    /// resolved with. Returns nullptr when the form does not apply, so that the call reports
    /// the compound assignment it wrote.
    node_handle_t compoundAssignmentByRebinding(
        const std::string &group, const GCT::node_ptr_t &gct,
        const std::vector<Argument> &operands, const camel::core::type::type_vec_t &normTypes);
    /// Whether the plain operator `group` has an overload for the types of `operands` (design
    /// principles 4.3, 5.7): a built-in or a module's own rule is asked by type, one written in
    /// the program is matched where the operator is resolved. Asked before an operator DREF
    /// exists, for `+=`, which follows the plain operator
    /// (`compoundAssignmentByRebinding`).
    bool plainOperatorAvailableFor(
        const std::string &group, const std::vector<Argument> &operands,
        const camel::core::type::type_vec_t &normTypes);
    /// An error at `call` when the conversion `group` (`int`, `i32`, `float`, ...) is given
    /// a literal that does not fit the type (LiteralNotRepresentable).
    void rejectLiteralConversion(
        const std::string &group, FunctionType *funcType, const std::vector<Argument> &operands,
        const GCT::node_ptr_t &call);
    /// An error at `origin` when `node` still carries `with` arguments no partial
    /// application could be made of (PartialApplicationRefused): the binding is never
    /// dropped without a word.
    void rejectUnappliedBinding(node_handle_t node, const GCT::node_ptr_t &origin);
    /// An error at `origin` when a call has several candidates in the pass it is making:
    /// the exact matches when there are any, otherwise the functions a literal enters
    ///  (design principles 5.1). An ordinary call, an operator call and a partial
    /// application used as a value all report through this one, so they cannot drift.
    /// Declaration order never settles the choice; the candidates are listed and the help
    /// says how to choose one. Throws BuildAbortException.
    void rejectAmbiguousCall(
        const std::string &name, const std::string &argumentTypes,
        const std::vector<std::string> &candidates, const GCT::node_ptr_t &origin);
    /// The same, located at an origin that is already the whole expression (a partial
    /// application's `f<w>` rather than the `<w>` its node spans).
    void rejectAmbiguousCallAt(
        const std::string &name, const std::string &argumentTypes,
        const std::vector<std::string> &candidates, camel::source::origin_id_t origin);
    /// The same for candidates that are function types: their signatures are listed in the
    /// order they were written, which is all an ordinary call and a partial application
    /// have to show.
    void rejectAmbiguousCall(
        const std::string &name, const std::string &argumentTypes,
        const std::vector<camel::core::type::FunctionType *> &candidates,
        const GCT::node_ptr_t &origin);
    // The functions named `name` when `decorated` is one of several of that name (the set a
    // call chooses from); null when the decorated function is the only one.
    graph_set_ptr_t
    overloadsOfDecorated(const std::string &name, const compile_graph_ptr_t &decorated);
    // The function a decorated graph produces when its decorators run.
    node_handle_t lowerDecoratedGraphValue(
        const compile_graph_ptr_t &decoratedGraph, const GCT::node_ptr_t &origin);
    std::optional<node_handle_t> modifierOf(node_handle_t node) const;
    void setModifier(node_handle_t input, node_handle_t modifier);
    // An operation writes `variable`: later reads and writers are ordered after it. The write is
    // complete at `exit` (one node for a call; the JOIN of a branch). `throughCallee`: the write
    // is one a called function or a branch makes to a variable, which the summaries already
    // account for.
    void recordWrite(node_handle_t variable, node_handle_t exit, bool throughCallee);
    void recordWrite(node_handle_t variable, node_handle_t writer) {
        recordWrite(variable, writer, false);
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
    // Reports the writes of captured variables that turned out to be real.
    void reportIgnoredCapturedWrites();
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
    // `:var/get` or `:var/peek` applied to `variable`, recorded as a read.
    node_handle_t readVariableThrough(
        node_handle_t variable, camel::core::OperId oper);
    // The value `variable` holds (`:var/peek`): the same as readVariable, but the variable keeps
    // its array available for writing in place (the read does not take the array away).
    node_handle_t peekVariable(node_handle_t variable);
    // `node` as a value: read when it is a variable, itself otherwise.
    node_handle_t valueOf(node_handle_t node) {
        return isVariable(node) ? readVariable(node) : node;
    }
    // An operand of a call. A variable named directly stays a variable: whether the callee
    // takes the variable or its value is only known once the callee is resolved.
    node_handle_t visitOperand(const GCT::node_ptr_t &expr, const GCT::node_ptr_t &call);
    // The node of `:var/new` or `:var/get` applied to `input`.
    node_handle_t variableOperator(camel::core::OperId oper, node_handle_t input);
    /// The group of an operator the compiler emits itself (`oper::EnumMake` and the like): the
    /// built-in module registered it; an id is fixed at start-up, the group when that module is
    /// loaded, which is before any module of a program is compiled.
    oper_group_ptr_t internalOperatorGroup(camel::core::OperId oper);
    /// The DREF of such an operator: the construction node carries the id, so the group is not
    /// looked up by a name the program could shadow (visitDRefNode).
    GCT::node_ptr_t internalOperatorDref(camel::core::OperId oper);
    // The immutable name an argument expression denotes: the name itself or a field of it
    // (`s`, `s.x`). Empty for a variable and for an expression that is not a name.
    std::optional<std::string> immutableNameOf(const GCT::node_ptr_t &expr);
    // Reports an immutable name passed where the callee has a `var` parameter.
    void requireMutableArgument(const Argument &arg, const std::string &callee);

    std::any visit(const GCT::node_ptr_t &gct);

    void_ptr_t visitDeclNode(const GCT::node_ptr_t &gct);
    compile_graph_ptr_t visitFuncNode(const GCT::node_ptr_t &gct);
    /// The signature of a lambda whose parameters leave their type out (design principles 5.9),
    /// with those types taken from the function type the position of the lambda expects
    /// (`expected`, the one channel of the expected type). A parameter that type does not
    /// determine is reported, at the parameter, with a located diagnostic. Any other signature
    /// is returned as it is.
    FunctionType *
    completeLambdaSignature(const GCT::node_ptr_t &gct, FunctionType *funcType, Type *expected);
    node_handle_t visitDataNode(const GCT::node_ptr_t &gct);
    Type *visitTypeNode(const GCT::node_ptr_t &gct);
    /**
     * The type a type expression denotes once its `typeof` placeholders are resolved: the
     * expression is built where the type is used, and the placeholder is replaced by its value
     * type. Any other type is bound by `boundType`. A placeholder nested inside a larger type
     * is refused: `typeof` stands for a whole type.
     */
    Type *resolveTypeNode(Type *type, const GCT::node_ptr_t &at);
    node_handle_t visitNRefNode(const GCT::node_ptr_t &gct);
    /// A name: its value node, or the build-time tuple a rest parameter names (no node).
    std::any visitDRefNode(const GCT::node_ptr_t &gct);
    node_handle_t visitCastNode(const GCT::node_ptr_t &gct);
    node_handle_t visitVariNode(const GCT::node_ptr_t &gct);
    node_handle_t visitLinkNode(const GCT::node_ptr_t &gct);
    /// `a..b` (the GCT link of `kCompositionCall`): the composition of two functions, or the
    /// interval of two integers, told apart by the types of the two operands (design principles
    ///  4.2; the rule on ` -generator-and-composition`). `expected`
    /// is the type of the position the whole expression stands in: a function type there is the
    ///  type the lambda `(x) => g(f(x))` would have and instantiates the operands (
    /// section generic-operands-of-composition).
    node_handle_t buildComposition(const GCT::node_ptr_t &gct, Type *expected);
    /// The same, from two operands already built: the sequence position of a loop builds them
    /// itself, to know whether the interval is the loop's form before the rest of `..` is built.
    node_handle_t
    buildCompositionWith(const GCT::node_ptr_t &gct, node_handle_t lhs, node_handle_t rhs);
    /// Whether `expr` is written `a..b`: the GCT link of `kCompositionCall`.
    static bool isIntervalExpr(const GCT::node_ptr_t &expr);
    /// Whether the two operands of `..` are the interval's: two integers, and nothing else
    /// (design principles 4.2).
    bool intervalBounds(node_handle_t from, node_handle_t to) const;
    /// The two bounds of an interval a loop iterates, as the one value its sequence is: a tuple
    /// of the two integers (camel::core::LoopIterableForm::Interval). The interval is the
    /// `int[]` of `intervalValue` in every other position.
    node_handle_t
    intervalSequenceValue(node_handle_t from, node_handle_t to, const GCT::node_ptr_t &expr);
    /// The composition `f..g` as the lambda `(x) => g(f(x))`: a graph of one parameter whose body
    /// calls `f` and then `g`, both of them captures of it.
    node_handle_t composeFunctionValue(
        node_handle_t lhs, node_handle_t rhs, FunctionType *lhsType, FunctionType *rhsType,
        const GCT::node_ptr_t &gct);
    /// The interval `a..b` of two integers as the array `int[]`. The form it is written in is
    /// `a..b` and nothing else: the two-parameter `range(a, b)` it used to be the same operation
    ///  as is gone (design principles 4.2).
    node_handle_t intervalValue(
        node_handle_t lhs, node_handle_t rhs, const GCT::node_ptr_t &gct);
    node_handle_t visitWithNode(const GCT::node_ptr_t &gct);
    node_handle_t visitTypeApplication(const GCT::node_ptr_t &gct);
    FunctionType *signatureForArgumentContext(node_handle_t target) const;
    node_handle_t visitAccsNode(const GCT::node_ptr_t &gct);
    node_handle_t visitBrchNode(const GCT::node_ptr_t &gct);
    /// `match type`: selects a case from the shape of the subject type and builds only that
    /// case's body, in the current graph (no branch node).
    node_handle_t visitTypeMatchNode(const GCT::node_ptr_t &gct);
    /// `static for n, F in fields T`: the body is built once per field, in the current graph.
    /// With `values`, each round's value is collected instead of dropped (a comprehension); with
    /// `shape`, the type the fields were taken from is handed back for the caller to pack.
    node_handle_t visitStaticForNode(
        const GCT::node_ptr_t &gct, std::vector<node_handle_t> *values = nullptr,
        Type **shape = nullptr);
    /// `{ static for ... }` / `( static for ... )`: the rounds' values are packed into a struct or
    /// tuple of the same fields with one FILL; the comprehension leaves no node of its own.
    /// The packed struct, or the build-time tuple of a tuple comprehension (no node).
    std::any visitStaticComprehensionNode(const GCT::node_ptr_t &gct);
    /// `static loop s1 = a1, ..., sk = ak for n, F in fields T do body`: the body is built
    /// once per field, in the current graph, with the state names bound to the previous round's
    /// values; the value is the last state (a tuple FILL when there are several).
    node_handle_t visitStaticLoopNode(const GCT::node_ptr_t &gct);
    /// The pack a `static for` over a struct or tuple makes: a static value of `values.size()`
    /// slots and a FILL whose type has the fields of `shape` (a tuple of the values' types when
    /// `shape` is not a struct). The origins are recorded against `origin` under `originKind`.
    node_handle_t packStaticValues(
        bool isStruct, Type *shape, const std::vector<node_handle_t> &values,
        const GCT::node_ptr_t &origin, const char *originKind);
    /// The FILL of `type` over `values`: what materializeStaticTuple and the tuple half of
    /// packStaticValues build.
    node_handle_t fillStaticValues(
        Type *type, const std::vector<node_handle_t> &values, const GCT::node_ptr_t &origin,
        const char *originKind);
    /// `t.[key]`: the field or tuple element is chosen at compile time.
    node_handle_t visitStaticAccessNode(const GCT::node_ptr_t &gct);
    bool typeCaseMatches(Type *subject, const GCT::node_ptr_t &caseNode) const;

    /// Whether a name read in a case body that is not selected names something. It walks the
    ///  same layers as visitDRefNode, but builds no node and checks no type (step 1, Q10).
    bool valueNameExists(const std::string &written, const GCT::node_ptr_t &gct);

    /// Resolve the names of a case body that is not selected: its own `let`/`var` names bind,
    /// and every other name must exist. No graph node is made and no type is checked.
    void resolveStaticBodyNames(const GCT::node_ptr_t &body);
    ///  Resolve the names of a generic declaration where it is declared (
    /// on `generic-rule-condition-unresolved-name`, docs/handover/reviews/questions-2026-10-23.md):
    /// the parameters bind, every other name its body writes must exist, and the declaration-site
    /// half of its `when` clause is answered. No graph node is made and no type is read, so what
    /// the check needs from the type parameters -- an operator overload, a field, the argument of
    /// a call -- stays with the instantiation. A name that is not one at all is reported here, on
    /// the declaration's line, and the module does not load.
    void resolveGenericTemplateNames(const generic_template_ptr_t &tmpl);
    /// The names a function's own parameters bind, for a body read where it is written: every
    /// parameter's name, and the names a parameter destructured as a tuple binds
    /// (`((a, b)) => ...`, builder.cpp declares them the same way).
    void insertParameterNames(
        const GCT::node_ptr_t &func, const FunctionType *signature,
        std::unordered_set<std::string> &into);
    /// Whether the module's source declares `written` anywhere: the fallback of the
    /// declaration-site check for a name a later declaration gives.
    bool moduleSourceDeclares(const std::string &written);
    void walkStaticBodyNames(
        const GCT::node_ptr_t &node, std::vector<std::unordered_set<std::string>> &locals,
        bool intoFunctions = false);
    static constexpr size_t kMaxStaticMatchDepth = 256;
    /// A loop (design principles 3.10; camel/runtime/loop.h): the first values and the array
    /// where the loop is, its condition and its body as regions of the current graph (built where
    /// they are written, like the arms of a branch), and one LOOP node that has them.
    node_handle_t visitLoopNode(const GCT::node_ptr_t &gct);
    /// Rewrites the body of a loop whose state values are handed back as `xs->append<v>`, when
    /// that state's first value is a fresh literal, to the form that may append to the state in
    /// place (design principles 10.5): the state's old value cannot be seen once the next round
    /// begins, the same criterion as `t = t->relu`. The decision is made per state, so a loop
    /// that appends to one state and counts in another rewrites only the appended one; with one
    /// state the body hands the append call back directly, with several it returns a tuple whose
    /// j-th component is state j's next value.
    void markLoopStateInPlace(
        const arm_ptr_t &body, const GCT::node_ptr_t &loop, std::vector<node_handle_t> &initial,
        const std::vector<bool> &privateInitial = {});
    /// The per-state half of markLoopStateInPlace: rewrites state `index` when its next value
    /// (`next`) is `state->append<v>` and the body uses that state's parameter nowhere else.
    /// `initialIsPrivate`: the first value is a projection of a value the loop does not own (a
    ///  part of a struct state); it is copied once before the loop either way.
    void markLoopStateInPlaceOne(
        const arm_ptr_t &body, const GCT::node_ptr_t &loop, std::vector<node_handle_t> &initial,
        size_t index, node_handle_t next, bool initialIsPrivate = false);
    /// Recognises a state that starts as an empty array and whose only use is appending one
    /// value per round, and gives the loop a collection slot for the appended values
    /// (design principles 3.10). The state itself stays: it is the value the program sees and
    /// the channel its gradient uses. The slot is an extra, internal output; a FILL (or an
    /// access, for one state) projects the loop's value back to the states alone. Must run
    /// before autodiff and before `std::opt`: both read the loop's shape. The body then gives
    /// the tuple of the next states and the appended values.
    std::vector<Type *> collectLoopAppends(
        const arm_ptr_t &body, const GCT::node_ptr_t &loop, const std::vector<Type *> &stateTypes,
        const std::vector<bool> &emptyInitial);
    /// Rewrites an empty-array state's `void[]` type once the body's append has told us the
    /// element type it appends (design principles 5.1: `loop losses = [] for ...` infers from
    /// the value appended), in the ports of the condition (when given) and of the body. Returns
    /// false when the next value is not such an append.
    bool inferLoopStateFromAppend(
        const arm_ptr_t &body, const arm_ptr_t &cond, const GCT::node_ptr_t &loop,
        std::vector<node_handle_t> &initial, std::vector<Type *> &stateTypes, size_t index,
        node_handle_t next);
    /// The BRCH/JOIN pair of a branch on `condNode`. The arms come from the CASE children of
    /// `gct`, or, when `arms` is given, are those arms in order (built by buildArm).
    /// No arm is added to those the program wrote (design principles 3.7).
    /// `valueless` (with `arms`): the branch is an `if` without `else`, whose join takes no
    /// value from its arms.
    /// `firstCase`: the first CASE child the arms come from. The cases before it were already
    /// dispatched, so a guard that did not hold sends the value on to a branch over the cases
    /// from `firstCase` on, built exactly like this one.
    node_handle_t buildBranch(
        const GCT::node_ptr_t &gct, node_handle_t condNode,
        const std::vector<arm_ptr_t> *arms = nullptr, bool valueless = false);
    /// One case of a `match` whose cases carry guards: the form `buildCaseChain` lowers them
    /// in.
    struct CaseStep {
        GCT::node_ptr_t exec;            // the arm of the case: its region, and its body
        node_handle_t condition{};       // the test of its pattern, none for the case that ends
        GCT::node_ptr_t guard = nullptr; // its guard, when it has one
        std::function<void()> bind{};    // takes out the names its pattern binds, in the arm
    };
    /// Whether a CASE child of `gct`, from `first` on, carries a guard.
    bool hasGuardedCase(const GCT::node_ptr_t &gct, size_t first);
    /// The steps `steps` as one decision list, and the value of the `match`. A case is entered
    /// where the test of its pattern holds and, when it has one, its guard holds as well; the
    /// guard of a case whose pattern matched decides whether that case is taken at all, and the
    /// cases written after it are the `else` of its step, so a guard that does not hold leaves
    /// the value to them (review 「when-placement-and-match-guards」). The last step is the case
    /// that ends the list: it has no test of its own.
    node_handle_t buildCaseChain(std::vector<CaseStep> &steps, const GCT::node_ptr_t &origin);
    /// The general `match` `gct`, when its cases carry guards. It cannot be dispatched by case
    /// value alone -- the runtime branch takes the first arm whose case value matches and cannot
    /// be told to try the next when a guard refuses -- so it is one decision list over the cases
    /// in written order.
    node_handle_t buildGuardedMatch(const GCT::node_ptr_t &gct, node_handle_t condNode);
    /// `!value`: a branch that gives true where the value is false, and false where it is true.
    node_handle_t negateCondition(node_handle_t value, const GCT::node_ptr_t &origin);
    /// Refuses the `match` `gct` when it can meet a value none of its cases matches: it has
    /// no `case _` and does not list both values of a bool (MatchWithoutDefault, an error).
    void requireMatchDefault(const GCT::node_ptr_t &gct, node_handle_t scrutinee);
    /// One branch arm, or the condition or the body of a loop (`kind`): a region of the current
    /// graph. Its body is the block `exec`, or what `body` builds (called inside the region's
    /// scope) when given; `exec` then only locates the region. `declare` is called first, inside
    /// the scope: a loop part declares its ports there.
    arm_ptr_t buildArm(
        const GCT::node_ptr_t &exec, const std::function<node_handle_t()> &body = {},
        runtime::GCRegionKind kind                  = runtime::GCRegionKind::Arm,
        const std::function<void(ArmBuild &)> &declare = {});
    /// Gives the arm `arm` the value `result` (the last expression of its body) unless a
    /// `return` has given one.
    void setArmResult(ArmBuild &arm, node_handle_t result);
    /// The arm enters the form `target` at its end (the `CAST` is part of the arm).
    void coerceArmExit(const arm_ptr_t &arm, Type *target, const GCT::node_ptr_t &origin);
    /// The nodes of each arm that read nothing the arm computes wait for the BRCH.
    void linkArmEntries(const std::vector<arm_ptr_t> &arms, node_handle_t brchNode);
    /// After the JOIN is there: the variables the arms changed are changed by the branch.
    void mergeArmVariables(
        const std::vector<arm_ptr_t> &arms, node_handle_t joinNode,
        std::vector<node_handle_t> &written);
    /// The names of a block that is not a graph (an arm).
    void enterBlockScope();
    void leaveBlockScope();

    // ---- The tagged forms `T?` and `T!` (design principles 3.8;
    // docs/technical/29_nullable_and_fallible_values.md). Each construct below is a branch on
    // the one-bit tag of a value; nothing here adds a node kind. ----
    /// `a ?? b`, `a !! b`, `x ?-> f`.
    node_handle_t visitTaggedBrchNode(const GCT::node_ptr_t &gct);
    /// `match x { case null => ..  case v => .. }` and `case error e` over a tagged `x`.
    node_handle_t visitTaggedMatch(
        const GCT::node_ptr_t &gct, node_handle_t scrutinee, camel::core::type::TupleType *type);
    // ---- The enum (design principles 5.3). A value is a tag and the data of its variant; a
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
    /// A call of the compiler's own operator `oper` over the given GCT operands (a DREF of the
    /// operator and the operands), built like any call. The operator is the built-in module's,
    /// found by the id the table fixed at start-up (there is no name lookup, so nothing the
    /// program declares under an internal name changes what the compiler emits).
    /// `typeArgument` is given as `f::<T>`, for an operator whose result type is decided by a
    /// type the program does not write. The operands are GCT nodes; the call is lowered as a
    /// written one would be.
    node_handle_t applyInternalOperator(
        camel::core::OperId oper, camel::core::type::Type *typeArgument,
        const std::vector<GCT::node_ptr_t> &operands, const GCT::node_ptr_t &origin);
    /// The same, for an operator group the program may overload (`__eq__`): the overloads of
    /// every layer are the ones a written call of the name would use (design principles 10.2).
    node_handle_t applyInternalOperator(
        const char *op, camel::core::type::Type *typeArgument,
        const std::vector<GCT::node_ptr_t> &operands, const GCT::node_ptr_t &origin);
    /// The call with the given callee DREF: what both applyInternalOperator forms make.
    node_handle_t applyInternalOperatorCall(
        const GCT::node_ptr_t &callee, camel::core::type::Type *typeArgument,
        const std::vector<GCT::node_ptr_t> &operands, const GCT::node_ptr_t &origin);
    /// The type of a node whose value is of enum type: its result slot is of that type too
    /// (the operator's placeholder type is not what the value is kept as; a slot of a type
    /// the collector does not trace would hide the value's references from it).

    // ---- Conditions (design principles 3.8). `&&` and `||` are branches; a `T?` can
    // be compared with `null`; and where such a comparison is the condition (or an operand
    // `&&` joins in it), the name is a `T` in the arm that excludes null. Each is the
    // BRCH/JOIN pair the equivalent `if` or `match` gives: nothing here adds a node kind. ----
    /// The branch of `a && b`, `a || b`, or of an `if` whose condition compares names with
    /// `null`. nullptr (nothing built) for any other branch, which visitBrchNode builds.
    node_handle_t visitConditionBrchNode(const GCT::node_ptr_t &gct);
    /// `x == null` / `x != null` (either order) where `x` is a `T?`: the test of its tag, a
    /// `bool`. Where `x` has a type that is never null, the constant result of the test, with
    /// a warning (NullTestNeverNull; design principles 3.8). nullptr when the call `gct` of
    /// the operator group `group` is not that.
    node_handle_t nullComparison(
        const std::string &group, const GCT::node_ptr_t &gct, const std::vector<Argument> &operands,
        const camel::core::type::type_vec_t &types);
    /// The values a narrowing `if` binds its names to in the arm that excludes null: the name
    /// `x` in the then arm of `if x != null then ...` stands for one of these (design
    /// principles 3.8). A lookup that finds one of these nodes found a narrowed name;
    /// a declaration that shadows the name finds another node.
    std::unordered_set<node_handle_t> narrowedValues_;
    /// Whether `expr` is a name narrowed to a `T` by an `if` around it (see narrowedValues_).
    bool isNarrowedName(const GCT::node_ptr_t &expr);
    /// `subject`, the value of the narrowed name `expr`, put back into `T?` so that a
    /// construct written for a `T?` (`??`, a `match` with `case null`) applies to it, with a
    /// warning that the name is known not to be null (KnownNotNull; design principles 3.8).
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

    // ---- Operator overloads of the program (design principles 10.2) ----------------------
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
        /// The generic ones (a head that declares type parameters), in this module and imported.
        std::vector<generic_template_ptr_t> templates;
        /// Those of the built-in module and of the C++ modules imported, one group.
        oper_group_ptr_t ops;
        /// The module each imported overload comes from, for diagnostics (absent: this one).
        std::unordered_map<const void *, std::string> moduleOf;
    };
    /// The DREF of an operator (`__add__`) whose set has overloads of the program, with that
    /// set. A DREF of an operator without any is an ordinary DREF to the operator group.
    std::unordered_map<node_handle_t, OperatorOverloads> operatorOverloads_;
    /// The `!=` operators whose overloads (in operatorOverloads_) are those of `==`: an
    /// overload of `==` gives `!=`, the negation of its result (design principles 4.3).
    std::unordered_set<node_handle_t> negatedEqualities_;
    /// The generic overloads of an operator declared in this module, each with the scope it
    /// was declared in (visible from that scope and the scopes inside it).
    std::unordered_map<std::string, std::vector<std::pair<template_scope_ptr_t, generic_template_ptr_t>>>
        operatorTemplates_;
    /// Signatures already checked by rejectOperatorConflict (a function is declared ahead and
    /// then defined).
    std::unordered_set<FunctionType *> checkedOperatorSignatures_;

    /// Collects the overloads of the operator `group` visible here.
    OperatorOverloads operatorOverloadsOf(const std::string &group);
    /// The entity a name the current module imports stands for in this program
    /// (ProgramExports::importedEntity): the import bindings resolved against this program's
    ///  exported entities. The same module in two programs may see different graphs (task).
    std::optional<camel::core::module::entity> importedEntity(
        const Reference &ref,
        camel::core::module::ImportLayer layer = camel::core::module::ImportLayer::All) {
        return exports_->importedEntity(*module_, ref, layer);
    }
    /// The type an imported name stands for. A type declaration is the module's own and the
    /// same in every program, so this does not consult the program's table.
    std::optional<Type *> importedType(
        const Reference &ref,
        camel::core::module::ImportLayer layer = camel::core::module::ImportLayer::All) {
        return module_->getImportedType(ref, layer);
    }
    /// The overloads of `group` the current module imported
    /// (ProgramExports::importedOperatorOverloads), the operator groups merged into one;
    /// computed once per module and operator (the module changes while an instance of an
    /// imported generic function is built).
    const OperatorOverloads &importedOperatorOverloadsOf(const std::string &group);
    std::map<std::pair<const void *, std::string>, OperatorOverloads> importedOperators_;
    /// The DREF for a use of the operator `group` (visitDRefNode).
    node_handle_t operatorDrefOf(const std::string &group, const GCT::node_ptr_t &gct);
    /// The program's overload a call of an operator takes (an instance for a generic one), or
    /// nullptr when the call is the built-in group's to resolve (no overload of the program
    /// accepts the arguments; then the group reports what does not fit). Reports
    /// AmbiguousCall when more than one overload, built-in ones included, accepts them.
    compile_graph_ptr_t chooseOperatorOverload(
        const OperatorOverloads &set, std::vector<Argument> &withInputs,
        std::vector<Argument> &normInputs, camel::core::type::type_vec_t &withTypes,
        camel::core::type::type_vec_t &normTypes, const GCT::node_ptr_t &call,
        const std::function<bool(FunctionType *, bool)> &accepts);

    // ---- An operator of the module that declares the operand's type (design principles 5.7) --
    //
    // Names are looked up lexically where the expression is written; a value of a dependent
    // type variable is also operated on as its type's declaration means, so an instance looks
    // for the operator in the module that declares the operand's named type as well, not only
    // in the module the generic function is written in. What the caller imported is not looked
    // at. The types are concrete by the time an instance is built, so the modules are found
    // from the operand types.

    /// The modules whose overloads an operator call over these operand types may also use:
    /// for each named struct or enum type among the operand value types, the module that
    /// declares it (with the type's name as written), unless that is this module. Empty outside
    /// a generic instance, and for anonymous, builtin and alias types, which have no declaring
    /// module. Duplicates (two operands of the same type) appear once.
    std::vector<std::pair<std::string, std::string>>
    declaringModulesOf(const camel::core::type::type_vec_t &withTypes,
                       const camel::core::type::type_vec_t &normTypes);
    /// Adds what the declaring modules of the operands export as overloads of `group` to the
    /// set of `dref`, once each: an overload the module also imports (or one found by both
    /// routes) stays one candidate. `!=` takes the `==` overloads, as it does for the ones the
    /// program declares (design principles 4.3). Returns whether anything was added.
    bool addDeclaringModuleOverloads(
        node_handle_t dref, const std::string &group,
        const camel::core::type::type_vec_t &withTypes,
        const camel::core::type::type_vec_t &normTypes);
    /// Whether a module that declares one of the operand types exports an overload of `group`
    /// (design principles 5.7). Asked before an operator DREF exists, for `+=`, which follows the
    /// plain operator (`compoundAssignmentByRebinding`) — together with the group's own rules
    /// and the program's, `plainOperatorAvailableFor`.
    bool hasDeclaringModuleOperator(
        const std::string &group, const camel::core::type::type_vec_t &withTypes,
        const camel::core::type::type_vec_t &normTypes);
    /// The scope the diagnostic names when no overload fits: the module the expression is
    /// written in, and the declaring modules of the operands. Empty when the declaring-module
    /// lookup does not apply, so that the diagnostic of an ordinary operator failure is
    /// unchanged.
    std::string operatorSearchScopeText(
        const camel::core::type::type_vec_t &withTypes,
        const camel::core::type::type_vec_t &normTypes);
    /// The loaded module of this name, or nullptr (the declaring module of a type is loaded:
    /// the type came from it).
    camel::core::module::module_ptr_t moduleNamed(const std::string &name);
    /// What a diagnostic calls the module `name`: the name its source gives it, else the name
    /// it is loaded under.
    std::string moduleDisplayName(const std::string &name);
    /// Whether the generic overload `tmpl` accepts the arguments `normInputs` of the types
    /// `normTypes`: its type variables bind and every argument then fits its parameter
    /// (resolveGenericCall without the diagnostics). An argument that is a composite literal
    /// enters its parameter's type, as it does in a call (design principles 5.1).
    bool genericOperatorAccepts(
        const generic_template_ptr_t &tmpl, const std::vector<Argument> &normInputs,
        const camel::core::type::type_vec_t &normTypes);
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

    // ---- Collections are not changed in place (design principles 11.3) --------------------

    /// The statement of the block being built whose value nothing takes: every statement but
    /// the last (which is the block's value), while it is visited. A call that is this
    /// statement may be the rebinding `acc = acc->append<v>` (inPlaceRebindingOf, with the
    /// in-place form `append` declares) or a pure operation whose result is lost
    /// (rejectUnusedPureResult).
    const GCT::Node *unusedStatement_ = nullptr;

    //  ---- Comprehensions and static access (steps 3 and 4) ------------------------------
    //
    // `t.[n].[m]` inside a `static for` names the same parent once per inner round. The parent's
    // value does not change between the rounds of one expansion (the target is the same node:
    // a parameter or a `let`), so the second `t.[n]` would be a second ACCS on the same parent.
    // The access is remembered for the duration of the expansion and reused: a bound name and
    // the access written out build the same graph. Only an expansion reuses it; outside one the
    // cache is not consulted. Keyed by the target's draft node and the resolved field, both
    // already compared by identity; a target that makes a fresh node (a `var` read, a call)
    // therefore never matches an earlier round.
    std::map<std::pair<node_handle_t, std::string>, node_handle_t> staticAccessCache_;
    size_t staticExpansionDepth_ = 0;
    /// `xs = xs->op<w...>` (or `xs = op<w...>(xs)`) as the statement `gct` of a sync block,
    /// `xs` a variable holding an array and `op` an operator of a module that declares an
    /// in-place form for the overload the call resolves to (OperatorTable::setInPlaceForm;
    /// design principles 4.3: `xs = xs->sort`). The same rebinding, written
    /// `:op/assn_owned(xs, <in-place form><w...>(:var/take(xs)))`, which works on the
    /// variable's own array when nothing else refers to it and on a copy otherwise
    /// (__builtin__var_take__). Returns nullptr when `gct` is not that form; the statement
    /// is then built as written.
    node_handle_t inPlaceRebindingOf(const GCT::node_ptr_t &gct);
    /// A path assignment (design principles 3.5): `v.f = e`, `v[i] = e`, `v[i].f += e`, any
    /// combination of fields and indices starting at a variable `v`. The whole value of `v` is
    /// rebound to a `set` chain:
    ///   LINK(DREF __assn__, <path>, e)  ->
    ///   LINK(DREF __assn__, DREF v,
    ///        LINK(DREF __set_index__, DREF v, i, LINK(DREF __set_field__, DREF v[i], f, e)))
    /// and nothing downstream learns that a field or an element was written. In a sync block an
    /// array variable is written where it is: the outermost index set is the in-place form and
    /// the variable's array is the one `:var/take` gives it. Returns nullptr when `gct` is not
    /// such an assignment (the call is then built as written).
    node_handle_t pathAssignmentOf(const GCT::node_ptr_t &gct);
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
    std::unordered_map<const void *, std::pair<GCT::node_ptr_t, node_handle_t>>
        bodyValueStatement_;
    /// The last statement of the block visitExecNode finished most recently, and its node.
    std::pair<GCT::node_ptr_t, node_handle_t> lastBlockStatement_{};
    /// UnusedPureResult at the last statement of the body of `graph` when it is a call of a
    /// pure operator: the body's value is dropped where `graph` is used (see above).
    void rejectDiscardedValue(const void *body);
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
    /// The call `oper(<name>)` of one of the built-in operators over tagged values, by the id of
    /// the compiler's own operator (`oper::TagIsNull` and the like; internalOperatorDref).
    node_handle_t applyTagOperator(
        camel::core::OperId oper, const std::string &name, const GCT::node_ptr_t &origin);
    /// The call `<op>(<name>)` of a group the program may overload (`__not__`).
    node_handle_t
    applyTagOperator(const char *op, const std::string &name, const GCT::node_ptr_t &origin);
    /// The call with the given callee DREF: what both applyTagOperator forms make.
    node_handle_t applyTagOperatorCall(
        const GCT::node_ptr_t &callee, const std::string &name, const GCT::node_ptr_t &origin);
    /// A CAST of `value` to `target`, made by the compiler (no conversion rule is consulted).
    node_handle_t
    castNodeTo(node_handle_t value, camel::core::type::Type *target, const GCT::node_ptr_t &origin);
    /// `node` where a value of type `target` is expected (design principles 5.5): `node`
    /// itself when its type is assignable; otherwise the value put into `T?` / `T!` (one
    /// CAST per level), `null` as a `T?`, or the empty literal `[]` given the element type of
    /// `target`. A literal whose value the target type can hold takes the target type (design
    /// principles 5.1: `let x: float = 1`, `return 2` under `f32`), and so does an array or
    /// struct literal: its elements (fields) are positions with an expected type.
    /// nullptr when the value does not enter (camel::core::type::entersImplicitly).
    node_handle_t
    coerceTo(node_handle_t node, camel::core::type::Type *target, const GCT::node_ptr_t &origin);
    /// Applies the expected type of a position to the value built there, in the one place it is
    /// done: a value that names a generic function is instantiated for `expected`, then the
    /// value takes the type (coerceTo: literals, empty arrays, struct copies, `T?` / `T!`).
    /// nullptr when the value does not enter the type; callers that can report it do.
    node_handle_t checkValue(
        node_handle_t node, camel::core::type::Type *expected, const GCT::node_ptr_t &origin);
    /// Builds `gct` in a position that expects `expected`: the type is in force while the
    /// expression is built (a generic function value reads it), and checkValue applies it to
    /// the result. `expected` may be nullptr (no position).
    node_handle_t
    visitInPosition(const GCT::node_ptr_t &gct, camel::core::type::Type *expected);
    /// The literal at `gct` built as `expected` while it is made, so that no literal of its own
    /// type is left unused in the graph; nullptr when `gct` is not a literal that takes the
    /// type or the literal already is of it. Used by visitInPosition.
    node_handle_t
    literalDataInType(const GCT::node_ptr_t &gct, camel::core::type::Type *expected);
    /// `value` as the struct type `target`, field by field and by name: a real copy, made when
    /// the fields are the same but not in the same order, or when a field type is refined. No
    /// copy is made when the layouts already agree (that conversion is a CAST, CastSafety).
    /// nullptr when `value` is not a struct whose fields are the target's fields, by name and
    /// with a field-by-field conversion for each. A named struct type is a copy of its fields
    /// in the declaration's order (design principles 5.2); an unnamed value may enter it this
    /// way, and an explicit `as` may convert between two named types this way.
    node_handle_t convertStructValue(
        node_handle_t value, camel::core::type::StructType *target,
        const GCT::node_ptr_t &origin);
    /// The constant an array / struct literal written at `origin` becomes when the position
    /// gives its elements / fields the type `target` (design principles 5.1): `f([1, 2])` with
    /// `f(xs: float[])`, `return { a: 1 }` under `{ a: float }`. nullptr when `node` is not a
    /// literal of that shape, or when a part of it cannot take the type it is given (a literal
    /// the type cannot hold is reported here).
    node_handle_t compositeLiteralInType(
        node_handle_t node, camel::core::type::Type *target, const GCT::node_ptr_t &origin);
    /// Whether the array / struct literal written at `origin` can be given `target` as its type,
    /// with the same walk as compositeLiteralInType but without building the converted value and
    /// without reporting: an argument enters its parameter's type while overloads are tried
    /// (design principles 5.1), and the conversion — which may report a value that does not fit
    /// — is made once the callee is known (coerceTo). A value of the right kind that is out of
    /// the type's range still enters: it is the conversion that refuses it. An element that is a
    /// name is a value of its own type and does not enter; neither does one of another kind.
    bool compositeLiteralEntersIn(
        node_handle_t node, camel::core::type::Type *target, const GCT::node_ptr_t &origin);
    /// `data` with every element (field) in `type`, recursively; nullptr when a part of it is
    /// not a value of the type. The exact value an element was written as, kept by the GCT for
    /// a composite literal, is used rather than the double it was lowered to, so an element is
    /// rounded once (design principles 5.1).
    camel::core::data::data_ptr_t compositeDataInType(
        const camel::core::data::data_ptr_t &data, camel::core::type::Type *type,
        const GCT::node_ptr_t &origin);
    /// The same walk as compositeDataInType for a literal that is not one constant: some of its
    /// elements are computed (`refNames` / `refTypes` are the names the GCT gave them and the
    /// types of the nodes they stand for, in the literal's reference order), and the elements it
    /// did write take the types `position` states for them — a tuple, array or struct field the
    /// position does not reach otherwise (design principles 5.1). nullptr when the position does
    /// not fit the literal: another kind or shape, or a computed element that is not of the type
    /// the position gives that element — the literal then keeps the type its own elements give
    /// it, and the place it was written in reports the mismatch. The converted literal is a
    /// value of `position` itself, a named struct type included (design principles 5.2).
    /// `refHoles` is where the computed elements sit in the literal's own type, parallel to
    /// `refNames`; on success `holeSlots` holds where they sit in `position`, which is what the
    /// fill of the value reads. An array and a tuple are positional, so the indexes are the same;
    /// the fields of a struct are the same by name but a named type states its own order (design
    /// principles 5.2), so the slot of a computed field is the field of that name in `position`,
    /// not the index it had in the literal's type.
    camel::core::data::data_ptr_t compositeWithHolesInType(
        const camel::core::data::data_ptr_t &data, camel::core::type::Type *position,
        const GCT::node_ptr_t &origin, const std::vector<std::string> &refNames,
        const camel::core::type::type_vec_t &refTypes, const std::vector<size_t> &refHoles,
        std::vector<size_t> &holeSlots);
    /// One element (field) of a composite literal as a value of `type`: the value itself when
    /// the type accepts it, the exact conversion when it is a literal (`written` is the
    /// element as it was written, when the GCT kept it: its value and its position), nullptr
    /// for a type a literal cannot be given this way (`T?` / `T!`, a shape that does not
    /// match). A value the type cannot hold is reported at the element's own position,
    /// quoting it as written.
    camel::core::data::data_ptr_t literalElementInType(
        const camel::core::data::data_ptr_t &data, camel::core::type::Type *type,
        const GCT::node_ptr_t &origin, const GCT::Tree::LiteralElement *written = nullptr);
    // ---- Literals: the value as written, until the context gives it a type (5.1) ----
    /// The exact value of the expression `expr` when it is a literal: a numeric literal
    /// written in the program, or an expression of literals only that folds to one. A name
    /// bound to a constant is NOT one: a variable keeps the type it has.
    std::optional<camel::compile::literal::Value> literalValueOfExpr(const GCT::node_ptr_t &expr);
    /// The exact value of `gct`, a literal of the construction tree, when it has one.
    static const camel::compile::literal::Value *literalValueOfGct(const GCT::node_ptr_t &gct);
    /// The value as a constant of `type`. Reports SemanticDiag::LiteralNotRepresentable at the
    /// whole expression (the literal, or the expression it was folded from) and aborts when the
    /// type cannot hold it (a literal that does not fit its context is an error, not a value
    /// cut to size).
    node_handle_t makeLiteralNode(
        const camel::compile::literal::Value &value, camel::core::type::Type *type,
        const GCT::node_ptr_t &origin);
    /// The value as a constant of `type`, or nullptr when it does not fit (nothing reported).
    node_handle_t literalInType(
        const camel::compile::literal::Value &value, camel::core::type::Type *type,
        const GCT::node_ptr_t &origin, bool report);
    /// The type a literal of this kind has when its context does not say: `int` for an
    /// integer, `float` for a decimal or exponent form (design principles 5.1).
    static camel::core::type::Type *defaultTypeOf(const camel::compile::literal::Value &value);
    /// The value of a literal-only operator expression, computed exactly; nothing when `gct`
    /// is not one (a name, a call of a function, an operator the program overloads, a
    /// division by zero, a string). Used to fold an expression and to know what an operand is
    /// worth before the operator picks its types.
    std::optional<camel::compile::literal::Value> foldedLiteralValue(const GCT::node_ptr_t &gct);
    /// The constant a literal-only operator expression computes to, or nullptr when `gct` is
    /// not one. The value takes the expected type when it is numeric, and otherwise the
    /// default type of its kind (design principles 5.1). A value the type cannot hold is
    /// reported at `gct` and aborts.
    node_handle_t foldLiteralCall(const GCT::node_ptr_t &gct, camel::core::type::Type *expected);
    /// The design principles 5.1 resolution order for the operands of an operator: a literal
    /// operand takes the type of the other operand when that one is numeric, and then the
    /// value is converted exactly (an operand that does not fit is reported). With
    /// `anyOperatorCall`, a named operator call (`add(x, 2.0)`) is adapted as well; without it
    /// only the infix operators whose operands are related that way.
    void adaptLiteralOperands(
        const std::string &group, std::vector<Argument> &inputs,
        camel::core::type::type_vec_t &types, const GCT::node_ptr_t &call,
        bool anyOperatorCall = false);
    /// The return type the function around the current position declares (looking out of
    /// branch arms), or nullptr when it declares none.
    camel::core::type::Type *declaredExitTypeHere() const;
    /// The type a `return` here is expected to have (design principles 5.1): the declared
    /// return type of the function, or, for a `return` in a branch arm, the type the arm's
    /// position expects (the branch's expected type; nullptr when nothing expects one, and
    /// then the other arms decide through the join). The declared return type of the function
    /// is not the expected type of an arm's `return`: the value of an arm is the branch's, and
    /// the declared type is checked where the function itself returns (language reference
    ///  7.3).
    camel::core::type::Type *expectedExitTypeHere() const;
    /// What `return value` / `throw value` hands back: the value put into the declared
    /// `T?` / `T!`, or the failed form of the declared `T!`.
    node_handle_t adaptExitValue(const GCT::node_ptr_t &gct, node_handle_t value);
    /// `file:line:column` of an origin, for the `location` of an error; empty when unknown.
    std::string sourceLocationOf(uint64_t origin) const;
    size_t tagTemporaries_ = 0;
    node_handle_t visitAnnoNode(const GCT::node_ptr_t &gct);
    node_handle_t visitExitNode(const GCT::node_ptr_t &gct);
    /// Builds the statements of a block. `tailExpected` is the type of the position the block's
    /// last value stands at (a body's value; see visitBodyNode), nullptr when nothing expects a
    /// value.
    node_handle_t
    visitExecNode(const GCT::node_ptr_t &gct, camel::core::type::Type *tailExpected = nullptr);
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
