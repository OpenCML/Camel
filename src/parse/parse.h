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
 * Updated: Aug. 24, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "antlr4-runtime/antlr4-runtime.h"

#include "antlr/OpenCMLLexer.h"
#include "antlr/OpenCMLParser.h"

#include "ast.h"
#include "cst-dump.h"

#include "common/error/diagnostic.h"

class ParserErrorListener : public antlr4::BaseErrorListener {
  protected:
    bool hasErrors_ = false;
    diagnostics_ptr_t diagnostics_;

  public:
    ParserErrorListener(diagnostics_ptr_t diagnostics) : diagnostics_(diagnostics) {}
    virtual ~ParserErrorListener() = default;

    bool hasErrors() const { return hasErrors_; }

    virtual void syntaxError(antlr4::Recognizer *recognizer, antlr4::Token *offendingSymbol, size_t line,
                             size_t charPositionInLine, const std::string &msg, std::exception_ptr e) override {
        hasErrors_ = true;
        if (offendingSymbol) {
            diagnostics_->emplace(Diagnostic::Severity::Error, msg, offendingSymbol, offendingSymbol);
        } else {
            diagnostics_->emplace(Diagnostic::Severity::Error, msg, Diagnostic::Position{line, charPositionInLine});
        }
    }
};

class Parser {
    diagnostics_ptr_t diagnostics_;
    antlr4::ANTLRInputStream input_;

    std::unique_ptr<OpenCMLLexer> lexer_;
    std::unique_ptr<antlr4::CommonTokenStream> tokens_;
    std::unique_ptr<OpenCMLParser> parser_;

    antlr4::tree::ParseTree *cst_ = nullptr;
    AST::node_ptr_t ast_ = nullptr;

    bool buildCST() {
        auto interpreter = parser_->getInterpreter<antlr4::atn::ParserATNSimulator>();
        parser_->removeErrorListeners();

        try {
            interpreter->setPredictionMode(antlr4::atn::PredictionMode::SLL);
            parser_->setErrorHandler(std::make_shared<antlr4::BailErrorStrategy>());
            cst_ = parser_->program();
        } catch (antlr4::ParseCancellationException &e) {
            parser_->reset();
            interpreter->setPredictionMode(antlr4::atn::PredictionMode::LL);
            auto listener = std::make_unique<ParserErrorListener>();
            parser_->addErrorListener(listener.get());
            parser_->setErrorHandler(std::make_shared<antlr4::DefaultErrorStrategy>());

            try {
                cst_ = parser_->program();
            } catch (std::exception &e) {
                return false;
            }

            if (listener->hasErrors()) {
                return false;
            }
        } catch (std::exception &e) {
            throw e;
        }

        return true;
    }

    bool buildAST() {
        auto constructor = AST::Constructor();
        ast_ = constructor.construct(cst_, diagnostics_);
        return ast != nullptr && !diagnostics_->hasErrors();
    }

  public:
    Parser(diagnostics_ptr_t diagnostics) : diagnostics_(diagnostics) {}
    ~Parser() = default;

    const std::vector<antlr4::Token *> getTokens() {
        return tokens_ ? tokens_->getTokens() : std::vector<antlr4::Token *>{};
    }

    antlr4::tree::ParseTree *cst() const { return cst_; }
    AST::node_ptr_t ast() const { return ast_; }
    diagnostics_ptr_t diagnostics() const { return diagnostics_; }

    bool parse(std::istream &is, std::ostream &os) {
        input_.load(is);

        lexer_ = std::make_unique<OpenCMLLexer>(&input_);
        tokens_ = std::make_unique<antlr4::CommonTokenStream>(lexer_.get());
        parser_ = std::make_unique<OpenCMLParser>(tokens_.get());
        
        return buildCST() && buildAST();
    }
};
