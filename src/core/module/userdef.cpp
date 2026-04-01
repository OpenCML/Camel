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
 * Updated: Apr. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/utils/windows_parser_guard.h"

#include "camel/core/module/userdef.h"

#include "antlr4-runtime/antlr4-runtime.h"

#include "camel/core/debug_breakpoint.h"
#include "camel/parse/parse.h"
#include "compile/gct/builder.h"
#include "compile/gir/builder.h"

#include "camel/core/error/diagnostics.h"
#include "camel/core/error/listener.h"

#include "camel/utils/log.h"

#include <filesystem>

namespace fs = std::filesystem;

using namespace std;
using namespace antlr4;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::error;
using namespace camel::parse;

namespace GIR = camel::compile::gir;

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
            path.empty() ? "<in-memory>" : fs::absolute(path).string(),
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

bool UserDefinedModule::compile(CompileStage till) {
    if (till <= stage_) {
        return true;
    }
    if (stage_ == CompileStage::Done) {
        CAMEL_LOG_WARN_S("Module", "Module '{}' already built", name_);
        return true;
    }
    if (stage_ == CompileStage::None) {
        CAMEL_LOG_INFO_S("Module", "Start compiling module '{}' from file '{}'.", name_, path_);
    }

    if (stage_ == CompileStage::None && till > CompileStage::None) {
        if (!parser_->ast()) {
            std::ifstream ifs(path_);
            if (!ifs.good()) {
                throw DiagnosticBuilder::of(SemanticDiag::ModuleNotFound)
                    .commit(name_, "module file not found or unreadable: " + path_);
            }
            if (!parser_->parse(ifs)) {
                throw DiagnosticBuilder::of(SemanticDiag::ModuleParseFailed).commit(path_);
            }
        }
        if (diagnostics_->hasErrors()) {
            CAMEL_LOG_FATAL_S("Module", "Module '{}' failed to parse", name_);
            return false;
        }
        if (till == CompileStage::AST) {
            stage_ = CompileStage::AST;
            CAMEL_LOG_INFO_S("Module", "Module '{}' built successfully.", name_);
            return true;
        }
    }

    if (stage_ < CompileStage::AST && till >= CompileStage::AST) {
        if (!gct_) {
            auto ast        = parser_->ast();
            auto gctBuilder = GCT::Builder(context_, shared_from_this());
            gct_            = gctBuilder.build(ast, diagnostics_);
            EXEC_WHEN_DEBUG({ camel::DebugBreakpoint::Hit("GCT", gct_.get()); });
            if (diagnostics_->hasErrors()) {
                CAMEL_LOG_FATAL_S("Module", "Module '{}' failed to build GCT", name_);
                return false;
            }
        }
        if (till == CompileStage::GCT) {
            stage_ = CompileStage::GCT;
            CAMEL_LOG_INFO_S("Module", "Module '{}' built successfully.", name_);
            return true;
        }
    }

    if (stage_ < CompileStage::GIR && till >= CompileStage::GIR) {
        if (!gir_) {
            auto girBuilder = GIR::Builder(context_, shared_from_this());
            gir_            = girBuilder.build(gct_, diagnostics_);
            if (diagnostics_->hasErrors()) {
                CAMEL_LOG_FATAL_S("Module", "Module '{}' failed to build GIR", name_);
                return false;
            }
        }
        if (till == CompileStage::GIR) {
            stage_ = CompileStage::GIR;
            CAMEL_LOG_INFO_S("Module", "Module '{}' built successfully.", name_);
            return true;
        }
    }

    stage_ = CompileStage::Done;
    CAMEL_LOG_INFO_S("Module", "Module '{}' built successfully.", name_);
    return true;
}
