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

#include "userdef.h"

#include "antlr4-runtime/antlr4-runtime.h"

#include "config.h"

#include "parse/parse.h"

#include "common/error/base.h"
#include "common/error/diagnostic.h"
#include "common/error/listener.h"

#include "utils/log.h"

using namespace std;
using namespace antlr4;

UserDefinedModule::UserDefinedModule(
    const std::string &name, const std::string &path, context_ptr_t ctx, parser_ptr_t parser)
    : Module(name, path), context_(ctx) {
    if (parser) {
        parser_ = parser;
        diagnostics_ = parser->diagnostics();
    } else {
        diagnostics_ = std::make_shared<Diagnostics>(ctx->diagConfig());
        parser_ = std::make_shared<CamelParser>(diagnostics_);
    }
    // Automatically import the built-in module
    importAllRefsFromMod(ctx->importModule(""));
}

module_ptr_t UserDefinedModule::loadFromFile(
    const std::string &name, const std::string &path, context_ptr_t ctx) {
    return std::make_shared<UserDefinedModule>(name, path, ctx);
}

bool UserDefinedModule::compile() {
    l.in("Module").info("Compiling user-defined module '{}' from {}", name_, path_);
    if (this->built_) {
        l.in("Module").info("Module '{}' already built", name_);
        return this->built_;
    }

    if (!parser_->ast()) {
        std::ifstream ifs(path_);
        if (!ifs.good()) {
            throw CamelBaseException("Cannot open file: " + path_);
        }
        if (!parser_->parse(ifs)) {
            throw CamelBaseException("Failed to parse file: " + path_);
        }
    }

    auto ast = parser_->ast();
    auto gctConstructor = GCT::Constructor(context_, shared_from_this());
    gct_ = gctConstructor.construct(ast, diagnostics_);

    auto girConstructor = GIR::Constructor(context_, shared_from_this());
    gir_ = girConstructor.construct(gct_, diagnostics_);

    this->built_ = !diagnostics_->hasErrors();
    l.in("Module").debug("Module '{}' built: {}", name_, this->built_ ? "success" : "failed");
    return this->built_;
}