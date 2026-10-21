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
 * Updated: Oct. 19, 2026
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
#include <string_view>

namespace camel::parse::syntax {

using TK = TokenKind;
using NK = NodeKind;

class ParserImpl {
  public:
    explicit ParserImpl(TokenBuffer tokens);
    Tree run();
    /// One expression, to the end of the buffer (see Parser::parseExpression).
    Tree runExpression();

  private:
    // ---- State ----
    Tree tree_;
    const Token *toks_ = nullptr;
    uint32_t count_    = 0;
    uint32_t pos_      = 0;       // the current token (never trivia)
    uint32_t prev_     = 0;       // the last token consumed
    TK cur_            = TK::Eof; // kind of the current token
    std::vector<uint32_t> stack_; // children of the nodes under construction

    static constexpr uint32_t kNone     = UINT32_MAX;
    static constexpr uint8_t kEmptyNode = 1; // NodeData::flags: the node has no token

    // Layout context (see Scope). `nl_`: a line break before a token that could also start a
    // statement ends the expression. `angle_`: the `<` of the angle-bracket list the parser is
    // directly inside (not inside a bracket or a block nested in it), or kNone.
    bool nl_           = true;
    uint32_t angle_    = kNone;
    bool constData_    = false; // the expression being parsed is a `const` type argument
    uint32_t spacedGt_ = kNone; // a `>` after a space read as a comparison inside angle_
    // The parameter list being parsed belongs to a lambda, where a parameter's type may be
    // left out: it comes from the function type of the position the lambda stands in (design
    // principles 5.9). A function declaration's or a function type's parameters always write
    // their types.
    bool lambdaParams_ = false;

    bool panic_     = false; // an error was reported and the parser has not resynchronized
    uint32_t depth_ = 0;     // nesting of expressions, types and blocks
    static constexpr uint32_t kMaxDepth = 400;

    /// Sets the layout context for the inside of a bracket or a block; restores it at the end.
    struct Scope {
        ParserImpl &p;
        bool nl;
        uint32_t angle;
        uint32_t spacedGt;
        Scope(ParserImpl &parser, bool newNl, uint32_t newAngle = kNone)
            : p(parser), nl(parser.nl_), angle(parser.angle_), spacedGt(parser.spacedGt_) {
            p.nl_       = newNl;
            p.angle_    = newAngle;
            p.spacedGt_ = kNone;
        }
        ~Scope() {
            p.nl_       = nl;
            p.angle_    = angle;
            p.spacedGt_ = spacedGt;
        }
    };
    /// Sets `constData_` for the expression of a `const` type argument, where `constas` may
    /// appear; restores it at the end.
    struct ConstDataScope {
        ParserImpl &p;
        bool old;
        ConstDataScope(ParserImpl &parser, bool inside) : p(parser), old(parser.constData_) {
            p.constData_ = inside;
        }
        ~ConstDataScope() { p.constData_ = old; }
    };
    /// A type position named by the grammar: after `fields` and after `match type`
    /// the head `(` opens a tuple or a parenthesised type, never a parameter list, so `fields ()`
    /// is the empty tuple type (design principles 5.6, H79 ruling 1). The flag is read and
    /// cleared at the head of the type; a nested type keeps the function-type reading.
    bool typeHead_ = false;
    struct TypeHeadScope {
        ParserImpl &p;
        bool old;
        explicit TypeHeadScope(ParserImpl &parser) : p(parser), old(parser.typeHead_) {
            p.typeHead_ = true;
        }
        ~TypeHeadScope() { p.typeHead_ = old; }
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
    void parseBracedImports();
    void parseImportItem();
    void parseExportDecl();
    void parseUsingNamespaceDecl();
    void parseNamespaceDecl();
    void parseFuncAnno();
    void parseFuncDecl();
    void parseFuncDeclRest(size_t m);
    void parseAnnotatedStmt();
    /// The `@` decorators and `#` tag uses before a declaration or a block, in either order.
    void parseFrontMatter();
    /// At `tag` naming a tag declaration (offset `n`): `tag name<...> on ...`, `tag name on ...`
    /// or `tag name = [...]`.
    bool atTagDecl(uint32_t n) const;
    void parseTagDecl();
    /// One `#` form: `#name`, `#name<args>`, or `#[a, b<v>]` (each member a tagRef).
    void parseTagUse();
    /// tagRef : identRef angledValues?
    void parseTagRef();
    /// tagPositions : tagPositionName (',' tagPositionName)* ','?
    void parseTagPositions();
    /// A position name: `func`, `type`, `field`, `nodes`, `param` (design principles 6.2). The
    /// first three are keywords of the language; all five are names here only.
    void parseTagPositionName();
    /// `read_by` names: identRef (',' identRef)*
    void parseTagReaders();
    /// At `operator` followed by an operator symbol, right after `func`: the name of an
    /// operator overload rather than a function named `operator`.
    bool atOperatorName() const;
    void parseOperatorName();
    /// At the name `rule` (offset `n`) that starts a rule declaration.
    bool atRuleDecl(uint32_t n) const;
    void parseRuleDecl();
    /// ruleDecl after the tag uses that may precede it; `m` marks where they began.
    void parseRuleDeclRest(size_t m);
    void parseRuleContext();
    void parseRuleBody();
    /// At the name `analysis` (offset `n`) that starts an analysis declaration.
    bool atAnalysisDecl(uint32_t n) const;
    void parseAnalysisDecl();
    /// analysisDecl after the tags and decorators that may precede it; `m` marks where it began.
    void parseAnalysisDeclRest(size_t m);
    void parseAnalysisClause();
    void parseDataDecl();
    void parseTypeDecl();
    /// typeDecl after the tags and decorators that may precede it; `m` marks where it began.
    void parseTypeDeclRest(size_t m);
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
    void parseEnumType();
    void parseEnumVariant();
    void parseEnumField();
    /// Whether a function declaration follows after the modifiers at the current token.
    uint32_t afterModifiers() const;
    /// The index of the `n`-th token after the current one (0 is the current token), trivia
    /// skipped.
    uint32_t offsetOf(uint32_t n) const {
        uint32_t i = pos_;
        while (n > 0 && toks_[i].kind != TK::Eof) {
            ++i;
            while (isTrivia(toks_[i].kind)) {
                ++i;
            }
            --n;
        }
        return i;
    }
    /// The token at offset `n` is the identifier `word` (a contextual keyword: `tag`, `on` and
    /// `read_by` are not reserved words).
    bool atWord(uint32_t n, std::string_view word) const {
        const uint32_t i = offsetOf(n);
        return toks_[i].kind == TK::Identifier && tree_.tokens_.text(i) == word;
    }
    /// `{` at token offset `n` opens a struct literal: `{ name: ...`, `{ ...`, or a struct
    /// comprehension `{ static for ...` (H51 step 4). Anything else is a block.
    bool braceIsStruct(uint32_t n) const {
        const TK next = peek(n + 1);
        return next == TK::Ellipsis || (next == TK::Identifier && peek(n + 2) == TK::Colon) ||
               (next == TK::KwStatic && peek(n + 2) == TK::KwFor);
    }

    // ---- Expressions (parser_expr.cpp) ----
    bool startsExpr(TK kind) const;
    /// The last closed node is a `RefData` whose path has a `::`: the type of a variant
    /// construction (`Shape::Rect { ... }`) or the constructor of a pattern (`case Shape::Rect …`).
    bool lastRefIsPath() const {
        if (stack_.empty()) {
            return false;
        }
        const uint32_t word = stack_.back();
        if ((word & Tree::kTokenBit) != 0) {
            return false;
        }
        const NodeData &node = tree_.nodes_[word];
        if (node.kind != NK::RefData) {
            return false;
        }
        for (uint32_t token = node.firstToken; token <= node.lastToken; ++token) {
            if (toks_[token].kind == TK::ColonColon) {
                return true;
            }
        }
        return false;
    }
    void parseExpr();
    void parseAssign();
    void parseLogicalOr();
    void parseLogicalAnd();
    void parseEquality();
    void parseRelational();
    void parseAdditive();
    void parseMultiplicative();
    void parsePower();
    void parseNullable();
    void parseUnary();
    void parseLink();
    void parseComp();
    void parseAnno();
    void parseAccess();
    void parsePrimary();
    void parseConstasData();
    void parseLiteral();
    void parseArrayData();
    void parseStructData();
    void parseTupleData();
    void parseFuncData();
    void parseIf();
    void parseMatch();
    void parseMatchType();
    void parseStaticFor();
    void parseStaticForHeader();
    void parseStaticLoop();
    void parseMatchCase();
    void parseTypeCase();
    void parseTypeCasePattern();
    void parseEnumTuplePattern();
    void parseEnumStructPattern();
    void parsePattern();
    void parseTry();
    void parseLoop();
    void parseOr();
    void parseAnd();
    void parseNeg(bool underMinus);
    void parseCatchClause();
    void parseIndexValue();
    void parseKeyValuePair();
    void parsePairedValues(TK closer);
    void parseDataList(TK closer);
    void parseArguments(TK closer);
    void parseIndices();
    void parseSliceIndices();
    void parseSlicePart();
    void parseParentArgues();
    void parseAngledValues();
    void parseTypeArguments();
    bool literalSignAhead() const {
        if ((cur_ != TK::Minus && cur_ != TK::Plus) ||
            (peek(1) != TK::Integer && peek(1) != TK::Real &&
             peek(1) != TK::Imaginary)) {
            return false;
        }
        return true;
    }
    bool pairStartsAt(uint32_t n) const {
        return peek(n) == TK::Identifier && peek(n + 1) == TK::Colon;
    }
    bool lambdaAhead() const;

    // ---- Types (parser_type.cpp) ----
    bool typeStartsAt(uint32_t n) const;
    void parseType();
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
