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
 * Updated: Jul. 29, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "module.h"

#include "compile/gct.h"
#include "compile/gir.h"

#include "parse/parse.h"

class Context;
using context_ptr_t = std::shared_ptr<Context>;

class UserDefinedModule : public Module {
    bool built_ = false;
    context_ptr_t context_;
    GCT::node_ptr_t gct_;
    GIR::graph_ptr_t gir_;
    diagnostics_ptr_t diagnostics_;
    parser_ptr_t parser_;

    bool buildGCT(const AST::node_ptr_t &ast) {
        initTypes();
        auto constructor = GCT::Constructor(context_, shared_from_this());
        gct_ = constructor.construct(ast, diagnostics_);
        return gct_ != nullptr && !diagnostics_->hasErrors();
    }

    bool buildGIR() {
        auto constructor = GIR::Constructor(context_, shared_from_this());
        gir_ = constructor.construct(gct_, diagnostics_);
        return gir_ != nullptr && !diagnostics_->hasErrors();
    }

  public:
    UserDefinedModule(
        const std::string &name, const std::string &path, context_ptr_t ctx,
        parser_ptr_t parser = nullptr)
        : Module(name, path), context_(ctx) {
        if (parser) {
            parser_ = parser;
            diagnostics_ = parser->diagnostics();
        } else {
            diagnostics_ = std::make_shared<Diagnostics>(ctx->diagConfig());
            parser_ = std::make_shared<CamelParser>(diagnostics_);
        }
    }
    virtual ~UserDefinedModule() = default;

    GCT::node_ptr_t gct() const { return gct_; }
    GIR::graph_ptr_t gir() const { return gir_; }
    diagnostics_ptr_t diagnostics() const { return diagnostics_; }

    static module_ptr_t
    loadFromFile(const std::string &name, const std::string &path, context_ptr_t ctx) {
        return std::make_shared<UserDefinedModule>(name, path, ctx);
    }

    bool ready() const { return built_; }
    bool compile() {
        if (!parser_->ast()) {
            std::ifstream ifs(path_);
            if (!ifs.good()) {
                throw CamelBaseException("Cannot open file: " + path_);
            }
            if (!parser_->parse(ifs)) {
                throw CamelBaseException("Failed to parse file: " + path_);
            }
        }

        this->built_ = buildGCT(parser_->ast()) && buildGIR();
        return this->built_;
    }
};