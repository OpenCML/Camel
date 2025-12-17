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
 * Updated: Dec. 17, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "module.h"

#include "compile/gct.h"
#include "compile/gir.h"

#include "parse/parse.h"

class Context;
using context_ptr_t = std::shared_ptr<Context>;

enum class CompileStage { None, AST, GCT, GIR, Done };

class UserDefinedModule : public Module {
    GCT::node_ptr_t gct_;
    GraphIR::graph_ptr_t gir_;
    diagnostics_ptr_t diagnostics_;
    parser_ptr_t parser_;
    CompileStage stage_ = CompileStage::None;

  public:
    UserDefinedModule(
        const std::string &name, const std::string &path, context_ptr_t ctx,
        parser_ptr_t parser = nullptr);
    virtual ~UserDefinedModule() = default;

    GCT::node_ptr_t gct() const { return gct_; }
    GraphIR::graph_ptr_t gir() const { return gir_; }
    diagnostics_ptr_t diagnostics() const { return diagnostics_; }
    parser_ptr_t parser() const { return parser_; }
    virtual bool loaded() const override { return stage_ == CompileStage::Done; }
    CompileStage stage() const { return stage_; }

    bool compile(CompileStage till = CompileStage::Done);

    static module_ptr_t
    fromFile(const std::string &name, const std::string &path, context_ptr_t ctx);

    bool load() override {
        bool success = compile();
        loaded_      = success;
        return success;
    }
};
