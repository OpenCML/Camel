// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The kernel's one door to the compiler for a module written in Camel.
 *
 * The kernel owns such a module (`UserDefinedModule`): its name, its diagnostics, the stage the
 * loading reached, and the graph it hands out. Reading that module's source, building its
 * construction tree and compiling it into a program is the compiler's business, and the compiler
 * is a layer *above* the kernel: `kernel/` includes neither `parse/` nor `compile/` (design
 * principles 9: a pass sees graphs, and what it is handed is a graph).
 *
 * So the kernel declares the steps and the compiler implements them, in one object registered
 * here when the library loads (`src/compile/module_source.cpp`). The kernel holds the slot and
 * calls through it. A build that links no compiler registers none: it loads the modules written in
 * C++ and reports a clear diagnostic when a module written in Camel is asked for.
 *
 * Nothing here names a parser, a tree or a graph. The front end's state travels as an opaque
 * handle (`std::shared_ptr<void>`) that only the loader interprets, and every answer the kernel
 * reads back is one of the kernel's own types.
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "camel/core/module/userdef.h"

namespace camel::runtime {
class Program;
}

namespace camel::core::module {

/// The steps of loading a module written in Camel, and the few questions the kernel asks of its
/// front end once they have run. Implemented beside the compiler and registered by
/// `registerSourceModuleLoader`; the kernel calls it through `sourceModuleLoader()`.
class SourceModuleLoader {
  public:
    virtual ~SourceModuleLoader() = default;

    /// What `attach` hands back: the front end's state, opaque, and the diagnostics the module
    /// reports into -- the ones a reader the caller handed over already had, or the ones the
    /// loader made for a module that had none.
    struct Attached {
        std::shared_ptr<void> frontEnd;
        camel::core::error::diagnostics_ptr_t diagnostics;
    };

    /// Gives `module` its front end: the reader of its source -- `source`, the reader the caller
    /// already made, handed over opaquely, or one made for the module when it is null -- the
    /// diagnostics it reports into, and whatever else reading the source will need. Called once,
    /// from the module's constructor.
    virtual Attached attach(UserDefinedModule &module, const std::shared_ptr<void> &source) = 0;

    /// Reads the module's source into its parse tree. False when the file is not there or does
    /// not parse; the module's diagnostics say which.
    virtual bool readSource(UserDefinedModule &module) = 0;

    /// Builds the module's graph construction tree: what it declares, before any graph is built.
    /// False when a declaration is refused.
    virtual bool buildConstructionTree(UserDefinedModule &module) = 0;

    /// Declares the module into its program: its functions, its top-level values and its types
    /// become known there, and its exports readable, before any function body is built.
    virtual bool declare(UserDefinedModule &module) = 0;

    /// Builds and links the program the module is the entry of; the module is done when this
    /// returns true, having been handed that program's root graph through `setProgramGraph`.
    virtual bool buildProgram(UserDefinedModule &module) = 0;

    /// Compiles the module as a library (see `UserDefinedModule::compileAsLibrary`), on the graph
    /// builder of the program it belongs to.
    virtual bool buildLibrary(UserDefinedModule &module) = 0;

    /// Compiles the module as a program of its own whose entry is `entryFunction` (see
    /// `UserDefinedModule::compileAsProgram`); `root`, opaque, is the root graph it built.
    virtual UserDefinedModule::ProgramStatus buildProgramOfItsOwn(
        UserDefinedModule &module, const std::string &entryFunction, std::string &problem,
        std::shared_ptr<void> &root, camel::core::type::FunctionType *expected) = 0;

    /// The functions the module exports, in declaration order (see
    /// `UserDefinedModule::exportedFunctions`). Only the construction tree is read, so a module
    /// that is parsed answers even when it does not compile.
    virtual std::vector<UserDefinedModule::ExportedFunction>
    exportedFunctions(const UserDefinedModule &module) const = 0;

    /// Whether the module declares a top-level function `name`, exported or not.
    virtual bool
    declaresFunction(const UserDefinedModule &module, const std::string &name) const = 0;

    /// Where the tokens of the module's source are: null before the source is read, and for a
    /// module that has none.
    virtual const camel::core::error::TokenPositions *
    tokenPositions(const UserDefinedModule &module) const = 0;

    /// Writes the graphs reachable from `graph` -- a root graph this loader built -- into
    /// `program`, with `graph`'s runtime graph as its root. With `freshIds` those graphs take new
    /// ids from the module's context instead of keeping the compile graphs' own, so that one
    /// program never holds two graphs of one id (the definition graphs of operators; see
    /// `camel/compile/gir/encode.h`).
    virtual void encodeProgram(
        const std::shared_ptr<void> &graph, UserDefinedModule &module,
        camel::runtime::Program &program, bool freshIds) = 0;

    /// The graph of `module`'s exported function `name` that takes `params` ordinary parameters
    /// and no `with` parameter: the definition graph of an operator
    /// (`camel/runtime/definitions.h`). Null when the function has no such overload -- or is not
    /// one function at all, which the export table already answers for.
    virtual std::shared_ptr<void> definitionGraph(
        const module_ptr_t &module, const Reference &name, size_t params) = 0;
};

/// The loader registered for this process, or null: a build that links no compiler registers none.
/// Not const: a step of loading hands the module's own state to the loader, which fills it in.
SourceModuleLoader *sourceModuleLoader();

/// Registers `loader`, at load time, from the file that implements it. A second registration
/// replaces the first.
void registerSourceModuleLoader(SourceModuleLoader *loader);

} // namespace camel::core::module