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
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * CamelParser: one source file from text to abstract syntax tree.
 *
 *   text -> tokens (syntax::Lexer) -> concrete syntax tree (syntax::Parser) -> AST (CstBuilder)
 *
 * and the diagnostics of each step. The concrete tree is kept whatever the outcome: tools that
 * describe a file being edited (the language server, the formatter) work on it.
 *
 * TRANSITION. The ANTLR parser this replaces is still here as a second backend, so that the
 * two can be compared tree for tree (tools/parsediff) and the consumers of the ANTLR parse tree
 * can move one at a time. It is selected per instance (setBackend) or for the process
 * (setDefaultBackend, or the environment variable CAMEL_PARSER=antlr|hand) and goes away with
 * the grammar files.
 */

#pragma once

#include <iomanip>
#include <memory>

#include "antlr4-runtime/antlr4-runtime.h"

#include "camel/parse/antlr/OpenCMLLexer.h"
#include "camel/parse/antlr/OpenCMLParser.h"

#include "ast/builder.h"
#include "cst_dumper.h"
#include "syntax/parser.h"
#include "syntax_errors.h"

#include "camel/core/debug_breakpoint.h"
#include "camel/core/error/diagnostics.h"
#include "camel/utils/log.h"

namespace camel::parse {

class CamelParser {
  public:
    enum class Backend { Antlr, Hand };

    /// The backend of the parsers created from now on.
    static void setDefaultBackend(Backend backend);
    static Backend defaultBackend();

    explicit CamelParser(camel::core::error::diagnostics_ptr_t diagnostics);
    ~CamelParser();

    void setBackend(Backend backend) { backend_ = backend; }
    /// For the differential harness only (see syntax::ParseOptions).
    void setParseOptions(syntax::ParseOptions options) { parseOptions_ = options; }
    Backend backend() const { return backend_; }

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
    /// incomplete nodes where the text is wrong. Null before a parse (and with the ANTLR
    /// backend).
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

    // ---- ANTLR backend only (TRANSITION) ----
    const std::vector<antlr4::Token *> getTokens() {
        return tokens_ ? tokens_->getTokens() : std::vector<antlr4::Token *>{};
    }
    antlr4::tree::ParseTree *cst() const { return cst_; }
    /// The tree of the last parse also when the file has errors: what ANTLR recovered.
    antlr4::tree::ParseTree *syntaxTree() const { return syntaxTree_; }

  private:
    class Positions;

    camel::core::error::diagnostics_ptr_t diagnostics_;
    camel::source::source_context_ptr_t
        sourceContext_; // Parse-stage spans/origins are written here.
    Backend backend_;
    syntax::ParseOptions parseOptions_;
    // Logical ID of the source file currently being parsed in SourceContext.
    camel::source::source_file_id_t sourceFileId_ = camel::source::kInvalidSourceFileId;
    AST::node_ptr_t ast_                          = nullptr;
    bool parsed_                                  = false;

    std::unique_ptr<syntax::Tree> tree_;
    std::unique_ptr<Positions> positions_;

    // ANTLR backend.
    antlr4::ANTLRInputStream input_;
    std::unique_ptr<OpenCMLLexer> lexer_;
    std::unique_ptr<LexerErrorListener> lexerListener_;
    std::unique_ptr<antlr4::CommonTokenStream> tokens_;
    std::unique_ptr<OpenCMLParser> parser_;
    antlr4::tree::ParseTree *cst_        = nullptr;
    antlr4::tree::ParseTree *syntaxTree_ = nullptr;

    std::string readAndRegister(std::istream &is);
    void nameModuleFromTokens();
    bool buildCST(std::istream &is);
    bool buildAST();
    bool buildAntlrCST(const std::string &text);
};

using parser_ptr_t = std::shared_ptr<CamelParser>;

} // namespace camel::parse
