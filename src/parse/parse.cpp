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
 * Created: Oct. 12, 2026
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/parse/parse.h"

#include "camel/parse/ast/cst_builder.h"
#include "camel/parse/syntax/dump.h"
#include "camel/parse/syntax/lexer.h"
#include "camel/parse/syntax/parser.h"
#include "camel/parse/syntax/report.h"

#include <atomic>
#include <cstdlib>
#include <sstream>

using namespace camel::core::error;

namespace camel::parse {

namespace {

CamelParser::Backend initialBackend() {
    if (const char *value = std::getenv("CAMEL_PARSER")) {
        const std::string name(value);
        if (name == "hand") {
            return CamelParser::Backend::Hand;
        }
        if (name == "antlr") {
            return CamelParser::Backend::Antlr;
        }
    }
    return CamelParser::Backend::Antlr;
}

std::atomic<CamelParser::Backend> &defaultBackendSlot() {
    static std::atomic<CamelParser::Backend> slot{initialBackend()};
    return slot;
}

} // namespace

/// Token positions of the parser's last parse, for either backend.
class CamelParser::Positions : public TokenPositions {
  public:
    explicit Positions(const CamelParser &owner) : owner_(owner) {}

    size_t size() const override {
        if (owner_.backend_ == Backend::Hand) {
            return owner_.tree_ ? owner_.tree_->tokens().size() : 0;
        }
        return owner_.tokens_ ? owner_.tokens_->size() : 0;
    }

    CharRange charRange(size_t first, size_t last) const override {
        if (owner_.backend_ == Backend::Hand) {
            const syntax::TokenBuffer &buffer = owner_.tree_->tokens();
            const syntax::Token &a            = buffer[first];
            const syntax::Token &b            = buffer[last];
            // The end is on the line the last token starts on, its column that of the token
            // plus the token's length in bytes: what token positions always were.
            return CharRange{
                {buffer.lineOf(a.begin), buffer.columnOf(a.begin)},
                {buffer.lineOf(b.begin), buffer.columnOf(b.begin) + b.length()}};
        }
        return CharRange::fromTokens(owner_.tokens_->get(first), owner_.tokens_->get(last));
    }

  private:
    const CamelParser &owner_;
};

void CamelParser::setDefaultBackend(Backend backend) { defaultBackendSlot() = backend; }
CamelParser::Backend CamelParser::defaultBackend() { return defaultBackendSlot(); }

CamelParser::CamelParser(diagnostics_ptr_t diagnostics)
    : diagnostics_(std::move(diagnostics)), backend_(defaultBackend()),
      positions_(std::make_unique<Positions>(*this)) {}

CamelParser::~CamelParser() = default;

const TokenPositions &CamelParser::tokenPositions() const { return *positions_; }

std::string CamelParser::readAndRegister(std::istream &is) {
    std::stringstream buffer;
    buffer << is.rdbuf();
    std::string text = buffer.str();
    if (sourceContext_) {
        // Registered before parsing: the AST builder places its spans in this file.
        sourceFileId_ = sourceContext_->registerFile(diagnostics_->modulePath(), text);
    }
    parsed_     = false;
    ast_        = nullptr;
    cst_        = nullptr;
    syntaxTree_ = nullptr;
    return text;
}

bool CamelParser::lex(std::istream &is) {
    std::string text = readAndRegister(is);
    if (backend_ == Backend::Hand) {
        tree_ = std::make_unique<syntax::Tree>(syntax::Lexer::tokenize(std::move(text)));
        syntax::reportLexicalErrors(tree_->tokens(), *diagnostics_);
    } else {
        input_.load(text);
        lexer_         = std::make_unique<OpenCMLLexer>(&input_);
        lexerListener_ = std::make_unique<LexerErrorListener>(diagnostics_);
        lexer_->removeErrorListeners();
        lexer_->addErrorListener(lexerListener_.get());
        tokens_ = std::make_unique<antlr4::CommonTokenStream>(lexer_.get());
        parser_ = std::make_unique<OpenCMLParser>(tokens_.get());
        tokens_->fill();
    }
    nameModuleFromTokens();
    return !diagnostics_->hasErrors();
}

bool CamelParser::parseCST(std::istream &is) { return buildCST(is); }

bool CamelParser::buildCST(std::istream &is) {
    std::string text = readAndRegister(is);
    if (backend_ == Backend::Antlr) {
        return buildAntlrCST(text);
    }
    tree_ = std::make_unique<syntax::Tree>(syntax::Parser::parse(std::move(text), parseOptions_));
    syntax::reportLexicalErrors(tree_->tokens(), *diagnostics_);
    // A lexical error leaves the parser a token stream without the offending text; what it
    // then finds wrong is mostly a consequence, and the file is in error all the same.
    if (tree_->tokens().errors().empty()) {
        syntax::reportSyntaxErrors(*tree_, *diagnostics_);
    }
    parsed_ = !tree_->hasErrors() && !diagnostics_->hasErrors();
    EXEC_WHEN_DEBUG({
        if (parsed_)
            camel::DebugBreakpoint::Hit("CST", tree_.get());
    });
    return parsed_;
}

bool CamelParser::buildAntlrCST(const std::string &text) {
    input_.load(text);
    lexer_ = std::make_unique<OpenCMLLexer>(&input_);
    // Lexical errors become diagnostics; ANTLR's default listener printed them raw on
    // standard error and left the diagnostics empty.
    lexerListener_ = std::make_unique<LexerErrorListener>(diagnostics_);
    lexer_->removeErrorListeners();
    lexer_->addErrorListener(lexerListener_.get());
    tokens_ = std::make_unique<antlr4::CommonTokenStream>(lexer_.get());
    parser_ = std::make_unique<OpenCMLParser>(tokens_.get());
    EXEC_WHEN_DEBUG({ camel::DebugBreakpoint::Hit("CTS", this); });

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
        parser_->removeErrorListeners();

        if (listener->hasErrors()) {
            syntaxTree_ = cst_;
            cst_        = nullptr;
            return false;
        }
    }
    syntaxTree_ = cst_;
    if (diagnostics_->hasErrors()) {
        cst_ = nullptr;
        return false;
    }
    EXEC_WHEN_DEBUG({
        if (cst_)
            camel::DebugBreakpoint::Hit("CST", cst_);
    });
    parsed_ = cst_ != nullptr;
    return parsed_;
}

bool CamelParser::buildAST() {
    if (backend_ == Backend::Hand) {
        ast_ = AST::CstBuilder().build(*tree_, diagnostics_);
    } else {
        ast_ = AST::Builder().build(cst_, diagnostics_);
    }
    if (diagnostics_->hasErrors()) {
        ast_ = nullptr;
        return false;
    }
    EXEC_WHEN_DEBUG({
        if (ast_)
            camel::DebugBreakpoint::Hit("AST", ast_.get());
    });
    return ast_ != nullptr;
}

bool CamelParser::parse(std::istream &is) {
    if (!buildCST(is) || !buildAST()) {
        // A file that does not parse is still named by the module it declares: its
        // diagnostics read `(my_module)file.cml`, as the semantic ones do, not `(main)`.
        nameModuleFromTokens();
        return false;
    }
    // Use declared module name from source (e.g. "module mnist_json") for diagnostics
    std::string declaredName = getDeclaredModuleName();
    if (!declaredName.empty() && diagnostics_) {
        diagnostics_->setModuleName(declaredName);
    }
    return true;
}

std::string CamelParser::getDeclaredModuleName() const {
    if (!ast_) {
        return "";
    }
    auto load    = ast_->load();
    auto modLoad = std::dynamic_pointer_cast<AST::ModuleLoad>(load);
    if (!modLoad) {
        return "";
    }
    return modLoad->getModuleNameOrEmpty();
}

std::string CamelParser::declaredModuleNameInTokens() const {
    if (backend_ == Backend::Hand) {
        if (!tree_) {
            return "";
        }
        const syntax::TokenBuffer &buffer = tree_->tokens();
        bool afterModule                  = false;
        for (const syntax::Token &token : buffer.tokens()) {
            if (syntax::isTrivia(token.kind)) {
                continue;
            }
            if (afterModule && token.kind == syntax::TokenKind::Identifier) {
                return std::string(buffer.text(token));
            }
            afterModule = token.kind == syntax::TokenKind::KwModule;
        }
        return "";
    }
    if (!tokens_) {
        return "";
    }
    const antlr4::Token *keyword = nullptr;
    for (const antlr4::Token *token : tokens_->getTokens()) {
        if (token->getChannel() != antlr4::Token::DEFAULT_CHANNEL) {
            continue;
        }
        if (keyword != nullptr) {
            if (token->getType() == OpenCMLLexer::IDENTIFIER) {
                return token->getText();
            }
            keyword = nullptr;
        }
        if (token->getType() == OpenCMLLexer::MODULE) {
            keyword = token;
        }
    }
    return "";
}

void CamelParser::nameModuleFromTokens() {
    const std::string declaredName = declaredModuleNameInTokens();
    if (!declaredName.empty() && diagnostics_) {
        diagnostics_->setModuleName(declaredName);
    }
}

void CamelParser::dumpTokens(std::ostream &os) {
    if (backend_ == Backend::Hand) {
        if (tree_) {
            syntax::dumpTokens(os, tree_->tokens());
        }
        return;
    }
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

void CamelParser::dumpCST(std::ostream &os) {
    if (backend_ == Backend::Hand) {
        if (tree_) {
            syntax::dumpTree(os, *tree_);
        }
        return;
    }
    if (cst_) {
        auto visitor = CSTDumpVisitor(os);
        visitor.visit(cst_);
    }
}

void CamelParser::dumpDiagnostics(std::ostream &os, bool json) {
    diagnostics_->fetchAll(tokenPositions());
    diagnostics_->dump(os, json);
}

} // namespace camel::parse
