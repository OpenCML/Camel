// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "module.h"

#include "camel/compile/gct.h"

#include "camel/parse/parse.h"

#include <string>
#include <vector>

namespace camel::core::type {
class FunctionType;
}

namespace camel::runtime {
class GCGraph;
}

namespace camel::compile::gir {
class DraftGraphBuilder;
}

namespace camel::core::module {

class UserDefinedModule;

namespace detail {
class UserDefinedModuleAccess;
}

/// How far a module written in Camel is compiled. `Declared`: the program's graph builder
/// knows its declarations and its exports are readable (the first of the three steps of
/// loading, docs/design/graph-model/README.cn.md 3.9.4); `Done`: the program it belongs to is
/// built and linked. A module other than the entry module reaches `Done` when the entry
/// module's program is built.
enum class CompileStage { None, AST, GCT, Declared, Done };

class UserDefinedModule : public Module {
    friend class detail::UserDefinedModuleAccess;

    // The graph construction tree of the module. It is kept for as long as the module:
    // generic functions are lowered again from it, also by modules that import this one.
    std::unique_ptr<GCT::Tree> gct_;
    std::shared_ptr<camel::compile::gir::DraftGraphBuilder> compileGraph_;
    camel::core::error::diagnostics_ptr_t diagnostics_;
    camel::parse::parser_ptr_t parser_;
    CompileStage stage_ = CompileStage::None;
    // Set while compile() runs: a module imported back by a module it imports is not compiled
    // again from inside its own compilation.
    bool compiling_ = false;
    // compileAsLibrary(): the graph builder the module was built by, kept with it. It is the
    // program's builder (Context::graphBuildSession), shared rather than owned: see
    // compileAsLibrary.
    std::shared_ptr<void> libraryBuilder_;
    // compileAsProgram(): the graph builder of each program of its own, one per entry function,
    // kept with the module for as long as the graphs it made are used.
    std::vector<std::shared_ptr<void>> programBuilders_;
    // Set while compileAsProgram() builds: the graph of that program is not the graph the
    // module was loaded as part of, so setProgramGraph must not replace it.
    bool buildingProgramOfItsOwn_ = false;
    // A `.cml` found beside a `.cmo` of its name declares that C++ module's operators
    // (design principles 6.4, step 1): its top-level functions have no body and
    // `implementation_` is the C++ module, kept alive for its executors and operator traits.
    bool declarationModule_ = false;
    module_ptr_t implementation_;

  public:
    UserDefinedModule(
        const std::string &name, const std::string &path, camel::core::context::context_ptr_t ctx,
        camel::parse::parser_ptr_t parser = nullptr);
    virtual ~UserDefinedModule() = default;

    /// The graph construction tree, or null before the module reached that stage.
    const GCT::Tree *gct() const { return gct_.get(); }

    /// A function the module exports: the name it has and the type it was declared with.
    struct ExportedFunction {
        std::string name;
        const camel::core::type::FunctionType *type = nullptr;
    };
    /// The functions the module exports, in declaration order: its export list read against its
    /// declarations, so that a caller can name what the module offers (: the passes a
    /// `module::pass` could have named). Only the construction tree is read — no graph is built
    /// — so this answers for a module that is parsed even when it does not compile; empty
    /// before the tree exists.
    std::vector<ExportedFunction> exportedFunctions() const;
    /// Whether the module declares a top-level function `name`, exported or not. A name that is
    /// declared but not exported is a different mistake from a name that is not there, and the
    /// difference is what the user has to change.
    bool declaresFunction(const std::string &name) const;
    camel::core::error::diagnostics_ptr_t diagnostics() const { return diagnostics_; }
    camel::parse::parser_ptr_t parser() const { return parser_; }
    virtual bool loaded() const override { return stage_ == CompileStage::Done; }
    CompileStage stage() const { return stage_; }

    /// Whether this module is the declaration module of a C++ module (a `.cml` beside the
    /// `.cmo`): its top-level functions carry no body.
    bool declarationModule() const { return declarationModule_; }
    void setDeclarationModule(bool on) { declarationModule_ = on; }
    /// The C++ module whose operators the declarations stand for, when it has one.
    const module_ptr_t &implementation() const { return implementation_; }
    void setImplementation(const module_ptr_t &implementation) {
        implementation_ = implementation;
    }

    bool compile(CompileStage till = CompileStage::Done);
    /// Whether compile() is running for this module.
    bool compiling() const { return compiling_; }
    /// How building a library as a program of its own ended (compileAsProgram).
    enum class ProgramStatus {
        Built,          ///< Its root graph is returned.
        NoSuchFunction, ///< The module exports no function of that name.
        NotAFunction,   ///< The name is not one non-generic function.
        Failed          ///< The module does not compile; its diagnostics say why.
    };
    /**
     * Compiles the module as a library (docs/design/op-semantics, section 9): parsed, declared
     * and built without a call of an entry function, so that it can be compiled after the
     * program that uses it was built (the definition libraries of operators, and the functions
     * the graph module calls). It is built by the graph builder of its program: the modules
     * written in Camel it imports were declared there when its construction tree was built, and
     * building it there keeps the graphs those modules export the ones this one names (so a
     * pass module may import `array`, or any other library written in Camel). Done at once when
     * the module is done already (the program imported and built it). False when it is in
     * error; its diagnostics say why.
     */
    bool compileAsLibrary();
    /**
     * Compiles the module as a program of its own whose entry is the exported function
     * `entryFunction` (a pass written in Camel: src/execute/camel_pass.cpp), by a graph builder
     * of its own. `Built` with `root` set to its root graph, `NoSuchFunction` or `NotAFunction`
     * when the export table has no such function, `Failed` when the module does not compile (its
     * diagnostics say why).
     *
     * The program is the module, not the function: its root graph holds the top-level values of
     * the module and calls the function, so a module-level `let` — a literal, an expression, or
     * the result of a function call — is computed before the call, as a program computes its
     * top-level values before `main` (design principles 10.1). Encoding the function's graph
     * alone, as a definition library does, leaves its capture ports unbound: nothing computes
     * the values they stand for.
     *
     * The build is a program of its own and does not replace the graph the module was loaded as
     * part of; its builder is kept with the module, like the one of compileAsLibrary. The modules
     * written in Camel that this one imports are declared to that builder, in import order, and
     * built before it, so the pass may name what a Camel library exports.
     */
    ProgramStatus compileAsProgram(
        const std::string &entryFunction, std::string &problem,
        std::shared_ptr<camel::compile::gir::DraftGraphBuilder> &root);
    /// Called by the program's graph builder once the program is built: `root` is its root
    /// graph, and the module is done.
    void setProgramGraph(const std::shared_ptr<camel::compile::gir::DraftGraphBuilder> &root);

    void declareTypes() override;
    void declareEntities() override;
    // A module written in Camel brings no names along with an import of it.
    void declareDefaultRefs() override {}
    /// A declaration module answers for the operators of the C++ module it declares: its `rand:`
    /// is the protocol of `random`'s operators, not a name of its own.
    std::vector<std::string> operatorProtocols() const override;

    static module_ptr_t fromFile(
        const std::string &name, const std::string &path, camel::core::context::context_ptr_t ctx);

    bool load() override {
        bool success = compile();
        loaded_      = success;
        return success;
    }
};

} // namespace camel::core::module
