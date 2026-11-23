// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The loader of modules written in Camel: the compiler's half of the dependency the kernel
 * inverted (`camel/core/module/source_loader.h`).
 *
 * The kernel owns the module object (`UserDefinedModule`) and the stage machine that loads it;
 * this file owns what reading the source and compiling it means -- the parse tree, the graph
 * construction tree, the graph a program built -- and it lives here, beside the compiler, because
 * the kernel does not read source and does not compile (design principles 9: a pass sees graphs,
 * and what it is handed is a graph).
 *
 * `SourceModuleFrontEnd` is the one object the kernel reaches through its loader slot; it is
 * registered when the library loads, from the bottom of this file. Its state per module is
 * `UserDefinedModuleFrontEnd`, held by the module as an opaque handle.
 *
 * The steps, in order: read the source (`readSource`), build the construction tree
 * (`buildConstructionTree`), declare the module into its program (`declare`), build and link that
 * program (`buildProgram`).
 */

#include <sstream>

#include "camel/utils/windows_parser_guard.h"

#include "camel/core/module/source_loader.h"
#include "camel/core/module/userdef.h"
#include "core/module/entity_internal.h"
#include "core/module/userdef_internal.h"

#include "camel/compile/gct.h"
#include "camel/compile/gir/draft_graph_builder.h"
#include "camel/compile/gir/encode.h"
#include "camel/core/debug_breakpoint.h"
#include "camel/core/type/composite/func.h"
#include "camel/parse/parse.h"
#include "camel/utils/type.h"
#include "src/compile/src/gct/builder.h"
#include "src/compile/src/gir/builder.h"

#include "camel/core/error/diagnostics.h"

#include "camel/utils/log.h"
#include "camel/utils/path.h"
#include "camel/utils/phase_timer.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <memory>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

using namespace std;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::error;
using namespace camel::parse;

namespace camel::core::module::detail {

/// What the compiler keeps of one module between the steps of loading it: the reader of its
/// source, the tree it is built from, and the graphs made out of it. Declared opaquely in
/// userdef.h and held by the module as a `std::shared_ptr<void>`, so the kernel names none of
/// these types.
struct UserDefinedModuleFrontEnd {
    /// The reader of the module's source: its abstract syntax tree, its concrete tree and the
    /// positions of its tokens. Null until the source is read, and for a module that has none.
    camel::parse::parser_ptr_t parser;
    /// The graph construction tree of the module. It is kept for as long as the module: generic
    /// functions are lowered again from it, also by modules that import this one.
    std::unique_ptr<GCT::Tree> tree;
    /// compileAsLibrary(): the graph builder the module was built by, kept with it. It is the
    /// program's builder (Context::graphBuildSession), shared rather than owned: see
    /// buildLibrary.
    std::shared_ptr<void> libraryBuilder;
    /// compileAsProgram(): the graph builder of each program of its own, one per entry function,
    /// kept with the module for as long as the graphs it made are used.
    std::vector<std::shared_ptr<void>> programBuilders;
};

namespace {

/// The front end of `module`, made by this loader. Null when no loader ever attached one, which
/// is the case of every module built before the loader ran and of a build with none.
UserDefinedModuleFrontEnd *frontEndOf(const UserDefinedModule &module) {
    return static_cast<UserDefinedModuleFrontEnd *>(
        UserDefinedModuleAccess::frontEnd(module).get());
}

/// Number of nodes of an AST, for the phase report.
template <typename NodePtr> uint64_t countNodes(const NodePtr &root) {
    uint64_t count = 0;
    if (root) {
        root->traverse([&](const auto &) { ++count; });
    }
    return count;
}

/// The module as the graph builder takes it: the same object, and the builder's own signature
/// names the kernel's pointer type.
module_ptr_t asModule(UserDefinedModule &module) { return module.shared_from_this(); }

} // namespace

/// The loader the kernel reaches: one object, registered at load time. Everything it does is a
/// step of loading a module written in Camel, and the module's own state -- the stage, the
/// diagnostics, the graph -- stays the kernel's.
class SourceModuleFrontEnd final : public SourceModuleLoader {
  public:
    SourceModuleFrontEnd() { registerSourceModuleLoader(this); }

    Attached attach(UserDefinedModule &module, const std::shared_ptr<void> &source) override {
        auto frontEnd = std::make_shared<UserDefinedModuleFrontEnd>();
        Attached attached;
        attached.frontEnd = frontEnd;
        if (const auto reader = std::static_pointer_cast<CamelParser>(source)) {
            frontEnd->parser     = reader;
            attached.diagnostics = reader->diagnostics();
            reader->setSourceContext(module.context()->sourceContext());
        } else {
            attached.diagnostics = std::make_shared<Diagnostics>(
                module.name(),
                module.path().empty()
                    ? "<in-memory>"
                    : camel::utils::pathToUtf8(
                          fs::absolute(camel::utils::pathFromUtf8(module.path()))),
                module.context()->sourceContext());
            attached.diagnostics->setConfig(module.context()->diagConfig());
            frontEnd->parser = std::make_shared<CamelParser>(attached.diagnostics);
            frontEnd->parser->setSourceContext(module.context()->sourceContext());
        }
        return attached;
    }

    bool readSource(UserDefinedModule &module) override {
        UserDefinedModuleFrontEnd *frontEnd = frontEndOf(module);
        if (frontEnd == nullptr) {
            return false;
        }
        const diagnostics_ptr_t &diagnostics = module.diagnostics();
        if (!frontEnd->parser->ast()) {
            camel::utils::phase::Scope phaseScope("parse", module.name());
            const auto override = module.context()->sourceOverride(module.path());
            std::istringstream buffer(override.value_or(""));
            std::ifstream ifs;
            if (!override)
                ifs.open(camel::utils::pathFromUtf8(module.path()));
            if (!override && !ifs.good()) {
                throw DiagnosticBuilder::of(SemanticDiag::ModuleNotFound)
                    .commit(module.name(), "module file not found or unreadable: " + module.path());
            }
            if (!frontEnd->parser->parse(
                    override ? static_cast<std::istream &>(buffer)
                             : static_cast<std::istream &>(ifs))) {
                throw DiagnosticBuilder::of(SemanticDiag::ModuleParseFailed).commit(module.path());
            }
        }
        return !diagnostics->hasErrors();
    }

    bool buildConstructionTree(UserDefinedModule &module) override {
        UserDefinedModuleFrontEnd *frontEnd = frontEndOf(module);
        if (frontEnd == nullptr) {
            return false;
        }
        const diagnostics_ptr_t &diagnostics = module.diagnostics();
        auto ast                             = frontEnd->parser->ast();
        {
            camel::utils::phase::Scope phaseScope("gct", module.name());
            auto gctBuilder    = GCT::Builder(module.context(), asModule(module));
            frontEnd->tree     = gctBuilder.build(ast, diagnostics);
        }
        if (camel::utils::phase::enabled()) {
            // Counting walks both trees: a phase of its own, so that the cost of measuring is not
            // read as a cost of compiling.
            camel::utils::phase::Scope phaseScope("(counting)");
            camel::utils::phase::count("modules", 1);
            camel::utils::phase::count("ast.nodes", countNodes(ast));
            camel::utils::phase::count("gct.nodes", frontEnd->tree ? frontEnd->tree->nodeCount() : 0);
        }
        EXEC_WHEN_DEBUG(
            { camel::DebugBreakpoint::Hit("GCT", frontEnd->tree ? frontEnd->tree->root() : nullptr); });
        return frontEnd->tree != nullptr && !diagnostics->hasErrors();
    }

    bool declare(UserDefinedModule &module) override {
        UserDefinedModuleFrontEnd *frontEnd = frontEndOf(module);
        if (frontEnd == nullptr || !frontEnd->tree) {
            return false;
        }
        // No phase of its own: the kernel opens `declare` around this call where the loading of a
        // module measures it, and `library` where a library declares its imports, so the phase
        // report keeps saying what it said before the compiler became a step behind a loader.
        camel::compile::gir::Builder::of(module.context())
            .declareModule(asModule(module), *frontEnd->tree, module.diagnostics());
        return !module.diagnostics()->hasErrors();
    }

    bool buildProgram(UserDefinedModule &module) override {
        camel::utils::phase::Scope phaseScope("gir", module.name());
        const bool ok = camel::compile::gir::Builder::of(module.context()).buildProgram();
        return ok && !module.diagnostics()->hasErrors();
    }

    bool buildLibrary(UserDefinedModule &module) override {
        UserDefinedModuleFrontEnd *frontEnd = frontEndOf(module);
        if (frontEnd == nullptr) {
            return false;
        }
        // The `library` phase is the kernel's: it opens around the declaration and this build
        // together.
        // The module is built by the graph builder of its program, not one of its own: the
        // modules written in Camel it imports are declared there already, by compileImportedModules
        // when this module's construction tree was built (each of them had to be declared before a
        // body could name what it exports). Building the library on the same builder keeps the
        // graphs those modules export the ones their importer names, and the whole import closure
        // is in one place, so buildLibrary builds the imported bodies too.
        auto &builder = camel::compile::gir::Builder::of(module.context());
        if (!builder.buildLibrary(asModule(module)) || module.diagnostics()->hasErrors()) {
            return false;
        }
        // buildLibrary set the program graph (the kernel's stage is Done now); the builder owns
        // what the graphs of the module refer to and lives as long as the module.
        frontEnd->libraryBuilder = module.context()->graphBuildSession();
        return true;
    }

    UserDefinedModule::ProgramStatus buildProgramOfItsOwn(
        UserDefinedModule &module, const std::string &entryFunction, std::string &problem,
        std::shared_ptr<void> &root, camel::core::type::FunctionType *expected) override {
        UserDefinedModuleFrontEnd *frontEnd = frontEndOf(module);
        if (frontEnd == nullptr) {
            return UserDefinedModule::ProgramStatus::Failed;
        }
        using Status = UserDefinedModule::ProgramStatus;
        struct BuildingProgramOfItsOwn {
            UserDefinedModule &module;
            explicit BuildingProgramOfItsOwn(UserDefinedModule &m) : module(m) {
                UserDefinedModuleAccess::setBuildingAProgramOfItsOwn(module, true);
            }
            ~BuildingProgramOfItsOwn() {
                UserDefinedModuleAccess::setBuildingAProgramOfItsOwn(module, false);
            }
        } buildingProgramOfItsOwn(module);
        camel::utils::phase::Scope phaseScope("library", module.name());
        auto builder = std::make_shared<camel::compile::gir::Builder>(module.context());
        // A module written in Camel may import a library written in Camel (item 1): the pass
        // module is a program of its own, so its builder needs those imports declared and built
        // too. They are declared here, in import order, and built by buildFrom before this
        // module. Their graphs in this program are separate from the graphs the same module has
        // in the program that loaded it; the builder keeps them, and the export table of the
        // imported module is filled by this declaration, as it is whenever a library is compiled.
        std::vector<std::shared_ptr<UserDefinedModule>> imported;
        std::unordered_set<const UserDefinedModule *> seen;
        std::function<bool(const std::shared_ptr<UserDefinedModule> &)> collectImports =
            [&](const std::shared_ptr<UserDefinedModule> &mod) -> bool {
            if (mod.get() == &module || !seen.insert(mod.get()).second) {
                return true;
            }
            if (!compileForLoad(mod, CompileStage::GCT)) {
                return false;
            }
            for (const auto &site : mod->importSites()) {
                if (auto other = std::dynamic_pointer_cast<UserDefinedModule>(site.mod)) {
                    if (!collectImports(other)) {
                        return false;
                    }
                }
            }
            imported.push_back(mod);
            return true;
        };
        for (const auto &site : module.importSites()) {
            if (auto other = std::dynamic_pointer_cast<UserDefinedModule>(site.mod)) {
                if (!collectImports(other)) {
                    return Status::Failed;
                }
            }
        }
        // The flag is only for this build: these modules may be built again as part of another
        // program, and then their root graph must be recorded again.
        auto restoreImported = std::unique_ptr<void, std::function<void(void *)>>(
            reinterpret_cast<void *>(1), [&imported](void *) {
                for (const auto &mod : imported) {
                    UserDefinedModuleAccess::setBuildingAProgramOfItsOwn(*mod, false);
                }
            });
        for (const auto &mod : imported) {
            // The module object may already export names for the program that loaded it. Those
            // names are that program's, and they stay there: this builder has its own export
            // table (Builder::exports), so declaring the module again records this program's
            // graphs (task).
            UserDefinedModuleFrontEnd *importedFrontEnd = frontEndOf(*mod);
            if (importedFrontEnd == nullptr || !importedFrontEnd->tree) {
                return Status::Failed;
            }
            builder->declareModule(mod, *importedFrontEnd->tree, mod->diagnostics());
            if (mod->diagnostics()->hasErrors()) {
                return Status::Failed;
            }
            // Its graphs belong to this program's builder, not to the program it was loaded as
            // part of: setProgramGraph must not replace the graph it had there.
            UserDefinedModuleAccess::setBuildingAProgramOfItsOwn(*mod, true);
        }
        if (!frontEnd->tree) {
            return Status::Failed;
        }
        builder->declareModule(asModule(module), *frontEnd->tree, module.diagnostics());
        if (module.diagnostics()->hasErrors()) {
            return Status::Failed;
        }
        // The export table is filled by the declaration: the function the program runs must be one
        // non-generic function of this module, exactly as registerCamelPass demands of a pass.
        const auto entity =
            builder->exports().exportedEntity(module, Reference(entryFunction));
        if (!entity) {
            // A function that is there but not exported is the mistake `module::name` cannot tell
            // from a name that is not there; only the construction tree can.
            problem = module.declaresFunction(entryFunction)
                          ? std::format(
                                "'{}' declares '{}' but does not export it", module.name(),
                                entryFunction)
                          : std::format("'{}' exports no '{}'", module.name(), entryFunction);
            return Status::NoSuchFunction;
        }
        using Access = camel::core::module::detail::EntityAccess;
        if (expected != nullptr) {
            // A pass over the function `expected` is the instance of a generic function (design
            // principles 9.1).
            if (!Access::isGenericTemplate(*entity)) {
                problem = std::format(
                    "'{}::{}' is not a generic function", module.name(), entryFunction);
                return Status::NotAFunction;
            }
        } else if (
            Access::isGenericTemplate(*entity) || !Access::isGraphSet(*entity) ||
            !Access::graphSet(*entity) || Access::graphSet(*entity)->size() != 1) {
            problem = std::format(
                "'{}::{}' is not one non-generic function", module.name(), entryFunction);
            return Status::NotAFunction;
        }
        if (!builder->buildLibraryEntry(asModule(module), entryFunction, expected) ||
            module.diagnostics()->hasErrors()) {
            if (!builder->libraryEntryProblem().empty()) {
                problem = builder->libraryEntryProblem();
            }
            return Status::Failed;
        }
        auto built = builder->rootGraph();
        if (built == nullptr) {
            return Status::Failed;
        }
        root = built;
        // The builder owns what the graphs of the program refer to and lives as long as the
        // module.
        frontEnd->programBuilders.push_back(builder);
        // The module is not marked built: this program's exports are this program's, and another
        // program that loads the module (the one being transformed, another pass) declares it into
        // its own export table (task). The front end (parse, GCT) is done, which is what the
        // module object shares with every program.
        return Status::Built;
    }

    std::vector<UserDefinedModule::ExportedFunction>
    exportedFunctions(const UserDefinedModule &module) const override {
        std::vector<UserDefinedModule::ExportedFunction> functions;
        UserDefinedModuleFrontEnd *frontEnd = frontEndOf(module);
        if (frontEnd == nullptr || !frontEnd->tree) {
            return functions;
        }
        const auto root = frontEnd->tree->root();
        if (root == nullptr) {
            return functions;
        }
        // The export list says what the module offers; the declarations say which of those names
        // are functions and with which type. Both are read from the construction tree: no graph is
        // built, so a module that is parsed answers this even when it does not compile.
        std::unordered_set<std::string> exported;
        for (size_t i = 0; i < root->size(); ++i) {
            const auto &child = root->at(i);
            if (child->type() != GCT::NodeKind::EXPT) {
                continue;
            }
            for (size_t name = 0; name < child->exportCount(); ++name) {
                exported.insert(child->exportName(name));
            }
        }
        for (size_t i = 0; i < root->size(); ++i) {
            const auto &child = root->at(i);
            if (child->type() != GCT::NodeKind::DECL || !child->isFunc() ||
                !exported.contains(child->name()) || child->at(0) == nullptr) {
                continue;
            }
            functions.push_back(UserDefinedModule::ExportedFunction{
                child->name(),
                tt::as_ptr<camel::core::type::FunctionType>(child->at(0)->dataType())});
        }
        return functions;
    }

    bool declaresFunction(
        const UserDefinedModule &module, const std::string &name) const override {
        UserDefinedModuleFrontEnd *frontEnd = frontEndOf(module);
        if (frontEnd == nullptr || !frontEnd->tree) {
            return false;
        }
        const auto root = frontEnd->tree->root();
        if (root == nullptr) {
            return false;
        }
        for (size_t i = 0; i < root->size(); ++i) {
            const auto &child = root->at(i);
            if (child->type() == GCT::NodeKind::DECL && child->isFunc() &&
                child->name() == name) {
                return true;
            }
        }
        return false;
    }

    const camel::core::error::TokenPositions *
    tokenPositions(const UserDefinedModule &module) const override {
        UserDefinedModuleFrontEnd *frontEnd = frontEndOf(module);
        if (frontEnd == nullptr || !frontEnd->parser) {
            return nullptr;
        }
        return &frontEnd->parser->tokenPositions();
    }

    void encodeProgram(
        const std::shared_ptr<void> &graph, UserDefinedModule &module,
        camel::runtime::Program &program, bool freshIds) override {
        const auto root =
            std::static_pointer_cast<camel::compile::gir::DraftGraphBuilder>(graph);
        camel::compile::gir::encodeToRuntimeProgram(
            root, program, freshIds ? &module.context()->graphIds() : nullptr);
    }

    std::shared_ptr<void> definitionGraph(
        const module_ptr_t &module, const Reference &name, size_t params) override {
        using Access = camel::core::module::detail::EntityAccess;
        const auto entity =
            module->context()->programExports().exportedEntity(*module, name);
        if (!entity) {
            return nullptr;
        }
        const auto graphs = Access::graphSet(*entity);
        if (!graphs) {
            return nullptr;
        }
        // The signature (design 9.2) says which overload the definition is: its operands are the
        // function's ordinary parameters, and a definition has no `with` parameter.
        for (const auto &candidate : *graphs) {
            const auto *type = candidate ? candidate->funcType() : nullptr;
            if (type != nullptr && type->normTypesCount() == params && type->withTypesCount() == 0) {
                return candidate;
            }
        }
        return nullptr;
    }
};

/// The registration, made while the program loads: every module written in Camel is loaded
/// through this object, and no kernel file names it.
SourceModuleFrontEnd theSourceModuleLoader;

const void *UserDefinedModuleAccess::constructionTree(const UserDefinedModule &module) {
    UserDefinedModuleFrontEnd *frontEnd = frontEndOf(module);
    return frontEnd == nullptr ? nullptr : frontEnd->tree.get();
}

} // namespace camel::core::module::detail

// ---- What the tools read of a module's front end ------------------------------------------

namespace camel::compile {

std::shared_ptr<camel::parse::CamelParser>
parserOf(const camel::core::module::UserDefinedModule &module) {
    const auto frontEnd = camel::core::module::detail::UserDefinedModuleAccess::frontEnd(module);
    const auto state =
        static_cast<camel::core::module::detail::UserDefinedModuleFrontEnd *>(frontEnd.get());
    return state == nullptr
               ? nullptr
               : std::static_pointer_cast<camel::parse::CamelParser>(state->parser);
}

const camel::compile::gct::Tree *
treeOf(const camel::core::module::UserDefinedModule &module) {
    return static_cast<const camel::compile::gct::Tree *>(
        camel::core::module::detail::UserDefinedModuleAccess::constructionTree(module));
}

} // namespace camel::compile