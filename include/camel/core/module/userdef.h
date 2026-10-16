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
