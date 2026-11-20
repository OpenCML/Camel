// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The program-wide side of the GIR builder (design principles 10.1 "模块"; the graph model,
 * docs/design/graph-model/README.cn.md 3.9.4): one builder per program, and loading in three
 * steps.
 *
 * 1. Declare (declareModule): each module, once its construction tree is built, makes the
 *    graphs of its functions without bodies, registers its generic functions, its top-level
 *    values and its types, and fills its export table. Nothing of another module is looked
 *    at, so modules may import each other.
 * 2. Build (buildProgram, buildModuleBody): the modules in the order of their imports, each
 *    top-level declaration in the order it is written; but a function or a value is built
 *    when it is first needed, by whichever module needs it (ensureFunctionBuilt,
 *    buildTopLevelValue). So the top level of a module does not depend on the order it is
 *    written in, and a function's return type and captures are known where it is used. A
 *    value needed while it is being built depends on itself: ModuleValueCycle.
 * 3. Link: the captures a call is still owed (linkCaptureSites, a least fixpoint; the ones the
 *    call was built with are already its `with` inputs), the call of the entry module's `main`,
 *    the summaries of effects (another fixpoint) and the warnings
 *    that need them, per module.
 *
 * A library (buildLibrary) is built the same way by a builder of its own, from the library
 * module instead of the entry module and without a call of `main`: the definition libraries
 * of operators (docs/design/op-semantics, section 9), compiled when a definition is first
 * asked for, after the program itself is built.
 */

#include "builder.h"
#include "builder_support.h"

#include "camel/core/mm.h"
#include "camel/core/module/userdef.h"
#include "camel/core/operator_names.h"
#include "camel/core/type/composite/func.h"
#include "camel/parse/ast/rule_head.h"
#include "camel/runtime/graph.h"

#include "camel/compile/gir/reachable.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <format>
#include <functional>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::module;

namespace camel::compile::gir {

/// Puts the builder at the top level of a module (its tree, diagnostics and scopes, the root
/// graph as the current graph, nothing of an enclosing construct in force) for as long as it
/// lives, and back to where it was. Step two builds one module's declarations from inside
/// another's body: everything the builder keeps of "where it is" is saved here.
class Builder::UnitContext {
  public:
    UnitContext(Builder &builder, ModuleUnit &unit)
        : self_(builder), unit_(builder.unit_), module_(builder.module_), diags_(builder.diags_),
          tree_(builder.tree_), graph_(builder.currGraph_), nodeScope_(builder.nodeScope_),
          graphScope_(builder.graphScope_), decoratedScope_(builder.decoratedScope_),
          templateScope_(builder.templateScope_), bindings_(builder.typeBindings_),
          depth_(builder.instantiationDepth_), synced_(builder.synced_), varied_(builder.varied_),
          declaresVariable_(builder.declaresVariable_),
          calleeAllowed_(builder.genericCalleeAllowed_), lastSynced_(builder.lastSyncedNode_),
          refOrigin_(builder.refOrigin_), namespacePath_(builder.namespacePath_),
          usingFrames_(builder.usingFrames_) {
        builder.unit_                 = &unit;
        builder.module_               = unit.module;
        builder.diags_                = unit.diags;
        builder.tree_                 = unit.tree;
        builder.currGraph_            = builder.rootGraph_;
        builder.nodeScope_            = unit.nodeScope;
        builder.graphScope_           = unit.graphScope;
        builder.decoratedScope_       = unit.decoratedScope;
        builder.templateScope_        = unit.templateScope;
        builder.typeBindings_         = {};
        builder.instantiationDepth_   = 0;
        builder.synced_               = false;
        builder.varied_               = false;
        builder.declaresVariable_     = false;
        builder.genericCalleeAllowed_ = false;
        builder.lastSyncedNode_       = {};
        builder.namespacePath_.clear();
        // The `using namespace` of the module's top level are in force everywhere in it.
        builder.usingFrames_.clear();
        if (unit.tree != nullptr && unit.tree->root() != nullptr) {
            if (const auto *usings = unit.tree->usingsOf(unit.tree->root())) {
                builder.usingFrames_.push_back(usings);
            }
        }
    }
    ~UnitContext() {
        self_.unit_                 = unit_;
        self_.module_               = module_;
        self_.diags_                = diags_;
        self_.tree_                 = tree_;
        self_.currGraph_            = graph_;
        self_.nodeScope_            = nodeScope_;
        self_.graphScope_           = graphScope_;
        self_.decoratedScope_       = decoratedScope_;
        self_.templateScope_        = templateScope_;
        self_.typeBindings_         = std::move(bindings_);
        self_.instantiationDepth_   = depth_;
        self_.synced_               = synced_;
        self_.varied_               = varied_;
        self_.declaresVariable_     = declaresVariable_;
        self_.genericCalleeAllowed_ = calleeAllowed_;
        self_.lastSyncedNode_       = lastSynced_;
        self_.refOrigin_            = refOrigin_;
        self_.namespacePath_        = std::move(namespacePath_);
        self_.usingFrames_          = std::move(usingFrames_);
    }
    UnitContext(const UnitContext &)            = delete;
    UnitContext &operator=(const UnitContext &) = delete;

  private:
    Builder &self_;
    ModuleUnit *unit_;
    module_ptr_t module_;
    diagnostics_ptr_t diags_;
    GCT::Tree *tree_;
    compile_graph_ptr_t graph_;
    node_scope_ptr_t nodeScope_;
    graph_scope_ptr_t graphScope_;
    decorated_scope_ptr_t decoratedScope_;
    template_scope_ptr_t templateScope_;
    camel::core::type::type_bindings_t bindings_;
    size_t depth_;
    bool synced_, varied_, declaresVariable_, calleeAllowed_;
    node_handle_t lastSynced_;
    camel::source::origin_id_t refOrigin_;
    std::vector<std::string> namespacePath_;
    std::vector<const std::vector<Reference> *> usingFrames_;
};

namespace {
/// The namespaces of a qualified name (`A::B` of `A::B::f`).
std::vector<std::string> namespacesOf(const std::string &name) { return Reference(name).paths(); }
} // namespace

bool Builder::inModule(
    const camel::core::module::module_ptr_t &module, const std::function<void()> &body) {
    const auto unit = units_.find(module.get());
    if (unit == units_.end()) {
        return false;
    }
    UnitContext context(*this, *unit->second);
    // The sides of a rule are functions of the module's top level: no decorator is around them
    // (language reference 7.6), as for the declarations built above.
    AttrScope attrs(currentAttrs_, camel::core::attr::kNoAttrs);
    body();
    return true;
}

std::vector<std::string> Builder::namespacesOf(const TopLevelDeclaration &declaration) const {
    // An operator overload says its block apart from its name (GCT::Tree::namespaceOf).
    if (const auto *path = declaration.unit->tree->namespaceOf(declaration.gct)) {
        return *path;
    }
    return ::camel::compile::gir::namespacesOf(declaration.names.front());
}

namespace {

/// The names a top-level value declaration binds: `let x = ...` (NREF), `var x = ...` (VARI of
/// an NREF), `let a, b = ...` and the unpacking forms (an EXEC of NREFs, the first of which
/// may be a temporary the compiler names with a number).
void collectBoundNames(const GCT::node_ptr_t &node, std::vector<std::string> &names) {
    switch (node->type()) {
    case GCT::NodeKind::NREF:
        names.push_back(node->name());
        break;
    case GCT::NodeKind::VARI:
    case GCT::NodeKind::EXEC:
        for (size_t i = 0; i < node->size(); ++i) {
            if (node->at(i)->type() == GCT::NodeKind::NREF) {
                names.push_back(node->at(i)->name());
            } else if (node->type() == GCT::NodeKind::VARI) {
                collectBoundNames(node->at(i), names);
            }
        }
        break;
    default:
        break;
    }
}

/// Whether a name was written in the program (the compiler names its temporaries with a
/// number or a `$`).
bool isWrittenName(const std::string &name) {
    return !name.empty() && !std::isdigit(static_cast<unsigned char>(name.front())) &&
           name.front() != '$';
}

/// Whether `family`'s transform reads a rule's callee name and calls *that* function, never the
/// rule's right side: such a family has nothing to bind its pattern's names with, so its rules may
/// not read them (`SemanticDiag::RuleRightUsesOuterVariable`).
///
/// `rulesets.jvp` is not one of these: `jvp` calls the right side itself, once with the operands
/// and then with what it answers, so its rules write `x` and `w` on the right side and must
/// (`modules/transforms/jvp.cpp`). `rulesets.vmap` is: the batched graph has no unbatched value to
/// give the outer arrow, so the transform reads `rhsCalleeOf` and calls the named function with the
/// batch (`modules/transforms/vmap.cpp`), and a rule that reads `x` has nothing to read.
bool familyCallsOnlyItsCallee(const std::string &family) {
    // `rulesets.probe_batch` is the fixture of the same shape: the refusal is reachable on purpose
    // there (stdlib/rulesets/probe_batch.cml), the way `rulesets.probe_missing` makes a missing
    // rule reachable for `jvp`.
    return family == "rulesets.vmap" || family == "rulesets.probe_batch";
}

} // namespace

std::string Builder::qualifiedName(const ModuleUnit &unit, const std::string &name) {
    return unit.entry ? name : std::format("{}::{}", unit.module->name(), name);
}

Builder &Builder::of(const context_ptr_t &context) {
    GUARD(context != nullptr, "The graph builder of a program needs its context.");
    auto &session = context->graphBuildSession();
    if (!session) {
        // The context's own program declares its modules into the context's export table: other
        // readers of the program (the semantic analysis, definition lookups) see the same names.
        session = std::make_shared<Builder>(context, context->programExports());
    }
    return *std::static_pointer_cast<Builder>(session);
}

void Builder::claimRootNodes(size_t firstNode) {
    const size_t count = rootGraph_->view().nodeSlotCount();
    if (rootNodeOwners_.size() < count) {
        rootNodeOwners_.resize(count, nullptr);
    }
    for (size_t id = firstNode; id < count; ++id) {
        if (rootNodeOwners_[id] == nullptr) {
            rootNodeOwners_[id] = unit_;
        }
    }
}

// ---- Step one: declare ----

void Builder::declareModule(const module_ptr_t &module, GCT::Tree &tree, diagnostics_ptr_t diags) {
    // A signature is read here, before any body is built, and a type in it may be a `match type`
    // whose guard decides it (`func f(x: ShapeOf<string>)`): the predicate has to be answerable
    // here too, or the type would look like one no case selects.
    GuardScope guardScope(*this);
    // And it records the pairs of function types one type variable is asked for, which are judged
    // once every body is measured: a type in a top-level declaration is read here too
    // (resource_conflict.h).
    ResourceConflictScope conflictScope(*this);
    ensureRootGraph();
    recorder_  = context_ ? context_->semanticRecorder() : nullptr;
    auto &slot = units_[module.get()];
    if (!slot) {
        slot = std::make_unique<ModuleUnit>();
        unitOrder_.push_back(slot.get());
    }
    ModuleUnit &unit    = *slot;
    unit.module         = module;
    unit.tree           = &tree;
    unit.diags          = diags;
    unit.entry          = context_ && context_->mainModule() == module;
    unit.nodeScope      = node_scope_t::create();
    unit.graphScope     = graph_scope_t::create();
    unit.decoratedScope = decorated_scope_t::create();
    unit.templateScope  = template_scope_t::create();

    UnitContext context(*this, unit);
    const GCT::node_ptr_t &root = tree.root();
    try {
        // The generic functions of the top level are templates; the other functions get their
        // graphs (with their declared types, without bodies) from the DECL nodes the
        // construction tree puts ahead of everything else.
        registerGenericTemplates(root);
        for (size_t i = 0; i < root->size(); ++i) {
            const GCT::node_ptr_t &child = root->at(i);
            if (child->type() != GCT::NodeKind::DECL) {
                continue;
            }
            try {
                visitDeclNode(child);
            } catch (const BuildAbortException &) {
                // Reported (a redeclaration); the declaration is left out.
            }
        }
        // The body-less declarations are exported as the operator groups of their names once
        // all of them have been read: the group of a name is its set of overloads (design
        // principles 10.2). A module written in Camel otherwise exports through the program's
        // table; an operator group is the module's own, as it is for a C++ module. The export
        // list is read first: a declaration the module does not export is a private operator of
        // the module.
        GCT::node_ptr_t expt = nullptr;
        for (size_t i = 0; i < root->size(); ++i) {
            if (root->at(i)->type() == GCT::NodeKind::EXPT) {
                expt = root->at(i);
                break;
            }
        }
        exportNativeOperators(expt);
        // The `#impl` functions of this module, once every body-less declaration of it is
        // known: each kernel is checked against the function it stands for, and the function's
        // body graph is recorded as the semantic body a call does not use (design principles
        // 10.4).
        resolveImplDeclarations(root);
        // What step two builds, in the order it is written.
        for (size_t i = 0; i < root->size(); ++i) {
            const GCT::node_ptr_t &child = root->at(i);
            auto declaration             = std::make_unique<TopLevelDeclaration>();
            declaration->gct             = child;
            declaration->unit            = &unit;
            if (child->type() == GCT::NodeKind::EXPT) {
                expt = child;
                continue;
            }
            if (child->type() == GCT::NodeKind::FUNC && child->nativeDeclaration()) {
                continue; // a declaration of an operator: no graph of its own
            }
            if (child->type() == GCT::NodeKind::FUNC) {
                FunctionType *declared = tt::as_ptr<FunctionType>(child->at(0)->dataType());
                if (isGenericHere(declared, child)) {
                    continue; // a template: its instances are built where they are called
                }
                // The graph its DECL made: found by the declared type object, like overloads.
                if (auto graphs = graphScope_->get(child->name(), false); graphs.has_value()) {
                    for (const auto &graph : **graphs) {
                        if (graph->funcType() == declared) {
                            declaration->graph = graph;
                        }
                    }
                }
                if (declaration->graph == nullptr) {
                    continue; // its declaration was refused
                }
                declaration->names = {child->name()};
                if (!unit.entry) {
                    // Shown with its module wherever it is shown (dumps, diagnostics, the
                    // debugger), unless it is the entry module's (design principles 10.2).
                    declaration->graph->setName(qualifiedName(unit, child->name()));
                }
                topLevelFunctions_[declaration->graph.get()] = declaration.get();
                unit.declarations.push_back(std::move(declaration));
                continue;
            }
            collectBoundNames(child, declaration->names);
            if (declaration->names.empty()) {
                continue; // a `using`, a type: nothing to build
            }
            declaration->isVar = child->type() == GCT::NodeKind::VARI;
            for (const std::string &name : declaration->names) {
                // A second declaration of a name is reported where it is built.
                unit.values.try_emplace(name, declaration.get());
            }
            unit.declarations.push_back(std::move(declaration));
        }
        if (expt) {
            unit.exportList = expt;
            exportDeclarations(expt, false);
        }
    } catch (const BuildAbortException &) {
        // Reported where it was raised.
    } catch (Diagnostic &d) {
        diags_->add(std::move(d));
    }
}

// ---- Step two: build ----

std::string Builder::shownName(const TopLevelDeclaration &declaration) const {
    std::string name;
    for (const std::string &candidate : declaration.names) {
        if (isWrittenName(candidate)) {
            name = candidate;
            break;
        }
    }
    if (name.empty() && !declaration.names.empty()) {
        name = declaration.names.front();
    }
    if (declaration.unit != nullptr && module_ && declaration.unit->module != module_) {
        return std::format("{}::{}", declaration.unit->module->name(), name);
    }
    return name;
}

void Builder::reportValueCycle(const TopLevelDeclaration &declaration) {
    // The chain from where the declaration started to be built to the reference that asks
    // for it again, each with where it is declared.
    const auto start = std::find(buildStack_.begin(), buildStack_.end(), &declaration);
    std::string chain;
    for (auto it = start; it != buildStack_.end(); ++it) {
        const std::string where = sourceLocationOf((*it)->gct->origin());
        chain += shownName(**it) + (where.empty() ? "" : " (" + where + ")") + " -> ";
    }
    chain += shownName(declaration);
    // A function in the chain is named as the function; the value that closes it is the one
    // whose node would need itself.
    const TopLevelDeclaration *value = &declaration;
    if (declaration.graph != nullptr) {
        for (auto it = start; it != buildStack_.end(); ++it) {
            if ((*it)->graph == nullptr) {
                value = *it;
                break;
            }
        }
    }
    auto diag = diags_->of(SemanticDiag::ModuleValueCycle);
    diag.atOrigin(
        refOrigin_ != camel::source::kInvalidOriginId ? refOrigin_ : declaration.gct->origin());
    diag.commit(shownName(*value), chain);
    throw BuildAbortException();
}

void Builder::buildTopLevelValue(TopLevelDeclaration &declaration) {
    switch (declaration.state) {
    case BuildState::Built:
        return;
    case BuildState::Failed:
        // Its error was reported; what uses it follows from that error.
        throw BuildAbortException();
    case BuildState::Building:
        reportValueCycle(declaration);
    case BuildState::Declared:
        break;
    }
    declaration.state = BuildState::Building;
    buildStack_.push_back(&declaration);
    UnitContext context(*this, *declaration.unit);
    // A declaration of the top level: no decorator is around it (language reference 7.6).
    AttrScope attrs(currentAttrs_, camel::core::attr::kNoAttrs);
    // Built in the namespace it is declared in, with the `using namespace` of its blocks.
    namespacePath_ = namespacesOf(declaration);
    UsingScope usings(*this, declaration.unit->tree->usingsOf(declaration.gct));
    const size_t firstNode = rootGraph_->view().nodeSlotCount();
    try {
        (void)visit(declaration.gct);
        declaration.state = BuildState::Built;
        if (declaration.isVar) {
            for (const std::string &name : declaration.names) {
                if (const auto cell = declaration.unit->nodeScope->get(name, false);
                    cell.has_value()) {
                    moduleVariableResources_[*cell] = declaration.unit->module->name() + ":" + name;
                }
            }
        }
    } catch (...) {
        declaration.state = BuildState::Failed;
        for (const std::string &name : declaration.names) {
            if (!nodeScope_->has(name, false)) {
                poisonedNames_[nodeScope_].insert(name);
            }
        }
        claimRootNodes(firstNode);
        buildStack_.pop_back();
        throw;
    }
    claimRootNodes(firstNode);
    buildStack_.pop_back();
}

void Builder::ensureFunctionBuilt(const compile_graph_ptr_t &graph) {
    const auto known = topLevelFunctions_.find(graph.get());
    if (known == topLevelFunctions_.end()) {
        return; // not a function of a module's top level: built where it is written
    }
    TopLevelDeclaration &declaration = *known->second;
    switch (declaration.state) {
    case BuildState::Built:
    case BuildState::Failed:
        return;
    case BuildState::Building: {
        // A recursive use, fine in itself; but a value that started to be built since the
        // function did is needed by the function's body and needs the function: a cycle.
        const auto at = std::find(buildStack_.begin(), buildStack_.end(), &declaration);
        for (auto it = at; it != buildStack_.end(); ++it) {
            if ((*it)->graph == nullptr) {
                reportValueCycle(declaration);
            }
        }
        return;
    }
    case BuildState::Declared:
        break;
    }
    declaration.state = BuildState::Building;
    buildStack_.push_back(&declaration);
    {
        UnitContext context(*this, *declaration.unit);
        AttrScope attrs(currentAttrs_, camel::core::attr::kNoAttrs);
        namespacePath_ = namespacesOf(declaration);
        UsingScope usings(*this, declaration.unit->tree->usingsOf(declaration.gct));
        try {
            (void)visitFuncNode(declaration.gct);
            declaration.state = BuildState::Built;
        } catch (const BuildAbortException &) {
            declaration.state = BuildState::Failed;
        } catch (Diagnostic &d) {
            diags_->add(std::move(d));
            declaration.state = BuildState::Failed;
        }
    }
    buildStack_.pop_back();
}

bool Builder::buildTopLevelValueNamed(const std::string &name) {
    if (!module_) {
        return false;
    }
    const auto unit = units_.find(module_.get());
    if (unit == units_.end()) {
        return false;
    }
    const auto value = unit->second->values.find(name);
    if (value == unit->second->values.end() || value->second->state == BuildState::Built) {
        return false;
    }
    buildTopLevelValue(*value->second);
    return true;
}

node_handle_t Builder::exportedValueNode(ExportedValue &value) {
    if (value.node == nullptr) {
        auto &declaration = *static_cast<TopLevelDeclaration *>(value.declaration);
        buildTopLevelValue(declaration);
        const auto node = declaration.unit->nodeScope->get(value.name, false);
        if (!node.has_value()) {
            throw BuildAbortException();
        }
        value.node = *node;
    }
    return value.node;
}

void Builder::buildModuleBody(ModuleUnit &unit) {
    UnitContext context(*this, unit);
    // A dropped body is written and read within the module it is written in (see droppedBodies_):
    // the nodes of a finished module's tree are gone, and their addresses are handed back to the
    // next module's tree.
    droppedBodies_.clear();
    try {
        reportImportNameConflicts();
    } catch (const BuildAbortException &) {
        unit.failed = true;
        return;
    }
    for (const auto &declaration : unit.declarations) {
        try {
            if (declaration->graph != nullptr) {
                ensureFunctionBuilt(declaration->graph);
            } else {
                buildTopLevelValue(*declaration);
            }
        } catch (const BuildAbortException &) {
            // Reported where it was raised; the next declaration is still built.
        } catch (Diagnostic &d) {
            diags_->add(std::move(d));
        }
    }
    if (unit.exportList) {
        try {
            exportDeclarations(unit.exportList, true);
        } catch (const BuildAbortException &) {
        }
    }
    // Every exported value is built by now; its entity says which node it is (the language
    // service reads its type there).
    for (const auto &value : unit.exportedValues) {
        if (value->node == nullptr) {
            if (const auto node = unit.nodeScope->get(value->name, false); node.has_value()) {
                value->node = *node;
            }
        }
    }
}

// ---- Steps two and three for the program ----

bool Builder::buildProgram() {
    ensureRootGraph();
    const auto entryIt = units_.find(context_ ? context_->mainModule().get() : nullptr);
    if (entryIt == units_.end()) {
        return false;
    }
    const bool built = buildFrom(*entryIt->second, true);
    // The reading of what the guards of a `match type` cost: how many predicates ran, how many
    // were answered from the cache by (function, type arguments), and how many pairs that is.
    logPredicateCounts();
    return built;
}

bool Builder::buildLibrary(const module_ptr_t &module) {
    ensureRootGraph();
    const auto it = units_.find(module.get());
    if (it == units_.end()) {
        return false;
    }
    const bool built = buildFrom(*it->second, false);
    logPredicateCounts();
    return built;
}

bool Builder::buildLibraryEntry(
    const module_ptr_t &module, const std::string &entryFunction, FunctionType *expected) {
    ensureRootGraph();
    const auto it = units_.find(module.get());
    if (it == units_.end()) {
        return false;
    }
    libraryEntryFunction_ = entryFunction;
    libraryEntryExpected_ = expected;
    struct ClearEntryFunction {
        std::string &name;
        FunctionType *&expected;
        ~ClearEntryFunction() {
            name.clear();
            expected = nullptr;
        }
    } clearEntryFunction{libraryEntryFunction_, libraryEntryExpected_};
    return buildFrom(*it->second, true);
}

size_t Builder::sourceOffsetOf(camel::source::origin_id_t origin) const {
    auto sourceContext = context_ != nullptr ? context_->sourceContext() : nullptr;
    if (sourceContext == nullptr || origin == camel::source::kInvalidOriginId) {
        return kNoSourceOffset;
    }
    const camel::source::OriginRecord *record = sourceContext->origin(origin);
    if (record == nullptr) {
        return kNoSourceOffset;
    }
    const camel::source::SourceSpan *span = sourceContext->span(record->primarySpan);
    if (span == nullptr) {
        return kNoSourceOffset;
    }
    return span->startOffset;
}

void Builder::scanProgramWriters() {
    programWriters_.clear();
    programValueNames_.clear();
    // The variable it writes is the base of its target: the variable itself (`v = e`) or a path
    // into its value (`v.f = e`, `v[i] = e`), which the GIR builder reads as a rebinding of the
    // variable (pathAssignmentOf). A write is a node of kind ASSIGN: the kind, not a group's
    // spelling, is what says so.
    // The variable a target names: the reference under the fields and indices the assignment
    // writes through.
    const auto baseOf = [](GCT::node_ptr_t target) -> GCT::node_ptr_t {
        GCT::node_ptr_t node = target;
        while (node != nullptr) {
            if ((node->type() == GCT::NodeKind::ACCS || node->type() == GCT::NodeKind::SACC) &&
                node->size() > 0) {
                node = node->at(0);
                continue;
            }
            // An index of a path is a call of `__idx__` over the value it indexes
            // (pathAssignmentOf).
            if (node->type() == GCT::NodeKind::LINK && node->size() == 3 &&
                node->at(0) != nullptr && node->at(0)->type() == GCT::NodeKind::DREF &&
                node->at(0)->name() == "__idx__") {
                node = node->at(1);
                continue;
            }
            break;
        }
        if (node != nullptr &&
            (node->type() == GCT::NodeKind::DREF || node->type() == GCT::NodeKind::NREF)) {
            return node;
        }
        return nullptr;
    };
    struct Pending {
        GCT::node_ptr_t node = nullptr;
        // The function the node is written in: the innermost FUNC, or null at a module's top
        // level. A lambda or a nested function is a function of its own, which is what the set
        // counts a writer as.
        const GCT::Node *function = nullptr;
    };
    // The names a function binds itself: its parameters. A function that writes a name it takes as
    // a parameter writes that parameter -- the argument the call handed it, or the local copy of an
    // ordinary one -- and not the variable the same spelling names in the body it is called from.
    // Counting it would make every call a possible write of it for a reason the program does not
    // have, so the set leaves it out: a `var` argument is a write this layer cannot see held by the
    // hand-over of a cell, which is the boundary the set already has (a write reaching a variable
    // only by passing it as a `var` argument is not in the scan).
    std::unordered_map<const GCT::Node *, std::set<std::string>> parametersOf;
    const auto parametersIn = [&](const GCT::Node *function) -> const std::set<std::string> & {
        const auto known = parametersOf.find(function);
        if (known != parametersOf.end()) {
            return known->second;
        }
        std::set<std::string> &names = parametersOf[function];
        if (function != nullptr && function->size() > 0 && function->at(0) != nullptr) {
            if (auto *type = function->at(0)->dataType(); type != nullptr) {
                if (const auto *signature = tt::as_ptr<FunctionType>(type); signature != nullptr) {
                    for (size_t index = 0; index < signature->argNamesCount(); ++index) {
                        names.emplace(signature->argNameAt(index));
                    }
                }
            }
        }
        return names;
    };
    std::vector<Pending> pending;
    for (const auto &[module, unit] : units_) {
        if (unit->tree != nullptr && unit->tree->root() != nullptr) {
            pending.push_back({unit->tree->root(), nullptr});
        }
    }
    // The names a function declares itself: `var x = e` is a `VARI` over the reference that names
    // the cell. A function that writes a name it declared writes its own cell -- nothing outside it
    // reaches that cell but the functions written inside it, which are writers of their own and
    // stay in the set. The declaring function's own write is left out: a call through a function
    // value cannot write a cell it was never handed.
    std::unordered_map<const GCT::Node *, std::set<std::string>> declaredOf;
    const auto declaredNamesOf = [](GCT::node_ptr_t vari, std::set<std::string> &names) {
        std::function<void(GCT::node_ptr_t, int)> collect = [&](GCT::node_ptr_t node, int depth) {
            if (node == nullptr || depth > 1) {
                return;
            }
            if (node->type() == GCT::NodeKind::NREF || node->type() == GCT::NodeKind::DREF) {
                names.insert(node->name());
                return;
            }
            if (node->type() == GCT::NodeKind::LINK || node->type() == GCT::NodeKind::EXEC) {
                for (size_t i = 0; i < node->size(); ++i) {
                    collect(node->at(i), depth + 1);
                }
            }
        };
        for (size_t i = 0; i < vari->size(); ++i) {
            collect(vari->at(i), 0);
        }
    };
    while (!pending.empty()) {
        const Pending current = pending.back();
        pending.pop_back();
        const GCT::node_ptr_t node = current.node;
        if (node == nullptr) {
            continue;
        }
        const GCT::Node *function = node->type() == GCT::NodeKind::FUNC ? node : current.function;
        if (node->type() == GCT::NodeKind::VARI) {
            declaredNamesOf(node, declaredOf[function]);
        }
        if (node->type() == GCT::NodeKind::ASSIGN) {
            if (const GCT::node_ptr_t variable = baseOf(node->assignmentTarget());
                variable != nullptr) {
                const std::set<std::string> &parameters = parametersIn(function);
                if (parameters.find(variable->name()) == parameters.end()) {
                    programWriters_[variable->name()].insert(function);
                }
            }
        }
        // A name read where a call is not made is a value the program wants: `let k = f` makes a
        // value of `f`, and `f(1)` calls it. A `LINK` (and a `WITH` or `TAPP` that binds one) puts
        // its callee first; every other node reads every child. The value is what a capture of a
        // closure is about (a function value holds the values its captures had when it was made),
        // so the two are told apart here, before any body is built.
        const bool calleeFirst = node->type() == GCT::NodeKind::LINK ||
                                 node->type() == GCT::NodeKind::WITH ||
                                 node->type() == GCT::NodeKind::TAPP;
        for (size_t i = 0; i < node->size(); ++i) {
            const GCT::node_ptr_t child = node->at(i);
            if (child != nullptr && !(calleeFirst && i == 0) &&
                (child->type() == GCT::NodeKind::DREF || child->type() == GCT::NodeKind::NREF)) {
                // The callee of a `LINK`, a `WITH` and a `TAPP` is the first child and is skipped
                // above: `f(1)` calls `f`, and a name read anywhere else is a value the program
                // wants. An assignment's target is a name this scan already counted as a write of
                // `programWriters_`, and being read as a value here changes nothing.
                programValueNames_.insert(child->name());
            }
            pending.push_back({child, function});
        }
    }
    // A function's write of a name it declared is dropped: the cell is the function's own, and only
    // the functions written inside it can reach it, which are writers in their own right.
    for (auto entry = programWriters_.begin(); entry != programWriters_.end();) {
        std::set<const GCT::Node *> &functions = entry->second;
        for (auto writer = functions.begin(); writer != functions.end();) {
            const GCT::Node *function = *writer;
            const auto declared       = declaredOf.find(function);
            if (function != nullptr && declared != declaredOf.end() &&
                declared->second.count(entry->first) != 0) {
                writer = functions.erase(writer);
            } else {
                ++writer;
            }
        }
        if (functions.empty()) {
            entry = programWriters_.erase(entry);
        } else {
            ++entry;
        }
    }
}

void Builder::scanClosureLastUses() {
    closureUseOf_.clear();
    // A closure the program binds to a name (`let g = () => x`) is used where the block that binds
    // it names `g`: design principles 3.5 (198) takes that name's last use for the closure's. Two
    // other shapes are left out on purpose, because this scan cannot end their stretch and a
    // stretch cut wide is a legal program refused: a closure written at a call's argument position
    // is used by that call, and one the program puts into another value is used to the end of the
    // scope.
    struct Binding {
        const GCT::Node *closure = nullptr;
        const GCT::Node *block   = nullptr;
        std::string name;
        size_t at = kNoSourceOffset;
    };
    // The names each block writes, with the last offset it writes them at. The block is the key and
    // not the function: a name bound again in a block inside it is another name, and the closure
    // the outer block bound is not used by it -- which is also why a use written in a nested block
    // is not counted for the binding outside it (the stretch is cut short, never long).
    std::unordered_map<const GCT::Node *, std::unordered_map<std::string, size_t>> usesOfBlock;
    std::vector<Binding> bindings;
    struct Pending {
        GCT::node_ptr_t node = nullptr;
        // The innermost block (an EXEC) the node stands in, and the innermost function around it.
        const GCT::Node *block    = nullptr;
        const GCT::Node *function = nullptr;
    };
    std::vector<Pending> pending;
    for (const auto &[module, unit] : units_) {
        if (unit->tree != nullptr && unit->tree->root() != nullptr) {
            pending.push_back({unit->tree->root(), nullptr, nullptr});
        }
    }
    // The closure a binding's value is, if it is one: `let g = () => ...` is the reference `g` over
    // the function node, and `var g = () => ...` puts that reference inside the cell's declaration.
    // A VARI carries no name of its own (gct/node.h), so the name is read from the reference it
    // declares -- reading one off the cell is not a question the node answers.
    struct ClosureBinding {
        const GCT::Node *closure = nullptr;
        std::string name;
    };
    const auto closureOf = [](const GCT::node_ptr_t &node) -> ClosureBinding {
        const auto functionValue = [](const GCT::node_ptr_t &value) -> const GCT::Node * {
            return value != nullptr && value->type() == GCT::NodeKind::FUNC ? value : nullptr;
        };
        if (node == nullptr) {
            return {};
        }
        if (node->type() == GCT::NodeKind::NREF) {
            for (size_t i = 0; i < node->size(); ++i) {
                if (const GCT::Node *found = functionValue(node->at(i)); found != nullptr) {
                    return {found, node->name()};
                }
            }
            return {};
        }
        if (node->type() == GCT::NodeKind::VARI) {
            for (size_t i = 0; i < node->size(); ++i) {
                const GCT::node_ptr_t declared = node->at(i);
                if (declared == nullptr || declared->type() != GCT::NodeKind::NREF) {
                    continue;
                }
                for (size_t k = 0; k < declared->size(); ++k) {
                    if (const GCT::Node *found = functionValue(declared->at(k)); found != nullptr) {
                        return {found, declared->name()};
                    }
                }
            }
        }
        return {};
    };
    while (!pending.empty()) {
        const Pending current = pending.back();
        pending.pop_back();
        const GCT::node_ptr_t node = current.node;
        if (node == nullptr) {
            continue;
        }
        const GCT::Node *block    = current.block;
        const GCT::Node *function = current.function;
        if (node->type() == GCT::NodeKind::FUNC) {
            // A function's own body binds and uses its own names: nothing of it is this block's.
            function = node;
            block    = nullptr;
        } else if (node->type() == GCT::NodeKind::EXEC) {
            block = node;
        }
        // A binding in this block whose value is a closure.
        if (node->type() == GCT::NodeKind::NREF || node->type() == GCT::NodeKind::VARI) {
            if (const ClosureBinding binding = closureOf(node); binding.closure != nullptr) {
                bindings.push_back(
                    {binding.closure, block, binding.name, sourceOffsetOf(node->origin())});
            }
        }
        // A use of a name in this block: a reference the program writes (`g()`, `g`, `g.f`). A cell
        // carries no name, so only the two reference kinds are read here.
        if ((node->type() == GCT::NodeKind::NREF || node->type() == GCT::NodeKind::DREF) &&
            !node->name().empty() && block != nullptr) {
            // The binding's own reference is not a use of it: the closure is made there.
            const bool bindingHere =
                node->type() == GCT::NodeKind::NREF && closureOf(node).closure != nullptr;
            if (!bindingHere) {
                const size_t at = sourceOffsetOf(node->origin());
                if (at != kNoSourceOffset) {
                    size_t &last = usesOfBlock[block][node->name()];
                    if (last < at) {
                        last = at;
                    }
                }
            }
        }
        for (size_t i = 0; i < node->size(); ++i) {
            pending.push_back({node->at(i), block, function});
        }
    }
    for (const Binding &binding : bindings) {
        if (binding.closure == nullptr) {
            continue;
        }
        size_t lastUse = binding.at;
        if (binding.block != nullptr) {
            const auto uses = usesOfBlock.find(binding.block);
            if (uses != usesOfBlock.end()) {
                const auto named = uses->second.find(binding.name);
                if (named != uses->second.end()) {
                    lastUse = named->second;
                }
            }
        }
        if (binding.at != kNoSourceOffset && lastUse > binding.at) {
            closureUseOf_[binding.closure] = {lastUse, false, binding.name};
        } else if (binding.at != kNoSourceOffset) {
            // The block never names the closure again after the binding. Two shapes reach here and
            // the graph does not tell them apart: a closure the program put into another value (a
            // tuple, a struct, an array, a `Ref`, a return) -- whose construction may have dropped
            // the value from the datum it built, so no reference to the closure is left to walk --
            // and one the program never used at all. Design principles 3.5 (198) gives the first
            // shape an end: "the closure's value put into another value runs to the rest of the
            // scope". A stretch cut that far covers every write after the making, which is what the
            // report reads; the shape the design keeps legal, a name assigned after the closure is
            // done with, still shows as a use after the write and keeps its own offset above.
            //
            // Known over-wide, and not resolved here. The second shape -- a closure bound to a name
            // that the block never uses again, with no placement behind it -- has no stretch under
            // 3.5 and is legal, and this branch reports a write after it anyway:
            //
            //     let h = (): int => c     // never used
            //     c = 41                   // reported, though the stretch is empty
            //
            // The two shapes are not separable in the graph: making the closure's value part of
            // another value folds it out of the construction that built it, so the walk meets the
            // closure node exactly once (its own binding) and no node carries the name. The way to
            // narrow it is to read the placement off the source -- the tuple, struct, array, `Ref`
            // or return the value is put into -- and not to keep counting names in the graph. The
            // measured pair that makes this visible: `let h = (): int => c; let first = h(); c = c
            // + 1` prints 0 and is legal (the last use is the call), while the shape above is
            // refused.
            closureUseOf_[binding.closure] = {binding.at, true, binding.name};
        }
    }
}

bool Builder::buildFrom(ModuleUnit &entry, bool callEntry) {
    // From here on this builder answers the `match type` cases whose guards decide them: the type
    // layer asks for a predicate's answer while it selects a case, and only a build can read one
    // (typevar.h `typeCaseGuardEvaluator`, design principles 5.5).
    GuardScope guardScope(*this);
    // And it records the pairs of function types one type variable is asked for, judged once every
    // body is measured (resource_conflict.h).
    ResourceConflictScope conflictScope(*this);

    // The conservative writer set, made before any body is built (design principles 3.1, 3.5):
    // which variables the program writes is in the construction trees, and a function whose module
    // has not been built yet still has its body there. A call through a function value, whose
    // callee this layer does not know, is a possible write of those variables (visitLinkNode).
    scanProgramWriters();
    // And the last use of every closure the program binds to a name, from the same trees and for
    // the same reason: design principles 3.5 (198) makes a name unassignable from the making of a
    // closure to that use, and the end of that stretch is in the source, not in a body.
    scanClosureLastUses();

    // The modules in the order of their imports: a module after those it imports, except
    // around a cycle of imports, where one of them comes first (its uses of the others build
    // what it needs of them on demand).
    std::vector<ModuleUnit *> order;
    std::unordered_set<ModuleUnit *> seen;
    std::function<void(ModuleUnit &)> visitUnit = [&](ModuleUnit &unit) {
        if (!seen.insert(&unit).second) {
            return;
        }
        for (const auto &site : unit.module->importSites()) {
            if (auto it = units_.find(site.mod.get()); it != units_.end()) {
                visitUnit(*it->second);
            }
        }
        order.push_back(&unit);
    };
    visitUnit(entry);

    const auto inError = [](const ModuleUnit *unit) {
        return unit->failed || (unit->diags && unit->diags->hasErrors());
    };
    for (ModuleUnit *unit : order) {
        // A module that imports a module in error is not built: its errors would only follow
        // from the other one's. It is told so at the import, as when the imported module
        // could not be loaded.
        bool blocked = false;
        for (const auto &site : unit->module->importSites()) {
            const auto imported = units_.find(site.mod.get());
            if (imported == units_.end() || imported->second.get() == unit ||
                !inError(imported->second.get())) {
                continue;
            }
            auto diag = unit->diags->of(SemanticDiag::ImportedModuleHasErrors);
            if (site.origin != camel::source::kInvalidOriginId) {
                diag.atOrigin(site.origin);
            } else {
                diag.at(TokenRange{site.tokenStart, site.tokenEnd});
            }
            diag.commit(site.mod->name(), site.mod->path());
            blocked = true;
        }
        if (blocked) {
            unit->failed = true;
            continue;
        }
        try {
            buildModuleBody(*unit);
        } catch (const camel::core::mm::MemoryExhausted &e) {
            // A memory space is at its limit: reported like any other limit the program runs
            // into, at the function that was being compiled (buildFuncBody says which).
            camel::core::error::setPipelineStage("compile");
            auto diag = unit->diags->of(RuntimeDiag::MemoryExhausted);
            if (e.origin() != camel::source::kInvalidOriginId) {
                diag.atOrigin(e.origin());
            }
            diag.commit(std::string(), std::string(e.what()));
            camel::core::error::setPipelineStage("");
        }
        unit->failed = inError(unit);
        // A ruleset is a module of rules, and a transform finds a rule for a primitive by the head
        // of the rule's left side. The heads were read while the module's declarations were read
        // (a rule's `__lhs` has no body at that stage, so nothing here could read them); they are
        // indexed here, where the module and its diagnostics are in hand: one index per ruleset,
        // taken once, and a second rule under one head is refused rather than chosen by order.
        if (!unit->failed && unit->module && unit->module->name().rfind("rulesets.", 0) == 0) {
            const std::string ruleset = unit->module->name();
            std::vector<camel::parse::ast::RuleHead> rules =
                camel::parse::ast::takeRuleHeads(ruleset);
            if (rules.empty()) {
                rules = camel::parse::ast::takeRuleHeads(unit->diags->moduleName());
            }
            // The key is the operator and how many operands it was given, not the token alone:
            // `-x` and `x - w` are two primitives (`__neg__` and `__sub__`) that write the same
            // token, so a key without the count files two rules as one and refuses the second.
            //
            // Filing is the same act as refusing a duplicate: `indexRule` answers the rule already
            // under that key when it is a different one, so the index cannot hold two rules for one
            // primitive and the table the transform will ask is the table built here -- one place,
            // not a table built to be thrown away.
            for (const auto &rule : rules) {
                if (!rule.shape.empty()) {
                    auto diag = unit->diags->of(SemanticDiag::RuleLeftIsNotAnOperator);
                    diag.at(TokenRange{rule.tokenStart, rule.tokenEnd});
                    diag.commit(rule.rule, rule.shape);
                    continue;
                }
                // A family whose transform reads a rule's callee name and calls that function with
                // the batch never calls the rule's right side itself, so nothing binds the names
                // its left side's pattern writes. A rule that reads one of them is a rule no such
                // transform can use -- and the place to say so is here, where the rule is written,
                // not the call that would have used it:
                // the outer arrow is not called, so nothing may depend on it.
                if (familyCallsOnlyItsCallee(ruleset) && !rule.outerUses.empty()) {
                    auto diag = unit->diags->of(SemanticDiag::RuleRightUsesOuterVariable);
                    diag.at(TokenRange{rule.tokenStart, rule.tokenEnd});
                    diag.commit(rule.rule, rule.outerUses);
                    continue;
                }
                const std::string first = camel::parse::ast::indexRule(ruleset, rule);
                if (!first.empty()) {
                    auto diag = unit->diags->of(SemanticDiag::DuplicateRuleForOperator);
                    diag.at(TokenRange{rule.tokenStart, rule.tokenEnd});
                    diag.commit(first, rule.rule, rule.head, std::to_string(rule.arity));
                    continue;
                }
            }
            unit->failed = inError(unit);
        }
    }
    bool ok = std::none_of(order.begin(), order.end(), inError);

    // Step three: link.
    if (ok) {
        UnitContext context(*this, entry);
        refOrigin_             = camel::source::kInvalidOriginId;
        const size_t firstNode = rootGraph_->view().nodeSlotCount();
        try {
            // Every direct call gets the captures of its callee; then the entry call, made
            // once `main` has all of its captures (its value holds them); then the captures
            // that call needed.
            linkCaptureSites();
            if (callEntry) {
                buildEntryCall();
                linkCaptureSites();
            }
        } catch (const BuildAbortException &) {
            ok = false;
        } catch (Diagnostic &d) {
            diags_->add(std::move(d));
            ok = false;
        }
        claimRootNodes(firstNode);
    }
    if (ok) {
        // A declaration's clause is a promise; the bodies measure what it actually touches, and
        // the declaration is held to the promise (design effects plan, step 5). The measurement
        // runs here, while the bodies are still in hand; the report runs per module below, with
        // the other diagnostics of the module being reported.
        //
        // It runs before the two walks below because
        // a region's use of its writes is an edge laid at build time from every node
        // whose *inferred effect* writes (a call, a loop, a branch and an operator alike), and the
        // effect of a call is the measured signature of its callee, so the measurements have to
        // exist before the edges are laid. The measurement reads no edge of this kind --
        // `footprintOf` walks bodies and callee signatures -- so the two may stand in this order.
        measureInferredResources();
        // The edges themselves, once: what a write owes its region is not derived by each
        // walk that asks what runs (the warning's, the liveness of a graph, the encode, the
        // execution order) -- they follow these edges.
        layRegionWriteEdges();
        settleEffectfulGraphs();
        // A stream is read once on each execution path (design principles 5.4), which is a property
        // of the built graphs: this runs while every body is in hand and every read is a data edge
        // of the graph it was written in, before the graphs are encoded.
        settleStreamLinearity();
        // The effects that reach one resource are ordered by the order they are written in
        // (design principles 3.3). This runs on the settled measurements, before anything reads
        // the graphs as a schedule: an order edge is not a use, so it neither runs an effect nor
        // changes what a body reaches.
        orderEffectsOnOneResource();
        // Before the graphs are encoded: the reads whose value cannot be kept leave the mark
        // that lets `acc = acc->append<v>` append in place (design principles 11.3).
        markNonRetainingReads();
        for (ModuleUnit *unit : order) {
            UnitContext context(*this, *unit);
            try {
                // A stream is read once on each execution path (design principles 5.4): the check
                // measured the built graphs above, and what it found is named here, with the rest.
                reportStreamLinearity();
                reportIgnoredCapturedWrites();
                // Before the bodies are let go by reportDroppedEffectsOfBodies.
                reportUnusedFallibleValues();
                reportEffectfulConditions();
                // Before the bodies are let go, like reportDroppedEffectsOfBodies below.
                checkInferredResources();
                // The content rule of `Ref<T>`: a reference
                // may hold any type but one that holds a reference, because the boxes are
                // reclaimed by counting the handles that point at them. Also before the bodies are
                // let go.
                checkRefContents();
                reportDroppedEffectsOfBodies();
                if (unit->entry) {
                    // The top-level declarations run only as far as the call of `main`
                    // depends on them.
                    reportDroppedEffects(rootGraph_);
                }
            } catch (const BuildAbortException &) {
            } catch (Diagnostic &d) {
                diags_->add(std::move(d));
            }
        }
        ok = std::none_of(order.begin(), order.end(), inError);
    }

    // The functions of an imported module are part of the program as far as something calls
    // them or makes their value, like any function the program does not use: they are not
    // the root graph's own functions, which every reachability walk (encoding, the passes)
    // starts from.
    //
    // The semantic body of an `#impl` function is the exception (design principles 10.4): it is
    // the meaning of the function even when the only call is a direct one, which runs the kernel
    // operator and names no graph. An entry module keeps its bodies because they are its own
    // subgraphs; an imported module's body would be erased here and, being unreachable from the
    // root, never encoded, so Program::implBodies (and every consumer that expands on demand)
    // would silently lose it. Keep it: it stays a nested reference, which Program::traceGraphs
    // skips for an ordinary dump or pass, and Program::implBodies still lists it.
    std::vector<compile_graph_ptr_t> imported;
    for (const compile_graph_ptr_t &graph : rootGraph_->subGraphs()) {
        const auto owner        = rootSubGraphOwners_.find(graph.get());
        const bool semanticBody = (graph->view().flags() & camel::runtime::kGraphFlagImplBody) != 0;
        if (owner != rootSubGraphOwners_.end() && owner->second != nullptr &&
            !owner->second->entry && !semanticBody) {
            imported.push_back(graph);
        }
    }
    for (const compile_graph_ptr_t &graph : imported) {
        rootGraph_->eraseSubGraph(graph);
    }

    if (ok) {
        for (ModuleUnit *unit : order) {
            if (auto userModule = std::dynamic_pointer_cast<UserDefinedModule>(unit->module)) {
                userModule->setProgramGraph(rootGraph_);
            }
        }
    }
    return ok;
}

} // namespace camel::compile::gir
