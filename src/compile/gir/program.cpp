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
 * The program-wide side of the GIR builder (design principles 2.11 "模块"; the graph model,
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
 * 3. Link: the captures of every direct call (linkCaptureSites, a least fixpoint), the call of
 *    the entry module's `main`, the summaries of effects (another fixpoint) and the warnings
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

#include <algorithm>
#include <cctype>
#include <format>
#include <functional>
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
          depth_(builder.instantiationDepth_), waited_(builder.waited_),
          synced_(builder.synced_), varied_(builder.varied_),
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
        builder.waited_               = false;
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
        self_.waited_               = waited_;
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
    bool waited_, synced_, varied_, declaresVariable_, calleeAllowed_;
    node_handle_t lastSynced_;
    camel::source::origin_id_t refOrigin_;
    std::vector<std::string> namespacePath_;
    std::vector<const std::vector<Reference> *> usingFrames_;
};

namespace {
/// The namespaces of a qualified name (`A::B` of `A::B::f`).
std::vector<std::string> namespacesOf(const std::string &name) {
    return Reference(name).paths();
}
} // namespace

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

} // namespace

std::string Builder::qualifiedName(const ModuleUnit &unit, const std::string &name) {
    return unit.entry ? name : std::format("{}::{}", unit.module->name(), name);
}

Builder &Builder::of(const context_ptr_t &context) {
    ASSERT(context != nullptr, "The graph builder of a program needs its context.");
    auto &session = context->graphBuildSession();
    if (!session) {
        session = std::make_shared<Builder>(context);
    }
    return *std::static_pointer_cast<Builder>(session);
}

void Builder::claimRootNodes(size_t firstNode) {
    const size_t count = rootGraph_->draft().nodeSlotCount();
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

void Builder::declareModule(
    const module_ptr_t &module, GCT::Tree &tree, diagnostics_ptr_t diags) {
    ensureRootGraph();
    recorder_ = context_ ? context_->semanticRecorder() : nullptr;
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
        // What step two builds, in the order it is written; and the export list.
        GCT::node_ptr_t expt = nullptr;
        for (size_t i = 0; i < root->size(); ++i) {
            const GCT::node_ptr_t &child = root->at(i);
            auto declaration  = std::make_unique<TopLevelDeclaration>();
            declaration->gct  = child;
            declaration->unit = &unit;
            if (child->type() == GCT::NodeKind::EXPT) {
                expt = child;
                continue;
            }
            if (child->type() == GCT::NodeKind::FUNC) {
                FunctionType *declared = tt::as_ptr<FunctionType>(child->at(0)->dataType());
                if (isGenericHere(declared)) {
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
                    // debugger), unless it is the entry module's (design principles 2.12).
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
    // A declaration of the top level: no decorator is around it (design principles 2.17).
    AttrScope attrs(currentAttrs_, camel::core::attr::kNoAttrs);
    // Built in the namespace it is declared in, with the `using namespace` of its blocks.
    namespacePath_ = namespacesOf(declaration);
    UsingScope usings(*this, declaration.unit->tree->usingsOf(declaration.gct));
    const size_t firstNode = rootGraph_->draft().nodeSlotCount();
    try {
        (void)visit(declaration.gct);
        declaration.state = BuildState::Built;
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
    return buildFrom(*entryIt->second, true);
}

bool Builder::buildLibrary(const module_ptr_t &module) {
    ensureRootGraph();
    const auto it = units_.find(module.get());
    if (it == units_.end()) {
        return false;
    }
    return buildFrom(*it->second, false);
}

bool Builder::buildFrom(ModuleUnit &entry, bool callEntry) {

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
    }
    bool ok = std::none_of(order.begin(), order.end(), inError);

    // Step three: link.
    if (ok) {
        UnitContext context(*this, entry);
        refOrigin_ = camel::source::kInvalidOriginId;
        const size_t firstNode = rootGraph_->draft().nodeSlotCount();
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
        settleEffectfulGraphs();
        // Before the graphs are encoded: the reads whose value cannot be kept leave the mark
        // that lets `acc = acc->append<v>` append in place (design principles 2.12).
        markNonRetainingReads();
        for (ModuleUnit *unit : order) {
            UnitContext context(*this, *unit);
            try {
                reportIgnoredCapturedWrites();
                reportUndeclaredCapturedWrites();
                // Before the bodies are let go by reportDroppedEffectsOfBodies.
                reportUnusedFallibleValues();
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
    std::vector<compile_graph_ptr_t> imported;
    for (const compile_graph_ptr_t &graph : rootGraph_->subGraphs()) {
        const auto owner = rootSubGraphOwners_.find(graph.get());
        if (owner != rootSubGraphOwners_.end() && owner->second != nullptr &&
            !owner->second->entry) {
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
