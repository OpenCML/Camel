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
 * Created: Jul. 29, 2025
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include <sstream>

#include "camel/utils/windows_parser_guard.h"

#include "camel/core/module/userdef.h"
#include "core/module/entity_internal.h"
#include "core/module/userdef_internal.h"

#include "camel/compile/gir/encode.h"
#include "camel/core/debug_breakpoint.h"
#include "camel/parse/parse.h"
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
    if (compiling_ || stage_ != CompileStage::None || !compile(CompileStage::GCT)) {
        return false;
    }
    for (const auto &site : importSites()) {
        if (std::dynamic_pointer_cast<UserDefinedModule>(site.mod) && site.mod.get() != this) {
            diagnostics_->of(SemanticDiag::ModuleNotFound)
                .commit(
                    site.mod->name(),
                    "a library compiled on its own (" + name_ +
                        ") imports modules written in C++ only");
            return false;
        }
    }
    struct Compiling {
        bool &flag;
        explicit Compiling(bool &f) : flag(f) { flag = true; }
        ~Compiling() { flag = false; }
    } compiling(compiling_);
    camel::utils::phase::Scope phaseScope("library", name_);
    auto builder = std::make_shared<camel::compile::gir::Builder>(context_);
    builder->declareModule(shared_from_this(), *gct_, diagnostics_);
    if (diagnostics_->hasErrors()) {
        return false;
    }
    stage_ = CompileStage::Declared;
    if (!builder->buildLibrary(shared_from_this()) || diagnostics_->hasErrors()) {
        return false;
    }
    // buildLibrary set the program graph (stage Done); the builder owns what the graphs of
    // the module refer to and lives as long as the module.
    libraryBuilder_ = builder;
    return stage_ == CompileStage::Done;
}

void UserDefinedModule::setProgramGraph(
    const std::shared_ptr<camel::compile::gir::DraftGraphBuilder> &root) {
    if (buildingProgramOfItsOwn_) {
        // compileAsProgram() builds a program of its own from this module: its root graph is
        // kept with that program's builder, and the graph this module was loaded as part of
        // (the program that uses it, or the library of compileAsLibrary) stays as it was.
        stage_  = CompileStage::Done;
        loaded_ = true;
        return;
    }
    compileGraph_ = root;
    stage_        = CompileStage::Done;
    loaded_       = true;
}

UserDefinedModule::ProgramStatus UserDefinedModule::compileAsProgram(
    const std::string &entryFunction, std::string &problem,
    std::shared_ptr<camel::compile::gir::DraftGraphBuilder> &root) {
    if (compiling_) {
        return ProgramStatus::Failed;
    }
    if (!compile(CompileStage::GCT)) {
        return ProgramStatus::Failed;
    }
    for (const auto &site : importSites()) {
        if (std::dynamic_pointer_cast<UserDefinedModule>(site.mod) && site.mod.get() != this) {
            // The same restriction as compileAsLibrary (task H36 joins the two): a module written
            // in Camel that this one imports would have to be declared to the builder of another
            // program, and its export table is the program's.
            diagnostics_->of(SemanticDiag::ModuleNotFound)
                .commit(
                    site.mod->name(),
                    "a library compiled on its own (" + name_ +
                        ") imports modules written in C++ only");
            return ProgramStatus::Failed;
        }
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
    builder->declareModule(shared_from_this(), *gct_, diagnostics_);
    if (diagnostics_->hasErrors()) {
        return ProgramStatus::Failed;
    }
    // The export table is filled by the declaration: the function the program runs must be one
    // non-generic function of this module, exactly as registerCamelPass demands of a pass.
    const auto entity = getExportedEntity(Reference(entryFunction));
    if (!entity) {
        problem = std::format("'{}' exports no '{}'", name_, entryFunction);
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
    stage_  = CompileStage::Done;
    loaded_ = true;
    return ProgramStatus::Built;
}

void UserDefinedModule::declareTypes() { (void)compile(CompileStage::GCT); }

void UserDefinedModule::declareEntities() { (void)compile(CompileStage::Declared); }
