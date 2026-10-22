// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/parse/parse.h"

#include "camel/core/debug_breakpoint.h"
#include "camel/parse/ast/builder.h"
#include "camel/parse/syntax/dump.h"
#include "camel/parse/syntax/lexer.h"
#include "camel/parse/syntax/parser.h"
#include "camel/parse/syntax/report.h"
#include "camel/utils/log.h"

#include <sstream>

using namespace camel::core::error;

namespace camel::parse {

/// Token positions of the parser's last parse.
class CamelParser::Positions : public TokenPositions {
  public:
    explicit Positions(const CamelParser &owner) : owner_(owner) {}

    size_t size() const override { return owner_.tree_ ? owner_.tree_->tokens().size() : 0; }

    CharRange charRange(size_t first, size_t last) const override {
        const syntax::TokenBuffer &buffer = owner_.tree_->tokens();
        const syntax::Token &a            = buffer[first];
        const syntax::Token &b            = buffer[last];
        // The end is on the line the last token starts on, its column that of the token plus
        // the token's length in bytes: what token positions always were.
        return CharRange{
            {buffer.lineOf(a.begin), buffer.columnOf(a.begin)},
            {buffer.lineOf(b.begin), buffer.columnOf(b.begin) + b.length()}};
    }

  private:
    const CamelParser &owner_;
};

CamelParser::CamelParser(diagnostics_ptr_t diagnostics)
    : diagnostics_(std::move(diagnostics)), positions_(std::make_unique<Positions>(*this)) {}

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
    parsed_ = false;
    ast_    = nullptr;
    return text;
}

bool CamelParser::lex(std::istream &is) {
    tree_ = std::make_unique<syntax::Tree>(syntax::Lexer::tokenize(readAndRegister(is)));
    syntax::reportLexicalErrors(tree_->tokens(), *diagnostics_);
    nameModuleFromTokens();
    return !diagnostics_->hasErrors();
}

bool CamelParser::parseCST(std::istream &is) { return buildCST(is); }

bool CamelParser::buildCST(std::istream &is) {
    tree_ = std::make_unique<syntax::Tree>(syntax::Parser::parse(readAndRegister(is)));
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

bool CamelParser::buildAST() {
    ast_ = AST::Builder().build(*tree_, diagnostics_);
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

void CamelParser::nameModuleFromTokens() {
    const std::string declaredName = declaredModuleNameInTokens();
    if (!declaredName.empty() && diagnostics_) {
        diagnostics_->setModuleName(declaredName);
    }
}

void CamelParser::dumpTokens(std::ostream &os) {
    if (tree_) {
        syntax::dumpTokens(os, tree_->tokens());
    }
}

void CamelParser::dumpCST(std::ostream &os) {
    if (tree_) {
        syntax::dumpTree(os, *tree_);
    }
}

void CamelParser::dumpDiagnostics(std::ostream &os, bool json) {
    diagnostics_->fetchAll(tokenPositions());
    diagnostics_->dump(os, json);
}

} // namespace camel::parse
