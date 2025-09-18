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

#include "compile/builder/gct_builder.h"
#include "compile/builder/gir_builder.h"
#include "parse/parse.h"

#include "error/base.h"
#include "error/diagnostics/diagnostics.h"
#include "error/listener.h"

#include "utils/log.h"

#include <filesystem>

namespace fs = std::filesystem;

using namespace std;
using namespace antlr4;

namespace GIR = GraphIR;

UserDefinedModule::UserDefinedModule(
    const std::string &name, const std::string &path, context_ptr_t ctx, parser_ptr_t parser)
    : Module(name, path, ctx) {
    if (parser) {
        parser_ = parser;
        diagnostics_ = parser->diagnostics();
    } else {
        diagnostics_ = std::make_shared<Diagnostics>(
            name,
            path.empty() ? "<in-memory>" : fs::absolute(path).string());
        diagnostics_->setConfig(ctx->diagConfig());
        parser_ = std::make_shared<CamelParser>(diagnostics_);
    }
    // Automatically import the built-in module
    importAllRefsFromMod(ctx->importModule(""));
}

module_ptr_t
UserDefinedModule::fromFile(const std::string &name, const std::string &path, context_ptr_t ctx) {
    return std::make_shared<UserDefinedModule>(name, path, ctx);
}

bool UserDefinedModule::compile(CompileStage till) {
    if (till <= stage_) {
        return true;
    }
    if (stage_ == CompileStage::Done) {
        l.in("Module").warn("Module '{}' already built", name_);
        return true;
    }
    if (stage_ == CompileStage::None) {
        l.in("Module").info("Start compiling module '{}' from file '{}'.", name_, path_);
    }

    if (stage_ == CompileStage::None && till > CompileStage::None) {
        if (!parser_->ast()) {
            std::ifstream ifs(path_);
            if (!ifs.good()) {
                throw CamelBaseException("Cannot open file: " + path_);
            }
            if (!parser_->parse(ifs)) {
                throw CamelBaseException("Failed to parse file: " + path_);
            }
        }
        if (diagnostics_->hasErrors()) {
            l.in("Module").error("Module '{}' failed to parse", name_);
            return false;
        }
        if (till == CompileStage::AST) {
            stage_ = CompileStage::AST;
            l.in("Module").info("Module '{}' built successfully.", name_);
            return true;
        }
    }

    if (stage_ < CompileStage::AST && till >= CompileStage::AST) {
        if (!gct_) {
            auto ast = parser_->ast();
            auto gctBuilder = GCT::Builder(context_, shared_from_this());
            gct_ = gctBuilder.build(ast, diagnostics_);
            if (diagnostics_->hasErrors()) {
                l.in("Module").error("Module '{}' failed to build GCT", name_);
                return false;
            }
        }
        if (till == CompileStage::GCT) {
            stage_ = CompileStage::GCT;
            l.in("Module").info("Module '{}' built successfully.", name_);
            return true;
        }
    }

    if (stage_ < CompileStage::GIR && till >= CompileStage::GIR) {
        if (!gir_) {
            auto girBuilder = GIR::Builder(context_, shared_from_this());
            gir_ = girBuilder.build(gct_, diagnostics_);
            if (diagnostics_->hasErrors()) {
                l.in("Module").error("Module '{}' failed to build GIR", name_);
                return false;
            }
        }
        if (till == CompileStage::GIR) {
            stage_ = CompileStage::GIR;
            l.in("Module").info("Module '{}' built successfully.", name_);
            return true;
        }
    }

    stage_ = CompileStage::Done;
    l.in("Module").info("Module '{}' built successfully.", name_);
    return true;
}