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
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "module.h"

#include "camel/compile/gct.h"
#include "camel/compile/gir.h"

#include "camel/parse/parse.h"

namespace camel::core::module {

enum class CompileStage { None, AST, GCT, GIR, Done };

class UserDefinedModule : public Module {
    GCT::node_ptr_t gct_;
    GIR::graph_ptr_t gir_;
    camel::core::error::diagnostics_ptr_t diagnostics_;
    camel::parse::parser_ptr_t parser_;
    CompileStage stage_ = CompileStage::None;

  public:
    UserDefinedModule(
        const std::string &name, const std::string &path, camel::core::context::context_ptr_t ctx,
        camel::parse::parser_ptr_t parser = nullptr);
    virtual ~UserDefinedModule() = default;

    GCT::node_ptr_t gct() const { return gct_; }
    GIR::graph_ptr_t gir() const { return gir_; }
    camel::core::error::diagnostics_ptr_t diagnostics() const { return diagnostics_; }
    camel::parse::parser_ptr_t parser() const { return parser_; }
    virtual bool loaded() const override { return stage_ == CompileStage::Done; }
    CompileStage stage() const { return stage_; }

    bool compile(CompileStage till = CompileStage::Done);

    static module_ptr_t fromFile(
        const std::string &name, const std::string &path, camel::core::context::context_ptr_t ctx);

    bool load() override {
        bool success = compile();
        loaded_      = success;
        return success;
    }
};

} // namespace camel::core::module
