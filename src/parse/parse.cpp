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

#include "parse.h"

using namespace std;
using namespace antlr4;

void ParserErrorListener::syntaxError(
    antlr4::Recognizer *recognizer, antlr4::Token *offendingSymbol, size_t line,
    size_t charPositionInLine, const std::string &msg, std::exception_ptr e) {
    hasErrors_ = true;
    if (offendingSymbol) {
        diagnostics_->of(SyntaxDiag::UnknownSyntaxError).at(offendingSymbol).commit(msg);
    } else {
        diagnostics_->of(SyntaxDiag::UnknownSyntaxError)
            .at(CharRange{{line, charPositionInLine}, {line, charPositionInLine}})
            .commit(msg);
    }
}

bool CamelParser::buildCST() {
    auto interpreter = parser_->getInterpreter<antlr4::atn::ParserATNSimulator>();
    parser_->removeErrorListeners();

    try {
        interpreter->setPredictionMode(antlr4::atn::PredictionMode::SLL);
        parser_->setErrorHandler(std::make_shared<antlr4::BailErrorStrategy>());
        cst_ = parser_->program();
    } catch (antlr4::ParseCancellationException &e) {
        parser_->reset();
        interpreter->setPredictionMode(antlr4::atn::PredictionMode::LL);
        auto listener = std::make_unique<ParserErrorListener>(diagnostics_);
        parser_->addErrorListener(listener.get());
        parser_->setErrorHandler(std::make_shared<antlr4::DefaultErrorStrategy>());

        cst_ = parser_->program();

        if (listener->hasErrors()) {
            cst_ = nullptr;
            return false;
        }
    }

    return cst_ != nullptr;
}

bool CamelParser::buildAST() {
    auto constructor = AST::Builder();
    ast_ = constructor.build(cst_, diagnostics_);

    if (diagnostics_->hasErrors()) {
        ast_ = nullptr;
        return false;
    }

    return ast_ != nullptr;
}

bool CamelParser::parse(std::istream &is) {
    input_.load(is);

    lexer_ = std::make_unique<OpenCMLLexer>(&input_);
    tokens_ = std::make_unique<antlr4::CommonTokenStream>(lexer_.get());
    parser_ = std::make_unique<OpenCMLParser>(tokens_.get());

    return buildCST() && buildAST();
}

void CamelParser::dumpTokens(std::ostream &os) {
    tokens_->reset();
    while (true) {
        antlr4::Token *token = tokens_->LT(1);
        if (token->getType() == antlr4::Token::EOF) {
            break;
        }
        os << std::setw(4) << std::right << token->getTokenIndex() << " [" << std::setw(3)
           << std::right << token->getLine() << ":" << std::setw(3) << std::left
           << token->getCharPositionInLine() << "] (" << token->getChannel()
           << ") : " << token->getText() << std::endl;
        tokens_->consume();
    }
    tokens_->reset();
}

void CamelParser::dumpDiagnostics(std::ostream &os, bool json) {
    const auto &tokenVec = tokens_->getTokens();
    diagnostics_->fetchAll(tokenVec);
    diagnostics_->dump(os, json);
}
