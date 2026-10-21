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
 * Updated: May. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "module.h"

#include "camel/compile/gct.h"

#include "camel/parse/parse.h"

#include <vector>

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
    // compileAsLibrary(): the graph builder of its own the module was built by, kept with it.
    std::shared_ptr<void> libraryBuilder_;
    // compileAsProgram(): the graph builder of each program of its own, one per entry function,
    // kept with the module for as long as the graphs it made are used.
    std::vector<std::shared_ptr<void>> programBuilders_;
    // Set while compileAsProgram() builds: the graph of that program is not the graph the
    // module was loaded as part of, so setProgramGraph must not replace it.
    bool buildingProgramOfItsOwn_ = false;

  public:
    UserDefinedModule(
        const std::string &name, const std::string &path, camel::core::context::context_ptr_t ctx,
        camel::parse::parser_ptr_t parser = nullptr);
    virtual ~UserDefinedModule() = default;

    /// The graph construction tree, or null before the module reached that stage.
    const GCT::Tree *gct() const { return gct_.get(); }
    camel::core::error::diagnostics_ptr_t diagnostics() const { return diagnostics_; }
    camel::parse::parser_ptr_t parser() const { return parser_; }
    virtual bool loaded() const override { return stage_ == CompileStage::Done; }
    CompileStage stage() const { return stage_; }

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
     * Compiles the module as a library of its own (docs/design/op-semantics, section 9): parsed,
     * declared and built by a graph builder that is not the program's, without an entry
     * function, so that it can be compiled after the program that uses it was built (the
     * definition libraries of operators). It imports modules written in C++ only: a module
     * written in Camel it imported would be declared to the program's builder, so such an
     * import is refused. Done at once when the module is done already (the program imported
     * and built it). False when it is in error; its diagnostics say why.
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
     * part of; its builder is kept with the module, like the one of compileAsLibrary.
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

    static module_ptr_t fromFile(
        const std::string &name, const std::string &path, camel::core::context::context_ptr_t ctx);

    bool load() override {
        bool success = compile();
        loaded_      = success;
        return success;
    }
};

} // namespace camel::core::module
