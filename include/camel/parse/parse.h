// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * CamelParser: one source file from text to abstract syntax tree.
 *
 *   text -> tokens (syntax::Lexer) -> concrete syntax tree (syntax::Parser) -> AST (ast::Builder)
 *
 * and the diagnostics of each step. The concrete tree is kept whatever the outcome: the tools
 * that describe a file being edited (the language server, the formatter) work on it.
 * See docs/technical/34_handwritten_parser.md.
 */

#pragma once

#include <iosfwd>
#include <memory>
#include <string>

#include "ast.h"
#include "syntax/cst.h"

#include "camel/core/error/diagnostics.h"
#include "camel/core/source/manager.h"

namespace camel::parse {

class CamelParser {
  public:
    explicit CamelParser(camel::core::error::diagnostics_ptr_t diagnostics);
    ~CamelParser();

    /// The parser does not own SourceContext; it only records parse results into it.
    void setSourceContext(camel::source::source_context_ptr_t sourceContext) {
        sourceContext_ = std::move(sourceContext);
        if (diagnostics_) {
            diagnostics_->setSourceContext(sourceContext_);
        }
    }
    camel::source::source_context_ptr_t sourceContext() const { return sourceContext_; }
    camel::source::source_file_id_t sourceFileId() const { return sourceFileId_; }

    /// Lexical analysis only (`camel check -i`): the tokens are produced, nothing is parsed.
    /// False when the text has a lexical error.
    bool lex(std::istream &is);
    /// Tokens and concrete syntax tree. False when the text has a lexical or a syntax error.
    bool parseCST(std::istream &is);
    /// The whole way to the AST.
    bool parse(std::istream &is);

    AST::node_ptr_t ast() const { return ast_; }
    camel::core::error::diagnostics_ptr_t diagnostics() const { return diagnostics_; }

    /// The concrete syntax tree of the last parse, whatever its outcome: with Error nodes and
    /// incomplete nodes where the text is wrong. After lex() it holds the tokens only. Null
    /// before the first call.
    const syntax::Tree *tree() const { return tree_.get(); }
    /// Whether the last parse found neither a lexical nor a syntax error.
    bool parsed() const { return parsed_; }

    /// Where the tokens are, for diagnostics placed by token range.
    const camel::core::error::TokenPositions &tokenPositions() const;

    /// Returns the declared module name from AST (e.g. from "module mnist_json"), or empty if
    /// none.
    std::string getDeclaredModuleName() const;
    /// The module name a file declares (`module name`), read from the tokens: what a file
    /// that does not parse still says about itself. Empty if it declares none.
    std::string declaredModuleNameInTokens() const;

    void dumpTokens(std::ostream &os);
    void dumpCST(std::ostream &os);
    void dumpDiagnostics(std::ostream &os, bool json = false);

  private:
    class Positions;

    camel::core::error::diagnostics_ptr_t diagnostics_;
    camel::source::source_context_ptr_t
        sourceContext_; // Parse-stage spans/origins are written here.
    // Logical ID of the source file currently being parsed in SourceContext.
    camel::source::source_file_id_t sourceFileId_ = camel::source::kInvalidSourceFileId;
    AST::node_ptr_t ast_                          = nullptr;
    bool parsed_                                  = false;

    std::unique_ptr<syntax::Tree> tree_;
    std::unique_ptr<Positions> positions_;

    std::string readAndRegister(std::istream &is);
    void nameModuleFromTokens();
    bool buildCST(std::istream &is);
    bool buildAST();
};

using parser_ptr_t = std::shared_ptr<CamelParser>;

} // namespace camel::parse
