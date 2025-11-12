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
 * Created: Aug. 24, 2025
 * Updated: Nov. 12, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <iomanip>

#include "antlr4-runtime/antlr4-runtime.h"

#include "antlr/OpenCMLLexer.h"
#include "antlr/OpenCMLParser.h"

#include "ast_builder.h"
#include "cst_dumper.h"

#include "error/diagnostics/diagnostics.h"

class ParserErrorListener : public antlr4::BaseErrorListener {
  protected:
    bool hasErrors_ = false;
    diagnostics_ptr_t diagnostics_;

  public:
    ParserErrorListener(diagnostics_ptr_t diagnostics) : diagnostics_(diagnostics) {}
    virtual ~ParserErrorListener() = default;

    bool hasErrors() const { return hasErrors_; }

    virtual void syntaxError(
        antlr4::Recognizer *recognizer, antlr4::Token *offendingSymbol, size_t line,
        size_t charPositionInLine, const std::string &msg, std::exception_ptr e) override;
};

class CamelParser {
    diagnostics_ptr_t diagnostics_;
    antlr4::ANTLRInputStream input_;

    std::unique_ptr<OpenCMLLexer> lexer_;
    std::unique_ptr<antlr4::CommonTokenStream> tokens_;
    std::unique_ptr<OpenCMLParser> parser_;

    antlr4::tree::ParseTree *cst_ = nullptr;
    AST::node_ptr_t ast_ = nullptr;

    bool buildCST();

    bool buildAST();

  public:
    CamelParser(diagnostics_ptr_t diagnostics) : diagnostics_(diagnostics) {}
    ~CamelParser() = default;

    const std::vector<antlr4::Token *> getTokens() {
        return tokens_ ? tokens_->getTokens() : std::vector<antlr4::Token *>{};
    }

    antlr4::tree::ParseTree *cst() const { return cst_; }
    AST::node_ptr_t ast() const { return ast_; }
    diagnostics_ptr_t diagnostics() const { return diagnostics_; }

    bool parse(std::istream &is);

    void dumpTokens(std::ostream &os);

    void dumpDiagnostics(std::ostream &os, bool json = false);
};

using parser_ptr_t = std::shared_ptr<CamelParser>;
