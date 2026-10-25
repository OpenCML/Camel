// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include <sstream>

#include "camel/utils/windows_parser_guard.h"

#include "camel/core/module/userdef.h"
#include "core/module/entity_internal.h"
#include "core/module/userdef_internal.h"

#include "camel/compile/gir/encode.h"
#include "camel/core/debug_breakpoint.h"
#include "camel/core/type/composite/func.h"
#include "camel/parse/parse.h"
#include "camel/utils/type.h"
#include "compile/gct/builder.h"
#include "compile/gir/builder.h"

#include "camel/core/error/diagnostics.h"

#include "camel/utils/log.h"
#include "camel/utils/path.h"
#include "camel/utils/phase_timer.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
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

namespace {

/// Number of nodes of an AST, for the phase report.
template <typename NodePtr> uint64_t countNodes(const NodePtr &root) {
    uint64_t count = 0;
    if (root) {
        root->traverse([&](const auto &) { ++count; });
    }
    return count;
}

} // namespace

UserDefinedModule::UserDefinedModule(
    const std::string &name, const std::string &path, context_ptr_t ctx, parser_ptr_t parser)
    : Module(name, path, ctx) {
    if (parser) {
        parser_      = parser;
        diagnostics_ = parser->diagnostics();
        parser_->setSourceContext(ctx->sourceContext());
    } else {
        diagnostics_ = std::make_shared<Diagnostics>(
            name,
            path.empty()
                ? "<in-memory>"
                : camel::utils::pathToUtf8(fs::absolute(camel::utils::pathFromUtf8(path))),
            ctx->sourceContext());
        diagnostics_->setConfig(ctx->diagConfig());
        parser_ = std::make_shared<CamelParser>(diagnostics_);
        parser_->setSourceContext(ctx->sourceContext());
    }
    // Automatically import the built-in module
    importAllRefsFromMod(ctx->importModule(""));
}

module_ptr_t
UserDefinedModule::fromFile(const std::string &name, const std::string &path, context_ptr_t ctx) {
    return std::make_shared<UserDefinedModule>(name, path, ctx);
}

const void *camel::core::module::detail::UserDefinedModuleAccess::compileGraphOpaque(
    const UserDefinedModule &module) {
    return camel::compile::gir::compileGraphOpaque(module.compileGraph_);
}

void camel::core::module::detail::UserDefinedModuleAccess::encodeRuntimeProgram(
    const UserDefinedModule &module, camel::runtime::Program &program) {
    camel::compile::gir::encodeToRuntimeProgram(module.compileGraph_, program);
}

bool UserDefinedModule::compile(CompileStage till) {
    if (till <= stage_) {
        return true;
    }
    if (compiling_) {
        // Asked for while it is being compiled: a module it imports imports it back (design
        // principles 10.1 allows that). It has what it has declared so far; the rest of it is
        // there before any function body of the program is built.
        return true;
    }
    struct Compiling {
        bool &flag;
        explicit Compiling(bool &f) : flag(f) { flag = true; }
        ~Compiling() { flag = false; }
    } compiling(compiling_);
    if (stage_ == CompileStage::None) {
        CAMEL_LOG_INFO_S("Module", "Start compiling module '{}' from file '{}'.", name_, path_);
    }

    if (stage_ < CompileStage::AST) {
        if (!parser_->ast()) {
            camel::utils::phase::Scope phaseScope("parse", name_);
            const auto override = context_->sourceOverride(path_);
            std::istringstream buffer(override.value_or(""));
            std::ifstream ifs;
            if (!override)
                ifs.open(camel::utils::pathFromUtf8(path_));
            if (!override && !ifs.good()) {
                throw DiagnosticBuilder::of(SemanticDiag::ModuleNotFound)
                    .commit(name_, "module file not found or unreadable: " + path_);
            }
            if (!parser_->parse(
                    override ? static_cast<std::istream &>(buffer)
                             : static_cast<std::istream &>(ifs))) {
                throw DiagnosticBuilder::of(SemanticDiag::ModuleParseFailed).commit(path_);
            }
        }
        if (diagnostics_->hasErrors()) {
            CAMEL_LOG_INFO_S("Module", "Module '{}' failed to parse", name_);
            return false;
        }
        stage_ = CompileStage::AST;
    }

    if (till > CompileStage::AST && stage_ < CompileStage::GCT) {
        auto ast = parser_->ast();
        {
            camel::utils::phase::Scope phaseScope("gct", name_);
            auto gctBuilder = GCT::Builder(context_, shared_from_this());
            gct_            = gctBuilder.build(ast, diagnostics_);
        }
        if (camel::utils::phase::enabled()) {
            // Counting walks both trees: a phase of its own, so that the cost of
            // measuring is not read as a cost of compiling.
            camel::utils::phase::Scope phaseScope("(counting)");
            camel::utils::phase::count("modules", 1);
            camel::utils::phase::count("ast.nodes", countNodes(ast));
            camel::utils::phase::count("gct.nodes", gct_ ? gct_->nodeCount() : 0);
        }
        EXEC_WHEN_DEBUG({ camel::DebugBreakpoint::Hit("GCT", gct_ ? gct_->root() : nullptr); });
        if (!gct_ || diagnostics_->hasErrors()) {
            CAMEL_LOG_INFO_S("Module", "Module '{}' failed to build GCT", name_);
            return false;
        }
        stage_ = CompileStage::GCT;
    }

    if (till > CompileStage::GCT && stage_ < CompileStage::Declared) {
        // The first of the three steps of loading (the graph model, 3.9.4): what the module
        // declares (its functions, its top-level values, its types) becomes known to the
        // program, and its exports readable, before any function body is built.
        camel::utils::phase::Scope phaseScope("declare", name_);
        camel::compile::gir::Builder::of(context_).declareModule(
            shared_from_this(),
            *gct_,
            diagnostics_);
        if (diagnostics_->hasErrors()) {
            CAMEL_LOG_INFO_S("Module", "Module '{}' failed to declare", name_);
            return false;
        }
        stage_ = CompileStage::Declared;
    }

    if (till > CompileStage::Declared && stage_ < CompileStage::Done) {
        if (context_->mainModule().get() != this) {
            // A module is built with the program it belongs to, when its entry module is.
            return true;
        }
        // The other two steps for the whole program: build, then link.
        camel::utils::phase::Scope phaseScope("gir", name_);
        if (!camel::compile::gir::Builder::of(context_).buildProgram() ||
            diagnostics_->hasErrors()) {
            CAMEL_LOG_INFO_S("Module", "Module '{}' failed to build GIR", name_);
            return false;
        }
    }
    CAMEL_LOG_INFO_S("Module", "Module '{}' built successfully.", name_);
    return true;
}

bool UserDefinedModule::compileAsLibrary() {
    if (stage_ == CompileStage::Done) {
        return true;
    }
    if (compiling_) {
        return false;
    }
    if (stage_ < CompileStage::GCT && !compile(CompileStage::GCT)) {
        return false;
    }
    struct Compiling {
        bool &flag;
        explicit Compiling(bool &f) : flag(f) { flag = true; }
        ~Compiling() { flag = false; }
    } compiling(compiling_);
    camel::utils::phase::Scope phaseScope("library", name_);
    // The module is built by the graph builder of its program, not one of its own: the modules
    // written in Camel it imports are declared there already, by compileImportedModules when
    // this module's construction tree was built (each of them had to be declared before a body
    // could name what it exports). Building the library on the same builder keeps the graphs
    // those modules export the ones their importer names, and the whole import closure is in
    // one place, so buildLibrary builds the imported bodies too.
    auto &builder = camel::compile::gir::Builder::of(context_);
    if (stage_ != CompileStage::Declared) {
        builder.declareModule(shared_from_this(), *gct_, diagnostics_);
        if (diagnostics_->hasErrors()) {
            return false;
        }
        stage_ = CompileStage::Declared;
    }
    if (!builder.buildLibrary(shared_from_this()) || diagnostics_->hasErrors()) {
        return false;
    }
    // buildLibrary set the program graph (stage Done); the builder owns what the graphs of
    // the module refer to and lives as long as the module.
    libraryBuilder_ = context_->graphBuildSession();
    return stage_ == CompileStage::Done;
}

void UserDefinedModule::setProgramGraph(
    const std::shared_ptr<camel::compile::gir::DraftGraphBuilder> &root) {
    if (buildingProgramOfItsOwn_) {
        // compileAsProgram() builds a program of its own from this module: its root graph is
        // kept with that program's builder, and the graph this module was loaded as part of
        // (the program that uses it, or the library of compileAsLibrary) stays as it was.
        //
        // The module is not marked built here. What it exports in this program is this
        // program's, and a module is declared once per program: the program being compiled
        // (or another tool program) that loads the module later must declare it into its own
        // export table (task). Marking it Done would skip that declaration and leave the
        // names of this program as the module's only ones.
        return;
    }
    compileGraph_ = root;
    stage_        = CompileStage::Done;
    loaded_       = true;
}

std::vector<UserDefinedModule::ExportedFunction> UserDefinedModule::exportedFunctions() const {
    std::vector<ExportedFunction> functions;
    if (!gct_) {
        return functions;
    }
    const auto root = gct_->root();
    if (root == nullptr) {
        return functions;
    }
    // The export list says what the module offers; the declarations say which of those names are
    // functions and with which type. Both are read from the construction tree: no graph is built,
    // so a module that is parsed answers this even when it does not compile.
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
        functions.push_back(ExportedFunction{
            child->name(),
            tt::as_ptr<camel::core::type::FunctionType>(child->at(0)->dataType())});
    }
    return functions;
}

bool UserDefinedModule::declaresFunction(const std::string &name) const {
    if (!gct_) {
        return false;
    }
    const auto root = gct_->root();
    if (root == nullptr) {
        return false;
    }
    for (size_t i = 0; i < root->size(); ++i) {
        const auto &child = root->at(i);
        if (child->type() == GCT::NodeKind::DECL && child->isFunc() && child->name() == name) {
            return true;
        }
    }
    return false;
}

UserDefinedModule::ProgramStatus UserDefinedModule::compileAsProgram(
    const std::string &entryFunction, std::string &problem,
    std::shared_ptr<camel::compile::gir::DraftGraphBuilder> &root) {
    if (compiling_) {
        return ProgramStatus::Failed;
    }
    // A module that does not parse throws its diagnostic out of compile(); the diagnostic is
    // kept with the module, and the caller turns a failed program into the reason a pass is
    // not available. A pass module the user is still editing is a problem of that pass
    // specification, not an exception out of the pipeline, so the throw stops here.
    try {
        if (!compile(CompileStage::GCT)) {
            return ProgramStatus::Failed;
        }
    } catch (const Diagnostic &) {
        return ProgramStatus::Failed;
    }
    struct Compiling {
        bool &flag;
        explicit Compiling(bool &f) : flag(f) { flag = true; }
        ~Compiling() { flag = false; }
    } compiling(compiling_);
    struct BuildingProgramOfItsOwn {
        bool &flag;
        explicit BuildingProgramOfItsOwn(bool &f) : flag(f) { flag = true; }
        ~BuildingProgramOfItsOwn() { flag = false; }
    } buildingProgramOfItsOwn(buildingProgramOfItsOwn_);
    camel::utils::phase::Scope phaseScope("library", name_);
    auto builder = std::make_shared<camel::compile::gir::Builder>(context_);
    // A module written in Camel may import a library written in Camel (item 1): the
    // pass module is a program of its own, so its builder needs those imports declared and built
    // too. They are declared here, in import order, and built by buildFrom before this module.
    // Their graphs in this program are separate from the graphs the same module has in the
    // program that loaded it; the builder keeps them, and the export table of the imported
    // module is filled by this declaration, as it is whenever a library is compiled.
    std::vector<std::shared_ptr<UserDefinedModule>> imported;
    std::unordered_set<const UserDefinedModule *> seen;
    std::function<bool(const std::shared_ptr<UserDefinedModule> &)> collectImports =
        [&](const std::shared_ptr<UserDefinedModule> &mod) -> bool {
        if (mod.get() == this || !seen.insert(mod.get()).second) {
            return true;
        }
        if (!mod->compile(CompileStage::GCT)) {
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
    for (const auto &site : importSites()) {
        if (auto other = std::dynamic_pointer_cast<UserDefinedModule>(site.mod)) {
            if (!collectImports(other)) {
                return ProgramStatus::Failed;
            }
        }
    }
    // The flag is only for this build: these modules may be built again as part of another
    // program, and then their root graph must be recorded again.
    auto restoreImported = std::unique_ptr<void, std::function<void(void *)>>(
        reinterpret_cast<void *>(1), [&imported](void *) {
            for (const auto &mod : imported) {
                mod->buildingProgramOfItsOwn_ = false;
            }
        });
    for (const auto &mod : imported) {
        // The module object may already export names for the program that loaded it. Those
        // names are that program's, and they stay there: this builder has its own export table
        // (Builder::exports), so declaring the module again records this program's graphs
        // (task).
        builder->declareModule(mod, *mod->gct_, mod->diagnostics_);
        if (mod->diagnostics_->hasErrors()) {
            return ProgramStatus::Failed;
        }
        // Its graphs belong to this program's builder, not to the program it was loaded as
        // part of: setProgramGraph must not replace the graph it had there.
        mod->buildingProgramOfItsOwn_ = true;
    }
    builder->declareModule(shared_from_this(), *gct_, diagnostics_);
    if (diagnostics_->hasErrors()) {
        return ProgramStatus::Failed;
    }
    // The export table is filled by the declaration: the function the program runs must be one
    // non-generic function of this module, exactly as registerCamelPass demands of a pass.
    const auto entity = builder->exports().exportedEntity(*this, Reference(entryFunction));
    if (!entity) {
        // A function that is there but not exported is the mistake `module::name` cannot tell
        // from a name that is not there; only the construction tree can.
        problem = declaresFunction(entryFunction)
                      ? std::format("'{}' declares '{}' but does not export it", name_, entryFunction)
                      : std::format("'{}' exports no '{}'", name_, entryFunction);
        return ProgramStatus::NoSuchFunction;
    }
    using Access = detail::EntityAccess;
    if (Access::isGenericTemplate(*entity) || !Access::isGraphSet(*entity) ||
        !Access::graphSet(*entity) || Access::graphSet(*entity)->size() != 1) {
        problem = std::format("'{}::{}' is not one non-generic function", name_, entryFunction);
        return ProgramStatus::NotAFunction;
    }
    if (!builder->buildLibraryEntry(shared_from_this(), entryFunction) ||
        diagnostics_->hasErrors()) {
        return ProgramStatus::Failed;
    }
    root = builder->rootGraph();
    if (root == nullptr) {
        return ProgramStatus::Failed;
    }
    // The builder owns what the graphs of the program refer to and lives as long as the module.
    programBuilders_.push_back(builder);
    // The module is not marked built: this program's exports are this program's, and another
    // program that loads the module (the one being transformed, another pass) declares it into
    // its own export table (task). The front end (parse, GCT) is done, which is what the
    // module object shares with every program.
    return ProgramStatus::Built;
}

void UserDefinedModule::declareTypes() { (void)compile(CompileStage::GCT); }

void UserDefinedModule::declareEntities() { (void)compile(CompileStage::Declared); }

std::vector<std::string> UserDefinedModule::operatorProtocols() const {
    return implementation_ ? implementation_->operatorProtocols() : Module::operatorProtocols();
}
