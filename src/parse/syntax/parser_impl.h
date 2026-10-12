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

/*
 * The parser's state and its helpers, shared by the three files that hold the productions:
 * parser.cpp (program, declarations, statements), parser_expr.cpp (expressions) and
 * parser_type.cpp (types).
 *
 * Building the tree. A production takes a mark (the height of the child stack), pushes the
 * tokens it consumes and lets the productions it calls push their nodes, then closes: the
 * entries above the mark become the children of a new node, which is pushed in their place. A
 * production that turns out to have a single child does not close and leaves that child where
 * it is, which is how the pass-through levels of the grammar leave no node. A left operand is
 * wrapped after the fact by closing from the mark taken before it.
 */

#pragma once

#include "camel/parse/syntax/parser.h"

#include <initializer_list>

namespace camel::parse::syntax {

using TK = TokenKind;
using NK = NodeKind;

class ParserImpl {
  public:
    ParserImpl(TokenBuffer tokens, ParseOptions options);
    Tree run();

  private:
    // ---- State ----
    Tree tree_;
    bool lineBreaks_   = true;     // ParseOptions::lineBreaksEndStatements
    const Token *toks_ = nullptr;
    uint32_t count_    = 0;
    uint32_t pos_      = 0;        // the current token (never trivia)
    uint32_t prev_     = 0;        // the last token consumed
    TK cur_            = TK::Eof;  // kind of the current token
    std::vector<uint32_t> stack_;  // children of the nodes under construction

    static constexpr uint32_t kNone = UINT32_MAX;

    // Layout context (see Scope). `nl_`: a line break before a token that could also start a
    // statement ends the expression. `angle_`: the `<` of the angle-bracket list the parser is
    // directly inside (not inside a bracket or a block nested in it), or kNone.
    bool nl_             = true;
    uint32_t angle_      = kNone;
    bool typeBeforeBlock_ = false; // the type being parsed is followed by a block
    bool castType_        = false; // the type being parsed is that of `e as T` / `e is T`
    uint32_t spacedGt_   = kNone;  // a `>` after a space read as a comparison inside angle_

    bool panic_     = false; // an error was reported and the parser has not resynchronized
    uint32_t depth_ = 0;     // nesting of expressions, types and blocks
    static constexpr uint32_t kMaxDepth = 400;

    /// Sets the layout context for the inside of a bracket or a block; restores it at the end.
    struct Scope {
        ParserImpl &p;
        bool nl;
        uint32_t angle;
        bool typeBeforeBlock;
        bool castType;
        uint32_t spacedGt;
        Scope(ParserImpl &parser, bool newNl, uint32_t newAngle = kNone)
            : p(parser), nl(parser.nl_), angle(parser.angle_),
              typeBeforeBlock(parser.typeBeforeBlock_), castType(parser.castType_),
              spacedGt(parser.spacedGt_) {
            p.nl_              = newNl && parser.lineBreaks_;
            p.angle_           = newAngle;
            p.typeBeforeBlock_ = false;
            p.castType_        = false;
            p.spacedGt_        = kNone;
        }
        ~Scope() {
            p.nl_              = nl;
            p.angle_           = angle;
            p.typeBeforeBlock_ = typeBeforeBlock;
            p.castType_        = castType;
            p.spacedGt_        = spacedGt;
        }
    };
    struct Depth {
        ParserImpl &p;
        explicit Depth(ParserImpl &parser) : p(parser) { ++p.depth_; }
        ~Depth() { --p.depth_; }
        bool tooDeep() const { return p.depth_ > kMaxDepth; }
    };

    // ---- Tokens ----
    bool at(TK kind) const { return cur_ == kind; }
    const Token &tok() const { return toks_[pos_]; }
    /// Kind of the `n`-th token after the current one (0 is the current token).
    TK peek(uint32_t n) const {
        uint32_t i = pos_;
        while (n > 0 && toks_[i].kind != TK::Eof) {
            ++i;
            while (isTrivia(toks_[i].kind)) {
                ++i;
            }
            --n;
        }
        return toks_[i].kind;
    }
    void advance() {
        if (cur_ == TK::Eof) {
            return;
        }
        prev_ = pos_;
        do {
            ++pos_;
        } while (isTrivia(toks_[pos_].kind));
        cur_ = toks_[pos_].kind;
    }
    /// Consumes the current token as a child of the node under construction.
    void bump() {
        stack_.push_back(pos_ | Tree::kTokenBit);
        panic_ = false;
        advance();
    }
    bool eat(TK kind) {
        if (cur_ != kind) {
            return false;
        }
        bump();
        return true;
    }
    /// A line break stands before the current token, and line breaks matter here.
    bool lineBreak() const { return nl_ && tok().newlineBefore(); }
    /// The current token touches the one before it.
    bool adjacent() const { return !tok().spaceBefore(); }

    // ---- Tree ----
    size_t mark() const { return stack_.size(); }
    void close(size_t mark, NK kind);

    // ---- Errors ----
    void error(SyntaxError::Kind kind, std::initializer_list<uint8_t> expected, uint32_t at);
    void unexpected(std::initializer_list<uint8_t> expected) {
        error(SyntaxError::Kind::Unexpected, expected, pos_);
    }
    static constexpr uint8_t e(TK kind) { return static_cast<uint8_t>(kind); }
    bool expect(TK kind);
    /// The closing bracket of a list: `,` would have fitted too.
    bool expectListEnd(TK closer);
    /// A name: a keyword in its place is reported as such.
    bool expectName(NK wrapAs);
    void recover(bool inBlock, bool topLevel);

    // ---- Program, declarations, statements (parser.cpp) ----
    void parseProgram();
    bool startsDecl(TK kind) const;
    bool startsStmt(TK kind) const;
    void parseDecl();
    void parseStmt();
    void parseStmtList();
    void parseStmtBlock();
    void parseBlockExpr();
    void parseModuleName();
    void parseModuleDecl();
    void parseImportDecl();
    void parseExportDecl();
    void parseUsingNamespaceDecl();
    void parseNamespaceDecl();
    void parseFuncAnno();
    void parseFuncDecl();
    void parseDataDecl();
    void parseTypeDecl();
    void parseUseDecl();
    void parseRetStmt();
    void parseImplMark();
    void parseModifiers();
    void parseIdentDef();
    void parseIdentRef(NK kind);
    void parseIdentList();
    void parseBracketedIdents(TK closer, NK kind);
    void parseKeyParamPair();
    void parsePairedParams(TK closer);
    void parseParentParams();
    void parseAngledParams();
    /// Whether a function declaration follows after the modifiers at the current token.
    uint32_t afterModifiers() const;
    /// `{` at token offset `n` opens a struct literal: `{ name: ...` or `{ ...`.
    bool braceIsStruct(uint32_t n) const {
        const TK next = peek(n + 1);
        return next == TK::Ellipsis || (next == TK::Identifier && peek(n + 2) == TK::Colon);
    }

    // ---- Expressions (parser_expr.cpp) ----
    bool startsExpr(TK kind) const;
    void parseExpr();
    void parseAssign();
    void parseLogicalOr();
    void parseLogicalAnd();
    void parseEquality();
    void parseRelational();
    void parseAdditive();
    void parseMultiplicative();
    void parseNullable();
    void parseUnary();
    void parseLink();
    void parseComp();
    void parseAnno();
    void parseAccess();
    void parsePrimary();
    void parseLiteral();
    void parseArrayData();
    void parseStructData();
    void parseTupleData();
    void parseFuncData();
    void parseIf();
    void parseMatch();
    void parseMatchCase();
    void parsePattern();
    void parseTry();
    void parseCatchClause();
    void parseIndexValue();
    void parseKeyValuePair();
    void parsePairedValues(TK closer);
    void parseDataList(TK closer);
    void parseArguments(TK closer);
    void parseIndices();
    void parseParentArgues();
    void parseAngledValues();
    bool literalSignAhead() const {
        if ((cur_ != TK::Minus && cur_ != TK::Plus) ||
            (peek(1) != TK::Integer && peek(1) != TK::Real)) {
            return false;
        }
        // A minus is the sign of the number only when it touches it: `- 2->f` negates `2->f`.
        return cur_ == TK::Plus || !toks_[tree_.tokens_.nextSignificant(pos_ + 1)].spaceBefore();
    }
    bool pairStartsAt(uint32_t n) const {
        return peek(n) == TK::Identifier && peek(n + 1) == TK::Colon;
    }
    bool lambdaAhead() const;

    // ---- Types (parser_type.cpp) ----
    bool typeStartsAt(uint32_t n) const;
    void parseType();
    void parseUnionType();
    void parseInterType();
    void parseDiffType();
    void parseKeyUnionDiffType();
    void parseKeyInterType();
    void parseTypeUnit();
    void parseArrayType();
    void parseSpecType();
    void parsePrimaryType();
    void parseStructType();
    void parseFuncType();
    void parseTypeList(TK closer);
};

constexpr bool isModifier(TK kind) {
    return kind == TK::KwAtomic || kind == TK::KwShared || kind == TK::KwSync ||
           kind == TK::KwMacro;
}

} // namespace camel::parse::syntax
