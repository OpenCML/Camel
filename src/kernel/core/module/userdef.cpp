// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * A module written in Camel, as the kernel keeps it: its name, its diagnostics, the stage the
 * loading reached, and the graph it hands out.
 *
 * None of the loading happens here. The parser, the construction tree and the graph builder are
 * the compiler's (`src/compile/module_source.cpp`), and the compiler is a layer above the kernel,
 * so the kernel declares the steps in `camel/core/module/source_loader.h`, the compiler
 * implements them, and this file calls through the registered loader. The stage machine is the
 * kernel's -- the stage is the kernel's own state -- while each step is one call out.
 *
 * A build that registers no loader is a build with no compiler in it: it loads the modules
 * written in C++ and gives one clear diagnostic for each module it is asked to read from source
 * (`reportWithoutLoader`).
 */

#include "camel/core/module/userdef.h"

#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "camel/core/context/context.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/module/source_loader.h"
#include "camel/utils/log.h"
#include "camel/utils/path.h"
#include "camel/utils/phase_timer.h"
#include "core/module/userdef_internal.h"

namespace fs = std::filesystem;

using namespace camel::core::error;
using namespace camel::core::module;

namespace camel::core::module {

namespace {

/// The path a module's diagnostics show: its own, absolute, or the name of a file that has none
/// (`<in-memory>`, a module the Python binding or a tool built from text).
std::string shownPath(const std::string &path) {
    return path.empty() ? std::string("<in-memory>")
                        : camel::utils::pathToUtf8(
                              fs::absolute(camel::utils::pathFromUtf8(path)));
}

} // namespace

// ---- Construction --------------------------------------------------------------------------

UserDefinedModule::UserDefinedModule(
    const std::string &name, const std::string &path, camel::core::context::context_ptr_t ctx,
    const std::shared_ptr<void> &source)
    : Module(name, path, ctx) {
    if (SourceModuleLoader *loader = sourceModuleLoader()) {
        SourceModuleLoader::Attached attached = loader->attach(*this, source);
        frontEnd_                             = std::move(attached.frontEnd);
        diagnostics_                          = std::move(attached.diagnostics);
    } else {
        // No compiler: the module still needs somewhere to say why it cannot be read, so that the
        // report about it is the module's own and names the file.
        diagnostics_ =
            std::make_shared<Diagnostics>(name_, shownPath(path_), context_->sourceContext());
        diagnostics_->setConfig(context_->diagConfig());
    }
    // Automatically import the built-in module
    importAllRefsFromMod(ctx->importModule(""));
}

UserDefinedModule::~UserDefinedModule() = default;

module_ptr_t UserDefinedModule::fromFile(
    const std::string &name, const std::string &path, camel::core::context::context_ptr_t ctx) {
    return std::make_shared<UserDefinedModule>(name, path, ctx);
}

// ---- Loading -------------------------------------------------------------------------------

void UserDefinedModule::reportWithoutLoader() {
    if (!diagnostics_ || diagnostics_->hasErrors()) {
        return;
    }
    diagnostics_->of(InternalDiag::UnknownInternalError)
        .commit(std::format(
            "the module '{}' is written in Camel and this build has no compiler for it: no source "
            "module loader is registered, so only modules written in C++ can be loaded here (the "
            "file is '{}')",
            name_.empty() ? std::string("<entry>") : name_,
            path_.empty() ? std::string("<in-memory>") : path_));
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
    SourceModuleLoader *loader = sourceModuleLoader();
    if (loader == nullptr) {
        reportWithoutLoader();
        return false;
    }
    struct Compiling {
        bool &flag;
        explicit Compiling(bool &f) : flag(f) { flag = true; }
        ~Compiling() { flag = false; }
    } compiling(compiling_);
    if (stage_ == CompileStage::None) {
        CAMEL_LOG_INFO_S("Module", "Start compiling module '{}' from file '{}'.", name_, path_);
        // The built-in signatures are read with every program (the prelude's declaration file): by
        // the first module of the context that is compiled, whichever it is -- a session of the
        // Python binding has no entry module.
        if (const std::string broken = context_->checkPreludeDeclarations(); !broken.empty()) {
            diagnostics_->of(SemanticDiag::ImportedModuleHasErrors).commit("prelude", broken);
            return false;
        }
    }

    if (stage_ < CompileStage::AST) {
        if (!loader->readSource(*this)) {
            CAMEL_LOG_INFO_S("Module", "Module '{}' failed to parse", name_);
            return false;
        }
        stage_ = CompileStage::AST;
    }

    if (till > CompileStage::AST && stage_ < CompileStage::GCT) {
        if (!loader->buildConstructionTree(*this)) {
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
        if (!loader->declare(*this)) {
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
        // The other two steps for the whole program: build, then link. The module is done when
        // the program's builder hands it the root graph (setProgramGraph).
        if (!loader->buildProgram(*this)) {
            CAMEL_LOG_INFO_S("Module", "Module '{}' failed to build GIR", name_);
            return false;
        }
    }
    CAMEL_LOG_INFO_S("Module", "Module '{}' built successfully.", name_);
    return true;
}

bool compileForLoad(const std::shared_ptr<Module> &module, CompileStage till) {
    const auto user = std::dynamic_pointer_cast<UserDefinedModule>(module);
    if (!user) {
        return false;
    }
    try {
        return user->compile(till);
    } catch (const Diagnostic &failure) {
        // The module's own diagnostics already say what is wrong with it -- the parser writes them
        // as it gives up -- and they are the ones an entry file's compile would have left behind.
        // The throw only adds the sentence an import site prints for the module it could not load
        // ("failed to parse module file: <path>", "module file not found or unreadable"), which
        // has no place of its own here and must not be lost: it is recorded on the module when it
        // is the whole of the failure.
        const diagnostics_ptr_t &diagnostics = user->diagnostics();
        if (diagnostics && !diagnostics->hasErrors()) {
            Diagnostic recorded = failure;
            recorded.persisted  = false;
            diagnostics->add(std::move(recorded));
        }
        return false;
    }
}

bool UserDefinedModule::compileAsLibrary() {
    if (stage_ == CompileStage::Done) {
        return true;
    }
    if (compiling_) {
        return false;
    }
    if (stage_ < CompileStage::GCT && !compileForLoad(shared_from_this(), CompileStage::GCT)) {
        return false;
    }
    SourceModuleLoader *loader = sourceModuleLoader();
    if (loader == nullptr) {
        reportWithoutLoader();
        return false;
    }
    struct Compiling {
        bool &flag;
        explicit Compiling(bool &f) : flag(f) { flag = true; }
        ~Compiling() { flag = false; }
    } compiling(compiling_);
    // Declaring and building a library is one phase: the loader's `buildLibrary` opens none of
    // its own, so that the report measures what it measured when the compiler did both here.
    camel::utils::phase::Scope phaseScope("library", name_);
    if (stage_ < CompileStage::Declared) {
        if (!loader->declare(*this)) {
            return false;
        }
        stage_ = CompileStage::Declared;
    }
    if (!loader->buildLibrary(*this) || diagnostics_->hasErrors()) {
        return false;
    }
    return stage_ == CompileStage::Done;
}

void UserDefinedModule::setProgramGraph(const std::shared_ptr<void> &root) {
    if (buildingAProgramOfItsOwn_) {
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
    programGraph_ = root;
    stage_        = CompileStage::Done;
    loaded_       = true;
}

UserDefinedModule::ProgramStatus UserDefinedModule::compileAsProgram(
    const std::string &entryFunction, std::string &problem, std::shared_ptr<void> &root,
    camel::core::type::FunctionType *expected) {
    if (compiling_) {
        return ProgramStatus::Failed;
    }
    // A module that does not parse is a diagnostic against that module, and this load is one of
    // the programmatic ones: compileForLoad is the one place that decides it, and the caller
    // turns a failed program into the reason a pass is not available. A pass module the user is
    // still editing is a problem of that pass specification, not an exception out of the
    // pipeline.
    if (!compileForLoad(shared_from_this(), CompileStage::GCT)) {
        return ProgramStatus::Failed;
    }
    SourceModuleLoader *loader = sourceModuleLoader();
    if (loader == nullptr) {
        reportWithoutLoader();
        return ProgramStatus::Failed;
    }
    struct Compiling {
        bool &flag;
        explicit Compiling(bool &f) : flag(f) { flag = true; }
        ~Compiling() { flag = false; }
    } compiling(compiling_);
    return loader->buildProgramOfItsOwn(*this, entryFunction, problem, root, expected);
}

// ---- What the kernel reads of the front end -------------------------------------------------

std::vector<UserDefinedModule::ExportedFunction> UserDefinedModule::exportedFunctions() const {
    SourceModuleLoader *loader = sourceModuleLoader();
    if (loader == nullptr) {
        return {};
    }
    return loader->exportedFunctions(*this);
}

bool UserDefinedModule::declaresFunction(const std::string &name) const {
    SourceModuleLoader *loader = sourceModuleLoader();
    return loader != nullptr && loader->declaresFunction(*this, name);
}

const camel::core::error::TokenPositions *UserDefinedModule::tokenPositions() const {
    SourceModuleLoader *loader = sourceModuleLoader();
    return loader != nullptr ? loader->tokenPositions(*this) : nullptr;
}

void UserDefinedModule::declareTypes() {
    (void)compileForLoad(shared_from_this(), CompileStage::GCT);
}

void UserDefinedModule::declareEntities() {
    (void)compileForLoad(shared_from_this(), CompileStage::Declared);
}

std::vector<std::string> UserDefinedModule::operatorProtocols() const {
    return implementation_ ? implementation_->operatorProtocols() : Module::operatorProtocols();
}

// ---- What the loader reads and writes of the module's own state -----------------------------

} // namespace camel::core::module

namespace camel::core::module::detail {

std::shared_ptr<void> UserDefinedModuleAccess::frontEnd(const UserDefinedModule &module) {
    return module.frontEnd_;
}

std::shared_ptr<void> UserDefinedModuleAccess::programGraph(const UserDefinedModule &module) {
    return module.programGraph_;
}

void UserDefinedModuleAccess::setBuildingAProgramOfItsOwn(
    UserDefinedModule &module, bool on) {
    module.buildingAProgramOfItsOwn_ = on;
}

} // namespace camel::core::module::detail

// ---- The loader's registration slot --------------------------------------------------------

namespace camel::core::module {

namespace {

/// The slot. A function-local static, so that the loader of the compiler may register itself
/// from a static constructor in another translation unit before this one is initialized.
SourceModuleLoader *&loaderSlot() {
    static SourceModuleLoader *slot = nullptr;
    return slot;
}

} // namespace

SourceModuleLoader *sourceModuleLoader() { return loaderSlot(); }

void registerSourceModuleLoader(SourceModuleLoader *loader) { loaderSlot() = loader; }

} // namespace camel::core::module